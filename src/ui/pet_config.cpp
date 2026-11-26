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
// Display (the box's common rows, the job selector, then that job's when / where rows) / Frame / Content (the job's) /
// Text (the job's elements), like the old page. The job tabs' rows (bst/geo/smn_config.cpp on the old page) are
// described here ; a row the old page hid behind an element being off is greyed with its reason.
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
    int* demo = tab == 2 ? &c.smnDemo : (tab == 1 ? nullptr : &c.bstDemo);   // the preview's sample (GEO has none)
    ry += K.px(6.0f);
    const int nD = cfg_count_modified(PET_DISPLAY, NOPT(PET_DISPLAY), adv) + cfg_count_modified(jd, jdN, adv) + ((adv && demo) ? cfg_count_modified_bytes(demo, 4) : 0);
    if (v2_section(K, ctrl_uid_i(base, 1), 6, "Display", "Affichage", x, ry, w, nD)) {
        cfg_rows(K, ctrl_uid_i(base, 2), PET_DISPLAY, NOPT(PET_DISPLAY), x, ry, w, adv);
        {   // the JOB : every row below it, here and in Content / Text, is that job's. A view choice : no marker, no reset
            static const char* const JL[3] = { "BST", "GEO", "SMN" };
            if (int d = cfg_select_row(K, ctrl_uid_i(base, 3), x, ry, w, tr("Job", "Job"), JL[tab], nullptr, nullptr, 0)) { c.petTab = wrap(tab + d, 3); save_ui_config(); }
        }
        cfg_rows(K, ctrl_uid_i(base, 4), jd, jdN, x, ry, w, adv);
        if (adv && tab == 0) {   // Preview pet (edit / this page only) : every jug pet, its art and its moves
            const int pi = (c.bstDemo < 0 || c.bstDemo >= BST_PETS_N) ? 0 : c.bstDemo;
            if (int d = cfg_select_row(K, ctrl_uid_i(base, 5), x, ry, w, tr("Preview pet", "Pet (aper\xC3\xA7u)"), BST_PETS[pi].name, nullptr, &c.bstDemo, 4)) { c.bstDemo = wrap(pi + d, BST_PETS_N); save_ui_config(); }
        } else if (adv && tab == 2) {   // Preview avatar : every picture and pact list can be checked
            const int ai = (c.smnDemo < 0 || c.smnDemo >= SMN_AVATARS_N) ? 0 : c.smnDemo;
            if (int d = cfg_select_row(K, ctrl_uid_i(base, 6), x, ry, w, tr("Preview avatar", "Avatar (aper\xC3\xA7u)"), SMN_AVATARS[ai].en, nullptr, &c.smnDemo, 4)) { c.smnDemo = wrap(ai + d, SMN_AVATARS_N); save_ui_config(); }
        }
        v2_end(K, ry);
    }
    if (adv) {
        BoxStyle& bs = c.petBox;
        const FrameRefs fr = { &bs.on, &bs.border, &bs.alpha, &bs.alphaOwn, &bs.themeCopy, &bs.theme, &bs.hue, &bs.lum };
        if (v2_section(K, ctrl_uid_i(base, 7), 20, "Frame", "Cadre", x, ry, w, cfg_frame_modified(fr))) { cfg_frame_rows(K, ctrl_uid_i(base, 8), x, ry, w, fr); v2_end(K, ry); }
    }
    if (v2_section(K, ctrl_uid_i(base, 9), 21, "Content", "Contenu", x, ry, w, cfg_count_modified(jc, jcN, adv)))
        v2_rows(K, ctrl_uid_i(base, 10), jc, jcN, x, ry, w);
    if (adv) {   // the tab's text elements
        const char* lab[4]; TextStyle* ts[4]; int n = 0; int* sel = &cfgBstTextElem_;
        if (tab == 2) {
            const char* l[SMN_TE_COUNT] = { tr("Avatar name", "Nom de l'avatar"), tr("HP / TP / MP / distance", "HP / TP / MP / distance"), tr("Job abilities", "Job abilities"), tr("Pacts", "Pacts") };
            n = SMN_TE_COUNT; for (int i = 0; i < n; ++i) { lab[i] = l[i]; ts[i] = &c.smnText[i]; } sel = &cfgSmnTextElem_;
        } else if (tab == 1) {
            const char* l[GEO_TE_COUNT] = { tr("Bubble / aura names", "Noms bulle / aura"), tr("HP / MP / times / distance", "HP / MP / temps / distance"), tr("Job abilities", "Job abilities") };
            n = GEO_TE_COUNT; for (int i = 0; i < n; ++i) { lab[i] = l[i]; ts[i] = &c.geoText[i]; } sel = &cfgGeoTextElem_;
        } else {
            const char* l[BST_TE_COUNT] = { tr("Pet name", "Nom du pet"), tr("HP / TP / distance", "HP / TP / distance"), tr("Ready charges", "Charges Ready"), tr("Ready moves", "Ready moves") };
            n = BST_TE_COUNT; for (int i = 0; i < n; ++i) { lab[i] = l[i]; ts[i] = &c.bstText[i]; }
        }
        if (v2_section(K, ctrl_uid_i(base, 11), 5, "Text", "Texte", x, ry, w, cfg_text_modified(ts, n))) { cfg_text_rows(K, ctrl_uid_i(base, 12), x, ry, w, lab, ts, n, sel); v2_end(K, ry); }
    } else v2_simple_note(K, x, ry);
    g_fade = e;
}
#undef NOPT

} // namespace aio
