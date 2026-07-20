---
title: Party Member Packets
summary: Inbound 0x0DD/0x0DF packet layouts for other party members, plus the memory alt for the alliance struct.
source: REFERENCE.md §9b
---
# Party member packets

`packet_in` (IPlugin slot 11) gives `b` = the **decoded** inbound packet (a safe,
fully-populated buffer). `id = *(u16*)b & 0x1FF`; size = `((hdr>>9)&0x7F)*4`.

**`0x0DD` — party member full update** (`PartyState::on_dd`). One member per packet:

| offset | type | field |
|---|---|---|
| `+0x04` | u32 | member server id (key; `0` → ignore) |
| `+0x08` | u32 | HP  (`0` ⇒ member is **out of our zone** → no vitals) |
| `+0x0C` | u32 | MP |
| `+0x10` | u32 | TP |
| `+0x14` | u32 | flags (party/alliance leader, quarter-master bits) |
| `+0x1D` | byte | MP % |
| `+0x1E` | byte | HP % |
| `+0x20` | u16 | member zone id (set when out of our zone) |
| `+0x22` | byte | main job |
| `+0x23` | byte | main job level |
| `+0x24` | byte | sub job |
| `+0x25` | byte | **sub job level** |
| `+0x28` | char[] | name (ASCII, ≤19) |

`+0x25` (sub level) was wired by symmetry with the memory finding — the packet carries the
job fields in the same order (`mjob@0x22, mlvl@0x23, sjob@0x24, slvl@0x25`), so `on_dd`
reads `+0x25` into `PMember.slvl`. Consistent with the [party-array `member+0x74`](party-array.md)
value, though not independently packet-probed. Reversed 2026-07-05.

**`0x0DF` — vitals refresh** (`PartyState::on_df`): `+0x04` id, `+0x08` HP,
`+0x0C` MP, `+0x10` TP. Only updates a member already known from a `0x0DD`.

> Note the **same field, different offset** between sources: HP%/jobs sit at
> different places in the packet (`+0x1E`,`+0x22`/`+0x24`) vs memory
> (`+0x64`,`+0x94`/`+0x9C`). Don't copy one offset set onto the other.

**Alt — party struct in memory** (used by `//aio chain`): `a=*(LuaCore+0x1C8400)`,
`b=*(a+0x248)`, `party=*b` → this is the **allianceinfo_t** (leaders/counts, see [party array](party-array.md)),
NOT a member array. Its `+0x18` pointers go to UI/menu structs, not clean member data
(an early wrong turn). The real member array is in [party array](party-array.md).

## See also
- [Local player struct](player-struct.md)
- [The party member ARRAY in memory](party-array.md)
- [Two traps that cost real time](traps.md)
- [Party-member buffs — the 0x076 packet](member-buffs.md)
