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
#include "ui/config_controls.h"   // shared toolkit : cat_header / row_slider / toggle_chip / row_selector + palette + g_fade
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
        // ===== sub-section : DISPLAY (box chrome + size + placement). No outer "Target" wrapper -- opens straight
        //       into its sub-sections like every other module (the sidebar already names the module). =====
        if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Display", "Affichage"), catOpen_[6])) catOpen_[6] = !catOpen_[6];
        ROW_NEXT(42.0f)
        if (catOpen_[6]) {
        // Show : master on/off for the WHOLE Target module (hidden everywhere when off ; other rows stay editable).
        { ROW_BAND(52.0f)
            const float rowH = snap(40.0f), ty = ry + yo; fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, tr("Show", "Afficher"), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
            const float bbw = snap(150.0f), bbh = snap(34.0f), bx2 = coX + ctrlW - bbw, bty = ty + (rowH - bbh) * 0.5f;
            if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx2, bty, bbw, bbh, ui_config().tgtShow ? tr("On", "Oui") : tr("Off", "Non"), ui_config().tgtShow != 0)) { ui_config().tgtShow = !ui_config().tgtShow; save_ui_config(); }
        }
        ROW_NEXT(52.0f)
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
        // Box : draw the frame chrome or not. NO box -> name/%/bar/buffs/timer float with a heavy outline, and the
        // theme rows below are hidden (no chrome to theme).
        { ROW_BAND(52.0f)
            const float rowH = snap(40.0f), ty = ry + yo; fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, tr("Box", "Cadre"), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
            const float bbw = snap(150.0f), bbh = snap(34.0f), bx2 = coX + ctrlW - bbw, bty = ty + (rowH - bbh) * 0.5f;
            if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx2, bty, bbw, bbh, ui_config().tgtBox ? tr("On", "Oui") : tr("None", "Aucun"), ui_config().tgtBox != 0)) { ui_config().tgtBox = !ui_config().tgtBox; save_ui_config(); }
        }
        ROW_NEXT(52.0f)
        if (ui_config().tgtBox) {
        // Transparence : how see-through the box chrome is (the content stays opaque). 0% = solid.
        { ROW_BAND(46.0f)
            const float transp = 1.0f - ui_config().tgtBoxAlpha; char b[16]; sprintf(b, "%d%%", (int)(transp * 100.0f + 0.5f));
            float v01 = clampf(transp, 0.0f, 1.0f);   // full 0..100% range
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Transparency", "Transparence"), b, &v01)) {
                ui_config().tgtBoxAlpha = 1.0f - v01; }
        }
        ROW_NEXT(46.0f)
        // Copy Party : follow the party box theme (hides the own-theme rows below).
        { ROW_BAND(52.0f)
            const float rowH = snap(40.0f), ty = ry + yo; fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, tr("Theme", "Thème"), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
            const float bbw = snap(150.0f), bbh = snap(34.0f), bx2 = coX + ctrlW - bbw, bty = ty + (rowH - bbh) * 0.5f;
            if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx2, bty, bbw, bbh, ui_config().tgtThemeCopy ? tr("Same as Party", "Comme Party") : tr("Custom", "Perso"), ui_config().tgtThemeCopy != 0)) { ui_config().tgtThemeCopy = !ui_config().tgtThemeCopy; save_ui_config(); }
        }
        ROW_NEXT(52.0f)
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
                  const u32 c = box_hue_color(k);
                  if (sel) { cs_add(dev); rrect_glow(dev, xk, yk, cw, ch, snap(6.0f), (c & 0x00FFFFFF) | 0x80000000, snap(6.0f)); cs(dev); }
                  rrect_fill(dev, xk, yk, cw, ch, snap(6.0f), c, shade(c, -0.28f));
                  outline(dev, xk, yk, cw, ch, sel ? 0xFFFFFFFF : C_BORDER);
              }
              if (inrect(mo, xk, yk, cw, ch) && click) { ui_config().tgtTheme = window_theme_index(fam, k); save_ui_config(); }
          }
          ROW_NEXT(slotH)
        }
        // Luminosity : darken / lighten the chosen box-theme colour (procedural families only).
        if (window_theme_family(ui_config().tgtTheme) != 0)
        { ROW_BAND(46.0f)
            float v01 = (ui_config().tgtLum + 1.0f) * 0.5f; v01 = clampf(v01, 0.0f, 1.0f);
            const int pct = (int)(ui_config().tgtLum * 100.0f + (ui_config().tgtLum >= 0.0f ? 0.5f : -0.5f));
            char b[16]; sprintf(b, "%+d%%", pct);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Luminosity", "Luminosité"), b, &v01)) {
                ui_config().tgtLum = v01 * 2.0f - 1.0f; save_ui_config(); }
          ROW_NEXT(46.0f)
        }
        }   // end own-theme rows (!tgtThemeCopy)
        }   // end theme rows (tgtBox on)
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
        }   // end sub-section Box / Frame (catOpen_[6])
        // ---- sub-section : Bars ----
        if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Bars", "Barres"), catOpen_[7])) catOpen_[7] = !catOpen_[7];
        ROW_NEXT(42.0f)
        if (catOpen_[7]) {
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
        // Distance Height : the range gauge height (like Bar Height for the HP bar).
        if (ui_config().tgtRange)
        { ROW_BAND(46.0f)
            const float lo = 0.60f, hi = 2.00f; char b[16]; sprintf(b, "%d%%", (int)(ui_config().tgtRangeH * 100.0f + 0.5f));
            float v01 = (ui_config().tgtRangeH - lo) / (hi - lo); v01 = clampf(v01, 0.0f, 1.0f);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Distance Height", "Hauteur distance"), b, &v01)) {   // 213 : distinct from the Text "Size" slider (both were 210 -> dragging one wrote the other)
                float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f; ui_config().tgtRangeH = v < lo ? lo : (v > hi ? hi : v); }
          ROW_NEXT(46.0f)
        }
        }   // end sub-section Bars (catOpen_[7])
        // ---- sub-section : Detail ----
        if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Detail", "Détail"), catOpen_[8])) catOpen_[8] = !catOpen_[8];
        ROW_NEXT(42.0f)
        if (catOpen_[8]) {
        // Name colour by hostility : red = claimed by another, orange = engaged/in combat.
        { ROW_BAND(52.0f)
            const float rowH = snap(40.0f), ty = ry + yo;
            fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, tr("Name colour", "Couleur du nom"), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
            const float bbw = snap(140.0f), bbh = snap(34.0f), bx2 = coX + ctrlW - bbw, bty = ty + (rowH - bbh) * 0.5f;
            if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx2, bty, bbw, bbh, ui_config().tgtNameHostile ? tr("By hostility", "Par hostilité") : tr("Fixed", "Fixe"), ui_config().tgtNameHostile != 0)) { ui_config().tgtNameHostile = !ui_config().tgtNameHostile; save_ui_config(); }
        }
        ROW_NEXT(52.0f)
        // Sub-target (<st>) box PLACEMENT relative to the main box (shown while a sub-target cursor is up).
        { ROW_BAND(52.0f)
            const char* SUBPOS[6] = { tr("Above-Right", "Haut-Droite"), tr("Above-Left", "Haut-Gauche"),
                                      tr("Below-Right", "Bas-Droite"),  tr("Below-Left", "Bas-Gauche"),
                                      tr("Right", "Droite"),            tr("Left", "Gauche") };
            int sp = (ui_config().tgtSubPos < 0 || ui_config().tgtSubPos >= 6) ? 0 : ui_config().tgtSubPos;
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Sub-target", "Sous-cible"), SUBPOS[sp])) {
                ui_config().tgtSubPos = wrap(sp + d, 6); save_ui_config(); }
        }
        ROW_NEXT(52.0f)
        // Speed : show the target's movement speed % (green = slower, red = faster) in the detail row.
        { ROW_BAND(52.0f)
            const float rowH = snap(40.0f), ty = ry + yo; fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, tr("Speed", "Vitesse"), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
            const float bbw = snap(112.0f), bbh = snap(34.0f), bx2 = coX + ctrlW - bbw, bty = ty + (rowH - bbh) * 0.5f;
            if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx2, bty, bbw, bbh, ui_config().tgtSpeed ? tr("On", "Oui") : tr("Off", "Non"), ui_config().tgtSpeed != 0)) { ui_config().tgtSpeed = !ui_config().tgtSpeed; save_ui_config(); }
        }
        ROW_NEXT(52.0f)
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
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Speed/TH Icon Size", "Taille icônes Vit/TH"), b, &v01)) {
                float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f; ui_config().tgtDetailIconSz = v < lo ? lo : (v > hi ? hi : v); }
          ROW_NEXT(46.0f)
        }
        // Treasure Hunter : show the TH level applied to the target (packet-tracked).
        { ROW_BAND(52.0f)
            const float rowH = snap(40.0f), ty = ry + yo; fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, tr("Treasure Hunter", "Treasure Hunter"), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
            const float bbw = snap(112.0f), bbh = snap(34.0f), bx2 = coX + ctrlW - bbw, bty = ty + (rowH - bbh) * 0.5f;
            if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx2, bty, bbw, bbh, ui_config().tgtTH ? tr("On", "Oui") : tr("Off", "Non"), ui_config().tgtTH != 0)) { ui_config().tgtTH = !ui_config().tgtTH; save_ui_config(); }
        }
        ROW_NEXT(52.0f)
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
        }   // end sub-section Detail (catOpen_[8])
        // ---- sub-section : Debuffs ----
        if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Debuffs", "Debuffs"), catOpen_[9])) catOpen_[9] = !catOpen_[9];
        ROW_NEXT(42.0f)
        if (catOpen_[9]) {
        // Debuffs : show the tracked debuff icons under the HP bar (grows the box downward).
        { ROW_BAND(52.0f)
            const float rowH = snap(40.0f), ty = ry + yo;
            fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, tr("Debuffs", "Debuffs"), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
            const float bbw = snap(112.0f), bbh = snap(34.0f), bx2 = coX + ctrlW - bbw, bty = ty + (rowH - bbh) * 0.5f;
            if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx2, bty, bbw, bbh, ui_config().tgtDebuffs ? tr("On", "Oui") : tr("Off", "Non"), ui_config().tgtDebuffs != 0)) { ui_config().tgtDebuffs = !ui_config().tgtDebuffs; save_ui_config(); }
        }
        ROW_NEXT(52.0f)
        // Max Debuffs : how many debuff icons to show at most (1..20 ; wraps 10 per row/column).
        if (ui_config().tgtDebuffs)
        { ROW_BAND(46.0f)
            const float lo = 1.0f, hi = 20.0f; int cur = ui_config().tgtBuffMax; if (cur < 1) cur = 1; if (cur > 20) cur = 20;
            char b[16]; sprintf(b, "%d", cur);
            float v01 = ((float)cur - lo) / (hi - lo); v01 = clampf(v01, 0.0f, 1.0f);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Max Debuffs", "Max debuffs"), b, &v01)) {
                int v = (int)(lo + v01 * (hi - lo) + 0.5f); ui_config().tgtBuffMax = v < 1 ? 1 : (v > 20 ? 20 : v); }
          ROW_NEXT(46.0f)
        }
        // Buff position : where the debuff row sits -- inside the box (grows it) or floating outside on a side.
        if (ui_config().tgtDebuffs)
        { ROW_BAND(52.0f)
            const char* BPL[5] = { tr("Inside", "Dans la box"), tr("Below", "Dessous"), tr("Above", "Dessus"), tr("Left", "Gauche"), tr("Right", "Droite") };
            const bool boxOff = !ui_config().tgtBox;                      // no box -> "Inside" is meaningless : offer only 1..4
            int bp = (ui_config().tgtBuffPos < 0 || ui_config().tgtBuffPos > 4) ? 0 : ui_config().tgtBuffPos;
            if (boxOff && bp == 0) { bp = 1; ui_config().tgtBuffPos = 1; save_ui_config(); }   // coerce Inside -> Below when box off
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Buff position", "Position buffs"), BPL[bp])) {
                int nv = boxOff ? (1 + wrap(bp - 1 + d, 4)) : wrap(bp + d, 5);   // box off cycles 1..4 only
                ui_config().tgtBuffPos = nv; save_ui_config(); }
        }
        ROW_NEXT(52.0f)
        // Buff Size : the debuff icon size.
        if (ui_config().tgtDebuffs)
        { ROW_BAND(46.0f)
            const float lo = 0.50f, hi = 2.00f; char b[16]; sprintf(b, "%d%%", (int)(ui_config().tgtIconSz * 100.0f + 0.5f));
            float v01 = (ui_config().tgtIconSz - lo) / (hi - lo); v01 = clampf(v01, 0.0f, 1.0f);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Buff Size", "Taille buffs"), b, &v01)) {
                float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f; ui_config().tgtIconSz = v < lo ? lo : (v > hi ? hi : v); }
          ROW_NEXT(46.0f)
        }
        // Debuff Timers : the countdown number under each icon (needs Debuffs on ; off = icons only).
        if (ui_config().tgtDebuffs)
        { ROW_BAND(52.0f)
            const float rowH = snap(40.0f), ty = ry + yo;
            fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, tr("Debuff Timers", "Minuteurs debuff"), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
            const float bbw = snap(112.0f), bbh = snap(34.0f), bx2 = coX + ctrlW - bbw, bty = ty + (rowH - bbh) * 0.5f;
            if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx2, bty, bbw, bbh, ui_config().tgtTimers ? tr("On", "Oui") : tr("Off", "Non"), ui_config().tgtTimers != 0)) { ui_config().tgtTimers = !ui_config().tgtTimers; save_ui_config(); }
        }
        ROW_NEXT(52.0f)
        }   // end sub-section Debuffs (catOpen_[9])
        // ---- Typography sub-section : per-element Font / Size / Outline / Style / Colour (Name / HP% / Timer) ----
        if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Text", "Texte"), catOpen_[5])) catOpen_[5] = !catOpen_[5];
        ROW_NEXT(42.0f)
        if (catOpen_[5]) {
        { ROW_BAND(52.0f)   // element selector
            const char* TLBL[TGT_TE_COUNT] = { tr("Name", "Nom"), tr("HP%", "PV%"), tr("Timer", "Minuteur"), tr("Speed", "Vitesse"), tr("TH", "TH"), tr("Distance", "Distance") };
            int te = (cfgTgtTextElem_ < 0 || cfgTgtTextElem_ >= TGT_TE_COUNT) ? 0 : cfgTgtTextElem_;
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Element", "Élément"), TLBL[te])) {
                cfgTgtTextElem_ = wrap(te + d, TGT_TE_COUNT); }
        }
        ROW_NEXT(52.0f)
        draw_text_style(dev, fo, mo, click, ry, ri, e, bandX, bandW, coX, ctrlW,
                        ui_config().tgtText[(cfgTgtTextElem_ < 0 || cfgTgtTextElem_ >= TGT_TE_COUNT) ? 0 : cfgTgtTextElem_], true);
        }   // end Text sub-section (catOpen_[5])
        ry += snap(10.0f);

    #undef ROW_BAND
    #undef ROW_NEXT
}

} // namespace aio
