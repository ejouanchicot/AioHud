---
title: Party-Member Buffs — the 0x076 Packet
summary: Self buffs from memory plus other members' status icons from the packed 0x076 packet, and the buff atlas.
source: REFERENCE.md §9h
---
# Party-member buffs — the `0x076` packet  (WORKING, 2026-06-28)

Status icons drawn to the **left of each party row** (2 stacked rows, mirror of
`design`'s `.pm-buffs`). Two **separate** sources — the `0x076` packet never carries
the local player, so **self** comes from memory and **everyone else** from the packet.

**Self buffs — memory** (reversed from LuaCore `get_player`, `FUN_10072040`). The same
`player = *(G + 0x3C)` struct holds a **32-entry `u16` array at `player+0x1C`**
(`[+0x1C, +0x5C)`); `0xFF` = empty slot. `get_player` loops it and appends every
non-`0xFF` id under the lua key `buffs`. Code: `read_player_buffs` (`game_mem.cpp`).

**Other members — packet `0x076`** (`PartyState::on_076`). `b` = decoded packet, same
base as `0x0DD` (payload at `+0x04`). **5 member slots of 48 (`0x30`) bytes:**

| per-slot field | offset (slot `k`) | notes |
|---|---|---|
| member server id | `k*48 + 4` (u32) | `0` = empty slot; local player never appears |
| buff `i` low byte | `k*48 + 20 + i` (i=0..31) | low 8 bits of the status id |
| buff `i` high 2 bits | `(p[k*48 + 12 + i/4] >> 2*(i%4)) & 3` | 8 bytes pack 32×2 high bits |

`buff = low + 256*high2`; `255` = empty. Buffs are kept in a transient
`PartyState::buffs_[18]` keyed by server id (NOT part of the cached roster — they
refresh every `0x076`); the UI reads them via `buffs_for(id)`.
*Credit: Kenshi/PartyBuffs + Byrth/GearSwap, via XivParty's `0x076` parse.*

**Icons:** a single atlas `assets/buff_atlas.raw` (1024×640 BGRA, 32-col grid of 32px
cells, id → cell `(id%32, id/32)`), built by `scripts/gen_buff_atlas.ps1` from
XivParty's `assets/buffIcons/*.png`. Ids ≥ 640 (outside the atlas) are skipped.

**Patch one cell.** `scripts/patch_buff_icon.ps1 -Id <n>` overwrites a **single** atlas
cell from `XivParty/assets/buffIcons/<n>.png` (SourceCopy → straight alpha, spliced at
`col=n%32, row=n/32`) **without** rebuilding the whole atlas — handy to fix/refresh one
icon. (The Target module reuses cell **32** as its movement-Speed detail icon.)

## See also
- [Local player struct](player-struct.md)
- [Party member packets](party-packets.md)
- [The party member ARRAY in memory](party-array.md)
