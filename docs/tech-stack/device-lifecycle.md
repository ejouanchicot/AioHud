---
title: Device lifecycle (zoning)
summary: The strict widget lifecycle AioHUD follows across FFXI device resets on zoning — forget handles on lost, recreate lazily, release only on unload.
source: TECH_STACK.md §12
---
# Device lifecycle (zoning) — the one D3D8 rule to never break

FFXI recreates its D3D device on zone changes → our GPU handles go stale. Widgets follow a strict
lifecycle (`ui/widget.h`):
- `on_device_lost()` — **forget** GPU handles (set to 0). **Do NOT `Release`** — the old device may
  already be dead. (`Party::on_device_lost` zeroes every `*_tex_` + `*_tried_`.)
- `ensure(dev)` — lazily (re)create resources next frame (`load_raw_texture`, `make_dot`).
- `dispose()` — on `//unload`, `Release` while the device is still alive.

This maps to the standard D3D **lost-device** discipline: `D3DPOOL_DEFAULT` resources must be released
and recreated across a reset; managed resources survive. We treat *all* our handles as volatile.

**References.**
[Lost devices (MS Learn)](https://learn.microsoft.com/en-us/windows/win32/direct3d9/lost-devices) ·
[D3DPOOL enumeration (MS Learn)](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dpool) ·
[Handling lost devices (Dev-C++ blog)](http://orwelldevcpp.blogspot.com/2011/04/handling-lost-devices.html)

## See also
- [Direct3D 8 — fixed-function, 2D](direct3d8-2d.md)
- [Textures — raw BGRA](textures.md)
- [Windower 4 (IPlugin ABI)](windower-abi.md)
- [D3D8 rendering](../reference/d3d8-rendering.md)
- [Workflow gotchas](../reference/workflow-gotchas.md)
