// player_config.cpp -- the Player Hub module's settings panel (//aio config -> "Player" / "Joueur").
//
// Split out like target_config.cpp : the Player module OWNS its config file. Each control is keyed by
// CTRL_ID (file:line hash, see config_controls.h) -- no hand-numbered uids. Defined as a ConfigPage
// method so it reaches the shared
// edit state (catOpen_[10] = the Player category ; sub-sections reuse catOpen_[6..9], harmless since
// only one module page is visible at a time). The page hands us its running layout cursor by reference.
#include "ui/config_page.h"
#include "ui/config_controls.h"   // shared toolkit : cat_header / row_slider / toggle_chip + palette + g_fade
#include "ui/config_rows.h"       // ROW_BAND / ROW_NEXT row-layout macros
#include "ui/cfg_engine.h"        // the redesign : OptDef tables + the kit (draw_player_v2)
#include "model/ui_config.h"      // ui_config(), save
#include "gfx/font.h"
#include "gfx/draw.h"
#include "gfx/window.h"           // box themes : window_theme_family/variant/index/name/is_proc, box_family_*, box_hue_*
#include <cstdio>
#include <cstring>

namespace aio {

// one On/Off toggle row (label left, chip right). Its own { } block (ROW_BAND declares ap/yo).
// Thin wrapper over the shared row_toggle (config_controls) -- kept as a macro so each call lands on its own __LINE__.

// draw the Player category (+ its Box / Content / Bars / Buffs sub-sections). Advances ry/ri.
void ConfigPage::draw_player_config(u32 dev, Font* fo, const MouseState* mo, bool click,
                                    float& ry, int& ri, float e,
                                    float bandX, float bandW, float coX, float ctrlW,
                                    float hdrX, float hdrW) {
        // ===== sub-section : DISPLAY (box chrome + size). No outer "Player" wrapper -- opens straight into its
        //       sub-sections like every other module (the sidebar already names the module). =====
        // The section FOLDS : cat_fold owns the eased progress, the clip and the cursor (config_controls.h).
        const float aF6_ = cat_fold(CTRL_ID, !ui_config().cfgAdv || (catOpen_[6]));
        cat_panel(dev, hdrX, ry, hdrW, cat_card_h(catH_[6], aF6_));   // the section IS a card, collapsed or not
        if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Display", "Affichage"), catOpen_[6], aF6_) && ui_config().cfgAdv) catOpen_[6] = !catOpen_[6];
        ROW_NEXT(42.0f)
        if (aF6_ > 0.0f) {
            const float top6_ = ry;
            cat_fold_clip(dev, hdrX, top6_, hdrW, catH_[6] * aF6_);
        // Show : master on/off for the WHOLE Player Hub (hidden everywhere when off ; other rows stay editable).
        ROW_TOGGLE_G(CTRL_ID, tr("Show", "Afficher"), ui_config().plrShow, 52.0f, 40.0f, 150.0f)
        // Size : scale the whole hub box (canonical : right after Show, before the Box chrome).
        { ROW_BAND(46.0f)
            const float lo = 0.50f, hi = 2.00f; char b[16]; sprintf(b, "%d%%", (int)(ui_config().plrScale * 100.0f + 0.5f));
            float v01 = (ui_config().plrScale - lo) / (hi - lo); v01 = clampf(v01, 0.0f, 1.0f);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Size", "Taille"), b, &v01)) {
                float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f; ui_config().plrScale = v < lo ? lo : (v > hi ? hi : v); }
        }
        ROW_NEXT(46.0f)
            cat_fold_end(dev, ry, top6_, catH_[6], aF6_);
        }   // end sub-section Box (catOpen_[6])
        ry += snap(16.0f);                                 // air between this section and the next title bar
    // ===== sub-section : FRAME (the config grammar : Display / Frame / Content / Behaviour / Text, docs/architecture/config-panels.md) =====
if (ui_config().cfgAdv) {   // Advanced only (the config's depth, 2026-11-26)
    const float aF20_ = cat_fold(CTRL_ID, catOpen_[20]);
    cat_panel(dev, hdrX, ry, hdrW, cat_card_h(catH_[20], aF20_));
    if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Frame", "Cadre"), catOpen_[20], aF20_)) catOpen_[20] = !catOpen_[20];
    ROW_NEXT(42.0f)
    if (aF20_ > 0.0f) {
        const float top20_ = ry;
        cat_fold_clip(dev, hdrX, top20_, hdrW, catH_[20] * aF20_);
        // Box : draw the frame chrome or not.
        ROW_CHOICE_G(CTRL_ID, tr("Box", "Cadre"), ui_config().plrBox, tr("On", "Oui"), tr("None", "Aucun"), 52.0f, 40.0f, 150.0f)
        if (ui_config().plrBox) {
        // Border : the frame's edges + corners (None = background only), like every other box
        ROW_CHOICE_G(CTRL_ID, tr("Border", "Bordure"), ui_config().plrBorder, tr("On", "Oui"), tr("None", "Aucune"), 52.0f, 40.0f, 150.0f)
        // Transparency : how see-through the box chrome is (content stays opaque). 0% = solid.
        // the Party box's (default) or this box's own ; the slider shows the EFFECTIVE value, moving it makes it own (2026-11-17)
        ROW_CHOICE_G(CTRL_ID, tr("Transparency", "Transparence"), ui_config().plrAlphaOwn, tr("Custom", "Perso"), tr("Same as Party", "Comme Party"), 52.0f, 40.0f, 150.0f)
        { ROW_BAND(46.0f)
            UiConfig& uc = ui_config();
            const float transp = 1.0f - (uc.plrAlphaOwn ? uc.plrBoxAlpha : uc.skinBoxAlpha); char b[16]; sprintf(b, "%d%%", (int)(transp * 100.0f + 0.5f));
            float v01 = clampf(transp, 0.0f, 1.0f);   // full 0..100% range
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Box transparency", "Transparence du cadre"), b, &v01)) {
                uc.plrBoxAlpha = 1.0f - v01; uc.plrAlphaOwn = 1; }
        }
        ROW_NEXT(46.0f)
        // Copy Party : follow the party box theme (hides the own-theme rows below).
        ROW_CHOICE_G(CTRL_ID, tr("Theme", "Thème"), ui_config().plrThemeCopy, tr("Same as Party", "Comme Party"), tr("Custom", "Perso"), 52.0f, 40.0f, 150.0f)
        if (!ui_config().plrThemeCopy) {
        // Box Theme : a FAMILY selector + a variant grid, INDEPENDENT of the party theme (plrTheme).
        { ROW_BAND(52.0f)
          const int fam = window_theme_family(ui_config().plrTheme), var = window_theme_variant(ui_config().plrTheme);
          if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Box Theme", "Thème de cadre"), box_family_name(fam))) {   // keyed by CTRL_ID
              ui_config().plrTheme = window_theme_index(wrap(fam + d, box_family_count()), var); save_ui_config(); } }
        ROW_NEXT(52.0f)
        // procedural family : a free CUSTOM hue -- a toggle that swaps the preset swatches for the HSV picker.
        if (window_theme_family(ui_config().plrTheme) != 0) { ROW_BAND(48.0f)
            const float rowH = snap(38.0f), ty = ry + yo; fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, tr("Custom colour", "Couleur perso"), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
            const float bbw = snap(112.0f), bbh = snap(34.0f), bx2 = coX + ctrlW - bbw, bty = ty + (rowH - bbh) * 0.5f;
            const bool on = ui_config().plrHue != 0;
            if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx2, bty, bbw, bbh, on ? tr("On", "Oui") : tr("Off", "Non"), on)) {
                ui_config().plrHue = on ? 0u : (box_hue_color(window_theme_variant(ui_config().plrTheme)) | 0xFF000000u); save_ui_config(); }
            ROW_NEXT(48.0f)
        }
        if (window_theme_family(ui_config().plrTheme) != 0 && ui_config().plrHue != 0) {
            CFG_COLOR_PICKER(&ui_config().plrHue)
        } else
        {   // variant grid : FFXI -> theme-number chips ; procedural family -> hue swatches (click to pick)
          const int fam = window_theme_family(ui_config().plrTheme), var = window_theme_variant(ui_config().plrTheme);
          float slotH = 0.0f;
          {   // the height has to be known BEFORE ROW_BAND, so it is computed from the same rule the grid uses
              const int nr = (box_hue_count() + 14) / 15;
              slotH = (fam == 0) ? snap(52.0f)   // FFXI : a selector row, like every other setting
                                : (float)nr * snap(22.0f) + (float)(nr - 1) * snap(7.0f) + snap(20.0f); }
          ROW_BAND(slotH) (void)yo;
          {   float sh_ = slotH;
              const int pick = theme_grid(dev, fo, mo, click, CTRL_ID, coX, ry, ctrlW, fam, var, sh_);
              if (pick >= 0) { ui_config().plrTheme = window_theme_index(fam, pick); save_ui_config(); } }
          ROW_NEXT(slotH)
        }
        // Luminosity : darken / lighten the chosen box-theme colour (procedural families only).
        if (window_theme_family(ui_config().plrTheme) != 0)
        { ROW_BAND(46.0f)
            float v01 = (ui_config().plrLum + 1.0f) * 0.5f; v01 = clampf(v01, 0.0f, 1.0f);
            const int pct = (int)(ui_config().plrLum * 100.0f + (ui_config().plrLum >= 0.0f ? 0.5f : -0.5f));
            char b[16]; sprintf(b, "%+d%%", pct);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Luminosity", "Luminosité"), b, &v01)) {
                ui_config().plrLum = v01 * 2.0f - 1.0f; }
          ROW_NEXT(46.0f)
        }
        }   // end own-theme rows (!plrThemeCopy)
        }
        cat_fold_end(dev, ry, top20_, catH_[20], aF20_);
    }   // end Frame
    ry += snap(16.0f);
}   // end Advanced only
        // ---- sub-section : Content (what the hub shows) ----
        // The section FOLDS : cat_fold owns the eased progress, the clip and the cursor (config_controls.h).
        const float aF7_ = cat_fold(CTRL_ID, !ui_config().cfgAdv || (catOpen_[7]));
        cat_panel(dev, hdrX, ry, hdrW, cat_card_h(catH_[7], aF7_));   // the section IS a card, collapsed or not
        if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Content", "Contenu"), catOpen_[7], aF7_) && ui_config().cfgAdv) catOpen_[7] = !catOpen_[7];
        ROW_NEXT(42.0f)
        if (aF7_ > 0.0f) {
            const float top7_ = ry;
            cat_fold_clip(dev, hdrX, top7_, hdrW, catH_[7] * aF7_);
        ROW_TOGGLE(CTRL_ID, tr("Emblem", "Emblème"), ui_config().plrEmblem)
        ROW_TOGGLE(CTRL_ID, tr("Name", "Nom"),       ui_config().plrName)
        ROW_TOGGLE(CTRL_ID, tr("Level", "Niveau"),   ui_config().plrLvl)
        ROW_TOGGLE(CTRL_ID, "HP",               ui_config().plrHp)
        ROW_TOGGLE(CTRL_ID, "MP",               ui_config().plrMp)
        ROW_TOGGLE(CTRL_ID, "TP",               ui_config().plrTp)
        ROW_TOGGLE(CTRL_ID, tr("Speed", "Vitesse"), ui_config().plrSpeed)
        ROW_TOGGLE(CTRL_ID, tr("The spell you cast", "Le sort que tu lances"), ui_config().plrCast)
        if (ui_config().cfgAdv) { ROW_TOGGLE(CTRL_ID, tr("Sample spell (to place it)", "Sort d'exemple (pour le placer)"), ui_config().plrCastDemo) }   // a placement aid : Advanced
        ROW_TOGGLE(CTRL_ID, tr("Buffs", "Buffs"), ui_config().plrBuffs)
        ROW_TOGGLE(CTRL_ID, tr("Equipment", "Équipement"), ui_config().plrEquip)
        // Gil : lives in Content while the equipment is docked or off (gil then rides the header / hub) ; when the
        // equipment is DETACHED the gil goes with that box, so its toggle moves to the Equipment sub-section.
        if (!(ui_config().plrEquip && ui_config().plrEquipDetach)) { ROW_TOGGLE(CTRL_ID, tr("Gil", "Gil"), ui_config().plrGil) }
            cat_fold_end(dev, ry, top7_, catH_[7], aF7_);
        }   // end sub-section Content (catOpen_[7])
        ry += snap(16.0f);                                 // air between this section and the next title bar
        // ---- sub-section : Identity (the job emblem is an icon -> size only) ----
        // The section FOLDS : cat_fold owns the eased progress, the clip and the cursor (config_controls.h).
