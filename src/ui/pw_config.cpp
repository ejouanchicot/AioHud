// pw_config.cpp -- the PointWatch module's settings panel (//aio config -> "PointWatch").
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

void ConfigPage::draw_pw_config(u32 dev, Font* fo, const MouseState* mo, bool click,
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
            if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx2, bty, bbw, bbh, c.pwShow ? tr("On", "Oui") : tr("Off", "Non"), c.pwShow != 0)) { c.pwShow = !c.pwShow; save_ui_config(); }
        } ROW_NEXT(48.0f)
        { ROW_BAND(46.0f)   // Size
            const float lo = 0.50f, hi = 2.00f; char b[16]; sprintf(b, "%d%%", (int)(c.pwScale * 100.0f + 0.5f));
            float v01 = (c.pwScale - lo) / (hi - lo); v01 = clampf(v01, 0.0f, 1.0f);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Size", "Taille"), b, &v01)) { float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f; c.pwScale = v < lo ? lo : (v > hi ? hi : v); }
        } ROW_NEXT(46.0f)
        draw_box_appearance(dev, fo, mo, click, ry, ri, e, bandX, bandW, coX, ctrlW, c.pwBox);   // Box / Transparency / Theme / Hue / Luminosity
        { ROW_BAND(52.0f)   // Progression stage (Auto / XP / CP / Master)
            const char* MLBL[4] = { tr("Auto", "Auto"), tr("XP (leveling)", "XP (level)"), tr("CP (capacity)", "CP (capacit\xC3\xA9)"), tr("Master (ML)", "Master (ML)") };
            int md = (c.pwMode < 0 || c.pwMode > 3) ? 0 : c.pwMode;
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Progression", "Progression"), MLBL[md])) { c.pwMode = wrap(md + d, 4); save_ui_config(); }
        } ROW_NEXT(52.0f)
        { ROW_BAND(52.0f)   // Layout : Vertical (stacked) / Horizontal (side by side)
            const char* LLBL[2] = { tr("Vertical", "Vertical"), tr("Horizontal", "Horizontal") };
            int ly = (c.pwLayout == 1) ? 1 : 0;
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Layout", "Disposition"), LLBL[ly])) { c.pwLayout = wrap(ly + d, 2); save_ui_config(); }
        } ROW_NEXT(52.0f)
        { ROW_BAND(52.0f)   // Show : Both (text + bar) / Text only / Bar only
            const char* DLBL[3] = { tr("Both", "Les deux"), tr("Text only", "Texte seul"), tr("Bar only", "Barre seule") };
            int dm = (c.pwDisplay < 0 || c.pwDisplay > 2) ? 0 : c.pwDisplay;
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Show", "Afficher"), DLBL[dm])) { c.pwDisplay = wrap(dm + d, 3); save_ui_config(); }
        } ROW_NEXT(52.0f)
        { ROW_BAND(48.0f)   // Rate X/h
            const float rowH = snap(38.0f), ty = ry + yo; fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, tr("Show rate (X/h)", "Afficher le taux (X/h)"), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
            const float bbw = snap(112.0f), bbh = snap(34.0f), bx2 = coX + ctrlW - bbw, bty = ty + (rowH - bbh) * 0.5f;
            if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx2, bty, bbw, bbh, c.pwRate ? tr("On", "Oui") : tr("Off", "Non"), c.pwRate != 0)) { c.pwRate = !c.pwRate; save_ui_config(); }
        } ROW_NEXT(48.0f)
        { ROW_BAND(40.0f)   // note
            const float ty = ry + yo; fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + snap(16.0f), tr("XP under 99, CP at 99, ML once mastered. + Merits.", "XP <99, CP a\xCC\x80 99, ML si master. + Merits."), snap(12.0f), fa(C_MUTE), fa(C_STROKE), 1.0f);
        } ROW_NEXT(40.0f)
    }   // end Display

    // ===== sub-section : TEXT =====
    if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Text", "Texte"), catOpen_[5])) catOpen_[5] = !catOpen_[5];
    ROW_NEXT(42.0f)
    if (catOpen_[5]) {
        { ROW_BAND(52.0f)   // element selector
            const char* TLBL[PW_TE_COUNT] = { tr("Label", "Label"), tr("Value", "Valeur"), tr("Rate", "Taux") };
            int te = (cfgPwTextElem_ < 0 || cfgPwTextElem_ >= PW_TE_COUNT) ? 0 : cfgPwTextElem_;
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Element", "\xC3\x89l\xC3\xA9ment"), TLBL[te])) { cfgPwTextElem_ = wrap(te + d, PW_TE_COUNT); }
        }
        ROW_NEXT(52.0f)
        draw_text_style(dev, fo, mo, click, ry, ri, e, bandX, bandW, coX, ctrlW,
                        c.pwText[(cfgPwTextElem_ < 0 || cfgPwTextElem_ >= PW_TE_COUNT) ? 0 : cfgPwTextElem_]);
    }   // end Text

    ry += snap(10.0f);
    #undef ROW_BAND
    #undef ROW_NEXT
}

} // namespace aio
