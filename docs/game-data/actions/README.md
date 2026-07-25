---
title: game-data / actions
summary: The 0x028 action packet and what is derived from it. Read `action-packet.md` first — almost everything else here parses it.
---

# Les actions

The 0x028 action packet and what is derived from it. Read `action-packet.md` first — almost everything else here parses it.

| Page | Ce qu'elle contient |
|---|---|
| [action-packet.md](action-packet.md) | **The 0x028 bit layout**, reversed from the client's own parser. The variable-stride target walk lives here |
| [action-menu.md](action-menu.md) | Zero-tap menu identification via the def-name string, examine caches, the ghost problem |
| [skillchains.md](skillchains.md) | Detecting a skillchain OPEN (step-1) / CLOSE (step+1) and the property tables |

## See also

- [`../README.md`](../README.md) — the game-data index and the rule that governs this folder
- [`../../reverse-engineering/recipe.md`](../../reverse-engineering/recipe.md) — how to find the next offset
