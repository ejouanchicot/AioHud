---
title: Limbus — Apollyon / Temenos tracker
summary: Zone Tracker mode 6 — the 0x075 battlefield bars (area, level, floor, gauge) and the 0x02A run economy (units, coffers, weekly allowance), including the seven Temenos-only defects fixed on 2026-07-19 and the dead ends not to re-chase.
source: docs/game-data/zone-tracker.md (Limbus sections, split out 2026-07-19) + model/party_state_zonetracker.cpp (on_limbus_075 / on_2a mode 6) + model/party_state.h (limbus_slot_of / limbus_slot_label)
---
# Limbus — Apollyon / Temenos tracker

> **Sondes citees ici.** Les commandes `//aio` nommees dans cette page ne sont pas toutes livrees.
> `limbuschip`, `limbuslog`, `limbusmem`, `limbusrun` : **retiree(s) le 2026-09-14** — le code est garde dans `dev/archive/aiohud_probes_2026-09-14.cpp`, a remettre dans `aiohud_probes.cpp` pour refaire une capture.
> Ce qu'une release porte vraiment : `//aio help` (et `//aio doctor` en premier quand quelque chose ne va pas).

Zone Tracker **mode 6**, entered by zone id: **38 = Apollyon**, **37 = Temenos**
(`zt_set_zone`, `party_state_zonetracker.cpp:327`). Two channels feed it and nothing is read
from memory except the entity name of an award's source:

- **0x075** — the generic battlefield timer/bars packet → area, level, floor, gauge.
- **0x02A** — zone messages → units, coffers, the weekly data allowance.

