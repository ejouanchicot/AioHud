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
#include "ui/cfg_engine.h"         // the redesign : OptDef tables + the kit (draw_sc_v2)
#include "model/ui_config.h"
#include "model/skillchain.h"   // SCP_N, scprop_name (the preview selector)
#include "gfx/font.h"
#include "gfx/draw.h"
#include <cmath>
#include <cstdio>
#include <cstring>   // memcmp (the Advanced page's modified count)

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

// ======== THE NEW SKILLCHAINS PAGE (the config redesign, milestone 3, 2026-11-27) ========
// SIMPLE : Display [6] / Elements [7], the tables below (a row the chosen STYLE does not draw is greyed with that reason).
// ADVANCED (2026-11-27 restructure) : sections by ELEMENT, and the STYLE decides which exist --
//   Display [6] · Frame [20] (the Classic box, or the Circle's list frame : only while that list shows) ·
//   Sphere (Circle) / Lines (Classic) [7] · Weaponskill list [21] · WS / Magic Burst popup [26] (Circle only) ·
//   Behaviour [22] (the nearby scope ; the Circle's animations) · Text [5].
// Each "show X + how" pair is ONE choice (the list : Off / Rows / Tree / Cards / Grouped ; the popup : Off / WS / Magic
// Burst / Both) and a row that only one mode reads appears in that mode only ; a plain on/off dependency greys with its
// reason. //aio config v1 : the old page (draw_sc_config).
#define F_(name) [](UiConfig& c) -> void* { return &c.name; }
static const char* const SC_ST_EN[2] = { "Circle", "Classic" },                    * const SC_ST_FR[2] = { "Cercle", "Classique" };
static const char* const SC_WS_EN[2] = { "My weapon", "All my job's weapons" },    * const SC_WS_FR[2] = { "Mon arme", "Toutes les armes du job" };
static const char* const SC_LS_EN[4] = { "Rows", "Tree", "Cards", "Grouped" },     * const SC_LS_FR[4] = { "Lignes", "Arbre", "Cartes", "Group\xC3\xA9""e" };
static const char* const SC_CO_EN[5] = { "Auto", "1", "2", "3", "4" },             * const SC_CO_FR[5] = { "Auto", "1", "2", "3", "4" };
static const char* const SC_LP_EN[5] = { "Under", "Above", "Left", "Right", "Both sides" };
static const char* const SC_LP_FR[5] = { "Dessous", "Dessus", "\xC3\x80 gauche", "\xC3\x80 droite", "Des deux c\xC3\xB4t\xC3\xA9s" };
static const char* const SC_HS_EN[4] = { "Versus", "Comic", "Impact", "Critical" }, * const SC_HS_FR[4] = { "Versus", "Comic", "Impact", "Critique" };
static const char* const SC_HP_EN[5] = { "Auto (free side)", "Above", "Under", "Left", "Right" };
static const char* const SC_HP_FR[5] = { "Auto (c\xC3\xB4t\xC3\xA9 libre)", "Dessus", "Dessous", "\xC3\x80 gauche", "\xC3\x80 droite" };
// the reasons
static const char* sw_circle(const UiConfig& c)  { return c.scStyle == 0 ? nullptr : tr("inactive : Classic style", "inactif : style Classique"); }
static const char* sw_classic(const UiConfig& c) { return c.scStyle != 0 ? nullptr : tr("inactive : Circle style", "inactif : style Cercle"); }
static const char* sw_list(const UiConfig& c)    { return c.scList ? nullptr : tr("inactive : weaponskill list off", "inactif : liste des WS coup\xC3\xA9""e"); }
static const char* sw_gap(const UiConfig& c)     {   // read by Rows / Grouped and Classic -- not Tree, Cards
    if (const char* w = sw_list(c)) return w;
    return (c.scStyle == 0 && (c.scListStyle == 1 || c.scListStyle == 2)) ? tr("inactive : Tree / Cards list style", "inactif : liste en Arbre / Cartes") : nullptr; }
