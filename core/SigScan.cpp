#include "pch.h"
#include "SigScan.h"
#include "Globals.h"
#include "OffsetsUpdater.h"
#include "../features/Offsets.h"
#include "../features/Cache.h"
#include "SDK.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

// Diagnose-Spiegel: alle printf dieser Datei landen zusätzlich in OwOEsp.log
// (Remote-Diagnose ohne Console-Copy).
#undef printf
#define printf(...) do { ::printf(__VA_ARGS__); \
    if (g_logFile) { ::fprintf(g_logFile, __VA_ARGS__); ::fflush(g_logFile); } } while (0)

// ─────────────────────────────────────────────────────────────────────────────
// core/SigScan.cpp — siehe SigScan.h
// Muster = a2x/cs2-dumper src/analysis/offsets.rs (client-Block).
// Auflösung: disp32 an +3, Ziel = match + 7 + disp (RIP-relativ).
// Validierung: Ziel im Image + je nach Offset Werte-Check aus dem Puffer.
// ─────────────────────────────────────────────────────────────────────────────
namespace {

struct SigDef {
    const char* regName;   // Updater-Registry-Name
    uintptr_t*  outPtr;    // offsets:: Variable (nur fürs Log)
    uint8_t     pat[16];
    const char* mask;      // 'x' = fix, '?' = egal
    int         len;
    int         check;     // 0=nur Image, 1=+Sens-Float, 2=+ViewMatrix
    int         extraAdd;  // a2x-Sonderversatz (dwSensitivity: +8)
    bool        fullImg;   // true = ganzes Modul statt nur .text (Fallback)
};

bool MatchAt(const uint8_t* base, const SigDef& s) {
    for (int i = 0; i < s.len; i++)
        if (s.mask[i] == 'x' && base[i] != s.pat[i]) return false;
    return true;
}

bool CheckValue(const SigDef& s, const std::vector<uint8_t>& img, uintptr_t target) {
    if (s.check == 1) { // dwSensitivity: Float in der Nähe plausibel?
        // +0x58 ist Stand 06.10. — nach Game-Update kann das Feld wandern.
        // Kleines Fenster prüfen (Update-Toleranz), sonst MISS statt Fehl-Lock.
        static const uintptr_t kTry[] = { 0x58, 0x40, 0x44, 0x48, 0x4C,
                                          0x50, 0x54, 0x5C, 0x60, 0x64 };
        for (uintptr_t o : kTry) {
            if (target + o + 4 > img.size()) continue;
            float v = 0.f;
            memcpy(&v, img.data() + target + o, 4);
            if (v > 0.01f && v < 50.f) return true;
        }
        return false;
    }
    if (s.check == 2) { // dwViewMatrix: finit + [3][3]==1 — oder Menü-Null?
        // UC/a2x-Hintergrund: Im Hauptmenü ist die ViewMatrix noch Null
        // (alles 0.0). Der alte Check verlangte m[3][3]==1 und hat deshalb
        // das korrekte Pattern im Menü als MISS verworfen -> stale Updater-
        // Wert (06.10.) blieb aktiv und nach dem 09.10.-Update war die
        // Matrix dauerhaft falsch (ESP/Aim/Trigger tot).
        // Fix: Null-Matrix (alles 0) ebenfalls als Treffer werten — das
        // Pattern (RVA) stimmt trotzdem. Live-Validierung (m33==1) macht
        // später Snapshot/Hunter im Match.
        if (target + sizeof(Matrix4x4) > img.size()) return false;
        Matrix4x4 m{};
        memcpy(&m, img.data() + target, sizeof(m));
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 4; c++)
                if (!isfinite(m.m[r][c])) return false;
        if (m.m[3][3] == 1.f) return true;
        bool allZero = true;
        for (int r = 0; r < 4 && allZero; r++)
            for (int c = 0; c < 4; c++)
                if (m.m[r][c] != 0.f) { allZero = false; break; }
        return allZero;
    }
    return true;
}

} // namespace

