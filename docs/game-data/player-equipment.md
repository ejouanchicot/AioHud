---
title: Player Equipment (16 equipped items)
summary: Pointer chain for the 16 currently-equipped item ids, reversed from LuaCore's windower.ffxi.get_items('equipment') binding. Same items_root as the gil chain.
source: reverse-engineering-recipe.md §5a (Ghidra LuaCore decompile)
---
# Player equipment — read from memory

The 16 equipped slots do NOT store item ids directly. The client keeps, per equip slot,
an `{inventory_index, bag_id}` pair; the item id is resolved by indexing the item container
(same `items_root = *(g+0x50)` as gil). Reversed 2026-07-05 by decompiling LuaCore's
`get_items('equipment')` binding (`FUN_10074690` -> equipment builder `FUN_10094410`).

## Pointer chain (this install, `LuaCore.dll`)
```
G          = *(LuaCore.dll + 0x1C8400)   // data root (our `g`)
items_root = *(G + 0x50)                 // item-container root (gil @+0x04)
idx_arr    = *(G + 0x54)                 // u8[16]  : inventory index per equip slot
bag_arr    = *(G + 0x58)                 // s32[16] : bag/container id per equip slot
```
Both `idx_arr` and `bag_arr` are indexed **directly by the packet equip-slot id** S (0..15):
```
index(S) = *(u8 )(idx_arr + S)           // slot within the bag ; 0 = EMPTY (nothing equipped)
bag(S)   = *(s32)(bag_arr + S*4)         // which container (0 inventory, 8 wardrobe, 10 wardrobe2, ...)
```
Then resolve the item struct in the container (the confirmed numeric `get_items(bag,slot)` form,
`bag*0xCA8 + items_root + slot*0x28`) and read its fields:
```
item(S)  = items_root + bag(S)*0xCA8 + index(S)*0x28
id (S)   = *(u16)(item(S) + 0x00)        // item id   (0xFFFF = the gil pseudo-item)
count(S) = *(u32)(item(S) + 0x04)        // stack count (ammo slot 3 needs this)
```
**Empty slot:** `index(S) == 0` -> nothing equipped. Do NOT resolve it: bag/index 0 lands on
container slot 0 (inventory bag-0 slot-0 is the gil pseudo-item, id 0xFFFF), which is not a real
equip. Gate on `index != 0` before reading the id.

## Equip-slot id -> name (matches the addon equip.lua / the 0x050 equip packet)
```
0 main   1 sub    2 range  3 ammo   4 head   5 body   6 hands  7 legs
8 feet   9 neck  10 waist 11 left_ear 12 right_ear 13 left_ring 14 right_ring 15 back
```

## Item struct (0x28 bytes) — from `FUN_10093200` (the numeric-form item pusher)
```
+0x00  u16   id
+0x02  u8    slot          (its own inventory index)
+0x04  u32   count         (stack count == gil for bag0/slot0)
+0x08  u32   bazaar        (price)
+0x0C  u8    status        (0 none, 5 equipped, ...)
+0x0D  char[0x18]  extdata (24 bytes)
```

## How it was found (decomp, authoritative)
`FUN_10074690` (`get_items`) dispatches on the string arg. The `"equipment"` branch (the string
renders as a literal in the decomp — no byte dump needed, unlike `"gil"`) calls:
```c
FUN_10094410(param_1, *(byte **)(DAT_101c8400 + 0x54));      // DAT_101c8400 = g
```
`FUN_10094410` builds the Lua table. It reads two arrays:
- `param_2 = *(g+0x54)` (a `byte*`): `FUN_10009cd0(L, "<name>", param_2 + K)` sets field `<name>`
  from the **single byte** at `param_2+K` (`bVar1 = *param_3`). The K values map name->slot exactly:
  main=+0, sub=+1, range=+2, ammo=+3, head=+4, body=+5, hands=+6, legs=+7, feet=+8, neck=+9,
  waist=+0xa, left_ear=+0xb, right_ear=+0xc, left_ring=+0xd, right_ring=+0xe, back=+0xf.
