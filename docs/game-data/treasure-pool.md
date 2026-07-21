---
title: Treasure Pool — the lottery pool packets + memory reconciliation
summary: The 0x0D2 item-added and 0x0D3 lot-info packet offsets that drive the Treasure Pool box, the 5-min expiry rule, the in-game treasure MEMORY struct used to reconcile away phantom slots, and the 23.5k item-name table.
source: model/party_state.cpp (on_treasure_add/on_treasure_lot/reconcile_treasure/treasure_clear), model/game_mem.cpp (read_treasure_pool), plugin/aiohud.cpp (feed_packet 0x0D2/0x0D3/0x00B), ui/hud.cpp (per-frame reconcile call)
---
# Treasure Pool — the lottery pool packets

Shows the shared drop pool (up to 10 slots), one row per item, with its lot info
and a countdown. **Packet-fed, memory-reconciled**: the live rows are built from the
0x0D2/0x0D3 packets (state = `TreasureItem treasure_[10]` in `PartyState`, indexed by
the packet Index), then each frame checked against the game's own treasure memory
(`read_treasure_pool`) so a phantom slot can't linger — see
[Memory reconciliation](#memory-reconciliation-the-box-with-no-pool-fix) below.
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

## Memory reconciliation — the "box with no pool" fix
The pool used to be **packet-only with no ground truth**, so a single missed or
misparsed 0x0D3 "Drop" clear left an item lingering until its ~5-min expiry — a
**~1-min phantom box** for an already-mature drop (reported by Gab). The structural
fix: read the struct the game's **own Treasure menu** renders and reconcile against it.

**The treasure memory view** — base = `*(g + 0x5C)` (the `treasure` view of `get_items`,
sibling of `items_root = *(g+0x50)`; `g = data_root()`). **10 slots, stride `0x30`.**
Reversed 2026-07-21 from LuaCore `get_items` (`FUN_10074690` → `FUN_100935c0` →
`FUN_10094270`) **and** confirmed live with `//aio tmem`:

| field | offset | type | notes |
|---|---|---|---|
| status | +0x00 | u8 | **!= 0 = slot in use** — the game's own per-slot gate (the reconcile predicate) |
| item id | +0x02 | u16 | matched the live dump (slot 0 = `0x039C`) |
| lot | +0x04 | u16 | highest lot 0..999 (valid when `lot_id != 0`) |
| lot_id | +0x08 | u32 | winner char id (0 = nobody lotted) — do **not** use as the occupancy test |
| lot_index | +0x0C | u32 | winner alliance index |
| lot_name | +0x10 | char[20] | winning-lotter name, NUL-term |
| dropper_id | +0x24 | u32 | dropping mob's entity id |
| timestamp | +0x28 | u32 | drop time (unix, same epoch as the 0x0D2 ts) |

> `status @+0x00` and `item_id @+0x02` are confirmed **live** (tmem dump). `lot`/`lot_name`
> are Ghidra-confirmed only — nobody had lotted at dump time (`lot_id` was 0 everywhere) —
> so lot/lotter still come from the **packets**. A future `//aio tmem` with a lotted item
> would confirm those and let the whole module move off packets if ever wanted.

**`read_treasure_pool(TreasureSlot[10])`** (`game_mem.cpp`) does ONE SEH-guarded
0x1E0-byte block copy of `*(g+0x5C)` then parses the snapshot (rule 5/6 — never a
`safe_read` per field). Returns **false** when the view is unmapped (zoning) so the
caller treats it as *unknown*, **not** empty (rule 10 — else a zone would wipe a live pool).

**`PartyState::reconcile_treasure()`** runs once per frame (`hud.cpp`, after the hate /
skillchain prunes). For each non-empty packet slot: kept if memory says
`occupied && item_id` match; otherwise pruned — **unless** it was added < 2 s ago
(a fresh-add grace, so a 1-frame memory lag behind the 0x0D2 can't wipe a real item).
A phantom therefore clears in **~2 s** instead of ~1 min, whatever the packet cause.
With `//aio tpool` armed each prune logs `TPOOL RECONCILE slot=… pruned`.

### Probes
- `//aio tpool` — trace the next 200 treasure packets (0x0D2/0x0D3) + the expiry math +
  zone-in/out markers + reconcile prunes to `aiohud_debug.log` (`TPOOL` lines).
- `//aio tmem` — one-shot hex dump of `*(g+0x5C)` (512 bytes, `TMEM` lines) to read the
  per-slot layout off a live pool. Both are outside `AIOHUD_PROBES` (like `geartrace`), so
  they work on a release build for a tester capture.

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
