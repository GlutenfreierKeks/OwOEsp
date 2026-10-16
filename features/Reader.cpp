#include "pch.h"
#include "Reader.h"
#include "Offsets.h"
#include "Weapons.h"
#include "../core/Globals.h"
#include <cstring>
#include <cstddef>
#include <cstdio>

// Diagnose-Spiegel ins Log (Remote-Diagnose).
extern FILE* g_logFile;
#undef printf
#define printf(...) do { ::printf(__VA_ARGS__); \
    if (g_logFile) { ::fprintf(g_logFile, __VA_ARGS__); ::fflush(g_logFile); } } while (0)

// ─────────────────────────────────────────────────────────────────────────────
// features/Reader.cpp — der komplette Lese-Thread
// Sammelt ALLE Daten aus cs2.exe (Spieler, Bomben, Items, Waffen, Recoil-State)
// und veroeffentlicht sie unter g_cacheMutex als Snapshot. Der Render-Thread
// macht nur noch einen strukturierten Copy — keine Memory-Reads im Render!
// ─────────────────────────────────────────────────────────────────────────────

// ─────────────────────────────────────────────────────────────────────────────
// features/Reader.cpp — der komplette Lese-Thread
// Sammelt ALLE Daten aus cs2.exe und publiziert Triple-Buffer-Snapshots
// (lockfrei, kein Mutex, keine Copies im Render-Thread).
// Tiers: FAST jeden Tick (Pos/HP/Team/Flags/Vel/Vis), MED jeden 2. (Bones,
// Waffen), SLOW jeden 15. (Namen). Scans nach eigenem Takt, Chunk-hoisted.
// ─────────────────────────────────────────────────────────────────────────────

// ── Thread-/Snapshot-Globals (Deklarationen in features/Cache.h) ─────────────
std::atomic<bool> g_stopThread{ false };
std::thread       g_readThread;
std::thread       g_bhopThread;
std::thread       g_chamsThread;
std::thread       g_smoothThread;
std::atomic<int>  g_scrW{ 1920 };
std::atomic<int>  g_scrH{ 1080 };

SnapshotSet g_snap[3]{};
std::atomic<int> g_snapFront{ 0 };
ReaderStats g_readerStats{};

// ── Granaten-Selbstkalibrierung (Reader → Render, Definitions in Cache.h) ────
// Der Reader misst echte Projektile (m_vInitialVelocity / m_flDetonateTime /
// m_flSpawnTime) und passt die Pre-Throw-Formel live an — dadurch stimmen
// Landepunkt + Zünder nach kurzer Zeit exakt, ohne feste Magic Numbers.
std::atomic<float> g_nadeSpeedK{ 1090.f };
std::atomic<float> g_nadeFuseS[NADEK_COUNT] = {
    std::atomic<float>(0.f),   // Smoke:      Detonation beim Aufprall
    std::atomic<float>(0.f),   // Molotov:    Bruch beim Aufprall
    std::atomic<float>(0.f),   // Incendiary: Bruch beim Aufprall
    std::atomic<float>(1.6f),  // HE:         Zünder (kalibriert sich live)
    std::atomic<float>(1.5f),  // Flash:      Zünder (kalibriert sich live)
    std::atomic<float>(0.f),   // Decoy:      Ruhe nach Aufprall
};

// Letzter gehaltener Wurf (nur Reader-Thread): Abgleich der Formel mit dem
// echten Projektil (Standwurf → g_nadeSpeedK). Statisch, kein Lock nötig.
static float   s_nadeLastStrength = -1.f;
static Vector3 s_nadeLastVel{};
static uintptr_t s_nadeObserved[16] = {};
static int       s_nadeObservedNext = 0;
static bool NadeAlreadyObserved(uintptr_t a) {
    for (int i = 0; i < 16; ++i) if (s_nadeObserved[i] == a) return true;
    return false;
}
static void NadeMarkObserved(uintptr_t a) {
    s_nadeObserved[s_nadeObservedNext++ & 15] = a;
}

// ── QPC-Mikro-Timer (Reader-intern) ─────────────────────────────────────────
namespace {
    LARGE_INTEGER qpfOnce{};
    inline long long Qus(const LARGE_INTEGER& a, const LARGE_INTEGER& b) {
        if (!qpfOnce.QuadPart) QueryPerformanceFrequency(&qpfOnce);
        return (b.QuadPart - a.QuadPart) * 1000000LL / qpfOnce.QuadPart;
    }
}
#define QRD_NOW(v) LARGE_INTEGER v{}; QueryPerformanceCounter(&v)

// Akkumulatoren für Bones-/Waffen-Anteile (pro Tick in Stats übernommen)
static std::atomic<long long> s_accBones{ 0 };
static std::atomic<long long> s_accWeapons{ 0 };
// Letzte Scan-Zeiten (sonst 0 — Profiler zeigt ehrlich "kein Scan diesen Tick")
static long long s_usItemsTick = 0, s_usUtilTick = 0, s_usChickenTick = 0;

// ── Slot-Med-Cache (MED/SLOW-Felder pro Controller-Slot, Update-resistent) ──
struct SlotMed {
    uintptr_t ctrlAddr = 0; // Slot-Validierung (Spielerwechsel -> Neulesen)
    uint8_t   team = 0;     // Team/HP ändern sich selten -> MED-Tier
    int       health = 0;
    int     weaponId = 0;
    int     weaponIds[4] = {};
    int     weaponCount = 0;
    char    name[32]{};
    bool    hasBones = false;
    BoneScreen bones[kBoneCount]{};
    Vector3 boneWorld[kBoneCount]{};
    uintptr_t boneArray = 0;   // gecachter Bone-Array-Pointer (UC: cachen!)
    int32_t   weaponHandle = 0; // zugehörige aktive Waffe ...
    uintptr_t weaponAddr = 0;   // ... (bei gleichem Handle kein Re-Resolve)
};
static SlotMed s_med[65]; // Index 1..64 = Controller-Slot

// ── Konstanten ───────────────────────────────────────────────────────────────
namespace {
    constexpr uintptr_t kEntityListStride = 0x70;
    constexpr int       kEntityListStart  = 65;   // erste Nicht-Spieler-Entitaet
    constexpr int       kEntityListEnd    = 2048;
    // Fenster-Groessen (Slots/Tick): volle Sweeps als Block sind Stalls.
    // Ground 146 (~100ms Abdeckung @144Hz — Projektile brauchen das),
    // Chicken 146, Util 64 (~400ms reicht für Smoke/Feuer), Bombe 512.
    constexpr int kWinGround  = 146;
    constexpr int kWinChicken = 146;
    constexpr int kWinUtil    = 64;
    constexpr int kWinBomb    = 512;
}

// ── Fenster-Merge: Einträge des Fensters raus, frische rein ──────────────────
// Persistent-Caches bleiben zwischen Fenstern stabil (kein Flackern).
static void MergeGround(GroundCache& dst, const GroundItem* fresh, int n, int ws, int we) {
    int w = 0;
    for (int i = 0; i < dst.count; i++)
        if (dst.items[i].slot < ws || dst.items[i].slot >= we) dst.items[w++] = dst.items[i];
    dst.count = w;
    for (int k = 0; k < n && dst.count < MAX_GROUND_ITEMS; k++)
        dst.items[dst.count++] = fresh[k];
}

static void MergeEnt(EntAimCache& dst, const EntAimTarget* fresh, int n, int ws, int we) {
    int w = 0;
    for (int i = 0; i < dst.count; i++)
        if (dst.targets[i].slot < ws || dst.targets[i].slot >= we) dst.targets[w++] = dst.targets[i];
    dst.count = w;
    for (int k = 0; k < n && dst.count < MAX_ENT_TARGETS; k++)
        dst.targets[dst.count++] = fresh[k];
}