if (ui_config().cfgAdv && ui_config().plrEmblem) {   // Advanced only ; and only with the emblem (its one setting is its size)
        const float aF11_ = cat_fold(CTRL_ID, catOpen_[11]);
        cat_panel(dev, hdrX, ry, hdrW, cat_card_h(catH_[11], aF11_));   // the section IS a card, collapsed or not
        if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Identity", "Identité"), catOpen_[11], aF11_)) catOpen_[11] = !catOpen_[11];
        ROW_NEXT(42.0f)
        if (aF11_ > 0.0f) {
            const float top11_ = ry;
            cat_fold_clip(dev, hdrX, top11_, hdrW, catH_[11] * aF11_);
        // Emblem Size
        { ROW_BAND(46.0f)
            const float lo = 0.50f, hi = 2.00f; char b[16]; sprintf(b, "%d%%", (int)(ui_config().plrEmblemSz * 100.0f + 0.5f));
            float v01 = (ui_config().plrEmblemSz - lo) / (hi - lo); v01 = clampf(v01, 0.0f, 1.0f);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Emblem Size", "Taille emblème"), b, &v01)) {
                float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f; ui_config().plrEmblemSz = v < lo ? lo : (v > hi ? hi : v); }
        }
        ROW_NEXT(46.0f)
            cat_fold_end(dev, ry, top11_, catH_[11], aF11_);
        }   // end sub-section Identity (catOpen_[11])
        ry += snap(16.0f);                                 // air between this section and the next title bar
}   // end Advanced only
        // ---- sub-section : Bars (fiole size) ----
        // The section FOLDS : cat_fold owns the eased progress, the clip and the cursor (config_controls.h).
