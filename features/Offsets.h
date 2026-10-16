#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// features/Offsets.h — cs2-dumper Offsets & Schemas
//
// Alle Werte werden ueber offsetsupdater::Register() an echte Variablen
// gebunden. Beim Start wird der Cache (offsets_cache.txt) geladen und sofort
// auf die Variablen angewendet. Der Hintergrund-Download (cdn.jsdelivr.net /
// raw.githubusercontent) ueberschreibt Cache + Live-Variablen direkt — kein
// Neustart mehr noetig.
// Fallback-Werte gelten nur wenn kein Cache und kein Download vorhanden ist.
// ─────────────────────────────────────────────────────────────────────────────
#include <cstdint>
#include "../core/OffsetsUpdater.h"

namespace schemas {
    inline uintptr_t m_pGameSceneNode;
    inline uintptr_t m_iHealth;
    inline uintptr_t m_lifeState;
    inline uintptr_t m_iTeamNum;
    inline uintptr_t m_vecAbsOrigin;
    inline uintptr_t m_hPlayerPawn;
    inline uintptr_t m_sSanitizedPlayerName;
    inline uintptr_t m_iPawnHealth;
    inline uintptr_t m_bPawnIsAlive;
    inline uintptr_t m_hOriginalControllerOfCurrentPawn;
    inline uintptr_t m_entitySpottedState;
    inline uintptr_t m_bSpottedByMask;
    inline uintptr_t m_bBombTicking;
    inline uintptr_t m_nBombSite;
    inline uintptr_t m_flC4Blow;
    inline uintptr_t m_bCannotBeDefused;
    inline uintptr_t m_bHasExploded;
    inline uintptr_t m_flTimerLength;
    inline uintptr_t m_bBeingDefused;
    inline uintptr_t m_bC4Activated;
    inline uintptr_t m_flDefuseLength;
    inline uintptr_t m_flDefuseCountDown;
    inline uintptr_t m_bBombDefused;
    inline uintptr_t m_pWeaponServices;
    inline uintptr_t m_bInBombZone;
    inline uintptr_t m_bPawnHasDefuser;
    inline uintptr_t m_AttributeManager;
    inline uintptr_t m_nFallbackPaintKit;
    inline uintptr_t m_nFallbackSeed;
    inline uintptr_t m_flFallbackWear;
    inline uintptr_t m_nFallbackStatTrak;
    inline uintptr_t m_nUnloadedModelIndex;
    inline uintptr_t m_Item;
    inline uintptr_t m_iItemDefinitionIndex;
    inline uintptr_t m_iEntityQuality;
    inline uintptr_t m_iEntityLevel;
    inline uintptr_t m_iItemID;
    inline uintptr_t m_iItemIDHigh;
    inline uintptr_t m_iItemIDLow;
    inline uintptr_t m_iAccountID;
    inline uintptr_t m_bInitialized;
    inline uintptr_t m_bClearWeaponIdentifyingUGC;
    inline uintptr_t m_bVisualsDataSet;
    inline uintptr_t m_hActiveWeapon;
    inline uintptr_t m_pCameraServices;
    inline uintptr_t m_pAimPunchServices;
    inline uintptr_t m_pMovementServices;
    inline uintptr_t m_iShotsFired;
    inline uintptr_t m_fFlags;
    inline uintptr_t m_vecAbsVelocity;
    inline uintptr_t m_angEyeAngles;
    inline uintptr_t m_angEyeAnglesVelocity;
    inline uintptr_t m_vecCsViewPunchAngle;
    inline uintptr_t m_predictableBaseAngle;
    inline uintptr_t m_predictableBaseAngleVel;
    inline uintptr_t m_unpredictableBaseAngle;
    inline uintptr_t m_flNextAttack;
    inline uintptr_t m_steamID;
    inline uintptr_t m_modelState;
    inline uintptr_t m_hBombDefuser;
    inline uintptr_t m_pItemServices;
    inline uintptr_t m_bHasDefuser;
    inline uintptr_t m_flThrowStrength;
    inline uintptr_t m_bPinPulled;
    // Util-Radius: Smoke-Volumen + Inferno-Flammen (Stand 01.10.2026)
    inline uintptr_t m_vSmokeDetonationPos; // C_SmokeGrenadeProjectile 0x1378
    inline uintptr_t m_bDidSmokeEffect;     // C_SmokeGrenadeProjectile 0x1364
    inline uintptr_t m_firePositions;  // C_Inferno 0x1108 (VectorWS[64])
    inline uintptr_t m_fireCount;      // C_Inferno 0x1A48
    // Granaten-Projektile (In-Flight-Tracking, a2x-Dump 15.10.2026):
    inline uintptr_t m_flDetonateTime;   // C_BaseGrenade 0x1270 (GameTime_t, absolut)
    inline uintptr_t m_hThrower;         // C_BaseGrenade 0x1290 (CHandle<C_CSPlayerPawn>)
    inline uintptr_t m_vInitialPosition; // C_BaseCSGrenadeProjectile 0x12B0
    inline uintptr_t m_vInitialVelocity; // C_BaseCSGrenadeProjectile 0x12BC
    inline uintptr_t m_nBounces;         // C_BaseCSGrenadeProjectile 0x12C8
    inline uintptr_t m_flSpawnTime;      // C_BaseCSGrenadeProjectile 0x12E8
    inline uintptr_t m_pCollision;
    inline uintptr_t m_vecMins;
    inline uintptr_t m_vecMaxs;
    inline uintptr_t m_bDormant; // CGameSceneNode (0x103) — Cull für Reader
    // Observer-Kette: Pawn + m_pObserverServices -> Services + Target/Mode
    inline uintptr_t m_pObserverServices; // C_BasePlayerPawn 0x1308
    inline uintptr_t m_hObserverTarget; // relativ zu ObserverServices! 0x4C
    inline uintptr_t m_iObserverMode;   // relativ zu ObserverServices! 0x48
    // UC-Fix (Big-Update): Observer-Pawn extra über Controller auflösen —
    // der tote Pawn wird NICHT mehr automatisch zum Observer-Pawn.
    inline uintptr_t m_hObserverPawn;   // CCSPlayerController 0x918
    inline uintptr_t m_hPawn;           // CCSPlayerController 0x6BC (alias m_hPlayerPawn)

