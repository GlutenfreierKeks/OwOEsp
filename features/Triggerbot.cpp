#include "pch.h"
#include "Triggerbot.h"
#include "Cache.h"
#include "Offsets.h"
#include "Weapons.h"
#include "Reader.h"
#include "../core/Globals.h"
#include "../core/Input.h"

#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <cstdio>
#include <cstring>

// Diagnose-Spiegel ins Log (Remote-Diagnose).
extern FILE* g_logFile;
#undef printf
#define printf(...) do { ::printf(__VA_ARGS__); \
    if (g_logFile) { ::fprintf(g_logFile, __VA_ARGS__); ::fflush(g_logFile); } } while (0)

// ─────────────────────────────────────────────────────────────────────────────
// features/Triggerbot.cpp — Hybrid Triggerbot (UC Seed-Style + Legacy)
//
// Modus 0 = Legacy Pixel-Radius (WorldToScreen Brust, wie bisher)
// Modus 1 = Crosshair-ID (UnknownCheats "Simple External Triggerbot"):
//           Server-Trace m_iIDEntIndex — kein Projektionsfehler, keine Wand-
//           schüsse, exakt was das Fadenkreuz wirklich trifft.
// Modus 2 = Seed-Hybrid (empfohlen, UC "external seed triggerbot" / Omnis):
//           Crosshair-ID + Spread-Gate über m_fAccuracyPenalty + Speed.
//           Hält den Schuss, bis die Waffe genau ist — der Schuss landet
//           sofort wenn die Accuracy sich erholt, statt in den Spread zu feuern.
//
// Externes Seed-Prinzip (intern wäre SHA1(viewangles,tick)+Valve-RNG):
//   Extern lesen wir die live akkumulierte Ungenauigkeit (m_fAccuracyPenalty).
//   Movement-Inaccuracy steckt NICHT drin (separater Velocity-Term) — darum
//   gaten wir auf BEIDES (Omnis-CS2-External Ansatz).
//
// UC-Fund 16.10.2026 — WARUM der Trigger "sehr oft" fehlschlug:
// CS2 hat auf dem Pawn zwei CountdownTimer:
//   m_delayTargetIDTimer — setzt m_iIDEntIndex erst nach kurzer Verzögerung
//   m_holdTargetIDTimer  — hält die ID nach dem Verlassen des Ziels weiter
//                          (Name flackert nicht) — die ID ist also kurz
//                          "veraltet", während das Fadenkreuz schon weg ist.
// Wer blind auf die ID feuert, schießt genau dann ins Leere. Fix hier:
//   1) Ray-Check (Default an): Ziel-Knochen FRISCH lesen; nur feuern wenn
//      der Fadenkreuz-Strahl (Augenposition + dwViewAngles, jeden Frame
//      frisch) wirklich noch auf dem Modell liegt.
//   2) Stand-Gate: max. triggerbotMaxSpeed (Default 60 u/s). Die alten
//      160 u/s ließen Shift-Walk durch — der ist NICHT genau (sichere Misses).
// Die Timer-Timestamps gehen ins Debug-Log (Semantik kalibrieren).
// ─────────────────────────────────────────────────────────────────────────────

namespace {
    constexpr uintptr_t kEntStridePrimary = 0x70; // wie Reader.cpp
    constexpr uintptr_t kEntStrideFallback = 0x78; // UC-Srcs nutzen 120

    // Trigger-State (nur Render-Thread)
    static DWORD s_firstSeen = 0;
    static DWORD s_lastShot = 0;
    static DWORD s_lastValid = 0;
    static int   s_jitterMs = 0;
    static bool  s_tracking = false;
    static bool  s_randInit = false;
    static DWORD s_lastDbg = 0;
    static int   s_consec = 0; // aufeinanderfolgende gültige Frames (Ghost-Filter)
    static DWORD s_lastWarn = 0;
    static bool  s_clickHeld = false; // Maustaste unten? (non-blocking 15ms-Hold)
    static DWORD s_clickUpAt = 0;

