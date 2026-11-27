// edit_box.cpp -- see edit_box.h. Extracted verbatim from the Target box's drag + alignment overlay so
// every standalone box shares one implementation.
#include "ui/edit_box.h"
#include "model/ui_config.h"   // guide_push_out, edit_shift/edit_ctrl, save_ui_config, ui_config().wheel, ZPERM_*
#include "gfx/draw.h"          // grad_quad
#include "gfx/d3d.h"
#include <math.h>
#include <string.h>   // memmove : the undo stack

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


// ======== THE EDIT LAYOUT REDESIGN (2026-11-27) : identify, select, align, nudge, undo ========
// Every box is NAMED, one is SELECTED (a click on it, or its drag), a drag SNAPS to the other boxes' edges and centres
// (with the guide that caught it), and the keyboard moves the selected box (arrows : 1 px, Shift : 10 px), resets it
// (Delete), and undoes / redoes (Ctrl+Z / Ctrl+Y). The standalone boxes BIND their config fields here each frame (their
// edit_box_drag call) ; Party / Alliance keep their own drag and are selectable and named, not yet nudged.
const char* edit_box_name(int id, bool fr) {
    static const char* EN[EDITBOX_COUNT] = { "Target", "Player", "Minimap", "Party", "Alliance 1", "Alliance 2", "Arcade WS", "Skillchains",
        "Treasure Pool", "Hate List", "PointWatch", "Grimoire", "Zone Tracker", "Timers", "Recasts", "Equipment", "EmpyPop", "Debuffs",
        "Absorb-TP", "Pet", "Chat", "Battle log" };
    static const char* FR[EDITBOX_COUNT] = { "Cible", "Joueur", "Minicarte", "Groupe", "Alliance 1", "Alliance 2", "Arcade WS", "Skillchains",
        "Tr\xC3\xA9sor", "Liste de haine", "PointWatch", "Grimoire", "Zone Tracker", "Timers", "Recasts", "\xC3\x89quipement", "EmpyPop", "Debuffs",
        "Absorb-TP", "Pet", "Chat", "Combat" };
    return (id >= 0 && id < EDITBOX_COUNT) ? (fr ? FR[id] : EN[id]) : "";
}
struct Bind { float* fx; float* fy; bool* posSet; int* cH; int* cV; float* scale; float t; };
static Bind g_bind[EDITBOX_COUNT] = {};
static int g_sel = -1;
struct Undo { int id; float fx, fy, scale; bool posSet; int cH, cV; };
static const int UNDO_N = 64;
static Undo g_undo[UNDO_N]; static int g_undoN = 0;
static Undo g_redo[UNDO_N]; static int g_redoN = 0;
static bool bound(int id) { return id >= 0 && id < EDITBOX_COUNT && g_bind[id].fx && g_bind[id].fy; }
// a pointer INTO the live config : the only kind the undo may keep. box_edit hands edit_box_drag LOCAL copies (its
// grow-anchored boxes convert on the way back) -- keeping those, an undo wrote into a dead stack frame (2026-11-27).
static bool in_cfg(const void* p) { const char* b = (const char*)&ui_config(); return p && (const char*)p >= b && (const char*)p < b + sizeof(UiConfig); }
static Undo snap_of(int id) {
    const Bind& b = g_bind[id]; Undo u; u.id = id; u.fx = *b.fx; u.fy = *b.fy; u.posSet = b.posSet ? *b.posSet : true;
    u.cH = b.cH ? *b.cH : 0; u.cV = b.cV ? *b.cV : 0; u.scale = b.scale ? *b.scale : 1.0f; return u;
}
static void apply_undo(const Undo& u) {
    if (!bound(u.id)) return;
    const Bind& b = g_bind[u.id];
    *b.fx = u.fx; *b.fy = u.fy; if (b.posSet) *b.posSet = u.posSet; if (b.cH) *b.cH = u.cH; if (b.cV) *b.cV = u.cV; if (b.scale) *b.scale = u.scale;
    g_sel = u.id; save_ui_config();
}
static void push(Undo* st, int& n, const Undo& u) { if (n == UNDO_N) { memmove(st, st + 1, sizeof(Undo) * (UNDO_N - 1)); --n; } st[n++] = u; }
static void undo_record(int id) { if (!bound(id)) return; push(g_undo, g_undoN, snap_of(id)); g_redoN = 0; }
int  edit_box_selected() { return g_sel; }
void edit_box_select(int id) { g_sel = (id >= 0 && id < EDITBOX_COUNT) ? id : -1; }
bool edit_box_can_undo() { return g_undoN > 0; }
bool edit_box_can_redo() { return g_redoN > 0; }
void edit_box_undo() { if (!g_undoN) return; const Undo u = g_undo[--g_undoN]; if (bound(u.id)) push(g_redo, g_redoN, snap_of(u.id)); apply_undo(u); }
void edit_box_redo() { if (!g_redoN) return; const Undo u = g_redo[--g_redoN]; if (bound(u.id)) push(g_undo, g_undoN, snap_of(u.id)); apply_undo(u); }

