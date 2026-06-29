// config_page.cpp -- see config_page.h. Polished, animated, web-style config overlay.
// The AIOHUD window skin fills the WHOLE page (it IS the frame) ; the UI is drawn directly on it.
//
// Motion model : everything interactive eases. A tiny id->value animation table (g_anim) holds one
// 0..1 spring per element ; ease(id, target) nudges it toward target by the frame dt -> smooth hover,
// toggle crossfades, knob grow, etc. The page also fades + scales in, and the content rows stagger.
#include "ui/config_page.h"
#include "gfx/draw.h"      // grad_quad
#include "gfx/font.h"
#include "gfx/window.h"
#include "model/ui_config.h"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace aio {

// Keyboard text entry is fed by the plugin's slot-14 hook (see aio_plugin_key) into ConfigPage's
// feed_char/backspace/enter while the name field is focused -- the hook CONSUMES those keys so the
// game never sees them. No per-frame Win32 polling here.

static inline float snap(float v) { return (float)(int)(v + 0.5f); }
static inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }

static const char* TABS[]     = { "Configuration", "Profile", "Help" };
static const int   NTABS      = 3;
static const char* SECTIONS[] = { "Party / Alliance" };
static const int   NSECT      = 1;

// ---- palette (ARGB) ----
static const u32 C_DIMBG    = 0xCC04070D;
static const u32 C_TABON_T  = 0xFF3E88E8, C_TABON_B  = 0xFF2A60B4;
static const u32 C_TABOFF_T = 0xC0202B3C, C_TABOFF_B = 0xC0151E2C;
static const u32 C_TABHOV_T = 0xD0364865, C_TABHOV_B = 0xD0202E44;
static const u32 C_CONTENT_T= 0xE6141C28, C_CONTENT_B= 0xE60E141E;
static const u32 C_SIDEBAR  = 0xF0161F2D;
static const u32 C_ROWON_T  = 0xFF2C6AC4, C_ROWON_B  = 0xFF234E92;
static const u32 C_BORDER   = 0x33FFFFFF, C_BORDERHI = 0x66FFFFFF;
static const u32 C_TEXT     = 0xFFEAF1FB, C_DIM = 0xFFA6B6CC, C_MUTE = 0xFF6E7E96;
static const u32 C_ACCENT   = 0xFF5AA2FF, C_ACCENTHI = 0xFFBFE0FF, C_STROKE = 0xFF000000, C_CLOSEHOV = 0xFFCE424C;

void ConfigPage::set_tab(int t)     { if (t >= 0 && t < NTABS) tab_ = t; }
void ConfigPage::set_section(int s) { if (s >= 0 && s < NSECT) section_ = s; }

// ---- a global fade factor (open animation), applied to every quad + text colour ----
static float g_fade = 1.0f;
static float g_dt   = 0.016f;   // current frame delta (seconds) -- drives ease()
static inline u32 fa(u32 c) {
    u32 a = (u32)(((c >> 24) & 0xFF) * g_fade + 0.5f);
    return (c & 0x00FFFFFF) | (a << 24);
}
static u32 lerpc(u32 a, u32 b, float t) {
    t = clampf(t, 0.0f, 1.0f);
    int aa = (a>>24)&0xFF, ar = (a>>16)&0xFF, ag = (a>>8)&0xFF, ab = a&0xFF;
    int ba = (b>>24)&0xFF, br = (b>>16)&0xFF, bg = (b>>8)&0xFF, bb = b&0xFF;
    return ((u32)(aa+(int)((ba-aa)*t))<<24) | ((u32)(ar+(int)((br-ar)*t))<<16)
         | ((u32)(ag+(int)((bg-ag)*t))<<8)  |  (u32)(ab+(int)((bb-ab)*t));
}

// ---- per-element animation springs : one 0..1 value per stable id, eased toward a target ----
struct Anim { int id; float v; };
static Anim g_anim[128];
static int  g_animN = 0;
static float ease(int id, float target, float speed = 18.0f) {
    Anim* s = nullptr;
    for (int i = 0; i < g_animN; ++i) if (g_anim[i].id == id) { s = &g_anim[i]; break; }
    if (!s) { if (g_animN >= 128) return target; s = &g_anim[g_animN++]; s->id = id; s->v = target; }
    s->v += (target - s->v) * clampf(g_dt * speed, 0.0f, 1.0f);
    return s->v;
}
// staggered entrance factor (ease-out cubic) for content row i : later rows start a touch later.
static float stagger(float anim, int i) {
    float a = clampf((anim - 0.045f * (float)i) / 0.5f, 0.0f, 1.0f);
    return 1.0f - (1.0f - a) * (1.0f - a) * (1.0f - a);
}

