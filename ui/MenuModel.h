#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// ui/MenuModel.h — statisches Feature-Modell (Kategorie → Modul → Settings)
// Ersetzt die alten Tab-Funktionen: Menu.cpp rendert generisch aus dieser
// Tabelle, die Suche filtert sie, der Grid-View zeigt Module als Karten.
// Alle Zeiger zeigen direkt auf g_cfg — kein zweiter State.
// ─────────────────────────────────────────────────────────────────────────────
#include "pch.h"
#include <vector>

namespace ui {

enum class SettingType : int { Bool, Float, Int, Choice, Color, Key };

struct SettingDef {
    SettingType        type;
    const char*        label;
    void*              ptr;        // bool* / float* / int* / float[4]*
    float              mn = 0.f, mx = 1.f;
    const char*        fmt = "%.1f";
    const char* const* options = nullptr; // Choice
    int                optCount = 0;
    int                keyTarget = 0;      // Key (1=triggerbot, 2=aim)
};

struct ModuleDef {
    const char*              name;
    const char*              desc;
    bool*                    enabled;       // Master-Flag in g_cfg
    std::vector<SettingDef>  settings;
};

struct CategoryDef {
    const char*             name;
    const char*             icon;           // Icon-Font-Glyph
    std::vector<ModuleDef>  modules;
};

// Aufgebaut einmal in RegisterMenuModel() (idempotent).
const std::vector<CategoryDef>& MenuModel();
void RegisterMenuModel();

// Suche: flache Liste aller Settings mit Modul-Name (fuer den Search-View).
struct SearchHit {
    int cat, mod, set;
};
void SearchSettings(const char* query, std::vector<SearchHit>& out);

}  // namespace ui
