---
title: HUD & UI design references
summary: The design rationale behind AioHUD's party module visuals and the principles it applies — color = state, flash only for critical, show on demand, strong hierarchy.
source: TECH_STACK.md §14
---
# HUD & UI design references

The *why* behind the party module's visual rules (HP dominates, target unambiguous, combat-glanceable,
18 rows dense) lives in **[the party/alliance design brief](../design/brief-party-alliance.md)**. Supporting design reading:

- [Game HUD essentials (Page Flows)](https://pageflows.com/resources/game-hud/) — visual hierarchy, when to show info.
- [HUD design guide (Sunstrike)](https://sunstrikestudios.com/en/blog/HUD_design_in_games/) — readability, color coding, critical-state flashing.
- [Game UI principles (Justinmind)](https://www.justinmind.com/ui-design/game) — layered info architecture, context-sensitive display.
- [Heads-up display (Interaction Design Foundation)](https://ixdf.org/literature/topics/heads-up-display-hud) — attention distribution against dynamic backgrounds.

Principles we apply: **color = state** (green→yellow→orange→red HP, blue MP, magenta+glow TP-ready,
blue/yellow/red distance), **flash/pulse only for the critical** (HP ≤ 25 %, WS-ready), **show on
demand** (cast line, out-of-zone), and **strong hierarchy** (HP biggest, name bound to it, target frame
unmistakable).

## See also
- [UI composition — immediate-mode controls + 9-slice skin](ui-composition.md)
- [Fonts — GDI bundled, atlas-rasterized](fonts.md)
- [Party visual system](../design/party-visual-system.md)
- [Brief: party / alliance design](../design/brief-party-alliance.md)