// .text-Range bestimmen — Format per Image-Magic (MZ = PE, ELF = .so), NICHT
// per Host-OS: unter Linux läuft CS2 via Steam/Proton als Windows-PE
// (client.dll) ODER nativ als ELF (client.so) — beide Fälle müssen gehen.
// Rueckgabe: RVA + Groesse relativ zum Modulbeginn (wie der Scanner rechnet).
static void TextRangePE(const std::vector<uint8_t>& img, uintptr_t modSize,
                        uintptr_t& textOff, uintptr_t& textSize) {
    if (modSize < 0x200) return;
    if (img[0] != 'M' || img[1] != 'Z') return;
    uint32_t lfanew = 0;
    memcpy(&lfanew, img.data() + 0x3C, 4);
    if (lfanew + 6 > modSize) return;
    if (img[lfanew] != 'P' || img[lfanew+1] != 'E') return;
    uint16_t nSec = 0;
    memcpy(&nSec, img.data() + lfanew + 4 + 2, 2);
    uint16_t optSize = 0;
    memcpy(&optSize, img.data() + lfanew + 4 + 16, 2);
    uintptr_t secBase = (uintptr_t)lfanew + 4 + 20 + optSize;
    for (int s = 0; s < nSec && s < 96; s++) {
        uintptr_t se = secBase + (uintptr_t)s * 40;
        if (se + 40 > modSize) return;
        if (memcmp(img.data() + se, ".text", 5) != 0) continue;
        uint32_t vsize = 0, vaddr = 0;
        memcpy(&vsize, img.data() + se + 8, 4);
        memcpy(&vaddr, img.data() + se + 12, 4);
        uint32_t rawSize = 0;
        memcpy(&rawSize, img.data() + se + 16, 4);
        if (vaddr == 0) return;
        textOff = vaddr;
        textSize = vsize > rawSize ? vsize : rawSize;
        if (textOff + textSize > modSize) textSize = modSize - textOff;
        return;
    }
}

static void TextRangeELF(const std::vector<uint8_t>& img, uintptr_t modSize,
                         uintptr_t& textOff, uintptr_t& textSize) {
    // ELF64 (.so): PT_LOAD mit R+X suchen; RVA relativ zum niedrigsten LOAD.
    if (modSize < 0x200 || img.size() < 64) return;
    if (img[0] != 0x7F || img[1] != 'E' || img[2] != 'L' || img[3] != 'F') return;
    if (img[4] != 2) return; // nur 64-bit
    uint64_t phoff = 0;
    uint16_t phentsize = 0, phnum = 0;
    memcpy(&phoff, img.data() + 0x20, 8);
    memcpy(&phentsize, img.data() + 0x36, 2);
    memcpy(&phnum, img.data() + 0x38, 2);
    if (phentsize < 56 || phnum == 0 || phnum > 64) return;
    if (phoff + (uint64_t)phnum * phentsize > img.size()) return;
    // Basis = niedrigstes PT_LOAD-vaddr (meist 0).
    uint64_t baseVaddr = UINT64_MAX;
    for (int i = 0; i < phnum; i++) {
        uint32_t ptype = 0;
        uint64_t pvaddr = 0;
        memcpy(&ptype, img.data() + phoff + (uint64_t)i * phentsize, 4);
        memcpy(&pvaddr, img.data() + phoff + (uint64_t)i * phentsize + 8, 8);
        if (ptype == 1 /*PT_LOAD*/ && pvaddr < baseVaddr) baseVaddr = pvaddr;
    }
    if (baseVaddr == UINT64_MAX) return;
    for (int i = 0; i < phnum; i++) {
        const uint8_t* ph = img.data() + phoff + (uint64_t)i * phentsize;
        uint32_t ptype = 0, pflags = 0;
        uint64_t pvaddr = 0, pmemsz = 0;
        memcpy(&ptype, ph, 4);
        memcpy(&pflags, ph + 4, 4);
        memcpy(&pvaddr, ph + 8, 8);
        memcpy(&pmemsz, ph + 40, 8);
        if (ptype != 1 /*PT_LOAD*/) continue;
        if ((pflags & 5u) != 5u) continue; // R+X = Code
        if (pvaddr < baseVaddr) continue;
        textOff = (uintptr_t)(pvaddr - baseVaddr);
        textSize = (uintptr_t)pmemsz;
        if (textOff + textSize > modSize) textSize = modSize - textOff;
        return;
    }
}

