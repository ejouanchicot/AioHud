---
title: Skillchains — resonance detection from 0x028
summary: The 0x028 action-packet bit offsets that detect a skillchain OPEN (step-1) / CLOSE (step+1), the property/combo tables, and the usable-move continuation prediction.
source: model/party_state.cpp (act_skillchain, a stage of on_action), model/skillchain.{h,cpp}, model/skillchain_gen.h
---
# Skillchains — resonance detection from 0x028

> **Sondes citees ici.** Les commandes `//aio` nommees dans cette page ne sont pas toutes livrees.
> `bt` : **retiree(s) le 2026-09-14** — le code est garde dans `dev/archive/aiohud_probes_2026-09-14.cpp`, a remettre dans `aiohud_probes.cpp` pour refaire une capture.
> Ce qu'une release porte vraiment : `//aio help` (et `//aio doctor` en premier quand quelque chose ne va pas).

Detects an active skillchain window on the target and predicts which of your moves
continue it. **Pure 0x028 bit-reads** — no memory reverse for detection (the
continuation *prediction* reads usable-move bitmaps, below). Gated to a FRIENDLY actor
(`is_skillchain_actor`: you / a party or alliance member / a tracked pet / a party Trust) — otherwise a
passer-by's skillchain popped in the box. It is the `act_skillchain` stage of `PartyState::on_action`; when the
WS popup takes the packet it returns true and the later stages do not run.

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

> These offsets share the ActionPacket layout used by the [cast bar](../party/cast-bar.md)
> (target[0].param @bit 213 = target base 150 + 63). Only target[0] is read here; the per-target stride is
> VARIABLE (36 bits + per action 86 + 37 if add-effect + 1|35 spike), see
> [target-debuffs.md](../target/target-debuffs.md).

## OPEN vs CLOSE
For a finish whose `target[0].id` is a real mob:

- **CLOSE** — `getbits(271,1) && sc_is_skillchain_msg(getbits(299,10))`: the hit
  carries a skillchain added effect. `prop = sc_from_add_effect_anim(getbits(272,6))`
  is the **resulting property**; `sc_close(tid, aid, res, prop, delay)` advances the
  window to step+1 (delay from the move's `SkillRow`, default 3 s).
- **OPEN** — else a finish message at bit 230 (10b): a plain WS/spell finish opens
  a **step-1** resonance from the move's OWN skillchain property (`sk->prop` from
  `sc_skill_lookup(res, aid)`), via `sc_open(tid, aid, res, prop, delay)`. For **cat 3 (`SCR_WS`)
  the gate is the strict `sc_is_weaponskill_msg` (185 only)**: cat 3 shares its id space with damage job
  abilities (46 = Shield Bash AND Expiacion, 77 = Weapon Bash AND Ruinator, 45 Mug / Atonement, 228 Despoil /
  Final Paradise…), and Shield Bash (110) / Jump (317) opened windows under the colliding WS (reported
  2026-07-27). The other categories keep `sc_is_finish_msg`. Also OPENS: a SCH elemental spell (msg 2) while
  Immanence (470) is up on the caster (`has_immanence`).
- **Pet moves** (cat 11 from one of our pets, table delay 3): the Wait is `UiConfig::scReadyDelay`
  (`petMoveDelayMs_`, default 4.0 s, clamped 3–8 s) — measured with `//aio sclog` 2026-10-29.
- `//aio sclog` logs one `SCAPT` line per finish: the fields read, the window before, and the decision.

