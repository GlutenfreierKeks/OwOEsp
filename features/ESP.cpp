#include "pch.h"
#include "ESP.h"
#include "Offsets.h"
#include "Weapons.h"
#include "RadarMap.h"
#include "../core/Globals.h"
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <string_view>
#include <cctype>
#include <cstring>
#include <cfloat>
#include <vector>
#include <cstdio>

#ifdef __linux__
#include <GL/gl.h> // Textur-Upload (Linux-Only; Kontext muss current sein)
#endif

// Diagnose-Spiegel ins Log (Remote-Diagnose).
extern FILE* g_logFile;
#undef printf
#define printf(...) do { ::printf(__VA_ARGS__); \
    if (g_logFile) { ::fprintf(g_logFile, __VA_ARGS__); ::fflush(g_logFile); } } while (0)

#define NANOSVG_IMPLEMENTATION
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvgrast.h"

// ─────────────────────────────────────────────────────────────────────────────
// features/ESP.cpp — alle sichtbaren Overlay-Elemente
// OPTIMIERT: Batching, Culling, Spotted, Glow-Style, Radar
// Läuft ausschliesslich im Render-Thread; Memory-Reads passieren im Reader.
// ─────────────────────────────────────────────────────────────────────────────

// ── SVG-Textur-Cache (Waffen-Icons, in der EXE eingebettet) ─────────────────
// Quelle 1: RCDATA-Resources (resources/ItemIcons.rc, per gen_icons.ps1 aus
//   Gun_textures\ erzeugt — beim Build in der EXE, kein Ordner nötig).
// Quelle 2 (Fallback): Gun_textures\ neben der EXE (exe-relativ, KEIN
//   hartkodierter Dev-Pfad mehr — der alte absolute Pfad ist raus).
// Geladen wird einmalig per PreloadWeaponTextures() beim Start, danach nur
// noch Map-Lookup pro Frame (kein SVG-Parse mehr zur Laufzeit -> keine Lags
// beim ersten Sehen einer Waffe).
namespace {
    struct TexInfo {
        OwO_Tex srv = nullptr;
        int w = 0, h = 0;
    };

    // Heterogener Lookup: find(const char*) ohne Heap-Alloc (C++20).
    struct StrViewHash {
        using is_transparent = void;
        size_t operator()(std::string_view s) const noexcept {
            return std::hash<std::string_view>{}(s);
        }
    };
    struct StrViewEq {
        using is_transparent = void;
        bool operator()(std::string_view a, std::string_view b) const noexcept {
            return a == b;
        }
    };
    std::unordered_map<std::string, TexInfo, StrViewHash, StrViewEq> g_itemTextures;

    static std::string ExeDir() {
#ifdef _WIN32
        char buf[MAX_PATH]{};
        GetModuleFileNameA(nullptr, buf, MAX_PATH);
        char* slash = strrchr(buf, '\\');
        if (slash) slash[1] = '\0';
        return std::string(buf);
#else
        return OwO_ExeDir();
#endif
    }

#ifdef _WIN32
    const char* EmbeddedSvg(const char* svgName, DWORD& outSize) {
        // rc.exe schreibt String-Namen GROSS ("AK47.SVG"), FindResource ist
        // case-sensitiv — darum Lookup immer in Uppercase.
        char upper[64];
        size_t n = strlen(svgName);
        if (n == 0 || n >= sizeof(upper)) return nullptr;
        for (size_t i = 0; i <= n; i++)
            upper[i] = (char)toupper((unsigned char)svgName[i]);
        HRSRC hRes = FindResourceA(nullptr, upper, MAKEINTRESOURCEA(10)); // RT_RCDATA
        if (!hRes) return nullptr;
        HGLOBAL hGlob = LoadResource(nullptr, hRes);
        if (!hGlob) return nullptr;
        DWORD size = SizeofResource(nullptr, hRes);
        const char* data = (const char*)LockResource(hGlob);
        if (!data || size == 0) return nullptr;
        outSize = size;
        return data;
    }
#endif

    // reason: 0=ok, 1=keine Resource, 2=Parse-Fehler, 3=auch Datei-Fallback fehlt
#ifdef _WIN32
    TexInfo LoadSvgTexture(const char* svgName, ID3D11Device* device, int* reason = nullptr) {
#else
    TexInfo LoadSvgTexture(const char* svgName, int* reason = nullptr) {
#endif
        auto setReason = [&](int r) { if (reason) *reason = r; };
        TexInfo result{};
        NSVGimage* image = nullptr;
        std::string embeddedText;
        DWORD embSize = 0;
#ifdef _WIN32
        const char* emb = EmbeddedSvg(svgName, embSize);
#else
        const char* emb = nullptr;
#endif
        if (!emb || embSize == 0) {
            setReason(1);
        } else {
            embeddedText.assign(emb, embSize);
            image = nsvgParse(embeddedText.data(), "px", 96.0f);
            if (!image) {
                printf("[tex] PARSE-FAIL %s (%u bytes embedded, nanosvg kommt nicht klar)\n",
                       svgName, embSize);
                setReason(2);
            }
        }
        if (!image) {
            // Fallback: Gun_textures neben der EXE (portabel, kein Dev-Pfad)
            std::string fullPath = ExeDir() + "Gun_textures/" + svgName;
            image = nsvgParseFromFile(fullPath.c_str(), "px", 96.0f);
            if (!image) {
                // Einmalig pro Name melden (kein Konsolen-Spam im Frame)
                static std::unordered_set<std::string> s_warned;
                if (s_warned.insert(svgName).second)
                    printf("[tex] missing %s (embedded=%s, Datei=%s)\n", svgName,
                           emb ? "da" : "FEHLT",
                           "nicht neben EXE gefunden — text fallback");
                if (!reason || *reason == 0) setReason(3);
                return result;
            }
            setReason(0);
        } else {
            setReason(0);
        }
        int origW = (int)image->width;
        int origH = (int)image->height;
        if (origW <= 0 || origH <= 0) { origW = 32; origH = 32; }
        const int MAX_SIZE = 64;
        float scale = 1.0f;
        if (origW > MAX_SIZE || origH > MAX_SIZE) {
            float sx = (float)MAX_SIZE / origW;
            float sy = (float)MAX_SIZE / origH;
            scale = (sx < sy) ? sx : sy;
        }
        int w = (int)(origW * scale);
        int h = (int)(origH * scale);
        if (w <= 0) w = 1;
        if (h <= 0) h = 1;
        std::vector<unsigned char> pixels(w * h * 4, 0);
        NSVGrasterizer* rast = nsvgCreateRasterizer();
        if (!rast) { nsvgDelete(image); return result; }
        nsvgRasterize(rast, image, 0, 0, scale, pixels.data(), w, h, w * 4);
        nsvgDeleteRasterizer(rast);
        nsvgDelete(image);
#ifdef _WIN32
        D3D11_TEXTURE2D_DESC td{};
        td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA sd{}; sd.pSysMem = pixels.data(); sd.SysMemPitch = w * 4;
        ID3D11Texture2D* tex = nullptr;
        HRESULT hr = device->CreateTexture2D(&td, &sd, &tex);
        if (FAILED(hr) || !tex) { printf("[tex] CreateTexture2D failed for %s\n", svgName); return result; }
        ID3D11ShaderResourceView* srv = nullptr;
        device->CreateShaderResourceView(tex, nullptr, &srv);
        tex->Release();
        if (srv) { result.srv = srv; result.w = w; result.h = h; }
#else
        // Linux: RGBA-Upload ins aktuelle GL (Kontext muss current sein).
        unsigned int tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, pixels.data());
        glBindTexture(GL_TEXTURE_2D, 0);
        if (tex) {
            result.srv = (OwO_Tex)(uintptr_t)tex;
            result.w = w;
            result.h = h;
        } else {
            printf("[tex] glTexImage2D failed for %s\n", svgName);
        }
#endif
        return result;
    }

    // Schreibt "<name>.svg" nach buf (kein Heap im Frame). Literale für Bekannte.
    const char* ItemSvgName(const GroundItem& item, char* buf, size_t len) {
        const char* n = item.name;
        if (strcmp(n, "C4") == 0) return "c4.svg";
        if (strcmp(n, "Planted C4") == 0) return "planted_c4.svg";
        if (strcmp(n, "Smoke") == 0) return "smokegrenade.svg";
        if (strcmp(n, "Molotov") == 0) return "molotov.svg";
        if (strcmp(n, "Incendiary") == 0) return "incgrenade.svg";
        if (strcmp(n, "HE") == 0) return "hegrenade.svg";
        if (strcmp(n, "Flash") == 0) return "flashbang.svg";
        if (strcmp(n, "Decoy") == 0) return "decoy.svg";
        if (strcmp(n, "Knife") == 0 || strcmp(n, "knife") == 0) return "knife.svg";
        snprintf(buf, len, "%s.svg", n);
        return buf;
    }

    const TexInfo* FindTexture(const char* svgFile) {
        auto it = g_itemTextures.find(svgFile);
        return it != g_itemTextures.end() ? &it->second : nullptr;
    }

#ifdef _WIN32
    const TexInfo* GetOrLoadTexture(const char* svgFile, ID3D11Device* device) {
#else
    const TexInfo* GetOrLoadTexture(const char* svgFile) {
#endif
        auto it = g_itemTextures.find(svgFile);
        if (it != g_itemTextures.end()) return &it->second;
        int reason = -1;
#ifdef _WIN32
        TexInfo ti = LoadSvgTexture(svgFile, device, &reason);
#else
        TexInfo ti = LoadSvgTexture(svgFile, &reason);
#endif
        if (ti.srv) { g_itemTextures[svgFile] = ti; return &g_itemTextures[svgFile]; }
        return nullptr;
    }
}

void ReleaseItemTextures() {
    for (auto& kv : g_itemTextures) {
#ifdef _WIN32
        if (kv.second.srv) kv.second.srv->Release();
#else
        if (kv.second.srv) {
            unsigned int tex = (unsigned int)(uintptr_t)kv.second.srv;
            glDeleteTextures(1, &tex);
        }
#endif
    }
    g_itemTextures.clear();
}

// ── Einheitliches ESP-Designsystem ─────────────────────────────────────────
// EINE Typo (Header 13 / Labels 12), EINE Schatten-Logik, EINE Farbquelle pro
// Spieler. Misst Texte mit derselben Größe wie gezeichnet (ImGui misst sonst
// mit Default-Size -> schief zentriert, alter Bug).
namespace {
    constexpr float kEspHeader = 13.f; // Name
    constexpr float kEspLabel  = 12.f; // alles andere (Spotted/HP/Dist/Waffe)
    constexpr ImU32 kEspShadow = IM_COL32(0, 0, 0, 180);

