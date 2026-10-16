#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// features/MouseSmooth.h — „Smooth Aim": eigener Maus-Input als weiche Fade-
// Kurve (Raw-Input-Senke + SendInput-Gegenkorrektur/Tail; siehe .cpp).
// ─────────────────────────────────────────────────────────────────────────────

// Startet die Raw-Input-Senke + den 1-ms-Fade-Loop (laeuft bis g_stopThread).
void MouseSmoothThread();
