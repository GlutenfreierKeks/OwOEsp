#include "pch.h"
#include "Renderer.h"
#include "../core/Globals.h"
#include "../core/SDK.h"
#include "../core/Input.h"
#include "../features/Offsets.h"
#include "../features/Cache.h"
#include "../features/ESP.h"
#include "../features/Aimbot.h"
#include "../features/Recoil.h"
#include "../features/Triggerbot.h"
#include "../features/CoordinateSaver.h"
#include "../ui/Menu.h"
#ifdef _WIN32
#include <dcomp.h>  // DirectComposition (optionaler Transparenz-Pfad)
#include <psapi.h>  // Profiler: RAM-Verbrauch
#pragma comment(lib, "dcomp.lib")
#pragma comment(lib, "psapi.lib")
#endif

#ifdef _WIN32
// ── DirectComposition-Objekte (nur bei overlayDcomp; sonst nullptr) ─────────
static IDCompositionDevice* g_dcompDev    = nullptr;
static IDCompositionTarget* g_dcompTarget = nullptr;
static IDCompositionVisual* g_dcompVisual = nullptr;
#endif

// ── Profiler (nur aktiv wenn showProfiler — sonst Null Kosten) ──────────────
namespace {
    constexpr int kProfFrames = 120;
    double  s_profMs[kProfFrames]{};
    int     s_profIdx = 0, s_profCount = 0;
    double  s_profCpuMs = 0.0;
    double  s_profImguiMs = 0.0;
    double  s_profPresentMs = 0.0; // Present-Block (VSync/GPU), KEINE CPU-Arbeit
    int     s_profVtx = 0, s_profIdxCount = 0, s_profCmds = 0;
    int     s_profEnts = 0;
    DWORD   s_profAgeMs = 0; // Snapshot-Alter (Reader->Render Latenz)
    DWORD   s_profAgeWorst = 0; // Max-Alter seit Profiler an (Spike-Nachweis)
    SIZE_T  s_profRam = 0, s_profRam0 = 0; // + Start-Basis für Δ (Leak-Kontrolle)
    size_t  s_profTex = 0, s_profTexBytes = 0;
    LARGE_INTEGER s_profPrevEnd{};
    bool    s_profQpfInit = false;
    LARGE_INTEGER s_profQpf{};

    double QpcMs(const LARGE_INTEGER& a, const LARGE_INTEGER& b) {
        if (!s_profQpfInit) {
            QueryPerformanceFrequency(&s_profQpf);
            s_profQpfInit = true;
        }
        return (double)(b.QuadPart - a.QuadPart) * 1000.0 / (double)s_profQpf.QuadPart;
    }

    void DrawProfiler(ImDrawList* dl, int scrW) {
        if (s_profCount < 2) return;
        double sum = 0.0, worst = 0.0, second = 0.0;
        for (int i = 0; i < s_profCount; i++) {
            sum += s_profMs[i];
            if (s_profMs[i] > worst) { second = worst; worst = s_profMs[i]; }
            else if (s_profMs[i] > second) second = s_profMs[i];
        }
        double avg = sum / (double)s_profCount;
        const ReaderStats& rs = g_readerStats;
        auto rms = [](long long us) { return (double)us / 1000.0; };
        char lines[11][96];
        snprintf(lines[0], sizeof(lines[0]), "Renderer  [%s]",
                 g_cfg.overlayDcomp ? "DComp/Flip" : "Layered/Discard");
        snprintf(lines[1], sizeof(lines[1]), "FPS: %.0f", avg > 0.001 ? 1000.0 / avg : 0.0);
        snprintf(lines[2], sizeof(lines[2]), "Frame: %.2f ms (worst %.2f, 1%%low %.2f)",
                 avg, worst, second);
        snprintf(lines[3], sizeof(lines[3]), "CPU: %.2f  ImGui: %.2f  Present: %.2f",
                 s_profCpuMs, s_profImguiMs, s_profPresentMs);
        snprintf(lines[4], sizeof(lines[4]), "Draws: %d  Vtx: %d  Idx: %d",
                 s_profCmds, s_profVtx, s_profIdxCount);
        snprintf(lines[5], sizeof(lines[5]), "Ent: %d  Tex: %zu (%.2f MB)  RAM: %.1f MB (%+.1f)",
                 s_profEnts, s_profTex, (double)s_profTexBytes / 1048576.0,
                 (double)s_profRam / 1048576.0,
                 ((double)s_profRam - (double)s_profRam0) / 1048576.0);
        snprintf(lines[6], sizeof(lines[6]), "--- Reader @%dHz (%d) ---",
                 rs.freqHz.load(std::memory_order_relaxed),
                 rs.playerCount.load(std::memory_order_relaxed));
        snprintf(lines[7], sizeof(lines[7]), "Total: %.2f  Ent: %.2f  Bones: %.2f  Wep: %.2f",
                 rms(rs.usTotal.load(std::memory_order_relaxed)),
                 rms(rs.usEntities.load(std::memory_order_relaxed)),
                 rms(rs.usBones.load(std::memory_order_relaxed)),
                 rms(rs.usWeapons.load(std::memory_order_relaxed)));
        snprintf(lines[8], sizeof(lines[8]), "Items: %.2f  Bomb: %.2f  Util: %.2f  Chick: %.2f",
                 rms(rs.usItems.load(std::memory_order_relaxed)),
                 rms(rs.usBomb.load(std::memory_order_relaxed)),
                 rms(rs.usUtil.load(std::memory_order_relaxed)),
                 rms(rs.usChicken.load(std::memory_order_relaxed)));
        snprintf(lines[9], sizeof(lines[9]), "Snap build: %.2f ms, wait/copy 0.00 (lockfrei)",
                 rms(rs.usPublish.load(std::memory_order_relaxed)));
        snprintf(lines[10], sizeof(lines[10]), "Data age: %u ms (worst %u)",
                 (unsigned)s_profAgeMs, (unsigned)s_profAgeWorst);
        float w = 340.f, h = 11 * 17.f + 16.f;
        float x = (float)scrW - w - 12.f, y = 12.f;
        // Panel im einheitlichen Stil: dunkler Glassmorphism-Hintergrund mit Kontur
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), IM_COL32(14, 16, 22, 225), 6.f);
        dl->AddRect(ImVec2(x - 1.f, y - 1.f), ImVec2(x + w + 1.f, y + h + 1.f), IM_COL32(0, 0, 0, 160), 6.f, 0, 1.5f);
        dl->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), IM_COL32(65, 75, 95, 180), 6.f, 0, 1.0f);
        // Header-Akzentstreifen
        dl->AddRectFilled(ImVec2(x + 12.f, y), ImVec2(x + w - 12.f, y + 2.f), IM_COL32(100, 150, 240, 220), 1.f);
        for (int i = 0; i < 11; i++) {
            ImVec2 tpos(x + 10.f, y + 9.f + i * 17.f);
            ImU32 col = (i == 0 || i == 6) ? IM_COL32(140, 200, 255, 255)
                        : (i == 10 && s_profAgeMs > 50) ? IM_COL32(255, 140, 80, 255)
                        : IM_COL32(215, 222, 235, 235);
            if (g_cfg.espShadow)
                dl->AddText(nullptr, 12.f, ImVec2(tpos.x + 1.f, tpos.y + 1.f), IM_COL32(0, 0, 0, 180), lines[i]);
            dl->AddText(nullptr, 12.f, tpos, col, lines[i]);
        }
    }
} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// render/Renderer.cpp — Overlay-Fenster + D3D11 + Frame-Orchestrierung
// Der einzige Ort, der direkt auf Win32/D3D11/ImGui-Rendering zugreift.
// Alle Funktionsmodule (ESP, Aimbot, ...) werden hier pro Frame aufgerufen.
// ─────────────────────────────────────────────────────────────────────────────

