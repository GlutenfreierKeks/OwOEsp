#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// render/Renderer.h — Overlay-Fenster, D3D11 (Win) / OpenGL+GLFW (Linux),
// Frame-Rendering.
// ─────────────────────────────────────────────────────────────────────────────

// Overlay-Fenster erzeugen (braucht g_mem.Init + g_client bereits!)
bool CreateOverlay(HINSTANCE inst);

// Render-Kontext + Swap/GL erzeugen (muss nach CreateOverlay kommen).
// Name historisch (D3D11); unter Linux: GL-Kontext + ImGui-Backend-Init.
bool CreateD3D11();

// Ein Frame rendern (ESP + Menü + Aim-Logik)
void DirectRenderFrame();

// Beim Beenden: Render-Ressourcen freigeben (Linux: Fenster + Terminate)
void CleanupD3D11();

// Tastatur-Flankenerkennung (für Menü-Hotkeys)
bool WasKeyPressed(int vk);

#ifdef __linux__
// ── Linux-Helfer für app/App.cpp (X11-Fensterverwaltung im Renderer) ─────────
// Game-Fenster-Geometrie (Root-Koordinaten). false = Fenster tot.
bool OwO_GameRect(int* x, int* y, int* w, int* h);
// Overlay bewegen/groessern.
void OwO_MoveOverlay(int x, int y, int w, int h);
// Quit-Signal (Game-Fenster starb waehrend eines Frames).
bool OwO_WantQuit();
// No-op-X-Error-Handler installieren (BadWindow bei totem Fenster schlucken).
void OwO_XErrorGuard();
// Click-through umschalten (Menü zu = durchklickbar, Menü auf = interaktiv).
void OwO_SetOverlayInteractive(bool interactive);
// Echter GLFW-Fenster-Pointer fürs ImGui-Backend (g_overWnd enthält unter
// X11 nur die Window-ID — KEIN GLFWwindow*).
struct GLFWwindow;
GLFWwindow* OwO_GetGLFWWindow();
#endif
