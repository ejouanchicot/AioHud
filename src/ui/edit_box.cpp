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

static inline float snap(float v) { return (float)(int)(v + 0.5f); }
static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// colour-quad render state for the grid quads (untextured diffuse).
static void color_state(u32 dev) {
    dSetVS(dev, FVF_XYZRHW_DIFFUSE);
    dSetRS(dev, D3DRS_ZENABLE, 0); dSetRS(dev, D3DRS_CULLMODE, D3DCULL_NONE); dSetRS(dev, D3DRS_LIGHTING, 0);
    dSetRS(dev, D3DRS_ALPHATESTENABLE, 0); dSetRS(dev, D3DRS_FOGENABLE, 0); dSetRS(dev, D3DRS_SPECULARENABLE, 0);
    dSetRS(dev, D3DRS_ALPHABLENDENABLE, 1); dSetRS(dev, D3DRS_SRCBLEND, D3DBLEND_SRCALPHA); dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    dSetRS(dev, D3DRS_BLENDOP, D3DBLENDOP_ADD); dSetTex(dev, 0, 0);
    dSetTSS(dev, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1); dSetTSS(dev, 0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    dSetTSS(dev, 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1); dSetTSS(dev, 0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    dSetTSS(dev, 1, D3DTSS_COLOROP, D3DTOP_DISABLE); dSetTSS(dev, 1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
}

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
    // Grab only on a FRESH press (clicked), over the box, when no OTHER box already owns the drag. Using
    // `clicked` (not held `down`) means sweeping over a box mid-drag can't grab it, and the owner lock
    // stops a second box being picked up even if two overlap at the click point.
    if (m->clicked && !st.dragging && over && edit_drag_grab(&st)) {
        st.dragging = true;
        st.grabDX = m->x - px; st.grabDY = m->y - py; st.dragShift = st.dragCtrl = false;
    }
    if (over && !st.dragging && !edit_drag_busy())               // hovering a grabbable box -> instant glow cue
        edit_box_hover_glow(f.dev, f, px, py, W, H);
    if (st.dragging) {
        if (m->down) {
            float ex = m->x - st.grabDX, ey = m->y - st.grabDY;
            // AXIS-LOCK : Shift = horizontal only, Ctrl = vertical only. The keyboard hook FLICKERS while the
            // mouse moves, so bridge it : engage instantly, release ~30 frames after the last true reading.
            const bool shiftRaw = edit_shift(), ctrlRaw = edit_ctrl();
            if (shiftRaw) st.shiftHold = 30; else if (st.shiftHold > 0) --st.shiftHold;
            if (ctrlRaw)  st.ctrlHold  = 30; else if (st.ctrlHold  > 0) --st.ctrlHold;
            const bool shift = st.shiftHold > 0, ctrl = st.ctrlHold > 0;
            st.dragShift = shift; st.dragCtrl = ctrl;
            // Freeze the locked axis to the box's current position AND re-sync that axis's grab offset each frame,
            // so releasing the key (or a flicker) never snaps the box to the mouse (no jump).
            if (shift) { ey = py; st.grabDY = m->y - py; }
            if (ctrl)  { ex = px; st.grabDX = m->x - px; }
            if (ex > f.screenW - W) ex = f.screenW - W; if (ex < 0.0f) ex = 0.0f;
            if (ey > f.screenH - H) ey = f.screenH - H; if (ey < 0.0f) ey = 0.0f;
            // SNAP to the screen centre (engages the centre-lock so it stays centred through a resize).
            const float SNAP = snap(12.0f), ecx = (f.screenW - W) * 0.5f, ecy = (f.screenH - H) * 0.5f;
            bool snapH = !ctrl  && fabsf(ex - ecx) < SNAP;
            bool snapV = !shift && fabsf(ey - ecy) < SNAP;
            if (snapH) ex = ecx; if (snapV) ey = ecy;
            guide_push_out(zperm, f.screenW, f.screenH, ex, ey, W, H);   // repelled by any forbidding zone
            edit_box_push_out(boxId, f.t, ex, ey, W, H);                      // repelled by every OTHER box (no overlap)
            if (ex > f.screenW - W) ex = f.screenW - W; if (ex < 0.0f) ex = 0.0f;
            if (ey > f.screenH - H) ey = f.screenH - H; if (ey < 0.0f) ey = 0.0f;
            if (fabsf(ex - ecx) > 0.5f) snapH = false;                   // a zone pushed it off centre -> lock didn't hold
            if (fabsf(ey - ecy) > 0.5f) snapV = false;
            posSet = true;
            if (!ctrl)  { fx = clampf(ex / f.screenW, 0.0f, 1.0f); centerH = snapH ? 1 : 0; }   // only the moved axis updates
            if (!shift) { fy = clampf(ey / f.screenH, 0.0f, 1.0f); centerV = snapV ? 1 : 0; }
            px = snap(ex); py = snap(ey);                                // immediate feedback
        } else { st.dragging = false; st.dragShift = st.dragCtrl = false;                        // released -> persist once + free the lock
                 edit_drag_release(&st); save_ui_config(); }
    }
    if (over && ui_config().wheel != 0) {                               // wheel -> resize the whole box (0.5x .. 2.0x)
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
}

} // namespace aio
