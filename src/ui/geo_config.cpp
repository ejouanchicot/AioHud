// geo_config.cpp -- the GEO tab of //aio config -> "Pet" (pet_config.cpp lays the page out : its common rows, the job
// tab, then these rows) : what the Pet box shows on a GEO main.
//
// Each control is keyed by CTRL_ID (file:line hash, see config_controls.h) -- no hand-numbered uids.
// SPEC docs/design/geo-module-spec.md : every element of the box has its own on / off ; the box's place, size and
// frame are the Pet module's (docs/design/pet-module-spec.md).
#include "ui/config_page.h"
#include "ui/config_controls.h"
#include "ui/config_rows.h"
#include "model/ui_config.h"
#include "gfx/font.h"
#include "gfx/draw.h"
#include <cstdio>

namespace aio {

// the Display rows of the GEO tab
void ConfigPage::draw_geo_rows(u32 dev, Font* fo, const MouseState* mo, bool click,
                               float& ry, int& ri, float e,
                               float bandX, float bandW, float coX, float ctrlW) {
    UiConfig& c = ui_config();
    ROW_TOGGLE(CTRL_ID, tr("Show on GEO", "Afficher en GEO"), c.geoShow)
    ROW_TOGGLE(CTRL_ID, tr("Only with a luopan out", "Seulement avec un luopan"), c.geoOnlyLuopan)
    { ROW_BAND(46.0f)   // the effect icons' size
        const float lo = 0.50f, hi = 2.00f; char b[16]; sprintf(b, "%d%%", (int)(c.geoIconSz * 100.0f + 0.5f));
        float v01 = (c.geoIconSz - lo) / (hi - lo); v01 = clampf(v01, 0.0f, 1.0f);
        if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Icon / luopan size", "Taille ic\xC3\xB4nes / luopan"), b, &v01)) { float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f; c.geoIconSz = v < lo ? lo : (v > hi ? hi : v); }
    } ROW_NEXT(46.0f)
    ROW_TOGGLE(CTRL_ID, tr("Bubble name", "Nom de la bulle"), c.geoBubble)
    ROW_TOGGLE(CTRL_ID, tr("Luopan HP", "HP du luopan"), c.geoHp)   // the ring and the % in the bubble (text look : the gauge)
    ROW_TOGGLE(CTRL_ID, tr("Estimated time left", "Temps estim\xC3\xA9 du luopan"), c.geoTime)   // from the HP slope : off by default
    ROW_TOGGLE(CTRL_ID, tr("Distance to the luopan", "Distance au luopan"), c.geoDist)
    ROW_TOGGLE(CTRL_ID, tr("Aura (Indi-)", "Aura (Indi-)"), c.geoAura)
    ROW_TOGGLE(CTRL_ID, tr("Entrust", "Entrust"), c.geoEntrust)
    ROW_TOGGLE(CTRL_ID, tr("MP", "MP"), c.geoMp)
    ROW_TOGGLE(CTRL_ID, tr("Job abilities", "Job abilities"), c.geoJa)
    { ROW_BAND(52.0f)   // how the abilities show : a row of icons, a grid of icon + name, a grid of names
        const char* JL[3] = { tr("Icons", "Ic\xC3\xB4nes"), tr("Icons + names", "Ic\xC3\xB4nes + noms"), tr("Names", "Noms") };
        const int js = (c.geoJaStyle < 0 || c.geoJaStyle > 2) ? 0 : c.geoJaStyle;
        if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Abilities as", "Job abilities en"), JL[js])) { c.geoJaStyle = wrap(js + d, 3); c.geoJaIcons = c.geoJaStyle != 2; save_ui_config(); }
    } ROW_NEXT(52.0f)
    ROW_TOGGLE(CTRL_ID, tr("Cardinal Chant compass", "Boussole Cardinal Chant"), c.geoCompass)   // round the luopan
    ROW_TOGGLE(CTRL_ID, tr("Cardinal bonus (MAB, M.Acc...)", "Bonus Cardinal (MAB, M.Acc...)"), c.geoCardText)   // the text under it
    { ROW_BAND(52.0f)   // where the compass and the luopan go : on top of the box, beside the text, or alone
        const char* LL[3] = { tr("Compass on top", "Boussole au-dessus"), tr("Side by side", "C\xC3\xB4te \xC3\xA0 c\xC3\xB4te"), tr("Bubble only", "Bulle seule") };
        const int l = (c.geoLayout < 0 || c.geoLayout > 2) ? 0 : c.geoLayout;
        if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Layout", "Disposition"), LL[l])) { c.geoLayout = wrap(l + d, 3); save_ui_config(); }
    } ROW_NEXT(52.0f)
    { ROW_BAND(40.0f)   // note
        const float ty = ry + yo; fo->begin(dev);
        fo->draw_lc(dev, coX + snap(4.0f), ty + snap(16.0f), tr("Shown when GEO is your main job.", "Affich\xC3\xA9 quand GEO est ton job principal."), snap(12.0f), fa(C_MUTE), fa(C_STROKE), 1.0f);
    } ROW_NEXT(40.0f)
}

// the Text rows of the GEO tab : which text element, then its typography
void ConfigPage::draw_geo_text_rows(u32 dev, Font* fo, const MouseState* mo, bool click,
                                    float& ry, int& ri, float e,
                                    float bandX, float bandW, float coX, float ctrlW) {
    UiConfig& c = ui_config();
    { ROW_BAND(52.0f)   // element selector
        const char* TLBL[GEO_TE_COUNT] = { tr("Bubble / aura names", "Noms bulle / aura"), tr("HP / MP / times / distance", "HP / MP / temps / distance"), tr("Job abilities", "Job abilities") };
        int te = (cfgGeoTextElem_ < 0 || cfgGeoTextElem_ >= GEO_TE_COUNT) ? 0 : cfgGeoTextElem_;
        if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Element", "\xC3\x89l\xC3\xA9ment"), TLBL[te])) { cfgGeoTextElem_ = wrap(te + d, GEO_TE_COUNT); }
    }
    ROW_NEXT(52.0f)
    draw_text_style(dev, fo, mo, click, ry, ri, e, bandX, bandW, coX, ctrlW,
                    c.geoText[(cfgGeoTextElem_ < 0 || cfgGeoTextElem_ >= GEO_TE_COUNT) ? 0 : cfgGeoTextElem_]);
}

#undef ROW_BAND
#undef ROW_NEXT

} // namespace aio
