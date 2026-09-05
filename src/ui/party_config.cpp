// party_config.cpp -- the Party / Alliance module's settings panel (//aio config -> "Groupe / Alliance").
//
// Split into three sibling sub-sections (no outer module wrapper, like every other module) :
//   Party    -- its box theme (appearance) + all party-box settings.
//   Alliance -- a "Same as Party / Custom" theme toggle (+ its own theme if Custom) + alliance-box settings.
//   Text     -- per-element typography, with a small Party/Alliance selector for which box's text to edit.
// The FRAME block IS shared now (draw_frame_section, box_style.cpp), called once per group. This file used to say
// the opposite -- that the blocks were written out twice on purpose, because a helper called twice would reuse
// each control's CTRL_ID (a source-line uid) and the two groups' sliders would drag together. That was true when
// it was written; ctrl_uid_i(CTRL_ID, group) is the escape and the config-panels doc says so. The justification
// outlived the problem, which is how ~90 duplicated lines became permanent. Any block copied per group should be
// read the same way: ask whether the reason still holds before copying it again.
#include "ui/config_page.h"
#include "ui/config_controls.h"   // shared toolkit : cat_header / row_slider / toggle_chip / row_selector + palette + g_fade
#include "ui/config_rows.h"       // ROW_BAND / ROW_NEXT row-layout macros (shared with config_page.cpp)
#include "model/ui_config.h"      // ui_config(), save/reset, TE_* enum, TextStyle
#include "model/buff_groups.h"    // BuffGroup + BUFF_GROUP_EN/FR : the party buff strip's display groups
#include "model/party_state.h"    // party().status_seen : which statuses have actually turned up this session
#include "ui/buff_atlas.h"       // the ONE shared status-icon atlas + buff_cell_uv (borrowed, never released here)
#include "ui/party.h"            // party_last_buff_icon_px : the size the HUD really draws a status icon at
#include "gfx/draw.h"            // tquad : the grid cells draw a real status icon, not a placeholder
#include "gfx/d3d.h"             // dTexQuadState / dSetTex
#include "gfx/font.h"
#include "gfx/draw.h"
#include "gfx/window.h"           // box themes : window_theme_family/variant/name, box_family_*, box_hue_*
#include <cmath>
#include <cstdio>
#include <cstring>