    inline float EspTextW(const char* txt, float size) {
        return ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0.f, txt).x;
    }

    inline void EspText(ImDrawList* draw, float x, float y, float size,
                        ImU32 col, const char* txt) {
        if (g_cfg.espShadow)
            draw->AddText(nullptr, size, ImVec2(x + 1.f, y + 1.f), kEspShadow, txt);
        draw->AddText(nullptr, size, ImVec2(x, y), col, txt);
    }

    // Zentriert auf cx ( misst mit Draw-Size -> exakt mittig )
    inline void EspTextC(ImDrawList* draw, float cx, float y, float size,
                         ImU32 col, const char* txt) {
        EspText(draw, cx - EspTextW(txt, size) * 0.5f, y, size, col, txt);
    }

    // IM_COL32 ist ABGR-gepackt: Kanäle exakt zerlegen (Glow-Bugfix — vorher
    // waren Rot/Blau vertauscht).
    inline ImU32 WithAlpha(ImU32 col, int alpha) {
        if (alpha < 0) alpha = 0;
        if (alpha > 255) alpha = 255;
        return (col & 0x00FFFFFFu) | ((ImU32)alpha << 24);
    }
    inline void SplitCol(ImU32 col, int& r, int& g, int& b) {
        r = (int)(col & 0xFF);
        g = (int)((col >> 8) & 0xFF);
        b = (int)((col >> 16) & 0xFF);
    }
} // namespace

size_t ItemTextureCount() { return g_itemTextures.size(); }

size_t ItemTextureBytes() {
    size_t bytes = 0;
    for (auto& kv : g_itemTextures)
        if (kv.second.srv) bytes += (size_t)kv.second.w * (size_t)kv.second.h * 4;
    return bytes;
}

