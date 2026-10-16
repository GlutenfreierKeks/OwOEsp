// inject/OffsetFetch.cpp — a2x-Download + Mini-JSON-Parse + Cache.
#include <windows.h>
#include <winhttp.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include "OffsetFetch.h"
#include "OffScan.h" // g_off

#pragma comment(lib, "winhttp.lib")

extern void Log(const char* fmt, ...);

FreshOffsets g_fresh;

namespace {
// "key" : 12345 suchen (optional erst ab classMarker). Gibt true+Wert.
bool FindNum(const char* json, const char* key, uint64_t& out, const char* after = nullptr) {
    const char* base = json;
    if (after) {
        base = strstr(json, after);
        if (!base) return false;
    }
    char pat[128];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char* p = strstr(base, pat);
    if (!p) return false;
    p = strchr(p + strlen(pat), ':');
    if (!p) return false;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    if (*p < '0' || *p > '9') return false;
    uint64_t v = 0;
    while (*p >= '0' && *p <= '9') { v = v * 10 + (uint64_t)(*p - '0'); p++; }
    out = v;
    return true;
}

bool HttpsGet(const wchar_t* host, const wchar_t* path, std::string& out, DWORD timeoutMs) {
    out.clear();
    HINTERNET hS = WinHttpOpen(L"OwOEspInject/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                               WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hS) return false;
    WinHttpSetTimeouts(hS, timeoutMs, timeoutMs, timeoutMs, timeoutMs);
    bool ok = false;
    HINTERNET hC = WinHttpConnect(hS, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (hC) {
        HINTERNET hR = WinHttpOpenRequest(hC, L"GET", path, nullptr, WINHTTP_NO_REFERER,
                                          WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        if (hR) {
            if (WinHttpSendRequest(hR, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                   WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                WinHttpReceiveResponse(hR, nullptr)) {
                DWORD status = 0, sz = sizeof(status);
                WinHttpQueryHeaders(hR, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                    nullptr, &status, &sz, nullptr);
                if (status == 200) {
                    char buf[8192];
                    DWORD rd = 0;
                    ok = true;
                    do {
                        rd = 0;
                        if (!WinHttpReadData(hR, buf, sizeof(buf), &rd)) { ok = false; break; }
                        if (rd) out.append(buf, rd);
                    } while (rd > 0);
                }
            }
            WinHttpCloseHandle(hR);
        }
        WinHttpCloseHandle(hC);
    }
    WinHttpCloseHandle(hS);
    return ok && !out.empty();
}

void CachePath(char* dst, size_t n) {
    char tmp[MAX_PATH] = {};
    GetTempPathA(MAX_PATH, tmp);
    snprintf(dst, n, "%sOwOEspInject_offsets.txt", tmp);
}

// Build-ID (PE-Timestamp von client.dll): ändert sie sich, ist der Cache
// von einem anderen Game-Build und MUSS neu geladen werden (sonst werden
// arbeitende Werte mit stale Dump-Werten überschrieben!).
uint32_t ClientBuildId() {
    HMODULE c = GetModuleHandleW(L"client.dll");
    if (!c) return 0;
    __try {
        auto* dos = (IMAGE_DOS_HEADER*)c;
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
        auto* nt = (IMAGE_NT_HEADERS64*)((uint8_t*)c + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;
        return nt->FileHeader.TimeDateStamp;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

bool LoadCache() {
    char p[MAX_PATH] = {};
    CachePath(p, sizeof(p));
    HANDLE f = CreateFileA(p, GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    char buf[2048] = {};
    DWORD rd = 0;
    bool ok = ReadFile(f, buf, sizeof(buf) - 1, &rd, nullptr) && rd > 0;
    CloseHandle(f);
    if (!ok) return false;
    // Build-ID prüfen: anderer Game-Build = Cache ungültig (sonst würden
    // arbeitende Werte mit stale Dump-Werten überschrieben!).
    {
        uint32_t build = ClientBuildId();
        char bpat[64];
        snprintf(bpat, sizeof(bpat), "build=%u", build);
        if (!build || !strstr(buf, bpat)) {
            Log("OffsetFetch: Build-Wechsel (game=%u) — Download erzwungen", build);
            return false;
        }
    }
    // Format: k=v Zeilen
    auto get = [&](const char* k, uintptr_t& o) {
        char pat[64];
        snprintf(pat, sizeof(pat), "%s=", k);
        const char* q = strstr(buf, pat);
        if (!q) return;
        o = (uintptr_t)strtoull(q + strlen(pat), nullptr, 0);
    };
    get("entityList", g_fresh.entityList);
    get("localPlayer", g_fresh.localPlayer);
    get("viewMatrix", g_fresh.viewMatrix);
    get("hPlayerPawn", g_fresh.hPlayerPawn);
    get("iHealth", g_fresh.iHealth);
    get("iTeamNum", g_fresh.iTeamNum);
    get("vOldOrigin", g_fresh.vOldOrigin);
    get("playerName", g_fresh.playerName);
    get("eyeAngles", g_fresh.eyeAngles);
    get("viewOffset", g_fresh.viewOffset);
    get("cameraSvc", g_fresh.cameraSvc);
    get("fov", g_fresh.fov);
    get("clr", g_fresh.clr);
    get("rmode", g_fresh.rmode);
    get("flags", g_fresh.flags);
    g_fresh.ok = g_fresh.entityList && g_fresh.localPlayer && g_fresh.viewMatrix;
    return g_fresh.ok;
}

void SaveCache() {
    char p[MAX_PATH] = {};
    CachePath(p, sizeof(p));
    HANDLE f = CreateFileA(p, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    char buf[1024];
    int n = snprintf(buf, sizeof(buf),
                     "build=%u\n"
                     "entityList=0x%llX\nlocalPlayer=0x%llX\nviewMatrix=0x%llX\n"
                     "hPlayerPawn=0x%llX\niHealth=0x%llX\niTeamNum=0x%llX\n"
                     "vOldOrigin=0x%llX\nplayerName=0x%llX\neyeAngles=0x%llX\n"
                     "viewOffset=0x%llX\ncameraSvc=0x%llX\nfov=0x%llX\nclr=0x%llX\nrmode=0x%llX\nflags=0x%llX\n",
                     ClientBuildId(),
                     (unsigned long long)g_fresh.entityList,
                     (unsigned long long)g_fresh.localPlayer,
                     (unsigned long long)g_fresh.viewMatrix,
                     (unsigned long long)g_fresh.hPlayerPawn,
                     (unsigned long long)g_fresh.iHealth,
                     (unsigned long long)g_fresh.iTeamNum,
                     (unsigned long long)g_fresh.vOldOrigin,
                     (unsigned long long)g_fresh.playerName,
                     (unsigned long long)g_fresh.eyeAngles,
                     (unsigned long long)g_fresh.viewOffset,
                     (unsigned long long)g_fresh.cameraSvc,
                     (unsigned long long)g_fresh.fov,
                     (unsigned long long)g_fresh.clr,
                     (unsigned long long)g_fresh.rmode,
                     (unsigned long long)g_fresh.flags);
    DWORD wr = 0;
    WriteFile(f, buf, (DWORD)n, &wr, nullptr);
    CloseHandle(f);
}

bool ParseDumps(const std::string& off, const std::string& cli) {
    uint64_t v = 0;
    bool ok = true;
    auto req = [&](const char* k, uintptr_t& o) {
        if (FindNum(off.c_str(), k, v)) { o = (uintptr_t)v; return; }
        Log("OffsetFetch: Key fehlt: %s", k);
        ok = false;
    };
    req("dwEntityList", g_fresh.entityList);
    req("dwLocalPlayerController", g_fresh.localPlayer);
    req("dwViewMatrix", g_fresh.viewMatrix);
    auto reqC = [&](const char* k, uintptr_t& o, const char* cls = nullptr) {
        if (FindNum(cli.c_str(), k, v, cls)) { o = (uintptr_t)v; return; }
        Log("OffsetFetch: Schema-Key fehlt: %s", k);
        ok = false;
    };
    reqC("m_hPlayerPawn", g_fresh.hPlayerPawn);
    reqC("m_iHealth", g_fresh.iHealth);
    reqC("m_iTeamNum", g_fresh.iTeamNum);
    reqC("m_vOldOrigin", g_fresh.vOldOrigin);
    reqC("m_sSanitizedPlayerName", g_fresh.playerName);
    reqC("m_angEyeAngles", g_fresh.eyeAngles);
    reqC("m_vecViewOffset", g_fresh.viewOffset);
    reqC("m_pCameraServices", g_fresh.cameraSvc);
    reqC("m_iFOV", g_fresh.fov, "\"CCSPlayerBase_CameraServices\"");
    reqC("m_clrRender", g_fresh.clr);
    reqC("m_nRenderMode", g_fresh.rmode);
    reqC("m_fFlags", g_fresh.flags);
    g_fresh.ok = ok && g_fresh.entityList && g_fresh.localPlayer && g_fresh.viewMatrix;
    return g_fresh.ok;
}
} // namespace

void FetchOffsetsOnce() {
    static bool s_done = false;
    if (s_done) return;
    s_done = true;

    // 1) Cache (offline-schnell)
    if (LoadCache()) {
        Log("OffsetFetch: Cache (%d Werte) — Download übersprungen",
            (int)(g_fresh.ok ? 1 : 0));
        // Trotzdem im Hintergrund aktualisieren? Nein — Bot-Match-KISS.
        // Cache reicht; bei Fehlern manuell löschen: %TEMP%\\OwOEspInject_offsets.txt
        return;
    }
    // 2) Download (2 Dateien, je 8s Timeout)
    Log("OffsetFetch: lade a2x-Dumps ...");
    std::string off, cli;
    bool okOff = HttpsGet(L"raw.githubusercontent.com",
                           L"/a2x/cs2-dumper/main/output/offsets.json", off, 8000);
    bool okCli = okOff && HttpsGet(L"raw.githubusercontent.com",
                           L"/a2x/cs2-dumper/main/output/client_dll.json", cli, 8000);
    if (!okOff || !okCli) {
        Log("OffsetFetch: Download FEHLGESCHLAGEN (offline?) — Fallback aktiv");
        return;
    }
    Log("OffsetFetch: %u + %u Bytes", (unsigned)off.size(), (unsigned)cli.size());
    if (ParseDumps(off, cli)) {
        SaveCache();
        Log("OffsetFetch: FRISCH entity=0x%llX local=0x%llX vm=0x%llX name=0x%llX eye=0x%llX",
            (unsigned long long)g_fresh.entityList, (unsigned long long)g_fresh.localPlayer,
            (unsigned long long)g_fresh.viewMatrix, (unsigned long long)g_fresh.playerName,
            (unsigned long long)g_fresh.eyeAngles);
    } else {
        Log("OffsetFetch: Parse FEHLER — Fallback aktiv");
    }
}

bool ApplyFreshOffsets() {
    if (!g_fresh.ok) return false;
    bool changed = false;
    auto set = [&](uintptr_t& dst, uintptr_t v, const char* n) {
        if (v && dst != v) {
            Log("OffsetFetch: %s 0x%llX -> 0x%llX", n,
                (unsigned long long)dst, (unsigned long long)v);
            dst = v;
            changed = true;
        }
    };
    set(g_off.entityList, g_fresh.entityList, "dwEntityList");
    set(g_off.localPlayer, g_fresh.localPlayer, "dwLocalPlayerController");
    set(g_off.viewMatrix, g_fresh.viewMatrix, "dwViewMatrix");
    // Schema (Schutz: plausible Ranges — kein Blind-Übernehmen).
    auto sets = [&](uintptr_t& dst, uintptr_t v, const char* n, uintptr_t lo, uintptr_t hi) {
        if (v >= lo && v <= hi && dst != v) {
            Log("OffsetFetch: %s 0x%llX -> 0x%llX", n,
                (unsigned long long)dst, (unsigned long long)v);
            dst = v;
            changed = true;
        }
    };
    sets(g_off.hPawn, g_fresh.hPlayerPawn, "m_hPlayerPawn", 0x100, 0x2000);
    sets(g_off.health, g_fresh.iHealth, "m_iHealth", 0x100, 0x2000);
    sets(g_off.team, g_fresh.iTeamNum, "m_iTeamNum", 0x100, 0x2000);
    sets(g_off.origin, g_fresh.vOldOrigin, "m_vOldOrigin", 0x100, 0x4000);
    sets(g_off.pname, g_fresh.playerName, "m_sSanitizedPlayerName", 0x100, 0x2000);
    sets(g_off.eye, g_fresh.eyeAngles, "m_angEyeAngles", 0x1000, 0x8000);
    sets(g_off.voff, g_fresh.viewOffset, "m_vecViewOffset", 0x100, 0x2000);
    sets(g_off.camsvc, g_fresh.cameraSvc, "m_pCameraServices", 0x100, 0x4000);
    sets(g_off.fovm, g_fresh.fov, "m_iFOV", 0x10, 0x1000);
    sets(g_off.clr, g_fresh.clr, "m_clrRender", 0x100, 0x2000);
    sets(g_off.rmode, g_fresh.rmode, "m_nRenderMode", 0x100, 0x2000);
    sets(g_off.flags, g_fresh.flags, "m_fFlags", 0x100, 0x2000);
    return changed;
}