static bool GetTextRange(const std::vector<uint8_t>& img, uintptr_t modSize,
                         uintptr_t& textOff, uintptr_t& textSize) {
    textOff = 0; textSize = 0;
    if (modSize >= 2 && img[0] == 'M' && img[1] == 'Z')
        TextRangePE(img, modSize, textOff, textSize);
    else
        TextRangeELF(img, modSize, textOff, textSize);
    return textOff && textSize;
}

// Image tolerant einlesen: 1-MB-Chunks via ReadBufferPartial. Auf Linux
// stoppt process_vm_readv an der ersten nicht gemappten Seite (Guard-Lücken
// existieren auch im geladenen Image) — ein einziger ReadBuffer über alles
// hat das komplette Image verworfen, wenn irgendwo eine Lücke war.
// Lücken bleiben im Puffer genullt (dort kein Treffer — gewollt).
// Rückgabe: true, wenn mindestens 1/8 des Images lesbar war.
static bool ReadImageTolerant(std::vector<uint8_t>& img, uintptr_t modSize) {
    const size_t kChunk = 1u << 20;
    size_t off = 0, totalGot = 0;
    while (off < (size_t)modSize) {
        size_t want = (size_t)modSize - off;
        if (want > kChunk) want = kChunk;
        size_t got = g_mem.ReadBufferPartial(g_client + off, img.data() + off, want);
        totalGot += got;
        if (got >= want)
            off += want;            // vollständig — nächster Chunk
        else
            off += got + 0x1000;    // eine Seite Lücke überspringen, dann weiter
    }
    return totalGot >= (size_t)modSize / 8;
}

// true sobald dwViewMatrix per Signatur fixiert wurde (Hunter überflüssig).
static bool s_vmLocked = false;

