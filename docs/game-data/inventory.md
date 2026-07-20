---
title: Inventory (memory)
summary: The item container at *(G+0x50) holds 18 bags x 81 entries (stride 0xCA8, entry 0x28), plus three u8[18] metadata arrays (max/count/enabled) at +0x19500/+0x19520/+0x19540. Bag table, addressing, and how it was confirmed.
---
# Inventory — read from memory

**Verdict: located and validated (2026-07-17).** Counting "how many of item id X do I own, across all
bags?" needs no packets — the whole container is resident and can be snapshotted in one block copy.

## Pointer chain
```
G          = *(LuaCore.dll + 0x1C8400)                  // data root  (aio::data_root())
items_root = *(G + 0x50)                                // aio::items_root()
entry      = items_root + (slot + bag*0x51) * 0x28      // == items_root + bag*0xCA8 + slot*0x28
    id     = u16 @ entry+0x00        // 0 = empty slot ; 0xFFFF = the reserved entry-0 header
    count  = u32 @ entry+0x04        // 1..99 (FFXI stack cap)
```
`bag` = 0..17, `slot` = **1..80**. Each bag holds **81** entries (`0x51 * 0x28 = 0xCA8`, the stride
[player-equipment](player-equipment.md) already used): **entry 0 of EVERY bag is a reserved header**
carrying id `0xFFFF` — in bag 0 its `count` is the player's **gil** (`read_player_gil()`).

`items_root` is the immediate neighbour of `key_items_base` (`*(G+0x4C)`): live,
`ki_base + 0x2000 == items_root` exactly (see [key-items](key-items.md)).

## Bag table
Names come from the `max_*` / `count_*` / `enabled_*` fields `FUN_100935c0` publishes, in index order.

| # | bag | # | bag | # | bag |
|---|---|---|---|---|---|
| 0 | inventory | 6 | sack | 12 | wardrobe4 |
| 1 | safe | 7 | case | 13 | wardrobe5 |
| 2 | storage | 8 | wardrobe | 14 | wardrobe6 |
| 3 | temporary | 9 | safe2 | 15 | wardrobe7 |
| 4 | locker | 10 | wardrobe2 | 16 | wardrobe8 |
| 5 | satchel | 11 | wardrobe3 | 17 | recycle |

`gil` (`items_root+0x04`), `equipment` (`*(G+0x54)`/`*(G+0x58)`) and `treasure` (`*(G+0x5C)`) are
*separate* views in `get_items()`, **not** bag indices.

## Per-bag metadata — three parallel u8[18] arrays
There IS a per-bag size/occupancy record, so no need to guess from a scan:

| array | offset (from `items_root`) | meaning |
|---|---|---|
| `max`     | `+0x19500 + bag` | capacity: 80 for a real bag, **0** temporary, **10** recycle |
| `count`   | `+0x19520 + bag` | occupied slots right now |
| `enabled` | `+0x19540 + bag` | bag reachable *right now* |

**`enabled` is not a validity gate for reading.** Live, safe/storage/locker/safe2 all read `enabled=0`
(character away from a moogle) while their **contents were fully resident and their `count` matched a
raw scan exactly**. So `count_item()` counts all 18 bags; `enabled` only says whether the game would
currently *let you* move things there. (Whether an EmpyPop pop item in `storage` should count toward
"usable now" is a UI decision, not a reader one.)

