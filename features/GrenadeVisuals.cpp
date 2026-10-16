#include "pch.h"
#include "ESP.h"
#include "Offsets.h"
#include "Weapons.h"
#include "../core/Globals.h"
#include <cstdio>
#include <cfloat>

// ─────────────────────────────────────────────────────────────────────────────
// features/GrenadeVisuals.cpp — Granaten-Visuals (aus ESP.cpp ausgelagert)
//
//  • DrawThrowPreview : Landepunkt/Explosions-Marker der gehaltenen Nade +
//    In-Flight-Fortsetzung aus ECHTEN Projektil-Daten (m_vInitialPosition/
//    Velocity, m_flDetonateTime, m_nBounces) + Ghost nach dem Wurf. Die
//    Blickrichtung kommt aus der ViewMatrix (dreht garantiert mit).
//    Landepunkte werden auf dem Boden-Raster (g_groundGrid — echte Proben
//    aus der Partie) gesucht statt auf einer flachen Ebene; bei echtem
//    Kontakt (m_nBounces) wird auf die Live-Position gesnappt. Ohne
//    Bodenprobe gibt es KEINEN geratenen Marker. Bewusst OHNE Linien.
//  • DrawGrenadeTracks : passives Live-Tracking (Molo-Bruch-Heuristik „FIRE“)
//  • DrawUtilRadius   : Smoke-/Feuer-Sperrflächen + Restzeit-Countdown
//    (Smoke: nur Umriss-Ringe + Timer — keine gefüllte Fläche mehr)
//
// Läuft ausschliesslich im Render-Thread.
// ─────────────────────────────────────────────────────────────────────────────

// ── Basis-Helfer ─────────────────────────────────────────────────────────────

static inline ImU32 ApplyAlpha(ImU32 col, int a) {
    if (a < 0) a = 0; else if (a > 255) a = 255;
    return (col & ~IM_COL32_A_MASK) | ((ImU32)a << IM_COL32_A_SHIFT);
}

// Pulsierender Faktor (lo..hi) für Marker/Glow
static inline float Pulse(float speed, float lo, float hi) {
    float s = 0.5f + 0.5f * sinf((float)ImGui::GetTime() * speed * 6.2831853f);
    return lo + (hi - lo) * s;
}

// Blickrichtung aus der ViewMatrix (Zeile 3 = Forward-Achse der Kamera).
// Das ist die EXAKTE blickende Richtung dieses Frames — dieselbe Matrix, die
// alle ESP-Elemente projiziert. Damit dreht die Vorschau immer mit, selbst
// wenn Schema-Offsets (m_angEyeAngles) stale/garbage wären.
static bool ViewDirFromMatrix(const Matrix4x4& vm, Vector3& fwd, float& pitchDeg, float& yawDeg) {
    Vector3 f{ vm.m[3][0], vm.m[3][1], vm.m[3][2] };
    float len2 = f.x*f.x + f.y*f.y + f.z*f.z;
    if (!(len2 > 0.25f) || !isfinite(len2)) return false;
    float len = sqrtf(len2);
    f.x /= len; f.y /= len; f.z /= len;
    float z = f.z;
    if (z > 1.f) z = 1.f; else if (z < -1.f) z = -1.f;
    float pitch = -asinf(z) * kRad2Deg;
    float yaw   = atan2f(f.y, f.x) * kRad2Deg;
    if (!isfinite(pitch) || !isfinite(yaw)) return false;
    fwd = f; pitchDeg = pitch; yawDeg = yaw;
    return true;
}

// CS2-Angles → Richtungsvektor (pitch + = nach unten)
static Vector3 DirFromAngles(float pitchDeg, float yawDeg) {
    float p = pitchDeg * kDeg2Rad, y = yawDeg * kDeg2Rad;
    float cp = cosf(p), sp = sinf(p);
    float cy = cosf(y), sy = sinf(y);
    return { cp * cy, cp * sy, -sp };
}

// Kleiner Bodenring-Helfer (Kreis in Welt-Units, perspektivisch exakt)
static void DrawGroundRing(ImDrawList* dl, const Matrix4x4& vm, int scrW, int scrH,
                           const Vector3& c, float radius, ImU32 col, float thick) {
    const int N = 28;
    Vector2 prev{};
    bool hp = false;
    for (int k = 0; k <= N; k++) {
        float a = (float)k / (float)N * 6.2831853f;
        Vector3 p{ c.x + cosf(a) * radius, c.y + sinf(a) * radius, c.z };
        Vector2 s{};
        if (!WorldToScreen(p, s, vm, scrW, scrH)) { hp = false; continue; }
        if (hp) dl->AddLine(ImVec2(prev.x, prev.y), ImVec2(s.x, s.y), col, thick);
        prev = s; hp = true;
    }
}

