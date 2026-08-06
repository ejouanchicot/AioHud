---
title: Hate List — mobs aggro'd on the party
summary: How mobs aggro'd on you/your party are detected (0x028 enmity tracking, verified against the entity array at display), the shared entity offsets it reuses, and the 0x067/0x068 friendly-pet learning.
source: model/game_mem.cpp (read_entities_by_id), model/party_state_hate.cpp (record_hate, refresh_hate, on_pet_info/on_pet_status)
---
# Hate List — mobs aggro'd on the party

Shows the mobs that have aggro on you / your party, one HP fiole per mob, sorted by
HP ascending. Built with **no new reverse** — it reuses the existing entity-array
offsets. `refresh_hate()` runs once/frame in hud.cpp right AFTER `set_target_ctx`
(so `selfX_/selfZ_` + `curTarget_` are fresh).

## Shared entity offsets (`ENT_*_OFF`)
Both readers block-copy the entity pointer array (`entity_array()`, `0x900` slots)
once and reuse the same field offsets (also used by the minimap):

| field | offset |
|---|---|
| server id | `+0x78` (`ENT_ID_OFF`) |
| name (char[24]) | `+0x7C` |
| HP% | `+0xEC` (low byte) |
| status | `+0x170` |
| claim id | `+0x188` |
| spawn type | `+0x1D0` (low byte; **mob = 0x10**) |
| render flags | `ENT_RENDER_OFF` (`& 0x4000` = hidden/despawned) |

## Detection — ONE source, verified at display
`refresh_hate` builds rows from the sticky enmity tracker only, then verifies each
tracked id against the entity array before drawing it.

> **Historical note.** There used to be a second, PRIMARY source: a claim scan
> (`read_party_aggro_mobs`) that walked the entity array for mobs whose claim id was
> in the friendly set. It was **removed on 2026-08-06**: `refresh_hate` had already
> stopped calling it (the tracker became the sole membership source), so it was 28
> lines of dead code carrying a third private copy of the entity offsets — a third
> place to fix when an offset drifts after a client patch. The insight it was built
> on still holds and is why pet-tanked mobs work: a pet / avatar / jug / automaton
> claims for its **OWNER**, a roster id. That now arrives through the 0x028 instead.

1. **0x028 enmity tracking** (`on_action` → `hate_[24] {mob, pc,
   lastMs}` via `record_hate`): catches UNCLAIMED aggro on a party member/pet
   (incoming adds before first damage). Loops ALL targets (AoE / cleave). Friendly
   = self / roster PC / a tracked friendly pet (`is_party_or_pet`); enemy candidate
   = **any id ≥ 0x01000000** that isn't friendly — do NOT filter mob-ness by id
   range (`(id & 0xFFF) ≤ 0x700` WRONGLY DROPPED MOST REAL MOBS, the "only 1 mob
   shows" bug). `refresh_hate` verifies spawnType 0x10 in memory at display.
   Entries are pruned only once **stale (> 3 s)** — never dropped by a one-frame
   HP%==0 misread (a second pop/depop cause).

Row flags: `red` = `claimId != 0 && claimId ∉ friendly` (contested/enemy claim);
`target` = `id == curTarget_`; PC name resolved from `claimId` (or the tracked pc)
against the roster (self is a roster slot). Cleared on zone (0x00B → `hate_clear`).

## Friendly pets — 0x067 / 0x068 (critical for pet-tank fights)
When a pet tanks, the linked mobs SWING AT THE PET (target = pet id ≥ 0x01000000),
which the roster check alone can't tell from a mob → those mobs were invisible.
`petId_[16]` / `petOwner_[16]` learn friendly pets from packets (a pet is
registered only if its owner `is_party_or_pet`; cleared on zone):

- **0x067 Pet Info** (`on_pet_info`): Pet ID @**0x08** (u32), Owner **Index** @**0x0C**
  (u16) → `entity_id_by_index` → owner id.
- **0x068 Pet Status** (`on_pet_status`): Owner ID @**0x08** (u32), Pet **Index**
  @**0x0C** (u16), **Target ID @**0x14** (u32)** = the mob the pet fights →
  `record_hate` directly.

`pet_owner(id)` maps a pet → its owner so the Target column shows the OWNER, not the
pet.

## Entity readers (game_mem.cpp)
- `read_entities_by_id(ids, n, EntityVitals*)` — resolve a set of ids → live vitals
  in one block-copy (used to re-check the 0x028-tracked ids at display).
- `entity_id_by_index(index)` — one indexed read of `entity_array[index] + 0x78`
  (used to resolve the 0x067/0x068 pet/owner indices).

## Widget & config
`Hud::draw_hate_list` (hud.cpp) — per row `[dist]  <fiole: name .. HP%>  >> PCname`;
`hl_hp_color` = the addon's own green→yellow→red ramp (distinct from party/target
gauge colour). Center-anchored (`hlX` centre, `hlY` top), `EDITBOX_HATE`. Config
module "Hate List" (`hl_config.cpp`): Display (Show / Size / Max mobs / Distance
column / Target column) + per-element Text (Distance / Name / HP% / Target). Lines
`hl=` / `hlText%d=`.

## Known gap
Pet-only tanking edge cases can still be missed if neither a claim nor a 0x028/0x068
pairing is seen.

## See also
- [Party cast bar — 0x028](../party/cast-bar.md) — the ActionPacket bit layout the enmity supplement reads.
- [Map system (minimap)](../world/map-system.md) — the sibling reader of the same entity array (`read_map_entities`).
- [Target & sub-target struct](target-substruct.md) — `curTarget_` (the framed row).
- [Data channels](../../architecture/data-channels.md) — memory scan + packet, no last-incoming cache.
