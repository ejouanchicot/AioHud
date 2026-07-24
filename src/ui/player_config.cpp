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
#include "model/ui_config.h"      // ui_config(), save
#include "gfx/font.h"
#include "gfx/draw.h"
#include "gfx/window.h"           // box themes : window_theme_family/variant/index/name/is_proc, box_family_*, box_hue_*
#include <cstdio>
#include <cstring>

namespace aio {

// one On/Off toggle row (label left, chip right). Its own { } block (ROW_BAND declares ap/yo).
// Thin wrapper over the shared row_toggle (config_controls) -- kept as a macro so each call lands on its own __LINE__.
#define PLR_TOGGLE(UID, LABEL, FIELD) { ROW_BAND(48.0f) row_toggle(dev, fo, mo, click, UID, coX, ry + yo, ctrlW, LABEL, &(FIELD)); } ROW_NEXT(48.0f)

// draw the Player category (+ its Box / Content / Bars / Buffs sub-sections). Advances ry/ri.
void ConfigPage::draw_player_config(u32 dev, Font* fo, const MouseState* mo, bool click,
                                    float& ry, int& ri, float e,
                                    float bandX, float bandW, float coX, float ctrlW,
                                    float hdrX, float hdrW) {
        // ===== sub-section : DISPLAY (box chrome + size). No outer "Player" wrapper -- opens straight into its
        //       sub-sections like every other module (the sidebar already names the module). =====
        if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Display", "Affichage"), catOpen_[6])) catOpen_[6] = !catOpen_[6];
        ROW_NEXT(42.0f)
        if (catOpen_[6]) {
        // Show : master on/off for the WHOLE Player Hub (hidden everywhere when off ; other rows stay editable).
        { ROW_BAND(52.0f)
            const float rowH = snap(40.0f), ty = ry + yo; fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, tr("Show", "Afficher"), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
            const float bbw = snap(150.0f), bbh = snap(34.0f), bx2 = coX + ctrlW - bbw, bty = ty + (rowH - bbh) * 0.5f;
            if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx2, bty, bbw, bbh, ui_config().plrShow ? tr("On", "Oui") : tr("Off", "Non"), ui_config().plrShow != 0)) { ui_config().plrShow = !ui_config().plrShow; save_ui_config(); }
        }
        ROW_NEXT(52.0f)
        // Size : scale the whole hub box (canonical : right after Show, before the Box chrome).
        { ROW_BAND(46.0f)
            const float lo = 0.50f, hi = 2.00f; char b[16]; sprintf(b, "%d%%", (int)(ui_config().plrScale * 100.0f + 0.5f));
            float v01 = (ui_config().plrScale - lo) / (hi - lo); v01 = clampf(v01, 0.0f, 1.0f);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Size", "Taille"), b, &v01)) {
                float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f; ui_config().plrScale = v < lo ? lo : (v > hi ? hi : v); }
        }
        ROW_NEXT(46.0f)
        // Box : draw the frame chrome or not.
        { ROW_BAND(52.0f)
            const float rowH = snap(40.0f), ty = ry + yo; fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, tr("Box", "Cadre"), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
            const float bbw = snap(150.0f), bbh = snap(34.0f), bx2 = coX + ctrlW - bbw, bty = ty + (rowH - bbh) * 0.5f;
            if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx2, bty, bbw, bbh, ui_config().plrBox ? tr("On", "Oui") : tr("None", "Aucun"), ui_config().plrBox != 0)) { ui_config().plrBox = !ui_config().plrBox; save_ui_config(); }
        }
        ROW_NEXT(52.0f)
        if (ui_config().plrBox) {
        // Transparency : how see-through the box chrome is (content stays opaque). 0% = solid.
        { ROW_BAND(46.0f)
            const float transp = 1.0f - ui_config().plrBoxAlpha; char b[16]; sprintf(b, "%d%%", (int)(transp * 100.0f + 0.5f));
            float v01 = clampf(transp, 0.0f, 1.0f);   // full 0..100% range
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Transparency", "Transparence"), b, &v01)) {
                ui_config().plrBoxAlpha = 1.0f - v01; }
        }
        ROW_NEXT(46.0f)
        // Copy Party : follow the party box theme (hides the own-theme rows below).
        { ROW_BAND(52.0f)
            const float rowH = snap(40.0f), ty = ry + yo; fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, tr("Theme", "Thème"), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
            const float bbw = snap(150.0f), bbh = snap(34.0f), bx2 = coX + ctrlW - bbw, bty = ty + (rowH - bbh) * 0.5f;
            if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx2, bty, bbw, bbh, ui_config().plrThemeCopy ? tr("Same as Party", "Comme Party") : tr("Custom", "Perso"), ui_config().plrThemeCopy != 0)) { ui_config().plrThemeCopy = !ui_config().plrThemeCopy; save_ui_config(); }
        }
        ROW_NEXT(52.0f)
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
          const bool isFFXI = (fam == 0);
          const int nVar = isFFXI ? window_tex_theme_count() : box_hue_count();
          const int COLS = isFFXI ? (nVar < 1 ? 1 : nVar) : 15;
          const int nrows = (nVar + COLS - 1) / COLS;
          const float cw = isFFXI ? snap(42.0f) : snap(22.0f), ch = isFFXI ? snap(26.0f) : snap(22.0f), cg = snap(7.0f);
          const float gridH = nrows * ch + (nrows - 1) * cg, slotH = gridH + snap(20.0f);
          ROW_BAND(slotH) (void)yo;
          fo->begin(dev);
          fo->draw_lc(dev, coX + snap(4.0f), ry + slotH * 0.5f, isFFXI ? tr("Theme", "Thème") : tr("Colour", "Couleur"), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
          const float gridW = COLS * cw + (COLS - 1) * cg;
          const float gx = coX + ctrlW - gridW, gy = ry + (slotH - gridH) * 0.5f;
          for (int k = 0; k < nVar; ++k) {
              const float xk = gx + (k % COLS) * (cw + cg), yk = gy + (k / COLS) * (ch + cg);
              const bool sel = (var == k);
              if (isFFXI) {
                  rpanel(dev, xk, yk, cw, ch, snap(6.0f), sel ? C_ROWON_T : 0x66121A18, sel ? C_ROWON_B : 0x66090D0F, sel ? C_ACCENT : C_BORDER, snap(1.2f));
                  fo->begin(dev); fo->draw_c(dev, xk + cw * 0.5f, yk + ch * 0.5f, window_theme_name(k), snap(13.0f), fa(sel ? C_ACCENTHI : C_TEXT), fa(C_STROKE), 1.0f);
              } else {
                  const u32 hc = box_hue_color(k);
                  if (sel) { cs_add(dev); rrect_glow(dev, xk, yk, cw, ch, snap(6.0f), (hc & 0x00FFFFFF) | 0x80000000, snap(6.0f)); cs(dev); }
                  rrect_fill(dev, xk, yk, cw, ch, snap(6.0f), hc, shade(hc, -0.28f));
                  outline(dev, xk, yk, cw, ch, sel ? 0xFFFFFFFF : C_BORDER);
              }
              if (inrect(mo, xk, yk, cw, ch) && click) { ui_config().plrTheme = window_theme_index(fam, k); save_ui_config(); }
          }
          ROW_NEXT(slotH)
        }
        // Luminosity : darken / lighten the chosen box-theme colour (procedural families only).
        if (window_theme_family(ui_config().plrTheme) != 0)
        { ROW_BAND(46.0f)
            float v01 = (ui_config().plrLum + 1.0f) * 0.5f; v01 = clampf(v01, 0.0f, 1.0f);
            const int pct = (int)(ui_config().plrLum * 100.0f + (ui_config().plrLum >= 0.0f ? 0.5f : -0.5f));
            char b[16]; sprintf(b, "%+d%%", pct);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Luminosity", "Luminosité"), b, &v01)) {
                ui_config().plrLum = v01 * 2.0f - 1.0f; save_ui_config(); }
          ROW_NEXT(46.0f)
        }
        }   // end own-theme rows (!plrThemeCopy)
        }
        }   // end sub-section Box (catOpen_[6])
        // ---- sub-section : Content (what the hub shows) ----
        if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Content", "Contenu"), catOpen_[7])) catOpen_[7] = !catOpen_[7];
        ROW_NEXT(42.0f)
        if (catOpen_[7]) {
        PLR_TOGGLE(CTRL_ID, tr("Emblem", "Emblème"), ui_config().plrEmblem)
        PLR_TOGGLE(CTRL_ID, tr("Name", "Nom"),       ui_config().plrName)
        PLR_TOGGLE(CTRL_ID, tr("Level", "Niveau"),   ui_config().plrLvl)
        PLR_TOGGLE(CTRL_ID, "HP",               ui_config().plrHp)
        PLR_TOGGLE(CTRL_ID, "MP",               ui_config().plrMp)
        PLR_TOGGLE(CTRL_ID, "TP",               ui_config().plrTp)
        PLR_TOGGLE(CTRL_ID, tr("Speed", "Vitesse"), ui_config().plrSpeed)
        PLR_TOGGLE(CTRL_ID, tr("Cast", "Sort"), ui_config().plrCast)
        PLR_TOGGLE(CTRL_ID, tr("Cast placeholder", "Sort fictif"), ui_config().plrCastDemo)
        PLR_TOGGLE(CTRL_ID, tr("Buffs", "Buffs"), ui_config().plrBuffs)
        PLR_TOGGLE(CTRL_ID, tr("Equipment", "Équipement"), ui_config().plrEquip)
        // Gil : lives in Content while the equipment is docked or off (gil then rides the header / hub) ; when the
        // equipment is DETACHED the gil goes with that box, so its toggle moves to the Equipment sub-section.
        if (!(ui_config().plrEquip && ui_config().plrEquipDetach)) { PLR_TOGGLE(CTRL_ID, tr("Gil", "Gil"), ui_config().plrGil) }
        }   // end sub-section Content (catOpen_[7])
        // ---- sub-section : Identity (the job emblem is an icon -> size only) ----
        if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Identity", "Identité"), catOpen_[11])) catOpen_[11] = !catOpen_[11];
        ROW_NEXT(42.0f)
        if (catOpen_[11]) {
        // Emblem Size
        { ROW_BAND(46.0f)
            const float lo = 0.50f, hi = 2.00f; char b[16]; sprintf(b, "%d%%", (int)(ui_config().plrEmblemSz * 100.0f + 0.5f));
            float v01 = (ui_config().plrEmblemSz - lo) / (hi - lo); v01 = clampf(v01, 0.0f, 1.0f);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Emblem Size", "Taille emblème"), b, &v01)) {
                float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f; ui_config().plrEmblemSz = v < lo ? lo : (v > hi ? hi : v); }
        }
        ROW_NEXT(46.0f)
        }   // end sub-section Identity (catOpen_[11])
        // ---- Typography sub-section : per-element Font / Size / Outline / Style / Colour (Name / Level) ----
        if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Text", "Texte"), catOpen_[5])) catOpen_[5] = !catOpen_[5];
        ROW_NEXT(42.0f)
        if (catOpen_[5]) {
        { ROW_BAND(52.0f)   // element selector
            const char* TLBL[PLR_TE_COUNT] = { tr("Name", "Nom"), tr("Level", "Niveau"), tr("Gil", "Gil"), tr("Speed", "Vitesse"), "HP", "MP", "TP", tr("Cast", "Sort") };
            int te = (cfgPlrTextElem_ < 0 || cfgPlrTextElem_ >= PLR_TE_COUNT) ? 0 : cfgPlrTextElem_;
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Element", "Élément"), TLBL[te])) {
                cfgPlrTextElem_ = wrap(te + d, PLR_TE_COUNT); }
        }
        ROW_NEXT(52.0f)
        draw_text_style(dev, fo, mo, click, ry, ri, e, bandX, bandW, coX, ctrlW,
                        ui_config().plrText[(cfgPlrTextElem_ < 0 || cfgPlrTextElem_ >= PLR_TE_COUNT) ? 0 : cfgPlrTextElem_], true);
        }   // end Text sub-section (catOpen_[5])
        // ---- sub-section : Bars (fiole size) ----
        if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Bars", "Barres"), catOpen_[8])) catOpen_[8] = !catOpen_[8];
        ROW_NEXT(42.0f)
        if (catOpen_[8]) {
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
        }   // end sub-section Bars (catOpen_[8])
        // ---- sub-section : Buffs (only meaningful when Buffs are shown) ----
        if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Buffs", "Buffs"), catOpen_[9])) catOpen_[9] = !catOpen_[9];
        ROW_NEXT(42.0f)
        if (catOpen_[9] && ui_config().plrBuffs) {
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
        }   // end sub-section Buffs (catOpen_[9])
        // ---- sub-section : Equipment (the gear grid) ----
        if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Equipment", "Équipement"), catOpen_[12])) catOpen_[12] = !catOpen_[12];
        ROW_NEXT(42.0f)
        if (catOpen_[12] && ui_config().plrEquip) {
        // Mode : the equipment lives INSIDE the Player Hub (docked, placement below) or as its OWN standalone
        // box with its own size, dragged in //aio edit.
        { ROW_BAND(52.0f)
            const float rowH = snap(40.0f), ty = ry + yo; fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, tr("Mode", "Mode"), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
            const float bbw = snap(150.0f), bbh = snap(34.0f), bx2 = coX + ctrlW - bbw, bty = ty + (rowH - bbh) * 0.5f;
            if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx2, bty, bbw, bbh, ui_config().plrEquipDetach ? tr("Standalone", "Autonome") : tr("In Player", "Dans Player"), ui_config().plrEquipDetach != 0)) { ui_config().plrEquipDetach = !ui_config().plrEquipDetach; save_ui_config(); }
        }
        ROW_NEXT(52.0f)
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
                float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f; ui_config().plrEquipScale = v < lo ? lo : (v > hi ? hi : v); save_ui_config(); }
        }
        ROW_NEXT(46.0f)
        // Standalone box chrome (frame / border / transparency / theme) : DETACHED only ; docked shares the Hub box.
        draw_box_appearance(dev, fo, mo, click, ry, ri, e, bandX, bandW, coX, ctrlW, ui_config().plrEqBox);
        }
        // Gil ON/OFF lives HERE only when the equipment is DETACHED (the gil rides that box) ; docked/off -> Content.
        if (ui_config().plrEquipDetach) { PLR_TOGGLE(CTRL_ID, tr("Gil", "Gil"), ui_config().plrGil) }
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
        { ROW_BAND(52.0f)
            const float rowH = snap(40.0f), ty = ry + yo; fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, tr("Border", "Bordure"), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
            const float bbw = snap(150.0f), bbh = snap(34.0f), bx2 = coX + ctrlW - bbw, bty = ty + (rowH - bbh) * 0.5f;
            if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx2, bty, bbw, bbh, ui_config().plrEqThemeBorder ? tr("Box Theme", "Thème box") : tr("Custom", "Perso"), ui_config().plrEqThemeBorder != 0)) { ui_config().plrEqThemeBorder = !ui_config().plrEqThemeBorder; save_ui_config(); }
        }
        ROW_NEXT(52.0f)
        if (!ui_config().plrEqThemeBorder) {
        CFG_COLOR_PICKER(&ui_config().plrEqColor)
        }   // end custom colour (!plrEqThemeBorder)
        // Cell background : the cell FILL colour -- Default dark, or a custom colour (filled slots stay a touch brighter).
        { ROW_BAND(52.0f)
            const float rowH = snap(40.0f), ty = ry + yo; fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, tr("Cell background", "Fond des cases"), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
            const float bbw = snap(150.0f), bbh = snap(34.0f), bx2 = coX + ctrlW - bbw, bty = ty + (rowH - bbh) * 0.5f;
            if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx2, bty, bbw, bbh, ui_config().plrEqCellBgCustom ? tr("Custom", "Perso") : tr("Default", "Défaut"), ui_config().plrEqCellBgCustom != 0)) { ui_config().plrEqCellBgCustom = !ui_config().plrEqCellBgCustom; save_ui_config(); }
        }
        ROW_NEXT(52.0f)
        if (ui_config().plrEqCellBgCustom) {
        CFG_COLOR_PICKER(&ui_config().plrEqCellBg)
        }   // end custom cell background
        }   // end sub-section Equipment (catOpen_[12])
        ry += snap(10.0f);

    #undef PLR_TOGGLE
    #undef ROW_BAND
    #undef ROW_NEXT
}

} // namespace aio