// Alle bekannten Waffen-/Item-Icons EINMAL beim Start in den GPU-Cache.
// Danach kein SVG-Parse mehr im Frame (keine Hitches beim ersten Anblick).
#ifdef _WIN32
void PreloadWeaponTextures(ID3D11Device* device) {
    if (!device) return;
#else
void PreloadWeaponTextures() {
#endif
    static bool done = false;
    if (done) return;
    done = true;
    const char* files[] = {
        "ak47.svg","aug.svg","awp.svg","bizon.svg","c4.svg","cz75a.svg","deagle.svg",
        "decoy.svg","elite.svg","famas.svg","fiveseven.svg","flashbang.svg","g3sg1.svg",
        "galilar.svg","glock.svg","hegrenade.svg","hkp2000.svg","incgrenade.svg",
        "knife.svg","m249.svg","m4a1.svg","m4a1_silencer.svg","mac10.svg","mag7.svg",
        "molotov.svg","mp5sd.svg","mp7.svg","mp9.svg","negev.svg","nova.svg","p2000.svg",
        "p250.svg","p90.svg","planted_c4.svg","revolver.svg","sawedoff.svg","scar20.svg",
        "sg556.svg","smokegrenade.svg","ssg08.svg","taser.svg","tec9.svg","ump45.svg",
        "usp_silencer.svg","xm1014.svg",
    };
    int ok = 0, noRes = 0, parseFail = 0;
    for (const char* f : files) {
        int reason = -1;
#ifdef _WIN32
        TexInfo ti = LoadSvgTexture(f, device, &reason);
#else
        TexInfo ti = LoadSvgTexture(f, &reason);
#endif
        if (ti.srv) { g_itemTextures[f] = ti; ok++; }
        else if (reason == 1) noRes++;
        else parseFail++;
    }
    printf("[tex] Preload: %d/%d ok (keineResource=%d parseFail=%d)\n",
           ok, (int)(sizeof(files)/sizeof(files[0])), noRes, parseFail);
}

// ── Skeleton Helpers — korrigiert: Haende 11/16 statt 10/15 (TKazer), Fueße getrennt
static const int kSkeletonPairs[][2] = {
    {6,5},{5,4},{4,2},{2,0},
    {4,8},{8,9},{9,11},
    {4,13},{13,14},{14,16},
    {0,22},{22,23},{23,24},
    {0,25},{25,26},{26,27}
};
static void DrawSkeleton(const PlayerData& p, ImDrawList* draw, ImU32 col, float thick,
                         const Matrix4x4& vm, int scrW, int scrH, DWORD ageMs) {
    if (!p.hasBones) return;
    Vector2 proj[kBoneCount]{};
    bool ok[kBoneCount]{};
    for (int b = 0; b < kBoneCount; b++) {
        if (!p.bones[b].ok) continue;
        ok[b] = WorldToScreen(PredictPos(p.boneWorld[b], p.velocity, ageMs), proj[b], vm, scrW, scrH);
    }

    const int baseA = (int)((col >> 24) & 0xFF);
    const ImU32 core = IM_COL32(255, 255, 255, (int)(baseA * 0.35f));

    // 1. Knochen-Linien in drei Lagen: dunkle Outline -> Farbe -> heller Kern
    //    (Premium-Look, klar lesbar auf jeder Map-Farbe).
    for (auto& pr : kSkeletonPairs) {
        int a = pr[0], b = pr[1];
        if (a < 0 || a >= kBoneCount || b < 0 || b >= kBoneCount) continue;
        if (!ok[a] || !ok[b]) continue;
        draw->AddLine(ImVec2(proj[a].x, proj[a].y), ImVec2(proj[b].x, proj[b].y),
                      IM_COL32(0, 0, 0, (int)(baseA * 0.75f)), thick + 2.2f);
    }
    for (auto& pr : kSkeletonPairs) {
        int a = pr[0], b = pr[1];
        if (a < 0 || a >= kBoneCount || b < 0 || b >= kBoneCount) continue;
        if (!ok[a] || !ok[b]) continue;
        draw->AddLine(ImVec2(proj[a].x, proj[a].y), ImVec2(proj[b].x, proj[b].y), col, thick);
    }
    for (auto& pr : kSkeletonPairs) {
        int a = pr[0], b = pr[1];
        if (a < 0 || a >= kBoneCount || b < 0 || b >= kBoneCount) continue;
        if (!ok[a] || !ok[b]) continue;
        draw->AddLine(ImVec2(proj[a].x, proj[a].y), ImVec2(proj[b].x, proj[b].y),
                      core, thick * 0.45f);
    }

    // 2. Kopf-Ring: Radius aus Hals->Kopf-Abstand (skaliert automatisch mit
    //    der Distanz — sieht auf jeder Entfernung richtig aus).
    if (ok[5] && ok[6]) {
        float hx = proj[6].x - proj[5].x, hy = proj[6].y - proj[5].y;
        float r = sqrtf(hx*hx + hy*hy) * 1.25f;
        if (r < 4.f) r = 4.f; else if (r > 42.f) r = 42.f;
        draw->AddCircle(ImVec2(proj[6].x, proj[6].y), r, IM_COL32(0, 0, 0, (int)(baseA * 0.7f)), 28, thick + 1.8f);
        draw->AddCircle(ImVec2(proj[6].x, proj[6].y), r, col, 28, thick);
    }

    // 3. Gelenkpunkte (Joint Dots) — dunkle Kante + Farbkern
    float dotR = (thick * 0.95f > 1.9f) ? (thick * 0.95f) : 1.9f;
    for (int b = 0; b < kBoneCount; b++) {
        if (!ok[b]) continue;
        draw->AddCircleFilled(ImVec2(proj[b].x, proj[b].y), dotR + 1.1f,
                              IM_COL32(0, 0, 0, (int)(baseA * 0.75f)), 10);
        draw->AddCircleFilled(ImVec2(proj[b].x, proj[b].y), dotR, col, 10);
    }
}

// ── Crosshair (7 Stile: Simple/Dot/Circle/CrossDot/Star/X/Swastika) ─────────
// + Rotate: dreht das Fadenkreuz dauerhaft (RotateSpeed in Grad/Sek).
static void DrawCrosshair(ImDrawList* draw, int scrW, int scrH) {
    if (!g_cfg.crosshairEnable) return;
    float cx = scrW * 0.5f, cy = scrH * 0.5f;
    float size = g_cfg.crosshairSize, gap = g_cfg.crosshairGap, thick = g_cfg.crosshairThickness;
    if (gap > size) gap = size * 0.3f;
    if (thick < 0.5f) thick = 0.5f;
    int style = g_cfg.crosshairStyle;
    if (style < 0 || style > 6) style = 0;
    ImColor col(g_cfg.crosshairColor[0], g_cfg.crosshairColor[1], g_cfg.crosshairColor[2], g_cfg.crosshairColor[3]);
    ImColor outline(0.f, 0.f, 0.f, g_cfg.crosshairColor[3] * 0.6f);
    float ot = thick + 2.f;
    ImVec2 c(cx, cy);

    // Rotationswinkel (Bogenmaß): dauerhaft rotierend wenn aktiviert.
    float rotRad = 0.f;
    if (g_cfg.crosshairRotate) {
        float deg = fmodf((float)(ImGui::GetTime() * (double)g_cfg.crosshairRotateSpeed), 360.f);
        rotRad = deg * 0.0174533f;
    }
    float rc = cosf(rotRad), rs = sinf(rotRad);
    auto rot = [&](float x, float y) -> ImVec2 {
        float dx = x - cx, dy = y - cy;
        return ImVec2(cx + dx * rc - dy * rs, cy + dx * rs + dy * rc);
    };
    auto rline = [&](float x0, float y0, float x1, float y1, ImU32 color, float th) {
        draw->AddLine(rot(x0, y0), rot(x1, y1), color, th);
    };

    auto dot = [&](float r, ImU32 color) {
        draw->AddCircleFilled(c, r, color, 12);
    };
    auto crossLines = [&](ImU32 color, float th) {
        rline(cx - gap - size, cy, cx - gap, cy, color, th);
        rline(cx + gap, cy, cx + gap + size, cy, color, th);
        rline(cx, cy - gap - size, cx, cy - gap, color, th);
        rline(cx, cy + gap, cx, cy + gap + size, color, th);
    };

    switch (style) {
    case 1: { // Punkt
        float r = size * 0.25f;
        if (r < 1.5f) r = 1.5f;
        dot(r + 1.f, IM_COL32(0, 0, 0, (int)(g_cfg.crosshairColor[3] * 150)));
        dot(r, col);
        break;
    }
    case 2: // Kreis
        draw->AddCircle(c, size, outline, 32, ot);
        draw->AddCircle(c, size, col, 32, thick);
        if (g_cfg.crosshairDot) dot(1.5f, col);
        break;
    case 3: // Kreuz + Punkt
        crossLines(outline, ot);
        crossLines(col, thick);
        dot(thick + 0.5f, col);
        break;
    case 4: { // Stern (Davidstern: zwei Dreiecke)
        float r = size;
        float rotDeg = g_cfg.crosshairRotate ? fmodf((float)(ImGui::GetTime() * (double)g_cfg.crosshairRotateSpeed), 360.f) : 0.f;
        for (int tri = 0; tri < 2; tri++) {
            float base = (tri == 0 ? -90.f : 90.f) + rotDeg;
            ImVec2 p[3];
            for (int k = 0; k < 3; k++) {
                float a = (base + k * 120.f) * 0.0174533f;
                p[k] = ImVec2(cx + cosf(a) * r, cy + sinf(a) * r);
            }
            draw->AddTriangle(p[0], p[1], p[2], outline, ot);
            draw->AddTriangle(p[0], p[1], p[2], col, thick);
        }
        if (g_cfg.crosshairDot) dot(1.5f, col);
        break;
    }
    case 5: { // X
        float d0 = gap, d1 = gap + size;
        const float inv = 0.7071f;
        auto xline = [&](float a, float b, ImU32 color, float th) {
            rline(cx - b*inv, cy - b*inv, cx - a*inv, cy - a*inv, color, th);
            rline(cx + a*inv, cy + a*inv, cx + b*inv, cy + b*inv, color, th);
            rline(cx + b*inv, cy - b*inv, cx + a*inv, cy - a*inv, color, th);
            rline(cx - a*inv, cy + a*inv, cx - b*inv, cy + b*inv, color, th);
        };
        xline(d0, d1, outline, ot);
        xline(d0, d1, col, thick);
        if (g_cfg.crosshairDot) dot(1.5f, col);
        break;
    }
    case 6: { // Swastika (4 Arme mit Haken, im Uhrzeigersinn)
        float L = size;          // Armlaenge ab Gap
        float H = size * 0.6f;   // Hakenlaenge
        if (H < 3.f) H = 3.f;
        auto swas = [&](ImU32 color, float th) {
            // Oben: vertikal + Haken rechts
            rline(cx, cy - gap - L, cx, cy - gap, color, th);
            rline(cx, cy - gap - L, cx + H, cy - gap - L, color, th);
            // Rechts: horizontal + Haken runter
            rline(cx + gap, cy, cx + gap + L, cy, color, th);
            rline(cx + gap + L, cy, cx + gap + L, cy + H, color, th);
            // Unten: vertikal + Haken links
            rline(cx, cy + gap, cx, cy + gap + L, color, th);
            rline(cx, cy + gap + L, cx - H, cy + gap + L, color, th);
            // Links: horizontal + Haken hoch
            rline(cx - gap - L, cy, cx - gap, cy, color, th);
            rline(cx - gap - L, cy, cx - gap - L, cy - H, color, th);
        };
        swas(outline, ot);
        swas(col, thick);
        if (g_cfg.crosshairDot) dot(1.5f, col);
        break;
    }
    default: // 0 Simple: dünnes Kreuz, eng — tuff Standard
        crossLines(outline, ot);
        crossLines(col, thick);
        if (g_cfg.crosshairDot) dot(1.5f, col);
        break;
    }
}

// ── Test-ESP ─────────────────────────────────────────────────────────────────
static void DrawTestESP(ImDrawList* draw, int scrW, int scrH) {
    float fw = (float)scrW, fh = (float)scrH;
    draw->AddRect(ImVec2(50, 50), ImVec2(150, 150), ImColor(1.f,0.f,0.f,1.f), 0.f,0,2.f);
    draw->AddText(nullptr, 14, ImVec2(55,155), ImColor(1.f,1.f,1.f,1.f), "TEST A");
    draw->AddRect(ImVec2(fw-150,50), ImVec2(fw-50,150),ImColor(0.f,1.f,0.f,1.f), 0.f,0,2.f);
    draw->AddText(nullptr, 14, ImVec2(fw-145,155), ImColor(1.f,1.f,1.f,1.f), "TEST B");
    draw->AddRect(ImVec2(fw*.5f-100,fh-150), ImVec2(fw*.5f+100,fh-50), ImColor(0.f,0.6f,1.f,1.f), 0.f,0,2.f);
    draw->AddText(nullptr,14,ImVec2(fw*.5f-40,fh-145),ImColor(1.f,1.f,1.f,1.f),"TEST C");
}

// ── HUD-Overlay — Fadenkreuz + Aim-FOV-Kreise ───────────────────────────────
// Reine Bildschirm-Elemente: haengen bewusst NICHT am ESP-Master (enabled) —
// vorher verschwanden sie mit ausgeschaltetem ESP.
void DrawHudOverlay(int scrW, int scrH) {
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    if (g_cfg.aimEnable) {
        draw->AddCircle(ImVec2(scrW*.5f, scrH*.5f), g_cfg.aimFov, IM_COL32(100,110,240,40), 64, 1.f);
    }
    if (g_cfg.chickenAimEnable) {
        draw->AddCircle(ImVec2(scrW*.5f, scrH*.5f), g_cfg.chickenAimFov, IM_COL32(100,240,140,40), 64, 1.f);
    }
    if (g_cfg.ballAimEnable) {
        draw->AddCircle(ImVec2(scrW*.5f, scrH*.5f), g_cfg.ballAimFov, IM_COL32(240,200,100,40), 64, 1.f);
    }
    if (g_cfg.markerAimEnable) {
        draw->AddCircle(ImVec2(scrW*.5f, scrH*.5f), g_cfg.markerAimFov, IM_COL32(220,220,230,40), 64, 1.f);
    }
    DrawCrosshair(draw, scrW, scrH);
}

// ── Haupt-ESP — OPTIMIERT ────────────────────────────────────────────────────
void DrawESP(const GameCache& cache, int scrW, int scrH) {
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    if (g_cfg.testEsp) DrawTestESP(draw, scrW, scrH);

    // Einheitliche dezentrale Watermark-Pille oben links
    {
        const char* wmText = "PixelMommySense";
        float wmW = ImGui::GetFont()->CalcTextSizeA(11.f, FLT_MAX, 0.f, wmText).x;
        draw->AddRectFilled(ImVec2(10.f, 10.f), ImVec2(10.f + wmW + 14.f, 26.f), IM_COL32(14, 16, 22, 190), 4.f);
        draw->AddRect(ImVec2(9.f, 9.f), ImVec2(10.f + wmW + 15.f, 27.f), IM_COL32(0, 0, 0, 140), 4.f, 0, 1.2f);
        draw->AddRect(ImVec2(10.f, 10.f), ImVec2(10.f + wmW + 14.f, 26.f), IM_COL32(65, 75, 95, 160), 4.f, 0, 1.0f);
        if (g_cfg.espShadow)
            draw->AddText(nullptr, 11.f, ImVec2(17.f + 1.f, 12.f + 1.f), IM_COL32(0, 0, 0, 180), wmText);
        draw->AddText(nullptr, 11.f, ImVec2(17.f, 12.f), IM_COL32(160, 180, 240, 220), wmText);
    }

    if (!g_cfg.enabled || !cache.valid || cache.playerCount==0) return;

    // ── Precompute per-frame colors ─────────────────────────────────────────
    static float rainbowHue = 0.f;
    if (g_cfg.rainbow) {
        rainbowHue += ImGui::GetIO().DeltaTime * g_cfg.rainbowSpeed;
        if (rainbowHue > 1.f) rainbowHue -= 1.f;
    }
    ImU32 teamU32 = IM_COL32((int)(g_cfg.teamColor[0]*255),(int)(g_cfg.teamColor[1]*255),(int)(g_cfg.teamColor[2]*255),(int)(g_cfg.teamColor[3]*255));
    ImU32 enemyU32 = IM_COL32((int)(g_cfg.enemyColor[0]*255),(int)(g_cfg.enemyColor[1]*255),(int)(g_cfg.enemyColor[2]*255),(int)(g_cfg.enemyColor[3]*255));
    ImU32 spottedU32 = IM_COL32((int)(g_cfg.spottedColor[0]*255),(int)(g_cfg.spottedColor[1]*255),(int)(g_cfg.spottedColor[2]*255),(int)(g_cfg.spottedColor[3]*255));
    ImU32 hiddenU32 = IM_COL32((int)(g_cfg.hiddenColor[0]*255),(int)(g_cfg.hiddenColor[1]*255),(int)(g_cfg.hiddenColor[2]*255),(int)(g_cfg.hiddenColor[3]*255));
    ImU32 rainbowU32 = 0;
    if (g_cfg.rainbow) {
        float r,g,b; ImGui::ColorConvertHSVtoRGB(rainbowHue, g_cfg.rainbowSat, g_cfg.rainbowVal, r,g,b);
        rainbowU32 = IM_COL32((int)(r*255),(int)(g*255),(int)(b*255),255);
    }
    const float scrWf = (float)scrW, scrHf = (float)scrH;

    // Health text buffer reused (avoid per-frame alloc)
    char hpBuf[8];

    for (int i = 0; i < cache.playerCount; i++) {
        const PlayerData& p = cache.players[i];
        if (!p.alive) continue;
        bool enemy = p.team != cache.localTeam;

        // ── Spotted Filter ─────────────────────────────────────────────────
        bool isVisible = p.visible;
        if (enemy) {
            if (g_cfg.visibleOnly && !isVisible) continue;
            if (g_cfg.spottedEnable && g_cfg.spottedVisibleOnly && !isVisible) continue;
        }

        // ── Vorhersage: Position + Velocity * Snapshot-Alter ────────────────
        // Kompensiert Reader-Takt + Engine-Interp (Standard gegen ESP-Lag).
        DWORD ageMs = GetTickCount() - cache.stampMs;
        Vector3 pred = PredictPos(p.worldOrigin, p.velocity, ageMs);
        Vector2 feet = p.feet, head = p.head;
        {
            Vector3 headW{ pred.x, pred.y, pred.z + (p.crouching ? 54.f : 72.f) };
            if (!WorldToScreen(pred, feet, cache.vm, scrW, scrH)) continue;
            if (!WorldToScreen(headW, head, cache.vm, scrW, scrH)) continue;
        }

        float bh = fabsf(feet.y - head.y);
        if (bh < 8.f) continue; // cull tiny
        float bw = bh * 0.45f;
        float bL = head.x - bw*0.5f;
        float bT = head.y;
        float bR = head.x + bw*0.5f;
        float bB = feet.y;

        // AABB cull: completely off-screen?
        if (g_cfg.espOptimize) {
            if (bR < -50.f || bL > scrWf+50.f || bB < -50.f || bT > scrHf+50.f) continue;
        }

        float hudScale = bh / 200.f;
        if (hudScale < 0.3f) hudScale = 0.3f;
        else if (hudScale > 3.f) hudScale = 3.f;

        ImU32 colU32;
        ImU32 radarU32 = IM_COL32(255,220,90,255);
        if (g_cfg.rainbow) colU32 = rainbowU32;
        else if (g_cfg.spottedEnable && g_cfg.spottedColorMode && enemy) {
            if (g_cfg.spottedMode==0 || g_cfg.spottedMode==2) {
                if (isVisible) colU32 = spottedU32;
                else if (p.radarSpotted) colU32 = radarU32;
                else colU32 = hiddenU32;
            } else colU32 = enemy ? enemyU32 : teamU32;
        } else {
            colU32 = enemy ? enemyU32 : teamU32;
        }

        // ── Box: IMMER dunkle Outline + Stil-Füllung ───────────────────────
        // Einheitlich über Corner/Full/Filled/Chams (Lesbarkeit überall gleich).
        if (g_cfg.box) {
            // Chams-Stil (UC-Look für External): flache Füllung wie Chams —
            // sichtbar = Spotted-Farbe, verdeckt = Hidden-Farbe. Keine Glow-
            // Layer (Performance), 2 Draw-Calls pro Spieler. Rein lesend
            // (keine Glow-Writes ins Spiel -> chams-kompatibel, kein extra
            // Detection-Vektor wenn später interne Chams dazu kommen).
            if (g_cfg.chamsStyle) {
                float a = g_cfg.chamsAlpha;
                if (a < 0.05f) a = 0.05f; if (a > 0.8f) a = 0.8f;
                draw->AddRectFilled(ImVec2(bL,bT), ImVec2(bR,bB),
                                    WithAlpha(colU32, (int)(a * 255.f)),
                                    g_cfg.espBoxRounding);
                draw->AddRect(ImVec2(bL-1,bT-1), ImVec2(bR+1,bB+1),
                              IM_COL32(0,0,0,140), g_cfg.espBoxRounding, 0, 2.5f);
                draw->AddRect(ImVec2(bL,bT), ImVec2(bR,bB), colU32,
                              g_cfg.espBoxRounding, 0, 1.5f);
            } else
            {
            float th = g_cfg.thickness;
            float rounding = g_cfg.espBoxRounding;
            // Glow: optimiert - bei espOptimize max 2 Layer statt 4
            if (g_cfg.glow) {
                int layers = (int)(g_cfg.glowSpread * 0.5f);
                if (layers < 1) layers = 1;
                int maxLayers = g_cfg.espOptimize ? 2 : 4;
                if (layers > maxLayers) layers = maxLayers;
                int gr, gg, gb;
                SplitCol(colU32, gr, gg, gb);
                for (int gi = layers; gi >= 1; gi--) {
                    float sp = gi * 2.f;
                    draw->AddRect(ImVec2(bL-sp,bT-sp), ImVec2(bR+sp,bB+sp),
                                  IM_COL32(gr, gg, gb, (int)(g_cfg.glowAlpha*255 / layers)),
                                  rounding, 0, th+1.f);
                }
            }
            // Box Style (alle mit dunkler Outline wie Full)
            if (g_cfg.espBoxStyle == 0) { // Corner
                float cL = bh * 0.18f;
                if (cL > bw*0.45f) cL = bw*0.45f;
                auto corner = [&](float x, float y, float sx, float sy) {
                    draw->AddLine(ImVec2(x,y), ImVec2(x+sx*cL,y), IM_COL32(0,0,0,140), th+2.f);
                    draw->AddLine(ImVec2(x,y), ImVec2(x,y+sy*cL), IM_COL32(0,0,0,140), th+2.f);
                    draw->AddLine(ImVec2(x,y), ImVec2(x+sx*cL,y), colU32, th);
                    draw->AddLine(ImVec2(x,y), ImVec2(x,y+sy*cL), colU32, th);
                };
                corner(bL,bT,+1,+1); corner(bR,bT,-1,+1);
                corner(bL,bB,+1,-1); corner(bR,bB,-1,-1);
            } else if (g_cfg.espBoxStyle == 1) { // Full
                draw->AddRect(ImVec2(bL-1,bT-1), ImVec2(bR+1,bB+1), IM_COL32(0,0,0,120), rounding, 0, th+1.f);
                draw->AddRect(ImVec2(bL,bT), ImVec2(bR,bB), colU32, rounding, 0, th);
            } else { // Filled
                draw->AddRectFilled(ImVec2(bL,bT), ImVec2(bR,bB),
                                    WithAlpha(colU32, (int)(g_cfg.espFilledAlpha*255)), rounding);
                draw->AddRect(ImVec2(bL-1,bT-1), ImVec2(bR+1,bB+1), IM_COL32(0,0,0,120), rounding, 0, th+1.f);
                draw->AddRect(ImVec2(bL,bT), ImVec2(bR,bB), colU32, rounding, 0, th);
            }
            } // !chamsStyle
        }

        // ── Spotted-Tag über der Box (MASK=SPOTTED, Radar=Gelb, sonst HIDDEN)
        if (g_cfg.spottedEnable && g_cfg.spottedText && enemy && (g_cfg.spottedMode == 1 || g_cfg.spottedMode == 2)) {
            const char* txt = "HIDDEN";
            ImU32 tcol = IM_COL32(245, 75, 75, 230);
            if (isVisible) { txt = "SPOTTED"; tcol = IM_COL32(90, 245, 110, 230); }
            else if (p.radarSpotted) { txt = "RADAR"; tcol = IM_COL32(245, 190, 65, 230); }

            float tagW = EspTextW(txt, 10.f);
            float pillW = tagW + 10.f;
            float pillH = 15.f;
            float tx = bL + (bw - pillW) * 0.5f;
            float ty = bT - pillH - 3.f;
            if (ty < 2.f) ty = bB + 3.f;

            // Status-Pill: dunkler Hintergrund + Kontur
            draw->AddRectFilled(ImVec2(tx, ty), ImVec2(tx + pillW, ty + pillH), IM_COL32(14, 16, 22, 220), 3.f);
            draw->AddRect(ImVec2(tx - 1.f, ty - 1.f), ImVec2(tx + pillW + 1.f, ty + pillH + 1.f), IM_COL32(0, 0, 0, 150), 3.f, 0, 1.2f);
            draw->AddRect(ImVec2(tx, ty), ImVec2(tx + pillW, ty + pillH), WithAlpha(tcol, 160), 3.f, 0, 1.0f);

            if (g_cfg.espShadow)
                draw->AddText(nullptr, 10.f, ImVec2(tx + 5.f + 1.f, ty + 1.f + 1.f), kEspShadow, txt);
            draw->AddText(nullptr, 10.f, ImVec2(tx + 5.f, ty + 1.f), tcol, txt);
        }

        // ── Skeleton (Spieler-Farbe bei sichtbar, sonst konfiguriert+faded) ─
        if (g_cfg.skeleton && p.hasBones) {
            ImU32 skCol = IM_COL32((int)(g_cfg.skeletonColor[0]*255),(int)(g_cfg.skeletonColor[1]*255),(int)(g_cfg.skeletonColor[2]*255),(int)(g_cfg.skeletonColor[3]*255));
            if (g_cfg.spottedEnable && g_cfg.spottedColorMode && enemy && !isVisible && !p.radarSpotted)
                skCol = WithAlpha(skCol, 0x6A); // hidden = leicht transparent
            DrawSkeleton(p, draw, skCol, g_cfg.skeletonThickness, cache.vm, scrW, scrH, ageMs);
        }

        // ── Head Dot (Farbe wie Box, immer mit Ring) ───────────────────────
        if (g_cfg.espHeadDot) {
            float hr = bh * 0.045f;
            if (hr < 2.f) hr = 2.f; if (hr > 6.f) hr = 6.f;
            draw->AddCircleFilled(ImVec2(head.x, head.y + hr), hr*0.6f, colU32);
            draw->AddCircle(ImVec2(head.x, head.y + hr), hr*0.6f, IM_COL32(0,0,0,180), 0, 1.f);
        }

        // ── Snapline in Box-Farbe (einheitlich, halbtransparent) ───────────
        if (g_cfg.snapLines) {
            float midX = (bL+bR)*0.5f;
            draw->AddLine(ImVec2(midX,bB), ImVec2(scrWf*0.5f, scrHf), WithAlpha(colU32, 150), 1.f);
        }

        // ── Health-Bar (links, Zahl in Label-Größe) ─────────────────────────
        if (g_cfg.health) {
            float barW = 4.f;
            float barX = bL - 7.f + g_cfg.healthBarOffsetX * hudScale;
            float barY = bT + g_cfg.healthBarOffsetY * hudScale;
            float barB2 = bB + g_cfg.healthBarOffsetY * hudScale;
            float hp = (float)p.health * 0.01f;
            if (hp > 1.f) hp = 1.f; if (hp < 0.f) hp = 0.f;
            float barH = barB2 - barY;
            float healthH = barH * hp;

            // Track Hintergrund + dunkle Outline
            draw->AddRectFilled(ImVec2(barX, barY), ImVec2(barX + barW, barB2), IM_COL32(14, 16, 22, 220), 2.f);
            draw->AddRect(ImVec2(barX - 1.f, barY - 1.f), ImVec2(barX + barW + 1.f, barB2 + 1.f), IM_COL32(0, 0, 0, 160), 2.f, 0, 1.2f);

            ImU32 hcol = IM_COL32((int)((1.f - hp) * 255.f), (int)(hp * 220.f), 40, 255);
            if (healthH > 1.f)
                draw->AddRectFilled(ImVec2(barX, barB2 - healthH), ImVec2(barX + barW, barB2), hcol, 2.f);

            // Health Number: nur anzeigen wenn genügend Platz oder Box hoch genug
            if (bh > 35.f && p.health < 100) {
                snprintf(hpBuf, sizeof(hpBuf), "%d", p.health);
                float numW = EspTextW(hpBuf, kEspLabel);
                float numY = barB2 - healthH - 6.f;
                if (numY < barY - 8.f) numY = barY - 8.f;
                EspText(draw, barX - numW - 3.f, numY, kEspLabel, IM_COL32(230, 235, 245, 230), hpBuf);
            }
        }

        // ── Name (Header-Größe, zentriert, geclampt) ────────────────────────
        if (g_cfg.name && p.name[0]) {
            float nameW = EspTextW(p.name, kEspHeader);
            float nameX = bL + (bw - nameW)*0.5f + g_cfg.nameOffsetX * hudScale;
            float nameY = bT + g_cfg.nameOffsetY * hudScale;
            if (nameX < 2.f) nameX = 2.f; if (nameX+nameW > scrWf-2.f) nameX = scrWf - nameW -2.f;
            EspText(draw, nameX, nameY, kEspHeader, IM_COL32(230,230,240,235), p.name);
        }

        // ── Waffen + Distanz teilen sich die Zeile UNTER der Box (Cursor) ────
        // Reihenfolge: Icon-Zeile in Pillen mit Hintergrund, darunter Distanz per Cursor
        float belowY = bB + 3.f + g_cfg.weaponIconOffsetY * hudScale;
        bool drewWeapons = false;
        if (g_cfg.showWeapon && p.weaponCount > 0) {
            float iconH = 18.f;
            float pillPadX = 5.f;
            float pillPadY = 2.f;
            float itemWs[4] = {};
            float totalW = 0.f;
            const TexInfo* iconTi[4] = {};
            const char* itemNames[4] = {};

            for (int wi = 0; wi < p.weaponCount; wi++) {
                const char* sf = WeaponIdToSvgFile(p.weaponIds[wi]);
#ifdef _WIN32
                iconTi[wi] = sf ? GetOrLoadTexture(sf, g_d3dDev) : nullptr;
#else
                iconTi[wi] = sf ? GetOrLoadTexture(sf) : nullptr;
#endif
                if (iconTi[wi] && iconTi[wi]->srv && iconTi[wi]->h > 0) {
                    itemWs[wi] = iconH * ((float)iconTi[wi]->w / (float)iconTi[wi]->h);
                } else {
                    itemNames[wi] = weapons::GetWeaponName(p.weaponIds[wi]);
                    if (!itemNames[wi]) itemNames[wi] = "";
                    itemWs[wi] = EspTextW(itemNames[wi], kEspLabel);
                }
                totalW += (itemWs[wi] + pillPadX * 2.f) + 4.f;
            }
            totalW -= 4.f;

            float sx = bL + (bw - totalW) * 0.5f + g_cfg.weaponIconOffsetX * hudScale;
            float sy = belowY;
            float pillH = iconH + pillPadY * 2.f;

            for (int wi = 0; wi < p.weaponCount; wi++) {
                float pw = itemWs[wi] + pillPadX * 2.f;
                // Pille: Hintergrund + dunkle Outline
                draw->AddRectFilled(ImVec2(sx, sy), ImVec2(sx + pw, sy + pillH), IM_COL32(14, 16, 22, 220), 3.f);
                draw->AddRect(ImVec2(sx - 1.f, sy - 1.f), ImVec2(sx + pw + 1.f, sy + pillH + 1.f), IM_COL32(0, 0, 0, 150), 3.f, 0, 1.2f);
                draw->AddRect(ImVec2(sx, sy), ImVec2(sx + pw, sy + pillH), IM_COL32(65, 75, 95, 170), 3.f, 0, 1.0f);

                if (iconTi[wi] && iconTi[wi]->srv) {
                    draw->AddImage(ImTextureRef((ImTextureID)(uintptr_t)iconTi[wi]->srv),
                                   ImVec2(sx + pillPadX, sy + pillPadY),
                                   ImVec2(sx + pillPadX + itemWs[wi], sy + pillPadY + iconH));
                } else if (itemNames[wi] && itemNames[wi][0]) {
                    EspText(draw, sx + pillPadX, sy + pillPadY + 1.f, kEspLabel, IM_COL32(215, 220, 230, 230), itemNames[wi]);
                }
                sx += pw + 4.f;
            }
            drewWeapons = true;
            belowY += pillH + 3.f;
        }

        if (g_cfg.espDistance && cache.valid) {
            float dx = pred.x - cache.localOrigin.x;
            float dy = pred.y - cache.localOrigin.y;
            // CS: 1m ~ 40 units
            float dist = sqrtf(dx*dx + dy*dy) / 40.f;
            char dbuf[16]; snprintf(dbuf, sizeof(dbuf), "%.0fm", dist);
            float distY = drewWeapons ? belowY : (bB + 3.f + g_cfg.distanceOffsetY * hudScale);
            EspTextC(draw, bL + bw*0.5f + g_cfg.distanceOffsetX * hudScale,
                     distY, kEspLabel, IM_COL32(200, 210, 225, 230), dbuf);
        }
    }
}

// ── Bombtimer — mit Defuse-Kit-Erkennung ─────────────────────────────────────
void DrawBombTimer(const BombData& bomb, int scrW, int scrH) {
    if (!bomb.active) return;
    float remaining;
    if (bomb.curTime > 1.f) remaining = bomb.blowTime - bomb.curTime;
    else if (bomb.wallStartMs) remaining = bomb.wallTimer - (float)(GetTickCount() - bomb.wallStartMs)/1000.f;
    else remaining = bomb.wallTimer > 0.f ? bomb.wallTimer : 40.f;
    if (remaining < 0.f) remaining = 0.f;

    // Hoehe dynamisch: extra Zeile wenn defused
    const float panelW = 270.f, pad = 18.f;
    float panelH = bomb.beingDefused ? 180.f : 152.f;
    float x = (g_cfg.bombTimerHAlign == 0) ? pad : (g_cfg.bombTimerHAlign == 2) ? (float)scrW - panelW - pad : (scrW - panelW) * 0.5f;
    float y = (g_cfg.bombTimerVAlign == 0) ? scrH * 0.15f : (g_cfg.bombTimerVAlign == 2) ? (float)scrH - panelH - pad : (scrH - panelH) * 0.5f;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    // Einheitlicher Panel-Hintergrund + dunkle Outline
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + panelW, y + panelH), IM_COL32(14, 16, 22, 230), 8.f);
    dl->AddRect(ImVec2(x - 1.f, y - 1.f), ImVec2(x + panelW + 1.f, y + panelH + 1.f), IM_COL32(0, 0, 0, 160), 8.f, 0, 1.5f);

    ImU32 borderCol = IM_COL32(65, 75, 95, 180);
    if (remaining < 5.f) borderCol = IM_COL32(240, 69, 79, 230);
    else if (remaining < 10.f) borderCol = IM_COL32(240, 181, 61, 210);
    dl->AddRect(ImVec2(x, y), ImVec2(x + panelW, y + panelH), borderCol, 8.f, 0, 1.0f);

    bool siteA = bomb.bombSite == 0;
    ImU32 siteAccent = siteA ? IM_COL32(110, 220, 120, 240) : IM_COL32(245, 180, 60, 240);
    dl->AddRectFilled(ImVec2(x + 16.f, y), ImVec2(x + panelW - 16.f, y + 2.5f), siteAccent, 1.f);

    const char* siteStr = siteA ? "BOMB  A" : "BOMB  B";
    float siteW = ImGui::GetFont()->CalcTextSizeA(15.f, FLT_MAX, 0.f, siteStr).x;
    float siteX = x + (panelW - siteW) * 0.5f;
    if (g_cfg.espShadow)
        dl->AddText(nullptr, 15.f, ImVec2(siteX + 1.f, y + 8.f + 1.f), kEspShadow, siteStr);
    dl->AddText(nullptr, 15.f, ImVec2(siteX, y + 8.f), siteAccent, siteStr);

    char timerStr[16];
    int mins = (int)(remaining / 60.f);
    int secs = (int)remaining % 60;
    snprintf(timerStr, sizeof(timerStr), "%02d:%02d", mins, secs);

    ImU32 timerCol = IM_COL32(235, 240, 250, 255);
    if (remaining < 5.f) timerCol = IM_COL32(245, 70, 80, 255);
    else if (remaining < 10.f) timerCol = IM_COL32(245, 185, 65, 255);

    float timerW = ImGui::GetFont()->CalcTextSizeA(36.f, FLT_MAX, 0.f, timerStr).x;
    float timerX = x + (panelW - timerW) * 0.5f;
    if (g_cfg.espShadow)
        dl->AddText(nullptr, 36.f, ImVec2(timerX + 1.5f, y + 28.f + 1.5f), kEspShadow, timerStr);
    dl->AddText(nullptr, 36.f, ImVec2(timerX, y + 28.f), timerCol, timerStr);

    float bX = x + 18.f, bY = y + 78.f, bW = panelW - 36.f, bH = 6.f;

    // ── Defuse: Haupt-Balken wird zum Defuse-Fortschritt (läuft runter) ─────
    // Restzeit bevorzugt aus Spiel-Daten (Countdown−curtime); Fallback per
    // Wanduhr seit Defuse-Beginn, damit der Balken auch bei kurzen
    // Lese-Aussetzern IMMER sichtbar runterläuft.
    static bool  s_defWasActive = false;
    static DWORD s_defStartMs   = 0;
    const DWORD  nowMs    = GetTickCount();
    const bool   defusing = bomb.beingDefused;
    float defRem = 0.f, defLen = 5.f;
    if (defusing) {
        if (!s_defWasActive) { s_defWasActive = true; s_defStartMs = nowMs; }
        defLen = bomb.defuseLength > 0.5f ? bomb.defuseLength : (bomb.defuserHasKit ? 5.f : 10.f);
        if (defLen < 1.f) defLen = 5.f;
        if (bomb.curTime > 1.f && bomb.defuseCountDown > 1.f) {
            defRem = ImMax(0.f, bomb.defuseCountDown - bomb.curTime);
        } else {
            defRem = ImMax(0.f, defLen - (float)(nowMs - s_defStartMs) / 1000.f);
        }
    } else {
        s_defWasActive = false;
    }

    // Defuse aktiv → Balken zeigt Defuse-Fortschritt (voll → komplett leer,
    // leer genau beim fertigen Defuse). Sonst ganz normal die Bombenzeit.
    float frac = defusing ? ImClamp(defRem / defLen, 0.f, 1.f)
                          : ImClamp(remaining / bomb.timerLength, 0.f, 1.f);

    // Progress Bar Track mit Outline
    dl->AddRectFilled(ImVec2(bX, bY), ImVec2(bX + bW, bY + bH), IM_COL32(10, 11, 15, 230), 3.f);
    dl->AddRect(ImVec2(bX - 1.f, bY - 1.f), ImVec2(bX + bW + 1.f, bY + bH + 1.f), IM_COL32(0, 0, 0, 140), 3.f, 0, 1.0f);

    ImU32 barFill;
    if (defusing) {
        // Kit-Blau / No-Kit-Orange wie bisher am Defuse-Balken
        barFill = bomb.defuserHasKit ? IM_COL32(80, 190, 255, 235) : IM_COL32(255, 175, 45, 235);
    } else {
        barFill = IM_COL32(110, 220, 120, 235);
        if (remaining < 5.f) barFill = IM_COL32(245, 70, 80, 235);
        else if (remaining < 10.f) barFill = IM_COL32(245, 185, 65, 235);
    }
    if (frac > 0.005f)
        dl->AddRectFilled(ImVec2(bX, bY), ImVec2(bX + bW * frac, bY + bH), barFill, 3.f);

    bool canKit = remaining > 5.f, canNoKit = remaining > 10.f;
    float infoY = bY + 14.f;
    char kitStr[24], nokitStr[24];
    snprintf(kitStr, sizeof(kitStr), "Kit: %s", canKit ? "YES" : "NO");
    snprintf(nokitStr, sizeof(nokitStr), "NoKit: %s", canNoKit ? "YES" : "NO");

    ImU32 kitCol = canKit ? IM_COL32(110, 220, 120, 255) : IM_COL32(245, 70, 80, 255);
    ImU32 nokitCol = canNoKit ? IM_COL32(110, 220, 120, 255) : IM_COL32(245, 70, 80, 255);
    float kW = ImGui::GetFont()->CalcTextSizeA(kEspLabel, FLT_MAX, 0.f, kitStr).x;
    float nW = ImGui::GetFont()->CalcTextSizeA(kEspLabel, FLT_MAX, 0.f, nokitStr).x;
    float gap = 20.f, rowW = kW + gap + nW, rX = x + (panelW - rowW) * 0.5f;

    if (g_cfg.espShadow) {
        dl->AddText(nullptr, kEspLabel, ImVec2(rX + 1.f, infoY + 1.f), kEspShadow, kitStr);
        dl->AddText(nullptr, kEspLabel, ImVec2(rX + kW + gap + 1.f, infoY + 1.f), kEspShadow, nokitStr);
    }
    dl->AddText(nullptr, kEspLabel, ImVec2(rX, infoY), kitCol, kitStr);
    dl->AddText(nullptr, kEspLabel, ImVec2(rX + kW + gap, infoY), nokitCol, nokitStr);

    if (defusing) {
        const char* kitTxt = "?";
        if (bomb.defuserKnown) kitTxt = bomb.defuserHasKit ? "KIT" : "NO KIT";
        else if (bomb.defuseLength > 0.5f) kitTxt = bomb.defuseLength < 7.5f ? "KIT" : "NO KIT";

        char defStr[64];
        snprintf(defStr, sizeof(defStr), "DEFUSING %.1fs [%s]", defRem, kitTxt);
        float dW = ImGui::GetFont()->CalcTextSizeA(kEspLabel, FLT_MAX, 0.f, defStr).x;
        float defX = x + (panelW - dW) * 0.5f;
        if (g_cfg.espShadow)
            dl->AddText(nullptr, kEspLabel, ImVec2(defX + 1.f, infoY + 18.f + 1.f), kEspShadow, defStr);
        dl->AddText(nullptr, kEspLabel, ImVec2(defX, infoY + 18.f), IM_COL32(255, 215, 60, 255), defStr);

        // Kann er vor der Explosion fertig werden? (Der Defuse-Balken ist der
        // obere Haupt-Balken — läuft bis komplett leer, leer = defused.)
        bool willSucceed = defRem < remaining;
        const char* resTxt = willSucceed ? "WILL DEFUSE" : "WILL FAIL";
        ImU32 resCol = willSucceed ? IM_COL32(110, 220, 120, 255) : IM_COL32(245, 70, 80, 255);
        float rw = ImGui::GetFont()->CalcTextSizeA(11.f, FLT_MAX, 0.f, resTxt).x;
        float resX = x + (panelW - rw) * 0.5f;
        float resY = infoY + 38.f;
        if (g_cfg.espShadow)
            dl->AddText(nullptr, 11.f, ImVec2(resX + 1.f, resY + 1.f), kEspShadow, resTxt);
        dl->AddText(nullptr, 11.f, ImVec2(resX, resY), resCol, resTxt);
    }
}

