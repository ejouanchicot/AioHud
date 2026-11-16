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
// carries a small triangle. A submenu open on top dims the whole menu. Its SUBMENUS (Magic categories, Abilities) are
// drawn the same way beside the picked row (native_sub_menu) ; every open is smooth but near-instant (~70-80 ms).
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
#include <string.h>
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

u32 g_handTex = 0;   // the party's selection-hand texture, handed in by the HUD each frame (device-loss managed there)
float g_hit[2][4] = { { 0, 0, 0, 0 }, { 0, 0, 0, 0 } };   // the drawn blocks (menu, submenu) as screen FRACTIONS : their clicks never reach the game

// ---- the action menu's submenus (native_sub_menu) : labels by ENTRY index (1-based), from their help texts ----
struct SubRow { const char* en; const char* fr; bool sub; Icon icon; };   // sub : none -- each row opens its LIST, no arrow in the client
const SubRow MAGSELEC[] = {
    { "White Magic", "Magie blanche",            false, IC_STAR },
    { "Black Magic", "Magie noire",              false, IC_STAR },
    { "Songs",       "Chants",                   false, IC_STAR },
    { "Ninjutsu",    "Ninjutsu",                 false, IC_STAR },
    { "Summoning",   "Invocation",               false, IC_STAR },
    { "Blue Magic",  "Magie bleue",              false, IC_STAR },
    { "Geomancy",    "G\xC3\xA9omancie",         false, IC_STAR },
};
const SubRow ABISELEC[] = {
    { "Job Abilities", "Aptitudes de job",       false, IC_BOLT    },
    { "Weapon Skills", "Weapon Skills",          false, IC_SWORD   },
    { "Ranged Attack", "Attaque \xC3\xA0 distance", false, IC_RETICLE },
    { "Pet Commands",  "Ordres au familier",     false, IC_DUO     },
    { "Mount",         "Monture",                false, IC_NONE    },
};
const SubRow* sub_row(const char* menu, int entry) {
    if (!strcmp(menu, "magselec") && entry >= 1 && entry <= 7) return &MAGSELEC[entry - 1];
    if (!strcmp(menu, "abiselec") && entry >= 1 && entry <= 5) return &ABISELEC[entry - 1];
    return 0;
}

// ---- ONE block : the menus' theme box, thin capsules, the party's gold glass on the picked row, the hand ----
struct RowSpec { const char* label; Icon icon; bool sub; bool off; };
struct Block {
    float vis = 0.0f, curY = -1.0f; unsigned openMs = 0; int lastN = 0; bool wasOpen = false; float sel[16] = { 0 };
    float x = 0, y = 0, w = 0, h = 0;           // drawn rect this frame (w = 0 : not drawn)
    float rowY0 = 0, rowStep = 0, capX = 0, capW = 0, capH = 0;
    int hoverRow = 0;
};
Block g_main, g_subB;
unsigned g_lastMs = 0;

