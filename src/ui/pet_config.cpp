// pet_config.cpp -- the Pet module's settings page (//aio config -> "Pet") : what is common to the box (show, size,
// grow direction, frame), the job tab (BST, GEO ; SMN and PUP later), then that job's rows (bst_config.cpp,
// geo_config.cpp). The tab also picks the sample the preview shows (UiConfig::petTab, hud_pet.cpp).
//
// Each control is keyed by CTRL_ID (file:line hash, see config_controls.h) -- no hand-numbered uids.
// Reuses the shared sub-section catOpen_ slots [6] (Display) / [5] (Text), like the other modules.
// SPEC docs/design/pet-module-spec.md.
#include "ui/config_page.h"
#include "ui/config_controls.h"
#include "ui/config_rows.h"
#include "model/ui_config.h"
#include "gfx/font.h"
#include "gfx/draw.h"
#include <cstdio>

namespace aio {

void ConfigPage::draw_pet_config(u32 dev, Font* fo, const MouseState* mo, bool click,
                                 float& ry, int& ri, float e,
                                 float bandX, float bandW, float coX, float ctrlW,
                                 float hdrX, float hdrW) {
    UiConfig& c = ui_config();
    const int tab = (c.petTab == 1) ? 1 : 0;

    // ===== sub-section : DISPLAY =====
    const float aF6_ = cat_fold(CTRL_ID, catOpen_[6]);
    cat_panel(dev, hdrX, ry, hdrW, cat_card_h(catH_[6], aF6_));
    if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Display", "Affichage"), catOpen_[6], aF6_)) catOpen_[6] = !catOpen_[6];
    ROW_NEXT(42.0f)
    if (aF6_ > 0.0f) {
        const float top6_ = ry;
        cat_fold_clip(dev, hdrX, top6_, hdrW, catH_[6] * aF6_);
        ROW_TOGGLE(CTRL_ID, tr("Show", "Afficher"), c.petShow)
        { ROW_BAND(46.0f)   // Size : the box's, whatever job fills it
            const float lo = 0.50f, hi = 2.00f; char b[16]; sprintf(b, "%d%%", (int)(c.petScale * 100.0f + 0.5f));
            float v01 = (c.petScale - lo) / (hi - lo); v01 = clampf(v01, 0.0f, 1.0f);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Size", "Taille"), b, &v01)) { float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f; c.petScale = v < lo ? lo : (v > hi ? hi : v); }
        } ROW_NEXT(46.0f)
        ROW_GROW(CTRL_ID, UiConfig::GB_PET)   // which way the box grows as its content widens (ui/box_grow.h)
        draw_box_appearance(dev, fo, mo, click, ry, ri, e, bandX, bandW, coX, ctrlW, c.petBox);   // Box / Transparency / Theme / Hue / Luminosity
        { ROW_BAND(52.0f)   // the job tab : the rows below and the preview are that job's
            const char* JL[2] = { "BST", "GEO" };
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Job", "Job"), JL[tab])) { c.petTab = wrap(tab + d, 2); save_ui_config(); }
        } ROW_NEXT(52.0f)
        { ROW_BAND(40.0f)   // note : the jobs to come
            const float ty = ry + yo; fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + snap(16.0f), tr("SMN and PUP : later.", "SMN et PUP : plus tard."), snap(12.0f), fa(C_MUTE), fa(C_STROKE), 1.0f);
        } ROW_NEXT(40.0f)
        if (tab == 1) draw_geo_rows(dev, fo, mo, click, ry, ri, e, bandX, bandW, coX, ctrlW);
        else          draw_bst_rows(dev, fo, mo, click, ry, ri, e, bandX, bandW, coX, ctrlW);
        cat_fold_end(dev, ry, top6_, catH_[6], aF6_);
    }   // end Display
    ry += snap(16.0f);

    // ===== sub-section : TEXT (the tab's text elements) =====
    const float aF5_ = cat_fold(CTRL_ID, catOpen_[5]);
    cat_panel(dev, hdrX, ry, hdrW, cat_card_h(catH_[5], aF5_));
    if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Text", "Texte"), catOpen_[5], aF5_)) catOpen_[5] = !catOpen_[5];
    ROW_NEXT(42.0f)
    if (aF5_ > 0.0f) {
        const float top5_ = ry;
        cat_fold_clip(dev, hdrX, top5_, hdrW, catH_[5] * aF5_);
        if (tab == 1) draw_geo_text_rows(dev, fo, mo, click, ry, ri, e, bandX, bandW, coX, ctrlW);
        else          draw_bst_text_rows(dev, fo, mo, click, ry, ri, e, bandX, bandW, coX, ctrlW);
        cat_fold_end(dev, ry, top5_, catH_[5], aF5_);
    }   // end Text

    ry += snap(16.0f);
}

#undef ROW_BAND
#undef ROW_NEXT

} // namespace aio
