#include "impl/includes.h"
#include "impl/shader.h"

static ID3D11Device* g_pDevice = nullptr;
static ID3D11DeviceContext* g_pCtx = nullptr;
static IDXGISwapChain* g_pSwap = nullptr;
static ID3D11RenderTargetView* g_pMainRtv = nullptr;

static void CreateMainRtv()
{
    ID3D11Texture2D* pBack = nullptr;
    g_pSwap->GetBuffer(0, IID_PPV_ARGS(&pBack));
    g_pDevice->CreateRenderTargetView(pBack, nullptr, &g_pMainRtv);
    pBack->Release();
}
static void CleanupMainRtv()
{
    if (g_pMainRtv) { g_pMainRtv->Release(); g_pMainRtv = nullptr; }
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
static bool g_Running = true;

static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wp, lp)) return true;
    switch (msg)
    {
    case WM_SIZE:
        if (g_pDevice && wp != SIZE_MINIMIZED)
        {
            CleanupMainRtv();
            g_pSwap->ResizeBuffers(0, LOWORD(lp), HIWORD(lp), DXGI_FORMAT_UNKNOWN, 0);
            CreateMainRtv();
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wp & 0xfff0) == SC_KEYMENU) return 0;
        break;
    case WM_DESTROY:
        PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hWnd, msg, wp, lp);
}

