---
title: Zone Tracker — Dynamis / Abyssea / Omen / Nyzul / Sheol / Limbus providers
summary: The six zone providers — 0x055 Dynamis granule bits + timer, 0x02A Abyssea lights/visitant, the slot-9 rendered-text Omen and Nyzul parsers, Sheol Mog Segments (0x034 + 0x02A 40016), and the generic 0x075 battlefield timer/bars packet with its FFXiMain+0x480800 mirror; the Limbus provider itself has its own page (limbus.md).
source: model/party_state_zonetracker.cpp (zt_set_zone/on_55/on_2a/on_omen_text/on_nyzul_text/on_034/on_00e/on_118/on_limbus_075), plugin/aiohud.cpp (routing), plugin/aiohud_probes.cpp (calibration commands)
---
# Zone Tracker — Dynamis / Abyssea / Omen / Nyzul / Sheol / Limbus providers

A dedicated box = the PointWatch **zone providers** ported from `pwcore.lua`. State
lives in `PartyState::zt_`. `zt_set_zone(zone, zone_name(zone))` is called each
frame from hud.cpp (after `refresh_hate`) and picks one of **six** modes — the first
two by zone **name**, the rest by zone **id**. Transitions reset the relevant state.

| mode | zone | detected by |
|---|---|---|
| **1** Dynamis | — | name prefix `"Dyn…"` |
| **2** Abyssea | — | name prefix `"Aby…"` |
| **3** Omen | 292 (Reisenjima Henge) | id |
| **4** Nyzul Isle | 77 | id |
| **5** Sheol / Odyssey | 298 / 279 | id **and** `oldZone == 247` (Rabao) — 298/279 are also Selbina HTMBs |
| **6** Limbus | 38 Apollyon / 37 Temenos | id |

The config's `Content` selector writes **`ztVariant` 0..5** (0 Dynamis, 1 Abyssea, 2 Omen,
3 Nyzul, 4 Sheol, 5 Limbus — default 1). It selects the **edit/preview zone** and filters which
options the panel shows; **it is off by one from `zt_.mode`** (1..6) and must not be conflated
with it. The Help tab can force a variant via `variantOverride` in `hud_zonetracker.cpp`.

## Dynamis (mode 1) — run timer + 5 granule Key Items
- KIs = ids **1545-1549** (Crimson / Azure / Amber / Alabaster / Obsidian), all
  **Type 3, bits 9-13** of the 0x055 "key item available" bitfield.
- **0x055** — `on_55`: guard `pkt_u32(p, 0x84) == 3` (the KI Type), then
  `ki[i] = (p[0x04 + bit/8] >> (bit%8)) & 1` for `bit = 9+i`.
- Timer = **3600 s + Σ owned-KI extensions** (minutes×60), `zt_recompute_dyn_limit`:
  - **city** zones `{185, 186, 187, 188}` use `{10, 10, 10, 15, 15}` minutes,
  - **other** zones `{134, 135, 39, 40, 41, 42}` use `{10, 10, 10, 10, 20}`.
- Entry time = `GetTickCount()` on zone-in. **GAP:** if the plugin loads mid-run
  the entry time is unknown (timer starts at full) — same as the addon.

## Abyssea (mode 2) — visitant timer + 7 lights
From the **0x02A** zone messages (`on_2a`). Params @0x08/0x0C/0x10/0x14 (`p1`..`p4`).
```
rel = (MessageID @0x1A & 0x3FFF) − abyOffset
abyOffset = 7238 for zones 215/253, else 7338
```
> The client message base drifted **+23** from the addon's old ids (7315 → 7338),
> CALIBRATED LIVE via `//aio abylog` (the two /heal bulk reports landed at rel 0/1 =
> mid 7338/7339, visitant at rel 9/10 = mid 7348). Re-run `//aio abylog` (kill a mob
> + /heal + get visitant) to re-pin after a client patch. CONFIRMED LIVE: gaining 8
> ruby fired mid 7526 = rel 188 → +8. ✓

