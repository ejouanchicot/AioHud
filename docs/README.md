---
title: AioHUD documentation — map
summary: Master index of the AioHUD docs. Start here, then open the one topic file you need.
---

# AioHUD — documentation

The docs are split into **one file per topic**, grouped in folders, so you (or a subagent) can
`glob` the tree, read a folder's `README.md`, and open **only** the file you need — no monolith
scanning. Every topic file carries YAML frontmatter (`title` / `summary` / `source`) and a
`## See also` list of relative links.

> Content is preserved verbatim from the original monoliths (`ARCHITECTURE.md`, `REFERENCE.md`,
> `TECH_STACK.md`, `EXPORT.md`, `BRIEF_DESIGN_PARTY.md`), just reorganized. Old `§` numbers in the
> `source:` frontmatter map each file back to its origin.

## Folders

| Folder | What's in it | Start at |
|---|---|---|
| [`architecture/`](architecture/README.md) | The **map above the code**: layers, per-frame data flow, widgets, roster, the RE recipe, build/deploy, conventions. | [layers](architecture/layers.md) → [data-flow](architecture/data-flow.md) |
| [`tech-stack/`](tech-stack/README.md) | The **technology layer**, one tech per file (C++/D3D8/blending/AA/stencil/textures/fonts/…), each with curated engineering references. | [tech-stack/README](tech-stack/README.md) (stack-at-a-glance + gotchas) |
| [`reference/`](reference/README.md) | The **reverse-engineered source of truth**: plugin ABI, host, service interfaces, D3D8 rendering rules, coordinates, debug/Ghidra, workflow, minimal example. | [plugin-abi](reference/plugin-abi.md) · [d3d8-rendering](reference/d3d8-rendering.md) |
| [`game-data/`](game-data/README.md) | The **reversed offsets & packets**: player struct, party array, target/sub, cast bar, action menu, member buffs, traps. | [player-struct](game-data/player-struct.md) · [party-array](game-data/party-array.md) |
| [`design/`](design/README.md) | The **design brief** + the party **visual system** + the edit-mode **zones**. | [brief-party-alliance](design/brief-party-alliance.md) |
| [`formats/`](formats/README.md) | On-disk **formats**: the layout descriptor JSON. | [layout-json](formats/layout-json.md) |

## Common entry points

- **"How do I add a new widget?"** → [architecture/widgets.md](architecture/widgets.md) (checklist) + [architecture/data-flow.md](architecture/data-flow.md).
- **"Which D3D8 rule did I break?"** (blur / black line / glow bleed) → [reference/d3d8-rendering.md](reference/d3d8-rendering.md) + [tech-stack/README.md](tech-stack/README.md) (rules that bite).
- **"Where's that memory offset / packet?"** → [game-data/](game-data/README.md).
- **"How was this reversed?"** → [architecture/reverse-engineering-recipe.md](architecture/reverse-engineering-recipe.md) + [reference/debug-and-mapping.md](reference/debug-and-mapping.md).
- **"What tech is this and how to use it well?"** → [tech-stack/](tech-stack/README.md).
- **"Design intent of the party box"** → [design/brief-party-alliance.md](design/brief-party-alliance.md) + [design/party-visual-system.md](design/party-visual-system.md).

## Conventions for these docs

- One topic per file; keep files focused (~50–150 lines). If a file grows two distinct topics, split it and add both to the folder `README.md`.
- Cross-link with **relative** paths (`../game-data/player-struct.md`), and add the target to `## See also`.
- When you reverse a new offset or add a technique, put it in the matching folder and add a bullet to that folder's `README.md` — never start a new monolith.
