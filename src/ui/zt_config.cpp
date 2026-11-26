// zt_config.cpp -- the Zone Tracker module's settings panel (//aio config -> "Zone Tracker").
//
// Each control is keyed by CTRL_ID (file:line hash, see config_controls.h) -- no hand-numbered uids.
// Reuses catOpen_ [6] (Display)/[5] (Text).
#include "ui/config_page.h"
#include "ui/config_controls.h"
#include "ui/config_rows.h"
#include "ui/cfg_engine.h"        // the redesign : OptDef tables + the kit (draw_zt_v2)
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
    const int zvSel = (c.ztVariant < 0 || c.ztVariant > 6) ? 1 : c.ztVariant;

    // ===== sub-section : DISPLAY =====
    const float aF6_ = cat_fold(CTRL_ID, !ui_config().cfgAdv || (catOpen_[6]));   // the section FOLDS, like every other module
    cat_panel(dev, hdrX, ry, hdrW, cat_card_h(catH_[6], aF6_));   // ... and it IS a card, collapsed or not
    if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Display", "Affichage"), catOpen_[6], aF6_) && ui_config().cfgAdv) catOpen_[6] = !catOpen_[6];
    ROW_NEXT(42.0f)
    if (aF6_ > 0.0f) {
        const float top6_ = ry;
        cat_fold_clip(dev, hdrX, top6_, hdrW, catH_[6] * aF6_);
        { ROW_BAND(52.0f)   // Content -- placed right under Show : it drives BOTH the preview and which options
                            // appear below, so it has to be read before anything else on this panel. This is the
                            // one panel where Size is not the row after Show (the canonical order elsewhere).
            const char* ZLBL[7] = { tr("Dynamis", "Dynamis"), tr("Abyssea", "Abyssea"), tr("Omen", "Omen"), tr("Nyzul", "Nyzul"), tr("Sheol", "Sheol"), tr("Limbus", "Limbus"), tr("Sortie", "Sortie") };
            int zv = zvSel;
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Preview zone", "Zone de l'aper\xC3\xA7u"), ZLBL[zv])) { c.ztVariant = wrap(zv + d, 7); save_ui_config(); }
        } ROW_NEXT(52.0f)
        ROW_TOGGLE(CTRL_ID, tr("Show", "Afficher"), c.ztShow)   // Show
        { ROW_BAND(46.0f)   // Size
            const float lo = 0.50f, hi = 2.00f; char b[16]; sprintf(b, "%d%%", (int)(c.ztScale * 100.0f + 0.5f));
            float v01 = (c.ztScale - lo) / (hi - lo); v01 = clampf(v01, 0.0f, 1.0f);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Size", "Taille"), b, &v01)) { float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f; c.ztScale = v < lo ? lo : (v > hi ? hi : v); }
        } ROW_NEXT(46.0f)
        ROW_GROW(CTRL_ID, UiConfig::GB_ZT)   // which way the box grows as its content widens (ui/box_grow.h)
        cat_fold_end(dev, ry, top6_, catH_[6], aF6_);
    }   // end Display
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
        draw_box_appearance(dev, fo, mo, click, ry, ri, e, bandX, bandW, coX, ctrlW, c.ztBox);   // Box / Transparency / Theme / Hue / Luminosity
        cat_fold_end(dev, ry, top20_, catH_[20], aF20_);
    }   // end Frame
    ry += snap(16.0f);
}   // end Advanced only

    // ===== sub-section : CONTENT =====
    // WHAT the box lists, for the zone selected above -- and it is the bulk of this panel: six variants, each
    // with its own switches. Display was carrying all of it plus the box's own settings, twenty-one controls
    // answering two questions. Same split as Timers, same names.