    void ResetTrack() {
        s_tracking = false;
        s_firstSeen = 0;
        s_jitterMs = 0;
        s_consec = 0;
    }

    // Offenen Klick schließen (fällt nach 15ms). Läuft am Funktionsanfang,
    // damit nie ein DOWN hängen bleibt — auch bei Loslassen/Zielverlust.
    // HINWEIS: kein Sleep hier! Sleep(15) im Render-Thread hat jeden Schuss
    // den Frame um 15ms gestoppt (sichtbares Stottern beim Testen).
    void TickClickUp(DWORD now) {
        if (s_clickHeld && (int32_t)(now - s_clickUpAt) >= 0) {
            INPUT up{};
            up.type = INPUT_MOUSE;
            up.mi.dwFlags = MOUSEEVENTF_LEFTUP;
            SendInput(1, &up, sizeof(INPUT));
            s_clickHeld = false;
        }
    }

    // Schuss auslösen: DOWN sofort + UP in 15ms (tick-sicher, non-blocking).
    void FireClick(DWORD now) {
        if (s_clickHeld) return; // UP steht noch aus — kein Doppel-DOWN
        INPUT dn{};
        dn.type = INPUT_MOUSE;
        dn.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
        SendInput(1, &dn, sizeof(INPUT));
        s_clickHeld = true;
        s_clickUpAt = now + 15;
    }

    uintptr_t ResolveEntIndex(uintptr_t entitySystem, int32_t index) {
        if (!entitySystem || index <= 0 || index >= 2048) return 0;
        for (int attempt = 0; attempt < 2; attempt++) {
            uintptr_t stride = (attempt == 0) ? kEntStridePrimary : kEntStrideFallback;
            uintptr_t list = g_mem.Read<uintptr_t>(entitySystem + 0x10 + 8 * (index >> 9));
            if (!list) continue;
            uintptr_t ent = g_mem.Read<uintptr_t>(list + stride * (index & 0x1FF));
            if (ent) return ent;
        }
        return 0;
    }

    bool IsBadWeapon(int wid) {
        if (wid <= 0) return false; // unbekannt -> fail-open (nicht blocken)
        if (weapons::IsGrenade(wid)) return true;
        if (wid == weapons::WEAPON_C4) return true;
        if (wid == weapons::WEAPON_ZEUS) return true;
        if (wid == weapons::WEAPON_KNIFE || wid == weapons::WEAPON_KNIFE_T) return true;
        if (wid >= 500 && wid <= 526) return true; // Messer-Skins
        return false;
    }

    bool NeedsScope(int wid) {
        return wid == weapons::WEAPON_AWP || wid == weapons::WEAPON_SSG08 ||
               wid == weapons::WEAPON_G3SG1 || wid == weapons::WEAPON_SCAR20;
    }

    // ── Ray-Check: ist das Fadenkreuz WIRKLICH (noch) auf dem Modell? ──────
    // CS2-Target-ID-Timer (UC-Fund): Die ID erscheint verzögert und bleibt
    // nach dem Verlassen des Ziels kurz gesetzt ("Hold") — wer blind auf die
    // ID feuert, schießt in der Hold-Phase ins Leere. Deshalb prüfen wir den
    // Fadenkreuz-Strahl live gegen die Knochen des Ziels. Radien sind bewusst
    // grobzügig (Interp-Lag ~30ms + Knochen-Lesen): Der Check verwirft nur
    // "klar daneben", er ist keine pixelgenaue Hitbox-Prüfung.
    struct RaySeg { int a; int b; float r; };
    static constexpr RaySeg kRaySegs[] = {
        { 6,  6,  8.0f},                     // Kopf
        { 5,  6,  6.5f}, { 4,  5,  6.5f},    // Hals / Brust->Hals
        { 2,  4, 10.0f}, { 0,  2, 10.0f},    // Torso (Wirbelsäule)
        {22, 23,  8.5f}, {23, 24,  7.5f},    // Bein L
        {25, 26,  8.5f}, {26, 27,  7.5f},    // Bein R
        { 4,  8,  7.5f}, { 8,  9,  6.5f},    // Arm L
        { 4, 13,  7.5f}, {13, 14,  6.5f},    // Arm R
    };

