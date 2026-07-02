---
title: Reverse-engineering recipe
summary: The two complementary techniques (Ghidra LuaCore binding decompile + live differential probe) used to find game memory offsets, with the party-window picker worked example.
source: ARCHITECTURE.md §5
---
# Reverse-engineering recipe (how we found the offsets)

Two complementary tools. Use the Ghidra route for *structure*, the live probe for *which field*.

### 5a. Reverse a LuaCore lua-binding (authoritative)
Windower exposes game data as Lua functions (`get_player`, `get_mob_by_target`, …). Reversing
the C function behind a binding tells you the exact pointer chain Windower itself uses.
1. The binding is registered as `lua_pushcclosure(L, FUN_xxxxxxxx, 0); lua_setfield(L,-2,"<name>")`.
   Find `FUN_xxxxxxxx` (the closure pushed right before the `setfield "<name>"`).
2. Decompile it headless:
   ```
   analyzeHeadless <proj> aiohud_re -process LuaCore.dll -noanalysis \
       -scriptPath scripts -postScript DecompOne.java <FUN addr-without-0x>
   ```
   (Ghidra 12, project `re/ghidra_proj/aiohud_re`, LuaCore base `0x10000000`.)
3. Read the field accesses off `DAT_101c8400` (= our `g`). Examples found this way:
   - `get_player` (`FUN_10072040`): self buffs loop over `u16[32]` at `player+0x1C` (0xFF=empty);
     jobs were NOT here (they're in the member array `+0x71/+0x73` — found via the live probe).
   - `get_mob_by_target` (`FUN_100750c0` → resolver `FUN_1008b7d0`): a string→target dispatch that
     indexes the entity array `*(g+0x24)` by a per-type index in the target structs.
> Caveat: the decompiler output is messy for string-heavy functions; use it to find the *struct*
> and *which g+offset*, then confirm the exact field with a live probe.

### 5b. Live differential probe (which field / confirm)
A `//aio` toggle in `aio_plugin_render6` that watches a memory window every few frames and logs
on change (to `aiohud_debug.log`, read it directly). The discipline that cracked hard cases:
- **Scan for a known id** (your server id is distinctive) across a struct → the offset that holds it.
- **Differential / transition**: capture state OFF vs ON (e.g. a menu cursor closed vs open) and
  diff — the field that changes is the one. KEY LESSON (the quartermaster cursor): a value that is
  `0` solo (an index of the lone member) is indistinguishable from zeroed memory — you need **≥2
  members** so the field takes a *non-zero, distinctive* value. Don't burn cycles probing a `0`.
- Resolve indices: `entity = *(*(g+0x24) + index*4)`, server id at `entity+0x00`.

Probes live in `plugin/aiohud.cpp` and are kept (off by default) for re-locating after a client
patch: `//aio tgt2`/`sub` (target struct), `pcur` (party-window picker), `pkt`/`menu`/`dump`/`grabmod`.

### 5c. Worked example — the party-window picker ([target/sub-target struct](../game-data/target-substruct.md))
Goal: frame the member the *Menu→Party→Distribution→Quartermaster* cursor is on. We ruled out
`target_t` and the LuaCore target list (both stay "nothing" → it's NOT a sub-target), then with
2 party members the `//aio pcur` dump showed the focused menu is named **`partywin`** with a
**1-based cursor index at `menu+0x4C`** → member = `rows[index-1]`. One field, covers
quartermaster / lottery / remove / leader.

## See also
- [Reading the live roster](roster.md)
- [The per-frame data flow](data-flow.md)
- [Target substruct](../game-data/target-substruct.md)
- [Action menu](../game-data/action-menu.md)
- [Debug and mapping](../reference/debug-and-mapping.md)
- [Traps](../game-data/traps.md)
