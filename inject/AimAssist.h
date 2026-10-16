#pragma once
// inject/AimAssist.h — Screen-Space-Aim (wie externer Legit-Pfad).
//
// Nimmt ESP-Kopfpositionen, zieht per mouse_event sanft hinterher.
// Keine ViewAngle-Writes, keine Bones nötig. Key halten (default ALT).
struct AimState {
    bool enable = false;
    int key = 0x12;      // VK_MENU (ALT halten)
    float smooth = 8.f;  // 1..30 (höher = langsamer/menschlicher)
    float fovPx = 150.f; // Fangradius um Fadenkreuz (px)
    bool mates = false;  // auch Mates als Ziel?
    int mode = 0;        // 0=Maus (extern-Stil), 1=Engine (ViewAngles)
    int targets = 0;     // Diagnose: Ziele im FOV
    bool locking = false; // Diagnose: zieht gerade?
};

extern AimState g_aim;

// Pro Present (drosselt intern): Ziel = kopfnächster Gegner im FOV.
void RunAimAssist(int scrW, int scrH);