// Zweizeiliges Info-Chip: dunkler, abgerundeter Hintergrund + Akzentrahmen.
// cx = horizontale Mitte, y = Oberkante. l2 darf nullptr/leer sein.
static void DrawChipCentered(ImDrawList* dl, float cx, float y,
                             const char* l1, const char* l2,
                             ImU32 accent, float alpha) {
    if (alpha <= 0.02f) return;
    ImFont* font = ImGui::GetFont();
    const float fs1 = 13.f, fs2 = 10.5f;
    const ImVec2 s1 = font->CalcTextSizeA(fs1, FLT_MAX, 0.f, l1);
    ImVec2 s2(0.f, 0.f);
    if (l2 && l2[0]) s2 = font->CalcTextSizeA(fs2, FLT_MAX, 0.f, l2);
    const float padX = 7.f, padY = 3.5f, gap = 1.f;
    const float w = (s2.x > s1.x ? s2.x : s1.x) + padX * 2.f;
    const float h = s1.y + (s2.y > 0.f ? gap + s2.y : 0.f) + padY * 2.f;
    const float x = cx - w * 0.5f;

    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h),
                      IM_COL32(10, 12, 16, (int)(205 * alpha)), 5.f);
    dl->AddRect(ImVec2(x, y), ImVec2(x + w, y + h),
                ApplyAlpha(accent, (int)(170 * alpha)), 5.f, 0, 1.2f);

    float ty = y + padY;
    float tx1 = x + (w - s1.x) * 0.5f;
    dl->AddText(font, fs1, ImVec2(tx1 + 1.f, ty + 1.f), IM_COL32(0, 0, 0, (int)(160 * alpha)), l1);
    dl->AddText(font, fs1, ImVec2(tx1, ty), ApplyAlpha(IM_COL32(240, 244, 252, 255), (int)(255 * alpha)), l1);
    if (s2.y > 0.f) {
        float ty2 = ty + s1.y + gap;
        float tx2 = x + (w - s2.x) * 0.5f;
        dl->AddText(font, fs2, ImVec2(tx2 + 1.f, ty2 + 1.f), IM_COL32(0, 0, 0, (int)(150 * alpha)), l2);
        dl->AddText(font, fs2, ImVec2(tx2, ty2), ApplyAlpha(accent, (int)(235 * alpha)), l2);
    }
}

// Billboard-Marker am Ereignispunkt: Doppel-Halo + Pulsring + Kern +
// langsam rotierende Ticks (Premium-Look wie gute Visual-Clients).
static void DrawEventMarker(ImDrawList* dl, const Vector2& s, ImU32 accent, float alpha) {
    const float pulse = Pulse(1.7f, 0.55f, 1.f);
    const float r = 13.f + 2.5f * pulse;
    const float rot = (float)ImGui::GetTime() * 0.9f;
    dl->AddCircle(ImVec2(s.x, s.y), r + 5.5f,
                  ApplyAlpha(accent, (int)(60 * alpha * pulse)), 28, 1.2f);
    dl->AddCircleFilled(ImVec2(s.x, s.y), r * 0.62f,
                        ApplyAlpha(accent, (int)(70 * alpha * pulse)), 20);
    dl->AddCircle(ImVec2(s.x, s.y), r, ApplyAlpha(accent, (int)(235 * alpha)), 24, 2.f);
    dl->AddCircle(ImVec2(s.x, s.y), 3.2f,
                  ApplyAlpha(IM_COL32(255, 255, 255, 255), (int)(245 * alpha)), 10, 1.8f);
    for (int k = 0; k < 4; k++) {
        float a = (float)k * 1.5707963f + 0.7853981f + rot;
        float c = cosf(a), sn = sinf(a);
        dl->AddLine(ImVec2(s.x + c * (r + 3.f), s.y + sn * (r + 3.f)),
                    ImVec2(s.x + c * (r + 8.f), s.y + sn * (r + 8.f)),
                    ApplyAlpha(accent, (int)(180 * alpha)), 1.6f);
    }
}

// ── Nade-Stile ───────────────────────────────────────────────────────────────

struct NadeStyle {
    const char* name  = "NADE";
    ImU32       color = IM_COL32(220, 220, 220, 255);
};

static NadeStyle StyleForKind(int kind) {
    switch (kind) {
        case NADEK_SMOKE:   return { "SMOKE",       IM_COL32(205, 210, 225, 255) };
        case NADEK_MOLOTOV: return { "MOLOTOV",     IM_COL32(255, 150,  60, 255) };
        case NADEK_INCG:    return { "INCENDIARY",  IM_COL32(255, 120,  50, 255) };
        case NADEK_HE:      return { "HE",          IM_COL32(255,  90,  85, 255) };
        case NADEK_FLASH:   return { "FLASH",       IM_COL32(255, 232, 120, 255) };
        case NADEK_DECOY:   return { "DECOY",       IM_COL32(130, 220, 200, 255) };
        default:            return { "NADE",        IM_COL32(220, 220, 220, 255) };
    }
}

static inline bool IsImpactKind(int kind) {
    return kind == NADEK_SMOKE || kind == NADEK_MOLOTOV || kind == NADEK_INCG;
}
static inline bool IsFuseKind(int kind) {
    return kind == NADEK_HE || kind == NADEK_FLASH;
}

static float SpeedK() {
    return g_nadeSpeedK.load(std::memory_order_relaxed);
}

static float FuseForKind(int kind) {
    if (kind < 0 || kind >= NADEK_COUNT) return 0.f;
    return g_nadeFuseS[kind].load(std::memory_order_relaxed);
}

