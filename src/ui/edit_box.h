// edit_box.h -- shared EDIT-MODE behaviour for a single movable HUD box (Target, Player, and future
// single boxes). ONE implementation of : grab + drag, Shift/Ctrl axis-lock (debounced against the
// keyboard-hook flicker), screen-centre snap (engages centre-lock), zone push-out, mouse-wheel resize,
// and the full-screen alignment GRID + centre guides + axis rails drawn while dragging.
//
// Party / Alliance keep their OWN logic (magnetic edges, per-count grow-up) -- this is only for the
// standalone boxes, so they all behave identically without duplicating the maths.
#pragma once
#include "ui/widget.h"   // Frame, MouseState, u32

namespace aio {

// stable ids for EVERY draggable box (registry slots -> global box-vs-box collision : a dragged box is
// repelled by all the others so no two overlap, across BOTH drag systems). Standalone boxes + the three
// Party/Alliance clusters.
enum { EDITBOX_TARGET = 0, EDITBOX_PLAYER, EDITBOX_MINIMAP, EDITBOX_PARTY, EDITBOX_ALLY1, EDITBOX_ALLY2, EDITBOX_WS, EDITBOX_SKILLCHAIN, EDITBOX_TREASURE, EDITBOX_HATE, EDITBOX_POINTWATCH, EDITBOX_GRIMOIRE, EDITBOX_ZONETRACKER, EDITBOX_TIMERS, EDITBOX_TIMERS_R, EDITBOX_EQUIP, EDITBOX_COUNT };

// GLOBAL box-occupancy registry (shared by both drag systems). Publish a box's screen rect each frame it is
// drawn (`t` = f.t stamp -> a box not seen recently stops repelling). push_out shoves (ex,ey,ew,eh) out of
// every OTHER recently-published box (minimal translation). Party/Alliance publish their cluster rect here
// so the standalone boxes repel them and vice-versa.
void edit_box_publish(int id, float x, float y, float w, float h, float t);
void edit_box_push_out(int id, float curT, float& ex, float& ey, float ew, float eh);

// per-box drag state : each box instance (or its file) owns one.
struct EditBox {
    bool  dragging = false;
    float grabDX = 0.0f, grabDY = 0.0f;
    int   shiftHold = 0, ctrlHold = 0;      // frames the axis-lock stays engaged after the last key reading
    bool  dragShift = false, dragCtrl = false;
};

// Handle this frame's drag for the box at (px,py) size (W,H). Updates px/py to the live position and writes
// the persisted config through the references (posSet / fx,fy screen-fraction top-left / centreH,centreV /
// scale for the wheel). `zperm` = the box's zone-permission id (ZPERM_TARGET / ZPERM_HUB ...). Returns true
// while the box is being dragged -> the caller then draws edit_box_grid().
// hitX/hitY/hitW/hitH (optional) : a LARGER click/scroll hit area than the box itself -- e.g. a Player box
// with a DOCKED equipment grid, so clicking the grid grabs the box. Position math still uses px/py/W/H (the
// box). Omit (or hitW/hitH <= 0) to hit-test the box rect.
bool edit_box_drag(EditBox& st, int boxId, const Frame& f, float& px, float& py, float W, float H,
                   int zperm, bool& posSet, float& fx, float& fy, int& centerH, int& centerV, float& scale,
                   float hitX = 0.0f, float hitY = 0.0f, float hitW = 0.0f, float hitH = 0.0f);

// The alignment overlay : full-screen grid + centre guides (bright when centre-locked on that axis) + the
// amber axis-lock rails. Call it while dragging (edit_box_drag returned true), before the box chrome.
void edit_box_grid(u32 dev, const Frame& f, const EditBox& st, float px, float py, float W, float H,
                   bool centerH, bool centerV);

// The HOVER affordance : a strong neon selection outline + corner brackets around (px,py,W,H) -> "this box is
// grabbable". Drawn OUTSIDE the box footprint so it survives the box chrome. edit_box_drag calls it for the
// standalone boxes ; Party/Alliance (own drag logic) call it directly while hovered + not dragging.
void edit_box_hover_glow(u32 dev, const Frame& f, float px, float py, float W, float H);

// SHARED drag lock across ALL boxes (standalone + Party/Alliance) : only one may be grabbed at a time, and a
// box only starts a drag on a FRESH click. Party/Alliance (own drag code) route through these so the two
// systems can't both grab. `owner` = any stable per-box token (e.g. &state). grab() succeeds only when free.
bool edit_drag_busy();
bool edit_drag_grab(const void* owner);
void edit_drag_release(const void* owner);

} // namespace aio
