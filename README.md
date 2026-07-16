# AioHud

**The most complete HUD ever built for FINAL FANTASY XI.** One native Windower 4 plugin that redraws the whole interface — party, target, timers, minimap, skillchains and a dozen more panels — live over the game, tuned entirely in-game, in English or French.

---

## Why it's different

Most FFXI overlays are a **stack of Lua addons**. Each one runs its own interpreted loop every frame, and the more you pile on, the more your framerate pays for it.

AioHud is **one C++ plugin drawn straight through the game's own Direct3D 8 device**, on the game's own render thread. No second process, no second thread, no interpreter. It reads memory **read-only** (every access crash-guarded), snapshots the game state **once per frame**, and draws with cached, driver-managed textures and binary-searched data tables. First-use font atlases are baked a couple per frame so there's no load hitch.

The result: **one HUD that does the work of fifteen addons, at effectively zero added frame cost.** That's the whole point.

## What you get

Every panel below is a real module — toggle it, theme it, and move it on its own.

- **Party & Alliance** — your party plus both alliance parties, with liquid HP/MP/TP gauges (pick from **8 styles**: vial, bars, segments, sphere, ring, crystal, minimal, text), job emblems, leader/quartermaster pips, per-member distance with out-of-range dimming, buff strips, and a sliding target cursor for `<t>` / `<st>` / lock-on. Examine an action and the row even shows its **MP cost, live recast, or weapon-skill TP**.
- **Target** — animated HP fiole with a white-damage trail and a low-HP alarm, name colored by claim, lock-on padlock, **Front/Flank/Behind** position badge, movement-speed readout, **Treasure Hunter** tier, a distance gauge with melee/WS/magic/ranged range bands, a type-colored cast bar, a full debuff row with timers, and its own **sub-target** panel.
- **Player Hub** — your vitals as liquid fioles, job/level, movement speed, gil, your own cast bar, a full **buff tray**, and a **16-slot equipment viewer** with real gear icons, ammo counts and locked-slot markers.
- **Timers** — your active buffs at their **exact server durations** and your recasts, on one panel, color-ramped by urgency. Party buffs you cast fold into **AoE-counted rows** ("Minuet V (AoE 6)"), with **BRD song tags**, **COR roll pips**, **GEO aura lifetimes**, and a per-job **Track list** with a 4-state focus system that flashes a red **OUT** alert the moment a critical buff (Haste, Refresh, Phalanx, Composure, Reraise…) drops — on you *or* on someone you buffed.
- **Minimap** — a real zone map centered on you, square or round, with rotating player/mob markers, target line, aggro ring, wheel zoom, and a **Vana'diel clock** with elemental day, a rendered **moon-phase** graphic, and next new/full countdowns.
- **Skillchains** — the open skillchain on your target: property, a three-phase **burst-window countdown** (Wait → Go! → Burst), the closing move, and a list of **your** moves that continue the chain, ranked by level.
- **Hate List** — every mob with enmity on your party, sorted by HP, each with a HP fiole, distance, and who it's pointing at; your current target is framed (red when it's claimed).
- **PointWatch** — XP / CP / Job Points / Master Level / EP and Merits, auto-picked for where you are, with a live gain-per-hour rate.
- **Grimoire (SCH)** — the Light/Dark grimoire book with stratagem charges and recast, a pulsing Addendum aura, and a closed book when you have no Arts.
- **Zone Tracker** — content-aware panels for **Dynamis, Abyssea, Omen, Nyzul Isle, and Sheol/Odyssey** (timers, key items, light values, floor objectives, segment counts, target resistances…).
- **Arcade WS** — when you land a weaponskill, a center-screen "ULTRA COMBO" flash with the name and total damage. Because why not.

## Make it yours

AioHud is built to be customized to death — **well over 150 saved options** across 13 panels:

- **7 theme families** (FFXI window skin, Modern, Medieval, Heroic, Neon, Frost, Royal) each with a full **hue palette** and luminosity.
- **Per-box transparency**, or turn the box chrome off entirely and let the content float.
- **8 gauge styles**, adjustable bar height/width, badge and buff scaling.
- **20 fonts** with **per-element** control — face, size, weight, italic, outline, color, uppercase — on names, HP, MP, TP, cast bars, distances, badges…
- **Everything toggles.** Whole modules, and individual pieces inside them (speed, TH, range, sub-target, equipment, clock parts, animations…).
- **Full English / French** interface.
- **Character-bound profiles** that snapshot *everything at once* — settings, layout and zones — and auto-load on login and job change. Playing a different job? Your HUD is already the way you set it.

## Layout you actually control

Type `//aio edit` and arrange the whole interface live over the running game:

- **Drag any box**, snap it to screen center, hold **Shift/Ctrl** to lock an axis, and **scroll to resize** it (0.5×–2×). Boxes won't overlap — they shove each other out of the way.
- Positions are stored as **screen fractions**, so your layout holds at any resolution.
- **No-go zones:** rubber-band a rectangle anywhere, then say which boxes may sit on it. Forbidden boxes are physically pushed out. Use it to fence the HUD off from the game menu, chat log, whatever you want kept clear.
- The party box is **bottom-anchored and grows upward as members join**, sized and placed from the game's own alliance markers and party size — so it **sits over FFXI's native party and alliance windows and covers them completely**, tracking the native layout instead of floating loose. The vanilla frames simply disappear underneath.

## Install

1. Download **`AioHud-x.y.z.zip`** from the [latest release](../../releases/latest).
2. **Extract it into your Windower folder** — the one that already contains `plugins\` and `addons\` (e.g. `D:\Windower\`). This drops:
   - `plugins\AioHud.dll` + `plugins\AioHud\` (the plugin and its assets)
   - `addons\aioupdate\` (the one-click in-game updater)
3. **Load it at startup.** Open `Windower\scripts\init.txt` in a text editor and add this line on its own line:
   ```
   load AioHud
   ```
   Save. AioHud now loads every time you launch the game. *(Or just tick **AioHud** in the Windower launcher's Plugins tab.)*
4. **Start the game** — or, if you're already in, type `//load AioHud`.

The first time it loads, AioHud also registers its updater (`lua load aioupdate`) in `init.txt` for you, so updating works from then on with nothing else to set up.

## Everyday use

| Command | What it does |
|---|---|
| `//aio config` | open the full-screen config window (themes, modules, fonts, colors…) with a live preview |
| `//aio edit` | move, resize and zone the boxes directly on screen |
| `//aioupdate` | update to the latest version in game, with **no window** (the HUD blinks for a second while it reloads) |

Your settings and profiles live in `plugins\AioHud\data\` and are kept safe across updates.

**Dual-boxing:** Windower locks the plugin while *any* client has it loaded, so `//unload AioHud` on the other client first, then run `//aioupdate`.
