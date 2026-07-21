---
title: Target & Sub-Target Struct
summary: The Ashita target_t heap struct (main vs sub reticle) driving the gold/blue target bars, plus the party-window picker cursor.
source: REFERENCE.md §9e
---
# Target & SUB-target struct  (REVERSED & WORKING, 2026-06-27, retail)

This drives the gold "loupe" bar (main target `<t>`) **and** the ocean-blue bar (sub-target
`<st>`/`<stpc>`). Layout matches **Ashita's `target_t`** (`plugins/sdk/ffxi/target.h`):
two `targetentry_t` (40 bytes / `0x28` each) + a sub-active flag.

> **Probe location note:** this page says several times that a probe is "kept in
> `plugin/aiohud.cpp`". The RE probes were since **moved to `src/plugin/aiohud_probes.cpp`**
> (gitignored, compiled only when present under `/DAIOHUD_PROBES`); only `keylog` / `dbflog` and the
> `#ifdef`-ed call sites remain in `aiohud.cpp`. See
> [architecture/re-probes.md](../architecture/re-probes.md) for the full command list.
>
> **Removed-probe note:** the `//aio tent`, `//aio tdbg` and `//aio pcur` commands referenced below
> have since been **removed** from the source (probes churn — they live in the untracked
> `src/plugin/aiohud_probes.cpp`). The offsets/findings they produced are still valid; to recapture,
> re-add a probe per [architecture/re-probes.md](../architecture/re-probes.md). `//aio tlock`, `//aio tgt`
> and `//aio sub` still exist.

```
*(FFXiMain.dll + 0x57876C)        -> target_t base (HEAP ptr ; ASLR-shifted, resolve at runtime)
  target_t + 0x00  u32  Targets[0].Index       (entity index)
  target_t + 0x04  u32  Targets[0].ServerId    = ACTIVE RETICLE  (sub when <st> open, else main)
  target_t + 0x08  u32  Targets[0].EntityPointer
  target_t + 0x0C  u32  Targets[0].ActorPointer
  target_t + 0x28  u32  Targets[1].Index
  target_t + 0x2C  u32  Targets[1].ServerId    = LOCKED MAIN  (valid while a <st> cursor is up)
  target_t + 0x50  u32  flags ; bit 0x00010000 = sub-target CURSOR OPEN (set only while selecting)
  target_t + 0x5C  u8   LOCK-ON flag = BIT 0 (upper bits carry other target flags)  (2026-07-02, corrected 2026-07-05)
```
`0x04000000` is the "nothing targeted" sentinel for a ServerId. Decode (see `read_target`):
- bit clear → main = `Targets[0].ServerId`, sub = none.
- bit set   → **sub = `Targets[0].ServerId`** (the cursor), **main = `Targets[1].ServerId`** (locked).

**Lock-on (`+0x5C`).** `read_target` also reads `+0x5C` → `TargetInfo.locked` → `GameState.targetLocked`
(the locked id is the reticle `Targets[0]`). The party widget tints the selection hand + frame **red**
when the targeted member is locked (gold = targeted, red = locked, blue = sub). Reversed via `//aio tlock`
(a one-shot `target_t` hexdump; run it un-targeted / targeting a member / locked on it → `+0x5C` is the
**only** byte that flips between "targeting" and "locked"). Probe kept in `plugin/aiohud.cpp`.

> **Correction (2026-07-05).** The lock is **bit 0** of `+0x5C`, not the whole dword. The original probe was
> done on a MOB, where the byte cleanly read `0x00` unlocked / `0x01` locked. But a **PC / party target** carries
> other flags in the upper bits: `//aio tlock` on Kaories read `+0x5C = 0xFD0010F0` un-locked vs `0xFD0010F1`
> locked — only bit 0 changes. So `read_target` masks `lk & 0x01` (a bare `!= 0` false-locked on every ally).

⚠️ Use the `+0x50` bit `0x00010000`, **NOT** the byte at `+0x78`. `+0x78` also goes 1 on
cursor-open but is STICKY: it stays 1 after you *confirm* the action (clears only on cancel),
leaving the sub bar stuck on. The `+0x50` bit clears on **both** confirm and cancel.

**Why not the obvious `FFXiMain+0x487F60`?** That static is only a *thin cache* of the
active reticle's `{index@+0x5C, id@+0x60}` (the value at `+0x58` is an unrelated constant
pointer). It follows the `<st>` cursor too, so it CANNOT tell main from sub, and it holds no
`Targets[1]`/flag. The real two-entry struct is the heap `target_t` above.

