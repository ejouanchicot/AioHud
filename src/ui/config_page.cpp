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
#include "model/gamestate.h"   // GameState::me (character name) for the Profile page
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
// Settings modules (the Configuration sidebar). Add a module here = a new settings page ; the profile
// is GLOBAL (a profile snapshots every module), so it lives in the profile bar, not per-module.
static const char* MODULES[]  = { "Party / Alliance" };
static const int   MODULE_N   = (int)(sizeof(MODULES) / sizeof(MODULES[0]));

// ---- palette (ARGB) ----
static const u32 C_DIMBG    = 0xCC04070D;
static const u32 C_TABON_T  = 0xFF3E88E8, C_TABON_B  = 0xFF2A60B4;
static const u32 C_TABOFF_T = 0xC0202B3C, C_TABOFF_B = 0xC0151E2C;
static const u32 C_TABHOV_T = 0xD0364865, C_TABHOV_B = 0xD0202E44;
static const u32 C_CONTENT_T= 0xE6141C28, C_CONTENT_B= 0xE60E141E;
static const u32 C_SIDEBAR  = 0xF0161F2D;
static const u32 C_ROWON_T  = 0xFF2C6AC4, C_ROWON_B  = 0xFF234E92;
static const u32 C_BORDER   = 0x33FFFFFF, C_BORDERHI = 0x66FFFFFF;
static const u32 C_TEXT     = 0xFFEAF1FB, C_DIM = 0xFFB4C2D8, C_MUTE = 0xFF8A9AB4;
static const u32 C_ACCENT   = 0xFF5AA2FF, C_ACCENTHI = 0xFFBFE0FF, C_STROKE = 0xFF000000, C_CLOSEHOV = 0xFFCE424C;
// FFXI gold (titles / glint / active indicators / unsaved-Save). Interactive accents stay blue.
static const u32 C_GOLD     = 0xFFFFDC78, C_GOLDHI = 0xFFFFF3C8, C_GOLD_DEEP = 0xFFD8A94A;
// control surfaces : a cohesive blue "glass" (NOT translucent white, which reads grey on navy).
static const u32 C_CTL_T    = 0x6E2A4368, C_CTL_B = 0x5E16283F;        // idle button/field fill (navy-blue glass)
static const u32 C_CTL_BR   = 0x88486F9E;                              // idle control border (soft steel blue)
static const u32 C_ARROW    = 0xFF9FC4F2;                              // chevron / stepper glyph idle (bright steel blue)
// preview gauges (party brief : HP green / MP blue / TP magenta)
static const u32 C_HP = 0xFF5ADC5A, C_HP_D = 0xFF148C2D, C_MP = 0xFF9597FF, C_MP_D = 0xFF3A3CE0, C_TP = 0xFFCD6EFF, C_TP_D = 0xFF5A0FBE;

void ConfigPage::set_tab(int t)     { if (t >= 0 && t < NTABS) tab_ = t; }
void ConfigPage::set_section(int s) { if (s >= 0) section_ = s; }   // (sections folded into the profile sidebar)

