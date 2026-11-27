// minimap_config.cpp -- the Minimap module's settings panel (//aio config -> "Minimap").
//
// Split out like the other module panels. Each control is keyed by CTRL_ID (file:line hash, see
// config_controls.h) -- no hand-numbered uids. Reuses the shared sub-section
// catOpen_ slots [5]/[6]/[7]/[8]/[9] (only one module page is visible at a time). Defined as a ConfigPage
// method so it reaches the shared edit state ; the page hands us its running layout cursor by reference.
#include "ui/config_page.h"
#include "ui/config_controls.h"    // cat_header / row_slider / row_selector / toggle_chip + palette
#include "ui/config_rows.h"        // ROW_BAND / ROW_NEXT
#include "ui/cfg_engine.h"         // the redesign : OptDef tables + the kit (draw_minimap_v2)
#include "model/ui_config.h"
#include "gfx/font.h"
#include "gfx/draw.h"
#include <cstdio>
#include <cmath>

namespace aio {

// one On/Off toggle row (label left, chip right) -- mirrors PLR_TOGGLE.
// Thin wrappers over the shared row_toggle / row_pct_slider (config_controls). Kept as macros only so call sites read
// the same and each expansion lands on its own __LINE__ for CTRL_ID ; the body is now one shared helper call.
#define MM_PCT_SLIDER(UID, LABEL, FIELD, LO, HI) { ROW_BAND(46.0f) row_pct_slider(dev, fo, mo, UID, coX, ry + yo, ctrlW, LABEL, &(FIELD), (LO), (HI)); } ROW_NEXT(46.0f)

void ConfigPage::draw_minimap_config(u32 dev, Font* fo, const MouseState* mo, bool click,
                                     float& ry, int& ri, float e,
                                     float bandX, float bandW, float coX, float ctrlW,
                                     float hdrX, float hdrW) {
        UiConfig& c = ui_config();

        // ===== sub-section : DISPLAY =====
        // The section FOLDS : cat_fold owns the eased progress, the clip and the cursor (config_controls.h).
        const float aF6_ = cat_fold(CTRL_ID, !ui_config().cfgAdv || (catOpen_[6]));
        cat_panel(dev, hdrX, ry, hdrW, cat_card_h(catH_[6], aF6_));   // the section IS a card, collapsed or not
        if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Display", "Affichage"), catOpen_[6], aF6_) && ui_config().cfgAdv) catOpen_[6] = !catOpen_[6];
        ROW_NEXT(42.0f)
        if (aF6_ > 0.0f) {
            const float top6_ = ry;
            cat_fold_clip(dev, hdrX, top6_, hdrW, catH_[6] * aF6_);
        ROW_TOGGLE(CTRL_ID, tr("Show", "Afficher"), c.mmShow)
        MM_PCT_SLIDER(CTRL_ID, tr("Size", "Taille"), c.mmScale, 0.50f, 2.00f)
        MM_PCT_SLIDER(CTRL_ID, tr("Map size", "Taille carte"), c.mmMapSize, 0.50f, 1.60f)
        // Zoom (1x .. 24x)
        { ROW_BAND(46.0f)
            // BOUNDED print of a value that comes from a FILE. `sprintf("%.1fx")` into char[16] wrote 35 bytes
            // for mmZoom=1e30 -> stack cookie -> the player's client dies on opening this tab. Nothing clamped
            // mmZoom between the config file and this line (the widget clamps into a LOCAL, never back into the
            // config), so a hand-edited or corrupted file reached it directly. Same class as the char[40] +
            // "%.4f" smash fixed in io/json.h in July -- that time the class was never swept for siblings.
            const float lo = 1.0f, hi = 24.0f;
            char b[16]; _snprintf(b, sizeof(b), "%.1fx", clampf(c.mmZoom, lo, hi)); b[sizeof(b) - 1] = 0;
            float v01 = (c.mmZoom - lo) / (hi - lo); v01 = clampf(v01, 0.0f, 1.0f);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Zoom", "Zoom"), b, &v01)) {
                float v = lo + v01 * (hi - lo); c.mmZoom = v < lo ? lo : (v > hi ? hi : v); }
        }
        ROW_NEXT(46.0f)
        // Background opacity (0 = transparent, the game shows through) -- not under a themed square frame, which paints
        // its own interior (minimap.cpp themedOk : square + Box Theme, the default)
        if (!(c.mmShape == 0 && c.mmFrame == 1 && c.mmLayout != 2)) { ROW_BAND(46.0f)
            char b[16]; sprintf(b, "%d%%", (int)(c.mmBgAlpha * 100.0f + 0.5f));
            float v01 = clampf(c.mmBgAlpha, 0.0f, 1.0f);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Map background", "Fond carte"), b, &v01)) c.mmBgAlpha = v01;
          ROW_NEXT(46.0f)
        }
            cat_fold_end(dev, ry, top6_, catH_[6], aF6_);
        }   // end Display
        ry += snap(16.0f);                                 // air between this section and the next title bar

        // ===== sub-section : SHAPE & FRAME =====
        // The section FOLDS : cat_fold owns the eased progress, the clip and the cursor (config_controls.h).
        const float aF7_ = cat_fold(CTRL_ID, !ui_config().cfgAdv || (catOpen_[7]));
        cat_panel(dev, hdrX, ry, hdrW, cat_card_h(catH_[7], aF7_));   // the section IS a card, collapsed or not
        if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Shape & Frame", "Forme & Cadre"), catOpen_[7], aF7_) && ui_config().cfgAdv) catOpen_[7] = !catOpen_[7];
        ROW_NEXT(42.0f)
        if (aF7_ > 0.0f) {
            const float top7_ = ry;
            cat_fold_clip(dev, hdrX, top7_, hdrW, catH_[7] * aF7_);
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
        if (c.mmFrame == 2) {   // custom frame colour : HSV picker (braced : the macro's ROW_NEXT ran even when hidden -- the gap)
            CFG_COLOR_PICKER(&c.mmFrameColor)
        }
        // --- per-shape frame thickness / bezel options ---
        const bool dialSqC = c.mmLayout == 2 && c.mmShape == 0;   // the square watch case (minimap.cpp dialSq)
        if (c.mmShape == 1 && (c.mmClock || c.mmLayout == 2)) {   // ROUND : brass bezel ring (drawn only with the clock header, or the Dial)
            if (c.mmLayout != 2) { ROW_TOGGLE(CTRL_ID, tr("Bezel ring", "Anneau bordure"), c.mmBezel) }   // (the Dial IS its bezel : no toggle)
            if (c.mmBezel || c.mmLayout == 2) {
                MM_PCT_SLIDER(CTRL_ID, tr("Bezel width", "Largeur anneau"), c.mmBezelW, 0.50f, 2.00f)
                MM_PCT_SLIDER(CTRL_ID, tr("Cardinal size", "Taille cardinaux"), c.mmCardSz, 0.50f, 2.00f)
            }
        } else if (dialSqC) {   // the SQUARE DIAL : its case band (mmBezelW -- the square border slider is not read there)
            MM_PCT_SLIDER(CTRL_ID, tr("Case width", "Largeur du bo\xC3\xAEtier"), c.mmBezelW, 0.50f, 2.00f)
        } else if (c.mmShape == 0 && c.mmFrame == 2) {   // SQUARE : the border thickness -- read only for a CUSTOM frame
            MM_PCT_SLIDER(CTRL_ID, tr("Border width", "Largeur bordure"), c.mmSqBorder, 0.50f, 2.00f)
        }
            cat_fold_end(dev, ry, top7_, catH_[7], aF7_);
        }   // end Shape & Frame
        ry += snap(16.0f);                                 // air between this section and the next title bar