    // Welt-Knochen des Ziels FRISCH lesen (ein Bulk-Read, Render-Thread).
    // Rückgabe = Bitmaske gültiger Bones (0..29), 0 = nicht lesbar.
    static uint32_t ReadTargetBoneWorld(uintptr_t node, const Vector3& origin, Vector3 out[30]) {
        if (!node) return 0;
        static constexpr int kRaw = 30, kStride = 32;
        uintptr_t cands[2];
        cands[0] = g_mem.Read<uintptr_t>(node + schemas::m_modelState + schemas::kBoneArrayInModelState);
        cands[1] = g_mem.Read<uintptr_t>(node + 0x1E0); // Legacy-Kandidat wie Reader
        for (uintptr_t arr : cands) {
            if (!arr || arr < 0x10000) continue;
            uint8_t raw[kRaw * kStride]{};
            if (!g_mem.ReadBuffer(arr, raw, sizeof(raw))) continue;
            uint32_t mask = 0;
            for (int i = 0; i < kRaw; i++) {
                float x, y, z;
                memcpy(&x, raw + (size_t)i * kStride + 0, 4);
                memcpy(&y, raw + (size_t)i * kStride + 4, 4);
                memcpy(&z, raw + (size_t)i * kStride + 8, 4);
                if (x == 0.f && y == 0.f && z == 0.f) {
                    memcpy(&x, raw + (size_t)i * kStride + 16, 4);
                    memcpy(&y, raw + (size_t)i * kStride + 20, 4);
                    memcpy(&z, raw + (size_t)i * kStride + 24, 4);
                    if (x == 0.f && y == 0.f && z == 0.f) continue;
                }
                if (!isfinite(x) || !isfinite(y) || !isfinite(z)) continue;
                if (fabsf(x) > 20000.f || fabsf(y) > 20000.f) continue;
                float dx = x - origin.x, dy = y - origin.y, dz = z - origin.z;
                if (dx * dx + dy * dy > 150.f * 150.f) continue;
                if (dz < -30.f || dz > 130.f) continue;
                out[i] = Vector3{ x, y, z };
                mask |= (1u << i);
            }
            if (mask) {
                // Härtung: erst als brauchbar ansehen, wenn genug Knochen da
                // sind UND Kern-Knochen (Becken/Wirbel/Brust/Hals/Kopf) dabei
                // sind — sonst lieber Fallback-Kandidat / fail-open.
                uint32_t core = (1u << 0) | (1u << 2) | (1u << 4) | (1u << 5) | (1u << 6);
                int total = 0, coreCnt = 0;
                for (uint32_t m = mask; m; m &= m - 1) total++;
                for (uint32_t m = mask & core; m; m &= m - 1) coreCnt++;
                if (total >= 6 && coreCnt >= 2) return mask;
            }
        }
        return 0;
    }

    // Abstand² zwischen Segment A->B und Halbstrahl E + t*d (|d|=1, t>=0).
    static float SegmentRayDistSq(const Vector3& A, const Vector3& B,
                                  const Vector3& E, const Vector3& d) {
        float ux = B.x - A.x, uy = B.y - A.y, uz = B.z - A.z;
        float wx = A.x - E.x, wy = A.y - E.y, wz = A.z - E.z;
        float a = ux * ux + uy * uy + uz * uz;
        float b = ux * d.x + uy * d.y + uz * d.z;
        float e = wx * d.x + wy * d.y + wz * d.z;
        float f = wx * ux + wy * uy + wz * uz;
        float s = 0.f, t = 0.f;
        if (a < 1e-6f) {          // degeneriertes Segment: Punkt vs. Strahl
            t = -e; if (t < 0.f) t = 0.f;
        } else {
            float denom = a - b * b;
            t = (denom > 1e-6f) ? (a * e - b * f) / denom : 0.f;
            s = (t * b - f) / a;
            if (s < 0.f) s = 0.f; else if (s > 1.f) s = 1.f;
            t = e + s * b;         // exaktes t für geclampetes s
            if (t < 0.f) {         // Strahl "hinter" dem Segment-Anfang
                t = 0.f;
                s = -f / a;        // Projektion der Augenposition aufs Segment
                if (s < 0.f) s = 0.f; else if (s > 1.f) s = 1.f;
            }
        }
        float px = E.x + d.x * t - (A.x + ux * s);
        float py = E.y + d.y * t - (A.y + uy * s);
        float pz = E.z + d.z * t - (A.z + uz * s);
        return px * px + py * py + pz * pz;
    }

