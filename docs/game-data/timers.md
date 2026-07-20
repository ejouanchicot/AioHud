---
title: Timers — self buff durations + ability/spell recasts
summary: The 0x063 order-9 self buff-timer packet (absolute FFXI ticks), the client recast tables (g+0x22C/0x230 abilities, g+0x234 spells), the buff-caster self-cast filter, the shared-recast_id name-collision disambiguation, and the curated addon icon set (why ROM/119/57.DAT can't supply menu icons).
source: model/party_state.cpp (on_set_update order 9 / on_action buff-caster / ffxi_now_tick), model/game_mem.cpp (read_recasts)
---
# Timers — self buff durations + ability/spell recasts

Two columns, both **exact server-sourced** — no estimation. The **Duration** column is your
own buff timers (server-sent absolute expiry); the **Recast** column is your job-ability and
spell cooldowns read from the client recast tables. Sorted soonest-first, coloured by urgency
(white → orange ≤30 s → flashing red ≤10 s), placed via `//aio edit`.

## Duration column — self buff timers (packet 0x063 order-9)

`PartyState::on_set_update` (party_state.cpp:756) dispatches 0x063 on **Order @0x04** (u16).
**Order 9** is a **full-refresh, self-only** buff-timer block (there is **NO owner field** — it
is always *your* buffs):

| field | offset | type | note |
|---|---|---|---|
| Order | 0x04 | u16 | == 9 selects this block |
| Buffs | 0x08 | u16[32] | status id per slot; `0`, `0xFF`, `0xFFFF` = empty |
| Expiry | 0x48 | u32[32] | absolute FFXI tick when the buff ends |

Each refresh clears `buffTimerN_` and repacks the non-empty slots into `buffTimers_[]`. This
covers **gear / merits / Composure / song duration — nothing is estimated** (party_state.h:168).

### The FFXI tick epoch (`ffxi_now_tick`, party_state.cpp:752)
Expiry is an absolute count of **1/60-second ticks**. Remaining seconds =
`(int)(expiry − ffxi_now_tick()) / 60` — the **signed** diff handles the 32-bit wrap.

```
EPOCH = 1009810800.0
ERA   = (4294967296.0 / 60.0) * 10.0        // 2^32/60 ticks per era, ×10 eras
now   = (unsigned)(((double)time(0) - EPOCH - ERA) * 60.0)
```

`draw_timers` drops any row with `rem <= 0` or `rem > 6*3600` (6 h sanity cap).

### Buff-source filter and PER-TIMER caster attribution (0x028)

`tmBuffSrc` has four values: mine only / mine + players / mine + trusts / **all** (the default).
The legacy `tmOthers` boolean it replaced is kept for config migration only and is read nowhere else.

Attribution is learned from the **0x028 action packet** in `PartyState::on_action`, walking targets
with the VARIABLE stride (see [`action-packet.md`](action-packet.md) — the fixed `150 + i*123` stride
is only correct for `target[0]`).

**It is keyed per TIMER, not per status.** A status id does not identify a spell: every march is
status 214, every minuet 198. The old `buffCaster_[status] = actor` map could therefore hold exactly
one caster per status, and last-writer-wins — so a trust singing over you overwrote the attribution of
your own live song, and under "mine + players" the filter then dropped YOUR row while the buff was
plainly still up. Measured 2026-07-20; see [`../audits/timers-audit-2026-07-20.md`](../audits/timers-audit-2026-07-20.md).

The mechanism now:

- **`selfCasts_[64]`** — a ring of `{status, spell, caster, tick, predExp}` recording **every** caster's
  buff casts that land on you. `predExp` is the expiry we expect the cast to produce: computed from the
  real duration for our own casts (Troubadour / Marcato / gear / JP), base duration for foreign ones.
- **`match_cast(status, expiry, timerIdx)`** pairs a 0x063 timer to its cast by predicted-expiry
  **ORDER**, not by absolute closeness and not by recency. Both of those were tried and measured wrong:
  a trust's song is the most RECENT cast but the SOONEST to expire, and our own duration maths is not
  accurate enough for an absolute match. Ordering is reliable — the longest-lasting cast produces the
  longest-lasting timer. Ties (two trusts singing in one server tick) break on the timer slot.
- **Asymmetric on doubt.** No match returns "unknown", never someone else's spell. A foreign cast whose
  timer outlives its prediction by minutes is rejected. This is deliberate: an unknown KEEPS the row,
  while a wrong trust attribution HIDES it under the filter. Never blame a trust when unsure.
- **`latch_co_expiry_casters()`** (on each 0x063 refresh) — a trust move can grant several statuses while
  the packet names only one (Monberaux's move gives Protect AND Shell, reports status 40 only). A status
  with no caster inherits from a co-expiring sibling, requiring **unanimity** among co-expiring statuses.
  Latched at refresh time because the anchor is temporary: a later Protectra V replaces the Protect and
  orphans the Shell.
- **Stale ids read as unknown.** Trusts get new entity ids on re-summon, so a stored caster that no longer
  resolves to a name is discarded and re-derived rather than rendered as a blank owner.

Consumers: `buff_caster_for(status, expiry, timerIdx)` for the filter, the sort band and the `(Owner)`
row tag; `self_buff_spell_ranked(...)` for the row name and the hide/focus keys. The filter verdict is a
single lambda (`srcKeeps`) shared by the row emit **and** the focus monitor — they disagreed before, so a
buff the column hid could still fire a red OUT alert.

## Recast column — ability & spell cooldowns (`read_recasts`, game_mem.cpp:675)

Snapshotted **once/frame** into `GameState.recasts[40]` (`RecastEntry {recastId, kind, sec}`,
gamestate.h:58). Two client tables off the LuaCore `g` root:

- **Job abilities** — timers `*(g+0x22C)` int32[32]; ids `*(g+0x230)` **stride 8**, byte0 =
  `recast_id`. A slot counts when `0 < t ≤ 60*7200` (else ready / empty / garbage). `kind = 0`.
- **Spells** — `*(g+0x234)` ushort[1024], **indexed directly by `recast_id`**, which for a spell
  **equals the spell's own id**. Same `0 < v ≤ 60*7200` gate. `kind = 1`.

Seconds are **ceil'd**: `sec = (v + 59) / 60`. (`spell_recast_sec(recast_id)`, game_mem.cpp:700,
does the single-entry version for the action-menu "Next".)

Each row's icon/name is joined by `recast_id`:
`ic = (kind==0) ? ja_recast_icon(rid) : spell_recast_icon(rid)` and
`nm = abil_name_by_recast(rid) / spell_name_by_recast(rid)`. A row with neither is skipped.

### Recast name-collision disambiguation (`abil_name_by_recast`, hud.cpp:1776)

Many job abilities **share one `recast_id`**, so a linear scan of `ABILS` for that id could pick the wrong
label. Two collision shapes:

- **(a) Cross-job SP2s** all sit on **rc 254** — Clarion Call / Brazen Rush / Astral Conduit / Asylum /
  Stymie / … (≈20 abilities). Only ONE is usable on any given job.
- **(b) Same-job families** share a timer: Blood Pact **rc 173/174**, Phantom Roll **193**, Steps **220**,
  Sambas **216**, Jigs **218**, Flourishes **221/222/226**, stratagems **231**, … Here MANY are usable at
  once, and the wanted label is the **family header** (the first table row).

Rule: read the **usable-JA bitmap** once/frame (`read_usable_ja_bits`, game_mem.cpp:374 — the get_abilities
**"block 0xAC"**, JA bits at **+0x44**, exactly **128 bytes** = 1024 ids). Then:

```
exactly ONE usable  -> that ability   (fixes the SP2 cross-job case (a))
otherwise           -> the FIRST table row   (keeps the family header (b) = old behaviour)
```

So the net change vs the old first-row behaviour is **only** the one-usable case. `recast_id == 0` is shared
(2-hours…) and skipped as hopelessly ambiguous.

## Generated tables (do NOT hand-edit — regen via `scripts/`)

| file | script | maps |
|---|---|---|
| `recast_icons_gen.h` | `gen_recast_icons.py` | `recast_id → icon index` (`JA_RC_ICON` / `SP_RC_ICON`) |
| `buffs_gen.h` | `gen_buff_names.py` | `status id → name` (res/buffs.lua) |
| `action_status_gen.h` | `gen_action_status.py` | `action id → status` (res spells + job_abilities `status=`) |

The recast **icon atlas** is `assets/action_icons.raw`, baked by `gen_action_icons.py` from the
addon's `assets/icons/` set. The Duration column reuses the **buff status-icon atlas** shared with
the Player / Party boxes.

## Note: `ROM/119/57.DAT` is a dead end for menu icons

The recast column's icons come from the addon's **curated `assets/icons/` set**, which only
contains buff/debuff-duration actions. Actions without a duration icon (Provoke, nukes, cures)
have **no atlas cell and fall back to the NAME** — this is by design, not a bug to "fix".

Do **not** re-attempt sourcing menu icons from the game's `ROM/119/57.DAT`: it is a **640-entry
STATUS-icon set** (BMP 32×32, 32bpp BGRA, bottom-up, stride 6144, pixels at +701), **NOT** indexed
by the res spell/ability `icon_id`, so it cannot supply the ability/spell menu icons. That path was
already reversed and is a confirmed dead end.

## See also
- [Buffs you cast on ALLIES](buffs-on-allies.md) — the optional `tmMine` rows beside these columns: estimated ally durations + the AoE self-mirror.
- [Geomancy duration (GEO Indi-)](geomancy-duration.md) — the computed self Indi- row + the 542-556/612 pulse noise filter this column applies.
- [PointWatch — XP/CP/ML + Merits](pointwatch.md) — shares the 0x063 Set-Update packet (orders 2 / 5 vs order 9 here).
- [Party cast bar — 0x028](cast-bar.md) — the ActionPacket bit layout the buff-caster filter reads (actor.param @bit 86, cats 4/6).
- [Party-member buffs — 0x076](member-buffs.md) — the buff status-icon atlas the Duration column reuses.
- [Skillchains](skillchains.md) — the same `on_action` cat-4/6 parse feeds the buff-caster records.
- [Config panels — the split config page](../architecture/config-panels.md) — the Timers settings panel (`tm_config.cpp`, section 11).
