# verify_payload.ps1 -- does dist\ actually contain everything the plugin loads at runtime ?
#
# WHY. The payload is assembled by a robocopy with an exclusion (`/XD *_src`) and a handful of individual
# copies. Every one of those can silently ship less than it should : an exclusion pattern that matches more
# than intended, a copy whose source moved, a file truncated by a disk hiccup. Nothing downstream would say so.
# The DLL still loads, the HUD still draws, and the first person to find out is a player whose status icons or
# gear icons are simply absent -- the exact failure mode rule 10 of CLAUDE.md is about : a silent, permanent
# loss that looks like a feature that never worked.
#
# The check is a COMPARISON, not a list of expected sizes. assets\ in the repo is the source of truth, so every
# file in it (minus the *_src regeneration trees, which are deliberately not shipped) must exist in the payload
# with the same byte count. That way it needs no maintenance when an asset is added, and it catches a dropped
# file of any kind rather than only the ones somebody thought to name.
#
# On top of that, three floors on things whose ABSENCE is survivable but whose emptiness is not -- a zero-byte
# atlas copies "successfully" and leaves every icon blank.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File scripts\verify_payload.ps1 -Root <repo> -Dist <dist>
#
# Exits non-zero on any failure, so package.bat aborts instead of producing a zip nobody can use.
param(
    [Parameter(Mandatory = $true)][string]$Root,
    [Parameter(Mandatory = $true)][string]$Dist
)
$ErrorActionPreference = 'Continue'

$src = Join-Path $Root 'assets'
$dst = Join-Path $Dist 'plugins\AioHud\assets'
$bad = New-Object System.Collections.ArrayList

if (-not (Test-Path -LiteralPath $src -PathType Container)) { Write-Host "[verify] no assets\ in $Root"; exit 1 }
if (-not (Test-Path -LiteralPath $dst -PathType Container)) { Write-Host "[verify] the payload has no assets\ at all"; exit 1 }

# --- 1) every source asset reaches the payload, at the same size ---------------------------------------------
$srcFull = (Get-Item -LiteralPath $src).FullName
$n = 0
foreach ($f in Get-ChildItem -LiteralPath $src -Recurse -File) {
    if ($f.FullName -match '\\[^\\]+_src\\') { continue }       # regeneration sources : never shipped, by design
    $rel = $f.FullName.Substring($srcFull.Length).TrimStart('\')
    $t = Join-Path $dst $rel
    $n++
    if (-not (Test-Path -LiteralPath $t -PathType Leaf)) { [void]$bad.Add("MISSING   assets\$rel"); continue }
    $ti = Get-Item -LiteralPath $t
    if ($ti.Length -eq 0 -and $f.Length -gt 0) { [void]$bad.Add("EMPTY     assets\$rel") }
    elseif ($ti.Length -ne $f.Length) { [void]$bad.Add(("SIZE      assets\{0} : source {1} bytes, payload {2}" -f $rel, $f.Length, $ti.Length)) }
}

# --- 2) floors on what a player notices first ----------------------------------------------------------------
# The atlas is one file : lose it and EVERY status icon in Timers and Debuffs goes blank at once.
$atlas = Join-Path $dst 'buff_atlas.raw'
if (Test-Path -LiteralPath $atlas -PathType Leaf) {
    $len = (Get-Item -LiteralPath $atlas).Length
    if ($len -lt 1048576) { [void]$bad.Add("SMALL     buff_atlas.raw is $len bytes -- a real sheet is megabytes") }
} else { [void]$bad.Add('MISSING   buff_atlas.raw (the status-icon sheet)') }

# Gear icons are 1300+ separate files, which is exactly the shape a partial copy leaves half of.
$gearDir = Join-Path $dst 'gearicons'
$gear = 0
if (Test-Path -LiteralPath $gearDir -PathType Container) { $gear = @(Get-ChildItem -LiteralPath $gearDir -Recurse -File).Count }
$gearSrc = 0
$gearSrcDir = Join-Path $src 'gearicons'
if (Test-Path -LiteralPath $gearSrcDir -PathType Container) { $gearSrc = @(Get-ChildItem -LiteralPath $gearSrcDir -Recurse -File).Count }
if ($gear -ne $gearSrc) { [void]$bad.Add("COUNT     gearicons : $gearSrc in the repo, $gear in the payload") }

# --- 3) the files that are copied one by one, outside the robocopy -------------------------------------------
foreach ($p in @('plugins\AioHud.dll',
                 'plugins\AioHud\design\exports\layout.json',
                 'plugins\AioHud\aioicons.ps1',
                 'plugins\AioHud\NOTICE-EmpyPop.txt',
                 'addons\aioupdate\aioupdate.lua')) {
    $t = Join-Path $Dist $p
    if (-not (Test-Path -LiteralPath $t -PathType Leaf)) { [void]$bad.Add("MISSING   $p") }
    elseif ((Get-Item -LiteralPath $t).Length -eq 0) { [void]$bad.Add("EMPTY     $p") }
}

# --- verdict --------------------------------------------------------------------------------------------------
if ($bad.Count -eq 0) {
    Write-Host ("[verify] payload OK : {0} asset file(s), {1} gear icon(s), atlas {2:N1} MB" -f $n, $gear, ((Get-Item -LiteralPath $atlas).Length / 1MB))
    exit 0
}
Write-Host "[verify] PAYLOAD INCOMPLETE -- $($bad.Count) problem(s) :"
foreach ($b in $bad) { Write-Host "   $b" }
Write-Host "[verify] this zip would install a broken AioHud -- not packaging it"
exit 1