// ── Granaten-Physik (Boden aus echten Partie-Proben statt flacher Ebene) ────
// Extern gibt es keinen BSP-Zugriff; die alte Schwäche war die Projektion auf
// eine flache Ebene. Jetzt: Bodenhöhe unter der Flugbahn aus dem Raster
// (g_groundGrid — Spielerfüße/Items/Zonen/Nade-Kontakte der laufenden
// Partie). Wird unter der Bahn KEINE Probe gefunden, gibt es keinen
// geratenen Landepunkt (SimResult.noFloor). Gravitation 800, Tick 1/64,
// Bounce e=0.45 / Bodenreibung 0.55, Ruhe < 70. breakOnImpact: Smoke/Molo/
// IncG detonieren beim ERSTEN Bodenkontakt. fuseS > 0: HE/Flash — Detonation
// nach fuseS ab Sim-Start (ggf. in der Luft). atAgeS = „jetzt" auf der
// Sim-Zeitachse (In-Flight: bereits geflogene Zeit → posNow).
struct SimResult {
    Vector3 impactPos{}; float tImpact = 0.f;   // erster Bodenkontakt
    Vector3 restPos{};   float tRest   = 0.f;   // Ruhelage
    bool    rested   = false;
    Vector3 eventPos{};  float tEvent  = 0.f;   // Detonation/Anzeige-Ereignis
    bool    airEvent = false;                   // Ereignis in der Luft
    bool    hitGround = false;                  // in der Sim gab es Bodenkontakt
    bool    noFloor  = false;                   // nie Bodenproben → keine Landeprobe
    Vector3 posNow{};    bool  hasNow = false;  // Position zu atAgeS
    float   tNow     = 0.f;
};

// Bodenhöhe unter (x,y) inkl. Nutzer-Offset. Rückgabe wie
// GroundGrid::ProbeFloor: 1 = Fläche (outZ gesetzt), 2 = Geländefront
// (outZ = Wand-/Hanghöhe), 0 = keine Daten in Reichweite (dann gibt es
// keinen geratenen Marker).
static int ProbeFloorAt(float x, float y, float aboveZ, uint32_t now, float& outZ) {
    float gz = 0.f;
    const int r = g_groundGrid.ProbeFloor(x, y, gz, now, aboveZ);
    if (r != 0) outZ = gz + g_cfg.throwGroundOffset;
    return r;
}

static void SimulateNade(const Vector3& start, const Vector3& vel,
                         bool breakOnImpact, float fuseS, float atAgeS,
                         int maxBounces, SimResult& out) {
    const float dt = 1.f / 64.f, g = 800.f;
    const uint32_t now = (uint32_t)GetTickCount();
    out = SimResult{};
    Vector3 p = start, v = vel;
    float t = 0.f;
    bool hitGround = false, rested = false, eventSet = false;
    int  bounces = 0;
    out.posNow = start; out.tNow = 0.f;
    if (atAgeS <= 0.f) out.hasNow = true;

    const float maxT = fuseS > 0.f ? fuseS + 3.f : 8.f;
    const int steps = (int)(maxT / dt) + 2;
    for (int i = 0; i < steps; ++i) {
        Vector3 prev = p;
        v.z -= g * dt;
        p.x += v.x * dt; p.y += v.y * dt; p.z += v.z * dt;
        t += dt;

        // Bodenhöhe unter der aktuellen Position aus dem Partie-Raster.
        // aboveZ = höchster der beiden Schritte → Flächen, die die Nade im
        // Schritt passiert hat (z.B. Dach während des Sinkens), zählen mit.
        // probe == 2: Hang/Mauer — die Nade steckt in der Geländefront.
        float floorZ = 0.f;
        const float aboveZ = (prev.z > p.z) ? prev.z : p.z;
        const int  probe = ProbeFloorAt(p.x, p.y, aboveZ, now, floorZ);
        const bool haveFloor = (probe == 1);

        if (haveFloor && !hitGround && v.z < 0.f && p.z <= floorZ && prev.z > floorZ) {
            float denom = prev.z - p.z;
            float a = denom > 0.0001f ? (prev.z - floorZ) / denom : 1.f;
            if (a < 0.f) a = 0.f; else if (a > 1.f) a = 1.f;
            p.x = prev.x + (p.x - prev.x) * a;
            p.y = prev.y + (p.y - prev.y) * a;
            p.z = floorZ;
            hitGround = true;
            out.impactPos = p;
            out.tImpact = t;
            if (breakOnImpact) {
                out.eventPos = p; out.tEvent = t; out.airEvent = false;
                out.restPos = p; out.tRest = t; out.rested = true;
                eventSet = true;
                break;
            }
        } else if (haveFloor && hitGround && v.z <= 0.f && p.z <= floorZ) {
            p.z = floorZ;
            v.z = -v.z * 0.45f;
            v.x *= 0.55f; v.y *= 0.55f;
            bounces++;
            float sp = sqrtf(v.x*v.x + v.y*v.y + v.z*v.z);
            if (!rested && (sp < 70.f || bounces > maxBounces)) {
                rested = true;
                out.rested = true;
                out.restPos = p;
                out.tRest = t;
                v = { 0.f, 0.f, 0.f };
            }
        } else if (probe == 2) {
            // Geländefront (Hang/Mauer im eigenen Raster-Zellbereich): Kontakt
            // HIER annehmen statt durch das Gelände zu fliegen. Impact-Nades
            // detonieren am Gesicht; Fuse-Nades stoppen ab und fallen am Fuß
            // weiter (nächster Bodenkontakt fängt sie dann normal).
            hitGround = true;
            out.impactPos = p;
            out.tImpact = t;
            if (breakOnImpact) {
                out.eventPos = p; out.tEvent = t; out.airEvent = false;
                out.restPos = p; out.tRest = t; out.rested = true;
                eventSet = true;
                break;
            }
            v = { v.x * 0.15f, v.y * 0.15f, v.z * 0.2f };
        }

        if (!out.hasNow && t >= atAgeS) {
            out.posNow = p; out.tNow = t; out.hasNow = true;
        }

        if (!eventSet && fuseS > 0.f && t >= fuseS) {
            out.eventPos = p; out.tEvent = fuseS;
            out.airEvent = !haveFloor || (p.z > floorZ + 1.f);
            eventSet = true;
        }

        if (eventSet && fuseS > 0.f) break;   // Detonation erreicht (ggf. in der Luft)
        if (rested && fuseS <= 0.f) break;    // Impact-Nade: Ruhe = Endzustand
        if (p.z < -2600.f) break;             // aus der Welt gefallen → Sim-Ende
    }
    if (!out.hasNow) { out.posNow = p; out.tNow = t; out.hasNow = true; }
    if (!eventSet) { // Decoy / Fallback: Ereignis = Ruhelage
        out.eventPos = rested ? out.restPos : p;
        out.tEvent  = rested ? out.tRest   : t;
        out.airEvent = false;
    }
    if (!rested) { out.rested = false; out.restPos = p; out.tRest = t; }
    if (!hitGround) { out.impactPos = p; out.tImpact = t; }
    out.hitGround = hitGround;
    out.noFloor   = !hitGround;
}

