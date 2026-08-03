# AioUpdate — in-game updater for AioHud (no window)

A tiny Windower **Lua addon** that updates the AioHud **plugin** from its latest GitHub release, in game — with **no console window** at any point.

## Why a plugin *and* an addon?
- A compiled C++ plugin can't hot-swap or **unload itself** (it would crash mid-call).
- A Lua-spawned process (`io.popen` / `os.execute`) always **flashes a cmd window**.

So the work is split:
- **The plugin** launches the updater PowerShell with `CreateProcess + CREATE_NO_WINDOW` (native → truly no window): it checks the latest release, downloads it, verifies its checksum, **stages** it, **waits** for `AioHud.dll` to unlock, installs it over the **Windower root** (so it refreshes both `plugins\AioHud\` and this `addons\aioupdate\` addon), and writes `data\update\done.txt`.
- **This addon** only sends the trigger (`//aio update`) and does the `//unload` + `//load` the plugin can't — all in **pure Lua** (`send_command` + polling that file), so it never opens a window either.

## A failed update leaves the install exactly as it was

Everything that can fail happens **before** the plugin is unloaded: download, checksum, and extraction into a scratch folder, whose `AioHud.dll` is then checked for real. Only then does the HUD unload, and the install itself is additive — assets first (`robocopy`, incremental), the DLL last, from a backup that is put back if that single copy fails.

This is not decoration. Until v1.0.74 the payload was expanded with `Expand-Archive -Force` **straight over the live Windower root**, and that cmdlet deletes each destination file before writing it, then deletes everything it has already expanded if anything throws on the way. One access-denied, antivirus or I/O hiccup anywhere in the ~1400-file payload therefore left **no `AioHud.dll` at all** — the addon then asked Windower to load a file that no longer existed (`Error: aiohud - File does not exist.`) while the actual reason sat unread in `done.txt`. A failed update must never be able to uninstall the plugin, and it must say why.

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