static void MergeUtil(UtilCache& dst, const UtilZone* fresh, int n, int ws, int we) {
    int w = 0;
    for (int i = 0; i < dst.count; i++)
        if (dst.zones[i].slot < ws || dst.zones[i].slot >= we) dst.zones[w++] = dst.zones[i];
    dst.count = w;
    // Timer-Stempel über Fenster hinweg erhalten (Slot+Typ identisch): eine
    // Smoke/Feuer-Zone wird nur 1× je Sweep-Zyklus frisch gelesen — ohne
    // Übernahme würde der Restzeit-Countdown jedes Mal neu starten.
    // 30s-Kappe: nach einem Despawn+Slot-Reuse gilt die Zone als neu.
    DWORD nowMs = GetTickCount();
    for (int k = 0; k < n && dst.count < MAX_UTIL_ZONES; k++) {
        UtilZone z = fresh[k];
        z.firstSeenMs = 0;
        for (int i = 0; i < w; i++) {
            if (dst.zones[i].slot == z.slot && dst.zones[i].type == z.type) {
                if (dst.zones[i].firstSeenMs &&
                    (uint32_t)(nowMs - dst.zones[i].firstSeenMs) < 30000u)
                    z.firstSeenMs = dst.zones[i].firstSeenMs;
                break;
            }
        }
        if (!z.firstSeenMs) z.firstSeenMs = nowMs;
        dst.zones[dst.count++] = z;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Wichtig: Diese Funktionen laufen NUR im Reader-Thread
// ─────────────────────────────────────────────────────────────────────────────

// ViewMatrix → Bildschirmkoordinaten (aus core/SDK.h)
static bool W2S(const Vector3& world, Vector2& screen, const Matrix4x4& vm, int w, int h) {
    return WorldToScreen(world, screen, vm, w, h);
}

// LocalPawn ohne dwLocalPlayerPawn (Update-resistent): Controller lesen,
// dessen Pawn-Handle über die Entity-Liste auflösen. Nur stabile Offsets.
// Controller + EntitySystem gecacht (UC: Pointer cachen, nicht jedes Mal neu;
// Revalidierung 2s/5s — Adressen sind sitzungsstabil).
// Thread-Sicherheit: Reader-Tick UND Bhop-Thread (~1 kHz) rufen das auf.
// Mutex + 30-ms-Memo: Pointer-Caches sind geschützt, der Bhop-Spin kostet
// praktisch keine Syscalls mehr.
static uintptr_t GetLocalPawnSlow() {
    if (!g_mem.IsValid() || !g_client) return 0;
    DWORD now = GetTickCount();
    static uintptr_t s_ctrl = 0;
    static DWORD s_ctrlTime = 0;
    static uintptr_t s_es = 0;
    static DWORD s_esTime = 0;
    if (!s_es || now - s_esTime > 5000) {
        s_es = g_mem.Read<uintptr_t>(g_client + offsets::dwEntityList);
        s_esTime = now;
    }
    if (!s_es) return 0;
    if (!s_ctrl || now - s_ctrlTime > 2000) {
        s_ctrl = g_mem.Read<uintptr_t>(g_client + offsets::dwLocalPlayerController);
        s_ctrlTime = now;
    }
    uintptr_t ctrl = s_ctrl;
    if (!ctrl) return 0;
    int32_t handle = g_mem.Read<int32_t>(ctrl + schemas::m_hPlayerPawn);
    if (handle <= 0) return 0;
    int idx = handle & 0x7FFF;
    uintptr_t le = g_mem.Read<uintptr_t>(s_es + 0x10 + 8 * (idx >> 9));
    if (!le) return 0;
    return g_mem.Read<uintptr_t>(le + kEntityListStride * (idx & 0x1FF));
}

uintptr_t GetLocalPawn() {
    static std::mutex s_mx;
    static uintptr_t  s_val  = 0;
    static DWORD      s_time = 0;
    std::lock_guard<std::mutex> lk(s_mx);
    DWORD now = GetTickCount();
    if (s_time && (now - s_time) < 30) return s_val;
    s_val  = GetLocalPawnSlow();
    s_time = now;
    return s_val;
}

static uintptr_t ResolveEntityHandle(uintptr_t entitySystem, int32_t handle) {
    if (!entitySystem || handle <= 0) return 0;
    int idx = handle & 0x7FFF;
    uintptr_t list = g_mem.Read<uintptr_t>(entitySystem + 0x10 + 8 * (idx >> 9));
    if (!list) return 0;
    return g_mem.Read<uintptr_t>(list + kEntityListStride * (idx & 0x1FF));
}

// CS2 Bone-Indices die wir zeichnen (Subset von kBoneCount)
static constexpr int kSkeletonBones[] = {
    0, 2, 4, 5, 6, 8, 9, 10, 13, 14, 15, 22, 23, 24, 25, 26, 27
};

// ── Fast-Bone-Read (UC-Standard für External) ───────────────────────────────
// EIN Bulk-Read statt ~200 Einzel-Reads: BoneArray = node + m_modelState + 0x80,
// danach 30 CTransform (32 Byte, Position an +0) in einem ReadBuffer.
// Gegen Random-Lines (falscher Pointer -> Müllwerte): Kandidaten-Scoring —
// der Array gilt nur wenn Knochen (a) nah am Spieler-Ursprung, (b) davon das
// Becken (Bone 0) dicht am Ursprung und (c) verbundene Paare in der Welt
// zusammenhängen (max. 2 gerissene Paare). Sonst: kein Skeleton statt Müll.
static constexpr int kSkeletonPairsCheck[][2] = {
    {6,5},{5,4},{4,2},{2,0},
    {4,8},{8,9},{9,11},
    {4,13},{13,14},{14,16},
    {0,22},{22,23},{23,24},
    {0,25},{25,26},{26,27}
};

static int ScoreBoneBuffer(const uint8_t* raw, const Vector3& origin,
                           const Matrix4x4& vm, int scrW, int scrH,
                           PlayerData& out) {
    static constexpr int kRawBones = 30;
    static constexpr int kStride = 32;
    Vector3 wpos[kRawBones];
    bool has[kRawBones] = {};
    int ok = 0;
    for (int bi : kSkeletonBones) {
        if (bi < 0 || bi >= kRawBones) continue;
        float x, y, z;
        memcpy(&x, raw + (size_t)bi * kStride + 0, 4);
        memcpy(&y, raw + (size_t)bi * kStride + 4, 4);
        memcpy(&z, raw + (size_t)bi * kStride + 8, 4);
        if (x == 0.f && y == 0.f && z == 0.f) {
            memcpy(&x, raw + (size_t)bi * kStride + 16, 4);
            memcpy(&y, raw + (size_t)bi * kStride + 20, 4);
            memcpy(&z, raw + (size_t)bi * kStride + 24, 4);
            if (x == 0.f && y == 0.f && z == 0.f) continue;
        }
        if (!isfinite(x) || !isfinite(y) || !isfinite(z)) continue;
        if (x > 20000.f || x < -20000.f || y > 20000.f || y < -20000.f) continue;
        float dx = x - origin.x, dy = y - origin.y, dz = z - origin.z;
        if (dx*dx + dy*dy > 130.f*130.f) continue;
        if (dz < -15.f || dz > 110.f) continue;
        wpos[bi] = { x, y, z };
        has[bi] = true; ok++;
    }
    if (ok < 4) return 0;
    // Becken-Anker: Bone 0 muss nah am Entity-Ursprung liegen (Müll-Filter)
    if (!has[0]) return 0;
    {
        float dx = wpos[0].x - origin.x, dy = wpos[0].y - origin.y, dz = wpos[0].z - origin.z;
        if (dx*dx + dy*dy + dz*dz > 60.f*60.f) return 0;
    }
    // Kohärenz: verbundene Knochen müssen in der Welt zusammenhängen.
    // Gerissene Paare (>70u auseinander) = Müll-Array.
    int broken = 0;
    for (auto& pr : kSkeletonPairsCheck) {
        int a = pr[0], b = pr[1];
        if (a < 0 || a >= kRawBones || b < 0 || b >= kRawBones) continue;
        if (!has[a] || !has[b]) continue;
        float dx = wpos[a].x - wpos[b].x;
        float dy = wpos[a].y - wpos[b].y;
        float dz = wpos[a].z - wpos[b].z;
        if (dx*dx + dy*dy + dz*dz > 70.f*70.f) broken++;
    }
    if (broken > 2) return 0;
    // Erst JETZT projizieren (spart W2S für verworfene Arrays)
    int shown = 0;
    for (int bi : kSkeletonBones) {
        if (bi < 0 || bi >= kBoneCount || !has[bi]) continue;
        out.boneWorld[bi] = wpos[bi]; // Welt immer merken (Render sagt vorher)
        if (W2S(wpos[bi], out.bones[bi].pos, vm, scrW, scrH)) {
            out.bones[bi].ok = true; shown++;
        }
    }
    return (shown >= 4) ? (ok * 10 - broken) : 0;
}

static void ReadPlayerBones(uintptr_t node, Vector3 origin, PlayerData& out, const Matrix4x4& vm, int scrW, int scrH, SlotMed& sm) {
    out.hasBones = false;
    for (int i = 0; i < kBoneCount; i++) { out.bones[i] = {}; out.boneWorld[i] = {}; }
    if (!node) return;

    uintptr_t baseOff = schemas::m_modelState + schemas::kBoneArrayInModelState; // 0x140+0x80
    // Kandidaten: gecachter Array-Pointer zuerst (0 Extra-Reads bei Treffer),
    // dann Hauptpfad, dann Legacy-Fallback. Erster Score>0 gewinnt.
    struct Cand { uintptr_t addr; bool direct; };
    Cand cands[3] = { { sm.boneArray, true }, { baseOff, false }, { 0x1E0, false } };
    static constexpr int kBufSize = 30 * 32 + 16;

    PlayerData best{};
    int bestScore = 0;
    uintptr_t bestArr = 0;
    for (auto& cd : cands) {
        uintptr_t arr = 0;
        if (cd.direct) {
            arr = cd.addr;
            if (!arr || arr < 0x10000) continue;
        } else {
            if (!cd.addr) continue;
            arr = g_mem.Read<uintptr_t>(node + cd.addr);
            if (!arr || arr < 0x10000) continue;
        }
        uint8_t raw[kBufSize]{};
        if (!g_mem.ReadBuffer(arr, raw, sizeof(raw))) continue;
        PlayerData tmp{};
        int score = ScoreBoneBuffer(raw, origin, vm, scrW, scrH, tmp);
        if (score > bestScore) { best = tmp; bestScore = score; bestArr = arr; }
        if (bestScore > 0) break; // Treffer: keine weiteren Reads
    }
    if (bestScore > 0) {
        for (int i = 0; i < kBoneCount; i++) {
            out.bones[i] = best.bones[i];
            out.boneWorld[i] = best.boneWorld[i];
        }
        out.hasBones = true;
        sm.boneArray = bestArr; // für nächste Ticks cachen
    } else {
        sm.boneArray = 0; // Array ungültig geworden (Respawn/Mapwechsel)
    }
}

// Alle Spieler einsammeln: ESP-Daten + Aim-Ziele (TIERS: fast jeden Tick,
// med Argument, slow für Namen). Lite-Tick (Hintergrund) = nur Fast-Felder.
// PERFORMANCE: Alle Reads laufen über Batch-Phasen (Memory::ReadMany) — auf
// Linux wird daraus EIN process_vm_readv pro Phase statt hunderte Einzel-Syscalls.
static bool s_liteTick = false;

// Batch-Kandidat eines Controller-Slots (wird durch die Phasen angereichert).
struct RpCand {
    int       slot   = 0;
    uintptr_t ctrl   = 0;
    int32_t   handle = 0;
    uintptr_t pawn   = 0;
    uint8_t   life   = 0;      // 0 = lebt (wie T{}-Default des Einzel-Reads)
    bool      resync = false;  // anderer Controller auf dem Slot
    // Fast-Felder
    uint8_t   teamRaw = 0;     // m_iTeamNum (MED bzw. Spec-Pfad)
    uint8_t   team    = 0;
    int       health  = 0;
    uint32_t  flags   = 0;
    uintptr_t node    = 0;
    Vector3   vel{};
    uint8_t   dormByte = 0;
    bool      dormant  = false;
    bool      slotOk   = false; // lebt + Team/HP ok + Node + nicht dormant
    Vector3   origin{};
    uint8_t   spotRaw[4][20]{};
    int       spotReq[4] = { -1, -1, -1, -1 };
    // Waffen
    bool      wepFresh = false;
    uintptr_t wepWs = 0;
    int32_t   wepActiveH = 0;
    int32_t   wepVecN = 0;
    uintptr_t wepVecPtr = 0;
    int32_t   wepVecH[4]{};
    int16_t   wepActiveId = 0;
    int16_t   wepVecIds[4]{};
};

static int ReadPlayers(uintptr_t es, PlayerData* out, int maxOut, const Matrix4x4& vm, int scrW, int scrH,
                       AimTarget* aimBuf, int* aimCount,
                       const Vector3& eyePos, const Vector3& viewAngles,
                       bool medTick, bool slowTick) {
    int count = 0, ac = 0;
    if (aimCount) *aimCount = 0;
    (void)viewAngles;
    if (!es) return 0;

    uintptr_t localCtrl = g_mem.Read<uintptr_t>(g_client + offsets::dwLocalPlayerController);
    if (!localCtrl) return 0;

    // Chunk-0-ListEntry: Slots 1..64 (Controller UND Pawns) liegen dort.
    uintptr_t le0 = g_mem.Read<uintptr_t>(es + 0x10);
    if (!le0) return 0;

    // Spotted-Kandidaten in fester Reihenfolge (registrierter Fund zuerst,
    // dann Schema, dann Legacy — Semantik wie bisher).
    static uintptr_t s_spotOff = 0;
    uintptr_t spotOffs[4] = { s_spotOff, schemas::m_entitySpottedState,
                              (uintptr_t)0x1E88, (uintptr_t)0x1C60 };

    // Lokalen Controller-Index (1-64) fuer die Spotted-Mask: gecacht (Slot
    // wechselt im Match praktisch nie); Revalidierung bei Wechsel oder alle 2s.
    int localCtrlIndex = -1;
    {
        static uintptr_t s_lastCtrl = 0;
        static int s_idx = -1;
        static DWORD s_idxTime = 0;
        DWORD nowIdx = GetTickCount();
        if (localCtrl == s_lastCtrl && s_idx >= 0 && nowIdx - s_idxTime < 2000) {
            localCtrlIndex = s_idx;
        } else {
            uintptr_t fCtrls[64] = {};
            ReadReq fReq[64];
            for (int fi = 1; fi <= 64; ++fi) {
                fReq[fi - 1].addr = le0 + kEntityListStride * (uintptr_t)(uint32_t)fi;
                fReq[fi - 1].buf  = &fCtrls[fi - 1];
                fReq[fi - 1].size = sizeof(uintptr_t);
            }
            uint8_t fOk[64] = {};
            g_mem.ReadMany(fReq, 64, fOk);
            for (int fi = 1; fi <= 64; ++fi)
                if (fOk[fi - 1] && fCtrls[fi - 1] == localCtrl) { localCtrlIndex = fi; break; }
            s_lastCtrl = localCtrl;
            s_idx = localCtrlIndex;
            s_idxTime = nowIdx;
        }
    }

    // ── Phase A: Controller aller 64 Slots (1 Batch) ─────────────────────────
    RpCand cands[64]{};
    int nc = 0;
    {
        uintptr_t ctrls[64] = {};
        ReadReq reqs[64];
        for (int i = 1; i <= 64; ++i) {
            reqs[i - 1].addr = le0 + kEntityListStride * (uintptr_t)(uint32_t)i;
            reqs[i - 1].buf  = &ctrls[i - 1];
            reqs[i - 1].size = sizeof(uintptr_t);
        }
        uint8_t ok[64] = {};
        g_mem.ReadMany(reqs, 64, ok);
        for (int i = 1; i <= 64; ++i) {
            uintptr_t ctrl = ok[i - 1] ? ctrls[i - 1] : 0;
            if (!ctrl || ctrl == localCtrl) continue;
            // Slot-Resync: anderer Controller auf dem Slot -> Med/Slow neu lesen
            SlotMed& sm = s_med[i];
            RpCand& cd = cands[nc++];
            cd.slot = i;
            cd.ctrl = ctrl;
            cd.resync = (sm.ctrlAddr != ctrl);
            if (cd.resync) { sm = SlotMed{}; sm.ctrlAddr = ctrl; }
        }
    }
    if (nc == 0) return 0;

    // ── Phase B: Pawn-Handles (1 Batch; m_hPlayerPawn-Fallback einzeln) ──────
    if (schemas::m_hPawn) {
        int32_t hr[64] = {};
        ReadReq reqs[64];
        for (int j = 0; j < nc; ++j) {
            reqs[j].addr = cands[j].ctrl + schemas::m_hPawn;
            reqs[j].buf  = &hr[j];
            reqs[j].size = sizeof(int32_t);
        }
        uint8_t ok[64] = {};
        g_mem.ReadMany(reqs, nc, ok);
        for (int j = 0; j < nc; ++j)
            if (ok[j]) cands[j].handle = hr[j];
    }
    for (int j = 0; j < nc; ++j)
        if (cands[j].handle <= 0)
            cands[j].handle = g_mem.Read<int32_t>(cands[j].ctrl + schemas::m_hPlayerPawn);

    // ── Phase C: Pawn-Pointer (Chunk-Entries dedupliziert, 1 Batch) ──────────
    {
        int chId[64]; uintptr_t chVal[64] = {}; int chOf[64];
        int nch = 0;
        for (int j = 0; j < nc; ++j) {
            chOf[j] = -1;
            if (cands[j].handle <= 0) continue;
            int ch = (cands[j].handle & 0x7FFF) >> 9;
            int t = -1;
            for (int q = 0; q < nch; ++q) if (chId[q] == ch) { t = q; break; }
            if (t < 0) { t = nch; chId[nch++] = ch; }
            chOf[j] = t;
        }
        if (nch) {
            ReadReq creqs[64];
            for (int q = 0; q < nch; ++q) {
                creqs[q].addr = es + 0x10 + 8 * (uintptr_t)(uint32_t)chId[q];
                creqs[q].buf  = &chVal[q];
                creqs[q].size = sizeof(uintptr_t);
            }
            uint8_t cok[64] = {};
            g_mem.ReadMany(creqs, nch, cok);
            for (int q = 0; q < nch; ++q) if (!cok[q]) chVal[q] = 0;
        }
        ReadReq reqs[64];
        uint8_t ok[64] = {};
        int idxOf[64];
        int n = 0;
        for (int j = 0; j < nc; ++j) {
            idxOf[j] = -1;
            if (chOf[j] < 0) continue;
            uintptr_t le = chVal[chOf[j]];
            if (!le) continue;
            int idx = cands[j].handle & 0x7FFF;
            reqs[n].addr = le + kEntityListStride * (uintptr_t)(uint32_t)(idx & 0x1FF);
            reqs[n].buf  = &cands[j].pawn;
            reqs[n].size = sizeof(uintptr_t);
            idxOf[j] = n++;
        }
        if (n) g_mem.ReadMany(reqs, n, ok);
        int w = 0;
        for (int j = 0; j < nc; ++j) {
            if (idxOf[j] < 0 || !ok[idxOf[j]] || !cands[j].pawn) continue;
            if (w != j) cands[w] = cands[j];
            ++w;
        }
        nc = w;
    }
    if (nc == 0) return 0;

    // ── Phase D1a: lifeState aller Kandidaten (1 Batch) ──────────────────────
    {
        uint8_t lifeRaw[64] = {};
        ReadReq reqs[64];
        for (int j = 0; j < nc; ++j) {
            reqs[j].addr = cands[j].pawn + schemas::m_lifeState;
            reqs[j].buf  = &lifeRaw[j];
            reqs[j].size = 1;
        }
        uint8_t ok[64] = {};
        g_mem.ReadMany(reqs, nc, ok);
        for (int j = 0; j < nc; ++j) cands[j].life = lifeRaw[j]; // Fehler -> 0 (wie T{})
    }

    // ── Phase D1b: Team/HP — lebend im MED-Tier, tot nur für die Spec-Liste ──
    {
        ReadReq reqs[128];
        uint8_t ok[128] = {};
        int tOf[64], hOf[64];
        uint8_t teamRaw[64] = {}; int32_t hpRaw[64] = {};
        int n = 0;
        for (int j = 0; j < nc; ++j) {
            tOf[j] = hOf[j] = -1;
            if (cands[j].life == 0) {
                if (!(medTick || cands[j].resync)) continue;
                tOf[j] = n; reqs[n].addr = cands[j].pawn + schemas::m_iTeamNum;
                            reqs[n].buf = &teamRaw[j]; reqs[n].size = 1; ++n;
                hOf[j] = n; reqs[n].addr = cands[j].pawn + schemas::m_iHealth;
                            reqs[n].buf = &hpRaw[j];  reqs[n].size = 4; ++n;
            } else if (g_cfg.specListEnable) {
                tOf[j] = n; reqs[n].addr = cands[j].pawn + schemas::m_iTeamNum;
                            reqs[n].buf = &teamRaw[j]; reqs[n].size = 1; ++n;
            }
        }
        if (n) g_mem.ReadMany(reqs, n, ok);
        for (int j = 0; j < nc; ++j) {
            if (tOf[j] >= 0) cands[j].teamRaw = teamRaw[j];   // Spec-Pfad (dteam)
            if (hOf[j] >= 0) {                                // lebend + MED-Tier
                s_med[cands[j].slot].team   = teamRaw[j];
                s_med[cands[j].slot].health = hpRaw[j];
            }
        }
    }

    // ── Phase D2: Fast-Felder der lebenden Spieler (Gate + 1 Batch) ──────────
    int aliveIdx[64]; int nAlive = 0;
    {
        ReadReq reqs[64 * 7];
        uint8_t ok[64 * 7] = {};
        int gateOf[64];
        int n = 0;
        for (int j = 0; j < nc; ++j) {
            gateOf[j] = -1;
            if (cands[j].life != 0) continue;
            uint8_t team = s_med[cands[j].slot].team;
            if (team < 2 || team > 3) continue;
            int health = s_med[cands[j].slot].health;
            if (health <= 0 || health > 100) continue;
            cands[j].team = team;
            cands[j].health = health;
            gateOf[j] = n;
            aliveIdx[nAlive++] = j;
            reqs[n].addr = cands[j].pawn + schemas::m_fFlags;
            reqs[n].buf  = &cands[j].flags; reqs[n].size = 4; ++n;
            reqs[n].addr = cands[j].pawn + schemas::m_pGameSceneNode;
            reqs[n].buf  = &cands[j].node;  reqs[n].size = 8; ++n;
            reqs[n].addr = cands[j].pawn + schemas::m_vecAbsVelocity;
            reqs[n].buf  = &cands[j].vel;   reqs[n].size = 12; ++n;
            for (int c = 0; c < 4; ++c) {
                if (!spotOffs[c]) continue;
                cands[j].spotReq[c] = n;
                reqs[n].addr = cands[j].pawn + spotOffs[c];
                reqs[n].buf  = cands[j].spotRaw[c];
                reqs[n].size = 20;
                ++n;
            }
        }
        if (n) g_mem.ReadMany(reqs, n, ok);
        for (int k = 0; k < nAlive; ++k) {
            int j = aliveIdx[k];
            int g = gateOf[j];
            if (!ok[g + 0]) cands[j].flags = 0;
            if (!ok[g + 1]) cands[j].node  = 0;
            if (!ok[g + 2]) cands[j].vel   = {};
            // Spot-Read-Fehler bleiben 0 (spotRaw ist wert-initialisiert).
        }
    }

    // ── Phase D3: Dormant-Cull + Origin (1 Batch) ────────────────────────────
    {
        ReadReq reqs[128];
        uint8_t ok[128] = {};
        int dormOf[64], orgOf[64];
        int n = 0;
        for (int k = 0; k < nAlive; ++k) {
            int j = aliveIdx[k];
            dormOf[j] = orgOf[j] = -1;
            if (!cands[j].node) continue;
            if (schemas::m_bDormant) {
                dormOf[j] = n;
                reqs[n].addr = cands[j].node + schemas::m_bDormant;
                reqs[n].buf  = &cands[j].dormByte;
                reqs[n].size = 1;
                ++n;
            }
            orgOf[j] = n;
            reqs[n].addr = cands[j].node + schemas::m_vecAbsOrigin;
            reqs[n].buf  = &cands[j].origin;
            reqs[n].size = 12;
            ++n;
        }
        if (n) g_mem.ReadMany(reqs, n, ok);
        for (int k = 0; k < nAlive; ++k) {
            int j = aliveIdx[k];
            cands[j].dormant = (dormOf[j] >= 0) && ok[dormOf[j]] && cands[j].dormByte != 0;
            if (orgOf[j] >= 0 && !ok[orgOf[j]]) cands[j].origin = {};
            cands[j].slotOk = (!cands[j].dormant && cands[j].node != 0);
        }
    }

    // ── Phase W1-W4: Waffen (MED-Tier) in 4 Batches inkl. Item-ID-Auflösung ──
    {
        bool anyWep = false;
        for (int j = 0; j < nc; ++j) {
            RpCand& cd = cands[j];
            if (!cd.slotOk) continue;
            if (!(g_cfg.showWeapon && !s_liteTick && (medTick || cd.resync))) continue;
            cd.wepFresh = true;
            anyWep = true;
        }
        if (anyWep) {
            QRD_NOW(wp0);
            // W1: WeaponServices-Pointer.
            {
                ReadReq reqs[64]; uint8_t ok[64] = {};
                int of[64]; int n = 0;
                for (int j = 0; j < nc; ++j) {
                    of[j] = -1;
                    if (!cands[j].wepFresh) continue;
                    of[j] = n;
                    reqs[n].addr = cands[j].pawn + schemas::m_pWeaponServices;
                    reqs[n].buf  = &cands[j].wepWs;
                    reqs[n].size = sizeof(uintptr_t);
                    ++n;
                }
                if (n) g_mem.ReadMany(reqs, n, ok);
                for (int j = 0; j < nc; ++j)
                    if (of[j] >= 0 && !ok[of[j]]) cands[j].wepWs = 0;
            }
            // W2: Active-Handle + Vector (Size/Elements).
            {
                ReadReq reqs[192]; uint8_t ok[192] = {};
                int ofA[64], ofS[64], ofE[64]; int n = 0;
                for (int j = 0; j < nc; ++j) {
                    ofA[j] = ofS[j] = ofE[j] = -1;
                    RpCand& cd = cands[j];
                    if (!cd.wepFresh || !cd.wepWs) continue;
                    ofA[j] = n; reqs[n].addr = cd.wepWs + 0x60;
                                reqs[n].buf = &cd.wepActiveH; reqs[n].size = 4; ++n;
                    ofS[j] = n; reqs[n].addr = cd.wepWs + 0x48;
                                reqs[n].buf = &cd.wepVecN;    reqs[n].size = 4; ++n;
                    ofE[j] = n; reqs[n].addr = cd.wepWs + 0x50;
                                reqs[n].buf = &cd.wepVecPtr;  reqs[n].size = 8; ++n;
                }
                if (n) g_mem.ReadMany(reqs, n, ok);
                for (int j = 0; j < nc; ++j) {
                    RpCand& cd = cands[j];
                    if (!cd.wepFresh || !cd.wepWs) continue;
                    if (!ok[ofA[j]]) cd.wepActiveH = 0;
                    if (!ok[ofS[j]]) cd.wepVecN = 0;
                    if (!ok[ofE[j]]) cd.wepVecPtr = 0;
                }
            }
            // W3: Vector-Handles (max 4).
            {
                ReadReq reqs[64]; uint8_t ok[64] = {};
                int of[64]; int n = 0;
                for (int j = 0; j < nc; ++j) {
                    of[j] = -1;
                    RpCand& cd = cands[j];
                    if (!cd.wepFresh || !cd.wepWs || cd.wepVecN <= 0 || !cd.wepVecPtr) continue;
                    int cnt = (cd.wepVecN > 4) ? 4 : cd.wepVecN;
                    of[j] = n;
                    reqs[n].addr = cd.wepVecPtr;
                    reqs[n].buf  = cd.wepVecH;
                    reqs[n].size = (size_t)cnt * sizeof(int32_t);
                    ++n;
                }
                if (n) g_mem.ReadMany(reqs, n, ok);
                for (int j = 0; j < nc; ++j)
                    if (of[j] >= 0 && !ok[of[j]]) memset(cands[j].wepVecH, 0, sizeof(cands[j].wepVecH));
            }
            // W4: Weapon-Pointer + Item-IDs (EconEntity-0x1290 zuerst, dann Schema).
            {
                struct WRes { int cand; int which; int32_t handle; };
                WRes wr[320];
                int nw = 0;
                for (int j = 0; j < nc; ++j) {
                    RpCand& cd = cands[j];
                    if (!cd.wepFresh || !cd.wepWs) continue;
                    if (cd.wepActiveH > 0 && nw < 320) {
                        wr[nw].cand = j; wr[nw].which = 0; wr[nw].handle = cd.wepActiveH; ++nw;
                    }
                    int cnt = (cd.wepVecN > 4) ? 4 : (cd.wepVecN > 0 ? cd.wepVecN : 0);
                    for (int wi = 0; wi < cnt && nw < 320; ++wi)
                        if (cd.wepVecH[wi] > 0) {
                            wr[nw].cand = j; wr[nw].which = wi + 1; wr[nw].handle = cd.wepVecH[wi]; ++nw;
                        }
                }
                if (nw) {
                    // Chunk-ListEntries (dedupliziert) für alle Handles.
                    int chId[320]; uintptr_t chVal[320] = {}; int chOf[320];
                    int nch = 0;
                    for (int k = 0; k < nw; ++k) {
                        int ch = (wr[k].handle & 0x7FFF) >> 9;
                        int t = -1;
                        for (int q = 0; q < nch; ++q) if (chId[q] == ch) { t = q; break; }
                        if (t < 0) { t = nch; chId[nch++] = ch; }
                        chOf[k] = t;
                    }
                    {
                        ReadReq creqs[320];
                        for (int q = 0; q < nch; ++q) {
                            creqs[q].addr = es + 0x10 + 8 * (uintptr_t)(uint32_t)chId[q];
                            creqs[q].buf  = &chVal[q];
                            creqs[q].size = sizeof(uintptr_t);
                        }
                        uint8_t cok[320] = {};
                        g_mem.ReadMany(creqs, nch, cok);
                        for (int q = 0; q < nch; ++q) if (!cok[q]) chVal[q] = 0;
                    }
                    // Weapon-Pointer.
                    uintptr_t wrWep[320] = {};
                    {
                        ReadReq reqs[320]; uint8_t ok[320] = {};
                        int of[320]; int n = 0;
                        for (int k = 0; k < nw; ++k) {
                            of[k] = -1;
                            uintptr_t le = chVal[chOf[k]];
                            if (!le) continue;
                            int idx = wr[k].handle & 0x7FFF;
                            of[k] = n;
                            reqs[n].addr = le + kEntityListStride * (uintptr_t)(uint32_t)(idx & 0x1FF);
                            reqs[n].buf  = &wrWep[k];
                            reqs[n].size = sizeof(uintptr_t);
                            ++n;
                        }
                        if (n) g_mem.ReadMany(reqs, n, ok);
                        for (int k = 0; k < nw; ++k) if (of[k] >= 0 && !ok[of[k]]) wrWep[k] = 0;
                    }
                    // Item-IDs: Kandidaten {0x1290, m_AttributeManager}.
                    {
                        ReadReq reqs[640]; uint8_t ok[640] = {};
                        int16_t wrIds[320][2] = {};
                        int of[320][2]; int n = 0;
                        uintptr_t mgrs[2] = { (uintptr_t)0x1290, schemas::m_AttributeManager };
                        for (int k = 0; k < nw; ++k) {
                            of[k][0] = of[k][1] = -1;
                            if (!wrWep[k]) continue;
                            for (int m = 0; m < 2; ++m) {
                                if (!mgrs[m]) continue;
                                uintptr_t eiv = wrWep[k] + mgrs[m] + schemas::m_Item;
                                of[k][m] = n;
                                reqs[n].addr = eiv + schemas::m_iItemDefinitionIndex;
                                reqs[n].buf  = &wrIds[k][m];
                                reqs[n].size = sizeof(int16_t);
                                ++n;
                            }
                        }
                        if (n) g_mem.ReadMany(reqs, n, ok);
                        for (int k = 0; k < nw; ++k) {
                            int16_t id = 0;
                            for (int m = 0; m < 2; ++m) {
                                if (of[k][m] < 0) continue;
                                int16_t v = ok[of[k][m]] ? wrIds[k][m] : 0;
                                if (v > 0 && v < 1000) { id = v; break; }
                            }
                            RpCand& cd = cands[wr[k].cand];
                            if (wr[k].which == 0) cd.wepActiveId = id;
                            else cd.wepVecIds[wr[k].which - 1] = id;
                        }
                    }
                }
            }
            LARGE_INTEGER wp1{}; QueryPerformanceCounter(&wp1);
            s_accWeapons.fetch_add(Qus(wp0, wp1), std::memory_order_relaxed);
        }
    }

    // ── Ausgabe-Phase: alle Daten liegen vor — nur noch filtern + füllen ─────
    // (Syscalls hier nur noch im seltenen Spec-Pfad sowie für Namen/Bones.)
    for (int j = 0; j < nc && count < maxOut; ++j) {
        RpCand& cd = cands[j];
        SlotMed& sm = s_med[cd.slot];

        if (cd.life != 0) {
            // Tote liegen am Boden → verlässliche Bodenprobe (Ragdoll-Punkt).
            // m_vOldOrigin direkt am Pawn — der SceneNode-Origin wird bei
            // toten Kandidaten im Gate oben nicht gelesen. Füllt das
            // Höhenraster auch in Ecken, wo kein Lebender steht.
            if (cd.pawn) {
                Vector3 ov = g_mem.Read<Vector3>(cd.pawn + schemas::m_vOldOrigin);
                if (ov.x != 0.f || ov.y != 0.f || ov.z != 0.f)
                    g_groundGrid.Feed(ov.x, ov.y, ov.z, (uint32_t)GetTickCount());
            }
            // Tot: nur Spectator-Infos (wenn Modul an) — sonst skip.
            // UC-Fix (Big-Update): toter Pawn != Observer-Pawn! Kette:
            // Controller --m_hObserverPawn--> ObserverPawn --m_pObserverServices-->
            // Services --m_hObserverTarget--> Ziel-Pawn. Alter Pfad (Services
            // direkt am toten Pawn) als Fallback wenn Handle fehlt.
            if (g_cfg.specListEnable && count < maxOut) {
                uint8_t dteam = cd.teamRaw;
                if (dteam >= 2 && dteam <= 3) {
                    uintptr_t specPawnAddr = 0;
                    int specModeVal = 0;
                    auto resolvePawn = [&](int32_t handle) -> uintptr_t {
                        if (handle <= 0 || handle == (int32_t)0xFFFFFFFF) return 0;
                        int tidx = handle & 0x7FFF;
                        uintptr_t tle = g_mem.Read<uintptr_t>(
                            es + 0x10 + 8 * ((uint32_t)(tidx >> 9)));
                        if (!tle) return 0;
                        return g_mem.Read<uintptr_t>(
                            tle + kEntityListStride * ((uint32_t)(tidx & 0x1FF)));
                    };
                    // 1) Observer-Pawn über Controller (UC-Standard)
                    uintptr_t obsPawn = 0;
                    if (schemas::m_hObserverPawn) {
                        int32_t obsHandle = g_mem.Read<int32_t>(cd.ctrl + schemas::m_hObserverPawn);
                        obsPawn = resolvePawn(obsHandle);
                    }
                    // 2) Services: erst Observer-Pawn, dann Fallback toter Pawn
                    uintptr_t obs = 0;
                    if (obsPawn && schemas::m_pObserverServices)
                        obs = g_mem.Read<uintptr_t>(obsPawn + schemas::m_pObserverServices);
                    if (!obs && schemas::m_pObserverServices)
                        obs = g_mem.Read<uintptr_t>(cd.pawn + schemas::m_pObserverServices);
                    if (obs) {
                        int32_t tgtHandle = g_mem.Read<int32_t>(obs + schemas::m_hObserverTarget);
                        specModeVal = (int)g_mem.Read<uint8_t>(obs + schemas::m_iObserverMode);
                        specPawnAddr = resolvePawn(tgtHandle);
                    }
                    if (specPawnAddr) {
                        PlayerData& od = out[count];
                        od.alive = false;
                        od.team = (int)dteam;
                        od.pawnAddr = cd.pawn;
                        od.specPawn = specPawnAddr;
                        od.specMode = specModeVal;
                        char snm[32]{};
                        g_mem.ReadBuffer(cd.ctrl + schemas::m_sSanitizedPlayerName,
                                         snm, sizeof(snm) - 1);
                        memcpy(od.name, snm, 32);
                        count++;
                    }
                }
            }
            continue;
        }

        if (!cd.slotOk) continue;

        bool crouching = (cd.flags & 2) != 0; // FL_DUCKING

        // Geschwindigkeit validieren (wie bisher; Read-Fehler -> 0).
        Vector3 pvel = cd.vel;
        if (!isfinite(pvel.x) || !isfinite(pvel.y) || !isfinite(pvel.z) ||
            pvel.x > 2000.f || pvel.x < -2000.f ||
            pvel.y > 2000.f || pvel.y < -2000.f ||
            pvel.z > 2000.f || pvel.z < -2000.f)
            pvel = {};

        // Visibility: unser Spot-Kandidat mit irgendeinem Nonzero gewinnt
        // (Semantik des früheren Kandidaten-Loops, inkl. Radar-Fallback).
        bool visible = false;
        bool radarSpotted = false;
        {
            uint32_t mask0 = 0, mask1 = 0;
            bool found = false;
            for (int c = 0; c < 4; ++c) {
                uintptr_t off = spotOffs[c];
                if (!off) continue;
                uint8_t b = 0; uint32_t m0 = 0, m1 = 0;
                if (cd.spotReq[c] >= 0) {
                    memcpy(&b,  cd.spotRaw[c] + 0x8,  1);
                    memcpy(&m0, cd.spotRaw[c] + 0xC,  4);
                    memcpy(&m1, cd.spotRaw[c] + 0x10, 4);
                }
                if (m0 != 0 || m1 != 0 || b != 0) {
                    radarSpotted = b != 0;
                    mask0 = m0; mask1 = m1;
                    s_spotOff = off;
                    found = true;
                    break;
                }
                radarSpotted = b != 0;
                mask0 = m0; mask1 = m1;
            }
            if (!found) {
                // Billiger Fallback statt Brute-Scan: nur Radar-Bit
                // (Wert stammt aus dem bereits gelesenen Batch).
                uintptr_t offFb = schemas::m_entitySpottedState ? schemas::m_entitySpottedState
                                                                : (uintptr_t)0x1E88;
                int cFb = (spotOffs[1] == offFb) ? 1 : 2;
                if (cd.spotReq[cFb] >= 0) radarSpotted = cd.spotRaw[cFb][0x8] != 0;
            }
            int idx = localCtrlIndex - 1;
            bool maskVisible = false;
            if (idx >= 0 && idx < 32) maskVisible = (mask0 & (1u << idx)) != 0;
            else if (idx >= 32 && idx < 64) maskVisible = (mask1 & (1u << (idx - 32))) != 0;
            else if (idx < 0) maskVisible = radarSpotted;

            // Heuristik nur wenn idx ungültig
            if (idx < 0 && !maskVisible) {
                float dx = cd.origin.x - eyePos.x, dy = cd.origin.y - eyePos.y, dz = cd.origin.z - eyePos.z;
                float dist = sqrtf(dx*dx+dy*dy+dz*dz);
                if (dist < 1400.f && radarSpotted) maskVisible = true;
            }
            visible = maskVisible;
            // Debug fuer ersten gesammelten Spieler (count==0) alle 2s.
            // HINWEIS: nur mit debugLog — Console-I/O bremst den Reader!
            if (count==0 && g_cfg.debugLog) {
                static DWORD lastDbg = 0;
                DWORD nd = GetTickCount();
                if (nd - lastDbg > 1500) {
                    printf("[spotted] local=%d idx=%d mask0=%08X mask1=%08X rad=%d vis=%d team=%d dist=%.0f\n",
                           localCtrlIndex, idx, mask0, mask1, radarSpotted, visible, (int)cd.team,
                           sqrtf((cd.origin.x-eyePos.x)*(cd.origin.x-eyePos.x)+(cd.origin.y-eyePos.y)*(cd.origin.y-eyePos.y)));
                    lastDbg = nd;
                }
            }
        }

        if (aimBuf && ac < maxOut) {
            aimBuf[ac] = { cd.origin, (int)cd.team, cd.health, true, crouching, visible, pvel };
            ac++;
        }

        Vector3 headPos  = { cd.origin.x, cd.origin.y, cd.origin.z + (crouching ? 54.f : 72.f) };
        Vector3 chestPos = { cd.origin.x, cd.origin.y, cd.origin.z + (crouching ? 48.f : 64.f) };

        // W2S für Screen-Projektion berechnen (nicht verwerfen, damit Radar auch Off-Screen-Spieler hat!)
        W2S(cd.origin, out[count].feet,  vm, scrW, scrH);
        W2S(headPos,   out[count].head,  vm, scrW, scrH);
        W2S(chestPos,  out[count].chest, vm, scrW, scrH);

        // SLOW-Tier: Namen (alle ~15 Ticks, sonst Slot-Cache)
        // Spec-Liste braucht Zielnamen auch bei name=aus -> mit einbeziehen.
        if ((slowTick || cd.resync) && (g_cfg.name || g_cfg.specListEnable || g_menuOpen) && !s_liteTick) {
            g_mem.ReadBuffer(cd.ctrl + schemas::m_sSanitizedPlayerName, sm.name, sizeof(sm.name) - 1);
        }
        memcpy(out[count].name, sm.name, 32);

        out[count].health   = cd.health;
        out[count].team     = (int)cd.team;
        out[count].alive    = true;
        out[count].visible  = visible;
        out[count].radarSpotted = radarSpotted;
        out[count].crouching = crouching;
        out[count].worldOrigin = cd.origin;
        // Bodenprobe für das Landeprognose-Raster: Lebender Spieler = steht
        // auf einer echten Fläche (m_vecAbsOrigin = Füße).
        g_groundGrid.Feed(cd.origin.x, cd.origin.y, cd.origin.z, (uint32_t)GetTickCount());
        out[count].velocity = pvel;
        out[count].pawnAddr = cd.pawn;
        out[count].weaponId = 0;
        out[count].weaponCount = 0;
        memset(out[count].weaponIds, 0, sizeof(out[count].weaponIds));
        out[count].hasBones = false;

        // MED-Tier: Skeleton (alle 2 Ticks frisch, sonst Slot + Vorhersage).
        // Nah-Cull bleibt jeden Tick (kostenlos, kein RPM).
        if (g_cfg.skeleton && !s_liteTick) {
            float dx = cd.origin.x - eyePos.x, dy = cd.origin.y - eyePos.y, dz = cd.origin.z - eyePos.z;
            if (dx*dx + dy*dy + dz*dz < 2500.f * 2500.f) {
                if (medTick || cd.resync) {
                    QRD_NOW(b0);
                    ReadPlayerBones(cd.node, cd.origin, out[count], vm, scrW, scrH, sm);
                    LARGE_INTEGER b1{}; QueryPerformanceCounter(&b1);
                    s_accBones.fetch_add(Qus(b0, b1), std::memory_order_relaxed);
                    sm.hasBones = out[count].hasBones;
                    for (int bi = 0; bi < kBoneCount; bi++) {
                        sm.bones[bi] = out[count].bones[bi];
                        sm.boneWorld[bi] = out[count].boneWorld[bi];
                    }
                } else if (sm.hasBones) {
                    out[count].hasBones = true;
                    for (int bi = 0; bi < kBoneCount; bi++) {
                        out[count].bones[bi] = sm.bones[bi];
                        out[count].boneWorld[bi] = sm.boneWorld[bi];
                    }
                }
            }
        }

        // MED-Tier: Waffen — frisch aus den Batch-Phasen (W1-W4), sonst Slot-Cache.
        if (cd.wepFresh) {
            if (cd.wepWs) {
                out[count].weaponId = cd.wepActiveId;
                int n = (cd.wepVecN > 4) ? 4 : cd.wepVecN;
                if (cd.wepVecN > 0 && cd.wepVecPtr) {
                    for (int wi = 0; wi < n; wi++) {
                        int16_t id = cd.wepVecIds[wi];
                        if (id > 0) {
                            bool dup = false;
                            for (int q = 0; q < out[count].weaponCount; q++)
                                if (out[count].weaponIds[q] == id) { dup = true; break; }
                            if (!dup)
                                out[count].weaponIds[out[count].weaponCount++] = id;
                        }
                    }
                } else if (cd.wepActiveH > 0 && out[count].weaponId > 0) {
                    out[count].weaponIds[out[count].weaponCount++] = out[count].weaponId;
                }
            }
            sm.weaponId = out[count].weaponId;
            sm.weaponCount = out[count].weaponCount;
            for (int wi = 0; wi < 4; wi++) sm.weaponIds[wi] = out[count].weaponIds[wi];
        } else if (g_cfg.showWeapon) {
            // Slot-Cache übernehmen (kein RPM)
            out[count].weaponId = sm.weaponId;
            out[count].weaponCount = sm.weaponCount;
            for (int wi = 0; wi < 4; wi++) out[count].weaponIds[wi] = sm.weaponIds[wi];
        }

        count++;
    }

    if (aimCount) *aimCount = ac;
    return count;
}

// ─────────────────────────────────────────────────────────────────────────────
// Bombe: Entity-Scan + Schema-Reads + Fallback-Scanner
// ─────────────────────────────────────────────────────────────────────────────

static float ReadCurtime() {
    uintptr_t gvPtr = g_mem.Read<uintptr_t>(g_client + offsets::dwGlobalVars);
    if (!gvPtr) return 0.f;
    return g_mem.Read<float>(gvPtr + 0x10); // curtime
}

// ─────────────────────────────────────────────────────────────────────────────
// Kartenname (Ganzkarten-Radar): CGlobalVars enthält einen Zeiger auf den
// aktuellen Kartennamen. UC-Stand: m_uCurrentMapName bei +0x1B8 (ältere
// Builds +0x188) — beide Kandidaten werden probiert, jeweils mit Plausi-Check
// (Maps heißen de_/cs_/ar_* oder "workshop/…"). 1×/s gelesen (nur bei
// Kartenwechsel relevant); Ergebnis wird gepuffert.
// ─────────────────────────────────────────────────────────────────────────────
static bool IsPlausibleMapName(const char* s) {
    if (!s || !s[0]) return false;
    if (!strncmp(s, "de_", 3) || !strncmp(s, "cs_", 3) || !strncmp(s, "ar_", 3)) return true;
    return strchr(s, '/') != nullptr; // Workshop: "workshop/<id>/<name>"
}

static const char* ReadMapNameCached() {
    static char     s_buf[32] = {};
    static uint32_t s_lastMs  = 0;
    const uint32_t now = (uint32_t)GetTickCount();
    if (now - s_lastMs < 1000) return s_buf;
    s_lastMs = now;
    s_buf[0] = 0;
    if (!g_mem.IsValid() || !g_client || !offsets::dwGlobalVars) return s_buf;

    const uintptr_t gv = g_mem.Read<uintptr_t>(g_client + offsets::dwGlobalVars);
    if (!gv) return s_buf;

    const uintptr_t kCandidates[2] = { 0x1B8, 0x188 };
    for (uintptr_t off : kCandidates) {
        const uintptr_t p = g_mem.Read<uintptr_t>(gv + off);
        if (!p || p < 0x10000ull || p > 0x7FFFFFFFFFFFull) continue;
        char tmp[32] = {};
        if (!g_mem.ReadBuffer(p, tmp, sizeof(tmp) - 1)) continue;
        if (IsPlausibleMapName(tmp)) { memcpy(s_buf, tmp, sizeof(s_buf)); break; }
    }
    return s_buf;
}

// ─────────────────────────────────────────────────────────────────────────────
// Batch-Namensfenster: liest für Slots [ws,we) Entities + DesignerNames in
// 5 Batch-Runden (ListEntries → Entities → Identity → NamePtr → Namen).
// Rückgabe: Anzahl gültiger Zeilen (Entity + Identity + NamePtr != 0).
// ─────────────────────────────────────────────────────────────────────────────
struct EntNameRow {
    int       slot    = 0;
    uintptr_t entity  = 0;
    uintptr_t identity = 0;
    uintptr_t namePtr = 0;
    char      name[32]{};
};

static int ReadEntityNamesWindow(uintptr_t es, int ws, int we, EntNameRow* rows, int maxRows) {
    if (!es || !rows || maxRows <= 0) return 0;
    if (ws < 1) ws = 1;
    if (we > kEntityListEnd) we = kEntityListEnd;
    if (ws >= we) return 0;
    if (we - ws > 512) we = ws + 512; // Fenster sind nie größer als 512 Slots

    // Runde 1: ListEntry je Chunk.
    uintptr_t chVals[4] = {};
    {
        int c0 = ws >> 9, c1 = (we - 1) >> 9;
        ReadReq reqs[4]; uint8_t ok[4] = {};
        int n = 0;
        for (int c = c0; c <= c1 && n < 4; ++c) {
            reqs[n].addr = es + 0x10 + 8 * (uintptr_t)(uint32_t)c;
            reqs[n].buf  = &chVals[c - c0];
            reqs[n].size = sizeof(uintptr_t);
            ++n;
        }
        if (n) g_mem.ReadMany(reqs, n, ok);
    }

    // Runde 2: Entity-Pointer je Slot; Null-Slots fallen raus (kompaktieren).
    int rowCount = 0;
    {
        uintptr_t ents[512] = {};
        uint8_t ok[512] = {};
        int slotOf[512];
        ReadReq reqs[512];
        int n = 0;
        for (int i = ws; i < we; ++i) {
            int c = (i >> 9) - (ws >> 9);
            uintptr_t le = chVals[c];
            if (!le) continue;
            slotOf[n] = i;
            reqs[n].addr = le + kEntityListStride * (uintptr_t)(uint32_t)(i & 0x1FF);
            reqs[n].buf  = &ents[n];
            reqs[n].size = sizeof(uintptr_t);
            ++n;
        }
        if (n) g_mem.ReadMany(reqs, n, ok);
        for (int k = 0; k < n && rowCount < maxRows; ++k) {
            if (!ok[k] || !ents[k]) continue;
            rows[rowCount].slot = slotOf[k];
            rows[rowCount].entity = ents[k];
            ++rowCount;
        }
    }

    // Runde 3: Identity-Pointer (entity + 0x10).
    {
        uintptr_t ids[512] = {};
        uint8_t ok[512] = {};
        ReadReq reqs[512];
        for (int k = 0; k < rowCount; ++k) {
            reqs[k].addr = rows[k].entity + 0x10;
            reqs[k].buf  = &ids[k];
            reqs[k].size = sizeof(uintptr_t);
        }
        if (rowCount) g_mem.ReadMany(reqs, rowCount, ok);
        int w = 0;
        for (int k = 0; k < rowCount; ++k) {
            if (!ok[k] || !ids[k]) continue;
            rows[w] = rows[k];
            rows[w].identity = ids[k];
            ++w;
        }
        rowCount = w;
    }

    // Runde 4: designerNamePtr (identity + 0x20).
    {
        uintptr_t np[512] = {};
        uint8_t ok[512] = {};
        ReadReq reqs[512];
        for (int k = 0; k < rowCount; ++k) {
            reqs[k].addr = rows[k].identity + 0x20;
            reqs[k].buf  = &np[k];
            reqs[k].size = sizeof(uintptr_t);
        }
        if (rowCount) g_mem.ReadMany(reqs, rowCount, ok);
        int w = 0;
        for (int k = 0; k < rowCount; ++k) {
            if (!ok[k] || !np[k]) continue;
            rows[w] = rows[k];
            rows[w].namePtr = np[k];
            ++w;
        }
        rowCount = w;
    }

    // Runde 5: Namen (31 B wie bisher; Fehler lassen den Puffer genullt).
    {
        ReadReq reqs[512]; uint8_t ok[512] = {};
        for (int k = 0; k < rowCount; ++k) {
            reqs[k].addr = rows[k].namePtr;
            reqs[k].buf  = rows[k].name;
            reqs[k].size = 31;
        }
        if (rowCount) g_mem.ReadMany(reqs, rowCount, ok);
    }
    return rowCount;
}

static uintptr_t FindPlantedC4Window(uintptr_t es, int winStart, int winEnd, bool lastWindow) {
    if (!es) return 0;
    if (winStart < kEntityListStart) winStart = kEntityListStart;
    if (winEnd > kEntityListEnd) winEnd = kEntityListEnd;
    if (winStart >= winEnd) return 0;

    static DWORD lastDebug = 0;
    DWORD now = GetTickCount();
    bool doDebug = g_cfg.debugLog && (now - lastDebug > 10000);

    EntNameRow rows[512]{};
    int nRows = ReadEntityNamesWindow(es, winStart, winEnd, rows, 512);
    for (int k = 0; k < nRows; ++k) {
        if (strcmp(rows[k].name, "planted_c4") == 0) {
            if (doDebug) {
                bool ticking = g_mem.Read<uint8_t>(rows[k].entity + schemas::m_bBombTicking) != 0;
                printf("[bomb-entity] FOUND by name at index %d (entity=0x%llX) ticking=%d\n",
                       rows[k].slot, (unsigned long long)rows[k].entity, ticking);
                lastDebug = now;
            }
            return rows[k].entity;
        }
        if (doDebug && rows[k].slot < 70 && rows[k].name[0])
            printf("[bomb-entity] idx=%d name=%s\n", rows[k].slot, rows[k].name);
    }

    if (doDebug && lastWindow) {
        printf("[bomb-entity] No planted C4 found in entity list\n");
        lastDebug = now;
    }
    return 0;
}

struct BombOffsetsCache {
    uintptr_t blowTime      = 0;
    uintptr_t timerLength   = 0;
    uintptr_t beingDefused  = 0;
    uintptr_t bombSite      = 0;
    uintptr_t defuseCountDown = 0;
    uintptr_t entityCheck   = 0;
    bool      valid         = false;
    DWORD     lastScan      = 0;
};

static BombOffsetsCache g_bombOffs;

static bool ReadBombFromEntity(uintptr_t entity, BombData& bomb, uintptr_t es) {
    static DWORD lastPrint = 0;
    DWORD now = GetTickCount();

    bool ticking = g_mem.Read<uint8_t>(entity + schemas::m_bBombTicking) != 0;
    if (!ticking) return false;

    // Schema-Offsets direkt (cs2-dumper)
    float rawBlow = g_mem.Read<float>(entity + schemas::m_flC4Blow);
    float rawTimer = g_mem.Read<float>(entity + schemas::m_flTimerLength);
    float curtime = ReadCurtime();

    bool schemaValid = (rawTimer >= 20.f && rawTimer <= 60.f) && (rawBlow > curtime);

    if (schemaValid) {
        bomb.active = true;
        bomb.blowTime = rawBlow;
        bomb.timerLength = rawTimer;
        bomb.beingDefused = g_mem.Read<uint8_t>(entity + schemas::m_bBeingDefused) != 0;
        bomb.bombSite = g_mem.Read<int32_t>(entity + schemas::m_nBombSite);
        bomb.defuseCountDown = g_mem.Read<float>(entity + schemas::m_flDefuseCountDown);
        bomb.defuseLength = g_mem.Read<float>(entity + schemas::m_flDefuseLength);
        bomb.defuserHasKit = false;
        bomb.defuserKnown = false;

        if (bomb.beingDefused) {
            int32_t defHandle = g_mem.Read<int32_t>(entity + schemas::m_hBombDefuser);
            uintptr_t defPawn = ResolveEntityHandle(es, defHandle);
            if (defPawn) {
                uintptr_t itemSv = g_mem.Read<uintptr_t>(defPawn + schemas::m_pItemServices);
                if (itemSv) {
                    bomb.defuserHasKit = g_mem.Read<uint8_t>(itemSv + schemas::m_bHasDefuser) != 0;
                    bomb.defuserKnown = true;
                }
            }
            // Fallback: Defuse-Dauer verrät Kit (~5s) vs kein Kit (~10s)
            if (!bomb.defuserKnown && bomb.defuseLength > 0.5f) {
                bomb.defuserHasKit = bomb.defuseLength < 7.5f;
                bomb.defuserKnown = true;
            }
        }

        if (g_cfg.debugLog && now - lastPrint > 5000) {
            printf("[bomb] Schema direct: blow=%.2f timer=%.2f curtime=%.2f site=%d defusing=%d\n",
                   bomb.blowTime, bomb.timerLength, curtime, bomb.bombSite, bomb.beingDefused);
            lastPrint = now;
        }
        return true;
    }

    // Schema ungültig → Scanner-Fallback
    if (g_cfg.debugLog && now - lastPrint > 2000) {
        printf("[bomb] Schema data invalid blow=%.2f timer=%.2f curtime=%.2f — running scanner\n",
               rawBlow, rawTimer, curtime);
        lastPrint = now;
    }

    if (g_bombOffs.entityCheck != entity || (g_bombOffs.valid && now - g_bombOffs.lastScan > 5000)) {
        g_bombOffs = BombOffsetsCache{};
        g_bombOffs.entityCheck = entity;
    }

    if (!g_bombOffs.valid) {
        if (now - g_bombOffs.lastScan < 200) return false; // Scanner-Cooldown
        g_bombOffs.lastScan = now;

        if (curtime > 10.f) {
            struct Cand { uintptr_t off; float val; };
            Cand timerCands[32], blowCands[32];
            int nTimer = 0, nBlow = 0;

            // Batch: 679 Floats (0x1164..0x1C00, Schritt 4) in einem Rutsch.
            // Fehlgeschlagene Reads werden übersprungen (val bliebe 0 und
            // fiele durch die Filter — identisch zur Einzellesung).
            enum { kScanFirst = 0x1164, kScanCount = (0x1C00 - 0x1164) / 4 };
            float   scanVals[kScanCount];
            uint8_t scanOk[kScanCount];
            ReadReq reqs[kScanCount];
            for (int i = 0; i < kScanCount; i++) {
                reqs[i].addr = entity + kScanFirst + (uintptr_t)i * 4;
                reqs[i].buf  = &scanVals[i];
                reqs[i].size = sizeof(float);
            }
            g_mem.ReadMany(reqs, kScanCount, scanOk);

            for (int i = 0; i < kScanCount; i++) {
                if (!scanOk[i]) continue;
                uintptr_t off = kScanFirst + (uintptr_t)i * 4;
                float val = scanVals[i];
                if (val >= 30.f && val <= 50.f && nTimer < 32)
                    timerCands[nTimer++] = { off, val };
                if (val > curtime + 5.f && val < curtime + 100.f && nBlow < 32)
                    blowCands[nBlow++] = { off, val };
            }

            for (int ti = 0; ti < nTimer && !g_bombOffs.valid; ti++) {
                for (int bi = 0; bi < nBlow; bi++) {
                    float diff = blowCands[bi].val - timerCands[ti].val;
                    if (fabsf(diff - curtime) < 5.0f) {
                        uintptr_t foundTimerLength = timerCands[ti].off;
                        g_bombOffs.timerLength = foundTimerLength;
                        g_bombOffs.blowTime = blowCands[bi].off;
                        g_bombOffs.beingDefused = foundTimerLength + (0x11DC - 0x11D8);
                        g_bombOffs.defuseCountDown = foundTimerLength + (0x11F0 - 0x11D8);
                        g_bombOffs.bombSite = foundTimerLength + (0x11A4 - 0x11D8);
                        g_bombOffs.valid = true;
                    }
                }
            }
        }
    }

    if (g_bombOffs.valid) {
        bomb.active = true;
        bomb.blowTime = g_mem.Read<float>(entity + g_bombOffs.blowTime);
        bomb.timerLength = g_mem.Read<float>(entity + g_bombOffs.timerLength);
        bomb.beingDefused = g_mem.Read<uint8_t>(entity + g_bombOffs.beingDefused) != 0;
        bomb.bombSite = g_mem.Read<int32_t>(entity + g_bombOffs.bombSite);
        bomb.defuseCountDown = g_mem.Read<float>(entity + g_bombOffs.defuseCountDown);
        if (bomb.timerLength < 20.f || bomb.timerLength > 60.f) bomb.timerLength = 40.f;
        if (bomb.blowTime < curtime) bomb.blowTime = curtime + bomb.timerLength;
        return true;
    }
    return false;
}

static auto g_bombFirstSeen = std::chrono::steady_clock::time_point{};
static bool g_bombWasActive = false;
static DWORD g_bombWallMs   = 0;
static uintptr_t g_bombEntForChams = 0; // gepflanzte C4 → Chams (Item-Glow)

static void ReadBombData(BombData& bomb, uintptr_t localPawn, uintptr_t es) {
    bomb.active = false;
    bomb.defuserHasKit = false;
    bomb.defuserKnown = false;
    bomb.defuseLength = 0.f;
    bomb.beingDefused = false;

    // Mehrere Offsets fuer dwPlantedC4 probieren
    uintptr_t plantedC4 = 0;
    uintptr_t offsetsToTry[] = {
        offsets::dwPlantedC4,
        offsets::dwPlantedC4 - 0x10,
        offsets::dwPlantedC4 - 0x1010
    };

    for (uintptr_t off : offsetsToTry) {
        uintptr_t ptr = g_mem.Read<uintptr_t>(g_client + off);
        if (ptr && ptr > 0x100000 && ptr < 0x7FFFFFFFFFFF) {
            bool ticking = g_mem.Read<bool>(ptr + schemas::m_bBombTicking);
            float blowTime = g_mem.Read<float>(ptr + schemas::m_flC4Blow);
            float timerLength = g_mem.Read<float>(ptr + schemas::m_flTimerLength);
            if (ticking || blowTime > 100.f || (timerLength >= 30.f && timerLength <= 50.f)) {
                plantedC4 = ptr;
                break;
            }
        }
    }

    // Fallback: Entity-Liste scannen. Backoff: lange keine Bombe gesehen ->
    // 2s-Takt statt 500ms (voller Sweep als Dauerlast ist ein Stall).
    static DWORD lastScan = 0;
    static uintptr_t cachedPlantedC4 = 0;
    static DWORD lastBombSeen = 0;
    DWORD now = GetTickCount();

    if (!plantedC4) {
        if (cachedPlantedC4) {
            if (g_mem.Read<bool>(cachedPlantedC4 + schemas::m_bBombTicking))
                plantedC4 = cachedPlantedC4;
            else
                cachedPlantedC4 = 0;
        }
        // Fenster-Sweep (512 Slots/Tick) statt Block: kein Stall, Suche
        // läuft über wenige Ticks, danach Pause bis Intervall.
        static int bombCursor = kEntityListStart;
        static bool bombSweeping = false;
        DWORD interval = (now - lastBombSeen < 45000) ? 500 : 2000;
        if (!plantedC4) {
            if (!bombSweeping && (now - lastScan > interval)) {
                bombSweeping = true;
                bombCursor = kEntityListStart;
            }
            if (bombSweeping) {
                int ws = bombCursor, we = ws + kWinBomb;
                if (we > kEntityListEnd) we = kEntityListEnd;
                uintptr_t foundC4 = FindPlantedC4Window(es, ws, we, we >= kEntityListEnd);
                if (foundC4) {
                    cachedPlantedC4 = foundC4;
                    plantedC4 = foundC4;
                    bombSweeping = false;
                    lastScan = now;
                } else if (we >= kEntityListEnd) {
                    bombSweeping = false;
                    lastScan = now;
                } else {
                    bombCursor = we;
                }
            }
        } else {
            bombSweeping = false;
        }
    } else {
        cachedPlantedC4 = plantedC4;
    }

    if (plantedC4)
        ReadBombFromEntity(plantedC4, bomb, es);

    if (bomb.active) lastBombSeen = now;

    if (bomb.active) {
        float ct = ReadCurtime();
        // curtime validieren (Spielzeit: Sekunden seit Map-Start, 1..3h)
        bomb.curTime = (ct >= 1.f && ct < 10800.f) ? ct : 0.f;
        auto nowTime = std::chrono::steady_clock::now();
        if (!g_bombWasActive) {
            g_bombFirstSeen = nowTime;
            g_bombWallMs    = GetTickCount();
            g_bombWasActive = true;
        }
        bomb.wallStartMs = g_bombWallMs;
        bomb.wallTimer   = bomb.timerLength;
    } else {
        g_bombWasActive = false;
    }

    if (bomb.active && localPawn) {
        uintptr_t localCtrl = g_mem.Read<uintptr_t>(g_client + offsets::dwLocalPlayerController);
        bomb.localHasDefuser = localCtrl ? g_mem.Read<bool>(localCtrl + schemas::m_bPawnHasDefuser) : false;
        bomb.localInBombZone = g_mem.Read<bool>(localPawn + schemas::m_bInBombZone);
    }

    // Gepflanzte C4 ans Chams melden (Item-Glow mit Bomben-Farbe). Der
    // Namen-Sweep verpasst die Entity je Map/Version — dwPlantedC4 nicht.
    // (!ticking → 0 → Cleanup-Pass dreht den Glow wieder ab.)
    g_bombEntForChams = (bomb.active && plantedC4) ? plantedC4 : 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Boden-Items (Waffen, Bombe, Util auf dem Boden)
// FENSTER-Version: scannt [winStart, winEnd) und taggt Slot für Merge.
// Ein Fenster/Tick ≈ voller Sweep je ~100ms @144Hz — keine Block-Stalls.
// ─────────────────────────────────────────────────────────────────────────────
static int ReadGroundItemsWindow(GroundItem* out, int maxOut, const Matrix4x4& vm,
                                 int scrW, int scrH, uintptr_t es, int winStart, int winEnd,
                                 uintptr_t localPawn) {
    if (!es || !out || maxOut <= 0) return 0;
    if (winStart < kEntityListStart) winStart = kEntityListStart;
    if (winEnd > kEntityListEnd) winEnd = kEntityListEnd;
    if (winStart >= winEnd) return 0;

    // Batch-Namensfenster (5 Runden) statt Read pro Slot.
    EntNameRow rows[512]{};
    int nRows = ReadEntityNamesWindow(es, winStart, winEnd, rows, 512);

    // Klassifikation wie bisher — nur sammeln, Reads danach gebündelt.
    int   candK[512];
    int   candType[512];
    const char* candDisp[512];
    int   nCand = 0;
    for (int k = 0; k < nRows && nCand < 512; ++k) {
        const char* nameBuf = rows[k].name;
        int type = -1;
        const char* displayName = nameBuf;

        if (strncmp(nameBuf, "weapon_", 7) == 0) {
            if (strcmp(nameBuf, "weapon_c4") == 0) {
                type = 1; displayName = "C4";
            } else if (strcmp(nameBuf, "weapon_knife") == 0 || strcmp(nameBuf, "weapon_knife_t") == 0) {
                type = 0; displayName = "Knife";
            } else {
                type = 0; displayName = nameBuf + 7;
            }
        } else if (strcmp(nameBuf, "planted_c4") == 0) {
            type = 1; displayName = "Planted C4";
        } else if (strcmp(nameBuf, "smokegrenade_projectile") == 0) {
            type = 2; displayName = "Smoke";
        } else if (strcmp(nameBuf, "molotov_projectile") == 0) {
            type = 2; displayName = "Molotov";
        } else if (strcmp(nameBuf, "incgrenade_projectile") == 0) {
            type = 2; displayName = "Incendiary";
        } else if (strcmp(nameBuf, "hegrenade_projectile") == 0) {
            type = 2; displayName = "HE";
        } else if (strcmp(nameBuf, "flashbang_projectile") == 0) {
            type = 2; displayName = "Flash";
        } else if (strcmp(nameBuf, "decoy_projectile") == 0) {
            type = 2; displayName = "Decoy";
        }

        if (type < 0) continue;
        candK[nCand]    = k;
        candType[nCand] = type;
        candDisp[nCand] = displayName;
        ++nCand;
    }
    if (!nCand) return 0;

    // Batch 1: GameSceneNode; Batch 2: AbsOrigin.
    uintptr_t nodes[512] = {};
    uint8_t   okNode[512] = {};
    {
        ReadReq reqs[512];
        for (int c = 0; c < nCand; ++c) {
            reqs[c].addr = rows[candK[c]].entity + schemas::m_pGameSceneNode;
            reqs[c].buf  = &nodes[c];
            reqs[c].size = sizeof(uintptr_t);
        }
        g_mem.ReadMany(reqs, nCand, okNode);
    }
    Vector3 origins[512] = {};
    {
        ReadReq reqs[512];
        int n = 0;
        for (int c = 0; c < nCand; ++c) {
            if (!okNode[c] || !nodes[c]) continue;
            reqs[n].addr = nodes[c] + schemas::m_vecAbsOrigin;
            reqs[n].buf  = &origins[c];
            reqs[n].size = sizeof(Vector3);
            ++n;
        }
        if (n) g_mem.ReadMany(reqs, n);
    }

    // ── Projektil-Details (nur util-Projektile): exakte Ballistik + Zünder ──
    // m_vInitialPosition/Velocity + m_flSpawnTime/DetonateTime + Thrower —
    // damit rechnet der Render-Thread die echte Flugbahn (kein Winkel-Rätsel).
    struct ProjInfo {
        Vector3 p0{}, v0{}, vel{};
        float   spawnT = 0.f, detT = 0.f;
        int32_t thrower = 0;
        int32_t bounces = 0;
        uint8_t okP0 = 0, okV0 = 0, okVel = 0, okSpawn = 0, okDet = 0,
                okThrower = 0, okBounce = 0;
        bool    owned = false;
    };
    static ProjInfo pinfo[512]; // static: 512*~80B — kein Stack-Risiko
    int  projIdx[512];
    int  nProj = 0;
    for (int c = 0; c < nCand; ++c)
        if (candType[c] == 2) projIdx[nProj++] = c;
    float curForProj = 0.f;
    if (nProj) {
        for (int i = 0; i < nProj; ++i) pinfo[projIdx[i]] = ProjInfo{};
        curForProj = ReadCurtime();
        uint8_t okB[512];
        // Liest ein Feld für alle Projektile gebündelt (1 ReadMany).
        auto readField = [&](size_t fieldOff, uintptr_t schemaOff, size_t size) -> bool {
            if (!schemaOff) { memset(okB, 0, sizeof(okB)); return false; }
            ReadReq reqs[512];
            for (int i = 0; i < nProj; ++i) {
                int c = projIdx[i];
                reqs[i].addr = rows[candK[c]].entity + schemaOff;
                reqs[i].buf  = (uint8_t*)&pinfo[0] + sizeof(ProjInfo) * (size_t)c + fieldOff;
                reqs[i].size = size;
            }
            memset(okB, 0, sizeof(okB));
            g_mem.ReadMany(reqs, nProj, okB);
            return true;
        };
        if (readField(offsetof(ProjInfo, p0), schemas::m_vInitialPosition, sizeof(Vector3)))
            for (int i = 0; i < nProj; ++i) pinfo[projIdx[i]].okP0 = okB[i];
        if (readField(offsetof(ProjInfo, v0), schemas::m_vInitialVelocity, sizeof(Vector3)))
            for (int i = 0; i < nProj; ++i) pinfo[projIdx[i]].okV0 = okB[i];
        if (readField(offsetof(ProjInfo, vel), schemas::m_vecAbsVelocity, sizeof(Vector3)))
            for (int i = 0; i < nProj; ++i) pinfo[projIdx[i]].okVel = okB[i];
        if (readField(offsetof(ProjInfo, spawnT), schemas::m_flSpawnTime, sizeof(float)))
            for (int i = 0; i < nProj; ++i) pinfo[projIdx[i]].okSpawn = okB[i];
        if (readField(offsetof(ProjInfo, detT), schemas::m_flDetonateTime, sizeof(float)))
            for (int i = 0; i < nProj; ++i) pinfo[projIdx[i]].okDet = okB[i];
        if (readField(offsetof(ProjInfo, thrower), schemas::m_hThrower, sizeof(int32_t)))
            for (int i = 0; i < nProj; ++i) pinfo[projIdx[i]].okThrower = okB[i];
        if (readField(offsetof(ProjInfo, bounces), schemas::m_nBounces, sizeof(int32_t)))
            for (int i = 0; i < nProj; ++i) pinfo[projIdx[i]].okBounce = okB[i];

        // Eigentümer: m_hThrower → Entity → Vergleich mit lokalem Pawn.
        for (int i = 0; i < nProj; ++i) {
            ProjInfo& pi = pinfo[projIdx[i]];
            if (pi.okThrower && pi.thrower > 0 && localPawn) {
                uintptr_t throwerEnt = ResolveEntityHandle(es, pi.thrower);
                pi.owned = (throwerEnt != 0 && throwerEnt == localPawn);
            }
        }
    }

    // Ausgabe (W2S) — Reihenfolge = Slot-Reihenfolge wie zuvor.
    DWORD nowStamp = GetTickCount();
    int count = 0;
    for (int c = 0; c < nCand && count < maxOut; ++c) {
        if (!okNode[c] || !nodes[c]) continue;
        Vector2 screen;
        if (!W2S(origins[c], screen, vm, scrW, scrH)) continue;

        out[count].origin = origins[c];
        out[count].screen = screen;
        out[count].onScreen = true;
        out[count].type = candType[c];
        out[count].itemId = 0;
        snprintf(out[count].name, sizeof(out[count].name), "%s", candDisp[c]);
        out[count].slot = rows[candK[c]].slot;
        out[count].entAddr = rows[candK[c]].entity; // stabiler Key für Render-Animationen

        if (candType[c] == 2) {
            const ProjInfo& pi = pinfo[c];
            out[count].isProj    = true;
            out[count].stampMs   = nowStamp;
            out[count].ownedByLocal = pi.owned;
            out[count].bounces   = (pi.okBounce && pi.bounces >= 0 && pi.bounces < 100) ? pi.bounces : 0;
            float cur = curForProj;
            if (pi.okV0 && isfinite(pi.v0.x) && isfinite(pi.v0.y) && isfinite(pi.v0.z)) {
                float v2 = pi.v0.x*pi.v0.x + pi.v0.y*pi.v0.y + pi.v0.z*pi.v0.z;
                if (v2 > 100.f*100.f && v2 < 2500.f*2500.f && pi.okP0 &&
                    (pi.p0.x != 0.f || pi.p0.y != 0.f || pi.p0.z != 0.f)) {
                    out[count].p0 = pi.p0;
                    out[count].v0 = pi.v0;
                }
            }
            if (pi.okVel && isfinite(pi.vel.x) && isfinite(pi.vel.y) && isfinite(pi.vel.z)) {
                float v2 = pi.vel.x*pi.vel.x + pi.vel.y*pi.vel.y + pi.vel.z*pi.vel.z;
                if (v2 < 3500.f*3500.f) {
                    out[count].vel = pi.vel;
                    out[count].velOk = true;
                }
            }
            if (cur > 1.f && pi.okSpawn && isfinite(pi.spawnT)) {
                float age = cur - pi.spawnT;
                if (age >= 0.f && age < 30.f) out[count].ageAtRead = age;
            }
            if (cur > 1.f && pi.okDet && isfinite(pi.detT)) {
                float dIn = pi.detT - cur;
                if (dIn > -2.f && dIn < 30.f) out[count].detonateIn = dIn;
            }
            count++;
        } else {
            count++;
        }
    }
    return count;
}

// ─────────────────────────────────────────────────────────────────────────────
// Util-Zonen (Smoke-Volumen + Inferno-Flammen) — UC-Standard (c_inferno etc.)
// Smoke: smokegrenade_projectile lebt nach Detonation weiter (m_bDidSmokeEffect)
//   und trägt die Detonationsposition — der Kreis zeigt die Sperrfläche.
// Feuer: inferno-Entity mit echten Flammenpositionen (m_firePositions[64]).
// Entities despawnen von selbst (Smoke ~18s, Feuer ~7s) — keine Timer nötig.
// ─────────────────────────────────────────────────────────────────────────────
static int ReadUtilZonesWindow(UtilZone* out, int maxOut, uintptr_t es, int winStart, int winEnd) {
    if (!es || !out || maxOut <= 0) return 0;
    if (winStart < 1) winStart = 1;
    if (winEnd > kEntityListEnd) winEnd = kEntityListEnd;
    if (winStart >= winEnd) return 0;

    // Batch-Namensfenster (5 Runden) statt Read pro Slot. Smoke-/Inferno-
    // Folge-Reads bleiben einzeln (Flammen-Batching bräuchte 512×768-B).
    EntNameRow rows[512]{};
    int nRows = ReadEntityNamesWindow(es, winStart, winEnd, rows, 512);

    int count = 0;
    for (int k = 0; k < nRows && count < maxOut; ++k) {
        const char* nameBuf = rows[k].name;
        uintptr_t entity = rows[k].entity;

        // Discovery: Feuer-/Rauch-Namen einmalig melden (case-insensitiv,
        // falls Valve umbenennt oder anders schreibt)
        {
            char lower[32]{};
            for (int c = 0; c < 31 && nameBuf[c]; c++)
                lower[c] = (char)tolower((unsigned char)nameBuf[c]);
            bool interesting = strstr(lower, "inferno") || strstr(lower, "fire") ||
                               strstr(lower, "burn") || strstr(lower, "flame") ||
                               strstr(lower, "smoke") || strstr(lower, "molo");
            if (interesting) {
                static char logged[16][32]{};
                static int nLogged = 0;
                bool known = false;
                for (int kk = 0; kk < nLogged; kk++)
                    if (!strcmp(logged[kk], nameBuf)) { known = true; break; }
                if (!known && nLogged < 16) {
                    snprintf(logged[nLogged++], sizeof(logged[0]), "%s", nameBuf);
                    if (g_cfg.debugLog)
                        printf("[util] Entity-Kandidat: %s\n", nameBuf);
                }
            }
        }

        if (strcmp(nameBuf, "smokegrenade_projectile") == 0) {
            if (!schemas::m_bDidSmokeEffect || !schemas::m_vSmokeDetonationPos) continue;
            if (!g_mem.Read<uint8_t>(entity + schemas::m_bDidSmokeEffect)) continue; // noch im Flug
            Vector3 det = g_mem.Read<Vector3>(entity + schemas::m_vSmokeDetonationPos);
            if (det.x == 0.f && det.y == 0.f && det.z == 0.f) continue;
            out[count].origin = det;
            out[count].flameCount = 0;
            out[count].type = 0;
            out[count].slot = rows[k].slot;
            count++;
        } else if (strstr(nameBuf, "inferno") != nullptr) {
            if (!schemas::m_fireCount || !schemas::m_firePositions) continue;
            int32_t fc = g_mem.Read<int32_t>(entity + schemas::m_fireCount);
            if (fc <= 0 || fc > 64) continue;
            int n = (fc > MAX_UTIL_FLAMES) ? MAX_UTIL_FLAMES : fc;
            Vector3 flames[64]{};
            if (!g_mem.ReadBuffer(entity + schemas::m_firePositions, flames, (size_t)fc * sizeof(Vector3)))
                continue;
            // Mitte aus Flammen mitteln (robuster als Entity-Origin)
            Vector3 mid{};
            int valid = 0;
            for (int f = 0; f < n; f++) {
                if (flames[f].x == 0.f && flames[f].y == 0.f && flames[f].z == 0.f) continue;
                out[count].flames[valid] = flames[f];
                mid.x += flames[f].x; mid.y += flames[f].y; mid.z += flames[f].z;
                valid++;
            }
            if (valid == 0) continue;
            out[count].origin = { mid.x / valid, mid.y / valid, mid.z / valid };
            out[count].flameCount = valid;
            out[count].type = 1;
            out[count].slot = rows[k].slot;
            count++;
        }
    }
    return count;
}

// ─────────────────────────────────────────────────────────────────────────────
// Chicken- / Ball-Ziele (fuer Chicken-/Ball-Aimbot)
// DesignerName-Scan wie bei Boden-Items: "chicken" exakt, Ball ueber
// ball-artige Namen (soccer/basket/volley/football/ball). Tote Huehner
// despawnen aus der Entity-Liste, darum kein Health-Filter noetig.
// ─────────────────────────────────────────────────────────────────────────────
static bool NameContainsNoCase(const char* hay, const char* needle) {
    if (!hay || !needle || !*needle) return false;
    for (const char* h = hay; *h; ++h) {
        const char* hh = h;
        const char* nn = needle;
        while (*hh && *nn) {
            char a = *hh, b = *nn;
            if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
            if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
            if (a != b) break;
            ++hh; ++nn;
        }
        if (!*nn) return true;
    }
    return false;
}

static bool IsBallNameMatch(const char* name) {
    // Explizite Ball-/Glocken-Namen: immer ein Ziel (z.B. Community-Maps,
    // Inferno-Glocken falls so benannt).
    if (NameContainsNoCase(name, "ball")
        || NameContainsNoCase(name, "soccer")
        || NameContainsNoCase(name, "basket")
        || NameContainsNoCase(name, "volley")
        || NameContainsNoCase(name, "football"))
        return true;
    if (NameContainsNoCase(name, "bell")
        || NameContainsNoCase(name, "glocke"))
        return true;
    return false;
}

static bool IsPropClassName(const char* name) {
    // Generische Prop-Klassen (Baelle sind meist Physik-Props, heissen aber
    // nicht so — z.B. Dust2-Football). Groesse/Bewegung entscheiden weiter
    // unten, sonst wuerde jede Deko zum Ziel (33 prop_dynamic auf Aim-Maps).
    if (strcmp(name, "prop_physics") == 0
        || strcmp(name, "prop_physics_multiplayer") == 0
        || strcmp(name, "prop_physics_override") == 0
        || strcmp(name, "prop_dynamic") == 0
        || strcmp(name, "prop_dynamic_override") == 0
        || strcmp(name, "func_physbox") == 0)
        return true;
    return false;
}

// Bewegungs-Gedaechtnis pro Entity-Index: Deko steht still, der Ball rollt.
struct PropTrack { Vector3 prev{}; bool init = false; DWORD lastMove = 0; };
static PropTrack s_propTrack[2048];

// Prop-Kandidat pruefen: Bewegung (inkl. "kuerzlich bewegt") oder
// Ball-Groesse per Collision-Box. outMoved = Treffer durch Bewegung.
static bool PropPassesBallFilter(int index, uintptr_t entity, const Vector3& origin,
                                 float maxSize, bool* outMoved) {
    if (outMoved) *outMoved = false;
    DWORD now = GetTickCount();
    PropTrack& tr = s_propTrack[index & 2047];

    bool moved = false;
    if (tr.init) {
        float dx = origin.x - tr.prev.x;
        float dy = origin.y - tr.prev.y;
        float dz = origin.z - tr.prev.z;
        float d = sqrtf(dx*dx + dy*dy + dz*dz);
        if (d > 2000.f) { tr.init = false; tr.lastMove = 0; } // Slot wiederverwendet
        else if (d > 3.f) { tr.lastMove = now; moved = true; }
    }
    tr.prev = origin;
    tr.init = true;

    bool recentlyMoved = (tr.lastMove != 0 && now - tr.lastMove < 4000);
    if (moved || recentlyMoved) { if (outMoved) *outMoved = true; return true; }

    // Groesse: ruhender Ball ist klein (Football ~10u), Deko meist gross.
    if (schemas::m_pCollision && schemas::m_vecMins && schemas::m_vecMaxs) {
        uintptr_t col = g_mem.Read<uintptr_t>(entity + schemas::m_pCollision);
        if (col) {
            Vector3 mins = g_mem.Read<Vector3>(col + schemas::m_vecMins);
            Vector3 maxs = g_mem.Read<Vector3>(col + schemas::m_vecMaxs);
            float sx = maxs.x - mins.x, sy = maxs.y - mins.y, sz = maxs.z - mins.z;
            if (sx > 0.5f && sy > 0.5f && sz > 0.5f && sx < 500.f && sy < 500.f && sz < 500.f) {
                float biggest = sx > sy ? (sx > sz ? sx : sz) : (sy > sz ? sy : sz);
                if (biggest <= maxSize) return true;
            }
        }
    }
    return false;
}

static void ReadChickenAndBallWindow(EntAimTarget* chickenOut, int* chickenCount,
                                     EntAimTarget* ballOut, int* ballCount, int maxOut,
                                     uintptr_t es, int winStart, int winEnd) {
    int nc = 0, nb = 0;
    const float maxSize = g_cfg.ballMaxSize > 0.f ? g_cfg.ballMaxSize : 32.f;
    if (chickenCount) *chickenCount = 0;
    if (ballCount) *ballCount = 0;
    if (!es || !chickenOut || !ballOut || maxOut <= 0) return;
    if (winStart < kEntityListStart) winStart = kEntityListStart;
    if (winEnd > kEntityListEnd) winEnd = kEntityListEnd;
    if (winStart >= winEnd) return;

    // Batch-Namensfenster (5 Runden) statt Read pro Slot.
    EntNameRow rows[512]{};
    int nRows = ReadEntityNamesWindow(es, winStart, winEnd, rows, 512);

    // Klassifikation wie bisher — Kandidaten sammeln, node+origin gebündelt.
    int  candK[512];
    bool candChick[512];
    bool candProp[512];
    int  nCand = 0;
    for (int k = 0; k < nRows && nCand < 512; ++k) {
        const char* nameBuf = rows[k].name;
        bool isChicken  = (strcmp(nameBuf, "chicken") == 0);
        bool isBallName = !isChicken && IsBallNameMatch(nameBuf);
        bool isProp     = !isChicken && !isBallName && IsPropClassName(nameBuf);
        if (!isChicken && !isBallName && !isProp) continue;
        candK[nCand]     = k;
        candChick[nCand] = isChicken;
        candProp[nCand]  = isProp;
        ++nCand;
    }
    if (!nCand) return;

    // Batch 1: GameSceneNode; Batch 2: AbsOrigin.
    uintptr_t nodes[512] = {};
    uint8_t   okNode[512] = {};
    {
        ReadReq reqs[512];
        for (int c = 0; c < nCand; ++c) {
            reqs[c].addr = rows[candK[c]].entity + schemas::m_pGameSceneNode;
            reqs[c].buf  = &nodes[c];
            reqs[c].size = sizeof(uintptr_t);
        }
        g_mem.ReadMany(reqs, nCand, okNode);
    }
    Vector3 origins[512] = {};
    {
        ReadReq reqs[512];
        int n = 0;
        for (int c = 0; c < nCand; ++c) {
            if (!okNode[c] || !nodes[c]) continue;
            reqs[n].addr = nodes[c] + schemas::m_vecAbsOrigin;
            reqs[n].buf  = &origins[c];
            reqs[n].size = sizeof(Vector3);
            ++n;
        }
        if (n) g_mem.ReadMany(reqs, n);
    }

    // Ausgabe in Slot-Reihenfolge mit identischen Filtern wie zuvor.
    for (int c = 0; c < nCand && (nc < maxOut || nb < maxOut); ++c) {
        if (!okNode[c] || !nodes[c]) continue;
        int k = candK[c];
        bool isChicken = candChick[c];
        if (isChicken && nc >= maxOut) continue;
        if (!isChicken && nb >= maxOut) continue;
        const Vector3& origin = origins[c];
        if (origin.x == 0.f && origin.y == 0.f && origin.z == 0.f) continue;

        if (isChicken) {
            chickenOut[nc].origin = origin; chickenOut[nc].alive = true;
            chickenOut[nc].slot = rows[k].slot; nc++;
        } else {
            bool moved = false;
            // Generische Props nur bei Bewegung oder Ball-Groesse (keine Deko)
            if (candProp[c] && !PropPassesBallFilter(rows[k].slot, rows[k].entity, origin, maxSize, &moved)) continue;
            ballOut[nb].origin = origin; ballOut[nb].alive = true;
            ballOut[nb].slot = rows[k].slot; nb++;
        }
    }

    if (chickenCount) *chickenCount = nc;
    if (ballCount) *ballCount = nb;
}

// ── Entity-Census: alle eindeutigen Designer-Namen auf der Map loggen ────────
// Nur aktiv wenn Ball Aim an ist (gedrosselt). Zeigt den exakten Namen des
// Balls, falls er nicht unter die bekannten Muster faellt.
static void LogEntityCensus(uintptr_t es) {
    static DWORD lastCensus = 0;
    DWORD now = GetTickCount();
    if (now - lastCensus < 10000) return;
    lastCensus = now;
    if (!es) return;

    char unique[64][32]{};
    int  uniqueCount = 0;

    // Ganze Liste in 512er-Fenstern über den Batch-Helfer.
    EntNameRow rows[512]{};
    for (int w = kEntityListStart; w < kEntityListEnd && uniqueCount < 64; w += 512) {
        int we = w + 512;
        if (we > kEntityListEnd) we = kEntityListEnd;
        int nRows = ReadEntityNamesWindow(es, w, we, rows, 512);
        for (int r = 0; r < nRows && uniqueCount < 64; ++r) {
            const char* nameBuf = rows[r].name;
            if (!nameBuf[0]) continue;

            bool known = false;
            for (int k = 0; k < uniqueCount; k++) {
                if (strcmp(unique[k], nameBuf) == 0) { known = true; break; }
            }
            if (!known) snprintf(unique[uniqueCount++], 32, "%s", nameBuf);
        }
    }

    printf("[census] %d Entity-Typen auf der Map:", uniqueCount);
    for (int k = 0; k < uniqueCount; k++) printf(" %s", unique[k]);
    printf("\n");
}

// ─────────────────────────────────────────────────────────────────────────────
// Lokale Waffe / Recoil-State / Sensitivitaet lesen
// ─────────────────────────────────────────────────────────────────────────────
static void ReadLocalWeaponAndRecoil(GameCache& local, uintptr_t lp) {
    local.currentWeaponId = 0;
    local.punchAngle = {};
    local.shotsFired = 0;
    local.holdingUtil = false;
    local.pinPulled = false;
    local.throwStrength = 1.f;
    local.utilWeaponId = 0;
    if (!lp) return;

    uintptr_t ws = g_mem.Read<uintptr_t>(lp + schemas::m_pWeaponServices);
    if (ws) {
        int32_t activeWeaponHandle = g_mem.Read<int32_t>(ws + 0x60);
        if (activeWeaponHandle > 0) {
            int weaponIndex = activeWeaponHandle & 0x7FFF;
            uintptr_t entitySystem = g_mem.Read<uintptr_t>(g_client + offsets::dwEntityList);
            if (entitySystem) {
                uintptr_t le = g_mem.Read<uintptr_t>(entitySystem + 0x10 + 8 * (weaponIndex >> 9));
                if (le) {
                    uintptr_t weapon = g_mem.Read<uintptr_t>(le + kEntityListStride * (weaponIndex & 0x1FF));
                    if (weapon) {
                        // EconEntity zuerst (siehe Spieler-Resolve)
                        int16_t wid = 0;
                        uintptr_t mgrs[2] = { (uintptr_t)0x1290, schemas::m_AttributeManager };
                        for (uintptr_t mgr : mgrs) {
                            if (!mgr) continue;
                            uintptr_t eiv = weapon + mgr + schemas::m_Item;
                            int16_t id = g_mem.Read<int16_t>(eiv + schemas::m_iItemDefinitionIndex);
                            if (id > 0 && id < 1000) { wid = id; break; }
                        }
                        local.currentWeaponId = wid;
                        if (weapons::IsGrenade(local.currentWeaponId)) {
                            local.holdingUtil = true;
                            local.utilWeaponId = local.currentWeaponId;
                            local.throwStrength = g_mem.Read<float>(weapon + schemas::m_flThrowStrength);
                            if (local.throwStrength < 0.f) local.throwStrength = 0.f;
                            if (local.throwStrength > 1.f) local.throwStrength = 1.f;
                            local.pinPulled = g_mem.Read<uint8_t>(weapon + schemas::m_bPinPulled) != 0;
                            // Für den Formel-Abgleich (Kalibrierung beim Projektil)
                            s_nadeLastStrength = local.throwStrength;
                            s_nadeLastVel      = local.localVelocity;
                        }
                    }
                }
            }
        }
    }

    // Recoil: AimPunchServices (predictable) + CameraServices (Fallback)
    uintptr_t aimPunch = g_mem.Read<uintptr_t>(lp + schemas::m_pAimPunchServices);
    uintptr_t camSv    = g_mem.Read<uintptr_t>(lp + schemas::m_pCameraServices);
    static DWORD lastPunchLog = 0;
    DWORD now = GetTickCount();

    if (aimPunch) {
        Vector3 v = g_mem.Read<Vector3>(aimPunch + 0x50);  // predictableBaseAngle
        if (g_cfg.debugLog && now - lastPunchLog > 3000) {
            printf("[punch] aimPunchSv=0x%llX camSv=0x%llX pred=(%.4f,%.4f,%.4f)\n",
                   aimPunch, camSv, v.x, v.y, v.z);
        }
        local.punchAngle = v;
    }
    if (camSv) {
        Vector3 v = g_mem.Read<Vector3>(camSv + 0x48);     // m_vecCsViewPunchAngle
        if (g_cfg.debugLog && now - lastPunchLog > 3000) {
            printf("[punch] camViewPunch=(%.4f,%.4f,%.4f)\n", v.x, v.y, v.z);
            lastPunchLog = now;
        }
        if (local.punchAngle.x == 0 && local.punchAngle.y == 0 && local.punchAngle.z == 0)
            local.punchAngle = v;
    } else if (g_cfg.debugLog && now - lastPunchLog > 3000) {
        printf("[punch] camSv IS NULL\n");
        lastPunchLog = now;
    }

    local.shotsFired = g_mem.Read<int32_t>(lp + schemas::m_iShotsFired);
}

// ── Grenade Throw Preview (UC-Source Celse2021 Jan 2025) ───────────────────────
// Calculates where a thrown grenade will land based on view angles, throw strength,
// player velocity and physics (gravity 800.0f, 64-tick, CoR 0.45f, friction 0.4f).
// Trigger: local player holds a grenade + mouse left button held.

static void AngleToDirection(const Vector3& ang, Vector3& dir) {
    float cy = cosf(ang.y * (float)(3.14159265358979 / 180.0));
    float sy = sinf(ang.y * (float)(3.14159265358979 / 180.0));
    float cp = cosf(ang.x * (float)(3.14159265358979 / 180.0));
    float sp = sinf(ang.x * (float)(3.14159265358979 / 180.0));
    dir.x = cp * cy;
    dir.y = cp * sy;
    dir.z = -sp;
}

static void CalculateThrowVelocity(const Vector3& dir, float throwStrength,
                                  const Vector3& playerVel, Vector3& vel) {
    float strengthScale = throwStrength * 0.7f + 0.3f;
    vel.x = dir.x * strengthScale * 1115.0f + playerVel.x * 1.25f;
    vel.y = dir.y * strengthScale * 1115.0f + playerVel.y * 1.25f;
    vel.z = dir.z * strengthScale * 1115.0f + playerVel.z * 1.25f;
}

static std::vector<Vector3> SimulateGrenadeTrajectory(const Vector3& start,
                                                      const Vector3& initVel,
                                                      float gravity /*= 800.0f*/,
                                                      float tickInterval /*= 1.f/64.f*/,
                                                      float CoR /*= 0.45f*/,
                                                      float friction /*= 0.4f*/,
                                                      int maxSteps /*= 200*/) {
    std::vector<Vector3> pts;
    pts.reserve(maxSteps);
    Vector3 pos = start;
    Vector3 vel = initVel;

    for (int i = 0; i < maxSteps; i++) {
        pts.push_back(pos);

        // Gravity
        vel.z -= gravity * tickInterval;

        // Integration
        pos.x += vel.x * tickInterval;
        pos.y += vel.y * tickInterval;
        pos.z += vel.z * tickInterval;

        // Simple ground collision (z <= 0)
        if (pos.z <= 0.0f) {
            pos.z = 0.0f;
            vel.z *= -CoR;   // bounce
            vel.x *= friction;
            vel.y *= friction;

            // Ruhen, wenn kaum Bewegung mehr da
            if (fabsf(vel.z) < 0.1f && fabsf(vel.x) < 0.1f && fabsf(vel.y) < 0.1f) {
                break;
            }
        }
    }
    return pts;
}

static bool HasGrenadeEquipped() {
    uintptr_t lp = g_mem.IsValid() && g_client ? GetLocalPawn() : 0;
    if (!lp) return false;
    uintptr_t ws = g_mem.Read<uintptr_t>(lp + schemas::m_pWeaponServices);
    if (!ws) return false;
    int32_t activeHandle = g_mem.Read<int32_t>(ws + 0x60);
    if (activeHandle <= 0) return false;
    int idx = activeHandle & 0x7FFF;
    uintptr_t entitySystem = g_mem.Read<uintptr_t>(g_client + offsets::dwEntityList);
    if (!entitySystem) return false;
    uintptr_t le = g_mem.Read<uintptr_t>(entitySystem + 0x10 + 8 * (idx >> 9));
    if (!le) return false;
    uintptr_t weapon = g_mem.Read<uintptr_t>(le + 0x70 * (idx & 0x1FF));
    if (!weapon) return false;
    int16_t id = g_mem.Read<int16_t>(weapon + schemas::m_iItemDefinitionIndex);
    return (id == 400 || id == 420 || id == 433 || id == 435);
}

// ─────────────────────────────────────────────────────────────────────────────

// ─────────────────────────────────────────────────────────────────────────────
// Haupt-Thread: konfigurierbare Hz (Default 360), liest ALLES, publiziert Snapshot
// ─────────────────────────────────────────────────────────────────────────────
void GameReaderThread() {
    // Standard-Priorität (kein Verhungern des Spiels — siehe WinMain).

    // Persistente Slow-Caches (Scans füllen sie nach eigenem Takt, Publish
    // übernimmt sie jeden Tick — kein Flackern zwischen Scans).
    static GroundCache s_ground;
    static EntAimCache s_chicken, s_ball;
    static UtilCache   s_util;
    static uint64_t    s_tick = 0;
    static int         s_freqCount = 0;
    static DWORD       s_freqWin = 0;

    while (!g_stopThread.load(std::memory_order_relaxed)) {
        auto frameStart = std::chrono::steady_clock::now();
        QRD_NOW(tTick0);

        // Hintergrund: Lite-Takt (20 Hz, nur Fast-Felder, keine Scans).
        // g_appFocused setzt der Render-Thread (Spiel ODER Overlay im Fokus).
        bool focused = g_appFocused.load(std::memory_order_relaxed);
        s_liteTick = !focused;
        bool medTick  = focused && ((s_tick % 2) == 0);
        bool slowTick = focused && ((s_tick % 15) == 0);
        s_tick++;

        // Freq-Messung (1s-Fenster)
        {
            DWORD nw = GetTickCount();
            s_freqCount++;
            if (nw - s_freqWin >= 1000) {
                g_readerStats.freqHz.store((int)((long long)s_freqCount * 1000 / (long long)(nw - s_freqWin + 1)),
                                           std::memory_order_relaxed);
                s_freqCount = 0;
                s_freqWin = nw;
            }
        }

        if (g_mem.IsValid() && g_client) {
            int sw = g_scrW.load(std::memory_order_relaxed);
            int sh = g_scrH.load(std::memory_order_relaxed);

            GameCache local{};
            AimCache  aimLocal{};

            local.vm = aimLocal.vm = g_mem.Read<Matrix4x4>(g_client + offsets::dwViewMatrix);
            // Stempel sofort nach erstem Speicherzugriff — misst exaktes Snapshot-Alter
            local.stampMs = aimLocal.stampMs = GetTickCount();
            // Entity-Liste EINMAL pro Tick — alle Fenster-Scans nutzen denselben es.
            uintptr_t es = g_mem.Read<uintptr_t>(g_client + offsets::dwEntityList);
            uintptr_t lp = GetLocalPawn(); // kein dwLocalPlayerPawn nötig
            local.localTeam = aimLocal.localTeam = lp ? g_mem.Read<uint8_t>(lp + schemas::m_iTeamNum) : 0;
            local.localPawnAddr = lp;

            if (lp) {
                uintptr_t localNode = g_mem.Read<uintptr_t>(lp + schemas::m_pGameSceneNode);
                if (localNode) local.localOrigin = g_mem.Read<Vector3>(localNode + schemas::m_vecAbsOrigin);
                uint32_t localFlags = g_mem.Read<uint32_t>(lp + schemas::m_fFlags);
                float eyeHeight = (localFlags & 2) ? 48.f : 64.f; // FL_DUCKING
                local.onGround = (localFlags & 1) != 0;           // FL_ONGROUND
                local.localEyePos = { local.localOrigin.x,
                                      local.localOrigin.y,
                                      local.localOrigin.z + eyeHeight };
                // UC-Throw: Spielergeschwindigkeit für Jump/Run-Throws.
                Vector3 lv = g_mem.Read<Vector3>(lp + schemas::m_vecAbsVelocity);
                if (isfinite(lv.x) && isfinite(lv.y) && isfinite(lv.z) &&
                    fabsf(lv.x) < 2000.f && fabsf(lv.y) < 2000.f && fabsf(lv.z) < 2000.f)
                    local.localVelocity = lv;
            }

            // View Angles: primär Pawn-Eye-Angles (Netzwerk-Winkel, kein dw
            // nötig — update-sicher). Fallbacks: dwViewAngles, CSGOInput.
            // Bereichs-Check gegen eingefrorene Bögen (Throw/Radar).
            {
                bool done = false;
                if (lp) {
                    Vector3 av = g_mem.Read<Vector3>(lp + schemas::m_angEyeAngles);
                    if (isfinite(av.x) && isfinite(av.y) &&
                        av.x > -89.f && av.x < 89.f &&
                        av.y >= -180.f && av.y <= 180.f &&
                        (av.x != 0.f || av.y != 0.f)) {
                        local.localViewAngles = av;
                        done = true;
                    }
                }
                if (!done) {
                    Vector3 direct = g_mem.Read<Vector3>(g_client + offsets::dwViewAngles);
                    bool sane = isfinite(direct.x) && isfinite(direct.y) &&
                                direct.x > -89.f && direct.x < 89.f &&
                                direct.y >= -180.f && direct.y <= 180.f &&
                                (direct.x != 0.f || direct.y != 0.f);
                    if (sane) {
                        local.localViewAngles = direct;
                    } else {
                        uintptr_t csgoInput = g_mem.Read<uintptr_t>(g_client + offsets::dwCSGOInput);
                        if (csgoInput) {
                            Vector3 fb = g_mem.Read<Vector3>(csgoInput + 0x688);
                            if (isfinite(fb.x) && isfinite(fb.y) &&
                                fb.x > -89.f && fb.x < 89.f &&
                                fb.y >= -180.f && fb.y <= 180.f)
                                local.localViewAngles = fb;
                        }
                    }
                }
            }

            QRD_NOW(tEnt0);
            local.playerCount = ReadPlayers(es, local.players, MAX_PLAYERS, local.vm, sw, sh,
                                            aimLocal.targets, &aimLocal.count,
                                            local.localEyePos, local.localViewAngles,
                                            medTick, slowTick);
            QRD_NOW(tEnt1);
            QRD_NOW(tBomb0);
            ReadBombData(local.bomb, lp, es);
            QRD_NOW(tBomb1);

            QRD_NOW(tWep0);
            ReadLocalWeaponAndRecoil(local, lp);
            QRD_NOW(tWep1);
            s_accWeapons.fetch_add(Qus(tWep0, tWep1), std::memory_order_relaxed);

            // Sensitivitaet
            {
                uintptr_t sensPtr = g_mem.Read<uintptr_t>(g_client + offsets::dwSensitivity);
                if (sensPtr)
                    local.gameSensitivity = g_mem.Read<float>(sensPtr + offsets::dwSensitivity_sensitivity);
            }

            // Boden-Items: FENSTER-Sweep statt Block (keine Stalls mehr).
            // Merge per Slot in persistenten Cache — frisch + lückenlos.
            DWORD now = GetTickCount();
            {
                static int curGround = kEntityListStart;
                static bool groundWrapped = false;
                bool wantGround = focused && (g_cfg.itemEsp || g_cfg.throwPreview ||
                                  (g_cfg.utilRadiusEnable && g_cfg.utilRadiusFire) ||
                                  (g_cfg.chamsEnable && g_cfg.chamsItems) ||
                                  (g_cfg.radarEnable && g_cfg.radarFull && g_cfg.radarFullBomb));
                if (wantGround) {
                    QRD_NOW(tIt0);
                    int ws = curGround, we = ws + kWinGround;
                    if (we > kEntityListEnd) we = kEntityListEnd;
                    GroundItem buf[128];
                    int n = ReadGroundItemsWindow(buf, 128, local.vm, sw, sh, es, ws, we, lp);
                    MergeGround(s_ground, buf, n, ws, we);
                    // Bodenproben fürs Landeprognose-Raster: Items am Boden
                    // (inkl. C4) direkt; Projektile erst, wenn sie sich gesetzt
                    // haben (Restgeschwindigkeit klein).
                    for (int k = 0; k < n; ++k) {
                        const GroundItem& gi = buf[k];
                        if (gi.origin.x == 0.f && gi.origin.y == 0.f && gi.origin.z == 0.f) continue;
                        bool grounded = !gi.isProj;
                        if (gi.isProj && gi.velOk) {
                            float sp2 = gi.vel.x*gi.vel.x + gi.vel.y*gi.vel.y + gi.vel.z*gi.vel.z;
                            grounded = sp2 < 60.f * 60.f;
                        }
                        if (grounded)
                            g_groundGrid.Feed(gi.origin.x, gi.origin.y, gi.origin.z, (uint32_t)GetTickCount());
                    }
                    // ── Selbstkalibrierung aus echten Projektilen ───────────
                    // Zünder je Typ (m_flDetonateTime - m_flSpawnTime) und
                    // Wurf-Speed K (eigener Standwurf vs. Formel). Einmal je
                    // Entity; EMA glättet Messrauschen.
                    for (int k = 0; k < n; ++k) {
                        const GroundItem& gi = buf[k];
                        if (!gi.isProj || !gi.entAddr || NadeAlreadyObserved(gi.entAddr)) continue;
                        int kind = NadeKindFromProjName(gi.name);
                        if (kind < 0) continue;
                        if ((kind == NADEK_HE || kind == NADEK_FLASH) &&
                            gi.ageAtRead >= 0.f && gi.detonateIn > -2.f) {
                            float fuse = gi.ageAtRead + gi.detonateIn;
                            if (fuse > 0.4f && fuse < 8.f) {
                                float cur = g_nadeFuseS[kind].load(std::memory_order_relaxed);
                                if (cur > 0.f) fuse = cur * 0.65f + fuse * 0.35f;
                                g_nadeFuseS[kind].store(fuse, std::memory_order_relaxed);
                                if (g_cfg.debugLog)
                                    printf("[nade] Zuender %s kalibriert: %.2fs\n", gi.name, fuse);
                            }
                        }
                        if (gi.ownedByLocal && s_nadeLastStrength >= 0.f &&
                            fabsf(s_nadeLastVel.x) < 30.f &&
                            fabsf(s_nadeLastVel.y) < 30.f &&
                            fabsf(s_nadeLastVel.z) < 30.f) {
                            float vlen = sqrtf(gi.v0.x*gi.v0.x + gi.v0.y*gi.v0.y + gi.v0.z*gi.v0.z);
                            float scale = s_nadeLastStrength * 0.7f + 0.3f;
                            if (scale > 0.45f && vlen > 100.f) {
                                float K = vlen / scale;
                                if (K > 650.f && K < 1700.f) {
                                    float cur = g_nadeSpeedK.load(std::memory_order_relaxed);
                                    K = cur * 0.6f + K * 0.4f;
                                    g_nadeSpeedK.store(K, std::memory_order_relaxed);
                                    if (g_cfg.debugLog)
                                        printf("[nade] Wurf-Speed K kalibriert: %.0f (v0=%.0f, strength=%.2f)\n",
                                               K, vlen, s_nadeLastStrength);
                                }
                            }
                        }
                        NadeMarkObserved(gi.entAddr);
                    }
                    if (we >= kEntityListEnd) { curGround = kEntityListStart; groundWrapped = true; }
                    else curGround = we;
                    s_ground.valid = groundWrapped;
                    QRD_NOW(tIt1);
                    s_usItemsTick = Qus(tIt0, tIt1);
                } else if (!g_cfg.itemEsp && !g_cfg.throwPreview &&
                           !(g_cfg.utilRadiusEnable && g_cfg.utilRadiusFire) &&
                           !(g_cfg.chamsEnable && g_cfg.chamsItems) &&
                           !(g_cfg.radarEnable && g_cfg.radarFull && g_cfg.radarFullBomb)) {
                    s_ground = GroundCache{};
                    curGround = kEntityListStart;
                    groundWrapped = false;
                }
                // Unfokussiert: Cache + Cursor behalten (kein Neuaufbau nötig)
            }

            // Gepflanzte C4 in den Ground-Snapshot fuer den Chams-Thread
            // (Bomben-Glow unabhaengig vom Namen-Sweep).
            s_ground.plantedC4 = g_bombEntForChams;

            local.valid   = true;
            aimLocal.valid = true;
            // Kartenname fürs Ganzkarten-Radar (1×/s, gepuffert — billig).
            {
                const char* mn = ReadMapNameCached();
                if (mn && mn[0]) {
                    memcpy(local.mapName, mn, sizeof(local.mapName) - 1);
                    local.mapName[sizeof(local.mapName) - 1] = 0;
                }
            }
            // Kartenwechsel/Teleport: springt der lokale Ursprung in EINEM
            // Tick um > 8000u (im Match unmöglich — Maps sind kleiner), sind
            // alle Raster-Proben Schrott → säubern, sonst projizieren alte
            // Höhen der Vormap auf die neue Karte.
            {
                static Vector3 s_lastLocal{};
                static bool    s_haveLastLocal = false;
                if (s_haveLastLocal) {
                    float dx = local.localOrigin.x - s_lastLocal.x;
                    float dy = local.localOrigin.y - s_lastLocal.y;
                    float dz = local.localOrigin.z - s_lastLocal.z;
                    if (dx*dx + dy*dy + dz*dz > 8000.f * 8000.f)
                        g_groundGrid.Clear();
                }
                s_lastLocal = local.localOrigin;
                s_haveLastLocal = true;
            }
            // Eigener Standpunkt = verlässlichste Bodenprobe (Füße am Boden).
            if (local.localOrigin.x != 0.f || local.localOrigin.y != 0.f || local.localOrigin.z != 0.f)
                g_groundGrid.Feed(local.localOrigin.x, local.localOrigin.y, local.localOrigin.z,
                                  (uint32_t)GetTickCount());
            // stampMs wird oben gesetzt (sofort nach VM-Read) — Alter bleibt akkurat

            // Util-Zonen: FENSTER-Sweep (64 Slots/Tick ≈ volle Abdeckung je
            // ~220ms — reicht für 18s-Smokes und 5s-Feuer bei weitem).
            // Läuft auch für Smoke-Checks (Aim/Trigger) ohne Visual-Modul.
            {
                static int curUtil = 1;
                static bool utilWrapped = false;
                bool needUtil = g_cfg.utilRadiusEnable ||
                                (g_cfg.aimSmokeCheck && g_cfg.aimEnable) ||
                                (g_cfg.triggerSmokeCheck && g_cfg.triggerbotEnable);
                if (focused && needUtil) {
                    QRD_NOW(tUt0);
                    int ws = curUtil, we = ws + kWinUtil;
                    if (we > kEntityListEnd) we = kEntityListEnd;
                    UtilZone buf[MAX_UTIL_ZONES];
                    int n = ReadUtilZonesWindow(buf, MAX_UTIL_ZONES, es, ws, we);
                    MergeUtil(s_util, buf, n, ws, we);
                    // Smokes/Infernos liegen am Boden → direkt als Bodenproben
                    // fürs Landeprognose-Raster übernehmen.
                    for (int k = 0; k < n; ++k)
                        g_groundGrid.Feed(buf[k].origin.x, buf[k].origin.y, buf[k].origin.z, (uint32_t)GetTickCount());
                    if (we >= kEntityListEnd) { curUtil = 1; utilWrapped = true; }
                    else curUtil = we;
                    s_util.valid = utilWrapped;
                    QRD_NOW(tUt1);
                    s_usUtilTick = Qus(tUt0, tUt1);
                } else if (!g_cfg.utilRadiusEnable &&
                           !(g_cfg.aimSmokeCheck && g_cfg.aimEnable) &&
                           !(g_cfg.triggerSmokeCheck && g_cfg.triggerbotEnable)) {
                    s_util = UtilCache{};
                    curUtil = 1;
                    utilWrapped = false;
                }
            }

            // Chicken-/Ball: FENSTER-Sweep (146 Slots/Tick ≈ Abdeckung je
            // ~100ms — wie bisherige Kadenz, aber ohne Block).
            {
                static int curEnt = kEntityListStart;
                static bool entWrapped = false;
                bool wantChicken = focused && g_cfg.chickenAimEnable;
                bool wantBall    = focused && g_cfg.ballAimEnable;
                if (wantChicken || wantBall) {
                    QRD_NOW(tCh0);
                    int ws = curEnt, we = ws + kWinChicken;
                    if (we > kEntityListEnd) we = kEntityListEnd;
                    EntAimTarget cBuf[MAX_ENT_TARGETS]{}, bBuf[MAX_ENT_TARGETS]{};
                    int nc = 0, nb = 0;
                    ReadChickenAndBallWindow(cBuf, &nc, bBuf, &nb, MAX_ENT_TARGETS, es, ws, we);
                    if (wantChicken) MergeEnt(s_chicken, cBuf, nc, ws, we);
                    else s_chicken = EntAimCache{};
                    if (wantBall) MergeEnt(s_ball, bBuf, nb, ws, we);
                    else s_ball = EntAimCache{};
                    if (we >= kEntityListEnd) { curEnt = kEntityListStart; entWrapped = true; }
                    else curEnt = we;
                    s_chicken.valid = s_ball.valid = entWrapped;
                    QRD_NOW(tCh1);
                    s_usChickenTick = Qus(tCh0, tCh1);
                } else {
                    if (!g_cfg.chickenAimEnable) s_chicken = EntAimCache{};
                    if (!g_cfg.ballAimEnable) s_ball = EntAimCache{};
                    curEnt = kEntityListStart;
                    entWrapped = false;
                }
                s_chicken.vm = s_ball.vm = local.vm;
            }

            // Entity-Census (alle ~10s, eigener Timer, nur mit Debug-Log)
            if (g_cfg.debugLog && (g_cfg.ballAimEnable || g_cfg.utilRadiusEnable) && focused) LogEntityCensus(es);

            // Snapshot publizieren — LOCKFREI (Triple-Buffer, kein Mutex):
            // Back-Puffer füllen, dann Front-Index atomar umschalten.
            QRD_NOW(tPub0);
            {
                int back = (g_snapFront.load(std::memory_order_relaxed) + 1) % 3;
                g_snap[back].game    = local;
                g_snap[back].aim     = aimLocal;
                g_snap[back].ground  = s_ground;
                g_snap[back].chicken = s_chicken;
                g_snap[back].ball    = s_ball;
                g_snap[back].util    = s_util;
                g_snapFront.store(back, std::memory_order_release);
            }
            QRD_NOW(tPub1);
            QRD_NOW(tTick1);

            // Stats übergeben (Bones/Weapons-Akkus pro Tick leeren)
            {
                long long bUs = s_accBones.exchange(0, std::memory_order_relaxed);
                long long wUs = s_accWeapons.exchange(0, std::memory_order_relaxed);
                long long entGross = Qus(tEnt0, tEnt1);
                long long entNet = entGross - bUs;
                if (entNet < 0) entNet = 0;
                // Waffen-Anteil der Spieler steckt in entGross; Lokal-Anteil separat.
                // Darstellung: Entities netto + Bones + Weapons (Summe = entGross + lokal).
                g_readerStats.usEntities.store(entNet, std::memory_order_relaxed);
                g_readerStats.usBones.store(bUs, std::memory_order_relaxed);
                g_readerStats.usWeapons.store(wUs, std::memory_order_relaxed);
                g_readerStats.usBomb.store(Qus(tBomb0, tBomb1), std::memory_order_relaxed);
                g_readerStats.usItems.store(s_usItemsTick, std::memory_order_relaxed);
                g_readerStats.usUtil.store(s_usUtilTick, std::memory_order_relaxed);
                g_readerStats.usChicken.store(s_usChickenTick, std::memory_order_relaxed);
                g_readerStats.usPublish.store(Qus(tPub0, tPub1), std::memory_order_relaxed);
                g_readerStats.usTotal.store(Qus(tTick0, tTick1), std::memory_order_relaxed);
                g_readerStats.playerCount.store(local.playerCount, std::memory_order_relaxed);
            }
        }

        // Pacing: fokussiert per Config (60..240 Hz), Hintergrund fix 20 Hz.
        // Schläft effizient (kein Busy-Loop) — Kern bleibt kühl.
        int hz = focused ? g_cfg.readerHz : 20;
        if (hz < 20) hz = 20;
        if (hz > 240) hz = 240;
        const long long periodUs = 1000000LL / (long long)hz;
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - frameStart).count();
        auto sleepUs = periodUs - elapsed;
        if (sleepUs > 200LL)
            std::this_thread::sleep_for(std::chrono::microseconds(sleepUs));
    }
}