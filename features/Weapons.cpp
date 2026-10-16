#include "pch.h"
#include "Weapons.h"
#include "Offsets.h"

// ─────────────────────────────────────────────────────────────────────────────
// features/Weapons.cpp — Waffen-zu-SVG-Zuordnung (ESP-Icons)
// ─────────────────────────────────────────────────────────────────────────────
namespace {
    // Muss nach features/knives.hpp ausgerichtet sein — Knives werden von der
    // UI (MenuShared.h) definiert; hier nur der SVG-Name.
    inline bool IsKnifeId(int wid) { return wid >= 500 && wid <= 525; }
}

// Waffen-ID -> SVG-Datei (Gun_textures\)
// Rueckgabe nullptr, wenn kein Icon existiert (Fallback: Text-Name)
const char* WeaponIdToSvgFile(int wid) {
    if (IsKnifeId(wid)) return "knife.svg";
    switch (wid) {
    case weapons::WEAPON_DEAGLE:  return "deagle.svg";
    case weapons::WEAPON_ELITE:   return "elite.svg";
    case weapons::WEAPON_FIVESEVEN: return "fiveseven.svg";
    case weapons::WEAPON_GLOCK:   return "glock.svg";
    case weapons::WEAPON_AK47:    return "ak47.svg";
    case weapons::WEAPON_AUG:     return "aug.svg";
    case weapons::WEAPON_AWP:     return "awp.svg";
    case weapons::WEAPON_FAMAS:   return "famas.svg";
    case weapons::WEAPON_G3SG1:   return "g3sg1.svg";
    case weapons::WEAPON_GALILAR: return "galilar.svg";
    case weapons::WEAPON_M249:    return "m249.svg";
    case weapons::WEAPON_M4A1:    return "m4a1_silencer.svg";
    case weapons::WEAPON_M4A4:    return "m4a1.svg";
    case weapons::WEAPON_MAC10:   return "mac10.svg";
    case weapons::WEAPON_P90:     return "p90.svg";
    case weapons::WEAPON_MP5SD:   return "mp5sd.svg";
    case weapons::WEAPON_UMP45:   return "ump45.svg";
    case weapons::WEAPON_XM1014:  return "xm1014.svg";
    case weapons::WEAPON_BIZON:   return "bizon.svg";
    case weapons::WEAPON_MAG7:    return "mag7.svg";
    case weapons::WEAPON_NEGEV:   return "negev.svg";
    case weapons::WEAPON_SAWEDOFF: return "sawedoff.svg";
    case weapons::WEAPON_TEC9:    return "tec9.svg";
    case weapons::WEAPON_ZEUS:    return "taser.svg";
    case weapons::WEAPON_P250:    return "p250.svg";
    case weapons::WEAPON_MP7:     return "mp7.svg";
    case weapons::WEAPON_MP9:     return "mp9.svg";
    case weapons::WEAPON_NOVA:    return "nova.svg";
    case weapons::WEAPON_P2000:   return "p2000.svg";
    case weapons::WEAPON_R8:      return "revolver.svg";
    case weapons::WEAPON_SCAR20:  return "scar20.svg";
    case weapons::WEAPON_SG556:   return "sg556.svg";
    case weapons::WEAPON_SSG08:   return "ssg08.svg";
    case weapons::WEAPON_USP_SILENCER: return "usp_silencer.svg";
    default: return nullptr;
    }
}