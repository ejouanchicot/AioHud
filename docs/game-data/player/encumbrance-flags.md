---
title: Encumbrance Flags — the 0x01B Job-Info Packet
summary: The u32 'Encumbrance Flags' bitfield at 0x60 of packet 0x01B; bit sid = equip slot sid is locked, indexed directly by the equipment viewer.
source: Windower addons/libs/packets/fields.lua (0x01B field list)
---
# Encumbrance flags — the `0x01B` packet

AioHUD parses inbound packet id `0x01B` ("Job Info") to cache which equip slots are
**locked** (encumbered). The client resends `0x01B` on **job change / zone / encumbrance
change**, so the cache stays current with no polling. Same packet-fed idiom as `target_th`.

- Feed: `feed_packet()` in `src/plugin/aiohud.cpp` dispatches
  `id == 0x01B -> aio::party().on_01b(b)`.
- Parse: `PartyState::on_01b` (`src/model/party_state.cpp`) does `encumber_ = rd32(p, 0x60)`.
- Read: `party().encumbrance()` (u32); the equipment viewer consumes it.

## The field

| field | offset | type | notes |
|---|---|---|---|
| **Encumbrance Flags** | `0x60` | u32 | bit `sid` set = equip slot `sid` is **LOCKED** |

Offset is from the packet **start** (byte 0 = the header), confirmed against Windower's
`addons/libs/packets/fields.lua` (the `0x01B` field list annotates *Encumbrance Flags* at
hex offset `60`).

## Bit -> equip slot

The bit order matches AioHUD's equip-slot ids **exactly** — the same `main=0 .. back=15`
order used by [player-equipment](player-equipment.md). So the equipment viewer indexes the
bitfield directly: `bit sid` set -> draw a red cross over grid cell `sid`.

```
LSB -> 0 main   1 sub     2 range      3 ammo    4 head   5 body   6 hands  7 legs
       8 feet   9 neck   10 waist     11 left_ear 12 right_ear 13 left_ring 14 right_ring 15 back  <- bit15
```

`fields.lua` lists these MSB-first *per byte*, which resolves to exactly this
`LSB=main .. bit15=back` order.

## Neighbouring fields (parsed elsewhere / not used yet)

The same `0x01B` payload also carries, for reference:

| field | offset | type |
|---|---|---|
| Maximum HP | `0x3C` | u32 |
| Maximum MP | `0x40` | u32 |
| Current Monster Level | `0x5F` | u8 |
| Mastery Rank | `0x66` | u8 |

AioHUD does not consume these yet — noted only as map for a future job-info read.

## See also
- [Player equipment](player-equipment.md) — the 16 equipped slots this bitfield locks.
- [Gear icons (equipment viewer)](gear-icons.md) — the viewer that draws the red cross.
