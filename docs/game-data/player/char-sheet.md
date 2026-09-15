---
title: Character sheet — the 0x061, 0x01B and 0x063-order-5 fields
summary: Attributes (base + gear), Attack/Defense, resistances, title and rank from 0x061 ; every job level and mastered flag from 0x01B ; Capacity and Job Points per job from 0x063 order 5. Offsets confirmed in game on 2026-09-15.
source: Windower addons/libs/packets/fields.lua, confirmed against the game's own Status menu and against Windower's player table
---

# The character sheet

Three packets AioHUD already received carried far more than it read. `model/charsheet.h` decodes them; this file
is what each field is, where it sits, and **how it was confirmed** — because two of these blocks have no
automatic witness at all and only a human reading the game's menu can settle them.

## What each packet gives

### `0x061` — Char Stats
Offsets include the 4-byte header, from `fields.lua`.

| field | offset | type |
|---|---|---|
| Maximum HP / MP | `0x04` / `0x08` | u32 |
| Main job / level, sub job / level | `0x0C`..`0x0F` | u8 |
| Current / required EXP | `0x10` / `0x12` | u16 |
| **Base STR DEX VIT AGI INT MND CHR** | `0x14`..`0x20` | u16 × 7 |
| **What is added to the base, same order** | `0x22`..`0x2E` | **s16** × 7 |
| Attack / Defense | `0x30` / `0x32` | u16 |
| Fire Ice Wind Earth Lightning Water Light Dark resistance | `0x34`..`0x42` | s16 × 8 |
| Title / Nation rank / Rank points | `0x44` / `0x46` / `0x48` | u16 |
| Master Level | `0x65` | u8 |
| Exemplar current / required | `0x68` / `0x6C` | u32 |

> **It is NOT a gear-only column, whatever it was called here for months.** Measured 2026-09-15 with the
> equipment untouched for three minutes: casting Sentinel moved `Defense` from **3618 to 2412**, and nothing else
> changed but the buff list. The block holds everything above the base — gear, buffs, food — which is also what
> the game's own Status menu shows, and is why the menu reading of `STR 158 +180` matched it exactly.
>
> Anything comparing this block against the equipped set must account for that: a change here does **not** imply
> a change of gear, and identical gear does **not** imply an identical block. A dashboard rule built on the
> opposite assumption accused the plugin of being "out of phase with its own equipment" for an evening.

**The gear column is SIGNED.** A piece can subtract from an attribute, and read unsigned a −5 comes out at
65531 and inflates the total. `tests/t_charsheet.cpp` pins it with a negative value; the mutation to unsigned
fails two checks.

### `0x01B` — Job Info
Measured length: **132 bytes (0x84)**.

| field | offset |
|---|---|
| Sub/job unlock flags | `0x0C` (u32, bit 0 = subjob unlocked) |
| **Level of each job**, id 1..22 | `0x49` + (id − 1), one byte each |
| Encumbrance flags | `0x60` — see [encumbrance-flags](encumbrance-flags.md) |
| **Mastered flag of each job** | `0x68`, a **BITFIELD** — bit `j` is job id `j` (bit 0 is padding) |
| **Master Level of each job**, id 1..22 | `0x6D` + (id − 1), one byte each |

> **The mastery flags are BITS, and this was read as 22 BYTES for months.** It agreed with Windower on four of
> the eleven mastered jobs — right often enough to survive a spot check. `fields.lua` spells the layout out:
> `bit[1] _junk1`, then 22 `boolbit`s, then `bit[1] _junk2` (24 bits, `0x68..0x6A`), a `_junk3` short at `0x6B`,
> then the 22 one-byte **master levels at `0x6D`** — which this decoder never read at all, while the very same
> numbers were being hunted through memory.
>
> The offline test could not catch it: its fixture wrote the flags as bytes too, because it had been written
> from the code instead of from the packet's layout. **A fixture that repeats the code's assumption is an echo,
> not a test.** Only the second witness — Windower parsing the same live packet — showed the disagreement.

**Each block floors on its own length.** The encumbrance reader needs `0x64`, the job tables need `0x7E`: one
floor for the whole packet would throw away a short `0x01B` that carries a perfectly good encumbrance field.

### `0x063` order 5 — Job Points
`job_point_info[jobId]` at `0x0C`, **6 bytes per job**, indexed by job id (not id − 1):

| field | offset in the entry |
|---|---|
| Capacity Points held | `+0` (u16) |
| **Job Points in RESERVE** | `+2` (u16) — the game caps this at 500 |
| **Job Points SPENT, lifetime** | `+4` (u16) |

**The middle field was called "spent" for months, and it is the reserve.** Measured 2026-09-15 against
Windower's `job_points[job].jp_spent`: where Windower reported **2100 spent** the plugin read **500** — the cap
on points held — and where Windower reported **0 spent** it read 73, 23, 2: the reserves of jobs never merited.
Nothing contradicted the wrong label until a second witness was asked for it. The PointWatch box shows `+2`
under the heading "JP", which is *not* wrong — the points you can spend are the reserve — only the code comment
was.

## How each block was confirmed

| block | witness | verdict |
|---|---|---|
| Job levels (22) | Windower `player.jobs` | **confirmed**, entry by entry, 2026-09-15 |
| Job Points spent | Windower `job_points[job].jp_spent` | **confirmed** after the `+4` correction |
| Max HP / MP | Windower `player.vitals` | **confirmed** (3209 / 1077) |
| Equipped set, 16 slots | Windower `get_items(bag, index)` | **confirmed**, 16/16 |
| **Attributes, base + gear** | **none** — see below | **confirmed by the player, in game** |
| **Resistances** | **none** | **confirmed zero**, see below |

