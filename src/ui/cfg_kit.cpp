// cfg_kit.cpp -- the config redesign's component kit and its SHEET (//aio config kit). See cfg_kit.h.
//
// The MATERIAL is the existing config's (config_controls) : a steel edging one size larger than the control
// (ctl_edge), a gradient fill, a sliver of glass on the top, the lamp filament on the rim (ctl_crown), a drop shadow,
// in the THEME colours (C_ACCENT family, steel, the logo's gold). What the kit adds : one size unit `u` (the old
// controls are fixed pixels), real toggles, the states the old ones lack (unavailable + reason, modified, focus with
// the FFXI pointer), and the graduation on sliders. 2026-11-26 : a first flat version was refused -- "très loin de la
// qualité qu'on a déjà" -- the redesign keeps this material and fixes size, hierarchy and states.
#include "ui/cfg_kit.h"
#include "ui/config_controls.h"   // the material : ctl_edge / ctl_crown / rrect_fill / rpanel / drop_shadow / chevron + palette
#include "model/ui_config.h"      // the config's own face (the Interface text element)
#include "gfx/draw.h"
#include "gfx/font.h"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace aio {

float Kit::px(float v) const { return snap(v * u); }

static const float TS_TITLE = 28.0f, TS_SECTION = 15.5f, TS_LABEL = 16.0f, TS_VALUE = 15.0f, TS_NOTE = 13.5f, TS_NAV = 16.0f;   // 2026-11-26 : -15 % ("trop gros")
static const u32 C_FOCUS = C_METAL, C_MODIFIED = C_METAL;   // the brand gold (not the theme accent) : focus and "modified"

// a disabled part draws through the page fade : one dimming for every shape and text it has
// the page fade a Dim() dimmed -- window_face draws a disabled window at THAT opacity (dim colours, readable), not at 1.0 :
// at 1.0 it popped in at full opacity through the page's open fade (review 2026-11-26)
static float s_undimmed = -1.0f;
struct Dim { float keep, was; Dim(bool on) : keep(g_fade), was(s_undimmed) { if (on) { s_undimmed = keep; g_fade = keep * 0.42f; } } ~Dim() { g_fade = keep; s_undimmed = was; } };

static void text_l(Font* fo, u32 dev, float x, float cy, const char* s, float sz, u32 col) { fo->begin(dev); fo->draw_lc(dev, x, cy, s, sz, fa(col), fa(C_STROKE), 1.0f); }
static void tri(u32 dev, float ax, float ay, float bx, float by, float cx, float cy, u32 col) { const float p[6] = { ax, ay, bx, by, cx, cy }; cs(dev); fill_poly_aa(dev, p, 3, fa(col)); }

bool kit_begin(Kit& K, const Frame& f, float sh, float textScale) {
    if (!f.fonts || sh <= 0.0f) return false;
    const TextStyle& ts = ui_config().text[0][TE_UI];                  // the config's own face, as the old page
    const char* face = ts.face > 0 ? ui_font_face(ts.face) : "Verdana";
    K.fr = f.fonts->get(face, 600); K.fs = f.fonts->get(face, 700);
    if (!K.fr || !K.fs) return false;
    K.fr->ensure(f.dev); K.fs->ensure(f.dev);
    if (!K.fr->ready() || !K.fs->ready()) return false;
    K.dev = f.dev; K.mo = f.mouse; K.click = K.mo && K.mo->clicked;
    // the DENSITY : 0.82 of a 1080p pixel per unit (2026-11-26 : "tout est trop gros") -- every part shrinks together
    K.u = (sh / 1080.0f) * 0.82f * (textScale > 0.5f ? textScale : 1.0f);
    return true;
}

// ---- the FFXI pointer : the game's selection hand reduced to a gold arrow, dark heart, tip at (x, cy) ----
void kit_focus_cursor(Kit& K, float x, float cy) {
    const float L = K.px(17.0f), H = K.px(10.5f), i = K.px(3.2f);
    cs(K.dev); disc(K.dev, x - L * 0.55f, cy + K.px(1.5f), L * 0.55f, fa(0x40000000u));   // it sits on the page
    tri(K.dev, x, cy, x - L, cy - H, x - L, cy + H, C_METAL);
    tri(K.dev, x - i * 1.7f, cy, x - L + i, cy - H + i * 1.25f, x - L + i, cy + H - i * 1.25f, C_METAL_DEEP);
    tri(K.dev, x - i * 1.7f, cy, x - L + i, cy - H + i * 1.25f, x - L + i, cy, C_METAL_HI);   // the lit facet
}

// ======== THE METALWORK (the minimap dial's recipes, 2026-11-26 : "regarde la minimap, c'est là qu'on a bossé
// l'esthétique") : forged steel for the structure, cast brass for what moves, ivory windows with sepia figures for the
// values -- the dial's date window. Pieces are CAST together (dark edge, the body lit from the top, a polished filet,
// a chamfer into the hollow), never laid on top ; no ink outline ; text upright. Same recipes as minimap.cpp's
// dial_metal_set / draw_square_case / drum_text ; once the direction is chosen they move to one shared file. ========
struct Metal { u32 edge, body, top, bot, dark, lite, glint, shade; };
static u32 m_scale(u32 c, float k) {
    float r = ((c >> 16) & 0xFF) * k, g = ((c >> 8) & 0xFF) * k, b = (c & 0xFF) * k;
    r = r > 255.0f ? 255.0f : r; g = g > 255.0f ? 255.0f : g; b = b > 255.0f ? 255.0f : b;
    return 0xFF000000u | ((u32)r << 16) | ((u32)g << 8) | (u32)b;
}
static u32 m_toward(u32 c, u32 t, float k) {
    const float q = 1.0f - k;
    return 0xFF000000u | ((u32)(((c >> 16) & 0xFF) * q + ((t >> 16) & 0xFF) * k) << 16) | ((u32)(((c >> 8) & 0xFF) * q + ((t >> 8) & 0xFF) * k) << 8)
                       | (u32)((c & 0xFF) * q + (t & 0xFF) * k);
}
static Metal metal_of(u32 base) {   // dial_metal_set : a polished metal's palette from one colour
    u32 mx = (base >> 16) & 0xFF; if (((base >> 8) & 0xFF) > mx) mx = (base >> 8) & 0xFF; if ((base & 0xFF) > mx) mx = base & 0xFF;
    const u32 B = mx ? m_scale(base, 190.0f / (float)mx) : 0xFF9A9A9Au;
    Metal m; m.body = B; m.top = m_toward(B, 0xFFFFFFFFu, 0.25f); m.bot = m_scale(B, 0.68f); m.edge = m_scale(B, 0.46f);
    m.dark = m_scale(B, 0.46f); m.lite = m_toward(B, 0xFFFFFFFFu, 0.34f); m.glint = m_toward(B, 0xFFFFFFFFu, 0.58f); m.shade = m_scale(B, 0.16f);
    return m;
}
static const Metal& STEEL() { static Metal m = metal_of(0xFF8C97A6u); static bool once = false; if (!once) { once = true; m.top = m_scale(m.top, 0.80f); m.bot = m_scale(m.bot, 0.78f); m.body = m_scale(m.body, 0.80f); } return m; }   // brushed, a shade darker : the dial's ring, not chrome   // the structure (the dial's ring)
static const Metal& BRASS() { static const Metal m = { 0xFF3A2A10u, 0xFFB79A61u, 0xFFD4B574u, 0xFF8E7440u, 0xFF5E4A26u, 0xFFE2C482u, 0xFFFFF0C8u, 0xFF241808u }; return m; }   // what moves
static u32 MA(u32 a, u32 col) { return (((u32)(a * g_fade)) << 24) | (col & 0x00FFFFFFu); }

// a cast PLINTH : shadow, the dark edge, the metal lit from the top, a polished filet
static void plinth(Kit& K, float x, float y, float w, float h, float r, const Metal& m, float lift = 0.0f) {
    const u32 dev = K.dev;
    cs(dev);
    rrect_glow(dev, x, y + K.px(2.0f), w, h, r, MA(0x80, 0x000000u), K.px(5.0f));
    // the LIGHT (2026-11-26, from the profile slot's lit lip : "la lumiere est parfaite, ce genre de rendu partout") :
    // never a uniform rim. The silhouette is dark ; the BEVEL under it is a gradient -- the light caught along the top,
    // falling to shadow at the foot -- so it runs round every rounded corner by itself, the way real metal does.
    const float o = K.px(1.4f), b = K.px(1.1f);
    rrect(dev, x - o, y - o, w + 2.0f * o, h + 2.0f * o, r + o, fa(m_scale(m.edge, 0.85f)), fa(m_scale(m.edge, 0.55f)));
    rrect(dev, x - o * 0.35f, y - o * 0.35f, w + o * 0.7f, h + o * 0.7f, r + o * 0.35f,
          fa(m_toward(m.glint, 0xFFFFFFFFu, 0.10f * lift)), fa(m_scale(m.dark, 0.85f)));
    rrect(dev, x + b * 0.65f, y + b, w - b * 1.3f, h - b * 1.9f, r > b ? r - b * 0.8f : 0.0f,
          fa(m_toward(m.top, 0xFFFFFFFFu, 0.12f * lift)), fa(m_toward(m.bot, 0xFFFFFFFFu, 0.10f * lift)));
}
// a HOLLOW cut into a plinth : a dark upper edge and a lit lower lip, the floor
static void hollow(Kit& K, float x, float y, float w, float h, float r, const Metal& m, u32 floorCol) {
    const u32 dev = K.dev;
    cs(dev);
    rrect(dev, x - K.px(1.6f), y - K.px(1.6f), w + K.px(3.2f), h + K.px(3.2f), r + K.px(1.6f), fa(m.edge), fa(m.lite));
    rrect(dev, x, y, w, h, r, fa(floorCol), fa(floorCol));
    grad_quad(dev, x, y, w, h * 0.35f, fa(0x90000000u), fa(0x90000000u), 0, 0);   // the upper edge's shadow on the floor
}
// the WINDOW : the dial's date window -- an ivory face in a hollow, sepia figures, a glass over it
static void window_face(Kit& K, float x, float y, float w, float h, const char* s, float sz, bool dim) {
    const u32 dev = K.dev;
    const Metal& m = STEEL();
    const float r = K.px(3.0f);
    const float keep = g_fade; if (dim && s_undimmed >= 0.0f) g_fade = s_undimmed;   // a dimmed window is drawn in dim colours at full opacity : a faded one smears its letters
    hollow(K, x, y, w, h, r, m, 0xFF0A0806u);
    const float ix = x + K.px(1.2f), iy = y + K.px(1.2f), iw = w - K.px(2.4f), ih = h - K.px(2.4f);
    const u32 face = dim ? 0xFF4A4740u : 0xFFC2AF84u, crown = dim ? 0xFF524F47u : 0xFFD2C095u;   // 2026-11-26 : a duller ivory (bright stripes down the column)
    cs(dev);
    rrect(dev, ix, iy, iw, ih, r * 0.7f, fa(face), fa(face));
    rrect(dev, ix + iw * 0.04f, iy + ih * 0.26f, iw * 0.92f, ih * 0.48f, 0.0f, fa(crown), fa(crown));   // its bright crown
    grad_quad(dev, ix, iy, iw, ih * 0.30f, MA(0x90, 0x3A2410u), MA(0x90, 0x3A2410u), 0, 0);             // turning away : top
    grad_quad(dev, ix, iy + ih * 0.70f, iw, ih * 0.30f, 0, 0, MA(0x90, 0x3A2410u), MA(0x90, 0x3A2410u)); // ... bottom
    K.fs->begin(dev);
    K.fs->draw_c(dev, snap(ix + iw * 0.5f), snap(iy + ih * 0.5f), s, sz, fa(dim ? 0xFF8E8778u : 0xFF2A190Au), 0, 0.0f);   // sepia ink, no outline
    cs(dev);
    grad_quad(dev, ix, iy, iw, ih * 0.45f, MA(0x30, 0xFFFFFFu), MA(0x30, 0xFFFFFFu), MA(0x06, 0xFFFFFFu), MA(0x06, 0xFFFFFFu));   // the glass
    grad_quad(dev, ix, iy, iw, 1.0f, MA(0x60, 0xFFFFFFu), MA(0x60, 0xFFFFFFu), MA(0x60, 0xFFFFFFu), MA(0x60, 0xFFFFFFu));
    g_fade = keep;
}
// an engraved cut in metal : the cut, the light caught in it
static void engrave(Kit& K, float ax, float ay, float bx, float by, const Metal& m) {
    cs(K.dev);
    seg_soft(K.dev, ax + 0.6f, ay + 0.7f, bx + 0.6f, by + 0.7f, K.px(1.0f), MA(0x80, m.glint));
    seg_soft(K.dev, ax, ay, bx, by, K.px(1.0f), fa(m.edge));
}

