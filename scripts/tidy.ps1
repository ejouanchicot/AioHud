# tidy.ps1 -- lance clang-tidy sur ce projet, avec les drapeaux qui le font marcher.
#
# OUTIL LOCAL, pas une etape de CI. Voir l'en-tete de .clang-tidy pour le pourquoi : evalue le 2026-07-25 sur
# ~5000 lignes, il n'a trouve AUCUN bug reel. Il garde de la valeur sur du code NEUF (champ non initialise,
# division entiere involontaire), pas comme filet permanent.
#
#   .\scripts\tidy.ps1                       -> les 5 fichiers les plus charges
#   .\scripts\tidy.ps1 src\ui\minimap.cpp    -> un fichier precis
#   .\scripts\tidy.ps1 -All                  -> tout src\ (long)
#
# Les trois drapeaux non evidents, chacun pour une raison precise :
#   --header-filter        sans lui, 7994 des 8018 avertissements viennent des en-tetes systeme
#   -D_ALLOW_COMPILER...   clang 12 (livre avec VS BuildTools) refuse la STL de VS 2022 sans ca
#   -m32                   le plugin est 32 bits ; sans ca les tailles de types sont fausses
param([string]$File, [switch]$All)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

# clang-tidy est livre avec VS BuildTools -- pas besoin d'installer LLVM.
$tidy = Get-ChildItem -Path 'C:\Program Files (x86)\Microsoft Visual Studio','C:\Program Files\LLVM' `
                      -Filter clang-tidy.exe -Recurse -ErrorAction SilentlyContinue |
        Select-Object -First 1 -ExpandProperty FullName
if (-not $tidy) { Write-Error "clang-tidy introuvable (livre avec VS BuildTools, ou installer LLVM)"; exit 1 }

if ($File)     { $targets = @($File) }
elseif ($All)  { $targets = Get-ChildItem "$root\src" -Filter *.cpp -Recurse |
                            Where-Object { $_.Name -ne 'aiohud_probes.cpp' } |
                            ForEach-Object { $_.FullName } }
else           { $targets = @('src\model\game_mem.cpp','src\model\party_state.cpp',
                              'src\ui\hud_timers.cpp','src\gfx\texture.cpp','src\ui\party.cpp') }

$flags = @('--','-m32','-std=c++17',"-I$root\include","-I$root\src",
           '-D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH')
$total = 0
foreach ($t in $targets) {
    $p = if ([System.IO.Path]::IsPathRooted($t)) { $t } else { Join-Path $root $t }
    if (-not (Test-Path $p)) { Write-Host "  absent : $t"; continue }
    # PowerShell 5.1 : rediriger stderr d'un .exe transforme chaque ligne en NativeCommandError et fait
    # echouer le script alors que l'outil a reussi. On bascule en Continue et on FUSIONNE les flux ; le filtre
    # ci-dessous jette les lignes de bruit de toute facon.
    $prev = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $out = & $tidy $p '--header-filter=aiohud[\\/](src|include)' @flags 2>&1
    $ErrorActionPreference = $prev
    # On ne garde que NOS fichiers, et seulement les lignes qui COMMENCENT par le chemin du depot : les flux
    # fusionnes reprennent le meme diagnostic dans les lignes de contexte, ce qui gonflait le compte (158 au
    # lieu de 34). Les erreurs <tuple> de la STL VS2022 tombent d'elles-memes, elles ne sont pas chez nous.
    $rx = '^' + [regex]::Escape($root)
    $hits = $out | ForEach-Object { "$_" } |
            Where-Object { $_ -match $rx -and $_ -match 'warning:' } |
            Select-Object -Unique
    $total += $hits.Count
    "{0,-26} {1,3} constat(s)" -f (Split-Path $t -Leaf), $hits.Count
    $hits | ForEach-Object { "     " + ($_ -replace [regex]::Escape("$root\"), '') }
}
""
"TOTAL : $total constat(s)"
"Rappel : un constat n'est pas un bug. Lire la ligne avant de changer quoi que ce soit."
