#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// core/SigScan.h — Update-resistente dw-Offsets per Signatur-Scan.
//
// a2x/cs2-dumper findet seine dw-Offsets per Pattern-Scan in client.dll —
// dieselben Muster scannt der Client beim Start selbst (einmalig, .text).
// Gefundene Werte werden per LockValue() fixiert (Updater fasst sie nicht
// mehr an). Fehlschlag -> Updater-/Fallback-Wert bleibt (graceful).
// Schemas (m_*) sind relativ und überleben Updates meist — bleiben beim Updater.
// ─────────────────────────────────────────────────────────────────────────────
void SigScanOffsets();

// Nachscan der Fehlstellen (30s/90s/150s): ViewMatrix ist im Menü noch Null
// und trifft erst mit geladener Map. Bereits fixierte bleiben unangetastet.
void SigScanRetryDelayed();

// ViewMatrix-Jäger: findet die Matrix per Bewegungstest (Maus), wenn Pattern
// und Dump versagen. Lockt NUR bei eindeutiger Bewegung (sicherer Default).
void ViewMatrixHunter();