    // Fadenkreuz auf dem Modell? (fail-open nur wenn keine Knochen lesbar)
    static bool CrosshairOnBones(const Vector3& eye, const Vector3& ang,
                                 const Vector3 bones[30], uint32_t mask) {
        if (!mask) return true;
        float p = ang.x * (3.14159265f / 180.f);
        float y = ang.y * (3.14159265f / 180.f);
        float cp = cosf(p);
        Vector3 d{ cp * cosf(y), cp * sinf(y), -sinf(p) };
        for (const RaySeg& seg : kRaySegs) {
            if (!(mask & (1u << seg.a)) || !(mask & (1u << seg.b))) continue;
            if (SegmentRayDistSq(bones[seg.a], bones[seg.b], eye, d) <= seg.r * seg.r)
                return true;
        }
        return false;
    }

    // Ziel-ID-Timer als Debug-Werte (Semantik kalibrieren).
    static float IdTimerTs(uintptr_t pawn, uintptr_t timerOff) {
        if (!pawn || !timerOff) return 0.f;
        return g_mem.Read<float>(pawn + timerOff + 0x4);
    }

    // Waffen-ID robust lesen (AttributeManager driftet je nach Dump-Klasse).
    int ReadWeaponId(uintptr_t weapon) {
        if (!weapon) return 0;
        uintptr_t tryMgr[] = {
            schemas::m_AttributeManager,
            (uintptr_t)0x1290, // C_EconEntity (Waffen, aktuell)
            (uintptr_t)0x11A8, // alter Fallback
            (uintptr_t)0x13D0, // alter Cache-Wert
            (uintptr_t)0x14C8, // C_Chicken (falls Updater falsch liegt)
        };
        for (uintptr_t mgr : tryMgr) {
            if (!mgr) continue;
            uintptr_t eiv = weapon + mgr + schemas::m_Item;
            int16_t id = g_mem.Read<int16_t>(eiv + schemas::m_iItemDefinitionIndex);
            if (id > 0 && id < 1000) return (int)id;
        }
        return 0;
    }

    uintptr_t ReadActiveWeapon(uintptr_t localPawn) {
        uintptr_t ws = g_mem.Read<uintptr_t>(localPawn + schemas::m_pWeaponServices);
        if (!ws) return 0;
        int32_t handle = g_mem.Read<int32_t>(ws + 0x60); // m_hActiveWeapon
        if (handle <= 0) return 0;
        uintptr_t entitySystem = g_mem.Read<uintptr_t>(g_client + offsets::dwEntityList);
        if (!entitySystem) return 0;
        int idx = handle & 0x7FFF;
        uintptr_t list = g_mem.Read<uintptr_t>(entitySystem + 0x10 + 8 * (idx >> 9));
        if (!list) return 0;
        return g_mem.Read<uintptr_t>(list + kEntStridePrimary * (idx & 0x1FF));
    }