// ── Item-ESP (Pillen: Icon + Label, dunkler Hintergrund, klare Kontur) ─────
void DrawItemESP(const GroundCache& ground, const GameCache* cache, int scrW, int scrH) {
    (void)scrW; (void)scrH;
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    char svgBuf[48];
    const DWORD now = GetTickCount();

    // Fade-in-Cache: neue Items schweben sanft ein (Key = entAddr, sonst Slot).
    struct ItemFade { uintptr_t key = 0; DWORD firstSeen = 0; DWORD lastTouch = 0; };
    static ItemFade s_fade[96];

    for (int i = 0; i < ground.count; i++) {
        const auto& it = ground.items[i];
        if (!it.onScreen) continue;
        if (it.type == 0 && !g_cfg.itemEspWeapons) continue;
        if (it.type == 1 && !g_cfg.itemEspBomb) continue;
        if (it.type == 2 && !g_cfg.itemEspUtil) continue;

        // Fade-Eintrag holen/erzeugen (Eviction: ältester lastTouch)
        const uintptr_t key = it.entAddr ? it.entAddr : ((uintptr_t)0x1000000000ull + (uintptr_t)it.slot);
        ItemFade* fe = nullptr;
        ItemFade* freeSlot = nullptr;
        ItemFade* oldest = &s_fade[0];
        for (int f = 0; f < 96; f++) {
            if (s_fade[f].key == key) { fe = &s_fade[f]; break; }
            if (!s_fade[f].key) { if (!freeSlot) freeSlot = &s_fade[f]; }
            else if (s_fade[f].lastTouch < oldest->lastTouch) oldest = &s_fade[f];
        }
        if (!fe) {
            fe = freeSlot ? freeSlot : oldest;
            fe->key = key;
            fe->firstSeen = now;
        }
        fe->lastTouch = now;

        // Smoothstep über 240 ms + sanftes Einschweben von oben
        float a = (float)(now - fe->firstSeen) / 240.f;
        if (a > 1.f) a = 1.f;
        a = a * a * (3.f - 2.f * a);

        ImU32 accentCol;
        switch (it.type) {
            case 0:  accentCol = IM_COL32(90, 140, 255, 235); break;  // Waffen
            case 1:  accentCol = IM_COL32(255, 75, 75, 245); break;   // Bombe
            default: accentCol = IM_COL32(255, 205, 70, 235); break;  // Utility/Granaten
        }
        // Dezentes Pulsieren der Akzentkante (Bombe stärker = dringlicher)
        const float pulse = 0.5f + 0.5f * sinf((float)ImGui::GetTime() * (it.type == 1 ? 4.2f : 2.2f));
        accentCol = WithAlpha(accentCol,
            (int)((it.type == 1 ? 175.f + 65.f * pulse : 150.f + 45.f * pulse) * a));

        const char* svgFile = ItemSvgName(it, svgBuf, sizeof(svgBuf));
#ifdef _WIN32
        const TexInfo* texInfo = GetOrLoadTexture(svgFile, g_d3dDev);
#else
        const TexInfo* texInfo = GetOrLoadTexture(svgFile);
#endif

        // Distanz zum lokalen Spieler (in Metern, ~40u = 1m)
        char distTxt[16] = "";
        if (cache && cache->valid) {
            Vector3 d = VecSub(it.origin, cache->localOrigin);
            float m = sqrtf(d.x*d.x + d.y*d.y + d.z*d.z) / 40.f;
            if (m > 0.4f && m < 400.f) snprintf(distTxt, sizeof(distTxt), "%.0fm", m);
        }

        float iconH = 20.f;
        float iconW = (texInfo && texInfo->srv && texInfo->h > 0)
            ? (iconH * ((float)texInfo->w / (float)texInfo->h)) : 20.f;

        float nameW = EspTextW(it.name, kEspLabel);
        float distW = distTxt[0] ? EspTextW(distTxt, kEspLabel) : 0.f;
        float pillW = iconW + nameW + 20.f + (distW > 0.f ? distW + 6.f : 0.f);
        float pillH = 24.f;
        float liftY = (1.f - a) * -6.f;   // Einschweb-Offset

        float px = it.screen.x - pillW * 0.5f;
        float py = it.screen.y - pillH * 0.5f + liftY;

        // Pill: dunkler Body + schwarze Outline + pulsierende Akzentkante
        draw->AddRectFilled(ImVec2(px, py), ImVec2(px + pillW, py + pillH),
                            IM_COL32(14, 16, 22, (int)(230 * a)), 6.f);
        draw->AddRect(ImVec2(px - 1.f, py - 1.f), ImVec2(px + pillW + 1.f, py + pillH + 1.f),
                      IM_COL32(0, 0, 0, (int)(170 * a)), 6.f, 0, 1.5f);
        draw->AddRect(ImVec2(px, py), ImVec2(px + pillW, py + pillH),
                      accentCol, 6.f, 0, 1.2f);

        // Icon
        float curX = px + 7.f;
        float iconY = py + (pillH - iconH) * 0.5f;
        if (texInfo && texInfo->srv) {
            draw->AddImage(ImTextureRef((ImTextureID)(uintptr_t)texInfo->srv),
                           ImVec2(curX, iconY), ImVec2(curX + iconW, iconY + iconH),
                           ImVec2(0.f, 0.f), ImVec2(1.f, 1.f),
                           IM_COL32(255, 255, 255, (int)(255 * a)));
        }
        curX += iconW + 6.f;

        // Name (hell) + Distanz (Akzentfarbe, rechtsbündig)
        float textY = py + (pillH - kEspLabel) * 0.5f - 1.f;
        if (g_cfg.espShadow)
            draw->AddText(nullptr, kEspLabel, ImVec2(curX + 1.f, textY + 1.f),
                          WithAlpha(kEspShadow, (int)(180 * a)), it.name);
        draw->AddText(nullptr, kEspLabel, ImVec2(curX, textY),
                      WithAlpha(IM_COL32(232, 236, 246, 255), (int)(245 * a)), it.name);
        if (distW > 0.f) {
            float distX = px + pillW - 7.f - distW;
            if (g_cfg.espShadow)
                draw->AddText(nullptr, kEspLabel, ImVec2(distX + 1.f, textY + 1.f),
                              WithAlpha(kEspShadow, (int)(160 * a)), distTxt);
            draw->AddText(nullptr, kEspLabel, ImVec2(distX, textY), accentCol, distTxt);
        }
    }
}

