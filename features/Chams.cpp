#include "pch.h"
#include "Chams.h"
#include "Offsets.h"
#include "Reader.h"
#include "../core/Globals.h"
#include "../core/SDK.h"
#include <cstdint>
#include <cstdio>
#include <cstring>

// ─────────────────────────────────────────────────────────────────────────────
// features/Chams.cpp — externe Chams über die Glow-Properties (UC-Standard)
//
// Extern (ohne Injection/Material-Hooks) geht kein echtes Model-Tinting —
// der UC-Weg für externe CS2-Builds ist der Outline-Glow: pro Entity die
// eingebettete CGlowProperty beschreiben — m_iGlowType = 3 (Outline),
// m_glowColorOverride (RGBA), m_bGlowing + m_bEligibleForScreenHighlight = 1.
//
// WARUM HAMMER-MODUS: CS2 setzt die Glow-Flags (m_bGlowing, teils auch Typ/
// Farbe) frame-weise selbst zurück. Ein einzelner Write „blitzt" daher nur
// für einen Frame auf. Der Thread schreibt den Glow-Block deshalb im
// ~1-ms-Takt neu (Sleep(1); timeBeginPeriod(1) läuft global, siehe App.cpp) —
// das ist der übliche externe Glow-Ansatz. Pro Zyklus laufen ZWEI Write-Bursts
// (mit Mini-Yield dazwischen): verdoppelt die effektive Rate und deckt beide
// Hälften eines Game-Frames ab — drückt das Rest-Flackern auf nahe null.
// Fremdfelder im Block (m_nGlowRange, m_bFlashing & Co.) kommen aus einem
// Read-Cache (1×/s aufgefrischt) — dadurch kostet ein Burst pro Entity genau
// EIN WriteProcessMemory (kein Read-Back).
//
// Ziel-Entities:
//   • Spieler (Pawns): Farbe je Sichtbarkeit (Spotted-Mask) — sichtbar/
//     verdeckt/Team getrennt konfigurierbar. Optional Health-Tint: Farbe
//     folgt den HP (gruen = voll, rot = fast tot) statt der Sichtbarkeit.
//   • Boden-Items (Waffen, C4, Util): eigene Farbe; die C4 (type 1) hat eine
//     EIGENE Bomben-Farbe. Gleicher Glow-Trick, weil Items
//     C_BaseModelEntity-Derivate mit m_Glow sind.
//
// Beim Deaktivieren/Exit läuft ein Clear-Pass über alle je beschriebenen
// Entities — keine stehenbleibenden Glows. Slots werden nach Tod/Despawn
// freigegeben (sonst laufen die 96 über die Runden voll).
//
// ⚠ Schreibzugriffe (wie Bhop) sind der auffälligste Vektor — bewusst nutzen.
//
// Offsets: schemas::m_Glow + CGlowProperty-Felder (a2x-Abgleich 16.10.2026,
// deckt sich mit inject/GameOffsets.h). Bei Game-Updates via OffsetsUpdater.
// ─────────────────────────────────────────────────────────────────────────────