        // ===== sub-section : CLOCK (Vana'diel header) =====
        // The section FOLDS : cat_fold owns the eased progress, the clip and the cursor (config_controls.h).
if (ui_config().cfgAdv) {   // Advanced only (the config's depth, 2026-11-26)
        const float aF9_ = cat_fold(CTRL_ID, catOpen_[9]);
        cat_panel(dev, hdrX, ry, hdrW, cat_card_h(catH_[9], aF9_));   // the section IS a card, collapsed or not
        if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Clock", "Horloge"), catOpen_[9], aF9_)) catOpen_[9] = !catOpen_[9];
        ROW_NEXT(42.0f)
        if (aF9_ > 0.0f) {
            const float top9_ = ry;
            cat_fold_clip(dev, hdrX, top9_, hdrW, catH_[9] * aF9_);
        { ROW_BAND(52.0f)   // Classic (stacked header) / Compact (thin bands above / below the map)
            static const char* LL_EN[3] = { "Classic", "Compact", "Dial (round)" }; static const char* LL_FR[3] = { "Classique", "Compact", "Cadran (rond)" };
            const int ly = (c.mmLayout < 0 || c.mmLayout > 2) ? 0 : c.mmLayout;
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Layout", "Disposition"), tr(LL_EN[ly], LL_FR[ly]))) { c.mmLayout = wrap(ly + d, 3); save_ui_config(); }
        } ROW_NEXT(52.0f)
        if (c.mmLayout == 2) {
            { ROW_BAND(52.0f)   // the Dial's day subdial : the enamel globe or the icons' carousel
                static const char* DD_EN[3] = { "Globe (colours)", "Carousel (icons)", "Disc (icons)" }; static const char* DD_FR[3] = { "Globe (couleurs)", "Carrousel (ic\xC3\xB4nes)", "Disque (ic\xC3\xB4nes)" };
                const int dd = (c.mmDayDial < 0 || c.mmDayDial > 2) ? 1 : c.mmDayDial;
                if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Day dial", "Cadran du jour"), tr(DD_EN[dd], DD_FR[dd]))) { c.mmDayDial = wrap(dd + d, 3); save_ui_config(); }
            } ROW_NEXT(52.0f)
            if (c.mmDayDial == 2) {   // the MODULES around the compass : where each one sits
                // the PLACES of the chosen shape : the round -- 4 corners and 2 sides ; the square -- 4 corners and 2 places on
                // each side (the user 2026-11-25), going round the frame
                const bool sqS = c.mmShape == 0;
                static const char* SL_EN[9] = { "Top left", "Left", "Bottom left", "Bottom right", "Right", "Top right", "Off", "Left (low)", "Right (low)" };
                static const char* SL_FR[9] = { "Haut gauche", "Gauche", "Bas gauche", "Bas droite", "Droite", "Haut droite", "Aucun", "Gauche (bas)", "Droite (bas)" };
                static const char* SQ_EN[9] = { "Top left", "Left (high)", "Bottom left", "Bottom right", "Right (high)", "Top right", "Off", "Left (low)", "Right (low)" };
                static const char* SQ_FR[9] = { "Haut gauche", "Gauche (haut)", "Bas gauche", "Bas droite", "Droite (haut)", "Haut droite", "Aucun", "Gauche (bas)", "Droite (bas)" };
                static const int ORD_R[7] = { 0, 1, 2, 3, 4, 5, 6 }, ORD_S[9] = { 0, 1, 7, 2, 3, 8, 4, 5, 6 };
                auto label = [&](int v) { return sqS ? tr(SQ_EN[v], SQ_FR[v]) : tr(SL_EN[v], SL_FR[v]); };
                auto shown = [&](int v) { return sqS ? v : (v == 7 ? 1 : (v == 8 ? 4 : v)); };   // the round folds 7 / 8 onto its sides
                auto taken = [&](int slot, int self) {           // a place held by another module (Off holds none)
                    if (slot == 6) return false;
                    const int m[4] = { c.mmPodDay, c.mmPodMoon, c.mmPodSky, c.mmPodWx };
                    const bool on[4] = { c.mmClkDay != 0, c.mmClkMoon != 0, c.mmSkyShow != 0, c.mmWxShow != 0 };
                    for (int q = 0; q < 4; ++q) if (q != self && on[q] && shown(m[q]) == slot) return true;
                    return false;
                };
                // one module's place : step through the shape's places (Off only for the day/night and the weather), skip the taken
                auto place = [&](int& field, int self, bool canOff, int def, const char* en, const char* fr) {
                    ROW_BAND(52.0f)
                    const int* ord = sqS ? ORD_S : ORD_R; const int n = (sqS ? 9 : 7) - (canOff ? 0 : 1);
                    int cur = (field < 0 || field > 8) ? def : shown(field); if (!canOff && cur == 6) cur = def;
                    int idx = 0; for (int k = 0; k < n; ++k) if (ord[k] == cur) idx = k;
                    if (int d = row_selector(dev, fo, mo, click, ctrl_uid_i(CTRL_ID, self), coX, ry + yo, ctrlW, tr(en, fr), label(cur))) {
                        int k2 = idx;
                        for (int t = 0; t < n; ++t) { k2 = (k2 + d + n) % n; if (!taken(ord[k2], self)) break; }
                        field = ord[k2]; save_ui_config();
                    }
                    ROW_NEXT(52.0f)
                };
                // EACH ELEMENT : its switch, then its place (the user 2026-11-25 : "an on / off per element rather than in the
                // place choices ; the position is missing")
                ROW_TOGGLE(CTRL_ID, tr("Vana'diel time", "Heure Vana'diel"), c.mmClkTime)
                ROW_TOGGLE(CTRL_ID, tr("Map square (F-10)", "Case de carte (F-10)"), c.mmGrid)
                if (c.mmClkTime || c.mmGrid) { ROW_BAND(52.0f)
                    static const char* HP_EN[2] = { "Hour top, square bottom", "Square top, hour bottom" };
                    static const char* HP_FR[2] = { "Heure en haut, case en bas", "Case en haut, heure en bas" };
                    const int hp = c.mmPodHP == 1 ? 1 : 0;
                    if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Hour / map square", "Heure / case de carte"), tr(HP_EN[hp], HP_FR[hp]))) { (void)d; c.mmPodHP = 1 - hp; save_ui_config(); }
                ROW_NEXT(52.0f) }
                ROW_TOGGLE(CTRL_ID, tr("Elemental day", "Jour \xC3\xA9l\xC3\xA9mentaire"), c.mmClkDay)
                if (c.mmClkDay) place(c.mmPodDay, 0, false, 3, "  Place", "  Emplacement");
                ROW_TOGGLE(CTRL_ID, tr("Moon phase", "Phase de lune"), c.mmClkMoon)
                if (c.mmClkMoon) place(c.mmPodMoon, 1, false, 2, "  Place", "  Emplacement");
                ROW_TOGGLE(CTRL_ID, tr("Day / night", "Jour / nuit"), c.mmSkyShow)
                if (c.mmSkyShow) place(c.mmPodSky, 2, false, 5, "  Place", "  Emplacement");
                ROW_TOGGLE(CTRL_ID, tr("Weather", "M\xC3\xA9t\xC3\xA9o"), c.mmWxShow)
                if (c.mmWxShow) place(c.mmPodWx, 3, false, 4, "  Place", "  Emplacement");
                { ROW_BAND(52.0f)   // the gem set in the closed dial (a zone without a map)
                    static const char* GM_EN[6] = { "Ruby", "Sapphire", "Emerald", "Topaz", "Amethyst", "Diamond" };
                    static const char* GM_FR[6] = { "Rubis", "Saphir", "\xC3\x89meraude", "Topaze", "Am\xC3\xA9thyste", "Diamant" };
                    const int gm = (c.mmGem < 0 || c.mmGem > 5) ? 0 : c.mmGem;
                    if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Gem (zone without a map)", "Pierre (zone sans carte)"), tr(GM_EN[gm], GM_FR[gm]))) { c.mmGem = wrap(gm + d, 6); save_ui_config(); }
                } ROW_NEXT(52.0f)
            }
        }
        if (c.mmLayout == 1) {
            { ROW_BAND(52.0f)
                static const char* IB_EN[3] = { "Off", "Top", "Bottom" }; static const char* IB_FR[3] = { "Aucune", "Haut", "Bas" };
                const int ib = (c.mmInfoBand < 0 || c.mmInfoBand > 2) ? 2 : c.mmInfoBand;
                if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Info band (square, zoom, targets)", "Bande infos (case, zoom, cibles)"), tr(IB_EN[ib], IB_FR[ib]))) { c.mmInfoBand = wrap(ib + d, 3); save_ui_config(); }
            } ROW_NEXT(52.0f)
            if (c.mmInfoBand > 0) {
                ROW_TOGGLE(CTRL_ID, tr("Distances (target, Wide Scan)", "Distances (cible, Wide Scan)"), c.mmInfoDist)
            }
        }
        // THE DIAL (the user 2026-11-25 : "many options are not appropriate for this model") : it needs the clock and the
        // bezel (turning either off made the whole Dial vanish) -- forced on, their toggles hidden ; the real-time row and
        // the header's position do not exist on it
        const bool dialM = c.mmLayout == 2;
        if (dialM && (!c.mmClock || !c.mmBezel)) { c.mmClock = 1; c.mmBezel = 1; save_ui_config(); }
        if (!dialM) { ROW_TOGGLE(CTRL_ID, tr("Clock header", "En-t\xC3\xAAte horloge"), c.mmClock) }
        if (c.mmClock) {
        if (!(dialM && c.mmDayDial == 2)) {                                 // (the Dial's modules : their switches sit with their places above)
        ROW_TOGGLE(CTRL_ID, tr("Vana'diel time", "Heure Vana'diel"), c.mmClkTime)
        ROW_TOGGLE(CTRL_ID, tr("Elemental day", "Jour \xC3\xA9l\xC3\xA9mentaire"), c.mmClkDay)
        ROW_TOGGLE(CTRL_ID, tr("Moon phase", "Phase de lune"), c.mmClkMoon)
        }
        if (!dialM) { ROW_TOGGLE(CTRL_ID, tr("Real / GMT time", "Heure r\xC3\xA9""elle / GMT"), c.mmClkReal) }
        if (!dialM) { { ROW_BAND(52.0f)
            static const char* PLBL_EN[4] = { "Top", "Bottom", "Left", "Right" }; static const char* PLBL_FR[4] = { "Haut", "Bas", "Gauche", "Droite" };
            if (c.mmLayout == 1) {                          // Compact : the clock band goes above or below the map
                const int cb = (c.mmClkBand == 1) ? 1 : 0;
                if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Clock position", "Position horloge"), tr(PLBL_EN[cb], PLBL_FR[cb]))) { c.mmClkBand = wrap(cb + d, 2); save_ui_config(); }
            } else {
            int cp = (c.mmClockPos < 0 || c.mmClockPos > 3) ? 0 : c.mmClockPos;
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Clock position", "Position horloge"), tr(PLBL_EN[cp], PLBL_FR[cp]))) { c.mmClockPos = wrap(cp + d, 4); save_ui_config(); }
            }
        } ROW_NEXT(52.0f) }
        draw_box_appearance(dev, fo, mo, click, ry, ri, e, bandX, bandW, coX, ctrlW, c.mmBox);   // clock-box chrome : Box / Transparency / Theme / Hue / Luminosity
        }
            cat_fold_end(dev, ry, top9_, catH_[9], aF9_);
        }   // end Clock
        ry += snap(16.0f);                                 // air between this section and the next title bar
}   // end Advanced only

