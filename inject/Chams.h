#pragma once
// inject/Chams.h — Chams-Modul (intern, DrawObject-Hook, XQZ-Modellfarben).
//
// v3: Engine-Glow ist RAUS. Modi malen Spieler-Modelle direkt ein:
// Flat (deckend grün/rot...), Glass, Rainbow (animiert, Lava-Lampe),
// Pulse, Health (HP-Farbe via m_clrRender), Ghost — alle mit XQZ-Pass
// (durch Wände) aus ModelChams.cpp. Chams.cpp liefert Team-Info + Diagnose.

enum class ChamsMaterialMode : int {
    Flat = 0,    // Deckende Modellfarbe
    Glass = 1,   // Transparent (Alpha-Slider)
    Rainbow = 2, // Animierter Hue-Cycle
    Pulse = 3,   // Pulsierende Alpha
    Health = 4,  // HP-Farbe (Feld) + XQZ in Teamfarbe
    Ghost = 5,   // Fast unsichtbar + sichtbar
};

struct ChamsState {
    bool glowEnable = false; // Master: Chams an/aus (default AUS)
    bool glowEnemies = true;
    bool glowTeammates = false;
    float enemyColor[3] = { 0.0f, 1.0f, 0.0f };
    float teamColor[3] = { 1.0f, 0.0f, 0.0f };
    int glassAlpha = 115; // Alpha für Glass/Ghost (0..255)
    int material = 0; // ChamsMaterialMode (Feld-Tönung, sichtbar)
    bool xqzEnable = false; // XQZ-Pass (trifft derzeit nur Deco/Shafts!)
    int xqzStride = 52;    // Spieler-Stride (Slider zum Tunen)
    int xqzIdxMin = 0;     // Index-Untergrenze (Shafts/Deco rausfiltern)
    int xqzIdxMax = 20000; // Index-Obergrenze
    int applied = 0;  // Diagnose: lebende Pawns im Poll
    bool offsetsOk = false; // EntitySystem + LocalPlayer gefunden
};

extern ChamsState g_chams;

// Pro Present: Team pollen + Hook bei Bedarf installieren (lazy).
void RunChams();
