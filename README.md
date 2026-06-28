# AioHUD — native Windower 4 plugin (reverse-engineered)

A from-scratch C++ HUD plugin for **Windower 4** (hook 4.7.9.0), built entirely by
reverse-engineering the existing plugin DLLs and the live FFXI client with Ghidra —
there is no public Windower plugin SDK.

It renders a full party + alliance display (XivParty-style), liquid HP/MP/TP bars,
and an in-game action-menu info box (spell MP + recast "Next", job-ability recast,
weapon-skill TP) driven by data read straight from FFXI/LuaCore memory.

## Read this first
- **`docs/REFERENCE.md`** — the complete reverse-engineered reference (plugin ABI,
  every interface + method + field offset, memory layout, coordinate system, the
  D3D8 2D rules, gotchas, examples). The single source of truth.
- **`docs/EXPORT.md`** — the mockup → native contract (how `design/` drives the UI).

## Layout
```
src/            native plugin (C++), layered:
  gfx/          D3D8 backend (textures, fonts, primitives, noise)
  model/        game memory reads, party/cast state, generated spell/ability tables
  ui/           widgets (party list, liquid bars), HUD compositor, layout
  plugin/       IPlugin ABI glue + //aio command dispatch (incl. RE probes)
include/        reusable framework: windower.h (ABI wrappers), windower_debug.h
design/         LIVE HTML/CSS mockup + dev server (serve.bat) + exports/layout.json
  icons/        element + WS-type sprites served to the mockup
assets/         runtime textures loaded by the plugin (.raw icons, cap .bin)
docs/           REFERENCE.md (full RE reference) + EXPORT.md (mockup contract)
re/             reverse-engineering artifacts (git-ignored, kept locally):
  ffximain_dump.bin   live POL-decrypted FFXiMain image (via //aio grabmod)
  ffximain_fixed.dll  repaired PE for static analysis
  ghidra_proj/        Ghidra project (LuaCore / Hook / FFXIDB analysed)
  scratch_*.log       headless decompile logs
research/        notes, legacy drafts, reference addon (Bars), concept art,
                 screenshots, source art, old sandbox experiment
scripts/         Ghidra headless scripts (RE) + asset generators
build.bat / deploy.bat
```

## Build & run
```
build.bat                 -> build\AioTest.dll  (32-bit MSVC; needs the FFXI process to load)
# in game:  //unload AioTest
deploy.bat                -> copies into plugins\AioTest.dll
# in game:  //load AioTest
```
Windower keeps a loaded plugin DLL **file-locked until //unload**, so always unload
before deploying.

## Design mockup (live UI iteration)
```
design\serve.bat          -> dev server with live-reload (port 8777)
```
Edit the mockup, export `design/exports/layout.json`, then `//aio layout` in game to
hot-reload the native widget placement. See `docs/EXPORT.md`.

## Useful in-game commands
```
//aio party demo            fake a full party (preview the layout out of game)
//aio alliance1|alliance2 demo   add 1 or 2 fake alliance parties
//aio demo off              back to live data
//aio layout                hot-reload design/exports/layout.json
//aio grabmod               dump the live FFXiMain image to re/ffximain_dump.bin
```
`//aio` also exposes the RE probes used to reverse the client (tgt / sub / anchor /
dump / find / pkt / menu) — kept because the live-alliance roster is still TODO.

## Status / next
- Done: party + alliance boxes (same gabarit, stacked with a themed separator),
  liquid bars, action-menu info box with live spell/JA recast, leader/QM markers,
  HP≤25% red danger blink (party + alliance), and **buff icons** left of each party
  row (self from memory, others from packet `0x076` — see docs/REFERENCE.md §9h).
- TODO: a real live alliance roster source (the model is still capped at the main
  party of 6); per-WS TP thresholds.
