#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// core/Config.h — alle Einstellungen des Cheats (eine Quelle der Wahrheit)
// Wird roh per fwrite/fread gespeichert → neue Felder NUR am Ende anhaengen,
// sonst brechen alte Config-Dateien.
// ─────────────────────────────────────────────────────────────────────────────
#include "platform/platform.h"

struct Config {
    // ── Visuals ──────────────────────────────────────────────────────────────
    bool  enabled         = true;
    bool  box             = true;
    bool  health          = false;
    bool  name            = false;
    bool  visibleOnly     = false;
    bool  snapLines       = false;

    bool  testEsp         = false;
    bool  bombTimer       = true;
    int   bombTimerHAlign = 1;
    int   bombTimerVAlign = 0;

    float enemyColor[4]   = { 1.f, 0.3f, 0.3f, 1.f };
    float teamColor[4]    = { 0.3f, 1.f, 0.5f, 1.f };

    float thickness       = 1.5f;

    bool  crosshairEnable    = true;
    bool  crosshairDot       = false;
    float crosshairSize      = 8.f;
    float crosshairThickness = 1.0f;
    float crosshairGap       = 2.f;
    float crosshairColor[4]  = { 0.39f, 0.9f, 0.39f, 1.f };

    bool  glow       = false;
    float glowSpread = 6.f;
    float glowAlpha  = 0.25f;

    bool  rainbow      = false;
    float rainbowSpeed = 0.3f;
    float rainbowSat   = 1.f;
    float rainbowVal   = 1.f;

    // ── Triggerbot ───────────────────────────────────────────────────────────
    bool  triggerbotEnable    = false;
    bool  triggerbotCheckTeam = true;
    bool  triggerbotCheckVis  = false;
    int   triggerbotKey       = VK_XBUTTON1;
    int   triggerbotDelay     = 30;
    float triggerbotRecoil    = 0.f;

    // ── Aim Assist ───────────────────────────────────────────────────────────
    bool  aimEnable = false;
    int   aimKey    = VK_XBUTTON2;
    float aimSmooth = 5.f;
    float aimFov    = 300.f;
    float aimHeightStand   = 64.f;
    float aimHeightCrouch  = 40.f;

    // ── Skin Changer ─────────────────────────────────────────────────────────
    bool   skinChanger    = true;
    int    skinKnifeModel = 500;
    int    skinPaintKit   = 416;
    int    skinSeed       = 0;
    float  skinWear       = 0.01f;
    int    skinStatTrak   = -1;

    // ── Recoil Control ───────────────────────────────────────────────────────
    bool   recoilEnable      = false;
    bool   recoilActive      = false;
    float  recoilStrength    = 100.f;
    float  recoilSensitivity = 2.5f;
    float  recoilSmooth      = 1.f;
    float  recoilXScale     = 1.0f;
    float  recoilYScale     = 1.0f;
    bool   recoilShowIndicator = true;
    int    recoilMode        = 0;
    int    recoilModeShots   = 5;
    int    recoilMaxShots    = 0;

    // ── HUD / ESP-Optionen ───────────────────────────────────────────────────
    bool   showWeapon        = true;

    bool   itemEsp           = false;
    bool   itemEspBomb       = true;
    bool   itemEspWeapons    = true;
    bool   itemEspUtil       = true;

    float  nameOffsetX       = 0.f;
    float  nameOffsetY       = -15.f;
    float  healthBarOffsetX  = -7.f;
    float  healthBarOffsetY  = 0.f;
    float  weaponIconOffsetX = 0.f;
    float  weaponIconOffsetY = 2.f;
    float  distanceOffsetX   = 0.f;
    float  distanceOffsetY   = -30.f;

    int targetFps = 0; // 0 = unlimited (niedrigste Latenz)

    // ── Entfernte Features (Ragebot/Spinbot/SkinChanger) — Felder bleiben
    //    fuer Config-Layout-Stabilitaet (Raw-Save). Unbenutzt im Code. ─────────
    bool  rageEnable     = false;
    int   rageKey        = 'C';
    float rageFov        = 180.f;
    bool  rageWallbang   = false;
    bool  rageAutoFire   = true;
    int   rageInterval   = 100; // ms between shots
    float rageHeadStand  = 64.f;
    float rageHeadCrouch = 48.f;

