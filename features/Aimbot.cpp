#include "pch.h"
#include "Aimbot.h"
#include "Cache.h"
#include "CoordinateSaver.h"
#include "../core/Globals.h"
#include "../core/Input.h"

#include <chrono>
#include <cmath>
#include <cstdlib>

// ─────────────────────────────────────────────────────────────────────────────
// features/Aimbot.cpp — Aim-Assist (human) + Aim Bot (rage)
//
// Assist: exponentieller Tiefpass + Humanize-Layer (UC-Legit-Standard à la
// "Smooth Deviation"): Zielpunkt-Jitter (Random-Walk alle ~200ms statt
// Roboter-Starre), Overshoot beim Zielwechsel (flickt vorbei, settelt zurück)
// und Vis-Check (kein Lock durch Wände — spotted-Maske aus dem Reader).
// Aim Bot (Rage): separater Pfad — Head-Höhe, 90%-Snap, kein Vis-Check
// (geht durch Wände), optional Autofire mit Intervall.
// ─────────────────────────────────────────────────────────────────────────────
namespace {
    // Humanize-State (nur Render-Thread)
    static float s_jx = 0.f, s_jy = 0.f;       // Jitter-Offset auf Zielpunkt
    static float s_smoothMul = 1.f;            // Smooth-Deviation
    static DWORD s_jitterNext = 0;             // Re-Roll-Timer
    static int   s_aimLastIdx = -2;            // Zielwechsel-Erkennung
    static float s_over = 0.f;                 // Overshoot-Rest (px)
    static float s_overDx = 0.f, s_overDy = 0.f;
    static bool  s_randInit = false;

    inline float Rand01() { return (float)rand() / (float)RAND_MAX; }
}