// the KEYS (from the key hook, aio_plugin_key) : accumulated, applied by the selected box's own edit_box_drag call
static volatile int g_nudgeX = 0, g_nudgeY = 0, g_resetReq = 0;
static float g_lastNudgeT = -10.0f, g_lastWheelT = -10.0f;
static void snap_to_boxes(int id, float t, float& ex, float& ey, float W, float H, bool lockX, bool lockY);
static int g_setId = -1; static float g_setX = 0.0f, g_setY = 0.0f;
void edit_box_set_pos(int id, float x, float y) { g_setId = id; g_setX = x; g_setY = y; }
void edit_box_bind(int id, float* fx, float* fy, bool* posSet, float* scale, float t) {
    if (id < 0 || id >= EDITBOX_COUNT) return;
    if (!in_cfg(fx) || !in_cfg(fy)) return;
    Bind& b = g_bind[id]; b.fx = fx; b.fy = fy; b.posSet = in_cfg(posSet) ? posSet : nullptr; b.cH = nullptr; b.cV = nullptr; b.scale = in_cfg(scale) ? scale : nullptr; b.t = t;
}
void edit_box_snap(int id, float t, float& ex, float& ey, float W, float H) { snap_to_boxes(id, t, ex, ey, W, H, false, false); }
void edit_box_grabbed(int id) { undo_record(id); g_sel = id; }
void edit_box_wheeled(int id, float t) { if (t - g_lastWheelT > 0.8f || t < g_lastWheelT) undo_record(id); g_lastWheelT = t; g_sel = id; }
bool edit_box_take(int id, float t, int& dx, int& dy, bool& set, float& sx, float& sy, bool& reset) {
    dx = dy = 0; set = reset = false; sx = sy = 0.0f;
    if (id != g_sel || !bound(id)) return false;
    if (g_nudgeX || g_nudgeY) {
        dx = g_nudgeX; dy = g_nudgeY; g_nudgeX = 0; g_nudgeY = 0;
        if (t - g_lastNudgeT > 0.8f || t < g_lastNudgeT) undo_record(id);
        g_lastNudgeT = t;
    }
    if (g_setId == id) { set = true; sx = g_setX; sy = g_setY; g_setId = -1; undo_record(id); }
    if (g_resetReq) { reset = true; g_resetReq = 0; undo_record(id); }
    return dx || dy || set || reset;
}
bool edit_box_key(int dik, bool shift, bool ctrl) {
    const int step = shift ? 10 : 1;
    switch (dik) {
    case 0xCB: g_nudgeX -= step; return true;            // Left
    case 0xCD: g_nudgeX += step; return true;            // Right
    case 0xC8: g_nudgeY -= step; return true;            // Up
    case 0xD0: g_nudgeY += step; return true;            // Down
    case 0xD3: g_resetReq = 1; return true;              // Delete : the box back to its default place and size
    case 0x01: g_sel = -1; return true;                  // Escape : nothing selected
    case 0x2C: if (ctrl) { if (shift) edit_box_redo(); else edit_box_undo(); return true; } return false;   // Ctrl+Z / Ctrl+Shift+Z
    case 0x15: if (ctrl) { edit_box_redo(); return true; } return false;                                   // Ctrl+Y
    }
    return false;
}

