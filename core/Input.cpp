#include "pch.h"
#include "Input.h"
#include "Globals.h"
#include "../features/Offsets.h"

// ─────────────────────────────────────────────────────────────────────────────
// core/Input.cpp — Eingabe-Simulation
// 1) SendInput fuer Maus/Klick (ereignisbasiert, bewiesen funktionierend)
// 2) Direkte Writes ins CS2-Inputsystem (dwCSGOInput) fuer Tastatur-Buttons —
//    tick-perfekt, kein SendInput-Fenster noetig.
// ─────────────────────────────────────────────────────────────────────────────

// ── CS2 CInputSystem: Byte-Arrays je ButtonCode ──────────────────────────────
//   0x40 = Down-Zustand  (m_bButtonStates)
//   0x48 = Pressed       (m_bButtonPressed)
//   0x50 = Released      (m_bButtonReleased)
bool InputSystemPress(int buttonCode) {
    uintptr_t input = g_mem.Read<uintptr_t>(g_client + offsets::dwCSGOInput);
    if (!input) return false;
    g_mem.Write<uint8_t>(input + 0x40 + buttonCode, 1); // Down an
    g_mem.Write<uint8_t>(input + 0x48 + buttonCode, 1); // Pressed
    return true;
}

bool InputSystemRelease(int buttonCode) {
    uintptr_t input = g_mem.Read<uintptr_t>(g_client + offsets::dwCSGOInput);
    if (!input) return false;
    g_mem.Write<uint8_t>(input + 0x40 + buttonCode, 0); // Down aus
    g_mem.Write<uint8_t>(input + 0x50 + buttonCode, 1); // Released
    return true;
}

void SimulateKeyDown(WORD vk) {
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    in.ki.dwFlags = 0;
    SendInput(1, &in, sizeof(INPUT));
}

void SimulateKeyUp(WORD vk) {
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    in.ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(1, &in, sizeof(INPUT));
}

void SimulateKeyTap(WORD vk, DWORD keyHeldMs) {
    SimulateKeyDown(vk);
    Sleep(keyHeldMs);
    SimulateKeyUp(vk);
}

