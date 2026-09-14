#!/usr/bin/env python3
# gen_grimoire.py -- convert the two Scholar "grimoire" book textures PNG -> raw BGRA for the plugin.
#
# Source : addons/AioHUD/assets/Grimoire-{Light,Dark}-HD.png  (1536x1024 RGBA, straight alpha).
# Output : assets/grimoire_{light,dark}.raw  -- headerless 512x342 straight-alpha BGRA blobs
#          (B,G,R,A per pixel, row-major top-to-bottom, tightly packed), ready for
#          load_raw_texture(dev, path, W=512, H=342). 512*342*4 = 700416 bytes each.
#
# Pipeline (mirrors the byte order of the other .raw generators -- gen_element_icons.py):
#   1. Open the source RGBA, keep its OWN straight alpha (no key / flood-fill).
#   2. Downscale 1536x1024 -> 512x342 (exact /3 on width; 1024/3 = 341.33 -> 342 rounds the
#      3:2 aspect) with LANCZOS in premultiplied space so transparent RGB can't bleed a fringe
#      into the anti-aliased edges, then un-premultiply back to straight alpha.
#   3. Reorder RGBA -> BGRA and write the raw blob.
#
# Pure asset tooling -- no game/RE. Re-run if the source book art changes.

import os
import sys
import numpy as np
from PIL import Image

import genpaths

ROOT = genpaths.ROOT
# the HD book art lives with the addon, not under assets/*_src/ (large shared source) : $WINDOWER_ADDONS, else the
# dev install. It used to be <repo>\..\..\addons -- right while the repo sat inside Windower\, a missing folder since.
SRC_DIR = os.path.join(genpaths.addons_dir(), 'AioHUD', 'assets')
OUT_DIR = os.path.join(ROOT, 'assets')

W, H = 512, 342                                # 3:2 book, 1536x1024 divided by 3

BOOKS = [
    ('Grimoire-Light-HD.png',  'grimoire_light.raw'),
    ('Grimoire-Dark-HD.png',   'grimoire_dark.raw'),
    ('Grimoire-Closed-HD.png', 'grimoire_closed.raw'),   # shown when the SCH has NO Arts / Addendum (a closed book, no charges/recast)
]


def convert(src_path):
    """Return a HxWx4 uint8 RGBA array downscaled to W x H, straight alpha preserved."""
    img = Image.open(src_path).convert('RGBA')
    a = np.asarray(img).astype(np.float64)              # H,W,4 -- keep the REAL straight alpha
    alpha = a[..., 3]

    pm = a[..., 0:3] * (alpha[..., None] / 255.0)       # premultiply so transparent RGB can't bleed
    pm_s = np.stack([np.asarray(Image.fromarray(pm[..., c].astype(np.uint8)).resize((W, H), Image.LANCZOS))
                     for c in range(3)], axis=2).astype(np.float64)
    a_s = np.asarray(Image.fromarray(alpha.astype(np.uint8)).resize((W, H), Image.LANCZOS)).astype(np.float64)
    with np.errstate(divide='ignore', invalid='ignore'):
        rgb_s = np.where(a_s[..., None] > 0, pm_s / (a_s[..., None] / 255.0), 0)   # un-premultiply -> straight
    rgb_s = np.clip(rgb_s, 0, 255)

    out = np.zeros((H, W, 4), np.uint8)
    out[..., 0:3] = rgb_s.astype(np.uint8)
    out[..., 3] = np.clip(a_s, 0, 255).astype(np.uint8)
    return out


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    # ALL sources first : checking inside the loop wrote two books and then died on the third, a half-updated set.
    missing = [os.path.join(SRC_DIR, s) for s, _ in BOOKS if not os.path.isfile(os.path.join(SRC_DIR, s))]
    if missing:
        sys.exit('source not found: %s -- set WINDOWER_ADDONS to your Windower addons folder' % '; '.join(missing))
    for src_name, out_name in BOOKS:
        src_path = os.path.join(SRC_DIR, src_name)
        rgba = convert(src_path)
        bgra = rgba[..., [2, 1, 0, 3]].tobytes()        # straight-alpha BGRA (B,G,R,A)
        out_path = os.path.join(OUT_DIR, out_name)
        with open(out_path, 'wb') as f:
            f.write(bgra)
        assert len(bgra) == W * H * 4, 'unexpected size'
        print('[grimoire] OK -> %s  (%d bytes)  %dx%d  straight-alpha BGRA'
              % (out_path, len(bgra), W, H))


if __name__ == '__main__':
    main()
