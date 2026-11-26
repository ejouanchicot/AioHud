"""gen_plate_icons.py -- the NAME PLATE icons (the glyphs FFXI draws before a name over a character : status, linkshell,
bazaar, Mentor, nations... docs/notes/nameplate-re-2026-11-26.md s.2), redrawn in HD (IconsHD_ChatGPT/etiquettes/) ->
assets/plate_icons.raw (+ assets/pearl_hd.raw, the pearl alone, 128 px) : a 512x512 straight-alpha BGRA atlas of 8 x 8 cells of 64 px. The cell of a glyph = its code
- 0x8E (0x8E..0xB1 : cells 0..35) ; a cell left empty = an icon not redrawn yet (AioHUD keeps its fallback there).

SOURCES : each sheet (assets/plate_icons_src/) with the glyph codes of its icons in reading order (rows top to bottom,
each left to right) ; the icons are cut by their own pixels (the biggest blobs). Add a sheet when ChatGPT delivers one.
SINGLES : one icon per file, named XX_name.png (XX = the glyph code) -- they WIN over a sheet's (drawn after).
Usage : python scripts/gen_plate_icons.py"""
import os
import numpy as np
from PIL import Image
from scipy import ndimage

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
SRC = os.path.join(ROOT, 'assets', 'plate_icons_src')
OUT = os.path.join(ROOT, 'assets', 'plate_icons.raw')
PEARL = os.path.join(ROOT, 'assets', 'pearl_hd.raw')   # the linkshell pearl alone, 128 x 128 : AioHUD recolours it per LS colour (its sheen kept white)
CELL, COLS, MARGIN, FIRST = 64, 8, 2, 0x8E

SOURCES = [
    ('sheet_b1_9e.png', 5, [0xB1, 0xB0, 0xAF, 0xAE, 0xAD,      # info, moon, snowflake, blossom, gold star (Master Level)
                            0xAC, 0xAB, 0xAA, 0x94, 0xA8,      # "!" star, Monstrosity, rank star, SWORD + SHIELD (0x94 : ChatGPT drew it
                                                               # in A9's place -- A9 is a small lone sword, not redrawn yet), Level Sync
                            0xA6, 0xA7, 0xA5, 0xA4, 0xA3,      # red / blue banners, red shard, green / blue griffins
                            0xA2, 0xA1, 0xA0, 0x9F, 0x9E]),    # Mentor, new adventurer, Windurst, Bastok, San d'Oria
]


def premul_resize(im, size):
    a = np.asarray(im, dtype=np.float32) / 255.0
    pm = Image.fromarray(np.uint8(np.clip(np.concatenate([a[..., :3] * a[..., 3:4], a[..., 3:4]], -1) * 255.0 + 0.5, 0, 255)), 'RGBA')
    b = np.asarray(pm.resize(size, Image.LANCZOS), dtype=np.float32) / 255.0
    al = b[..., 3:4]
    rgb = np.where(al > 1e-4, b[..., :3] / np.maximum(al, 1e-4), 0.0)
    return Image.fromarray(np.uint8(np.clip(np.concatenate([rgb, al], -1) * 255.0 + 0.5, 0, 255)), 'RGBA')


