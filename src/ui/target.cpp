// target.cpp -- see target.h.
#include "ui/target.h"
#include "model/paths.h"
#include "gfx/draw.h"
#include "gfx/font.h"
#include "gfx/window.h"
#include "gfx/texture.h"
#include "model/gamestate.h"
#include "model/ui_config.h"
#include "model/party_state.h"
#include "ui/liquid_bars.h"
#include "ui/edit_box.h"       // shared edit-mode drag + alignment grid (one implementation for every single box)
#include "ui/text_style.h"     // te_sz/te_ow/te_col : shared TextStyle-resolve impl
#include "ui/ui_colors.h"      // scl / mul_a / lerp_color / hp_color : shared ARGB helpers
#include "ui/entity_color.h"   // in_my_group / allegiance_color : shared claim/allegiance palette (with the minimap)
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ui/buff_atlas.h"

namespace aio {

// status-icon atlas layout (shared with the party buffs) : 32 cols x 32px cells ; id -> cell (id%32, id/32).
static const int TB_SPEED_ID = 32;                               // buff-atlas cell used as the Speed icon (32.png)
static const char* TGT_TH_ICON() { static char b[260]; if (!b[0]) plugin_path(b, 260, "assets\\icon_th_coffer.raw"); return b; }
static const int TH_ICON_W = 64, TH_ICON_H = 64;                 // Treasure Hunter coffer icon (full-colour, straight alpha)

static inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }

// edit-mode drag state for the (single) Target box (the drag maths + alignment grid live in edit_box.cpp,
// shared with the Player Hub and any future standalone box).
static EditBox g_tgtEdit;

// textured-quad render state for the atlas (mirrors party.cpp setup_tex_state, MIPFILTER NONE = crisp).
static void setup_tex_state(u32 dev, u32 tex) { dTexQuadState(dev, tex); }   // mip NONE + CLAMP : the defaults
// scale a colour's RGB by f (keep alpha) -> a darker/brighter shade for the bar fill gradient.
// scl / mul_a / lerp_color / hp_color -> ui/ui_colors.h (shared with party/player)
// the shared colour-quad render state (mirrors party.cpp setup_color_state).
static void setup_color_state(u32 dev) {
    dColorQuadState(dev);   // shared 2D colour-quad pipeline (gfx/d3d.h)
}

// a small directional CHEVRON (the Front/Flank/Behind badge arrow) centred at (cx,cy), height ~`sz`, pointing
// UP at ang=0 and rotated `ang` radians (screen space). Two ANTI-ALIASED soft segments (tip->arms) so the edges
// feather cleanly at this small size (fill_tri was hard-edged / jaggy). Caller sets the colour-quad state first.
static void draw_dir_arrow(u32 dev, float cx, float cy, float sz, float ang, u32 col) {
    const float c = cosf(ang), s = sinf(ang);
    static const float LX[3] = { 0.00f, -0.40f, 0.40f };           // tip, bottom-left, bottom-right
    static const float LY[3] = { -0.46f, 0.34f, 0.34f };           // (up-pointing chevron, tip at -y)
    float px[3], py[3];
    for (int i = 0; i < 3; ++i) {
        const float lx = LX[i] * sz, ly = LY[i] * sz;
        px[i] = cx + lx * c - ly * s;                              // rotate about the centre (screen y is down)
        py[i] = cy + lx * s + ly * c;
    }
    const float th = sz * 0.24f;
    seg_soft(dev, px[0], py[0], px[1], py[1], th, col);           // tip -> bottom-left  (AA)
    seg_soft(dev, px[0], py[0], px[2], py[2], th, col);           // tip -> bottom-right (AA)
}

// a small PADLOCK (lock-on indicator) : shackle (∩) drawn first, then the rounded body over its lower
// half (hides the loop bottom -> a clean ∩), then a keyhole. col = ARGB, a = fade. Matches the party's
// red locked cursor (0xFFFF4030).
static void draw_lock(u32 dev, float x, float cy, float h, u32 col, float a) {
    const float iconW = h * 0.80f;
    const float bodyH = h * 0.56f;
    const float bodyY = cy + h * 0.5f - bodyH;
    const float shW = iconW * 0.58f, shH = h * 0.60f;
    const float shX = x + (iconW - shW) * 0.5f, shY = cy - h * 0.5f;
    float th = h * 0.15f; if (th < 1.2f) th = 1.2f;
    rrect_stroke(dev, shX, shY, shW, shH, shW * 0.5f, mul_a(col, a), th);                        // shackle loop...
    rrect(dev, x, bodyY, iconW, bodyH, h * 0.15f, mul_a(col, a), mul_a(scl(col, 0.74f), a));     // ...body hides its lower half
    disc(dev, x + iconW * 0.5f, bodyY + bodyH * 0.52f, bodyH * 0.17f, mul_a(0xE0140A08, a));     // keyhole
}

// rounded stencil CLIP (rrect_clip_begin/end) -> gfx/draw.h (shared with liquid_bars.cpp)
void Target::ensure(u32 dev) {
    if (!buff_tex_ && !buff_tried_) { buff_tex_ = load_raw_texture(dev, buff_atlas_path(), BUFF_ATLAS_W, BUFF_ATLAS_H); buff_tried_ = true; }
    if (!th_tex_   && !th_tried_)   { th_tex_   = load_raw_texture(dev, TGT_TH_ICON(),    TH_ICON_W,  TH_ICON_H);  th_tried_   = true; }
}
void Target::on_device_lost() { buff_tex_ = 0; buff_tried_ = false; th_tex_ = 0; th_tried_ = false; tgtSkin_.on_device_lost(); tgtSkinVar_ = -1; }   // FORGET handles (don't Release) -> reload
void Target::dispose() {
    release_texture(buff_tex_);    buff_tex_ = 0;    buff_tried_ = false;
    release_texture(th_tex_);      th_tex_   = 0;    th_tried_   = false;
    tgtSkin_.dispose();            tgtSkinVar_ = -1;
}

// ---- per-element typography (ui_config().tgtText[TGT_*]) : each Target text element resolves its own Font
//      face/weight/italic + size / outline / colour / UPPERCASE, on top of the widget's base size. ----
static Font* tgt_font(FontManager* fm, Font* deflt, int elem) {
    if (!fm) return deflt;
    const TextStyle& t = ui_config().tgtText[elem];
    const char* face = (t.face > 0) ? ui_font_face(t.face) : 0;   // 0 => the manager's default face
    Font* r = fm->get(face, t.bold ? 700 : 400, t.italic);
    return r ? r : deflt;
}
static inline float tgt_sz(int e, float base) { return te_sz(ui_config().tgtText[e], base); }
static inline float tgt_ow(int e, float base) { return te_ow(ui_config().tgtText[e], base); }
static inline u32   tgt_col(int e, u32 base)  { return te_col(ui_config().tgtText[e], base); }
static const char*  tgt_up(int e, const char* s, char* buf, int cap) {   // UPPERCASE into buf if the element wants CAPS
    if (!ui_config().tgtText[e].upper || !s) return s;
    int i = 0; for (; s[i] && i < cap - 1; ++i) { char c = s[i]; buf[i] = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c; } buf[i] = 0; return buf;
}
// is this server id YOU or a member of YOUR party (the 6) ? Used for name-colour by allegiance. Deliberately
// MAIN PARTY only -- an alliance-mate is in a different sub-party ("pas la mienne") and reads as an outsider.
// target NAME colour = the shared claim/allegiance palette (ui/entity_color.h), keyed off SpawnType
// (0x10 mob / 0x01 PC). Off (tgtNameHostile) or absent -> the user's custom TGT_NAME base (else pale gold).
static u32 target_name_color_e(const TargetEntity& e) {
    const u32 base = tgt_col(TGT_NAME, 0xFFF2F2B7);
    if (!ui_config().tgtNameHostile) return base;
    return allegiance_color(e.spawnType == 0x10, (e.spawnType & 0x01) != 0, e.id, e.claimId, e.status, e.pflags, base);
}
static u32 target_name_color(const GameState& g, bool present, bool fake) {
    if (fake || !present) return tgt_col(TGT_NAME, 0xFFF2F2B7);
    return target_name_color_e(g.target);
}

