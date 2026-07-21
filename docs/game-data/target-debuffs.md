---
title: Target debuffs — detection, wear-off, and mob-behaviour inference
summary: How the Target module tracks debuffs ON a mob from the 0x028/0x029 packets by SPELL id, learns wear-offs, and infers sleep/crowd-control removal from the mob's own broadcast actions.
source: split from cast-bar.md (REFERENCE.md §9f)
---
# Target debuffs — detection, wear-off, and mob-behaviour inference

The Target module tracks debuffs **ON a mob** entirely from inbound action packets — FFXI keeps no
readable per-mob status list (see the investigation in [target-substruct.md](target-substruct.md)).
Native parse in `party_state.cpp` (`on_action` / `on_029` + the `record_debuff` / `record_th` helpers).

## Detect by SPELL id, not the per-target `param` (CORRECTED, 2026-07-04)

Debuffs are recorded from the **category-4 (finish)** action packet, identified **by the cast SPELL id,
NOT the per-target `param`**:

- The **reliable spell id is the ACTOR-level param `@ bit 86` (16 bits)** — what `parse_action` calls
  `actor.param` (confirmed via `//aio act`: **Bind = 258, Silence = 59**). `on_action` maps it through
  **`spell_debuff(spellId)`** → the generated **`src/model/tb_debuff_gen.h`** table (`{ spell, effect,
  durSec }`), giving the applied **status effect** + its **base duration**.
- Mapping by SPELL (not `param`) is what makes **every** enfeeble register (Distract / Frazzle / Addle /
  Slow II …) with no curated status-id list, AND cleanly separates a **debuff from a nuke** — a nuke/cure's
  spell simply isn't in the table (`spell_debuff` returns null → nothing recorded), so its damage `param`
  can never fake a debuff. It also gives Dia/Bio the right effect even though their `param` is damage.
- **Do NOT use the per-target ANIMATION field.** It coincides with the spell id for single-target casts but
  a `-ga` (AoE) spell shares one animation id that mis-maps (Sleepga showed a Frost icon).

**`tb_debuff_gen.h` is GENERATED** by `scripts/gen_tb_debuffs.py` from the old AioHUD addon's TargetBar
spell table (`vendor/targetbar/tb_spells.lua`), filtered to the debuff status-id set (buffs like
Protect/Haste excluded, so an ally buff is never mistaken for a mob debuff). Don't hand-edit — regenerate.

**Multi-target (AoE) parse — REVERSED via `//aio act` bit-scan.** The finish (category 4) lists EVERY target
hit: **target[0].id `@ bit 150`** (32b, was mis-derived as 152), **per-target STRIDE = 123 bits**
(target[1].id @ 273, …), and `target_count @ bit 72` (6b). `on_action` loops
`for i<tcount: tid = getbits(150+123*i, 32)` and calls `record_debuff(tid, effect, durSec*1000, bySelf)` for
each real server id (guard: `tid` top byte must be `0x01`). Tracks EVERY caster → Sleepga hits all mobs, and
other players' debuffs show.

## "No effect" / resisted — the per-target MESSAGE at bit 230 (REVERSED via `//aio dbflog`)

Unlike the caster-level fields, a cat-4 spell **finish carries a usable per-target action message**: for
target[0] it sits `@ bit 230` (`getbits(p, base+80, 10)`, `base = 150 + 123*i`, width 10). Two ids matter:

- **message 236 = the debuff LANDED** — record/refresh it.
- **message 75 = "no effect" / resisted / target already has it** — on it we `continue` **without** recording
  or refreshing. Without this gate, recasting **Sleep / Lullaby on an already-slept mob wrongly reset the
  countdown to full**.

(The *caster-level* `0x028` message reads `0`, which is why detection is driven by the spell-id table, not a
message — but the *per-target* result blocks do carry one.)

## The `tdebuffs_[DEBUFF_SLOTS]` slot table — `DEBUFF_SLOTS = 32` (raised from 8)

One `DebuffSet` per live mob id, keyed by the **unique server id**. On overflow, `record_debuff` /
`record_th` evict the **OLDEST set by `DebuffSet.touchMs`** (last time the set was updated), never an
actively-updated target.

**Why it was raised from 8 → 32:** at 8 slots, an AoE / `-ga` / Horde song debuffing a whole same-name pack
filled all 8, and extra mobs collapsed into slot 0 — so two mobs SHARED it and each recast wiped the other.
That was the "only one mob keeps its debuff box" / ping-pong depop bug. `struct DebuffSet` still holds up to
**16 status entries** per mob (`ids[16]`), plus `th` (Treasure Hunter level) and `lastHpp` (death / recycled-id
detection).

## Sleep / crowd-control removed from the MOB'S OWN behaviour

