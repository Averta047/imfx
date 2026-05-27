//================================================================//
//
//	Author:     Averta047 x Cluade Sonnet 4.6
//	Purpose:	ImFX DX11 backend implementation
//
//================================================================//

#include "imfx_impl_dx11.h"
#include "imgui_internal.h"

#include <d3dcompiler.h>
#pragma comment(lib, "d3dcompiler.lib")

//================================================================//
// Default built-in shaders
//================================================================//

static const char* g_ImFXDefaultVertexSrc = R"(
    struct VS_OUTPUT
    {
        float4 pos : SV_POSITION;
        float2 uv  : TEXCOORD0;
    };

    VS_OUTPUT main(uint id : SV_VertexID)
    {
        float2 uv = float2((id << 1) & 2, id & 2);
        VS_OUTPUT o;
        o.uv  = uv;
        o.pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
        return o;
    }
)";

// Built-in fluid effect - used by DefaultEffect.
// To swap: call ImFX::CreateEffectFromString with your own src.
static const char* g_ImFXDefaultPixelSrc = R"(
    cbuffer ImFXParamCB : register(b0)
    {
        float2 Resolution;
        float  Time;
        float  Speed;
        float  Scale;
        float  Density;
        float2 _Pad;
        float4 ColorA;
        float4 ColorB;
    };

    struct PS_INPUT
    {
        float4 pos : SV_POSITION;
        float2 uv  : TEXCOORD0;
    };

    float hash21(float2 p)
    {
        p = frac(p * float2(127.1, 311.7));
        p += dot(p, p + 17.5);
        return frac(p.x * p.y);
    }

    float vnoise(float2 p)
    {
        float2 i = floor(p);
        float2 f = frac(p);
        float2 u = f * f * (3.0 - 2.0 * f);
        return lerp(
            lerp(hash21(i),               hash21(i + float2(1, 0)), u.x),
            lerp(hash21(i + float2(0, 1)), hash21(i + float2(1, 1)), u.x),
            u.y);
    }

    float fbm(float2 p)
    {
        float v = 0.0, a = 0.5;
        [unroll]
        for (int o = 0; o < 5; ++o)
        {
            v += a * vnoise(p);
            p  = p * 2.03 + float2(1.7, 9.2);
            a *= 0.5;
        }
        return v;
    }

    float4 main(PS_INPUT i) : SV_TARGET
    {
        float  t  = Time * Speed * 0.4;
        float  ar = Resolution.x / max(Resolution.y, 1.0);
        float2 p  = i.uv * float2(ar, 1.0) * Scale;

        float2 warp = float2(fbm(p + float2(0.0, t)),
                             fbm(p + float2(5.2, t + 1.3)));

        float  n     = fbm(p + warp + float2(t * 0.5, 0.0));
        float  s     = saturate(n + Density);
        float3 col   = lerp(ColorA.rgb, ColorB.rgb, s);
        float  alpha = lerp(ColorA.a,   ColorB.a,   s);

        float2 vig = i.uv * 2.0 - 1.0;
        col *= 1.0 - dot(vig, vig) * 0.15;

        return float4(saturate(col), alpha);
    }
)";

//================================================================//
// Internal structs
//================================================================//

// CPU-side mirror of the HLSL constant buffer.
// Layout must match cbuffer ImFXParamCB exactly.
struct alignas(16) ImFX_ImplDX11_ParamCB
{
    float   Resolution[2];  // 8
    float   Time;           // 4
    float   Speed;          // 4  -> row 0 end
    float   Scale;          // 4
    float   Density;        // 4
    float   _Pad[2];        // 8  -> row 1 end
    float   ColorA[4];      // 16 -> row 2 end
    float   ColorB[4];      // 16 -> row 3 end
};
static_assert(sizeof(ImFX_ImplDX11_ParamCB) == 64, "ImFX_ImplDX11_ParamCB must be 64 bytes");

// Per-effect D3D11 resources.
// Lives behind ImFXEffect::_BackendData as a void*.
// Allocated lazily on first UpdateEffects call for that effect.
struct ImFX_ImplDX11_EffectData
{
    ID3D11Texture2D*            ResolveTarget;  // 1x sample, SRV source
    ID3D11ShaderResourceView*   Srv;            // Exposed via effect->GetTexture()
    ID3D11Texture2D*            MsaaTarget;     // MSAA render target
    ID3D11RenderTargetView*     MsaaRtv;
    UINT                        SampleCount;

