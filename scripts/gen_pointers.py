# gen_pointers.py -- builds assets/pointers.raw : the selectable cursor styles (Party > Cursor style).
#
# Sources : assets/pointers_src/<n>_<name>.dds, the client's "anc anc" pointer texture (a 64x64 DXT3 sheet of 6
# animation frames, 2 columns x 3 rows of 32 x 21.33 px, played row by row) -- the XiView HD pack's variants.
# Output  : 1024 x 512 BGRA, rows = styles (style 1 = row 0), columns 0..5 = the frames, 128 px cells.
# Every style is fitted the SAME way as assets/hand_cursor.raw (the hand, style 0) : its union over the 6 frames
# fills at most 80 x 60 px of the cell, the TIP at x = 111 and the middle at y = 61, so swapping the style keeps
# the cursor pointing at the same spot.
#   python scripts/gen_pointers.py
import glob, os
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC, OUT = os.path.join(ROOT, 'assets', 'pointers_src'), os.path.join(ROOT, 'assets', 'pointers.raw')
CELL, COLS, W, H = 128, 8, 1024, 512
TIP_X, MID_Y, FIT_W, FIT_H = 111, 61, 80, 60

def frames(path):
    im = Image.open(path).convert('RGBA')
    out = []
    for k in range(6):
        c, r = k % 2, k // 2
        y0, y1 = round(r * 64 / 3), round((r + 1) * 64 / 3)
        out.append(im.crop((c * 32, y0, c * 32 + 32, y1)))
    return out

def main():
    srcs = sorted(glob.glob(os.path.join(SRC, '*.dds')))
    assert 0 < len(srcs) <= H // CELL, srcs
    sheet = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    for row, p in enumerate(srcs):
        fr = frames(p)
        boxes = [f.getchannel('A').point(lambda a: 255 if a > 8 else 0).getbbox() for f in fr]
        boxes = [b for b in boxes if b]
        ux0, uy0 = min(b[0] for b in boxes), min(b[1] for b in boxes)
        ux1, uy1 = max(b[2] for b in boxes), max(b[3] for b in boxes)
        s = min(FIT_W / (ux1 - ux0), FIT_H / (uy1 - uy0))
        for k, f in enumerate(fr):
            # LANCZOS in premultiplied space : straight alpha would bleed the transparent black into the edges
            big = f.convert('RGBa').resize((round(f.width * s), round(f.height * s)), Image.LANCZOS).convert('RGBA')
            ox = round(TIP_X - ux1 * s)
            oy = round(MID_Y - (uy0 + uy1) * 0.5 * s)
            cell = Image.new('RGBA', (CELL, CELL), (0, 0, 0, 0))
            cell.alpha_composite(big, (max(ox, 0), max(oy, 0)), (max(-ox, 0), max(-oy, 0)))
            sheet.alpha_composite(cell, (k * CELL, row * CELL))
        print('%-24s union %dx%d  x%.2f' % (os.path.basename(p), ux1 - ux0, uy1 - uy0, s))
    # premultiplied-free straight BGRA, the layout load_raw_texture expects
    open(OUT, 'wb').write(sheet.tobytes('raw', 'BGRA'))
    print('wrote', OUT, os.path.getsize(OUT))

if __name__ == '__main__':
    main()
