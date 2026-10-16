#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// features/Triggerbot.h — automatischer Schuss beim Ueber-Hover
// ─────────────────────────────────────────────────────────────────────────────
struct GameCache;
struct UtilCache;

// Fuehrt die Triggerbot-Logik aus (nur im Render-Thread aufrufen!)
void RunTriggerbot(const GameCache& cache, const UtilCache& utilCache, int scrW, int scrH);