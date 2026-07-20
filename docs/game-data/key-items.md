---
title: Key Items (memory)
summary: The client keeps key items DECODED as a flat u8[8192] (one byte per key-item id) at *(G+0x4C) — not as the 0x055 packet's bitfield. Pointer chain, addressing, and how it was confirmed.
---
# Key items — read from memory

**Verdict: located and validated (2026-07-17).** The 0x055 packet is *not* needed to know the
player's key items — the client decodes it into a persistent flat array we can read directly.

## Pointer chain
```
G       = *(LuaCore.dll + 0x1C8400)     // data root  (aio::data_root())
ki_base = *(G + 0x4C)                   // u8[0x2000] : ONE BYTE per key-item id
has(id) = ki_base[id] != 0              // id in 0 .. 0x1FFF
```
`G+0x4C` is the immediate neighbour of `G+0x50` (`items_root`, see
[player-equipment](player-equipment.md)) — and in the live process
`ki_base + 0x2000 == items_root` **exactly**, which independently pins the array's size.

## Addressing — NOT a bitfield
This is the one thing worth internalising: **the packet's layout is not the memory layout.**

| | 0x055 packet | memory |
|---|---|---|
| unit | 1 **bit** per key item | 1 **byte** per key item |
| grouping | tables of 512 bits (`table = id/512`, `bit = id%512`), table id at `@0x84` | **flat**, no tables — index by raw id |
| lookup | `p[0x04 + bit/8] >> (bit%8) & 1` | `ki_base[id] != 0` |

So `table = id/512` (`ny_has_ki`, `party_state_zonetracker.cpp:96`) applies **only** to the packet.
In memory there is no table index at all. The client demultiplexes every 0x055 into this array.

Observed byte values are **only `0x00` and `0x01`** (histogram over all 8192 entries) — a bool array,
not packed bits. Treat it as `!= 0` rather than `== 1`.

Id space: the array spans ids `0..8191`; real ids in use today top out around **3366**
(ids ≥ 4096, i.e. "tables" 8-15, were entirely zero on a fully-progressed character).

## How it was found
1. **Ghidra (authoritative — this IS the chain Windower uses).** Windower's own
   `windower.ffxi.get_key_items()` binding: the string `get_key_items` (`@1019185c`) is registered by
   `FUN_1007bef0` via `lua_pushcclosure(L, FUN_10073b80, 0); lua_setfield(L,-2,"get_key_items")`.
   Decompiling `FUN_10073b80` gives both forms of the lookup:
   ```c
   iVar6 = *(int *)(DAT_101c8400 + 0x4c);        // DAT_101c8400 = G
   cVar1 = *(char *)(iVar6 + (int)fVar8);        // has(id) : one BYTE at base + id
   ...
   do { if (*(char *)(iVar7 + *(int *)(DAT_101c8400 + 0x4c)) != '\0') { /* push id */ }
        iVar7 = iVar7 + 1; } while (iVar7 < 0x2000);   // enumerate 0..0x1FFF
   ```
   The `< 0x2000` bound is Windower's own — reading that range is as safe as `get_key_items()`.
   ```
   analyzeHeadless re/ghidra_proj aiohud_re -process LuaCore.dll -noanalysis \
       -scriptPath scripts -postScript DecompOne.java 10073b80
   ```
2. **Live validation, out-of-process** (`ReadProcessMemory` on `pol.exe`, 32-bit PowerShell — used
   because the deployed DLL was file-locked by the running game). Walked the chain and dumped all
   8192 bytes against `res/key_items.lua`:
   - **869 owned ids — 869/869 exist in `key_items.lua`, zero garbage.** This is the decisive
     evidence: a wrong pointer would scatter hits across the ~2400 ids the resource does *not*
     define. Categories were coherent for the character (245 Permanent, 163 Abyssea, 109 Magical
     Maps, 87 Mog Garden, 56 Voidwatch, 17 Mounts, 17 Geas Fete …), and spot-reads were verifiable
     in game: `8 airship pass`, `138 chocobo license`, `3073-3107 ♪…companion` mounts,
     `559 Mega Moglification: Bonecraft`, Sortie shards/souls.
   - Byte-value histogram over 8192 entries: `0x00 -> 7323`, `0x01 -> 869`. Nothing else.
   - `ki_base = 0x05EF603C`, `items_root = 0x05EF803C` → adjacency confirms the `0x2000` extent.

> **Trap (the recipe's KEY LESSON, hit head-on here).** The first spot-checks — KI 797 (Assault
> armband) and the Abyssea pop ids 1470/1471/1482/1483/1484 — all read `0x00`. That proves
> *nothing*: on a character that lacks them, "all zero" is exactly what a **wrong pointer** also
> returns. The chain was only confirmed by the **positive** set (869 owned ids all resolving to real
> names). Never validate this array on key items the player lacks.

## Probe (kept in-tree, off by default)
`//aio ki` — dumps every owned id, the per-512 spread, the byte-value histogram, and spot-checks
797/1470/1471/1482/1483/1484. `//aio ki <id>` tests one id.
Lives in `src/plugin/aiohud_probes.cpp` (dev-only, `/DAIOHUD_PROBES`). Use it to re-locate `G+0x4C`
after a client/Windower patch: re-decompile `get_key_items` and compare.

**Status:** the chain is confirmed and the `//aio ki` probe compiles, but the *in-game* probe run is
still outstanding — validation above was done out-of-process (equivalent chain, same result).

## See also
- [Player equipment](player-equipment.md) — `items_root` = `*(G+0x50)`, the array's neighbour
- [Zone tracker](zone-tracker.md) — the 0x055 packet path (`ny_has_ki`), still used for live deltas
- [Reverse-engineering recipe](../architecture/reverse-engineering-recipe.md)