namespace {

constexpr int kGlowOutline = 3; // CGlowProperty::m_iGlowType — Outline

// Je Entity (Pawn ODER Item, Key = Adresse) merken, was wir geschrieben
// haben (Clear-Pass, Slot-TTL) + Read-Cache der 0x22 Bytes ab m_iGlowType
// (Fremdfelder erhalten).
struct Tracked {
    uintptr_t pawn     = 0;
    bool      on       = false;   // zuletzt AN geschrieben?
    bool      seen     = false;   // in diesem Zyklus angetroffen?
    bool      wantOn   = false;   // Soll-Zustand dieses Zyklus
    uint32_t  wantCol  = 0;       // Soll-Farbe dieses Zyklus
    uint32_t  color    = 0;       // zuletzt geschriebene Farbe (Diagnose)
    uint32_t  lastMs   = 0;
    uint8_t   span[0x22] = {};
    bool      spanOk   = false;   // Cache gültig?
    uint32_t  spanMs   = 0;       // wann gelesen (1×/s Auffrischung)
};

Tracked s_track[96];

inline uint32_t PackColor(const float c[4]) {
    auto ch = [](float v) -> uint32_t {
        if (v < 0.f) v = 0.f;
        if (v > 1.f) v = 1.f;
        return (uint32_t)(v * 255.f + 0.5f);
    };
    return ch(c[0]) | (ch(c[1]) << 8) | (ch(c[2]) << 16) | (ch(c[3]) << 24);
}

// Health-Tint (Toggle „Health-Farbe" im Chams-Menue): 100 HP = gruen →
// 0 HP = rot. Hue 120°..0° (rot→gelb→gruen), gesaettigt, Alpha voll —
// der Glow zeigt so direkt, wie viel Leben das Ziel noch hat.
inline uint32_t PackHealthColor(int hp) {
    float t = (float)hp * 0.01f;      // 0..1
    if (t < 0.f) t = 0.f;
    if (t > 1.f) t = 1.f;
    const float h = t * 120.f;        // Grad: 0 = rot, 60 = gelb, 120 = gruen
    float r, g;
    if (h < 60.f) { r = 1.f;                     g = h / 60.f; }
    else          { r = 1.f - (h - 60.f) / 60.f; g = 1.f; }
    auto ch = [](float v) -> uint32_t { return (uint32_t)(v * 255.f + 0.5f); };
    return ch(r) | (ch(g) << 8) | ((uint32_t)30 << 16) | (0xFFu << 24);
}

// EIN Write über m_iGlowType..m_bGlowing. Unsere vier Felder werden jeden
// Aufruf frisch gepatcht; alle anderen Bytes kommen aus dem Read-Cache.
bool WriteGlowSpan(Tracked& t, uintptr_t pawn, bool on, uint32_t color, uint32_t nowMs) {
    if (!pawn || !schemas::m_Glow) return false;
    const uintptr_t spanStart = pawn + schemas::m_Glow + schemas::m_iGlowType;
    const size_t    spanLen   = (schemas::m_bGlowing + 1) - schemas::m_iGlowType;              // 0x22
    const size_t    offColor  = schemas::m_glowColorOverride - schemas::m_iGlowType;           // 0x10
    const size_t    offElig   = schemas::m_bEligibleForScreenHighlight - schemas::m_iGlowType; // 0x20
    const size_t    offOn     = schemas::m_bGlowing - schemas::m_iGlowType;                    // 0x21
    if (spanLen == 0 || spanLen > sizeof(t.span)) return false;
    if (offColor + 4 > spanLen || offElig >= spanLen || offOn >= spanLen) return false;

    if (!t.spanOk || nowMs - t.spanMs > 1000) {
        if (!g_mem.ReadBuffer(spanStart, t.span, spanLen)) { t.spanOk = false; return false; }
        t.spanOk = true;
        t.spanMs = nowMs;
    }

    const int32_t type = on ? kGlowOutline : 0;
    memcpy(t.span, &type, sizeof(type));
    memcpy(t.span + offColor, &color, sizeof(color));
    t.span[offElig] = on ? 1 : 0;
    t.span[offOn]   = on ? 1 : 0;

    return g_mem.WriteBuffer(spanStart, t.span, spanLen);
}

// Alles abdrehen, was wir je angeschrieben haben (Toggle aus / Exit).
void ClearAllTracked() {
    const uint32_t nowMs = (uint32_t)GetTickCount();
    for (auto& e : s_track) {
        if (e.pawn && e.on) WriteGlowSpan(e, e.pawn, false, 0, nowMs);
        e.pawn = 0; e.on = false; e.seen = false; e.wantOn = false;
        e.color = 0; e.spanOk = false;
    }
}

// Slot finden (Adresse) oder freien belegen.
Tracked* AllocSlot(uintptr_t addr) {
    for (auto& e : s_track) if (e.pawn == addr) return &e;
    for (auto& e : s_track) if (e.pawn == 0) return &e;
    return nullptr;
}

} // namespace

