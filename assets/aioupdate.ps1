# AioHud self-updater. Launched by the plugin with CreateProcess + CREATE_NO_WINDOW (no console window at all),
# and driven together with the AioUpdate Lua addon (which does the //unload + //load the plugin can't do itself).
#
# Two modes:
#   -CheckOnly : just query the latest release and write <Data>\update\check.txt (no download, no reload) :
#       UPTODATE <ver>   / AVAILABLE <ver>   / ERROR <msg>       (the plugin reads this for the Update tab)
#   (default)  : full update, writing phases to <Data>\update\done.txt (the Lua addon polls it) :
#       UPTODATE <ver>   already on the latest release -> nothing to do
#       READY <ver>      newer release downloaded -> the addon //unloads AioHud so the DLL can be replaced
#       OK <ver>         extracted the new build over the Windower root (plugins\ + addons\) -> the addon //loads AioHud
#       ERROR <msg>      something went wrong -> the addon reloads the current build
param([string]$Current = '0', [string]$Repo = 'ejouanchicot/AioHud', [string]$Plugins, [string]$Data, [switch]$CheckOnly)
$ErrorActionPreference = 'Stop'
$updir = Join-Path $Data 'update'
$done  = Join-Path $updir 'done.txt'
$check = Join-Path $updir 'check.txt'
# PER-PROCESS download name. This was a fixed 'update.zip', and in dual-box EVERY client spawns its own updater :
# both downloaded to the SAME path, so the second one died with "cannot access the file ... used by another
# process" and the Update tab showed that as a failed update -- even though the first updater had succeeded.
$zip   = Join-Path (Join-Path $Data 'cache') "update_$PID.zip"
function Write1($path, $s) { New-Item -ItemType Directory -Force -Path $updir | Out-Null; Set-Content -LiteralPath $path -Value $s -Encoding ascii }
function Status($s) { Write1 $done  $s }
function Check($s)  { Write1 $check $s }
# Version comparison must be an ORDER, not an equality. This was `$tag -eq $Current`, so ANY value that wasn't
# the remote tag counted as "behind" : a locally built DLL (Current = 'dev') was offered an update on every
# single load, and one click OVERWROTE the dev build with the published release. Same trap for a tester left on
# a pre-release. Rule now : only ever move FORWARD, and never touch a build whose version isn't a real number.
function Newer($remote, $local) {
    $rx = '^\d+(\.\d+){1,3}$'
    if ($local  -notmatch $rx) { return $false }   # 'dev' / '0' / garbage = a local build -> leave it alone
    if ($remote -notmatch $rx) { return $false }   # unparsable tag -> do nothing rather than guess
    return ([version]$remote -gt [version]$local)
}
try {
    New-Item -ItemType Directory -Force -Path $updir | Out-Null
    if ($CheckOnly) { Remove-Item -LiteralPath $check -ErrorAction SilentlyContinue }
    else            { Remove-Item -LiteralPath $done  -ErrorAction SilentlyContinue }
    try { [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12 } catch {}
    $ua = @{ 'User-Agent' = 'AioUpdate' }
    $r = Invoke-RestMethod "https://api.github.com/repos/$Repo/releases/latest" -Headers $ua
    $tag = ($r.tag_name -replace '^v', '')

    if ($CheckOnly) {
        if (Newer $tag $Current) { Check "AVAILABLE $tag" } else { Check "UPTODATE $Current" }
        exit
    }

    if (-not (Newer $tag $Current)) { Status "UPTODATE $Current"; exit }
    $a = $r.assets | Where-Object { $_.name -like 'AioHud-*.zip' -and $_.name -notlike '*.sha256' } | Select-Object -First 1
    if (-not $a) { Status 'ERROR no-zip-asset-in-release'; exit }
    New-Item -ItemType Directory -Force -Path (Split-Path $zip) | Out-Null
    Invoke-WebRequest $a.browser_download_url -OutFile $zip -Headers $ua

    # INTEGRITY GATE. Everything below this point extracts attacker-controlled-if-compromised content over the
    # Windower ROOT, and a plugin DLL there is loaded into the game process on the next //load. HTTPS proves we
    # talked to GitHub; it does not prove the asset is the one CI built. Compare against the .sha256 sidecar the
    # release workflow publishes next to the zip.
    # FAILS CLOSED, on purpose, including when the sidecar is missing. That does mean a release published
    # without one cannot be installed by this updater -- which is the intent: "no checksum" and "wrong checksum"
    # are the same statement about how much we know. Both messages name the remedy so it is never a silent stall.
    $sa = $r.assets | Where-Object { $_.name -like '*.zip.sha256' } | Select-Object -First 1
    if (-not $sa) {
        Remove-Item -LiteralPath $zip -Force -ErrorAction SilentlyContinue
        Status 'ERROR release has no .sha256 checksum -- refusing to install it. Update manually from the GitHub release page.'; exit
    }
    $want = ((Invoke-WebRequest $sa.browser_download_url -Headers $ua).Content -replace '[^0-9a-fA-F]', '').ToLower()
    $got  = (Get-FileHash -Algorithm SHA256 -LiteralPath $zip).Hash.ToLower()
    if ($want -ne $got) {
        Remove-Item -LiteralPath $zip -Force -ErrorAction SilentlyContinue
        Status "ERROR checksum mismatch (expected $($want.Substring(0,[Math]::Min(12,$want.Length))), got $($got.Substring(0,12))) -- download refused, nothing was installed"; exit
    }

    Status "READY $tag"    # download verified -> the addon now //unloads AioHud
    # wait (up to 30s) for AioHud.dll to become writable = the plugin unloaded (on ALL clients, for dual-box)
    $dll = Join-Path $Plugins 'AioHud.dll'
    $unlocked = $false
    for ($i = 0; $i -lt 60; $i++) {
        try { $fs = [IO.File]::Open($dll, 'Open', 'ReadWrite', 'None'); $fs.Close(); $unlocked = $true; break }
        catch { Start-Sleep -Milliseconds 500 }
    }
    if (-not $unlocked) { Status 'ERROR dll-locked (dual-box? //unload AioHud on the other client)'; exit }
    # the zip is Windower-root-relative (plugins\... + addons\...), so extract over the root = parent of plugins\
    $root = Split-Path $Plugins -Parent
    Expand-Archive -LiteralPath $zip -DestinationPath $root -Force
    Remove-Item -LiteralPath $zip -Force -ErrorAction SilentlyContinue
    # drop the legacy fixed-name download if an older build left one behind
    Remove-Item -LiteralPath (Join-Path (Join-Path $Data 'cache') 'update.zip') -Force -ErrorAction SilentlyContinue
    Status "OK $tag"
}
catch { if ($CheckOnly) { Check "ERROR $($_.Exception.Message)" } else { Status "ERROR $($_.Exception.Message)" } }
