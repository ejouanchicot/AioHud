// bst_config.cpp -- the Beastmaster module's settings panel (//aio config -> "BST").
//
// Each control is keyed by CTRL_ID (file:line hash, see config_controls.h) -- no hand-numbered uids.
// Reuses the shared sub-section catOpen_ slots [6] (Display) / [5] (Text), like the other modules.
// SPEC docs/design/bst-module-spec.md : every element of the box has its own on / off.
#include "ui/config_page.h"
#include "ui/config_controls.h"
#include "ui/config_rows.h"
#include "model/ui_config.h"
#include "model/bst_pets_gen.h"   // the preview pets
#include "gfx/font.h"
#include "gfx/draw.h"
#include <cstdio>

namespace aio {

void ConfigPage::draw_bst_config(u32 dev, Font* fo, const MouseState* mo, bool click,
                                 float& ry, int& ri, float e,
                                 float bandX, float bandW, float coX, float ctrlW,
                                 float hdrX, float hdrW) {
    UiConfig& c = ui_config();

    // ===== sub-section : DISPLAY =====
    const float aF6_ = cat_fold(CTRL_ID, catOpen_[6]);
    cat_panel(dev, hdrX, ry, hdrW, cat_card_h(catH_[6], aF6_));
    if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Display", "Affichage"), catOpen_[6], aF6_)) catOpen_[6] = !catOpen_[6];
    ROW_NEXT(42.0f)
    if (aF6_ > 0.0f) {
        const float top6_ = ry;
        cat_fold_clip(dev, hdrX, top6_, hdrW, catH_[6] * aF6_);
        ROW_TOGGLE(CTRL_ID, tr("Show", "Afficher"), c.bstShow)
        ROW_TOGGLE(CTRL_ID, tr("Only with a pet out", "Seulement avec un pet sorti"), c.bstOnlyPet)
        { ROW_BAND(46.0f)   // Size
            const float lo = 0.50f, hi = 2.00f; char b[16]; sprintf(b, "%d%%", (int)(c.bstScale * 100.0f + 0.5f));
            float v01 = (c.bstScale - lo) / (hi - lo); v01 = clampf(v01, 0.0f, 1.0f);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Size", "Taille"), b, &v01)) { float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f; c.bstScale = v < lo ? lo : (v > hi ? hi : v); }
        } ROW_NEXT(46.0f)
        ROW_GROW(CTRL_ID, UiConfig::GB_BST)   // which way the box grows as its content widens (ui/box_grow.h)
        draw_box_appearance(dev, fo, mo, click, ry, ri, e, bandX, bandW, coX, ctrlW, c.bstBox);   // Box / Transparency / Theme / Hue / Luminosity
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
        ROW_TOGGLE(CTRL_ID, tr("Pet name", "Nom du pet"), c.bstName)
        ROW_TOGGLE(CTRL_ID, tr("HP", "HP"), c.bstHp)
        ROW_TOGGLE(CTRL_ID, tr("TP", "TP"), c.bstTp)
        ROW_TOGGLE(CTRL_ID, tr("Distance", "Distance"), c.bstDist)
        ROW_TOGGLE(CTRL_ID, tr("Ready charges", "Charges Ready"), c.bstCharges)
        ROW_TOGGLE(CTRL_ID, tr("Ready moves", "Ready moves"), c.bstMoves)
        ROW_TOGGLE(CTRL_ID, tr("Move type icons", "Ic\xC3\xB4nes de type"), c.bstIcons)   // weapon / element icon, else a colour tick
        { ROW_BAND(40.0f)   // note
            const float ty = ry + yo; fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + snap(16.0f), tr("Only appears when BST is your main job.", "Appara\xC3\xAet seulement si BST est ton job principal."), snap(12.0f), fa(C_MUTE), fa(C_STROKE), 1.0f);
        } ROW_NEXT(40.0f)
        cat_fold_end(dev, ry, top6_, catH_[6], aF6_);
    }   // end Display
    ry += snap(16.0f);

    // ===== sub-section : TEXT =====
    const float aF5_ = cat_fold(CTRL_ID, catOpen_[5]);
    cat_panel(dev, hdrX, ry, hdrW, cat_card_h(catH_[5], aF5_));
    if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Text", "Texte"), catOpen_[5], aF5_)) catOpen_[5] = !catOpen_[5];
    ROW_NEXT(42.0f)
    if (aF5_ > 0.0f) {
        const float top5_ = ry;
        cat_fold_clip(dev, hdrX, top5_, hdrW, catH_[5] * aF5_);
        { ROW_BAND(52.0f)   // element selector
            const char* TLBL[BST_TE_COUNT] = { tr("Pet name", "Nom du pet"), tr("HP / TP / distance", "HP / TP / distance"), tr("Ready charges", "Charges Ready"), tr("Ready moves", "Ready moves") };
            int te = (cfgBstTextElem_ < 0 || cfgBstTextElem_ >= BST_TE_COUNT) ? 0 : cfgBstTextElem_;
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Element", "\xC3\x89l\xC3\xA9ment"), TLBL[te])) { cfgBstTextElem_ = wrap(te + d, BST_TE_COUNT); }
        }
        ROW_NEXT(52.0f)
        draw_text_style(dev, fo, mo, click, ry, ri, e, bandX, bandW, coX, ctrlW,
                        c.bstText[(cfgBstTextElem_ < 0 || cfgBstTextElem_ >= BST_TE_COUNT) ? 0 : cfgBstTextElem_]);
        cat_fold_end(dev, ry, top5_, catH_[5], aF5_);
    }   // end Text

    ry += snap(16.0f);
    #undef ROW_BAND
    #undef ROW_NEXT
}

} // namespace aio
