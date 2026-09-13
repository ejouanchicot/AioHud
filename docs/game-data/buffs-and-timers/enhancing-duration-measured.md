---
title: Enhancing Magic durations, measured against the server
summary: 83 casts on 2026-09-13, the model's number against the receiver's own 0x063 timer -- Composure on yourself, the set bonus on allies, and the attribution bug the measurements exposed.
---

# Enhancing Magic durations, measured against the server

**Date :** 2026-09-13. **Characters :** Kaories (RDM99/RNG, 20 ranks of the "Enhancing Magic Duration" job point gift,
merit 0 -- Windower agrees) and Tetsouo (PLD99/RUN), side by side in Selbina.

## Method

Both characters are logged in on one machine, each running AioHUD. For every cast :

- **the model's number** comes from the CASTER's plugin : the predicted expiry of a cast on yourself (the cast ring),
  or the estimate the Timers box draws for a buff on an ally (`otherBuffs_`) ;
- **the server's number** comes from the RECEIVER's plugin : the exact 0x063 expiry of that very buff.

So nothing is compared with a formula from a wiki : the server says how long the buff lasts, on the client that
carries it. The raw rows (caster state, gear snapshot, both numbers) are kept by the dev tooling. Pace : one support
spell every 15 s or more, no movement.

## 1. Without Composure, the model is exact

| Cast | Spells | model vs server |
|---|---|---|
| Kaories on Tetsouo | Haste II, Refresh III, Phalanx II, Protect IV, Shell IV, Regen II, Flurry II | all within 2 s |
| Kaories on herself | Refresh III, Protect IV, Shell IV, Regen II, Gain-DEX, Enfire II, Shock Spikes, Barfira, Aquaveil, Stoneskin, Blink | all within 3 s |
| Kaories on herself, idle gear (GearSwap off) | Aquaveil 793 / 794, Barfira 640 / 642 | within 2 s |
| Tetsouo on himself | Phalanx, Crusade, Reprisal, Enlight II | within 2 s |
| Tetsouo on Kaories | Protect IV (an AoE copy), Shell IV | within 1 s |

This also answers "does the model read the right inputs" : equipment AT CAST TIME (the midcast set, augments included),
the job point gift and the merit. A wrong input would not land 20 different casts within 3 s, in two different sets of
gear.

## 2. Composure on YOURSELF : x3, up to 30 minutes, never shortening

| Spell (on herself) | without | with Composure |
|---|---|---|
| Refresh III | 272 s | 817 s (x3) |
| Regen II | 147 s | 442 s (x3) |
| Haste II, Phalanx, Temper II, Enfire II, Shock Spikes | 637 s | 1800 s |
| Gain-DEX, Blink | 1020 s | 1800 s |
| Stoneskin | 820 s | 1801 s |
| Barfira | 1593 s | 1801 s |
| Aquaveil in idle gear | 794 s | 1800 s |
| Aquaveil in its duration set | 1975 s | 1975 s |
| Protect IV, Shell IV | 5797 s | 5797 s |

**Rule :** `on yourself = max(d, min(3 x d, 1800 s))`, where `d` is the duration WITHOUT Composure.

- The idle-gear Aquaveil is what settles the exception : at 1975 s Composure did nothing, at 794 s it went to 1800. So
  Aquaveil is not excluded -- a duration already past 30 minutes is simply left alone (and not cut down to 30).
- The Estoqueur's / Lethargy "Augments Composure" set bonus does **not** apply on yourself : Refresh III was cast with
  set pieces worn, the model with the set said 299 s, the server gave 817 = 272 x 3.

Before the fix the model predicted `d` (plus that set bonus) for your own casts : 636 s where the server gave 1800. The
visible rows were never wrong -- they draw the server's timer -- but the prediction is what pairs a timer with its
caster when several casts compete. Code : `composure_self_sec()` in `src/model/enh_dur.h`, measured cases in
`tests/t_durations.cpp`. After the fix : 14 of 14 self casts under Composure within 2 s.

## 3. Composure on an ALLY : the set bonus, no triple, no cap

With Composure up on Kaories, her casts on Tetsouo matched the multiplicative model (set bonus included) within 2 s :
Haste II 696 / 698, Refresh III 526 / 526, Phalanx II 905 / 907, Regen II 284 / 285, Flurry II 696 / 697. No x3. And
**no 30 minute cap on an ally** either : Protect IV on Tetsouo lasted 5795 s.

