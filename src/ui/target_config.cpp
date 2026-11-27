// target_config.cpp -- the Target module's settings panel (//aio config -> "Cible").
//
// Split out of config_page.cpp so the Target module OWNS its config (its own file). Each control is
// keyed by CTRL_ID (file:line hash, see config_controls.h) -- no hand-numbered uids. Defined as
// a ConfigPage method -> full access to the per-module edit state (catOpen_[4..9], cfgTgtTextElem_).
//
// The page's draw() hands us its running layout cursor by reference (ry/ri advance as rows are laid
// out) plus the column geometry ; the shared ROW_BAND/ROW_NEXT macros are re-established locally so
// the rows read exactly as they did inline. Pure move out of config_page.cpp -- no behaviour change.
#include "ui/config_page.h"
#include "ui/config_controls.h"
#include "ui/cfg_engine.h"     // the redesign : OptDef tables + the kit (draw_target_v2)   // shared toolkit : cat_header / row_slider / toggle_chip / row_selector + palette + g_fade
#include "ui/config_rows.h"       // ROW_BAND / ROW_NEXT row-layout macros (shared with config_page.cpp)
#include "model/ui_config.h"      // ui_config(), save/reset, TGT_* enum, TextStyle
#include "gfx/font.h"
#include "gfx/draw.h"
#include "gfx/window.h"           // box themes : window_theme_family/variant/name, box_family_*, box_hue_*
#include <cmath>
#include <cstdio>
#include <cstring>

