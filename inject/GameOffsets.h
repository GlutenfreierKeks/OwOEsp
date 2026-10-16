#pragma once
// inject/GameOffsets.h — EIN Ort für alle internen Offsets.
// Quelle: Downloads\output\offsets.json + client_dll.json (Dump 09.10. 18:35).
// Bei CS2-Update: Werte aus frischem Dump hier nachziehen (Chams.cpp und
// OverlayESP.cpp nutzen beide diese Datei).
#include <cstdint>

namespace injoff {
constexpr uintptr_t kDwEntityList = 0x2711598;
constexpr uintptr_t kDwLocalPlayerController = 0x25338A8;
constexpr uintptr_t kDwViewMatrix = 0x2561CD0;
constexpr uintptr_t kMHPlayerPawn = 0x92C; // CCSPlayerController::m_hPlayerPawn
constexpr uintptr_t kMIHealth = 0x34C;     // C_BaseEntity::m_iHealth
constexpr uintptr_t kMITeamNum = 0x3E7;     // C_BaseEntity::m_iTeamNum (uint8!)
constexpr uintptr_t kMVOldOrigin = 0x14A4;  // C_BasePlayerPawn::m_vOldOrigin
constexpr uintptr_t kMName = 0x878; // Controller: m_sSanitizedPlayerName
constexpr uintptr_t kMGlow = 0xDE8;        // C_BaseModelEntity::m_Glow
constexpr uintptr_t kGlowType = 0x30;      // CGlowProperty::m_iGlowType
constexpr uintptr_t kGlowColor = 0x40;     // CGlowProperty::m_glowColorOverride
constexpr uintptr_t kGlowOn = 0x51;        // CGlowProperty::m_bGlowing
constexpr uintptr_t kMClrRender = 0xCA0;   // C_BaseModelEntity::m_clrRender
constexpr uintptr_t kMRenderMode = 0xC80;  // C_BaseEntity::m_nRenderMode
constexpr uintptr_t kMFlags = 0x3F4;       // C_BaseEntity::m_fFlags
constexpr uintptr_t kMEyeAngles = 0x3600;  // C_CSPlayerPawn::m_angEyeAngles
constexpr uintptr_t kMCameraSvc = 0x1328;  // C_BasePlayerPawn::m_pCameraServices
constexpr uintptr_t kMFov = 0x298;         // CameraServices::m_iFOV
constexpr uintptr_t kMViewOffset = 0xF60;  // C_BaseModelEntity::m_vecViewOffset
constexpr uintptr_t kListStride = 0x70;  // Entity-Chunk-Stride (wie extern)
} // namespace injoff
