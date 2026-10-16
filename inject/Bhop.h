#pragma once
// inject/Bhop.h — Auto-Bunnyhop per SendInput (kein ForceJump-Offset nötig).
//
// Springt beim Landen (m_fFlags on-ground), solange SPACE physisch gehalten.
// Optional Air-Strafe-Helfer (wechselt A/D in der Luft, nur wenn der Spieler
// selbst weder A noch D hält). Keine Game-Writes — reiner Input.
struct BhopState {
    bool enable = false;
    bool strafe = true;
    bool jumping = false; // Diagnose: gerade gesprungen?
    bool air = false;     // Diagnose: in der Luft?
};

extern BhopState g_bhop;

// Pro Present aufrufen (Edge-Erkennung bei ~100Hz reicht).
void RunBhop();

// Gehaltene Tasten loslassen (Unload).
void BhopCleanup();
