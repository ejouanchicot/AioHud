---
title: Textures — raw BGRA
summary: How AioHUD ships art as flat BGRA .raw files loaded to IDirect3DTexture8, UV-addressed atlases, baked-margin cropping, and UV-scroll liquid animation.
source: TECH_STACK.md §8
---
# Textures — raw BGRA

**Why.** We ship our own art (job emblems, hand cursor, buff atlas, vial liquid/glass) as flat
**BGRA `.raw`** files — no image decoder in the DLL, a `memcpy`-simple load.

**How we use it.** `gfx/texture.cpp` `load_raw_texture(dev, path, w, h)` → `IDirect3DTexture8`. Atlases
are grids of fixed cells addressed by UV (job atlas: 8×3 of 64 px; buff atlas; etc.). White-mask
atlases are tinted per role via MODULATE ([blending](blending.md)).
- **Cropping baked margins:** emblems carry transparent padding inside their cell — we crop it in UV
  (measured min margin across all jobs) so the art fills the badge (`draw_job_icon`, `party.cpp`).

**Best-practices.** `CLAMP` addressing + linear filtering for sprites; keep exact cell UVs; regenerate
atlases from source, never hand-edit the `.raw`.

**UV-scroll animation.** The vial's "flowing liquid" is a static texture whose **UV offset advances
with time** (wrap/`frac` into 0..1) — the fixed-function equivalent of an animated-UV material. Cheap:
we move 4 texcoords, not pixels. Same idea as modern flow-map water, minus the shader.

**Regenerate** (Python + PIL): load each source PNG RGBA → resize to the cell → force white RGB / keep
alpha (for masks) → paste in order → write BGRA raw. See [party visual system](../design/party-visual-system.md).

**References.**
[Manipulating texture coordinates / UV scroll (VFXDoc)](https://vfxdoc.readthedocs.io/en/latest/shaders/texcoord/) ·
[Animating UV coordinates (Unreal docs)](https://dev.epicgames.com/documentation/en-us/unreal-engine/animating-uv-coordinates-in-unreal-engine) ·
[Scrolling textures (David Gouveia)](https://www.david-gouveia.com/scrolling-textures-with-zoom-and-rotation)

## See also
- [Blending — alpha & additive](blending.md)
- [Fonts — GDI bundled, atlas-rasterized](fonts.md)
- [Tooling — Ghidra & Python](tooling.md)
- [Device lifecycle (zoning)](device-lifecycle.md)
- [Party visual system](../design/party-visual-system.md)