if (ui_config().cfgAdv) {   // Advanced only (the config's depth, 2026-11-26)
        const float aF8_ = cat_fold(CTRL_ID, catOpen_[8]);
        cat_panel(dev, hdrX, ry, hdrW, cat_card_h(catH_[8], aF8_));   // the section IS a card, collapsed or not
        if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Bars", "Barres"), catOpen_[8], aF8_)) catOpen_[8] = !catOpen_[8];
        ROW_NEXT(42.0f)
        if (aF8_ > 0.0f) {
            const float top8_ = ry;
            cat_fold_clip(dev, hdrX, top8_, hdrW, catH_[8] * aF8_);
        // Bar Height : the fiole height.
        { ROW_BAND(46.0f)
            const float lo = 0.50f, hi = 2.00f; char b[16]; sprintf(b, "%d%%", (int)(ui_config().plrBarH * 100.0f + 0.5f));
            float v01 = (ui_config().plrBarH - lo) / (hi - lo); v01 = clampf(v01, 0.0f, 1.0f);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Bar Height", "Hauteur barre"), b, &v01)) {
                float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f; ui_config().plrBarH = v < lo ? lo : (v > hi ? hi : v); }
        }
        ROW_NEXT(46.0f)
        // Bar Width : the fiole width as a fraction of the box (centred).
        { ROW_BAND(46.0f)
            const float lo = 0.40f, hi = 1.00f; char b[16]; sprintf(b, "%d%%", (int)(ui_config().plrBarW * 100.0f + 0.5f));
            float v01 = (ui_config().plrBarW - lo) / (hi - lo); v01 = clampf(v01, 0.0f, 1.0f);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Bar Width", "Largeur barre"), b, &v01)) {
                float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f; ui_config().plrBarW = v < lo ? lo : (v > hi ? hi : v); }
        }
        ROW_NEXT(46.0f)
        // Bar Spacing : the gap between the HP / MP / TP fioles (0% = touching).
        { ROW_BAND(46.0f)
            const float lo = 0.00f, hi = 3.00f; char b[16]; sprintf(b, "%d%%", (int)(ui_config().plrBarGap * 100.0f + 0.5f));
            float v01 = (ui_config().plrBarGap - lo) / (hi - lo); v01 = clampf(v01, 0.0f, 1.0f);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Bar Spacing", "Espacement barres"), b, &v01)) {
                float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f; ui_config().plrBarGap = v < lo ? lo : (v > hi ? hi : v); }
        }
        ROW_NEXT(46.0f)
            cat_fold_end(dev, ry, top8_, catH_[8], aF8_);
        }   // end sub-section Bars (catOpen_[8])
        ry += snap(16.0f);                                 // air between this section and the next title bar
}   // end Advanced only
        // ---- sub-section : Buffs (only meaningful when Buffs are shown) ----
