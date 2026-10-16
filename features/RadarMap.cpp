#include "pch.h"
#include "RadarMap.h"
#include "../core/Globals.h"
#include "../core/Config.h"
#include "../core/SDK.h"
#include "Cache.h"
#include "../platform/platform.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <cfloat>

#ifdef _WIN32
// Einzige stb-Implementation auf Windows (MenuAssets nutzt dort WIC).
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "../platform/stb_image.h"
#else
// Linux: Implementation liegt in ui/MenuAssets.cpp — hier nur Deklarationen.
#include "../platform/stb_image.h"
#include <GL/gl.h> // GL-Textur-Upload (Kontext ist im Render-Thread current)
#endif

// Diagnose-Spiegel ins Log (Remote-Diagnose, wie ESP.cpp).
extern FILE* g_logFile;
#undef printf
#define printf(...) do { ::printf(__VA_ARGS__); \
    if (g_logFile) { ::fprintf(g_logFile, __VA_ARGS__); ::fflush(g_logFile); } } while (0)

// ─────────────────────────────────────────────────────────────────────────────
// features/RadarMap.cpp — „Radar KOMPLETT": echte Map als Bild.
//
// Externe Radars im UC-Stil rendern die Karte aus den Overview-Daten: pro Map
// ein Radar-Bild + Transform (Spielunits pro Pixel + Ursprungs-Offset). Wir
// nutzen die gepflegten CS2-Radars des boltobserv-Projekts (radar/<map>.png,
// 2048px, Koordinatenraum 1024px) + radar/maps.json mit res/offx/offy.
//
// Welt → Bild (boltobserv-Meta): x_img = (wx + offx) / res,
//                               y_img = 1024 − (wy + offy) / res
//   (offy = Abstand des Weltursprungs von der UNTEREN Kartenkante — y ist
//    im Bild gespiegelt; normalisiert auf 0..1 unabhängig von der PNG-Größe).
//
// Modi: Ganzkarte (statisch, ganze Map im Widget, Zoom um die Mitte) oder
// „drehen" (Spieler zentriert, Karte rotiert mit der Blickrichtung — wie das
// Ingame-Radar). Markiert: Gegner (Farbe nach Sichtbarkeit/Spotted), Teammates,
// C4 (pulsierend), eigener Spieler + Blickkegel. Fällt automatisch auf den
// lokalen Radar zurück, wenn Assets fehlen (siehe RadarMapDrawFull).
//
// Einmalige Lade-Kosten (PNG-Decode + Textur) pro Kartenwechsel, im
// Render-Thread — bewusst einfach, passiert nur beim Mapwechsel.
// ─────────────────────────────────────────────────────────────────────────────

namespace {

struct MapInfo {
    float res  = 0.f;  // Spielunits pro Kartenpixel
    float offx = 0.f;  // Weltursprung: Units von der linken Kante
    float offy = 0.f;  // Weltursprung: Units von der unteren Kante
    bool  ok   = false;
};

std::string g_curMap;      // Basename der aktuell geladenen Map
MapInfo     g_info;
OwO_Tex     g_tex      = nullptr;
uint32_t    g_texTryMs = 0; // letzter Asset-Ladeversuch (Retry-Drossel)

std::string ExeDir() {
#ifdef _WIN32
    char buf[MAX_PATH]{};
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    std::string p(buf);
    size_t slash = p.find_last_of("\\/");
    if (slash == std::string::npos) return std::string("./");
    p.resize(slash + 1);
    return p;
#else
    return OwO_ExeDir(); // mit trailing '/'
#endif
}

bool ReadFileBytes(const std::string& path, std::string& out) {
    FILE* f = nullptr;
#ifdef _WIN32
    if (fopen_s(&f, path.c_str(), "rb") != 0 || !f) return false;
#else
    f = fopen(path.c_str(), "rb");
    if (!f) return false;
#endif
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 16 * 1024 * 1024) { fclose(f); return false; }
    out.resize((size_t)sz);
    size_t rd = fread(&out[0], 1, (size_t)sz, f);
    fclose(f);
    out.resize(rd);
    return rd != 0;
}

