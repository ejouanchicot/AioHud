# gen_logo.py -- bake the AioHUD emblem into the raw BGRA the plugin loads.
#
# Source : assets/logo_src/aiohud_logo.png (an image-model render : real alpha, but a coloured HALO around the
# gold where the model anti-aliased against a dark backdrop it then removed). Two things have to happen:
#   1. take the EMBLEM only. The wordmark is drawn live by the config header (chrome_text), which keeps it crisp
#      at any size and lets it follow the theme accent -- a baked one would blur and would not recolour.
#   2. decontaminate the fringe. Where alpha is partial the model left the halo's colour in RGB, so it shows as a
#      red/yellow rim once composited over anything. Un-premultiply toward the nearest opaque neighbour's hue.
# Output : assets/aiohud_logo.raw, 256x256 BGRA, straight (non-premultiplied) alpha.
import os, sys
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC  = os.path.join(ROOT, 'assets', 'logo_src', 'aiohud_logo.png')
OUT  = os.path.join(ROOT, 'assets', 'aiohud_logo.raw')
N    = 256

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
top, bot = bands[0]
cols = [max(a.crop((x, top, x + 1, bot)).getdata()) for x in range(w)]
xs = [x for x, v in enumerate(cols) if v > 8]
left, right = xs[0], xs[-1] + 1

# square crop centred on the emblem, so the mark is not squashed by the resize
cx, cy = (left + right) * 0.5, (top + bot) * 0.5
half = max(right - left, bot - top) * 0.5 + 6          # a few px of air
box = (int(cx - half), int(cy - half), int(cx + half), int(cy + half))
em = im.crop(box).resize((N, N), Image.LANCZOS)

# ---- fringe decontamination ----
# A partially transparent pixel whose RGB is far darker than its opaque neighbours is halo residue. Rather than
# guess a matte colour, push its RGB toward the colour it would have had at full opacity: divide out the alpha,
# which is exactly the inverse of compositing against black. Clamped, so a genuinely dark edge stays dark.
px = em.load()
for y in range(N):
    for x in range(N):
        r, g, b, al = px[x, y]
        if al == 0:
            px[x, y] = (0, 0, 0, 0)                     # keep fully-clear pixels colourless : no bleed under bilinear
        elif al < 250:
            f = 255.0 / float(al)
            if f > 2.6: f = 2.6                         # a hard divide blows out the softest edges into white
            px[x, y] = (min(255, int(r * f)), min(255, int(g * f)), min(255, int(b * f)), al)

with open(OUT, 'wb') as f:
    for y in range(N):
        row = bytearray()
        for x in range(N):
            r, g, b, al = px[x, y]
            row += bytes((b, g, r, al))                 # BGRA, straight alpha
        f.write(bytes(row))
print('gen_logo: %s  <- crop %s of %dx%d  -> %dx%d BGRA (%d bytes)' % (OUT, box, w, h, N, N, N * N * 4))
