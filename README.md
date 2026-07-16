# AioHud

A complete HUD overlay for **FINAL FANTASY XI** — a native Windower 4 plugin that redraws the interface (party, target, timers, minimap, skillchains and more) live over the game. Everything is configured in-game, in English or French.

---

## Built as one native plugin

Most FFXI overlays are a collection of Lua addons, each running its own interpreted loop every frame. AioHud is a single C++ plugin drawn through the game's own Direct3D 8 device, on the game's render thread — no separate process, no separate thread, no interpreter.

It reads game memory read-only (every access crash-guarded), snapshots the state once per frame, and draws with driver-managed textures and binary-searched lookup tables. In practice that means the full interface runs with a negligible framerate cost, where stacking the equivalent addons would not.

## Modules

Each panel is independent — toggle it, theme it, and position it on its own.

- **Party & Alliance** — your party and both alliance parties, with HP/MP/TP gauges (8 styles: vial, bars, segments, sphere, ring, crystal, minimal, text), job emblems, leader/quartermaster markers, per-member distance with out-of-range dimming, and buff strips. Selecting an action shows its MP cost, live recast, or weapon-skill TP on the row.
- **Target** — animated HP fiole with a white-damage trail and a low-HP alarm, name colored by claim, lock-on indicator, Front/Flank/Behind position badge, movement speed, Treasure Hunter tier, a distance gauge with range bands, a cast bar, a debuff row with timers, and a separate sub-target panel.
- **Player Hub** — vitals, job/level, movement speed, gil, your cast bar, a buff tray, and a 16-slot equipment viewer with real gear icons, ammo counts and locked-slot markers.
- **Timers** — active buffs at their exact server durations and recasts on one panel, color-ramped by urgency. Buffs you cast on the party fold into AoE-counted rows, with bard song tags, corsair roll pips and geomancer aura lifetimes. A per-job track list with a 4-state focus system flashes an alert when a critical buff (Haste, Refresh, Phalanx, Composure, Reraise…) drops on you or on someone you buffed.
- **Minimap** — a zone map centered on you, square or round, with rotating player/mob markers, target line, aggro ring, wheel zoom, and a Vana'diel clock with elemental day, moon phase and next new/full countdowns.
- **Skillchains** — the open skillchain on your target: property, a burst-window countdown, the closing move, and the moves you can use to continue the chain.
- **Hate List** — mobs with enmity on your party, sorted by HP, each with a HP gauge, distance and who it's targeting; your current target is framed.
- **PointWatch** — XP / CP / Job Points / Master Level / EP and Merits, selected for where you are, with a gain-per-hour rate.
- **Grimoire (SCH)** — the Light/Dark grimoire with stratagem charges and recast, an Addendum aura, and a closed book when you have no Arts.
- **Zone Tracker** — content-aware panels for Dynamis, Abyssea, Omen, Nyzul Isle and Sheol/Odyssey (timers, key items, light values, floor objectives, segment counts, target resistances).
- **Arcade WS** — a center-screen flash with the weaponskill name and total damage when you land one.

## Customization

Over 150 saved settings across 13 panels:

- **7 theme families** (FFXI window skin, Modern, Medieval, Heroic, Neon, Frost, Royal), each with a hue palette and luminosity.
- **Per-box transparency**, or hide the box chrome and let the content float.
- **8 gauge styles**, adjustable bar sizes, badge and buff scaling.
- **20 fonts** with per-element control — face, size, weight, italic, outline, color, uppercase — on names, HP, MP, TP, cast bars, distances and badges.
- Every module toggles, and so do the pieces inside them (speed, TH, range, sub-target, equipment, clock parts, animations).
- Full **English / French** interface.
- **Character-bound profiles** that capture settings, layout and zones together, and auto-load on login and job change.

## Layout

Type `//aio edit` to arrange the interface live over the game:

- Drag any box, snap it to center, hold **Shift/Ctrl** to lock an axis, and scroll to resize (0.5×–2×). Boxes don't overlap — they push each other out of the way.
- Positions are stored as screen fractions, so the layout holds at any resolution.
- **No-go zones:** draw a rectangle anywhere and choose which boxes may sit on it; forbidden boxes are pushed out. Useful for keeping the HUD clear of the game menu or chat log.
- The party box is bottom-anchored and grows upward as members join, placed from the game's own alliance markers and party size, so it covers FFXI's native party and alliance windows and tracks their layout.

## Install

1. Download **`AioHud-x.y.z.zip`** from the [latest release](../../releases/latest).
2. **Extract it into your Windower folder** — the one that already contains `plugins\` and `addons\` (e.g. `D:\Windower\`). This drops:
   - `plugins\AioHud.dll` + `plugins\AioHud\` (the plugin and its assets)
   - `addons\aioupdate\` (the in-game updater)
3. **Load it at startup.** Open `Windower\scripts\init.txt` in a text editor and add these two lines (near the other addons is fine):
   ```
   load AioHud
   lua load aioupdate
   ```
   The first loads the HUD; the second loads the one-click in-game updater. Save the file — both now load every time you launch the game.
4. **Start the game** (or, this once, type `//load AioHud` and `//lua load aioupdate` to load them without restarting).

> **Why two lines?** A Windower plugin can't load a Lua addon on its own, so the updater is a tiny companion addon that rides alongside AioHud. Add both once and you never think about it again.

## Commands

| Command | What it does |
|---|---|
| `//aio config` | open the config window (themes, modules, fonts, colors) with a live preview |
| `//aio edit` | move, resize and zone the boxes on screen |
| `//aioupdate` | update to the latest version in game, with no window (the HUD reloads for a moment) |

Settings and profiles live in `plugins\AioHud\data\` and are kept across updates.

**Dual-boxing:** Windower locks the plugin while any client has it loaded, so `//unload AioHud` on the other client first, then run `//aioupdate`.