if (ui_config().cfgAdv) {   // Advanced only (the config's depth, 2026-11-26)
    const float aFz_ = cat_fold(CTRL_ID, catOpen_[15]);
    cat_panel(dev, hdrX, ry, hdrW, cat_card_h(catH_[15], aFz_));
    if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Content", "Contenu"), catOpen_[15], aFz_)) catOpen_[15] = !catOpen_[15];
    ROW_NEXT(42.0f)
    if (aFz_ > 0.0f) {
        const float topZ_ = ry;
        cat_fold_clip(dev, hdrX, topZ_, hdrW, catH_[15] * aFz_);
        ROW_TOGGLE(CTRL_ID, tr("Show title", "Afficher le titre"), c.ztHeader)   // Show title row (Dynamis / Abyssea / Omen / Nyzul / Sheol)
        // ---- Sheol / Odyssey sub-options (only meaningful in Sheol ; harmless elsewhere) ----
        // Size factors (bar / dot / icon) as a percentage slider. Same shape as the Limbus gauge sliders below.
        // UID is passed IN (like ZT_SHEOL_TOGGLE) : CTRL_ID inside a multi-line macro body would give every
        // expansion the same file:line hash, and one shared slot animates all the sliders together.
        #define ZT_SIZE_SLIDER(UID, LABEL, FIELD, LO, HI) { ROW_BAND(46.0f) row_pct_slider(dev, fo, mo, UID, coX, ry + yo, ctrlW, LABEL, &(FIELD), (LO), (HI)); } ROW_NEXT(46.0f)
        // Per-variant rows : only the options of the zone selected just above are shown, so the panel matches
        // what the preview is actually drawing instead of listing every zone's switches at once.
        if (zvSel == 0) {   // Dynamis
            ROW_TOGGLE(CTRL_ID, tr("Run timer", "Timer de run"),        c.ztDyTimer)
            if (c.ztDyTimer) {   // its size : only with it on
            ZT_SIZE_SLIDER(CTRL_ID, tr("Timer bar width", "Largeur barre timer"),     c.ztDyBarW, 0.40f, 1.00f)
            ZT_SIZE_SLIDER(CTRL_ID, tr("Timer bar height", "Hauteur barre timer"),    c.ztDyBarH, 0.50f, 2.50f)
            }
            ROW_TOGGLE(CTRL_ID, tr("Key items", "Objets cl\xC3\xA9s"),  c.ztDyKi)
            if (c.ztDyKi) {   // its size : only with it on
            ZT_SIZE_SLIDER(CTRL_ID, tr("Key-item dot size", "Taille pastilles"),      c.ztDyDot, 0.50f, 2.50f)
            }
        }
        if (zvSel == 1) {   // Abyssea
            ROW_TOGGLE(CTRL_ID, tr("Visitant timer", "Timer visitant"),  c.ztAbTimer)
            if (c.ztAbTimer) {   // its size : only with it on
            ZT_SIZE_SLIDER(CTRL_ID, tr("Timer bar width", "Largeur barre timer"),      c.ztAbBarW, 0.40f, 1.00f)
            ZT_SIZE_SLIDER(CTRL_ID, tr("Timer bar height", "Hauteur barre timer"),     c.ztAbBarH, 0.50f, 2.50f)
            }
            ROW_TOGGLE(CTRL_ID, tr("Lights", "Lumi\xC3\xA8res"),         c.ztAbLights)
            if (c.ztAbLights) {   // its size : only with it on
            ZT_SIZE_SLIDER(CTRL_ID, tr("Light bar width", "Largeur barre lumi\xC3\xA8re"),  c.ztAbLightW, 0.50f, 3.00f)
            ZT_SIZE_SLIDER(CTRL_ID, tr("Light bar height", "Hauteur barre lumi\xC3\xA8re"), c.ztAbLightH, 0.50f, 2.50f)
            }
        }
        if (zvSel == 2) {   // Omen
            ROW_TOGGLE(CTRL_ID, tr("Floor objective", "Objectif d'\xC3\xA9tage"), c.ztOmObj)
            ROW_TOGGLE(CTRL_ID, tr("Omens + bonus", "Omens + bonus"),             c.ztOmCount)
            ROW_TOGGLE(CTRL_ID, tr("Objective rows", "Lignes d'objectifs"),       c.ztOmRows)
        }
        if (zvSel == 3) {   // Nyzul Isle
            ROW_TOGGLE(CTRL_ID, tr("Floor", "\xC3\x89tage"),               c.ztNyFloor)
            ROW_TOGGLE(CTRL_ID, tr("Floor timer", "Timer d'\xC3\xA9tage"), c.ztNyTime)
            ROW_TOGGLE(CTRL_ID, tr("Objective", "Objectif"),               c.ztNyObj)
            ROW_TOGGLE(CTRL_ID, tr("Restriction", "Restriction"),          c.ztNyRestr)
            ROW_TOGGLE(CTRL_ID, tr("Floors cleared", "\xC3\x89tages faits"), c.ztNyComp)
            ROW_TOGGLE(CTRL_ID, tr("Reward rate", "Taux de r\xC3\xA9" "compense"), c.ztNyRate)
            ROW_TOGGLE(CTRL_ID, tr("Tokens", "Tokens"),                    c.ztNyTok)
        }
        if (zvSel == 4) {
            ROW_TOGGLE(CTRL_ID, tr("Segments", "Segments"),                  c.ztSheolSeg)
            ROW_TOGGLE(CTRL_ID, tr("Resistances", "R\xC3\xA9sistances"),     c.ztSheolRes)
            ROW_TOGGLE(CTRL_ID, tr("Family name", "Nom de famille"),         c.ztShFam)
            ZT_SIZE_SLIDER(CTRL_ID, tr("Weapon icon size", "Taille ic\xC3\xB4nes armes"),  c.ztShIcon, 0.50f, 2.50f)
            ZT_SIZE_SLIDER(CTRL_ID, tr("Element dot size", "Taille pastilles \xC3\xA9l\xC3\xA9ments"), c.ztShDot, 0.50f, 2.50f)
            ROW_TOGGLE(CTRL_ID, tr("Cruel Joke", "Cruel Joke"),              c.ztSheolJoke)
        }
        if (zvSel == 5) {
            ROW_TOGGLE(CTRL_ID, tr("Area & level", "Zone et niveau"),              c.ztLbName)
            { ROW_BAND(46.0f)   // gauge width, as a fraction of the box content width
                const float lo = 0.40f, hi = 1.00f; char b[16]; sprintf(b, "%d%%", (int)(c.ztLbBarW * 100.0f + 0.5f));
                float v01 = (c.ztLbBarW - lo) / (hi - lo); v01 = clampf(v01, 0.0f, 1.0f);
                if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Gauge width", "Largeur jauge"), b, &v01)) { float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f; c.ztLbBarW = clampf(v, lo, hi); }
            } ROW_NEXT(46.0f)
            { ROW_BAND(46.0f)   // gauge height multiplier
                const float lo = 0.50f, hi = 2.50f; char b[16]; sprintf(b, "%d%%", (int)(c.ztLbBarH * 100.0f + 0.5f));
                float v01 = (c.ztLbBarH - lo) / (hi - lo); v01 = clampf(v01, 0.0f, 1.0f);
                if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Gauge height", "Hauteur jauge"), b, &v01)) { float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f; c.ztLbBarH = clampf(v, lo, hi); }
            } ROW_NEXT(46.0f)
            ROW_TOGGLE(CTRL_ID, tr("Floor on gauge", "\xC3\x89tage sur la jauge"), c.ztLbFloor)
            ROW_TOGGLE(CTRL_ID, tr("Currencies", "Monnaies"),                      c.ztLbCur)
            ROW_TOGGLE(CTRL_ID, tr("Run total", "Total du run"),                    c.ztLbRun)
            ROW_TOGGLE(CTRL_ID, tr("Coffer dots", "Pastilles coffres"),             c.ztLbChips)
        }
        if (zvSel == 6) {   // Sortie
            ROW_TOGGLE(CTRL_ID, tr("Gallimaufry", "Gallimaufry"),                   c.ztSoGal)
            ROW_TOGGLE(CTRL_ID, tr("Bosses and shards", "Boss et shards"),          c.ztSoBoss)
            ROW_TOGGLE(CTRL_ID, tr("Coffers and NM", "Coffres et NM"),              c.ztSoCof)
            ROW_TOGGLE(CTRL_ID, tr("Items obtained", "Objets obtenus"),             c.ztSoLoot)
            ROW_TOGGLE(CTRL_ID, tr("Wing NM / bitzer", "NM / bitzer de l'aile"),     c.ztSoNav)
            ROW_TOGGLE(CTRL_ID, tr("Auto Widescan track", "Track Widescan auto"),    c.ztSoTrack)   // sent by the aioupdate addon, one track per wing (see ui_config.h)
        }
        #undef ZT_SIZE_SLIDER
        { ROW_BAND(40.0f)   // note
            const float ty = ry + yo; fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + snap(16.0f), tr("Only appears in a tracked zone (Dynamis, Abyssea, Omen, Nyzul, Sheol, Limbus, Sortie).", "Appara\xC3\xAet seulement en zone suivie (Dynamis, Abyssea, Omen, Nyzul, Sheol, Limbus, Sortie)."), snap(12.0f), fa(C_MUTE), fa(C_STROKE), 1.0f);
        } ROW_NEXT(40.0f)
        cat_fold_end(dev, ry, topZ_, catH_[15], aFz_);
    }   // end Content
    ry += snap(16.0f);                                 // air between this section and the next title bar
}   // end Advanced only

    // ===== sub-section : TEXT =====