struct Metrics { float S, tz, ow, capH, gap, icZ, pad, gutter, capPadL, icW, capPadR, triW; bool icons; };
Metrics metrics(const Frame& f) {
    Metrics k; k.S = f.screenH / 1000.0f; const float S = k.S;
    k.tz = 11.5f * S; k.ow = 0.9f * S; k.capH = snap(19.0f * S); k.gap = snap(3.0f * S); k.icZ = 10.0f * S;
    k.pad = snap(6.0f * S); k.gutter = snap(22.0f * S); k.icons = ui_config().muLabel == 1;
    k.capPadL = snap(9.0f * S); k.icW = k.icons ? snap(k.icZ + 7.0f * S) : 0.0f; k.capPadR = snap(8.0f * S); k.triW = snap(8.0f * S);
    return k;
}
// the block's size for these rows
void block_size(const Frame& f, const Metrics& k, const RowSpec* rows, int n, float& w, float& h) {
    float wLab = 0.0f;
    for (int i = 0; i < n; ++i) { const float lw = f.font->measure(rows[i].label, k.tz); if (lw > wLab) wLab = lw; }
    const float capW = snap(k.capPadL + k.icW + wLab + k.capPadR + k.triW);
    w = snap(k.gutter + capW + k.pad); h = snap((float)n * k.capH + (float)(n - 1) * k.gap + 2.0f * k.pad);
}
// open / close easing ; false when nothing is to be drawn
bool block_ease(Block& b, bool open, unsigned now, float dt) {
    const float kFast = 1.0f - expf(-dt * 45.0f);   // ~70 ms to settle : smooth, never in the way
    if (open && !b.wasOpen) { b.openMs = now; for (int i = 0; i < 16; ++i) b.sel[i] = 0.0f; b.curY = -1.0f; }
    b.wasOpen = open;
    b.vis += ((open ? 1.0f : 0.0f) - b.vis) * kFast;
    if (b.vis < 0.01f) { b.vis = 0.0f; b.curY = -1.0f; b.w = 0.0f; return false; }
    return true;
}
// draw at (bx, by) ; returns the row (1-based) under the mouse, 0 none
int block_draw(const Frame& f, const Metrics& k, Block& b, const RowSpec* rows, int n, int cursorRow, bool focused,
               float bx, float by, float bw, float bh, float A, float dt) {
    const u32 dev = f.dev; const float S = k.S;
    const float kFast = 1.0f - expf(-dt * 30.0f), kSel = 1.0f - expf(-dt * 24.0f);   // the selection glides, quickly
    const float capW = bw - k.gutter - k.pad, cx0 = bx + k.gutter, step = k.capH + k.gap, r = k.capH * 0.5f;
    b.x = bx; b.y = by; b.w = bw; b.h = bh; b.rowY0 = by + k.pad; b.rowStep = step; b.capX = cx0; b.capW = capW; b.capH = k.capH;

    draw_themed_box(dev, f.skin, bx, by, bw, bh, ui_config().muBox, A, S);   // Game UI -> Menus : one theme for all
    const bool hasCur = cursorRow >= 1 && cursorRow <= n;
    if (hasCur) {
        const float targetY = by + k.pad + (float)(cursorRow - 1) * step;
        if (b.curY < 0.0f || b.lastN != n) b.curY = targetY;
        b.curY += (targetY - b.curY) * kFast;
        if (fabsf(targetY - b.curY) < 0.3f) b.curY = targetY;
    }
    b.lastN = n;
    for (int i = 0; i < n && i < 16; ++i) {
        const bool on = i + 1 == cursorRow;
        b.sel[i] += ((on ? 1.0f : 0.0f) - b.sel[i]) * kSel;
        if (fabsf((on ? 1.0f : 0.0f) - b.sel[i]) < 0.004f) b.sel[i] = on ? 1.0f : 0.0f;
        const float y = snap(by + k.pad + (float)i * step);
        const float a = A * (rows[i].off ? 0.55f : 1.0f);
        rrect(dev, cx0, y, capW, k.capH, r, with_alpha(0x5A1C2638u, a), with_alpha(0x5A0A0F18u, a));
        rrect_topcaps(dev, cx0 + 1.0f, y + 1.0f, capW - 2.0f, snap(k.capH * 0.45f), r - 1.0f, with_alpha(0x14FFFFFFu, a), with_alpha(0x00FFFFFFu, a));
        rrect_stroke(dev, cx0, y, capW, k.capH, r, with_alpha(0x38C8D6EAu, a), 1.0f);
    }
    if (hasCur) {   // the selection glides between rows : clipped to its capsule, the exact party glass + sweep, a gold rim
        const float y = snap(b.curY);
        rrect_clip_begin(dev, cx0, y, capW, k.capH, r);
        party_selframe(dev, cx0, y, capW, k.capH, f.t, A, 0);
        rrect_clip_end(dev);
        rrect_stroke(dev, cx0, y, capW, k.capH, r, with_alpha(0xC8FFDA8Au, A), 1.0f);
    }
    for (int i = 0; i < n && i < 16; ++i) {
        const float s = b.sel[i], a = A * (rows[i].off ? 0.55f : 1.0f);
        const float cy = snap(by + k.pad + (float)i * step) + k.capH * 0.5f;
        if (k.icons) draw_icon(dev, rows[i].icon, cx0 + k.capPadL + k.icZ * 0.5f, cy, k.icZ, S, with_alpha(mix(0xFF9FB0C6u, 0xFFFFF1CCu, s), a));
        if (rows[i].sub) {
            const float tx = cx0 + capW - k.capPadR * 0.5f - k.triW * 0.5f, th = 3.2f * S;
            tri(dev, tx + th * 0.8f, cy, tx - th * 0.6f, cy - th, tx - th * 0.6f, cy + th, with_alpha(mix(0xA0B9C8DCu, 0xFFFFF1CCu, s), a));
        }
    }
    if (hasCur && focused) {   // AioHUD's hand, in the gutter of the menu being driven, bobbing on the party's rhythm
        const float hand = snap(k.capH * 1.25f), cy = b.curY + k.capH * 0.5f;
        const float hx = bx + k.gutter * 0.5f - 1.0f * S + party_cursor_bob(f.t, hand);
        if (g_handTex) party_cursor(dev, g_handTex, hx, cy, hand, 0);
        else tri(dev, hx + 5.0f * S, cy, hx - 3.5f * S, cy - 5.0f * S, hx - 3.5f * S, cy + 5.0f * S, with_alpha(0xFFFFDA8Au, A));
    }
    // the labels, in ONE textured pass (begin() binds the glyph state -- a shape drawn before leaves the stage on plain
    // colour, which draws every glyph as a solid quad : the white bands of 2026-11-16)
    Font* fo = f.font;
    fo->begin(dev);
    for (int i = 0; i < n && i < 16; ++i) {
        const float s = b.sel[i], a = A * (rows[i].off ? 0.5f : 1.0f);
        const float y = snap(by + k.pad + (float)i * step);
        fo->draw_lc(dev, snap(cx0 + k.capPadL + k.icW), snap(y + k.capH * 0.5f), rows[i].label, k.tz,
                    with_alpha(mix(0xFFC9D3E0u, 0xFFFFFFFFu, s), a), with_alpha(0xC8000000u, a), k.ow);
    }
    // the row under the mouse (the menu being driven only)
    const MouseState* ms = f.mouse;
    if (!(ms && ms->focused && focused && ms->x >= cx0 && ms->x < cx0 + capW)) return 0;
    const float ry = ms->y - (by + k.pad);
    if (ry < 0.0f) return 0;
    const int row = (int)(ry / step);
    return (row < n && ry - (float)row * step < k.capH) ? row + 1 : 0;
}

} // namespace

