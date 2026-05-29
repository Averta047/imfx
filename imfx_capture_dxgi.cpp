#include "imfx_capture_dxgi.h"
#include <stdio.h>

#pragma comment(lib, "dxgi.lib")

ImFXCapture_DXGI::ImFXCapture_DXGI()
{
    memset(_LastError, 0, sizeof(_LastError));
}

bool ImFXCapture_DXGI::Init(ID3D11Device* pDevice, ID3D11DeviceContext* pCtx, UINT monitorIndex)
{
    _pDevice = pDevice;
    _pCtx    = pCtx;

    // ---- Get the DXGI device from the D3D11 device -------------------------
    IDXGIDevice* pDxgiDevice = nullptr;
    if (FAILED(pDevice->QueryInterface(IID_PPV_ARGS(&pDxgiDevice))))
    {
        snprintf(_LastError, sizeof(_LastError), "QueryInterface IDXGIDevice failed");
        return false;
    }

    // ---- Walk adapter -> output --------------------------------------------
    IDXGIAdapter* pAdapter = nullptr;
    HRESULT hr = pDxgiDevice->GetParent(IID_PPV_ARGS(&pAdapter));
    pDxgiDevice->Release();
    if (FAILED(hr))
    {
        snprintf(_LastError, sizeof(_LastError), "GetParent IDXGIAdapter failed (hr=0x%08X)", hr);
        return false;
    }

    IDXGIOutput* pOutput = nullptr;
    hr = pAdapter->EnumOutputs(monitorIndex, &pOutput);
    pAdapter->Release();
    if (FAILED(hr))
    {
        snprintf(_LastError, sizeof(_LastError),
            "EnumOutputs(%u) failed - monitor index out of range? (hr=0x%08X)", monitorIndex, hr);
        return false;
    }

    // ---- QI to IDXGIOutput1 for DuplicateOutput ----------------------------
    IDXGIOutput1* pOutput1 = nullptr;
    hr = pOutput->QueryInterface(IID_PPV_ARGS(&pOutput1));
    pOutput->Release();
    if (FAILED(hr))
    {
        snprintf(_LastError, sizeof(_LastError),
            "QI IDXGIOutput1 failed - Windows 8+ required (hr=0x%08X)", hr);
        return false;
    }

    // ---- Store monitor dimensions for reference ----------------------------
    DXGI_OUTPUT_DESC outDesc = {};
    pOutput1->GetDesc(&outDesc);
    _MonitorW = (UINT)(outDesc.DesktopCoordinates.right  - outDesc.DesktopCoordinates.left);
    _MonitorH = (UINT)(outDesc.DesktopCoordinates.bottom - outDesc.DesktopCoordinates.top);

    // ---- Create the duplication object -------------------------------------
    // Common failure modes:
    //   DXGI_ERROR_NOT_CURRENTLY_AVAILABLE - another app holds duplication,
    //     or this is a Remote Desktop session.
    //   E_ACCESSDENIED - running without PROCESS_DPI_AWARENESS, rare.
    hr = pOutput1->DuplicateOutput(pDevice, &_pDuplication);
    pOutput1->Release();
    if (FAILED(hr))
    {
        if (hr == DXGI_ERROR_NOT_CURRENTLY_AVAILABLE)
            snprintf(_LastError, sizeof(_LastError),
                "DuplicateOutput: DXGI_ERROR_NOT_CURRENTLY_AVAILABLE\n"
                "Another application may already be using desktop duplication,\n"
                "or this is a Remote Desktop / virtual machine session.");
        else if (hr == E_ACCESSDENIED)
            snprintf(_LastError, sizeof(_LastError),
                "DuplicateOutput: E_ACCESSDENIED - try running as administrator");
        else
            snprintf(_LastError, sizeof(_LastError),
                "DuplicateOutput failed (hr=0x%08X)", hr);
        return false;
    }

    // Pre-allocate the staging texture at monitor resolution
    RecreateStaging(_MonitorW, _MonitorH);

    snprintf(_LastError, sizeof(_LastError), "OK");
    return true;
}

