#include "pch.h"
#include "Theme.h"

// ─────────────────────────────────────────────────────────────────────────────
// ui/Theme.cpp — Glutenium-Palette (Port von payload/src/ui/Theme.cpp)
// Reset-then-override wie die Referenz: Dark ist die Basis, die anderen
// Themes überschreiben nur was abweicht — so hinterlaesst ein Wechsel von
// Translucent zurueck auf Dark keine Transparenz-Reste.
// ─────────────────────────────────────────────────────────────────────────────
#include "Theme.h"

namespace ui {
namespace {

Palette kPal;
Theme   kTheme = Theme::Dark;

// Der Akzent des Spielers ueberlebt den Theme-Wechsel (wie in der Referenz).
ImVec4 kAccent     = ImVec4(0xE5 / 255.f, 0x39 / 255.f, 0x35 / 255.f, 1.f);
ImVec4 kAccentSoft = ImVec4(0xE5 / 255.f, 0x39 / 255.f, 0x35 / 255.f, 0.16f);

ImVec4 HexCol(unsigned hex, float a = 1.f) {
    return ImVec4(((hex >> 16) & 0xFF) / 255.f,
                  ((hex >> 8) & 0xFF) / 255.f,
                  (hex & 0xFF) / 255.f,
                  a);
}

}  // namespace

const char* ThemeName(Theme theme) {
    switch (theme) {
        case Theme::Dark:        return "Dark";
        case Theme::Translucent: return "Translucent";
        case Theme::Crimson:     return "Crimson";
        case Theme::Midnight:    return "Midnight";
        default:                 return "Dark";
    }
}

const Palette& Pal() { return kPal; }

Theme GetTheme() { return kTheme; }

void SetTheme(Theme theme) {
    kTheme = theme;
    const ImVec4 savedAccent = kAccent;
    const ImVec4 savedSoft   = kAccentSoft;

    /* Reset auf Dark (die Basis der Referenz). */
    kPal.window        = HexCol(0x171717);
    kPal.card          = HexCol(0x232323);
    kPal.cardHover     = HexCol(0x2B2B2B);
    kPal.setting       = HexCol(0x1E1E1E);
    kPal.settingHover  = HexCol(0x272727);
    kPal.button        = HexCol(0x1E1E1E);
    kPal.buttonHover   = HexCol(0x2A2A2A);
    kPal.track         = HexCol(0x333333);
    kPal.sidebarActive = HexCol(0x202020);
    kPal.border        = HexCol(0xFFFFFF, 0.06f);
    kPal.borderStrong  = HexCol(0xFFFFFF, 0.14f);

    switch (theme) {
        case Theme::Translucent: {
            /* Frosted Glass: ohne echtes Backdrop-Blur im Overlay bleiben nur
             * die Alpha-Tints — dunkler, aber gleiche Struktur. */
            const float a = 0.78f;
            kPal.window        = HexCol(0x14181D, a);
            kPal.card          = HexCol(0x14181D, 0.72f);
            kPal.cardHover     = HexCol(0x1C222B, 0.78f);
            kPal.setting       = HexCol(0x14181D, 0.72f);
            kPal.settingHover  = HexCol(0x1C222B, 0.78f);
            kPal.button        = HexCol(0x14181D, 0.72f);
            kPal.buttonHover   = HexCol(0x1C222B, 0.80f);
            kPal.track         = HexCol(0x2A313C, 0.85f);
            kPal.sidebarActive = HexCol(0x1C222B, 0.76f);
            kPal.border        = HexCol(0xFFFFFF, 0.09f);
            kPal.borderStrong  = HexCol(0xFFFFFF, 0.18f);
            break;
        }
        case Theme::Crimson: {
            /* Getrocknetes Blutrot: warme Slabs, der Akzent sitzt in der
             * eigenen Familie statt auf Grau. */
            kPal.window        = HexCol(0x1D1215);
            kPal.card          = HexCol(0x2A161B);
            kPal.cardHover     = HexCol(0x331B21);
            kPal.setting       = HexCol(0x241316);
            kPal.settingHover  = HexCol(0x2E181D);
            kPal.button        = HexCol(0x241316);
            kPal.buttonHover   = HexCol(0x331C22);
            kPal.track         = HexCol(0x402227);
            kPal.sidebarActive = HexCol(0x2E181D);
            break;
        }
        case Theme::Midnight: {
            /* Kaltes Abyss-Blau: tiefe Slate-Slabs mit Stahlkanten. */
            kPal.window        = HexCol(0x0E1320);
            kPal.card          = HexCol(0x141B2C);
            kPal.cardHover     = HexCol(0x1B2440);
            kPal.setting       = HexCol(0x11172A);
            kPal.settingHover  = HexCol(0x182036);
            kPal.button        = HexCol(0x11172A);
            kPal.buttonHover   = HexCol(0x1C2742);
            kPal.track         = HexCol(0x26314D);
            kPal.sidebarActive = HexCol(0x182036);
            break;
        }
        default:
            break;
    }

    /* Der Akzent ist des Spielers eigener. */
    kAccent     = savedAccent;
    kAccentSoft = savedSoft;
    kPal.accent    = kAccent;
    kPal.accentSoft = kAccentSoft;
    kPal.accentDim = ImVec4(kAccent.x * 0.7f, kAccent.y * 0.7f, kAccent.z * 0.7f, 1.f);

    kPal.textBright = HexCol(0xFFFFFF);
    kPal.text       = HexCol(0xFFFFFF, 0.86f);
    kPal.textDim    = HexCol(0xFFFFFF, 0.51f);
    kPal.textFaint  = HexCol(0xFFFFFF, 0.27f);

    kPal.positive = HexCol(0x5BE59A);
    kPal.warning  = HexCol(0xFFC55C);
    kPal.negative = HexCol(0xFF5C6E);
    kPal.shadow   = HexCol(0x000000);
}

void SetAccent(const ImVec4& accent) {
    kAccent = accent;
    kPal.accent = accent;
    kPal.accentSoft = ImVec4(accent.x, accent.y, accent.z, 0.16f);
    /* Die aktive Sidebar-Pill ist der Akzent abgestuft statt fadend — eine
     * halbtransparente Pill nimmt an was hinter ihr liegt und hoert auf,
     * zum Akzent ueberall sonst zu passen. */
    kPal.accentDim = ImVec4(accent.x * 0.7f, accent.y * 0.7f, accent.z * 0.7f, 1.f);
}

ImVec4 AccentShade(float lightness, float alpha) {
    const ImVec4& accent = kPal.accent;
    if (lightness <= 1.f) {
        const float s = lightness < 0.f ? 0.f : (lightness > 1.f ? 1.f : lightness);
        return ImVec4(accent.x * s, accent.y * s, accent.z * s, accent.w * alpha);
    }
    /* Ueber 1 geht der Schritt Richtung Weiss statt past Sättigung zu clippen
     * und dabei die Hue zu verschieben. */
    const float t = lightness - 1.f > 1.f ? 1.f : lightness - 1.f;
    return ImVec4(accent.x + (1.f - accent.x) * t,
                  accent.y + (1.f - accent.y) * t,
                  accent.z + (1.f - accent.z) * t,
                  accent.w * alpha);
}

void DrawWindowShadow(ImDrawList* dl, const ImVec2& min, const ImVec2& max,
                      float radius, float alpha) {
    if (!dl || alpha <= 0.005f) return;
    /* 5 Schichten wie in der Referenz — weit/weich draussen, eng/dicht
     * drinnen. Primitive AddRectFilled-Gauss via Uebergreifung. */
    struct Layer { float spread; float a; };
    static const Layer layers[5] = {
        { 30.f,  3.f / 255.f },
        { 20.f,  6.f / 255.f },
        { 12.f, 12.f / 255.f },
        {  6.f, 24.f / 255.f },
        {  2.f, 48.f / 255.f },
    };
    for (const Layer& l : layers) {
        const float s = l.spread;
        dl->AddRectFilled(ImVec2(min.x - s, min.y - s * 0.6f),
                          ImVec2(max.x + s, max.y + s * 0.9f),
                          IM_COL32(0, 0, 0, (int)(l.a * alpha * 255.f)), radius + s);
    }
}

}  // namespace ui
