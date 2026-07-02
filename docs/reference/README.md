# Reference — Windower 4 plugin ABI & rendering

Everything we worked out about writing a native C++ plugin for **Windower 4**
(hook `4.7.9.0`, this install). All of it was recovered with Ghidra from
`Hook.dll`, `FFXIDB.dll`, `Timers.dll` — there is no public SDK. This is the
single source of truth; the framework in `include/` implements it. These pages
cover the plugin contract, the host, its service interfaces, direct-device
rendering, coordinates, debugging, workflow and a minimal example. (Game-data
reversing lives under `../game-data/`.)

- [Plugin DLL Contract (ABI)](plugin-abi.md) — the two exports, calling convention, 34-slot vtable, and `//command` routing via GetDescription.
- [The Host — PluginManager](host-pluginmanager.md) — the object Init receives and its getters for the device and services.
- [Service Interfaces](service-interfaces.md) — Console, TextHandler/TextObject, PrimitiveHandler/PrimitiveObject and the ffxi interface.
- [Direct D3D8 Rendering](d3d8-rendering.md) — the FFXIDB custom-quad path, the zoning alpha gotcha, and the full clean-2D rules (half-pixel, filtering, blending).
- [Coordinate System](coordinates.md) — the 2560×1400 draw space and why GetViewport lies under supersampling.
- [Debug Enumeration & Address Mapping](debug-and-mapping.md) — dumping on-screen objects and runtime↔Ghidra address math.
- [Workflow & Gotchas](workflow-gotchas.md) — build/iterate cycle, orphans, live config tuning, performance.
- [Minimal Working Example](minimal-example.md) — a complete plugin that draws a rect + label and cleans up.