    void RunLegacy(const GameCache& cache, int scrW, int scrH) {
        const float cx = scrW * 0.5f, cy = scrH * 0.5f;
        const float trigPx = g_cfg.triggerbotPx;
        const float radiusSq = trigPx * trigPx;
        DWORD ageMs = GetTickCount() - cache.stampMs;

        for (int ti = 0; ti < cache.playerCount; ti++) {
            const auto& p = cache.players[ti];
            if (!p.alive) continue;
            if (g_cfg.triggerbotCheckTeam && p.team == cache.localTeam) continue;
            if (g_cfg.triggerbotCheckVis && !p.visible) continue;

            // Brusthohe als Trefferpunkt (vorhergesagt)
            Vector3 pred = PredictPos(p.worldOrigin, p.velocity, ageMs);
            Vector3 chestPos{ pred.x, pred.y,
                              pred.z + (p.crouching ? 48.f : 64.f) };
            Vector2 scr;
            if (!WorldToScreen(chestPos, scr, cache.vm, scrW, scrH)) continue;

            float dx = scr.x - cx, dy = scr.y - cy;
            if (dx * dx + dy * dy > radiusSq) continue;

            DWORD now = GetTickCount();
            if (now - s_lastShot >= (DWORD)g_cfg.triggerbotDelay) {
                FireClick(now);
                if (g_cfg.triggerbotRecoil > 0.f)
                    SimulateMouseMoveExternal(0, (int)g_cfg.triggerbotRecoil);
                s_lastShot = now;
            }
            break;
        }
    }
} // namespace