// Zahl zu "key": innerhalb des Map-Objekts lesen (bewusst kein JSON-Parser —
// Format ist von uns erzeugt und stabil).
bool JsonNum(const std::string& s, size_t from, const char* key, float& out) {
    size_t k = s.find(std::string("\"") + key + "\"", from);
    if (k == std::string::npos || k > from + 800) return false;
    k = s.find(':', k);
    if (k == std::string::npos) return false;
    out = (float)atof(s.c_str() + k + 1);
    return true;
}

void UnloadTex() {
    if (!g_tex) return;
#ifdef _WIN32
    ((ID3D11ShaderResourceView*)g_tex)->Release();
#else
    GLuint t = (GLuint)(uintptr_t)g_tex;
    if (t) glDeleteTextures(1, &t);
#endif
    g_tex = nullptr;
}

bool LoadTexFromMemory(const std::string& bytes) {
    int w = 0, h = 0, n = 0;
    unsigned char* px = stbi_load_from_memory((const stbi_uc*)bytes.data(),
                                              (int)bytes.size(), &w, &h, &n, 4);
    if (!px || w <= 0 || h <= 0) {
        if (px) stbi_image_free(px);
        return false;
    }
#ifdef _WIN32
    if (!g_d3dDev) { stbi_image_free(px); return false; }
    D3D11_TEXTURE2D_DESC td{};
    td.Width = (UINT)w;
    td.Height = (UINT)h;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA sd{};
    sd.pSysMem = px;
    sd.SysMemPitch = (UINT)w * 4;
    ID3D11Texture2D* tex = nullptr;
    HRESULT hr = g_d3dDev->CreateTexture2D(&td, &sd, &tex);
    if (FAILED(hr) || !tex) { stbi_image_free(px); return false; }
    ID3D11ShaderResourceView* srv = nullptr;
    hr = g_d3dDev->CreateShaderResourceView(tex, nullptr, &srv);
    tex->Release();
    stbi_image_free(px);
    if (FAILED(hr) || !srv) return false;
    g_tex = srv;
#else
    // Linux: GL-Kontext muss beim Aufruf current sein (Render-Thread-Frame).
    GLuint t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glBindTexture(GL_TEXTURE_2D, 0);
    stbi_image_free(px);
    g_tex = (OwO_Tex)(uintptr_t)t;
#endif
    return true;
}

// "workshop/123/de_dust2" → "de_dust2" (Workshop-Kopien offizieller Maps
// nutzen dieselbe Geometrie → unser Radar passt).
std::string BaseMapName(const char* name) {
    std::string s = name ? name : "";
    size_t slash = s.find_last_of('/');
    if (slash != std::string::npos) s = s.substr(slash + 1);
    return s;
}

// Lazy-Load beim Kartenwechsel (oder Retry alle 5 s, falls Assets fehlen).
void EnsureLoaded(const std::string& map, uint32_t now) {
    const bool newMap = (g_curMap != map);
    if (newMap) {
        g_curMap = map;
        g_info = MapInfo{};
        UnloadTex();
        printf("[radar] Map gewechselt: %s\n", map.c_str());
    }
    if (g_info.ok && g_tex) return;                    // fertig geladen
    if (!newMap && now - g_texTryMs < 5000) return;    // Fehlversuch drosseln
    g_texTryMs = now;

    const std::string base = ExeDir() + "radar/";
    if (!g_info.ok) {
        std::string json;
        if (!ReadFileBytes(base + "maps.json", json)) {
            if (newMap) printf("[radar] radar/maps.json fehlt — lokaler Radar bleibt aktiv\n");
            return;
        }
        const std::string key = "\"" + map + "\"";
        size_t k = json.find(key);
        if (k == std::string::npos) {
            if (newMap) printf("[radar] keine Meta fuer '%s' — lokaler Radar bleibt aktiv\n", map.c_str());
            return;
        }
        float v = 0.f;
        bool want = true;
        want = want && JsonNum(json, k, "res", v);  if (want) g_info.res  = v;
        want = want && JsonNum(json, k, "offx", v); if (want) g_info.offx = v;
        want = want && JsonNum(json, k, "offy", v); if (want) g_info.offy = v;
        g_info.ok = want && g_info.res > 0.5f;
        if (!g_info.ok) {
            if (newMap) printf("[radar] Meta unvollstaendig fuer '%s'\n", map.c_str());
            return;
        }
    }
    if (!g_tex) {
        std::string bytes;
        if (!ReadFileBytes(base + map + ".png", bytes)) {
            if (newMap) printf("[radar] Bild fehlt: radar/%s.png\n", map.c_str());
            return;
        }
        if (LoadTexFromMemory(bytes))
            printf("[radar] %s geladen (res=%.2f off=%.0f,%.0f)\n", map.c_str(), g_info.res, g_info.offx, g_info.offy);
        else
            printf("[radar] Bild/Textur fehlgeschlagen: %s\n", map.c_str());
    }
}

} // namespace

