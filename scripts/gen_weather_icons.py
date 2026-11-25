#!/usr/bin/env python3
# gen_weather_icons.py -- the WEATHER's 8 element icons, in HD, for the minimap's weather orbs ONLY (the user 2026-11-25 :
# "specific to the weather" ; every other element icon stays assets/element_icons.raw) : assets/weather_icons_src/weather_elements.png (one sheet, 4 x 2, transparent
# background, row 1 Fire Earth Water Wind, row 2 Ice Lightning Light Dark -- drawn to read at 16 px and at 128 px)
# -> assets/weather_icons.raw, a 1024x128 horizontal strip of 8 cells of 128 px, straight-alpha BGRA, loaded with mipmaps.
#
# Cell order MUST match VanaClock.dayIdx (src/model/vana_clock.h) : 0=Fire 1=Earth 2=Water 3=Wind 4=Ice 5=Lightning 6=Light
# 7=Dark -- the sheet's own reading order.
#
# Pipeline :
#   1. Split the sheet on its 4 x 2 grid (the fire's tip nearly touches the snowflake : a grid cut, not a flood fill).
#   2. In each grid cell keep the LARGEST alpha component and what touches it (drop the stray matting specks and the
#      coloured fringe islands the generator left around some icons).
#   3. Trim to the icon's bounding box (alpha > 24), fit CENTERED into 128 px with a small margin, aspect preserved,
#      LANCZOS in premultiplied space (no fringe bleeding into the anti-aliased edge).
#
# Keep the icons' own colours (drawn as-is, not tinted). Re-run if the sheet changes.
#   python scripts/gen_weather_icons.py
import os
import sys
import numpy as np
from PIL import Image
from scipy import ndimage

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, 'assets', 'weather_icons_src', 'weather_elements.png')
OUT = os.path.join(ROOT, 'assets', 'weather_icons.raw')
CELL, MARGIN, CELLS = 128, 3, 8
NAMES = ['Fire', 'Earth', 'Water', 'Wind', 'Ice', 'Lightning', 'Light', 'Dark']


def premul_fit(rgba, side):
    a = rgba[..., 3:4].astype(np.float32) / 255.0
    pm = np.concatenate([rgba[..., :3].astype(np.float32) * a, a * 255.0], -1)
    h, w = rgba.shape[:2]
    k = side / max(w, h)
    tw, th = max(1, round(w * k)), max(1, round(h * k))
    pm_s = np.asarray(Image.fromarray(np.uint8(np.clip(pm + 0.5, 0, 255)), 'RGBA').resize((tw, th), Image.LANCZOS)).astype(np.float32)
    al = pm_s[..., 3:4] / 255.0
    rgb = np.where(al > 1e-4, pm_s[..., :3] / np.maximum(al, 1e-4), 0.0)
    return np.uint8(np.clip(np.concatenate([rgb, pm_s[..., 3:4]], -1) + 0.5, 0, 255))


def main():
    sheet = np.asarray(Image.open(SRC).convert('RGBA'))
    H, W = sheet.shape[:2]
    strip = np.zeros((CELL, CELL * CELLS, 4), np.uint8)
    for i in range(CELLS):
        r, c = divmod(i, 4)
        x0, x1, y0, y1 = c * W // 4, (c + 1) * W // 4, r * H // 2, (r + 1) * H // 2
        cell = sheet[y0:y1, x0:x1].copy()
        a = cell[..., 3]
        lab, n = ndimage.label(a > 24)
        if n == 0:
            print('cell %d (%s) is EMPTY -- the sheet changed ?' % (i, NAMES[i])); return 1
        sizes = ndimage.sum(np.ones_like(a), lab, range(1, n + 1))
        keep = np.zeros(n + 1, bool)
        big = sizes.max()
        for k in range(n):
            if sizes[k] >= big * 0.02:              # the icon (and any real part of it) ; specks and fringe islands go
                keep[k + 1] = True
        mask = keep[lab]
        mask = ndimage.binary_dilation(mask, iterations=2)   # keep the soft anti-aliased edge around what is kept
        cell[..., 3] = np.where(mask, a, 0)
        ys, xs = np.nonzero(cell[..., 3] > 24)
        icon = cell[ys.min():ys.max() + 1, xs.min():xs.max() + 1]
        fit = premul_fit(icon, CELL - 2 * MARGIN)
        th, tw = fit.shape[:2]
        ox, oy = i * CELL + (CELL - tw) // 2, (CELL - th) // 2
        strip[oy:oy + th, ox:ox + tw] = fit
    with open(OUT, 'wb') as f:
        f.write(strip[..., [2, 1, 0, 3]].copy().tobytes())
    Image.fromarray(strip, 'RGBA').save(os.path.join(ROOT, 'build', 'weather_icons_preview.png'))
    print('wrote', OUT, '(%dx%d, 8 cells of %d px)' % (CELL * CELLS, CELL, CELL))
    return 0


if __name__ == '__main__':
    sys.exit(main())
