#include "pch.h"
#include "Config.h"
#include "Globals.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <filesystem>
#ifdef _WIN32
#include <commdlg.h>
#include <shellapi.h>
#endif

// ─────────────────────────────────────────────────────────────────────────────
// core/Config.cpp — rohe Struct-Persistenz
// Neue Felder ans Struct-ENDE anhaengen → alte Configs bleiben kompatibel.
// Slots: configs/<name>.cfg neben der EXE (gleiches Format).
// ─────────────────────────────────────────────────────────────────────────────
namespace fs = std::filesystem;

static const char* CONFIG_PATH = "PixelMommySense.cfg";

static std::string ExeDirCfg() {
#ifdef _WIN32
    char buf[MAX_PATH]{};
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    char* slash = strrchr(buf, '\\');
    if (slash) slash[1] = '\0';
    return std::string(buf);
#else
    return OwO_ExeDir(); // mit trailing '/'
#endif
}

static std::string ConfigsDir() {
    return ExeDirCfg() + "configs";
}

// Dateiname säubern: nur alnum + _ - , max 32 Zeichen.
static std::string SanitizeName(const char* name) {
    std::string out;
    if (!name) return out;
    for (const char* p = name; *p && out.size() < 32; ++p) {
        char c = *p;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '-' || c == ' ')
            out += c;
    }
    // trim spaces
    while (!out.empty() && out.front() == ' ') out.erase(out.begin());
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

