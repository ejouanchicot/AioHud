# gen_buff_atlas.ps1 -- build the native buff-icon atlas from XivParty's status PNGs.
#
# Source : addons/XivParty/assets/buffIcons/<id>.png  (642 icons, ids 0..639, 32x32 each)
# Output : assets/buff_atlas.raw  -- a straight-alpha BGRA blob, ready for load_raw_texture().
#
# Layout : a fixed 32-column grid, cell 32x32. A status id maps to cell
#          (col = id % 32, row = id / 32) -> atlas px (col*32, row*32).
#          Atlas size = 1024 x (ceil((maxId+1)/32) * 32).  Empty cells stay transparent.
#
# Re-run whenever XivParty's icon set changes. Pure asset tooling -- no game/RE involved.

Add-Type -AssemblyName System.Drawing

$root    = Split-Path $PSScriptRoot -Parent
$srcDir  = Join-Path $root '..\..\addons\XivParty\assets\buffIcons'
$outPath = Join-Path $root 'assets\buff_atlas.raw'

$CELL = 32
$COLS = 32

# discover the icon set (integer-named PNGs only) and the id range
$files = Get-ChildItem -Path $srcDir -Filter '*.png' | Where-Object { $_.BaseName -match '^\d+$' }
$maxId = ($files | ForEach-Object { [int]$_.BaseName } | Measure-Object -Maximum).Maximum
$rows  = [math]::Ceiling(($maxId + 1) / $COLS)
$W = $COLS * $CELL
$H = $rows * $CELL

Write-Host "[atlas] $($files.Count) icons, ids 0..$maxId -> $W x $H ($COLS x $rows cells of $CELL px)"

$atlas = New-Object System.Drawing.Bitmap($W, $H, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g = [System.Drawing.Graphics]::FromImage($atlas)
# SourceCopy : copy source pixels (incl. straight alpha) verbatim, no blend against the bg
$g.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceCopy

foreach ($f in $files) {
    $id  = [int]$f.BaseName
    $col = $id % $COLS
    $row = [math]::Floor($id / $COLS)
    $img = [System.Drawing.Image]::FromFile($f.FullName)
    $g.DrawImage($img, ($col * $CELL), ($row * $CELL), $CELL, $CELL)
    $img.Dispose()
}
$g.Dispose()

# LockBits as 32bppArgb -> in-memory byte order is B,G,R,A (little-endian ARGB) = exactly
# the straight-alpha BGRA blob load_raw_texture() expects. Copy the bytes out and write them.
$rect = New-Object System.Drawing.Rectangle(0, 0, $W, $H)
$data = $atlas.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$bytes = New-Object byte[] ($data.Stride * $H)
[System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $bytes, 0, $bytes.Length)
$atlas.UnlockBits($data)
$atlas.Dispose()

[System.IO.File]::WriteAllBytes($outPath, $bytes)
Write-Host "[atlas] OK -> $outPath  ($($bytes.Length) bytes)  | W=$W H=$H CELL=$CELL COLS=$COLS"
