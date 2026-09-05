// config_controls.cpp -- see config_controls.h. The shared immediate-mode config toolkit
// (palette state + animation springs + AA primitives + the labeled controls), lifted out of
// config_page.cpp so every module's *_config.cpp draws with the same controls and ease() namespaces.
#include "ui/config_controls.h"
#include "gfx/window.h"      // window_tex_theme_count / window_theme_name (the FFXI skins)
#include "ui/box_style.h"       // box_hue_count / box_hue_color (the procedural families)
#include "gfx/draw.h"          // grad_quad, rrect, soft_blob, rrect_glow, disc, disc_glow, seg_soft, fill_tri, tquad, dSet*
#include "model/ui_config.h"   // ui_config(), save_ui_config() (row_slider persists on release)
#include "windower_debug.h"    // debug::log : ease()'s spring table says so when it fills (rule 10 corollary)
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
struct Anim { int id; int sub; float v; float vel; };   // vel : only a spring uses it ; ease() leaves it at 0
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
// Make a CUSTOM accent usable on the black chrome without bleaching it.
//
// The rule here used to be: mix the colour toward WHITE until its luma reached 150. Two things were wrong with
// that. Mixing toward white removes SATURATION -- that IS what washed out MEANS -- and by luma every saturated hue is
// dark (a pure red is 104, a pure blue 29), so the rule fired on nearly every colour anyone would actually pick
// and handed back a pastel of it: EF4444 became F37C7C, 8B5CF6 became A986F8. The whole menu then ran on that
// pastel, and every surface derived from it (selected tab, selected row, chips) inherited the lost contrast.
//
// Two steps instead, in this order:
//   1. lift the VALUE to a floor -- multiply the three channels by kVmin/max when max is below it. Hue and
//      saturation are untouched by construction (V scales all three equally), so a deep blood red becomes a
//      VIVID red, never a pink -- and a colour that was already bright comes back byte-for-byte unchanged.
//   2. only if it is STILL too dark to read as a foreground -- the deep blues and violets, whose value was
//      already at the top -- mix toward white, and only as far as luma 96. A real floor for text on graphite,
//      not the 150 that was bleaching hues which had no legibility problem in the first place.
// Presets stay untouched (they are designed legible, and their nuance chart NEEDS its deep row to stay deep --
// normalising row 2 would hand back row 1 and collapse three choices into two).
static u32 accent_normalise(u32 a) {
    int r = (int)((a >> 16) & 0xFF), g = (int)((a >> 8) & 0xFF), b = (int)(a & 0xFF);
    // Lift to a FLOOR, not to full. Full value would repaint a mid green as a neon one -- the accent has to
    // stay the colour that was picked. 219 leaves every vivid choice byte-for-byte untouched (EF4444, 3B82F6,
    // 8B5CF6 all have a channel above it) and only lifts what is genuinely too dim to carry the chrome.
    const int kVmin = 219;
    const int mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
    if (mx > 0 && mx < kVmin) { r = r * kVmin / mx; g = g * kVmin / mx; b = b * kVmin / mx; }   // value -> the floor, ratios kept
    u32 c = 0xFF000000u | ((u32)r << 16) | ((u32)g << 8) | (u32)b;
    const int L = (r * 54 + g * 183 + b * 19) >> 8;
    if (L < 96) { float f = (96.0f - (float)L) / (255.0f - (float)L); if (f > 0.5f) f = 0.5f; c = shade(c, f); }
    return c;
}
void apply_ui_theme(int style, int color) {
    u32 a;
    if (ui_config().uiAccent & 0xFF000000u) {                            // custom accent wins over the style/colour preset
        a = accent_normalise(ui_config().uiAccent);
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

// ---- LABEL ON A COLOURED FILL ------------------------------------------------------------------------------
// The accent is whatever colour the user picked, so any button that hard-codes its label colour is only right
// for half the palette : dark text vanishes on a dark accent, white text vanishes on a bright one. One rule,
// one place. `fill` is the fill the text sits on (for a gradient, pass its MIDPOINT -- that is what the eye
// averages behind a glyph) ; `stroke` gets the matching outline, which is the other half of the contrast.
bool fill_is_bright(u32 c) {
    const int L = (int)((((c >> 16) & 0xFF) * 54u + ((c >> 8) & 0xFF) * 183u + (c & 0xFF) * 19u) >> 8);
    return L > 135;
}
u32 text_on_fill(u32 fill, u32* stroke) {
    const bool bright = fill_is_bright(fill);
    if (stroke) *stroke = bright ? 0x66FFFFFFu : 0xFF000000u;   // light halo under dark text | black under light text
    return bright ? C_ONACC : 0xFFF4F8F7u;
}

// COMPOSITE key (id, sub) : a control keys its N springs on (its unique CTRL_ID, 0..N-1) so no two controls can
// ever share a slot -- no arithmetic offsets (uid+1 / 40+uid / 2000+uid*2) that could overlap. Legacy 2-arg callers
// (party / edit_box / textures) map to sub 0, and their small hand-picked ids never meet a CTRL_ID hash.
float ease(int id, int sub, float target, float speed) {
    Anim* s = nullptr;
    for (int i = 0; i < g_animN; ++i) if (g_anim[i].id == id && g_anim[i].sub == sub) { s = &g_anim[i]; break; }
    if (!s) {
        // SAY SO when the budget is spent (rule 10's corollary). Silently returning `target` means the control
        // appears fully-formed with no transition while its neighbours still animate -- a symptom nobody would
        // trace back to a full table. Probably unreachable today (entries are never recycled, so the count
        // tracks DISTINCT controls visited, not time), but an unreachable budget that dies quietly reads exactly
        // like a bug that isn't happening. One line, once per saturation. buff_atlas.cpp / minimap.cpp already
        // do this for theirs ; this was the last silent one.
        if (g_animN >= ANIM_MAX) {
            static bool full = false;
            if (!full) { full = true; windower::debug::log("ease(): animation table FULL (%d springs) -- new controls will snap instead of animating", ANIM_MAX); }
            return target;
        }
        s = &g_anim[g_animN++]; s->id = id; s->sub = sub; s->v = target; s->vel = 0.0f;
    }
    s->v += (target - s->v) * clampf(g_dt * speed, 0.0f, 1.0f);
    return s->v;
}
float ease(int id, float target, float speed) { return ease(id, 0, target, speed); }
float spring(int id, int sub, float target, float stiffness, float damping) {
    Anim* s = nullptr;
    for (int i = 0; i < g_animN; ++i) if (g_anim[i].id == id && g_anim[i].sub == sub) { s = &g_anim[i]; break; }
    if (!s) {
        if (g_animN >= ANIM_MAX) return target;   // budget spent : snap, and ease() already says so once in the log
        s = &g_anim[g_animN++]; s->id = id; s->sub = sub; s->v = target; s->vel = 0.0f;
    }
    // Semi-implicit Euler, with dt CLAMPED. A long frame (a zone load, a stall) would otherwise integrate a huge
    // step and fling the value across the screen -- the classic way a spring explodes in a game loop.
    float dt = g_dt; if (dt < 0.0f) dt = 0.0f; if (dt > 0.033f) dt = 0.033f;
    s->vel += ((target - s->v) * stiffness - s->vel * damping) * dt;
    s->v   += s->vel * dt;
    return s->v;
}
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
    dSetRS(dev, D3DRS_SHADEMODE, D3DSHADE_GOURAUD);   // vertex colours must INTERPOLATE : a stray FLAT from the
    dSetRS(dev, D3DRS_DITHERENABLE, 1);               // game breaks every gradient along its quad's diagonal.
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
    dSetRS(dev, D3DRS_SHADEMODE, D3DSHADE_GOURAUD);   // same two as cs() : a glow IS a gradient, and the
    dSetRS(dev, D3DRS_DITHERENABLE, 1);               // additive passes are where banding shows the most.
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
    // NO rule under every row. Sections sit on their own card, so the grouping is already done by the surface ;
    // a line under each row on top of that is the belt-and-braces look that dates an interface, and it was the
    // reason a page of settings read as a spreadsheet. Fading it was not enough -- a faint wrong thing is still
    // the wrong thing. Rows are separated by their own height and by the contrast between label and control.
}

// ---- rectangular stencil CLIP for the scrolling controls viewport (D3D8 has no scissor rect ; same
// technique as the vial's rounded clip). Everything drawn between begin/end is masked to the rect.
// If the back-buffer has no stencil the ops are ignored -> the column just overflows as before (no crash). ----
enum { SCL_ENABLE = 52, SCL_FAIL = 53, SCL_ZFAIL = 54, SCL_PASS = 55, SCL_FUNC = 56, SCL_REF = 57, SCL_MASK = 58, SCL_WRITEMASK = 59 };
// ---- NESTED stencil clipping. ----
// This used to be a one-level scissor: begin() cleared its region to 0 and wrote 1 inside, end() switched the
// stencil off. Fine while nothing nested -- and the moment something did, the INNER end() dropped the OUTER
// clip for the rest of the frame. That is not hypothetical: the module content is drawn inside a scroll
// viewport clip, and a folding section clips inside that, so a tall page's overflow stopped being contained
// after the first section.
// It counts DEPTH now. Level 1 clears and writes 1, as before. Each deeper level INCREMENTS the stencil inside
// its own rect but only where the parent's value already stands -- so a child can only ever shrink its parent's
// region, never escape it -- and the test is "equal to my depth". end() DECREMENTS the same rect back and
// restores the parent's test, which is why the rects are kept: you cannot undo a region you have forgotten.
enum { STOP_KEEP = 1, STOP_REPLACE = 3, STOP_INCRSAT = 4, STOP_DECRSAT = 5, SCMP_EQUAL = 3, SCMP_ALWAYS = 8 };
static struct ClipRect { float x, y, w, h; } g_clipStack[6];
static int g_clipDepth = 0;
static int g_clipSkipped = 0;   // begins refused for want of depth ; their end() must still be swallowed

void clip_rect_begin(u32 dev, float x, float y, float w, float h) {
    if (g_clipDepth >= (int)(sizeof(g_clipStack) / sizeof(g_clipStack[0]))) {
        // Budget spent. SAY SO once (rule 10's corollary): silently not clipping looks like a layout bug
        // somewhere else entirely, which is a long way from here.
        static bool full = false;
        if (!full) { full = true; windower::debug::log("clip_rect_begin(): nesting too deep (%d) -- this clip is a no-op", g_clipDepth); }
        ++g_clipSkipped;   // its end() is coming regardless, and must NOT pop a level it never pushed
        return;
    }
    const int d = g_clipDepth;
    dSetVS(dev, FVF_XYZRHW_DIFFUSE); dSetTex(dev, 0, 0);
    dSetRS(dev, D3DRS_ALPHATESTENABLE, 0);
    dSetRS(dev, D3DRS_ALPHABLENDENABLE, 0);
    dSetRS(dev, SCL_ENABLE, 1);
    dSetRS(dev, SCL_MASK, 0xFF); dSetRS(dev, SCL_WRITEMASK, 0xFF);
    dSetRS(dev, D3DRS_COLORWRITEENABLE, 0);                        // mask pass : write stencil only, no colour
    if (d == 0) {
        dSetRS(dev, SCL_FUNC, SCMP_ALWAYS);
        dSetRS(dev, SCL_FAIL, STOP_REPLACE); dSetRS(dev, SCL_ZFAIL, STOP_REPLACE); dSetRS(dev, SCL_PASS, STOP_REPLACE);
        dSetRS(dev, SCL_REF, 0);
        grad_quad(dev, x - 2.0f, y - 2.0f, w + 4.0f, h + 4.0f, 0, 0, 0, 0);          // clear the region -> 0
        dSetRS(dev, SCL_REF, 1);
        grad_quad(dev, x, y, w, h, 0xFF000000, 0xFF000000, 0xFF000000, 0xFF000000);  // set 1 inside the rect
    } else {
        dSetRS(dev, SCL_FUNC, SCMP_EQUAL); dSetRS(dev, SCL_REF, d);                  // only where the parent stands
        dSetRS(dev, SCL_FAIL, STOP_KEEP); dSetRS(dev, SCL_ZFAIL, STOP_KEEP); dSetRS(dev, SCL_PASS, STOP_INCRSAT);
        grad_quad(dev, x, y, w, h, 0xFF000000, 0xFF000000, 0xFF000000, 0xFF000000);
    }
    g_clipStack[d].x = x; g_clipStack[d].y = y; g_clipStack[d].w = w; g_clipStack[d].h = h;
    g_clipDepth = d + 1;

    dSetRS(dev, D3DRS_COLORWRITEENABLE, 0x0000000F);               // content : colour on, ONLY at my depth
    dSetRS(dev, SCL_FUNC, SCMP_EQUAL); dSetRS(dev, SCL_REF, g_clipDepth);
    dSetRS(dev, SCL_FAIL, STOP_KEEP); dSetRS(dev, SCL_ZFAIL, STOP_KEEP); dSetRS(dev, SCL_PASS, STOP_KEEP);
    dSetRS(dev, D3DRS_ALPHABLENDENABLE, 1);
}
void clip_rect_end(u32 dev) {
    if (g_clipSkipped > 0) { --g_clipSkipped; return; }   // the matching begin was refused : leave the stencil alone
    if (g_clipDepth <= 0) { dSetRS(dev, SCL_ENABLE, 0); dSetRS(dev, D3DRS_COLORWRITEENABLE, 0x0000000F); return; }
    const int d = --g_clipDepth;
    if (d == 0) {                                                  // outermost : just switch the test off
        dSetRS(dev, SCL_ENABLE, 0);
        dSetRS(dev, D3DRS_COLORWRITEENABLE, 0x0000000F);
        dSetRS(dev, D3DRS_ALPHATESTENABLE, 0);                     // restore what begin() turned off
        return;
    }
    const ClipRect r = g_clipStack[d];                             // undo exactly the region this level added
    dSetVS(dev, FVF_XYZRHW_DIFFUSE); dSetTex(dev, 0, 0);
    dSetRS(dev, D3DRS_ALPHABLENDENABLE, 0);
    dSetRS(dev, D3DRS_COLORWRITEENABLE, 0);
    dSetRS(dev, SCL_FUNC, SCMP_EQUAL); dSetRS(dev, SCL_REF, d + 1);
    dSetRS(dev, SCL_FAIL, STOP_KEEP); dSetRS(dev, SCL_ZFAIL, STOP_KEEP); dSetRS(dev, SCL_PASS, STOP_DECRSAT);
    grad_quad(dev, r.x, r.y, r.w, r.h, 0xFF000000, 0xFF000000, 0xFF000000, 0xFF000000);
    dSetRS(dev, D3DRS_COLORWRITEENABLE, 0x0000000F);               // ... and hand the parent's test back
    dSetRS(dev, SCL_FUNC, SCMP_EQUAL); dSetRS(dev, SCL_REF, d);
    dSetRS(dev, SCL_FAIL, STOP_KEEP); dSetRS(dev, SCL_ZFAIL, STOP_KEEP); dSetRS(dev, SCL_PASS, STOP_KEEP);
    dSetRS(dev, D3DRS_ALPHABLENDENABLE, 1);
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
// round the TOP corners only (tabs : the bottom melts into the body). The SHAPE is gfx's rrect_topcaps --
// this is the config-page wrapper that adds the two things every fill here needs: the colour-quad state and
// the page's fade. It used to draw the shape itself out of four bands and two flat qfans, with no half-pixel
// offset and a feather on the arcs but not on the straight edges. That is what the faceted, slightly soft
// corners were -- four separate defects, all of them fixed by using the same recipe as every other shape.
void rrect_top(u32 dev, float x, float y, float w, float h, float r, u32 top, u32 bot) {
    cs(dev);
    rrect_topcaps(dev, x, y, w, h, r, fa(top), fa(bot));
}

// a bordered rounded panel : border ring + inner gradient fill (opaque-friendly).
void rpanel(u32 dev, float x, float y, float w, float h, float r, u32 top, u32 bot, u32 border, float bt) {
    rrect_fill(dev, x, y, w, h, r, border, border);
    rrect_fill(dev, x + bt, y + bt, w - 2 * bt, h - 2 * bt, (r - bt > 0.0f ? r - bt : 0.0f), top, bot);
}
// A drop shadow under an element (draw BEFORE it) -> floats it off the page.
//
// The old one was a soft_blob : a bilinear TENT, peak at the CENTRE, falling linearly to zero at its own
// edges. The peak therefore sat UNDER the opaque element and what escaped past the edge was the tail. The
// arithmetic is brutal -- alpha_at_edge = alpha * spread / (w/2 + spread) -- so a 1000px section card with
// alpha 64 and spread 5 put 0.6/255 on the screen, and a 140px button 2.2/255. Ten call sites, all of them
// drawing nothing, and worse for bigger elements: exactly backwards. This was not a value that needed
// raising ; the shape was wrong.
//
// rrect_glow is the right primitive and was already here: a feathered band from the silhouette outward, no
// interior fill, so ALL of it lands outside the element where a shadow belongs. Two passes, because that is
// what elevation looks like -- a tight CONTACT shadow just under the edge that says the thing is resting on
// something, and a wide AMBIENT one further down that says how far above.
void drop_shadow(u32 dev, float x, float y, float w, float h, float spread, u32 alpha, float r) {
    if (w <= 0.0f || h <= 0.0f || alpha == 0) return;
    if (r > w * 0.5f) r = w * 0.5f;
    if (r > h * 0.5f) r = h * 0.5f;
    if (r < 0.0f) r = 0.0f;
    const float k = clampf((float)alpha / 64.0f, 0.0f, 2.0f) * g_fade;   // callers pass 30..110 ; 64 = a normal card
    if (k <= 0.01f) return;
    cs(dev);
    rrect_glow(dev, x, y + snap(4.0f), w, h, r, (u32)(44.0f * k) << 24, spread + snap(10.0f));   // ambient
    rrect_glow(dev, x, y + snap(2.0f), w, h, r, (u32)(86.0f * k) << 24, spread * 0.6f + snap(2.0f));   // contact
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
    fo->draw_lc(dev, x + snap(4.0f), y + rowH * 0.5f, label, ts_label(), fa(C_TEXT), fa(C_STROKE), 1.0f);

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

    rpanel(dev, cx, cy, ctlW, aH, r, 0x8C1B242B, 0x8C121920, lerpc(0x22FFFFFFu, C_ACCENT, anyT), snap(1.0f));   // lighter surface, fainter edge : the CONTROL should read, not its fence
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

// Release the drag latch from OUTSIDE the control, and persist what it was editing.
// The latch is only ever cleared by the dragged row being drawn again with the button up -- so closing the
// config page (or switching tab) mid-drag strands it: the row stops being drawn, `g_slider` stays set, and
// the `g_slider < 0` gate then blocks EVERY slider and colour picker in the whole menu, with nothing on
// screen to explain it. The value is lost too, because the release branch is what calls save_ui_config().
// This was harmless while each panel also saved on every drag frame ; removing those 11 redundant saves is
// what exposed it.
void ctrl_release_drag() {
    if (g_slider < 0) return;
    g_slider = -1;
    save_ui_config();   // the release branch never ran -> persist the value the drag left in memory
}
// ---- the latch, reused by a NON-value drag (the buff-strip reorder in party_config.cpp). ----
// Deliberately the same `g_slider`, not a second variable : two independent latches would let a strip drag and
// a slider drag run at once, which is the whole failure mode the latch was added for. ctrl_release_drag()
// therefore also frees a stranded strip drag when the page stops being drawn.
bool ctrl_drag_begin(int id, const MouseState* mo, bool hot) {
    if (!(mo && mo->clicked && hot && g_slider < 0)) return false;
    g_slider = id; return true;
}
bool ctrl_drag_active(int id) { return g_slider == id; }
bool ctrl_drag_any() { return g_slider >= 0; }
bool ctrl_drag_end(int id, const MouseState* mo) {
    if (g_slider != id) return false;
    if (mo && mo->down) return false;      // still held
    g_slider = -1; return true;            // the caller persists what the drop changed -- no blanket save here
}

bool row_slider(u32 dev, Font* fo, const MouseState* mo, int id,
                       float x, float y, float w, const char* label, const char* valueText, float* v01) {
    const float rowH = snap(40.0f);
    fo->begin(dev);
    fo->draw_lc(dev, x + snap(4.0f), y + rowH * 0.5f, label, ts_label(), fa(C_TEXT), fa(C_STROKE), 1.0f);

    const float valW = snap(58.0f), gap = snap(12.0f), trkW = snap(196.0f);
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
    // The GROOVE reads recessed and the FILL reads lit -- one 1px hairline each, which is the whole difference
    // between a track that looks cut into the panel and two stacked coloured pills.
    rrect_fill(dev, trkX, trkY, trkW, trkH, tr, 0x77090C10, 0x77050709);
    flat(dev, trkX + tr, trkY, trkW - 2.0f * tr, 1.0f, 0x55000000);                        // inner top shadow
    if (fillW >= trkH) {
        rrect_fill(dev, trkX, trkY, fillW, trkH, tr, C_ACCENTHI, C_ACCENT);                    // accent fill
        flat(dev, trkX + tr, trkY + snap(1.0f), fillW - 2.0f * tr, 1.0f, 0x3AFFFFFF);      // lit top hairline
    }
    rrect_stroke(dev, trkX, trkY, trkW, trkH, tr, fa(0x2AFFFFFF), snap(1.0f));                 // a border that hugs the capsule

    // clean round knob : a STEEL rim around a white face -- the rim is what stops it dissolving into a light
    // accent fill -- easing bigger on hover/drag with a soft accent halo and a real shadow. No gloss.
    const float kt = ease(id, 0, (hot || act) ? 1.0f : 0.0f);
    const float kr = knobR * (1.0f + 0.18f * kt);
    const float kx = trkX + fillW;
    if (kt > 0.01f) { cs_add(dev); disc_glow(dev, kx, cy, kr + snap(1.2f), (C_ACCENT & 0x00FFFFFF) | ((u32)(78.0f * kt) << 24), snap(7.0f)); }   // soft accent ring
    cs(dev);
    disc(dev, kx, cy + snap(1.4f), kr + snap(1.2f), fa(0x5E000000));                           // drop shadow
    disc(dev, kx, cy, kr + snap(1.1f), fa(lerpc(C_STEEL_DEEP, C_STEEL_HI, kt)));               // steel rim
    disc(dev, kx, cy, kr, fa(0xFFF7FAFB));                                                     // face

    // the value as a READOUT, not a floating number : the same dark plate + steel hairline as every other
    // readout on the page, so a column of sliders lines up on something instead of on ragged text.
    const float pw = valW, ph = snap(22.0f), px = trkX + trkW + gap, py = cy - ph * 0.5f;
    rrect_fill(dev, px, py, pw, ph, snap(6.0f), 0x66101519, 0x660A0E11);
    rrect_stroke(dev, px, py, pw, ph, snap(6.0f), fa((C_STEEL_DEEP & 0x00FFFFFF) | 0x70000000), snap(1.0f));
    fo->begin(dev);
    fo->draw_c(dev, px + pw * 0.5f, cy, valueText, ts_value(), fa(C_TEXT), fa(C_STROKE), 1.0f);
    return changed;
}

// A labeled ON/OFF toggle row : "Label .......... [On]". Flips *field and persists on change ; returns true on the
// click. Shared body of the old per-panel *_TOGGLE macros (they were byte-identical). Call inside a ROW_BAND(48) block,
// passing y = ry + yo. `field` is the int 0/1 config flag.
bool row_choice(u32 dev, Font* fo, const MouseState* mo, bool click, int uid,
                float coX, float y, float ctrlW, const char* label, int* field,
                const char* onText, const char* offText, float rowHf, float chipWf) {
    const float rowH = snap(rowHf);
    fo->begin(dev);
    fo->draw_lc(dev, coX + snap(4.0f), y + rowH * 0.5f, label, snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
    const float bbw = snap(chipWf), bbh = snap(34.0f), bx2 = coX + ctrlW - bbw, bty = y + (rowH - bbh) * 0.5f;
    if (toggle_chip(dev, fo, mo, click, uid, bx2, bty, bbw, bbh, *field ? onText : offText, *field != 0)) {
        *field = !*field; save_ui_config(); return true;
    }
    return false;
}
bool row_toggle(u32 dev, Font* fo, const MouseState* mo, bool click, int uid,
                float coX, float y, float ctrlW, const char* label, int* field,
                float rowHf, float chipWf) {
    return row_choice(dev, fo, mo, click, uid, coX, y, ctrlW, label, field, tr("On", "Oui"), tr("Off", "Non"), rowHf, chipWf);
}
// A labeled percent slider bound to a float *field in [lo,hi], shown as "NN%", stepped to `step`. Persists on RELEASE
// (via row_slider) -- do NOT add a save here (that was the per-drag-frame save bug some panels had). Shared body of the
// old *_PCT_SLIDER / *_SIZE_SLIDER macros. Call inside a ROW_BAND(46) block, passing y = ry + yo.
// bool* fields (party's animHP/animTP, the per-tier flags...) : convert, delegate, convert back. Deliberately
// NOT a second copy of the row -- the drawing and the save live in exactly one place.
bool row_toggle(u32 dev, Font* fo, const MouseState* mo, bool click, int uid,
                float coX, float y, float ctrlW, const char* label, bool* field,
                float rowHf, float chipWf) {
    int v = *field ? 1 : 0;
    const bool changed = row_toggle(dev, fo, mo, click, uid, coX, y, ctrlW, label, &v, rowHf, chipWf);
    if (changed) *field = (v != 0);
    return changed;
}

bool row_pct_slider(u32 dev, Font* fo, const MouseState* mo, int uid,
                    float coX, float y, float ctrlW, const char* label, float* field, float lo, float hi, float step) {
    char b[16]; _snprintf(b, sizeof(b), "%d%%", (int)(*field * 100.0f + 0.5f)); b[sizeof(b) - 1] = 0;
    float v01 = clampf((*field - lo) / (hi - lo), 0.0f, 1.0f);
    if (row_slider(dev, fo, mo, uid, coX, y, ctrlW, label, b, &v01)) {
        float v = lo + v01 * (hi - lo);
        v = (float)((int)(v / step + 0.5f)) * step;
        *field = clampf(v, lo, hi);
        return true;
    }
    return false;
}

// ---- HSV colour picker (shared) -- ONE CARD, two halves. Top : the instruments (SV square + a VERTICAL hue
// strip beside it, the arrangement every serious picker uses) with the live swatch and the favourites in the
// column to their right. Bottom : the nuancier, a real chart. Replaces the per-channel R/G/B slider triples in
// every module. The two draggable zones share the g_slider latch. HSV is CACHED per-uid so dragging Value to
// black or Saturation to 0 doesn't lose the hue (RGB can't encode it).
//
// The pieces used to sit loose on the row band -- a 112 square, a full-width hue bar under it, a preset grid
// under that, favourites under that -- four stacked strips with the whole right half of the row empty. They are
// now one panel with its own padding and border, so the picker reads as a single instrument instead of four
// things that happen to be adjacent.
//
// METRICS live here, ONCE : color_picker_height() and the body both derive from them and cannot drift. The one
// coupling to respect is that the height depends on the chart's CHIP size, so the chip is fixed and the GAP
// absorbs the available width -- not the other way round.
namespace {
const float CP_MAXW   = 420.0f;   // the card is a card : it never stretches across a 560+ wide control column
const float CP_PAD    = 14.0f;    // card padding
const float CP_SQ     = 124.0f;   // SV square -- and the height of the whole instrument row
const float CP_HUEW   = 18.0f;    // vertical hue strip
const float CP_GAP    = 10.0f;    // SV <-> hue strip
const float CP_COLGAP = 18.0f;    // instruments <-> right column
const float CP_SWH    = 44.0f;    // live swatch tile
const float CP_ROWGAP = 16.0f;    // instrument row <-> nuancier
const float CP_FCH    = 22.0f, CP_FGAP = 5.0f;   // favourite chip height + gap
const int   CP_FAVC   = 8;                       // favourites per row ("+" takes slot 0 -> 16 slots -> 2 rows, always)
const float CP_CH     = 24.0f, CP_CGAP = 6.0f;   // nuancier chip + gap
const int   CP_COLS   = 13, CP_ROWS = 3;         // 12 hues + 1 neutral column, x tint/base/shade
}
float color_picker_height() {
    // CONSTANT, and deliberately so : it used to grow a row the moment a 9th favourite was saved, which shifted
    // every row below the picker under the user's cursor mid-click. The favourites now live in the right column
    // beside the SV square, where both possible rows always fit.
    return snap(CP_PAD) * 2.0f + snap(CP_SQ) + snap(CP_ROWGAP)
         + (float)CP_ROWS * snap(CP_CH) + (float)(CP_ROWS - 1) * snap(CP_CGAP);
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
    // SATURATION IS ANNOUNCED, like ease()'s spring table right above. Returning 0 here makes color_picker()
    // bail before its first quad, so the picker draws NOTHING -- a ~249px hole in the panel, permanently, with
    // no log line anywhere. Slots are never recycled, so the count is "distinct pickers visited since load":
    // whether it happens at all depends on the order the user opened panels in. 19 of 24 are in use today.
    if (g_pickN >= 24) {
        static bool warned = false;
        if (!warned) { warned = true; windower::debug::log("colour-picker slot table FULL (24) -- further pickers will NOT be drawn this session"); }
    }
    if (g_pickN < 24) {PickHSV& p = g_pick[g_pickN]; p.uid = uid; rgb2hsv(col, p.h, p.s, p.v); p.col = col; return &g_pick[g_pickN++]; }
    return 0;
}

// ---- the NUANCIER : a real chart -- one HUE per column, three LIGHTNESS rows (tint / base / shade), plus a
// neutral column for white / steel / near-black. Deliberately the SAME grammar as the theme chart in the
// Interface panel (config_page.cpp) : same chip, same three-row nuance idea, so the menu's two colour surfaces
// read as one system. The old nuancier was 15 flat chips -- no light or deep variant of anything, so every
// pastel or muted tone had to be hand-dialled in the SV square even though `nuance()` was right there.
static const u32 CP_HUE[CP_COLS] = {
    0xFFEF4444u, 0xFFF97316u, 0xFFF59E0Bu, 0xFFEAB308u, 0xFF84CC16u, 0xFF22C55Eu,
    0xFF14B8A6u, 0xFF06B6D4u, 0xFF3B82F6u, 0xFF6366F1u, 0xFF8B5CF6u, 0xFFEC4899u,
    0xFFFFFFFFu,   // last column = NEUTRAL : its three rows come from CP_NEUTRAL, not from nuance()
};
// The neutral column can't be a nuance ramp : shade(white, +0.42) is still white, so rows 0 and 1 would be the
// same chip. Spelt out instead -- and these three (paper white, steel grey, near-black) are the neutrals people
// actually pick for text and outlines.
static const u32 CP_NEUTRAL[CP_ROWS] = { 0xFFFFFFFFu, 0xFF9AA5B1u, 0xFF0A0D10u };
static u32 cp_chart(int col, int row) {
    return (col == CP_COLS - 1) ? CP_NEUTRAL[row] : nuance(CP_HUE[col], row);
}
// A swatch chip, drawn the one way : rounded fill with its own darker underside, a border that hugs the round,
// a white ring + a glow of its own colour when it IS the current colour, and a small lift under the pointer.
void cp_swatch(u32 dev, float x, float y, float w, float h, u32 c, bool sel, float hov) {
    const float r = snap(7.0f), lift = snap(1.5f) * hov;
    const float X = x - lift, Y = y - lift, W = w + 2.0f * lift, H = h + 2.0f * lift;
    if (sel) { cs_add(dev); rrect_glow(dev, X, Y, W, H, r, (c & 0x00FFFFFFu) | 0x88000000u, snap(7.0f)); cs(dev); }
    // The same relief as a button, scaled down : an edging that is a NUANCE OF THE SWATCH ITSELF (light rim on
    // a dark colour, dark rim on a bright one -- which is the only rule that works across a chart holding both
    // white and near-black), the fill, a sliver of glass, and the lamp. It replaces a flat 1px stroke that was
    // the same grey on all thirty-nine.
    // NO GLASS AND NO LAMP ON A SWATCH. A chip like this one IS a colour -- it exists so you can read the
    // colour off it -- and any highlight laid on top becomes part of what you read. A white sheen vanished on
    // white and washed out everything in between ; the lamp had to be dark on a bright chip, which puts a
    // SHADOW along the top edge and lights the swatch from underneath. Both were the reflex of treating a
    // swatch as a button. It is not one: its relief lives entirely OUTSIDE the colour -- the shadow it casts,
    // the rim around it (a nuance of the swatch itself), and the lift it takes under the pointer.
    drop_shadow(dev, X, Y, W, H, snap(2.0f), sel ? 60 : (u32)(30.0f + 34.0f * hov), r);
    ctl_edge(dev, X, Y, W, H, r, sel ? 1.0f : (0.62f + 0.34f * hov), c, c);
    rrect_fill(dev, X, Y, W, H, r, c, shade(c, -0.28f));
    if (sel) rrect_stroke(dev, X, Y, W, H, r, 0xFFFFFFFFu, snap(1.7f));   // the white ring says WHICH one
}

bool color_picker(u32 dev, Font* fo, const MouseState* mo, int uidSV, int uidHue,
                  float x, float y, float w, u32* color) {
    if (!color) return false;
    PickHSV* p = pick_slot(uidSV, *color);
    if (!p) return false;
    const u32 alpha = *color & 0xFF000000u;

    // ---- layout ----------------------------------------------------------------------------------------
    const float W    = (w > snap(CP_MAXW)) ? snap(CP_MAXW) : w;
    const float H    = color_picker_height();
    const float pad  = snap(CP_PAD);
    const float cx0  = x + pad, cy0 = y + pad, cw0 = W - 2.0f * pad;      // content box inside the card
    const float sq   = snap(CP_SQ), hw = snap(CP_HUEW);
    const float hueX = cx0 + sq + snap(CP_GAP);
    const float rx   = hueX + hw + snap(CP_COLGAP), rw = cx0 + cw0 - rx;  // right column (swatch + favourites)
    const float swH  = snap(CP_SWH);
    const float favLabY = cy0 + swH + snap(12.0f), favY = favLabY + snap(15.0f);
    const float fch  = snap(CP_FCH), fgap = snap(CP_FGAP);
    const float fcw  = snap((rw - (float)(CP_FAVC - 1) * fgap) / (float)CP_FAVC);   // SNAPPED : every chip x is a multiple of it
    const float cch  = snap(CP_CH);                                       // nuancier : chip FIXED (height() knows it)
    // The gap absorbs the width, TRUNCATED not rounded : rounding it up widens the grid past the content box
    // (13 chips multiply the error by twelve) and the chart then hangs over the card's padding on both sides.
    const float ccg  = (float)(int)clampf((cw0 - (float)CP_COLS * cch) / (float)(CP_COLS - 1), 2.0f, 10.0f);
    const float gridW = (float)CP_COLS * cch + (float)(CP_COLS - 1) * ccg;
    const float gx   = snap(cx0 + (cw0 - gridW) * 0.5f), chartY = cy0 + sq + snap(CP_ROWGAP);
    const u32   hue  = hsv2rgb(p->h, 1.0f, 1.0f, 0xFF000000u);

    // ---- interaction : SV square + hue strip hold the row_slider latch ; chips are one-shot clicks --------
    const bool hotSV  = inrect(mo, cx0, cy0, sq, sq);
    const bool hotHue = inrect(mo, hueX, cy0, hw, sq);
    if (mo && mo->clicked && g_slider < 0) { if (hotSV) g_slider = uidSV; else if (hotHue) g_slider = uidHue; }
    bool changed = false;
    if (g_slider == uidSV) {
        if (mo && mo->down) { p->s = clampf((mo->x - cx0) / sq, 0.0f, 1.0f); p->v = clampf(1.0f - (mo->y - cy0) / sq, 0.0f, 1.0f);
                              const u32 nc = hsv2rgb(p->h, p->s, p->v, alpha); if (nc != *color) { *color = nc; p->col = nc; changed = true; } }
        else { g_slider = -1; save_ui_config(); }
    } else if (g_slider == uidHue) {
        if (mo && mo->down) { p->h = clampf((mo->y - cy0) / sq, 0.0f, 1.0f) * 360.0f;   // VERTICAL strip -> Y maps to hue
                              const u32 nc = hsv2rgb(p->h, p->s, p->v, alpha); if (nc != *color) { *color = nc; p->col = nc; changed = true; } }
        else { g_slider = -1; save_ui_config(); }
    }
    if (mo && mo->clicked && g_slider < 0) {                                             // the nuancier
        for (int k = 0; k < CP_COLS * CP_ROWS; ++k) {
            const int col = k % CP_COLS, row = k / CP_COLS;
            if (!inrect(mo, gx + col * (cch + ccg), chartY + row * (cch + ccg), cch, cch)) continue;
            const u32 nc = (cp_chart(col, row) & 0x00FFFFFFu) | alpha;
            if (nc != *color) { *color = nc; rgb2hsv(nc, p->h, p->s, p->v); p->col = nc; changed = true; save_ui_config(); }
            break;
        }
    }
    if (mo && mo->clicked && g_slider < 0) {                                             // favourites : "+" adds, a chip applies, its corner badge removes
        if (inrect(mo, rx, favY, fcw, fch)) { if (ui_config().fav_color_add(*color)) save_ui_config(); }
        else for (int i = 0; i < ui_config().favColorN; ++i) {
            const int gp = i + 1; const float sx = rx + (gp % CP_FAVC) * (fcw + fgap), sy = favY + (gp / CP_FAVC) * (fch + fgap);
            if (!inrect(mo, sx, sy, fcw, fch)) continue;
            if (inrect(mo, sx + fcw - snap(11.0f), sy, snap(11.0f), snap(11.0f))) { ui_config().fav_color_remove(i); save_ui_config(); }
            else { const u32 nc = (ui_config().favColors[i] & 0x00FFFFFFu) | alpha; if (nc != *color) { *color = nc; rgb2hsv(nc, p->h, p->s, p->v); p->col = nc; changed = true; save_ui_config(); } }
            break;
        }
    }
    // ONE hover spring for the whole picker, not one per chip : 39 nuancier chips + 16 favourite slots would
    // burn 55 of ease()'s 1024 springs PER PICKER, and entries are never recycled. Whichever chip is under the
    // pointer reads this value ; moving between two chips hands it over instantly, which is what the eye wants.
    const float hv = ease(uidSV, 2, inrect(mo, x, y, W, H) ? 1.0f : 0.0f, 22.0f);

    // ---- the card --------------------------------------------------------------------------------------
    drop_shadow(dev, x, y, W, H, snap(5.0f), 58, snap(12.0f));
    rpanel(dev, x, y, W, H, snap(12.0f), 0xF02A343Eu, 0xF01F2831u,
           (C_STEEL_DEEP & 0x00FFFFFFu) | 0x88000000u, snap(1.2f));                  // STEEL border : this is structure, not brand
                                                                                     // (fill on the control step : the picker is a control sitting on a card)
    flat(dev, x + snap(12.0f), y + snap(1.0f), W - snap(24.0f), 1.0f, 0x1AFFFFFFu);  // 1px lit top edge

    // ---- SV square : white -> pure hue across the top, fading to black at the bottom --------------------
    // TWO exact ramps, not one four-corner quad. A quad is two TRIANGLES and Gouraud interpolates per
    // triangle, so a quad whose corners are not planar in colour space renders as two surfaces that disagree
    // along the shared diagonal -- a CREASE straight across the square. These corners (white / hue / black /
    // black) are non-planar for every hue but white, since white + black != hue + black. It was there from the
    // first version, on the largest gradient of the page, and no render state can fix it: it is what the
    // topology computes. This is also the ONLY four-corner gradient in the whole config UI -- every other one
    // is a pure horizontal or vertical ramp, which two triangles agree on exactly.
    //
    // The decomposition is exact, because HSV is exactly this product : rgb(s,v) = v * lerp(white, hue, s).
    //   pass 1 : white -> hue horizontally  (top pair == bottom pair -> a function of x alone -> exact)
    //   pass 2 : black, alpha 0 -> 255 down (left pair == right pair -> a function of y alone -> exact)
    //   and "over" composites them as dst*(1-a) = dst*v, which IS the value axis.
    q4(dev, cx0, cy0, sq, sq, 0xFFFFFFFFu, hue,        0xFFFFFFFFu, hue);
    q4(dev, cx0, cy0, sq, sq, 0x00000000u, 0x00000000u, 0xFF000000u, 0xFF000000u);
    outline(dev, cx0 - 1.0f, cy0 - 1.0f, sq + 2.0f, sq + 2.0f, (C_STEEL_DEEP & 0x00FFFFFFu) | 0xAA000000u);
    const float cxp = cx0 + p->s * sq, cyp = cy0 + (1.0f - p->v) * sq;                   // cursor : readable on ANY shade
    const float ct = ease(uidSV, 3, (hotSV || g_slider == uidSV) ? 1.0f : 0.0f);
    disc(dev, cxp, cyp + snap(1.0f), snap(6.2f) + snap(0.8f) * ct, 0x66000000u);
    disc(dev, cxp, cyp, snap(5.6f) + snap(0.8f) * ct, 0xE60A0D10u);
    disc(dev, cxp, cyp, snap(4.2f) + snap(0.8f) * ct, 0xFFF6FAFAu);
    disc(dev, cxp, cyp, snap(2.4f) + snap(0.8f) * ct, *color | 0xFF000000u);

    // ---- hue strip : VERTICAL, beside the square (six segments top->bottom) + a handle that spans it ----
    static const u32 HUE6[7] = { 0xFFFF0000u, 0xFFFFFF00u, 0xFF00FF00u, 0xFF00FFFFu, 0xFF0000FFu, 0xFFFF00FFu, 0xFFFF0000u };
    const float segH = sq / 6.0f;
    for (int i = 0; i < 6; ++i) { const float sy = cy0 + i * segH; q4(dev, hueX, sy, hw, segH + 1.0f, HUE6[i], HUE6[i], HUE6[i + 1], HUE6[i + 1]); }
    outline(dev, hueX - 1.0f, cy0 - 1.0f, hw + 2.0f, sq + 2.0f, (C_STEEL_DEEP & 0x00FFFFFFu) | 0xAA000000u);
    const float hcy = snap(cy0 + (p->h / 360.0f) * sq);
    rrect_bordered(dev, hueX - snap(4.0f), hcy - snap(4.0f), hw + snap(8.0f), snap(8.0f), snap(4.0f),
                   0xFFF8FBFCu, 0xFFCED8E2u, 0xFF080B0Eu, snap(1.3f));

    // ---- live swatch : the colour itself, big, with its hex ON it (dark or light text, by luminance) ----
    const u32 cur = *color | 0xFF000000u;
    const float swR = snap(10.0f);
    // The big swatch is the same object as the little ones, only larger : it shows a colour, so nothing is
    // drawn ON it. Relief by shadow and rim only -- and the gradient it keeps (+6% to -20%) is a property of
    // the sample, not a light: it is what lets you judge a colour against a shaded version of itself.
    drop_shadow(dev, rx, cy0, rw, swH, snap(3.0f), 58, swR);
    ctl_edge(dev, rx, cy0, rw, swH, swR, 0.80f, cur, cur);          // a nuance of the colour it is showing
    rrect_fill(dev, rx, cy0, rw, swH, swR, shade(cur, 0.06f), shade(cur, -0.20f));
    if (fo) {
        char hb[10]; sprintf(hb, "#%06X", (unsigned)(*color & 0x00FFFFFFu));
        const int lum = (77 * (int)((cur >> 16) & 0xFF) + 150 * (int)((cur >> 8) & 0xFF) + 29 * (int)(cur & 0xFF)) >> 8;
        const bool dark = lum > 140;   // a bright swatch takes dark text, and the outline flips with it
        fo->begin(dev);
        fo->draw_c(dev, rx + rw * 0.5f, cy0 + swH * 0.5f, hb, ts_value(),
                   fa(dark ? 0xFF0B0F13u : 0xFFF2F6FAu), fa(dark ? 0x40FFFFFFu : 0xC0000000u), 1.0f);
    }

    // ---- favourites : a micro label, then the "+" slot and the saved colours as the same chip -----------
    if (fo) { fo->begin(dev); fo->draw_lc(dev, rx, favLabY + snap(7.0f), tr("FAVOURITES", "FAVORIS"), ts_micro(), fa(C_MUTE), fa(C_STROKE), 1.0f); }
    { const bool hov = inrect(mo, rx, favY, fcw, fch);                                    // "+" : slot 0, a chip-shaped BUTTON
      const float t = hov ? hv : 0.0f;
      const u32 pT = lerpc(C_CTL_IDLE_T, C_CTL_HOV_T, t), pB = lerpc(C_CTL_IDLE_B, C_CTL_HOV_B, t);
      ctl_edge(dev, rx, favY, fcw, fch, snap(7.0f), 0.60f + 0.35f * t, lerpc(pT, pB, 0.5f));
      rrect_fill(dev, rx, favY, fcw, fch, snap(7.0f), pT, pB);
      ctl_crown(dev, rx, favY, fcw, fch, snap(7.0f), C_STEEL_HI, C_STEEL, 0.46f * t);
      const float pcx = rx + fcw * 0.5f, pcy = favY + fch * 0.5f, pr = snap(5.0f), pt = snap(1.6f);
      const u32 pc = lerpc(0xFFAEB9C4u, 0xFFEAF2F8u, t);
      flat(dev, pcx - pr, pcy - pt * 0.5f, pr * 2.0f, pt, pc); flat(dev, pcx - pt * 0.5f, pcy - pr, pt, pr * 2.0f, pc); }
    for (int i = 0; i < ui_config().favColorN; ++i) {
        const int gp = i + 1; const float sx = rx + (gp % CP_FAVC) * (fcw + fgap), sy = favY + (gp / CP_FAVC) * (fch + fgap);
        const u32 fc = ui_config().favColors[i] | 0xFF000000u;
        const bool hov = inrect(mo, sx, sy, fcw, fch);
        cp_swatch(dev, sx, sy, fcw, fch, fc, ((*color) & 0x00FFFFFFu) == (fc & 0x00FFFFFFu), hov ? hv : 0.0f);
        if (fo && hov) {                                                                 // hover : a remove badge in the corner
            const float br = snap(6.0f), bcx = sx + fcw - br, bcy = sy + br;
            disc(dev, bcx, bcy, br, 0xEE12171Cu); disc(dev, bcx, bcy, br - snap(1.1f), 0xFF2A343Cu);
            fo->begin(dev); fo->draw_c(dev, bcx, bcy, "x", ts_micro(), 0xFFE9EFF4u, C_STROKE, 1.0f);
        }
    }

    // ---- the nuancier ----------------------------------------------------------------------------------
    flat(dev, cx0, cy0 + sq + snap(CP_ROWGAP) * 0.5f, cw0, 1.0f, (C_STEEL_DEEP & 0x00FFFFFFu) | 0x66000000u);
    for (int col = 0; col < CP_COLS; ++col) for (int row = 0; row < CP_ROWS; ++row) {
        const float sx = gx + col * (cch + ccg), sy = chartY + row * (cch + ccg);
        const u32 cc2 = cp_chart(col, row);
        cp_swatch(dev, sx, sy, cch, cch, cc2, ((*color) & 0x00FFFFFFu) == (cc2 & 0x00FFFFFFu),
                inrect(mo, sx, sy, cch, cch) ? hv : 0.0f);
    }
    return changed;
}

// ---- the box-theme grid (see the header) ------------------------------------------------------------------
int theme_grid(u32 dev, Font* fo, const MouseState* mo, bool click, int uid,
               float coX, float ry, float ctrlW, int fam, int var, float& slotH) {
    const bool  isFFXI = (fam == 0);
    const int   nVar   = isFFXI ? window_tex_theme_count() : box_hue_count();
    if (isFFXI) {
        // THE FFXI SKIN IS PICKED WITH A SELECTOR, not a grid -- the same `< name >` row as the family above
        // it, and the same shape as every other setting on the page. A grid of named chips can only be as wide
        // as its names, so right-aligned in its row it started at a different x than the hue swatches and the
        // block jumped sideways whenever you changed family. Matching the two widths was treating a symptom:
        // the two modes were never the same KIND of control. They are now, and this row sits exactly where the
        // procedural family puts "Custom colour", so the section keeps its shape across the switch.
        slotH = snap(52.0f);
        const int d = row_selector(dev, fo, mo, click, uid, coX, ry + (slotH - snap(40.0f)) * 0.5f, ctrlW,
                                   tr("Theme", "Th\xC3\xA8me"), window_theme_name(var));
        return d ? wrap(var + d, nVar < 1 ? 1 : nVar) : -1;
    }
    const float cg = snap(7.0f);
    const int   COLS = 15;
    const float GRIDW = 15.0f * snap(22.0f) + 14.0f * cg;
    const float cw = snap(22.0f), ch = snap(22.0f);
    const int   nrows = (nVar + COLS - 1) / COLS;
    const float gridH = (float)nrows * ch + (float)(nrows - 1) * cg;
    slotH = gridH + snap(20.0f);
    fo->begin(dev);
    fo->draw_lc(dev, coX + snap(4.0f), ry + slotH * 0.5f, tr("Colour", "Couleur"), ts_label(), fa(C_TEXT), fa(C_STROKE), 1.0f);
    const float gx = coX + ctrlW - GRIDW, gy = ry + (slotH - gridH) * 0.5f;   // anchored on the SHARED width
    int picked = -1;
    for (int k = 0; k < nVar; ++k) {
        const float xk = gx + (k % COLS) * (cw + cg), yk = gy + (k / COLS) * (ch + cg);
        const bool  sel = (var == k), hov = inrect(mo, xk, yk, cw, ch);
        const float t = ease(ctrl_uid_i(uid, k), hov ? 1.0f : 0.0f);
        cp_swatch(dev, xk, yk, cw, ch, box_hue_color(k), sel, t);   // the same swatch the picker draws
        if (hov && click) picked = k;
    }
    return picked;
}

void nav_row(u32 dev, Font* fo, float x, float y, float w, float h, const char* label, bool active, float t, float pulse) {
    const float r = snap(9.0f);
    if (active) {
        drop_shadow(dev, x, y, w, h, snap(3.0f), 52, r);
        ctl_edge(dev, x, y, w, h, r, 0.55f, lerpc(C_CARD_T, C_ROWON_T, 0.72f));
        rrect_fill(dev, x, y, w, h, r, lerpc(C_CARD_T, C_ROWON_T, 0.72f), lerpc(C_CARD_B, C_ROWON_B, 0.72f));
        ctl_crown(dev, x, y, w, h, r, C_GOLDHI, C_GOLD, 0.80f + 0.20f * pulse);
        // (No vertical rail down the left edge any more. The row now has a lifted surface, a shadow, an edging
        //  and a gold lamp -- the rail was a fifth way of saying the one thing they already say together, and
        //  the same argument that took the accent rail off cat_panel applies here.)
    } else if (t > 0.01f) {
        ctl_edge(dev, x, y, w, h, r, 0.80f * t, lerpc(C_CTL_IDLE_T, C_CTL_HOV_T, t));
        const u32 fT = ((u32)(0xF0 * t) << 24) | (lerpc(C_CTL_IDLE_T, C_CTL_HOV_T, t) & 0x00FFFFFFu);
        const u32 fB = ((u32)(0xF0 * t) << 24) | (lerpc(C_CTL_IDLE_B, C_CTL_HOV_B, t) & 0x00FFFFFFu);
        rrect_fill(dev, x, y, w, h, r, fT, fB);                    // fades IN with the hover, so the rail stays a list
        ctl_crown(dev, x, y, w, h, r, C_STEEL_HI, C_STEEL, 0.50f * t);
    }
    if (fo) { fo->begin(dev);
              fo->draw_lc(dev, x + snap(18.0f), y + h * 0.5f, label, ts_label(),
                          lerpc(C_DIM, C_TEXT, active ? 1.0f : t), fa(C_STROKE), 1.0f); }
}

u32 ctl_edge_tint(u32 fill, u32 from) {
    const u32  base   = from ? from : C_ACCENT;
    const bool bright = fill_is_bright(fill);
    const u32  acc    = bright ? shade(base, -0.58f) : shade(base, 0.46f);
    return lerpc(acc, bright ? C_STEEL_DEEP : C_STEEL_HI, 0.45f);
}
void ctl_edge(u32 dev, float x, float y, float w, float h, float r, float strength, u32 fill, u32 from) {
    if (strength <= 0.01f) return;
    if (strength > 1.0f) strength = 1.0f;
    const float bw = snap(2.0f);
    const u32 c = (ctl_edge_tint(fill, from) & 0x00FFFFFFu) | ((u32)(235.0f * strength) << 24);
    rrect_fill(dev, x - bw, y - bw, w + 2.0f * bw, h + 2.0f * bw, r + bw, c, c);
}
void ctl_edge_top(u32 dev, float x, float y, float w, float h, float r, float strength, u32 fill, u32 from) {
    if (strength <= 0.01f) return;
    if (strength > 1.0f) strength = 1.0f;
    const float bw = snap(2.0f);
    const u32 c = (ctl_edge_tint(fill, from) & 0x00FFFFFFu) | ((u32)(235.0f * strength) << 24);
    rrect_top(dev, x - bw, y - bw, w + 2.0f * bw, h + bw, r + bw, c, c);   // +bw on the top only : the feet stay open
}
void ctl_crown(u32 dev, float x, float y, float w, float h, float r, u32 hiRGB, u32 loRGB, float k) {
    if (k <= 0.01f) return;
    if (k > 1.0f) k = 1.0f;
    const u32 hi = hiRGB & 0x00FFFFFFu, lo = loRGB & 0x00FFFFFFu;
    // Two ramps, stacked : their sum keeps softening all the way down, where one linear ramp ends in a kink
    // the eye reads as an edge. On the surface's OWN rect and radius, so they have no edge of their own.
    rrect_top(dev, x, y, w, h * 0.60f, r, hi | ((u32)(26.0f * k) << 24), lo);
    rrect_top(dev, x, y, w, h * 0.26f, r, hi | ((u32)(33.0f * k) << 24), lo);
    rrect_top(dev, x, y, w, h * 0.12f, r, hi | ((u32)(30.0f * k) << 24), lo);   // a third, tight ramp : it does
                                                                                // the job the upward bloom did
    const float ins = r + snap(2.0f);            // a straight bar must clear the round corners
    const float bx = x + ins, bw = w - 2.0f * ins;
    if (bw < snap(10.0f)) return;                // too narrow to carry a filament : the ramps alone say it
    // NO HAZE ABOVE THE FILAMENT. It used to be two hglow_soft passes centred on the bar, reaching 9px UP --
    // straight over the top border and out past it, additively. That is what made the rim look washed out
    // exactly where it should be sharpest: a border is the one line on a control that has to stay crisp, and
    // light drawn across it erases it. The reflection is a separate thing and it belongs INSIDE -- which is
    // what the ramps above already are, all of them falling downward from the top edge.
    cs(dev);
    hbar_soft(dev, bx, y + snap(2.0f), bw, snap(2.0f), hi, (u32)(235.0f * k * g_fade), 0.34f);
}

// A pill toggle chip. Modern: OFF = a neutral graphite pill ; ON = a SOLID teal fill with dark text.
// Hover smoothly lifts the surface + warms the border with a thin accent ring. No dot, no glass sweep.
bool toggle_chip(u32 dev, Font* fo, const MouseState* mo, bool click, int uid,
                        float x, float y, float w, float h, const char* label, bool on) {
    const bool hov = inrect(mo, x, y, w, h);
    const float st = ease(uid, 0, on ? 1.0f : 0.0f, 14.0f);          // on/off crossfade (sub 0)
    const float ht = ease(uid, 1, hov ? 1.0f : 0.0f);               // hover lift    (sub 1)
    const float r  = h * 0.5f;                                       // full pill
    // fill : neutral graphite (off) -> solid accent (on) ; hover lightens both a touch
    u32 ft = lerpc(C_CTL_IDLE_T, C_CHIP_ON_T, st), fb = lerpc(C_CTL_IDLE_B, C_CHIP_ON_B, st);
    ft = lerpc(ft, lerpc(C_CTL_HOV_T, C_ACCENTHI, st), ht * 0.8f);       // hover clearly lifts the surface (visible on BOTH the dark OFF and the bright ON pill)
    fb = lerpc(fb, lerpc(C_CTL_HOV_B, C_ACCENT, st), ht * 0.8f);
    // THE TAB LANGUAGE : a steel edging one size larger, the fill over it, a sliver of glass, and the lamp.
    // The old chip had a 1.4px stroked border instead -- a hairline drawn ON the pill, where the tabs have an
    // edge that IS the pill's own rim. That difference is most of why the two read as unrelated controls.
    ctl_edge(dev, x, y, w, h, r, 0.58f + 0.28f * ht + 0.14f * st, lerpc(ft, fb, 0.5f));
    rrect_fill(dev, x, y, w, h, r, ft, fb);
    rrect_top(dev, x + snap(2.0f), y + snap(1.0f), w - snap(4.0f), h * 0.42f, r * 0.7f,
              ((u32)(0x14 + 0x1C * (0.35f + 0.65f * ht)) << 24) | 0x00FFFFFF, 0x02FFFFFF);
    // The lamp reads the fill it sits on : a bright ON pill takes a near-white filament (an accent one would
    // vanish into its own colour), a dark pill takes the accent when ON and steel under the pointer.
    { const bool bright = fill_is_bright(lerpc(ft, fb, 0.5f));
      const u32 chi = bright ? 0xFFFFFFFFu : (st > 0.5f ? C_GOLDHI : C_STEEL_HI);
      const u32 clo = bright ? 0xFFE8F1F8u : (st > 0.5f ? C_GOLD   : C_STEEL);
      ctl_crown(dev, x, y, w, h, r, chi, clo, st > 0.5f ? (0.72f + 0.28f * ht) : (0.55f * ht)); }
    cs(dev);
    // text : legible on ANY pill -- the shared rule, since the ON fill follows an accent the user chooses.
    u32 onStk; const u32 onTxt = text_on_fill(lerpc(C_CHIP_ON_T, C_CHIP_ON_B, 0.5f), &onStk);
    const u32 txt = lerpc(C_TEXT,   onTxt, st);
    const u32 stk = lerpc(C_STROKE, onStk, st);
    fo->begin(dev); fo->draw_c(dev, x + w * 0.5f, y + h * 0.5f, label, ts_chip(), fa(txt), fa(stk), 1.0f);
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
    const u32 idleT = tone ? 0xFF3B2B2E : C_CTL_IDLE_T, idleB = tone ? 0xFF2D2023 : C_CTL_IDLE_B;   // the danger tone rides the same ramp, in red
    const u32 hovT  = tone ? 0xFF4C363C : C_CTL_HOV_T,  hovB  = tone ? 0xFF3B292E : C_CTL_HOV_B;
    const float pin = press ? snap(1.0f) : 0.0f;                     // press : a small inward nudge
    const float bx = x + pin, by = y + pin, bw = w - 2 * pin, bh = h - 2 * pin, r = snap(9.0f);
    drop_shadow(dev, bx, by, bw, bh, snap(3.0f), press ? 30 : 54);
    // Same three pieces as a tab and a chip : edging, fill, glass, then the lamp on hover. The 1px top
    // hairline this replaces was a flat line drawn on the button ; the lamp is light falling on its edge.
    ctl_edge(dev, bx, by, bw, bh, r, 0.60f + 0.35f * t, lerpc(lerpc(idleT, hovT, t), lerpc(idleB, hovB, t), 0.5f));
    rrect_fill(dev, bx, by, bw, bh, r, lerpc(idleT, hovT, t), lerpc(idleB, hovB, t));
    rrect_top(dev, bx + snap(2.0f), by + snap(1.0f), bw - snap(4.0f), bh * 0.42f, snap(6.0f),
              ((u32)(0x14 + 0x1E * t) << 24) | 0x00FFFFFF, 0x02FFFFFF);
    ctl_crown(dev, bx, by, bw, bh, r, tone ? 0xFFFFC9CDu : C_STEEL_HI, tone ? accBr : C_STEEL, press ? 0.30f * t : 0.62f * t);
    cs(dev);
    // the label follows the fill (text_on_fill), then warms toward the accent on hover -- but only while the
    // fill is DARK enough for a bright label. On a bright button the accent tint would erase the contrast the
    // rule just bought.
    u32 pbStk; const u32 pbTxt = text_on_fill(lerpc(lerpc(idleT, hovT, t), lerpc(idleB, hovB, t), 0.5f), &pbStk);
    const u32 pbLbl = fill_is_bright(lerpc(idleT, hovT, t)) ? pbTxt : lerpc(pbTxt, accHi, t * 0.45f);
    fo->begin(dev); fo->draw_c(dev, x + w * 0.5f, y + h * 0.5f, label, snap(13.0f), fa(pbLbl), fa(pbStk), 1.0f);
    return hov && click;
}

// a collapsible CATEGORY header : a full-width gold bar with a triangle (right = collapsed, down = open) +
// label. Returns true on click (the caller toggles the open flag). uid = animation slot.
// the SOLID background "card" behind an OPEN category (drawn BEFORE its header + rows). The tab body is
// transparent, so THIS is what gives each menu a full, solid surface -- not the striped row bands.
float cat_fold(int uid, bool open) {
    // Smoothstepped, so the fold has no jerk at either end -- a linear ease starts and stops abruptly at exactly
    // the two moments the eye is watching it. Speed 10 is deliberate: 15 read as a jump with a smear on it, 7.5
    // dragged.
    const float a = ease(uid, open ? 1.0f : 0.0f, 10.0f);
    if (a < 0.001f) return 0.0f;
    if (a > 0.999f) return 1.0f;
    return a * a * (3.0f - 2.0f * a);
}
void cat_fold_clip(u32 dev, float x, float top, float w, float visH) {
    clip_rect_begin(dev, x, top, w, visH + 1.0f);   // +1 : a zero-height scissor would drop the first row entirely
}
void cat_fold_end(u32 dev, float& ry, float top, float& full, float a) {
    clip_rect_end(dev);
    // The card's height is this `full`, so the last row used to land exactly ON the bottom border : the content
    // had a 16px margin above the next section but ZERO inside its own panel, which reads as the content
    // spilling out of the bottom of the card rather than sitting in it. The padding goes HERE, in the one place
    // that measures a section, rather than in the thirty-odd call sites that would each have to remember it.
    full = ry - top + snap(14.0f);   // measured from the FULL layout, plus the panel's bottom padding
    ry   = top + full * a;           // ... and the cursor goes back to what was actually revealed
}

void cat_panel(u32 dev, float x, float y, float w, float h) {
    if (h < snap(4.0f)) return;
    // The card FLOATS : a shadow is what separates the tier you are working in from the page behind it, and
    // depth that carries hierarchy is the one form of it worth having (blur for its own sake is the trend every
    // 2026 survey warns off).
    drop_shadow(dev, x, y, w, h, snap(5.0f), 64);
    // A tier reads as a tier when it is a different TONE, not when it is fenced off. The card used to be darker
    // than the page and held together by a 1px white border -- an outline doing a job that a shade does better.
    // It is a step LIGHTER than the content surface now, and the border is barely there.
    // A REAL border, in the theme's edge tint. It was 0x1AFFFFFF -- 10% white, described in this very comment
    // as "barely there", which turned out to mean "not there": the card that holds every open section had no
    // visible edge at all, so a page of settings floated with nothing saying where the panel ended.
    rpanel(dev, x, y, w, h, snap(9.0f), C_CARD_T, C_CARD_B,
           (ctl_edge_tint(C_CARD_T) & 0x00FFFFFFu) | 0xC0000000u, snap(2.0f));
    flat(dev, x + snap(9.0f), y + snap(1.0f), w - snap(18.0f), 1, 0x16FFFFFF);           // the light catches the top edge
    // (No accent rail down the left. It was meant to say WHERE you are without spending a label on it, and it
    //  said nothing the open title bar was not already saying by its shape -- one section is open, this is it.
    //  A mark that repeats what the structure already states is decoration, and it read as one.)
}
bool cat_header(u32 dev, Font* fo, const MouseState* mo, bool click, int uid, float x, float y, float w, const char* label, bool open, float a) {
    // A SECTION HEADER IS A TITLE BAR, not a caret with a word after it.
    // It used to be the quietest possible mark -- a small arrow, an uppercase label, a hairline running out to
    // the right -- on the argument that a heading should not shout like the controls it introduces. That was
    // right about the shouting and wrong about the object: with nothing but a hairline behind it, the header had
    // no surface, so nothing said it could be pressed, and a page of collapsed ones read as a list of labels
    // rather than as a stack of closed drawers.
    // Now it is a band with a real surface, and it changes shape according to what it is doing. CLOSED it is a
    // free-standing bar, rounded on all four corners, sitting on the page. OPEN, its bottom corners square off
    // so it welds to the panel underneath and becomes that panel's title -- the shape itself says "this bar owns
    // what is below it", which is the whole grammar of an accordion and costs no extra ink to say.
    // The fold's progress, as measured by the fold itself. Everything that separates an open header from a
    // closed one -- its tint, its lamp, the shape of its feet -- crosses over on exactly this number, so the
    // bar and the card it titles can never be in two different states.
    const float o = a < 0.0f ? 0.0f : (a > 1.0f ? 1.0f : a);
    (void)open;   // `open` is still the CLICK's target state ; `a` is what is on screen
    // ONE HEIGHT, ALWAYS. A title bar that grows when you collapse it is a bar that changed object. What
    // changes is the CARD: closed it is exactly this bar (cat_card_h), open it is this bar plus the padding
    // plus the content. So the title is centred in what you see, in both states, without the bar moving.
    const float h = CAT_BAR_H, r = snap(8.0f);
    const bool hov = inrect(mo, x, y, w, h);
    const float t = ease(uid, hov ? 1.0f : 0.0f);

    // The bar sits on the ELEVATION RAMP, and this is the surface that matters most on this page: with every
    // section collapsed, the closed bars are the ONLY thing drawn in the whole controls column. The container
    // behind them is fully transparent by design, and C_CONTENT is never painted anywhere at all -- so the
    // screen you look at most of the time was a few translucent strips over the dimmed game, and whatever the
    // cards, chips and shadows did, none of it was reachable from there.
    // CLOSED = the control step, opaque. OPEN = accent-tinted, but floored on the CARD step, so a title bar can
    // never come out darker than the card it is the title of.
    const u32 baseT = lerpc(C_CTL_IDLE_T, lerpc(C_CARD_T, C_ROWON_T, 0.72f), o);
    const u32 baseB = lerpc(C_CTL_IDLE_B, lerpc(C_CARD_B, C_ROWON_B, 0.72f), o);
    const u32 fT = lerpc(lerpc(baseT, C_CTL_HOV_T, t), lerpc(baseT, shade(baseT, 0.22f), t), o);
    const u32 fB = lerpc(lerpc(baseB, C_CTL_HOV_B, t), lerpc(baseB, shade(baseB, 0.22f), t), o);
    // ONE SHAPE, OPEN OR CLOSED. It used to change: rounded on four corners when closed, square-footed when
    // open. The idea was that the shape should say what the bar owns -- but the panel is drawn in BOTH states
    // now (collapsed, it is simply a panel with no content in it), so the bar is always a title welded to
    // something. A section that changes silhouette when you press it reads as two different objects, and what
    // actually happens is one object whose CONTENT unfolds downward.
    // The perimeter belongs to the CARD (cat_panel draws it, at the same rect), so the bar draws no edging of
    // its own -- an edging is the shape one size LARGER, and it stuck two pixels out past the card on three
    // sides. It sits INSIDE the card's border, inset by exactly that thickness, flush with nothing between.
    const float bwC = snap(2.0f);                          // == cat_panel's border thickness
    const float ir  = r > bwC ? r - bwC : 0.0f;
    // Inset on ALL FOUR sides, not three. The fill used to run from y+bwC to y+h -- straight over the card's
    // bottom border, which closed is exactly there (the card is bar-height), so the border came out truncated:
    // present on three sides and painted over on the fourth. The bar sits inside the ring, never on it.
    const float bh = h - 2.0f * bwC;
    // The FEET. Square feet are right for an open section -- the bar welds to the content under it -- and
    // wrong for a closed one, where the bar sits at the very bottom of the card and its square corners poke
    // out through the card's rounded ones. So the feet round off exactly when there is nothing to weld to.
    // THE FEET ARE ANIMATED, not switched. Rounded when the bar is closed, square when it is welded to an open
    // card -- and in between the bottom radius simply interpolates on the fold's own progress, so there is no
    // moment at which the shape jumps. A switch (at any threshold) pops ; a threshold near zero pops LATE,
    // which is worse. The two END states go through the crisp masked paths, so nothing is lost at rest:
    // motion is drawn by rrect_tb, and a fifth of a second of feathered corners is not what the eye is on.
    // ONE call, in every state. rrect_tb picks the baked masks itself when the radii land on whole pixels, so
    // there is no longer a moment where the bar changes RENDERER -- which is what flashed at the end of the
    // fold: the silhouette went from feathered to crisp on the last frame, and that reads as the border
    // brightening. The bottom radius is simply the top one, retracted by the fold.
    cs(dev);
    rrect_tb(dev, x + bwC, y + bwC, w - 2.0f * bwC, bh, ir, ir * (1.0f - o), fa(fT), fa(fB));
    ctl_crown(dev, x + bwC, y + bwC, w - 2.0f * bwC, bh, ir,
              lerpc(C_STEEL_HI, C_GOLDHI, o), lerpc(C_STEEL, C_GOLD, o),
              (0.30f + 0.45f * t) + o * (0.25f - 0.20f * t));
    // NO rim at the bar's foot. There is exactly ONE bottom border on a section and it belongs to the CARD --
    // closed, the card is header-height so that border sits just under the title ; open, the same border
    // travels down as the content unfolds. A rim here made a SECOND one, so a closed section had its border
    // under the title and an open section had two: one under the title and one at the foot of the panel.
    // The border does not change identity when the section opens ; it moves.
    { const float ih = snap(2.0f);                                                                // the bar is inside the card in both states
      flat(dev, x + r + ih, y + snap(1.0f) + ih, w - r * 2.0f - 2.0f * ih, 1,
           ((u32)(0x18 + (u32)(0x12 * t)) << 24) | 0x00FFFFFFu); }                                // the light catches its top edge
    if (!open && t > 0.01f) ctl_crown(dev, x, y, w, h, r, C_STEEL_HI, C_STEEL, 0.55f * t);

    // The label in the heading's uppercase ; the note beside it in the ordinary case, because it is a value.
    const bool up0 = fo->upper();
    fo->set_upper(true);
    // Centred in what is actually DRAWN : the fill starts at y+bwC and ends at y+h, so its middle is not y+h/2.
    // The indent is CONSTANT. It used to be 14 closed and 16 open -- a leftover from when the open bar was not
    // inset into the card and needed the extra two pixels to line up. Both states are inset by bwC now, so the
    // two numbers describe the same position, and interpolating between them just made the title slide.
    const float tx = x + bwC + snap(12.0f), gy = y + bwC + (h - 2.0f * bwC) * 0.5f;

    // The chevron sits at the RIGHT end, which is where a disclosure control belongs when the label is on the
    // left : the two ends of the bar are its two jobs, naming and opening. It points DOWN when open, at what it
    // opened, and right when closed, at what would happen next.
    // (Drawn as a filled AA triangle, not with chevron(): that primitive's `dir` is a multiplier on the apex's
    //  X offset, so it can point left or right and nothing else -- and this one has to point DOWN.)
    { const float cx2 = x + w - snap(16.0f), sTri = snap(4.5f);
      const u32 cc = fa(lerpc(C_MUTE, C_ACCENTHI, o > t ? o : t));
      if (open) { const float d[6] = { cx2 - sTri, gy - sTri * 0.55f,  cx2 + sTri, gy - sTri * 0.55f,  cx2, gy + sTri * 0.85f };
                  fill_poly_aa(dev, d, 3, cc); }
      else      { const float d[6] = { cx2 - sTri * 0.55f, gy - sTri,  cx2 - sTri * 0.55f, gy + sTri,  cx2 + sTri * 0.85f, gy };
                  fill_poly_aa(dev, d, 3, cc); } }

    fo->begin(dev);
    // The title is the THEME's colour, not a neutral grey -- the page names things in the accent everywhere
    // else (the module title, the MODULES eyebrow, the chosen tab), and a section heading is a name.
    // Closed it is the accent LIGHTENED and mixed back toward C_DIM : measured against the closed bar that
    // holds 4.8:1 or better on every hue, where the raw accent fell to 3.1:1 on red and violet. Open it goes
    // to the bright accent, which measures 6.9:1 or better on its own accent-tinted bar.
    const u32 titleDim = lerpc(C_DIM, shade(C_ACCENT, 0.45f), 0.70f);
    fo->draw_lc(dev, tx, gy, label, ts_section(), fa(lerpc(titleDim, C_GOLDHI, o > t ? o : t)), fa(C_STROKE), 1.2f);
    fo->set_upper(up0);                                                                      // put the font back as we found it
    return hov && click;
}


} // namespace aio
