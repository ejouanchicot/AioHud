# AioUpdate — in-game updater for AioHud (no window)

A tiny Windower **Lua addon** that updates the AioHud **plugin** from its latest GitHub release, in game — with **no console window** at any point.

## Why a plugin *and* an addon?
- A compiled C++ plugin can't hot-swap or **unload itself** (it would crash mid-call).
- A Lua-spawned process (`io.popen` / `os.execute`) always **flashes a cmd window**.

So the work is split:
- **The plugin** launches the updater PowerShell with `CreateProcess + CREATE_NO_WINDOW` (native → truly no window): it checks the latest release, downloads it, **waits** for `AioHud.dll` to unlock, extracts the zip over the **Windower root** (so it refreshes both `plugins\AioHud\` and this `addons\aioupdate\` addon), and writes `data\update\done.txt`.
- **This addon** only sends the trigger (`//aio update`) and does the `//unload` + `//load` the plugin can't — all in **pure Lua** (`send_command` + polling that file), so it never opens a window either.

Your settings are safe: the release zip contains only `plugins\AioHud.dll` + `plugins\AioHud\assets\` + `plugins\AioHud\design\` + `addons\aioupdate\` — your `plugins\AioHud\data\` (config, profiles) is never touched.

## Install (once)
The release zip already places this addon in `<Windower>\addons\aioupdate\` when you extract it into your Windower root. So you only need to load it once:

1. In game: `//lua load aioupdate` (or add `lua load aioupdate` to `<Windower>\scripts\init.txt` to auto-load).

The `aioupdate.ps1` script ships **with the plugin** (in `plugins\AioHud\assets\`), so this addon is just the `.lua`.

## Use
```
//aioupdate      check for a newer release and, if any, update AioHud (the HUD blinks off ~3s during the reload)
```

## Dual-box note
Windower keeps the DLL locked while **any** client has AioHud loaded. On a dual-box setup, `//unload AioHud` on the **other** client first, then `//aioupdate` — otherwise the updater waits ~30s for the lock and gives up (the addon just reloads the current build).