// ---- toggle : a steel SLIDE -- a slot in a plinth, a cast brass block with grip cuts ; ON uncovers the lit enamel ----
bool kit_toggle(Kit& K, int id, float x, float y, bool on, int st) {
    const bool dis = (st & KS_DISABLED) != 0;
    Dim dim(dis);
    const float w = K.px(56.0f), h = K.px(28.0f);
    const bool hov = !dis && ((st & KS_HOVER) || inrect(K.mo, x, y, w, h));
    const float s = ease(id, 0, on ? 1.0f : 0.0f, 16.0f), ht = ease(id, 1, hov ? 1.0f : 0.0f);
    const Metal& st_ = STEEL(); const Metal& br = BRASS();
    plinth(K, x, y, w, h, K.px(5.0f), st_, ht);
    const float gx = x + K.px(5.0f), gy = y + K.px(8.0f), gw = w - K.px(10.0f), gh = h - K.px(16.0f);
    hollow(K, gx, gy, gw, gh, K.px(2.0f), st_, 0xFF0C0A08u);
    const float bw = K.px(22.0f), bh = h - K.px(6.0f), bx = x + K.px(3.0f) + (w - K.px(6.0f) - bw) * s, by = y + K.px(3.0f);
    if (s > 0.01f) {   // the enamel the block uncovers : warm, lit from within
        const float ew = (bx - gx); if (ew > 1.0f) { cs(K.dev); rrect(K.dev, gx + K.px(1.0f), gy + K.px(1.0f), ew - K.px(1.0f), gh - K.px(2.0f), K.px(1.5f), fa(m_toward(BRASS().lite, 0xFF0C0A08u, 1.0f - s)), fa(m_toward(BRASS().body, 0xFF0C0A08u, 1.0f - s))); }
    }
    plinth(K, bx, by, bw, bh, K.px(4.0f), br, ht);   // the cast block
    for (int i = -1; i <= 1; ++i) { const float cx = bx + bw * 0.5f + (float)i * K.px(4.5f); engrave(K, cx, by + K.px(5.0f), cx, by + bh - K.px(5.0f), br); }
    return !dis && K.click && inrect(K.mo, x, y, w, h);
}

// ---- slider : the dial's INDEX RAIL -- a steel rail engraved every 10 %, a brass inlay up to the value, a cast brass
//      pointer with the red index line ; the value in an ivory window ----
bool kit_slider(Kit& K, int id, float x, float y, float w, float* v01, const char* val, int st) {
    const bool dis = (st & KS_DISABLED) != 0;
    Dim dim(dis);
    const float winW = K.px(74.0f), gap = K.px(16.0f), tw = w - winW - gap, h = K.px(36.0f), cy = y + K.px(14.0f);
    bool changed = false;
    // the config's shared drag latch (ctrl_drag_*) : a drag that leaves the content viewport keeps the pointer
    if (!dis) {
        ctrl_drag_begin(id, K.mo, inrect(K.mo, x - K.px(8.0f), y, tw + K.px(16.0f), h));
        if (ctrl_drag_active(id)) {
            if (K.mo && K.mo->down) { float v = (K.mo->x - x) / tw; v = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); if (v != *v01) { *v01 = v; changed = true; } }
            else ctrl_drag_end(id, K.mo);
        }
    }
    const bool drag = (st & KS_DRAG) || ctrl_drag_active(id);
    const bool hot = !dis && (drag || (st & KS_HOVER) || inrect(K.mo, x - K.px(8.0f), y, tw + K.px(16.0f), h));
    const float kt = ease(id, 0, hot ? 1.0f : 0.0f), v = *v01 < 0.0f ? 0.0f : (*v01 > 1.0f ? 1.0f : *v01);
    const Metal& st_ = STEEL(); const Metal& br = BRASS();
    const float rh = K.px(16.0f), ry = cy - rh * 0.5f;
    plinth(K, x, ry, tw, rh, K.px(3.0f), st_, kt * 0.6f);
    const float gy = cy - K.px(2.5f), gh = K.px(5.0f);
    hollow(K, x + K.px(5.0f), gy, tw - K.px(10.0f), gh, K.px(1.5f), st_, 0xFF0C0A08u);
    const float fx = x + K.px(5.0f) + (tw - K.px(10.0f)) * v;
    if (fx > x + K.px(6.0f)) { cs(K.dev); rrect(K.dev, x + K.px(6.0f), gy + K.px(1.0f), fx - x - K.px(6.0f), gh - K.px(2.0f), K.px(1.0f), fa(br.lite), fa(br.body)); }   // the brass inlay
    for (int i = 0; i <= 10; ++i) {   // the index marks, engraved in the rail's edges
        const float mx = snap(x + K.px(5.0f) + (tw - K.px(10.0f)) * (float)i / 10.0f), L = K.px(i % 5 == 0 ? 4.5f : 2.5f);
        engrave(K, mx, ry + K.px(1.5f), mx, ry + K.px(1.5f) + L, st_);
        engrave(K, mx, ry + rh - K.px(1.5f) - L, mx, ry + rh - K.px(1.5f), st_);
    }
    // the pointer : a cast brass cursor straddling the rail, the red index line down its middle
    const float pw = K.px(14.0f), ph = K.px(26.0f), px0 = fx - pw * 0.5f, py0 = cy - ph * 0.5f;
    plinth(K, px0, py0, pw, ph, K.px(3.0f), br, kt);
    cs(K.dev); rrect(K.dev, fx - K.px(1.0f), py0 + K.px(4.0f), K.px(2.0f), ph - K.px(8.0f), 0.0f, fa(0xFFC0281Eu), fa(0xFF8A1810u));
    window_face(K, x + tw + gap, cy - K.px(14.0f), winW, K.px(28.0f), val, K.px(14.5f), dis);
    return changed;
}

// ---- selector : a steel plinth with the value in its ivory window, an engraved arrow on each side ----
int kit_selector(Kit& K, int id, float x, float y, float w, const char* val, int st) {
    const bool dis = (st & KS_DISABLED) != 0;
    Dim dim(dis);
    const float h = K.px(40.0f), aw = K.px(40.0f), cy = y + h * 0.5f;
    const bool lh = !dis && inrect(K.mo, x, y, aw, h), rh = !dis && (inrect(K.mo, x + w - aw, y, aw, h) || (st & KS_HOVER));
    const float lt = ease(id, 0, lh ? 1.0f : 0.0f), rt = ease(id, 1, rh ? 1.0f : 0.0f);
    const Metal& m = STEEL(); const Metal& br = BRASS();
    plinth(K, x, y, w, h, K.px(6.0f), m, (lt > rt ? lt : rt) * 0.5f);
    window_face(K, x + aw, y + K.px(6.0f), w - 2.0f * aw, h - K.px(12.0f), val, K.px(13.5f), dis);
    // the arrows INLAID in the metal : brass when the pointer is on them
    const float s = K.px(6.0f);
    const u32 la = m_toward(m.dark, br.lite, lt), ra = m_toward(m.dark, br.lite, rt);
    { const float ax = x + aw * 0.5f; const float t[6] = { ax - s * 0.7f, cy, ax + s * 0.6f, cy - s, ax + s * 0.6f, cy + s }; cs(K.dev); fill_poly_aa(K.dev, t, 3, fa(la)); }
    { const float ax = x + w - aw * 0.5f; const float t[6] = { ax + s * 0.7f, cy, ax - s * 0.6f, cy - s, ax - s * 0.6f, cy + s }; cs(K.dev); fill_poly_aa(K.dev, t, 3, fa(ra)); }
    if (dis || !K.click) return 0;
    if (inrect(K.mo, x, y, aw, h)) return -1;
    if (inrect(K.mo, x + w - aw, y, aw, h)) return 1;
    return 0;
}

// a text INLAID in metal : the cut's shadow above, the letters in bright metal (the dial's engraved signs) ; readable first
static void inlay_l(Font* fo, u32 dev, float x, float cy, const char* s, float sz, u32 col) {
    fo->begin(dev);
    fo->draw_lc(dev, x, cy - 1.0f, s, sz, fa(0xC0000000u), 0, 0.0f);
    fo->draw_lc(dev, x, cy, s, sz, fa(col), fa(0x90000000u), 1.0f);
}
static void inlay_c(Font* fo, u32 dev, float cx, float cy, const char* s, float sz, u32 col) {
    fo->begin(dev);
    fo->draw_c(dev, cx, cy - 1.0f, s, sz, fa(0xC0000000u), 0, 0.0f);
    fo->draw_c(dev, cx, cy, s, sz, fa(col), fa(0x90000000u), 1.0f);
}
static void ink_c(Font* fo, u32 dev, float cx, float cy, const char* s, float sz, u32 col) {   // sepia ink on brass / ivory : no outline
    fo->begin(dev); fo->draw_c(dev, cx, cy, s, sz, fa(col), 0, 0.0f);
}
static const u32 INK = 0xFF2A190Au, TXT_METAL = 0xFFEDE6D6u;

