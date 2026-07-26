#!/usr/bin/env bash
# gen_window_skin.sh -- convert the game's window-skin DDS into the BGRA .raw the plugin loads.
#
# The game builds every window from 4 DXT1 textures per theme (a tiled bg, a horizontal edge band,
# a vertical edge band, a corner sheet). Their DDS headers have non-standard flags (FFXI omits the
# HEIGHT/WIDTH/PIXELFORMAT bits), so PC tools reject them -> we patch dwFlags, then convert to a
# straight-alpha BGRA blob with ImageMagick.  Output: assets/window/<theme>/{corner,hframe,vframe,bg}.raw
#
# Source layout: "assets/window_src/0/<theme>/menu {corner,hfr1|fr1,vfr1,newtex|newtext}.dds"
# (_Bis = the user's modified variants ; a couple of themes have typo'd filenames -> handled below).
#
# Run from anywhere : paths resolve from THIS script's own location, so they follow the repo instead of
# the machine. (They used to be hardcoded to the old runtime folder `plugins\_aiohud_re`, which was renamed
# AND split from the dev repo -- the script had been unrunnable ever since.)
# Needs python + ImageMagick (magick) on PATH.
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRCBASE="$ROOT/assets/window_src/0"
OUTBASE="$ROOT/assets/window"
[ -d "$SRCBASE" ] || { echo "gen_window_skin: source themes not found : $SRCBASE" >&2; exit 1; }

# patch the DDS flags to standard, then magick -> BGRA raw. $1=src dds  $2=out raw  $3=tmp dir
conv() {
  python -c "import sys;b=bytearray(open(sys.argv[1],'rb').read());b[8:12]=bytes([7,16,10,0]);open(sys.argv[2],'wb').write(b)" "$1" "$3/_t.dds"
  magick "$3/_t.dds" -depth 8 "BGRA:$2" 2>/dev/null
}
# first existing "<dir>/<name>.dds" among the candidate names (handles hfr1/fr1, newtex/newtext typos)
first() { for n in "${@:2}"; do [ -f "$1/$n.dds" ] && { echo "$1/$n.dds"; return; }; done; }

cd "$SRCBASE" || exit 1
for d in */; do d=${d%/}; o="$OUTBASE/$d"; mkdir -p "$o"
  conv "$(first "$d" "menu corner")"                 "$o/corner.raw" "$o"
  conv "$(first "$d" "menu vfr1" "menu vf1")"         "$o/vframe.raw" "$o"
  conv "$(first "$d" "menu hfr1" "menu fr1" "menu hf1")" "$o/hframe.raw" "$o"
  conv "$(first "$d" "menu newtex" "menu newtext")"  "$o/bg.raw"     "$o"
  rm -f "$o/_t.dds"
  echo "  $d -> $(ls "$o"/*.raw 2>/dev/null | wc -l) raw"
done
echo "Done. Add new theme folders to THEMES[] in src/gfx/window.cpp to expose them in //aio menu N."
