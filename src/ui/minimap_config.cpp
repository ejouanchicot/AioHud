// minimap_config.cpp -- the Minimap module's settings panel (//aio config -> "Minimap").
//
// Split out like the other module panels. Each control is keyed by CTRL_ID (file:line hash, see
// config_controls.h) -- no hand-numbered uids. Reuses the shared sub-section
// catOpen_ slots [5]/[6]/[7]/[8]/[9] (only one module page is visible at a time). Defined as a ConfigPage
// method so it reaches the shared edit state ; the page hands us its running layout cursor by reference.
#include "ui/config_page.h"
#include "ui/config_controls.h"    // cat_header / row_slider / row_selector / toggle_chip + palette
#include "ui/config_rows.h"        // ROW_BAND / ROW_NEXT
#include "model/ui_config.h"
#include "gfx/font.h"
#include "gfx/draw.h"
#include <cstdio>

namespace aio {

// one On/Off toggle row (label left, chip right) -- mirrors PLR_TOGGLE.
#define MM_TOGGLE(UID, LABEL, FIELD)                                                                                     \
    { ROW_BAND(48.0f)                                                                                                  \
        const float rowH = snap(38.0f), ty = ry + yo; fo->begin(dev);                                                 \
        fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, LABEL, snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);      \
        const float bbw = snap(112.0f), bbh = snap(34.0f), bx2 = coX + ctrlW - bbw, bty = ty + (rowH - bbh) * 0.5f;    \
        if (toggle_chip(dev, fo, mo, click, UID, bx2, bty, bbw, bbh, (FIELD) ? tr("On", "Oui") : tr("Off", "Non"), (FIELD) != 0)) { FIELD = !(FIELD); save_ui_config(); } \
    } ROW_NEXT(48.0f)

// a 0.50..2.00 (x%) size slider bound to a float FIELD, on uid UID.
#define MM_PCT_SLIDER(UID, LABEL, FIELD, LO, HI)                                                                        \
    { ROW_BAND(46.0f)                                                                                                  \
        const float lo = (LO), hi = (HI); char b[16]; sprintf(b, "%d%%", (int)((FIELD) * 100.0f + 0.5f));              \
        float v01 = ((FIELD) - lo) / (hi - lo); v01 = clampf(v01, 0.0f, 1.0f);                                         \
        if (row_slider(dev, fo, mo, UID, coX, ry + yo, ctrlW, LABEL, b, &v01)) {                                       \
            float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f; FIELD = v < lo ? lo : (v > hi ? hi : v); } \
    } ROW_NEXT(46.0f)