// ---- section header : a cast steel bar ; the title inlaid, the chevron in brass, the modified count in a brass window ----
bool kit_section(Kit& K, int id, float x, float y, float w, const char* title, bool open, int nMod, int st) {
    const u32 dev = K.dev;
    const float h = K.px(34.0f), r = K.px(6.0f);   // 2026-11-26 : 42 -> 34 ("trop gros")
    const bool hov = (st & KS_HOVER) || inrect(K.mo, x, y, w, h);
    const float t = ease(id, 0, hov ? 1.0f : 0.0f), o = ease(id, 1, open ? 1.0f : 0.0f, 14.0f);
    Metal m = STEEL(); m.top = m_scale(m.top, 0.72f); m.bot = m_scale(m.bot, 0.72f);   // the bar : darker than the parts on it
    plinth(K, x, y, w, h, r, m, t * 0.5f);
    if (o > 0.01f) { cs(dev); rrect(dev, x + r, y + h - K.px(3.0f), w - 2.0f * r, K.px(2.0f), 0.0f, MA((u32)(200 * o), BRASS().lite), MA((u32)(200 * o), BRASS().body)); }   // open : a brass filet along its foot
    const float cx = x + w - K.px(22.0f), cy = y + h * 0.5f, s = K.px(5.5f);
    const u32 cc = m_toward(BRASS().body, BRASS().lite, t);
    if (o < 0.5f) { const float p[6] = { cx + s * 0.7f, cy, cx - s * 0.6f, cy - s, cx - s * 0.6f, cy + s }; cs(dev); fill_poly_aa(dev, p, 3, fa(cc)); }
    else          { const float p[6] = { cx, cy + s * 0.7f, cx - s, cy - s * 0.6f, cx + s, cy - s * 0.6f }; cs(dev); fill_poly_aa(dev, p, 3, fa(cc)); }
    inlay_l(K.fs, dev, x + K.px(16.0f), cy, title, K.px(TS_SECTION), TXT_METAL);
    if (nMod > 0) {
        char b[32]; _snprintf(b, sizeof(b), tr("%d modified", nMod > 1 ? "%d modifi\xC3\xA9s" : "%d modifi\xC3\xA9"), nMod); b[31] = 0;
        // ENGRAVED in the bar, muted brass (2026-11-26 : a brass plate on every section was noise)
        const float bx = cx - K.px(20.0f) - K.fr->measure(b, K.px(TS_NOTE));
        inlay_l(K.fr, dev, bx, cy, b, K.px(TS_NOTE), m_scale(BRASS().body, 0.80f));
    }
    return K.click && inrect(K.mo, x, y, w, h);
}

// ---- a row's ground : hover = a faint steel sheen ; focus = the FFXI pointer + a brass filet ; modified = a brass corner ----
void kit_row(Kit& K, float x, float y, float w, float h, int st) {
    const u32 dev = K.dev;
    const bool hov = (st & KS_HOVER) || (!(st & KS_DISABLED) && inrect(K.mo, x, y, w, h));
    const float r = K.px(6.0f);
    if (hov || (st & KS_FOCUS)) {
        cs(dev);
        rrect(dev, x, y, w, h, r, MA(0x40, STEEL().lite), MA(0x18, STEEL().body));
        rrect_stroke(dev, x, y, w, h, r, MA(0x30, STEEL().glint), 1.0f);
    }
    if (st & KS_FOCUS) {
        cs(dev);
        rrect_stroke(dev, x - K.px(1.0f), y - K.px(1.0f), w + K.px(2.0f), h + K.px(2.0f), r + K.px(1.0f), fa(BRASS().edge), K.px(1.0f));
        rrect_stroke(dev, x, y, w, h, r, fa(BRASS().lite), K.px(1.6f));
        kit_focus_cursor(K, x - K.px(6.0f), y + h * 0.5f);
    }
    if (st & KS_MODIFIED) {   // a brass BEAD set left of the label, like the dial's rivets (the corner triangle was refused : "moche")
        const float bx = x + K.px(8.0f), by = y + h * 0.5f - ((st & KS_DISABLED) ? K.px(8.5f) : 0.0f), r = K.px(3.6f);
        cs(dev);
        disc(dev, bx, by + K.px(0.8f), r + K.px(1.0f), fa(0x70000000u));     // it sits in the metal
        disc(dev, bx, by, r + K.px(0.8f), fa(BRASS().edge));
        disc(dev, bx, by, r, fa(BRASS().body));
        disc(dev, bx - r * 0.25f, by - r * 0.30f, r * 0.55f, fa(BRASS().lite));
        disc(dev, bx - r * 0.38f, by - r * 0.42f, r * 0.22f, MA(0xC0, BRASS().glint));
    }
}

void kit_label(Kit& K, float x, float cy, const char* s, int st, const char* reason) {
    const bool dis = (st & KS_DISABLED) != 0;
    const float dy = reason ? K.px(8.5f) : 0.0f;
    text_l(K.fr, K.dev, x, cy - dy, s, K.px(TS_LABEL), dis ? C_MUTE : C_TEXT);
    if (reason) text_l(K.fr, K.dev, x, cy + K.px(10.5f), reason, K.px(TS_NOTE), 0xFF8F7E5Cu);   // the reason : a faded sepia -- it explains, it must not call (bright brass made greyed rows the loudest)
}

// ---- buttons : cast plates -- main = brass with ink, secondary = steel with inlaid letters, destructive = steel with red enamel ----
bool kit_button(Kit& K, int id, float x, float y, float w, const char* s, int kind, int st) {
    const bool dis = (st & KS_DISABLED) != 0;
    const u32 dev = K.dev;
    const float h = K.px(42.0f), r = K.px(6.0f);
    const bool hov = !dis && ((st & KS_HOVER) || inrect(K.mo, x, y, w, h)), press = hov && K.mo && K.mo->down;
    const float t = ease(id, 0, hov ? 1.0f : 0.0f), pin = press ? K.px(1.0f) : 0.0f;
    const float bx = x + pin, by = y + pin, bw = w - 2.0f * pin, bh = h - 2.0f * pin;
    if (dis) {
        Metal m = STEEL(); m.top = m_scale(m.top, 0.55f); m.bot = m_scale(m.bot, 0.55f); m.glint = m_scale(m.glint, 0.5f);
        plinth(K, bx, by, bw, bh, r, m, 0.0f);
        inlay_c(K.fs, dev, x + w * 0.5f, y + h * 0.5f, s, K.px(TS_VALUE), 0xFF7E8590u);
        return false;
    }
    if (kind == 0) { plinth(K, bx, by, bw, bh, r, BRASS(), t); ink_c(K.fs, dev, x + w * 0.5f, y + h * 0.5f, s, K.px(TS_VALUE), INK); }
    else {
        plinth(K, bx, by, bw, bh, r, STEEL(), t);
        inlay_c(K.fs, dev, x + w * 0.5f, y + h * 0.5f, s, K.px(TS_VALUE), kind == 2 ? m_toward(0xFFE0675Cu, 0xFFFFB0A8u, t) : TXT_METAL);
    }
    return K.click && inrect(K.mo, x, y, w, h);
}

// ---- search : a field CUT into a steel plate (sunken), the magnifier engraved, a brass lip when it has the focus ----
void kit_search(Kit& K, float x, float y, float w, const char* text, int st) {
    const u32 dev = K.dev;
    const float h = K.px(44.0f), cy = y + h * 0.5f;
    const bool foc = (st & KS_FOCUS) != 0;
    const Metal& m = STEEL();
    plinth(K, x, y, w, h, K.px(7.0f), m, 0.0f);
    const float ix = x + K.px(6.0f), iy = y + K.px(6.0f), iw = w - K.px(12.0f), ih = h - K.px(12.0f);
    hollow(K, ix, iy, iw, ih, K.px(4.0f), m, 0xFF0B0E12u);
    if (foc) { cs(dev); rrect_stroke(dev, ix - K.px(1.6f), iy - K.px(1.6f), iw + K.px(3.2f), ih + K.px(3.2f), K.px(5.6f), fa(BRASS().lite), K.px(1.5f)); }
    const float rr = K.px(6.5f), mx = ix + K.px(18.0f), my = cy - K.px(1.5f);
    const u32 mc = foc ? BRASS().lite : m.lite;
    cs(dev); rrect_stroke(dev, mx - rr, my - rr, 2.0f * rr, 2.0f * rr, rr, fa(mc), K.px(2.0f));
    seg_soft(dev, mx + rr * 0.7f, my + rr * 0.7f, mx + rr * 1.6f, my + rr * 1.6f, K.px(2.4f), fa(mc));
    const bool empty = !text || !text[0];
    text_l(K.fr, dev, ix + K.px(38.0f), cy, empty ? "Rechercher un r\xC3\xA9glage\xE2\x80\xA6" : text, K.px(TS_VALUE), empty ? C_MUTE : C_TEXT);
    if (foc && !empty) { const float cx2 = ix + K.px(38.0f) + K.fr->measure(text, K.px(TS_VALUE)) + K.px(2.0f); flat(dev, cx2, cy - K.px(10.0f), K.px(2.0f), K.px(20.0f), BRASS().lite); }
}

void kit_result(Kit& K, float x, float y, float w, const char* label, const char* path, const char* val, int st) {
    const u32 dev = K.dev;
    const float h = K.px(60.0f);
    kit_row(K, x, y, w, h, st & (KS_HOVER | KS_FOCUS));
    text_l(K.fs, dev, x + K.px(16.0f), y + K.px(20.0f), label, K.px(TS_LABEL), C_TEXT);
    text_l(K.fr, dev, x + K.px(16.0f), y + K.px(43.0f), path, K.px(TS_NOTE), C_DIM);
    const float ww = K.px(76.0f);
    window_face(K, x + w - K.px(36.0f) - ww, y + h * 0.5f - K.px(13.0f), ww, K.px(26.0f), val, K.px(13.5f), false);
    const float cx = x + w - K.px(18.0f), cy = y + h * 0.5f, s = K.px(5.0f);
    tri(dev, cx + s * 0.7f, cy, cx - s * 0.6f, cy - s, cx - s * 0.6f, cy + s, BRASS().body);
}

