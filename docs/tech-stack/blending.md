---
title: Blending — alpha & additive
summary: The two blend modes that carry AioHUD's look — standard alpha and additive glow — plus MODULATE tinting and the texture-cascade termination rule.
source: TECH_STACK.md §5
---
# Blending — alpha & additive

**Why.** Two blend modes carry the whole look: standard alpha for opaque panels/AA edges, and
**additive** for glows (WS-ready aura, HP-critical wash, level-line light, button halo/shine).

**How we use it.**
- **Standard alpha:** `D3DRS_ALPHABLENDENABLE = TRUE`, `SRCBLEND = D3DBLEND_SRCALPHA`,
  `DESTBLEND = D3DBLEND_INVSRCALPHA`. (`cs()` in `config_page.cpp` sets this baseline.)
- **Additive / glow:** same, but `DESTBLEND = D3DBLEND_ONE` — light accumulates → neon.
  (`cs_add()` / `dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_ONE)`.) After an additive pass you **must
  reset the blend** before the next opaque draw — a recurring source of "why is my triangle glowing"
  bugs in this codebase (e.g. the `cat_header` caret needed an explicit `cs()` after the hover shine).
- **Tinting textures by a colour** (white-mask atlases → role colour): texture stage
  `D3DTSS_COLOROP = D3DTOP_MODULATE`, `COLORARG1 = TEXTURE`, `COLORARG2 = DIFFUSE`, same for
  `ALPHAOP`. The job-icon atlas is pure white so MODULATE paints it any role colour (`draw_job_icon`).

**Best-practices.** Draw additive passes *last* within an element, or bracket them and restore;
never leave the device in additive state for the next widget. In the fixed-function **texture cascade**,
keep as many **alpha** stages active as **colour** stages, and terminate the cascade by setting the
*next* stage's `COLOROP`/`ALPHAOP` to `D3DTOP_DISABLE` — a common source of "the second texture stage
does nothing" bugs.

**References.**
[Additive blending in D3D8 (Drunken Hyena)](https://drunkenhyena.com/pages/projects/d3d8/dhAdditiveBlend.php) ·
[Blending explained (P.A. Minerva)](https://paminerva.github.io/docs/LearnDirectX/02.A-Blending.html) ·
[Pre-multiplied alpha (dtrebilco)](https://github.com/dtrebilco/PreMulAlpha) ·
[Configuring blend state (MS Learn)](https://learn.microsoft.com/en-us/windows/win32/direct3d11/d3d10-graphics-programming-guide-blend-state) ·
[D3DTEXTURESTAGESTATETYPE (MS Learn)](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dtexturestagestatetype) ·
[Texture argument flags — DX8 docs](https://documentation.help/directx8_c/TextureArgumentFlags.htm) ·
[Setting up multitexturing (GameDev.net)](https://www.gamedev.net/forums/topic/105898-how-to-set-up-multitexturing/) ·
[DirectX 8.1 Direct3D functional spec](https://empyreal96.github.io/nt-info-depot/SourceLevel/multimedia/directx/dxg/ddk/help/d3d8funcspec81.doc)

## See also
- [Direct3D 8 — fixed-function, 2D](direct3d8-2d.md)
- [Hand-rolled anti-aliasing (feathered vector primitives)](anti-aliasing.md)
- [Textures — raw BGRA](textures.md)
- [D3D8 rendering](../reference/d3d8-rendering.md)
