#include "pch.h"
#include "Recoil.h"
#include "Cache.h"
#include "Weapons.h"
#include "../core/Globals.h"
#include "../core/Input.h"

// ─────────────────────────────────────────────────────────────────────────────
// features/Recoil.cpp — Muster-basierte Recoil-Kompensation
//
// CS2 gibt Punch-Angles extern nicht zuverlaessig heraus → wir tracken
// m_iShotsFired und schieben die Maus pro Schuss um ein waffen-spezifisches
// Muster nach unten (in Pixel, umgerechnet ueber die Sensitivitaet).
// FIX 2026-08-28: Vorher wurde JEDEN Frame die GESAMTE akkumulierte Distanz
// (accRecoilY ≈ 300px nach 10 Schuessen) erneut per SendInput geschickt —
// bei 60 FPS = ~18000 px/s nach unten, Maus wirkte "eingefroren".
// Jetzt: nur das Delta des aktuellen Schusses wird einmalig bewegt (+ Carry).
// ─────────────────────────────────────────────────────────────────────────────

namespace {
    struct RecoilPattern { float pitchPerShot; float yawPerShot; float totalShots; };

    // Index = Waffen-ID (Item Definition Index); 0-Eintraege = kein Muster
    constexpr RecoilPattern kPatterns[] = {
        { 0.0f, 0.0f, 0 },       // 0: unknown
        { 0.0f, 0.0f, 0 },       // 1: Deagle
        { 0.0f, 0.0f, 0 },       // 2: Dualies
        { 0.0f, 0.0f, 0 },       // 3: Five-SeveN
        { 0.0f, 0.0f, 0 },       // 4: Glock
        { 0.0f, 0.0f, 0 },       // 5: unused
        { 0.0f, 0.0f, 0 },       // 6: unused
        { 1.85f, 0.0f, 30 },     // 7: AK-47
        { 1.60f, 0.0f, 30 },     // 8: AUG
        { 0.0f, 0.0f, 0 },       // 9: AWP (semi-auto, pattern not useful)
        { 1.50f, 0.0f, 30 },     // 10: FAMAS
        { 0.0f, 0.0f, 0 },       // 11: G3SG1
        { 0.0f, 0.0f, 0 },       // 12: unused
        { 1.50f, 0.0f, 30 },     // 13: Galil AR
        { 0.0f, 0.0f, 0 },       // 14: M249
    };
}

