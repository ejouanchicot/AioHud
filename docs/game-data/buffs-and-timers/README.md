---
title: game-data / buffs-and-timers
summary: Where a buff comes from, and how long it is going to last. The most bug-prone corner of the project.
---

# Buffs et durées

Where a buff comes from, and how long it is going to last. The most bug-prone corner of the project.

| Page | Ce qu'elle contient |
|---|---|
| [timers.md](timers.md) | The 0x063 order-9 self buff-timer packet (absolute FFXI ticks) + the client recast tables |
| [member-buffs.md](member-buffs.md) | Self buffs from memory, other members' icons from the packed 0x076 |
| [buffs-on-allies.md](buffs-on-allies.md) | The Timers `tmMine` rows: 0x028 detection, the AoE self-mirror trick, the per-job estimation models |
| [client-clock.md](client-clock.md) | How the client computes "now" for buff timers — a pulled monotonic clock plus a signed server offset |
| [song-duration.md](song-duration.md) | **The BRD song duration model** and the rule that had it wrong for months (potency IS duration) |
| [song-duration-items.txt](song-duration-items.txt) | Per-item song-duration percentages lifted out of `Timers.dll` (historical; the formula was right) |
| [enhancing-duration-items.txt](enhancing-duration-items.txt) | Same, for Enhancing Magic duration gear |
| [geomancy-duration.md](geomancy-duration.md) | GEO Indi- is an AURA refreshed every ~3 s by its pulse, so its duration is not what it looks like |
| [grimoire-sch.md](grimoire-sch.md) | SCH grimoire: buff ids, stratagem recast, the level/JP → (interval, maxCharges) table |
| [chants-barde-comment-ca-marche.md](chants-barde-comment-ca-marche.md) | Les chants du barde en français, sans code ni jargon |

## See also

- [`../README.md`](../README.md) — the game-data index and the rule that governs this folder
- [`../../reverse-engineering/recipe.md`](../../reverse-engineering/recipe.md) — how to find the next offset