bool RunAimbot(const GameCache& cache, const AimCache& aimCache, const UtilCache& utilCache, int scrW, int scrH) {
    if (!g_cfg.aimEnable || !aimCache.valid) return false;
    if (!(GetAsyncKeyState(g_cfg.aimKey) & 0x8000)) {
        s_aimLastIdx = -2; s_over = 0.f; // Key los: Overshoot zurücksetzen
        int drX = 0, drY = 0;
        ConsumeExternalMouseMove(&drX, &drY); // Backlog leeren (kein Alt-Stand)
        return false;
    }
    if (!s_randInit) { srand(GetTickCount()); s_randInit = true; }

    float cx = scrW * 0.5f, cy = scrH * 0.5f;
    int   bestIdx  = -1;
    float bestDist = g_cfg.aimFov;
    Vector2 aimScr{};
    DWORD ageMs = GetTickCount() - aimCache.stampMs;

    for (int ai = 0; ai < aimCache.count; ai++) {
        const auto& tg = aimCache.targets[ai];
        if (!tg.alive || tg.team == aimCache.localTeam) continue;
        // Kein Lock durch Wände (UC-Legit: spotted/visible-Check)
        if (g_cfg.aimCheckVis && !tg.visible) continue;

        Vector3 pred = PredictPos(tg.origin, tg.velocity, ageMs);
        Vector3 aimPos{ pred.x, pred.y,
                        pred.z + (tg.crouching ? g_cfg.aimHeightCrouch : g_cfg.aimHeightStand) };
        // Kein Lock durch Smoke (Strecke Auge->Ziel vs. Smoke-Kugeln)
        if (g_cfg.aimSmokeCheck &&
            SmokeBlocksShot(cache.localEyePos, aimPos, utilCache.zones, utilCache.count))
            continue;
        Vector2 scr;
        if (!WorldToScreen(aimPos, scr, aimCache.vm, scrW, scrH)) continue;

        float dx = scr.x - cx, dy = scr.y - cy;
        float d  = sqrtf(dx*dx + dy*dy);
        if (d < bestDist) { bestDist = d; bestIdx = ai; aimScr = scr; }
    }

    if (bestIdx < 0) { s_aimLastIdx = -2; s_over = 0.f; return false; }

    // ── Smoothing: deltaTime-basiert, FPS-unabhaengig ────────────────────────
    static auto lastTime = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - lastTime).count();
    lastTime = now;
    if (dt <= 0.f || dt > 0.05f) dt = 0.05f;   // Frame-Haenger nicht als Snap

    float dx = aimScr.x - cx, dy = aimScr.y - cy;
    float dist = sqrtf(dx*dx + dy*dy);

    // ── Humanize: Jitter + Overshoot auf den Zielpunkt ───────────────────
    {
        DWORD nowJ = GetTickCount();
        if (nowJ >= s_jitterNext) {
            // Random-Walk alle 120..280ms (Hand statt Roboter)
            s_jitterNext = nowJ + 120 + (DWORD)(Rand01() * 160.f);
            float h = g_cfg.aimHumanize;
            if (h < 0.f) h = 0.f; if (h > 15.f) h = 15.f;
            s_jx = (Rand01() * 2.f - 1.f) * h;
            s_jy = (Rand01() * 2.f - 1.f) * h;
            s_smoothMul = 1.f + (Rand01() * 2.f - 1.f) * 0.15f; // ±15% Smooth
        }
        // Zielwechsel: Flick schiesst übers Ziel hinaus, settelt zurück
        if (bestIdx != s_aimLastIdx) {
            s_aimLastIdx = bestIdx;
            float o = g_cfg.aimOvershoot;
            if (o < 0.f) o = 0.f; if (o > 60.f) o = 60.f;
            s_over = o;
            if (dist > 0.01f) { s_overDx = dx / dist; s_overDy = dy / dist; }
            else { s_overDx = 0.f; s_overDy = 0.f; }
        }
        aimScr.x += s_jx + s_overDx * s_over;
        aimScr.y += s_jy + s_overDy * s_over;
        s_over *= expf(-5.f * dt); // ~200ms Settling
        if (s_over < 0.25f) s_over = 0.f;
        dx = aimScr.x - cx; dy = aimScr.y - cy;
        dist = sqrtf(dx*dx + dy*dy);
    }

    static float carryX = 0.f, carryY = 0.f;   // Subpixel-Rest fuer fliessende Bewegung
    float mx = 0.f, my = 0.f;

    const float deadzone = 3.f;                 // unter 3px: nichts mehr bewegen (kein Zittern)
    if (dist > deadzone) {
        // k: 1..20 Smoothing -> Zeitkonstante ~33ms (schnell) .. ~670ms (butterweich)
        // aimSpeed multipliziert die Ansatzgeschwindigkeit (0.5 = halb, 3 = dreifach)
        float k    = (30.f / g_cfg.aimSmooth) * g_cfg.aimSpeed * s_smoothMul;
        float move = dist * (1.f - expf(-k * dt));
        mx = (dx / dist) * move;
        my = (dy / dist) * move;
    }

    float totalX = mx + carryX, totalY = my + carryY;
    int ix = (int)totalX; carryX = totalX - ix;
    int iy = (int)totalY; carryY = totalY - iy;

    // ── Eigene Mausbewegung hat Vorrang ──────────────────────────────────────
    // Cursor-Delta minus die eigene SendInput-Bewegung = echte User-Bewegung.
    // Bewegt der User selbst, kaempft der AimAssist nicht dagegen an.
    static POINT lastCursor{};
    static int   lastSimX = 0, lastSimY = 0;
    POINT cur;
    GetCursorPos(&cur);
    // External-Moves (RCS/Trigger) abziehen — sonst pausiert der Assist,
    // sobald RCS/Trigger die Maus bewegen. Echte User-Bewegung bleibt übrig.
    int extX = 0, extY = 0;
    ConsumeExternalMouseMove(&extX, &extY);
    float userDx = (float)(cur.x - lastCursor.x) - lastSimX - (float)extX;
    float userDy = (float)(cur.y - lastCursor.y) - lastSimY - (float)extY;
    float userMove = sqrtf(userDx * userDx + userDy * userDy);
    bool  cursorMoved = (cur.x != lastCursor.x || cur.y != lastCursor.y);
    if (cursorMoved && userMove > 5.f && (ix != 0 || iy != 0)) {
        ix = 0;
        iy = 0;   // User faehrt selbst — AimAssist pausiert diesen Frame
    }

    if (ix != 0 || iy != 0)
        SimulateMouseMove(ix, iy);

    POINT after;
    GetCursorPos(&after);
    lastCursor = after;
    lastSimX = ix;
    lastSimY = iy;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Chicken- / Ball-Aimbot — dieselbe Mechanik wie der Player-Aimbot:
