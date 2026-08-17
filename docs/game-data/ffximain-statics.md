---
title: FFXiMain statics — the addresses that move at every client patch
summary: The six FFXiMain RVAs AioHUD reads, why a game update breaks exactly those and nothing else, and how each one re-derives itself at runtime from a proof only the real address can pass.
---

# FFXiMain statics, and how they repair themselves

Everything AioHUD reads hangs off one of two anchors:

- **LuaCore's data root** `g = *(LuaCore + <rva>)` — Windower's own DLL. A **game** patch does not touch it…
  but a **Windower** update does, and Windower updates itself silently for everyone. 4.7.9.3 moved it and
  blanked the entire HUD. That RVA is derived at runtime too now — see
  [luacore-data-root.md](luacore-data-root.md).
- **A hard-coded RVA inside FFXiMain.dll** — the game's own module, **recompiled at every client patch**.

Only six things live in the second group. That asymmetry is the whole story of this page: when the client
updates, a handful of features die *while everything else keeps working perfectly*, so the failure reads
like a widget bug rather than a patch.

## What happened on 2026-08-12

SE shipped a client patch. `FFXiMain.dll` was rewritten (+20 KB on disk) and **every** static below moved
by exactly `+0x40` — 64 bytes of new globals inserted ahead of them. The user-visible result was:

> "the selector in party doesn't pick players any more"

…and nothing else. The party list still drew, HP still updated, buffs still ticked — all LuaCore-fed. Only
`target_t` had died, taking `<t>`/`<st>` with it, which is what `party.cpp` uses to mark the selected row.
The cost/Next box and PointWatch were dead too; nobody had noticed yet.

| static | pre-patch | post-patch | feeds |
|---|---|---|---|
| `target_t` pointer | `0x57876C` | `0x5787AC` | party selection cursor, Target box, `<bt>` |
| live-menu pointer | `0x5EED6C` | `0x5EEDAC` | the cost/Next box (which menu is open) |
| examined SPELL id | `0x634F28` | `0x634F68` | the highlighted spell |
| examined ABILITY id | `0x634590` | `0x6345D0` | the highlighted job ability / weapon skill |
| PointWatch block | `0x485644` | `0x485684` | EXP, Master Level, Exemplar |
| PointWatch merits | `0x485826` | `0x485866` | Limit Points, merit count |

**The uniform `+0x40` was a coincidence of this patch**, not a rule. Statics in different sections shift by
different amounts. Never apply a global delta without re-validating each address — which is exactly why
every mechanism below proves each one separately.

## RVA versus struct offset

The distinction that decides everything:

- an **RVA** is where a variable sits *inside the module* — it moves whenever the module is rebuilt;
- a **struct offset** (`target_t+0x04`, `member+0x1C`, `desc+0x3C`) is where a field sits *inside a
  structure* — it moves only if SE redesigns that structure, which is a matter of years.

So the addresses are worth automating and the offsets are not. When a struct offset *does* change, no
healing helps and it takes a real reverse session — `//aio mbox` exists to tell the two apart in the one
place where they produce the same symptom (a cost box that appears but stays blank).

## The registry — `model/ffximain_rva.{h,cpp}`

The six addresses are **data, not constants**: a seed (the last known-good value) plus a runtime proof.
Nothing else in the codebase may hold a copy — `game_mem.cpp`, the probes and `//aio doctor` all read
`fm_addr()`. A second copy is what turns the next patch into three edits and one forgotten one.

Each static is adopted only on evidence nothing else can produce:

| static | proof | needs |
|---|---|---|
| `target_t` | a pointer whose `Targets[0].ServerId` equals the id of the entity at `+0x08`, that entity being the one `entity_array` holds at its own index | you target anything |
| PointWatch block | the exact values packet **0x061** just delivered (14 bytes across a `0x60` span) — the client mirrors that packet body verbatim | nothing; zone or Status menu |
| PointWatch merits | the values from packet **0x063 order 2**, taking the match *nearest* the 0x061 block | merit menu |
| live-menu ptr | a slot showing **two different non-empty menu names** — `logwindo` and `inline` are always themselves; only the focused slot follows what you open | open two different menus |
| examine caches | a value that decodes to a real action **while that action's own menu is open**, or one that changes to a *different* real action as the cursor moves | move the cursor in the menu |

Findings are cached to `data\rva_cache.txt`, keyed by a **client fingerprint** (`SizeOfImage ^
TimeDateStamp`), so the work happens once per client build and the file is discarded automatically the next
time the game is patched. The file also carries a `format=` version, bumped whenever a healer changes its
mind about what counts as proof — without it, an address adopted by an older, weaker rule comes back from
disk marked "confirmed" and outlives the fix. **Only proven addresses are written**; a proposal borrowed
from another static's shift stays in memory, because persisting a hypothesis promotes it to fact.

## Commands

| command | what it does |
|---|---|
| `//aio doctor` | reports all six: address, live value, whether it is PROVEN, and by what |
| `//aio rva` | scans the image for the three structural signatures and adopts what it proves; the report to send when something is still dark |
| `//aio rva break [delta]` | **breaks every address on purpose**, in memory only, so the healing can be watched working |
| `//aio mbox` | why the cost box is *empty* rather than absent — separates a bad address from a broken struct offset from a widget bug |

`//aio rva break` earned its place. The healing runs about once a year, so without a way to trigger it on
demand it would go untested until the day everything depends on it. Two of the three test rounds found a
real defect:

- the first menu healer adopted the **log-window slot**, because it treated a transient empty read as a
  changed name — a confidently wrong answer, the failure mode worse than a dead feature;
- the examine caches could not recover when the anchor's shift was **zero**, and had no proof of their own
  at all, so a cache moving independently would have left the box blank forever.

Neither would have surfaced before the next patch.

## What this does *not* cover — and the sentinel that watches for it

Healing repairs a **move**. It cannot repair a **redesign**: if SE changes a structure's layout, or shifts a
field inside a packet, the values keep reading and start being quietly wrong. Nothing goes dark, so nobody
looks. That is the dangerous class.

`model/sentinel.{h,cpp}` exists for it. Several values reach us **twice**, by paths that break
independently — the server sends them in a packet, and the client also keeps them in memory. Today one is
used and the other ignored; made to disagree out loud, that redundancy becomes an alarm for breakage nobody
predicted. It **never repairs anything**: a cross-check cannot tell which side is wrong, so acting on it
would be guessing with the user's data. It reports, names both values, and stops.

| pair | packet side | memory side | compared on |
|---|---|---|---|
| `SEN_MEMBER` | 0x0DD name / job / level | the member block via `read_member` | identity, never HP |
| `SEN_BUFFS` | 0x063 order 9 buff ids | the self buff array | set **overlap**, not equality |
| `SEN_POINTWATCH` | 0x061 / 0x063 values | the static block | raised by the re-pinner when the values match *nowhere* |

Choosing the pairs **is** the design, and the trap is obvious only in hindsight: comparing HP compares two
samples taken at different instants, so it would disagree constantly and the alarm would be muted within a
minute. Every pair compares something stable — a name, a job, a level — or an overlap that one expiring
entry cannot break. Likewise a single disagreement says nothing: it takes **four in a row, each from a
different packet**, and any agreement resets the streak. Real breakage does not agree intermittently.

`//aio doctor` prints one line per pair, including **"not checked yet"** — because a cross-check that never
ran is not reassurance, and printing only the good news makes the two look identical.
