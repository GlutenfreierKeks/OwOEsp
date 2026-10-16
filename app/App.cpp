#include "pch.h"
#ifdef _WIN32
#include <mmsystem.h>
#include <share.h>
#endif
#include "../core/Globals.h"
#include "../core/Config.h"
#include "../core/OffsetsUpdater.h"
#include "../core/SigScan.h"
#include "../features/Offsets.h"
#include "../features/Reader.h"
#include "../features/Bhop.h"
#include "../features/Chams.h"
#include "../features/MouseSmooth.h"
#include "../features/ESP.h"
#include "../render/Renderer.h"
#include "../ui/Menu.h"
#ifdef __linux__
#include <curl/curl.h> // einmaliges curl_global_init (OffsetsUpdater nutzt easy)
#include <GLFW/glfw3.h>
#include "imgui/backends/imgui_impl_glfw.h"
#include "imgui/backends/imgui_impl_opengl3.h"
#endif

#ifdef _WIN32
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "dwmapi.lib")
#endif

// ─────────────────────────────────────────────────────────────────────────────
// app/App.cpp — Einstiegspunkt + Main-Loop
// Initialisiert Memory-Schnittstelle, Overlay, D3D11/GL, ImGui und die
// Hintergrund-Threads. Enthaelt keinerlei Feature-Logik — die lebt in
// features/ (ESP, Bhop, Aimbot, ...) und render/.
// ─────────────────────────────────────────────────────────────────────────────
#ifdef _WIN32
int WINAPI WinMain(HINSTANCE inst, HINSTANCE, LPSTR, int) {
#else
// Linux: normale main (Konsole existiert bereits), kein COM/DPI/Timer-Tuning.
int main() {
    HINSTANCE inst = nullptr;
    OwO_InitInputEarly(); // XInitThreads + XOpenDisplay VOR allen GLFW/X11-Aufrufen
    curl_global_init(CURL_GLOBAL_DEFAULT);
#endif
    SetProcessDPIAware();
    // KEINE High-Priorities mehr: Der Cheat darf CS2s Threads nicht verhungern
    // lassen (Maus-/Render-Lag im Spiel). Standard-Priorität reicht bei
    // 144Hz Reader + VSync-Render völlig.

    // 1ms Timer-Aufloesung fuer praezises Sleep() im Frame-Limiter
    timeBeginPeriod(1);

#ifdef _WIN32
    AllocConsole();
    FILE* fDummy;
    freopen_s(&fDummy, "CONOUT$", "w", stdout);
    freopen_s(&fDummy, "CONOUT$", "w", stderr);
    SetConsoleTitle(L"PixelMommySense Debug");
#endif
    // Build-Tag: zeigt SOFORT welche EXE läuft (gegen stale-binary Verwirrung)
#ifdef _WIN32
    printf("=== PixelMommySense [Build 2026-10-09 F: Hunter-v2+Dedup+ViewProj] ===\n\n");
#else
    printf("=== PixelMommySense [Linux-Build: X11/GLFW/OpenGL] ===\n\n");
#endif

    // Diagnose-Logdatei (OwOEsp.log) — Spiegel wichtiger Zeilen für Support.
    // Windows: _fsopen mit _SH_DENYWR (lesbar während der Client läuft).
    {
        std::string logPath;
#ifdef _WIN32
        char logBuf[MAX_PATH]{};
        GetModuleFileNameA(nullptr, logBuf, MAX_PATH);
        char* slash = strrchr(logBuf, '\\');
        if (slash) strcpy_s(slash + 1, MAX_PATH - (slash + 1 - logBuf), "OwOEsp.log");
        else strcpy_s(logBuf, "OwOEsp.log");
        logPath = logBuf;
        FILE* fLog = _fsopen(logPath.c_str(), "w", _SH_DENYWR);
#else
        logPath = OwO_ExeDir() + "OwOEsp.log";
        FILE* fLog = fopen(logPath.c_str(), "w");
#endif
        if (fLog) {
            g_logFile = fLog;
            printf("[*] Diagnose-Log: %s\n", logPath.c_str());
        }
    }

    // Config FRÜH laden: Overlay-Erstellung (DComp vs. Layered) hängt davon ab.
    LoadConfig();

    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    printf("[*] Suche %s ...\n", OWO_PROC_NAME);
    if (!g_mem.Init(OWO_PROC_NAME)) {
        printf("[!] CS2 nicht gefunden!\n");
        MessageBoxA(nullptr, "CS2 nicht gefunden!", "PixelMommySense", MB_ICONERROR);
        timeEndPeriod(1);
        return 1;
    }
    printf("[+] CS2 gefunden! PID: %u\n", g_mem.Pid());

    g_client = g_mem.GetModuleBase(OWO_CLIENT_MODULE);
    if (!g_client) {
        printf("[!] %s nicht gefunden!\n", OWO_CLIENT_MODULE);
        MessageBoxA(nullptr, "client-Modul nicht gefunden!", "PixelMommySense", MB_ICONERROR);
        g_mem.Close(); timeEndPeriod(1);
        return 1;
    }
    printf("[+] %s Base: 0x%llX\n", OWO_CLIENT_MODULE, (unsigned long long)g_client);

    printf("[*] Erstelle Overlay ...\n");
    if (!CreateOverlay(inst)) {
        printf("[!] Overlay fehlgeschlagen!\n");
        MessageBoxA(nullptr, "Overlay Fehler!", "PixelMommySense", MB_ICONERROR);
        g_mem.Close(); timeEndPeriod(1);
        return 1;
    }

    printf("[*] Initialisiere Render-Backend ...\n");
    if (!CreateD3D11()) {
        printf("[!] Render-Backend fehlgeschlagen!\n");
        MessageBoxA(nullptr, "Render Fehler!", "PixelMommySense", MB_ICONERROR);
        g_mem.Close(); timeEndPeriod(1);
        return 1;
    }
#ifdef _WIN32
    printf("[+] D3D11 initialisiert (FLIP_DISCARD)\n");
#else
    printf("[+] OpenGL initialisiert (GLFW/X11-Overlay)\n");
#endif

    // ── ImGui-Setup ───────────────────────────────────────────────────────────
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Theme + Fonts + Texturen (modular, siehe ui/Menu.cpp)
#ifdef _WIN32
    InitMenu(g_d3dDev);
#else
    InitMenu();
#endif

    // Waffen-/Item-Icons EINMAL in GPU-Cache (embedded aus der EXE) —
    // danach kein SVG-Parse mehr im Frame.
#ifdef _WIN32
    PreloadWeaponTextures(g_d3dDev);

    ImGui_ImplWin32_Init(g_overWnd);
    ImGui_ImplDX11_Init(g_d3dDev, g_d3dCtx);
#else
    PreloadWeaponTextures();

    ImGui_ImplGlfw_InitForOpenGL(OwO_GetGLFWWindow(), true);
    ImGui_ImplOpenGL3_Init("#version 130");
#endif
    printf("[+] ImGui initialisiert\n");
    printf("[i] INSERT = Menü öffnen/schließen\n\n");

    // (LoadConfig läuft seit Build J vor CreateOverlay — DComp-Auswahl.)

    // ── Offset-Auto-Update (Hintergrund-Download von a2x/cs2-dumper) ──────────
    // Registrierungen aus Offsets.h forcieren (sonst leere Registry falls App.cpp
    // als erste TU initialisiert wird). Danach frische Offsets bei JEDEM Start.
    EnsureOffsetsRegistered();
    offsetsupdater::StartAutoRefresh();
    printf("[*] Offset-Update gestartet (a2x/cs2-dumper) — frische Offsets bei jedem Start, live ohne Neustart ...\n");

    // ── Signatur-Scan (Update-Resistenz): dw-Offsets direkt aus client.dll ───
    // Fixiert gefundene Werte (Updater fasst sie nicht mehr an). Schemas (m_*)
    // kommen weiter per Updater (relativ, update-stabil).
    SigScanOffsets();
    SigScanRetryDelayed(); // Fehlstellen (ViewMatrix im Menü) später erneut
    ViewMatrixHunter();    // Notlösung: Matrix per Maus-Bewegungstest finden

    // ── Offset-Selbsttest (immer an, billig, ein Block) ─────────────────────
    // Zeigt SOFORT ob dw- und Schema-Werte plausibel sind + nach 6s ob der
    // Reader echte Daten sieht. Eine Konsolen-Copy reicht zur Ferndiagnose.
    {
        printf("[selftest] dw: ent=0x%llX lctrl=0x%llX vm=0x%llX jump=0x%llX c4=0x%llX sens=0x%llX input=0x%llX vang=0x%llX (Dump %s, %d Werte)\n",
               (unsigned long long)offsets::dwEntityList, (unsigned long long)offsets::dwLocalPlayerController,
               (unsigned long long)offsets::dwViewMatrix, (unsigned long long)offsets::dwJumpButton,
               (unsigned long long)offsets::dwPlantedC4, (unsigned long long)offsets::dwSensitivity,
               (unsigned long long)offsets::dwCSGOInput, (unsigned long long)offsets::dwViewAngles,
               offsetsupdater::DumpDate(), offsetsupdater::AppliedCount());
        printf("[selftest] schema: hPawn=0x%llX hPlayerPawn=0x%llX hObsPawn=0x%llX obsSvc=0x%llX name=0x%llX alive=0x%llX idEnt=0x%llX team=0x%llX hp=0x%llX vm dipl.\n",
               (unsigned long long)schemas::m_hPawn, (unsigned long long)schemas::m_hPlayerPawn,
               (unsigned long long)schemas::m_hObserverPawn, (unsigned long long)schemas::m_pObserverServices,
               (unsigned long long)schemas::m_sSanitizedPlayerName, (unsigned long long)schemas::m_bPawnIsAlive,
               (unsigned long long)schemas::m_iIDEntIndex, (unsigned long long)schemas::m_iTeamNum,
               (unsigned long long)schemas::m_iHealth);
        // Game-Build-Alter: PE-Timestamp von client.dll (Update-Erkennung).
        // Linux (.so = ELF): Build-ID stattdessen über .note.gnu.build-id —
        // hier vereinfacht: Timestamp-Feld entfällt, Size dient als Fingerprint.
#ifdef _WIN32
        uint32_t lfanew = g_mem.Read<uint32_t>(g_client + 0x3C);
        uint32_t ts = (lfanew && lfanew < 0x800) ? g_mem.Read<uint32_t>(g_client + lfanew + 8) : 0;
        printf("[selftest] client.dll PE-Timestamp: %u\n", ts);
#else
        printf("[selftest] client.so Image-Größe: 0x%llX\n",
               (unsigned long long)g_mem.GetModuleSize(OWO_CLIENT_MODULE));
#endif
        std::thread([] {
            Sleep(6000);
            const SnapshotSet& s = SnapFront();
            const GameCache& c = s.game;
            bool haveLocal = c.valid && c.localPawnAddr != 0;
            bool havePlayers = c.valid && c.playerCount > 0;
            float m33 = c.valid ? c.vm.m[3][3] : 0.f;
            printf("[selftest] snap nach 6s: valid=%d players=%d localPawn=%s team=%d vm[3][3]=%.2f alter=%ums\n",
                   c.valid, c.playerCount, haveLocal ? "OK" : "FEHLT",
                   c.localTeam, m33,
                   c.valid ? (GetTickCount() - c.stampMs) : 0);
            if (!c.valid || (!haveLocal && !havePlayers))
                printf("[selftest] DEUTUNG: keine Daten -> dwEntityList/dwLocalPlayerController falsch (Game-Update neuer als Dump %s?) + SigScan-Log oben prüfen.\n",
                       offsetsupdater::DumpDate());
            else if (!havePlayers)
                printf("[selftest] DEUTUNG: Local ok, 0 Spieler -> m_hPawn/m_hPlayerPawn falsch.\n");
            else if (m33 != 1.f)
                printf("[selftest] DEUTUNG: Spieler da, Matrix kaputt -> dwViewMatrix falsch.\n");
            else
                printf("[selftest] DEUTUNG: Daten OK -> Keys/Menü/Config prüfen (Trigger-Taste halten, Menü ZU für Trigger/Bhop).\n");
        }).detach();
    }

    // ── Hintergrund-Threads starten ───────────────────────────────────────────
    g_stopThread.store(false);
    g_readThread = std::thread(GameReaderThread);
    printf("[+] Reader-Thread gestartet\n");
    g_bhopThread = std::thread(BhopThread);
    printf("[+] Bhop-Thread gestartet (Spam-Modus)\n");
    g_chamsThread = std::thread(ChamsThread);
    printf("[+] Chams-Thread gestartet (Glow, UC-Standard)\n");
    g_smoothThread = std::thread(MouseSmoothThread);

    // ── Main-Loop ─────────────────────────────────────────────────────────────
    bool done = false;
#ifdef __linux__
    OwO_XErrorGuard(); // BadWindow bei totem Game-Fenster schlucken
#endif
#ifdef _WIN32
    static RECT  lastRect        = {};
    static DWORD lastWindowCheck = 0;

    while (!done) {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { done = true; break; }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (done) break;

        // Game-Fenster-Position tracken / Schliessen erkennen (50ms: kein
        // hinterherhängendes Overlay mehr beim Moven/Resizen)
        DWORD now = GetTickCount();
        if (now - lastWindowCheck > 50) {
            lastWindowCheck = now;
            if (g_gameWnd && IsWindow(g_gameWnd)) {
                RECT crc;
                GetClientRect(g_gameWnd, &crc);
                POINT tl = { crc.left, crc.top };
                ClientToScreen(g_gameWnd, &tl);
                if (tl.x != lastRect.left || tl.y != lastRect.top ||
                    crc.right != lastRect.right || crc.bottom != lastRect.bottom) {
                    lastRect = { tl.x, tl.y, crc.right, crc.bottom };
                    SetWindowPos(g_overWnd, HWND_TOPMOST, tl.x, tl.y,
                                 crc.right - crc.left, crc.bottom - crc.top, SWP_NOACTIVATE);
                }
            } else {
                printf("[!] CS2-Fenster geschlossen — beende ...\n");
                done = true;
            }
        }
#else
    // Linux: GLFW pumpt Events im Renderer (PollEvents in DirectRenderFrame).
    // Hier: Game-Geometrie tracken (50ms) + Quit-Signal + Hotkeys.
    static int s_lastX = -1, s_lastY = -1, s_lastW = 0, s_lastH = 0;
    static DWORD lastWindowCheck = 0;

    while (!done) {
        if (OwO_WantQuit()) { done = true; break; }

        DWORD now = GetTickCount();
        if (now - lastWindowCheck > 50) {
            lastWindowCheck = now;
            int gx = 0, gy = 0, gw = 0, gh = 0;
            if (OwO_GameRect(&gx, &gy, &gw, &gh)) {
                if (gx != s_lastX || gy != s_lastY || gw != s_lastW || gh != s_lastH) {
                    s_lastX = gx; s_lastY = gy; s_lastW = gw; s_lastH = gh;
                    OwO_MoveOverlay(gx, gy, gw, gh);
                }
            } else {
                printf("[!] CS2-Fenster geschlossen — beende ...\n");
                done = true;
            }
        }
#endif

        if (WasKeyPressed(VK_INSERT)) {
            g_menuOpen = !g_menuOpen;
            if (g_keyBinding) { g_keyBinding = false; }
            printf("[debug] Menü %s\n", g_menuOpen ? "geöffnet" : "geschlossen");
        }

        // Key-Bind-Capture-Polling (Fallback fuer Maus-Seiten-/Mittelklick-Tasten)
        if (g_keyBinding) {
            for (int vk = 0; vk < 256; vk++) {
                if (vk == VK_INSERT || vk == VK_LBUTTON || vk == VK_RBUTTON) continue;
                if (WasKeyPressed(vk)) {
                    if (g_keyBindTarget == 1) g_cfg.triggerbotKey = vk;
                    else if (g_keyBindTarget == 2) g_cfg.aimKey = vk;
                    else if (g_keyBindTarget == 3) g_cfg.chickenAimKey = vk;
                    else if (g_keyBindTarget == 4) g_cfg.ballAimKey = vk;
                    else if (g_keyBindTarget == 5) g_cfg.coordSaverKey = vk;
                    else if (g_keyBindTarget == 6) g_cfg.markerAimKey = vk;
                    else if (g_keyBindTarget == 7) g_cfg.rageKey = vk;
                    g_keyBinding = false;
                    break;
                }
            }
        }

        DirectRenderFrame();
    }

    // ── Cleanup ───────────────────────────────────────────────────────────────
    SaveConfig();

    g_stopThread.store(true);
    if (g_readThread.joinable()) g_readThread.join();
    if (g_bhopThread.joinable()) g_bhopThread.join();
    if (g_chamsThread.joinable()) g_chamsThread.join();
    if (g_smoothThread.joinable()) g_smoothThread.join();
    printf("[+] Reader-Thread beendet\n");

    ReleaseItemTextures();
    ReleaseMenuAssets();

#ifdef _WIN32
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
#else
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
#endif
    ImGui::DestroyContext();
    CleanupD3D11();
#ifdef _WIN32
    DestroyWindow(g_overWnd);
#else
    curl_global_cleanup();
#endif
    g_mem.Close();
    if (g_logFile) { fclose(g_logFile); g_logFile = nullptr; }
    CoUninitialize();
    timeEndPeriod(1);
    printf("[+] Aufgeräumt. Tschüss!\n");
    return 0;
}