### The two blocks with no witness

`windower.ffxi.get_player()` on this build exposes **no attributes**. The key list, dumped in game on
2026-09-15, is: `autorun, buffs, id, in_combat, index, item_level, job_points, jobs, linkshell,
linkshell_slot, main_job, main_job_full, main_job_id, main_job_level, master_level, master_levels, merits,
name, nation, skills, status…` — there is no `stats`, and nothing anywhere exposes the resistances.

So these were settled the only way left, by reading the game's own Status menu:

> **2026-09-15, Tetsouo, PLD 99 in Selbina.** The menu showed **STR 158 +180** and **VIT 158 +318**; the plugin
> decoded **STR 158 +180** and **VIT 158 +318**. The eight elemental resistances showed **0** in the menu, and
> the plugin decoded **0**.

That confirms the base block, the gear block *as a separate signed field*, and that an all-zero resistance row
is a **real state** — a character wearing no resistance gear — and not a bad offset. It does not, on its own,
prove the resistance offsets: zeroes would also come from padding. What brackets them is that the fields
*before* them (base and gear) are confirmed and the fields *after* them (title, rank) decode to sane values;
a non-zero resistance measured against the menu would close it completely, and is worth taking the day the
character wears one.

## The job table also lives in memory (measured 2026-09-15)

`0x01B` arrives on a **login or a job change** — *not* on a zone. So after a plugin reload the sheet had no job
table for as long as the player kept the same job, and the page showed nothing where twenty-two numbers belong.
Windower has them at all times, which proved they are in memory; only our address was missing.

Found with `//aio jobtab` (`dev/src/jobscan.cpp`), which is **given the real table from outside** and asked to
find where it is written — the opposite of being told an offset and asked whether it looks plausible:

| table | address | size |
|---|---|---|
| Job levels, id 1..22 | `*(g+0x48) + 0x3C6D` | 22 bytes |
| **Master levels**, same order | `*(g+0x48) + 0x3C91` | 22 bytes (levels + `0x24`) |

`g` is the LuaCore data root, and `*(g+0x48)` is the **same pointer `read_job_spent` already uses** for the Job
Points — one anchor for the block, not a second one to keep in step.

**How it was proven.** Two characters, two client processes, two different allocation bases, two different value
sets — Tetsouo (every job 99) and Kaories (`1,1,99,99,99,1,99,1,1,99,…,72,99,99,1`) — gave the **same offsets**.
And on Kaories the bytes read back were **identical to what Windower's own packet parser decoded from `0x01B`**,
entry by entry. That is what makes it a measurement rather than a lucky offset.

**The region is a private (heap) allocation, not the FFXiMain image** (FFXiMain sat at `060A0000` while the table
was at `05C7ECD1`). So there is no RVA to self-heal here: the pointer chain is the only stable route, and
`read_job_table_mem` validates before adopting — every level ≤ 99, every master level ≤ 50, the table must agree
with the main job's level we already know, and an all-zero table is refused as the blanked state during a zone.

> **A job unlocked but never levelled is stored as `1`, not `0`.** The packet says 1, memory says 1 — and
> `windower.ffxi.get_player().jobs` reports **0**. The witness is the one departing from the packet, so the
> in-game rule treats 1-vs-0 as a known difference. It never showed on a character whose jobs are all 99; it
> would have opened twenty-two false incidents on the first alt measured.

**The mastery FLAG is packet-only.** Memory carries the master *level* beside the levels, and "master level 0" is
not the same fact as "not mastered" — a job just mastered sits at ML 0. `CharSheet::masteredOk` keeps its own
validity bit rather than let one be derived from the other.

## The stat block lags the equipment by ~20 frames (measured 2026-09-15)

The equipped-item array and the character's stat block are **not refreshed together by the game**. `//aio gearskew`
(`dev/src/jobscan.cpp`) watched four real gear swaps frame by frame:

| swap | equipment changed | stat column changed | lag |
|---|---|---|---|
| Holy Circle, out | frame 112 | frame 130 | **18 frames** |
| Holy Circle, back | frame 154 | frame 177 | **23 frames** |
| Sentinel, out | frame 599 | frame 624 | **25 frames** |
| Sentinel, back | frame 641 | frame 666 | **25 frames** |

Roughly **half a second**. AioHUD reads both on the same frame, so during that window it holds the new set with
the old stat column — and it is *right* to: that is what the client itself holds. Nothing here is a defect to fix.

**What it means for anything that checks the two against each other**: a sample taken inside the window must not
be compared. The in-game sweep pairs a stat column only with a set that was already present in the *previous*
capture (captures are ≥ 1.5 s apart, well past 25 frames), and the dashboard's session rule only records a set
it has seen twice in a row. Before that gate existed, thirteen of sixteen sweep rows read "the set moved and the
stats did not" against a plugin that was reporting the truth.

## Where it lives

- Decoders: `model/charsheet.h` (pure, no globals, no clock) — `cs_read_061`, `cs_read_01b`, `cs_read_063_jobpoints`.
- Fed by: `on_char_stats`, `on_01b`, `on_set_update` order 5 — each beside what it already read, on its own floor.
- Also filled from the client's own mirror of the `0x061` body (`read_charsheet_mem`, `game_mem.cpp`), so the
  sheet is complete on LOAD instead of waiting for the next login / job change / zone. The mirror starts at the
  packet's `0x10`, so it can never answer for maxHP/maxMP or the jobs: `CharSheet::fromPacket` says which source
  filled it, and the cross-check only asks about those two when the packet did.
- Offline tests: `tests/t_charsheet.cpp`. In-game rules: `dev/scripts/igtest.py`.