void SigScanOffsets() {
    if (!g_mem.IsValid() || !g_client) {
        printf("[sigscan] kein Prozess/Modul — übersprungen\n");
        return;
    }
    uintptr_t modSize = g_mem.GetModuleSize(OWO_CLIENT_MODULE);
    if (modSize < 0x100000 || modSize > 0x80000000ULL) {
        printf("[sigscan] Modulgröße unplausibel — übersprungen\n");
        return;
    }
    printf("[sigscan] lese %s Image (%llu MB) ...\n",
           OWO_CLIENT_MODULE, (unsigned long long)modSize / 1048576ULL);
    std::vector<uint8_t> img(modSize);
    if (!ReadImageTolerant(img, modSize)) {
        printf("[sigscan] Image fast nicht lesbar — übersprungen\n");
        return;
    }

    // .text-Sektion finden (PE-Header im Image)
    uintptr_t textOff = 0, textSize = 0;
    if (!GetTextRange(img, modSize, textOff, textSize)) {
        printf("[sigscan] .text nicht gefunden — übersprungen\n");
        return;
    }

    static SigDef kSigs[] = {
        // dwEntityList: 48 89 0D disp E9 ... CC
        { "dwEntityList", &offsets::dwEntityList,
          { 0x48,0x89,0x0D,0,0,0,0, 0xE9,0,0,0,0, 0xCC }, "xxx????x????x", 13, 0, 0, false },
        // dwLocalPlayerController: 48 8B 05 disp 41 89 BE
        { "dwLocalPlayerController", &offsets::dwLocalPlayerController,
          { 0x48,0x8B,0x05,0,0,0,0, 0x41,0x89,0xBE }, "xxx????xxx", 10, 0, 0, false },
        // dwGlobalVars: 48 89 15 disp 48 89 42
        { "dwGlobalVars", &offsets::dwGlobalVars,
          { 0x48,0x89,0x15,0,0,0,0, 0x48,0x89,0x42 }, "xxx????xxx", 10, 0, 0, false },
        // dwPlantedC4: 48 8B 1D disp 45 32 F6
        { "dwPlantedC4", &offsets::dwPlantedC4,
          { 0x48,0x8B,0x1D,0,0,0,0, 0x45,0x32,0xF6 }, "xxx????xxx", 10, 0, 0, false },
        // dwSensitivity: 48 8D 0D disp 0F 57 C9 0F 28 F0 (+8 a2x-Versatz)
        // Vollbild-Scan: Referenz kann ausserhalb .text liegen.
        { "dwSensitivity", &offsets::dwSensitivity,
          { 0x48,0x8D,0x0D,0,0,0,0, 0x0F,0x57,0xC9,0x0F,0x28,0xF0 }, "xxx????xxxxx", 13, 1, 8, true },
        // dwViewMatrix: 48 8D 0D disp 48 C1 E0 06 (Vollbild-Scan)
        { "dwViewMatrix", &offsets::dwViewMatrix,
          { 0x48,0x8D,0x0D,0,0,0,0, 0x48,0xC1,0xE0,0x06 }, "xxx????xxxx", 11, 2, 0, true },
        // dwCSGOInput: 48 89 05 disp 0F 57 C0 0F 11 05
        { "dwCSGOInput", &offsets::dwCSGOInput,
          { 0x48,0x89,0x05,0,0,0,0, 0x0F,0x57,0xC0,0x0F,0x11,0x05 }, "xxx????xxxxxx", 13, 0, 0, false },
    };
    const int kSigCount = (int)(sizeof(kSigs) / sizeof(kSigs[0]));
    static bool s_hit[kSigCount]{};

    int ok = 0, already = 0;
    for (int si = 0; si < kSigCount; si++) {
        const SigDef& s = kSigs[si];
        if (s_hit[si]) { already++; ok++; continue; } // schon fixiert
        // Scan-Bereich: .text, oder ganzes Image (Sens/Matrix-Referenzen
        // können ausserhalb .text stehen).
        uintptr_t scanOff = s.fullImg ? 0 : textOff;
        uintptr_t scanEnd = s.fullImg ? modSize : textOff + textSize;
        // Alle Kandidaten sammeln (statt blind erstem Treffer): Globals
        // liegen in .data — Treffer, die mitten in .text zeigen, sind
        // garantiert False-Positives (Code zeigt nicht auf Code) und werden
        // verworfen. Mehrere Kandidaten -> Warnung ins Log (Mehrdeutigkeit).
        uintptr_t cands[8]{};
        int nCands = 0;
        for (uintptr_t off = scanOff; off + (uintptr_t)s.len <= scanEnd; off++) {
            if (!MatchAt(img.data() + off, s)) continue;
            int32_t disp = 0;
            memcpy(&disp, img.data() + off + 3, 4);
            int64_t target = (int64_t)off + 7 + (int64_t)disp + (int64_t)s.extraAdd;
            if (target <= 0 || (uintptr_t)target >= modSize) continue; // ausserhalb Image
            if ((uintptr_t)target >= textOff && (uintptr_t)target < textOff + textSize)
                continue; // zeigt in .text -> kein Global, False-Positive
            // UC-Fix: ViewMatrix (check==2) Value-Check waehrend des Scans
            // ausschalten. Das Pattern "48 8D 0D disp 48 C1 E0 06" ist einmalig;
            // nach Game-Updates kann der Wertcheck fehlschlagen, obwohl das Pattern
            // stimmt. Erst nach der Kandidaten-Sammlung entscheiden.
            bool skip_check = !strcmp(s.regName, "dwViewMatrix") && s.check == 2;
            if (!skip_check && !CheckValue(s, img, (uintptr_t)target)) continue;      // Werte-Check
            if (!skip_check && nCands < 8) cands[nCands++] = (uintptr_t)target;
            if (skip_check) cands[nCands++] = (uintptr_t)target;  // Value-Check spaeter
        }
        // Ende der Kandidatensammlung
        if (nCands == 0) {
            // UC-Diagnose: Roh-Treffer zahlen (Pattern tot vs. Wert-Filter?).
            // ViewMatrix im Menü = Null-Matrix wird seit Fix oben akzeptiert;
            // 0 Roh-Treffer = Codegen hat sich geaendert (Update neuer als a2x).
            if (!strcmp(s.regName, "dwViewMatrix")) {
                int raw = 0;
                for (uintptr_t off = scanOff; off + (uintptr_t)s.len <= scanEnd; off++)
                    if (MatchAt(img.data() + off, s) && ++raw >= 10) break;
                printf("[sigscan] %-22s MISS (Updater-Wert bleibt, rawHits=%d)\n", s.regName, raw);
            } else {
                printf("[sigscan] %-22s MISS (Updater-Wert bleibt)\n", s.regName);
            }
        } else if (nCands == 1 && !strcmp(s.regName, "dwViewMatrix")) {
            // UC-Fix: genau 1 ViewMatrix-Kandidat -> Pattern ist eindeutig,
            // Value-Check kann nach Update fehlschlagen. Direkt akzeptieren.
            offsetsupdater::LockValue(s.regName, cands[0]);
            printf("[sigscan] %-22s = 0x%llX (1 Kandidat, Pattern akzeptiert)\n",
                   s.regName, (unsigned long long)cands[0]);
            if (!strcmp(s.regName, "dwViewMatrix")) s_vmLocked = true;
            s_hit[si] = true;
            ok++;
        } else if (nCands == 1) {
            // Normalfall: 1 Kandidat, Value-Check war OK oder war gar nicht noetig
            offsetsupdater::LockValue(s.regName, cands[0]);
            printf("[sigscan] %-22s = 0x%llX\n", s.regName, (unsigned long long)cands[0]);
            if (!strcmp(s.regName, "dwViewMatrix")) s_vmLocked = true;
            s_hit[si] = true;
            ok++;
        } else if (nCands > 1) {
            // Mehrere Kandidaten: Value-Check anwenden (außer bei ViewMatrix, da
            // bereits 1 Kandidat oben behandelt wurde; hier sollten es wirklich
            // mehrere echte Treffer sein)
            if (!strcmp(s.regName, "dwViewMatrix")) {
                // ViewMatrix mit mehreren Kandidaten: Value-Check filtern
                bool locked = false;
                for (int i = 0; i < nCands; i++) {
                    if (CheckValue(kSigs[si], img, cands[i])) {
                        offsetsupdater::LockValue(s.regName, cands[i]);
                        printf("[sigscan] %-22s = 0x%llX (mehrere Kandidaten, Value-Check bestanden)\n",
                               s.regName, (unsigned long long)cands[i]);
                        if (!strcmp(s.regName, "dwViewMatrix")) s_vmLocked = true;
                        s_hit[si] = true;
                        ok++;
                        locked = true;
                        break;
                    }
                }
                if (!locked) {
                    printf("[sigscan] %-22s MISS (mehrere Kandidaten, Value-Check fehlgeschlagen)\n", s.regName);
                }
            } else {
                if (nCands > 1) {
                    printf("[sigscan] %-22s %d Kandidaten, nehme ersten (0x%llX) — Pattern mehrdeutig!\n",
                           s.regName, nCands, (unsigned long long)cands[0]);
                }
                offsetsupdater::LockValue(s.regName, cands[0]);
                printf("[sigscan] %-22s = 0x%llX\n", s.regName, (unsigned long long)cands[0]);
                if (!strcmp(s.regName, "dwViewMatrix")) s_vmLocked = true;
                s_hit[si] = true;
                ok++;
            }
        }
            (void)s.outPtr;
    }
    // UC-Fallback: Falls das primäre ViewMatrix-LEA (48 8D 0D / rcx) nach
    // einem Game-Update anders kodiert ist (z.B. r9=4C 8D 0D oder
    // rax=48 8D 05 — siehe UC "CS2 updated patterns"), hier Varianten
    // probieren. Gleicher relaxter Wert-Check (Null im Menü ok).
    if (!s_vmLocked) {
        for (int si = 0; si < kSigCount; si++)
            if (!strcmp(kSigs[si].regName, "dwViewMatrix") && s_hit[si]) { /* primary ok */ goto vm_done; }
        {
            struct Var { const char* mk; uint8_t p[11]; int len; };
            // Jeweils: LEA reg,[RIP+disp] + 48 C1 E0 06 (<<6, *64)
            Var vars[2] = {
                { "xxx????xxxx", { 0x4C,0x8D,0x0D,0,0,0,0, 0x48,0xC1,0xE0,0x06 }, 11 },
                { "xxx????xxxx", { 0x48,0x8D,0x05,0,0,0,0, 0x48,0xC1,0xE0,0x06 }, 11 },
            };
            SigDef tmp{};
            tmp.regName = "dwViewMatrix"; tmp.check = 2; tmp.extraAdd = 0;
            for (auto& v : vars) {
                memcpy(tmp.pat, v.p, 11); tmp.mask = v.mk; tmp.len = v.len;
                for (uintptr_t off = 0; off + 11 <= modSize; off++) {
                    if (!MatchAt(img.data() + off, tmp)) continue;
                    int32_t disp = 0;
                    memcpy(&disp, img.data() + off + 3, 4);
                    int64_t target = (int64_t)off + 7 + (int64_t)disp;
                    if (target <= 0 || (uintptr_t)target >= modSize) continue;
                    if ((uintptr_t)target >= textOff && (uintptr_t)target < textOff + textSize) continue;
                    if (!CheckValue(tmp, img, (uintptr_t)target)) continue;
                    offsetsupdater::LockValue("dwViewMatrix", (uintptr_t)target);
                    printf("[sigscan] dwViewMatrix           = 0x%llX (Variante)\n",
                           (unsigned long long)(uintptr_t)target);
                    s_vmLocked = true;
                    for (int si = 0; si < kSigCount; si++)
                        if (!strcmp(kSigs[si].regName, "dwViewMatrix")) { s_hit[si] = true; ok++; }
                    break;
                }
                if (s_vmLocked) break;
            }
        }
    }
vm_done:
    printf("[sigscan] %d/%d per Signatur (%d schon fixiert, Rest: Updater/Cache)\n",
           ok, kSigCount, already);
}

