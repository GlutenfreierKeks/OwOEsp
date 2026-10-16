// inject/DllMain.cpp — OwOEsp Inject v1 (Test-UI only).
//
// v2: KEIN MinHook mehr (keine externe DLL-Abhängigkeit — das war der stille
// Kill: CS2 fand MinHook.x64.dll nicht, LoadLibrary schlug fehl).
// Stattdessen direkter VTable-Patch von Present (idx 8) + ResizeBuffers (13).
// Logging: %TEMP%\OwOEspInject.log (anhängen) + OutputDebugString.
// Erfolgs-Signal: MessageBox nach Hook + Test-UI im Spiel (INSERT).

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <cstdio>
#include <cstdarg>

#include "../imgui/imgui.h"
#include "../imgui/backends/imgui_impl_win32.h"
#include "../imgui/backends/imgui_impl_dx11.h"
#include "TestUI.h"
#include "Chams.h"
#include "ModelChams.h"
#include "OverlayESP.h"
#include "OffScan.h"
#include "OffsetFetch.h"
#include "AimAssist.h"
#include "Bhop.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

volatile bool g_requestUnload = false;

// Build-Stempel (UI + Log beweisen, welcher Stand läuft — kein Raten mehr).
const char* kBuildTag = "2026-10-11 UC-GenPrim-Check+deferred+stamp";

static HMODULE         g_hMod = nullptr;
static volatile bool   g_running = false;

static ID3D11Device*           g_dev = nullptr;
static ID3D11DeviceContext*    g_ctx = nullptr;
static IDXGISwapChain*         g_swap = nullptr;
static ID3D11RenderTargetView* g_rtv = nullptr;
static HWND                    g_hwnd = nullptr;
static WNDPROC                 g_origWndProc = nullptr;
static bool                    g_imguiInit = false;

using PresentFn = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT);
using ResizeBuffersFn = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
static PresentFn       g_oPresent = nullptr;
static ResizeBuffersFn g_oResizeBuffers = nullptr;
static void**          g_slotPresent = nullptr; // VTable-Slot, den wir patchen
static void**          g_slotResize = nullptr;

// ── Logger: %TEMP%\OwOEspInject.log (+ Fallback: Ordner der DLL) ─────────────
static char g_logPath[MAX_PATH] = {};
static char g_logPath2[MAX_PATH] = {};

static void LogInit() {
    char tmp[MAX_PATH] = {};
    DWORD n = GetTempPathA(MAX_PATH, tmp);
    if (n == 0 || n >= MAX_PATH)
        strcpy_s(tmp, "C:\\Windows\\Temp\\");
    snprintf(g_logPath, sizeof(g_logPath), "%sOwOEspInject.log", tmp);
    // Fallback: Verzeichnis dieser DLL (falls TEMP nicht schreibbar).
    if (g_hMod) {
        char mod[MAX_PATH] = {};
        GetModuleFileNameA(g_hMod, mod, MAX_PATH);
        char* s = strrchr(mod, '\\');
        if (s) {
            size_t dirLen = (size_t)(s - mod + 1);
            if (dirLen + 16 < MAX_PATH) {
                memcpy(g_logPath2, mod, dirLen);
                strcpy_s(g_logPath2 + dirLen, MAX_PATH - dirLen, "OwOEspInject.log");
            }
        }
    }
}

static void LogTo(const char* path, const char* line) {
    FILE* f = nullptr;
    if (fopen_s(&f, path, "a") == 0 && f) {
        fputs(line, f);
        fclose(f);
    }
}

void Log(const char* fmt, ...) {
    char msg[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    char line[1200];
    snprintf(line, sizeof(line), "[%lu] %s\n", GetTickCount(), msg);
    OutputDebugStringA(line);

    bool wrote = false;
    if (g_logPath[0]) {
        FILE* f = nullptr;
        if (fopen_s(&f, g_logPath, "a") == 0 && f) {
            fputs(line, f);
            fclose(f);
            wrote = true;
        } else {
            char dbg[256];
            snprintf(dbg, sizeof(dbg), "[OwOEspInject] fopen TEMP failed err=%lu path=%s\n",
                     GetLastError(), g_logPath);
            OutputDebugStringA(dbg);
        }
    }
    // Fallback immer mitschreiben (billig, beweist Ausführung auch bei TEMP-Problem).
    if (g_logPath2[0])
        LogTo(g_logPath2, line);
    (void)wrote;
}

// ── VTable-Patch-Helfer ──────────────────────────────────────────────────────
static bool PatchVTableSlot(void** slot, void* detour, void** outOrig) {
    if (!slot || !detour || !outOrig)
        return false;
    *outOrig = *slot;
    DWORD old = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &old))
        return false;
    *slot = detour;
    DWORD tmp = 0;
    VirtualProtect(slot, sizeof(void*), old, &tmp);
    return true;
}

