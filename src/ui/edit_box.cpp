// edit_box.cpp -- see edit_box.h. Extracted verbatim from the Target box's drag + alignment overlay so
// every standalone box shares one implementation.
#include "ui/edit_box.h"
#include "model/ui_config.h"   // guide_push_out, edit_shift/edit_ctrl, save_ui_config, ui_config().wheel, ZPERM_*
#include "gfx/draw.h"          // grad_quad
#include "gfx/d3d.h"
#include <math.h>

namespace aio {

// Only ONE box may be grabbed at a time, ACROSS every draggable box (the standalone Target/Player/Minimap
// AND the Party/Alliance clusters). Without this global owner, every box independently checks "over me" each
// frame, so sweeping the cursor over a second box mid-drag would start a SECOND drag. One shared token, so
// the two drag systems can't both grab at once.
static const void* g_editDragOwner = nullptr;
bool edit_drag_busy() { return g_editDragOwner != nullptr; }
bool edit_drag_grab(const void* owner) { if (g_editDragOwner || !owner) return false; g_editDragOwner = owner; return true; }
void edit_drag_release(const void* owner) { if (g_editDragOwner == owner) g_editDragOwner = nullptr; }

// Z-ORDER arbiter (see edit_box.h). One instance shared by every box. The LAST box to report `over` in a frame is
// the topmost (drawn last). Decisions read LAST frame's winner (g_zTopPrev), so a click grabs the box that was
// visibly on top rather than whichever widget happened to draw first.
static int g_zTopBox = -1, g_zTopPrev = -1; static float g_zTopT = -1.0f;
void edit_z_track(int boxId, bool over, float t) {
    if (t != g_zTopT) { g_zTopPrev = g_zTopBox; g_zTopBox = -1; g_zTopT = t; }   // new frame -> roll last frame's result forward
    if (over) g_zTopBox = boxId;                                                 // last over box wins = topmost
}
bool edit_z_topmost(int boxId) { return g_zTopPrev < 0 || boxId == g_zTopPrev; }   // <0 : first frame -> allow (self-corrects next frame)

// UI BLOCKER (the edit toolbar). Published each frame it's visible ; boxes check it (with their own f.t) so a
// stale rect (edit off, or the toolbar hidden during a drag) is inert past a short window.
static float g_blkX = 0, g_blkY = 0, g_blkW = 0, g_blkH = 0, g_blkT = -1e9f;
void edit_set_ui_blocker(float x, float y, float w, float h, float t) { g_blkX = x; g_blkY = y; g_blkW = w; g_blkH = h; g_blkT = t; }
bool edit_over_ui_blocker(float mx, float my, float t) {
    if (t - g_blkT > 0.25f) return false;                   // not published recently -> inert
    return mx >= g_blkX && mx < g_blkX + g_blkW && my >= g_blkY && my < g_blkY + g_blkH;
}

// Box-vs-box occupancy : every standalone box publishes its rect here each edit frame ; a dragged box is
// then shoved out of the OTHERS so two boxes can't overlap (same minimal-translation resolve as the zone
// push-out). Keyed by boxId. `t` = the frame time it last published -> a box not drawn recently (module
// toggled off) is treated as absent and stops repelling.
struct BoxSlot { float x, y, w, h, t; bool used; };
static BoxSlot g_boxReg[EDITBOX_COUNT] = {};

void edit_box_publish(int id, float x, float y, float w, float h, float t) {
    if (id < 0 || id >= EDITBOX_COUNT) return;
    g_boxReg[id].x = x; g_boxReg[id].y = y; g_boxReg[id].w = w; g_boxReg[id].h = h; g_boxReg[id].t = t; g_boxReg[id].used = true;
}
// shove (ex,ey,ew,eh) out of every OTHER recently-published box (fresh within ~0.2s), minimal-translation.
void edit_box_push_out(int id, float curT, float& ex, float& ey, float ew, float eh) {
    for (int it = 0; it < 4; ++it) {                              // a few passes to settle against several boxes
        bool moved = false;
        for (int i = 0; i < EDITBOX_COUNT; ++i) {
            if (i == id || !g_boxReg[i].used) continue;
            float dt = curT - g_boxReg[i].t; if (dt < 0.0f) dt = -dt;   // f.t wraps -> treat as stale, not present
            if (dt > 0.20f) continue;                            // not drawn recently -> a hidden box, ignore
            const BoxSlot& b = g_boxReg[i];
            if (ex < b.x + b.w && ex + ew > b.x && ey < b.y + b.h && ey + eh > b.y) {
                const float pL = b.x - (ex + ew), pR = (b.x + b.w) - ex;   // move left (neg) / right (pos)
                const float pU = b.y - (ey + eh), pD = (b.y + b.h) - ey;   // move up (neg) / down (pos)
                const float ax = (fabsf(pL) < fabsf(pR)) ? pL : pR;
                const float ay = (fabsf(pU) < fabsf(pD)) ? pU : pD;
                if (fabsf(ax) <= fabsf(ay)) ex += ax; else ey += ay;      // push along the shallower axis
                moved = true;
            }
        }
        if (!moved) break;
    }
}

static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// colour-quad render state for the grid quads (untextured diffuse). Delegates like every other module: this was
// the FIFTH hand-copied version and the one the v1.0.31 merge missed. It happened to still match, but that is
// exactly the drift vector d3d.h warns about -- the previous copies had already diverged before being merged.
static void color_state(u32 dev) { dColorQuadState(dev); }

// a centre GUIDE line : bright core + soft cyan glow when the box is snapped (on), else a faint hairline.
static void edit_vline(u32 dev, float cx, float sh, bool on) {
    if (on) { const u32 g1 = 0x2266D9FFu, g2 = 0x4866D9FFu;
        grad_quad(dev, cx - 6.0f, 0.0f, 12.0f, sh, g1, g1, g1, g1);
        grad_quad(dev, cx - 2.0f, 0.0f, 4.0f,  sh, g2, g2, g2, g2); }
    const u32 c = on ? 0xFFEAF7FFu : 0x4CA8D4F0u;
    grad_quad(dev, cx - (on ? 1.0f : 0.0f), 0.0f, on ? 2.0f : 1.0f, sh, c, c, c, c);
}
static void edit_hline(u32 dev, float cy, float sw, bool on) {
    if (on) { const u32 g1 = 0x2266D9FFu, g2 = 0x4866D9FFu;
        grad_quad(dev, 0.0f, cy - 6.0f, sw, 12.0f, g1, g1, g1, g1);
        grad_quad(dev, 0.0f, cy - 2.0f, sw, 4.0f,  g2, g2, g2, g2); }
    const u32 c = on ? 0xFFEAF7FFu : 0x4CA8D4F0u;
    grad_quad(dev, 0.0f, cy - (on ? 1.0f : 0.0f), sw, on ? 2.0f : 1.0f, c, c, c, c);
}

// HOVER affordance (edit mode) : a STRONG, immediate neon selection outline OUTSIDE the box border -> the
// industry "you can grab this" reaction. Every band sits fully outside the box footprint (rect grown by `o`,
// thickness `o`), so it survives the box chrome drawn afterwards. A wide bright halo + a crisp solid ring +
// four corner brackets (the classic "selected / movable" handles). A fast, shallow shimmer keeps it alive
// without the sluggish breathing that read as weak.
void edit_box_hover_glow(u32 dev, const Frame& f, float px, float py, float W, float H) {
    color_state(dev);
    const float shim = 0.85f + 0.15f * sinf(f.t * 9.0f);        // fast, shallow -> lively, not a slow pulse
    const u32   accent = 0x8AE0FFu;                             // bright cyan
    const float r = snap(7.0f);
    for (int k = 0; k < 7; ++k) {                              // WIDE bright outward halo
        const float o = 1.6f * (float)(k + 1);
        const float a = 210.0f * shim * (1.0f - (float)k / 7.0f);
        rrect_stroke(dev, px - o, py - o, W + 2.0f * o, H + 2.0f * o, r + o, ((u32)a << 24) | accent, snap(2.4f));
    }
    rrect_stroke(dev, px - 2.0f, py - 2.0f, W + 4.0f, H + 4.0f, r + 2.0f, 0xF2F2FBFFu, snap(2.2f));   // crisp solid selection ring
    // four CORNER BRACKETS just outside the corners -> the unmistakable "movable object" handles.
    const float b = snap(9.0f), t = snap(2.4f), off = snap(2.0f);
    const u32 hc = 0xFFEAF7FFu;
    const float L = px - off, R = px + W + off, T = py - off, B = py + H + off;
    // TL
    rrect(dev, L, T, b, t, 0.0f, hc, hc, 0.6f); rrect(dev, L, T, t, b, 0.0f, hc, hc, 0.6f);
    // TR
    rrect(dev, R - b, T, b, t, 0.0f, hc, hc, 0.6f); rrect(dev, R - t, T, t, b, 0.0f, hc, hc, 0.6f);
    // BL
    rrect(dev, L, B - t, b, t, 0.0f, hc, hc, 0.6f); rrect(dev, L, B - b, t, b, 0.0f, hc, hc, 0.6f);
    // BR
    rrect(dev, R - b, B - t, b, t, 0.0f, hc, hc, 0.6f); rrect(dev, R - t, B - b, t, b, 0.0f, hc, hc, 0.6f);
}

bool edit_box_drag(EditBox& st, int boxId, const Frame& f, float& px, float& py, float W, float H,
                   int zperm, bool& posSet, float& fx, float& fy, int& centerH, int& centerV, float& scale,
                   float hitX, float hitY, float hitW, float hitH) {
    if (!f.mouse || f.screenW <= 0.0f || f.screenH <= 0.0f) return st.dragging;
    const MouseState* m = f.mouse;
    edit_box_publish(boxId, px, py, W, H, f.t);   // register this box's footprint so the OTHER boxes are repelled by it
    // hit area : a supplied larger rect (docked satellite grid) or the box itself. grabDX/DY stay relative to
    // the box origin (px/py) so a grab anywhere in the hit area drags the box by the correct offset.
    const bool useHit = hitW > 0.0f && hitH > 0.0f;
    const float hx = useHit ? hitX : px, hy = useHit ? hitY : py, hw = useHit ? hitW : W, hh = useHit ? hitH : H;
    const bool over = m->x >= hx && m->x < hx + hw && m->y >= hy && m->y < hy + hh;
    edit_z_track(boxId, over, f.t);                         // feed the shared z-order arbiter (see edit_box.h)
    // Topmost under the cursor AND not under the toolbar (which owns its own clicks). A box already being dragged
    // keeps priority regardless (the toolbar is hidden mid-drag anyway).
    const bool topmost = (edit_z_topmost(boxId) && !edit_over_ui_blocker(m->x, m->y, f.t)) || st.dragging;
    // Grab only on a FRESH press (clicked), over the box, when it is the TOPMOST under the cursor and no OTHER box
    // already owns the drag. `clicked` (not held `down`) means sweeping over a box mid-drag can't grab it.
    if (m->clicked && !st.dragging && over && topmost && edit_drag_grab(&st)) {
        st.dragging = true;
        st.grabDX = m->x - px; st.grabDY = m->y - py; st.dragShift = st.dragCtrl = false;
        st.shiftHold = st.ctrlHold = 0;   // FRESH grab : clear any leftover axis-lock hold from a previous drag
    }
    if (over && topmost && !st.dragging && !edit_drag_busy())    // hovering a grabbable box (only the TOP one glows)
        edit_box_hover_glow(f.dev, f, px, py, W, H);
    if (st.dragging) {
        if (m->down) {
            float ex = m->x - st.grabDX, ey = m->y - st.grabDY;
            // AXIS-LOCK : Shift = horizontal only, Ctrl = vertical only. The modifier state is now read straight
            // from the key event's flags (aio_plugin_key -> edit_set_modifiers), which is stable and never stuck,
            // so we use it directly -- no more flicker-bridge / hold frames (which used to leave it locked).
            const bool shift = edit_shift(), ctrl = edit_ctrl();
            // FREE placement : Ctrl+Shift held TOGETHER (a combo that would otherwise just freeze both axes = useless)
            // or Alt when the game doesn't swallow it -> ignore axis-lock, box-vs-box repulsion AND keep-out zones.
            const bool freePlace = (shift && ctrl) || edit_alt();
            st.dragShift = shift && !freePlace; st.dragCtrl = ctrl && !freePlace; st.dragFree = freePlace;
            // AXIS-LOCK (only when NOT free) : Shift = horizontal only, Ctrl = vertical only. Freeze the locked axis
            // to the box's current position + re-sync that axis's grab offset each frame so a key release / flicker
            // never snaps the box to the mouse.
            if (!freePlace) {
                if (shift) { ey = py; st.grabDY = m->y - py; }
                if (ctrl)  { ex = px; st.grabDX = m->x - px; }
            }
            if (ex > f.screenW - W) ex = f.screenW - W; if (ex < 0.0f) ex = 0.0f;
            if (ey > f.screenH - H) ey = f.screenH - H; if (ey < 0.0f) ey = 0.0f;
            // SNAP to the screen centre (engages the centre-lock so it stays centred through a resize). Not in free mode.
            const float SNAP = snap(12.0f), ecx = (f.screenW - W) * 0.5f, ecy = (f.screenH - H) * 0.5f;
            bool snapH = !freePlace && !ctrl  && fabsf(ex - ecx) < SNAP;
            bool snapV = !freePlace && !shift && fabsf(ey - ecy) < SNAP;
            if (snapH) ex = ecx; if (snapV) ey = ecy;
            if (!freePlace) {   // free placement overrides ALL restrictions -> drop it anywhere, over any box or keep-out zone
                guide_push_out(zperm, f.screenW, f.screenH, ex, ey, W, H);   // repelled by any forbidding zone
                edit_box_push_out(boxId, f.t, ex, ey, W, H);                      // repelled by every OTHER box (no overlap)
            }
            if (ex > f.screenW - W) ex = f.screenW - W; if (ex < 0.0f) ex = 0.0f;
            if (ey > f.screenH - H) ey = f.screenH - H; if (ey < 0.0f) ey = 0.0f;
            if (fabsf(ex - ecx) > 0.5f) snapH = false;                   // a zone pushed it off centre -> lock didn't hold
            if (fabsf(ey - ecy) > 0.5f) snapV = false;
            posSet = true;
            // free mode moves BOTH axes -> persist both ; else only the un-locked axis updates.
            if (freePlace || !ctrl)  { fx = clampf(ex / f.screenW, 0.0f, 1.0f); centerH = snapH ? 1 : 0; }
            if (freePlace || !shift) { fy = clampf(ey / f.screenH, 0.0f, 1.0f); centerV = snapV ? 1 : 0; }
            px = snap(ex); py = snap(ey);                                // immediate feedback
        } else { st.dragging = false; st.dragShift = st.dragCtrl = false;                        // released -> persist once + free the lock
                 edit_drag_release(&st); save_ui_config(); }
    }
    if (over && topmost && ui_config().wheel != 0) {                    // wheel -> resize the whole box (0.5x .. 2.0x) ; only the TOP box under the cursor
        float s = scale + (float)ui_config().wheel * 0.05f;
        scale = s < 0.5f ? 0.5f : (s > 2.0f ? 2.0f : s);
        ui_config().wheel = 0;
    }
    return st.dragging;
}

void edit_box_grid(u32 dev, const Frame& f, const EditBox& st, float px, float py, float W, float H,
                   bool centerH, bool centerV) {
    if (f.screenW <= 1.0f || f.screenH <= 1.0f) return;
    color_state(dev);
    const float sw = f.screenW, sh = f.screenH;
    // two-tier grid : fine MINOR lines + brighter MAJOR lines every few cells (pro alignment feel).
    const u32 minor = 0x0CFFFFFFu, major = 0x1EFFFFFFu;
    const int NC = 48, NR = 27;
    for (int i = 1; i < NC; ++i) { const float gx = snap(sw * (float)i / NC); const u32 c = (i % 6 == 0) ? major : minor; grad_quad(dev, gx, 0.0f, 1.0f, sh, c, c, c, c); }
    for (int i = 1; i < NR; ++i) { const float gy = snap(sh * (float)i / NR); const u32 c = (i % 3 == 0) ? major : minor; grad_quad(dev, 0.0f, gy, sw, 1.0f, c, c, c, c); }
    edit_vline(dev, snap(sw * 0.5f), sh, centerH);                      // centre guides on top
    edit_hline(dev, snap(sh * 0.5f), sw, centerV);
    // AXIS-LOCK rails : a bright amber line along the constrained axis (also confirms the modifier is detected).
    if (st.dragShift) { const u32 c = 0xFFFFC24A, gl = 0x40FFC24A; const float cy = snap(py + H * 0.5f);
        grad_quad(dev, 0.0f, cy - 4.0f, sw, 8.0f, gl, gl, gl, gl); grad_quad(dev, 0.0f, cy - 1.0f, sw, 2.0f, c, c, c, c); }
    if (st.dragCtrl)  { const u32 c = 0xFFFFC24A, gl = 0x40FFC24A; const float cx = snap(px + W * 0.5f);
        grad_quad(dev, cx - 4.0f, 0.0f, 8.0f, sh, gl, gl, gl, gl); grad_quad(dev, cx - 1.0f, 0.0f, 2.0f, sh, c, c, c, c); }
    // FREE-placement cue (Alt) : a green frame around the WHOLE SCREEN (drawn on the full-screen grid layer, so the
    // box chrome -- painted AFTER the grid -- can't cover it). Unmistakable "no restrictions, drop it anywhere",
    // and doubles as an Alt-detected check : no green border => the plugin isn't seeing Alt held.
    if (st.dragFree) { const u32 c = 0xFF56E0A0u, gl = 0x2856E0A0u; const float t = snap(3.0f), h = snap(10.0f);
        grad_quad(dev, 0.0f, 0.0f, sw, h, gl, gl, 0x0056E0A0u, 0x0056E0A0u);                 // soft inner glow (top)
        grad_quad(dev, 0.0f, sh - h, sw, h, 0x0056E0A0u, 0x0056E0A0u, gl, gl);               //                 (bottom)
        grad_quad(dev, 0.0f, 0.0f, sw, t, c, c, c, c); grad_quad(dev, 0.0f, sh - t, sw, t, c, c, c, c);
        grad_quad(dev, 0.0f, 0.0f, t, sh, c, c, c, c); grad_quad(dev, sw - t, 0.0f, t, sh, c, c, c, c); }
}

} // namespace aio
