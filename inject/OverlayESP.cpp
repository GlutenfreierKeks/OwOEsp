// inject/OverlayESP.cpp — Boxen/HP/Namen (read-only, W2S wie externer ESP).
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include "../imgui/imgui.h"
#include "OverlayESP.h"
#include "GameOffsets.h"
#include "OffScan.h" // g_off (sig-geheilt)
#include "GameProj.h"
#include "Chams.h" // Team-Filter + Farben wiederverwenden

extern void Log(const char* fmt, ...);

namespace {
using injproj::Vec3;
using injproj::Mat44;
using injproj::W2S;

uintptr_t g_client = 0;

uintptr_t ResolveEntity(uintptr_t es, uint32_t idx) {
    uintptr_t chunk = *(uintptr_t*)(es + 0x10 + 8 * (idx >> 9));
    if (!chunk) return 0;
    return *(uintptr_t*)(chunk + injoff::kListStride * (idx & 0x1FF));
}
uintptr_t ResolvePawn(uintptr_t es, uint32_t h) {
    uint32_t idx = h & 0x7FFF;
    if (idx == 0 || idx == 0x7FFF) return 0;
    return ResolveEntity(es, idx);
}

// ViewProj aus Pawn lesen (Augen + Winkel + FOV) — dünn um GameProj.
bool PawnViewProj(uintptr_t lpawn, int scrW, int scrH, Mat44& out, uint32_t& fovOut) {
    Vec3 origin = *(Vec3*)(lpawn + g_off.origin);
    Vec3 voff = *(Vec3*)(lpawn + g_off.voff);
    float* ang = (float*)(lpawn + g_off.eye); // pitch, yaw, roll
    uint32_t fovH = 90;
    uintptr_t cam = *(uintptr_t*)(lpawn + g_off.camsvc);
    if (cam) {
        uint32_t f = *(uint32_t*)(cam + g_off.fovm);
        if (f >= 30 && f <= 150) fovH = f;
    }
    fovOut = fovH;
    Vec3 eye = { origin.x + voff.x, origin.y + voff.y, origin.z + voff.z };
    return injproj::BuildViewProj(eye, ang[0], ang[1], fovH, scrW, scrH, out);
}
} // namespace

OverlayEspState g_esp;