// ── Spectator-Liste: NUR Zuschauer, die MICH beobachten ─────────────────────
// UC-Standard: m_hObserverPawn-Kette (Reader), Modi nach CS2-Enum
// (NONE 0, FIXED 1, IN_EYE 2, CHASE 3, ROAMING 4, DIRECTED 5).
// Filter: Observer-Ziel == lokaler Pawn. Dadurch verschwinden die
// unaufloesbaren "?"-Zeilen (vorher wurden ALLE toten Spieler gelistet).
void DrawSpecList(const GameCache& cache, int scrW) {
    if (!g_cfg.specListEnable || !cache.valid || !cache.localPawnAddr) return;

    struct Row { const char* watcher; int wteam; int mode; };
    Row rows[16];
    int n = 0;
    for (int i = 0; i < cache.playerCount; i++) {
        const auto& p = cache.players[i];
        if (p.alive) continue;
        if (!p.name[0] || n >= 16) continue;
        if (p.specPawn != cache.localPawnAddr) continue; // nur MIR-Zuschauer
        rows[n].watcher = p.name;
        rows[n].wteam   = p.team;
        rows[n].mode    = p.specMode;
        n++;
    }
    if (n == 0) return;

    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    float w = 250.f, h = 30.f + (float)n * 17.f;
    float x = (float)scrW - w - 12.f;
    float y = g_cfg.showProfiler ? 228.f : 12.f;
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), IM_COL32(10, 12, 16, 210), 6.f);
    dl->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), IM_COL32(90, 100, 130, 160), 6.f, 0, 1.f);
    char title[32];
    snprintf(title, sizeof(title), "ZUSCHAUER (%d)", n);
    dl->AddText(nullptr, 13.f, ImVec2(x + 8, y + 7),
                IM_COL32(140, 200, 255, 255), title);
    for (int i = 0; i < n; i++) {
        // CS2 ObserverMode_t (a2x-Dump): 0 NONE, 1 FIXED, 2 IN_EYE, 3 CHASE,
        // 4 ROAMING, 5 DIRECTED. Legacy-Fallback 4/5 bleibt lesbar.
        const char* mtxt = "SPEC";
        switch (rows[i].mode) {
            case 2:  mtxt = "EYE";   break;
            case 3:  mtxt = "CHASE"; break;
            case 1:  mtxt = "FIXED"; break;
            case 4:  mtxt = "ROAM";  break;
            case 5:  mtxt = "DIR";   break;
            default: mtxt = "SPEC";  break;
        }
        ImU32 wcol = rows[i].wteam == cache.localTeam ? IM_COL32(90, 210, 120, 235)
                                                     : IM_COL32(240, 110, 110, 235);
        char buf[96];
        snprintf(buf, sizeof(buf), "%s [%s]", rows[i].watcher, mtxt);
        dl->AddText(nullptr, 12.f, ImVec2(x + 8, y + 26 + i * 17.f), wcol, buf);
    }
}

