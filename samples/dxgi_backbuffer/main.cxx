#include "impl/includes.h"
#include "impl/shader.h"

static ID3D11Device* g_pDevice = nullptr;
static ID3D11DeviceContext* g_pCtx = nullptr;
static IDXGISwapChain* g_pSwap = nullptr;
static ID3D11RenderTargetView* g_pMainRtv = nullptr;

static void CreateRenderTarget()
{
    ID3D11Texture2D* pBack = nullptr;
    g_pSwap->GetBuffer(0, IID_PPV_ARGS(&pBack));
    g_pDevice->CreateRenderTargetView(pBack, nullptr, &g_pMainRtv);
    pBack->Release();
}
static void CleanupRenderTarget()
{
    if (g_pMainRtv) { g_pMainRtv->Release(); g_pMainRtv = nullptr; }
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
static bool g_Running = true;
static ImFXCapture_DXGI* g_pCapture = nullptr;

static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wp, lp)) return true;
    switch (msg)
    {
    case WM_SIZE:
        if (g_pDevice && wp != SIZE_MINIMIZED)
        {
            CleanupRenderTarget();
            g_pSwap->ResizeBuffers(0, LOWORD(lp), HIWORD(lp), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wp & 0xfff0) == SC_KEYMENU) return 0;
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wp, lp);
}

