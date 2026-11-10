#!/usr/bin/env python3
"""Convert the player's avatar and spirit pictures (the SMN content of the Pet box, src/ui/hud_smn.cpp) into the repo's
assets\\smn\\, shipped by deploy.bat / package.bat. SPEC docs/design/smn-module-spec.md.

    python scripts/gen_smn_art.py --src "C:\\Users\\<you>\\Downloads"

The player's pictures (2026-11-05 / 06 ; "oui c'est mes images", 2026-11-10) are 1024 x 1024 PNGs on a transparent
background, in colour : the 14 avatars in <src>\\avatars_ffxi_1024\\<name>.png, the 8 spirits in <src>\\<name>.png.
Each becomes assets\\smn\\<name>.raw, 128 x 128 BGRA, top-down, no header (load_raw_texture), its colours kept.
Each picture is cropped to what it draws and centred in its square with the same margin, so every avatar fills its
disc the same way whatever room the picture left round it. A missing file is said and left out : the box draws that
avatar's disc without a picture.
"""
import argparse, os, sys
from PIL import Image

OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'assets', 'smn')
SIDE, MARGIN, HAZE = 128, 0.03, 8   # alpha at or under HAZE is the generator's haze, not the picture
AVATARS = ['ifrit', 'shiva', 'garuda', 'titan', 'ramuh', 'leviathan', 'carbuncle', 'fenrir', 'diabolos', 'cait_sith',
           'siren', 'atomos', 'alexander', 'odin']
SPIRITS = ['fire_spirit', 'ice_spirit', 'wind_spirit', 'earth_spirit', 'thunder_spirit', 'water_spirit',
           'light_spirit', 'dark_spirit']


def convert(src):
    im = Image.open(src).convert('RGBA')
    a = im.split()[3].point(lambda v: 0 if v <= HAZE else v)
    im.putalpha(a)
    box = a.point(lambda v: 255 if v else 0).getbbox()
    if not box:
        return None
    part = im.crop(box)
    side = int(max(part.size) * (1 + 2 * MARGIN))
    sq = Image.new('RGBA', (side, side), (0, 0, 0, 0))
    sq.paste(part, ((side - part.width) // 2, (side - part.height) // 2))
    # resized premultiplied, so the transparent pixels' colour does not bleed a dark fringe round the edge
    r, g, b, al = sq.split()
    pre = Image.merge('RGBA', tuple(Image.composite(c, Image.new('L', sq.size, 0), al) for c in (r, g, b)) + (al,))
    small = pre.resize((SIDE, SIDE), Image.LANCZOS)
    out = []
    for pr, pg, pb, pa in small.getdata():
        if pa: out.append((min(255, pr * 255 // pa), min(255, pg * 255 // pa), min(255, pb * 255 // pa), pa))
        else:  out.append((0, 0, 0, 0))
    res = Image.new('RGBA', (SIDE, SIDE)); res.putdata(out)
    return res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--src', required=True, help='the folder holding avatars_ffxi_1024\\ and the *_spirit.png pictures')
    a = ap.parse_args()
    os.makedirs(OUT, exist_ok=True)
    want = [(n, os.path.join(a.src, 'avatars_ffxi_1024', n + '.png')) for n in AVATARS] + \
           [(n, os.path.join(a.src, n + '.png')) for n in SPIRITS]
    n = 0
    for name, src in want:
        if not os.path.isfile(src):
            print('  missing', src); continue
        im = convert(src)
        if im is None:
            print('  empty', src); continue
        r, g, b, al = im.split()
        open(os.path.join(OUT, name + '.raw'), 'wb').write(Image.merge('RGBA', (b, g, r, al)).tobytes())
        n += 1
    print('wrote %d picture(s) -> %s' % (n, OUT))
    return 0 if n == len(want) else 1


if __name__ == '__main__':
    sys.exit(main())
