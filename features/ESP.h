#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// features/ESP.h — sichtbare Elemente (Boxen, Health, Namen, Bombe, Items)
// + Radar Overlay auf echtem CS2-Radar
// ─────────────────────────────────────────────────────────────────────────────
#include "Cache.h"
#include "../platform/platform.h" // OwO_Tex (Textur-Handle, plattformneutral)

// Alle ESP-Elemente zeichnen (nur im Render-Thread aufrufen!)
void DrawESP(const GameCache& cache, int scrW, int scrH);

// Fadenkreuz + Aim-FOV-Kreise — reine Bildschirm-Elemente, laufen UNABHAENGIG
// vom ESP-Master (enabled); nur im Render-Thread aufrufen!
void DrawHudOverlay(int scrW, int scrH);
void DrawBombTimer(const BombData& bomb, int scrW, int scrH);
void DrawItemESP(const GroundCache& ground, const GameCache* cache, int scrW, int scrH);
void DrawRadar(const GameCache& cache, int scrW, int scrH);

// Arrow ESP: Pfeile auf einem Ring um das Fadenkreuz Richtung Gegner
// (Offscreen-Indicator; Farbe = Spotted/Hidden-Farben). Render-Thread only.
void DrawArrowESP(const GameCache& cache, int scrW, int scrH);

// Granaten-Landepunkt-/Explosions-Marker (implementiert in GrenadeVisuals.cpp):
// Marker + Restzeit-Countdown am Ereignispunkt der gehaltenen Nade (Winkel aus
// der ViewMatrix), der eigenen fliegenden Nade (echte Ballistik) und als
// Ghost nach dem Wurf. Bewusst OHNE Trajektorien-Linie.
void DrawThrowPreview(const GameCache& cache, const GroundCache& ground, int scrW, int scrH);

// Passives Granaten-Tracking: Molo-Bruch-Heuristik (Feuer-Marker als Fallback,
// bis die echte Inferno-Zone sichtbar ist). util optional (unterdrückt
// Heuristik-Doppel wenn echte Zone da ist).
void DrawGrenadeTracks(const GroundCache& ground, const Matrix4x4& vm, int scrW, int scrH,
                       const UtilCache* util = nullptr);

// Util-Radius: Smoke-Sperrfläche + Molo-Flammen-Form am Boden + Restzeit.
void DrawUtilRadius(const UtilCache& util, const Matrix4x4& vm, int scrW, int scrH);

// Spectator-Liste: NUR tote Spieler, die MICH beobachten (Observer-Ziel = du).
void DrawSpecList(const GameCache& cache, int scrW);

// SVG-Texturen freigeben (beim Beenden)
void ReleaseItemTextures();

// Icon-Cache-Stand (für Profiler): Anzahl + ca. GPU-Bytes.
#include <cstddef>
size_t ItemTextureCount();
size_t ItemTextureBytes();

// Alle bekannten Waffen-/Item-Icons EINMAL beim Start laden (nach D3D11-Init).
// Danach kein SVG-Parse mehr im Frame.
#ifdef _WIN32
struct ID3D11Device;
void PreloadWeaponTextures(ID3D11Device* device);
#else
// Linux: nutzt den aktuellen GL-Kontext (muss beim Aufruf current sein).
void PreloadWeaponTextures();
#endif