static bool UnpatchVTableSlot(void** slot, void* orig) {
    if (!slot || !orig)
        return false;
    DWORD old = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &old))
        return false;
    *slot = orig;
    DWORD tmp = 0;
    VirtualProtect(slot, sizeof(void*), old, &tmp);
    return true;
}

// FPS-Messung für Test-UI + Watchdog-Zähler (sichtbar für MainThread)
static double s_fps = 0.0;
static int    s_frames = 0;
static DWORD  s_fpsTick = 0;
static volatile LONG g_presentCalls = 0; // HkPresent-Aufrufe gesamt
static volatile bool g_initDead = false; // InitImGui endgültig gescheitert
static DWORD g_startTick = 0;

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

static LRESULT CALLBACK HookWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (g_test.showMenu) {
        if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp))
            return true;
        if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP || msg == WM_RBUTTONDOWN ||
            msg == WM_RBUTTONUP || msg == WM_MBUTTONDOWN || msg == WM_MBUTTONUP ||
            msg == WM_MOUSEWHEEL || msg == WM_MOUSEMOVE)
            return true;
    } else {
        ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp);
    }
    return CallWindowProc(g_origWndProc, hwnd, msg, wp, lp);
}

static BOOL CALLBACK FindWndCb(HWND hwnd, LPARAM lp) {
    DWORD wpid = 0;
    GetWindowThreadProcessId(hwnd, &wpid);
    if (wpid != GetCurrentProcessId())
        return TRUE;
    wchar_t cls[128] = {};
    GetClassNameW(hwnd, cls, 128);
    if (wcsstr(cls, L"SDL") || wcsstr(cls, L"Valve") || wcsstr(cls, L"Direct")) {
        *reinterpret_cast<HWND*>(lp) = hwnd;
        return FALSE;
    }
    return TRUE;
}

static void CleanupRenderTarget() {
    if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
}

static bool CreateRenderTarget(IDXGISwapChain* sc) {
    ID3D11Texture2D* bb = nullptr;
    HRESULT hr = sc->GetBuffer(0, IID_PPV_ARGS(&bb));
    if (FAILED(hr)) {
        Log("CreateRenderTarget: GetBuffer hr=0x%08X", (unsigned)hr);
        return false;
    }
    hr = g_dev->CreateRenderTargetView(bb, nullptr, &g_rtv);
    bb->Release();
    if (FAILED(hr)) {
        Log("CreateRenderTarget: CreateRTV hr=0x%08X", (unsigned)hr);
        return false;
    }
    return true;
}

static bool InitImGui(IDXGISwapChain* sc) {
    if (g_imguiInit)
        return true;

    HRESULT hr = sc->GetDevice(IID_PPV_ARGS(&g_dev));
    if (FAILED(hr) || !g_dev) {
        Log("InitImGui: GetDevice hr=0x%08X", (unsigned)hr);
        return false;
    }
    g_dev->GetImmediateContext(&g_ctx);
    if (!g_ctx) {
        Log("InitImGui: GetImmediateContext NULL");
        return false;
    }

    DXGI_SWAP_CHAIN_DESC sd{};
    hr = sc->GetDesc(&sd);
    if (FAILED(hr)) {
        Log("InitImGui: GetDesc hr=0x%08X", (unsigned)hr);
        return false;
    }
    g_hwnd = sd.OutputWindow;
    Log("InitImGui: OutputWindow=0x%p", g_hwnd);
    if (!g_hwnd || !IsWindow(g_hwnd)) {
        g_hwnd = nullptr;
        EnumWindows(FindWndCb, reinterpret_cast<LPARAM>(&g_hwnd));
        Log("InitImGui: Fallback-Fenster=0x%p", g_hwnd);
    }
    if (!g_hwnd || !IsWindow(g_hwnd)) {
        Log("InitImGui: kein Fenster!");
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr; // keine imgui.ini ins Game-Verzeichnis schreiben
    ImGui::StyleColorsDark();

    if (!ImGui_ImplWin32_Init(g_hwnd)) {
        Log("InitImGui: ImplWin32_Init FAILED");
        return false;
    }
    if (!ImGui_ImplDX11_Init(g_dev, g_ctx)) {
        Log("InitImGui: ImplDX11_Init FAILED");
        return false;
    }

    SetLastError(0);
    g_origWndProc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtr(g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(HookWndProc)));
    DWORD werr = GetLastError();
    Log("InitImGui: WndProc alt=0x%p new=0x%p err=%lu", g_origWndProc, HookWndProc, werr);
    if (!g_origWndProc) {
        Log("InitImGui: SetWindowLongPtr FAILED err=%lu", werr);
        return false;
    }

    if (!CreateRenderTarget(sc)) {
        Log("InitImGui: RTV failed");
        return false;
    }
    g_swap = sc;
    g_imguiInit = true;
    s_fpsTick = GetTickCount();
    Log("InitImGui: OK dev=0x%p ctx=0x%p", g_dev, g_ctx);
    return true;
}

