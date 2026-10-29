#!/usr/bin/env python3
"""Convert the BST box's icons into the RUNTIME folder, never into the repo (as the pet art: scripts/gen_bst_pet_art.py).

    python scripts/gen_bst_icons.py --src <folder> --map scripts/bst_icon_map.txt

Each icon becomes plugins\\AioHud\\assets\\bstpets\\<key>.raw : 64 x 64, BGRA, top-down, no header (load_raw_texture).
  charge       the Ready charge's paw: a WHITE silhouette the box tints with the theme. Its alpha is made solid
               (an image generator leaves a near-transparent haze around the drawing: invisible on a light page,
               a grey veil on the game's dark background), specks taken out.
  eco_<Family> a family of the correlation line: the player's own pet drawing in colour. Only its biggest
               creature is kept (each drawing has a small prey in a corner), cropped square around it.
A missing file: that icon is left out and said so; the box then draws without it (the dial, the names alone).
"""
import argparse, io, os, sys
from collections import deque
import genpaths
from PIL import Image, ImageFilter

SIDE = 64
WORK = 256   # the side the creature is found at


def to_raw(im, out):
    r, g, b, a = im.convert('RGBA').split()
    open(out, 'wb').write(Image.merge('RGBA', (b, g, r, a)).tobytes())


def square(im, box, margin=0.06):
    """The part `box` of `im`, centred in a transparent square with a small margin."""
    part = im.crop(box)
    side = int(max(part.size) * (1 + 2 * margin))
    sq = Image.new('RGBA', (side, side), (0, 0, 0, 0))
    sq.paste(part, ((side - part.width) // 2, (side - part.height) // 2))
    return sq


def paw(path):
    im = Image.open(path).convert('RGBA')
    a = im.split()[3].point(lambda v: 255 if v >= 128 else 0).filter(ImageFilter.MedianFilter(9))
    white = Image.merge('RGBA', (Image.new('L', im.size, 255),) * 3 + (a,))
    return square(white, a.getbbox() or (0, 0) + im.size).resize((SIDE, SIDE), Image.LANCZOS)


def biggest_creature(path, side=SIDE):
    """The drawing with only its largest blob of opaque pixels (the creature), cropped around it."""
    im = Image.open(path).convert('RGBA')
    small = im.split()[3].resize((WORK, WORK), Image.BILINEAR)
    px = small.load()
    seen = [[False] * WORK for _ in range(WORK)]
    best = []
    for y in range(WORK):
        for x in range(WORK):
            if seen[y][x] or px[x, y] < 40:
                continue
            blob, q = [], deque([(x, y)])
            seen[y][x] = True
            while q:
                cx, cy = q.popleft()
                blob.append((cx, cy))
                for nx, ny in ((cx + 1, cy), (cx - 1, cy), (cx, cy + 1), (cx, cy - 1)):
                    if 0 <= nx < WORK and 0 <= ny < WORK and not seen[ny][nx] and px[nx, ny] >= 40:
                        seen[ny][nx] = True
                        q.append((nx, ny))
            if len(blob) > len(best):
                best = blob
    mask = Image.new('L', (WORK, WORK), 0)
    mp = mask.load()
    for x, y in best:
        mp[x, y] = 255
    mask = mask.filter(ImageFilter.MaxFilter(5)).resize(im.size, Image.BILINEAR)
    r, g, b, a = im.split()
    a = Image.composite(a, Image.new('L', im.size, 0), mask)
    kept = Image.merge('RGBA', (r, g, b, a))
    return square(kept, a.getbbox() or (0, 0) + im.size).resize((side, side), Image.LANCZOS)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--src', required=True)
    ap.add_argument('--map', required=True)
    ap.add_argument('--out', help='default: <Windower>\\plugins\\AioHud\\assets\\bstpets')
    a = ap.parse_args()
    out = a.out or os.path.join(os.path.dirname(genpaths.addons_dir()), 'plugins', 'AioHud', 'assets', 'bstpets')
    os.makedirs(out, exist_ok=True)
    n = 0
    for line in io.open(a.map, encoding='utf-8'):
        line = line.strip()
        if not line or line.startswith('#') or '=' not in line:
            continue
        key, pic = (x.strip() for x in line.split('=', 1))
        path = pic if os.path.isabs(pic) else os.path.join(a.src, pic)
        if not os.path.isfile(path):
            print('  missing', path); continue
        icon = paw(path) if key == 'charge' else biggest_creature(path)
        to_raw(icon, os.path.join(out, key + '.raw')); n += 1
        print('  %s <- %s' % (key, os.path.basename(path)))
    print('wrote %d icon(s) -> %s' % (n, out))
    return 0 if n else 1


if __name__ == '__main__':
    sys.exit(main())