int main()
{
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0, 0,
                       GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr,
                       L"imfx", nullptr };
    RegisterClassExW(&wc);
    HWND hWnd = CreateWindowW(wc.lpszClassName, L"sample",
        WS_OVERLAPPEDWINDOW, 80, 80, 1440, 860,
        nullptr, nullptr, nullptr, nullptr);

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
        if (FAILED(D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
            devFlags, fla, 2, D3D11_SDK_VERSION,
            &sd, &g_pSwap, &g_pDevice, &fl, &g_pCtx)))
        {
            MessageBoxW(nullptr, L"D3D11CreateDeviceAndSwapChain failed", L"Error", MB_OK);
            return 1;
        }
        CreateRenderTarget();
    }

    ShowWindow(hWnd, true);
    UpdateWindow(hWnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    ImGuiStyle& st = ImGui::GetStyle();
    st.WindowRounding = 6.0f;
    st.FrameRounding = 4.0f;

    ImGui_ImplWin32_Init(hWnd);
    ImGui_ImplDX11_Init(g_pDevice, g_pCtx);

    ImFXCapture_DXGI capture;
    g_pCapture = &capture;
    bool captureOk = capture.Init(g_pDevice, g_pCtx, 0);

    ImFX::CreateContext();

    ImFX_ImplDX11_InitInfo fxInfo;
    fxInfo.Device = g_pDevice;
    fxInfo.DeviceContext = g_pCtx;
    fxInfo.CreateDefaultEffect = false;
    ImFX_ImplDX11_Init(&fxInfo);

    ImVec2 effectSize = captureOk
        ? ImVec2((float)capture.MonitorW(), (float)capture.MonitorH())
        : ImVec2(1920.0f, 1080.0f);

    ImFXEffect* depth = ImFX::CreateEffectFromString(
        g_depth_shader, ImFXShaderLang_HLSL, effectSize);

    bool  captureEnabled = true;
    bool  showSideBySide = true;
    bool  showRawCapture = true;
    ID3D11ShaderResourceView* pLastSrv = nullptr;

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

        ID3D11ShaderResourceView* pFrameSrv = nullptr;

        if (captureOk && captureEnabled)
        {
            pFrameSrv = capture.AcquireFrame(0);

            if (!capture.IsReady())
            {
                capture.Shutdown();
                captureOk = capture.Init(g_pDevice, g_pCtx, 0);
                pFrameSrv = nullptr;
            }

            if (pFrameSrv)
                pLastSrv = pFrameSrv;
            else
                pFrameSrv = pLastSrv;
        }

        if (pFrameSrv)
            depth->BackBufferSrv = (void*)pFrameSrv;

        ImFX_ImplDX11_NewFrame();
        ImFX_ImplDX11_UpdateEffects(ImFX::GetContext());
        capture.ReleaseFrame();

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ImGuiIO& io = ImGui::GetIO();

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("##Root", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus);

        {
            bool bbBound = (depth->_BackendFlags & ImFXEffectBackendFlags_BackBufferBound) != 0;

            if (!captureOk)
            {
                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "  capture failed:");
                ImGui::SameLine();
                ImGui::TextUnformatted(capture.LastError());
            }
            else if (!captureEnabled)
            {
                ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.2f, 1.0f), "  capture stopped");
            }
            else if (bbBound)
            {
                ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f),
                    "  capturing  |  %ux%u  |  BackBufferSrv bound",
                    capture.MonitorW(), capture.MonitorH());
            }
            else
            {
                ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.2f, 1.0f), "  waiting for frame...");
            }

            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 340);
            ImGui::Checkbox("capture desktop", &captureEnabled);
            ImGui::SameLine();
            ImGui::Checkbox("show capture frame", &showRawCapture);
        }

        ImGui::Separator();
        ImGui::Spacing();

        float availW = ImGui::GetContentRegionAvail().x;
        float availH = ImGui::GetContentRegionAvail().y - 130;

        float monAr = (capture.MonitorH() > 0)
            ? (float)capture.MonitorW() / (float)capture.MonitorH()
            : 16.0f / 9.0f;

        if (showSideBySide && showRawCapture && pLastSrv)
        {
            float panelW = (availW - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
            float panelH = panelW / monAr;
            if (panelH > availH) { panelH = availH; panelW = panelH * monAr; }

            ImGui::BeginGroup();
            ImGui::TextDisabled("input frame");
            ImGui::Image((ImTextureID)pLastSrv, ImVec2(panelW, panelH));
            ImVec2 tl = ImGui::GetItemRectMin(), br = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddRect(tl, br, IM_COL32(80, 200, 120, 200), 0., 0, 2.);
            ImGui::EndGroup();

            ImGui::SameLine();

            ImGui::BeginGroup();
            ImGui::TextDisabled("output frame");
            ImGui::Image(depth->GetTexture(), ImVec2(panelW, panelH));
            tl = ImGui::GetItemRectMin(); br = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddRect(tl, br, IM_COL32(80, 140, 255, 200), 0.f, 0, 2.f);
            ImGui::EndGroup();
        }
        else
        {
            float imgH = availW / monAr;
            if (imgH > availH) { imgH = availH; availW = imgH * monAr; }
            ImGui::TextDisabled("output frame");
            ImGui::Image(depth->GetTexture(), ImVec2(availW, imgH));
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        float colW = 180.0f;
        ImGui::SetNextItemWidth(colW);
        ImGui::SliderFloat("Speed", &depth->Speed, 0.0f, 4.0f);
        ImGui::SameLine(0, 20);
        ImGui::SetNextItemWidth(colW);
        ImGui::SliderFloat("Scale", &depth->Scale, 0.1f, 8.0f);
        ImGui::SameLine(0, 20);
        ImGui::SetNextItemWidth(colW);
        ImGui::SliderFloat("Amplitude", &depth->Density, 0.0f, 1.0f);
        ImGui::SameLine(0, 20);
        ImGui::SetNextItemWidth(220.0f);
        ImGui::ColorEdit4("Edge tint", &depth->ColorB.x);

        ImGui::End();

        float bg[4] = { 0.05f, 0.05f, 0.07f, 1.0f };
        g_pCtx->OMSetRenderTargets(1, &g_pMainRtv, nullptr);
        g_pCtx->ClearRenderTargetView(g_pMainRtv, bg);

        D3D11_VIEWPORT vp = {};
        vp.Width = io.DisplaySize.x;
        vp.Height = io.DisplaySize.y;
        vp.MaxDepth = 1.0f;
        g_pCtx->RSSetViewports(1, &vp);

        ImGui::Render();
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_pSwap->Present(1, 0);
    }

    g_pCapture = nullptr;
    capture.Shutdown();

    ImFX_ImplDX11_Shutdown();
    ImFX::DestroyContext();

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupRenderTarget();
    if (g_pSwap) { g_pSwap->Release(); }
    if (g_pCtx) { g_pCtx->Release(); }
    if (g_pDevice) { g_pDevice->Release(); }
    DestroyWindow(hWnd);
    return 0;
}
