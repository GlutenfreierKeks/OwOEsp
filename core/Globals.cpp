#include "pch.h"
#include "Globals.h"

// ─────────────────────────────────────────────────────────────────────────────
// core/Globals.cpp — Definitionen der cheat-weiten Globals
// ─────────────────────────────────────────────────────────────────────────────
Memory    g_mem;
uintptr_t g_client   = 0;
HWND      g_gameWnd  = nullptr;

Config    g_cfg;
bool      g_menuOpen = false;
bool      g_keyBinding    = false;
int       g_keyBindTarget = 0; // 1=triggerbot, 2=aim, 3=chicken, 4=ball, 5=coordsaver, 6=marker, 7=rage

ID3D11Device*           g_d3dDev    = nullptr;
ID3D11DeviceContext*    g_d3dCtx    = nullptr;
IDXGISwapChain*         g_swapChain = nullptr;
ID3D11RenderTargetView* g_rtv       = nullptr;
HWND                    g_overWnd   = nullptr;

int       g_hudDragTarget = -1; // -1=none, 0=name, 1=health, 2=weapon, 3=distance

std::atomic<bool> g_appFocused{ true };

FILE* g_logFile = nullptr;