if (ui_config().cfgAdv) {   // Advanced only (the config's depth, 2026-11-26)
        const float aF9b_ = cat_fold(CTRL_ID, catOpen_[9] && ui_config().plrBuffs);   // the section FOLDS -- and with the feature off there is nothing to reveal
        cat_panel(dev, hdrX, ry, hdrW, cat_card_h(catH_[9], aF9b_));
        if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Buffs", "Buffs"), catOpen_[9], aF9b_)) catOpen_[9] = !catOpen_[9];
        ROW_NEXT(42.0f)
        if (aF9b_ > 0.0f) {
            const float top9b_ = ry;
            cat_fold_clip(dev, hdrX, top9b_, hdrW, catH_[9] * aF9b_);
        // Max Buffs : how many status icons to show at most.
        { ROW_BAND(46.0f)
            const float lo = 1.0f, hi = 32.0f; int cur = ui_config().plrBuffMax; if (cur < 1) cur = 1; if (cur > 32) cur = 32;
            char b[16]; sprintf(b, "%d", cur);
            float v01 = ((float)cur - lo) / (hi - lo); v01 = clampf(v01, 0.0f, 1.0f);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Max Buffs", "Max buffs"), b, &v01)) {
                int v = (int)(lo + v01 * (hi - lo) + 0.5f); ui_config().plrBuffMax = v < 1 ? 1 : (v > 32 ? 32 : v); }
        }
        ROW_NEXT(46.0f)
        // Buff Size : the status-icon size.
        { ROW_BAND(46.0f)
            const float lo = 0.50f, hi = 2.00f; char b[16]; sprintf(b, "%d%%", (int)(ui_config().plrIconSz * 100.0f + 0.5f));
            float v01 = (ui_config().plrIconSz - lo) / (hi - lo); v01 = clampf(v01, 0.0f, 1.0f);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Buff Size", "Taille buffs"), b, &v01)) {
                float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f; ui_config().plrIconSz = v < lo ? lo : (v > hi ? hi : v); }
        }
        ROW_NEXT(46.0f)
            cat_fold_end(dev, ry, top9b_, catH_[9], aF9b_);
        }   // end sub-section Buffs (catOpen_[9])
        ry += snap(16.0f);                                 // air between this section and the next title bar
}   // end Advanced only
        // ---- sub-section : Equipment (the gear grid) ----
if (ui_config().cfgAdv) {   // Advanced only (the config's depth, 2026-11-26)
        const float aF12b_ = cat_fold(CTRL_ID, catOpen_[12] && ui_config().plrEquip);   // the section FOLDS -- and with the feature off there is nothing to reveal
        cat_panel(dev, hdrX, ry, hdrW, cat_card_h(catH_[12], aF12b_));
        if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Equipment", "Équipement"), catOpen_[12], aF12b_)) catOpen_[12] = !catOpen_[12];
        ROW_NEXT(42.0f)
        if (aF12b_ > 0.0f) {
            const float top12b_ = ry;
            cat_fold_clip(dev, hdrX, top12b_, hdrW, catH_[12] * aF12b_);
        // Mode : the equipment lives INSIDE the Player Hub (docked, placement below) or as its OWN standalone
        // box with its own size, dragged in //aio edit.
        ROW_CHOICE_G(CTRL_ID, tr("Mode", "Mode"), ui_config().plrEquipDetach, tr("Standalone", "Autonome"), tr("In Player", "Dans Player"), 52.0f, 40.0f, 150.0f)
        if (!ui_config().plrEquipDetach) {
        // Placement : inside the box (left/centre/right) or docked outside (left/right).
        { ROW_BAND(52.0f)
            static const char* PL_EN[9] = { "Box \xC2\xB7 Center", "Box \xC2\xB7 Left", "Box \xC2\xB7 Right", "Dock Left", "Dock Right", "Dock Top", "Dock Bottom", "Box \xC2\xB7 Side Left", "Box \xC2\xB7 Side Right" };
            static const char* PL_FR[9] = { "Box \xC2\xB7 Centre", "Box \xC2\xB7 Gauche", "Box \xC2\xB7 Droite", "Dock Gauche", "Dock Droite", "Dock Haut", "Dock Bas", "Box \xC2\xB7 C\xC3\xB4t\xC3\xA9 Gauche", "Box \xC2\xB7 C\xC3\xB4t\xC3\xA9 Droite" };
            int pl = ui_config().plrEqPlace; if (pl < 0 || pl > 8) pl = 0;
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Placement", "Placement"), tr(PL_EN[pl], PL_FR[pl]))) {
                ui_config().plrEqPlace = wrap(pl + d, 9); save_ui_config(); }
        }
        ROW_NEXT(52.0f)
        } else {
        // Standalone : its own size ; position is dragged in //aio edit.
        { ROW_BAND(46.0f)
            const float lo = 0.50f, hi = 2.00f; char b[16]; sprintf(b, "%d%%", (int)(ui_config().plrEquipScale * 100.0f + 0.5f));
            float v01 = (ui_config().plrEquipScale - lo) / (hi - lo); v01 = clampf(v01, 0.0f, 1.0f);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Size", "Taille"), b, &v01)) {
                float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f; ui_config().plrEquipScale = v < lo ? lo : (v > hi ? hi : v); }
        }
        ROW_NEXT(46.0f)
        // Standalone box chrome (frame / border / transparency / theme) : DETACHED only ; docked shares the Hub box.
        draw_box_appearance(dev, fo, mo, click, ry, ri, e, bandX, bandW, coX, ctrlW, ui_config().plrEqBox);
        }
        // Gil ON/OFF lives HERE only when the equipment is DETACHED (the gil rides that box) ; docked/off -> Content.
        if (ui_config().plrEquipDetach) { ROW_TOGGLE(CTRL_ID, tr("Gil", "Gil"), ui_config().plrGil) }
        // Gil position (standalone only) : which side of the grid the gil row sits on.
        if (ui_config().plrEquipDetach && ui_config().plrGil) {
        { ROW_BAND(52.0f)
            static const char* GP_EN[4] = { "Below", "Above", "Left", "Right" };
            static const char* GP_FR[4] = { "Dessous", "Dessus", "Gauche", "Droite" };
            int gp = ui_config().plrEqGilPlace; if (gp < 0 || gp > 3) gp = 0;
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Gil position", "Position Gil"), tr(GP_EN[gp], GP_FR[gp]))) {
                ui_config().plrEqGilPlace = wrap(gp + d, 4); save_ui_config(); }
        }
        ROW_NEXT(52.0f)
        }
        // Cell Size : the gear-cell dimension.
        { ROW_BAND(46.0f)
            const float lo = 0.50f, hi = 2.00f; char b[16]; sprintf(b, "%d%%", (int)(ui_config().plrEqCell * 100.0f + 0.5f));
            float v01 = (ui_config().plrEqCell - lo) / (hi - lo); v01 = clampf(v01, 0.0f, 1.0f);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Cell Size", "Taille cellule"), b, &v01)) {
                float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f; ui_config().plrEqCell = v < lo ? lo : (v > hi ? hi : v); }
        }
        ROW_NEXT(46.0f)
        // Border : follow the box theme, or a custom colour.
        ROW_CHOICE_G(CTRL_ID, tr("Cell border", "Bordure des cases"), ui_config().plrEqThemeBorder, tr("Box Theme", "Thème box"), tr("Custom", "Perso"), 52.0f, 40.0f, 150.0f)
        if (!ui_config().plrEqThemeBorder) {
        CFG_COLOR_PICKER(&ui_config().plrEqColor)
        }   // end custom colour (!plrEqThemeBorder)
        // Cell background : the cell FILL colour -- Default dark, or a custom colour (filled slots stay a touch brighter).
        ROW_CHOICE_G(CTRL_ID, tr("Cell background", "Fond des cases"), ui_config().plrEqCellBgCustom, tr("Custom", "Perso"), tr("Default", "Défaut"), 52.0f, 40.0f, 150.0f)
        if (ui_config().plrEqCellBgCustom) {
        CFG_COLOR_PICKER(&ui_config().plrEqCellBg)
        }   // end custom cell background
            cat_fold_end(dev, ry, top12b_, catH_[12], aF12b_);
        }   // end sub-section Equipment (catOpen_[12])
        ry += snap(16.0f);
}   // end Advanced only

        // ---- Typography sub-section : per-element Font / Size / Outline / Style / Colour (Name / Level) ----
        // The section FOLDS : cat_fold owns the eased progress, the clip and the cursor (config_controls.h).
