// inject/OffScan.cpp — Sig-Scan für dwEntityList/dwLocalPlayer/dwViewMatrix.
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cmath>
#include "OffScan.h"
#include "GameOffsets.h"

extern void Log(const char* fmt, ...);

InjOffsets g_off{ injoff::kDwEntityList, injoff::kDwLocalPlayerController,
                  injoff::kDwViewMatrix, injoff::kMHPlayerPawn, injoff::kMIHealth,
                  injoff::kMITeamNum, injoff::kMVOldOrigin, injoff::kMName,
                  injoff::kMEyeAngles, injoff::kMViewOffset, injoff::kMCameraSvc,
                  injoff::kMFov, injoff::kMClrRender, injoff::kMRenderMode,
                  injoff::kMFlags };

volatile int g_matchPawns = 0;
volatile DWORD g_espFailTick = 0;

uintptr_t InjClientBase() {
    static uintptr_t s_client = 0;
    if (!s_client)
        s_client = (uintptr_t)GetModuleHandleW(L"client.dll");
    return s_client;
}

uintptr_t InjResolveEntity(uintptr_t es, uint32_t idx) {
    uintptr_t chunk = *(uintptr_t*)(es + 0x10 + 8 * (idx >> 9));
    if (!chunk) return 0;
    return *(uintptr_t*)(chunk + injoff::kListStride * (idx & 0x1FF));
}

uintptr_t InjResolvePawn(uintptr_t es, uint32_t handle) {
    uint32_t idx = handle & 0x7FFF;
    if (idx == 0 || idx == 0x7FFF) return 0;
    return InjResolveEntity(es, idx);
}

bool InjLocalPawn(uintptr_t& lpawn, int& team) {
    lpawn = 0;
    team = -1;
    uintptr_t client = InjClientBase();
    if (!client) return false;
    uintptr_t es = *(uintptr_t*)(client + g_off.entityList);
    uintptr_t lc = *(uintptr_t*)(client + g_off.localPlayer);
    if (!es || !lc) return false;
    uint32_t lh = *(uint32_t*)(lc + g_off.hPawn);
    lpawn = InjResolvePawn(es, lh);
    if (!lpawn) return false;
    team = *(uint8_t*)(lpawn + g_off.team);
    return team == 2 || team == 3;
}

namespace {
struct Pat {
    const char* name;
    uint8_t bytes[13];
    const char* mask;
    int len;
    bool fullImg; // true = ganzes Modul, sonst nur .text
};

bool MatchAt(const uint8_t* p, const Pat& s) {
    for (int i = 0; i < s.len; i++)
        if (s.mask[i] == 'x' && p[i] != s.bytes[i]) return false;
    return true;
}

// Scannt [from, end), sammelt Kandidaten (Ziel in .data, nicht .text).
// Gibt Ziel-RVA zurück bei GENAU 1 Kandidat, sonst 0.
uintptr_t ScanOne(const uint8_t* base, uintptr_t modSize, uintptr_t textOff,
                  uintptr_t textSize, const Pat& s) {
    uintptr_t from = s.fullImg ? 0 : textOff;
    uintptr_t end = s.fullImg ? modSize : textOff + textSize;
    uintptr_t found = 0;
    int n = 0;
    for (uintptr_t off = from; off + (uintptr_t)s.len <= end; off++) {
        if (!MatchAt(base + off, s)) continue;
        int32_t disp = 0;
        memcpy(&disp, base + off + 3, 4);
        int64_t tgt = (int64_t)off + 7 + (int64_t)disp;
        if (tgt <= 0 || (uintptr_t)tgt >= modSize) continue;
        if ((uintptr_t)tgt >= textOff && (uintptr_t)tgt < textOff + textSize)
            continue; // zeigt in Code -> kein Global
        n++;
        if (n == 1) found = (uintptr_t)tgt;
    }
    if (n != 1) {
        Log("OffScan: %s %s (Kandidaten=%d)", s.name,
            n == 0 ? "MISS" : "MEHRDEUTIG", n);
        return 0;
    }
    return found;
}
} // namespace

