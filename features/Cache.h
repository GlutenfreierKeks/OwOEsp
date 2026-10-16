#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// features/Cache.h — Datenmodelle + Snapshot-Globals
// Der Reader-Thread fuellt diese Strukturen, der Render-Thread liest sie nur.
// Thread-Steuerung lebt hier (externe Definition in features/Reader.cpp).
// ─────────────────────────────────────────────────────────────────────────────
#include "../core/SDK.h"
#include "Weapons.h"   // NadeKind-Mapping (Granaten-IDs)
#include <atomic>
#include <thread>
#include <mutex>
#include <cstring>

// ── Konstanten ───────────────────────────────────────────────────────────────
constexpr int MAX_PLAYERS      = 64;
constexpr int MAX_GROUND_ITEMS = 128;
constexpr int MAX_ENT_TARGETS  = 64;   // Chicken- / Ball-Ziele (meist < 10)

// ── Granaten-Typen (Index für Kalibrierung) ─────────────────────────────────
enum NadeKind : int {
    NADEK_SMOKE = 0, NADEK_MOLOTOV, NADEK_INCG, NADEK_HE, NADEK_FLASH, NADEK_DECOY,
    NADEK_COUNT
};

inline int NadeKindFromWeaponId(int id) {
    switch (id) {
        case weapons::WEAPON_SMOKEGRENADE: return NADEK_SMOKE;
        case weapons::WEAPON_MOLOTOV:      return NADEK_MOLOTOV;
        case weapons::WEAPON_INCGRENADE:   return NADEK_INCG;
        case weapons::WEAPON_HEGRENADE:    return NADEK_HE;
        case weapons::WEAPON_FLASHBANG:    return NADEK_FLASH;
        case weapons::WEAPON_DECOY:        return NADEK_DECOY;
        default: return -1;
    }
}

// GroundItem-Namen der Projektile (Reader-Klassifikation) → NadeKind
inline int NadeKindFromProjName(const char* n) {
    if (!n) return -1;
    if (!strcmp(n, "Smoke"))      return NADEK_SMOKE;
    if (!strcmp(n, "Molotov"))    return NADEK_MOLOTOV;
    if (!strcmp(n, "Incendiary")) return NADEK_INCG;
    if (!strcmp(n, "HE"))         return NADEK_HE;
    if (!strcmp(n, "Flash"))      return NADEK_FLASH;
    if (!strcmp(n, "Decoy"))      return NADEK_DECOY;
    return -1;
}

// ── Selbstkalibrierung Reader → Render (lockfrei, relaxed) ──────────────────
// g_nadeSpeedK: Basis-Wurfgeschwindigkeit der Formel (0.7*strength+0.3)*K.
// Der Reader misst echte Würfe (m_vInitialVelocity) und korrigiert live.
// g_nadeFuseS: Zünderzeiten je NadeKind — Reader lernt aus echten Projektilen
// (m_flDetonateTime - m_flSpawnTime); Pre-Throw-Schätzung nutzt sie.
extern std::atomic<float> g_nadeSpeedK;
extern std::atomic<float> g_nadeFuseS[NADEK_COUNT];

// ── Bombendaten ──────────────────────────────────────────────────────────────
struct BombData {
    float blowTime    = 0.f;
    float timerLength = 40.f;
    float curTime     = 0.f;
    float defuseCountDown = 0.f;
    float defuseLength    = 0.f;   // 5 (Kit) oder 10 (ohne) — Dauer dieses Defuse
    int   bombSite    = 0;
    bool  active      = false;
    bool  beingDefused  = false;
    bool  hasExploded   = false;
    bool  localHasDefuser = false;
    bool  localInBombZone = false;
    bool  defuserHasKit   = false; // Kit des aktuellen Defusers (nicht local)
    bool  defuserKnown    = false;
    // Wallclock-Fallback (wenn curtime unlesbar ist): Wanduhr beim
    // ersten Tick-Erkennen + Timerlaenge -> Countdown ohne Spielzeit
    unsigned long wallStartMs = 0;
    float         wallTimer   = 0.f;
};