        // ===== sub-section : MARKERS =====
        // The section FOLDS : cat_fold owns the eased progress, the clip and the cursor (config_controls.h).
if (ui_config().cfgAdv) {   // Advanced only (the config's depth, 2026-11-26)
        const float aF8_ = cat_fold(CTRL_ID, catOpen_[8]);
        cat_panel(dev, hdrX, ry, hdrW, cat_card_h(catH_[8], aF8_));   // the section IS a card, collapsed or not
        if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Markers", "Marqueurs"), catOpen_[8], aF8_)) catOpen_[8] = !catOpen_[8];
        ROW_NEXT(42.0f)
        if (aF8_ > 0.0f) {
            const float top8_ = ry;
            cat_fold_clip(dev, hdrX, top8_, hdrW, catH_[8] * aF8_);
        { ROW_BAND(52.0f)   // Classic (dots + arrows) / Icons (a gold-framed silhouette per kind)
            static const char* MS_EN[2] = { "Classic", "Icons" }; static const char* MS_FR[2] = { "Classique", "Ic\xC3\xB4nes" };
            const int ms = (c.mmMarkStyle == 1) ? 1 : 0;
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Marker style", "Style des marqueurs"), tr(MS_EN[ms], MS_FR[ms]))) { c.mmMarkStyle = wrap(ms + d, 2); save_ui_config(); }
        } ROW_NEXT(52.0f)
        MM_PCT_SLIDER(CTRL_ID, tr("Marker Size", "Taille marqueurs"), c.mmMarkerScale, 0.50f, 2.00f)
        ROW_TOGGLE(CTRL_ID, tr("Players (PC)", "Joueurs (PC)"), c.mmPC)
        ROW_TOGGLE(CTRL_ID, tr("NPCs", "PNJ"),                  c.mmNPC)
        ROW_TOGGLE(CTRL_ID, tr("Monsters", "Monstres"),         c.mmMob)
        // --- Target line : you -> your <t> target ---
        ROW_TOGGLE(CTRL_ID, tr("Target line", "Ligne cible"), c.mmTgtLine)
        if (c.mmTgtLine) {
            CFG_COLOR_PICKER(&c.mmTgtLineCol)
        }
        // --- Range ring : a pull / aggro distance gauge around you ---
        ROW_TOGGLE(CTRL_ID, tr("Range ring", "Anneau de port\xC3\xA9""e"), c.mmRing)
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
        // --- the native compass's pieces : the Wide Scan tracked target, the map grid square ---
        ROW_TOGGLE(CTRL_ID, tr("Wide Scan track", "Suivi Wide Scan"), c.mmScan)
        if (c.mmScan) {
            CFG_COLOR_PICKER(&c.mmScanCol)
        }
        if (!(c.mmLayout == 2 && c.mmDayDial == 2)) { ROW_TOGGLE(CTRL_ID, tr("Map square (F-10)", "Case de carte (F-10)"), c.mmGrid) }   // (the Dial : in its modules' group)
            cat_fold_end(dev, ry, top8_, catH_[8], aF8_);
        }   // end Markers

        ry += snap(16.0f);
}   // end Advanced only
    #undef MM_PCT_SLIDER