void actmenu_set_hand_tex(u32 tex) { g_handTex = tex; }
bool actmenu_hit(float fx, float fy) {
    for (int i = 0; i < 2; ++i) {
        const float* h = g_hit[i];
        if (h[2] > 0.0f && fx >= h[0] && fx < h[0] + h[2] && fy >= h[1] && fy < h[1] + h[3]) return true;
    }
    return false;
}

void actmenu_draw(const Frame& f) {
    const UiConfig& C = ui_config();
    const NativeMenu& M = native_action_menu();
    const NativeEntryMenu& SM = native_sub_menu();
    g_hit[0][2] = g_hit[1][2] = 0.0f;                               // published again below, only while drawn and open
    if (!(C.nuHide & NU_ACT) || !f.font || f.screenH <= 0.0f || f.screenW <= 0.0f) {
        g_main.vis = g_subB.vis = 0.0f; g_main.wasOpen = g_subB.wasOpen = false; g_main.w = g_subB.w = 0.0f; return;
    }
    const unsigned now = GetTickCount();
    float dt = g_lastMs ? (float)(now - g_lastMs) / 1000.0f : 0.0f; g_lastMs = now;
    if (dt > 0.1f) dt = 0.1f;
    const Metrics k = metrics(f);
    const float S = k.S;

    // ======== the action menu ========
    static NativeMenu last;                       // the last menu seen : it fades out with its own rows
    if (M.open) last = M;
    if (!block_ease(g_main, M.open, now, dt) || !last.n || last.uiW <= 0 || last.uiH <= 0) { g_subB.w = 0.0f; return; }
    const NativeMenu& m = last;
    RowSpec rows[16]; char nums[16][12];
    for (int i = 0; i < m.n && i < 16; ++i) {
        const Cmd* c = cmd_of(m.code[i]);
        if (c) { rows[i].label = tr(c->en, c->fr); rows[i].icon = c->icon; rows[i].sub = c->sub; }
        else { sprintf(nums[i], "#%d", m.code[i]); rows[i].label = nums[i]; rows[i].icon = IC_NONE; rows[i].sub = false; }
        rows[i].off = false;
    }
    float bw, bh; block_size(f, k, rows, m.n, bw, bh);
    const float tOpen = ease_out(((float)(now - g_main.openMs) / 1000.0f) / 0.08f);
    // place : like the client, ABOVE THE CHAT -- the client opens it on top of its log (on the battle log when that one
    // sits above the talk log) ; AioHUD's chat boxes replace that log, so the menu sits on the HIGHER of the two that
    // are shown, flush with its left edge. No chat box shown : the client's own place (its bottom-left corner).
    const float sx = f.screenW / (float)m.uiW, sy = f.screenH / (float)m.uiH;   // the client's UI units -> backbuffer px
    float ax = (float)m.rx0 * sx, ay = (float)m.ry1 * sy;
    {
        float best = 1e9f;
        for (int b = 0; b < 2; ++b) {
            float x, y, w, h;
            if (chat_box_rect(b, x, y, w, h) && y < best) { best = y; ax = x; ay = y - snap(4.0f * S); }
        }
    }
    float bx = snap(ax), by = snap(ay - bh + (1.0f - tOpen) * 4.0f * S);
    if (bx + bw > f.screenW - 4.0f) bx = snap(f.screenW - 4.0f - bw); if (bx < 4.0f) bx = 4.0f;
    if (by < 4.0f) by = 4.0f; if (by + bh > f.screenH - 4.0f) by = snap(f.screenH - 4.0f - bh);
    // Abilities (abiselec) does not open BESIDE : the client puts it IN PLACE of this menu (same left, same bottom, its
    // own height) -- this menu gives way while it is up (a quick cross-fade) ; Magic's categories open beside.
    static NativeEntryMenu lastS;
    if (SM.open) lastS = SM;
    const bool replacing = lastS.n && !strcmp(lastS.name, "abiselec");
    const float give = replacing ? g_subB.vis : 0.0f;
    const float A = g_main.vis * tOpen * (m.focused ? 1.0f : 0.62f) * (1.0f - give);   // a submenu on top : steps back
    const int row = A > 0.0f ? block_draw(f, k, g_main, rows, m.n, m.cursor, m.focused, bx, by, bw, bh, A, dt) : 0;
    if (M.open && give < 0.5f) {
        g_hit[0][0] = bx / f.screenW; g_hit[0][1] = by / f.screenH; g_hit[0][2] = bw / f.screenW; g_hit[0][3] = bh / f.screenH;
        // the MOUSE : hover moves the client's cursor, a click validates -- the client's own events (native_ui)
        if (row && row != g_main.hoverRow && row != M.cursor) native_action_hover(row);
        g_main.hoverRow = row;
        if (row && f.mouse && f.mouse->clicked) native_action_pick(row);
    } else g_main.hoverRow = 0;

    // ======== its submenu : Magic's categories beside it, Abilities in its place ========
    if (!block_ease(g_subB, SM.open && M.open, now, dt) || !lastS.n) return;
    RowSpec srows[16]; char snums[16][12]; int scur = 0;
    for (int i = 0; i < lastS.n && i < 16; ++i) {
        const SubRow* r = sub_row(lastS.name, lastS.entry[i]);
        if (r) { srows[i].label = tr(r->en, r->fr); srows[i].icon = r->icon; srows[i].sub = r->sub; }
        else { sprintf(snums[i], "#%d", lastS.entry[i]); srows[i].label = snums[i]; srows[i].icon = IC_NONE; srows[i].sub = false; }
        srows[i].off = lastS.state[i] == 5;
        if (lastS.entry[i] == lastS.cursor) scur = i + 1;
    }
    float sw, sh; block_size(f, k, srows, lastS.n, sw, sh);
    const float tS = ease_out(((float)(now - g_subB.openMs) / 1000.0f) / 0.07f);
    // beside the action menu, the SAME height (top and bottom level with it). More rows than the menu : it grows UP
    // (bottoms level, the default) or DOWN (tops level) -- Game UI -> Menus. Slides out from under it.
    float sbx, sby;
    if (replacing) {                                  // in place : same left, the BOTTOM anchored, its own height
        if (sw < bw) sw = bw;
        sbx = bx; sby = snap(by + bh - sh + (1.0f - tS) * 3.0f * S);
    } else {
        if (sh < bh) sh = bh;
        sbx = snap(bx + bw + 3.0f * S - (1.0f - tS) * 5.0f * S);
        sby = C.muGrow == 1 ? by : snap(by + bh - sh);
        if (sbx + sw > f.screenW - 4.0f) sbx = snap(bx - 3.0f * S - sw);        // no room on the right : on the left
    }
    if (sby + sh > f.screenH - 4.0f) sby = snap(f.screenH - 4.0f - sh); if (sby < 4.0f) sby = 4.0f;
    const float SA = g_subB.vis * tS * (lastS.focused ? 1.0f : 0.62f);          // its own list open on top : steps back
    if (SA <= 0.0f) return;
    const int srow = block_draw(f, k, g_subB, srows, lastS.n, scur, lastS.focused, sbx, sby, sw, sh, SA, dt);
    if (SM.open) {
        g_hit[1][0] = sbx / f.screenW; g_hit[1][1] = sby / f.screenH; g_hit[1][2] = sw / f.screenW; g_hit[1][3] = sh / f.screenH;
        const int ent = (srow >= 1 && srow <= lastS.n) ? lastS.entry[srow - 1] : 0;
        if (ent && srow != g_subB.hoverRow && ent != SM.cursor) native_sub_hover(ent);
        g_subB.hoverRow = srow;
        if (ent && f.mouse && f.mouse->clicked) native_sub_pick(ent);
    } else g_subB.hoverRow = 0;
}

} // namespace aio