    // ── Bhop (perfect auto-jump) ─────────────────────────────────────────────
    bool  bhopEnable     = false;

    // ── Triggerbot Fadenkreuz-Radius (px) ────────────────────────────────────
    float triggerbotPx   = 20.f;

    // ── Entfernte Features — Felder bleiben fuer Config-Layout ──────────────
    bool  rageSpinEnable = false;
    float rageSpinSpeed  = 25.f;
    int   rageHitbox     = 0;
    int   injectMethod       = 0;
    bool  injectEraseHeaders = true;

    // ── Aim-Speed-Multiplikator (NUR HIER anhaengen — Raw-Config-Save) ─────
    float aimSpeed = 1.0f;

    // ── Spotted ESP (NEU — am Ende anhaengen) ─────────────────────────────
    bool  spottedEnable      = false; // Master: Spotted-Unterscheidung aktiv
    bool  spottedVisibleOnly = false; // nur Sichtbare anzeigen (hard filter)
    bool  spottedText        = true;  // SPOTTED/HIDDEN Text ueber Box
    bool  spottedColorMode   = true;  // Farbe nach Sichtbarkeit
    float spottedColor[4]    = { 0.20f, 0.90f, 0.20f, 1.f }; // sichtbar = gruen
    float hiddenColor[4]     = { 0.90f, 0.30f, 0.30f, 1.f }; // verdeckt = rot
    int   spottedMode        = 2;     // 0=Farbe, 1=Text, 2=Beides

    // ── ESP Style & Performance ───────────────────────────────────────────
    int   espBoxStyle        = 0;     // 0=Corner,1=Full,2=Filled
    bool  espHeadDot         = false;
    bool  espDistance        = false;
    float espBoxRounding     = 0.f;
    float espFilledAlpha     = 0.12f;
    bool  espOptimize        = true;  // Batching + Culling + Cache
    bool  espShadow          = true;  // Outline/Schatten fuer Lesbarkeit

    // ── Radar (auf echtem CS2-Radar) ──────────────────────────────────────
    bool  radarEnable        = false;
    bool  radarUseRealPos    = true;  // am echten Radar andocken (oben links)
    float radarPosX          = 22.f;
    float radarPosY          = 62.f;
    float radarSize          = 185.f;
    float radarZoom          = 0.30f; // Welt->Radar Skalierung
    float radarAlpha         = 0.88f;
    bool  radarShowEnemies   = true;
    bool  radarShowTeam      = false;
    bool  radarShowViewCone  = true;
    int   radarShape         = 0;     // 0=Rund,1=Eckig
    bool  radarBorder        = true;

    // ── Performance (NEU — am Ende anhaengen) ─────────────────────────────
    int   readerHz           = 144;   // Memory-Reader Frequenz (Hz, 144 reicht)

    // ── Skeleton / Throw Preview (NEU — am Ende anhaengen) ────────────────
    bool  skeleton           = false;
    float skeletonThickness  = 1.5f;
    float skeletonColor[4]   = { 1.f, 1.f, 1.f, 0.95f };
    bool  throwPreview       = false;
    float throwPreviewColor[4] = { 0.35f, 0.85f, 1.f, 0.9f };

    // ── Menü-Look (Glutenium-Style — NUR am Ende anhängen) ─────────────────
    int   menuTheme          = 0;     // 0=Dark, 1=Translucent, 2=Crimson, 3=Midnight
    float menuAccent[4]      = { 0.898f, 0.224f, 0.208f, 1.f }; // #E53935
    float menuScale          = 1.0f;

