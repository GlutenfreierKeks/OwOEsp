#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// features/Reader.h — Hintergrund-Memory-Thread (~120 Hz)
// Liest alle Spiel-Daten und publiziert sie unter Mutex als Snapshot.
// ─────────────────────────────────────────────────────────────────────────────
#include "Cache.h"

// Thread-Funktion (wird in app/App.cpp gestartet/gestoppt)
void GameReaderThread();

// LocalPawn robust ohne dwLocalPlayerPawn: Controller -> Pawn-Handle ->
// Entity-Liste (update-sicher, nur stabile Schema-Offsets nötig).
uintptr_t GetLocalPawn();