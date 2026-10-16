#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// ui/MenuShared.h — gemeinsamer Umbrella-Header für die UI
// Bündelt alle Kerntypen/Module, die Menü + Features gemeinsam nutzen.
// (Die Schemas/Offsets leben jetzt in features/Offsets.h, das Config-Struct
//  in core/Config.h, die Global-Externs in core/Globals.h.)
// ─────────────────────────────────────────────────────────────────────────────
#include "pch.h"
#include "../memory.h"
#include "../core/SDK.h"
#include "../core/Globals.h"
#include "../core/Config.h"
#include "../features/Offsets.h"
#include "../features/Cache.h"
#include "../features/Weapons.h"

// ─────────────────────────────────────────────────────────────────────────────
// Knives (Definition in ui/MenuWidgets.cpp)
// ─────────────────────────────────────────────────────────────────────────────
namespace knives {
    constexpr int knifeCount = 18;
    extern const char* names[];
    extern int values[];
    constexpr bool IsKnife(uint16_t idx) { return idx >= 500 && idx <= 525; }
}