- `piVar2 = *(g+0x58)` (an `int*`): `FUN_10009890(L, "<name>_bag", &piVar2[K])` sets the `_bag`
  fields from the **int** `piVar2[K]`, K running the SAME 0..15 order (main_bag=[0], sub_bag=[1],
  range_bag=[2], ammo_bag=[3], ... back_bag=[0xf]).

The item-struct field offsets come from `FUN_10093200(L, ushort* item)` (the pusher the numeric
`get_items(bag,slot)` form calls): id via `FUN_10009c10(L,"id",item)` reads `*item` (u16 @+0x00);
count via `FUN_1000a190(L,"count",item+2)` reads an int @+0x04. Container stride 0xCA8 / slot
stride 0x28 confirmed in the same function: `FUN_10093200(L, slot*0xca8 + *(g+0x50) + idx*0x28)`.

## Type / semantics
- `id` u16 (0xFFFF = gil pseudo-item), `count` u32.
- Empty equip slot: `index == 0`. Container not mapped while zoning: `*(g+0x54/0x58/0x50)` is
  0/invalid -> guarded reads no-op. Validate each pointer with `valid_ptr`.
- `bag` values seen: 0 inventory, 8 wardrobe, 10 wardrobe2, 11 wardrobe3 ... (the bag id list is
  the same dispatch table in `get_items`: safe=1, storage=2, temporary=3, locker=4, satchel=5,
  sack=6, `case=7`, wardrobe=8, safe2=9, wardrobe2=0xa, wardrobe3=0xb ... wardrobe8=0x10, etc.).

## Confidence
High — read the actual decompiled Windower binding chain (`get_items` -> `FUN_10094410`
equipment builder, `FUN_10093200`/`FUN_10009c10` item fields, `FUN_10009cd0` byte index).
Live-probe pending (`//aio equip`, below) to eyeball the 16 ids against real gear.

## Verification probe (`//aio equip`)
Model it on `//aio gil`/`//aio jlvl` (debug::log + debug::hexdump). For each slot S 0..15,
log name / bag / index / id / count so the user can compare to real gear:
```
g          = aio::data_root();              // *(LuaCore+0x1C8400)
items_root = *(g + 0x50);                    // safe_read + valid_ptr
idx_arr    = *(g + 0x54);                     // u8[16]
bag_arr    = *(g + 0x58);                     // s32[16]
for S in 0..15:
    index = *(u8 )(idx_arr + S);
    bag   = *(s32)(bag_arr + S*4);
    if index == 0: log "slotname  EMPTY";  continue;
    item  = items_root + bag*0xCA8 + index*0x28;
    id    = *(u16)(item + 0x00);   count = *(u32)(item + 0x04);
    debug::log("EQ %-10s bag=%2d idx=%3d id=0x%04X cnt=%u", name[S], bag, index, id, count);
```
Spans to eyeball with debug::hexdump:
- `idx_arr + 0x00 .. +0x10`  (the 16 index bytes, slot order main..back)
- `bag_arr + 0x00 .. +0x40`  (the 16 bag ints)
- for a populated slot: `item + 0x00 .. +0x28` (id@+0x00, count@+0x04, status@+0x0C)

## See also
- [Player gil](player-gil.md) — same `items_root = *(g+0x50)`, container stride 0xCA8 / slot 0x28.
- [Encumbrance flags](encumbrance-flags.md) — `0x01B` bitfield locking these same 16 slots.
- [Gear icons](gear-icons.md) — the 32x32 icon per equipped item id.
- [Player Equipment Viewer](../design/player-equipment-viewer.md) — the 4x4 grid UI that renders these 16 ids (placement, config, edit).
- [Local player struct](player-struct.md)
- [Reverse-engineering recipe](../architecture/reverse-engineering-recipe.md)
