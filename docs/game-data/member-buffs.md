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

## Is there a per-ally buff TIMER anywhere? — NO. (Ghidra-closed, 2026-07-24)

**Definitive: the client keeps NO remaining-time / expiry for a party or alliance member's buffs —
not in memory, not in any packet.** Reversed from the FFXiMain dump (`re/ffximain_dump.bin`, base
`0x05C60000`; scanners `re/scratch_reg.py` + `re/scratch_dis076.py`). Evidence, so we never re-ask:

- **The `0x076` handler is `FFXiMain+0x098E80`** (found via the `register_incoming` push-id/push-fn
  scan, not the edx-relative dispatch table). Its **entire body is one instruction of work:**
  `memcpy(0x060E09A0, packet+4, 0x3C dwords = 240 bytes)` — a **verbatim copy** of the 5×48 payload
  into a static buffer at **`FFXiMain+0x4A09A0`** (runtime `0x060E09A0`). It computes nothing,
  timestamps nothing, stores nothing else, then `return 1`.
- **That 240-byte buffer is the client's ONLY store of ally buffs.** A full-image scan for references
  into `[0x060E0980, 0x060E0B00)` returns for `buf+0x000` exactly **three** sites: `0x098E8F` (the
  memcpy dest above), `0x098EA5` (the id→member lookup, `FFXiMain+0x098EA0`, stride `0x30`, end
  sentinel `0x060E0A90`), and `0x093683` (a reset routine that `rep stosd`-**zeroes** the buffer).
  So the buffer has **one writer (the 0x076 memcpy) and one clearer** — no other packet handler
  touches it, and none adds a time value.
- **Each 48-byte member entry is fully accounted for with no room for a timer:** id `u32 @+0x00`,
  4 pad, 8 bytes of packed high-2-bits `@+0x08`, 32 status low-bytes `@+0x10..+0x2F`. The decoder
  `FFXiMain+0x098EC0` reads exactly `low = entry[0x10+i]`, `hi2 = (entry[0x08+(i>>2)] >> 2*(i&3)) & 3`,
  `id = low + 256*hi2`, `0xFFFF` = empty — **byte-for-byte our `on_076` parse.**
- **The exact-timer packet `0x063` (`FFXiMain+0x0991A0`) is structurally SELF-ONLY.** It is a switch on
  the sub-command that `rep movsd`-copies each payload into **fixed singleton globals**
  (`0x60e2f98`, `0x60e306c`, `0x60e5830`, `0x60e58c4`, …) — the local player's char-update block. **No
  case is indexed by a member server id**, so the order-9 buff-timer array it fills is the player's
  own; there is no ally equivalent.
- The [party-member array](party-array.md) (`partymember_t`, stride `0x7C`) carries vitals/name/jobs
  and **no buff field at all** — buffs live only in the `0x060E09A0` buffer above.

**Consequence for AioHUD:** ally-buff durations MUST stay estimated (see [buffs on allies](buffs-on-allies.md)).
The only exact ally figure available is the **AoE self-mirror** (a buff that also lands on you → mirror your
own `0x063` self timer). The static RVAs above are client-version-specific — re-run the two `re/scratch_*.py`
scanners after a client patch to re-locate the `0x076` handler and its buffer.

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
