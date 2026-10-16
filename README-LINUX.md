# OwOEsp unter Linux (X11)

Gleicher Code, gleiche Features wie unter Windows — anderes Backend:
**GLFW + OpenGL3 + X11-Overlay** statt Win32 + D3D11, `process_vm_readv`
statt RPM, XTest statt SendInput, libcurl statt WinHTTP. Das Windows-Projekt
(`OwOEsp.vcxproj`) ist unverändert und baut wie bisher.

## Voraussetzungen

- X11-Session (kein natives Wayland — unter Wayland läuft CS2 via XWayland;
  das Overlay findet das Spiel dann ggf. nicht, siehe Hinweise)
- Compositor mit Transparenz (z. B. **Picom**), sonst ist das Overlay schwarz
- CS2 im **randlosen Fenster** (kein exklusives Fullscreen — darüber kann
  kein Overlay liegen)
- Pakete (Debian/Ubuntu):
  ```
  sudo apt install cmake g++ libglfw3-dev libgl1-mesa-dev \
      libx11-dev libxtst-dev libxext-dev libcurl4-openssl-dev
  ```
- Pakete (**CachyOS / Arch**):
  ```
  sudo pacman -S --needed base-devel cmake glfw mesa \
      libx11 libxtst libxext curl
  ```
  (`base-devel` enthält gcc/make; `glfw` = GLFW 3.4+ mit X11.)
- Fedora: `glfw-devel mesa-libGL-devel libX11-devel libXtst-devel
  libXext-devel libcurl-devel`.

## Bauen

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

Der Release-Build setzt automatisch **-O3**, **-march=native/-mtune=native**
und **LTO** (Interprocedural Optimization) — auf CachyOS/Arch lohnt sich das,
die Binary wird auf deine CPU zugeschnitten. Beides ist abschaltbar, falls du
die Binary auf andere Rechner kopierst:
```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DOWO_NATIVE=OFF -DOWO_LTO=OFF
```
`-ffast-math` wird bewusst **nicht** verwendet — der Code prüft Floats auf
`isfinite`/NaN (ViewMatrix, Geometrie); Fast-Math würde diese Checks brechen.

Ergebnis in `build/`: `OwOEsp`, `OwOEspPreview` (+ kopierte `Gun_textures/`,
`assets/`, `imgui.ini`). Start aus dem Build-Verzeichnis (Config/Log-Dateien
landen neben der Binary, wie unter Windows).

## Starten

1. CS2 starten — der Client sucht **automatisch** erst die native Variante
   (`cs2` / `client.so`), dann **Steam/Proton** (`cs2.exe` / `client.dll`).
   Im Log steht dann z. B. `[mem] Prozess via Fallback gefunden: cs2.exe
   (Proton)`. (Konstanten: `OWO_PROC_NAME` / `OWO_CLIENT_MODULE` + `_ALT` in
   `platform/platform.h`.)
2. `./OwOEsp` (ggf. `chmod +x OwOEsp`).
3. `INSERT` = Menü (wie Windows). Key-Binds, Trigger/Aim-Keys etc. identisch.

## Speicherzugriff / ptrace_scope

Lesen/Schreiben fremder Prozesse braucht gleiche UID (Steam + Cheat als
**gleicher User**, kein sudo nötig). Falls `readv fail ... errno=1 (EPERM)`:
```
echo 0 | sudo tee /proc/sys/kernel/yama/ptrace_scope
```
(Ubuntu-Default `1` blockt sonst. Persistent via sysctl.d.)

## Offsets unter Linux

Der Updater lädt a2x-Dumps und parst sie formatbasiert — das funktioniert
auch unter Linux. **Aber:** Die Standard-URLs liefern Windows-Dumps, deren
Adressen unter Linux **falsch** sind — **Ausnahme:** Läuft CS2 über
**Steam/Proton** (`cs2.exe`/`client.dll`), ist es die Windows-Binary und die
Standard-Dumps passen. Drei Optionen (nur für den nativen Fall nötig):