def pearl_light(alpha, N=128):
    """the CLIENT's pearl, smooth and HD (its own art, tinted by multiply, measured 2026-11-26) : the BODY is an ellipsoid
    fitted to the shape (its stem opened away), the stem a small dome of its own ; lit as an environment reflection -- a
    bright sky cap with a soft highlight, a soft dark belt just under the middle, a lighter ground bounce, the edge a touch
    darker. -> (body light 0..1.3, sheen 0..0.85)"""
    m = alpha > 128
    body = ndimage.binary_opening(m, iterations=max(2, N // 14))
    ys, xs = np.nonzero(body)
    cy, cx = (ys.min() + ys.max()) / 2.0, (xs.min() + xs.max()) / 2.0
    ry, rx = (ys.max() - ys.min()) / 2.0 + 0.5, (xs.max() - xs.min()) / 2.0 + 0.5
    Y, X = np.mgrid[0:alpha.shape[0], 0:alpha.shape[1]].astype(np.float32)
    ex, ey = (X - cx) / rx, (Y - cy) / ry
    nz_e = np.sqrt(np.clip(1.0 - ex * ex - ey * ey, 0.0, 1.0))
    n = np.stack([ex, ey, nz_e], -1)
    # the stem (in the shape, out of the ellipse) : a dome from its own distance to the edge
    stem = m & (ex * ex + ey * ey > 0.98)
    if stem.any():
        d = ndimage.distance_transform_edt(m); h = ndimage.gaussian_filter(np.sqrt(np.clip(d / max(d[stem].max(), 1.0), 0, 1)), 1.5)
        gy, gx = np.gradient(h * N * 0.12)
        ns = np.stack([-gx, -gy, np.ones_like(h)], -1)
        n = np.where(stem[..., None], ns, n)
    n = n / np.maximum(np.linalg.norm(n, axis=-1, keepdims=True), 1e-6)
    r = 2.0 * n[..., 2:3] * n - np.array([0, 0, 1.0])               # the view reflected
    up = -r[..., 1]                                                  # screen y is down : up > 0 = the sky
    belt = np.exp(-((up + 0.10) / 0.32) ** 2)
    env = 0.66 + 0.38 * np.clip(up, 0, 1) ** 0.8 + 0.20 * np.clip(-up, 0, 1) ** 1.5 - 0.18 * belt
    lum = ndimage.gaussian_filter(env * (0.80 + 0.20 * np.clip(n[..., 2], 0, 1) ** 0.35), N / 160.0)
    L = np.array([-0.35, -0.75, 0.56]); L /= np.linalg.norm(L)       # the key light : top, slightly left
    H = L + np.array([0.0, 0.0, 1.0]); H /= np.linalg.norm(H)
    spec = ndimage.gaussian_filter(np.clip((n * H).sum(-1), 0, 1) ** 40, N / 90.0)
    return np.clip(lum, 0, 1.3), np.clip(spec * 1.3, 0, 0.85)


def cut(im, n, cols):
    a0 = np.asarray(im)[..., 3]
    lab, _ = ndimage.label(a0 > 24)
    objs = sorted(ndimage.find_objects(lab), key=lambda s: -(s[0].stop - s[0].start) * (s[1].stop - s[1].start))[:n]
    assert len(objs) == n, (len(objs), n)
    objs.sort(key=lambda s: s[0].start + s[0].stop)                                     # rows of `cols` ...
    objs = [o for r in range(0, n, cols) for o in sorted(objs[r:r + cols], key=lambda s: s[1].start)]   # ... each left to right
    return [im.crop((s[1].start, s[0].start, s[1].stop, s[0].stop)) for s in objs]


def main():
    atlas = Image.new('RGBA', (CELL * COLS, CELL * COLS), (0, 0, 0, 0))
    have = []
    singles = sorted(f for f in os.listdir(SRC) if len(f) > 3 and f[2] == '_' and f.lower().endswith('.png') and all(c in '0123456789ABCDEFabcdef' for c in f[:2]))
    jobs = [(Image.open(os.path.join(SRC, fn)).convert('RGBA'), cols, codes) for fn, cols, codes in SOURCES]
    jobs += [(Image.open(os.path.join(SRC, fn)).convert('RGBA'), 1, [int(fn[:2], 16)]) for fn in singles]
    for im, cols, codes in jobs:
        for code, part in zip(codes, cut(im, len(codes), cols)):
            k = (CELL - 2 * MARGIN) / max(part.size)
            part = premul_resize(part, (max(1, round(part.size[0] * k)), max(1, round(part.size[1] * k))))
            c = code - FIRST
            cx, cy = (c % COLS) * CELL, (c // COLS) * CELL
            atlas.paste((0, 0, 0, 0), (cx, cy, cx + CELL, cy + CELL))   # a single replaces the sheet's icon
            atlas.alpha_composite(part, (cx + (CELL - part.size[0]) // 2, cy + (CELL - part.size[1]) // 2))
            have.append(code)
    a = np.asarray(atlas)
    with open(OUT, 'wb') as f:
        f.write(a[..., [2, 1, 0, 3]].copy().tobytes())
    atlas.save(os.path.join(SRC, 'atlas_preview.png'))
    # the pearl : a LIGHTING MAP of its shape (ChatGPT's, hook included), recoloured at run time per LS colour. Its own
    # painted light made a hard white dot once tinted (2026-11-26) : the shape is re-lit smooth -- a dome from the
    # distance to the edge, a soft top-left key light, a broad soft sheen. Channels : R the body light (0..1.3 -> 0..255),
    # G the sheen (0..1), A the shape ; AioHUD : out = rgb * body, then towards white by the sheen
    # the SHAPE : the client's own pearl silhouette (pearl_shape.png = its 64 px art), upscaled smooth -- ChatGPT's had a
    # notch carved into the ball (2026-11-26 : "deforme") where the client's has a small stem on top right
    pf = os.path.join(SRC, 'pearl_shape.png')
    if os.path.exists(pf):
        src = Image.open(pf).convert('RGBA')
        a0 = np.asarray(src)[..., 3].astype(np.float32) / 255.0
        ys, xs = np.nonzero(a0 > 0.5); a0 = a0[ys.min():ys.max() + 1, xs.min():xs.max() + 1]
        big = np.asarray(Image.fromarray(np.uint8(a0 * 255)).resize((a0.shape[1] * 8, a0.shape[0] * 8), Image.BICUBIC), np.float32) / 255.0
        big = ndimage.gaussian_filter(big, 6.0)
        big = np.clip((big - 0.5) * 6.0 + 0.5, 0, 1)                       # a smooth, crisp edge
        k = (128 - 6) / max(big.shape)
        sm = Image.fromarray(np.uint8(big * 255)).resize((max(1, round(big.shape[1] * k)), max(1, round(big.shape[0] * k))), Image.LANCZOS)
        canvas = Image.new('L', (128, 128), 0); canvas.paste(sm, ((128 - sm.size[0]) // 2, (128 - sm.size[1]) // 2))
        alpha = np.asarray(canvas)
        lum, spec = pearl_light(alpha)
        out = np.zeros((128, 128, 4), np.uint8)
        out[..., 2] = np.uint8(np.clip(lum / 1.3 * 255.0 + 0.5, 0, 255))   # BGRA in the file : R = channel 2
        out[..., 1] = np.uint8(np.clip(spec * 255.0 + 0.5, 0, 255))
        out[..., 3] = alpha
        with open(PEARL, 'wb') as f:
            f.write(out.tobytes())
        print('pearl_hd.raw : 128x128')
    have = sorted(set(have)); print('plate_icons.raw : %dx%d, %d icon(s) : %s' % (atlas.size[0], atlas.size[1], len(have), ' '.join('%02X' % c for c in have)))


if __name__ == '__main__':
    main()