        // ===== sub-section : TEXT (clock typography) =====
if (ui_config().cfgAdv && c.mmClock) {   // Advanced only ; and only with the clock header (the text it styles)
        const float aF5b_ = cat_fold(CTRL_ID, catOpen_[5] && c.mmClock);   // the section FOLDS -- and with the feature off there is nothing to reveal
        cat_panel(dev, hdrX, ry, hdrW, cat_card_h(catH_[5], aF5b_));
        if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Text", "Texte"), catOpen_[5], aF5b_)) catOpen_[5] = !catOpen_[5];
        ROW_NEXT(42.0f)
        if (aF5b_ > 0.0f) {
            const float top5b_ = ry;
            cat_fold_clip(dev, hdrX, top5b_, hdrW, catH_[5] * aF5b_);
        { ROW_BAND(52.0f)   // element selector
            const char* TLBL[MM_TE_COUNT] = { tr("Time", "Heure"), tr("Day", "Jour"), tr("Moon", "Lune"), tr("Real / GMT", "R\xC3\xA9""el / GMT") };
            const int nTe = c.mmLayout == 2 ? MM_REAL : MM_TE_COUNT;   // the Dial has no Real / GMT line (MM_REAL is the last element)
            int te = (cfgMmTextElem_ < 0 || cfgMmTextElem_ >= nTe) ? 0 : cfgMmTextElem_;
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Element", "\xC3\x89l\xC3\xA9ment"), TLBL[te])) {
                cfgMmTextElem_ = wrap(te + d, nTe); }
        }
        ROW_NEXT(52.0f)
        draw_text_style(dev, fo, mo, click, ry, ri, e, bandX, bandW, coX, ctrlW,
                        c.mmText[(cfgMmTextElem_ < 0 || cfgMmTextElem_ >= MM_TE_COUNT) ? 0 : cfgMmTextElem_], true);
            cat_fold_end(dev, ry, top5b_, catH_[5], aF5b_);
        }   // end Text
        ry += snap(16.0f);                                 // air between this section and the next title bar
}   // end Advanced only
    if (!ui_config().cfgAdv) SIMPLE_MORE   // Simple : where the rest is
    #undef ROW_BAND
    #undef ROW_NEXT
}


// ======== THE NEW MINIMAP PAGE (the config redesign, milestone 3, 2026-11-26) ========
// Simple : Display / Shape & Frame. Advanced (restructured 2026-11-27) : Display (+ Layout, Shape) / Frame / Box frame /
// Clock (Classic, Compact) or Dial (the Dial layout) / Info band (Compact) / Markers / Text. The LAYOUT and the SHAPE
// decide which rows exist (a Dial-only row appears only on the Dial) ; a plain dependency keeps its grey + reason ; a
// colour picker appears with its switch. "Shown + where" is one choice (the clock and its side, each Dial module and its
// place). Old mappings kept : Day dial / Info band read 1 / 2 when out of range, a module's place skips the taken places,
// the Dial forces the clock and the bezel on.
#define F_(name) [](UiConfig& c) -> void* { return &c.name; }
static const char* const M_SH_EN[2] = { "Square", "Round" },                  * const M_SH_FR[2] = { "Carr\xC3\xA9", "Rond" };
static const char* const M_FR_EN[3] = { "None", "Box Theme", "Custom" },      * const M_FR_FR[3] = { "Aucun", "Th\xC3\xA8me box", "Perso" };
static const char* const M_LL_EN[3] = { "Classic", "Compact", "Dial (round)" }, * const M_LL_FR[3] = { "Classique", "Compact", "Cadran (rond)" };
static const char* const M_DD_EN[3] = { "Globe (colours)", "Carousel (icons)", "Disc (icons)" };
static const char* const M_DD_FR[3] = { "Globe (couleurs)", "Carrousel (ic\xC3\xB4nes)", "Disque (ic\xC3\xB4nes)" };
static const char* const M_HP_EN[2] = { "Hour top, square bottom", "Square top, hour bottom" };
static const char* const M_HP_FR[2] = { "Heure en haut, case en bas", "Case en haut, heure en bas" };
static const char* const M_GM_EN[6] = { "Ruby", "Sapphire", "Emerald", "Topaz", "Amethyst", "Diamond" };
static const char* const M_GM_FR[6] = { "Rubis", "Saphir", "\xC3\x89meraude", "Topaze", "Am\xC3\xA9thyste", "Diamant" };
static const char* const M_IB_EN[3] = { "Off", "Top", "Bottom" },             * const M_IB_FR[3] = { "Aucune", "Haut", "Bas" };
static const char* const M_PL_EN[4] = { "Top", "Bottom", "Left", "Right" },   * const M_PL_FR[4] = { "Haut", "Bas", "Gauche", "Droite" };
static const char* const M_MS_EN[2] = { "Classic", "Icons" },                 * const M_MS_FR[2] = { "Classique", "Ic\xC3\xB4nes" };
// the Dial modules' PLACES : the round -- 4 corners and 2 sides ; the square -- 4 corners and 2 places on each side
static const char* const M_SL_EN[9] = { "Top left", "Left", "Bottom left", "Bottom right", "Right", "Top right", "Off", "Left (low)", "Right (low)" };
static const char* const M_SL_FR[9] = { "Haut gauche", "Gauche", "Bas gauche", "Bas droite", "Droite", "Haut droite", "Aucun", "Gauche (bas)", "Droite (bas)" };
static const char* const M_SQ_EN[9] = { "Top left", "Left (high)", "Bottom left", "Bottom right", "Right (high)", "Top right", "Off", "Left (low)", "Right (low)" };
static const char* const M_SQ_FR[9] = { "Haut gauche", "Gauche (haut)", "Bas gauche", "Bas droite", "Droite (haut)", "Haut droite", "Aucun", "Gauche (bas)", "Droite (bas)" };
static const int M_ORD_R[7] = { 0, 1, 2, 3, 4, 5, 6 }, M_ORD_S[9] = { 0, 1, 7, 2, 3, 8, 4, 5, 6 };

