#!/usr/bin/env python3
# gen_moon_phases.py -- the moon pod's eight phase icons, cut UPRIGHT out of the starry dial plate
# assets/moon_dial_src/moon_dial.png (its moons on one circle, new at the bottom, waxing up the left, full at the top,
# waning down the right) -> assets/moon_phases.raw, a 1024x128 straight-alpha BGRA strip of 8 cells :
#   0 new, 1 waxing crescent, 2 first quarter, 3 waxing gibbous, 4 full, 5 waning gibbous, 6 last quarter, 7 waning crescent
# Each cell : the moon and its patch of night sky, a soft round edge. The minimap's moon pod (src/ui/minimap.cpp) shows
# yesterday's / today's / tomorrow's phase from it, upright on the screen (a rotating disc would tilt them).
#   python scripts/gen_moon_phases.py
import math
import os
import sys
import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, 'assets', 'moon_dial_src', 'moon_dial.png')
OUT = os.path.join(ROOT, 'assets', 'moon_phases.raw')
CELL = 128
RING = 0.705      # the moons' centres, in disc radii (measured on the plate)
MOON = 0.205      # a moon's radius (its outline), in disc radii
PAD = 1.12        # the sky kept round it


def fit_circle(gold, gx, gy, R):
    """the moon's outline : the circle (centre within +-6 % R of the guess, radius 0.18..0.23 R) whose rim is the most gold"""
    best = (-1.0, gx, gy, R * MOON)
    t = np.linspace(0, 2 * math.pi, 180, endpoint=False)
    h, w = gold.shape
    for rr in np.arange(0.18, 0.231, 0.004) * R:
        for dy in np.arange(-0.06, 0.061, 0.006) * R:
            for dx in np.arange(-0.06, 0.061, 0.006) * R:
                xs = np.clip((gx + dx + np.cos(t) * rr).astype(int), 0, w - 1)
                ys = np.clip((gy + dy + np.sin(t) * rr).astype(int), 0, h - 1)
                inside = np.clip((gx + dx + np.cos(t) * (rr + 6)).astype(int), 0, w - 1), np.clip((gy + dy + np.sin(t) * (rr + 6)).astype(int), 0, h - 1)
                sc = gold[ys, xs].mean() - gold[inside[1], inside[0]].mean()   # gold ON the rim, sky just outside it
                if sc > best[0]:
                    best = (sc, gx + dx, gy + dy, rr)
    return best[1], best[2], best[3]


def main():
    im = Image.open(SRC).convert('RGBA')
    a = np.asarray(im)[..., 3]
    ys, xs = np.nonzero(a > 20)
    cx, cy = (xs.min() + xs.max() + 1) / 2.0, (ys.min() + ys.max() + 1) / 2.0
    R = max(xs.max() + 1 - xs.min(), ys.max() + 1 - ys.min()) / 2.0
    out = np.zeros((CELL, CELL * 8, 4), np.uint8)
    rgb = np.asarray(im)[..., :3].astype(np.float32)
    gold = np.clip((rgb[..., 0] + rgb[..., 1]) * 0.5 - rgb[..., 2], 0, 255)   # gold vs the indigo sky
    yy, xx = np.mgrid[0:CELL, 0:CELL]
    d = np.hypot(xx + 0.5 - CELL / 2.0, yy + 0.5 - CELL / 2.0) / (CELL / 2.0)
    mask = np.clip((1.0 - d) / 0.04, 0.0, 1.0)                    # a round cell, a 2 % soft edge
    for k in range(8):
        ang = math.pi / 2 + k * math.pi / 4                         # screen angle, y down : new at the bottom, then clockwise
        mx, my = cx + math.cos(ang) * R * RING, cy + math.sin(ang) * R * RING
        mx, my, rr = fit_circle(gold, mx, my, R)                    # the plate is hand-made : find each moon's own outline
        half = rr * PAD
        cell = im.crop((int(round(mx - half)), int(round(my - half)), int(round(mx + half)), int(round(my + half)))).resize((CELL, CELL), Image.LANCZOS)
        c = np.asarray(cell).astype(np.float32)
        c[..., 3] *= mask
        out[:, k * CELL:(k + 1) * CELL] = np.uint8(np.clip(c + 0.5, 0, 255))
    with open(OUT, 'wb') as f:
        f.write(out[..., [2, 1, 0, 3]].copy().tobytes())
    Image.fromarray(out, 'RGBA').save(os.path.join(ROOT, 'assets', 'moon_dial_src', 'moon_phases_preview.png'))
    print('wrote', OUT, '(1024x128, 8 phases)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
