// sc_config.cpp -- the Skillchains module's settings panel (//aio config -> "Skillchains").
//
// Each control is keyed by CTRL_ID (file:line hash, see config_controls.h) -- no hand-numbered uids. Reuses shared
// sub-section catOpen_ slots [6]/[7]/[5]. The box
// itself is placed in //aio edit (like every other box) -- position isn't a slider here.
#include "ui/config_page.h"
#include "ui/config_controls.h"    // cat_header / row_slider / row_selector / toggle_chip + palette + color_picker
#include "ui/config_rows.h"        // ROW_BAND / ROW_NEXT / CFG_COLOR_PICKER
#include "model/ui_config.h"
#include "gfx/font.h"
#include "gfx/draw.h"
#include <cstdio>

namespace aio {

void ConfigPage::draw_sc_config(u32 dev, Font* fo, const MouseState* mo, bool click,
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
        ROW_TOGGLE(CTRL_ID, tr("Show", "Afficher"), c.scShow)   // Show the skillchains box
        { ROW_BAND(46.0f)   // Size (overall box scale) -- canonical : right after Show
            const float lo = 0.50f, hi = 2.00f; char b[16]; sprintf(b, "%d%%", (int)(c.scScale * 100.0f + 0.5f));
            float v01 = (c.scScale - lo) / (hi - lo); v01 = clampf(v01, 0.0f, 1.0f);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Size", "Taille"), b, &v01)) { float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f; c.scScale = v < lo ? lo : (v > hi ? hi : v); }
        } ROW_NEXT(46.0f)
        draw_box_appearance(dev, fo, mo, click, ry, ri, e, bandX, bandW, coX, ctrlW, c.scBox);   // Box / Transparency / Theme / Hue / Luminosity
        ROW_TOGGLE(CTRL_ID, tr("Show party/nearby chains", "Afficher SC du groupe/proches"), c.scNearby)   // Display scope : also show party/nearby chains (not just your target)
        cat_fold_end(dev, ry, top6_, catH_[6], aF6_);
    }   // end Display

    // ===== sub-section : ELEMENTS (which lines to show) =====
    // The section FOLDS : cat_fold owns the eased progress, the clip and the cursor (config_controls.h).
    const float aF7_ = cat_fold(CTRL_ID, catOpen_[7]);
    if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Elements", "\xC3\x89l\xC3\xA9ments"), catOpen_[7])) catOpen_[7] = !catOpen_[7];
    ROW_NEXT(42.0f)
    if (aF7_ > 0.0f) {
        const float top7_ = ry;
        cat_fold_clip(dev, hdrX, top7_, hdrW, catH_[7] * aF7_);
        ROW_TOGGLE(CTRL_ID, tr("Title", "Titre"),                            c.scTitle)
        ROW_TOGGLE(CTRL_ID, tr("Timer (Go! / Burst)", "Timer (Go! / Burst)"), c.scTimer)
        ROW_TOGGLE(CTRL_ID, tr("Step line", "Ligne Step"),                    c.scStep)
        ROW_TOGGLE(CTRL_ID, tr("Property + elements", "Propri\xC3\xA9t\xC3\xA9 + \xC3\xA9l\xC3\xA9ments"), c.scProps)
        ROW_TOGGLE(CTRL_ID, tr("Weaponskill list", "Liste weaponskills"),     c.scList)
        ROW_TOGGLE(CTRL_ID, tr("TP indicator", "Indicateur TP"),              c.scTP)
        // WS spacing : the vertical gap between each weaponskill in the continuation list. Always shown (it only
        // has a visible effect while the list is on, but hiding it made it hard to find).
        { ROW_BAND(46.0f)
            const float lo = 0.60f, hi = 3.00f; char b[16]; sprintf(b, "%d%%", (int)(c.scListGap * 100.0f + 0.5f));
            float v01 = (c.scListGap - lo) / (hi - lo); v01 = clampf(v01, 0.0f, 1.0f);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("WS spacing", "Espacement WS"), b, &v01)) { float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f; c.scListGap = v < lo ? lo : (v > hi ? hi : v); }
        } ROW_NEXT(46.0f)
        cat_fold_end(dev, ry, top7_, catH_[7], aF7_);
    }   // end Elements

    // ===== sub-section : TEXT (per-element typography, like the other modules) =====
    // The section FOLDS : cat_fold owns the eased progress, the clip and the cursor (config_controls.h).
    const float aF5_ = cat_fold(CTRL_ID, catOpen_[5]);
    if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Text", "Texte"), catOpen_[5])) catOpen_[5] = !catOpen_[5];
    ROW_NEXT(42.0f)
    if (aF5_ > 0.0f) {
        const float top5_ = ry;
        cat_fold_clip(dev, hdrX, top5_, hdrW, catH_[5] * aF5_);
        { ROW_BAND(52.0f)   // element selector
            const char* TLBL[SC_TE_COUNT] = { tr("Title", "Titre"), tr("Timer", "Timer"), tr("Step", "Step"), tr("Property", "Propri\xC3\xA9t\xC3\xA9"), tr("WS list", "Liste WS") };
            int te = (cfgScTextElem_ < 0 || cfgScTextElem_ >= SC_TE_COUNT) ? 0 : cfgScTextElem_;
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Element", "\xC3\x89l\xC3\xA9ment"), TLBL[te])) { cfgScTextElem_ = wrap(te + d, SC_TE_COUNT); }
        }
        ROW_NEXT(52.0f)
        draw_text_style(dev, fo, mo, click, ry, ri, e, bandX, bandW, coX, ctrlW,
                        c.scText[(cfgScTextElem_ < 0 || cfgScTextElem_ >= SC_TE_COUNT) ? 0 : cfgScTextElem_]);
        cat_fold_end(dev, ry, top5_, catH_[5], aF5_);
    }   // end Text

    ry += snap(10.0f);
    #undef ROW_BAND
    #undef ROW_NEXT
}

} // namespace aio
