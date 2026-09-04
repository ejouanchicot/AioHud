# gen_logo.py -- bake the AioHUD lockup into the raw BGRA the plugin loads, plus the metrics the header needs.
#
# Source : assets/logo_src/aiohud_logo.png, an image-model render. Two shapes have come out of that pipeline and
# both are handled, because which one you get is not up to you:
#   ONE band of content  -> the lockup is already horizontal. Crop it tight and fit it to the canvas.
#   TWO bands            -> emblem stacked over wordmark. Unusable as-is: a masthead band is ~90px tall, and
#                           stacked the word lands about nine pixels high. Cut the halves apart and set them
#                           side by side, emblem at full height, word at 42% of it.
#
# FRINGE. A render composited against a dark backdrop that was then removed leaves the backdrop's colour in the
# RGB of every partially-transparent pixel -- a coloured rim over whatever it is drawn on. Dividing RGB by alpha
# undoes exactly that. But it RUINS a clean straight-alpha render, turning soft edges into a bright halo, so it
# is applied only on evidence: un-premultiply a sample of edge pixels and compare them to their nearest opaque
# neighbour. Contaminated edges come back at roughly the neighbour's brightness (ratio ~1); clean ones come back
# far brighter, because their RGB was never multiplied down in the first place.
#
# Outputs : assets/aiohud_logo.raw (1024x256 BGRA, straight alpha) and src/ui/logo_metrics.h, which tells the
# header where the art actually sits inside that canvas -- so a render of any proportion lands in the right
# place without anyone editing a magic number.
import os, sys
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC  = os.path.join(ROOT, 'assets', 'logo_src', 'aiohud_logo.png')
OUT  = os.path.join(ROOT, 'assets', 'aiohud_logo.raw')
MET  = os.path.join(ROOT, 'src', 'ui', 'logo_metrics.h')
W_OUT, H_OUT = 1024, 256

# WHAT to bake out of the render. The symbol went through five concepts -- a gem in a broken ring, a flat road
# sign, a crystal A, a ring of six -- and none earned its place beside the word, so the mark IS the word. That is
# a decision, not a retreat: a logotype with nothing else in it is a perfectly good answer, and this one is
# genuinely well drawn. 'lockup' bakes the whole render, 'word' the part right of the widest internal gap,
# 'symbol' the part left of it -- so putting a mark back later is a one-word change here, not a rewrite.
MODE = 'word'

im = Image.open(SRC).convert('RGBA')
w, h = im.size
a = im.split()[3]

rows = [max(a.crop((0, y, w, y + 1)).getdata()) for y in range(h)]
bands, s = [], None
for y, v in enumerate(rows):
    if v > 8 and s is None: s = y
    elif v <= 8 and s is not None: bands.append((s, y)); s = None
if s is not None: bands.append((s, h))
if not bands: sys.exit('gen_logo: the source is empty')

def tight(y0, y1):
    cols = [max(a.crop((x, y0, x + 1, y1)).getdata()) for x in range(w)]
    xs = [x for x, v in enumerate(cols) if v > 8]
    return (xs[0], y0, xs[-1] + 1, y1)

def split_h(bb):
    # The widest run of empty COLUMNS inside a horizontal lockup is the gap between mark and word.
    cc = [max(a.crop((x, bb[1], x + 1, bb[3])).getdata()) for x in range(bb[0], bb[2])]
    runs, st = [], None
    for i, v in enumerate(cc):
        if v <= 8 and st is None: st = i
        elif v > 8 and st is not None: runs.append((st, i)); st = None
    runs = [r for r in runs if r[1] - r[0] > (bb[2] - bb[0]) * 0.02]
    if not runs: return None
    g = max(runs, key=lambda r: r[1] - r[0])
    return (bb[0] + g[0], bb[0] + g[1])

out = Image.new('RGBA', (W_OUT, H_OUT), (0, 0, 0, 0))
if len(bands) == 1:
    b = tight(*bands[0])
    if MODE in ('word', 'symbol'):
        g = split_h(b)
        if not g: sys.exit('gen_logo: MODE=%s but the render has no internal gap to split on' % MODE)
        im = im.crop((g[1], b[1], b[2], b[3])) if MODE == 'word' else im.crop((b[0], b[1], g[0], b[3]))
        a = im.split()[3]
        # Re-tighten at the SAME threshold the band scan used. getbbox() would not do: the render carries a very
        # faint alpha haze (values of 1..8) over the whole frame, so "any non-zero pixel" is the entire canvas,
        # and the crop came out 2.35:1 instead of the word's true 4.7:1 -- baked at less than half the size it
        # should have been. A threshold is what separates the artwork from the air around it.
        cw, ch = im.size
        rr = [max(a.crop((0, y, cw, y + 1)).getdata()) for y in range(ch)]
        ys = [y for y, v in enumerate(rr) if v > 8]
        if not ys: sys.exit('gen_logo: MODE=%s selected an empty region' % MODE)
        cc2 = [max(a.crop((x, ys[0], x + 1, ys[-1] + 1)).getdata()) for x in range(cw)]
        xs2 = [x for x, v in enumerate(cc2) if v > 8]
        b = (xs2[0], ys[0], xs2[-1] + 1, ys[-1] + 1)
    aw, ah = b[2] - b[0], b[3] - b[1]
    sc = min(W_OUT / float(aw), H_OUT / float(ah))
    lw, lh = int(round(aw * sc)), int(round(ah * sc))
    x0 = (W_OUT - lw) // 2
    y0 = (H_OUT - lh) // 2
    out.paste(im.crop(b).resize((lw, lh), Image.LANCZOS), (x0, y0))
    how = 'horizontal source (%s), fitted' % MODE