    // ── UC Seed-Triggerbot (Crosshair-ID + Spread-Gate, external) ──────────
    // m_iIDEntIndex: Server-Trace unter Fadenkreuz (C_CSPlayerPawn, 0x36CC @ 01.10.2026)
    inline uintptr_t m_iIDEntIndex;
    // m_fAccuracyPenalty: akkumulierte Ungenauigkeit der Waffe (C_CSWeaponBase)
    inline uintptr_t m_fAccuracyPenalty;
    inline uintptr_t m_bIsScoped;
    inline uintptr_t m_bInReload;
    inline uintptr_t m_iClip1;
    inline uintptr_t m_flRecoilIndex;
    // CS2 Target-ID-Delay/Hold (CountdownTimer, UC-Fund 16.10.2026):
    // Die Crosshair-ID erscheint verzögert und bleibt nach dem Verlassen des
    // Ziels kurz gültig. Timestamp liegt bei +0x4 im Timer-Struct.
    inline uintptr_t m_delayTargetIDTimer; // C_CSPlayerPawn 0x36E0
    inline uintptr_t m_holdTargetIDTimer;  // C_CSPlayerPawn 0x3700

    // ── Chams/Glow (UC-Standard externe Glow-Properties) + Leichen-Origin ───
    inline uintptr_t m_vOldOrigin;                  // C_BasePlayerPawn (Fußpunkt, auch tot)
    inline uintptr_t m_Glow;                        // C_BaseModelEntity (Basis der CGlowProperty)
    inline uintptr_t m_iGlowType;                   // CGlowProperty
    inline uintptr_t m_glowColorOverride;           // CGlowProperty (RGBA-Bytes)
    inline uintptr_t m_bEligibleForScreenHighlight; // CGlowProperty
    inline uintptr_t m_bGlowing;                    // CGlowProperty

    // Nicht im Schema: Bone-Array-Pointer relativ zu CModelState
    constexpr uintptr_t kBoneArrayInModelState = 0x80;

    constexpr auto m_pEntityInstance = 0x00;

