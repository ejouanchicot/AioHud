---
title: Core offsets verified against Windower's own LuaCore bindings
summary: The party-member, entity, entity-array-bound and recast offsets AioHUD uses, each checked against the LuaCore function that implements the matching windower.ffxi binding. Two entity fields came back contradicted.
source: Ghidra headless on LuaCore.dll (base 0x10000000) + ffximain_dump.bin (base 0x05C60000), 2026-07-19
---
# Core offsets - verified against LuaCore

Method: `docs/architecture/ghidra-setup.md`. The registration function **`FUN_1007BEF0`** pushes every
`windower.ffxi.*` closure next to its name, so each binding pairs with the code that implements it.
`g` = `DAT_101C8400` = `*(LuaCore+0x1C8400)`.

## Framing: Windower's member[i] is `*(g+0x248) + i*0x7C` - AioHUD's is that MINUS 4

`get_party` = **`FUN_10070AE0`**:

```c
iStack_5c = *(int *)(DAT_101c8400 + 0x248);
uVar9 = 0;
do {  piStack_54 = (int *)(uVar9 * 0x7c + iStack_5c);        // stride 0x7C
      pppuStack_50 = (uint ***)(uVar9 % 6);                  // key = "p"/"a1"/"a2" + (i%6)
      FUN_10094e70(pvStack_58, (int)piStack_54);             // member -> lua table
} while (uVar9 < 0x12);                                      // 18 slots
```

`0..5` = party (`p0..p5`), `6..11` = `a10..a15`, `12..17` = `a20..a25` (strings at `0x10190058/5c/60`).
AioHUD anchors at `pp-4` and adds 4 to every offset, landing on the same bytes. Both framings agree.

## Party member (`FUN_10094E70`, the member-to-table serializer)

| Lua field | Windower off | AioHUD off | Type | Status |
|---|---|---|---|---|
| `name`  | `+0x06` | `+0x0A` | ASCII | CONFIRMED |
| `id`    | `+0x18` | `+0x1C` | u32 | CONFIRMED |
| (entity index) | `+0x1C` | `+0x20` | **u16** | CONFIRMED |
| `hp`    | `+0x24` | `+0x28` | u32 | CONFIRMED |
| `mp`    | `+0x28` | `+0x2C` | u32 | CONFIRMED |
| `tp`    | `+0x2C` | `+0x30` | u32 | CONFIRMED |
| `hpp`   | `+0x30` | `+0x34` | u8  | CONFIRMED |
| `mpp`   | `+0x31` | `+0x35` | u8  | CONFIRMED |
| `zone`  | `+0x32` | `+0x36` | u16 | CONFIRMED |
| `flags` | `+0x38` | `+0x3C` | u32 (`is_leader` = bit 2, `is_quartermaster` = bit 4) | CONFIRMED |
| slot valid | `+0x7A` | `+0x7E` | u8, gates the WHOLE slot | not read by AioHUD |
| jobs | `+0x6D..0x70` | `+0x71..0x74` | u8 | **not exposed by `get_party`** - still only the in-game eyeball |

The Lua key strings were read straight out of LuaCore (`0x10197138` = "id", `0x1019713c` = "hp", ...), so the
mapping is not inferred from field order.

The member-to-entity hop is in the same function, and it is the origin of the array bound:

```c
if ((*(int *)(DAT_101c8400 + 0x24) == 0) || (0x8fe < *(ushort *)(param_2 + 0x1c) - 1)) mob = 0;
else mob = *(int *)(*(int *)(DAT_101c8400 + 0x24) + (uint)*(ushort *)(param_2 + 0x1c) * 4);
```

Index valid iff `1 <= idx <= 0x8FF`, so the array is **0x900 pointers**. AioHUD's
`if (!idx || idx >= 0x900) return 0` is the same test. The zone guard above it also confirms `zone_id()`:
`*(short *)(*(g+0x40) + 2)`.

`get_party_info` (`FUN_10070D10`) reads the alliance-info struct at `**(g+0x248)` - a pointer in the first
dword of the block: `[0]` alliance leader, `[1]/[2]/[3]` party1/2/3 leader, bytes `+0x13/+0x14/+0x15`
party1/2/3 count. Matches `load_from_memory` exactly.

## Entity struct (`FUN_1008DB90`, the mob-to-table serializer)

| Lua field | Offset | Status |
|---|---|---|
| `name` | `+0x7C` | CONFIRMED |
| `x` / `y` / `z` | `+0x04` / `+0x0C` / `+0x08` | CONFIRMED (AioHUD's "X,Z" = Windower's x,y - the two horizontal axes) |
| `heading` / `facing` | `+0x18` | CONFIRMED |
| `index` | `+0x74` | CONFIRMED |
| `id` | `+0x78` | CONFIRMED |
| `is_npc` | `+0x7B` | unused by AioHUD |
| `movement_speed` | `+0x98` | CONFIRMED (`animation_speed` = `+0x9C`) |
| `hpp` | `+0xEC` | CONFIRMED |
| `race` | `+0xEF` | unused |
| `valid_target` | `!(*(u32 *)(e+0x120) & 0x4000) && dist < cap` | CONFIRMED - `+0x120 & 0x4000` is the hidden/ghost filter |
| `status` | **`+0x16C`** | contradicts AioHUD's `+0x170` - see below |
| `claim_id` | `+0x188` | CONFIRMED (settles the old `0x18C` mis-guess) |
| `spawn_type` | `+0x1D0` | CONFIRMED |
| `in_party` | **`*(u8 *)(e+0x1D0) >> 2 & 1`** | contradicts AioHUD's `+0x124 & 0x00800000` |
| `in_alliance` | `*(u8 *)(e+0x1D0) >> 3 & 1` | new |
| `charmed` | `*(u32 *)(e+0x12C) >> 13 & 1` | new |
| `target_index` / `pet_index` / `fellow_index` | `+0x1F8` / `+0x1FA` / `+0x2A0` | new (u16) |

