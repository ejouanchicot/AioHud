// zt_config.cpp -- the Zone Tracker module's settings panel (//aio config -> "Zone Tracker").
//
// Each control is keyed by CTRL_ID (file:line hash, see config_controls.h) -- no hand-numbered uids.
// Reuses catOpen_ [6] (Display)/[5] (Text).
#include "ui/config_page.h"
#include "ui/config_controls.h"
#include "ui/config_rows.h"
#include "model/ui_config.h"
#include "gfx/font.h"
#include "gfx/draw.h"
#include <cstdio>

namespace aio {

void ConfigPage::draw_zt_config(u32 dev, Font* fo, const MouseState* mo, bool click,
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
            if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx2, bty, bbw, bbh, c.ztShow ? tr("On", "Oui") : tr("Off", "Non"), c.ztShow != 0)) { c.ztShow = !c.ztShow; save_ui_config(); }
        } ROW_NEXT(48.0f)
        { ROW_BAND(46.0f)   // Size (canonical : right after Show)
            const float lo = 0.50f, hi = 2.00f; char b[16]; sprintf(b, "%d%%", (int)(c.ztScale * 100.0f + 0.5f));
            float v01 = (c.ztScale - lo) / (hi - lo); v01 = clampf(v01, 0.0f, 1.0f);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Size", "Taille"), b, &v01)) { float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f; c.ztScale = v < lo ? lo : (v > hi ? hi : v); }
        } ROW_NEXT(46.0f)
        draw_box_appearance(dev, fo, mo, click, ry, ri, e, bandX, bandW, coX, ctrlW, c.ztBox);   // Box / Transparency / Theme / Hue / Luminosity
        { ROW_BAND(48.0f)   // Show title row (Dynamis / Abyssea / Omen / Nyzul / Sheol)
            const float rowH = snap(38.0f), ty = ry + yo; fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, tr("Show title", "Afficher le titre"), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
            const float bbw = snap(112.0f), bbh = snap(34.0f), bx2 = coX + ctrlW - bbw, bty = ty + (rowH - bbh) * 0.5f;
            if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx2, bty, bbw, bbh, c.ztHeader ? tr("On", "Oui") : tr("Off", "Non"), c.ztHeader != 0)) { c.ztHeader = !c.ztHeader; save_ui_config(); }
        } ROW_NEXT(48.0f)
        { ROW_BAND(52.0f)   // Preview zone
            const char* ZLBL[5] = { tr("Dynamis", "Dynamis"), tr("Abyssea", "Abyssea"), tr("Omen", "Omen"), tr("Nyzul", "Nyzul"), tr("Sheol", "Sheol") };
            int zv = (c.ztVariant < 0 || c.ztVariant > 4) ? 1 : c.ztVariant;
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Preview zone", "Zone (aper\xC3\xA7u)"), ZLBL[zv])) { c.ztVariant = wrap(zv + d, 5); save_ui_config(); }
        } ROW_NEXT(52.0f)
        // ---- Sheol / Odyssey sub-options (only meaningful in Sheol ; harmless elsewhere) ----
        #define ZT_SHEOL_TOGGLE(UID, LABEL, FIELD)                                                                       \
            { ROW_BAND(48.0f) const float rowH = snap(38.0f), ty = ry + yo; fo->begin(dev);                             \
              fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, LABEL, snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f); \
              const float bbw = snap(112.0f), bbh = snap(34.0f), bx2 = coX + ctrlW - bbw, bty = ty + (rowH - bbh) * 0.5f;\
              if (toggle_chip(dev, fo, mo, click, UID, bx2, bty, bbw, bbh, (FIELD) ? tr("On", "Oui") : tr("Off", "Non"), (FIELD) != 0)) { FIELD = !(FIELD); save_ui_config(); } } ROW_NEXT(48.0f)
        ZT_SHEOL_TOGGLE(CTRL_ID, tr("Sheol: segments", "Sheol : segments"),          c.ztSheolSeg)
        ZT_SHEOL_TOGGLE(CTRL_ID, tr("Sheol: resistances", "Sheol : r\xC3\xA9sistances"), c.ztSheolRes)
        ZT_SHEOL_TOGGLE(CTRL_ID, tr("Sheol: Cruel Joke", "Sheol : Cruel Joke"),      c.ztSheolJoke)
        #undef ZT_SHEOL_TOGGLE
        { ROW_BAND(40.0f)   // note
            const float ty = ry + yo; fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + snap(16.0f), tr("Only appears in a tracked zone (Dynamis, Abyssea, Omen, Nyzul, Sheol).", "Appara\xC3\xAet seulement en zone suivie (Dynamis, Abyssea, Omen, Nyzul, Sheol)."), snap(12.0f), fa(C_MUTE), fa(C_STROKE), 1.0f);
        } ROW_NEXT(40.0f)
    }   // end Display

    // ===== sub-section : TEXT =====
    if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Text", "Texte"), catOpen_[5])) catOpen_[5] = !catOpen_[5];
    ROW_NEXT(42.0f)
    if (catOpen_[5]) {
        { ROW_BAND(52.0f)   // element selector
            const char* TLBL[ZT_TE_COUNT] = { tr("Header", "Titre"), tr("Body", "Corps") };
            int te = (cfgZtTextElem_ < 0 || cfgZtTextElem_ >= ZT_TE_COUNT) ? 0 : cfgZtTextElem_;
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Element", "\xC3\x89l\xC3\xA9ment"), TLBL[te])) { cfgZtTextElem_ = wrap(te + d, ZT_TE_COUNT); }
        }
        ROW_NEXT(52.0f)
        draw_text_style(dev, fo, mo, click, ry, ri, e, bandX, bandW, coX, ctrlW,
                        c.ztText[(cfgZtTextElem_ < 0 || cfgZtTextElem_ >= ZT_TE_COUNT) ? 0 : cfgZtTextElem_]);
    }   // end Text

    ry += snap(10.0f);
    #undef ROW_BAND
    #undef ROW_NEXT
}

} // namespace aio