else:
    eb, wb = tight(*bands[0]), tight(*bands[1])
    emH = H_OUT
    emW = int(round((eb[2] - eb[0]) * emH / float(eb[3] - eb[1])))
    wmH = int(round(H_OUT * 0.42))
    wmW = int(round((wb[2] - wb[0]) * wmH / float(wb[3] - wb[1])))
    gap = int(round(H_OUT * 0.18))
    lw = emW + gap + wmW
    if lw > W_OUT: sys.exit('gen_logo: the re-laid lockup is %dpx wide, canvas is %d' % (lw, W_OUT))
    x0 = (W_OUT - lw) // 2
    out.paste(im.crop(eb).resize((emW, emH), Image.LANCZOS), (x0, 0))
    out.paste(im.crop(wb).resize((wmW, wmH), Image.LANCZOS), (x0 + emW + gap, (H_OUT - wmH) // 2))
    lh, y0 = H_OUT, 0
    how = 'stacked source, re-laid side by side'

px = out.load()

# ---- is the fringe contaminated ? ----
ratios = []
for y in range(0, H_OUT, 2):
    for x in range(1, W_OUT - 1, 2):
        r, g, b2, al = px[x, y]
        if not (40 < al < 200): continue
        for d in range(1, 12):
            hit = False
            for xx in (x - d, x + d):
                if 0 <= xx < W_OUT:
                    R, G, B, A = px[xx, y]
                    if A > 245:
                        l2 = 0.3 * R + 0.6 * G + 0.1 * B
                        if l2 > 20:
                            ratios.append(((0.3 * r + 0.6 * g + 0.1 * b2) / (al / 255.0)) / l2)
                        hit = True
                        break
            if hit: break
ratios.sort()
med = ratios[len(ratios) // 2] if ratios else 1.0
contaminated = med < 1.25
if contaminated:
    for y in range(H_OUT):
        for x in range(W_OUT):
            r, g, b2, al = px[x, y]
            if al == 0: px[x, y] = (0, 0, 0, 0)
            elif al < 250:
                f = min(255.0 / float(al), 2.6)   # a hard divide blows the softest edges out to white
                px[x, y] = (min(255, int(r * f)), min(255, int(g * f)), min(255, int(b2 * f)), al)
else:
    for y in range(H_OUT):
        for x in range(W_OUT):
            if px[x, y][3] == 0: px[x, y] = (0, 0, 0, 0)   # clear pixels carry no colour -> nothing bleeds under bilinear

with open(OUT, 'wb') as f:
    for y in range(H_OUT):
        row = bytearray()
        for x in range(W_OUT):
            r, g, b2, al = px[x, y]
            row += bytes((b2, g, r, al))                   # BGRA, straight alpha
        f.write(bytes(row))

x1, y1 = x0 + lw, y0 + lh
with open(MET, 'w') as f:
    f.write('// logo_metrics.h -- GENERATED by scripts/gen_logo.py. Do not hand-edit.\n'
            '// Where the artwork sits inside aiohud_logo.raw. The canvas is a fixed power of two so the mip chain\n'
            '// is exact, and the art is centred in it at whatever proportion the render happened to have -- so the\n'
            '// header positions itself from these instead of from a magic number that a new render would break.\n'
            '#pragma once\n'
            'namespace aio {\n'
            'static const int   LOGO_TEX_W = %d, LOGO_TEX_H = %d;\n'
            'static const float LOGO_ART_X0 = %.5ff, LOGO_ART_X1 = %.5ff;   // fractions of the texture width\n'
            'static const float LOGO_ART_Y0 = %.5ff, LOGO_ART_Y1 = %.5ff;   // ... and of its height\n'
            '} // namespace aio\n' % (W_OUT, H_OUT, x0 / float(W_OUT), x1 / float(W_OUT),
                                             y0 / float(H_OUT), y1 / float(H_OUT)))

print('gen_logo: %s  (%s)\n          art %dx%d at x=%d..%d of %dx%d  |  fringe %s (median %.2f)'
      % (OUT, how, lw, lh, x0, x1, W_OUT, H_OUT,
         'DECONTAMINATED' if contaminated else 'clean, left alone', med))
