#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// core/Input.h — simulierte Eingaben (SendInput-Kapselung)
// WICHTIG für CS2: Tastenzustände werden pro Tick (128 Hz = 7.8 ms) abgetastet.
// Ein DOWN+UP im selben Tick ist UNSICHTBAR → Flanken muessen > 8 ms gehalten
// werden, damit das Spiel sie registriert.
// ─────────────────────────────────────────────────────────────────────────────
#include "platform/platform.h"

// ── CS2 ButtonCode_t (Source-Engine — CS2 identisch) ─────────────────────────
// Die IN_*-Codes sind "virtuelle Buttons" ab 264; die Game-Eingabe wird pro
// Tick (128 Hz) aus dem CInputSystem (dwCSGOInput) abgetastet.
constexpr int kInAttack    = 264;
constexpr int kInJump      = 265;
constexpr int kInDuck      = 266;
constexpr int kInForward   = 267;
constexpr int kInBack      = 268;
constexpr int kInUse       = 269;
constexpr int kInLeft      = 271;
constexpr int kInRight     = 272;
constexpr int kInMoveLeft  = 273;
constexpr int kInMoveRight = 274;
constexpr int kInAttack2   = 275;
constexpr int kInReload    = 277;

// ── Direktes Schreiben ins CS2-Inputsystem (dwCSGOInput) ─────────────────────
// 0x40 = Down-Zustand, 0x48 = Pressed, 0x50 = Released (Byte-Arrays je Button).
// Tick-perfekt: der Zustand liegt am Lande-/Schuss-Tick garantiert an.
// Rueckgabe: true = erfolgreich geschrieben.
bool InputSystemPress(int buttonCode);
bool InputSystemRelease(int buttonCode);

// Taste einmal betätigen: DOWN, warten, UP (keyHeldMs = Dauer des DOWN-Zustands)
void SimulateKeyTap(WORD vk, DWORD keyHeldMs = 9);

// Taste drücken (DOWN) bzw. loslassen (UP) — fuer Zustandsmaschinen
void SimulateKeyDown(WORD vk);
void SimulateKeyUp(WORD vk);

// Mausklick (DOWN+UP hintereinander) — Maus-Buttons werden ereignisbasiert
// verarbeitet, ein sofortiges Paar reicht.
void SimulateMouseClick();

// Maus um dx/dy Pixel bewegen (absolute dx/dy aus der DPI-Maus = 0..65535)
void SimulateMouseMove(int dx, int dy);

// Wie SimulateMouseMove, zählt aber zusätzlich in einem gemeinsamen
// External-Counter mit (RCS-/Trigger-Bewegungen). Der Aimbot zieht den
// Counter beim User-Override-Check ab — sonst hält er RCS für User-Input
// und pausiert mitten im Spray (exakt das Zusammenspiel-Problem).
// NUR Render-Thread! Aimbot-eigene Moves bleiben ungezählt (eigene Buchhaltung).
void SimulateMouseMoveExternal(int dx, int dy);

// Holt+nullt den External-Counter (Aimbot, pro Frame einmal).
// Rueckgabe immer true, dx/dy = seit letztem Call akkumuliert (oft 0,0).
bool ConsumeExternalMouseMove(int* dx, int* dy);

// Weicher Click: DOWN, keyHeldMs warten, UP — falls das Spiel das Down-Fenster
// braucht (z.B. stabile Schuss-Erkennung).
void SimulateMouseClickHeld(DWORD keyHeldMs = 8);

// Einmaliges Ereignis pro Druck (Flankenerkennung fuer Tasten)
bool WasKeyPressed(int vk);
