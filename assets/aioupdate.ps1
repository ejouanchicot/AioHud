# AioHud self-updater. Launched by the plugin with CreateProcess + CREATE_NO_WINDOW (no console window at all),
# and driven together with the AioUpdate Lua addon (which does the //unload + //load the plugin can't do itself).
#
# Two modes:
#   -CheckOnly : just query the latest release and write <Data>\update\check.txt (no download, no reload) :
#       UPTODATE <ver>   / AVAILABLE <ver>   / ERROR <msg>       (the plugin reads this for the Update tab)
#   (default)  : full update, writing phases to <Data>\update\done.txt (the Lua addon polls it) :
#       UPTODATE <ver>   already on the latest release -> nothing to do
#       READY <ver>      newer release downloaded, VERIFIED and staged -> the addon //unloads AioHud so the DLL can be replaced
#       OK <ver>         installed over the Windower root (plugins\ + addons\) -> the addon //loads AioHud
#       ERROR <msg>      something went wrong -> the addon reloads the current build
#
# INVARIANT (the one this script exists to keep) : a failed update leaves the install EXACTLY as it was. Download,
# checksum and extraction all happen before the plugin is even unloaded ; the install is then additive (assets
# first) with the DLL swapped last, from a backup that is restored if that single copy fails. There is no path
# that ends with no AioHud.dll on disk -- there used to be, and it read to the player as "File does not exist".
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
    # Sweep the leftovers of runs that died before their own cleanup (killed process, closed game mid-update) :
    # per-PID downloads and scratch folders. Measured on a dev install : 28 MB of update_*.zip from four aborted
    # runs, kept forever. Only touch what is over a day old, so a CONCURRENT updater (dual-box) is never robbed.
    $cache = Join-Path $Data 'cache'
    if (Test-Path -LiteralPath $cache) {
        $cut = (Get-Date).AddDays(-1)
        Get-ChildItem -LiteralPath $cache -Force -ErrorAction SilentlyContinue |
            Where-Object { $_.LastWriteTime -lt $cut -and ($_.Name -like 'update_*.zip' -or $_.Name -like 'stage_*') } |
            Remove-Item -Recurse -Force -ErrorAction SilentlyContinue
    }
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
    # -OutFile, NOT (Invoke-WebRequest).Content. On PS 5.1 that property is a BYTE ARRAY whenever the response
    # is not a recognised text type -- and GitHub serves release assets as application/octet-stream. The
    # `-replace` then ran per byte and produced "49 56 99 52 ..." (the ASCII codes of the hex digits), so the
    # comparison could never match and EVERY update would have been refused as a checksum mismatch. Caught by
    # running this against the real published release instead of trusting it; reading it from a file is
    # deterministic regardless of content type or PowerShell version.
    $shaFile = "$zip.sha256"
    Invoke-WebRequest $sa.browser_download_url -OutFile $shaFile -Headers $ua
    $want = ((Get-Content -Raw -LiteralPath $shaFile) -replace '[^0-9a-fA-F]', '').ToLower()
    Remove-Item -LiteralPath $shaFile -Force -ErrorAction SilentlyContinue
    $got  = (Get-FileHash -Algorithm SHA256 -LiteralPath $zip).Hash.ToLower()
    if ($want -ne $got) {
        Remove-Item -LiteralPath $zip -Force -ErrorAction SilentlyContinue
        Status "ERROR checksum mismatch (expected $($want.Substring(0,[Math]::Min(12,$want.Length))), got $($got.Substring(0,12))) -- download refused, nothing was installed"; exit
    }

    # STAGE THE PAYLOAD -- never expand straight over the live Windower root. `Expand-Archive -Force` there is
    # what turned a failed update into a DEAD install : per entry it Remove-Item's the destination file BEFORE
    # extracting (Microsoft.PowerShell.Archive.psm1 l.1029), and its `finally` DELETES everything it had already
    # expanded if anything throws on the way (l.411). So one access-denied / antivirus / I-O hiccup anywhere in
    # the 1400-file payload left plugins\AioHud.dll simply GONE ; the addon then //loaded a file that no longer
    # existed and Windower answered "File does not exist", with the real reason unread in done.txt. Reported
    # 2026-08-03 by a player going 1.0.71 -> 1.0.73.
    # Expanding into a scratch folder first also moves the risky step BEFORE the //unload : an extraction failure
    # now aborts with the HUD still running, instead of interrupting a half-finished install.
    $stage = Join-Path (Join-Path $Data 'cache') "stage_$PID"
    Remove-Item -LiteralPath $stage -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force -Path $stage | Out-Null
    Expand-Archive -LiteralPath $zip -DestinationPath $stage -Force
    Remove-Item -LiteralPath $zip -Force -ErrorAction SilentlyContinue
    # A partial success is not a success : check what the extraction actually PRODUCED, not that it returned.
    $newDll = Join-Path $stage 'plugins\AioHud.dll'
    if (-not (Test-Path -LiteralPath $newDll -PathType Leaf) -or (Get-Item -LiteralPath $newDll).Length -lt 100000) {
        Remove-Item -LiteralPath $stage -Recurse -Force -ErrorAction SilentlyContinue
        Status 'ERROR the downloaded payload carries no usable AioHud.dll -- nothing was installed'; exit
    }

    Status "READY $tag"    # payload staged AND verified -> the addon now //unloads AioHud
    # wait (up to 30s) for AioHud.dll to become writable = the plugin unloaded (on ALL clients, for dual-box)
    $dll = Join-Path $Plugins 'AioHud.dll'
    $unlocked = $false
    for ($i = 0; $i -lt 60; $i++) {
        try { $fs = [IO.File]::Open($dll, 'Open', 'ReadWrite', 'None'); $fs.Close(); $unlocked = $true; break }
        catch { Start-Sleep -Milliseconds 500 }
    }
    if (-not $unlocked) {
        Remove-Item -LiteralPath $stage -Recurse -Force -ErrorAction SilentlyContinue
        Status 'ERROR dll-locked (dual-box? //unload AioHud on the other client)'; exit
    }
    # the payload is Windower-root-relative (plugins\... + addons\...), so it installs over the root = parent of plugins\
    $root = Split-Path $Plugins -Parent
    # 1) everything EXCEPT the DLL. /R:2 /W:1 : robocopy's default is a million retries 30s apart -- one locked
    #    file and the update would hang until the player gives up. Exit codes 0-7 are success, 8+ a real error.
    & robocopy $stage $root /E /XF AioHud.dll /R:2 /W:1 /NFL /NDL /NJH /NJS /NP | Out-Null
    if ($LASTEXITCODE -ge 8) {
        Remove-Item -LiteralPath $stage -Recurse -Force -ErrorAction SilentlyContinue
        Status "ERROR asset install failed (robocopy $LASTEXITCODE) -- your current build was left untouched"; exit
    }
    # 2) the DLL LAST, and with a restorable copy of the one that works. Everything above this line is additive ;
    #    this is the only step that can leave the plugin unloadable, so it owns the shortest possible window.
    $bak = "$dll.bak"
    Remove-Item -LiteralPath $bak -Force -ErrorAction SilentlyContinue
    if (Test-Path -LiteralPath $dll -PathType Leaf) { Copy-Item -LiteralPath $dll -Destination $bak -Force }
    try {
        Copy-Item -LiteralPath $newDll -Destination $dll -Force
        if (-not (Test-Path -LiteralPath $dll -PathType Leaf) -or
            (Get-Item -LiteralPath $dll).Length -ne (Get-Item -LiteralPath $newDll).Length) { throw 'truncated copy' }
    } catch {
        $why = $_.Exception.Message
        if (Test-Path -LiteralPath $bak -PathType Leaf) { Copy-Item -LiteralPath $bak -Destination $dll -Force -ErrorAction SilentlyContinue }
        Remove-Item -LiteralPath $stage -Recurse -Force -ErrorAction SilentlyContinue
        if (Test-Path -LiteralPath $dll -PathType Leaf) {
            Remove-Item -LiteralPath $bak -Force -ErrorAction SilentlyContinue
            Status "ERROR install failed ($why) -- your previous build was restored"
        } else {
            Status "ERROR install failed ($why) -- AioHud.dll is MISSING, reinstall the zip from the release page"
        }
        exit
    }
    Remove-Item -LiteralPath $bak -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $stage -Recurse -Force -ErrorAction SilentlyContinue
    # drop the legacy fixed-name download if an older build left one behind
    Remove-Item -LiteralPath (Join-Path (Join-Path $Data 'cache') 'update.zip') -Force -ErrorAction SilentlyContinue
    Status "OK $tag"
}
catch { if ($CheckOnly) { Check "ERROR $($_.Exception.Message)" } else { Status "ERROR $($_.Exception.Message)" } }
