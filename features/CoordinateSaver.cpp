#include "pch.h"
#include "CoordinateSaver.h"
#include "Offsets.h"
#include "../core/Globals.h"
#include "../core/Input.h"
#include <vector>
#include <cstdio>
#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
// features/CoordinateSaver.cpp — "Coordinate Saver"-Modul
//
// Tastendruck (Flanke) -> naechstes Objekt am Fadenkreuz suchen (wie der
// Triggerbot per Screen-Projektion) -> Weltposition + Label in Liste und in
// PixelMommySense_coords.txt speichern. Marker werden im ESP gezeichnet.
//
// Abdeckung: Spieler/Chicken/Ball/Items aus den Caches + Tueren, Breakables,
// Buttons und Hostages per Live-Scan beim Tastendruck. Reine Weltgeometrie
// (Waende/Boeden) ist keine Entity: dafuer gibt es den Strahl-Fallback —
// einen Punkt auf dem Blickstrahl in einstellbarer Distanz, mit Live-Vorschau
// im ESP (Marker auf die Wand legen -> Distanz so waehlen, dass die Vorschau
// auf der Wand sitzt -> Taste druecken).
// ─────────────────────────────────────────────────────────────────────────────
namespace {
constexpr int kMaxSaved = 256;
constexpr const char* kFileName = "PixelMommySense_coords.txt";
// Wie Reader.cpp (dort lokal definiert)
constexpr uintptr_t kEntListStride = 0x70;
constexpr int       kEntListStart  = 65;
constexpr int       kEntListEnd    = 2048;

std::vector<SavedCoord> g_saved;
bool g_loaded = false;

void EnsureLoaded() {
    if (g_loaded) return;
    g_loaded = true;
    FILE* f = nullptr;
    if (fopen_s(&f, kFileName, "r") != 0 || !f) return;
    char line[128];
    while (fgets(line, sizeof(line), f) && (int)g_saved.size() < kMaxSaved) {
        SavedCoord sc{};
        float x = 0.f, y = 0.f, z = 0.f;
        // Format: label | x y z (%47 begrenzt → kein Overflow ohne _s nötig)
        if (sscanf(line, "%47[^|]|%f%f%f", sc.label, &x, &y, &z) == 4) {
            // Label trimmen (Leerzeichen um | entfernen)
            size_t n = strlen(sc.label);
            while (n > 0 && sc.label[n - 1] == ' ') sc.label[--n] = 0;
            sc.pos = { x, y, z };
            g_saved.push_back(sc);
        }
    }
    fclose(f);
    if (!g_saved.empty())
        printf("[coordsaver] %d gespeicherte Koordinaten geladen\n", (int)g_saved.size());
}

void AppendToFile(const SavedCoord& sc) {
    FILE* f = nullptr;
    if (fopen_s(&f, kFileName, "a") != 0 || !f) return;
    fprintf(f, "%s | %.1f %.1f %.1f\n", sc.label, sc.pos.x, sc.pos.y, sc.pos.z);
    fclose(f);
}

// Bauwerk-Entities (Tueren/Breakables/Buttons/Hostages): exakte Namen.
bool IsStructureDesignerName(const char* name) {
    static const char* kNames[] = {
        "prop_door_rotating",
        "func_door", "func_door_rotating",
        "func_breakable", "func_breakable_surf",
        "func_button", "func_rot_button",
        "func_movelinear", "func_brush", "func_wall",
        "hostage_entity",
    };
    for (const char* n : kNames)
        if (strcmp(name, n) == 0) return true;
    return false;
}

// Blickrichtung aus ViewAngles (Source-Konvention: x=pitch, y=yaw, Grad).
Vector3 ViewForward(const Vector3& angles) {
    float p = angles.x * kDeg2Rad;
    float y = angles.y * kDeg2Rad;
    float cp = cosf(p);
    return { cp * cosf(y), cp * sinf(y), -sinf(p) };
}

Vector3 RayPoint(const GameCache& cache, float dist) {
    Vector3 dir = ViewForward(cache.localViewAngles);
    return { cache.localEyePos.x + dir.x * dist,
             cache.localEyePos.y + dir.y * dist,
             cache.localEyePos.z + dir.z * dist };
}
} // namespace

int SavedCoordCount() { return (int)g_saved.size(); }

int GetSavedCoords(SavedCoord* out, int maxOut) {
    if (!out || maxOut <= 0) return 0;
    int n = (int)g_saved.size() < maxOut ? (int)g_saved.size() : maxOut;
    for (int i = 0; i < n; i++) out[i] = g_saved[i];
    return n;
}