void DrawOverlayESP(int scrW, int scrH) {
    g_esp.drawn = 0;
    // Eck-Debug IMMER zuerst (mit letzten Werten): kein Text = ESP läuft
    // gar nicht / wird nicht aufgerufen. Text braucht nur die DrawList.
    static int s_pawns = -1, s_proj = -1, s_box = -1, s_W = -1, s_H = -1;
    static float s_vm33 = -1.f;
    if (g_esp.enable && scrW > 0 && scrH > 0) {
        char st0[128];
        snprintf(st0, sizeof(st0), "ESP pawns=%d proj=%d box=%d vm33=%.2f %dx%d",
                 s_pawns, s_proj, s_box, s_vm33, s_W, s_H);
        ImGui::GetBackgroundDrawList()->AddText(
            ImVec2(12.f, 12.f), IM_COL32(140, 200, 255, 220), st0);
    }
    if (!g_esp.enable || scrW <= 0 || scrH <= 0) return;
    if (!g_client) {
        g_client = (uintptr_t)GetModuleHandleW(L"client.dll");
        if (!g_client) return;
    }
    int dbgPawns = 0, dbgProj = 0;
    float dbgVm33 = 0.f;
    __try {
        uintptr_t es = *(uintptr_t*)(g_client + g_off.entityList);
        uintptr_t lc = *(uintptr_t*)(g_client + g_off.localPlayer);
        if (!es || !lc) return;

        uint32_t lh = *(uint32_t*)(lc + g_off.hPawn);
        uintptr_t lpawn = ResolvePawn(es, lh);
        if (!lpawn) return;
        int localTeam = *(uint8_t*)(lpawn + g_off.team);

        // Pfad 1: Spiel-Matrix (frisch via OffsetFetch) — exakt wie extern.
        // Ins eigene Spalten-Format transponieren (ein Pfad für alles).
        Mat44 vm{};
        bool useGame = false;
        {
            Mat44 raw{};
            memcpy(&raw, (void*)(g_client + g_off.viewMatrix), sizeof(raw));
            dbgVm33 = raw.m[3][3];
            if (raw.m[3][3] == 1.f) {
                bool fin = true;
                for (int a = 0; a < 4 && fin; a++)
                    for (int b = 0; b < 4; b++)
                        if (!isfinite(raw.m[a][b])) { fin = false; break; }
                if (fin) {
                    for (int a = 0; a < 4; a++)
                        for (int b = 0; b < 4; b++) vm.m[a][b] = raw.m[b][a];
                    useGame = true;
                    g_espFailTick = 0;
                }
            }
        }
        // Pfad 2: selbst gebaut (Fallback, kein dw nötig).
        uint32_t fovH = 0;
        if (!useGame) {
            if (!PawnViewProj(lpawn, scrW, scrH, vm, fovH)) return;
        }

        // Einmalige Tiefen-Diagnose (alle 10s): Auge/Winkel/FOV + Clip des
        // ersten Gegners — damit ist jede Konventionsfrage beantwortbar.
        {
            static DWORD s_lastDeep = 0;
            DWORD nowDeep = GetTickCount();
            if (nowDeep - s_lastDeep > 10000) {
                s_lastDeep = nowDeep;
                Vec3 eye = *(Vec3*)(lpawn + g_off.origin);
                Vec3 vo = *(Vec3*)(lpawn + g_off.voff);
                float* ang = (float*)(lpawn + g_off.eye);
                // erster lebender Fremd-Pawn + sein Clip
                Vec3 p0 = { 0.f, 0.f, 0.f };
                float cx = 0.f, cy = 0.f, cw = 0.f;
                bool haveP0 = false;
                for (uint32_t i = 1; i < 64 && !haveP0; i++) {
                    uintptr_t c2 = ResolveEntity(es, i);
                    if (!c2) continue;
                    uint32_t ph2 = *(uint32_t*)(c2 + g_off.hPawn);
                    uintptr_t pw2 = ResolvePawn(es, ph2);
                    if (!pw2 || pw2 == lpawn) continue;
                    int32_t hp2 = *(int32_t*)(pw2 + g_off.health);
                    if (hp2 <= 0 || hp2 > 100) continue;
                    p0 = *(Vec3*)(pw2 + g_off.origin);
                    cx = vm.m[0][0] * p0.x + vm.m[1][0] * p0.y + vm.m[2][0] * p0.z + vm.m[3][0];
                    cy = vm.m[0][1] * p0.x + vm.m[1][1] * p0.y + vm.m[2][1] * p0.z + vm.m[3][1];
                    cw = vm.m[0][2] * p0.x + vm.m[1][2] * p0.y + vm.m[2][2] * p0.z + vm.m[3][2];
                    haveP0 = true;
                }
                Log("ESPdeep: eye=(%.1f,%.1f,%.1f) voff=(%.1f,%.1f,%.1f) p=%.1f y=%.1f fovH=%u "
                    "p0=(%.1f,%.1f,%.1f) clip=(%.1f,%.1f,%.1f) %s",
                    eye.x, eye.y, eye.z, vo.x, vo.y, vo.z, ang[0], ang[1], fovH,
                    p0.x, p0.y, p0.z, cx, cy, cw, haveP0 ? "OK" : "keinP0");
            }
        }

        ImDrawList* dl = ImGui::GetBackgroundDrawList();
        for (uint32_t i = 1; i < 64; i++) {
            uintptr_t ctrl = ResolveEntity(es, i);
            if (!ctrl) continue;
            uint32_t ph = *(uint32_t*)(ctrl + g_off.hPawn);
            uintptr_t pawn = ResolvePawn(es, ph);
            if (!pawn || pawn == lpawn) continue;
            int32_t hp = *(int32_t*)(pawn + g_off.health);
            if (hp <= 0 || hp > 100) continue;
            dbgPawns++;
            int team = *(uint8_t*)(pawn + g_off.team);
            bool isMate = (team == localTeam);
            if (isMate && !g_chams.glowTeammates) continue;
            if (!isMate && !g_chams.glowEnemies) continue;

            Vec3 feet = *(Vec3*)(pawn + g_off.origin);
            Vec3 head = { feet.x, feet.y, feet.z + 72.f };
            float fx, fy, hx, hy;
            if (!W2S(feet, fx, fy, vm, scrW, scrH)) continue;
            if (!W2S(head, hx, hy, vm, scrW, scrH)) continue;
            dbgProj++;
            float h = fy - hy;
            if (h <= 2.f || h > (float)scrH) continue;
            float w = h * 0.4f;
            float x0 = hx - w * 0.5f, x1 = hx + w * 0.5f;

            const float* c = isMate ? g_chams.teamColor : g_chams.enemyColor;
            ImU32 col = IM_COL32((int)(c[0] * 255), (int)(c[1] * 255), (int)(c[2] * 255), 255);
            dl->AddRect(ImVec2(x0, hy), ImVec2(x1, fy), col, 0.f, 0, 1.5f);
            dl->AddRect(ImVec2(x0 - 1, hy - 1), ImVec2(x1 + 1, fy + 1),
                        IM_COL32(0, 0, 0, 200), 0.f, 0, 1.f);
            // HP-Balken links
            float frac = (float)hp / 100.f;
            ImU32 hpCol = IM_COL32((int)((1.f - frac) * 255), (int)(frac * 255), 40, 255);
            dl->AddRectFilled(ImVec2(x0 - 5, hy), ImVec2(x0 - 2, fy), IM_COL32(0, 0, 0, 160));
            dl->AddRectFilled(ImVec2(x0 - 5, fy - h * frac), ImVec2(x0 - 2, fy), hpCol);
            // Name über der Box
            char name[40] = {};
            memcpy(name, (void*)(ctrl + g_off.pname), sizeof(name) - 1);
            name[sizeof(name) - 1] = 0;
            if (name[0]) {
                ImVec2 tsz = ImGui::CalcTextSize(name);
                dl->AddText(ImVec2(hx - tsz.x * 0.5f + 1, hy - tsz.y - 2 + 1),
                            IM_COL32(0, 0, 0, 200), name);
                dl->AddText(ImVec2(hx - tsz.x * 0.5f, hy - tsz.y - 2),
                            IM_COL32(230, 235, 245, 255), name);
            }
            g_esp.drawn++;
        }
        // Eck-Debugzeile (liest man im Spiel + im Log alle 10s).
        s_pawns = dbgPawns;
        s_proj = dbgProj;
        s_box = g_esp.drawn;
        s_vm33 = dbgVm33;
        s_W = scrW;
        s_H = scrH;
        ImDrawList* dbg = ImGui::GetBackgroundDrawList();
        char st[128];
        snprintf(st, sizeof(st), "ESP pawns=%d proj=%d box=%d vm33=%.2f %dx%d",
                 dbgPawns, dbgProj, g_esp.drawn, dbgVm33, scrW, scrH);
        dbg->AddText(ImVec2(12.f, 12.f), IM_COL32(140, 200, 255, 220), st);
        static DWORD s_lastDbg = 0;
        DWORD nowDbg = GetTickCount();
        if (nowDbg - s_lastDbg > 10000) {
            s_lastDbg = nowDbg;
            Log("ESP: %s fovH=%u", st, fovH);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_esp.drawn = 0;
    }
}
