---
title: Target & Sub-Target Struct
summary: The Ashita target_t heap struct (main vs sub reticle) driving the gold/blue target bars, plus the party-window picker cursor.
source: REFERENCE.md §9e
---
# Target & SUB-target struct  (REVERSED & WORKING, 2026-06-27, retail)

This drives the gold "loupe" bar (main target `<t>`) **and** the ocean-blue bar (sub-target
`<st>`/`<stpc>`). Layout matches **Ashita's `target_t`** (`plugins/sdk/ffxi/target.h`):
two `targetentry_t` (40 bytes / `0x28` each) + a sub-active flag.

```
*(FFXiMain.dll + 0x57876C)        -> target_t base (HEAP ptr ; ASLR-shifted, resolve at runtime)
  target_t + 0x00  u32  Targets[0].Index       (entity index)
  target_t + 0x04  u32  Targets[0].ServerId    = ACTIVE RETICLE  (sub when <st> open, else main)
  target_t + 0x08  u32  Targets[0].EntityPointer
  target_t + 0x0C  u32  Targets[0].ActorPointer
  target_t + 0x28  u32  Targets[1].Index
  target_t + 0x2C  u32  Targets[1].ServerId    = LOCKED MAIN  (valid while a <st> cursor is up)
  target_t + 0x50  u32  flags ; bit 0x00010000 = sub-target CURSOR OPEN (set only while selecting)
```
`0x04000000` is the "nothing targeted" sentinel for a ServerId. Decode (see `read_target`):
- bit clear → main = `Targets[0].ServerId`, sub = none.
- bit set   → **sub = `Targets[0].ServerId`** (the cursor), **main = `Targets[1].ServerId`** (locked).

⚠️ Use the `+0x50` bit `0x00010000`, **NOT** the byte at `+0x78`. `+0x78` also goes 1 on
cursor-open but is STICKY: it stays 1 after you *confirm* the action (clears only on cancel),
leaving the sub bar stuck on. The `+0x50` bit clears on **both** confirm and cancel.

**Why not the obvious `FFXiMain+0x487F60`?** That static is only a *thin cache* of the
active reticle's `{index@+0x5C, id@+0x60}` (the value at `+0x58` is an unrelated constant
pointer). It follows the `<st>` cursor too, so it CANNOT tell main from sub, and it holds no
`Targets[1]`/flag. The real two-entry struct is the heap `target_t` above.

**How it was found (probe chain, all via `//aio …`):**
1. `//aio tgt2` — Tab a party member, then scan all memory for that ServerId. The real
   `target_t` is the hit with `Index@-4` small **and** a valid `EntityPointer@+4` **and**
   `Targets[1].ServerId@+0x28` is *another* party id. Then scan for a STATIC (FFXiMain)
   pointer holding that heap base → the `0x57876C` anchor. (Cap the id-scan high: the id
   appears 1000s of times; `target_t` can be past the 1024th hit.)
2. `//aio sub` — per-frame change-watcher over `target_t[0..0x80]`. Open then cancel a
   `<stpc>` cursor: the dword that flips `0↔1` on open/cancel is the flag (`+0x78`).
These probe commands live in `plugin/aiohud.cpp` (kept for re-locating after a client patch).

**Party-window picker cursor (Quartermaster / Lottery / remove member / leader) — WORKING, 2026-06-28.**
This picker is **NOT a target** — `target_t`, the LuaCore target list `*(g+0x30)`, and the targeting
struct all stay `0x04000000` ("nothing") while it's open. It's a pure **menu cursor**: when it's
active the focused-menu pointer `*(FFXiMain+0x5EED6C)` points at the menu named **`"partywin"`**
(name at `def+0x4E`, `def = *(menu+0x04)`), and the hovered member is a **1-based cursor index at
`menu+0x4C`** (`+0x08` = a pointer to the selected row's UI object). The party window lists members
in slot order = our row order, so the hovered member is `rows[index-1]`. `poll_game_state` fills
`GameState::partyMenuSel`; the party widget frames that row. Found with `//aio pcur` (needs ≥2 party
members so the index is non-zero — solo it is `0`, an all-zero byte you can't pick out). The probe
(`g_pcur_probe`) stays in `plugin/aiohud.cpp` for re-locating after a client patch.

## See also
- [The party member ARRAY in memory](party-array.md)
- [Party cast bar — the 0x028 action packet](cast-bar.md)
- [Action-menu info box](action-menu.md)
