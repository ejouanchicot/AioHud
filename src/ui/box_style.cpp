// box_style.cpp -- the shared themed-box DRAW helper + the shared "box appearance" CONFIG rows (see box_style.h).
// One implementation reused by every module so their box chrome matches the Party/Target master.
#include "ui/box_style.h"
#include "ui/config_page.h"
#include "ui/config_controls.h"
#include "ui/config_rows.h"
#include "model/ui_config.h"
#include "gfx/window.h"
#include "gfx/draw.h"
#include "gfx/font.h"
#include <cstdio>

namespace aio {

static inline u32 mula(u32 c, float a) {                       // scale a colour's alpha by `a`
    if (a < 0.0f) a = 0.0f; if (a > 1.0f) a = 1.0f;
    return (c & 0x00FFFFFFu) | ((u32)(((c >> 24) & 0xFFu) * a) << 24);
}

void draw_themed_box(u32 dev, const WindowSkin* partySkin, float x, float y, float w, float h,
                     const BoxStyle& bs, float base, float S) {
    if (!bs.on) return;
    const UiConfig& c = ui_config();
    const bool cp = bs.themeCopy != 0;
    const int   theme = cp ? c.skinTheme : bs.theme;
    const float lum   = cp ? c.skinLum   : bs.lum;
    const unsigned hue = cp ? c.skinHue  : bs.hue;
    float a = base * bs.alpha; if (a < 0.0f) a = 0.0f; if (a > 1.0f) a = 1.0f;
    const u32 tint = mula(0xFFFFFFFFu, a);
    const bool border = bs.border != 0;                          // Border off -> background only (draw_window's drawBorder=false)
    if (window_theme_is_proc(theme)) {
        draw_proc_window(dev, theme, x, y, w, h, tint, false, border, lum, hue);
    } else if (partySkin && partySkin->ready()) {                // FFXI family : reuse the shared skin texture
        draw_window(dev, *partySkin, x, y, w, h, tint, S, false, border);
    } else {
        const float R = 6.0f * S;                                // last-resort flat panel (skin not ready)
        rrect_bordered(dev, x, y, w, h, R, mula(0xFF232E54u, a), mula(0xFF080B1Au, a), mula(border ? 0x6699BBFFu : 0x00000000u, a), 1.0f);
    }
}

// ---- shared config rows (Box on/off, Transparency, Theme Same-as-Party/own family+hue+grid+luminosity) ----
// Each control is keyed by CTRL_ID (file:line hash, see config_controls.h) -- no hand-numbered uids.
void ConfigPage::draw_box_appearance(u32 dev, Font* fo, const MouseState* mo, bool click,
                                     float& ry, int& ri, float e,
                                     float bandX, float bandW, float coX, float ctrlW, BoxStyle& bs) {
    { ROW_BAND(52.0f)   // Box on/off
        const float rowH = snap(40.0f), ty = ry + yo; fo->begin(dev);
        fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, tr("Box", "Cadre"), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
        const float bbw = snap(150.0f), bbh = snap(34.0f), bx2 = coX + ctrlW - bbw, bty = ty + (rowH - bbh) * 0.5f;
        if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx2, bty, bbw, bbh, bs.on ? tr("On", "Oui") : tr("None", "Aucun"), bs.on != 0)) { bs.on = !bs.on; save_ui_config(); }
    }
    ROW_NEXT(52.0f)
    if (bs.on) {
    { ROW_BAND(52.0f)   // Border on/off (frame edges + corners ; off = background only)
        const float rowH = snap(40.0f), ty = ry + yo; fo->begin(dev);
        fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, tr("Border", "Bordure"), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
        const float bbw = snap(150.0f), bbh = snap(34.0f), bx2 = coX + ctrlW - bbw, bty = ty + (rowH - bbh) * 0.5f;
        if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx2, bty, bbw, bbh, bs.border ? tr("On", "Oui") : tr("None", "Aucun"), bs.border != 0)) { bs.border = !bs.border; save_ui_config(); }
    }
    ROW_NEXT(52.0f)
    { ROW_BAND(46.0f)   // Transparency (content stays opaque)
        const float transp = 1.0f - bs.alpha; char b[16]; sprintf(b, "%d%%", (int)(transp * 100.0f + 0.5f));
        float v01 = clampf(transp, 0.0f, 1.0f);   // full 0..100% range (100% = fully invisible box)
        if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Transparency", "Transparence"), b, &v01)) { bs.alpha = 1.0f - v01; }
    }
    ROW_NEXT(46.0f)
    { ROW_BAND(52.0f)   // Theme : Same as Party / Custom
        const float rowH = snap(40.0f), ty = ry + yo; fo->begin(dev);
        fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, tr("Theme", "Thème"), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
        const float bbw = snap(150.0f), bbh = snap(34.0f), bx2 = coX + ctrlW - bbw, bty = ty + (rowH - bbh) * 0.5f;
        if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx2, bty, bbw, bbh, bs.themeCopy ? tr("Same as Party", "Comme Party") : tr("Custom", "Perso"), bs.themeCopy != 0)) { bs.themeCopy = !bs.themeCopy; save_ui_config(); }
    }
    ROW_NEXT(52.0f)
    if (!bs.themeCopy) {
        { ROW_BAND(52.0f)   // Box Theme family
            const int fam = window_theme_family(bs.theme), var = window_theme_variant(bs.theme);
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Box Theme", "Thème de cadre"), box_family_name(fam))) { bs.theme = window_theme_index(wrap(fam + d, box_family_count()), var); save_ui_config(); }
        }
        ROW_NEXT(52.0f)
        if (window_theme_family(bs.theme) != 0) { ROW_BAND(48.0f)   // procedural : custom-hue toggle
            const float rowH = snap(38.0f), ty = ry + yo; fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, tr("Custom colour", "Couleur perso"), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
            const float bbw = snap(112.0f), bbh = snap(34.0f), bx2 = coX + ctrlW - bbw, bty = ty + (rowH - bbh) * 0.5f;
            const bool on = bs.hue != 0;
            if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx2, bty, bbw, bbh, on ? tr("On", "Oui") : tr("Off", "Non"), on)) { bs.hue = on ? 0u : (box_hue_color(window_theme_variant(bs.theme)) | 0xFF000000u); save_ui_config(); }
            ROW_NEXT(48.0f)
        }
        if (window_theme_family(bs.theme) != 0 && bs.hue != 0) {
            CFG_COLOR_PICKER(&bs.hue)
        } else {   // variant grid : FFXI theme chips / procedural hue swatches
            const int fam = window_theme_family(bs.theme), var = window_theme_variant(bs.theme);
            const bool isFFXI = (fam == 0);
            const int nVar = isFFXI ? window_tex_theme_count() : box_hue_count();
            const int COLS = isFFXI ? (nVar < 1 ? 1 : nVar) : 15;
            const int nrows = (nVar + COLS - 1) / COLS;
            const float cw = isFFXI ? snap(42.0f) : snap(22.0f), ch = isFFXI ? snap(26.0f) : snap(22.0f), cg = snap(7.0f);
            const float gridH = nrows * ch + (nrows - 1) * cg, slotH = gridH + snap(20.0f);
            ROW_BAND(slotH) (void)yo;
            fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ry + slotH * 0.5f, isFFXI ? tr("Theme", "Thème") : tr("Colour", "Couleur"), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
            const float gridW = COLS * cw + (COLS - 1) * cg;
            const float gx = coX + ctrlW - gridW, gy = ry + (slotH - gridH) * 0.5f;
            for (int k = 0; k < nVar; ++k) {
                const float xk = gx + (k % COLS) * (cw + cg), yk = gy + (k / COLS) * (ch + cg);
                const bool sel = (var == k);
                if (isFFXI) {
                    rpanel(dev, xk, yk, cw, ch, snap(6.0f), sel ? C_ROWON_T : 0x66121A18, sel ? C_ROWON_B : 0x66090D0F, sel ? C_ACCENT : C_BORDER, snap(1.2f));
                    fo->begin(dev); fo->draw_c(dev, xk + cw * 0.5f, yk + ch * 0.5f, window_theme_name(k), snap(13.0f), fa(sel ? C_ACCENTHI : C_TEXT), fa(C_STROKE), 1.0f);
                } else {
                    const u32 col = box_hue_color(k);
                    if (sel) { cs_add(dev); rrect_glow(dev, xk, yk, cw, ch, snap(6.0f), (col & 0x00FFFFFF) | 0x80000000, snap(6.0f)); cs(dev); }
                    rrect_fill(dev, xk, yk, cw, ch, snap(6.0f), col, shade(col, -0.28f));
                    outline(dev, xk, yk, cw, ch, sel ? 0xFFFFFFFF : C_BORDER);
                }
                if (inrect(mo, xk, yk, cw, ch) && click) { bs.theme = window_theme_index(fam, k); save_ui_config(); }
            }
            ROW_NEXT(slotH)
        }
        if (window_theme_family(bs.theme) != 0) { ROW_BAND(46.0f)   // Luminosity (procedural only)
            float v01 = (bs.lum + 1.0f) * 0.5f; v01 = clampf(v01, 0.0f, 1.0f);
            const int pct = (int)(bs.lum * 100.0f + (bs.lum >= 0.0f ? 0.5f : -0.5f));
            char b[16]; sprintf(b, "%+d%%", pct);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Luminosity", "Luminosité"), b, &v01)) { bs.lum = v01 * 2.0f - 1.0f; }
            ROW_NEXT(46.0f)
        }
    }   // end own-theme
    }   // end box on
}