#ifdef _WIN32
extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

// ── Key-Bind-Capture (Maus + Tastatur) ────────────────────────────────────────
static LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam)) return true;

    if (g_keyBinding) {
        switch (msg) {
        case WM_MBUTTONDOWN:
            g_cfg.triggerbotKey = (g_keyBindTarget == 1) ? VK_MBUTTON : g_cfg.triggerbotKey;
            g_cfg.aimKey        = (g_keyBindTarget == 2) ? VK_MBUTTON : g_cfg.aimKey;
            g_cfg.chickenAimKey = (g_keyBindTarget == 3) ? VK_MBUTTON : g_cfg.chickenAimKey;
            g_cfg.ballAimKey    = (g_keyBindTarget == 4) ? VK_MBUTTON : g_cfg.ballAimKey;
            g_cfg.coordSaverKey = (g_keyBindTarget == 5) ? VK_MBUTTON : g_cfg.coordSaverKey;
            g_cfg.markerAimKey  = (g_keyBindTarget == 6) ? VK_MBUTTON : g_cfg.markerAimKey;
            g_cfg.rageKey       = (g_keyBindTarget == 7) ? VK_MBUTTON : g_cfg.rageKey;
            g_keyBinding = false;
            return 0;
        case WM_XBUTTONDOWN: {
            int vk = (GET_XBUTTON_WPARAM(wParam) == 1) ? VK_XBUTTON1 : VK_XBUTTON2;
            if (g_keyBindTarget == 1) g_cfg.triggerbotKey = vk;
            else if (g_keyBindTarget == 2) g_cfg.aimKey = vk;
            else if (g_keyBindTarget == 3) g_cfg.chickenAimKey = vk;
            else if (g_keyBindTarget == 4) g_cfg.ballAimKey = vk;
            else if (g_keyBindTarget == 5) g_cfg.coordSaverKey = vk;
            else if (g_keyBindTarget == 6) g_cfg.markerAimKey = vk;
            else if (g_keyBindTarget == 7) g_cfg.rageKey = vk;
            g_keyBinding = false;
            return 0;
        }
        }
    }

    switch (msg) {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_KEYDOWN:
        if (g_keyBinding) {
            if (wParam == VK_ESCAPE) { g_keyBinding = false; return 0; }
            if (g_keyBindTarget == 1) g_cfg.triggerbotKey = (int)wParam;
            else if (g_keyBindTarget == 2) g_cfg.aimKey = (int)wParam;
            else if (g_keyBindTarget == 3) g_cfg.chickenAimKey = (int)wParam;
            else if (g_keyBindTarget == 4) g_cfg.ballAimKey = (int)wParam;
            else if (g_keyBindTarget == 5) g_cfg.coordSaverKey = (int)wParam;
            else if (g_keyBindTarget == 6) g_cfg.markerAimKey = (int)wParam;
            else if (g_keyBindTarget == 7) g_cfg.rageKey = (int)wParam;
            g_keyBinding = false;
            return 0;
        }
        break;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// ── Game-Fenster finden (SDL/Valve-Klasse) ────────────────────────────────────
struct FindWindowCtx { DWORD pid; HWND hwnd; };

static BOOL CALLBACK FindGameWnd(HWND hwnd, LPARAM lParam) {
    auto* ctx = (FindWindowCtx*)lParam;
    DWORD wpid;
    GetWindowThreadProcessId(hwnd, &wpid);
    if (wpid == ctx->pid) {
        wchar_t cls[128];
        GetClassNameW(hwnd, cls, 128);
        if (wcsstr(cls, L"SDL") || wcsstr(cls, L"Valve")) {
            ctx->hwnd = hwnd;
            return FALSE;
        }
    }
    return TRUE;
}

bool CreateOverlay(HINSTANCE inst) {
    FindWindowCtx fwc{ g_mem.Pid(), nullptr };
    EnumWindows(FindGameWnd, (LPARAM)&fwc);
    g_gameWnd = fwc.hwnd;

    WNDCLASSEX wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = OverlayWndProc;
    wc.hInstance     = inst;
    wc.hIcon         = LoadIcon(inst, MAKEINTRESOURCE(1)); // app.ico (AppIcon.rc)
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)CreateSolidBrush(RGB(0, 0, 0));
    wc.lpszClassName = L"PixelMommySenseOverlay";
    RegisterClassEx(&wc);

    RECT rc{ 0, 0, 1920, 1080 };
    if (g_gameWnd) GetClientRect(g_gameWnd, &rc);
    int sw = rc.right - rc.left, sh = rc.bottom - rc.top;

    // DComp braucht KEIN Layered (echte Alpha-Komposition); Legacy braucht
    // Layered+Colorkey. Rest identisch (Topmost, Transparent, NoActivate).
    DWORD exStyle = WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE;
    if (!g_cfg.overlayDcomp) exStyle |= WS_EX_LAYERED;

    g_overWnd = CreateWindowEx(
        exStyle,
        L"PixelMommySenseOverlay", L"PixelMommySense",
        WS_POPUP, 0, 0, sw, sh,
        nullptr, nullptr, inst, nullptr);

    if (!g_overWnd) return false;
    if (!g_cfg.overlayDcomp)
        SetLayeredWindowAttributes(g_overWnd, RGB(0, 0, 0), 0, LWA_COLORKEY);

    if (g_gameWnd) {
        RECT grc;
        GetWindowRect(g_gameWnd, &grc);
        SetWindowPos(g_overWnd, HWND_TOPMOST, grc.left, grc.top,
                     grc.right - grc.left, grc.bottom - grc.top, SWP_SHOWWINDOW);
    }

    ShowWindow(g_overWnd, SW_SHOW);
    UpdateWindow(g_overWnd);
    return true;
}

