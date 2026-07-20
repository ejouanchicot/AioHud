---
title: PointWatch — XP / CP / Master Level + Merits
summary: The packet offsets and load-time FFXiMain static reads that feed the PointWatch progression bar (XP, Capacity/Job Points, Exemplar/Master Level, Merits) plus the X/h rate ring.
source: model/party_state.cpp (on_char_stats/on_set_update/on_exp_msg), model/game_mem.cpp (read_capacity_points/read_pointwatch)
---
# PointWatch — XP / CP / Master Level + Merits

One progression bar chosen by **job stage** + a Merits row. It is **packet-fed live**
and **memory-seeded on load** (packets 0x061/0x063 are infrequent — the box would be
empty until the game happens to send one). Ported from Byrthnoth's `pwcore.lua`.

**Stage logic** (`PartyState::pw_`): level < 99 → **XP**, 99 not-mastered → **CP**,
Master Level ≥ 1 → **ML/EP**. The **Merits** row always shows. Config `pwMode` can
force a stage (0 Auto / 1 XP / 2 CP / 3 ML).

## Packets (offsets include the 4-byte header, from Windower `fields.lua`)

### 0x061 Char Stats — `on_char_stats`
| field | offset | type |
|---|---|---|
| Main Job | 0x0C | u8 |
| Main Job Level | 0x0D | u8 |
| Current EXP | 0x10 | u16 |
| Required EXP | 0x12 | u16 |
| Master Level | 0x65 | u8 |
| Current Exemplar Pts | 0x68 | u32 |
| Required Exemplar Pts | 0x6C | u32 |

### 0x063 Set Update — `on_set_update` (dispatch on **Order @0x04** u16)
- **Order 2 (merits)**: Limit Points @0x08 (u16), Merit count = `p[0x0A] & 0x7F`
  (bit 7 masked off), Max Merit Points @0x0C (u8).
- **Order 5 (job points)**: a `job_point_info[24]` array @0x0C, 6 bytes each,
  indexed by **JOB ID** (not job−1). For the main job the entry is at
  `0x0C + jobId*6`: **Capacity Points u16 @+0**, **Job Points u16 @+2** (spent JP
  @+4 is only read from static memory below).

### 0x029 / 0x02D exp messages — `on_exp_msg`
Param1 (the gain value) @**0x0C for 0x029** / @**0x10 for 0x02D**; Message @0x18.
The gain increments the live `Current` (with wraparound) **and** feeds a `RateReg`
ring → the **X/h rate**.

| Message id | gained |
|---|---|
| 8 / 105 | XP |
| 718 / 735 | Capacity Points |
| 371 / 372 | Limit Points (merits) |
| 809 / 810 | Exemplar Points (Master Level) |

> **NOTE:** 0x029 is *also* routed to `on_029` (debuff wear-off) — `feed_packet`
> calls **both** handlers for that id.

**Constants:** CP → next Job Point = **30000**, Limit Points → Merit = **10000**,
max Job Points = 500 (XP caps at 55999).

**X/h rate ring** (`RateReg`, party_state.cpp): stores up to 128 `(tick, gain)`
samples; `rate()` sums gains within the last **600 s**, divides by the span
(`maxAge/1000`) × 3600. Returns 0 until ~30 s of data (`maxAge ≤ 29000 ms`) so a
single kill doesn't spike it.

## Load-time seed — client memory (packets are infrequent)

The reference addon uses Windower's `packets.last_incoming()` cache, which the
**plugin ABI does not expose** (slot 11 is a NEW-packet callback only — no cache).
So the box is seeded from client memory every frame in `poll_game_state` /
`load_from_memory`.

### CP / Job Points — CONFIRMED (`read_capacity_points`, game_mem.cpp)
From the LuaCore `g` root (reversed via LuaCore `FUN_10091110`):
```
base = *(g + 0x48)
entry = base + 0x306 + (mainJob-1)*6     // WAR = first entry
cp = u16 @ entry+0 ;  jp = u16 @ entry+2 // one u32 read: cp=low16, jp=high16
```
Client-persistent → the CP row fills on **load**, no packet needed. Spent Job
Points for a job = `u16 @ entry+4` (`read_job_spent`, used by the [SCH grimoire](grimoire-sch.md)).

### Exemplar + Merits + Master Level + XP — FFXiMain **static** data (`read_pointwatch`)
These are NOT under any LuaCore `g` pointer — a full-memory scan for the packet's
exact values pinned them in `FFXiMain.dll` static data (verified by hexdump).
Read off `ffximain_base()`:

| value | RVA | type |
|---|---|---|
| Current EXP / Required EXP | `FM+0x485644` | u16 / u16 @+2 (= exemplar−0x58, packet-mirror; DERIVED) |
| Master Level | `FM+0x485699` | u8 (= packet 0x65 = exemplar−0x03, same struct) |
| Current / Required Exemplar | `FM+0x48569C` / `FM+0x4856A0` | u32 / u32 (adjacent = the ML bar) |
| Limit Points | `FM+0x485826` | u16 |
| Merit count | `FM+0x485828` | `byte & 0x7F` |
| Max Merits | `FM+0x48582A` | u8 |

The merit block mirrors the 0x063 order-2 payload exactly (LP u16 @+0, count byte
@+2, max byte @+4). **XP is DERIVED**, not directly scanned: in 0x061 CurrentEXP is
0x58 bytes before Exemplar-cur and the client mirrors the packet body contiguously
(the two adjacent exemplar u32s prove it) → `0x48569C − 0x58 = 0x485644`.

> ⚠️ **These FFXiMain RVAs are CLIENT-VERSION-SPECIFIC.** Re-pin them after a
> client patch with **`//aio pwscan`**: it arms `pw_scan_061` / `pw_scan_063`, and
> on the next 0x061/0x063 they `scan_word_range` all memory for the packet's exact
> values, logging module+offset to `aiohud_debug.log`. Open the Status menu (and
> the Merit menu) to trigger the packets.
>
> An earlier `*(g+0x3C)+0x93` guess for Master Level was WRONG — it coincidentally
> held 15 for the ML15 reverse char but read 1 for an ML40 char. Always derive ML
> from the confirmed exemplar base, never trust a lone small-byte scan match.

Related `g`-root pointers (not read by `get_player`): merit struct base `*(g+0x44)`
(categories from +0x20; `+0x22` is the max-merit-CATEGORY, NOT packet Max Merit
Pts); player/EXP struct `*(g+0x3C)`.

## Widget & config
`Hud::draw_pointwatch` (hud.cpp) — per row a head line "label value [extra] …… rate"
+ a thin stage-coloured progress bar (XP green / CP blue / ML orange / Merits
purple). Center-anchored (`pwX` centre, `pwY` top), `EDITBOX_POINTWATCH`. Config
module "PointWatch" (`pw_config.cpp`): Display (Show / Size / Progression mode /
Rate X/h) + per-element Text (Label / Value / Rate). Lines `pw=` / `pwText%d=`.

## See also
- [Data channels](../architecture/data-channels.md) — why PointWatch needs both memory reads AND packets.
- [SCH grimoire](grimoire-sch.md) — reuses `read_job_spent` (job_point_info +0x04).
- [Player struct](player-struct.md) — the LuaCore `g` root and other pointer chains.
- [Reverse-engineering recipe](../architecture/reverse-engineering-recipe.md) — the scan/calibrate method behind `//aio pwscan`.
