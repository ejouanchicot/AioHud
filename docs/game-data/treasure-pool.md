---
title: Treasure Pool — the lottery pool packets
summary: The 0x0D2 item-added and 0x0D3 lot-info packet offsets that drive the Treasure Pool box, the 5-min expiry rule, and the 23.5k item-name table.
source: model/party_state.cpp (on_treasure_add/on_treasure_lot/treasure_clear), plugin/aiohud.cpp (feed_packet 0x0D2/0x0D3/0x00B)
---
# Treasure Pool — the lottery pool packets

Shows the shared drop pool (up to 10 slots), one row per item, with its lot info
and a countdown. Built **entirely from packets** — no memory reverse. State =
`TreasureItem treasure_[10]` in `PartyState`, indexed directly by the packet Index.
Ported from Kenshi's TreasurePool.

## 0x0D2 — item added / removed (`on_treasure_add`)
| field | offset | type |
|---|---|---|
| Item id | 0x10 | u16 |
| pool Index | 0x14 | u8 (slot 0..9) |
| Timestamp (unix) | 0x18 | u32 |

- Item `0xFFFF` = **"no change" marker** → ignore.
- Item `0` = **slot cleared** → empty that slot.
- If the slot already holds the same `(itemId, timestamp)` it's kept (so its lot
  info survives a resend).
- **Expiry** (`expireUnix`): the lottery window is `Timestamp + 300` (5 min). A
  *fresh* drop (`natural ≥ now` and within 300 s) uses that real window; an item
  that's already old at the time we first see it gets a fresh `now + 300`.

## 0x0D3 — lot info / won (`on_treasure_lot`)
| field | offset | type |
|---|---|---|
| Highest Lot | 0x0E | u16 |
| pool Index | 0x14 | u8 |
| Drop | 0x15 | u8 |
| Highest Lotter Name | 0x16 | char[16] |

- **Drop != 0** → the item left the pool (won or floored) → clear that slot.
- Drop == 0 → just update `lot` (@0x0E) + `lotter` (@0x16, NUL/16-capped) for the
  existing item (skipped if the slot is empty).

## 0x00B — zone → `treasure_clear()`
On zone change the whole pool is cleared (routed alongside the
[Hate List](hate-list.md) clear).

## Item names
`scripts/gen_itemnames.py` → `src/model/itemnames_gen.h`: **23,503** id→English
names from Windower `res/items.lua`, sorted, `item_name(id)` binary search (~699 KB
header).

> GOTCHA that broke the first build: multi-byte UTF-8 in names + `\xNN` escapes — a
> `\xNN` escape greedily eats a following hex digit (`"\x6a"+"1"` → `\x6a1` = out of
> range). Fix: escape on the UTF-8 BYTES and follow every `\xNN` with `""`
> (string-break).

## Widget & config
`Hud::draw_treasure_pool` (hud.cpp, WS-popup pattern — not a factory widget): a
coffer icon (`assets/icon_chest.raw` 128², textured quad) + aligned columns
"idx name MM:SS lotter: lot", timer coloured (green > 3 min / orange / red < 1 min),
expired rows dropped live. Center-anchored, `EDITBOX_TREASURE`. Config module
"Treasure Pool" (`tp_config.cpp`): Display (Show / Size / Max items / Coffer icon)
+ per-element Text (Index / Name / Timer / Lotter). Lines `tp=` / `tpText%d=`.

## See also
- [Data channels](../architecture/data-channels.md) — the packet-hook channel this module is a worked example of.
- [Hate List](hate-list.md) — cleared on the same 0x00B zone packet.
- [Party cast bar — 0x028](cast-bar.md) — the sibling packet-parsing module.