Occupied slots are **not** packed contiguously, so `count` can't be used as a loop bound — only as an
early-out (`count == 0` -> skip the bag's 80 slots).

## How it was found
1. **Ghidra (authoritative — this IS the loop Windower runs).** `windower.ffxi.get_items()` =
   `FUN_10074690`. Its no-argument path calls `FUN_100935c0(L, *(DAT_101c8400 + 0x50))`, which dumps
   bags `0..0x11` via `FUN_10093360(L, bag)`:
   ```c
   uVar10 = 1;
   do {                                              // slot = 1 .. 0x50  (entry 0 skipped)
     FUN_10093200(param_1, (ushort *)(*(int *)(DAT_101c8400 + 0x50) + (uVar10 + param_2 * 0x51) * 0x28));
     lua_rawseti((int)param_1, -2, uVar10);
     uVar10 = uVar10 + 1;
   } while (uVar10 < 0x51);
   bVar1 = *(byte *)(*(int *)(DAT_101c8400 + 0x50) + 0x19500 + param_2);   // "max"
   bVar1 = *(byte *)(*(int *)(DAT_101c8400 + 0x50) + 0x19520 + param_2);   // "count"
   cVar2 = *(char *)(*(int *)(DAT_101c8400 + 0x50) + 0x19540 + param_2);   // "enabled"
   ```
   `FUN_100935c0` then emits `max_inventory..max_recycle` (`+0x19500..+0x19511`),
   `count_*` (`+0x19520..`), `enabled_*` (`+0x19540..`) — which is what names the 18 bags. The string
   dispatch in `FUN_10074690` independently maps `"inventory" -> 0` … `"recycle" -> 0x11`, and its
   indexed path confirms the same arithmetic: `(int)pcVar5 * 0xca8 + iVar8 + (int)pcVar9 * 0x28`.
   These bounds (18 bags, slots 1..80) are Windower's own -> as safe to read as `get_items()`.
   ```
   analyzeHeadless re/ghidra_proj aiohud_re -process LuaCore.dll -noanalysis \
       -scriptPath scripts -postScript DecompOne.java 10093360     # and 100935c0 / 10074690
   ```
2. **Live validation, out-of-process** (`ReadProcessMemory` on `pol.exe`, 32-bit PowerShell — the
   deployed DLL was file-locked by the running game). Walked the chain, one 0x19552-byte read from
   `items_root` (proving the bags + metadata are one contiguous readable region), then:
   - **The client's own `count` byte matched an independent slot scan for all 18/18 bags** — 21, 27,
     79, 0, 20, 41, 64, 63, 80, 80, 45, 36, 50, 64, 72, 66, 75, 0. Two unrelated sources agreeing
     across 18 bags: a wrong stride would desynchronise at the first bag.
   - **883 occupied slots -> 707 distinct ids -> 707/707 resolve via `aio::item_name()`, zero
     garbage** (the *positive* set — see the trap below). Names were coherent for the character
     (`Beitetsu x525`, `Heavy Metal x165`, `Thr. Tomahawk x218`, `Imperial Standard x1` …).
   - **Every per-slot count in 1..99, max exactly 99** (FFXI's stack cap), none `<1`, none `>99`.
   - Entry 0 of **all 18** bags read id `0xFFFF`; bag 0's held gil = 574,903,219 (matches
     `read_player_gil()`'s known `items_root+0x04`).
   - `ki_base = 0x05EF603C`, `items_root = 0x05EF803C` -> the `0x2000` KI extent reconfirmed.

> **Trap (the recipe's KEY LESSON).** "It reads 0" proves nothing — a *wrong* pointer returns zeros
> too. Never validate this on an item the player lacks. The chain is confirmed by the **positive**
> set: 707 owned ids all resolving to real item names, plus the 18/18 count agreement.

## Reader (model)
`aio::refresh_items()` takes ONE SEH-guarded block copy of the whole container (`0x19552` bytes) into a
static snapshot; `count_item(id)` / `owns_item(id)` / `count_items(ids,n,out)` / `item_slot()` /
`item_bag_info()` then answer from that snapshot with no further game-memory reads. This is deliberate
(rule 5/6): a `safe_read` per slot would be ~1440 SEH-guarded reads per poll. A poller must call
`refresh_items()` once per cycle; a bad pointer degrades to a no-op and every count reads 0.
Offsets live only in `game_mem.cpp` (rule 7).

## Probe (kept in-tree, off by default)
`//aio inv` — dumps every bag (max / count / enabled / scanned, flagging any scan-vs-`count`
mismatch), every owned id+count, and the two falsification checks (ids that fail `item_name()`,
counts outside 1..99). `//aio inv <id>` totals one id across all bags.
Lives in `src/plugin/aiohud_probes.cpp`. Use it to re-locate the chain after a client/Windower patch:
re-decompile `get_items` and compare.

**Status:** chain confirmed and the `//aio inv` probe compiles, but the *in-game* probe run is still
outstanding — validation above was done out-of-process (same chain, same bounds, same arithmetic).

## See also
- [Player equipment](player-equipment.md) — the `equipment` view over the same entries (`0xCA8`/`0x28`)
- [Key items](key-items.md) — `*(G+0x4C)`, the container's neighbour
- [Reverse-engineering recipe](../architecture/reverse-engineering-recipe.md)