Lights are stored/displayed in order **[0]Pearl [1]Azure [2]Ruby [3]Amber [4]Gold
[5]Silver [6]Ebon** (caps **230 / 255 / 255 / 255 / 200 / 200 / 200**).

| rel | meaning |
|---|---|
| 0 | /heal bulk: pearl=p1, ebon=p2, gold=p3, silver=p4 (EXACT totals) |
| 1 | /heal bulk: azure=p1, ruby=p2, amber=p3 (EXACT totals) |
| 183 | pearlescent +5 (cap 230) |
| 184 | golden +5·(p1+1) (cap 200) |
| 185 | silvery +5·(p1+1) (cap 200) |
| 186 | ebon +p1+1 (cap 200) |
| 187 | azure +8 (cap 255) |
| 188 | ruby +8 (cap 255) |
| 189 | amber +8 (cap 255) |
| 9 / 10 / 45 | visitant time set (= p1 minutes) |
| 12 | visitant extend (+= p1 minutes) |

**No-visitant EXPULSION timer:** on Abyssea entry (before you talk to the NPC) the
timer starts at **5 minutes** (`visitantMin = 5` in `zt_set_zone`, matching pwcore's
`abyssea.time_remaining = 5`); a real visitant message then overwrites it.

## Omen (mode 3, zone 292) — rendered-text objectives
The objective text is NOT in any parseable packet (0x02A/0x017/0x027 all checked;
0x027 "String Message" type 5 carries it but its msgids are too scattered to map).
Instead the Zone Tracker consumes the **client-rendered chat text via the plugin's
[incoming-text ABI callback — vtable slot 9](../reference/plugin-abi.md)**.

**GOTCHA:** the Omen chat mode is **673 = `161 | 0x200`** (Windower masks the 0x200
flag). Match `(*mode & 0x1FF) == 161`. `aio_plugin_text_in` routes matching lines to
`on_omen_text`.

`on_omen_text` ports the Omen addon's 14 objective patterns (Braden/Sechs
`addons/Omen/Omen.lua`):
- An objective line is `"N: …"` → slot `N` (1-10). The objective **type** is matched
  by a distinctive tail (ordered specific-first to disambiguate "magic burst" /
  "weapon skill" overlaps); the **number** wanted is the digits preceding a keyword
  starting with that type's *check letter* (`omen_num_before`, skipping the leading
  "N:"). `init` line = requirement (`req`); `"You have …"` = current (`cur`);
  `"You have failed"` is ignored.
- The 14 short names (`OMEN_SHORT`): WS Damage, MB Damage, Non-MB Nuke, Melee Round,
  Kills, Critical Hits, Abilities, Spells, Magic Bursts, Skillchains, All WS,
  Physical WS, Magic WS, 500 HP Cures.
- Specials: `"N seconds remaining"` = bonus timer, `"N omen"` = omen count,
  `"spectral light flares up"` = cleared, `"Vanquish …"` / `"treasure portent"` =
  floor objective, `"light shall come …"` = Free Floor.

State in `ZoneTracker`: `omen[10] {type, cur, req}`, `omens`, `omenBonusSec/Ms`,
`floorObj`. Text may carry trailing control bytes — parse tolerantly.

## Nyzul (mode 4, zone 77) and Sheol / Odyssey (mode 5, zones 298 / 279) — SHIPPED

These are **no longer "to port" — both are implemented** in `model/party_state_zonetracker.cpp`
(this section is a stub; the detail below is what the code establishes, and deserves its own
expansion).

- **Nyzul Isle**, zone **77**. Floor / timer / objective / restriction tracker + token estimator,
  ported 1:1 from the **NyzulHelper** addon (Glarin). Objectives arrive on the **slot-9 rendered
  text** channel (`on_nyzul_text`), e.g. `"Time limit has been reduced"` → subtract `n*60` from
  `nyzul_remaining()`. Exiting **77 → 72** (staging) keeps the run but zeroes the timer + armband,
  matching the addon; any other exit resets it. Config key `ztnyzul=` (per-row toggles: floor /
  time / objective / restriction / completion / rate / tokens).