**SpawnType is a BITFIELD, and Windower proves it.** `in_party` and `in_alliance` are bits 2 and 3 of the very
byte AioHUD compares with `==`. So a party PC really is `0x0D` (`0x01|0x04|0x08`) and a party trust `0x0E` -
the in-game observation recorded in `target-substruct.md` was right, and the `sp == 0x01` / `sp == 0x10`
equality tests in `game_mem.cpp` are wrong. Bits `0x01` = PC and `0x10` = Mob remain in-game observation only;
Windower does not name them.

**`status` is NOT settled.** Windower reads `+0x16C`; AioHUD reads `+0x170`, found by a clean live
idle-vs-engaged A/B (`//aio tent`, `target-substruct.md`). Both flip 0 to 1 on engage, which is exactly what an
adjacent Status / StatusServer pair would do - the A/B could never have separated them. Aligning on `+0x16C`
(what every Windower addon means by `mob.status`) needs one more probe logging BOTH dwords through an
engage / disengage / KO cycle.

> Re-checked 2026-07-19 by decompiling `FUN_10072040` again: the `*(int *)(x + 0x16c) == 1` test is
> confirmed present, but the decompiler reuses that temp across the whole Lua-stack body, so the base
> could NOT be tied to the player entity struct from the listing alone. Static analysis has now been
> tried twice and cannot settle this — **the live two-dword probe is the only remaining route.** Do not
> spend a third session decompiling it.

## Recast constants (`get_ability_recasts` `FUN_1006FF00`, `get_spell_recasts` `FUN_1006FE80`)

```c
// abilities: 32 slots
do {  id    = *(byte *)(*(int *)(g + 0x230) + i * 8);        // BYTE, stride 8
      timer = *(int  *)(*(int *)(g + 0x22c) + i * 4);        // SIGNED int32
      push(((double)timer + rounding) / _DAT_101a2e18);      // /60
} while (i < 0x20);
// spells: flat ushort[1024]
do { push(*(ushort *)(*(int *)(g + 0x234) + i * 2)); } while (i < 0x400);
```

CONFIRMED: **32** ability slots; the id is a **byte** at stride 8, so AioHUD's `id & 0xFF` against an
`unsigned short` API type is correct rather than a lucky mask; timers int32 at stride 4; spells
**ushort[1024]** indexed directly by recast id. AioHUD's extra `t > 60*7200` garbage gate has no counterpart
in Windower - a local invention, and a conservative one (it can only hide a recast longer than 2 h).

Bonus from the same function's slot-0 special case (`player+0xA0 == 0 && player+0x94 == 0x16`, i.e. Rune
Fencer with no sub): the player struct's **main job at `+0x94`** and **sub-job level at `+0xA0`** are confirmed.

## PointWatch RVAs (FFXiMain side, no LuaCore needed)

`FUN_05CF93D0` (RVA `0x993D0`) is the **packet 0x061** handler; it copies the body verbatim into the static
block at `0x485640`:

| Packet 0x061 body | Static addr | AioHUD |
|---|---|---|
| `+0x0C..0x0F` | `0x485640..43` | main job / main lvl / sub job / sub lvl |
| `+0x10` u16, `+0x12` u16 | **`0x485644`, `0x485646`** | `PW_FM_EXP` - CONFIRMED, despite having been derived by arithmetic (`0x48569C - 0x58`) and never scanned |
| `+0x64..0x67` dword | `0x485698..9B` | byte `0x485699` = `PW_FM_MLVL`; `0x48569A` is bit-tested with 1 next door (Master Breaker) |
| `+0x68` u32, `+0x6C` u32 | **`0x48569C`, `0x4856A0`** | `PW_FM_EXEMPLAR` cur/req - CONFIRMED as an adjacent u32 pair written from one packet |

`PW_FM_MERIT = 0x485826` is written by the **packet 0x063** dispatcher at `05CF91A0`
(`MOV CX,[EAX+4]; ADD ECX,-0x2; CMP ECX,0x8; JA ...; JMP [ECX*0x4 + 0x5CF930C]` - an order 2..10 jump table).
The order-2 arm copies body `+0x08` and `+0x0C` as two dwords to `0x485826` / `0x48582A`, so LP u16 `@+0`,
merit count `@+2` and max merit `@+4` map to body `+0x08` / `+0x0A` / `+0x0C`. CONFIRMED.

## Known gap left behind

`read_member` copies `0x7C` bytes from `base + i*0x7C` where `base = pp-4`, so it reads 4 bytes BEFORE the
party block and misses each member's `+0x78..0x7B` tail - including the `+0x7A` byte Windower uses to gate the
whole slot. Harmless today (the copy is SEH-guarded and every field read lands inside), but it is why AioHUD
cannot use Windower's own liveness test.

## Re-running this audit after a patch

```
analyzeHeadless <projdir> aiohud_re -process LuaCore.dll -noanalysis \
  -scriptPath scripts -postScript DecompForce.java 10070ae0 10094e70 1008db90 1006ff00 1006fe80
```

`DecompForce.java` (added for this audit) creates the function first - several `lua_pushcclosure` targets are
bare `LAB_` labels that `DecompOne.java` refuses. `RawDisasm.java` dumps an address range that Ghidra never
turned into a function (needed for the 0x063 jump-table arms).