bool RadarMapDrawFull(const GameCache& cache, float x, float y, float size) {
    if (!cache.valid || !cache.mapName[0] || !g_cfg.radarFull) return false;
    const std::string map = BaseMapName(cache.mapName);
    if (map.empty() || size < 40.f) return false;
    EnsureLoaded(map, (uint32_t)GetTickCount());
    if (!g_info.ok) return false; // Aufrufer zeichnet den lokalen Radar

    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    float alpha = g_cfg.radarAlpha;
    if (alpha < 0.15f) alpha = 0.15f;
    if (alpha > 1.f) alpha = 1.f;
    const int aImg = (int)(alpha * 255.f);

    float zoom = g_cfg.radarFullZoom;
    if (zoom < 0.4f) zoom = 0.4f;
    if (zoom > 6.f) zoom = 6.f;

    const bool  rot     = g_cfg.radarFullRotate;
    const float yawRad  = cache.localViewAngles.y * kDeg2Rad;
    // Rotation: Blickrichtung → oben. Herleitung: Richtung in Bildkoord. ist
    // (cos yaw, −sin yaw) (y gespiegelt); Drehung um (yaw − 90°) macht sie zu
    // (0,−1) = Bildschirm-oben.
    const float rotAng  = yawRad - 1.57079633f;
    const float rcos    = cosf(rotAng), rsin = sinf(rotAng);

    const float cx = x + size * 0.5f;
    const float cy = y + size * 0.5f;

    // Welt → normalisierte Kartenkoordinate (0..1, y nach unten).
    auto norm = [&](float wx, float wy, float& nx, float& ny) {
        nx = ((wx + g_info.offx) / g_info.res) / 1024.f;
        ny = 1.f - ((wy + g_info.offy) / g_info.res) / 1024.f;
    };

    // Position des lokalen Spielers (immer) + Rotations-/Zoomzentrum:
    // drehend = Spieler; statisch = Kartenmitte.
    float pnx = 0.f, pny = 0.f;
    norm(cache.localOrigin.x, cache.localOrigin.y, pnx, pny);
    const float cnx = rot ? pnx : 0.5f;
    const float cny = rot ? pny : 0.5f;

    auto map2screen = [&](float nx, float ny) -> ImVec2 {
        float dx = (nx - cnx) * 1024.f;
        float dy = (ny - cny) * 1024.f;
        if (rot) {
            const float rx = dx * rcos - dy * rsin;
            const float ry = dx * rsin + dy * rcos;
            dx = rx; dy = ry;
        }
        const float sc = size / 1024.f * zoom;
        return ImVec2(cx + dx * sc, cy + dy * sc);
    };

    // Blickrichtung des lokalen Spielers in Bildkoordinaten (ggf. rotiert).
    auto facingDir = [&]() -> ImVec2 {
        float dx = cosf(yawRad), dy = -sinf(yawRad);
        if (rot) {
            const float rx = dx * rcos - dy * rsin;
            const float ry = dx * rsin + dy * rcos;
            dx = rx; dy = ry;
        }
        return ImVec2(dx, dy);
    };

    dl->PushClipRect(ImVec2(x, y), ImVec2(x + size, y + size), true);

    // Hintergrund (unter dem Bild — fängt PNG-Transparenz sauber ab).
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + size, y + size), IM_COL32(10, 12, 16, (int)(alpha * 235.f)), 4.f);

    // Kartenbild (oder Gitter, falls Bild fehlt aber Meta da ist).
    if (g_tex) {
        dl->AddImageQuad(ImTextureRef((ImTextureID)(uintptr_t)g_tex),
                         map2screen(0.f, 0.f), map2screen(1.f, 0.f),
                         map2screen(1.f, 1.f), map2screen(0.f, 1.f),
                         ImVec2(0.f, 0.f), ImVec2(1.f, 0.f),
                         ImVec2(1.f, 1.f), ImVec2(0.f, 1.f),
                         IM_COL32(255, 255, 255, aImg));
    } else {
        for (int i = 0; i <= 8; ++i) {
            const float f = i / 8.f;
            dl->AddLine(map2screen(f, 0.f), map2screen(f, 1.f), IM_COL32(120, 130, 150, 26), 1.f);
            dl->AddLine(map2screen(0.f, f), map2screen(1.f, f), IM_COL32(120, 130, 150, 26), 1.f);
        }
    }

    // ── Blickkegel des lokalen Spielers ────────────────────────────────────
    ImVec2 lpOut = map2screen(pnx, pny);
    if (g_cfg.radarShowViewCone) {
        const ImVec2 d = facingDir();
        const float a0 = atan2f(d.y, d.x);
        const float coneLen = rot ? size * 0.34f : 40.f * zoom;
        const float coneHalf = 35.f * kDeg2Rad;
        const ImVec2 p1(lpOut.x + cosf(a0 - coneHalf) * coneLen, lpOut.y + sinf(a0 - coneHalf) * coneLen);
        const ImVec2 p2(lpOut.x + cosf(a0 + coneHalf) * coneLen, lpOut.y + sinf(a0 + coneHalf) * coneLen);
        dl->AddTriangleFilled(lpOut, p1, p2, IM_COL32(90, 150, 255, 24));
        dl->AddLine(lpOut, p1, IM_COL32(110, 170, 255, 46), 1.2f);
        dl->AddLine(lpOut, p2, IM_COL32(110, 170, 255, 46), 1.2f);
    }

    // ── Spieler ────────────────────────────────────────────────────────────
    const uint32_t ageMs = (uint32_t)(GetTickCount() - cache.stampMs);
    const bool teamsKnown = (cache.localTeam == 2 || cache.localTeam == 3);
    for (int i = 0; i < cache.playerCount; ++i) {
        const PlayerData& p = cache.players[i];
        if (!p.alive) continue;
        const bool enemy = !teamsKnown || (p.team != cache.localTeam);
        if (enemy ? !g_cfg.radarShowEnemies : !g_cfg.radarShowTeam) continue;

        const Vector3 pp = PredictPos(p.worldOrigin, p.velocity, ageMs);
        float nx = 0.f, ny = 0.f;
        norm(pp.x, pp.y, nx, ny);
        if (nx < -0.03f || nx > 1.03f || ny < -0.03f || ny > 1.03f) continue; // außerhalb (Map-Splits)
        const ImVec2 sp = map2screen(nx, ny);

        ImU32 col;
        if (enemy)
            col = p.visible ? IM_COL32(90, 245, 110, 235)
                            : (p.radarSpotted ? IM_COL32(245, 190, 65, 235)
                                              : IM_COL32(245, 75, 75, 235));
        else
            col = IM_COL32(80, 160, 255, 235);

        const float r = enemy ? 5.f : 4.f;
        dl->AddCircleFilled(sp, r, col, 12);
        dl->AddCircle(sp, r, IM_COL32(0, 0, 0, 180), 12, 1.2f);

        // Bewegungsvektor (Richtung entlang der Karte, rotiert mit).
        const float vlen = sqrtf(p.velocity.x * p.velocity.x + p.velocity.y * p.velocity.y);
        if (vlen > 30.f) {
            float dxn = p.velocity.x / vlen, dyn = -p.velocity.y / vlen; // Bildkoord. (y gespiegelt)
            if (rot) {
                const float rx = dxn * rcos - dyn * rsin;
                const float ry = dxn * rsin + dyn * rcos;
                dxn = rx; dyn = ry;
            }
            dl->AddLine(sp, ImVec2(sp.x + dxn * 8.f, sp.y + dyn * 8.f), col, 1.5f);
        }
    }

    // ── C4 (Plant/Dropped): pulsiert ───────────────────────────────────────
    if (g_cfg.radarFullBomb) {
        const GroundCache& gr = SnapFront().ground;
        const float pulse = 0.5f + 0.5f * sinf((float)GetTickCount() * 0.006f);
        const int ba = (int)(150.f + 100.f * pulse);
        for (int i = 0; i < gr.count && i < MAX_GROUND_ITEMS; ++i) {
            const GroundItem& it = gr.items[i];
            if (it.type != 1 || it.isProj) continue;
            float nx = 0.f, ny = 0.f;
            norm(it.origin.x, it.origin.y, nx, ny);
            if (nx < -0.03f || nx > 1.03f || ny < -0.03f || ny > 1.03f) continue;
            const ImVec2 sp = map2screen(nx, ny);
            dl->AddRectFilled(ImVec2(sp.x - 4.f, sp.y - 4.f), ImVec2(sp.x + 4.f, sp.y + 4.f),
                              IM_COL32(255, 60, 60, ba), 2.f);
            dl->AddRect(ImVec2(sp.x - 4.f, sp.y - 4.f), ImVec2(sp.x + 4.f, sp.y + 4.f),
                        IM_COL32(0, 0, 0, 200), 2.f, 0, 1.2f);
            dl->AddText(nullptr, 10.f, ImVec2(sp.x + 6.f, sp.y - 7.f), IM_COL32(255, 205, 205, 235), "C4");
        }
    }

    // ── Lokaler Spieler (über allem) ───────────────────────────────────────
    dl->AddCircleFilled(lpOut, 4.5f, IM_COL32(255, 255, 255, 255), 12);
    dl->AddCircle(lpOut, 4.5f, IM_COL32(0, 0, 0, 180), 12, 1.2f);
    {
        const ImVec2 d = facingDir();
        dl->AddLine(lpOut, ImVec2(lpOut.x + d.x * 11.f, lpOut.y + d.y * 11.f),
                    IM_COL32(255, 255, 255, 255), 2.2f);
    }

    dl->PopClipRect();

    // ── Rahmen + Label ─────────────────────────────────────────────────────
    if (g_cfg.radarBorder) {
        dl->AddRect(ImVec2(x - 1.f, y - 1.f), ImVec2(x + size + 1.f, y + size + 1.f),
                    IM_COL32(0, 0, 0, 160), 6.f, 0, 1.5f);
        dl->AddRect(ImVec2(x, y), ImVec2(x + size, y + size),
                    IM_COL32(65, 75, 95, 180), 6.f, 0, 1.0f);
    }
    {
        const char* label = rot ? "MAP *" : "MAP";
        const float labW = ImGui::GetFont()->CalcTextSizeA(11.f, FLT_MAX, 0.f, label).x;
        dl->AddRectFilled(ImVec2(x + 5.f, y + 4.f), ImVec2(x + labW + 13.f, y + 18.f),
                          IM_COL32(10, 12, 16, 200), 3.f);
        if (g_cfg.espShadow)
            dl->AddText(nullptr, 11.f, ImVec2(x + 10.f, y + 5.f), IM_COL32(0, 0, 0, 180), label);
        dl->AddText(nullptr, 11.f, ImVec2(x + 9.f, y + 4.f), IM_COL32(200, 210, 225, 210), label);
    }

    return true;
}
