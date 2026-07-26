# gen_flow.ps1 -- triple the fiole flow frames by linear interpolation.
# For each adjacent pair (i, i+1) of the 32 seamless-loop frames, emit the
# original plus 2 in-between frames (pixel-wise A*(1-f)+B*f over all 4 channels)
# -> 96 frames FlowX_00..95. i+1 wraps 31->0 (seamless loop). Output to a
# dedicated folder so the addon assets stay untouched.
#
# HISTORICAL ART TOOL -- nothing in the plugin loads these frames today (the fiole liquid is drawn
# procedurally by ui/liquid_bars.cpp). Its 96 OUTPUT frames survive as research\art_src\FlowX_00..95.png ;
# its 32 INPUT frames (Flow0_00..31.png) lived in an old Windower addon folder and are NOT in this repo,
# so the script cannot run as-is. Kept because it documents how the 96 were produced. Point -SrcDir at a
# folder holding Flow0_00..31.png to run it again.
# Paths resolve from THIS script's location (they were hardcoded to machine-absolute dead paths).
param(
    [string]$SrcDir,
    [string]$DstDir
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$root = Split-Path -Parent $PSScriptRoot
if (-not $SrcDir) { $SrcDir = Join-Path $root 'research\art_src\fiole_src' }
if (-not $DstDir) { $DstDir = Join-Path $root 'research\art_src' }
$src = (Join-Path $SrcDir '')
$dst = (Join-Path $DstDir '')
if (-not (Test-Path (Join-Path $SrcDir 'Flow0_00.png'))) {
    throw "gen_flow: the 32 source frames Flow0_00..31.png are not in this repo (looked in $SrcDir). The 96 frames this script produced are kept in research\art_src\FlowX_*.png ; pass -SrcDir to regenerate from a folder that has the originals."
}
if (-not (Test-Path $dst)) { New-Item -ItemType Directory -Path $dst | Out-Null }

$N = 32; $STEPS = 3        # NB: PowerShell vars are case-insensitive -> don't name the loop counter $m
$W = 256; $H = 26
$argb = [System.Drawing.Imaging.PixelFormat]::Format32bppArgb
$png  = [System.Drawing.Imaging.ImageFormat]::Png
$rect = New-Object System.Drawing.Rectangle 0, 0, $W, $H

# load all 32 frames into raw BGRA byte arrays
$srcs = New-Object 'System.Collections.Generic.List[byte[]]'
for ($i = 0; $i -lt $N; $i++) {
    $bm0 = New-Object System.Drawing.Bitmap @(($src + ('Flow0_{0:00}.png' -f $i)))
    $bm  = New-Object System.Drawing.Bitmap @($W, $H, $argb)
    $g = [System.Drawing.Graphics]::FromImage($bm); $g.DrawImage($bm0, 0, 0, $W, $H); $g.Dispose(); $bm0.Dispose()
    $d = $bm.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, $argb)
    $b = New-Object byte[] ($W * $H * 4)
    [System.Runtime.InteropServices.Marshal]::Copy($d.Scan0, $b, 0, $b.Length)
    $bm.UnlockBits($d); $bm.Dispose()
    $srcs.Add($b)
}
"loaded $($srcs.Count) source frames"

$out = 0
for ($i = 0; $i -lt $N; $i++) {
    $a = $srcs[$i]; $bn = $srcs[($i + 1) % $N]
    for ($s = 0; $s -lt $STEPS; $s++) {
        $ob = New-Object byte[] ($W * $H * 4)
        if ($s -eq 0) {
            [Array]::Copy($a, $ob, $a.Length)
        } else {
            $f = $s / [double]$STEPS
            for ($p = 0; $p -lt $ob.Length; $p++) {
                $ob[$p] = [byte][int][math]::Round($a[$p] * (1 - $f) + $bn[$p] * $f)
            }
        }
        $obm = New-Object System.Drawing.Bitmap @($W, $H, $argb)
        $od = $obm.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::WriteOnly, $argb)
        [System.Runtime.InteropServices.Marshal]::Copy($ob, 0, $od.Scan0, $ob.Length)
        $obm.UnlockBits($od)
        $obm.Save(($dst + ('FlowX_{0:00}.png' -f $out)), $png)
        $obm.Dispose()
        $out++
    }
}
"generated $out frames -> $dst"
