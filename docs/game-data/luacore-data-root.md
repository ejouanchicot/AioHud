---
title: LuaCore's data root — the one address everything hangs off, and why it is no longer a constant
summary: Windower updates itself silently, and 4.7.9.3 slid LuaCore's data root by 0x2020 — which blanked the entire HUD while the config panel kept drawing. How the root is now derived from LuaCore's own code, and how to re-derive it by hand.
---

# LuaCore's data root

Every game read in AioHUD starts at one pointer:

```
g = *(LuaCore.dll + <root rva>)      // aio::data_root(), src/model/game_mem.cpp
```

`g` is the only anchor `model/` has for the player (`*(g+0x3C)`), the party array (`*(g+0x248)`), the
entity array (`*(g+0x24)`), the zone info (`*(g+0x40)`), items, key items and equipment. If that one RVA is
wrong, **every** read returns 0 at once.

## What happened on 2026-08-16

Windower updated itself to **4.7.9.3** (Hook, LuaCore 2.6.8.4, FFXIDB, Timers all rewritten the same
minute). LuaCore's data root moved:

| Windower | root RVA |
|---|---|
| ≤ 4.7.9.0 | `0x1C8400` |
| 4.7.9.3 (LuaCore 2.6.8.4) | `0x1CA420` |

`+0x2020`. The plugin kept loading, kept getting a valid D3D8 device, kept being called every frame — and
drew nothing, because `read_player` failed, `inGame` stayed false and `hud.cpp` hides every box until it is
true. The config panel does **not** hang off `inGame`, so it kept rendering and reported *"not logged in"*.
That combination — config fine, HUD gone — is the signature of this failure and nothing else.

The reason it was not anticipated is written at the top of [`ffximain-statics.md`](ffximain-statics.md): the
two anchors were classified as "fragile" (FFXiMain, recompiled at every **game** patch) and "stable"
(LuaCore, *"a game patch does not touch it"*). True — and it hid the other axis. LuaCore is recompiled by
the **Windower** team, and Windower updates itself for every user, without asking. The stable anchor was
only stable against the risk we had thought of.

## How it is resolved now

`model/luacore_root.cpp` asks LuaCore's own code where the root is, instead of remembering an address.

The bindings load the global and **immediately** dereference it at an offset we already know. `get_party`:

```
A1 20 A4 1C 10        mov eax,[101CA420]     ; the root
8B 88 48 02 00 00     mov ecx,[eax+0x248]    ; the party array
6B C7 7C              imul eax,edi,0x7C      ; stride 0x7C, the documented member size
...  F7 E7 C1 EA 02   ; i % 6  ->  "p" / "a1" / "a2"
```

So the scan walks `.text` for `mov r32,[imm32]` followed by `mov r32,[<same reg>+disp]` where `disp` is one
of the root offsets (`0x248 0x3C 0x40 0x24 0x50 0x4C 0x5C 0x54 0x58`), and counts hits per global. On
LuaCore 2.6.8.4 exactly **one** global matches — 12 times — and the outgoing constant `0x1C8400` matches
**zero** times. Adoption needs ≥5 hits *and* more than twice the runner-up: a tie means we recognised
nothing, and saying so beats picking one.

Properties that matter:

- It reads the **mapped image**, not live game data, so it works at the login screen, with no character, and
  gives the same answer on every machine running that Windower build.
- It is deterministic, so it runs **once** per session and its result is final. `data_root()` is on the path
  of every single game read — a scan that re-ran on failure would walk 1.4 MB of `.text` per read.
- The shipped RVAs (`SEED_RVA`) are a fallback for the day a compiler stops emitting that idiom. A seed is
  only adopted once it *reads back* as a live root; otherwise it stays an unproven guess and says so.

`//aio doctor` prints the root, where it came from, and whether it is live — and when it is dead it names
the remedy, because "the plugin sees no character" and "Windower moved the root" need different actions.

## The recast block moved too — and it *lied* instead of dying

The same update slid three contiguous pointers by `+4`:

| table | ≤ 4.7.9.0 | 4.7.9.3 | shape |
|---|---|---|---|
| JA recast timers | `0x22C` | **`0x230`** | `int32[32]`, remaining in 1/60 s |
| JA recast ids | `0x230` | **`0x234`** | stride-8 entries, `byte[0]` = recast_id |
| Spell recasts | `0x234` | **`0x238`** | `ushort[1024]`, indexed by recast_id |

This one is worth more than a table, because it failed *differently* from the root. A wrong root reads 0
and everything goes quiet. A wrong recast offset reads the **neighbouring table** — so `spell_recast_sec`
was indexing the ability-ids table as if it were `ushort[1024]` and reported a screenful of spells all on
the same timer. Reported as *"recast affiche plein de magie avec un timer fixe"*.

The existing guard could not catch it, and it is instructive why:

```c
if (v == 0 || v > 60u * 7200u) return 0;      // "ready, or garbage (>2h)"
```

`v` is a `ushort`, so it maxes out at 65535 — `60*7200` is 432000. **The garbage branch was unreachable.**
A bound that sits outside the type it guards is not a check, and it read like one for months.

Both offsets are now derived, from the two bindings that use them, and each one is only accepted if the
other agrees (`spells == ids + 4` — the only layout either binding describes):

```
get_spell_recasts   @ rva 071950      get_ability_recasts @ rva 0719D0
  A1 20 A4 1C 10   mov eax,[root]       8B 0D 20 A4 1C 10  mov ecx,[root]
  ...4 unrelated instructions...        8B 81 34 02 00 00  mov eax,[ecx+0x234]
  8B 80 38 02 00 00 mov eax,[eax+0x238] 0F B6 14 F8        movzx edx,byte [eax+edi*8]
  8D 14 70          lea edx,[eax+esi*2]                    ^ stride 8 -> the ids table
  81 FE 00 04 00 00 cmp esi,0x400
                    ^ 1024 entries -> the spell array
```

Note the four unrelated instructions between the load and the deref in `get_spell_recasts`: the first
version of this scan required them to be adjacent, matched **nothing**, and still looked healthy because
the fallback seed happened to be the right number. A derivation that silently degrades to a constant is
worse than a constant — it claims to be self-healing. That is what `lc_recast_how()` reporting `"seed"` vs
`"code scan"` exists to expose, and what the offline test asserts.

## Re-deriving it by hand

If the scan ever fails, `scripts/` is not needed — the same derivation is a short PE walk over
`plugins\LuaCore.dll`: find every `A1 imm32` / `8B 05|0D|15|1D|2D|35|3D imm32` in `.text` whose next
instruction is `8B` with `mod=01/10` on the same register and a displacement of `0x248` or `0x3C`, then take
the immediate that dominates. The winner is the root; subtract the image base (`0x10000000`) for the RVA.

## Related

- [FFXiMain statics](ffximain-statics.md) — the *other* anchor, and the healers for the addresses a **game**
  patch moves. The two failure modes look nothing alike: a client patch kills a handful of features while
  everything else works; a Windower patch kills everything at once.
- [Core offsets verified against LuaCore](luacore-verified-offsets.md) — where the `+0x248` / `0x7C` /
  `%6` facts used by the signature above were established.
