#pragma once
// inject/OverlayESP.h — Through-Wall-Boxen über unseren Present-Hook.
//
// Braucht KEINE Render-Hooks, KEINE Feld-Writes (nur lesend): ViewMatrix +
// Positionen → Boxen/HP/Namen auf die ImGui-Hintergrundliste. Funktioniert
// garantiert, solange Present-Hook + Entity-Offsets stimmen.

struct OverlayEspState {
    bool enable = true;
    int drawn = 0; // Boxen im letzten Frame
    int eyeSrc = 0; // Winkel-Quelle: 0=Auto, 1=0x35F0, 2=0x3600
};

extern OverlayEspState g_esp;

// Nach ImGui::NewFrame() aufrufen (zeichnet auf BackgroundDrawList).
void DrawOverlayESP(int scrW, int scrH);
