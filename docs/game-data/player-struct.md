---
title: Local Player Struct
summary: Pointer chain and field offsets for reading the local player (vitals, name, jobs, buffs) from FFXI memory.
source: REFERENCE.md §9a
---
# Local player struct — read from memory

Pointer chain (this install, `LuaCore.dll 2.6.8.2`):
```
G      = *(LuaCore.dll + 0x1C8400)      // data root
player = *(G + 0x3C)                    // local player struct
```
**Validity gate:** read `max_hp` first; if `0` or `> 0x100000` the struct isn't
populated yet (mid-zone) — bail and keep last good values. Right after a zone the
**vitals populate before the name/job strings**, so gate on vitals, not the name.

Field offsets (relative to `player`), all confirmed against a live hexdump
(`//aio dump <player> 200`):

| offset | type | field | notes |
|---|---|---|---|
| `+0x00` | u32 | server id | e.g. `0x0006BD0B`; matches the packet member id |
| `+0x08` | char[] | **name** (ASCII, NUL-term, ≤18) | "Tetsouo" |
| `+0x1C..+0x5C` | u16[32] | **own buffs** (status icons) | `0xFF` = empty slot; ids are FFXI status ids. See `read_player_buffs`. The `0x076` packet never carries self → read here. |
| `+0x5C` | u32 | HP | |
| `+0x60` | u32 | **max HP** | the validity gate |
| `+0x64` | byte | HP % (0..100) | the game's own %, adapts to current max |
| `+0x68` | u32 | MP | |
| `+0x6C` | u32 | max MP | |
| `+0x70` | byte | MP % (0..100) | |
| `+0x74` | u32 | TP (0..3000) | |
| `+0x7D..+0x92` | byte[22] | **job-level array** (WAR..RUN) | all `0x63`=99 on a maxed char — see the trap in [traps](traps.md) |
| `+0x94` | u32 (low byte) | **main job id** | `4`=BLM. Ids: 1 WAR…22 RUN (see `JOBS[]` in `party_state.cpp`) |
| `+0x98` | u32 | main job **level** | `99` — NOT the job (cost us an afternoon, [traps](traps.md)) |
| `+0x9C` | u32 (low byte) | **sub job id** | `20`=SCH |
| `+0xA0` | char[] | linkshell name (ASCII) | "Harfang des Neiges" |
| `+0xB4..` | u16[] | combat/magic skills | `0x80xx` packed, not yet decoded |

## See also
- [Party member packets](party-packets.md)
- [The party member ARRAY in memory](party-array.md)
- [Two traps that cost real time](traps.md)
- [Party-member buffs — the 0x076 packet](member-buffs.md)
