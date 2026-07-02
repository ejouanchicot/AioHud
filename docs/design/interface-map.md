---
title: Interface map — everything the UI shows and how it connects
summary: A complete, outsider-readable inventory of the AioHUD interface — the live HUD (party/alliance/cost/cursor), the config overlay (Configuration/Profile/Help tabs), edit mode, the zones/Rules system, profiles, persistence and every //aio command — plus how they all wire together. Written as the starting point for a clean redesign.
source: reversed from src/ (party.cpp, hud.cpp, config_page.cpp, ui_config.*, aiohud.cpp), 2026-07-02
---

# AioHUD — complete interface map

> Read this first if you want to understand **who does what, on what, on whom** across the whole
> plugin, without reading the code. It is a functional inventory, not a design spec. The last
> section lists observations for the upcoming redesign.

AioHUD has **two surfaces**:

1. **The live HUD** — always on, drawn over the game, replaces FFXI's native Party/Alliance windows.
2. **The config overlay** — a full-screen panel you open with `//aio config` to change everything.

A third "surface" is **edit mode** (`//aio edit`): the config panel hides and you arrange the live
boxes directly on the game screen.

One architectural fact that explains a lot: **there is a single widget class, `Party`, instantiated
three times** — tier 0 = your Party, tier 1 = Alliance 1, tier 2 = Alliance 2. They share all
rendering code; `tier` is the only thing that differs. Everything is drawn by hand in Direct3D 8
(colour quads + one shared font atlas), inside one save/restore render-state block per frame.

---

## PART A — The live HUD

### A.1 The four boxes and how they stack

All four boxes share **one right edge** and stack bottom→top. The Party box is the anchor.

```
        ┌───────────────────────────┐
        │     Alliance 2 (tier 2)   │   always 6 rows, no buffs, no cost box
        ├───────────────────────────┤
        │     Alliance 1 (tier 1)   │   always 6 rows, no buffs, no cost box
        ├───────────────────────────┤
        │     Cost / Next box       │   reserved slot (tier 0 only)
        ├───────────────────────────┤
        │     Party (tier 0)        │   up to 6 rows + buff strip + cursor
        └───────────────────────────┘ ◄── bottom-right corner = the anchor (pinned)
```

| Box | Tier | Shows | Position rule |
|---|---|---|---|
| **Party** | 0 | up to 6 member rows (you are always row 0), the buff strip, the selection cursor | **Bottom-right anchored.** Grows **upward** as members join; the bottom-right corner never moves. Grows up far enough to hide FFXI's native party window (top set by a reference line / zone). |
| **Cost / Next** | 0 | the highlighted spell/ability/WS from an open game menu | Floats **directly on top of the Party box**, right edge aligned, bottom flush to the party top. Reserves its height even when empty (so nothing jumps). |
| **Alliance 1** | 1 | 6 rows (members 6–11) | Sits above the reserved Cost box; right-aligned to the party. |
| **Alliance 2** | 2 | 6 rows (members 12–17) | Stacks flush on top of Alliance 1 (no gap). |

**Anchoring detail:** positions are stored as a **fraction of the screen** (resolution-independent).
When a box's measured size changes, the HUD shifts its stored top-left by the size delta so the
**bottom-right stays pinned**. If you drag a box in edit mode, that manual position wins over the
automatic stacking. Alliance boxes can alternatively anchor onto the native FFXI window positions
if alliance reference markers / zones are set.

### A.2 Anatomy of one member row

Column order, left → right, with the buff strip and cursor hanging off the far left:

```
[cursor] [buffs] │ [leader/QM pips]   [job    [ name              ]  [ HP ]  [ MP ]  [ TP ]
                 │ [distance]          badge]  [ cast line (spell) ]
```

