# ─────────────────────────────────────────────────────────────────────────────
# tools/gen_icons.ps1 — erzeugt resources/ItemIcons.rc aus Gun_textures\*.svg
# Nach dem Hinzufuegen neuer Waffen-SVGs erneut ausfuehren und neu bauen.
# ─────────────────────────────────────────────────────────────────────────────
$ErrorActionPreference = 'Stop'
$root  = Split-Path -Parent $PSScriptRoot
$src   = Join-Path $root 'Gun_textures'
$out   = Join-Path $root 'resources\ItemIcons.rc'

$files = Get-ChildItem -Path $src -Filter '*.svg' | Sort-Object Name
if (-not $files) { Write-Error 'Keine SVG-Dateien in Gun_textures gefunden.' }

$lines = @(
  '// AUTO-GENERIERT von tools/gen_icons.ps1 — nicht von Hand editieren.',
  '#include <windows.h>',
  ''
)
foreach ($f in $files) {
  $res = $f.Name
  # Doppelte Backslashes: rc.exe interpretiert \a \t \n als Escape-Sequenzen
  $rel = '..\\Gun_textures\\' + $res
  $lines += '"' + $res + '" RCDATA "' + $rel + '"'
}

$outDir = Split-Path -Parent $out
if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Path $outDir | Out-Null }
$lines | Set-Content -Path $out -Encoding Ascii
Write-Host ("generiert: {0} ({1} Icons)" -f $out, $files.Count)