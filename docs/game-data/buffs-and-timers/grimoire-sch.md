---
title: SCH Grimoire — Arts, Addendum & stratagem charges
summary: The buff ids, stratagem recast id, and level/JP→(interval,maxCharges) table that drive the Scholar grimoire book, charge count and recast timer.
source: model/game_mem.cpp (compute_grimoire, sch_recast_info, read_job_spent)
---
# SCH Grimoire — Arts, Addendum & stratagem charges

A free-floating book shown ONLY for a Scholar (job id **20**) main or sub. Computed
once/frame in `compute_grimoire` (game_mem.cpp) → `GameState.grimoire`. The book,
its Arts colours and its visibility are ported from AioHUD `targetbar/sch.lua` ; the
charge arithmetic below is NOT — the addon's table applied the Job-Point gift to a
subjob and then clamped it to a state the game cannot produce (see below).

## Visibility
- SCH **main** → always show.
- SCH **sub** → show UNLESS the sub-job-restriction buff **157** is up, OR
  (`zone ∈ {298, 39, 40, 41, 42}` AND sub level 0).

## Arts / Addendum (from player buffs, priority Addendum > Arts)
| buff id | meaning | book |
|---|---|---|
| 401 | Addendum: White | light book + aura |
| 402 | Addendum: Black | dark book + aura |
| 358 | Light Arts | light |
| 359 | Dark Arts | dark |
| (none) | no Arts | dim light book |

## Charges + recast timer

The client keeps **one** recast slot for the whole stratagem pool — ability recast
id **231** (`ability_recasts.lua`: `[231] Stratagems`; id 233 is the *Stratagems*
menu entry, not the pool) — and it counts down to the pool being **FULL again**,
never to the next charge. So both numbers on the book are derived:

| Scholar level in play | max charges | one charge |
|---|---|---|
| ≥ 90 | 5 | 48 s |
| ≥ 70 | 4 | 60 s |
| ≥ 50 | 3 | 80 s |
| ≥ 30 | 2 | 120 s |
| ≥ 10 | 1 | 240 s |
| < 10 | 0 | — |

The pool always refills in **240 s**, so one charge is simply `240 / charges`.
The Job-Point **gift** “Stratagem Recast Time” (**550 JP spent** on SCH) takes
15 s off that interval: 48 → **33 s**. A gift only counts on the **main job**, so
a job subbing /SCH never gets it. The subjob level is **read from the game**
(`gs.me.slvl`), never assumed: **Master Levels raise it past the old 49 cap**
(measured: PLD ML 48 → sub level **58**), so a /SCH on a mastered character is over
50 and gets **3 charges / 80 s**, while one with little Master Level sits at 49 and
gets **2 / 120 s**. The addon's flat “clamp a subjob to 3 charges” was right only
for the first case. (`read_job_spent(20)` = `job_point_info + 0x04`, see
[PointWatch load-time seed](../player/pointwatch.md); it is not even read for a /SCH.)

Given `ticks` = the raw 1/60 s pool counter (`GameState::recasts`, entry 231):

```
per         = interval * 60
recharging  = ceil(ticks / per)            // charges still coming back
charges     = max(0, maxCharges - recharging)
timerSec    = ceil((ticks - (recharging-1) * per) / 60)   // to the NEXT charge, -1 when full
```

The **raw ticks**, not the ceil-ed seconds every other reader gets: spending one
charge sets the pool to exactly one interval, and a ceil-ed `33` against a 33 s
interval lands on the wrong side of a `<` — one stratagem out of a full book read
as two spent, for a whole second.

Three modules share this one computation: the book’s two pastilles, the
**Timers** row (`Stratagem [n]`, counting down to the next charge —
`timers_build.cpp`), and the floating **Cost/Next** box, which for recast 231
shows the next charge plus `Charges n` instead of the raw pool time (a raw
“Next 2:30” while three charges sit ready is the opposite of what the box is for).

### The interval is LEARNED, not looked up

Spending a stratagem adds **exactly one interval** to the pool timer, so the client
states its own number every time you use one. Between two polls the pool also *falls*
by the time that passed, so for a poll at `t0` and the next at `t0 + D`:

```
pool1 = pool0 + interval − D        →   interval = jump + D
```

…whenever the spend happened inside that window. `D` is measured with
`model_now_ms()` and **must** be added back: a first version dropped it and took the
*smallest* jump seen, which learned **47 s** on a client whose four clean spends had
all said 48 (`//aio schlog`, 2026-09-16). The minimum is not the truth — a double
spend pushes a jump up, a slow frame pushes it down.

So: reject any measurement longer than the table (only the gift shortens the
interval, and a jump that long is two stratagems inside one window), and keep the
**largest** of what remains. A slow frame can then only lose to a cleaner
measurement, and a bad first value repairs itself on the next spend. The learned
value overrides the table — the count is right for a Scholar whose numbers were never
measured (the gift, a future SE change, any level) — and the table is only the answer
*before* the first spend of a session. Learning is dropped on a job/level change, and
never happens under **Tabula Rasa** (buff 377), which refills the pool and moves its pace.

Two last-resort guards: the pool can never run longer than `maxCharges × interval`
(a pool that does proves the interval is too short → fall back to `240 / charges`,
rather than tell a Scholar holding three charges that he has none), and `grim.interval`
/ `grim.maxcharges` are published in the harness dump so `igtest` judges **what was
actually used**, not a table it re-derives.

**Measured** (2026-09-16, `//aio schlog`): SCH main 99 with **0 JP spent**
(`sheet.job20` mastered=0) — four spends in a row, each jump in the pool counter
`+2879/+2852/+2880` ticks → **48 s**, exactly the ungifted row, and the pool ran to
8879 ticks (147 s) with 1 charge left. The **33 s** (gifted) row is the one value
no capture has confirmed yet, so the poller guards it: a pool longer than
`maxCharges × interval` proves the interval is too short and falls back to
`240 / charges`, instead of telling a Scholar holding three charges that he has none.

### Proving it in game — `//aio schlog [sec]`
Logs the decision (level, spent JP, the interval and max charges they produce)
next to the raw counter, one line per second, to `aiohud_debug.log`. On every
spend it also **measures** the interval: a spend adds exactly one interval to the
pool, so the jump between two frames *is* that interval, and the line says
whether the measurement matches the table.

## Textures & widget
`assets/grimoire_light.raw` + `grimoire_dark.raw` = the addon's `Grimoire-*-HD.png`
downscaled to **512×342 BGRA** (`scripts/gen_grimoire.py`), loaded via
`load_raw_texture(dev, path, 512, 342)`. `Hud::draw_grimoire` draws the book quad +
a pulsing Addendum aura (additive `rrect_glow`, gold `#FFD766` light / purple
`#B98CFF` dark, 2.8 s breathe) + two pastille number tokens (charge @17%/78%, timer
@81%/78%). Center-anchored (`grimX` centre, `grimY` top), `EDITBOX_GRIMOIRE`.
Config module "Grimoire (SCH)" (`grim_config.cpp`): Display (Show / Size / Preview
art) + per-element Text (Charge / Timer). Lines `grim=` / `grimText%d=`.

## See also
- [PointWatch](../player/pointwatch.md) — shares `read_job_spent` (job_point_info +0x04 = spent JP).
- [Player struct — buffs](../player/player-struct.md) — where the Arts/Addendum buff ids come from.
- [Party cast bar — 0x028](../party/cast-bar.md) — the ability-recast source pattern.
