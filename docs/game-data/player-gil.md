---
title: Player Gil
summary: Pointer chain for the local player's gil amount, reversed from LuaCore's windower.ffxi.get_items('gil') binding.
source: reverse-engineering-recipe.md §5a (Ghidra LuaCore decompile)
---
# Player gil — read from memory

Gil lives in the item-container root, not the player struct. Reversed 2026-07-05 by
decompiling LuaCore's `get_items` binding (`FUN_10074690`, the fn behind
`windower.ffxi.get_items`).

Pointer chain (this install, `LuaCore.dll`):
```
G          = *(LuaCore.dll + 0x1C8400)   // data root (our `g`)
items_root = *(G + 0x50)                 // item-container root object
gil        = *(items_root + 0x04)        // u32, UNSIGNED
```

## How it was found (decomp, authoritative)
`FUN_10074690` (`get_items`) dispatches on the string arg. The 3-char string at
`DAT_1018fc2c` = `"gil\0"` (confirmed by a byte dump) takes the branch:
```c
FUN_100099e0(param_1, (int *)(*(int *)(DAT_101c8400 + 0x50) + 4));
```
- `DAT_101c8400` = `g`; `DAT_101c8400 + 0x50` = `g + 0x50`.
- `FUN_100099e0(L, p)` reads `iVar1 = *p` (a 32-bit int) and pushes it as a Lua number
  (type tag 3) with the classic uint32→double fixup (`+2^32` when the sign bit is set)
  → **gil is treated as an unsigned u32**.

The same `*(g + 0x50)` base is the whole item container: `get_items()` with no arg dumps
`*(g+0x50)`, and the numeric `(bag, slot)` form indexes `bag*0xCA8 + *(g+0x50) + slot*0x28`.
Gil is the **stack count of inventory (bag 0) slot 0** — the special "gil item": slot 0's
item struct has its id at `+0x00` and its count (`u32`) at `+0x04`, hence `items_root + 0x04`.

## Type / semantics
- Width: **u32, unsigned** (max in-game gil 999,999,999 fits well under 2^31).
- Empty/not-ready: `*(g + 0x50)` is 0 (or invalid) while zoning — a guarded read no-ops.
  Gate/validate `items_root` with `valid_ptr` before reading `+0x04`.

## Confidence
High — read the actual decompiled Windower binding and confirmed the `"gil"` dispatch
string by byte dump. **Live-probe pending** (`//aio gil`, below) to eyeball the u32 == the
player's real in-game gil before wiring the poller read.

## Verification probe (`//aio gil`)
Model it on `//aio jlvl`: dump the container root and the u32 so the user can compare.
```
g          = aio::data_root();                 // *(LuaCore+0x1C8400)
items_root = *(g + 0x50)                        // safe_read + valid_ptr
gil        = *(items_root + 0x04)   (u32)
debug::log("GIL: g=%08X items_root=%08X  gil=%u (0x%08X)", g, items_root, gil, gil);
debug::hexdump("items_root", items_root, 0x28);  // slot 0 : id@+0x00 (0xFFFF), count@+0x04 == gil
```
Span to eyeball: **`items_root + 0x00 .. +0x28`** (one 0x28-byte item slot). Confirm the u32
at `+0x04` equals the real gil, and note `+0x00` (expected `0xFFFF`, the gil pseudo-item id).

## See also
- [Local player struct](player-struct.md)
- [Reverse-engineering recipe](../architecture/reverse-engineering-recipe.md)
