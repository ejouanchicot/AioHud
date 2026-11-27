// aw_config.cpp -- the Absorb-TP module's settings panel (//aio config -> "Absorb-TP").
//
// Each control is keyed by CTRL_ID (file:line hash, see config_controls.h) -- no hand-numbered uids. Reuses the shared
// sub-section catOpen_ slot [6] (Display) like the other module panels (only one renders at a time). The box is
// placed in //aio edit like every other box.
#include "ui/config_page.h"
#include "ui/config_controls.h"    // cat_header / row_slider / toggle_chip + palette
#include "ui/config_rows.h"        // ROW_BAND / ROW_NEXT / ROW_TOGGLE / ROW_GROW
#include "ui/cfg_engine.h"         // the redesign : OptDef tables + the kit (draw_aw_v2)
#include "model/ui_config.h"
#include "gfx/font.h"
#include "gfx/draw.h"
#include <cstdio>

namespace aio {

void ConfigPage::draw_aw_config(u32 dev, Font* fo, const MouseState* mo, bool click,
                                float& ry, int& ri, float e,
                                float bandX, float bandW, float coX, float ctrlW,
                                float hdrX, float hdrW) {
    UiConfig& c = ui_config();

    const float aF6_ = cat_fold(CTRL_ID, !ui_config().cfgAdv || (catOpen_[6]));
    cat_panel(dev, hdrX, ry, hdrW, cat_card_h(catH_[6], aF6_));
    if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Display", "Affichage"), catOpen_[6], aF6_) && ui_config().cfgAdv) catOpen_[6] = !catOpen_[6];
    ROW_NEXT(42.0f)
    if (aF6_ > 0.0f) {
        const float top6_ = ry;
        cat_fold_clip(dev, hdrX, top6_, hdrW, catH_[6] * aF6_);
        ROW_TOGGLE(CTRL_ID, tr("Show", "Afficher"), c.awShow)
        { ROW_BAND(52.0f)   // the shape : the dial (the last cast in the middle, the casters round it) or the table
            const char* ST[2] = { tr("Animated list", "Liste anim\xC3\xA9" "e"), tr("Table (classic)", "Tableau (classique)") };
            const int st = c.awStyle == 1 ? 1 : 0;
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Style", "Style"), ST[st])) c.awStyle = wrap(st + d, 2);
        } ROW_NEXT(52.0f)
        { ROW_BAND(46.0f)   // Size
            const float lo = 0.50f, hi = 2.00f; char b[16]; sprintf(b, "%d%%", (int)(c.awScale * 100.0f + 0.5f));
            float v01 = (c.awScale - lo) / (hi - lo); v01 = clampf(v01, 0.0f, 1.0f);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Size", "Taille"), b, &v01)) { float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f; c.awScale = v < lo ? lo : (v > hi ? hi : v); }
        } ROW_NEXT(46.0f)
        ROW_GROW(CTRL_ID, UiConfig::GB_AW)   // which way the box grows as its content widens (ui/box_grow.h)
        cat_fold_end(dev, ry, top6_, catH_[6], aF6_);
    }   // end Display
    ry += snap(16.0f);
    // ===== sub-section : FRAME (the config grammar : Display / Frame / Content / Behaviour / Text, docs/architecture/config-panels.md) =====
if (ui_config().cfgAdv) {   // Advanced only (the config's depth, 2026-11-26)
    const float aF20_ = cat_fold(CTRL_ID, catOpen_[20]);
    cat_panel(dev, hdrX, ry, hdrW, cat_card_h(catH_[20], aF20_));
    if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Frame", "Cadre"), catOpen_[20], aF20_)) catOpen_[20] = !catOpen_[20];
    ROW_NEXT(42.0f)
    if (aF20_ > 0.0f) {
        const float top20_ = ry;
        cat_fold_clip(dev, hdrX, top20_, hdrW, catH_[20] * aF20_);
        draw_box_appearance(dev, fo, mo, click, ry, ri, e, bandX, bandW, coX, ctrlW, c.awBox);   // Box / Transparency / Theme / Hue / Luminosity
        cat_fold_end(dev, ry, top20_, catH_[20], aF20_);
    }   // end Frame
    ry += snap(16.0f);
}   // end Advanced only
    // ===== sub-section : CONTENT (the config grammar : Display / Frame / Content / Behaviour / Text, docs/architecture/config-panels.md) =====
    const float aF21_ = cat_fold(CTRL_ID, !ui_config().cfgAdv || (catOpen_[21]));
    cat_panel(dev, hdrX, ry, hdrW, cat_card_h(catH_[21], aF21_));
    if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Content", "Contenu"), catOpen_[21], aF21_) && ui_config().cfgAdv) catOpen_[21] = !catOpen_[21];
    ROW_NEXT(42.0f)
    if (aF21_ > 0.0f) {
        const float top21_ = ry;
        cat_fold_clip(dev, hdrX, top21_, hdrW, catH_[21] * aF21_);
        if (c.awStyle == 0) {
            { ROW_BAND(52.0f)   // the beacon (the last cast / the warning) : on top, at the bottom, none
                const char* LY[3] = { tr("Beacon on top", "Rep\xC3\xA8re en haut"), tr("Beacon at the bottom", "Rep\xC3\xA8re en bas"), tr("List only", "Liste seule") };
                const int ly = (c.awLayout >= 0 && c.awLayout <= 2) ? c.awLayout : 0;
                if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Layout", "Disposition"), LY[ly])) c.awLayout = wrap(ly + d, 3);
            } ROW_NEXT(52.0f)
            if (ui_config().cfgAdv) {   // Simple : the sizes, badges and animations stay in Advanced
            { ROW_BAND(46.0f)   // the list's text
                const float lo = 0.70f, hi = 1.60f; char b[16]; sprintf(b, "%d%%", (int)(c.awText * 100.0f + 0.5f));
                float v01 = clampf((c.awText - lo) / (hi - lo), 0.0f, 1.0f);
                if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Text size", "Taille du texte"), b, &v01)) { float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f; c.awText = v < lo ? lo : (v > hi ? hi : v); }
            } ROW_NEXT(46.0f)
            if (c.awLayout != 2) {
                { ROW_BAND(46.0f)   // the beacon's orb
                    const float lo = 0.60f, hi = 1.80f; char b[16]; sprintf(b, "%d%%", (int)(c.awOrb * 100.0f + 0.5f));
                    float v01 = clampf((c.awOrb - lo) / (hi - lo), 0.0f, 1.0f);
                    if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Orb size", "Taille de l'orbe"), b, &v01)) { float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f; c.awOrb = v < lo ? lo : (v > hi ? hi : v); }
                } ROW_NEXT(46.0f)
            }
            { ROW_BAND(52.0f)   // the job badges
                const char* JB[3] = { tr("None", "Aucun"), tr("Job", "Job"), tr("Initials", "Initiales") };
                const int jb = (c.awJobs >= 0 && c.awJobs <= 2) ? c.awJobs : 1;
                if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Badges", "Badges"), JB[jb])) c.awJobs = wrap(jb + d, 3);
            } ROW_NEXT(52.0f)
            if (c.awJobs) {
                { ROW_BAND(46.0f)
                    const float lo = 0.70f, hi = 1.60f; char b[16]; sprintf(b, "%d%%", (int)(c.awBadge * 100.0f + 0.5f));
                    float v01 = clampf((c.awBadge - lo) / (hi - lo), 0.0f, 1.0f);
                    if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Badge size", "Taille des badges"), b, &v01)) { float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f; c.awBadge = v < lo ? lo : (v > hi ? hi : v); }
                } ROW_NEXT(46.0f)
            }
            ROW_TOGGLE(CTRL_ID, tr("Absorb animation", "Animation d'absorption"), c.awFx)
            if (c.awFx) { ROW_TOGGLE(CTRL_ID, tr("Scene distortion", "D\xC3\xA9" "formation du d\xC3\xA9" "cor"), c.awDistort) }
            }
        }
        ROW_TOGGLE(CTRL_ID, tr("Colour the TP", "Couleur des TP"), c.awColors)       // white / green / yellow / orange / red ; blue = nothing drained
        ROW_TOGGLE(CTRL_ID, tr("Time since", "Temps \xC3\xA9""coul\xC3\xA9"), c.awTimer)
        ROW_TOGGLE(CTRL_ID, tr("Absorb count", "Nombre d'Absorb"), c.awCount)
        { ROW_BAND(46.0f)   // how long without a new Absorb-TP before that caster's row turns red
            const float lo = 10.0f, hi = 600.0f; char b[16]; sprintf(b, "%ds", c.awScreen);
            float v01 = ((float)c.awScreen - lo) / (hi - lo); v01 = clampf(v01, 0.0f, 1.0f);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Red alert after", "Alerte rouge apr\xC3\xA8s"), b, &v01)) { int v = (int)(lo + v01 * (hi - lo) + 0.5f); v = (v / 5) * 5; c.awScreen = v < 10 ? 10 : (v > 600 ? 600 : v); }
            if (c.awMemory < c.awScreen) c.awMemory = c.awScreen;   // the memory holds the row at least until it turns red
        } ROW_NEXT(46.0f)
        { ROW_BAND(46.0f)   // how far back the count looks
            const float lo = 60.0f, hi = 3600.0f; char b[16]; sprintf(b, "%d min", c.awMemory / 60);
            float v01 = ((float)c.awMemory - lo) / (hi - lo); v01 = clampf(v01, 0.0f, 1.0f);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Keep & count over", "Garder et compter sur"), b, &v01)) { int v = (int)(lo + v01 * (hi - lo) + 0.5f); v = (v / 60) * 60; c.awMemory = v < 60 ? 60 : (v > 3600 ? 3600 : v); }
            if (c.awScreen > c.awMemory) c.awScreen = c.awMemory;   // ... and the alert never outlives it
        } ROW_NEXT(46.0f)
        { ROW_BAND(40.0f)   // note
            const float ty = ry + yo; fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + snap(16.0f), tr("A row turns red when that player stops casting Absorb-TP.", "Une ligne passe en rouge quand le joueur ne relance plus Absorb-TP."), snap(12.0f), fa(C_MUTE), fa(C_STROKE), 1.0f);
        } ROW_NEXT(40.0f)
        cat_fold_end(dev, ry, top21_, catH_[21], aF21_);
    }   // end Content
    ry += snap(16.0f);
    if (!ui_config().cfgAdv) SIMPLE_MORE   // Simple : where the rest is
}