void SimulateMouseClick() {
    INPUT in[2]{};
    in[0].type = in[1].type = INPUT_MOUSE;
    in[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    in[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    SendInput(2, in, sizeof(INPUT));
}

void SimulateMouseClickHeld(DWORD keyHeldMs) {
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    SendInput(1, &in, sizeof(INPUT));
    Sleep(keyHeldMs);
    in.mi.dwFlags = MOUSEEVENTF_LEFTUP;
    SendInput(1, &in, sizeof(INPUT));
}

void SimulateMouseMove(int dx, int dy) {
    if (dx == 0 && dy == 0) return;
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dx = dx;
    in.mi.dy = dy;
    in.mi.dwFlags = MOUSEEVENTF_MOVE;
    SendInput(1, &in, sizeof(INPUT));
}

// Gemeinsamer External-Counter (nur Render-Thread — kein Lock nötig).
static int s_extMoveX = 0, s_extMoveY = 0;

void SimulateMouseMoveExternal(int dx, int dy) {
    if (dx == 0 && dy == 0) return;
    s_extMoveX += dx;
    s_extMoveY += dy;
    SimulateMouseMove(dx, dy);
}

bool ConsumeExternalMouseMove(int* dx, int* dy) {
    if (!dx || !dy) return false;
    *dx = s_extMoveX;
    *dy = s_extMoveY;
    s_extMoveX = 0;
    s_extMoveY = 0;
    return true;
}

bool WasKeyPressed(int vk) {
    static bool prev[256]{};
    bool cur = (GetAsyncKeyState(vk) & 0x8000) != 0;
    bool pressed = cur && !prev[vk];
    prev[vk] = cur;
    return pressed;
}

#ifdef __linux__
// ── Linux: X11-Tastaturstatus + XTest-Simulation ─────────────────────────────
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>

namespace {
Display* g_owoDisp = nullptr;
std::once_flag g_owoDispOnce;

Display* OwO_XDisp() {
    std::call_once(g_owoDispOnce, [] {
        g_owoDisp = XOpenDisplay(nullptr);
        if (!g_owoDisp) fprintf(stderr, "[input] XOpenDisplay fehlgeschlagen (kein X11?)\n");
    });
    return g_owoDisp;
}

// Muss VOR allen anderen X11-/GLFW-Aufrufen (main-Thread) laufen:
// XInitThreads() macht Xlib-Aufrufe aus mehreren Threads (Reader + Render) sicher.
void OwO_InitInputEarly() {
    XInitThreads();
    (void)OwO_XDisp();
}

// VK (Win32-numerisch, Config-kompatibel) -> X11-KeySym. 0 = nicht abbildbar.
unsigned long OwO_VkToKeysym(int vk) {
    if (vk >= 'A' && vk <= 'Z') return (unsigned long)vk; // XK_A..Z == ASCII
    if (vk >= '0' && vk <= '9') return (unsigned long)vk; // XK_0..9 == ASCII
    if (vk >= VK_F1 && vk <= VK_F12) return (unsigned long)(XK_F1 + (vk - VK_F1));
    switch (vk) {
    case VK_SPACE:   return XK_space;
    case VK_RETURN:  return XK_Return;
    case VK_TAB:     return XK_Tab;
    case VK_ESCAPE:  return XK_Escape;
    case VK_BACK:    return XK_BackSpace;
    case VK_DELETE:  return XK_Delete;
    case VK_INSERT:  return XK_Insert;
    case VK_CAPITAL: return XK_Caps_Lock;
    case VK_LEFT:    return XK_Left;
    case VK_UP:      return XK_Up;
    case VK_RIGHT:   return XK_Right;
    case VK_DOWN:    return XK_Down;
    case VK_SHIFT:   return XK_Shift_L;
    case VK_CONTROL: return XK_Control_L;
    case VK_MENU:    return XK_Alt_L;
    case VK_LSHIFT:   return XK_Shift_L;
    case VK_RSHIFT:   return XK_Shift_R;
    case VK_LCONTROL: return XK_Control_L;
    case VK_RCONTROL: return XK_Control_R;
    case VK_LMENU:    return XK_Alt_L;
    case VK_RMENU:    return XK_Alt_R;
    default: break;
    }
    return 0;
}

// VK-Mausbutton -> X11-Buttonnummer (0 = keine Maustaste).
int OwO_VkToXButton(int vk) {
    switch (vk) {
    case VK_LBUTTON:  return 1;
    case VK_RBUTTON:  return 3;
    case VK_MBUTTON:  return 2;
    case VK_XBUTTON1: return 8;
    case VK_XBUTTON2: return 9;
    default: break;
    }
    return 0;
}

bool OwO_XTestOk() {
    static int cached = -1; // 0/1 nach Erstprüfung (spart Roundtrip je SendInput)
    if (cached >= 0) return cached != 0;
    Display* d = OwO_XDisp();
    int ev = 0, err = 0, maj = 0, min = 0;
    cached = (d && XTestQueryExtension(d, &ev, &err, &maj, &min)) ? 1 : 0;
    return cached != 0;
}
} // namespace

// ── Snapshot-Cache (pro Thread) ──────────────────────────────────────────────
// XQueryKeymap/XQueryPointer sind Roundtrips (~50–200 µs). Bei ~1 kHz-Abfragen
// aus Bhop/Aim reicht ein gemeinsamer Snapshot mit kurzer TTL völlig aus.
namespace {
constexpr int64_t kOwOInputTtlUs = 2000; // 2 ms

int64_t OwO_NowUs() {
    return (int64_t)std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

struct OwO_InputCache {
    int64_t  ptrStamp = 0;           // µs
    bool     ptrValid = false;
    unsigned ptrMask  = 0;
    int      ptrX = 0, ptrY = 0;

    int64_t  keyStamp = 0;
    bool     keyValid = false;
    char     keys[32]{};

    int      kc[256]{};              // VK -> X-Keycode (0 = keine Abbildung)
    uint8_t  kcValid[256]{};
};
thread_local OwO_InputCache t_owoIn;

bool OwO_Stale(int64_t stamp) { return OwO_NowUs() - stamp >= kOwOInputTtlUs; }

void OwO_RefreshPointer(Display* d) {
    OwO_InputCache& c = t_owoIn;
    Window rret = 0, cret = 0;
    int rx = 0, ry = 0, wx = 0, wy = 0;
    unsigned mask = 0;
    if (XQueryPointer(d, DefaultRootWindow(d), &rret, &cret, &rx, &ry, &wx, &wy, &mask)) {
        c.ptrMask = mask;
        c.ptrX = rx;
        c.ptrY = ry;
        c.ptrValid = true;
    }
    c.ptrStamp = OwO_NowUs();
}

void OwO_RefreshKeys(Display* d) {
    OwO_InputCache& c = t_owoIn;
    XQueryKeymap(d, c.keys);
    c.keyValid = true;
    c.keyStamp = OwO_NowUs();
}

int OwO_KeycodeForVk(Display* d, int vk) {
    OwO_InputCache& c = t_owoIn;
    if (!c.kcValid[vk]) {
        unsigned long sym = OwO_VkToKeysym(vk);
        c.kc[vk] = sym ? XKeysymToKeycode(d, (KeySym)sym) : 0;
        c.kcValid[vk] = 1;
    }
    return c.kc[vk];
}
} // namespace

short OwO_GetAsyncKeyState(int vk) {
    Display* d = OwO_XDisp();
    if (!d) return 0;

    int xb = OwO_VkToXButton(vk);
    if (xb > 0) {
        if (!t_owoIn.ptrValid || OwO_Stale(t_owoIn.ptrStamp))
            OwO_RefreshPointer(d);
        // Button N -> Maskenbit (N+7): Button1Mask == 1<<8 etc.
        if (t_owoIn.ptrValid && (t_owoIn.ptrMask & (1u << (7 + (unsigned)xb)))) return (short)0x8000;
        return 0;
    }

    int kc = OwO_KeycodeForVk(d, vk);
    if (kc <= 0 || kc > 255) return 0;
    if (!t_owoIn.keyValid || OwO_Stale(t_owoIn.keyStamp))
        OwO_RefreshKeys(d);
    if (t_owoIn.keyValid && (t_owoIn.keys[(kc >> 3) & 31] & (1 << (kc & 7)))) return (short)0x8000;
    return 0;
}

int OwO_GetCursorPos(POINT* pt) {
    if (!pt) return 0;
    Display* d = OwO_XDisp();
    if (!d) return 0;
    if (!t_owoIn.ptrValid || OwO_Stale(t_owoIn.ptrStamp))
        OwO_RefreshPointer(d);
    if (!t_owoIn.ptrValid) return 0;
    pt->x = t_owoIn.ptrX;
    pt->y = t_owoIn.ptrY;
    return 1;
}

int SendInput(unsigned n, INPUT* p, int /*sz*/) {
    if (!p || n == 0) return 0;
    Display* d = OwO_XDisp();
    if (!d || !OwO_XTestOk()) return 0;
    for (unsigned i = 0; i < n; i++) {
        if (p[i].type == INPUT_MOUSE) {
            uint32_t f = p[i].mi.dwFlags;
            if (f & MOUSEEVENTF_MOVE)
                XTestFakeRelativeMotionEvent(d, p[i].mi.dx, p[i].mi.dy, CurrentTime);
            if (f & MOUSEEVENTF_LEFTDOWN)
                XTestFakeButtonEvent(d, 1, True, CurrentTime);
            if (f & MOUSEEVENTF_LEFTUP)
                XTestFakeButtonEvent(d, 1, False, CurrentTime);
        } else if (p[i].type == INPUT_KEYBOARD) {
            int kc = 0;
            if (p[i].ki.wScan) {
                kc = (int)p[i].ki.wScan;
            } else {
                unsigned long sym = OwO_VkToKeysym(p[i].ki.wVk);
                if (sym) kc = XKeysymToKeycode(d, (KeySym)sym);
            }
            if (kc > 0)
                XTestFakeKeyEvent(d, kc,
                    (p[i].ki.dwFlags & KEYEVENTF_KEYUP) ? False : True, CurrentTime);
        }
    }
    XFlush(d);
    return (int)n;
}
#endif // __linux__