// ── Skeleton-Bones (Screen-Space) ────────────────────────────────────────────
constexpr int kBoneCount = 28;
struct BoneScreen {
    Vector2 pos{};
    bool    ok = false;
};

// ── Ziel (fuer Aim/Trigger/Rage) ─────────────────────────────────────────────
struct AimTarget {
    Vector3 origin;
    int     team;
    int     health;
    bool    alive;
    bool    crouching = false;
    bool    visible   = false; // Spotted-Mask (unzuverlaessig, nur optional)
    Vector3 velocity{};        // fuer Vorhersage (PredictPos)
};

struct AimCache {
    AimTarget targets[MAX_PLAYERS];
    int       count     = 0;
    int       localTeam = 0;
    Matrix4x4 vm;
    bool      valid     = false;
    uint32_t  stampMs   = 0; // Reader-Zeitpunkt (Alter = GetTickCount()-stamp)
};

// ── Chicken-/Ball-Ziele (fuer Chicken-/Ball-Aimbot) ──────────────────────────
// Gefuellt vom Reader-Thread per DesignerName-Scan ("chicken" bzw.
// ball-artige Namen). Der Aimbot projiziert sie wie Spieler-Ziele.
struct EntAimTarget {
    Vector3 origin;
    bool    alive = true;
    int     slot = -1; // Entity-Slot (Merge bei Fenster-Sweeps)
};

struct EntAimCache {
    EntAimTarget targets[MAX_ENT_TARGETS];
    int          count = 0;
    Matrix4x4    vm;
    bool         valid = false;
};

// ── ESP-Spieler ──────────────────────────────────────────────────────────────
struct PlayerData {
    Vector2 feet, head, chest;
    Vector3 worldOrigin;
    int     health, team;
    char    name[32];
    bool    alive;
    bool    visible; // Mask-basiert (lokal) — true wenn du ihn siehst (halb hinter Wand = true)
    bool    radarSpotted; // m_bSpotted (Radar, irgendein Team hat ihn gesehen)
    bool    crouching;
    int     weaponId = 0;
    int     weaponIds[4] = {};
    int     weaponCount = 0;
    BoneScreen bones[kBoneCount]{}; // gefuellt wenn Skeleton aktiv
    bool    hasBones = false;
    Vector3 velocity{};             // fuer Vorhersage (PredictPos)
    Vector3 boneWorld[kBoneCount]{};// Welt-Knochen (Render projiziert vorhergesagt)
    uintptr_t pawnAddr = 0;         // Pawn-Adresse (Spec-Matching, Trigger-Vis)
    uintptr_t specPawn = 0;         // Observer-Ziel (0 = schaut niemandem zu)
    int     specMode = 0;           // 4=Eye, 5=Chase, sonst Spec
};

// ── Kompletter Frame-Snapshot ────────────────────────────────────────────────
struct GameCache {
    Matrix4x4  vm;
    PlayerData players[MAX_PLAYERS];
    BombData   bomb;
    int        playerCount = 0;
    int        localTeam   = 0;
    int        currentWeaponId = 0;
    Vector3    localOrigin = {};
    Vector3    localEyePos = {};     // origin + view offset (eye position)
    Vector3    localVelocity = {};   // UC-Throw: Spielergeschwindigkeit (Jump/Run-Throws)
    Vector3    localViewAngles = {}; // pitch, yaw, roll aus dwViewAngles
    Vector3    punchAngle  = {};     // m_vecCsViewPunchAngle
    int        shotsFired  = 0;      // m_iShotsFired
    float      gameSensitivity = 2.5f;
    bool       onGround    = false;  // FL_ONGROUND des lokalen Spielers
    bool       valid       = false;

    // Throw Preview (lokale Util)
    bool       holdingUtil    = false;
    bool       pinPulled      = false;
    float      throwStrength  = 1.f;   // 0..1
    int        utilWeaponId   = 0;

    uintptr_t  localPawnAddr  = 0; // eigener Pawn (Spec-Matching: wer schaut mir zu)

    uint32_t   stampMs        = 0; // Reader-Zeitpunkt (Alter = GetTickCount()-stamp)
    char       mapName[32]{};      // Kartenname (Ganzkarten-Radar, aus GlobalVars)
};