static const char* mw_square(const UiConfig&)  { return tr("inactive : square shape", "inactif : forme carr\xC3\xA9""e"); }
static const char* mw_clock(const UiConfig& c) { return c.mmClock ? nullptr : tr("inactive : clock header off", "inactif : en-t\xC3\xAAte horloge coup\xC3\xA9"); }
static const char* mw_bg(const UiConfig& c)    { return (c.mmShape == 0 && c.mmFrame == 1 && c.mmLayout != 2) ? tr("inactive : the box theme paints the square", "inactif : le th\xC3\xA8me de cadre peint le carr\xC3\xA9") : nullptr; }
static const char* mw_bezel(const UiConfig& c) {   // the round's bezel switch : drawn with the clock header ; the Dial IS its bezel
    if (c.mmLayout == 2) return tr("inactive : the Dial is its own bezel", "inactif : le Cadran est son propre anneau");
    if (c.mmShape != 1) return mw_square(c);
    return mw_clock(c);
}
static const char* mw_card(const UiConfig& c) {    // the round's bezel width / cardinals
    if (c.mmShape != 1) return mw_square(c);
    if (c.mmLayout == 2) return nullptr;
    if (!c.mmClock) return mw_clock(c);
    return c.mmBezel ? nullptr : tr("inactive : bezel ring off", "inactif : anneau coup\xC3\xA9");
}
static const char* mw_sqborder(const UiConfig& c) {   // the square's border : read only for a CUSTOM frame, never on the Dial
    if (c.mmShape == 1) return tr("inactive : round shape", "inactif : forme ronde");
    if (c.mmLayout == 2) return tr("inactive : the Dial has its case", "inactif : le Cadran a son bo\xC3\xAEtier");
    return c.mmFrame == 2 ? nullptr : tr("inactive : frame not Custom", "inactif : cadre pas Perso");
}

