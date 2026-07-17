// grim_config.cpp -- the Scholar Grimoire module's settings panel (//aio config -> "Grimoire (SCH)").
//
// Each control is keyed by CTRL_ID (file:line hash, see config_controls.h) -- no hand-numbered uids.
// Reuses the shared sub-section catOpen_ slots [6] (Display) / [5] (Text), like the other modules.
#include "ui/config_page.h"
#include "ui/config_controls.h"
#include "ui/config_rows.h"
#include "model/ui_config.h"
#include "gfx/font.h"
#include "gfx/draw.h"
#include <cstdio>

namespace aio {

void ConfigPage::draw_grim_config(u32 dev, Font* fo, const MouseState* mo, bool click,
                                  float& ry, int& ri, float e,
                                  float bandX, float bandW, float coX, float ctrlW,
                                  float hdrX, float hdrW) {
    UiConfig& c = ui_config();

    // ===== sub-section : DISPLAY =====
    if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Display", "Affichage"), catOpen_[6])) catOpen_[6] = !catOpen_[6];
    ROW_NEXT(42.0f)
    if (catOpen_[6]) {
        { ROW_BAND(48.0f)   // Show
            const float rowH = snap(38.0f), ty = ry + yo; fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, tr("Show", "Afficher"), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
            const float bbw = snap(112.0f), bbh = snap(34.0f), bx2 = coX + ctrlW - bbw, bty = ty + (rowH - bbh) * 0.5f;
            if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx2, bty, bbw, bbh, c.grimShow ? tr("On", "Oui") : tr("Off", "Non"), c.grimShow != 0)) { c.grimShow = !c.grimShow; save_ui_config(); }
        } ROW_NEXT(48.0f)
        { ROW_BAND(46.0f)   // Size
            const float lo = 0.50f, hi = 2.00f; char b[16]; sprintf(b, "%d%%", (int)(c.grimScale * 100.0f + 0.5f));
            float v01 = (c.grimScale - lo) / (hi - lo); v01 = clampf(v01, 0.0f, 1.0f);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Size", "Taille"), b, &v01)) { float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f; c.grimScale = v < lo ? lo : (v > hi ? hi : v); }
        } ROW_NEXT(46.0f)
        { ROW_BAND(52.0f)   // Preview art (edit only)
            const char* ALBL[4] = { tr("Light Arts", "Light Arts"), tr("Dark Arts", "Dark Arts"), tr("Addendum: White", "Addendum: White"), tr("Addendum: Black", "Addendum: Black") };
            int ar = (c.grimArt < 0 || c.grimArt > 3) ? 2 : c.grimArt;
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Preview art", "Art (aper\xC3\xA7u)"), ALBL[ar])) { c.grimArt = wrap(ar + d, 4); save_ui_config(); }
        } ROW_NEXT(52.0f)
        { ROW_BAND(40.0f)   // note
            const float ty = ry + yo; fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + snap(16.0f), tr("Only appears when SCH is your main or sub job.", "Appara\xC3\xAet seulement si SCH est en main ou sub."), snap(12.0f), fa(C_MUTE), fa(C_STROKE), 1.0f);
        } ROW_NEXT(40.0f)
    }   // end Display

    // ===== sub-section : TEXT (the charge + timer numbers) =====
    if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Text", "Texte"), catOpen_[5])) catOpen_[5] = !catOpen_[5];
    ROW_NEXT(42.0f)
    if (catOpen_[5]) {
        { ROW_BAND(52.0f)   // element selector
            const char* TLBL[GRIM_TE_COUNT] = { tr("Charge", "Charge"), tr("Timer", "Timer") };
            int te = (cfgGrimTextElem_ < 0 || cfgGrimTextElem_ >= GRIM_TE_COUNT) ? 0 : cfgGrimTextElem_;
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Element", "\xC3\x89l\xC3\xA9ment"), TLBL[te])) { cfgGrimTextElem_ = wrap(te + d, GRIM_TE_COUNT); }
        }
        ROW_NEXT(52.0f)
        draw_text_style(dev, fo, mo, click, ry, ri, e, bandX, bandW, coX, ctrlW,
                        c.grimText[(cfgGrimTextElem_ < 0 || cfgGrimTextElem_ >= GRIM_TE_COUNT) ? 0 : cfgGrimTextElem_]);   // shared Font/Size/Outline/Style/Colour block (was an inline copy)
    }   // end Text

    ry += snap(10.0f);
    #undef ROW_BAND
    #undef ROW_NEXT
}

} // namespace aio
