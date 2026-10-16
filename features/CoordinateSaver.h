#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// features/CoordinateSaver.h — speichert per Tastendruck die Weltkoordinaten
// des anvisierten Objekts (Spieler/Chicken/Ball/Item am Fadenkreuz).
// Liste lebt im Render-Thread, wird zusaetzlich in
// PixelMommySense_coords.txt neben der Exe gespeichert.
// ─────────────────────────────────────────────────────────────────────────────
#include "Cache.h"

struct SavedCoord {
    Vector3 pos{};
    char    label[48]{};
};

// Einmal pro Frame aufrufen (Render-Thread). Speichert bei Tastendruck.
void RunCoordinateSaver(const GameCache& cache, const EntAimCache& chicken,
                        const EntAimCache& ball, const GroundCache& ground,
                        int scrW, int scrH);

// Gespeicherte Punkte als Marker zeichnen (nur Render-Thread).
void DrawSavedCoords(const Matrix4x4& vm, int scrW, int scrH);

// Live-Vorschau des Strahl-Fallbacks (wo der Punkt landen wuerde).
void DrawCoordRayPreview(const GameCache& cache, int scrW, int scrH);

// Liste + Datei leeren.
void ClearSavedCoords();

// Anzahl gespeicherter Punkte.
int SavedCoordCount();

// Punkte fuer Marker Aim kopieren (Rueckgabe: Anzahl, max. maxOut).
int GetSavedCoords(SavedCoord* out, int maxOut);