void RunRecoilControl(const GameCache& cache, int scrH) {
    if (!g_cfg.recoilEnable || !cache.valid) return;

    static int   lastShotsFired = 0;
    static float carryX = 0.f;
    static float carryY = 0.f;
    static DWORD lastRcsPrint = 0;
    // Punch-Live-State (eigene Carry, kein Cross-Talk bei Modus-Wechsel)
    static Vector3 s_prevPunch{};
    static bool    s_havePunch = false;
    static float   s_pcarryX = 0.f, s_pcarryY = 0.f;

    bool isShooting = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    DWORD now = GetTickCount();

    auto resetAll = [&]() {
        carryX = 0.f; carryY = 0.f;
        s_pcarryX = 0.f; s_pcarryY = 0.f;
        s_havePunch = false;
    };

    // Nur beim Schiessen und wenn Schusszahl gestiegen ist bewegen
    if (isShooting && cache.shotsFired > 0) {
        int delta = cache.shotsFired - lastShotsFired;

        // Modus-Filter (Schuss-Fenster) - gilt fuer BEIDE Quellen
        int sc = cache.shotsFired;
        bool shouldMove = true;
        if (g_cfg.recoilMode == 1 && sc > g_cfg.recoilModeShots) shouldMove = false;
        if (g_cfg.recoilMode == 2 && sc <= g_cfg.recoilModeShots) shouldMove = false;
        if (g_cfg.recoilMaxShots > 0 && sc > g_cfg.recoilMaxShots) shouldMove = false;

        float sens = g_cfg.recoilSensitivity;
        if (sens < 0.1f) sens = 2.5f;
        float degPerCount = sens * 0.022f;
        if (degPerCount < 0.001f) degPerCount = 0.022f;
        float str = g_cfg.recoilStrength / 100.f;
        float sm = ImMax(1.f, g_cfg.recoilSmooth);

        if (g_cfg.recoilSource == 1) {
            // ── Punch-Live (Seed-Stil): echter Kick pro Frame ──────────────
            // Kompensiert exakt was das Spiel gerade gekickt hat (inkl.
            // Zufalls-Anteil). Herleitung aus internem RCS
            // (view -= punch*2) über Maus-Faktoren (yaw -= m_yaw*dx,
            // pitch += m_pitch*dy): mx = +2*dYaw, my = -2*dPitch.
            // Mouse-RUNTER für negativen Pitch-Delta (Kick nach oben) etc.
            if (shouldMove) {
                Vector3 cur = cache.punchAngle;
                if (s_havePunch) {
                    float dYaw   = cur.y - s_prevPunch.y;
                    float dPitch = cur.x - s_prevPunch.x;
                    // Plausibilitaet: Frame-Deltas >5 Grad sind Drift/Snaps
                    if (fabsf(dYaw) < 5.f && fabsf(dPitch) < 5.f &&
                        (dYaw != 0.f || dPitch != 0.f)) {
                        float mx = +dYaw   * 2.f * str * g_cfg.recoilXScale / degPerCount;
                        float my = -dPitch * 2.f * str * g_cfg.recoilYScale / degPerCount;
                        mx /= sm; my /= sm;
                        // Drift-Schutz: max 60px/Frame
                        if (mx > 60.f) mx = 60.f; else if (mx < -60.f) mx = -60.f;
                        if (my > 60.f) my = 60.f; else if (my < -60.f) my = -60.f;
                        float fx = mx + s_pcarryX;
                        float fy = my + s_pcarryY;
                        int ix = (int)fx, iy = (int)fy;
                        s_pcarryX = fx - (float)ix;
                        s_pcarryY = fy - (float)iy;

                        if (g_cfg.debugLog && now - lastRcsPrint > 2000) {
                            printf("[rcs punch] shots=%d dYaw=%.3f dPitch=%.3f move=(%d,%d)\n",
                                   cache.shotsFired, dYaw, dPitch, ix, iy);
                            lastRcsPrint = now;
                        }

                        if (ix != 0 || iy != 0)
                            SimulateMouseMoveExternal(ix, iy);
                    }
                }
                s_prevPunch = cur;
                s_havePunch = true;
            }
            if (delta < 0) resetAll();
        } else if (delta > 0 && delta < 10) {
            // Plausibilitaet Legacy: delta 1-5, sonst Waffenwechsel/Fehler
            if (shouldMove) {
                float pitchPerShot = 1.7f;
                float yawPerShot = 0.0f;
                int wid = cache.currentWeaponId;
                if (wid >= 0 && wid < (int)(sizeof(kPatterns)/sizeof(kPatterns[0]))) {
                    // Nur ueberschreiben wenn Muster definiert (pitch != 0)
                    if (kPatterns[wid].pitchPerShot != 0.f || kPatterns[wid].yawPerShot != 0.f) {
                        pitchPerShot = kPatterns[wid].pitchPerShot;
                        yawPerShot   = kPatterns[wid].yawPerShot;
                    }
                }

                float moveY = (pitchPerShot * (float)delta) / degPerCount;
                float moveX = (yawPerShot   * (float)delta) / degPerCount;

                // Staerke + Skalierung + Glaettung  (einmalig pro Schuss, nicht pro Frame)
                moveX *= str * g_cfg.recoilXScale;
                moveY *= str * g_cfg.recoilYScale;
                moveX /= sm;
                moveY /= sm;

                // Subpixel-Akkumulator fuer weiche Bewegung (verhindert 0px-Rundung)
                float fx = moveX + carryX;
                float fy = moveY + carryY;
                int ix = (int)fx;
                int iy = (int)fy;
                carryX = fx - (float)ix;
                carryY = fy - (float)iy;

                if (g_cfg.debugLog && now - lastRcsPrint > 2000) {
                    printf("[rcs] shots=%d delta=%d move=(%d,%d) carry=(%.2f,%.2f)\n",
                           cache.shotsFired, delta, ix, iy, carryX, carryY);
                    lastRcsPrint = now;
                }

                if (ix != 0 || iy != 0)
                    SimulateMouseMoveExternal(ix, iy);
            }
        } else if (delta < 0) {
            // Schusszahl gesunken (Nachladen / Waffenwechsel) -> Carry reset
            resetAll();
        }

        lastShotsFired = cache.shotsFired;
    } else {
        // Nicht am Schiessen: Reset — naechster Burst startet bei delta=1
        if (lastShotsFired != 0 || carryX != 0.f || carryY != 0.f) {
            resetAll();
        }
        lastShotsFired = 0;
        // lastShotsFired bleibt 0 bis naechster Schuss; verhindert Riesen-Delta
        if (!isShooting)
            lastShotsFired = 0;
        else if (cache.shotsFired == 0)
            lastShotsFired = 0;
    }

    // Waffen-Indikator unten links
    if (g_cfg.recoilShowIndicator && cache.currentWeaponId > 0) {
        const char* weaponName = weapons::GetWeaponName(cache.currentWeaponId);
        if (!weaponName) weaponName = "Unknown";
        ImDrawList* dl = ImGui::GetBackgroundDrawList();
        
        char info[48]{};
        bool isShooting = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        if (isShooting && cache.shotsFired > 0)
            snprintf(info, sizeof(info), " [%d]", cache.shotsFired);

        float nameW = ImGui::GetFont()->CalcTextSizeA(12.f, FLT_MAX, 0.f, weaponName).x;
        float infoW = info[0] ? ImGui::GetFont()->CalcTextSizeA(12.f, FLT_MAX, 0.f, info).x : 0.f;
        float pillW = nameW + infoW + 16.f;
        float pillH = 22.f;
        ImVec2 p0(12.f, (float)scrH - 34.f);
        ImVec2 p1(p0.x + pillW, p0.y + pillH);

        // Pill background + dark outline
        dl->AddRectFilled(p0, p1, IM_COL32(14, 16, 22, 225), 4.f);
        dl->AddRect(ImVec2(p0.x - 1.f, p0.y - 1.f), ImVec2(p1.x + 1.f, p1.y + 1.f), IM_COL32(0, 0, 0, 160), 4.f, 0, 1.5f);
        dl->AddRect(p0, p1, IM_COL32(65, 75, 95, 180), 4.f, 0, 1.0f);

        bool hasRecoil = cache.shotsFired > 0;
        ImU32 textCol = hasRecoil ? IM_COL32(110, 230, 140, 255) : IM_COL32(215, 222, 235, 235);
        ImVec2 textPos(p0.x + 8.f, p0.y + 4.f);
        if (g_cfg.espShadow)
            dl->AddText(nullptr, 12.f, ImVec2(textPos.x + 1.f, textPos.y + 1.f), IM_COL32(0, 0, 0, 180), weaponName);
        dl->AddText(nullptr, 12.f, textPos, textCol, weaponName);

        if (info[0]) {
            ImVec2 infoPos(textPos.x + nameW + 2.f, textPos.y);
            if (g_cfg.espShadow)
                dl->AddText(nullptr, 12.f, ImVec2(infoPos.x + 1.f, infoPos.y + 1.f), IM_COL32(0, 0, 0, 180), info);
            dl->AddText(nullptr, 12.f, infoPos, IM_COL32(255, 190, 70, 255), info);
        }
    }
}
