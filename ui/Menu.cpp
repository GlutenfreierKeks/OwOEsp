#include "pch.h"
#include "Menu.h"
#include "MenuShared.h"
#include "MenuWidgets.h"
#include "MenuModel.h"
#include "MenuAssets.h"
#include "../Montserrat-Regular.h"
#include "../core/OffsetsUpdater.h"

// ─────────────────────────────────────────────────────────────────────────────
// ui/Menu.cpp — Glutenium-ClickGUI (Port von payload/src/ui/ClickGui.cpp)
// Fenster (1280x760 * Scale, Radius 18, 5-Layer-Schatten), Sidebar (Logo,
// Kategorien, Aktionen), scrollbares Modul-Karten-Grid, 360px-Settings-Panel
// das von rechts reinslidet, Suche als eigener View, HUD-Editor in
// Vollbreite, Configs-View. Alle Views laufen über die Tabelle in MenuModel.
// ─────────────────────────────────────────────────────────────────────────────

ImFont* g_fontIcon   = nullptr;
ImFont* g_fontBody   = nullptr;
ImFont* g_fontHeader = nullptr;

// Aus MenuWidgets (Design-px * Scale)
using ui::Px;

namespace ui {

// ── Views ────────────────────────────────────────────────────────────────────
enum class View : int { Modules, Search, HudEditor, Configs, Client };

static constexpr float kBodySize = 14.f;   // Design-px, * Scale beim Zeichnen
static constexpr float kDisplaySize = 18.f;

// Animations-/UI-Zustand (Menü ist exklusiv offen — globaler State ok)
namespace {
View   s_view       = View::Modules;
int    s_cat        = 0;            // aktive Kategorie
int    s_selected   = -1;           // Modul-Index (Settings-Panel offen)
float  s_alpha      = 0.f;          // Fenster-Open/Close
float  s_settingsA  = 0.f;          // Panel-Slide
float  s_setScroll  = 0.f, s_setTarget = 0.f;
float  s_gridScroll = 0.f, s_gridTarget = 0.f;
float  s_hudScroll  = 0.f, s_hudTarget = 0.f;
float  s_cfgScroll  = 0.f, s_cfgTarget = 0.f;
char   s_query[64]  = {};
std::vector<SearchHit> s_hits;
bool   s_firstFrame = true;

ImVec2 s_winMin, s_winMax;           // Fenster-Rect (für Picker-Flip)
float  s_dt = 1.f / 60.f;

const float kFitMax = 1.0f;

float ScaleFit() {
    ImGuiIO& io = ImGui::GetIO();
    const float fbW = io.DisplaySize.x, fbH = io.DisplaySize.y;
    const float fit = fbW * 0.94f / layout::WinW < fbH * 0.94f / layout::WinH
                          ? fbW * 0.94f / layout::WinW
                          : fbH * 0.94f / layout::WinH;
    float s = g_cfg.menuScale * (fit < kFitMax ? fit : kFitMax);
    if (s < 0.3f) s = 0.3f;
    if (s > 2.5f) s = 2.5f;
    return s;
}

const CategoryDef* Cat(int i) {
    const std::vector<CategoryDef>& m = MenuModel();
    return i >= 0 && i < (int)m.size() ? &m[i] : nullptr;
}

void DrawWindow(ImDrawList* dl) {
    DrawWindowShadow(dl, s_winMin, s_winMax, Px(layout::WinRadius), s_alpha);
    Surface(dl, s_winMin, s_winMax, Px(layout::WinRadius), Pal().window);
}

// ── Sidebar ──────────────────────────────────────────────────────────────────
struct SideAction { const char* id; const char* icon; const char* label; View view; };
const SideAction kActions[] = {
    { "act_search", "V", "Suche",       View::Search },
    { "act_hud",    "H", "HUD Editor",  View::HudEditor },
    { "act_cfg",    "C", "Configs",     View::Configs },
    { "act_client", "S", "Einstellungen", View::Client },
};

void RenderSidebar(ImDrawList* dl) {
    const Palette& pal = Pal();
    const float pad = Px(layout::SidePad);

    // Logo + Name
    if (g_texLogo) {
        dl->AddImage((ImTextureID)(uintptr_t)g_texLogo,
                     ImVec2(s_winMin.x + pad, s_winMin.y + Px(20.f)),
                     ImVec2(s_winMin.x + pad + Px(34.f), s_winMin.y + Px(20.f) + Px(34.f)));
    }
    dl->AddText(g_fontHeader, Px(kDisplaySize),
                ImVec2(s_winMin.x + pad + Px(42.f), s_winMin.y + Px(28.f)),
                ColU32(pal.textBright), "PixelMommySense");

    const float sepY = s_winMin.y + Px(72.f);
    dl->AddRectFilled(ImVec2(s_winMin.x + pad, sepY),
                      ImVec2(s_winMin.x + Px(layout::SidebarW) - pad, sepY + (Px(1.f) > 1.f ? Px(1.f) : 1.f)),
                      ColU32(pal.accent), 0.f);

    float y = sepY + Px(16.f);
    const float itemW = Px(layout::SidebarW) - pad * 2.f;

    // Kategorien
    const std::vector<CategoryDef>& model = MenuModel();
    for (int i = 0; i < (int)model.size(); ++i) {
        bool active = (s_view == View::Modules || s_view == View::Search) && s_cat == i;
        if (SideItem(dl, model[i].name, ImVec2(s_winMin.x + pad, y), itemW,
                     model[i].icon, model[i].name, active, s_dt)) {
            s_view = View::Modules;
            s_cat = i;
            s_selected = -1;
            s_gridTarget = 0.f;
            s_setTarget = 0.f;
        }
        y += Px(layout::SideItemH) + Px(layout::SideGap);
    }

    y += Px(24.f) - Px(layout::SideGap);

    // Aktionen
    for (const SideAction& a : kActions) {
        bool active = s_view == a.view;
        if (SideItem(dl, a.id, ImVec2(s_winMin.x + pad, y), itemW, a.icon, a.label, active, s_dt)) {
            s_view = a.view;
            s_selected = -1;
            s_hudTarget = 0.f;
            s_cfgTarget = 0.f;
        }
        y += Px(layout::SideItemH) + Px(4.f);
    }

    // Version unten
    dl->AddText(g_fontBody, Px(12.f),
                ImVec2(s_winMin.x + pad, s_winMax.y - Px(30.f)),
                ColU32(pal.textFaint), "PixelMommySense v1.0");
}

// ── Header über dem Grid (Kategorietitel + Info) ────────────────────────────
// Lokales min (Makro-Schatten von windows.h vermeiden)
namespace {
struct UiRect { ImVec2 mn, mx; bool Contains(const ImVec2& p) const {
    return p.x >= mn.x && p.x <= mx.x && p.y >= mn.y && p.y <= mx.y; } };
}

void RenderGridHeader(ImDrawList* dl, float x, float y, float w, const char* title, const char* sub) {
    dl->AddText(g_fontHeader, Px(kDisplaySize), ImVec2(x, y), ColU32(Pal().textBright), title);
    dl->AddText(g_fontBody, Px(13.f), ImVec2(x, y + Px(24.f)), ColU32(Pal().textDim), sub);
    const float sepY = y + Px(24.f) + Px(13.f) + Px(14.f);
    dl->AddRectFilled(ImVec2(x, sepY), ImVec2(x + w, sepY + (Px(1.f) > 1.f ? Px(1.f) : 1.f)),
                      ColU32(Pal().accent), 0.f);
}

// ── Modul-Grid ───────────────────────────────────────────────────────────────
void RenderGrid(ImDrawList* dl, ImVec2 gmin, ImVec2 gmax) {
    const CategoryDef* c = Cat(s_cat);
    if (!c) return;

    RenderGridHeader(dl, gmin.x + Px(layout::GridPad), gmin.y + Px(20.f),
                     gmax.x - gmin.x - Px(layout::GridPad) * 2.f, c->name,
                     "Linksklick: An/Aus  •  Rechtsklick: Einstellungen");
    const float headerH = Px(20.f) + Px(24.f) + Px(13.f) + Px(14.f) + Px(20.f) + Px(1.f);

    // Grid-Maße
    const float availW = (gmax.x - gmin.x) - Px(layout::GridPad) * 2.f;
    int cols = (int)((availW + Px(layout::CardGap)) / (Px(layout::CardW) + Px(layout::CardGap)));
    if (cols < 1) cols = 1;
    const float cardW = (availW - (float)(cols - 1) * Px(layout::CardGap)) / (float)cols;
    const float cellH = Px(layout::CardH) + Px(layout::CardGap);

    const int count = (int)c->modules.size();
    const int rows = (count + cols - 1) / cols;
    const float totalH = (float)rows * cellH + Px(16.f);
    const float listTop = gmin.y + headerH + Px(16.f);
    const float listH = gmax.y - listTop;
    const float maxScroll = totalH - listH > 0.f ? totalH - listH : 0.f;
    s_gridTarget = s_gridTarget < -maxScroll ? -maxScroll : (s_gridTarget > 0.f ? 0.f : s_gridTarget);
    s_gridScroll = Approach(s_gridScroll, s_gridTarget, 14.f, s_dt);

    // Mausrad
    ImGuiIO& io = ImGui::GetIO();
    UiRect gridRect{ gmin, gmax };
    if (gridRect.Contains(io.MousePos) && io.MouseWheel != 0.f)
        s_gridTarget += io.MouseWheel * Px(48.f);

    dl->PushClipRect(gmin, gmax, true);
    float y = listTop + s_gridScroll;
    for (int i = 0; i < count; ++i) {
        const int col = i % cols;
        const int row = i / cols;
        const float cx = gmin.x + Px(layout::GridPad) + (float)col * (cardW + Px(layout::CardGap));
        const float cy = listTop + s_gridScroll + (float)row * cellH;
        if (cy + Px(layout::CardH) < gmin.y || cy > gmax.y) continue;

        ModuleDef& md = const_cast<CategoryDef*>(c)->modules[i];
        char id[64];
        snprintf(id, sizeof(id), "card_%s", md.name);
        // Linksklick toggelt in ModuleCard direkt, Rechtsklick (true) oeffnet Panel
        if (ModuleCard(dl, id, ImVec2(cx, cy), cardW, Px(layout::CardH),
                       md.name, md.desc, md.enabled, s_dt, i == s_selected)) {
            s_selected = s_selected == i ? -1 : i;
            s_setTarget = 0.f;
        }
    }
    dl->PopClipRect();
}

// ── Settings-Panel (slidet von rechts) ──────────────────────────────────────
void RenderSettingsPanel(ImDrawList* dl) {
    const CategoryDef* c = Cat(s_cat);
    if (!c || s_selected < 0 || s_selected >= (int)c->modules.size()) return;
    ModuleDef& md = const_cast<CategoryDef*>(c)->modules[s_selected];
    const Palette& pal = Pal();

    const float progress = EaseOutCubic(s_settingsA);
    const float panelW = Px(layout::SettingsW);
    const float px_ = s_winMax.x - panelW - Px(16.f) + (1.f - progress) * Px(34.f);
    const ImVec2 pmin(px_, s_winMin.y + Px(8.f));
    const ImVec2 pmax(px_ + panelW, s_winMax.y - Px(8.f));

    dl->AddRectFilled(pmin, pmax, IM_COL32(0, 0, 0, (int)(0.35f * 255.f * s_settingsA)), Px(16.f));
    Surface(dl, pmin, pmax, Px(16.f), pal.setting);

    const float innerX = pmin.x + Px(22.f);
    const float innerW = panelW - Px(44.f);

    // Titel + Beschreibung + Separator
    dl->AddText(g_fontHeader, Px(kDisplaySize), ImVec2(innerX, pmin.y + Px(20.f)),
                ColU32(pal.textBright), md.name);
    dl->AddText(g_fontBody, Px(13.f), ImVec2(innerX, pmin.y + Px(20.f) + Px(22.f) + Px(6.f)),
                ColU32(pal.textDim), md.desc);
    const float sepY = pmin.y + Px(20.f) + Px(22.f) + Px(6.f) + Px(13.f) + Px(14.f);
    dl->AddRectFilled(ImVec2(innerX, sepY), ImVec2(innerX + innerW, sepY + (Px(1.f) > 1.f ? Px(1.f) : 1.f)),
                      ColU32(pal.accent), 0.f);

    // Scroll-Bereich (erste Reihe: Master-Schalter, damit AN/AUS auch hier geht)
    float y = sepY + Px(20.f);
    const float visibleH = pmax.y - y - Px(16.f);
    float contentH = (float)(md.settings.size() + 1) * (Px(layout::SettingH) + Px(layout::SettingGap));
    // Keybind-Karten extra
    bool hasKey = false;
    for (const SettingDef& s : md.settings) if (s.type == SettingType::Key) hasKey = true;

    const float maxScroll = contentH - visibleH > 0.f ? contentH - visibleH : 0.f;
    s_setTarget = s_setTarget < -maxScroll ? -maxScroll : (s_setTarget > 0.f ? 0.f : s_setTarget);
    s_setScroll = Approach(s_setScroll, s_setTarget, 14.f, s_dt);

    ImGuiIO& io = ImGui::GetIO();
    UiRect panelRect{ pmin, pmax };
    if (panelRect.Contains(io.MousePos) && io.MouseWheel != 0.f)
        s_setTarget += io.MouseWheel * Px(48.f);

    dl->PushClipRect(ImVec2(pmin.x, pmin.y), ImVec2(pmax.x, pmax.y), true);

    float cy = y + s_setScroll;
    // Master-Schalter oben — spiegelt den Karten-Toggle (Linksklick-Logik bleibt gleich)
    {
        char mid[96];
        snprintf(mid, sizeof(mid), "set_%s_master", md.name);
        if (cy + Px(layout::SettingH) >= pmin.y && cy <= pmax.y)
            BoolRow(dl, mid, ImVec2(innerX, cy), innerW, "Aktiviert", md.enabled, s_dt);
        cy += Px(layout::SettingH) + Px(layout::SettingGap);
    }
    int keyRows = 0;
    for (const SettingDef& s : md.settings) {
        if (cy + Px(layout::SettingH) < pmin.y || cy > pmax.y) { cy += Px(layout::SettingH) + Px(layout::SettingGap); continue; }
        char id[96];
        snprintf(id, sizeof(id), "set_%s_%s", md.name, s.label);

        switch (s.type) {
        case SettingType::Bool:
            BoolRow(dl, id, ImVec2(innerX, cy), innerW, s.label, (bool*)s.ptr, s_dt);
            break;
        case SettingType::Float:
            SliderRow(dl, id, ImVec2(innerX, cy), innerW, s.label,
                      (float*)s.ptr, s.mn, s.mx, s.fmt, s_dt);
            break;
        case SettingType::Int:
            SliderRowInt(dl, id, ImVec2(innerX, cy), innerW, s.label,
                         (int*)s.ptr, (int)s.mn, (int)s.mx, s.fmt, s_dt);
            break;
        case SettingType::Choice:
            ChoiceRow(dl, id, ImVec2(innerX, cy), innerW, s.label,
                      (int*)s.ptr, s.options, s.optCount, s_dt);
            break;
        case SettingType::Color: {
            float* col4 = (float*)s.ptr;
            ColorRow(dl, id, ImVec2(innerX, cy), innerW, s.label, col4, s_dt);
            break;
        }
        case SettingType::Key:
            KeyRow(dl, id, ImVec2(innerX, cy), innerW, s.label,
                   (int*)s.ptr, s.keyTarget, s_dt);
            break;
        }
        cy += Px(layout::SettingH) + Px(layout::SettingGap);
    }

    // Picker (nach allen Rows zeichnen — liegen oben)
    // +1 Reihe fuer den Master-Schalter oben
    cy = y + s_setScroll + Px(layout::SettingH) + Px(layout::SettingGap);
    for (const SettingDef& s : md.settings) {
        if (s.type == SettingType::Color) {
            char id[96];
            snprintf(id, sizeof(id), "set_%s_%s", md.name, s.label);
            float* col4 = (float*)s.ptr;
            // Swatch-Anker: linke Kante der 24px-Fleache (wie in ColorRow)
            ImVec2 anchor(innerX + innerW - Px(14.f) - Px(24.f), cy + Px(13.f));
            ColorPickerPopup(dl, id, anchor, pmax.x - Px(8.f), col4, s_dt);
        }
        cy += Px(layout::SettingH) + Px(layout::SettingGap);
    }

    dl->PopClipRect();
}

// ── Search-View ──────────────────────────────────────────────────────────────
void RenderSearch(ImDrawList* dl, ImVec2 gmin, ImVec2 gmax) {
    const float pad = Px(layout::GridPad);
    // Eingabefeld oben (ImGui-native, in Fensterfarbe gestylt)
    ImGui::SetCursorScreenPos(ImVec2(gmin.x + pad, gmin.y + Px(24.f)));
    ImGui::PushItemWidth((gmax.x - gmin.x) - pad * 2.f);
    ImGui::InputText("##searchquery", s_query, sizeof(s_query));
    ImGui::PopItemWidth();

    const float inputH = ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().FramePadding.y * 2.f;
    const float listTop = gmin.y + Px(24.f) + inputH + Px(16.f);

    SearchSettings(s_query, s_hits);

    const float rowW = (gmax.x - gmin.x) - pad * 2.f;
    float totalH = (float)s_hits.size() * (Px(layout::SettingH) + Px(layout::SettingGap)) + Px(16.f);
    const float maxScroll = totalH - (gmax.y - listTop) > 0.f ? totalH - (gmax.y - listTop) : 0.f;
    s_gridTarget = s_gridTarget < -maxScroll ? -maxScroll : (s_gridTarget > 0.f ? 0.f : s_gridTarget);
    s_gridScroll = Approach(s_gridScroll, s_gridTarget, 14.f, s_dt);

    ImGuiIO& io = ImGui::GetIO();
    UiRect gr{ gmin, gmax };
    if (gr.Contains(io.MousePos) && io.MouseWheel != 0.f) s_gridTarget += io.MouseWheel * Px(48.f);

    dl->PushClipRect(gmin, gmax, true);
    const std::vector<CategoryDef>& model = MenuModel();
    float cy = listTop + s_gridScroll;
    for (const SearchHit& h : s_hits) {
        if (cy + Px(layout::SettingH) < gmin.y || cy > gmax.y) {
            cy += Px(layout::SettingH) + Px(layout::SettingGap);
            continue;
        }
        const ModuleDef& md = model[h.cat].modules[h.mod];
        const SettingDef& s = md.settings[h.set];
        char id[128];
        snprintf(id, sizeof(id), "srch_%s_%s", md.name, s.label);

        // Modulname klein ueber der Row? Nein — Referenz zeigt flache Rows.
        switch (s.type) {
        case SettingType::Bool:
            BoolRow(dl, id, ImVec2(gmin.x + pad, cy), rowW, s.label, (bool*)s.ptr, s_dt);
            break;
        case SettingType::Float:
            SliderRow(dl, id, ImVec2(gmin.x + pad, cy), rowW, s.label,
                      (float*)s.ptr, s.mn, s.mx, s.fmt, s_dt);
            break;
        case SettingType::Int:
            SliderRowInt(dl, id, ImVec2(gmin.x + pad, cy), rowW, s.label,
                         (int*)s.ptr, (int)s.mn, (int)s.mx, s.fmt, s_dt);
            break;
        case SettingType::Choice:
            ChoiceRow(dl, id, ImVec2(gmin.x + pad, cy), rowW, s.label,
                      (int*)s.ptr, s.options, s.optCount, s_dt);
            break;
        case SettingType::Color:
            ColorRow(dl, id, ImVec2(gmin.x + pad, cy), rowW, s.label, (float*)s.ptr, s_dt);
            break;
        case SettingType::Key:
            KeyRow(dl, id, ImVec2(gmin.x + pad, cy), rowW, s.label,
                   (int*)s.ptr, s.keyTarget, s_dt);
            break;
        }
        cy += Px(layout::SettingH) + Px(layout::SettingGap);
    }
    if (s_hits.empty() && s_query[0]) {
        dl->AddText(g_fontBody, Px(14.f), ImVec2(gmin.x + pad, listTop),
                    ColU32(Pal().textDim), "Keine Einstellungen gefunden.");
    }
    dl->PopClipRect();
}

// ── HUD-Editor (Vollbreite, wie der alte Tab F) ─────────────────────────────
void RenderHudEditor(ImDrawList* dl, ImVec2 gmin, ImVec2 gmax) {
    RenderGridHeader(dl, gmin.x + Px(layout::GridPad), gmin.y + Px(20.f),
                     gmax.x - gmin.x - Px(layout::GridPad) * 2.f,
                     "HUD Editor", "Elemente ziehen — Positionen landen in der Config");
    const float headerH = Px(20.f) + Px(24.f) + Px(13.f) + Px(14.f) + Px(20.f) + Px(1.f);

    const float pad = Px(layout::GridPad);
    const float ctrlW = Px(220.f);
    const float listTop = gmin.y + headerH + Px(16.f);
    const float areaH = gmax.y - listTop;

    // Steuerleiste links (scrollbar)
    float contentH = Px(300.f);
    const float maxScroll = contentH - areaH > 0.f ? contentH - areaH : 0.f;
    s_hudTarget = s_hudTarget < -maxScroll ? -maxScroll : (s_hudTarget > 0.f ? 0.f : s_hudTarget);
    s_hudScroll = Approach(s_hudScroll, s_hudTarget, 14.f, s_dt);
    ImGuiIO& io = ImGui::GetIO();
    UiRect cr{ ImVec2(gmin.x, listTop), ImVec2(gmin.x + pad + ctrlW, gmax.y) };
    if (cr.Contains(io.MousePos) && io.MouseWheel != 0.f) s_hudTarget += io.MouseWheel * Px(48.f);

    dl->PushClipRect(ImVec2(gmin.x, listTop), gmax, true);
    float y = listTop + s_hudScroll;
    const float rowW = ctrlW;

    // Offsets-Anzeige
    char buf[128];
    snprintf(buf, sizeof(buf), "X: %.0f  Y: %.0f", g_cfg.nameOffsetX, g_cfg.nameOffsetY);
    dl->AddText(g_fontBody, Px(13.f), ImVec2(gmin.x + pad, y), ColU32(Pal().textDim), buf);
    y += Px(24.f);

    if (PillButton(dl, "hud_reset", ImVec2(gmin.x + pad, y), rowW - Px(10.f), Px(30.f), "Reset", s_dt)) {
        g_cfg.nameOffsetX = 0.f;       g_cfg.nameOffsetY = -15.f;
        g_cfg.healthBarOffsetX = -7.f; g_cfg.healthBarOffsetY = 0.f;
        g_cfg.weaponIconOffsetX = 0.f; g_cfg.weaponIconOffsetY = 2.f;
        g_cfg.distanceOffsetX = 0.f;   g_cfg.distanceOffsetY = -30.f;
    }
    y += Px(38.f);

    if (PillButton(dl, "hud_upd", ImVec2(gmin.x + pad, y), rowW - Px(10.f), Px(30.f),
                   "Offsets aktualisieren", s_dt)) {
        offsetsupdater::StartAutoRefresh();
    }
    y += Px(38.f);

    // Status (mit Dump-Stand — bei CS2-Update sofort sichtbar ob a2x aktuell ist)
    {
        int ost = offsetsupdater::Status();
        char stbuf[96];
        const char* txt = nullptr;
        if (ost == 1) txt = "Update läuft ...";
        else if (ost == 2) {
            snprintf(stbuf, sizeof(stbuf), "OK — Dump %s (%d Werte)",
                     offsetsupdater::DumpDate(), offsetsupdater::AppliedCount());
            txt = stbuf;
        }
        else if (ost == 3) txt = "Fehler — offline?";
        else txt = "Noch nicht geprüft";
        ImU32 col = ost == 2 ? ColU32(Pal().positive) : ColU32(Pal().textDim);
        dl->AddText(g_fontBody, Px(13.f), ImVec2(gmin.x + pad, y), col, txt);
        y += Px(24.f);
    }

    // HUD-Canvas rechts (Stick-Figur + draggable Elemente) — Port des alten Codes
    const float canvasX = gmin.x + pad + ctrlW + Px(24.f);
    const float canvasW = gmax.x - pad - canvasX;
    const ImVec2 cmin(canvasX, listTop);
    const ImVec2 cmax(gmax.x - pad, gmax.y);
    Surface(dl, cmin, cmax, Px(12.f), Pal().setting);
    dl->AddRect(cmin, cmax, ColU32(Pal().border), Px(12.f), 0, Px(1.f));

    float cx = (cmin.x + cmax.x) * 0.5f;
    float cyy = (cmin.y + cmax.y) * 0.5f;
    float bodyH = Px(150.f);
    float bodyW = bodyH * 0.4f;
    float headR = bodyH * 0.2f;
    float headY = cyy - bodyH * 0.5f - headR;

    dl->AddCircleFilled(ImVec2(cx, headY), headR, IM_COL32(200, 200, 220, 255), 24);
    dl->AddLine(ImVec2(cx, headY + headR), ImVec2(cx, headY + headR + bodyH), IM_COL32(200, 200, 220, 255), Px(2.f));
    float armY = headY + headR + bodyH * 0.35f;
    dl->AddLine(ImVec2(cx - bodyW, armY), ImVec2(cx + bodyW, armY), IM_COL32(200, 200, 220, 255), Px(2.f));
    float legSpread = bodyW * 1.2f;
    float bodyBot = headY + headR + bodyH;
    dl->AddLine(ImVec2(cx, bodyBot), ImVec2(cx - legSpread, bodyBot + bodyH * 0.4f), IM_COL32(200, 200, 220, 255), Px(2.f));
    dl->AddLine(ImVec2(cx, bodyBot), ImVec2(cx + legSpread, bodyBot + bodyH * 0.4f), IM_COL32(200, 200, 220, 255), Px(2.f));

    float boxL = cx - bodyW * 1.6f, boxR = cx + bodyW * 1.6f;
    float boxT = headY - headR - Px(8.f), boxB = bodyBot + bodyH * 0.4f + Px(8.f);
    dl->AddRect(ImVec2(boxL, boxT), ImVec2(boxR, boxB), IM_COL32(255, 255, 255, 80), 0.f, 0, Px(1.f));

    // Draggable Elemente (Name/HP/Waffe/Distanz) — gleiche Mechanik wie vorher
    static int dragTarget = -1;
    static float dragOffX = 0.f, dragOffY = 0.f;
    ImVec2 mp = io.MousePos;
    bool canvasHovered = mp.x >= cmin.x && mp.x <= cmax.x && mp.y >= cmin.y && mp.y <= cmax.y;

    auto inRect = [&](float x, float yy, float w, float h) {
        return mp.x >= x - w * 0.5f && mp.x <= x + w * 0.5f && mp.y >= yy - 2.f && mp.y <= yy + h;
    };

    auto nameBox = [&]() { return ImVec2(boxL + (boxR - boxL) * 0.5f + g_cfg.nameOffsetX, boxT + g_cfg.nameOffsetY); };
    auto hpBox   = [&]() { return ImVec2(boxL + g_cfg.healthBarOffsetX, boxT + g_cfg.healthBarOffsetY); };
    auto wpBox   = [&]() { return ImVec2(boxL + (boxR - boxL) * 0.5f + g_cfg.weaponIconOffsetX - 10.f, boxB + g_cfg.weaponIconOffsetY); };
    auto distBox = [&]() { return ImVec2(boxL + (boxR - boxL) * 0.5f + g_cfg.distanceOffsetX, boxT + g_cfg.distanceOffsetY); };

    int hovered = -1;
    if (canvasHovered) {
        ImVec2 p;
        p = nameBox(); if (inRect(p.x, p.y, Px(60.f), Px(16.f))) hovered = 0;
        p = hpBox();   if (inRect(p.x, p.y, Px(16.f), (boxB - boxT) * 0.7f)) hovered = 1;
        p = wpBox();   if (inRect(p.x + Px(10.f), p.y, Px(24.f), Px(24.f))) hovered = 2;
        p = distBox(); if (inRect(p.x, p.y, Px(40.f), Px(16.f))) hovered = 3;
    }

    if (canvasHovered && io.MouseClicked[0] && hovered >= 0 && !g_keyBinding) {
        dragTarget = hovered; dragOffX = 0.f; dragOffY = 0.f;
        g_hudDragTarget = hovered;
    }
    if (!io.MouseDown[0]) {
        if (dragTarget >= 0) {
            float* ox = nullptr; float* oy = nullptr;
            if (dragTarget == 0) { ox = &g_cfg.nameOffsetX; oy = &g_cfg.nameOffsetY; }
            else if (dragTarget == 1) { ox = &g_cfg.healthBarOffsetX; oy = &g_cfg.healthBarOffsetY; }
            else if (dragTarget == 2) { ox = &g_cfg.weaponIconOffsetX; oy = &g_cfg.weaponIconOffsetY; }
            else if (dragTarget == 3) { ox = &g_cfg.distanceOffsetX; oy = &g_cfg.distanceOffsetY; }
            if (ox) { *ox += dragOffX; *oy += dragOffY; }
        }
        dragTarget = -1; g_hudDragTarget = -1; dragOffX = 0.f; dragOffY = 0.f;
    } else if (dragTarget >= 0) {
        dragOffX += io.MouseDelta.x; dragOffY += io.MouseDelta.y;
    }

    auto disp = [&](int e) -> ImVec2 {
        return dragTarget == e ? ImVec2(dragOffX, dragOffY) : ImVec2(0.f, 0.f);
    };
    auto drawBox = [&](int e, ImVec2 a, ImVec2 b) {
        if (dragTarget == e || hovered == e)
            dl->AddRect(a, b, dragTarget == e ? IM_COL32(255, 200, 50, 255) : IM_COL32(255, 255, 255, 80), Px(2.f), 0, Px(1.f));
    };

    // Name
    {
        ImVec2 p = nameBox() + disp(0);
        dl->AddText(g_fontBody, Px(13.f), ImVec2(p.x + 1.f, p.y + 1.f), IM_COL32(0, 0, 0, 180), "Player");
        dl->AddText(g_fontBody, Px(13.f), p, IM_COL32(230, 230, 240, 235), "Player");
        drawBox(0, ImVec2(p.x - Px(32.f), p.y - 2.f), ImVec2(p.x + Px(32.f), p.y + Px(16.f)));
    }
    // Healthbar
    {
        ImVec2 p = hpBox() + disp(1);
        float hpH = (boxB - boxT) * 0.7f;
        dl->AddRectFilled(ImVec2(p.x, p.y), ImVec2(p.x + Px(4.f), p.y + hpH), IM_COL32(80, 200, 90, 220), Px(2.f));
        drawBox(1, ImVec2(p.x - Px(6.f), p.y), ImVec2(p.x + Px(10.f), p.y + hpH));
    }
    // Waffe
    {
        ImVec2 p = wpBox() + disp(2);
        dl->AddRectFilled(p, ImVec2(p.x + Px(20.f), p.y + Px(22.f)), IM_COL32(90, 110, 255, 180), Px(2.f));
        dl->AddText(g_fontBody, Px(11.f), ImVec2(p.x + Px(4.f), p.y + Px(4.f)), IM_COL32(220, 220, 235, 230), "AK");
        drawBox(2, ImVec2(p.x - Px(4.f), p.y - 2.f), ImVec2(p.x + Px(24.f), p.y + Px(24.f)));
    }
    // Distanz
    {
        ImVec2 p = distBox() + disp(3);
        dl->AddText(g_fontBody, Px(13.f), ImVec2(p.x + 1.f, p.y + 1.f), IM_COL32(0, 0, 0, 180), "12m");
        dl->AddText(g_fontBody, Px(13.f), p, IM_COL32(220, 220, 235, 230), "12m");
        drawBox(3, ImVec2(p.x - Px(20.f), p.y - 2.f), ImVec2(p.x + Px(20.f), p.y + Px(16.f)));
    }

    dl->PopClipRect();
}

// ── Configs-View ─────────────────────────────────────────────────────────────
// Slots in configs/*.cfg: Name eingeben + Speichern, Liste wählen + Laden /
// Löschen / Exportieren, Import über Datei-Dialog, Ordner öffnen.
// PixelMommySense.cfg bleibt Autoload beim Start + Autosave beim Beenden.
void RenderConfigs(ImDrawList* dl, ImVec2 gmin, ImVec2 gmax) {
    RenderGridHeader(dl, gmin.x + Px(layout::GridPad), gmin.y + Px(20.f),
                     gmax.x - gmin.x - Px(layout::GridPad) * 2.f,
                     "Configs", "Slots in configs/ — Autoload: PixelMommySense.cfg");
    const float headerH = Px(20.f) + Px(24.f) + Px(13.f) + Px(14.f) + Px(20.f) + Px(1.f);
    const float pad = Px(layout::GridPad);
    const float listW = (gmax.x - gmin.x) - pad * 2.f;
    float y = gmin.y + headerH + Px(24.f);

    static char s_name[33] = {};
    static int s_sel = -1;
    static std::vector<std::string> s_list;
    static char s_status[128] = {};
    static bool s_needRefresh = true;

    if (s_needRefresh) {
        ListConfigs(s_list);
        if (s_sel >= (int)s_list.size()) s_sel = -1;
        s_needRefresh = false;
    }

    auto setStatus = [&](const char* txt) {
        snprintf(s_status, sizeof(s_status), "%s", txt);
    };

    // Namensfeld
    ImGui::SetCursorScreenPos(ImVec2(gmin.x + pad, y));
    ImGui::PushItemWidth(listW);
    ImGui::InputText("##cfgname", s_name, sizeof(s_name));
    ImGui::PopItemWidth();
    y += ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().FramePadding.y * 2.f + Px(12.f);

    // Aktionsreihe
    const float bw = (listW - Px(12.f) * 2.f) / 3.f;
    ImGui::SetCursorScreenPos(ImVec2(gmin.x + pad, y));
    if (ImGui::Button("Speichern", ImVec2(bw, Px(32.f)))) {
        if (SaveConfigAs(s_name[0] ? s_name : "default")) {
            setStatus("Gespeichert.");
            s_needRefresh = true;
        } else setStatus("Speichern fehlgeschlagen (Name?).");
    }
    ImGui::SameLine(0.f, Px(12.f));
    if (ImGui::Button("Laden", ImVec2(bw, Px(32.f)))) {
        const char* target = s_sel >= 0 && s_sel < (int)s_list.size()
            ? s_list[s_sel].c_str() : (s_name[0] ? s_name : nullptr);
        if (target && LoadConfigByName(target)) {
            ApplyMenuThemeFromConfig();
            snprintf(s_name, sizeof(s_name), "%s", target);
            setStatus("Geladen.");
        } else setStatus("Laden fehlgeschlagen.");
    }
    ImGui::SameLine(0.f, Px(12.f));
    if (ImGui::Button("Löschen", ImVec2(bw, Px(32.f)))) {
        if (s_sel >= 0 && s_sel < (int)s_list.size() && DeleteConfig(s_list[s_sel].c_str())) {
            s_sel = -1;
            s_needRefresh = true;
            setStatus("Gelöscht.");
        } else setStatus("Nichts gewählt.");
    }
    y += Px(32.f) + Px(12.f);

    // Zweite Reihe: Import / Export / Ordner / Refresh
    ImGui::SetCursorScreenPos(ImVec2(gmin.x + pad, y));
    if (ImGui::Button("Import", ImVec2(bw, Px(32.f)))) {
        if (ImportConfig()) { s_needRefresh = true; setStatus("Importiert."); }
        else setStatus("Import abgebrochen.");
    }
    ImGui::SameLine(0.f, Px(12.f));
    if (ImGui::Button("Export", ImVec2(bw, Px(32.f)))) {
        const char* target = s_sel >= 0 && s_sel < (int)s_list.size()
            ? s_list[s_sel].c_str() : (s_name[0] ? s_name : nullptr);
        if (target && ExportConfig(target)) setStatus("Exportiert.");
        else setStatus("Export abgebrochen.");
    }
    ImGui::SameLine(0.f, Px(12.f));
    if (ImGui::Button("Ordner", ImVec2(bw, Px(32.f)))) {
        OpenConfigsFolder();
        setStatus("Ordner geöffnet.");
    }
    y += Px(32.f) + Px(12.f);
    if (PillButton(dl, "cfg_refresh", ImVec2(gmin.x + pad, y), Px(180.f), Px(30.f),
                   "Aktualisieren", s_dt)) {
        s_needRefresh = true;
        setStatus("Liste aktualisiert.");
    }
    y += Px(30.f) + Px(8.f);

    if (s_status[0]) {
        dl->AddText(g_fontBody, Px(13.f), ImVec2(gmin.x + pad, y),
                    ColU32(Pal().textDim), s_status);
        y += Px(22.f);
    }

    // Liste
    dl->AddText(g_fontBody, Px(13.f), ImVec2(gmin.x + pad, y),
                ColU32(Pal().textDim), "Slots (Klick = wählen):");
    y += Px(22.f);
    const float rowH = 30.f;
    for (int i = 0; i < (int)s_list.size(); i++) {
        if (y + rowH > gmax.y - Px(8.f)) break;
        ImGui::SetCursorScreenPos(ImVec2(gmin.x + pad, y));
        char label[64];
        snprintf(label, sizeof(label), "%s##cfg_%d", s_list[i].c_str(), i);
        if (ImGui::Selectable(label, s_sel == i, 0, ImVec2(listW, rowH))) {
            s_sel = i;
            snprintf(s_name, sizeof(s_name), "%s", s_list[i].c_str());
        }
        y += rowH + 2.f;
    }
    if (s_list.empty()) {
        dl->AddText(g_fontBody, Px(13.f), ImVec2(gmin.x + pad, y),
                    ColU32(Pal().textDim), "Keine Slots — Namen eingeben + Speichern.");
    }
}

// ── Client-View (Theme/Akzent/Scale + Info) ─────────────────────────────────
void RenderClient(ImDrawList* dl, ImVec2 gmin, ImVec2 gmax) {
    RenderGridHeader(dl, gmin.x + Px(layout::GridPad), gmin.y + Px(20.f),
                     gmax.x - gmin.x - Px(layout::GridPad) * 2.f,
                     "Einstellungen", "Look & Feel des Menüs");
    const float headerH = Px(20.f) + Px(24.f) + Px(13.f) + Px(14.f) + Px(20.f) + Px(1.f);
    const float pad = Px(layout::GridPad);
    const float rowW = Px(420.f);
    float y = gmin.y + headerH + Px(24.f);

    // Theme-Choice
    static const char* kThemes[] = { "Dark", "Translucent", "Crimson", "Midnight" };
    int themeIdx = (int)g_cfg.menuTheme;
    if (ChoiceRow(dl, "client_theme", ImVec2(gmin.x + pad, y), rowW, "Theme",
                  &themeIdx, kThemes, 4, s_dt)) {
        g_cfg.menuTheme = themeIdx;
        ApplyMenuThemeFromConfig();
    }
    y += Px(layout::SettingH) + Px(layout::SettingGap);

    // Akzent
    ColorRow(dl, "client_accent", ImVec2(gmin.x + pad, y), rowW, "Akzentfarbe", g_cfg.menuAccent, s_dt);
    ColorPickerPopup(dl, "client_accent", ImVec2(gmin.x + pad + rowW - Px(14.f) - Px(24.f), y + Px(13.f)),
                     gmax.x - Px(8.f), g_cfg.menuAccent, s_dt);
    // Akzent live anwenden (nur wenn sich was geaendert hat — billig genug)
    ApplyMenuThemeFromConfig();
    y += Px(layout::SettingH) + Px(layout::SettingGap);

    // Scale
    SliderRow(dl, "client_scale", ImVec2(gmin.x + pad, y), rowW, "Menü-Scale",
              &g_cfg.menuScale, 0.5f, 2.f, "%.2fx", s_dt);
    y += Px(layout::SettingH) + Px(layout::SettingGap);

    // Overlay-Performance (Flip-Model + Waitable-Latency)
    BoolRow(dl, "client_vsync", ImVec2(gmin.x + pad, y), rowW, "VSync (FPS-schonend)",
            &g_cfg.overlayVsync, s_dt);
    y += Px(layout::SettingH) + Px(layout::SettingGap);

    SliderRowInt(dl, "client_fps", ImVec2(gmin.x + pad, y), rowW, "FPS-Limit (nur ohne VSync)",
                 &g_cfg.targetFps, 0, 240, "%d", s_dt);
    y += Px(layout::SettingH) + Px(layout::SettingGap);

    SliderRowInt(dl, "client_reader", ImVec2(gmin.x + pad, y), rowW, "Reader-Hz (144 reicht)",
                 &g_cfg.readerHz, 60, 240, "%d Hz", s_dt);
    y += Px(layout::SettingH) + Px(layout::SettingGap);

    // Ein-Klick-Latenz-Modus: VSync aus (kein Present-Wait), kein FPS-Cap,
    // Reader 144Hz. Minimale Latenz, evtl. Tearing — der UC-Weg bei Lag.
    if (PillButton(dl, "client_latency", ImVec2(gmin.x + pad, y), rowW, Px(34.f),
                   "Latenz-Modus (VSync aus, uncapped)", s_dt)) {
        g_cfg.overlayVsync = false;
        g_cfg.targetFps = 0;
        g_cfg.readerHz = 144;
        SaveConfig();
    }
    y += Px(34.f) + Px(layout::SettingGap);

    BoolRow(dl, "client_dcomp", ImVec2(gmin.x + pad, y), rowW, "DComp-Transparenz (Beta, Neustart!)",
            &g_cfg.overlayDcomp, s_dt);
    y += Px(layout::SettingH) + Px(layout::SettingGap);

    BoolRow(dl, "client_prof", ImVec2(gmin.x + pad, y), rowW, "Profiler (Leistung)",
            &g_cfg.showProfiler, s_dt);
    y += Px(layout::SettingH) + Px(layout::SettingGap);

    BoolRow(dl, "client_dbg", ImVec2(gmin.x + pad, y), rowW, "Debug-Log (Konsole, bremst!)",
            &g_cfg.debugLog, s_dt);
    y += Px(layout::SettingH) + Px(layout::SettingGap);

    // Info-Zeilen
    const char* info[] = {
        "Render + Reader getrennt (Thread-Modell)",
        "DISCARD Swap (LWA_COLORKEY kompatibel)",
        "VSync an = kein Busy-Loop, DWM atmet",
        "DComp/Profiler-Umschalter brauchen ggf. Neustart",
    };
    for (const char* line : info) {
        dl->AddText(g_fontBody, Px(13.f), ImVec2(gmin.x + pad, y), ColU32(Pal().textDim), line);
        y += Px(22.f);
    }
}

// ── Haupt-Render ─────────────────────────────────────────────────────────────
} // namespace

} // namespace ui