// ── DirectComposition anhängen (Flip-Swapchain -> transparentes Visual) ────
// Clear-to-Zero + ImGui-Blending ergibt effektiv premultiplied Output, daher
// DXGI_ALPHA_MODE_PREMULTIPLIED ohne Halo-Artefakte.
static bool AttachComposition(int sw, int sh) {
    IDXGIDevice* dxgiDev = nullptr;
    if (FAILED(g_d3dDev->QueryInterface(IID_PPV_ARGS(&dxgiDev)))) return false;

    IDXGIAdapter* adapter = nullptr;
    HRESULT hr = dxgiDev->GetAdapter(&adapter);
    IDXGIFactory2* factory = nullptr;
    if (SUCCEEDED(hr)) hr = adapter->GetParent(IID_PPV_ARGS(&factory));

    IDXGISwapChain1* sc1 = nullptr;
    if (SUCCEEDED(hr)) {
        DXGI_SWAP_CHAIN_DESC1 sd1{};
        sd1.Width = (UINT)sw; sd1.Height = (UINT)sh;
        sd1.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd1.Stereo = FALSE;
        sd1.SampleDesc.Count = 1;
        sd1.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd1.BufferCount = 2;
        sd1.Scaling = DXGI_SCALING_STRETCH;
        sd1.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        sd1.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
        sd1.Flags = 0;
        hr = factory->CreateSwapChainForComposition(g_d3dDev, &sd1, nullptr, &sc1);
    }
    if (SUCCEEDED(hr)) hr = DCompositionCreateDevice(dxgiDev, IID_PPV_ARGS(&g_dcompDev));
    if (SUCCEEDED(hr)) hr = g_dcompDev->CreateTargetForHwnd(g_overWnd, TRUE, &g_dcompTarget);
    if (SUCCEEDED(hr)) hr = g_dcompDev->CreateVisual(&g_dcompVisual);
    if (SUCCEEDED(hr)) hr = g_dcompVisual->SetContent(sc1);
    if (SUCCEEDED(hr)) hr = g_dcompTarget->SetRoot(g_dcompVisual);
    if (SUCCEEDED(hr)) hr = g_dcompDev->Commit();

    bool ok = SUCCEEDED(hr);
    if (ok) g_swapChain = sc1; // IDXGISwapChain1 -> Basis (Present/Resize identisch)
    else if (sc1) sc1->Release();

    if (factory) factory->Release();
    if (adapter) adapter->Release();
    dxgiDev->Release();
    if (!ok) {
        if (g_dcompVisual)  { g_dcompVisual->Release();  g_dcompVisual  = nullptr; }
        if (g_dcompTarget)  { g_dcompTarget->Release();  g_dcompTarget  = nullptr; }
        if (g_dcompDev)     { g_dcompDev->Release();     g_dcompDev     = nullptr; }
        printf("[dcomp] Composition-Setup fehlgeschlagen (hr=0x%lX) — nutze Layered-Modus nach Neustart\n",
               (unsigned long)hr);
    }
    return ok;
}

bool CreateD3D11() {
    if (g_cfg.overlayDcomp) {
        // DComp-Pfad: Device ohne Swapchain, dann Composition-Swapchain.
        D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
        HRESULT hr = D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &fl, 1,
            D3D11_SDK_VERSION, &g_d3dDev, nullptr, &g_d3dCtx);
        if (FAILED(hr)) return false;

        RECT rc{ 0, 0, 1920, 1080 };
        if (g_gameWnd) GetClientRect(g_gameWnd, &rc);
        if (!AttachComposition(rc.right - rc.left, rc.bottom - rc.top))
            return false; // Fallback: neu starten mit overlayDcomp=false
    } else {
        // Bewährt: DISCARD + LWA_COLORKEY (Flip bricht die Transparenz auf
        // Layered-Windows — schwarzes Overlay). FPS-Schutz kommt aus VSync +
        // Foreground-Skip statt aus dem Swap-Modell.
        DXGI_SWAP_CHAIN_DESC sd{};
        sd.BufferCount       = 1;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferUsage       = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow      = g_overWnd;
        sd.SampleDesc.Count  = 1;
        sd.Windowed          = TRUE;
        sd.SwapEffect        = DXGI_SWAP_EFFECT_DISCARD;
        sd.Flags             = 0;

        D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
        HRESULT hr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &fl, 1,
            D3D11_SDK_VERSION, &sd, &g_swapChain, &g_d3dDev, nullptr, &g_d3dCtx);
        if (FAILED(hr)) return false;
    }

    ID3D11Texture2D* bb = nullptr;
    g_swapChain->GetBuffer(0, IID_PPV_ARGS(&bb));
    if (bb) { g_d3dDev->CreateRenderTargetView(bb, nullptr, &g_rtv); bb->Release(); }
    return g_rtv != nullptr;
}

void CleanupD3D11() {
    if (g_dcompVisual) { g_dcompVisual->Release(); g_dcompVisual = nullptr; }
    if (g_dcompTarget) { g_dcompTarget->Release(); g_dcompTarget = nullptr; }
    if (g_dcompDev)    { g_dcompDev->Release();    g_dcompDev    = nullptr; }
    if (g_rtv)       { g_rtv->Release();       g_rtv       = nullptr; }
    if (g_swapChain) { g_swapChain->Release(); g_swapChain = nullptr; }
    if (g_d3dCtx)    { g_d3dCtx->Release();    g_d3dCtx    = nullptr; }
    if (g_d3dDev)    { g_d3dDev->Release();    g_d3dDev    = nullptr; }
}

