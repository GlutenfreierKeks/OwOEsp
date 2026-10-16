#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// features/Recoil.h — Muster-basierte Recoil-Kompensation
// ─────────────────────────────────────────────────────────────────────────────
struct GameCache;

// Fuehrt die Recoil-Kompensation + Waffen-Indikator aus (Render-Thread!)
void RunRecoilControl(const GameCache& cache, int scrH);