namespace aio {

// draw the Target category (+ its Box/Bars/Detail/Debuffs/Text sub-sections). Advances ry/ri.
// e = the page's open-fade base ; bandX/bandW = row-band rect ; coX/ctrlW = control column ;
// hdrX/hdrW = category-header rect. Mirrors the party branch's local environment 1:1.
void ConfigPage::draw_target_config(u32 dev, Font* fo, const MouseState* mo, bool click,
                                    float& ry, int& ri, float e,
                                    float bandX, float bandW, float coX, float ctrlW,
                                    float hdrX, float hdrW) {
    // ROW_BAND / ROW_NEXT come from config_rows.h (dev/bandX/bandW/e/ry/ri/anim_ are all in scope here).
    // ===== SIMPLE : two cards, always open -- what the box shows and its size. Everything else (frame, text, bar
    //       sizes, the debuff box's own look...) is Advanced : the same fields, nothing lost (2026-11-26) =====
    if (!ui_config().cfgAdv) {
        UiConfig& c = ui_config();
        SIMPLE_CARD(tr("Display", "Affichage"))
            ROW_TOGGLE_G(CTRL_ID, tr("Show", "Afficher"), c.tgtShow, 52.0f, 40.0f, 112.0f)
            { ROW_BAND(46.0f)
                const float lo = 0.50f, hi = 2.00f; char b[16]; sprintf(b, "%d%%", (int)(c.tgtScale * 100.0f + 0.5f));
                float v01 = clampf((c.tgtScale - lo) / (hi - lo), 0.0f, 1.0f);
                if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Size", "Taille"), b, &v01)) {
                    float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f; c.tgtScale = v < lo ? lo : (v > hi ? hi : v); }
            } ROW_NEXT(46.0f)
            ROW_NOTE(tr("To move it : Edit Layout (top tab).", "Pour la d\xC3\xA9placer : onglet \xC3\x89""diter dispo, en haut."))
        SIMPLE_CARD_END
        SIMPLE_CARD(tr("What it shows", "Ce qu'elle affiche"))
            ROW_TOGGLE_G(CTRL_ID, tr("Distance to the target", "Distance de la cible"), c.tgtRange, 52.0f, 40.0f, 112.0f)
            ROW_TOGGLE_G(CTRL_ID, tr("The spell it casts", "Le sort qu'elle lance"), c.tgtCast, 52.0f, 40.0f, 112.0f)
            ROW_TOGGLE_G(CTRL_ID, tr("Its debuffs", "Ses debuffs"), c.tgtDebuffs, 52.0f, 40.0f, 112.0f)
            if (c.tgtDebuffs) {
                ROW_CHOICE_G(CTRL_ID, tr("Debuffs shown", "Debuffs affich\xC3\xA9s"), c.dbShow, tr("In their own box", "Dans leur propre box"), tr("In the target box", "Dans la box cible"), 52.0f, 40.0f, 190.0f) }
            ROW_TOGGLE_G(CTRL_ID, tr("Its speed (slowed / hasted)", "Sa vitesse (ralentie / acc\xC3\xA9l\xC3\xA9r\xC3\xA9""e)"), c.tgtSpeed, 52.0f, 40.0f, 112.0f)
            ROW_TOGGLE_G(CTRL_ID, tr("Treasure Hunter", "Treasure Hunter"), c.tgtTH, 52.0f, 40.0f, 112.0f)
        SIMPLE_CARD_END
        SIMPLE_MORE
        return;
    }
        // ===== sub-section : DISPLAY (box chrome + size + placement). No outer "Target" wrapper -- opens straight
        //       into its sub-sections like every other module (the sidebar already names the module). =====
        // The section FOLDS : cat_fold owns the eased progress, the clip and the cursor (config_controls.h).
{
        const float aF6_ = cat_fold(CTRL_ID, catOpen_[6]);
        cat_panel(dev, hdrX, ry, hdrW, cat_card_h(catH_[6], aF6_));   // the section IS a card, collapsed or not
        if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Display", "Affichage"), catOpen_[6], aF6_)) catOpen_[6] = !catOpen_[6];
        ROW_NEXT(42.0f)
        if (aF6_ > 0.0f) {
            const float top6_ = ry;
            cat_fold_clip(dev, hdrX, top6_, hdrW, catH_[6] * aF6_);
        // Show : master on/off for the WHOLE Target module (hidden everywhere when off ; other rows stay editable).
        ROW_TOGGLE_G(CTRL_ID, tr("Show", "Afficher"), ui_config().tgtShow, 52.0f, 40.0f, 150.0f)
        // Size : scale the whole target box (canonical : right after Show, before the Box chrome).
        { ROW_BAND(46.0f)
            const float lo = 0.50f, hi = 2.00f;
            char szbuf[16]; sprintf(szbuf, "%d%%", (int)(ui_config().tgtScale * 100.0f + 0.5f));
            float v01 = (ui_config().tgtScale - lo) / (hi - lo); v01 = v01 < 0.0f ? 0.0f : (v01 > 1.0f ? 1.0f : v01);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Size", "Taille"), szbuf, &v01)) {
                float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f;
                ui_config().tgtScale = v < lo ? lo : (v > hi ? hi : v); }
        }
        ROW_NEXT(46.0f)
        // Centre-lock hint : centring is done by DRAGGING the box to the screen centre in Edit Layout -- it snaps
        // and stays centred (so it survives a resolution change). No button ; edit-mode drag is the control.
        { ROW_BAND(34.0f)
            fo->begin(dev);
            const char* h = (ui_config().tgtCenterH || ui_config().tgtCenterV)
                ? tr("Centred (drag it off to free).", "Centré (glisse-la pour libérer).")
                : tr("Drag to the screen centre in Edit Layout to centre.", "Glisse au centre en Éditer dispo pour centrer.");
            fo->draw_lc(dev, coX + snap(4.0f), ry + yo + snap(14.0f), h, snap(11.0f), fa(C_MUTE), fa(C_STROKE), 1.0f);
        }
        ROW_NEXT(34.0f)
            cat_fold_end(dev, ry, top6_, catH_[6], aF6_);
        }   // end sub-section Box / Frame (catOpen_[6])
        ry += snap(16.0f);                                 // air between this section and the next title bar
}
    // ===== sub-section : FRAME (the config grammar : Display / Frame / Content / Behaviour / Text, docs/architecture/config-panels.md) =====
{
    const float aF20_ = cat_fold(CTRL_ID, catOpen_[20]);
    cat_panel(dev, hdrX, ry, hdrW, cat_card_h(catH_[20], aF20_));
    if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Frame", "Cadre"), catOpen_[20], aF20_)) catOpen_[20] = !catOpen_[20];
    ROW_NEXT(42.0f)
    if (aF20_ > 0.0f) {
        const float top20_ = ry;
        cat_fold_clip(dev, hdrX, top20_, hdrW, catH_[20] * aF20_);
        // Box : draw the frame chrome or not. NO box -> name/%/bar/buffs/timer float with a heavy outline, and the
        // theme rows below are hidden (no chrome to theme).
        ROW_CHOICE_G(CTRL_ID, tr("Box", "Cadre"), ui_config().tgtBox, tr("On", "Oui"), tr("None", "Aucun"), 52.0f, 40.0f, 150.0f)
        if (ui_config().tgtBox) {
        // Border : the frame's edges + corners (None = background only), like every other box
        ROW_CHOICE_G(CTRL_ID, tr("Border", "Bordure"), ui_config().tgtBorder, tr("On", "Oui"), tr("None", "Aucune"), 52.0f, 40.0f, 150.0f)
        // Transparence : how see-through the box chrome is (the content stays opaque). 0% = solid.
        // Transparency : the Party box's (default) or this box's own -- the same chip as Theme ; the slider shows the
        // EFFECTIVE value and moving it makes it this box's own (2026-11-17)
        ROW_CHOICE_G(CTRL_ID, tr("Transparency", "Transparence"), ui_config().tgtAlphaOwn, tr("Custom", "Perso"), tr("Same as Party", "Comme Party"), 52.0f, 40.0f, 150.0f)
        { ROW_BAND(46.0f)
            UiConfig& uc = ui_config();
            const float transp = 1.0f - (uc.tgtAlphaOwn ? uc.tgtBoxAlpha : uc.skinBoxAlpha); char b[16]; sprintf(b, "%d%%", (int)(transp * 100.0f + 0.5f));
            float v01 = clampf(transp, 0.0f, 1.0f);   // full 0..100% range
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Box transparency", "Transparence du cadre"), b, &v01)) {
                uc.tgtBoxAlpha = 1.0f - v01; uc.tgtAlphaOwn = 1; }
        }
        ROW_NEXT(46.0f)
        // Copy Party : follow the party box theme (hides the own-theme rows below).
        ROW_CHOICE_G(CTRL_ID, tr("Theme", "Thème"), ui_config().tgtThemeCopy, tr("Same as Party", "Comme Party"), tr("Custom", "Perso"), 52.0f, 40.0f, 150.0f)
        if (!ui_config().tgtThemeCopy) {
        // Box Theme : a FAMILY selector + a variant grid, INDEPENDENT of the party theme (tgtTheme).
        { ROW_BAND(52.0f)
          const int fam = window_theme_family(ui_config().tgtTheme), var = window_theme_variant(ui_config().tgtTheme);
          if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Box Theme", "Thème de cadre"), box_family_name(fam))) {   // keyed by CTRL_ID
              ui_config().tgtTheme = window_theme_index(wrap(fam + d, box_family_count()), var); save_ui_config(); } }
        ROW_NEXT(52.0f)
        // procedural family : a free CUSTOM hue -- a toggle that swaps the preset swatches for the HSV picker.
        if (window_theme_family(ui_config().tgtTheme) != 0) { ROW_BAND(48.0f)
            const float rowH = snap(38.0f), ty = ry + yo; fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, tr("Custom colour", "Couleur perso"), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
            const float bbw = snap(112.0f), bbh = snap(34.0f), bx2 = coX + ctrlW - bbw, bty = ty + (rowH - bbh) * 0.5f;
            const bool on = ui_config().tgtHue != 0;
            if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx2, bty, bbw, bbh, on ? tr("On", "Oui") : tr("Off", "Non"), on)) {
                ui_config().tgtHue = on ? 0u : (box_hue_color(window_theme_variant(ui_config().tgtTheme)) | 0xFF000000u); save_ui_config(); }
            ROW_NEXT(48.0f)
        }
        if (window_theme_family(ui_config().tgtTheme) != 0 && ui_config().tgtHue != 0) {
            CFG_COLOR_PICKER(&ui_config().tgtHue)
        } else
        {   // variant grid : FFXI -> theme-number chips ; procedural family -> hue swatches (click to pick)
          const int fam = window_theme_family(ui_config().tgtTheme), var = window_theme_variant(ui_config().tgtTheme);
          float slotH = 0.0f;
          {   // the height has to be known BEFORE ROW_BAND, so it is computed from the same rule the grid uses
              const int nr = (box_hue_count() + 14) / 15;
              slotH = (fam == 0) ? snap(52.0f)   // FFXI : a selector row, like every other setting
                                : (float)nr * snap(22.0f) + (float)(nr - 1) * snap(7.0f) + snap(20.0f); }
          ROW_BAND(slotH) (void)yo;
          {   float sh_ = slotH;
              const int pick = theme_grid(dev, fo, mo, click, CTRL_ID, coX, ry, ctrlW, fam, var, sh_);
              if (pick >= 0) { ui_config().tgtTheme = window_theme_index(fam, pick); save_ui_config(); } }
          ROW_NEXT(slotH)
        }
        // Luminosity : darken / lighten the chosen box-theme colour (procedural families only).
        if (window_theme_family(ui_config().tgtTheme) != 0)
        { ROW_BAND(46.0f)
            float v01 = (ui_config().tgtLum + 1.0f) * 0.5f; v01 = clampf(v01, 0.0f, 1.0f);
            const int pct = (int)(ui_config().tgtLum * 100.0f + (ui_config().tgtLum >= 0.0f ? 0.5f : -0.5f));
            char b[16]; sprintf(b, "%+d%%", pct);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Luminosity", "Luminosité"), b, &v01)) {
                ui_config().tgtLum = v01 * 2.0f - 1.0f; }
          ROW_NEXT(46.0f)
        }
        }   // end own-theme rows (!tgtThemeCopy)
        }   // end theme rows (tgtBox on)
        cat_fold_end(dev, ry, top20_, catH_[20], aF20_);
    }   // end Frame
    ry += snap(16.0f);
}
        // ---- sub-section : Bars ----
        // The section FOLDS : cat_fold owns the eased progress, the clip and the cursor (config_controls.h).
{
        const float aF7_ = cat_fold(CTRL_ID, catOpen_[7]);
        cat_panel(dev, hdrX, ry, hdrW, cat_card_h(catH_[7], aF7_));   // the section IS a card, collapsed or not
        if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Bars", "Barres"), catOpen_[7], aF7_)) catOpen_[7] = !catOpen_[7];
        ROW_NEXT(42.0f)
        if (aF7_ > 0.0f) {
            const float top7_ = ry;
            cat_fold_clip(dev, hdrX, top7_, hdrW, catH_[7] * aF7_);
        // Bar Height : the HP fiole height.
        { ROW_BAND(46.0f)
            const float lo = 0.60f, hi = 2.00f; char b[16]; sprintf(b, "%d%%", (int)(ui_config().tgtBarH * 100.0f + 0.5f));
            float v01 = (ui_config().tgtBarH - lo) / (hi - lo); v01 = clampf(v01, 0.0f, 1.0f);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Bar Height", "Hauteur barre"), b, &v01)) {
                float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f; ui_config().tgtBarH = v < lo ? lo : (v > hi ? hi : v); }
        }
        ROW_NEXT(46.0f)
        // Bar Width : the HP fiole width as a fraction of the box (centred).
        { ROW_BAND(46.0f)
            const float lo = 0.40f, hi = 1.00f; char b[16]; sprintf(b, "%d%%", (int)(ui_config().tgtBarW * 100.0f + 0.5f));
            float v01 = (ui_config().tgtBarW - lo) / (hi - lo); v01 = clampf(v01, 0.0f, 1.0f);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Bar Width", "Largeur barre"), b, &v01)) {
                float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f; ui_config().tgtBarW = v < lo ? lo : (v > hi ? hi : v); }
        }
        ROW_NEXT(46.0f)
        // Distance Height : the range gauge height (like Bar Height for the HP bar). Gauge mode only (minimal = just a number).
        if (ui_config().tgtRange && !ui_config().tgtRangeMin)
        { ROW_BAND(46.0f)
            const float lo = 0.60f, hi = 2.00f; char b[16]; sprintf(b, "%d%%", (int)(ui_config().tgtRangeH * 100.0f + 0.5f));
            float v01 = (ui_config().tgtRangeH - lo) / (hi - lo); v01 = clampf(v01, 0.0f, 1.0f);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Distance bar height", "Hauteur de la jauge distance"), b, &v01)) {   // 213 : distinct from the Text "Size" slider (both were 210 -> dragging one wrote the other)
                float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f; ui_config().tgtRangeH = v < lo ? lo : (v > hi ? hi : v); }
          ROW_NEXT(46.0f)
        }
            cat_fold_end(dev, ry, top7_, catH_[7], aF7_);
        }   // end sub-section Bars (catOpen_[7])
        ry += snap(16.0f);                                 // air between this section and the next title bar
}
        // ---- sub-section : Detail ----
        // The section FOLDS : cat_fold owns the eased progress, the clip and the cursor (config_controls.h).
{
        const float aF8_ = cat_fold(CTRL_ID, catOpen_[8]);
        cat_panel(dev, hdrX, ry, hdrW, cat_card_h(catH_[8], aF8_));   // the section IS a card, collapsed or not
        if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Detail", "Détail"), catOpen_[8], aF8_)) catOpen_[8] = !catOpen_[8];
        ROW_NEXT(42.0f)
        if (aF8_ > 0.0f) {
            const float top8_ = ry;
            cat_fold_clip(dev, hdrX, top8_, hdrW, catH_[8] * aF8_);
        // Name colour by hostility : red = claimed by another, orange = engaged/in combat.
        { ROW_BAND(52.0f)
            const float rowH = snap(40.0f), ty = ry + yo;
            fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, tr("Name colour", "Couleur du nom"), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
            const float bbw = snap(140.0f), bbh = snap(34.0f), bx2 = coX + ctrlW - bbw, bty = ty + (rowH - bbh) * 0.5f;
            if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx2, bty, bbw, bbh, ui_config().tgtNameHostile ? tr("By hostility", "Par hostilité") : tr("Fixed", "Fixe"), ui_config().tgtNameHostile != 0)) { ui_config().tgtNameHostile = !ui_config().tgtNameHostile; save_ui_config(); }
        }
        ROW_NEXT(52.0f)
        // Sub-target (<st>) box : on / off (the audit 2026-11-26 : it could not be turned off), then its PLACEMENT
        ROW_TOGGLE_G(CTRL_ID, tr("Sub-target box (<st>)", "Box de sous-cible (<st>)"), ui_config().tgtSub, 52.0f, 40.0f, 112.0f)
        if (ui_config().tgtSub) { ROW_BAND(52.0f)
            const char* SUBPOS[6] = { tr("Above-Right", "Haut-Droite"), tr("Above-Left", "Haut-Gauche"),
                                      tr("Below-Right", "Bas-Droite"),  tr("Below-Left", "Bas-Gauche"),
                                      tr("Right", "Droite"),            tr("Left", "Gauche") };
            int sp = (ui_config().tgtSubPos < 0 || ui_config().tgtSubPos >= 6) ? 0 : ui_config().tgtSubPos;
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Sub-target", "Sous-cible"), SUBPOS[sp])) {
                ui_config().tgtSubPos = wrap(sp + d, 6); save_ui_config(); }
          ROW_NEXT(52.0f)
        }
        // Speed : show the target's movement speed % (green = slower, red = faster) in the detail row.
        ROW_TOGGLE_G(CTRL_ID, tr("Speed", "Vitesse"), ui_config().tgtSpeed, 52.0f, 40.0f, 112.0f)
        // Speed display : plain text ("Spd +12%") or the buff-atlas Speed icon + the value.
        if (ui_config().tgtSpeed) { ROW_BAND(52.0f)
            const char* SDL[2] = { tr("Text", "Texte"), tr("Icon", "Icône") };
            int sm = ui_config().tgtSpeedIcon ? 1 : 0;
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Speed display", "Affichage vitesse"), SDL[sm])) {
                ui_config().tgtSpeedIcon = wrap(sm + d, 2); save_ui_config(); }
            ROW_NEXT(52.0f) }
        // Speed/TH icon size : only relevant when Speed or TH is shown as an ICON (not text).
        if ((ui_config().tgtSpeed && ui_config().tgtSpeedIcon) || (ui_config().tgtTH && ui_config().tgtThIcon))
        { ROW_BAND(46.0f)
            const float lo = 0.50f, hi = 3.00f; char b[16]; sprintf(b, "%d%%", (int)(ui_config().tgtDetailIconSz * 100.0f + 0.5f));
            float v01 = (ui_config().tgtDetailIconSz - lo) / (hi - lo); v01 = clampf(v01, 0.0f, 1.0f);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Speed / TH icon size", "Taille ic\xC3\xB4nes vitesse / TH"), b, &v01)) {
                float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f; ui_config().tgtDetailIconSz = v < lo ? lo : (v > hi ? hi : v); }
          ROW_NEXT(46.0f)
        }
        // Treasure Hunter : show the TH level applied to the target (packet-tracked).
        ROW_TOGGLE_G(CTRL_ID, tr("Treasure Hunter", "Treasure Hunter"), ui_config().tgtTH, 52.0f, 40.0f, 112.0f)
        // TH display : plain text ("TH9") or the coffer icon + the level.
        if (ui_config().tgtTH) { ROW_BAND(52.0f)
            const char* HDL[2] = { tr("Text", "Texte"), tr("Icon", "Icône") };
            int hm = ui_config().tgtThIcon ? 1 : 0;
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("TH display", "Affichage TH"), HDL[hm])) {
                ui_config().tgtThIcon = wrap(hm + d, 2); save_ui_config(); }
            ROW_NEXT(52.0f) }
        // Distance : the range gauge (Melee/WS/Magic/Ranged/Enmity on a mob ; Trade/AoE/Cast on an ally).
        { ROW_BAND(52.0f)
            const float rowH = snap(40.0f), ty = ry + yo;
            fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, tr("Distance", "Distance"), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
            const float bbw = snap(112.0f), bbh = snap(34.0f), bx2 = coX + ctrlW - bbw, bty = ty + (rowH - bbh) * 0.5f;
            if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx2, bty, bbw, bbh, ui_config().tgtRange ? tr("On", "Oui") : tr("Off", "Non"), ui_config().tgtRange != 0)) { ui_config().tgtRange = !ui_config().tgtRange; save_ui_config(); }
        }
        ROW_NEXT(52.0f)
        // Distance display : the full GAUGE (zones + cursor) or MINIMAL (just the number, coloured by its range zone).
        if (ui_config().tgtRange)
        { ROW_BAND(52.0f)
            const char* DDL[2] = { tr("Gauge", "Jauge"), tr("Minimal", "Minimal") };
            int dm = ui_config().tgtRangeMin ? 1 : 0;
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Distance display", "Affichage distance"), DDL[dm])) {
                ui_config().tgtRangeMin = wrap(dm + d, 2); save_ui_config(); }
          ROW_NEXT(52.0f)
        }
        // Cast : show the target's ACTION bar (its live spell cast : name + filling bar) under the HP bar.
        ROW_TOGGLE_G(CTRL_ID, tr("The spell it casts", "Le sort qu'elle lance"), ui_config().tgtCast, 52.0f, 40.0f, 112.0f)
        // Cast placeholder : a demo cast bar during normal play so it can be positioned even when not casting.
        ROW_TOGGLE_G(CTRL_ID, tr("Sample spell (to place it)", "Sort d'exemple (pour le placer)"), ui_config().tgtCastDemo, 52.0f, 40.0f, 112.0f)
            cat_fold_end(dev, ry, top8_, catH_[8], aF8_);
        }   // end sub-section Detail (catOpen_[8])
        ry += snap(16.0f);                                 // air between this section and the next title bar
}
        // ---- sub-section : Debuffs ----
        // The section FOLDS : cat_fold owns the eased progress, the clip and the cursor (config_controls.h).
{
        const float aF9_ = cat_fold(CTRL_ID, catOpen_[9]);
        cat_panel(dev, hdrX, ry, hdrW, cat_card_h(catH_[9], aF9_));   // the section IS a card, collapsed or not
        if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Debuffs", "Debuffs"), catOpen_[9], aF9_)) catOpen_[9] = !catOpen_[9];
        ROW_NEXT(42.0f)
        if (aF9_ > 0.0f) {
            const float top9_ = ry;
            cat_fold_clip(dev, hdrX, top9_, hdrW, catH_[9] * aF9_);
        // Debuffs : show the tracked debuff icons under the HP bar (grows the box downward).
        { ROW_BAND(52.0f)
            const float rowH = snap(40.0f), ty = ry + yo;
            fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, tr("Debuffs", "Debuffs"), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
            const float bbw = snap(112.0f), bbh = snap(34.0f), bx2 = coX + ctrlW - bbw, bty = ty + (rowH - bbh) * 0.5f;
            if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx2, bty, bbw, bbh, ui_config().tgtDebuffs ? tr("On", "Oui") : tr("Off", "Non"), ui_config().tgtDebuffs != 0)) { ui_config().tgtDebuffs = !ui_config().tgtDebuffs; save_ui_config(); }
        }
        ROW_NEXT(52.0f)
        // Mode : the debuffs sit INSIDE the Target box (icons under the bar) or DETACHED into their own
        // Timers-style list (mob name + icon/name/timer rows) placed with //aio edit -- like Player's
        // Equipment "In Player / Standalone".
        if (ui_config().tgtDebuffs) {   // (braced : the macro's ROW_NEXT ran even with Debuffs off -- an empty band)
        ROW_CHOICE_G(CTRL_ID, tr("Mode", "Mode"), ui_config().dbShow, tr("Standalone", "Autonome"), tr("In Target", "Dans la cible"), 52.0f, 40.0f, 126.0f) }
        // Max Debuffs : how many debuff icons to show at most (1..20 ; wraps 10 per row/column).
        if (ui_config().tgtDebuffs && !ui_config().dbShow)
        { ROW_BAND(46.0f)
            const float lo = 1.0f, hi = 32.0f; int cur = ui_config().tgtBuffMax; if (cur < 1) cur = 1; if (cur > 32) cur = 32;
            char b[16]; sprintf(b, "%d", cur);
            float v01 = ((float)cur - lo) / (hi - lo); v01 = clampf(v01, 0.0f, 1.0f);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Max Debuffs", "Max debuffs"), b, &v01)) {
                int v = (int)(lo + v01 * (hi - lo) + 0.5f); ui_config().tgtBuffMax = v < 1 ? 1 : (v > 32 ? 32 : v); }
          ROW_NEXT(46.0f)
        }
        // Buff position : where the debuff row sits -- inside the box (grows it) or floating outside on a side.
        if (ui_config().tgtDebuffs && !ui_config().dbShow)
        { ROW_BAND(52.0f)
            const char* BPL[5] = { tr("Inside", "Dans la box"), tr("Below", "Dessous"), tr("Above", "Dessus"), tr("Left", "Gauche"), tr("Right", "Droite") };
            const bool boxOff = !ui_config().tgtBox;                      // no box -> "Inside" is meaningless : offer only 1..4
            int bp = (ui_config().tgtBuffPos < 0 || ui_config().tgtBuffPos > 4) ? 0 : ui_config().tgtBuffPos;
            if (boxOff && bp == 0) bp = 1;   // shown as Below while there is no box -- the stored Inside is KEPT (the renderer falls back itself)   // coerce Inside -> Below when box off
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Debuffs position", "Position des debuffs"), BPL[bp])) {
                int nv = boxOff ? (1 + wrap(bp - 1 + d, 4)) : wrap(bp + d, 5);   // box off cycles 1..4 only
                ui_config().tgtBuffPos = nv; save_ui_config(); }
          ROW_NEXT(52.0f)   // (inside : a hidden row must not keep its band -- the Debuffs section showed two empty ones)
        }
        // Buff Size : the debuff icon size.
        if (ui_config().tgtDebuffs && !ui_config().dbShow)
        { ROW_BAND(46.0f)
            const float lo = 0.50f, hi = 2.00f; char b[16]; sprintf(b, "%d%%", (int)(ui_config().tgtIconSz * 100.0f + 0.5f));
            float v01 = (ui_config().tgtIconSz - lo) / (hi - lo); v01 = clampf(v01, 0.0f, 1.0f);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Debuff icon size", "Taille des ic\xC3\xB4nes"), b, &v01)) {
                float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f; ui_config().tgtIconSz = v < lo ? lo : (v > hi ? hi : v); }
          ROW_NEXT(46.0f)
        }
        // Debuff Timers : the countdown number under each icon (needs Debuffs on ; off = icons only).
        if (ui_config().tgtDebuffs && !ui_config().dbShow)
        { ROW_BAND(52.0f)
            const float rowH = snap(40.0f), ty = ry + yo;
            fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, tr("Time left under each", "Temps restant sous chacun"), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
            const float bbw = snap(112.0f), bbh = snap(34.0f), bx2 = coX + ctrlW - bbw, bty = ty + (rowH - bbh) * 0.5f;
            if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx2, bty, bbw, bbh, ui_config().tgtTimers ? tr("On", "Oui") : tr("Off", "Non"), ui_config().tgtTimers != 0)) { ui_config().tgtTimers = !ui_config().tgtTimers; save_ui_config(); }
          ROW_NEXT(52.0f)
        }
        cat_fold_end(dev, ry, top9_, catH_[9], aF9_);
    }   // end Debuffs
    ry += snap(16.0f);                                 // air between this section and the next title bar
}

    // ===== sub-section : DEBUFF BOX =====
    // The STANDALONE debuff list is a second BOX -- its own size, its own row count, its own frame and theme --
    // and it was living at the bottom of the section that configures the debuff icons drawn ON the target.
    // Two objects, one section: you scrolled past six settings for one box to reach eight for another.
    // It only exists in Standalone mode, so the section only exists then either -- an empty card would be a
    // promise of settings that are not there.
    if (ui_config().tgtDebuffs && ui_config().dbShow) {
{
        const float aFd_ = cat_fold(CTRL_ID, catOpen_[16]);
        cat_panel(dev, hdrX, ry, hdrW, cat_card_h(catH_[16], aFd_));
        if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Debuff box", "Bo\xC3\xAEte debuffs"), catOpen_[16], aFd_)) catOpen_[16] = !catOpen_[16];
        ROW_NEXT(42.0f)
        if (aFd_ > 0.0f) {
            const float topD_ = ry;
            cat_fold_clip(dev, hdrX, topD_, hdrW, catH_[16] * aFd_);
        // ---- STANDALONE (detached) list : its own Timers-style appearance. Shown only in Standalone mode. ----
        if (ui_config().tgtDebuffs && ui_config().dbShow) {
            UiConfig& c = ui_config();
            { ROW_BAND(46.0f)   // Size
                const float lo = 0.50f, hi = 2.00f; char b[16]; sprintf(b, "%d%%", (int)(c.dbScale * 100.0f + 0.5f));
                float v01 = (c.dbScale - lo) / (hi - lo); v01 = clampf(v01, 0.0f, 1.0f);
                if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Size", "Taille"), b, &v01)) { float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f; c.dbScale = v < lo ? lo : (v > hi ? hi : v); }
            } ROW_NEXT(46.0f)
            ROW_GROW(CTRL_ID, UiConfig::GB_DB)   // which way the box grows as its content widens (ui/box_grow.h)
            { ROW_BAND(46.0f)   // Max rows
                const float lo = 1.0f, hi = 32.0f; char b[16]; sprintf(b, "%d", c.dbMax);
                float v01 = ((float)c.dbMax - lo) / (hi - lo); v01 = clampf(v01, 0.0f, 1.0f);
                if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Max", "Max"), b, &v01)) { int v = (int)(lo + v01 * (hi - lo) + 0.5f); c.dbMax = v < 1 ? 1 : (v > 32 ? 32 : v); }
            } ROW_NEXT(46.0f)
            ROW_TOGGLE_G(CTRL_ID, tr("Header (mob name)", "En-t\xC3\xAAte (nom du mob)"), c.dbHeader, 52.0f, 40.0f, 112.0f)   // Header (mob name) toggle
            { ROW_BAND(52.0f)   // Display : Icon / Name / Icon+Name
                const char* DISP[3] = { tr("Icon", "Ic\xC3\xB4ne"), tr("Name", "Nom"), tr("Icon+Name", "Ic\xC3\xB4ne+Nom") };
                int m = (c.dbDisp < 0 || c.dbDisp > 2) ? 0 : c.dbDisp;
                if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Display", "Affichage"), DISP[m])) { c.dbDisp = wrap(m + d, 3); save_ui_config(); }
            } ROW_NEXT(52.0f)
            { ROW_BAND(46.0f)   // Icon size
                const float lo = 0.50f, hi = 2.00f; char b[16]; sprintf(b, "%d%%", (int)(c.dbIconScale * 100.0f + 0.5f));
                float v01 = (c.dbIconScale - lo) / (hi - lo); v01 = clampf(v01, 0.0f, 1.0f);
                if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Icon size", "Taille ic\xC3\xB4nes"), b, &v01)) { float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f; c.dbIconScale = v < lo ? lo : (v > hi ? hi : v); }
            } ROW_NEXT(46.0f)
            { ROW_BAND(46.0f)   // Row spacing
                const float lo = 0.60f, hi = 3.00f; char b[16]; sprintf(b, "%d%%", (int)(c.dbRowGap * 100.0f + 0.5f));
                float v01 = (c.dbRowGap - lo) / (hi - lo); v01 = clampf(v01, 0.0f, 1.0f);
                if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Row spacing", "Espacement lignes"), b, &v01)) { float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f; c.dbRowGap = v < lo ? lo : (v > hi ? hi : v); }
            } ROW_NEXT(46.0f)
            draw_box_appearance(dev, fo, mo, click, ry, ri, e, bandX, bandW, coX, ctrlW, c.dbBox);   // detached-box frame / transparency / theme
        }
            cat_fold_end(dev, ry, topD_, catH_[16], aFd_);
        }   // end Debuff box
        ry += snap(16.0f);
}
    }                                 // air between this section and the next title bar
        // ---- Typography sub-section : per-element Font / Size / Outline / Style / Colour (Name / HP% / Timer) ----
        // The section FOLDS : cat_fold owns the eased progress, the clip and the cursor (config_controls.h).
{
        const float aF5_ = cat_fold(CTRL_ID, catOpen_[5]);
        cat_panel(dev, hdrX, ry, hdrW, cat_card_h(catH_[5], aF5_));   // the section IS a card, collapsed or not
        if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Text", "Texte"), catOpen_[5], aF5_)) catOpen_[5] = !catOpen_[5];
        ROW_NEXT(42.0f)
        if (aF5_ > 0.0f) {
            const float top5_ = ry;
            cat_fold_clip(dev, hdrX, top5_, hdrW, catH_[5] * aF5_);
        // The element list ADAPTS to the debuff mode : in-box shows the in-box debuff "Timer" (tgtText[TGT_TIMER]) ;
        // DETACHED drops it and instead exposes the standalone box's own text (dbText : header / name / timer), so
        // editing here modifies the right thing. (No dead controls, no cross-wiring.)
        UiConfig& c = ui_config();
        struct TE { const char* label; TextStyle* ts; };
        TE items[TGT_TE_COUNT + DB_TE_COUNT]; int nte = 0;
        items[nte++] = { tr("Name", "Nom"),  &c.tgtText[TGT_NAME] };
        items[nte++] = { tr("HP%", "PV%"),   &c.tgtText[TGT_HP] };
        if (c.tgtDebuffs && !c.dbShow && c.tgtTimers) items[nte++] = { tr("Timer", "Minuteur"), &c.tgtText[TGT_TIMER] };   // in-box debuff timer, when shown
        if (c.tgtSpeed) items[nte++] = { tr("Speed", "Vitesse"),  &c.tgtText[TGT_SPD] };   // each only while its info shows
        if (c.tgtTH)    items[nte++] = { tr("TH", "TH"),          &c.tgtText[TGT_TH] };
        if (c.tgtRange) items[nte++] = { tr("Distance", "Distance"), &c.tgtText[TGT_RANGE] };
        if (c.tgtDebuffs && c.dbShow) {   // detached debuff box -> its own typography
            items[nte++] = { tr("Debuff header", "En-t\xC3\xAAte debuff"), &c.dbText[DB_HEADER] };
            items[nte++] = { tr("Debuff name",   "Nom debuff"),           &c.dbText[DB_NAME] };
            items[nte++] = { tr("Debuff timer",  "Timer debuff"),         &c.dbText[DB_TIMER] };
        }
        int te = (cfgTgtTextElem_ < 0 || cfgTgtTextElem_ >= nte) ? 0 : cfgTgtTextElem_;
        { ROW_BAND(52.0f)   // element selector
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Element", "\xC3\x89l\xC3\xA9ment"), items[te].label)) {
                cfgTgtTextElem_ = wrap(te + d, nte); te = cfgTgtTextElem_; }
        }
        ROW_NEXT(52.0f)
        draw_text_style(dev, fo, mo, click, ry, ri, e, bandX, bandW, coX, ctrlW, *items[te].ts, true);
            cat_fold_end(dev, ry, top5_, catH_[5], aF5_);
        }   // end Text sub-section (catOpen_[5])
        ry += snap(16.0f);
}

    #undef ROW_BAND
    #undef ROW_NEXT
}