// ── Radar auf echtem CS2-Radar ──────────────────────────────────────────────
void DrawRadar(const GameCache& cache, int scrW, int scrH) {
    if (!g_cfg.radarEnable || !cache.valid) return;

    // ── Echtes CS2 HUD-Radar Alignment ─────────────────────────────────────
    // In CS2 skaliert das Radar mit der vertikalen Bildschirmhöhe (1080p Basis: ~190-210px).
    // radarUseRealPos dockt direkt an die Position des Ingame-Radars oben links an.
    float size = g_cfg.radarSize;
    float x = g_cfg.radarPosX;
    float y = g_cfg.radarPosY;

    if (g_cfg.radarUseRealPos) {
        float hudScale = (float)scrH / 1080.f;
        if (hudScale < 0.5f) hudScale = 0.5f;
        size = 208.f * hudScale;
        x = 20.f * hudScale;
        y = 18.f * hudScale;
    } else {
        if (size < 100.f) size = 100.f; if (size > 400.f) size = 400.f;
        if (x < 2.f) x = 2.f; if (y < 2.f) y = 2.f;
        if (x + size > (float)scrW - 2.f) x = (float)scrW - size - 2.f;
        if (y + size > (float)scrH - 2.f) y = (float)scrH - size - 2.f;
    }

    // ── Ganzkarten-Modus („Radar KOMPLETT") ────────────────────────────────
    // Echtes CS2-Map-Bild + reale Koordinaten (radar/<map>.png + maps.json,
    // boltobserv-Daten). Ohne Assets/Meta fällt es automatisch auf den
    // lokalen Relativ-Radar unten zurück — kein Feature-Verlust.
    if (g_cfg.radarFull && RadarMapDrawFull(cache, x, y, size)) return;

    float cx = x + size * 0.5f;
    float cy = y + size * 0.5f;
    float radius = size * 0.5f;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    // ── Radar-Hintergrund & Rahmen ─────────────────────────────────────────
    int bgAlpha = (int)(g_cfg.radarAlpha * 255.f);
    if (bgAlpha < 30) bgAlpha = 30; if (bgAlpha > 255) bgAlpha = 255;
    ImU32 bg = IM_COL32(14, 16, 22, bgAlpha);
    ImU32 borderCol = IM_COL32(65, 75, 95, 180);

    if (g_cfg.radarShape == 0) {
        dl->AddCircleFilled(ImVec2(cx, cy), radius, bg, 48);
        if (g_cfg.radarBorder) {
            dl->AddCircle(ImVec2(cx, cy), radius + 1.f, IM_COL32(0, 0, 0, 160), 48, 1.5f);
            dl->AddCircle(ImVec2(cx, cy), radius, borderCol, 48, 1.0f);
        }
    } else {
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + size, y + size), bg, 6.f);
        if (g_cfg.radarBorder) {
            dl->AddRect(ImVec2(x - 1.f, y - 1.f), ImVec2(x + size + 1.f, y + size + 1.f), IM_COL32(0, 0, 0, 160), 6.f, 0, 1.5f);
            dl->AddRect(ImVec2(x, y), ImVec2(x + size, y + size), borderCol, 6.f, 0, 1.0f);
        }
    }

    // Dezentrales Raster (Kreuz + Distanzringe)
    dl->AddLine(ImVec2(cx, y + 4.f), ImVec2(cx, y + size - 4.f), IM_COL32(255, 255, 255, 14), 1.f);
    dl->AddLine(ImVec2(x + 4.f, cy), ImVec2(x + size - 4.f, cy), IM_COL32(255, 255, 255, 14), 1.f);
    if (g_cfg.radarShape == 0) {
        dl->AddCircle(ImVec2(cx, cy), radius * 0.66f, IM_COL32(255, 255, 255, 10), 32, 1.f);
        dl->AddCircle(ImVec2(cx, cy), radius * 0.33f, IM_COL32(255, 255, 255, 7), 24, 1.f);
    } else {
        dl->AddRect(ImVec2(cx - radius * 0.5f, cy - radius * 0.5f), ImVec2(cx + radius * 0.5f, cy + radius * 0.5f), IM_COL32(255, 255, 255, 8), 0, 0, 1.f);
    }

    // ── Local Player: Blickkegel & Zentrierter Blip ────────────────────────
    // CS2-Standard: Eigenes Radar rotiert mit der Spieler-Blickrichtung,
    // sodass "Vorne" auf dem Radar immer nach OBEN zeigt.
    if (g_cfg.radarShowViewCone) {
        float coneRad = 35.f * kDeg2Rad;
        float coneLen = radius * 0.88f;
        ImVec2 p0(cx, cy);
        // Da die Welt relativ zu Yaw rotiert wird, ist die lokale Blickrichtung immer 0 rad (senkrecht nach oben)
        ImVec2 p1(cx - sinf(coneRad) * coneLen, cy - cosf(coneRad) * coneLen);
        ImVec2 p2(cx + sinf(coneRad) * coneLen, cy - cosf(coneRad) * coneLen);
        dl->AddTriangleFilled(p0, p1, p2, IM_COL32(90, 150, 255, 22));
        dl->AddLine(p0, p1, IM_COL32(110, 170, 255, 45), 1.2f);
        dl->AddLine(p0, p2, IM_COL32(110, 170, 255, 45), 1.2f);
    }

    // Lokaler Spielerpunkt in der Mitte
    dl->AddCircleFilled(ImVec2(cx, cy), 4.5f, IM_COL32(255, 255, 255, 255), 12);
    dl->AddCircle(ImVec2(cx, cy), 4.5f, IM_COL32(0, 0, 0, 180), 12, 1.2f);
    // Blickrichtungszeiger nach oben
    dl->AddLine(ImVec2(cx, cy), ImVec2(cx, cy - 11.f), IM_COL32(255, 255, 255, 255), 2.2f);

    // ── Spieler relativ zur Blickrichtung rotieren ─────────────────────────
    // CS2 Weltkoordinaten: +X = Vorne/Osten (Yaw 0), +Y = Links/Norden (Yaw 90).
    // Source-AngleVectors: right = (sin yaw, -cos yaw) → Projektion auf den
    // Blick-Rechts-Vektor:
    //   forward = dx * cos(yaw) + dy * sin(yaw)
    //   right   = dx * sin(yaw) - dy * cos(yaw)   (war gespiegelt — gefixt)
    // Radar-Projektion:
    //   radarX = cx + right * scale
    //   radarY = cy - forward * scale
    float zoom = g_cfg.radarZoom;
    if (zoom < 0.05f) zoom = 0.05f; if (zoom > 1.5f) zoom = 1.5f;

    float localYawRad = cache.localViewAngles.y * kDeg2Rad;
    float cosYaw = cosf(localYawRad);
    float sinYaw = sinf(localYawRad);

    for (int i = 0; i < cache.playerCount; i++) {
        const auto& p = cache.players[i];
        if (!p.alive) continue;
        bool enemy = p.team != cache.localTeam;
        if (enemy && !g_cfg.radarShowEnemies) continue;
        if (!enemy && !g_cfg.radarShowTeam) continue;

        bool vis = p.visible;
        Vector3 pp = PredictPos(p.worldOrigin, p.velocity, GetTickCount() - cache.stampMs);
        float dx = pp.x - cache.localOrigin.x;
        float dy = pp.y - cache.localOrigin.y;

        float fwdDist   = dx * cosYaw + dy * sinYaw;
        float rightDist = dx * sinYaw - dy * cosYaw;

        float radarX = cx + rightDist * zoom;
        float radarY = cy - fwdDist * zoom;

        float distFromCenter = sqrtf((radarX - cx) * (radarX - cx) + (radarY - cy) * (radarY - cy));
        bool out = false;

        if (g_cfg.radarShape == 0) {
            if (distFromCenter > radius - 7.f) {
                out = true;
                float ang = atan2f(radarY - cy, radarX - cx);
                radarX = cx + cosf(ang) * (radius - 7.f);
                radarY = cy + sinf(ang) * (radius - 7.f);
            }
        } else {
            if (radarX < x + 7.f) { radarX = x + 7.f; out = true; }
            if (radarX > x + size - 7.f) { radarX = x + size - 7.f; out = true; }
            if (radarY < y + 7.f) { radarY = y + 7.f; out = true; }
            if (radarY > y + size - 7.f) { radarY = y + size - 7.f; out = true; }
        }

        ImU32 col;
        if (enemy) col = vis ? IM_COL32(90, 245, 110, 230) : (p.radarSpotted ? IM_COL32(245, 190, 65, 230) : IM_COL32(245, 75, 75, 230));
        else col = IM_COL32(80, 160, 255, 230);
        if (out) col = WithAlpha(col, 130);
        else if (!vis && enemy && !p.radarSpotted) col = WithAlpha(col, 170);

        float r = enemy ? 5.f : 4.f;
        dl->AddCircleFilled(ImVec2(radarX, radarY), r, col, 12);
        dl->AddCircle(ImVec2(radarX, radarY), r, IM_COL32(0, 0, 0, 180), 12, 1.2f);

        // Bewegungs-/Geschwindigkeitsvektor relativ zur Rotation zeichnen
        float vlen = sqrtf(p.velocity.x * p.velocity.x + p.velocity.y * p.velocity.y);
        if (vlen > 30.f && !out) {
            float vFwd = p.velocity.x * cosYaw + p.velocity.y * sinYaw;
            float vRight = p.velocity.x * sinYaw - p.velocity.y * cosYaw;
            float lineScale = 8.f / vlen;
            if (lineScale > 0.05f) {
                float lx = radarX + vRight * lineScale * 8.f;
                float ly = radarY - vFwd * lineScale * 8.f;
                dl->AddLine(ImVec2(radarX, radarY), ImVec2(lx, ly), col, 1.5f);
            }
        }

        if (size > 160.f && !out) {
            char hbuf[8]; snprintf(hbuf, sizeof(hbuf), "%d", p.health);
            if (g_cfg.espShadow)
                dl->AddText(nullptr, 10.f, ImVec2(radarX + 7.f, radarY - 6.f + 1.f), IM_COL32(0, 0, 0, 180), hbuf);
            dl->AddText(nullptr, 10.f, ImVec2(radarX + 6.f, radarY - 6.f), IM_COL32(230, 235, 245, 210), hbuf);
        }

        if (out) {
            float outAng = atan2f(radarY - cy, radarX - cx);
            float a1 = outAng + 2.5f, a2 = outAng - 2.5f;
            ImVec2 triTip(radarX, radarY);
            ImVec2 triA(radarX - cosf(a1) * 7.f, radarY - sinf(a1) * 7.f);
            ImVec2 triB(radarX - cosf(a2) * 7.f, radarY - sinf(a2) * 7.f);
            dl->AddTriangleFilled(triTip, triA, triB, col);
        }
    }

    // Radar Header-Pille
    {
        float labW = ImGui::GetFont()->CalcTextSizeA(11.f, FLT_MAX, 0.f, "RADAR").x;
        dl->AddRectFilled(ImVec2(x + 5.f, y + 4.f), ImVec2(x + labW + 13.f, y + 18.f), IM_COL32(10, 12, 16, 200), 3.f);
        if (g_cfg.espShadow)
            dl->AddText(nullptr, 11.f, ImVec2(x + 9.f + 1.f, y + 4.f + 1.f), IM_COL32(0, 0, 0, 180), "RADAR");
        dl->AddText(nullptr, 11.f, ImVec2(x + 9.f, y + 4.f), IM_COL32(200, 210, 225, 210), "RADAR");
    }

    char zbuf[16]; snprintf(zbuf, sizeof(zbuf), "%.2fx", zoom);
    float zw = ImGui::GetFont()->CalcTextSizeA(10.f, FLT_MAX, 0.f, zbuf).x;
    if (g_cfg.espShadow)
        dl->AddText(nullptr, 10.f, ImVec2(x + size - zw - 6.f + 1.f, y + size - 14.f + 1.f), IM_COL32(0, 0, 0, 180), zbuf);
    dl->AddText(nullptr, 10.f, ImVec2(x + size - zw - 6.f, y + size - 14.f), IM_COL32(140, 150, 170, 180), zbuf);
}

