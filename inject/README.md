# OwOEsp Inject v1 — Test-UI

Zweite Version neben der externen EXE (`OwOEsp.exe`): eine **interne DLL**,
die per Injector in `cs2.exe` geladen wird und dort per `Present`-Hook eine
ImGui-Test-UI rendert. Keine Feature-Logik — nur Hook + Render + Input als Basis.

## Dateien

| Datei | Zweck |
|---|---|
| `OwOEsp/inject/DllMain.cpp` | DllMain, Present/ResizeBuffers per direktem VTable-Patch (kein MinHook, keine Extra-DLL), ImGui-Init, Tasten, File-Logging |
| `OwOEsp/inject/TestUI.h/.cpp` | Test-Fenster (Slider, Combo, Farbe, Demo-Toggle, Unload-Button) |
| `OwOEsp/OwOEspInject.vcxproj` | DLL-Projekt (x64, `MinHook.x64.lib`, ImGui aus eigenem Tree) |
| `OwOEsp/tools/Injector.cpp` | Minimaler LoadLibrary-Injector (Konsole) |
| `OwOEsp/OwOEspInjector.vcxproj` | Injector-Projekt (x64) |

## Bauen

In Visual Studio die Solution `OwOEsp.slnx` öffnen — enthält jetzt 3 Projekte:

- `OwOEspInject` (DLL)
- `OwOEspInjector` (Konsole)

Nur **x64 / Release** bauen. Output landet in `x64/Release/`:

- `OwOEspInject.dll` (nur System-DLLs + MSVC-Runtime als Abhängigkeit)
- `OwOEspInjector.exe`

## Diagnose

- DLL-Log: `%TEMP%\OwOEspInject.log` — jeder Schritt (Thread-Start, Dummy-Device,
  VTable-Adressen, Hooks AKTIV, Present-Calls, ImGui-Init, Heartbeat) steht drin.
- Injector-Log: Konsole bleibt offen — zeigt PID, Remote-Ergebnis, Modul-Check,
  Pfad zur Logdatei.
- Ablauf zum Testen:
  1. Falls die alte DLL noch in CS2 steckt: im Spiel `END` drücken (Unload) oder
     CS2 neu starten — sonst ist die Datei gesperrt und der Build schlägt fehl.
  2. CS2 starten, Injector als Admin starten.
  3. `%TEMP%\OwOEspInject.log` öffnen und prüfen, was dort steht.

## Nutzung

1. CS2 starten (Hauptmenü reicht, kein Bot-Match nötig).
2. `OwOEspInjector.exe` als Admin starten (sucht `cs2.exe` + `OwOEspInject.dll` neben sich).
   Alternativ: `OwOEspInjector.exe cs2.exe C:\pfad\zu\OwOEspInject.dll`
   Oder einen Injector deiner Wahl nutzen.
3. Im Spiel erscheint `OwOEsp Inject — Test UI`.

## Tasten

- `INSERT` — Menü an/aus
- `END` (oder Unload-Button) — DLL sauber entladen (Hooks runter, ImGui weg, `FreeLibraryAndExitThread`)

## Eject ohne Neustart

- `OwOEspInjector.exe unload [cs2.exe]` — sendet ein Eject-Signal an die
  geladene DLL und wartet auf Bestätigung (kein CS2-Neustart nötig).
- **Auto-Eject:** Die DLL entlädt sich selbst, wenn etwas schiefgeht —
  Hook-Installation fehlgeschlagen, ImGui-Init nach 50 Versuchen tot, oder
  25s lang kein einziges `Present` (falscher Renderer/Pfad). Jeder Fehlerpfad
  endet mit sauberem Unload statt die Datei zu blockieren — danach einfach neu
  injecten, ohne CS2 zu schließen.

## Chams (Glow)

- `inject/Chams.h/.cpp` — Entity-Glow nach UC-Standardmethode: pro Pawn
  `m_Glow + m_glowColorOverride / m_iGlowType / m_bGlowing` beschreiben
  (direkte Pointer, ~30Hz, SEH-geschützt, Clear-Pass beim Abschalten).
- UI: eigenes Fenster „OwOEsp Inject — Chams" (Gegner/Mates, Farben,
  Glow-Typ, Diagnose: Offsets OK + aktive Pawns).
- Offsets (`dwEntityList`, `m_Glow` etc.) stehen oben in `Chams.cpp` —
  Quelle `cs2-dumped/` im Repo. Nach CS2-Update dort abgleichen (gleiche
  Quelle wie der externe Offsets-Updater).
- `inject/ChamsMaterials.h` — VMat-Buffer (Latex/Glass/Fresnel, UC-Sammlung)
  als Daten + Anleitung für echte Material-Chams (DrawObject-Hook in
  scenesystem + IMaterialSystem2 — noch nicht verdrahtet, im UI als Stub
  markiert).
- **Nur in `-insecure` testen** (Bot-Match). Interne Speicher-Schreibzugriffe
  auf VAC-Servern sind Bann-Risiko — kein Matchmaking mit injizierter DLL.

## Wie es funktioniert (kurz)

1. `DllMain` (PROCESS_ATTACH) startet `MainThread` (Loader-Lock sicher).
2. Dummy-D3D11-Device → `Present` (VTable 8) + `ResizeBuffers` (VTable 13) Adressen.
3. `MH_CreateHook` + `MH_EnableHook` auf beide.
4. Beim ersten echten `Present`: Device/Context aus SwapChain, Game-HWND,
   `ImGui_ImplWin32/DX11_Init`, WndProc-Hook, RenderTarget.
5. Jedes `Present`: `NewFrame → DrawInjectTestUI → Render → RenderDrawData`,
   dann originales `Present` aufrufen.

## Nächste Schritte (noch nicht drin)

- Feature-Code aus `features/` intern verdrahten (direkte Pointer statt RPM).
- Echte Menü-UI (`ui/Menu`) statt Test-UI wiederverwenden.
- VAC-/Sichtbarkeits-Hinweis: interne DLLs sind riskanter als das externe Overlay —
  nur zum Testen/Lernen, nicht auf Main-Accounts.
