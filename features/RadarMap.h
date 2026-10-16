#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// features/RadarMap.h — Ganzkarten-Radar mit echten CS2-Map-Bildern.
//
// Assets liegen neben der EXE in radar/ (<map>.png + maps.json; Quelle:
// boltobserv-Projekt — radial-Transform: res/offx/offy je Map). Zeichnet in
// das Radar-Rechteck des Overlays; gibt false zurück, wenn Bild/Meta fehlen —
// der Aufrufer fällt dann auf den lokalen Relativ-Radar zurück.
// Läuft ausschließlich im Render-Thread.
// ─────────────────────────────────────────────────────────────────────────────
struct GameCache;

// true = Ganzkarte gezeichnet (oder Meta vorhanden). false = Aufrufer soll
// den lokalen Radar zeichnen (Assets fehlen / Map unbekannt).
bool RadarMapDrawFull(const GameCache& cache, float x, float y, float size);