if (ui_config().cfgAdv) {   // Advanced only (the config's depth, 2026-11-26)
        const float aF5_ = cat_fold(CTRL_ID, catOpen_[5]);
        cat_panel(dev, hdrX, ry, hdrW, cat_card_h(catH_[5], aF5_));   // the section IS a card, collapsed or not
        if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Text", "Texte"), catOpen_[5], aF5_)) catOpen_[5] = !catOpen_[5];
        ROW_NEXT(42.0f)
        if (aF5_ > 0.0f) {
            const float top5_ = ry;
            cat_fold_clip(dev, hdrX, top5_, hdrW, catH_[5] * aF5_);
        { ROW_BAND(52.0f)   // element selector
            const char* TLBL[PLR_TE_COUNT] = { tr("Name", "Nom"), tr("Level", "Niveau"), tr("Gil", "Gil"), tr("Speed", "Vitesse"), "HP", "MP", "TP", tr("Cast", "Sort") };
            int te = (cfgPlrTextElem_ < 0 || cfgPlrTextElem_ >= PLR_TE_COUNT) ? 0 : cfgPlrTextElem_;
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Element", "Élément"), TLBL[te])) {
                cfgPlrTextElem_ = wrap(te + d, PLR_TE_COUNT); }
        }
        ROW_NEXT(52.0f)
        draw_text_style(dev, fo, mo, click, ry, ri, e, bandX, bandW, coX, ctrlW,
                        ui_config().plrText[(cfgPlrTextElem_ < 0 || cfgPlrTextElem_ >= PLR_TE_COUNT) ? 0 : cfgPlrTextElem_], true);
            cat_fold_end(dev, ry, top5_, catH_[5], aF5_);
        }   // end Text sub-section (catOpen_[5])
        ry += snap(16.0f);                                 // air between this section and the next title bar
}   // end Advanced only
    if (!ui_config().cfgAdv) SIMPLE_MORE   // Simple : where the rest is

    #undef ROW_BAND
    #undef ROW_NEXT
}


// ======== THE NEW PLAYER PAGE (the config redesign, milestone 3, 2026-11-26) ========
// Every option described once (OptDef, cfg_engine.h). Simple : Display + Content (the on/off list). Advanced
// (2026-11-27 restructure, the Target pattern) : Display / Frame / one section per ELEMENT the player looks for
// (Identity, Bars, Spell, Buffs, Equipment and gil) holding that element's on/off AND its settings / Text. A simple
// dependency GREYS its rows with the reason ; rows that belong to one MODE (docked / standalone equipment) exist only
// in that mode, and the "shown + how" pairs are ONE choice (Equipment : Off / In the box / Standalone).
#define F_(name) [](UiConfig& c) -> void* { return &c.name; }
static const char* const P_EQM_EN[2] = { "In the Player box", "Standalone" },   * const P_EQM_FR[2] = { "Dans la box Joueur", "Autonome" };
static const char* const P_PL_EN[9] = { "Box \xC2\xB7 Center", "Box \xC2\xB7 Left", "Box \xC2\xB7 Right", "Dock Left", "Dock Right", "Dock Top", "Dock Bottom", "Box \xC2\xB7 Side Left", "Box \xC2\xB7 Side Right" };
static const char* const P_PL_FR[9] = { "Box \xC2\xB7 Centre", "Box \xC2\xB7 Gauche", "Box \xC2\xB7 Droite", "Dock Gauche", "Dock Droite", "Dock Haut", "Dock Bas", "Box \xC2\xB7 C\xC3\xB4t\xC3\xA9 Gauche", "Box \xC2\xB7 C\xC3\xB4t\xC3\xA9 Droite" };
static const char* const P_GP_EN[4] = { "Below", "Above", "Left", "Right" },    * const P_GP_FR[4] = { "Dessous", "Dessus", "Gauche", "Droite" };
static const char* const P_CB_EN[2] = { "Custom", "The box theme" },             * const P_CB_FR[2] = { "Perso", "Le th\xC3\xA8me de la box" };
static const char* const P_BG_EN[2] = { "Default", "Custom" },                   * const P_BG_FR[2] = { "D\xC3\xA9""faut", "Perso" };
static const char* pw_cast(const UiConfig& c)   { return c.plrCast ? nullptr : tr("inactive : spell off", "inactif : sort coup\xC3\xA9"); }
static const char* pw_emblem(const UiConfig& c) { return c.plrEmblem ? nullptr : tr("inactive : emblem off", "inactif : embl\xC3\xA8me coup\xC3\xA9"); }
static const char* pw_bars(const UiConfig& c)   { return (c.plrHp || c.plrMp || c.plrTp) ? nullptr : tr("inactive : HP, MP and TP off", "inactif : PV, PM et TP coup\xC3\xA9s"); }
static const char* pw_buffs(const UiConfig& c)  { return c.plrBuffs ? nullptr : tr("inactive : buffs off", "inactif : buffs coup\xC3\xA9s"); }