// ======== THE NEW TARGET PAGE (the config redesign, milestone 1, 2026-11-26) ========
// every option described once (OptDef, cfg_engine.h) and drawn by the kit (cfg_kit.h) : modified marker, right-click
// reset, a dependency greys the row WITH its reason. Frame and Text are the engine's shared blocks (cfg_frame_rows,
// cfg_text_rows). //aio config v1 : the old page (draw_target_config).
#define F_(name) [](UiConfig& c) -> void* { return &c.name; }
static const char* const C_TF_EN[2] = { "Fixed", "By hostility" },            * const C_TF_FR[2] = { "Fixe", "Par hostilit\xC3\xA9" };
static const char* const C_TI_EN[2] = { "Text", "Icon" },                      * const C_TI_FR[2] = { "Texte", "Ic\xC3\xB4ne" };
static const char* const C_GM_EN[2] = { "Gauge", "Minimal" },                  * const C_GM_FR[2] = { "Jauge", "Minimal" };
static const char* const C_DB_EN[2] = { "In the target box", "In their own box" }, * const C_DB_FR[2] = { "Dans la box cible", "Dans leur propre box" };
static const char* const C_SP_EN[6] = { "Above-Right", "Above-Left", "Below-Right", "Below-Left", "Right", "Left" };
static const char* const C_SP_FR[6] = { "Haut-Droite", "Haut-Gauche", "Bas-Droite", "Bas-Gauche", "Droite", "Gauche" };
static const char* const C_BP_EN[5] = { "Inside", "Below", "Above", "Left", "Right" },  * const C_BP_FR[5] = { "Dans la box", "Dessous", "Dessus", "Gauche", "Droite" };
static const char* const C_DD_EN[3] = { "Icon", "Name", "Icon + name" },       * const C_DD_FR[3] = { "Ic\xC3\xB4ne", "Nom", "Ic\xC3\xB4ne + nom" };
// the reasons : what greys a row, said in a few words
static const char* w_debuffs(const UiConfig& c)   { return c.tgtDebuffs ? nullptr : tr("inactive : debuffs off", "inactif : debuffs coup\xC3\xA9s"); }
static const char* w_inbox(const UiConfig& c)     { return !c.tgtDebuffs ? tr("inactive : debuffs off", "inactif : debuffs coup\xC3\xA9s") : (c.dbShow ? tr("inactive : debuffs in their own box", "inactif : debuffs dans leur propre box") : nullptr); }
static const char* w_range(const UiConfig& c)     { return c.tgtRange ? nullptr : tr("inactive : distance off", "inactif : distance coup\xC3\xA9""e"); }
static const char* w_gauge(const UiConfig& c)     { return !c.tgtRange ? tr("inactive : distance off", "inactif : distance coup\xC3\xA9""e") : (c.tgtRangeMin ? tr("inactive : minimal distance", "inactif : distance minimale") : nullptr); }
static const char* w_speed(const UiConfig& c)     { return c.tgtSpeed ? nullptr : tr("inactive : speed off", "inactif : vitesse coup\xC3\xA9""e"); }
static const char* w_th(const UiConfig& c)        { return c.tgtTH ? nullptr : tr("inactive : Treasure Hunter off", "inactif : Treasure Hunter coup\xC3\xA9"); }
static const char* w_icons(const UiConfig& c)     { return (c.tgtSpeed && c.tgtSpeedIcon) || (c.tgtTH && c.tgtThIcon) ? nullptr : tr("inactive : speed and TH shown as text", "inactif : vitesse et TH en texte"); }
static const char* w_sub(const UiConfig& c)       { return c.tgtSub ? nullptr : tr("inactive : sub-target box off", "inactif : box de sous-cible coup\xC3\xA9""e"); }
static const char* w_cast(const UiConfig& c)      { return c.tgtCast ? nullptr : tr("inactive : spell off", "inactif : sort coup\xC3\xA9"); }