> **The whole module was written against Apollyon.** Every defect fixed on 2026-07-19 was a
> Temenos-only failure, and all seven were found by *reading one captured run*, not by guessing.
> Limbus is one entry per day and the Unique Data attempt one per week, so a probe that quietly
> drops half the content costs a real day — see [the probe trap](#probes) below.

## 0x075 — area, level, floor, gauge

Bar *n* sits at `+0x28 + n*0x14` of the packet, `{ s32 progress ; char label[16] }`, **six** of them
(Windower's `fields.lua` documents five). `on_limbus_075` (`party_state_zonetracker.cpp:399`) matches
bars **by label content, never by index** — 0x075 is multiplexed (other senders put position floats
there), so a bar whose label matches nothing touches no field.

| label form | wing | parsed into |
|---|---|---|
| `<Area>_Lv<NNN>` — `Apollyon_Lv119`, `Temenos_Lv135` | both | `limbusArea` + `limbusLevel` |
| `<Quad>_Floor_#<N>` — `SW_Floor_#3` | Apollyon | `limbusQuad` / `limbusFloor` + **this bar's** `progress` = the gauge |
| `<Side>_Tower_F<N>` — `North_Tower_F1`, `West_Tower_F3` | Temenos | same |
| `Uniq_Data0` | both | **nothing** — it tracks NMs, not data collection |

**Temenos had no gauge because the parser only looked for `Floor`** (`:426`). The `_Lv` bar still
matched, so the wing name showed while `limbusProgress` stayed -1 — exactly the reported *"Temenos
shows the name but never a progress bar"*. Both keywords are matched now (`:430-432`).

Temenos' `bar0` (`Temenos_Lv135`) carries `progress = -1` on **every** packet of a full run: it is a
difficulty **label carrier**, not a gauge. The gauge is `bar1`. Same shape as Apollyon, where the
2026-07-18 `//aio limbusmem` capture read:

```
bar0  progress=-1          label="Apollyon_Lv119"    <- area + level (label carrier only)
bar1  progress=96 -> 0     label="SW_Floor_#3"       <- THE FLOOR *and* THE GAUGE
bar2  progress=0           label="Uniq_Data0"
bar3..5 progress=7FFFFFFF  label=""                  <- unfilled
```

`progress` is a **percentage 0..100** signed int (the client renderer multiplies by `0.01f`);
sentinels (`-1`, `0x7FFFFFFF`) mean the gauge is inactive → stored as -1, nothing drawn (`:440`).
It fills and resets several times within one floor, and **what it counts cannot be answered from
the binary** — the server computes it. See [zone-tracker](zone-tracker.md) for the memory mirror at
`FFXiMain+0x480800` and the proof that the client holds nothing beyond it.

## 0x02A — the run economy

Message ids are zone-relative and drift across patches, so they are matched masked
(`pkt_u16(p,0x1A) & 0x3FFF`).

| masked | meaning | fields |
|---|---|---|
| **7247** | *"Acquired Apollyon units: N…"* | `p1` gained, `p2` zone pool left, `p3` **your total**, `p4` **your cap** |
| **7240** | *"Acquired Temenos units: N…"* | same four — was **7239** before a 2026-09 patch |
| **award + 41** | *"You may collect data N more times"* | `p1` = weekly allowance left — Temenos 7280, **7281** since the patch |
| 7069 / 7070 | key item gained / lost (bulk) — a floor validated / the run ended | `p1` = KI id — Temenos **7070 / 7071** since the patch |
| 6394 | item obtained (the coffer's item) | `p1` = item id |

### The award id keys on the WING — settled 2026-07-19

Apollyon **7247** / Temenos **7239**, proven because **Apollyon-coffer == Apollyon-point-of-interest
== 7247** while Temenos-point-of-interest == 7239 (`:488-490`). Consequence: **the id can never tell
a coffer from a point of interest.** Only 7247 was handled before, so an entire Temenos run recorded
nothing.

### The source is the target ENTITY, not the id

Bytes **0x18-0x19** of the 0x02A are an entity **INDEX**; **0x1A-0x1B** are the message id. Reading
all four as one u32 is why the first captures came back `<unresolved>` (`:492-494`). The index is
resolved by `entity_name_by_index()` (`model/game_mem.cpp:310`) — one indexed read into the entity
array, SEH-guarded, no scan:

- a coffer → `"Temenos Coffer #4"`,
- a point of interest → `"???"`.

The name test accepts `"Coffer"` (EN) and `"Coffre"` (FR), case-insensitively (`:504-507`). A client
in another language degrades to **"not a coffer"** — a *missed* chip, which is visible — rather than
a false one, and the miss is logged (`:514`).

> **Dead end, verified — do not rebuild on it.** 7069/7070 cannot discriminate: a point of interest
> grants the Code **only once per week**, so the accompanying item message is absent the rest of the
> time (`:495-496`).

### Units no longer come from mobs

Since a recent game update, **mobs pay no units at all** — units come only from coffers, points of
interest and floor key items (`:483-487`). The old *"~40-110 per kill"* note in these docs was stale
and cost real time: it suggested an Odyssey-style per-kill stream and led to reading msg 372's rising
`p1` as a running total. There is **no frequent update** in Limbus; the displayed total refreshes a
handful of times per run.

`LIMBUS_COFFER_MIN = 1000` / `LIMBUS_BIG_MIN = 5000` (`:19-20`) still separate a coffer-sized payout
from a small one, but the chip is now gated on the **entity name** as well, so a large
point-of-interest award no longer records a false chip.

`limbusRunUnits` is derived as `total - baseline`, **never** by summing `p1`: a duplicated packet
cannot double-count and a dropped one cannot under-count (`:509-510`).

### Weekly allowance = award id + 41

Captured 2026-07-19 from the **`Temenos Operator`** entity as 7280, `p1` = the count. A 2026-09 patch
shifted Temenos's whole table by +1 (tape `temenos_20260922_172633` : award 7240 every floor, KI 7070 per
floor, bulk 7071 at the end, then 7281 `p1=3` right after the final coffer). The constant 7280 went dead
with it, silently. The allowance is therefore read at `LIMBUS_WEEK_FROM_AWARD` (41) above the wing's
**healed** award id, so it moves when the healer re-proves the award. Apollyon's 7288 (= 7247 + 41) is
consistent with that offset but has **never been captured**.

### Temenos run, 2026-09-22 (tape) — what the packets said

- 0x075 bars : `Temenos_Lv119`, then `<Side>_Tower_F<N>` with the gauge in 20 % steps (5 kills) on
  N/W/E towers and 13/26/40/53/66/80/93 (≈7.5 kills) on Center ; between floors bar1 is simply absent
  for 2-3 s. `Uniq_Data0` stayed 0 all run. ~9 unrelated 0x075 per second, all rejected by the label filter.
- Floor clear : 7240 `p1=60` (Center : 120), always paired with 7070 = the next floor's KI.
- `p2 + p3` stayed constant (260015) all run : `p2` is a pool that shrinks as `p3` (your total) grows.
- The final coffer paid 3000 and moved the cap `p4` 204000 → 207000.
- Second run (North F1-F4, tape `temenos2_20260922_190659`) : no 7281 at entry, none without a coffer. The Operator's `Temenos units` option answers with a 0x05C carrying `p2 / p3 / p4` of the last 7240 exactly (129680 / 130335 / 207000) -- a ground truth for the total.
- **The weekly allowance is spent on OPENING THE COFFER, not on entering.** The coffer of 2026-09-22 20:21 paid 3000 and 7281 came with it carrying `p1 = 2` ; the player states the count was still 3 through the whole run before it. So the `2` that menu 1032 and 0x05C `1,1,2` carried at 19:30, while the true count was 3, is NOT the runs left -- it looked like a match only because the coffer had since spent one. Nothing reads it. A count seen at entry would need its own measurement, on a run whose two numbers differ.

## Towers, quadrants and the coffer row

A coffer sits on the **last floor of a quadrant**, so the row is four fixed slots
(`limbus_slot_label`, `party_state.h:227`):

| area | slots |
|---|---|
| Apollyon | `NW5 SW4 NE5 SE4` |
| Temenos | `N4 W4 E4 C3` — Northern / Western / Eastern towers have **4** floors, Central **3** |

Temenos read **N7 W7 E7 C4** before 2026-07-19; that was simply wrong.

**No Temenos coffer was ever recorded**, because `limbus_slot_of` (`party_state.h:236`) matched the
label exactly against `"N"/"W"/"E"/"C"` while the 0x075 label spells the side out (`North`, `West`).
Temenos now matches by **prefix** — which accepts `N`, `North` *and* `Northern` without guessing
which form a patch uses — while Apollyon keeps the exact match (its quads are two letters and share
initials) (`party_state.h:240-250`).

The floor tag is `%.7s #%d`, not `%.3s`: Apollyon's quads are two letters but Temenos spells its
sides out, which used to render as `"Nor #1"` (`:30-33`).

Dots: dim = not opened, red = 3k, green = 5k. Reopening a quadrant overwrites its slot. A 5k archives
then clears the other slots (cycle restart) and keeps its own green. **There is no weekly wipe** — the
row is the last known payout per quadrant and self-corrects (`:539-542`).

Persisted in its **own** per-character file (`limbus_%08X.bin`), not the zone cache — see
[per-character caches](../../architecture/per-character-caches.md).

## Run baseline — key items 9956..9998

- `9956..9980` Temenos — `Tem. N-F1..F7`, `W-F1..F7`, `E-F1..F7`, `C-F1..F4`
- `9981..9998` Apollyon — `NW #1..5`, `SW #1..4`, `NE #1..5`, `SE #1..4`

Owning **none** = no run in progress → counters restart. Owning some = the previous run is live and
its totals are kept. A KI store that is not reachable yet reads -1 = *unknown* and is treated as
**keep**, never as zero (`limbus_ki_owned`, `:37-42`).

## Probes

> **Ces sondes ont ete retirees le 2026-09-14** (elagage de `aiohud_probes.cpp`). Elles ne repondent plus en jeu : leur code est garde dans `dev/archive/aiohud_probes_2026-09-14.cpp`, d'ou il peut etre remis dans `src/plugin/aiohud_probes.cpp` si une capture doit etre refaite.

| command | what it does |
|---|---|
| `//aio limbusmem` | decodes the memory mirror at `FFXiMain+0x480800` (flags + 6 bars) |
| `//aio limbusrun` | arms a whole-run capture: 0x118 deltas, 0x02A ids+params, 0x075 bar changes, key-item diffs, and the chat line next to each id |
| `//aio limbuschip <quad> <k>` | seeds/clears a coffer dot by hand (coffers opened before the feature existed cannot be replayed) |

**The probe silently ignored half of Limbus.** `//aio limbuslog` filtered on `pkt[0x2C..0x2E] == "Apo"`,
written during the Apollyon reversing; a Temenos label starts `"Tem"`, so a whole run captured nothing
while the arming message still announced "Apollyon packets". It now accepts both wings, dumps all six
bars, logs 0x029/0x02A/0x02D unfiltered (raw **and** masked ids), and its budget is 4000.
**Before any Limbus capture, check the probe covers the wing you are entering.**

These probes live in `src/plugin/aiohud_probes.cpp`, which is **not tracked by git** — so they are not
in a release build. See [release checklist](../../architecture/release-checklist.md).

## Still unverified

- A payout `>= 5000` has **never been observed** — the green dot and the cycle reset are extrapolated
  from the 3000 case (the display path was exercised via `//aio limbuschip`).
- The weekly allowance rolling back up (new week) has not been seen.
- Whether the 5k moves between weeks; the persisted history will answer it after a few weeks.

## See also
- [Zone Tracker](zone-tracker.md) — the other five providers, the 0x075 battlefield block RE, and the widget/config.
- [Limbus currencies have no static — a negative result](limbus-currency-no-static.md) — why the unit totals cannot be read from memory.
- [Per-character caches](../../architecture/per-character-caches.md) — where `limbus_%08X.bin` and `zone_%08X.bin` live and why.
- [Release checklist](../../architecture/release-checklist.md) — why the probes above are absent from a tester's build.
- [Reverse-engineering recipe](../../reverse-engineering/recipe.md) — the masked-id lesson and the channels in cost order.
- [Static analysis of FFXiMain](../../reverse-engineering/static-analysis.md) — the dump and its 0x05C60000 base.