Message-id sets (`skillchain.h`):
```
sc_is_skillchain_msg : [288..301] ∪ [385..397] ∪ [767..770]   // CLOSE add-effect
sc_is_finish_msg     : {110, 185, 187, 317, 802}               // OPEN, cats 4/6/11/13-15
sc_is_weaponskill_msg: {185}                                   // OPEN, cat 3 (and the WS popup)
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
  `sc_skill_lookup(resource, id)`. **924 rows**: WS 215, spells 105, monster
  abilities 511, job abilities 85, elements 8 (+ 16 Aeonic weapon items, `SC_AEONIC_ITEMS`).

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
2. **your battle target `<bt>`** — the first mob claimed by you or your party, as the GAME
   resolves it → `GameState::battleTargetId`. `<bt>` is **not stored** in memory: `read_battle_target`
   (`game_mem.cpp`, rule in `model/battle_target.h`, reversed 2026-09-13 from FFXiMain's `<bt>` resolver)
   walks the entity array in index order for the first entity with an actor loaded (`+0xA0 != 0`), claimed
   (`+0x188`) by you or a party member (not the alliance), and not dead (`+0x170` not 2/3).
   *History:* it used to be `target_t + 0x7C` (taken 2026-07-10 via `//aio bt`); measured in combat on
   2026-09-13, `+0x7C` follows the RETICLE target (Windower `<bt>` = a claimed Sand Hare while `+0x7C` held
   the Goblin Mugger under the cursor), so it is no longer read;
3. **the newest LIVE chain on any nearby mob** (`party().skillchain_newest_live()`) —
   covers a **healer / support / observer** who is *not engaged* and *not targeting* the mob.

**Steps 2 and 3 are BOTH gated by config `scNearby`** (default on). With `scNearby` **off**
the box is STRICT — only your `<t>` chain shows; nothing appears while you target nothing /
an ally, even if engaged (this is what fixed "the box appears even with the toggle off").
Steps 2–3 never fire while your reticle is on a *different* mob (`targetingMob`), so the box
can't linger on a mob you targeted away from. Config panel: "Skillchains → Display → Show
party/nearby chains".

## Widget & config
`Hud::draw_skillchains` (`ui/hud_skillchains.cpp`, WS-popup pattern) — dark box + gold border, lines
title / timer (Wait · Go! · Burst) / "Step: N > closing move" / "[prop] (elements)"
multi-colour. Center-anchored, `EDITBOX_SKILLCHAIN`. Config module "Skillchains"
(`sc_config.cpp`): Size + per-line Elements toggles + per-element Text
(Title / Timer / Step / Property / List). Lines `sc=` / `sc2=` / `scText%d=`.

**Two styles** (`scStyle`, Display's first row after Show) :
- **Circle** (default, 2026-11-22) — the resonance SPHERE (`ui/sc_circle.h`, engine `ui/hud_scfx.cpp`, shared
  with the `//aio scfx` dev demo). A lone move = a neutral grey sphere ; a formed chain = its own animation
  (each property's identity : lava, electric shocks, air bursts, cut stone, water waves, freezing, collapse, light
  lances, Lv.2/3 combinations), the scene bent behind it (one 512×512 `CopyRects` of the render target into a
  `D3DPOOL_DEFAULT` texture, made at the first sight of a window, given back 5 s after its last use, forgotten on
  device loss ; 3 failed tries -> OFF, logged, drawn waves instead). Centre : state, seconds, TP + gauge ; round
  it the countdown arc and the burst-element icons ; above, the chain's name + level pips. The continuing moves
  are ONE column split on an axis : the move | its level, skillchain name, burst elements — under / above / left /
  right of the sphere. Animation clock = `now - Resonating::openMs` (when the closing move landed) ; a new
  `target ^ openMs` restarts it.
- **Classic** — the text box above.

Circle settings, line `sc3=style,listScale,listPos,listOff,anim,distort,particles,reach` + `sclbox=` (the list's
frame, a `BoxStyle`, default off = a faint wash). The `sc3=` / `sclbox=` parse is out-of-line (`parse_sc3_line`) :
the load's else-if chain is at MSVC's nesting limit (C1061). Panel : Display (Style, Sphere size, List size, Grow) ·
Frame (the list's) · Content (scope, List position, List distance) · Elements · **Animations** `[22]` (on/off,
Scene distortion, Particles, Effect reach) · Text (Property = the list's skillchain names, WS list = its moves).

## See also
- [Party cast bar — 0x028](../party/cast-bar.md) — the shared ActionPacket bit layout (this module reuses those offsets).
- [Data channels](../../architecture/data-channels.md) — the packet-hook channel + the `get_abilities` memory read for prediction.
- [Reverse-engineering recipe](../../reverse-engineering/recipe.md) — how `get_abilities`/BLU were reversed.