bool ScanGameOffsets() {
    HMODULE client = GetModuleHandleW(L"client.dll");
    if (!client) {
        Log("OffScan: client.dll noch nicht da");
        return false;
    }
    auto* dos = (IMAGE_DOS_HEADER*)client;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    auto* nt = (IMAGE_NT_HEADERS64*)((uint8_t*)client + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
    uintptr_t modSize = nt->OptionalHeader.SizeOfImage;
    if (modSize < 0x100000 || modSize > 0x80000000ULL) return false;

    // .text-Range
    uintptr_t textOff = 0, textSize = 0;
    auto* sec = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++) {
        if (memcmp(sec->Name, ".text", 5) != 0) continue;
        textOff = sec->VirtualAddress;
        textSize = sec->Misc.VirtualSize;
        break;
    }
    if (!textOff || !textSize) return false;
    const uint8_t* base = (const uint8_t*)client;

    // Muster aus extern/core/SigScan.cpp (a2x).
    Pat sigs[3] = {
        { "dwEntityList",
          { 0x48,0x89,0x0D,0,0,0,0, 0xE9,0,0,0,0, 0xCC }, "xxx????x????x", 13, false },
        { "dwLocalPlayerController",
          { 0x48,0x8B,0x05,0,0,0,0, 0x41,0x89,0xBE }, "xxx????xxx", 10, false },
        { "dwViewMatrix",
          { 0x48,0x8D,0x0D,0,0,0,0, 0x48,0xC1,0xE0,0x06 }, "xxx????xxxx", 11, true },
    };

    bool any = false;
    uintptr_t rva = ScanOne(base, modSize, textOff, textSize, sigs[0]);
    if (rva) {
        // Plausibel? EntitySystem-Pointer oder null.
        uintptr_t v = *(uintptr_t*)(base + rva);
        (void)v;
        g_off.entityList = rva;
        Log("OffScan: dwEntityList = 0x%llX", (unsigned long long)rva);
        any = true;
    }
    rva = ScanOne(base, modSize, textOff, textSize, sigs[1]);
    if (rva) {
        g_off.localPlayer = rva;
        Log("OffScan: dwLocalPlayerController = 0x%llX", (unsigned long long)rva);
        any = true;
    }
    rva = ScanOne(base, modSize, textOff, textSize, sigs[2]);
    if (rva) {
        // Validierung wie extern: m[15]==1 oder alles null (Menü).
        float m[16]{};
        memcpy(m, base + rva, sizeof(m));
        bool finite = true;
        for (int k = 0; k < 16; k++)
            if (!isfinite(m[k])) { finite = false; break; }
        bool m33 = (m[15] == 1.f);
        bool allZero = true;
        for (int k = 0; k < 16; k++)
            if (m[k] != 0.f) { allZero = false; break; }
        if (finite && (m33 || allZero)) {
            g_off.viewMatrix = rva;
            Log("OffScan: dwViewMatrix = 0x%llX (m33=%.2f)", (unsigned long long)rva, m[15]);
            any = true;
        } else {
            Log("OffScan: dwViewMatrix-Kandidat 0x%llX verworfen (Wert-Check)",
                (unsigned long long)rva);
        }
    }
    if (!any)
        Log("OffScan: nichts gefunden — Fallback-Konstanten aktiv");
    return any;
}

// ── ViewMatrix-Jäger (non-blocking State-Machine) ────────────────────────────
namespace {
enum class HuntSt { Idle, WaitMove, Pick };
HuntSt s_hunt = HuntSt::Idle;
DWORD s_huntT0 = 0;
uintptr_t s_cands[256];
float s_first[256][16];
float s_delta[256];
int s_nCands = 0;
bool s_hunted = false;

float RowLen(const float* m, int r) {
    return sqrtf(m[r*4+0]*m[r*4+0] + m[r*4+1]*m[r*4+1] + m[r*4+2]*m[r*4+2]);
}
} // namespace

