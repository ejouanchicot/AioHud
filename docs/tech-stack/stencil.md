---
title: Stencil-buffer clipping
summary: How AioHUD clips the vial's scrolling liquid into a rounded capsule using the stencil buffer, with the square fallback when no stencil is available.
source: TECH_STACK.md §7
---
# Stencil-buffer clipping

**Why.** To clip the vial's scrolling liquid texture into a **rounded capsule** (round ends,
transparent corners) we can't use a rectangle scissor — we need an arbitrary rounded mask.

**How we use it.** `ui/liquid_bars.cpp` `rrect_clip_begin/end`: write the capsule shape into the
stencil buffer, draw the animated liquid only where the stencil passes, then clear the mask. Falls
back to a square (never black) if the back-buffer has **no stencil** — always check availability.

**Best-practices.** Check for stencil support before relying on it; keep stencil ref/mask/op local and
reset the stencil state when done so no other widget inherits a mask.

**References.**
[Stencil buffer techniques (MS Learn)](https://learn.microsoft.com/en-us/windows/win32/direct3d9/stencil-buffer-techniques) ·
[Stencil techniques — DX8 docs](https://documentation.help/directx8_c/StencilBufferTechniques.htm) ·
[Inside Direct3D: Stencil Buffers](https://www.gamedeveloper.com/programming/inside-direct3d----stencil-buffers)

## See also
- [Direct3D 8 — fixed-function, 2D](direct3d8-2d.md)
- [Hand-rolled anti-aliasing (feathered vector primitives)](anti-aliasing.md)
- [Textures — raw BGRA](textures.md)
- [D3D8 rendering](../reference/d3d8-rendering.md)
