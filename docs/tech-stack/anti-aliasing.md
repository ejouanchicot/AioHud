---
title: Hand-rolled anti-aliasing (feathered vector primitives)
summary: How AioHUD gets smooth shapes without shaders via alpha-feathered geometry, the consistent-feather rule, arc tessellation, and the gamma-space blending caveat.
source: TECH_STACK.md §6
---
# Hand-rolled anti-aliasing (feathered vector primitives)

**Why.** No pixel shaders → no SDF/`fwidth` AA. We get smooth shapes by **geometry feathering**: a
solid core plus a 1-px rim whose alpha fades to 0. Cheap, uniform at any size, and it composites with
one blend per pixel.

**How we use it (`gfx/draw.cpp`).**
- `disc`, `seg_soft`, `rrect`, `rrect_bordered`, `rrect_left`, `rrect_glow`, `rrect_stroke` — all
  feathered. Corner arcs go radius `r` → `r+feather` at alpha 0; straight edges extend by the same
  `feather`.
- **Hard-won rule:** feather *all four straight edges AND the corners consistently*, or none — if
  only the corners feather, the rounded ends reach ~1 px past the straight centre and the backdrop
  shows as a thin dark line (the "black line" bug). Bar fills draw **edge-to-edge** over the track rim
  so the rim never shows as a line inside the fluid. ([party visual system](../design/party-visual-system.md).)

**Arc tessellation.** Rounded corners/discs/segments are **triangle fans** whose vertex count scales
with radius (arc length = `radius × angle`) so the curve stays smooth at any size — exactly how
`qfan`/the corner loops in `rrect` work. Convex shapes fan cleanly (an n-gon = n−2 triangles).
In `rrect`/`rrect_left` each corner arc uses `Nc = clamp((int)(r*0.9), 6, NcMax)` segments — **6** for
small caps up to **14** (`NcMax`) for large radii — so big corners stay round without over-tessellating
tiny ones. `NcMax` is a compile-time bound because the fan/ring vertices live in **stack arrays**
(`VtxC fan[NcMax+2]`, `ring[2*(NcMax+1)]`). **The feather ring uses the SAME `Nc` as the solid fan** so
both silhouettes extend by the same amount everywhere — mismatched segment counts would let the rounded
end reach past the straight edge and show the backdrop as the 1px "black line". (`rrect_glow` /
`rrect_bordered` corners use a smaller fixed `Nc` of 6/8.)

**Colour-space caveat (why edges sometimes fringe).** Direct3D 8/9 blends in **gamma (sRGB) space**,
not linear — so a feathered edge over a dark backdrop can read slightly dark/"fringed" (the same
premultiplied-alpha dark-fringe issue). We work with it: opaque cores, feather kept to ~1 px, and fills
drawn edge-to-edge so a rim never sits *inside* a bright fluid. Worth knowing before chasing a "dark
line" that's actually gamma-space blending.

**Best-practices / when to revisit.** If FFXI is ever driven through a shader-capable wrapper,
**signed-distance-field** rendering is the modern upgrade (analytic AA, free outlines, crisp under
scale). Chris Green's Valve SIGGRAPH 2007 paper is the canonical reference. Our feathering is the
shader-free equivalent.

**References.**
[Signed distance fields intro (GM Shaders)](https://mini.gmshaders.com/p/sdf) ·
[Improved Alpha-Tested Magnification — SDF, Green/Valve 2007 (PDF)](https://steamcdn-a.akamaihd.net/apps/valve/2007/SIGGRAPH2007_AlphaTestedMagnification.pdf) ·
[Perfecting AA on SDFs (pkh.me)](https://blog.pkh.me/p/44-perfecting-anti-aliasing-on-signed-distance-functions.html) ·
[Distance-based AA with fwidth (numb3r23)](http://www.numb3r23.net/2015/08/17/using-fwidth-for-distance-based-anti-aliasing/) ·
[Drawing polylines by tessellation (artgrammer)](https://artgrammer.blogspot.com/2011/07/drawing-polylines-by-tessellation.html) ·
[LinaVG — 2D AA vector library (GitHub)](https://github.com/inanevin/LinaVG) ·
[Distance-field fonts (libGDX)](https://libgdx.com/wiki/graphics/2d/fonts/distance-field-fonts) ·
*Colour space:* [What every coder should know about gamma (Novak)](https://blog.johnnovak.net/2016/09/21/what-every-coder-should-know-about-gamma/) ·
[GPUs prefer premultiplication (Real-Time Rendering)](https://www.realtimerendering.com/blog/gpus-prefer-premultiplication/) ·
[Is alpha blending gamma-corrected? (GameDev.net)](https://www.gamedev.net/forums/topic/631164-does-alpha-blending-shall-be-gamma-corrected/)

## See also
- [Direct3D 8 — fixed-function, 2D](direct3d8-2d.md)
- [Blending — alpha & additive](blending.md)
- [Stencil-buffer clipping](stencil.md)
- [Fonts — GDI bundled, atlas-rasterized](fonts.md)
- [D3D8 rendering](../reference/d3d8-rendering.md)