**How it was found (probe chain, all via `//aio …`):**
1. `//aio tgt2` — Tab a party member, then scan all memory for that ServerId. The real
   `target_t` is the hit with `Index@-4` small **and** a valid `EntityPointer@+4` **and**
   `Targets[1].ServerId@+0x28` is *another* party id. Then scan for a STATIC (FFXiMain)
   pointer holding that heap base → the `0x57876C` anchor. (Cap the id-scan high: the id
   appears 1000s of times; `target_t` can be past the 1024th hit.)
2. `//aio sub` — per-frame change-watcher over `target_t[0..0x80]`. Open then cancel a
   `<stpc>` cursor: the dword that flips `0↔1` on open/cancel is the flag (`+0x78`).
These probe commands live in `plugin/aiohud.cpp` (kept for re-locating after a client patch).

**Party-window picker cursor (Quartermaster / Lottery / remove member / leader) — WORKING, 2026-06-28.**
This picker is **NOT a target** — `target_t`, the LuaCore target list `*(g+0x30)`, and the targeting
struct all stay `0x04000000` ("nothing") while it's open. It's a pure **menu cursor**: when it's
active the focused-menu pointer `*(FFXiMain+0x5EED6C)` points at the menu named **`"partywin"`**
(name at `def+0x4E`, `def = *(menu+0x04)`), and the hovered member is a **1-based cursor index at
`menu+0x4C`** (`+0x08` = a pointer to the selected row's UI object). The party window lists members
in slot order = our row order, so the hovered member is `rows[index-1]`. `poll_game_state` fills
`GameState::partyMenuSel`; the party widget frames that row. Found with `//aio pcur` (needs ≥2 party
members so the index is non-zero — solo it is `0`, an all-zero byte you can't pick out). The probe
(`g_pcur_probe`) stays in `plugin/aiohud.cpp` for re-locating after a client patch.

## Resolving the target to its ENTITY struct (Target-HUD reverse — REVERSED & WORKING, 2026-07-03)

The Target-HUD needs the target's **name / HP%**, not just its ServerId. `Targets[0]` carries the entity
handle, so we skip the id→index scan entirely — reached **directly** off `target_t`:
```
  target_t + 0x08  u32  Targets[0].EntityPointer   -> the reticle's ENTITY struct  (heap)
```
Confirmed by `//aio tent`: on all three probes `EntityPointer == *(entity_array + Index*4)` (`match=1`), where
`entity_array = *(g+0x24)` — the same array the party uses for member position (`party_state.cpp`: X @+0x04,
Z @+0x0C off the entity pointer). So route (a) `EntityPointer` is the cheap path; (b) `*(entity_array+Index*4)`
is a fallback/cross-check. This is the classic FFXI entity struct:

| Field | Offset | Type | Confirmed by |
|---|---|---|---|
| position X / Z | `+0x04` / `+0x0C` | float | already used by the party (movement detection) |
| Index | `+0x74` | u16 | 0xF3=243 / 0x1AB=427 / 0x542=1346 = the header index |
| **ServerId** | `+0x78` | u32 | 011060F3 / 010E71AB / 000A77AE = the probed sid |
| **Name** | `+0x7C` | char[0x18] | "Vampire Leech" / "Trisvain" / "Gozzi" |
| **movement speed** | `+0x98` | float | the ONLY speed field (full `+0x90..0xAC` scan) — ~4 rest / ~10 run for a mob, gear-static for a PC; see gotcha |
| base/normal speed | `+0x9C` | float | a **CONSTANT `5.0`** (the reference "normal" speed) — not a live value, don't read it |
| **HP%** | `+0xEC` | u8 (0..100) | see below |
| **engaged flag** | `+0x170` | u32 | 0 idle → 1 on engage / in-combat (`//aio tent` A/B) |
| **render flags (pflags)** | `+0x124` | u32 | bit `0x00800000` = the PC is IN A PARTY (game shows the name blue) |
| **claim id** | `+0x188` | u32 | 0 = unclaimed, else the claiming player's server id |
| **SpawnType** | `+0x1D0` | u32 | `0x01` PC / `0x02` NPC / `0x10` Mob |

Reader constants in `game_mem.cpp` (`read_target_entity`): `ENT_INDEX_OFF 0x74` / `ENT_ID_OFF 0x78` /
`ENT_NAME_OFF 0x7C` / `ENT_HPP_OFF 0xEC` / `ENT_STATUS_OFF 0x170` / `ENT_CLAIM_OFF 0x188` /
`ENT_SPAWN_OFF 0x1D0` / `ENT_PFLAGS_OFF 0x124`. Floats (`0x04`/`0x0C`/`0x98`) are `safe_read` as u32 then
bit-copied via `memcpy`. Fills `GameState::target` (`TargetEntity{ id, index, name, hpp, status, claimId,
spawnType, pflags, moveSpeed, posX, posZ, valid }`) once per frame in `poll_game_state`.

**HP% = `+0xEC`, pinned by real low-HP samples.** A *Vampire Leech* read `+0xEC` = **9** at 9% HP and
**0** at 0% HP (dying), while full-HP humanoids read **100**. Crucially `+0xDC` and `+0xE0` stay pinned at
**100 even for the 0%-HP mob**, so they are NOT HP% — `+0xEC` is the only byte that tracks damage 0..100.

**movement speed `+0x98` — the ONLY speed field, but read it DIFFERENTLY per entity type.** A full field
scan of `+0x90..0xAC` proved `+0x98` is the only float that tracks speed; `+0x9C` is a constant `5.0` (the
"normal" reference), everything else is unrelated. How to interpret `+0x98` depends on whether the entity is
a PC or a mob:

- **PC (self or another player)** — `+0x98` is a **STATIC** field = the character's gear capability, so it's
  always shown (no movement gate). `% = 100·(ms/5 − 1)`, base **5.0** — exactly the reference addon
  `addons/SpeedChecker` (`100*(me.movement_speed/5-1)`, which it only ever does for *yourself*). `5.0` = 0%,
  `5.9` = +18% (feet), and **+18% is the gear cap**.
- **MOB** — `+0x98` is a **DYNAMIC** field (~4 idle, ~10 running, lower under Gravity). `% = 100·(ms/10 − 1)`,
  base **10**, and it is **gated on actual movement** (a position delta from `+0x04`/`+0x0C` between frames)
  so a stationary mob reads 0% instead of a false "slow". Relative to the player's base: a running normal mob
  = 0%, a Gravity II mob = −75%.
- **OTHER players' `+0x98` is quantised/estimated** — only YOUR OWN reads exact. A 0-gear PC can read `5.1`
  (+2%) and an 18% PC `6.0` (+20%, impossible from gear). It is **not** a uniform +2 (many read right:
  0/12/18), so the fix (OTHER PCs only) is: **cap at +18% and snap `|%| < 4` to 0**. Self + mobs are untouched.

**PC-vs-mob test = the SpawnType `0x01` bit, `spawnType & 0x01` (NOT `== 0x01`).** Your OWN character's
SpawnType reads `0x20D` (`0x200 | 0x0D`, and `0x0D` contains the `0x01` bit); other PCs read `0x01`. A
**PURE NPC** (`spawnType & 0x11 == 0`, e.g. `0x02` — neither the PC `0x01` bit nor the Mob `0x10` bit) shows
**no speed and no TH**.

*A position-measured "real speed" (stick-throttle) mode was tried and **REMOVED** — measuring speed from the
position delta is inherently imprecise; the static `+0x98` field is the correct method.* See
[target-module.md](../design/target-module.md).

**Type / claim / party (mob·NPC·PC, idle·engaged·claimed) — REVERSED & WORKING, 2026-07-03/04**, all via
the `//aio tent` differential probe (target the same entity in two states, diff the `0x120`-byte block copy):

- **`+0x170` engaged flag (u32)** — clean idle-vs-engaged A/B on one mob: this dword flips `0 → 1` the
  instant you engage / it enters combat, back to `0` idle.
- **`+0x188` claim id (u32)** — the server id of the claiming player, `0` while unclaimed. *Was first
  mis-guessed at `0x18C`*; the same idle-vs-engaged A/B proved `+0x188` is the dword that flips
  `0 → claimer-id`. In `//aio tent` it read `0` idle and the claimer's id engaged.
- **`+0x124` render flags (u32), bit `0x00800000`** (byte `0x126 & 0x80`) — set when the PC is **in a
  party** (the game colours the name blue). Found by diffing an **in-party PC vs a solo PC**.
- **`+0x1D0` SpawnType (u32)** — `0x01` PC / `0x02` NPC / `0x10` Mob. NPC vs Mob differ **HERE, not in the
  ServerId** (both an NPC and a mob are `0x01xxxxxx`). Reversed by diffing an **NPC "Corua" = 0x02** vs a
  **mob "Vampire Leech" = 0x10**. The name-colour treats **anything that is neither a PC (`sp & 0x01`) nor a
  mob (`sp == 0x10`)** as an NPC / object — a door, crystal, etc. — colouring it soft green, so objects with
  a SpawnType other than the exact `0x02` don't fall through to white (see the colour table in
  [target-module.md](../design/target-module.md)).

These drive the Target module's name colour (SpawnType → claim/pflags), its engaged/claim state, and its
speed% gate — the full colour table lives in [target-module.md](../design/target-module.md).

**Probe `//aio tent`** (kept in `plugin/aiohud.cpp`, off by default): resolves `target_t` → `Targets[0]`, logs
`sid / index / eptr / arrRes / match`, then a single SEH-guarded block-copy of the entity (0x120 bytes) dumped
as hex + inline ASCII with two aids (longest ASCII run = NAME; every byte in 1..100 = HP% candidates). No-op
when nothing is targeted. Re-run it to re-locate offsets after a client patch.

**Reader:** `read_target_entity()` lives in `game_mem.cpp` right **after `read_target`**, with the offsets above
as the single source of truth (`T0_EPTR_OFF` / `ENT_*_OFF`). It fills `GameState::target` (`TargetEntity`
{ id, index, name, hpp, valid }) once per frame in `poll_game_state`.

## Does the client store a mob's DEBUFF list? — investigation (probe added 2026-07-03)

The Target module currently packet-tracks debuffs (0x028 action packets) with *approximate* durations.
If the client stored the target's live status list — and a remaining-tick — we could read **real**
debuffs + **exact** expiry straight from memory. The `//aio tent` dump only covered `+0x000..+0x120`
and showed **no** status array there, but a list could sit beyond that, or hang off a pointer.

**Probe `//aio tdbg`** (kept in `plugin/aiohud.cpp`, off by default) resolves the same entity as `//aio
tent` (`target_t → Targets[0].EntityPointer`, array-resolved fallback) and runs three passes over ONE
SEH-guarded `0x400` block copy:
1. **Wide hex dump** `+0x000..+0x400` (hex | ascii) — extends the `0x120` `tent` already covered so an
   inline status array/list beyond it is visible.
2. **Value scan** — logs every offset holding a byte/word equal to a known debuff id (Burn=128, Bind=11,
   Choke=130, Poison=3, Slow=13), with the surrounding bytes (a duration/tick counter often sits adjacent).
3. **Pointer follow** — dereferences every distinct heap-looking dword in the entity, dumps `0x80` behind
   it and value-scans it too (a mob's list may hang off a pointer, not be inline). Deduped + capped.

**Shape to expect:** the local player's OWN buffs live at `player+0x1C` as **32× u16 status ids** (`0xFF`
= empty slot; see `read_player_buffs` in `game_mem.cpp`). A mob list, if it exists, should mirror that —
a run of small u16s with the applied id among them.

**Differential recipe (this is what proves it):**
1. Target a mob with **no debuffs** → `//aio tdbg` (baseline — the id should be ABSENT everywhere).
2. Cast **Burn** (status 128) on it → `//aio tdbg` → a slot that was `0` now reads `128` = the live status
   store. Note its offset + any adjacent value (duration/tick).
3. Wait ~2 min for Burn to wear off (or dispel) → `//aio tdbg` → if that slot returns to `0`, it's the
   confirmed per-target status list; if the adjacent value counted down between steps 2 and 3, that's an
   exact timer. If `128` **never** appears in the entity or any pointer-followed region → the client does
   **not** store mob debuffs, and packet-tracking stays the only option.

**RESULT (2026-07-03): the client does NOT store a mob's debuffs — CONFIRMED by the differential.**
Three `//aio tdbg` runs on the same mob (baseline / Burn applied / worn off), diffed by offset:
- **Burn (128) never appears then disappears** at any stable offset — inline (`+0x000..+0x400`) or behind
  any followed pointer. The `128`/`0x80` hits the value-scan reported are **float bytes** (`3F800000`=1.0f,
  `40800000`=4.0f, …), identical across all three runs = noise, not a status slot.
- The dump WAS live and correct — the one byte that changed meaningfully was **HP% `+0xEC`: 100 → 99 → 80**
  (the mob taking Burn's damage). So we were reading live memory; the debuff simply isn't in it.
Conclusion: the server doesn't send mob debuff state to the client (why every addon packet-tracks). There is
**no memory store and no memory timer** to read → the Target module's **packet-tracked icons** are the only
option. The only remaining accuracy gain is reversing the **wear-off message** packet for EXACT icon removal
(no countdown either way). `//aio tdbg` stays in `aiohud.cpp` for re-checking after a client patch.

## See also
- [Target module — the TargetBar widget & its config](../design/target-module.md)
- [The party member ARRAY in memory](party-array.md)
- [Party cast bar — the 0x028 action packet](cast-bar.md)
- [Target debuffs — detection, wear-off, and mob-behaviour inference](target-debuffs.md)
- [Action-menu info box](action-menu.md)
- [Party visual system — the red locked cursor/frame colours](../design/party-visual-system.md)