// ── In-Flight-Schätzung: echte Reader-Daten → Ereignispunkt + Restzeit ──────
// Für eigene UND fremde Projektile. Parabel (m_vInitialPosition/-Velocity) ist
// exakt; nach Wandkontakt (m_nBounces ≥ 1) stattdessen Live-Position/-Velocity.
// Ist der ECHTE Kontakt da (Impact-Nade oder ruhende Fuse-Nade), wird die
// Live-Position als Wahrheit übernommen (kein geratener Landepunkt mehr).
// Zünder (HE/Flash) aus m_flDetonateTime, sonst kalibrierter Wert.
struct InFlightEst {
    bool      ok      = false;
    bool      active  = false;   // Marker gerade sinnvoll?
    bool      real    = false;   // Ort stammt aus echtem Kontakt (m_nBounces)
    int       kind    = -1;
    SimResult sim{};
    float     remain  = 0.f;
    float     distM   = 0.f;
};

static InFlightEst EstimateInFlight(const GroundItem& it, const GameCache& cache, DWORD nowMs) {
    InFlightEst est;
    est.kind = NadeKindFromProjName(it.name);
    float wallDt = (float)(nowMs - it.stampMs) / 1000.f;
    if (wallDt < 0.f) wallDt = 0.f; else if (wallDt > 1.f) wallDt = 1.f;
    const float ageNow = it.ageAtRead + wallDt;

    const bool impact = IsImpactKind(est.kind);
    const bool fuseK  = IsFuseKind(est.kind);

    auto len2 = [](const Vector3& v) { return v.x*v.x + v.y*v.y + v.z*v.z; };

    // ── ECHTER Kontakt (m_nBounces ≥ 1) + Ruhe: Live-Position = Wahrheit ─────
    // Achtung: m_nBounces zählt auch WAND-Abpraller (Nade fliegt weiter) —
    // deshalb NICHT beim ersten Bounce snappen, sondern erst bei echter Ruhe
    // (Restgeschwindigkeit klein) — oder weiter unten mit Live-Pos/Vel aus
    // dem post-Bounce-Zustand simulieren (folgt der echten Flugbahn).
    if (it.bounces >= 1) {
        const float sp2 = it.velOk ? len2(it.vel) : 0.f;
        if (it.velOk && sp2 < 60.f * 60.f) {
            est.sim.eventPos  = it.origin;
            est.sim.airEvent  = false;
            est.sim.hitGround = true;
            est.sim.rested    = true;
            est.sim.restPos   = it.origin;
            est.sim.hasNow    = true;
            est.sim.posNow    = it.origin;
            est.real = true;
            float remain = 0.f;
            if (it.detonateIn > -0.05f) remain = it.detonateIn - wallDt;
            est.remain = remain > 0.f ? remain : 0.f;
            est.active = true;
            Vector3 d = VecSub(it.origin, cache.localOrigin);
            est.distM = sqrtf(d.x*d.x + d.y*d.y) / 40.f;
            est.ok = true;
            return est;
        }
    }

    const bool haveParabola = (len2(it.p0) > 1.f && len2(it.v0) > 100.f * 100.f);
    const bool haveLiveVel  = (it.velOk && len2(it.vel) > 100.f * 100.f);

    Vector3 start{}, vel{};
    float atAge = 0.f;
    bool  useAbs = false, ok = false;
    if (it.bounces == 0 && haveParabola) {
        start = it.p0; vel = it.v0; atAge = ageNow; useAbs = true; ok = true;
    } else if (haveLiveVel) {
        start = it.origin; vel = it.vel; atAge = 0.f; useAbs = false; ok = true;
    } else if (haveParabola) {
        start = it.p0; vel = it.v0; atAge = ageNow; useAbs = true; ok = true;
    }
    if (!ok) return est;

    float fuse = 0.f;
    if (fuseK) {
        if (it.detonateIn > -0.05f)
            fuse = useAbs ? (ageNow + it.detonateIn) : it.detonateIn;
        else { float f = FuseForKind(est.kind); fuse = f > 0.f ? f : 1.5f; }
    }

    SimulateNade(start, vel, impact, fuse, atAge, 3, est.sim);

    float remain = (useAbs ? est.sim.tEvent - atAge : est.sim.tEvent);
    if (fuseK && it.detonateIn >= 0.f) remain = it.detonateIn - wallDt; // exakt aus m_flDetonateTime
    if (remain < 0.f) remain = 0.f;
    est.remain = remain;
    est.active = useAbs ? (atAge < est.sim.tEvent + 0.35f) : (est.sim.tEvent > 0.05f);
    // Impact-Nades ohne Bodenprobe unter der Bahn: KEIN geratener Landepunkt
    // (genau die alte Schwäche: Projektion auf eine flache Ebene).
    if (impact && !est.sim.hitGround) est.active = false;

    Vector3 d = VecSub(est.sim.eventPos, start);
    est.distM = sqrtf(d.x*d.x + d.y*d.y) / 40.f;
    est.ok = true;
    return est;
}