void ClearSavedCoords() {
    g_saved.clear();
    FILE* f = nullptr;
    if (fopen_s(&f, kFileName, "w") == 0 && f) fclose(f); // Datei leeren
    printf("[coordsaver] Liste geloescht\n");
}

void RunCoordinateSaver(const GameCache& cache, const EntAimCache& chicken,
                        const EntAimCache& ball, const GroundCache& ground,
                        int scrW, int scrH) {
    EnsureLoaded();
    if (!g_cfg.coordSaverEnable) return;
    if (g_keyBinding) return;

    // "Liste loeschen"-Schalter: einmal ausfuehren, dann zuruecksetzen
    if (g_cfg.coordSaverClear) {
        g_cfg.coordSaverClear = false;
        ClearSavedCoords();
        return;
    }

    if (!cache.valid) return;
    if (!WasKeyPressed(g_cfg.coordSaverKey)) return;

    const float cx = scrW * 0.5f, cy = scrH * 0.5f;
    const float radius = g_cfg.coordSaverRadius;
    const float radiusSq = radius * radius;

    bool   found = false;
    float  bestSq = radiusSq;
    Vector3 bestPos{};
    char   bestLabel[48]{};

    auto consider = [&](const Vector3& world, const char* label) {
        Vector2 scr;
        if (!WorldToScreen(world, scr, cache.vm, scrW, scrH)) return;
        float dx = scr.x - cx, dy = scr.y - cy;
        float dSq = dx * dx + dy * dy;
        if (dSq < bestSq) {
            bestSq = dSq; bestPos = world; found = true;
            snprintf(bestLabel, sizeof(bestLabel), "%s", label);
        }
    };

    char label[48];
    // Spieler (Brustpunkt wie Triggerbot)
    for (int i = 0; i < cache.playerCount; i++) {
        const auto& p = cache.players[i];
        if (!p.alive) continue;
        Vector3 chest{ p.worldOrigin.x, p.worldOrigin.y,
                       p.worldOrigin.z + (p.crouching ? 48.f : 64.f) };
        if (p.name[0])
            snprintf(label, sizeof(label), "Player %s", p.name);
        else
            snprintf(label, sizeof(label), "Player %s", p.team == 2 ? "T" : "CT");
        consider(chest, label);
    }
    // Chicken
    if (chicken.valid) {
        for (int i = 0; i < chicken.count; i++) {
            const auto& t = chicken.targets[i];
            if (!t.alive) continue;
            Vector3 aim{ t.origin.x, t.origin.y, t.origin.z + 10.f };
            consider(aim, "chicken");
        }
    }
    // Ball
    if (ball.valid) {
        for (int i = 0; i < ball.count; i++) {
            const auto& t = ball.targets[i];
            if (!t.alive) continue;
            consider(t.origin, "ball");
        }
    }
    // Boden-Items (Screen-Position liegt schon im Cache)
    if (ground.valid) {
        for (int i = 0; i < ground.count; i++) {
            const auto& it = ground.items[i];
            if (!it.onScreen) continue;
            float dx = it.screen.x - cx, dy = it.screen.y - cy;
            if (dx * dx + dy * dy < bestSq) {
                bestSq = dx * dx + dy * dy; bestPos = it.origin; found = true;
                snprintf(bestLabel, sizeof(bestLabel), "item %s", it.name);
            }
        }
    }

    // Tueren/Breakables/Buttons/Hostages: Live-Scan, einmalig pro Tastendruck
    {
        uintptr_t entitySystem = g_mem.Read<uintptr_t>(g_client + offsets::dwEntityList);
        if (entitySystem) {
            for (int i = kEntListStart; i < kEntListEnd; i++) {
                uintptr_t listEntry = g_mem.Read<uintptr_t>(
                    entitySystem + 0x10 + 8 * (uintptr_t)((uint32_t)(i >> 9)));
                if (!listEntry) continue;
                uintptr_t entity = g_mem.Read<uintptr_t>(
                    listEntry + kEntListStride * (uintptr_t)((uint32_t)(i & 0x1FF)));
                if (!entity) continue;
                uintptr_t identity = g_mem.Read<uintptr_t>(entity + 0x10);
                if (!identity) continue;
                uintptr_t designerNamePtr = g_mem.Read<uintptr_t>(identity + 0x20);
                if (!designerNamePtr) continue;
                char nameBuf[32]{};
                g_mem.ReadBuffer(designerNamePtr, nameBuf, sizeof(nameBuf) - 1);
                if (!IsStructureDesignerName(nameBuf)) continue;
                uintptr_t node = g_mem.Read<uintptr_t>(entity + schemas::m_pGameSceneNode);
                if (!node) continue;
                Vector3 origin = g_mem.Read<Vector3>(node + schemas::m_vecAbsOrigin);
                if (origin.x == 0.f && origin.y == 0.f && origin.z == 0.f) continue;
                consider(origin, nameBuf);
            }
        }
    }

    // Strahl-Fallback: kein Objekt getroffen -> Punkt auf dem Blickstrahl
    // (Schaetzung fuer Waende/Boeden — per Vorschau auf die Wand legen).
    bool isRayEstimate = false;
    if (!found && g_cfg.coordSaverRayFallback) {
        float d = g_cfg.coordSaverRayDist;
        if (d < 1.f) d = 1.f;
        bestPos = RayPoint(cache, d);
        snprintf(bestLabel, sizeof(bestLabel), "ray ~%.0fu", d);
        found = true;
        isRayEstimate = true;
    }

    if (!found) {
        printf("[coordsaver] nichts anvisiert (Radius %.0f px)\n", radius);
        return;
    }

    if ((int)g_saved.size() >= kMaxSaved)
        g_saved.erase(g_saved.begin()); // aeltesten verwerfen

    SavedCoord sc{};
    sc.pos = bestPos;
    snprintf(sc.label, sizeof(sc.label), "%s", bestLabel);
    g_saved.push_back(sc);
    AppendToFile(sc);
    printf("[coordsaver] #%d %s | %.1f %.1f %.1f%s\n",
           (int)g_saved.size(), sc.label, sc.pos.x, sc.pos.y, sc.pos.z,
           isRayEstimate ? " (geschaetzt: Strahl-Fallback)" : "");
}

