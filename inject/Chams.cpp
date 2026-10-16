// inject/Chams.cpp — Team-Poll + Feld-Tönung pro Modus (Team bekannt).
//
// Modi (sichtbarer Pass; XQZ-Weiß kommt aus dem DI-Hook in ModelChams.cpp):
// Flat deckend, Glass/Ghost alpha, Rainbow/Pulse animiert, Health nach HP.
// Einmalig OffScan (Sig-Heilung), danach g_off. SEH-geschützt, Clear-Pass.
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include "Chams.h"
#include "ModelChams.h"
#include "GameOffsets.h"
#include "OffScan.h"
#include "OverlayESP.h" // eyeSrc (Winkel-Quelle)

extern void Log(const char* fmt, ...); // DllMain.cpp

namespace {
uintptr_t g_client = 0;

uintptr_t ResolveEntity(uintptr_t entitySystem, uint32_t idx) {
    uintptr_t chunk = *(uintptr_t*)(entitySystem + 0x10 + 8 * (idx >> 9));
    if (!chunk) return 0;
    return *(uintptr_t*)(chunk + injoff::kListStride * (idx & 0x1FF));
}

uintptr_t ResolvePawn(uintptr_t entitySystem, uint32_t handle) {
    uint32_t idx = handle & 0x7FFF;
    if (idx == 0 || idx == 0x7FFF) return 0;
    return ResolveEntity(entitySystem, idx);
}

uint32_t PackTint(const float c[3], uint8_t a) {
    auto cl = [](float v) -> uint32_t {
        if (v < 0.f) v = 0.f;
        if (v > 1.f) v = 1.f;
        return (uint32_t)(v * 255.f);
    };
    return ((uint32_t)a << 24) | (cl(c[0]) << 16) | (cl(c[1]) << 8) | cl(c[2]);
}
} // namespace

ChamsState g_chams;

