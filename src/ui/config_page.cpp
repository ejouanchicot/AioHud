// config_page.cpp -- see config_page.h. Polished, animated, web-style config overlay.
// The AIOHUD window skin fills the WHOLE page (it IS the frame) ; the UI is drawn directly on it.
#include "ui/config_page.h"
#include "gfx/draw.h"      // grad_quad
#include "gfx/font.h"
#include "gfx/window.h"
#include "model/ui_config.h"
#include <cmath>
#include <cstdio>

namespace aio {

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
static const u32 C_ACCENT   = 0xFF5AA2FF, C_STROKE = 0xFF000000, C_CLOSEHOV = 0xFFCE424C;

void ConfigPage::set_tab(int t)     { if (t >= 0 && t < NTABS) tab_ = t; }
void ConfigPage::set_section(int s) { if (s >= 0 && s < NSECT) section_ = s; }

// ---- a global fade factor (open animation), applied to every quad + text colour ----
static float g_fade = 1.0f;
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
static inline bool inrect(const MouseState* m, float x, float y, float w, float h) {
    return m && m->x >= x && m->x < x + w && m->y >= y && m->y < y + h;
}
static void cursor(u32 dev, float x, float y) {
    for (int i = 0; i <= 17; ++i) flat(dev, x - 1.0f, y - 1.0f + (float)i, (float)i * 0.70f + 4.0f, 2.0f, 0xFF0A0D12);
    for (int i = 0; i <= 15; ++i) flat(dev, x + 1.0f, y + 1.0f + (float)i, (float)i * 0.66f + 1.0f, 2.0f, 0xFFFFFFFF);
}

// A labeled selector row :  "Label                 [<]   value   [>]".
// Returns -1 / +1 when an arrow is clicked, else 0. Draws hover states on the arrows.
static int row_selector(u32 dev, Font* fo, const MouseState* mo, bool click,
                        float x, float y, float w, const char* label, const char* value) {
    const float rowH = snap(40.0f);
    flat(dev, x, y + rowH, w, 1, 0x14FFFFFF);                         // subtle separator under the row
    fo->begin(dev);
    fo->draw_lc(dev, x + snap(4.0f), y + rowH * 0.5f, label, snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);

    const float aS = snap(32.0f), valW = snap(168.0f), ctlW = aS + valW + aS;
    const float cx = x + w - ctlW, cy = y + (rowH - aS) * 0.5f;
    int delta = 0;

    const bool lh = inrect(mo, cx, cy, aS, aS);
    vg(dev, cx, cy, aS, aS, lh ? 0x55FFFFFF : 0x26FFFFFF, lh ? 0x33FFFFFF : 0x12FFFFFF);
    outline(dev, cx, cy, aS, aS, C_BORDER);
    fo->begin(dev); fo->draw_c(dev, cx + aS * 0.5f, cy + aS * 0.5f, "<", snap(16.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
    if (lh && click) delta = -1;

    const float vx = cx + aS;
    vg(dev, vx, cy, valW, aS, 0x33101620, 0x33080C12);
    outline(dev, vx, cy, valW, aS, C_BORDER);
    fo->begin(dev); fo->draw_c(dev, vx + valW * 0.5f, cy + aS * 0.5f, value, snap(14.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);

    const float rx = vx + valW;
    const bool rh = inrect(mo, rx, cy, aS, aS);
    vg(dev, rx, cy, aS, aS, rh ? 0x55FFFFFF : 0x26FFFFFF, rh ? 0x33FFFFFF : 0x12FFFFFF);
    outline(dev, rx, cy, aS, aS, C_BORDER);
    fo->begin(dev); fo->draw_c(dev, rx + aS * 0.5f, cy + aS * 0.5f, ">", snap(16.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
    if (rh && click) delta = +1;
    return delta;
}
static int wrap(int v, int n) { if (v < 0) return n - 1; if (v >= n) return 0; return v; }

void ConfigPage::draw(const Frame& f, float sw, float sh) {
    if (!open_) return;
    u32 dev = f.dev;
    Font* fo = f.font;
    if (!fo || !fo->ready() || sw <= 0 || sh <= 0) return;
    const MouseState* mo = f.mouse;
    const bool click = mo && mo->clicked;

    // --- EDIT LAYOUT mode : hide the page (the game + the real boxes show through) and draw only a
    //     floating toolbar. The party/alliance boxes handle their own drag/resize (see party.cpp). ---
    if (ui_config().editLayout) {
        g_fade = 1.0f;
        const float bw = snap(600.0f), bh = snap(46.0f), bx = snap((sw - bw) * 0.5f), by = snap(22.0f);
        vg(dev, bx, by, bw, bh, 0xF0202B3C, 0xF0141C28);
        outline(dev, bx, by, bw, bh, C_BORDERHI);
        fo->begin(dev);
        fo->draw_lc(dev, bx + snap(16.0f), by + bh * 0.5f, "EDIT LAYOUT  \xC2\xB7  drag = move,  wheel = resize", snap(14.0f), C_TEXT, C_STROKE, 1.0f);
        const float bh2 = snap(30.0f), dby = by + (bh - bh2) * 0.5f;
        const float db = snap(80.0f), dbx = bx + bw - db - snap(10.0f);         // Done (far right)
        const float rb = snap(96.0f), rbx = dbx - rb - snap(8.0f);              // Default (left of Done)
        const bool rh = inrect(mo, rbx, dby, rb, bh2);
        vg(dev, rbx, dby, rb, bh2, rh ? 0xFFB85050 : 0xFF3A2A2E, rh ? 0xFF8A3A3A : 0xFF281D20);
        outline(dev, rbx, dby, rb, bh2, C_BORDERHI);
        fo->begin(dev); fo->draw_c(dev, rbx + rb * 0.5f, dby + bh2 * 0.5f, "Default", snap(13.0f), C_TEXT, C_STROKE, 1.0f);
        if (rh && click) reset_boxes();                                        // reset positions + sizes only
        const bool dh = inrect(mo, dbx, dby, db, bh2);
        vg(dev, dbx, dby, db, bh2, dh ? 0xFF3A82E0 : 0xFF2A3548, dh ? 0xFF2A61B6 : 0xFF1D2738);
        outline(dev, dbx, dby, db, bh2, C_BORDERHI);
        fo->begin(dev); fo->draw_c(dev, dbx + db * 0.5f, dby + bh2 * 0.5f, "Done", snap(14.0f), C_TEXT, C_STROKE, 1.0f);
        if (dh && click) { ui_config().editLayout = false; save_ui_config(); } // persist position/size on exit
        if (mo) cursor(dev, mo->x, mo->y);
        return;
    }

    // --- frame clock -> open fade + a tiny scale-in ---
    float dt = (lastT_ < 0.0f) ? 0.016f : (f.t - lastT_);
    if (dt < 0.0f || dt > 0.25f) dt = 0.016f;
    lastT_ = f.t;
    anim_ = clampf(anim_ + dt / 0.16f, 0.0f, 1.0f);
    const float e = 1.0f - (1.0f - anim_) * (1.0f - anim_) * (1.0f - anim_);   // ease-out cubic
    g_fade = e;
    const float pulse = 0.5f + 0.5f * sinf(f.t * 3.0f);                        // 0..1 slow pulse

    // ===== BACK LAYER : dim + the AIOHUD skin filling the WHOLE page (this IS the frame) =====
    flat(dev, 0, 0, sw, sh, C_DIMBG);
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
    fo->draw_lc(dev, ix + tw + snap(14.0f), iy + snap(24.0f), "CONFIGURATION", snap(15.0f), fa(C_ACCENT), fa(C_STROKE), 1.2f);

    // close button (X), top-right
    const float cbS = snap(36.0f), cbX = ix + iw - cbS, cbY = iy + snap(2.0f);
    const bool cbHov = inrect(mo, cbX, cbY, cbS, cbS);
    vg(dev, cbX, cbY, cbS, cbS, cbHov ? C_CLOSEHOV : 0x33FFFFFF, cbHov ? 0xFFA0303A : 0x14FFFFFF);
    outline(dev, cbX, cbY, cbS, cbS, cbHov ? 0xFFE57078 : C_BORDERHI);
    fo->begin(dev);
    fo->draw_c(dev, cbX + cbS * 0.5f, cbY + cbS * 0.5f, "X", snap(18.0f), fa(C_TEXT), fa(C_STROKE), 1.3f);
    if (cbHov && click) { open_ = false; if (mo) cursor(dev, mo->x, mo->y); return; }

    // accent divider under the header (animated wipe-in width)
    const float divY = iy + snap(50.0f);
    flat(dev, ix, divY, iw, 1, C_BORDER);
    flat(dev, ix, divY, iw * e, snap(2.0f), C_ACCENT);

    // ===== TAB STRIP =====
    const float tabY = divY + snap(18.0f), tabH = snap(42.0f), tabW = snap(176.0f), tabGap = snap(6.0f);
    const float bodyY = tabY + tabH;
    float activeX = ix;
    for (int i = 0; i < NTABS; ++i) {
        const float tx = ix + i * (tabW + tabGap);
        const bool active = (i == tab_);
        const bool hover  = inrect(mo, tx, tabY, tabW, tabH);
        if (hover && click) tab_ = i;
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
    flat(dev, tabSlide_, bodyY - snap(3.0f), tabW, snap(3.0f), lerpc(C_ACCENT, 0xFFBFE0FF, pulse));

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
            if (active) { vg(dev, rx, ry, rw, rh, C_ROWON_T, C_ROWON_B); flat(dev, rx, ry, snap(3.0f), rh, C_ACCENT); }
            else if (hover) flat(dev, rx, ry, rw, rh, 0x22FFFFFF);
            fo->begin(dev);
            fo->draw_lc(dev, rx + snap(16.0f), ry + rh * 0.5f, SECTIONS[i], snap(15.0f),
                        (active || hover) ? fa(C_TEXT) : fa(C_DIM), fa(C_STROKE), 1.0f);
        }
        // content : live Party / Alliance controls (edit ui_config -> applied next frame)
        const float coX = ix + sbW + snap(30.0f), coY = bodyY + snap(26.0f);
        const float coW = (ix + iw) - coX - snap(26.0f);
        fo->begin(dev);
        fo->draw_lc(dev, coX, coY, SECTIONS[section_], snap(20.0f), fa(C_TEXT), fa(C_STROKE), 1.4f);
        flat(dev, coX, coY + snap(18.0f), snap(44.0f), snap(2.0f), C_ACCENT);

        float ry = coY + snap(48.0f);
        // Box Theme -> window skin
        if (int d = row_selector(dev, fo, mo, click, coX, ry, coW, "Box Theme", window_theme_name(ui_config().skinTheme))) {
            ui_config().skinTheme = wrap(ui_config().skinTheme + d, window_theme_count()); save_ui_config(); }
        ry += snap(52.0f);
        // Font -> party/alliance text face
        if (int d = row_selector(dev, fo, mo, click, coX, ry, coW, "Font", ui_font_label(ui_config().fontFace))) {
            ui_config().fontFace = wrap(ui_config().fontFace + d, ui_font_count()); save_ui_config(); }
        ry += snap(52.0f);
        // Font Size -> party/alliance scale
        char szbuf[16]; sprintf(szbuf, "%d%%", (int)(ui_config().box[0].scale * 100.0f + 0.5f));
        if (int d = row_selector(dev, fo, mo, click, coX, ry, coW, "Font Size", szbuf)) {
            float v = ui_config().box[0].scale + (float)d * 0.05f;
            v = v < 0.50f ? 0.50f : (v > 2.00f ? 2.00f : v);
            for (int b = 0; b < 3; ++b) ui_config().box[b].scale = v;   // scale all party/alliance boxes together
            save_ui_config();
        }
        ry += snap(52.0f);
        // Buff Size -> fraction of the player row (independent of Font Size ; 40%..100% of the row)
        char bzbuf[16]; sprintf(bzbuf, "%d%%", (int)(ui_config().buffScale * 100.0f + 0.5f));
        if (int d = row_selector(dev, fo, mo, click, coX, ry, coW, "Buff Size", bzbuf)) {
            float v = ui_config().buffScale + (float)d * 0.05f;
            ui_config().buffScale = v < 0.40f ? 0.40f : (v > 1.00f ? 1.00f : v);
            save_ui_config();
        }
        ry += snap(56.0f);
        // Buttons : Edit Layout (enter drag/resize) + Default (reset EVERYTHING)
        {
            const float bh = snap(34.0f), bw = snap(150.0f), gap = snap(10.0f);
            const float defX = coX + coW - bw, edX = defX - gap - bw;
            fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ry + bh * 0.5f, "Layout", snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
            const bool eh = inrect(mo, edX, ry, bw, bh);
            vg(dev, edX, ry, bw, bh, eh ? 0xFF3A82E0 : 0xFF2A3548, eh ? 0xFF2A61B6 : 0xFF1D2738);
            outline(dev, edX, ry, bw, bh, C_BORDERHI);
            fo->begin(dev); fo->draw_c(dev, edX + bw * 0.5f, ry + bh * 0.5f, "Edit Layout", snap(13.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
            if (eh && click) ui_config().editLayout = true;
            const bool rh = inrect(mo, defX, ry, bw, bh);
            vg(dev, defX, ry, bw, bh, rh ? 0xFFB85050 : 0xFF3A2A2E, rh ? 0xFF8A3A3A : 0xFF281D20);
            outline(dev, defX, ry, bw, bh, C_BORDERHI);
            fo->begin(dev); fo->draw_c(dev, defX + bw * 0.5f, ry + bh * 0.5f, "Default (all)", snap(13.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
            if (rh && click) reset_ui_config();   // theme + font + positions + sizes
        }
    } else {
        fo->begin(dev);
        fo->draw_lc(dev, ix + snap(26.0f), bodyY + snap(32.0f),
                    tab_ == 1 ? "Profile  (coming soon)" : "Help  (coming soon)", snap(16.0f), fa(C_DIM), fa(C_STROKE), 1.0f);
    }

    // footer + cursor on top
    fo->begin(dev);
    fo->draw_lc(dev, ix, pageBot + snap(14.0f), "click X  or  //aio config  to close", snap(12.0f), fa(C_MUTE), fa(C_STROKE), 1.0f);
    if (mo) cursor(dev, mo->x, mo->y);
}

} // namespace aio
