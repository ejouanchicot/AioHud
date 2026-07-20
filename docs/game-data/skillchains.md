---
title: Skillchains — resonance detection from 0x028
summary: The 0x028 action-packet bit offsets that detect a skillchain OPEN (step-1) / CLOSE (step+1), the property/combo tables, and the usable-move continuation prediction.
source: model/party_state.cpp (on_action skillchain block), model/skillchain.{h,cpp}, model/skillchain_gen.h
---
# Skillchains — resonance detection from 0x028

Detects an active skillchain window on the target and predicts which of your moves
continue it. **Pure 0x028 bit-reads** — no memory reverse for detection (the
continuation *prediction* reads usable-move bitmaps, below). Runs for ANY actor
(you or your party). The block lives at the top of `PartyState::on_action`.

## The finish categories → skillchain resource
The 0x028 `category` @bit 82 (4b) selects the resource table:

| cat | resource | source |
|---|---|---|
| 3 | `SCR_WS` | weaponskill_finish |
| 4 | `SCR_SPELL` | spell_finish (SCH/BLU/nukes) |
| 11 | `SCR_MOB` | mob_tp_finish (BST charmed / jug pet ready moves) |
| 6, 13, 14, 15 | `SCR_JA` | job ability / avatar blood pact / `*_run` |

(SCH's 8 elemental magics = `SCR_ELEM`, added for the prediction when mjob = SCH.)

## The bit offsets (a finish action)
Requires `size*8 ≥ 309`. `size = ((hdr>>9) & 0x7F) * 4` bytes.

| field | bit | width | note |
|---|---|---|---|
| actor id | 40 | 32 | the acting entity |
| category | 82 | 4 | finish-category selector above |
| **action id** (`actor.param`) | **86** | 16 | the WS/spell/ability id (all finish cats) |
| target[0].id | 150 | 32 | must be a real server id (`>>24 == 0x01`) |
| has add-effect | 271 | 1 | the CLOSE gate |
| **add-effect animation** | **272** | 6 | 1..16 → the resulting property (`sc_from_add_effect_anim`) |
| add-effect message | 299 | 10 | must be a skillchain msg (`sc_is_skillchain_msg`) |
| finish message | 230 | 10 | the OPEN gate (`sc_is_finish_msg`) |

> These offsets share the ActionPacket layout used by the [cast bar](cast-bar.md)
> (target[0].param @bit 213 = target base 150 + 63; per-target stride 123 bits).

## OPEN vs CLOSE
For a finish whose `target[0].id` is a real mob:

- **CLOSE** — `getbits(271,1) && sc_is_skillchain_msg(getbits(299,10))`: the hit
  carries a skillchain added effect. `prop = sc_from_add_effect_anim(getbits(272,6))`
  is the **resulting property**; `sc_close(tid, aid, res, prop, delay)` advances the
  window to step+1 (delay from the move's `SkillRow`, default 3 s).
- **OPEN** — else `sc_is_finish_msg(getbits(230,10))`: a plain WS/spell finish opens
  a **step-1** resonance from the move's OWN skillchain property (`sk->prop` from
  `sc_skill_lookup(res, aid)`), via `sc_open(tid, aid, res, prop, delay)`.

Message-id sets (`skillchain.h`):
```
sc_is_skillchain_msg : [288..301] ∪ [385..397] ∪ [767..770]   // CLOSE add-effect
sc_is_finish_msg     : {110, 185, 187, 317, 802}               // OPEN "uses <WS>"
```
`sc_from_add_effect_anim`: animation 1..16 → Light, Darkness, Gravitation,
Fragmentation, Distortion, Fusion, Compression, Liquefaction, Induration,
Reverberation, Transfixion, Scission, Detonation, Impaction, Radiance, Umbra.

## Data tables
- `src/model/skillchain.h` — the 16-property (`SCProp`) / 8-element (`SCElem`) enums,
  names + colours, and **`SC_INFO`** (`sc_info(p)`): per property its burst elements,
  level, and the `OLD.with(NEW) → {lvl, result}` combinations. `sc_check_props`
  (skillchain.cpp) = the addon's `check_props` (does a candidate move's opening
  props continue the active chain, and to what level/result).
