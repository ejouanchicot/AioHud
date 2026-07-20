---
title: SCH Grimoire — Arts, Addendum & stratagem charges
summary: The buff ids, stratagem recast id, and level/JP→(interval,maxCharges) table that drive the Scholar grimoire book, charge count and recast timer.
source: model/game_mem.cpp (compute_grimoire, sch_recast_info, read_job_spent)
---
# SCH Grimoire — Arts, Addendum & stratagem charges

A free-floating book shown ONLY for a Scholar (job id **20**) main or sub. Computed
once/frame in `compute_grimoire` (game_mem.cpp) → `GameState.grimoire`. Ported from
AioHUD `targetbar/sch.lua`.

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
Interval (s) and max charges by **level & SCH spent Job Points**
(`read_job_spent(20)` = `job_point_info + 0x04`, see
[PointWatch load-time seed](pointwatch.md)):

| condition | interval | maxCharges |
|---|---|---|
| jp ≥ 550 | 33 s | 5 |
| level ≥ 90 | 48 s | 5 |
| level ≥ 70 | 60 s | 4 |
| level ≥ 50 | 80 s | 3 |
| level ≥ 30 | 120 s | 2 |
| else | 240 s | 1 |

For SCH **as a subjob** the result is capped: `charges ≤ 3`, `interval ≥ 80 s`.

- `recast = ability_recast_sec(231)` — the **Stratagems** recast (ability id 231).
- `count = maxCharges − floor(recast / interval)` (clamped ≥ 0; via the explicit
  `recast < N*interval` ladder in code).
- `timerSec = recast % interval` (−1 when full).

Ported from `sch.lua` exactly.

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
- [PointWatch](pointwatch.md) — shares `read_job_spent` (job_point_info +0x04 = spent JP).
- [Player struct — buffs](player-struct.md) — where the Arts/Addendum buff ids come from.
- [Party cast bar — 0x028](cast-bar.md) — the ability-recast source pattern.
