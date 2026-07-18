// zt_config.cpp -- the Zone Tracker module's settings panel (//aio config -> "Zone Tracker").
//
// Each control is keyed by CTRL_ID (file:line hash, see config_controls.h) -- no hand-numbered uids.
// Reuses catOpen_ [6] (Display)/[5] (Text).
#include "ui/config_page.h"
#include "ui/config_controls.h"
#include "ui/config_rows.h"
#include "model/ui_config.h"
#include "gfx/font.h"
#include "gfx/draw.h"
#include <cstdio>

namespace aio {

void ConfigPage::draw_zt_config(u32 dev, Font* fo, const MouseState* mo, bool click,
                                float& ry, int& ri, float e,
                                float bandX, float bandW, float coX, float ctrlW,
                                float hdrX, float hdrW) {
    UiConfig& c = ui_config();
    // The panel follows the "Content" selector : sub-options and text elements are those of THAT zone.
    const int zvSel = (c.ztVariant < 0 || c.ztVariant > 5) ? 1 : c.ztVariant;

    // ===== sub-section : DISPLAY =====
    if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Display", "Affichage"), catOpen_[6])) catOpen_[6] = !catOpen_[6];
    ROW_NEXT(42.0f)
    if (catOpen_[6]) {
        { ROW_BAND(48.0f)   // Show
            const float rowH = snap(38.0f), ty = ry + yo; fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, tr("Show", "Afficher"), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
            const float bbw = snap(112.0f), bbh = snap(34.0f), bx2 = coX + ctrlW - bbw, bty = ty + (rowH - bbh) * 0.5f;
            if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx2, bty, bbw, bbh, c.ztShow ? tr("On", "Oui") : tr("Off", "Non"), c.ztShow != 0)) { c.ztShow = !c.ztShow; save_ui_config(); }
        } ROW_NEXT(48.0f)
        { ROW_BAND(52.0f)   // Content -- placed right under Show : it drives BOTH the preview and which options
                            // appear below, so it has to be read before anything else on this panel. This is the
                            // one panel where Size is not the row after Show (the canonical order elsewhere).
            const char* ZLBL[6] = { tr("Dynamis", "Dynamis"), tr("Abyssea", "Abyssea"), tr("Omen", "Omen"), tr("Nyzul", "Nyzul"), tr("Sheol", "Sheol"), tr("Limbus", "Limbus") };
            int zv = zvSel;
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Content", "Contenu"), ZLBL[zv])) { c.ztVariant = wrap(zv + d, 6); save_ui_config(); }
        } ROW_NEXT(52.0f)
        { ROW_BAND(46.0f)   // Size
            const float lo = 0.50f, hi = 2.00f; char b[16]; sprintf(b, "%d%%", (int)(c.ztScale * 100.0f + 0.5f));
            float v01 = (c.ztScale - lo) / (hi - lo); v01 = clampf(v01, 0.0f, 1.0f);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Size", "Taille"), b, &v01)) { float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f; c.ztScale = v < lo ? lo : (v > hi ? hi : v); }
        } ROW_NEXT(46.0f)
        draw_box_appearance(dev, fo, mo, click, ry, ri, e, bandX, bandW, coX, ctrlW, c.ztBox);   // Box / Transparency / Theme / Hue / Luminosity
        { ROW_BAND(48.0f)   // Show title row (Dynamis / Abyssea / Omen / Nyzul / Sheol)
            const float rowH = snap(38.0f), ty = ry + yo; fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, tr("Show title", "Afficher le titre"), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
            const float bbw = snap(112.0f), bbh = snap(34.0f), bx2 = coX + ctrlW - bbw, bty = ty + (rowH - bbh) * 0.5f;
            if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx2, bty, bbw, bbh, c.ztHeader ? tr("On", "Oui") : tr("Off", "Non"), c.ztHeader != 0)) { c.ztHeader = !c.ztHeader; save_ui_config(); }
        } ROW_NEXT(48.0f)
        // ---- Sheol / Odyssey sub-options (only meaningful in Sheol ; harmless elsewhere) ----
        #define ZT_SHEOL_TOGGLE(UID, LABEL, FIELD)                                                                       \
            { ROW_BAND(48.0f) const float rowH = snap(38.0f), ty = ry + yo; fo->begin(dev);                             \
              fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, LABEL, snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f); \
              const float bbw = snap(112.0f), bbh = snap(34.0f), bx2 = coX + ctrlW - bbw, bty = ty + (rowH - bbh) * 0.5f;\
              if (toggle_chip(dev, fo, mo, click, UID, bx2, bty, bbw, bbh, (FIELD) ? tr("On", "Oui") : tr("Off", "Non"), (FIELD) != 0)) { FIELD = !(FIELD); save_ui_config(); } } ROW_NEXT(48.0f)
        // Size factors (bar / dot / icon) as a percentage slider. Same shape as the Limbus gauge sliders below.
        // UID is passed IN (like ZT_SHEOL_TOGGLE) : CTRL_ID inside a multi-line macro body would give every
        // expansion the same file:line hash, and one shared slot animates all the sliders together.
        #define ZT_SIZE_SLIDER(UID, LABEL, FIELD, LO, HI)                                                                \
            { ROW_BAND(46.0f) const float lo = (LO), hi = (HI); char b[16]; sprintf(b, "%d%%", (int)((FIELD) * 100.0f + 0.5f)); \
              float v01 = ((FIELD) - lo) / (hi - lo); v01 = clampf(v01, 0.0f, 1.0f);                                      \
              if (row_slider(dev, fo, mo, UID, coX, ry + yo, ctrlW, LABEL, b, &v01)) {                                    \
                  float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f; FIELD = clampf(v, lo, hi); save_ui_config(); } } ROW_NEXT(46.0f)
        // Per-variant rows : only the options of the zone selected just above are shown, so the panel matches
        // what the preview is actually drawing instead of listing every zone's switches at once.
        if (zvSel == 0) {   // Dynamis
            ZT_SHEOL_TOGGLE(CTRL_ID, tr("Run timer", "Timer de run"),        c.ztDyTimer)
            ZT_SIZE_SLIDER(CTRL_ID, tr("Timer bar width", "Largeur barre timer"),     c.ztDyBarW, 0.40f, 1.00f)
            ZT_SIZE_SLIDER(CTRL_ID, tr("Timer bar height", "Hauteur barre timer"),    c.ztDyBarH, 0.50f, 2.50f)
            ZT_SHEOL_TOGGLE(CTRL_ID, tr("Key items", "Objets cl\xC3\xA9s"),  c.ztDyKi)
            ZT_SIZE_SLIDER(CTRL_ID, tr("Key-item dot size", "Taille pastilles"),      c.ztDyDot, 0.50f, 2.50f)
        }
        if (zvSel == 1) {   // Abyssea
            ZT_SHEOL_TOGGLE(CTRL_ID, tr("Visitant timer", "Timer visitant"),  c.ztAbTimer)
            ZT_SIZE_SLIDER(CTRL_ID, tr("Timer bar width", "Largeur barre timer"),      c.ztAbBarW, 0.40f, 1.00f)
            ZT_SIZE_SLIDER(CTRL_ID, tr("Timer bar height", "Hauteur barre timer"),     c.ztAbBarH, 0.50f, 2.50f)
            ZT_SHEOL_TOGGLE(CTRL_ID, tr("Lights", "Lumi\xC3\xA8res"),         c.ztAbLights)
            ZT_SIZE_SLIDER(CTRL_ID, tr("Light bar width", "Largeur barre lumi\xC3\xA8re"),  c.ztAbLightW, 0.50f, 3.00f)
            ZT_SIZE_SLIDER(CTRL_ID, tr("Light bar height", "Hauteur barre lumi\xC3\xA8re"), c.ztAbLightH, 0.50f, 2.50f)
        }
        if (zvSel == 2) {   // Omen
            ZT_SHEOL_TOGGLE(CTRL_ID, tr("Floor objective", "Objectif d'\xC3\xA9tage"), c.ztOmObj)
            ZT_SHEOL_TOGGLE(CTRL_ID, tr("Omens + bonus", "Omens + bonus"),             c.ztOmCount)
            ZT_SHEOL_TOGGLE(CTRL_ID, tr("Objective rows", "Lignes d'objectifs"),       c.ztOmRows)
        }
        if (zvSel == 3) {   // Nyzul Isle
            ZT_SHEOL_TOGGLE(CTRL_ID, tr("Floor", "\xC3\x89tage"),               c.ztNyFloor)
            ZT_SHEOL_TOGGLE(CTRL_ID, tr("Floor timer", "Timer d'\xC3\xA9tage"), c.ztNyTime)
            ZT_SHEOL_TOGGLE(CTRL_ID, tr("Objective", "Objectif"),               c.ztNyObj)
            ZT_SHEOL_TOGGLE(CTRL_ID, tr("Restriction", "Restriction"),          c.ztNyRestr)
            ZT_SHEOL_TOGGLE(CTRL_ID, tr("Floors cleared", "\xC3\x89tages faits"), c.ztNyComp)
            ZT_SHEOL_TOGGLE(CTRL_ID, tr("Reward rate", "Taux de r\xC3\xA9" "compense"), c.ztNyRate)
            ZT_SHEOL_TOGGLE(CTRL_ID, tr("Tokens", "Tokens"),                    c.ztNyTok)
        }
        if (zvSel == 4) {
            ZT_SHEOL_TOGGLE(CTRL_ID, tr("Segments", "Segments"),                  c.ztSheolSeg)
            ZT_SHEOL_TOGGLE(CTRL_ID, tr("Resistances", "R\xC3\xA9sistances"),     c.ztSheolRes)
            ZT_SHEOL_TOGGLE(CTRL_ID, tr("Family name", "Nom de famille"),         c.ztShFam)
            ZT_SIZE_SLIDER(CTRL_ID, tr("Weapon icon size", "Taille ic\xC3\xB4nes armes"),  c.ztShIcon, 0.50f, 2.50f)
            ZT_SIZE_SLIDER(CTRL_ID, tr("Element dot size", "Taille pastilles \xC3\xA9l\xC3\xA9ments"), c.ztShDot, 0.50f, 2.50f)
            ZT_SHEOL_TOGGLE(CTRL_ID, tr("Cruel Joke", "Cruel Joke"),              c.ztSheolJoke)
        }
        if (zvSel == 5) {
            ZT_SHEOL_TOGGLE(CTRL_ID, tr("Area & level", "Zone et niveau"),              c.ztLbName)
            { ROW_BAND(46.0f)   // gauge width, as a fraction of the box content width
                const float lo = 0.40f, hi = 1.00f; char b[16]; sprintf(b, "%d%%", (int)(c.ztLbBarW * 100.0f + 0.5f));
                float v01 = (c.ztLbBarW - lo) / (hi - lo); v01 = clampf(v01, 0.0f, 1.0f);
                if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Gauge width", "Largeur jauge"), b, &v01)) { float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f; c.ztLbBarW = clampf(v, lo, hi); save_ui_config(); }
            } ROW_NEXT(46.0f)
            { ROW_BAND(46.0f)   // gauge height multiplier
                const float lo = 0.50f, hi = 2.50f; char b[16]; sprintf(b, "%d%%", (int)(c.ztLbBarH * 100.0f + 0.5f));
                float v01 = (c.ztLbBarH - lo) / (hi - lo); v01 = clampf(v01, 0.0f, 1.0f);
                if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Gauge height", "Hauteur jauge"), b, &v01)) { float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f; c.ztLbBarH = clampf(v, lo, hi); save_ui_config(); }
            } ROW_NEXT(46.0f)
            ZT_SHEOL_TOGGLE(CTRL_ID, tr("Floor on gauge", "\xC3\x89tage sur la jauge"), c.ztLbFloor)
            ZT_SHEOL_TOGGLE(CTRL_ID, tr("Currencies", "Monnaies"),                      c.ztLbCur)
            ZT_SHEOL_TOGGLE(CTRL_ID, tr("Run total", "Total du run"),                    c.ztLbRun)
            ZT_SHEOL_TOGGLE(CTRL_ID, tr("Coffer dots", "Pastilles coffres"),             c.ztLbChips)
        }
        #undef ZT_SIZE_SLIDER
        #undef ZT_SHEOL_TOGGLE
        { ROW_BAND(40.0f)   // note
            const float ty = ry + yo; fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + snap(16.0f), tr("Only appears in a tracked zone (Dynamis, Abyssea, Omen, Nyzul, Sheol, Limbus).", "Appara\xC3\xAet seulement en zone suivie (Dynamis, Abyssea, Omen, Nyzul, Sheol, Limbus)."), snap(12.0f), fa(C_MUTE), fa(C_STROKE), 1.0f);
        } ROW_NEXT(40.0f)
    }   // end Display

    // ===== sub-section : TEXT =====
    if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Text", "Texte"), catOpen_[5])) catOpen_[5] = !catOpen_[5];
    ROW_NEXT(42.0f)
    if (catOpen_[5]) {
        // Element list is per-variant : Header + Body are shared by every zone, then that zone's own rows. The
        // selector index walks THIS list, so picking Limbus never exposes a slot another zone cannot draw.
        int elems[ZT_TE_COUNT]; const char* elbl[ZT_TE_COUNT]; int nEl = 0;
        elems[nEl] = ZT_HEADER; elbl[nEl++] = tr("Header", "Titre");
        if (zvSel == 0) {
            elems[nEl] = ZT_DY_TIMER; elbl[nEl++] = tr("Run timer", "Timer de run");
            elems[nEl] = ZT_DY_KI;    elbl[nEl++] = tr("Key items", "Objets cl\xC3\xA9s");
        }
        if (zvSel == 1) {
            elems[nEl] = ZT_AB_TIMER; elbl[nEl++] = tr("Visitant timer", "Timer visitant");
            elems[nEl] = ZT_AB_LIGHT; elbl[nEl++] = tr("Light labels", "Labels lumi\xC3\xA8res");
            elems[nEl] = ZT_AB_VAL;   elbl[nEl++] = tr("Light values", "Valeurs lumi\xC3\xA8res");
        }
        if (zvSel == 2) {
            elems[nEl] = ZT_OM_OBJ;   elbl[nEl++] = tr("Floor objective", "Objectif d'\xC3\xA9tage");
            elems[nEl] = ZT_OM_COUNT; elbl[nEl++] = tr("Omens + bonus", "Omens + bonus");
            elems[nEl] = ZT_OM_ROW;   elbl[nEl++] = tr("Objective rows", "Lignes d'objectifs");
        }
        if (zvSel == 3) {
            elems[nEl] = ZT_NY_FLOOR; elbl[nEl++] = tr("Floor", "\xC3\x89tage");
            elems[nEl] = ZT_NY_TIME;  elbl[nEl++] = tr("Floor timer", "Timer d'\xC3\xA9tage");
            elems[nEl] = ZT_NY_OBJ;   elbl[nEl++] = tr("Objective", "Objectif");
            elems[nEl] = ZT_NY_RESTR; elbl[nEl++] = tr("Restriction", "Restriction");
            elems[nEl] = ZT_NY_STATS; elbl[nEl++] = tr("Stat rows", "Lignes de stats");
        }
        if (zvSel == 4) {
            elems[nEl] = ZT_SH_SEG;  elbl[nEl++] = tr("Segments", "Segments");
            elems[nEl] = ZT_SH_FAM;  elbl[nEl++] = tr("Family name", "Nom de famille");
            elems[nEl] = ZT_SH_RES;  elbl[nEl++] = tr("Resistances", "R\xC3\xA9sistances");
            elems[nEl] = ZT_SH_JOKE; elbl[nEl++] = tr("Cruel Joke", "Cruel Joke");
        }
        if (zvSel == 5) {
            elems[nEl] = ZT_BODY;     elbl[nEl++] = tr("Area & level", "Zone et niveau");
            elems[nEl] = ZT_LB_GAUGE; elbl[nEl++] = tr("Gauge", "Jauge");
            elems[nEl] = ZT_LB_CUR;   elbl[nEl++] = tr("Currencies", "Monnaies");
            elems[nEl] = ZT_LB_RUN;   elbl[nEl++] = tr("Run total", "Total du run");
            elems[nEl] = ZT_LB_CHIP;  elbl[nEl++] = tr("Dot labels", "Labels pastilles");
        }
        { ROW_BAND(52.0f)   // element selector
            int te = (cfgZtTextElem_ < 0 || cfgZtTextElem_ >= nEl) ? 0 : cfgZtTextElem_;
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Element", "\xC3\x89l\xC3\xA9ment"), elbl[te])) { cfgZtTextElem_ = wrap(te + d, nEl); }
        }
        ROW_NEXT(52.0f)
        draw_text_style(dev, fo, mo, click, ry, ri, e, bandX, bandW, coX, ctrlW,
                        c.ztText[elems[(cfgZtTextElem_ < 0 || cfgZtTextElem_ >= nEl) ? 0 : cfgZtTextElem_]]);
    }   // end Text

    ry += snap(10.0f);
    #undef ROW_BAND
    #undef ROW_NEXT
}

} // namespace aio