void RunTriggerbot(const GameCache& cache, const UtilCache& utilCache, int scrW, int scrH) {
    DWORD nowT = GetTickCount();
    TickClickUp(nowT); // offenes DOWN immer schließen (nie hängen lassen)
    if (!g_cfg.triggerbotEnable || !cache.valid) return;
    if (g_menuOpen) { ResetTrack(); return; }
    if (!(GetAsyncKeyState(g_cfg.triggerbotKey) & 0x8000)) { ResetTrack(); return; }

    int mode = g_cfg.triggerbotMode;
    if (mode < 0 || mode > 2) mode = 2;
    if (mode == 0) { RunLegacy(cache, scrW, scrH); return; }

    if (!s_randInit) { srand(GetTickCount()); s_randInit = true; }

    DWORD now = GetTickCount();

    uintptr_t localPawn = GetLocalPawn(); // Controller-Handle (kein dw nötig)
    if (!localPawn) { ResetTrack(); return; }

    // Tot? -> nichts tun
    {
        uint8_t life = g_mem.Read<uint8_t>(localPawn + schemas::m_lifeState);
        int32_t hp = g_mem.Read<int32_t>(localPawn + schemas::m_iHealth);
        if (life != 0 || hp <= 0) { ResetTrack(); return; }
    }

    // ── 1) UC Crosshair-ID (Server-Trace) ─────────────────────────────────
    int32_t crossId = g_mem.Read<int32_t>(localPawn + schemas::m_iIDEntIndex);
    bool targetValid = false;
    uintptr_t targetPawn = 0;
    int targetTeam = 0;

    if (crossId > 0 && crossId < 2048) {
        uintptr_t entitySystem = g_mem.Read<uintptr_t>(g_client + offsets::dwEntityList);
        targetPawn = ResolveEntIndex(entitySystem, crossId);
        if (targetPawn) {
            uint8_t tLife = g_mem.Read<uint8_t>(targetPawn + schemas::m_lifeState);
            int32_t tHp = g_mem.Read<int32_t>(targetPawn + schemas::m_iHealth);
            uint8_t tTeam = g_mem.Read<uint8_t>(targetPawn + schemas::m_iTeamNum);
            if (tLife == 0 && tHp > 0 && tHp <= 100 && tTeam >= 2 && tTeam <= 3) {
                targetTeam = (int)tTeam;
                if (g_cfg.triggerbotCheckTeam && targetTeam == cache.localTeam) {
                    targetValid = false; // Teammate
                } else {
                    targetValid = true;
                    // Optionaler Wand-Check über Reader-Sichtbarkeit:
                    // Crosshair-ID trifft per Definition nur Sichtbares (Wände
                    // blocken den Trace). Falls trotzdem an, gegen Cache prüfen.
                    if (g_cfg.triggerbotCheckVis) {
                        uintptr_t tNode = g_mem.Read<uintptr_t>(targetPawn + schemas::m_pGameSceneNode);
                        Vector3 tOrg = tNode ? g_mem.Read<Vector3>(tNode + schemas::m_vecAbsOrigin) : Vector3{};
                        bool visOk = false;
                        for (int i = 0; i < cache.playerCount; i++) {
                            const auto& p = cache.players[i];
                            if (!p.alive || p.team != targetTeam) continue;
                            float dx = p.worldOrigin.x - tOrg.x;
                            float dy = p.worldOrigin.y - tOrg.y;
                            float dz = p.worldOrigin.z - tOrg.z;
                            if (dx*dx + dy*dy + dz*dz < 2500.f) { // <50u = gleicher Pawn
                                visOk = p.visible;
                                break;
                            }
                        }
                        if (!visOk) targetValid = false;
                    }
                }
            }
        }
    }

    // ── 1b) Smoke-Check: Crosshair-Trace geht DURCH Smoke — halten! ───────
    // (Server liefert ID auch hinterm Nebel; Schuss wäre blind.)
    if (targetValid && g_cfg.triggerSmokeCheck) {
        uintptr_t tNode = g_mem.Read<uintptr_t>(targetPawn + schemas::m_pGameSceneNode);
        if (tNode) {
            Vector3 tOrg = g_mem.Read<Vector3>(tNode + schemas::m_vecAbsOrigin);
            if (SmokeBlocksShot(cache.localEyePos, tOrg, utilCache.zones, utilCache.count))
                targetValid = false;
        }
    }

    // ── 2) Flick-Latch (Omnis) + stabile ID: Ghost-Reads (ein einzelner
    // Frame mit gültiger ID — z.B. Hold-Timer nach Verlassen des Ziels)
    // starten KEIN Tracking, erst 2 aufeinanderfolgende Frames.
    if (targetValid) {
        s_lastValid = now;
        s_consec++;
        if (!s_tracking && s_consec >= 2) {
            s_tracking = true;
            s_firstSeen = now;
            int j = g_cfg.triggerbotJitter;
            if (j < 0) j = 0; if (j > 200) j = 200;
            s_jitterMs = (j > 0) ? (rand() % (j + 1)) : 0;
        }
    } else {
        if (s_tracking && (now - s_lastValid) <= (DWORD)(g_cfg.triggerbotLatchMs < 0 ? 0 : g_cfg.triggerbotLatchMs)) {
            // Fadenkreuz flackert über Kante — Delay NICHT zurücksetzen,
            // aber nicht in die Luft schiessen.
            return;
        }
        ResetTrack();
        return;
    }

    // ── 3) Reaktions-Delay + Jitter (humanized, UC trigger_delay) ──────────
    {
        DWORD need = (DWORD)g_cfg.triggerbotDelay + (DWORD)s_jitterMs;
        if (now - s_firstSeen < need) return;
    }

    // ── 4) Burst-Gap (min. Abstand zwischen Schüssen) ──────────────────────
    {
        int gap = g_cfg.triggerbotBurstGap;
        if (gap < 0) gap = 0;
        if (gap > 0 && (now - s_lastShot) < (DWORD)gap) return;
    }

    // ── 5) Waffen-Checks + Seed-Gate ───────────────────────────────────────
    float penalty = 0.f;
    float speed2d = 0.f;
    int wid = 0;
    bool scoped = true;
    {
        uintptr_t weapon = ReadActiveWeapon(localPawn);
        bool wantSeed = (mode == 2 && g_cfg.triggerbotSeedGate);
        // LAUTE Diagnose: ohne Weapon-Handle läuft das Seed-Gate ins Leere
        // (fail-open = kein Unterschied zu Modus 1). Dann Offsets prüfen!
        if (!weapon && wantSeed && now - s_lastWarn > 5000) {
            s_lastWarn = now;
            printf("[trigger] WARN: kein Weapon-Handle (Gate AUS, fail-open) — dwEntityList/m_pWeaponServices prüfen!\n");
        }
        if (weapon && g_cfg.triggerbotBlockBadWeapons) {
            wid = ReadWeaponId(weapon);
            if (IsBadWeapon(wid)) return; // Nade/C4/Messer/Zeus halten, Delay behalten
            int32_t clip = g_mem.Read<int32_t>(weapon + schemas::m_iClip1);
            // clip==0 nur blocken wenn Feld plausibel (sonst Offset-Drift)
            if (clip == 0) {
                // Gegenprobe: Reload-Flag — nur wenn wirklich leer/nachladend
                uint8_t rel = schemas::m_bInReload ? g_mem.Read<uint8_t>(weapon + schemas::m_bInReload) : 0;
                if (rel) return;
                // Ohne Reload-Flag trotzdem bei AWP/SSG mit clip 0 nicht schiessen
                if (wid == weapons::WEAPON_AWP || wid == weapons::WEAPON_SSG08) return;
            }
            uint8_t rel2 = schemas::m_bInReload ? g_mem.Read<uint8_t>(weapon + schemas::m_bInReload) : 0;
            if (rel2) return;
        } else if (weapon) {
            wid = ReadWeaponId(weapon);
        }
        // Fallback: Cache-Waffe wenn direkter Read 0 (Offset-Drift) — nur für
        // Bad-Weapon-Block, nie um zu feuern verhindern wenn unbekannt.
        if (wid == 0 && g_cfg.triggerbotBlockBadWeapons)
            wid = cache.currentWeaponId;

        if (g_cfg.triggerbotBlockBadWeapons && IsBadWeapon(wid)) return;

        if (g_cfg.triggerbotScopeCheck && NeedsScope(wid)) {
            scoped = g_mem.Read<uint8_t>(localPawn + schemas::m_bIsScoped) != 0;
            if (!scoped) return; // unscoped Sniper halten
        }

        if (wantSeed && weapon) {
            // Messer/Zeus haben keinen Spread — Gate überspringen
            bool melee = (wid == weapons::WEAPON_KNIFE || wid == weapons::WEAPON_KNIFE_T ||
                          (wid >= 500 && wid <= 526) || wid == weapons::WEAPON_ZEUS);
            if (!melee) {
                penalty = g_mem.Read<float>(weapon + schemas::m_fAccuracyPenalty);
                // Unplausibel (Offset-Drift/NaN) -> fail-open
                if (!(penalty >= 0.f && penalty < 5.f)) penalty = 0.f;
                if (penalty > g_cfg.triggerbotMaxPenalty) {
                    if (g_cfg.triggerbotShowDebug && now - s_lastDbg > 1000) {
                        s_lastDbg = now;
                        printf("[trigger] HOLD penalty=%.3f > %.2f wid=%d\n",
                               penalty, g_cfg.triggerbotMaxPenalty, wid);
                    }
                    return; // Waffe streut noch — halten bis sie sich setzt
                }
                // Movement-Inaccuracy (separater Term, nicht in Penalty):
                // Stand-Gate (Default 60 u/s): die früheren 160 erlaubten
                // Shift-Walk (~130) — der ist NICHT genau und kostete Treffer.
                // Counter-Strafe-Moment (kurz vor Stillstand) feuert sofort.
                Vector3 vel = g_mem.Read<Vector3>(localPawn + schemas::m_vecAbsVelocity);
                if (vel.x > -500.f && vel.x < 500.f) {
                    speed2d = sqrtf(vel.x * vel.x + vel.y * vel.y);
                    int maxSpd = g_cfg.triggerbotMaxSpeed;
                    if (maxSpd > 0 && speed2d > (float)maxSpd) {
                        if (g_cfg.triggerbotShowDebug && now - s_lastDbg > 1000) {
                            s_lastDbg = now;
                            printf("[trigger] HOLD speed=%.0f > %d wid=%d\n", speed2d, maxSpd, wid);
                        }
                        return; // laufen -> halten bis (fast) Stand
                    }
                }
            }
        }
    }

    // ── 5b) Ray-Check (UC-Fund: CS2 Target-ID-Delay/Hold) ───────────────────
    // m_iIDEntIndex bleibt nach dem Verlassen des Ziels kurz gültig und wird
    // bei neuem Ziel verzögert gesetzt (m_delay/m_holdTargetIDTimer). Ohne
    // Gegenprüfung feuert der Trigger in der Hold-Phase ins Leere. Läuft
    // erst nach den günstigen Gates (weniger Bone-Reads).
    if (g_cfg.triggerbotRayCheck) {
        uintptr_t tNode = g_mem.Read<uintptr_t>(targetPawn + schemas::m_pGameSceneNode);
        Vector3 tOrig = tNode ? g_mem.Read<Vector3>(tNode + schemas::m_vecAbsOrigin) : Vector3{};
        Vector3 tBones[30]{};
        uint32_t boneMask = ReadTargetBoneWorld(tNode, tOrig, tBones);
        if (boneMask) {
            // Augenposition frisch (Cache ist bis ~10ms alt, im Lauf bis ~3u)
            Vector3 eye = cache.localEyePos;
            uintptr_t lNode = g_mem.Read<uintptr_t>(localPawn + schemas::m_pGameSceneNode);
            if (lNode) {
                Vector3 lo = g_mem.Read<Vector3>(lNode + schemas::m_vecAbsOrigin);
                if (isfinite(lo.x) && isfinite(lo.y) && isfinite(lo.z)) {
                    uint32_t lf = g_mem.Read<uint32_t>(localPawn + schemas::m_fFlags);
                    eye = Vector3{ lo.x, lo.y, lo.z + ((lf & 2) ? 48.f : 64.f) };
                }
            }
            // Blickrichtung frisch (wie Renderer: dwViewAngles, jeden Frame)
            Vector3 va = g_mem.Read<Vector3>(g_client + offsets::dwViewAngles);
            if (!(isfinite(va.x) && isfinite(va.y) &&
                  va.x > -89.5f && va.x < 89.5f && va.y > -720.f && va.y < 720.f))
                va = cache.localViewAngles;
            if (!CrosshairOnBones(eye, va, tBones, boneMask)) {
                if (g_cfg.triggerbotShowDebug && now - s_lastDbg > 500) {
                    s_lastDbg = now;
                    printf("[trigger] RAY-Reject id=%d (Fadenkreuz real nicht auf Modell; idTs d=%.2f h=%.2f)\n",
                           (int)crossId,
                           IdTimerTs(localPawn, schemas::m_delayTargetIDTimer),
                           IdTimerTs(localPawn, schemas::m_holdTargetIDTimer));
                }
                return; // ID noch "gültig" — real daneben: nicht feuern
            }
        }
        // boneMask == 0 -> fail-open (Knochen nicht lesbar, altes Verhalten)
    }

    // ── 6) Feuer: DOWN sofort, UP nach 15ms (non-blocking, tick-sicher) ────
    // CS2 sampelt Input pro Tick (64-Tick = 15.6ms) — down+up im selben
    // virtuellen Moment kann im Tick untergehen (fühlt sich wie Miss an).
    FireClick(now);
    if (g_cfg.triggerbotRecoil > 0.f)
        SimulateMouseMoveExternal(0, (int)g_cfg.triggerbotRecoil);
    s_lastShot = now;

    if (g_cfg.triggerbotShowDebug && now - s_lastDbg > 1000) {
        s_lastDbg = now;
        printf("[trigger] SHOT id=%d team=%d wid=%d pen=%.3f delay=%d+%d | idTs d=%.2f h=%.2f\n",
               (int)crossId, targetTeam, wid, penalty,
               g_cfg.triggerbotDelay, s_jitterMs,
               IdTimerTs(localPawn, schemas::m_delayTargetIDTimer),
               IdTimerTs(localPawn, schemas::m_holdTargetIDTimer));
    }
    // Tracking behalten (weiter auf gleichem Ziel ohne Re-Delay), BurstGap
    // verhindert Dauerfeuer bei Semis. Neuer Jitter erst bei neuem Ziel.
}
