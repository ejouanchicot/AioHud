---
title: Layers
summary: The four source layers (gfx/model/ui/plugin) and the strict dependency rule that keeps widgets testable in isolation.
source: ARCHITECTURE.md §1
---
# Layers (`src/`)

```
gfx/     D3D8 backend, zero game knowledge : textures, fonts, primitive submission (draw.h),
         procedural noise. Everything draws as pre-transformed quads (FVF XYZRHW).
model/   GAME DATA only, zero rendering : memory reads (game_mem), the live party/alliance
         roster + packet handlers (party_state), the per-frame snapshot (gamestate), the
         layout descriptor parser (layout), generated tables (spells/abilities/weapon_skills).
ui/      WIDGETS + the HUD compositor. A widget owns its GPU resources and draws itself from
         the snapshot. Never reads game memory in draw().
plugin/  IPlugin ABI glue (aiohud.cpp) : init/unload, the render hook, the packet hook, and
         the //aio command dispatch (incl. the RE probes).
include/ reusable ABI framework : windower.h (typed host-interface wrappers, all SEH-guarded),
         windower_debug.h (file logging + hexdump + RTTI/vtable helpers for RE).
```

Dependency rule: `ui` → `model` + `gfx`. `model` → nothing in `ui`/`gfx`. `gfx` → nothing.
Keep it that way — it's what lets a widget be tested/reasoned about in isolation.

## See also
- [The per-frame data flow](data-flow.md)
- [Widgets](widgets.md)
- [Notable conventions](conventions.md)
- [Plugin ABI](../reference/plugin-abi.md)
- [Host interface wrappers](../reference/host-pluginmanager.md)