| Element | What it shows | Data source | Colour / states |
|---|---|---|---|
| **Name** | player/trust name, truncated with "…" | party memory / packets | normal `#F0FFFF`; **dead** (HP≤0) red `#FF4646`; **out-of-zone** grey `#6E7689`. Optional UPPERCASE. |
| **Job badge** | job in a framed box. 4 modes: `0` off · `1` main job · `2` main+sub · `3` role icon | member job ids | Border + text tinted by **role colour** (see A.4). Icon mode tints a job emblem from the atlas. |
| **HP gauge** | current HP % | vitals | continuous gradient green `#6FDC74` → yellow → orange → red `#FB5A5A`. **Blinks red** when ≤25% (if HP anim on). |
| **MP gauge** | current MP % | vitals | constant blue `#4F9DFF`. |
| **TP gauge** | TP (0–3000 → 0–100%) | vitals | **bright pink `#FF7AE8` when TP ≥ 1000 (WS ready)** + glow pulse; dull purple `#7A5C8E` below. |
| **Buff strip** (party only) | status-effect icons, 2 reserved rows of up to 16 | packet `0x076` + self memory | icons from the buff atlas; laid out right-to-left, just left of the cursor. `Max Buffs` caps the count, `Buff Size` scales them. |
| **Distance** | yalms to you, `00.00` | member position vs player | **blue `#8FC6FF`** <10' · **yellow `#E7C95A`** 10–20.79' · **red `#E76C6C`** ≥20.8' (out of cast range). You show none. |
| **Leader / QM pips** | up to 3 dots (pop-in animated) | server-id leader match + QM flag | **white** = alliance leader (left) · **canary yellow `#FFEF3F`** = party leader (middle) · **green `#42D98A`** = Quartermaster (right). |
| **Cast line** | spell being cast, under the name | action packet `0x028` | light gold `#FFD970`, breathing opacity. Truncated to ~6 chars. |
| **Out-of-range veil** | dark wash over the whole row | dist ≥ 20.8' | translucent dark blue-grey — "can't reach them". |
| **Out-of-zone** | zone name instead of gauges | maxHP==0 | greyed name + zone name (or "out of zone"). |

**Gauge styles** (`Gauge Style`, per box): `0 Vial` (real liquid fiole), `1 Bars`, `2 Segments`,
`3 Minimal`, `4 Sphere`, `5 Ring`, `6 Crystal`, `7 Text` (the number itself carries the colour).
Fills animate smoothly toward their targets; each partial fill draws a "level line" at the top.

### A.3 The selection / target cursor

Two elements track **who you have targeted**: the **hand cursor** (slides between rows, points at
the name, bobs gently) and the **selection highlight frame** (a tinted glass bar with a slow sweep
across it). Both share three colour states:

| State | Hand | Frame | When |
|---|---|---|---|
| **Main target** | white `#FFFFFF` | gold glass + white sweep + side rims | you target a member |
| **Sub-target** (`<st>`) | blue `#2E9CFF` | ocean-blue glass + blue sweep (no rims), sits on top | you have a sub-target |
| **Locked-on** (`<t>` lock) | **red `#FF4030`** | **red glass + reddish sweep** | your target is locked (`targetLocked`, reversed from `target_t+0x5C`) |

The game's own party menu (Menu → Party → Distribution: **Quartermaster / Lottery / remove**) drives
a **blue frame** onto whichever member it points at — even while you're locked on someone else (the
lock keeps its own gold/red frame). This keeps the HUD in sync with the native menu on screen.

See [party visual system](party-visual-system.md) for the exact pixel values, and
[target & sub-target struct](../game-data/target-substruct.md) for the reversed flags.

### A.4 Job role colour (one tint per job)

`job_role_color()` is the single source. It classifies every job into a role and tints the job
badge (border + text + icon):

| Role | Colour | Jobs |
|---|---|---|
| Healer | green `#86D36F` | WHM, RDM, SMN |
| Buffer | yellow `#ECC94A` | GEO, COR, BRD |
| Tank | blue `#7D9BF0` | RUN, PLD |
| Special | purple `#B58BF0` | special trusts (SPC) |
| DD | red `#E08585` | everyone else |

### A.5 The Cost / Next box (spell/ability/WS helper)