- `src/model/skillchain_gen.h` — **generated** (`scripts/gen_skillchain.py`, don't
  hand-edit): per-action `SkillRow {id, prop[3], delay, aeonic}`, binary-searched by
  `sc_skill_lookup(resource, id)`. **814 rows**: WS 215, spells 105, monster
  abilities 408, job abilities 78, elements 8.

The skillchain that FORMS is read from the 0x028 add-effect (no table lookup), so it
works for ANY move; only the OPEN (step-1 property) and the prediction consult the
tables.

## Continuation prediction (which of YOUR moves extend the chain)
Reads the player's currently-**usable** move ids (Windower `get_abilities`),
reversed in `game_mem.cpp` (LuaCore `get_abilities` = `FUN_100732B0`): a 512-byte
"block 0xAC" fetched through the runtime resource manager at `*(g+4)` via two
virtual calls (`this`-on-stack `__stdcall`), then bitmaps:
- **usable weapon skills** = `block[0x04..0x24)` bit per id (ids 1..255),
- **usable job abilities** = `block[0x44..0xC4)` bit per id (ids 0..1023; pet ready
  moves included when a pet is out).
- **BLU set spells** (`read_blu_spells`) — a pure pointer chain (no client call):
  `M = *(g+0x60)`, gate `*(M+0x00) == 0x10` (BLU tag; 0x12/0x17 = PUP), then 20 u8
  slots @`M+0x05` (0 = empty), full spell id = `byte + 0x200`.
- **SCH** — the 8 elemental magics (`SCR_ELEM`) are always castable, added when mjob
  = SCH.

## Which mob's chain is shown (`<t>` → `<bt>` → nearby)
Fixes "a party member's skillchain doesn't always show" — your OWN weaponskill always
has the mob targeted, a party member's often does not. `draw_skillchains` picks the mob
in priority order:

1. **your current target `<t>`** — if it has a live window. **Always shown**, regardless of `scNearby`.
2. **your battle target `<bt>`** — the mob you're *engaged* with, held even after you
   drop the reticle (`target_t + 0x7C`, u32 ServerId; reversed 2026-07-10 via `//aio bt`,
   self-calibrating: stays set when `<t>` is cleared, clears to 0 on disengage → read in
   `read_target` → `GameState::battleTargetId`);
3. **the newest LIVE chain on any nearby mob** (`party().skillchain_newest_live()`) —
   covers a **healer / support / observer** who is *not engaged* and *not targeting* the mob.

**Steps 2 and 3 are BOTH gated by config `scNearby`** (default on). With `scNearby` **off**
the box is STRICT — only your `<t>` chain shows; nothing appears while you target nothing /
an ally, even if engaged (this is what fixed "the box appears even with the toggle off").
Steps 2–3 never fire while your reticle is on a *different* mob (`targetingMob`), so the box
can't linger on a mob you targeted away from. Config panel: "Skillchains → Display → Show
party/nearby chains".

## Widget & config
`Hud::draw_skillchains` (hud.cpp, WS-popup pattern) — dark box + gold border, lines
title / timer (Wait · Go! · Burst) / "Step: N > closing move" / "[prop] (elements)"
multi-colour. Center-anchored, `EDITBOX_SKILLCHAIN`. Config module "Skillchains"
(`sc_config.cpp`): Size + per-line Elements toggles + per-element Text
(Title / Timer / Step / Property / List). Lines `sc=` / `sc2=` / `scText%d=`.

## See also
- [Party cast bar — 0x028](cast-bar.md) — the shared ActionPacket bit layout (this module reuses those offsets).
- [Data channels](../architecture/data-channels.md) — the packet-hook channel + the `get_abilities` memory read for prediction.
- [Reverse-engineering recipe](../architecture/reverse-engineering-recipe.md) — how `get_abilities`/BLU were reversed.
