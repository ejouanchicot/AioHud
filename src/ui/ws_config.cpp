// ws_config.cpp -- the Arcade WS ("ULTRA COMBO") popup module's settings panel (//aio config -> "Arcade WS").
//
// Each control is keyed by CTRL_ID (file:line hash, see config_controls.h) -- no hand-numbered uids. Reuses the
// shared sub-section catOpen_ slots [6]/[7]. Defined as a ConfigPage method like the other *_config.cpp panels.
#include "ui/config_page.h"
#include "ui/config_controls.h"    // cat_header / row_slider / row_selector / toggle_chip + palette
#include "ui/config_rows.h"        // ROW_BAND / ROW_NEXT
#include "model/ui_config.h"
#include "gfx/font.h"
#include "gfx/draw.h"
#include <cstdio>

namespace aio {

void ConfigPage::draw_ws_config(u32 dev, Font* fo, const MouseState* mo, bool click,
                                float& ry, int& ri, float e,
                                float bandX, float bandW, float coX, float ctrlW,
                                float hdrX, float hdrW) {
    UiConfig& c = ui_config();

    // ===== sub-section : DISPLAY =====
    if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Display", "Affichage"), catOpen_[6])) catOpen_[6] = !catOpen_[6];
    ROW_NEXT(42.0f)
    if (catOpen_[6]) {
        { ROW_BAND(48.0f)   // Show on weaponskill
            const float rowH = snap(38.0f), ty = ry + yo; fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, tr("Show", "Afficher"), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
            const float bbw = snap(112.0f), bbh = snap(34.0f), bx2 = coX + ctrlW - bbw, bty = ty + (rowH - bbh) * 0.5f;
            if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx2, bty, bbw, bbh, c.wsShow ? tr("On", "Oui") : tr("Off", "Non"), c.wsShow != 0)) { c.wsShow = !c.wsShow; save_ui_config(); }
        } ROW_NEXT(48.0f)
        { ROW_BAND(46.0f)   // Size
            const float lo = 0.50f, hi = 2.50f; char b[16]; sprintf(b, "%d%%", (int)(c.wsScale * 100.0f + 0.5f));
            float v01 = (c.wsScale - lo) / (hi - lo); v01 = clampf(v01, 0.0f, 1.0f);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Size", "Taille"), b, &v01)) { float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f; c.wsScale = v < lo ? lo : (v > hi ? hi : v); }
        } ROW_NEXT(46.0f)
        { ROW_BAND(52.0f)   // Font
            int fc = c.wsFont; if (fc < 0 || fc >= ui_font_count()) fc = 0;
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Font", "Police"), ui_font_label(fc))) { c.wsFont = wrap(fc + d, ui_font_count()); save_ui_config(); }
        } ROW_NEXT(52.0f)
        { ROW_BAND(48.0f)   // Impact effects
            const float rowH = snap(38.0f), ty = ry + yo; fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, tr("Impact effects", "Effets d'impact"), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
            const float bbw = snap(112.0f), bbh = snap(34.0f), bx2 = coX + ctrlW - bbw, bty = ty + (rowH - bbh) * 0.5f;
            if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx2, bty, bbw, bbh, c.wsFx ? tr("On", "Oui") : tr("Off", "Non"), c.wsFx != 0)) { c.wsFx = !c.wsFx; save_ui_config(); }
        } ROW_NEXT(48.0f)
    }   // end Display

    // ===== sub-section : COLOURS =====
    if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Colours", "Couleurs"), catOpen_[7])) catOpen_[7] = !catOpen_[7];
    ROW_NEXT(42.0f)
    if (catOpen_[7]) {
        struct ColRow { const char* en; const char* fr; unsigned* col; };
        ColRow cols[3] = {
            { "Name",     "Nom",       &c.wsNameCol },
            { "Damage A", "D\xC3\xA9g\xC3\xA2ts A", &c.wsDmgCol1 },   // the number flashes between A and B
            { "Damage B", "D\xC3\xA9g\xC3\xA2ts B", &c.wsDmgCol2 },
        };
        for (int i = 0; i < 3; ++i) {
            unsigned& F = *cols[i].col;
            { ROW_BAND(52.0f)   // label + swatch
                const float rowH = snap(40.0f), ty = ry + yo; fo->begin(dev);
                fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, tr(cols[i].en, cols[i].fr), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
                const float bbh = snap(34.0f), bty = ty + (rowH - bbh) * 0.5f, pw = snap(58.0f), pxs = coX + ctrlW - pw;
                flat(dev, pxs, bty, pw, bbh, F | 0xFF000000u); outline(dev, pxs, bty, pw, bbh, C_BORDER);
            } ROW_NEXT(52.0f)
            CFG_COLOR_PICKER_I(&F, i)   // loop variant : distinct drag/hover uids per colour row (plain CFG_COLOR_PICKER = IDX 0 -> the 3 pickers collided)
        }
    }   // end Colours

    ry += snap(10.0f);
    #undef ROW_BAND
    #undef ROW_NEXT
}

} // namespace aio
