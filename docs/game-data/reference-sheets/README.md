---
title: Reference sheets — verbatim game mechanics
summary: BG-Wiki / in-game mechanics sheets kept as-is. Not AioHUD docs — the raw source the duration and debuff models were built from.
---

# Reference sheets

Game mechanics kept **verbatim**, exactly as collected. Nothing here describes AioHUD; these are the sources
its models were derived from, kept so a model can be re-checked against what it was built on rather than
against somebody's memory of it.

They are **git-tracked** (like the rest of `game-data/`) for the same reason the reversed offsets are: one of
them is a build input, and re-collecting the others costs real time.

| Sheet | Covers | Used by |
|---|---|---|
| [`song_resume.html`](song_resume.html) | BRD songs: skill caps, song slots, cast/recast reduction, **effect duration** | **Build input.** [`scripts/gen_song_dur.py`](../../../scripts/gen_song_dur.py) reads its rule *and* its percentages → `song_dur_gen.h` |
| [`threnody_resume.html`](threnody_resume.html) | Threnody family: elements, resistance-down potency | [song-potency.md](song-potency.md) |
| [`quick_draw_resume.html`](quick_draw_resume.html) | COR Quick Draw: Light/Dark Shot behaviour | [target-debuffs.md](../target-debuffs.md) — proved Light/Dark Shot **reinforce** a debuff without raising its tier |
| [`elemental_debuff_resume.html`](elemental_debuff_resume.html) | Elemental debuffs (Burn/Frost/Choke/Rasp/Shock/Drown) and the element wheel | [target-debuffs.md](../target-debuffs.md) — the overwrite rules in `overwrites_gen.h` |
| [`regen-duration.md`](regen-duration.md) | Regen: base durations, modifiers, the gear that lengthens it | [buffs-on-allies.md](../buffs-on-allies.md), `regen_dur_gen.h` |

## Why `song_resume.html` is load-bearing

It carries the one line that unblocked the whole song-duration model:

> *"Le gear « Song+ » (potence) ajoute **+10 % de durée par 1 Song+ ». Tous les bonus de durée d'équipement
> sont additifs entre eux, et additifs avec le bonus de durée des effets Song+ potence."*

Its duration table is also **authoritative over the item's own in-game text**: several pieces carry a duration
their description never mentions. Brioso Slippers +4 is the proof — no duration line in game, +15 % on the
sheet, and dropping it put the model exactly 15 points under a server measurement.

If you replace this sheet, re-run `python scripts/gen_song_dur.py` and then `tests.bat`: five server
measurements are pinned in [`tests/t_durations.cpp`](../../../tests/t_durations.cpp) and will catch a
regression that would otherwise stay invisible until somebody sang in game.

## Markdown sheets

Wiki pages kept verbatim, one per subject. They back a model somewhere in the code, so they are listed with
what they actually feed rather than by title alone.

| Sheet | Backs |
|---|---|
| [bard.md](bard.md) | The job itself: song list, JAs, merits and job points that touch duration — `song_dur.h` |
| [song-potency.md](song-potency.md) | Per-`+song` potency tables. **Read with [song-duration.md](../song-duration.md)**: the same `+1 <Song>` is also +10 % duration, a point this page long denied |
| [carnwenhan.md](carnwenhan.md) | Mythic dagger, song duration by ilvl stage — the `SHEET_DURATION` Carnwenhan rows |
| [fili-attire-set.md](fili-attire-set.md) | Empyrean BRD set: per-piece duration % and `+1 <Song>` potency |
| [composure.md](composure.md) | RDM ability — the ×3 self-duration multiplier in `enh_dur.h` |
| [lethargy-armor-set.md](lethargy-armor-set.md) | The Empyrean set that augments Composure (per-piece %) |
| [enhancing-magic.md](enhancing-magic.md) | Enhancing magic overall: skill is potency/interrupt, **never duration** |
| [enhancing-duration-gear.md](enhancing-duration-gear.md) | The gear granting "Enhancing magic effect duration" — pairs with [enhancing-duration-items.txt](../enhancing-duration-items.txt) |
| [regen-duration.md](regen-duration.md) | Regen's own duration gear — `regen_dur_gen.h` |
| [movement-speed.md](movement-speed.md) | The movement-speed stat — background for [movement-speed-analysis.md](../movement-speed-analysis.md) |
| [moonphase.md](moonphase.md) | Moon phase ids ↔ names and percentages |

> `enhancing-magic-wiki.md` and `fili-attire-set-brd.md` used to sit beside these. The first was
> byte-identical to `enhancing-magic.md`, the second a strict subset of `fili-attire-set.md`. Both deleted.

## See also

- [song-duration.md](../song-duration.md) — the model these sheets produce, with the measurements
- [../README.md](../README.md) — the game-data index