// the GUIDES a drag snapped to this frame (drawn by the overlay)
static EditGuide g_guide[4]; static int g_guideN = 0; static float g_guideT = -1.0f;
int edit_box_guides(EditGuide* out, int max, float t) {
    if (t - g_guideT > 0.05f || t < g_guideT) return 0;
    int n = g_guideN < max ? g_guideN : max; for (int i = 0; i < n; ++i) out[i] = g_guide[i]; return n;
}
bool edit_box_rect(int id, float t, float& x, float& y, float& w, float& h) {
    if (id < 0 || id >= EDITBOX_COUNT || !g_boxReg[id].used) return false;
    float dt = t - g_boxReg[id].t; if (dt < 0.0f) dt = -dt; if (dt > 0.20f) return false;
    x = g_boxReg[id].x; y = g_boxReg[id].y; w = g_boxReg[id].w; h = g_boxReg[id].h; return true;
}
int edit_box_hovered() { return g_zTopBox; }   // this frame's topmost box under the cursor (the overlay draws after the boxes)

// SNAP to the other boxes : the dragged box's left / centre / right against theirs (and abutting edge to edge), the
// same vertically ; within a few px it locks and the line that caught it is recorded for the overlay
static void snap_to_boxes(int id, float t, float& ex, float& ey, float W, float H, bool lockX, bool lockY) {
    const float TH = snap(7.0f);
    float bestDX = TH + 1.0f, bestDY = TH + 1.0f, gx = 0, gy = 0; int bx = -1, by = -1;
    const float me[3] = { ex, ex + W * 0.5f, ex + W }, mey[3] = { ey, ey + H * 0.5f, ey + H };
    for (int i = 0; i < EDITBOX_COUNT; ++i) {
        if (i == id || !g_boxReg[i].used) continue;
        float dt = t - g_boxReg[i].t; if (dt < 0.0f) dt = -dt; if (dt > 0.20f) continue;
        const BoxSlot& b = g_boxReg[i];
        const float tx[3] = { b.x, b.x + b.w * 0.5f, b.x + b.w }, ty[3] = { b.y, b.y + b.h * 0.5f, b.y + b.h };
        for (int a = 0; a < 3; ++a) for (int c = 0; c < 3; ++c) {
            if ((a == 1) != (c == 1)) continue;                                   // centre to centre ; edge to edge
            if (!lockX) { const float d = tx[c] - me[a]; if (fabsf(d) < fabsf(bestDX)) { bestDX = d; gx = tx[c]; bx = i; } }
            if (!lockY) { const float d = ty[c] - mey[a]; if (fabsf(d) < fabsf(bestDY)) { bestDY = d; gy = ty[c]; by = i; } }
        }
    }
    g_guideN = 0; g_guideT = t;
    if (bx >= 0 && fabsf(bestDX) <= TH) {
        ex += bestDX; const BoxSlot& b = g_boxReg[bx];
        const float a0 = (ey < b.y ? ey : b.y), a1 = ((ey + H) > (b.y + b.h) ? (ey + H) : (b.y + b.h));
        EditGuide g = { true, gx, a0, a1 }; g_guide[g_guideN++] = g;
    }
    if (by >= 0 && fabsf(bestDY) <= TH) {
        ey += bestDY; const BoxSlot& b = g_boxReg[by];
        const float a0 = (ex < b.x ? ex : b.x), a1 = ((ex + W) > (b.x + b.w) ? (ex + W) : (b.x + b.w));
        EditGuide g = { false, gy, a0, a1 }; g_guide[g_guideN++] = g;
    }
}
bool edit_box_bound(int id) { return bound(id); }
void edit_box_frame_end() { if (!bound(g_sel)) { g_nudgeX = 0; g_nudgeY = 0; g_resetReq = 0; } }

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
void edit_box_hover_glow(u32 dev, const Frame& f, float px, float py, float W, float H) {   // retired 2026-11-27 : the edit overlay (config_page) frames and names the hovered box
    (void)dev; (void)f; (void)px; (void)py; (void)W; (void)H;
}

