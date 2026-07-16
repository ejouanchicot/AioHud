# AioHud

**A full HUD overlay for FINAL FANTASY XI** — a native **Windower 4** plugin, drawn live over the game in Direct3D 8. It replaces and extends the native interface with a cohesive, fully in-game-configurable set of modules.

---

## Features

- **Party & Alliance** — XivParty-style roster (your party + both alliance parties), liquid HP/MP/TP bars, jobs, leader/quartermaster markers, low-HP danger blink, per-member buff icons.
- **Target** — HP + cast/ability bars, distance, TH indicator, debuffs with timers, sub-target box.
- **Player Hub** — your vitals, job/level, equipment, buffs, speed.
- **Timers** — your active **buffs** (exact server durations) and **recasts** on one panel, plus a per-job **track list** with a 4-state focus system (Tracked / Tracked+focus / Hidden / Hidden+focus) and red loss alerts.
- **Minimap** — live radar with player/mob markers, target line, zone map, and a Vana'diel clock (day / moon).
- **Skillchains** — the open skillchain on your target: property, window countdown, and the weapon skills that continue it.
- **Hate List** · **PointWatch** · **Treasure Pool** · **Grimoire (SCH)** · **Zone Tracker** (Dynamis / Abyssea / Omen / Nyzul / Sheol).
- **Profiles** — character-bound: auto-named `Name Main-Sub` and auto-loaded on login / job change (custom profiles stay manual).

Everything is tuned from one full-screen config window (`//aio config`) with a live preview, in **English or French**.

## Install

1. Download **`AioHud-x.y.z.zip`** from the [latest release](../../releases/latest) and **extract it into your Windower root** (the folder that already contains `plugins\` and `addons\`, e.g. `D:\Windower\`). It drops the plugin in `plugins\` and the `//aioupdate` companion addon in `addons\` in one shot.
2. In game: `//load AioHud` (and, for one-click updates, `//lua load aioupdate`)
3. Open the config: `//aio config` · move boxes: `//aio edit`
4. Update later, in game, with **no window**: `//aioupdate`

## Build from source

Windows + MSVC (any recent Visual Studio with the **x86** C++ toolchain).

```bat
build.bat        REM -> build\AioHud.dll  (32-bit)
deploy.bat       REM copies the DLL + assets into your Windower plugins folder
package.bat      REM assembles a clean shippable payload into dist\
```
Windower keeps a loaded plugin DLL **file-locked until `//unload`** — always `//unload AioHud` before deploying. CI builds the DLL and publishes a Release on every `v*` tag.

## Project layout

```
src/       plugin source (C++), layered:
  gfx/     Direct3D 8 backend (textures, fonts, primitives)
  model/   game state, party/cast tracking, generated spell/ability tables, config
  ui/      widgets + HUD compositor + the config window
  plugin/  Windower plugin glue + //aio command dispatch
include/   Windower interface headers
assets/    runtime textures
design/    HTML/CSS layout mockup + exports/layout.json (default box layout)
scripts/   asset + data-table generators
```

## Notes

Unofficial, fan-made overlay — **not affiliated with or endorsed by Square Enix**. Provided as-is; use at your own discretion. Runtime data (config, profiles) lives under `plugins\AioHud\data\` and is created in-game.
