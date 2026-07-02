---
title: The per-frame data flow
summary: How the render hook polls the roster + GameState snapshot once per frame, then draws every widget from that snapshot.
source: ARCHITECTURE.md §2
---
# The per-frame data flow (THE important diagram)

The render hook (`aio_plugin_render6` → `Hud::render`) runs once per game frame:

```
Hud::render(dev) {
    party().load_from_memory();     // 1. roster : self + party + alliance, slots 0..17 (model)
    poll_game_state(state_);        // 2. snapshot : player vitals/jobs, target, leaders, menu (model)
    Frame f { dev, fonts, t, game=&state_ };
    for (w : widgets_) w->draw(f);  // 3. every widget renders FROM the snapshot, never polls memory
}
```

**The seam** (`gamestate.h`): `GameState` is the single per-frame snapshot. `poll_game_state()`
reads each memory pointer-chain **once**; widgets read `f.game->…`. This is what makes the HUD
scale to many widgets — N widgets that need the target read it once, not N times — and keeps the
fragile memory offsets in **one** place (the poller) instead of scattered across every `draw()`.

> The party ROSTER is the one exception: it's bulky and shared, so it lives in the
> `party()` singleton (also refreshed once per frame by `load_from_memory`), reached through
> the same once-per-frame discipline.

`GameState` today: `inGame`, player `me` + hp/mp/tp fractions, `targetId`/`subTargetId`,
alliance/party leader ids, action-menu `menuType`/`menuAction`, and `partyMenuSel` (the
party-window picker cursor). Add a field here when a new widget needs a new datum.

## See also
- [Layers](layers.md)
- [Widgets](widgets.md)
- [Reading the live roster](roster.md)
- [Player struct](../game-data/player-struct.md)
- [Target substruct](../game-data/target-substruct.md)