- **Sheol A/B/C (Odyssey)**, zones **298 / 279** — but **only when entered from Rabao (247)**,
  because 298/279 are *also* the Selbina HTMB zones. The Sheol choice (A/B/C) comes from **packet
  0x034** (NPC interaction, the Rabao conflux entry menu: `Menu Parameters[0]` = 1/2/3).
  Mog-Segment totals come from 0x02A **msg 40016**, and the first one of a run **self-baselines**
  the counter (`segBase = p2 - p1` = the banked total before the first kill) — the same
  derive-from-an-absolute rule as `limbusRunUnits`. Leaving Sheol → Rabao **freezes** the run total
  as *"N (last run)"* (the addon's *conserve*) and clears A/B/C for the next run. Config keys
  `ztsheol=` (segments / resistances / cruel joke) and `ztsheol2=` (family row / weapon-icon size /
  element-puck size).

## Limbus (mode 6, zones 38 Apollyon / 37 Temenos)

Detected by **zone id** in `zt_set_zone`: `zone == 38` (Apollyon) or `zone == 37`
(Temenos, the sibling Limbus zone). Entering resets `limbusArea/Level/Progress/Floor`.

**Limbus has its own page — [limbus.md](limbus.md)**: the 0x075 bar labels per wing, the
0x02A run economy (wing-keyed award ids, the source entity, the weekly allowance), the
tower/quadrant tables, the coffer row, and the probes. The rest of *this* section documents
the **generic battlefield packet** it rides on, which also serves Odyssey, Dynamis D and Unity.

## 0x075 is the generic BATTLEFIELD timer/bars packet (not a "Limbus menu")

Reversed **statically** (2026-07-18) from `re/ffximain_dump.bin`. **The image base in
that dump is `0x05C60000`, NOT `0x10000000`** — pinned by cross-checking the incoming
0x078 handler against its own jump table. Subtract `0x05C60000` from any absolute in
that dump to get an RVA.

The incoming-packet dispatcher is `FFXiMain+0xFA6C5`:
```
mov cx, word ptr [edi]                       ; packet header word
and ecx, 0x1ff                               ; id
mov eax, [edx + ecx*4 + 0x44730]             ; handler table
call eax
```
Handlers are registered by `register_incoming(id, fn)` (`FFXiMain+0xFC270`, thunk
`+0xFC2D0`). Scanning every `push fn / push id / call` pair gives **id 0x075 ->
handler `FFXiMain+0x98DC0`**, which decodes the packet exactly as Windower's
`addons/libs/packets/fields.lua` documents it (`Type` bitflag at +0x24, timer at
+0x08/0x0C/0x10, bars at +0x28). So **the "Apollyon_Lv119" string AioHUD reads at
+0x2C is Bar 0's 16-char label**, not a bespoke Limbus field.

`test byte [esi+0x24],2` (bars flag) -> `lea ecx,[esi+0x28]; call FFXiMain+0x87860`,
which does `rep movsd` of **0x20 dwords** from packet+0x28 into a static global.

### CONFIRMED (static): the battlefield block at `FFXiMain.dll+0x480800`

| RVA | type | field |
|---|---|---|
| +0x480800 | u32 | Fight Designation (0 = no battlefield; writing 0 also clears the flags) |
| +0x480804 | u32 | Timestamp Offset (packet +0x08) |
| +0x480808 | u32 | Fight Duration (packet +0x0C) |
| +0x48080C / +0x480810 / +0x480814 | u32 | packet +0x10 / +0x14 / +0x18 |
| +0x480818 / +0x48081C | u32 | Battlefield / Render radius (yalms x1000) |
| +0x480820 / +0x480822 | u16 | packet +0xA8 / +0xAA |
| **+0x480824** | u8 | **flags** : b0 timer active, b1 **bars active**, b2 +0x20/22 valid, b3 +0x26 valid |
| +0x480825 / +0x480826 | u8 | packet +0x25 / +0x26 |
| **+0x480828** | bar[6], stride **0x14** | `{ s32 progress ; char label[16] }` |
| +0x4808A8 | u8 | redraw/dirty flags |

Setters: `+0x877E0` (designation), `+0x87800` (timer), `+0x87860` (bars),
`+0x878C0` (radii). Accessor `get_bars()` = `+0x87690` — returns `0x480828` if the
bars flag is set, else NULL.

**Progress semantics** — from the renderer `FFXiMain+0x21F9D0`:
```
call get_bars ; esi = bar[0]
add esi, 4                        ; -> label
mov dword [esp+0x10], 6           ; SIX bars (fields.lua documents only five)
loop:
  cmp byte [esi], bl              ; label[0]==0 -> skip this bar
  je   next
  fild dword ptr [esi-4]          ; progress read as a SIGNED 32-BIT INT
  fmul dword ptr [0x5F89A18]      ; = 0.01f
  ...
next:
  add edi, 0x10
  add esi, 0x14                   ; stride 0x14
```
So progress is a **percentage 0..100** (a signed int, `x * 0.01f` = bar fraction), not
a kill count and not a current/max pair. `0xFF`/`0xFFFFF7` "inactive" in fields.lua is
just this int going **negative** (`FF F7 FF FF` = -2049). A bar is drawn only when
`label[0] != 0`. There are **6** bars: +0x28, +0x3C, +0x50, +0x64, +0x78, +0x8C —
fields.lua's trailing `_unknown5 data[32]` swallows bar 5.

So: `limbusProgress = *(s32*)(FFXiMain + 0x480828)` (bar 0, the one labelled
`Apollyon_Lv<NNN>`), valid while `(*(u8*)(FFXiMain+0x480824) & 2)`.

### CONFIRMED live (2026-07-18) — which bar carries what

`//aio limbusmem` in an Apollyon run printed the block verbatim:

```
bar0  progress=-1          label="Apollyon_Lv119"    <- area + level (progress INACTIVE: label carrier only)
bar1  progress=96 -> 0     label="SW_Floor_#3"       <- THE FLOOR *and* THE GAUGE
bar2  progress=0           label="Uniq_Data0"
bar3..5 progress=7FFFFFFF  label=""                  <- unfilled
```

So the earlier hypothesis was right in shape but wrong in index: it is **bar 1**, not
bar 0, that carries both the floor label and the on-screen gauge. Bar 0 is a pure
label carrier (`progress = -1`).

**We therefore read everything from the PACKET, not from memory.** Bar *n* sits at
`+0x28 + n*0x14` of incoming 0x075, so `on_limbus_075` parses all six bars and matches
them **by label content**, never by index — the per-wing label forms are tabulated in
[limbus.md](limbus.md). Matching by content survives the server reordering the bars, and a
0x075 sent by something else (it is multiplexed) matches no label and touches nothing.

Observed gauge behaviour: it fills in steps and **resets to 0 several times within one
floor** (0→96 twice on `SW_Floor_#4`, steps of 16; steps of 20 on `NE_Floor_#1`). So it
is not "floor completion" — but that does not matter: the decompiled renderer multiplies
this exact value by `0.01f` to draw the game's own gauge, so mirroring it is correct by
construction whatever it counts.

## Limbus run economy — packet 0x02A

**Moved to [limbus.md](limbus.md)** (2026-07-19). The award ids, the coffer-vs-point-of-interest
discrimination, the tower tables, the coffer row and the probes all live there — several of the
statements that used to sit here were Apollyon-only and are now corrected on that page. Two findings
of general interest stay:

1. **0x118 never fires mid-run.** Not one in a whole armed run — and there is no static holding the
   totals either ([limbus-currency-no-static](limbus-currency-no-static.md)). The 0x02A award's `p3`
   *is* the live total, so the HUD takes it from there and 0x118 is only the zone-entry seed
   (`+0x98` Temenos / `+0x9C` Apollyon, the same packet as Mog Segments `+0x8C`).
2. A run total derived as `total - baseline`, **never** by summing per-event gains — same reason as
   `segments`/`segBase` for Sheol: a duplicated packet cannot double-count and a dropped one cannot
   under-count.

The coffer chips are persisted in their **own** per-character file (`data\cache\limbus_%08X.bin`),
deliberately *not* in the zone cache — a single file shared by every tracked mode and only restored
when `curZone` matches, so one Dynamis run between two Limbus runs would wipe it. See
[per-character caches](../architecture/per-character-caches.md).

### The client holds NOTHING beyond this block — verified with Ghidra (2026-07-19)

`FindImmRange 060E0800 060E0830` on the fully-analysed dump returns a complete **getter/setter
family**, one small accessor per field, and the ONLY writer of the bar array is `FUN_05ce7860`
(RVA `0x87860`) — the 0x075 handler's `rep movsd`. The block is therefore mapped end to end:

| Offset | Accessor | Note |
|---|---|---|
| `+0x00` | `FUN_05ce75c0` | designation (`0xFFFF`/`65535` seen in Limbus and Odyssey) |
| `+0x04` / `+0x08` | `FUN_05ce7630` / `7650` | timestamp offset / **fight duration** — the run timer |
| `+0x0C` | `FUN_05ce7670` | |
| `+0x10` / `+0x14` | `FUN_05ce76a0` / `76c0` | read with **`FILD`** : integers consumed as floats (radii, yalm x1000) |
| `+0x18` / `+0x1C` | `FUN_05ce76e0` / `7720` | |
| `+0x20` / `+0x22` | `FUN_05ce7780` / `77a0` | dword / word |
| `+0x24` | `FUN_05ce75e0`, `7600`, `7610` | flags (bit0 timer, bit1 bars) — read AND written |
| `+0x25` / `+0x26` | `FUN_05ce7760` / `77c0` | |
| `+0x28` | `FUN_05ce7860` (writer) | the six bars, stride `0x14` |

**Consequence — a closed question, not an open one.** There is no separate Limbus instance struct
in the client. What packet 0x075 carries is everything the client knows. So *"what does the Limbus
gauge count?"* — it fills and resets several times within one floor (0 → 96 twice on `SW_Floor_#4`,
steps of 16, then steps of 20 on `NE_Floor_#1`) — **cannot be answered from the binary**. The server
computes the percentage and sends it; the client only draws it (`x 0.01f`). Answering it would take
in-game correlation, and no memory scan can shortcut that. Stop looking in the client.

