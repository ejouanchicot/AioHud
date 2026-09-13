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
| [enhancing-duration-measured.md](enhancing-duration-measured.md) | **83 casts against the server (2026-09-13)** : Composure on yourself (x3 up to 30 min), the set bonus on allies, and the attribution bug they exposed |
| [geomancy-duration.md](geomancy-duration.md) | GEO Indi- is an AURA refreshed every ~3 s by its pulse, so its duration is not what it looks like |
| [grimoire-sch.md](grimoire-sch.md) | SCH grimoire: buff ids, stratagem recast, the level/JP → (interval, maxCharges) table |
| [chants-barde-comment-ca-marche.md](chants-barde-comment-ca-marche.md) | Les chants du barde en français, sans code ni jargon |
| [status-icon-dat.md](status-icon-dat.md) | The game's own 640 status ICONS: the DAT record layout, its two alpha conventions, and which sheet the HUD draws |

## A trap worth naming: the ghost list

`buff_groups.h` hides nine properly-named statuses nobody can carry -- four of them songs (Chocobo Hum, Devotee
Serenade, Cactuar Fugue, Rhapsody). The hiding is conditional: `buff_group_members` lets a ghost through the
moment its `seen` predicate says the game has put it on somebody, because the list is an argument from silence
and silence must not become permanent (rule 10).

That same predicate is also how a caller asks for a group's whole CATALOGUE, and `[](unsigned){ return true; }`
therefore answered *both* questions at once -- resurrecting all nine ghosts in the party buff-order editor
(reported 2026-09-07: "Fugue, Hum, Rhapsody and Serenade are in the songs"). The fix is `bs_catalogue` in
`ui/party_config.cpp`: everything, **except** a ghost still has to have been seen. Anywhere else that wants a
full catalogue must use it rather than a bare `true`.

## See also

- [`../README.md`](../README.md) — the game-data index and the rule that governs this folder
- [`../../reverse-engineering/recipe.md`](../../reverse-engineering/recipe.md) — how to find the next offset
