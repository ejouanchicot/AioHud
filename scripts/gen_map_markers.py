#!/usr/bin/env python3
# gen_map_markers.py -- the minimap MARKER atlas from the v3 sheets (assets/markers_src/v3_sheet1..3.png, transparent
# background) -> assets/map_markers.raw : a 512x256 straight-alpha BGRA atlas of 8 x 4 cells of 64 px.
#
# The v3 set comes in LAYERS : a white/grey FILL (the engine tints it : the claim colour of a mob, the party blue...)
# and a gold FRAME drawn over it untinted. A fill and its frame land in two cells with the SAME transform : one scale
# (the frame's fit), both centred on the cell -- so drawing fill then frame at one spot stacks them.
#
# Cells (MK_* in src/ui/minimap.cpp) -- directional ones point UP :
#    0 me fill        1 me frame       2 mob fill       3 mob frame      4 NM frame (crown)   5 party fill   6 party frame
#    7 alliance fill  8 alliance frame 9 trust fill    10 trust frame   11 pet fill          12 pet frame
#   13 target fill   14 target frame  15 pin fill      16 pin frame     17 other PC          18 NPC
#   19 Home Point    20 Survival Guide 21 Moogle        22 Waypoint      23 quest "???"       24 Wide Scan   25 off-map
#
#   python scripts/gen_map_markers.py
import os
import sys
import numpy as np
from PIL import Image
from scipy import ndimage

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
SRC = os.path.join(ROOT, 'assets', 'markers_src')
OUT = os.path.join(ROOT, 'assets', 'map_markers.raw')
CELL, COLS, ROWS, MARGIN = 64, 8, 4, 3

# sheet -> its icons in reading order (row 1 left to right, then row 2), each -> its atlas cell
SHEETS = [
    ('v3_sheet1.png', [0, 2, 5, 7, 1, 3, 6, 8]),                       # fills : me mob party alliance / frames
    ('v3_sheet2.png', [9, 11, 13, 15, 10, 12, 14, 16]),                # fills : trust pet target pin  / frames
    ('v3_sheet3.png', [17, 18, 19, 20, 21, 22, 23, 24, 25, 4]),        # pc npc homepoint guide moogle / waypoint quest scan offmap nm
]
PAIRS = [(0, 1), (2, 3), (2, 4), (5, 6), (7, 8), (9, 10), (11, 12), (13, 14), (15, 16)]   # (fill, frame) ; the NM reuses the mob fill


def premul_resize(im, size):
    a = np.asarray(im, dtype=np.float32) / 255.0
    rgb = a[..., :3] * a[..., 3:4]
    pm = Image.fromarray(np.uint8(np.clip(np.concatenate([rgb, a[..., 3:4]], -1) * 255.0 + 0.5, 0, 255)), 'RGBA')
    pm = pm.resize(size, Image.LANCZOS)
    b = np.asarray(pm, dtype=np.float32) / 255.0
    al = b[..., 3:4]
    rgb = np.where(al > 1e-4, b[..., :3] / np.maximum(al, 1e-4), 0.0)
    return Image.fromarray(np.uint8(np.clip(np.concatenate([rgb, al], -1) * 255.0 + 0.5, 0, 255)), 'RGBA')


def icons(im):
    """each icon as (RGBA cut by its own pixels, bbox) in reading order. Blobs of one icon (the paw's toes, the scan's
    rings, the NM's crown) are merged when they overlap in x on one row ; the background speckles (tiny blobs, and
    the faint fringe the matting left) are dropped."""
    a = np.asarray(im)[..., 3]
    lab, _ = ndimage.label(a > 40)
    objs = ndimage.find_objects(lab)
    mid = im.size[1] // 2
    big = []
    for k, s in enumerate(objs, start=1):
        if (s[0].stop - s[0].start) * (s[1].stop - s[1].start) > 3000:
            big.append([s[1].start, s[0].start, s[1].stop, s[0].stop, {k}])
    out = []
    for row in (0, 1):
        rb = sorted([b for b in big if ((b[1] + b[3]) // 2 >= mid) == bool(row)], key=lambda b: b[0])
        merged = []
        for b in rb:
            if merged and b[0] < merged[-1][2]:
                m = merged[-1]
                merged[-1] = [min(m[0], b[0]), min(m[1], b[1]), max(m[2], b[2]), max(m[3], b[3]), m[4] | b[4]]
            else:
                merged.append(b)
        out += merged
    src = np.asarray(im)
    near, idx = ndimage.distance_transform_edt(lab == 0, return_indices=True)
    nearest = lab[idx[0], idx[1]]
    res = []
    for b in out:
        mine = np.isin(nearest, list(b[4])) & (near <= 2) & (a > 0)
        cut = src.copy(); cut[..., 3] = np.where(mine, cut[..., 3], 0)
        ys, xs = np.nonzero(mine)
        box = (xs.min(), ys.min(), xs.max() + 1, ys.max() + 1)
        res.append(Image.fromarray(cut, 'RGBA').crop(box))
    return res


def place(atlas, cell, ic, k):
    w, h = max(1, round(ic.size[0] * k)), max(1, round(ic.size[1] * k))
    ic = premul_resize(ic, (w, h))
    cx, cy = (cell % COLS) * CELL, (cell // COLS) * CELL
    atlas.paste(ic, (cx + (CELL - w) // 2, cy + (CELL - h) // 2), ic)


def main():
    cells = {}
    for name, order in SHEETS:
        ics = icons(Image.open(os.path.join(SRC, name)).convert('RGBA'))
        if len(ics) != len(order):
            print('%s : expected %d icons, found %d -- the sheet changed ?' % (name, len(order), len(ics)))
            return 1
        for c, ic in zip(order, ics):
            cells[c] = ic
    side = CELL - 2 * MARGIN
    atlas = Image.new('RGBA', (CELL * COLS, CELL * ROWS), (0, 0, 0, 0))
    paired = set()
    for fill, frame in PAIRS:                                       # one scale per pair : the frame's fit
        fr = cells[frame]
        k = side / max(fr.size)
        place(atlas, frame, fr, k)
        if fill not in paired:
            place(atlas, fill, cells[fill], k)
            paired.add(fill)
        paired.add(frame)
    for c, ic in cells.items():                                     # the single-layer icons : their own fit
        if c not in paired:
            place(atlas, c, ic, side / max(ic.size))
    a = np.asarray(atlas, dtype=np.uint8)
    with open(OUT, 'wb') as f:
        f.write(a[..., [2, 1, 0, 3]].copy().tobytes())
    atlas.save(os.path.join(ROOT, 'build', 'map_markers_preview.png'))
    print('wrote', OUT, '(%dx%d, %d cells) ; preview build/map_markers_preview.png' % (atlas.size[0], atlas.size[1], len(cells)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
