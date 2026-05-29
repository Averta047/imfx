# imfx

**imfx** is a lightweight shader-driven animated texture framework designed as an addon for **[Dear ImGui](https://github.com/ocornut/imgui)**. It allows you to feed pixel shaders from strings, files, or raw byte blobs into a global context, modulate parameters in real-time, and bake them into standard textures (`ImTextureID`) for immediate rendering within your ImGui UI layouts.

> **Note:** This is just a fun little side project I put together with the help of Claude Sonnet 4.6. I'm mainly just throwing it up here to share with the community, so feel free to play around with it!

Co-authored by **[Averta047](https://github.com/Averta047)** and **Claude Sonnet 4.6**

---

## Features

- **Works with any renderer** — The core layout and effect settings do not care what graphics API you use, making it completely backend-independent
- **Easy shader loading** — Load shader code from raw strings, files on disk, or pre-compiled binary blobs (CSO)
- **Matches ImGui style** — Context creation, frame update, and shutdown follow the exact same pattern as standard ImGui modules
- **Built-in default effect** — Ships with a cloudy fluid smoke shader ready to use out of the box
- **BackBuffer input** — Pass any texture (a live captured frame, another effect's output, a scene render target) directly into your shader via `BackBufferSrv`
- **MSAA** — Render targets are created with the best MSAA level supported by the device
- **DirectX 11 backend included** — Fully working DX11 backend wrapper with DXGI desktop capture helper

---

## Known Limitations

- **Shaders depend on the renderer language** — The shader code you pass must match the graphics API in use, controlled by the `ImFXShaderLang` enum. Switching backends means updating your shader source
- **Only DirectX 11 / HLSL supported for now** — Other backends and shader languages are future work

---

## Showcase

https://github.com/user-attachments/assets/0936f231-716c-4d7a-85f2-9a20ef0d3762

https://github.com/user-attachments/assets/9d9b697e-95e1-4c17-9b08-7e9c40559915

---

## Project Structure

```
imfx/
├── imfx.h                          # Core context, effect struct, public API
├── imfx.cpp                        # Context and effect management
├── imfx_impl_dx11.h                # DX11 backend header
├── imfx_impl_dx11.cpp              # DX11 backend implementation
├── imfx_capture_dxgi.h             # DXGI desktop capture helper header
├── imfx_capture_dxgi.cpp           # DXGI desktop capture helper implementation
└── samples/
    ├── shader_backbuffer/          # GPU-generated texture fed into a distortion shader
    │   ├── main.cpp
    │   └── impl/
    │       ├── includes.h
    │       └── shader.h
    └── dxgi_backbuffer/            # Live desktop capture fed into a distortion shader
        ├── main.cxx
        └── impl/
            ├── includes.h
            └── shader.h
```

---

## Usage

### 1 — Initialization

```cpp
// Create the ImFX context (call before any effect creation)
ImFX::CreateContext();

// Initialize the DX11 backend
ImFX_ImplDX11_InitInfo info;
info.Device              = pDevice;
info.DeviceContext       = pDeviceContext;
info.CreateDefaultEffect = true;                    // Creates Effects[0], the built-in fluid smoke
info.DefaultEffectSize   = ImVec2(1280.0f, 720.0f);
ImFX_ImplDX11_Init(&info);

// Tweak default effect params
if (info.CreateDefaultEffect)
{
    ImFXEffect* def = ImFX::GetContext()->DefaultEffect;
    def->Speed   = 0.6f;
    def->Scale   = 2.5f;
    def->Density = 0.1f;
    def->ColorA  = ImVec4(0.0f,  0.0f,  0.0f,  0.0f);
    def->ColorB  = ImVec4(0.30f, 0.30f, 0.30f, 0.5f);
}

// Register a custom effect from a shader string
ImFXEffect* voronoi = ImFX::CreateEffectFromString(
    g_VoronoiShader, ImFXShaderLang_HLSL, ImVec2(800.0f, 600.0f));

voronoi->Speed   = 1.2f;
voronoi->Scale   = 3.5f;
voronoi->Density = 0.3f;
voronoi->ColorA  = ImVec4(0.02f, 0.02f, 0.08f, 1.0f);
voronoi->ColorB  = ImVec4(0.20f, 0.60f, 0.90f, 1.0f);
```

You can also load from a file or a pre-compiled CSO blob:

```cpp
ImFXEffect* fromFile  = ImFX::CreateEffectFromFile("shaders/noise.hlsl", ImFXShaderLang_HLSL, size);
ImFXEffect* fromBytes = ImFX::CreateEffectFromBytes(csoData, csoSize, ImFXShaderLang_CSO, size);
```

---

### 2 — Frame Update

```cpp
// ImFX must update before ImGui's frame starts
ImFX_ImplDX11_NewFrame();
ImFX_ImplDX11_UpdateEffects(ImFX::GetContext());

// Then the standard ImGui frame
ImGui_ImplDX11_NewFrame();
ImGui_ImplWin32_NewFrame();
ImGui::NewFrame();
```

---

### 3 — Rendering

```cpp
// Default built-in effect
ImFXEffect* def = ImFX::GetContext()->DefaultEffect;
ImGui::Image(def->GetTexture(), def->Size);

// Any custom effect by index
ImFXEffect* voronoi = ImFX::GetContext()->Effects[1];
ImGui::Image(voronoi->GetTexture(), voronoi->Size);
```

---

### 4 — Cleanup

```cpp
// ImFX shutdown before standard ImGui shutdown
ImFX_ImplDX11_Shutdown();
ImFX::DestroyContext();

// Standard ImGui cleanup
ImGui_ImplDX11_Shutdown();
ImGui_ImplWin32_Shutdown();
ImGui::DestroyContext();
```

---

## BackBuffer Input

Every `ImFXEffect` has a `BackBufferSrv` field. Assign any `ID3D11ShaderResourceView*` to it before `UpdateEffects` and the backend binds it to `t0` / `s0` inside your pixel shader for that frame. The pointer is consumed and cleared to `nullptr` after the draw, so you must re-assign every frame you want it bound.

```cpp
// Assign before UpdateEffects - consumed and cleared after the draw
myEffect->BackBufferSrv = (void*)pSomeSrv;

ImFX_ImplDX11_NewFrame();
ImFX_ImplDX11_UpdateEffects(ImFX::GetContext());
```

Inside your HLSL, declare the input at `t0` / `s0`:

```hlsl
Texture2D    BackBuffer  : register(t0);
SamplerState BackSampler : register(s0);

float4 main(PS_IN i) : SV_TARGET
{
    float4 scene = BackBuffer.Sample(BackSampler, i.uv);
    // distort, tint, blur, composite...
    return scene;
}
```

You can confirm it was bound by checking the backend flag:

```cpp
bool bound = (myEffect->_BackendFlags & ImFXEffectBackendFlags_BackBufferBound) != 0;
```

Two patterns are shown in the samples:

---

### Pattern A — Shader as BackBuffer (`samples/shader_backbuffer`)

Chain two effects together. The first effect renders a procedural pattern (the built-in fluid smoke); its output SRV is fed into a second effect that distorts it with a sine-wave warp.

```
Effects[0]  fluid smoke  ──► GetTexture()
                                  │
                          BackBufferSrv (assigned before UpdateEffects)
                                  │
                                  ▼
Effects[1]  sine distort  ──► GetTexture()  ──►  ImGui::Image()
```

Because `UpdateEffects` processes effects in array order, `Effects[0]` always renders before `Effects[1]` in the same call, so assigning `Effects[0]->GetTexture()` before the call gives `Effects[1]` the current frame's output with no lag.

```cpp
// Before UpdateEffects each frame:
distort->BackBufferSrv = (void*)fluid->GetTexture();

ImFX_ImplDX11_NewFrame();
ImFX_ImplDX11_UpdateEffects(ImFX::GetContext());
```

---

### Pattern B — DXGI Desktop Capture as BackBuffer (`samples/dxgi_backbuffer`)

Use `ImFXCapture_DXGI` to grab the live desktop frame and feed it into a shader effect. This uses `IDXGIOutputDuplication`, the same API used by screen recorders and overlays — the desktop frame arrives as a GPU texture with no CPU round-trip.

```
Desktop (DWM compositor output)
         │
  IDXGIOutputDuplication::AcquireNextFrame()
         │
  CopyResource() ──► staging texture (BIND_SHADER_RESOURCE)
         │
  BackBufferSrv
         │
         ▼
ImFX effect  ──►  GetTexture()  ──►  ImGui::Image()
         │
  ReleaseFrame()   ← must call AFTER UpdateEffects
```

```cpp
ImFXCapture_DXGI capture;
capture.Init(pDevice, pCtx, 0 /*primary monitor*/);

// Each frame, before UpdateEffects:
ID3D11ShaderResourceView* srv = capture.AcquireFrame();
if (srv)
    myEffect->BackBufferSrv = (void*)srv;

ImFX_ImplDX11_NewFrame();
ImFX_ImplDX11_UpdateEffects(ImFX::GetContext());

capture.ReleaseFrame(); // always call, even if AcquireFrame returned nullptr
```

`ImFXCapture_DXGI` handles resolution changes (`DXGI_ERROR_ACCESS_LOST`) automatically if you check `IsReady()` after `AcquireFrame` and call `Shutdown()` / `Init()` again when it returns false.

> **Note:** DXGI Output Duplication requires Windows 8+ and a hardware DX11 device. It will fail on Remote Desktop sessions — check `Init()`'s return value and surface `LastError()` if it does.

---

## cbuffer Layout

All effects share the same constant buffer layout. Your pixel shader must declare it exactly as shown:

```hlsl
cbuffer ImFXParamCB : register(b0)
{
    float2 Resolution;  // render target size in pixels
    float  Time;        // accumulated time in seconds
    float  Speed;       // effect->Speed
    float  Scale;       // effect->Scale
    float  Density;     // effect->Density
    float2 _Pad;
    float4 ColorA;      // effect->ColorA
    float4 ColorB;      // effect->ColorB
};
```

`BackBuffer` / `BackSampler` at `t0` / `s0` are optional — only declare them if you assign `BackBufferSrv`.

---

## Future Ideas

- **Texture inputs** — Pass custom textures directly as named shader parameters
- **Extra custom parameters** — Expand the cbuffer so effects can receive arbitrary user data
- **OpenGL / Vulkan backends** — GLSL support when a new backend is added

---

## License

This is free and unencumbered code released into the public domain. Do whatever you want with it.