static void ShutdownImGui() {
    if (g_origWndProc && g_hwnd && IsWindow(g_hwnd)) {
        SetWindowLongPtr(g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_origWndProc));
        g_origWndProc = nullptr;
    }
    if (g_imguiInit) {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        g_imguiInit = false;
    }
    CleanupRenderTarget();
    if (g_ctx) { g_ctx->Release(); g_ctx = nullptr; }
    if (g_dev) { g_dev->Release(); g_dev = nullptr; }
    g_swap = nullptr;
    g_hwnd = nullptr;
}

static HRESULT __stdcall HkPresent(IDXGISwapChain* sc, UINT sync, UINT flags) {
    LONG n = InterlockedIncrement(&g_presentCalls);
    if (n == 1)
        Log("HkPresent: ERSTER Call sc=0x%p", sc);
    else if (n % 1000 == 0)
        Log("HkPresent: %ld calls, imgui=%d menu=%d", n, (int)g_imguiInit, (int)g_test.showMenu);

    EnsureDiHook(sc); // Stride-Recon (einmalig, read-only)
    VerifyModelHooks(); // Hook-Integrität pro Frame (Revert-Race gewinnen)

    if (!g_imguiInit && !g_initDead) {
        static int s_tries = 0;        if (s_tries < 50) { // 50 Versuche, dann Ruhe (Spam vermeiden)
            s_tries++;
            if (!InitImGui(sc))
                Log("HkPresent: InitImGui Versuch %d FAILED", s_tries);
            if (s_tries >= 50) {
                g_initDead = true; // Watchdog im MainThread lädt uns dann aus
                Log("HkPresent: InitImGui AUFGEGEBEN (50 Versuche) -> Auto-Eject");
            }
        }
    }

    if (g_imguiInit && g_rtv && g_ctx) {
        static bool s_insDown = false;
        bool insDown = (GetAsyncKeyState(VK_INSERT) & 0x8000) != 0;
        if (insDown && !s_insDown) {
            g_test.showMenu = !g_test.showMenu;
            Log("Menue %s", g_test.showMenu ? "AN" : "AUS");
        }
        s_insDown = insDown;
        if (GetAsyncKeyState(VK_END) & 0x8000)
            g_requestUnload = true;

        s_frames++;
        DWORD now = GetTickCount();
        if (now - s_fpsTick >= 500) {
            s_fps = s_frames * 1000.0 / double(now - s_fpsTick ? (now - s_fpsTick) : 1);
            g_test.fps = s_fps;
            s_frames = 0;
            s_fpsTick = now;
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        RunChams(); // Team-Poll + Hook-Lazy-Init (read-only)
        DrawInjectTestUI();
        // Screen-Größe: SwapChain-Desc, Fallback Fenster-Rect (GetDesc
        // kann an gewrappten Chains scheitern — dann wäre ESP stumm).
        static int sW = 0, sH = 0, sTick = 0;
        if (sW == 0 || (++sTick % 60) == 0) {
            DXGI_SWAP_CHAIN_DESC sd{};
            if (SUCCEEDED(sc->GetDesc(&sd)) && sd.BufferDesc.Width > 0) {
                sW = (int)sd.BufferDesc.Width;
                sH = (int)sd.BufferDesc.Height;
            } else if (g_hwnd && IsWindow(g_hwnd)) {
                RECT rc{};
                if (GetClientRect(g_hwnd, &rc) && rc.right > 0) {
                    sW = rc.right - rc.left;
                    sH = rc.bottom - rc.top;
                }
            }
        }
        DrawOverlayESP(sW, sH); // Boxen/HP/Namen (read-only, robust)
        RunAimAssist(sW, sH);   // Screen-Aim (mouse_event, Key halten)
        RunBhop();              // Auto-Hop + Strafe (SendInput)

        ImGui::Render();
        g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }

    return g_oPresent(sc, sync, flags);
}

static HRESULT __stdcall HkResizeBuffers(IDXGISwapChain* sc, UINT bc, UINT w, UINT h,
                                         DXGI_FORMAT fmt, UINT flags) {
    Log("HkResizeBuffers %ux%u", w, h);
    if (g_imguiInit) {
        CleanupRenderTarget();
        HRESULT hr = g_oResizeBuffers(sc, bc, w, h, fmt, flags);
        Log("ResizeBuffers orig hr=0x%08X", (unsigned)hr);
        CreateRenderTarget(sc);
        return hr;
    }
    return g_oResizeBuffers(sc, bc, w, h, fmt, flags);
}

// Dummy-Fenster + Device für VTable-Adressen (mit Logging).
static bool GetSwapTargets(void**& slotP, void**& slotR) {
    WNDCLASSA wc{};
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = g_hMod ? g_hMod : GetModuleHandleA(nullptr);
    wc.lpszClassName = "OwOEspDummyWnd";
    if (!RegisterClassA(&wc)) {
        DWORD e = GetLastError();
        if (e != ERROR_CLASS_ALREADY_EXISTS) {
            Log("Dummy: RegisterClass err=%lu", e);
            return false;
        }
    }
    HWND dummy = CreateWindowExA(0, "OwOEspDummyWnd", "dummy", WS_OVERLAPPEDWINDOW,
                                 0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);
    if (!dummy) {
        Log("Dummy: CreateWindow err=%lu", GetLastError());
        return false;
    }

    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 1;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = dummy;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    IDXGISwapChain* sc = nullptr;
    D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                                               &fl, 1, D3D11_SDK_VERSION, &sd,
                                               &sc, &dev, nullptr, &ctx);
    if (FAILED(hr) || !sc) {
        Log("Dummy: D3D11CreateDeviceAndSwapChain hr=0x%08X err=%lu", (unsigned)hr, GetLastError());
        DestroyWindow(dummy);
        return false;
    }
    void** vt = *reinterpret_cast<void***>(sc);
    slotP = &vt[8];
    slotR = &vt[13];
    Log("Dummy: OK sc=0x%p vt=0x%p presentSlot=0x%p(orig 0x%p) resizeSlot=0x%p(orig 0x%p)",
        sc, vt, slotP, vt[8], slotR, vt[13]);

    if (ctx) ctx->Release();
    if (dev) dev->Release();
    if (sc) sc->Release();
    DestroyWindow(dummy);
    return true;
}