// ── Boden-Items (Waffen, Bombe, Util) ────────────────────────────────────────
struct GroundItem {
    Vector3 origin;
    Vector2 screen;
    bool    onScreen = false;
    int     type     = 0; // 0=weapon, 1=bomb, 2=util
    int     itemId   = 0; // weapon def index or 0
    char    name[32]{};
    int     slot = -1;    // Entity-Slot (Merge bei Fenster-Sweeps)

    // ── Projektil-Details (nur für *_projectile gesetzt, sonst Defaults) ────
    // Basis für das In-Flight-Tracking: echte Ballistik (p0/v0) statt Winkel-
    // Formel, exakte Zünderzeit (m_flDetonateTime) und Eigentümer-Check.
    bool    isProj       = false;
    bool    ownedByLocal = false;  // m_hThrower == lokaler Pawn
    bool    velOk        = false;  // m_vecAbsVelocity plausibel
    int     bounces      = 0;      // m_nBounces (Wandkontakte)
    float   ageAtRead    = -1.f;   // curtime - m_flSpawnTime (s), -1 = unbekannt
    float   detonateIn   = -1.f;   // m_flDetonateTime - curtime (s), -1 = unbekannt
    uintptr_t entAddr    = 0;      // Entity-Adresse (Einmal-Observation)
    uint32_t stampMs     = 0;      // Reader-Lesezeitpunkt (GetTickCount)
    Vector3 p0{};                  // m_vInitialPosition (Wurf-Ort)
    Vector3 v0{};                  // m_vInitialVelocity (Wurf-Geschwindigkeit)
    Vector3 vel{};                 // m_vecAbsVelocity (aktuell)
};

struct GroundCache {
    GroundItem items[MAX_GROUND_ITEMS];
    int        count = 0;
    bool       valid = false;
    // Gepflanzte C4 (vom Bomb-Modul aufgeloest). Landet nicht zuverlaessig im
    // Namen-Sweep — eigener Kanal fuers Item-Chams (Bomben-Farbe).
    uintptr_t  plantedC4 = 0;
};

// ── Boden-Höhenraster (externes Mini-„Navmesh“ für Landeprognosen) ──────────
// Extern gibt es keinen BSP/Trace-Zugriff — Landepunkte wurden früher auf eine
// flache Ebene projiziert (falsch über Kanten/Dächern/Abgründen). Stattdessen
// sammelt der Reader echte Bodenproben aus der laufenden Partie
// (Spielerfüße, Items am Boden, Util-Zonen, gelandete Nades) in ein grobes
// 256u-Raster; die Prognose landet auf der tatsächlichen Oberfläche.
// Ohne Probe in Reichweite wird KEIN Fake-Landepunkt mehr geraten.
// Threading: Reader schreibt, Render liest — 4B-Writes, benign races (wie
// überall in diesem Projekt; Werte sind nur Schätzgrundlage).
constexpr int      kGroundGridN       = 128;      // 128 × 128 Zellen
constexpr float    kGroundGridCell    = 256.f;    // ≈ ±16384u ab Weltmitte
constexpr uint32_t kGroundGridFreshMs = 900000u;  // Proben 15 min gültig (Clear() bei Kartenwechsel)

struct GroundGrid {
    float    z[kGroundGridN * kGroundGridN] = {};
    uint32_t t[kGroundGridN * kGroundGridN] = {}; // 0 = nie befüllt

    // Bodenprobe einsammeln: je Zelle im Frischefenster den TIEFSTEN Wert
    // behalten (Spieler auf Kisten „heben“ den Boden nicht an).
    void Feed(float x, float y, float zVal, uint32_t now) {
        int cx = (int)(x / kGroundGridCell + kGroundGridN * 0.5f);
        int cy = (int)(y / kGroundGridCell + kGroundGridN * 0.5f);
        if (cx < 0 || cy < 0 || cx >= kGroundGridN || cy >= kGroundGridN) return;
        int i = cy * kGroundGridN + cx;
        if (t[i] == 0 || now - t[i] > kGroundGridFreshMs || zVal < z[i]) z[i] = zVal;
        t[i] = now;
    }