// ── Ghost: letzte Vorhersage (überlebt den Wurf, zählt runter & blendet) ────
struct ThrowGhost {
    bool    active   = false;
    int     kind     = -1;
    Vector3 eventPos{};
    float   eventIn  = 0.f;   // Sekunden bis Ereignis ab setMs
    float   distM    = 0.f;
    DWORD   setMs    = 0;
    bool    airEvent = false;
};
static ThrowGhost s_ghost;

// Marker + Chip für ein Vorhersage-Ereignis zeichnen
static void DrawPredictionAt(const GameCache& cache, int scrW, int scrH, int kind,
                             const Vector3& eventPos, bool airEvent,
                             float remainS, float distM, float alpha,
                             const char* l1Override) {
    Vector2 s{};
    if (!WorldToScreen(eventPos, s, cache.vm, scrW, scrH)) return;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    NadeStyle st = StyleForKind(kind);

    if (!airEvent)
        DrawGroundRing(dl, cache.vm, scrW, scrH, eventPos, 30.f,
                       ApplyAlpha(st.color, (int)(150 * alpha)), 1.8f);
    DrawEventMarker(dl, s, st.color, alpha);

    char l2[64];
    if (remainS > 0.05f) snprintf(l2, sizeof(l2), "%.1fs · %.0fm", remainS, distM);
    else                 snprintf(l2, sizeof(l2), "%.0fm", distM);
    float chipY = s.y - 46.f;
    if (chipY < 4.f) chipY = s.y + 24.f;
    DrawChipCentered(dl, s.x, chipY, l1Override ? l1Override : st.name, l2, st.color, alpha);
}