    namespace detail {
        inline bool _registered = []() -> bool {
            offsetsupdater::Register("m_pGameSceneNode", 0x330, &m_pGameSceneNode);
            offsetsupdater::Register("m_iHealth", 0x34C, &m_iHealth);
            offsetsupdater::Register("m_lifeState", 0x354, &m_lifeState);
            offsetsupdater::Register("m_iTeamNum", 0x3E7, &m_iTeamNum);
            offsetsupdater::Register("m_vecAbsOrigin", 0xC8, &m_vecAbsOrigin);
            // Fallbacks = a2x/cs2-dumper Stand 06.10.2026 (Game-Build 14189;
            // Schemas unverändert seit 01.10., dw-Adressen +0x2000).
            // Kollisionen (Spotted/C4, AttributeManager/Chicken) zusätzlich
            // per Klassen-Override in OffsetsUpdater.cpp abgesichert.
            offsetsupdater::Register("m_hPlayerPawn", 0x92C, &m_hPlayerPawn);
            offsetsupdater::Register("m_sSanitizedPlayerName", 0x878, &m_sSanitizedPlayerName);
            offsetsupdater::Register("m_iPawnHealth", 0x938, &m_iPawnHealth);
            offsetsupdater::Register("m_bPawnIsAlive", 0x934, &m_bPawnIsAlive);
            offsetsupdater::Register("m_hOriginalControllerOfCurrentPawn", 0x950, &m_hOriginalControllerOfCurrentPawn);
            // m_entitySpottedState: Klassen-Override (C_CSPlayerPawn) in
            // OffsetsUpdater.cpp; Fallback = klassen-korrekter Wert (15.10.2026).
            offsetsupdater::Register("m_entitySpottedState", 0x1E80, &m_entitySpottedState);
            offsetsupdater::Register("m_bSpottedByMask", 0xC, &m_bSpottedByMask);
            offsetsupdater::Register("m_bBombTicking", 0x1288, &m_bBombTicking);
            offsetsupdater::Register("m_nBombSite", 0x128C, &m_nBombSite);
            offsetsupdater::Register("m_flC4Blow", 0x12B8, &m_flC4Blow);
            offsetsupdater::Register("m_bCannotBeDefused", 0x12BC, &m_bCannotBeDefused);
            offsetsupdater::Register("m_bHasExploded", 0x12BD, &m_bHasExploded);
            offsetsupdater::Register("m_flTimerLength", 0x12C0, &m_flTimerLength);
            offsetsupdater::Register("m_bBeingDefused", 0x12C4, &m_bBeingDefused);
            offsetsupdater::Register("m_bC4Activated", 0x12D0, &m_bC4Activated);
            offsetsupdater::Register("m_flDefuseLength", 0x12D4, &m_flDefuseLength);
            offsetsupdater::Register("m_flDefuseCountDown", 0x12D8, &m_flDefuseCountDown);
            offsetsupdater::Register("m_bBombDefused", 0x12DC, &m_bBombDefused);
            offsetsupdater::Register("m_pWeaponServices", 0x12F0, &m_pWeaponServices);
            offsetsupdater::Register("m_bInBombZone", 0x15E9, &m_bInBombZone);
            offsetsupdater::Register("m_bPawnHasDefuser", 0x940, &m_bPawnHasDefuser);
            offsetsupdater::Register("m_AttributeManager", 0x1290, &m_AttributeManager);
            offsetsupdater::Register("m_nFallbackPaintKit", 0x18A8, &m_nFallbackPaintKit);
            offsetsupdater::Register("m_nFallbackSeed", 0x18AC, &m_nFallbackSeed);
            offsetsupdater::Register("m_flFallbackWear", 0x18B0, &m_flFallbackWear);
            offsetsupdater::Register("m_nFallbackStatTrak", 0x18B4, &m_nFallbackStatTrak);
            offsetsupdater::Register("m_nUnloadedModelIndex", 0x18E4, &m_nUnloadedModelIndex);
            offsetsupdater::Register("m_Item", 0x50, &m_Item);
            offsetsupdater::Register("m_iItemDefinitionIndex", 0x1BA, &m_iItemDefinitionIndex);
            offsetsupdater::Register("m_iEntityQuality", 0x1BC, &m_iEntityQuality);
            offsetsupdater::Register("m_iEntityLevel", 0x1C0, &m_iEntityLevel);
            offsetsupdater::Register("m_iItemID", 0x1C8, &m_iItemID);
            offsetsupdater::Register("m_iItemIDHigh", 0x1D0, &m_iItemIDHigh);
            offsetsupdater::Register("m_iItemIDLow", 0x1D4, &m_iItemIDLow);
            offsetsupdater::Register("m_iAccountID", 0x1D8, &m_iAccountID);
            offsetsupdater::Register("m_bInitialized", 0x1E8, &m_bInitialized);
            offsetsupdater::Register("m_bClearWeaponIdentifyingUGC", 0x1B20, &m_bClearWeaponIdentifyingUGC);
            offsetsupdater::Register("m_bVisualsDataSet", 0x1B21, &m_bVisualsDataSet);
            offsetsupdater::Register("m_hActiveWeapon", 0x60, &m_hActiveWeapon);
            offsetsupdater::Register("m_pCameraServices", 0x1328, &m_pCameraServices);
            offsetsupdater::Register("m_pAimPunchServices", 0x1598, &m_pAimPunchServices);
            offsetsupdater::Register("m_pMovementServices", 0x1330, &m_pMovementServices);
            // Stand 15.10.2026 (a2x): Schemas um +0x10 verschoben (m_angEyeAngles
            // 0x35F0->0x3600 usw.) — alte Werte lieferten "eingefrorene" Winkel!
            offsetsupdater::Register("m_iShotsFired", 0x1EB0, &m_iShotsFired);
            offsetsupdater::Register("m_fFlags", 0x3F4, &m_fFlags);
            offsetsupdater::Register("m_vecAbsVelocity", 0x3F8, &m_vecAbsVelocity);
            offsetsupdater::Register("m_angEyeAngles", 0x3600, &m_angEyeAngles);
            offsetsupdater::Register("m_angEyeAnglesVelocity", 0x36D0, &m_angEyeAnglesVelocity);
            offsetsupdater::Register("m_vecCsViewPunchAngle", 0x48, &m_vecCsViewPunchAngle);
            offsetsupdater::Register("m_predictableBaseAngle", 0x50, &m_predictableBaseAngle);
            offsetsupdater::Register("m_predictableBaseAngleVel", 0x5C, &m_predictableBaseAngleVel);
            offsetsupdater::Register("m_unpredictableBaseAngle", 0xA4, &m_unpredictableBaseAngle);
            offsetsupdater::Register("m_flNextAttack", 0xD0, &m_flNextAttack);
            offsetsupdater::Register("m_steamID", 0x788, &m_steamID);
            offsetsupdater::Register("m_modelState", 0x140, &m_modelState);
            offsetsupdater::Register("m_hBombDefuser", 0x12E0, &m_hBombDefuser);
            offsetsupdater::Register("m_pItemServices", 0x12F8, &m_pItemServices);
            offsetsupdater::Register("m_bHasDefuser", 0x48, &m_bHasDefuser);
            // C_BaseCSGrenade (Stand 01.10.2026) — alte 0x1CF0/0x1CE3 waren falsch
            offsetsupdater::Register("m_flThrowStrength", 0x1F30, &m_flThrowStrength);
            offsetsupdater::Register("m_bPinPulled", 0x1F23, &m_bPinPulled);
            // Granaten-Projektile (a2x-Dump 15.10.2026) — exakte Ballistik + Zünder
            offsetsupdater::Register("m_flDetonateTime", 0x1270, &m_flDetonateTime);
            offsetsupdater::Register("m_hThrower", 0x1290, &m_hThrower);
            offsetsupdater::Register("m_vInitialPosition", 0x12B0, &m_vInitialPosition);
            offsetsupdater::Register("m_vInitialVelocity", 0x12BC, &m_vInitialVelocity);
            offsetsupdater::Register("m_nBounces", 0x12C8, &m_nBounces);
            offsetsupdater::Register("m_flSpawnTime", 0x12E8, &m_flSpawnTime);
            offsetsupdater::Register("m_vSmokeDetonationPos", 0x1378, &m_vSmokeDetonationPos);
            offsetsupdater::Register("m_bDidSmokeEffect", 0x1364, &m_bDidSmokeEffect);
            offsetsupdater::Register("m_firePositions", 0x1108, &m_firePositions);
            offsetsupdater::Register("m_fireCount", 0x1A48, &m_fireCount);
            offsetsupdater::Register("m_pCollision", 0x340, &m_pCollision);
            offsetsupdater::Register("m_vecMins", 0x40, &m_vecMins);
            offsetsupdater::Register("m_vecMaxs", 0x4C, &m_vecMaxs);
            offsetsupdater::Register("m_bDormant", 0x103, &m_bDormant);
            offsetsupdater::Register("m_pObserverServices", 0x1308, &m_pObserverServices);
            offsetsupdater::Register("m_hObserverTarget", 0x4C, &m_hObserverTarget);
            offsetsupdater::Register("m_iObserverMode", 0x48, &m_iObserverMode);
            offsetsupdater::Register("m_hObserverPawn", 0x930, &m_hObserverPawn);
            offsetsupdater::Register("m_hPawn", 0x6BC, &m_hPawn);
            // UC Seed-Triggerbot: Fallbacks Stand 15.10.2026 (a2x/cs2-dumper),
            // werden beim Start per Auto-Refresh live korrigiert.
            offsetsupdater::Register("m_iIDEntIndex", 0x36DC, &m_iIDEntIndex);
            offsetsupdater::Register("m_fAccuracyPenalty", 0x1A18, &m_fAccuracyPenalty);
            offsetsupdater::Register("m_bIsScoped", 0x1E98, &m_bIsScoped);
            offsetsupdater::Register("m_bInReload", 0x1A3C, &m_bInReload);
            offsetsupdater::Register("m_iClip1", 0x1928, &m_iClip1);
            offsetsupdater::Register("m_flRecoilIndex", 0x1A28, &m_flRecoilIndex);
            // UC-Fund 16.10.2026 (a2x-Dump): Target-ID-Timer des Pawns.
            // Namen sind im Dump eindeutig (kein Klassen-Override nötig).
            offsetsupdater::Register("m_delayTargetIDTimer", 0x36E0, &m_delayTargetIDTimer);
            offsetsupdater::Register("m_holdTargetIDTimer", 0x3700, &m_holdTargetIDTimer);
            // Chams/Glow + Leichen-Bodenproben. Fallbacks = a2x-Abgleich
            // 16.10.2026 (stimmt mit injoff/GameOffsets.h überein); Namen sind
            // im Dump eindeutig (kein Klassen-Override nötig).
            offsetsupdater::Register("m_vOldOrigin", 0x14A4, &m_vOldOrigin);
            offsetsupdater::Register("m_Glow", 0xDE8, &m_Glow);
            offsetsupdater::Register("m_iGlowType", 0x30, &m_iGlowType);
            offsetsupdater::Register("m_glowColorOverride", 0x40, &m_glowColorOverride);
            offsetsupdater::Register("m_bEligibleForScreenHighlight", 0x50, &m_bEligibleForScreenHighlight);
            offsetsupdater::Register("m_bGlowing", 0x51, &m_bGlowing);
            return true;
        }();
    }
}

