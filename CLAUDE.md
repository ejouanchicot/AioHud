# CLAUDE.md — AioHUD

A **32-bit C++ Direct3D 8 fixed-function** overlay plugin for **Final Fantasy XI** via **Windower 4**.
It redraws the party/alliance HUD on top of the game. No shaders, no UI framework — hand-rolled quads,
anti-aliased vector primitives, raw BGRA textures, GDI fonts, fed by SEH-guarded game-memory reads and
packet hooks.

## Build / deploy / iterate
```
build.bat        # -> build\AioTest.dll  (MSVC cl, 32-bit). FAILS on a nonzero cl exit — keep that check.
# in game:  //unload AioTest            (Windower FILE-LOCKS the loaded DLL — always unload first)
deploy.bat       # copies build\AioTest.dll -> plugins\AioTest.dll
# in game:  //load AioTest
```
- After a build, verify the DLL **timestamp actually moved** (a silent stale DLL has cost real time).
- Out-of-game demo: `//aio party demo` (+ `alliance1|alliance2 demo`) drives the layout from baked rows.

## Where things live
- **Docs are one-topic-per-file under `docs/<folder>/` — start at [`docs/README.md`](docs/README.md)** (the map).
  - `architecture/` — layers, per-frame data flow, widgets, the "add a module" checklist.
  - `reference/` — reversed plugin ABI + the D3D8 rendering rules (`reference/d3d8-rendering.md`).
  - `game-data/` — reversed memory offsets & packets (player struct, party array, target, casts, buffs).
  - `tech-stack/` — one file per technology + curated engineering references.
  - `design/` — the party brief + visual system + edit-mode zones.
- Code: `src/gfx` (D3D backend, zero game knowledge) · `src/model` (game data only, zero rendering) ·
  `src/ui` (widgets + HUD compositor) · `src/plugin` (IPlugin glue) · `include/` (ABI wrappers).
- **Dependency rule:** `ui → model + gfx`; `model → nothing in ui/gfx`; `gfx → nothing`. Keep it.

## Non-negotiable rules (the ones that bite)
1. **Pixel-snap + half-pixel offset** or borders/text blur (`snap()`; see `reference/d3d8-rendering.md`).
2. **Feather all edges AND corners consistently**, or none — partial feathering shows a 1px dark "black line".
3. **Reset the blend after an additive pass** (`cs()` / DESTBLEND back to INVSRCALPHA) or the next draw glows.
4. **`on_device_lost()` FORGETS GPU handles** (set to 0) — do NOT `Release` (old device may be dead).
   `dispose()` Releases (device alive); `ensure()` lazily recreates.
5. **SEH-guard every game-memory read** (`safe_read` / one guarded block copy), validate with `valid_ptr`.
   A bad pointer must degrade to a no-op, never crash.
6. **Snapshot, don't poll-in-draw**: widgets read `f.game` (the once-per-frame `GameState`); only the
   poller (model) reads memory. The bulky roster lives in the `party()` singleton, refreshed once/frame.
7. **One source of truth for every offset** (the poller / `read_member`), documented in `game-data/`.
   Reverse a new offset before using it (recipe: `architecture/reverse-engineering-recipe.md`).
8. **Don't leave dirty D3D state / a bound texture** for the next widget (the HUD wraps one save/restore).
9. **Immediate-mode config** (`config_page.cpp`): each control's hover spring is keyed by a small int
   `uid` via `ease(uid,…)`. Give every control a UNIQUE `uid` (a shared slot animates two controls together).

## Gotchas
- `src/model/*_gen.h` (spells/abilities/weapon_skills) are **generated** — don't hand-edit; regenerate via `scripts/`.
- Runtime/source assets: `assets/*.raw` are loaded at runtime; `assets/job_icons_src/` (job PNGs) and
  `assets/window_src/` (window DDS) are regeneration SOURCES (Python/PIL + `scripts/gen_window_skin.sh`).
- Job role colour = ONE tint per job (`job_role_color`, `party_state.cpp`): healer green WHM/RDM/SMN,
  buffer yellow GEO/COR/BRD, tank blue RUN/PLD, DD red everyone else, SPC purple.

## Conventions
- No per-frame heap allocation; fixed-capacity arrays. Match the surrounding code's density/idiom.
- Commit only when asked; branch off `main` isn't the working branch (`master` is). End commit messages with the
  `Co-Authored-By` trailer.
