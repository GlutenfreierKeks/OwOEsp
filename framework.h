#pragma once

#ifdef _WIN32
// SDLCheck=true: sscanf/strncat & Co. sind dann Fehler (C4996). Die Linux-
// Seite nutzt dieselben POSIX-Funktionen — daher nur MSVC-weit abschalten.
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define WIN32_LEAN_AND_MEAN             // Selten verwendete Komponenten aus Windows-Headern ausschließen
// Windows-Headerdateien
#include <windows.h>
#endif
// Zentrale Plattform-Schicht (platform/platform.h) IMMER einbinden:
// - Windows: OWO_PROC_NAME/OWO_CLIENT_MODULE, OwO_Tex (ID3D11-SRV-Typ)
// - Linux:   Win32-Typen, VK-Codes, Timing-/Input-/Pfad-Shims
#include "platform/platform.h"
