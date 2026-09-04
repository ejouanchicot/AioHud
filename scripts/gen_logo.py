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

out = Image.new('RGBA', (W_OUT, H_OUT), (0, 0, 0, 0))
if len(bands) == 1:
    b = tight(*bands[0])
    aw, ah = b[2] - b[0], b[3] - b[1]
    sc = min(W_OUT / float(aw), H_OUT / float(ah))
    lw, lh = int(round(aw * sc)), int(round(ah * sc))
    x0 = (W_OUT - lw) // 2
    out.paste(im.crop(b).resize((lw, lh), Image.LANCZOS), (x0, (H_OUT - lh) // 2))
    how = 'horizontal source, fitted'
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

x1 = x0 + lw
with open(MET, 'w') as f:
    f.write('// logo_metrics.h -- GENERATED by scripts/gen_logo.py. Do not hand-edit.\n'
            '// Where the artwork sits inside aiohud_logo.raw. The canvas is a fixed power of two so the mip chain\n'
            '// is exact, and the art is centred in it at whatever proportion the render happened to have -- so the\n'
            '// header positions itself from these instead of from a magic number that a new render would break.\n'
            '#pragma once\n'
            'namespace aio {\n'
            'static const int   LOGO_TEX_W = %d, LOGO_TEX_H = %d;\n'
            'static const float LOGO_ART_X0 = %.5ff, LOGO_ART_X1 = %.5ff;   // fractions of the texture width\n'
            '} // namespace aio\n' % (W_OUT, H_OUT, x0 / float(W_OUT), x1 / float(W_OUT)))

print('gen_logo: %s  (%s)\n          art %dx%d at x=%d..%d of %dx%d  |  fringe %s (median %.2f)'
      % (OUT, how, lw, lh if len(bands) == 1 else H_OUT, x0, x1, W_OUT, H_OUT,
         'DECONTAMINATED' if contaminated else 'clean, left alone', med))