FFXI sends **no wear-off packet for many sleep/CC breaks** (a hit, a silent DoT tick, another player's action).
So beyond the exact `0x029` path below, sleep/CC removal is INFERRED from what the mob itself broadcasts.
Status groups (helpers in `party_state.cpp`):

- `is_sleep_status(s)` = **Sleep (2), Sleep II (19), Lullaby (193)** — all three sleep variants.
- `is_incapacitate_status(s)` = sleep-family **+ Petrification (7) + Stun (10) + Terror (28)** — statuses that
  PREVENT a mob from acting. (NOT Bind / Silence / Amnesia / Charm — a mob still acts under those.)
- `is_magic_block_status(s)` = **Silence (6)** only. Nothing we cast Mutes a mob (Mute is mob→player only) and
  Omerta is a player event status, so a mob casting a spell clears exactly Silence.
- `is_dot_status(s)` = **Poison (3), Kaustra (23), Burn/Frost/Choke/Rasp/Shock/Drown (128–133), Dia (134),
  Bio (135), Helix (186), Requiem (192)**.

### The wake message: 0x029 msg 204, param 2 (generic Sleep)

The "\<mob\> is no longer asleep" `0x029` is **message id 204 with param 2** — param 2 is the **GENERIC Sleep
status** even when the sleep came from **Lullaby (tracked as 193)** or **Sleep II (19)**. So `on_029`, when the
wear-off param is *any* sleep status, removes **ALL** sleep variants `{2, 19, 193}` from that mob — an exact
status match missed Lullaby. This covers a wake from **any cause** (hit, silent DoT tick, natural). msg 204 is
the general "recovers from X" message (also carries **Stun** as param 10). Reversed via `//aio dbflog`.

### A DAMAGING hit wakes sleep (cat 1/2)

On a category-**1** (auto-attack) or **2** (ranged) hit that DEALS DAMAGE (`param @ +63 > 0`, i.e. not a
miss/shadow) on target[0], `on_action` clears every `is_sleep_status` from that mob's set. A miss has `param 0`
and correctly does NOT wake.

### A slept/incapacitated mob that ACTS has recovered (cat 1/2/3/4/6/7/8/11)

When a tracked mob (`actor` top byte `0x01`) is the **ACTOR** of a `0x028` action in categories
**1/2/3/4/6/7/8/11** (melee / ranged / WS / spell-finish / ability / readies / begin-cast / mob-TP-finish), it
has clearly recovered → drop its `is_incapacitate_status` entries (Sleep 2/19/193 + Petrification 7 + Stun 10 +
Terror 28). A **magic cast (cat 4 or 8) ADDITIONALLY clears Silence (6)** — and ONLY a magic cast, because a
mob can still melee / use TP moves while Silenced. The **actor id names the exact mob**, so several same-name
mobs acting at once are disambiguated cleanly. Catches wakes we get no "no longer asleep" message for (someone
else woke it), straight from the mob's own broadcast action.

### DoT ticks wake sleep (silent — inferred in `record_debuff`)

DoT ticks are **SILENT** (they send no action packet), so a wake from another player's DoT never reaches us as a
`0x029`. We instead enforce a mutual-exclusion invariant in `record_debuff`, from what we DO track (every
caster's debuffs): **a DoT and a sleep cannot coexist on a mob.**

- Recording any `is_dot_status` on a mob **drops any sleep** already on it.
- A sleep landing on a **DoT'd** mob is a **no-op** (the DoT would just wake it).

## Wear-off from the `0x029` action-message packet (REVERSED & WIRED, 2026-07-03)

**RESULT (`//aio wear`):** a status wearing off a target is the inbound **`0x029`** packet whose **message id
`@ 0x18` (`& 0x7FFF`)** is one of the "wore off / recovered from" set **`{64, 204, 206, 321, 322, 350, 531}`**
(`is_wearoff_msg`, ported from the old tb_engine remove set). On it, **`param @ 0x0C` (u32) = the status id**
that wore off, and **`target @ 0x08` (u32) = the mob's server id** — a REAL id (`0x011060F3`), unlike the
garbage `0x028` target id @152. Captured cleanly on a Stun (msg=204, param=10, target=011060F3).
`PartyState::on_029()` removes that exact status from `tdebuffs_[target]` (inverse of `record_debuff`),
dispatched from `feed_packet` beside 0x028/0x076 — the icon drops the instant the debuff wears. It ALSO
**LEARNS the real duration**: `life = now − startMs` is stored into `learnedMs_[status]` (keeping the longest,
so the unresisted full duration wins) to sharpen future countdowns; the per-cast base (`debuff_base_ms`) stays
only as a fallback if a wear-off message is missed (out of range).

**No remaining-time exists in the packet.** FFXI computes enfeeble durations client-side (skill vs resist) and
doesn't display mob debuff timers, so there is no true timer to read. The tracker seeds each entry's countdown
from the table's `durSec`, learns the real lifetime from `0x029`, and falls back to a coarse
`debuff_fallback_ms` for a status never learned. Icons are the source of truth; timers are estimates.

Standard `0x029` layout the `//aio wear` probe decodes (bytes from packet start, LE):
```
actor  server id @ 0x04 u32     target server id @ 0x08 u32
param             @ 0x0C u32     value / param2   @ 0x10 u32
actor  index      @ 0x14 u16     target index     @ 0x16 u16     message id @ 0x18 u16 (& 0x7FFF)
```
**`//aio dbflog N`** (survivor) traces the next N debuff mutations to `aiohud_debug.log` (self-limiting);
it lives in `plugin/aiohud.cpp`. The **`//aio act` and `//aio wear`** probes used to reverse the layouts
above have since been **removed** — probes churn and live in the untracked `plugin/aiohud_probes.cpp`; the
fields they produced are still valid, and to recapture re-add a probe per
[architecture/re-probes.md](../architecture/re-probes.md).

## Display caps

The tracker holds up to 16 statuses per mob, and the drawers wrap past 16:

- **Detached debuff box** (`ui/hud_debuffs.cpp`): a single column up to 16, then **two balanced columns** past
  16 (`nCol = nd <= 16 ? 1 : 2`; 32 → 16+16, 20 → 10+10), column-major fill.
- **In-box target list** (`ui/target.cpp`, `BUFF_PER_LINE = 16`): one row/column of 16, a second past 16, so
  **up to 32** icons (2 × 16).

## See also
- [Party Cast Bar — the 0x028 action packet](cast-bar.md)
- [Target & SUB-target struct](target-substruct.md)
- [Target module — the TargetBar widget & its config](../design/target-module.md)
- [Hate List](hate-list.md)
