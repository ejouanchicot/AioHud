#!/usr/bin/env python3
# gen_astral.py -- the ASTRAL COMPASS kit (assets/astral_src/<palette>/*.png, manifest.json) -> the runtime textures
# assets/astral/<palette>_<piece>.raw (straight-alpha BGRA, what load_raw_texture_mip reads).
#
# The kit is authored at 1024 (ring layers) / 128 (pastille, letters, cartouches) / 256 (moon satellite). The ring is
# shown at 200..400 px, so the ring layers are halved to 512 (4 MB -> 1 MB each) and the small pieces to half too ;
# the mip chain covers the rest. The resize runs in PREMULTIPLIED space (no dark fringe on the alpha edges).
# Geometry (radii, pivots, the cartouche text areas) is in manifest.json and NOT changed by the scaling : the
# renderer works in fractions of the canvas.
#
#   python scripts/gen_astral.py            (both palettes)
import os
import sys
import numpy as np
from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
SRC = os.path.join(ROOT, 'assets', 'astral_src')
OUT = os.path.join(ROOT, 'assets', 'astral')

# piece -> output size (w, h)
PIECES = {
    'lentille_ombre': (512, 512), 'anneau_base': (512, 512), 'anneau_graduations': (512, 512), 'anneau_reflet': (512, 512),
    'pastille_cardinal': (64, 64), 'lettre_N': (64, 64), 'lettre_E': (64, 64), 'lettre_S': (64, 64), 'lettre_W': (64, 64),
    'satellite_lune': (128, 128),
    'cartouche_haut_gauche': (32, 64), 'cartouche_haut_milieu': (16, 64), 'cartouche_haut_droite': (32, 64),
    'cartouche_bas_gauche': (32, 64), 'cartouche_bas_milieu': (16, 64), 'cartouche_bas_droite': (32, 64),
    'separateur': (8, 32),
}
PALETTES = ['laiton', 'gris']   # laiton : the brass as painted ; gris : the same in grey, tinted by the HUD theme's metal


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
    os.makedirs(OUT, exist_ok=True)
    total = 0
    for pal in PALETTES:
        for name, size in PIECES.items():
            src = os.path.join(SRC, pal, name + '.png')
            im = Image.open(src).convert('RGBA')
            if im.size != size:
                im = premul_resize(im, size)
            a = np.asarray(im, dtype=np.uint8)
            bgra = a[..., [2, 1, 0, 3]].copy()
            dst = os.path.join(OUT, '%s_%s.raw' % (pal, name))
            with open(dst, 'wb') as f:
                f.write(bgra.tobytes())
            total += bgra.nbytes
    print('wrote %d piece(s) x %d palette(s) to %s (%d KB)' % (len(PIECES), len(PALETTES), OUT, total // 1024))
    return 0


if __name__ == '__main__':
    sys.exit(main())