// colour-only quad state (grad_quad uses the current device state ; the font leaves a textured
// stage bound, which would fade any quad drawn after text -> reset before every fill).
static void cs(u32 dev) {
    dSetVS(dev, FVF_XYZRHW_DIFFUSE);
    dSetRS(dev, D3DRS_ALPHABLENDENABLE, 1);
    dSetRS(dev, D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    dSetTex(dev, 0, 0);
    dSetTSS(dev, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1); dSetTSS(dev, 0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    dSetTSS(dev, 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1); dSetTSS(dev, 0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    dSetTSS(dev, 1, D3DTSS_COLOROP, D3DTOP_DISABLE);
}
static void q4(u32 dev, float x, float y, float w, float h, u32 tl, u32 tr, u32 bl, u32 br) {
    cs(dev); grad_quad(dev, x, y, w, h, fa(tl), fa(tr), fa(bl), fa(br));
}
static inline void flat(u32 dev, float x, float y, float w, float h, u32 c) { q4(dev, x, y, w, h, c, c, c, c); }
static inline void vg(u32 dev, float x, float y, float w, float h, u32 t, u32 b) { q4(dev, x, y, w, h, t, t, b, b); }
static void outline(u32 dev, float x, float y, float w, float h, u32 c) {
    flat(dev, x, y, w, 1, c); flat(dev, x, y + h - 1, w, 1, c);
    flat(dev, x, y, 1, h, c); flat(dev, x + w - 1, y, 1, h, c);
}
static void shadow_down(u32 dev, float x, float y, float w, float h, u32 top) {
    q4(dev, x, y, w, h, top, top, top & 0x00FFFFFF, top & 0x00FFFFFF);
}
// soft accent halo BEHIND an element (draw before the element) -> a glow that eases in on hover.
static void halo(u32 dev, float x, float y, float w, float h, u32 col, float t) {
    if (t <= 0.01f) return;
    const float g = snap(3.0f);
    u32 a = (u32)(70.0f * clampf(t, 0.0f, 1.0f));
    u32 c = (col & 0x00FFFFFF) | (a << 24);
    flat(dev, x - g, y - g, w + 2 * g, h + 2 * g, c);
}
static inline bool inrect(const MouseState* m, float x, float y, float w, float h) {
    return m && m->x >= x && m->x < x + w && m->y >= y && m->y < y + h;
}
static void cursor(u32 dev, float x, float y) {
    for (int i = 0; i <= 17; ++i) flat(dev, x - 1.0f, y - 1.0f + (float)i, (float)i * 0.70f + 4.0f, 2.0f, 0xFF0A0D12);
    for (int i = 0; i <= 15; ++i) flat(dev, x + 1.0f, y + 1.0f + (float)i, (float)i * 0.66f + 1.0f, 2.0f, 0xFFFFFFFF);
}

// a small square [<] / [>] stepper button with eased hover. uid = its animation slot.
static bool arrow_btn(u32 dev, Font* fo, const MouseState* mo, bool click, int uid,
                      float x, float y, float s, const char* glyph) {
    const bool hov = inrect(mo, x, y, s, s);
    const float t = ease(uid, hov ? 1.0f : 0.0f);
    halo(dev, x, y, s, s, C_ACCENT, t * 0.7f);
    vg(dev, x, y, s, s, lerpc(0x26FFFFFF, 0x55BFD8FF, t), lerpc(0x12FFFFFF, 0x3370A6E0, t));
    outline(dev, x, y, s, s, lerpc(C_BORDER, C_ACCENT, t));
    fo->begin(dev); fo->draw_c(dev, x + s * 0.5f, y + s * 0.5f, glyph, snap(16.0f), lerpc(C_DIM, C_TEXT, t), fa(C_STROKE), 1.0f + 0.3f * t);
    return hov && click;
}

// A labeled selector row :  "Label                 [<]   value   [>]".  uid = animation base.
static int row_selector(u32 dev, Font* fo, const MouseState* mo, bool click, int uid,
                        float x, float y, float w, const char* label, const char* value) {
    const float rowH = snap(40.0f);
    flat(dev, x, y + rowH, w, 1, 0x14FFFFFF);                         // subtle separator under the row
    fo->begin(dev);
    fo->draw_lc(dev, x + snap(4.0f), y + rowH * 0.5f, label, snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);

    const float aS = snap(32.0f), valW = snap(168.0f), ctlW = aS + valW + aS;
    const float cx = x + w - ctlW, cy = y + (rowH - aS) * 0.5f;
    int delta = 0;

    if (arrow_btn(dev, fo, mo, click, uid + 0, cx, cy, aS, "<")) delta = -1;
    const float vx = cx + aS;
    vg(dev, vx, cy, valW, aS, 0x33101620, 0x33080C12);
    outline(dev, vx, cy, valW, aS, C_BORDER);
    fo->begin(dev); fo->draw_c(dev, vx + valW * 0.5f, cy + aS * 0.5f, value, snap(14.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
    if (arrow_btn(dev, fo, mo, click, uid + 1, vx + valW, cy, aS, ">")) delta = +1;
    return delta;
}
static int wrap(int v, int n) { if (v < 0) return n - 1; if (v >= n) return 0; return v; }

// Box Theme row : a big SQUARE live preview of the window skin, flanked by [<] / [>] (centered on the
// square). Returns -1/+1 on an arrow click. Taller than a normal row -> see THEME_ROW_H for the advance.
static const float THEME_SQ = 84.0f;                         // square swatch size
static const float THEME_ROW_H = THEME_SQ + 16.0f;           // row height (caller advances ry by this + gap)
static int row_theme(u32 dev, Font* fo, const MouseState* mo, bool click, int uid,
                     float x, float y, float w, const char* label, const char* value, const WindowSkin* skin) {
    const float sq = snap(THEME_SQ), rowH = snap(THEME_ROW_H);
    flat(dev, x, y + rowH, w, 1, 0x14FFFFFF);
    fo->begin(dev);
    fo->draw_lc(dev, x + snap(4.0f), y + rowH * 0.5f, label, snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);

    const float aS = snap(32.0f), agap = snap(10.0f);
    const float ctlW = aS + agap + sq + agap + aS;
    const float cx = x + w - ctlW, cyMid = y + rowH * 0.5f, aY = cyMid - aS * 0.5f;
    int delta = 0;

    if (arrow_btn(dev, fo, mo, click, uid + 0, cx, aY, aS, "<")) delta = -1;

    const float sx = cx + aS + agap, sy = y + (rowH - sq) * 0.5f;                 // square preview
    const bool sHov = inrect(mo, sx, sy, sq, sq);
    const float st = ease(uid + 2, sHov ? 1.0f : 0.0f);
    halo(dev, sx, sy, sq, sq, C_ACCENT, st);
    if (skin && skin->ready()) draw_window(dev, *skin, sx, sy, sq, sq, fa(0xFFFFFFFF), 1.0f);
    else vg(dev, sx, sy, sq, sq, 0x33101620, 0x33080C12);
    outline(dev, sx, sy, sq, sq, lerpc(C_BORDER, C_ACCENT, st));
    fo->begin(dev); fo->draw_c(dev, sx + sq * 0.5f, sy + sq - snap(12.0f), value, snap(14.0f), fa(C_TEXT), fa(0xFF000000), 2.2f);   // name at the swatch bottom, heavy stroke

    if (arrow_btn(dev, fo, mo, click, uid + 1, sx + sq + agap, aY, aS, ">")) delta = +1;
    return delta;
}

// A labeled SLIDER row :  "Label        [===O      ]  value". Drag the track/knob to set. Updates
// *v01 (normalized 0..1) LIVE while dragging and persists once on release. Returns true the frames it
// changed. g_slider latches the dragged slider so a press that wanders off the row keeps control of it.
static int g_slider = -1;   // id of the slider being dragged (-1 = none)
static bool row_slider(u32 dev, Font* fo, const MouseState* mo, int id,
                       float x, float y, float w, const char* label, const char* valueText, float* v01) {
    const float rowH = snap(40.0f);
    flat(dev, x, y + rowH, w, 1, 0x14FFFFFF);                         // subtle separator under the row
    fo->begin(dev);
    fo->draw_lc(dev, x + snap(4.0f), y + rowH * 0.5f, label, snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);

    const float valW = snap(56.0f), gap = snap(12.0f), trkW = snap(176.0f);
    const float trkX = x + w - valW - gap - trkW;
    const float cy = y + rowH * 0.5f, trkH = snap(6.0f), trkY = cy - trkH * 0.5f, knobR = snap(8.0f);

    const bool hot = mo && mo->x >= trkX - knobR && mo->x < trkX + trkW + knobR && mo->y >= y && mo->y < y + rowH;
    bool changed = false;
    if (mo && mo->clicked && hot && g_slider < 0) g_slider = id;     // grab on press over the track
    const bool act = (g_slider == id);
    if (act) {
        if (mo && mo->down) {
            float nv = clampf((mo->x - trkX) / trkW, 0.0f, 1.0f);
            if (nv != *v01) { *v01 = nv; changed = true; }
        } else { g_slider = -1; save_ui_config(); }                  // release -> persist once
    }

    const float fillW = snap(trkW * clampf(*v01, 0.0f, 1.0f));
    vg(dev, trkX, trkY, trkW, trkH, 0x33101620, 0x33080C12);          // track groove
    outline(dev, trkX, trkY, trkW, trkH, C_BORDER);
    vg(dev, trkX, trkY, fillW, trkH, fa(C_ACCENTHI), fa(C_ACCENT));   // filled portion (subtle gradient)

    // knob : eases bigger on hover / while dragging, with a soft halo + drop seat.
    const float kt = ease(40 + id, (hot || act) ? 1.0f : 0.0f);
    const float kr = knobR * (1.0f + 0.35f * kt);
    const float kx = trkX + fillW;
    halo(dev, kx - kr, cy - kr, 2 * kr, 2 * kr, C_ACCENT, kt);
    vg(dev, kx - kr, cy - kr, 2 * kr, 2 * kr, lerpc(0xFFCFE0F5, 0xFFFFFFFF, kt), lerpc(0xFF8FB6E8, 0xFF5AA2FF, kt));
    outline(dev, kx - kr, cy - kr, 2 * kr, 2 * kr, C_BORDERHI);
    fo->begin(dev);
    fo->draw_c(dev, trkX + trkW + gap + valW * 0.5f, cy, valueText, snap(14.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
    return changed;
}

// A pill toggle chip with a label, eased on/off colour crossfade + a hover halo. uid = animation base.
static bool toggle_chip(u32 dev, Font* fo, const MouseState* mo, bool click, int uid,
                        float x, float y, float w, float h, const char* label, bool on) {
    const bool hov = inrect(mo, x, y, w, h);
    const float st = ease(uid + 0, on ? 1.0f : 0.0f, 14.0f);         // on/off crossfade
    const float ht = ease(uid + 1, hov ? 1.0f : 0.0f);               // hover lift
    halo(dev, x, y, w, h, on ? 0xFF49C46A : 0xFFB85A66, (st > 0.5f ? st : (1.0f - st)) * ht);
    const u32 onT = 0xFF2E8C49, onB = 0xFF206030, offT = 0xFF552530, offB = 0xFF3A1820;
    u32 t = lerpc(offT, onT, st), b = lerpc(offB, onB, st);
    t = lerpc(t, lerpc(0xFF6E2E38, 0xFF3FA85A, st), ht * 0.6f);       // brighten on hover
    vg(dev, x, y, w, h, t, b);
    outline(dev, x, y, w, h, lerpc(C_BORDER, C_BORDERHI, ht));
    // a little state dot, left of the label : green when on, dim red when off
    const float dr = snap(4.0f), dx = x + snap(9.0f), dy = y + h * 0.5f;
    flat(dev, dx - dr, dy - dr, 2 * dr, 2 * dr, fa(lerpc(0xFF7E3A42, 0xFF8CF2A8, st)));
    fo->begin(dev); fo->draw_c(dev, x + w * 0.5f + snap(5.0f), dy, label, snap(12.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
    return hov && click;
}

// a wide push button (Edit Layout / Default) with eased hover + accent halo. tone : 0 = neutral blue,
// 1 = danger red. uid = animation slot. Returns true on click.
static bool push_btn(u32 dev, Font* fo, const MouseState* mo, bool click, int uid,
                     float x, float y, float w, float h, const char* label, int tone) {
    const bool hov = inrect(mo, x, y, w, h);
    const float t = ease(uid, hov ? 1.0f : 0.0f);
    const u32 idleT = tone ? 0xFF3A2A2E : 0xFF2A3548, idleB = tone ? 0xFF281D20 : 0xFF1D2738;
    const u32 hovT  = tone ? 0xFFB85050 : 0xFF3A82E0, hovB  = tone ? 0xFF8A3A3A : 0xFF2A61B6;
    halo(dev, x, y, w, h, tone ? 0xFFE06868 : C_ACCENT, t * 0.8f);
    vg(dev, x, y, w, h, lerpc(idleT, hovT, t), lerpc(idleB, hovB, t));
    outline(dev, x, y, w, h, lerpc(C_BORDERHI, tone ? 0xFFE57078 : C_ACCENTHI, t));
    fo->begin(dev); fo->draw_c(dev, x + w * 0.5f, y + h * 0.5f, label, snap(13.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
    return hov && click;
}

void ConfigPage::draw(const Frame& f, float sw, float sh) {
    if (!open_) return;
    u32 dev = f.dev;
    Font* fo = f.font;
    if (!fo || !fo->ready() || sw <= 0 || sh <= 0) return;
    const MouseState* mo = f.mouse;
    const bool click = mo && mo->clicked;

    // --- frame clock (shared by both modes) : drives the open fade + every ease() spring ---
    float dt = (lastT_ < 0.0f) ? 0.016f : (f.t - lastT_);
    if (dt < 0.0f || dt > 0.25f) dt = 0.016f;
    lastT_ = f.t;
    g_dt = dt;

    // --- EDIT LAYOUT mode : hide the page (the game + the real boxes show through) and draw only a
    //     floating toolbar. The party/alliance boxes handle their own drag/resize (see party.cpp). ---
    if (ui_config().editLayout) {
        g_fade = 1.0f;
        const float bw = snap(760.0f), bh = snap(46.0f), bx = snap((sw - bw) * 0.5f), by = snap(22.0f);
        const float pop = ease(900, 1.0f, 16.0f);                                   // subtle slide-in
        const float byA = by - (1.0f - pop) * snap(10.0f);
        shadow_down(dev, bx - snap(4.0f), byA + bh, bw + snap(8.0f), snap(10.0f), 0x55000000);
        vg(dev, bx, byA, bw, bh, 0xF0202B3C, 0xF0141C28);
        flat(dev, bx, byA, bw, 1, 0x55FFFFFF);                                       // top sheen
        outline(dev, bx, byA, bw, bh, C_BORDERHI);
        fo->begin(dev);
        fo->draw_lc(dev, bx + snap(16.0f), byA + bh * 0.5f, "EDIT LAYOUT  |  drag,  wheel = size", snap(14.0f), C_TEXT, C_STROKE, 1.0f);
        const float bh2 = snap(30.0f), dby = byA + (bh - bh2) * 0.5f;
        const float db = snap(80.0f), dbx = bx + bw - db - snap(10.0f);             // Done (far right)
        const float rb = snap(90.0f), rbx = dbx - rb - snap(8.0f);                  // Default (left of Done)
        const float sb = snap(120.0f), sbx = rbx - sb - snap(8.0f);                 // Set Ref (left of Default)
        // Set Ref : lock the party reference Y (align the party box on the native block, then click).
        const bool refSet = ui_config().partyRefY >= 0.0f;
        {
            const bool shv = inrect(mo, sbx, dby, sb, bh2);
            const float t = ease(901, shv ? 1.0f : 0.0f);
            halo(dev, sbx, dby, sb, bh2, refSet ? 0xFF49C46A : C_ACCENT, t * 0.8f);
            const u32 iT = refSet ? 0xFF2E6E3A : 0xFF2A3548, iB = refSet ? 0xFF205028 : 0xFF1D2738;
            vg(dev, sbx, dby, sb, bh2, lerpc(iT, refSet ? 0xFF3FA85A : 0xFF3A82E0, t), lerpc(iB, refSet ? 0xFF2A7A44 : 0xFF2A61B6, t));
            outline(dev, sbx, dby, sb, bh2, lerpc(C_BORDERHI, C_ACCENTHI, t));
            fo->begin(dev); fo->draw_c(dev, sbx + sb * 0.5f, dby + bh2 * 0.5f, refSet ? "Ref set" : "Set Ref", snap(13.0f), C_TEXT, C_STROKE, 1.0f);
            if (shv && click && ui_config().box[0].posSet) { ui_config().partyRefY = ui_config().box[0].y; save_ui_config(); }
        }
        if (push_btn(dev, fo, mo, click, 902, rbx, dby, rb, bh2, "Default", 1)) reset_boxes();
        if (push_btn(dev, fo, mo, click, 903, dbx, dby, db, bh2, "Done", 0)) { ui_config().editLayout = false; save_ui_config(); }
        if (mo && mo->focused) cursor(dev, mo->x, mo->y);   // hide our cursor when the game isn't the OS foreground
        return;
    }

    // --- open animation : fade + a tiny scale-in ---
    anim_ = clampf(anim_ + dt / 0.18f, 0.0f, 1.0f);
    const float e = 1.0f - (1.0f - anim_) * (1.0f - anim_) * (1.0f - anim_);   // ease-out cubic
    g_fade = e;
    const float pulse = 0.5f + 0.5f * sinf(f.t * 3.0f);                        // 0..1 slow pulse

    // ===== BACK LAYER : dim + the AIOHUD skin filling the WHOLE page (this IS the frame) =====
    flat(dev, 0, 0, sw, sh, C_DIMBG);   // fa() already fades this by `e`
    if (f.skin && f.skin->ready()) draw_window(dev, *f.skin, 0, 0, sw, sh, fa(0xFFFFFFFF), 1.0f);

    // content inset from the skin border (no second frame -- we draw straight on the skin)
    const float m = snap(30.0f);
    const float ix = m, iy = m, iw = sw - 2 * m;
    const float pageBot = sh - m;

    // ===== HEADER (title + close), directly on the skin =====
    fo->begin(dev);
    const float titleSz = snap(28.0f);
    fo->draw_lc(dev, ix, iy + snap(22.0f), "AIOHUD", titleSz, fa(C_TEXT), fa(C_STROKE), 2.2f);
    const float tw = fo->measure("AIOHUD", titleSz);
    fo->draw_lc(dev, ix + tw + snap(14.0f), iy + snap(24.0f), "CONFIGURATION", snap(15.0f), fa(lerpc(C_ACCENT, C_ACCENTHI, pulse)), fa(C_STROKE), 1.2f);

    // close button (X), top-right -- eased red crossfade + a tiny size bump on hover
    const float cbS = snap(36.0f), cbX = ix + iw - cbS, cbY = iy + snap(2.0f);
    const bool cbHov = inrect(mo, cbX, cbY, cbS, cbS);
    const float ct = ease(1, cbHov ? 1.0f : 0.0f);
    halo(dev, cbX, cbY, cbS, cbS, C_CLOSEHOV, ct * 0.9f);
    vg(dev, cbX, cbY, cbS, cbS, lerpc(0x33FFFFFF, C_CLOSEHOV, ct), lerpc(0x14FFFFFF, 0xFFA0303A, ct));
    outline(dev, cbX, cbY, cbS, cbS, lerpc(C_BORDERHI, 0xFFE57078, ct));
    fo->begin(dev);
    fo->draw_c(dev, cbX + cbS * 0.5f, cbY + cbS * 0.5f, "X", snap(18.0f) + snap(2.0f) * ct, fa(C_TEXT), fa(C_STROKE), 1.3f + 0.3f * ct);
    if (cbHov && click) { open_ = false; if (mo) cursor(dev, mo->x, mo->y); return; }

    // accent divider under the header (animated wipe-in width + a soft glow seat)
    const float divY = iy + snap(50.0f);
    flat(dev, ix, divY, iw, 1, C_BORDER);
    flat(dev, ix, divY, iw * e, snap(2.0f), lerpc(C_ACCENT, C_ACCENTHI, pulse));

    // ===== TAB STRIP =====
    const float tabY = divY + snap(18.0f), tabH = snap(42.0f), tabW = snap(176.0f), tabGap = snap(6.0f);
    const float bodyY = tabY + tabH;
    float activeX = ix;
    for (int i = 0; i < NTABS; ++i) {
        const float tx = ix + i * (tabW + tabGap);
        const bool active = (i == tab_);
        const bool hover  = inrect(mo, tx, tabY, tabW, tabH);
        if (hover && click) { tab_ = i; if (i == 1) profDirty_ = true; nameFocus_ = false; }
        hov_[i] += (((hover ? 1.0f : 0.0f) - hov_[i]) * clampf(dt * 14.0f, 0.0f, 1.0f));   // eased hover
        if (active) activeX = tx;

        if (active) {
            shadow_down(dev, tx - snap(4.0f), tabY - snap(6.0f), tabW + snap(8.0f), snap(8.0f), (u32)(0x55000000)); // soft top glow seat
            vg(dev, tx, tabY, tabW, tabH + snap(2.0f), C_TABON_T, C_TABON_B);   // +2 : bleed into the body
        } else {
            vg(dev, tx, tabY, tabW, tabH, lerpc(C_TABOFF_T, C_TABHOV_T, hov_[i]), lerpc(C_TABOFF_B, C_TABHOV_B, hov_[i]));
        }
        outline(dev, tx, tabY, tabW, tabH, active ? 0x77FFFFFF : C_BORDER);
        flat(dev, tx, tabY, tabW, 1, lerpc(0x22FFFFFF, 0x66FFFFFF, active ? 1.0f : hov_[i]));   // top inner highlight
        fo->begin(dev);
        fo->draw_c(dev, tx + tabW * 0.5f, tabY + tabH * 0.5f, TABS[i], snap(15.0f),
                   lerpc(C_DIM, C_TEXT, active ? 1.0f : hov_[i]), fa(C_STROKE), 1.0f);
    }
    // sliding active-tab indicator (interpolates toward the active tab) + accent pulse
    if (tabSlide_ < 0.0f) tabSlide_ = activeX;
    tabSlide_ += (activeX - tabSlide_) * clampf(dt * 16.0f, 0.0f, 1.0f);
    flat(dev, tabSlide_, bodyY - snap(3.0f), tabW, snap(3.0f), lerpc(C_ACCENT, C_ACCENTHI, pulse));

    // ===== CONTENT BODY (the tab content surface) =====
    const float bodyH = pageBot - bodyY;
    shadow_down(dev, ix, bodyY, iw, snap(10.0f), (u32)(0x44000000));            // inner top shadow for depth
    vg(dev, ix, bodyY, iw, bodyH, C_CONTENT_T, C_CONTENT_B);
    outline(dev, ix, bodyY, iw, bodyH, C_BORDERHI);

    if (tab_ == 0) {
        // sidebar
        const float sbW = snap(220.0f);
        vg(dev, ix, bodyY, sbW, bodyH, C_SIDEBAR, 0xF0121A27);
        flat(dev, ix + sbW, bodyY, 1, bodyH, C_BORDER);
        fo->begin(dev);
        fo->draw_lc(dev, ix + snap(20.0f), bodyY + snap(24.0f), "SECTIONS", snap(12.0f), fa(C_MUTE), fa(C_STROKE), 0.8f);
        for (int i = 0; i < NSECT; ++i) {
            const float rx = ix + snap(10.0f), rw = sbW - snap(20.0f);
            const float ry = bodyY + snap(44.0f) + i * snap(42.0f), rh = snap(36.0f);
            const bool active = (i == section_);
            const bool hover  = inrect(mo, rx, ry, rw, rh);
            if (hover && click) section_ = i;
            const float ht = ease(10 + i, (hover || active) ? 1.0f : 0.0f);
            if (active) { vg(dev, rx, ry, rw, rh, C_ROWON_T, C_ROWON_B); }
            else if (ht > 0.01f) flat(dev, rx, ry, rw, rh, (0x22FFFFFF & 0x00FFFFFF) | ((u32)(0x22 * ht) << 24));
            flat(dev, rx, ry, snap(3.0f), rh * (active ? 1.0f : ht), lerpc(C_ACCENT, C_ACCENTHI, pulse));   // accent bar grows in
            fo->begin(dev);
            fo->draw_lc(dev, rx + snap(16.0f), ry + rh * 0.5f, SECTIONS[i], snap(15.0f),
                        lerpc(C_DIM, C_TEXT, active ? 1.0f : ht), fa(C_STROKE), 1.0f);
        }
        // content : live Party / Alliance controls (edit ui_config -> applied next frame)
        const float coX = ix + sbW + snap(30.0f), coY = bodyY + snap(26.0f);
        const float coW = (ix + iw) - coX - snap(26.0f);
        fo->begin(dev);
        fo->draw_lc(dev, coX, coY, SECTIONS[section_], snap(20.0f), fa(C_TEXT), fa(C_STROKE), 1.4f);
        flat(dev, coX, coY + snap(18.0f), snap(44.0f) * e, snap(2.0f), lerpc(C_ACCENT, C_ACCENTHI, pulse));

        // each control row eases in (fade + slight slide-up), staggered. g_fade is set per row, then
        // restored to `e` for the footer. ri = the running row index for the stagger delay.
        const float baseRy = coY + snap(48.0f);
        float ry = baseRy; int ri = 0;
        #define ROW_ENTER  float ap = stagger(anim_, ri++); g_fade = e * ap; float yo = (1.0f - ap) * snap(14.0f);

        // Box Theme -> window skin (big SQUARE live preview of the active skin)
        { ROW_ENTER
          if (int d = row_theme(dev, fo, mo, click, 20, coX, ry + yo, coW, "Box Theme", window_theme_name(ui_config().skinTheme), f.skin)) {
              ui_config().skinTheme = wrap(ui_config().skinTheme + d, window_theme_count()); save_ui_config(); } }
        ry += snap(THEME_ROW_H + 12.0f);
        // Font -> party/alliance text face
        { ROW_ENTER
          if (int d = row_selector(dev, fo, mo, click, 30, coX, ry + yo, coW, "Font", ui_font_label(ui_config().fontFace))) {
              ui_config().fontFace = wrap(ui_config().fontFace + d, ui_font_count()); save_ui_config(); } }
        ry += snap(52.0f);
        // Font Size -> party/alliance scale (slider, 50%..200%, 5% steps)
        { ROW_ENTER
            const float lo = 0.50f, hi = 2.00f;
            char szbuf[16]; sprintf(szbuf, "%d%%", (int)(ui_config().box[0].scale * 100.0f + 0.5f));
            float v01 = (ui_config().box[0].scale - lo) / (hi - lo);
            if (row_slider(dev, fo, mo, 1, coX, ry + yo, coW, "Font Size", szbuf, &v01)) {
                float v = lo + v01 * (hi - lo);
                v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f;          // snap to 5% steps
                v = v < lo ? lo : (v > hi ? hi : v);
                for (int b = 0; b < 3; ++b) ui_config().box[b].scale = v;   // scale all party/alliance boxes together
            }
        }
        ry += snap(52.0f);
        // Buff Size -> fraction of the player row (slider, 40%..100%, 5% steps ; independent of Font Size)
        { ROW_ENTER
            const float lo = 0.40f, hi = 1.00f;
            char bzbuf[16]; sprintf(bzbuf, "%d%%", (int)(ui_config().buffScale * 100.0f + 0.5f));
            float v01 = (ui_config().buffScale - lo) / (hi - lo);
            if (row_slider(dev, fo, mo, 2, coX, ry + yo, coW, "Buff Size", bzbuf, &v01)) {
                float v = lo + v01 * (hi - lo);
                v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f;          // snap to 5% steps
                ui_config().buffScale = v < lo ? lo : (v > hi ? hi : v);
            }
        }
        ry += snap(52.0f);
        // Borders : per-box window-skin chrome on/off (Party box, Cost MP box, Alliance 1, Alliance 2)
        { ROW_ENTER
            const float rowH = snap(40.0f), ty = ry + yo;
            flat(dev, coX, ty + rowH, coW, 1, 0x14FFFFFF);
            fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, "Borders", snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
            const char* blbl[4] = { "Party", "Cost", "Ally 1", "Ally 2" };
            bool* bval[4] = { &ui_config().border[0], &ui_config().borderCost, &ui_config().border[1], &ui_config().border[2] };
            const float bbw = snap(66.0f), bgap = snap(8.0f), bbh = snap(28.0f);
            const float bx0 = coX + coW - (4 * bbw + 3 * bgap), bty = ty + (rowH - bbh) * 0.5f;
            for (int i = 0; i < 4; ++i) {
                const float bx = bx0 + i * (bbw + bgap);
                if (toggle_chip(dev, fo, mo, click, 50 + i * 2, bx, bty, bbw, bbh, blbl[i], *bval[i])) { *bval[i] = !*bval[i]; save_ui_config(); }
            }
        }
        ry += snap(56.0f);
        // Buttons : Edit Layout (enter drag/resize) + Default (reset EVERYTHING)
        { ROW_ENTER
            const float bh = snap(34.0f), bw = snap(150.0f), gap = snap(10.0f), ty = ry + yo;
            const float defX = coX + coW - bw, edX = defX - gap - bw;
            fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + bh * 0.5f, "Layout", snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
            if (push_btn(dev, fo, mo, click, 60, edX, ty, bw, bh, "Edit Layout", 0)) ui_config().editLayout = true;
            if (push_btn(dev, fo, mo, click, 61, defX, ty, bw, bh, "Default (all)", 1)) reset_ui_config();
        }
        #undef ROW_ENTER
        g_fade = e;   // restore for the footer
    } else if (tab_ == 1) {
        // ===== PROFILE tab : a "create / edit" card + a list of saved profiles (Load / Delete) =====
        if (profDirty_) { profile_refresh(); profDirty_ = false; }
        const float pX = ix + snap(40.0f), pW = iw - snap(80.0f);
        float py = bodyY + snap(28.0f);
        fo->begin(dev);
        fo->draw_lc(dev, pX, py, "Profiles", snap(22.0f), fa(C_TEXT), fa(C_STROKE), 1.4f);
        flat(dev, pX, py + snap(20.0f), snap(48.0f) * e, snap(2.0f), lerpc(C_ACCENT, C_ACCENTHI, pulse));
        fo->begin(dev);
        fo->draw_lc(dev, pX, py + snap(42.0f), "Snapshot every Party / Alliance setting under a name -- Load to switch instantly.",
                    snap(13.0f), fa(C_DIM), fa(C_STROKE), 1.0f);

        // --- CREATE / EDIT card ---
        py += snap(64.0f);
        const float cardH = snap(86.0f);
        vg(dev, pX, py, pW, cardH, 0x40101826, 0x400A111C);                // card surface
        outline(dev, pX, py, pW, cardH, C_BORDER);
        flat(dev, pX, py, snap(3.0f), cardH, lerpc(C_ACCENT, C_ACCENTHI, pulse));   // accent edge
        fo->begin(dev);
        fo->draw_lc(dev, pX + snap(18.0f), py + snap(18.0f), "NEW  /  EDIT", snap(11.0f), fa(C_MUTE), fa(C_STROKE), 0.8f);

        const float inX = pX + snap(18.0f), inY = py + snap(34.0f);
        const float fldH = snap(38.0f), btnW = snap(130.0f), fGap = snap(12.0f);
        const float fldW = pW - snap(36.0f) - btnW - fGap;
        const bool fldHov = inrect(mo, inX, inY, fldW, fldH);
        if (click) nameFocus_ = fldHov;                                   // click in = focus, click out = blur
        bool commit = kbCommit_; kbCommit_ = false;                       // Enter (from the keyboard hook) commits
        const float ft = ease(700, nameFocus_ ? 1.0f : 0.0f);
        halo(dev, inX, inY, fldW, fldH, C_ACCENT, ft * 0.7f);
        vg(dev, inX, inY, fldW, fldH, 0x55101620, 0x550A0F16);
        outline(dev, inX, inY, fldW, fldH, lerpc(C_BORDER, C_ACCENT, ft));
        flat(dev, inX, inY, snap(2.0f), fldH, lerpc(0x00000000, C_ACCENT, ft));   // focus caret-rail
        fo->begin(dev);
        const float txY = inY + fldH * 0.5f, txX = inX + snap(13.0f);
        if (nameLen_ > 0) fo->draw_lc(dev, txX, txY, nameBuf_, snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
        else              fo->draw_lc(dev, txX, txY, "Type a profile name...", snap(14.0f), fa(C_MUTE), fa(C_STROKE), 1.0f);
        if (nameFocus_ && (sinf(f.t * 5.0f) > 0.0f)) {                     // blinking caret
            const float cxp = txX + (nameLen_ > 0 ? fo->measure(nameBuf_, snap(15.0f)) : 0.0f) + snap(2.0f);
            flat(dev, cxp, inY + snap(9.0f), snap(2.0f), fldH - snap(18.0f), C_ACCENTHI);
        }
        const bool canSave = nameLen_ > 0;
        const bool exists = canSave && profile_exists(nameBuf_);
        // Editing a LOADED profile (activeProf_) and the name changed -> Save RENAMES it (update in place,
        // delete the old file) instead of spawning a duplicate.
        const bool renaming = canSave && activeProf_[0] && strcmp(nameBuf_, activeProf_) != 0;
        const float saveX = inX + fldW + fGap;
        {   // Save : rename the loaded profile, else create / overwrite the typed name
            const bool hov = canSave && inrect(mo, saveX, inY, btnW, fldH);
            const float t = ease(701, hov ? 1.0f : 0.0f);
            halo(dev, saveX, inY, btnW, fldH, C_ACCENT, t * 0.8f);
            vg(dev, saveX, inY, btnW, fldH, canSave ? lerpc(0xFF2A3548, 0xFF3A82E0, t) : 0xFF1E2630,
                                            canSave ? lerpc(0xFF1D2738, 0xFF2A61B6, t) : 0xFF141A22);
            outline(dev, saveX, inY, btnW, fldH, lerpc(C_BORDERHI, C_ACCENTHI, t));
            const char* lbl = renaming ? "Rename" : (exists ? "Overwrite" : "Save");
            fo->begin(dev); fo->draw_c(dev, saveX + btnW * 0.5f, txY, lbl, snap(14.0f), fa(canSave ? C_TEXT : C_MUTE), fa(C_STROKE), 1.0f);
            if (canSave && ((hov && click) || commit)) {
                profile_save(nameBuf_);                                      // write under the (new) name
                if (renaming) profile_delete(activeProf_);                   // editing a loaded profile + name changed -> drop the old file
                strncpy(activeProf_, nameBuf_, sizeof(activeProf_) - 1); activeProf_[sizeof(activeProf_) - 1] = 0;
                profDirty_ = true;
            }
        }

        // --- saved profiles list ---
        py += cardH + snap(22.0f);
        fo->begin(dev);
        char hdr[48]; sprintf(hdr, "SAVED  (%d)", profile_count());
        fo->draw_lc(dev, pX, py, hdr, snap(12.0f), fa(C_MUTE), fa(C_STROKE), 0.8f);
        py += snap(22.0f);
        const float rowH = snap(46.0f), rGap = snap(8.0f);
        const int nprof = profile_count();
        if (nprof == 0) {
            vg(dev, pX, py, pW, snap(56.0f), 0x2A101826, 0x2A0A111C);
            outline(dev, pX, py, pW, snap(56.0f), C_BORDER);
            fo->begin(dev);
            fo->draw_c(dev, pX + pW * 0.5f, py + snap(28.0f), "No profiles yet -- type a name above and Save.", snap(14.0f), fa(C_DIM), fa(C_STROKE), 1.0f);
        }
        const int maxRows = (int)((pageBot - py) / (rowH + rGap));
        for (int i = 0; i < nprof && i < maxRows; ++i) {
            const char* nm = profile_name(i);
            const bool active = activeProf_[0] && strcmp(nm, activeProf_) == 0;
            const float ry = py + i * (rowH + rGap);
            const float ap = stagger(anim_, i); g_fade = e * ap;          // staggered list entrance
            const float yo = (1.0f - ap) * snap(12.0f), rry = ry + yo;
            const bool rowHov = inrect(mo, pX, rry, pW, rowH);
            const float rt = ease(720 + i, rowHov ? 1.0f : 0.0f);
            vg(dev, pX, rry, pW, rowH, lerpc(active ? 0x402C6AC4 : 0x22141C28, active ? 0x553A82E0 : 0x33223A5C, rt),
                                       lerpc(active ? 0x40234E92 : 0x220E141E, active ? 0x552A61B6 : 0x331A2A44, rt));
            outline(dev, pX, rry, pW, rowH, active ? C_ACCENT : lerpc(C_BORDER, C_BORDERHI, rt));
            flat(dev, pX, rry, snap(3.0f), rowH, lerpc(C_ACCENT, C_ACCENTHI, active ? 1.0f : pulse));
            fo->begin(dev);
            fo->draw_lc(dev, pX + snap(18.0f), rry + rowH * 0.5f, nm, snap(16.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
            // Load / Delete buttons (right-aligned) + an ACTIVE pill
            const float lbw = snap(80.0f), dbw = snap(80.0f), bgap = snap(8.0f), bH = snap(30.0f);
            const float dX = pX + pW - dbw - snap(10.0f), lX = dX - bgap - lbw, bY = rry + (rowH - bH) * 0.5f;
            if (active) {
                const float pillW = snap(58.0f), pillX = lX - snap(12.0f) - pillW, pillH = snap(20.0f), pillY = rry + (rowH - pillH) * 0.5f;
                vg(dev, pillX, pillY, pillW, pillH, 0xFF2E8C49, 0xFF206030);
                outline(dev, pillX, pillY, pillW, pillH, 0xFF49C46A);
                fo->begin(dev); fo->draw_c(dev, pillX + pillW * 0.5f, pillY + pillH * 0.5f, "ACTIVE", snap(10.0f), fa(C_TEXT), fa(C_STROKE), 0.8f);
            }
            if (push_btn(dev, fo, mo, click, 740 + i * 2, lX, bY, lbw, bH, "Load", 0)) {
                profile_load(nm);
                strncpy(nameBuf_, nm, sizeof(nameBuf_) - 1); nameBuf_[sizeof(nameBuf_) - 1] = 0; nameLen_ = (int)strlen(nameBuf_); nameFocus_ = false;
                strncpy(activeProf_, nm, sizeof(activeProf_) - 1); activeProf_[sizeof(activeProf_) - 1] = 0;
            }
            if (push_btn(dev, fo, mo, click, 741 + i * 2, dX, bY, dbw, bH, "Delete", 1)) {
                if (active) activeProf_[0] = 0;
                profile_delete(nm); profDirty_ = true;
            }
        }
        if (nprof > maxRows) {
            g_fade = e; fo->begin(dev);
            char more[48]; sprintf(more, "+%d more (delete some to see them)", nprof - maxRows);
            fo->draw_lc(dev, pX, py + maxRows * (rowH + rGap) + snap(14.0f), more, snap(12.0f), fa(C_MUTE), fa(C_STROKE), 1.0f);
        }
        g_fade = e;
    } else {
        fo->begin(dev);
        fo->draw_lc(dev, ix + snap(26.0f), bodyY + snap(32.0f), "Help  (coming soon)", snap(16.0f), fa(C_DIM), fa(C_STROKE), 1.0f);
    }

    // footer + cursor on top
    fo->begin(dev);
    fo->draw_lc(dev, ix, pageBot + snap(14.0f), "click X  or  //aio config  to close", snap(12.0f), fa(C_MUTE), fa(C_STROKE), 1.0f);
    if (mo && mo->focused) cursor(dev, mo->x, mo->y);   // hide our cursor when the game isn't the OS foreground
}

} // namespace aio
