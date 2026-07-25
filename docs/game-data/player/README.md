---
title: game-data / player
summary: Everything about YOU: the player struct and the chains hanging off it.
---

# Le personnage local

Everything about YOU: the player struct and the chains hanging off it.

| Page | Ce qu'elle contient |
|---|---|
| [player-struct.md](player-struct.md) | Pointer chain + field offsets for vitals, name, jobs, buffs — the root every other read starts from |
| [player-equipment.md](player-equipment.md) | The 16 equipped item ids (reversed from LuaCore's `get_items('equipment')`) |
| [inventory.md](inventory.md) | The item container at `*(G+0x50)`: 18 bags x 81 entries, plus the three u8[18] tables |
| [key-items.md](key-items.md) | Key items kept DECODED as a flat `u8[8192]` at `*(G+0x4C)` — not as the bitfield you would expect |
| [player-gil.md](player-gil.md) | Pointer chain for the gil amount |
| [encumbrance-flags.md](encumbrance-flags.md) | The u32 bitfield at 0x60 of packet 0x01B: which equip slots are locked |
| [movement-speed-analysis.md](movement-speed-analysis.md) | Target movement speed as a % of yours — the design analysis |
| [pointwatch.md](pointwatch.md) | XP / Capacity / Merit progression: packet offsets + load-time statics |
| [gear-icons.md](gear-icons.md) | Resolving a 32x32 icon per item id: bundled seed, else a live decode |

## See also

- [`../README.md`](../README.md) — the game-data index and the rule that governs this folder
- [`../../reverse-engineering/recipe.md`](../../reverse-engineering/recipe.md) — how to find the next offset
