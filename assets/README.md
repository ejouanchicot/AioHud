# assets/

Everything the plugin loads at runtime, plus the sources it was baked from.

**The distinction matters at deploy time**: `deploy.bat` and `package.bat` ship the runtime files and
deliberately **exclude every `*_src/` folder**. A `_src` folder is a regeneration input — PNGs, DDS, whatever
the tool needed — and shipping it would roughly double the payload for no gain.

| Path | Kind | Notes |
|---|---|---|
| `*.raw` (flat) | **runtime** | Raw BGRA, loaded directly. No decoder in the plugin — that is the point |
| `gearicons/` | **runtime** | 1 323 pre-extracted 32×32 item icons |
| `window/` | **runtime** | The FFXI window-skin pieces |
| `job_icons_src/` | source | Job PNGs → `job_icons.raw` (`scripts/`, Python + PIL) |
| `window_src/` | source | Window DDS → `window/` (`scripts/gen_window_skin.sh`) |
| `marker_src/` · `weapon_icons_src/` · `icon_gil_src/` · `icon_th_src/` | source | Same pattern, one atlas each |

## Two things that have cost real time

- **`deploy.bat` overwrites `gearicons/` from the repo.** So any in-game test that depends on a *missing*
  icon is undone the moment you deploy. Test that case by pointing at a fresh id, not by deleting a file.
- **The bundled gear icons are one player's EquipViewer cache.** Coverage is therefore biased toward that
  player's gear, and an icon bug hits other users far harder than it hits the dev machine. A missing icon is
  the normal case, not the exception — the resolver must degrade gracefully.

## See also

- [`../docs/game-data/player/gear-icons.md`](../docs/game-data/player/gear-icons.md) — how an item id becomes an icon
- [`../scripts/README.md`](../scripts/README.md) — the bakers that produce these files
- [`../docs/design/box-themes.md`](../docs/design/box-themes.md) — the procedural materials, generated at runtime rather than stored here
