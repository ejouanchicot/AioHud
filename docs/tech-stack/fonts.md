---
title: Fonts — GDI bundled, atlas-rasterized
summary: How AioHUD bundles GDI faces privately via AddFontResourceEx, rasterizes glyphs into a texture atlas, and handles weight authority and advance/kerning.
source: TECH_STACK.md §9
---
# Fonts — GDI bundled, atlas-rasterized

**Why.** Per-element typography (Name/HP/MP/TP/Cast/Badge/Distance/Interface/Cost, each with its own
face/size/weight/outline) needs real fonts we can ship without installing anything system-wide.

**How we use it (`gfx/font.cpp`).**
- Bundle faces in `assets/fonts`, register at load with **`AddFontResourceEx(..., FR_PRIVATE, 0)`** so
  they're visible only to our process (no install, no leak into the OS font list).
- Rasterize glyphs with **GDI** into a texture **atlas**, then draw text as textured quads. **Bold is
  the sole weight authority** (400/700). Pool `MAXF = 48` (was 8, which silently fell back to slot 0).

**Glyph batching (draw-call reduction).** `emit()` no longer draws per glyph — it **appends** each
glyph's quad into ONE shared file-static vertex list (`g_gbuf`, `GBUF_CAP = 6144` = 1024 quads,
`D3DPT_TRIANGLELIST`, 6 verts/quad); `draw()` flushes it once for **all 8 outline offset passes** and
once for the **main pass** (`gbuf_flush`). So an outlined label costs **2** `DrawPrimitiveUP` submits
instead of one per glyph per pass — a 5-char label went from `9×5 = 45` submits to `2`. The atlas
is bound ONCE by `draw()` (was rebound in every emit pass). Geometry per quad is **byte-identical** to
the old per-glyph `tquad()` path — same `-0.5` half-texel shift (`gbuf_quad`), same two triangles — so
pixels are unchanged. `TRIANGLELIST` (not strip) lets quads concatenate with no degenerate verts;
rendering is single-threaded inside `EndScene`, so the static scratch buffer is safe and keeps the
"no per-frame heap" rule. (`D3DPT_TRIANGLELIST = 4` was added to `gfx/d3d.h`.)

**Best-practices.** Use `GetCharABCWidths` / `GetTextExtentPoint32` for correct advance/kerning and a
small gutter between glyphs to avoid bilinear bleed; one atlas per face+size bucket.

**References.**
[Drawing text in a 3D program (virtualdub.org)](https://www.virtualdub.org/blog2/entry_379.html) ·
[Improving the font pipeline (Hypersect)](https://blog.hypersect.com/improving-the-font-pipeline/) ·
[Bitmap fonts with GDI+ (GameDev.net)](https://gamedev.net/forums/topic/570359-bitmap-fonts-with-gdi/4644529) ·
[MakeSpriteFont (DirectXTK wiki)](https://github.com/microsoft/DirectXTK/wiki/MakeSpriteFont)

## See also
- [Textures — raw BGRA](textures.md)
- [Hand-rolled anti-aliasing (feathered vector primitives)](anti-aliasing.md)
- [UI composition — immediate-mode controls + 9-slice skin](ui-composition.md)
- [HUD & UI design references](hud-design.md)