// ---- DISTANCE gauge : range zones (yalms), ported from the old addon (targetbar.lua). A MOB shows offensive
//      ranges (Melee/WS/Magic/Ranged/Enmity, scaled by model_size) ; a PC/ally shows support ranges
//      (Trade/AoE/Cast). Colours = the addon's. lcol = the lighter label / in-zone cursor tint. ----
struct RangeBand { float to; const char* name; u32 col, lcol; };
static int range_zones(unsigned spawnType, float modelSize, RangeBand* b, float& scale) {
    if (spawnType & 0x10) {                                  // MOB -> offensive ranges
        const float ws     = modelSize + 3.1f;               // model_size + WS.range(2) * 1.55
        const float melee  = ws - 1.0f;                       // auto-attack = WS - 1'
        const float magic  = fmaxf(20.9f + modelSize, ws + 0.5f);
        const float ranged = fmaxf(25.0f, magic + 0.5f);
        const float enmity = fmaxf(30.0f, ranged + 0.5f);
        scale = fmaxf(35.0f, enmity + 3.0f);
        b[0] = { melee,  "Melee",  0xFF3CC846u, 0xFF78D282u };
        b[1] = { ws,     "WS",     0xFFEBA541u, 0xFFEBBE82u };
        b[2] = { magic,  "Magic",  0xFF508CF0u, 0xFF8CAAF0u };
        b[3] = { ranged, "Ranged", 0xFFEBCD3Cu, 0xFFEBD26Eu };
        b[4] = { enmity, "Enmity", 0xFFE1504Bu, 0xFFEB827Du };
        return 5;
    }
    // everything NON-MOB (a PC / ally OR an NPC) -> support ranges (like the old addon's `else` branch)
    const float trade = 6.0f, aoe = 10.0f, cast = 20.79f;   // PC / ally / NPC support ranges (fixed ; the old fmaxf() operands were constant)
    scale = 25.0f;
    b[0] = { trade, "Trade", 0xFFA587E1u, 0xFFC3AAEBu };
    b[1] = { aoe,   "AoE",   0xFFEBAA46u, 0xFFEBC378u };
    b[2] = { cast,  "Cast",  0xFF46C85Au, 0xFF8CDC96u };
    return 3;
}
static inline u32 shade(u32 c, float f) {                     // scale RGB by f, keep alpha (zone bottom sheen)
    const u32 A = c & 0xFF000000u;
    const u32 r = (u32)(((c >> 16) & 0xFF) * f), g = (u32)(((c >> 8) & 0xFF) * f), b = (u32)((c & 0xFF) * f);
    return A | (r << 16) | (g << 8) | b;
}
static inline const char* rup(bool upper, const char* s, char* buf, int cap) {   // UPPERCASE into buf when the element wants CAPS
    if (!upper || !s) return s;
    int i = 0; for (; s[i] && i < cap - 1; ++i) { char c = s[i]; buf[i] = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c; } buf[i] = 0; return buf;
}
// draw the gauge with the HP-BAR look : a dark rounded CAPSULE track + tinted zone bands (clipped to the round
// ends) + a glass sheen + an AA rim, then the cursor line/triangle at `dist`, the staggered zone labels, the
// "Out" tail and the distance number. Typography (font/size/colour/outline/CAPS) = the TGT_RANGE element.
static void draw_range_gauge(u32 dev, Font* lf, float gx, float gy, float gw, float gaugeH,
                             const RangeBand* b, int nb, float scale, float dist,
                             float lblSz, float numSz, u32 numCol, bool upper, float a, u32 stk, float ow, float S) {
    const float r = gaugeH * 0.5f;
    setup_color_state(dev);
    rrect(dev, gx, gy, gw, gaugeH, r, mul_a(0xFF0A0E14u, a), mul_a(0xFF05070Cu, a));   // dark rounded capsule track (== HP bar)
    rrect_clip_begin(dev, gx, gy, gw, gaugeH, r);                                       // clip the zones to the round ends
    float prev = 0.0f;
    for (int i = 0; i < nb; ++i) {
        const float t1 = fminf(1.0f, fmaxf(0.0f, prev / scale)), t2 = fminf(1.0f, fmaxf(0.0f, b[i].to / scale));
        const float x1 = snap(gx + gw * t1), zw = snap(gx + gw * t2) - x1;
        if (zw > 0.5f) { const u32 ct = mul_a(b[i].col, a), cb = mul_a(shade(b[i].col, 0.62f), a);
            grad_quad(dev, x1, gy, zw, gaugeH, ct, ct, cb, cb); }                       // vertical sheen : bright top -> dark bottom
        prev = b[i].to;
    }
    if (a > 0.4f) fiole_glass(dev, gx, gy, gw, gaugeH);   // the HP-bar's glass front-face (curvature + specular)
    rrect_clip_end(dev);
    setup_color_state(dev);                               // resets the blend fiole_glass left additive
    rrect_stroke(dev, gx, gy, gw, gaugeH, r, mul_a(0x66FFFFFFu, a), snap(1.0f * S));    // AA capsule rim (== HP bar)
    u32 core = 0xFFF5463Cu;                                                             // out of range -> red (245,70,60)
    for (int i = 0; i < nb; ++i) if (dist <= b[i].to) { core = b[i].lcol; break; }
    const float tcur = fminf(1.0f, fmaxf(0.0f, dist / scale));
    const float cx = fminf(snap(gx + gw * tcur), gx + gw - 2.0f);
    const u32 blk = mul_a(0xFF000000u, a), cc = mul_a(core, a);
    grad_quad(dev, snap(cx - 2.0f), gy - 1.0f, 5.0f, gaugeH + 2.0f, blk, blk, blk, blk);   // cursor black outline
    grad_quad(dev, snap(cx - 1.0f), gy - 1.0f, 3.0f, gaugeH + 2.0f, cc, cc, cc, cc);        // cursor line
    for (int rr = 0; rr < 5; ++rr) { const float w = (float)((5 - rr) * 2 - 1);            // downward triangle (9,7,5,3,1)
        grad_quad(dev, snap(cx - w * 0.5f), gy - 6.0f + (float)rr, w, 1.0f, cc, cc, cc, cc); }
    if (lf) {                                                                     // labels (staggered) + Out + number
        lf->begin(dev);
        const float lblY = gy + gaugeH + snap(2.0f);
        float pr = 0.0f;
        for (int i = 0; i <= nb; ++i) {
            const bool tail = (i == nb);
            if (tail && !(pr < scale - 1.0f)) break;                              // no "Out" tail if the last zone fills the bar
            const float mid = tail ? (pr + scale) * 0.5f : (pr + b[i].to) * 0.5f;
            const float tm = fminf(1.0f, fmaxf(0.0f, mid / scale));
            char ub[16]; const char* nm = rup(upper, tail ? "Out" : b[i].name, ub, 16);
            const float hw = lf->measure(nm, lblSz) * 0.5f;
            float lx = snap(gx + gw * tm); if (lx < gx + hw) lx = gx + hw; if (lx > gx + gw - hw) lx = gx + gw - hw;
            const float ly = lblY + ((i % 2 == 1) ? lblSz * 1.05f : 0.0f);
            lf->draw_c(dev, lx, ly + lblSz * 0.5f, nm, lblSz, tail ? mul_a(0xFF8C8C96u, a) : mul_a(b[i].lcol, a), stk, ow);
            if (!tail) pr = b[i].to;
        }
        char db[12]; sprintf(db, "%.2f", dist > 50.0f ? 50.0f : dist);            // distance number, centred in the gauge
        lf->draw_c(dev, gx + gw * 0.5f, gy + gaugeH * 0.5f, db, numSz, mul_a(numCol, a), stk, ow);
    }
}

// ================= Help live samples : draw ONE real Target element (SAME code path as the widget) =================
// Called by the Help tab (config_page.cpp) so it shows the ACTUAL visuals, not a mock-up. Everything here
// reuses the static draw helpers above -> the sample and the live widget can never drift.

void target_help_textures(u32 dev, u32& buffTex, u32& thTex) {   // caller caches these + forgets on device-lost
    if (!buffTex) buffTex = load_raw_texture(dev, buff_atlas_path(), BUFF_ATLAS_W, BUFF_ATLAS_H);
    if (!thTex)   thTex   = load_raw_texture(dev, TGT_TH_ICON(),    TH_ICON_W,  TH_ICON_H);
}

// HP bar with the delayed "white damage" trail : a looping hit drops the bar, the orange trail holds then drains.
void target_help_hpbar(u32 dev, float x, float y, float w, float h, float t) {
    const float r = h * 0.5f;
    const float c = fmodf(t, 1.7f);                                       // 1.7s loop : hit at c=0, trail drains, repeat
    const float hitFrom = 72.0f, hitTo = 44.0f, hp = hitTo;
    const float ghost = (c < 0.35f) ? hitFrom
                      : (c < 1.15f) ? hitFrom + (hitTo - hitFrom) * ((c - 0.35f) / 0.80f)
                                    : hitTo;
    const u32 hc = hp_color(hp);
    setup_color_state(dev);
    rrect(dev, x, y, w, h, r, 0xFF0A0E14u, 0xFF05070Cu);                  // dark capsule track (== the widget)
    LiquidBars* vp = vial_provider();
    if (vp && vp->vial_ready()) {
        vp->draw_vial_scaled(dev, t, x, y, w, h, 0 /*HP*/, hp / 100.0f, hc, 0.0f, hp <= 25.0f ? 1.0f : 0.0f, 4);
        if (ghost > hp + 0.3f) {                                          // the delayed damage trail on top
            rrect_clip_begin(dev, x, y, w, h, r);
            const float gx = x + w * (hp / 100.0f), gw = w * ((ghost - hp) / 100.0f);
            grad_quad(dev, gx, y, gw, h, 0xDCFF9A2Cu, 0xDCFF9A2Cu, 0xD4EE5A0Eu, 0xD4EE5A0Eu);
            rrect_clip_end(dev);
            setup_color_state(dev);
        }
    } else {
        const float fw = w * (hp / 100.0f);
        if (fw > 1.0f) rrect_left(dev, x, y, fw, h, r, hc, scl(hc, 0.6f));
    }
    rrect_stroke(dev, x, y, w, h, r, 0x66FFFFFFu, snap(1.0f));            // AA capsule rim (== the widget)
}

// distance / range gauge (the exact widget primitive) : the cursor sweeps across the zones and into "Out".
void target_help_range(u32 dev, Font* lf, float x, float y, float w, float h, bool mob, float t) {
    RangeBand b[8]; float scale = 1.0f;
    const int n = range_zones(mob ? 0x10u : 0x01u, 0.5f, b, scale);       // mob : Melee/WS/Magic/Ranged/Enmity ; PC : Trade/AoE/Cast
    const float dist = fmodf(t * 3.5f, scale * 1.12f);                    // 0 .. just past the last zone (into "Out")
    draw_range_gauge(dev, lf, x, y, w, h, b, n, scale, dist,
                     snap(9.0f), snap(11.0f), 0xFFFFFFFFu, false, 1.0f, 0xFF000000u, 1.2f, 1.0f);
}

