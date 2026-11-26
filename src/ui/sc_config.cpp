// sc_config.cpp -- the Skillchains module's settings panel (//aio config -> "Skillchains").
//
// Each control is keyed by CTRL_ID (file:line hash, see config_controls.h) -- no hand-numbered uids. The panel grammar
// (docs/architecture/config-panels.md) : Display [6] / Frame [20] / Content [21] / Elements [7] / Animations [22] /
// Text [5]. The STYLE row comes first in Display and every row below is that style's : the Circle (the resonance
// sphere + its list, ui/sc_circle.h) or the Classic text box. The box itself is placed in //aio edit (like every other
// box) -- position isn't a slider here.
#include "ui/config_page.h"
#include "ui/config_controls.h"    // cat_header / row_slider / row_selector / toggle_chip + palette + color_picker
#include "ui/config_rows.h"        // ROW_BAND / ROW_NEXT / CFG_COLOR_PICKER
#include "model/ui_config.h"
#include "model/skillchain.h"   // SCP_N, scprop_name (the preview selector)
#include "gfx/font.h"
#include "gfx/draw.h"
#include <cstdio>

namespace aio {

// a percent slider over [lo, hi] in `step`s, on a float field (the rows below share it)
#define SC_SLIDER(LABEL, FIELD, LO, HI, STEP, FMT_MUL) \
    { ROW_BAND(46.0f) \
        const float lo_ = (LO), hi_ = (HI); char b_[16]; sprintf(b_, "%d%%", (int)((FIELD) * (FMT_MUL) + 0.5f)); \
        float v01_ = ((FIELD) - lo_) / (hi_ - lo_); v01_ = clampf(v01_, 0.0f, 1.0f); \
        if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, LABEL, b_, &v01_)) { float v_ = lo_ + v01_ * (hi_ - lo_); v_ = (float)((int)(v_ / (STEP) + 0.5f)) * (STEP); (FIELD) = v_ < lo_ ? lo_ : (v_ > hi_ ? hi_ : v_); } \
    } ROW_NEXT(46.0f)

