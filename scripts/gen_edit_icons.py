"""gen_edit_icons.py -- the Edit Layout drawer's icons (eye shown / hidden, padlock locked / open).

Sources : assets/icons_src/fontawesome/*.svg -- Font Awesome Free 7 (Fonticons, Inc.), icons under CC BY 4.0
(attribution : NOTICE-FontAwesome.txt, shipped by package.bat).

Output : assets/edit_icons.raw -- 512 x 128 BGRA, four 128 px cells left to right :
  0 eye (solid)  = shown      1 eye (regular) = hidden      2 lock (solid) = locked      3 unlock (solid) = open
WHITE glyphs on transparent : the plugin tints them (brass when on, steel when off).

Rasterised by ImageMagick (its SVG renderer) at a high density, then reduced -- the plugin builds the mip chain.
    python scripts/gen_edit_icons.py
"""
import os, subprocess, sys, tempfile
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, 'assets', 'icons_src', 'fontawesome')
OUT = os.path.join(ROOT, 'assets', 'edit_icons.raw')
ORDER = ['eye-solid-full', 'eye-regular-full', 'lock-solid-full', 'unlock-solid-full']
CELL = 128
MAGICK = os.environ.get('MAGICK', r'F:\Logiciels\ImageMagick-7.1.1-Q16-HDRI\magick.exe')

def main():
    sheet = Image.new('RGBA', (CELL * len(ORDER), CELL), (255, 255, 255, 0))
    with tempfile.TemporaryDirectory() as tmp:
        for k, name in enumerate(ORDER):
            svg = os.path.join(SRC, name + '.svg')
            png = os.path.join(tmp, name + '.png')
            # a 10 % margin round the glyph so its anti-aliased edge never touches the cell border (mip bleed)
            subprocess.check_call([MAGICK, '-background', 'none', '-density', '900', svg, '-resize', '%dx%d' % (CELL - 12, CELL - 12),
                                   '-gravity', 'center', '-extent', '%dx%d' % (CELL, CELL), png])
            a = Image.open(png).convert('RGBA').getchannel('A')
            glyph = Image.new('RGBA', (CELL, CELL), (255, 255, 255, 0)); glyph.putalpha(a)
            sheet.paste(glyph, (k * CELL, 0))
    r, g, b, a = sheet.split()
    with open(OUT, 'wb') as f:
        f.write(Image.merge('RGBA', (b, g, r, a)).tobytes())   # BGRA, top-down
    print('edit icons ->', OUT, sheet.size)

if __name__ == '__main__':
    sys.exit(main())
