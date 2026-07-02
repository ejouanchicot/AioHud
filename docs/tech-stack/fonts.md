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
