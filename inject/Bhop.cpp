// inject/Bhop.cpp — Landekanten-Sprung + Air-Strafe per SendInput.
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include "Bhop.h"
#include "GameOffsets.h"
#include "OffScan.h" // g_off + Shared-Resolver
#include "TestUI.h"  // Menüschutz

extern void Log(const char* fmt, ...);

BhopState g_bhop;

namespace {
bool s_jumped = false;
int s_strafeHold = 0; // 0=keine, 'A', 'D'
DWORD s_strafeNext = 0;
bool s_strafePhase = false;

void KeyTap(WORD vk) {
    INPUT in[2]{};
    in[0].type = INPUT_KEYBOARD;
    in[0].ki.wVk = vk;
    in[1].type = INPUT_KEYBOARD;
    in[1].ki.wVk = vk;
    in[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, in, sizeof(INPUT));
}

void KeyDown(WORD vk) {
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    SendInput(1, &in, sizeof(INPUT));
}

void KeyUp(WORD vk) {
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    in.ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(1, &in, sizeof(INPUT));
}

void ReleaseStrafe() {
    if (s_strafeHold == 'A') KeyUp('A');
    else if (s_strafeHold == 'D') KeyUp('D');
    s_strafeHold = 0;
}
} // namespace

void RunBhop() {
    g_bhop.jumping = false;
    if (!g_bhop.enable || g_test.showMenu) {
        if (s_strafeHold) ReleaseStrafe();
        s_jumped = false;
        g_bhop.air = false;
        return;
    }
    bool spaceHeld = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
    if (!spaceHeld) {
        if (s_strafeHold) ReleaseStrafe();
        s_jumped = false;
        g_bhop.air = false;
        return;
    }
    __try {
        uintptr_t lpawn = 0;
        int team = -1;
        if (!InjLocalPawn(lpawn, team)) {
            g_bhop.air = false;
            return;
        }
        uint32_t flags = *(uint32_t*)(lpawn + g_off.flags);
        bool onGround = (flags & 1u) != 0;
        g_bhop.air = !onGround;
        if (onGround) {
            if (s_strafeHold) ReleaseStrafe();
            if (!s_jumped) {
                KeyTap(VK_SPACE);
                s_jumped = true;
                g_bhop.jumping = true;
            }
        } else {
            s_jumped = false;
            // Air-Strafe: nur wenn Spieler weder A noch D hält.
            bool aHeld = (GetAsyncKeyState('A') & 0x8000) != 0;
            bool dHeld = (GetAsyncKeyState('D') & 0x8000) != 0;
            if (!g_bhop.strafe || aHeld || dHeld) {
                if (s_strafeHold) ReleaseStrafe();
                return;
            }
            DWORD now = GetTickCount();
            if (now >= s_strafeNext) {
                s_strafeNext = now + 110;
                s_strafePhase = !s_strafePhase;
                if (s_strafeHold) ReleaseStrafe();
                s_strafeHold = s_strafePhase ? 'A' : 'D';
                KeyDown((WORD)s_strafeHold);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

void BhopCleanup() {
    if (s_strafeHold) ReleaseStrafe();
    s_jumped = false;
    g_bhop.air = false;
    g_bhop.jumping = false;
}