    ID3D11PixelShader*          Ps;             // Compiled from effect's SourceBuffer
    ID3D11Buffer*               ParamCb;

    float                       Time;
    ImVec2                      LastSize;       // Detect size changes -> RT rebuild

    ImFX_ImplDX11_EffectData()
    {
        memset(this, 0, sizeof(*this));
        SampleCount = 1;
    }
};

// Global backend state - one per application.
// Stored in ImFXContext::_BackendRendererUserData.
struct ImFX_ImplDX11_Data
{
    ID3D11Device*               Device;
    ID3D11DeviceContext*        DeviceContext;

    // Shared across all effects
    ID3D11VertexShader*         CommonVs;
    ID3D11RasterizerState*      RsState;
    ID3D11DepthStencilState*    DsState;
    ID3D11BlendState*           BlendState;

    static const DXGI_FORMAT    RTFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

    ImFX_ImplDX11_Data()
    {
        memset(this, 0, sizeof(*this));
    }
};

//================================================================//
// Internal helpers
//================================================================//

static ImFX_ImplDX11_Data* ImFX_ImplDX11_GetBackendData()
{
    ImFXContext* ctx = ImFX::GetContext();
    return ctx ? (ImFX_ImplDX11_Data*)ctx->_BackendRendererUserData : nullptr;
}

static bool ImFX_ImplDX11_CompileShader(
    const char*         pSrc,
    const char*         pEntry,
    const char*         pTarget,
    ID3DBlob**          ppBlob)
{
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif
    ID3DBlob* pError = nullptr;
    HRESULT hr = D3DCompile(
        pSrc, strlen(pSrc), nullptr, nullptr, nullptr,
        pEntry, pTarget, flags, 0, ppBlob, &pError
    );

    if (FAILED(hr) && pError)
    {
        // Surface the HLSL error message via IM_ASSERT so it shows
        // in debug output without a hard crash
        IM_ASSERT_USER_ERROR(false, (const char*)pError->GetBufferPointer());
        pError->Release();
    }
    else if (pError)
    {
        pError->Release();
    }

    return SUCCEEDED(hr);
}

static bool ImFX_ImplDX11_CreateRenderTarget(ImFX_ImplDX11_Data* bd, ImFX_ImplDX11_EffectData* ed, ImVec2 size)
{
    UINT w = (UINT)size.x;
    UINT h = (UINT)size.y;

    IM_ASSERT(w > 0 && h > 0 && "ImFX: effect Size must be > 0");

    // Find best supported MSAA level
    ed->SampleCount = 1;
    for (UINT s = 4; s > 1; s >>= 1)
    {
        UINT q = 0;
        if (SUCCEEDED(bd->Device->CheckMultisampleQualityLevels(bd->RTFormat, s, &q)) && q > 0)
        {
            ed->SampleCount = s;
            break;
        }
    }

    // Resolve target - 1x sample, SRV-capable
    {
        D3D11_TEXTURE2D_DESC d = {};
        d.Width             = w;
        d.Height            = h;
        d.MipLevels         = 1;
        d.ArraySize         = 1;
        d.Format            = bd->RTFormat;
        d.SampleDesc.Count  = 1;
        d.Usage             = D3D11_USAGE_DEFAULT;
        d.BindFlags         = D3D11_BIND_SHADER_RESOURCE;

        if (FAILED(bd->Device->CreateTexture2D(&d, nullptr, &ed->ResolveTarget)))
            return false;

        D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
        sd.Format                    = bd->RTFormat;
        sd.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
        sd.Texture2D.MipLevels       = 1;

        if (FAILED(bd->Device->CreateShaderResourceView(ed->ResolveTarget, &sd, &ed->Srv)))
            return false;
    }

    // MSAA render target
    {
        D3D11_TEXTURE2D_DESC d = {};
        d.Width                 = w;
        d.Height                = h;
        d.MipLevels             = 1;
        d.ArraySize             = 1;
        d.Format                = bd->RTFormat;
        d.SampleDesc.Count      = ed->SampleCount;
        d.SampleDesc.Quality    = 0;
        d.Usage                 = D3D11_USAGE_DEFAULT;
        d.BindFlags             = D3D11_BIND_RENDER_TARGET;

        if (FAILED(bd->Device->CreateTexture2D(&d, nullptr, &ed->MsaaTarget)))
            return false;

        D3D11_RENDER_TARGET_VIEW_DESC rd = {};
        rd.Format        = bd->RTFormat;
        rd.ViewDimension = (ed->SampleCount > 1)
                            ? D3D11_RTV_DIMENSION_TEXTURE2DMS
                            : D3D11_RTV_DIMENSION_TEXTURE2D;

        if (FAILED(bd->Device->CreateRenderTargetView(ed->MsaaTarget, &rd, &ed->MsaaRtv)))
            return false;
    }

    ed->LastSize = size;
    return true;
}