    // ── Chicken / Ball Aimbot (NEU — am Ende anhaengen, Raw-Config-Save) ────
    bool  chickenAimEnable = false;
    int   chickenAimKey    = VK_XBUTTON2;
    float chickenAimSmooth = 5.f;
    float chickenAimSpeed  = 1.f;
    float chickenAimFov    = 300.f;
    float chickenAimHeight = 10.f;
    bool  ballAimEnable    = false;
    int   ballAimKey       = VK_XBUTTON2;
    float ballAimSmooth    = 5.f;
    float ballAimSpeed     = 1.f;
    float ballAimFov       = 300.f;
    float ballAimHeight    = 4.f;

    // ── Coordinate Saver (NEU — am Ende anhaengen, Raw-Config-Save) ─────────
    bool  coordSaverEnable  = false;
    int   coordSaverKey     = VK_F8;  // Flanke: speichert anvisiertes Objekt
    float coordSaverRadius  = 25.f;   // max. Abstand zum Fadenkreuz (px)
    bool  coordSaverMarkers = true;   // gespeicherte Punkte im ESP zeigen
    bool  coordSaverClear   = false;  // Schalter: Liste + Datei leeren

    // ── Coordinate Saver Strahl-Fallback (NEU — am Ende anhaengen) ──────────
    bool  coordSaverRayFallback = true;   // kein Objekt: Punkt auf Blickstrahl
    float coordSaverRayDist     = 1000.f; // Distanz dafuer (Units)

    // ── Ball-Filter + Marker Aim (NEU — am Ende anhaengen, Raw-Config-Save) ─
    float ballMaxSize      = 32.f;  // max. Prop-Box (Units) für Ball Aim
    bool  markerAimEnable  = false; // zielt auf Coordinate-Saver-Punkte
    int   markerAimKey     = VK_XBUTTON2;
    float markerAimSmooth  = 5.f;
    float markerAimSpeed   = 1.f;
    float markerAimFov     = 300.f;

    // ── UC Seed-Triggerbot (NEU — NUR am Ende anhaengen, Raw-Config-Save) ───
    // Modus: 0=Legacy Pixel-Radius, 1=Crosshair-ID (UC Simple External),
    //        2=Seed-Hybrid (Crosshair-ID + AccuracyPenalty-Gate, empfohlen)
    int   triggerbotMode   = 2;
    bool  triggerbotSeedGate = true;  // Spread-Gate: nur schiessen wenn Waffe genau
    float triggerbotMaxPenalty = 0.35f; // max. m_fAccuracyPenalty (0.1..2.0)
    bool  triggerbotScopeCheck = true;  // Sniper nur scoped
    bool  triggerbotBlockBadWeapons = true; // keine Nades/C4/Messer/Zeus
    int   triggerbotJitter = 15;      // zusaetzlicher Zufalls-Delay 0..Jitter (ms)
    int   triggerbotBurstGap = 120;   // Mindestabstand zwischen Schuessen (ms)
    int   triggerbotLatchMs = 90;     // Crosshair-Latch fuer Flicks (ms)
    bool  triggerbotShowDebug = false;

    // ── Chams-Style ESP (NEU — NUR am Ende anhaengen, Raw-Config-Save) ─────
    bool  chamsStyle = false;   // flache Chams-Füllung statt Corner/Full/Filled
    float chamsAlpha = 0.35f;   // Füll-Alpha im Chams-Stil (0.05..0.8)

    // ── Aim Humanize (NEU — NUR am Ende anhaengen, Raw-Config-Save) ─────────
    bool  aimCheckVis = true;   // Assist nur auf Sichtbare (kein Wall-Lock)
    float aimHumanize = 2.5f;   // Zielpunkt-Jitter (px, 0=aus)
    float aimOvershoot = 10.f;  // Flick-Überschiessen bei Zielwechsel (px)

    // ── Overlay-Rendering (NEU — NUR am Ende anhaengen, Raw-Config-Save) ───
    bool  overlayVsync = true;  // Monitor-Sync (Default an: FPS-schonend, kein Tearing)

    // ── Util Radius (NEU — NUR am Ende anhaengen, Raw-Config-Save) ─────────
    bool  utilRadiusEnable = false; // Smoke-/Feuer-Sperrflächen am Boden
    bool  utilRadiusSmoke  = true;
    bool  utilRadiusFire   = true;

