// hud_actmenu.cpp -- the target ACTION menu (Enter on a target), drawn by AioHUD in place of the client's "playermo".
//
// A MIRROR, not a replacement : the client keeps its own menu (parked off-screen by native_ui, Game UI -> NU_ACT),
// its keyboard / gamepad input and every action it runs ; this only draws what that menu shows -- its rows (the
// command codes it built for this target), its cursor, which rows open a submenu -- where the client would have put
// it. Data : native_action_menu() (model/native_ui.h), read once a frame. Unknown codes are shown by number, never
// guessed (the code table is in native_ui.h).
//
// Look (2026-11-16, v3 after the user's "un block avec des capsules dedans, texte plus petit, la main d'AioHUD") :
// the menus' theme box (Game UI -> Menus : UiConfig::muBox, Same-as-Party by default) holding one thin glass capsule per row, each with its command's vector icon ; the
// picked row carries the PARTY's own gold glass selection (clipped to the capsule, with its moving sweep) gliding
// from row to row, and AioHUD's selection HAND bobs in the box's left gutter on the party's rhythm ; a submenu's row
// carries a small triangle. A submenu open on top dims the whole menu.
#include "ui/hud.h"
#include "ui/hud_internal.h"
#include "ui/config_controls.h"   // tr()
#include "ui/box_style.h"         // draw_themed_box : the block
#include "ui/party.h"             // party_cursor / party_selframe / party_cursor_bob : AioHUD's hand and gold glass
#include "model/ui_config.h"
#include "model/native_ui.h"
#include "gfx/draw.h"
#include "gfx/font.h"
#include <stdio.h>
#include <math.h>
#include <windows.h>

