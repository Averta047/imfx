//================================================================//
//
//	Author:     Averta047 x Cluade Sonnet 4.6
//	Purpose:	High-level context and effect management
//
//================================================================//

#include "imfx.h"
#include "imgui_internal.h"     // ImStrdup, ImFileOpen/Read/Close, IM_NEW, IM_DELETE, IM_ASSERT

//================================================================//
// Internal global context  (mirrors GImGui)
//================================================================//
static ImFXContext* GImFX = nullptr;

//================================================================//
// ImFXEffect
//================================================================//
ImFXEffect::ImFXEffect()
{
    SourceType    = ImFXShaderSourceType_String;
    ShaderLang    = ImFXShaderLang_HLSL;
    SourceSize    = 0;
    Size          = ImVec2(512.0f, 512.0f);
    Speed         = 1.0f;
    Scale         = 2.0f;
    Density       = 0.0f;
    ColorA        = ImVec4(0.05f, 0.10f, 0.40f, 1.0f);
    ColorB        = ImVec4(0.60f, 0.10f, 0.50f, 1.0f);
    _BackendFlags = ImFXEffectBackendFlags_None;
    _BackendData  = nullptr;
    _TextureID    = ImTextureID_Invalid;
    BackBufferSrv = nullptr;
}

//================================================================//
// ImFXContext
//================================================================//
ImFXContext::ImFXContext()
{
    DefaultEffect             = nullptr;
    _BackendRendererUserData  = nullptr;
}

ImFXContext::~ImFXContext()
{
    // Free all owned effects
    for (int i = 0; i < Effects.Size; i++)
        IM_DELETE(Effects[i]);
    Effects.clear();
    DefaultEffect = nullptr;
}

//================================================================//
// ImFX namespace
//================================================================//
namespace ImFX
{

ImFXContext* CreateContext()
{
    ImFXContext* ctx = IM_NEW(ImFXContext);
    if (GImFX == nullptr)
        GImFX = ctx;
    return ctx;
}

void DestroyContext(ImFXContext* ctx)
{
    if (ctx == nullptr)
        ctx = GImFX;
    IM_ASSERT(ctx != nullptr && "ImFX: no context to destroy");
    if (GImFX == ctx)
        GImFX = nullptr;
    IM_DELETE(ctx);
}

ImFXContext* GetContext()
{
    return GImFX;
}

void SetContext(ImFXContext* ctx)
{
    GImFX = ctx;
}

//----------------------------------------------------------------//
// Internal helpers
//----------------------------------------------------------------//
static ImFXEffect* CreateEffectBase(ImFXShaderSourceType type, ImFXShaderLang lang, ImVec2 size)
{
    IM_ASSERT(GImFX != nullptr && "ImFX: call ImFX::CreateContext() before creating effects");

    ImFXEffect* effect = IM_NEW(ImFXEffect);
    effect->SourceType = type;
    effect->ShaderLang = lang;
    effect->Size       = size;

    GImFX->Effects.push_back(effect);
    return effect;
}

static void CopySourceString(ImFXEffect* effect, const char* src)
{
    int len = (int)ImStrlen(src) + 1;  // include null terminator
    effect->SourceBuffer.resize(len);
    ImStrncpy(effect->SourceBuffer.Data, src, len);
    effect->SourceSize = (size_t)len;
}

//----------------------------------------------------------------//
// CreateEffectFromString
//----------------------------------------------------------------//
ImFXEffect* CreateEffectFromString(const char* hlslSrc, ImFXShaderLang lang, ImVec2 size)
{
    IM_ASSERT(hlslSrc != nullptr && "ImFX: shader source string is null");

    ImFXEffect* effect = CreateEffectBase(ImFXShaderSourceType_String, lang, size);
    CopySourceString(effect, hlslSrc);
    return effect;
}

//----------------------------------------------------------------//
// CreateEffectFromFile
// Reads the entire file into SourceBuffer at creation time so the
// backend gets a self-contained buffer identical to the String path.
//----------------------------------------------------------------//
ImFXEffect* CreateEffectFromFile(const char* path, ImFXShaderLang lang, ImVec2 size)
{
    IM_ASSERT(path != nullptr && "ImFX: shader file path is null");

    ImFXEffect* effect = CreateEffectBase(ImFXShaderSourceType_File, lang, size);

    // Store the path so callers can identify the source later
    CopySourceString(effect, path);

    // Also load the file contents into a secondary buffer for the backend.
    // We re-use SourceBuffer: first we store the path for diagnostics,
    // then overwrite with file data.
    ImFileHandle f = ImFileOpen(path, "rb");
    if (f == nullptr)
    {
        IM_ASSERT(false && "ImFX: failed to open shader file");
        effect->_BackendFlags |= ImFXEffectBackendFlags_CompileError;
        return effect;
    }

    ImU64 fileSize = ImFileGetSize(f);
    effect->SourceBuffer.resize((int)fileSize + 1);
    ImFileRead(effect->SourceBuffer.Data, 1, fileSize, f);
    effect->SourceBuffer[(int)fileSize] = '\0';
    effect->SourceSize = (size_t)fileSize;
    ImFileClose(f);

    return effect;
}

//----------------------------------------------------------------//
// CreateEffectFromBytes
// For pre-compiled CSO blobs - copied as-is, no null terminator.
//----------------------------------------------------------------//
ImFXEffect* CreateEffectFromBytes(const void* data, size_t dataSize, ImFXShaderLang lang, ImVec2 size)
{
    IM_ASSERT(data != nullptr && dataSize > 0 && "ImFX: invalid byte blob");

    ImFXEffect* effect = CreateEffectBase(ImFXShaderSourceType_Bytes, lang, size);
    effect->SourceBuffer.resize((int)dataSize);
    memcpy(effect->SourceBuffer.Data, data, dataSize);
    effect->SourceSize = dataSize;
    return effect;
}

} // namespace ImFX