The same block serves **every instanced content**: an Odyssey Sheol B run on 2026-07-19 showed
`flags=03` (timer + bars), `duration=1797` (~30 min) and `bar0 label="Izzat"`. One exact,
server-driven timer is therefore implementable once for Limbus, Odyssey, Dynamis D and Unity —
unlike the Dynamis/Abyssea timers, which start from plugin load and are wrong if you load mid-run.

### Dev probes (gitignored, never shipped)

The Limbus-specific probes are listed in [limbus.md](limbus.md); the generic one is
`//aio memsnap` / `memdiff` / `memlist` / `memreset`, the differential scanner below.

**Gitignored means absent from a release** — `src/plugin/aiohud_probes.*` is not tracked by git, so
a tester cannot run any of them. See [release checklist](../architecture/release-checklist.md).

### The differential scanner (and why it failed here)

`//aio memsnap` stores **one 32-bit hash per 64-byte block** (16× smaller, so all ~1.1 GB
of writable memory fits and coverage is complete); `//aio memdiff <+1|inc|dec|chg|same|=N>`
intersects the surviving candidates, auto-promoting blocks to dword candidates once few
enough remain. Lessons paid for in this session:

- A raw byte snapshot covered barely a quarter of the heap — the target may never be in
  the candidate set. **Always read the `MEMSNAP:` coverage line before farming.**