void DrawSavedCoords(const Matrix4x4& vm, int scrW, int scrH) {
    if (!g_cfg.coordSaverEnable || !g_cfg.coordSaverMarkers) return;
    if (g_saved.empty()) return;
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    const ImU32 col = IM_COL32(80, 220, 255, 230);
    const ImU32 txt = IM_COL32(230, 235, 245, 240);
    char buf[96];
    char posBuf[48];
    for (int i = 0; i < (int)g_saved.size(); i++) {
        Vector2 scr;
        if (!WorldToScreen(g_saved[i].pos, scr, vm, scrW, scrH)) continue;
        const float r = 7.f;
        // Raute mit schwarzer Outline
        draw->AddLine(ImVec2(scr.x - 1.f, scr.y - r - 1.f), ImVec2(scr.x + r + 1.f, scr.y - 1.f), IM_COL32(0, 0, 0, 180), 2.5f);
        draw->AddLine(ImVec2(scr.x + r + 1.f, scr.y - 1.f), ImVec2(scr.x - 1.f, scr.y + r + 1.f), IM_COL32(0, 0, 0, 180), 2.5f);
        draw->AddLine(ImVec2(scr.x - 1.f, scr.y + r + 1.f), ImVec2(scr.x - r - 1.f, scr.y - 1.f), IM_COL32(0, 0, 0, 180), 2.5f);
        draw->AddLine(ImVec2(scr.x - r - 1.f, scr.y - 1.f), ImVec2(scr.x - 1.f, scr.y - r - 1.f), IM_COL32(0, 0, 0, 180), 2.5f);

        draw->AddLine(ImVec2(scr.x, scr.y - r), ImVec2(scr.x + r, scr.y), col, 1.5f);
        draw->AddLine(ImVec2(scr.x + r, scr.y), ImVec2(scr.x, scr.y + r), col, 1.5f);
        draw->AddLine(ImVec2(scr.x, scr.y + r), ImVec2(scr.x - r, scr.y), col, 1.5f);
        draw->AddLine(ImVec2(scr.x - r, scr.y), ImVec2(scr.x, scr.y - r), col, 1.5f);
        draw->AddCircleFilled(ImVec2(scr.x, scr.y), 2.f, col);

        snprintf(buf, sizeof(buf), "#%d %s", i + 1, g_saved[i].label);
        snprintf(posBuf, sizeof(posBuf), "%.0f %.0f %.0f",
                 g_saved[i].pos.x, g_saved[i].pos.y, g_saved[i].pos.z);

        float tw1 = ImGui::GetFont()->CalcTextSizeA(12.f, FLT_MAX, 0.f, buf).x;
        float tw2 = ImGui::GetFont()->CalcTextSizeA(11.f, FLT_MAX, 0.f, posBuf).x;
        float pillW = (tw1 > tw2 ? tw1 : tw2) + 12.f;
        float pillH = 28.f;
        float px = scr.x + r + 5.f;
        float py = scr.y - 14.f;

        // Pill background + dark outline
        draw->AddRectFilled(ImVec2(px, py), ImVec2(px + pillW, py + pillH), IM_COL32(14, 16, 22, 225), 4.f);
        draw->AddRect(ImVec2(px - 1.f, py - 1.f), ImVec2(px + pillW + 1.f, py + pillH + 1.f), IM_COL32(0, 0, 0, 160), 4.f, 0, 1.5f);
        draw->AddRect(ImVec2(px, py), ImVec2(px + pillW, py + pillH), IM_COL32(65, 75, 95, 180), 4.f, 0, 1.0f);

        if (g_cfg.espShadow) {
            draw->AddText(nullptr, 12.f, ImVec2(px + 6.f + 1.f, py + 2.f + 1.f), IM_COL32(0, 0, 0, 180), buf);
            draw->AddText(nullptr, 11.f, ImVec2(px + 6.f + 1.f, py + 14.f + 1.f), IM_COL32(0, 0, 0, 180), posBuf);
        }
        draw->AddText(nullptr, 12.f, ImVec2(px + 6.f, py + 2.f), txt, buf);
        draw->AddText(nullptr, 11.f, ImVec2(px + 6.f, py + 14.f), IM_COL32(145, 185, 205, 210), posBuf);
    }
}

