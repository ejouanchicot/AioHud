---
title: The chat log — Windower text modes and the client's control bytes
summary: How a log line reaches AioHUD (text_in, its own thread), the mode -> channel table the Chat module uses, and the bytes FFXI embeds in a line (colours, breaks, auto-translate, Shift-JIS) with what each decodes to.
source: model/chat_log.{h,cpp} (decode, classify, history) ; ui/hud_chat.cpp (the two boxes) ; tests/t_chat.cpp ; Windower res/chat.lua
---
# The chat log

> **Sondes citees ici.** Les commandes `//aio` nommees dans cette page ne sont pas toutes livrees.
> `chatmodes` : sonde de developpement (`src/plugin/aiohud_probes.cpp`, non suivi par git) — cela repond sur la machine de dev, **jamais dans une release**.
> Ce qu'une release porte vraiment : `//aio help` (et `//aio doctor` en premier quand quelque chose ne va pas).

## 1. How a line arrives

Windower calls the plugin's `text_in(original, modified, &mode)` for EVERY line the client writes to its log, on
its **own thread** (measured 2026-07-20 : tid differs from render / packets). `modified` is the line after the
plugins before us ; an empty `modified` = a plugin blocked it, the log will not show it. `mode` carries a 0x200 flag
from Windower : mask `& 0x1FF`.

The Chat module only **copies** the line there (`chat_queue`, a 256-slot single-producer / single-consumer ring) ;
`chat_drain` decodes and classifies it on the main thread, once per frame, into a 1024-line history.

## 2. Mode -> channel

From Windower `res/chat.lua` (incoming / outgoing ids) :

| channel | incoming | outgoing |
|---|---|---|
| say | 9 | 1 |
| shout | 10 | 2 |
| yell | 11 | 3 |
| tell | 12 | 4 |
| party | 13 | 5 |
| linkshell | 14 | 6 |
| emote | 15 | 7 |
| linkshell 2 | 214 | 213 |
| unity | 212 | 211 |
| assist (J / E) | 220 / 222 | 219 / 221 |
| system message | 200 | -- |

**Not in that file, PROVISIONAL** (to confirm with the dev probe `//aio chatmodes`, which logs every mode seen with
a sample line) : the combat log = modes 19..120 ; NPC dialogue = 150 / 151 / 152 / 142 / 144 ; everything else is
filed under System. Known from other modules : 161 = Omen objectives, 123 / 146 / 148 = Nyzul lines.

## 3. Bytes inside a line

| bytes | meaning | decoded to |
|---|---|---|
| `1E n` | colour by palette index ; `1E 01` = back to the line's colour | per-char colour code n |
| `1F n` | colour of chat mode n (names, emphasis) | per-char code `0x80 | channel(n)` |
| `7F n` | marker + one code byte (Omen lines end `7F 31`) | dropped |
| `EF 27` / `EF 28` | auto-translate brackets | `{` / `}` |
| `FD tt ll ii ii FD` | an auto-translate phrase by id | `{...}` (not resolved yet) |
| `07`, CR, LF | a break inside one message (NPC dialogue) | separate history lines, `cont = 1` |
| Shift-JIS pair | full-width punctuation, the battle arrow `81 A8` | ASCII (`->` ...) or `?` |

All of it is locked by `tests/t_chat.cpp`.
