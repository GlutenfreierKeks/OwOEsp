#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// core/OffsetsUpdater.h — automatischer Offset-Abgleich mit dem offiziellen
// cs2-dumper (a2x/cs2-dumper auf GitHub).
//
// Die hartkodierten Werte in features/Offsets.h sind Fallbacks. Beim Start
// (und per Menue-Button) werden die Dump-Dateien heruntergeladen,
// geparst und in "offsets_cache.txt" neben der EXE gespeichert. Beim naechsten
// Start werden die gecachten Werte sofort verwendet (offline-faehig).
// LIVE-UPDATE: Alle Offsets werden ueber Register() an echte Variablen
// gebunden und nach jedem Download sofort live ueberschrieben — kein Neustart
// noetig. StartAutoRefresh() laeuft nach wie vor beim Start (jetzt mit Auto-
// Apply), RefreshNow() ist synchron/blockierend.
// ─────────────────────────────────────────────────────────────────────────────
#include <cstdint>

namespace offsetsupdater {
    // Liefert den aktuellen Wert fuer `name` (Cache oder Fallback).
    uintptr_t Value(const char* name, uintptr_t fallback);

    // Registriert `out` als Live-Ziel fuer `name`. Setzt *out sofort auf
    // den gecachten/geladenen Wert (oder Fallback) und sorgt dafuer, dass
    // kuenftige Downloads *out automatisch aktualisieren.
    void Register(const char* name, uintptr_t fallback, uintptr_t* out);

    // Uebertraegt alle gecachten/geladenen Werte auf die registrierten Ziele.
    void ApplyLiveValues();

    // Startet den Hintergrund-Refresh (einmal beim Programmstart).
    void StartAutoRefresh();

    // Blockiert bis der Download fertig ist. true = neue Offsets aktiv.
    bool RefreshNow();

    // UI-Status: 0=idle, 1=laueft, 2=ok, 3=fehler
    int  Status();
    // Anzahl erfolgreich uebernommener Werte (fuer UI-Anzeige)
    int  AppliedCount();
    // Datum des Dump-Stands (YYYY-MM-DD oder "?") — Diagnose bei CS2-Updates
    const char* DumpDate();
    // Fixiert einen Offset (Signatur-Scan): Updater überschreibt ihn nicht mehr
    void LockValue(const char* name, uintptr_t value);
}