void ImFXCapture_DXGI::Shutdown()
{
    if (_FrameHeld && _pDuplication)
    {
        _pDuplication->ReleaseFrame();
        _FrameHeld = false;
    }
    DestroyStaging();
    if (_pDuplication) { _pDuplication->Release(); _pDuplication = nullptr; }
    _pDevice = nullptr;
    _pCtx    = nullptr;
}

void ImFXCapture_DXGI::RecreateStaging(UINT w, UINT h)
{
    DestroyStaging();

    // Must be BGRA to match the desktop duplication format.
    // BIND_SHADER_RESOURCE so we can make an SRV directly.
    D3D11_TEXTURE2D_DESC td = {};
    td.Width            = w;
    td.Height           = h;
    td.MipLevels        = 1;
    td.ArraySize        = 1;
    td.Format           = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage            = D3D11_USAGE_DEFAULT;
    td.BindFlags        = D3D11_BIND_SHADER_RESOURCE;

    if (FAILED(_pDevice->CreateTexture2D(&td, nullptr, &_pStagingTex)))
        return;

    _pDevice->CreateShaderResourceView(_pStagingTex, nullptr, &_pStagingSrv);
}

void ImFXCapture_DXGI::DestroyStaging()
{
    if (_pStagingSrv) { _pStagingSrv->Release(); _pStagingSrv = nullptr; }
    if (_pStagingTex) { _pStagingTex->Release(); _pStagingTex = nullptr; }
}

ID3D11ShaderResourceView* ImFXCapture_DXGI::AcquireFrame(UINT timeoutMs)
{
    if (!_pDuplication || !_pStagingTex)
        return nullptr;

    // Should not call AcquireFrame twice without ReleaseFrame in between
    if (_FrameHeld)
    {
        _pDuplication->ReleaseFrame();
        _FrameHeld = false;
    }

    DXGI_OUTDUPL_FRAME_INFO frameInfo = {};
    IDXGIResource* pDesktopResource   = nullptr;

    HRESULT hr = _pDuplication->AcquireNextFrame(timeoutMs, &frameInfo, &pDesktopResource);

    if (hr == DXGI_ERROR_WAIT_TIMEOUT)
        return nullptr;  // No new frame this tick - caller reuses last effect output

    if (hr == DXGI_ERROR_ACCESS_LOST)
    {
        // Desktop mode change (resolution, DPI, fullscreen toggle).
        // Re-init is required; signal the caller via nullptr.
        snprintf(_LastError, sizeof(_LastError), "AcquireNextFrame: ACCESS_LOST - call Shutdown()/Init() again");
        _pDuplication->Release();
        _pDuplication = nullptr;
        return nullptr;
    }

    if (FAILED(hr))
    {
        snprintf(_LastError, sizeof(_LastError), "AcquireNextFrame failed (hr=0x%08X)", hr);
        return nullptr;
    }

    _FrameHeld = true;

    // ---- Get the desktop texture -------------------------------------------
    ID3D11Texture2D* pDesktopTex = nullptr;
    hr = pDesktopResource->QueryInterface(IID_PPV_ARGS(&pDesktopTex));
    pDesktopResource->Release();

    if (FAILED(hr))
        return nullptr;

    // ---- Check if resolution changed (e.g. DPI scaling event) -------------
    D3D11_TEXTURE2D_DESC desktopDesc = {};
    pDesktopTex->GetDesc(&desktopDesc);

    if (desktopDesc.Width != _MonitorW || desktopDesc.Height != _MonitorH)
    {
        _MonitorW = desktopDesc.Width;
        _MonitorH = desktopDesc.Height;
        RecreateStaging(_MonitorW, _MonitorH);
    }

    // ---- Copy desktop texture -> SRV-capable staging texture ---------------
    // CopyResource is a GPU-side blit - no CPU round-trip.
    // The desktop texture has no SRV bind flag so we can't use it directly.
    if (_pStagingTex)
        _pCtx->CopyResource(_pStagingTex, pDesktopTex);

    pDesktopTex->Release();

    return _pStagingSrv;  // valid until next ReleaseFrame()
}

void ImFXCapture_DXGI::ReleaseFrame()
{
    if (_FrameHeld && _pDuplication)
    {
        _pDuplication->ReleaseFrame();
        _FrameHeld = false;
    }
}