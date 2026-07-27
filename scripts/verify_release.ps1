# verify_release.ps1 -- check a PUBLISHED release the way a player's updater sees it.
#
# WHY THIS EXISTS. v1.0.71 was published with an integrity check that could never pass. The comparison read
# `(Invoke-WebRequest ...).Content`, which on PowerShell 5.1 is a BYTE ARRAY whenever the response is not a
# recognised text type -- and GitHub serves release assets as application/octet-stream. The `-replace` then ran
# per byte and produced "49 56 99 52 ..." (the ASCII codes of the hex digits), so `$want -ne $got` was always
# true and EVERY update would have been refused as a checksum mismatch. A gate that only ever says no.
#
# The code had been reviewed, its syntax validated, and the hash logic exercised on a local file. None of that
# touched the one thing that was wrong: what GitHub actually hands back over the wire. The defect existed only
# in the real artifact, so only the real artifact could reveal it -- which is the whole argument for this file.
# It found the bug on its first genuine input, before any player had downloaded the release.
#
# Run it after every release. It exits non-zero on any failure, so CI runs it as a post-publish step and a red
# release job means the payload is not installable -- rather than everyone finding out at their next update.
#
#   .\scripts\verify_release.ps1                       # the current /releases/latest
#   .\scripts\verify_release.ps1 -Tag v1.0.71          # a specific tag
#
# Needs nothing but network access (no gh, no token -- the release API is public), so it runs identically on a
# dev machine and on a CI runner.
param(
    [string]$Repo = 'ejouanchicot/AioHud',
    [string]$Tag  = ''            # empty = whatever /releases/latest points at, i.e. what the updater queries
)
$ErrorActionPreference = 'Stop'
$ua   = @{ 'User-Agent' = 'AioUpdate' }
$fail = 0
function Ok  ($m) { Write-Host "  OK    $m" }
function Bad ($m) { Write-Host "  FAIL  $m"; $script:fail++ }

try { [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12 } catch {}

# 1) THE ENDPOINT THE UPDATER ACTUALLY USES. /releases/latest is not "the newest version" -- GitHub points it at
#    whichever release was PUBLISHED last. A release created without --latest leaves this pointing at an older
#    build, and the updater follows this, not the tag list. Checking the tag directly would miss that entirely.
$url = if ($Tag) { "https://api.github.com/repos/$Repo/releases/tags/$Tag" }
       else      { "https://api.github.com/repos/$Repo/releases/latest" }
$r = Invoke-RestMethod $url -Headers $ua
$ver = $r.tag_name -replace '^v', ''
Write-Host "release $($r.tag_name)  ($($r.assets.Count) asset(s))"

# 2) BOTH assets. The sidecar is load-bearing: clients on 1.0.71+ fail closed without it, so a release missing
#    one is not merely unverified, it is uninstallable.
$zipA = $r.assets | Where-Object { $_.name -like 'AioHud-*.zip' -and $_.name -notlike '*.sha256' } | Select-Object -First 1
$shaA = $r.assets | Where-Object { $_.name -like '*.zip.sha256' } | Select-Object -First 1
if ($zipA) { Ok "payload present : $($zipA.name)" } else { Bad 'no payload zip'; exit 1 }
if ($shaA) { Ok "checksum present : $($shaA.name)" } else { Bad 'no .sha256 sidecar -- clients on 1.0.71+ would refuse this release' }

# 3) OLD-CLIENT COMPATIBILITY. Every player runs the PREVIOUS version's script when they update, and that one
#    filters `-like 'AioHud-*.zip'` with no exclusion, taking the first match. It must still land on the zip and
#    never on the sidecar. It does today only because `-like` anchors the end of the pattern -- rename an asset
#    and that stops being true, silently, for everyone who has not updated yet.
$oldPick = $r.assets | Where-Object { $_.name -like 'AioHud-*.zip' } | Select-Object -First 1
if ($oldPick -and $oldPick.name -eq $zipA.name) { Ok "a pre-1.0.71 client still resolves to $($oldPick.name)" }
else { Bad "a pre-1.0.71 client would download '$($oldPick.name)' -- asset naming broke old updaters" }

if ($shaA) {
    $zip = Join-Path $env:TEMP "aio_relchk_$PID.zip"
    $sf  = Join-Path $env:TEMP "aio_relchk_$PID.sha"
    try {
        Invoke-WebRequest $zipA.browser_download_url -OutFile $zip -Headers $ua
        Invoke-WebRequest $shaA.browser_download_url -OutFile $sf  -Headers $ua

        # 4) THE CHECK ITSELF, byte for byte, exactly as assets/aioupdate.ps1 performs it -- read from a FILE,
        #    never from .Content (see the header).
        $want = ((Get-Content -Raw -LiteralPath $sf) -replace '[^0-9a-fA-F]', '').ToLower()
        $got  = (Get-FileHash -Algorithm SHA256 -LiteralPath $zip).Hash.ToLower()
        if ($want.Length -ne 64) { Bad "sidecar is not a sha256 digest (got $($want.Length) hex chars) -- this is the shape the .Content bug produced" }
        elseif ($want -eq $got)  { Ok  "checksum matches ($($want.Substring(0,16))...)" }
        else                     { Bad "checksum MISMATCH : sidecar $want, payload $got" }

        # 5) The gate must also be able to say NO. A comparison that accepts everything looks identical to a
        #    working one from the outside, so prove the rejection too.
        Add-Content -LiteralPath $zip -Value 'tamper'
        $bad = (Get-FileHash -Algorithm SHA256 -LiteralPath $zip).Hash.ToLower()
        if ($want -ne $bad) { Ok 'a tampered payload is rejected' } else { Bad 'a tampered payload would be ACCEPTED' }

        # 6) WHAT IS INSIDE. A payload can be intact and still be wrong: the three files that make an update work
        #    have to be in it, and the updater script it carries is the one every FUTURE update will run.
        Remove-Item -LiteralPath $zip -Force -ErrorAction SilentlyContinue
        Invoke-WebRequest $zipA.browser_download_url -OutFile $zip -Headers $ua
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        $ar = [IO.Compression.ZipFile]::OpenRead($zip)
        try {
            foreach ($need in @('plugins/AioHud.dll', 'plugins/AioHud/assets/aioupdate.ps1', 'addons/aioupdate/aioupdate.lua')) {
                if ($ar.Entries | Where-Object { $_.FullName -eq $need }) { Ok "contains $need" } else { Bad "MISSING $need" }
            }
            $e = $ar.Entries | Where-Object { $_.FullName -eq 'plugins/AioHud/assets/aioupdate.ps1' }
            if ($e) {
                $sr = New-Object IO.StreamReader($e.Open()); $txt = $sr.ReadToEnd(); $sr.Close()
                if ($txt.Contains(').Content -replace')) { Bad 'the shipped updater reads the sidecar from .Content -- the byte-array bug is back' }
                else { Ok 'the shipped updater does not read the sidecar from .Content' }
            }
        } finally { $ar.Dispose() }
    } finally {
        Remove-Item -LiteralPath $zip -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath $sf  -Force -ErrorAction SilentlyContinue
    }
}

Write-Host ''
if ($fail -eq 0) { Write-Host "release $ver : OK"; exit 0 }
Write-Host "release $ver : $fail check(s) FAILED"
exit 1