static const OptDef PLR_DISPLAY[] = {
    { "Show", "Afficher", OK_TOGGLE, OL_SIMPLE, F_(plrShow), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Size", "Taille", OK_PCT, OL_SIMPLE, F_(plrScale), 0.5f, 2.0f, nullptr, nullptr, 0, nullptr, 0 },
    { "To move it : Edit Layout (top tab).", "Pour la d\xC3\xA9placer : onglet \xC3\x89""diter dispo.", OK_NOTE, OL_SIMPLE, nullptr, 0, 0, nullptr, nullptr, 0, nullptr, 0 },
};
static const OptDef PLR_CONTENT[] = {   // SIMPLE only : the on/off list (Advanced puts each one in its element's section)
    { "Emblem", "Embl\xC3\xA8me", OK_TOGGLE, OL_SIMPLE, F_(plrEmblem), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Name", "Nom", OK_TOGGLE, OL_SIMPLE, F_(plrName), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Level", "Niveau", OK_TOGGLE, OL_SIMPLE, F_(plrLvl), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "HP", "PV", OK_TOGGLE, OL_SIMPLE, F_(plrHp), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "MP", "PM", OK_TOGGLE, OL_SIMPLE, F_(plrMp), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "TP", "TP", OK_TOGGLE, OL_SIMPLE, F_(plrTp), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Speed", "Vitesse", OK_TOGGLE, OL_SIMPLE, F_(plrSpeed), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "The spell you cast", "Le sort que tu lances", OK_TOGGLE, OL_SIMPLE, F_(plrCast), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Buffs", "Buffs", OK_TOGGLE, OL_SIMPLE, F_(plrBuffs), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Equipment", "\xC3\x89quipement", OK_TOGGLE, OL_SIMPLE, F_(plrEquip), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Gil", "Gil", OK_TOGGLE, OL_SIMPLE, F_(plrGil), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
};
// ---- ADVANCED : one table per element section ----
static const OptDef PLR_IDENTITY[] = {   // the identity row : emblem / name / level, and the speed it hosts (left)
    { "Emblem", "Embl\xC3\xA8me", OK_TOGGLE, OL_ADV, F_(plrEmblem), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Emblem size", "Taille de l'embl\xC3\xA8me", OK_PCT, OL_ADV, F_(plrEmblemSz), 0.5f, 2.0f, nullptr, nullptr, 0, pw_emblem, 0 },
    { "Name", "Nom", OK_TOGGLE, OL_ADV, F_(plrName), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Level", "Niveau", OK_TOGGLE, OL_ADV, F_(plrLvl), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Movement speed", "Vitesse de d\xC3\xA9placement", OK_TOGGLE, OL_ADV, F_(plrSpeed), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
};
static const OptDef PLR_BARS[] = {
    { "HP", "PV", OK_TOGGLE, OL_ADV, F_(plrHp), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "MP", "PM", OK_TOGGLE, OL_ADV, F_(plrMp), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "TP", "TP", OK_TOGGLE, OL_ADV, F_(plrTp), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Bar height", "Hauteur des barres", OK_PCT, OL_ADV, F_(plrBarH), 0.5f, 2.0f, nullptr, nullptr, 0, pw_bars, 0 },
    { "Bar width", "Largeur des barres", OK_PCT, OL_ADV, F_(plrBarW), 0.4f, 1.0f, nullptr, nullptr, 0, pw_bars, 0 },
    { "Bar spacing", "Espacement des barres", OK_PCT, OL_ADV, F_(plrBarGap), 0.0f, 3.0f, nullptr, nullptr, 0, pw_bars, 0 },
};
static const OptDef PLR_SPELL[] = {   // the action bar (own spell / ability / weaponskill)
    { "The spell you cast", "Le sort que tu lances", OK_TOGGLE, OL_ADV, F_(plrCast), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Sample spell (to place it)", "Sort d'exemple (pour le placer)", OK_TOGGLE, OL_ADV, F_(plrCastDemo), 0, 0, nullptr, nullptr, 0, pw_cast, 0 },
};
static const OptDef PLR_BUFFS[] = {
    { "Buffs", "Buffs", OK_TOGGLE, OL_ADV, F_(plrBuffs), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Max buffs", "Buffs max", OK_INT, OL_ADV, F_(plrBuffMax), 1, 32, nullptr, nullptr, 0, pw_buffs, 0 },
    { "Buff size", "Taille des buffs", OK_PCT, OL_ADV, F_(plrIconSz), 0.5f, 2.0f, nullptr, nullptr, 0, pw_buffs, 0 },
};
static const OptDef PLR_EQ_DOCK[] = {    // equipment IN the Player box only
    { "Placement", "Placement", OK_SELECT, OL_ADV, F_(plrEqPlace), 0, 0, P_PL_EN, P_PL_FR, 9, nullptr, 0 },
};
static const OptDef PLR_EQ_ALONE[] = {   // standalone equipment only (its position : Edit Layout)
    { "Size", "Taille", OK_PCT, OL_ADV, F_(plrEquipScale), 0.5f, 2.0f, nullptr, nullptr, 0, nullptr, 0 },
};
static const OptDef PLR_EQ_CELLS[] = {   // either mode
    { "Cell size", "Taille des cases", OK_PCT, OL_ADV, F_(plrEqCell), 0.5f, 2.0f, nullptr, nullptr, 0, nullptr, 0 },
    { "Cell border", "Bordure des cases", OK_SELECT, OL_ADV, F_(plrEqThemeBorder), 0, 0, P_CB_EN, P_CB_FR, 2, nullptr, 0 },
};
static const OptDef PLR_EQ_BG[] = {
    { "Cell background", "Fond des cases", OK_SELECT, OL_ADV, F_(plrEqCellBgCustom), 0, 0, P_BG_EN, P_BG_FR, 2, nullptr, 0 },
};
#undef F_
#define NOPT(a) ((int)(sizeof(a) / sizeof((a)[0])))

void ConfigPage::draw_player_v2(u32 dev, Font* fo, const MouseState* mo, bool click,
                                float& ry, int& ri, float e,
                                float bandX, float bandW, float coX, float ctrlW,
                                float hdrX, float hdrW) {
    Kit K;
    if (!v2_kit(K, mo, click)) { draw_player_config(dev, fo, mo, click, ry, ri, e, bandX, bandW, coX, ctrlW, hdrX, hdrW); return; }
    g_fade = e;
    UiConfig& c = ui_config();
    const bool adv = c.cfgAdv != 0;
    const float x = coX, w = ctrlW;
    const int base = CTRL_ID;
    ry += K.px(6.0f);
    if (!adv) {   // SIMPLE : Display + the on/off list, then where the rest is
        if (v2_section(K, ctrl_uid_i(base, 1), 6, "Display", "Affichage", x, ry, w, cfg_count_modified(PLR_DISPLAY, NOPT(PLR_DISPLAY), false)))
            v2_rows(K, ctrl_uid_i(base, 2), PLR_DISPLAY, NOPT(PLR_DISPLAY), x, ry, w);
        if (v2_section(K, ctrl_uid_i(base, 5), 7, "Content", "Contenu", x, ry, w, cfg_count_modified(PLR_CONTENT, NOPT(PLR_CONTENT), false)))
            v2_rows(K, ctrl_uid_i(base, 6), PLR_CONTENT, NOPT(PLR_CONTENT), x, ry, w);
        v2_simple_note(K, x, ry);
        g_fade = e;
        return;
    }
    // ADVANCED (2026-11-27 restructure, the Target pattern) : Display / Frame / a section per ELEMENT (its on/off and its
    // settings together) / Text. No catch-all "Content" list : every toggle lives with the rows it governs.
    static const UiConfig D{};
    // ONE choice for "shown or not, and how" : 0 = Off, k = mode k-1 (the same helper as the Target page)
    auto choice = [&](int uid, const char* en, const char* frl, int& on, int& mode, const char* const* men, const char* const* mfr, int n, const char* why, int dOn) {
        const char* lab[8]; lab[0] = tr("Off", "D\xC3\xA9sactiv\xC3\xA9"); for (int k = 0; k < n && k < 7; ++k) lab[k + 1] = tr(men[k], mfr[k]);
        int v = on ? 1 + (mode < 0 || mode >= n ? 0 : mode) : 0;
        void* mark = (on != dOn) ? (void*)&on : (void*)&mode;   // the marker / right-click reset : the field that differs
        const int prevOn = on;
        if (int d = cfg_select_row(K, uid, x, ry, w, tr(en, frl), lab[v], why, mark, 4)) {
            v = wrap(v + d, n + 1); on = v > 0 ? 1 : 0; if (v > 0) mode = v - 1; save_ui_config(); }
        else if (on != prevOn) {   // a right-click reset (it restored "on") : the mode back to its factory value too
            static const UiConfig DEFc{};
            mode = *(const int*)((const char*)&DEFc + ((const char*)&mode - (const char*)&ui_config())); save_ui_config(); }
    };
    auto diff2 = [&](int on, int mode, int dOn, int dMode) { return (on != dOn) || (on && mode != dMode) ? 1 : 0; };
    if (v2_section(K, ctrl_uid_i(base, 30), 6, "Display", "Affichage", x, ry, w, cfg_count_modified(PLR_DISPLAY, NOPT(PLR_DISPLAY), true)))
        v2_rows(K, ctrl_uid_i(base, 31), PLR_DISPLAY, NOPT(PLR_DISPLAY), x, ry, w);
    {   // FRAME : the shared block
        const FrameRefs fr = { &c.plrBox, &c.plrBorder, &c.plrBoxAlpha, &c.plrAlphaOwn, &c.plrThemeCopy, &c.plrTheme, &c.plrHue, &c.plrLum };
        if (v2_section(K, ctrl_uid_i(base, 32), 20, "Frame", "Cadre", x, ry, w, cfg_frame_modified(fr))) { cfg_frame_rows(K, ctrl_uid_i(base, 33), x, ry, w, fr); v2_end(K, ry); }
    }
    if (v2_section(K, ctrl_uid_i(base, 34), 11, "Identity", "Identit\xC3\xA9", x, ry, w, cfg_count_modified(PLR_IDENTITY, NOPT(PLR_IDENTITY), true)))
        v2_rows(K, ctrl_uid_i(base, 35), PLR_IDENTITY, NOPT(PLR_IDENTITY), x, ry, w);
    if (v2_section(K, ctrl_uid_i(base, 36), 8, "Bars", "Barres", x, ry, w, cfg_count_modified(PLR_BARS, NOPT(PLR_BARS), true)))
        v2_rows(K, ctrl_uid_i(base, 37), PLR_BARS, NOPT(PLR_BARS), x, ry, w);
    if (v2_section(K, ctrl_uid_i(base, 38), 10, "Spell", "Sort", x, ry, w, cfg_count_modified(PLR_SPELL, NOPT(PLR_SPELL), true)))
        v2_rows(K, ctrl_uid_i(base, 39), PLR_SPELL, NOPT(PLR_SPELL), x, ry, w);
    if (v2_section(K, ctrl_uid_i(base, 40), 9, "Buffs", "Buffs", x, ry, w, cfg_count_modified(PLR_BUFFS, NOPT(PLR_BUFFS), true)))
        v2_rows(K, ctrl_uid_i(base, 41), PLR_BUFFS, NOPT(PLR_BUFFS), x, ry, w);
    {   // EQUIPMENT AND GIL : where the grid goes (Off / in the box / standalone), that mode's rows, then the gil -- which
        // rides the equipment (under the docked grid, beside the standalone one) or the identity row with no equipment
        const bool eqOn = c.plrEquip != 0, alone = eqOn && c.plrEquipDetach;
        int nMod = diff2(c.plrEquip, c.plrEquipDetach, D.plrEquip, D.plrEquipDetach);
        if (eqOn) nMod += (alone ? cfg_count_modified(PLR_EQ_ALONE, NOPT(PLR_EQ_ALONE), true) : cfg_count_modified(PLR_EQ_DOCK, NOPT(PLR_EQ_DOCK), true))
                        + cfg_count_modified(PLR_EQ_CELLS, NOPT(PLR_EQ_CELLS), true) + cfg_count_modified(PLR_EQ_BG, NOPT(PLR_EQ_BG), true);
        nMod += alone ? diff2(c.plrGil, c.plrEqGilPlace, D.plrGil, D.plrEqGilPlace) : (c.plrGil != D.plrGil ? 1 : 0);
        if (v2_section(K, ctrl_uid_i(base, 42), 12, "Equipment and gil", "\xC3\x89quipement et gil", x, ry, w, nMod)) {
            choice(ctrl_uid_i(base, 43), "Equipment", "\xC3\x89quipement", c.plrEquip, c.plrEquipDetach, P_EQM_EN, P_EQM_FR, 2, nullptr, D.plrEquip);
            if (eqOn) {
                if (alone) cfg_rows(K, ctrl_uid_i(base, 44), PLR_EQ_ALONE, NOPT(PLR_EQ_ALONE), x, ry, w, true);
                else       cfg_rows(K, ctrl_uid_i(base, 45), PLR_EQ_DOCK, NOPT(PLR_EQ_DOCK), x, ry, w, true);
                cfg_rows(K, ctrl_uid_i(base, 46), PLR_EQ_CELLS, NOPT(PLR_EQ_CELLS), x, ry, w, true);
                if (!c.plrEqThemeBorder) cfg_color_row(K, ctrl_uid_i(base, 47), x, ry, w, &c.plrEqColor);   // the custom border colour
                cfg_rows(K, ctrl_uid_i(base, 48), PLR_EQ_BG, NOPT(PLR_EQ_BG), x, ry, w, true);
                if (c.plrEqCellBgCustom) cfg_color_row(K, ctrl_uid_i(base, 49), x, ry, w, &c.plrEqCellBg);   // the custom background
            }
            if (alone) choice(ctrl_uid_i(base, 50), "Gil", "Gil", c.plrGil, c.plrEqGilPlace, P_GP_EN, P_GP_FR, 4, nullptr, D.plrGil);   // Off / Below / Above / Left / Right
            else if (cfg_toggle_row(K, ctrl_uid_i(base, 51), x, ry, w, tr("Gil", "Gil"), c.plrGil != 0, nullptr, &c.plrGil, 4)) { c.plrGil = !c.plrGil; save_ui_config(); }
            v2_end(K, ry);
        }
        if (alone) {   // the standalone equipment's own frame : exists only in that mode
            BoxStyle& bs = c.plrEqBox;
            const FrameRefs fr = { &bs.on, &bs.border, &bs.alpha, &bs.alphaOwn, &bs.themeCopy, &bs.theme, &bs.hue, &bs.lum };
            if (v2_section(K, ctrl_uid_i(base, 52), 21, "Equipment frame", "Cadre de l'\xC3\xA9quipement", x, ry, w, cfg_frame_modified(fr))) { cfg_frame_rows(K, ctrl_uid_i(base, 53), x, ry, w, fr); v2_end(K, ry); }
        }
    }
    {   // TEXT : the element list follows what is shown (no dead element) ; cfgPlrTextElem_ stays a PLR_* index
        const char* lab[PLR_TE_COUNT]; TextStyle* ts[PLR_TE_COUNT]; int idx[PLR_TE_COUNT]; int n = 0, sel = 0;
        auto add = [&](int k, const char* en, const char* frl, bool shown) {
            if (!shown) return;
            if (k == cfgPlrTextElem_) sel = n;
            lab[n] = tr(en, frl); ts[n] = &c.plrText[k]; idx[n] = k; ++n; };
        add(PLR_NAME,  "Name", "Nom", c.plrName != 0);
        add(PLR_LVL,   "Level", "Niveau", c.plrLvl != 0);
        add(PLR_SPEED, "Speed", "Vitesse", c.plrSpeed != 0);
        add(PLR_HP,    "HP", "PV", c.plrHp != 0);
        add(PLR_MP,    "MP", "PM", c.plrMp != 0);
        add(PLR_TP,    "TP", "TP", c.plrTp != 0);
        add(PLR_CAST,  "Spell", "Sort", c.plrCast != 0);
        add(PLR_GIL,   "Gil", "Gil", c.plrGil != 0);
        if (n > 0 && v2_section(K, ctrl_uid_i(base, 54), 5, "Text", "Texte", x, ry, w, cfg_text_modified(ts, n))) {
            cfg_text_rows(K, ctrl_uid_i(base, 55), x, ry, w, lab, ts, n, &sel);
            cfgPlrTextElem_ = idx[sel < 0 || sel >= n ? 0 : sel];
            v2_end(K, ry);
        }
    }
    g_fade = e;
}
#undef NOPT

} // namespace aio