// ======== THE NEW ABSORB-TP PAGE (the config redesign, milestone 3, 2026-11-26) ========
// Display / Frame / Content, like the old page ; a row the old page hid behind the style / layout / badges / animation
// is now greyed with its reason. The two time sliders keep their own units (seconds, minutes) and their coupling.
#define F_(name) [](UiConfig& c) -> void* { return &c.name; }
static const char* const AW_ST_EN[2] = { "Animated list", "Table (classic)" },   * const AW_ST_FR[2] = { "Liste anim\xC3\xA9""e", "Tableau (classique)" };
static const char* const AW_LY_EN[3] = { "Beacon on top", "Beacon at the bottom", "List only" };
static const char* const AW_LY_FR[3] = { "Rep\xC3\xA8re en haut", "Rep\xC3\xA8re en bas", "Liste seule" };
static const char* const AW_JB_EN[3] = { "None", "Job", "Initials" },             * const AW_JB_FR[3] = { "Aucun", "Job", "Initiales" };
static const char* aw_list(const UiConfig& c)  { return c.awStyle == 0 ? nullptr : tr("inactive : table style", "inactif : style tableau"); }
static const char* aw_orb(const UiConfig& c)   { if (const char* w = aw_list(c)) return w; return c.awLayout != 2 ? nullptr : tr("inactive : list only", "inactif : liste seule"); }
static const char* aw_badge(const UiConfig& c) { if (const char* w = aw_list(c)) return w; return c.awJobs ? nullptr : tr("inactive : badges off", "inactif : badges coup\xC3\xA9s"); }
static const char* aw_fx(const UiConfig& c)    { if (const char* w = aw_orb(c)) return w; return c.awFx ? nullptr : tr("inactive : absorb animation off", "inactif : animation d'absorption coup\xC3\xA9""e"); }

