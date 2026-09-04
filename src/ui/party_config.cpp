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
            // bgOpen_ is indexed by BuffGroup. trkCatOpen_ and relOpen_ each overflowed into their neighbour
            // exactly this way once their enum outgrew the array -- silently, because nothing checked.
            static_assert(BG_COUNT <= (int)(sizeof(((ConfigPage*)0)->bgOpen_) / sizeof(bool)), "bgOpen_ must be at least BG_COUNT wide");
            static_assert(BG_COUNT <= (int)(sizeof(((ConfigPage*)0)->bgAll_)  / sizeof(bool)), "bgAll_ must be at least BG_COUNT wide");
            { ROW_BAND(34.0f)   // the one thing a number alone cannot say : which END of the strip position 1 is
                fo->begin(dev);
                fo->draw_lc(dev, coX + snap(4.0f), ry + yo + snap(17.0f),
                            tr("1 = closest to the row (right)", "1 = le plus pres de la ligne (droite)"),
                            snap(13.0f), fa(C_DIM), fa(C_STROKE), 1.0f);
            }
            ROW_NEXT(34.0f)
            // The swap is applied AFTER the loop : moving a group mid-iteration would draw the rest of the list
            // against an order that has already changed, and the row under the cursor would no longer be the one
            // that was clicked. CTRL_ID is a file:LINE hash, so every iteration would share ONE uid (and one hover
            // spring) -- ctrl_uid_i scatters them, exactly as the distance-colour pickers below have to.
            int mvFrom = -1, mvTo = -1;
            const float chipW = snap(96.0f), chipH = snap(30.0f), chipGap = snap(10.0f), indent = snap(22.0f);
            const float capW = snap(46.0f) + snap(168.0f) + snap(46.0f);   // row_selector's capsule : the SAME three snaps it uses, so the caret hit zone below stops exactly where the capsule starts
            for (int i = 0; i < UiConfig::BUFF_ORDER_N; ++i) {
                const int g = ui_config().buffOrder[i];
                const bool hid = ui_config().buff_group_hidden(g);
                { ROW_BAND(40.0f)
                char pos[8]; sprintf(pos, "%d", i + 1);
                const float selX = coX + indent, selW = ctrlW - indent - chipW - chipGap, ty = ry + yo;
                // ---- the caret : click the NAME half of the row to expand the group. Its zone is clamped to end
                // where the selector capsule begins, so expanding can never steal a click meant for an arrow. ----
                const float czW = (selX + selW - capW) - coX - snap(6.0f);
                const bool czHov = (czW > snap(20.0f)) && inrect(mo, coX, ty, czW, snap(40.0f));
                if (czHov) flat(dev, coX, ty, czW, snap(40.0f), 0x14FFFFFFu);
                if (czHov && click) bgOpen_[g] = !bgOpen_[g];
                { const float gx = coX + snap(9.0f), gy = ty + snap(20.0f), cs3 = snap(3.5f); const u32 cc = fa(C_ACCENTHI);
                  if (bgOpen_[g]) { const float d[6] = { gx - cs3, gy - cs3 * 0.55f,  gx + cs3, gy - cs3 * 0.55f,  gx, gy + cs3 * 0.85f }; fill_poly_aa(dev, d, 3, cc); }
                  else            { const float d[6] = { gx - cs3 * 0.55f, gy - cs3,  gx - cs3 * 0.55f, gy + cs3,  gx + cs3 * 0.85f, gy }; fill_poly_aa(dev, d, 3, cc); } }
                // The position selector is narrowed so the visibility chip owns the right end of the row.
                // A hidden group KEEPS its number : unhide it and it returns to the same slot, so the list
                // never renumbers under you while you are still arranging it.
                if (int d = row_selector(dev, fo, mo, click, ctrl_uid_i(CTRL_ID, i), selX, ty, selW,
                                         tr(BUFF_GROUP_EN[g], BUFF_GROUP_FR[g]), pos)) {
                    const int j = i + (d < 0 ? -1 : +1);
                    if (j >= 0 && j < UiConfig::BUFF_ORDER_N) { mvFrom = i; mvTo = j; }   // at the ends, the arrow is simply inert (no wrap : a group teleporting from one end of the strip to the other is never what you meant)
                }
                // Separate source line -> its own CTRL_ID, so the chip and the selector above cannot share
                // a spring even though both key their hover on sub 0/1 of their uid.
                if (toggle_chip(dev, fo, mo, click, ctrl_uid_i(CTRL_ID, i),
                                coX + ctrlW - chipW, ty + (snap(40.0f) - chipH) * 0.5f, chipW, chipH,
                                hid ? tr("Hidden", "MasquÃ©") : tr("Shown", "AffichÃ©"), !hid)) {
                    ui_config().buff_group_toggle(g); save_ui_config();
                }
                }
                ROW_NEXT(40.0f)
                // ---- expanded : the group's members, in the order the strip draws them. ----
                // NOT every status the group can hold -- "Other" can hold ~238 named ones, and listing them to be
                // hand-ordered is a menu nobody opens. A status gets a row when it is curated (the built-in list),
                // arranged, or has actually turned up on somebody this session. So the groups that matter are
                // complete from the first launch, and the open-ended ones show what you really meet.
                if (!bgOpen_[g]) continue;
                // ---- list mode. Two things are true at once and they pull opposite ways : you must be able to
                // arrange ANY status (party composition changes -- a job you have never grouped with brings statuses
                // this session has never seen), and the default view must not be a wall of 134 job abilities.
                // So : "Met" is the short list (curated + actually seen), "All" is every named status the group can
                // hold. The mode is per group and lives only for the session -- it is a way of looking, not a setting.
                { ROW_BAND(30.0f)
                    const float mchipW = snap(84.0f), mchipH = snap(26.0f), ty2 = ry + yo;
                    fo->begin(dev);
                    // The label carries the meaning, not the chip : "Seen" alone is ambiguous, and the chip is too
                    // narrow to say "the ones that have actually turned up this session" -- so the row says it.
                    fo->draw_lc(dev, coX + snap(42.0f), ty2 + snap(15.0f),
                                bgAll_[g] ? tr("List : every status of this group", "Liste : tous les statuts du groupe")
                                          : tr("List : only those seen so far", "Liste : ceux rencontr\xC3\xA9s jusqu'ici"),
                                snap(12.0f), fa(C_MUTE), fa(C_STROKE), 1.0f);
                    if (toggle_chip(dev, fo, mo, click, ctrl_uid_i(CTRL_ID, i),
                                    coX + ctrlW - mchipW, ty2 + (snap(30.0f) - mchipH) * 0.5f, mchipW, mchipH,
                                    bgAll_[g] ? tr("All", "Tous") : tr("Seen", "Vus"), bgAll_[g])) bgAll_[g] = !bgAll_[g];
                }
                ROW_NEXT(30.0f)
                unsigned short mem[256]; int total = 0;
                const int listCap = bgAll_[g] ? (int)(sizeof(mem) / sizeof(mem[0])) : UiConfig::BUFF_PIN_MAX;
                const int nm = bgAll_[g]
                    ? buff_group_members(ui_config(), g, mem, listCap, &total, [](unsigned) { return true; })
                    : buff_group_members(ui_config(), g, mem, listCap, &total,
                                         [](unsigned st) { return party().status_seen(st); });
                int mvA = -1, mvB = -1;   // applied after this group's loop : a swap mid-iteration would draw the
                for (int k = 0; k < nm; ++k) {   //  rest of the list against an order that has already changed
                    ROW_BAND(40.0f)
                    const char* nm2 = buff_status_name(mem[k]);
                    if (k < UiConfig::BUFF_PIN_MAX) {
                        char mp[8]; sprintf(mp, "%d", k + 1);
                        if (int d = row_selector(dev, fo, mo, click, ctrl_uid_i(CTRL_ID, g * UiConfig::BUFF_PIN_MAX + k),
                                                 coX + snap(38.0f), ry + yo, ctrlW - snap(38.0f), nm2 ? nm2 : "?", mp)) {
                            const int j2 = k + (d < 0 ? -1 : +1);
                            if (j2 >= 0 && j2 < nm) { mvA = k; mvB = j2; }
                        }
                    } else {
                        // Past what can be STORED (the arranged prefix materialises down to the row you touch, so
                        // moving row 200 would mean storing 200 ids). The row is still listed -- you can see the
                        // status is there -- but it carries no arrows and says why, rather than offering a control
                        // that would quietly do nothing.
                        fo->begin(dev);
                        fo->draw_lc(dev, coX + snap(42.0f), ry + yo + snap(20.0f), nm2 ? nm2 : "?", snap(13.0f), fa(C_MUTE), fa(C_STROKE), 1.0f);
                        fo->draw_lc(dev, coX + ctrlW - snap(120.0f), ry + yo + snap(20.0f), tr("game order", "ordre du jeu"), snap(11.0f), fa(C_MUTE), fa(C_STROKE), 1.0f);
                    }
                    ROW_NEXT(40.0f)
                }
                if (nm == 0 || total > nm) { ROW_BAND(28.0f)
                    // Two different things to say, and saying neither would leave an empty block that reads as a bug.
                    char lb[110];
                    if (nm == 0) lstrcpynA(lb, tr("none seen yet -- switch to All to arrange them", "rien de rencontr\xC3\xA9 -- passe sur Tous pour les ranger"), sizeof(lb));
                    else { _snprintf(lb, sizeof(lb), tr("+%d more, in the game's order", "+%d autres, dans l'ordre du jeu"), total - nm); lb[sizeof(lb) - 1] = 0; }
                    fo->begin(dev);
                    fo->draw_lc(dev, coX + snap(42.0f), ry + yo + snap(14.0f), lb, snap(12.0f), fa(C_MUTE), fa(C_STROKE), 1.0f);
                    ROW_NEXT(28.0f)
                }
                if (mvA >= 0) {
                    // Materialise the arranged prefix from WHAT IS ON SCREEN, down to the row that was touched, then
                    // swap. Seeding from the displayed order is what makes the first move on a group preserve
                    // everything above it -- the built-in order and the stored one can never disagree about it.
                    UiConfig& c = ui_config();
                    int need = (mvA > mvB ? mvA : mvB) + 1; if (need > UiConfig::BUFF_PIN_MAX) need = UiConfig::BUFF_PIN_MAX;
                    while (c.buffPinN[g] < need && c.buffPinN[g] < nm) { c.buffPin[g][c.buffPinN[g]] = mem[c.buffPinN[g]]; ++c.buffPinN[g]; }
                    if (mvA < c.buffPinN[g] && mvB < c.buffPinN[g]) {
                        const unsigned short t = c.buffPin[g][mvA]; c.buffPin[g][mvA] = c.buffPin[g][mvB]; c.buffPin[g][mvB] = t;
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