static void ImFX_ImplDX11_DestroyRenderTarget(ImFX_ImplDX11_EffectData* ed)
{
    if (ed->MsaaRtv)      { ed->MsaaRtv->Release();      ed->MsaaRtv      = nullptr; }
    if (ed->MsaaTarget)   { ed->MsaaTarget->Release();   ed->MsaaTarget   = nullptr; }
    if (ed->Srv)          { ed->Srv->Release();           ed->Srv          = nullptr; }
    if (ed->ResolveTarget){ ed->ResolveTarget->Release(); ed->ResolveTarget= nullptr; }
}

static bool ImFX_ImplDX11_InitEffectData(ImFX_ImplDX11_Data* bd, ImFXEffect* effect, ImFX_ImplDX11_EffectData* ed)
{
    // Compile pixel shader from effect's source buffer
    const char* pSrc = effect->SourceBuffer.Data;
    IM_ASSERT(pSrc != nullptr && "ImFX: effect has no shader source");

    ID3DBlob* pBlob = nullptr;
    if (!ImFX_ImplDX11_CompileShader(pSrc, "main", "ps_5_0", &pBlob))
    {
        effect->_BackendFlags |= ImFXEffectBackendFlags_CompileError;
        return false;
    }

    HRESULT hr = bd->Device->CreatePixelShader(
        pBlob->GetBufferPointer(), pBlob->GetBufferSize(), nullptr, &ed->Ps);
    pBlob->Release();

    if (FAILED(hr))
    {
        effect->_BackendFlags |= ImFXEffectBackendFlags_CompileError;
        return false;
    }

    // Per-effect constant buffer
    {
        ImFX_ImplDX11_ParamCB zero = {};
        D3D11_BUFFER_DESC d        = {};
        d.ByteWidth                = sizeof(ImFX_ImplDX11_ParamCB);
        d.Usage                    = D3D11_USAGE_DEFAULT;
        d.BindFlags                = D3D11_BIND_CONSTANT_BUFFER;

        D3D11_SUBRESOURCE_DATA init = {};
        init.pSysMem                = &zero;

        if (FAILED(bd->Device->CreateBuffer(&d, &init, &ed->ParamCb)))
            return false;
    }

    if (!ImFX_ImplDX11_CreateRenderTarget(bd, ed, effect->Size))
        return false;

    effect->_BackendFlags |= ImFXEffectBackendFlags_Ready;
    return true;
}

static void ImFX_ImplDX11_DestroyEffectData(ImFXEffect* effect)
{
    if (effect->_BackendData == nullptr)
        return;

    ImFX_ImplDX11_EffectData* ed = (ImFX_ImplDX11_EffectData*)effect->_BackendData;
    ImFX_ImplDX11_DestroyRenderTarget(ed);

    if (ed->Ps)      { ed->Ps->Release();      ed->Ps      = nullptr; }
    if (ed->ParamCb) { ed->ParamCb->Release(); ed->ParamCb = nullptr; }

    IM_DELETE(ed);
    effect->_BackendData  = nullptr;
    effect->_TextureID    = ImTextureID_Invalid;
    effect->_BackendFlags = ImFXEffectBackendFlags_None;
}

//================================================================//
// Public API
//================================================================//