static const OptDef TGT_DISPLAY[] = {
    { "Show", "Afficher", OK_TOGGLE, OL_SIMPLE, F_(tgtShow), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Size", "Taille", OK_PCT, OL_SIMPLE, F_(tgtScale), 0.5f, 2.0f, nullptr, nullptr, 0, nullptr, 0 },
    { "To move it : Edit Layout (top tab). Dragged to the screen centre, it stays centred.", "Pour la d\xC3\xA9placer : onglet \xC3\x89""diter dispo. Gliss\xC3\xA9""e au centre de l'\xC3\xA9""cran, elle y reste.", OK_NOTE, OL_SIMPLE, nullptr, 0, 0, nullptr, nullptr, 0, nullptr, 0 },
};
static const OptDef TGT_SHOWS[] = {   // Simple's second card
    { "Distance to the target", "Distance de la cible", OK_TOGGLE, OL_SIMPLE, F_(tgtRange), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "The spell it casts", "Le sort qu'elle lance", OK_TOGGLE, OL_SIMPLE, F_(tgtCast), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Its debuffs", "Ses debuffs", OK_TOGGLE, OL_SIMPLE, F_(tgtDebuffs), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Debuffs shown", "Debuffs affich\xC3\xA9s", OK_SELECT, OL_SIMPLE, F_(dbShow), 0, 0, C_DB_EN, C_DB_FR, 2, w_debuffs, 0 },
    { "Its speed (slowed / hasted)", "Sa vitesse (ralentie / acc\xC3\xA9l\xC3\xA9r\xC3\xA9""e)", OK_TOGGLE, OL_SIMPLE, F_(tgtSpeed), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Treasure Hunter", "Treasure Hunter", OK_TOGGLE, OL_SIMPLE, F_(tgtTH), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
};
static const OptDef TGT_BARS[] = {
    { "Bar height", "Hauteur de la barre", OK_PCT, OL_ADV, F_(tgtBarH), 0.6f, 2.0f, nullptr, nullptr, 0, nullptr, 0 },
    { "Bar width", "Largeur de la barre", OK_PCT, OL_ADV, F_(tgtBarW), 0.4f, 1.0f, nullptr, nullptr, 0, nullptr, 0 },
    { "Distance bar height", "Hauteur de la jauge distance", OK_PCT, OL_ADV, F_(tgtRangeH), 0.6f, 2.0f, nullptr, nullptr, 0, w_gauge, 0 },
};
static const OptDef TGT_DETAIL[] = {
    { "Name colour", "Couleur du nom", OK_SELECT, OL_ADV, F_(tgtNameHostile), 0, 0, C_TF_EN, C_TF_FR, 2, nullptr, 0 },
    { "Sub-target box (<st>)", "Box de sous-cible (<st>)", OK_TOGGLE, OL_ADV, F_(tgtSub), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Sub-target position", "Position de la sous-cible", OK_SELECT, OL_ADV, F_(tgtSubPos), 0, 0, C_SP_EN, C_SP_FR, 6, w_sub, 0 },
    { "Speed", "Vitesse", OK_TOGGLE, OL_ADV, F_(tgtSpeed), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Speed display", "Affichage de la vitesse", OK_SELECT, OL_ADV, F_(tgtSpeedIcon), 0, 0, C_TI_EN, C_TI_FR, 2, w_speed, 0 },
    { "Treasure Hunter", "Treasure Hunter", OK_TOGGLE, OL_ADV, F_(tgtTH), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "TH display", "Affichage du TH", OK_SELECT, OL_ADV, F_(tgtThIcon), 0, 0, C_TI_EN, C_TI_FR, 2, w_th, 0 },
    { "Speed / TH icon size", "Taille des ic\xC3\xB4nes vitesse / TH", OK_PCT, OL_ADV, F_(tgtDetailIconSz), 0.5f, 3.0f, nullptr, nullptr, 0, w_icons, 0 },
    { "Distance", "Distance", OK_TOGGLE, OL_ADV, F_(tgtRange), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Distance display", "Affichage de la distance", OK_SELECT, OL_ADV, F_(tgtRangeMin), 0, 0, C_GM_EN, C_GM_FR, 2, w_range, 0 },
    { "The spell it casts", "Le sort qu'elle lance", OK_TOGGLE, OL_ADV, F_(tgtCast), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Sample spell (to place it)", "Sort d'exemple (pour le placer)", OK_TOGGLE, OL_ADV, F_(tgtCastDemo), 0, 0, nullptr, nullptr, 0, w_cast, 0 },
};
static const OptDef TGT_DEBUFFS[] = {
    { "Debuffs", "Debuffs", OK_TOGGLE, OL_ADV, F_(tgtDebuffs), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Debuffs shown", "Debuffs affich\xC3\xA9s", OK_SELECT, OL_ADV, F_(dbShow), 0, 0, C_DB_EN, C_DB_FR, 2, w_debuffs, 0 },
    { "Max debuffs", "Debuffs max", OK_INT, OL_ADV, F_(tgtBuffMax), 1, 32, nullptr, nullptr, 0, w_inbox, 0 },
    { "Debuffs position", "Position des debuffs", OK_SELECT, OL_ADV, F_(tgtBuffPos), 0, 0, C_BP_EN, C_BP_FR, 5, w_inbox, 0 },
    { "Debuff icon size", "Taille des ic\xC3\xB4nes", OK_PCT, OL_ADV, F_(tgtIconSz), 0.5f, 2.0f, nullptr, nullptr, 0, w_inbox, 0 },
    { "Time left under each", "Temps restant sous chacun", OK_TOGGLE, OL_ADV, F_(tgtTimers), 0, 0, nullptr, nullptr, 0, w_inbox, 0 },
};
static const OptDef TGT_DBBOX[] = {   // the standalone debuff box : exists only in that mode (a structural section)
    { "Size", "Taille", OK_PCT, OL_ADV, F_(dbScale), 0.5f, 2.0f, nullptr, nullptr, 0, nullptr, 0 },
    { "Grows toward", "Grandit vers", OK_GROW, OL_ADV, nullptr, 0, 0, nullptr, nullptr, 0, nullptr, UiConfig::GB_DB },
    { "Max rows", "Lignes max", OK_INT, OL_ADV, F_(dbMax), 1, 32, nullptr, nullptr, 0, nullptr, 0 },
    { "Header (mob name)", "En-t\xC3\xAAte (nom du mob)", OK_TOGGLE, OL_ADV, F_(dbHeader), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Rows show", "Les lignes montrent", OK_SELECT, OL_ADV, F_(dbDisp), 0, 0, C_DD_EN, C_DD_FR, 3, nullptr, 0 },
    { "Icon size", "Taille des ic\xC3\xB4nes", OK_PCT, OL_ADV, F_(dbIconScale), 0.5f, 2.0f, nullptr, nullptr, 0, nullptr, 0 },
    { "Row spacing", "Espacement des lignes", OK_PCT, OL_ADV, F_(dbRowGap), 0.6f, 3.0f, nullptr, nullptr, 0, nullptr, 0 },
};
#undef F_
#define NOPT(a) ((int)(sizeof(a) / sizeof((a)[0])))

void ConfigPage::draw_target_v2(u32 dev, Font* fo, const MouseState* mo, bool click,
                                float& ry, int& ri, float e,
                                float bandX, float bandW, float coX, float ctrlW,
                                float hdrX, float hdrW) {
    Kit K;   // a COPY of the frame's kit (draw() built it) : no second font lookup ; its mouse is then gated to this viewport
    if (s_ck_page) K = *s_ck_page;
    else if (!frame_ || !kit_begin(K, *frame_, frame_->screenH, 1.0f)) {   // no kit this frame : the old page, never nothing
        draw_target_config(dev, fo, mo, click, ry, ri, e, bandX, bandW, coX, ctrlW, hdrX, hdrW); return; }
    K.mo = mo; K.click = click;                    // the page's gated mouse (the viewport) ; the kit's own would click through
    K.clipTop = viewTop_; K.clipBot = viewBot_;    // rows outside the viewport are not drawn
    g_fade = e;
    const bool adv = ui_config().cfgAdv != 0;
    // the column FILLS its band (left edge = the page title, right edge = the preview's gutter) : a narrower one floated
    // with a void beside it (2026-11-26). Long rows are tracked by the faint alternate banding (cfg_engine row_begin).
    const float x = coX, w = ctrlW;
    const int base = CTRL_ID;
    ry += K.px(6.0f);
    if (!adv) {
        bool on = true;
        cfg_section(K, ctrl_uid_i(base, 1), x, ry, w, tr("Display", "Affichage"), &on, cfg_count_modified(TGT_DISPLAY, NOPT(TGT_DISPLAY), false), false);
        cfg_rows(K, ctrl_uid_i(base, 2), TGT_DISPLAY, NOPT(TGT_DISPLAY), x, ry, w, false);
        ry += K.px(18.0f);
        cfg_section(K, ctrl_uid_i(base, 3), x, ry, w, tr("What it shows", "Ce qu'elle affiche"), &on, cfg_count_modified(TGT_SHOWS, NOPT(TGT_SHOWS), false), false);
        cfg_rows(K, ctrl_uid_i(base, 4), TGT_SHOWS, NOPT(TGT_SHOWS), x, ry, w, false);
        ry += K.px(14.0f);
        K.fr->begin(dev);
        K.fr->draw_lc(dev, x + K.px(18.0f), ry + K.px(14.0f), tr("Frame, text, fine sizes : Advanced (Interface page). Right-click a modified setting to reset it.",
                       "Cadre, texte, tailles fines : Avanc\xC3\xA9 (page Interface). Clic droit sur un r\xC3\xA9glage modifi\xC3\xA9 pour le r\xC3\xA9initialiser."),
                      K.px(13.5f), fa(C_DIM), 0, 0.0f);
        ry += K.px(40.0f);
        cfg_flush(mo);
        return;
    }
    struct Sec { int slot; const char* en; const char* fr; const OptDef* d; int n; };
    auto section = [&](int k, const Sec& s) {
        bool open = catOpen_[s.slot];
        if (cfg_section(K, ctrl_uid_i(base, 10 + k), x, ry, w, tr(s.en, s.fr), &open, cfg_count_modified(s.d, s.n, true), true)) {
            cfg_rows(K, ctrl_uid_i(base, 20 + k), s.d, s.n, x, ry, w, true); ry += K.px(12.0f); }
        catOpen_[s.slot] = open;
        ry += K.px(6.0f);
    };
    const Sec DISPLAY = { 6, "Display", "Affichage", TGT_DISPLAY, NOPT(TGT_DISPLAY) };
    section(0, DISPLAY);
    UiConfig& c = ui_config();
    {   // FRAME : the shared block, in the kit
        const FrameRefs fr = { &c.tgtBox, &c.tgtBorder, &c.tgtBoxAlpha, &c.tgtAlphaOwn, &c.tgtThemeCopy, &c.tgtTheme, &c.tgtHue, &c.tgtLum };
        bool open = catOpen_[20];
        if (cfg_section(K, ctrl_uid_i(base, 15), x, ry, w, tr("Frame", "Cadre"), &open, cfg_frame_modified(fr), true)) {
            cfg_frame_rows(K, ctrl_uid_i(base, 25), x, ry, w, fr); ry += K.px(12.0f); }
        catOpen_[20] = open; ry += K.px(6.0f);
    }
    const Sec BARS = { 7, "Bars", "Barres", TGT_BARS, NOPT(TGT_BARS) }, DETAIL = { 8, "Detail", "D\xC3\xA9tail", TGT_DETAIL, NOPT(TGT_DETAIL) },
              DEBUFFS = { 9, "Debuffs", "Debuffs", TGT_DEBUFFS, NOPT(TGT_DEBUFFS) };
    section(1, BARS); section(2, DETAIL); section(3, DEBUFFS);
    if (c.tgtDebuffs && c.dbShow) {   // the standalone box : its section exists only in that mode
        BoxStyle& bs = c.dbBox;
        const FrameRefs fr = { &bs.on, &bs.border, &bs.alpha, &bs.alphaOwn, &bs.themeCopy, &bs.theme, &bs.hue, &bs.lum };
        bool open = catOpen_[16];
        if (cfg_section(K, ctrl_uid_i(base, 14), x, ry, w, tr("Debuff box", "Bo\xC3\xAEte des debuffs"), &open,
                        cfg_count_modified(TGT_DBBOX, NOPT(TGT_DBBOX), true) + cfg_frame_modified(fr), true)) {
            cfg_rows(K, ctrl_uid_i(base, 24), TGT_DBBOX, NOPT(TGT_DBBOX), x, ry, w, true);
            cfg_frame_rows(K, ctrl_uid_i(base, 26), x, ry, w, fr);   // its frame : the shared block
            ry += K.px(12.0f);
        }
        catOpen_[16] = open; ry += K.px(6.0f);
    }
    {   // TEXT : the element list follows what is shown (no dead element) -- the same rule as the old page
        const char* lab[TGT_TE_COUNT + DB_TE_COUNT]; TextStyle* ts[TGT_TE_COUNT + DB_TE_COUNT]; int n = 0;
        auto add = [&](const char* en, const char* frl, TextStyle* t) { lab[n] = tr(en, frl); ts[n] = t; ++n; };
        add("Name", "Nom", &c.tgtText[TGT_NAME]);
        add("HP%", "PV%", &c.tgtText[TGT_HP]);
        if (c.tgtDebuffs && !c.dbShow && c.tgtTimers) add("Debuff timer", "Minuteur des debuffs", &c.tgtText[TGT_TIMER]);
        if (c.tgtSpeed) add("Speed", "Vitesse", &c.tgtText[TGT_SPD]);
        if (c.tgtTH)    add("Treasure Hunter", "Treasure Hunter", &c.tgtText[TGT_TH]);
        if (c.tgtRange) add("Distance", "Distance", &c.tgtText[TGT_RANGE]);
        if (c.tgtDebuffs && c.dbShow) {
            add("Debuff box header", "En-t\xC3\xAAte de la bo\xC3\xAEte", &c.dbText[DB_HEADER]);
            add("Debuff name", "Nom du debuff", &c.dbText[DB_NAME]);
            add("Debuff timer", "Minuteur du debuff", &c.dbText[DB_TIMER]);
        }
        bool open = catOpen_[5];
        if (cfg_section(K, ctrl_uid_i(base, 16), x, ry, w, tr("Text", "Texte"), &open, cfg_text_modified(ts, n), true)) {
            cfg_text_rows(K, ctrl_uid_i(base, 27), x, ry, w, lab, ts, n, &cfgTgtTextElem_); ry += K.px(12.0f); }
        catOpen_[5] = open; ry += K.px(6.0f);
    }
    cfg_flush(mo);
    g_fade = e;
}
#undef NOPT

} // namespace aio
