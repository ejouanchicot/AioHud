// box_grow.h -- WHICH WAY a module box grows when its content widens it (UiConfig::boxGrow, one per box).
//
// Each box stores ONE X (scX, tpX, ... ; the Timers have two, tmX and tmRX) and that X is the point its grow
// direction pins : the LEFT edge (grows right), the CENTRE (both sides) or the RIGHT edge (grows left). So a box is
// placed by box_grow_x, dragged by box_edit with anchorX = its grow value, and a direction change goes through
// box_grow_set -- which moves the stored X by the box's last drawn width so the box itself stays exactly put.
// The on-screen guard (box_on_screen, hud_internal.h) still applies on top.
#pragma once
#include "ui/widget.h"   // Frame

namespace aio {

// The share of the box's width that lies LEFT of its pinned point : 0 (left edge), 0.5 (centre), 1 (right edge).
inline float box_grow_k(int grow) { return grow == 1 ? 0.5f : (grow == 2 ? 1.0f : 0.0f); }

// Live placement : the box's left edge in pixels, from its stored X (a screen fraction) and its current width. Also
// records that width (per box, per stored X -- `sub` 1 is the separate Timers Recast box) for box_grow_set.
float box_grow_x(float screenW, int box, int sub, float cfgX, float boxW);   // screenW : the one the module places with

// Change a box's grow direction WITHOUT moving it : its stored X(s) follow the new pinned point. Saves the config.
void box_grow_set(int box, int grow);

}  // namespace aio
