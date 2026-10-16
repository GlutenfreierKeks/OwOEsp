#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// features/Aimbot.h — sanfter Aim-Assist (Screen-Distanz -> Mausbewegung)
// Player-, Chicken- und Ball-Varianten teilen sich dieselbe
// Tiefpass-/Deadzone-/UserOverride-Mechanik (siehe Aimbot.cpp).
// ─────────────────────────────────────────────────────────────────────────────
#include "Cache.h"

// Fuehrt die Aim-Assist-Logik aus. Rueckgabe: true = Ziel wird verfolgt.
bool RunAimbot(const GameCache& cache, const AimCache& aimCache, const UtilCache& utilCache, int scrW, int scrH);

// Chicken-/Ball-Aimbot: naechstes Entity im FOV, sonst identisch zum Player-Aimbot.
bool RunChickenAimbot(const EntAimCache& chickenCache, int scrW, int scrH);
bool RunBallAimbot(const EntAimCache& ballCache, int scrW, int scrH);

// Marker Aim: zielt auf Coordinate-Saver-Punkte (statische Ziele: Glocken etc.).
bool RunMarkerAimbot(const Matrix4x4& vm, bool valid, int scrW, int scrH);

// Aim Bot (Rage): Head-Snap ohne Vis-Check (durch Wände) + optional Autofire.
// Rueckgabe: true = Rage hat ein Ziel bearbeitet.
bool RunRagebot(const AimCache& aimCache, int scrW, int scrH);