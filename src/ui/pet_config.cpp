// pet_config.cpp -- the Pet module's settings page (//aio config -> "Pet") : what is common to the box (show, size,
// grow direction, frame), the job tab (BST, GEO, SMN ; PUP later), then that job's rows (bst_config.cpp,
// geo_config.cpp, smn_config.cpp). The tab also picks the sample the preview shows (UiConfig::petTab, hud_pet.cpp).
//
// Each control is keyed by CTRL_ID (file:line hash, see config_controls.h) -- no hand-numbered uids.
// Reuses the shared sub-section catOpen_ slots [6] (Display) / [5] (Text), like the other modules.
// SPEC docs/design/pet-module-spec.md.
#include "ui/config_page.h"
#include "ui/config_controls.h"
#include "ui/config_rows.h"
#include "ui/cfg_engine.h"            // the redesign : OptDef tables + the kit (draw_pet_v2)
#include "model/ui_config.h"
#include "model/bst_pets_gen.h"       // draw_pet_v2 : the BST preview pet
#include "model/smn_avatars_gen.h"    // draw_pet_v2 : the SMN preview avatar
#include "gfx/font.h"
#include "gfx/draw.h"
#include <cstdio>

namespace aio {

void ConfigPage::draw_pet_config(u32 dev, Font* fo, const MouseState* mo, bool click,
                                 float& ry, int& ri, float e,
                                 float bandX, float bandW, float coX, float ctrlW,
                                 float hdrX, float hdrW) {
    UiConfig& c = ui_config();
    const int tab = (c.petTab == 1 || c.petTab == 2) ? c.petTab : 0;

    // ===== sub-section : DISPLAY =====
    const float aF6_ = cat_fold(CTRL_ID, !ui_config().cfgAdv || (catOpen_[6]));
    cat_panel(dev, hdrX, ry, hdrW, cat_card_h(catH_[6], aF6_));
    if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Display", "Affichage"), catOpen_[6], aF6_) && ui_config().cfgAdv) catOpen_[6] = !catOpen_[6];
    ROW_NEXT(42.0f)
    if (aF6_ > 0.0f) {
        const float top6_ = ry;
        cat_fold_clip(dev, hdrX, top6_, hdrW, catH_[6] * aF6_);
        ROW_TOGGLE(CTRL_ID, tr("Show", "Afficher"), c.petShow)
        { ROW_BAND(46.0f)   // Size : the box's, whatever job fills it
            const float lo = 0.50f, hi = 2.00f; char b[16]; sprintf(b, "%d%%", (int)(c.petScale * 100.0f + 0.5f));
            float v01 = (c.petScale - lo) / (hi - lo); v01 = clampf(v01, 0.0f, 1.0f);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Size", "Taille"), b, &v01)) { float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f; c.petScale = v < lo ? lo : (v > hi ? hi : v); }
        } ROW_NEXT(46.0f)
        ROW_GROW(CTRL_ID, UiConfig::GB_PET)   // which way the box grows as its content widens (ui/box_grow.h)
        // the JOB : the variant row of the grammar's Display (like Chat's Box, Zone Tracker's Content) -- every row
        // below it, here and in Content / Text, is that job's. Then the job's own when / where rows.
        { ROW_BAND(52.0f)
            const char* JL[3] = { "BST", "GEO", "SMN" };
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Job", "Job"), JL[tab])) { c.petTab = wrap(tab + d, 3); save_ui_config(); }
        } ROW_NEXT(52.0f)
        if (tab == 2)      draw_smn_rows(dev, fo, mo, click, ry, ri, e, bandX, bandW, coX, ctrlW, 0);
        else if (tab == 1) draw_geo_rows(dev, fo, mo, click, ry, ri, e, bandX, bandW, coX, ctrlW, 0);
        else               draw_bst_rows(dev, fo, mo, click, ry, ri, e, bandX, bandW, coX, ctrlW, 0);
        cat_fold_end(dev, ry, top6_, catH_[6], aF6_);
    }   // end Display
    ry += snap(16.0f);
    // ===== sub-section : FRAME (the config grammar : Display / Frame / Content / Behaviour / Text, docs/architecture/config-panels.md) =====
if (ui_config().cfgAdv) {   // Advanced only (the config's depth, 2026-11-26)
    const float aF20_ = cat_fold(CTRL_ID, catOpen_[20]);
    cat_panel(dev, hdrX, ry, hdrW, cat_card_h(catH_[20], aF20_));
    if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Frame", "Cadre"), catOpen_[20], aF20_)) catOpen_[20] = !catOpen_[20];
    ROW_NEXT(42.0f)
    if (aF20_ > 0.0f) {
        const float top20_ = ry;
        cat_fold_clip(dev, hdrX, top20_, hdrW, catH_[20] * aF20_);
        draw_box_appearance(dev, fo, mo, click, ry, ri, e, bandX, bandW, coX, ctrlW, c.petBox);   // Box / Transparency / Theme / Hue / Luminosity
        cat_fold_end(dev, ry, top20_, catH_[20], aF20_);
    }   // end Frame
    ry += snap(16.0f);
}   // end Advanced only
    // ===== sub-section : CONTENT (the config grammar : Display / Frame / Content / Behaviour / Text, docs/architecture/config-panels.md) =====
    const float aF21_ = cat_fold(CTRL_ID, !ui_config().cfgAdv || (catOpen_[21]));
    cat_panel(dev, hdrX, ry, hdrW, cat_card_h(catH_[21], aF21_));
    if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Content", "Contenu"), catOpen_[21], aF21_) && ui_config().cfgAdv) catOpen_[21] = !catOpen_[21];
    ROW_NEXT(42.0f)
    if (aF21_ > 0.0f) {
        const float top21_ = ry;
        cat_fold_clip(dev, hdrX, top21_, hdrW, catH_[21] * aF21_);
        if (tab == 2)      draw_smn_rows(dev, fo, mo, click, ry, ri, e, bandX, bandW, coX, ctrlW, 1);   // what the job's box shows
        else if (tab == 1) draw_geo_rows(dev, fo, mo, click, ry, ri, e, bandX, bandW, coX, ctrlW, 1);
        else               draw_bst_rows(dev, fo, mo, click, ry, ri, e, bandX, bandW, coX, ctrlW, 1);
        cat_fold_end(dev, ry, top21_, catH_[21], aF21_);
    }   // end Content
    ry += snap(16.0f);

    // ===== sub-section : TEXT (the tab's text elements) =====
if (ui_config().cfgAdv) {   // Advanced only (the config's depth, 2026-11-26)
    const float aF5_ = cat_fold(CTRL_ID, catOpen_[5]);
    cat_panel(dev, hdrX, ry, hdrW, cat_card_h(catH_[5], aF5_));
    if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Text", "Texte"), catOpen_[5], aF5_)) catOpen_[5] = !catOpen_[5];
    ROW_NEXT(42.0f)
    if (aF5_ > 0.0f) {
        const float top5_ = ry;
        cat_fold_clip(dev, hdrX, top5_, hdrW, catH_[5] * aF5_);
        if (tab == 2)      draw_smn_text_rows(dev, fo, mo, click, ry, ri, e, bandX, bandW, coX, ctrlW);
        else if (tab == 1) draw_geo_text_rows(dev, fo, mo, click, ry, ri, e, bandX, bandW, coX, ctrlW);
        else               draw_bst_text_rows(dev, fo, mo, click, ry, ri, e, bandX, bandW, coX, ctrlW);
        cat_fold_end(dev, ry, top5_, catH_[5], aF5_);
    }   // end Text

    ry += snap(16.0f);
}   // end Advanced only
    if (!ui_config().cfgAdv) SIMPLE_MORE   // Simple : where the rest is
}

#undef ROW_BAND
#undef ROW_NEXT


// ======== THE NEW PET PAGE (the config redesign, milestone 3, 2026-11-26) ========
// SIMPLE : Display (the box's common rows, the job selector, then that job's when / where rows) / Content (the job's),
// from the *_DISPLAY / *_CONTENT tables below (their OL_SIMPLE rows). ADVANCED (2026-11-27) : the *_A_* tables further
// down -- the box's Display and Frame, one BST | GEO | SMN switch, that job's sections by element, its Text.
#define F_(name) [](UiConfig& c) -> void* { return &c.name; }
static const char* const BST_LY_EN[3] = { "Side by side", "Pet on top", "Pet + moves" };
static const char* const BST_LY_FR[3] = { "C\xC3\xB4te \xC3\xA0 c\xC3\xB4te", "Pet au-dessus", "Pet + moves" };
static const char* const BST_AM_EN[3] = { "Image", "Correlation", "Both" },     * const BST_AM_FR[3] = { "Image", "Corr\xC3\xA9lation", "Les deux" };
static const char* const BST_CS_EN[3] = { "Paws", "Ring", "Bars" },             * const BST_CS_FR[3] = { "Pattes", "Cercle", "Barres" };
static const char* const GEO_LY_EN[3] = { "Compass on top", "Side by side", "Bubble only" };
static const char* const GEO_LY_FR[3] = { "Boussole au-dessus", "C\xC3\xB4te \xC3\xA0 c\xC3\xB4te", "Bulle seule" };
static const char* const GEO_JS_EN[3] = { "Icons", "Icons + names", "Names" },  * const GEO_JS_FR[3] = { "Ic\xC3\xB4nes", "Ic\xC3\xB4nes + noms", "Noms" };
static const char* const SMN_LY_EN[2] = { "Portrait on top", "Side by side" },  * const SMN_LY_FR[2] = { "Portrait au-dessus", "C\xC3\xB4te \xC3\xA0 c\xC3\xB4te" };
static const char* pt_image(const UiConfig& c)   { return c.bstImage ? nullptr : tr("inactive : pet image off", "inactif : image du pet coup\xC3\xA9""e"); }
static const char* pt_artsz(const UiConfig& c)   { if (const char* w = pt_image(c)) return w; return c.bstArtMode != 1 ? nullptr : tr("inactive : correlation only", "inactif : corr\xC3\xA9lation seule"); }
static const char* pt_ecosz(const UiConfig& c)   { if (const char* w = pt_image(c)) return w; return c.bstArtMode != 0 ? nullptr : tr("inactive : image only", "inactif : image seule"); }
static const char* pt_charges(const UiConfig& c) { return c.bstCharges ? nullptr : tr("inactive : Ready charges off", "inactif : charges Ready coup\xC3\xA9""es"); }
static const char* pt_moves(const UiConfig& c)   { return c.bstMoves ? nullptr : tr("inactive : Ready moves off", "inactif : Ready moves coup\xC3\xA9s"); }
static const char* pt_geoja(const UiConfig& c)   { return c.geoJa ? nullptr : tr("inactive : job abilities off", "inactif : job abilities coup\xC3\xA9""es"); }
static const char* pt_compass(const UiConfig& c) { return c.geoCompass ? nullptr : tr("inactive : compass off", "inactif : boussole coup\xC3\xA9""e"); }

static const OptDef PET_DISPLAY[] = {   // the box's, whatever job fills it
    { "Show", "Afficher", OK_TOGGLE, OL_SIMPLE, F_(petShow), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Size", "Taille", OK_PCT, OL_SIMPLE, F_(petScale), 0.5f, 2.0f, nullptr, nullptr, 0, nullptr, 0 },
    { "Grows toward", "Grandit vers", OK_GROW, OL_SIMPLE, nullptr, 0, 0, nullptr, nullptr, 0, nullptr, UiConfig::GB_PET },
};
// ---- BST ----
static const OptDef BST_DISPLAY[] = {   // (then the preview pet, a dynamic list : Advanced)
    { "Show on BST", "Afficher en BST", OK_TOGGLE, OL_SIMPLE, F_(bstShow), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Layout", "Disposition", OK_SELECT, OL_SIMPLE, F_(bstLayout), 0, 0, BST_LY_EN, BST_LY_FR, 3, nullptr, 0 },
    { "Only with a pet out", "Seulement avec un pet sorti", OK_TOGGLE, OL_SIMPLE, F_(bstOnlyPet), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
};
static const OptDef BST_CONTENT[] = {
    { "Pet image", "Image du pet", OK_TOGGLE, OL_SIMPLE, F_(bstImage), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Shows", "Affiche", OK_SELECT, OL_ADV, F_(bstArtMode), 0, 0, BST_AM_EN, BST_AM_FR, 3, pt_image, 0 },
    { "Image size", "Taille de l'image", OK_PCT, OL_ADV, F_(bstArtSz), 0.5f, 2.0f, nullptr, nullptr, 0, pt_artsz, 0 },
    { "Correlation icons", "Ic\xC3\xB4nes de corr\xC3\xA9lation", OK_PCT, OL_ADV, F_(bstEcoSz), 0.5f, 2.0f, nullptr, nullptr, 0, pt_ecosz, 0 },
    { "Pet name", "Nom du pet", OK_TOGGLE, OL_SIMPLE, F_(bstName), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "HP", "HP", OK_TOGGLE, OL_SIMPLE, F_(bstHp), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "TP", "TP", OK_TOGGLE, OL_SIMPLE, F_(bstTp), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Distance", "Distance", OK_TOGGLE, OL_SIMPLE, F_(bstDist), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Ready charges", "Charges Ready", OK_TOGGLE, OL_SIMPLE, F_(bstCharges), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Charge style", "Style des charges", OK_SELECT, OL_ADV, F_(bstChargeStyle), 0, 0, BST_CS_EN, BST_CS_FR, 3, pt_charges, 0 },
    { "Ready size", "Taille Ready", OK_PCT, OL_ADV, F_(bstPawSz), 0.5f, 2.0f, nullptr, nullptr, 0, pt_charges, 0 },
    { "Ready moves", "Ready moves", OK_TOGGLE, OL_SIMPLE, F_(bstMoves), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Moves' skillchain", "Skillchain des moves", OK_TOGGLE, OL_ADV, F_(bstSc), 0, 0, nullptr, nullptr, 0, pt_moves, 0 },
    { "Job abilities", "Job abilities", OK_TOGGLE, OL_SIMPLE, F_(bstJa), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Move type icons", "Ic\xC3\xB4nes de type", OK_TOGGLE, OL_ADV, F_(bstIcons), 0, 0, nullptr, nullptr, 0, pt_moves, 0 },
};
// ---- GEO ----
static const OptDef GEO_DISPLAY[] = {
    { "Show on GEO", "Afficher en GEO", OK_TOGGLE, OL_SIMPLE, F_(geoShow), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Only with a luopan out", "Seulement avec un luopan", OK_TOGGLE, OL_SIMPLE, F_(geoOnlyLuopan), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Layout", "Disposition", OK_SELECT, OL_SIMPLE, F_(geoLayout), 0, 0, GEO_LY_EN, GEO_LY_FR, 3, nullptr, 0 },
};
static const OptDef GEO_CONTENT[] = {
    { "Icon / luopan size", "Taille ic\xC3\xB4nes / luopan", OK_PCT, OL_ADV, F_(geoIconSz), 0.5f, 2.0f, nullptr, nullptr, 0, nullptr, 0 },
    { "Bubble name", "Nom de la bulle", OK_TOGGLE, OL_SIMPLE, F_(geoBubble), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Luopan HP", "HP du luopan", OK_TOGGLE, OL_SIMPLE, F_(geoHp), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Estimated time left", "Temps estim\xC3\xA9 du luopan", OK_TOGGLE, OL_SIMPLE, F_(geoTime), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Distance to the luopan", "Distance au luopan", OK_TOGGLE, OL_SIMPLE, F_(geoDist), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Aura (Indi-)", "Aura (Indi-)", OK_TOGGLE, OL_SIMPLE, F_(geoAura), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Entrust", "Entrust", OK_TOGGLE, OL_SIMPLE, F_(geoEntrust), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "MP", "MP", OK_TOGGLE, OL_SIMPLE, F_(geoMp), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Job abilities", "Job abilities", OK_TOGGLE, OL_SIMPLE, F_(geoJa), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Abilities as", "Job abilities en", OK_SELECT, OL_ADV, F_(geoJaStyle), 0, 0, GEO_JS_EN, GEO_JS_FR, 3, pt_geoja, 0 },
    { "Cardinal Chant compass", "Boussole Cardinal Chant", OK_TOGGLE, OL_SIMPLE, F_(geoCompass), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Cardinal bonus (MAB, M.Acc...)", "Bonus Cardinal (MAB, M.Acc...)", OK_TOGGLE, OL_SIMPLE, F_(geoCardText), 0, 0, nullptr, nullptr, 0, pt_compass, 0 },
};
// ---- SMN ----
static const OptDef SMN_DISPLAY[] = {   // (then the preview avatar, a dynamic list : Advanced)
    { "Show on SMN", "Afficher en SMN", OK_TOGGLE, OL_SIMPLE, F_(smnShow), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Only with an avatar out", "Seulement avec un avatar", OK_TOGGLE, OL_SIMPLE, F_(smnOnlyPet), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Layout", "Disposition", OK_SELECT, OL_SIMPLE, F_(smnLayout), 0, 0, SMN_LY_EN, SMN_LY_FR, 2, nullptr, 0 },
};
static const OptDef SMN_CONTENT[] = {
    { "Portrait / icon size", "Taille portrait / ic\xC3\xB4nes", OK_PCT, OL_ADV, F_(smnIconSz), 0.5f, 2.0f, nullptr, nullptr, 0, nullptr, 0 },
    { "Avatar portrait", "Portrait de l'avatar", OK_TOGGLE, OL_SIMPLE, F_(smnPortrait), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Avatar name", "Nom de l'avatar", OK_TOGGLE, OL_SIMPLE, F_(smnName), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Avatar HP", "HP de l'avatar", OK_TOGGLE, OL_SIMPLE, F_(smnHp), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Avatar TP", "TP de l'avatar", OK_TOGGLE, OL_SIMPLE, F_(smnTp), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Distance to the avatar", "Distance \xC3\xA0 l'avatar", OK_TOGGLE, OL_SIMPLE, F_(smnDist), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Blood Pact Rage / Ward", "Blood Pact Rage / Ward", OK_TOGGLE, OL_SIMPLE, F_(smnBp), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Job abilities", "Job abilities", OK_TOGGLE, OL_SIMPLE, F_(smnJa), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "MP", "MP", OK_TOGGLE, OL_SIMPLE, F_(smnMp), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "The avatar's pacts", "Les pacts de l'avatar", OK_TOGGLE, OL_SIMPLE, F_(smnPacts), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
};
// ---- ADVANCED (2026-11-27 restructure) : the box's rows, its frame, then ONE job switch (BST | GEO | SMN) and that job's
//      sections by ELEMENT ; every "on + how" pair is one choice (draw_pet_v2's `choice`), so these tables hold the rest.
//      The greys below are read from the renderers (hud_bst.cpp / hud_geo.cpp) : what a layout does not draw. ----
static const char* pt_box(const UiConfig& c)     { return c.petShow ? nullptr : tr("inactive : the pet box is hidden", "inactif : la bo\xC3\xAEte des pets est masqu\xC3\xA9""e"); }
static const char* pt_bl2(const UiConfig& c)     { return c.bstLayout != 2 ? nullptr : tr("inactive : not drawn in the Pet + moves layout", "inactif : absent de la disposition Pet + moves"); }
static const char* pt_bub(const UiConfig& c)     { return c.geoLayout != 2 ? nullptr : tr("inactive : not drawn in the Bubble only layout", "inactif : absent de la disposition Bulle seule"); }
static const OptDef BST_A_LAYOUT[] = { { "Layout", "Disposition", OK_SELECT, OL_ADV, F_(bstLayout), 0, 0, BST_LY_EN, BST_LY_FR, 3, nullptr, 0 } };
static const OptDef BST_A_ARTSZ[]  = { { "Image size", "Taille de l'image", OK_PCT, OL_ADV, F_(bstArtSz), 0.5f, 2.0f, nullptr, nullptr, 0, nullptr, 0 } };   // shown with an image
static const OptDef BST_A_ECOSZ[]  = { { "Correlation icons", "Ic\xC3\xB4nes de corr\xC3\xA9lation", OK_PCT, OL_ADV, F_(bstEcoSz), 0.5f, 2.0f, nullptr, nullptr, 0, pt_bl2, 0 } };   // shown with the correlation
static const OptDef BST_A_PET[] = {
    { "Pet name", "Nom du pet", OK_TOGGLE, OL_ADV, F_(bstName), 0, 0, nullptr, nullptr, 0, pt_bl2, 0 },
    { "HP", "HP", OK_TOGGLE, OL_ADV, F_(bstHp), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "TP", "TP", OK_TOGGLE, OL_ADV, F_(bstTp), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Distance", "Distance", OK_TOGGLE, OL_ADV, F_(bstDist), 0, 0, nullptr, nullptr, 0, pt_bl2, 0 },
};
static const OptDef BST_A_READY[] = {   // under the Ready charges choice
    { "Ready size", "Taille Ready", OK_PCT, OL_ADV, F_(bstPawSz), 0.5f, 2.0f, nullptr, nullptr, 0, pt_charges, 0 },
    { "Ready moves", "Ready moves", OK_TOGGLE, OL_ADV, F_(bstMoves), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Moves' skillchain", "Skillchain des moves", OK_TOGGLE, OL_ADV, F_(bstSc), 0, 0, nullptr, nullptr, 0, pt_moves, 0 },
    { "Move type icons", "Ic\xC3\xB4nes de type", OK_TOGGLE, OL_ADV, F_(bstIcons), 0, 0, nullptr, nullptr, 0, pt_moves, 0 },
    { "Job abilities", "Job abilities", OK_TOGGLE, OL_ADV, F_(bstJa), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
};
static const OptDef GEO_A_LAYOUT[] = { { "Layout", "Disposition", OK_SELECT, OL_ADV, F_(geoLayout), 0, 0, GEO_LY_EN, GEO_LY_FR, 3, nullptr, 0 } };
static const OptDef GEO_A_LUOPAN[] = {   // then the compass choice
    { "Icon / luopan size", "Taille ic\xC3\xB4nes / luopan", OK_PCT, OL_ADV, F_(geoIconSz), 0.5f, 2.0f, nullptr, nullptr, 0, nullptr, 0 },
    { "Bubble name", "Nom de la bulle", OK_TOGGLE, OL_ADV, F_(geoBubble), 0, 0, nullptr, nullptr, 0, pt_bub, 0 },
    { "Luopan HP", "HP du luopan", OK_TOGGLE, OL_ADV, F_(geoHp), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Estimated time left", "Temps estim\xC3\xA9 du luopan", OK_TOGGLE, OL_ADV, F_(geoTime), 0, 0, nullptr, nullptr, 0, pt_bub, 0 },
    { "Distance to the luopan", "Distance au luopan", OK_TOGGLE, OL_ADV, F_(geoDist), 0, 0, nullptr, nullptr, 0, pt_bub, 0 },
};
static const OptDef GEO_A_SELF[] = {   // then the job abilities choice
    { "Aura (Indi-)", "Aura (Indi-)", OK_TOGGLE, OL_ADV, F_(geoAura), 0, 0, nullptr, nullptr, 0, pt_bub, 0 },
    { "Entrust", "Entrust", OK_TOGGLE, OL_ADV, F_(geoEntrust), 0, 0, nullptr, nullptr, 0, pt_bub, 0 },
    { "MP", "MP", OK_TOGGLE, OL_ADV, F_(geoMp), 0, 0, nullptr, nullptr, 0, pt_bub, 0 },
};
static const OptDef SMN_A_LAYOUT[] = { { "Layout", "Disposition", OK_SELECT, OL_ADV, F_(smnLayout), 0, 0, SMN_LY_EN, SMN_LY_FR, 2, nullptr, 0 } };
static const OptDef SMN_A_AVATAR[] = {
    { "Portrait / icon size", "Taille portrait / ic\xC3\xB4nes", OK_PCT, OL_ADV, F_(smnIconSz), 0.5f, 2.0f, nullptr, nullptr, 0, nullptr, 0 },
    { "Avatar portrait", "Portrait de l'avatar", OK_TOGGLE, OL_ADV, F_(smnPortrait), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Avatar name", "Nom de l'avatar", OK_TOGGLE, OL_ADV, F_(smnName), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Avatar HP", "HP de l'avatar", OK_TOGGLE, OL_ADV, F_(smnHp), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Avatar TP", "TP de l'avatar", OK_TOGGLE, OL_ADV, F_(smnTp), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Distance to the avatar", "Distance \xC3\xA0 l'avatar", OK_TOGGLE, OL_ADV, F_(smnDist), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
};
static const OptDef SMN_A_ABIL[] = {
    { "The avatar's pacts", "Les pacts de l'avatar", OK_TOGGLE, OL_ADV, F_(smnPacts), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Blood Pact Rage / Ward", "Blood Pact Rage / Ward", OK_TOGGLE, OL_ADV, F_(smnBp), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Job abilities", "Job abilities", OK_TOGGLE, OL_ADV, F_(smnJa), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "MP", "MP", OK_TOGGLE, OL_ADV, F_(smnMp), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
};
static const char* const PT_WHEN_BST_EN[2] = { "Always", "Only with a pet out" },      * const PT_WHEN_BST_FR[2] = { "Toujours", "Seulement avec un pet sorti" };
static const char* const PT_WHEN_GEO_EN[2] = { "Always", "Only with a luopan out" },   * const PT_WHEN_GEO_FR[2] = { "Toujours", "Seulement avec un luopan" };
static const char* const PT_WHEN_SMN_EN[2] = { "Always", "Only with an avatar out" },  * const PT_WHEN_SMN_FR[2] = { "Toujours", "Seulement avec un avatar" };
static const char* const PT_PIC_EN[3] = { "Image", "Correlation", "Image + correlation" }, * const PT_PIC_FR[3] = { "Image", "Corr\xC3\xA9lation", "Image + corr\xC3\xA9lation" };
static const char* const PT_CMP_EN[2] = { "Compass only", "Compass + bonus (MAB, M.Acc...)" }, * const PT_CMP_FR[2] = { "Boussole seule", "Boussole + bonus (MAB, M.Acc...)" };
#undef F_
#define NOPT(a) ((int)(sizeof(a) / sizeof((a)[0])))

void ConfigPage::draw_pet_v2(u32 dev, Font* fo, const MouseState* mo, bool click,
                             float& ry, int& ri, float e,
                             float bandX, float bandW, float coX, float ctrlW,
                             float hdrX, float hdrW) {
    Kit K;
    if (!v2_kit(K, mo, click)) { draw_pet_config(dev, fo, mo, click, ry, ri, e, bandX, bandW, coX, ctrlW, hdrX, hdrW); return; }
    g_fade = e;
    UiConfig& c = ui_config();
    const bool adv = c.cfgAdv != 0;
    const float x = coX, w = ctrlW;
    const int base = CTRL_ID;
    const int tab = (c.petTab == 1 || c.petTab == 2) ? c.petTab : 0;
    const OptDef* jd = tab == 2 ? SMN_DISPLAY : (tab == 1 ? GEO_DISPLAY : BST_DISPLAY);
    const int     jdN = tab == 2 ? NOPT(SMN_DISPLAY) : (tab == 1 ? NOPT(GEO_DISPLAY) : NOPT(BST_DISPLAY));
    const OptDef* jc = tab == 2 ? SMN_CONTENT : (tab == 1 ? GEO_CONTENT : BST_CONTENT);
    const int     jcN = tab == 2 ? NOPT(SMN_CONTENT) : (tab == 1 ? NOPT(GEO_CONTENT) : NOPT(BST_CONTENT));
    ry += K.px(6.0f);
    if (!adv) {   // SIMPLE : the box's rows, the job, that job's when / where rows ; then what its box shows (unchanged)
        const int nD = cfg_count_modified(PET_DISPLAY, NOPT(PET_DISPLAY), false) + cfg_count_modified(jd, jdN, false);
        if (v2_section(K, ctrl_uid_i(base, 1), 6, "Display", "Affichage", x, ry, w, nD)) {
            cfg_rows(K, ctrl_uid_i(base, 2), PET_DISPLAY, NOPT(PET_DISPLAY), x, ry, w, false);
            {   // the JOB : every row below it, here and in Content, is that job's. A view choice : no marker, no reset
                static const char* const JL[3] = { "BST", "GEO", "SMN" };
                if (int d = cfg_select_row(K, ctrl_uid_i(base, 3), x, ry, w, tr("Job", "Job"), JL[tab], nullptr, nullptr, 0)) { c.petTab = wrap(tab + d, 3); save_ui_config(); }
            }
            cfg_rows(K, ctrl_uid_i(base, 4), jd, jdN, x, ry, w, false);
            v2_end(K, ry);
        }
        if (v2_section(K, ctrl_uid_i(base, 9), 21, "Content", "Contenu", x, ry, w, cfg_count_modified(jc, jcN, false)))
            v2_rows(K, ctrl_uid_i(base, 10), jc, jcN, x, ry, w);
        v2_simple_note(K, x, ry);
        g_fade = e;
        return;
    }
    // ADVANCED (2026-11-27 restructure) : Display and Frame are the BOX's (every job) ; then ONE switch BST | GEO | SMN
    // (it is also the preview's sample) and that job's sections by element ; then its Text. Every "on + how" pair is one
    // choice (Off / mode...), as on the Target page.
    static const UiConfig D{};
    auto choice = [&](int uid, const char* en, const char* frl, int& on, int& mode, const char* const* men, const char* const* mfr, int n, const char* why, int dOn) {
        const char* lab[8]; lab[0] = tr("Off", "D\xC3\xA9sactiv\xC3\xA9"); for (int k = 0; k < n && k < 7; ++k) lab[k + 1] = tr(men[k], mfr[k]);
        int v = on ? 1 + (mode < 0 || mode >= n ? 0 : mode) : 0;
        void* mark = (on != dOn) ? (void*)&on : (void*)&mode;   // the marker / right-click reset : the field that differs
        if (int d = cfg_select_row(K, uid, x, ry, w, tr(en, frl), lab[v], why, mark, 4)) {
            v = wrap(v + d, n + 1); on = v > 0 ? 1 : 0; if (v > 0) mode = v - 1; save_ui_config(); }
    };
    auto diff2 = [&](int on, int mode, int dOn, int dMode) { return (on != dOn) || (on && mode != dMode) ? 1 : 0; };
    auto cnt = [&](const OptDef* d, int n) { return cfg_count_modified(d, n, true); };
    if (v2_section(K, ctrl_uid_i(base, 20), 6, "Display (every job)", "Affichage (tous les jobs)", x, ry, w, cnt(PET_DISPLAY, NOPT(PET_DISPLAY))))
        v2_rows(K, ctrl_uid_i(base, 21), PET_DISPLAY, NOPT(PET_DISPLAY), x, ry, w);
    {   BoxStyle& bs = c.petBox;
        const FrameRefs fr = { &bs.on, &bs.border, &bs.alpha, &bs.alphaOwn, &bs.themeCopy, &bs.theme, &bs.hue, &bs.lum };
        if (v2_section(K, ctrl_uid_i(base, 22), 20, "Frame (every job)", "Cadre (tous les jobs)", x, ry, w, cfg_frame_modified(fr))) { cfg_frame_rows(K, ctrl_uid_i(base, 23), x, ry, w, fr); v2_end(K, ry); }
    }
    {   // THE JOB : one switch, the sections below are that job's (a view choice : no marker, no reset ; saved, the preview follows it)
        static const char* const JL[3] = { "BST", "GEO", "SMN" };
        const float segW = K.px(130.0f), sh = K.px(36.0f);
        const int hit = chrome_segment(K, ctrl_uid_i(base, 24), x + (w - 3.0f * segW) * 0.5f, ry + K.px(4.0f), segW, sh, JL, 3, tab);
        if (hit >= 0 && hit != tab) { c.petTab = hit; save_ui_config(); }
        ry += sh + K.px(16.0f);
    }
    const int T = (c.petTab == 1 || c.petTab == 2) ? c.petTab : 0;   // this frame's click applied at once
    if (T == 0) {   // ---- BST ----
        if (v2_section(K, ctrl_uid_i(base, 30), 21, "When and layout", "Quand et disposition", x, ry, w, diff2(c.bstShow, c.bstOnlyPet, D.bstShow, D.bstOnlyPet) + cnt(BST_A_LAYOUT, 1))) {
            choice(ctrl_uid_i(base, 31), "Shown on BST", "Affich\xC3\xA9""e en BST", c.bstShow, c.bstOnlyPet, PT_WHEN_BST_EN, PT_WHEN_BST_FR, 2, pt_box(c), D.bstShow);
            v2_rows(K, ctrl_uid_i(base, 32), BST_A_LAYOUT, 1, x, ry, w);
        }
        {   const bool img = c.bstImage && c.bstArtMode != 1, cor = c.bstImage && c.bstArtMode != 0;
            const int nMod = cfg_count_modified_bytes(&c.bstDemo, 4) + diff2(c.bstImage, c.bstArtMode, D.bstImage, D.bstArtMode)
                           + (img ? cnt(BST_A_ARTSZ, 1) : 0) + (cor ? cnt(BST_A_ECOSZ, 1) : 0) + cnt(BST_A_PET, NOPT(BST_A_PET));
            if (v2_section(K, ctrl_uid_i(base, 33), 7, "Pet", "Pet", x, ry, w, nMod)) {
                {   // Preview pet (edit / this page only) : every jug pet, its art and its moves
                    const int pi = (c.bstDemo < 0 || c.bstDemo >= BST_PETS_N) ? 0 : c.bstDemo;
                    if (int d = cfg_select_row(K, ctrl_uid_i(base, 34), x, ry, w, tr("Preview pet", "Pet (aper\xC3\xA7u)"), BST_PETS[pi].name, nullptr, &c.bstDemo, 4)) { c.bstDemo = wrap(pi + d, BST_PETS_N); save_ui_config(); }
                }
                choice(ctrl_uid_i(base, 35), "Pet picture", "Image du pet", c.bstImage, c.bstArtMode, PT_PIC_EN, PT_PIC_FR, 3, nullptr, D.bstImage);
                if (img) cfg_rows(K, ctrl_uid_i(base, 36), BST_A_ARTSZ, 1, x, ry, w, true);   // each size only with what it sizes
                if (cor) cfg_rows(K, ctrl_uid_i(base, 37), BST_A_ECOSZ, 1, x, ry, w, true);
                v2_rows(K, ctrl_uid_i(base, 38), BST_A_PET, NOPT(BST_A_PET), x, ry, w);
            }
        }
        if (v2_section(K, ctrl_uid_i(base, 39), 8, "Ready and abilities", "Ready et abilities", x, ry, w, diff2(c.bstCharges, c.bstChargeStyle, D.bstCharges, D.bstChargeStyle) + cnt(BST_A_READY, NOPT(BST_A_READY)))) {
            choice(ctrl_uid_i(base, 40), "Ready charges", "Charges Ready", c.bstCharges, c.bstChargeStyle, BST_CS_EN, BST_CS_FR, 3, nullptr, D.bstCharges);
            v2_rows(K, ctrl_uid_i(base, 41), BST_A_READY, NOPT(BST_A_READY), x, ry, w);
        }
    } else if (T == 1) {   // ---- GEO ----
        if (v2_section(K, ctrl_uid_i(base, 50), 21, "When and layout", "Quand et disposition", x, ry, w, diff2(c.geoShow, c.geoOnlyLuopan, D.geoShow, D.geoOnlyLuopan) + cnt(GEO_A_LAYOUT, 1))) {
            choice(ctrl_uid_i(base, 51), "Shown on GEO", "Affich\xC3\xA9""e en GEO", c.geoShow, c.geoOnlyLuopan, PT_WHEN_GEO_EN, PT_WHEN_GEO_FR, 2, pt_box(c), D.geoShow);
            v2_rows(K, ctrl_uid_i(base, 52), GEO_A_LAYOUT, 1, x, ry, w);
        }
        if (v2_section(K, ctrl_uid_i(base, 53), 7, "Luopan", "Luopan", x, ry, w, cnt(GEO_A_LUOPAN, NOPT(GEO_A_LUOPAN)) + diff2(c.geoCompass, c.geoCardText, D.geoCompass, D.geoCardText))) {
            cfg_rows(K, ctrl_uid_i(base, 54), GEO_A_LUOPAN, NOPT(GEO_A_LUOPAN), x, ry, w, true);
            choice(ctrl_uid_i(base, 55), "Cardinal Chant compass", "Boussole Cardinal Chant", c.geoCompass, c.geoCardText, PT_CMP_EN, PT_CMP_FR, 2, nullptr, D.geoCompass);
            v2_end(K, ry);
        }
        if (v2_section(K, ctrl_uid_i(base, 56), 8, "Auras, MP and abilities", "Auras, MP et abilities", x, ry, w, cnt(GEO_A_SELF, NOPT(GEO_A_SELF)) + diff2(c.geoJa, c.geoJaStyle, D.geoJa, D.geoJaStyle))) {
            cfg_rows(K, ctrl_uid_i(base, 57), GEO_A_SELF, NOPT(GEO_A_SELF), x, ry, w, true);
            choice(ctrl_uid_i(base, 58), "Job abilities", "Job abilities", c.geoJa, c.geoJaStyle, GEO_JS_EN, GEO_JS_FR, 3, pt_bub(c), D.geoJa);
            v2_end(K, ry);
        }
    } else {   // ---- SMN ----
        if (v2_section(K, ctrl_uid_i(base, 70), 21, "When and layout", "Quand et disposition", x, ry, w, diff2(c.smnShow, c.smnOnlyPet, D.smnShow, D.smnOnlyPet) + cnt(SMN_A_LAYOUT, 1))) {
            choice(ctrl_uid_i(base, 71), "Shown on SMN", "Affich\xC3\xA9""e en SMN", c.smnShow, c.smnOnlyPet, PT_WHEN_SMN_EN, PT_WHEN_SMN_FR, 2, pt_box(c), D.smnShow);
            v2_rows(K, ctrl_uid_i(base, 72), SMN_A_LAYOUT, 1, x, ry, w);
        }
        if (v2_section(K, ctrl_uid_i(base, 73), 7, "Avatar", "Avatar", x, ry, w, cfg_count_modified_bytes(&c.smnDemo, 4) + cnt(SMN_A_AVATAR, NOPT(SMN_A_AVATAR)))) {
            {   // Preview avatar : every picture and pact list can be checked
                const int ai = (c.smnDemo < 0 || c.smnDemo >= SMN_AVATARS_N) ? 0 : c.smnDemo;
                if (int d = cfg_select_row(K, ctrl_uid_i(base, 74), x, ry, w, tr("Preview avatar", "Avatar (aper\xC3\xA7u)"), SMN_AVATARS[ai].en, nullptr, &c.smnDemo, 4)) { c.smnDemo = wrap(ai + d, SMN_AVATARS_N); save_ui_config(); }
            }
            v2_rows(K, ctrl_uid_i(base, 75), SMN_A_AVATAR, NOPT(SMN_A_AVATAR), x, ry, w);
        }
        if (v2_section(K, ctrl_uid_i(base, 76), 8, "Pacts, abilities and MP", "Pacts, abilities et MP", x, ry, w, cnt(SMN_A_ABIL, NOPT(SMN_A_ABIL))))
            v2_rows(K, ctrl_uid_i(base, 77), SMN_A_ABIL, NOPT(SMN_A_ABIL), x, ry, w);
    }
    {   // TEXT : that job's text elements (T : the switch's job, this frame's click included)
        const char* lab[4]; TextStyle* ts[4]; int n = 0; int* sel = &cfgBstTextElem_;
        if (T == 2) {
            const char* l[SMN_TE_COUNT] = { tr("Avatar name", "Nom de l'avatar"), tr("HP / TP / MP / distance", "HP / TP / MP / distance"), tr("Job abilities", "Job abilities"), tr("Pacts", "Pacts") };
            n = SMN_TE_COUNT; for (int i = 0; i < n; ++i) { lab[i] = l[i]; ts[i] = &c.smnText[i]; } sel = &cfgSmnTextElem_;
        } else if (T == 1) {
            const char* l[GEO_TE_COUNT] = { tr("Bubble / aura names", "Noms bulle / aura"), tr("HP / MP / times / distance", "HP / MP / temps / distance"), tr("Job abilities", "Job abilities") };
            n = GEO_TE_COUNT; for (int i = 0; i < n; ++i) { lab[i] = l[i]; ts[i] = &c.geoText[i]; } sel = &cfgGeoTextElem_;
        } else {
            const char* l[BST_TE_COUNT] = { tr("Pet name", "Nom du pet"), tr("HP / TP / distance", "HP / TP / distance"), tr("Ready charges", "Charges Ready"), tr("Ready moves", "Ready moves") };
            n = BST_TE_COUNT; for (int i = 0; i < n; ++i) { lab[i] = l[i]; ts[i] = &c.bstText[i]; }
        }
        if (v2_section(K, ctrl_uid_i(base, 90), 5, "Text", "Texte", x, ry, w, cfg_text_modified(ts, n))) { cfg_text_rows(K, ctrl_uid_i(base, 91), x, ry, w, lab, ts, n, sel); v2_end(K, ry); }
    }
    g_fade = e;
}
#undef NOPT

} // namespace aio