void RunChams() {
    DWORD now = GetTickCount();

    if (!g_client) {
        g_client = (uintptr_t)GetModuleHandleW(L"client.dll");
        if (!g_client) return;
    }
    // Einmalig Offsets per Sig heilen (danach g_off).
    {
        static bool s_scanned = false;
        if (!s_scanned) {
            s_scanned = true;
            ScanGameOffsets();
        }
    }

    // Hook lazy installieren, sobald der Master an ist.
    if (g_chams.glowEnable && !g_mchams.hooked) {
        static bool s_tried = false;
        if (!s_tried) {
            s_tried = true;
            InitModelChams();
        }
    }

    __try {
        uintptr_t entitySystem = *(uintptr_t*)(g_client + g_off.entityList);
        if (!entitySystem) {
            static DWORD s_lastLog = 0;
            if (now - s_lastLog > 5000) {
                s_lastLog = now;
                Log("Chams: kein EntitySystem (OffScan ohne Treffer?)");
            }
            g_chams.offsetsOk = false;
            g_chams.applied = 0;
            g_hookLocalTeam = -1;
            return;
        }
        uintptr_t localCtrl = *(uintptr_t*)(g_client + g_off.localPlayer);
        if (!localCtrl) {
            g_chams.offsetsOk = false;
            g_chams.applied = 0;
            g_hookLocalTeam = -1;
            return;
        }
        g_chams.offsetsOk = true;

        uint32_t lh = *(uint32_t*)(localCtrl + g_off.hPawn);
        uintptr_t lpawn = ResolvePawn(entitySystem, lh);
        if (!lpawn) {
            g_hookLocalTeam = -1;
            g_chams.applied = 0;
            return;
        }
        int localTeam = *(uint8_t*)(lpawn + g_off.team);
        g_hookLocalTeam = localTeam;

        // Winkel-Quelle: manuell > Auto-Detect (exakt-Null = Velocity).
        // m_angEyeAngles vs. Velocity liegen 0xD0 auseinander; nur EINES
        // schwimmt beim Zielen, das andere steht bei Stillstand exakt null.
        if (g_esp.eyeSrc == 1) {
            if (g_off.eye != 0x35F0) {
                g_off.eye = 0x35F0;
                Log("EyeAngles: manuell 0x35F0");
            }
        } else if (g_esp.eyeSrc == 2) {
            if (g_off.eye != 0x3600) {
                g_off.eye = 0x3600;
                Log("EyeAngles: manuell 0x3600");
            }
        } else {
            static DWORD s_lastPick = 0;
            static int s_picked = 0; // 0=offen, 1=35F0, 2=3600
            static DWORD s_zeroA = 0, s_zeroB = 0;
            if (now - s_lastPick > 200) {
                s_lastPick = now;
                float* a = (float*)(lpawn + 0x35F0);
                float* b = (float*)(lpawn + 0x3600);
                bool za = (a[0] == 0.f && a[1] == 0.f && a[2] == 0.f);
                bool zb = (b[0] == 0.f && b[1] == 0.f && b[2] == 0.f);
                if (za && !zb) {
                    if (!s_zeroA) s_zeroA = now;
                    if (now - s_zeroA > 2000 && s_picked != 2) {
                        s_picked = 2;
                        g_off.eye = 0x3600;
                        Log("EyeAngles: Auto -> 0x3600 (0x35F0 stand null = Velocity)");
                    }
                } else s_zeroA = 0;
                if (zb && !za) {
                    if (!s_zeroB) s_zeroB = now;
                    if (now - s_zeroB > 2000 && s_picked != 1) {
                        s_picked = 1;
                        g_off.eye = 0x35F0;
                        Log("EyeAngles: Auto -> 0x35F0 (0x3600 stand null = Velocity)");
                    }
                } else s_zeroB = 0;
            }
        }
        // Feld-Tönung pro Modus + Clear-Pass. Zählt nebenbei lebende Pawns.
        static bool s_tintActive = false;
        static int s_lastMode = -1;
        static DWORD s_lastCount = 0;
        auto modeNow = (ChamsMaterialMode)g_chams.material;
        bool wantTint = g_chams.glowEnable;
        bool modeChanged = ((int)modeNow != s_lastMode);
        if (wantTint || (s_tintActive && !wantTint) || (s_tintActive && modeChanged)) {
            const uint8_t glassA = (uint8_t)(g_chams.glassAlpha < 0 ? 0
                                             : g_chams.glassAlpha > 255 ? 255
                                             : g_chams.glassAlpha);
            float tSec = (float)now / 1000.f;
            int count = 0;
            for (uint32_t i = 1; i < 64; i++) {
                uintptr_t ctrl = ResolveEntity(entitySystem, i);
                if (!ctrl) continue;
                uint32_t ph = *(uint32_t*)(ctrl + g_off.hPawn);
                uintptr_t pawn = ResolvePawn(entitySystem, ph);
                if (!pawn) continue;
                int32_t hp = *(int32_t*)(pawn + g_off.health);
                if (hp <= 0 || hp > 100) continue;
                count++;
                int team = *(uint8_t*)(pawn + g_off.team);
                bool isMate = (team == localTeam);
                if (isMate && !g_chams.glowTeammates) continue;
                if (!isMate && !g_chams.glowEnemies) continue;
                if (!wantTint) {
                    *(uint32_t*)(pawn + g_off.clr) = 0xFFFFFFFFu;
                    *(uint8_t*)(pawn + g_off.rmode) = 0;
                    continue;
                }
                const float* base = isMate ? g_chams.teamColor : g_chams.enemyColor;
                uint32_t tint = 0xFFFFFFFFu;
                uint8_t rm = 0;
                switch (modeNow) {
                case ChamsMaterialMode::Glass:
                    tint = PackTint(base, glassA);
                    rm = 1;
                    break;
                case ChamsMaterialMode::Rainbow: {
                    float h = fmodf(tSec * 90.f + (float)i * 14.f, 360.f);
                    if (h < 0.f) h += 360.f;
                    float x = 1.f - fabsf(fmodf(h / 60.f, 2.f) - 1.f);
                    float r = 0.f, g2 = 0.f, b2 = 0.f;
                    if (h < 60.f) { r = 1.f; g2 = x; }
                    else if (h < 120.f) { r = x; g2 = 1.f; }
                    else if (h < 180.f) { g2 = 1.f; b2 = x; }
                    else if (h < 240.f) { g2 = x; b2 = 1.f; }
                    else if (h < 300.f) { r = x; b2 = 1.f; }
                    else { r = 1.f; b2 = x; }
                    float cc[3] = { r, g2, b2 };
                    tint = PackTint(cc, 255);
                    break;
                }
                case ChamsMaterialMode::Pulse: {
                    float s = sinf(tSec * 4.f);
                    if (s < 0.f) s = -s;
                    tint = PackTint(base, (uint8_t)(90 + 165 * s));
                    rm = 1;
                    break;
                }
                case ChamsMaterialMode::Health: {
                    float f = (float)hp / 100.f;
                    float cc[3];
                    if (f < 0.5f) { cc[0] = 1.f; cc[1] = f * 2.f; cc[2] = 0.f; }
                    else { cc[0] = (1.f - f) * 2.f; cc[1] = 1.f; cc[2] = 0.f; }
                    tint = PackTint(cc, 255);
                    break;
                }
                case ChamsMaterialMode::Ghost:
                    tint = PackTint(base, 40);
                    rm = 1;
                    break;
                case ChamsMaterialMode::Flat:
                default:
                    tint = PackTint(base, 255);
                    break;
                }
                *(uint32_t*)(pawn + g_off.clr) = tint;
                *(uint8_t*)(pawn + g_off.rmode) = rm;
            }
            s_tintActive = wantTint;
            s_lastMode = (int)modeNow;
            // applied direkt aus dem Durchlauf (kein zweiter Loop nötig).
            s_lastCount = now;
            g_chams.applied = count;
        } else if (now - s_lastCount > 500) {
            s_lastCount = now;
            int count = 0;
            for (uint32_t i = 1; i < 64; i++) {
                uintptr_t ctrl = ResolveEntity(entitySystem, i);
                if (!ctrl) continue;
                uint32_t ph = *(uint32_t*)(ctrl + g_off.hPawn);
                uintptr_t pawn = ResolvePawn(entitySystem, ph);
                if (!pawn) continue;
                int32_t hp = *(int32_t*)(pawn + g_off.health);
                if (hp > 0 && hp <= 100) count++;
            }
            g_chams.applied = count;
        }
        g_matchPawns = g_chams.applied; // Hunter-Gate (Match läuft?)
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        static DWORD s_lastEx = 0;
        if (now - s_lastEx > 5000) {
            s_lastEx = now;
            Log("Chams: Exception (Offsets stale? Code=0x%08X)",
                (unsigned)GetExceptionCode());
        }
        g_chams.applied = 0;
        g_hookLocalTeam = -1;
    }
}
