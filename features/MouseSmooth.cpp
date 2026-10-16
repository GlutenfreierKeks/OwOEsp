#include "pch.h"
#include "MouseSmooth.h"
// ─────────────────────────────────────────────────────────────────────────────
// features/MouseSmooth.cpp — „Smooth Aim": eigener Maus-Input als weiche
// Fade-Kurve („smoooooth", wenn man die Maus bewegt).
//
// Prinzip (NUR Raw Input + SendInput — kein Hook, keine Injection ins Spiel):
//   • Eigene Raw-Input-Senke (Message-only-Window, RIDEV_INPUTSINK) liest die
//     ECHTEN physischen Deltas in Echtzeit — exakt dieselben, die CS2 sieht.
//   • Injizierte Events (SendInput: Aimbot, Triggerbot UND unsere eigene
//     Korrektur) tragen hDevice == NULL (empirisch verifiziert) und werden
//     unangetastet durchgereicht — kein Feedback-Loop, Bot-Bewegungen werden
//     NICHT geglaettet.
//   • Pro 1-ms-Tick wird ein Bruchteil f des Puffers als SendInput
//     nachgeschoben (exponentieller Fade-Tail). Die vom Spiel bereits
//     angewendete Rohbewegung wird sofort per Gegenkorrektur abgezogen:
//         Injection = (-D sofort) + (f·pending je ms)  →  Summe der Injects = 0
//     → das Spiel erlebt die weiche Kurve, die GESAMT-Strecke bleibt exakt
//     erhalten (kein Aim-Drift, Summe smoothing invariant).
//
// Fokus-Gate: nur wenn das Spiel (g_mem.Pid()) im Vordergrund ist — Menue und
// Desktop werden nie angefasst. Flush bei Toggle-Aus/Fokusverlust gleicht den
// Rest sofort aus (Gesamtstrecke bleibt auch dann exakt).
//
// Thread: gestartet in App.cpp; laeuft bis g_stopThread (wie Chams/Reader).
// ─────────────────────────────────────────────────────────────────────────────

#ifdef _WIN32

#include <windows.h>
#include <cmath>
#include <cstdio>
#include "../core/Config.h"
#include "../core/Input.h"
#include "../core/Globals.h"
#include "../memory.h"
#include "Cache.h" // g_stopThread

