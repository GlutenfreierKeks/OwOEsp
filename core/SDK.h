#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// core/SDK.h — Basis-Mathematik & Welt-Konvertierung
// Keine Abhaengigkeiten, wird von allen Modulen genutzt.
// ─────────────────────────────────────────────────────────────────────────────
#include <cstdint>
#include <cmath>

// ── Vektoren / Matrizen ──────────────────────────────────────────────────────
struct Vector2 { float x, y; };
struct Vector3 { float x, y, z; };
struct Matrix4x4 { float m[4][4]; };

// ── Konstante ────────────────────────────────────────────────────────────────
constexpr float kDeg2Rad = 0.0174532925199433f;   // PI / 180
constexpr float kRad2Deg = 57.2957795130823f;     // 180 / PI

// ── Winkel-Normalisierung ────────────────────────────────────────────────────
inline float NormalizeAngle(float a) {
    while (a >  180.f) a -= 360.f;
    while (a < -180.f) a += 360.f;
    return a;
}

// ── ViewMatrix → Bildschirmkoordinaten ───────────────────────────────────────
// Gleiche Matrix, die der ESP fuer die Boxen benutzt — funktioniert exakt
// wie das Spiel selbst (kein Winkel-Vorzeichen-Risiko).
inline bool WorldToScreen(const Vector3& world, Vector2& screen, const Matrix4x4& vm,
                          int w, int h) {
    float clipX = vm.m[0][0] * world.x + vm.m[0][1] * world.y + vm.m[0][2] * world.z + vm.m[0][3];
    float clipY = vm.m[1][0] * world.x + vm.m[1][1] * world.y + vm.m[1][2] * world.z + vm.m[1][3];
    float clipW = vm.m[3][0] * world.x + vm.m[3][1] * world.y + vm.m[3][2] * world.z + vm.m[3][3];
    if (clipW < 0.001f) return false;
    float ndcX = clipX / clipW;
    float ndcY = clipY / clipW;
    screen.x   = (ndcX + 1.f) * 0.5f * (float)w;
    screen.y   = (1.f - ndcY) * 0.5f * (float)h;
    return true;
}

// ── Vektor-Mathematik (kleine Helfer) ────────────────────────────────────────
inline float VecDist(const Vector3& a, const Vector3& b) {
    float dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
    return sqrtf(dx*dx + dy*dy + dz*dz);
}

inline Vector3 VecSub(const Vector3& a, const Vector3& b) {
    return { a.x - b.x, a.y - b.y, a.z - b.z };
}

// ── Vorhersage gegen Reader-/Interp-Lag ──────────────────────────────────────
// Position + Geschwindigkeit * Zeit. ageMs = Snapshot-Alter, plus 25ms
// Engine-Interp-Pauschale, gedeckelt auf 120ms (kein wildes Vorauseilen bei
// Richtungswechseln). Standard-Trick externer ESPs.
inline Vector3 PredictPos(const Vector3& o, const Vector3& v, uint32_t ageMs) {
    float dt = ageMs > 95 ? 0.12f : ((float)ageMs / 1000.f + 0.025f);
    return { o.x + v.x * dt, o.y + v.y * dt, o.z + v.z * dt };
}
