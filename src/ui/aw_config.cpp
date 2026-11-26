// aw_config.cpp -- the Absorb-TP module's settings panel (//aio config -> "Absorb-TP").
//
// Each control is keyed by CTRL_ID (file:line hash, see config_controls.h) -- no hand-numbered uids. Reuses the shared
// sub-section catOpen_ slot [6] (Display) like the other module panels (only one renders at a time). The box is
// placed in //aio edit like every other box.
#include "ui/config_page.h"
#include "ui/config_controls.h"    // cat_header / row_slider / toggle_chip + palette
#include "ui/config_rows.h"        // ROW_BAND / ROW_NEXT / ROW_TOGGLE / ROW_GROW
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
    if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Display", "Affichage"), catOpen_[6], aF6_)) catOpen_[6] = !catOpen_[6];
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
    if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Content", "Contenu"), catOpen_[21], aF21_)) catOpen_[21] = !catOpen_[21];
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

} // namespace aio