namespace offsets {
    inline uintptr_t dwEntityList;
    inline uintptr_t dwLocalPlayerController;
    inline uintptr_t dwLocalPlayerPawn;
    inline uintptr_t dwViewMatrix;
    inline uintptr_t dwPlantedC4;
    inline uintptr_t dwGlobalVars;
    inline uintptr_t dwSensitivity;
    inline uintptr_t dwSensitivity_sensitivity;
    inline uintptr_t dwCSGOInput;
    inline uintptr_t dwViewAngles;
    // Bhop-Button (cs2-dumper output/buttons.hpp — UC-Standard: 65537=+jump)
    inline uintptr_t dwJumpButton;

    namespace detail {
        inline bool _registered = []() -> bool {
            // Fallbacks = a2x/cs2-dumper Stand 15.10.2026 (alle dw um ~+0x9000
            // verschoben vs. 09.10.). SigScan lockt dwViewMatrix/dwEntityList
            // ggf. auf live verifizierte Werte — gelockte fasst der Updater
            // nicht an; alle anderen korrigiert der Auto-Refresh beim Start.
            offsetsupdater::Register("dwEntityList", 0x271A908, &dwEntityList);
            offsetsupdater::Register("dwLocalPlayerController", 0x253CB78, &dwLocalPlayerController);
            offsetsupdater::Register("dwLocalPlayerPawn", 0x2565718, &dwLocalPlayerPawn);
            offsetsupdater::Register("dwViewMatrix", 0x256B020, &dwViewMatrix);
            offsetsupdater::Register("dwPlantedC4", 0x24CB3C0, &dwPlantedC4);
            offsetsupdater::Register("dwGlobalVars", 0x2230090, &dwGlobalVars);
            offsetsupdater::Register("dwSensitivity", 0x2562A18, &dwSensitivity);
            offsetsupdater::Register("dwSensitivity_sensitivity", 0x58, &dwSensitivity_sensitivity);
            // 15.10.: CSGOInput 0x257B640; ViewAngles = CSGOInput + 0x688.
            offsetsupdater::Register("dwCSGOInput", 0x257B640, &dwCSGOInput);
            offsetsupdater::Register("dwViewAngles", 0x257BCC8, &dwViewAngles);
            // buttons.hpp (Stand 15.10.2026: jump=0x22343E0)
            offsetsupdater::Register("jump", 0x22343E0, &dwJumpButton);
            return true;
        }();
    }
}

// Sicherstellen dass die statischen Registrierungen vor dem ersten Refresh
// ausgefuehrt wurden (gegen undefinierte TU-Initialisierungsreihenfolge).
inline void EnsureOffsetsRegistered() {
    (void)schemas::detail::_registered;
    (void)offsets::detail::_registered;
}
