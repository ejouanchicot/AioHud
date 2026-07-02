---
title: Game data — RE'd memory reads + packet hooks
summary: How AioHUD reads live party/target/cast/buff data via SEH-guarded pointer chains and FFXI packet hooks, snapshotting once per frame.
source: TECH_STACK.md §10
---
# Game data — RE'd memory reads + packet hooks

**Why.** The HUD needs live party/alliance vitals, target/sub-target, jobs, casts and buffs. Those
live in FFXI's process memory (and in packets). None of it is a public API.

**How we use it (`model/`).**
- **Pointer chains** from a known anchor (`g = *(LuaCore+0x1C8400)`, party base `pp-4`, member stride
  `0x7C`, 18 slots). Each field has one documented offset ([game data](../game-data/README.md)).
- **SEH everywhere we touch game memory:** `safe_read` (4 bytes) or one guarded block copy; a bad
  pointer degrades to a no-op, **never a crash**. Validate with `valid_ptr`.
- **Snapshot, don't poll-in-draw:** `poll_game_state()` reads each chain **once per frame** into
  `GameState`; widgets read `f.game->…`. The bulky roster lives in the `party()` singleton, likewise
  refreshed once per frame.
- **Packets top up** what memory lacks/lags: `0x0DD` (**Party Member Update** — name/jobs/zone/HP%),
  `0x0DF` (**Char Update** — HP/MP/TP), `0x028` cast bar, `0x076` others' buffs (self buffs from
  `player+0x1C`). FFXI packet header = **9-bit ID + 7-bit (length/4) + 16-bit sequence**. The Windower
  **Lua `packets` library** (`data.lua`) is a ready field-by-field map of every packet — use it as the
  spec when adding a handler.

**Best-practices.** Reverse a **LuaCore Lua-binding** (the C function behind `get_player` etc.) to get
the *authoritative* pointer chain; confirm the exact field with a **live differential probe** (capture
OFF vs ON, diff — the changing field is the one). Keep probes in the tree (off by default) for
re-locating after a client patch. ([reverse-engineering recipe](../architecture/reverse-engineering-recipe.md).)

**References.**
[Tracking down pointers with Ghidra (suXin)](https://suxin.space/notes/tracking-down-playstation-pointers-using-debuggers-ghidra/) ·
[Reverse engineering a GBA game (Starcube)](https://www.starcubelabs.com/reverse-engineering-gba/) ·
[RE x86 binaries with Ghidra (tarasyk)](https://tarasyk.ca/2019/11/24/reverse-eng-gta-sa.html) ·
[Structured Exception Handling (`__try/__except`, MS Learn)](https://learn.microsoft.com/en-us/cpp/cpp/structured-exception-handling-c-cpp) ·
*FFXI/FFXIV community RE (patterns transfer):* [Ashita memory functions](https://wiki.ashitaxi.com/doku.php?id=addons%3Afunctions%3Amemory) ·
[Ashita v4 features (pointer/offset .ini overrides)](https://docs.ashitaxi.com/features/) ·
[FFXIVClientStructs — fixed-offset struct approach (GitHub)](https://github.com/aers/FFXIVClientStructs) ·
[Reverse engineering for plugin devs (Dalamud)](https://dalamud.dev/plugin-development/reverse-engineering/) ·
*FFXI packets:* [Windower Lua packets library — `data.lua` (GitHub)](https://github.com/Windower/Lua/blob/dev/addons/libs/packets/data.lua) ·
[PacketViewer / PacketFlow (packet inspection)](https://www.ffxiah.com/forum/topic/56713/packetflow-for-ashita-and-windower/)

## See also
- [Windower 4 (IPlugin ABI)](windower-abi.md)
- [Tooling — Ghidra & Python](tooling.md)
- [Player struct](../game-data/player-struct.md)
- [Party packets](../game-data/party-packets.md)
- [Party array](../game-data/party-array.md)
- [Reverse-engineering recipe](../architecture/reverse-engineering-recipe.md)
