# patch_buff_icon.ps1 -- overwrite a SINGLE cell of the buff atlas from its source PNG,
# leaving every other cell untouched (unlike gen_buff_atlas.ps1 which rebuilds the whole atlas).
#
#   powershell -File scripts/patch_buff_icon.ps1 -Id 616
#
# Source : addons/XivParty/assets/buffIcons/<Id>.png   (32x32)
# Target : assets/buff_atlas.raw  (1024 x 640 straight-alpha BGRA ; cell = 32x32, 32 cols)
#          cell <Id> -> col = Id%32, row = Id/32 -> atlas px (col*32, row*32).

#          XivParty is found under $env:WINDOWER_ADDONS, else the dev install (the scripts/genpaths.py order).
# !! The shipped atlas is the AioPack sheet since c6fb52e, not XivParty's : a patched cell will not match its neighbours.

param(
    [Parameter(Mandatory=$true)][int]$Id,
    [string]$Src = (Join-Path $(if ($env:WINDOWER_ADDONS) { $env:WINDOWER_ADDONS } else { 'D:\Windower Tetsouo\addons' }) ('XivParty\assets\buffIcons\{0}.png' -f $Id)),
    [string]$Atlas = (Join-Path (Split-Path $PSScriptRoot -Parent) 'assets\buff_atlas.raw')
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$W = 1024; $H = 640; $CELL = 32; $COLS = 32
if (-not (Test-Path $Src))   { throw "source PNG not found: $Src" }
if (-not (Test-Path $Atlas)) { throw "atlas not found: $Atlas" }

# --- source PNG -> 32x32 BGRA bytes (SourceCopy keeps straight alpha verbatim) ---
$cellBmp = New-Object System.Drawing.Bitmap($CELL, $CELL, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g = [System.Drawing.Graphics]::FromImage($cellBmp)
$g.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceCopy
$img = [System.Drawing.Image]::FromFile($Src)
$g.DrawImage($img, 0, 0, $CELL, $CELL)
$img.Dispose(); $g.Dispose()
$rect = New-Object System.Drawing.Rectangle(0, 0, $CELL, $CELL)
$data = $cellBmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$cellBytes = New-Object byte[] ($CELL * $CELL * 4)
[System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $cellBytes, 0, $cellBytes.Length)
$cellBmp.UnlockBits($data); $cellBmp.Dispose()

# --- splice the cell into the atlas at (col*32, row*32) ---
$col = $Id % $COLS
$row = [math]::Floor($Id / $COLS)
$px = $col * $CELL; $py = $row * $CELL
if (($px + $CELL) -gt $W -or ($py + $CELL) -gt $H) { throw "cell $Id (px $px,$py) is outside the $W x $H atlas." }

$bytes = [System.IO.File]::ReadAllBytes($Atlas)
if ($bytes.Length -ne ($W * $H * 4)) { throw "atlas size $($bytes.Length) != expected $($W * $H * 4)." }
$stride = $W * 4
for ($y = 0; $y -lt $CELL; $y++) {
    $dst = (($py + $y) * $stride) + ($px * 4)
    [System.Array]::Copy($cellBytes, $y * $CELL * 4, $bytes, $dst, $CELL * 4)
}
[System.IO.File]::WriteAllBytes($Atlas, $bytes)
Write-Host "[patch] cell $Id -> col $col row $row (px $px,$py) written into $Atlas"