// naechstes Ziel im FOV (Projektion vs. Bildschirmzentrum), exponentieller
// Tiefpass, Deadzone, Subpixel-Akkumulator, User-Override.
// Eigener State pro Bot, damit sich die Bots nicht gegenseitig stoeren.
// ─────────────────────────────────────────────────────────────────────────────
struct EntAimState {
    float carryX = 0.f, carryY = 0.f;
    int   lastSimX = 0, lastSimY = 0;
    POINT lastCursor{};
    std::chrono::steady_clock::time_point lastTime{};
    bool  init = false;
};

static bool RunEntityAim(int key, float smooth, float speed, float fov, float height,
                         const EntAimTarget* targets, int count, const Matrix4x4& vm,
                         bool valid, int scrW, int scrH, EntAimState& st) {
    if (!valid || count <= 0) return false;
    if (!(GetAsyncKeyState(key) & 0x8000)) {
        int drX = 0, drY = 0;
        ConsumeExternalMouseMove(&drX, &drY);
        return false;
    }

    float cx = scrW * 0.5f, cy = scrH * 0.5f;
    bool  found = false;
    float bestDist = fov;
    Vector2 aimScr{};

    for (int i = 0; i < count; i++) {
        const auto& tg = targets[i];
        if (!tg.alive) continue;

        Vector3 aimPos{ tg.origin.x, tg.origin.y, tg.origin.z + height };
        Vector2 scr;
        if (!WorldToScreen(aimPos, scr, vm, scrW, scrH)) continue;

        float dx = scr.x - cx, dy = scr.y - cy;
        float d  = sqrtf(dx*dx + dy*dy);
        if (d < bestDist) { bestDist = d; found = true; aimScr = scr; }
    }

    if (!found) return false;

    // ── Smoothing: deltaTime-basiert, FPS-unabhaengig (wie Player-Aimbot) ────
    auto now = std::chrono::steady_clock::now();
    if (!st.init) { st.lastTime = now; st.init = true; }
    float dt = std::chrono::duration<float>(now - st.lastTime).count();
    st.lastTime = now;
    if (dt <= 0.f || dt > 0.05f) dt = 0.05f;   // Frame-Haenger nicht als Snap

    float dx = aimScr.x - cx, dy = aimScr.y - cy;
    float dist = sqrtf(dx*dx + dy*dy);

    float mx = 0.f, my = 0.f;

    const float deadzone = 3.f;                 // unter 3px: nichts mehr bewegen (kein Zittern)
    if (dist > deadzone) {
        float k    = (30.f / smooth) * speed;
        float move = dist * (1.f - expf(-k * dt));
        mx = (dx / dist) * move;
        my = (dy / dist) * move;
    }

    float totalX = mx + st.carryX, totalY = my + st.carryY;
    int ix = (int)totalX; st.carryX = totalX - ix;
    int iy = (int)totalY; st.carryY = totalY - iy;

    // ── Eigene Mausbewegung hat Vorrang (wie Player-Aimbot) ──────────────────
    POINT cur;
    GetCursorPos(&cur);
    int extX = 0, extY = 0;
    ConsumeExternalMouseMove(&extX, &extY);
    float userDx = (float)(cur.x - st.lastCursor.x) - st.lastSimX - (float)extX;
    float userDy = (float)(cur.y - st.lastCursor.y) - st.lastSimY - (float)extY;
    float userMove = sqrtf(userDx * userDx + userDy * userDy);
    bool  cursorMoved = (cur.x != st.lastCursor.x || cur.y != st.lastCursor.y);
    if (cursorMoved && userMove > 5.f && (ix != 0 || iy != 0)) {
        ix = 0;
        iy = 0;   // User faehrt selbst — AimAssist pausiert diesen Frame
    }

    if (ix != 0 || iy != 0)
        SimulateMouseMove(ix, iy);

    POINT after;
    GetCursorPos(&after);
    st.lastCursor = after;
    st.lastSimX = ix;
    st.lastSimY = iy;
    return true;
}

bool RunChickenAimbot(const EntAimCache& chickenCache, int scrW, int scrH) {
    if (!g_cfg.chickenAimEnable) return false;
    static EntAimState st;
    return RunEntityAim(g_cfg.chickenAimKey, g_cfg.chickenAimSmooth, g_cfg.chickenAimSpeed,
                        g_cfg.chickenAimFov, g_cfg.chickenAimHeight,
                        chickenCache.targets, chickenCache.count, chickenCache.vm,
                        chickenCache.valid, scrW, scrH, st);
}