// ---- colour : an enamel set in a steel bezel, under glass ; the chosen one ringed in brass ----
bool kit_swatch(Kit& K, int id, float x, float y, u32 col, int st) {
    (void)id;
    const u32 dev = K.dev;
    const float s = K.px(34.0f);
    if (st & KS_FOCUS) { cs(dev); rrect_stroke(dev, x - K.px(4.0f), y - K.px(4.0f), s + K.px(8.0f), s + K.px(8.0f), K.px(8.0f), fa(BRASS().lite), K.px(2.0f)); }
    plinth(K, x, y, s, s, K.px(6.0f), STEEL(), 0.0f);
    const float i = K.px(5.0f);
    hollow(K, x + i, y + i, s - 2.0f * i, s - 2.0f * i, K.px(3.0f), STEEL(), col);
    cs(dev);
    grad_quad(dev, x + i, y + i, s - 2.0f * i, (s - 2.0f * i) * 0.45f, MA(0x40, 0xFFFFFFu), MA(0x40, 0xFFFFFFu), MA(0x04, 0xFFFFFFu), MA(0x04, 0xFFFFFFu));
    return K.click && inrect(K.mo, x, y, s, s);
}

void kit_nav_group(Kit& K, float x, float y, const char* s) { text_l(K.fs, K.dev, x, y, s, K.px(15.0f), C_DIM); }

// ---- sidebar entry : plain until hovered ; the selected one a steel plate with a brass filet ; the status an enamel bead ----
bool kit_nav(Kit& K, int id, float x, float y, float w, const char* s, bool active, bool moduleOn, int st) {
    const u32 dev = K.dev;
    const float h = K.px(38.0f), r = K.px(6.0f);
    const bool hov = (st & KS_HOVER) || inrect(K.mo, x, y, w, h);
    const float t = ease(id, 0, hov ? 1.0f : 0.0f);
    if (active) {
        Metal m = STEEL(); m.top = m_scale(m.top, 0.80f); m.bot = m_scale(m.bot, 0.80f);
        plinth(K, x, y, w, h, r, m, t * 0.4f);
    } else if (t > 0.01f) { cs(dev); rrect(dev, x, y, w, h, r, MA((u32)(0x40 * t), STEEL().lite), MA((u32)(0x18 * t), STEEL().body)); }
    const float bx = x + w - K.px(18.0f), by = y + h * 0.5f;   // the bead in its steel setting
    cs(dev); disc(dev, bx, by, K.px(5.5f), fa(STEEL().edge)); disc(dev, bx, by, K.px(4.0f), fa(moduleOn ? 0xFF4FB07Cu : 0xFF3A414Bu));
    if (moduleOn) disc(dev, bx - K.px(1.2f), by - K.px(1.4f), K.px(1.4f), MA(0xA0, 0xFFFFFFu));
    if (active) inlay_l(K.fs, dev, x + K.px(18.0f), y + h * 0.5f, s, K.px(TS_NAV), BRASS().lite);   // where you are : the plate + its name in brass (no accent bar : refused)
    else text_l(K.fr, dev, x + K.px(18.0f), y + h * 0.5f, s, K.px(TS_NAV), lerpc(C_DIM, C_TEXT, t));
    return K.click && inrect(K.mo, x, y, w, h);
}

// ---- tab : steel plates ; the active one cast in brass with ink ----
bool kit_tab(Kit& K, int id, float x, float y, const char* s, bool active, int st) {
    const u32 dev = K.dev;
    Font* fo = active ? K.fs : K.fr;
    const float h = K.px(42.0f), w = fo->measure(s, K.px(TS_NAV)) + K.px(40.0f), r = K.px(6.0f);
    const bool hov = (st & KS_HOVER) || inrect(K.mo, x, y, w, h);
    const float t = ease(id, 0, hov ? 1.0f : 0.0f);
    if (active) { plinth(K, x, y, w, h, r, BRASS(), t); ink_c(fo, dev, x + w * 0.5f, y + h * 0.5f, s, K.px(TS_NAV), INK); }
    else {
        Metal m = STEEL(); m.top = m_scale(m.top, 0.78f); m.bot = m_scale(m.bot, 0.78f);
        plinth(K, x, y, w, h, r, m, t);
        inlay_c(fo, dev, x + w * 0.5f, y + h * 0.5f, s, K.px(TS_NAV), lerpc(0xFFB9C0CAu, TXT_METAL, t));
    }
    return K.click && inrect(K.mo, x, y, w, h);
}

void chrome_housing(Kit& K, float x, float y, float w, float h);   // defined below ; the stepper and the keys use it

