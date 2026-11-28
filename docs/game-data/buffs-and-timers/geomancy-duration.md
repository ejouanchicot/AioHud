---
title: Geomancy duration (GEO Indi-) — computed aura lifetime
summary: GEO Indi- (skill 44) is an AURA whose 0x063 status is refreshed every ~3s by the pulse, so its duration is COMPUTED ((Base + JP1362×2 + flat Indicolure gear) × (1 + "Indi. eff. dur." augment %), measured 2026-09-13), the three self/normal/Entrust cases, the 542-556/612 noise filter, and the rule that only the Indi- you carry NOW can raise a red OUT.
source: model/geo_dur.h, model/geo_dur_gen.h (gen_geo_dur.py), party_state.cpp (act_ally_buffs skill-44 branch, record_geo_aura, selfGeo_, entrustTick_), model/timers_build.cpp (the self aura row), model/timers_rules.h (the noise filter), model/focus_rules.h (focus_geo_replaced)
---
# Geomancy duration (GEO Indi-)

> **Sondes citees ici.** Les commandes `//aio` nommees dans cette page ne sont pas toutes livrees.
> `geodbg` : **retiree(s) le 2026-09-14** — le code est garde dans `dev/archive/aiohud_probes_2026-09-14.cpp`, a remettre dans `aiohud_probes.cpp` pour refaire une capture.
> Ce qu'une release porte vraiment : `//aio help` (et `//aio doctor` en premier quand quelque chose ne va pas).

GEO **Indicolure** (Indi-) spells put a bearer status on the target, but Indi- is an **AURA**: a luopan-less
Colure that **pulses** — the effect status in the 0x063 buff block is **REFRESHED every ~3 s** by the
pulse, so it never shows the real aura lifetime (you'd see a useless 3 s countdown looping). Therefore the
duration is **COMPUTED, not read**. This is the skill-44 special case of
[buffs you cast on allies](buffs-on-allies.md).

## Model — `geo_dur.h` (measured 2026-09-13)

Flat seconds add first, then ONE percent term multiplies the sum:

```
dur = (Base + JP(1362)×2 + Σ(equipped "Indicolure ... duration +N" seconds)) × (1 + Σ "Indi. eff. dur. +N%" augments)
```

- **Base** = res/spells.lua duration (`tb_buff_gen`, skill 44). Indi- base is **180 s** (the retired
  `//aio geodbg` dump hardcoded 180 for comparison).
- **JP gift 1362** "Indicolure Spell Effect Dur." = **+2 s per rank**, **GEO main only** (`me.mjob == 21`,
  `read_jp_gift_rank(1362)*2`, `geoJpSec` in party_state.cpp). The sum is `geo_dur_sec(base, jp, gear, augPct)`
  (`geo_dur.h`).
- **gear** = native "Indicolure … duration +N" **flat seconds**, `geo_dur_gear_sec(eids)` over the 16
  equipped ids. The table `GEO_DUR_LISTED` is **generated** by `scripts/gen_geo_dur.py` from res
  item_descriptions.lua (`geo_dur_gen.h`) — universal / future-proof.

- **augments** = "Indi. eff. dur. +N%" (augment **1250 = 0x4E2**, a percent in `res/augments.lua`), read from the
  equipped items' extdata with the same system-1 decoder as the Enhancing augment (`geo_dur_augment_pct`).

**This page used to say "NO %".** It was wrong, and a measurement showed it: an Indi- **entrusted** to an ally in a set
with Gada "Indi. eff. dur. +11" and Lifestream Cape "Indi. eff. dur. +20" lasted **355-356 s** on the ally's own
"Colure Active" (612) timer, where the flat model said 271 s ; (180 + 40 JP + 51 flat) × 1.31 = **355.0**. The same GEO's
self aura, cast in a set WITHOUT those two pieces, was already exact (240 / 240 — which is why the percent had never
shown). Still no set bonus and no merit: the GEO group-1 merits are Indi/Geo POTENCY, not duration.

**Where the real duration is.** The effect status (539 Regen, 541 Refresh, ...) is re-applied by every pulse and reads
2-3 s. The aura's lifetime is the **"Colure Active" (612)** timer, on the GEO for her own aura and on the ally for an
entrusted one — the ground truth the measurement used.

Geo- (**luopan**) spells are a different spell-id range and are **NOT modelled** — a luopan puts no bearer
status on an ally, so it never reaches the buffs-on-allies path.

## Spell & status ids

- **Indi- spells 768–797.** All are Indicolure. (Geo-/luopan is a separate id range, ignored.)
- **Indi- buff effect statuses**: GEO-unique Boosts **542–556** (e.g. Indi-Fury = **549** Attack Boost,
  Indi-Barrier = **550** Defense Boost, …) + the **shared** statuses **539** Regen / **541** Refresh /
  **580** Haste. **"Colure Active" = status 612.**

## Three cases (party_state.cpp, `act_ally_buffs`)

Detected when `spell_buff(sid)->skill == 44`:

1. **Indi- on YOURSELF** (`aoeSelf`, i.e. a target id == `selfId_`) → `record_geo_aura(status, spell,
   ffxi_now_tick() + geo_dur_sec(base, JP, gear, augPct)×60)` stores the single `selfGeo_` aura you carry. The
   Timers build emits a **stable computed self row** (`model/timers_build.cpp`, the `self_geo()` block) at
   `geo_aura_remaining()`, replacing the 3 s pulse. The GEO box reads the same aura (`game_mem.cpp`,
   `auraLeftSec`).
2. **Normal Indi- on an ally** → **NO row.** The aura moves with you / pulses; there's no fixed buff to
   count. The ally loop is skipped (`if (b->skill == 44 && !geoEntrust) {} else …`).
3. **ENTRUST'd Indi- on an ally** → **a FIXED buff on that ally** (Entrust makes the Indi- stay on the
   target instead of following you). We DO record the ally row with the computed duration. Entrust is JA
   **386** (`entrustTick_`); the next skill-44 cast within **60 s** (`geoEntrust`) runs the ally loop and records the
   computed duration. **60, not 15** : measured, Entrust grants status **584 for 60 s**, and an Indi- cast 19 s after
   Entrust was entrusted by the game while the 15 s window drew no row at all. Any skill-44
   cast consumes the Entrust window (`entrustTick_ = 0`).