// ── RTV nach Resize neu erzeugen ──────────────────────────────────────────────
static void RecreateRTV(int w, int h) {
    if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
    if (g_cfg.overlayDcomp)
        g_swapChain->ResizeBuffers(2, (UINT)w, (UINT)h, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
    else
        g_swapChain->ResizeBuffers(1, (UINT)w, (UINT)h, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
    ID3D11Texture2D* bb = nullptr;
    g_swapChain->GetBuffer(0, IID_PPV_ARGS(&bb));
    if (bb) { g_d3dDev->CreateRenderTargetView(bb, nullptr, &g_rtv); bb->Release(); }
}

#else // ── Linux: GLFW + OpenGL3 + X11 ─────────────────────────────────────────
// (Implementierung steht am Dateiende: X11-Helfer, Callbacks, OwO_-API,
//  CreateOverlay/CreateD3D11/CleanupD3D11/RecreateRTV.)
#endif // _WIN32

// ── Praeziser Frame-Limiter (kein DwmFlush — der addiert 1+ Frame Latenz) ─────
// targetFps <= 0 → unlimited. ABSOLUTE Deadline statt relativem Reset:
// Scheduling-Overhead eines Frames drückt nicht auf alle Folgeframes (kein
// Drift), verspätete Frames holen nicht in Bursts auf.
static void LimitFps(int targetFps) {
    using HrClock = std::chrono::high_resolution_clock;
    static auto next = HrClock::now();
    static HrClock::duration period = HrClock::duration::zero();
    static int lastFps = -1;

    if (targetFps <= 0) {
        next = HrClock::now();
        lastFps = -1;
        return;
    }

    if (targetFps != lastFps) {
        lastFps = targetFps;
        period = std::chrono::duration_cast<HrClock::duration>(
            std::chrono::duration<double>(1.0 / (double)targetFps));
        if (period < HrClock::duration::zero()) period = HrClock::duration::zero();
    }

    const auto now = HrClock::now();
    if (next < now) next = now; // hinterher: Slots überspringen, nicht aufholen

    // Grob bis kurz vor die Deadline schlafen (Sleep ist ~1ms ungenau),
    // Rest spinnen — landet präzise auf der absoluten Deadline.
    constexpr auto spinBudget = std::chrono::microseconds(1500);
    if (next - now > spinBudget)
        std::this_thread::sleep_until(next - spinBudget);
    while (HrClock::now() < next) {
        YieldProcessor();
    }
    next += period;
}

// ── Frame-Inhalt: Snapshot + alle Module ──────────────────────────────────────
static void RenderFrameContent(int scrW, int scrH) {
    // Screen-Groesse fuer den Reader-Thread veroeffentlichen
    g_scrW.store(scrW, std::memory_order_relaxed);
    g_scrH.store(scrH, std::memory_order_relaxed);

    // ── Snapshot (LOCKFREI): Front EINMAL laden, dann lokal kopieren ───────
    // Der Reader schreibt nie in den Front-Puffer — die Kopie ist stabil.
    // Grund für die Kopie: die ViewMatrix wird direkt hier FRISCH gelesen.
    // Der Snapshot ist im Schnitt eine halbe Reader-Tick-Länge alt (144 Hz
    // ≈ 3,5 ms, worst case 7 ms) — bei schnellen Kameraschwenks wandern
    // dadurch ALLE Boxen sichtbar hinter dem Modell her (klassischer
    // External-"lag behind"; UC-Thread „Lag in external ESP"). Ein einzelner
    // 64-Byte-Read pro Frame macht die Projektion quasi zeitgleich.
    const SnapshotSet& snap = SnapFront();
    GameCache cache = snap.game;
    if (cache.valid && g_mem.IsValid() && g_client && offsets::dwViewMatrix) {
        Matrix4x4 vmFresh = g_mem.Read<Matrix4x4>(g_client + offsets::dwViewMatrix);
        if (vmFresh.m[3][3] != 0.f) cache.vm = vmFresh; // sonst Snapshot-Matrix behalten
    }
    // Blickrichtung ebenfalls frisch: Arrow-ESP, Radar-ViewCone und drehendes
    // Ganzkarten-Radar hängen direkt an der aktuellen Yaw — aus dem Snapshot
    // (bis ~7 ms alt) würden die Richtungen beim Kamera-Flick nachhinken.
    if (cache.valid && g_mem.IsValid() && g_client && offsets::dwViewAngles) {
        Vector3 va = g_mem.Read<Vector3>(g_client + offsets::dwViewAngles);
        if (va.y > -720.f && va.y < 720.f && va.x > -90.5f && va.x < 90.5f)
            cache.localViewAngles = va;
    }
    const AimCache&    aimCache     = snap.aim;
    const GroundCache& groundCache  = snap.ground;
    const EntAimCache& chickenCache = snap.chicken;
    const EntAimCache& ballCache    = snap.ball;
    const UtilCache&   utilCache    = snap.util;

    // ── HUD (Fadenkreuz + Aim-FOV-Kreise): laeuft IMMER — haengt bewusst
    //    NICHT am ESP-Master, sonst verschwinden sie mit „ESP aus". ─────────
    DrawHudOverlay(scrW, scrH);

    // ── ESP ──────────────────────────────────────────────────────────────────
    if (g_cfg.enabled && cache.valid) {
        DrawESP(cache, scrW, scrH);
    }

    // ── Arrow ESP: Pfeile ums Fadenkreuz Richtung Gegner ────────────────────
    if (g_cfg.arrowEsp && cache.valid) {
        DrawArrowESP(cache, scrW, scrH);
    }

    if (g_cfg.bombTimer && cache.valid && cache.bomb.active) {
        DrawBombTimer(cache.bomb, scrW, scrH);
    }

    if (g_cfg.itemEsp && groundCache.valid) {
        DrawItemESP(groundCache, &cache, scrW, scrH);
    }

    // ── Util Radius: Smoke-/Feuer-Sperrflächen ─────────────────────────────
    if (g_cfg.utilRadiusEnable && utilCache.valid) {
        DrawUtilRadius(utilCache, cache.vm, scrW, scrH);
    }

    // ── Spectator-Liste: wer schaut zu ─────────────────────────────────────
    if (g_cfg.specListEnable && cache.valid) {
        DrawSpecList(cache, scrW);
    }

    // ── Coordinate Saver: gespeicherte Punkte als Marker ─────────────────────
    if (g_cfg.coordSaverEnable && cache.valid) {
        DrawSavedCoords(cache.vm, scrW, scrH);
        DrawCoordRayPreview(cache, scrW, scrH);
    }

    // ── Radar auf echtem CS2-Radar (Overlay) ────────────────────────────────
    if (g_cfg.radarEnable && cache.valid) {
        DrawRadar(cache, scrW, scrH);
    }

    // ── Throw Preview (Landepunkt-/Explosions-Marker + Countdown) ──────────
    if (g_cfg.throwPreview && cache.valid) {
        DrawThrowPreview(cache, groundCache, scrW, scrH);
    }
    // Nade-Tracking (Molo-Bruch-Heuristik): bei Throw Preview ODER
    // Util-Feuer — braucht Projektil-Daten auch ohne Throw Preview.
    if (cache.valid && groundCache.valid &&
        (g_cfg.throwPreview || (g_cfg.utilRadiusEnable && g_cfg.utilRadiusFire))) {
        DrawGrenadeTracks(groundCache, cache.vm, scrW, scrH, &utilCache);
    }

    // ── Triggerbot (Crosshair-ID / Seed-Hybrid + Smoke-Check) ──────────────
    RunTriggerbot(cache, utilCache, scrW, scrH);

    // ── Aim Bot (Rage zuerst: Head-Snap durch Wände) ───────────────────────
    // ── Aim-Assist (Prioritaet: Spieler > Chicken > Ball > Marker) ──────────
    if (!RunRagebot(aimCache, scrW, scrH)) {
    if (!RunAimbot(cache, aimCache, utilCache, scrW, scrH)) {
        if (!RunChickenAimbot(chickenCache, scrW, scrH)) {
            if (!RunBallAimbot(ballCache, scrW, scrH))
                RunMarkerAimbot(cache.vm, cache.valid, scrW, scrH);
        }
    }
    }

    // ── Coordinate Saver (Tastendruck speichert anvisiertes Objekt) ──────────
    RunCoordinateSaver(cache, chickenCache, ballCache, groundCache, scrW, scrH);

    // ── Recoil-Kompensation + Waffen-Indikator ───────────────────────────────
    RunRecoilControl(cache, scrH);

    // ── Click-through-Toggle (Menü offen = Maus eingefangen) ─────────────────
    static bool prevMenu = false;
    if (g_menuOpen != prevMenu) {
        prevMenu = g_menuOpen;
#ifdef _WIN32
        LONG ex = WS_EX_TOPMOST;
        if (!g_cfg.overlayDcomp) ex |= WS_EX_LAYERED;
        if (!g_menuOpen) ex |= WS_EX_TRANSPARENT | WS_EX_NOACTIVATE;
        SetWindowLong(g_overWnd, GWL_EXSTYLE, ex);
        if (g_menuOpen) {
            SetForegroundWindow(g_overWnd);
            SetCapture(g_overWnd);
        } else {
            ReleaseCapture();
        }
#else
        // Linux: Input-Shape umschalten (X11) statt ExStyle-Bits.
        OwO_SetOverlayInteractive(g_menuOpen);
#endif
    }

    // ── ImGui-Menü (modular, siehe ui/Menu.cpp) ──────────────────────────────
    if (g_menuOpen) RenderMenu();

    // ── Profiler (optional, nach allem anderen = obenauf) ───────────────────
    if (g_cfg.showProfiler) {
        s_profEnts = cache.playerCount;
        s_profAgeMs = GetTickCount() - cache.stampMs;
        if (s_profAgeMs > s_profAgeWorst) s_profAgeWorst = s_profAgeMs;
        // Spike-Diagnose (gedrosselt): Wer war langsam als Daten alt waren?
        {
            static DWORD lastLagLog = 0;
            DWORD nowLag = GetTickCount();
            if (s_profAgeMs > 120 && nowLag - lastLagLog > 5000) {
                lastLagLog = nowLag;
                const ReaderStats& rs = g_readerStats;
                printf("[lag] age=%ums R:[tot %.2f ent %.2f bo %.2f we %.2f it %.2f bo %.2f ut %.2f ch %.2f pub %.2f] C:[cpu %.2f img %.2f pr %.2f]\n",
                       (unsigned)s_profAgeMs,
                       (double)rs.usTotal.load(std::memory_order_relaxed) / 1000.0,
                       (double)rs.usEntities.load(std::memory_order_relaxed) / 1000.0,
                       (double)rs.usBones.load(std::memory_order_relaxed) / 1000.0,
                       (double)rs.usWeapons.load(std::memory_order_relaxed) / 1000.0,
                       (double)rs.usItems.load(std::memory_order_relaxed) / 1000.0,
                       (double)rs.usBomb.load(std::memory_order_relaxed) / 1000.0,
                       (double)rs.usUtil.load(std::memory_order_relaxed) / 1000.0,
                       (double)rs.usChicken.load(std::memory_order_relaxed) / 1000.0,
                       (double)rs.usPublish.load(std::memory_order_relaxed) / 1000.0,
                       s_profCpuMs, s_profImguiMs, s_profPresentMs);
            }
        }
        DrawProfiler(ImGui::GetBackgroundDrawList(), scrW);
    } else if (s_profAgeWorst > 0) {
        s_profAgeWorst = 0; // Reset bei Aus (sauberer Neustart)
    }
}

#ifdef _WIN32
void DirectRenderFrame() {
    if (!g_d3dCtx || !g_rtv || !g_swapChain) return;

    // Tabbed out: keine Frames verbrennen (Game-FPS-Schutz). Messages laufen
    // in App.cpp weiter; Schliessen wird trotzdem erkannt.
    HWND fg = GetForegroundWindow();
    g_appFocused.store(fg == g_gameWnd || fg == g_overWnd, std::memory_order_relaxed);
    if (fg != g_gameWnd && fg != g_overWnd) {
        if (g_gameWnd && !IsWindow(g_gameWnd)) PostQuitMessage(0);
        Sleep(30);
        return;
    }

    RECT rc;
    GetClientRect(g_overWnd, &rc);
    int scrW = rc.right  - rc.left;
    int scrH = rc.bottom - rc.top;

    LARGE_INTEGER t0{};
    if (g_cfg.showProfiler) QueryPerformanceCounter(&t0);

    // Swap-Chain bei Groessenwechsel neu erzeugen
    static int g_lastScrW = 0, g_lastScrH = 0;
    if (scrW != g_lastScrW || scrH != g_lastScrH) {
        if (scrW > 0 && scrH > 0) {
            RecreateRTV(scrW, scrH);
            g_lastScrW = scrW;
            g_lastScrH = scrH;
        }
    }

    g_d3dCtx->OMSetRenderTargets(1, &g_rtv, nullptr);

    // DComp: Alpha 0 (echte Transparenz). Layered: Alpha 1 + Schwarz-Key.
    float clearColor[4] = { 0.f, 0.f, 0.f, g_cfg.overlayDcomp ? 0.f : 1.f };
    g_d3dCtx->ClearRenderTargetView(g_rtv, clearColor);

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    RenderFrameContent(scrW, scrH);

    LARGE_INTEGER tRenderEnd{};
    if (g_cfg.showProfiler) QueryPerformanceCounter(&tRenderEnd);

    ImGui::Render();
    if (g_cfg.showProfiler) {
        if (ImDrawData* dd = ImGui::GetDrawData()) {
            s_profVtx = dd->TotalVtxCount;
            s_profIdxCount = dd->TotalIdxCount;
            s_profCmds = 0;
            for (int n = 0; n < dd->CmdListsCount; n++)
                s_profCmds += dd->CmdLists[n]->CmdBuffer.Size;
        }
    }
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    LARGE_INTEGER tDrawEnd{};
    if (g_cfg.showProfiler) QueryPerformanceCounter(&tDrawEnd);

    // mutually exclusive Modi: VSync ODER Software-Limit ODER Unlimited.
    // (VSync + Spin gleichzeitig = Jitter ohne Nutzen.)
    if (g_cfg.overlayVsync) {
        g_swapChain->Present(1, 0); // VSync taktet allein
    } else if (g_cfg.targetFps > 0) {
        g_swapChain->Present(0, 0);
        LimitFps(g_cfg.targetFps);
    } else {
        g_swapChain->Present(0, 0); // Unlimited — aber Kern abgeben (kein Busy-Spin)
        Sleep(0);
    }

    if (g_cfg.showProfiler) {
        LARGE_INTEGER t1{};
        QueryPerformanceCounter(&t1);
        s_profCpuMs = QpcMs(t0, tRenderEnd);       // Frame-Arbeit (ESP/Aim/Menü)
        s_profImguiMs = QpcMs(tRenderEnd, tDrawEnd); // ImGui-Render + Upload
        s_profPresentMs = QpcMs(tDrawEnd, t1);     // Present-Block (VSync/GPU)
        if (s_profPrevEnd.QuadPart != 0) {
            s_profMs[s_profIdx] = QpcMs(s_profPrevEnd, t1);
            s_profIdx = (s_profIdx + 1) % kProfFrames;
            if (s_profCount < kProfFrames) s_profCount++;
        }
        s_profPrevEnd = t1;
        PROCESS_MEMORY_COUNTERS pmc{};
        if (GetProcessMemoryInfo(GetCurrentProcess(), (PPROCESS_MEMORY_COUNTERS)&pmc, sizeof(pmc))) {
            if (s_profRam0 == 0) s_profRam0 = pmc.WorkingSetSize; // Basis für Δ
            s_profRam = pmc.WorkingSetSize;
        }
        s_profTex = ItemTextureCount();
        s_profTexBytes = ItemTextureBytes();
    } else if (s_profCount > 0) {
        // Aus = State zurücksetzen (sauberer Neustart beim Einschalten)
        s_profCount = 0; s_profIdx = 0;
        s_profPrevEnd.QuadPart = 0;
    }
}

#endif // _WIN32 (Windows-DirectRenderFrame)

#ifdef __linux__
// ═════════════════════════════════════════════════════════════════════════════
// Linux: GLFW + OpenGL3 + X11-Overlay.
// ═════════════════════════════════════════════════════════════════════════════
#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <GL/gl.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/extensions/shape.h>
#include <strings.h>
#include <cstring>
#include <cctype>
#include "imgui/backends/imgui_impl_glfw.h"
#include "imgui/backends/imgui_impl_opengl3.h"

namespace {
GLFWwindow* s_win   = nullptr;
Display*    s_dpy   = nullptr;
Window      s_game  = 0;
bool        s_quit  = false;
bool        s_shapeOk = false;
bool        s_shapeInit = false;

int OwO_XErr(Display*, XErrorEvent*) { return 0; }
} // namespace

void OwO_XErrorGuard() { XSetErrorHandler(OwO_XErr); }
bool OwO_WantQuit() { return s_quit; }
static void OwO_RequestQuit() { s_quit = true; }

namespace {
bool OwO_ClassMatch(const char* cls) {
    if (!cls || !*cls) return false;
    auto has = [&](const char* sub) {
        size_t sl = strlen(cls), pl = strlen(sub);
        if (pl > sl) return false;
        for (size_t i = 0; i + pl <= sl; i++) {
            size_t k = 0;
            while (k < pl && tolower((unsigned char)cls[i + k]) == tolower((unsigned char)sub[k])) k++;
            if (k == pl) return true;
        }
        return false;
    };
    return has("SDL") || has("cs2") || has("Valve") || has("hl2");
}

unsigned long OwO_WinPid(Display* d, Window w) {
    static Atom aPid = 0;
    if (!aPid) aPid = XInternAtom(d, "_NET_WM_PID", False);
    Atom type = 0;
    int fmt = 0;
    unsigned long n = 0, extra = 0;
    unsigned char* p = nullptr;
    if (XGetWindowProperty(d, w, aPid, 0, 1, False, XA_CARDINAL,
                           &type, &fmt, &n, &extra, &p) != Success || !p)
        return 0;
    unsigned long pid = 0;
    if (fmt == 32 && n >= 1) pid = *(unsigned long*)p;
    XFree(p);
    return pid;
}

Window OwO_FindWin(Display* d, Window root, uint32_t pid, int depth, Window* anyPid) {
    Window r = 0, par = 0, *kids = nullptr;
    unsigned nk = 0;
    if (!XQueryTree(d, root, &r, &par, &kids, &nk)) return 0;
    Window best = 0;
    for (unsigned i = 0; i < nk && !best; i++) {
        Window w = kids[i];
        if ((unsigned long)pid == OwO_WinPid(d, w)) {
            if (!*anyPid) *anyPid = w;
            XClassHint ch{};
            if (XGetClassHint(d, w, &ch)) {
                bool m = OwO_ClassMatch(ch.res_class) || OwO_ClassMatch(ch.res_name);
                if (ch.res_class) XFree(ch.res_class);
                if (ch.res_name) XFree(ch.res_name);
                if (m) best = w;
            }
        }
        if (!best && depth > 0) {
            Window sub = OwO_FindWin(d, w, pid, depth - 1, anyPid);
            if (sub) best = sub;
        }
    }
    if (kids) XFree(kids);
    return best;
}

bool OwO_WinRect(Display* d, Window w, int* x, int* y, int* sw, int* sh) {
    if (!w) return false;
    XWindowAttributes a{};
    if (!XGetWindowAttributes(d, w, &a)) return false;
    Window root = DefaultRootWindow(d);
    int rx = 0, ry = 0;
    Window child = 0;
    if (!XTranslateCoordinates(d, w, root, 0, 0, &rx, &ry, &child)) return false;
    if (x) *x = rx;
    if (y) *y = ry;
    if (sw) *sw = a.width;
    if (sh) *sh = a.height;
    return a.width > 0 && a.height > 0;
}

void OwO_SetXStates(Display* d, Window w) {
    Atom st = XInternAtom(d, "_NET_WM_STATE", False);
    Atom above = XInternAtom(d, "_NET_WM_STATE_ABOVE", False);
    Atom taskbar = XInternAtom(d, "_NET_WM_STATE_SKIP_TASKBAR", False);
    Atom pager = XInternAtom(d, "_NET_WM_STATE_SKIP_PAGER", False);
    Atom arr[3] = { above, taskbar, pager };
    XChangeProperty(d, w, st, XA_ATOM, 32, PropModeReplace, (unsigned char*)arr, 3);
}

int OwO_GlfwToVk(int key) {
    if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z) return key;
    if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9) return key;
    if (key >= GLFW_KEY_F1 && key <= GLFW_KEY_F12) return VK_F1 + (key - GLFW_KEY_F1);
    switch (key) {
    case GLFW_KEY_SPACE: return VK_SPACE;
    case GLFW_KEY_ENTER: return VK_RETURN;
    case GLFW_KEY_TAB: return VK_TAB;
    case GLFW_KEY_ESCAPE: return VK_ESCAPE;
    case GLFW_KEY_BACKSPACE: return VK_BACK;
    case GLFW_KEY_DELETE: return VK_DELETE;
    case GLFW_KEY_INSERT: return VK_INSERT;
    case GLFW_KEY_LEFT: return VK_LEFT;
    case GLFW_KEY_UP: return VK_UP;
    case GLFW_KEY_RIGHT: return VK_RIGHT;
    case GLFW_KEY_DOWN: return VK_DOWN;
    case GLFW_KEY_LEFT_SHIFT: return VK_LSHIFT;
    case GLFW_KEY_RIGHT_SHIFT: return VK_RSHIFT;
    case GLFW_KEY_LEFT_CONTROL: return VK_LCONTROL;
    case GLFW_KEY_RIGHT_CONTROL: return VK_RCONTROL;
    case GLFW_KEY_LEFT_ALT: return VK_LMENU;
    case GLFW_KEY_RIGHT_ALT: return VK_RMENU;
    case GLFW_KEY_CAPS_LOCK: return VK_CAPITAL;
    default: break;
    }
    return 0;
}

void OwO_AssignKey(int vk) {
    if (vk == VK_ESCAPE) { g_keyBinding = false; return; }
    if (g_keyBindTarget == 1) g_cfg.triggerbotKey = vk;
    else if (g_keyBindTarget == 2) g_cfg.aimKey = vk;
    else if (g_keyBindTarget == 3) g_cfg.chickenAimKey = vk;
    else if (g_keyBindTarget == 4) g_cfg.ballAimKey = vk;
    else if (g_keyBindTarget == 5) g_cfg.coordSaverKey = vk;
    else if (g_keyBindTarget == 6) g_cfg.markerAimKey = vk;
    else if (g_keyBindTarget == 7) g_cfg.rageKey = vk;
    g_keyBinding = false;
}

void OwO_KeyCb(GLFWwindow*, int key, int, int action, int) {
    if (!g_keyBinding || action != GLFW_PRESS) return;
    int vk = OwO_GlfwToVk(key);
    if (vk) OwO_AssignKey(vk);
}

void OwO_MouseCb(GLFWwindow*, int button, int action, int) {
    if (!g_keyBinding || action != GLFW_PRESS) return;
    int vk = 0;
    if (button == GLFW_MOUSE_BUTTON_MIDDLE) vk = VK_MBUTTON;
    else if (button == 3) vk = VK_XBUTTON1;
    else if (button == 4) vk = VK_XBUTTON2;
    if (vk) OwO_AssignKey(vk);
}
} // namespace