    // ── Throw-Preview Bodenkalibrierung (NEU — Ende anhaengen) ──────────────
    // Extern gibt es kein BSP: Bodenebene = eigene Füße + Offset. Ziel liegt
    // tiefer/höher? Offset anpassen (z.B. -150 für Wurf nach unten).
    float throwGroundOffset = 0.f; // -400..+400u

    // ── Overlay/Profiler (NEU — NUR am Ende anhaengen, Raw-Config-Save) ────
    bool  overlayDcomp = false; // DirectComposition+Flip statt Layered+Colorkey (Beta, braucht Neustart)
    bool  showProfiler = false; // Leistungs-Overlay: FPS/Frame/CPU/DrawCalls/RAM

    // ── Diagnose (NEU — Ende anhaengen) ─────────────────────────────────────
    // Schalter für Konsolen-Debug (spotted/punch/bomb/ball/census/throw).
    // AUS lassen: volles Console-Scrollback bremst den Reader-Thread!
    bool  debugLog = false;

    // ── Recoil-Quelle (NEU — NUR am Ende anhaengen, Raw-Config-Save) ───────
    // 0=Muster-Tabelle (Legacy), 1=Punch-Live (Seed-Stil, folgt echtem Kick)
    int   recoilSource = 1;

    // ── Smoke-Check + Spectator-Liste (NEU — Ende anhaengen) ────────────────
    bool  aimSmokeCheck = true;      // Assist: keine Locks durch Smoke
    bool  triggerSmokeCheck = true;  // Trigger: kein Feuer durch Smoke
    bool  specListEnable = true;     // Zuschauer-Panel (wer schaut zu)

    // ── Fadenkreuz-Stil (NEU — NUR am Ende anhaengen, Raw-Config-Save) ──────
    // 0=Simple, 1=Punkt, 2=Kreis, 3=Kreuz+Punkt, 4=Stern, 5=X, 6=Swastika
    int   crosshairStyle = 0;
    bool  crosshairRotate = false;   // Fadenkreuz rotiert dauerhaft
    float crosshairRotateSpeed = 90.f; // Grad/Sekunde (-360..360)

    // ── Chams (Glow) (NEU — NUR am Ende anhaengen, Raw-Config-Save) ─────────
    // UC-Standard extern: Glow-Properties pro Pawn schreiben (Outline durch
    // Wände). Farbe je Sichtbarkeit (Spotted-Mask). ⚠ Schreibzugriff (wie
    // Bhop) — höchstes Erkennungsrisiko, nur bewusst nutzen.
    bool  chamsEnable    = false; // Master (Menü: Visuals → Chams (Glow))
    bool  chamsTeammates = false; // auch Teammates outline'n
    float chamsColVisible[4] = { 0.30f, 1.00f, 0.45f, 1.f }; // Feind sichtbar
    float chamsColHidden [4] = { 1.00f, 0.30f, 0.40f, 1.f }; // Feind verdeckt
    float chamsColTeam   [4] = { 0.35f, 0.70f, 1.00f, 1.f }; // Teammates

    // ── Chams für Items (NEU — NUR am Ende anhaengen, Raw-Config-Save) ──────
    // Gleiche Glow-Chams für Boden-Items (Waffen, C4, Util) — Items sind
    // C_BaseModelEntity-Derivate mit eigener CGlowProperty.
    bool  chamsItems = true;
    float chamsItemCol[4] = { 1.00f, 0.62f, 0.12f, 1.f }; // orange

    // ── Arrow ESP (NEU — NUR am Ende anhaengen, Raw-Config-Save) ────────────
    // Pfeile auf einem Ring um das Fadenkreuz Richtung Gegner (UC-Standard
    // „Offscreen-Indicator"). Farbe = Spotted/Hidden-Farben des ESP.
    bool  arrowEsp              = false;
    bool  arrowEspOffscreenOnly = true;  // nur Gegner außerhalb des Bildschirms
    bool  arrowEspTeam          = false; // auch Teammates
    float arrowEspRadius        = 90.f;  // 30..300 px Abstand vom Zentrum
    float arrowEspSize          = 9.f;   // 4..30 px Pfeilgröße