void ConfigPage::draw_minimap_config(u32 dev, Font* fo, const MouseState* mo, bool click,
                                     float& ry, int& ri, float e,
                                     float bandX, float bandW, float coX, float ctrlW,
                                     float hdrX, float hdrW) {
        UiConfig& c = ui_config();

        // ===== sub-section : DISPLAY =====
        if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Display", "Affichage"), catOpen_[6])) catOpen_[6] = !catOpen_[6];
        ROW_NEXT(42.0f)
        if (catOpen_[6]) {
        MM_TOGGLE(CTRL_ID, tr("Show", "Afficher"), c.mmShow)
        MM_PCT_SLIDER(CTRL_ID, tr("Size", "Taille"), c.mmScale, 0.50f, 2.00f)
        MM_PCT_SLIDER(CTRL_ID, tr("Map size", "Taille carte"), c.mmMapSize, 0.50f, 1.60f)
        // Zoom (1x .. 24x)
        { ROW_BAND(46.0f)
            const float lo = 1.0f, hi = 24.0f; char b[16]; sprintf(b, "%.1fx", c.mmZoom);
            float v01 = (c.mmZoom - lo) / (hi - lo); v01 = clampf(v01, 0.0f, 1.0f);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Zoom", "Zoom"), b, &v01)) {
                float v = lo + v01 * (hi - lo); c.mmZoom = v < lo ? lo : (v > hi ? hi : v); }
        }
        ROW_NEXT(46.0f)
        // Background opacity (0 = transparent, the game shows through)
        { ROW_BAND(46.0f)
            char b[16]; sprintf(b, "%d%%", (int)(c.mmBgAlpha * 100.0f + 0.5f));
            float v01 = clampf(c.mmBgAlpha, 0.0f, 1.0f);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Map background", "Fond carte"), b, &v01)) c.mmBgAlpha = v01;
        }
        ROW_NEXT(46.0f)
        }   // end Display

        // ===== sub-section : CLOCK (Vana'diel header) =====
        if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Clock", "Horloge"), catOpen_[9])) catOpen_[9] = !catOpen_[9];
        ROW_NEXT(42.0f)
        if (catOpen_[9]) {
        MM_TOGGLE(CTRL_ID, tr("Clock header", "En-t\xC3\xAAte horloge"), c.mmClock)
        if (c.mmClock) {
        MM_TOGGLE(CTRL_ID, tr("Vana'diel time", "Heure Vana'diel"), c.mmClkTime)
        MM_TOGGLE(CTRL_ID, tr("Elemental day", "Jour \xC3\xA9l\xC3\xA9mentaire"), c.mmClkDay)
        MM_TOGGLE(CTRL_ID, tr("Moon phase", "Phase de lune"), c.mmClkMoon)
        MM_TOGGLE(CTRL_ID, tr("Real / GMT time", "Heure r\xC3\xA9""elle / GMT"), c.mmClkReal)
        { ROW_BAND(52.0f)
            static const char* PLBL_EN[4] = { "Top", "Bottom", "Left", "Right" }; static const char* PLBL_FR[4] = { "Haut", "Bas", "Gauche", "Droite" };
            int cp = (c.mmClockPos < 0 || c.mmClockPos > 3) ? 0 : c.mmClockPos;
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Clock position", "Position horloge"), tr(PLBL_EN[cp], PLBL_FR[cp]))) { c.mmClockPos = wrap(cp + d, 4); save_ui_config(); }
        } ROW_NEXT(52.0f)
        draw_box_appearance(dev, fo, mo, click, ry, ri, e, bandX, bandW, coX, ctrlW, c.mmBox);   // clock-box chrome : Box / Transparency / Theme / Hue / Luminosity
        }
        }   // end Clock

        // ===== sub-section : TEXT (clock typography) =====
        if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Text", "Texte"), catOpen_[5])) catOpen_[5] = !catOpen_[5];
        ROW_NEXT(42.0f)
        if (catOpen_[5] && c.mmClock) {
        { ROW_BAND(52.0f)   // element selector
            const char* TLBL[MM_TE_COUNT] = { tr("Time", "Heure"), tr("Day", "Jour"), tr("Moon", "Lune"), tr("Real / GMT", "R\xC3\xA9""el / GMT") };
            int te = (cfgMmTextElem_ < 0 || cfgMmTextElem_ >= MM_TE_COUNT) ? 0 : cfgMmTextElem_;
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Element", "\xC3\x89l\xC3\xA9ment"), TLBL[te])) {
                cfgMmTextElem_ = wrap(te + d, MM_TE_COUNT); }
        }
        ROW_NEXT(52.0f)
        draw_text_style(dev, fo, mo, click, ry, ri, e, bandX, bandW, coX, ctrlW,
                        c.mmText[(cfgMmTextElem_ < 0 || cfgMmTextElem_ >= MM_TE_COUNT) ? 0 : cfgMmTextElem_], true);
        }   // end Text

        // ===== sub-section : SHAPE & FRAME =====
        if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Shape & Frame", "Forme & Cadre"), catOpen_[7])) catOpen_[7] = !catOpen_[7];
        ROW_NEXT(42.0f)
        if (catOpen_[7]) {
        // Shape : Square / Round
        { ROW_BAND(52.0f)
            static const char* SH_EN[2] = { "Square", "Round" }; static const char* SH_FR[2] = { "Carr\xC3\xA9", "Rond" };
            int sh = (c.mmShape == 1) ? 1 : 0;
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Shape", "Forme"), tr(SH_EN[sh], SH_FR[sh]))) { c.mmShape = wrap(sh + d, 2); save_ui_config(); }
        }
        ROW_NEXT(52.0f)
        // Frame : None / Box Theme / Custom
        { ROW_BAND(52.0f)
            static const char* FR_EN[3] = { "None", "Box Theme", "Custom" }; static const char* FR_FR[3] = { "Aucun", "Th\xC3\xA8me box", "Perso" };
            int fr = (c.mmFrame < 0 || c.mmFrame > 2) ? 0 : c.mmFrame;
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Frame", "Cadre"), tr(FR_EN[fr], FR_FR[fr]))) { c.mmFrame = wrap(fr + d, 3); save_ui_config(); }
        }
        ROW_NEXT(52.0f)
        if (c.mmFrame == 2)   // custom frame colour : HSV picker
            CFG_COLOR_PICKER(&c.mmFrameColor)
        // --- per-shape frame thickness / bezel options ---
        if (c.mmShape == 1) {   // ROUND : brass bezel ring
            MM_TOGGLE(CTRL_ID, tr("Bezel ring", "Anneau bordure"), c.mmBezel)
            if (c.mmBezel) {
                MM_PCT_SLIDER(CTRL_ID, tr("Bezel width", "Largeur anneau"), c.mmBezelW, 0.50f, 2.00f)
                MM_PCT_SLIDER(CTRL_ID, tr("Cardinal size", "Taille cardinaux"), c.mmCardSz, 0.50f, 2.00f)
            }
        } else {                // SQUARE : frame/border thickness
            MM_PCT_SLIDER(CTRL_ID, tr("Border width", "Largeur bordure"), c.mmSqBorder, 0.50f, 2.00f)
        }
        }   // end Shape & Frame

        // ===== sub-section : MARKERS =====
        if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Markers", "Marqueurs"), catOpen_[8])) catOpen_[8] = !catOpen_[8];
        ROW_NEXT(42.0f)
        if (catOpen_[8]) {
        MM_PCT_SLIDER(CTRL_ID, tr("Marker Size", "Taille marqueurs"), c.mmMarkerScale, 0.50f, 2.00f)
        MM_TOGGLE(CTRL_ID, tr("Players (PC)", "Joueurs (PC)"), c.mmPC)
        MM_TOGGLE(CTRL_ID, tr("NPCs", "PNJ"),                  c.mmNPC)
        MM_TOGGLE(CTRL_ID, tr("Monsters", "Monstres"),         c.mmMob)
        // --- Target line : you -> your <t> target ---
        MM_TOGGLE(CTRL_ID, tr("Target line", "Ligne cible"), c.mmTgtLine)
        if (c.mmTgtLine)
            CFG_COLOR_PICKER(&c.mmTgtLineCol)
        // --- Range ring : a pull / aggro distance gauge around you ---
        MM_TOGGLE(CTRL_ID, tr("Range ring", "Anneau de port\xC3\xA9""e"), c.mmRing)
        if (c.mmRing) {
            { ROW_BAND(46.0f)   // radius (3 .. 50 yalms)
                const float lo = 3.0f, hi = 50.0f; char b[16]; sprintf(b, "%d", (int)(c.mmRingR + 0.5f));
                float v01 = (c.mmRingR - lo) / (hi - lo); v01 = clampf(v01, 0.0f, 1.0f);
                if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Radius", "Rayon"), b, &v01)) {
                    float v = lo + v01 * (hi - lo); c.mmRingR = v < lo ? lo : (v > hi ? hi : v); }
            }
            ROW_NEXT(46.0f)
            CFG_COLOR_PICKER(&c.mmRingCol)
        }
        }   // end Markers

        ry += snap(10.0f);
    #undef MM_TOGGLE
    #undef MM_PCT_SLIDER
    #undef ROW_BAND
    #undef ROW_NEXT
}

} // namespace aio
