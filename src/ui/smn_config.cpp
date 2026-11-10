// smn_config.cpp -- the SMN tab of //aio config -> "Pet" (pet_config.cpp lays the page out : its common rows, the job
// tab, then these rows) : what the Pet box shows on a SMN main.
//
// Each control is keyed by CTRL_ID (file:line hash, see config_controls.h) -- no hand-numbered uids.
// SPEC docs/design/smn-module-spec.md : every element of the box has its own on / off ; the box's place, size and
// frame are the Pet module's (docs/design/pet-module-spec.md).
#include "ui/config_page.h"
#include "ui/config_controls.h"
#include "ui/config_rows.h"
#include "model/ui_config.h"
#include "model/smn_avatars_gen.h"   // the preview's avatar choice
#include "gfx/font.h"
#include "gfx/draw.h"
#include <cstdio>

namespace aio {

// the Display rows of the SMN tab
void ConfigPage::draw_smn_rows(u32 dev, Font* fo, const MouseState* mo, bool click,
                               float& ry, int& ri, float e,
                               float bandX, float bandW, float coX, float ctrlW) {
    UiConfig& c = ui_config();
    ROW_TOGGLE(CTRL_ID, tr("Show on SMN", "Afficher en SMN"), c.smnShow)
    ROW_TOGGLE(CTRL_ID, tr("Only with an avatar out", "Seulement avec un avatar"), c.smnOnlyPet)
    { ROW_BAND(46.0f)   // the portrait's and the ability icons' size
        const float lo = 0.50f, hi = 2.00f; char b[16]; sprintf(b, "%d%%", (int)(c.smnIconSz * 100.0f + 0.5f));
        float v01 = (c.smnIconSz - lo) / (hi - lo); v01 = clampf(v01, 0.0f, 1.0f);
        if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Portrait / icon size", "Taille portrait / ic\xC3\xB4nes"), b, &v01)) { float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f; c.smnIconSz = v < lo ? lo : (v > hi ? hi : v); }
    } ROW_NEXT(46.0f)
    { ROW_BAND(52.0f)   // where the portrait goes : on top of the box, or beside its text
        const char* LL[2] = { tr("Portrait on top", "Portrait au-dessus"), tr("Side by side", "C\xC3\xB4te \xC3\xA0 c\xC3\xB4te") };
        const int l = (c.smnLayout == 1) ? 1 : 0;
        if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Layout", "Disposition"), LL[l])) { c.smnLayout = wrap(l + d, 2); save_ui_config(); }
    } ROW_NEXT(52.0f)
    { ROW_BAND(52.0f)   // which avatar or spirit the preview and //aio edit show : every picture and pact list can be checked
        const int ai = (c.smnDemo < 0 || c.smnDemo >= SMN_AVATARS_N) ? 0 : c.smnDemo;
        if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Preview avatar", "Avatar (aper\xC3\xA7u)"), SMN_AVATARS[ai].en)) { c.smnDemo = wrap(ai + d, SMN_AVATARS_N); save_ui_config(); }
    } ROW_NEXT(52.0f)
    ROW_TOGGLE(CTRL_ID, tr("Avatar portrait", "Portrait de l'avatar"), c.smnPortrait)
    ROW_TOGGLE(CTRL_ID, tr("Avatar name", "Nom de l'avatar"), c.smnName)
    ROW_TOGGLE(CTRL_ID, tr("Avatar HP", "HP de l'avatar"), c.smnHp)
    ROW_TOGGLE(CTRL_ID, tr("Avatar TP", "TP de l'avatar"), c.smnTp)
    ROW_TOGGLE(CTRL_ID, tr("Distance to the avatar", "Distance \xC3\xA0 l'avatar"), c.smnDist)
    ROW_TOGGLE(CTRL_ID, tr("Blood Pact Rage / Ward", "Blood Pact Rage / Ward"), c.smnBp)
    ROW_TOGGLE(CTRL_ID, tr("Job abilities", "Job abilities"), c.smnJa)
    ROW_TOGGLE(CTRL_ID, tr("MP", "MP"), c.smnMp)
    ROW_TOGGLE(CTRL_ID, tr("The avatar's pacts", "Les pacts de l'avatar"), c.smnPacts)
    { ROW_BAND(40.0f)   // note
        const float ty = ry + yo; fo->begin(dev);
        fo->draw_lc(dev, coX + snap(4.0f), ty + snap(16.0f), tr("Shown when SMN is your main job.", "Affich\xC3\xA9 quand SMN est ton job principal."), snap(12.0f), fa(C_MUTE), fa(C_STROKE), 1.0f);
    } ROW_NEXT(40.0f)
}

// the Text rows of the SMN tab : which text element, then its typography
void ConfigPage::draw_smn_text_rows(u32 dev, Font* fo, const MouseState* mo, bool click,
                                    float& ry, int& ri, float e,
                                    float bandX, float bandW, float coX, float ctrlW) {
    UiConfig& c = ui_config();
    { ROW_BAND(52.0f)   // element selector
        const char* TLBL[SMN_TE_COUNT] = { tr("Avatar name", "Nom de l'avatar"), tr("HP / TP / MP / distance", "HP / TP / MP / distance"),
                                           tr("Job abilities", "Job abilities"), tr("Pacts", "Pacts") };
        int te = (cfgSmnTextElem_ < 0 || cfgSmnTextElem_ >= SMN_TE_COUNT) ? 0 : cfgSmnTextElem_;
        if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Element", "\xC3\x89l\xC3\xA9ment"), TLBL[te])) { cfgSmnTextElem_ = wrap(te + d, SMN_TE_COUNT); }
    }
    ROW_NEXT(52.0f)
    draw_text_style(dev, fo, mo, click, ry, ri, e, bandX, bandW, coX, ctrlW,
                    c.smnText[(cfgSmnTextElem_ < 0 || cfgSmnTextElem_ >= SMN_TE_COUNT) ? 0 : cfgSmnTextElem_]);
}

#undef ROW_BAND
#undef ROW_NEXT

} // namespace aio
