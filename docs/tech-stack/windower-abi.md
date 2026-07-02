---
title: Windower 4 (IPlugin ABI)
summary: Why AioHUD is a Windower 4 plugin, how it implements the IPlugin ABI (render/packet hooks, //aio dispatch), and why the device hook is host-owned.
source: TECH_STACK.md §3
---
# Windower 4 (`IPlugin` ABI)

**Why.** Windower 4 is the FFXI overlay/addon host. A **plugin** is a compiled C++ DLL (vs. an
**addon**, which is Lua) — plugins get the low-level render + packet hooks we need, which Lua can't
do at frame cost.

**How we use it.**
- `plugin/aiohud.cpp` implements the `IPlugin` ABI: `init` / `unload`, the **render hook**
  (`aio_plugin_render6` → `Hud::render`, once per frame), the **packet hook**, and the `//aio`
  command dispatch.
- **Windower already owns the D3D device hook** and hands it to our render callback — so we *don't*
  hook `EndScene`/`CreateDevice` ourselves. The references below document how a standalone overlay
  *would* do it (vtable index 42 on `IDirect3DDevice8`, a trampoline via MinHook/Detours); useful
  background for how the frame we draw into is intercepted.
- `include/windower.h` wraps every host interface call, **all SEH-guarded**.
- Windower also exposes game data as **Lua bindings** (`get_player`, `get_mob_by_target`…). We don't
  *call* them — we **reverse the C function behind a binding** to learn the exact pointer chain the
  game uses (see [game-data I/O](game-data-io.md) and [reverse-engineering recipe](../architecture/reverse-engineering-recipe.md)).

**Gotchas.** The DLL is file-locked while loaded → always `//unload` before overwriting. Packet
handlers must be cheap and must not throw.

**References.** [Windower plugins](https://docs.windower.net/plugins/) ·
[Windower addons](https://docs.windower.net/addons/) ·
[Plugin commands](https://docs.windower.net/commands/plugin/) ·
[Windower support libraries (GitHub)](https://github.com/DefrostedTuna/ffxi-windower-4-support-libraries) ·
*Overlay hooking background:* [Hooking Direct3D EndScene (BananaMafia)](https://bananamafia.dev/post/d3dhook/) ·
[EndScene hook example (GitHub)](https://github.com/stimmy1442/EndSceneHookExample) ·
[Runtime DirectX vtable hooking (rohitab)](http://www.rohitab.com/discuss/topic/34411-run-time-directx-hooking-using-code-injection-and-vtable/) ·
[MinHook — x86/x64 hooking library (GitHub)](https://github.com/TsudaKageyu/minhook) ·
[Microsoft Detours (GitHub wiki)](https://github.com/microsoft/Detours/wiki/OverviewInterception) ·
[X64 function hooking by example (Kyle Halladay)](https://kylehalladay.com/blog/2020/11/13/Hooking-By-Example.html)

## See also
- [C++ (32-bit, MSVC)](cpp-and-build.md)
- [Direct3D 8 — fixed-function, 2D](direct3d8-2d.md)
- [Game data — RE'd memory reads + packet hooks](game-data-io.md)
- [Plugin ABI](../reference/plugin-abi.md)
- [Host / plugin manager](../reference/host-pluginmanager.md)
