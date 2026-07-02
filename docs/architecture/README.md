# Architecture

How the AioHUD plugin is wired and **how to add a new module fast**. For the reverse-engineered
ABI, memory offsets and D3D rules, see the **[Reference](../reference/README.md)** section (the source of truth); this section
is the map *above* it — the data flow, the conventions, and the recipes we used to reverse
the game.

- [Layers](layers.md) — the four source layers (`gfx`/`model`/`ui`/`plugin`) and the dependency rule.
- [The per-frame data flow](data-flow.md) — the render hook, the `GameState` snapshot seam (the important diagram).
- [Widgets](widgets.md) — the `Widget` base class lifecycle + the add-a-new-module checklist.
- [Reading the live roster](roster.md) — how `party_state.cpp` reads the 18-slot member array.
- [Reverse-engineering recipe](reverse-engineering-recipe.md) — Ghidra LuaCore route + live differential probe.
- [Build / deploy / iterate](build-deploy.md) — the build/deploy/load cycle and its gotchas.
- [Notable conventions](conventions.md) — the codebase's non-negotiable rules.