if (ui_config().cfgAdv) {   // Advanced only (the config's depth, 2026-11-26)
    const float aF5_ = cat_fold(CTRL_ID, catOpen_[5]);   // the section FOLDS, like every other module
    cat_panel(dev, hdrX, ry, hdrW, cat_card_h(catH_[5], aF5_));   // ... and it IS a card, collapsed or not
    if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Text", "Texte"), catOpen_[5], aF5_)) catOpen_[5] = !catOpen_[5];
    ROW_NEXT(42.0f)
    if (aF5_ > 0.0f) {
        const float top5_ = ry;
        cat_fold_clip(dev, hdrX, top5_, hdrW, catH_[5] * aF5_);
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
        if (zvSel == 6) {
            elems[nEl] = ZT_SO_GAL;  elbl[nEl++] = tr("Gallimaufry", "Gallimaufry");
            elems[nEl] = ZT_SO_BOSS; elbl[nEl++] = tr("Boss letters", "Lettres des boss");
            elems[nEl] = ZT_SO_LINE; elbl[nEl++] = tr("Coffers and items", "Coffres et objets");
        }
        { ROW_BAND(52.0f)   // element selector
            int te = (cfgZtTextElem_ < 0 || cfgZtTextElem_ >= nEl) ? 0 : cfgZtTextElem_;
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Element", "\xC3\x89l\xC3\xA9ment"), elbl[te])) { cfgZtTextElem_ = wrap(te + d, nEl); }
        }
        ROW_NEXT(52.0f)
        draw_text_style(dev, fo, mo, click, ry, ri, e, bandX, bandW, coX, ctrlW,
                        c.ztText[elems[(cfgZtTextElem_ < 0 || cfgZtTextElem_ >= nEl) ? 0 : cfgZtTextElem_]]);
        cat_fold_end(dev, ry, top5_, catH_[5], aF5_);
    }   // end Text

    ry += snap(16.0f);
}   // end Advanced only
    if (!ui_config().cfgAdv) SIMPLE_MORE   // Simple : where the rest is
    #undef ROW_BAND
    #undef ROW_NEXT
}


