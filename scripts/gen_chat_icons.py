#!/usr/bin/env python3
# gen_chat_icons.py -- build the Chat module's tab icon strip (the mother-of-pearl set, 2026-11-16).
#
# Source : assets/chat_icons_src/<name>.png  (white mother-of-pearl renders with their own soft glow, real
#          transparency in the alpha channel).
# Output : assets/chat_icons.raw  -- a 512x64 horizontal strip of 8 cells (64x64 each), straight-alpha BGRA
#          (B,G,R,A per pixel, row-major, tightly packed) for load_raw_texture_mip(). 512*64*4 = 131072 bytes.
#
# Cell order MUST match hud_chat.cpp's TabIcon enum :
#   0 TI_ALL  1 TI_PARTY  2 TI_LS  3 TI_TELL  4 TI_SAY  5 TI_SYS  6 TI_SWORDS  7 TI_ME
# TI_ALL's cell stays EMPTY : the bubble is Say's alone (the user, 2026-11-16) -- "all" keeps its drawn icon.
#
# Pipeline (as gen_element_icons.py) : the source's OWN alpha, trimmed to its bounding box (alpha > 8, the glow
# included), fit CENTERED into the cell with a small margin, LANCZOS in premultiplied space (no dark fringe), then
# straight-alpha BGRA. The icons are WHITE on purpose : the chat tints them with each channel's colour.

import os
import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, 'assets', 'chat_icons_src')
OUT = os.path.join(ROOT, 'assets', 'chat_icons.raw')
CELL, MARGIN, CELLS = 64, 2, 8
ORDER = [None, 'party.png', 'pearl.png', 'envelope.png', 'bubble.png', 'gear.png', 'swords.png', 'bust.png']


def premul_resize(im, size):
    a = np.asarray(im, dtype=np.float32) / 255.0
    rgb = a[..., :3] * a[..., 3:4]
    pm = Image.fromarray(np.uint8(np.clip(np.concatenate([rgb, a[..., 3:4]], -1) * 255.0 + 0.5, 0, 255)), 'RGBA')
    pm = pm.resize(size, Image.LANCZOS)
    b = np.asarray(pm, dtype=np.float32) / 255.0
    al = b[..., 3:4]
    rgb = np.where(al > 1e-4, b[..., :3] / np.maximum(al, 1e-4), 0.0)
    return Image.fromarray(np.uint8(np.clip(np.concatenate([rgb, al], -1) * 255.0 + 0.5, 0, 255)), 'RGBA')


def main():
    strip = Image.new('RGBA', (CELL * CELLS, CELL), (0, 0, 0, 0))
    for i, name in enumerate(ORDER):
        if not name: continue
        im = Image.open(os.path.join(SRC, name)).convert('RGBA')
        box = im.getchannel('A').point(lambda v: 255 if v > 8 else 0).getbbox()
        im = im.crop(box)
        side = CELL - 2 * MARGIN
        k = side / max(im.size)
        w, h = max(1, round(im.size[0] * k)), max(1, round(im.size[1] * k))
        im = premul_resize(im, (w, h))
        strip.paste(im, (i * CELL + (CELL - w) // 2, (CELL - h) // 2), im)
    a = np.asarray(strip, dtype=np.uint8)
    bgra = a[..., [2, 1, 0, 3]].copy()
    with open(OUT, 'wb') as f:
        f.write(bgra.tobytes())
    print('wrote', OUT, bgra.nbytes, 'bytes')


if __name__ == '__main__':
    main()