// Global (Deklaration in Menu.h) — wendet Theme/Akzent aus der Config an.
void ApplyMenuThemeFromConfig() {
    ui::Theme t = ui::Theme::Dark;
    if (g_cfg.menuTheme >= 0 && g_cfg.menuTheme < (int)ui::Theme::Count)
        t = (ui::Theme)g_cfg.menuTheme;
    ui::SetTheme(t);
    ui::SetAccent(ImVec4(g_cfg.menuAccent[0], g_cfg.menuAccent[1],
                         g_cfg.menuAccent[2], 1.f));
}

// ── Init ─────────────────────────────────────────────────────────────────────
#ifdef _WIN32
void InitMenu(ID3D11Device* device) {
#else
void InitMenu() {
#endif
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 0.f; s.FrameRounding = 0.f;
    s.WindowBorderSize = 0.f; s.FrameBorderSize = 0.f; s.PopupBorderSize = 0.f;
    s.WindowPadding = ImVec2(0, 0);
    s.ItemSpacing = ImVec2(0, 0);
    s.ScrollbarSize = 0.f;
    s.TabBarOverlineSize = 0.f;

    // Eingebettete ImGui-Widgets (Suchfeld) dezent faerben
    auto* c = s.Colors;
    const ImVec4 input = ImVec4(0.13f, 0.13f, 0.15f, 1.f);
    c[ImGuiCol_FrameBg]         = input;
    c[ImGuiCol_FrameBgHovered]  = ImVec4(0.18f, 0.18f, 0.21f, 1.f);
    c[ImGuiCol_FrameBgActive]   = ImVec4(0.22f, 0.22f, 0.26f, 1.f);
    c[ImGuiCol_Text]            = ImVec4(0.92f, 0.92f, 0.96f, 1.f);
    c[ImGuiCol_TextDisabled]    = ImVec4(0.45f, 0.45f, 0.50f, 1.f);
    c[ImGuiCol_TextSelectedBg]  = ImVec4(0.898f, 0.224f, 0.208f, 0.35f);

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImFontConfig fcfg;
    fcfg.FontDataOwnedByAtlas = false;
    // 2x Oversampling: TTF-Atlas in doppelter Auflösung -> ESP-Labels (12px
    // aus 16px-Font skaliert) bleiben glatt statt matschig.
    fcfg.OversampleH = 2;
    fcfg.OversampleV = 2;
    // Cyrillic-Ranges = Default + Umlaute (0x20-0xFF) + kyrillisch — deckt
    // alle deutschen Labels ab ("Groesse", "Staerke", "Zielhoehe", ...).
    const ImWchar* ranges = io.Fonts->GetGlyphRangesCyrillic();

    g_fontIcon   = io.Fonts->AddFontFromMemoryTTF((void*)Icons, sizeof(Icons), 18.f, &fcfg, ranges);
    g_fontHeader = io.Fonts->AddFontFromMemoryTTF((void*)LexendDeca, sizeof(LexendDeca), 20.f, &fcfg, ranges);
    g_fontBody   = io.Fonts->AddFontFromMemoryTTF((void*)Intermedium, sizeof(Intermedium), 16.f, &fcfg, ranges);
    io.FontDefault = g_fontBody;

#ifdef _WIN32
    LoadMenuAssets(device);
#else
    LoadMenuAssets();
#endif
    ui::RegisterMenuModel();
    ApplyMenuThemeFromConfig();
}

// ── RenderMenu ───────────────────────────────────────────────────────────────
void RenderMenu() {
    using namespace ui;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y),
                             ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.f);
    ImGuiWindowFlags wf = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoScrollbar
                        | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoMove
                        | ImGuiWindowFlags_NoBringToFrontOnFocus;
    if (!ImGui::Begin("##pms_gui", nullptr, wf)) {
        ImGui::End();
        ImGui::PopStyleVar();
        return;
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGuiIO& io = ImGui::GetIO();
    s_dt = io.DeltaTime > 0.f ? io.DeltaTime : 1.f / 60.f;
    if (s_dt > 0.1f) s_dt = 0.1f;

    g_menuScale = ScaleFit();
    RegisterMenuModel();  // ui:: dank using-declaration oben

    // Fenster zentriert
    const float winW = Px(layout::WinW), winH = Px(layout::WinH);
    const ImVec2 ds = io.DisplaySize;
    s_winMin = ImVec2((ds.x - winW) * 0.5f, (ds.y - winH) * 0.5f);
    s_winMax = ImVec2(s_winMin.x + winW, s_winMin.y + winH);

    DrawWindow(dl);
    RenderSidebar(dl);

    // Grid/Content-Bereich rechts der Sidebar
    const ImVec2 gmin(s_winMin.x + Px(layout::SidebarW), s_winMin.y);
    const ImVec2 gmax(s_winMax.x, s_winMax.y);

    // Settings-Panel-Alpha treiben
    const bool panelWanted = s_view == View::Modules && s_selected >= 0;
    s_settingsA = HoverTowards(s_settingsA, panelWanted ? 1.f : 0.f, s_dt, 220.f);

    switch (s_view) {
    case View::Modules:
        RenderGrid(dl, gmin, gmax);
        if (s_settingsA > 0.01f && panelWanted) {
            RenderSettingsPanel(dl);
            // Rechtsklick auf die Karte schliesst das Panel wieder (Toggle im RenderGrid)
        }
        break;
    case View::Search:
        s_selected = -1;
        RenderSearch(dl, gmin, gmax);
        break;
    case View::HudEditor:
        s_selected = -1;
        RenderHudEditor(dl, gmin, gmax);
        break;
    case View::Configs:
        s_selected = -1;
        RenderConfigs(dl, gmin, gmax);
        break;
    case View::Client:
        s_selected = -1;
        RenderClient(dl, gmin, gmax);
        break;
    }

    ImGui::End();
    ImGui::PopStyleVar();
}