bool OwO_GameRect(int* x, int* y, int* w, int* h) {
    if (!s_dpy || !s_game) return false;
    return OwO_WinRect(s_dpy, s_game, x, y, w, h);
}

GLFWwindow* OwO_GetGLFWWindow() { return s_win; }

void OwO_MoveOverlay(int x, int y, int w, int h) {
    if (!s_dpy || !s_win) return;
    Window wnd = glfwGetX11Window(s_win);
    if (!wnd) return;
    XMoveResizeWindow(s_dpy, wnd, x, y, w > 0 ? w : 1, h > 0 ? h : 1);
    XRaiseWindow(s_dpy, wnd);
    XFlush(s_dpy);
}

void OwO_SetOverlayInteractive(bool interactive) {
    if (!s_dpy || !s_win) return;
    Window wnd = glfwGetX11Window(s_win);
    if (!wnd) return;
    if (!s_shapeInit) {
        s_shapeInit = true;
        int ev = 0, err = 0;
        s_shapeOk = XShapeQueryExtension(s_dpy, &ev, &err) != 0;
        if (!s_shapeOk)
            printf("[overlay] XShape fehlt — Overlay bleibt immer interaktiv!\n");
    }
    if (s_shapeOk) {
        if (interactive) {
            int w = 0, h = 0;
            glfwGetWindowSize(s_win, &w, &h);
            XRectangle r{ (short)0, (short)0, (unsigned short)w, (unsigned short)h };
            XShapeCombineRectangles(s_dpy, wnd, ShapeInput, 0, 0, &r, 1, ShapeSet, Unsorted);
        } else {
            XShapeCombineRectangles(s_dpy, wnd, ShapeInput, 0, 0, nullptr, 0, ShapeSet, Unsorted);
        }
    }
    if (interactive) {
        XRaiseWindow(s_dpy, wnd);
        XSetInputFocus(s_dpy, wnd, RevertToParent, CurrentTime);
    }
    XFlush(s_dpy);
}

