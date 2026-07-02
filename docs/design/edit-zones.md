---
title: Edit-layout GUIDES & ZONES
summary: The //aio edit overlay — how named fractional zones (GuideGroup) with per-type permissions and roles drive party/alliance box sizing and placement.
source: ARCHITECTURE.md §8
---
# Edit-layout GUIDES & ZONES  (2026-07-01)

`//aio edit` (`ui_config().editLayout`) lets you arrange the boxes on the live game. The config
overlay (`config_page.cpp`, the `editLayout` branch) draws a floating toolbar; the party/alliance
boxes handle their own drag/resize in `party.cpp`. The **"Rules"** toggle (`editShowLines_`) hides
the whole HUD (`hud.cpp` skips the widget draw) so only the guides + toolbar show.

**Zones (`GuideGroup`, `ui_config.h`).** A zone is a named **rectangle** the user draws by drag
(rubber-band) in Rules mode, stored as **screen fractions** (`x/y/w/h` in 0..1 — resolution
independent). Fixed-capacity array (`guideGroup[GUIDE_GROUPS_MAX]`, no heap), serialized as
`zone=x,y,w,h,allowP,allowA,allowH,role,name` in the config file.
- `allow[ZPERM_PARTY|ALLIANCE|HUB]` — which HUD box may sit on the zone. A box being dragged is
  pushed OUT of any zone that forbids its type (`guide_push_out`, called from `party.cpp`'s drag).
- `role` — `0` = plain keep-out/permission zone ; `1..6` = the **party box for that member count**
  (its rectangle TOP drives the party grow-up) ; `7/8` = **Alliance 1 / 2** (their top/bottom place
  the alliance boxes). This replaces the old full-span reference-LINE handles.

**How zones drive the boxes (`party.cpp`).**
- Party sizing: `guide_party_top(count, sh)` returns the role-`count` zone's top; `party.cpp` grows
  the party box up to it. Falls back to the legacy `partyRef[]` line if that zone is absent.
- Alliance placement: `guide_alliance_refs(ar)` fills `{A1 top, A1 bot, A2 top, A2 bot}` from the
  role 7/8 zones; `party.cpp` uses the bottoms to anchor the alliance boxes. Falls back to `allyRefY[]`.
- Only the party zone's TOP and the alliance zones' TOP/BOTTOM are functional; left/right/other edges
  are visual (like the old L/R/`0` markers, which were visual-only).

The legacy fraction fields (`partyRef[6]`, `partyRefX[2]`, `partyBottomY`, `allyRefY[4]`) are kept as
the **fallback** and as the source values the `+ Party` / `+ Ally` buttons seed the zones from.

**Panel.** Draggable, auto-sized (width fits the title/hint text, height fits the zone list + the
action row), scrollable (mouse wheel or the scrollbar thumb). Rename uses the shared keyboard field
(`nameBuf_`/`nameFocus_`, the same slot-14 hook as the Profile tab) with an **OK** button — no Enter
needed. Input layering: the panel rect (`overPanel`) is computed FIRST so nothing underneath (zone
handles) grabs a click that lands on the panel.

## See also
- [Party visual system & config](party-visual-system.md)
- [Layout JSON format](../formats/layout-json.md)
- [Coordinates](../reference/coordinates.md)
- [UI composition](../tech-stack/ui-composition.md)
