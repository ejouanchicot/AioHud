---
title: Party Member Array in Memory
summary: The Ashita partymember_t array (18 slots) for instant full party at load, plus allianceinfo_t leadership resolution.
source: REFERENCE.md §9d
---
# The party member ARRAY in memory — instant full party at load (the real table)

The packet path only fills members the game *sends*; loading mid-party shows nothing
until packets arrive. The fix is to read the game's own **party member array** at load.
On **retail** this is the documented **Ashita `partymember_t`** (stride **`0x7C`**, **18
slots**: 0..5 = your party, 6..11 / 12..17 = alliance parties 2 / 3). Verified field-by-
field in-game (`model/party_state.cpp` `load_from_memory`).

**Anchor:** `g = *(LuaCore+0x1C8400)`; `pp = *(g+0x248)` (points **4 bytes into**
member[0]); `member[0] = pp - 4`; `member[i] = member[0] + i*0x7C`. Self-validate:
`member[0].ServerId` must equal the player id (else don't trust it).

> In code these chains go through the shared accessors in `model/game_mem.{h,cpp}` — **one source
> of truth** for the offsets: `data_root()` = `g`, `party_ptr()` = `pp`, `entity_array()` = `*(g+0x24)`,
> plus cached `luacore_base()` / `ffximain_base()`. Don't re-resolve `LuaCore+0x1C8400` inline.

| member offset | field | notes |
|---|---|---|
| `+0x0A` | name (char[18], ASCII NUL-term) | |
| `+0x1C` | u32 server id | the anchor-validation key |
| `+0x28` | u32 HP | |
| `+0x2C` | u32 MP | |
| `+0x30` | u32 TP | |
| `+0x34` | byte HP % | |
| `+0x35` | byte MP % | |
| `+0x36` | u16 zone id | member's zone (≠ yours ⇒ out of zone; offzone members are kept here) |
| `+0x3C` | u32 **flag mask** | quartermaster = **bit `0x10`** (verified: the bit moved to the member given QM) |
| `+0x71` | byte **main job id** | 1 WAR … 5 RDM … 22 RUN (verified: Kaories RDM=5, Tetsouo BLM=4) |
| `+0x72` | byte main job level | `0x63` = 99 |
| `+0x73` | byte **sub job id** | (verified: Tetsouo SCH=20, Kaories WHM=3) |
| `+0x74` | byte **sub job level** | reversed 2026-07-05 → `PMember.slvl` (`b[0x74]`) |

Jobs **are** in this struct (`+0x71`/`+0x73`, reversed 2026-06-28 from a live alliance dump
— an earlier note wrongly read `0`). Reading them here gives correct job badges for **every**
member of all three boxes instantly, with no dependence on packet timing. `load_from_memory`
runs every frame, so the roster + vitals + jobs stay live. Trust members (no job byte) fall
back to a name → job DB.

**Sub level (`+0x74`)** was verified because slot 0 of the member array is **SELF**, whose
sub level is known: `member[0]+0x74` read `52` = the player's real sub, and `member[1]`
(Kaories, RDM99/WHM sub) read `57` = correct. Like the local-player `slvl` at
[player-struct `+0xA0`](player-struct.md), this is the displayed/capped value. The debug
probe `//aio mjlvl` dumps each member's `+0x71/+0x72/+0x73/+0x74` bytes — it was used to
reverse `+0x74`. (Its matching needed a guard: the string `mjlvl` contains `jlvl`, the
existing player-dump probe, so the `jlvl` handler now excludes `mjlvl`.)

**Leadership is NOT a member flag — it's server-id matching against `allianceinfo_t`.**
`allianceinfo = *(pp)` (= `*(g+0x248)` dereferenced; this is the `//aio chain` "party"):

| allianceinfo offset | field |
|---|---|
| `+0x00` | u32 alliance leader server id (0 = no alliance) |
| `+0x04` | u32 **party-1 leader** server id (your party) |
| `+0x08` / `+0x0C` | u32 alliance party 2 / 3 leader ids |
| `+0x10`/`+0x11`/`+0x12` | byte party 1/2/3 visible |
| `+0x13`/`+0x14`/`+0x15` | byte party 1/2/3 member count |

A member is *party leader* iff its id == `+0x04`, *alliance leader* iff == `+0x00`.
Read live each frame (`read_party_leaders`) → the `*`/`^` markers follow the real role
automatically when leadership changes. Quartermaster is the only role that lives in the
member flag mask (`+0x3C` bit `0x10`), not here.

## See also
- [Party member packets](party-packets.md)
- [Local player struct](player-struct.md)
- [Target & SUB-target struct](target-substruct.md)
- [Two traps that cost real time](traps.md)