1. **Eigene Dump-Quelle per Env-Variable** (empfohlen):
   ```
   OWO_OFFSETS_BASE=https://mein.host/cs2/output ./OwOEsp
   ```
   Erwartet dort `offsets.hpp`, `client_dll.hpp`, `buttons.hpp` im
   a2x-Format (Linux-Werte!). Fällt der Download fehl/schlägt der
   Mindestgrößen-Test fehl, versucht der Updater automatisch die
   Standard-Quelle (a2x) als Fallback.
2. **Offset-Cache-Datei**: `offsets_cache.txt` neben die Binary legen,
   Format `name=0x...` pro Zeile (gleiches Format wie unter Windows).
3. **Code-Anpassung**: URLs in `core/OffsetsUpdater.cpp` (`kFiles`)
   auf eine Linux-Dump-Quelle umbiegen.

Der SigScan (`core/SigScan.cpp`) erkennt PE oder ELF am Image-Magic —
nativ (ELF/`client.so`) genauso wie Proton (PE/`client.dll`) — und fixiert
gefundene `dw`-Werte wie unter Windows.

## Unterschiede zu Windows

- Kein DirectComposition-Pfad (`overlayDcomp` wird ignoriert, immer GL-Alpha).
- Kein `MinHook`/DLL-Inject — `inject/`, `tools/Injector.cpp`, `.rc`/Icons
  sind rein Windows (DLL-Inject + D3D11-Hooks via `__try/__except` +
  `SetWindowLongPtr`) und nicht Teil des Linux-Builds. Der Linux-Client ist
  bewusst **extern** (keine Injektion): `process_vm_readv` + X11-Overlay,
  kein Code im Spielprozess.
- Config-Import/Export nutzt `zenity` (falls installiert), sonst CWD-Fallback;
  „Ordner öffnen" nutzt `xdg-open`.
- Config-Dateien (`PixelMommySense.cfg`, Slots) sind binärkompatibel zum
  Windows-Build (bei Auffälligkeiten einfach löschen, wird neu angelegt).
- Debug-Konsole = das Terminal, aus dem du startest (kein separates Fenster).
- Steam-Overlay stört nicht; falls das Overlay flackert: Compositor prüfen.

## Tastatur/Maus

Keycodes sind numerisch identisch zu Win32 (`VK_*` in `platform/platform.h`),
Configs bleiben kompatibel. Maus-Seitentasten = X-Buttons 8/9, Mittelklick
gleich. Simulation via XTest (benötigt keine Extra-Rechte als gleicher User).

## Performance-Design (Linux)

- **Gebündelte Reads:** Der Reader liest Spielerdaten, Fenster-Scans (Boden-
  Items, Util, Chicken/Ball, Bomb-Suche) und den Bomb-Scanner als Batches —
  Linux fasst sie zu **einem `process_vm_readv` pro 512er-Slice** zusammen
  (statt tausender Einzel-Syscalls). Das drückt die Syscall-Last pro Tick um
  Größenordnungen und glättet das Frame-Pacing.
- **Input ohne Roundtrips:** X11-Tastatur/Maus wird als Snapshot mit 2-ms-TTL
  pro Thread gecacht (`core/Input.cpp`) — Triggerbot/Aimbot/Bhop lesen nicht
  mehr pro Prüfung synchron vom X-Server.
- **Frame-Limiter:** absolute Deadline (`sleep_until` + kurzer Spin) statt
  relativem Sleep — kein Drift. Unfokussiert wartet das Overlay per
  `glfwWaitEventsTimeout` auf Events (kein Poll-Wakeup).
- **Signatur-Scan:** liest das Modul-Image in 1-MB-Chunks tolerant ein —
  unmapped Lücken (Guard-Seiten) brechen den Scan nicht mehr ab.

Damit läuft der Reader-Thread auch auf schwächeren CPUs stabil auf den
konfigurierten Hz (60–240, Default siehe Menü → Performance).