static const OptDef AW_DISPLAY[] = {
    { "Show", "Afficher", OK_TOGGLE, OL_SIMPLE, F_(awShow), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Style", "Style", OK_SELECT, OL_SIMPLE, F_(awStyle), 0, 0, AW_ST_EN, AW_ST_FR, 2, nullptr, 0 },
    { "Size", "Taille", OK_PCT, OL_SIMPLE, F_(awScale), 0.5f, 2.0f, nullptr, nullptr, 0, nullptr, 0 },
    { "Grows toward", "Grandit vers", OK_GROW, OL_SIMPLE, nullptr, 0, 0, nullptr, nullptr, 0, nullptr, UiConfig::GB_AW },
};
static const OptDef AW_CONTENT_A[] = {   // the animated list's own rows (greyed on the table)
    { "Layout", "Disposition", OK_SELECT, OL_SIMPLE, F_(awLayout), 0, 0, AW_LY_EN, AW_LY_FR, 3, aw_list, 0 },
    { "Text size", "Taille du texte", OK_PCT, OL_ADV, F_(awText), 0.7f, 1.6f, nullptr, nullptr, 0, aw_list, 0 },
    { "Orb size", "Taille de l'orbe", OK_PCT, OL_ADV, F_(awOrb), 0.6f, 1.8f, nullptr, nullptr, 0, aw_orb, 0 },
    { "Badges", "Badges", OK_SELECT, OL_ADV, F_(awJobs), 0, 0, AW_JB_EN, AW_JB_FR, 3, aw_list, 0 },
    { "Badge size", "Taille des badges", OK_PCT, OL_ADV, F_(awBadge), 0.7f, 1.6f, nullptr, nullptr, 0, aw_badge, 0 },
    { "Absorb animation", "Animation d'absorption", OK_TOGGLE, OL_ADV, F_(awFx), 0, 0, nullptr, nullptr, 0, aw_orb, 0 },   // the orb's : not in "List only"
    { "Scene distortion", "D\xC3\xA9""formation du d\xC3\xA9""cor", OK_TOGGLE, OL_ADV, F_(awDistort), 0, 0, nullptr, nullptr, 0, aw_fx, 0 },
    { "Colour the TP", "Couleur des TP", OK_TOGGLE, OL_SIMPLE, F_(awColors), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Time since", "Temps \xC3\xA9""coul\xC3\xA9", OK_TOGGLE, OL_SIMPLE, F_(awTimer), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Absorb count", "Nombre d'Absorb", OK_TOGGLE, OL_SIMPLE, F_(awCount), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
};
static const OptDef AW_CONTENT_B[] = {
    { "A row turns red when that player stops casting Absorb-TP.", "Une ligne passe en rouge quand le joueur ne relance plus Absorb-TP.", OK_NOTE, OL_SIMPLE, nullptr, 0, 0, nullptr, nullptr, 0, nullptr, 0 },
};
#undef F_
#define NOPT(a) ((int)(sizeof(a) / sizeof((a)[0])))

void ConfigPage::draw_aw_v2(u32 dev, Font* fo, const MouseState* mo, bool click,
                            float& ry, int& ri, float e,
                            float bandX, float bandW, float coX, float ctrlW,
                            float hdrX, float hdrW) {
    Kit K;
    if (!v2_kit(K, mo, click)) { draw_aw_config(dev, fo, mo, click, ry, ri, e, bandX, bandW, coX, ctrlW, hdrX, hdrW); return; }
    g_fade = e;
    UiConfig& c = ui_config();
    const bool adv = c.cfgAdv != 0;
    const float x = coX, w = ctrlW;
    const int base = CTRL_ID;
    ry += K.px(6.0f);
    if (v2_section(K, ctrl_uid_i(base, 1), 6, "Display", "Affichage", x, ry, w, cfg_count_modified(AW_DISPLAY, NOPT(AW_DISPLAY), adv)))
        v2_rows(K, ctrl_uid_i(base, 2), AW_DISPLAY, NOPT(AW_DISPLAY), x, ry, w);
    if (adv) {
        BoxStyle& bs = c.awBox;
        const FrameRefs fr = { &bs.on, &bs.border, &bs.alpha, &bs.alphaOwn, &bs.themeCopy, &bs.theme, &bs.hue, &bs.lum };
        if (v2_section(K, ctrl_uid_i(base, 3), 20, "Frame", "Cadre", x, ry, w, cfg_frame_modified(fr))) { cfg_frame_rows(K, ctrl_uid_i(base, 4), x, ry, w, fr); v2_end(K, ry); }
    }
    // the two time sliders (their own units, their coupling) : Simple's Content, Advanced's Behaviour
    auto timeRows = [&]() {
        {   // Red alert after : seconds without a new Absorb-TP before that caster's row turns red (10..600, step 5)
            const float lo = 10.0f, hi = 600.0f; char b[16]; _snprintf(b, sizeof(b), "%ds", c.awScreen); b[15] = 0;
            float v01 = ((float)c.awScreen - lo) / (hi - lo); v01 = clampf(v01, 0.0f, 1.0f);
            if (cfg_slider_row(K, ctrl_uid_i(base, 7), x, ry, w, tr("Red alert after", "Alerte rouge apr\xC3\xA8s"), &v01, b, nullptr, &c.awScreen, 4, false)) {
                int v = (int)(lo + v01 * (hi - lo) + 0.5f); v = (v / 5) * 5; c.awScreen = v < 10 ? 10 : (v > 600 ? 600 : v);
                if (c.awMemory < c.awScreen) c.awMemory = c.awScreen; }   // the memory holds the row at least until it turns red
        }
        {   // Keep & count over : how far back the count looks (60..3600 s, shown in minutes)
            const float lo = 60.0f, hi = 3600.0f; char b[16]; _snprintf(b, sizeof(b), "%d min", c.awMemory / 60); b[15] = 0;
            float v01 = ((float)c.awMemory - lo) / (hi - lo); v01 = clampf(v01, 0.0f, 1.0f);
            if (cfg_slider_row(K, ctrl_uid_i(base, 8), x, ry, w, tr("Keep & count over", "Garder et compter sur"), &v01, b, nullptr, &c.awMemory, 4, false)) {
                int v = (int)(lo + v01 * (hi - lo) + 0.5f); v = (v / 60) * 60; c.awMemory = v < 60 ? 60 : (v > 3600 ? 3600 : v);
                if (c.awScreen > c.awMemory) c.awScreen = c.awMemory; }   // ... and the alert never outlives it
        }
        cfg_rows(K, ctrl_uid_i(base, 9), AW_CONTENT_B, NOPT(AW_CONTENT_B), x, ry, w, adv);
    };
    const int nTime = cfg_count_modified_bytes(&c.awScreen, 4) + cfg_count_modified_bytes(&c.awMemory, 4);
    if (!adv) {   // Simple : one Content card, as before
        if (v2_section(K, ctrl_uid_i(base, 5), 21, "Content", "Contenu", x, ry, w, cfg_count_modified(AW_CONTENT_A, NOPT(AW_CONTENT_A), false) + nTime)) {
            cfg_rows(K, ctrl_uid_i(base, 6), AW_CONTENT_A, NOPT(AW_CONTENT_A), x, ry, w, false);
            timeRows();
            v2_end(K, ry);
        }
        v2_simple_note(K, x, ry);
        g_fade = e;
        return;
    }
    // ADVANCED (2026-11-27) : sections by ELEMENT. The animated list's rows exist only in that style (shown only then, not
    // greyed) ; the orb only with a beacon. Slices of AW_CONTENT_A : [0..1] layout + text, [2] orb, [3..6] badges + animation,
    // [7..9] the caster rows (both styles).
    static_assert(NOPT(AW_CONTENT_A) == 10, "the Advanced slices below index AW_CONTENT_A");
    if (c.awStyle == 0) {
        const bool orb = c.awLayout != 2;
        const int nL = cfg_count_modified(AW_CONTENT_A, 2, true) + (orb ? cfg_count_modified(AW_CONTENT_A + 2, 1, true) : 0) + cfg_count_modified(AW_CONTENT_A + 3, 4, true);
        if (v2_section(K, ctrl_uid_i(base, 10), 21, "Animated list", "Liste anim\xC3\xA9""e", x, ry, w, nL)) {
            cfg_rows(K, ctrl_uid_i(base, 11), AW_CONTENT_A, 2, x, ry, w, true);
            if (orb) cfg_rows(K, ctrl_uid_i(base, 12), AW_CONTENT_A + 2, 1, x, ry, w, true);
            cfg_rows(K, ctrl_uid_i(base, 13), AW_CONTENT_A + 3, 4, x, ry, w, true);
            v2_end(K, ry);
        }
    }
    if (v2_section(K, ctrl_uid_i(base, 14), 23, "Caster rows", "Lignes des lanceurs", x, ry, w, cfg_count_modified(AW_CONTENT_A + 7, 3, true)))
        v2_rows(K, ctrl_uid_i(base, 15), AW_CONTENT_A + 7, 3, x, ry, w);
    if (v2_section(K, ctrl_uid_i(base, 16), 22, "Behaviour", "Comportement", x, ry, w, nTime)) { timeRows(); v2_end(K, ry); }
    g_fade = e;
}
#undef NOPT

} // namespace aio
