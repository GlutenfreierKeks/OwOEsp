#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// platform/platform.h — zentrale Plattform-Schicht (Windows + Linux).
// Wird ueber pch.h in ALLE TUs gezogen. Ziel: Feature-Code bleibt unveraendert;
// nur plattformspezifische Stellen nutzen OwO_-Helfer oder #ifdef __linux__.
// ─────────────────────────────────────────────────────────────────────────────
#ifdef _WIN32
#include <windows.h>
#else
// ── Linux: minimale Win32-Typen + portable Helfer ────────────────────────────
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <chrono>
#include <thread>
#include <string>
#include <unistd.h>

using DWORD  = uint32_t;
using WORD   = uint16_t;
using BOOL   = int;
using BYTE   = uint8_t;
using UINT   = unsigned int;
using LONG   = long;
using HANDLE = void*;
using HINSTANCE = void*;
using HWND      = void*;
using SIZE_T = size_t;
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#define INVALID_HANDLE_VALUE ((void*)-1)
#ifndef MAX_PATH
#define MAX_PATH 1024
#endif

struct POINT { int x; int y; };
struct RECT { int left; int top; int right; int bottom; };

// LARGE_INTEGER + QPC-Ersatz (Nanosekunden-Basis, gleiche Aufrufsignatur).
struct LARGE_INTEGER { long long QuadPart; };
inline int QueryPerformanceFrequency(LARGE_INTEGER* p) {
    if (!p) return 0;
    p->QuadPart = 1000000000LL;
    return 1;
}
inline int QueryPerformanceCounter(LARGE_INTEGER* p) {
    if (!p) return 0;
    p->QuadPart = (long long)std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    return 1;
}

