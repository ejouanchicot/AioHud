---
title: Direct3D 8 — fixed-function, 2D
summary: How AioHUD renders the HUD as pre-transformed XYZRHW quads on the fixed-function D3D8 pipeline, with pixel-snap, texture addressing, and batching best-practices.
source: TECH_STACK.md §4
---
# Direct3D 8 — fixed-function, 2D

**Why.** FFXI ships a Direct3D 8 device (2003-era). No programmable shaders — everything is the
**fixed-function pipeline**. We render the HUD as **pre-transformed 2D quads** on top of the game's
frame.

**How we use it.**
- Every vertex is **pre-transformed**: FVF `D3DFVF_XYZRHW (| D3DFVF_DIFFUSE | D3DFVF_TEX1)`. `x,y` are
  screen pixels, `z`≈0, `rhw`=1 → the pipeline **skips world/view/projection and lighting**. This is
  the standard way to do 2D in D3D8.
- Geometry is submitted with `DrawPrimitiveUP` (user-pointer, no managed vertex buffer) —
  see `dDrawUP` in `gfx/draw.cpp`. Simple, allocation-free for our small quad batches.
- The HUD wraps all widgets in **one** render-state save/restore block; widgets set only what they
  need and must not leave a bound texture for the next widget ([conventions](../architecture/conventions.md)).

**Best-practices we follow (and why).**
- **Disable the 3D pipeline for 2D**: lighting off, cull none, Z-buffer off — no 3D overhead, and no
  depth fighting with the game.
- **Half-pixel / pixel-snap rule** ([D3D8 rendering](../reference/d3d8-rendering.md)): snap geometry to whole pixels (`snap()`),
  sample at the half-pixel offset. Without it, D3D's pixel-center convention blurs 1-px borders and
  text. This is *the* rule for crisp 2D in D3D8/9 and the flipcode 2D guide's biggest omission — we
  learned it the hard way.
- **Texture addressing** `CLAMP` + **linear** min/mag filter for UI sprites (icons, cursor, job
  atlas) so cells never bleed into each other; `MIPFILTER_NONE` for pixel-exact atlases.
- **Batch & minimize state changes.** `DrawPrimitiveUP` is fine for our small quad counts (the VB
  advantage only shows past ~500 tris); the real cost is API calls + render/texture-state churn. Group
  draws by texture (all buff icons in one atlas pass, all job icons in another), and set only the state
  that changed. Each `Draw*Primitive*` and each `Set*State` has a CPU cost — keep both low.

**References.**
[D3D8 2D guide (flipcode)](https://www.flipcode.com/archives/A_2D_Guide_To_DirectX_8_Graphics-Using_2D_graphics_in_a_3D_Environment.shtml) ·
[Performance optimizations (MS Learn)](https://learn.microsoft.com/en-us/windows/win32/direct3d9/performance-optimizations) ·
[DrawPrimitiveUP vs vertex buffer (GameDev.net)](https://gamedev.net/forums/topic/355496-use-drawprimitiveup-or-vertex-buffer/3331211/) ·
[PrimitiveBatch (DirectXTK wiki)](https://github.com/microsoft/DirectXTK/wiki/PrimitiveBatch) ·
[DirectX 8 Graphics: A Fresh Start (GameDev.net)](https://www.gamedev.net/tutorials/programming/graphics/directx-8-graphics-and-video-a-fresh-start-r1247/) ·
[DirectX 8 Programmer's Guide](https://documentation.help/directx8_c/ProgrammersGuide.htm) ·
[Fixed-function FVF codes (MS Learn)](https://learn.microsoft.com/en-us/windows/win32/direct3d9/fixed-function-fvf-codes) ·
[Fixed-function vertex processing (MS Learn)](https://learn.microsoft.com/en-us/windows/win32/direct3d9/fixed-function-vertex-processing) ·
[Handling XYZRHW vertices (GameDev.net)](https://gamedev.net/forums/topic/525047-semantics-of-d3d-transform-and-clipping--pipeline-handling-xyzrhw-vertices/) ·
[Drunken Hyena D3D8 tutorials](https://drunkenhyena.com/pages/projects/d3d8/)

## See also
- [Blending — alpha & additive](blending.md)
- [Hand-rolled anti-aliasing (feathered vector primitives)](anti-aliasing.md)
- [Device lifecycle (zoning)](device-lifecycle.md)
- [D3D8 rendering](../reference/d3d8-rendering.md)
- [Coordinates](../reference/coordinates.md)
