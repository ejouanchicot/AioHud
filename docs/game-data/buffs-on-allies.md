---
title: Buffs you cast on ALLIES — estimated durations
summary: The Timers "buffs on ally" rows — 0x028 cat-4/6 detection keyed by (target, spell), the AoE self-mirror exact-timer trick, and the per-job estimation models (Enhancing skill 34, BRD songs skill 40, COR rolls cat 6). GEO Indi- lives in its own doc.
source: model/party_state.cpp (on_action cat==4/cat==6 actor==selfId_), model/enh_dur.h, model/song_dur.h, ui/hud.cpp draw_timers (C.tmMine block)
---
# Buffs you cast on ALLIES — estimated durations

The Timers box has an optional block (config `tmMine`, `hud.cpp:1853`) that shows **buffs YOU cast
on other players** — one row per (person, spell), `"Person - Spell"`, with a countdown. The catch: **the
client sends you NO per-buff timer for anyone but yourself** (your own buffs come exact from the 0x063
order-9 packet, see [Timers](timers.md)). So for allies the duration is **ESTIMATED** to reproduce the
Windower Timers.dll model — job by job — **except** the AoE self-mirror case below, which is **exact**.

## Detection (`on_action`, party_state.cpp:1102)

A buff **spell (cat 4)** with `actor == selfId_` that lands on a member ≠ you:

- Spell id = `getbits(p, 86, 16, size)` (actor.param). Mapped to `{status, base duration, skill}` via
  `spell_buff(sid)` (`tb_buff_gen`). A spell absent from the table (nuke/cure) records nothing.
- **Target gate**: each target id `getbits(p, base=150 + i*123, 32, size)`, resolved through
  `pc_name_by_id(tid)`. **Do NOT gate on the `(id>>24)==0x01` mob top-byte** — not all PC ids carry it;
  `pc_name_by_id` (roster + both alliances) is the reliable "is this a real party/alliance member" test.
- **Keyed by (target, SPELL)** — not (target, status) — so two tiers of the same song (Valor Minuet V +
  IV, one status) become two rows. Re-cast refreshes; the list caps at 32 (steals the oldest slot).
- **Pruned** by `prune_other_buffs_worn` (party_state.cpp:1406), which reconciles each row against the
  member's live **0x076** status icons (per status, keep only as many as the member actually shows,
  newest first) after a 3 s grace. This drops buffs that ended early (dispel / overwrite / death).