    // ── Ganzkarten-Radar (NEU — NUR am Ende anhaengen, Raw-Config-Save) ─────
    // Echte CS2-Karte als Bild (radar/<map>.png + maps.json neben der EXE,
    // Quelle: boltobserv-Projekt — siehe radar/README.txt). Fällt automatisch
    // auf den lokalen Radar zurück, wenn Bild/Meta fehlen.
    bool  radarFull       = true;   // Ganzkarte statt lokalem Radar
    bool  radarFullRotate = false;  // mit Blickrichtung drehen (Spieler zentriert)
    bool  radarFullBomb   = true;   // C4 auf der Karte markieren
    float radarFullZoom   = 1.0f;   // 1 = ganze Karte, >1 = reinzoomen (0.4..6)

    // ── Triggerbot Anti-Miss (NEU — NUR am Ende anhaengen, Raw-Config-Save) ─
    // UC-Fund: CS2 verzoegert die Crosshair-ID (m_delayTargetIDTimer) und
    // haelt sie nach dem Verlassen des Ziels weiter gueltig
    // (m_holdTargetIDTimer) — blind auf die ID feuern = Schuss ins Leere.
    bool  triggerbotRayCheck = true;  // Knochen-Ray: nur feuern wenn das
                                      // Fadenkreuz wirklich auf dem Modell ist
    int   triggerbotMaxSpeed = 60;    // Stand-Gate: max. Speed in u/s beim
                                      // Feuern (0 = aus; vorher hart 160)

    // ── Chams: Bomben-Farbe + Health-Tint (NEU — NUR am Ende anhaengen) ─────
    // Bombe (C4, GroundItem.type == 1) bekommt eine EIGENE Glow-Farbe; die
    // Items-Farbe gilt nur noch fuer Waffen/Util. Health-Tint faerbt den
    // Spieler-Glow nach HP (gruen = voll, rot = fast tot) statt der
    // Sichtbarkeits-Farben — Toggle im Chams (Glow)-Modul.
    float chamsBombCol[4] = { 1.00f, 0.15f, 0.15f, 1.f }; // Bombe (C4)
    bool  chamsHealthTint = false; // Spieler-Glow-Farbe folgt dem Leben

    // ── Chams: GEPFLANZTE C4 (NEU — NUR am Ende anhaengen) ──────────────────
    // Gepflanzte Bombe taucht je Map/Version nicht stabil im Namen-Sweep auf;
    // Flag: Reader reicht die dwPlantedC4-Adresse an den Chams-Thread weiter.
    bool  chamsPlantedC4 = true;

    // ── Smooth Aim (NEU — NUR am Ende anhaengen, Raw-Config-Save) ───────────
    // Eigener Maus-Input wird zur weichen Fade-Bewegung geglaettet: echte
    // Deltas aus Raw Input, Korrektur per SendInput — Gesamtstrecke bleibt
    // exakt erhalten (kein Aim-Drift).
    bool  smoothAimEnable   = false;
    float smoothAimSoftness = 88.f; // 60..97 % — hoeher = weicher (laengerer Fade)
};

// Config speichern/laden (rohes Struct → PixelMommySense.cfg)
void SaveConfig();
void LoadConfig();

// ── Config-Slots (configs/*.cfg neben der EXE) ──────────────────────────────
#include <string>
#include <vector>
void ListConfigs(std::vector<std::string>& out); // Namen ohne .cfg, sortiert
bool SaveConfigAs(const char* name);   // sanitized -> configs/<name>.cfg
bool LoadConfigByName(const char* name);
bool DeleteConfig(const char* name);
bool ImportConfig();                   // Datei-Dialog -> kopiert nach configs/
bool ExportConfig(const char* name);   // configs/<name>.cfg -> Datei-Dialog
void OpenConfigsFolder();