bool RunBallAimbot(const EntAimCache& ballCache, int scrW, int scrH) {
    if (!g_cfg.ballAimEnable) return false;
    static EntAimState st;
    return RunEntityAim(g_cfg.ballAimKey, g_cfg.ballAimSmooth, g_cfg.ballAimSpeed,
                        g_cfg.ballAimFov, g_cfg.ballAimHeight,
                        ballCache.targets, ballCache.count, ballCache.vm,
                        ballCache.valid, scrW, scrH, st);
}

bool RunMarkerAimbot(const Matrix4x4& vm, bool valid, int scrW, int scrH) {
    if (!g_cfg.markerAimEnable || !valid) return false;
    static EntAimState st;
    // Gespeicherte Punkte als Ziele (exakte Positionen, keine Hoehe noetig)
    SavedCoord saved[256];
    int n = GetSavedCoords(saved, 256);
    if (n <= 0) return false;
    static EntAimTarget buf[256];
    for (int i = 0; i < n; i++) { buf[i].origin = saved[i].pos; buf[i].alive = true; }
    return RunEntityAim(g_cfg.markerAimKey, g_cfg.markerAimSmooth, g_cfg.markerAimSpeed,
                        g_cfg.markerAimFov, 0.f, buf, n, vm, true, scrW, scrH, st);
}

// ─────────────────────────────────────────────────────────────────────────────
// Aim Bot (Rage) — UC-Rage-Standard: naechstes Ziel im FOV, Head-Höhe,
// 90%-Snap pro Frame (nahezu instant), KEIN Vis-Check (geht durch Wände),
// optional Autofire mit Intervall wenn das Fadenkreuz sitzt.
// Nutzt die legacy rage-*-Felder aus der Config (kein Struct-Wachstum).
// ─────────────────────────────────────────────────────────────────────────────
bool RunRagebot(const AimCache& aimCache, int scrW, int scrH) {
    if (!g_cfg.rageEnable || !aimCache.valid) return false;
    if (!(GetAsyncKeyState(g_cfg.rageKey) & 0x8000)) return false;

    float cx = scrW * 0.5f, cy = scrH * 0.5f;
    int   bestIdx  = -1;
    float bestDist = g_cfg.rageFov > 0.f ? g_cfg.rageFov : 600.f;
    Vector2 rageScr{};
    DWORD rageAge = GetTickCount() - aimCache.stampMs;

    for (int ai = 0; ai < aimCache.count; ai++) {
        const auto& tg = aimCache.targets[ai];
        if (!tg.alive || tg.team == aimCache.localTeam) continue;
        // Absichtlich KEIN Sichtbarkeits-Check: Rage geht durch Wände.

        Vector3 pred = PredictPos(tg.origin, tg.velocity, rageAge);
        Vector3 aimPos{ pred.x, pred.y,
                        pred.z + (tg.crouching ? g_cfg.rageHeadCrouch : g_cfg.rageHeadStand) };
        Vector2 scr;
        if (!WorldToScreen(aimPos, scr, aimCache.vm, scrW, scrH)) continue;

        float dx = scr.x - cx, dy = scr.y - cy;
        float d  = sqrtf(dx*dx + dy*dy);
        if (d < bestDist) { bestDist = d; bestIdx = ai; rageScr = scr; }
    }

    if (bestIdx < 0) return false;

    // Snap: 90% der Restdistanz sofort (Subpixel-Akku für den Rest)
    static float rCarryX = 0.f, rCarryY = 0.f;
    float dx = rageScr.x - cx, dy = rageScr.y - cy;
    float dist = sqrtf(dx*dx + dy*dy);
    if (dist > 1.f) {
        float totalX = dx * 0.9f + rCarryX;
        float totalY = dy * 0.9f + rCarryY;
        int ix = (int)totalX; rCarryX = totalX - ix;
        int iy = (int)totalY; rCarryY = totalY - iy;
        if (ix != 0 || iy != 0) SimulateMouseMove(ix, iy);
    } else { rCarryX = 0.f; rCarryY = 0.f; }

    // Autofire: Fadenkreuz sitzt (nah dran) -> Klick im Intervall
    if (g_cfg.rageAutoFire && dist < 12.f) {
        static DWORD rLastShot = 0;
        DWORD now = GetTickCount();
        int gap = g_cfg.rageInterval > 0 ? g_cfg.rageInterval : 100;
        if (now - rLastShot >= (DWORD)gap) {
            SimulateMouseClick();
            rLastShot = now;
        }
    }
    return true;
}