int main()
{
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0, 0,
                       GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr,
                       L"imfx", nullptr };
    RegisterClassExW(&wc);
    HWND hWnd = CreateWindowW(wc.lpszClassName, L"imfx - fluid distortion",
        WS_OVERLAPPEDWINDOW, 80, 80, 1440, 860,
        nullptr, nullptr, nullptr, nullptr);

    // ---- D3D11 -----------------------------------------------------------------
    {
        DXGI_SWAP_CHAIN_DESC sd = {};
        sd.BufferCount = 2;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferDesc.RefreshRate.Numerator = 360;
        sd.BufferDesc.RefreshRate.Denominator = 1;
        sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = hWnd;
        sd.SampleDesc.Count = 1;
        sd.Windowed = TRUE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        UINT devFlags = 0;
#if defined(_DEBUG)
        devFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
        D3D_FEATURE_LEVEL fl;
        const D3D_FEATURE_LEVEL fla[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
        if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
            devFlags, fla, 2, D3D11_SDK_VERSION, &sd, &g_pSwap, &g_pDevice, &fl, &g_pCtx)))
        {
            MessageBoxW(nullptr, L"D3D11CreateDeviceAndSwapChain failed", L"Error", MB_OK);
            return 1;
        }
        CreateMainRtv();
    }

    ShowWindow(hWnd, true);
    UpdateWindow(hWnd);

    // ---- ImGui -----------------------------------------------------------------
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    ImGuiStyle& st = ImGui::GetStyle();
    st.WindowRounding = 6.0f;
    st.FrameRounding = 4.0f;
    ImGui_ImplWin32_Init(hWnd);
    ImGui_ImplDX11_Init(g_pDevice, g_pCtx);

    // ---- ImFX ------------------------------------------------------------------
    ImFX::CreateContext();

    static const ImVec2 kEffectSize = ImVec2(1280.0f, 720.0f);

    ImFX_ImplDX11_InitInfo fxInfo;
    fxInfo.Device = g_pDevice;
    fxInfo.DeviceContext = g_pCtx;
    fxInfo.CreateDefaultEffect = true;      // Effects[0] - the fluid source
    fxInfo.DefaultEffectSize = kEffectSize;
    ImFX_ImplDX11_Init(&fxInfo);

    ImFXEffect* fluid = ImFX::GetContext()->DefaultEffect;
    fluid->Speed = 0.6f;
    fluid->Scale = 2.5f;
    fluid->Density = 0.1f;
    fluid->ColorA = ImVec4(0.04f, 0.02f, 0.10f, 1.0f);
    fluid->ColorB = ImVec4(0.50f, 0.10f, 0.70f, 1.0f);

    // Effects[1] - sine distortion pass, reads fluid SRV at t0
    ImFXEffect* distort = ImFX::CreateEffectFromString(
        g_sine_distort_ps, ImFXShaderLang_HLSL, kEffectSize);
    distort->Speed = 1.0f;
    distort->Scale = 2.0f;
    distort->Density = 0.5f;
    distort->ColorA = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    distort->ColorB = ImVec4(0.05f, 0.05f, 0.15f, 0.6f);

    // ---- State -----------------------------------------------------------------
    bool showRaw = true;

    // ---- Loop ------------------------------------------------------------------
    MSG msg = {};
    while (g_Running)
    {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) g_Running = false;
        }
        if (!g_Running) break;

        // UpdateEffects processes all effects in order.
        // Effects[0] (fluid) renders first and writes its SRV.
        // We then hand that SRV to Effects[1] (distort) - but
        // UpdateEffects has already started its loop by then, so
        // we need to assign BackBufferSrv BEFORE the call and rely
        // on the fact that Effects[0] is processed before Effects[1].
        //
        // The ordering guarantee holds because Effects is a flat
        // array iterated 0..N, so fluid always renders before distort.
        distort->BackBufferSrv = (void*)fluid->GetTexture();

        ImFX_ImplDX11_NewFrame();
        ImFX_ImplDX11_UpdateEffects(ImFX::GetContext());

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ImGuiIO& io = ImGui::GetIO();

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("##Root", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus);

        // status
        {
            bool bbBound = (distort->_BackendFlags & ImFXEffectBackendFlags_BackBufferBound) != 0;
            bool err = (distort->_BackendFlags & ImFXEffectBackendFlags_CompileError) != 0;
            if (err)
                ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "  shader compile error");
            else if (bbBound)
                ImGui::TextColored(ImVec4(0.2f, 1, 0.4f, 1), "  fluid -> distort active");
            else
                ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.2f, 1), "  waiting...");

            ImGui::SameLine(io.DisplaySize.x - 160);
            ImGui::Checkbox("show source", &showRaw);
        }

        ImGui::Separator();
        ImGui::Spacing();

        float availW = ImGui::GetContentRegionAvail().x;
        float availH = ImGui::GetContentRegionAvail().y - 120;
        float ar = kEffectSize.x / kEffectSize.y;

        if (showRaw)
        {
            float pw = (availW - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
            float ph = pw / ar;
            if (ph > availH) { ph = availH; pw = ph * ar; }

            ImGui::BeginGroup();
            ImGui::TextDisabled("source  (Effects[0] fluid)");
            ImGui::Image(fluid->GetTexture(), ImVec2(pw, ph));
            ImGui::GetWindowDrawList()->AddRect(
                ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                IM_COL32(80, 200, 120, 200), 0.f, 0, 2.f);
            ImGui::EndGroup();

            ImGui::SameLine();

            ImGui::BeginGroup();
            ImGui::TextDisabled("output  (Effects[1] sine distort)");
            ImGui::Image(distort->GetTexture(), ImVec2(pw, ph));
            ImGui::GetWindowDrawList()->AddRect(
                ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                IM_COL32(80, 140, 255, 200), 0.f, 0, 2.f);
            ImGui::EndGroup();
        }
        else
        {
            float ph = availW / ar;
            if (ph > availH) { ph = availH; availW = ph * ar; }
            ImGui::TextDisabled("output  (sine distorted fluid)");
            ImGui::Image(distort->GetTexture(), ImVec2(availW, ph));
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // fluid source params
        ImGui::TextDisabled("fluid source");
        float cw = 160.0f;
        ImGui::SetNextItemWidth(cw); ImGui::SliderFloat("Speed##f", &fluid->Speed, 0.0f, 4.0f);
        ImGui::SameLine(0, 20);
        ImGui::SetNextItemWidth(cw); ImGui::SliderFloat("Scale##f", &fluid->Scale, 0.1f, 8.0f);
        ImGui::SameLine(0, 20);
        ImGui::SetNextItemWidth(cw); ImGui::SliderFloat("Density##f", &fluid->Density, -0.5f, 0.5f);
        ImGui::SameLine(0, 20);
        ImGui::SetNextItemWidth(200.0f); ImGui::ColorEdit4("ColorB##f", &fluid->ColorB.x);

        ImGui::Spacing();

        // distort params
        ImGui::TextDisabled("distortion");
        ImGui::SetNextItemWidth(cw); ImGui::SliderFloat("Speed##d", &distort->Speed, 0.0f, 4.0f);
        ImGui::SameLine(0, 20);
        ImGui::SetNextItemWidth(cw); ImGui::SliderFloat("Scale##d", &distort->Scale, 0.1f, 8.0f);
        ImGui::SameLine(0, 20);
        ImGui::SetNextItemWidth(cw); ImGui::SliderFloat("Amplitude##d", &distort->Density, 0.0f, 1.0f);
        ImGui::SameLine(0, 20);
        ImGui::SetNextItemWidth(200.0f); ImGui::ColorEdit4("Edge tint##d", &distort->ColorB.x);

        ImGui::End();

        float bg[4] = { 0.05f, 0.05f, 0.07f, 1.0f };
        g_pCtx->OMSetRenderTargets(1, &g_pMainRtv, nullptr);
        g_pCtx->ClearRenderTargetView(g_pMainRtv, bg);
        D3D11_VIEWPORT vp = { 0, 0, io.DisplaySize.x, io.DisplaySize.y, 0, 1 };
        g_pCtx->RSSetViewports(1, &vp);
        ImGui::Render();
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_pSwap->Present(1, 0);
    }

    // ---- Cleanup ---------------------------------------------------------------
    ImFX_ImplDX11_Shutdown();
    ImFX::DestroyContext();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupMainRtv();
    if (g_pSwap) { g_pSwap->Release(); }
    if (g_pCtx) { g_pCtx->Release(); }
    if (g_pDevice) { g_pDevice->Release(); }
    DestroyWindow(hWnd);
    return 0;
}