#pragma once
// inject/ModelChams.h — XQZ-Modell-Chams via DrawObject-Hook (scenesystem).
//
// Weg (kein Materialsystem nötig): DrawObject rendert jedes Scene-Objekt.
// Der Hook erkennt Spieler-Modelle am Materialnamen ("characters/models",
// Team an "ctm_"/"tm_"), malt sie 2x: 1x mit deaktiviertem Depth-Test
// (durch Wände) + 1x normal, Farbe via m_color. Animierte Modi (Rainbow,
// Pulse) ändern die Farbe pro Frame (Lava-Lampen-Effekt).
// Alles SEH-geschützt, Standard: AUS (Lazy-Init beim ersten Aktivieren).

#include <windows.h>
#include <cstdint>

struct IDXGISwapChain; // fwd (dxgi.h nicht nötig für Pointer-Param)

struct ModelChamsStatus {
    bool hooked = false;
    uintptr_t hookAddr = 0;
    long drawsSeen = 0;     // DrawObject-Calls gesamt
    long playersColored = 0; // Calls mit Spieler-Effekt
    int uniqueNames = 0;    // geloggte Modellnamen (Diagnose)
    bool diHooked = false;  // DrawIndexed-Hook (Stride-Analyse + XQZ)
    long diDraws = 0;       // DrawIndexed-Calls gesamt
    long xqzDraws = 0;      // XQZ-Pässe (Spieler-Stride, depth-off)
    long diReverts = 0;     // Slot-Reverts (Dritte entfernen Hook)
    long drawReverts = 0;   // DrawObject-Reverts
    int defCtx = 0;         // Deferred-Contexts gesehen
    long defDraws = 0;      // Draws auf Deferred-Contexts
};

extern ModelChamsStatus g_mchams;

// Haken setzen (einmalig, idempotent). false = Sig nicht gefunden etc.
bool InitModelChams();

// Haken entfernen (sauberer Eject).
void ShutdownModelChams();

// DrawIndexed-Recon (Stride-Analyse, read-only) über Game-SwapChain sichern.
void EnsureDiHook(IDXGISwapChain* sc);

// Patches überwachen (Overlay/Integrity) + ggf. reparieren.
void VerifyModelHooks();

// Für den Hook: Team des Localplayers (aus Chams.cpp-Poll, -1 = unbekannt).
extern volatile int g_hookLocalTeam;
