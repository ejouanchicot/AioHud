#!/usr/bin/env python3
# gen_moon_dial.py -- the moon-phase dial's face : assets/moon_dial_src/moon_dial.png (a parchment disc, a continuous
# midnight-blue band with the eight golden phases on it : NEW at the bottom, waxing up the LEFT, FULL at the top, waning down the RIGHT ; the centre empty ;
# transparent around the disc) -> assets/moon_dial.raw, a 512x512 straight-alpha BGRA texture (loaded with mipmaps).
# The minimap draws it as the moon dial's face and its metal hand over it (src/ui/minimap.cpp, the modules style).
#   python scripts/gen_moon_dial.py
import os
import sys
import numpy as np
from PIL import Image, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, 'assets', 'moon_dial_src', 'moon_dial.png')
OUT = os.path.join(ROOT, 'assets', 'moon_dial.raw')
SIZE = 512


def main():
    im = Image.open(SRC).convert('RGBA')
    a = np.asarray(im)[..., 3]
    ys, xs = np.nonzero(a > 20)
    x0, x1, y0, y1 = xs.min(), xs.max() + 1, ys.min(), ys.max() + 1
    side = max(x1 - x0, y1 - y0)
    cx, cy = (x0 + x1) // 2, (y0 + y1) // 2
    im = im.crop((cx - side // 2, cy - side // 2, cx - side // 2 + side, cy - side // 2 + side))
    # premultiplied resize : no dark fringe from the transparent RGB
    arr = np.asarray(im).astype(np.float32) / 255.0
    pm = np.concatenate([arr[..., :3] * arr[..., 3:4], arr[..., 3:4]], -1)
    pm = Image.fromarray(np.uint8(np.clip(pm * 255.0 + 0.5, 0, 255)), 'RGBA').resize((SIZE, SIZE), Image.LANCZOS)
    pm = pm.filter(ImageFilter.UnsharpMask(radius=1.2, percent=120, threshold=2))   # keep the thin ink lines crisp at small size
    b = np.asarray(pm).astype(np.float32) / 255.0
    al = b[..., 3:4]
    rgb = np.where(al > 1e-4, b[..., :3] / np.maximum(al, 1e-4), 0.0)
    out = np.uint8(np.clip(np.concatenate([rgb, al], -1) * 255.0 + 0.5, 0, 255))
    with open(OUT, 'wb') as f:
        f.write(out[..., [2, 1, 0, 3]].copy().tobytes())
    print('wrote', OUT, '(%dx%d)' % (SIZE, SIZE))
    return 0


if __name__ == '__main__':
    sys.exit(main())