bool edit_box_drag(EditBox& st, int boxId, const Frame& f, float& px, float& py, float W, float H,
                   int zperm, bool& posSet, float& fx, float& fy, int& centerH, int& centerV, float& scale,
                   float hitX, float hitY, float hitW, float hitH) {
    if (!f.mouse || f.screenW <= 0.0f || f.screenH <= 0.0f) return st.dragging;
    const MouseState* m = f.mouse;
    edit_box_publish(boxId, px, py, W, H, f.t);   // register this box's footprint so the OTHER boxes are repelled by it
    if (boxId >= 0 && boxId < EDITBOX_COUNT && in_cfg(&fx) && in_cfg(&fy)) {   // only the config's own fields (box_edit binds its own, first)
        Bind& bd = g_bind[boxId]; bd.fx = &fx; bd.fy = &fy; bd.posSet = in_cfg(&posSet) ? &posSet : nullptr;
        bd.cH = in_cfg(&centerH) ? &centerH : nullptr; bd.cV = in_cfg(&centerV) ? &centerV : nullptr; bd.scale = in_cfg(&scale) ? &scale : nullptr; bd.t = f.t; }
    // the KEYS, on the selected box : arrows nudge (1 / 10 px), Delete puts it back at its default place and size
    if (boxId == g_sel && !st.dragging) {
        const int nx = g_nudgeX, ny = g_nudgeY;
        if (nx || ny) {
            g_nudgeX = 0; g_nudgeY = 0;
            if (f.t - g_lastNudgeT > 0.8f || f.t < g_lastNudgeT) undo_record(boxId);   // one undo step per burst of presses
            g_lastNudgeT = f.t;
            float ex = px + (float)nx, ey = py + (float)ny;
            if (ex > f.screenW - W) ex = f.screenW - W; if (ex < 0.0f) ex = 0.0f;
            if (ey > f.screenH - H) ey = f.screenH - H; if (ey < 0.0f) ey = 0.0f;
            posSet = true;
            if (nx) { fx = clampf(ex / f.screenW, 0.0f, 1.0f); centerH = 0; }
            if (ny) { fy = clampf(ey / f.screenH, 0.0f, 1.0f); centerV = 0; }
            px = snap(ex); py = snap(ey); save_ui_config();
        }
        if (g_setId == boxId) {   // a typed position : one undo step
            g_setId = -1; undo_record(boxId);
            float ex = g_setX, ey = g_setY;
            if (ex > f.screenW - W) ex = f.screenW - W; if (ex < 0.0f) ex = 0.0f;
            if (ey > f.screenH - H) ey = f.screenH - H; if (ey < 0.0f) ey = 0.0f;
            posSet = true; fx = clampf(ex / f.screenW, 0.0f, 1.0f); fy = clampf(ey / f.screenH, 0.0f, 1.0f); centerH = 0; centerV = 0;
            px = snap(ex); py = snap(ey); save_ui_config();
        }
        if (g_resetReq) { g_resetReq = 0; undo_record(boxId); posSet = false; centerH = 0; centerV = 0; scale = 1.0f; save_ui_config(); }
    }
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
        undo_record(boxId); g_sel = boxId;   // the redesign : a grab selects the box and opens an undo step
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
            if (!freePlace) snap_to_boxes(boxId, f.t, ex, ey, W, H, ctrl, shift);   // the other boxes' edges and centres (guides drawn by the overlay)
            else g_guideN = 0;
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
        if (f.t - g_lastWheelT > 0.8f || f.t < g_lastWheelT) undo_record(boxId);   // one undo step per burst of notches
        g_lastWheelT = f.t; g_sel = boxId;
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