// Später Nachscan NUR der Fehlstellen: Beim Start im Hauptmenü ist die
// ViewMatrix noch Null (Check schlägt fehl -> MISS). Sobald eine Map lädt,
// trifft der Nachscan. Updater-Werte bleiben bis dahin aktiv.
void SigScanRetryDelayed() {
    std::thread([] {
        for (int round = 0; round < 3; round++) {
            Sleep(round == 0 ? 30000 : 60000);
            if (g_stopThread) return;
            if (!g_mem.IsValid() || !g_client) return;
            printf("[sigscan] Nachscan Fehlstellen (Runde %d) ...\n", round + 1);
            SigScanOffsets();
        }
    }).detach();
}

// ViewMatrix-Jäger v2 (Notlösung wenn Pattern + Dump versagen, z.B.
// Game-Update neuer als a2x): läuft passiv im Hintergrund und prüft erst den
// Snapshot — sobald die Matrix kaputt ist (m33!=1), wird gejagt. Lock nur bei
// EINDEUTIGER Maus-Bewegung UND ViewProj-Nachweis (reine View-Matrix hat
// orthonormale Zeilen Länge 1.0 und würde falsch projizieren -> abgelehnt).
// Ohne Beweis kein Lock (Updater bleibt Fallback und heilt bei frischem Dump).
void ViewMatrixHunter() {
    std::thread([] {
        for (int attempt = 0; attempt < 12 && !s_vmLocked; attempt++) {
            Sleep(attempt == 0 ? 8000 : 10000);
            if (g_stopThread.load(std::memory_order_relaxed)) return;
            if (!g_mem.IsValid() || !g_client) return;
            if (s_vmLocked) return;
            // Snapshot-Gate: Matrix anderweitig OK (Sig/Updater) -> fertig.
            // Menü-Gate (UC-Fix 09.10.): Im Hauptmenü ist die Matrix NATÜRLICH
            // Null und es gibt keine Spieler — dann NICHT jagen (sonst 146
            // Zufalls-Kandidaten mit m[15]==1.0 und delta=0 wie im Log).
            {
                const SnapshotSet& s = SnapFront();
                if (s.game.valid && s.game.vm.m[3][3] == 1.f) {
                    printf("[vmhunter] Matrix im Snapshot gültig — nichts zu tun.\n");
                    return;
                }
                bool inMatch = s.game.valid &&
                    (s.game.playerCount > 0 || s.game.localPawnAddr != 0);
                if (!inMatch) {
                    printf("[vmhunter] Menü erkannt (keine Spieler) — warte auf Match (Versuch %d).\n",
                           attempt + 1);
                    continue;
                }
            }

            uintptr_t modSize = g_mem.GetModuleSize(OWO_CLIENT_MODULE);
            if (modSize < 0x100000 || modSize > 0x80000000ULL) continue;
            std::vector<uint8_t> img(modSize);
            if (!ReadImageTolerant(img, modSize)) continue;
            uintptr_t textOff = 0, textSize = 0;
            if (!GetTextRange(img, modSize, textOff, textSize)) continue;

            // Phase 0: Log-nur Pattern-Varianten (niemals Lock — reine Intel,
            // falls das alte LEA mit anderem Register kodiert ist).
            {
                struct Var { const char* n; uint8_t p[11]; const char* mk; };
                Var vars[2] = {
                    { "V1-r9", { 0x4C,0x8D,0x0D,0,0,0,0, 0x48,0xC1,0xE0,0x06 }, "xxx????xxxx" },
                    { "V3-rax", { 0x48,0x8D,0x05,0,0,0,0, 0x48,0xC1,0xE0,0x06 }, "xxx????xxxx" },
                };
                for (auto& v : vars) {
                    int hits = 0;
                    for (uintptr_t off = 0; off + 11 <= modSize && hits < 4; off++) {
                        bool mm = true;
                        for (int k = 0; k < 11; k++)
                            if (v.mk[k] == 'x' && img[off + k] != v.p[k]) { mm = false; break; }
                        if (!mm) continue;
                        int32_t disp = 0;
                        memcpy(&disp, img.data() + off + 3, 4);
                        int64_t tgt = (int64_t)off + 7 + (int64_t)disp;
                        if (tgt <= 0 || (uintptr_t)tgt + 64 >= modSize) continue;
                        float m[16];
                        memcpy(m, img.data() + (uintptr_t)tgt, 64);
                        if (m[15] != 1.f) continue;
                        bool fin = true;
                        for (int k = 0; k < 16; k++)
                            if (!isfinite(m[k])) { fin = false; break; }
                        if (!fin) continue;
                        printf("[vmhunter] Variante %s -> 0x%llX (Matrix-Look, nur Intel)\n",
                               v.n, (unsigned long long)(uintptr_t)tgt);
                        hits++;
                    }
                }
            }

            // Phase 1: Kandidaten — 64B-Block, m[15]==1.0 exakt, alles finit,
            // erste 12 Floats nicht komplett Null. Ausserhalb .text.
            // Überlappende Fenster (<64B) werden entdoppelt (Twins wie
            // 0x...DC/E0/E4 mit identischem Delta entfallen).
            std::vector<uintptr_t> cands;
            cands.reserve(256);
            for (uintptr_t off = 0; off + 64 <= modSize && cands.size() < 256; off += 4) {
                if (off >= textOff && off < textOff + textSize) continue;
                float m[16];
                memcpy(m, img.data() + off, 64);
                if (m[15] != 1.f) continue;
                bool fin = true, any = false;
                for (int k = 0; k < 16; k++) {
                    if (!isfinite(m[k])) { fin = false; break; }
                    if (k < 12 && m[k] != 0.f) any = true;
                }
                if (!fin || !any) continue;
                cands.push_back(off);
                off += 60; // überlappende Fenster überspringen (64B-Blöcke)
            }
            if (cands.empty()) {
                printf("[vmhunter] keine Kandidaten (Versuch %d)\n", attempt + 1);
                continue;
            }
            printf("[vmhunter] %d Kandidaten — normal weiterspielen + Maus bewegen!\n",
                   (int)cands.size());

            // Phase 2: Bewegungstest — derselbe Block live 2x lesen (t0, +1.5s).
            struct Full { uintptr_t rva; float a[16]; };
            std::vector<Full> full;
            full.reserve(cands.size());
            for (uintptr_t rva : cands) {
                Full f{}; f.rva = rva;
                if (g_mem.ReadBuffer(g_client + rva, (uint8_t*)f.a, 64)) full.push_back(f);
            }
            Sleep(1500);
            if (g_stopThread.load(std::memory_order_relaxed)) return;
            struct Res { uintptr_t rva; float d; float rl[3]; };
            std::vector<Res> res;
            res.reserve(full.size());
            auto rowlen = [](const float* b, int r) {
                return sqrtf(b[r*4+0]*b[r*4+0] + b[r*4+1]*b[r*4+1] + b[r*4+2]*b[r*4+2]);
            };
            for (auto& f : full) {
                float b[16]{};
                if (!g_mem.ReadBuffer(g_client + f.rva, (uint8_t*)b, 64)) continue;
                if (b[15] != 1.f) continue; // muss Matrix bleiben
                // UC-Fix: inf/nan-Zeilen (zufällige 1.0-Floats im .data, siehe
                // Log rl=inf) sind nie die ViewMatrix — sofort verwerfen.
                bool bad = false;
                for (int k = 0; k < 16; k++)
                    if (!isfinite(b[k]) || fabsf(b[k]) > 100000.f) { bad = true; break; }
                if (bad) continue;
                float d = 0.f;
                for (int k = 0; k < 12; k++) {
                    if (!isfinite(b[k])) { d = -1.f; break; }
                    float dd = fabsf(b[k] - f.a[k]);
                    if (dd > d) d = dd;
                }
                if (d < 0.f) continue;
                Res r{ f.rva, d, { rowlen(b,0), rowlen(b,1), rowlen(b,2) } };
                res.push_back(r);
            }
            // Top-5 fürs Log (Ferndiagnose inkl. Zeilenlängen).
            for (int i = 0; i < (int)res.size(); i++)
                for (int j = i + 1; j < (int)res.size(); j++)
                    if (res[j].d > res[i].d) { Res t = res[i]; res[i] = res[j]; res[j] = t; }
            for (int i = 0; i < (int)res.size() && i < 5; i++)
                printf("[vmhunter] Kandidat 0x%llX delta=%.5f rl=%.3f/%.3f/%.3f\n",
                       (unsigned long long)res[i].rva, res[i].d,
                       res[i].rl[0], res[i].rl[1], res[i].rl[2]);
            if (res.empty() || res[0].d < 1e-5f) {
                printf("[vmhunter] keine Bewegung (Versuch %d) — laufe im Match + Maus bewegen.\n",
                       attempt + 1);
                continue;
            }
            if (res.size() > 1 && res[0].d < 3.f * res[1].d) {
                printf("[vmhunter] mehrdeutig (best=%.5f zweit=%.5f) — kein Lock, weiter beobachten.\n",
                       res[0].d, res[1].d);
                continue;
            }
            // ViewProj-Nachweis: reine View-Matrix hat orthonormale Zeilen
            // (alle Länge 1.0) und projiziert mit unserem W2S falsch -> aus
            // den Top-3 den ersten NICHT-orthonormalen nehmen.
            Res* win = nullptr;
            for (int i = 0; i < (int)res.size() && i < 3; i++) {
                bool ortho = true;
                for (int r = 0; r < 3; r++)
                    if (fabsf(res[i].rl[r] - 1.f) > 2e-3f) { ortho = false; break; }
                if (!ortho) { win = &res[i]; break; }
            }
            if (!win) {
                printf("[vmhunter] Top-3 alle orthonormal (=reine View?) — kein Lock, weiter beobachten.\n");
                continue;
            }
            // Gewinner final validieren + Matrix ins Log + locken.
            float w[16]{};
            if (!g_mem.ReadBuffer(g_client + win->rva, (uint8_t*)w, 64) || w[15] != 1.f) {
                printf("[vmhunter] Gewinner verworfen (Race) — weiter beobachten.\n");
                continue;
            }
            printf("[vmhunter] Matrix @0x%llX: [%.3f %.3f %.3f %.1f] [%.3f %.3f %.3f %.1f] [%.3f %.3f %.3f %.1f] [%.3f %.3f %.3f %.1f]\n",
                   (unsigned long long)win->rva,
                   w[0], w[1], w[2], w[3], w[4], w[5], w[6], w[7],
                   w[8], w[9], w[10], w[11], w[12], w[13], w[14], w[15]);
            offsetsupdater::LockValue("dwViewMatrix", win->rva);
            s_vmLocked = true;
            printf("[vmhunter] dwViewMatrix = 0x%llX (Bewegung %.4f) — ESP/Aim aktiv!\n",
                   (unsigned long long)win->rva, win->d);
            return;
        }
        if (!s_vmLocked)
            printf("[vmhunter] Pause — frischen a2x-Dump abwarten (Updater heilt von selbst).\n");
    }).detach();
}