## The self-buff noise filter (`model/timers_rules.h` + `timers_build.cpp`)

The Duration column hides geomancy's 3 s-looping statuses for **everyone** (your own Indi-, or one you
merely stand in / receive): the row rule in `timers_rules.h` refuses
`(st >= 542 && st <= 556) || st == 612` (Boosts + Colure Active pulse noise), and a status whose
`geo_aura_remaining()` is `>= 0` (`geoAuraTracked()`, timers_build.cpp) is your carried Indi-, redrawn as the
stable row.

The **shared** statuses **539 Regen / 541 Refresh / 580 Haste** are **left visible** — real buffs still show.

## The "OUT" rule — only the Indi- you carry NOW may alert (`geoReplaced` → `focus_geo_replaced`, `model/focus_rules.h`)

You carry **exactly one** aura (`selfGeo_` is a single slot, not a list), so casting a **different** Indi-
**replaces** the previous one. The old effect status therefore leaves your 0x063 buff list — and the FOCUS
monitor, which knows nothing about geomancy, read that departure as a **loss** and drew a permanent red
**OUT**. Reported 2026-07-26: `Indi-Fury → Indi-Refresh → Indi-Regen` left **Fury and Refresh stuck OUT**
while only Regen was up. A swap is not a loss: you cannot put the old one back without dropping the one you
deliberately chose, so it must just **depop**.

`geoReplaced(entry)` suppresses the alert (and frees the monitor slot the next frame, exactly like the BRD
`songReplaced` rule it mirrors):

- **Is this a geomancy entry?** the **SPELL** first — `spell_buff(spell)->skill == 44` (`tb_buff_gen`) — with
  the **GEO-only statuses 542–556 / 612** as the fallback when the cast was never attributed. The status
  alone can NOT decide, because **539 Regen / 541 Refresh / 580 Haste are shared** with the real spells: a
  real Refresh must keep its normal OUT.
- **Was it replaced?** *self*: `self_geo().status` is set and is a **different** status → swapped out.
  *Entrust'd on an ally*: no per-ally aura slot exists, so it takes the evidence — a **newer skill-44 cast on
  that same ally** within **6 s** (the song rule's window: long enough for the 0x076 update the swap arrives
  in, short enough that a later dispel still OUTs).

An Indi- that simply **expires** still alerts — its status is still the one `selfGeo_` holds. `//aio oblog`
prints `geoRepl=` + the carried aura status on every focus entry; the focus trace logs `GEOOUT … SUPPRESSED`.

## `//aio geodbg` (retired 2026-09-14)

Dumped the per-slot Indicolure-duration gear and the total: `base 180 + JP1362 rank×2 + gear seconds`, to
calibrate against Timers.dll for the current set. Its code is in `dev/archive/aiohud_probes_2026-09-14.cpp`
(banner above); it predates the augment percent term.

## See also
- [Buffs you cast on ALLIES](buffs-on-allies.md) — the umbrella feature; GEO is its skill-44 special case.
- [Timers](timers.md) — the Duration column the self Indi- row and noise filter live in.
- [Party cast bar — 0x028](../party/cast-bar.md) — the cat-6 Entrust detection + cat-4 target parse.
- [Player equipment](../player/player-equipment.md) — the equipped ids the gear table scans.
