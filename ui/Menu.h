#pragma once
#include "pch.h"
#include "MenuShared.h"
#include "MenuAssets.h"
#include "Theme.h"
#include "MenuModel.h"
#include "MenuWidgets.h"

// ─────────────────────────────────────────────────────────────────────────────
// ui/Menu.h — Glutenium-Style-ClickGUI (Port von payload/src/ui/ClickGui)
// 1280x760-Fenster, 210px-Sidebar (Logo, Kategorien, Aktionen), Modul-Karten-
// Grid, 360px-Settings-Panel das von rechts slidet. 4 Themes + lebender
// Akzent. Alle alten Features bleiben über MenuModel bedienbar.
// ─────────────────────────────────────────────────────────────────────────────

// Fonts (auch von MenuWidgets/MenuAssets genutzt)
extern ImFont* g_fontIcon;
extern ImFont* g_fontBody;
extern ImFont* g_fontHeader;

// Style + Fonts (MUST run before Backend-Init) + textures + model
#ifdef _WIN32
struct ID3D11Device;
void InitMenu(ID3D11Device* device);
#else
// Linux: GL-Kontext muss beim Aufruf current sein (Textur-Uploads).
void InitMenu();
#endif

// Zeichnet das komplette Menü (nur aufrufen, wenn g_menuOpen)
void RenderMenu();

// Theme/Akzent/Scale aus g_cfg anwenden (nach LoadConfig oder live im Client-View)
void ApplyMenuThemeFromConfig();
