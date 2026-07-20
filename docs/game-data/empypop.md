---
title: EmpyPop — Abyssea empyrean NM pop-chain tracker
summary: How the EmpyPop module resolves a tracked NM's pop item/key-item chain against live inventory, key items and the treasure pool, emitting data (not text) for the widget.
source: model/party_state_empypop.cpp, model/nms_gen.h, ui/hud_empypop.cpp, ui/ep_config.cpp
---
# EmpyPop — Abyssea empyrean NM pop-chain tracker

A C++ rewrite of the upstream **Empy Pop Tracker** (Xurion of Bismarck, `modules/empypop.lua`).
For a chosen Abyssea empyrean NM it resolves the whole **pop chain** — which trigger items and key
items you need, which sub-NM drops each of them, and how many of each you already hold — and shows
it as an indented tree with per-group ready/not-ready state.

## Where the data comes from

Three reads, all already documented elsewhere; EmpyPop itself reverses nothing new:

| what | source |
|---|---|
| key items owned | [`owns_key_item(id)`](key-items.md) — the flat `u8[8192]` at `*(g+0x4C)` |
| items owned | [`count_item(id)` / `count_items`](inventory.md) — all 18 bags at `*(g+0x50)` |
| items still in the lot window | the packet-fed [Treasure Pool](treasure-pool.md) (0x0D2/0x0D3) |

**Pool counting gotcha:** the treasure pool holds **one item per slot and has no count field**, so
N copies of a drop occupy N slots — `ep_pool_count` **counts slots, it does not sum a quantity**.

## The generated NM table

`model/nms_gen.h` (**generated** by `scripts/gen_nms.py`, BSD-3 upstream data — do not hand-edit):

- `NMS[]` — one row per NM: `key` (the lookup string, e.g. `"briareus"`, `"chloris"`), English
  name, `popCount`, and an optional `collectable` item id.
- `POPS[]` — the pop nodes, each of which may reference a **sub-chain** (the NM that drops it).
- `nm_by_key(key)` — the lookup used by the config selector and `//aio ep <key>`.

## Resolution — one bounded DFS, two input sources

`ep_add_chain()` appends `POPS[idx]` and its whole sub-chain **depth-first**, recording a `depth`
per node. DFS order + per-node depth is what lets the widget draw the indented tree with a **flat
loop** — no tree walk in `draw()`.

Two deliberate departures from the Lua original:

1. **It emits DATA, not pre-coloured text lines.** `model` must not know about rendering
   ([layering rule](../architecture/layers.md)). The widget decides the colours.
2. **The recursion is bounded and CLAMPED.** Fixed-capacity arrays, no heap, no unbounded descent.
   Hitting `EmpyPop::MAX_NODES` makes `ep_add_chain` return `false` and the caller **stops** —
   a regenerated table that grew too large degrades to a *truncated chain*, never to corruption.

Result state lives in `EmpyPop` (`model/party_state.h`): `nodes[]` (`id`, `isKI`, `name`, `owned`,
`pool`, `depth`, `fromName`), `groups[]` (`first`/`count`/`obtained`), plus the collectable
(`collId`, `collCount`, `collTarget`, `collPool`, `collDone`) and `valid` / `allDone`.

## The demo/preview path

`sample = true` swaps the two memory reads for a fixed fake inventory (**CHLORIS** holding key
items 1470 + 1471, one fake item, one drop sitting in the pool, 30/50 of the collectable) — chosen
because it shows *every* widget state at once: some groups green, some red, one orange `[n]`, and
overall NOT ready.

It lives in **model**, not ui, because assembling an `EmpyPop` means reading `nms_gen.h` and `ui/`
must not include the generated tables. **The same single recursion serves both paths**, so the
config preview can never drift from the live box. Being pure data, the preview never touches game
memory — it renders identically on a fresh character who owns nothing.

## Widget & config

- Widget: `ui/hud_empypop.cpp`, edit-mode box like every other module.
- Config: **`ui/ep_config.cpp`**, sidebar section **12** ("EmpyPop") — see
  [architecture/config-panels.md](../architecture/config-panels.md).
- Persistence: `ep=` (show / scale / x / y / show-collectable), **`eptrack=`** (the tracked NM key —
  read as **rest-of-line, not `sscanf("%s")`**, because a key can contain spaces like
  `"arch dynamis lord"`, and the trailing newline must be stripped or `nm_by_key` never matches),
  `epText%d=` and `epbox=` — all parsed by
  the out-of-line **`parse_ep_line`**, which is the *original* C1061 workaround the later
  `parse_db_line` / `parse_mm_line` / `parse_zt_line` all copied.

## Probes

| command | what it logs |
|---|---|
| `//aio ep [key]` | resolves that NM (default `briareus`) and logs the whole chain: per node `KI/IT`, id, name, `owned`, `pool`, and which NM it comes from; plus the collectable line |
| `//aio eplist` | every NM `key` the generated table knows, so `//aio ep <key>` can be aimed |

Both are probe-build only — see [The in-game RE probe toolkit](../architecture/re-probes.md).

## See also
- [Key items](key-items.md) — the `u8[8192]` read behind `owns_key_item`.
- [Inventory](inventory.md) — the 18-bag `count_item` read.
- [Treasure Pool](treasure-pool.md) — the 0x0D2/0x0D3 pool this cross-checks against.
- [Config panels](../architecture/config-panels.md) — where `ep_config.cpp` sits and the C1061 rule.
- [Layers](../architecture/layers.md) — why the model emits data, not coloured lines.