// ─────────────────────────────────────────────────────────────────────────────
// Arrow ESP (UC-Standard „Offscreen-Indicator"): Pfeile auf einem Ring um das
// Fadenkreuz in Richtung der Gegner. Der Winkel kommt rein aus den
// Welt-Deltas + frischer Blickrichtung (der Renderer überschreibt
// cache.localViewAngles jeden Frame mit den aktuellen ViewAngles) → kein
// Nachhinken bei Kamera-Flicks. Farbe: Spotted/Hidden-Farben wie ESP.
// ─────────────────────────────────────────────────────────────────────────────
void DrawArrowESP(const GameCache& cache, int scrW, int scrH) {
    if (!g_cfg.arrowEsp || !cache.valid || cache.playerCount <= 0) return;

    const float cx = scrW * 0.5f;
    const float cy = scrH * 0.5f;

    float R = g_cfg.arrowEspRadius;
    if (R < 25.f) R = 25.f;
    if (R > 400.f) R = 400.f;
    float s = g_cfg.arrowEspSize;
    if (s < 3.f) s = 3.f;
    if (s > 40.f) s = 40.f;

    const float  yawRad = cache.localViewAngles.y * kDeg2Rad;
    const float  cosY   = cosf(yawRad), sinY = sinf(yawRad);
    const uint32_t ageMs = (uint32_t)(GetTickCount() - cache.stampMs);
    const bool teamsKnown = (cache.localTeam == 2 || cache.localTeam == 3);

    const ImU32 colVis    = IM_COL32((int)(g_cfg.spottedColor[0] * 255.f), (int)(g_cfg.spottedColor[1] * 255.f),
                                     (int)(g_cfg.spottedColor[2] * 255.f), 235);
    const ImU32 colHidden = IM_COL32((int)(g_cfg.hiddenColor[0] * 255.f), (int)(g_cfg.hiddenColor[1] * 255.f),
                                     (int)(g_cfg.hiddenColor[2] * 255.f), 235);
    const ImU32 colTeam   = IM_COL32((int)(g_cfg.teamColor[0] * 255.f), (int)(g_cfg.teamColor[1] * 255.f),
                                     (int)(g_cfg.teamColor[2] * 255.f), 235);

    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    for (int i = 0; i < cache.playerCount; ++i) {
        const PlayerData& p = cache.players[i];
        if (!p.alive) continue;
        const bool enemy = !teamsKnown || (p.team != cache.localTeam);
        if (!enemy && !g_cfg.arrowEspTeam) continue;

        const Vector3 pp = PredictPos(p.worldOrigin, p.velocity, ageMs);

        // „Nur außerhalb": Gegner sichtbar auf dem Bildschirm → kein Pfeil
        // (deren Position sieht man ohnehin direkt).
        if (g_cfg.arrowEspOffscreenOnly) {
            Vector2 scr;
            if (WorldToScreen(pp, scr, cache.vm, scrW, scrH) &&
                scr.x >= 0.f && scr.x <= (float)scrW &&
                scr.y >= 0.f && scr.y <= (float)scrH)
                continue;
        }

        const float dx = pp.x - cache.localOrigin.x;
        const float dy = pp.y - cache.localOrigin.y;
        if (dx == 0.f && dy == 0.f) continue;

        // Welt-Delta → Blickkoordinaten (wie Radar): forward/right, dann
        // Winkel relativ zum Fadenkreuz (+ = rechts). right = Projektion auf
        // den Source-Rechts-Vektor (sin yaw, −cos yaw) — das Vorzeichen war
        // vertauscht, dadurch waren die Pfeile spiegelverkehrt.
        const float fwd   =  dx * cosY + dy * sinY;
        const float right =  dx * sinY - dy * cosY;
        const float rel   = atan2f(right, fwd);

        // Ringposition: rel 0 = oben, + = rechts (Uhrzeigersinn auf dem Ring).
        const ImVec2 dir(sinf(rel), -cosf(rel));
        const ImVec2 pos(cx + dir.x * R, cy + dir.y * R);
        const ImVec2 perp(-dir.y, dir.x);
        const ImVec2 tip(pos.x + dir.x * s,           pos.y + dir.y * s);
        const ImVec2 b1 (pos.x + perp.x * s * 0.62f,  pos.y + perp.y * s * 0.62f);
        const ImVec2 b2 (pos.x - perp.x * s * 0.62f,  pos.y - perp.y * s * 0.62f);

        const ImU32 col = enemy ? (p.visible ? colVis : colHidden) : colTeam;

        // Schatten für Lesbarkeit auf hellem Untergrund.
        dl->AddTriangleFilled(ImVec2(tip.x + 1.5f, tip.y + 1.5f), ImVec2(b1.x + 1.5f, b1.y + 1.5f),
                              ImVec2(b2.x + 1.5f, b2.y + 1.5f), IM_COL32(0, 0, 0, 140));
        dl->AddTriangleFilled(tip, b1, b2, col);
        dl->AddTriangle(tip, b1, b2, IM_COL32(0, 0, 0, 190), 1.2f);
    }
}