static const char* sw_licon(const UiConfig& c)   { return c.scListElem ? nullptr : tr("inactive : list icons off", "inactif : ic\xC3\xB4nes de la liste coup\xC3\xA9""es"); }
static const char* sw_anim(const UiConfig& c)    { return c.scAnim ? nullptr : tr("inactive : animations off", "inactif : animations coup\xC3\xA9""es"); }
// the two "how it shows" choices of the Advanced page (index 0 = Off is added by the choice row)
static const char* const SC_PU_EN[3] = { "Your weaponskill", "Your Magic Burst", "Both" }, * const SC_PU_FR[3] = { "Ta WS", "Ton Magic Burst", "Les deux" };

static const OptDef SC_DISPLAY_A[] = {
    { "Show", "Afficher", OK_TOGGLE, OL_SIMPLE, F_(scShow), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Style", "Style", OK_SELECT, OL_SIMPLE, F_(scStyle), 0, 0, SC_ST_EN, SC_ST_FR, 2, nullptr, 0 },
};
static const OptDef SC_DISPLAY_B[] = {
    { "List size", "Taille de la liste", OK_PCT, OL_SIMPLE, F_(scListScale), 0.5f, 2.0f, nullptr, nullptr, 0, sw_circle, 0 },
    { "Grows toward", "Grandit vers", OK_GROW, OL_SIMPLE, nullptr, 0, 0, nullptr, nullptr, 0, nullptr, UiConfig::GB_SC },
};
static const OptDef SC_GROW[] = {   // Advanced : the grow row alone (the list size moved to the list's section)
    { "Grows toward", "Grandit vers", OK_GROW, OL_ADV, nullptr, 0, 0, nullptr, nullptr, 0, nullptr, UiConfig::GB_SC },
};
static const OptDef SC_ELEM_A[] = {
    { "Title", "Titre", OK_TOGGLE, OL_SIMPLE, F_(scTitle), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Timer (Go! / Burst)", "Timer (Go! / Burst)", OK_TOGGLE, OL_SIMPLE, F_(scTimer), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Step line", "Ligne Step", OK_TOGGLE, OL_SIMPLE, F_(scStep), 0, 0, nullptr, nullptr, 0, sw_classic, 0 },
};
static const OptDef SC_ELEM_B[] = {
    { "Weaponskill list", "Liste weaponskills", OK_TOGGLE, OL_SIMPLE, F_(scList), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "TP indicator", "Indicateur TP", OK_TOGGLE, OL_SIMPLE, F_(scTP), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "WS spacing", "Espacement WS", OK_PCT, OL_SIMPLE, F_(scListGap), 0.6f, 3.0f, nullptr, nullptr, 0, sw_gap, 0 },
};
#undef F_
#define NOPT(a) ((int)(sizeof(a) / sizeof((a)[0])))
static int s_scColPick = 0;   // the popup colour the picker edits : 0 WS name, 1 damage A, 2 damage B

void ConfigPage::draw_sc_v2(u32 dev, Font* fo, const MouseState* mo, bool click,
                            float& ry, int& ri, float e,
                            float bandX, float bandW, float coX, float ctrlW,
                            float hdrX, float hdrW) {
    Kit K;
    if (!v2_kit(K, mo, click)) { draw_sc_config(dev, fo, mo, click, ry, ri, e, bandX, bandW, coX, ctrlW, hdrX, hdrW); return; }
    g_fade = e;
    UiConfig& c = ui_config();
    const bool adv = c.cfgAdv != 0;
    const float x = coX, w = ctrlW;
    const int base = CTRL_ID;
    static const UiConfig D{};   // the factory values (the modified count of a row drawn by hand)
    // COUNT, then DRAW : every section body runs twice -- once with `cnt` set, adding up the rows that will carry the
    // modified bead (exactly the rows the body draws, greyed ones included), then for real under its header
    bool cnt = false; int nC = 0;
    auto fac = [&](const void* p) -> const void* { return (const char*)&D + ((const char*)p - (const char*)&c); };
    auto dI  = [&](const void* p) { return memcmp(p, fac(p), 4) != 0 ? 1 : 0; };
    auto dF  = [&](const float* p) { return fabsf(*p - *(const float*)fac(p)) > 0.0005f ? 1 : 0; };
    auto tog = [&](int uid, const char* label, int* f, const char* why) {
        if (cnt) { nC += dI(f); return; }
        if (cfg_toggle_row(K, uid, x, ry, w, label, *f != 0, why, f, 4)) { *f = *f ? 0 : 1; save_ui_config(); }
    };
    auto sel = [&](int uid, const char* label, int* f, const char* const* en, const char* const* frl, int n, const char* why) {
        if (cnt) { nC += dI(f); return; }
        const int v = (*f < 0 || *f >= n) ? 0 : *f;
        if (int d = cfg_select_row(K, uid, x, ry, w, label, tr(en[v], frl[v]), why, f, 4)) { *f = wrap(v + d, n); save_ui_config(); }
    };
    // a percent slider on a float, in 0.05 steps (the old SC_SLIDER) -- for a row whose range or label follows the style
    auto pct = [&](int uid, const char* label, float* f, float lo, float hi, const char* why) {
        if (cnt) { nC += dF(f); return; }
        float v01 = (*f - lo) / (hi - lo); v01 = v01 < 0.0f ? 0.0f : (v01 > 1.0f ? 1.0f : v01);
        char b[16]; _snprintf(b, sizeof(b), "%d %%", (int)(*f * 100.0f + 0.5f)); b[15] = 0;
        if (cfg_slider_row(K, uid, x, ry, w, label, &v01, b, why, f, 4, true)) {
            const float v = floorf((lo + v01 * (hi - lo)) / 0.05f + 0.5f) * 0.05f; *f = v < lo ? lo : (v > hi ? hi : v); }
    };
    // a signed offset in whole units (design px / sphere units), shown as a plain number like the old page
    auto off = [&](int uid, const char* label, float* f, float lo, float hi, const char* why) {
        if (cnt) { nC += dF(f); return; }
        float v01 = (*f - lo) / (hi - lo); v01 = v01 < 0.0f ? 0.0f : (v01 > 1.0f ? 1.0f : v01);
        char b[16]; _snprintf(b, sizeof(b), "%d", (int)(*f + (*f < 0.0f ? -0.5f : 0.5f))); b[15] = 0;
        if (cfg_slider_row(K, uid, x, ry, w, label, &v01, b, why, f, 4, true)) {
            const float v = lo + v01 * (hi - lo); *f = (float)(int)(v + (v < 0.0f ? -0.5f : 0.5f)); }
    };
    auto rows = [&](int uid, const OptDef* d, int n) { if (cnt) { nC += cfg_count_modified(d, n, true); return; } cfg_rows(K, uid, d, n, x, ry, w, true); };
    auto note = [&](const char* t) { if (!cnt) cfg_note(K, x, ry, t); };
    // ONE choice for "shown or not, and how" : 0 = Off, k = mode k-1 ; the bead / right-click reset : the field that differs
    auto choice = [&](int uid, const char* label, int* on, int* mode, const char* const* men, const char* const* mfr, int n) {
        int* mark = memcmp(on, fac(on), 4) != 0 ? on : mode;
        if (cnt) { nC += dI(mark); return; }
        const char* lab[8]; lab[0] = tr("Off", "D\xC3\xA9sactiv\xC3\xA9"); for (int k = 0; k < n && k < 7; ++k) lab[k + 1] = tr(men[k], mfr[k]);
        int v = *on ? 1 + ((*mode < 0 || *mode >= n) ? 0 : *mode) : 0;
        if (int d = cfg_select_row(K, uid, x, ry, w, label, lab[v], nullptr, mark, 4)) {
            v = wrap(v + d, n + 1); *on = v > 0 ? 1 : 0; if (v > 0) *mode = v - 1; save_ui_config(); }
    };
    // a section : its body counted, then (open) drawn
    auto run = [&](int uid, int slot, const char* en, const char* frl, auto&& body) {
        cnt = true; nC = 0; body(); cnt = false;
        if (v2_section(K, uid, slot, en, frl, x, ry, w, nC)) { body(); v2_end(K, ry); }
    };
    auto U_ = [&](int n) { return ctrl_uid_i(base, n); };
    ry += K.px(6.0f);

    if (!adv) {   // ======== SIMPLE : unchanged (Display, Elements, then where the rest is) ========
        const bool circle = c.scStyle == 0;
        {   // ---- Display ----
            const int nMod = cfg_count_modified(SC_DISPLAY_A, NOPT(SC_DISPLAY_A), adv) + cfg_count_modified(SC_DISPLAY_B, NOPT(SC_DISPLAY_B), adv) + cfg_count_modified_bytes(&c.scScale, 4);
            if (v2_section(K, U_(1), 6, "Display", "Affichage", x, ry, w, nMod)) {
                cfg_rows(K, U_(2), SC_DISPLAY_A, NOPT(SC_DISPLAY_A), x, ry, w, adv);
                {   // the PREVIEW's sample : a lone move, or any skillchain (transient, never saved : no marker)
                    const int pv = (c.scPreview >= -1 && c.scPreview < SCP_N) ? c.scPreview : SCP_Fusion;
                    const char* nm = pv < 0 ? tr("Opening move", "WS d'ouverture") : scprop_name(pv);
                    if (int d = cfg_select_row(K, U_(3), x, ry, w, tr("Preview", "Aper\xC3\xA7u"), nm, nullptr, nullptr, 0)) c.scPreview = wrap(pv + 1 + d, SCP_N + 1) - 1;
                }
                pct(U_(4), circle ? tr("Sphere size", "Taille de la sph\xC3\xA8re") : tr("Size", "Taille"), &c.scScale, 0.5f, circle ? 2.5f : 2.0f, nullptr);   // (Classic draws up to 200 %)
                cfg_rows(K, U_(5), SC_DISPLAY_B, NOPT(SC_DISPLAY_B), x, ry, w, adv);
                v2_end(K, ry);
            }
        }
        {   // ---- Elements (which lines to show) ----
            const int nMod = cfg_count_modified(SC_ELEM_A, NOPT(SC_ELEM_A), adv) + cfg_count_modified(SC_ELEM_B, NOPT(SC_ELEM_B), adv) + cfg_count_modified_bytes(&c.scProps, 4);
            if (v2_section(K, U_(12), 7, "Elements", "\xC3\x89l\xC3\xA9ments", x, ry, w, nMod)) {
                cfg_rows(K, U_(13), SC_ELEM_A, NOPT(SC_ELEM_A), x, ry, w, adv);
                const char* pl = circle ? tr("Burst element icons", "Ic\xC3\xB4nes des \xC3\xA9l\xC3\xA9ments") : tr("Property + elements", "Propri\xC3\xA9t\xC3\xA9 + \xC3\xA9l\xC3\xA9ments");
                if (cfg_toggle_row(K, U_(14), x, ry, w, pl, c.scProps != 0, nullptr, &c.scProps, 4)) { c.scProps = c.scProps ? 0 : 1; save_ui_config(); }
                cfg_rows(K, U_(15), SC_ELEM_B, NOPT(SC_ELEM_B), x, ry, w, adv);
                v2_end(K, ry);
            }
        }
        v2_simple_note(K, x, ry);
        g_fade = e;
        return;
    }

    // ======== ADVANCED : sections by element ; the STYLE decides which sections / rows exist ========
    run(U_(40), 6, "Display", "Affichage", [&] {
        tog(U_(41), tr("Show", "Afficher"), &c.scShow, nullptr);
        sel(U_(42), tr("Style", "Style"), &c.scStyle, SC_ST_EN, SC_ST_FR, 2, nullptr);
        if (!cnt) {   // the PREVIEW's sample : a lone move, or any skillchain (transient, never saved : no marker)
            const int pv = (c.scPreview >= -1 && c.scPreview < SCP_N) ? c.scPreview : SCP_Fusion;
            const char* nm = pv < 0 ? tr("Opening move", "WS d'ouverture") : scprop_name(pv);
            if (int d = cfg_select_row(K, U_(43), x, ry, w, tr("Preview", "Aper\xC3\xA7u"), nm, nullptr, nullptr, 0)) c.scPreview = wrap(pv + 1 + d, SCP_N + 1) - 1;
        }
        const bool cir = c.scStyle == 0;
        pct(U_(44), cir ? tr("Sphere size", "Taille de la sph\xC3\xA8re") : tr("Size", "Taille"), &c.scScale, 0.5f, cir ? 2.5f : 2.0f, nullptr);   // (Classic draws up to 200 %)
        rows(U_(45), SC_GROW, NOPT(SC_GROW));
    });
    const bool circle = c.scStyle == 0;   // read AFTER Display : a Style change this frame lays the rest out for the new style

    // ---- Frame : the Classic box's chrome, or the Circle's LIST frame (the sphere has none : no list, no frame) ----
    if (!circle || c.scList) {
        BoxStyle& bs = circle ? c.scListBox : c.scBox;
        const FrameRefs fr = { &bs.on, &bs.border, &bs.alpha, &bs.alphaOwn, &bs.themeCopy, &bs.theme, &bs.hue, &bs.lum };
        if (v2_section(K, U_(46), 20, "Frame", "Cadre", x, ry, w, cfg_frame_modified(fr))) {
            if (circle) cfg_note(K, x, ry, tr("The frame of the weapon skill list (off : a faint shade).", "Le cadre de la liste des WS (sans : une l\xC3\xA9g\xC3\xA8re ombre)."));
            cfg_frame_rows(K, U_(47), x, ry, w, fr);
            v2_end(K, ry);
        }
    }

    // ---- the box itself : the Circle's SPHERE (title above it, the timer and TP in its medallion, the burst icons round
    // its ring), or the Classic box's LINES ----
    if (circle) run(U_(50), 7, "Sphere", "Sph\xC3\xA8re", [&] {
        tog(U_(51), tr("Title", "Titre"), &c.scTitle, nullptr);
        off(U_(52), tr("Title height", "Hauteur du titre"), &c.scTitleOff, -120.0f, 80.0f, nullptr);   // also places an "above" popup : never greyed
        tog(U_(53), tr("Timer (Go! / Burst)", "Timer (Go! / Burst)"), &c.scTimer, nullptr);
        tog(U_(54), tr("Burst element icons", "Ic\xC3\xB4nes des \xC3\xA9l\xC3\xA9ments"), &c.scProps, nullptr);
        tog(U_(55), tr("TP indicator", "Indicateur TP"), &c.scTP, nullptr);
    });
    else run(U_(56), 7, "Lines", "Lignes", [&] {
        tog(U_(57), tr("Title", "Titre"), &c.scTitle, nullptr);
        tog(U_(58), tr("Timer (Go! / Burst)", "Timer (Go! / Burst)"), &c.scTimer, nullptr);
        tog(U_(59), tr("Step line", "Ligne Step"), &c.scStep, nullptr);
        tog(U_(60), tr("Property + elements", "Propri\xC3\xA9t\xC3\xA9 + \xC3\xA9l\xC3\xA9ments"), &c.scProps, nullptr);
        tog(U_(61), tr("TP indicator", "Indicateur TP"), &c.scTP, nullptr);
    });

    // ---- the WEAPONSKILL LIST : the Circle's is one choice (Off / its shape), then that shape's own rows ; the Classic
    // box's is a line (on / off) ----
    run(U_(62), 21, "Weaponskill list", "Liste des WS", [&] {
        if (circle) {
            choice(U_(63), tr("Layout", "Disposition"), &c.scList, &c.scListStyle, SC_LS_EN, SC_LS_FR, 4);
            if (!c.scList) return;
            const int st = (c.scListStyle >= 0 && c.scListStyle < 4) ? c.scListStyle : 0;
            sel(U_(64), tr("Weapon skills", "WS propos\xC3\xA9""es"), &c.scWsScope, SC_WS_EN, SC_WS_FR, 2, nullptr);
            pct(U_(65), tr("List size", "Taille de la liste"), &c.scListScale, 0.5f, 2.0f, nullptr);
            if (st != 0) sel(U_(66), tr("Columns", "Colonnes"), &c.scListCols, SC_CO_EN, SC_CO_FR, 5, nullptr);   // Tree / Cards / Grouped (Rows has its own axis)
            if (st == 2) pct(U_(67), tr("Card size", "Taille des cartes"), &c.scCardScale, 0.6f, 1.6f, nullptr);
            if (st == 0 || st == 3) pct(U_(68), tr("WS spacing", "Espacement WS"), &c.scListGap, 0.6f, 3.0f, nullptr);   // read by Rows / Grouped only
            sel(U_(69), tr("List position", "Position de la liste"), &c.scListPos, SC_LP_EN, SC_LP_FR, 5, nullptr);
            off(U_(70), tr("List distance", "Distance de la liste"), &c.scListOff, -200.0f, 80.0f, nullptr);   // < 0 : closer, even over the sphere
            tog(U_(71), tr("Element icons in the list", "Ic\xC3\xB4nes d'\xC3\xA9l\xC3\xA9ment dans la liste"), &c.scListElem, nullptr);
            pct(U_(72), tr("List icons size", "Taille des ic\xC3\xB4nes (liste)"), &c.scListIcon, 0.5f, 2.0f, sw_licon(c));
        } else {
            tog(U_(73), tr("Weaponskill list", "Liste des WS"), &c.scList, nullptr);
            sel(U_(74), tr("Weapon skills", "WS propos\xC3\xA9""es"), &c.scWsScope, SC_WS_EN, SC_WS_FR, 2, sw_list(c));
            pct(U_(75), tr("WS spacing", "Espacement WS"), &c.scListGap, 0.6f, 3.0f, sw_list(c));
        }
    });

    // ---- the POPUP on the sphere (the Circle's : your WS / your magic burst ; the Arcade WS module keeps its own) ----
    if (circle) run(U_(76), 26, "WS / Magic Burst popup", "Popup WS / Magic Burst", [&] {
        {   // one choice : Off / your WS / your Magic Burst / both (scArcade, scMbFx)
            int* mark = dI(&c.scArcade) ? &c.scArcade : &c.scMbFx;
            if (cnt) nC += dI(mark);
            else {
                const char* lab[4] = { tr("Off", "D\xC3\xA9sactiv\xC3\xA9"), tr(SC_PU_EN[0], SC_PU_FR[0]), tr(SC_PU_EN[1], SC_PU_FR[1]), tr(SC_PU_EN[2], SC_PU_FR[2]) };
                const int v = (c.scArcade ? 1 : 0) + (c.scMbFx ? 2 : 0);
                if (int d = cfg_select_row(K, U_(77), x, ry, w, tr("Popup", "Popup"), lab[v], nullptr, mark, 4)) {
                    const int nv = wrap(v + d, 4); c.scArcade = (nv & 1) ? 1 : 0; c.scMbFx = (nv & 2) ? 1 : 0; save_ui_config(); }
            }
        }
        if (!c.scArcade && !c.scMbFx) return;
        sel(U_(78), tr("Popup style", "Style du popup"), &c.scHitStyle, SC_HS_EN, SC_HS_FR, 4, nullptr);
        if (cnt) nC += dI(&c.scHitFont);
        else {   // the font (drawn heavy, like the Arcade WS popup)
            const int nf = ui_font_count() < 1 ? 1 : ui_font_count();
            const int fc = (c.scHitFont < 0 || c.scHitFont >= nf) ? 0 : c.scHitFont;
            if (int d = cfg_select_row(K, U_(79), x, ry, w, tr("Font", "Police"), fc == 0 ? tr("Impact (default)", "Impact (d\xC3\xA9""faut)") : ui_font_label(fc), nullptr, &c.scHitFont, 4)) {
                c.scHitFont = wrap(fc + d, nf); save_ui_config(); }
        }
        pct(U_(80), tr("Text size", "Taille du texte"), &c.scHitScale, 0.5f, 2.0f, nullptr);
        tog(U_(81), tr("Impact effects", "Effets d'impact"), &c.scHitFx, nullptr);
        sel(U_(82), tr("Position", "Position"), &c.scHitPos, SC_HP_EN, SC_HP_FR, 5, nullptr);
        off(U_(83), tr("Move left / right", "D\xC3\xA9placer gauche / droite"), &c.scHitX, -200.0f, 200.0f, nullptr);
        off(U_(84), tr("Move up / down", "D\xC3\xA9placer haut / bas"), &c.scHitY, -200.0f, 200.0f, nullptr);
        off(U_(85), tr("Name line height", "Hauteur de la ligne du nom"), &c.scHitNameY, -80.0f, 80.0f, nullptr);   // the WS name / "MAGIC BURST" line
        off(U_(86), tr("Damage height", "Hauteur des d\xC3\xA9g\xC3\xA2ts"), &c.scHitDmgY, -80.0f, 80.0f, nullptr);
        if (c.scMbFx) {   // the Magic Burst's own lines
            tog(U_(87), tr("\"MAGIC BURST\" line", "Ligne \"MAGIC BURST\""), &c.scMbLabel, nullptr);
            tog(U_(88), tr("Burst spell name", "Nom du sort burst\xC3\xA9"), &c.scMbName, nullptr);
        }
        if (c.scArcade) {   // the colours are the WS popup's (a burst takes its element's) : which one, then its picker
            unsigned* col[3] = { &c.scHitNameCol, &c.scHitDmgCol1, &c.scHitDmgCol2 };
            const int k = (s_scColPick < 0 || s_scColPick > 2) ? 0 : s_scColPick;
            if (cnt) nC += dI(col[k]);
            else {
                const char* cn[3] = { tr("WS name", "Nom de la WS"), tr("Damage A", "D\xC3\xA9g\xC3\xA2ts A"), tr("Damage B", "D\xC3\xA9g\xC3\xA2ts B") };
                if (int d = cfg_select_row(K, U_(89), x, ry, w, tr("Colour", "Couleur"), cn[k], nullptr, col[k], 4)) s_scColPick = wrap(k + d, 3);
                cfg_color_row(K, U_(90), x, ry, w, col[(s_scColPick < 0 || s_scColPick > 2) ? 0 : s_scColPick]);
            }
            if (c.scMbFx) note(tr("A burst's damage takes the colour of its element.", "Les d\xC3\xA9g\xC3\xA2ts d'un burst prennent la couleur de son \xC3\xA9l\xC3\xA9ment."));
        }
    });

    // ---- Behaviour : whose chains show ; the Circle's animations (what it costs) ----
    run(U_(91), 22, "Behaviour", "Comportement", [&] {
        tog(U_(92), tr("Show party/nearby chains", "Afficher SC du groupe/proches"), &c.scNearby, nullptr);
        if (!circle) return;
        tog(U_(93), tr("Animations", "Animations"), &c.scAnim, nullptr);
        tog(U_(94), tr("Scene distortion", "D\xC3\xA9""formation du d\xC3\xA9""cor"), &c.scDistort, sw_anim(c));
        pct(U_(95), tr("Particles", "Particules"), &c.scParticles, 0.0f, 1.0f, sw_anim(c));
        pct(U_(96), tr("Effect reach", "Port\xC3\xA9""e des effets"), &c.scReach, 0.3f, 1.0f, sw_anim(c));
    });

    {   // ---- Text : the Circle has no Step line (Property = the list's skillchain names, WS list = its moves) ----
        const char* lab[SC_TE_COUNT]; TextStyle* ts[SC_TE_COUNT]; int idx[SC_TE_COUNT]; int n = 0, sel = 0;
        int cur = (cfgScTextElem_ < 0 || cfgScTextElem_ >= SC_TE_COUNT) ? 0 : cfgScTextElem_;
        if (circle && cur == SC_STEP) cur = SC_TIMER;
        const char* LB[SC_TE_COUNT] = { tr("Title", "Titre"), tr("Timer", "Timer"), tr("Step", "Step"),
                                        circle ? tr("List : skillchain", "Liste : skillchain") : tr("Property", "Propri\xC3\xA9t\xC3\xA9"),
                                        circle ? tr("List : moves", "Liste : WS") : tr("WS list", "Liste WS") };
        for (int k = 0; k < SC_TE_COUNT; ++k) {
            if (circle && k == SC_STEP) continue;
            if (k == cur) sel = n;
            lab[n] = LB[k]; ts[n] = &c.scText[k]; idx[n] = k; ++n;
        }
        if (v2_section(K, U_(97), 5, "Text", "Texte", x, ry, w, cfg_text_modified(ts, n))) {
            cfg_text_rows(K, U_(98), x, ry, w, lab, ts, n, &sel);
            cfgScTextElem_ = idx[(sel < 0 || sel >= n) ? 0 : sel];
            v2_end(K, ry);
        }
    }
    g_fade = e;
}

#undef NOPT

} // namespace aio
