// inject/AimAssist.cpp — sanfter Screen-Aim auf Köpfe.
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include "../imgui/imgui.h"
#include "AimAssist.h"
#include "GameProj.h"
#include "GameOffsets.h"
#include "OffScan.h" // g_off + Shared-Resolver
#include "TestUI.h"  // g_test.showMenu (nicht ins Spiel zielen im Menü)

extern void Log(const char* fmt, ...);

AimState g_aim;

void RunAimAssist(int scrW, int scrH) {
    g_aim.targets = 0;
    g_aim.locking = false;
    if (!g_aim.enable || g_test.showMenu) return;
    if (scrW <= 0 || scrH <= 0) return;
    if (!(GetAsyncKeyState(g_aim.key) & 0x8000)) return;

    __try {
        uintptr_t client = InjClientBase();
        if (!client) return;
        uintptr_t es = *(uintptr_t*)(client + g_off.entityList);
        if (!es) return;
        uintptr_t lpawn = 0;
        int localTeam = -1;
        if (!InjLocalPawn(lpawn, localTeam)) return;

        // ViewProj aus eigenem Pawn (GameProj, kein dw nötig).
        injproj::Vec3 origin = *(injproj::Vec3*)(lpawn + g_off.origin);
        injproj::Vec3 voff = *(injproj::Vec3*)(lpawn + g_off.voff);
        float* ang = (float*)(lpawn + g_off.eye);
        uint32_t fovH = 90;
        uintptr_t cam = *(uintptr_t*)(lpawn + g_off.camsvc);
        if (cam) {
            uint32_t f = *(uint32_t*)(cam + g_off.fovm);
            if (f >= 30 && f <= 150) fovH = f;
        }
        injproj::Vec3 eye = { origin.x + voff.x, origin.y + voff.y, origin.z + voff.z };
        injproj::Mat44 vm{};
        if (!injproj::BuildViewProj(eye, ang[0], ang[1], fovH, scrW, scrH, vm)) return;

        float cx = (float)scrW * 0.5f, cy = (float)scrH * 0.5f;
        float bestD2 = g_aim.fovPx * g_aim.fovPx;
        float bestTx = 0.f, bestTy = 0.f;
        injproj::Vec3 bestHead = { 0.f, 0.f, 0.f };
        bool have = false;
        int n = 0;
        for (uint32_t i = 1; i < 64; i++) {
            uintptr_t ctrl = InjResolveEntity(es, i);
            if (!ctrl) continue;
            uint32_t ph = *(uint32_t*)(ctrl + g_off.hPawn);
            uintptr_t pawn = InjResolvePawn(es, ph);
            if (!pawn || pawn == lpawn) continue;
            int32_t hp = *(int32_t*)(pawn + g_off.health);
            if (hp <= 0 || hp > 100) continue;
            int team = *(uint8_t*)(pawn + g_off.team);
            bool isMate = (team == localTeam);
            if (isMate && !g_aim.mates) continue;
            if (!isMate && team != (localTeam == 2 ? 3 : 2)) {
                // Unbekanntes Team (Spezl?) — nur mit mates-Flag.
                if (!g_aim.mates) continue;
            }
            injproj::Vec3 o = *(injproj::Vec3*)(pawn + g_off.origin);
            injproj::Vec3 head = { o.x, o.y, o.z + 70.f };
            float hx, hy;
            if (!injproj::W2S(head, hx, hy, vm, scrW, scrH)) continue;
            float dx = hx - cx, dy = hy - cy;
            float d2 = dx * dx + dy * dy;
            if (d2 > bestD2) continue;
            bestD2 = d2;
            bestTx = hx;
            bestTy = hy;
            bestHead = head;
            have = true;
            n++;
        }
        g_aim.targets = n;
        if (!have) return;
        if (g_aim.mode == 1) {
            // ── Engine-Modus: Blickwinkel direkt schreiben (echt intern) ──
            // Kopf-Weltpos -> gewünschte Pitch/Yaw (Source: pitch>0 = runter).
            float dx = bestHead.x - eye.x;
            float dy = bestHead.y - eye.y;
            float dz = bestHead.z - eye.z;
            float len = sqrtf(dx * dx + dy * dy + dz * dz);
            if (len < 1.f || !isfinite(len)) return;
            float desYaw = atan2f(dy, dx) * 57.29578f;
            float desPitch = -asinf(dz / len) * 57.29578f;
            float* ang = (float*)(lpawn + g_off.eye);
            float curP = ang[0], curY = ang[1];
            if (!isfinite(curP) || !isfinite(curY)) return;
            float dP = desPitch - curP;
            float dY = desYaw - curY;
            while (dY > 180.f) dY -= 360.f;
            while (dY < -180.f) dY += 360.f;
            float step = g_aim.smooth < 1.f ? 1.f : g_aim.smooth;
            float nP = curP + dP / step;
            float nY = curY + dY / step;
            while (nY > 180.f) nY -= 360.f;
            while (nY < -180.f) nY += 360.f;
            if (nP > 89.f) nP = 89.f;
            if (nP < -89.f) nP = -89.f;
            if (dP * dP + dY * dY < 0.02f) return; // Deadzone (Grad)
            ang[0] = nP;
            ang[1] = nY;
            g_aim.locking = true;
            return;
        }
        float mx = (bestTx - cx) / g_aim.smooth;
        float my = (bestTy - cy) / g_aim.smooth;
        // Deadzone: Mikrojitter vermeiden.
        if (mx * mx + my * my < 2.25f) return;
        // Clamp: keine wilden Flicks.
        if (mx > 60.f) mx = 60.f;
        if (mx < -60.f) mx = -60.f;
        if (my > 60.f) my = 60.f;
        if (my < -60.f) my = -60.f;
        mouse_event(MOUSEEVENTF_MOVE, (DWORD)(int)mx, (DWORD)(int)my, 0, 0);
        g_aim.locking = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
