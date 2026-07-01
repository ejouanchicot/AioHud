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
#include "ui/party.h"          // party_gauge() : the REAL HP/MP/TP liquid gauge, for the Help live samples
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
// ---- language : 0 = English, 1 = French. tr() picks the active-language string for inline UI text. ----
static const char* tr(const char* en, const char* fr) { return ui_config().lang == 1 ? fr : en; }
static const char* tab_label(int i) {
    static const char* en[] = { "Configuration", "Profile", "Help" };
    static const char* fr[] = { "Configuration", "Profil", "Aide" };
    return (ui_config().lang == 1 ? fr : en)[i];
}
// Settings modules (the Configuration sidebar). Add a module here = a new settings page ; the profile
// is GLOBAL (a profile snapshots every module), so it lives in the profile bar, not per-module.
static const char* MODULES[]  = { "Party / Alliance" };
static const int   MODULE_N   = (int)(sizeof(MODULES) / sizeof(MODULES[0]));
static const char* module_label(int i) {                       // Configuration sidebar module name (localized)
    static const char* fr[] = { "Groupe / Alliance" };
    return (ui_config().lang == 1) ? fr[i] : MODULES[i];
}

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
    const float peak = 46.0f * clampf(amt, 0.0f, 1.0f) * g_fade;
    // alpha falls off LINEARLY from the streak centre. Compute it at each (possibly clamped) edge, so the
    // streak stays a SOFT gradient even while entering / leaving the rect -- it used to collapse to a hard
    // solid block at the ends (the "blur lost at the end of the sweep").
    float d0 = fabsf(x0 - mid) / sw; if (d0 > 1.0f) d0 = 1.0f;
    float d1 = fabsf(x1 - mid) / sw; if (d1 > 1.0f) d1 = 1.0f;
    const u32 c0 = ((u32)(peak * (1.0f - d0)) << 24) | 0x00FFFFFF;
    const u32 c1 = ((u32)(peak * (1.0f - d1)) << 24) | 0x00FFFFFF;
    const u32 cM = ((u32)peak << 24) | 0x00FFFFFF;
    cs_add(dev);
    if (mid > x0 && mid < x1) {
        grad_quad(dev, x0, y, mid - x0, h, c0, cM, c0, cM);
        grad_quad(dev, mid, y, x1 - mid, h, cM, c1, cM, c1);
    } else {
        grad_quad(dev, x0, y, x1 - x0, h, c0, c1, c0, c1);     // clamped at an edge -> still a soft falloff, no hard block
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
// a small rounded tag. ONE accent colour drives it : a dark opaque pill + accent border + BRIGHT accent
// text -> always high-contrast and legible, whatever the row colour behind it.
static float badge(u32 dev, Font* fo, float x, float cy, const char* text, u32 accent) {
    const float sz = snap(10.0f), padx = snap(9.0f), h = snap(18.0f);
    const float w = fo->measure(text, sz) + 2.0f * padx, y = cy - h * 0.5f;
    rpanel(dev, x, y, w, h, h * 0.5f, 0xF00E1420, 0xF0090D16, accent, snap(1.2f));   // dark opaque pill + accent border
    fo->begin(dev); fo->draw_c(dev, x + w * 0.5f, cy, text, sz, fa(accent), fa(C_STROKE), 0.9f);   // bright accent text
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

// ---- Help content. kind 0 = heading, 1 = paragraph, 2 = bullet. Each item carries EN + FR text. ----
struct HelpItem { int kind; const char* en; const char* fr; };

// GENERAL : everything global -- the window, language, profiles, the command list. (Module-specific
// help lives under its own page below, e.g. Party / Alliance.)
static const HelpItem HELP_GENERAL[] = {
    {0, "AioHUD", "AioHUD"},
    {1, "AioHUD is a from-scratch interface for FFXI, drawn live over the game. For now it provides the *Party* and *Alliance* module, and more will follow. Everything is set up from this window.",
        "AioHUD est une interface repensée de zéro pour FFXI, dessinée en direct par-dessus le jeu. Pour l'instant elle fournit le module *Party* et *Alliance*, et d'autres suivront. Tout se règle depuis cette fenêtre."},

    {0, "The config window", "La fenêtre de configuration"},
    {1, "Type //aio config to open this window. It has three tabs:",
        "Tape //aio config pour ouvrir cette fenêtre. Elle comporte trois onglets :"},
    {2, "*Configuration* tunes how things look, with a live preview on the right.",
        "*Configuration* règle l'apparence, avec un aperçu en direct à droite."},
    {2, "*Profile* saves, loads and manages complete setups.",
        "*Profil* enregistre, charge et gère des configurations complètes."},
    {2, "*Help* is this reference. Pick a module in the left column to read about it.",
        "*Aide* est cette référence. Choisis un module dans la colonne de gauche pour le découvrir."},

    {0, "Language", "Langue"},
    {1, "The EN / FR button in the top-right corner switches the whole interface between English and French. It is on every tab and your choice is saved.",
        "Le bouton EN / FR en haut à droite bascule toute l'interface entre l'anglais et le français. Il est présent sur chaque onglet et ton choix est sauvegardé."},

    {0, "Profiles", "Profils"},
    {1, "A profile stores every setting under a name. Load one to switch your whole setup at once. Edit a loaded profile's name and press Save to rename it in place. They are handy for a per-character or per-job look.",
        "Un profil enregistre tous les réglages sous un nom. Charges-en un pour changer toute ta configuration d'un coup. Modifie le nom d'un profil chargé puis appuie sur Enregistrer pour le renommer. Pratique pour un rendu par personnage ou par job."},
    {1, "The last profile you load is remembered, so when the plugin restarts it automatically comes back on that same profile.",
        "Le dernier profil chargé est mémorisé : au redémarrage du plugin, il revient automatiquement sur ce même profil."},

    {0, "Commands", "Commandes"},
    {2, "*//aio config* opens this window.", "*//aio config* ouvre cette fenêtre."},
    {2, "*//aio edit* moves and resizes the boxes on screen.", "*//aio edit* déplace et redimensionne les cadres."},
    {2, "*//aio profile* save, load, delete or list a named setup.", "*//aio profile* save, load, delete ou list un profil nommé."},
    {2, "*//aio party demo N* previews the *Party* with N members, 1 to 6.", "*//aio party demo N* prévisualise la *Party* avec N membres, de 1 à 6."},
    {2, "*//aio alliance1 demo* and *alliance2 demo* add alliances.", "*//aio alliance1 demo* et *alliance2 demo* ajoutent des alliances."},
    {2, "*//aio demo off* returns to live data.", "*//aio demo off* revient aux données réelles."},
};
static const int HELP_GENERAL_N = (int)(sizeof(HELP_GENERAL) / sizeof(HELP_GENERAL[0]));

// PARTY / ALLIANCE : only what concerns the party + alliance boxes.
static const HelpItem HELP_PA[] = {
    {0, "Party and Alliance", "Party et Alliance"},
    {1, "AioHUD replaces the game's *Party* and *Alliance* windows with up to three boxes, your *Party* plus *Alliance 1* and *Alliance 2*. Each one shows its members with live HP, MP and TP, their job, the leader marks, the distance, and for your own *Party* their status effects. It all moves in real time, and whoever you are targeting stays lit up.",
        "AioHUD remplace les fenêtres *Party* et *Alliance* du jeu par un maximum de trois cadres, ta *Party* plus l'*Alliance 1* et l'*Alliance 2*. Chacun montre ses membres avec leurs HP, MP et TP en direct, leur job, les marques de leader, la distance, et pour ta propre *Party* leurs effets de statut. Tout bouge en temps réel, et la personne que tu cibles reste allumée."},

    {0, "Reading a member", "Lire un membre"},
    {1, "A member's row reads from left to right, starting with the leader dots and the distance, then the job badge, the name, and finally the three gauges.",
        "La ligne d'un membre se lit de gauche à droite, en commençant par les pastilles de leader et la distance, puis le badge de job, le nom, et enfin les trois jauges."},
    {2, "The *name* sits in a clean off-white. It turns *red* when the member is KO, and greys out when they are in another zone.",
        "Le *nom* est en blanc cassé. Il passe au *rouge* quand le membre est KO, et grise quand il est dans une autre zone."},
    {2, "The *job badge* carries the main job on top and the sub below, WAR over NIN for example. It is tinted by the member's *role*, tank, healer, damage or support, so you read the make-up of the group at a glance.",
        "Le *badge de job* porte le job principal en haut et le sous-job en dessous, WAR sur NIN par exemple. Il est teinté selon le *rôle* du membre, tank, healer, damage ou support, pour lire la composition du groupe d'un coup d'œil."},
    {2, "*HP* runs from green when it is high down through yellow and orange to red, and it blinks alarm-red once the member drops to a quarter or less.",
        "Les *HP* vont du vert quand elles sont hautes jusqu'au rouge en passant par le jaune et l'orange, et clignotent en rouge d'alarme dès que le membre tombe à un quart ou moins."},
    {12, "the HP colour, live, blinking at the critical quarter", "la couleur des HP en direct, clignotant au quart critique"},
    {2, "*MP* is blue. *TP* fills as it builds and lights up at 1000, which means a weapon skill is ready.",
        "Les *MP* sont bleus. Les *TP* se remplissent et s'illuminent à 1000, ce qui veut dire qu'une weapon skill est prête."},
    {13, "the TP gauge, live, glowing past 1000", "la jauge de TP en direct, brillant au-delà de 1000"},
    {2, "*Buff icons* for your own *Party*, up to 20 in a row on the left. The game never sends *Alliance* buffs, so alliance members show none.",
        "Des *icônes de buffs* pour ta propre *Party*, jusqu'à 20 en ligne à gauche. Le jeu n'envoie jamais les buffs d'*Alliance*, donc les membres d'alliance n'en ont pas."},

    {0, "Leader and Quartermaster dots", "Pastilles Party Leader, Alliance Leader et Quartermaster"},
    {1, "Three little dots sit at the top of the left column, each in its own fixed slot so they never shuffle around. A slot stays empty when the member does not hold that role, and the dots fade in and out as roles change.",
        "Trois petites pastilles en haut de la colonne de gauche, chacune dans un emplacement fixe pour ne jamais se mélanger. Un emplacement reste vide si le membre n'a pas ce rôle, et les pastilles apparaissent et disparaissent quand les rôles changent."},
    {11, "", ""},

    {0, "Distance", "Distance"},
    {1, "Under the dots, each member shows how far they are from you in yalms, written as 00.00. The colour tells you at a glance whether your spells can reach them.",
        "Sous les pastilles, chaque membre montre à quelle distance il est de toi en yalms, au format 00.00. La couleur te dit d'un coup d'œil si tes sorts peuvent l'atteindre."},
    {10, "under 10 blue, up to 20.8 yellow, beyond that red", "moins de 10 bleu, jusqu'à 20.8 jaune, au-delà rouge"},
    {2, "*Blue* under 10 yalms, in range of everything, cures and the short spells like the AoE -ra line or Majesty.",
        "*Bleu* sous 10 yalms, à portée de tout, cures et sorts courts comme les -ra de zone ou Majesty."},
    {2, "*Yellow* from 10 up to 20.8, still in normal casting range but too far for those short spells.",
        "*Jaune* de 10 à 20.8, encore dans la portée de cast normale mais trop loin pour ces sorts courts."},
    {2, "*Red* at 20.8 and beyond, out of range. A normal cure or buff will fail, and the whole row dims.",
        "*Rouge* à 20.8 et au-delà, hors de portée. Une cure ou un buff normal échouera, et toute la ligne s'assombrit."},
    {1, "You, your trusts, and anyone in another zone show no distance.",
        "Toi, tes trusts et quiconque dans une autre zone n'affichent pas de distance."},

    {0, "Target cursor", "Curseur de cible"},
    {1, "When you target a *Party* or *Alliance* member, a hand appears at the left of their row pointing at them and a soft highlight slides onto it. The cursor follows your target from one member to the next, bobbing gently so it is always easy to spot.",
        "Quand tu cibles un membre de la *Party* ou de l'*Alliance*, une main apparaît à gauche de sa ligne en le pointant et une surbrillance douce glisse dessus. Le curseur suit ta cible de membre en membre, en oscillant légèrement pour rester bien visible."},
    {14, "", ""},
    {1, "The game's own party menu, like Quartermaster or Lottery, also lights up the member it points at, so the box always matches the menu on screen.",
        "Le menu de groupe du jeu, comme Quartermaster ou Lottery, allume aussi le membre qu'il pointe, donc le cadre correspond toujours au menu à l'écran."},

    {0, "Selection highlight", "Surbrillance de sélection"},
    {1, "The highlight is a faint tinted bar with a slow glass sheen sweeping across it, bright enough to frame the member but never enough to hide the gauges. The main target's bar is *gold*, and a sub-target's bar is ocean *blue* and sits on top.",
        "La surbrillance est une barre teintée discrète avec un reflet de verre qui la balaie lentement, assez visible pour encadrer le membre mais jamais au point de masquer les jauges. La barre de la cible principale est *or*, et celle d'une sous-cible est *bleu* océan et passe au-dessus."},

    {0, "Casting and out of range", "Incantation et hors de portée"},
    {1, "While a member casts, the spell name shows in *gold* just under their name, breathing softly. A member beyond casting range sits under a dark veil over their row, a clear can't-reach-them cue. A member who has moved to another zone shows that zone's name in place of the gauges.",
        "Quand un membre lance un sort, le nom du sort s'affiche en *or* juste sous son nom, en pulsant doucement. Un membre hors de portée de cast est recouvert d'un voile sombre sur sa ligne, un signal clair qu'on ne peut pas l'atteindre. Un membre parti dans une autre zone montre le nom de cette zone à la place des jauges."},

    {0, "Adaptive layout", "Disposition adaptative"},
    {1, "The *Party* box is anchored by its lower-right corner. It grows upward as members join and shrinks back as they leave, but the corner you placed never moves. Whatever the member count, it reaches up just far enough to keep the game's own party window hidden behind it, and that top edge is set by the *Party* zones covered further down.",
        "Le cadre *Party* est ancré par son coin bas-droit. Il grandit vers le haut quand des membres arrivent et se réduit quand ils partent, mais le coin que tu as placé ne bouge jamais. Quel que soit le nombre de membres, il remonte juste assez pour garder la fenêtre de groupe native du jeu cachée derrière lui, et ce bord haut est défini par les zones *Party* vues plus bas."},

    {0, "Cost and Next box", "Cadre Coût et Next"},
    {1, "When you open a spell, ability or weapon-skill menu, a small box appears just above the *Party*. It shows the action's name with, depending on the action, its MP cost for spells, its *Next* recast timer for spells and abilities, or your live TP for weapon skills. *Alliance 1* stacks flush on top of this box.",
        "Quand tu ouvres un menu de sort, d'aptitude ou de weapon skill, un petit cadre apparaît juste au-dessus de la *Party*. Il montre le nom de l'action avec, selon le cas, son coût en MP pour les sorts, son délai *Next* pour les sorts et aptitudes, ou tes TP en direct pour les weapon skills. L'*Alliance 1* se cale juste au-dessus de ce cadre."},

    {0, "Configuration", "Configuration"},
    {1, "The Configuration tab sets how the boxes look, with a live preview on the right that follows your changes.",
        "L'onglet Configuration règle l'apparence des cadres, avec un aperçu en direct à droite qui suit tes changements."},
    {2, "*Box Theme* is the FFXI window skin used for every box.",
        "*Box Theme* est l'habillage de fenêtre FFXI utilisé pour tous les cadres."},
    {2, "*Font* is the text face for names, jobs and numbers.",
        "*Font* est la police des noms, jobs et chiffres."},
    {2, "*Party Size* scales the *Party* box from 100 to 200 percent. It never drops below 100 so it always covers the native window. *Ally 1* and *Ally 2* scale on their own, 50 to 200.",
        "*Party Size* met le cadre *Party* à l'échelle de 100 à 200 pour cent. Il ne descend jamais sous 100 pour toujours couvrir la fenêtre native. *Ally 1* et *Ally 2* s'échelonnent seuls, de 50 à 200."},
    {2, "*Buff Size* sets how big the buff icons are, as a share of the row height.",
        "*Buff Size* règle la taille des icônes de buffs, en proportion de la hauteur de ligne."},
    {2, "*Bar Height* and *Bar Width* size the HP, MP and TP gauges. The box grows to fit.",
        "*Bar Height* et *Bar Width* dimensionnent les jauges HP, MP et TP. Le cadre s'agrandit en conséquence."},
    {2, "*Job Badge* is Off, Main job only, or Main plus Sub.",
        "*Job Badge* est Aucun, Job principal seul, ou Principal plus Sub."},
    {2, "*Casts* shows the casting line, toggled apart for *Party* and *Alliance*. *Distance* shows the yalms number, toggled per box.",
        "*Casts* affiche la ligne d'incantation, activable à part pour la *Party* et l'*Alliance*. *Distance* affiche le nombre de yalms, activable par cadre."},
    {2, "*Borders* turns the window frame off per box, *Party*, *Cost*, *Alliance 1* and *Alliance 2*. The background stays, only the frame goes.",
        "*Borders* enlève le cadre de fenêtre par boîte, *Party*, *Cost*, *Alliance 1* et *Alliance 2*. Le fond reste, seule la bordure part."},
    {1, "Whenever a box is resized its lower-right corner stays put, so it grows up and to the left from where you placed it and nothing drifts.",
        "Quand un cadre est redimensionné son coin bas-droit reste en place, il grandit vers le haut et la gauche depuis où tu l'as posé et rien ne dérive."},

    {0, "Move and resize", "Déplacer et redimensionner"},
    {1, "Type //aio edit, or click Edit Layout, to arrange the boxes right on the game screen. Drag a box to move it, and roll the wheel over it to resize, the *Party* from 100 to 200 and the alliances from 50 to 200. Boxes snap to each other's edges as you drag. Default resets every position and size, and Done saves and leaves edit mode.",
        "Tape //aio edit, ou clique Éditer dispo, pour disposer les cadres directement sur l'écran. Glisse un cadre pour le déplacer, et utilise la molette dessus pour redimensionner, la *Party* de 100 à 200 et les alliances de 50 à 200. Les cadres s'aimantent à leurs bords pendant le glissement. Défaut réinitialise toutes les positions et tailles, et Terminé sauvegarde et quitte."},

    {0, "Zones", "Zones"},
    {1, "In edit mode the *Rules* button opens the zone editor. The HUD hides so only your zones and the toolbar show. Zones are rectangles you draw over the screen to say where each box may sit, to line the boxes up on the game's native windows and keep them off spots like the chat log.",
        "En mode édition le bouton *Rules* ouvre l'éditeur de zones. Le HUD se cache pour ne montrer que tes zones et la barre d'outils. Les zones sont des rectangles que tu dessines sur l'écran pour dire où chaque cadre peut se poser, pour aligner les cadres sur les fenêtres natives du jeu et les tenir à l'écart d'endroits comme le journal de chat."},
    {2, "*Draw* a zone by dragging on an empty spot, or click + Zone. Move it by its body, resize it by its corners. A plain click only selects it.",
        "*Dessine* une zone en glissant sur un endroit vide, ou clique + Zone. Déplace-la par son corps, redimensionne-la par ses coins. Un simple clic la sélectionne seulement."},
    {2, "*Name and permissions*, pick a zone then rename it in the panel and choose which boxes may sit on it, *Party*, *Alliance* or *Hub*. A zone that allows nothing is a keep-out, a box dragged onto it is pushed back out.",
        "*Nom et permissions*, choisis une zone puis renomme-la dans le panneau et choisis quels cadres peuvent s'y poser, *Party*, *Alliance* ou *Hub*. Une zone qui n'autorise rien est interdite, un cadre glissé dessus en est repoussé."},
    {2, "*+ Party* creates the six party zones, 1p to 6p. The *Party* box grows up to the zone that matches your member count. *+ Ally* creates the two alliance zones. Drag and resize them onto the native windows.",
        "*+ Party* crée les six zones party, 1p à 6p. Le cadre *Party* remonte jusqu'à la zone qui correspond à ton nombre de membres. *+ Ally* crée les deux zones d'alliance. Glisse-les et redimensionne-les sur les fenêtres natives."},
    {1, "Turn *Rules* off to go back to placing the boxes. Your zones stay active and keep the boxes out of the forbidden spots. The panel is draggable and its place is remembered. Everything is stored as a share of the screen, so a saved layout scales to any resolution, you may just re-align the zones to your own native windows.",
        "Désactive *Rules* pour revenir au placement des cadres. Tes zones restent actives et tiennent les cadres hors des endroits interdits. Le panneau est déplaçable et sa place est mémorisée. Tout est stocké en proportion de l'écran, donc une disposition sauvée s'adapte à toute résolution, il te suffira peut-être de ré-aligner les zones sur tes propres fenêtres natives."},

    {0, "Preview and demo", "Aperçu et démo"},
    {1, "Use //aio party demo N to fill the *Party* with N fake members from 1 to 6, or //aio alliance1 demo and alliance2 demo to add alliances. In demo a target cursor cycles through the members so you watch the highlight move. //aio demo off returns to live data. Demo mirrors the real layout, so you can tune spacing and placement with no live party.",
        "Utilise //aio party demo N pour remplir la *Party* avec N faux membres de 1 à 6, ou //aio alliance1 demo et alliance2 demo pour ajouter des alliances. En démo un curseur de cible défile sur les membres pour voir bouger la surbrillance. //aio demo off revient aux données réelles. La démo reproduit la vraie disposition, pour régler l'espacement et le placement sans groupe réel."},
};
static const int HELP_PA_N = (int)(sizeof(HELP_PA) / sizeof(HELP_PA[0]));

// One help page per module (the Help tab's left menu lists these ; add a module = add a row here).
struct HelpModule { const char* en; const char* fr; const HelpItem* items; int count; };
static const HelpModule HELP_MODULES[] = {
    { "General",          "Général",            HELP_GENERAL, HELP_GENERAL_N },
    { "Party / Alliance", "Party / Alliance",    HELP_PA,      HELP_PA_N },
};
static const int HELP_MODULE_N = (int)(sizeof(HELP_MODULES) / sizeof(HELP_MODULES[0]));

// Draw word-wrapped text from (x,y), returning the new y. Lines whose center is outside [top,bot] are
// skipped -- a cheap clip for the scrolling viewport (D3D8 has no scissor rect).
// Word-wrap with tiny inline markup : *bold* (brighter + a heavier outline) and _highlight_ (gold).
// Drawn word by word so emphasis can change mid-line. Markers are consumed, not shown. STRICT top/bot clip.
static float draw_wrapped(u32 dev, Font* fo, float x, float y, float maxW, float top, float bot,
                          const char* text, float sz, u32 col, float lineH) {
    const float spaceW = fo->measure(" ", sz);
    const char* p = text; char word[160];
    float lineX = x; bool lineStart = true; int emph = 0;   // 0 normal, 1 bold, 2 highlight (carried across words)
    for (;;) {
        while (*p == ' ') ++p;
        while (*p == '*' || *p == '_') { emph = (*p == '*') ? (emph == 1 ? 0 : 1) : (emph == 2 ? 0 : 2); ++p; }
        const int wEmph = emph;
        int wl = 0;
        while (*p && *p != ' ' && wl < 159) {
            if (*p == '*') { emph = (emph == 1 ? 0 : 1); ++p; continue; }
            if (*p == '_') { emph = (emph == 2 ? 0 : 2); ++p; continue; }
            word[wl++] = *p++;
        }
        word[wl] = 0;
        if (wl > 0) {
            const float ww = fo->measure(word, sz);
            if (!lineStart && lineX + spaceW + ww > x + maxW) { y += lineH; lineX = x; lineStart = true; }   // wrap
            if (!lineStart) lineX += spaceW;
            const u32 wc = (wEmph == 1) ? C_TEXT : (wEmph == 2) ? C_GOLD : col;
            if (y >= top && y + lineH <= bot) { fo->begin(dev); fo->draw_lc(dev, lineX, y + lineH * 0.5f, word, sz, fa(wc), fa(C_STROKE), wEmph == 1 ? 1.9f : 1.0f); }
            lineX += ww; lineStart = false;
        }
        if (!*p) break;
    }
    return y + lineH;
}

void ConfigPage::draw(const Frame& f, float sw, float sh) {
    pvOn_ = false;   // live-preview anchor : off unless we reach the Configuration tab below
    if (!open_) return;
    if (!profSynced_) { strncpy(activeProf_, active_profile_name(), sizeof(activeProf_) - 1); activeProf_[sizeof(activeProf_) - 1] = 0; profSynced_ = true; }   // reflect the startup auto-loaded profile
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
        // ZONES : while Rules is OFF (placing boxes) draw each zone FAINTLY so you see the keep-out / allowed
        // areas -- a box is pushed out of any zone that forbids it (see guide_push_out). Rules ON draws them
        // interactively further below.
        if (!editShowLines_) {
            static const u32 ZC[8] = { 0xFFFF6E6E,0xFFFF9E50,0xFFEFD24A,0xFF7ED86A,0xFF4AC8E0,0xFF6E8CFF,0xFFC090FF,0xFFFF8AD8 };
            float zx[GUIDE_GROUPS_MAX], zy[GUIDE_GROUPS_MAX], zw[GUIDE_GROUPS_MAX], zh[GUIDE_GROUPS_MAX]; int zg[GUIDE_GROUPS_MAX];
            const int nz = guide_zones(sw, sh, zx, zy, zw, zh, zg, GUIDE_GROUPS_MAX);
            for (int i = 0; i < nz; ++i) {
                const GuideGroup& g = ui_config().guideGroup[zg[i]];
                const bool openZone = g.allow[ZPERM_PARTY] || g.allow[ZPERM_ALLIANCE] || g.allow[ZPERM_HUB];
                const u32 col = ZC[zg[i] % 8];
                flat(dev, zx[i], zy[i], zw[i], zh[i], (col & 0x00FFFFFF) | (openZone ? 0x28000000 : 0x40000000));   // fill (forbidden = a touch denser)
                outline(dev, zx[i], zy[i], zw[i], zh[i], col);
                char zl[28]; if (g.name[0]) sprintf(zl, "%s", g.name); else sprintf(zl, tr("Zone %d", "Zone %d"), zg[i] + 1);
                fo->begin(dev); fo->draw_lc(dev, zx[i] + snap(6.0f), zy[i] + snap(10.0f), zl, snap(11.0f), col, C_STROKE, 1.2f);
            }
        }
        // RULES mode : the HUD is hidden (hud.cpp) so only the guide ZONES + the toolbar show. Party 1p-6p and
        // Alliance 1/2 are real zones now (role-tagged) -- there is no separate reference-line handle system.
        if (editShowLines_) {
        UiConfig& C = ui_config();
        // ZONES panel geometry (auto-sized) computed FIRST so EVERY draggable element under it (party-ref
        // handles, zone handles) can ignore clicks that land on the panel -- else one click hits both.
        const char* pTitle = tr("ZONES  (drag on screen to draw)", "ZONES  (dessiner à l'écran)");
        const char* pHint  = tr("Drag on empty space to draw a zone.", "Glissez sur l'écran pour dessiner une zone.");
        const float tbh = snap(28.0f), rowH = snap(22.0f), stride = rowH + snap(3.0f);
        const bool  renaming = (editZoneName_ >= 0 && editZoneName_ < C.guideGroupCount);
        const bool  zoneSel = (groupSel_ >= 0 && groupSel_ < C.guideGroupCount);
        const float actionsH = renaming ? snap(44.0f) : (zoneSel ? snap(66.0f) : snap(26.0f));
        const int   visRows = C.guideGroupCount < 12 ? C.guideGroupCount : 12;
        const float listH = (C.guideGroupCount > 0) ? visRows * stride : 0.0f;
        const float newBtnRow = snap(38.0f);
        float pw = fo->measure(pTitle, snap(11.0f)) + snap(28.0f);
        { const float hw2 = fo->measure(pHint, snap(11.0f)) + snap(24.0f); if (hw2 > pw) pw = hw2; }
        if (pw < snap(200.0f)) pw = snap(200.0f); if (pw > snap(420.0f)) pw = snap(420.0f); pw = snap(pw);
        const float ph = snap(tbh + newBtnRow + listH + snap(6.0f) + actionsH + snap(8.0f));
        float pxp = (C.zonePanelX >= 0.0f) ? snap(C.zonePanelX * sw) : snap(sw - pw - 20.0f);
        float pyp = (C.zonePanelY >= 0.0f) ? snap(C.zonePanelY * sh) : snap(80.0f);
        if (pxp > sw - pw) pxp = snap(sw - pw); if (pxp < 0.0f) pxp = 0.0f;
        if (pyp > sh - ph) pyp = snap(sh - ph); if (pyp < 0.0f) pyp = 0.0f;
        const bool overPanel = inrect(mo, pxp, pyp, pw, ph);

        // ===== USER-DRAWN ZONES : drag on empty space to draw a rectangle (creates a zone). Move it by its
        // body, resize it by its corners, name it, set which boxes may sit on it. A box is pushed OUT of any
        // zone that forbids it (guide_push_out). Rectangular by construction. The panel is draggable. =====
        {
            const bool rClick = click && editConfirm_ == 0;
            static const u32 GRP_COL[8] = { 0xFFFF6E6E,0xFFFF9E50,0xFFEFD24A,0xFF7ED86A,0xFF4AC8E0,0xFF6E8CFF,0xFFC090FF,0xFFFF8AD8 };

            // inline RENAME : mirror the keyboard field into the zone's name each frame ; finish on OK/blur.
            if (editZoneName_ >= 0 && editZoneName_ < C.guideGroupCount) {
                strncpy(C.guideGroup[editZoneName_].name, nameBuf_, 18); C.guideGroup[editZoneName_].name[18] = 0;
                if (kbCommit_ || !nameFocus_) { kbCommit_ = false; editZoneName_ = -1; nameFocus_ = false; save_ui_config(); }
            }

            // on-screen HOW-TO banner (top-centre) so the drag-to-draw gesture is discoverable without reading the panel.
            {
                const char* bn = tr("Drag on an empty area to draw a zone   -   move by its body, resize by its corners",
                                    "Glissez sur une zone vide pour dessiner   -   déplacez par le corps, coins pour redimensionner");
                const float bnw = fo->measure(bn, snap(13.0f)) + snap(30.0f), bnx = snap((sw - bnw) * 0.5f), bny = snap(sh * 0.12f), bnh = snap(30.0f);
                drop_shadow(dev, bnx, bny, bnw, bnh, snap(4.0f), 50);
                vg(dev, bnx, bny, bnw, bnh, 0xE0202B3C, 0xE0141C28);
                flat(dev, bnx, bny, bnw, 1, 0x55FFFFFF); outline(dev, bnx, bny, bnw, bnh, C_BORDERHI);
                fo->begin(dev); fo->draw_c(dev, sw * 0.5f, bny + bnh * 0.5f, bn, snap(13.0f), C_TEXT, C_STROKE, 1.0f);
            }

            // ---- draw + edit each ZONE rectangle on screen ----
            const float HS = snap(9.0f);   // corner-handle half-size
            static int grabZone = -1, grabMode = 0;   // grabMode : 0 move ; 1..4 = TL/TR/BL/BR corner resize
            static float gzOffX = 0.0f, gzOffY = 0.0f, grabPX = 0.0f, grabPY = 0.0f; static bool grabMoved = false;
            if (!(mo && mo->down)) { if (grabZone >= 0 && grabMoved) save_ui_config(); grabZone = -1; grabMoved = false; }
            for (int i = 0; i < C.guideGroupCount; ++i) {
                GuideGroup& z = C.guideGroup[i];
                const float rx = snap(z.x * sw), ry = snap(z.y * sh), rw = snap(z.w * sw), rh = snap(z.h * sh);
                const bool sel = (groupSel_ == i);
                const u32  col = GRP_COL[i % 8];
                const bool openZone = z.allow[0] || z.allow[1] || z.allow[2];
                flat(dev, rx, ry, rw, rh, (col & 0x00FFFFFF) | (openZone ? 0x40000000 : 0x60000000));   // less transparent fill
                outline(dev, rx, ry, rw, rh, sel ? 0xFFFFE59E : col);
                if (sel) outline(dev, rx + snap(1.0f), ry + snap(1.0f), rw - snap(2.0f), rh - snap(2.0f), (col & 0x00FFFFFF) | 0x88000000);
                if (sel) {   // corner handles for the selected zone
                    const float cxs[4] = { rx, rx + rw, rx, rx + rw }, cys[4] = { ry, ry, ry + rh, ry + rh };
                    for (int cci = 0; cci < 4; ++cci) { vg(dev, cxs[cci] - HS, cys[cci] - HS, 2 * HS, 2 * HS, 0xFFFFE59E, 0xFFD9B24A); outline(dev, cxs[cci] - HS, cys[cci] - HS, 2 * HS, 2 * HS, C_STROKE); }
                }
                char zl[28]; if (z.name[0]) sprintf(zl, "%s", z.name); else sprintf(zl, tr("Zone %d", "Zone %d"), i + 1);
                fo->begin(dev); fo->draw_lc(dev, rx + snap(6.0f), ry + snap(11.0f), zl, snap(11.0f), sel ? 0xFFFFFFFF : col, C_STROKE, 1.2f);
            }
            // start a grab on a FRESH press : selected zone's corner -> body of any zone -> else rubber-band draw.
            static bool zPrevDown = false; const bool freshPress = mo && mo->down && !zPrevDown; zPrevDown = mo && mo->down;
            if (freshPress && grabZone < 0 && !zoneDrawing_ && editConfirm_ == 0 && !overPanel) {
                bool got = false;
                if (groupSel_ >= 0 && groupSel_ < C.guideGroupCount) {
                    GuideGroup& z = C.guideGroup[groupSel_];
                    const float rx = z.x * sw, ry = z.y * sh, rw = z.w * sw, rh = z.h * sh;
                    const float cxs[4] = { rx, rx + rw, rx, rx + rw }, cys[4] = { ry, ry, ry + rh, ry + rh };
                    for (int cci = 0; cci < 4 && !got; ++cci) if (inrect(mo, cxs[cci] - HS, cys[cci] - HS, 2 * HS, 2 * HS)) { grabZone = groupSel_; grabMode = 1 + cci; grabPX = mo->x; grabPY = mo->y; grabMoved = false; got = true; }
                }
                if (!got) for (int i = C.guideGroupCount - 1; i >= 0 && !got; --i) {
                    GuideGroup& z = C.guideGroup[i];
                    if (inrect(mo, z.x * sw, z.y * sh, z.w * sw, z.h * sh)) { grabZone = i; grabMode = 0; groupSel_ = i; gzOffX = mo->x - z.x * sw; gzOffY = mo->y - z.y * sh; grabPX = mo->x; grabPY = mo->y; grabMoved = false; got = true; }
                }
                if (!got) { zoneDrawing_ = true; zoneDrawX_ = mo->x; zoneDrawY_ = mo->y; }
            }
            if (grabZone >= 0 && grabZone < C.guideGroupCount && mo && !grabMoved) {   // 4px dead-zone : a plain click only SELECTS
                const float dx = mo->x - grabPX, dy = mo->y - grabPY; if (dx * dx + dy * dy > 16.0f) grabMoved = true;
            }
            if (grabZone >= 0 && grabZone < C.guideGroupCount && mo && grabMoved) {   // apply an active move / corner resize
                GuideGroup& z = C.guideGroup[grabZone];
                if (grabMode == 0) { z.x = clampf((mo->x - gzOffX) / sw, 0.0f, 1.0f - z.w); z.y = clampf((mo->y - gzOffY) / sh, 0.0f, 1.0f - z.h); }
                else {
                    float x0 = z.x, y0 = z.y, x1 = z.x + z.w, y1 = z.y + z.h;
                    const float mx = clampf(mo->x / sw, 0.0f, 1.0f), my = clampf(mo->y / sh, 0.0f, 1.0f);
                    if (grabMode == 1) { x0 = mx; y0 = my; } else if (grabMode == 2) { x1 = mx; y0 = my; } else if (grabMode == 3) { x0 = mx; y1 = my; } else { x1 = mx; y1 = my; }
                    if (x1 < x0) { float t = x0; x0 = x1; x1 = t; grabMode = (grabMode == 1) ? 2 : (grabMode == 2) ? 1 : (grabMode == 3) ? 4 : 3; }
                    if (y1 < y0) { float t = y0; y0 = y1; y1 = t; grabMode = (grabMode <= 2) ? grabMode + 2 : grabMode - 2; }
                    z.x = x0; z.y = y0; z.w = (x1 - x0 < 0.01f) ? 0.01f : (x1 - x0); z.h = (y1 - y0 < 0.01f) ? 0.01f : (y1 - y0);
                }
            }
            if (zoneDrawing_ && mo) {   // rubber-band : show the pending rect ; on release create the zone
                const float x0 = zoneDrawX_ < mo->x ? zoneDrawX_ : mo->x, y0 = zoneDrawY_ < mo->y ? zoneDrawY_ : mo->y;
                const float x1 = zoneDrawX_ > mo->x ? zoneDrawX_ : mo->x, y1 = zoneDrawY_ > mo->y ? zoneDrawY_ : mo->y;
                if (mo->down) {
                    flat(dev, x0, y0, x1 - x0, y1 - y0, 0x3340C0FF); outline(dev, x0, y0, x1 - x0, y1 - y0, 0xFFBFE0FF);
                    char d[28]; sprintf(d, tr("New zone  %dx%d", "Zone  %dx%d"), (int)(x1 - x0), (int)(y1 - y0));
                    fo->begin(dev); fo->draw_c(dev, (x0 + x1) * 0.5f, (y0 + y1) * 0.5f, d, snap(12.0f), 0xFFFFFFFF, C_STROKE, 1.4f);
                }
                else {
                    zoneDrawing_ = false;
                    if ((x1 - x0) > snap(16.0f) && (y1 - y0) > snap(16.0f) && C.guideGroupCount < GUIDE_GROUPS_MAX) {
                        GuideGroup z; z.x = x0 / sw; z.y = y0 / sh; z.w = (x1 - x0) / sw; z.h = (y1 - y0) / sh;
                        C.guideGroup[C.guideGroupCount] = z; groupSel_ = C.guideGroupCount++; save_ui_config();
                    }
                }
            }

            // ---- draggable PANEL : move it FIRST (so background + content share one position), then draw ----
            static float panDX = 0.0f, panDY = 0.0f; static int grabPan = 0;
            const bool tbHov = inrect(mo, pxp, pyp, pw, tbh);
            if (mo && mo->down && !grabPan && tbHov && editConfirm_ == 0 && grabZone < 0 && !zoneDrawing_) { grabPan = 1; panDX = mo->x - pxp; panDY = mo->y - pyp; }
            if (!(mo && mo->down)) { if (grabPan) save_ui_config(); grabPan = 0; }   // persist the panel position on drop
            if (grabPan && mo) {
                pxp = snap(mo->x - panDX); pyp = snap(mo->y - panDY);
                if (pxp > sw - pw) pxp = snap(sw - pw); if (pxp < 0.0f) pxp = 0.0f;
                if (pyp > sh - ph) pyp = snap(sh - ph); if (pyp < 0.0f) pyp = 0.0f;
                C.zonePanelX = pxp / sw; C.zonePanelY = pyp / sh;   // store the CLAMPED position -> stays == what's drawn
            }
            drop_shadow(dev, pxp, pyp, pw, ph, snap(6.0f), 74);
            vg(dev, pxp, pyp, pw, ph, 0xF01C2636, 0xF0121A26);
            outline(dev, pxp, pyp, pw, ph, C_BORDERHI);
            vg(dev, pxp, pyp, pw, tbh, (tbHov || grabPan) ? 0xF02C3A50 : 0xF0243044, 0xF01A2434);
            flat(dev, pxp, pyp, pw, 1, 0x55FFFFFF);
            fo->begin(dev); fo->draw_lc(dev, pxp + snap(12.0f), pyp + tbh * 0.5f, pTitle, snap(11.0f), C_GOLD, C_STROKE, 1.0f);

            const float px0 = pxp + snap(10.0f), pxw = pw - snap(20.0f);
            // + Zone (plain) | + Party (6 party-size zones, role 1..6) | + Ally (2 alliance zones, role 7/8).
            const float nbY = pyp + tbh + snap(6.0f), nbH = snap(24.0f), nbT = (pxw - snap(12.0f)) / 3.0f;
            const float Lx = (C.partyRefX[0] >= 0.0f ? C.partyRefX[0] : 0.42f), Rx = (C.partyRefX[1] >= 0.0f ? C.partyRefX[1] : 0.56f);
            const float x0f = Lx < Rx ? Lx : Rx, wf = Lx < Rx ? (Rx - Lx) : (Lx - Rx);
            if (push_btn(dev, fo, mo, rClick, 933, px0, nbY, nbT, nbH, tr("+ Zone", "+ Zone"), 0) && C.guideGroupCount < GUIDE_GROUPS_MAX) {
                GuideGroup z; z.x = 0.42f; z.y = 0.42f; z.w = 0.16f; z.h = 0.14f; C.guideGroup[C.guideGroupCount] = z; groupSel_ = C.guideGroupCount++; save_ui_config();
            }
            if (push_btn(dev, fo, mo, rClick, 935, px0 + nbT + snap(6.0f), nbY, nbT, nbH, tr("+ Party", "+ Party"), 0)) {
                const float By = (C.partyBottomY >= 0.0f ? C.partyBottomY : 0.95f);
                bool made = false;
                for (int i = 0; i < 6 && C.guideGroupCount < GUIDE_GROUPS_MAX; ++i) {   // create any MISSING count 1p..6p
                    bool has = false; for (int g = 0; g < C.guideGroupCount; ++g) if (C.guideGroup[g].role == i + 1) has = true;
                    if (has) continue;
                    // the TOP is specific per count ; use the stored line if it's a valid top (above the bottom),
                    // else rebuild it -- interpolate from the neighbours (3p<->5p for 4p), or fall back to spacing.
                    float top = C.partyRef[i];
                    if (!(top >= 0.0f && top < By - 0.03f)) {
                        // rebuild : interpolate from the neighbour COUNT zones (role i and i+2, i.e. counts i and i+2)
                        // -- their CURRENT tops -- falling back to partyRef, then to default spacing.
                        float lo = -1.0f, hi = -1.0f;
                        for (int g = 0; g < C.guideGroupCount; ++g) {
                            if (C.guideGroup[g].role == i)          lo = C.guideGroup[g].y;
                            else if (C.guideGroup[g].role == i + 2) hi = C.guideGroup[g].y;
                        }
                        if (lo < 0.0f && i > 0 && C.partyRef[i - 1] >= 0.0f && C.partyRef[i - 1] < By) lo = C.partyRef[i - 1];
                        if (hi < 0.0f && i < 5 && C.partyRef[i + 1] >= 0.0f && C.partyRef[i + 1] < By) hi = C.partyRef[i + 1];
                        if (lo >= 0.0f && hi >= 0.0f) top = (lo + hi) * 0.5f;
                        else if (lo >= 0.0f)          top = lo - 0.015f;
                        else if (hi >= 0.0f)          top = hi + 0.015f;
                        else                          top = 0.90f - 0.0145f * (float)i;
                    }
                    if (top >= By) top = By - 0.05f; if (top < 0.0f) top = 0.0f;
                    GuideGroup z; z.x = x0f; z.y = top; z.w = wf; z.h = By - top; z.role = i + 1; z.allow[ZPERM_PARTY] = true;
                    sprintf(z.name, tr("Party %dp", "Groupe %dp"), i + 1);
                    C.guideGroup[C.guideGroupCount++] = z; made = true;
                }
                if (made) { groupSel_ = C.guideGroupCount - 1; save_ui_config(); }
            }
            if (push_btn(dev, fo, mo, rClick, 936, px0 + 2 * nbT + snap(12.0f), nbY, nbT, nbH, tr("+ Ally", "+ Ally"), 0)) {
                const float adef[4] = { 0.34f, 0.42f, 0.48f, 0.56f };   // A1 top/bottom, A2 top/bottom (defaults)
                bool made = false;
                for (int a = 0; a < 2 && C.guideGroupCount < GUIDE_GROUPS_MAX; ++a) {   // role 7 = Alliance 1, role 8 = Alliance 2
                    const int rl = 7 + a;
                    bool has = false; for (int g = 0; g < C.guideGroupCount; ++g) if (C.guideGroup[g].role == rl) has = true;
                    if (has) continue;
                    float top = (C.allyRefY[a * 2] >= 0.0f ? C.allyRefY[a * 2] : adef[a * 2]);
                    float bot = (C.allyRefY[a * 2 + 1] >= 0.0f ? C.allyRefY[a * 2 + 1] : adef[a * 2 + 1]);
                    if (bot < top) { const float t = top; top = bot; bot = t; }
                    if (bot - top < 0.03f) bot = top + 0.03f;
                    GuideGroup z; z.x = x0f; z.y = top; z.w = wf; z.h = bot - top; z.role = rl; z.allow[ZPERM_ALLIANCE] = true;
                    sprintf(z.name, tr("Alliance %d", "Alliance %d"), a + 1);
                    C.guideGroup[C.guideGroupCount++] = z; made = true;
                }
                if (made) { groupSel_ = C.guideGroupCount - 1; save_ui_config(); }
            }
            const float listTop = pyp + tbh + newBtnRow, listBot = pyp + ph - actionsH, visH = listBot - listTop;
            const float total = C.guideGroupCount * stride, maxScroll = (total > visH) ? (total - visH) : 0.0f;
            const bool  sbVisible = maxScroll > 0.0f;
            const float listRight = px0 + pxw - (sbVisible ? snap(10.0f) : 0.0f);
            if (overPanel && ui_config().wheel != 0) { guideScroll_ -= (float)ui_config().wheel * stride * 3.0f; ui_config().wheel = 0; }
            if (guideScroll_ < 0.0f) guideScroll_ = 0.0f; if (guideScroll_ > maxScroll) guideScroll_ = maxScroll;
            float ly = listTop - guideScroll_;
            for (int i = 0; i < C.guideGroupCount; ++i, ly += stride) {
                if (ly < listTop || ly + rowH > listBot) continue;
                GuideGroup& z = C.guideGroup[i]; const bool sel = (groupSel_ == i);
                const float rw2 = listRight - px0; const bool hov = inrect(mo, px0, ly, rw2, rowH);
                vg(dev, px0, ly, rw2, rowH, sel ? 0xFF2C5AA0 : (hov ? 0xF02A3548 : 0xF01E2838), sel ? 0xFF20447E : 0xF0161E2C);
                outline(dev, px0, ly, rw2, rowH, sel ? C_ACCENTHI : C_BORDER);
                flat(dev, px0 + snap(4.0f), ly + rowH * 0.5f - snap(5.0f), snap(6.0f), snap(10.0f), GRP_COL[i % 8]);
                char zl[28]; if (z.name[0]) sprintf(zl, "%s", z.name); else sprintf(zl, tr("Zone %d", "Zone %d"), i + 1);
                fo->begin(dev); fo->draw_lc(dev, px0 + snap(16.0f), ly + rowH * 0.5f, zl, snap(11.0f), sel ? 0xFFFFFFFF : C_TEXT, C_STROKE, 1.0f);
                if (hov && rClick) groupSel_ = i;
            }
            if (sbVisible) {
                const float sbw = snap(6.0f), sbx = px0 + pxw - sbw;
                float thumbH = visH * visH / total; if (thumbH < snap(24.0f)) thumbH = snap(24.0f);
                const float thumbY = listTop + ((maxScroll > 0.0f) ? guideScroll_ / maxScroll : 0.0f) * (visH - thumbH);
                flat(dev, sbx, listTop, sbw, visH, 0x50101720);
                static int grabSB = 0; static float sbOff = 0.0f;
                const bool thHov = inrect(mo, sbx - snap(2.0f), thumbY, sbw + snap(4.0f), thumbH);
                if (mo && mo->down && !grabSB && thHov && editConfirm_ == 0) { grabSB = 1; sbOff = mo->y - thumbY; }
                if (!(mo && mo->down)) grabSB = 0;
                if (grabSB && mo) { float t = (visH - thumbH > 0.0f) ? (mo->y - sbOff - listTop) / (visH - thumbH) : 0.0f; t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t); guideScroll_ = t * maxScroll; }
                vg(dev, sbx, thumbY, sbw, thumbH, (thHov || grabSB) ? 0xFF5A8FE0 : 0xFF3A506E, (thHov || grabSB) ? 0xFF3A6FC0 : 0xFF283A52);
                outline(dev, sbx, thumbY, sbw, thumbH, 0x80FFFFFF);
            }

            float ay = listBot + snap(8.0f);
            if (renaming) {   // real inline TEXT FIELD (caret + Left/Right/Home/End/Delete via the key hook) + OK button -- no Enter needed
                const float fH = snap(30.0f), okW = snap(58.0f), fGap = snap(8.0f), fW = pxw - okW - fGap;
                rpanel(dev, px0, ay, fW, fH, snap(6.0f), 0xE6080C14, 0xE605080F, C_ACCENT, snap(1.5f));
                const float txY = ay + fH * 0.5f, txX = px0 + snap(10.0f);
                fo->begin(dev);
                if (nameLen_ > 0) fo->draw_lc(dev, txX, txY, nameBuf_, snap(13.0f), C_TEXT, C_STROKE, 1.0f);
                else              fo->draw_lc(dev, txX, txY, tr("Zone name...", "Nom de la zone..."), snap(12.0f), C_MUTE, C_STROKE, 1.0f);
                if (sinf(f.t * 5.0f) > 0.0f) {   // blinking caret AT the cursor index
                    int cn = nameCur_ < 0 ? 0 : (nameCur_ > nameLen_ ? nameLen_ : nameCur_);
                    char pre[32]; memcpy(pre, nameBuf_, cn); pre[cn] = 0;
                    const float cxx = txX + (cn > 0 ? fo->measure(pre, snap(13.0f)) : 0.0f) + snap(1.0f);
                    flat(dev, cxx, ay + snap(7.0f), snap(2.0f), fH - snap(14.0f), C_ACCENTHI);
                }
                if (push_btn(dev, fo, mo, rClick, 934, px0 + fW + fGap, ay, okW, fH, tr("OK", "OK"), 0)) { editZoneName_ = -1; nameFocus_ = false; save_ui_config(); }
            } else if (groupSel_ >= 0 && groupSel_ < C.guideGroupCount) {   // selected ZONE : Rename / Delete + permissions
                GuideGroup& z = C.guideGroup[groupSel_];
                const float halfg = (pxw - snap(8.0f)) * 0.5f;
                if (push_btn(dev, fo, mo, rClick, 931, px0, ay, halfg, snap(24.0f), tr("Rename", "Renom."), 0)) {
                    editZoneName_ = groupSel_; nameFocus_ = true;
                    strncpy(nameBuf_, z.name, sizeof(nameBuf_) - 1); nameBuf_[sizeof(nameBuf_) - 1] = 0; nameLen_ = (int)strlen(nameBuf_); nameCur_ = nameLen_;
                }
                if (push_btn(dev, fo, mo, rClick, 932, px0 + halfg + snap(8.0f), ay, halfg, snap(24.0f), tr("Delete", "Suppr."), 1)) {
                    for (int k = groupSel_; k < C.guideGroupCount - 1; ++k) C.guideGroup[k] = C.guideGroup[k + 1];
                    --C.guideGroupCount; groupSel_ = -1; editZoneName_ = -1; nameFocus_ = false; save_ui_config();
                }
                ay += snap(30.0f);
                const char* pl[ZPERM_COUNT] = { tr("Party", "Groupe"), tr("Alliance", "Alliance"), tr("Hub", "Hub") };
                const float cwp = (pxw - snap(12.0f)) / 3.0f;
                for (int k = 0; k < ZPERM_COUNT && groupSel_ >= 0 && groupSel_ < C.guideGroupCount; ++k) {
                    const float cxp = px0 + k * (cwp + snap(6.0f)); const bool on = z.allow[k];
                    const bool hov = inrect(mo, cxp, ay, cwp, snap(24.0f));
                    vg(dev, cxp, ay, cwp, snap(24.0f), on ? 0xFF2E7A44 : (hov ? 0xF02A3548 : 0xF01E2838), on ? 0xFF1F5730 : 0xF0161E2C);
                    outline(dev, cxp, ay, cwp, snap(24.0f), on ? 0xFF5ADC8A : C_BORDER);
                    fo->begin(dev); fo->draw_c(dev, cxp + cwp * 0.5f, ay + snap(12.0f), pl[k], snap(10.0f), on ? 0xFFFFFFFF : C_MUTE, C_STROKE, 1.0f);
                    if (hov && rClick) { z.allow[k] = !z.allow[k]; save_ui_config(); }
                }
            } else {
                fo->begin(dev); fo->draw_lc(dev, px0, ay + snap(12.0f), pHint, snap(11.0f), C_MUTE, C_STROKE, 1.0f);
            }
        }
        }   // end if (editShowLines_)
        const float bw = snap(820.0f), bh = snap(46.0f), bx = snap((sw - bw) * 0.5f), by = snap(22.0f);
        const float pop = ease(900, 1.0f, 16.0f);                                   // subtle slide-in
        const float byA = by - (1.0f - pop) * snap(10.0f);
        shadow_down(dev, bx - snap(4.0f), byA + bh, bw + snap(8.0f), snap(10.0f), 0x55000000);
        vg(dev, bx, byA, bw, bh, 0xF0202B3C, 0xF0141C28);
        flat(dev, bx, byA, bw, 1, 0x55FFFFFF);                                       // top sheen
        outline(dev, bx, byA, bw, bh, C_BORDERHI);
        fo->begin(dev);
        fo->draw_lc(dev, bx + snap(16.0f), byA + bh * 0.5f, tr("EDIT LAYOUT  |  drag,  wheel = size", "ÉDITION  |  glisser,  molette = taille"), snap(14.0f), C_TEXT, C_STROKE, 1.0f);
        const bool tbClick = click && editConfirm_ == 0;   // while the confirm modal is up the toolbar is inert
        const float bh2 = snap(30.0f), dby = byA + (bh - bh2) * 0.5f;
        const float db = snap(80.0f),  dbx = bx + bw - db - snap(10.0f);            // Done (far right)
        const float rb = snap(90.0f),  rbx = dbx - rb - snap(8.0f);                 // Default
        const float sb = snap(120.0f), sbx = rbx - sb - snap(8.0f);                 // Clear lines
        const float lb = snap(104.0f), lbx = sbx - lb - snap(8.0f);                 // Rules on/off toggle
        // RULES toggle : show / hide the reference lines. While ON the HUD boxes are hidden (hud.cpp) so only
        // the rules + this toolbar show -- align each line onto the game's native window, then toggle OFF to
        // go back to dragging/resizing the boxes.
        {
            const bool hov = inrect(mo, lbx, dby, lb, bh2);
            const float t = ease(904, (editShowLines_ || hov) ? 1.0f : 0.0f);
            halo(dev, lbx, dby, lb, bh2, C_ACCENT, t * 0.8f);
            vg(dev, lbx, dby, lb, bh2, lerpc(0xFF2A3548, 0xFF3A82E0, t), lerpc(0xFF1D2738, 0xFF2A61B6, t));
            outline(dev, lbx, dby, lb, bh2, lerpc(C_BORDERHI, C_ACCENTHI, t));
            fo->begin(dev); fo->draw_c(dev, lbx + lb * 0.5f, dby + bh2 * 0.5f,
                                       editShowLines_ ? tr("Rules: ON", "Règles: ON") : tr("Rules: OFF", "Règles: OFF"),
                                       snap(13.0f), C_TEXT, C_STROKE, 1.0f);
            if (hov && tbClick) editShowLines_ = !editShowLines_;
        }
        // Clear lines : wipe every reference line back to default -> asks to confirm first.
        bool refSet = ui_config().partyBottomY >= 0.0f || ui_config().partyRefX[0] >= 0.0f || ui_config().partyRefX[1] >= 0.0f;
        for (int i = 0; i < 6; ++i) if (ui_config().partyRef[i] >= 0.0f) refSet = true;
        for (int i = 0; i < 4; ++i) if (ui_config().allyRefY[i] >= 0.0f) refSet = true;
        {
            const bool shv = inrect(mo, sbx, dby, sb, bh2) && refSet;
            const float t = ease(901, shv ? 1.0f : 0.0f);
            halo(dev, sbx, dby, sb, bh2, C_ACCENT, t * 0.8f);
            vg(dev, sbx, dby, sb, bh2, lerpc(0xFF2A3548, 0xFF3A82E0, t), lerpc(0xFF1D2738, 0xFF2A61B6, t));
            outline(dev, sbx, dby, sb, bh2, lerpc(C_BORDERHI, C_ACCENTHI, t));
            fo->begin(dev); fo->draw_c(dev, sbx + sb * 0.5f, dby + bh2 * 0.5f, tr("Clear lines", "Effacer lignes"), snap(13.0f), refSet ? C_TEXT : C_MUTE, C_STROKE, 1.0f);
            if (shv && tbClick) editConfirm_ = 1;   // -> confirmation modal
        }
        if (push_btn(dev, fo, mo, tbClick, 902, rbx, dby, rb, bh2, tr("Default", "Défaut"), 1)) editConfirm_ = 2;   // -> confirm
        if (push_btn(dev, fo, mo, tbClick, 903, dbx, dby, db, bh2, tr("Done", "Terminé"), 0)) { ui_config().editLayout = false; editShowLines_ = false; editZoneName_ = -1; nameFocus_ = false; groupSel_ = -1; save_ui_config(); }

        // CONFIRMATION dialog for the destructive actions (Clear lines / Default), drawn on top and capturing input.
        if (editConfirm_ != 0) {
            flat(dev, 0.0f, 0.0f, sw, sh, 0x99000000);                              // dim the screen behind the dialog
            const float mw = snap(440.0f), mh = snap(170.0f), mx = snap((sw - mw) * 0.5f), my = snap((sh - mh) * 0.5f);
            shadow_down(dev, mx - snap(4.0f), my + mh, mw + snap(8.0f), snap(12.0f), 0x66000000);
            vg(dev, mx, my, mw, mh, 0xF0202B3C, 0xF0141C28);
            flat(dev, mx, my, mw, 1, 0x55FFFFFF);                                    // top sheen
            outline(dev, mx, my, mw, mh, C_BORDERHI);
            fo->begin(dev); fo->draw_c(dev, mx + mw * 0.5f, my + snap(34.0f), tr("Please confirm", "Confirmation"), snap(13.0f), C_GOLD, C_STROKE, 1.2f);
            const char* msg = (editConfirm_ == 1) ? tr("Clear all reference lines?", "Effacer toutes les lignes de repère ?")
                                                  : tr("Reset all boxes to default?", "Réinitialiser toutes les boîtes ?");
            fo->begin(dev); fo->draw_c(dev, mx + mw * 0.5f, my + snap(74.0f), msg, snap(15.0f), C_TEXT, C_STROKE, 1.0f);
            const float pbw = snap(160.0f), pbh = snap(34.0f), pby = my + mh - pbh - snap(20.0f);
            const float clx = mx + snap(30.0f), crx = mx + mw - pbw - snap(30.0f);
            if (push_btn(dev, fo, mo, click, 910, clx, pby, pbw, pbh, tr("Cancel", "Annuler"), 0)) editConfirm_ = 0;
            if (push_btn(dev, fo, mo, click, 911, crx, pby, pbw, pbh, tr("Confirm", "Confirmer"), 1)) {
                if (editConfirm_ == 1) {   // clear every reference line
                    for (int i = 0; i < 6; ++i) ui_config().partyRef[i] = -1.0f;
                    for (int i = 0; i < 4; ++i) ui_config().allyRefY[i] = -1.0f;
                    ui_config().partyBottomY = -1.0f; ui_config().partyRefX[0] = ui_config().partyRefX[1] = -1.0f;
                    save_ui_config();
                } else if (editConfirm_ == 2) {   // reset every box position + size
                    reset_boxes();
                }
                editConfirm_ = 0;
            }
        }
        // (no custom cursor : the game/OS already shows one -> avoid a double pointer)
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
    if (cbHov && click) { open_ = false; return; }

    // language toggle (EN | FR), left of the close X -- drawn in the shared header, so it's on EVERY tab
    {
        const float lh = snap(26.0f), segW = snap(34.0f), lw = segW * 2.0f;
        const float lx = cbX - snap(12.0f) - lw, ly = cbY + (cbS - lh) * 0.5f;
        rpanel(dev, lx, ly, lw, lh, snap(7.0f), C_CTL_T, C_CTL_B, C_CTL_BR, snap(1.5f));
        const char* seg[2] = { "EN", "FR" };
        for (int i = 0; i < 2; ++i) {
            const float sx = lx + (float)i * segW;
            const bool on = (ui_config().lang == i);
            const bool hov = inrect(mo, sx, ly, segW, lh);
            if (on) rrect_fill(dev, sx + snap(2.0f), ly + snap(2.0f), segW - snap(4.0f), lh - snap(4.0f), snap(5.0f), lerpc(C_ACCENT, C_ACCENTHI, pulse), C_ACCENT);   // blue = interactive
            fo->begin(dev);
            fo->draw_c(dev, sx + segW * 0.5f, ly + lh * 0.5f, seg[i], snap(13.0f),
                       on ? fa(0xFFFFFFFF) : fa(lerpc(C_DIM, C_TEXT, hov ? 1.0f : 0.0f)), fa(C_STROKE), 1.0f);   // white on blue / dim on dark
            if (hov && click && !on) { ui_config().lang = i; save_ui_config(); }
        }
    }

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
        fo->draw_c(dev, tx + tabW * 0.5f, tabY + tabH * 0.5f, tab_label(i), snap(15.0f),
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
            fo->begin(dev); fo->draw_lc(dev, rx + snap(18.0f), ry + rh * 0.5f, module_label(i), snap(15.0f), lerpc(C_DIM, C_TEXT, active ? 1.0f : ht), fa(C_STROKE), 1.0f);
        }
        // forward-looking hint that the sidebar scales (no interaction)
        fo->begin(dev); fo->draw_lc(dev, ix + snap(26.0f), bodyY + snap(44.0f) + MODULE_N * snap(42.0f) + snap(18.0f), tr("more modules soon", "d'autres modules bientôt"), snap(12.0f), fa(C_MUTE), fa(C_STROKE), 1.0f);

        const float coX = ix + sbW + snap(30.0f);
        const float coW = (ix + iw) - coX - snap(26.0f);

        // ===== PROFILE BAR (full content width : active profile + unsaved + quick Save) =====
        const bool dirty = activeProf_[0] && profile_dirty();
        const float barY = bodyY + snap(18.0f), barH = snap(46.0f), barCy = barY + barH * 0.5f;
        drop_shadow(dev, coX, barY, coW, barH, snap(4.0f), 50);
        rpanel(dev, coX, barY, coW, barH, snap(10.0f), 0x55101826, 0x550A111C, C_BORDER, snap(1.5f));
        rrect_fill(dev, coX + snap(4.0f), barY + snap(7.0f), snap(4.0f), barH - snap(14.0f), snap(2.0f), lerpc(C_GOLD, C_GOLDHI, pulse), lerpc(C_GOLD, C_GOLDHI, pulse));   // gold accent pill
        fo->begin(dev); fo->draw_lc(dev, coX + snap(18.0f), barCy, tr("PROFILE", "PROFIL"), snap(11.0f), fa(C_GOLD_DEEP), fa(C_STROKE), 1.2f);
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
            fo->draw_c(dev, fX + fW * 0.5f, barCy, tr("(none -- open the Profile tab)", "(aucun -- ouvre l'onglet Profil)"), snap(13.0f), fa(C_DIM), fa(C_STROKE), 1.0f);   // GREY = no profile
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
                fo->begin(dev); fo->draw_c(dev, bx + saveW * 0.5f, barCy, tr("Save changes", "Enregistrer"), snap(14.0f), fa(0xFF241600), 0, 0.0f);
            } else {                                      // saved / nothing to save -> quiet navy
                if (canSave) halo(dev, bx, bY, saveW, bH, C_ACCENT, t * 0.7f);
                rpanel(dev, bx, bY, saveW, bH, sr, canSave ? lerpc(0xFF2A3548, 0xFF3A82E0, t) : 0xFF1E2630, canSave ? lerpc(0xFF1D2738, 0xFF2A61B6, t) : 0xFF141A22, lerpc(C_BORDERHI, C_ACCENTHI, t), snap(1.5f));
                fo->begin(dev); fo->draw_c(dev, bx + saveW * 0.5f, barCy, tr("Saved", "Enregistré"), snap(14.0f), fa(canSave ? C_TEXT : C_MUTE), fa(C_STROKE), 1.0f);
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
        const float secW = fo->measure(module_label(section_), snap(20.0f));
        fo->draw_lc(dev, coX, coY, module_label(section_), snap(20.0f), fa(C_GOLD), fa(C_STROKE), 1.4f);
        flat(dev, coX, coY + snap(18.0f), secW * e, snap(2.0f), lerpc(C_GOLD, C_GOLDHI, pulse));

        // LIVE PREVIEW stage : a recessed backdrop in the right column. The HUD draws the REAL
        // party + 2-alliance demo boxes (forced //aio alliance2 demo) on top, anchored bottom-right
        // here -- so the preview is exactly what ships in game (cost box space included).
        {
            const float pvx = coX + ctrlW + splitGap, pvy = coY - snap(2.0f);
            fo->begin(dev); fo->draw_lc(dev, pvx, pvy + snap(7.0f), tr("LIVE PREVIEW", "APERÇU EN DIRECT"), snap(12.0f), fa(C_GOLD_DEEP), fa(C_STROKE), 1.4f);
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
          if (int d = row_selector(dev, fo, mo, click, 20, coX, ry + yo, ctrlW, tr("Box Theme", "Thème de cadre"), window_theme_name(ui_config().skinTheme))) {
              ui_config().skinTheme = wrap(ui_config().skinTheme + d, window_theme_count()); save_ui_config(); } }
        ROW_NEXT(52.0f)
        // Font -> party/alliance text face
        { ROW_BAND(52.0f)
          if (int d = row_selector(dev, fo, mo, click, 30, coX, ry + yo, ctrlW, tr("Font", "Police"), ui_font_label(ui_config().fontFace))) {
              ui_config().fontFace = wrap(ui_config().fontFace + d, ui_font_count()); save_ui_config(); } }
        ROW_NEXT(52.0f)
        // Box Size -> per-box scale, INDEPENDENT for Party / Alliance 1 / Alliance 2 (sliders, 5% steps).
        // PARTY min is 100% : below that its footprint can no longer cover the game's native party block,
        // so it can only grow (100%..200%). Alliances are free to shrink too (50%..200%).
        {
            const int   szTier[3] = { 0, 1, 2 };
            const char* szLbl[3]  = { tr("Party Size", "Taille groupe"), tr("Ally 1 Size", "Taille alliance 1"), tr("Ally 2 Size", "Taille alliance 2") };
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
            if (row_slider(dev, fo, mo, 2, coX, ry + yo, ctrlW, tr("Buff Size", "Taille des buffs"), bzbuf, &v01)) {
                float v = lo + v01 * (hi - lo);
                v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f;          // snap to 5% steps
                ui_config().buffScale = v < lo ? lo : (v > hi ? hi : v);
            }
        }
        ROW_NEXT(52.0f)
        // Bar Height -> HP/MP/TP gauge height (the taller rows give the room ; rows grow past the badge)
        { ROW_BAND(46.0f)
            const float lo = 0.80f, hi = 1.80f;
            char hb[16]; sprintf(hb, "%d%%", (int)(ui_config().barHeight * 100.0f + 0.5f));
            float v01 = (ui_config().barHeight - lo) / (hi - lo); v01 = v01 < 0.0f ? 0.0f : (v01 > 1.0f ? 1.0f : v01);
            if (row_slider(dev, fo, mo, 5, coX, ry + yo, ctrlW, tr("Bar Height", "Hauteur des barres"), hb, &v01)) {
                float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f;
                ui_config().barHeight = v < lo ? lo : (v > hi ? hi : v);
            }
        }
        ROW_NEXT(46.0f)
        // Bar Width -> HP/MP/TP gauge width (the box auto-fits wider)
        { ROW_BAND(46.0f)
            const float lo = 0.80f, hi = 1.50f;
            char wb[16]; sprintf(wb, "%d%%", (int)(ui_config().barWidth * 100.0f + 0.5f));
            float v01 = (ui_config().barWidth - lo) / (hi - lo); v01 = v01 < 0.0f ? 0.0f : (v01 > 1.0f ? 1.0f : v01);
            if (row_slider(dev, fo, mo, 6, coX, ry + yo, ctrlW, tr("Bar Width", "Largeur des barres"), wb, &v01)) {
                float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f;
                ui_config().barWidth = v < lo ? lo : (v > hi ? hi : v);
            }
        }
        ROW_NEXT(46.0f)
        // Job Badge : Off / Main job only / Main + Sub
        { ROW_BAND(52.0f)
            int m = ui_config().jobBadge; if (m < 0 || m > 2) m = 2;
            const char* jb[3] = { tr("Off", "Aucun"), tr("Main job", "Job principal"), tr("Main + Sub", "Principal + Sub") };
            if (int d = row_selector(dev, fo, mo, click, 35, coX, ry + yo, ctrlW, tr("Job Badge", "Badge de job"), jb[m])) {
                ui_config().jobBadge = wrap(m + d, 3); save_ui_config(); }
        }
        ROW_NEXT(52.0f)
        // Casts : show the casting-spell line, per box type
        { ROW_BAND(56.0f)
            const float rowH = snap(40.0f), ty = ry + yo;
            fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, tr("Casts", "Sorts"), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
            const char* clbl[2] = { tr("Party", "Groupe"), tr("Alliance", "Alliance") };
            bool* cval[2] = { &ui_config().castParty, &ui_config().castAlly };
            const float bbw = snap(118.0f), bgap = snap(8.0f), bbh = snap(34.0f);
            const float bx0 = coX + ctrlW - (2 * bbw + bgap), bty = ty + (rowH - bbh) * 0.5f;
            for (int i = 0; i < 2; ++i) {
                const float bx2 = bx0 + i * (bbw + bgap);
                if (toggle_chip(dev, fo, mo, click, 70 + i * 2, bx2, bty, bbw, bbh, clbl[i], *cval[i])) { *cval[i] = !*cval[i]; save_ui_config(); }
            }
        }
        ROW_NEXT(56.0f)
        // Distance : show the yalms distance, per box
        { ROW_BAND(56.0f)
            const float rowH = snap(40.0f), ty = ry + yo;
            fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, tr("Distance", "Distance"), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
            const char* dlbl[3] = { tr("Party", "Groupe"), tr("All. 1", "All. 1"), tr("All. 2", "All. 2") };
            const float bbw = snap(82.0f), bgap = snap(8.0f), bbh = snap(34.0f);
            const float bx0 = coX + ctrlW - (3 * bbw + 2 * bgap), bty = ty + (rowH - bbh) * 0.5f;
            for (int i = 0; i < 3; ++i) {
                const float bx2 = bx0 + i * (bbw + bgap);
                if (toggle_chip(dev, fo, mo, click, 74 + i * 2, bx2, bty, bbw, bbh, dlbl[i], ui_config().dist[i])) { ui_config().dist[i] = !ui_config().dist[i]; save_ui_config(); }
            }
        }
        ROW_NEXT(56.0f)
        // Borders : per-box window-skin chrome on/off (Party box, Cost MP box, Alliance 1, Alliance 2)
        { ROW_BAND(56.0f)
            const float rowH = snap(40.0f), ty = ry + yo;
            fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, tr("Borders", "Bordures"), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
            const char* blbl[4] = { tr("Party", "Groupe"), tr("Cost", "Coût"), tr("Ally 1", "All. 1"), tr("Ally 2", "All. 2") };
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
            fo->draw_lc(dev, coX + snap(4.0f), ty + bh * 0.5f, tr("Layout", "Disposition"), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
            if (push_btn(dev, fo, mo, click, 60, edX, ty, bw, bh, tr("Edit Layout", "Éditer dispo"), 0)) ui_config().editLayout = true;
            if (push_btn(dev, fo, mo, click, 61, defX, ty, bw, bh, tr("Default (all)", "Défaut (tout)"), 1)) reset_ui_config();
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
        fo->begin(dev); fo->draw_lc(dev, cx0, bodyY + snap(24.0f), tr("CHARACTER", "PERSONNAGE"), snap(12.0f), fa(C_GOLD_DEEP), fa(C_STROKE), 1.4f);

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
        fo->draw_lc(dev, nlx, ry0 + snap(26.0f), charName ? charName : tr("(not logged in)", "(non connecté)"), snap(17.0f), fa(charName ? C_TEXT : C_MUTE), fa(C_STROKE), 1.2f);
        char actl[80]; if (activeProf_[0]) _snprintf(actl, sizeof(actl), tr("Active : %s", "Actif : %s"), activeProf_); else strcpy(actl, tr("Active : none", "Actif : aucun"));
        fo->draw_lc(dev, nlx, ry0 + snap(50.0f), actl, snap(12.0f), fa(C_DIM), fa(C_STROKE), 1.0f);
        if (dirty) { cs(dev); disc(dev, nlx + snap(3.0f), ry0 + snap(70.0f), snap(3.5f), fa(0xFFFFB454));
            fo->begin(dev); fo->draw_lc(dev, nlx + snap(12.0f), ry0 + snap(70.0f), tr("unsaved changes", "modifs non enregistrées"), snap(11.0f), fa(0xFFFFB454), fa(C_STROKE), 1.0f); }

        // quick-save buttons
        ry0 += ccH + snap(18.0f);
        fo->begin(dev); fo->draw_lc(dev, cx0, ry0, tr("QUICK SAVE", "SAUVEGARDE RAPIDE"), snap(11.0f), fa(C_GOLD_DEEP), fa(C_STROKE), 1.2f);
        ry0 += snap(16.0f); const float qbH = snap(40.0f);
        { char lbl[48]; if (charName) _snprintf(lbl, sizeof(lbl), tr("Save for %s", "Sauver pour %s"), charName); else strcpy(lbl, tr("Save for character", "Sauver pour le perso"));
          if (push_btn(dev, fo, mo, click, 710, cx0, ry0, cw0, qbH, lbl, 0) && charName) {
              profile_save(charName); strncpy(activeProf_, charName, 31); activeProf_[31] = 0;
              nameBuf_[0] = 0; nameLen_ = 0; nameCur_ = 0; nameFocus_ = false; profDirty_ = true; } }
        ry0 += qbH + snap(10.0f);
        if (push_btn(dev, fo, mo, click, 711, cx0, ry0, cw0, qbH, tr("Save as Default", "Sauver comme Défaut"), 0)) {
            profile_save("Default"); strcpy(activeProf_, "Default");
            nameBuf_[0] = 0; nameLen_ = 0; nameCur_ = 0; nameFocus_ = false; profDirty_ = true; }
        ry0 += qbH + snap(18.0f);
        fo->begin(dev); fo->draw_lc(dev, cx0, ry0, tr("A profile snapshots every setting. Quick-save", "Un profil capture tous les réglages. La sauvegarde"), snap(11.0f), fa(C_MUTE), fa(C_STROKE), 1.0f);
        fo->draw_lc(dev, cx0, ry0 + snap(16.0f), tr("binds one to this character or the default.", "rapide en lie un à ce personnage ou au défaut."), snap(11.0f), fa(C_MUTE), fa(C_STROKE), 1.0f);

        // ===== RIGHT : create / rename + the saved library =====
        const float pX = ix + sbW + snap(34.0f), pW = (ix + iw) - pX - snap(30.0f);
        float py = bodyY + snap(26.0f);
        fo->begin(dev);
        const char* ptit = tr("Profiles", "Profils");
        const float ptW = fo->measure(ptit, snap(22.0f));
        fo->draw_lc(dev, pX, py, ptit, snap(22.0f), fa(C_GOLD), fa(C_STROKE), 1.4f);
        flat(dev, pX, py + snap(22.0f), ptW * e, snap(2.0f), lerpc(C_GOLD, C_GOLDHI, pulse));
        fo->begin(dev);   // re-bind the glyph atlas : the underline flat() above left a colour-only state
        fo->draw_lc(dev, pX, py + snap(44.0f), tr("Load to switch instantly. Type a name to create a new profile, or an existing name to overwrite it.", "Charge pour basculer instantanément. Tape un nom pour créer un profil, ou un nom existant pour le remplacer."), snap(13.0f), fa(C_DIM), fa(C_STROKE), 1.0f);

        // create / rename field (rounded recessed) + action button
        py += snap(66.0f);
        fo->begin(dev); fo->draw_lc(dev, pX, py, tr("NEW PROFILE", "NOUVEAU PROFIL"), snap(11.0f), fa(C_GOLD_DEEP), fa(C_STROKE), 1.2f);
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
        else              fo->draw_lc(dev, txX, txY, tr("Type a profile name...", "Tape un nom de profil..."), snap(14.0f), fa(C_MUTE), fa(C_STROKE), 1.0f);
        if (nameFocus_ && sinf(f.t * 5.0f) > 0.0f) {              // blinking caret AT the cursor index
            int cn = nameCur_ < 0 ? 0 : (nameCur_ > nameLen_ ? nameLen_ : nameCur_);
            char pre[32]; memcpy(pre, nameBuf_, cn); pre[cn] = 0;
            const float cxx = txX + (cn > 0 ? fo->measure(pre, snap(15.0f)) : 0.0f) + snap(1.0f);
            flat(dev, cxx, py + snap(11.0f), snap(2.0f), fH - snap(22.0f), C_ACCENTHI); }
        const bool canSave = nameLen_ > 0, exists = canSave && profile_exists(nameBuf_);
        const char* lbl = exists ? tr("Overwrite", "Remplacer") : tr("Create", "Créer");   // always non-destructive : never auto-renames the active one
        const bool doSave = (push_btn(dev, fo, mo, click, 701, pX + fW + fGap, py, btnW, fH, lbl, 0) || commit) && canSave;
        if (doSave) {
            profile_save(nameBuf_);
            strncpy(activeProf_, nameBuf_, sizeof(activeProf_) - 1); activeProf_[sizeof(activeProf_) - 1] = 0;
            nameBuf_[0] = 0; nameLen_ = 0; nameCur_ = 0; nameFocus_ = false;   // clear the field -> ready for the NEXT new profile
            profDirty_ = true;
        }

        // saved library
        py += fH + snap(24.0f);
        fo->begin(dev); char hdr[48]; sprintf(hdr, tr("SAVED  (%d)", "ENREGISTRÉS  (%d)"), nprof);
        fo->draw_lc(dev, pX, py, hdr, snap(12.0f), fa(C_GOLD_DEEP), fa(C_STROKE), 1.2f);
        py += snap(24.0f);
        const float rowH = snap(50.0f), rGap = snap(9.0f);
        if (nprof == 0) {
            rpanel(dev, pX, py, pW, snap(62.0f), snap(11.0f), 0x2A101826, 0x2A0A111C, C_BORDER, snap(1.5f));
            fo->begin(dev); fo->draw_c(dev, pX + pW * 0.5f, py + snap(31.0f), tr("No profiles yet -- quick-save one, or type a name and Create.", "Aucun profil -- fais une sauvegarde rapide, ou tape un nom et Créer."), snap(14.0f), fa(C_DIM), fa(C_STROKE), 1.0f);
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
            if (active) bxa += badge(dev, fo, bxa, bcy, tr("ACTIVE", "ACTIF"),   0xFF5FD37E) + snap(6.0f);   // green
            if (isDef)  bxa += badge(dev, fo, bxa, bcy, tr("DEFAULT", "DÉFAUT"), C_GOLDHI)   + snap(6.0f);   // gold
            if (isChar) bxa += badge(dev, fo, bxa, bcy, charName,                0xFF74BCFF)  + snap(6.0f);   // blue
            const float lbw = snap(84.0f), dbw = snap(84.0f), bgap = snap(8.0f), bH = snap(32.0f);
            const float dX = pX + pW - dbw - snap(10.0f), lX = dX - bgap - lbw, bY = ry + (rowH - bH) * 0.5f;
            if (push_btn(dev, fo, mo, click, 760 + i * 2, lX, bY, lbw, bH, tr("Load", "Charger"), 0)) {
                profile_load(nm);
                strncpy(activeProf_, nm, sizeof(activeProf_) - 1); activeProf_[sizeof(activeProf_) - 1] = 0;
                nameBuf_[0] = 0; nameLen_ = 0; nameCur_ = 0; nameFocus_ = false;   // keep the create field free for a NEW profile
            }
            if (push_btn(dev, fo, mo, click, 761 + i * 2, dX, bY, dbw, bH, tr("Delete", "Supprimer"), 1)) {
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
            fo->draw_lc(dev, rx + snap(16.0f), ry + rh * 0.5f, tr(HELP_MODULES[i].en, HELP_MODULES[i].fr), snap(15.0f),
                        lerpc(C_DIM, C_TEXT, active ? 1.0f : ht), fa(C_STROKE), 1.0f);
        }

        // content : the selected module's help, scrollable
        if (helpSel_ < 0 || helpSel_ >= HELP_MODULE_N) helpSel_ = 0;
        const HelpModule& mod = HELP_MODULES[helpSel_];
        const float hx = ix + sbW + snap(30.0f);
        float hw = (ix + iw) - hx - snap(40.0f);
        const float hwMax = snap(1180.0f); if (hw > hwMax) hw = hwMax;   // cap line length for readability
        const float top = bodyY + snap(8.0f), bot = pageBot - snap(4.0f);
        if (ui_config().wheel != 0) {   // clamp against LAST frame's limit immediately -> no overscroll bounce at the bottom
            helpScroll_ -= (float)ui_config().wheel * snap(40.0f); ui_config().wheel = 0;
            if (helpScroll_ < 0.0f) helpScroll_ = 0.0f; if (helpScroll_ > helpMaxScroll_) helpScroll_ = helpMaxScroll_;
        }
        const float natTop = top + snap(16.0f), lh = snap(22.0f), bsz = snap(14.0f), hsz = snap(18.0f);
        float y = natTop - helpScroll_;
        for (int i = 0; i < mod.count; ++i) {
            const HelpItem& it = mod.items[i];
            const char* txt = (ui_config().lang == 1) ? it.fr : it.en;   // active-language text for this item
            if (it.kind == 0) {                       // heading + accent underline
                y += snap(16.0f);
                if (y >= top && y + lh + snap(3.0f) <= bot) {   // STRICT clip (incl. the underline) -> never spills onto the tabs / footer
                    fo->begin(dev); fo->draw_lc(dev, hx, y + lh * 0.5f, txt, hsz, fa(C_GOLD), fa(C_STROKE), 1.3f);
                    const float ulW = fo->measure(txt, hsz);   // underline spans the FULL title width
                    flat(dev, hx, y + lh + snap(1.0f), ulW, snap(2.0f), lerpc(C_GOLD, C_GOLDHI, pulse));
                }
                y += lh + snap(8.0f);
            } else if (it.kind == 2) {                // bullet
                if (y >= top && y + lh <= bot) flat(dev, hx + snap(6.0f), y + lh * 0.5f - snap(2.0f), snap(4.0f), snap(4.0f), fa(C_ACCENT));
                y = draw_wrapped(dev, fo, hx + snap(20.0f), y, hw - snap(20.0f), top, bot, txt, bsz, C_DIM, lh);
                y += snap(3.0f);
            } else if (it.kind == 10) {               // LIVE sample : the distance readout sweeping 0.00 -> 30.00
                const float rh2 = snap(28.0f), bw = snap(96.0f);
                if (y >= top && y + rh2 <= bot) {
                    const float d = fmodf(f.t * 4.0f, 30.0f);
                    const u32 dc = (d < 10.0f) ? 0xFF5AA2FF : (d < 20.8f) ? 0xFFEFD24A : 0xFFFF6E6E;   // blue / yellow / red
                    vg(dev, hx, y, bw, rh2, 0xF01E2838, 0xF0141C24); outline(dev, hx, y, bw, rh2, C_BORDER);
                    char db[16]; sprintf(db, "%05.2f", d);
                    fo->begin(dev); fo->draw_c(dev, hx + bw * 0.5f, y + rh2 * 0.5f, db, snap(16.0f), fa(dc), fa(C_STROKE), 1.3f);
                    fo->draw_lc(dev, hx + bw + snap(14.0f), y + rh2 * 0.5f, txt, bsz, fa(C_DIM), fa(C_STROKE), 1.0f);
                }
                y += rh2 + snap(10.0f);
            } else if (it.kind == 11) {               // LIVE sample : the three leader / QM dots with their game terms
                const u32 dcol[3] = { 0xFFFFFFFF, 0xFFFFEF3F, 0xFF42D98A };
                const char* dlab[3] = { "Alliance Leader", "Party Leader", "Quartermaster" };
                const float rh2 = snap(22.0f);
                for (int k = 0; k < 3; ++k) {
                    if (y >= top && y + rh2 <= bot) {
                        disc(dev, hx + snap(9.0f), y + rh2 * 0.5f, snap(4.5f), dcol[k]);
                        fo->begin(dev); fo->draw_lc(dev, hx + snap(24.0f), y + rh2 * 0.5f, dlab[k], bsz, fa(C_TEXT), fa(C_STROKE), 1.0f);
                    }
                    y += rh2;
                }
                y += snap(8.0f);
            } else if (it.kind == 12) {               // LIVE sample : the REAL HP gauge sweeping full -> empty (colour + critical blink)
                const float gh = snap(22.0f), gw = snap(210.0f), rh2 = gh + snap(12.0f);
                if (y >= top && y + rh2 <= bot) {
                    const float hp = 0.5f + 0.5f * sinf(f.t * 0.55f);   // 0..1
                    const u32 hc = (hp >= 0.5f) ? lerpc(0xFFEFD24A, 0xFF5ADC5A, (hp - 0.5f) / 0.5f) : lerpc(0xFFFF4646, 0xFFEFD24A, hp / 0.5f);
                    party_gauge(dev, hx + snap(4.0f), y + snap(5.0f), gw, gh, hp * 100.0f, hc, f.t, 0.0f, hp <= 0.25f ? 1.0f : 0.0f);
                    fo->begin(dev); fo->draw_lc(dev, hx + gw + snap(24.0f), y + rh2 * 0.5f, txt, bsz, fa(C_DIM), fa(C_STROKE), 1.0f);
                }
                y += rh2 + snap(8.0f);
            } else if (it.kind == 13) {               // LIVE sample : the REAL MP (blue) and TP (glows past 1000) gauges
                const float gh = snap(22.0f), gw = snap(130.0f), gap = snap(16.0f), rh2 = gh + snap(12.0f);
                if (y >= top && y + rh2 <= bot) {
                    const float mp = 0.45f + 0.35f * sinf(f.t * 0.4f + 1.0f);
                    party_gauge(dev, hx + snap(4.0f), y + snap(5.0f), gw, gh, mp * 100.0f, 0xFF4F9DFF, f.t, 0.0f, 0.0f);
                    const float tpf = 0.5f + 0.5f * sinf(f.t * 0.5f);   // 0..1 = 0..3000 TP
                    const bool ready = tpf >= (1000.0f / 3000.0f);
                    party_gauge(dev, hx + snap(4.0f) + gw + gap, y + snap(5.0f), gw, gh, tpf * 100.0f, ready ? 0xFFFF7AE8 : 0xFFE35AD6, f.t, ready ? 1.0f : 0.0f, 0.0f);
                    fo->begin(dev); fo->draw_lc(dev, hx + snap(4.0f) + 2 * gw + gap + snap(22.0f), y + rh2 * 0.5f, txt, bsz, fa(C_DIM), fa(C_STROKE), 1.0f);
                }
                y += rh2 + snap(8.0f);
            } else if (it.kind == 14) {               // LIVE sample : the REAL selection hand + its highlight bar, gold (main) / blue (sub)
                const float rh2 = snap(44.0f), hand = snap(44.0f), bx2 = hx + hand + snap(8.0f), bw = snap(200.0f);
                for (int t2 = 0; t2 < 2; ++t2) {
                    if (y >= top && y + rh2 <= bot) {
                        const u32 base = (t2 == 0) ? 0xFFFFDC78 : 0xFF5AA2FF;
                        flat(dev, bx2, y, bw, rh2, (base & 0x00FFFFFF) | 0x30000000);
                        outline(dev, bx2, y, bw, rh2, (base & 0x00FFFFFF) | 0x99000000);
                        party_cursor(dev, helpCursorTex_, hx + hand * 0.5f, y + rh2 * 0.5f, hand, t2 == 1);
                        const char* cl = (t2 == 0) ? (ui_config().lang == 1 ? "Cible principale, curseur blanc" : "Main target, white hand")
                                                   : (ui_config().lang == 1 ? "Sous-cible, curseur bleu" : "Sub-target, blue hand");
                        fo->begin(dev); fo->draw_lc(dev, bx2 + bw + snap(14.0f), y + rh2 * 0.5f, cl, bsz, fa(C_DIM), fa(C_STROKE), 1.0f);
                    }
                    y += rh2 + snap(4.0f);
                }
                y += snap(6.0f);
            } else {                                  // paragraph
                y = draw_wrapped(dev, fo, hx, y, hw, top, bot, txt, bsz, C_DIM, lh);
                y += snap(10.0f);
            }
        }
        // scroll clamp (next frame) + a thin scrollbar on the right
        const float viewH = bot - natTop, contentH = (y + helpScroll_) - natTop;
        float maxScroll = contentH - viewH; if (maxScroll < 0.0f) maxScroll = 0.0f;
        helpMaxScroll_ = maxScroll;   // remember for next frame's wheel clamp (kills the overscroll bounce)
        if (helpScroll_ < 0.0f) helpScroll_ = 0.0f; if (helpScroll_ > maxScroll) helpScroll_ = maxScroll;
        if (maxScroll > 0.0f) {
            const float sbX = ix + iw - snap(8.0f);
            flat(dev, sbX, top, snap(3.0f), bot - top, 0x22FFFFFF);
            const float thH = (bot - top) * (viewH / contentH), thY = top + (bot - top - thH) * (helpScroll_ / maxScroll);
            flat(dev, sbX, thY, snap(3.0f), thH, lerpc(C_ACCENT, C_ACCENTHI, pulse));
        }
    }

    // (no custom cursor : the game/OS already shows one -> avoid a double pointer)
}

} // namespace aio