// ======== the CHROME : the window around the pages (masthead, tabs, rail, bars, frames), in the same metal ========
// Drawn into the OLD geometry (the caller's px rects) : the restyle changes the material, not the layout.
void chrome_plate(Kit& K, float x, float y, float w, float h) {   // the masthead : a forged slab, lit from the top
    const u32 dev = K.dev; const Metal& m = STEEL();
    cs(dev);
    q4(dev, x, y, w, h, fa(m_scale(m.top, 0.42f)), fa(m_scale(m.top, 0.42f)), fa(m_scale(m.bot, 0.30f)), fa(m_scale(m.bot, 0.30f)));
    grad_quad(dev, x, y, w, h * 0.45f, MA(0x14, 0xFFFFFFu), MA(0x14, 0xFFFFFFu), 0, 0);   // the light on its upper face
    flat(dev, x, y + h - 1.0f, w, 1.0f, MA(0x60, m.glint));                              // the polished lower edge
}
float chrome_tag(Kit& K, float x, float cy, const char* s) {   // a small brass tag, the version stamped in it
    const float sz = K.px(TS_NOTE), h = K.px(24.0f), w = K.fs->measure(s, sz) + K.px(18.0f);
    plinth(K, x, cy - h * 0.5f, w, h, K.px(4.0f), BRASS(), 0.0f);
    ink_c(K.fs, K.dev, x + w * 0.5f, cy, s, sz, INK);
    return x + w;
}
// a segmented switch : a steel slot, the chosen segment a cast brass block (the toggle's language)
int chrome_segment(Kit& K, int id, float x, float y, float segW, float h, const char* const* labels, int n, int sel) {
    const u32 dev = K.dev; const Metal& m = STEEL();
    const float w = segW * n;
    plinth(K, x, y, w, h, K.px(5.0f), m, 0.0f);
    hollow(K, x + K.px(3.0f), y + K.px(3.0f), w - K.px(6.0f), h - K.px(6.0f), K.px(3.0f), m, 0xFF0B0E12u);
    int hit = -1;
    for (int i = 0; i < n; ++i) {
        const float sx = x + segW * i, cy = y + h * 0.5f;
        const bool hov = inrect(K.mo, sx, y, segW, h);
        const float t = ease(ctrl_uid_i(id, i), 0, hov ? 1.0f : 0.0f);
        const float bx = sx + K.px(3.0f), by = y + K.px(3.0f), bw = segW - K.px(6.0f), bh = h - K.px(6.0f);
        if (i == sel) {   // CHOSEN : pressed down into the slot, its face brass (2026-11-26 : "enfonce et jaune = valide")
            cs(dev);
            rrect(dev, bx, by, bw, bh, K.px(3.0f), fa(m_scale(BRASS().body, 0.92f)), fa(m_scale(BRASS().bot, 0.95f)));
            grad_quad(dev, bx, by, bw, bh * 0.45f, MA(0x70, 0x000000u), MA(0x70, 0x000000u), 0, 0);   // the slot's lip shades it : it sits LOW
            flat(dev, bx + K.px(2.0f), by + bh - 1.0f, bw - K.px(4.0f), 1.0f, MA(0x90, BRASS().lite));
            ink_c(K.fs, dev, sx + segW * 0.5f, cy + K.px(0.5f), labels[i], K.px(TS_NOTE), INK);
        } else {          // the other : a raised steel key, standing proud of the slot
            Metal km = STEEL(); km.top = m_scale(km.top, 0.62f); km.bot = m_scale(km.bot, 0.55f);
            plinth(K, bx, by - K.px(1.0f), bw, bh, K.px(3.0f), km, t);
            inlay_c(K.fr, dev, sx + segW * 0.5f, cy - K.px(1.0f), labels[i], K.px(TS_NOTE), m_toward(0xFFAEB6C1u, TXT_METAL, t));
            if (hov && K.click) hit = i;
        }
    }
    return hit;
}
// A RUBY set in steel (2026-11-26 : the destructive keys "en ruby", in the world's lore -- crystals and gems, not paint).
// A cut stone seen face-on : deep at the top where the setting shades it, the light coming THROUGH it and pooling at
// the bottom (a gem is lit from behind its own facets), a crisp reflection on its table, two facet lines.
static void ruby_face(Kit& K, float x, float y, float w, float h, float r, float t, bool dim) {
    // a STEP-CUT RUBY seen face-on (2026-11-26 : "un rouge plus sombre, qu'on dirait du ruby") : pigeon-blood red, very
    // dark where the facets turn away, alive where the light comes back through. The crown's four facets frame a flat
    // TABLE : the top one darkest (the setting shades it), the bottom one brightest (the light returning through the
    // stone), the sides between ; the table itself a deep red rising toward its foot ; one quiet reflection on it.
    const u32 dev = K.dev;
    const float k = dim ? 0.45f : 1.0f;
    auto C = [&](u32 c) { return m_toward(m_scale(c, k), 0xFF201A1Bu, dim ? 0.55f : 0.0f); };
    const u32 cTop = C(0xFF1A0105u), cSide = C(0xFF34040Cu), cSideR = C(0xFF2A030Au), cBot = C(m_toward(0xFF6E0818u, 0xFF861024u, t));
    const u32 tTop = C(0xFF3A040Cu), tBot = C(m_toward(0xFF5C0614u, 0xFF6E0A1Au, t));
    const float bw = h * 0.26f > K.px(7.0f) ? K.px(7.0f) : h * 0.26f;   // the crown facets' width
    const float x0 = x, y0 = y, x1 = x + w, y1 = y + h, ix0 = x + bw * 1.4f, iy0 = y + bw, ix1 = x + w - bw * 1.4f, iy1 = y + h - bw;
    cs(dev);
    rrect(dev, x, y, w, h, r, fa(C(0xFF140104u)), fa(C(0xFF140104u)));                     // the stone's body (under the facets)
    const float top[8] = { x0 + r * 0.6f, y0, x1 - r * 0.6f, y0, ix1, iy0, ix0, iy0 };
    const float bot[8] = { ix0, iy1, ix1, iy1, x1 - r * 0.6f, y1, x0 + r * 0.6f, y1 };
    const float lft[8] = { x0, y0 + r * 0.6f, ix0, iy0, ix0, iy1, x0, y1 - r * 0.6f };
    const float rgt[8] = { ix1, iy0, x1, y0 + r * 0.6f, x1, y1 - r * 0.6f, ix1, iy1 };
    fill_poly_aa(dev, top, 4, fa(cTop)); fill_poly_aa(dev, bot, 4, fa(cBot));
    fill_poly_aa(dev, lft, 4, fa(cSide)); fill_poly_aa(dev, rgt, 4, fa(cSideR));
    q4(dev, ix0, iy0, ix1 - ix0, iy1 - iy0, fa(tTop), fa(tTop), fa(tBot), fa(tBot));          // the table
    // the facet edges : hairlines where the planes meet, catching a little light
    seg_soft(dev, ix0, iy0, ix1, iy0, 1.0f, MA(dim ? 0x10 : 0x2A, 0xFFFFFFu));
    seg_soft(dev, ix0, iy1, ix1, iy1, 1.0f, MA(dim ? 0x10 : 0x38, 0xFF8090u));
    seg_soft(dev, x0 + r * 0.3f, y0 + r * 0.3f, ix0, iy0, 1.0f, MA(0x22, 0xFFFFFFu));
    seg_soft(dev, x1 - r * 0.3f, y1 - r * 0.3f, ix1, iy1, 1.0f, MA(0x26, 0xFF8090u));
    rrect_stroke(dev, x + 0.5f, y + 0.5f, w - 1.0f, h - 1.0f, r, fa(C(0xFF0C0002u)), K.px(1.2f));   // its girdle, in the setting
    grad_quad(dev, ix0, iy0 + K.px(1.0f), ix1 - ix0, (iy1 - iy0) * 0.35f, MA(dim ? 0x08 : 0x1C, 0xFFFFFFu), MA(dim ? 0x08 : 0x1C, 0xFFFFFFu), 0, 0);   // the table's reflection
}
bool chrome_close(Kit& K, int id, float x, float y, float s) {
    // a steel key, the X ENGRAVED in it : two cuts, dark, with the light caught on their lower lip -- no red paint
    // (2026-11-26 : the red "pas credible"). Hover lifts the key like every other ; pressed, it goes down.
    const bool hov = inrect(K.mo, x, y, s, s), press = hov && K.mo && K.mo->down;
    const float t = ease(id, 0, hov ? 1.0f : 0.0f), d = press ? K.px(1.2f) : 0.0f;
    Metal m = STEEL(); if (press) { m.top = m_scale(m.top, 0.82f); m.bot = m_scale(m.bot, 0.88f); }
    plinth(K, x, y + d, s, s - d, K.px(6.0f), m, press ? 0.0f : t * 0.6f);
    // the X : a RUBY CROSS set in the key (2026-11-26 : "la croix doit devenir rouge mais belle") -- its setting cut
    // into the steel, the stone deep red with the light running along its arms, a spark on each ; brighter on hover
    const float c = s * 0.5f, a = s * 0.20f, cx = x + c, cy = y + c + d * 0.5f, w2 = K.px(6.0f);
    cs(K.dev);
    for (int k = 0; k < 2; ++k) {   // the setting : a dark cut a little wider than the stone
        const float sx = k == 0 ? 1.0f : -1.0f;
        seg_soft(K.dev, cx - a * sx, cy - a, cx + a * sx, cy + a, w2 + K.px(2.4f), fa(m_scale(m.edge, 0.5f)));
    }
    for (int k = 0; k < 2; ++k) {   // the stone
        const float sx = k == 0 ? 1.0f : -1.0f;
        seg_soft(K.dev, cx - a * sx, cy - a, cx + a * sx, cy + a, w2, fa(m_toward(0xFF2A030Au, 0xFF3A050Eu, t)));
    }
    for (int k = 0; k < 2; ++k) {   // the light through it
        const float sx = k == 0 ? 1.0f : -1.0f;
        seg_soft(K.dev, cx - a * sx * 0.85f, cy - a * 0.85f + K.px(0.8f), cx + a * sx * 0.85f, cy + a * 0.85f + K.px(0.8f), w2 * 0.45f, fa(m_toward(0xFF6A0818u, 0xFF861024u, t)));
    }
    for (int k = 0; k < 2; ++k) {   // a quiet reflection along the upper edge
        const float sx = k == 0 ? 1.0f : -1.0f;
        seg_soft(K.dev, cx - a * sx * 0.7f, cy - a * 0.7f - K.px(1.3f), cx + a * sx * 0.7f, cy + a * 0.7f - K.px(1.3f), K.px(0.9f), MA(0x40, 0xFFFFFFu));
    }
    return hov && K.click;
}
void chrome_tab_text(Kit& K, float cx, float cy, const char* s, bool active, float t, float sz) {
    if (active) ink_c(K.fs, K.dev, cx, cy, s, sz, INK);
    else inlay_c(K.fr, K.dev, cx, cy, s, sz, m_toward(0xFFAEB6C1u, TXT_METAL, t));
}
u32 chrome_tab_ink(bool active, float t) { return active ? INK : m_toward(0xFFAEB6C1u, TXT_METAL, t); }
// a RAIL entry (the module list) : plain ; hovered a steel sheen ; the chosen one a steel plate, its name in brass
void chrome_nav(Kit& K, float x, float y, float w, float h, const char* s, bool active, float t) {
    const u32 dev = K.dev;
    if (active) { Metal m = STEEL(); m.top = m_scale(m.top, 0.80f); m.bot = m_scale(m.bot, 0.80f); plinth(K, x, y, w, h, K.px(5.0f), m, 0.0f); }
    else if (t > 0.01f) { cs(dev); rrect(dev, x, y, w, h, K.px(5.0f), MA((u32)(0x40 * t), STEEL().lite), MA((u32)(0x18 * t), STEEL().body)); }
    if (active) inlay_l(K.fs, dev, x + K.px(14.0f), y + h * 0.5f, s, K.px(TS_NAV), BRASS().lite);
    else text_l(K.fr, dev, x + K.px(14.0f), y + h * 0.5f, s, K.px(TS_NAV), lerpc(0xFF9AA3AEu, C_TEXT, t));
}
void chrome_nav_group(Kit& K, float x, float cy, const char* s) {   // a group title : engraved, muted brass
    inlay_l(K.fs, K.dev, x, cy, s, K.px(TS_NOTE), m_scale(BRASS().body, 0.80f));
}
// a WELL cut into the page (the module rail) : a dark floor, its upper edge in shadow, a lit lower lip -- a zone told
// apart by DEPTH, not by an outline (2026-11-26 : the flat rims round the rail and the content "font con")
// a RIVET : a domed steel head set in the plate (the dial case's screws)
void chrome_rivet(Kit& K, float cx, float cy) {
    const u32 dev = K.dev; const Metal& m = STEEL(); const float r = K.px(3.4f);
    cs(dev);
    disc(dev, cx, cy + K.px(0.9f), r + K.px(1.0f), MA(0x90, 0x000000u));   // seated in the metal
    disc(dev, cx, cy, r + K.px(0.6f), fa(m.edge));
    disc(dev, cx, cy, r, fa(m.bot));
    disc(dev, cx - r * 0.18f, cy - r * 0.22f, r * 0.70f, fa(m.body));
    disc(dev, cx - r * 0.35f, cy - r * 0.40f, r * 0.30f, MA(0xB0, m.glint));
}
// a RAISED plate of dark forged steel, riveted at its corners (the module rail)
static Metal forged() {   // the assembly's forged steel : the beam and the rail plate are ONE casting
    Metal m = STEEL(); m.top = m_scale(m.top, 0.46f); m.bot = m_scale(m.bot, 0.40f); m.glint = m_scale(m.glint, 0.70f); return m;
}
// THE CASTING : the beam and the rail plate as ONE piece -- an L of forged steel (2026-11-26 : their vertical borders
// "devraient etre communs et alignes", "fusionne tout ca de facon credible"). Drawn in the plinth's layers, but each
// layer for BOTH shapes before the next : every silhouette, then every bevel, then every body -- so the seams between
// the two pieces are covered and the outline runs round the whole L unbroken ; one gradient carries the beam's foot down
// the plate ; the inside corner is a fillet. railW <= 0 : the beam alone (a page without the rail).
void chrome_casting(Kit& K, float x, float by, float bw, float bh, float railW, float railBot) {
    const u32 dev = K.dev; const Metal m = forged();
    const float o = K.px(1.4f), b = K.px(1.1f), r = K.px(6.0f);
    const bool rail = railW > 0.0f;
    const float ry = by, rh = railBot - by;
    cs(dev);
    rrect_glow(dev, x, by + K.px(2.0f), bw, bh, r, MA(0x80, 0x000000u), K.px(5.0f));
    if (rail) rrect_glow(dev, x, ry + K.px(2.0f), railW, rh, r, MA(0x80, 0x000000u), K.px(5.0f));
    // 1. the silhouettes
    rrect(dev, x - o, by - o, bw + 2.0f * o, bh + 2.0f * o, r + o, fa(m_scale(m.edge, 0.85f)), fa(m_scale(m.edge, 0.70f)));
    if (rail) rrect(dev, x - o, ry - o, railW + 2.0f * o, rh + 2.0f * o, r + o, fa(m_scale(m.edge, 0.85f)), fa(m_scale(m.edge, 0.55f)));
    // 2. the bevels (the rail's last : along the common left edge it is the one that shows, top to bottom in one run)
    const u32 bvTop = m.glint, bvBot = m_scale(m.dark, 0.85f);
    rrect(dev, x - o * 0.35f, by - o * 0.35f, bw + o * 0.7f, bh + o * 0.7f, r + o * 0.35f, fa(bvTop), fa(m_toward(bvTop, bvBot, 0.5f)));
    if (rail) rrect(dev, x - o * 0.35f, ry - o * 0.35f, railW + o * 0.7f, rh + o * 0.7f, r + o * 0.35f, fa(bvTop), fa(bvBot));
    // 3. the bodies : the plate from the beam's foot down, the beam over it, in ONE gradient
    const u32 cBeamTop = m.top, cFoot = m.bot, cBot = m_scale(m.bot, 0.78f);
    if (rail) q4(dev, x + b * 0.65f, by + bh - b * 2.0f, railW - b * 1.3f, (railBot - b * 1.9f) - (by + bh - b * 2.0f), fa(cFoot), fa(cFoot), fa(cBot), fa(cBot));
    rrect(dev, x + b * 0.65f, by + b, bw - b * 1.3f, bh - b * 1.9f, r - b * 0.8f, fa(cBeamTop), fa(cFoot));
    if (rail) q4(dev, x + b * 0.65f, by + bh - r, railW - b * 1.3f, r + K.px(1.0f), fa(cFoot), fa(cFoot), fa(cFoot), fa(cFoot));   // the beam's rounded foot corners, filled where the plate continues
    grad_quad(dev, x + K.px(2.0f), by + K.px(2.0f), bw - K.px(4.0f), bh * 0.40f, MA(0x12, 0xFFFFFFu), MA(0x12, 0xFFFFFFu), 0, 0);   // the beam's upper face
    if (!rail) return;
    // 4. the FILLET in the inside corner (x + railW, beam's foot)
    const float x1 = x + railW, y = by + bh, f = K.px(10.0f);
    const int N = 12; const float cx = x1 + f, cy = y + f;
    float px = cx, py = y - b;
    for (int k = 1; k <= N; ++k) {
        const float a = 3.14159265f * (1.5f - 0.5f * (float)k / (float)N);
        const float nx = cx + f * cosf(a), ny = cy + f * sinf(a);
        fill_tri(dev, x1 - b, y - b, px, py, nx, ny, fa(cFoot));
        px = nx; py = ny;
    }
    fill_tri(dev, x1 - b, y - b, px, py, x1 - b, cy + K.px(1.0f), fa(cFoot));
    float qx = cx, qy = y;
    for (int k = 1; k <= N; ++k) {
        const float a = 3.14159265f * (1.5f - 0.5f * (float)k / (float)N);
        const float nx = cx + f * cosf(a), ny = cy + f * sinf(a);
        seg_soft(dev, qx + o * 0.5f, qy + o * 0.5f, nx + o * 0.5f, ny + o * 0.5f, o, fa(m_scale(m.edge, 0.7f)));
        seg_soft(dev, qx, qy, nx, ny, K.px(1.0f), fa(m_scale(m.dark, 0.85f)));
        qx = nx; qy = ny;
    }
}
void chrome_rail_rivets(Kit& K, float x, float y, float w, float h) {
    const float i = K.px(8.0f);
    chrome_rivet(K, x + i, y + h - i); chrome_rivet(K, x + w - i, y + h - i);
}
// a HOUSING plate (a group of controls set into the beam) : the stepper's steel
void chrome_housing(Kit& K, float x, float y, float w, float h) {
    Metal hm = STEEL(); hm.top = m_scale(hm.top, 0.70f); hm.bot = m_scale(hm.bot, 0.66f);
    plinth(K, x, y, w, h, K.px(5.0f), hm, 0.0f);
}
// the frame's bars : the rail's dark forged steel (a bright bar read as a chrome tube, not the same assembly)
static Metal frame_metal() {
    Metal m = STEEL(); const float k = 0.60f;
    m.top = m_scale(m.top, k); m.lite = m_scale(m.lite, k); m.bot = m_scale(m.bot, k); m.glint = m_scale(m.glint, 0.75f);
    return m;
}
// a MULLION : the bar of the same frame between two of its openings -- dark edges, lit along its middle (the bezel's profile)
void chrome_mullion(Kit& K, float x, float y, float w, float h) {
    const u32 dev = K.dev; const Metal m = frame_metal();
    cs(dev);
    // a rounded bar lit from the top-left : its left face catches the light, the crest a bright line, the right falls away
    const float c = w * 0.38f;
    grad_quad(dev, x, y, c, h, fa(m.top), fa(m.lite), fa(m.top), fa(m.lite));
    grad_quad(dev, x + c, y, w - c, h, fa(m.lite), fa(m.bot), fa(m.lite), fa(m.bot));
    flat(dev, x + c - 0.5f, y, 1.0f, h, MA(0x70, m.glint));                                  // the crest
    flat(dev, x, y, 1.0f, h, fa(m_scale(m.edge, 0.9f))); flat(dev, x + w - 1.0f, y, 1.0f, h, fa(m_scale(m.edge, 0.6f)));
}
// a SLOT milled into a plate : the keys / controls sit in it
void chrome_slot(Kit& K, float x, float y, float w, float h) { hollow(K, x, y, w, h, K.px(4.0f), STEEL(), 0xFF0A0D11u); }
// the STEPPER (the profile switcher) : one steel housing ; at each end a key seated in its own socket -- raised at rest,
// PRESSED DOWN into the socket while the button is held -- and the ivory window between (2026-11-26 : the arrows "ne
// s'appuient pas, il n'y a pas de metal autour"). Returns -1 / +1 on a click.
int chrome_stepper(Kit& K, int id, float x, float y, float w, float h, float kw, const char* s, bool dim, bool housing) {
    const u32 dev = K.dev;
    if (housing) chrome_housing(K, x, y, w, h);
    const float g = K.px(3.0f), kh = h - 2.0f * g;
    int hit = 0;
    for (int i = 0; i < 2; ++i) {
        const float kx = i == 0 ? x + g : x + w - g - kw, ky = y + g;
        const bool hov = inrect(K.mo, kx, ky, kw, kh), press = hov && K.mo && K.mo->down;
        const float t = ease(ctrl_uid_i(id, i), 0, hov ? 1.0f : 0.0f);
        hollow(K, kx, ky, kw, kh, K.px(3.0f), STEEL(), 0xFF07090Cu);                    // the socket
        const float d = press ? K.px(1.2f) : 0.0f;
        if (press) {   // pressed : down in the socket, its face in the socket's shadow
            cs(dev);
            rrect(dev, kx + K.px(1.0f), ky + K.px(1.5f), kw - K.px(2.0f), kh - K.px(2.0f), K.px(3.0f), fa(m_scale(STEEL().bot, 0.70f)), fa(m_scale(STEEL().bot, 0.85f)));
            grad_quad(dev, kx + K.px(1.0f), ky + K.px(1.5f), kw - K.px(2.0f), kh * 0.4f, MA(0x80, 0x000000u), MA(0x80, 0x000000u), 0, 0);
        } else {
            Metal km = STEEL(); km.top = m_scale(km.top, 0.80f); km.bot = m_scale(km.bot, 0.72f);
            plinth(K, kx + K.px(1.0f), ky + K.px(0.5f), kw - K.px(2.0f), kh - K.px(2.0f), K.px(3.0f), km, t);
        }
        const float cx = kx + kw * 0.5f, cy = ky + kh * 0.5f + d, a = kh * 0.20f, dir = i == 0 ? -1.0f : 1.0f;
        const float p[6] = { cx + dir * a * 0.8f, cy, cx - dir * a * 0.6f, cy - a, cx - dir * a * 0.6f, cy + a };
        cs(dev); fill_poly_aa(dev, p, 3, fa(m_toward(m_scale(STEEL().dark, 0.8f), BRASS().lite, press ? 1.0f : t)));
        if (hov && K.click) hit = i == 0 ? -1 : 1;
    }
    const float wx = x + g + kw + g, ww = w - 2.0f * (g + kw + g);
    window_face(K, wx, y + K.px(4.0f), ww, h - K.px(8.0f), s, K.px(TS_VALUE), dim);
    return hit;
}
// a KEY BUTTON (Save) : seated in its socket like the stepper's keys ; raised at rest, PRESSED DOWN while held ; its face
// brass when there is something to do (unsaved changes), steel with the word inlaid when there is not
// text ENGRAVED in metal (a key's word) : the cut's shadow above, the light caught on its lower lip below, the letters
// with NO outline -- an outlined word reads as a sticker laid on the key (2026-11-26 : "pas de la meme qualite ... faux")
static void engrave_c(Font* fo, u32 dev, float cx, float cy, const char* s, float sz, u32 col) {
    fo->begin(dev);
    fo->draw_c(dev, snap(cx), snap(cy) - 1.0f, s, sz, fa(0xB0000000u), 0, 0.0f);
    fo->draw_c(dev, snap(cx), snap(cy) + 1.0f, s, sz, MA(0x38, 0xFFFFFFu), 0, 0.0f);
    fo->draw_c(dev, snap(cx), snap(cy), s, sz, fa(col), 0, 0.0f);
}
bool chrome_keybtn(Kit& K, int id, float x, float y, float w, float h, const char* s, int kind, bool enabled, bool housing) {
    // ONE cast key, the selector's quality : a standalone button is a single plinth (no housing + socket + key = three
    // nested outlines) ; set INTO a housing (the profile group) it sits in a socket cut there. Pressed : it goes down a
    // step and into its own shadow.
    const bool brass = kind == 1;
    const u32 dev = K.dev;
    float kx = x, ky = y, kw = w, kh = h;
    if (!housing) { const float g = K.px(3.0f); kx = x + g; ky = y + g; kw = w - 2.0f * g; kh = h - 2.0f * g;
                    hollow(K, kx, ky, kw, kh, K.px(3.0f), STEEL(), 0xFF07090Cu); kx += K.px(1.0f); ky += K.px(0.5f); kw -= K.px(2.0f); kh -= K.px(2.0f); }
    if (kind == 3) {   // LATCHED : the state it stands for is the current one -- held DOWN, its face brass (the tabs' and the
                       // Simple / Advanced switch's language : "enfonce et jaune = valide"), not greyed out
        cs(dev);
        rrect(dev, kx - K.px(1.4f), ky - K.px(1.4f), kw + K.px(2.8f), kh + K.px(2.8f), K.px(4.0f), fa(0xFF06080Bu), fa(m_scale(STEEL().lite, 0.45f)));   // the recess it sits in
        rrect(dev, kx, ky + K.px(1.2f), kw, kh - K.px(1.2f), K.px(3.0f), fa(m_scale(BRASS().body, 0.92f)), fa(m_scale(BRASS().bot, 0.95f)));
        grad_quad(dev, kx, ky + K.px(1.2f), kw, kh * 0.42f, MA(0x70, 0x000000u), MA(0x70, 0x000000u), 0, 0);
        flat(dev, kx + K.px(3.0f), ky + kh - 1.0f, kw - K.px(6.0f), 1.0f, MA(0x90, BRASS().lite));
        ink_c(K.fs, dev, kx + kw * 0.5f, ky + kh * 0.5f + K.px(0.8f), s, K.px(TS_VALUE), INK);
        (void)id; (void)enabled;
        return false;
    }
    const bool hov = enabled && inrect(K.mo, kx, ky, kw, kh), press = hov && K.mo && K.mo->down;
    const float t = ease(id, 0, hov ? 1.0f : 0.0f), d = press ? K.px(1.2f) : 0.0f;
    // kind 2 (destructive) is a plain steel key : the danger is said by the confirmation, not by paint (red text and an
    // enamel bead were both "pas credible"). Kind 4 is THAT moment -- the armed delete, the confirm : the whole key a
    // red-LACQUERED part, the word engraved through the lacquer.
    Metal km = brass ? BRASS() : STEEL();   // kind 4 (armed / confirm) : the same steel key -- the red is the MARK, not the key
    if (!brass) { km.top = m_scale(km.top, enabled ? 0.80f : 0.55f); km.bot = m_scale(km.bot, enabled ? 0.72f : 0.50f); km.glint = m_scale(km.glint, enabled ? 1.0f : 0.6f); }
    if (press) { km.top = m_scale(km.top, 0.82f); km.bot = m_scale(km.bot, 0.88f); km.glint = m_scale(km.glint, 0.7f); }
    plinth(K, kx, ky + d, kw, kh - d, K.px(housing ? 6.0f : 3.0f), km, press ? 0.0f : t * 0.6f);
    const float cx = kx + kw * 0.5f, cy = ky + kh * 0.5f + d * 0.5f;
    if (brass) ink_c(K.fs, dev, cx, cy, s, K.px(TS_VALUE), INK);
    else if (kind == 2 || kind == 4) {   // DESTRUCTIVE : the key IS a ruby, set in its steel (the plinth above is the setting)
        const float g = K.px(3.5f);
        ruby_face(K, kx + g, ky + d + g, kw - 2.0f * g, (kh - d) - 2.0f * g, K.px(3.0f), kind == 4 ? 1.0f : t, !enabled);
        // the word engraved in the stone : pale, its cut a shade of the ruby, no outline
        const float sz = K.px(TS_VALUE);
        K.fs->begin(dev);
        K.fs->draw_c(dev, snap(cx), snap(cy) - 1.0f, s, sz, fa(0xC0300006u), 0, 0.0f);
        K.fs->draw_c(dev, snap(cx), snap(cy), s, sz, fa(!enabled ? 0xFF8A6A6Au : 0xFFF6E2DCu), 0, 0.0f);
    }
    else engrave_c(K.fs, dev, cx, cy, s, K.px(TS_VALUE), !enabled ? 0xFF6E7580u : m_toward(0xFFB9C0CAu, 0xFFE4E8EEu, t));
    return hov && K.click;
}
// ---- text set into the metal (the pages' labels) : 0 engraved metal, 1 a note, 2 engraved brass, 3 red enamel, 4 group caps ----
void chrome_label(Kit& K, float x, float cy, const char* s, float sz, int kind) {
    switch (kind) {
    case 0: inlay_l(K.fs, K.dev, x, cy, s, sz, TXT_METAL); break;
    case 1: text_l(K.fr, K.dev, x, cy, s, sz, 0xFF8E96A1u); break;
    case 2: inlay_l(K.fs, K.dev, x, cy, s, sz, BRASS().lite); break;
    case 3: inlay_l(K.fs, K.dev, x, cy, s, sz, 0xFFE0675Cu); break;
    default: inlay_l(K.fs, K.dev, x, cy, s, sz, m_scale(BRASS().body, 0.80f)); break;
    }
}
float chrome_label_w(Kit& K, const char* s, float sz) { return K.fs->measure(s, sz); }
// a TEXT FIELD : a slot cut into a steel plate ; a brass lip round it while it has the keyboard
void chrome_field(Kit& K, float x, float y, float w, float h, float focus) {
    const u32 dev = K.dev;
    chrome_housing(K, x, y, w, h);
    const float g = K.px(4.0f);
    hollow(K, x + g, y + g, w - 2.0f * g, h - 2.0f * g, K.px(3.0f), STEEL(), 0xFF07090Cu);
    if (focus > 0.01f) { cs(dev); rrect_stroke(dev, x + g - K.px(1.6f), y + g - K.px(1.6f), w - 2.0f * g + K.px(3.2f), h - 2.0f * g + K.px(3.2f), K.px(4.6f), MA((u32)(0xE0 * focus), BRASS().lite), K.px(1.4f)); }
}
// a MEDALLION (the character) : a brass bezel, a dark enamel face, the initial engraved in brass
void chrome_medallion(Kit& K, float cx, float cy, float r, const char* s) {
    const u32 dev = K.dev; const Metal& b = BRASS();
    cs(dev);
    disc(dev, cx, cy + K.px(2.0f), r + K.px(2.0f), MA(0x80, 0x000000u));
    disc(dev, cx, cy, r + K.px(1.2f), fa(b.edge));
    disc(dev, cx, cy, r, fa(b.body));
    disc(dev, cx - r * 0.10f, cy - r * 0.12f, r * 0.86f, fa(b.top));
    disc(dev, cx, cy, r * 0.78f, fa(b.dark));
    disc(dev, cx, cy + r * 0.04f, r * 0.72f, fa(0xFF141A22u));
    disc(dev, cx - r * 0.15f, cy - r * 0.2f, r * 0.45f, MA(0x18, 0xFFFFFFu));
    inlay_c(K.fs, dev, cx, cy, s, r * 0.95f, BRASS().lite);
}
// a ROW PLATE (a list entry) : dark steel, lit on hover ; the chosen one with a brass filet along its foot
void chrome_rowplate(Kit& K, float x, float y, float w, float h, bool active, float t) {
    Metal m = STEEL(); m.top = m_scale(m.top, active ? 0.70f : 0.58f); m.bot = m_scale(m.bot, active ? 0.66f : 0.54f);
    plinth(K, x, y, w, h, K.px(5.0f), m, t);
    if (active) { cs(K.dev); rrect(K.dev, x + K.px(6.0f), y + h - K.px(3.0f), w - K.px(12.0f), K.px(2.0f), 0.0f, MA(0xD0, BRASS().lite), MA(0xD0, BRASS().body)); }
}
// a PLAQUE : a cast brass plate, the words stamped in ink (the installed version)
void chrome_plaque(Kit& K, float x, float y, float w, float h, const char* s, float sz) {
    plinth(K, x, y, w, h, K.px(6.0f), BRASS(), 0.0f);
    cs(K.dev); grad_quad(K.dev, x + K.px(2.0f), y + K.px(2.0f), w - K.px(4.0f), h * 0.45f, MA(0x22, 0xFFFFFFu), MA(0x22, 0xFFFFFFu), 0, 0);
    ink_c(K.fs, K.dev, x + w * 0.5f, y + h * 0.5f, s, sz, INK);
}
// a small STEEL TAG (a badge that is not the brass one)
float chrome_tag_steel(Kit& K, float x, float cy, const char* s) {
    const float sz = K.px(TS_NOTE), h = K.px(24.0f), w = K.fs->measure(s, sz) + K.px(18.0f);
    Metal m = STEEL(); m.top = m_scale(m.top, 0.80f); m.bot = m_scale(m.bot, 0.74f);
    plinth(K, x, cy - h * 0.5f, w, h, K.px(4.0f), m, 0.0f);
    inlay_c(K.fs, K.dev, x + w * 0.5f, cy, s, sz, TXT_METAL);
    return x + w;
}
// a TAB KEY in the beam's slot : the chosen one PRESSED DOWN, its face brass ; the others raised steel (the Simple /
// Advanced switch's language, the one the user approved : "enfonce et jaune = valide")
float chrome_tabkey(Kit& K, int id, float x, float y, float w, float h, bool active) {
    const u32 dev = K.dev;
    const bool hov = !active && inrect(K.mo, x, y, w, h);
    const float t = ease(id, 0, hov ? 1.0f : 0.0f);
    if (active) {
        cs(dev);
        rrect(dev, x, y, w, h, K.px(4.0f), fa(m_scale(BRASS().body, 0.92f)), fa(m_scale(BRASS().bot, 0.95f)));
        grad_quad(dev, x, y, w, h * 0.42f, MA(0x70, 0x000000u), MA(0x70, 0x000000u), 0, 0);
        flat(dev, x + K.px(3.0f), y + h - 1.0f, w - K.px(6.0f), 1.0f, MA(0x90, BRASS().lite));
    } else {
        Metal km = STEEL(); km.top = m_scale(km.top, 0.62f); km.bot = m_scale(km.bot, 0.55f);
        plinth(K, x, y - K.px(1.0f), w, h, K.px(4.0f), km, t);
    }
    return t;
}
// a TRANSOM : the frame's horizontal bar between two openings (the mullion's profile, turned)
void chrome_transom(Kit& K, float x, float y, float w, float h) {
    const u32 dev = K.dev; const Metal m = frame_metal();
    cs(dev);
    // the same bar laid flat : its upper face in the light, the crest, the underside in shadow
    const float c = h * 0.38f;
    grad_quad(dev, x, y, w, c, fa(m.top), fa(m.top), fa(m.lite), fa(m.lite));
    grad_quad(dev, x, y + c, w, h - c, fa(m.lite), fa(m.lite), fa(m.bot), fa(m.bot));
    flat(dev, x, y + c - 0.5f, w, 1.0f, MA(0x70, m.glint));
    flat(dev, x, y, w, 1.0f, fa(m_scale(m.edge, 0.9f))); flat(dev, x, y + h - 1.0f, w, 1.0f, fa(m_scale(m.edge, 0.6f)));
}
// a WELD SEAM along a joint (ax,ay)->(bx,by) : the bead laid in overlapping ripples, each a little different (a fixed
// pseudo-random per position, so it never shimmers), its shadow below, the light on its crown, and the faint heat tint
// the torch leaves in the steel beside it -- without it two parts that "meet" read as drawn, not joined
void chrome_weld(Kit& K, float ax, float ay, float bx, float by) {
    const u32 dev = K.dev; const Metal& m = STEEL();
    const float dx = bx - ax, dy = by - ay, len = sqrtf(dx * dx + dy * dy);
    if (len < 1.0f) return;
    const float ux = dx / len, uy = dy / len, nx = -uy, ny = ux;
    const float r = K.px(2.1f), step = K.px(1.5f);
    cs(dev);
    // the heat tint : straw then blue, very faint, a band either side of the seam
    for (float t = 0.0f; t <= len; t += K.px(3.0f)) {
        const float cx = ax + ux * t, cy = ay + uy * t;
        disc(dev, cx, cy, r * 2.6f, MA(0x10, 0x5A6E9Au));
        disc(dev, cx, cy, r * 1.9f, MA(0x12, 0xB08A4Au));
    }
    unsigned seed = (unsigned)(ax * 7.0f + ay * 13.0f) * 2654435761u;
    for (float t = 0.0f; t <= len; t += step) {
        seed = seed * 1103515245u + 12345u;
        const float j = ((float)((seed >> 16) & 0xFF) / 255.0f - 0.5f);           // -0.5 .. 0.5
        const float rr = r * (0.88f + 0.24f * (j + 0.5f));
        const float cx = ax + ux * t + nx * j * K.px(0.6f), cy = ay + uy * t + ny * j * K.px(0.6f);
        disc(dev, cx + K.px(0.5f), cy + K.px(0.9f), rr, MA(0x90, 0x000000u));        // its shadow
        disc(dev, cx, cy, rr, fa(m_scale(m.body, 0.82f + 0.12f * j)));                // the bead
        disc(dev, cx - rr * 0.25f, cy - rr * 0.35f, rr * 0.45f, MA(0x70, m.glint));   // the ripple's crown
    }
}
// the FLOOR of an opening in a frame (the settings' side) : a step darker, the frame's shadow falling in
void chrome_floor(Kit& K, float x, float y, float w, float h) {
    const u32 dev = K.dev;
    cs(dev);
    q4(dev, x, y, w, h, fa(0xFF0C1015u), fa(0xFF0C1015u), fa(0xFF090C10u), fa(0xFF090C10u));
    grad_quad(dev, x, y, w, K.px(22.0f), MA(0x88, 0x000000u), MA(0x88, 0x000000u), 0, 0);
    grad_quad(dev, x, y, K.px(12.0f), h, MA(0x50, 0x000000u), 0, MA(0x50, 0x000000u), 0);
}

