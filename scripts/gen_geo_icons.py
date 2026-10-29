#!/usr/bin/env python3
"""Convert the GEO content's images (the Pet box, src/ui/hud_geo.cpp) into the repo's assets\\geo\\, shipped by
deploy.bat / package.bat. docs/design/pet-module-spec.md, lot 2.

    python scripts/gen_geo_icons.py --src "F:\\Telechargement Divers\\Pet\\Geo"

The player's images (ChatGPT, 2026-10-29 ; the two last cardinal icons drawn in code the same day) are white on
transparent : the box tints them (the luopan in its bubble's element colour, the rest in the theme's). Each becomes
assets\\geo\\<name>.raw, BGRA, top-down, no header (load_raw_texture) :
  luopan            128 x 128  the bubble, its soft glow kept
  compass_ring      128 x 128  the Cardinal Chant compass ring, cropped round its outer circle (its empty centre's
                               share of the side is printed : hud_geo.cpp RING_INNER)
  ja_<name>          64 x 64   the GEO job abilities
  cardinal_<bonus>   64 x 64   the four Cardinal Chant bonuses
Every image is cropped to what it draws and centred in its square with the same margin, so the icons look the same
size whatever room the generator left around them. RGB forced to pure white (only the alpha is the drawing), the
near-transparent haze an image generator leaves taken out. A missing file is said and left out : the box draws that
element the way it did before the images.
"""
import argparse, os, sys
from PIL import Image

OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'assets', 'geo')
IMAGES = [   # (source file, output name, side, margin)
    ('geo_luopan.png', 'luopan', 128, 0.02),
    ('geo_compass_ring.png', 'compass_ring', 128, 0.0),
    ('geo_ja_full_circle.png', 'ja_full_circle', 64, 0.06),
    ('geo_ja_lasting_emanation.png', 'ja_lasting_emanation', 64, 0.06),
    ('geo_ja_ecliptic_attrition.png', 'ja_ecliptic_attrition', 64, 0.06),
    ('geo_ja_life_cycle.png', 'ja_life_cycle', 64, 0.06),
    ('geo_ja_blaze_of_glory.png', 'ja_blaze_of_glory', 64, 0.06),
    ('geo_ja_dematerialize.png', 'ja_dematerialize', 64, 0.06),
    ('geo_ja_entrust.png', 'ja_entrust', 64, 0.06),
    ('geo_ja_bolster.png', 'ja_bolster', 64, 0.06),
    ('geo_cardinal_mab.png', 'cardinal_mab', 64, 0.06),
    ('geo_cardinal_macc.png', 'cardinal_macc', 64, 0.06),
    ('geo_cardinal_mb.png', 'cardinal_mb', 64, 0.06),
    ('geo_cardinal_crit.png', 'cardinal_crit', 64, 0.06),
]
HAZE = 8   # alpha at or under this is the generator's haze, not the drawing


def convert(src, side, margin):
    im = Image.open(src).convert('RGBA')
    a = im.split()[3].point(lambda v: 0 if v <= HAZE else v)
    box = a.point(lambda v: 255 if v else 0).getbbox()
    if not box:
        return None, 0.0
    part = a.crop(box)
    sq_side = int(max(part.size) * (1 + 2 * margin))
    sq = Image.new('L', (sq_side, sq_side), 0)
    sq.paste(part, ((sq_side - part.width) // 2, (sq_side - part.height) // 2))
    alpha = sq.resize((side, side), Image.LANCZOS)
    white = Image.new('L', (side, side), 255)
    return Image.merge('RGBA', (white, white, white, alpha)), (sq_side, box)


def inner_share(src):
    """The compass ring's empty centre, as a share of its outer circle's diameter (walked from the centre)."""
    a = Image.open(src).convert('RGBA').split()[3]
    box = a.point(lambda v: 255 if v > HAZE else 0).getbbox()
    cx, cy = (box[0] + box[2]) // 2, (box[1] + box[3]) // 2
    r = 0
    while cx + r < a.width and a.getpixel((cx + r, cy)) <= HAZE: r += 1
    return 2.0 * r / max(box[2] - box[0], box[3] - box[1])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--src', required=True, help='the folder of the geo_*.png images')
    a = ap.parse_args()
    os.makedirs(OUT, exist_ok=True)
    n = 0
    for name, key, side, margin in IMAGES:
        src = os.path.join(a.src, name)
        if not os.path.isfile(src):
            print('  missing', src); continue
        im, _ = convert(src, side, margin)
        if im is None:
            print('  empty', src); continue
        r, g, b, al = im.split()
        open(os.path.join(OUT, key + '.raw'), 'wb').write(Image.merge('RGBA', (b, g, r, al)).tobytes())
        n += 1
        if key == 'compass_ring':
            print('  compass_ring : empty centre = %.3f of the side (hud_geo.cpp RING_INNER)' % inner_share(src))
    print('wrote %d image(s) -> %s' % (n, OUT))
    return 0 if n == len(IMAGES) else 1


if __name__ == '__main__':
    sys.exit(main())