Appears above the Party box while a **magic / ability / weapon-skill menu is open** (and permanently
as a spacer slot when you're in an alliance). It shows the highlighted action's name plus a value:

- **Spell** → "Cost N MP" + "Next m:ss" recast.
- **Job Ability** → "Next m:ss" recast.
- **Weapon Skill** → live "TP N".

Border toggled separately (`Border → Cost box`).

### A.6 Demo vs live

`//aio party demo [N]` (and `alliance1/alliance2 demo`) fill the boxes with a **baked FF-themed
roster** (Cloud, Yuna…) whose values are deliberately spread to show every state at once — full/mid/
low/critical HP, WS-ready and charging TP, in-range and out-of-range members. `//aio demo off`
returns to live memory data. `//aio sim N` instead appends N fake members to the **live** party to
test the grow-up. The config **Live Preview** forces a full 3-box demo and slides a cursor through
all 18 slots to show the selection highlight.

---

## PART B — The config overlay (`//aio config`)

A full-screen dimmed panel with its own gradient (no game skin), a gold **AIOHUD / CONFIGURATION**
header, an **EN | FR** language toggle and a close **X** on every tab. Three tabs:

| # | EN / FR | Purpose |
|---|---|---|
| 0 | **Configuration** / Configuration | all visual settings + the live preview |
| 1 | **Profile** / Profil | save / load / manage setting snapshots |
| 2 | **Help** / Aide | the in-app manual, with live examples |

`//aio config N` opens directly on tab N. `//aio config` while in edit mode acts as "Done".

### B.1 Configuration tab

Layout: a left **MODULES** sidebar (only one module today — **Party / Alliance** — with a
"more modules soon" hint), a **Profile quick-bar** across the top, then two columns —
**controls** on the left, **Live Preview** on the right.

Controls are grouped into three collapsible categories:

**GENERAL (global — affects every box)**

| Control | Changes | Options |
|---|---|---|
| **Box Theme** | the FFXI window skin for all boxes | 9 themes (14…21) |
| **Font** | text face for names/jobs/numbers | 20 faces |

**PARTY / ALLIANCE** — a **Box selector** (Party · Alliance 1 · Alliance 2) chooses which box `T`
the rows below edit. Most settings are **per-box**:

| Control | Changes | Scope | Range |
|---|---|---|---|
| **Box Size** | overall box scale | per-box | Party 100–200%, Alliances 50–200% |
| **Buff Size** | buff icon size | **Party only** | 40–200% |
| **Max Buffs** | how many buffs shown | **Party only** | 16 / 20 / 24 / 32 |
| **Cursor Size** | selection hand size | **Party only (global cursor)** | 50–200% |
| **Bar Height / Bar Width** | gauge dimensions | per-box | 80–180% / 80–150% |
| **Gauge Style** | gauge look | per-box | Vial / Bars / Segments / Minimal / Sphere / Ring / Crystal / Text |
| **Animation** | HP blink & TP glow | **global** | HP on/off, TP on/off |
| **Job Badge** | badge mode | per-box | Off / Main / Main+Sub / Icons |
| **Badge Size** | badge scale (hidden if Off) | per-box | 50–200% |
| **Casts** | show the casting line | per-box | on/off |
| **Distance** | show the yalm number | per-box | on/off |
| **Border** | window frame per box | per-box (+ **Cost box** when Party) | on/off |

Plus a **Layout** row: **Edit Layout** (enters edit mode) and **Default (all)** (resets everything).

**TYPOGRAPHY** — an **Element** selector picks one of 9 text elements (Name, HP, MP, TP, Cast,
Job Badge, Distance, Interface, Cost box); the controls below edit that element's style **globally**:
Font, Size (50–200%), Outline (0–200%), Style (Bold / Italic / CAPS), Colour (Default or a custom
preset from 8 swatches). ("Interface" styles the config menu's own font.)

### B.2 Live Preview

The right column renders the **real** party + both alliance demo boxes, anchored bottom-right in a
recessed stage, reflecting every change instantly (it's the exact shipping renderer, not a mock-up).

### B.3 Profile tab

- **Character rail** — the logged-in character, the active profile, an "unsaved changes" dot, and
  two **Quick Save** buttons: *Save for &lt;char&gt;* and *Save as Default*.
- **Profiles pane** — a **New Profile** text field (button reads *Create* or *Overwrite*), and a
  **Saved (N)** library where each row has **ACTIVE / DEFAULT / character** badges plus **Load** and
  **Delete** buttons.

A profile is a **snapshot of the entire config** (all settings + the whole layout + zones); see E.

### B.4 Help tab

A left module menu + a scrollable, wheel-scrolled article on the right, in EN/FR with `*bold*` /
`_highlight_` markup. Two modules today: **General** (intro, the three tabs, language, profiles,
the command list) and **Party / Alliance** (every HUD feature). The Help embeds **live samples**:

- a distance readout sweeping 0→30 with its colour thresholds,
- the three leader/QM dots with their game terms,
- the real HP gauge sweeping full→empty (with the critical blink),
- the real MP + TP gauges (TP glowing past 1000),
- the selection hand shown three ways — **main (white/gold), sub (blue), locked (red)**.

---

## PART C — Edit mode (move & resize)

Entered by **Edit Layout** or `//aio edit`. The config panel hides; the game and the live boxes show
through, plus a floating **toolbar** (top-center):

- **Rules: ON/OFF** — opens the Zones editor (and hides the HUD while on).
- **Clear lines** — wipe all reference lines (with a confirm modal).
- **Default** — reset every box position + size (confirm modal).
- **Done** — leave edit mode and save.

**Interactions:** drag a box to **move** it; **mouse-wheel over a box to resize** it (each box scales
independently, clamped 0.5–2.0× at the raw level). Boxes **snap to each other's edges** within ~10px
while dragging, and are pushed out of forbidding zones. The Party box stays **bottom-right anchored**
as it resizes. Mouse and keyboard are captured only while the overlay/edit is up and the game window
is foreground (a click is one gesture with a single swallow/pass decision, so the character never
gets "stuck").

---

## PART D — Zones / "Rules" system

A **zone** is a named rectangle stored as **screen fractions** (resolution-independent) that
constrains where a box may sit. Up to 48 zones.

- **Permissions** (`allow[3]`): **Party**, **Alliance**, **Hub**. A zone that allows **nothing** is a
  **keep-out** — every box dragged onto it is pushed back out. Otherwise, boxes of the allowed types
  may overlap it and others are pushed out.
- **Role** drives placement: role `1..6` = the Party box's top line **for that member count** (party
  grows up to the zone matching your headcount); role `7/8` = Alliance 1 / Alliance 2 anchors.

**Editing (Rules: ON):** the HUD hides so only zones + toolbar + a draggable **Zones panel** show.
- **Draw** by rubber-banding on empty space; **move** by the body; **resize** by corner handles.
- Panel buttons: **+ Zone** (one plain zone), **+ Party** (creates the six `1p…6p` zones),
  **+ Ally** (creates the two alliance zones). Per-zone: **Rename**, **Delete**, and the
  **Party / Alliance / Hub** permission chips.
- The zone list is scrollable and drag-reorderable.

When Rules is off, zones still silently constrain placement (drawn faintly). Legacy reference lines
(`partyRef`, `allyRefY`…) remain as fallbacks and as seed values for the + Party / + Ally buttons.

See [edit-mode zones](edit-zones.md) for the on-screen workflow.

---

## PART E — Profiles & persistence

**Two persistence layers coexist:**

1. **Live config — `aio_config.txt`** — a `key=value` file holding the *entire* `UiConfig`: theme,
   font, per-box options, typography, buffs/cursor, the reference lines, one line per zone, and each
   box's `posSet,x,y,scale`. Saved on every meaningful change (stepper release, edit exit, zone edit,
   resets). `aio_active.txt` remembers the active profile.
2. **Layout descriptor — `design/exports/layout.json`** — the **design → native contract** exported
   from the HTML/CSS mock (`//aio layout` loads it): widgets with a stable `id`, native `type`,
   `anchor` corner, `pos` as % of viewport, size, z, visibility, job gating; plus native-window
   keep-out `zones`. It carries **no style**, only placement. See [layout-json](../formats/layout-json.md).

**Positions are resolution-independent everywhere** — every box position and every zone is a share of
the screen (0..1), converted to pixels at draw time. Out-of-range values are clamped so a box can
never end up off-screen.

**A profile** (`aiohud_profiles/<name>.txt`) is a **full snapshot of `UiConfig`** — all settings +
the whole layout + all zones (language excluded from dirty-tracking). Save / Load / Delete / Create /
Overwrite / Quick-save are all non-destructive; the last active profile auto-loads at startup. Up to
64 profiles; display names are `%`-encoded for the filesystem.

---

## PART F — All `//aio …` commands

Console alias is `aio`; matching is case-insensitive substring.

**User-facing**

| Command | Does |
|---|---|
| `//aio config [N]` | toggle the overlay (optionally on tab 1–3); acts as "Done" in edit mode |
| `//aio edit` | toggle layout edit mode (drag/resize on the live game) |
| `//aio profile save\|load\|delete <name>` · `profile list` | manage profiles from chat |
| `//aio party demo [N]` | fake party of N (1–6) |
| `//aio alliance1 demo` · `alliance2 demo` | add 1 / 2 fake alliances (12 / 18) |
| `//aio demo off` | back to live data |
| `//aio menu N` | pick window-skin theme N |
| `//aio lay N` | effect layers (1 = liquid only, ≥4 = full) |
| `//aio sim [N]` / `sim off` | append N fake members to the **live** party (grow-up test) |
| `//aio res` | report backbuffer vs window size (the 1:1 resolution to set) |

**Debug / reverse-engineering probes** (log to `aiohud_debug.log`; not for players): `tlock`, `tgt`,
`tgt2`, `anchor`, `tcheck`, `sub`, `pcur`, `dist`, `slots`, `party`, `chain`, `vit`, `ffxi`, `obj`,
`recast`, `rcfind`, `menu`, `mouse`, `keyprobe`, `pkt`, `find`, `scan`, `dump`, `grabmod`, `hp/mp/tp`
fills, `layout`.

---

## PART G — How it all connects

```
   //aio config ──► Config overlay ──┬─ Configuration tab ─► writes UiConfig fields ─┐
                                      ├─ Profile tab ───────► snapshot/restore UiConfig │
                                      └─ Help tab                                       │
                                                                                        ▼
   //aio edit ────► Edit mode ──► box[].x/y/scale (screen fractions) ───────────► UiConfig
                      │                                    ▲                          │
                      └─ Rules ─► Zones (GuideGroup[]) ────┘ constrain placement       │
                                                                                        ▼
                                              save_ui_config() ──► aio_config.txt ◄── startup load
                                                                        ▲
                                            profiles/<name>.txt ────────┘ (full snapshot)

   Every frame:  poller reads game memory/packets ─► GameState snapshot ─► the 3 Party
                 widgets (tier 0/1/2) draw the boxes using the live UiConfig values.
```

Key linkages an outsider should keep in mind:

- **One config, three boxes.** Per-box arrays (`box`, `barHeight`, `gaugeStyle`, `jobBadge`, `cast`,
  `dist`, `border`) are indexed 0=Party, 1=Alliance 1, 2=Alliance 2. Global fields (theme, font,
  buffs, cursor, animation, typography) hit all three.
- **Size changes re-anchor, they don't drift.** Because every box is bottom-right anchored, changing
  a size (slider or edit-wheel) keeps the placed corner fixed. A bulk load (startup / profile / reset)
  flags a "scale baseline reset" so it isn't mistaken for a live resize.
- **Zones are the placement contract.** The Party box's grow-up top and the alliance anchors come
  from zones (with legacy reference lines as fallback); keep-out zones repel boxes during drags.
- **Profiles snapshot everything at once** — settings *and* layout *and* zones — so "load a profile"
  reconfigures the entire interface in one step.
- **Preview = the real thing.** The config Live Preview is the shipping renderer in demo mode, so
  what you see while tuning is exactly what you get in play.

---

## PART H — Observations for the redesign

Factual friction points surfaced while mapping (not yet design decisions — inputs for the reorg):

- **Scope is uneven and not obvious in the UI.** Some "Party / Alliance" controls are secretly
  **Party-only** (Buff Size, Max Buffs, Cursor Size) or **global** (Animation, Box Theme, Font,
  all Typography) even though they sit next to per-box controls behind the Box selector. A cleaner
  layout would separate *global* from *per-box* explicitly.
- **Two ways to change size, two clamps.** The Box Size slider advertises 100–200% / 50–200%, but the
  raw edit-mode wheel clamps 0.5–2.0× for all tiers — worth unifying.
- **Profiles appear in two places** (the quick-bar on the Configuration tab and the full Profile tab)
  — fine, but the relationship should be obvious.
- **One module, "more soon."** The MODULES sidebar has a single entry; the structure anticipates
  growth but currently adds a layer of nesting for one item.
- **Zones are powerful but hidden** behind Edit → Rules → panel, and overlap conceptually with the
  legacy reference lines and with `layout.json`'s native keep-out zones. Three placement mechanisms
  (box drag, zones/roles, layout.json) is a lot to hold in one's head.
- **Typography is deep** (9 elements × face/size/outline/bold/italic/caps/colour) — great power, but a
  lot of clicks; a redesign could surface the common cases and tuck the rest.

## See also
- [Party / Alliance design brief](brief-party-alliance.md)
- [Party visual system](party-visual-system.md) — exact pixel values
- [Edit-mode zones](edit-zones.md)
- [Layout JSON format](../formats/layout-json.md)
- [Widgets & the add-a-module checklist](../architecture/widgets.md)
- [Target & sub-target struct](../game-data/target-substruct.md)