// ======== the SHEET ========
bool kit_sheet_draw(const Frame& f, float sw, float sh) {
    Kit K;
    if (!kit_begin(K, f, sh, 1.0f)) return false;
    const u32 dev = K.dev;
    const float keepFade = g_fade; g_fade = 1.0f;
    auto P = [&](float v) { return K.px(v); };
    // the page : the config's own content surface, opaque enough to read on
    cs(dev); q4(dev, 0.0f, 0.0f, sw, sh, 0xFA0F161Du, 0xFA0F161Du, 0xFA090E13u, 0xFA090E13u);
    static bool tOn = true, secOpen = true; static float sv = 0.70f; static int selI = 0;
    static const char* SEL[3] = { "Dans leur propre box", "Dans la box cible", "Cach\xC3\xA9s" };
    char buf[48];
    text_l(K.fs, dev, P(60), P(52), "Planche de composants", P(TS_TITLE), C_TEXT);
    _snprintf(buf, sizeof(buf), "1 u = %.2f px", K.u); buf[47] = 0;
    text_l(K.fr, dev, P(60), P(90), "Le kit de la nouvelle config, dans la mati\xC3\xA8re de l'actuelle. La premi\xC3\xA8re ligne de chaque partie r\xC3\xA9pond \xC3\xA0 la souris.", P(TS_NOTE), C_DIM);
    text_l(K.fr, dev, snap(sw - P(200) - K.fr->measure(buf, P(TS_NOTE))), P(52), buf, P(TS_NOTE), C_DIM);
    const bool close = kit_button(K, CTRL_ID, sw - P(60) - P(120), P(31), P(120), "Fermer", 1, 0);
    auto cap = [&](float x, float y, const char* s) { text_l(K.fs, dev, P(x), P(y), s, P(17), C_DIM); };
    const float RW = 540.0f, RH = 50.0f;
    // ---- column A : toggles, sliders ----
    float x = 60, y = 134;
    cap(x, y, "Interrupteur"); y += 24;
    struct TR { const char* lab; bool on; int st; const char* reason; };
    const TR trs[6] = { { "Activ\xC3\xA9 (essaie-le)", tOn, 0, nullptr }, { "D\xC3\xA9sactiv\xC3\xA9", false, 0, nullptr }, { "Survol\xC3\xA9", true, KS_HOVER, nullptr },
                        { "Indisponible", false, KS_DISABLED, "inactif : debuffs coup\xC3\xA9s" }, { "Focus (clavier / manette)", true, KS_FOCUS, nullptr }, { "Modifi\xC3\xA9", true, KS_MODIFIED, nullptr } };
    for (int i = 0; i < 6; ++i) {
        const float hh = trs[i].reason ? 60.0f : RH, ry = P(y), rh = P(hh);
        kit_row(K, P(x), ry, P(RW), rh, trs[i].st);
        kit_label(K, P(x + 18), ry + rh * 0.5f, trs[i].lab, trs[i].st, trs[i].reason);
        if (kit_toggle(K, ctrl_uid_i(CTRL_ID, i), P(x + RW - 18 - 50), ry + (rh - P(28)) * 0.5f, trs[i].on, trs[i].st) && i == 0) tOn = !tOn;
        y += hh + 4;
    }
    y += 22; cap(x, y, "Curseur"); y += 24;
    struct SR { const char* lab; float v; int st; };
    const SR srs[4] = { { "Taille (essaie-le)", sv, 0 }, { "Gliss\xC3\xA9", 0.45f, KS_DRAG }, { "Indisponible", 0.30f, KS_DISABLED }, { "Focus", 0.80f, KS_FOCUS } };
    for (int i = 0; i < 4; ++i) {
        const float ry = P(y), rh = P(RH);
        float v = srs[i].v; _snprintf(buf, sizeof(buf), "%d %%", (int)(v * 100.0f + 0.5f)); buf[47] = 0;
        kit_row(K, P(x), ry, P(RW), rh, srs[i].st & ~KS_DRAG);
        kit_label(K, P(x + 18), ry + rh * 0.5f, srs[i].lab, srs[i].st);
        if (kit_slider(K, ctrl_uid_i(CTRL_ID, i), P(x + 230), ry + P(11), P(RW - 230 - 18), &v, buf, srs[i].st) && i == 0) sv = v;
        y += RH + 4;
    }
    // ---- column B : selectors, sections, buttons, colours ----
    x = 690; y = 134;
    cap(x, y, "S\xC3\xA9lecteur"); y += 24;
    struct XR { const char* lab; int st; const char* reason; };
    const XR xrs[3] = { { "Debuffs affich\xC3\xA9s", 0, nullptr }, { "Survol\xC3\xA9", KS_HOVER, nullptr }, { "Indisponible", KS_DISABLED, "inactif : debuffs coup\xC3\xA9s" } };
    for (int i = 0; i < 3; ++i) {
        const float hh = xrs[i].reason ? 60.0f : RH, rh = P(hh), ry = P(y);
        kit_row(K, P(x), ry, P(RW), rh, xrs[i].st & KS_HOVER);
        kit_label(K, P(x + 18), ry + rh * 0.5f, xrs[i].lab, xrs[i].st, xrs[i].reason);
        const int d = kit_selector(K, ctrl_uid_i(CTRL_ID, i), P(x + RW - 18 - 280), ry + (rh - P(38)) * 0.5f, P(280), SEL[i == 0 ? selI : 0], xrs[i].st);
        if (i == 0 && d) selI = (selI + d + 3) % 3;
        y += hh + 4;
    }
    y += 22; cap(x, y, "Titre de section"); y += 24;
    if (kit_section(K, CTRL_ID, P(x), P(y), P(RW), "Ce qu'elle affiche (essaie-le)", secOpen, 3, 0)) secOpen = !secOpen;
    y += 48 + 10;
    kit_section(K, CTRL_ID, P(x), P(y), P(RW), "Barres", false, 0, 0);
    y += 48 + 30; cap(x, y, "Bouton"); y += 24;
    kit_button(K, CTRL_ID, P(x), P(y), P(170), "Sauvegarder", 0, 0);
    kit_button(K, CTRL_ID, P(x + 184), P(y), P(140), "Annuler", 1, 0);
    kit_button(K, CTRL_ID, P(x + 338), P(y), P(150), "Supprimer", 2, 0);
    y += 56;
    kit_button(K, CTRL_ID, P(x), P(y), P(170), "Survol\xC3\xA9", 0, KS_HOVER);
    kit_button(K, CTRL_ID, P(x + 184), P(y), P(140), "Survol\xC3\xA9", 1, KS_HOVER);
    kit_button(K, CTRL_ID, P(x + 338), P(y), P(150), "Indisponible", 0, KS_DISABLED);
    y += 56 + 26; cap(x, y, "Couleur"); y += 26;
    const u32 cols[6] = { 0xFFD2A64Eu, 0xFF5CC08Au, 0xFF5B8FD9u, 0xFFD9675Au, 0xFFB98AE0u, 0xFFEEF1F5u };
    for (int i = 0; i < 6; ++i) kit_swatch(K, ctrl_uid_i(CTRL_ID, i), P(x + i * 48), P(y), cols[i], i == 0 ? KS_FOCUS : 0);
    // ---- column C : sidebar, tabs, search ----
    x = 1320; y = 134;
    cap(x, y, "Barre de gauche"); y += 28;
    kit_nav_group(K, P(x + 16), P(y), "Groupe"); y += 18;
    struct NR { const char* s; bool act, on; int st; };
    const NR nrs[4] = { { "Groupe / Alliance", false, true, 0 }, { "Cible", true, true, 0 }, { "Joueur", false, true, KS_HOVER }, { "Pet", false, false, 0 } };
    for (int i = 0; i < 4; ++i) { kit_nav(K, ctrl_uid_i(CTRL_ID, i), P(x), P(y), P(260), nrs[i].s, nrs[i].act, nrs[i].on, nrs[i].st); y += 42; }
    y += 22; cap(x, y, "Onglets"); y += 22;
    { float tx = P(x); const char* T[3] = { "Configuration", "Profil", "Aide" };
      for (int i = 0; i < 3; ++i) { Font* fo = i == 0 ? K.fs : K.fr; kit_tab(K, ctrl_uid_i(CTRL_ID, i), tx, P(y), T[i], i == 0, i == 2 ? KS_HOVER : 0); tx += fo->measure(T[i], P(TS_NAV)) + P(40) + P(8); } }
    y += 70; cap(x, y, "Recherche"); y += 24;
    kit_search(K, P(x), P(y), P(RW), nullptr, 0); y += 54;
    kit_search(K, P(x), P(y), P(RW), "taille", KS_FOCUS); y += 54;
    kit_result(K, P(x), P(y), P(RW), "Taille", "Cible \xE2\x80\xBA Affichage", "70 %", KS_FOCUS); y += 64;
    kit_result(K, P(x), P(y), P(RW), "Taille de la carte", "Minicarte \xE2\x80\xBA Affichage", "100 %", 0); y += 64;
    kit_result(K, P(x), P(y), P(RW), "Taille des cardinaux", "Minicarte \xE2\x80\xBA Forme et cadre \xC2\xB7 Avanc\xC3\xA9", "70 %", 0);
    g_fade = keepFade;
    return close;
}

} // namespace aio
