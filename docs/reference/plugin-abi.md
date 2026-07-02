---
title: Plugin DLL Contract (ABI)
summary: The two exported functions, calling convention, IPlugin vtable slots, and console-command routing a Windower 4 native plugin must implement.
source: REFERENCE.md §1, §2b
---
# Plugin DLL Contract (ABI)

A Windower plugin is a 32-bit DLL that exports exactly two functions:

```c
extern "C" __declspec(dllexport) void*        CreateInstance();        // returns an IPlugin*
extern "C" __declspec(dllexport) unsigned int GetInterfaceVersion();   // MUST return 0x04070300
```

- Export them **undecorated** (`CreateInstance`, `GetInterfaceVersion`) via a `.def`.
- `GetInterfaceVersion` must return **`0x04070300`** or the hook refuses to load it.
- `CreateInstance` returns a pointer to an object whose first member is a 34-entry vtable.

Loader flow (Hook.dll `FUN_1006ee10`): `GetProcAddress` both exports → check
version → `CreateInstance()` → then immediately calls `vtbl[0]` and `vtbl[1]`
and runs `strlen` on the results (so they MUST return valid `char*`), then `vtbl[2]` (Init).

### Calling convention

All 34 vtable methods are `__stdcall` with **`this` passed as the first stack
argument** (callee cleans the stack, `ret N`). We model the vtable as a manual
array of `__stdcall` function pointers (see `windower_plugin.h`) so we control
each method's stack cleanup exactly. Stack-arg counts (incl. `self`) per slot,
measured from FFXIDB's `ret N`:

```
slot   0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33
args   1  1  2  1  1  1  1  2  2  4  4  5  5  6  4  5  5  1  1  0  0  0  0  1  1  3  3  4  4  5  3  4  4  0
```

### IPlugin slots we identified

| slot | meaning | notes |
|---|---|---|
| 0 | `char* GetName()` | shown in plugin list; strlen'd at load — must be valid |
| 1 | `char* GetDescription()` | strlen'd at load — must be valid |
| 2 | `Init(PluginManager* host)` | the host is the entry to everything (see [host / PluginManager](host-pluginmanager.md)) |
| 3 | `OnUnload()` | called once at `//unload` then `FreeLibrary` (`FUN_10070610` → `FUN_10070fb0` calls `vtbl[3]`). **Clean up your objects here.** |
| 4 | `bool IsCore()` | **MUST return 0** or `//unload` is refused ("Core plugins cannot be manually unloaded"). A `void` method leaks garbage in EAX → treated as core. |
| 5, 6 | per-frame render hooks | fire ~every frame (≈ framerate). Animate here. No device passed. |
| 7 | `HandleCommand(char* cmd)` | console command for this plugin. `cmd` = the args AFTER the alias (leading space already there). Routed by the **GetDescription alias**, NOT GetName. |
| 11, 12 | packet in / out | return `byte` (handled?) |
| 13, 14 | keyboard / mouse | fire during play |
| 32 | scalar deleting destructor | `vtbl[0x80](1)` when refcount hits 0 |

Everything else: safe no-op returning 0. (Our framework returns `u32 0` from
every method so EAX is never garbage.)

### 2b. Console commands (`//yourcmd ...`)  — REVERSED & WORKING

Native plugins **do** get `//` commands — the routing is just not by GetName.
Reversed from Hook.dll + a live dump of its plugin table:

- Hook keeps a plugin table (`*(Hook+0x1cbe4c) → +0xc = manager`, vector at
  `manager+0xa14a8..+0xa14ac`, **stride 100**). Each entry: `+0x04` IPlugin*,
  `+0x08` name (`std::string`, = **GetName lowercased**, used by //unload///load),
  `+0x58/+0x5c` = **command-alias vector** (`std::string`, stride 0x18).
- The dispatcher **`FUN_10042840`** walks every plugin's alias list, byte-compares
  (case-sensitive, `FUN_10037a00`) the typed token against each alias, and on a
  hit calls that plugin's **slot 7** with the remaining args.
- **The alias = the plugin's `GetDescription()` string, lowercased.** (Verified:
  our description "AioHUD D3D liquid" became alias `aiohud d3d liquid`; ffxidb's
  is `ffxidb`, luacore's `lua`, guildwork's `gw`.) So to get `//foo`, make
  **`GetDescription()` return `"foo"`** (a single clean lowercase word).
- `HandleCommand` (slot 7) then receives the text **after** the alias, e.g.
  `//foo hp 50` → cmd = `" hp 50"` (FFXIDB skips the leading space).
- Don't fight it with file polling — the GetDescription alias is the real path.

## See also
- [The host: PluginManager](host-pluginmanager.md)
- [Service interfaces](service-interfaces.md)
- [Minimal working example](minimal-example.md)
- [Workflow & gotchas](workflow-gotchas.md)