    // Alle Proben vergessen (Kartenwechsel/Teleport) — alte Höhen würden
    // sonst auf die neue Map projiziert. Wird vom Reader bei großen
    // Positionssprüngen des lokalen Spielers aufgerufen.
    void Clear() {
        memset(z, 0, sizeof(z));
        memset(t, 0, sizeof(t));
    }

    // Bodenprobe an (x,y). Rückgabe:
    //  1 = Fläche gefunden: outZ = HÖCHSTE Fläche unter aboveZ+2 (Dächer/
    //      Plattformen landen korrekt, Brücken bleiben unterfliegbar).
    //  2 = Geländefront: in der EIGENEN Zelle liegt (ohne tiefere Probe) nur
    //      eine Probe knapp ÜBER der Bahn (2..18u) → die Nade steckt in
    //      Hang/Mauer. outZ = deren Höhe. Nur eigene Zelle, sonst stoppen
    //      Naden, die an Wänden vorbeifliegen (256u-Raster!).
    //  0 = keine Daten in Reichweite (kein geratener Marker).
    // aboveZ = aktuelle Flughöhe.
    int ProbeFloor(float x, float y, float& outZ, uint32_t now, float aboveZ, int maxR = 3) const {
        int cx = (int)(x / kGroundGridCell + kGroundGridN * 0.5f);
        int cy = (int)(y / kGroundGridCell + kGroundGridN * 0.5f);

        // Eigene Zelle: Boden hat Vorrang vor „Wand" (Tunnel mit Bodenproben
        // innen wird nicht vom Deckensample darüber gestoppt).
        if (cx >= 0 && cy >= 0 && cx < kGroundGridN && cy < kGroundGridN) {
            int i = cy * kGroundGridN + cx;
            if (t[i] && now - t[i] <= kGroundGridFreshMs) {
                if (z[i] <= aboveZ + 2.f) { outZ = z[i]; return 1; }
                if (z[i] <= aboveZ + 18.f) { outZ = z[i]; return 2; }
            }
        }

        // Nachbarringe: nur echte Flächen unter der Bahn (Wand-Heuristik
        // gilt ausschließlich in der eigenen Zelle).
        for (int r = 1; r <= maxR; ++r) {
            bool found = false; float best = 0.f;
            for (int dy = -r; dy <= r; ++dy)
                for (int dx = -r; dx <= r; ++dx) {
                    if (dy > -r && dy < r && dx > -r && dx < r) continue; // nur Ring
                    int gx = cx + dx, gy = cy + dy;
                    if (gx < 0 || gy < 0 || gx >= kGroundGridN || gy >= kGroundGridN) continue;
                    int i = gy * kGroundGridN + gx;
                    if (!t[i] || now - t[i] > kGroundGridFreshMs) continue;
                    if (z[i] > aboveZ + 2.f) continue;           // liegt über der Bahn
                    if (!found || z[i] > best) { best = z[i]; found = true; }
                }
            if (found) { outZ = best; return 1; }
        }
        return 0;
    }
};
inline GroundGrid g_groundGrid; // Reader füttert, Render liest

// ── Util-Zonen (Smoke-Volumen + Inferno-Flammen) ────────────────────────────
constexpr int MAX_UTIL_ZONES = 16;
constexpr int MAX_UTIL_FLAMES = 24;

// Lebensdauern (CS2): Smoke 18s ab Detonation, Feuer ~7s ab Brandbeginn.
// Der Reader stempelt firstSeenMs beim ersten Sichtkontakt — der Render-
// Thread rechnet daraus die Restzeit (Korrektur per Sweep-Latenz ≤ ~0,2s).
constexpr float kSmokeDurationS = 18.f;
constexpr float kFireDurationS  = 7.f;