bool ImFX_ImplDX11_Init(ImFX_ImplDX11_InitInfo* info)
{
    IM_ASSERT(info != nullptr);
    IM_ASSERT(info->Device != nullptr && "ImFX_ImplDX11_Init: Device is null");
    IM_ASSERT(info->DeviceContext != nullptr && "ImFX_ImplDX11_Init: DeviceContext is null");

    ImFXContext* ctx = ImFX::GetContext();
    IM_ASSERT(ctx != nullptr && "ImFX_ImplDX11_Init: call ImFX::CreateContext() first");
    IM_ASSERT(ctx->_BackendRendererUserData == nullptr && "ImFX_ImplDX11_Init: already initialized");

    ImFX_ImplDX11_Data* bd = IM_NEW(ImFX_ImplDX11_Data);
    bd->Device        = info->Device;
    bd->DeviceContext = info->DeviceContext;

    ctx->_BackendRendererUserData = bd;

    // ---- Compile shared vertex shader ----------------------------------------
    {
        ID3DBlob* pBlob = nullptr;
        if (!ImFX_ImplDX11_CompileShader(g_ImFXDefaultVertexSrc, "main", "vs_5_0", &pBlob))
        {
            IM_ASSERT(false && "ImFX_ImplDX11_Init: failed to compile built-in vertex shader");
            return false;
        }
        HRESULT hr = bd->Device->CreateVertexShader(
            pBlob->GetBufferPointer(), pBlob->GetBufferSize(), nullptr, &bd->CommonVs);
        pBlob->Release();
        if (FAILED(hr)) return false;
    }

    // ---- Shared pipeline states ----------------------------------------------
    {
        D3D11_RASTERIZER_DESC d = {};
        d.FillMode = D3D11_FILL_SOLID;
        d.CullMode = D3D11_CULL_NONE;
        if (FAILED(bd->Device->CreateRasterizerState(&d, &bd->RsState))) return false;
    }
    {
        D3D11_DEPTH_STENCIL_DESC d = {};
        d.DepthEnable   = FALSE;
        d.StencilEnable = FALSE;
        if (FAILED(bd->Device->CreateDepthStencilState(&d, &bd->DsState))) return false;
    }
    {
        // Opaque blend - shader outputs RGBA directly including alpha.
        // ImGui::Image() composites using its own blend state.
        D3D11_BLEND_DESC d = {};
        d.RenderTarget[0].BlendEnable           = FALSE;
        d.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        if (FAILED(bd->Device->CreateBlendState(&d, &bd->BlendState))) return false;
    }

    // ---- Default built-in effect ---------------------------------------------
    if (info->CreateDefaultEffect)
    {
        ImFXEffect* def = ImFX::CreateEffectFromString(
            g_ImFXDefaultPixelSrc, ImFXShaderLang_HLSL, info->DefaultEffectSize);
        ctx->DefaultEffect = def;
    }

    return true;
}

void ImFX_ImplDX11_Shutdown()
{
    ImFX_ImplDX11_Data* bd = ImFX_ImplDX11_GetBackendData();
    IM_ASSERT(bd != nullptr && "ImFX_ImplDX11_Shutdown: backend not initialized");

    ImFXContext* ctx = ImFX::GetContext();

    // Destroy per-effect D3D11 resources
    if (ctx)
        for (int i = 0; i < ctx->Effects.Size; i++)
            ImFX_ImplDX11_DestroyEffectData(ctx->Effects[i]);

    if (bd->BlendState) { bd->BlendState->Release(); bd->BlendState = nullptr; }
    if (bd->DsState)    { bd->DsState->Release();    bd->DsState    = nullptr; }
    if (bd->RsState)    { bd->RsState->Release();    bd->RsState    = nullptr; }
    if (bd->CommonVs)   { bd->CommonVs->Release();   bd->CommonVs   = nullptr; }

    if (ctx)
        ctx->_BackendRendererUserData = nullptr;

    IM_DELETE(bd);
}

void ImFX_ImplDX11_NewFrame()
{
    ImFX_ImplDX11_Data* bd = ImFX_ImplDX11_GetBackendData();
    IM_ASSERT(bd != nullptr && "ImFX_ImplDX11_NewFrame: call ImFX_ImplDX11_Init() first");
    // Reserved for future per-frame global state resets
}