void VmHunterTick() {
    if (s_hunted) return;
    DWORD now = GetTickCount();
    HMODULE client = GetModuleHandleW(L"client.dll");
    if (!client) return;

    if (s_hunt == HuntSt::Idle) {
        // Trigger: ESP-FAIL frisch (<3s) + Match (Pawns>0).
        if (g_espFailTick == 0 || now - g_espFailTick > 3000) return;
        if (g_matchPawns <= 0) return;
        auto* dos = (IMAGE_DOS_HEADER*)client;
        auto* nt = (IMAGE_NT_HEADERS64*)((uint8_t*)client + dos->e_lfanew);
        uintptr_t modSize = nt->OptionalHeader.SizeOfImage;
        uintptr_t textOff = 0, textSize = 0;
        auto* sec = IMAGE_FIRST_SECTION(nt);
        for (int i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++) {
            if (memcmp(sec->Name, ".text", 5) != 0) continue;
            textOff = sec->VirtualAddress;
            textSize = sec->Misc.VirtualSize;
            break;
        }
        s_nCands = 0;
        const uint8_t* base = (const uint8_t*)client;
        __try {
            for (uintptr_t off = 0; off + 64 <= modSize && s_nCands < 256; off += 4) {
                if (off >= textOff && off < textOff + textSize) continue;
                float m[16]{};
                memcpy(m, base + off, 64);
                if (m[15] != 1.f) continue;
                bool fin = true, any = false;
                for (int k = 0; k < 16; k++) {
                    if (!isfinite(m[k]) || fabsf(m[k]) > 100000.f) { fin = false; break; }
                    if (k < 12 && m[k] != 0.f) any = true;
                }
                if (!fin || !any) continue;
                s_cands[s_nCands] = off;
                memcpy(s_first[s_nCands], m, 64);
                s_nCands++;
                off += 60; // überlappende Fenster überspringen
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            Log("VmHunter: Scan-Exception");
            return;
        }
        if (s_nCands == 0) {
            Log("VmHunter: keine Kandidaten (Menü? kein Match-Render?)");
            g_espFailTick = 0; // später erneut versuchen
            return;
        }
        // Bereits gültige Matrix unter Kandidaten? (z.B. g_off stimmt doch)
        for (int i = 0; i < s_nCands; i++) {
            if (s_cands[i] == g_off.viewMatrix) {
                Log("VmHunter: g_off.viewMatrix unter Kandidaten — Wert prüfen statt jagen");
                s_hunted = true; // kein Re-Scan nötig
                return;
            }
        }
        // Dedupe nach WERT: identische Matrizen an mehreren RVAs (Kopien
        // pro View/Schatten) würden sonst jede Margin-Regel blockieren.
        {
            int w = 0;
            for (int i = 0; i < s_nCands; i++) {
                bool dup = false;
                for (int j = 0; j < w; j++) {
                    if (memcmp(s_first[i], s_first[j], 64) == 0) { dup = true; break; }
                }
                if (!dup) {
                    if (w != i) {
                        s_cands[w] = s_cands[i];
                        memcpy(s_first[w], s_first[i], 64);
                    }
                    w++;
                }
            }
            Log("VmHunter: %d Kandidaten (%d eindeutig) — JETZT MAUS BEWEGEN (3s)!",
                s_nCands, w);
            s_nCands = w;
        }
        s_huntT0 = now;
        s_hunt = HuntSt::WaitMove;
        return;
    }

    if (s_hunt == HuntSt::WaitMove) {
        if (now - s_huntT0 < 3000) return; // Sammelphase
        const uint8_t* base = (const uint8_t*)client;
        int best = -1;
        float bestD = 0.f, secondD = 0.f;
        __try {
            for (int i = 0; i < s_nCands; i++) {
                float b[16]{};
                memcpy(b, base + s_cands[i], 64);
                float d = 0.f;
                if (b[15] != 1.f) { s_delta[i] = -1.f; continue; }
                for (int k = 0; k < 12; k++) {
                    if (!isfinite(b[k])) { d = -1.f; break; }
                    float dd = fabsf(b[k] - s_first[i][k]);
                    if (dd > d) d = dd;
                }
                s_delta[i] = d;
                if (d < 0.f) continue;
                if (d > bestD) { secondD = bestD; bestD = d; best = i; }
                else if (d > secondD) secondD = d;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            Log("VmHunter: ReRead-Exception");
            s_hunt = HuntSt::Idle;
            return;
        }
        if (best < 0 || bestD < 1e-5f) {
            Log("VmHunter: keine Bewegung (best=%.6f) — im Match Maus bewegen!", bestD);
            s_hunt = HuntSt::Idle; // Retry später
            g_espFailTick = 0;
            return;
        }
        // ViewProj-Nachweis: nicht orthonormal (reine View hat Zeilenlänge 1).
        float rl[3] = { RowLen(s_first[best], 0), RowLen(s_first[best], 1),
                        RowLen(s_first[best], 2) };
        bool ortho = true;
        for (int r = 0; r < 3; r++)
            if (fabsf(rl[r] - 1.f) > 2e-3f) { ortho = false; break; }
        if (ortho) {
            Log("VmHunter: bester Kandidat 0x%llX orthonormal (reine View?) — kein Lock",
                (unsigned long long)s_cands[best]);
            s_hunt = HuntSt::Idle;
            g_espFailTick = 0;
            return;
        }
        if (secondD > 0.f && bestD < 2.f * secondD) {
            // Top-5 für Ferndiagnose (ich wähle dann per Hand).
            int idx[5] = { -1, -1, -1, -1, -1 };
            for (int i = 0; i < s_nCands; i++) {
                if (s_delta[i] < 0.f) continue;
                for (int t = 0; t < 5; t++) {
                    if (idx[t] < 0 || s_delta[i] > s_delta[idx[t]]) {
                        for (int u = 4; u > t; u--) idx[u] = idx[u - 1];
                        idx[t] = i;
                        break;
                    }
                }
            }
            char line[384] = {};
            int pos = snprintf(line, sizeof(line), "VmHunter: mehrdeutig (%.5f vs %.5f) Top:",
                               bestD, secondD);
            for (int t = 0; t < 5 && idx[t] >= 0 && pos < 300; t++)
                pos += snprintf(line + pos, sizeof(line) - (size_t)pos, " 0x%llX:%.3f",
                                (unsigned long long)s_cands[idx[t]], s_delta[idx[t]]);
            Log("%s", line);
            s_hunt = HuntSt::Idle;
            g_espFailTick = 0;
            return;
        }
        g_off.viewMatrix = s_cands[best];
        s_hunted = true;
        s_hunt = HuntSt::Idle;
        Log("VmHunter: dwViewMatrix = 0x%llX (Bewegung %.5f, rl=%.3f/%.3f/%.3f) — ESP aktiv!",
            (unsigned long long)s_cands[best], bestD, rl[0], rl[1], rl[2]);
        return;
    }
}
