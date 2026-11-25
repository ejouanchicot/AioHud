#!/usr/bin/env python3
# gen_metal_grain.py -- the Dial's METAL MATERIAL (the user 2026-11-25 : "now the quality of the metal's texture") : a
# tileable 256 x 256 detail layer laid over the flat metal of the collar and the bezel, under their lighting. Cast and worn
# Bastokan metal : a soft low-frequency mottling (patina), a fine grain, a few small casting pits and faint scratches.
# Encoded as a SIGNED overlay : a lighter texel is white with alpha = its excess, a darker one black with alpha = its
# deficit -- alpha-blended over any metal tint, it only modulates. -> assets/metal_grain.raw (BGRA), loaded with mipmaps,
# drawn with WRAP addressing.
#   python scripts/gen_metal_grain.py
import os
import sys
import numpy as np
from scipy import ndimage

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'assets', 'metal_grain.raw')
N = 256


def noise(rng, sigma):
    n = ndimage.gaussian_filter(rng.standard_normal((N, N)), sigma, mode='wrap')
    return n / (np.abs(n).max() + 1e-9)


def main():
    rng = np.random.default_rng(1891)                    # fixed : the same metal every build
    v = 0.55 * noise(rng, 22.0) + 0.30 * noise(rng, 7.0) + 0.18 * noise(rng, 1.6) + 0.10 * noise(rng, 0.7)
    # casting pits : small dark dots with a lighter rim
    yy, xx = np.mgrid[0:N, 0:N]
    for _ in range(26):
        px, py, pr = rng.uniform(0, N), rng.uniform(0, N), rng.uniform(0.8, 2.0)
        dx = (xx - px + N / 2) % N - N / 2; dy = (yy - py + N / 2) % N - N / 2
        d = np.sqrt(dx * dx + dy * dy)
        v -= 0.9 * np.exp(-(d / pr) ** 2)
        v += 0.25 * np.exp(-((d - pr * 1.4) / (pr * 0.6)) ** 2)
    # faint scratches : thin random lines
    for _ in range(7):
        x0, y0, ang, L = rng.uniform(0, N), rng.uniform(0, N), rng.uniform(0, np.pi), rng.uniform(10, 35)
        t = np.linspace(0, L, int(L * 3))
        for q in t:
            x = int(x0 + np.cos(ang) * q) % N; y = int(y0 + np.sin(ang) * q) % N
            v[y, x] += 0.12
    v = v - v.mean()
    v = v / (np.percentile(np.abs(v), 99.5) + 1e-9)
    v = np.clip(v, -1.0, 1.0)
    a = np.uint8(np.clip(np.abs(v) * 255.0, 0, 255))
    c = np.where(v > 0, 255, 0).astype(np.uint8)
    out = np.stack([c, c, c, a], -1)                   # BGRA (grey : B = G = R)
    with open(OUT, 'wb') as f:
        f.write(out.tobytes())
    from PIL import Image
    prev = np.uint8(np.clip(128 + v * 90, 0, 255))
    Image.fromarray(prev, 'L').save(os.path.join(ROOT, 'build', 'metal_grain_preview.png'))
    print('wrote', OUT, '(256x256)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
