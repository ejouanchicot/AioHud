---
title: Direct D3D8 Rendering
summary: The FFXIDB direct-device path (custom XYZRHW quads, UV scroll, state blocks), the zoning alpha mis-sample gotcha, and the full clean-2D rendering rules including the half-pixel rule.
source: REFERENCE.md §3b, §3c, §10
---
# 3b. DIRECT D3D8 rendering (the FFXIDB path) — bypasses the primitive API

The Windower primitive/text API is the *easy* path but limited (no UV scroll, rect
clip only, no rotation). For full control, render **straight through the
IDirect3DDevice8** the host hands you at `host->vtbl[2]` — this is exactly what
**FFXIDB** does for its round, scrolling minimap. Decompiled from FFXIDB's
per-frame draw (`FUN_1006a910`): it stores the device and calls, per frame,

```
dev->vtbl[76](dev, 0x104)                       // SetVertexShader(FVF = XYZRHW|TEX1)
dev->vtbl[61](dev, 0, texture)                  // SetTexture(stage 0, tex)
dev->vtbl[63](dev, 0, type, value)              // SetTextureStageState
dev->vtbl[50](dev, state, value)                // SetRenderState
dev->vtbl[72](dev, 5, 2, &verts, 0x18)          // DrawPrimitiveUP(TRIANGLESTRIP, 2 tris, verts, stride=24)
```

i.e. it builds its **own vertices** (FVF `0x104` = `D3DFVF_XYZRHW|D3DFVF_TEX1`,
24-byte verts = screen pos + **UV**) and draws them with `DrawPrimitiveUP`. The UVs
are the player position → the map **scrolls** smoothly; the round shape is custom
geometry/mask. **You can do the same** (UV scroll, any size, rotation, circular
mask) — the primitive API's limits are not the engine's.

IDirect3DDevice8 vtable indices we use (× 4 = byte offset):
`50` SetRenderState, `51` GetRenderState, `54` ApplyStateBlock, `56` DeleteStateBlock,
`57` CreateStateBlock, `61` SetTexture, `63` SetTextureStageState, `72` DrawPrimitiveUP,
`76` SetVertexShader, `41` GetViewport.

The per-frame render hooks (IPlugin slots 5/6) fire from inside EndScene, so the
device is mid-scene and ready to draw. **Save/restore device state** around your
draw or you corrupt the game/Windower rendering — simplest: `CreateStateBlock(
D3DSBT_ALL)` (captures current) → set states → DrawPrimitiveUP → `ApplyStateBlock`
→ `DeleteStateBlock`. (FVF `0x104` verts are 24 bytes: float x,y,z,rhw + float u,v.)

**VERIFIED WORKING (2026-06-22), recipe:**
- **Draw on IPlugin slot 6, not slot 5.** Slot 5 fired but nothing showed; slot 6
  (FFXIDB's draw slot) draws to screen. (Wire `m06` → your render in
  windower_plugin.h.)
- **Coord space for XYZRHW = 2560×1400** (a (0,0)-(1280,700) quad covered exactly
  the top-left quarter) — i.e. the same logical space as the primitive API, NOT
  the supersampled 5120×2800 that GetViewport returns.
- **Make a texture yourself** (no d3dx/PNG loader needed): `device->CreateTexture(
  w,h,1,0,D3DFMT_A8R8G8B8=21,D3DPOOL_MANAGED=1,&tex)` [vtbl 20], then
  `tex->LockRect(0,&lr,0,0)` [tex vtbl 16] → fill `lr.pBits` rows (pitch `lr.Pitch`,
  A8R8G8B8) → `tex->UnlockRect(0)` [17]. Release on unload [tex vtbl 2].
- **UV scroll** = draw the quad with U range `[u0, u0+1]`, `u0 = time`, plus
  `SetTextureStageState(0, D3DTSS_ADDRESSU=13, D3DTADDRESS_WRAP=1)` → the texture
  scrolls smoothly forever (no frames). This is the smooth-liquid path.
- **Alpha blend**: `SetRenderState(ALPHABLENDENABLE=27,1)`, `SRCBLEND=19→SRCALPHA(5)`,
  `DESTBLEND=20→INVSRCALPHA(6)`. Texture stage for a pure texture:
  `COLOROP=1→SELECTARG1(2)`, `COLORARG1=2→D3DTA_TEXTURE(2)`, same for ALPHAOP/ARG1.
  Also `ZENABLE=7→0`, `CULLMODE=22→NONE(1)`, `LIGHTING=137→0`.
