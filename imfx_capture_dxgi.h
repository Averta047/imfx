//================================================================//
//
//  Author:     Averta047 x Claude Sonnet 4.6
//  Purpose:    DXGI Output Duplication desktop capture helper
//
//  Usage (per frame, before ImFX_ImplDX11_UpdateEffects):
//
//    // Init once
//    ImFXCapture_DXGI cap;
//    cap.Init(pDevice, pDeviceContext);
//
//    // Each frame
//    ID3D11ShaderResourceView* srv = cap.AcquireFrame();
//    if (srv)
//        myEffect->BackBufferSrv = srv;   // ImFX consumes + nulls it
//    cap.ReleaseFrame();                  // always call, even if AcquireFrame returned null
//
//  Notes
//  -----
//  - Output Duplication captures the entire monitor (monitor 0 by
//    default; pass a monitor index to Init).
//  - The returned SRV is valid only until the next ReleaseFrame().
//    Do not cache it across frames.
//  - First-frame latency: DWM may not have composited a new frame
//    yet.  AcquireFrame returns nullptr in that case; the previous
//    effect output remains visible, which is fine.
//  - Requires Windows 8+ and a hardware DX11 device.
//  - Will fail on remote desktop sessions (DWM duplication is
//    blocked by RDP).  Check Init() return value.
//  - The captured texture is DXGI_FORMAT_B8G8R8A8_UNORM (desktop
//    format). The effect RT is R8G8B8A8_UNORM.  Sampling across
//    formats works correctly in HLSL - the hardware swizzles.
//
//================================================================//

#pragma once

#include <d3d11.h>
#include <dxgi1_2.h>

struct ImFXCapture_DXGI
{
    //------------------------------------------------------------------
    // Init / Shutdown
    //------------------------------------------------------------------

    // monitorIndex: 0 = primary monitor.
    // Returns false if output duplication is unavailable on this system
    // (remote desktop, unsupported driver, etc.).
    bool Init(ID3D11Device* pDevice, ID3D11DeviceContext* pCtx, UINT monitorIndex = 0);
    void Shutdown();

    //------------------------------------------------------------------
    // Per-frame
    //------------------------------------------------------------------

    // Tries to acquire the latest desktop frame.
    // Returns an SRV pointing at an internal staging texture that has
    // been filled with the captured frame, or nullptr if no new frame
    // was available this tick (caller should skip assignment and reuse
    // last frame's effect output).
    // timeoutMs: how long to wait for a new frame (0 = don't wait,
    //   just return nullptr immediately if nothing is ready).
    ID3D11ShaderResourceView* AcquireFrame(UINT timeoutMs = 0);

    // Must be called once per frame after UpdateEffects, whether or
    // not AcquireFrame returned a valid SRV.
    void ReleaseFrame();

    //------------------------------------------------------------------
    // State query
    //------------------------------------------------------------------
    bool        IsReady()    const { return _pDuplication != nullptr; }
    const char* LastError()  const { return _LastError; }
    UINT        MonitorW()   const { return _MonitorW; }
    UINT        MonitorH()   const { return _MonitorH; }

    ImFXCapture_DXGI();
    ~ImFXCapture_DXGI() { Shutdown(); }

private:
    void RecreateStaging(UINT w, UINT h);
    void DestroyStaging();

    ID3D11Device*               _pDevice        = nullptr;
    ID3D11DeviceContext*        _pCtx           = nullptr;
    IDXGIOutputDuplication*     _pDuplication   = nullptr;

    // Staging path: desktop tex -> resolve copy -> SRV-capable tex -> SRV
    // We need the copy because the duplicated texture is not
    // SRV-bindable (it has no D3D11_BIND_SHADER_RESOURCE flag).
    ID3D11Texture2D*            _pStagingTex    = nullptr;
    ID3D11ShaderResourceView*   _pStagingSrv    = nullptr;

    bool    _FrameHeld  = false;    // true between AcquireFrame and ReleaseFrame
    UINT    _MonitorW   = 0;
    UINT    _MonitorH   = 0;
    char    _LastError[256] = {};
};