static DWORD WINAPI MainThread(LPVOID) {
    // HINWEIS: LogInit braucht g_hMod für den Fallback-Pfad → erst HIER,
    // g_hMod wird in DllMain vor CreateThread gesetzt.
    LogInit();
    g_startTick = GetTickCount();
    int exitCode = 0;
    Log("======== OwOEspInject [%s] start (PID %lu) ========", kBuildTag, GetCurrentProcessId());
    Log("Logdatei: %s", g_logPath);
    if (g_logPath2[0])
        Log("Fallback-Log: %s", g_logPath2);

    // Eject-Event (Befehl vom Injector: "OwOEspInjector.exe unload").
    // Reset: ein alter Signal-Zustand darf eine frische Instanz nicht killen.
    HANDLE hEv = CreateEventW(nullptr, TRUE, FALSE, L"Local\\OwOEspInjectUnload");
    if (hEv) ResetEvent(hEv);
    else Log("Warnung: Eject-Event nicht erstellbar (err=%lu)", GetLastError());

    char modPath[MAX_PATH] = {};
    GetModuleFileNameA(g_hMod, modPath, MAX_PATH);
    Log("Modul: %s", modPath);

    // Frische Offsets (a2x, Cache/Download) — danach g_off aktuell.
    FetchOffsetsOnce();
    ApplyFreshOffsets();

    if (!GetSwapTargets(g_slotPresent, g_slotResize)) {
        Log("FATAL: SwapTargets failed -> Auto-Eject");
        MessageBoxA(nullptr, "OwOEspInject: Dummy-Device fehlgeschlagen.\nSiehe %TEMP%\\OwOEspInject.log",
                    "OwOEspInject", MB_ICONERROR);
        exitCode = 1;
        goto unload;
    }
    if (!PatchVTableSlot(g_slotPresent, (void*)&HkPresent, (void**)&g_oPresent)) {
        Log("FATAL: Patch Present failed err=%lu -> Auto-Eject", GetLastError());
        MessageBoxA(nullptr, "OwOEspInject: Present-Hook fehlgeschlagen.",
                    "OwOEspInject", MB_ICONERROR);
        exitCode = 1;
        goto unload;
    }
    if (!PatchVTableSlot(g_slotResize, (void*)&HkResizeBuffers, (void**)&g_oResizeBuffers)) {
        Log("FATAL: Patch ResizeBuffers failed -> Auto-Eject");
        UnpatchVTableSlot(g_slotPresent, (void*)g_oPresent);
        MessageBoxA(nullptr, "OwOEspInject: ResizeBuffers-Hook fehlgeschlagen.",
                    "OwOEspInject", MB_ICONERROR);
        exitCode = 1;
        goto unload;
    }
    Log("Hooks AKTIV: oPresent=0x%p oResize=0x%p", g_oPresent, g_oResizeBuffers);
    Log("Warte auf Present ... (Spiel muss rendern, ggf. Fokus aufs Spiel)");
    Log("Eject: END-Taste, Unload-Button oder OwOEspInjector.exe unload");

    g_running = true;
    {
        int beat = 0;
        while (g_running && !g_requestUnload) {
            DWORD w = WAIT_TIMEOUT;
            if (hEv) w = WaitForSingleObject(hEv, 500);
            else Sleep(500);
            if (w == WAIT_OBJECT_0) {
                Log("Eject per Befehl (Injector unload)");
                break;
            }
            VmHunterTick(); // ViewMatrix-Jäger (non-blocking)
            // ── Watchdog: Selbst-Entladung bei Fehlschlägen ──────────────
            // (Hook-Integrität prüft HkPresent pro Frame.)
            DWORD elapsed = GetTickCount() - g_startTick;
            if (g_initDead) {
                Log("Auto-Eject: ImGui-Init endgültig gescheitert");
                break;
            }
            if (elapsed > 25000 && g_presentCalls == 0) {
                Log("Auto-Eject: kein Present nach 25s (falscher Renderer/Pfad?)");
                break;
            }
            if (++beat % 10 == 0) // alle 5s Herzschlag
                Log("Heartbeat: imgui=%d menu=%d presents=%ld",
                    (int)g_imguiInit, (int)g_test.showMenu, g_presentCalls);
        }
    }

    Log("Unload beginnt ...");
unload:
    Sleep(300); // Present soll raus sein
    BhopCleanup(); // evtl. gehaltene Strafe-Tasten loslassen
    if (g_slotPresent) UnpatchVTableSlot(g_slotPresent, (void*)g_oPresent);
    if (g_slotResize) UnpatchVTableSlot(g_slotResize, (void*)g_oResizeBuffers);
    g_slotPresent = g_slotResize = nullptr;
    Sleep(200);
    ShutdownModelChams(); // DrawObject-Bytes zurück, States freigeben
    ShutdownImGui();
    if (hEv) CloseHandle(hEv);
    Log("Unloaded (code=%d), bye", exitCode);
    g_running = false;
    FreeLibraryAndExitThread(g_hMod, (DWORD)exitCode);
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hMod, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        // Roh-Marker OHNE CRT (beweist DllMain-Ausführung, auch wenn danach
        // alles schiefgeht). CreateFile/WriteFile sind hier sicher genug.
        {
            wchar_t p[MAX_PATH] = {};
            DWORD n = GetTempPathW(MAX_PATH, p);
            if (n > 0 && n < MAX_PATH - 32) {
                wcscat_s(p, L"OwOEspInject.mark");
                HANDLE h = CreateFileW(p, FILE_APPEND_DATA,
                                       FILE_SHARE_READ | FILE_SHARE_WRITE,
                                       nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (h != INVALID_HANDLE_VALUE) {
                    char b[160];
                    int len = wsprintfA(b, "DllMain ATTACH pid=%lu mod=0x%p tick=%lu\r\n",
                                        GetCurrentProcessId(), hMod, GetTickCount());
                    DWORD w = 0;
                    WriteFile(h, b, (DWORD)len, &w, nullptr);
                    CloseHandle(h);
                }
            }
        }
        g_hMod = hMod;
        DisableThreadLibraryCalls(hMod);
        g_requestUnload = false;
        HANDLE t = CreateThread(nullptr, 0, MainThread, nullptr, 0, nullptr);
        if (t) {
            CloseHandle(t);
        } else {
            DWORD e = GetLastError();
            char buf[256];
            snprintf(buf, sizeof(buf),
                     "OwOEspInject: CreateThread fehlgeschlagen (%lu).\n"
                     "Die DLL wurde geladen, aber der Start-Thread nicht.",
                     e);
            MessageBoxA(nullptr, buf, "OwOEspInject", MB_ICONERROR);
        }
    }
    return TRUE;
}
