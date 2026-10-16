#pragma once
// inject/OffScan.h — interne Offset-Selbstheilung (Sig-Scan in client.dll).
//
// Portiert die Muster aus OwOEsp/core/SigScan.cpp (a2x-Analyse):
// Auflösung disp32 an +3, Ziel = match + 7 + disp. Einmalig beim Start,
// danach gelten die gefundenen RVAs (Fallback: GameOffsets.h-Konstanten).
#include <cstdint>

struct InjOffsets {
    uintptr_t entityList = 0;
    uintptr_t localPlayer = 0;
    uintptr_t viewMatrix = 0;
    // Schema (frisch via OffsetFetch, Fallback GameOffsets.h)
    uintptr_t hPawn = 0;
    uintptr_t health = 0;
    uintptr_t team = 0;
    uintptr_t origin = 0;
    uintptr_t pname = 0;
    uintptr_t eye = 0;
    uintptr_t voff = 0;
    uintptr_t camsvc = 0;
    uintptr_t fovm = 0;
    uintptr_t clr = 0;
    uintptr_t rmode = 0;
    uintptr_t flags = 0;
};

extern InjOffsets g_off;

// Geteilte Entity-Helfer (einmalig hier, von allen Modulen genutzt).
uintptr_t InjClientBase();
uintptr_t InjResolveEntity(uintptr_t es, uint32_t idx);
uintptr_t InjResolvePawn(uintptr_t es, uint32_t handle);
// Local-Pawn + Team. false = nicht im Match.
bool InjLocalPawn(uintptr_t& lpawn, int& team);

extern InjOffsets g_off;

// Scannt client.dll (einmalig sinnvoll). Loggt Treffer/MISS. true = mind.
// ein Wert neu gefunden.
bool ScanGameOffsets();

// ViewMatrix-Jäger (State-Machine, non-blocking, aus MainThread ticken):
// Startet, wenn ESP-Matrix dauerhaft FAILt UND Match läuft (Pawns da).
// Kandidaten: 64B-Blöcke mit m[15]==1, finit; Bewegungstest wählt.
// Setzt g_off.viewMatrix + loggt. Danach Ruhe (einmalig).
void VmHunterTick();

// geteilter Zustand (Chams-Poll <-> ESP <-> Hunter)
extern volatile int g_matchPawns;  // lebende Pawns (Chams-Poll)
extern volatile DWORD g_espFailTick; // letzter Matrix-FAIL (0 = ok)