void ConfigPage::draw_sc_config(u32 dev, Font* fo, const MouseState* mo, bool click,
                                float& ry, int& ri, float e,
                                float bandX, float bandW, float coX, float ctrlW,
                                float hdrX, float hdrW) {
    UiConfig& c = ui_config();
    const bool circle = c.scStyle == 0;

    // ===== sub-section : DISPLAY =====
    const float aF6_ = cat_fold(CTRL_ID, !ui_config().cfgAdv || (catOpen_[6]));
    cat_panel(dev, hdrX, ry, hdrW, cat_card_h(catH_[6], aF6_));   // the section IS a card, collapsed or not
    if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Display", "Affichage"), catOpen_[6], aF6_) && ui_config().cfgAdv) catOpen_[6] = !catOpen_[6];
    ROW_NEXT(42.0f)
    if (aF6_ > 0.0f) {
        const float top6_ = ry;
        cat_fold_clip(dev, hdrX, top6_, hdrW, catH_[6] * aF6_);
        ROW_TOGGLE(CTRL_ID, tr("Show", "Afficher"), c.scShow)   // Show the skillchains box
        { ROW_BAND(52.0f)   // the STYLE : the resonance circle, or the classic text box
            const char* SL[2] = { tr("Circle", "Cercle"), tr("Classic", "Classique") };
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Style", "Style"), SL[circle ? 0 : 1])) c.scStyle = wrap(c.scStyle + d, 2);
        } ROW_NEXT(52.0f)
        { ROW_BAND(52.0f)   // the PREVIEW's sample : a lone move, or any skillchain (not saved -- it only drives the preview)
            const int pv = (c.scPreview >= -1 && c.scPreview < SCP_N) ? c.scPreview : SCP_Fusion;
            const char* nm = pv < 0 ? tr("Opening move", "WS d'ouverture") : scprop_name(pv);
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Preview", "Aper\xC3\xA7u"), nm)) c.scPreview = wrap(pv + 1 + d, SCP_N + 1) - 1;
        } ROW_NEXT(52.0f)
        SC_SLIDER(circle ? tr("Sphere size", "Taille de la sph\xC3\xA8re") : tr("Size", "Taille"), c.scScale, 0.50f, circle ? 2.50f : 2.00f, 0.05f, 100.0f)   // (Classic draws up to 200 %)
        if (circle) { SC_SLIDER(tr("List size", "Taille de la liste"), c.scListScale, 0.50f, 2.00f, 0.05f, 100.0f) }   // braces : the macro is TWO statements
        ROW_GROW(CTRL_ID, UiConfig::GB_SC)   // which way the box grows as its content widens (ui/box_grow.h)
        cat_fold_end(dev, ry, top6_, catH_[6], aF6_);
    }   // end Display
    ry += snap(16.0f);                                 // air between this section and the next title bar

    // ===== sub-section : FRAME -- the Classic box's chrome, or the Circle's LIST frame (the sphere has none) =====
if (ui_config().cfgAdv) {   // Advanced only (the config's depth, 2026-11-26)
    const float aF20_ = cat_fold(CTRL_ID, catOpen_[20]);
    cat_panel(dev, hdrX, ry, hdrW, cat_card_h(catH_[20], aF20_));
    if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Frame", "Cadre"), catOpen_[20], aF20_)) catOpen_[20] = !catOpen_[20];
    ROW_NEXT(42.0f)
    if (aF20_ > 0.0f) {
        const float top20_ = ry;
        cat_fold_clip(dev, hdrX, top20_, hdrW, catH_[20] * aF20_);
        if (circle) ROW_NOTE(tr("The frame of the weapon skill list (off : a faint shade).", "Le cadre de la liste des WS (sans : une l\xC3\xA9g\xC3\xA8re ombre)."))
        draw_box_appearance(dev, fo, mo, click, ry, ri, e, bandX, bandW, coX, ctrlW, circle ? c.scListBox : c.scBox);   // Box / Border / Transparency / Theme / Hue / Luminosity
        cat_fold_end(dev, ry, top20_, catH_[20], aF20_);
    }   // end Frame
    ry += snap(16.0f);
}   // end Advanced only

    // ===== sub-section : CONTENT =====
if (ui_config().cfgAdv) {   // Advanced only (the config's depth, 2026-11-26)
    const float aF21_ = cat_fold(CTRL_ID, catOpen_[21]);
    cat_panel(dev, hdrX, ry, hdrW, cat_card_h(catH_[21], aF21_));
    if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Content", "Contenu"), catOpen_[21], aF21_)) catOpen_[21] = !catOpen_[21];
    ROW_NEXT(42.0f)
    if (aF21_ > 0.0f) {
        const float top21_ = ry;
        cat_fold_clip(dev, hdrX, top21_, hdrW, catH_[21] * aF21_);
        ROW_TOGGLE(CTRL_ID, tr("Show party/nearby chains", "Afficher SC du groupe/proches"), c.scNearby)   // Display scope : also show party/nearby chains (not just your target)
        { ROW_BAND(52.0f)   // which moves the list offers : the weapon in your hands, or every weapon your job wields
            const char* WS2[2] = { tr("My weapon", "Mon arme"), tr("All my job's weapons", "Toutes les armes du job") };
            const int sc2 = c.scWsScope ? 1 : 0;
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Weapon skills", "WS propos\xC3\xA9" "es"), WS2[sc2])) c.scWsScope = wrap(sc2 + d, 2);
        } ROW_NEXT(52.0f)
        if (circle) {
            { ROW_BAND(52.0f)   // the list's SHAPE (the same information) : rows, a tree, cards, grouped by skillchain
                const char* LS[4] = { tr("Rows", "Lignes"), tr("Tree", "Arbre"), tr("Cards", "Cartes"), tr("Grouped", "Group\xC3\xA9" "e") };
                const int st = (c.scListStyle >= 0 && c.scListStyle < 4) ? c.scListStyle : 0;
                if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("List style", "Style de la liste"), LS[st])) c.scListStyle = wrap(st + d, 4);
            } ROW_NEXT(52.0f)
            if (c.scListStyle != 0) { { ROW_BAND(52.0f)   // how many columns the list is laid in (the Rows style has its own axis)
                const char* CO[5] = { tr("Auto", "Auto"), "1", "2", "3", "4" };
                const int lc = (c.scListCols >= 0 && c.scListCols <= 4) ? c.scListCols : 0;
                if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Columns", "Colonnes"), CO[lc])) c.scListCols = wrap(lc + d, 5);
              } ROW_NEXT(52.0f) }
            ROW_TOGGLE(CTRL_ID, tr("Element icons in the list", "Ic\xC3\xB4nes d'\xC3\xA9l\xC3\xA9ment dans la liste"), c.scListElem)   // off : the most compact list
            if (c.scListElem) { SC_SLIDER(tr("List icons size", "Taille des ic\xC3\xB4nes (liste)"), c.scListIcon, 0.50f, 2.00f, 0.05f, 100.0f) }   // braces : the macro is TWO statements
            if (c.scListStyle == 2) { SC_SLIDER(tr("Card size", "Taille des cartes"), c.scCardScale, 0.60f, 1.60f, 0.05f, 100.0f) }   // braces : the macro is TWO statements
            { ROW_BAND(52.0f)   // where the list sits round the sphere
                const char* PL[5] = { tr("Under", "Dessous"), tr("Above", "Dessus"), tr("Left", "\xC3\x80 gauche"), tr("Right", "\xC3\x80 droite"), tr("Both sides", "Des deux c\xC3\xB4t\xC3\xA9s") };
                const int lp = (c.scListPos >= 0 && c.scListPos < 5) ? c.scListPos : 0;
                if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("List position", "Position de la liste"), PL[lp])) c.scListPos = wrap(lp + d, 5);
            } ROW_NEXT(52.0f)
            { ROW_BAND(46.0f)   // how far from the sphere
                const float lo = -200.0f, hi = 80.0f; char b[16]; sprintf(b, "%d", (int)(c.scListOff + (c.scListOff < 0.0f ? -0.5f : 0.5f)));   // < 0 : closer, even over the sphere
                float v01 = clampf((c.scListOff - lo) / (hi - lo), 0.0f, 1.0f);
                if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("List distance", "Distance de la liste"), b, &v01)) { float v = lo + v01 * (hi - lo); c.scListOff = (float)(int)(v + (v < 0.0f ? -0.5f : 0.5f)); }
            } ROW_NEXT(46.0f)
        }
        if (circle) {
            { ROW_BAND(46.0f)   // the title : up, or down over the sphere
                const float lo = -120.0f, hi = 80.0f; char b[16]; sprintf(b, "%d", (int)(c.scTitleOff + (c.scTitleOff < 0.0f ? -0.5f : 0.5f)));
                float v01 = clampf((c.scTitleOff - lo) / (hi - lo), 0.0f, 1.0f);
                if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Title height", "Hauteur du titre"), b, &v01)) { const float v = lo + v01 * (hi - lo); c.scTitleOff = (float)(int)(v + (v < 0.0f ? -0.5f : 0.5f)); }
            } ROW_NEXT(46.0f)
        }
        cat_fold_end(dev, ry, top21_, catH_[21], aF21_);
    }   // end Content
    ry += snap(16.0f);
}   // end Advanced only

    // ===== sub-section : ELEMENTS (which lines to show) =====
    const float aF7_ = cat_fold(CTRL_ID, !ui_config().cfgAdv || (catOpen_[7]));
    cat_panel(dev, hdrX, ry, hdrW, cat_card_h(catH_[7], aF7_));   // the section IS a card, collapsed or not
    if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Elements", "\xC3\x89l\xC3\xA9ments"), catOpen_[7], aF7_) && ui_config().cfgAdv) catOpen_[7] = !catOpen_[7];
    ROW_NEXT(42.0f)
    if (aF7_ > 0.0f) {
        const float top7_ = ry;
        cat_fold_clip(dev, hdrX, top7_, hdrW, catH_[7] * aF7_);
        ROW_TOGGLE(CTRL_ID, tr("Title", "Titre"),                            c.scTitle)
        ROW_TOGGLE(CTRL_ID, tr("Timer (Go! / Burst)", "Timer (Go! / Burst)"), c.scTimer)
        if (!circle) { ROW_TOGGLE(CTRL_ID, tr("Step line", "Ligne Step"),       c.scStep) }
        if (circle) { ROW_TOGGLE(CTRL_ID, tr("Burst element icons", "Ic\xC3\xB4nes des \xC3\xA9l\xC3\xA9ments"), c.scProps) }
        else        { ROW_TOGGLE(CTRL_ID, tr("Property + elements", "Propri\xC3\xA9t\xC3\xA9 + \xC3\xA9l\xC3\xA9ments"), c.scProps) }
        ROW_TOGGLE(CTRL_ID, tr("Weaponskill list", "Liste weaponskills"),     c.scList)
        ROW_TOGGLE(CTRL_ID, tr("TP indicator", "Indicateur TP"),              c.scTP)
        // WS spacing : the vertical gap between each weaponskill in the continuation list. Always shown (it only
        // has a visible effect while the list is on, but hiding it made it hard to find).
        if (c.scList && (!circle || (c.scListStyle != 1 && c.scListStyle != 2))) {   // read by Rows / Grouped and Classic -- not Tree, Cards
        SC_SLIDER(tr("WS spacing", "Espacement WS"), c.scListGap, 0.60f, 3.00f, 0.05f, 100.0f)
        }
        cat_fold_end(dev, ry, top7_, catH_[7], aF7_);
    }   // end Elements
    ry += snap(16.0f);                                 // air between this section and the next title bar

    // ===== sub-section : ANIMATIONS (the Circle's) -- what it costs : all of it, the scene distortion, the
    // particles, how far the effects spread =====
    if (circle) {
if (ui_config().cfgAdv) {   // Advanced only (the config's depth, 2026-11-26)
        const float aF22_ = cat_fold(CTRL_ID, catOpen_[22]);
        cat_panel(dev, hdrX, ry, hdrW, cat_card_h(catH_[22], aF22_));
        if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Animations", "Animations"), catOpen_[22], aF22_)) catOpen_[22] = !catOpen_[22];
        ROW_NEXT(42.0f)
        if (aF22_ > 0.0f) {
            const float top22_ = ry;
            cat_fold_clip(dev, hdrX, top22_, hdrW, catH_[22] * aF22_);
            ROW_TOGGLE(CTRL_ID, tr("Animations", "Animations"), c.scAnim)
            if (c.scAnim) { ROW_TOGGLE(CTRL_ID, tr("Scene distortion", "D\xC3\xA9" "formation du d\xC3\xA9" "cor"), c.scDistort) }   // (part of the animations)
            SC_SLIDER(tr("Particles", "Particules"), c.scParticles, 0.0f, 1.0f, 0.05f, 100.0f)
            SC_SLIDER(tr("Effect reach", "Port\xC3\xA9" "e des effets"), c.scReach, 0.30f, 1.00f, 0.05f, 100.0f)
            cat_fold_end(dev, ry, top22_, catH_[22], aF22_);
        }   // end Animations
        ry += snap(16.0f);
}   // end Advanced only

        // ===== sub-section : ARCADE WS / MAGIC BURST (the hit popup on the sphere ; the Arcade WS module keeps its own) =====
if (ui_config().cfgAdv) {   // Advanced only (the config's depth, 2026-11-26)
        const float aF26_ = cat_fold(CTRL_ID, catOpen_[26]);
        cat_panel(dev, hdrX, ry, hdrW, cat_card_h(catH_[26], aF26_));
        if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Popup on the sphere (WS / Magic Burst)", "Popup sur la sph\xC3\xA8re (WS / Magic Burst)"), catOpen_[26], aF26_)) catOpen_[26] = !catOpen_[26];
        ROW_NEXT(42.0f)
        if (aF26_ > 0.0f) {
            const float top26_ = ry;
            cat_fold_clip(dev, hdrX, top26_, hdrW, catH_[26] * aF26_);
            ROW_TOGGLE(CTRL_ID, tr("Arcade WS (your weaponskill)", "Arcade WS (ta WS)"), c.scArcade)
            ROW_TOGGLE(CTRL_ID, tr("Magic Burst (yours)", "Magic Burst (le tien)"), c.scMbFx)
            if (c.scArcade || c.scMbFx) {
                { ROW_BAND(52.0f)   // the text's look : four fighting-game styles
                    const char* HS[4] = { "Versus", "Comic", "Impact", tr("Critical", "Critique") };
                    const int hst = (c.scHitStyle >= 0 && c.scHitStyle <= 3) ? c.scHitStyle : 0;
                    if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Popup style", "Style du popup"), HS[hst])) c.scHitStyle = wrap(hst + d, 4);
                } ROW_NEXT(52.0f)
                { ROW_BAND(52.0f)   // where it bursts : never over the sphere -- the side the list leaves free, or the one chosen
                    const char* HP[5] = { tr("Auto (free side)", "Auto (c\xC3\xB4t\xC3\xA9 libre)"), tr("Above", "Dessus"), tr("Under", "Dessous"), tr("Left", "\xC3\x80 gauche"), tr("Right", "\xC3\x80 droite") };
                    const int hp = (c.scHitPos >= 0 && c.scHitPos <= 4) ? c.scHitPos : 0;
                    if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Position", "Position"), HP[hp])) c.scHitPos = wrap(hp + d, 5);
                } ROW_NEXT(52.0f)
                { ROW_BAND(46.0f)   // the whole block : left / right
                    const float lo = -200.0f, hi = 200.0f; char b[16]; sprintf(b, "%d", (int)(c.scHitX + (c.scHitX < 0.0f ? -0.5f : 0.5f)));
                    float v01 = clampf((c.scHitX - lo) / (hi - lo), 0.0f, 1.0f);
                    if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Move left / right", "D\xC3\xA9placer gauche / droite"), b, &v01)) { const float v = lo + v01 * (hi - lo); c.scHitX = (float)(int)(v + (v < 0.0f ? -0.5f : 0.5f)); }
                } ROW_NEXT(46.0f)
                { ROW_BAND(46.0f)   // the whole block : up / down
                    const float lo = -200.0f, hi = 200.0f; char b[16]; sprintf(b, "%d", (int)(c.scHitY + (c.scHitY < 0.0f ? -0.5f : 0.5f)));
                    float v01 = clampf((c.scHitY - lo) / (hi - lo), 0.0f, 1.0f);
                    if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Move up / down", "D\xC3\xA9placer haut / bas"), b, &v01)) { const float v = lo + v01 * (hi - lo); c.scHitY = (float)(int)(v + (v < 0.0f ? -0.5f : 0.5f)); }
                } ROW_NEXT(46.0f)
                ROW_TOGGLE(CTRL_ID, tr("Impact effects", "Effets d'impact"), c.scHitFx)
                SC_SLIDER(tr("Text size", "Taille du texte"), c.scHitScale, 0.50f, 2.00f, 0.05f, 100.0f)
                { ROW_BAND(46.0f)   // the name / MAGIC BURST line : up or down
                    const float lo = -80.0f, hi = 80.0f; char b[16]; sprintf(b, "%d", (int)(c.scHitNameY + (c.scHitNameY < 0.0f ? -0.5f : 0.5f)));
                    float v01 = clampf((c.scHitNameY - lo) / (hi - lo), 0.0f, 1.0f);
                    if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("WS name height", "Hauteur du nom du WS"), b, &v01)) { const float v = lo + v01 * (hi - lo); c.scHitNameY = (float)(int)(v + (v < 0.0f ? -0.5f : 0.5f)); }
                } ROW_NEXT(46.0f)
                { ROW_BAND(46.0f)   // the damage line : up or down
                    const float lo = -80.0f, hi = 80.0f; char b[16]; sprintf(b, "%d", (int)(c.scHitDmgY + (c.scHitDmgY < 0.0f ? -0.5f : 0.5f)));
                    float v01 = clampf((c.scHitDmgY - lo) / (hi - lo), 0.0f, 1.0f);
                    if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Damage height", "Hauteur des d\xC3\xA9g\xC3\xA2ts"), b, &v01)) { const float v = lo + v01 * (hi - lo); c.scHitDmgY = (float)(int)(v + (v < 0.0f ? -0.5f : 0.5f)); }
                } ROW_NEXT(46.0f)
                if (c.scMbFx) {
                    ROW_TOGGLE(CTRL_ID, tr("\"MAGIC BURST\" line", "Ligne \"MAGIC BURST\""), c.scMbLabel)
                    ROW_TOGGLE(CTRL_ID, tr("Burst spell name", "Nom du sort burst\xC3\xA9"), c.scMbName)
                }
                { ROW_BAND(52.0f)   // the font (drawn heavy, like the Arcade WS popup)
                    int fc = c.scHitFont; if (fc < 0 || fc >= ui_font_count()) fc = 0;
                    if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Font", "Police"), fc == 0 ? tr("Impact (default)", "Impact (d\xC3\xA9""faut)") : ui_font_label(fc))) c.scHitFont = wrap(fc + d, ui_font_count());
                } ROW_NEXT(52.0f)
                struct HitCol { const char* en; const char* fr; unsigned* col; };
                HitCol hc[3] = { { "WS name", "Nom de la WS", &c.scHitNameCol }, { "Damage A", "D\xC3\xA9g\xC3\xA2ts A", &c.scHitDmgCol1 }, { "Damage B", "D\xC3\xA9g\xC3\xA2ts B", &c.scHitDmgCol2 } };
                for (int i = 0; i < 3; ++i) {
                    { ROW_BAND(34.0f)
                        const float rowH = snap(34.0f), ty = ry + yo; fo->begin(dev);
                        fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, tr(hc[i].en, hc[i].fr), ts_label(), fa(C_TEXT), fa(C_STROKE), 1.0f);
                    } ROW_NEXT(34.0f)
                    CFG_COLOR_PICKER_I(hc[i].col, i)   // loop variant : distinct drag/hover uids per colour
                }
                ROW_NOTE(tr("A burst's damage takes the colour of its element.", "Les d\xC3\xA9g\xC3\xA2ts d'un burst prennent la couleur de son \xC3\xA9l\xC3\xA9ment."))
            }
            cat_fold_end(dev, ry, top26_, catH_[26], aF26_);
        }   // end Arcade WS / Magic Burst
        ry += snap(16.0f);
}   // end Advanced only
    }

    // ===== sub-section : TEXT (per-element typography, like the other modules) =====