// GetTickCount / Sleep als Makros (gleiche Aufrufsyntax wie Win32).
inline DWORD OwO_TickMs() {
    return (DWORD)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
inline void OwO_SleepMs(unsigned ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}
#define GetTickCount() OwO_TickMs()
#define Sleep(ms) OwO_SleepMs((unsigned)(ms))
// Spin-Hint: x86 PAUSE, sonst Thread-Yield (portabel für ARM/aarch64).
#if defined(__x86_64__) || defined(__i386__)
#define YieldProcessor() __builtin_ia32_pause()
#else
inline void OwO_YieldProc() { std::this_thread::yield(); }
#define YieldProcessor() OwO_YieldProc()
#endif
#define timeBeginPeriod(x) ((void)0)
#define timeEndPeriod(x) ((void)0)
#define SetProcessDPIAware() ((void)0)
#define AllocConsole() ((void)0)
#define CoInitializeEx(a, b) ((void)0)
#define CoUninitialize() ((void)0)
#define COINIT_MULTITHREADED 0

// fopen_s-Signatur auf fopen abbilden (Rueckgabe 0 = ok wie MSVC).
#define fopen_s(pf, path, mode) (((*(pf)) = fopen((path), (mode))) ? 0 : 1)
#define _countof(a) (sizeof(a) / sizeof((a)[0]))
#define MessageBoxA(h, t, c, f) OwO_MsgBox((c), (t))
// MessageBox-Flags (nur fuer die Makro-Signatur relevant — Linux loggt statt UI).
#ifndef MB_OK
#define MB_OK 0x0
#endif
#ifndef MB_ICONERROR
#define MB_ICONERROR 0x10
#endif
#ifndef MB_ICONWARNING
#define MB_ICONWARNING 0x30
#endif
#ifndef MB_ICONINFORMATION
#define MB_ICONINFORMATION 0x40
#endif
inline void OwO_MsgBox(const char* cap, const char* txt) {
    fprintf(stderr, "[%s] %s\n", cap ? cap : "?", txt ? txt : "?");
}

// GetCurrentProcess + RAM-Profiler via /proc/self/statm.
inline void* OwO_GetCurrentProcess() { return (void*)(uintptr_t)getpid(); }
#define GetCurrentProcess() OwO_GetCurrentProcess()
struct PROCESS_MEMORY_COUNTERS { size_t WorkingSetSize = 0; };
using PPROCESS_MEMORY_COUNTERS = PROCESS_MEMORY_COUNTERS*;
inline int GetProcessMemoryInfo(void*, PROCESS_MEMORY_COUNTERS* pmc, unsigned long) {
    if (!pmc) return 0;
    FILE* f = fopen("/proc/self/statm", "r");
    if (!f) return 0;
    long rss = 0;
    if (fscanf(f, "%*d %ld", &rss) == 1 && rss > 0)
        pmc->WorkingSetSize = (size_t)rss * (size_t)sysconf(_SC_PAGESIZE);
    fclose(f);
    return 1;
}

// Tastatur-/Maus-Status (Implementierung in core/Input.cpp, X11/XTest).
short OwO_GetAsyncKeyState(int vk);
int OwO_GetCursorPos(POINT* pt);
#define GetAsyncKeyState(vk) OwO_GetAsyncKeyState(vk)
#define GetCursorPos(p) OwO_GetCursorPos(p)

// Einmalig VOR allen anderen X11-Aufrufen (und vor GLFW) im main-Thread aufrufen:
// XInitThreads() + Display-Cache. Siehe core/Input.cpp.
void OwO_InitInputEarly();

// SendInput-Kompatibilität für Triggerbot (Implementierung in core/Input.cpp).
struct MOUSEINPUT {
    int dx = 0, dy = 0;
    uint32_t mouseData = 0, dwFlags = 0, time = 0;
    uintptr_t dwExtraInfo = 0;
};
struct KEYBDINPUT {
    uint16_t wVk = 0, wScan = 0;
    uint32_t dwFlags = 0, time = 0;
    uintptr_t dwExtraInfo = 0;
};
struct INPUT {
    uint32_t type = 0;
    union { MOUSEINPUT mi; KEYBDINPUT ki; };
};
#define INPUT_MOUSE 0
#define INPUT_KEYBOARD 1
#define MOUSEEVENTF_MOVE 1
#define MOUSEEVENTF_LEFTDOWN 2
#define MOUSEEVENTF_LEFTUP 4
#define KEYEVENTF_KEYUP 2
int SendInput(unsigned n, INPUT* p, int sz);

// Exe-Verzeichnis mit trailing '/' (Linux: /proc/self/exe).
inline std::string OwO_ExeDir() {
    char buf[1024]{};
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return std::string("./");
    std::string s(buf, (size_t)n);
    size_t slash = s.find_last_of('/');
    if (slash == std::string::npos) return std::string("./");
    return s.substr(0, slash + 1);
}

// ── Tastatur-Codes (numerisch identisch zu Win32 VK_*, Config-kompatibel) ─────
#define VK_LBUTTON 1
#define VK_RBUTTON 2
#define VK_MBUTTON 4
#define VK_XBUTTON1 5
#define VK_XBUTTON2 6
#define VK_BACK 8
#define VK_TAB 9
#define VK_RETURN 13
#define VK_SHIFT 16
#define VK_CONTROL 17
#define VK_MENU 18
#define VK_CAPITAL 20
#define VK_ESCAPE 27
#define VK_SPACE 32
#define VK_LEFT 37
#define VK_UP 38
#define VK_RIGHT 39
#define VK_DOWN 40
#define VK_INSERT 45
#define VK_DELETE 46
#define VK_F1 112
#define VK_F2 113
#define VK_F3 114
#define VK_F4 115
#define VK_F5 116
#define VK_F6 117
#define VK_F7 118
#define VK_F8 119
#define VK_F9 120
#define VK_F10 121
#define VK_F11 122
#define VK_F12 123
#define VK_LSHIFT 160
#define VK_RSHIFT 161
#define VK_LCONTROL 162
#define VK_RCONTROL 163
#define VK_LMENU 164
#define VK_RMENU 165

#endif // __linux__

// Grafik-Textur-Handle: D3D-SRV (Win) bzw. GL-Textur als void* (Linux).
#ifdef _WIN32
struct ID3D11ShaderResourceView;
using OwO_Tex = ID3D11ShaderResourceView*;
#else
using OwO_Tex = void*;
#endif

// Spiel-Prozess/Modul (beide Plattformen — App.cpp nutzt diese Namen).
// Linux: primär native (cs2/client.so), Fallback Proton (cs2.exe/client.dll) —
// die Fallback-Logik steckt in memory.cpp (Init/GetModuleBase/GetModuleSize).
#ifdef _WIN32
#define OWO_PROC_NAME "cs2.exe"
#define OWO_CLIENT_MODULE "client.dll"
#define OWO_PROC_NAME_ALT OWO_PROC_NAME
#define OWO_CLIENT_MODULE_ALT OWO_CLIENT_MODULE
#else
#define OWO_PROC_NAME "cs2"
#define OWO_CLIENT_MODULE "client.so"
#define OWO_PROC_NAME_ALT "cs2.exe"        // Steam/Proton (Wine)
#define OWO_CLIENT_MODULE_ALT "client.dll" // Steam/Proton (Wine)
#endif