Rolls (cat 6) take the parallel path at party_state.cpp:1194 — see [COR Phantom Rolls](#cor-phantom-rolls-cat-6) below.

## AoE self-mirror — the EXACT case (no per-job model)

When a buff you cast **also lands on YOU** (AoE songs, `-ra` spells, Accession Indi-, rolls…), we already
have the **exact** server timer for it in the 0x063 self block. So the ally row just **mirrors your own
timer** instead of estimating:

- `aoeSelf` = any target id in the packet equals `selfId_`. Sets `OtherBuff.mirrorSelf = 1`
  (party_state.cpp:1171). The drawer reads `party().self_buff_remaining(status)` (hud.cpp:1860).
- The mirror lives **~2 s**, then `prune_other_buffs_worn` **snapshots** the exact remaining and
  **freezes** it: `ob.expTick = self_buff_expiry(status)` (party_state.cpp:1414). Freezing on the same
  FFXI-tick clock as the self rows means no drift/racing, and a later **self-only** re-cast can't bump the
  ally row. A cast that re-hits the ally re-arms `mirrorSelf`.

Because AoE buffs are exact via the mirror, the per-job models below only need to handle **single-target**
casts on an ally.

## Enhancing Magic (skill 34) — `enh_dur.h`

The Windower model (confirmed from its debug log + gear breakdown). Regular "Enhancing magic duration +%"
gear normally only affects **self**-cast; the RDM **Estoqueur's / Lethargy** set bonus *"Augments Composure"*
**unlocks** the caster's full enhancing-duration gear onto party members. Evaluated at cast time:

```
dur = (Base + flatSec)
    × (1 + setBonus)     // Estoqueur's/Lethargy worn count 2/3/4/5 -> +10/20/35/50%
    × (1 + listed%)      // native "Enhancing magic duration +N%" item stats (ADD within category)
    × (1 + augment%)     // augment "Enh. Mag. eff. dur. +N%" (extdata 0x4E0 + path augments; ADD within)
    × Perpetuance        // SCH: ×2.0 base .. ×2.65 (Arbatel Bracers +3)
    , capped at 1800 s (30 min).
```

The three % multipliers are **separate** (native × augment multiply; within a category the %s add).
Validated: Haste (180) + 20 JP, 4pc set, listed 88%, augment 49% → `(180+20)×1.35×1.88×1.49 = 756.3 s == Timers`.

Term by term (party_state.cpp:1117-1128):

- **flatSec** = **RDM-main only** JP gift **338** "Enhancing Magic Duration" (+1 s/rank, `read_jp_gift_rank`)
  + merit **2320** (+6 s/level, `read_merit_level`). `me.mjob == 5`.
- **setBonus** = `composure_set_pct(eids)` (`COMPOSURE_SET_IDS`, worn count → %), gated on **Composure
  status 419** being active (`read_player_buffs`). No Composure → no set bonus.
- **listed%** = `enh_dur_listed_pct` — the **generated** `enh_dur_listed_gen.h` (from res
  item_descriptions.lua, future-proof) + a small `ENH_DUR_LISTED_EXTRA` for qualitative-text relics.
  **Applies for ALL jobs.**
- **augment%** = `enh_dur_augment_pct` — extdata **0x4E0** decode (`% = value + 1`, see
  [Player equipment](player-equipment.md)) + `ENH_DUR_PATH` Delve-neck path augments. **All jobs.**
- **Perpetuance** = SCH status **469** → `perpetuance_mult(eids)`: ×2.0 base, up to ×2.65 with Arbatel
  Bracers +3 (`PERP_BRACERS`).

`//aio enhdur` dumps the per-slot breakdown.

## BRD songs (skill 40) — `song_dur.h`

RE'd from Timers.dll `FUN_10007a40`. `Base` = 120 (Miracle Cheer → flat 900):

```
dur = 120 × m1 × m2 × m3 + a3
m1  = 1 + Σ(per-item FLAT "song effect duration +%" gear) + 0.05 (BRD-main JP)
m2  = ×2 if Troubadour (status 348)
m3  = ×1.5 if (Soul Voice 52 OR Marcato 231) AND song family ∈ {11 Hymnus, 12 Mazurka, 14 Scherzo}
a3  = flat merit seconds: Clarion Call(499)/Tenuto(455) -> read_jp_u8(0x142)×2 ; Marcato(231) -> read_jp_u8(0x148)
```

**IMPORTANT**: the per-item **"+1 <Song>"** gear (Madrigal+1, Minuet+1, …) is **POTENCY, not duration**
(see [Song Potency](song-potency.md)) — the RE mis-read those `family/familyPct` columns into the timer's
m1 register, so `song_dur_m1_pct` deliberately counts **only the `flat` column**. Miracle Cheer is detected
at range slot (`ids[2]`). Family via `song_family_gen.h`.

## COR Phantom Rolls (cat 6)

A **Phantom Roll** is a job ability (**cat 6, not cat 4**) — party_state.cpp:1194. A roll **also lands on
the Corsair**, so like an AoE song every ally row **mirrors your exact 0x063 self timer** (`mirrorSelf`);
there is **no roll-duration model**. Detection: `abil_buff_status(aid)` in **310–339** (rolls) or **600**
(Runeist's Roll). Key facts:

- `OtherBuff.isAbil = 1` — `spell` holds an **ability id**, so the label resolves via **ABILS**
  (`abil_name_by_id`, hud.cpp:1793), **not SPELLS**. A roll's ability id collides numerically with a real
  spell id, so resolving through the spell table would mis-name the row.
- The block does **not** seed `selfBuffSpell_` / `selfCasts_` (those map a status → a *spell* id for the
  self-row name; a roll has no tiers, so the self row names fine via `buff_status_name`).

## See also
- [Timers](timers.md) — the exact self-buff Duration column + the Recast column this block sits beside.
- [Geomancy duration (GEO Indi-)](geomancy-duration.md) — skill 44, an AURA — computed, not mirrored; its own doc.
- [Composure](composure.md) · [Lethargy Armor Set](lethargy-armor-set.md) — the RDM set bonus that unlocks enhancing duration onto allies.
- [Enhancing duration gear](enhancing-duration-gear.md) — the native `+%` gear feeding `listed%` (extracted %s in `enhancing-duration-items.txt`).
- [Song Potency](song-potency.md) — proves the `+1 <Song>` gear is potency, not duration (why `song_dur.h` ignores the family columns); the extracted song-duration gear list is `song-duration-items.txt`.
- [Player equipment](player-equipment.md) — the equipped-item ids + extdata the augment decode reads.
- [Party cast bar — 0x028](cast-bar.md) — the ActionPacket bit layout (actor.param @bit 86, target stride 123).
- [Party-member buffs — 0x076](member-buffs.md) — the live status icons `prune_other_buffs_worn` reconciles against.
