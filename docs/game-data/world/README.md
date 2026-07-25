---
title: game-data / world
summary: Zones, maps, and the instanced content the Zone Tracker follows.
---

# Le monde et le contenu

Zones, maps, and the instanced content the Zone Tracker follows.

| Page | Ce qu'elle contient |
|---|---|
| [map-system.md](map-system.md) | The minimap: live zone id, the world→map-pixel transform, DAT decoding |
| [zone-tracker.md](zone-tracker.md) | The six zone providers (Dynamis granules, Abyssea lights, Odyssey, Limbus…) |
| [limbus.md](limbus.md) | 0x075 battlefield bars (floor + gauge) and the 0x02A run economy |
| [limbus-currency-no-static.md](limbus-currency-no-static.md) | Ghidra proof that the unit totals exist ONLY in the Currency menu rows — a documented dead end |
| [empypop.md](empypop.md) | Resolving a tracked NM's pop item / key-item chain against live inventory |
| [treasure-pool.md](treasure-pool.md) | 0x0D2 item-added / 0x0D3 lot-info offsets, the 5-min expiry, and the memory ground truth |

## See also

- [`../README.md`](../README.md) — the game-data index and the rule that governs this folder
- [`../../reverse-engineering/recipe.md`](../../reverse-engineering/recipe.md) — how to find the next offset
