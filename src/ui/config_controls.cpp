// config_controls.cpp -- see config_controls.h. The shared immediate-mode config toolkit
// (palette state + animation springs + AA primitives + the labeled controls), lifted out of
// config_page.cpp so every module's *_config.cpp draws with the same controls and ease() namespaces.
#include "ui/config_controls.h"
#include "gfx/draw.h"          // grad_quad, rrect, soft_blob, rrect_glow, disc, disc_glow, seg_soft, fill_tri, tquad, dSet*
#include "model/ui_config.h"   // ui_config(), save_ui_config() (row_slider persists on release)
#include <cmath>
#include <cstdio>

namespace aio {

// ---- MUTABLE accent family : default teal, overwritten every frame by apply_ui_theme() ----
u32 C_ACCENT = 0xFF2FD4C6, C_ACCENTHI = 0xFF7FEFE4;
u32 C_GOLD   = 0xFF2FD4C6, C_GOLDHI = 0xFF8FF2E8, C_GOLD_DEEP = 0xFF6FE0D6;
u32 C_CTL_T  = 0x6E1E3A37, C_CTL_B = 0x5E132927, C_CTL_BR = 0x8836726B, C_ARROW = 0xFF7FD8CE;
u32 C_TABON_T = 0xFF204742, C_TABON_B = 0xFF163330;
u32 C_ROWON_T = 0xFF1F413C, C_ROWON_B = 0xFF163430;
u32 C_CHIP_ON_T = 0xFF35DACC, C_CHIP_ON_B = 0xFF1FA79C;

// ---- frame clock + fade (set once per frame by the config page, before drawing) ----
float g_fade = 1.0f;
float g_dt   = 0.016f;
float g_t    = 0.0f;

// ---- per-element animation springs : one 0..1 value per stable id, eased toward a target ----
struct Anim { int id; int sub; float v; };
static const int ANIM_MAX = 1024;  // hover/toggle springs, one per distinct (control id, sub-slot)
static Anim g_anim[ANIM_MAX];
static int  g_animN = 0;

// ---- colour STYLES : 12 base hues per family at that family's saturation/brightness character ----
static const u32 STY_NEON[]  = { 0xFFFF3B4D,0xFFFF8A1F,0xFFFFB01F,0xFFFFE23D,0xFFA6F034,0xFF3BFF7A,0xFF24E0A0,0xFF2FE0C8,0xFF22DEFF,0xFF3D8BFF,0xFF9A4DFF,0xFFE84DFF };
static const u32 STY_MATTE[] = { 0xFFB57A7A,0xFFB58A66,0xFFB0A06E,0xFF9AA87A,0xFF7BA07A,0xFF6FA0A0,0xFF6E8BB0,0xFF7C8BA8,0xFF8B8BB0,0xFF9A8BA8,0xFFB08AA0,0xFFB58A99 };
static const u32 STY_MED[]   = { 0xFFC9A227,0xFFC77B4A,0xFFB0703A,0xFFA63A3A,0xFF8A2F3A,0xFF8A3A55,0xFF6E4A8A,0xFF45557A,0xFF3A6A7A,0xFF3A7A6A,0xFF4E7A45,0xFF7A8A3A };
static const u32 STY_HERO[]  = { 0xFFE03A3A,0xFFE0602E,0xFFE0902E,0xFFF5C542,0xFFA8D63A,0xFF3AC46E,0xFF2EC48A,0xFF2EC0B0,0xFF35A8E0,0xFF2E6AE0,0xFF7A4AE0,0xFFC23AE0 };
static const u32 STY_PAS[]   = { 0xFFF0A6A6,0xFFF5C6A6,0xFFF0E6A6,0xFFDCE8A6,0xFFB6E0A6,0xFF9CE8D0,0xFFA6E8E8,0xFFA6D6F0,0xFFA6B0F0,0xFFC6B0F0,0xFFE6B0F0,0xFFF0A6C0 };
extern const ThemeStyle STYLES[] = {
    { "Neon",     "Néon",     STY_NEON,  (int)(sizeof(STY_NEON)  / sizeof(u32)) },
    { "Matte",    "Mat",      STY_MATTE, (int)(sizeof(STY_MATTE) / sizeof(u32)) },
    { "Medieval", "Médiéval", STY_MED,   (int)(sizeof(STY_MED)   / sizeof(u32)) },
    { "Heroic",   "Héroïque", STY_HERO,  (int)(sizeof(STY_HERO)  / sizeof(u32)) },
    { "Pastel",   "Pastel",   STY_PAS,   (int)(sizeof(STY_PAS)   / sizeof(u32)) },
};
extern const int STYLE_N = (int)(sizeof(STYLES) / sizeof(STYLES[0]));

// ---- language : 0 = English, 1 = French. tr() picks the active-language string for inline UI text. ----
const char* tr(const char* en, const char* fr) { return ui_config().lang == 1 ? fr : en; }

// brighten (f>0 -> toward white) / darken (f<0 -> toward black) an opaque RGB ; keeps the given alpha.
u32 shade(u32 c, float f, u32 alpha) {
    int r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
    if (f >= 0.0f) { r += (int)((255 - r) * f); g += (int)((255 - g) * f); b += (int)((255 - b) * f); }
    else { const float k = 1.0f + f; r = (int)(r * k); g = (int)(g * k); b = (int)(b * k); }
    if (r < 0) r = 0; if (r > 255) r = 255; if (g < 0) g = 0; if (g > 255) g = 255; if (b < 0) b = 0; if (b > 255) b = 255;
    return (alpha << 24) | ((u32)r << 16) | ((u32)g << 8) | (u32)b;
}
// one hue's lightness NUANCE : row 0 = light tint, 1 = base, 2 = deep shade (a proper tint/shade ramp).
u32 nuance(u32 base, int row) {
    static const float F[NUANCE_ROWS] = { 0.42f, 0.0f, -0.42f };
    if (row < 0) row = 0; else if (row >= NUANCE_ROWS) row = NUANCE_ROWS - 1;
    return shade(base, F[row]);
}
// total swatches for a style = hues * lightness rows (the nuance chart).
int style_swatch_count(int style) {
    if (style < 0 || style >= STYLE_N) style = 0;
    return STYLES[style].n * NUANCE_ROWS;
}
// resolve the chosen (style, colour index) to its accent RGB. The index walks the chart ROW-major :
// index = row * hues + column -> column = hue, row = lightness.
u32 theme_accent(int style, int color) {
    if (style < 0 || style >= STYLE_N) style = 0;
    const ThemeStyle& S = STYLES[style];
    const int total = S.n * NUANCE_ROWS;
    if (color < 0 || color >= total) color = 0;
    return nuance(S.col[color % S.n], color / S.n);
}
// derive the whole accent family from the chosen style + colour (called once per frame, before drawing).
void apply_ui_theme(int style, int color) {
    u32 a;
    if (ui_config().uiAccent & 0xFF000000u) {                            // custom accent wins over the style/colour preset
        a = ui_config().uiAccent;
        // A custom accent can be arbitrarily DARK -> it would vanish on the black chrome (tab icons/indicators,
        // the AIOHUD title, glows, the Save fill, ...). Guarantee a MINIMUM luminance (keeping the hue) so every
        // accent-as-foreground element stays visible. Presets are left untouched (they are designed legible).
        const int L = (int)((((a >> 16) & 0xFF) * 54u + ((a >> 8) & 0xFF) * 183u + (a & 0xFF) * 19u) >> 8);
        if (L < 150) { float f = (150.0f - (float)L) / (255.0f - (float)L); if (f > 0.85f) f = 0.85f; a = shade(a, f); }
    } else {
        a = theme_accent(style, color);
    }
    C_ACCENT    = shade(a, 0.0f);          C_ACCENTHI  = shade(a, 0.45f);
    C_GOLD      = C_ACCENT;                C_GOLDHI    = C_ACCENTHI;         C_GOLD_DEEP = shade(a, 0.18f);   // eyebrow labels : a LIGHTENED accent -> readable on the dark bg for every theme colour (was -0.5 = too dark)
    C_ARROW     = shade(a, 0.35f);
    C_CTL_T     = shade(a, -0.82f, 0x6E);  C_CTL_B     = shade(a, -0.88f, 0x5E);  C_CTL_BR = shade(a, -0.48f, 0x88);
    C_TABON_T   = shade(a, -0.70f);        C_TABON_B   = shade(a, -0.78f);
    C_ROWON_T   = shade(a, -0.66f);        C_ROWON_B   = shade(a, -0.74f);
    C_CHIP_ON_T = shade(a, 0.06f);         C_CHIP_ON_B = shade(a, -0.28f);
}


u32 fa(u32 c) {
    u32 a = (u32)(((c >> 24) & 0xFF) * g_fade + 0.5f);
    return (c & 0x00FFFFFF) | (a << 24);
}
u32 lerpc(u32 a, u32 b, float t) {
    t = clampf(t, 0.0f, 1.0f);
    int aa = (a>>24)&0xFF, ar = (a>>16)&0xFF, ag = (a>>8)&0xFF, ab = a&0xFF;
    int ba = (b>>24)&0xFF, br = (b>>16)&0xFF, bg = (b>>8)&0xFF, bb = b&0xFF;
    return ((u32)(aa+(int)((ba-aa)*t))<<24) | ((u32)(ar+(int)((br-ar)*t))<<16)
         | ((u32)(ag+(int)((bg-ag)*t))<<8)  |  (u32)(ab+(int)((bb-ab)*t));
}

// COMPOSITE key (id, sub) : a control keys its N springs on (its unique CTRL_ID, 0..N-1) so no two controls can
// ever share a slot -- no arithmetic offsets (uid+1 / 40+uid / 2000+uid*2) that could overlap. Legacy 2-arg callers
// (party / edit_box / textures) map to sub 0, and their small hand-picked ids never meet a CTRL_ID hash.
float ease(int id, int sub, float target, float speed) {
    Anim* s = nullptr;
    for (int i = 0; i < g_animN; ++i) if (g_anim[i].id == id && g_anim[i].sub == sub) { s = &g_anim[i]; break; }
    if (!s) { if (g_animN >= ANIM_MAX) return target; s = &g_anim[g_animN++]; s->id = id; s->sub = sub; s->v = target; }
    s->v += (target - s->v) * clampf(g_dt * speed, 0.0f, 1.0f);
    return s->v;
}
float ease(int id, float target, float speed) { return ease(id, 0, target, speed); }
// staggered entrance factor (ease-out cubic) for content row i : later rows start a touch later.
// The per-row delay is CAPPED so long, scrollable lists still reach FULL opacity when the page is
// open -- without the cap, rows past index ~22 got a factor of 0 and stayed invisible.
float stagger(float anim, int i) {
    if (i > 10) i = 10;
    float a = clampf((anim - 0.045f * (float)i) / 0.5f, 0.0f, 1.0f);
    return 1.0f - (1.0f - a) * (1.0f - a) * (1.0f - a);
}

// colour-only quad state (grad_quad uses the current device state ; the font leaves a textured
// stage bound, which would fade any quad drawn after text -> reset before every fill).
void cs(u32 dev) {
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
void cs_add(u32 dev) {
    dSetVS(dev, FVF_XYZRHW_DIFFUSE);
    dSetRS(dev, D3DRS_ALPHABLENDENABLE, 1);
    dSetRS(dev, D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_ONE);
    dSetTex(dev, 0, 0);
    dSetTSS(dev, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1); dSetTSS(dev, 0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    dSetTSS(dev, 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1); dSetTSS(dev, 0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    dSetTSS(dev, 1, D3DTSS_COLOROP, D3DTOP_DISABLE);
}
void q4(u32 dev, float x, float y, float w, float h, u32 tl, u32 tr, u32 bl, u32 br) {
    cs(dev); grad_quad(dev, x, y, w, h, fa(tl), fa(tr), fa(bl), fa(br));
}
void flat(u32 dev, float x, float y, float w, float h, u32 c) { q4(dev, x, y, w, h, c, c, c, c); }
void vg(u32 dev, float x, float y, float w, float h, u32 t, u32 b) { q4(dev, x, y, w, h, t, t, b, b); }
void outline(u32 dev, float x, float y, float w, float h, u32 c) {
    flat(dev, x, y, w, 1, c); flat(dev, x, y + h - 1, w, 1, c);
    flat(dev, x, y, 1, h, c); flat(dev, x + w - 1, y, 1, h, c);
}
void shadow_down(u32 dev, float x, float y, float w, float h, u32 top) {
    q4(dev, x, y, w, h, top, top, top & 0x00FFFFFF, top & 0x00FFFFFF);
}
// luminous accent glow BEHIND an element (draw before it). ADDITIVE 3-layer bloom : a wide soft base,
// a mid ring and a tight bright core -> the light builds up and reads as real neon glow, not a flat card.
void halo(u32 dev, float x, float y, float w, float h, u32 col, float t) {
    if (t <= 0.01f) return;
    t = clampf(t, 0.0f, 1.0f) * g_fade;
    const u32 rgb = col & 0x00FFFFFF;
    const float cx = x + w * 0.5f, cy = y + h * 0.5f, hw = w * 0.5f, hh = h * 0.5f;
    cs_add(dev);
    soft_blob(dev, cx, cy, hw + snap(22.0f), hh + snap(22.0f), rgb | ((u32)(38.0f * t) << 24));   // wide soft base
    soft_blob(dev, cx, cy, hw + snap(12.0f), hh + snap(12.0f), rgb | ((u32)(58.0f * t) << 24));   // mid bloom
    soft_blob(dev, cx, cy, hw + snap(5.0f),  hh + snap(5.0f),  rgb | ((u32)(72.0f * t) << 24));   // bright core
}
// accent glow BEHIND a (rounded) rectangular element (draw before it) : two SMOOTH feathered bands that
// hug the rounded silhouette -> a clean luminous halo that pulses without visible banding / coarse steps.
void halo_rect(u32 dev, float x, float y, float w, float h, u32 col, float t) {
    if (t <= 0.01f) return;
    t = clampf(t, 0.0f, 1.0f) * g_fade;
    const u32 rgb = col & 0x00FFFFFF;
    const float r = snap(6.0f);
    cs_add(dev);
    rrect_glow(dev, x, y, w, h, r, rgb | ((u32)(38.0f * t) << 24), snap(10.0f));   // wide soft aura
    rrect_glow(dev, x, y, w, h, r, rgb | ((u32)(64.0f * t) << 24), snap(3.5f));    // tight bright edge
}
// a moving glass "shine" streak that sweeps across an element while hovered (additive, clipped to the
// rect by clamping the band). amt = hover strength (0..1) ; tsec = wrapping seconds for the motion.
void shine(u32 dev, float x, float y, float w, float h, float amt, float tsec) {
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

// a crisp thin CHEVRON drawn as two ANTI-ALIASED (feathered) strokes -> an elegant "<" / ">", not a
// chunky, jagged filled triangle. dir < 0 points left, dir > 0 points right.
void chevron(u32 dev, float cx, float cy, float s, int dir, u32 col) {
    cs(dev);
    const float hw = s * 0.26f, hh = s * 0.40f, th = snap(2.6f);
    const u32 c = fa(col);
    const float apex = cx + dir * hw, base = cx - dir * hw;   // apex = the pointed side
    seg_soft(dev, base, cy - hh, apex, cy,      th, c);       // upper arm -> apex
    seg_soft(dev, apex, cy,      base, cy + hh, th, c);       // apex -> lower arm
}
// a settings-row background band : faint zebra fill that ties the left label to the far-right control,
// brightening a touch on hover. Kills the "label and control floating in a void" problem.
void row_band(u32 dev, float x, float y, float w, float h, bool alt, float hov) {
    (void)alt;                                                       // no zebra bands : the category card is a full solid surface
    const int a = (int)(0x14 * clampf(hov, 0.0f, 1.0f) + 0.5f);      // hover highlight (base is flat)
    if (a > 0) flat(dev, x, y, w, h, ((u32)a << 24) | 0x00FFFFFF);
    flat(dev, x + snap(8.0f), y + h - 1.0f, w - snap(16.0f), 1, 0x16FFFFFF);   // interligne : a thin divider between rows
}

// ---- rectangular stencil CLIP for the scrolling controls viewport (D3D8 has no scissor rect ; same
// technique as the vial's rounded clip). Everything drawn between begin/end is masked to the rect.
// If the back-buffer has no stencil the ops are ignored -> the column just overflows as before (no crash). ----
enum { SCL_ENABLE = 52, SCL_FAIL = 53, SCL_ZFAIL = 54, SCL_PASS = 55, SCL_FUNC = 56, SCL_REF = 57, SCL_MASK = 58, SCL_WRITEMASK = 59 };
void clip_rect_begin(u32 dev, float x, float y, float w, float h) {
    dSetVS(dev, FVF_XYZRHW_DIFFUSE); dSetTex(dev, 0, 0);
    dSetRS(dev, D3DRS_ALPHATESTENABLE, 0);
    dSetRS(dev, D3DRS_ALPHABLENDENABLE, 0);
    dSetRS(dev, SCL_ENABLE, 1);
    dSetRS(dev, SCL_MASK, 0xFF); dSetRS(dev, SCL_WRITEMASK, 0xFF);
    dSetRS(dev, D3DRS_COLORWRITEENABLE, 0);                        // mask pass : write stencil only, no colour
    dSetRS(dev, SCL_FUNC, 8);                                      // ALWAYS
    dSetRS(dev, SCL_FAIL, 3); dSetRS(dev, SCL_ZFAIL, 3); dSetRS(dev, SCL_PASS, 3);   // REPLACE
    dSetRS(dev, SCL_REF, 0);
    grad_quad(dev, x - 2.0f, y - 2.0f, w + 4.0f, h + 4.0f, 0, 0, 0, 0);              // clear the region -> 0
    dSetRS(dev, SCL_REF, 1);
    grad_quad(dev, x, y, w, h, 0xFF000000, 0xFF000000, 0xFF000000, 0xFF000000);      // set 1 inside the rect
    dSetRS(dev, D3DRS_COLORWRITEENABLE, 0x0000000F);              // content : colour on, ONLY where stencil == 1
    dSetRS(dev, SCL_FUNC, 3);                                      // EQUAL
    dSetRS(dev, SCL_FAIL, 1); dSetRS(dev, SCL_ZFAIL, 1); dSetRS(dev, SCL_PASS, 1);   // KEEP (don't touch stencil while drawing)
    dSetRS(dev, D3DRS_ALPHABLENDENABLE, 1);
}
void clip_rect_end(u32 dev) {
    dSetRS(dev, SCL_ENABLE, 0);
    dSetRS(dev, D3DRS_COLORWRITEENABLE, 0x0000000F);
    dSetRS(dev, D3DRS_ALPHATESTENABLE, 0);   // restore what clip_rect_begin turned off (leave state as found)
}

// a CHROME wordmark : a dark EXTRUDED shadow (offset passes -> depth) under a vertical METALLIC gradient
// face (bright top -> deep bottom), drawn by stencil-clipping the glyphs to thin horizontal bands.
// `w` = clip width covering the text ; band[Top,Bot] = the glyph's vertical span. A real logo, not flat text.
void chrome_text(u32 dev, Font* fo, float x, float y, const char* s, float size, float w,
                        u32 top, u32 bot, float bandTop, float bandBot) {
    for (int i = 4; i >= 1; --i) {                                              // extrude : dark offset passes (far -> near)
        fo->begin(dev);
        fo->draw_lc(dev, x + i * snap(1.0f), y + i * snap(1.0f), s, size, fa(shade(bot, -0.62f)), 0, 0.0f);
    }
    const int N = 9;
    for (int i = 0; i < N; ++i) {                                              // metallic face : gradient bands
        const float by = bandTop + (bandBot - bandTop) * (float)i / (float)N;
        const float bh = (bandBot - bandTop) / (float)N + snap(1.5f);
        clip_rect_begin(dev, x - snap(8.0f), by, w + snap(16.0f), bh);
        fo->begin(dev);
        fo->draw_lc(dev, x, y, s, size, fa(lerpc(top, bot, (i + 0.5f) / (float)N)), 0, 0.0f);
        clip_rect_end(dev);
    }
    fo->begin(dev);                                                            // a crisp bright rim on the glyph tops
    clip_rect_begin(dev, x - snap(8.0f), bandTop, w + snap(16.0f), (bandBot - bandTop) * 0.30f);
    fo->draw_lc(dev, x, y - snap(1.0f), s, size, fa((0xB0u << 24) | (top & 0x00FFFFFF)), 0, 0.0f);
    clip_rect_end(dev);
}

// a heraldic LOZENGE (diamond) : one AA convex quad (feathered silhouette). Ornament for the fantasy logo.
void gem(u32 dev, float cx, float cy, float r, u32 col) {
    cs(dev);
    const float d[8] = { cx, cy - r,  cx + r, cy,  cx, cy + r,  cx - r, cy };   // top, right, bottom, left
    fill_poly_aa(dev, d, 4, fa(col));
}

// ---- modern primitives : rounded rects (rect bands + quarter-disc corners) + soft drop shadows. ----
// A filled QUARTER disc (triangle fan) confined to ONE corner square -> it NEVER overlaps the bands,
// so the whole rounded rect composites with a SINGLE blend per pixel (correct for translucent fills,
// no double-blend "pinwheel" artefact in the corners).
void qfan(u32 dev, float cx, float cy, float r, float a0, float a1, u32 col) {
    cs(dev);
    const int N = 8;
    const u32 c = fa(col);
    float px = cx + r * cosf(a0), py = cy + r * sinf(a0);
    for (int i = 1; i <= N; ++i) {
        const float a = a0 + (a1 - a0) * (float)i / (float)N;
        const float nx = cx + r * cosf(a), ny = cy + r * sinf(a);
        fill_tri(dev, cx, cy, px, py, nx, ny, c);
        px = nx; py = ny;
    }
    arc_feather(dev, cx, cy, r, a0, a1, N, c);   // AA the outer arc (same seg count -> sits on the facet edge)
}

// rounded fill -> the ANTI-ALIASED primitive (feathered corners, uniform at any size). Kept as a thin
// wrapper so every existing call site (panels, tabs, sliders, row highlights) gets crisp corners for free.
void rrect_fill(u32 dev, float x, float y, float w, float h, float r, u32 top, u32 bot) {
    cs(dev);
    rrect(dev, x, y, w, h, r, fa(top), fa(bot));   // fa() : honour the panel's global fade-in alpha
}
// round the TOP corners only (tabs : the bottom melts into the body).
void rrect_top(u32 dev, float x, float y, float w, float h, float r, u32 top, u32 bot) {
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
void rpanel(u32 dev, float x, float y, float w, float h, float r, u32 top, u32 bot, u32 border, float bt) {
    rrect_fill(dev, x, y, w, h, r, border, border);
    rrect_fill(dev, x + bt, y + bt, w - 2 * bt, h - 2 * bt, (r - bt > 0.0f ? r - bt : 0.0f), top, bot);
}
// a soft, feathered drop shadow under an element (draw BEFORE it) -> floats the card off the page.
void drop_shadow(u32 dev, float x, float y, float w, float h, float spread, u32 alpha) {
    cs(dev);
    soft_blob(dev, x + w * 0.5f, y + h * 0.5f + snap(5.0f), w * 0.5f + spread, h * 0.5f + spread, (alpha << 24));
}
// a tiny rounded status pill (ACTIVE / DEFAULT / a character name). Returns its width so they stack.
// a small rounded tag. ONE accent colour drives it : a dark opaque pill + accent border + BRIGHT accent
// text -> always high-contrast and legible, whatever the row colour behind it.
float badge(u32 dev, Font* fo, float x, float cy, const char* text, u32 accent) {
    const float sz = snap(10.0f), padx = snap(9.0f), h = snap(18.0f);
    const float w = fo->measure(text, sz) + 2.0f * padx, y = cy - h * 0.5f;
    rpanel(dev, x, y, w, h, h * 0.5f, 0xF00E1420, 0xF0090D16, accent, snap(1.2f));   // dark opaque pill + accent border
    fo->begin(dev); fo->draw_c(dev, x + w * 0.5f, cy, text, sz, fa(accent), fa(C_STROKE), 0.9f);   // bright accent text
    return w;
}

// a < / > stepper. Modern & borderless : a bare chevron that lights up with a round accent glow on
// hover and nudges on press (same visual language as the selector-capsule ends -- no boxy sub-button).
bool arrow_btn(u32 dev, Font* fo, const MouseState* mo, bool click, int uid,
                      float x, float y, float s, const char* glyph) {
    (void)fo;
    const float pad = snap(7.0f);                                  // GENEROUS hit zone : forgiving target around the chevron
    const bool hov = inrect(mo, x - pad, y - pad, s + 2.0f * pad, s + 2.0f * pad);
    const bool press = hov && mo && mo->down;
    const float t = ease(uid, 0, hov ? 1.0f : 0.0f);               // smooth hover (composite (id,sub) key -> no cross-control collision)
    const float cx = x + s * 0.5f, cy = y + s * 0.5f;
    const int dir = (glyph[0] == '<') ? -1 : +1;
    if (t > 0.02f) {   // hover : a soft rounded accent KEY (crisp, AA) + a tight feathered rim -- no fuzzy blob
        const u32 acc = C_ACCENT & 0x00FFFFFF;
        const float hs = s + snap(3.0f), hr = hs * 0.42f;
        rrect_fill(dev, cx - hs * 0.5f, cy - hs * 0.5f, hs, hs, hr, acc | ((u32)(52.0f * t) << 24), acc | ((u32)(22.0f * t) << 24));
        cs_add(dev); rrect_glow(dev, cx - hs * 0.5f, cy - hs * 0.5f, hs, hs, hr, acc | ((u32)(44.0f * t * g_fade) << 24), snap(4.0f));
    }
    chevron(dev, cx + (press ? dir * snap(1.0f) : 0.0f), cy, s * 0.60f, dir, lerpc(C_ARROW, C_ACCENTHI, t));
    return hov && click;
}

// A labeled selector row :  "Label            <     value     >".  ONE continuous glass capsule (not
// three boxed sub-controls) : the chevrons live at the two ends and light up with a round accent glow on
// hover, the value floats in the middle. uid = animation base (uid*2 / uid*2+1 -> the two ends).
int row_selector(u32 dev, Font* fo, const MouseState* mo, bool click, int uid,
                        float x, float y, float w, const char* label, const char* value) {
    const float rowH = snap(40.0f);
    fo->begin(dev);
    fo->draw_lc(dev, x + snap(4.0f), y + rowH * 0.5f, label, snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);

    const float aH = snap(30.0f), aEnd = snap(46.0f), valW = snap(168.0f);
    const float ctlW = aEnd + valW + aEnd;
    const float cx = x + w - ctlW, cy = y + (rowH - aH) * 0.5f, ccy = cy + aH * 0.5f;
    const float r  = aH * 0.5f;                                          // fully-rounded pill
    int delta = 0;

    // the two END hit zones of the SAME capsule (the middle just shows the value). GENEROUS : each end
    // is wide and spans the FULL row height, so the chevrons are easy to hit -- not a tight little target.
    const float lx = cx, rx = cx + ctlW - aEnd;
    const bool lh = inrect(mo, lx, y, aEnd, rowH), rh = inrect(mo, rx, y, aEnd, rowH);
    const bool lp = lh && mo && mo->down, rp = rh && mo && mo->down;
    const float lt = ease(uid, 0, lh ? 1.0f : 0.0f);                    // left/right arrow springs : sub 0 / 1 of this control's id
    const float rt = ease(uid, 1, rh ? 1.0f : 0.0f);
    const float anyT = lt > rt ? lt : rt;

    rpanel(dev, cx, cy, ctlW, aH, r, 0x7016211F, 0x700C1513, lerpc(C_CTL_BR, C_ACCENT, anyT), snap(1.2f));   // one glass capsule, border warms to accent
    flat(dev, cx + r, cy + snap(1.0f), ctlW - 2.0f * r, 1, 0x14FFFFFF);                                      // top sheen hairline

    const float lcx = cx + aEnd * 0.5f, rcx = cx + ctlW - aEnd * 0.5f;
    // hover : a soft rounded accent KEY that hugs the pill (crisp, AA) + a tight feathered rim -- no fuzzy blob.
    const u32 acc = C_ACCENT & 0x00FFFFFF;
    const float hlH = aH - snap(8.0f), hlW = aEnd - snap(12.0f), hlR = hlH * 0.5f;
    if (lt > 0.02f) { rrect_fill(dev, lcx - hlW * 0.5f, ccy - hlH * 0.5f, hlW, hlH, hlR, acc | ((u32)(58.0f * lt) << 24), acc | ((u32)(24.0f * lt) << 24));
                      cs_add(dev); rrect_glow(dev, lcx - hlW * 0.5f, ccy - hlH * 0.5f, hlW, hlH, hlR, acc | ((u32)(44.0f * lt * g_fade) << 24), snap(4.0f)); }
    if (rt > 0.02f) { rrect_fill(dev, rcx - hlW * 0.5f, ccy - hlH * 0.5f, hlW, hlH, hlR, acc | ((u32)(58.0f * rt) << 24), acc | ((u32)(24.0f * rt) << 24));
                      cs_add(dev); rrect_glow(dev, rcx - hlW * 0.5f, ccy - hlH * 0.5f, hlW, hlH, hlR, acc | ((u32)(44.0f * rt * g_fade) << 24), snap(4.0f)); }
    chevron(dev, lcx + (lp ? snap(1.0f) : 0.0f), ccy, aH * 0.58f, -1, lerpc(C_ARROW, C_ACCENTHI, lt));   // press nudges the chevron inward
    chevron(dev, rcx - (rp ? snap(1.0f) : 0.0f), ccy, aH * 0.58f, +1, lerpc(C_ARROW, C_ACCENTHI, rt));

    fo->begin(dev); fo->draw_c(dev, cx + ctlW * 0.5f, ccy, value, snap(14.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);

    if (lh && click) delta = -1;
    if (rh && click) delta = +1;
    return delta;
}
int wrap(int v, int n) { if (v < 0) return n - 1; if (v >= n) return 0; return v; }

// A labeled SLIDER row :  "Label        [===O      ]  value". Drag the track/knob to set. Updates
// *v01 (normalized 0..1) LIVE while dragging and persists once on release. Returns true the frames it
// changed. g_slider latches the dragged slider so a press that wanders off the row keeps control of it.
static int g_slider = -1;   // id of the slider being dragged (-1 = none)
bool row_slider(u32 dev, Font* fo, const MouseState* mo, int id,
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
    rrect_fill(dev, trkX, trkY, trkW, trkH, tr, 0x66101416, 0x66090C0E);       // dark groove
    if (fillW >= trkH) rrect_fill(dev, trkX, trkY, fillW, trkH, tr, C_ACCENTHI, C_ACCENT);   // accent fill

    // clean round knob : eases bigger on hover/drag with a THIN accent ring + a soft shadow (no big halo, no gloss).
    const float kt = ease(id, 0, (hot || act) ? 1.0f : 0.0f);
    const float kr = knobR * (1.0f + 0.22f * kt);
    const float kx = trkX + fillW;
    if (kt > 0.01f) { cs_add(dev); disc_glow(dev, kx, cy, kr + snap(1.0f), (C_ACCENT & 0x00FFFFFF) | ((u32)(72.0f * kt) << 24), snap(6.0f)); }   // soft accent ring
    cs(dev);
    disc(dev, kx, cy + snap(1.0f), kr + snap(1.0f), fa(0x55000000));           // subtle drop shadow under the knob
    disc(dev, kx, cy, kr, fa(0xFFF6FAFA));                                     // clean white knob
    fo->begin(dev);
    fo->draw_c(dev, trkX + trkW + gap + valW * 0.5f, cy, valueText, snap(14.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
    return changed;
}

// ---- HSV colour picker (shared) : an SV square + a rainbow hue bar + a live swatch. Replaces the per-channel
// R/G/B slider triples in every module. The two draggable zones (SV square, hue bar) share the g_slider latch.
// HSV is CACHED per-uid so dragging Value to black or Saturation to 0 doesn't lose the hue (RGB can't encode it).
float color_picker_height() {   // SV square (112) + hue slider + 2 preset rows + a "Favourites" label + 1-2 favourite rows
    const int favRows = (ui_config().favColorN + 1) <= 8 ? 1 : 2;   // "+" button + up to 15 favourites -> at most 2 rows of 8
    return 210.0f + 15.0f + (float)favRows * 24.0f;
}

static void rgb2hsv(u32 c, float& h, float& s, float& v) {
    const float r = ((c >> 16) & 0xFF) / 255.0f, g = ((c >> 8) & 0xFF) / 255.0f, b = (c & 0xFF) / 255.0f;
    const float mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
    const float mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
    const float d = mx - mn; v = mx; s = (mx <= 0.0f) ? 0.0f : d / mx;
    if (d <= 0.0f) { h = 0.0f; return; }
    float hh; if (mx == r) hh = fmodf((g - b) / d, 6.0f); else if (mx == g) hh = (b - r) / d + 2.0f; else hh = (r - g) / d + 4.0f;
    hh *= 60.0f; if (hh < 0.0f) hh += 360.0f; h = hh;
}
static u32 hsv2rgb(float h, float s, float v, u32 argbAlpha) {
    h = fmodf(h, 360.0f); if (h < 0.0f) h += 360.0f;
    const float c = v * s, x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f)), m = v - c;
    float r, g, b; const int seg = (int)(h / 60.0f);
    switch (seg) { case 0: r = c; g = x; b = 0; break; case 1: r = x; g = c; b = 0; break; case 2: r = 0; g = c; b = x; break;
                   case 3: r = 0; g = x; b = c; break; case 4: r = x; g = 0; b = c; break; default: r = c; g = 0; b = x; break; }
    const int R = (int)((r + m) * 255.0f + 0.5f), G = (int)((g + m) * 255.0f + 0.5f), B = (int)((b + m) * 255.0f + 0.5f);
    return (argbAlpha & 0xFF000000u) | ((u32)R << 16) | ((u32)G << 8) | (u32)B;
}
struct PickHSV { int uid; float h, s, v; u32 col; };
static PickHSV g_pick[24]; static int g_pickN = 0;
static PickHSV* pick_slot(int uid, u32 col) {
    for (int i = 0; i < g_pickN; ++i) if (g_pick[i].uid == uid) {
        if (g_pick[i].col != col) { rgb2hsv(col, g_pick[i].h, g_pick[i].s, g_pick[i].v); g_pick[i].col = col; }   // colour changed elsewhere -> resync HSV
        return &g_pick[i];
    }
    if (g_pickN < 24) { PickHSV& p = g_pick[g_pickN]; p.uid = uid; rgb2hsv(col, p.h, p.s, p.v); p.col = col; return &g_pick[g_pickN++]; }
    return 0;
}

// curated preset "nuancier" : one compact 5x3 mini-grid tucked to the RIGHT of the swatch (vivid hues then a
// couple of neutrals). Clicking a chip sets the colour instantly (its alpha byte is preserved).
static const u32 CP_PRESETS[15] = {
    0xFFEF4444u, 0xFFF97316u, 0xFFF59E0Bu, 0xFFEAB308u, 0xFF84CC16u,
    0xFF22C55Eu, 0xFF14B8A6u, 0xFF06B6D4u, 0xFF3B82F6u, 0xFF6366F1u,
    0xFF8B5CF6u, 0xFFD946EFu, 0xFFEC4899u, 0xFFFFFFFFu, 0xFF0F172Au,
};

// ---- Collapsible colour FIELD (accordion) : a compact "[caret] label ... [swatch]" row that expands ONE colour
//      picker below it. Only one field is open at a time (global latch), so a panel with several colours stays tidy
//      instead of stacking a full picker per colour. ----
static int g_openColorField = -1;
bool color_field_open(int uid)   { return g_openColorField == uid; }
void color_field_toggle(int uid) { g_openColorField = (g_openColorField == uid) ? -1 : uid; }
bool color_field_row(u32 dev, Font* fo, const MouseState* mo, float x, float y, float w, const char* label, u32 color, bool open) {
    const float rh = snap(38.0f);
    const bool hov = inrect(mo, x, y, w, rh);
    if (hov || open) rrect(dev, x, y, w, rh, snap(6.0f), fa(open ? 0xFF212932u : 0xFF181D22u), fa(open ? 0xFF212932u : 0xFF181D22u));
    const float gx = x + snap(11.0f), gy = y + rh * 0.5f, s = snap(3.6f);                  // caret : right (closed) / down (open)
    if (open) { const float d[6] = { gx - s, gy - s * 0.55f, gx + s, gy - s * 0.55f, gx, gy + s * 0.85f }; fill_poly_aa(dev, d, 3, fa(C_ACCENTHI)); }
    else      { const float d[6] = { gx - s * 0.55f, gy - s, gx - s * 0.55f, gy + s, gx + s * 0.85f, gy }; fill_poly_aa(dev, d, 3, fa(C_ACCENTHI)); }
    if (fo) { fo->begin(dev); fo->draw_lc(dev, x + snap(26.0f), gy, label, snap(14.0f), fa(C_TEXT), fa(C_STROKE), 1.2f); }
    const float sw = snap(48.0f), sh = snap(22.0f), sxp = x + w - sw - snap(8.0f), syp = y + (rh - sh) * 0.5f;   // colour swatch, right
    rrect_bordered(dev, sxp, syp, sw, sh, snap(4.0f), fa(color | 0xFF000000u), fa(color | 0xFF000000u), fa(C_BORDERHI), snap(1.2f));
    return hov && mo && mo->clicked;
}

bool color_picker(u32 dev, Font* fo, const MouseState* mo, int uidSV, int uidHue,
                  float x, float y, float w, u32* color) {
    if (!color) return false;
    PickHSV* p = pick_slot(uidSV, *color);
    if (!p) return false;
    const u32 alpha = *color & 0xFF000000u;

    // ---- layout : big SV SQUARE on top (+ live swatch/hex to its right) | full-width horizontal HUE slider below |
    //      full-width PRESET grid (2 rows of larger chips) at the bottom. Taller, properly proportioned. ----
    const float gap = snap(8.0f);
    const float W = (w > snap(360.0f)) ? snap(360.0f) : w;   // cap the working width : a compact CARD, never stretched across the whole panel
    const float sqW = snap(112.0f), sqH = sqW;             // SV : a real SQUARE
    const float rx  = x + sqW + gap;                       // live swatch + hex, right of the square
    const float swW = snap(26.0f), swH = snap(18.0f);
    const float hueY = y + sqH + gap, hbH = snap(18.0f);   // horizontal hue slider, FULL WIDTH
    const float preY = hueY + hbH + gap;                   // preset grid top (full width, 2 rows)
    const u32   hue = hsv2rgb(p->h, 1.0f, 1.0f, 0xFF000000u);
    const int   PC = 8;                                    // preset columns (8 + 7 = 15)
    const float cg = snap(4.0f);
    const float cw = snap((W - (PC - 1) * cg) / (float)PC), ch = snap(20.0f);
    const float favLabelY = preY + 2.0f * (ch + cg) + snap(4.0f);   // FAVOURITES : small label, then a "+" add button + the saved swatches
    const float favY = favLabelY + snap(15.0f);

    // ---- interaction : SV square + hue slider share the row_slider latch ; presets are one-shot clicks ----
    const bool hotSV  = inrect(mo, x, y, sqW, sqH);
    const bool hotHue = inrect(mo, x, hueY, W, hbH);
    if (mo && mo->clicked && g_slider < 0) { if (hotSV) g_slider = uidSV; else if (hotHue) g_slider = uidHue; }
    bool changed = false;
    if (g_slider == uidSV) {
        if (mo && mo->down) { p->s = clampf((mo->x - x) / sqW, 0.0f, 1.0f); p->v = clampf(1.0f - (mo->y - y) / sqH, 0.0f, 1.0f);
                              const u32 nc = hsv2rgb(p->h, p->s, p->v, alpha); if (nc != *color) { *color = nc; p->col = nc; changed = true; } }
        else { g_slider = -1; save_ui_config(); }
    } else if (g_slider == uidHue) {
        if (mo && mo->down) { p->h = clampf((mo->x - x) / W, 0.0f, 1.0f) * 360.0f;   // horizontal -> map X to hue
                              const u32 nc = hsv2rgb(p->h, p->s, p->v, alpha); if (nc != *color) { *color = nc; p->col = nc; changed = true; } }
        else { g_slider = -1; save_ui_config(); }
    }
    if (mo && mo->clicked && g_slider < 0) {                                            // preset click (separate region)
        for (int i = 0; i < 15; ++i) {
            const float sx = x + (i % PC) * (cw + cg), sy = preY + (i / PC) * (ch + cg);
            if (inrect(mo, sx, sy, cw, ch)) {
                const u32 nc = (CP_PRESETS[i] & 0x00FFFFFFu) | alpha;
                if (nc != *color) { *color = nc; rgb2hsv(nc, p->h, p->s, p->v); p->col = nc; changed = true; save_ui_config(); }
                break;
            }
        }
    }
    if (mo && mo->clicked && g_slider < 0) {                                            // FAVOURITES : "+" adds the current colour ; a swatch applies it (its top-right corner removes it)
        if (inrect(mo, x, favY, cw, ch)) { if (ui_config().fav_color_add(*color)) save_ui_config(); }   // "+" button (slot 0)
        else for (int i = 0; i < ui_config().favColorN; ++i) {
            const int gp = i + 1; const float sx = x + (gp % PC) * (cw + cg), sy = favY + (gp / PC) * (ch + cg);
            if (!inrect(mo, sx, sy, cw, ch)) continue;
            if (inrect(mo, sx + cw - snap(11.0f), sy, snap(11.0f), snap(11.0f))) { ui_config().fav_color_remove(i); save_ui_config(); }   // top-right corner -> remove
            else { const u32 nc = (ui_config().favColors[i] & 0x00FFFFFFu) | alpha; if (nc != *color) { *color = nc; rgb2hsv(nc, p->h, p->s, p->v); p->col = nc; changed = true; save_ui_config(); } }
            break;
        }
    }

    // ---- SV square : white->pure-hue across the top, fading to black at the bottom (the standard picker) ----
    q4(dev, x, y, sqW, sqH, 0xFFFFFFFFu, hue, 0xFF000000u, 0xFF000000u);
    outline(dev, x, y, sqW, sqH, C_BORDER);
    const float cxp = x + p->s * sqW, cyp = y + (1.0f - p->v) * sqH;                    // SV cursor (readable on any shade)
    disc(dev, cxp, cyp, snap(5.0f), 0xCC000000u); disc(dev, cxp, cyp, snap(3.6f), 0xFFFFFFFFu); disc(dev, cxp, cyp, snap(2.0f), *color | 0xFF000000u);

    // ---- live swatch (rounded) + hex readout, top-right of the square ----
    rrect_bordered(dev, rx, y, swW, swH, snap(4.0f), *color | 0xFF000000u, *color | 0xFF000000u, C_BORDERHI, snap(1.2f));
    if (fo) { char hb[10]; sprintf(hb, "#%06X", (unsigned)(*color & 0x00FFFFFFu));
              fo->begin(dev); fo->draw_lc(dev, rx + swW + snap(8.0f), y + swH * 0.5f, hb, snap(12.0f), C_DIM, C_STROKE, 1.0f); }

    // ---- horizontal hue slider : 6 rainbow segments left->right, thin vertical cursor ----
    static const u32 HUE6[7] = { 0xFFFF0000u, 0xFFFFFF00u, 0xFF00FF00u, 0xFF00FFFFu, 0xFF0000FFu, 0xFFFF00FFu, 0xFFFF0000u };
    const float segW = W / 6.0f;
    for (int i = 0; i < 6; ++i) { const float sx = x + i * segW; q4(dev, sx, hueY, segW + 1.0f, hbH, HUE6[i], HUE6[i + 1], HUE6[i], HUE6[i + 1]); }
    outline(dev, x, hueY, W, hbH, C_BORDER);
    const float hcx = x + (p->h / 360.0f) * W;
    flat(dev, snap(hcx) - snap(2.0f), hueY - snap(2.0f), snap(4.0f), hbH + snap(4.0f), 0xFF000000u);
    flat(dev, snap(hcx) - snap(1.0f), hueY - snap(2.0f), snap(2.0f), hbH + snap(4.0f), 0xFFFFFFFFu);

    // ---- preset grid (the nuancier) : larger rounded chips, full width, 2 rows ; a white ring marks the active colour ----
    for (int i = 0; i < 15; ++i) {
        const float sx = x + (i % PC) * (cw + cg), sy = preY + (i / PC) * (ch + cg);
        rrect_bordered(dev, sx, sy, cw, ch, snap(4.0f), CP_PRESETS[i], CP_PRESETS[i], C_BORDER, snap(1.0f));
        if (((*color) & 0x00FFFFFFu) == (CP_PRESETS[i] & 0x00FFFFFFu))
            rrect_stroke(dev, sx - snap(1.0f), sy - snap(1.0f), cw + snap(2.0f), ch + snap(2.0f), snap(5.0f), 0xFFFFFFFFu, snap(1.8f));
    }

    // ---- favourites : small label + a "+" add button (slot 0) + the saved swatches (hover a swatch shows a remove x) ----
    if (fo) { fo->begin(dev); fo->draw_lc(dev, x, favLabelY + snap(7.0f), tr("Favourites", "Favoris"), snap(11.0f), C_DIM, C_STROKE, 1.0f); }
    { const bool hov = inrect(mo, x, favY, cw, ch);                                     // "+" add button
      rrect_bordered(dev, x, favY, cw, ch, snap(4.0f), hov ? 0xFF2A343Cu : 0xFF171D22u, hov ? 0xFF2A343Cu : 0xFF171D22u, C_BORDERHI, snap(1.0f));
      const float pcx = x + cw * 0.5f, pcy = favY + ch * 0.5f, pr = snap(5.0f), pt = snap(1.6f);
      flat(dev, pcx - pr, pcy - pt * 0.5f, pr * 2.0f, pt, 0xFFCDD6DEu); flat(dev, pcx - pt * 0.5f, pcy - pr, pt, pr * 2.0f, 0xFFCDD6DEu); }
    for (int i = 0; i < ui_config().favColorN; ++i) {
        const int gp = i + 1; const float sx = x + (gp % PC) * (cw + cg), sy = favY + (gp / PC) * (ch + cg);
        const u32 fc = ui_config().favColors[i] | 0xFF000000u;
        rrect_bordered(dev, sx, sy, cw, ch, snap(4.0f), fc, fc, C_BORDER, snap(1.0f));
        if (((*color) & 0x00FFFFFFu) == (fc & 0x00FFFFFFu))
            rrect_stroke(dev, sx - snap(1.0f), sy - snap(1.0f), cw + snap(2.0f), ch + snap(2.0f), snap(5.0f), 0xFFFFFFFFu, snap(1.8f));
        if (fo && inrect(mo, sx, sy, cw, ch)) {                                         // hover : a small remove "x" in the top-right corner
            const float xr = sx + cw - snap(11.0f);
            flat(dev, xr, sy, snap(11.0f), snap(11.0f), 0xE0101418u);
            fo->begin(dev); fo->draw_c(dev, xr + snap(5.5f), sy + snap(5.5f), "x", snap(10.0f), 0xFFFFFFFFu, C_STROKE, 1.0f);
        }
    }
    return changed;
}

// A pill toggle chip. Modern: OFF = a neutral graphite pill ; ON = a SOLID teal fill with dark text.
// Hover smoothly lifts the surface + warms the border with a thin accent ring. No dot, no glass sweep.
bool toggle_chip(u32 dev, Font* fo, const MouseState* mo, bool click, int uid,
                        float x, float y, float w, float h, const char* label, bool on) {
    const bool hov = inrect(mo, x, y, w, h);
    const float st = ease(uid, 0, on ? 1.0f : 0.0f, 14.0f);          // on/off crossfade (sub 0)
    const float ht = ease(uid, 1, hov ? 1.0f : 0.0f);               // hover lift    (sub 1)
    const float r  = h * 0.5f;                                       // full pill
    // fill : neutral graphite (off) -> solid teal (on) ; hover lightens both a touch
    u32 ft = lerpc(0xFF1B2228, C_CHIP_ON_T, st), fb = lerpc(0xFF141A1F, C_CHIP_ON_B, st);
    ft = lerpc(ft, lerpc(0xFF2C363E, C_ACCENTHI, st), ht * 0.8f);        // hover clearly lifts the surface (visible on BOTH the dark OFF and the bright ON pill)
    fb = lerpc(fb, lerpc(0xFF20292F, C_ACCENT, st), ht * 0.8f);
    const u32 br = lerpc(lerpc(C_CTL_BR, C_ACCENTHI, st), 0xFFEAFBF9, ht);   // border -> near-white on hover : a clear cue even on the bright ON pill
    rpanel(dev, x, y, w, h, r, ft, fb, br, snap(1.4f));
    if (ht > 0.01f) { cs_add(dev); rrect_glow(dev, x, y, w, h, r, (C_ACCENTHI & 0x00FFFFFF) | ((u32)(70.0f * ht) << 24), snap(6.0f)); }   // clear accent ring on hover
    cs(dev);
    // text : legible on ANY pill. The ON fill follows the accent, which can now be a CUSTOM colour (possibly
    // DARK) -> pick dark text on a bright ON pill, light text on a dark one, each with a contrasting outline
    // (was : always near-black text + no outline -> dark-on-dark "baveux" once the accent went dark).
    const u32 onFill = lerpc(C_CHIP_ON_T, C_CHIP_ON_B, 0.5f);
    const int onL = (int)((((onFill >> 16) & 0xFF) * 54u + ((onFill >> 8) & 0xFF) * 183u + (onFill & 0xFF) * 19u) >> 8);
    const bool onBright = onL > 135;
    const u32 onTxt = onBright ? C_ONACC     : 0xFFF4F8F7u;          // dark-on-bright  |  light-on-dark
    const u32 onStk = onBright ? 0x66FFFFFFu : 0xFF000000u;          // contrasting outline in each case
    const u32 txt = lerpc(C_TEXT,   onTxt, st);
    const u32 stk = lerpc(C_STROKE, onStk, st);
    fo->begin(dev); fo->draw_c(dev, x + w * 0.5f, y + h * 0.5f, label, snap(12.0f), fa(txt), fa(stk), 1.0f);
    return hov && click;
}

// a wide push button (Edit Layout / Default) with eased hover + accent halo. tone : 0 = neutral blue,
// 1 = danger red. uid = animation slot. Returns true on click.
bool push_btn(u32 dev, Font* fo, const MouseState* mo, bool click, int uid,
                     float x, float y, float w, float h, const char* label, int tone) {
    const bool hov = inrect(mo, x, y, w, h);
    const bool press = hov && mo && mo->down;
    const float t = ease(uid, hov ? 1.0f : 0.0f);
    const u32 accBr = tone ? 0xFFE0555F : C_ACCENT, accHi = tone ? 0xFFFF8A92 : C_ACCENTHI;
    const u32 idleT = tone ? 0xFF241A1C : 0xFF1E252B, idleB = tone ? 0xFF1A1214 : 0xFF161C21;
    const u32 hovT  = tone ? 0xFF39262A : 0xFF27313A, hovB  = tone ? 0xFF281A1C : 0xFF1C232A;
    const float pin = press ? snap(1.0f) : 0.0f;                     // press : a small inward nudge
    const float bx = x + pin, by = y + pin, bw = w - 2 * pin, bh = h - 2 * pin, r = snap(9.0f);
    drop_shadow(dev, bx, by, bw, bh, snap(3.0f), press ? 30 : 54);
    if (t > 0.01f) { cs_add(dev); rrect_glow(dev, bx, by, bw, bh, r, (accBr & 0x00FFFFFF) | ((u32)(46.0f * t) << 24), snap(6.0f)); }   // soft accent ring on hover
    rpanel(dev, bx, by, bw, bh, r, lerpc(idleT, hovT, t), lerpc(idleB, hovB, t), lerpc(C_CTL_BR, accBr, t), snap(1.3f));
    flat(dev, bx + r, by + snap(1.0f), bw - 2.0f * r, 1.0f, ((u32)(26.0f + 22.0f * t) << 24) | 0x00FFFFFF);   // crisp 1px top hairline (not a glossy gradient)
    cs(dev);
    fo->begin(dev); fo->draw_c(dev, x + w * 0.5f, y + h * 0.5f, label, snap(13.0f), fa(lerpc(C_TEXT, accHi, t * 0.45f)), fa(C_STROKE), 1.0f);
    return hov && click;
}

// a collapsible CATEGORY header : a full-width gold bar with a triangle (right = collapsed, down = open) +
// label. Returns true on click (the caller toggles the open flag). uid = animation slot.
// the SOLID background "card" behind an OPEN category (drawn BEFORE its header + rows). The tab body is
// transparent, so THIS is what gives each menu a full, solid surface -- not the striped row bands.
void cat_panel(u32 dev, float x, float y, float w, float h) {
    if (h < snap(4.0f)) return;
    rpanel(dev, x, y, w, h, snap(9.0f), 0xF2141B22, 0xF20D1219, C_BORDER, snap(1.2f));   // solid graphite card
    flat(dev, x + snap(9.0f), y + snap(1.0f), w - snap(18.0f), 1, 0x12FFFFFF);           // faint top hairline
}
bool cat_header(u32 dev, Font* fo, const MouseState* mo, bool click, int uid, float x, float y, float w, const char* label, bool open) {
    const float h = snap(32.0f);
    const bool hov = inrect(mo, x, y, w, h);
    const float t = ease(uid, hov ? 1.0f : 0.0f);
    rpanel(dev, x, y, w, h, snap(7.0f), lerpc(0x6614302C, 0x99203A36, t), lerpc(0x660E1B19, 0x99152A28, t), lerpc(C_BORDER, C_ACCENT, t), snap(1.2f));   // clean teal surface on hover
    if (t > 0.01f) { cs_add(dev); rrect_glow(dev, x, y, w, h, snap(7.0f), (C_ACCENT & 0x00FFFFFF) | ((u32)(24.0f * t) << 24), snap(5.0f)); }   // thin accent ring
    cs(dev);                                                                              // back to the colour-quad state before the caret triangle
    const float gx = x + snap(15.0f), gy = y + h * 0.5f, s = snap(4.0f);
    if (open) { const float d[6] = { gx - s, gy - s * 0.55f,  gx + s, gy - s * 0.55f,  gx, gy + s * 0.85f };   // down triangle (AA)
                fill_poly_aa(dev, d, 3, fa(C_ACCENTHI)); }
    else      { const float d[6] = { gx - s * 0.55f, gy - s,  gx - s * 0.55f, gy + s,  gx + s * 0.85f, gy };   // right triangle (AA)
                fill_poly_aa(dev, d, 3, fa(C_ACCENTHI)); }
    fo->begin(dev); fo->draw_lc(dev, x + snap(30.0f), gy, label, snap(13.0f), fa(C_TEXT), fa(C_STROKE), 1.3f);
    return hov && click;
}


} // namespace aio
