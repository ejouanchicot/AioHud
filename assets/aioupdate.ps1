# AioHud self-updater. Launched by the plugin with CreateProcess + CREATE_NO_WINDOW (no console window at all),
# and driven together with the AioUpdate Lua addon (which does the //unload + //load the plugin can't do itself).
#
# Phases, each written as a single line to <Data>\update\done.txt (the Lua addon polls it):
#   UPTODATE <ver>   already on the latest release -> nothing to do
#   READY <ver>      newer release downloaded -> the addon //unloads AioHud so the DLL can be replaced
#   OK <ver>         extracted the new build over the Windower root (plugins\ + addons\) -> the addon //loads AioHud
#   ERROR <msg>      something went wrong -> the addon reloads the current build
param([string]$Current = '0', [string]$Repo = 'Tetsouo/AioHud', [string]$Plugins, [string]$Data)
$ErrorActionPreference = 'Stop'
$updir = Join-Path $Data 'update'
$done  = Join-Path $updir 'done.txt'
$zip   = Join-Path (Join-Path $Data 'cache') 'update.zip'
function Status($s) { New-Item -ItemType Directory -Force -Path $updir | Out-Null; Set-Content -LiteralPath $done -Value $s -Encoding ascii }
try {
    New-Item -ItemType Directory -Force -Path $updir | Out-Null
    Remove-Item -LiteralPath $done -ErrorAction SilentlyContinue
    try { [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12 } catch {}
    $ua = @{ 'User-Agent' = 'AioUpdate' }
    $r = Invoke-RestMethod "https://api.github.com/repos/$Repo/releases/latest" -Headers $ua
    $tag = ($r.tag_name -replace '^v', '')
    if ($tag -eq $Current) { Status "UPTODATE $tag"; exit }
    $a = $r.assets | Where-Object { $_.name -like 'AioHud-*.zip' } | Select-Object -First 1
    if (-not $a) { Status 'ERROR no-zip-asset-in-release'; exit }
    New-Item -ItemType Directory -Force -Path (Split-Path $zip) | Out-Null
    Invoke-WebRequest $a.browser_download_url -OutFile $zip -Headers $ua
    Status "READY $tag"    # download done -> the addon now //unloads AioHud
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
    Status "OK $tag"
}
catch { Status "ERROR $($_.Exception.Message)" }