// a row of real debuff icons from the atlas, each with a live countdown timer (same format/colours as the widget).
void target_help_debuffs(u32 dev, Font* f, u32 buffTex, float x, float y, float cell, int n, float t) {
    if (!buffTex) return;
    static const int ids[6]  = { 3, 4, 5, 6, 13, 8 };                     // Poison, Paralysis, Blind, Silence, Slow, Disease
    static const int secs[6] = { 57, 33, 12, 4, 125, 88 };               // demo remaining seconds
    const float au = (float)BUFF_CELL / (float)BUFF_ATLAS_W, av = (float)BUFF_CELL / (float)BUFF_ATLAS_H, gap = snap(8.0f);
    setup_tex_state(dev, buffTex);
    for (int i = 0; i < n; ++i) {
        const int id = ids[i % 6];
        const float u0 = (float)(id % BUFF_COLS) * au, v0 = (float)(id / BUFF_COLS) * av;
        tquad(dev, x + i * (cell + gap), y, cell, cell, u0, u0 + au, v0, v0 + av, 0xFFFFFFFFu, 0xFFFFFFFFu);
    }
    dSetTex(dev, 0, 0);                                                   // don't leave the atlas bound for the next draw
    if (f) {
        f->begin(dev);
        for (int i = 0; i < n; ++i) {
            int r = secs[i % 6] - (int)fmodf(t, 30.0f); if (r < 0) r += 30;   // a gently ticking demo countdown
            char tb[8]; if (r >= 60) sprintf(tb, "%d:%02d", r / 60, r % 60); else sprintf(tb, "%d", r);
            const u32 col = (r <= 5) ? 0xFFFF7A4Au : 0xFFF6C64Eu;             // YOURS : gold, red when about to wear off
            f->draw_c(dev, x + i * (cell + gap) + cell * 0.5f, y + cell + snap(9.0f), tb, snap(12.0f), col, 0xFF000000u, 1.2f);
        }
    }
}

// the Treasure Hunter coffer + its tier number, as shown on an engaged mob.
void target_help_th(u32 dev, Font* f, u32 thTex, float x, float y, float size, float t) {
    if (!thTex) return;
    setup_tex_state(dev, thTex);
    tquad(dev, x, y, size, size, 0.0f, 1.0f, 0.0f, 1.0f, 0xFFFFFFFFu, 0xFFFFFFFFu);
    dSetTex(dev, 0, 0);
    if (f) {
        const int th = 1 + ((int)(t * 0.5f) % 4);                         // cycles TH 1..4
        char tb[8]; sprintf(tb, "%d", th);
        f->begin(dev);
        f->draw_lc(dev, x + size + snap(6.0f), y + size * 0.5f, tb, snap(16.0f), 0xFFF6C64Eu, 0xFF000000u, 1.4f);
    }
}

// movement-speed readout, exactly as the detail row draws it : "Spd +NN%" (text) or the Speed icon + value,
// in the speed colour (faster = green for a PC / red for a mob ; slower = the opposite ; neutral = grey).
void target_help_speed(u32 dev, Font* f, u32 buffTex, float x, float cy, int pct, bool player, bool icon) {
    if (!f) return;
    const u32 col = (pct > 1)  ? (player ? 0xFF78E678u : 0xFFFF4646u)    // faster : good for a PC, bad for a mob
                  : (pct < -1) ? (player ? 0xFFFF4646u : 0xFF78E678u)    // slower : bad for a PC, good for a mob
                               : 0xFFC8C8C8u;
    char sv[16]; sprintf(sv, "%+d%%", pct);
    const float ssz = snap(15.0f);
    if (icon && buffTex) {                                               // icon mode : Speed cell (32) + value
        const float cell = snap(20.0f), au = (float)BUFF_CELL / (float)BUFF_ATLAS_W, av = (float)BUFF_CELL / (float)BUFF_ATLAS_H;
        const float u0 = (float)(TB_SPEED_ID % BUFF_COLS) * au, v0 = (float)(TB_SPEED_ID / BUFF_COLS) * av;
        setup_tex_state(dev, buffTex);
        tquad(dev, x, cy - cell * 0.5f, cell, cell, u0, u0 + au, v0, v0 + av, 0xFFFFFFFFu, 0xFFFFFFFFu);
        dSetTex(dev, 0, 0);
        f->begin(dev); f->draw_lc(dev, x + cell + snap(3.0f), cy, sv, ssz, col, 0xFF000000u, 1.2f);
    } else {                                                            // text mode : "Spd +NN%"
        char sb[24]; sprintf(sb, "Spd %s", sv);
        f->begin(dev); f->draw_lc(dev, x, cy, sb, ssz, col, 0xFF000000u, 1.2f);
    }
}

void Target::measure(float& w, float& h) const {
    const float S = (scale_ < 0.3f ? 0.3f : scale_) * (ui_config().tgtScale < 0.3f ? 0.3f : ui_config().tgtScale);
    // name + bar only (the debuff band grows DOWNWARD past this) : mirror draw()'s no-debuff content height,
    // honouring the configured name text size + bar height so anchoring/edit bounds track the real box.
    const float pad = 13.0f * S, nameSz = 15.0f * S * ui_config().tgtText[TGT_NAME].size, barH = 18.0f * S * ui_config().tgtBarH;
    const float outerPad = ui_config().tgtBox ? pad : 0.0f;   // edge margin -> 0 boxless (bounds hug the content ; box ON unchanged)
    w = baseW_ * S - 2.0f * (pad - outerPad);                 // boxless shrinks by the removed side margins (bar width unchanged)
    h = outerPad + nameSz + 5.0f * S + barH + outerPad;
}

