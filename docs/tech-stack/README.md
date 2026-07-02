---
title: Tech Stack — Index
summary: Landing page for the AioHUD technology stack — the stack-at-a-glance table, the rules-that-bite quick reference, and an index of every tech-stack topic file.
source: TECH_STACK.md §1, §13
---
# AioHUD — Technology Stack

The full list of technologies AioHUD is built on, **why** each was chosen, **how** we use it in
this codebase, the **best-practices** that keep it fast and crisp, and a curated set of
**engineering references** for using each one effectively.

> One line: **a 32-bit C++ plugin for Windower 4 that draws a modern FFXI HUD in DirectX 8
> fixed-function — pre-transformed quads + hand-rolled anti-aliased vector primitives + raw BGRA
> textures and bundled GDI fonts — fed by SEH-guarded game-memory reads and FFXI packet hooks.**

For the data-flow map and the "add a module" recipe see the **[Architecture](../architecture/README.md)** section; for the reversed
ABI, memory offsets and D3D state rules see the **[Reference](../reference/README.md)** section. This file is the *technology*
layer under both.

## Stack at a glance

| Concern | Technology | Where in the repo |
|---|---|---|
| Language / binary | **C++**, 32-bit, MSVC `cl` → `AioTest.dll` | `build.bat`, all of `src/` |
| Host / integration | **Windower 4** `IPlugin` ABI (FFXI addon host) | `plugin/aiohud.cpp`, `include/windower.h` |
| Rendering | **Direct3D 8**, fixed-function, pre-transformed quads (FVF `XYZRHW`) | `gfx/draw.{h,cpp}` |
| Vector look | **Hand-rolled anti-aliasing** by alpha *feathering* (no shaders) | `gfx/draw.cpp` (`rrect`, `disc`, `seg_soft`…) |
| Clipping | **Stencil buffer** masking (rounded capsule for the vial liquid) | `ui/liquid_bars.cpp` (`rrect_clip_begin/end`) |
| Blending | Standard alpha + **additive** for glow/neon | `gfx/draw.cpp`, every widget |
| Textures | **Raw BGRA** files (`.raw`) loaded to `IDirect3DTexture8` | `gfx/texture.cpp`, `assets/*.raw` |
| Fonts | **GDI** faces bundled via `AddFontResourceEx (FR_PRIVATE)`, rasterized to a glyph atlas | `gfx/font.cpp`, `assets/fonts` |
| Game data | **Reverse-engineered memory reads**, all **SEH-guarded**, + packet hooks | `model/party_state.cpp`, `model/game_mem.cpp` |
| RE tooling | **Ghidra** (headless) + live differential probes | `re/`, `//aio` probes in `plugin/aiohud.cpp` |
| Asset tooling | **Python + PIL** (atlas assembly) | `scripts/` |

## Quick-reference: the rules that bite

1. **Pixel-snap + half-pixel offset** or your borders/text blur. ([Direct3D 8 — 2D](direct3d8-2d.md))
2. **Feather all edges consistently** or you get the "black line". ([anti-aliasing](anti-aliasing.md))
3. **Reset the blend after an additive pass** or the next draw glows. ([blending](blending.md))
4. **`on_device_lost` forgets, never Releases**; `dispose` Releases. ([device lifecycle](device-lifecycle.md))
5. **SEH-guard every game-memory read**; validate pointers. ([game-data I/O](game-data-io.md))
6. **`//unload` before `deploy`** (the DLL is file-locked); verify the DLL timestamp moved. ([C++ & build](cpp-and-build.md))
7. **One source of truth for offsets** (the poller); document each in [game data](../game-data/README.md). ([game-data I/O](game-data-io.md))
8. **Don't leave a bound texture / dirty state** for the next widget. ([Direct3D 8 — 2D](direct3d8-2d.md) / [blending](blending.md))

## Topics

- [C++ (32-bit, MSVC)](cpp-and-build.md) — why a 32-bit DLL, the no-heap-in-hot-path style, CRT boundary caveats.
- [Windower 4 (IPlugin ABI)](windower-abi.md) — the plugin ABI, render/packet hooks, host-owned device hook.
- [Direct3D 8 — fixed-function, 2D](direct3d8-2d.md) — pre-transformed XYZRHW quads, pixel-snap, batching.
- [Blending — alpha & additive](blending.md) — the two blend modes, MODULATE tinting, cascade termination.
- [Hand-rolled anti-aliasing (feathered vector primitives)](anti-aliasing.md) — alpha feathering, the consistent-feather rule, gamma caveat.
- [Stencil-buffer clipping](stencil.md) — rounded-capsule masking for the vial liquid, square fallback.
- [Textures — raw BGRA](textures.md) — flat `.raw` art, UV atlases, margin cropping, UV-scroll animation.
- [Fonts — GDI bundled, atlas-rasterized](fonts.md) — private `AddFontResourceEx` faces, glyph atlas, weight authority.
- [UI composition — immediate-mode controls + 9-slice skin](ui-composition.md) — IMGUI config UI vs retained zones, nine-patch skin.
- [Game data — RE'd memory reads + packet hooks](game-data-io.md) — SEH-guarded pointer chains, once-per-frame snapshot, packet top-ups.
- [Tooling — Ghidra & Python](tooling.md) — headless decompile for offsets, deterministic PIL atlas regen.
- [Device lifecycle (zoning)](device-lifecycle.md) — forget/recreate/release discipline across device resets.
- [HUD & UI design references](hud-design.md) — the design rationale and principles behind the party visuals.
