# ─────────────────────────────────────────────────────────────────────────────
# tools/gen_icon.ps1 — erzeugt resources/app.ico aus assets/logo.png
# Nach Aenderung von logo.png erneut ausfuehren und neu bauen.
# ─────────────────────────────────────────────────────────────────────────────
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$root  = Split-Path -Parent $PSScriptRoot
$src   = Join-Path $root 'assets\logo.png'
$out   = Join-Path $root 'resources\app.ico'

$srcBmp = [System.Drawing.Bitmap]::FromFile($src)
$sizes  = @(256, 128, 64, 48, 32, 16)
$frames = @()  # jedes Element: [byte[]] PNG

foreach ($s in $sizes) {
  $bmp = New-Object System.Drawing.Bitmap($s, $s)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
  $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
  $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
  $g.Clear([System.Drawing.Color]::Transparent)
  $g.DrawImage($srcBmp, 0, 0, $s, $s)
  $g.Dispose()
  $ms = New-Object System.IO.MemoryStream
  $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
  $frames += , $ms.ToArray()
  $ms.Dispose()
  $bmp.Dispose()
}

# ICO-Datei bauen (PNG-komprimierte Frames, 256er muss PNG sein)
$fs = [System.IO.File]::Create($out)
$bw = New-Object System.IO.BinaryWriter($fs)
$bw.Write([UInt16]0); $bw.Write([UInt16]1); $bw.Write([UInt16]$frames.Count)
$offset = 6 + 16 * $frames.Count
for ($i = 0; $i -lt $frames.Count; $i++) {
  $s = $sizes[$i]
  $bw.Write([Byte]($(if ($s -ge 256) { 0 } else { $s })))
  $bw.Write([Byte]($(if ($s -ge 256) { 0 } else { $s })))
  $bw.Write([Byte]0)                       # Farben
  $bw.Write([Byte]0)                       # reserviert
  $bw.Write([UInt16]1)                     # Planes
  $bw.Write([UInt16]32)                    # BPP
  $bw.Write([UInt32]$frames[$i].Length)    # Frame-Groesse
  $bw.Write([UInt32]$offset)               # Offset
  $offset += $frames[$i].Length
}
foreach ($f in $frames) { $bw.Write($f) }
$bw.Flush(); $bw.Dispose()
$srcBmp.Dispose()
Write-Host ("generiert: {0} ({1} Groessen)" -f $out, $sizes.Count)