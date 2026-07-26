# conv_caps.ps1 -- convert the cap PNGs (front/back) to raw BGRA .bin the plugin
# can load via CreateTexture + LockRect (D3DFMT_A8R8G8B8 = BGRA in memory, straight
# alpha). Downscaled to TWxTH keeping the shared 643x1096 canvas so front & back
# stay aligned.
#
# Paths resolve from THIS script's location so they follow the repo, not the machine (they were hardcoded
# to the old runtime folder `plugins\_aiohud_re`, renamed AND split from the dev repo -- unrunnable since).
# Defaults regenerate the SHIPPED assets\cap_{front,back}.bin that ui/liquid_bars.cpp loads ; pass -DstDir
# to write elsewhere (e.g. to diff against the shipped pair before overwriting it).
param(
    [string]$SrcDir,
    [string]$DstDir
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$root = Split-Path -Parent $PSScriptRoot
if (-not $SrcDir) { $SrcDir = Join-Path $root 'research\art_src\out3_capuchon' }
if (-not $DstDir) { $DstDir = Join-Path $root 'assets' }
$src = (Join-Path $SrcDir '')
$dst = (Join-Path $DstDir '')
if (-not (Test-Path $SrcDir)) { throw "conv_caps: source PNGs not found : $SrcDir" }
if (-not (Test-Path $dst)) { New-Item -ItemType Directory -Path $dst | Out-Null }
$TW = 164; $TH = 280
$argb = [System.Drawing.Imaging.PixelFormat]::Format32bppArgb
$rect = New-Object System.Drawing.Rectangle 0, 0, $TW, $TH

function conv($inName, $outName) {
    $bm0 = New-Object System.Drawing.Bitmap (($src + $inName))
    $bm  = New-Object System.Drawing.Bitmap @($TW, $TH, $argb)
    $g = [System.Drawing.Graphics]::FromImage($bm)
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.DrawImage($bm0, 0, 0, $TW, $TH); $g.Dispose(); $bm0.Dispose()
    $d = $bm.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, $argb)
    $b = New-Object byte[] ($TW * $TH * 4)
    [System.Runtime.InteropServices.Marshal]::Copy($d.Scan0, $b, 0, $b.Length)
    $bm.UnlockBits($d); $bm.Dispose()
    [System.IO.File]::WriteAllBytes(($dst + $outName), $b)
    "$outName : $($b.Length) bytes ($TW x $TH)"
}

conv 'Capuchon_0001_Calque-1.png' 'cap_front.bin'
conv 'Capuchon_0000_Calque-2.png' 'cap_back.bin'