bool CreateOverlay(HINSTANCE inst) {
    (void)inst;
    if (!glfwInit()) {
        printf("[overlay] glfwInit fehlgeschlagen\n");
        return false;
    }
    s_dpy = glfwGetX11Display();
    if (!s_dpy) {
        printf("[overlay] kein X11-Display (Wayland-nativ wird nicht unterstützt)\n");
        return false;
    }
    Window anyPid = 0;
    s_game = OwO_FindWin(s_dpy, DefaultRootWindow(s_dpy), g_mem.Pid(), 2, &anyPid);
    if (!s_game) s_game = anyPid;
    g_gameWnd = (HWND)(uintptr_t)s_game;

    int gx = 0, gy = 0, sw = 1920, sh = 1080;
    if (s_game) OwO_WinRect(s_dpy, s_game, &gx, &gy, &sw, &sh);

    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    glfwWindowHint(GLFW_FLOATING, GLFW_TRUE);
    glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_FALSE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    s_win = glfwCreateWindow(sw, sh, "PixelMommySense", nullptr, nullptr);
    if (!s_win) {
        printf("[overlay] Fenster-Erstellung fehlgeschlagen\n");
        return false;
    }
    glfwSetWindowPos(s_win, gx, gy);
    g_overWnd = (HWND)(uintptr_t)glfwGetX11Window(s_win);
    OwO_SetXStates(s_dpy, glfwGetX11Window(s_win));
    glfwSetKeyCallback(s_win, OwO_KeyCb);
    glfwSetMouseButtonCallback(s_win, OwO_MouseCb);
    OwO_SetOverlayInteractive(false);
    glfwShowWindow(s_win);
    return true;
}