namespace aio {

namespace {

enum Icon { IC_NONE, IC_SWORD, IC_SHEATHE, IC_BUBBLE, IC_BOLT, IC_STAR, IC_BAG, IC_LENS, IC_RETICLE, IC_SWAP, IC_DUO };
struct Cmd { unsigned short code; const char* en; const char* fr; bool sub; Icon icon; };
// proven 2026-11-16 (captures + the user's screenshots of the three menus) ; `sub` = the client draws an arrow
const Cmd CMDS[] = {
    {  1, "Attack",        "Attaquer",              false, IC_SWORD   },
    {  3, "Disengage",     "D\xC3\xA9sengager",     true,  IC_SHEATHE },
    {  5, "Chat",          "Discuter",              true,  IC_BUBBLE  },
    {  7, "Abilities",     "Aptitudes",             false, IC_BOLT    },
    {  8, "Magic",         "Magie",                 true,  IC_STAR    },
    {  9, "Items",         "Objets",                false, IC_BAG     },
    { 10, "Check",         "Examiner",              false, IC_LENS    },
    { 11, "Check",         "Examiner",              false, IC_LENS    },
    { 18, "Switch Target", "Changer de cible",      false, IC_RETICLE },
    { 19, "Trade",         "\xC3\x89""changer",     false, IC_SWAP    },
    { 25, "Trust",         "Alter Ego",             false, IC_DUO     },
};
const Cmd* cmd_of(unsigned short code) {
    for (int i = 0; i < (int)(sizeof(CMDS) / sizeof(CMDS[0])); ++i) if (CMDS[i].code == code) return &CMDS[i];
    return 0;
}

inline u32 with_alpha(u32 c, float a) {
    if (a >= 1.0f) return c; if (a <= 0.0f) return c & 0x00FFFFFFu;
    return (c & 0x00FFFFFFu) | ((u32)((float)(c >> 24) * a + 0.5f) << 24);
}
inline u32 mix(u32 a, u32 b, float t) {   // every channel, alpha included
    if (t <= 0.0f) return a; if (t >= 1.0f) return b;
    u32 r = 0;
    for (int sh = 0; sh < 32; sh += 8) {
        const int x = (a >> sh) & 255, y = (b >> sh) & 255;
        r |= (u32)(x + (int)((float)(y - x) * t + 0.5f)) << sh;
    }
    return r;
}
// an anti-aliased triangle (fill_poly_aa wants convex shapes : every icon below is built from convex pieces)
inline void tri(u32 dev, float x0, float y0, float x1, float y1, float x2, float y2, u32 c) {
    const float xy[6] = { x0, y0, x1, y1, x2, y2 }; fill_poly_aa(dev, xy, 3, c);
}
inline float ease_out(float t) { if (t <= 0.0f) return 0.0f; if (t >= 1.0f) return 1.0f; const float u = 1.0f - t; return 1.0f - u * u * u; }


// ---- the command icons : a few strokes in a z-sized square centred on (cx, cy) ----
void draw_icon(u32 dev, Icon ic, float cx, float cy, float z, float S, u32 c) {
    const float h = z * 0.5f, t = 1.5f * S;
    switch (ic) {
    case IC_SWORD:                                     // a blade on the diagonal, its guard and pommel
        seg_soft(dev, cx - h * 0.75f, cy + h * 0.75f, cx + h * 0.85f, cy - h * 0.85f, t, c);
        seg_soft(dev, cx - h * 0.70f, cy + h * 0.05f, cx - h * 0.05f, cy + h * 0.70f, t, c);
        disc(dev, cx - h * 0.85f, cy + h * 0.85f, 1.6f * S, c);
        break;
    case IC_SHEATHE:                                   // the blade going back into its scabbard
        seg_soft(dev, cx - h * 0.80f, cy + h * 0.80f, cx + h * 0.30f, cy - h * 0.30f, t, c);
        rrect_stroke(dev, cx - h * 0.05f, cy - h * 0.95f, h * 0.95f, h * 0.95f, h * 0.30f, c, 1.2f * S);
        break;
    case IC_BUBBLE: {                                  // a speech bubble and its tail
        const float bw = z * 0.92f, bh = z * 0.64f;
        rrect_stroke(dev, cx - bw * 0.5f, cy - h * 0.85f, bw, bh, bh * 0.42f, c, 1.3f * S);
        tri(dev, cx - h * 0.45f, cy - h * 0.85f + bh - 0.5f, cx - h * 0.05f, cy - h * 0.85f + bh - 0.5f, cx - h * 0.60f, cy + h * 0.80f, c);
        break; }
    case IC_BOLT:                                      // a lightning bolt : two triangles meeting in the middle
        tri(dev, cx + h * 0.30f, cy - h, cx - h * 0.60f, cy + h * 0.18f, cx + h * 0.18f, cy + h * 0.18f, c);
        tri(dev, cx - h * 0.18f, cy - h * 0.18f, cx + h * 0.60f, cy - h * 0.18f, cx - h * 0.30f, cy + h, c);
        break;
    case IC_STAR: {                                    // a four-point spark (two thin diamonds) and a small one
        const float o = h * 0.95f, i = h * 0.24f;
        const float v[8] = { cx, cy - o, cx + i, cy, cx, cy + o, cx - i, cy };
        const float w[8] = { cx - o, cy, cx, cy - i, cx + o, cy, cx, cy + i };
        fill_poly_aa(dev, v, 4, c); fill_poly_aa(dev, w, 4, c);
        disc(dev, cx + h * 0.80f, cy - h * 0.80f, 1.3f * S, c);
        break; }
    case IC_BAG:                                       // a pouch : body, neck tie, flap
        rrect(dev, cx - h * 0.78f, cy - h * 0.30f, h * 1.56f, h * 1.25f, h * 0.45f, c, c);
        rrect(dev, cx - h * 0.42f, cy - h * 0.82f, h * 0.84f, h * 0.42f, h * 0.18f, c, c);
        break;
    case IC_LENS:                                      // a magnifier
        rrect_stroke(dev, cx - h * 0.85f, cy - h * 0.85f, h * 1.25f, h * 1.25f, h * 0.625f, c, 1.4f * S);
        seg_soft(dev, cx + h * 0.30f, cy + h * 0.30f, cx + h * 0.90f, cy + h * 0.90f, 1.9f * S, c);
        break;
    case IC_RETICLE:                                   // a target reticle
        rrect_stroke(dev, cx - h * 0.62f, cy - h * 0.62f, h * 1.24f, h * 1.24f, h * 0.62f, c, 1.3f * S);
        seg_soft(dev, cx, cy - h, cx, cy - h * 0.40f, t, c); seg_soft(dev, cx, cy + h * 0.40f, cx, cy + h, t, c);
        seg_soft(dev, cx - h, cy, cx - h * 0.40f, cy, t, c); seg_soft(dev, cx + h * 0.40f, cy, cx + h, cy, t, c);
        disc(dev, cx, cy, 1.4f * S, c);
        break;
    case IC_SWAP:                                      // two arrows, opposite ways
        seg_soft(dev, cx - h * 0.85f, cy - h * 0.40f, cx + h * 0.55f, cy - h * 0.40f, t, c);
        tri(dev, cx + h * 0.95f, cy - h * 0.40f, cx + h * 0.40f, cy - h * 0.80f, cx + h * 0.40f, cy, c);
        seg_soft(dev, cx + h * 0.85f, cy + h * 0.45f, cx - h * 0.55f, cy + h * 0.45f, t, c);
        tri(dev, cx - h * 0.95f, cy + h * 0.45f, cx - h * 0.40f, cy + h * 0.05f, cx - h * 0.40f, cy + h * 0.85f, c);
        break;
    case IC_DUO:                                       // two companions
        disc(dev, cx - h * 0.38f, cy - h * 0.38f, h * 0.30f, c);
        rrect_topcaps(dev, cx - h * 0.92f, cy + h * 0.05f, h * 1.08f, h * 0.85f, h * 0.50f, c, c);
        disc(dev, cx + h * 0.45f, cy - h * 0.52f, h * 0.26f, with_alpha(c, 0.75f));
        rrect_topcaps(dev, cx + h * 0.02f, cy - h * 0.15f, h * 0.90f, h * 0.75f, h * 0.42f, with_alpha(c, 0.75f), with_alpha(c, 0.75f));
        break;
    default:
        disc(dev, cx, cy, h * 0.30f, c);
        break;
    }
}

struct State { float vis; float curY; unsigned lastMs, openMs; int lastN; bool wasOpen; float sel[16]; };
State g_st = { 0.0f, -1.0f, 0, 0, 0, false, { 0 } };
u32 g_handTex = 0;   // the party's selection-hand texture, handed in by the HUD each frame (device-loss managed there)
float g_hit[4] = { 0, 0, 0, 0 };   // the drawn block as screen FRACTIONS (x, y, w, h) : clicks inside never reach the game
int g_hoverRow = 0;                 // the row the mouse last pointed at (one hover event per change, not per frame)

} // namespace

void actmenu_set_hand_tex(u32 tex) { g_handTex = tex; }
bool actmenu_hit(float fx, float fy) { return g_hit[2] > 0.0f && fx >= g_hit[0] && fx < g_hit[0] + g_hit[2] && fy >= g_hit[1] && fy < g_hit[1] + g_hit[3]; }

void actmenu_draw(const Frame& f) {
    const UiConfig& C = ui_config();
    const NativeMenu& M = native_action_menu();
    g_hit[2] = 0.0f;                                                // published again below, only while drawn and open
    if (!(C.nuHide & NU_ACT) || !f.font || f.screenH <= 0.0f) { g_st.vis = 0.0f; g_st.curY = -1.0f; g_st.wasOpen = false; return; }

    const unsigned now = GetTickCount();
    float dt = g_st.lastMs ? (float)(now - g_st.lastMs) / 1000.0f : 0.0f; g_st.lastMs = now;
    if (dt > 0.1f) dt = 0.1f;
    const float kFast = 1.0f - expf(-dt * 22.0f), kSel = 1.0f - expf(-dt * 16.0f);
    static NativeMenu last;                       // the last menu seen : it fades out with its own rows
    if (M.open) last = M;
    if (M.open && !g_st.wasOpen) { g_st.openMs = now; for (int i = 0; i < 16; ++i) g_st.sel[i] = 0.0f; g_st.curY = -1.0f; }
    g_st.wasOpen = M.open;
    g_st.vis += ((M.open ? 1.0f : 0.0f) - g_st.vis) * kFast;
    if (g_st.vis < 0.01f) { g_st.vis = 0.0f; g_st.curY = -1.0f; return; }
    if (!last.n || last.uiW <= 0 || last.uiH <= 0) return;
    const NativeMenu& m = last;
    const float tOpen = ease_out(((float)(now - g_st.openMs) / 1000.0f) / 0.18f);
    const float A0 = g_st.vis * (m.focused ? 1.0f : 0.62f);   // a submenu open on top : this one steps back

    const u32 dev = f.dev;
    const float S = f.screenH / 1000.0f;
    Font* fo = f.font;
    // ---- metrics : thin capsules in a block, the hand in the block's left gutter ----
    const float tz = 11.5f * S, ow = 0.9f * S;
    const float capH = snap(19.0f * S), gap = snap(3.0f * S), icZ = 10.0f * S;
    const float pad = snap(6.0f * S), gutter = snap(22.0f * S);
    const bool icons = C.muLabel == 1;                             // Game UI -> Menus : text only (default) / icon + text
    const float capPadL = snap(9.0f * S), icW = icons ? snap(icZ + 7.0f * S) : 0.0f, capPadR = snap(8.0f * S), triW = snap(8.0f * S);
    char num[24];
    float wLab = 0.0f;
    for (int i = 0; i < m.n; ++i) {
        const Cmd* c = cmd_of(m.code[i]);
        const char* t = c ? tr(c->en, c->fr) : (sprintf(num, "#%d", m.code[i]), num);
        const float w = fo->measure(t, tz); if (w > wLab) wLab = w;
    }
    const float capW = snap(capPadL + icW + wLab + capPadR + triW);
    const float boxW = snap(gutter + capW + pad), boxH = snap((float)m.n * capH + (float)(m.n - 1) * gap + 2.0f * pad);
    const float sx = f.screenW / (float)m.uiW, sy = f.screenH / (float)m.uiH;   // the client's UI units -> backbuffer px
    // place : like the client, ABOVE THE CHAT -- the client opens it on top of its log (on the battle log when that one
    // sits above the talk log) ; AioHUD's chat boxes replace that log, so the menu sits on the HIGHER of the two that
    // are shown, flush with its left edge. No chat box shown : the client's own place (its bottom-left corner).
    float ax = (float)m.rx0 * sx, ay = (float)m.ry1 * sy;
    {
        float best = 1e9f;
        for (int b = 0; b < 2; ++b) {
            float x, y, w, h;
            if (chat_box_rect(b, x, y, w, h) && y < best) { best = y; ax = x; ay = y - snap(4.0f * S); }
        }
    }
    float bx = snap(ax), by = snap(ay - boxH + (1.0f - tOpen) * 8.0f * S);
    if (bx + boxW > f.screenW - 4.0f) bx = snap(f.screenW - 4.0f - boxW); if (bx < 4.0f) bx = 4.0f;
    if (by < 4.0f) by = 4.0f; if (by + boxH > f.screenH - 4.0f) by = snap(f.screenH - 4.0f - boxH);
    const float A = A0 * tOpen;
    if (A <= 0.0f) return;

    // ---- the MOUSE : hover moves the client's cursor, a click validates -- the client's own events (native_ui) ----
    if (M.open && f.screenW > 0.0f) {
        g_hit[0] = bx / f.screenW; g_hit[1] = by / f.screenH; g_hit[2] = boxW / f.screenW; g_hit[3] = boxH / f.screenH;
        const MouseState* ms = f.mouse;
        int row = 0;
        if (ms && ms->focused && M.focused && ms->x >= bx + gutter && ms->x < bx + gutter + capW) {
            const float ry = ms->y - (by + pad);
            if (ry >= 0.0f) { const int k = (int)(ry / (capH + gap)); if (k < m.n && ry - (float)k * (capH + gap) < capH) row = k + 1; }
        }
        if (row && row != g_hoverRow && row != M.cursor) native_action_hover(row);
        g_hoverRow = row;
        if (row && ms->clicked) native_action_pick(row);
    } else g_hoverRow = 0;

    // ---- the block : the theme's own box (Same-as-Party) ----
    const BoxStyle& bs = C.muBox;                                   // Game UI -> Menus : one theme for every redrawn menu
    draw_themed_box(dev, f.skin, bx, by, boxW, boxH, bs, A, S);

    const float cx0 = bx + gutter;                                  // capsules' left edge
    const float targetY = by + pad + (float)(m.cursor - 1) * (capH + gap);
    if (g_st.curY < 0.0f || g_st.lastN != m.n) g_st.curY = targetY;
    g_st.lastN = m.n;
    g_st.curY += (targetY - g_st.curY) * kFast;
    if (fabsf(targetY - g_st.curY) < 0.3f) g_st.curY = targetY;

    // ---- the capsules : quiet glass rows ; the picked one carries the party's gold glass selection (+ sweep) ----
    const float r = capH * 0.5f;
    for (int i = 0; i < m.n && i < 16; ++i) {
        const bool on = i + 1 == m.cursor;
        g_st.sel[i] += ((on ? 1.0f : 0.0f) - g_st.sel[i]) * kSel;
        if (fabsf((on ? 1.0f : 0.0f) - g_st.sel[i]) < 0.004f) g_st.sel[i] = on ? 1.0f : 0.0f;
        const float y = snap(by + pad + (float)i * (capH + gap));
        rrect(dev, cx0, y, capW, capH, r, with_alpha(0x5A1C2638u, A), with_alpha(0x5A0A0F18u, A));
        rrect_topcaps(dev, cx0 + 1.0f, y + 1.0f, capW - 2.0f, snap(capH * 0.45f), r - 1.0f, with_alpha(0x14FFFFFFu, A), with_alpha(0x00FFFFFFu, A));
        rrect_stroke(dev, cx0, y, capW, capH, r, with_alpha(0x38C8D6EAu, A), 1.0f);
    }
    {   // the selection glides between rows : clipped to its capsule, the exact party glass + sweep, a gold rim
        const float y = snap(g_st.curY);
        rrect_clip_begin(dev, cx0, y, capW, capH, r);
        party_selframe(dev, cx0, y, capW, capH, f.t, A, 0);
        rrect_clip_end(dev);
        rrect_stroke(dev, cx0, y, capW, capH, r, with_alpha(0xC8FFDA8Au, A), 1.0f);
    }
    // icons and submenu marks
    for (int i = 0; i < m.n && i < 16; ++i) {
        const float s = g_st.sel[i];
        const float y = snap(by + pad + (float)i * (capH + gap)), cy = y + capH * 0.5f;
        const Cmd* c = cmd_of(m.code[i]);
        if (icons) draw_icon(dev, c ? c->icon : IC_NONE, cx0 + capPadL + icZ * 0.5f, cy, icZ, S, with_alpha(mix(0xFF9FB0C6u, 0xFFFFF1CCu, s), A));
        if (c && c->sub) {
            const float tx = cx0 + capW - capPadR * 0.5f - triW * 0.5f, th = 3.2f * S;
            tri(dev, tx + th * 0.8f, cy, tx - th * 0.6f, cy - th, tx - th * 0.6f, cy + th, with_alpha(mix(0xA0B9C8DCu, 0xFFFFF1CCu, s), A));
        }
    }
    // ---- AioHUD's hand, in the gutter, bobbing on the party's rhythm ----
    {
        const float hand = snap(capH * 1.25f), cy = g_st.curY + capH * 0.5f;
        const float hx = bx + gutter * 0.5f - 1.0f * S + party_cursor_bob(f.t, hand);
        if (g_handTex) party_cursor(dev, g_handTex, hx, cy, hand, 0);
        else tri(dev, hx + 5.0f * S, cy, hx - 3.5f * S, cy - 5.0f * S, hx - 3.5f * S, cy + 5.0f * S, with_alpha(0xFFFFDA8Au, A));   // the texture not loaded yet
    }

    // ---- the labels, in ONE textured pass (begin() binds the glyph state -- a shape drawn before leaves the stage on
    // plain colour, which draws every glyph as a solid quad : the white bands of 2026-11-16) ----
    fo->begin(dev);
    for (int i = 0; i < m.n && i < 16; ++i) {
        const Cmd* c = cmd_of(m.code[i]);
        const char* t = c ? tr(c->en, c->fr) : (sprintf(num, "#%d", m.code[i]), num);
        const float s = g_st.sel[i];
        const float y = snap(by + pad + (float)i * (capH + gap));
        fo->draw_lc(dev, snap(cx0 + capPadL + icW), snap(y + capH * 0.5f), t, tz,
                    with_alpha(mix(0xFFC9D3E0u, 0xFFFFFFFFu, s), A), with_alpha(0xC8000000u, A), ow);
    }
}

} // namespace aio
