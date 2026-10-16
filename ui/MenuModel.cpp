#include "pch.h"
#include "MenuModel.h"
#include "../core/Globals.h"

// ─────────────────────────────────────────────────────────────────────────────
// ui/MenuModel.cpp — die komplette Feature-Tabelle
// Reihenfolge der Kategorien = Sidebar-Reihenfolge. Jede Einstellung der
// alten Tabs ist hier gemappt; nichts wurde inhaltlich geändert.
// ─────────────────────────────────────────────────────────────────────────────

namespace ui {
namespace {

std::vector<CategoryDef> g_model;

const char* kBoxStyles[]   = { "Corner", "Full", "Filled" };
const char* kRadarShapes[] = { "Rund", "Eckig" };
const char* kHAligns[]     = { "Links", "Mitte", "Rechts" };
const char* kVAligns[]     = { "Oben", "Mitte", "Unten" };
const char* kTriggerModes[] = { "Legacy Pixel", "Crosshair UC", "Seed Hybrid" };
const char* kCrossStyles[] = { "Simple", "Punkt", "Kreis", "Kreuz+Punkt", "Stern", "X", "Swastika" };

SettingDef Bool (const char* l, bool* v) {
    SettingDef s; s.type = SettingType::Bool;  s.label = l; s.ptr = v; return s;
}
SettingDef Flt  (const char* l, float* v, float mn, float mx, const char* fmt) {
    SettingDef s; s.type = SettingType::Float; s.label = l; s.ptr = v;
    s.mn = mn; s.mx = mx; s.fmt = fmt; return s;
}
SettingDef Int  (const char* l, int* v, int mn, int mx, const char* fmt) {
    SettingDef s; s.type = SettingType::Int;   s.label = l; s.ptr = v;
    s.mn = (float)mn; s.mx = (float)mx; s.fmt = fmt; return s;
}
SettingDef Cho  (const char* l, int* v, const char* const* opts, int n) {
    SettingDef s; s.type = SettingType::Choice; s.label = l; s.ptr = v;
    s.options = opts; s.optCount = n; return s;
}
SettingDef Col  (const char* l, float* v) {
    SettingDef s; s.type = SettingType::Color; s.label = l; s.ptr = v; return s;
}
SettingDef Key  (const char* l, int* v, int target) {
    SettingDef s; s.type = SettingType::Key;   s.label = l; s.ptr = v;
    s.keyTarget = target; return s;
}

} // namespace

const std::vector<CategoryDef>& MenuModel() { return g_model; }

void RegisterMenuModel() {
    if (!g_model.empty()) return;

    // ── Visuals ──────────────────────────────────────────────────────────────
    CategoryDef visuals; visuals.name = "Visuals"; visuals.icon = "A";
    {
        ModuleDef esp; esp.name = "ESP"; esp.enabled = &g_cfg.enabled;
        esp.desc = "Boxen, Health, Namen, Snaplines, Head Dot, Distanz";
        esp.settings = {
            Bool("Box",        &g_cfg.box),
            Bool("Health",     &g_cfg.health),
            Bool("Namen",      &g_cfg.name),
            Bool("Snap Lines", &g_cfg.snapLines),
            Bool("Waffe",      &g_cfg.showWeapon),
            Bool("Head Dot",   &g_cfg.espHeadDot),
            Bool("Distanz",    &g_cfg.espDistance),
            Bool("Optimiert",  &g_cfg.espOptimize),
            Bool("Schatten",   &g_cfg.espShadow),
            Cho ("Box Stil",   &g_cfg.espBoxStyle, kBoxStyles, 3),
            Bool("Chams-Stil",  &g_cfg.chamsStyle),
            Flt ("Chams Alpha", &g_cfg.chamsAlpha, 0.05f, 0.8f, "%.2f"),
            Flt ("Box Dicke",  &g_cfg.thickness, 0.5f, 5.f, "%.1f px"),
            Flt ("Fill Alpha", &g_cfg.espFilledAlpha, 0.05f, 0.5f, "%.2f"),
            Flt ("Rounding",   &g_cfg.espBoxRounding, 0.f, 8.f, "%.1f"),
            Col ("Feind",      g_cfg.enemyColor),
            Col ("Team",       g_cfg.teamColor),
        };
        visuals.modules.push_back(esp);

        ModuleDef sk; sk.name = "Skeleton"; sk.enabled = &g_cfg.skeleton;
        sk.desc = "17 Bones: Kopf, Wirbelsaeule, Arme, Beine";
        sk.settings = {
            Flt("Dicke", &g_cfg.skeletonThickness, 0.5f, 4.f, "%.1f px"),
            Col("Farbe", g_cfg.skeletonColor),
        };
        visuals.modules.push_back(sk);

        ModuleDef tp; tp.name = "Throw Preview"; tp.enabled = &g_cfg.throwPreview;
        tp.desc = "UC-Formel: Pitch-Korrektur, Speed 1090, Vel*1.25, Grav 800";
        tp.settings = {
            Col("Farbe", g_cfg.throwPreviewColor),
            Flt("Boden-Offset", &g_cfg.throwGroundOffset, -400.f, 400.f, "%.0f u"),
        };
        visuals.modules.push_back(tp);

        // Chams nach UC-Standard: Outline-Glow pro Pawn (durch Waende).
        // Farbe je Sichtbarkeit — verdeckte/sichtbare Feinde unterschiedlich.
        ModuleDef ch; ch.name = "Chams (Glow)"; ch.enabled = &g_cfg.chamsEnable;
        ch.desc = "UC-Standard: Outline durch Waende; Farben je Sichtbarkeit/HP";
        ch.settings = {
            Bool("Teammates##ch",  &g_cfg.chamsTeammates),
            Bool("Items##ch",      &g_cfg.chamsItems),
            Bool("Gepflanzte C4##ch", &g_cfg.chamsPlantedC4),
            Bool("Health-Farbe##ch", &g_cfg.chamsHealthTint),
            Col ("Feind sichtbar##ch", g_cfg.chamsColVisible),
            Col ("Feind verdeckt##ch", g_cfg.chamsColHidden),
            Col ("Team##ch",           g_cfg.chamsColTeam),
            Col ("Items##chc",         g_cfg.chamsItemCol),
            Col ("Bombe##chc",         g_cfg.chamsBombCol),
        };
        visuals.modules.push_back(ch);

        // Arrow ESP (UC-Standard „Offscreen-Indicator"): Pfeile am Ring um
        // das Fadenkreuz Richtung Gegner, Farbe nach Sichtbarkeit.
        ModuleDef aw; aw.name = "Arrow ESP"; aw.enabled = &g_cfg.arrowEsp;
        aw.desc = "Pfeile ums Fadenkreuz Richtung Gegner (Offscreen-Indicator)";
        aw.settings = {
            Bool("Nur ausserhalb##aw", &g_cfg.arrowEspOffscreenOnly),
            Bool("Teammates##aw",      &g_cfg.arrowEspTeam),
            Flt ("Radius",             &g_cfg.arrowEspRadius, 30.f, 300.f, "%.0f px"),
            Flt ("Groesse",            &g_cfg.arrowEspSize, 4.f, 30.f, "%.0f px"),
            Col ("Sichtbar##aw",       g_cfg.spottedColor),
            Col ("Verdeckt##aw",       g_cfg.hiddenColor),
        };
        visuals.modules.push_back(aw);

        ModuleDef rd; rd.name = "Radar"; rd.enabled = &g_cfg.radarEnable;
        rd.desc = "Ganzkarte (CS2-Map-Bild) oder lokaler Radar, C4 + Pfeile";
        rd.settings = {
            Bool("Ganzkarte##rd",        &g_cfg.radarFull),
            Bool("Ganzkarte drehen##rd", &g_cfg.radarFullRotate),
            Bool("C4 auf Karte##rd",     &g_cfg.radarFullBomb),
            Flt ("Karten-Zoom##rd",      &g_cfg.radarFullZoom, 0.4f, 4.f, "%.2f"),
            Bool("Auf echtem Radar", &g_cfg.radarUseRealPos),
            Bool("Enemies",          &g_cfg.radarShowEnemies),
            Bool("Teammates",        &g_cfg.radarShowTeam),
            Bool("ViewCone",         &g_cfg.radarShowViewCone),
            Bool("Border",           &g_cfg.radarBorder),
            Cho ("Form",             &g_cfg.radarShape, kRadarShapes, 2),
            Flt ("Size",             &g_cfg.radarSize, 100.f, 300.f, "%.0f px"),
            Flt ("Zoom",             &g_cfg.radarZoom, 0.1f, 1.f, "%.2f"),
            Flt ("Alpha",            &g_cfg.radarAlpha, 0.3f, 1.f, "%.2f"),
            Flt ("X Pos",            &g_cfg.radarPosX, 0.f, 600.f, "%.0f"),
            Flt ("Y Pos",            &g_cfg.radarPosY, 0.f, 400.f, "%.0f"),
        };
        visuals.modules.push_back(rd);

        ModuleDef ur; ur.name = "Util Radius"; ur.enabled = &g_cfg.utilRadiusEnable;
        ur.desc = "Smoke-Kreis + Molo-Flammen am Boden (Sperrflächen)";
        ur.settings = {
            Bool("Smokes", &g_cfg.utilRadiusSmoke),
            Bool("Feuer",  &g_cfg.utilRadiusFire),
        };
        visuals.modules.push_back(ur);

        ModuleDef spec; spec.name = "Spectator Liste"; spec.enabled = &g_cfg.specListEnable;
        spec.desc = "Zeigt nur, wer MIR zuschaut (Observer-Ziel = du)";
        spec.settings = {
        };
        visuals.modules.push_back(spec);
    }

    // ── Combat ───────────────────────────────────────────────────────────────
    CategoryDef combat; combat.name = "Combat"; combat.icon = "B";
    {
        ModuleDef tb; tb.name = "Triggerbot"; tb.enabled = &g_cfg.triggerbotEnable;
        tb.desc = "Crosshair-ID + Seed-Gate; Ray-Check + Stand-Gate gegen CS2 ID-Delay/Hold";
        tb.settings = {
            Cho ("Modus",             &g_cfg.triggerbotMode, kTriggerModes, 3),
            Bool("Team Check", &g_cfg.triggerbotCheckTeam),
            Bool("Wand-Check", &g_cfg.triggerbotCheckVis),
            Bool("Smoke-Check (kein Feuer in Smoke)", &g_cfg.triggerSmokeCheck),
            Bool("Seed-Gate (Spread)", &g_cfg.triggerbotSeedGate),
            Flt ("Max Penalty", &g_cfg.triggerbotMaxPenalty, 0.05f, 2.f, "%.2f"),
            Bool("Scope-Check (Sniper)", &g_cfg.triggerbotScopeCheck),
            Bool("Block Nade/Messer", &g_cfg.triggerbotBlockBadWeapons),
            Flt ("Fadenkreuz-Radius", &g_cfg.triggerbotPx, 2.f, 60.f, "%.0f px"),
            Int ("Verzögerung",       &g_cfg.triggerbotDelay, 10, 500, "%d ms"),
            Int ("Jitter",            &g_cfg.triggerbotJitter, 0, 200, "%d ms"),
            Int ("Burst-Gap",         &g_cfg.triggerbotBurstGap, 0, 500, "%d ms"),
            Int ("Latch (Flicks)",    &g_cfg.triggerbotLatchMs, 0, 300, "%d ms"),
            Bool("Ray-Check (ID-Hold-Fix)", &g_cfg.triggerbotRayCheck),
            Int ("Max Speed",         &g_cfg.triggerbotMaxSpeed, 0, 400, "%d u/s (0=aus)"),
            Flt ("Recoil Kontrolle",  &g_cfg.triggerbotRecoil, 0.f, 20.f, "%.1f px"),
            Bool("Debug-Log", &g_cfg.triggerbotShowDebug),
            Key ("Taste",             &g_cfg.triggerbotKey, 1),
        };
        combat.modules.push_back(tb);

        ModuleDef aim; aim.name = "Aim Assist"; aim.enabled = &g_cfg.aimEnable;
        aim.desc = "Human: Jitter + Overshoot, nur Sichtbare";
        aim.settings = {
            Bool("Vis-Check (kein Wall-Lock)", &g_cfg.aimCheckVis),
            Bool("Smoke-Check (kein Lock in Smoke)", &g_cfg.aimSmokeCheck),
            Flt("Smoothing",       &g_cfg.aimSmooth, 1.f, 20.f, "%.1f"),
            Flt("Speed",           &g_cfg.aimSpeed, 0.5f, 5.f, "%.1fx"),
            Flt("Humanize (Jitter)", &g_cfg.aimHumanize, 0.f, 15.f, "%.1f px"),
            Flt("Overshoot",       &g_cfg.aimOvershoot, 0.f, 60.f, "%.0f px"),
            Flt("FOV",             &g_cfg.aimFov, 30.f, 600.f, "%.0f px"),
            Flt("Zielhöhe Stehen", &g_cfg.aimHeightStand, 0.f, 100.f, "%.1f"),
            Flt("Zielhöhe Ducken", &g_cfg.aimHeightCrouch, 0.f, 100.f, "%.1f"),
            Key("Taste",           &g_cfg.aimKey, 2),
        };
        combat.modules.push_back(aim);

        ModuleDef chicken; chicken.name = "Chicken Aim"; chicken.enabled = &g_cfg.chickenAimEnable;
        chicken.desc = "Wie Aim Assist, zielt aber auf Huehner";
        chicken.settings = {
            Flt("Smoothing", &g_cfg.chickenAimSmooth, 1.f, 20.f, "%.1f"),
            Flt("Speed",     &g_cfg.chickenAimSpeed, 0.5f, 5.f, "%.1fx"),
            Flt("FOV",       &g_cfg.chickenAimFov, 30.f, 600.f, "%.0f px"),
            Flt("Zielhöhe",  &g_cfg.chickenAimHeight, 0.f, 60.f, "%.1f"),
            Key("Taste",     &g_cfg.chickenAimKey, 3),
        };
        combat.modules.push_back(chicken);

        ModuleDef sm; sm.name = "Smooth Aim"; sm.enabled = &g_cfg.smoothAimEnable;
        sm.desc = "Eigener Maus-Input als weiche Fade-Animation (Gesamtstrecke bleibt exakt — kein Drift)";
        sm.settings = {
            Flt("Weichheit", &g_cfg.smoothAimSoftness, 60.f, 97.f, "%.0f%%"),
        };
        combat.modules.push_back(sm);

    }

    // ── Movement ─────────────────────────────────────────────────────────────
    CategoryDef movement; movement.name = "Movement"; movement.icon = "C";
    {
        ModuleDef bh; bh.name = "Bhop"; bh.enabled = &g_cfg.bhopEnable;
        bh.desc = "Space halten — huepft, sobald du am Boden bist";
        bh.settings = {};
        movement.modules.push_back(bh);
    }

    // ── UI ───────────────────────────────────────────────────────────────────
    CategoryDef uiCat; uiCat.name = "UI"; uiCat.icon = "D";
    {
        ModuleDef ch; ch.name = "Fadenkreuz"; ch.enabled = &g_cfg.crosshairEnable;
        ch.desc = "7 Stile: Simple, Punkt, Kreis, Kreuz+Punkt, Stern, X, Swastika";
        ch.settings = {
            Cho ("Stil",      &g_cfg.crosshairStyle, kCrossStyles, 7),
            Bool("Punkt",   &g_cfg.crosshairDot),
            Bool("Rotieren", &g_cfg.crosshairRotate),
            Flt ("Rot-Speed", &g_cfg.crosshairRotateSpeed, -360.f, 360.f, "%.0f deg/s"),
            Flt ("Größe",   &g_cfg.crosshairSize, 2.f, 30.f, "%.0f"),
            Flt ("Dicke",   &g_cfg.crosshairThickness, 0.5f, 5.f, "%.1f"),
            Flt ("Abstand", &g_cfg.crosshairGap, 0.f, 20.f, "%.0f"),
            Col ("Farbe",   g_cfg.crosshairColor),
        };
        uiCat.modules.push_back(ch);

        ModuleDef bt; bt.name = "Bomb Timer"; bt.enabled = &g_cfg.bombTimer;
        bt.desc = "C4-Zeit, Position frei waehlbar";
        bt.settings = {
            Cho("Horizontal", &g_cfg.bombTimerHAlign, kHAligns, 3),
            Cho("Vertikal",   &g_cfg.bombTimerVAlign, kVAligns, 3),
        };
        uiCat.modules.push_back(bt);

    }

    g_model.push_back(visuals);
    g_model.push_back(combat);
    g_model.push_back(movement);
    g_model.push_back(uiCat);
}

void SearchSettings(const char* query, std::vector<SearchHit>& out) {
    out.clear();
    if (!query || !query[0]) return;
    const std::vector<CategoryDef>& model = MenuModel();
    for (int c = 0; c < (int)model.size(); ++c) {
        for (int m = 0; m < (int)model[c].modules.size(); ++m) {
            // Modul-Name + Setting-Label grob matchen (alle Buchstaben muessen
            // in "Modul: Label" vorkommen, wie die alte MatchQuery).
            char combined[256];
            snprintf(combined, sizeof(combined), "%s %s", model[c].modules[m].name, "");
            for (int s = 0; s < (int)model[c].modules[m].settings.size(); ++s) {
                snprintf(combined, sizeof(combined), "%s %s",
                         model[c].modules[m].name,
                         model[c].modules[m].settings[s].label);
                bool ok = true;
                for (const char* q = query; *q; ++q) {
                    char qc = (char)tolower((unsigned char)*q);
                    if (qc == ' ') continue;
                    bool found = false;
                    for (const char* l = combined; *l; ++l) {
                        if (tolower((unsigned char)*l) == qc) { found = true; break; }
                    }
                    if (!found) { ok = false; break; }
                }
                if (ok) out.push_back({ c, m, s });
            }
        }
    }
}

}  // namespace ui