- `inc`-only narrowing plateaus forever on free-running counters (clocks, driver and
  plugin counters). The discriminator is the **negative** filter: `same` while idle.
- Even done right it converged onto a combat-stats block near `FFXiMain+0x4804E4`
  (cumulative damage), not the target — because the answer was in a packet all along.
  **Reach for the packet docs (`Windower/addons/libs/packets/fields.lua`) and a static
  read of the binary before brute-forcing memory.**

## Widget & config
`Hud::draw_zonetracker` (hud.cpp) — dark box + gold stroke, header (zone name) +
time bar (MM:SS, green→red ramp `zt_time_col`) + body: Dynamis = 5 KI rows;
Abyssea = 7 vertical light bars; Omen = a dedicated mode-3 branch (floor / bonus
timer / objective rows). Center-anchored (`ztX` centre, `ztY` top),
`EDITBOX_ZONETRACKER`. Packets routed in aiohud.cpp: **0x02A→on_2a, 0x055→on_55,
0x075→on_limbus_075, 0x118→on_118, 0x034→on_034** (Rabao conflux menu → which Sheol) **and
0x00E→on_00e**. The module's own code lives in `model/party_state_zonetracker.cpp` (split out of
`party_state.cpp`); the widget is `ui/hud_zonetracker.cpp`.

