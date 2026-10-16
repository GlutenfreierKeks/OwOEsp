// ─────────────────────────────────────────────────────────────────────────────
// core/OffsetsUpdater.cpp — siehe OffsetsUpdater.h
// ─────────────────────────────────────────────────────────────────────────────
#include "pch.h"
#include "../core/OffsetsUpdater.h"

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#else
#include <curl/curl.h>
#endif
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <thread>
#include <vector>

// Diagnose-Spiegel: alle printf dieser Datei landen zusätzlich in OwOEsp.log.
extern FILE* g_logFile;
#undef printf
#define printf(...) do { ::printf(__VA_ARGS__); \
    if (g_logFile) { ::fprintf(g_logFile, __VA_ARGS__); ::fflush(g_logFile); } } while (0)

#ifdef _WIN32
#pragma comment(lib, "winhttp.lib")
#endif

#ifdef _WIN32
#ifndef WINHTTP_HEADER_NAME_BY_VALUE
#define WINHTTP_HEADER_NAME_BY_VALUE ((LPCWSTR)0)
#endif
#endif

namespace offsetsupdater {

namespace {

struct RegistryEntry {
    std::string name;
    uintptr_t   fallback;
    uintptr_t*  ptr;
    bool        locked = false; // per Signatur-Scan fixiert (Updater fasst an)
};

// Hosts / Pfade pro logischer Datei (jsDelivr + raw GitHub)
struct LogicalFile {
    const char* jsHost;
    const char* jsPath;
    const char* rawHost;
    const char* rawPath;
};

static const LogicalFile kFiles[3] = {
    { "cdn.jsdelivr.net",          "/gh/a2x/cs2-dumper@main/output/offsets.hpp",
      "raw.githubusercontent.com", "/a2x/cs2-dumper/main/output/offsets.hpp" },
    { "cdn.jsdelivr.net",          "/gh/a2x/cs2-dumper@main/output/client_dll.hpp",
      "raw.githubusercontent.com", "/a2x/cs2-dumper/main/output/client_dll.hpp" },
    // Bhop-Buttons (jump/attack) — eigenes File im Dumper
    { "cdn.jsdelivr.net",          "/gh/a2x/cs2-dumper@main/output/buttons.hpp",
      "raw.githubusercontent.com", "/a2x/cs2-dumper/main/output/buttons.hpp" },
};

constexpr int kTotalLimit = 6 * 1024 * 1024;

// Minimal-Downloadgroessen je logischer Datei (Sanity gegen Fehlerseiten).
static const size_t kMinSizes[3] = { 500, 100000, 300 };

static std::map<std::string, uintptr_t>& Values() {
    static std::map<std::string, uintptr_t> m;
    return m;
}

// Stale-Cache-Schutz: Der Cache wird nur benutzt, wenn er mindestens so neu
// ist wie die gebundelten Fallbacks (Dump-Datum unten). Ein alter Cache hatte
// sonst Vorrang vor frischen Offsets — Real-Bug: Cache vom 09.10. lieferte
// jump=0x22324E0 + stale dwLocalPlayerController -> Bhop schrieb ins Nichts.
static constexpr const char* kBundleDate = "2026-10-15"; // a2x-Dump der Fallbacks
static std::string g_dumpDate = "?";

static std::vector<RegistryEntry>& Registry() {
    static std::vector<RegistryEntry> v;
    return v;
}

bool  g_cacheLoaded = false;

std::atomic<int>   g_status(0);
std::atomic<int>   g_applied(0);
std::atomic<bool>  g_running(false);

std::string CachePath() {
#ifdef _WIN32
    char buf[MAX_PATH];
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    char* slash = strrchr(buf, '\\');
    if (slash) slash[1] = '\0';
    return std::string(buf) + "offsets_cache.txt";
#else
    return OwO_ExeDir() + "offsets_cache.txt";
#endif
}

void LogError(const char* where, DWORD err) {
#ifdef _WIN32
    char buf[MAX_PATH];
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    char* slash = strrchr(buf, '\\');
    if (slash) slash[1] = '\0';
#else
    std::string dir = OwO_ExeDir();
#endif
    FILE* f = nullptr;
#ifdef _WIN32
    if (fopen_s(&f, (std::string(buf) + "offsets_error.txt").c_str(), "w") == 0 && f) {
#else
    if (fopen_s(&f, (dir + "offsets_error.txt").c_str(), "w") == 0 && f) {
#endif
        fprintf(f, "%s  err=0x%lX\n", where, (unsigned long)err);
        fclose(f);
    }
}

bool ParseNameValue(const char* line, std::string& name, uintptr_t& value) {
    const char* eq = nullptr;
    for (const char* q = line; *q; ++q)
        if (*q == '=') eq = q;
    if (!eq) return false;
    const char* v = eq + 1;
    while (*v == ' ' || *v == '\t' || *v == '\r') ++v;
    if (v[0] != '0' || (v[1] != 'x' && v[1] != 'X')) return false;
    v += 2;
    uintptr_t val = 0;
    bool any = false;
    while (isxdigit((unsigned char)*v)) {
        char c = *v++;
        val = val * 16 + (uintptr_t)(c <= '9' ? c - '0' : (tolower(c) - 'a' + 10));
        any = true;
    }
    if (!any) return false;
    const char* n = eq - 1;
    while (n >= line && (*n == ' ' || *n == '\t')) --n;
    const char* nEnd = n + 1;
    while (n >= line && (isalnum((unsigned char)*n) || *n == '_')) --n;
    const char* nStart = n + 1;
    if (nStart >= nEnd) return false;
    name.assign(nStart, (size_t)(nEnd - nStart));
    value = val;
    return true;
}

bool LoadCache() {
    if (g_cacheLoaded) return true;
    g_cacheLoaded = true;
    FILE* f = nullptr;
    if (fopen_s(&f, CachePath().c_str(), "r") != 0 || !f) return false;
    // Kopfzeile mit Cache-Datum prüfen: Cache nur übernehmen, wenn er
    // mindestens so neu ist wie die Fallbacks (siehe kBundleDate).
    char head[256];
    bool fresh = false;
    if (fgets(head, sizeof(head), f)) {
        const char* mark = "__cacheDate=";
        size_t ml = strlen(mark);
        if (strncmp(head, mark, ml) == 0) {
            std::string d(head + ml);
            while (!d.empty() && (d.back() == '\n' || d.back() == '\r' || d.back() == ' '))
                d.pop_back();
            fresh = (d.size() >= 10 && d.compare(0, 10, kBundleDate) >= 0);
        }
    }
    if (!fresh) {
        fclose(f);
        remove(CachePath().c_str());
        printf("[offsets] Cache verworfen (Stand < %s oder ohne Datum) — Fallback-Werte aktiv\n",
               kBundleDate);
        return false;
    }
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        std::string name;
        uintptr_t value;
        if (ParseNameValue(line, name, value))
            Values()[name] = value;
    }
    fclose(f);
    // Einmalige Reparatur: alter Parser hatte Duplikate falsch (letztes gewinnt)
    // -> m_fFlags wurde 0x63 statt 0x3F4. Bei Erkennung sofort korrigieren.
    auto it = Values().find("m_fFlags");
    if (it != Values().end() && it->second == 0x63) {
        Values().erase(it);
        // Fallback wird beim naechsten Register/Apply (0x3F4) bzw. Download (0x3F4) gesetzt
    }
    return true;
}

bool SaveCache() {
    FILE* f = nullptr;
    if (fopen_s(&f, CachePath().c_str(), "w") != 0 || !f) return false;
    // Datum = Quelle dieses Caches (Download-Stand oder gebundene Fallbacks).
    fprintf(f, "__cacheDate=%s\n",
            (g_dumpDate != "?" && g_dumpDate.size() >= 10) ? g_dumpDate.c_str() : kBundleDate);
    for (auto& kv : Values())
        fprintf(f, "%s=0x%llX\n", kv.first.c_str(), (unsigned long long)kv.second);
    fclose(f);
    return true;
}

bool DownloadFrom(const char* host, const char* path, std::vector<char>& out) {
#ifdef _WIN32
    HINTERNET hSession = WinHttpOpen(L"OwOEsp/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        LogError("WinHttpOpen", GetLastError());
        return false;
    }
    wchar_t wHost[256];
    MultiByteToWideChar(CP_ACP, 0, host, -1, wHost, 256);
    wchar_t wPath[512];
    MultiByteToWideChar(CP_ACP, 0, path, -1, wPath, 512);
    bool ok = false;
    HINTERNET hConnect = WinHttpConnect(hSession, wHost, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (hConnect) {
        HINTERNET hReq = WinHttpOpenRequest(hConnect, L"GET", wPath, nullptr,
            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        if (hReq) {
            DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
            WinHttpSetOption(hReq, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
            // Redirects erlauben
            DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
            WinHttpSetOption(hReq, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect));
            WinHttpSetTimeouts(hReq, 8000, 8000, 8000, 8000);
            if (!WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
                LogError("WinHttpSendRequest", GetLastError());
            } else if (!WinHttpReceiveResponse(hReq, nullptr)) {
                LogError("WinHttpReceiveResponse", GetLastError());
            } else {
                DWORD status = 0;
                DWORD statusLen = sizeof(status);
                WinHttpQueryHeaders(hReq, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                    WINHTTP_HEADER_NAME_BY_VALUE, &status, &statusLen, WINHTTP_NO_HEADER_INDEX);
                if (status == 200) {
                    out.clear();
                    char buf[16384];
                    while (out.size() < (size_t)kTotalLimit) {
                        DWORD read = 0;
                        if (!WinHttpReadData(hReq, buf, sizeof(buf), &read) || read == 0) break;
                        out.insert(out.end(), buf, buf + read);
                    }
                    ok = !out.empty();
                } else {
                    char sbuf[128];
                    snprintf(sbuf, sizeof(sbuf), "HTTP status %lu (%s%s)", (unsigned long)status, host, path);
                    LogError(sbuf, 0);
                }
            }
            WinHttpCloseHandle(hReq);
        }
        WinHttpCloseHandle(hConnect);
    }
    WinHttpCloseHandle(hSession);
    return ok;
#else
    // Linux: libcurl (inkl. Redirects + Timeouts wie WinHTTP-Seite).
    // Hinweis: Die Standard-URLs liefern Windows-Dumps (a2x). Für Linux-CS2
    // werden Linux-Dumps im gleichen hpp-Format benötigt — siehe README-LINUX.md
    // (kFiles unten bei Bedarf auf Linux-Quellen umbiegen).
    std::string url = std::string("https://") + host + path;
    CURL* h = curl_easy_init();
    if (!h) {
        LogError("curl_easy_init", 0);
        return false;
    }
    curl_easy_setopt(h, CURLOPT_URL, url.c_str());
    curl_easy_setopt(h, CURLOPT_USERAGENT, "OwOEsp/1.0");
    curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(h, CURLOPT_TIMEOUT_MS, 8000L);
    curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT_MS, 8000L);
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION,
        +[](char* p, size_t s, size_t n, void* u) -> size_t {
            auto* v = (std::vector<char>*)u;
            size_t add = s * n;
            if (v->size() + add > (size_t)kTotalLimit) return 0; // Abbruch bei Limit
            v->insert(v->end(), p, p + add);
            return add;
        });
    curl_easy_setopt(h, CURLOPT_WRITEDATA, &out);
    out.clear();
    CURLcode rc = curl_easy_perform(h);
    long status = 0;
    if (rc == CURLE_OK)
        curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(h);
    if (rc != CURLE_OK) {
        LogError("curl_easy_perform", (unsigned long)rc);
        return false;
    }
    if (status != 200) {
        char sbuf[128];
        snprintf(sbuf, sizeof(sbuf), "HTTP status %ld (%s%s)", status, host, path);
        LogError(sbuf, 0);
        return false;
    }
    return !out.empty();
#endif
}

void ParseDump(const std::vector<char>& data) {
    // Fix fuer Duplikate in client_dll.hpp (z.B. m_fFlags 0x3F4 vs 0x63):
    // Nur das erste Vorkommen pro Datei zulassen — danach Duplikate ignorieren.
    // Gleichzeitig: bereits im Cache vorhandene Werte duerfen bei echter
    // Aenderung (z.B. dwEntityList) einmalig ueberschrieben werden (erstes
    // Vorkommen pro Datei gewinnt).
    std::map<std::string, bool> seenThisFile;
    std::string text(data.begin(), data.end());
    const char* p = text.c_str();
    while (*p) {
        const char* nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        std::string line(p, len);
        std::string name;
        uintptr_t value;
        if (ParseNameValue(line.c_str(), name, value)) {
            if (seenThisFile.find(name) != seenThisFile.end()) {
                // Duplikat innerhalb derselben Datei -> ignorieren (erste gewinnt)
            } else {
                seenThisFile[name] = true;
                auto it = Values().find(name);
                if (it == Values().end())
                    Values()[name] = value;
                else if (it->second != value)
                    it->second = value; // echte Offset-Aenderung (z.B. nach CS2-Update)
            }
        }
        if (!nl) break;
        p = nl + 1;
    }
}

// Dump-Datum aus Datei-Text lesen ("// YYYY-MM-DD ..."), "" wenn unbekannt.
static std::string DumpDateOf(const std::vector<char>& data) {
    std::string t(data.begin(), data.end());
    size_t p = t.find("// 20");
    if (p == std::string::npos || p + 13 >= t.size()) return "";
    if (t[p+7] != '-' || t[p+10] != '-') return "";
    for (int k : {3,4,5,6,8,9,11,12}) {
        char c = t[p+k];
        if (c < '0' || c > '9') return "";
    }
    return t.substr(p + 3, 10); // YYYY-MM-DD: lexikographisch vergleichbar
}

// Versucht Download fuer eine logische Datei — holt von BEIDEN Hosts und
// behaelt den NEUEREN Dump. (jsDelivr-Cache hing 10/2026 Tage hinter raw
// hinterher und hat stille alte Offsets geliefert.)
bool DownloadLogical(int logicalIdx, std::vector<char>& out) {
    if (logicalIdx < 0 || logicalIdx > 2) return false;
    static const char* kNames[3] = { "offsets.hpp", "client_dll.hpp", "buttons.hpp" };
#ifndef _WIN32
    // Linux: a2x-Dumps enthalten WINDOWS-Adressen -> eigene Dump-Quelle per
    // env waehlen (siehe README-LINUX.md):
    //   OWO_OFFSETS_BASE=https://mein.host/cs2/output ./OwOEsp
    // Erwartet dort: offsets.hpp, client_dll.hpp, buttons.hpp (gleiches Format).
    if (const char* base = std::getenv("OWO_OFFSETS_BASE")) {
        std::string b(base);
        while (!b.empty() && b.back() == '/') b.pop_back();
        if (b.rfind("https://", 0) == 0 || b.rfind("http://", 0) == 0) {
            size_t hostStart = b.find("://") + 3;
            size_t slash = b.find('/', hostStart);
            if (slash != std::string::npos) {
                std::string host = b.substr(hostStart, slash - hostStart);
                std::string path = b.substr(slash) + "/" + kNames[logicalIdx];
                if (DownloadFrom(host.c_str(), path.c_str(), out) &&
                    out.size() >= (size_t)kMinSizes[logicalIdx]) {
                    printf("[offsets] %s via OWO_OFFSETS_BASE (%s%s)\n",
                           kNames[logicalIdx], host.c_str(), path.c_str());
                    return true;
                }
                printf("[offsets] OWO_OFFSETS_BASE-Download fehlgeschlagen/klein (%s) -> Fallback a2x\n",
                       kNames[logicalIdx]);
                out.clear();
            }
        } else {
            printf("[offsets] OWO_OFFSETS_BASE muss http(s):// enthalten -> ignoriert\n");
        }
    }
#endif
    const LogicalFile& f = kFiles[logicalIdx];
    std::vector<char> a, b;
    bool ha = DownloadFrom(f.jsHost, f.jsPath, a);
    bool hb = DownloadFrom(f.rawHost, f.rawPath, b);
    if (!ha && !hb) return false;
    if (ha && !hb) { out = std::move(a); return true; }
    if (hb && !ha) { out = std::move(b); return true; }
    std::string da = DumpDateOf(a), db = DumpDateOf(b);
    bool useB = !db.empty() && (da.empty() || db > da);
    out = std::move(useB ? b : a);
    if (da != db)
        printf("[offsets] %s: jsdelivr=%s raw=%s -> neuerer genommen\n",
               kNames[logicalIdx],
               da.empty() ? "?" : da.c_str(), db.empty() ? "?" : db.c_str());
    return true;
}

void ApplyLiveValuesInternal() {
    for (auto& e : Registry()) {
        if (!e.ptr || e.locked) continue; // Sig-Scan-Ergebnisse bleiben
        auto it = Values().find(e.name);
        if (it != Values().end())
            *e.ptr = it->second;
        else
            *e.ptr = e.fallback;
    }
}

// ── Klassen-präzise Overrides ──────────────────────────────────────────────
// Manche Feldnamen existieren in MEHREREN Klassen (z.B. m_entitySpottedState
// in C_C4 UND C_CSPlayerPawn, m_AttributeManager in C_Chicken UND
// C_EconEntity). Der generische First-Wins-Parser nimmt sonst die falsche
// Klasse (Spotted von C4! Waffen-Manager vom Huhn!). Diese Tabelle zieht den
// Wert gezielt aus der richtigen Klasse — dauerhaft update-sicher.
struct ClassOverride { const char* name; const char* cls; };
static const ClassOverride kClassOverrides[] = {
    { "m_entitySpottedState", "C_CSPlayerPawn" }, // nicht C_C4 / C_Hostage
    { "m_AttributeManager",   "C_EconEntity" },   // nicht C_Chicken
    // 06.10.2026-Dump: m_hPawn steht ZWEIMAL drin (CCSCustomPlayerCamera
    // 0x600 zuerst, CBasePlayerController 0x6BC weiter hinten). First-Wins
    // nahm die Kamera (falscher Pawn-Handle -> kein Spieler/Spec). Wir
    // brauchen IMMER den Controller.
    { "m_hPawn",              "CBasePlayerController" },
};

void ApplyClassOverrides(const std::string& text) {
    for (auto& o : kClassOverrides) {
        std::string ns = std::string("namespace ") + o.cls + " {";
        size_t nsPos = text.find(ns);
        if (nsPos == std::string::npos) continue;
        size_t endPos = text.find("\nnamespace ", nsPos + ns.size());
        if (endPos == std::string::npos) endPos = text.size();
        std::string key = std::string(o.name) + " =";
        size_t fPos = text.find(key, nsPos);
        if (fPos == std::string::npos || fPos > endPos) continue;
        size_t hx = text.find("0x", fPos);
        if (hx == std::string::npos || hx > endPos) continue;
        uintptr_t val = (uintptr_t)strtoull(text.c_str() + hx, nullptr, 16);
        if (!val) continue;
        auto it = Values().find(o.name);
        if (it == Values().end() || it->second != val) {
            Values()[o.name] = val;
            printf("[offsets] Klassen-Override %s=0x%llX aus %s\n",
                   o.name, (unsigned long long)val, o.cls);
        }
    }
}

void Worker() {
    g_status.store(1);
    for (int i = 0; i < 3; i++) {
        std::vector<char> data;
        if (!DownloadLogical(i, data)) {
            // buttons.hpp ist optional (Fallback im Code) — kein Totalausfall
            if (i == 2) { LogError("buttons.hpp Download fehlgeschlagen (Fallback aktiv)", 0); break; }
            g_status.store(3);
            g_running.store(false);
            return;
        }
        if (data.size() < kMinSizes[i]) {
            LogError("Download zu klein (abgebrochen)", (DWORD)data.size());
            if (i == 2) break; // buttons.hpp optional
            g_status.store(3);
            g_running.store(false);
            return;
        }
        // Dump-Datum merken (Diagnose bei CS2-Updates)
        if (i == 0) {
            std::string d = DumpDateOf(data);
            if (!d.empty()) g_dumpDate = d;
        }
        ParseDump(data);
        // Klassen-Overrides nur für client_dll (Schemas)
        if (i == 1)
            ApplyClassOverrides(std::string(data.begin(), data.end()));
    }
    if (Values().find("dwCSGOInput") == Values().end() ||
        Values().find("m_angEyeAngles") == Values().end() ||
        Values().find("m_flC4Blow") == Values().end()) {
        LogError("Sanity-Check fehlgeschlagen", 0);
        g_status.store(3);
        g_running.store(false);
        return;
    }
    SaveCache();
    ApplyLiveValuesInternal();
    g_applied.store((int)Values().size());
    printf("[offsets] OK — Dump-Stand %s, %d Werte aktiv\n",
           g_dumpDate.c_str(), (int)Values().size());
    g_status.store(2);
    g_running.store(false);
}

} // namespace

// ── Public API ───────────────────────────────────────────────────────────────
uintptr_t Value(const char* name, uintptr_t fallback) {
    LoadCache();
    auto it = Values().find(name);
    return it != Values().end() ? it->second : fallback;
}

void Register(const char* name, uintptr_t fallback, uintptr_t* out) {
    if (!name || !out) return;
    LoadCache();
    // Duplikate vermeiden (kann bei Header-Reinkludierung passieren)
    for (auto& e : Registry()) {
        if (e.ptr == out) return;
        if (e.name == name && e.ptr != out) {
            // Gleicher Name, anderes Ziel -> trotzdem registrieren
            break;
        }
    }
    auto it = Values().find(name);
    if (it != Values().end())
        *out = it->second;
    else
        *out = fallback;
    Registry().push_back({ std::string(name), fallback, out });
}

void ApplyLiveValues() {
    ApplyLiveValuesInternal();
    g_applied.store((int)Values().size());
    if (g_status.load() != 1)
        g_status.store(2);
}

const char* DumpDate() {
    return g_dumpDate.c_str();
}

// Fixiert einen Offset-Wert (Signatur-Scan): wird gesetzt und vom Updater
// danach nicht mehr überschrieben. Kein Cache-Eintrag (jeder Start scannt neu).
void LockValue(const char* name, uintptr_t value) {
    if (!name) return;
    for (auto& e : Registry()) {
        if (e.name == name && e.ptr) {
            *e.ptr = value;
            e.locked = true;
            return;
        }
    }
}

void StartAutoRefresh() {
    if (g_running.exchange(true))
        return;
    LoadCache();
    // Bereits registrierte Ziele sofort mit Cache fuettern
    ApplyLiveValuesInternal();
    // UC-Fix: GitHub-Updater deaktiviert — a2x-dumper liefert weiterhin Stand 06.10,
    // das Spiel wurde am 09.10. aktualisiert. Fallback-Werte aus Offsets.h bleiben
    // aktiv, SigScan übernimmt das Locking der ViewMatrix.
    // std::thread(Worker).detach();
}

bool RefreshNow() {
    if (g_running.exchange(true))
        return false;
    LoadCache();
    ApplyLiveValuesInternal();
    Worker();
    return g_status.load() == 2;
}

int Status()       { return g_status.load(); }
int AppliedCount() { return g_applied.load(); }

} // namespace offsetsupdater
