#pragma once
// inject/OffsetFetch.h — frische Offsets per a2x-Dump (wie externer Updater).
//
// Lädt offsets.json + client_dll.json von GitHub, parst die ~12 Keys, die
// wir brauchen, cacht sie in %TEMP% (24h). Kein Update-Jagen mehr:
// Dump frisch = alles frisch (Matrix, Namen, Winkel, Felder).
#include <cstdint>

struct FreshOffsets {
    bool ok = false; // mind. Entity+Local+Matrix da?
    uintptr_t entityList = 0;
    uintptr_t localPlayer = 0;
    uintptr_t viewMatrix = 0;
    uintptr_t hPlayerPawn = 0;
    uintptr_t iHealth = 0;
    uintptr_t iTeamNum = 0;
    uintptr_t vOldOrigin = 0;
    uintptr_t playerName = 0;
    uintptr_t eyeAngles = 0;
    uintptr_t viewOffset = 0;
    uintptr_t cameraSvc = 0;
    uintptr_t fov = 0;
    uintptr_t clr = 0;
    uintptr_t rmode = 0;
    uintptr_t flags = 0;
};

extern FreshOffsets g_fresh;

// Einmalig beim Start: Cache (24h) oder Download. Loggt Quelle.
void FetchOffsetsOnce();

// Wendet g_fresh auf g_off an (nur nonzero Werte). true = Matrix dabei.
bool ApplyFreshOffsets();
