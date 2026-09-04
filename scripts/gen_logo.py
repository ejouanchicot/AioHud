# gen_logo.py -- bake the AioHUD emblem into the raw BGRA the plugin loads.
#
# Source : assets/logo_src/aiohud_logo.png (an image-model render : real alpha, but a coloured HALO around the
# gold where the model anti-aliased against a dark backdrop it then removed). Three things have to happen:
#   1. RE-LAY IT OUT. The render stacks the emblem over the wordmark, and stacked it cannot be used: a header
#      band is ~90px tall, and at that height the word would come out nine pixels high. So the two halves are
#      cut apart and set side by side -- emblem at full height, wordmark beside it at 42% of it, which is the
#      one arrangement where both the mark and the word survive being that small.
#   2. decontaminate the fringe. Where alpha is partial the model left the halo's colour in RGB, so it shows as a
#      red/yellow rim once composited over anything. Dividing RGB by alpha is the inverse of compositing on black.
#   3. keep the canvas a power of two so the mip chain is exact -- 1024x256, the lockup centred in it.
# Output : assets/aiohud_logo.raw, 1024x256 BGRA, straight (non-premultiplied) alpha.
import os, sys
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC  = os.path.join(ROOT, 'assets', 'logo_src', 'aiohud_logo.png')
OUT  = os.path.join(ROOT, 'assets', 'aiohud_logo.raw')
W_OUT, H_OUT = 1024, 256

im = Image.open(SRC).convert('RGBA')
w, h = im.size
a = im.split()[3]

# ---- the emblem is the FIRST band of non-empty rows ; the wordmark is the second ----
rows = [max(a.crop((0, y, w, y + 1)).getdata()) for y in range(h)]
bands, s = [], None
for y, v in enumerate(rows):
    if v > 8 and s is None: s = y
    elif v <= 8 and s is not None: bands.append((s, y)); s = None
if s is not None: bands.append((s, h))
if not bands: sys.exit('gen_logo: the source is empty')
if len(bands) < 2: sys.exit('gen_logo: expected an emblem band AND a wordmark band, found %d' % len(bands))

def tight(y0, y1):
    cols = [max(a.crop((x, y0, x + 1, y1)).getdata()) for x in range(w)]
    xs = [x for x, v in enumerate(cols) if v > 8]
    return (xs[0], y0, xs[-1] + 1, y1)
eb, wb = tight(*bands[0]), tight(*bands[1])

# ---- side by side : emblem at full height, wordmark at 42% of it ----
emH = H_OUT
emW = int(round((eb[2] - eb[0]) * emH / float(eb[3] - eb[1])))
wmH = int(round(H_OUT * 0.42))
wmW = int(round((wb[2] - wb[0]) * wmH / float(wb[3] - wb[1])))
gap = int(round(H_OUT * 0.18))
lockW = emW + gap + wmW
if lockW > W_OUT: sys.exit('gen_logo: the lockup is %dpx wide, canvas is %d' % (lockW, W_OUT))
x0 = (W_OUT - lockW) // 2                              # centred, so the header can draw the whole texture

em = Image.new('RGBA', (W_OUT, H_OUT), (0, 0, 0, 0))
em.paste(im.crop(eb).resize((emW, emH), Image.LANCZOS), (x0, 0))
em.paste(im.crop(wb).resize((wmW, wmH), Image.LANCZOS), (x0 + emW + gap, (H_OUT - wmH) // 2))
N = 0

# ---- fringe decontamination ----
# A partially transparent pixel whose RGB is far darker than its opaque neighbours is halo residue. Rather than
# guess a matte colour, push its RGB toward the colour it would have had at full opacity: divide out the alpha,
# which is exactly the inverse of compositing against black. Clamped, so a genuinely dark edge stays dark.
px = em.load()
for y in range(H_OUT):
    for x in range(W_OUT):
        r, g, b, al = px[x, y]
        if al == 0:
            px[x, y] = (0, 0, 0, 0)                     # keep fully-clear pixels colourless : no bleed under bilinear
        elif al < 250:
            f = 255.0 / float(al)
            if f > 2.6: f = 2.6                         # a hard divide blows out the softest edges into white
            px[x, y] = (min(255, int(r * f)), min(255, int(g * f)), min(255, int(b * f)), al)

with open(OUT, 'wb') as f:
    for y in range(H_OUT):
        row = bytearray()
        for x in range(W_OUT):
            r, g, b, al = px[x, y]
            row += bytes((b, g, r, al))                 # BGRA, straight alpha
        f.write(bytes(row))
print('gen_logo: %s  <- %dx%d  ->  emblem %dx%d + word %dx%d, lockup %dpx in %dx%d BGRA (%d bytes)'
      % (OUT, w, h, emW, emH, wmW, wmH, lockW, W_OUT, H_OUT, W_OUT * H_OUT * 4))
