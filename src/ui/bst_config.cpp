// bst_config.cpp -- the BST tab of //aio config -> "Pet" (pet_config.cpp lays the page out : its common rows, the job
// tab, then these rows) : what the Pet box shows on a BST main.
//
// Each control is keyed by CTRL_ID (file:line hash, see config_controls.h) -- no hand-numbered uids.
// SPEC docs/design/bst-module-spec.md : every element of the box has its own on / off ; the box's place, size and
// frame are the Pet module's (docs/design/pet-module-spec.md).
#include "ui/config_page.h"
#include "ui/config_controls.h"
#include "ui/config_rows.h"
#include "model/ui_config.h"
#include "model/bst_pets_gen.h"   // the preview pets
#include "gfx/font.h"
#include "gfx/draw.h"
#include <cstdio>

namespace aio {

// the Display rows of the BST tab
void ConfigPage::draw_bst_rows(u32 dev, Font* fo, const MouseState* mo, bool click,
                               float& ry, int& ri, float e,
                               float bandX, float bandW, float coX, float ctrlW) {
    UiConfig& c = ui_config();
    ROW_TOGGLE(CTRL_ID, tr("Show on BST", "Afficher en BST"), c.bstShow)
    { ROW_BAND(52.0f)   // where the pet goes : beside the Ready dial in the box, on top of the box, on top of its moves only
        const char* LL[3] = { tr("Side by side", "C\xC3\xB4te \xC3\xA0 c\xC3\xB4te"), tr("Pet on top", "Pet au-dessus"), tr("Pet + moves", "Pet + moves") };
        const int l = (c.bstLayout < 0 || c.bstLayout > 2) ? 0 : c.bstLayout;
        if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Layout", "Disposition"), LL[l])) { c.bstLayout = wrap(l + d, 3); save_ui_config(); }
    } ROW_NEXT(52.0f)
    ROW_TOGGLE(CTRL_ID, tr("Only with a pet out", "Seulement avec un pet sorti"), c.bstOnlyPet)
    { ROW_BAND(52.0f)   // Preview pet (edit / this page only) : every jug pet, its art and its moves
        const int pi = (c.bstDemo < 0 || c.bstDemo >= BST_PETS_N) ? 0 : c.bstDemo;
        if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Preview pet", "Pet (aper\xC3\xA7u)"), BST_PETS[pi].name)) { c.bstDemo = wrap(pi + d, BST_PETS_N); save_ui_config(); }
    } ROW_NEXT(52.0f)
    ROW_TOGGLE(CTRL_ID, tr("Pet image", "Image du pet"), c.bstImage)
    { ROW_BAND(52.0f)   // what that place shows : the art, the pet's correlation as text, or both
        const char* ML[3] = { tr("Image", "Image"), tr("Correlation", "Corr\xC3\xA9lation"), tr("Both", "Les deux") };
        const int am = (c.bstArtMode < 0 || c.bstArtMode > 2) ? 0 : c.bstArtMode;
        if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Shows", "Affiche"), ML[am])) { c.bstArtMode = wrap(am + d, 3); save_ui_config(); }
    } ROW_NEXT(52.0f)
    { ROW_BAND(46.0f)   // the pet art's size
        const float lo = 0.50f, hi = 2.00f; char b[16]; sprintf(b, "%d%%", (int)(c.bstArtSz * 100.0f + 0.5f));
        float v01 = (c.bstArtSz - lo) / (hi - lo); v01 = clampf(v01, 0.0f, 1.0f);
        if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Image size", "Taille de l'image"), b, &v01)) { float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f; c.bstArtSz = v < lo ? lo : (v > hi ? hi : v); }
    } ROW_NEXT(46.0f)
    { ROW_BAND(46.0f)   // the correlation icons' size
        const float lo = 0.50f, hi = 2.00f; char b[16]; sprintf(b, "%d%%", (int)(c.bstEcoSz * 100.0f + 0.5f));
        float v01 = (c.bstEcoSz - lo) / (hi - lo); v01 = clampf(v01, 0.0f, 1.0f);
        if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Correlation icons", "Ic\xC3\xB4nes de corr\xC3\xA9lation"), b, &v01)) { float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f; c.bstEcoSz = v < lo ? lo : (v > hi ? hi : v); }
    } ROW_NEXT(46.0f)
    ROW_TOGGLE(CTRL_ID, tr("Pet name", "Nom du pet"), c.bstName)
    ROW_TOGGLE(CTRL_ID, tr("HP", "HP"), c.bstHp)
    ROW_TOGGLE(CTRL_ID, tr("TP", "TP"), c.bstTp)
    ROW_TOGGLE(CTRL_ID, tr("Distance", "Distance"), c.bstDist)
    ROW_TOGGLE(CTRL_ID, tr("Ready charges", "Charges Ready"), c.bstCharges)
    { ROW_BAND(52.0f)   // how the charges are drawn : paws, the ring, bars
        const char* CS[3] = { tr("Paws", "Pattes"), tr("Ring", "Cercle"), tr("Bars", "Barres") };
        const int cs = (c.bstChargeStyle < 0 || c.bstChargeStyle > 2) ? 0 : c.bstChargeStyle;
        if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Charge style", "Style des charges"), CS[cs])) { c.bstChargeStyle = wrap(cs + d, 3); save_ui_config(); }
    } ROW_NEXT(52.0f)
    { ROW_BAND(46.0f)   // the Ready paws' size
        const float lo = 0.50f, hi = 2.00f; char b[16]; sprintf(b, "%d%%", (int)(c.bstPawSz * 100.0f + 0.5f));
        float v01 = (c.bstPawSz - lo) / (hi - lo); v01 = clampf(v01, 0.0f, 1.0f);
        if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Ready size", "Taille Ready"), b, &v01)) { float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f; c.bstPawSz = v < lo ? lo : (v > hi ? hi : v); }
    } ROW_NEXT(46.0f)
    ROW_TOGGLE(CTRL_ID, tr("Ready moves", "Ready moves"), c.bstMoves)
    ROW_TOGGLE(CTRL_ID, tr("Move type icons", "Ic\xC3\xB4nes de type"), c.bstIcons)   // weapon / element icon, else a colour tick
    { ROW_BAND(40.0f)   // note
        const float ty = ry + yo; fo->begin(dev);
        fo->draw_lc(dev, coX + snap(4.0f), ty + snap(16.0f), tr("Shown when BST is your main job.", "Affich\xC3\xA9 quand BST est ton job principal."), snap(12.0f), fa(C_MUTE), fa(C_STROKE), 1.0f);
    } ROW_NEXT(40.0f)
}

// the Text rows of the BST tab : which text element, then its typography
void ConfigPage::draw_bst_text_rows(u32 dev, Font* fo, const MouseState* mo, bool click,
                                    float& ry, int& ri, float e,
                                    float bandX, float bandW, float coX, float ctrlW) {
    UiConfig& c = ui_config();
    { ROW_BAND(52.0f)   // element selector
        const char* TLBL[BST_TE_COUNT] = { tr("Pet name", "Nom du pet"), tr("HP / TP / distance", "HP / TP / distance"), tr("Ready charges", "Charges Ready"), tr("Ready moves", "Ready moves") };
        int te = (cfgBstTextElem_ < 0 || cfgBstTextElem_ >= BST_TE_COUNT) ? 0 : cfgBstTextElem_;
        if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Element", "\xC3\x89l\xC3\xA9ment"), TLBL[te])) { cfgBstTextElem_ = wrap(te + d, BST_TE_COUNT); }
    }
    ROW_NEXT(52.0f)
    draw_text_style(dev, fo, mo, click, ry, ri, e, bandX, bandW, coX, ctrlW,
                    c.bstText[(cfgBstTextElem_ < 0 || cfgBstTextElem_ >= BST_TE_COUNT) ? 0 : cfgBstTextElem_]);
}

#undef ROW_BAND
#undef ROW_NEXT

} // namespace aio
