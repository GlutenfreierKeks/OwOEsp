#ifndef PCH_H
#define PCH_H

#include "framework.h"

#ifdef _WIN32
#include <d3d11.h>
#include <dxgi.h>
#include <dwmapi.h>
#endif
#include <cstdint>
#include <cmath>

#ifdef _WIN32
#include "MinHook.h"
#endif

// Courtesy maths operators for ImVec2/ImVec4 (used across the UI code)
#define IMGUI_DEFINE_MATH_OPERATORS

#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#ifdef _WIN32
#include "imgui/backends/imgui_impl_win32.h"
#include "imgui/backends/imgui_impl_dx11.h"
// Linux: Backend-Header (glfw/opengl3) nur dort, wo sie benutzt werden
// (render/Renderer.cpp, preview_main.cpp) — nicht global.
#endif

#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>

#endif