## 4. What the measurements exposed : a player's cast that replaced yours stayed "yours"

Kaories held her own Protect IV (5790 s left) ; Tetsouo cast Protect IV on her ; the server replaced it (1957 s left).
Kaories' model still credited HER. The cast ring had paired the live timer with Tetsouo's cast correctly ; then the
"a later cast re-applied it" check preferred the per-status caster latch -- and the latch had refused Tetsouo's
landing, because of a guard written for songs ("a trust singing another march must not take your live march").
Songs are the only buffs that run several copies on one status ; Protect has one, and the later cast replaces it.

Consequence : under "Mine only", another player's Protect showed as yours, and its loss could have raised your alert.
The guard is now song-only (`party_state.cpp`, tests in `tests/t_timers.cpp`). Re-measured in game after the fix :
the same sequence credits Tetsouo. A recorded session carried the old attribution for Phalanx in one field ; its
golden was rebaselined for that field, with the reason written beside the tape.

## 5. After a job change : Corsair rolls and Scholar (same day, evening)

Kaories as COR/DNC, Tetsouo as BLM/SCH.

| Cast | What | model vs server |
|---|---|---|
| Kaories' Phantom Roll, on Tetsouo | Chaos Roll 659 / 659, Samurai Roll 660 / 660 | exact (the ally copy follows the roller's own timer) |
| Roll total (the pip on the row) | Chaos 6, Samurai 3 | the model on BOTH clients = Windower's own action parser |
| Tetsouo, Light Arts + Accession, onto Kaories | Stoneskin 462 / 462, Blink 495 / 496, Aquaveil 858 / 858, Regen 75 / 75 | within 1 s |
| Tetsouo on himself | Stoneskin, Blink, Aquaveil, Klimaform | within 1 s |
| Tetsouo single-target on Kaories | Protect II 2970 / 2970, Shell II 2970 / 2971 | within 1 s |

Phantom Roll is ONE shared recast : a roll 15 s after another does not happen, so rolls are measured a minute apart.
Double-Up only exists within its window after a roll and was not measured.

## 6. Bard songs (Tetsouo BRD/DNC)

| Song | Case | model vs server |
|---|---|---|
| Valor Minuet V | on the party, your own copy (the prediction) | 343 / 344 |
| Blade Madrigal | on the party, your own copy | 355 / 357 |
| Knight's Minne V | Pianissimo on Kaories -- the song model's own estimate, no timer to borrow | 334 / 334 |
| Army's Paeon VI | Pianissimo on Kaories | 355 / 356 |
| Mage's Ballad III | Troubadour + Pianissimo on Kaories (x2) | 645 / 646 |
| Sentinel's Scherzo | Marcato + Pianissimo on Kaories | 995 / 996 |

The Pianissimo rows are the real test : nothing but the model decides them, and it lands within 1 s with Troubadour
and Marcato. The model is the one described in [song-duration.md](song-duration.md) (potency is duration).

## 7. Geomancy (Kaories GEO/WHM, outside town -- no Indi- goes off in Selbina)

- **The aura's real lifetime is the "Colure Active" (612) timer** ; the effect status (539, 541...) re-applies every
  pulse and reads 2-3 s. Self aura, Indi-Regen : model 230 / server 231 at +10 s, 180 / 181 at +60 s -- exact.
- **Entrust grants status 584 for 60 s.** The model armed Entrust for 15 s : an Indi- cast 19 s after Entrust landed
  on Tetsouo while the GEO's model drew no row. Window now 60 s ; re-measured with 20 s : the row exists.
- **The entrusted duration had a missing PERCENT** : model 271 s, server 355-356 s. The Entrust set carries Gada
  "Indi. eff. dur. +11" and Lifestream Cape "Indi. eff. dur. +20" (augment 1250, a percent), read from the items by
  Windower's extdata decoder : (180 + 40 JP + 51 flat) x 1.31 = 355.0. Fixed (`geo_dur.h`, tests with the real
  extdata bytes) ; re-measured in game : **model 355 / server 356**. See [geomancy-duration.md](geomancy-duration.md).

## Not measured (no character for it)

Soul Voice / Clarion Call, Double-Up and Crooked Cards, SCH Perpetuance (main-job SCH), and gear decomposition piece by piece. The
last one was left alone on purpose : a first attempt at changing gear lifted a GearSwap weapon lock on the character,
which had to be restored by hand -- gear experiments need the owner at the keyboard.