void Target::draw(const Frame& f) {
    if (!visible_ || !f.game) return;
    const u32 dev = f.dev;
    const GameState& g = *f.game;
    const float S = (scale_ < 0.3f ? 0.3f : scale_) * (ui_config().tgtScale < 0.3f ? 0.3f : ui_config().tgtScale);

    // dt-based smoothing (f.t wraps every ~1000s -> guard the negative step).
    float dt = (lastT_ < 0.0f) ? 0.016f : (f.t - lastT_);
    if (dt < 0.0f) dt += 1000.0f;
    if (dt > 0.10f) dt = 0.10f;
    lastT_ = f.t;

    // demo_ (config live preview) and EDIT mode force a fake target so the box is visible + grabbable with
    // nothing targeted. A real target always wins.
    const bool editing = ui_config().editLayout;
    const bool live    = g.target.valid && g.target.name[0] != 0;
    const bool fake    = demo_ || (editing && !live);
    const bool present = live || fake;
    const char*    srcName = fake ? "Dummy Target" : g.target.name;
    const unsigned srcId   = fake ? 0x01DE0001u    : g.target.id;
    const int      srcHpp  = fake ? 62             : g.target.hpp;

    // HP% eases to the live value (animated drain / heal + a trailing chunk). Only updates WHILE a target is
    // held : on a lost target (kill OR detarget -- indistinguishable) the bar just fades out at its last % ;
    // draining it to 0 would look wrong on a plain Tab-away. Switching mobs snaps to the new one's HP.
    if (present) {
        int ni = 0; for (; ni < 23 && srcName[ni]; ++ni) lastName_[ni] = srcName[ni]; lastName_[ni] = 0;   // cache -> fades out with the frame
        float tgt = (float)srcHpp; if (tgt < 0.0f) tgt = 0.0f; if (tgt > 100.0f) tgt = 100.0f;
        if (srcId != lastId_) {                                                          // switched mobs -> snap everything, no trail
            lastId_ = srcId; hpp_ = hppGhost_ = lastTgt_ = tgt; ghostHold_ = 0.0f; hitFlash_ = 0.0f;
            spdShown_ = 0.0f; spdWindow_ = 5.0f;                                          // don't carry the old target's speed readout
        } else {
            if (tgt < lastTgt_ - 0.3f) {                                                  // FRESH DAMAGE : restart the delayed-bar hold + an impact flash sized to the hit
                ghostHold_ = 0.26f;
                float f2 = (lastTgt_ - tgt) / 28.0f; if (f2 > 1.0f) f2 = 1.0f; if (f2 > hitFlash_) hitFlash_ = f2;
            }
            lastTgt_ = tgt;
            hpp_ += (tgt - hpp_) * (1.0f - expf(-dt * 34.0f));                            // FOREGROUND fill : ~instant drop = a crisp hit read
            if (fabsf(tgt - hpp_) < 0.25f) hpp_ = tgt;
            if (hppGhost_ > hpp_) {                                                       // DELAYED damage bar : HOLD, then eased catch-up (drains the "white damage")
                if (ghostHold_ > 0.0f) ghostHold_ -= dt;
                else hppGhost_ += (hpp_ - hppGhost_) * (1.0f - expf(-dt * 6.5f));
                if (hppGhost_ - hpp_ < 0.25f) hppGhost_ = hpp_;
            } else hppGhost_ = hpp_;                                                      // heal / caught up -> no trail
            if (hpp_ <= 0.5f) hppGhost_ = hpp_;                                           // dead : no lingering trail
        }
        hitFlash_ -= dt * 3.2f; if (hitFlash_ < 0.0f) hitFlash_ = 0.0f;                   // decay the impact bump
    } else {
        hppGhost_ = hpp_; ghostHold_ = 0.0f; hitFlash_ = 0.0f;                            // lost target : kill trail + flash while fading
    }
    const float want = present ? 1.0f : 0.0f;
    appear_ += (want - appear_) * (1.0f - expf(-dt * (want > appear_ ? 12.0f : 20.0f)));   // fade OUT faster than IN
    if (want < 0.5f && appear_ < 0.06f) { appear_ = 0.0f; return; }   // cut the long exponential tail -> nothing lingers
    if (appear_ < 0.012f) { appear_ = 0.0f; return; }               // fully hidden -> draw nothing

    const float a = (appear_ > 1.0f) ? 1.0f : appear_;

    // debuffs on the target (icons only ; the //aio-act tracker). Query BEFORE the chrome so the box grows to fit.
    unsigned short dids[32]; int drem[32] = {0}; unsigned char dself[32] = {0}; int nd = 0;
    if (fake && ui_config().tgtDebuffs) {           // live-preview : a full 20-icon sample (yours=gold / others=white / "???")
        static const unsigned short DID[20] = { 2, 11, 6, 3, 4, 5, 7, 8, 9, 10, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21 };
        static const int           DRM[20] = { 45, 28, -30, 120, 90, 15, 300, 5, 60, 180, 30, 8, -18, 240, 75, 100, 20, -95, 55, 12 };
        static const unsigned char DSF[20] = { 1, 1, 0, 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1, 0, 1, 1, 0, 1 };
        int nSamp = ui_config().tgtBuffMax; if (nSamp < 1) nSamp = 1; if (nSamp > 32) nSamp = 32;   // cycle the 20 samples up to Max so the preview reflects the setting (up to 32)
        nd = nSamp; for (int i = 0; i < nSamp; ++i) { const int s = i % 20; dids[i] = DID[s]; drem[i] = DRM[s]; dself[i] = DSF[s]; }
    } else if (present && ui_config().tgtDebuffs) nd = party().target_debuffs(g.target.id, dids, drem, dself, 32);
    if (ui_config().dbShow) nd = 0;   // DETACHED : the Debuffs module owns them now -> zero the inline row (kills the width/height growth + the icon draw, all gated on nd>0), incl. the fake preview
    // compact to valid atlas ids, capped at the user's max (1..32 ; 2 rows/columns of 16) -> index-driven layout below.
    { int cap = ui_config().tgtBuffMax; if (cap < 1) cap = 1; if (cap > 32) cap = 32;
      int nv = 0; for (int i = 0; i < nd && nv < cap; ++i) { if (dids[i] >= (BUFF_COLS * BUFF_ATLAS_ROWS)) continue;
        if (nv != i) { dids[nv] = dids[i]; drem[nv] = drem[i]; dself[nv] = dself[i]; } ++nv; } nd = nv; }
    static const int BUFF_PER_LINE = 16;                     // max icons per row (horizontal) or per column (vertical) -> 2 rows/cols of 16 = 32 max
    const int nLines = (nd > BUFF_PER_LINE) ? 2 : 1;         // >16 -> a second row/column
    const bool showTimers = ui_config().tgtTimers != 0;

    // debuff icon metrics (needed up-front : an INSIDE row can widen the box). icon + its gap + timer track iconK.
    const float pad  = snap(13.0f * S);      // content margin : clears the MAIN box border (thick in Royal) ; also the buff-row side margin
    // EDGE margin only : collapses to 0 when the box chrome is off (tgtBox=0) so the content hugs its bounds for
    // precise placement. box ON -> outerPad == pad, so the whole boxed layout is byte-for-byte unchanged. The
    // buff-row side margin (buffRowW) keeps the full `pad` -> internal spacing is untouched, only the borders shrink.
    const float outerPad = ui_config().tgtBox ? pad : 0.0f;
    const float iconK = ui_config().tgtIconSz;
    const float icon  = snap(18.0f * S * iconK);
    const float timerPx = showTimers ? tgt_sz(TGT_TIMER, 8.5f * S * iconK) : 0.0f;   // timer text size (drives icon spacing + bottom margin)
    const float igap    = snap(3.0f * S * iconK + timerPx * 0.40f);                  // icon spacing grows GENTLY with the timer
    const float tGapY   = snap(3.0f * S * iconK);                                     // gap between the icon bottom and its timer
    const float tMargin = snap(4.0f * S) + timerPx * 0.25f;                           // margin below the timer to the box edge

    // geometry (pixel-snapped). Top block : name (left) + HP% (right) + the bar. A debuff band grows BELOW it.
    // INSIDE placement widens the box so a full row (up to 10 icons) fits with the same side padding.
    const int   bpR = (ui_config().tgtBuffPos < 0 || ui_config().tgtBuffPos > 4) ? 0 : ui_config().tgtBuffPos;
    const bool  insideBuffs = (nd > 0 && bpR == 0 && ui_config().tgtBox != 0);
    const int   rowMax  = (nd < BUFF_PER_LINE) ? nd : BUFF_PER_LINE;                  // widest (first) row's icon count
    const float buffRowW = (rowMax > 0) ? (rowMax * icon + (rowMax - 1) * igap + 2.0f * pad) : 0.0f;
    float W = snap(baseW_ * S - 2.0f * (pad - outerPad));   // boxless : shrink by the removed side margins so the box hugs the bar (bar width stays identical)
    if (insideBuffs && buffRowW > W) W = snap(buffRowW);
    // position : a user-placed (edit-mode) fraction wins over the layout default. The config preview uses the
    // origin the HUD set via set_origin (demo_), so it isn't overridden by the stored position.
    float px, py;
    if (!demo_ && ui_config().tgtPosSet && f.screenW > 0.0f && f.screenH > 0.0f) {
        px = snap(ui_config().tgtX * f.screenW); py = snap(ui_config().tgtY * f.screenH);
    } else { px = snap(px_); py = snap(py_); }
    // Centre-lock : the widget re-centres itself each frame on the chosen axis (survives screen-size changes ;
    // a drag below releases it). V uses last frame's height (this frame's isn't computed yet -> self-corrects).
    if (!demo_) {
        if (ui_config().tgtCenterH && f.screenW > 0.0f) px = snap((f.screenW - W) * 0.5f);
        if (ui_config().tgtCenterV && f.screenH > 0.0f) py = snap((f.screenH - lastH_) * 0.5f);
    }

    // EDIT MODE : shared drag (move + Shift/Ctrl axis-lock + centre snap + zone push-out + wheel resize). The grab
    // rect uses LAST frame's height (this frame's geometry isn't computed yet). Implementation in edit_box.cpp.
    if (editing && !demo_)
        edit_box_drag(g_tgtEdit, EDITBOX_TARGET, f, px, py, W, lastH_, ZPERM_TARGET,
                      ui_config().tgtPosSet, ui_config().tgtX, ui_config().tgtY,
                      ui_config().tgtCenterH, ui_config().tgtCenterV, ui_config().tgtScale);
    const float innerX = px + outerPad, innerW = W - 2.0f * outerPad;
    const float nameSz = tgt_sz(TGT_NAME, 15.0f * S);                    // name text size (its own config multiplier)
    const float nameCy = py + outerPad + nameSz * 0.5f;
    const float fmT = 0.0f;                                              // NO border on the bar (borderless test) -> the bar sits directly in the main box
    const float barH = snap(18.0f * S * ui_config().tgtBarH), barR = barH * 0.5f;   // HP bar height (config multiplier)
    const float barW = snap(innerW * clampf(ui_config().tgtBarW, 0.40f, 1.00f));     // HP bar width (fraction of inner, centred)
    const float barX = snap(innerX + (innerW - barW) * 0.5f);
    const float barY = snap(py + outerPad + nameSz + 5.0f * S + fmT);
    // ---- detail row : target movement SPEED % (green = slower, red = faster). movement_speed is ambiguous at
    //      rest (walk vs run), so for a MOB the % is gated on actual movement (position delta) ; a PC's speed is
    //      reliable (reflects gear) so it always shows. Sits between the bar and the debuff row. ----
    // A PURE NPC (spawnType has neither the PC 0x01 bit nor the Mob 0x10 bit, e.g. 0x02) shows NO speed / TH.
    const bool tPureNPC = present && !fake && ((g.target.spawnType & 0x11) == 0);
    const bool showSpd = ui_config().tgtSpeed != 0 && !tPureNPC;
    const int  th = fake ? (ui_config().tgtTH ? 4 : 0) : (ui_config().tgtTH ? party().target_th(g.target.id) : 0);
    const bool showTH = ui_config().tgtTH != 0 && th > 0;   // th>0 is only ever set on a mob you hit -> auto mob-only
    float spdPct = 0.0f; u32 spdCol = 0xFFC8C8C8u;
    const bool rangeMinShown = ui_config().tgtRange != 0 && ui_config().tgtRangeMin != 0 && present;   // minimal distance sits in the detail row (centre)
    const bool showDetail = (showSpd || showTH || rangeMinShown) && present;
    if (showSpd && present) {
        if (fake) { spdPct = -12.0f; spdCol = 0xFF78E678u; }             // demo sample (a slowed mob)
        else {
            if (g.target.id != posId_) spdWindow_ = 5.0f;   // reset the held mob speed on a target change (5.0 = 0% baseline)
            posId_ = g.target.id;
            // MOVEMENT SPEED (FFXI wiki + in-game probes) : base 5.0 yalms/s, MAX 8.0 (=+60%). movement_speed@0x98.
            //  - ANY PLAYER (self or other) : the field is STATIC and exact (their gear) -- verified stable even while
            //    moving (a +18% test char, moving & at rest). -> 100*(ms/5-1), always. (Measuring position is noisy AND a
            //    chasing entity does catch-up = YOUR speed, so never measure.)
            //  - MOB : the field is its real speed while IDLE (tiger 6.8=+36%, normal mob 4.0=-20%) but SPIKES to a
            //    bogus 10-17 while it CHASES you. -> use it when PLAUSIBLE (<=8.0), FREEZE the last good value otherwise.
            //  - MOUNTED (status 5 chocobo / 85 mount) : field = mount speed -> 100*(ms/4).
            const bool isPlayer = (g.target.spawnType & 0x01) != 0;    // 0x01 bit : self reads 0x20D, other PCs 0x01
            const bool mounted  = (g.target.status == 5 || g.target.status == 85);
            const float ms = g.target.moveSpeed;
            float raw;
            if (mounted)       raw = 100.0f * (ms / 4.0f);
            else if (isPlayer) raw = 100.0f * (ms / 5.0f - 1.0f);      // any player : static field, always reliable
            else {                                                     // MOB : field when plausible (<=8), else the last held value
                if (ms > 0.5f && ms <= 8.05f) spdWindow_ = ms;
                raw = 100.0f * (spdWindow_ / 5.0f - 1.0f);
            }
            if (raw > 60.0f) raw = 60.0f;                              // wiki : caps at +60% (field 8.0 / base 5.0)
            spdShown_ += (raw - spdShown_) * (1.0f - expf(-dt * 7.0f));     // ease the displayed % -> smooths the start/stop spike
            spdPct = spdShown_;
            if (spdPct > 1.0f)       spdCol = isPlayer ? 0xFF78E678u : 0xFFFF4646u;   // faster : good for a PC, bad for a mob
            else if (spdPct < -1.0f) spdCol = isPlayer ? 0xFFFF4646u : 0xFF78E678u;   // slower : bad for a PC, good for a mob
            else                     spdCol = 0xFFC8C8C8u;
        }
    }
    // Speed/TH ICON mode : the icon has its own size slider ; the detail row GROWS so a bigger icon fits (no overlap).
    const float dIconSz = snap(15.0f * S * ui_config().tgtDetailIconSz);
    const bool  spdIcon = showSpd && ui_config().tgtSpeedIcon != 0;
    const bool  thIcon  = showTH  && ui_config().tgtThIcon != 0;
    const bool  anyDetailIcon = spdIcon || thIcon;
    const float detailTextH = snap(15.0f * S);
    const float detailH = !showDetail ? 0.0f : (anyDetailIcon && (dIconSz + snap(2.0f * S)) > detailTextH ? snap(dIconSz + snap(2.0f * S)) : detailTextH);
    const float detailY = snap(barY + barH + fmT + snap(4.0f * S));
    const float belowBar = showDetail ? (detailY + detailH) : (barY + barH + fmT);
    // ---- ACTION bar : the target mob's LIVE spell cast (name + filling bar), read from the shared cast tracker
    //      (party().cast_label -- the SAME 0x028 begin-cast data the party rows use). Sits under the detail row ;
    //      grows the box only while the mob is actually casting (fades in/out via castA). ----
    float castPct = 0.0f, castA = 0.0f; int castKind = 0; const char* castName = 0;
    if (fake) { castName = "Flame Breath"; castPct = 0.62f; castA = 1.0f; }        // preview sample
    else if (present) castName = party().cast_label(g.target.id, castPct, castA, &castKind);
    const bool  actReserve = ui_config().tgtCast && present && !tPureNPC;           // slot reserved (stable box height) for a target that can act
    const bool  actShow    = actReserve && castName && castA > 0.01f;              // the name + fill draw only while actually casting
    // the empty grey slot ("placeholder") : the reserved track drawn while idle, shown when casting OR when
    // Cast-placeholder is on ; hidden when idle + placeholder off.
    const bool  actTrack   = actReserve && (actShow || ui_config().tgtCastDemo != 0 || fake);
    const float actNameSz = snap(tgt_sz(TGT_NAME, 11.0f * S));
    const float actBarH2  = snap(6.0f * S);
    const float actSecH   = actReserve ? snap(actNameSz + snap(3.0f * S) + actBarH2 + snap(7.0f * S)) : 0.0f;
    const float actY      = snap(belowBar + snap(4.0f * S));                        // action-section top
    const float belowAct  = actReserve ? snap(belowBar + actSecH) : belowBar;
    // ---- RANGE (distance) gauge : sits between the detail row and the inside debuffs, and GROWS the box.
    //      The LIVE distance needs the entity distance offset (not reversed yet) -> for now it renders in the
    //      config PREVIEW only (fake). Zones : mob = Melee/WS/Magic/Ranged/Enmity ; PC = Trade/AoE/Cast. ----
    RangeBand rBands[6]; float rScale = 35.0f; int rN = 0;
    // LIVE distance = horizontal (yalms) between the target (posX/posZ @0x04/0x0C) and the player's own position
    // (party().selfX_/selfZ_, same entity_array read the member distances use -> no new offset to reverse).
    const float rdx = g.target.posX - party().selfX_, rdz = g.target.posZ - party().selfZ_;
    const float rDist = fake ? 4.2f : (present ? sqrtf(rdx * rdx + rdz * rdz) : 0.0f);
    if (ui_config().tgtRange && present) {
        if (fake) {                                         // preview : a representative mob sample (cursor in Melee)
            rBands[0] = { 4.3f,  "Melee",  0xFF3CC846u, 0xFF78D282u }; rBands[1] = { 5.3f,  "WS",     0xFFEBA541u, 0xFFEBBE82u };
            rBands[2] = { 21.8f, "Magic",  0xFF508CF0u, 0xFF8CAAF0u }; rBands[3] = { 25.0f, "Ranged", 0xFFEBCD3Cu, 0xFFEBD26Eu };
            rBands[4] = { 30.0f, "Enmity", 0xFFE1504Bu, 0xFFEB827Du }; rScale = 35.0f; rN = 5;
        } else if (rDist > 0.0f) rN = range_zones(g.target.spawnType, 0.5f, rBands, rScale);
    }
    const bool  rMinimal = ui_config().tgtRangeMin != 0;   // MINIMAL : just the number (coloured by its zone), drawn in the detail row -> the GAUGE section is skipped
    const bool  rShow   = rN > 0 && !rMinimal;             // the below-action GAUGE only ; the minimal number lives in the detail row
    const float rTri    = snap(6.0f * S);                   // room above the gauge for the cursor triangle
    const float rGaugeH = snap(15.0f * S * ui_config().tgtRangeH);   // height : own multiplier (like the HP bar's tgtBarH)
    const float rLblSz  = snap(tgt_sz(TGT_RANGE, 9.0f * S));         // zone-label size (TGT_RANGE typography)
    const float rGaugeY = snap(belowAct + rTri + snap(4.0f * S));                                 // gauge top (below the action bar)
    const float rSecH   = rShow ? snap(rTri + snap(4.0f * S) + rGaugeH + rLblSz * 2.1f + snap(6.0f * S)) : 0.0f;
    const float belowRange = rShow ? snap(belowAct + rSecH) : belowAct;                            // baseline for the debuffs below
    // DEBUFF ROW PLACEMENT : 0 = inside (grows the box) / 1 below / 2 above / 3 left / 4 right (1-4 float OUTSIDE).
    // With no box, "inside" is meaningless -> fall back to Below.
    int buffPosRaw = (ui_config().tgtBuffPos < 0 || ui_config().tgtBuffPos > 4) ? 0 : ui_config().tgtBuffPos;
    if (buffPosRaw == 0 && ui_config().tgtBox == 0) buffPosRaw = 1;
    const int  buffPos   = (nd > 0) ? buffPosRaw : 0;
    const bool buffInside = (buffPos == 0);
    const bool buffVert   = (buffPos == 3 || buffPos == 4);
    const float rowUnit = icon + (showTimers ? (tGapY + timerPx) : 0.0f);   // one horizontal row : icon + timer below
    const float rowGap  = snap(4.0f * S);                                    // gap between wrapped rows
    const int   hLines  = buffVert ? 1 : nLines;                             // horizontal placements wrap into rows (vertical -> 2 columns)
    const float rowsH   = hLines * rowUnit + (hLines - 1) * rowGap;          // total stacked-rows height
    const float bandY = snap(belowRange + 7.0f * S);                    // the INSIDE rows' top Y (below the bar / detail / range)
    // the box HUGS its content : the range section + only the INSIDE debuff rows grow the box (outside keeps name+bar+range).
    const float contentBot = (nd > 0 && buffInside) ? (bandY + rowsH + (showTimers ? tMargin : snap(2.0f * S))) : belowRange;
    // SUB-TARGET (<st>) : drawn as its OWN small box at the top-right (see the end of draw) -- it does NOT grow the
    // main box. Shown only while a sub-target cursor is up.
    const bool subShow = ui_config().tgtSub && (fake ? true : (g.hasSubTarget && g.subTarget.valid));
    const float H = snap(contentBot - py + outerPad);
    lastH_ = H;                              // cache for next frame's edit-mode grab hit-rect
    // debuff row anchor by placement. With NO box the outside rows hug the actual TEXT (name top / content bottom),
    // not the box border+padding -- otherwise the invisible padding leaves an empty gap.
    const bool noBox = (ui_config().tgtBox == 0);
    const float blockH = rowsH;                                             // above/below block height (no bottom margin)
    const float topAnchor = noBox ? (py + outerPad) : py;                  // above : name top when boxless, else box top
    const float botAnchor = noBox ? belowRange : (py + H);                 // below : content bottom when boxless, else box bottom
    float dbX = innerX, dbY = bandY;
    if (buffPos == 1)      { dbX = px + outerPad;              dbY = botAnchor + snap(4.0f * S); }              // below (outside)
    else if (buffPos == 2) { dbX = px + outerPad;              dbY = topAnchor - blockH - snap(4.0f * S); }    // above (outside)
    else if (buffPos == 3) { dbX = px - icon - snap(6.0f * S); dbY = py + outerPad; }                           // left (vertical column)
    else if (buffPos == 4) { dbX = px + W + snap(6.0f * S);    dbY = py + outerPad; }                           // right (vertical column)

    // ---- edit-mode alignment overlay : grid + centre guides + axis rails, only while dragging (shared) ----
    if (editing && !demo_ && g_tgtEdit.dragging)
        edit_box_grid(dev, f, g_tgtEdit, px, py, W, lastH_, ui_config().tgtCenterH != 0, ui_config().tgtCenterV != 0);

    // ---- box chrome : the chosen Box Theme (same as the party). The chrome is a big OPAQUE surface, so at a
    //      given alpha it reads as more visible than the thin name/bar -> fade it on a STEEPER curve (a^2) so it
    //      disappears in sync with the content instead of lingering. Skipped entirely in NO-BOX mode (tgtBox=0),
    //      where the name/%/bar/buffs/timer float on the game with a heavier outline (applied below). ----
    setup_color_state(dev);
    const bool drawBox = ui_config().tgtBox != 0;
    const float ca = a * a * clampf(ui_config().tgtBoxAlpha, 0.0f, 1.0f);   // chrome fade x user opacity (content stays opaque)
    const u32 tint = mul_a(0xFFFFFFFF, ca);
    // "Copy Party" : follow the party box theme (skinTheme/skinLum) instead of the Target's own.
    const bool copyParty = ui_config().tgtThemeCopy != 0;
    const int   theme = copyParty ? ui_config().skinTheme : ui_config().tgtTheme;
    const float lum   = copyParty ? ui_config().skinLum   : ui_config().tgtLum;
    const unsigned hue = copyParty ? ui_config().skinHue  : ui_config().tgtHue;   // custom box hue (0 = preset)
    if (!drawBox) {
        /* no box : draw nothing here -- the elements carry their own contrast */
    } else if (window_theme_is_proc(theme)) {
        draw_proc_window(dev, theme, px, py, W, H, tint, false, true, lum, hue);
    } else if (copyParty && f.skin && f.skin->ready()) {
        draw_window(dev, *f.skin, px, py, W, H, tint, S, false, true);   // reuse the party's already-loaded FFXI skin
    } else {
        // FFXI family (own theme) : the Target owns its OWN window skin so its texture variant is INDEPENDENT of
        // the party's (family 0 -> the flat theme index IS the texture theme index). Reload only when it changes.
        if (tgtSkinVar_ != theme) { tgtSkin_.dispose(); tgtSkin_.load(dev, window_theme_name(theme)); tgtSkinVar_ = theme; }
        if (tgtSkin_.ready()) {
            draw_window(dev, tgtSkin_, px, py, W, H, tint, S, false, true);
        } else {
            const float R = snap(6.0f * S);
            rrect_bordered(dev, px, py, W, H, R, mul_a(0xFF232E54, ca), mul_a(0xFF080B1A, ca), mul_a(0x6699BBFF, ca), 1.0f);
        }
    }

    // ---- HP bar : a real HD animated FIOLE (the Player-Hub liquid vial), borrowed via the vial provider.
    //      Streaming liquid + glass + green->orange->red palette + critical alarm are all handled by the fiole. ----
    setup_color_state(dev);
    const u32 hc = hp_color(hpp_);
    const bool crit = present && hpp_ > 0.0f && hpp_ <= 25.0f;
    { LiquidBars* vp = vial_provider();
      // a clean AA dark CAPSULE track behind the fiole -> smooth ROUND ends (the fiole's stencil-clipped
      // capsule has hard/transparent corners that looked bevelled against the box ; the AA track owns the silhouette).
      rrect(dev, barX, barY, barW, barH, barR, mul_a(0xFF0A0E14, a), mul_a(0xFF05070C, a));
      // the fiole ignores our fade alpha -> only draw it once the box is mostly in (avoids a hard pop vs the fading frame).
      if (vp && vp->vial_ready() && a > 0.4f) {
          vp->draw_vial_scaled(dev, f.t, barX, barY, barW, barH, 0 /*HP*/, hpp_ / 100.0f, hc, 0.0f, crit ? 1.0f : 0.0f, 4);
          if (hppGhost_ > hpp_ + 0.3f) {                                  // DELAYED damage trail on top : strong orange = the HP just lost
              rrect_clip_begin(dev, barX, barY, barW, barH, barR);
              const float gx = barX + barW * (hpp_ / 100.0f), gw = barW * ((hppGhost_ - hpp_) / 100.0f);
              grad_quad(dev, gx, barY, gw, barH, mul_a(0xDCFF9A2C, a), mul_a(0xDCFF9A2C, a), mul_a(0xD4EE5A0E, a), mul_a(0xD4EE5A0E, a));
              rrect_clip_end(dev);
              setup_color_state(dev);
          }
      } else {                                                          // fallback (fiole not ready / fading out) : flat capsule fill on the track
          const float fw = barW * (hpp_ / 100.0f);
          if (fw > 1.0f) rrect_left(dev, barX, barY, fw, barH, barR, mul_a(hc, a), mul_a(scl(hc, 0.6f), a));
      }
      // AA capsule rim on top -> a crisp smooth outline hiding the fiole's hard clip edge at the round ends.
      // The fiole leaves a bound texture + MODULATE op ; reset to colour-quad state first, else the rim AND the
      // lock padlock below sample that texture's corner texel and come out a wrong colour (only the damage-trail
      // path reset it before, so the bug only showed when HP was stable).
      setup_color_state(dev);
      rrect_stroke(dev, barX, barY, barW, barH, barR, mul_a(0x66FFFFFF, a), snap(1.0f * S));
    }

    // ---- lock-on : a red padlock LEFT of the name when the main target is LOCKED (<t>). Drawn in the
    //      colour phase (before the font pass) ; the name shifts right to make room for it. ----
    const bool locked = !fake && g.targetLocked && present;
    const float lockH = nameSz * 0.88f;
    const float lockW = locked ? (lockH * 0.80f + snap(5.0f * S)) : 0.0f;
    if (locked) draw_lock(dev, innerX, nameCy, lockH, 0xFFFF4030, a);
    const float nameX = innerX + lockW;

    // ---- text : name (left) + HP% number (right), on the top row -- each element resolves its own font /
    //      size / outline / colour / CAPS from ui_config().tgtText[TGT_*]. NO-BOX mode : opaque-black stroke +
    //      a heavier outline so the text stays legible floating on any game background. ----
    const u32   stk = mul_a(drawBox ? 0xC8000000u : 0xFF000000u, a);
    const float owK = drawBox ? 1.0f : 2.1f;                              // outline boost when there's no box behind
    const u32 nameCol = target_name_color(g, present, fake);   // NPC/mob-claim/PC-allegiance colour (see the helper)
    Font* fName = tgt_font(f.fonts, f.font, TGT_NAME);
    if (fName) {
        fName->begin(dev);
        char nbuf[28]; const char* nm = tgt_up(TGT_NAME, lastName_, nbuf, 28);
        fName->draw_lc(dev, nameX, nameCy, nm, nameSz, mul_a(nameCol, a), stk, tgt_ow(TGT_NAME, 1.2f * S * owK));
    }
    Font* fHp = tgt_font(f.fonts, f.font, TGT_HP);
    float hpNumW = 0.0f;                                                   // HP% number width -> the position badge sits to its LEFT
    if (fHp) {
        fHp->begin(dev);
        int hv = (int)(hpp_ + 0.5f); if (hv < 0) hv = 0; if (hv > 100) hv = 100;
        char pct[8]; sprintf(pct, "%d%%", hv);
        const float psz = tgt_sz(TGT_HP, 15.0f * S * 0.92f), pw = fHp->measure(pct, psz);
        hpNumW = pw;
        fHp->draw_lc(dev, innerX + innerW - pw, nameCy, pct, psz, mul_a(tgt_col(TGT_HP, hp_color(hpp_)), a), stk, tgt_ow(TGT_HP, 1.2f * S * owK));
    }

    // ---- POSITION badge : where WE stand relative to the MOB's facing -- Front (bad) / Flank (warn) / Behind
    //      (good). The chevron points toward our bearing around the mob (up = the mob's front). Mobs only ; sits
    //      on the name row, just LEFT of the HP%. NOTE: heading sign not yet calibrated in-game -- if Front/Behind
    //      or the arrow read reversed, flip `rel` (negate or +PI). ----
    if (present && (fake || g.target.spawnType == 0x10)) {
        float rel;
        if (fake) rel = 3.14159265f;                                       // preview : behind (arrow down, green)
        else {
            const float pdx = party().selfX_ - g.target.posX, pdz = party().selfZ_ - g.target.posZ;
            // Our SCREEN bearing from the mob (north-up minimap convention : screen offset = (dx, -dz)) minus the
            // mob's facing. rel=0 -> we're in front (arrow up), rel=+-PI -> behind (down). Same angle drives the
            // chevron rotation and the Front/Flank/Behind zone, so they can't disagree.
            rel = atan2f(-pdz, pdx) - g.target.heading;
            while (rel >  3.14159265f) rel -= 6.28318530f;
            while (rel < -3.14159265f) rel += 6.28318530f;
        }
        const float frontDeg = fabsf(rel) * 57.29578f;
        const char* plbl; u32 pcol;
        if      (frontDeg <=  60.0f) { plbl = "Front";  pcol = 0xFFE1504Bu; }   // facing us -> bad (red)
        else if (frontDeg >= 120.0f) { plbl = "Behind"; pcol = 0xFF3CC846u; }   // at its back -> good (green)
        else                         { plbl = "Flank";  pcol = 0xFFEBA541u; }   // side -> warn (orange)
        Font* pf = tgt_font(f.fonts, f.font, TGT_NAME);
        const float plsz = tgt_sz(TGT_NAME, 10.0f * S);
        // FIXED layout : reserve the widest label ("Behind") and the widest HP% ("100%") so the icon + text NEVER
        // shift as the content length changes (Front/Flank/Behind, 7%/100%). Anchor both to a fixed x ; the label
        // is drawn LEFT-aligned from that x so its start never moves.
        const float slotW  = pf ? pf->measure("Behind", plsz) : 0.0f;
        Font* hf2 = tgt_font(f.fonts, f.font, TGT_HP);
        const float hpSlot = hf2 ? hf2->measure("100%", tgt_sz(TGT_HP, 15.0f * S * 0.92f)) : hpNumW;
        const float arSz = snap(nameSz * 0.72f), gap = snap(5.0f * S);
        const float ringR = snap(arSz * 0.72f);                            // ring around the chevron (the mockup's .posring)
        const float badgeR = innerX + innerW - hpSlot - snap(12.0f * S);   // fixed right edge of the badge zone (clear of the widest HP%)
        const float lblX = badgeR - slotW, arCx = lblX - gap - ringR;      // label LEFT (fixed) ; arrow centre (fixed)
        setup_color_state(dev);
        const float ringT = snap(1.7f * S), ri = ringR - ringT;            // AA ring = colour disc with a dark disc punched inside
        rrect(dev, arCx - ringR, nameCy - ringR, 2.0f * ringR, 2.0f * ringR, ringR, mul_a(pcol, a), mul_a(pcol, a), snap(1.2f * S));           // AA colour disc
        rrect(dev, arCx - ri,    nameCy - ri,    2.0f * ri,    2.0f * ri,    ri,    mul_a(0xCC0B0E16u, a), mul_a(0xCC0B0E16u, a), snap(1.2f * S)); // AA dark inner -> leaves a feathered ring
        draw_dir_arrow(dev, arCx, nameCy, arSz * 0.86f, -rel - 3.14159265f, mul_a(pcol, a));   // chevron INSIDE the ring ; NEGATED angle -> mirror left/right (screen bearing was flipped) while front=down / behind=up stays

        if (pf) { pf->begin(dev); pf->draw_lc(dev, lblX, nameCy, plbl, plsz, mul_a(pcol, a), stk, tgt_ow(TGT_NAME, 1.0f * S * owK)); }
    }
    // ---- detail row : target SPEED % (left) + Treasure Hunter level (right) -- each with its own config element ----
    if (showDetail) {
        const float dcy = detailY + detailH * 0.5f;
        const float dIcon = dIconSz, dIconY = dcy - dIcon * 0.5f;                 // detail-row icon size (own slider)
        const u32   iTint = mul_a(0xFFFFFFFFu, a);                                 // full-brightness icon tint
        if (showSpd) {   // SPD : text ("Spd +12%") OR the buff-atlas Speed icon (cell 32) + the % value. green/red by speed.
            const float ssz = snap(tgt_sz(TGT_SPD, 11.0f * S)), sow = tgt_ow(TGT_SPD, 1.0f * S * owK);
            const u32   scol = mul_a(tgt_col(TGT_SPD, spdCol), a);
            char sv[16]; sprintf(sv, "%+d%%", (int)(spdPct + (spdPct >= 0.0f ? 0.5f : -0.5f)));
            Font* sf = tgt_font(f.fonts, f.font, TGT_SPD);
            if (ui_config().tgtSpeedIcon && buff_tex_) {   // icon on the left, value to its right
                const float au = (float)BUFF_CELL / (float)BUFF_ATLAS_W, av = (float)BUFF_CELL / (float)BUFF_ATLAS_H;
                const float u0 = (float)(TB_SPEED_ID % BUFF_COLS) * au, v0 = (float)(TB_SPEED_ID / BUFF_COLS) * av;
                setup_tex_state(dev, buff_tex_);
                tquad(dev, innerX, dIconY, dIcon, dIcon, u0, u0 + au, v0, v0 + av, iTint, iTint);
                dSetTex(dev, 0, 0);
                if (sf) { sf->begin(dev); sf->draw_lc(dev, innerX + dIcon + snap(3.0f * S), dcy, sv, ssz, scol, stk, sow); }
            } else if (sf) {                               // text mode : CAPS honoured (not forced)
                sf->begin(dev);
                char sb[24]; sprintf(sb, "Spd %s", sv);
                char sbu[24]; const char* sl = tgt_up(TGT_SPD, sb, sbu, 24);
                sf->draw_lc(dev, innerX, dcy, sl, ssz, scol, stk, sow);
            }
        }
        if (showTH) {    // TH : text ("TH9") OR the coffer icon + level, both right-aligned. gold (custom colour overrides).
            const float hsz = snap(tgt_sz(TGT_TH, 11.0f * S)), how = tgt_ow(TGT_TH, 1.0f * S * owK);
            const u32   hcol = mul_a(tgt_col(TGT_TH, 0xFFF6C64Eu), a);
            Font* hf = tgt_font(f.fonts, f.font, TGT_TH);
            if (ui_config().tgtThIcon && th_tex_ && hf) {  // value on the right, coffer to its left
                char hv[8]; sprintf(hv, "%d", th);
                const float vw = hf->measure(hv, hsz), groupR = innerX + innerW, gap = snap(2.0f * S);
                hf->begin(dev); hf->draw_lc(dev, groupR - vw, dcy, hv, hsz, hcol, stk, how);
                setup_tex_state(dev, th_tex_);
                tquad(dev, groupR - vw - gap - dIcon, dIconY, dIcon, dIcon, 0.0f, 1.0f, 0.0f, 1.0f, iTint, iTint);
                dSetTex(dev, 0, 0);
            } else if (hf) {                               // text mode
                hf->begin(dev);
                char tb[16]; sprintf(tb, "TH%d", th);
                char tbu[16]; const char* tl = tgt_up(TGT_TH, tb, tbu, 16);
                const float tw = hf->measure(tl, hsz);
                hf->draw_lc(dev, innerX + innerW - tw, dcy, tl, hsz, hcol, stk, how);
            }
        }
        if (rangeMinShown && rN > 0) {   // MINIMAL distance : the number, CENTRED in the detail row (between Speed & TH), coloured by its range zone
            u32 zc = rBands[rN - 1].lcol;                                    // past the last zone -> keep the last band's colour
            for (int i = 0; i < rN; ++i) if (rDist <= rBands[i].to) { zc = rBands[i].lcol; break; }
            Font* rlf = tgt_font(f.fonts, f.font, TGT_RANGE);
            if (rlf) {
                const float rsz = snap(tgt_sz(TGT_RANGE, 11.0f * S)), row2 = tgt_ow(TGT_RANGE, 1.0f * S * owK);
                char nb[12]; sprintf(nb, "%.1f", rDist > 99.9f ? 99.9f : rDist);
                rlf->begin(dev); rlf->draw_c(dev, innerX + innerW * 0.5f, dcy, nb, rsz, mul_a(tgt_col(TGT_RANGE, zc), a), stk, row2);
            }
        }
    }

    // ---- ACTION bar : the target's live action. Slot is ALWAYS reserved (a faint empty track) so the box height
    //      never jumps ; while the mob casts/readies, the spell/ability NAME (left) + a filling bar show. Fill is
    //      MAGIC-PURPLE for a spell (cat 8), TP/WS-ORANGE for a weaponskill / ability / TP move (cat 7). ----
    if (actTrack) {
        const float aby = snap(actY + actNameSz + snap(3.0f * S)), abr = actBarH2 * 0.5f;
        setup_color_state(dev);
        rrect(dev, barX, aby, barW, actBarH2, abr, mul_a(0x50121828u, a), mul_a(0x500C1120u, a), snap(1.0f * S));   // faint empty track (reserved slot)
        if (actShow) {
            rrect(dev, barX, aby, barW, actBarH2, abr, mul_a(0xC0121828u, a * castA), mul_a(0xC00C1120u, a * castA), snap(1.0f * S));   // solid track under the fill
            const u32 cTop = castKind == 0 ? 0xFF6FA8FFu : (castKind == 2 ? 0xFFFFD65Au : 0xFFF2B45Au);   // spell BLUE / JA YELLOW / TP-WS orange
            const u32 cBot = castKind == 0 ? 0xFF4C82E6u : (castKind == 2 ? 0xFFE0A828u : 0xFFE08A2Eu);
            const float cp = castPct < 0.0f ? 0.0f : (castPct > 1.0f ? 1.0f : castPct);
            const float fw = barW * cp;
            if (fw > 1.5f) rrect_left(dev, barX, aby, fw, actBarH2, abr, mul_a(cTop, a * castA), mul_a(cBot, a * castA), snap(1.0f * S));
            Font* af = tgt_font(f.fonts, f.font, TGT_NAME);
            if (af) { af->begin(dev);
                char ab[40]; const char* an = tgt_up(TGT_NAME, castName, ab, 40);
                af->draw_lc(dev, innerX, snap(actY + actNameSz * 0.5f), an, actNameSz, mul_a(0xFFE9DEFFu, a * castA), stk, tgt_ow(TGT_NAME, 1.0f * S * owK)); }
        }
    }

    // ---- RANGE gauge : dark track + tinted zone bands + cursor + zone labels + distance number (geometry above) ----
    if (rShow) {
        Font* rlf = tgt_font(f.fonts, f.font, TGT_RANGE);   // labels + number : the TGT_RANGE typography element
        const float rNumSz  = snap(tgt_sz(TGT_RANGE, 12.0f * S));   // distance number size : FIXED base (independent of bar height) -> grow via the Distance text size
        const u32   rNumCol = tgt_col(TGT_RANGE, 0xFFFFFFFFu);            // white by default ; a custom colour overrides
        const bool  rUp     = ui_config().tgtText[TGT_RANGE].upper;
        draw_range_gauge(dev, rlf, innerX, rGaugeY, innerW, rGaugeH, rBands, rN, rScale, rDist,
                         rLblSz, rNumSz, rNumCol, rUp, a, stk, tgt_ow(TGT_RANGE, 1.1f * S * owK), S);
    }

    // ---- debuff row : atlas icons + an approx TIMER per icon. HORIZONTAL (inside/above/below : icons flow right,
    //      timer below) or VERTICAL (left/right : icons stack down, timer to the right). Placement = buffPos. ----
    if (nd > 0 && buff_tex_) {
        setup_tex_state(dev, buff_tex_);
        const float au = (float)BUFF_CELL / (float)BUFF_ATLAS_W, av = (float)BUFF_CELL / (float)BUFF_ATLAS_H;
        const float igapV = snap(3.0f * S * iconK);                      // vertical stacking gap (left/right columns)
        const float leftX  = px - icon - snap(6.0f * S);                 // vertical columns sit left / right of the box
        const float rightX = px + W + snap(6.0f * S);
        // VERTICAL is centred on the box height, based on the FIRST column's icon count (<=10).
        const int   col0 = buffVert ? ((nd + 1) / 2) : 0;   // vertical : BALANCED columns -> primary = ceil(N/2)
        const float colTop = buffVert ? snap(py + H * 0.5f - (col0 * icon + (col0 - 1) * igapV) * 0.5f) : dbY;
        float icx[32], icy[32]; int trem[32]; unsigned char tsf[32], tdir[32]; int tn = 0;   // tdir : 0 below / 1 left / 2 right
        for (int i = 0; i < nd; ++i) {
            const unsigned id = dids[i];                                 // already compacted to valid atlas ids
            float ix, iy; unsigned char dir;
            if (buffVert) {                                              // BALANCED two columns : ceil(N/2) primary + floor(N/2) other
                const bool inCol0 = (i < col0);
                const int  rowc = inCol0 ? i : (i - col0);
                const bool physLeft = (buffPos == 3) ? inCol0 : !inCol0;   // Left: bigger half LEFT ; Right: bigger half RIGHT
                ix = physLeft ? leftX : rightX; iy = colTop + rowc * (icon + igapV); dir = physLeft ? 1 : 2;
            } else {                                                     // 10 per row ; overflow wraps to a second row below -- each row CENTRED on the box
                const int col = i % BUFF_PER_LINE, row = i / BUFF_PER_LINE;
                const int rowN = (row == 0) ? (nd < BUFF_PER_LINE ? nd : BUFF_PER_LINE) : (nd - BUFF_PER_LINE);   // icons on this row
                const float rowW = rowN * icon + (rowN - 1) * igap;
                ix = snap(px + W * 0.5f - rowW * 0.5f) + col * (icon + igap); iy = dbY + row * (rowUnit + rowGap); dir = 0;
            }
            const u32 icol = mul_a(0xFFFFFFFF, a);                       // full brightness -> caster distinction is on the TIMER colour
            const float u0 = (float)(id % BUFF_COLS) * au, v0 = (float)(id / BUFF_COLS) * av;
            tquad(dev, ix, iy, icon, icon, u0, u0 + au, v0, v0 + av, icol, icol);
            icx[tn] = ix; icy[tn] = iy; trem[tn] = drem[i]; tsf[tn] = dself[i]; tdir[tn] = dir; ++tn;
        }
        dSetTex(dev, 0, 0);                                              // don't leave the atlas bound for the next widget
        Font* tf = showTimers ? tgt_font(f.fonts, f.font, TGT_TIMER) : 0;   // TIMER text (own font/size) ; off -> icons only
        if (tf) {
            tf->begin(dev);
            const float tsz = snap(timerPx);
            const u32 tstk = mul_a(drawBox ? 0xDD000000u : 0xFF000000u, a);   // opaque stroke when floating (no box)
            const bool tCustom = ui_config().tgtText[TGT_TIMER].colorOn;   // a custom colour overrides the caster/expiry tint
            for (int i = 0; i < tn; ++i) {
                char tb[12]; const int r = trem[i];
                if (r < 0)        { int a2 = -r; if (a2 >= 60) sprintf(tb, "-%d:%02d", a2 / 60, a2 % 60); else sprintf(tb, "-0:%02d", a2); }   // past the estimate -> negative overage (-0:30)
                else if (r >= 60) sprintf(tb, "%d:%02d", r / 60, r % 60);
                else              sprintf(tb, "%d", r);
                const u32 sem = (r < 0)  ? 0xFFAEB6C2u                                 // past estimate -> grey (overdue / uncertain)
                              : tsf[i]   ? ((r <= 5) ? 0xFFFF7A4Au : 0xFFF6C64Eu)      // YOURS -> gold (red about to wear off)
                                         : 0xFFD8DEE8u;                                // OTHERS -> white / grey
                const u32 tc = mul_a(tCustom ? ui_config().tgtText[TGT_TIMER].color : sem, a);
                const float ow = tgt_ow(TGT_TIMER, 1.1f * S * owK);
                if (tdir[i] == 1)      tf->draw_lc(dev, icx[i] - snap(3.0f * S) - tf->measure(tb, tsz), icy[i] + icon * 0.5f, tb, tsz, tc, tstk, ow);   // left column : timer outward-left
                else if (tdir[i] == 2) tf->draw_lc(dev, icx[i] + icon + snap(3.0f * S), icy[i] + icon * 0.5f, tb, tsz, tc, tstk, ow);                   // right column : timer outward-right
                else                   tf->draw_c(dev, icx[i] + icon * 0.5f, icy[i] + icon + tGapY + timerPx * 0.5f, tb, tsz, tc, tstk, ow);            // horizontal : timer below
            }
        }
    }

    // ---- SUB-TARGET (<st>) : its OWN small box at the TOP-RIGHT (the mockup's Focus bar). ONE compact row :
    //      name + themed FIOLE HP bar (% inline) + distance. Same box theme as the Target. Only while <st> is up. ----
    if (subShow) {
        const TargetEntity& se = g.subTarget;
        const char* rawName = fake ? "Bumba" : (se.name[0] ? se.name : "Sub");
        int shp = fake ? 93 : se.hpp; if (shp < 0) shp = 0; if (shp > 100) shp = 100;
        const u32 snCol = fake ? 0xFFF0787Au : target_name_color_e(se);
        const u32 hcS   = hp_color((float)shp);
        const float sPad = snap(7.0f * S), sGap = snap(6.0f * S);
        const float sTxt = snap(tgt_sz(TGT_NAME, 12.0f * S)), sBarH = snap(12.0f * S), sBarR = sBarH * 0.5f, dsz = snap(sTxt * 0.85f);
        const float rowH = sBarH > sTxt ? sBarH : sTxt;
        Font* sf = tgt_font(f.fonts, f.font, TGT_NAME);
        // MEASURE the content -> the box HUGS it (name + fixed HP bar + distance).
        char nb[28]; { int i = 0; for (; rawName[i] && i < 27; ++i) nb[i] = rawName[i]; nb[i] = 0; }
        const float nameW = sf ? sf->measure(nb, sTxt) : 0.0f;   // FULL name width : the box hugs it (no cap -> the name never runs under the bar)
        const float sdx = se.posX - party().selfX_, sdz = se.posZ - party().selfZ_;
        const float sDist = fake ? 18.4f : sqrtf(sdx * sdx + sdz * sdz);
        char db[12]; sprintf(db, "%.1fy", sDist > 99.9f ? 99.9f : sDist);
        const float distW = sf ? sf->measure(db, dsz) : 0.0f;
        const float barW = snap(62.0f * S);
        const float subW = snap(2.0f * sPad + distW + sGap + nameW + sGap + barW);   // layout : distance | name | HP bar
        const float subBoxH = snap(2.0f * sPad + rowH);
        // PLACEMENT (tgtSubPos) relative to the main box. The Above positions FUSE (openBottom omits the sub's
        // bottom border so the Target's top border is the shared seam) ; the others are detached, positioned boxes
        // (draw_window only supports openBottom, so only Above can share a seam).
        // Detached positions OVERLAP the main box by the border thickness so the sub box (drawn on top) covers
        // the shared border seam -> a SINGLE border, not a doubled one (draw_window can only open the bottom).
        float subX, subY; bool subFused = false;
        const float ov = snap(2.0f * S);   // border overlap (tune if a sliver doubles / content clips)
        switch (ui_config().tgtSubPos) {
            default:
            case 0: subX = snap(px + W - subW); subY = snap(py - subBoxH);  subFused = true; break;  // Above - Right (fused seam)
            case 1: subX = snap(px);            subY = snap(py - subBoxH);  subFused = true; break;  // Above - Left  (fused seam)
            case 2: subX = snap(px + W - subW); subY = snap(py + H - ov);                    break;  // Below - Right
            case 3: subX = snap(px);            subY = snap(py + H - ov);                    break;  // Below - Left
            case 4: subX = snap(px + W - ov);   subY = snap(py);                             break;  // Right  (flush, top-aligned)
            case 5: subX = snap(px - subW + ov);subY = snap(py);                             break;  // Left   (flush, top-aligned)
        }
        // box chrome : same theme + No-box + Transparency as the Target box (openBottom=true -> fused onto its top).
        setup_color_state(dev);
        const float sCa = a * a * clampf(ui_config().tgtBoxAlpha, 0.0f, 1.0f);   // same chrome opacity as the main box
        const u32   sTint = mul_a(0xFFFFFFFF, sCa);
        if (ui_config().tgtBox == 0) { /* No box : the sub content floats, like the Target */ }
        else if (window_theme_is_proc(theme))                draw_proc_window(dev, theme, subX, subY, subW, subBoxH, sTint, subFused, true, lum, hue);
        else if (copyParty && f.skin && f.skin->ready())     draw_window(dev, *f.skin, subX, subY, subW, subBoxH, sTint, S, subFused, true);
        else if (tgtSkin_.ready())                           draw_window(dev, tgtSkin_, subX, subY, subW, subBoxH, sTint, S, subFused, true);
        else rrect_bordered(dev, subX, subY, subW, subBoxH, snap(5.0f * S), mul_a(0xFF232E54, sCa), mul_a(0xFF080B1A, sCa), mul_a(0x6699BBFF, sCa), 1.0f);
        const float sIx = subX + sPad, rowCy = snap(subY + sPad + rowH * 0.5f);
        const float nameX = snap(sIx + distW + sGap), barX = snap(nameX + nameW + sGap);
        if (sf) { sf->begin(dev); sf->draw_lc(dev, sIx, rowCy, db, dsz, 0xFF9AA6BEu, 0xFF000000u, tgt_ow(TGT_NAME, 1.0f * S)); }   // DISTANCE : left
        if (sf) { sf->begin(dev); sf->draw_lc(dev, nameX, rowCy, nb, sTxt, snCol, 0xFF000000u, tgt_ow(TGT_NAME, 1.2f * S)); }      // name
        {   // themed FIOLE HP bar + inline %
            setup_color_state(dev);
            rrect(dev, barX, snap(rowCy - sBarH * 0.5f), barW, sBarH, sBarR, 0xFF0A0E14u, 0xFF05070Cu);
            LiquidBars* vp = vial_provider();
            if (vp && vp->vial_ready()) vp->draw_vial_scaled(dev, f.t, barX, snap(rowCy - sBarH * 0.5f), barW, sBarH, 0, (float)shp / 100.0f, hcS, 0.0f, shp <= 25 ? 1.0f : 0.0f, 4);
            else { const float fw = barW * ((float)shp / 100.0f); if (fw > 1.0f) rrect_left(dev, barX, snap(rowCy - sBarH * 0.5f), fw, sBarH, sBarR, hcS, scl(hcS, 0.6f)); }
            if (sf) { sf->begin(dev); char pb[6]; sprintf(pb, "%d%%", shp); sf->draw_c(dev, snap(barX + barW * 0.5f), rowCy, pb, snap(sTxt * 0.78f), 0xFFF4FAFFu, 0xFF000000u, snap(1.4f * S)); }
        }
    }
}

} // namespace aio
