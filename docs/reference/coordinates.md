---
title: Coordinate System
summary: Primitives and text share the D3D viewport 1:1 pixel space (2560×1400 here); why GetViewport lies under supersampling and how to centre elements.
source: REFERENCE.md §4
---
# 4. Coordinate system

**Primitives AND text share ONE space = the D3D viewport, 1:1 pixels.**
Here that is **2560×1400** (confirmed: a `(0,0)-(1280,700)` box = exactly the
top-left quarter). It is NOT:
- the window client (`GetClientRect` = 2558×1349),
- the desktop (`GetDisplayMode` = 5120×1440, dual monitor),
- the supersampled buffer (settings has `supersampling=2`).

⚠️ **Do NOT read the coord space from `GetViewport`.** On this install (with
`supersampling=2`) device `GetViewport` ([41]) returns the **supersampled
back-buffer = 5120×2800 = 2× the coord space**, not the 2560×1400 you draw in.
Trusting it placed elements at 2× their intended position (a centred box landed
bottom-right). The draw coord space here is 2560×1400; the supersampled buffer is
that × the supersampling factor. Until the factor is read at runtime, treat the
coord space as fixed (2560×1400 here) and only use GetViewport for diagnostics:
```c
u32 dev = host->vtbl[2](host);                 // IDirect3DDevice8
// IDirect3DDevice8::GetViewport == vtable index 41
struct VP { unsigned x,y,w,h; float minz,maxz; } vp;
dev->vtbl[41](dev, &vp);                        // vp.w,h = SUPERSAMPLED buffer (5120x2800), NOT the coord space
```
Other handy device indices: `[8]` GetDisplayMode, `[14]` Reset, `[15]` Present,
`[34]` BeginScene, `[35]` EndScene.

Centre an element: `x = (vp.w - elemW)/2`, `y = (vp.h - elemH)/2`. For text,
get `elemW/elemH` from `TextObject::GetExtents` ([12]).

## See also
- [Direct D3D8 rendering](d3d8-rendering.md)
- [Service interfaces](service-interfaces.md)
- [Enumerating everything on screen & address mapping](debug-and-mapping.md)