- This unlocks what the primitive API can't: UV scroll, any size, rotation, custom
  geometry (round/curved containers), multi-layer compositing.

### 3c. ⚠️ ZONING GOTCHA — a texture's ALPHA mis-samples while a zone loads

**Symptom:** during a zone load (TP/door), any quad whose BLEND alpha comes from a
**texture** renders too opaque → too bright, then snaps correct the instant the
load finishes. RGB is unaffected the whole time. (Found 2026-06-22; the liquid
fiole "brightened" at every zoning. Proven via an in-game A/B harness — opaque
textured = stable, translucent textured = bugs, translucent *untextured* = stable,
DESTBLEND=ZERO still bugs so it's not the background, vertex-alpha = stable.)

**Root cause:** while the loading screen is up, sampling a `D3DPOOL_MANAGED`
texture returns its **alpha channel as ~255 (opaque)** while RGB stays correct.
So `ALPHAOP=MODULATE/SELECTARG1` with `ALPHAARG=D3DTA_TEXTURE` yields the wrong
(too-high) blend alpha. Things that DON'T trigger it: device reset, VRAM eviction
(PreLoad each frame does NOT help), gamma, our render states being overwritten
(read-back stable 2000+ frames), the background, WRAP/CLAMP, UV, magnification.
It is specifically the **sampled texture alpha**. Alpha at the safe extremes (0 or
255 — e.g. cap/icon art that's fully transparent or fully opaque) survives; only
**mid-range** texture alpha (a translucent fluid) visibly shifts.

**THE RULE — never drive blend alpha from a texture's alpha channel.** Take the
blend alpha from the **vertex/diffuse** instead:
```c
SetTextureStageState(0, COLOROP=1, MODULATE=4)      // RGB  = texture × diffuse  (RGB samples fine)
SetTextureStageState(0, COLORARG1=2, D3DTA_TEXTURE=2)
SetTextureStageState(0, COLORARG2=3, D3DTA_DIFFUSE=0)
SetTextureStageState(0, ALPHAOP=4, SELECTARG1=2)    // ALPHA = vertex/diffuse ONLY  (immune)
SetTextureStageState(0, ALPHAARG1=5, D3DTA_DIFFUSE=0)
```
- **Translucent textured fill (liquid):** put the translucency in the **vertex
  diffuse alpha**; keep RGB = texture×diffuse. (Per-pixel translucency baked into
  the texture's alpha is NOT usable — it mis-samples. If you need per-pixel
  translucency, bake it into RGB premultiplied instead.)
- **Additive shaped sprites (glow / halo / bubbles, DESTBLEND=ONE):** store the
  SHAPE/falloff in the texture **RGB (grayscale)**, not alpha. Edges = black = add
  nothing. Then `COLOROP=MODULATE` (shape×tint) + `ALPHAOP=SELECTARG1 DIFFUSE`
  (intensity from vertex). Shape now comes from RGB → immune.

This is why solid (untextured) rects, and opaque/icon textures, never showed the
glitch — only translucent texture-alpha fills did.

---

## 10. Clean 2D rendering in Direct3D 8 (technical reference)

Everything here draws as **pre-transformed quads (FVF XYZRHW)** straight through the
device — no D3DX, no sprite/font helper. That path is fast and flexible but has exact
rules; get one wrong and you get blur, dropped edges, or seams. Sources: MS "Directly
Mapping Texels to Pixels", "Texture Filtering with Mipmaps", GameDev.net D3D8 threads.

### 10a. The HALF-PIXEL rule (most important) — `x -= 0.5, y -= 0.5`
A pixel is a **point at the centre** of its cell; screen (0,0) is that centre, so the
top-left *corner* is (-0.5,-0.5). With XYZRHW you bypass all transforms, so YOU must
shift every vertex by **-0.5 in x and y**. If you don't:
- the rasterizer's **top-left fill rule drops the bottom row / right column** of a quad
  whose edges sit on integer pixel lines → the classic *"cut off at the bottom/right"*
  (this is exactly what cut our marker dots);
- sampled textures land **between four texels** → bilinear averages them → **blur**.
**Applied here in every `draw.cpp` helper** (`tquad*`, `glow_quad`, `cap_quad`,
`grad_quad`, `seg_soft`, `disc`). `font.cpp` used to do `-0.5` itself — now it must NOT
(the helper does it) or text double-shifts. Add the `-0.5` to any NEW vertex builder.

### 10b. Filtering — LINEAR + mips for anything scaled
- Set all three: `D3DTSS_MINFILTER`, `MAGFILTER`, `MIPFILTER = D3DTEXF_LINEAR`.
- **Build a mip chain** for any texture shown smaller than native (icons, dots): mips +
  `MIPFILTER_LINEAR` = clean minification, no shimmer. We use `make_texture_argb_mip`.
- `D3DTEXF_POINT` ONLY for true 1:1 pixel-art at integer positions, never scaled/rotated.
- (Filtering is a **sampler/stage** state — re-set it in your draw pass; the previous
  widget may have left POINT.)

### 10c. Addressing — CLAMP for sprites; inset atlas UVs
- A standalone sprite/icon/dot: `D3DTSS_ADDRESSU/ADDRESSV = D3DTADDRESS_CLAMP` so the
  edge texel doesn't WRAP to the opposite side (a faint line on one edge).
- A **texture atlas** (our font glyphs): leave a 1px **gutter** between cells AND inset
  the UVs by **half a texel** (`+0.5/W .. (cellW-0.5)/W`) so LINEAR filtering can't bleed
  a neighbour in. Bleeding shows as faint fringes around glyphs/sprites when scaled.
- UV-scroll effects (liquid) use `WRAP` deliberately — that's the exception.

### 10d. Alpha blending — straight vs premultiplied
- **Straight alpha** (what we use): `ALPHABLENDENABLE=1`, `SRCBLEND=SRCALPHA`,
  `DESTBLEND=INVSRCALPHA`. Formula `out = src.rgb*a + dst*(1-a)`. Simple; fine for solid
  icons/dots/text. Downside: bad at very soft edges over bright bg (dark halo) and can't
  mix add+over in one draw (D3D8 has **no** `SEPARATEALPHABLENDENABLE`).
- **Premultiplied alpha**: bake `rgb *= a` into the texture, draw with
  `SRCBLEND=ONE`, `DESTBLEND=INVSRCALPHA` → `out = src.rgb + dst*(1-a)`. Cleaner edges,
  and additive (glow) + normal in the same sheet (alpha 0 + bright rgb = additive). Worth
  it if marker/glow edges ever look haloed.
- **Tint a white silhouette**: `COLOROP=MODULATE`, `COLORARG1=TEXTURE`, `COLORARG2=DIFFUSE`
  + vertex colour = tint (texture×colour). For a full-colour PNG: tint = white
  (`0xFFFFFFFF`) → shows its own colours. Alpha likewise `ALPHAOP=MODULATE`(tex×diffuse)
  or `SELECTARG1 TEXTURE`.

### 10e. Textures: format, pool, size
- `D3DFMT_A8R8G8B8` (32-bit straight ARGB), `D3DPOOL_MANAGED` (survives a device **reset**;
  D3D restores it). Forget (don't release) handles on a device **recreate** — see the
  Widget `on_device_lost`/`ensure`/`dispose` lifecycle.
- Prefer **power-of-two** dimensions (32/64/128…): universally safe + required for a full
  mip chain. Non-PoT can fail or disable mips on older hardware.
- **Zoning gotcha:** a MANAGED texture's **alpha mis-samples as ~255 while a zone
  loads**. For anything whose translucency matters during loads, drive blend alpha from
  the **vertex/diffuse**, not the texture alpha (or premultiply into RGB). Tiny opaque
  markers can ignore it (a brief square during the load screen only).

### 10f. State hygiene (multi-widget)
The HUD wraps all widgets in ONE state block (save→draw→restore). But **within** that
block, each widget/pass must set the states it relies on (FVF, blend, tex-stage,
filtering, addressing) — the previous pass left its own. Always: set FVF for your vertex
type (`0x44` colour / `0x144` textured), bind/unbind your texture (`dSetTex(...,0)` after),
and don't assume defaults. `ZENABLE=0`, `CULLMODE=NONE`, `LIGHTING=0` for flat 2D.

### 10g. Coordinate space (recap of [coordinates](coordinates.md))
Draw in the logical **2560×1400** space here (NOT the supersampled back-buffer that
`GetViewport` reports). Supersampling=2 actually helps 2D edges (downsampled MSAA-like),
so geometry (e.g. the `disc` triangle fan) comes out smoother than at native res.

## See also
- [The host: PluginManager](host-pluginmanager.md)
- [Service interfaces](service-interfaces.md)
- [Coordinate system](coordinates.md)
- [Runtime ↔ Ghidra address mapping](debug-and-mapping.md)
