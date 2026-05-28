# imfx

**imfx** is a lightweight shader-driven animated texture framework designed as an addon for **[Dear ImGui](https://github.com/ocornut/imgui)** It allows you to feed pixel shaders from strings, files, or raw byte blobs into a global context, modulate parameters in real-time, and bake them into standard textures (`ImTextureID`) for immediate rendering within your ImGui UI layouts

> **Note:** This is just a fun little side project I put together with the help of Claude 4.6 Sonnet. I'm mainly just throwing it up here to share with the community, so feel free to play around with it!

Co-authored by **[Averta047](https://github.com/Averta047)** and **Claude 4.6 Sonnet**

## Features

* **Works with any renderer** The core layout and effect settings do not care what graphics API you use, making it completely independent
* **Easy shader loading** You can load your shader code directly from regular strings, files on your disk, or raw binary files
* **Matches ImGui style** The setup, context creation, and loop steps match the exact design style of standard ImGui modules like `CreateContext`, `NewFrame`, and `Shutdown` while exclusively using ImGui's built-in types and wrapped utils
* **Includes a default effect** It comes with a built-in cloudy smoke shader
* **DirectX 11 sample included** Shipped with a working DX11 backend wrapper

## Known Limitations

* **Shaders depend on the renderer language** Even though the main context system does not care about your graphics API, the shader code you pass to it does (controlled by the `ImFXShaderLang` enum) If you switch rendering backends, you have to update your shader code language accordingly
* **Only DirectX 11 / HLSL supported for now** Because this was built as a quick side project, I didn't bother adding support for other renderer backends or shader languages yet

## Showcase

https://github.com/user-attachments/assets/0936f231-716c-4d7a-85f2-9a20ef0d3762

https://github.com/user-attachments/assets/9d9b697e-95e1-4c17-9b08-7e9c40559915

## Usage Example

### 1 Initialization
```cpp
// Backend free part, init our effect context where we store the shader code & array of effects + result texture
ImFX::CreateContext();

// Initialize effect backend
ImFX_ImplDX11_InitInfo info;
info.Device = pDevice;
info.DeviceContext = pDeviceContext;
info.CreateDefaultEffect = true; // Maps to Effects[0] & DefaultEffect, which is a built-in cloudy smoke effect
info.DefaultEffectSize = ImVec2(1280.0f, 720.0f);
ImFX_ImplDX11_Init(&info);

// Tweak default shader-effect params
if (info.CreateDefaultEffect)
{
    ImFXEffect* def = ImFX::GetContext()->DefaultEffect;
    def->Speed = 0.6f;
    def->Scale = 2.5f;
    def->Density = 0.1f;
    def->ColorA = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    def->ColorB = ImVec4(0.30f, 0.30f, 0.30f, 0.5f); // Usually the background, depends on the effect
}

// Push a new effect into the Effects vector, following the familiar ImGui pattern
ImFXEffect* voronoi = ImFX::CreateEffectFromString(g_VoronoiShader, ImFXShaderLang_HLSL, ImVec2(800.0f, 600.0f));

// Tweak custom effect params dynamically
voronoi->Speed = 1.2f;
voronoi->Scale = 3.5f;
voronoi->Density = 0.3f;
voronoi->ColorA = ImVec4(0.02f, 0.02f, 0.08f, 1.0f);  // Near-black background
voronoi->ColorB = ImVec4(0.20f, 0.60f, 0.90f, 1.0f);  // Cyan glow
```

### 2 Frame Update
```cpp
// Update ImFX framework before the main ImGui frame update
ImFX_ImplDX11_NewFrame();
ImFX_ImplDX11_UpdateEffects(ImFX::GetContext());

// Standard ImGui backend update
ImGui_ImplDX11_NewFrame();
ImGui_ImplWin32_NewFrame();
```

### 3 Rendering
```cpp
// Render the default effect (Index 0 when CreateDefaultEffect is enabled)
ImFXEffect* def = ImFX::GetContext()->DefaultEffect;
ImGui::Image(def->GetTexture(), def->Size);

// Render subsequent custom effects by accessing the context vector index directly
ImFXEffect* voronoi = ImFX::GetContext()->Effects[1];
ImGui::Image(voronoi->GetTexture(), voronoi->Size);
```

### 4 Cleanup
```cpp
// Clean up the framework right before your standard ImGui cleanup sequence
ImFX_ImplDX11_Shutdown();
ImFX::DestroyContext();

// Standard ImGui cleanup
ImGui_ImplDX11_Shutdown();
ImGui_ImplWin32_Shutdown();
ImGui::DestroyContext();
```

## Future Ideas
* **Texture Inputs:** Adding support for passing custom textures directly into the shader parameters
* **Extra Custom Parameters:** Expanding the parameter so you can add pass more data to your effects (could be usefull for complex effects)

## License

This is free and unencumbered piece of code released into the public domain, you can do whatever you want with it :)