// shared per-element "Text" style rows -- see config_page.h. The caller draws the element SELECTOR and passes the
// chosen TextStyle ; these are the identical Font/Size/Outline/Style/Colour/Alpha controls every module's Text
// sub-section used to copy-paste. CTRL_ID is collision-free (one call per panel, panels mutually exclusive), like
// draw_box_appearance above. (ROW_BAND/ROW_NEXT stay defined from config_rows.h through both functions -> #undef'd
// only after this one.)
void ConfigPage::draw_text_style(u32 dev, Font* fo, const MouseState* mo, bool click,
                                 float& ry, int& ri, float e,
                                 float bandX, float bandW, float coX, float ctrlW, TextStyle& ts, bool swatch) {
    { ROW_BAND(52.0f)   // Font
        int fc = ts.face; if (fc < 0 || fc >= ui_font_count()) fc = 0;
        if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Font", "Police"), ui_font_label(fc))) { ts.face = wrap(fc + d, ui_font_count()); save_ui_config(); }
    }
    ROW_NEXT(52.0f)
    { ROW_BAND(46.0f)   // Size
        const float lo = 0.50f, hi = 2.00f; char b[16]; sprintf(b, "%d%%", (int)(ts.size * 100.0f + 0.5f));
        float v01 = (ts.size - lo) / (hi - lo); v01 = clampf(v01, 0.0f, 1.0f);
        if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Size", "Taille"), b, &v01)) { float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f; ts.size = v < lo ? lo : (v > hi ? hi : v); }
    }
    ROW_NEXT(46.0f)
    { ROW_BAND(46.0f)   // Outline
        const float lo = 0.00f, hi = 2.00f; char b[16]; sprintf(b, "%d%%", (int)(ts.outline * 100.0f + 0.5f));
        float v01 = (ts.outline - lo) / (hi - lo); v01 = clampf(v01, 0.0f, 1.0f);
        if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Outline", "Contour"), b, &v01)) { float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f; ts.outline = v < lo ? lo : (v > hi ? hi : v); }
    }
    ROW_NEXT(46.0f)
    { ROW_BAND(52.0f)   // Bold / Italic / CAPS
        const float rowH = snap(40.0f), ty = ry + yo; fo->begin(dev);
        fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, tr("Style", "Style"), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
        const float bbw = snap(80.0f), bgap = snap(8.0f), bbh = snap(34.0f), bty = ty + (rowH - bbh) * 0.5f, bx0 = coX + ctrlW - (3 * bbw + 2 * bgap);
        if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx0, bty, bbw, bbh, tr("Bold", "Gras"), ts.bold)) { ts.bold = !ts.bold; save_ui_config(); }
        if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx0 + bbw + bgap, bty, bbw, bbh, tr("Italic", "Ital."), ts.italic)) { ts.italic = !ts.italic; save_ui_config(); }
        if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx0 + 2 * (bbw + bgap), bty, bbw, bbh, tr("CAPS", "MAJ"), ts.upper)) { ts.upper = !ts.upper; save_ui_config(); }
    }
    ROW_NEXT(52.0f)
    { ROW_BAND(52.0f)   // Colour : Default / Custom
        const float rowH = snap(40.0f), ty = ry + yo; fo->begin(dev);
        fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, tr("Colour", "Couleur"), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
        const float bbh = snap(34.0f), bty = ty + (rowH - bbh) * 0.5f, tgw = snap(96.0f), onx = coX + ctrlW - tgw;
        if (toggle_chip(dev, fo, mo, click, CTRL_ID, onx, bty, tgw, bbh, ts.colorOn ? tr("Custom", "Perso") : tr("Default", "D\xC3\xA9""faut"), ts.colorOn)) {
            ts.colorOn = !ts.colorOn; if (ts.colorOn && (ts.color >> 24) == 0) ts.color |= 0xFF000000u; save_ui_config(); }
        if (swatch && ts.colorOn) {   // live colour preview (Target / Player / Minimap panels) : checkerboard + colour
            const float pw = snap(58.0f), pxs = onx - snap(12.0f) - pw;
            flat(dev, pxs, bty, pw * 0.5f, bbh, 0xFFFFFFFF); flat(dev, pxs + pw * 0.5f, bty, pw * 0.5f, bbh, 0xFF262A31);
            flat(dev, pxs, bty, pw, bbh, ts.color); outline(dev, pxs, bty, pw, bbh, C_BORDER);
        }
    }
    ROW_NEXT(52.0f)
    if (ts.colorOn) {   // HSV picker + Alpha
        CFG_COLOR_PICKER(&ts.color)
        { ROW_BAND(40.0f)
            int a = (int)((ts.color >> 24) & 0xFFu); char vb[8]; sprintf(vb, "%d", a); float v01 = a / 255.0f;
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, "A", vb, &v01)) {
                int nv = (int)(v01 * 255.0f + 0.5f); if (nv < 0) nv = 0; if (nv > 255) nv = 255;
                ts.color = (ts.color & 0x00FFFFFFu) | ((u32)nv << 24); }
        }
        ROW_NEXT(40.0f)
    }
    #undef ROW_BAND
    #undef ROW_NEXT
}

} // namespace aio
