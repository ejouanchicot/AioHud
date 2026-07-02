---
title: Workflow & Gotchas
summary: Build/iterate cycle, the file-lock-until-unload rule, orphaned render objects, live config-file tuning, and performance headroom.
source: REFERENCE.md §7
---
# 7. Workflow & gotchas

- **Build**: MSVC 2017 (x86), `cl /LD /O2 /MT /I include sandbox.cpp /link /DEF:sandbox.def user32.lib kernel32.lib`. See `build.bat` / `deploy.bat`.
- **Iterate**: `//unload AioTest` in game → `deploy.bat` → `//load AioTest`. Windower keeps a loaded DLL **file-locked until //unload** (and never releases "core" plugins), so always unload before deploying. Single name `AioTest` works once IsCore returns 0.
- **Orphans**: objects you `create()` but never `remove()` stay in the handler lists and keep rendering after unload; cleared only by a **game restart**. Always remove on unload (slot 3).
- **Live tuning without rebuilds**: poll a config file (`plugins\aio_cfg.txt`) from the per-frame hook and apply it — lets you move/resize/recolour things by editing a text file. (Used to calibrate the geometry.)
- **Performance**: 1000 animated primitives (rect+color rewritten every frame) = **locked 59-60 fps, zero drop**. Solid-colour quads are ~free. Huge headroom for elaborate HUDs.

## See also
- [Plugin DLL contract (ABI)](plugin-abi.md)
- [Minimal working example](minimal-example.md)
- [Debug enumeration & address mapping](debug-and-mapping.md)
