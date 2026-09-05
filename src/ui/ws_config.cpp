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
    const float aF6_ = cat_fold(CTRL_ID, catOpen_[6]);   // the section FOLDS, like every other module
    cat_panel(dev, hdrX, ry, hdrW, cat_card_h(catH_[6], aF6_));   // ... and it IS a card, collapsed or not
    if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Display", "Affichage"), catOpen_[6], aF6_)) catOpen_[6] = !catOpen_[6];
    ROW_NEXT(42.0f)
    if (aF6_ > 0.0f) {
        const float top6_ = ry;
        cat_fold_clip(dev, hdrX, top6_, hdrW, catH_[6] * aF6_);
        ROW_TOGGLE(CTRL_ID, tr("Show", "Afficher"), c.wsShow)   // Show on weaponskill
        { ROW_BAND(46.0f)   // Size
            const float lo = 0.50f, hi = 2.50f; char b[16]; sprintf(b, "%d%%", (int)(c.wsScale * 100.0f + 0.5f));
            float v01 = (c.wsScale - lo) / (hi - lo); v01 = clampf(v01, 0.0f, 1.0f);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Size", "Taille"), b, &v01)) { float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f; c.wsScale = v < lo ? lo : (v > hi ? hi : v); }
        } ROW_NEXT(46.0f)
        { ROW_BAND(52.0f)   // Font
            int fc = c.wsFont; if (fc < 0 || fc >= ui_font_count()) fc = 0;
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Font", "Police"), ui_font_label(fc))) { c.wsFont = wrap(fc + d, ui_font_count()); save_ui_config(); }
        } ROW_NEXT(52.0f)
        ROW_TOGGLE(CTRL_ID, tr("Impact effects", "Effets d'impact"), c.wsFx)   // Impact effects
        cat_fold_end(dev, ry, top6_, catH_[6], aF6_);
    }   // end Display
    ry += snap(16.0f);                                 // air between this section and the next title bar

    // ===== sub-section : COLOURS =====
    const float aF7_ = cat_fold(CTRL_ID, catOpen_[7]);   // the section FOLDS, like every other module
    cat_panel(dev, hdrX, ry, hdrW, cat_card_h(catH_[7], aF7_));   // ... and it IS a card, collapsed or not
    if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Colours", "Couleurs"), catOpen_[7], aF7_)) catOpen_[7] = !catOpen_[7];
    ROW_NEXT(42.0f)
    if (aF7_ > 0.0f) {
        const float top7_ = ry;
        cat_fold_clip(dev, hdrX, top7_, hdrW, catH_[7] * aF7_);
        struct ColRow { const char* en; const char* fr; unsigned* col; };
        ColRow cols[3] = {
            { "Name",     "Nom",       &c.wsNameCol },
            { "Damage A", "D\xC3\xA9g\xC3\xA2ts A", &c.wsDmgCol1 },   // the number flashes between A and B
            { "Damage B", "D\xC3\xA9g\xC3\xA2ts B", &c.wsDmgCol2 },
        };
        for (int i = 0; i < 3; ++i) {
            unsigned& F = *cols[i].col;
            // Just the NAME of the colour being edited. The preview square that used to sit at the right end
            // of this row said exactly what the picker under it already says, larger and with its hex on it --
            // and this was the only row in the program still drawing one. The band shrinks with it.
            { ROW_BAND(34.0f)
                const float rowH = snap(34.0f), ty = ry + yo; fo->begin(dev);
                fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, tr(cols[i].en, cols[i].fr), ts_label(), fa(C_TEXT), fa(C_STROKE), 1.0f);
            } ROW_NEXT(34.0f)
            CFG_COLOR_PICKER_I(&F, i)   // loop variant : distinct drag/hover uids per colour row
        }
        cat_fold_end(dev, ry, top7_, catH_[7], aF7_);
    }   // end Colours

    ry += snap(16.0f);
    #undef ROW_BAND
    #undef ROW_NEXT
}

} // namespace aio
