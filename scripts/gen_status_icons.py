"""gen_status_icons.py -- the PlayOnline status icons, redrawn in HD (assets/status_icons_src/status_icons_grid.png, a
3 x 3 grid on a transparent background, made from the client's keytops3 sprites : IconsHD_ChatGPT/) ->
assets/status_icons.raw : a 512x512 straight-alpha BGRA atlas of 4 x 4 cells of 128 px.

Cells (reading order of the grid) :
  0 online (keytops3 #97)   1 away (#98)        2 invisible (#96)      -- Friend List -> Online Status, the 3 buttons
  3 online small (#64)      4 away small (#65)  5 status 3 (#66)       -- the friend list, before a name
  6 status 4 (#177)         7 unknown (#67)     8 unread message (#158)
Each icon is cut by its own pixels (the 9 biggest blobs, rows then columns ; a big icon spills past its third), fitted (aspect kept) into its cell with a margin, resized
premultiplied (no dark fringe). Usage : python scripts/gen_status_icons.py"""
import os
import numpy as np
from PIL import Image
from scipy import ndimage

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
SRC = os.path.join(ROOT, 'assets', 'status_icons_src', 'status_icons_grid.png')
OUT = os.path.join(ROOT, 'assets', 'status_icons.raw')
CELL, COLS, MARGIN = 128, 4, 4


def premul_resize(im, size):
    a = np.asarray(im, dtype=np.float32) / 255.0
    pm = Image.fromarray(np.uint8(np.clip(np.concatenate([a[..., :3] * a[..., 3:4], a[..., 3:4]], -1) * 255.0 + 0.5, 0, 255)), 'RGBA')
    b = np.asarray(pm.resize(size, Image.LANCZOS), dtype=np.float32) / 255.0
    al = b[..., 3:4]
    rgb = np.where(al > 1e-4, b[..., :3] / np.maximum(al, 1e-4), 0.0)
    return Image.fromarray(np.uint8(np.clip(np.concatenate([rgb, al], -1) * 255.0 + 0.5, 0, 255)), 'RGBA')


def main():
    im = Image.open(SRC).convert('RGBA')
    W, H = im.size
    atlas = Image.new('RGBA', (CELL * COLS, CELL * COLS), (0, 0, 0, 0))
    a0 = np.asarray(im)[..., 3]
    lab, _ = ndimage.label(a0 > 8)
    objs = sorted(ndimage.find_objects(lab), key=lambda s: -(s[0].stop - s[0].start) * (s[1].stop - s[1].start))[:9]
    objs.sort(key=lambda s: s[0].start + s[0].stop)                                          # by height : 3 rows of 3 ...
    objs = [o for r in range(3) for o in sorted(objs[r * 3:r * 3 + 3], key=lambda s: s[1].start)]   # ... each left to right
    assert len(objs) == 9, len(objs)
    for n, sl in enumerate(objs):
        part = im.crop((sl[1].start, sl[0].start, sl[1].stop, sl[0].stop))
        k = (CELL - 2 * MARGIN) / max(part.size)
        part = premul_resize(part, (max(1, round(part.size[0] * k)), max(1, round(part.size[1] * k))))
        cx, cy = (n % COLS) * CELL, (n // COLS) * CELL
        atlas.alpha_composite(part, (cx + (CELL - part.size[0]) // 2, cy + (CELL - part.size[1]) // 2))
    a = np.asarray(atlas)
    with open(OUT, 'wb') as f:
        f.write(a[..., [2, 1, 0, 3]].copy().tobytes())
    atlas.save(os.path.join(ROOT, 'assets', 'status_icons_src', 'atlas_preview.png'))
    print('status_icons.raw : %dx%d, 9 cells of %d px' % (atlas.size[0], atlas.size[1], CELL))


if __name__ == '__main__':
    main()
