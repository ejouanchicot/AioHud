// aw_config.cpp -- the Absorb-TP module's settings panel (//aio config -> "Absorb-TP").
//
// Each control is keyed by CTRL_ID (file:line hash, see config_controls.h) -- no hand-numbered uids. Reuses the shared
// sub-section catOpen_ slot [6] (Display) like the other module panels (only one renders at a time). The box is
// placed in //aio edit like every other box.
#include "ui/config_page.h"
#include "ui/config_controls.h"    // cat_header / row_slider / toggle_chip + palette
#include "ui/config_rows.h"        // ROW_BAND / ROW_NEXT / ROW_TOGGLE / ROW_GROW
#include "model/ui_config.h"
#include "gfx/font.h"
#include "gfx/draw.h"
#include <cstdio>

namespace aio {

void ConfigPage::draw_aw_config(u32 dev, Font* fo, const MouseState* mo, bool click,
                                float& ry, int& ri, float e,
                                float bandX, float bandW, float coX, float ctrlW,
                                float hdrX, float hdrW) {
    UiConfig& c = ui_config();

    const float aF6_ = cat_fold(CTRL_ID, catOpen_[6]);
    cat_panel(dev, hdrX, ry, hdrW, cat_card_h(catH_[6], aF6_));
    if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Display", "Affichage"), catOpen_[6], aF6_)) catOpen_[6] = !catOpen_[6];
    ROW_NEXT(42.0f)
    if (aF6_ > 0.0f) {
        const float top6_ = ry;
        cat_fold_clip(dev, hdrX, top6_, hdrW, catH_[6] * aF6_);
        ROW_TOGGLE(CTRL_ID, tr("Show", "Afficher"), c.awShow)
        { ROW_BAND(46.0f)   // Size
            const float lo = 0.50f, hi = 2.00f; char b[16]; sprintf(b, "%d%%", (int)(c.awScale * 100.0f + 0.5f));
            float v01 = (c.awScale - lo) / (hi - lo); v01 = clampf(v01, 0.0f, 1.0f);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Size", "Taille"), b, &v01)) { float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f; c.awScale = v < lo ? lo : (v > hi ? hi : v); }
        } ROW_NEXT(46.0f)
        ROW_GROW(CTRL_ID, UiConfig::GB_AW)   // which way the box grows as its content widens (ui/box_grow.h)
        draw_box_appearance(dev, fo, mo, click, ry, ri, e, bandX, bandW, coX, ctrlW, c.awBox);   // Box / Transparency / Theme / Hue / Luminosity
        ROW_TOGGLE(CTRL_ID, tr("Colour the TP", "Couleur des TP"), c.awColors)       // white / green / yellow / orange / red ; blue = nothing drained
        ROW_TOGGLE(CTRL_ID, tr("Time since", "Temps \xC3\xA9""coul\xC3\xA9"), c.awTimer)
        ROW_TOGGLE(CTRL_ID, tr("Absorb count", "Nombre d'Absorb"), c.awCount)
        { ROW_BAND(46.0f)   // how long without a new Absorb-TP before that caster's row turns red
            const float lo = 10.0f, hi = 600.0f; char b[16]; sprintf(b, "%ds", c.awScreen);
            float v01 = ((float)c.awScreen - lo) / (hi - lo); v01 = clampf(v01, 0.0f, 1.0f);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Red alert after", "Alerte rouge apr\xC3\xA8s"), b, &v01)) { int v = (int)(lo + v01 * (hi - lo) + 0.5f); v = (v / 5) * 5; c.awScreen = v < 10 ? 10 : (v > 600 ? 600 : v); }
        } ROW_NEXT(46.0f)
        { ROW_BAND(46.0f)   // how far back the count looks
            const float lo = 60.0f, hi = 3600.0f; char b[16]; sprintf(b, "%d min", c.awMemory / 60);
            float v01 = ((float)c.awMemory - lo) / (hi - lo); v01 = clampf(v01, 0.0f, 1.0f);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Keep & count over", "Garder et compter sur"), b, &v01)) { int v = (int)(lo + v01 * (hi - lo) + 0.5f); v = (v / 60) * 60; c.awMemory = v < 60 ? 60 : (v > 3600 ? 3600 : v); }
        } ROW_NEXT(46.0f)
        { ROW_BAND(40.0f)   // note
            const float ty = ry + yo; fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + snap(16.0f), tr("A row turns red when that player stops casting Absorb-TP.", "Une ligne passe en rouge quand le joueur ne relance plus Absorb-TP."), snap(12.0f), fa(C_MUTE), fa(C_STROKE), 1.0f);
        } ROW_NEXT(40.0f)
        cat_fold_end(dev, ry, top6_, catH_[6], aF6_);
    }   // end Display
    ry += snap(16.0f);
}

} // namespace aio