if (ui_config().cfgAdv) {   // Advanced only (the config's depth, 2026-11-26)
    const float aF5_ = cat_fold(CTRL_ID, catOpen_[5]);
    cat_panel(dev, hdrX, ry, hdrW, cat_card_h(catH_[5], aF5_));   // the section IS a card, collapsed or not
    if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Text", "Texte"), catOpen_[5], aF5_)) catOpen_[5] = !catOpen_[5];
    ROW_NEXT(42.0f)
    if (aF5_ > 0.0f) {
        const float top5_ = ry;
        cat_fold_clip(dev, hdrX, top5_, hdrW, catH_[5] * aF5_);
        { ROW_BAND(52.0f)   // element selector (the Circle : Property = the list's skillchain names, WS list = its moves)
            const char* TLBL[SC_TE_COUNT] = { tr("Title", "Titre"), tr("Timer", "Timer"), tr("Step", "Step"), circle ? tr("List : skillchain", "Liste : skillchain") : tr("Property", "Propri\xC3\xA9t\xC3\xA9"), circle ? tr("List : moves", "Liste : WS") : tr("WS list", "Liste WS") };
            int te = (cfgScTextElem_ < 0 || cfgScTextElem_ >= SC_TE_COUNT) ? 0 : cfgScTextElem_;
            if (circle && te == SC_STEP) te = SC_TIMER;   // no Step line in the Circle
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Element", "\xC3\x89l\xC3\xA9ment"), TLBL[te])) {
                te = wrap(te + d, SC_TE_COUNT); if (circle && te == SC_STEP) te = wrap(te + d, SC_TE_COUNT);
                cfgScTextElem_ = te;
            }
        }
        ROW_NEXT(52.0f)
        {
            int te = (cfgScTextElem_ < 0 || cfgScTextElem_ >= SC_TE_COUNT) ? 0 : cfgScTextElem_;
            if (circle && te == SC_STEP) te = SC_TIMER;
            draw_text_style(dev, fo, mo, click, ry, ri, e, bandX, bandW, coX, ctrlW, c.scText[te]);
        }
        cat_fold_end(dev, ry, top5_, catH_[5], aF5_);
    }   // end Text

    ry += snap(16.0f);
}   // end Advanced only
    if (!ui_config().cfgAdv) SIMPLE_MORE   // Simple : where the rest is
    #undef SC_SLIDER
    #undef ROW_BAND
    #undef ROW_NEXT
}

} // namespace aio