bool CreateD3D11() {
    if (!s_win) return false;
    glfwMakeContextCurrent(s_win);
    glfwSwapInterval(g_cfg.overlayVsync ? 1 : 0);
    int w = 0, h = 0;
    glfwGetFramebufferSize(s_win, &w, &h);
    if (w > 0 && h > 0) glViewport(0, 0, w, h);
    return w > 0 && h > 0;
}

void CleanupD3D11() {
    if (s_win) {
        glfwDestroyWindow(s_win);
        s_win = nullptr;
    }
    glfwTerminate();
    g_overWnd = nullptr;
    s_dpy = nullptr;
    s_game = 0;
}

static void RecreateRTV(int w, int h) {
    if (w > 0 && h > 0) glViewport(0, 0, (GLsizei)w, (GLsizei)h);
}

void DirectRenderFrame() {
    if (!s_win) return;

    Window focus = 0;
    int rev = 0;
    XGetInputFocus(s_dpy, &focus, &rev);
    Window overW = glfwGetX11Window(s_win);
    bool focused = (s_game && focus == s_game) || (overW && focus == overW);
    g_appFocused.store(focused, std::memory_order_relaxed);
    if (!focused) {
        if (s_game && !OwO_WinRect(s_dpy, s_game, nullptr, nullptr, nullptr, nullptr))
            OwO_RequestQuit();
        // Unfokussiert: auf Events warten (Fokus-/Quit-Wechsel weckt sofort)
        // statt periodisch zu pollen — spart CPU im Hintergrund.
        glfwWaitEventsTimeout(0.05);
        return;
    }

    int scrW = 0, scrH = 0;
    glfwGetFramebufferSize(s_win, &scrW, &scrH);

    auto t0 = std::chrono::high_resolution_clock::now();

    static int g_lastScrW = 0, g_lastScrH = 0;
    if (scrW != g_lastScrW || scrH != g_lastScrH) {
        if (scrW > 0 && scrH > 0) {
            RecreateRTV(scrW, scrH);
            g_lastScrW = scrW;
            g_lastScrH = scrH;
        }
    }

    glClearColor(0.f, 0.f, 0.f, 0.f);
    glClear(GL_COLOR_BUFFER_BIT);

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    RenderFrameContent(scrW, scrH);

    auto tRenderEnd = std::chrono::high_resolution_clock::now();

    ImGui::Render();
    if (g_cfg.showProfiler) {
        if (ImDrawData* dd = ImGui::GetDrawData()) {
            s_profVtx = dd->TotalVtxCount;
            s_profIdxCount = dd->TotalIdxCount;
            s_profCmds = 0;
            for (int n = 0; n < dd->CmdListsCount; n++)
                s_profCmds += dd->CmdLists[n]->CmdBuffer.Size;
        }
    }
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    auto tDrawEnd = std::chrono::high_resolution_clock::now();

    static int lastVsync = -1;
    int wantVsync = g_cfg.overlayVsync ? 1 : 0;
    if (wantVsync != lastVsync) {
        lastVsync = wantVsync;
        glfwSwapInterval(wantVsync);
    }
    glfwSwapBuffers(s_win);
    glfwPollEvents();
    if (g_cfg.overlayVsync) {
        // VSync taktet allein
    } else if (g_cfg.targetFps > 0) {
        LimitFps(g_cfg.targetFps);
    } else {
        Sleep(0);
    }

    if (g_cfg.showProfiler) {
        auto t1 = std::chrono::high_resolution_clock::now();
        auto ms = [](auto a, auto b) {
            return std::chrono::duration<double, std::milli>(b - a).count();
        };
        s_profCpuMs = ms(t0, tRenderEnd);
        s_profImguiMs = ms(tRenderEnd, tDrawEnd);
        s_profPresentMs = ms(tDrawEnd, t1);
        if (s_profPrevEnd.QuadPart != 0) {
            LARGE_INTEGER cur{};
            QueryPerformanceCounter(&cur);
            s_profMs[s_profIdx] = QpcMs(s_profPrevEnd, cur);
            s_profIdx = (s_profIdx + 1) % kProfFrames;
            if (s_profCount < kProfFrames) s_profCount++;
        }
        QueryPerformanceCounter(&s_profPrevEnd);
        // RAM via Shim (/proc/self/statm)
        PROCESS_MEMORY_COUNTERS pmc{};
        if (GetProcessMemoryInfo(GetCurrentProcess(), (PPROCESS_MEMORY_COUNTERS)&pmc, sizeof(pmc))) {
            if (s_profRam0 == 0) s_profRam0 = pmc.WorkingSetSize;
            s_profRam = pmc.WorkingSetSize;
        }
        s_profTex = ItemTextureCount();
        s_profTexBytes = ItemTextureBytes();
    } else if (s_profCount > 0) {
        s_profCount = 0; s_profIdx = 0;
        s_profPrevEnd.QuadPart = 0;
    }
}
#endif // __linux__