// ---- a global fade factor (open animation), applied to every quad + text colour ----
static float g_fade = 1.0f;
static float g_dt   = 0.016f;   // current frame delta (seconds) -- drives ease()
static float g_t    = 0.0f;     // current (wrapping) seconds -- drives the hover shine sweep
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
// ADDITIVE colour state (SRCALPHA, ONE) -> light ACCUMULATES instead of replacing : real luminous
// glow / bloom / shine, the way a neon HUD reads. Any following q4/flat/vg calls cs() -> resets to
// normal alpha, so additive only affects the glow draws issued right after this.
static void cs_add(u32 dev) {
    dSetVS(dev, FVF_XYZRHW_DIFFUSE);
    dSetRS(dev, D3DRS_ALPHABLENDENABLE, 1);
    dSetRS(dev, D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_ONE);
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
// luminous accent glow BEHIND an element (draw before it). ADDITIVE 3-layer bloom : a wide soft base,
// a mid ring and a tight bright core -> the light builds up and reads as real neon glow, not a flat card.
static void halo(u32 dev, float x, float y, float w, float h, u32 col, float t) {
    if (t <= 0.01f) return;
    t = clampf(t, 0.0f, 1.0f) * g_fade;
    const u32 rgb = col & 0x00FFFFFF;
    const float cx = x + w * 0.5f, cy = y + h * 0.5f, hw = w * 0.5f, hh = h * 0.5f;
    cs_add(dev);
    soft_blob(dev, cx, cy, hw + snap(22.0f), hh + snap(22.0f), rgb | ((u32)(38.0f * t) << 24));   // wide soft base
    soft_blob(dev, cx, cy, hw + snap(12.0f), hh + snap(12.0f), rgb | ((u32)(58.0f * t) << 24));   // mid bloom
    soft_blob(dev, cx, cy, hw + snap(5.0f),  hh + snap(5.0f),  rgb | ((u32)(72.0f * t) << 24));   // bright core
}
// a moving glass "shine" streak that sweeps across an element while hovered (additive, clipped to the
// rect by clamping the band). amt = hover strength (0..1) ; tsec = wrapping seconds for the motion.
static void shine(u32 dev, float x, float y, float w, float h, float amt, float tsec) {
    if (amt <= 0.01f || w <= 1.0f) return;
    float ph = tsec * 0.55f; ph -= floorf(ph);                 // 0..1 loop (~1.8s)
    const float mid = x - w * 0.35f + (w * 1.7f) * ph;         // streak centre travels left->right
    const float sw = w * 0.16f;
    float x0 = mid - sw, x1 = mid + sw;
    if (x0 < x) x0 = x; if (x1 > x + w) x1 = x + w;
    if (x1 - x0 <= 1.0f) return;
    const u32 a = (u32)(46.0f * clampf(amt, 0.0f, 1.0f) * g_fade);
    const u32 cMid = (a << 24) | 0x00FFFFFF, cEdge = 0x00FFFFFF;
    cs_add(dev);
    if (mid > x0 && mid < x1) {
        grad_quad(dev, x0, y, mid - x0, h, cEdge, cMid, cEdge, cMid);
        grad_quad(dev, mid, y, x1 - mid, h, cMid, cEdge, cMid, cEdge);
    } else {
        grad_quad(dev, x0, y, x1 - x0, h, cMid, cMid, cMid, cMid);
    }
}
static inline bool inrect(const MouseState* m, float x, float y, float w, float h) {
    return m && m->x >= x && m->x < x + w && m->y >= y && m->y < y + h;
}
static void cursor(u32 dev, float x, float y) {
    for (int i = 0; i <= 17; ++i) flat(dev, x - 1.0f, y - 1.0f + (float)i, (float)i * 0.70f + 4.0f, 2.0f, 0xFF0A0D12);
    for (int i = 0; i <= 15; ++i) flat(dev, x + 1.0f, y + 1.0f + (float)i, (float)i * 0.66f + 1.0f, 2.0f, 0xFFFFFFFF);
}
// a crisp caret (filled triangle) -> sharp stepper arrows. dir < 0 points left, dir > 0 points right.
static void chevron(u32 dev, float cx, float cy, float s, int dir, u32 col) {
    cs(dev);
    const float hw = s * 0.30f, hh = s * 0.42f;
    const u32 c = fa(col);
    if (dir < 0) fill_tri(dev, cx + hw, cy - hh, cx + hw, cy + hh, cx - hw, cy, c);
    else         fill_tri(dev, cx - hw, cy - hh, cx - hw, cy + hh, cx + hw, cy, c);
}
// a settings-row background band : faint zebra fill that ties the left label to the far-right control,
// brightening a touch on hover. Kills the "label and control floating in a void" problem.
static void row_band(u32 dev, float x, float y, float w, float h, bool alt, float hov) {
    const int a = (alt ? 0x0E : 0x05) + (int)(0x14 * clampf(hov, 0.0f, 1.0f) + 0.5f);
    flat(dev, x, y, w, h, ((u32)a << 24) | 0x00FFFFFF);
}

// ---- modern primitives : rounded rects (rect bands + quarter-disc corners) + soft drop shadows. ----
// A filled QUARTER disc (triangle fan) confined to ONE corner square -> it NEVER overlaps the bands,
// so the whole rounded rect composites with a SINGLE blend per pixel (correct for translucent fills,
// no double-blend "pinwheel" artefact in the corners).
static void qfan(u32 dev, float cx, float cy, float r, float a0, float a1, u32 col) {
    cs(dev);
    const int N = 6;
    const u32 c = fa(col);
    float px = cx + r * cosf(a0), py = cy + r * sinf(a0);
    for (int i = 1; i <= N; ++i) {
        const float a = a0 + (a1 - a0) * (float)i / (float)N;
        const float nx = cx + r * cosf(a), ny = cy + r * sinf(a);
        fill_tri(dev, cx, cy, px, py, nx, ny, c);
        px = nx; py = ny;
    }
}
static const float PI_ = 3.14159265f;
static void rrect_fill(u32 dev, float x, float y, float w, float h, float r, u32 top, u32 bot) {
    if (w <= 0 || h <= 0) return;
    if (r > w * 0.5f) r = w * 0.5f; if (r > h * 0.5f) r = h * 0.5f;
    if (r < 1.0f) { vg(dev, x, y, w, h, top, bot); return; }
    const u32 cT = lerpc(top, bot, r / h), cB = lerpc(top, bot, (h - r) / h);
    vg(dev, x + r,     y,         w - 2 * r, r,         top, cT);   // top band
    vg(dev, x + r,     y + h - r, w - 2 * r, r,         cB,  bot);  // bottom band
    vg(dev, x,         y + r,     r,         h - 2 * r, cT,  cB);   // left band
    vg(dev, x + w - r, y + r,     r,         h - 2 * r, cT,  cB);   // right band
    vg(dev, x + r,     y + r,     w - 2 * r, h - 2 * r, cT,  cB);   // center
    qfan(dev, x + r,     y + r,     r, PI_,        1.5f * PI_, top);   // TL
    qfan(dev, x + w - r, y + r,     r, 1.5f * PI_, 2.0f * PI_, top);   // TR
    qfan(dev, x + r,     y + h - r, r, 0.5f * PI_, PI_,        bot);   // BL
    qfan(dev, x + w - r, y + h - r, r, 0.0f,       0.5f * PI_, bot);   // BR
}
// round the TOP corners only (tabs : the bottom melts into the body).
static void rrect_top(u32 dev, float x, float y, float w, float h, float r, u32 top, u32 bot) {
    if (w <= 0 || h <= 0) return;
    if (r > w * 0.5f) r = w * 0.5f; if (r > h) r = h;
    if (r < 1.0f) { vg(dev, x, y, w, h, top, bot); return; }
    const u32 cT = lerpc(top, bot, r / h);
    vg(dev, x + r,     y,     w - 2 * r, r,     top, cT);    // top band
    vg(dev, x,         y + r, r,         h - r, cT,  bot);   // left band (down to bottom -> square corner)
    vg(dev, x + w - r, y + r, r,         h - r, cT,  bot);   // right band
    vg(dev, x + r,     y + r, w - 2 * r, h - r, cT,  bot);   // center
    qfan(dev, x + r,     y + r, r, PI_,        1.5f * PI_, top);   // TL
    qfan(dev, x + w - r, y + r, r, 1.5f * PI_, 2.0f * PI_, top);   // TR
}
// a bordered rounded panel : border ring + inner gradient fill (opaque-friendly).
static void rpanel(u32 dev, float x, float y, float w, float h, float r, u32 top, u32 bot, u32 border, float bt) {
    rrect_fill(dev, x, y, w, h, r, border, border);
    rrect_fill(dev, x + bt, y + bt, w - 2 * bt, h - 2 * bt, (r - bt > 0.0f ? r - bt : 0.0f), top, bot);
}
// a soft, feathered drop shadow under an element (draw BEFORE it) -> floats the card off the page.
static void drop_shadow(u32 dev, float x, float y, float w, float h, float spread, u32 alpha) {
    cs(dev);
    soft_blob(dev, x + w * 0.5f, y + h * 0.5f + snap(5.0f), w * 0.5f + spread, h * 0.5f + spread, (alpha << 24));
}
// a tiny rounded status pill (ACTIVE / DEFAULT / a character name). Returns its width so they stack.
static float badge(u32 dev, Font* fo, float x, float cy, const char* text, u32 fill, u32 brd, u32 txt) {
    const float sz = snap(10.0f), padx = snap(9.0f), h = snap(18.0f);
    const float w = fo->measure(text, sz) + 2.0f * padx, y = cy - h * 0.5f;
    rpanel(dev, x, y, w, h, h * 0.5f, fill, fill, brd, snap(1.0f));
    fo->begin(dev); fo->draw_c(dev, x + w * 0.5f, cy, text, sz, fa(txt), fa(C_STROKE), 0.8f);
    return w;
}

// a small square [<] / [>] stepper button with eased hover. uid = its animation slot.
static bool arrow_btn(u32 dev, Font* fo, const MouseState* mo, bool click, int uid,
                      float x, float y, float s, const char* glyph) {
    (void)fo;
    const bool hov = inrect(mo, x, y, s, s);
    const float t = ease(uid, hov ? 1.0f : 0.0f);
    halo(dev, x, y, s, s, C_ACCENT, t * 0.7f);
    const float r = snap(s * 0.30f);
    rpanel(dev, x, y, s, s, r, lerpc(C_CTL_T, 0x884E8FE0, t), lerpc(C_CTL_B, 0x6E2E63B4, t), lerpc(C_CTL_BR, C_ACCENTHI, t), snap(1.5f));
    const int dir = (glyph[0] == '<') ? -1 : +1;
    chevron(dev, x + s * 0.5f, y + s * 0.5f, s * (0.62f + 0.06f * t), dir, lerpc(C_ARROW, C_GOLDHI, t));
    return hov && click;
}

// A labeled selector row :  "Label                 [<]   value   [>]".  uid = animation base.
static int row_selector(u32 dev, Font* fo, const MouseState* mo, bool click, int uid,
                        float x, float y, float w, const char* label, const char* value) {
    const float rowH = snap(40.0f);
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
    const float tr = trkH * 0.5f;
    rrect_fill(dev, trkX, trkY, trkW, trkH, tr, 0x44101620, 0x44080C12);       // rounded groove
    if (fillW >= trkH) rrect_fill(dev, trkX, trkY, fillW, trkH, tr, C_ACCENTHI, C_ACCENT);   // rounded fill

    // round knob : eases bigger on hover / while dragging, with a soft halo + rim.
    const float kt = ease(40 + id, (hot || act) ? 1.0f : 0.0f);
    const float kr = knobR * (1.0f + 0.35f * kt);
    const float kx = trkX + fillW;
    halo(dev, kx - kr, cy - kr, 2 * kr, 2 * kr, C_ACCENT, 0.4f + kt);
    cs(dev);
    disc(dev, kx, cy, kr + snap(1.5f), fa(C_BORDERHI));                        // rim
    disc(dev, kx, cy, kr, fa(lerpc(0xFFCFE0F5, 0xFFFFFFFF, kt)));              // body
    disc(dev, kx, cy - kr * 0.30f, kr * 0.55f, fa(0x66FFFFFF));               // top gloss
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
    rpanel(dev, x, y, w, h, snap(h * 0.44f), t, b, lerpc(C_BORDER, C_BORDERHI, ht), snap(1.5f));
    // a little round state dot, left of the label : green when on, dim red when off
    const float dr = snap(4.0f), dx = x + snap(11.0f), dy = y + h * 0.5f;
    cs(dev); disc(dev, dx, dy, dr, fa(lerpc(0xFF7E3A42, 0xFF8CF2A8, st)));
    fo->begin(dev); fo->draw_c(dev, x + w * 0.5f + snap(6.0f), dy, label, snap(12.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
    return hov && click;
}

// a wide push button (Edit Layout / Default) with eased hover + accent halo. tone : 0 = neutral blue,
// 1 = danger red. uid = animation slot. Returns true on click.
static bool push_btn(u32 dev, Font* fo, const MouseState* mo, bool click, int uid,
                     float x, float y, float w, float h, const char* label, int tone) {
    const bool hov = inrect(mo, x, y, w, h);
    const bool press = hov && mo && mo->down;
    const float t = ease(uid, hov ? 1.0f : 0.0f);
    const u32 idleT = tone ? 0xFF3A2A2E : 0xFF2A3548, idleB = tone ? 0xFF281D20 : 0xFF1D2738;
    const u32 hovT  = tone ? 0xFFB85050 : 0xFF3A82E0, hovB  = tone ? 0xFF8A3A3A : 0xFF2A61B6;
    const float pin = press ? snap(2.0f) : 0.0f;                     // press : nudge inward
    const float bx = x + pin, by = y + pin, bw = w - 2 * pin, bh = h - 2 * pin;
    const float r = snap(h * 0.24f);
    drop_shadow(dev, bx, by, bw, bh, snap(4.0f), press ? 36 : 64);
    halo(dev, bx, by, bw, bh, tone ? 0xFFE06868 : C_ACCENT, t * 0.8f);
    rpanel(dev, bx, by, bw, bh, r, lerpc(idleT, hovT, t), lerpc(idleB, hovB, t), lerpc(C_BORDERHI, tone ? 0xFFE57078 : C_ACCENTHI, t), snap(1.5f));
    rrect_top(dev, bx + snap(2.0f), by + snap(2.0f), bw - snap(4.0f), bh * 0.42f, snap(r * 0.6f), 0x33FFFFFF, 0x05FFFFFF);   // top sheen
    shine(dev, bx + snap(2.0f), by + snap(2.0f), bw - snap(4.0f), bh - snap(4.0f), t, g_t);   // glass sweep on hover
    fo->begin(dev); fo->draw_c(dev, x + w * 0.5f, y + h * 0.5f, label, snap(13.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
    return hov && click;
}

// ---- Help content : the Party / Alliance module reference. kind 0 = heading, 1 = paragraph, 2 = bullet. ----
struct HelpItem { int kind; const char* text; };
static const HelpItem HELP_PA[] = {
    {0, "Party & Alliance"},
    {1, "AioHUD replaces the default party and alliance windows with up to three boxes -- your party plus two alliance parties. Each box lists its members with live HP, MP and TP, status icons and role-coloured names."},
    {0, "Reading a member"},
    {1, "The name and main / sub job sit on the left, tinted by role (tank, healer, DD, support). Three gauges show HP, MP and TP. Coloured dots flag the alliance leader (white), the party leader (yellow) and the quartermaster (green), with the member's distance in yalms shown just below. The icons to the left of a row are their active buffs (up to 20)."},
    {1, "Your current target is marked by a highlight that slides onto its row, and the name zooms in slightly; a sub-target (<st>) adds a second, blue marker. While a member is casting, the spell's name appears in gold under their name. Members out of casting range are dimmed, and a member in another zone shows the zone name instead of their vitals."},
    {0, "Adaptive layout"},
    {1, "The party box is bottom-anchored: it grows upward as members join and shrinks back when they leave, so its lower-right corner stays exactly where you placed it. A solo party gets a little extra height, and the box extends up slightly to cover the game's native party block."},
    {0, "Cost / Next box"},
    {1, "When you open a spell, ability or weapon-skill menu, a small box appears above the party showing the action's name with its MP cost (spells), its recast 'Next' timer (spells and abilities) or your live TP (weapon skills)."},
    {0, "Appearance"},
    {1, "On the Configuration tab you can change the window skin (Box Theme), the text Font and Font Size, and the Buff icon Size. Borders can be switched off per box -- Party, the Cost box, Alliance 1 and Alliance 2 -- which keeps the background but hides the frame."},
    {0, "Move & resize"},
    {1, "Click Edit Layout (or type //aio edit) to arrange the boxes directly on screen: drag a box to move it, and roll the mouse wheel over it to resize. Boxes snap to each other's edges. 'Set Ref' locks the party's reference line so lowering the box only makes it taller. 'Default' resets positions and sizes; click Done to save and exit."},
    {0, "Preview (Demo)"},
    {1, "Use //aio party demo [N] to fill the party with N fake members (1-6), or //aio alliance1 demo / alliance2 demo to add alliance parties. //aio demo off returns to live data. Demo mirrors the real layout, so you can tune spacing and placement without a live party."},
    {0, "Profiles"},
    {1, "The Profile tab saves every Party / Alliance setting under a name. Load switches instantly; editing a loaded profile's name and pressing Save renames it in place. The same actions exist as //aio profile save | load | delete | list <name>."},
    {0, "Commands"},
    {2, "//aio config  --  open this window"},
    {2, "//aio edit  --  move & resize the boxes"},
    {2, "//aio party demo [N]  --  preview the layout"},
    {2, "//aio profile save | load | delete <name>"},
};
static const int HELP_PA_N = (int)(sizeof(HELP_PA) / sizeof(HELP_PA[0]));

// One help page per module (the Help tab's left menu lists these ; add a module = add a row here).
struct HelpModule { const char* name; const HelpItem* items; int count; };
static const HelpModule HELP_MODULES[] = {
    { "Party / Alliance", HELP_PA, HELP_PA_N },
};
static const int HELP_MODULE_N = (int)(sizeof(HELP_MODULES) / sizeof(HELP_MODULES[0]));

// Draw word-wrapped text from (x,y), returning the new y. Lines whose center is outside [top,bot] are
// skipped -- a cheap clip for the scrolling viewport (D3D8 has no scissor rect).
static float draw_wrapped(u32 dev, Font* fo, float x, float y, float maxW, float top, float bot,
                          const char* text, float sz, u32 col, float lineH) {
    char cur[512]; int cl = 0; char word[160]; const char* p = text;
    for (;;) {
        while (*p == ' ') ++p;
        int wl = 0; while (*p && *p != ' ' && wl < 159) word[wl++] = *p++;
        word[wl] = 0;
        if (wl == 0) break;
        char trial[640];
        if (cl == 0) { strncpy(trial, word, sizeof(trial) - 1); trial[sizeof(trial) - 1] = 0; }
        else { _snprintf(trial, sizeof(trial), "%s %s", cur, word); trial[sizeof(trial) - 1] = 0; }
        if (cl == 0 || fo->measure(trial, sz) <= maxW) { strncpy(cur, trial, sizeof(cur) - 1); cur[sizeof(cur) - 1] = 0; cl = (int)strlen(cur); }
        else {
            if (y + lineH > top && y < bot) { fo->begin(dev); fo->draw_lc(dev, x, y + lineH * 0.5f, cur, sz, fa(col), fa(C_STROKE), 1.0f); }
            y += lineH; strncpy(cur, word, sizeof(cur) - 1); cur[sizeof(cur) - 1] = 0; cl = wl;
        }
    }
    if (cl > 0) { if (y + lineH > top && y < bot) { fo->begin(dev); fo->draw_lc(dev, x, y + lineH * 0.5f, cur, sz, fa(col), fa(C_STROKE), 1.0f); } y += lineH; }
    return y;
}

void ConfigPage::draw(const Frame& f, float sw, float sh) {
    pvOn_ = false;   // live-preview anchor : off unless we reach the Configuration tab below
    if (!open_) return;
    u32 dev = f.dev;
    // the whole config interface is drawn in Verdana (get-or-build its atlas ; fall back to default)
    Font* fo = (f.fonts ? f.fonts->get("Verdana", 600) : f.font);
    if (fo) fo->ensure(dev);
    if (!fo || !fo->ready()) fo = f.font;
    if (!fo || !fo->ready() || sw <= 0 || sh <= 0) return;
    const MouseState* mo = f.mouse;
    const bool click = mo && mo->clicked;

    // --- frame clock (shared by both modes) : drives the open fade + every ease() spring ---
    float dt = (lastT_ < 0.0f) ? 0.016f : (f.t - lastT_);
    if (dt < 0.0f || dt > 0.25f) dt = 0.016f;
    lastT_ = f.t;
    g_dt = dt;
    g_t  = f.t;

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

    // ===== BACK LAYER : dim the game, then a self-made MODERN gradient page (no game skin) =====
    flat(dev, 0, 0, sw, sh, C_DIMBG);                                  // darken the game behind us
    vg(dev, 0, 0, sw, sh, 0xF01B2740, 0xF0080B14);                     // deep slate-blue -> near-black
    vg(dev, 0, 0, sw, snap(180.0f), 0x2A2E6AB0, 0x002E6AB0);           // soft blue glow falling from the top
    // two large magical glows drifting slowly across the page (gold + indigo) -> living, LUMINOUS background (additive)
    cs_add(dev);
    {
        const float gx1 = sw * (0.28f + 0.17f * sinf(f.t * 0.17f));
        const float gx2 = sw * (0.74f + 0.15f * sinf(f.t * 0.12f + 2.1f));
        const u32 ag = (u32)((9.0f + 5.0f * pulse)) << 24;
        soft_blob(dev, gx1, sh * 0.04f, sw * 0.36f, sh * 0.34f, ag | 0x00FFDC78);   // gold
        soft_blob(dev, gx2, sh * 0.10f, sw * 0.34f, sh * 0.30f, 0x0E2E6AB0);        // indigo
        soft_blob(dev, sw * 0.5f, sh * 1.02f, sw * 0.6f, sh * 0.28f, 0x10244B8C);   // bottom lift
    }
    vg(dev, 0, sh - snap(120.0f), sw, snap(120.0f), 0x00000000, 0x55000000);   // bottom vignette
    flat(dev, 0, 0, sw, snap(2.0f), lerpc(C_GOLD, C_GOLDHI, pulse));           // top GOLD hairline (FFXI glint)
    flat(dev, 0, 0, sw, 1, 0x40FFFFFF);                               // crisp top inner highlight
    outline(dev, 0, 0, sw, sh, C_BORDERHI);

    // content inset from the skin border (no second frame -- we draw straight on the skin)
    const float m = snap(30.0f);
    const float ix = m, iy = m, iw = sw - 2 * m;
    const float pageBot = sh - m;

    // ===== HEADER (title + close), directly on the skin =====
    const float titleSz = snap(28.0f);
    const float tw = fo->measure("AIOHUD", titleSz);
    cs_add(dev); soft_blob(dev, ix + tw * 0.5f, iy + snap(20.0f), tw * 0.62f, snap(32.0f),   // luminous gold aura behind the wordmark (additive)
                       ((u32)(48.0f * (0.6f + 0.4f * pulse)) << 24) | 0x00FFDC78);
    fo->begin(dev);
    fo->draw_lc(dev, ix, iy + snap(22.0f), "AIOHUD", titleSz, fa(C_GOLD), fa(C_STROKE), 2.4f);   // gold wordmark
    fo->draw_lc(dev, ix + tw + snap(14.0f), iy + snap(24.0f), "CONFIGURATION", snap(15.0f), fa(lerpc(C_ACCENT, C_ACCENTHI, pulse)), fa(C_STROKE), 1.2f);

    // close button (X), top-right -- eased red crossfade + a tiny size bump on hover
    const float cbS = snap(36.0f), cbX = ix + iw - cbS, cbY = iy + snap(2.0f);
    const bool cbHov = inrect(mo, cbX, cbY, cbS, cbS);
    const float ct = ease(1, cbHov ? 1.0f : 0.0f);
    halo(dev, cbX, cbY, cbS, cbS, C_CLOSEHOV, ct * 0.9f);
    rpanel(dev, cbX, cbY, cbS, cbS, snap(cbS * 0.30f), lerpc(C_CTL_T, C_CLOSEHOV, ct), lerpc(C_CTL_B, 0xFFA0303A, ct), lerpc(C_CTL_BR, 0xFFE57078, ct), snap(1.5f));
    fo->begin(dev);
    fo->draw_c(dev, cbX + cbS * 0.5f, cbY + cbS * 0.5f, "X", snap(18.0f) + snap(2.0f) * ct, fa(C_TEXT), fa(C_STROKE), 1.3f + 0.3f * ct);
    if (cbHov && click) { open_ = false; if (mo) cursor(dev, mo->x, mo->y); return; }

    // accent divider under the header (animated wipe-in width + a soft glow seat)
    const float divY = iy + snap(50.0f);
    flat(dev, ix, divY, iw, 1, C_BORDER);
    flat(dev, ix, divY, iw * e, snap(2.0f), lerpc(C_GOLD, C_GOLDHI, pulse));   // gold wipe-in divider

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

        const float tr = snap(10.0f);
        if (active) {
            halo(dev, tx + tabW * 0.5f - snap(2.0f), tabY + tabH * 0.5f, tabW * 0.5f, tabH * 0.5f, C_GOLD, 0.35f + 0.2f * pulse);   // gold seat glow
            rrect_top(dev, tx, tabY, tabW, tabH + snap(2.0f), tr, C_TABON_T, C_TABON_B);   // +2 : bleed into the body
            rrect_top(dev, tx + snap(2.0f), tabY + snap(1.0f), tabW - snap(4.0f), tabH * 0.45f, snap(7.0f), 0x40FFFFFF, 0x06FFFFFF);   // top sheen
        } else {
            rrect_top(dev, tx, tabY, tabW, tabH, tr, lerpc(C_TABOFF_T, C_TABHOV_T, hov_[i]), lerpc(C_TABOFF_B, C_TABHOV_B, hov_[i]));
        }
        fo->begin(dev);
        fo->draw_c(dev, tx + tabW * 0.5f, tabY + tabH * 0.5f, TABS[i], snap(15.0f),
                   lerpc(C_DIM, active ? C_GOLDHI : C_TEXT, active ? 1.0f : hov_[i]), fa(C_STROKE), 1.0f);
    }
    // sliding active-tab indicator (interpolates toward the active tab) + a soft gold glow
    if (tabSlide_ < 0.0f) tabSlide_ = activeX;
    tabSlide_ += (activeX - tabSlide_) * clampf(dt * 16.0f, 0.0f, 1.0f);
    halo(dev, tabSlide_ + tabW * 0.5f, bodyY - snap(1.0f), tabW * 0.42f, snap(6.0f), C_GOLD, 0.5f + 0.3f * pulse);
    rrect_fill(dev, tabSlide_ + snap(8.0f), bodyY - snap(3.0f), tabW - snap(16.0f), snap(3.0f), snap(1.5f), lerpc(C_GOLD, C_GOLDHI, pulse), lerpc(C_GOLD, C_GOLDHI, pulse));

    // ===== CONTENT BODY (the tab content surface) =====
    const float bodyH = pageBot - bodyY;
    shadow_down(dev, ix, bodyY, iw, snap(10.0f), (u32)(0x44000000));            // inner top shadow for depth
    vg(dev, ix, bodyY, iw, bodyH, C_CONTENT_T, C_CONTENT_B);                    // base gradient
    cs_add(dev); soft_blob(dev, ix + iw * 0.5f, bodyY + snap(4.0f), iw * 0.48f, bodyH * 0.40f, 0x0C2A4E84);   // cool light from the top (additive) -> depth
    vg(dev, ix, bodyY + bodyH - snap(150.0f), iw, snap(150.0f), 0x00000000, 0x3A000000);   // bottom inner vignette
    flat(dev, ix + 1, bodyY + 1, iw - 2, 1, 0x16FFFFFF);                        // crisp top inner highlight
    outline(dev, ix, bodyY, iw, bodyH, C_BORDERHI);

    if (tab_ == 0) {
        ui_config().wheel = 0;   // discard wheel on this tab (only Help scrolls / edit resizes)
        if (profDirty_) { profile_refresh(); profDirty_ = false; }

        // ===== LEFT : MODULES (one settings page per module ; scales to many) =====
        const float sbW = snap(220.0f);
        vg(dev, ix, bodyY, sbW, bodyH, C_SIDEBAR, 0xF0121A27);
        cs_add(dev); soft_blob(dev, ix + sbW * 0.5f, bodyY + snap(2.0f), sbW * 0.62f, bodyH * 0.16f, 0x0A2A4E84);   // faint top glow
        q4(dev, ix + sbW, bodyY, snap(22.0f), bodyH, 0x30000000, 0x00000000, 0x30000000, 0x00000000);              // recessed shadow on the content side
        flat(dev, ix + sbW, bodyY, 1, bodyH, C_BORDER);
        fo->begin(dev);
        fo->draw_lc(dev, ix + snap(20.0f), bodyY + snap(24.0f), "MODULES", snap(12.0f), fa(C_GOLD_DEEP), fa(C_STROKE), 1.4f);
        if (section_ < 0 || section_ >= MODULE_N) section_ = 0;
        for (int i = 0; i < MODULE_N; ++i) {
            const float rx = ix + snap(10.0f), rw = sbW - snap(20.0f);
            const float ry = bodyY + snap(44.0f) + i * snap(42.0f), rh = snap(36.0f);
            const bool active = (i == section_), hover = inrect(mo, rx, ry, rw, rh);
            if (hover && click) section_ = i;
            const float ht = ease(10 + i, (hover || active) ? 1.0f : 0.0f), af = active ? 1.0f : ht;
            if (active)          rrect_fill(dev, rx, ry, rw, rh, snap(9.0f), C_ROWON_T, C_ROWON_B);
            else if (ht > 0.01f) rrect_fill(dev, rx, ry, rw, rh, snap(9.0f), ((u32)(0x24 * ht) << 24) | 0x00FFFFFF, ((u32)(0x12 * ht) << 24) | 0x00FFFFFF);
            if (af > 0.01f) rrect_fill(dev, rx + snap(2.0f), ry + rh * (1.0f - af) * 0.5f, snap(4.0f), rh * af, snap(2.0f), lerpc(C_GOLD, C_GOLDHI, pulse), lerpc(C_GOLD, C_GOLDHI, pulse));   // gold accent pill
            fo->begin(dev); fo->draw_lc(dev, rx + snap(18.0f), ry + rh * 0.5f, MODULES[i], snap(15.0f), lerpc(C_DIM, C_TEXT, active ? 1.0f : ht), fa(C_STROKE), 1.0f);
        }
        // forward-looking hint that the sidebar scales (no interaction)
        fo->begin(dev); fo->draw_lc(dev, ix + snap(26.0f), bodyY + snap(44.0f) + MODULE_N * snap(42.0f) + snap(18.0f), "more modules soon", snap(12.0f), fa(C_MUTE), fa(C_STROKE), 1.0f);

        const float coX = ix + sbW + snap(30.0f);
        const float coW = (ix + iw) - coX - snap(26.0f);

        // ===== PROFILE BAR (full content width : active profile + unsaved + quick Save) =====
        const bool dirty = activeProf_[0] && profile_dirty();
        const float barY = bodyY + snap(18.0f), barH = snap(46.0f), barCy = barY + barH * 0.5f;
        drop_shadow(dev, coX, barY, coW, barH, snap(4.0f), 50);
        rpanel(dev, coX, barY, coW, barH, snap(10.0f), 0x55101826, 0x550A111C, C_BORDER, snap(1.5f));
        rrect_fill(dev, coX + snap(4.0f), barY + snap(7.0f), snap(4.0f), barH - snap(14.0f), snap(2.0f), lerpc(C_GOLD, C_GOLDHI, pulse), lerpc(C_GOLD, C_GOLDHI, pulse));   // gold accent pill
        fo->begin(dev); fo->draw_lc(dev, coX + snap(18.0f), barCy, "PROFILE", snap(11.0f), fa(C_GOLD_DEEP), fa(C_STROKE), 1.2f);
        const int nprof = profile_count();
        int cur = -1; for (int i = 0; i < nprof; ++i) if (activeProf_[0] && strcmp(profile_name(i), activeProf_) == 0) { cur = i; break; }
        const float aS = snap(28.0f), ay = barCy - aS * 0.5f, lx = coX + snap(86.0f);
        if (arrow_btn(dev, fo, mo, click, 400, lx, ay, aS, "<") && nprof > 0) {
            const char* nm = profile_name((cur <= 0) ? nprof - 1 : cur - 1);
            profile_load(nm); strncpy(activeProf_, nm, 31); activeProf_[31] = 0;
        }
        // a recessed BLACK field holds the active profile, BETWEEN the two steppers.
        const float fX = lx + aS + snap(10.0f);
        const float nX = lx + aS + snap(258.0f);                  // > stepper x (field sits just left of it)
        const float fW = (nX - snap(10.0f)) - fX, fH = snap(30.0f), fY = barCy - fH * 0.5f;
        rpanel(dev, fX, fY, fW, fH, snap(7.0f), 0xE6070B13, 0xE604070D, 0x55355072, snap(1.5f));
        fo->begin(dev);
        if (activeProf_[0]) {
            if (dirty) { cs(dev); disc(dev, fX + snap(13.0f), barCy, snap(3.5f), fa(0xFFFFB454)); }   // unsaved dot
            fo->begin(dev); fo->draw_c(dev, fX + fW * 0.5f, barCy, activeProf_, snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);   // WHITE = a profile is loaded
        } else {
            fo->draw_c(dev, fX + fW * 0.5f, barCy, "(none -- open the Profile tab)", snap(13.0f), fa(C_DIM), fa(C_STROKE), 1.0f);   // GREY = no profile
        }
        if (arrow_btn(dev, fo, mo, click, 401, nX, ay, aS, ">") && nprof > 0) {
            const char* nm = profile_name((cur < 0 || cur >= nprof - 1) ? 0 : cur + 1);
            profile_load(nm); strncpy(activeProf_, nm, 31); activeProf_[31] = 0;
        }
        // right : Save (updates the active profile in place ; GOLD glow while there are unsaved changes)
        const float bH = snap(30.0f), bY = barCy - bH * 0.5f, saveW = snap(140.0f);
        const float bx = coX + coW - snap(12.0f) - saveW;
        {
            const bool canSave = activeProf_[0] != 0;
            const bool hov = canSave && inrect(mo, bx, bY, saveW, bH);
            const float t = ease(410, hov ? 1.0f : 0.0f);
            const float sr = snap(bH * 0.30f);
            if (dirty) {                                  // unsaved -> a SOLID gold pill + dark, high-contrast label
                halo(dev, bx, bY, saveW, bH, C_GOLD, 0.55f + 0.35f * pulse);
                rpanel(dev, bx, bY, saveW, bH, sr, lerpc(0xFFF2C24E, 0xFFFFE08A, t), lerpc(0xFFCB901C, 0xFFE3A626, t), C_GOLDHI, snap(1.5f));
                rrect_top(dev, bx + snap(2.0f), bY + snap(2.0f), saveW - snap(4.0f), bH * 0.42f, snap(sr * 0.6f), 0x44FFFFFF, 0x08FFFFFF);
                shine(dev, bx + snap(2.0f), bY + snap(2.0f), saveW - snap(4.0f), bH - snap(4.0f), 0.85f, g_t);   // continuous sweep -> "act on me"
                fo->begin(dev); fo->draw_c(dev, bx + saveW * 0.5f, barCy, "Save changes", snap(14.0f), fa(0xFF241600), 0, 0.0f);
            } else {                                      // saved / nothing to save -> quiet navy
                if (canSave) halo(dev, bx, bY, saveW, bH, C_ACCENT, t * 0.7f);
                rpanel(dev, bx, bY, saveW, bH, sr, canSave ? lerpc(0xFF2A3548, 0xFF3A82E0, t) : 0xFF1E2630, canSave ? lerpc(0xFF1D2738, 0xFF2A61B6, t) : 0xFF141A22, lerpc(C_BORDERHI, C_ACCENTHI, t), snap(1.5f));
                fo->begin(dev); fo->draw_c(dev, bx + saveW * 0.5f, barCy, "Saved", snap(14.0f), fa(canSave ? C_TEXT : C_MUTE), fa(C_STROKE), 1.0f);
            }
            if (canSave && dirty && hov && click) { profile_save(activeProf_); }
        }

        // ===== two columns below the bar : controls (left) | LIVE PREVIEW (right) =====
        // previewW is PROPORTIONAL (screenW_ is the real backbuffer, not a fixed canvas) with guards
        // so the controls column never collapses on a small resolution.
        const float splitGap = snap(40.0f);
        float previewW = coW * 0.40f;
        const float minCtrl = snap(560.0f);
        if (coW - previewW - splitGap < minCtrl) previewW = coW - splitGap - minCtrl;
        if (previewW < snap(260.0f)) previewW = snap(260.0f);
        const float ctrlW = coW - previewW - splitGap;
        const float coY = barY + barH + snap(26.0f);

        // section title (GOLD) -- underline spans the FULL title width (wipes in)
        fo->begin(dev);
        const float secW = fo->measure(MODULES[section_], snap(20.0f));
        fo->draw_lc(dev, coX, coY, MODULES[section_], snap(20.0f), fa(C_GOLD), fa(C_STROKE), 1.4f);
        flat(dev, coX, coY + snap(18.0f), secW * e, snap(2.0f), lerpc(C_GOLD, C_GOLDHI, pulse));

        // LIVE PREVIEW stage : a recessed backdrop in the right column. The HUD draws the REAL
        // party + 2-alliance demo boxes (forced //aio alliance2 demo) on top, anchored bottom-right
        // here -- so the preview is exactly what ships in game (cost box space included).
        {
            const float pvx = coX + ctrlW + splitGap, pvy = coY - snap(2.0f);
            fo->begin(dev); fo->draw_lc(dev, pvx, pvy + snap(7.0f), "LIVE PREVIEW", snap(12.0f), fa(C_GOLD_DEEP), fa(C_STROKE), 1.4f);
            const float stageY = pvy + snap(22.0f), stageH = pageBot - stageY;
            drop_shadow(dev, pvx, stageY, previewW, stageH, snap(6.0f), 60);
            rpanel(dev, pvx, stageY, previewW, stageH, snap(12.0f), 0x40080C16, 0x40060A12, C_BORDER, snap(1.5f));
            vg(dev, pvx + snap(2.0f), stageY + snap(2.0f), previewW - snap(4.0f), snap(46.0f), 0x55000000, 0x00000000);   // inner top shadow -> recessed screen
            cs_add(dev); soft_blob(dev, pvx + previewW * 0.5f, stageY + stageH - snap(8.0f), previewW * 0.5f, stageH * 0.34f, 0x0E1E5AA0);   // soft blue backlight rising from the bottom (additive)
            rrect_top(dev, pvx + snap(2.0f), stageY + snap(2.0f), previewW - snap(4.0f), snap(40.0f), snap(10.0f), 0x14FFFFFF, 0x00FFFFFF);   // top sheen
            pvRightX_  = pvx + previewW - snap(18.0f);
            pvBottomY_ = pageBot - snap(14.0f);
            pvOn_ = true;
        }

        // each control row sits on an alternating band (label<->control tie) + eases in, staggered.
        const float bandX = coX - snap(12.0f), bandW = ctrlW + snap(24.0f);
        float ry = coY + snap(44.0f); int ri = 0;
        // ROW_BAND : draw the alternating background band + set the staggered entrance (g_fade, yo).
        // ROW_NEXT : advance ry by the row's slot height and bump the stagger index.
        // yo also vertically CENTERS the row content (helper rows are 40px tall) inside the taller band.
        #define ROW_BAND(slotH)   row_band(dev, bandX, ry, bandW, snap(slotH), (ri & 1) != 0, 0.0f); \
            float ap = stagger(anim_, ri); g_fade = e * ap; \
            float yo = (1.0f - ap) * snap(14.0f) + (snap(slotH) - snap(40.0f)) * 0.5f; (void)ap;
        #define ROW_NEXT(adv)     ry += snap(adv); ri++;

        // Box Theme (name only -- the live preview shows the actual skin)
        { ROW_BAND(52.0f)
          if (int d = row_selector(dev, fo, mo, click, 20, coX, ry + yo, ctrlW, "Box Theme", window_theme_name(ui_config().skinTheme))) {
              ui_config().skinTheme = wrap(ui_config().skinTheme + d, window_theme_count()); save_ui_config(); } }
        ROW_NEXT(52.0f)
        // Font -> party/alliance text face
        { ROW_BAND(52.0f)
          if (int d = row_selector(dev, fo, mo, click, 30, coX, ry + yo, ctrlW, "Font", ui_font_label(ui_config().fontFace))) {
              ui_config().fontFace = wrap(ui_config().fontFace + d, ui_font_count()); save_ui_config(); } }
        ROW_NEXT(52.0f)
        // Box Size -> per-box scale, INDEPENDENT for Party / Alliance 1 / Alliance 2 (sliders, 5% steps).
        // PARTY min is 100% : below that its footprint can no longer cover the game's native party block,
        // so it can only grow (100%..200%). Alliances are free to shrink too (50%..200%).
        {
            const int   szTier[3] = { 0, 1, 2 };
            const char* szLbl[3]  = { "Party Size", "Ally 1 Size", "Ally 2 Size" };
            const int   szId[3]   = { 1, 3, 4 };
            const float szLo[3]   = { 1.00f, 0.50f, 0.50f };   // party floor = 100% (native-block coverage)
            const float hi = 2.00f;
            for (int k = 0; k < 3; ++k) {
                const int t = szTier[k]; const float lo = szLo[k];
                ROW_BAND(46.0f)
                char szbuf[16]; sprintf(szbuf, "%d%%", (int)(ui_config().box[t].scale * 100.0f + 0.5f));
                float v01 = (ui_config().box[t].scale - lo) / (hi - lo);
                v01 = v01 < 0.0f ? 0.0f : (v01 > 1.0f ? 1.0f : v01);
                if (row_slider(dev, fo, mo, szId[k], coX, ry + yo, ctrlW, szLbl[k], szbuf, &v01)) {
                    float v = lo + v01 * (hi - lo);
                    v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f;       // snap to 5% steps
                    v = v < lo ? lo : (v > hi ? hi : v);
                    ui_config().box[t].scale = v;                       // ONLY this tier
                }
                ROW_NEXT(46.0f)
            }
        }
        // Buff Size -> fraction of the player row (slider, 40%..100%, 5% steps ; independent of Font Size)
        { ROW_BAND(52.0f)
            const float lo = 0.40f, hi = 1.00f;
            char bzbuf[16]; sprintf(bzbuf, "%d%%", (int)(ui_config().buffScale * 100.0f + 0.5f));
            float v01 = (ui_config().buffScale - lo) / (hi - lo);
            if (row_slider(dev, fo, mo, 2, coX, ry + yo, ctrlW, "Buff Size", bzbuf, &v01)) {
                float v = lo + v01 * (hi - lo);
                v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f;          // snap to 5% steps
                ui_config().buffScale = v < lo ? lo : (v > hi ? hi : v);
            }
        }
        ROW_NEXT(52.0f)
        // Borders : per-box window-skin chrome on/off (Party box, Cost MP box, Alliance 1, Alliance 2)
        { ROW_BAND(56.0f)
            const float rowH = snap(40.0f), ty = ry + yo;
            fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, "Borders", snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
            const char* blbl[4] = { "Party", "Cost", "Ally 1", "Ally 2" };
            bool* bval[4] = { &ui_config().border[0], &ui_config().borderCost, &ui_config().border[1], &ui_config().border[2] };
            const float bbw = snap(76.0f), bgap = snap(8.0f), bbh = snap(34.0f);
            const float bx0 = coX + ctrlW - (4 * bbw + 3 * bgap), bty = ty + (rowH - bbh) * 0.5f;   // centred in the band
            for (int i = 0; i < 4; ++i) {
                const float bx2 = bx0 + i * (bbw + bgap);
                if (toggle_chip(dev, fo, mo, click, 50 + i * 2, bx2, bty, bbw, bbh, blbl[i], *bval[i])) { *bval[i] = !*bval[i]; save_ui_config(); }
            }
        }
        ROW_NEXT(56.0f)
        // Buttons : Edit Layout (enter drag/resize) + Default (reset EVERYTHING)
        { ROW_BAND(56.0f)
            const float bh = snap(34.0f), bw = snap(168.0f), gap = snap(10.0f);
            const float ty = ry + yo + (snap(40.0f) - bh) * 0.5f;   // these buttons are 34px (not 40) -> recentre in the band
            const float defX = coX + ctrlW - bw, edX = defX - gap - bw;
            fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + bh * 0.5f, "Layout", snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
            if (push_btn(dev, fo, mo, click, 60, edX, ty, bw, bh, "Edit Layout", 0)) ui_config().editLayout = true;
            if (push_btn(dev, fo, mo, click, 61, defX, ty, bw, bh, "Default (all)", 1)) reset_ui_config();
        }
        ROW_NEXT(56.0f)
        #undef ROW_BAND
        #undef ROW_NEXT
        g_fade = e;   // restore for the footer
    } else if (tab_ == 1) {
        ui_config().wheel = 0;
        if (profDirty_) { profile_refresh(); profDirty_ = false; }
        bool commit = kbCommit_; kbCommit_ = false;
        const char* charName = (f.game && f.game->inGame && f.game->me.name[0]) ? f.game->me.name : 0;
        const int   nprof = profile_count();
        const bool  dirty = activeProf_[0] && profile_dirty();

        // ===== LEFT RAIL : the current character + one-click saves =====
        const float sbW = snap(290.0f);
        vg(dev, ix, bodyY, sbW, bodyH, C_SIDEBAR, 0xF0121A27);
        cs_add(dev); soft_blob(dev, ix + sbW * 0.5f, bodyY + snap(2.0f), sbW * 0.62f, bodyH * 0.16f, 0x0A2A4E84);   // faint top glow
        q4(dev, ix + sbW, bodyY, snap(22.0f), bodyH, 0x30000000, 0x00000000, 0x30000000, 0x00000000);              // recessed shadow on the content side
        flat(dev, ix + sbW, bodyY, 1, bodyH, C_BORDER);
        const float cx0 = ix + snap(20.0f), cw0 = sbW - snap(40.0f);
        fo->begin(dev); fo->draw_lc(dev, cx0, bodyY + snap(24.0f), "CHARACTER", snap(12.0f), fa(C_GOLD_DEEP), fa(C_STROKE), 1.4f);

        float ry0 = bodyY + snap(40.0f); const float ccH = snap(98.0f);
        drop_shadow(dev, cx0, ry0, cw0, ccH, snap(4.0f), 50);
        rpanel(dev, cx0, ry0, cw0, ccH, snap(11.0f), 0x55101826, 0x550A111C, C_BORDER, snap(1.5f));
        rrect_fill(dev, cx0 + snap(4.0f), ry0 + snap(9.0f), snap(4.0f), ccH - snap(18.0f), snap(2.0f), lerpc(C_GOLD, C_GOLDHI, pulse), lerpc(C_GOLD, C_GOLDHI, pulse));
        const float avR = snap(22.0f), avX = cx0 + snap(34.0f), avY = ry0 + snap(34.0f);
        cs(dev); disc(dev, avX, avY, avR, fa(0xFF2C6AC4)); disc(dev, avX, avY, avR - snap(2.0f), fa(0xFF15315C));
        if (charName) { char ini[2] = { (char)(charName[0] >= 'a' && charName[0] <= 'z' ? charName[0] - 32 : charName[0]), 0 };
            fo->begin(dev); fo->draw_cc(dev, avX, avY, ini, snap(22.0f), fa(C_GOLDHI), fa(C_STROKE), 1.6f); }
        else { fo->begin(dev); fo->draw_cc(dev, avX, avY, "?", snap(22.0f), fa(C_MUTE), fa(C_STROKE), 1.4f); }
        const float nlx = avX + avR + snap(14.0f);
        fo->begin(dev);
        fo->draw_lc(dev, nlx, ry0 + snap(26.0f), charName ? charName : "(not logged in)", snap(17.0f), fa(charName ? C_TEXT : C_MUTE), fa(C_STROKE), 1.2f);
        char actl[80]; if (activeProf_[0]) _snprintf(actl, sizeof(actl), "Active : %s", activeProf_); else strcpy(actl, "Active : none");
        fo->draw_lc(dev, nlx, ry0 + snap(50.0f), actl, snap(12.0f), fa(C_DIM), fa(C_STROKE), 1.0f);
        if (dirty) { cs(dev); disc(dev, nlx + snap(3.0f), ry0 + snap(70.0f), snap(3.5f), fa(0xFFFFB454));
            fo->begin(dev); fo->draw_lc(dev, nlx + snap(12.0f), ry0 + snap(70.0f), "unsaved changes", snap(11.0f), fa(0xFFFFB454), fa(C_STROKE), 1.0f); }

        // quick-save buttons
        ry0 += ccH + snap(18.0f);
        fo->begin(dev); fo->draw_lc(dev, cx0, ry0, "QUICK SAVE", snap(11.0f), fa(C_GOLD_DEEP), fa(C_STROKE), 1.2f);
        ry0 += snap(16.0f); const float qbH = snap(40.0f);
        { char lbl[48]; if (charName) _snprintf(lbl, sizeof(lbl), "Save for %s", charName); else strcpy(lbl, "Save for character");
          if (push_btn(dev, fo, mo, click, 710, cx0, ry0, cw0, qbH, lbl, 0) && charName) {
              profile_save(charName); strncpy(activeProf_, charName, 31); activeProf_[31] = 0;
              nameBuf_[0] = 0; nameLen_ = 0; nameCur_ = 0; nameFocus_ = false; profDirty_ = true; } }
        ry0 += qbH + snap(10.0f);
        if (push_btn(dev, fo, mo, click, 711, cx0, ry0, cw0, qbH, "Save as Default", 0)) {
            profile_save("Default"); strcpy(activeProf_, "Default");
            nameBuf_[0] = 0; nameLen_ = 0; nameCur_ = 0; nameFocus_ = false; profDirty_ = true; }
        ry0 += qbH + snap(18.0f);
        fo->begin(dev); fo->draw_lc(dev, cx0, ry0, "A profile snapshots every setting. Quick-save", snap(11.0f), fa(C_MUTE), fa(C_STROKE), 1.0f);
        fo->draw_lc(dev, cx0, ry0 + snap(16.0f), "binds one to this character or the default.", snap(11.0f), fa(C_MUTE), fa(C_STROKE), 1.0f);

        // ===== RIGHT : create / rename + the saved library =====
        const float pX = ix + sbW + snap(34.0f), pW = (ix + iw) - pX - snap(30.0f);
        float py = bodyY + snap(26.0f);
        fo->begin(dev);
        const float ptW = fo->measure("Profiles", snap(22.0f));
        fo->draw_lc(dev, pX, py, "Profiles", snap(22.0f), fa(C_GOLD), fa(C_STROKE), 1.4f);
        flat(dev, pX, py + snap(22.0f), ptW * e, snap(2.0f), lerpc(C_GOLD, C_GOLDHI, pulse));
        fo->begin(dev);   // re-bind the glyph atlas : the underline flat() above left a colour-only state
        fo->draw_lc(dev, pX, py + snap(44.0f), "Load to switch instantly. Type a name to create a new profile, or an existing name to overwrite it.", snap(13.0f), fa(C_DIM), fa(C_STROKE), 1.0f);

        // create / rename field (rounded recessed) + action button
        py += snap(66.0f);
        fo->begin(dev); fo->draw_lc(dev, pX, py, "NEW PROFILE", snap(11.0f), fa(C_GOLD_DEEP), fa(C_STROKE), 1.2f);
        py += snap(16.0f);
        const float fH = snap(42.0f), btnW = snap(148.0f), fGap = snap(12.0f), fW = pW - btnW - fGap;
        const bool fldHov = inrect(mo, pX, py, fW, fH);
        if (click) { const bool was = nameFocus_; nameFocus_ = fldHov; if (fldHov && !was) nameCur_ = nameLen_; }
        const float ft = ease(700, nameFocus_ ? 1.0f : 0.0f);
        halo(dev, pX, py, fW, fH, C_ACCENT, ft * 0.6f);
        rpanel(dev, pX, py, fW, fH, snap(8.0f), 0xE6080C14, 0xE605080F, lerpc(C_CTL_BR, C_ACCENT, ft), snap(1.5f));
        const float txY = py + fH * 0.5f, txX = pX + snap(15.0f);
        fo->begin(dev);
        if (nameLen_ > 0) fo->draw_lc(dev, txX, txY, nameBuf_, snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
        else              fo->draw_lc(dev, txX, txY, "Type a profile name...", snap(14.0f), fa(C_MUTE), fa(C_STROKE), 1.0f);
        if (nameFocus_ && sinf(f.t * 5.0f) > 0.0f) {              // blinking caret AT the cursor index
            int cn = nameCur_ < 0 ? 0 : (nameCur_ > nameLen_ ? nameLen_ : nameCur_);
            char pre[32]; memcpy(pre, nameBuf_, cn); pre[cn] = 0;
            const float cxx = txX + (cn > 0 ? fo->measure(pre, snap(15.0f)) : 0.0f) + snap(1.0f);
            flat(dev, cxx, py + snap(11.0f), snap(2.0f), fH - snap(22.0f), C_ACCENTHI); }
        const bool canSave = nameLen_ > 0, exists = canSave && profile_exists(nameBuf_);
        const char* lbl = exists ? "Overwrite" : "Create";   // always non-destructive : never auto-renames the active one
        const bool doSave = (push_btn(dev, fo, mo, click, 701, pX + fW + fGap, py, btnW, fH, lbl, 0) || commit) && canSave;
        if (doSave) {
            profile_save(nameBuf_);
            strncpy(activeProf_, nameBuf_, sizeof(activeProf_) - 1); activeProf_[sizeof(activeProf_) - 1] = 0;
            nameBuf_[0] = 0; nameLen_ = 0; nameCur_ = 0; nameFocus_ = false;   // clear the field -> ready for the NEXT new profile
            profDirty_ = true;
        }

        // saved library
        py += fH + snap(24.0f);
        fo->begin(dev); char hdr[48]; sprintf(hdr, "SAVED  (%d)", nprof);
        fo->draw_lc(dev, pX, py, hdr, snap(12.0f), fa(C_GOLD_DEEP), fa(C_STROKE), 1.2f);
        py += snap(24.0f);
        const float rowH = snap(50.0f), rGap = snap(9.0f);
        if (nprof == 0) {
            rpanel(dev, pX, py, pW, snap(62.0f), snap(11.0f), 0x2A101826, 0x2A0A111C, C_BORDER, snap(1.5f));
            fo->begin(dev); fo->draw_c(dev, pX + pW * 0.5f, py + snap(31.0f), "No profiles yet -- quick-save one, or type a name and Create.", snap(14.0f), fa(C_DIM), fa(C_STROKE), 1.0f);
        }
        const int maxRows = (int)((pageBot - py) / (rowH + rGap));
        const int cnLen = charName ? (int)strlen(charName) : 0;
        for (int i = 0; i < nprof && i < maxRows; ++i) {
            const char* nm = profile_name(i);
            const bool active = activeProf_[0] && strcmp(nm, activeProf_) == 0;
            const bool isDef  = strcmp(nm, "Default") == 0;
            const bool isChar = charName && (strcmp(nm, charName) == 0 || (strncmp(nm, charName, cnLen) == 0 && nm[cnLen] == '/'));
            const float ap = stagger(anim_, i); g_fade = e * ap;
            const float ry = py + i * (rowH + rGap) + (1.0f - ap) * snap(12.0f);
            const bool rowHov = inrect(mo, pX, ry, pW, rowH);
            const float rt = ease(320 + i, rowHov ? 1.0f : 0.0f);
            rpanel(dev, pX, ry, pW, rowH, snap(11.0f),
                   lerpc(active ? 0x55203A66 : 0x26141C28, active ? 0x55295082 : 0x33223A5C, rt),
                   lerpc(active ? 0x55172C4E : 0x260E141E, active ? 0x55203F6E : 0x331A2A44, rt),
                   active ? C_GOLD : lerpc(C_BORDER, C_BORDERHI, rt), snap(1.5f));
            rrect_fill(dev, pX + snap(4.0f), ry + snap(9.0f), snap(4.0f), rowH - snap(18.0f), snap(2.0f), lerpc(C_GOLD, C_GOLDHI, active ? 1.0f : pulse), lerpc(C_GOLD, C_GOLDHI, active ? 1.0f : pulse));
            fo->begin(dev); fo->draw_lc(dev, pX + snap(20.0f), ry + rowH * 0.5f, nm, snap(16.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
            float bxa = pX + snap(20.0f) + fo->measure(nm, snap(16.0f)) + snap(12.0f);
            const float bcy = ry + rowH * 0.5f;
            if (active) bxa += badge(dev, fo, bxa, bcy, "ACTIVE",  0xCC2E8C49, 0xFF49C46A, 0xFFEAFBEF) + snap(6.0f);
            if (isDef)  bxa += badge(dev, fo, bxa, bcy, "DEFAULT", 0xCC8A6A1E, C_GOLD,     0xFF2A1C06) + snap(6.0f);
            if (isChar) bxa += badge(dev, fo, bxa, bcy, charName,  0xCC2C6AC4, 0xFF64B4FF, 0xFFEAF3FF) + snap(6.0f);
            const float lbw = snap(84.0f), dbw = snap(84.0f), bgap = snap(8.0f), bH = snap(32.0f);
            const float dX = pX + pW - dbw - snap(10.0f), lX = dX - bgap - lbw, bY = ry + (rowH - bH) * 0.5f;
            if (push_btn(dev, fo, mo, click, 760 + i * 2, lX, bY, lbw, bH, "Load", 0)) {
                profile_load(nm);
                strncpy(activeProf_, nm, sizeof(activeProf_) - 1); activeProf_[sizeof(activeProf_) - 1] = 0;
                nameBuf_[0] = 0; nameLen_ = 0; nameCur_ = 0; nameFocus_ = false;   // keep the create field free for a NEW profile
            }
            if (push_btn(dev, fo, mo, click, 761 + i * 2, dX, bY, dbw, bH, "Delete", 1)) {
                if (active) { activeProf_[0] = 0; nameBuf_[0] = 0; nameLen_ = 0; nameCur_ = 0; }
                profile_delete(nm); profDirty_ = true;
            }
        }
        g_fade = e;
    } else {
        // ===== HELP tab : a left menu of MODULES + the selected module's reference (scrollable) =====
        const float sbW = snap(220.0f);
        vg(dev, ix, bodyY, sbW, bodyH, C_SIDEBAR, 0xF0121A27);
        cs_add(dev); soft_blob(dev, ix + sbW * 0.5f, bodyY + snap(2.0f), sbW * 0.62f, bodyH * 0.16f, 0x0A2A4E84);   // faint top glow
        q4(dev, ix + sbW, bodyY, snap(22.0f), bodyH, 0x30000000, 0x00000000, 0x30000000, 0x00000000);              // recessed shadow on the content side
        flat(dev, ix + sbW, bodyY, 1, bodyH, C_BORDER);
        fo->begin(dev);
        fo->draw_lc(dev, ix + snap(20.0f), bodyY + snap(24.0f), "MODULES", snap(12.0f), fa(C_GOLD_DEEP), fa(C_STROKE), 1.4f);
        for (int i = 0; i < HELP_MODULE_N; ++i) {
            const float rx = ix + snap(10.0f), rw = sbW - snap(20.0f);
            const float ry = bodyY + snap(44.0f) + i * snap(42.0f), rh = snap(36.0f);
            const bool active = (i == helpSel_), hover = inrect(mo, rx, ry, rw, rh);
            if (hover && click && i != helpSel_) { helpSel_ = i; helpScroll_ = 0.0f; }
            const float ht = ease(200 + i, (hover || active) ? 1.0f : 0.0f);
            if (active) vg(dev, rx, ry, rw, rh, C_ROWON_T, C_ROWON_B);
            else if (ht > 0.01f) flat(dev, rx, ry, rw, rh, (0x22FFFFFF & 0x00FFFFFF) | ((u32)(0x22 * ht) << 24));
            flat(dev, rx, ry, snap(3.0f), rh * (active ? 1.0f : ht), lerpc(C_GOLD, C_GOLDHI, pulse));
            fo->begin(dev);
            fo->draw_lc(dev, rx + snap(16.0f), ry + rh * 0.5f, HELP_MODULES[i].name, snap(15.0f),
                        lerpc(C_DIM, C_TEXT, active ? 1.0f : ht), fa(C_STROKE), 1.0f);
        }

        // content : the selected module's help, scrollable
        if (helpSel_ < 0 || helpSel_ >= HELP_MODULE_N) helpSel_ = 0;
        const HelpModule& mod = HELP_MODULES[helpSel_];
        const float hx = ix + sbW + snap(30.0f);
        float hw = (ix + iw) - hx - snap(40.0f);
        const float hwMax = snap(1180.0f); if (hw > hwMax) hw = hwMax;   // cap line length for readability
        const float top = bodyY + snap(8.0f), bot = pageBot - snap(4.0f);
        if (ui_config().wheel != 0) { helpScroll_ -= (float)ui_config().wheel * snap(40.0f); ui_config().wheel = 0; }
        const float natTop = top + snap(16.0f), lh = snap(22.0f), bsz = snap(14.0f), hsz = snap(18.0f);
        float y = natTop - helpScroll_;
        for (int i = 0; i < mod.count; ++i) {
            const HelpItem& it = mod.items[i];
            if (it.kind == 0) {                       // heading + accent underline
                y += snap(16.0f);
                if (y + lh > top && y < bot) {
                    fo->begin(dev); fo->draw_lc(dev, hx, y + lh * 0.5f, it.text, hsz, fa(C_GOLD), fa(C_STROKE), 1.3f);
                    flat(dev, hx, y + lh + snap(1.0f), snap(34.0f), snap(2.0f), lerpc(C_GOLD, C_GOLDHI, pulse));
                }
                y += lh + snap(8.0f);
            } else if (it.kind == 2) {                // bullet
                if (y + lh > top && y < bot) flat(dev, hx + snap(6.0f), y + lh * 0.5f - snap(2.0f), snap(4.0f), snap(4.0f), fa(C_ACCENT));
                y = draw_wrapped(dev, fo, hx + snap(20.0f), y, hw - snap(20.0f), top, bot, it.text, bsz, C_DIM, lh);
                y += snap(3.0f);
            } else {                                  // paragraph
                y = draw_wrapped(dev, fo, hx, y, hw, top, bot, it.text, bsz, C_DIM, lh);
                y += snap(10.0f);
            }
        }
        // scroll clamp (next frame) + a thin scrollbar on the right
        const float viewH = bot - natTop, contentH = (y + helpScroll_) - natTop;
        float maxScroll = contentH - viewH; if (maxScroll < 0.0f) maxScroll = 0.0f;
        if (helpScroll_ < 0.0f) helpScroll_ = 0.0f; if (helpScroll_ > maxScroll) helpScroll_ = maxScroll;
        if (maxScroll > 0.0f) {
            const float sbX = ix + iw - snap(8.0f);
            flat(dev, sbX, top, snap(3.0f), bot - top, 0x22FFFFFF);
            const float thH = (bot - top) * (viewH / contentH), thY = top + (bot - top - thH) * (helpScroll_ / maxScroll);
            flat(dev, sbX, thY, snap(3.0f), thH, lerpc(C_ACCENT, C_ACCENTHI, pulse));
        }
    }

    // footer + cursor on top
    if (mo && mo->focused) cursor(dev, mo->x, mo->y);   // hide our cursor when the game isn't the OS foreground
}

} // namespace aio