namespace aio {

// Move one entry of the buff strip from `from` to `to`, and persist. ONE function for both the arrows and
// the drop, and for both levels : a swap is just a span move of one step, and having two code paths for
// "the same gesture at two levels" would be the first thing to drift apart.
//
// Inside a group it also MATERIALISES the arranged prefix, from what is on screen down to the deeper of
// the two indices, before moving. Seeding from the displayed order is what makes the first move on a
// group preserve everything above it -- the built-in order and the stored one can never then disagree
// about what the user was looking at. Beyond BUFF_PIN_MAX there is nothing to store, so the move is a
// no-op rather than a silent partial one.
static void strip_apply_move(bool atGroups, int innerG, int from, int to,
                             const unsigned short* mem, int nmem,
                             const int* slots, int nslots) {
    UiConfig& c = ui_config();
    if (from == to || from < 0 || to < 0) return;
    if (atGroups) {
        // `from`/`to` index the VISIBLE band, which may be missing the empty groups. Reorder the values
        // among the slots the band is actually showing and write them back : a group the band left out
        // keeps its own position exactly, so hiding the empties from view never silently re-ranks them.
        if (!slots || from >= nslots || to >= nslots || nslots > UiConfig::BUFF_ORDER_N) return;
        unsigned char v[UiConfig::BUFF_ORDER_N];
        for (int k = 0; k < nslots; ++k) {
            if (slots[k] < 0 || slots[k] >= UiConfig::BUFF_ORDER_N) return;
            v[k] = c.buffOrder[slots[k]];
        }
        const unsigned char g0 = v[from];
        if (from < to) for (int k = from; k < to; ++k) v[k] = v[k + 1];
        else           for (int k = from; k > to; --k) v[k] = v[k - 1];
        v[to] = g0;
        for (int k = 0; k < nslots; ++k) c.buffOrder[slots[k]] = v[k];
        save_ui_config();
        return;
    }
    const int g = innerG;
    if (g < 0 || g >= UiConfig::BUFF_ORDER_N) return;
    int need = (from > to ? from : to) + 1; if (need > UiConfig::BUFF_PIN_MAX) need = UiConfig::BUFF_PIN_MAX;
    while (c.buffPinN[g] < need && c.buffPinN[g] < nmem) { c.buffPin[g][c.buffPinN[g]] = mem[c.buffPinN[g]]; ++c.buffPinN[g]; }
    if (from >= c.buffPinN[g] || to >= c.buffPinN[g]) return;   // past what the prefix can hold
    const unsigned short vv = c.buffPin[g][from];
    if (from < to) for (int i = from; i < to; ++i) c.buffPin[g][i] = c.buffPin[g][i + 1];
    else           for (int i = from; i > to; --i) c.buffPin[g][i] = c.buffPin[g][i - 1];
    c.buffPin[g][to] = vv;
    save_ui_config();
}

void ConfigPage::draw_party_config(u32 dev, Font* fo, const MouseState* mo, bool click,
                                   float& ry, int& ri, float e,
                                   float bandX, float bandW, float coX, float ctrlW,
                                   float hdrX, float hdrW) {
    // ROW_BAND / ROW_NEXT come from config_rows.h (dev/bandX/bandW/e/ry/ri/anim_ are all in scope here).

    // =========================================================== PARTY ===========================================================
    // ===================================================== THE GRAMMAR =====================================================
    // Every module panel reads in the same four sections -- General, Frame, Content, Text -- and the first two are
    // literally the same controls everywhere, because they drive the same shared code (box_style.cpp). Nothing was
    // removed: this is a RELAYOUT. Same UiConfig fields, same file keys, same profiles.
    //
    // Two things did the damage before. A single "Party" section carried eighteen unrelated settings, so opening it
    // did not narrow the search; and every row held ONE control on a column 560-960px wide, leaving the right half
    // empty -- Bar Height and Bar Width were two rows. The panel measured ~2370px, 2.3 screens.
    //
    // So: settings are grouped by the OBJECT they act on (gauges, badge, buffs, cursor), and related ones share a
    // row. A slider needs about 340px (its 244px capsule plus a readable label), so pairing is only done when the
    // column can actually hold two; below that the pair stacks and the band is twice as tall. One code path either
    // way -- the geometry is two ternaries, not two branches, because a second branch is what drifts.
    // The gutter is 44px, not 18. At 18 the first column's control -- which is RIGHT-aligned inside its half --
    // finished a hair before the second column's label began, so the two read as one run-on line instead of two
    // columns. A column needs air on both sides of its content before the eye accepts it as a column.
    // And a rule down the middle of that gutter, so the split is STATED rather than inferred from a gap.
    const bool  twoCol = ctrlW >= snap(720.0f);
    const float gutter = snap(44.0f);
    const float halfW  = twoCol ? (ctrlW - gutter) * 0.5f : ctrlW;
    const float col2X  = coX + (twoCol ? halfW + gutter : 0.0f);
    const float sepX   = snap(coX + halfW + gutter * 0.5f);
    // Drawn per paired ROW, never as one line down the whole section : a row whose second half is empty (no
    // procedural theme, no badge to size) has no split to state, and a rule through it would claim one.
    auto sepv = [&](float y, float h, bool on) {
        if (twoCol && on) flat(dev, sepX, snap(y + snap(7.0f)), snap(1.0f), snap(h) - snap(14.0f), 0x1EFFFFFFu);
    };

    // ========================================================== FRAME =========================================================
    // The same block every module gets, because it drives the same shared code. Drawn by draw_frame_section so
    // the party box and the alliance boxes cannot drift apart -- they used to be two ~90-line copies.
    const float aS0_ = cat_fold(CTRL_ID, pcFrameOpen_);   // eased 0..1 : the card, the clip and ry all ride this
    if (aS0_ > 0.0f) cat_panel(dev, hdrX, ry, hdrW, CAT_HEADER_ADV + pcFull_[0] * aS0_);
    if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Frame", "Cadre"), pcFrameOpen_)) pcFrameOpen_ = !pcFrameOpen_;
    ROW_NEXT(42.0f)
    if (aS0_ > 0.0f) {
        const float cTop_ = ry;
        cat_fold_clip(dev, hdrX, cTop_, hdrW, pcFull_[0] * aS0_);
        draw_frame_section(dev, fo, mo, click, ry, ri, e, bandX, bandW, coX, ctrlW,
                           0, nullptr,                                   // the party box IS the master : nothing to follow
                           &ui_config().skinTheme, &ui_config().skinHue, &ui_config().skinLum, &ui_config().skinBoxAlpha,
                           &ui_config().border[0], &ui_config().borderCost, tr("Cost box", "BoÃ®te coÃ»t"));
        cat_fold_end(dev, ry, cTop_, pcFull_[0], aS0_);
    }   // end Frame

    // ========================================================== PARTY ==========================================================
    // The party box itself : whether it shows, how big, and what a member row carries. Its FRAME is the section
    // above -- shared shape with every other module -- and its BUFF STRIP is the section at the bottom, because
    // the strip is one object and its editor belongs with the settings that describe it.
    // The only section that really differs between modules, so it gets the room. Grouped by the OBJECT each setting
    // acts on -- gauges, badge, buffs, cursor -- which is what turns three rows into one.
    const float aS1_ = cat_fold(CTRL_ID, catOpen_[1]);   // eased 0..1 : the card, the clip and ry all ride this
    if (aS1_ > 0.0f) cat_panel(dev, hdrX, ry, hdrW, CAT_HEADER_ADV + pcFull_[1] * aS1_);
    if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Party", "Party"), catOpen_[1])) catOpen_[1] = !catOpen_[1];
    ROW_NEXT(42.0f)
    if (aS1_ > 0.0f) {
        const float cTop_ = ry;
        cat_fold_clip(dev, hdrX, cTop_, hdrW, pcFull_[1] * aS1_);
        // Show + Size : the two settings everyone touches, first, on one line.
        { const float bh2 = twoCol ? snap(48.0f) : snap(96.0f);
          ROW_BAND(bh2) (void)yo;   // this row places its own lines (yA / yB) -- ROW_BAND's single-line centring does not apply
          const float yA = ry + (1.0f - ap) * snap(14.0f) + (twoCol ? (bh2 - snap(40.0f)) * 0.5f : snap(4.0f));
          const float yB = twoCol ? yA : yA + snap(48.0f);
          const float xB = twoCol ? col2X : coX;
          sepv(ry, bh2, true);
          row_toggle(dev, fo, mo, click, CTRL_ID, coX, yA, halfW, tr("Show", "Afficher"), &ui_config().partyShow, 40.0f, 150.0f);
          { const float lo = 1.00f, hi = 2.00f;   // party floor 100% : it must cover the native block
            char szbuf[16]; sprintf(szbuf, "%d%%", (int)(ui_config().box[0].scale * 100.0f + 0.5f));
            float v01 = (ui_config().box[0].scale - lo) / (hi - lo); v01 = v01 < 0.0f ? 0.0f : (v01 > 1.0f ? 1.0f : v01);
            if (row_slider(dev, fo, mo, CTRL_ID, xB, yB, halfW, tr("Size", "Taille"), szbuf, &v01)) {
                float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f;
                ui_config().box[0].scale = v < lo ? lo : (v > hi ? hi : v); } }
          ROW_NEXT(bh2)
        }
        // --- gauges : the style, and the two dimensions of the bars it draws ---
        { const float bh2 = twoCol ? snap(48.0f) : snap(96.0f);
          ROW_BAND(bh2) (void)yo;   // this row places its own lines (yA / yB) -- ROW_BAND's single-line centring does not apply
          const float yA = ry + (1.0f - ap) * snap(14.0f) + (twoCol ? (bh2 - snap(40.0f)) * 0.5f : snap(4.0f));
          const float yB = twoCol ? yA : yA + snap(48.0f);
          const float xB = twoCol ? col2X : coX;
          sepv(ry, bh2, true);
          { int s = ui_config().gaugeStyle[0]; if (s < 0 || s > 7) s = 0;
            const char* sb[8] = { tr("Vial", "Fiole"), tr("Bars", "Barres"), tr("Segments", "Segments"), tr("Minimal", "Minimal"),
                                  tr("Sphere", "Sph\xC3\xA8re"), tr("Ring", "Anneau"), tr("Crystal", "Cristal"), tr("Text", "Texte") };
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, yA, halfW, tr("Gauge Style", "Style de jauge"), sb[s])) {
                ui_config().gaugeStyle[0] = wrap(s + d, 8); save_ui_config(); } }
          { const float rowH = snap(38.0f); fo->begin(dev);   // Animation : two chips, HP and TP
            fo->draw_lc(dev, xB + snap(4.0f), yB + rowH * 0.5f, tr("Animation", "Animation"), ts_label(), fa(C_TEXT), fa(C_STROKE), 1.0f);
            const float bbw = snap(96.0f), bgap = snap(8.0f), bbh = snap(34.0f), bty = yB + (rowH - bbh) * 0.5f;
            const float bx0 = xB + halfW - (2 * bbw + bgap);
            if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx0, bty, bbw, bbh, ui_config().animHP ? tr("HP on", "HP oui") : tr("HP off", "HP non"), ui_config().animHP)) { ui_config().animHP = !ui_config().animHP; save_ui_config(); }
            if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx0 + bbw + bgap, bty, bbw, bbh, ui_config().animTP ? tr("TP on", "TP oui") : tr("TP off", "TP non"), ui_config().animTP)) { ui_config().animTP = !ui_config().animTP; save_ui_config(); } }
          ROW_NEXT(bh2)
        }
        { const float bh2 = twoCol ? snap(46.0f) : snap(92.0f);   // bar height + width : the pair that was two rows
          ROW_BAND(bh2) (void)yo;   // this row places its own lines (yA / yB) -- ROW_BAND's single-line centring does not apply
          const float yA = ry + (1.0f - ap) * snap(14.0f) + (twoCol ? (bh2 - snap(40.0f)) * 0.5f : snap(3.0f));
          const float yB = twoCol ? yA : yA + snap(46.0f);
          const float xB = twoCol ? col2X : coX;
          sepv(ry, bh2, true);
          { const float lo = 0.80f, hi = 1.80f; char hb[16]; sprintf(hb, "%d%%", (int)(ui_config().barHeight[0] * 100.0f + 0.5f));
            float v01 = (ui_config().barHeight[0] - lo) / (hi - lo); v01 = v01 < 0.0f ? 0.0f : (v01 > 1.0f ? 1.0f : v01);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, yA, halfW, tr("Bar Height", "Hauteur des barres"), hb, &v01)) {
                float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f;
                ui_config().barHeight[0] = v < lo ? lo : (v > hi ? hi : v); } }
          { const float lo = 0.70f, hi = 1.60f; char wb[16]; sprintf(wb, "%d%%", (int)(ui_config().barWidth[0] * 100.0f + 0.5f));
            float v01 = (ui_config().barWidth[0] - lo) / (hi - lo); v01 = v01 < 0.0f ? 0.0f : (v01 > 1.0f ? 1.0f : v01);
            if (row_slider(dev, fo, mo, CTRL_ID, xB, yB, halfW, tr("Bar Width", "Largeur des barres"), wb, &v01)) {
                float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f;
                ui_config().barWidth[0] = v < lo ? lo : (v > hi ? hi : v); } }
          ROW_NEXT(bh2)
        }
        // --- badge : what it shows, and how big ---
        { const bool hasBadge = (ui_config().jobBadge[0] != 0);
          const float bh2 = (twoCol || !hasBadge) ? snap(48.0f) : snap(96.0f);
          ROW_BAND(bh2) (void)yo;   // this row places its own lines (yA / yB) -- ROW_BAND's single-line centring does not apply
          const float yA = ry + (1.0f - ap) * snap(14.0f) + ((twoCol || !hasBadge) ? (bh2 - snap(40.0f)) * 0.5f : snap(4.0f));
          const float yB = twoCol ? yA : yA + snap(48.0f);
          const float xB = twoCol ? col2X : coX;
          sepv(ry, bh2, hasBadge);   // an empty second half has no split to state
          { int m = ui_config().jobBadge[0]; if (m < 0 || m > 3) m = 0;
            const char* jb[4] = { tr("Off", "Aucun"), tr("Main job", "Job principal"), tr("Main + Sub", "Principal + Sub"), tr("Icons", "Ic\xC3\xB4nes") };
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, yA, hasBadge ? halfW : ctrlW, tr("Job Badge", "Badge de job"), jb[m])) {
                ui_config().jobBadge[0] = wrap(m + d, 4); save_ui_config(); } }
          if (hasBadge) {   // no badge, nothing to size
              const float lo = 0.60f, hi = 1.80f; char gb[16]; sprintf(gb, "%d%%", (int)(ui_config().badgeScale[0] * 100.0f + 0.5f));
              float v01 = (ui_config().badgeScale[0] - lo) / (hi - lo); v01 = v01 < 0.0f ? 0.0f : (v01 > 1.0f ? 1.0f : v01);
              if (row_slider(dev, fo, mo, CTRL_ID, xB, yB, halfW, tr("Badge Size", "Taille du badge"), gb, &v01)) {
                  float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f;
                  ui_config().badgeScale[0] = v < lo ? lo : (v > hi ? hi : v); }
          }
          ROW_NEXT(bh2)
        }
        // --- the cursor, and what else the member row may show ---
        { const float bh2 = twoCol ? snap(48.0f) : snap(96.0f);
          ROW_BAND(bh2) (void)yo;   // this row places its own lines (yA / yB) -- ROW_BAND's single-line centring does not apply
          const float yA = ry + (1.0f - ap) * snap(14.0f) + (twoCol ? (bh2 - snap(40.0f)) * 0.5f : snap(4.0f));
          const float yB = twoCol ? yA : yA + snap(48.0f);
          const float xB = twoCol ? col2X : coX;
          sepv(ry, bh2, true);
          { const float lo = 0.50f, hi = 2.00f; char czbuf[16]; sprintf(czbuf, "%d%%", (int)(ui_config().cursorScale * 100.0f + 0.5f));
            float v01 = (ui_config().cursorScale - lo) / (hi - lo); v01 = v01 < 0.0f ? 0.0f : (v01 > 1.0f ? 1.0f : v01);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, yA, halfW, tr("Cursor Size", "Taille du curseur"), czbuf, &v01)) {
                float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f;
                ui_config().cursorScale = v < lo ? lo : (v > hi ? hi : v); } }
          { const float rowH = snap(38.0f); fo->begin(dev);
            fo->draw_lc(dev, xB + snap(4.0f), yB + rowH * 0.5f, tr("Row extras", "Sur la ligne"), ts_label(), fa(C_TEXT), fa(C_STROKE), 1.0f);
            const float bbw = snap(96.0f), bgap = snap(8.0f), bbh = snap(34.0f), bty = yB + (rowH - bbh) * 0.5f;
            const float bx0 = xB + halfW - (2 * bbw + bgap);
            if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx0, bty, bbw, bbh, tr("Casts", "Sorts"), ui_config().cast[0] != 0)) { ui_config().cast[0] = !ui_config().cast[0]; save_ui_config(); }
            if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx0 + bbw + bgap, bty, bbw, bbh, tr("Distance", "Distance"), ui_config().dist[0] != 0)) { ui_config().dist[0] = !ui_config().dist[0]; save_ui_config(); } }
          ROW_NEXT(bh2)
        }
        // ---- Distance-zone colours : the yalms number is tinted by cast-range zone (Close < 10' / Normal 10'..20.8' / Far >= 20.8'). ----
        // Three zones, three colours -- but three OPEN pickers is ~690px of panel for a setting most people touch
        // once. One row of swatches says everything the three label rows said (the colour IS the label), and the
        // picker opens for the one you click. Click it again to close it.
        if (ui_config().dist[0]) {
            u32* dcol[3] = { &ui_config().distColClose, &ui_config().distColNormal, &ui_config().distColFar };
            const char* dsh[3] = { tr("Close", "Proche"), tr("Normal", "Normal"), tr("Far", "Loin") };
            { ROW_BAND(48.0f)
                const float rowH = snap(38.0f), ty = ry + yo; fo->begin(dev);
                fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, tr("Distance colours", "Couleurs de distance"), ts_label(), fa(C_TEXT), fa(C_STROKE), 1.0f);
                const float sw2 = snap(74.0f), sh2 = snap(28.0f), sg = snap(8.0f);
                const float sx0 = coX + ctrlW - (3 * sw2 + 2 * sg), sy2 = ty + (rowH - sh2) * 0.5f;
                for (int di = 0; di < 3; ++di) {   // PASS 1 : the swatches (quads) + the clicks
                    const float sx = sx0 + di * (sw2 + sg);
                    const bool selc = (pcDistPick_ == di);
                    if (selc) { cs_add(dev); rrect_glow(dev, sx, sy2, sw2, sh2, snap(6.0f), (*dcol[di] & 0x00FFFFFF) | 0x80000000u, snap(6.0f)); cs(dev); }
                    rrect_fill(dev, sx, sy2, sw2, sh2, snap(6.0f), *dcol[di], shade(*dcol[di], -0.25f));
                    outline(dev, sx, sy2, sw2, sh2, selc ? 0xFFFFFFFF : C_BORDER);
                    if (inrect(mo, sx, sy2, sw2, sh2) && click) pcDistPick_ = selc ? -1 : di;
                }
                fo->begin(dev);
                for (int di = 0; di < 3; ++di) {   // PASS 2 : the zone names, in ONE font pass. White on a stroke so
                    const float sx = sx0 + di * (sw2 + sg);   // they stay readable on ANY colour the user picks.
                    fo->draw_c(dev, sx + sw2 * 0.5f, sy2 + sh2 * 0.5f, dsh[di], snap(11.5f), fa(0xFFFFFFFFu), fa(0xFF000000u), 1.4f);
                }
                ROW_NEXT(48.0f)
            }
            if (pcDistPick_ >= 0 && pcDistPick_ < 3) { CFG_COLOR_PICKER_I(dcol[pcDistPick_], pcDistPick_) }
        }
        cat_fold_end(dev, ry, cTop_, pcFull_[1], aS1_);
    }   // end Party

    // ==================================================== ALLIANCE ====================================================
    // The SAME three sections as the party box, in the same order, holding the same kinds of thing. That is the
    // point of a grammar : what you learned one category up still applies here. Alliance has no buff strip (the game
    // never sends alliance buffs) and no selection cursor, so those simply do not appear -- a missing row is not a
    // different layout.
    const float aS2_ = cat_fold(CTRL_ID, catOpen_[7]);   // eased 0..1 : the card, the clip and ry all ride this
    if (aS2_ > 0.0f) cat_panel(dev, hdrX, ry, hdrW, CAT_HEADER_ADV + pcFull_[2] * aS2_);
    if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Alliance", "Alliance"), catOpen_[7])) catOpen_[7] = !catOpen_[7];
    ROW_NEXT(42.0f)
    if (aS2_ > 0.0f) {
        const float cTop_ = ry;
        cat_fold_clip(dev, hdrX, cTop_, hdrW, pcFull_[2] * aS2_);
        // ---- General ----
        { const float bh2 = twoCol ? snap(48.0f) : snap(96.0f);
          ROW_BAND(bh2) (void)yo;
          const float yA = ry + (1.0f - ap) * snap(14.0f) + (twoCol ? (bh2 - snap(40.0f)) * 0.5f : snap(4.0f));
          const float yB = twoCol ? yA : yA + snap(48.0f);
          const float xB = twoCol ? col2X : coX;
          sepv(ry, bh2, true);
          row_toggle(dev, fo, mo, click, CTRL_ID, coX, yA, halfW, tr("Show", "Afficher"), &ui_config().allyShow, 40.0f, 150.0f);
          { const float lo = 0.50f, hi = 2.00f;   // alliance may go smaller than the party box
            char szbuf[16]; sprintf(szbuf, "%d%%", (int)(ui_config().box[1].scale * 100.0f + 0.5f));
            float v01 = (ui_config().box[1].scale - lo) / (hi - lo); v01 = v01 < 0.0f ? 0.0f : (v01 > 1.0f ? 1.0f : v01);
            if (row_slider(dev, fo, mo, CTRL_ID, xB, yB, halfW, tr("Size", "Taille"), szbuf, &v01)) {
                float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f;
                ui_config().box[1].scale = v < lo ? lo : (v > hi ? hi : v); } }
          ROW_NEXT(bh2)
        }
        // ---- Frame : the same rows as the party box, from the same function ----
        draw_frame_section(dev, fo, mo, click, ry, ri, e, bandX, bandW, coX, ctrlW,
                           1, &ui_config().allyThemeCopy,                // may simply follow the party theme
                           &ui_config().allyTheme, &ui_config().allyHue, &ui_config().allyLum, &ui_config().allyBoxAlpha,
                           &ui_config().border[1], nullptr, nullptr);    // no Cost box on an alliance row
        // ---- Content ----
        { const float bh2 = twoCol ? snap(48.0f) : snap(96.0f);   // what the gauges are, and what the badge says
          ROW_BAND(bh2) (void)yo;
          const float yA = ry + (1.0f - ap) * snap(14.0f) + (twoCol ? (bh2 - snap(40.0f)) * 0.5f : snap(4.0f));
          const float yB = twoCol ? yA : yA + snap(48.0f);
          const float xB = twoCol ? col2X : coX;
          sepv(ry, bh2, true);
          { int s = ui_config().gaugeStyle[1]; if (s < 0 || s > 7) s = 0;
            const char* sb[8] = { tr("Vial", "Fiole"), tr("Bars", "Barres"), tr("Segments", "Segments"), tr("Minimal", "Minimal"),
                                  tr("Sphere", "Sph\xC3\xA8re"), tr("Ring", "Anneau"), tr("Crystal", "Cristal"), tr("Text", "Texte") };
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, yA, halfW, tr("Gauge Style", "Style de jauge"), sb[s])) {
                ui_config().gaugeStyle[1] = wrap(s + d, 8); save_ui_config(); } }
          { int m = ui_config().jobBadge[1]; if (m < 0 || m > 3) m = 0;
            const char* jb[4] = { tr("Off", "Aucun"), tr("Main job", "Job principal"), tr("Main + Sub", "Principal + Sub"), tr("Icons", "Ic\xC3\xB4nes") };
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, xB, yB, halfW, tr("Job Badge", "Badge de job"), jb[m])) {
                ui_config().jobBadge[1] = wrap(m + d, 4); save_ui_config(); } }
          ROW_NEXT(bh2)
        }
        { const float bh2 = twoCol ? snap(46.0f) : snap(92.0f);   // the two dimensions of the bars
          ROW_BAND(bh2) (void)yo;
          const float yA = ry + (1.0f - ap) * snap(14.0f) + (twoCol ? (bh2 - snap(40.0f)) * 0.5f : snap(3.0f));
          const float yB = twoCol ? yA : yA + snap(46.0f);
          const float xB = twoCol ? col2X : coX;
          sepv(ry, bh2, true);
          { const float lo = 0.80f, hi = 1.80f; char hb[16]; sprintf(hb, "%d%%", (int)(ui_config().barHeight[1] * 100.0f + 0.5f));
            float v01 = (ui_config().barHeight[1] - lo) / (hi - lo); v01 = v01 < 0.0f ? 0.0f : (v01 > 1.0f ? 1.0f : v01);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, yA, halfW, tr("Bar Height", "Hauteur des barres"), hb, &v01)) {
                float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f;
                ui_config().barHeight[1] = v < lo ? lo : (v > hi ? hi : v); } }
          { const float lo = 0.70f, hi = 1.60f; char wb[16]; sprintf(wb, "%d%%", (int)(ui_config().barWidth[1] * 100.0f + 0.5f));
            float v01 = (ui_config().barWidth[1] - lo) / (hi - lo); v01 = v01 < 0.0f ? 0.0f : (v01 > 1.0f ? 1.0f : v01);
            if (row_slider(dev, fo, mo, CTRL_ID, xB, yB, halfW, tr("Bar Width", "Largeur des barres"), wb, &v01)) {
                float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f;
                ui_config().barWidth[1] = v < lo ? lo : (v > hi ? hi : v); } }
          ROW_NEXT(bh2)
        }
        { const bool hasBadge = (ui_config().jobBadge[1] != 0);
          const float bh2 = (twoCol || !hasBadge) ? snap(48.0f) : snap(96.0f);
          ROW_BAND(bh2) (void)yo;
          const float yA = ry + (1.0f - ap) * snap(14.0f) + ((twoCol || !hasBadge) ? (bh2 - snap(40.0f)) * 0.5f : snap(4.0f));
          const float yB = twoCol ? yA : yA + snap(48.0f);
          const float xB = twoCol ? col2X : coX;
          sepv(ry, bh2, hasBadge);   // an empty second half has no split to state
          { const float rowH = snap(38.0f); fo->begin(dev);   // what else an alliance row may show
            fo->draw_lc(dev, coX + snap(4.0f), yA + rowH * 0.5f, tr("Row extras", "Sur la ligne"), ts_label(), fa(C_TEXT), fa(C_STROKE), 1.0f);
            const float bbw = snap(96.0f), bgap = snap(8.0f), bbh = snap(34.0f), bty = yA + (rowH - bbh) * 0.5f;
            const float bx0 = coX + halfW - (2 * bbw + bgap);
            if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx0, bty, bbw, bbh, tr("Casts", "Sorts"), ui_config().cast[1] != 0)) { ui_config().cast[1] = !ui_config().cast[1]; save_ui_config(); }
            if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx0 + bbw + bgap, bty, bbw, bbh, tr("Distance", "Distance"), ui_config().dist[1] != 0)) { ui_config().dist[1] = !ui_config().dist[1]; save_ui_config(); } }
          if (hasBadge) {   // no badge, nothing to size
              const float lo = 0.60f, hi = 1.80f; char gb[16]; sprintf(gb, "%d%%", (int)(ui_config().badgeScale[1] * 100.0f + 0.5f));
              float v01 = (ui_config().badgeScale[1] - lo) / (hi - lo); v01 = v01 < 0.0f ? 0.0f : (v01 > 1.0f ? 1.0f : v01);
              if (row_slider(dev, fo, mo, CTRL_ID, xB, yB, halfW, tr("Badge Size", "Taille du badge"), gb, &v01)) {
                  float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f;
                  ui_config().badgeScale[1] = v < lo ? lo : (v > hi ? hi : v); }
          }
          ROW_NEXT(bh2)
        }
        cat_fold_end(dev, ry, cTop_, pcFull_[2], aS2_);
    }   // end Alliance

    // =========================================================== TEXT ===========================================================
    const float aS3_ = cat_fold(CTRL_ID, catOpen_[0]);   // eased 0..1 : the card, the clip and ry all ride this
    if (aS3_ > 0.0f) cat_panel(dev, hdrX, ry, hdrW, CAT_HEADER_ADV + pcFull_[3] * aS3_);
    if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Text", "Texte"), catOpen_[0])) catOpen_[0] = !catOpen_[0];
    ROW_NEXT(42.0f)
    if (aS3_ > 0.0f) {
        const float cTop_ = ry;
        cat_fold_clip(dev, hdrX, cTop_, hdrW, pcFull_[3] * aS3_);
        { ROW_BAND(56.0f)   // which box's text : Party / Alliance
            const float rowH = snap(40.0f), ty = ry + yo; fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, tr("Box", "Boîte"), ts_label(), fa(C_TEXT), fa(C_STROKE), 1.0f);
            const char* tlbl[2] = { tr("Party", "Groupe"), tr("Alliance", "Alliance") };
            const float bbw = snap(140.0f), bgap = snap(8.0f), bbh = snap(34.0f);
            const float bx0 = coX + ctrlW - (2 * bbw + bgap), bty = ty + (rowH - bbh) * 0.5f;
            for (int i = 0; i < 2; ++i) if (toggle_chip(dev, fo, mo, click, ctrl_uid_i(CTRL_ID, i), bx0 + i * (bbw + bgap), bty, bbw, bbh, tlbl[i], cfgTarget_ == i)) cfgTarget_ = i;
        }
        ROW_NEXT(56.0f)
        const int T = (cfgTarget_ < 0 || cfgTarget_ > 1) ? 0 : cfgTarget_;
        { ROW_BAND(52.0f)   // element selector -- Interface (TE_UI) is skipped : its font is Interface > Font
            int el = (cfgTextElem_ < 0 || cfgTextElem_ >= TE_COUNT || cfgTextElem_ == TE_UI) ? 0 : cfgTextElem_;
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Element", "Élément"), ui_text_elem_label(el))) {
                int n = wrap(el + d, TE_COUNT); if (n == TE_UI) n = wrap(n + d, TE_COUNT); cfgTextElem_ = n; }
        }
        ROW_NEXT(52.0f)
        {
            TextStyle& ts = ui_config().text[T][(cfgTextElem_ < 0 || cfgTextElem_ >= TE_COUNT) ? 0 : cfgTextElem_];
            { ROW_BAND(52.0f)   // Font face (0 = default)
                int fc = ts.face; if (fc < 0 || fc >= ui_font_count()) fc = 0;
                if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Font", "Police"), ui_font_label(fc))) { ts.face = wrap(fc + d, ui_font_count()); save_ui_config(); }
            }
            ROW_NEXT(52.0f)
            { ROW_BAND(46.0f)   // Size
                const float lo = 0.50f, hi = 2.00f; char b[16]; sprintf(b, "%d%%", (int)(ts.size * 100.0f + 0.5f));
                float v01 = (ts.size - lo) / (hi - lo); v01 = clampf(v01, 0.0f, 1.0f);
                if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Size", "Taille"), b, &v01)) { float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f; ts.size = v < lo ? lo : (v > hi ? hi : v); }
            }
            ROW_NEXT(46.0f)
            { ROW_BAND(46.0f)   // Outline width
                const float lo = 0.00f, hi = 2.00f; char b[16]; sprintf(b, "%d%%", (int)(ts.outline * 100.0f + 0.5f));
                float v01 = (ts.outline - lo) / (hi - lo); v01 = clampf(v01, 0.0f, 1.0f);
                if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Outline", "Contour"), b, &v01)) { float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f; ts.outline = v < lo ? lo : (v > hi ? hi : v); }
            }
            ROW_NEXT(46.0f)
            { ROW_BAND(52.0f)   // Bold / Italic / CAPS
                const float rowH = snap(40.0f), ty = ry + yo; fo->begin(dev);
                fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, tr("Style", "Style"), ts_label(), fa(C_TEXT), fa(C_STROKE), 1.0f);
                const float bbw = snap(80.0f), bgap = snap(8.0f), bbh = snap(34.0f), bty = ty + (rowH - bbh) * 0.5f, bx0 = coX + ctrlW - (3 * bbw + 2 * bgap);
                if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx0, bty, bbw, bbh, tr("Bold", "Gras"), ts.bold)) { ts.bold = !ts.bold; save_ui_config(); }
                if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx0 + bbw + bgap, bty, bbw, bbh, tr("Italic", "Ital."), ts.italic)) { ts.italic = !ts.italic; save_ui_config(); }
                if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx0 + 2 * (bbw + bgap), bty, bbw, bbh, tr("CAPS", "MAJ"), ts.upper)) { ts.upper = !ts.upper; save_ui_config(); }
            }
            ROW_NEXT(52.0f)
            { ROW_BAND(52.0f)   // Colour : Default / Custom + a live swatch
                const float rowH = snap(40.0f), ty = ry + yo; fo->begin(dev);
                fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, tr("Colour", "Couleur"), ts_label(), fa(C_TEXT), fa(C_STROKE), 1.0f);
                const float bbh = snap(34.0f), bty = ty + (rowH - bbh) * 0.5f, tgw = snap(96.0f), onx = coX + ctrlW - tgw;
                if (toggle_chip(dev, fo, mo, click, CTRL_ID, onx, bty, tgw, bbh, ts.colorOn ? tr("Custom", "Perso") : tr("Default", "Défaut"), ts.colorOn)) {
                    ts.colorOn = !ts.colorOn; if (ts.colorOn && (ts.color >> 24) == 0) ts.color |= 0xFF000000u; save_ui_config(); }
                if (ts.colorOn) {
                    const float pw = snap(58.0f), pxs = onx - snap(12.0f) - pw;
                    flat(dev, pxs, bty, pw * 0.5f, bbh, 0xFFFFFFFF); flat(dev, pxs + pw * 0.5f, bty, pw * 0.5f, bbh, 0xFF262A31);
                    flat(dev, pxs, bty, pw, bbh, ts.color); outline(dev, pxs, bty, pw, bbh, C_BORDER);
                }
            }
            ROW_NEXT(52.0f)
            if (ts.colorOn) {
                static const u32 PAL[] = {
                    0xFFFFFFFF,0xFFC8CDD6,0xFF8A93A2,0xFF3A4150,0xFFFF5A5A,0xFFFF9A4A,0xFFFFDC78,0xFFF2F25A,0xFF9BE85A,0xFF5ADC5A,0xFF5ADCB0,0xFF5AC8FF,
                    0xFF4F9DFF,0xFF6A7AF0,0xFFB07AF0,0xFFF07AE8,0xFFFF7AB0,0xFFE08585,0xFF86D36F,0xFFECC94A,0xFF7D9BF0,0xFFB58BF0,0xFF2C6AC4,0xFF141414 };
                const int NPAL = (int)(sizeof(PAL) / sizeof(PAL[0])), COLS = 12;
                { ROW_BAND(52.0f)
                    const float sqw = snap(20.0f), sg = snap(6.0f), gx = coX + snap(4.0f), gy = ry + yo - snap(1.0f);
                    for (int k = 0; k < NPAL; ++k) {
                        const float x = gx + (k % COLS) * (sqw + sg), y = gy + (k / COLS) * (sqw + sg);
                        const bool sel = ((ts.color & 0x00FFFFFF) == (PAL[k] & 0x00FFFFFF));
                        flat(dev, x, y, sqw, sqw, PAL[k]); outline(dev, x, y, sqw, sqw, sel ? 0xFFFFFFFF : C_BORDER);
                        if (inrect(mo, x, y, sqw, sqw) && click) { ts.color = (ts.color & 0xFF000000u) | (PAL[k] & 0x00FFFFFF); if ((ts.color >> 24) == 0) ts.color |= 0xFF000000u; save_ui_config(); }
                    }
                }
                ROW_NEXT(52.0f)
                CFG_COLOR_PICKER(&ts.color)
                { ROW_BAND(40.0f)
                    int a = (int)((ts.color >> 24) & 0xFFu); char vb[8]; sprintf(vb, "%d", a); float v01 = a / 255.0f;
                    if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, "A", vb, &v01)) {
                        int nv = (int)(v01 * 255.0f + 0.5f); if (nv < 0) nv = 0; if (nv > 255) nv = 255;
                        ts.color = (ts.color & 0x00FFFFFFu) | ((u32)nv << 24); }
                }
                ROW_NEXT(40.0f)
            }
        }
        cat_fold_end(dev, ry, cTop_, pcFull_[3], aS3_);
    }   // end Text
    // ======================================================= BUFFS =======================================================
    // Everything about the buff strip in ONE place -- how big, how many, over how many lines, and in what order.
    // Splitting them was a failure of the panel's own rule: Content groups by the OBJECT a setting acts on, and the
    // strip is one object. Its size lived under Content while its order lived in a section of its own, so answering
    // "how do my buffs show up" meant visiting two places.
    // It is also why this is a top-level section rather than a sub-section: the band is an EDITOR, and nesting it
    // one level deeper is exactly the third disclosure level the research says to avoid.
    const float aS4_ = cat_fold(CTRL_ID, pcBuffsOpen_);   // eased 0..1 : the card, the clip and ry all ride this
    if (aS4_ > 0.0f) cat_panel(dev, hdrX, ry, hdrW, CAT_HEADER_ADV + pcFull_[4] * aS4_);
    if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Buffs", "Buffs"), pcBuffsOpen_)) pcBuffsOpen_ = !pcBuffsOpen_;
    ROW_NEXT(42.0f)
    if (aS4_ > 0.0f) {
        const float cTop_ = ry;
        cat_fold_clip(dev, hdrX, cTop_, hdrW, pcFull_[4] * aS4_);
        { const float bh2 = twoCol ? snap(48.0f) : snap(96.0f);   // how big, and how many
          ROW_BAND(bh2) (void)yo;
          const float yA = ry + (1.0f - ap) * snap(14.0f) + (twoCol ? (bh2 - snap(40.0f)) * 0.5f : snap(4.0f));
          const float yB = twoCol ? yA : yA + snap(48.0f);
          const float xB = twoCol ? col2X : coX;
          sepv(ry, bh2, true);
          { const float lo = 0.40f, hi = 2.00f; char bzbuf[16]; sprintf(bzbuf, "%d%%", (int)(ui_config().buffScale * 100.0f + 0.5f));
            float v01 = (ui_config().buffScale - lo) / (hi - lo);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, yA, halfW, tr("Buff Size", "Taille des buffs"), bzbuf, &v01)) {
                float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f;
                ui_config().buffScale = v < lo ? lo : (v > hi ? hi : v); } }
          { static const int BM[] = { 0, 16, 20, 24, 32 };   // 0 = no buff strip at all
            int idx = 0; for (int k = 0; k < 5; ++k) if (BM[k] == ui_config().buffMax) { idx = k; break; }
            char bmv[28]; if (BM[idx] == 0) lstrcpynA(bmv, tr("None", "Aucun"), sizeof(bmv)); else sprintf(bmv, "%d", BM[idx]);
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, xB, yB, halfW, tr("Max Buffs", "Buffs max"), bmv)) {
                idx = wrap(idx + d, 5); ui_config().buffMax = BM[idx]; save_ui_config(); } }
          ROW_NEXT(bh2)
        }
        { ROW_BAND(48.0f)   // over how many lines
            const char* brl[2] = { tr("1 line", "1 ligne"), tr("2 lines", "2 lignes") };
            int bri = (ui_config().buffRows <= 1) ? 0 : 1;
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Buff Rows", "Lignes de buffs"), brl[bri])) {
                bri = wrap(bri + d, 2); ui_config().buffRows = bri + 1; save_ui_config(); }
            ROW_NEXT(48.0f)
        }
        // ---- the ORDER : the band itself. The strip draws its icons RIGHT-TO-LEFT from index 0, so the group at
        // position 1 ends up nearest the member's row. Ordering by GROUP (13 rows) rather than by status (624 of
        // them) is what keeps this configurable at all -- and it makes Max Buffs deliberate : the cut now falls on
        // whatever the user parked last instead of on whichever buff the server happened to send late. ----
        {
            // ================= THE STRIP IS THE CONTROL =================
            // What is configured is a horizontal band of icons, so the editor IS that band : the real atlas, filling
            // right-to-left exactly like the HUD strip hugging a member row. You grab a run and slide it. "Where will
            // Sneak end up" is answered by looking, not by translating a position number into a place on screen.
            //
            // ONE code path drives BOTH levels -- arranging the 13 groups, and arranging one group's own buffs.
            // Same geometry, same grab, same drop; entering a group changes the scale, never the gesture. That is
            // the whole design claim, so the two levels are deliberately not two blocks of code.
            static_assert(BG_COUNT <= UiConfig::BUFF_ORDER_N, "every BuffGroup must have a slot in the stored order");
            // ONE uid for the WHOLE drag, taken once. CTRL_ID is a __FILE__:__LINE__ hash, so writing it at
            // the grab, the hold, the release and the recovery would mint FOUR different ids : the grab would
            // latch one and every other call would ask about another. The drag then died after a frame AND
            // left g_slider latched forever, which also blocks every slider in the menu. This is the exact
            // trap config-panels.md warns about for controls in a loop -- it bites a multi-call control too.
            const int dragUid = CTRL_ID;
            const u32 bTex = buff_atlas_tex(dev);   // BORROWED : buff_atlas.cpp owns it and is the only place that releases it. 0 while its bounded retry has not landed -> the band draws its tints and stays usable, rather than holes.

            struct Run { int n; unsigned short ic[4]; float x, w, by, ly, iy; unsigned char grp; bool hid; bool faint; const char* lbl; };
            Run runs[UiConfig::BUFF_PIN_MAX]; int nRun = 0;   // sized by the cap, not by a number that was once big enough
            int slots[UiConfig::BUFF_ORDER_N];   // slots[k] = the buffOrder position the k-th visible run occupies
            unsigned short inner[UiConfig::BUFF_PIN_MAX]; int innerTotal = 0, innerN = 0;
            const bool atGroups = (bsInner_ < 0 || bsInner_ >= BG_COUNT);
            if (atGroups) {
                for (int i = 0; i < UiConfig::BUFF_ORDER_N && nRun < 64; ++i) {
                    const int g = ui_config().buffOrder[i];
                    unsigned short m[4]; int t = 0;
                    int k = buff_group_members(ui_config(), g, m, 4, &t, [](unsigned st) { return party().status_seen(st); });
                    const bool hid = ui_config().buff_group_hidden(g);
                    // NO EMPTY BLOCKS, EVER. A block that shows nothing says nothing -- and five or six groups
                    // hold nothing at any given time, because nobody is every job at once. So a group either
                    // shows what you actually carry, or (with All on) what it IS, previewed from its own
                    // catalogue and dimmed to say "not met yet". A group left out of the VIEW keeps its
                    // POSITION : the instant one of its buffs turns up, it lands where it was put.
                    bool faint = false;
                    if (k == 0) {   // not met yet -> preview it from its own catalogue, faint
                        k = buff_group_members(ui_config(), g, m, 4, &t, [](unsigned) { return true; });
                        faint = true;
                        if (k == 0) continue;   // a group the catalogue itself cannot fill has nothing to say
                    }
                    slots[nRun] = i;
                    Run& r = runs[nRun++];
                    r.n = k; for (int q = 0; q < k; ++q) r.ic[q] = m[q];
                    r.grp = (unsigned char)g; r.hid = hid; r.faint = faint;
                    r.lbl = tr(BUFF_GROUP_SHORT_EN[g], BUFF_GROUP_SHORT_FR[g]);
                }
            } else {
                const int g = bsInner_;
                const float icsProbe = (party_last_buff_icon_px() > 0.0f) ? party_last_buff_icon_px() : snap(22.0f);   // how many fit on one line, at the size they will really be
                // The group's OWN CATALOGUE, always -- not just what this session has met. Opening a group you
                // have never played and finding an empty band is not a view, it is a dead end : there is
                // nothing to arrange and no way to arrange it. What you carry draws normally, the rest draws
                // faint, and both are movable -- which is what "fill it yourself" has to mean.
                // Capped to one line's worth : past BUFF_PIN_MAX nothing can be stored anyway, and the last
                // line reports whatever is left.
                // WHAT YOU HAVE ACTUALLY MET (plus the curated ones), not the whole catalogue. Listing the
                // catalogue made every group complete in theory and unusable in practice: "Other" reached 214
                // entries, and a "+N more" line came back on Rolls and Songs. That line is worse than useless
                // here -- it names things you cannot reach, on the one screen whose entire job is to let you
                // arrange them. Filtered to what you meet, a group fits whole and the line never appears.
                // A group NEVER met still falls back to its catalogue, so opening it is not a dead end --
                // that was the reason the filter was dropped in the first place, and it is kept.
                // THE WHOLE CATALOGUE WHENEVER IT FITS -- and with the cap at 160 that is every group but one.
                // Abilities 142, Enhancing 59, Debuffs 49, Rolls 31, Geomancy 30, Songs 23 : all complete, all
                // arrangeable, seen or not, with no "+N more" line anywhere.
                // Other (238) is the single exception and stays filtered to what you have met : it is the
                // unclassified bin, and listing 238 statuses to announce a remainder helps nobody on the
                // screen whose whole job is arranging them.
                (void)icsProbe;
                const int capN = UiConfig::BUFF_PIN_MAX;
                innerN = buff_group_members(ui_config(), g, inner, capN, &innerTotal, [](unsigned) { return true; });
                if (innerTotal > capN) {   // does not fit whole -> the ones you have met
                    innerN = buff_group_members(ui_config(), g, inner, capN, &innerTotal,
                                                [](unsigned st) { return party().status_seen(st); });
                    if (innerN == 0)       // ... and never met either -> its catalogue, capped, so it is not a dead end
                        innerN = buff_group_members(ui_config(), g, inner, capN, &innerTotal, [](unsigned) { return true; });
                }
                for (int i = 0; i < innerN && nRun < 64; ++i) {
                    slots[0] = 0;
                    Run& r = runs[nRun++];
                    r.n = 1; r.ic[0] = inner[i]; r.grp = (unsigned char)g; r.hid = false;
                    r.faint = !party().status_seen(inner[i]);   // in the catalogue, not on anyone yet
                    r.hid   = buff_hidden_effective(ui_config(), inner[i]);   // hidden ONE buff, not the whole group
                    r.lbl = buff_status_name(inner[i]);   // named above its icon, exactly like a group : the band has to be readable at BOTH levels
                }
                // (No "#id" disambiguation any more : buff_group_members lists ONE entry per effect, so two
                //  tiles can no longer read the same word. The several ids the game gives one buff are the same
                //  buff, and you never carry two of them at once.)
            }
            if (bsSel_ >= nRun) bsSel_ = -1;
            if (bsDrag_ >= nRun) { bsDrag_ = -1; bsDrop_ = -1; }

            // ---- fit the band, WRAPPING it over as many lines as it needs ----
            // One line was a mistake with teeth : once thirteen blocks (or a group's whole catalogue, now that
            // every buff is named) outgrew the column, the overflow ran off the LEFT -- and the band is
            // right-aligned, so what vanished was the TAIL of the order. Other and Debuffs simply were not
            // there, and the page's stencil clip hid the fact. Wrapping is also the HUD's own idiom : the real
            // strip already runs on two rows.
            // Shrink the per-run PREVIEW before shrinking anything else : losing Watch's 4th icon costs nothing,
            // losing legibility costs everything. A hidden group keeps a narrow stub -- hiding a group must not
            // remove it from the editor that is the only place to bring it back.
            const float gapR = snap(6.0f);
            // THE SIZE THE GAME ACTUALLY DRAWS. 32 is the atlas CELL, not the rendered size : the strip draws at
            // buffIconH() * S, which follows Buff Size, the 1/2-row mode, the box scale and the screen. Asking
            // the party box (party_last_buff_icon_px) instead of recomputing that chain keeps ONE source of
            // truth -- and the editor then shows the icons at exactly the size you will see in game, which is
            // the entire premise of editing the band rather than a list. The fallback covers the first frames
            // before the party box has drawn (fresh load, or party hidden) ; the floor keeps a block big enough
            // to grab when someone runs a very small HUD.
            float ics = party_last_buff_icon_px();
            if (ics <= 0.0f) ics = snap(22.0f);
            if (ics < snap(14.0f)) ics = snap(14.0f);
            ics = snap(ics);
            float lsz = snap(10.5f); const float gapI = snap(2.0f), padR = snap(8.0f);   // padR : the tile's inner margin, so the icons never touch its border
            int   capI = atGroups ? 4 : 1;
            // ONE WIDTH FOR EVERY TILE, taken from the widest thing any of them has to hold -- its icons or
            // its name. Cells sized individually made a ragged band : the eye reads a row of equal cells as a
            // grid and a row of unequal ones as debris, and nothing here is worth the raggedness, since the
            // information is the ORDER, not the width. Uniform cells also mean the drop targets are uniform.
            float cellW = 0.0f;
            for (int pass = 0; pass < 10; ++pass) {
                cellW = 0.0f;
                for (int i = 0; i < nRun; ++i) {
                    const int k = runs[i].hid ? 0 : (runs[i].n < capI ? runs[i].n : capI);
                    float w = (k > 0) ? (k * ics + (k - 1) * gapI + 2 * padR) : (ics + 2 * padR);
                    if (runs[i].lbl) { const float lw = fo->measure(runs[i].lbl, lsz) + 2 * padR; if (lw > w) w = lw; }
                    if (w > cellW) cellW = w;
                }
                const float totalW = nRun * cellW + (nRun > 0 ? (nRun - 1) * gapR : 0.0f);
                if (totalW <= ctrlW * 3.0f) break;               // up to three lines : past that it stops being a band
                if (capI > 1) --capI;                            // first : fewer icons per tile
                else if (lsz > snap(8.0f)) lsz -= snap(0.5f);    // then the label
                else break;                                      // the icons are NEVER scaled : they are the game's own size
            }
            for (int i = 0; i < nRun; ++i) runs[i].w = cellW;

            // Entry 0 sits at the RIGHT edge and the band fills leftward, wrapping down. No position numbers
            // anywhere, because the position IS the position.
            const float rightX = coX + ctrlW;
            const float lineH = snap(ics + snap(42.0f));
            int nLines = 1;

            int mvFrom = -1, mvTo = -1;   // one move per frame, whatever produced it -- applied after the band

            // ---- toolbar : the selection, the NON-DRAG path, and the level ----
            // The arrows do exactly what the drag does. Not redundancy : a reorder that exists only as a drag
            // excludes anyone who cannot make that gesture precisely, so the click path ships beside it.
            { ROW_BAND(34.0f)
                const float ty = ry + yo + snap(3.0f), bh = snap(26.0f), aS = snap(15.0f), chipW2 = snap(74.0f);
                float bx = coX + ctrlW;
                bx -= aS; const float rArrX = bx; bx -= snap(10.0f) + aS; const float lArrX = bx;
                const float aY = ty + (bh - aS) * 0.5f;
                if (bsSel_ >= 0) {   // left = further from the member row (later), right = closer (earlier)
                    if (arrow_btn(dev, fo, mo, click, CTRL_ID, lArrX, aY, aS, "<") && bsSel_ + 1 < nRun) { mvFrom = bsSel_; mvTo = bsSel_ + 1; }
                    if (arrow_btn(dev, fo, mo, click, CTRL_ID, rArrX, aY, aS, ">") && bsSel_ > 0)        { mvFrom = bsSel_; mvTo = bsSel_ - 1; }
                }
                bx -= snap(12.0f);
                if (atGroups) {
                    if (bsSel_ >= 0) {
                        // An explicit way in. Clicking the selected block again also works, but a gesture nobody
                        // can see is not a feature -- the button is what makes the second level discoverable.
                        bx -= chipW2;
                        if (push_btn(dev, fo, mo, click, CTRL_ID, bx, ty, chipW2, bh, tr("Open", "Ouvrir"), 0)) { bsInner_ = runs[bsSel_].grp; bsSel_ = -1; }
                        bx -= snap(8.0f);
                        const bool hid = (bsSel_ >= 0 && bsSel_ < nRun) ? runs[bsSel_].hid : false;
                        bx -= chipW2;
                        if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx, ty, chipW2, bh,
                                        hid ? tr("Hidden", "Masque") : tr("Shown", "Affiche"), !hid)) {
                            ui_config().buff_group_toggle(runs[bsSel_].grp); save_ui_config();
                        }
                        bx -= snap(8.0f);
                    }
                } else {
                    if (bsSel_ >= 0) {   // the same chip, one level down : it hides ONE buff instead of the group
                        const bool bHid = runs[bsSel_].hid;   // NOT `bh` : that is the row's chip HEIGHT, and shadowing it here cost a build
                        bx -= chipW2;
                        if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx, ty, chipW2, bh,
                                        bHid ? tr("Hidden", "Masque") : tr("Shown", "Affiche"), !bHid)) {
                            ui_config().buff_status_toggle(buff_canon(runs[bsSel_].ic[0])); save_ui_config();   // the effect, not one of its ids
                        }
                        bx -= snap(8.0f);
                    }
                    bx -= chipW2;
                    // The mouse's thumb BACK button does the same thing -- that is what a hand reaches for to go up
                    // a level. Beside the button, never instead of it : a gesture with no visible control is not a
                    // feature, and a mouse without thumb buttons is not a broken mouse.
                    const bool wentBack = push_btn(dev, fo, mo, click, CTRL_ID, bx, ty, chipW2, bh, tr("Back", "Retour"), 0)
                                       || (mo && mo->backClicked);
                    if (wentBack) { bsInner_ = -1; bsSel_ = -1; }
                    bx -= snap(8.0f);
                }
                // The band carries no text of its own, so this line does the talking -- and it has to, because
                // NOTHING else told you the tiles were draggable or that a group opened. It names whatever is
                // under the pointer and, in the same breath, the gesture that acts on it : a hint that arrives
                // when the hand is already there is read, where a static caption over the band is furniture.
                // Hover wins over selection : the pointer is the more recent statement of intent.
                char lb[140];
                const int hov = (bsHot_ >= 0 && bsHot_ < nRun) ? bsHot_ : -1;   // last frame's, see bsHot_
                const int nam = (hov >= 0) ? hov : bsSel_;
                if (nam < 0) lstrcpynA(lb, atGroups ? tr("Drag a block to move it  -  double-click a group to open it",
                                                        "Glisse un bloc pour le deplacer  -  double-clic sur un groupe pour l'ouvrir")
                                                    : tr("Drag a buff to move it", "Glisse un buff pour le deplacer"), sizeof(lb));
                else {
                    const char* n2 = atGroups ? tr(BUFF_GROUP_EN[runs[nam].grp], BUFF_GROUP_FR[runs[nam].grp])
                                              : buff_status_name(runs[nam].ic[0]);
                    if (!n2) n2 = "?";
                    const char* fm = !atGroups        ? tr("%s  -  drag to move", "%s  -  glisse pour deplacer")
                                   : (hov < 0)        ? tr("%s  -  click again to open", "%s  -  clique encore pour ouvrir")
                                                      : tr("%s  -  drag to move, double-click to open",
                                                           "%s  -  glisse pour deplacer, double-clic pour ouvrir");
                    _snprintf(lb, sizeof(lb), fm, n2);
                    lb[sizeof(lb) - 1] = 0;   // _snprintf does not terminate on truncation
                }
                fo->begin(dev);
                fo->draw_lc(dev, coX + snap(4.0f), ty + bh * 0.5f, lb, ts_note(), fa(bsSel_ >= 0 ? C_TEXT : C_MUTE), fa(C_STROKE), 1.0f);
            }
            ROW_NEXT(34.0f)

            // ================= the band =================
            // Laid out in READING ORDER : right-to-left along a line, then down to the next. Position 1 is the
            // top-RIGHT block, against the member row -- the direction the HUD strip fills, continued exactly the
            // way its own two-row mode continues.
            {
                // A press LATCHES the tile ; it does not yet CARRY it. Until the pointer has travelled past the
                // threshold, the tile stays exactly where it is -- no lift, no gap, no reflow. Detaching on the
                // press meant a plain click to open a group looked like the start of a move : the tile jumped to
                // the cursor and the band opened a hole, then everything snapped back. A drag should have to be
                // meant. (`bsMoved_` is the same travel test that already decides click-vs-drag on release, so
                // the two can never disagree about what the gesture was.)
                const bool carrying = (bsDrag_ >= 0 && bsMoved_ != 0);
                // ---- the order the blocks are laid out in : with the carried one lifted out and re-inserted ----
                int vis[UiConfig::BUFF_PIN_MAX], nv = 0;
                if (carrying && bsDrop_ >= 0) {
                    for (int k = 0; k < nRun; ++k) if (k != bsDrag_) { if (nv == bsDrop_) vis[nv++] = bsDrag_; vis[nv++] = k; }
                    if (nv <= bsDrop_) vis[nv++] = bsDrag_;
                } else for (int k = 0; k < nRun; ++k) vis[nv++] = k;

                // ---- wrap : a block that no longer fits on this line starts the next one ----
                float tx[UiConfig::BUFF_PIN_MAX]; int tline[UiConfig::BUFF_PIN_MAX];
                { float x = rightX; int ln = 0;
                  for (int k = 0; k < nv; ++k) {
                      const int i = vis[k];
                      if (x - runs[i].w < coX && x < rightX) { ++ln; x = rightX; }
                      x -= runs[i].w; tx[i] = x; tline[i] = ln; x -= gapR;
                  }
                  nLines = ln + 1;
                }
                const float bandH = lineH * nLines;
                ROW_BAND(bandH) (void)yo;
                const float sy0 = ry;

                // A drag we think we own but the LATCH no longer does : the page was closed, or the tab switched,
                // mid-drag and ctrl_release_drag() freed it under us. Without this the block stays lifted and the
                // `bsDrag_ < 0` gate below blocks every future grab -- one badly-timed close and the editor is dead
                // for the session. Recover ; never latch a transient into a state.
                if (bsDrag_ >= 0 && !ctrl_drag_active(dragUid)) { bsDrag_ = -1; bsDrop_ = -1; bsEnter_ = 0; }

                // ---- position each block, smoothing the reflow only while something is being carried ----
                // BOTH axes are eased. X alone was half a reflow : a block pushed onto the next line slid sideways
                // and then teleported down, which is the one moment the eye most needs to follow it. With Y eased
                // too, a block that wraps travels there, and dragging between lines reads as one continuous motion.
                // While something is CARRIED the tiles move on springs : they lead, overshoot a hair and settle,
                // which is what makes a reflow read as things being pushed aside rather than redrawn elsewhere.
                // Idle they snap (ease at a huge speed) -- there is nothing being manipulated, and a rebuilt list
                // must not animate one tile into another tile's place.
                { for (int k = 0; k < nRun; ++k) {
                      if (carrying) {
                          runs[k].x  = spring(dragUid, 1000 + k, tx[k]);
                          runs[k].by = spring(dragUid, 3000 + k, sy0 + tline[k] * lineH);
                      } else {
                          runs[k].x  = ease(dragUid, 1000 + k, tx[k], 1000.0f);
                          runs[k].by = ease(dragUid, 3000 + k, sy0 + tline[k] * lineH, 1000.0f);
                      }
                  } }

                // ---- grab : remember WHERE in the block you took hold of it ----
                int hot = -1;
                for (int i = 0; i < nRun; ++i) if (inrect(mo, runs[i].x, sy0 + tline[i] * lineH, runs[i].w, lineH)) { hot = i; break; }
                bsHot_ = hot;   // for the hint line, which is drawn ABOVE the band and so runs a frame ahead of it
                // The hover lift rides ONE spring, not one per tile : only one tile is hovered at a time, and a
                // slot each would have cost 224 of the page's 1024 (never recycled) and left later controls
                // snapping. bsLift_ remembers WHICH tile it belongs to, so leaving the band still fades out on
                // the tile you left rather than cutting.
                const bool liftable = (hot >= 0 && !carrying && bsDrag_ < 0);
                if (liftable) bsLift_ = hot;
                if (bsLift_ >= nRun) bsLift_ = -1;
                const float liftAmt = ease(dragUid, 5000, liftable ? 1.0f : 0.0f, 16.0f);
                if (bsDrag_ < 0 && hot >= 0 && ctrl_drag_begin(dragUid, mo, true)) {
                    bsEnter_ = (bsSel_ == hot) ? 1 : 0;   // a press on the ALREADY selected block means "go inside", if it turns out not to be a drag
                    bsDrag_ = hot; bsDrop_ = hot; bsSel_ = hot;
                    bsGrabDX_ = mo->x - runs[hot].x; bsGrabX_ = mo->x; bsGrabY_ = mo->y; bsMoved_ = 0;
                }

                // ---- where would it land ? ----
                // Decided against a layout that does NOT depend on the answer : the other tiles, in reading order,
                // at the uniform cellW. Using the DRAWN positions was a feedback loop -- they have already moved
                // because of the previous frame's answer, and with wrapping a change of answer moves tiles between
                // LINES, which changes the very positions the next answer is read from. That is what made a drag
                // from the third line feel like it was arguing with you.
                // The rule is now the one a hand expects : the slot the carried tile's CENTRE is over. Uniform
                // cells make that a division instead of a search, and the presentation (the gap opening) is left
                // free to follow the answer without ever feeding it.
                const int perLine = (int)((ctrlW + gapR) / (cellW + gapR)) > 0 ? (int)((ctrlW + gapR) / (cellW + gapR)) : 1;
                if (bsDrag_ >= 0 && ctrl_drag_active(dragUid) && mo) {
                    // Travel first : it is what promotes a latched press into a real carry. 6px is the stickiness --
                    // enough that a click cannot trip it, little enough that a move never feels resisted.
                    { const float dx0 = mo->x - bsGrabX_, dy0 = mo->y - bsGrabY_;
                      const float ax = dx0 < 0 ? -dx0 : dx0, ay = dy0 < 0 ? -dy0 : dy0;
                      if (ax > snap(6.0f) || ay > snap(6.0f)) bsMoved_ = 1; }
                    if (bsMoved_) {
                        const float cx = (mo->x - bsGrabDX_) + cellW * 0.5f;   // the carried tile's centre, not the pointer's
                        int col = (int)((rightX - cx + cellW * 0.5f) / (cellW + gapR));   // 0 = the rightmost slot
                        if (col < 0) col = 0; if (col >= perLine) col = perLine - 1;
                        int line = (int)((mo->y - sy0) / lineH);
                        const int maxLine = (nRun - 1) / perLine;
                        if (line < 0) line = 0; if (line > maxLine) line = maxLine;
                        int at = line * perLine + col;
                        if (at < 0) at = 0; if (at > nRun - 1) at = nRun - 1;
                        bsDrop_ = at;
                    }
                }

                // ---- release ----
                if (bsDrag_ >= 0 && ctrl_drag_end(dragUid, mo)) {
                    const int from = bsDrag_, to = bsDrop_;
                    bsDrag_ = -1; bsDrop_ = -1;
                    if (bsMoved_ && to >= 0 && to != from) { mvFrom = from; mvTo = to; }
                    else if (!bsMoved_ && bsEnter_ && atGroups) { bsInner_ = runs[from].grp; bsSel_ = -1; }   // a click on an already-selected group : go inside
                    bsEnter_ = 0; bsMoved_ = 0;
                }
                // the carried block leaves the layout and follows the pointer
                // `carrying` was computed at the TOP of the frame ; the release handler just above may have set
                // bsDrag_ to -1 since. Without the second test that is runs[-1].x -- a write off the front of the
                // array, on every release, into whatever the stack put there.
                if (carrying && mo && bsDrag_ >= 0) runs[bsDrag_].x = mo->x - bsGrabDX_;

                // ---- PASS 1 : the TILES (colour-quad state) ----
                // Every block is a real cell : a rounded panel with a border, drawn always -- not only when hovered.
                // Floating icons over a bare band left the eye to infer where one group ended and the next began,
                // from a gap. A bordered tile states it. The BORDER carries the group's tint, which is why the
                // separate tint rule underneath is gone : one identity mark per tile, not two.
                // (NOTHING is drawn in the slot the carried tile came out of, and that was tried twice.
                //  A full-size panel there read as the tile's own background left behind ; insetting it and
                //  thinning it to an outline did not help, because the problem was never the styling -- any
                //  mark sitting still at the place you just lifted from reads as something you failed to
                //  pick up. The gap alone is the right answer : the neighbours shifting into it is the
                //  feedback, which is what was asked for in the first place, and an empty space is the one
                //  thing that cannot be mistaken for an object.)
                for (int k = 0; k < nv; ++k) {
                    const int i = vis[k];
                    const bool lift = carrying && (i == bsDrag_);
                    // HOVER LIFTS the tile a few pixels. Motion is the cheapest way to say "this one is loose" --
                    // it costs no pixels of a band that has none to spare, and it reads before any label does.
                    // Eased, because a tile that jumps on hover reads as a glitch rather than as an invitation.
                    const float hov2 = (i == bsLift_) ? liftAmt : 0.0f;
                    const float by = lift ? ((mo ? mo->y : sy0) - lineH * 0.5f) : (runs[i].by - hov2 * snap(3.0f));   // carried : centred on the pointer ; the rest : their eased slot
                    const float ty2 = by + snap(3.0f), th2 = lineH - snap(6.0f);   // inset, so wrapped lines do not touch
                    runs[i].ly = by + snap(12.0f);
                    runs[i].iy = runs[i].ly + snap(9.0f);
                    const u32 tint = runs[i].hid ? C_MUTE : buff_group_tint(runs[i].grp);
                    u32 ft, fb, br; float bw2;
                    if (lift) {   // carried : opaque and raised, so it reads as held ABOVE the band
                        // ... and it takes ITS OWN COLOUR with it. This used to go neutral grey with the generic
                        // accent on the edge, so the moment you picked a tile up it stopped being Songs or Rolls
                        // and became a slab -- the group's identity stayed behind in the band while the thing in
                        // your hand had none. Opaque is what says "held above" ; the tint is what says WHAT is
                        // held, and the two are not the same job. So: the tint washed into a dark opaque base,
                        // and the edge at full strength.
                        drop_shadow(dev, runs[i].x, ty2, runs[i].w, th2, snap(6.0f), 110);
                        ft = lerpc(0xFF232C33u, tint, 0.22f);
                        fb = lerpc(0xFF161C22u, tint, 0.13f);
                        br = (tint & 0x00FFFFFF) | 0xFF000000u;
                        bw2 = snap(1.6f);
                    } else if (i == bsSel_) {
                        ft = (C_ACCENT & 0x00FFFFFF) | 0x3C000000u; fb = (C_ACCENT & 0x00FFFFFF) | 0x18000000u;
                        br = C_ACCENTHI; bw2 = snap(1.5f);
                    } else if (i == hot && !carrying) {
                        drop_shadow(dev, runs[i].x, ty2, runs[i].w, th2, snap(5.0f), (u32)(70.0f * hov2));   // the lift needs a shadow or it is just a nudge
                        ft = 0x40202830u; fb = 0x40161C22u; br = (tint & 0x00FFFFFF) | 0xAA000000u; bw2 = snap(1.3f);
                    } else if (runs[i].hid) {
                        // HIDDEN has to read before the icon does, not after. A dim icon alone was too polite :
                        // it looked like "not met yet", which is a different thing entirely. So the tile itself
                        // goes flat and colourless -- no group tint on the edge, nothing to catch the eye --
                        // and a slash is struck across it below. Three signals, none of which needs reading.
                        ft = 0x14090C10u; fb = 0x14060809u;
                        br = (C_MUTE & 0x00FFFFFF) | 0x30000000u;
                        bw2 = snap(1.0f);
                    } else {
                        ft = 0x2A141A1Fu; fb = 0x2A0E1317u;
                        br = (tint & 0x00FFFFFF) | (runs[i].faint ? 0x38000000u : 0x70000000u);   // not met yet -> a fainter edge, same hue
                        bw2 = snap(1.2f);
                    }
                    rpanel(dev, runs[i].x, ty2, runs[i].w, th2, snap(8.0f), ft, fb, br, bw2);
                    // ---- the GRIP : six dots in the dead strip under the icons, faded in with the hover. ----
                    // The lift says something is loose ; the grip says what to do about it. It is the one mark
                    // every desktop already uses for "take hold of this", so it needs no legend, and it lives in
                    // space the tile was not using -- between the bottom of the icons and its own border.
                    if (hov2 > 0.01f) {
                        const u32 ga = (u32)(190.0f * hov2) << 24;
                        const u32 gc = (buff_group_tint(runs[i].grp) & 0x00FFFFFF) | ga;
                        const float d = snap(1.8f), sp = snap(3.6f);
                        const float gx = runs[i].x + runs[i].w * 0.5f - (sp + d * 0.5f), gy = by + ics + snap(29.0f);
                        for (int r2 = 0; r2 < 2; ++r2)
                            for (int c3 = 0; c3 < 3; ++c3)
                                rrect_fill(dev, snap(gx + c3 * sp), snap(gy + r2 * sp), d, d, d * 0.5f, gc, gc);
                    }
                    if (lift) rrect_top(dev, runs[i].x, ty2, runs[i].w, snap(2.0f), snap(8.0f), (tint & 0x00FFFFFF) | 0xB4000000u, (tint & 0x00FFFFFF) | 0x00000000u);   // the top light is the group's colour too
                }
                // ---- PASS 2 : every icon, under ONE texture bind for the whole band ----
                if (bTex) {
                    dTexQuadState(dev, bTex, false);
                    for (int i = 0; i < nRun; ++i) {
                        const int kk = runs[i].n < capI ? runs[i].n : capI;
                        for (int q = 0; q < kk; ++q) {
                            // MIRRORED inside the block, like the band itself : rank 0 is the RIGHTMOST icon of its
                            // own block. Drawing the preview left-to-right inside a band that reads right-to-left put
                            // Haste on the wrong side of Refresh -- the block contradicted the strip it lives in.
                            float au, av, u0, v0; buff_cell_uv(runs[i].ic[q], au, av, u0, v0);
                            const float rw = kk * ics + (kk - 1) * gapI;                 // the icon run's own width
                            const float r0 = runs[i].x + (runs[i].w - rw) * 0.5f;         // centred in the block
                            const float ix = r0 + rw - (q + 1) * ics - q * gapI;          // rank 0 still the RIGHTMOST of the run
                            // HIDDEN draws dim, never absent : the editor is the only place to bring it back, so
                            // removing it from the editor would be a one-way door. Faint = in the catalogue but
                            // not met yet. Both fade in the VERTEX colour -- a MANAGED texture's alpha
                            // mis-samples as opaque while a zone loads (reference/d3d8-rendering.md).
                            const u32 tc = runs[i].faint ? fa(0x70FFFFFFu) : 0xFFFFFFFFu;   // hidden is handled by the scrim below, not here
                            tquad(dev, snap(ix), snap(runs[i].iy), ics, ics, u0, u0 + au, v0, v0 + av, tc, tc);
                        }
                    }
                    dSetTex(dev, 0, 0); cs(dev);   // never leave a bound texture / textured state for the next control (rule 8)
                }
                // ---- PASS 3 : the names, ONE font pass, each centred over its own block ----
                fo->begin(dev);
                for (int i = 0; i < nRun; ++i) {
                    if (!runs[i].lbl) continue;
                    const u32 c2 = runs[i].hid ? C_MUTE
                                 : (i == bsSel_ || (carrying && i == bsDrag_)) ? C_ACCENTHI
                                 : (runs[i].faint ? C_MUTE : C_DIM);
                    fo->draw_c(dev, runs[i].x + runs[i].w * 0.5f, runs[i].ly, runs[i].lbl, lsz, fa(c2), fa(C_STROKE), 1.0f);
                }
                // ---- PASS 4 : HIDDEN -- a scrim over the whole finished tile, then the slash on top ----
                // Knocking the icon back alone was never going to be enough : it left a fully lit NAME and a
                // fully lit tile around a dim picture, so the eye read the tile as live. The scrim goes over
                // everything the tile has drawn -- surface, icon, name -- so the whole cell dims as ONE thing,
                // which is what "off" looks like. Drawn last, for the same reason.
                // The icon survives underneath as a ghost, which is the point : you must still be able to tell
                // WHAT you hid, because this editor is the only place to bring it back.
                for (int i = 0; i < nRun; ++i) {
                    if (!runs[i].hid) continue;
                    const float by2 = runs[i].ly - snap(12.0f) + snap(3.0f), th3 = lineH - snap(6.0f);
                    rrect_fill(dev, runs[i].x, by2, runs[i].w, th3, snap(8.0f), 0xC4070A0Du, 0xCC040608u);
                    const float in2 = snap(7.0f);
                    // RED and thick : the slash is the only mark that has to survive a glance across a band of
                    // thirty tiles, so it gets the one saturated colour the menu reserves for "no" (C_CLOSEHOV's
                    // family, the close button's red) and a stroke wide enough to read at icon scale.
                    seg_soft(dev, runs[i].x + in2, by2 + th3 - in2, runs[i].x + runs[i].w - in2, by2 + in2,
                             snap(3.4f), fa(0xE6E0555Fu));
                }
                ROW_NEXT(bandH)
            }


            // ---- the one thing the band cannot say about itself ----
            { ROW_BAND(24.0f)
                char lb2[140];
                if (!atGroups && innerTotal > innerN) { _snprintf(lb2, sizeof(lb2), tr("+%d more, in the game's order", "+%d autres, dans l'ordre du jeu"), innerTotal - innerN); lb2[sizeof(lb2) - 1] = 0; }
                else lstrcpynA(lb2, tr("The rightmost block sits against the member row",
                                       "Le bloc le plus a droite est contre la ligne du membre"), sizeof(lb2));
                fo->begin(dev);
                fo->draw_lc(dev, coX + snap(4.0f), ry + yo + snap(12.0f), lb2, ts_micro(), fa(C_MUTE), fa(C_STROKE), 1.0f);
            }
            ROW_NEXT(24.0f)

            // ---- apply the single move of this frame, whatever produced it (arrow or drop) ----
            if (mvFrom >= 0 && mvTo >= 0 && mvFrom != mvTo) {
                strip_apply_move(atGroups, bsInner_, mvFrom, mvTo, inner, innerN, slots, nRun);
                bsSel_ = mvTo;   // the selection FOLLOWS what you moved, so a second press keeps moving the same thing
            }
        }
        cat_fold_end(dev, ry, cTop_, pcFull_[4], aS4_);
    }   // end Buffs

    #undef ROW_BAND
    #undef ROW_NEXT
}

} // namespace aio
