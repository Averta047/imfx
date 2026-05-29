#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

#include <include/imgui/imgui.h>
#include <include/imgui/imgui_internal.h>
#include <include/imgui/backends/imgui_impl_win32.h>
#include <include/imgui/backends/imgui_impl_dx11.h>

#include <include/imfx/imfx.h>
#include <include/imfx/imfx_impl_dx11.h>
#include <include/imfx/imfx_capture_dxgi.h>
