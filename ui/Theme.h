#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// ui/Theme.h — Glutenium-Style-Palette (Portierung von payload/src/ui/Theme)
// Ein dunkler Slab, ein Akzent. Keine Borders, keine Gradienten auf Controls:
// Zustand wird ueber Deckkraft + Akzent gesagt. 4 Themes, 1 lebender Akzent.
// ─────────────────────────────────────────────────────────────────────────────
#include "pch.h"

namespace ui {

enum class Theme : int {
    Dark        = 0,
    Translucent = 1,
    Crimson     = 2,
    Midnight    = 3,
    Count       = 4,
};

const char* ThemeName(Theme theme);

struct Palette {
    // Menue + HUD-Oberflaechen. Themes ueberschreiben diese; jede Flaeche
    // liest sie, damit ein Theme-Wechsel alles auf einmal umfaerbt.
    ImVec4 window;        // Menue-Fensterkoerper
    ImVec4 card;          // Modul-Karte
    ImVec4 cardHover;
    ImVec4 setting;       // Setting-Karte (50px-Reihe)
    ImVec4 settingHover;
    ImVec4 button;
    ImVec4 buttonHover;
    ImVec4 track;         // Slider-Track
    ImVec4 sidebarActive; // aktive Sidebar-Pill
    ImVec4 border;        // hauchfeine weiße Linie (6%)
    ImVec4 borderStrong;  // 14%

    ImVec4 accent;
    ImVec4 accentSoft;    // 16% Alpha
    ImVec4 accentDim;     // Akzent * 0.7 (Hover-Border)

    ImVec4 textBright;    // 100%
    ImVec4 text;          // 86%
    ImVec4 textDim;       // 51%
    ImVec4 textFaint;     // 27%

    ImVec4 positive;
    ImVec4 warning;
    ImVec4 negative;

    ImVec4 shadow;
};

const Palette& Pal();

// Re-skinnt jede Flaeche auf einmal. Der Akzent bleibt erhalten.
void SetTheme(Theme theme);
Theme GetTheme();

// Der Akzent ist lebendig: Menue, HUD-Editor und Search bewegen sich mit.
void SetAccent(const ImVec4& accent);

/* Der Akzent, entlang der eigenen Achse verschoben statt auf eine andere
 * Hue. lightness < 1 -> Richtung Schwarz, > 1 -> Richtung Weiss. */
ImVec4 AccentShade(float lightness, float alpha = 1.0f);

// 5 geschichtete, weiche Schatten-Rects hinter einem abgerundeten Fenster.
void DrawWindowShadow(ImDrawList* dl, const ImVec2& min, const ImVec2& max,
                      float radius, float alpha);

}  // namespace ui