static void SanitizeConfig() {
    // Clamps fuer neue Felder (falls Config korrupt)
    if (g_cfg.radarSize < 80.f || g_cfg.radarSize > 400.f) g_cfg.radarSize = 185.f;
    if (g_cfg.radarZoom < 0.05f || g_cfg.radarZoom > 2.f) g_cfg.radarZoom = 0.30f;
    if (g_cfg.radarAlpha < 0.1f || g_cfg.radarAlpha > 1.f) g_cfg.radarAlpha = 0.88f;
    if (g_cfg.espBoxStyle < 0 || g_cfg.espBoxStyle > 2) g_cfg.espBoxStyle = 0;
    if (g_cfg.spottedMode < 0 || g_cfg.spottedMode > 2) g_cfg.spottedMode = 2;
    if (g_cfg.targetFps < 0) g_cfg.targetFps = 0;
    if (g_cfg.targetFps > 1000) g_cfg.targetFps = 1000;
        if (g_cfg.readerHz < 60) g_cfg.readerHz = 60;
        if (g_cfg.readerHz > 240) g_cfg.readerHz = 240; // mehr ist reine CPU-Last
    // Menue-Look (Glutenium-Style)
    if (g_cfg.menuTheme < 0 || g_cfg.menuTheme > 3) g_cfg.menuTheme = 0;
    for (int i = 0; i < 3; ++i) {
        if (g_cfg.menuAccent[i] < 0.f) g_cfg.menuAccent[i] = 0.f;
        if (g_cfg.menuAccent[i] > 1.f) g_cfg.menuAccent[i] = 1.f;
    }
    if (g_cfg.menuAccent[3] <= 0.f) g_cfg.menuAccent[3] = 1.f;
    if (g_cfg.menuScale < 0.5f || g_cfg.menuScale > 2.f) g_cfg.menuScale = 1.0f;
    // UC Seed-Triggerbot
    if (g_cfg.triggerbotMode < 0 || g_cfg.triggerbotMode > 2) g_cfg.triggerbotMode = 2;
    if (g_cfg.triggerbotMaxPenalty < 0.05f || g_cfg.triggerbotMaxPenalty > 2.f) g_cfg.triggerbotMaxPenalty = 0.35f;
    if (g_cfg.triggerbotJitter < 0 || g_cfg.triggerbotJitter > 200) g_cfg.triggerbotJitter = 15;
    if (g_cfg.triggerbotBurstGap < 0 || g_cfg.triggerbotBurstGap > 1000) g_cfg.triggerbotBurstGap = 120;
    if (g_cfg.triggerbotLatchMs < 0 || g_cfg.triggerbotLatchMs > 500) g_cfg.triggerbotLatchMs = 90;
    if (g_cfg.triggerbotMaxSpeed < 0 || g_cfg.triggerbotMaxSpeed > 400) g_cfg.triggerbotMaxSpeed = 60;
    // Chams-Style ESP
    if (g_cfg.chamsAlpha < 0.05f || g_cfg.chamsAlpha > 0.8f) g_cfg.chamsAlpha = 0.35f;
    // Aim Humanize
    if (g_cfg.aimHumanize < 0.f || g_cfg.aimHumanize > 15.f) g_cfg.aimHumanize = 2.5f;
    if (g_cfg.aimOvershoot < 0.f || g_cfg.aimOvershoot > 60.f) g_cfg.aimOvershoot = 10.f;
        // Fadenkreuz
        if (g_cfg.crosshairStyle < 0 || g_cfg.crosshairStyle > 6) g_cfg.crosshairStyle = 0;
    if (g_cfg.crosshairSize < 2.f || g_cfg.crosshairSize > 30.f) g_cfg.crosshairSize = 8.f;
    if (g_cfg.crosshairThickness < 0.5f || g_cfg.crosshairThickness > 5.f) g_cfg.crosshairThickness = 1.f;
    if (g_cfg.crosshairGap < 0.f || g_cfg.crosshairGap > 20.f) g_cfg.crosshairGap = 2.f;
    if (g_cfg.crosshairRotateSpeed < -360.f || g_cfg.crosshairRotateSpeed > 360.f) g_cfg.crosshairRotateSpeed = 90.f;
    // Recoil-Quelle
    if (g_cfg.recoilSource < 0 || g_cfg.recoilSource > 1) g_cfg.recoilSource = 1;
    // Chams (Glow)
    for (int i = 0; i < 4; ++i) {
        if (g_cfg.chamsColVisible[i] < 0.f || g_cfg.chamsColVisible[i] > 1.f) g_cfg.chamsColVisible[i] = (i == 3) ? 1.f : 0.5f;
        if (g_cfg.chamsColHidden[i]  < 0.f || g_cfg.chamsColHidden[i]  > 1.f) g_cfg.chamsColHidden[i]  = (i == 3) ? 1.f : 0.5f;
        if (g_cfg.chamsColTeam[i]    < 0.f || g_cfg.chamsColTeam[i]    > 1.f) g_cfg.chamsColTeam[i]    = (i == 3) ? 1.f : 0.5f;
        if (g_cfg.chamsItemCol[i]    < 0.f || g_cfg.chamsItemCol[i]    > 1.f) g_cfg.chamsItemCol[i]    = (i == 3) ? 1.f : 0.5f;
        if (g_cfg.chamsBombCol[i]    < 0.f || g_cfg.chamsBombCol[i]    > 1.f) g_cfg.chamsBombCol[i]    = (i == 3) ? 1.f : 0.5f;
    }
    // Arrow ESP
    if (g_cfg.arrowEspRadius < 20.f || g_cfg.arrowEspRadius > 500.f) g_cfg.arrowEspRadius = 90.f;
    if (g_cfg.arrowEspSize   < 3.f  || g_cfg.arrowEspSize   > 40.f)  g_cfg.arrowEspSize   = 9.f;
    // Smooth Aim
    if (g_cfg.smoothAimSoftness < 60.f || g_cfg.smoothAimSoftness > 97.f) g_cfg.smoothAimSoftness = 88.f;
    // Ganzkarten-Radar
    if (g_cfg.radarFullZoom < 0.4f || g_cfg.radarFullZoom > 6.f) g_cfg.radarFullZoom = 1.0f;
    // ── Entfernte Module (Menue-Bereinigung 16.10.2026): hart AUS ───────────
    // Alte Configs koennen sie noch aktiviert haben; die Felder bleiben im
    // Struct (Raw-Save-Layout!), sind aber nie mehr aktiv. Aus dem Menue
    // entfernt: Item ESP, Spotted ESP, Glow, Aim Bot, Recoil Control,
    // Marker Aim, Ball Aim, Rainbow, Coordinate Saver.
    g_cfg.itemEsp          = false;
    g_cfg.spottedEnable    = false;
    g_cfg.glow             = false;
    g_cfg.rageEnable       = false;
    g_cfg.recoilEnable     = false;
    g_cfg.markerAimEnable  = false;
    g_cfg.ballAimEnable    = false;
    g_cfg.rainbow          = false;
    g_cfg.coordSaverEnable = false;
}

static bool LoadFromPath(const char* path) {
    FILE* f = nullptr;
    if (fopen_s(&f, path, "rb") != 0 || !f) return false;
    // Abwaertskompatibel: alte Configs sind kleiner als neues Struct.
    Config tmp = g_cfg; // Defaults behalten
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    bool ok = false;
    if (sz > 0) {
        size_t toRead = (size_t)sz;
        if (toRead > sizeof(Config)) toRead = sizeof(Config);
        size_t got = fread(&tmp, 1, toRead, f);
        if (got > 0) { g_cfg = tmp; ok = true; }
    }
    fclose(f);
    if (ok) SanitizeConfig();
    return ok;
}

void SaveConfig() {
    FILE* f = nullptr;
    if (fopen_s(&f, CONFIG_PATH, "wb") == 0 && f) {
        fwrite(&g_cfg, sizeof(Config), 1, f);
        fclose(f);
    }
}

void LoadConfig() {
    LoadFromPath(CONFIG_PATH);
    SanitizeConfig();
}
void SaveConfigDebug() {
    printf("[cfg] Saved %zu bytes to %s\n", sizeof(Config), CONFIG_PATH);
}

