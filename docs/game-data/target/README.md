---
title: game-data / target
summary: The mob you are fighting: its struct, what is on it, and who has its attention.
---

# La cible

The mob you are fighting: its struct, what is on it, and who has its attention.

| Page | Ce qu'elle contient |
|---|---|
| [target-substruct.md](target-substruct.md) | The `target_t` heap struct (main vs sub reticle) driving the gold/blue bars |
| [target-debuffs.md](target-debuffs.md) | Debuffs ON a mob, tracked by SPELL id from 0x028/0x029 — the client stores no mob status list |
| [hate-list.md](hate-list.md) | Which mobs are aggro'd on you or your party: claim scan + 0x028 enmity |

## See also

- [`../README.md`](../README.md) — the game-data index and the rule that governs this folder
- [`../../reverse-engineering/recipe.md`](../../reverse-engineering/recipe.md) — how to find the next offset
