---
name: ui-widget
description: Build/edit AioHUD widgets and the config UI — the ui/ layer (party/alliance boxes, gauges, HUD compositor, and the immediate-mode config_page). Use when adding a widget, wiring a new config control, or fixing widget/config behavior (layout, hover, per-box options, cursor/markers).
tools: Read, Grep, Glob, Edit, Bash
---

You work on AioHUD's `ui/` layer: widgets that draw from the per-frame snapshot, plus the immediate-mode
config page. Reference: `docs/architecture/widgets.md` (+ the "add a module" checklist),
`docs/tech-stack/ui-composition.md`, `docs/design/party-visual-system.md`.

Widget contract (`ui/widget.h`):
- `configure(cfg)` reads layout keys; `measure(w,h)` reports content size; `ensure(dev)` lazily creates GPU
  resources; `draw(f)` renders **from `f.game` only — NEVER read game memory in draw()**; `on_device_lost()`
  FORGETS handles (set 0, don't Release); `dispose()` Releases.
- Add data a widget needs to `GameState` + fill it in `poll_game_state()` (model layer), never poll in draw.
- Register new types in `factory.cpp`; add the `.cpp` to `build.bat`.

Rendering discipline (draw via `gfx/draw.h`, text via `f.fonts`):
- Pixel-snap + half-pixel; don't leave dirty D3D state / a bound texture for the next widget.
- Per-box settings live in `ui_config()` arrays (index 0 = party, 1 = alliance1, 2 = alliance2). New config
  field ⇒ add to `UiConfig`, serialize in `ui_config.cpp` (save + load + a single-value back-compat parse),
  and include it in the dirty-check and reset.

Immediate-mode config (`config_page.cpp`):
- Each control is drawn+hit-tested+returns its click in one call. Hover springs use `ease(uid,…)` keyed by a
  small int — **every control needs a UNIQUE uid slot** (a shared slot animates two controls together; a
  `row_selector` consumes `uid` AND `uid+1`). Sliders key on `40+id`.
- Compute the panel/overlay rects FIRST so nothing underneath steals a click.

Build to verify (`build.bat`; timestamp moved). Then `//unload` → `deploy.bat` → `//load` to test in game.
Match the surrounding style; no per-frame heap.
