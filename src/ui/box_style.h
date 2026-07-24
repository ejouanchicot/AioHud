// box_style.h -- the shared "box appearance" bundle every module (except Party/Alliance, the master) uses so a
// box gets the SAME chrome options as Target/Player : frame on/off, transparency, theme (Same-as-Party or own
// procedural/FFXI family + hue + luminosity). One draw helper + one config-row helper -> no per-module dup.
#pragma once
#include "gfx/d3d.h"
#include "model/ui_config.h"    // BoxStyle (defined in model so ui_config can hold one per module)

namespace aio {
struct WindowSkin;

// Draw a themed box at [x,y,w,h]. Resolves Same-as-Party internally. `partySkin` = the shared FFXI skin (f.skin,
// reused for FFXI-family themes). `base` = an extra fade (1 = none). `S` = the box's UI scale (fallback corner).
void draw_themed_box(u32 dev, const WindowSkin* partySkin, float x, float y, float w, float h,
                     const BoxStyle& bs, float base, float S);

// The box's effective FRAME colour (RGB, alpha 0xFF), resolving Same-as-Party / procedural-hue / FFXI-skin -> tint an
// inner accent (e.g. the Skillchains "Nuke:" outline) to MATCH the box border. Soft-blue fallback while a skin loads.
u32 box_style_border_color(u32 dev, const WindowSkin* partySkin, const BoxStyle& bs);

// Per-box "Custom -> FFXI" skins own their own textures (independent of the shared party skin). Wired from Hud :
void box_skins_forget();    // device recreate : forget the handles (reload lazily)
void box_skins_dispose();   // shutdown : release them

} // namespace aio