// ── Slots ────────────────────────────────────────────────────────────────────
void ListConfigs(std::vector<std::string>& out) {
    out.clear();
    std::error_code ec;
    fs::create_directories(ConfigsDir(), ec);
    for (auto& e : fs::directory_iterator(ConfigsDir(), ec)) {
        if (!e.is_regular_file(ec)) continue;
        auto p = e.path();
        if (p.extension() != ".cfg") continue;
        out.push_back(p.stem().string());
    }
    std::sort(out.begin(), out.end());
}

bool SaveConfigAs(const char* name) {
    std::string safe = SanitizeName(name);
    if (safe.empty()) return false;
    std::error_code ec;
    fs::create_directories(ConfigsDir(), ec);
    std::string path = ConfigsDir() + "/" + safe + ".cfg";
    FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "wb") != 0 || !f) return false;
    fwrite(&g_cfg, sizeof(Config), 1, f);
    fclose(f);
    printf("[cfg] Gespeichert: %s (%zu Bytes)\n", path.c_str(), sizeof(Config));
    return true;
}

bool LoadConfigByName(const char* name) {
    std::string safe = SanitizeName(name);
    if (safe.empty()) return false;
    std::string path = ConfigsDir() + "/" + safe + ".cfg";
    bool ok = LoadFromPath(path.c_str());
    printf("[cfg] %s: %s\n", ok ? "Geladen" : "FEHLER", path.c_str());
    return ok;
}

bool DeleteConfig(const char* name) {
    std::string safe = SanitizeName(name);
    if (safe.empty()) return false;
    std::string path = ConfigsDir() + "/" + safe + ".cfg";
    std::error_code ec;
    bool ok = fs::remove(path, ec);
    printf("[cfg] %s: %s\n", ok ? "Gelöscht" : "FEHLER", path.c_str());
    return ok && !ec;
}

bool ImportConfig() {
#ifdef _WIN32
    char file[MAX_PATH] = {};
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = "Config (*.cfg)\0*.cfg\0Alle Dateien (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrTitle = "Config importieren";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameA(&ofn)) return false;
#else
    // Linux: kein commdlg — zenity nutzen falls vorhanden.
    char file[1024] = {};
    FILE* z = popen("zenity --file-selection --title='Config importieren' "
                    "--file-filter='Config (*.cfg)' 2>/dev/null", "r");
    if (!z || !fgets(file, sizeof(file), z)) {
        if (z) pclose(z);
        printf("[cfg] Import: zenity fehlt/abgebrochen (Datei manuell nach configs/ kopieren)\n");
        return false;
    }
    pclose(z);
    file[strcspn(file, "\r\n")] = '\0';
    if (!file[0]) return false;
#endif
    std::error_code ec;
    fs::create_directories(ConfigsDir(), ec);
    std::string dest = ConfigsDir() + "/" + fs::path(file).filename().string();
    fs::copy_file(file, dest, fs::copy_options::overwrite_existing, ec);
    printf("[cfg] Importiert: %s\n", dest.c_str());
    return !ec;
}

bool ExportConfig(const char* name) {
    std::string safe = SanitizeName(name);
    if (safe.empty()) return false;
    std::string src = ConfigsDir() + "/" + safe + ".cfg";
    if (!fs::exists(src)) return false;
#ifdef _WIN32
    char file[MAX_PATH] = {};
    snprintf(file, sizeof(file), "%s.cfg", safe.c_str());
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = "Config (*.cfg)\0*.cfg\0Alle Dateien (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrTitle = "Config exportieren";
    ofn.lpstrDefExt = "cfg";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameA(&ofn)) return false;
#else
    // Linux: Default = CWD/<name>.cfg (zenity falls vorhanden).
    std::string file = std::string("./") + safe + ".cfg";
    {
        FILE* z = popen("zenity --file-selection --save --confirm-overwrite "
                        "--title='Config exportieren' 2>/dev/null", "r");
        if (z) {
            char sel[1024] = {};
            if (fgets(sel, sizeof(sel), z)) {
                sel[strcspn(sel, "\r\n")] = '\0';
                if (sel[0]) file = sel;
            }
            pclose(z);
        }
    }
#endif
    std::error_code ec;
#ifdef _WIN32
    fs::copy_file(src, file, fs::copy_options::overwrite_existing, ec);
    printf("[cfg] Exportiert: %s\n", file);
#else
    fs::copy_file(src, file, fs::copy_options::overwrite_existing, ec);
    printf("[cfg] Exportiert: %s\n", file.c_str());
#endif
    return !ec;
}

void OpenConfigsFolder() {
    std::error_code ec;
    fs::create_directories(ConfigsDir(), ec);
#ifdef _WIN32
    ShellExecuteA(nullptr, "open", ConfigsDir().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
    std::string cmd = "xdg-open \"" + ConfigsDir() + "\" >/dev/null 2>&1 &";
    if (system(cmd.c_str()) != 0)
        printf("[cfg] Ordner: %s\n", ConfigsDir().c_str());
#endif
}
