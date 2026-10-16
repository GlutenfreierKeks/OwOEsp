#pragma once
#include "pch.h"

// ─────────────────────────────────────────────────────────────────────────────
// PNG textures loaded from assets\ via WIC (Windows) bzw. stb_image (Linux).
// Handle-Typ plattformneutral (OwO_Tex): D3D-SRV bzw. GL-Textur als void*.
// ─────────────────────────────────────────────────────────────────────────────

#ifdef _WIN32
struct ID3D11Device;
void LoadMenuAssets(ID3D11Device* device);
#else
void LoadMenuAssets();
#endif
void ReleaseMenuAssets();

extern OwO_Tex g_texLogo; // 90x90  -> drawn 48x54 at (24,18)
extern OwO_Tex g_texFoto; // 45x45  -> drawn 37x37 at (30,492)
