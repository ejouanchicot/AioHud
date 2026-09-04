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
            // bgOpen_/bgAll_/bgSel_ are indexed by BuffGroup. trkCatOpen_ and relOpen_ each overflowed into their
            // neighbour exactly this way once their enum outgrew the array -- silently, because nothing checked.
            static_assert(BG_COUNT <= (int)(sizeof(((ConfigPage*)0)->bgOpen_) / sizeof(bool)), "bgOpen_ must be at least BG_COUNT wide");
            static_assert(BG_COUNT <= (int)(sizeof(((ConfigPage*)0)->bgAll_)  / sizeof(bool)), "bgAll_ must be at least BG_COUNT wide");
            static_assert(BG_COUNT <= (int)(sizeof(((ConfigPage*)0)->bgSel_)  / sizeof(short)), "bgSel_ must be at least BG_COUNT wide");
            { ROW_BAND(30.0f)   // the one thing a number alone cannot say : which END of the strip position 1 is
                fo->begin(dev);
                fo->draw_lc(dev, coX + snap(4.0f), ry + yo + snap(15.0f),
                            tr("1 = closest to the row (right)", "1 = le plus pres de la ligne (droite)"),
                            snap(13.0f), fa(C_DIM), fa(C_STROKE), 1.0f);
            }
            ROW_NEXT(30.0f)
            // The status-icon atlas, BORROWED (buff_atlas.cpp owns it and is the only place that releases it).
            // 0 while its bounded retry has not landed -- the grid then draws names alone rather than holes.
            const u32 bTex = buff_atlas_tex(dev);
            const float chipW = snap(92.0f), chipH = snap(28.0f), arrS = snap(15.0f);
            int mvFrom = -1, mvTo = -1;   // group move, applied after the loop
            for (int i = 0; i < UiConfig::BUFF_ORDER_N; ++i) {
                const int g = ui_config().buffOrder[i];
                const bool hid = ui_config().buff_group_hidden(g);
                int memTotal = 0; unsigned short probe[1];
                buff_group_members(ui_config(), g, probe, 1, &memTotal, [](unsigned st) { return party().status_seen(st); });
                // ---------- the group CELL : [caret] Name (N)        < n >     [ Shown ] ----------
                // Compact on purpose : the 260px selector capsule this row used to carry left no room for anything
                // else, and thirteen of them read as a form. Two chevrons and a number do the same job in 64px.
                { ROW_BAND(34.0f)
                    const float ty = ry + yo + (snap(40.0f) - snap(34.0f)) * 0.5f;
                    const float chipX = coX + ctrlW - chipW, numW = snap(30.0f);
                    const float rArrX = chipX - snap(12.0f) - arrS, numX = rArrX - numW, lArrX = numX - arrS;
                    const float aY = ty + (snap(34.0f) - arrS) * 0.5f;
                    // arrow_btn pads its hit zone by 7px, so stop the expand strip WELL clear of it : at a 6px
                    // gap the two rects overlapped by a pixel and one click there would both move the group
                    // and toggle it open.
                    const float czW = lArrX - coX - snap(14.0f);
                    const bool czHov = (czW > snap(20.0f)) && inrect(mo, coX, ty, czW, snap(34.0f));
                    if (czHov) flat(dev, coX, ty, czW, snap(34.0f), 0x14FFFFFFu);
                    if (czHov && click) bgOpen_[g] = !bgOpen_[g];
                    { const float gx = coX + snap(10.0f), gy = ty + snap(17.0f), cs3 = snap(3.5f); const u32 cc = fa(hid ? C_MUTE : C_ACCENTHI);
                      if (bgOpen_[g]) { const float d[6] = { gx - cs3, gy - cs3 * 0.55f,  gx + cs3, gy - cs3 * 0.55f,  gx, gy + cs3 * 0.85f }; fill_poly_aa(dev, d, 3, cc); }
                      else            { const float d[6] = { gx - cs3 * 0.55f, gy - cs3,  gx - cs3 * 0.55f, gy + cs3,  gx + cs3 * 0.85f, gy }; fill_poly_aa(dev, d, 3, cc); } }
                    char hl[64]; _snprintf(hl, sizeof(hl), "%s (%d)", tr(BUFF_GROUP_EN[g], BUFF_GROUP_FR[g]), memTotal); hl[sizeof(hl) - 1] = 0;
                    char pos[8]; sprintf(pos, "%d", i + 1);
                    fo->begin(dev);
                    fo->draw_lc(dev, coX + snap(26.0f), ty + snap(17.0f), hl, snap(14.0f), fa(hid ? C_MUTE : C_TEXT), fa(C_STROKE), 1.0f);
                    fo->draw_c(dev, numX + numW * 0.5f, ty + snap(17.0f), pos, snap(14.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
                    // a hidden group KEEPS its number : unhide it and it returns to this slot, so the list never
                    // renumbers under you while you are still arranging it.
                    if (arrow_btn(dev, fo, mo, click, ctrl_uid_i(CTRL_ID, i), lArrX, aY, arrS, "<") && i > 0)                          { mvFrom = i; mvTo = i - 1; }
                    if (arrow_btn(dev, fo, mo, click, ctrl_uid_i(CTRL_ID, i), rArrX, aY, arrS, ">") && i + 1 < UiConfig::BUFF_ORDER_N) { mvFrom = i; mvTo = i + 1; }
                    if (toggle_chip(dev, fo, mo, click, ctrl_uid_i(CTRL_ID, i),
                                    chipX, ty + (snap(34.0f) - chipH) * 0.5f, chipW, chipH,
                                    hid ? tr("Hidden", "Masque") : tr("Shown", "Affiche"), !hid)) {
                        ui_config().buff_group_toggle(g); save_ui_config();
                    }
                }
                ROW_NEXT(34.0f)
                if (!bgOpen_[g]) continue;

                // ---------- expanded : a DENSE GRID, not one 40px row per status ----------
                // The Timers "Buff filter" panel already answered this shape for 365 statuses : compact cells read
                // down columns, ~22px tall. One row per buff turned "Other" into 250 rows of form ; three columns
                // of 22px put the same content in a twelfth of the height.
                // Two needs pull opposite ways and both are honoured : you must be able to arrange ANY status
                // (party composition changes bring statuses this session has never seen), and the default view must
                // not be a wall -- so the list is "seen so far" until you ask for All.
                unsigned short mem[256]; int total = 0;
                const int listCap = bgAll_[g] ? (int)(sizeof(mem) / sizeof(mem[0])) : UiConfig::BUFF_PIN_MAX;
                const int nm = bgAll_[g]
                    ? buff_group_members(ui_config(), g, mem, listCap, &total, [](unsigned) { return true; })
                    : buff_group_members(ui_config(), g, mem, listCap, &total,
                                         [](unsigned st) { return party().status_seen(st); });
                int sel = bgSel_[g]; if (sel >= nm) sel = -1;            // the list shrank under the selection
                int mvA = -1, mvB = -1;
                // ---- sub-header : what is selected, the two move arrows, and the Seen/All switch ----
                { ROW_BAND(30.0f)
                    const float ty = ry + yo + snap(3.0f), mchipW = snap(78.0f), mchipH = snap(26.0f);
                    const float chipX = coX + ctrlW - mchipW;
                    const float rArrX = chipX - snap(14.0f) - arrS, lArrX = rArrX - snap(10.0f) - arrS;
                    const float aY = ty + (snap(26.0f) - arrS) * 0.5f;
                    char lb[110];
                    if (sel >= 0) { const char* n2 = buff_status_name(mem[sel]);
                                    _snprintf(lb, sizeof(lb), tr("Move : %s", "Deplacer : %s"), n2 ? n2 : "?"); lb[sizeof(lb) - 1] = 0; }
                    else lstrcpynA(lb, tr("Pick a buff below to move it", "Choisis un buff ci-dessous pour le deplacer"), sizeof(lb));
                    fo->begin(dev);
                    fo->draw_lc(dev, coX + snap(26.0f), ty + snap(13.0f), lb, snap(12.0f), fa(sel >= 0 ? C_TEXT : C_MUTE), fa(C_STROKE), 1.0f);
                    if (sel >= 0) {
                        if (arrow_btn(dev, fo, mo, click, ctrl_uid_i(CTRL_ID, g), lArrX, aY, arrS, "<") && sel > 0)      { mvA = sel; mvB = sel - 1; }
                        if (arrow_btn(dev, fo, mo, click, ctrl_uid_i(CTRL_ID, g), rArrX, aY, arrS, ">") && sel + 1 < nm) { mvA = sel; mvB = sel + 1; }
                    }
                    // The label carries the meaning : the chip is too narrow to say "the ones that have actually
                    // turned up this session", and "Seen" alone reads as an abbreviation.
                    if (toggle_chip(dev, fo, mo, click, ctrl_uid_i(CTRL_ID, g), chipX, ty + (snap(26.0f) - mchipH) * 0.5f,
                                    mchipW, mchipH, bgAll_[g] ? tr("All", "Tous") : tr("Seen", "Vus"), bgAll_[g]))
                        { bgAll_[g] = !bgAll_[g]; bgSel_[g] = -1; }   // the list changes under it -> the selection would point at another buff
                }
                ROW_NEXT(30.0f)
                if (nm == 0) { ROW_BAND(26.0f)
                    fo->begin(dev);
                    fo->draw_lc(dev, coX + snap(30.0f), ry + yo + snap(13.0f),
                                tr("none seen yet -- switch to All to arrange them", "rien de rencontre -- passe sur Tous pour les ranger"),
                                snap(12.0f), fa(C_MUTE), fa(C_STROKE), 1.0f);
                    ROW_NEXT(26.0f)
                    continue;
                }
                // ---- the grid : COLUMN-MAJOR, so it reads DOWN each column like the Timers checklist ----
                // THREE passes over the whole grid, not per cell. Interleaving a coloured quad (FVF 0x44) and a
                // textured one (0x144) per cell costs two FVF + texture-stage switches EVERY cell -- ~500 a frame
                // in All mode, on a fixed-function D3D8 device where that is exactly the cost you do not pay
                // casually. So: every band first, then ONE texture bind for every icon, then ONE font pass.
                // Row geometry is arithmetic (fixed cellH), so each pass can address any row directly.
                {
                    const float availG = ctrlW - snap(30.0f), x0 = coX + snap(26.0f);
                    const int cols = (availG > snap(560.0f)) ? 3 : (availG > snap(300.0f)) ? 2 : 1;
                    const float colW = availG / cols, cellH = snap(22.0f), ics = snap(16.0f);
                    const int rpc = (nm + cols - 1) / cols;
                    const float gridTop = ry;
                    const int riBase = ri;
                    auto cellX = [&](int c2) { return x0 + c2 * colW; };
                    auto cellY = [&](int r)  { return gridTop + r * cellH; };
                    // --- pass 1 : bands, selection/hover fills, and the clicks (colour-quad state) ---
                    for (int r = 0; r < rpc; ++r) {
                        g_fade = e * stagger(anim_, riBase + r);
                        row_band(dev, bandX, cellY(r), bandW, cellH, ((riBase + r) & 1) != 0, 0.0f);
                        for (int c2 = 0; c2 < cols; ++c2) {
                            const int k = c2 * rpc + r;   // column-major
                            if (k >= nm) continue;
                            // Past what can be STORED (the arranged prefix materialises down to the entry you move,
                            // so moving entry 200 would mean storing 200 ids) -> not selectable, and dimmed in
                            // pass 3, rather than a cell that takes a click and then quietly does nothing.
                            const bool arr = (k < UiConfig::BUFF_PIN_MAX);
                            const float cx2 = cellX(c2), cy2 = cellY(r), cw2 = colW - snap(6.0f);
                            const bool hov = arr && inrect(mo, cx2, cy2, cw2, cellH);
                            if (k == sel)  flat(dev, cx2, cy2, cw2, cellH, (C_ACCENT & 0x00FFFFFF) | 0x40000000u);
                            else if (hov)  flat(dev, cx2, cy2, cw2, cellH, 0x18FFFFFFu);
                            if (hov && click) { bgSel_[g] = (short)((k == sel) ? -1 : k); sel = bgSel_[g]; }
                        }
                    }
                    // --- pass 2 : every icon, under ONE texture bind for the whole grid ---
                    if (bTex) {
                        dTexQuadState(dev, bTex, false);
                        for (int r = 0; r < rpc; ++r) {
                            g_fade = e * stagger(anim_, riBase + r);
                            for (int c2 = 0; c2 < cols; ++c2) {
                                const int k = c2 * rpc + r;
                                if (k >= nm) continue;
                                float au, av, u0, v0; buff_cell_uv(mem[k], au, av, u0, v0);
                                tquad(dev, snap(cellX(c2) + snap(3.0f)), snap(cellY(r) + (cellH - ics) * 0.5f), ics, ics,
                                      u0, u0 + au, v0, v0 + av, 0xFFFFFFFFu, 0xFFFFFFFFu);
                            }
                        }
                        dSetTex(dev, 0, 0); cs(dev);   // never leave a bound texture / textured state for the next control (rule 8)
                    }
                    // --- pass 3 : every name, in ONE font pass ---
                    fo->begin(dev);
                    for (int r = 0; r < rpc; ++r) {
                        g_fade = e * stagger(anim_, riBase + r);
                        for (int c2 = 0; c2 < cols; ++c2) {
                            const int k = c2 * rpc + r;
                            if (k >= nm) continue;
                            const bool arr = (k < UiConfig::BUFF_PIN_MAX);
                            const char* n2 = buff_status_name(mem[k]);
                            fo->draw_lc(dev, cellX(c2) + (bTex ? snap(22.0f) : snap(4.0f)), cellY(r) + cellH * 0.5f,
                                        n2 ? n2 : "?", snap(12.0f),
                                        fa(!arr ? C_MUTE : (k == sel ? C_ACCENTHI : C_TEXT)), fa(C_STROKE), 1.0f);
                        }
                    }
                    ry = gridTop + rpc * cellH + snap(4.0f); ri = riBase + rpc;
                }
                if (total > nm) { ROW_BAND(24.0f)
                    char lb2[96]; _snprintf(lb2, sizeof(lb2), tr("+%d more, in the game's order", "+%d autres, dans l'ordre du jeu"), total - nm); lb2[sizeof(lb2) - 1] = 0;
                    fo->begin(dev);
                    fo->draw_lc(dev, coX + snap(30.0f), ry + yo + snap(12.0f), lb2, snap(12.0f), fa(C_MUTE), fa(C_STROKE), 1.0f);
                    ROW_NEXT(24.0f)
                }
                if (mvA >= 0) {
                    // Materialise the arranged prefix from WHAT IS ON SCREEN, down to the entry that moved, then
                    // swap. Seeding from the displayed order is what makes the first move on a group preserve
                    // everything above it -- the built-in order and the stored one can never disagree about it.
                    UiConfig& c = ui_config();
                    int need = (mvA > mvB ? mvA : mvB) + 1; if (need > UiConfig::BUFF_PIN_MAX) need = UiConfig::BUFF_PIN_MAX;
                    while (c.buffPinN[g] < need && c.buffPinN[g] < nm) { c.buffPin[g][c.buffPinN[g]] = mem[c.buffPinN[g]]; ++c.buffPinN[g]; }
                    if (mvA < c.buffPinN[g] && mvB < c.buffPinN[g]) {
                        const unsigned short t = c.buffPin[g][mvA]; c.buffPin[g][mvA] = c.buffPin[g][mvB]; c.buffPin[g][mvB] = t;
                        bgSel_[g] = (short)mvB;   // the selection FOLLOWS the buff, so a second click keeps moving the same one
                        save_ui_config();
                    }
                }
            }
            if (mvFrom >= 0) { ui_config().buff_order_swap(mvFrom, mvTo); save_ui_config(); }
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