namespace {

// Nur vom Smooth-Thread benutzt (WndProc + DrainTick laufen dort) — kein Lock.
HWND  s_hwnd  = nullptr;
float s_pendX = 0.f, s_pendY = 0.f; // Fade-Puffer (soll noch „ausklingen")
float s_corrX = 0.f, s_corrY = 0.f; // noch nicht injizierte Gegenkorrektur

bool GameForeground() {
    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    const DWORD game = (DWORD)g_mem.Pid();
    return pid != 0 && game != 0 && pid == game;
}

// 1-ms-Tick: Fade emittieren. Aus/Fokus weg → Rest in einem Rutsch
// ausgleichen (Gesamtstrecke bleibt so oder so exakt).
void DrainTick() {
    if (s_pendX == 0.f && s_pendY == 0.f && s_corrX == 0.f && s_corrY == 0.f) return;

    const bool active = g_cfg.smoothAimEnable && GameForeground();
    if (active) {
        float soft = g_cfg.smoothAimSoftness;
        if (soft < 60.f) soft = 60.f; else if (soft > 97.f) soft = 97.f;
        const float f = (100.f - soft) * 0.01f; // 0.03 .. 0.40 (Anteil je ms)
        const float emitX = s_corrX + s_pendX * f;
        const float emitY = s_corrY + s_pendY * f;
        const int ix = (int)lroundf(emitX);
        const int iy = (int)lroundf(emitY);
        // Pending exakt um das Gesendete verringern (Rundungsrest bleibt drin).
        s_pendX -= (float)ix - s_corrX;
        s_pendY -= (float)iy - s_corrY;
        s_corrX = 0.f; s_corrY = 0.f;
        if (ix || iy) SimulateMouseMove(ix, iy);
        // Sub-Pixel-Rest sofort auslaufen lassen (sonst „klebt" der Fade).
        if (fabsf(s_pendX) < 0.75f && fabsf(s_pendY) < 0.75f) {
            const int rx = (int)lroundf(s_pendX);
            const int ry = (int)lroundf(s_pendY);
            s_pendX -= (float)rx; s_pendY -= (float)ry;
            if (rx || ry) SimulateMouseMove(rx, ry);
        }
    } else {
        const float emitX = s_pendX + s_corrX;
        const float emitY = s_pendY + s_corrY;
        s_pendX = 0.f; s_pendY = 0.f; s_corrX = 0.f; s_corrY = 0.f;
        const int ix = (int)lroundf(emitX);
        const int iy = (int)lroundf(emitY);
        if (ix || iy) SimulateMouseMove(ix, iy);
    }
}

LRESULT CALLBACK SmoothWndProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    if (msg == WM_INPUT) {
        RAWINPUT ri;
        UINT sz = sizeof(ri);
        if (GetRawInputData((HRAWINPUT)l, RID_INPUT, &ri, &sz, sizeof(RAWINPUTHEADER)) == sizeof(ri) &&
            ri.header.dwType == RIM_TYPEMOUSE) {
            const RAWMOUSE& rm = ri.data.mouse;
            // hDevice != NULL = physische Maus. Injektionen (hDevice == NULL,
            // per SendInput-Probe verifiziert) bleiben unangetastet.
            if (ri.header.hDevice && !(rm.usFlags & MOUSE_MOVE_ABSOLUTE) &&
                (rm.lLastX || rm.lLastY)) {
                if (g_cfg.smoothAimEnable && GameForeground()) {
                    s_pendX += (float)rm.lLastX;
                    s_pendY += (float)rm.lLastY;
                    s_corrX -= (float)rm.lLastX;
                    s_corrY -= (float)rm.lLastY;
                    // Sicherheitskappe gegen pathologische Zustaende.
                    if (s_pendX >  4096.f) s_pendX =  4096.f;
                    if (s_pendX < -4096.f) s_pendX = -4096.f;
                    if (s_pendY >  4096.f) s_pendY =  4096.f;
                    if (s_pendY < -4096.f) s_pendY = -4096.f;
                }
            }
        }
        return DefWindowProcA(h, msg, w, l); // Pflicht fuer Raw-Input-Cleanup
    }
    return DefWindowProcA(h, msg, w, l);
}

} // namespace

void MouseSmoothThread() {
    WNDCLASSEXA wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = SmoothWndProc;
    wc.hInstance     = GetModuleHandleA(NULL);
    wc.lpszClassName = "OwOSmoothRaw";
    RegisterClassExA(&wc);

    s_hwnd = CreateWindowExA(0, "OwOSmoothRaw", "", 0, 0, 0, 0, 0,
                             HWND_MESSAGE, NULL, wc.hInstance, NULL);
    if (!s_hwnd) {
        printf("[smooth] Raw-Input-Fenster fehlgeschlagen (%lu)\n", GetLastError());
        return;
    }

    RAWINPUTDEVICE rid{ 0x01, 0x02, RIDEV_INPUTSINK, s_hwnd };
    if (RegisterRawInputDevices(&rid, 1, sizeof(rid)))
        printf("[+] Smooth-Aim-Thread laeuft (Raw-Senke aktiv)\n");
    else
        printf("[smooth] Raw-Input-Registrierung fehlgeschlagen (%lu)\n", GetLastError());

    while (!g_stopThread.load(std::memory_order_relaxed)) {
        MSG msg;
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg); // WM_INPUT → SmoothWndProc
        }
        DrainTick();
        Sleep(1); // timeBeginPeriod(1) ist global gesetzt (App-Start)
    }

    // Rest sauber ausgleichen, Senke abmelden.
    s_pendX += s_corrX; s_pendY += s_corrY;
    s_corrX = 0.f; s_corrY = 0.f;
    const int fx = (int)lroundf(s_pendX);
    const int fy = (int)lroundf(s_pendY);
    if (fx || fy) SimulateMouseMove(fx, fy);
    RAWINPUTDEVICE ridOff{ 0x01, 0x02, RIDEV_REMOVE, NULL };
    RegisterRawInputDevices(&ridOff, 1, sizeof(ridOff));
    DestroyWindow(s_hwnd);
    s_hwnd = nullptr;
}

#else
void MouseSmoothThread() {} // Windows-only (Linux-Build nutzt den Stub)
#endif
