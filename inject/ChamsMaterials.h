#pragma once
// inject/ChamsMaterials.h — VMat-Buffer für Chams-Materialien (Quelle: UC-Thread
// "Chams materials", Janux123-Sammlung). Reine DATEN für später.
//
// Anwendung (noch NICHT verdrahtet, v1 nutzt Entity-Glow aus Chams.cpp):
//   1. Material über IMaterialSystem2 erzeugen (find_material auf
//      "materials/dev/primary_white.vmat" + create_material mit KV3-Buffer),
//   2. DrawObject-Hook in scenesystem.dll (Player-Objekte: player_ct=113 /
//      player_t=104 an m_objectInfo+176 erkennen),
//   3. pro Frame: erst Invisible-Material (F_IGNOREZ=1, durch Wände), dann
//      Visible-Material zeichnen, Farbe via m_color modulieren.
// Details: UC "Simple Chams system (Entity color modulating)" (W1lliam1337).

namespace chams::materials {

// Latex: deckend, einfarbig (klassische Chams-Optik).
inline constexpr const char* kLatexVisible =
    R"({ shader = "csgo_character.vfx" F_BLEND_MODE = 1 g_vColorTint = [1.0, 1.0, 1.0, 1.0] g_bFogEnabled = 0 g_flMetalness = 0.000 g_tMetalness = resource:"materials/default/default_metal_tga_8fbc2820.vtex" g_tColor = resource:"materials/dev/primary_white_color_tga_21186c76.vtex" g_tAmbientOcclusion = resource:"materials/default/default_ao_tga_79a2e0d0.vtex" g_tNormal = resource:"materials/default/default_normal_tga_1b833b2a.vtex" })";
inline constexpr const char* kLatexInvisible =
    R"({ shader = "csgo_character.vfx" F_DISABLE_Z_BUFFERING = 1 F_DISABLE_Z_PREPASS = 1 F_DISABLE_Z_WRITE = 1 F_BLEND_MODE = 1 g_vColorTint = [1.0, 1.0, 1.0, 1.0] g_bFogEnabled = 0 g_flMetalness = 0.000 g_tColor = resource:"materials/dev/primary_white_color_tga_21186c76.vtex" g_tAmbientOcclusion = resource:"materials/default/default_ao_tga_79a2e0d0.vtex" g_tNormal = resource:"materials/default/default_normal_tga_1b833b2a.vtex" g_tMetalness = resource:"materials/default/default_metal_tga_8fbc2820.vtex" })";

// Fresnel-Glow: leuchtende Kanten (Overlay-Stil).
inline constexpr const char* kGlowVisible =
    R"({ Shader = "csgo_effects.vfx" g_flFresnelExponent = 7.0 g_flFresnelFalloff = 10.0 g_flFresnelMax = 0.1 g_flFresnelMin = 1.0 g_tColor = resource:"materials/dev/primary_white_color_tga_21186c76.vtex" g_tMask1 = resource:"materials/default/default_mask_tga_fde710a5.vtex" g_tMask2 = resource:"materials/default/default_mask_tga_fde710a5.vtex" g_tMask3 = resource:"materials/default/default_mask_tga_fde710a5.vtex" g_tSceneDepth = resource:"materials/default/default_mask_tga_fde710a5.vtex" g_flToolsVisCubemapReflectionRoughness = 1.0 g_flBeginMixingRoughness = 1.0 g_vColorTint = [ 1.000000, 1.000000, 1.000000, 0 ] F_IGNOREZ = 0 F_DISABLE_Z_WRITE = 0 F_DISABLE_Z_BUFFERING = 0 F_RENDER_BACKFACES = 0 })";
inline constexpr const char* kGlowInvisible =
    R"({ Shader = "csgo_effects.vfx" g_flFresnelExponent = 7.0 g_flFresnelFalloff = 10.0 g_flFresnelMax = 0.1 g_flFresnelMin = 1.0 g_tColor = resource:"materials/dev/primary_white_color_tga_21186c76.vtex" g_tMask1 = resource:"materials/default/default_mask_tga_fde710a5.vtex" g_tMask2 = resource:"materials/default/default_mask_tga_fde710a5.vtex" g_tMask3 = resource:"materials/default/default_mask_tga_fde710a5.vtex" g_tSceneDepth = resource:"materials/default/default_mask_tga_fde710a5.vtex" g_flToolsVisCubemapReflectionRoughness = 1.0 g_flBeginMixingRoughness = 1.0 g_vColorTint = [ 1.000000, 1.000000, 1.000000, 0 ] F_IGNOREZ = 1 F_DISABLE_Z_WRITE = 1 F_DISABLE_Z_BUFFERING = 1 F_RENDER_BACKFACES = 0 })";

// Glass/Ghost: transparent (Geist-Optik).
inline constexpr const char* kGlassVisible =
    R"({ shader = "csgo_effects.vfx" g_tColor = resource:"materials/dev/primary_white_color_tga_21186c76.vtex" g_tNormal = resource:"materials/default/default_normal_tga_7652cb.vtex" g_tMask1 = resource:"materials/default/default_mask_tga_344101f8.vtex" g_tMask2 = resource:"materials/default/default_mask_tga_344101f8.vtex" g_tMask3 = resource:"materials/default/default_mask_tga_344101f8.vtex" g_flOpacityScale = 0.45 g_flFresnelExponent = 0.75 g_flFresnelFalloff = 1 g_flFresnelMax = 0.0 g_flFresnelMin = 1 F_ADDITIVE_BLEND = 1 F_BLEND_MODE = 1 F_TRANSLUCENT = 1 F_IGNOREZ = 0 F_DISABLE_Z_WRITE = 0 F_DISABLE_Z_BUFFERING = 0 F_RENDER_BACKFACES = 1 g_vColorTint = [1.0, 1.0, 1.0, 0.0] })";
inline constexpr const char* kGlassInvisible =
    R"({ shader = "csgo_effects.vfx" g_tColor = resource:"materials/dev/primary_white_color_tga_21186c76.vtex" g_tNormal = resource:"materials/default/default_normal_tga_7652cb.vtex" g_tMask1 = resource:"materials/default/default_mask_tga_344101f8.vtex" g_tMask2 = resource:"materials/default/default_mask_tga_344101f8.vtex" g_tMask3 = resource:"materials/default/default_mask_tga_344101f8.vtex" g_flOpacityScale = 0.45 g_flFresnelExponent = 0.75 g_flFresnelFalloff = 1 g_flFresnelMax = 0.0 g_flFresnelMin = 1 F_ADDITIVE_BLEND = 1 F_BLEND_MODE = 1 F_TRANSLUCENT = 1 F_IGNOREZ = 1 F_DISABLE_Z_WRITE = 1 F_DISABLE_Z_BUFFERING = 1 F_RENDER_BACKFACES = 1 g_vColorTint = [1.0, 1.0, 1.0, 0.0] })";

} // namespace chams::materials
