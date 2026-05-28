//================================================================//
//
//	Author:     Averta047 x Cluade Sonnet 4.6
//	Purpose:	ImFX DX11 backend
//  Setup:
//              0 - ImFX_ImplDX11_InitInfo info;
//              1 - info.Device        = pDevice;
//              2 - info.DeviceContext = pContext;
//              3 - ImFX_ImplDX11_Init(&info);
//
//  Per frame (before ImGui::NewFrame):
//              0 - ImFX_ImplDX11_NewFrame();
//              1 - ImFX_ImplDX11_UpdateEffects(ImFX::GetContext());
//
// Shutdown:
//              0 - ImFX_ImplDX11_Shutdown();
//
//================================================================//

#pragma once

#include "imfx.h"
#include <d3d11.h>

//================================================================//
// ImFX_ImplDX11_InitInfo
// Passed to ImFX_ImplDX11_Init - mirrors ImGui_ImplDX11_Init style.
//================================================================//
struct ImFX_ImplDX11_InitInfo
{
    ID3D11Device*           Device;
    ID3D11DeviceContext*    DeviceContext;

    // When true (default), the backend automatically creates a built-in
    // fluid effect and registers it as ImFX::GetContext()->DefaultEffect.
    bool    CreateDefaultEffect;
    ImVec2  DefaultEffectSize;

    ImFX_ImplDX11_InitInfo()
    {
        Device               = nullptr;
        DeviceContext        = nullptr;
        CreateDefaultEffect  = true;
        DefaultEffectSize    = ImVec2(512.0f, 512.0f);
    }
};

//================================================================//
// Backend API
//================================================================//

// Init / Shutdown
IMGUI_API bool  ImFX_ImplDX11_Init(ImFX_ImplDX11_InitInfo* info);
IMGUI_API void  ImFX_ImplDX11_Shutdown();

// Call once per frame before ImGui::NewFrame.
// Advances internal time accumulator.
IMGUI_API void  ImFX_ImplDX11_NewFrame();

// Renders every effect in the context into its own texture.
// Call after ImFX_ImplDX11_NewFrame, before ImGui::NewFrame.
// After this, effect->GetTexture() is valid until the next call.
IMGUI_API void  ImFX_ImplDX11_UpdateEffects(ImFXContext* ctx);
