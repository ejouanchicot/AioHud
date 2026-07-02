---
title: Notable conventions
summary: The codebase's non-negotiable rules — single source of truth for offsets, SEH everywhere, snapshot-not-poll, pixel-snapping, generated tables.
source: ARCHITECTURE.md §7
---
# Notable conventions in this codebase
- **One source of truth for offsets**: memory layout lives in the poller / `read_member`, never
  duplicated. Document every offset in [game data](../game-data/README.md) with how it was found and the date.
- **SEH everywhere we touch game memory**: `safe_read` (4 bytes) or one guarded block copy; a bad
  pointer degrades to a no-op, never a crash. Validate pointers with `valid_ptr`.
- **Snapshot, don't poll-in-draw**: widgets read `f.game`; only the poller (model) reads memory.
- **Pixel-snapped, half-pixel-offset 2D** ([D3D8 rendering](../reference/d3d8-rendering.md)) — non-negotiable for crisp text/borders.
- Generated tables (`*_gen.h`) come from `scripts/` — don't hand-edit; regenerate.

## See also
- [Reading the live roster](roster.md)
- [The per-frame data flow](data-flow.md)
- [Build / deploy / iterate](build-deploy.md)
- [Traps](../game-data/traps.md)
- [D3D8 rendering](../reference/d3d8-rendering.md)
- [Coordinates](../reference/coordinates.md)
