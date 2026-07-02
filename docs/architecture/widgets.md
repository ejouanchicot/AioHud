---
title: Widgets
summary: The Widget base class lifecycle methods, placement, and the step-by-step checklist to add a new module.
source: ARCHITECTURE.md §3
---
# Widgets (`ui/widget.h`)

A widget is a subclass of `Widget`:

| method | when | does |
|---|---|---|
| `type_name()` | — | the layout key (factory + descriptor), e.g. `"PartyList"` |
| `configure(cfg)` | on layout (re)load | reads its keys from the descriptor JSON |
| `measure(w,h)` | on placement | reports content size for right/bottom anchoring (or -1) |
| `ensure(dev)` | every frame, pre-draw | lazily (re)creates GPU resources, no-op once built |
| `draw(f)` | every frame | renders from `f.game` ; leaves no state that corrupts the next widget |
| `on_device_lost()` | on zoning (device recreated) | **forget** GPU handles (don't Release — old device may be dead) |
| `dispose()` | on `//unload` | Release GPU resources (device still alive) |

Placement (`px_/py_/z_/visible_/scale_`) is set by the HUD from `design/exports/layout.json`
(see [Layout JSON format](../formats/layout-json.md)). The widget draws relative to `px_/py_` and multiplies geometry by `scale_`.

### Add a new module — checklist
1. Add the data it needs to `GameState` + fill it in `poll_game_state()` (model layer).
   Reverse the offset first if it's new (see [reverse-engineering recipe](reverse-engineering-recipe.md)) and document it in [game data](../game-data/README.md).
2. New file `ui/<name>.{h,cpp}` subclassing `Widget`. Read `f.game`, draw via `gfx/draw.h`,
   text via `f.fonts`. Mirror the look from `design/src/panels/<name>` (the mockup is the spec).
3. Register the type in `ui/factory.cpp` (`if (type == "<Name>") return new <Name>();`).
4. Add it to the layout descriptor so the HUD places it.
5. Add the `.cpp` to `build.bat`'s source list. Build (see [build / deploy](build-deploy.md)), `//unload`, `deploy`, `//load`.

Conventions: D3D state hygiene ([D3D8 rendering](../reference/d3d8-rendering.md)) — set what you need, the HUD wraps all
widgets in one save/restore state block, but don't leave a bound texture for the next widget.
Snap geometry to whole pixels (`snap()`); draw at the half-pixel offset ([D3D8 rendering](../reference/d3d8-rendering.md)).

## See also
- [The per-frame data flow](data-flow.md)
- [Reverse-engineering recipe](reverse-engineering-recipe.md)
- [Build / deploy / iterate](build-deploy.md)
- [Layout JSON format](../formats/layout-json.md)
- [D3D8 rendering](../reference/d3d8-rendering.md)
- [Coordinates](../reference/coordinates.md)