struct UtilZone {
    Vector3 origin;                 // Smoke-Detonationspunkt / Inferno-Mitte
    Vector3 flames[MAX_UTIL_FLAMES];// Inferno: echte Flammenpositionen
    int     flameCount = 0;
    int     type = 0; // 0=Smoke, 1=Feuer
    int     slot = -1; // Entity-Slot (Merge bei Fenster-Sweeps)
    uint32_t firstSeenMs = 0; // Reader-Stempel: erster Sichtkontakt
};

struct UtilCache {
    UtilZone zones[MAX_UTIL_ZONES];
    int      count = 0;
    bool     valid = false;
};

// ── Thread-Steuerung (Definition in features/Reader.cpp) ────────────────────
extern std::atomic<bool> g_stopThread;
extern std::thread       g_readThread;
extern std::thread       g_bhopThread;
extern std::thread       g_chamsThread;
extern std::thread       g_smoothThread;
extern std::atomic<int>  g_scrW;
extern std::atomic<int>  g_scrH;

// ── Smoke-Sperrfläche (UC-Standard: Kugel um Detonationspunkt) ──────────────
constexpr float kSmokeRadius = 150.f;

// True wenn Strecke Auge->Ziel durch eine aktive Smoke-Wolke geht.
// Fail-open: keine Zonen = kein Block.
inline bool SmokeBlocksShot(const Vector3& eye, const Vector3& tgt,
                            const UtilZone* zones, int count) {
    float abx = tgt.x - eye.x, aby = tgt.y - eye.y, abz = tgt.z - eye.z;
    float len2 = abx*abx + aby*aby + abz*abz;
    if (len2 < 0.001f) return false;
    for (int i = 0; i < count; i++) {
        if (zones[i].type != 0) continue; // nur Smokes blockieren Sicht
        const Vector3& c = zones[i].origin;
        float t = ((c.x - eye.x) * abx + (c.y - eye.y) * aby + (c.z - eye.z) * abz) / len2;
        if (t < 0.f) t = 0.f; else if (t > 1.f) t = 1.f;
        float dx = eye.x + abx * t - c.x;
        float dy = eye.y + aby * t - c.y;
        float dz = eye.z + abz * t - c.z;
        if (dx*dx + dy*dy + dz*dz <= kSmokeRadius * kSmokeRadius) return true;
    }
    return false;
}

// ── Snapshots (Triple-Buffer, lockfrei) ──────────────────────────────────────
// Der Reader-Thread baut Back-Snapshots, der Render-Thread liest den Front-
// Snapshot. Niemals blockiert einer den anderen, keine Copies im Render-Loop:
//   Reader: s_snap[(front+1)%3] füllen → front atomar umschalten
//   Render: front EINMAL laden → direkt aus g_snap[front] lesen
// Mit 3 Puffern schreibt der Reader nie in den gelesenen Puffer.
struct SnapshotSet {
    GameCache   game;
    AimCache    aim;
    GroundCache ground;
    EntAimCache chicken;
    EntAimCache ball;
    UtilCache   util;
};

// ── Reader-Mikro-Profiler (µs, atomar, vernachlässigbarer Overhead) ─────────
struct ReaderStats {
    std::atomic<long long> usEntities{ 0 }; // Spieler-Loop ohne Bones/Weapons
    std::atomic<long long> usBones{ 0 };
    std::atomic<long long> usWeapons{ 0 };  // Spieler- + Lokal-Waffe
    std::atomic<long long> usItems{ 0 };    // Ground-/Projektile-Scan
    std::atomic<long long> usBomb{ 0 };
    std::atomic<long long> usUtil{ 0 };
    std::atomic<long long> usChicken{ 0 };
    std::atomic<long long> usPublish{ 0 };  // Snapshot bauen + umschalten
    std::atomic<long long> usTotal{ 0 };    // ganzer Tick
    std::atomic<int>  freqHz{ 0 };          // gemessene Tick-Rate
    std::atomic<int>  playerCount{ 0 };
};

extern SnapshotSet g_snap[3];
extern std::atomic<int> g_snapFront;
extern ReaderStats g_readerStats;

// Front-Snapshot holen (EIN laden pro Frame, danach lockfrei lesen)
inline const SnapshotSet& SnapFront() {
    return g_snap[g_snapFront.load(std::memory_order_acquire)];
}