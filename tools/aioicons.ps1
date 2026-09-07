# aioicons.ps1 -- AioHUD status-icon tool : DAT/pack <-> PNG <-> the HUD's own icon sheet.
#
# WHY THIS EXISTS. When an icon PACK is installed in XIPivot, AioHUD reads the game's own icon DAT and shows
# that pack -- your game and your HUD match with nothing to do. Without a pack it uses its bundled sheet (the
# client's vanilla status art is soft 32 px from 2002, and the bundle is a crisp redraw of the same statuses).
# This tool is for the two cases neither covers:
#   * you want to SEE / EDIT those icons  -> -Extract writes them as PNGs (640 files, or one contact sheet)
#   * you want your OWN icons in the HUD  -> -Build turns a folder of PNGs (or a sheet, or any icon DAT) into
#     the sheet AioHUD loads BEFORE anything else. No XIPivot needed, and the game's own files are never touched.
#
# The custom sheet lands in  <windower>\plugins\AioHud\icons\status_atlas.raw  and takes priority over the game
# DAT. -Reset deletes it and hands the HUD back to the game's icons. Reload with //unload AioHud + //load AioHud.
#
# USAGE
#   .\aioicons.ps1 -Extract                          # the game's current icons -> .\icons_png\0.png .. 639.png
#   .\aioicons.ps1 -Extract -Sheet -Out sheet.png    # ... as ONE 1024x640 contact sheet instead
#   .\aioicons.ps1 -Extract -Dat "C:\pack\57.DAT"    # ... from a specific DAT rather than the installed one
#   .\aioicons.ps1 -Build -From "C:\mes_icones"      # a folder of <id>.png  -> the HUD uses them
#   .\aioicons.ps1 -Build -From sheet.png            # an edited 1024x640 sheet -> same
#   .\aioicons.ps1 -Build -Dat "C:\pack\57.DAT"      # a pack you have NOT installed in XIPivot -> same
#   .\aioicons.ps1 -Reset                            # back to the game's icons
#
# Icon ids are the game's status ids : 0 = KO, 2 = Sleep, ... 639. A missing id is simply a transparent cell.
# Any PNG size works -- it is scaled to the 32x32 cell the HUD draws.
[CmdletBinding()]
param(
    [switch]$Extract,
    [switch]$Build,
    [switch]$Reset,
    [string]$From,          # -Build source : a folder of <id>.png, a sheet .png, or a .DAT
    [string]$Dat,           # explicit icon DAT (both modes) -- skips the auto-detection below
    [string]$Out,           # -Extract : output folder (or the sheet .png with -Sheet) ; -Build : output .raw
    [switch]$Sheet,         # -Extract : one contact sheet instead of 640 files
    [string]$Windower       # Windower root, when it cannot be deduced from where this script sits
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

# ---- the sheet geometry. It is the DAT's OWN layout : 640 records, cell index == status id, so the HUD atlas
#      was built to match it exactly (32 columns x 20 rows of 32 px). Do not "tidy" these apart. ----
$CELL = 32; $COLS = 32; $COUNT = 640
$ROWS = [int][math]::Ceiling($COUNT / $COLS)
$W = $COLS * $CELL; $H = $ROWS * $CELL

# ---- the icon DAT's record layout (reversed 2026-09-06, docs/game-data/buffs-and-timers/status-icon-dat.md) ----
$REC_STRIDE = 6144      # 0x1800 -- the file is exactly 640 * this
$REC_NAME   = 0x285     # char[16] "sts_iconst00_32 " -- the real magic (a size match alone proves nothing)
$REC_HDR    = 0x295     # a plain BITMAPINFOHEADER : biSize 40, 32, 32, 1 plane, 32 bpp
$REC_PIXELS = 0x2BD     # the pixel body : 32*32 BGRA, BOTTOM-UP like any DIB
$REC_PALETTE = 0x400    # 8-bpp records only : 256 BGRA entries, THEN the indices

function Fail($msg) { Write-Host "[aioicons] $msg" -ForegroundColor Red; exit 1 }
function Say($msg)  { Write-Host "[aioicons] $msg" }

# ---------------------------------------------------------------------------------------------------------
# Locating things
# ---------------------------------------------------------------------------------------------------------

# Windower root : given, or deduced from this script's own home (<windower>\plugins\AioHud\aioicons.ps1).
function Get-WindowerRoot {
    if ($Windower) { return (Resolve-Path $Windower).Path }
    $d = $PSScriptRoot
    for ($i = 0; $i -lt 4 -and $d; $i++) {
        if (Test-Path (Join-Path $d 'plugins')) { return $d }
        $d = Split-Path $d -Parent
    }
    return $null
}

# The FFXI install, from the PlayOnline registry values -- the same six keys the plugin reads.
function Get-FfxiRoot {
    $keys = @('HKLM:\SOFTWARE\WOW6432Node\PlayOnlineEU\InstallFolder', 'HKLM:\SOFTWARE\WOW6432Node\PlayOnlineUS\InstallFolder',
              'HKLM:\SOFTWARE\WOW6432Node\PlayOnline\InstallFolder',   'HKLM:\SOFTWARE\PlayOnlineEU\InstallFolder',
              'HKLM:\SOFTWARE\PlayOnlineUS\InstallFolder',             'HKLM:\SOFTWARE\PlayOnline\InstallFolder')
    foreach ($k in $keys) {
        try { $v = (Get-ItemProperty -Path $k -Name '0001' -ErrorAction Stop).'0001' } catch { continue }
        if ($v -and (Test-Path $v)) { return $v }
    }
    return $null
}

# The icon DAT the GAME is actually showing : each active XIPivot overlay in its configured priority order
# first (that is what makes an installed pack win), then the vanilla ROM. Same order as the plugin.
function Find-IconDat($winRoot) {
    if ($winRoot) {
        $xml = Join-Path $winRoot 'addons\XIPivot\data\settings.xml'
        if (Test-Path $xml) {
            $txt = Get-Content $xml -Raw
            if ($txt -match '(?s)<overlays>(.*?)</overlays>') {
                foreach ($o in ($matches[1] -split ',')) {
                    $o = $o.Trim()
                    if (-not $o) { continue }
                    $p = Join-Path $winRoot "addons\XIPivot\data\DATs\$o\ROM\119\57.DAT"
                    if (Test-Path $p) { return $p }
                }
            }
        }
    }
    $ffxi = Get-FfxiRoot
    if ($ffxi) { $p = Join-Path $ffxi 'ROM\119\57.DAT'; if (Test-Path $p) { return $p } }
    return $null
}

# ---------------------------------------------------------------------------------------------------------
# Reading a sheet (always -> one W*H*4 BGRA byte[], top-down : exactly what the plugin loads)
# ---------------------------------------------------------------------------------------------------------

function Read-DatSheet($path) {
    $d = [System.IO.File]::ReadAllBytes($path)
    if ($d.Length -lt $COUNT * $REC_STRIDE) { Fail "'$path' is only $($d.Length) bytes -- not a 640-icon status DAT." }
    # Validate EVERY record before producing anything : a file of the right size with the wrong content would
    # otherwise convert cleanly into 640 cells of noise.
    for ($k = 0; $k -lt $COUNT; $k++) {
        $b = $k * $REC_STRIDE
        $nm = [System.Text.Encoding]::ASCII.GetString($d, $b + $REC_NAME, 8)
        if ($nm -ne 'sts_icon') { Fail "'$path' is not the status-icon DAT (record $k is '$nm', expected 'sts_icon')." }
        $bpp = [BitConverter]::ToInt16($d, $b + $REC_HDR + 14)
        # 8 bpp is normal, not a corruption : the vanilla sheet has exactly one palettised record
        # ("sts_iconrelim_32", status 506). Rejecting it would throw away the whole vanilla sheet.
        if ([BitConverter]::ToInt32($d, $b + $REC_HDR + 4) -ne 32 -or [BitConverter]::ToInt32($d, $b + $REC_HDR + 8) -ne 32 `
            -or ($bpp -ne 32 -and $bpp -ne 8)) { Fail "'$path' record $k is not a 32x32 icon (32 or 8 bpp)." }
    }
    $out = New-Object byte[] ($W * $H * 4)
    $half = 0
    for ($k = 0; $k -lt $COUNT; $k++) {
        $rec  = $k * $REC_STRIDE
        $pal8 = ([BitConverter]::ToInt16($d, $rec + $REC_HDR + 14) -eq 8)
        $pal  = $rec + $REC_PIXELS                                        # 8-bpp only : 256 BGRA entries
        $src  = if ($pal8) { $rec + $REC_PIXELS + $REC_PALETTE } else { $rec + $REC_PIXELS }
        # Two alpha conventions, asked of each record. The game's own art is 7-bit (0..0x80 = fully opaque)
        # from end to end, art drawn in an image editor is full-range, and a file whose icons have been PARTLY
        # replaced holds both at once. Doubling everything hardens the new art's antialiasing ; doubling nothing
        # draws the originals as ghosts. A record whose alpha never exceeds 0x80 IS a 7-bit record.
        # For a palettised record the convention lives in the PALETTE, so that is what gets scanned and scaled.
        $maxA = 0
        if ($pal8) { for ($i = 3; $i -lt 0x400; $i += 4) { if ($d[$pal + $i] -gt $maxA) { $maxA = $d[$pal + $i] } } }
        else       { for ($i = 3; $i -lt $CELL * $CELL * 4; $i += 4) { if ($d[$src + $i] -gt $maxA) { $maxA = $d[$src + $i] } } }
        $seven = ($maxA -le 0x80)
        if ($seven) { $half++ }
        $cx = ($k % $COLS) * $CELL; $cy = [int][math]::Floor($k / $COLS) * $CELL
        for ($y = 0; $y -lt $CELL; $y++) {
            $o = (($cy + $y) * $W + $cx) * 4
            if ($pal8) {
                $s = $src + ($CELL - 1 - $y) * $CELL                      # one index byte per pixel
                for ($x = 0; $x -lt $CELL; $x++) {
                    $e = $pal + $d[$s + $x] * 4
                    $out[$o + $x*4] = $d[$e]; $out[$o + $x*4 + 1] = $d[$e + 1]; $out[$o + $x*4 + 2] = $d[$e + 2]; $out[$o + $x*4 + 3] = $d[$e + 3]
                }
            } else {
                $s = $src + ($CELL - 1 - $y) * $CELL * 4                  # DIB rows are bottom-up
                [Array]::Copy($d, $s, $out, $o, $CELL * 4)                # BGRA -> BGRA, straight copy
            }
            if ($seven) { for ($x = 3; $x -lt $CELL * 4; $x += 4) { $a = $out[$o + $x] * 2; $out[$o + $x] = [byte]([math]::Min(255, $a)) } }
        }
    }
    Say "read $path  ($($d.Length) bytes, $half icons rescaled from 7-bit alpha)"
    return ,$out          # the comma is load-bearing : without it PowerShell unrolls the byte[] into loose objects
}

# Copy one image straight into the sheet at (dx,dy), w x h. LockBits, NOT Graphics.DrawImage : DrawImage runs
# the pixels through GDI+'s premultiplied-alpha pipeline, which rounds every semi-transparent pixel -- 44 821
# of them, by up to 127, when this was measured against a reference decode of the same pack. Locking the
# source as Format32bppArgb hands back straight-alpha BGRA and the copy is exact. DrawImage is used ONLY when
# the art is not already 32x32 and must genuinely be resampled.
# NOTE the parameter names : PowerShell variables are CASE-INSENSITIVE, so naming these $w/$h silently
# shadowed the sheet's own $W/$H and every cell was written with a 32-pixel row stride -- 640 icons piled
# into the top-left corner. Same trap that made a $sheet byte[] clobber the -Sheet switch.
function Copy-ImageInto($img, $out, $dx, $dy, $cw, $ch) {
    $tmp = $null; $bmp = $img
    if ($img.Width -ne $cw -or $img.Height -ne $ch) {
        $tmp = New-Object -TypeName System.Drawing.Bitmap -ArgumentList @($cw, $ch, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $g = [System.Drawing.Graphics]::FromImage($tmp)
        $g.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceCopy   # copy straight alpha, no blend against the bg
        $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
        $g.DrawImage($img, 0, 0, $cw, $ch)
        $g.Dispose()
        $bmp = $tmp
    }
    $rect = New-Object System.Drawing.Rectangle(0, 0, $cw, $ch)
    $data = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    for ($y = 0; $y -lt $ch; $y++) {                                  # row by row : Stride may exceed cw*4
        [System.Runtime.InteropServices.Marshal]::Copy([IntPtr]($data.Scan0.ToInt64() + $y * $data.Stride), $out, (($dy + $y) * $W + $dx) * 4, $cw * 4)
    }
    $bmp.UnlockBits($data)
    if ($tmp) { $tmp.Dispose() }
}

function Read-FolderSheet($dir) {
    $files = Get-ChildItem -Path $dir -File | Where-Object { $_.BaseName -match '^\d+$' -and $_.Extension -match '^\.(png|bmp|gif|jpg|jpeg)$' }
    if (-not $files) { Fail "no <id>.png in '$dir' -- icons must be named by their status id : 0.png, 1.png, ... 639.png" }
    $script:used = 0; $script:skipped = 0
    $bytes = New-Object byte[] ($W * $H * 4)                          # anything not covered stays transparent
    foreach ($f in $files) {
        $id = [int]$f.BaseName
        if ($id -lt 0 -or $id -ge $COUNT) { $script:skipped++; continue }
        $img = [System.Drawing.Image]::FromFile($f.FullName)
        Copy-ImageInto $img $bytes (($id % $COLS) * $CELL) ([int][math]::Floor($id / $COLS) * $CELL) $CELL $CELL
        $img.Dispose(); $script:used++
    }
    Say "read $script:used icons from $dir$(if ($script:skipped) { " ($script:skipped outside 0..$($COUNT-1), ignored)" })"
    return ,$bytes
}

function Read-ImageSheet($png) {
    $img = [System.Drawing.Image]::FromFile((Resolve-Path $png).Path)
    if ($img.Width -ne $W -or $img.Height -ne $H) { $img.Dispose(); Fail "'$png' is $($img.Width)x$($img.Height) -- a contact sheet must be exactly ${W}x${H} (32 x $ROWS cells of $CELL px)." }
    $bytes = New-Object byte[] ($W * $H * 4)
    Copy-ImageInto $img $bytes 0 0 $W $H
    $img.Dispose()
    Say "read the ${W}x${H} sheet $png"
    return ,$bytes
}

# ---------------------------------------------------------------------------------------------------------
# Writing
# ---------------------------------------------------------------------------------------------------------

# Wrap the sheet bytes in a Bitmap WITHOUT copying (pinned), so cells can be cropped straight out of it.
function Invoke-WithSheetBitmap($bytes, [scriptblock]$body) {
    $pin = [System.Runtime.InteropServices.GCHandle]::Alloc($bytes, [System.Runtime.InteropServices.GCHandleType]::Pinned)
    try {
        # -ArgumentList, not New-Object Type(...) : the shorthand mis-binds this 5-argument ctor and dies on a
        # bogus "Object[] -> UInt32" cast.
        $bmp = New-Object -TypeName System.Drawing.Bitmap -ArgumentList @($W, $H, ($W * 4), [System.Drawing.Imaging.PixelFormat]::Format32bppArgb, $pin.AddrOfPinnedObject())
        try { & $body $bmp } finally { $bmp.Dispose() }
    } finally { $pin.Free() }
}

function Write-Pngs($bytes, $dir) {
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    Invoke-WithSheetBitmap $bytes {
        param($bmp)
        for ($k = 0; $k -lt $COUNT; $k++) {
            $r = New-Object System.Drawing.Rectangle((($k % $COLS) * $CELL), ([int][math]::Floor($k / $COLS) * $CELL), $CELL, $CELL)
            $c = $bmp.Clone($r, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
            $c.Save((Join-Path $dir "$k.png"), [System.Drawing.Imaging.ImageFormat]::Png)
            $c.Dispose()
        }
    }
    Say "OK -> $dir  ($COUNT PNGs, 0.png .. $($COUNT-1).png)"
}

function Write-SheetPng($bytes, $path) {
    $dir = Split-Path $path -Parent
    if ($dir) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
    Invoke-WithSheetBitmap $bytes { param($bmp) $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png) }
    Say "OK -> $path  (${W}x${H} contact sheet, cell = status id)"
}

function Write-Raw($bytes, $path) {
    $dir = Split-Path $path -Parent
    if ($dir) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
    [System.IO.File]::WriteAllBytes($path, $bytes)
    Say "OK -> $path  ($($bytes.Length) bytes)"
}

# ---------------------------------------------------------------------------------------------------------

if (-not ($Extract -or $Build -or $Reset)) {
    Get-Content $PSCommandPath | Select-Object -First 26 | ForEach-Object { $_ -replace '^# ?', '' }
    exit 0
}

$winRoot = Get-WindowerRoot
$customRaw = if ($winRoot) { Join-Path $winRoot 'plugins\AioHud\icons\status_atlas.raw' } else { $null }

if ($Reset) {
    if (-not $customRaw) { Fail "cannot find your Windower folder -- pass -Windower <path to your Windower folder>." }
    if (Test-Path $customRaw) { Remove-Item $customRaw -Force; Say "removed $customRaw -- AioHUD is back on the game's own icons (//unload AioHud then //load AioHud)." }
    else { Say "no custom sheet installed ($customRaw) -- AioHUD is already on the game's own icons." }
    exit 0
}

# Resolve the source, whichever mode we are in.
$srcDat = $Dat
if (-not $srcDat -and $From -and (Test-Path $From -PathType Leaf) -and ([System.IO.Path]::GetExtension($From) -match '^\.dat$')) { $srcDat = $From }
if ($Extract -and -not $srcDat -and -not $From) {
    $srcDat = Find-IconDat $winRoot
    if (-not $srcDat) { Fail "no icon DAT found (no XIPivot pack, no FFXI install in the registry) -- pass -Dat <path to 57.DAT>." }
    Say "using the game's current icons : $srcDat"
}

if ($srcDat)                                              { $px = Read-DatSheet (Resolve-Path $srcDat).Path }
elseif ($From -and (Test-Path $From -PathType Container)) { $px = Read-FolderSheet (Resolve-Path $From).Path }
elseif ($From)                                            { $px = Read-ImageSheet $From }
else { Fail "nothing to convert -- pass -From <folder|sheet.png|pack.DAT> (or -Dat <57.DAT>)." }

if ($Extract) {
    if ($Sheet) { Write-SheetPng $px ($(if ($Out) { $Out } else { 'icons_sheet.png' })) }
    else        { Write-Pngs     $px ($(if ($Out) { $Out } else { 'icons_png' })) }
    exit 0
}

# -Build : install the sheet as the HUD's own, in front of the game DAT.
$dest = $Out
if (-not $dest) {
    if (-not $customRaw) { Fail "cannot find your Windower folder -- pass -Windower <path to your Windower folder>, or -Out <file.raw>." }
    $dest = $customRaw
}
Write-Raw $px $dest
Say "AioHUD will use these icons everywhere (party, player, target, timers, debuffs)."
Say "Reload it in game :  //unload AioHud   then   //load AioHud       (-Reset puts the game's icons back)"