void ImFX_ImplDX11_UpdateEffects(ImFXContext* ctx)
{
    IM_ASSERT(ctx != nullptr && "ImFX_ImplDX11_UpdateEffects: context is null");

    ImFX_ImplDX11_Data* bd = ImFX_ImplDX11_GetBackendData();
    IM_ASSERT(bd != nullptr && "ImFX_ImplDX11_UpdateEffects: backend not initialized");

    ID3D11DeviceContext* dc  = bd->DeviceContext;
    const float          dt  = ImGui::GetIO().DeltaTime;

    // ---- Save caller render state -------------------------------------------
    ID3D11RenderTargetView* pOldRtv = nullptr;
    ID3D11DepthStencilView* pOldDsv = nullptr;
    D3D11_VIEWPORT          oldVp   = {};
    UINT                    uVpCount = 1;
    dc->OMGetRenderTargets(1, &pOldRtv, &pOldDsv);
    dc->RSGetViewports(&uVpCount, &oldVp);

    // ---- Set shared pipeline state once -------------------------------------
    dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    dc->IASetInputLayout(nullptr);
    dc->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
    dc->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
    dc->VSSetShader(bd->CommonVs, nullptr, 0);
    dc->RSSetState(bd->RsState);
    dc->OMSetDepthStencilState(bd->DsState, 0);
    float blendFactor[4] = {};
    dc->OMSetBlendState(bd->BlendState, blendFactor, 0xFFFFFFFF);

    // ---- Process each effect ------------------------------------------------
    for (int i = 0; i < ctx->Effects.Size; i++)
    {
        ImFXEffect* effect = ctx->Effects[i];

        // Skip effects flagged incompatible or failed to compile
        if (effect->_BackendFlags & ImFXEffectBackendFlags_Incompatible)
            continue;
        if (effect->_BackendFlags & ImFXEffectBackendFlags_CompileError)
            continue;

        // Validate shader language against this backend
        if (effect->ShaderLang != ImFXShaderLang_HLSL &&
            effect->ShaderLang != ImFXShaderLang_CSO  &&
            effect->ShaderLang != ImFXShaderLang_Unknown)
        {
            effect->_BackendFlags |= ImFXEffectBackendFlags_Incompatible;
            continue;
        }

        // Lazy-init per-effect D3D11 resources
        if (effect->_BackendData == nullptr)
        {
            ImFX_ImplDX11_EffectData* ed = IM_NEW(ImFX_ImplDX11_EffectData);
            effect->_BackendData = ed;

            if (!ImFX_ImplDX11_InitEffectData(bd, effect, ed))
                continue;
        }

        ImFX_ImplDX11_EffectData* ed = (ImFX_ImplDX11_EffectData*)effect->_BackendData;

        // Rebuild render target if size changed
        if (effect->Size.x != ed->LastSize.x || effect->Size.y != ed->LastSize.y)
        {
            ImFX_ImplDX11_DestroyRenderTarget(ed);
            if (!ImFX_ImplDX11_CreateRenderTarget(bd, ed, effect->Size))
                continue;
        }

        // Advance time
        ed->Time += dt;

        // Upload constant buffer
        {
            ImFX_ImplDX11_ParamCB cb = {};
            cb.Resolution[0] = effect->Size.x;
            cb.Resolution[1] = effect->Size.y;
            cb.Time          = ed->Time;
            cb.Speed         = effect->Speed;
            cb.Scale         = effect->Scale;
            cb.Density       = effect->Density;
            cb.ColorA[0]     = effect->ColorA.x;
            cb.ColorA[1]     = effect->ColorA.y;
            cb.ColorA[2]     = effect->ColorA.z;
            cb.ColorA[3]     = effect->ColorA.w;
            cb.ColorB[0]     = effect->ColorB.x;
            cb.ColorB[1]     = effect->ColorB.y;
            cb.ColorB[2]     = effect->ColorB.z;
            cb.ColorB[3]     = effect->ColorB.w;
            dc->UpdateSubresource(ed->ParamCb, 0, nullptr, &cb, 0, 0);
        }

        // Bind effect render target
        dc->OMSetRenderTargets(1, &ed->MsaaRtv, nullptr);

        D3D11_VIEWPORT vp = {};
        vp.Width    = effect->Size.x;
        vp.Height   = effect->Size.y;
        vp.MaxDepth = 1.0f;
        dc->RSSetViewports(1, &vp);

        // Bind effect pixel shader + constant buffer
        dc->PSSetShader(ed->Ps, nullptr, 0);
        dc->PSSetConstantBuffers(0, 1, &ed->ParamCb);

        // Draw fullscreen triangle - VS generates it from SV_VertexID
        dc->Draw(3, 0);

        // Resolve MSAA into the SRV-compatible texture
        if (ed->SampleCount > 1)
            dc->ResolveSubresource(ed->ResolveTarget, 0, ed->MsaaTarget, 0, bd->RTFormat);
        else
            dc->CopySubresourceRegion(ed->ResolveTarget, 0, 0, 0, 0, ed->MsaaTarget, 0, nullptr);

        // Write output texture ID - user reads this via effect->GetTexture()
        effect->_TextureID = (ImTextureID)ed->Srv;
    }

    // ---- Restore caller render state ----------------------------------------
    dc->OMSetRenderTargets(1, &pOldRtv, pOldDsv);
    if (uVpCount > 0)
        dc->RSSetViewports(1, &oldVp);

    // Release the refs OMGetRenderTargets added
    if (pOldRtv) pOldRtv->Release();
    if (pOldDsv) pOldDsv->Release();
}
