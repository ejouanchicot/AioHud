// hl_config.cpp -- the Hate List module's settings panel (//aio config -> "Hate List").
//
// Each control is keyed by CTRL_ID (file:line hash, see config_controls.h) -- no hand-numbered uids.
// Reuses the shared sub-section catOpen_ slots [6] (Display) / [5] (Text), like sc/tp (only one
// module panel renders at a time). The box is placed in //aio edit like every other box.
#include "ui/config_page.h"
#include "ui/config_controls.h"    // cat_header / row_slider / toggle_chip + palette
#include "ui/config_rows.h"        // ROW_BAND / ROW_NEXT
#include "model/ui_config.h"
#include "gfx/font.h"
#include "gfx/draw.h"
#include <cstdio>

namespace aio {

void ConfigPage::draw_hl_config(u32 dev, Font* fo, const MouseState* mo, bool click,
                                float& ry, int& ri, float e,
                                float bandX, float bandW, float coX, float ctrlW,
                                float hdrX, float hdrW) {
    UiConfig& c = ui_config();

    // ===== sub-section : DISPLAY =====
    // The section FOLDS : cat_fold owns the eased progress, the clip and the cursor (config_controls.h).
    const float aF6_ = cat_fold(CTRL_ID, catOpen_[6]);
    if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Display", "Affichage"), catOpen_[6])) catOpen_[6] = !catOpen_[6];
    ROW_NEXT(42.0f)
    if (aF6_ > 0.0f) {
        const float top6_ = ry;
        cat_fold_clip(dev, hdrX, top6_, hdrW, catH_[6] * aF6_);
        ROW_TOGGLE(CTRL_ID, tr("Show", "Afficher"), c.hlShow)   // Show
        { ROW_BAND(46.0f)   // Size
            const float lo = 0.50f, hi = 2.00f; char b[16]; sprintf(b, "%d%%", (int)(c.hlScale * 100.0f + 0.5f));
            float v01 = (c.hlScale - lo) / (hi - lo); v01 = clampf(v01, 0.0f, 1.0f);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Size", "Taille"), b, &v01)) { float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f; c.hlScale = v < lo ? lo : (v > hi ? hi : v); }
        } ROW_NEXT(46.0f)
        draw_box_appearance(dev, fo, mo, click, ry, ri, e, bandX, bandW, coX, ctrlW, c.hlBox);   // Box / Transparency / Theme / Hue / Luminosity
        { ROW_BAND(46.0f)   // Max mobs shown
            const float lo = 1.0f, hi = 20.0f; char b[16]; sprintf(b, "%d", c.hlCount);
            float v01 = ((float)c.hlCount - lo) / (hi - lo); v01 = clampf(v01, 0.0f, 1.0f);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Max mobs", "Mobs max"), b, &v01)) { int v = (int)(lo + v01 * (hi - lo) + 0.5f); c.hlCount = v < 1 ? 1 : (v > 20 ? 20 : v); }
        } ROW_NEXT(46.0f)
        ROW_TOGGLE(CTRL_ID, tr("Distance column", "Colonne distance"), c.hlDist)   // Show distance column
        ROW_TOGGLE(CTRL_ID, tr("Target column", "Colonne cible"), c.hlTgt)   // Show target column
        { ROW_BAND(40.0f)   // note
            const float ty = ry + yo; fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + snap(16.0f), tr("Shows mobs that have aggro on your party.", "Affiche les mobs qui ont de la haine sur le groupe."), snap(12.0f), fa(C_MUTE), fa(C_STROKE), 1.0f);
        } ROW_NEXT(40.0f)
        cat_fold_end(dev, ry, top6_, catH_[6], aF6_);
    }   // end Display

    // ===== sub-section : TEXT (per-element typography, like the other modules) =====
    // The section FOLDS : cat_fold owns the eased progress, the clip and the cursor (config_controls.h).
    const float aF5_ = cat_fold(CTRL_ID, catOpen_[5]);
    if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Text", "Texte"), catOpen_[5])) catOpen_[5] = !catOpen_[5];
    ROW_NEXT(42.0f)
    if (aF5_ > 0.0f) {
        const float top5_ = ry;
        cat_fold_clip(dev, hdrX, top5_, hdrW, catH_[5] * aF5_);
        { ROW_BAND(52.0f)   // element selector
            const char* TLBL[HL_TE_COUNT] = { tr("Distance", "Distance"), tr("Name", "Nom"), tr("HP%", "PV%"), tr("Target", "Cible") };
            int te = (cfgHlTextElem_ < 0 || cfgHlTextElem_ >= HL_TE_COUNT) ? 0 : cfgHlTextElem_;
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Element", "\xC3\x89l\xC3\xA9ment"), TLBL[te])) { cfgHlTextElem_ = wrap(te + d, HL_TE_COUNT); }
        }
        ROW_NEXT(52.0f)
        draw_text_style(dev, fo, mo, click, ry, ri, e, bandX, bandW, coX, ctrlW,
                        c.hlText[(cfgHlTextElem_ < 0 || cfgHlTextElem_ >= HL_TE_COUNT) ? 0 : cfgHlTextElem_]);
        cat_fold_end(dev, ry, top5_, catH_[5], aF5_);
    }   // end Text

    ry += snap(10.0f);
    #undef ROW_BAND
    #undef ROW_NEXT
}

} // namespace aio
