#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// core/Globals.h — cheat-weite Globals (deklariert)
// Definitionen liegen in core/Globals.cpp. Jedes Modul greift ueber diese
// Header auf die gemeinsame Welt zu — keine includes-includes mehr.
// ─────────────────────────────────────────────────────────────────────────────
#include "pch.h"
#include "Memory.h"
#include "Config.h"
#include "../features/Cache.h"

// Linux: D3D-Typen nur als unvollständige Deklaration (reine Pointer-Globals,
// keine Methodenaufrufe ausserhalb der plattformspezifischen Module).
#ifdef __linux__
struct ID3D11Device;
struct ID3D11DeviceContext;
struct IDXGISwapChain;
struct ID3D11RenderTargetView;
#endif

// ── Prozess / Module ─────────────────────────────────────────────────────────
extern Memory    g_mem;      // Handle auf cs2.exe
extern uintptr_t g_client;   // Base von client.dll
extern HWND      g_gameWnd;  // SDL/Valve-Fenster von CS2

// ── Konfiguration / Menue ────────────────────────────────────────────────────
extern Config    g_cfg;
extern bool      g_menuOpen;
extern bool      g_keyBinding;
extern int       g_keyBindTarget; // 1=triggerbot, 2=aim, 3=chicken, 4=ball, 5=coordsaver, 6=marker, 7=rage

// ── D3D11 / Overlay ──────────────────────────────────────────────────────────
extern ID3D11Device*           g_d3dDev;
extern ID3D11DeviceContext*    g_d3dCtx;
extern IDXGISwapChain*         g_swapChain;
extern ID3D11RenderTargetView* g_rtv;
extern HWND                    g_overWnd;

// ── HUD-Editor ───────────────────────────────────────────────────────────────
extern int g_hudDragTarget; // -1=none, 0=name, 1=health, 2=weapon, 3=distance

// ── App-Fokus (Render-Thread setzt, Reader-Thread drosselt danach) ───────────
#include <atomic>
#include <cstdio>
extern std::atomic<bool> g_appFocused;

// ── Diagnose-Logdatei (OwOEsp.log neben der EXE, für Remote-Diagnose) ─────────
extern FILE* g_logFile;
