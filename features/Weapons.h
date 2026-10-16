#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// features/Weapons.h — Waffen-Datenbank (IDs, Namen, Recoil-Relevanz)
// Header-only (inline), damit auch die UI ohne Zusatz-Link alles hat.
// ─────────────────────────────────────────────────────────────────────────────

// Waffen-ID -> SVG-Datei (Gun_textures\). Rueckgabe nullptr = kein Icon
// (Fallback: Text-Name). Definition in features/Weapons.cpp.
const char* WeaponIdToSvgFile(int wid);

namespace weapons {
    // Weapon IDs (Item Definition Index)
    enum WeaponID : int {
        WEAPON_DEAGLE = 1,
        WEAPON_ELITE = 2,
        WEAPON_FIVESEVEN = 3,
        WEAPON_GLOCK = 4,
        WEAPON_AK47 = 7,
        WEAPON_AUG = 8,
        WEAPON_AWP = 9,
        WEAPON_FAMAS = 10,
        WEAPON_G3SG1 = 11,
        WEAPON_GALILAR = 13,
        WEAPON_M249 = 14,
        WEAPON_M4A1 = 16,
        WEAPON_M4A4 = 60,
        WEAPON_MAC10 = 17,
        WEAPON_P90 = 19,
        WEAPON_MP5SD = 23,
        WEAPON_UMP45 = 24,
        WEAPON_XM1014 = 25,
        WEAPON_BIZON = 26,
        WEAPON_MAG7 = 28,
        WEAPON_NEGEV = 29,
        WEAPON_SAWEDOFF = 30,
        WEAPON_TEC9 = 33,
        WEAPON_ZEUS = 34,
        WEAPON_P250 = 36,
        WEAPON_MP7 = 61,
        WEAPON_MP9 = 62,
        WEAPON_NOVA = 35,
        WEAPON_P2000 = 64,
        WEAPON_R8 = 72,
        WEAPON_SCAR20 = 38,
        WEAPON_SG556 = 39,
        WEAPON_SSG08 = 40,
        WEAPON_USP_SILENCER = 63,
        WEAPON_CZ75A = 63,  // same as USP
        WEAPON_KNIFE = 42,
        WEAPON_KNIFE_T = 59,
        WEAPON_FLASHBANG = 43,
        WEAPON_HEGRENADE = 44,
        WEAPON_SMOKEGRENADE = 45,
        WEAPON_MOLOTOV = 46,
        WEAPON_DECOY = 47,
        WEAPON_INCGRENADE = 48,
        WEAPON_C4 = 49,
    };

    inline bool IsGrenade(int weaponId) {
        switch (weaponId) {
        case WEAPON_FLASHBANG:
        case WEAPON_HEGRENADE:
        case WEAPON_SMOKEGRENADE:
        case WEAPON_MOLOTOV:
        case WEAPON_DECOY:
        case WEAPON_INCGRENADE:
            return true;
        default:
            return false;
        }
    }

    inline const char* GetWeaponName(int weaponId) {
        switch (weaponId) {
        case WEAPON_DEAGLE: return "Deagle";
        case WEAPON_ELITE: return "Dual Berettas";
        case WEAPON_FIVESEVEN: return "Five-SeveN";
        case WEAPON_GLOCK: return "Glock-18";
        case WEAPON_AK47: return "AK-47";
        case WEAPON_AUG: return "AUG";
        case WEAPON_AWP: return "AWP";
        case WEAPON_FAMAS: return "FAMAS";
        case WEAPON_G3SG1: return "G3SG1";
        case WEAPON_GALILAR: return "Galil AR";
        case WEAPON_M249: return "M249";
        case WEAPON_M4A1: return "M4A1-S";
        case WEAPON_M4A4: return "M4A4";
        case WEAPON_MAC10: return "MAC-10";
        case WEAPON_P90: return "P90";
        case WEAPON_MP5SD: return "MP5-SD";
        case WEAPON_UMP45: return "UMP-45";
        case WEAPON_XM1014: return "XM1014";
        case WEAPON_BIZON: return "PP-Bizon";
        case WEAPON_MAG7: return "MAG-7";
        case WEAPON_NEGEV: return "Negev";
        case WEAPON_SAWEDOFF: return "Sawed-Off";
        case WEAPON_TEC9: return "Tec-9";
        case WEAPON_ZEUS: return "Zeus x27";
        case WEAPON_P250: return "P250";
        case WEAPON_MP7: return "MP7";
        case WEAPON_MP9: return "MP9";
        case WEAPON_NOVA: return "Nova";
        case WEAPON_P2000: return "P2000";
        case WEAPON_R8: return "R8 Revolver";
        case WEAPON_SCAR20: return "SCAR-20";
        case WEAPON_SG556: return "SG 553";
        case WEAPON_SSG08: return "SSG 08";
        case WEAPON_USP_SILENCER: return "USP-S";
        case WEAPON_KNIFE:
        case WEAPON_KNIFE_T: return "Knife";
        case WEAPON_FLASHBANG: return "Flashbang";
        case WEAPON_HEGRENADE: return "HE Grenade";
        case WEAPON_SMOKEGRENADE: return "Smoke";
        case WEAPON_MOLOTOV: return "Molotov";
        case WEAPON_DECOY: return "Decoy";
        case WEAPON_INCGRENADE: return "Incendiary";
        case WEAPON_C4: return "C4";
        default: return "Unknown";
        }
    }

    inline bool HasRecoilPattern(int weaponId) {
        switch (weaponId) {
        case WEAPON_AK47:
        case WEAPON_M4A4:
        case WEAPON_M4A1:
        case WEAPON_AUG:
        case WEAPON_FAMAS:
        case WEAPON_GALILAR:
        case WEAPON_SG556:
        case WEAPON_M249:
        case WEAPON_NEGEV:
        case WEAPON_AWP:
        case WEAPON_SCAR20:
        case WEAPON_G3SG1:
        case WEAPON_MP5SD:
        case WEAPON_MP7:
        case WEAPON_MP9:
        case WEAPON_UMP45:
        case WEAPON_P90:
        case WEAPON_BIZON:
        case WEAPON_MAC10:
        case WEAPON_P250:
        case WEAPON_DEAGLE:
        case WEAPON_R8:
        case WEAPON_FIVESEVEN:
        case WEAPON_GLOCK:
        case WEAPON_TEC9:
        case WEAPON_P2000:
        case WEAPON_USP_SILENCER:
        case WEAPON_ELITE:
            return true;
        default:
            return false;
        }
    }
}