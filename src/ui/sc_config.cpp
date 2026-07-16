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
    if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Display", "Affichage"), catOpen_[6])) catOpen_[6] = !catOpen_[6];
    ROW_NEXT(42.0f)
    if (catOpen_[6]) {
        { ROW_BAND(48.0f)   // Show the skillchains box
            const float rowH = snap(38.0f), ty = ry + yo; fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, tr("Show", "Afficher"), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
            const float bbw = snap(112.0f), bbh = snap(34.0f), bx2 = coX + ctrlW - bbw, bty = ty + (rowH - bbh) * 0.5f;
            if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx2, bty, bbw, bbh, c.scShow ? tr("On", "Oui") : tr("Off", "Non"), c.scShow != 0)) { c.scShow = !c.scShow; save_ui_config(); }
        } ROW_NEXT(48.0f)
        { ROW_BAND(46.0f)   // Size (overall box scale) -- canonical : right after Show
            const float lo = 0.50f, hi = 2.00f; char b[16]; sprintf(b, "%d%%", (int)(c.scScale * 100.0f + 0.5f));
            float v01 = (c.scScale - lo) / (hi - lo); v01 = clampf(v01, 0.0f, 1.0f);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Size", "Taille"), b, &v01)) { float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f; c.scScale = v < lo ? lo : (v > hi ? hi : v); }
        } ROW_NEXT(46.0f)
        draw_box_appearance(dev, fo, mo, click, ry, ri, e, bandX, bandW, coX, ctrlW, c.scBox);   // Box / Transparency / Theme / Hue / Luminosity
        { ROW_BAND(48.0f)   // Display scope : also show party/nearby chains (not just your target)
            const float rowH = snap(38.0f), ty = ry + yo; fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, tr("Show party/nearby chains", "Afficher SC du groupe/proches"), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
            const float bbw = snap(112.0f), bbh = snap(34.0f), bx2 = coX + ctrlW - bbw, bty = ty + (rowH - bbh) * 0.5f;
            if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx2, bty, bbw, bbh, c.scNearby ? tr("On", "Oui") : tr("Off", "Non"), c.scNearby != 0)) { c.scNearby = !c.scNearby; save_ui_config(); }
        } ROW_NEXT(48.0f)
        { ROW_BAND(44.0f)   // position -> drag it in //aio edit (like every other box)
            const float ty = ry + yo; fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + snap(19.0f), tr("Position: drag it in //aio edit", "Position : d\xC3\xA9place-la dans //aio edit"), snap(13.0f), fa(C_MUTE), fa(C_STROKE), 1.0f);
        } ROW_NEXT(44.0f)
    }   // end Display

    // ===== sub-section : ELEMENTS (which lines to show) =====
    if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Elements", "\xC3\x89l\xC3\xA9ments"), catOpen_[7])) catOpen_[7] = !catOpen_[7];
    ROW_NEXT(42.0f)
    if (catOpen_[7]) {
        #define SC_TOGGLE(UID, LABEL, FIELD)                                                                                  \
            { ROW_BAND(48.0f) const float rowH = snap(38.0f), ty = ry + yo; fo->begin(dev);                                 \
              fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, LABEL, snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);      \
              const float bbw = snap(112.0f), bbh = snap(34.0f), bx2 = coX + ctrlW - bbw, bty = ty + (rowH - bbh) * 0.5f;    \
              if (toggle_chip(dev, fo, mo, click, UID, bx2, bty, bbw, bbh, (FIELD) ? tr("On", "Oui") : tr("Off", "Non"), (FIELD) != 0)) { FIELD = !(FIELD); save_ui_config(); } } ROW_NEXT(48.0f)
        SC_TOGGLE(CTRL_ID, tr("Title", "Titre"),                            c.scTitle)
        SC_TOGGLE(CTRL_ID, tr("Timer (Go! / Burst)", "Timer (Go! / Burst)"), c.scTimer)
        SC_TOGGLE(CTRL_ID, tr("Step line", "Ligne Step"),                    c.scStep)
        SC_TOGGLE(CTRL_ID, tr("Property + elements", "Propri\xC3\xA9t\xC3\xA9 + \xC3\xA9l\xC3\xA9ments"), c.scProps)
        SC_TOGGLE(CTRL_ID, tr("Weaponskill list", "Liste weaponskills"),     c.scList)
        #undef SC_TOGGLE
    }   // end Elements

    // ===== sub-section : TEXT (per-element typography, like the other modules) =====
    if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Text", "Texte"), catOpen_[5])) catOpen_[5] = !catOpen_[5];
    ROW_NEXT(42.0f)
    if (catOpen_[5]) {
        { ROW_BAND(52.0f)   // element selector
            const char* TLBL[SC_TE_COUNT] = { tr("Title", "Titre"), tr("Timer", "Timer"), tr("Step", "Step"), tr("Property", "Propri\xC3\xA9t\xC3\xA9"), tr("WS list", "Liste WS") };
            int te = (cfgScTextElem_ < 0 || cfgScTextElem_ >= SC_TE_COUNT) ? 0 : cfgScTextElem_;
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Element", "\xC3\x89l\xC3\xA9ment"), TLBL[te])) { cfgScTextElem_ = wrap(te + d, SC_TE_COUNT); }
        }
        ROW_NEXT(52.0f)
        draw_text_style(dev, fo, mo, click, ry, ri, e, bandX, bandW, coX, ctrlW,
                        c.scText[(cfgScTextElem_ < 0 || cfgScTextElem_ >= SC_TE_COUNT) ? 0 : cfgScTextElem_]);
    }   // end Text

    ry += snap(10.0f);
    #undef ROW_BAND
    #undef ROW_NEXT
}

} // namespace aio