void DrawCoordRayPreview(const GameCache& cache, int scrW, int scrH) {
    if (!g_cfg.coordSaverEnable || !g_cfg.coordSaverRayFallback || !cache.valid) return;
    float d = g_cfg.coordSaverRayDist;
    if (d < 1.f) d = 1.f;
    Vector2 scr;
    if (!WorldToScreen(RayPoint(cache, d), scr, cache.vm, scrW, scrH)) return;
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    const ImU32 col = IM_COL32(255, 210, 90, 230);
    const float r = 6.f;
    
    // Kreis + Fadenkreuz mit Outline
    draw->AddCircle(ImVec2(scr.x, scr.y), r, IM_COL32(0, 0, 0, 180), 20, 2.5f);
    draw->AddCircle(ImVec2(scr.x, scr.y), r, col, 20, 1.5f);
    draw->AddLine(ImVec2(scr.x - r - 4.f, scr.y), ImVec2(scr.x + r + 4.f, scr.y), IM_COL32(0, 0, 0, 180), 2.2f);
    draw->AddLine(ImVec2(scr.x, scr.y - r - 4.f), ImVec2(scr.x, scr.y + r + 4.f), IM_COL32(0, 0, 0, 180), 2.2f);
    draw->AddLine(ImVec2(scr.x - r - 3.f, scr.y), ImVec2(scr.x + r + 3.f, scr.y), col, 1.2f);
    draw->AddLine(ImVec2(scr.x, scr.y - r - 3.f), ImVec2(scr.x, scr.y + r + 3.f), col, 1.2f);

    char buf[48];
    snprintf(buf, sizeof(buf), "Preview ~%.0fu", d);
    float tw = ImGui::GetFont()->CalcTextSizeA(12.f, FLT_MAX, 0.f, buf).x;
    float px = scr.x + r + 6.f;
    float py = scr.y - 10.f;
    float pillW = tw + 12.f;
    float pillH = 20.f;

    draw->AddRectFilled(ImVec2(px, py), ImVec2(px + pillW, py + pillH), IM_COL32(14, 16, 22, 225), 4.f);
    draw->AddRect(ImVec2(px - 1.f, py - 1.f), ImVec2(px + pillW + 1.f, py + pillH + 1.f), IM_COL32(0, 0, 0, 160), 4.f, 0, 1.5f);
    draw->AddRect(ImVec2(px, py), ImVec2(px + pillW, py + pillH), IM_COL32(65, 75, 95, 180), 4.f, 0, 1.0f);

    if (g_cfg.espShadow)
        draw->AddText(nullptr, 12.f, ImVec2(px + 6.f + 1.f, py + 3.f + 1.f), IM_COL32(0, 0, 0, 180), buf);
    draw->AddText(nullptr, 12.f, ImVec2(px + 6.f, py + 3.f), IM_COL32(255, 225, 140, 240), buf);
}
