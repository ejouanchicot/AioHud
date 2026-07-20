---
title: Party Cast Bar — the 0x028 Action Packet
summary: Bit-packed 0x028 action packet parse for party casting bars, the don't-clear-on-cat-4 gotcha, and the separate cast state array.
source: REFERENCE.md §9f
---
# Party cast bar — the `0x028` action packet  (WORKING, 2026-06-27)

Shows the spell a party member is casting (name + progress), driven by the **inbound `0x028`
action packet** (the same source every Lua addon uses via `windower.packets.parse_action`).
Native parse in `party_state.cpp on_action()`. Bit layout (LITTLE-ENDIAN bit packing ; offsets
in BITS from byte 0 of the packet, verified vs Ivaar's `actions.lua` gist):

```
actor id            @ bit 40  (byte 5), 32 bits   -> caster's server id (match to a party member)
category            @ bit 82,            4 bits    -> 8 = BEGIN casting (magic), 4 = finish
target[0].action[0].param @ bit 213,    17 bits    -> SPELL ID (on a category-8 begin)
```
`getbits(p, off, w)` = read 8 bytes LE from `off/8`, `>> (off&7)`, mask `w` bits.

- On **category 8**: look up `spell_info(id)` (name + base `cast_time`) and start a cast slot.
- **Do NOT clear on category 4.** On this client the cat-4 "finish" arrives *one packet later,
  instantly*, so clearing on it kills the bar before it ever shows. The bar instead lives on its
  own **duration** (`cast_time` from the spell table) + a short fade, and self-expires.

**Spell table:** `src/model/spells_gen.h` (957 spells, id→en+cast_ms) is AUTO-GENERATED from
Windower `res/spells.lua` by `scratchpad/gen_spells.py` — regenerate if the client adds spells.

⚠️ **Gotcha that cost an hour:** the cast state must NOT live in `PMember` — `hud.cpp` calls
`party().load_from_memory()` EVERY frame, which does `pm = PMember()` and would wipe a
packet-set field next frame. Cast state lives in a separate `PartyState::casts_[18]` array
(keyed by server id) that the per-frame roster refresh never touches. `cast_label(id,&pct,&alpha)`
reads it (with pop-in / depop fade) for the UI.

## Target debuffs use the same `0x028` (and `0x029`) — moved out

The Target module reuses this `on_action` parse to track debuffs **ON a mob** (detection by SPELL id,
"no effect" gating, sleep/crowd-control inference from the mob's own actions, `0x029` wear-off + duration
learning, and the 32-slot table). That is now its own topic — see
**[Target debuffs](target-debuffs.md)**.

## See also
- [Target debuffs — detection, wear-off, and mob-behaviour inference](target-debuffs.md)
- [Target module — the TargetBar widget & its config](../design/target-module.md)
- [Party member packets](party-packets.md)
- [Action-menu info box](action-menu.md)
- [Target & SUB-target struct](target-substruct.md)
