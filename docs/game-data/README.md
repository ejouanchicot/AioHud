---
title: Game data — reversed offsets, packets and structs
summary: What WE reversed out of the client, grouped by subject. Six subfolders plus the verbatim wiki sheets.
---

# Game data

Everything AioHUD reads out of FFXI, and how it was found. **This is the expensive folder** — reproducing it
costs hours of captures and static analysis, which is why it is one of only two subtrees under `docs/` that
git tracks.

AioHUD sources live data two ways: the **local player** from memory (always present, always accurate) and the
**other party members** from inbound packets — the game never sends you your *own* party packet. Code:
`model/game_mem.cpp` (memory) and `model/party_state.cpp` (packets).

## By subject

| Folder | What is in it | Open first |
|---|---|---|
| [`player/`](player/README.md) | You: the player struct and every chain hanging off it — equipment, inventory, key items, gil, encumbrance, XP/capacity, item icons | [player-struct](player/player-struct.md) |
| [`party/`](party/README.md) | The 18-slot roster, the packets that refresh it, and party casting bars | [party-array](party/party-array.md) |
| [`target/`](target/README.md) | The mob: its struct, the debuffs on it, and who has its attention | [target-substruct](target/target-substruct.md) |
| [`buffs-and-timers/`](buffs-and-timers/README.md) | Where a buff comes from and how long it lasts — **the most bug-prone corner of the project** | [timers](buffs-and-timers/timers.md) · [song-duration](buffs-and-timers/song-duration.md) |
| [`actions/`](actions/README.md) | The 0x028 action packet and what is derived from it | [action-packet](actions/action-packet.md) |
| [`world/`](world/README.md) | Zones, the minimap, and the instanced content the Zone Tracker follows | [zone-tracker](world/zone-tracker.md) |
| [`reference-sheets/`](reference-sheets/README.md) | **Not ours.** What the game and the wiki already say, kept verbatim because our models were derived from it | [song_resume.html](reference-sheets/song_resume.html) |

## Cross-cutting, so kept at this level

- [**Two traps that cost real time**](traps.md) — the dangling self-name pointer and the job-ID-vs-job-LEVEL
  offset. Read before reversing anything: both look like working code until they don't.
- [**Core offsets verified against LuaCore**](luacore-verified-offsets.md) — the 2026-07-19 Ghidra audit.
  Party stride and fields, the member→entity hop, the 0x900 bound, the entity struct, recast constants, the
  PointWatch RVAs — each checked against the Windower binding that implements it. **Two entity fields came
  back contradicted**, which is the point of the page.

## The rule that governs this folder

**One source of truth per offset.** An offset lives in the poller (`read_member` / `game_mem.cpp`) and is
documented here, once. If you find yourself reading the same field in two places, one of them is about to
drift. See [`../architecture/conventions.md`](../architecture/conventions.md).

**Reverse it before using it** — [`../reverse-engineering/recipe.md`](../reverse-engineering/recipe.md) gives
the three channels in cost order. And **SEH-guard every read**: a bad pointer must degrade to a no-op, never
crash.

## What is NOT here

Verbatim wiki material used to sit in this folder — ~2 000 lines of tables drowning the reversed pages they
were meant to support. It moved to [`reference-sheets/`](reference-sheets/README.md). The split is the whole
point: **this folder is what only we know**, that one is what anybody can look up.

Two duplicates were deleted rather than moved: `enhancing-magic-wiki.md` was **byte-identical** to
`enhancing-magic.md` (this index used to call one a "curated RE summary" — it was the same wiki dump under a
second name), and `fili-attire-set-brd.md` was a strict subset of `fili-attire-set.md`.

Kept here on purpose, because they are **not** wiki content: `song-duration-items.txt` and
`enhancing-duration-items.txt` were lifted out of `Timers.dll` by static analysis. The game states most of
those percentages qualitatively, or not at all.
