---
title: Encumbrance Flags — the 0x01B Job-Info Packet
summary: The u32 'Encumbrance Flags' bitfield at 0x60 of packet 0x01B; bit sid = equip slot sid is locked, indexed directly by the equipment viewer.
source: Windower addons/libs/packets/fields.lua (0x01B field list)
---
# Encumbrance flags — the `0x01B` packet

AioHUD parses inbound packet id `0x01B` ("Job Info") to cache which equip slots are
**locked** (encumbered). The client resends `0x01B` on **job change / zone / encumbrance
change**, so the cache stays current with no polling. Same packet-fed idiom as `target_th`.

- Feed: `feed_packet()` in `src/plugin/aiohud.cpp` hands every packet to `aio::model_feed_packet()`
  (`src/model/party_state.cpp`, the SEH-guarded model router since 2026-09-13), which dispatches
  `id == 0x01B -> party().on_01b(b)`.
- Parse: `PartyState::on_01b` (`src/model/party_state.cpp`) floors on `pkt_short(p, 0x64)` (the highest byte
  read is `0x63`), then does `encumber_ = pkt_u32(p, 0x60)`. It then hands the same packet to
  `cs_read_01b` (`model/charsheet.h`) for the job tables — on its **own** floor, so a short `0x01B` still
  delivers the encumbrance (see [Character sheet](char-sheet.md)).
- Read: `party().encumbrance()` (u32). Consumers: the Equipment Viewer (`ui/player.cpp`, no cross in the
  demo/fake preview) and the redrawn native Equipment menu grid (`ui/hud_actmenu.cpp`, same red cross).

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

The job tables of the same packet ARE parsed, by the character sheet (`cs_read_01b`, `model/charsheet.h`):
unlock flags `0x0C` (u32), job levels `0x49` (22 B), mastered flags `0x68` (22 **bits**), master levels `0x6D`
(22 B) — documented in [Character sheet](char-sheet.md). The same `0x01B` payload also carries, for reference:

| field | offset | type |
|---|---|---|
| Maximum HP | `0x3C` | u32 |
| Maximum MP | `0x40` | u32 |
| Current Monster Level | `0x5F` | u8 |
| Mastery Rank | `0x66` | u8 |

AioHUD does not consume these four yet — noted only as map for a future job-info read.

## See also
- [Character sheet](char-sheet.md) — the job levels / mastery read from this same packet.
- [Player equipment](player-equipment.md) — the 16 equipped slots this bitfield locks.
- [Gear icons (equipment viewer)](gear-icons.md) — the viewer that draws the red cross.
