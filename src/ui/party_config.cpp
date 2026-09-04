// party_config.cpp -- the Party / Alliance module's settings panel (//aio config -> "Groupe / Alliance").
//
// Split into three sibling sub-sections (no outer module wrapper, like every other module) :
//   Party    -- its box theme (appearance) + all party-box settings.
//   Alliance -- a "Same as Party / Custom" theme toggle (+ its own theme if Custom) + alliance-box settings.
//   Text     -- per-element typography, with a small Party/Alliance selector for which box's text to edit.
// The per-box control blocks are written out for each group (index 0 = Party, 1 = Alliance) rather than shared via a
// helper : a helper called twice would reuse each control's CTRL_ID (source-line uid) and the two groups' sliders
// would collide. Distinct source lines -> distinct uids -> no collision.
#include "ui/config_page.h"
#include "ui/config_controls.h"   // shared toolkit : cat_header / row_slider / toggle_chip / row_selector + palette + g_fade
#include "ui/config_rows.h"       // ROW_BAND / ROW_NEXT row-layout macros (shared with config_page.cpp)
#include "model/ui_config.h"      // ui_config(), save/reset, TE_* enum, TextStyle
#include "model/buff_groups.h"    // BuffGroup + BUFF_GROUP_EN/FR : the party buff strip's display groups
#include "model/party_state.h"    // party().status_seen : which statuses have actually turned up this session
#include "ui/buff_atlas.h"       // the ONE shared status-icon atlas + buff_cell_uv (borrowed, never released here)
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
    if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Party", "Groupe"), catOpen_[1])) catOpen_[1] = !catOpen_[1];
    ROW_NEXT(42.0f)
    if (catOpen_[1]) {
        // Show : master on/off for the main PARTY box (independent of Alliance below).
        ROW_TOGGLE_G(CTRL_ID, tr("Show", "Afficher"), ui_config().partyShow, 52.0f, 40.0f, 150.0f)
        // Size (canonical : right after Show). Party floor 100% : it must cover the native block.
        { ROW_BAND(46.0f)
            const float lo = 1.00f, hi = 2.00f;
            char szbuf[16]; sprintf(szbuf, "%d%%", (int)(ui_config().box[0].scale * 100.0f + 0.5f));
            float v01 = (ui_config().box[0].scale - lo) / (hi - lo); v01 = v01 < 0.0f ? 0.0f : (v01 > 1.0f ? 1.0f : v01);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Size", "Taille"), szbuf, &v01)) {
                float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f;
                ui_config().box[0].scale = v < lo ? lo : (v > hi ? hi : v); }
        }
        ROW_NEXT(46.0f)
        { ROW_BAND(46.0f)   // Transparency (canonical : before the theme rows)
            const float transp = 1.0f - ui_config().skinBoxAlpha; char b[16]; sprintf(b, "%d%%", (int)(transp * 100.0f + 0.5f));
            float v01 = clampf(transp, 0.0f, 1.0f);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Transparency", "Transparence"), b, &v01)) {
                ui_config().skinBoxAlpha = 1.0f - v01; }
          ROW_NEXT(46.0f)
        }
        // ---- Appearance : the PARTY box theme (procedural families = colour grid ; FFXI = game theme numbers). ----
        { ROW_BAND(52.0f)   // Box Theme (family)
          const int fam = window_theme_family(ui_config().skinTheme), var = window_theme_variant(ui_config().skinTheme);
          if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Box Theme", "Thème de cadre"), box_family_name(fam))) {
              ui_config().skinTheme = window_theme_index(wrap(fam + d, box_family_count()), var); save_ui_config(); } }
        ROW_NEXT(52.0f)
        if (window_theme_family(ui_config().skinTheme) != 0) { ROW_BAND(48.0f)   // Custom colour toggle
            const float rowH = snap(38.0f), ty = ry + yo; fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, tr("Custom colour", "Couleur perso"), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
            const float bbw = snap(112.0f), bbh = snap(34.0f), bx2 = coX + ctrlW - bbw, bty = ty + (rowH - bbh) * 0.5f;
            const bool on = ui_config().skinHue != 0;
            if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx2, bty, bbw, bbh, on ? tr("On", "Oui") : tr("Off", "Non"), on)) {
                ui_config().skinHue = on ? 0u : (box_hue_color(window_theme_variant(ui_config().skinTheme)) | 0xFF000000u); save_ui_config(); }
            ROW_NEXT(48.0f)
        }
        if (window_theme_family(ui_config().skinTheme) != 0 && ui_config().skinHue != 0) {
            CFG_COLOR_PICKER(&ui_config().skinHue)
        } else
        {   // variant grid : FFXI -> theme-number chips ; procedural family -> hue swatches (click to pick)
          const int fam = window_theme_family(ui_config().skinTheme), var = window_theme_variant(ui_config().skinTheme);
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
              if (inrect(mo, xk, yk, cw, ch) && click) { ui_config().skinTheme = window_theme_index(fam, k); save_ui_config(); }
          }
          ROW_NEXT(slotH)
        }
        if (window_theme_family(ui_config().skinTheme) != 0)   // Luminosity
        { ROW_BAND(46.0f)
            float v01 = (ui_config().skinLum + 1.0f) * 0.5f; v01 = clampf(v01, 0.0f, 1.0f);
            const int pct = (int)(ui_config().skinLum * 100.0f + (ui_config().skinLum >= 0.0f ? 0.5f : -0.5f));
            char b[16]; sprintf(b, "%+d%%", pct);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Luminosity", "Luminosité"), b, &v01)) {
                ui_config().skinLum = v01 * 2.0f - 1.0f; }
          ROW_NEXT(46.0f)
        }
        // ---- Party box settings (index 0) ----
        { ROW_BAND(52.0f)   // Buff Size (party only : the game sends no buffs for alliances)
            const float lo = 0.40f, hi = 2.00f;
            char bzbuf[16]; sprintf(bzbuf, "%d%%", (int)(ui_config().buffScale * 100.0f + 0.5f));
            float v01 = (ui_config().buffScale - lo) / (hi - lo);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Buff Size", "Taille des buffs"), bzbuf, &v01)) {
                float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f;
                ui_config().buffScale = v < lo ? lo : (v > hi ? hi : v); }
        }
        ROW_NEXT(52.0f)
        { ROW_BAND(40.0f)   // Max Buffs
            static const int BM[] = { 0, 16, 20, 24, 32 };   // 0 = no buff strip at all
            int idx = 0; for (int k = 0; k < 5; ++k) if (BM[k] == ui_config().buffMax) { idx = k; break; }
            char bmv[28]; if (BM[idx] == 0) lstrcpynA(bmv, tr("None", "Aucun"), sizeof(bmv)); else sprintf(bmv, "%d", BM[idx]);
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Max Buffs", "Buffs max"), bmv)) {
                idx = wrap(idx + d, 5); ui_config().buffMax = BM[idx]; save_ui_config(); }
        }
        ROW_NEXT(40.0f)
        { ROW_BAND(52.0f)   // Buff Rows : 1 or 2
            const char* brl[2] = { tr("1 line", "1 ligne"), tr("2 lines", "2 lignes") };
            int bri = (ui_config().buffRows <= 1) ? 0 : 1;
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Buff Rows", "Lignes de buffs"), brl[bri])) {
                bri = wrap(bri + d, 2); ui_config().buffRows = bri + 1; save_ui_config(); }
        }
        ROW_NEXT(52.0f)
        // ---- sub-section : BUFF ORDER. The strip draws its icons RIGHT-TO-LEFT from index 0, so the group at
        // position 1 ends up nearest the member's row. Ordering by GROUP (13 rows) rather than by status (624 of
        // them) is what keeps this configurable at all -- and it makes Max Buffs deliberate : the cut now falls on
        // whatever the user parked last instead of on whichever buff the server happened to send late. ----
        if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Buff order", "Ordre des buffs"), buffOrderOpen_)) buffOrderOpen_ = !buffOrderOpen_;
        ROW_NEXT(42.0f)
        if (buffOrderOpen_) {
            // ================= THE STRIP IS THE CONTROL =================
            // What is configured is a horizontal band of icons, so the editor IS that band : the real atlas, filling
            // right-to-left exactly like the HUD strip hugging a member row. You grab a run and slide it. "Where will
            // Sneak end up" is answered by looking, not by translating a position number into a place on screen.
            //
            // ONE code path drives BOTH levels -- arranging the 13 groups, and arranging one group's own buffs.
            // Same geometry, same grab, same drop; entering a group changes the scale, never the gesture. That is
            // the whole design claim, so the two levels are deliberately not two blocks of code.
            static_assert(BG_COUNT <= (int)(sizeof(((ConfigPage*)0)->bgAll_) / sizeof(bool)), "bgAll_ must be at least BG_COUNT wide");
            // ONE uid for the WHOLE drag, taken once. CTRL_ID is a __FILE__:__LINE__ hash, so writing it at
            // the grab, the hold, the release and the recovery would mint FOUR different ids : the grab would
            // latch one and every other call would ask about another. The drag then died after a frame AND
            // left g_slider latched forever, which also blocks every slider in the menu. This is the exact
            // trap config-panels.md warns about for controls in a loop -- it bites a multi-call control too.
            const int dragUid = CTRL_ID;
            const u32 bTex = buff_atlas_tex(dev);   // BORROWED : buff_atlas.cpp owns it and is the only place that releases it. 0 while its bounded retry has not landed -> the band draws its tints and stays usable, rather than holes.

            struct Run { int n; unsigned short ic[4]; float x, w; unsigned char grp; bool hid; };
            Run runs[64]; int nRun = 0;
            int slots[UiConfig::BUFF_ORDER_N]; int nEmpty = 0;   // slots[k] = the buffOrder position the k-th visible run occupies
            unsigned short inner[256]; int innerTotal = 0, innerN = 0;
            const bool atGroups = (bsInner_ < 0 || bsInner_ >= BG_COUNT);
            if (atGroups) {
                for (int i = 0; i < UiConfig::BUFF_ORDER_N && nRun < 64; ++i) {
                    const int g = ui_config().buffOrder[i];
                    unsigned short m[4]; int t = 0;
                    const int k = buff_group_members(ui_config(), g, m, 4, &t, [](unsigned st) { return party().status_seen(st); });
                    const bool hid = ui_config().buff_group_hidden(g);
                    // A group with nothing in it has nothing to show and nothing to order -- and there are
                    // usually five or six of them (you are not every job at once). Leaving them in the band as
                    // blank stubs is the noise this editor exists to remove, so they come out of the VIEW while
                    // keeping their POSITION : the instant one of their buffs turns up, it lands where it was
                    // put. The chip on the last line brings them back when you want to pre-arrange one.
                    if (k == 0 && !hid && !bsShowEmpty_) { ++nEmpty; continue; }
                    slots[nRun] = i;
                    Run& r = runs[nRun++];
                    r.n = k; for (int q = 0; q < k; ++q) r.ic[q] = m[q];
                    r.grp = (unsigned char)g; r.hid = hid;
                }
            } else {
                const int g = bsInner_;
                innerN = bgAll_[g]
                    ? buff_group_members(ui_config(), g, inner, 256, &innerTotal, [](unsigned) { return true; })
                    : buff_group_members(ui_config(), g, inner, UiConfig::BUFF_PIN_MAX, &innerTotal,
                                         [](unsigned st) { return party().status_seen(st); });
                if (innerN > 64) innerN = 64;   // the band is one line ; the rest keeps the game's order
                for (int i = 0; i < innerN; ++i) { slots[0] = 0; Run& r = runs[nRun++]; r.n = 1; r.ic[0] = inner[i]; r.grp = (unsigned char)g; r.hid = false; }
            }
            if (bsSel_ >= nRun) bsSel_ = -1;
            if (bsDrag_ >= nRun) { bsDrag_ = -1; bsDrop_ = -1; }

            // ---- fit the whole band on ONE line ----
            // Shrink the per-run PREVIEW before shrinking the icons : losing Watch's 4th icon costs nothing,
            // losing legibility costs everything. A hidden group keeps a narrow stub -- hiding a group must not
            // remove it from the editor that is the only place to bring it back.
            const float gapR = snap(6.0f);
            float ics = snap(18.0f); const float gapI = snap(2.0f), padR = snap(4.0f);
            int   capI = atGroups ? 4 : 1;
            for (int pass = 0; pass < 6; ++pass) {
                float totalW = 0.0f;
                for (int i = 0; i < nRun; ++i) {
                    const int k = runs[i].hid ? 0 : (runs[i].n < capI ? runs[i].n : capI);
                    runs[i].w = (k > 0) ? (k * ics + (k - 1) * gapI + 2 * padR) : snap(12.0f);
                    totalW += runs[i].w + (i ? gapR : 0.0f);
                }
                if (totalW <= ctrlW) break;
                if (capI > 1) --capI;                            // first : fewer icons per run
                else if (ics > snap(11.0f)) ics -= snap(2.0f);   // then, and only then : smaller icons
                else break;                                      // still over -> the tail clips ; never scale to mush
            }
            // Entry 0 sits at the RIGHT edge and the band fills leftward. No position numbers anywhere, because
            // the position IS the position.
            const float rightX = coX + ctrlW;
            { float x = rightX;
              for (int i = 0; i < nRun; ++i) { x -= runs[i].w; runs[i].x = x; x -= gapR; } }

            int mvFrom = -1, mvTo = -1;   // one move per frame, whatever produced it -- applied after the band

            // ---- toolbar : the selection, the NON-DRAG path, and the level ----
            // The arrows do exactly what the drag does. Not redundancy : a reorder that exists only as a drag
            // excludes anyone who cannot make that gesture precisely, so the click path ships beside it.
            { ROW_BAND(34.0f)
                const float ty = ry + yo + snap(3.0f), bh = snap(26.0f), aS = snap(15.0f), chipW2 = snap(74.0f);
                float bx = coX + ctrlW;
                if (!atGroups) {   // the catalogue switch : the band shows what you carry ; All reaches the rest
                    bx -= chipW2;
                    if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx, ty, chipW2, bh,
                                    bgAll_[bsInner_] ? tr("All", "Tous") : tr("Seen", "Vus"), bgAll_[bsInner_]))
                        { bgAll_[bsInner_] = !bgAll_[bsInner_]; bsSel_ = -1; }
                    bx -= snap(8.0f);
                }
                bx -= aS; const float rArrX = bx; bx -= snap(10.0f) + aS; const float lArrX = bx;
                const float aY = ty + (bh - aS) * 0.5f;
                if (bsSel_ >= 0) {   // left = further from the member row (later), right = closer (earlier)
                    if (arrow_btn(dev, fo, mo, click, CTRL_ID, lArrX, aY, aS, "<") && bsSel_ + 1 < nRun) { mvFrom = bsSel_; mvTo = bsSel_ + 1; }
                    if (arrow_btn(dev, fo, mo, click, CTRL_ID, rArrX, aY, aS, ">") && bsSel_ > 0)        { mvFrom = bsSel_; mvTo = bsSel_ - 1; }
                }
                bx -= snap(12.0f);
                if (atGroups) {
                    if (bsSel_ >= 0) {
                        const bool hid = runs[bsSel_].hid;
                        bx -= chipW2;
                        if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx, ty, chipW2, bh,
                                        hid ? tr("Hidden", "Masque") : tr("Shown", "Affiche"), !hid)) {
                            ui_config().buff_group_toggle(runs[bsSel_].grp); save_ui_config();
                        }
                        bx -= snap(8.0f);
                    }
                } else {
                    bx -= chipW2;
                    if (push_btn(dev, fo, mo, click, CTRL_ID, bx, ty, chipW2, bh, tr("Back", "Retour"), 0)) { bsInner_ = -1; bsSel_ = -1; }
                    bx -= snap(8.0f);
                }
                // The band carries no text of its own : this line names what is selected, and says how to go deeper.
                char lb[140];
                if (bsSel_ < 0) lstrcpynA(lb, atGroups ? tr("Drag a block to move it", "Glisse un bloc pour le deplacer")
                                                       : tr("Drag a buff to move it", "Glisse un buff pour le deplacer"), sizeof(lb));
                else if (atGroups) {
                    const int g = runs[bsSel_].grp;
                    _snprintf(lb, sizeof(lb), "%s  -  %s", tr(BUFF_GROUP_EN[g], BUFF_GROUP_FR[g]),
                              tr("click again to arrange inside", "clique encore pour ranger l'interieur")); lb[sizeof(lb) - 1] = 0;
                } else { const char* n2 = buff_status_name(runs[bsSel_].ic[0]); lstrcpynA(lb, n2 ? n2 : "?", sizeof(lb)); }
                fo->begin(dev);
                fo->draw_lc(dev, coX + snap(4.0f), ty + bh * 0.5f, lb, snap(12.5f), fa(bsSel_ >= 0 ? C_TEXT : C_MUTE), fa(C_STROKE), 1.0f);
            }
            ROW_NEXT(34.0f)

            // ================= the band =================
            { const float bandH = snap(46.0f);
              ROW_BAND(46.0f)
                const float sy = ry + yo + (snap(40.0f) - bandH) * 0.5f;
                const float iy = sy + snap(9.0f), uy = iy + ics + snap(5.0f);
                // A drag we think we own but the LATCH no longer does : the page was closed, or the tab
                // switched, mid-drag and ctrl_release_drag() freed it under us. Without this the run stays
                // lifted and the `bsDrag_ < 0` gate below blocks every future grab -- one badly-timed close
                // and the editor is dead for the session. Recover, do not latch a transient into a state.
                if (bsDrag_ >= 0 && !ctrl_drag_active(dragUid)) { bsDrag_ = -1; bsDrop_ = -1; bsEnter_ = 0; }
                int hot = -1;
                for (int i = 0; i < nRun; ++i) if (inrect(mo, runs[i].x, sy, runs[i].w, bandH)) { hot = i; break; }
                // ---- grab ----
                if (bsDrag_ < 0 && hot >= 0 && ctrl_drag_begin(dragUid, mo, true)) {
                    bsEnter_ = (bsSel_ == hot) ? 1 : 0;   // a press on the ALREADY selected run means "go inside", if it turns out not to be a drag
                    bsDrag_ = hot; bsDrop_ = hot; bsSel_ = hot;
                }
                // ---- hold : where would it land ? ----
                // Re-target as soon as the pointer passes a neighbour's MIDPOINT, not on release -- the band has to
                // answer "here" while you are still holding, or the drop is a guess. The band paints right-to-left,
                // so a pointer LEFT of a run's middle means LATER in the order.
                if (bsDrag_ >= 0 && ctrl_drag_active(dragUid) && mo) {
                    int at = bsDrop_;
                    for (int i = 0; i < nRun; ++i)
                        if (mo->x >= runs[i].x && mo->x < runs[i].x + runs[i].w) { at = (mo->x < runs[i].x + runs[i].w * 0.5f) ? i + 1 : i; break; }
                    if (mo->x >= rightX) at = 0;                                  // past the right edge -> first
                    if (nRun > 0 && mo->x < runs[nRun - 1].x) at = nRun;          // past the left edge  -> last
                    if (at != bsDrop_) bsEnter_ = 0;                              // it moved : this was a drag, not a second click
                    bsDrop_ = at;
                }
                // ---- release ----
                if (bsDrag_ >= 0 && ctrl_drag_end(dragUid, mo)) {
                    const int from = bsDrag_; int to = bsDrop_;
                    bsDrag_ = -1; bsDrop_ = -1;
                    if (to >= 0 && to != from && to != from + 1) {
                        if (to > from) --to;                    // removing `from` shifts everything after it down
                        mvFrom = from; mvTo = to;
                    } else if (bsEnter_ && atGroups) {          // a click on an already-selected group : go inside
                        bsInner_ = runs[from].grp; bsSel_ = -1;
                    }
                    bsEnter_ = 0;
                }
                // ---- PASS 1 : surfaces, selection, tint rules, the insertion caret (colour-quad state) ----
                for (int i = 0; i < nRun; ++i) {
                    const bool lift = (i == bsDrag_);
                    const float dy = lift ? -snap(4.0f) : 0.0f;
                    if (lift) drop_shadow(dev, runs[i].x, sy + dy, runs[i].w, bandH, snap(4.0f), 70);
                    if (i == bsSel_)   rrect_fill(dev, runs[i].x, sy + dy, runs[i].w, bandH, snap(7.0f), (C_ACCENT & 0x00FFFFFF) | 0x3C000000u, (C_ACCENT & 0x00FFFFFF) | 0x18000000u);
                    else if (i == hot) rrect_fill(dev, runs[i].x, sy + dy, runs[i].w, bandH, snap(7.0f), 0x18FFFFFFu, 0x0CFFFFFFu);
                    const int k = runs[i].hid ? 0 : (runs[i].n < capI ? runs[i].n : capI);
                    const float uw = (k > 0) ? (k * ics + (k - 1) * gapI) : snap(6.0f);
                    flat(dev, snap(runs[i].x + runs[i].w - padR - uw), snap(uy + dy), uw, snap(2.0f), fa(runs[i].hid ? C_MUTE : buff_group_tint(runs[i].grp)));
                }
                if (bsDrag_ >= 0 && bsDrop_ >= 0 && nRun > 0) {
                    const float cx3 = (bsDrop_ >= nRun) ? (runs[nRun - 1].x - gapR * 0.5f)
                                                        : (runs[bsDrop_].x + runs[bsDrop_].w + gapR * 0.5f);
                    flat(dev, snap(cx3 - snap(1.5f)), sy + snap(4.0f), snap(3.0f), bandH - snap(8.0f), fa(C_ACCENTHI));
                }
                // ---- PASS 2 : every icon, under ONE texture bind for the whole band ----
                if (bTex) {
                    dTexQuadState(dev, bTex, false);
                    for (int i = 0; i < nRun; ++i) {
                        if (runs[i].hid) continue;
                        const float y0 = iy + ((i == bsDrag_) ? -snap(4.0f) : 0.0f);
                        const int k = runs[i].n < capI ? runs[i].n : capI;
                        for (int q = 0; q < k; ++q) {
                            // MIRRORED inside the run, like the band itself : rank 0 is the RIGHTMOST icon of its
                            // own block, not the leftmost. Drawing the preview left-to-right inside a band that
                            // reads right-to-left put Haste on the wrong side of Refresh -- the run contradicted
                            // the strip it lives in, and the config then disagreed with the HUD it is editing.
                            float au, av, u0, v0; buff_cell_uv(runs[i].ic[q], au, av, u0, v0);
                            const float ix = runs[i].x + runs[i].w - padR - (q + 1) * ics - q * gapI;
                            tquad(dev, snap(ix), snap(y0), ics, ics,
                                  u0, u0 + au, v0, v0 + av, 0xFFFFFFFFu, 0xFFFFFFFFu);
                        }
                    }
                    dSetTex(dev, 0, 0); cs(dev);   // never leave a bound texture / textured state for the next control (rule 8)
                }
                // ---- PASS 3 : hidden groups, as a mark rather than icons ----
                for (int i = 0; i < nRun; ++i) if (runs[i].hid)
                    rrect_fill(dev, snap(runs[i].x + runs[i].w - snap(9.0f)), snap(iy + ics * 0.35f), snap(6.0f), snap(6.0f), snap(3.0f), fa(C_MUTE), fa(C_MUTE));
            }
            ROW_NEXT(46.0f)

            // ---- the one thing the band cannot say about itself ----
            { ROW_BAND(24.0f)
                char lb2[140];
                if (!atGroups && innerTotal > innerN) { _snprintf(lb2, sizeof(lb2), tr("+%d more, in the game's order", "+%d autres, dans l'ordre du jeu"), innerTotal - innerN); lb2[sizeof(lb2) - 1] = 0; }
                else if (atGroups && nEmpty > 0) { _snprintf(lb2, sizeof(lb2), tr("%d empty groups, position kept", "%d groupes vides, place conservee"), nEmpty); lb2[sizeof(lb2) - 1] = 0; }
                else lstrcpynA(lb2, tr("The rightmost block sits against the member row",
                                       "Le bloc le plus a droite est contre la ligne du membre"), sizeof(lb2));
                fo->begin(dev);
                fo->draw_lc(dev, coX + snap(4.0f), ry + yo + snap(12.0f), lb2, snap(11.5f), fa(C_MUTE), fa(C_STROKE), 1.0f);
                if (atGroups && (nEmpty > 0 || bsShowEmpty_)) {
                    const float cw3 = snap(74.0f), ch3 = snap(20.0f);
                    if (toggle_chip(dev, fo, mo, click, CTRL_ID, coX + ctrlW - cw3, ry + yo + snap(2.0f), cw3, ch3,
                                    tr("Empty", "Vides"), bsShowEmpty_)) { bsShowEmpty_ = !bsShowEmpty_; bsSel_ = -1; }
                }
            }
            ROW_NEXT(24.0f)

            // ---- apply the single move of this frame, whatever produced it (arrow or drop) ----
            if (mvFrom >= 0 && mvTo >= 0 && mvFrom != mvTo) {
                strip_apply_move(atGroups, bsInner_, mvFrom, mvTo, inner, innerN, slots, nRun);
                bsSel_ = mvTo;   // the selection FOLLOWS what you moved, so a second press keeps moving the same thing
            }
        }
        { ROW_BAND(52.0f)   // Cursor Size
            const float lo = 0.50f, hi = 2.00f;
            char czbuf[16]; sprintf(czbuf, "%d%%", (int)(ui_config().cursorScale * 100.0f + 0.5f));
            float v01 = (ui_config().cursorScale - lo) / (hi - lo); v01 = v01 < 0.0f ? 0.0f : (v01 > 1.0f ? 1.0f : v01);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Cursor Size", "Taille du curseur"), czbuf, &v01)) {
                float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f;
                ui_config().cursorScale = v < lo ? lo : (v > hi ? hi : v); }
        }
        ROW_NEXT(52.0f)
        { ROW_BAND(46.0f)   // Bar Height
            const float lo = 0.80f, hi = 1.80f;
            char hb[16]; sprintf(hb, "%d%%", (int)(ui_config().barHeight[0] * 100.0f + 0.5f));
            float v01 = (ui_config().barHeight[0] - lo) / (hi - lo); v01 = v01 < 0.0f ? 0.0f : (v01 > 1.0f ? 1.0f : v01);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Bar Height", "Hauteur des barres"), hb, &v01)) {
                float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f;
                ui_config().barHeight[0] = v < lo ? lo : (v > hi ? hi : v); }
        }
        ROW_NEXT(46.0f)
        { ROW_BAND(46.0f)   // Bar Width
            const float lo = 0.80f, hi = 1.50f;
            char wb[16]; sprintf(wb, "%d%%", (int)(ui_config().barWidth[0] * 100.0f + 0.5f));
            float v01 = (ui_config().barWidth[0] - lo) / (hi - lo); v01 = v01 < 0.0f ? 0.0f : (v01 > 1.0f ? 1.0f : v01);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Bar Width", "Largeur des barres"), wb, &v01)) {
                float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f;
                ui_config().barWidth[0] = v < lo ? lo : (v > hi ? hi : v); }
        }
        ROW_NEXT(46.0f)
        { ROW_BAND(52.0f)   // Gauge Style
            int s = ui_config().gaugeStyle[0]; if (s < 0 || s > 7) s = 0;
            const char* sb[8] = { tr("Vial", "Fiole"), tr("Bars", "Barres"), tr("Segments", "Segments"), tr("Minimal", "Minimal"),
                                  tr("Sphere", "Sphère"), tr("Ring", "Anneau"), tr("Crystal", "Cristal"), tr("Text", "Texte") };
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Gauge Style", "Style de jauge"), sb[s])) {
                ui_config().gaugeStyle[0] = wrap(s + d, 8); save_ui_config(); }
        }
        ROW_NEXT(52.0f)
        { ROW_BAND(52.0f)   // Animation (party only)
            const float rowH = snap(40.0f), ty = ry + yo; fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, tr("Animation", "Animation"), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
            const float bbw = snap(112.0f), bgap = snap(8.0f), bbh = snap(34.0f);
            const float bx0 = coX + ctrlW - (2 * bbw + bgap), bty = ty + (rowH - bbh) * 0.5f;
            if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx0, bty, bbw, bbh, ui_config().animHP ? tr("HP on", "HP oui") : tr("HP off", "HP non"), ui_config().animHP)) { ui_config().animHP = !ui_config().animHP; save_ui_config(); }
            if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx0 + bbw + bgap, bty, bbw, bbh, ui_config().animTP ? tr("TP on", "TP oui") : tr("TP off", "TP non"), ui_config().animTP)) { ui_config().animTP = !ui_config().animTP; save_ui_config(); }
        }
        ROW_NEXT(52.0f)
        { ROW_BAND(52.0f)   // Job Badge
            int m = ui_config().jobBadge[0]; if (m < 0 || m > 3) m = 2;
            const char* jb[4] = { tr("Off", "Aucun"), tr("Main job", "Job principal"), tr("Main + Sub", "Principal + Sub"), tr("Icons", "Icônes") };
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Job Badge", "Badge de job"), jb[m])) {
                ui_config().jobBadge[0] = wrap(m + d, 4); save_ui_config(); }
        }
        ROW_NEXT(52.0f)
        if (ui_config().jobBadge[0] != 0) {
          { ROW_BAND(46.0f)   // Badge Size
            const float lo = 0.50f, hi = 2.00f;
            char gb[16]; sprintf(gb, "%d%%", (int)(ui_config().badgeScale[0] * 100.0f + 0.5f));
            float v01 = (ui_config().badgeScale[0] - lo) / (hi - lo); v01 = v01 < 0.0f ? 0.0f : (v01 > 1.0f ? 1.0f : v01);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Badge Size", "Taille du badge"), gb, &v01)) {
                float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f;
                ui_config().badgeScale[0] = v < lo ? lo : (v > hi ? hi : v); }
          }
          ROW_NEXT(46.0f)
        }
        ROW_TOGGLE_G(CTRL_ID, tr("Casts", "Sorts"), ui_config().cast[0], 52.0f, 40.0f, 112.0f)   // Casts
        ROW_TOGGLE_G(CTRL_ID, tr("Distance", "Distance"), ui_config().dist[0], 52.0f, 40.0f, 112.0f)   // Distance
        // ---- Distance-zone colours : the yalms number is tinted by cast-range zone (Close < 10' / Normal 10'..20.8' / Far >= 20.8'). ----
        {
            struct ZoneCol { const char* en; const char* fr; unsigned* col; };
            ZoneCol zc[3] = {
                { "Close",  "Proche", &ui_config().distColClose  },
                { "Normal", "Normal", &ui_config().distColNormal },
                { "Far",    "Loin",   &ui_config().distColFar     },
            };
            for (int zi = 0; zi < 3; ++zi) {
                unsigned& F = *zc[zi].col;
                { ROW_BAND(52.0f)   // label + live swatch
                    const float rowH = snap(40.0f), ty = ry + yo; fo->begin(dev);
                    fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, tr(zc[zi].en, zc[zi].fr), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
                    const float bbh = snap(34.0f), bty = ty + (rowH - bbh) * 0.5f, pw = snap(58.0f), pxs = coX + ctrlW - pw;
                    flat(dev, pxs, bty, pw, bbh, F | 0xFF000000u); outline(dev, pxs, bty, pw, bbh, C_BORDER);
                } ROW_NEXT(52.0f)
                CFG_COLOR_PICKER_I(&F, zi)   // per-item uids : all three expand on THIS line, see the macro
            }
        }
        { ROW_BAND(52.0f)   // Border : the box frame (+ the floating Cost box)
            const float rowH = snap(40.0f), ty = ry + yo; fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, tr("Border", "Bordure"), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
            const float bbw = snap(112.0f), bgap = snap(8.0f), bbh = snap(34.0f), bty = ty + (rowH - bbh) * 0.5f;
            const float bx0 = coX + ctrlW - (2 * bbw + bgap);
            if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx0, bty, bbw, bbh, tr("Box", "Boîte"), ui_config().border[0])) { ui_config().border[0] = !ui_config().border[0]; save_ui_config(); }
            if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx0 + bbw + bgap, bty, bbw, bbh, tr("Cost box", "Boîte coût"), ui_config().borderCost)) { ui_config().borderCost = !ui_config().borderCost; save_ui_config(); }
        }
        ROW_NEXT(52.0f)
    }   // end Party

    // ========================================================= ALLIANCE =========================================================
    if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Alliance", "Alliance"), catOpen_[7])) catOpen_[7] = !catOpen_[7];
    ROW_NEXT(42.0f)
    if (catOpen_[7]) {
        // Show : master on/off for the two ALLIANCE boxes (independent of the Party box above).
        ROW_TOGGLE_G(CTRL_ID, tr("Show", "Afficher"), ui_config().allyShow, 52.0f, 40.0f, 150.0f)
        // ---- Alliance box settings (index 1 ; no buffs / animation -- alliances get none) ----
        { ROW_BAND(46.0f)   // Size (alliance : 50%..200%) -- canonical : right after Show
            const float lo = 0.50f, hi = 2.00f;
            char szbuf[16]; sprintf(szbuf, "%d%%", (int)(ui_config().box[1].scale * 100.0f + 0.5f));
            float v01 = (ui_config().box[1].scale - lo) / (hi - lo); v01 = v01 < 0.0f ? 0.0f : (v01 > 1.0f ? 1.0f : v01);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Size", "Taille"), szbuf, &v01)) {
                float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f;
                ui_config().box[1].scale = v < lo ? lo : (v > hi ? hi : v); }
        }
        ROW_NEXT(46.0f)
        // ---- Theme : follow the Party box theme, or give the alliance boxes their OWN (procedural themes fully). ----
        ROW_CHOICE_G(CTRL_ID, tr("Theme", "Thème"), ui_config().allyThemeCopy, tr("Same as Party", "Comme Party"), tr("Custom", "Perso"), 52.0f, 40.0f, 150.0f)
        if (!ui_config().allyThemeCopy) {
        { ROW_BAND(52.0f)   // Box Theme (family)
          const int fam = window_theme_family(ui_config().allyTheme), var = window_theme_variant(ui_config().allyTheme);
          if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Box Theme", "Thème de cadre"), box_family_name(fam))) {
              ui_config().allyTheme = window_theme_index(wrap(fam + d, box_family_count()), var); save_ui_config(); } }
        ROW_NEXT(52.0f)
        if (window_theme_family(ui_config().allyTheme) != 0) { ROW_BAND(48.0f)   // Custom colour toggle
            const float rowH = snap(38.0f), ty = ry + yo; fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, tr("Custom colour", "Couleur perso"), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
            const float bbw = snap(112.0f), bbh = snap(34.0f), bx2 = coX + ctrlW - bbw, bty = ty + (rowH - bbh) * 0.5f;
            const bool on = ui_config().allyHue != 0;
            if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx2, bty, bbw, bbh, on ? tr("On", "Oui") : tr("Off", "Non"), on)) {
                ui_config().allyHue = on ? 0u : (box_hue_color(window_theme_variant(ui_config().allyTheme)) | 0xFF000000u); save_ui_config(); }
            ROW_NEXT(48.0f)
        }
        if (window_theme_family(ui_config().allyTheme) != 0 && ui_config().allyHue != 0) {
            CFG_COLOR_PICKER(&ui_config().allyHue)
        } else
        {   // variant grid
          const int fam = window_theme_family(ui_config().allyTheme), var = window_theme_variant(ui_config().allyTheme);
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
              if (inrect(mo, xk, yk, cw, ch) && click) { ui_config().allyTheme = window_theme_index(fam, k); save_ui_config(); }
          }
          ROW_NEXT(slotH)
        }
        if (window_theme_family(ui_config().allyTheme) != 0)   // Luminosity
        { ROW_BAND(46.0f)
            float v01 = (ui_config().allyLum + 1.0f) * 0.5f; v01 = clampf(v01, 0.0f, 1.0f);
            const int pct = (int)(ui_config().allyLum * 100.0f + (ui_config().allyLum >= 0.0f ? 0.5f : -0.5f));
            char b[16]; sprintf(b, "%+d%%", pct);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Luminosity", "Luminosité"), b, &v01)) {
                ui_config().allyLum = v01 * 2.0f - 1.0f; }
          ROW_NEXT(46.0f)
        }
        { ROW_BAND(46.0f)   // Transparency
            const float transp = 1.0f - ui_config().allyBoxAlpha; char b[16]; sprintf(b, "%d%%", (int)(transp * 100.0f + 0.5f));
            float v01 = clampf(transp, 0.0f, 1.0f);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Transparency", "Transparence"), b, &v01)) {
                ui_config().allyBoxAlpha = 1.0f - v01; }
          ROW_NEXT(46.0f)
        }
        }   // end custom alliance theme (!allyThemeCopy)
        { ROW_BAND(46.0f)   // Bar Height
            const float lo = 0.80f, hi = 1.80f;
            char hb[16]; sprintf(hb, "%d%%", (int)(ui_config().barHeight[1] * 100.0f + 0.5f));
            float v01 = (ui_config().barHeight[1] - lo) / (hi - lo); v01 = v01 < 0.0f ? 0.0f : (v01 > 1.0f ? 1.0f : v01);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Bar Height", "Hauteur des barres"), hb, &v01)) {
                float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f;
                ui_config().barHeight[1] = v < lo ? lo : (v > hi ? hi : v); }
        }
        ROW_NEXT(46.0f)
        { ROW_BAND(46.0f)   // Bar Width
            const float lo = 0.80f, hi = 1.50f;
            char wb[16]; sprintf(wb, "%d%%", (int)(ui_config().barWidth[1] * 100.0f + 0.5f));
            float v01 = (ui_config().barWidth[1] - lo) / (hi - lo); v01 = v01 < 0.0f ? 0.0f : (v01 > 1.0f ? 1.0f : v01);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Bar Width", "Largeur des barres"), wb, &v01)) {
                float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f;
                ui_config().barWidth[1] = v < lo ? lo : (v > hi ? hi : v); }
        }
        ROW_NEXT(46.0f)
        { ROW_BAND(52.0f)   // Gauge Style
            int s = ui_config().gaugeStyle[1]; if (s < 0 || s > 7) s = 0;
            const char* sb[8] = { tr("Vial", "Fiole"), tr("Bars", "Barres"), tr("Segments", "Segments"), tr("Minimal", "Minimal"),
                                  tr("Sphere", "Sphère"), tr("Ring", "Anneau"), tr("Crystal", "Cristal"), tr("Text", "Texte") };
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Gauge Style", "Style de jauge"), sb[s])) {
                ui_config().gaugeStyle[1] = wrap(s + d, 8); save_ui_config(); }
        }
        ROW_NEXT(52.0f)
        { ROW_BAND(52.0f)   // Job Badge
            int m = ui_config().jobBadge[1]; if (m < 0 || m > 3) m = 2;
            const char* jb[4] = { tr("Off", "Aucun"), tr("Main job", "Job principal"), tr("Main + Sub", "Principal + Sub"), tr("Icons", "Icônes") };
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Job Badge", "Badge de job"), jb[m])) {
                ui_config().jobBadge[1] = wrap(m + d, 4); save_ui_config(); }
        }
        ROW_NEXT(52.0f)
        if (ui_config().jobBadge[1] != 0) {
          { ROW_BAND(46.0f)   // Badge Size
            const float lo = 0.50f, hi = 2.00f;
            char gb[16]; sprintf(gb, "%d%%", (int)(ui_config().badgeScale[1] * 100.0f + 0.5f));
            float v01 = (ui_config().badgeScale[1] - lo) / (hi - lo); v01 = v01 < 0.0f ? 0.0f : (v01 > 1.0f ? 1.0f : v01);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Badge Size", "Taille du badge"), gb, &v01)) {
                float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f;
                ui_config().badgeScale[1] = v < lo ? lo : (v > hi ? hi : v); }
          }
          ROW_NEXT(46.0f)
        }
        ROW_TOGGLE_G(CTRL_ID, tr("Casts", "Sorts"), ui_config().cast[1], 52.0f, 40.0f, 112.0f)   // Casts
        ROW_TOGGLE_G(CTRL_ID, tr("Distance", "Distance"), ui_config().dist[1], 52.0f, 40.0f, 112.0f)   // Distance
        ROW_TOGGLE_G(CTRL_ID, tr("Border", "Bordure"), ui_config().border[1], 52.0f, 40.0f, 112.0f)   // Border
    }   // end Alliance

    // =========================================================== TEXT ===========================================================
    if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Text", "Texte"), catOpen_[0])) catOpen_[0] = !catOpen_[0];
    ROW_NEXT(42.0f)
    if (catOpen_[0]) {
        { ROW_BAND(56.0f)   // which box's text : Party / Alliance
            const float rowH = snap(40.0f), ty = ry + yo; fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, tr("Box", "Boîte"), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
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
                fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, tr("Style", "Style"), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
                const float bbw = snap(80.0f), bgap = snap(8.0f), bbh = snap(34.0f), bty = ty + (rowH - bbh) * 0.5f, bx0 = coX + ctrlW - (3 * bbw + 2 * bgap);
                if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx0, bty, bbw, bbh, tr("Bold", "Gras"), ts.bold)) { ts.bold = !ts.bold; save_ui_config(); }
                if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx0 + bbw + bgap, bty, bbw, bbh, tr("Italic", "Ital."), ts.italic)) { ts.italic = !ts.italic; save_ui_config(); }
                if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx0 + 2 * (bbw + bgap), bty, bbw, bbh, tr("CAPS", "MAJ"), ts.upper)) { ts.upper = !ts.upper; save_ui_config(); }
            }
            ROW_NEXT(52.0f)
            { ROW_BAND(52.0f)   // Colour : Default / Custom + a live swatch
                const float rowH = snap(40.0f), ty = ry + yo; fo->begin(dev);
                fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, tr("Colour", "Couleur"), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
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
    }   // end Text
    ry += snap(10.0f);

    #undef ROW_BAND
    #undef ROW_NEXT
}

} // namespace aio
