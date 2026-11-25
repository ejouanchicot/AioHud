#!/usr/bin/env python3
# gen_sky_frames.py -- the SKY MODULE's twelve scenes (the minimap's sky wheel, src/ui/minimap.cpp) :
# assets/sky_frames_src/sky_frames.png (one hand-inked sheet on papyrus, 4 columns x 3 rows, read left to right then top to
# bottom : 00 02 04 06 / 08 10 12 14 / 16 18 20 22 h -- the same hills in every scene, the sun or the moon placed by the
# hour) -> assets/sky_frames.raw, a 1024x512 BGRA atlas : the same 4 x 3 grid, each scene 256 x 170 px (3:2, opaque), the
# fourth row's last 2 px empty. The wheel shows scene (hour / 2) and rolls one notch every two Vana'diel hours.
#   python scripts/gen_sky_frames.py
import os
import sys
import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, 'assets', 'sky_frames_src', 'sky_frames.png')
OUT = os.path.join(ROOT, 'assets', 'sky_frames.raw')
COLS, ROWS, FW, FH = 4, 3, 256, 170
INSET = 0.035                     # each scene's soft paper border and the sheet's gutters stay out


def main():
    im = Image.open(SRC).convert('RGB')
    W, H = im.size
    atlas = Image.new('RGBA', (1024, 512), (0, 0, 0, 0))
    for i in range(COLS * ROWS):
        c, r = i % COLS, i // COLS
        x0, x1, y0, y1 = c * W / COLS, (c + 1) * W / COLS, r * H / ROWS, (r + 1) * H / ROWS
        dx, dy = (x1 - x0) * INSET, (y1 - y0) * INSET
        cell = im.crop((int(x0 + dx), int(y0 + dy), int(x1 - dx), int(y1 - dy)))
        cell = cell.resize((FW, FH), Image.LANCZOS).convert('RGBA')
        atlas.paste(cell, (c * FW, r * FH))
    a = np.asarray(atlas)
    with open(OUT, 'wb') as f:
        f.write(a[..., [2, 1, 0, 3]].copy().tobytes())
    atlas.save(os.path.join(ROOT, 'build', 'sky_frames_preview.png'))
    print('wrote', OUT, '(1024x512, 12 scenes of %dx%d)' % (FW, FH))
    return 0


if __name__ == '__main__':
    sys.exit(main())