// ─────────────────────────────────────────────────────────────────────────────
// Throw-Preview: Marker am Lande-/Explosionspunkt der Nade — ohne Linie.
// 1) Nade in der Hand  → Formel-Vorschau mit Blickrichtung aus der ViewMatrix
// 2) Eigene Nade fliegt → ECHTE Ballistik (m_vInitialPosition/Velocity) +
//    exakte Zünderzeit (m_flDetonateTime)
// 3) Danach            → Ghost (letzte Vorhersage, zählt runter und blendet)
// ─────────────────────────────────────────────────────────────────────────────
void DrawThrowPreview(const GameCache& cache, const GroundCache& ground, int scrW, int scrH) {
    if (!g_cfg.throwPreview || !cache.valid) return;
    const DWORD nowMs = GetTickCount();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    // Gehaltene Util? (Fallback, falls holdingUtil-Read mal hakt)
    bool holding = false;
    int  wid = cache.utilWeaponId ? cache.utilWeaponId : cache.currentWeaponId;
    if (weapons::IsGrenade(wid) &&
        (cache.holdingUtil || weapons::IsGrenade(cache.currentWeaponId) ||
         weapons::IsGrenade(cache.utilWeaponId)))
        holding = true;
    const int holdKind = holding ? NadeKindFromWeaponId(wid) : -1;

    // Eigene Nade in der Luft? (jüngstes eigenes Projektil im GroundCache)
    const GroundItem* own = nullptr;
    if (ground.valid) {
        float bestAge = 1e30f;
        for (int i = 0; i < ground.count; ++i) {
            const GroundItem& it = ground.items[i];
            if (it.type != 2 || !it.isProj || !it.ownedByLocal) continue;
            if (it.ageAtRead < 0.f) continue;
            if (it.ageAtRead < bestAge) { bestAge = it.ageAtRead; own = &it; }
        }
    }

    bool drew = false;

    // ── Fall 1: Nade in der Hand ─────────────────────────────────────────────
    if (holding && holdKind >= 0) {
        float strength = cache.throwStrength;
        if (strength < 0.01f || strength > 1.5f) strength = 0.55f; // Pin zu / Read-Fehler
        const bool pinned = cache.pinPulled || strength > 0.3f;

        Vector3 fwd{}; float pitchDeg = 0.f, yawDeg = 0.f;
        if (!ViewDirFromMatrix(cache.vm, fwd, pitchDeg, yawDeg)) {
            // Fallback (sollte praktisch nie nötig sein): Pawn-ViewAngles
            pitchDeg = cache.localViewAngles.x;
            yawDeg   = cache.localViewAngles.y;
        }
        // UC-Pitch-Korrektur der Wurfrichtung (Naden fliegen etwas flacher)
        float adjPitch = pitchDeg - (90.f - fabsf(pitchDeg)) * 10.f / 90.f;
        fwd = DirFromAngles(adjPitch, yawDeg);

        NadeStyle st = StyleForKind(holdKind);
        const float speed = (strength * 0.7f + 0.3f) * SpeedK();
        // Start = Wurf-Ort: Eye + Forward*16 (CS2-Weapon_ShootPosition)
        Vector3 start{ cache.localEyePos.x + fwd.x * 16.f,
                       cache.localEyePos.y + fwd.y * 16.f,
                       cache.localEyePos.z + fwd.z * 16.f };
        Vector3 vel{ fwd.x * speed + cache.localVelocity.x * 1.25f,
                     fwd.y * speed + cache.localVelocity.y * 1.25f,
                     fwd.z * speed + cache.localVelocity.z * 1.25f };

        const bool  impact  = IsImpactKind(holdKind);
        const bool  fuseK   = IsFuseKind(holdKind);
        float fuse = fuseK ? FuseForKind(holdKind) : 0.f;
        if (fuseK && fuse <= 0.f) fuse = 1.5f;

        SimResult sim;
        SimulateNade(start, vel, impact, fuse, 0.f, 3, sim);

        Vector3 d = VecSub(sim.eventPos, start);
        float distM = sqrtf(d.x*d.x + d.y*d.y) / 40.f;

        // Impact-Nades (Smoke/Molo/Deko) ohne Bodenprobe unter der Bahn:
        // KEIN geratener Landepunkt mehr (früher: flache Ebene). HE/Flash
        // zeigen die Detonation trotzdem — die Parabel ist in der Luft exakt.
        const bool showMarker = !impact || sim.hitGround;

        char l1[48];
        snprintf(l1, sizeof(l1), "%s", st.name);
        if (!pinned) snprintf(l1, sizeof(l1), "%s · HOLD", st.name);

        if (showMarker) {
            s_ghost = ThrowGhost{ true, holdKind, sim.eventPos, sim.tEvent, distM, (DWORD)nowMs, sim.airEvent };
            drew = true;
            DrawPredictionAt(cache, scrW, scrH, holdKind, sim.eventPos, sim.airEvent,
                             sim.tEvent, distM, pinned ? 1.f : 0.85f, l1);
        }

        // Legende unterm Fadenkreuz: Stärke + Pin-Status („?" = noch keine
        // Bodenprobe in der Wurfrichtung — Marker kommt, sobald Daten da sind)
        {
            char leg[96];
            snprintf(leg, sizeof(leg), "%s  %.0f%%  %s%s", st.name, strength * 100.f,
                     pinned ? "PIN" : "HOLD", showMarker ? "" : "  ·  ?");
            DrawChipCentered(dl, scrW * 0.5f, scrH * 0.5f + 26.f, leg, nullptr, st.color, 0.95f);
        }

        if (g_cfg.debugLog) {
            static DWORD lastDbg = 0;
            if (nowMs - lastDbg > 1500) {
                printf("[throw] wid=%d strength=%.2f pinned=%d eye=(%.0f,%.0f,%.0f) fwd=(%+.2f,%+.2f,%+.2f) "
                       "flight=%.2fs boomIn=%.2fs dist=%.0fm\n",
                       wid, strength, pinned ? 1 : 0,
                       cache.localEyePos.x, cache.localEyePos.y, cache.localEyePos.z,
                       fwd.x, fwd.y, fwd.z, sim.tImpact, sim.tEvent, distM);
                lastDbg = nowMs;
            }
        }
    }
    // ── Fall 2: eigene Nade in der Luft (echte Ballistik + Zünder) ──────────
    else if (own) {
        const InFlightEst est = EstimateInFlight(*own, cache, nowMs);
        if (est.ok && est.active) {
            const NadeStyle st = StyleForKind(est.kind);
            s_ghost = ThrowGhost{ true, est.kind, est.sim.eventPos, est.remain, est.distM, (DWORD)nowMs, est.sim.airEvent };
            drew = true;

            DrawPredictionAt(cache, scrW, scrH, est.kind, est.sim.eventPos, est.sim.airEvent,
                             est.remain, est.distM, 1.f, nullptr);

            // Flugpunkt: Glow an der simulierten Ist-Position — nur solange die
            // Schätzung läuft (bei echtem Kontakt steht dort schon der Marker).
            if (est.sim.hasNow && !est.real) {
                Vector2 sp{};
                if (WorldToScreen(est.sim.posNow, sp, cache.vm, scrW, scrH)) {
                    float a = Pulse(2.2f, 0.35f, 0.75f);
                    dl->AddCircleFilled(ImVec2(sp.x, sp.y), 6.f,
                                        ApplyAlpha(st.color, (int)(110 * a)), 16);
                    dl->AddCircleFilled(ImVec2(sp.x, sp.y), 2.5f,
                                        ApplyAlpha(IM_COL32(255, 255, 255, 255), (int)(200 * a)), 10);
                }
            }
        }
    }

    // ── Fall 2b: fremde (und eigene, falls gerade eine 2. Nade gehalten wird)
    //    fliegende Nades ebenfalls markieren — max 6, dezent ─────────────────
    if (ground.valid) {
        int done = 0;
        for (int i = 0; i < ground.count && done < 6; ++i) {
            const GroundItem& it = ground.items[i];
            if (it.type != 2 || !it.isProj) continue;
            if (!holding && own && it.entAddr == own->entAddr) continue; // via Fall 2
            const InFlightEst est = EstimateInFlight(it, cache, nowMs);
            if (!est.ok || !est.active) continue;
            DrawPredictionAt(cache, scrW, scrH, est.kind, est.sim.eventPos, est.sim.airEvent,
                             est.remain, est.distM, 0.85f, nullptr);
            done++;
        }
    }

    // ── Fall 3: Ghost — letzte Vorhersage zählt runter, dann Fade ───────────
    if (!drew && s_ghost.active) {
        float ageS  = (float)(nowMs - s_ghost.setMs) / 1000.f;
        float remain = s_ghost.eventIn - ageS;
        if (remain < -1.6f || (remain <= 0.f && IsImpactKind(s_ghost.kind))) {
            s_ghost.active = false; // Impact-Nade: die echte Zone übernimmt sofort
        } else {
            float alpha = remain < 0.f ? (1.f + remain / 1.6f) : 1.f;
            Vector2 s{};
            if (WorldToScreen(s_ghost.eventPos, s, cache.vm, scrW, scrH)) {
                NadeStyle st = StyleForKind(s_ghost.kind);
                if (!s_ghost.airEvent)
                    DrawGroundRing(dl, cache.vm, scrW, scrH, s_ghost.eventPos, 30.f,
                                   ApplyAlpha(st.color, (int)(140 * alpha)), 1.8f);
                DrawEventMarker(dl, s, st.color, alpha);
                char l2[64];
                if (remain > 0.05f) snprintf(l2, sizeof(l2), "%.1fs · %.0fm", remain, s_ghost.distM);
                else                snprintf(l2, sizeof(l2), "%.0fm", s_ghost.distM);
                float chipY = s.y - 46.f;
                if (chipY < 4.f) chipY = s.y + 24.f;
                DrawChipCentered(dl, s.x, chipY, st.name, l2, st.color, alpha);
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Passives Granaten-Tracking: Molo-Bruch-Heuristik.
// Zerbrochenes Molotov-/Incendiary-Projektil (despawn) → dort brennt es
// ~7s. Die echte Inferno-Zone (DrawUtilRadius) unterdrückt das Duplikat.
// Bewusst OHNE Trail-Linie — nur der Feuer-Marker als Fallback.
// ─────────────────────────────────────────────────────────────────────────────
void DrawGrenadeTracks(const GroundCache& ground, const Matrix4x4& vm, int scrW, int scrH,
                       const UtilCache* util) {
    if (!ground.valid) return;
    const DWORD now = GetTickCount();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    struct MoloTrack {
        bool    used = false;
        char    name[32]{};
        Vector3 lastPos{};
        DWORD   lastSeen = 0;
        bool    counted  = false;
    };
    static MoloTrack s_mt[6];
    bool seen[6] = {};

    for (int i = 0; i < ground.count; ++i) {
        const GroundItem& it = ground.items[i];
        if (it.type != 2) continue;
        if (strcmp(it.name, "Molotov") != 0 && strcmp(it.name, "Incendiary") != 0) continue;

        int best = -1;
        float bestD = 100.f * 100.f;
        for (int t = 0; t < 6; ++t) {
            if (!s_mt[t].used || seen[t] || strcmp(s_mt[t].name, it.name) != 0) continue;
            float dx = s_mt[t].lastPos.x - it.origin.x;
            float dy = s_mt[t].lastPos.y - it.origin.y;
            float dz = s_mt[t].lastPos.z - it.origin.z;
            float d = dx*dx + dy*dy + dz*dz;
            if (d < bestD) { bestD = d; best = t; }
        }
        if (best < 0) {
            for (int t = 0; t < 6; ++t)
                if (!s_mt[t].used) { best = t; break; }
            if (best < 0) { // ältesten recyclen
                DWORD oldest = now;
                for (int t = 0; t < 6; ++t)
                    if (s_mt[t].lastSeen <= oldest) { oldest = s_mt[t].lastSeen; best = t; }
            }
            s_mt[best] = MoloTrack{};
            snprintf(s_mt[best].name, sizeof(s_mt[best].name), "%s", it.name);
        }
        s_mt[best].lastPos  = it.origin;
        s_mt[best].lastSeen = now;
        seen[best] = true;
    }

    // Verschwundene Tracks = Bruch → Feuer-Marker 7s
    struct AssumedFire { bool used = false; Vector3 pos{}; DWORD expiry = 0; };
    static AssumedFire s_fire[4];
    for (int t = 0; t < 6; ++t) {
        MoloTrack& tr = s_mt[t];
        if (!tr.used) continue;
        if (now - tr.lastSeen > 1200) { tr.used = false; continue; } // alt/Rundenwechsel
        if (seen[t] || tr.counted) continue;                          // noch da → kein Bruch
        int slot = -1;
        for (int k = 0; k < 4; ++k)
            if (!s_fire[k].used || now >= s_fire[k].expiry) { slot = k; break; }
        if (slot < 0) slot = 0;
        s_fire[slot].used   = true;
        s_fire[slot].pos    = tr.lastPos;
        s_fire[slot].expiry = now + (DWORD)(kFireDurationS * 1000.f);
        tr.counted = true;
    }

    for (int k = 0; k < 4; ++k) {
        if (!s_fire[k].used || now >= s_fire[k].expiry) continue;
        bool covered = false;
        if (util && util->valid) {
            for (int i = 0; i < util->count; ++i) {
                const UtilZone& z = util->zones[i];
                if (z.type != 1) continue;
                float dx = z.origin.x - s_fire[k].pos.x;
                float dy = z.origin.y - s_fire[k].pos.y;
                float dz = z.origin.z - s_fire[k].pos.z;
                if (dx*dx + dy*dy + dz*dz < 300.f * 300.f) { covered = true; break; }
            }
        }
        if (covered) continue;

        float remain = (float)(s_fire[k].expiry - now) / 1000.f;
        DrawGroundRing(dl, vm, scrW, scrH, s_fire[k].pos, 190.f,
                       IM_COL32(255, 140, 50, 200), 2.2f);
        Vector2 s{};
        if (WorldToScreen(s_fire[k].pos, s, vm, scrW, scrH)) {
            char l2[32];
            snprintf(l2, sizeof(l2), "~%.0fs", remain);
            float chipY = s.y - 42.f;
            if (chipY < 4.f) chipY = s.y + 20.f;
            DrawChipCentered(dl, s.x, chipY, "FIRE", l2, IM_COL32(255, 150, 60, 255), 0.95f);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Util-Radius: Smoke-Sperrfläche + Molo-Flammen-Form am Boden,
// jeweils mit Restzeit (Reader stempelt firstSeenMs beim ersten Sichtkontakt).
// Smoke: 18s ab Detonation | Feuer: ~7s ab Brandbeginn.
// ─────────────────────────────────────────────────────────────────────────────
void DrawUtilRadius(const UtilCache& util, const Matrix4x4& vm, int scrW, int scrH) {
    if (!util.valid || util.count == 0) return;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const DWORD now = GetTickCount();

    for (int i = 0; i < util.count; ++i) {
        const UtilZone& z = util.zones[i];
        const float age = z.firstSeenMs ? (float)(now - z.firstSeenMs) / 1000.f : 0.f;

        if (z.type == 0 && g_cfg.utilRadiusSmoke) {
            float remain = kSmokeDurationS - age;
            if (remain < 0.f) remain = 0.f;
            const bool urgent = remain < 5.f;
            const float pulse = Pulse(urgent ? 3.2f : 1.2f, 0.65f, 1.f);
            const ImU32 base = urgent ? IM_COL32(255, 120, 100, 235) : IM_COL32(165, 172, 195, 225);

            DrawGroundRing(dl, vm, scrW, scrH, z.origin, 160.f,
                           ApplyAlpha(base, (int)(190 * pulse)), 2.f);
            DrawGroundRing(dl, vm, scrW, scrH, z.origin, 105.f,
                           IM_COL32(150, 155, 180, 85), 1.3f);

            // Nur Umriss-Ringe + Timer — die frühere gefüllte „Kugel" über der
            // Smoke ist weg (verdeckte die Sicht und sah schmutzig aus).
            Vector2 sc{};
            if (WorldToScreen(z.origin, sc, vm, scrW, scrH)) {
                char l2[48];
                snprintf(l2, sizeof(l2), "%.1fs", remain);
                float chipY = sc.y - 42.f;
                if (chipY < 4.f) chipY = sc.y + 20.f;
                DrawChipCentered(dl, sc.x, chipY, "SMOKE", l2, base, 1.f);
            }
        } else if (z.type == 1 && g_cfg.utilRadiusFire) {
            float remain = kFireDurationS - age;
            if (remain < 0.f) remain = 0.f;
            const bool urgent = remain < 3.f;
            const float pulse = Pulse(urgent ? 3.2f : 1.6f, 0.6f, 1.f);
            const ImU32 base = urgent ? IM_COL32(255, 95, 80, 240) : IM_COL32(255, 150, 60, 240);

            for (int f = 0; f < z.flameCount; ++f) {
                Vector2 s{}, edge{};
                Vector3 ex{ z.flames[f].x + 55.f, z.flames[f].y, z.flames[f].z };
                if (!WorldToScreen(z.flames[f], s, vm, scrW, scrH)) continue;
                if (!WorldToScreen(ex, edge, vm, scrW, scrH)) continue;
                float r = sqrtf((edge.x - s.x) * (edge.x - s.x) + (edge.y - s.y) * (edge.y - s.y));
                if (r < 2.f) r = 2.f; else if (r > 60.f) r = 60.f;
                dl->AddCircleFilled(ImVec2(s.x, s.y), r, IM_COL32(235, 110, 40, 60), 14);
                dl->AddCircleFilled(ImVec2(s.x, s.y), r * 0.55f, IM_COL32(255, 170, 60, 55), 12);
                dl->AddCircle(ImVec2(s.x, s.y), r, ApplyAlpha(base, (int)(215 * pulse)), 14, 1.6f);
            }
            Vector2 sc{};
            if (WorldToScreen(z.origin, sc, vm, scrW, scrH)) {
                char l2[48];
                snprintf(l2, sizeof(l2), "%.1fs", remain);
                float chipY = sc.y - 42.f;
                if (chipY < 4.f) chipY = sc.y + 20.f;
                DrawChipCentered(dl, sc.x, chipY, "FIRE", l2, base, 1.f);
            }
        }
    }
}