Config module "Zone Tracker" (`zt_config.cpp`) — **everything is per-variant**. The
`Content` selector (second row, right under `Show` — the one panel where `Size` is not
the row after `Show`, because `Content` drives what the rest of the panel even shows)
picks the zone; the sub-options and the Text element list below are **filtered to that
zone**. Each variant has a visibility toggle per row, a text element per visual group
(font / size / outline / bold / colour), and size sliders for its bars, dots and icons.

Persisted as `zt=` / `ztText%d=` / `ztsheol=` plus one key per variant:
`ztdyn= ztaby= ztomen= ztnyzul= ztsheol2= ztlimbus=`. `zt=`, `ztsheol=` and `ztText%d=` stay on the
main chain; **every per-variant key is parsed in `parse_zt_line`**, deliberately OUT-OF-LINE: the
main `else if` chain in `load_config_from` sits at MSVC's
nesting limit and one more branch fails the build with **C1061** (same reason as
`parse_ep_line` / `parse_mm_line`). Short `sscanf` back-compat means an older config
loads with every row shown and every factor at 1.00.

Migration note: outside Limbus the rows no longer read `ZT_BODY` — each reads its own
element — so a user who had customised "Body" sees those rows fall back to defaults once.

**Calibration commands:** `//aio abylog` (dump next 40 0x02A), `//aio omenlog`
(next 24 0x017), `//aio pktall` (next 250 packet ids), `//aio textlog` (next 30
incoming-text lines with mode).

## See also
- [Limbus tracker](limbus.md) — mode 6 in full: the per-wing 0x075 labels, the 0x02A run economy, the towers and the coffer row.
- [Limbus currencies have no static](limbus-currency-no-static.md) — the negative Ghidra result behind "0x118 is the only seed".
- [Per-character caches](../architecture/per-character-caches.md) — how `zone_%08X.bin` / `limbus_%08X.bin` are keyed and when they are (not) written.
- [Release checklist](../architecture/release-checklist.md) — why none of the probes on this page exist on a tester's machine.
- [Plugin ABI — incoming text (slot 9)](../reference/plugin-abi.md) — the Omen text channel + mode-mask gotcha.
- [Data channels](../architecture/data-channels.md) — the three ways this plugin gets game data.
- [Reverse-engineering recipe](../architecture/reverse-engineering-recipe.md) — the general method this page's findings produced (channels in cost order + the masked-id lesson).
- [Static analysis of FFXiMain](../architecture/re-static-analysis.md) — the dump, its **0x05C60000** base, and how to re-pin it.
- [The differential memory scanner](../architecture/re-memory-scanner.md) — the generalized protocol + failure modes behind the post-mortem above.
- [The in-game RE probe toolkit](../architecture/re-probes.md) — every `//aio` diagnostic, including `limbusrun`/`limbusmem`.
- [Config panels](../architecture/config-panels.md) — the `Content`-selector pattern and the C1061 out-of-line parser rule.
- [PointWatch](pointwatch.md) — the same `pwcore.lua` engine (the core XP/CP/ML providers).
- [Party cast bar — 0x028](cast-bar.md) — the packet-bit-read pattern.
