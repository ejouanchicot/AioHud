---
title: Party visual system & config
summary: The most-styled widget — AA rounded-rect primitives, 8 gauge styles, job badges/icons atlas, buffs, cursor/markers/typography, and the design brief pointer.
source: ARCHITECTURE.md §9
---
# Party visual system & config  (2026-07-02)

The party/alliance box (`ui/party.cpp` + `party.h`) is the most-styled widget. Everything below is
driven by `ui_config()` and is **per-box** where noted (index 0 = party, 1 = alliance 1, 2 = alliance 2).

### 9a. AA rounded-rect primitives (`gfx/draw.{h,cpp}`)
The house AA style is **feathered geometry** (a solid core + a rim that fades alpha 0 over `feather`
px), the same recipe as `disc()`/`seg_soft()`. Added primitives:
- `rrect(x,y,w,h,r,cTop,cBot,feather=1.2)` — AA rounded rect, vertical gradient, uniform corners.
- `rrect_bordered(...)` — border ring + inner fill (the framed job-badge box, panels).
- `rrect_left(...)` — rounded LEFT corners, FLAT right edge (a bar fill whose tip is a clean level).
- `rrect_glow(...)` / `disc_glow(...)` — smooth additive halos (button/knob glows, WS-ready/HP-crit auras).
- `rrect_stroke(...)` — AA outline only (available; currently unused).
> **HARD-WON RULE:** feather **all four straight edges AND the corners consistently**, or none. If
> only the corners feather, the rounded ends extend ~1px past the straight centre → the backdrop shows
> as a thin dark line across the top/bottom (the "black line" bug). Bar fills draw **edge-to-edge** (full
> height, over the track rim) so the rim never shows as a line inside the fluid.

`config_page.cpp` has its own `rrect_fill`/`rpanel` that delegate to these (with `fa()` for the panel
fade). Buttons are rounded again (were forced rectangular before the AA existed).

### 9b. Gauge styles (`gaugeStyle[3]`, 0..7)
Per-box: `0 Vial` (real fiole textures, `liquid_bars.cpp`), `1 Bars`, `2 Segments`, `3 Minimal`,
`4 Sphere`, `5 Ring`, `6 Crystal`, `7 Text`. Round styles (4/5/6) use a square cell (`circularGauge()`).
The **Fiole** clips its textured liquid to a rounded capsule via the **stencil buffer**
(`rrect_clip_begin/end` in `liquid_bars.cpp`) → round ends, transparent corners, animation preserved;
falls back to square (never black) if the back-buffer has no stencil. Shape-matched level line
(`level_line`/`level_line_v`) + animated auras (`gauge_aura_soft` = `rrect_glow` capsule, `gauge_aura_round`
= `disc`). Per-box `barHeight[3]`, `barWidth[3]`, `animHP`, `animTP`.

### 9c. Job badge (`jobBadge[3]`, 0..3) — incl. **Icons**
`0 Off / 1 Main / 2 Main+Sub / 3 Icons`. All modes draw the same framed box (`rrect_bordered`, dark
bg + **role-colour border** from `job_role_color`). Text modes draw the abbr (`job_abbr`), role-tinted.
**Icons** mode draws a job emblem inside the box, tinted by role (`draw_job_icon`).
- **Atlas**: `assets/job_icons.raw` — 512×192 BGRA, 8×3 grid of 64px cells, **white masks** (RGB=255,
  keep alpha) so MODULATE tints them by the role colour. Cell = `job_id_from_abbr(job) - 1`, i.e. the
  `JOBS[1..22]` order (WAR=cell 0 … RUN=cell 21; SPC/unknown → skip).
- **Regenerate** (source PNGs in `ffxi_job_icons/`, one white emblem per job): a Python+PIL step —
  load each PNG RGBA, resize to 64, force RGB white / keep alpha, paste in `JOBS` order, write BGRA raw.
  (`JOBS` lives in `party_state.cpp`; `job_role_color` maps id → tank blue / healer green / support
  yellow / dd red / SPC purple.)

### 9d. Buffs (party box only — the game never sends alliance buffs)
Left of each row. `buffScale` (Buff Size %, 0.40..2.00) sizes the icon off `barSz_` and **grows the row
height** (`party.h buffBandH` is a floor on `mainBandH`). The strip **always reserves TWO rows**
(`buffRows()` fixed at 2) so changing `buffMax` (16/20/24/32 — a *count cap* only) never resizes the box;
`buffMax > 16` wraps into a second row (16+16). Icon size is **constant** whether 1 or 2 rows; the first
row keeps the TOP slot. A margin keeps the two rows clear of the next player. Demo buff set = `BUFF_POOL`
(32 ids).

### 9e. Cursor, markers, distance, typography
- **Selection cursor** (hand): `cursorScale` (50..200%); size = `mh*1.30*scale`, +1.45× on alliance boxes
  (condensed rows). Nudged right (`+0.14*iw`) so the finger touches the box (the art has a transparent
  right margin). Buffs start just left of the *visible* cursor.
- **Leader/QM pips + distance**: the pips+distance **unit** (height `marksColH`) is **centred in the main
  band** so it stays balanced when buffs grow the row (was pinned top/bottom → drifted apart).
- **Per-element typography** (`TextStyle text[TE_*]`): Name/HP/MP/TP/Cast/Badge/Distance/Interface/Cost,
  each Face/Size/Bold/Italic/Outline/CAPS/Color. Bold is the SOLE weight authority (400/700). Fonts are
  bundled (`assets/fonts`, `AddFontResourceEx` FR_PRIVATE) — `MAXF=48` pool (was 8, which silently fell
  back to slot 0).

### 9f. Design brief
[`brief-party-alliance.md`](brief-party-alliance.md) is a brief for an external "design" AI to invent new box concepts. Hard
constraints it must respect: **opaque contiguous** background (no separated per-player tiles), **3
independent boxes** (not one 18-stack), bottom-anchored grow-up (floor 100 % native coverage),
**resolution-independent** incl. 16:9/21:9/32:9/4:3 (fractions, not px), and **one concept at a time**.

## See also
- [Brief Design — Party / Alliance](brief-party-alliance.md)
- [Edit-layout GUIDES & ZONES](edit-zones.md)
- [Anti-aliasing](../tech-stack/anti-aliasing.md)
- [Stencil](../tech-stack/stencil.md)
- [Textures](../tech-stack/textures.md)
- [Fonts](../tech-stack/fonts.md)
- [Member buffs](../game-data/member-buffs.md)
- [Cast bar](../game-data/cast-bar.md)
