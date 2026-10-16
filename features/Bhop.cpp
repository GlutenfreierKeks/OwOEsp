#include "pch.h"
#include "Bhop.h"
#include "Offsets.h"
#include "Reader.h"
#include "../core/Globals.h"
#include "../core/Input.h"
#include "../core/SDK.h"
#include <cstdint>

// ─────────────────────────────────────────────────────────────────────────────
// features/Bhop.cpp — AUTO-BHOP über ForceJump-Button (UC-Standard)
//
// Alter Stand (tot): Jump-Events ins CInputSystem gespammt — dwCSGOInput-Offset
// driftet, ButtonCodes unklar, Ergebnis: kein Sprung mehr.
// Neu (UC cs2-external Standard, z.B. Ikersct/FrolixCZE/TKazer):
//   client.dll + buttons::jump  <-  65537 (+jump) am Boden, 256 (-jump) in Luft
// Gating über m_fFlags FL_ONGROUND des LocalPawns. Nur schreiben bei
// Zustandswechsel (kein Write-Spam), Cleanup beim Loslassen/Deaktivieren.
// ─────────────────────────────────────────────────────────────────────────────
void BhopThread() {
    bool jumpHeld = false; // wurde 65537 geschrieben?

    while (!g_stopThread.load(std::memory_order_relaxed)) {
        bool want = g_mem.IsValid() && g_client && g_cfg.bhopEnable && !g_menuOpen &&
                    (GetAsyncKeyState(VK_SPACE) & 0x8000);

        if (want && offsets::dwJumpButton) {
            uintptr_t lp = GetLocalPawn(); // Controller-Handle (kein dw nötig)
            if (lp) {
                uint8_t life = g_mem.Read<uint8_t>(lp + schemas::m_lifeState);
                if (life == 0) {
                    uint32_t flags = g_mem.Read<uint32_t>(lp + schemas::m_fFlags);
                    bool onGround = (flags & 1) != 0; // FL_ONGROUND
                    if (onGround) {
                        // Dauerfeuer am Boden: +jump setzen, Spiel springt im
                        // nächsten Tick und setzt das Bit selbst zurück
                        g_mem.Write<int32_t>(g_client + offsets::dwJumpButton, 65537);
                        jumpHeld = true;
                    } else if (jumpHeld) {
                        g_mem.Write<int32_t>(g_client + offsets::dwJumpButton, 256);
                        jumpHeld = false;
                    }
                    Sleep(1);
                    continue;
                }
            }
        }

        // Nicht aktiv / tot / kein Pawn: Button genau einmal sauber lösen
        if (jumpHeld && g_mem.IsValid() && g_client && offsets::dwJumpButton) {
            g_mem.Write<int32_t>(g_client + offsets::dwJumpButton, 256);
            jumpHeld = false;
        }
        Sleep(2);
    }

    // Thread-Ende: sicherheitshalber lösen
    if (jumpHeld && g_mem.IsValid() && g_client && offsets::dwJumpButton)
        g_mem.Write<int32_t>(g_client + offsets::dwJumpButton, 256);
}
