#!/usr/bin/env python3
# gen_net_icons.py -- the CONNECTION BOX icons : assets/net_icons_src/net_icons.png (row 1 : the FILLS -- envelope,
# speech bubble, bag, sword, the 8-dot spinner ; row 2 : the FRAMES -- envelope, "GM" bubble, bag, sword ; transparent
# background) -> assets/net_icons.raw, a 512x128 straight-alpha BGRA atlas of 8 x 2 cells of 64 px.
#
# Cells (NI_* in src/ui/hud_actmenu.cpp) : 0 mail fill, 1 mail frame, 2 gm fill, 3 gm frame, 4 bag fill, 5 bag frame,
# 6 gear fill, 7 gear frame, 8 spinner. Every pixel is turned to its LUMINANCE (the source carried a red / yellow
# matting fringe) : the fills are tinted by the engine, the frames take the HUD theme's metal. A fill and its frame share
# ONE scale (the frame's fit) and are both centred, so drawn at one spot they stack.
#
#   python scripts/gen_net_icons.py
import os
import sys
import numpy as np
from PIL import Image
from scipy import ndimage

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
SRC = os.path.join(ROOT, 'assets', 'net_icons_src', 'net_icons.png')
OUT = os.path.join(ROOT, 'assets', 'net_icons.raw')
CELL, COLS, ROWS, MARGIN = 64, 8, 2, 3
ORDER = [[0, 2, 4, 6, 8], [1, 3, 5, 7]]          # sheet row -> its icons' cells, left to right
PAIRS = [(0, 1), (2, 3), (4, 5), (6, 7)]


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
    a = np.asarray(im)[..., 3]
    lab, _ = ndimage.label(a > 40)
    objs = ndimage.find_objects(lab)
    big = []
    for k, s in enumerate(objs, start=1):
        if (s[0].stop - s[0].start) * (s[1].stop - s[1].start) > 2000:
            big.append([s[1].start, s[0].start, s[1].stop, s[0].stop, {k}])
    mid = im.size[1] // 2
    rows = []
    for row in (0, 1):
        rb = sorted([b for b in big if ((b[1] + b[3]) // 2 >= mid) == bool(row)], key=lambda b: b[0])
        merged = []
        for b in rb:                                  # overlapping in x = one icon (the spinner's dots, the G and M)
            if merged and b[0] < merged[-1][2] + 30:     # within 30 px (the icons stand ~70 px apart)
                m = merged[-1]
                merged[-1] = [min(m[0], b[0]), min(m[1], b[1]), max(m[2], b[2]), max(m[3], b[3]), m[4] | b[4]]
            else:
                merged.append(b)
        rows.append(merged)
    src = np.asarray(im).astype(np.float32)
    lum = 0.2126 * src[..., 0] + 0.7152 * src[..., 1] + 0.0722 * src[..., 2]   # grey : no matting tint left
    grey = np.stack([lum, lum, lum, src[..., 3]], -1)
    near, idx = ndimage.distance_transform_edt(lab == 0, return_indices=True)
    nearest = lab[idx[0], idx[1]]
    out = []
    for merged in rows:
        r = []
        for b in merged:
            mine = np.isin(nearest, list(b[4])) & (near <= 2) & (a > 0)
            cut = grey.copy(); cut[..., 3] = np.where(mine, cut[..., 3], 0)
            ys, xs = np.nonzero(mine)
            r.append(Image.fromarray(np.uint8(np.clip(cut + 0.5, 0, 255)), 'RGBA').crop((xs.min(), ys.min(), xs.max() + 1, ys.max() + 1)))
        out.append(r)
    return out


def place(atlas, cell, ic, k):
    w, h = max(1, round(ic.size[0] * k)), max(1, round(ic.size[1] * k))
    ic = premul_resize(ic, (w, h))
    cx, cy = (cell % COLS) * CELL, (cell // COLS) * CELL
    atlas.paste(ic, (cx + (CELL - w) // 2, cy + (CELL - h) // 2), ic)


def main():
    rows = icons(Image.open(SRC).convert('RGBA'))
    if [len(r) for r in rows] != [len(o) for o in ORDER]:
        print('expected %s icons per row, found %s -- the sheet changed ?' % ([len(o) for o in ORDER], [len(r) for r in rows]))
        return 1
    cells = {}
    for order, r in zip(ORDER, rows):
        for c, ic in zip(order, r):
            cells[c] = ic
    side = CELL - 2 * MARGIN
    atlas = Image.new('RGBA', (CELL * COLS, CELL * ROWS), (0, 0, 0, 0))
    for fill, frame in PAIRS:
        k = side / max(max(cells[frame].size), max(cells[fill].size))
        place(atlas, fill, cells[fill], k)
        place(atlas, frame, cells[frame], k)
    place(atlas, 8, cells[8], side / max(cells[8].size))
    a = np.asarray(atlas, dtype=np.uint8)
    with open(OUT, 'wb') as f:
        f.write(a[..., [2, 1, 0, 3]].copy().tobytes())
    atlas.save(os.path.join(ROOT, 'build', 'net_icons_preview.png'))
    print('wrote', OUT, '(%dx%d, 9 cells)' % atlas.size)
    return 0


if __name__ == '__main__':
    sys.exit(main())