void ChamsThread() {
    while (!g_stopThread.load(std::memory_order_relaxed)) {
        const uint32_t nowMs = (uint32_t)GetTickCount();
        const bool want = g_mem.IsValid() && g_client && schemas::m_Glow &&
                          g_cfg.chamsEnable;

        if (!want) {
            // Deaktiviert oder Handle weg: einmal sauber abschalten, dann
            // Stillstand (kein Write-Spam im Toggle-Off-Zustand).
            bool anyTracked = false;
            for (auto& e : s_track) if (e.pawn) { anyTracked = true; break; }
            if (anyTracked) ClearAllTracked();
            std::this_thread::sleep_for(std::chrono::milliseconds(120));
            continue;
        }

        // Diagnose (debugLog): VOR dem Schreiben einen Ziel-Slot read-back —
        // zeigt, welche Felder das Spiel zwischen zwei Zyklen zurücksetzt.
        if (g_cfg.debugLog) {
            static uint32_t s_dbgLast = 0;
            if (nowMs - s_dbgLast > 1000) {
                for (auto& e : s_track) {
                    if (!e.pawn || !e.on) continue;
                    uint8_t chk[0x22] = {};
                    if (g_mem.ReadBuffer(e.pawn + schemas::m_Glow + schemas::m_iGlowType,
                                         chk, sizeof(chk))) {
                        int32_t ty = 0; uint32_t col = 0;
                        memcpy(&ty, chk, sizeof(ty));
                        memcpy(&col, chk + (schemas::m_glowColorOverride - schemas::m_iGlowType), 4);
                        printf("[chams] readback %p type=%d glow=%d elig=%d col=%08X (soll %08X)\n",
                               (void*)e.pawn, ty,
                               (int)chk[schemas::m_bGlowing - schemas::m_iGlowType],
                               (int)chk[schemas::m_bEligibleForScreenHighlight - schemas::m_iGlowType],
                               col, e.color);
                    }
                    s_dbgLast = nowMs;
                    break;
                }
            }
        }

        const SnapshotSet& s = SnapFront();
        const GameCache&   c = s.game;
        const GroundCache& g = s.ground;

        for (auto& e : s_track) { e.seen = false; e.wantOn = false; }

        // ── Soll-Zustand einsammeln (Spieler) ──────────────────────────────
        if (c.valid) {
            const bool teamsKnown = (c.localTeam == 2 || c.localTeam == 3);
            for (int i = 0; i < c.playerCount && i < MAX_PLAYERS; ++i) {
                const PlayerData& p = c.players[i];
                if (!p.alive || !p.pawnAddr || p.pawnAddr == c.localPawnAddr) continue;

                // Ohne Teams: jeder Nicht-Lokale gilt als Feind (besser als
                // gar nichts anzeigen, solange localTeam noch nicht steht).
                const bool enemy = !teamsKnown || (p.team != c.localTeam);
                const bool wantOn = enemy ? true : g_cfg.chamsTeammates;

                Tracked* slot = AllocSlot(p.pawnAddr);
                if (!slot) continue;
                const bool wasTracked = (slot->pawn == p.pawnAddr);
                if (!wantOn && !wasTracked) continue; // Teammate, nie angefasst

                slot->pawn    = p.pawnAddr;
                slot->seen    = true;
                slot->wantOn  = wantOn;
                if (g_cfg.chamsHealthTint)
                    slot->wantCol = PackHealthColor(p.health); // Farbe = Leben
                else
                    slot->wantCol = enemy
                        ? PackColor(p.visible ? g_cfg.chamsColVisible : g_cfg.chamsColHidden)
                        : PackColor(g_cfg.chamsColTeam);
                slot->lastMs  = nowMs;
            }
        }

        // ── Soll-Zustand einsammeln (Boden-Items) ──────────────────────────
        // Gleicher Glow-Trick: Items sind C_BaseModelEntity-Derivate mit
        // m_Glow. Projektile (fliegende Nades) bewusst ausgenommen — nur
        // liegende Objekte (Waffen, C4, Util). g.valid = erster kompletter
        // Sweep durch (verhindert Glow auf halb aufgebautem Cache).
        if (g.valid) {
            for (int i = 0; i < g.count && i < MAX_GROUND_ITEMS; ++i) {
                const GroundItem& it = g.items[i];
                if (it.isProj || !it.entAddr) continue;

                const bool wantOn = g_cfg.chamsItems;
                Tracked* slot = AllocSlot(it.entAddr);
                if (!slot) continue;
                const bool wasTracked = (slot->pawn == it.entAddr);
                if (!wantOn && !wasTracked) continue;

                slot->pawn    = it.entAddr;
                slot->seen    = true;
                slot->wantOn  = wantOn;
                // C4 (type 1) bekommt die eigene Bomben-Farbe, alles andere
                // (Waffen, Util) die Items-Farbe.
                slot->wantCol = PackColor(it.type == 1 ? g_cfg.chamsBombCol
                                                       : g_cfg.chamsItemCol);
                slot->lastMs  = nowMs;
            }
        }

        // ── Soll-Zustand: GEPFLANZTE C4 (Adresse kommt vom Bomb-Modul) ──────
        // Die planted_c4-Entity taucht je Map/Version nicht stabil im Namen-
        // Sweep auf — der Reader loest sie ueber dwPlantedC4 auf und reicht
        // sie hier herein. Farbe = Bomben-Farbe, Toggle = Items.
        if (g.valid && g.plantedC4 && g_cfg.chamsItems && g_cfg.chamsPlantedC4) {
            Tracked* slot = AllocSlot(g.plantedC4);
            if (slot) {
                slot->pawn    = g.plantedC4;
                slot->seen    = true;
                slot->wantOn  = true;
                slot->wantCol = PackColor(g_cfg.chamsBombCol);
                slot->lastMs  = nowMs;
            }
        }

        // ── Write-Bursts ×2 (Mini-Yield dazwischen) ────────────────────────
        int wanted = 0;
        for (int pass = 0; pass < 2; ++pass) {
            for (auto& e : s_track) {
                if (!e.seen || !e.wantOn || !e.pawn) continue;
                if (WriteGlowSpan(e, e.pawn, true, e.wantCol, nowMs)) {
                    e.on    = true;
                    e.color = e.wantCol;
                    if (pass == 0) ++wanted;
                }
            }
            if (pass == 0) std::this_thread::yield();
        }

        // ── Abschalt-/Cleanup-Pass ─────────────────────────────────────────
        // Alles, was NICHT mehr leuchten soll (Toggle aus, Teammate-Option,
        // Tod, Item weg): einmalig abdrehen. Nicht gesehene Slots nach 3 s
        // freigeben (nur wenn der Sweep gültig war — sonst flackern Glows
        // bei Reader-Hiccups).
        for (auto& e : s_track) {
            if (!e.pawn) continue;
            if (e.seen && e.wantOn) continue;
            if (e.on) {
                WriteGlowSpan(e, e.pawn, false, 0, nowMs);
                e.on     = false;
                e.lastMs = nowMs;
            } else if (!e.seen && nowMs - e.lastMs > 3000) {
                e.pawn   = 0;   // Slot freigeben
                e.spanOk = false;
                e.color  = 0;
            }
        }

        // Pacing: mit aktiven Zielen ~1-ms-Takt (Glow bleibt durchgehend an,
        // das Spiel löscht pro Frame); ohne Ziele sparsam.
        std::this_thread::sleep_for(std::chrono::milliseconds(wanted > 0 ? 1 : 33));
    }

    ClearAllTracked(); // sauberer Exit — keine Glow-Leichen im Spiel
}