static const OptDef MM_DISPLAY[] = {
    { "Show", "Afficher", OK_TOGGLE, OL_SIMPLE, F_(mmShow), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Size", "Taille", OK_PCT, OL_SIMPLE, F_(mmScale), 0.5f, 2.0f, nullptr, nullptr, 0, nullptr, 0 },
    { "Map size", "Taille carte", OK_PCT, OL_SIMPLE, F_(mmMapSize), 0.5f, 1.6f, nullptr, nullptr, 0, nullptr, 0 },
};
static const OptDef MM_SHAPE[] = {
    { "Shape", "Forme", OK_SELECT, OL_SIMPLE, F_(mmShape), 0, 0, M_SH_EN, M_SH_FR, 2, nullptr, 0 },
    { "Frame", "Cadre", OK_SELECT, OL_SIMPLE, F_(mmFrame), 0, 0, M_FR_EN, M_FR_FR, 3, nullptr, 0 },
};
static const OptDef MM_BEZEL[] = {
    { "Bezel ring", "Anneau bordure", OK_TOGGLE, OL_SIMPLE, F_(mmBezel), 0, 0, nullptr, nullptr, 0, mw_bezel, 0 },
};
static const OptDef MM_BEZEL2[] = {
    { "Cardinal size", "Taille cardinaux", OK_PCT, OL_SIMPLE, F_(mmCardSz), 0.5f, 2.0f, nullptr, nullptr, 0, mw_card, 0 },
    { "Border width", "Largeur bordure", OK_PCT, OL_SIMPLE, F_(mmSqBorder), 0.5f, 2.0f, nullptr, nullptr, 0, mw_sqborder, 0 },
};
// Advanced : the MODEL (layout + shape decide which rows exist below) and the frame's style
static const OptDef MM_MODEL[] = {
    { "Layout", "Disposition", OK_SELECT, OL_ADV, F_(mmLayout), 0, 0, M_LL_EN, M_LL_FR, 3, nullptr, 0 },
    { "Shape", "Forme", OK_SELECT, OL_ADV, F_(mmShape), 0, 0, M_SH_EN, M_SH_FR, 2, nullptr, 0 },
};
static const OptDef MM_FRSTYLE[] = {
    { "Frame style", "Style du cadre", OK_SELECT, OL_ADV, F_(mmFrame), 0, 0, M_FR_EN, M_FR_FR, 3, nullptr, 0 },
};
static const OptDef MM_MARK_A[] = {
    { "Marker style", "Style des marqueurs", OK_SELECT, OL_ADV, F_(mmMarkStyle), 0, 0, M_MS_EN, M_MS_FR, 2, nullptr, 0 },
    { "Marker Size", "Taille marqueurs", OK_PCT, OL_ADV, F_(mmMarkerScale), 0.5f, 2.0f, nullptr, nullptr, 0, nullptr, 0 },
    { "Players (PC)", "Joueurs (PC)", OK_TOGGLE, OL_ADV, F_(mmPC), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "NPCs", "PNJ", OK_TOGGLE, OL_ADV, F_(mmNPC), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Monsters", "Monstres", OK_TOGGLE, OL_ADV, F_(mmMob), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Target line", "Ligne cible", OK_TOGGLE, OL_ADV, F_(mmTgtLine), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
};
static const OptDef MM_MARK_RING[] = {
    { "Range ring", "Anneau de port\xC3\xA9""e", OK_TOGGLE, OL_ADV, F_(mmRing), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
};
static const OptDef MM_MARK_SCAN[] = {
    { "Wide Scan track", "Suivi Wide Scan", OK_TOGGLE, OL_ADV, F_(mmScan), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
};
static const OptDef MM_MARK_GRID[] = {   // (on the Dial with its modules : in the Clock section instead)
    { "Map square (F-10)", "Case de carte (F-10)", OK_TOGGLE, OL_ADV, F_(mmGrid), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
};
#undef F_
#define NOPT(a) ((int)(sizeof(a) / sizeof((a)[0])))

void ConfigPage::draw_minimap_v2(u32 dev, Font* fo, const MouseState* mo, bool click,
                                 float& ry, int& ri, float e,
                                 float bandX, float bandW, float coX, float ctrlW,
                                 float hdrX, float hdrW) {
    Kit K;
    if (!v2_kit(K, mo, click)) { draw_minimap_config(dev, fo, mo, click, ry, ri, e, bandX, bandW, coX, ctrlW, hdrX, hdrW); return; }
    g_fade = e;
    UiConfig& c = ui_config();
    const bool adv = c.cfgAdv != 0;
    const float x = coX, w = ctrlW;
    const int base = CTRL_ID;
    // the rows a table cannot say : a toggle / a selector with its own clamp (`def` when out of range) / a free slider
    auto tog = [&](int k, const char* en, const char* frl, int* f, const char* why) {
        if (cfg_toggle_row(K, ctrl_uid_i(base, k), x, ry, w, tr(en, frl), *f != 0, why, f, 4)) { *f = *f ? 0 : 1; save_ui_config(); }
    };
    auto sel = [&](int k, const char* en, const char* frl, int* f, const char* const* chEn, const char* const* chFr, int n, int def, const char* why) {
        const int v = (*f < 0 || *f >= n) ? def : *f;
        if (int d = cfg_select_row(K, ctrl_uid_i(base, k), x, ry, w, tr(en, frl), tr(chEn[v], chFr[v]), why, f, 4)) { *f = wrap(v + d, n); save_ui_config(); }
    };
    auto mod = [](const void* p) { return cfg_count_modified_bytes(p, 4); };
    static const UiConfig D{};   // the factory values : a choice's marker, a float's
    auto fmod_ = [](float v, float d) { return fabsf(v - d) > 0.0005f ? 1 : 0; };   // a float row's marker (the slider row's own test)
    const bool dialM = c.mmLayout == 2, disc = dialM && c.mmDayDial == 2;
    const bool dialSqC = dialM && c.mmShape == 0;   // the square watch case (minimap.cpp dialSq) : mmBezelW is its band
    // a 50 .. 200 % slider, snapped to 5 % (the bezel / case width, the cardinals, the square's border)
    auto pct = [&](int k, const char* lab, float* f, const char* why) {
        float v01 = clampf((*f - 0.5f) / 1.5f, 0.0f, 1.0f);
        char b[16]; _snprintf(b, sizeof(b), "%d %%", (int)(*f * 100.0f + 0.5f)); b[15] = 0;
        if (cfg_slider_row(K, ctrl_uid_i(base, k), x, ry, w, lab, &v01, b, why, f, 4, true)) {
            const float v = floorf((0.5f + v01 * 1.5f) / 0.05f + 0.5f) * 0.05f; *f = clampf(v, 0.5f, 2.0f); }
    };
    auto zoom = [&](int k) {   // Zoom 1x .. 24x : BOUNDED print of a value that comes from a FILE (see the old page : a 1e30 smashed the stack once)
        const float lo = 1.0f, hi = 24.0f;
        char b[16]; _snprintf(b, sizeof(b), "%.1fx", clampf(c.mmZoom, lo, hi)); b[sizeof(b) - 1] = 0;
        float v01 = clampf((c.mmZoom - lo) / (hi - lo), 0.0f, 1.0f);
        if (cfg_slider_row(K, ctrl_uid_i(base, k), x, ry, w, tr("Zoom", "Zoom"), &v01, b, nullptr, &c.mmZoom, 4, true)) c.mmZoom = clampf(lo + v01 * (hi - lo), lo, hi);
    };
    auto bg = [&](int k) {     // the backdrop's opacity (0 = the game shows through) : a themed square frame paints its own interior
        char b[16]; _snprintf(b, sizeof(b), "%d %%", (int)(c.mmBgAlpha * 100.0f + 0.5f)); b[15] = 0;
        float v01 = clampf(c.mmBgAlpha, 0.0f, 1.0f);
        if (cfg_slider_row(K, ctrl_uid_i(base, k), x, ry, w, tr("Map background", "Fond carte"), &v01, b, mw_bg(c), &c.mmBgAlpha, 4, true)) c.mmBgAlpha = v01;
    };
    ry += K.px(6.0f);

    if (!adv) {   // ======== SIMPLE : Display, Shape & Frame (as before) ========
        if (v2_section(K, ctrl_uid_i(base, 1), 6, "Display", "Affichage", x, ry, w, cfg_count_modified(MM_DISPLAY, NOPT(MM_DISPLAY), false) + fmod_(c.mmZoom, D.mmZoom) + fmod_(c.mmBgAlpha, D.mmBgAlpha))) {
            cfg_rows(K, ctrl_uid_i(base, 2), MM_DISPLAY, NOPT(MM_DISPLAY), x, ry, w, false);
            zoom(3);
            bg(4);
            v2_end(K, ry);
        }
        const int nSh = cfg_count_modified(MM_SHAPE, NOPT(MM_SHAPE), false) + cfg_count_modified(MM_BEZEL, NOPT(MM_BEZEL), false) + fmod_(c.mmBezelW, D.mmBezelW)
                      + cfg_count_modified(MM_BEZEL2, NOPT(MM_BEZEL2), false);
        if (v2_section(K, ctrl_uid_i(base, 5), 7, "Shape & Frame", "Forme & Cadre", x, ry, w, nSh)) {
            cfg_rows(K, ctrl_uid_i(base, 6), MM_SHAPE, NOPT(MM_SHAPE), x, ry, w, false);
            if (c.mmFrame == 2) cfg_color_row(K, ctrl_uid_i(base, 7), x, ry, w, &c.mmFrameColor);   // the custom frame colour
            cfg_rows(K, ctrl_uid_i(base, 8), MM_BEZEL, NOPT(MM_BEZEL), x, ry, w, false);
            // one field, two names : the round's bezel ring, the square Dial's case band
            pct(9, dialSqC ? tr("Case width", "Largeur du bo\xC3\xAEtier") : tr("Bezel width", "Largeur anneau"), &c.mmBezelW, dialSqC ? nullptr : mw_card(c));
            cfg_rows(K, ctrl_uid_i(base, 10), MM_BEZEL2, NOPT(MM_BEZEL2), x, ry, w, false);
            v2_end(K, ry);
        }
        v2_simple_note(K, x, ry);
        g_fade = e;
        return;
    }

    // ======== ADVANCED (2026-11-27 restructure) : Display / Frame / Box frame / Clock or Dial / Info band / Markers / Text.
    // The LAYOUT decides which sections exist : a row that only one layout or shape draws appears only there (no more
    // "inactive : not the Dial layout") ; a plain dependency (the clock off, the frame not Custom) keeps its grey + reason.
    // "Shown or not, and where" is ONE choice (the clock and its position, each Dial module and its place).
    if (dialM && (!c.mmClock || !c.mmBezel)) { c.mmClock = 1; c.mmBezel = 1; save_ui_config(); }   // THE DIAL needs both (either off made it vanish)
    const bool round = c.mmShape == 1;
    // a choice's marker / right-click reset : the switch when it differs (or reads Off), else the mode
    auto markOf = [](int& on, int& mode, int dOn) -> void* { return (on != dOn || !on) ? (void*)&on : (void*)&mode; };
    auto chMod = [&](int& on, int& mode, int dOn) { return mod(markOf(on, mode, dOn)); };
    auto choice = [&](int k, const char* en, const char* frl, int& on, int& mode, const char* const* men, const char* const* mfr, int n, int dOn) {
        const char* lab[8]; lab[0] = tr("Off", "D\xC3\xA9sactiv\xC3\xA9"); for (int q = 0; q < n && q < 7; ++q) lab[q + 1] = tr(men[q], mfr[q]);
        const int v = on ? 1 + ((mode < 0 || mode >= n) ? 0 : mode) : 0;
        if (int d = cfg_select_row(K, ctrl_uid_i(base, k), x, ry, w, tr(en, frl), lab[v], nullptr, markOf(on, mode, dOn), 4)) {
            const int nv = wrap(v + d, n + 1); on = nv > 0 ? 1 : 0; if (nv > 0) mode = nv - 1; save_ui_config(); }
    };
    const char* wClk = c.mmClock ? nullptr : tr("inactive : clock off", "inactif : horloge coup\xC3\xA9""e");

    // ===== DISPLAY : the map itself, and the model (layout + shape) the rest of the page follows =====
    if (v2_section(K, ctrl_uid_i(base, 50), 6, "Display", "Affichage", x, ry, w,
                   cfg_count_modified(MM_DISPLAY, NOPT(MM_DISPLAY), true) + fmod_(c.mmZoom, D.mmZoom) + cfg_count_modified(MM_MODEL, NOPT(MM_MODEL), true))) {
        cfg_rows(K, ctrl_uid_i(base, 51), MM_DISPLAY, NOPT(MM_DISPLAY), x, ry, w, true);
        zoom(52);
        cfg_rows(K, ctrl_uid_i(base, 53), MM_MODEL, NOPT(MM_MODEL), x, ry, w, true);
        v2_end(K, ry);
    }
    // ===== FRAME : the map's frame -- each width row only for the shape / layout that draws it =====
    {
        const bool bezelRow = round && !dialM;           // the round's ring switch (the Dial IS its bezel)
        const bool widthRow = round || dialSqC;          // the ring's width, or the square Dial's case band (one field)
        const bool cardRow  = round || dialM;            // the ring's medallions, or the Dial's engraved letters
        const bool bordRow  = !round && !dialM;          // the plain square's border
        const char* wRing = dialM ? nullptr : (wClk ? wClk : (c.mmBezel ? nullptr : tr("inactive : bezel ring off", "inactif : anneau coup\xC3\xA9")));
        const int nMod = cfg_count_modified(MM_FRSTYLE, NOPT(MM_FRSTYLE), true) + (bezelRow ? mod(&c.mmBezel) : 0) + (widthRow ? fmod_(c.mmBezelW, D.mmBezelW) : 0)
                       + (cardRow ? fmod_(c.mmCardSz, D.mmCardSz) : 0) + (bordRow ? fmod_(c.mmSqBorder, D.mmSqBorder) : 0) + fmod_(c.mmBgAlpha, D.mmBgAlpha) + mod(&c.mmGem);
        if (v2_section(K, ctrl_uid_i(base, 54), 7, "Frame", "Cadre", x, ry, w, nMod)) {
            cfg_rows(K, ctrl_uid_i(base, 55), MM_FRSTYLE, NOPT(MM_FRSTYLE), x, ry, w, true);
            if (c.mmFrame == 2) cfg_color_row(K, ctrl_uid_i(base, 56), x, ry, w, &c.mmFrameColor);   // the custom frame colour
            if (bezelRow) tog(57, "Bezel ring", "Anneau bordure", &c.mmBezel, wClk);   // (drawn with the clock only : minimap.cpp astralRing)
            if (widthRow) pct(58, dialSqC ? tr("Case width", "Largeur du bo\xC3\xAEtier") : tr("Bezel width", "Largeur anneau"), &c.mmBezelW, wRing);
            if (cardRow)  pct(59, tr("Cardinal size", "Taille cardinaux"), &c.mmCardSz, wRing);
            if (bordRow)  pct(60, tr("Border width", "Largeur bordure"), &c.mmSqBorder, c.mmFrame == 2 ? nullptr : tr("inactive : frame not Custom", "inactif : cadre pas Perso"));
            bg(61);
            // the plate that closes the lens in a zone without a map : every layout draws it (minimap.cpp noMap)
            sel(62, "Gem (zone without a map)", "Pierre (zone sans carte)", &c.mmGem, M_GM_EN, M_GM_FR, 6, 0, nullptr);
            v2_end(K, ry);
        }
    }
    // ===== BOX FRAME : the themed box round the clock and the map -- it exists only when there is a box (minimap.cpp hasClock) =====
    if (c.mmClock || (c.mmLayout == 1 && c.mmInfoBand > 0)) {
        BoxStyle& bs = c.mmBox;
        const FrameRefs fr = { &bs.on, &bs.border, &bs.alpha, &bs.alphaOwn, &bs.themeCopy, &bs.theme, &bs.hue, &bs.lum };
        if (v2_section(K, ctrl_uid_i(base, 63), 24, "Box frame", "Cadre de la bo\xC3\xAEte", x, ry, w, cfg_frame_modified(fr))) { cfg_frame_rows(K, ctrl_uid_i(base, 64), x, ry, w, fr); v2_end(K, ry); }
    }
    if (!dialM) {
        // ===== CLOCK (Classic / Compact) : one choice "Off / where", then what it shows =====
        const bool cmp = c.mmLayout == 1;   // Compact : a band above / below the map ; Classic : a header on any side
        int& pos = cmp ? c.mmClkBand : c.mmClockPos;
        const int nMod = chMod(c.mmClock, pos, D.mmClock) + mod(&c.mmClkTime) + mod(&c.mmClkDay) + mod(&c.mmClkMoon) + mod(&c.mmClkReal);
        if (v2_section(K, ctrl_uid_i(base, 65), 9, "Clock", "Horloge", x, ry, w, nMod)) {
            if (cmp) choice(66, "Clock band", "Bande horloge", c.mmClock, c.mmClkBand, M_PL_EN, M_PL_FR, 2, D.mmClock);
            else     choice(67, "Clock", "Horloge", c.mmClock, c.mmClockPos, M_PL_EN, M_PL_FR, 4, D.mmClock);
            tog(68, "Vana'diel time", "Heure Vana'diel", &c.mmClkTime, wClk);
            tog(69, "Elemental day", "Jour \xC3\xA9l\xC3\xA9mentaire", &c.mmClkDay, wClk);
            tog(70, "Moon phase", "Phase de lune", &c.mmClkMoon, wClk);
            tog(71, "Real / GMT time", "Heure r\xC3\xA9""elle / GMT", &c.mmClkReal, wClk);
            v2_end(K, ry);
        }
    } else {
        // ===== DIAL : its day subdial, then its pieces -- the corner subdials, or (Disc) the modules round the compass =====
        const bool sqS = c.mmShape == 0;
        auto shown = [&](int v) { return sqS ? v : (v == 7 ? 1 : (v == 8 ? 4 : v)); };   // the round folds 7 / 8 onto its sides
        auto taken = [&](int slot, int self) {           // a place held by another module (Off holds none)
            if (slot == 6) return false;
            const int m[4] = { c.mmPodDay, c.mmPodMoon, c.mmPodSky, c.mmPodWx };
            const bool on[4] = { c.mmClkDay != 0, c.mmClkMoon != 0, c.mmSkyShow != 0, c.mmWxShow != 0 };
            for (int q = 0; q < 4; ++q) if (q != self && on[q] && shown(m[q]) == slot) return true;
            return false;
        };
        // a module : ONE choice "Off / its place" -- steps through the shape's places, skipping the ones another module holds
        auto module = [&](int k, const char* en, const char* frl, int& on, int& field, int self, int def, int dOn) {
            const int* ord = sqS ? M_ORD_S : M_ORD_R; const int n = (sqS ? 9 : 7) - 1;   // the places (the trailing 6 = Off is the switch)
            int cur = (field < 0 || field > 8) ? def : shown(field); if (cur == 6) cur = def;
            int idx = 0; for (int q = 0; q < n; ++q) if (ord[q] == cur) idx = q;
            const int v = on ? idx + 1 : 0;
            const char* lab = !on ? tr("Off", "D\xC3\xA9sactiv\xC3\xA9") : (sqS ? tr(M_SQ_EN[cur], M_SQ_FR[cur]) : tr(M_SL_EN[cur], M_SL_FR[cur]));
            if (int d = cfg_select_row(K, ctrl_uid_i(base, k), x, ry, w, tr(en, frl), lab, nullptr, markOf(on, field, dOn), 4)) {
                int nv = v;
                for (int t = 0; t <= n; ++t) { nv = (nv + d + n + 1) % (n + 1); if (nv == 0 || !taken(ord[nv - 1], self)) break; }
                on = nv > 0 ? 1 : 0; if (nv > 0) field = ord[nv - 1]; save_ui_config();
            }
        };
        const int nMod = mod(&c.mmDayDial) + mod(&c.mmClkTime)
                       + (disc ? mod(&c.mmGrid) + mod(&c.mmPodHP) + chMod(c.mmClkDay, c.mmPodDay, D.mmClkDay) + chMod(c.mmClkMoon, c.mmPodMoon, D.mmClkMoon)
                                 + chMod(c.mmSkyShow, c.mmPodSky, D.mmSkyShow) + chMod(c.mmWxShow, c.mmPodWx, D.mmWxShow)
                               : mod(&c.mmClkDay) + mod(&c.mmClkMoon));
        if (v2_section(K, ctrl_uid_i(base, 72), 9, "Dial", "Cadran", x, ry, w, nMod)) {
            sel(73, "Day dial", "Cadran du jour", &c.mmDayDial, M_DD_EN, M_DD_FR, 3, 1, nullptr);
            if (!disc) {   // the corner subdials
                tog(74, "Vana'diel time", "Heure Vana'diel", &c.mmClkTime, nullptr);
                tog(75, "Elemental day", "Jour \xC3\xA9l\xC3\xA9mentaire", &c.mmClkDay, nullptr);
                tog(76, "Moon phase", "Phase de lune", &c.mmClkMoon, nullptr);
            } else {       // the modules
                tog(77, "Vana'diel time", "Heure Vana'diel", &c.mmClkTime, nullptr);
                tog(78, "Map square (F-10)", "Case de carte (F-10)", &c.mmGrid, nullptr);
                sel(79, "Hour / map square", "Heure / case de carte", &c.mmPodHP, M_HP_EN, M_HP_FR, 2, 0,
                    (c.mmClkTime || c.mmGrid) ? nullptr : tr("inactive : hour and map square off", "inactif : heure et case coup\xC3\xA9""es"));
                module(80, "Elemental day", "Jour \xC3\xA9l\xC3\xA9mentaire", c.mmClkDay, c.mmPodDay, 0, 3, D.mmClkDay);
                module(81, "Moon phase", "Phase de lune", c.mmClkMoon, c.mmPodMoon, 1, 2, D.mmClkMoon);
                module(82, "Day / night", "Jour / nuit", c.mmSkyShow, c.mmPodSky, 2, 5, D.mmSkyShow);
                module(83, "Weather", "M\xC3\xA9t\xC3\xA9o", c.mmWxShow, c.mmPodWx, 3, 4, D.mmWxShow);
            }
            v2_end(K, ry);
        }
    }
    // ===== INFO BAND : the Compact layout's second band (map square | zoom | distances) =====
    if (c.mmLayout == 1 && v2_section(K, ctrl_uid_i(base, 84), 11, "Info band", "Bande d'infos", x, ry, w, mod(&c.mmInfoBand) + mod(&c.mmInfoDist))) {
        sel(85, "Position", "Position", &c.mmInfoBand, M_IB_EN, M_IB_FR, 3, 2, nullptr);
        tog(86, "Distances (target, Wide Scan)", "Distances (cible, Wide Scan)", &c.mmInfoDist, c.mmInfoBand > 0 ? nullptr : tr("inactive : info band off", "inactif : bande infos coup\xC3\xA9""e"));
        v2_end(K, ry);
    }
    // ===== MARKERS : who is on the map, the lines and the ring you draw over it =====
    {
        const int nMk = cfg_count_modified(MM_MARK_A, NOPT(MM_MARK_A), true) + cfg_count_modified(MM_MARK_RING, NOPT(MM_MARK_RING), true) + fmod_(c.mmRingR, D.mmRingR)
                      + cfg_count_modified(MM_MARK_SCAN, NOPT(MM_MARK_SCAN), true) + (dialM ? mod(&c.mmInfoDist) : 0)
                      + (disc ? 0 : cfg_count_modified(MM_MARK_GRID, NOPT(MM_MARK_GRID), true));
        if (v2_section(K, ctrl_uid_i(base, 87), 8, "Markers", "Marqueurs", x, ry, w, nMk)) {
            cfg_rows(K, ctrl_uid_i(base, 88), MM_MARK_A, NOPT(MM_MARK_A), x, ry, w, true);
            if (c.mmTgtLine) cfg_color_row(K, ctrl_uid_i(base, 89), x, ry, w, &c.mmTgtLineCol);
            cfg_rows(K, ctrl_uid_i(base, 90), MM_MARK_RING, NOPT(MM_MARK_RING), x, ry, w, true);
            {   // radius 3 .. 50 yalms
                const float lo = 3.0f, hi = 50.0f; char b[16]; _snprintf(b, sizeof(b), "%d", (int)(c.mmRingR + 0.5f)); b[15] = 0;
                float v01 = clampf((c.mmRingR - lo) / (hi - lo), 0.0f, 1.0f);
                if (cfg_slider_row(K, ctrl_uid_i(base, 91), x, ry, w, tr("Radius", "Rayon"), &v01, b,
                                   c.mmRing ? nullptr : tr("inactive : range ring off", "inactif : anneau de port\xC3\xA9""e coup\xC3\xA9"), &c.mmRingR, 4, true))
                    c.mmRingR = clampf(lo + v01 * (hi - lo), lo, hi);
            }
            if (c.mmRing) cfg_color_row(K, ctrl_uid_i(base, 92), x, ry, w, &c.mmRingCol);
            cfg_rows(K, ctrl_uid_i(base, 93), MM_MARK_SCAN, NOPT(MM_MARK_SCAN), x, ry, w, true);
            if (c.mmScan) cfg_color_row(K, ctrl_uid_i(base, 94), x, ry, w, &c.mmScanCol);
            // the Dial writes the distance ON the target / the Wide Scan mark (minimap.cpp : dial && mmInfoDist) ; Compact : in its info band
            if (dialM) tog(95, "Distances on the marks (target, Wide Scan)", "Distances sur les marques (cible, Wide Scan)", &c.mmInfoDist, nullptr);
            if (!disc) cfg_rows(K, ctrl_uid_i(base, 96), MM_MARK_GRID, NOPT(MM_MARK_GRID), x, ry, w, true);   // (Disc : a module, in the Dial section)
            v2_end(K, ry);
        }
    }
    // ===== TEXT (the clock's typography : exists with the clock) =====
    if (c.mmClock) {
        const char* lab[MM_TE_COUNT] = { tr("Time", "Heure"), tr("Day", "Jour"), tr("Moon", "Lune"), tr("Real / GMT", "R\xC3\xA9""el / GMT") };
        TextStyle* ts[MM_TE_COUNT]; for (int i = 0; i < MM_TE_COUNT; ++i) ts[i] = &c.mmText[i];
        const int nTe = dialM ? MM_REAL : MM_TE_COUNT;   // the Dial has no Real / GMT line (MM_REAL is the last element)
        if (v2_section(K, ctrl_uid_i(base, 97), 5, "Text", "Texte", x, ry, w, cfg_text_modified(ts, nTe))) { cfg_text_rows(K, ctrl_uid_i(base, 98), x, ry, w, lab, ts, nTe, &cfgMmTextElem_); v2_end(K, ry); }
    }
    g_fade = e;
}
#undef NOPT

} // namespace aio
