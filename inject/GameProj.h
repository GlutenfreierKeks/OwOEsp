#pragma once
// inject/GameProj.h — geteilte Projektion (OverlayESP + AimAssist).
// Eine Quelle: W2S (Spalten-Konvention) + BuildViewProj (Winkel/Ursprung/FOV).
#include <windows.h>
#include <cstdint>
#include <cmath>

namespace injproj {
struct Vec3 { float x, y, z; };
struct Mat44 { float m[4][4]; };

inline bool W2S(const Vec3& w, float& sx, float& sy, const Mat44& vm, int W, int H) {
    float cx = vm.m[0][0] * w.x + vm.m[1][0] * w.y + vm.m[2][0] * w.z + vm.m[3][0];
    float cy = vm.m[0][1] * w.x + vm.m[1][1] * w.y + vm.m[2][1] * w.z + vm.m[3][1];
    float cw = vm.m[0][2] * w.x + vm.m[1][2] * w.y + vm.m[2][2] * w.z + vm.m[3][2];
    if (cw < 0.001f) return false;
    sx = (cx / cw + 1.f) * 0.5f * (float)W;
    sy = (1.f - cy / cw) * 0.5f * (float)H;
    return true;
}

// ViewProj aus Augenposition + Blickwinkeln + FOV (kein dw nötig).
// eyeAngles: QAngle (pitch, yaw, roll), fovH: horizontal Grad.
inline bool BuildViewProj(const Vec3& eye, float pitch, float yaw, uint32_t fovH,
                          int scrW, int scrH, Mat44& out) {
    if (!isfinite(pitch) || !isfinite(yaw)) return false;
    if (pitch < -89.f || pitch > 89.f) return false;
    if (fovH < 30 || fovH > 150) fovH = 90;
    if (scrW <= 0 || scrH <= 0) return false;
    float aspect = (float)scrW / (float)scrH;
    float vHalf = atanf(tanf((float)fovH * 0.5f * 0.0174533f) / aspect);
    float h = 1.f / tanf(vHalf);
    float w = h / aspect;

    const float d = 0.0174533f;
    float cp = cosf(pitch * d), sp = sinf(pitch * d);
    float cy = cosf(yaw * d), sy = sinf(yaw * d);
    Vec3 F = { cp * cy, cp * sy, -sp };
    Vec3 R = { sy, -cy, 0.f }; // = F x U (gegen Spiegel verifiziert)
    Vec3 U = { sp * cy, sp * sy, cp };
    const Vec3& E = eye;

    float view[4][4] = {
        { R.x, U.x, F.x, 0.f },
        { R.y, U.y, F.y, 0.f },
        { R.z, U.z, F.z, 0.f },
        { -(R.x * E.x + R.y * E.y + R.z * E.z),
          -(U.x * E.x + U.y * E.y + U.z * E.z),
          -(F.x * E.x + F.y * E.y + F.z * E.z), 1.f },
    };
    const float zn = 1.f, zf = 50000.f;
    float proj[4][4] = {
        { w, 0.f, 0.f, 0.f },
        { 0.f, h, 0.f, 0.f },
        { 0.f, 0.f, zf / (zf - zn), 1.f },
        { 0.f, 0.f, -zn * zf / (zf - zn), 0.f },
    };
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            float s = 0.f;
            for (int k = 0; k < 4; k++) s += view[i][k] * proj[k][j];
            out.m[i][j] = s;
            if (!isfinite(s)) return false;
        }
    return true;
}
} // namespace injproj