// ======== THE NEW ZONE TRACKER PAGE (the config redesign, milestone 3, 2026-11-26) ========
// Display / Frame / Content / Text, as the old page. The Preview zone picks WHICH zone's switches Content and Text list
// (a choice's own sub-form) ; inside a zone, a size whose element is off is GREYED with the reason.
#define F_(name) [](UiConfig& c) -> void* { return &c.name; }
static const char* const Z_ZN_EN[7] = { "Dynamis", "Abyssea", "Omen", "Nyzul", "Sheol", "Limbus", "Sortie" };
static const char* const Z_ZN_FR[7] = { "Dynamis", "Abyssea", "Omen", "Nyzul", "Sheol", "Limbus", "Sortie" };
static const char* zw_dyt(const UiConfig& c) { return c.ztDyTimer ? nullptr : tr("inactive : run timer off", "inactif : timer de run coup\xC3\xA9"); }
static const char* zw_dyk(const UiConfig& c) { return c.ztDyKi ? nullptr : tr("inactive : key items off", "inactif : objets cl\xC3\xA9s coup\xC3\xA9s"); }
static const char* zw_abt(const UiConfig& c) { return c.ztAbTimer ? nullptr : tr("inactive : visitant timer off", "inactif : timer visitant coup\xC3\xA9"); }
static const char* zw_abl(const UiConfig& c) { return c.ztAbLights ? nullptr : tr("inactive : lights off", "inactif : lumi\xC3\xA8res coup\xC3\xA9""es"); }

