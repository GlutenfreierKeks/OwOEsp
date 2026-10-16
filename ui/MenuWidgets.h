#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// ui/MenuWidgets.h — Glutenium-Style-Widgets (Port von ClickGui.cpp)
// Karten (Module 150px / Settings 50px), Pill-Toggles mit outBack-Thumb,
// Slider mit 4px-Track, Choice-Pills, Key-Capture, eigener HSV-Picker.
// Animationen laufen über ImGui::GetStateStorage() — kein globaler Zustand.
// ─────────────────────────────────────────────────────────────────────────────
#include "pch.h"
#include "Theme.h"

extern ImFont* g_fontBody;
extern ImFont* g_fontIcon;

namespace ui {

namespace layout {
    // Design-Pixel der Referenz (1280x760 @ scale 1), alles * Scale.
    constexpr float WinW        = 1280.f;
    constexpr float WinH        = 760.f;
    constexpr float WinRadius   = 18.f;
    constexpr float SidebarW    = 210.f;
    constexpr float SettingsW   = 360.f;

    constexpr float CardW       = 340.f;
    constexpr float CardH       = 150.f;
    constexpr float CardGap     = 12.f;
    constexpr float CardRadius  = 16.f;
    constexpr float GridPad     = 24.f;

    constexpr float SettingH    = 50.f;
    constexpr float SettingGap  = 8.f;
    constexpr float SettingRad  = 10.f;

    constexpr float ToggleW     = 44.f;
    constexpr float ToggleH     = 24.f;
    constexpr float ThumbD      = 18.f;
    constexpr float TogglePad   = 3.f;

    constexpr float PickerW     = 230.f;
    constexpr float PickerPad   = 10.f;
    constexpr float PickerFieldH= 130.f;
    constexpr float PickerStripH= 12.f;

    constexpr float SidePad     = 22.f;
    constexpr float SideItemH   = 44.f;
    constexpr float SideGap     = 8.f;
}

// Aktueller Menue-Scale (fit-to-screen * menuScale-Setting; gesetzt in Menu.cpp)
extern float g_menuScale;

// ── Animation helpers (Port aus ClickGui.cpp) ───────────────────────────────
float Approach(float current, float target, float rate, float dt);
float HoverTowards(float current, float target, float dt, float durationMs = 180.f);
float EaseOutCubic(float t);
float EaseOutBack(float t, float overshoot = 1.2f);
ImU32 ColU32(const ImVec4& c);
ImVec4 Mix(const ImVec4& a, const ImVec4& b, float t);

// ── Animation-State pro ID (in ImGui's StateStorage) ────────────────────────
float GetAnim(ImGuiID id, const char* key, float def = 0.f);
void  SetAnim(ImGuiID id, const char* key, float v);

// ── Bausteine ───────────────────────────────────────────────────────────────
// Dunkle Flaeche (Karte/Panel). Border bei Hover machen die Aufrufer.
void Surface(ImDrawList* dl, const ImVec2& min, const ImVec2& max,
             float radius, const ImVec4& tint);

// Pill-Toggle 44x24, Akzent-Fill, outBack-Thumb. Toggle selbst zeichnend.
bool ToggleWidget(ImDrawList* dl, const char* id, const ImVec2& pos,
                  bool* value, float dt);

// 50px-Setting-Reihen (Karte + Label + Control). Gibt true zurueck, solange
// ein Slider gezogen wird (fuer Drag-Handling) bzw. bei Klick.
bool BoolRow(ImDrawList* dl, const char* id, const ImVec2& pos, float width,
             const char* label, bool* value, float dt);
bool SliderRow(ImDrawList* dl, const char* id, const ImVec2& pos, float width,
               const char* label, float* value, float mn, float mx,
               const char* fmt, float dt);
bool SliderRowInt(ImDrawList* dl, const char* id, const ImVec2& pos, float width,
                  const char* label, int* value, int mn, int mx,
                  const char* fmt, float dt);
bool ChoiceRow(ImDrawList* dl, const char* id, const ImVec2& pos, float width,
               const char* label, int* value, const char* const* options,
               int count, float dt);
bool KeyRow(ImDrawList* dl, const char* id, const ImVec2& pos, float width,
            const char* label, int* key, int target, float dt);
// Farbreihe: Klick auf das Swatch toggelt den Picker (Storage "open").
bool ColorRow(ImDrawList* dl, const char* id, const ImVec2& pos, float width,
              const char* label, float* color4, float dt);
// HSV-Picker (SV-Feld + Hue-Strip). anchorRight = rechte Swatch-Kante.
// limitX = rechte Fenstergrenze (fuer Flip). Open-Flag im Storage.
void ColorPickerPopup(ImDrawList* dl, const char* id, const ImVec2& anchorRight,
                      float limitX, float* color4, float dt);

// Interner Helfer: Blockt Klicks in einem Rect (offener Picker) fuer eine
// Frame. Menu.cpp ruft das nach dem Zeichnen des Pickers auf.
void SetPickerBlock(const ImVec2& min, const ImVec2& max, bool active);

// Skaliert Design-Pixel mit dem aktuellen Menue-Scale (auch fuer Menu.cpp).
float Px(float designPx);

// Sidebar-Row (44px): Icon-Glyph + Label, Hover/Active-Ramps.
bool SideItem(ImDrawList* dl, const char* id, const ImVec2& pos, float width,
              const char* icon, const char* label, bool active, float dt);

// Modul-Karte (340x150): Name, Beschreibung, Toggle oben rechts.
// Linksklick (Karte oder Toggle) schaltet *enabled um.
// Rechtsklick gibt true zurueck → Settings-Panel oeffnen/schliessen.
// selected = Panel fuer dieses Modul ist offen (Akzent-Markierung).
bool ModuleCard(ImDrawList* dl, const char* id, const ImVec2& pos,
                float width, float height, const char* name,
                const char* desc, bool* enabled, float dt,
                bool selected = false);

// Pill-Button (Configs etc.)
bool PillButton(ImDrawList* dl, const char* id, const ImVec2& pos,
                float width, float height, const char* label, float dt);

// Beschriftete Karte ohne eigenes Control (HUD-Editor-Steuerleiste etc.)
void LabeledCard(ImDrawList* dl, const char* id, const ImVec2& pos,
                 float width, const char* label, float dt);

// Text-Helfer
float MeasureText(const char* text, float size);
void  DrawTextCentered(ImDrawList* dl, ImVec2 center, const char* text, float size, ImU32 col);
void  DrawTextRight(ImDrawList* dl, ImVec2 topRight, const char* text, float size, ImU32 col);
// Schneidet mit "..." auf maxWidth zurueck (in dest buffer).
void  ElideText(char* dest, size_t destSize, const char* src, float maxWidth, float size);

// VK-Codes lesbar (auch vom Renderer/KeyCapture genutzt)
const char* VkToString(int vk);

}  // namespace ui
