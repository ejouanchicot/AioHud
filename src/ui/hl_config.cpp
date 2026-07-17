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
    if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Display", "Affichage"), catOpen_[6])) catOpen_[6] = !catOpen_[6];
    ROW_NEXT(42.0f)
    if (catOpen_[6]) {
        { ROW_BAND(48.0f)   // Show
            const float rowH = snap(38.0f), ty = ry + yo; fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, tr("Show", "Afficher"), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
            const float bbw = snap(112.0f), bbh = snap(34.0f), bx2 = coX + ctrlW - bbw, bty = ty + (rowH - bbh) * 0.5f;
            if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx2, bty, bbw, bbh, c.hlShow ? tr("On", "Oui") : tr("Off", "Non"), c.hlShow != 0)) { c.hlShow = !c.hlShow; save_ui_config(); }
        } ROW_NEXT(48.0f)
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
        { ROW_BAND(48.0f)   // Show distance column
            const float rowH = snap(38.0f), ty = ry + yo; fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, tr("Distance column", "Colonne distance"), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
            const float bbw = snap(112.0f), bbh = snap(34.0f), bx2 = coX + ctrlW - bbw, bty = ty + (rowH - bbh) * 0.5f;
            if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx2, bty, bbw, bbh, c.hlDist ? tr("On", "Oui") : tr("Off", "Non"), c.hlDist != 0)) { c.hlDist = !c.hlDist; save_ui_config(); }
        } ROW_NEXT(48.0f)
        { ROW_BAND(48.0f)   // Show target column
            const float rowH = snap(38.0f), ty = ry + yo; fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, tr("Target column", "Colonne cible"), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
            const float bbw = snap(112.0f), bbh = snap(34.0f), bx2 = coX + ctrlW - bbw, bty = ty + (rowH - bbh) * 0.5f;
            if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx2, bty, bbw, bbh, c.hlTgt ? tr("On", "Oui") : tr("Off", "Non"), c.hlTgt != 0)) { c.hlTgt = !c.hlTgt; save_ui_config(); }
        } ROW_NEXT(48.0f)
        { ROW_BAND(40.0f)   // note
            const float ty = ry + yo; fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + snap(16.0f), tr("Shows mobs that have aggro on your party.", "Affiche les mobs qui ont de la haine sur le groupe."), snap(12.0f), fa(C_MUTE), fa(C_STROKE), 1.0f);
        } ROW_NEXT(40.0f)
    }   // end Display

    // ===== sub-section : TEXT (per-element typography, like the other modules) =====
    if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Text", "Texte"), catOpen_[5])) catOpen_[5] = !catOpen_[5];
    ROW_NEXT(42.0f)
    if (catOpen_[5]) {
        { ROW_BAND(52.0f)   // element selector
            const char* TLBL[HL_TE_COUNT] = { tr("Distance", "Distance"), tr("Name", "Nom"), tr("HP%", "PV%"), tr("Target", "Cible") };
            int te = (cfgHlTextElem_ < 0 || cfgHlTextElem_ >= HL_TE_COUNT) ? 0 : cfgHlTextElem_;
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Element", "\xC3\x89l\xC3\xA9ment"), TLBL[te])) { cfgHlTextElem_ = wrap(te + d, HL_TE_COUNT); }
        }
        ROW_NEXT(52.0f)
        draw_text_style(dev, fo, mo, click, ry, ri, e, bandX, bandW, coX, ctrlW,
                        c.hlText[(cfgHlTextElem_ < 0 || cfgHlTextElem_ >= HL_TE_COUNT) ? 0 : cfgHlTextElem_]);
    }   // end Text

    ry += snap(10.0f);
    #undef ROW_BAND
    #undef ROW_NEXT
}

} // namespace aio
