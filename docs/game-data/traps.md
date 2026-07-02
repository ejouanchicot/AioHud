---
title: Two Traps That Cost Real Time
summary: The dangling self-name pointer bug and the job-ID-vs-job-LEVEL offset trap, plus the //aio debugging recipe.
source: REFERENCE.md §9c
---
# Two traps that cost real time (don't repeat them)

1. **Self name renders as `~??????` — dangling pointer, not a bad offset.**
   `Party::build_rows()` had `PlayerInfo me;` as a **stack local** and
   `fill_self()` stored `row.name = me.name`. The rows are consumed by the
   **caller** (`Party::draw`) *after* `build_rows` returns → `me` is gone → the
   `const char* name` dangles → garbage. Other members were fine because their
   `name` points into the **global** `g_party` (stable). **Fix:** make `me`
   `static`. **Rule:** `Row.name` is a *non-owning* pointer — only ever assign it
   something whose lifetime ≥ the frame (a global, a string literal, or `static`).

2. **Job badge empty — read the job ID, not the job LEVEL.** `read_player` first
   read main job at `+0x98`, which is the main-job **level** (`99`); `job_abbr(99)`
   is out of range → `""` → no badge. The `+0x7D..+0x92` all-99 **job-level array**
   is exactly what makes a stray `0x63` look like a "job". The real ids are the
   u32-aligned fields **`+0x94` (main)** and **`+0x9C` (sub)**, with the level
   wedged between them at `+0x98`. When an RE'd "job" reads ~99, you're on a level
   field — step back to the aligned id.

**Debugging recipe** (all via `//aio …`, plugin stays **loaded** — no unload):
`//aio vit` logs the `player` pointer; `//aio dump <player> 200` hexdumps it;
`//aio party` logs each member's `id` + `flags` + the resolved leaders;
`//aio chain` walks the alliance struct; `//aio find <hexid>` scans memory for a 32-bit
value (how we located the member array). Unload is ONLY needed to redeploy a rebuilt DLL.

## See also
- [Local player struct](player-struct.md)
- [Party member packets](party-packets.md)
- [The party member ARRAY in memory](party-array.md)
