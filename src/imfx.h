//================================================================//
//
//	Author:     Averta047 x Cluade Sonnet 4.6
//	Purpose:	Shader-driven animated texture addon for Dear ImGui
//  Node:       See imfx_impl_xxx.h for backend setup
//
//================================================================//

#pragma once

#include "imgui.h"

//================================================================//
// Enums
//================================================================//

// Shader language - passed explicitly by the user at effect creation.
// Only HLSL is supported for now; additional languages can be added
// when new backends are introduced.
enum ImFXShaderLang
{
    ImFXShaderLang_Unknown  = 0,
    ImFXShaderLang_HLSL     = 1,    // DX11 / DX12
    ImFXShaderLang_CSO      = 2,    // Pre-compiled HLSL blob (Bytes path only)
};

// How the shader source is stored inside the effect.
enum ImFXShaderSourceType
{
    ImFXShaderSourceType_String = 0,    // Raw HLSL source in memory
    ImFXShaderSourceType_File   = 1,    // Path on disk; backend loads and compiles
    ImFXShaderSourceType_Bytes  = 2,    // Pre-compiled blob (CSO)
};

// Flags written by the backend onto each effect.
// Read-only from user code.
enum ImFXEffectBackendFlags_
{
    ImFXEffectBackendFlags_None         = 0,
    ImFXEffectBackendFlags_Ready        = 1 << 0,   // Compiled & render target created
    ImFXEffectBackendFlags_Incompatible = 1 << 1,   // Wrong shader lang for this backend
    ImFXEffectBackendFlags_CompileError = 1 << 2,   // Shader compilation failed
    ImFXEffectBackendFlags_SizeDirty    = 1 << 3,   // Size changed, RT needs rebuild
};
typedef int ImFXEffectBackendFlags;

//================================================================//
// ImFXEffect
// Represents one animated shader effect.
// Create via ImFX::CreateEffectFromString / File / Bytes.
// Set params freely every frame before ImFX_ImplDX11_UpdateEffects.
//================================================================//
struct ImFXEffect
{
    //------------------------------------------------------------------
    // Source  (owned by this struct; deep-copied at creation)
    //------------------------------------------------------------------
    ImFXShaderSourceType    SourceType;
    ImFXShaderLang          ShaderLang;
    ImVector<char>          SourceBuffer;   // Holds code string, file path, or raw bytes
    size_t                  SourceSize;     // Byte count - only meaningful for Bytes type

    //------------------------------------------------------------------
    // Params  (set freely every frame, read by backend each update)
    //------------------------------------------------------------------
    ImVec2  Size;       // Render target size in pixels
    float   Speed;      // Animation playback multiplier     (default 1.0)
    float   Scale;      // Spatial frequency / zoom          (default 2.0)
    float   Density;    // Noise density bias in [-0.5, 0.5] (default 0.0, no bias)
    ImVec4  ColorA;     // Primary / low colour  (RGBA)
    ImVec4  ColorB;     // Secondary / high colour (RGBA)

    //------------------------------------------------------------------
    // Output  (written by backend after each render)
    //------------------------------------------------------------------
    ImTextureID GetTexture() const { return _TextureID; }

    //------------------------------------------------------------------
    // Internal  (backend fills these - do not modify)
    //------------------------------------------------------------------
    ImFXEffectBackendFlags  _BackendFlags;
    void*                   _BackendData;   // Cast: ImFX_ImplDX11_EffectData*
    ImTextureID             _TextureID;     // SRV written by backend

    IMGUI_API ImFXEffect();
};

//================================================================//
// ImFXContext
// Global context holding all registered effects.
// Mirrors ImGuiContext - one per application.
//================================================================//
struct ImFXContext
{
    ImVector<ImFXEffect*>   Effects;
    ImFXEffect*             DefaultEffect;          // Built-in fluid effect, index 0
    void*                   _BackendRendererUserData; // Owned by backend (ImFX_ImplDX11_Data*)

    IMGUI_API ImFXContext();
    IMGUI_API ~ImFXContext();
};

//================================================================//
// ImFX namespace - high-level API
//================================================================//
namespace ImFX
{
    // Context
    IMGUI_API ImFXContext*  CreateContext();
    IMGUI_API void          DestroyContext(ImFXContext* ctx = nullptr);   // nullptr = current
    IMGUI_API ImFXContext*  GetContext();
    IMGUI_API void          SetContext(ImFXContext* ctx);

    // Effect creation - mirrors ImFontAtlas::AddFont* pattern.
    // The effect is owned by the context and freed in DestroyContext.
    IMGUI_API ImFXEffect*   CreateEffectFromString(const char* hlslSrc,  ImFXShaderLang lang, ImVec2 size);
    IMGUI_API ImFXEffect*   CreateEffectFromFile  (const char* path,     ImFXShaderLang lang, ImVec2 size);
    IMGUI_API ImFXEffect*   CreateEffectFromBytes (const void* data, size_t dataSize, ImFXShaderLang lang, ImVec2 size);

} // namespace ImFX