static const OptDef ZT_DISPLAY[] = {
    { "Preview zone", "Zone de l'aper\xC3\xA7u", OK_SELECT, OL_SIMPLE, F_(ztVariant), 0, 0, Z_ZN_EN, Z_ZN_FR, 7, nullptr, 0 },
    { "Show", "Afficher", OK_TOGGLE, OL_SIMPLE, F_(ztShow), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Size", "Taille", OK_PCT, OL_SIMPLE, F_(ztScale), 0.5f, 2.0f, nullptr, nullptr, 0, nullptr, 0 },
    { "Grows toward", "Grandit vers", OK_GROW, OL_SIMPLE, nullptr, 0, 0, nullptr, nullptr, 0, nullptr, UiConfig::GB_ZT },
};
static const OptDef ZT_HEAD[] = {
    { "Show title", "Afficher le titre", OK_TOGGLE, OL_ADV, F_(ztHeader), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
};
static const OptDef ZT_DY[] = {   // Dynamis
    { "Run timer", "Timer de run", OK_TOGGLE, OL_ADV, F_(ztDyTimer), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Timer bar width", "Largeur barre timer", OK_PCT, OL_ADV, F_(ztDyBarW), 0.40f, 1.00f, nullptr, nullptr, 0, zw_dyt, 0 },
    { "Timer bar height", "Hauteur barre timer", OK_PCT, OL_ADV, F_(ztDyBarH), 0.50f, 2.50f, nullptr, nullptr, 0, zw_dyt, 0 },
    { "Key items", "Objets cl\xC3\xA9s", OK_TOGGLE, OL_ADV, F_(ztDyKi), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Key-item dot size", "Taille pastilles", OK_PCT, OL_ADV, F_(ztDyDot), 0.50f, 2.50f, nullptr, nullptr, 0, zw_dyk, 0 },
};
static const OptDef ZT_AB[] = {   // Abyssea
    { "Visitant timer", "Timer visitant", OK_TOGGLE, OL_ADV, F_(ztAbTimer), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Timer bar width", "Largeur barre timer", OK_PCT, OL_ADV, F_(ztAbBarW), 0.40f, 1.00f, nullptr, nullptr, 0, zw_abt, 0 },
    { "Timer bar height", "Hauteur barre timer", OK_PCT, OL_ADV, F_(ztAbBarH), 0.50f, 2.50f, nullptr, nullptr, 0, zw_abt, 0 },
    { "Lights", "Lumi\xC3\xA8res", OK_TOGGLE, OL_ADV, F_(ztAbLights), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Light bar width", "Largeur barre lumi\xC3\xA8re", OK_PCT, OL_ADV, F_(ztAbLightW), 0.50f, 3.00f, nullptr, nullptr, 0, zw_abl, 0 },
    { "Light bar height", "Hauteur barre lumi\xC3\xA8re", OK_PCT, OL_ADV, F_(ztAbLightH), 0.50f, 2.50f, nullptr, nullptr, 0, zw_abl, 0 },
};
static const OptDef ZT_OM[] = {   // Omen
    { "Floor objective", "Objectif d'\xC3\xA9tage", OK_TOGGLE, OL_ADV, F_(ztOmObj), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Omens + bonus", "Omens + bonus", OK_TOGGLE, OL_ADV, F_(ztOmCount), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Objective rows", "Lignes d'objectifs", OK_TOGGLE, OL_ADV, F_(ztOmRows), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
};
static const OptDef ZT_NY[] = {   // Nyzul Isle
    { "Floor", "\xC3\x89tage", OK_TOGGLE, OL_ADV, F_(ztNyFloor), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Floor timer", "Timer d'\xC3\xA9tage", OK_TOGGLE, OL_ADV, F_(ztNyTime), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Objective", "Objectif", OK_TOGGLE, OL_ADV, F_(ztNyObj), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Restriction", "Restriction", OK_TOGGLE, OL_ADV, F_(ztNyRestr), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Floors cleared", "\xC3\x89tages faits", OK_TOGGLE, OL_ADV, F_(ztNyComp), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Reward rate", "Taux de r\xC3\xA9" "compense", OK_TOGGLE, OL_ADV, F_(ztNyRate), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Tokens", "Tokens", OK_TOGGLE, OL_ADV, F_(ztNyTok), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
};
static const OptDef ZT_SH[] = {   // Sheol (Odyssey)
    { "Segments", "Segments", OK_TOGGLE, OL_ADV, F_(ztSheolSeg), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Resistances", "R\xC3\xA9sistances", OK_TOGGLE, OL_ADV, F_(ztSheolRes), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Family name", "Nom de famille", OK_TOGGLE, OL_ADV, F_(ztShFam), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Weapon icon size", "Taille ic\xC3\xB4nes armes", OK_PCT, OL_ADV, F_(ztShIcon), 0.50f, 2.50f, nullptr, nullptr, 0, nullptr, 0 },
    { "Element dot size", "Taille pastilles \xC3\xA9l\xC3\xA9ments", OK_PCT, OL_ADV, F_(ztShDot), 0.50f, 2.50f, nullptr, nullptr, 0, nullptr, 0 },
    { "Cruel Joke", "Cruel Joke", OK_TOGGLE, OL_ADV, F_(ztSheolJoke), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
};
static const OptDef ZT_LB[] = {   // Limbus
    { "Area & level", "Zone et niveau", OK_TOGGLE, OL_ADV, F_(ztLbName), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Gauge width", "Largeur jauge", OK_PCT, OL_ADV, F_(ztLbBarW), 0.40f, 1.00f, nullptr, nullptr, 0, nullptr, 0 },
    { "Gauge height", "Hauteur jauge", OK_PCT, OL_ADV, F_(ztLbBarH), 0.50f, 2.50f, nullptr, nullptr, 0, nullptr, 0 },
    { "Floor on gauge", "\xC3\x89tage sur la jauge", OK_TOGGLE, OL_ADV, F_(ztLbFloor), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Currencies", "Monnaies", OK_TOGGLE, OL_ADV, F_(ztLbCur), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Run total", "Total du run", OK_TOGGLE, OL_ADV, F_(ztLbRun), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Coffer dots", "Pastilles coffres", OK_TOGGLE, OL_ADV, F_(ztLbChips), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
};
static const OptDef ZT_SO[] = {   // Sortie
    { "Gallimaufry", "Gallimaufry", OK_TOGGLE, OL_ADV, F_(ztSoGal), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Bosses and shards", "Boss et shards", OK_TOGGLE, OL_ADV, F_(ztSoBoss), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Coffers and NM", "Coffres et NM", OK_TOGGLE, OL_ADV, F_(ztSoCof), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Items obtained", "Objets obtenus", OK_TOGGLE, OL_ADV, F_(ztSoLoot), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Wing NM / bitzer", "NM / bitzer de l'aile", OK_TOGGLE, OL_ADV, F_(ztSoNav), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Auto Widescan track", "Track Widescan auto", OK_TOGGLE, OL_ADV, F_(ztSoTrack), 0, 0, nullptr, nullptr, 0, nullptr, 0 },   // sent by the aioupdate addon
};
static const OptDef ZT_NOTE[] = {
    { "Only appears in a tracked zone (Dynamis, Abyssea, Omen, Nyzul, Sheol, Limbus, Sortie).", "Appara\xC3\xAet seulement en zone suivie (Dynamis, Abyssea, Omen, Nyzul, Sheol, Limbus, Sortie).", OK_NOTE, OL_ADV, nullptr, 0, 0, nullptr, nullptr, 0, nullptr, 0 },
};
#undef F_
#define NOPT(a) ((int)(sizeof(a) / sizeof((a)[0])))
static const OptDef* const ZT_ZONE[7] = { ZT_DY, ZT_AB, ZT_OM, ZT_NY, ZT_SH, ZT_LB, ZT_SO };
static const int ZT_ZONE_N[7] = { NOPT(ZT_DY), NOPT(ZT_AB), NOPT(ZT_OM), NOPT(ZT_NY), NOPT(ZT_SH), NOPT(ZT_LB), NOPT(ZT_SO) };

void ConfigPage::draw_zt_v2(u32 dev, Font* fo, const MouseState* mo, bool click,
                            float& ry, int& ri, float e,
                            float bandX, float bandW, float coX, float ctrlW,
                            float hdrX, float hdrW) {
    Kit K;
    if (!v2_kit(K, mo, click)) { draw_zt_config(dev, fo, mo, click, ry, ri, e, bandX, bandW, coX, ctrlW, hdrX, hdrW); return; }
    g_fade = e;
    UiConfig& c = ui_config();
    const bool adv = c.cfgAdv != 0;
    const float x = coX, w = ctrlW;
    const int base = CTRL_ID;
    ry += K.px(6.0f);
    if (v2_section(K, ctrl_uid_i(base, 1), 6, "Display", "Affichage", x, ry, w, cfg_count_modified(ZT_DISPLAY, NOPT(ZT_DISPLAY), adv)))
        v2_rows(K, ctrl_uid_i(base, 2), ZT_DISPLAY, NOPT(ZT_DISPLAY), x, ry, w);
    if (adv) {
        BoxStyle& bs = c.ztBox;
        const FrameRefs fr = { &bs.on, &bs.border, &bs.alpha, &bs.alphaOwn, &bs.themeCopy, &bs.theme, &bs.hue, &bs.lum };
        if (v2_section(K, ctrl_uid_i(base, 3), 20, "Frame", "Cadre", x, ry, w, cfg_frame_modified(fr))) { cfg_frame_rows(K, ctrl_uid_i(base, 4), x, ry, w, fr); v2_end(K, ry); }
        // Content : the title, then the PREVIEW zone's own switches (the old page's rule)
        const int zv = (c.ztVariant < 0 || c.ztVariant > 6) ? 1 : c.ztVariant;
        const int nMod = cfg_count_modified(ZT_HEAD, NOPT(ZT_HEAD), true) + cfg_count_modified(ZT_ZONE[zv], ZT_ZONE_N[zv], true);
        if (v2_section(K, ctrl_uid_i(base, 5), 15, "Content", "Contenu", x, ry, w, nMod)) {
            cfg_rows(K, ctrl_uid_i(base, 6), ZT_HEAD, NOPT(ZT_HEAD), x, ry, w, true);
            cfg_rows(K, ctrl_uid_i(base, 7 + zv), ZT_ZONE[zv], ZT_ZONE_N[zv], x, ry, w, true);   // 7..13 : one uid block per zone
            v2_rows(K, ctrl_uid_i(base, 14), ZT_NOTE, NOPT(ZT_NOTE), x, ry, w);
        }
        // Text : Header, then the preview zone's own elements (the selector walks THIS list)
        int el[ZT_TE_COUNT]; const char* lab[ZT_TE_COUNT]; int n = 0;
        el[n] = ZT_HEADER; lab[n++] = tr("Header", "Titre");
        if (zv == 0) {
            el[n] = ZT_DY_TIMER; lab[n++] = tr("Run timer", "Timer de run");
            el[n] = ZT_DY_KI;    lab[n++] = tr("Key items", "Objets cl\xC3\xA9s");
        }
        if (zv == 1) {
            el[n] = ZT_AB_TIMER; lab[n++] = tr("Visitant timer", "Timer visitant");
            el[n] = ZT_AB_LIGHT; lab[n++] = tr("Light labels", "Labels lumi\xC3\xA8res");
            el[n] = ZT_AB_VAL;   lab[n++] = tr("Light values", "Valeurs lumi\xC3\xA8res");
        }
        if (zv == 2) {
            el[n] = ZT_OM_OBJ;   lab[n++] = tr("Floor objective", "Objectif d'\xC3\xA9tage");
            el[n] = ZT_OM_COUNT; lab[n++] = tr("Omens + bonus", "Omens + bonus");
            el[n] = ZT_OM_ROW;   lab[n++] = tr("Objective rows", "Lignes d'objectifs");
        }
        if (zv == 3) {
            el[n] = ZT_NY_FLOOR; lab[n++] = tr("Floor", "\xC3\x89tage");
            el[n] = ZT_NY_TIME;  lab[n++] = tr("Floor timer", "Timer d'\xC3\xA9tage");
            el[n] = ZT_NY_OBJ;   lab[n++] = tr("Objective", "Objectif");
            el[n] = ZT_NY_RESTR; lab[n++] = tr("Restriction", "Restriction");
            el[n] = ZT_NY_STATS; lab[n++] = tr("Stat rows", "Lignes de stats");
        }
        if (zv == 4) {
            el[n] = ZT_SH_SEG;  lab[n++] = tr("Segments", "Segments");
            el[n] = ZT_SH_FAM;  lab[n++] = tr("Family name", "Nom de famille");
            el[n] = ZT_SH_RES;  lab[n++] = tr("Resistances", "R\xC3\xA9sistances");
            el[n] = ZT_SH_JOKE; lab[n++] = tr("Cruel Joke", "Cruel Joke");
        }
        if (zv == 5) {
            el[n] = ZT_BODY;     lab[n++] = tr("Area & level", "Zone et niveau");
            el[n] = ZT_LB_GAUGE; lab[n++] = tr("Gauge", "Jauge");
            el[n] = ZT_LB_CUR;   lab[n++] = tr("Currencies", "Monnaies");
            el[n] = ZT_LB_RUN;   lab[n++] = tr("Run total", "Total du run");
            el[n] = ZT_LB_CHIP;  lab[n++] = tr("Dot labels", "Labels pastilles");
        }
        if (zv == 6) {
            el[n] = ZT_SO_GAL;  lab[n++] = tr("Gallimaufry", "Gallimaufry");
            el[n] = ZT_SO_BOSS; lab[n++] = tr("Boss letters", "Lettres des boss");
            el[n] = ZT_SO_LINE; lab[n++] = tr("Coffers and items", "Coffres et objets");
        }
        TextStyle* ts[ZT_TE_COUNT]; for (int i = 0; i < n; ++i) ts[i] = &c.ztText[el[i]];
        if (v2_section(K, ctrl_uid_i(base, 15), 5, "Text", "Texte", x, ry, w, cfg_text_modified(ts, n))) { cfg_text_rows(K, ctrl_uid_i(base, 16), x, ry, w, lab, ts, n, &cfgZtTextElem_); v2_end(K, ry); }
    } else v2_simple_note(K, x, ry);
    g_fade = e;
}
#undef NOPT

} // namespace aio
