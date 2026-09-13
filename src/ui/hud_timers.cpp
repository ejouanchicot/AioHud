// hud_timers.cpp -- split out of hud.cpp (pure move). Timers box renderer.
#include "ui/hud.h"
#include "model/timers_build.h"   // the rows this file draws (and the FOCUS monitor behind them) are built there
#include "model/model_clock.h"    // the build is one model event
#ifdef AIOHUD_DEVTOOLS
#include "aiohud_devtools.h"        // dev-only (dev/src, never in a release)
#endif
#include "model/flipwatch.h"   // notice a row set that cannot settle
#include "model/capwatch.h"   // notice a fixed table that has quietly run out of room
#include "ui/hud_internal.h"
#include "model/ui_config.h"
#include "ui/text_style.h"
#include "ui/box_style.h"
#include "model/party_state.h"
#include "gfx/draw.h"
#include "gfx/d3d.h"
#include "model/abilities_gen.h"
#include "model/spells_gen.h"
#include "model/buffs_gen.h"
#include "model/song_family_gen.h"   // song_family(spell) : identify a BRD song (spell-keyed -> no status family-collapse) for the song-OUT rule
#include "model/tb_buff_gen.h"       // spell_buff(spell)->skill : identify a GEO Indicolure (skill 44) for the Indi--replaced OUT rule
#include "model/action_status_gen.h"   // is_debuff_status : keep enfeebles (Blind/Poison/Slow) out of the buff list
#include "model/mobskills_gen.h"
#include "gfx/texture.h"
#include "model/paths.h"
#include "model/gamestate.h"
#include "windower_debug.h"   // //aio songlog layer 4 (dev diagnosis ; same precedent as ui/hud.cpp)
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <algorithm>

#include "ui/buff_atlas.h"
#include "ui/tex_retry.h"   // bounded lazy texture load (Help atlas) -- rule 10
#include "model/focus_rules.h"   // the FOCUS monitor's three judgements, pure and tested
#include "model/ally_group.h"   // the (AoE N) count and the group-or-name decision, pure and tested
#include "model/selftest.h"   // this module's own checks, run by the in-game watcher

namespace aio {

// The DECISIONS (rows, sort, FOCUS monitor, //aio out) live in model/timers_build.cpp since 2026-09-13 (lot D).
// This file only draws the rows it is handed.

// ============================ TIMERS box (self buff timers + recasts) ============================
// Exact server-sent buff durations (0x063 type-9 -> party().buff_timers()). Each row = the buff's status icon (the
// SAME atlas as the Player / Party boxes) + a MM:SS countdown, sorted soonest-first, coloured by urgency (white ->
// orange <=30s -> flashing red <=10s). Placed via //aio edit (EDITBOX_TIMERS).
// ---- Timers typography (own per-element TextStyle : TM_HEADER column titles / TM_BODY names + MM:SS) ----
static Font* tm_font(const Frame& f, int e) { return te_font(f, ui_config().tmText[e]); }
static inline float tm_sz(int e, float base) { return te_sz(ui_config().tmText[e], base); }
static inline float tm_ow(int e, float base) { return te_ow(ui_config().tmText[e], base); }
static inline u32   tm_col(int e, u32 base)  { return te_col(ui_config().tmText[e], base); }

void timers_draw(const Frame& f, bool preview, float ovX, float ovY, float ovS, float screenW, float screenH,
                 u32 buffAtlas, bool measureOnly = false, float* outW = 0, float* outH = 0) {
    const UiConfig& C = ui_config();
    if (!C.tmShow) return;
    const bool editing = C.editLayout && !preview;
    u32 dev = f.dev;
    if (!f.font && !f.fonts) return;

    using Row = TimersRow;
    static TimersRows rowsBuilt;   // static : ~100 rows of storage, never on the render thread's stack
    // ONE MODEL EVENT : every clock read inside the build sees the same instant (model/model_clock.h), so a row
    // cannot be timed a millisecond apart from the one sorted next to it.
    model_event_begin('T');
    const bool built = timers_build_rows(f.game, preview, editing, rowsBuilt);
#ifdef AIOHUD_DEVTOOLS
    devtools::timers_shadow(f.game, preview, editing, rowsBuilt, built);   // dev-only : the refactor's equivalence witness, same instant, same inputs
#endif
    model_event_end();
    if (!built) return;
    Row* bufs = rowsBuilt.bufs; Row* recs = rowsBuilt.recs; int nb = rowsBuilt.nb, nr = rowsBuilt.nr;

    float sscl = C.tmScale; if (sscl < 0.5f) sscl = 0.5f; if (sscl > 2.0f) sscl = 2.0f;
    const float S = (ovS > 0.0f) ? ovS : (screenH / 1000.0f) * sscl;
    const float pad = (ui_config().tmBox.on ? 8.0f : 0.0f) * S, gap = 4.0f * S, midGap = 30.0f * S, icgap = 4.0f * S;   // pad 0 when no box chrome ; midGap : space between the Duration & Recast columns (fused)
    const u32 white = 0xFFEAF0FFu, strk = 0xFF000000u, orange = 0xFFEB9660u, red = 0xFFF06060u, dim = 0xFFB4B9C8u, green = 0xFF74D074u;
    Font* fN = tm_font(f, TM_NAME); Font* fT = tm_font(f, TM_TIMER); Font* fH = tm_font(f, TM_HEADER);
    const float zN = tm_sz(TM_NAME, 13.0f) * S, zT = tm_sz(TM_TIMER, 13.0f) * S, zH = tm_sz(TM_HEADER, 13.0f) * S;
    const float oN = tm_ow(TM_NAME, 1.0f) * S, oT = tm_ow(TM_TIMER, 1.0f) * S, oH = tm_ow(TM_HEADER, 1.0f) * S;
    float iscl = C.tmIconScale; if (iscl < 0.5f) iscl = 0.5f; if (iscl > 2.0f) iscl = 2.0f;
    const float icon = 20.0f * iscl * S, rowH = icon + 3.0f * S, headH = zH + 5.0f * S;
    float tmrg = C.tmRowGap; if (tmrg < 0.6f) tmrg = 0.6f; if (tmrg > 3.0f) tmrg = 3.0f;
    const float rowPit = rowH * tmrg;   // per-row PITCH (config: row spacing) ; content stays centred in rowH, extra gap below
    const bool showHdr = (C.tmTitle != 0);
    const float bau = (float)BUFF_CELL / (float)BUFF_ATLAS_W, bav = (float)BUFF_CELL / (float)BUFF_ATLAS_H;
    const int bcells = BUFF_COLS * (BUFF_ATLAS_H / BUFF_CELL);
    const bool flash = ((GetTickCount() / 250u) & 1u) != 0;   // ~2 Hz blink for the <=10s alarm
    const bool flashStrong = ((GetTickCount() / 140u) & 1u) != 0;   // ~3.5 Hz HARD blink (Soul Voice last-minute window -> Nitro + re-sing)
    char tb[16];
    auto fmt = [&](int r) -> const char* { if (r == TM_REM_MISSING) { strcpy(tb, "OUT"); return tb; } if (r >= 3600) sprintf(tb, "%d:%02d:%02d", r / 3600, (r % 3600) / 60, r % 60); else sprintf(tb, "%d:%02d", r / 60, r % 60); return tb; };
    // a row name may carry a COR roll pip drawn "Name [5] (AoE 6)" with ONLY the [5] tinted. These keep the width
    // measurement and the draw in exact sync (name -> " [" -> pip(colour) -> "]" -> post).
    char pbuf[8];
    // The PERSON is outside the display mode : "Icon" is icon + who, "Name" is who + spell, "Both" is all three.
    // A row that names someone always names them -- an ally row reduced to an anonymous icon would not say whose
    // Haste is running out, which is the only thing that row is for. `wantSpell` carries the mode's half.
    auto rowNameW = [&](const Row& R, bool wantSpell) -> float {
        float w = 0.0f;
        if (R.who) { w += fN->measure(R.who, zN); if (wantSpell && R.name) w += fN->measure(" - ", zN); }
        if (!wantSpell || !R.name) return w;
        w += fN->measure(R.name, zN);
        if (R.pip > 0) { sprintf(pbuf, "%d", R.pip); w += fN->measure(" [", zN) + fN->measure(pbuf, zN) + fN->measure("]", zN); }
        if (R.tag) w += fN->measure(R.tag, zN);
        if (R.post) w += fN->measure(R.post, zN);
        return w;
    };
    auto drawRowName = [&](const Row& R, float nx, float cy, u32 baseCol, bool wantSpell) {   // who -> name -> [pip] -> (tag) -> post, each its own colour
        float xx = nx;
        if (R.who) {   // the person, always
            fN->draw_lc(dev, xx, cy, R.who, zN, baseCol, strk, oN); xx += fN->measure(R.who, zN);
            if (wantSpell && R.name) { fN->draw_lc(dev, xx, cy, " - ", zN, baseCol, strk, oN); xx += fN->measure(" - ", zN); }
        }
        if (!wantSpell || !R.name) return;   // Icon mode : the person carried the row, the spell is the icon
        fN->draw_lc(dev, xx, cy, R.name, zN, baseCol, strk, oN); xx += fN->measure(R.name, zN);
        if (R.pip > 0) { char pb[8]; sprintf(pb, "%d", R.pip);
            fN->draw_lc(dev, xx, cy, " [", zN, baseCol, strk, oN); xx += fN->measure(" [", zN);
            fN->draw_lc(dev, xx, cy, pb, zN, R.pipCol, strk, oN); xx += fN->measure(pb, zN);
            fN->draw_lc(dev, xx, cy, "]", zN, baseCol, strk, oN); xx += fN->measure("]", zN); }
        if (R.tag) { fN->draw_lc(dev, xx, cy, R.tag, zN, R.tagCol, strk, oN); xx += fN->measure(R.tag, zN); }
        if (R.post) fN->draw_lc(dev, xx, cy, R.post, zN, R.postCol ? R.postCol : baseCol, strk, oN);   // postCol 0 = follow the name
    };

    float measH = 0.0f;   // emit() stashes its boxH here so the top-level measureOnly (Help scale-to-fit) can read it
    struct Col { const char* title; Row* list; int n; int mode; u32 tex; float au, av; int cells; bool recast; };
    // draw ONE box holding `nc` columns at fractional (fx,fy) ; when ovS>0 (config preview) it centres on (ovcx,ovcy).
    // measureOnly returns the box width WITHOUT drawing (so the preview can lay two separate boxes side by side). Returns boxW.
    auto emit = [&](Col* cols, int nc, float fx, float fy, int editId, float* saveFx, float* saveFy, float ovcx, float ovcy, bool measureOnly) -> float {
        // The monitor number sits in a GUTTER at the far LEFT of the column, before the icon -- not between the
        // icon and the name, where it would read as part of the buff. The gutter is one width for the whole
        // column (the widest number in it), so every icon in the column still lines up; a per-row width would
        // ragged them by a digit. Zero when nothing in the column is monitored, so a column that has no numbers
        // loses no space to them.
        float colW[2] = { 0.0f, 0.0f }, markW[2] = { 0.0f, 0.0f }; int rowsMax = 0;
        char mkb[8];
        for (int c = 0; c < nc; ++c) {
            const Col& CC = cols[c]; if (CC.n > rowsMax) rowsMax = CC.n;
            const bool wantIcon = (CC.mode == TMDISP_ICON || CC.mode == TMDISP_BOTH);
            const bool wantName = (CC.mode == TMDISP_NAME || CC.mode == TMDISP_BOTH);
            const bool colIcon = wantIcon;   // the mode alone decides the icon column now : no row forces one any more
            float timeW = 0.0f, nameW = 0.0f;
            for (int i = 0; i < CC.n; ++i) { const float w = fT->measure(fmt(CC.list[i].rem), zT); if (w > timeW) timeW = w;
                const float nw = rowNameW(CC.list[i], wantName); if (nw > nameW) nameW = nw; }   // a row with a person is measured in every mode -- it prints one
            for (int i = 0; i < CC.n; ++i) if (CC.list[i].mark > 0) {
                sprintf(mkb, "%d", CC.list[i].mark);
                const float w2 = fN->measure(mkb, zN); if (w2 > markW[c]) markW[c] = w2; }
            if (markW[c] > 0.0f) markW[c] += icgap;
            float leftW = markW[c] + (colIcon ? icon : 0.0f);
            if (nameW > 0.0f) leftW += (colIcon ? icgap : 0.0f) + nameW;
            if (leftW <= 0.0f) leftW = icon;
            float w = leftW + gap + timeW;
            if (showHdr) { const float tW = fH->measure(CC.title, zH); if (tW > w) w = tW; }
            if (w < 46.0f * S) w = 46.0f * S;
            colW[c] = w;
        }
        float boxW = pad * 2.0f;
        for (int c = 0; c < nc; ++c) { boxW += colW[c]; if (c) boxW += midGap; }
        const int bodyRows = rowsMax > 0 ? rowsMax : 1;
        const float boxH = pad + (showHdr ? headH + gap : 0.0f) + bodyRows * rowPit + pad;
        if (measureOnly) { measH = boxH; return boxW; }

        float px, py;
        if (ovS > 0.0f) { px = snap(ovcx - boxW * 0.5f); py = snap(ovcy - boxH * 0.5f); }
        else            { px = snap(fx * screenW); py = snap(fy * screenH); }
        if (editing && saveFx) {
            static EditBox eb[2]; EditBox& g = eb[editId == EDITBOX_TIMERS ? 0 : 1];
            float tfx = px / screenW, tfy = py / screenH; bool ps = true; int ch = 0, cv = 0; const bool wasDrag = g.dragging;
            if (edit_box_drag(g, editId, f, px, py, boxW, boxH, ZPERM_HUB, ps, tfx, tfy, ch, cv, ui_config().tmScale)) edit_box_grid(dev, f, g, px, py, boxW, boxH, ch != 0, cv != 0);
            *saveFx = px / screenW; *saveFy = py / screenH; if (wasDrag && !g.dragging) save_ui_config();
        }

        dColorQuadState(dev);
        draw_themed_box(dev, f.skin, px, py, boxW, boxH, C.tmBox, 1.0f, S);   // shared themed chrome (frame/transp/theme)
        float cx = px + pad;
        for (int c = 0; c < nc; ++c) {
            const Col& CC = cols[c];
            const bool wantIcon = (CC.mode == TMDISP_ICON || CC.mode == TMDISP_BOTH);
            const bool wantName = (CC.mode == TMDISP_NAME || CC.mode == TMDISP_BOTH);
            float cyTop = py + pad;
            if (showHdr) { fH->begin(dev); fH->draw_c(dev, cx + colW[c] * 0.5f, cyTop + headH * 0.5f, CC.title, zH, tm_col(TM_HEADER, orange), strk, oH); cyTop += headH + gap; }
            float cyy = cyTop;
            for (int i = 0; i < CC.n; ++i) {
                const int r = CC.list[i].rem, ic = CC.list[i].icon; const char* nm = CC.list[i].name;
                // A "+ focus" buff (Tracked OR Hidden) that drops UNDER the focus-warn threshold blinks (name + timer) as an
                // early "recast soon" cue -- the warn threshold used to affect Hidden+focus only (it made the row APPEAR) ;
                // a Tracked+focus row was already visible, so it never warned before its normal <10s red flash.
                const bool focusWarn = !CC.recast && ic > 0 && r > 0 && r < C.tmFocusWarn
                                       && C.tm_buff_off(UiConfig::TM_KEY_FOCUS | (unsigned)ic);
                const bool rWantIcon = wantIcon;
                const bool haveIcon = (CC.tex && ic >= 0 && ic < CC.cells);
                bool drewIcon = false;
                const float gx0 = cx + markW[c];   // everything after the number gutter
                if (CC.list[i].mark > 0) {         // the handle //aio out takes -- dim, it is not part of the buff
                    char mb[8]; sprintf(mb, "%d", CC.list[i].mark);
                    fN->begin(dev); fN->draw_lc(dev, cx, cyy + rowH * 0.5f, mb, zN, 0xFF6E7885u, strk, oN);
                }
                if (rWantIcon && haveIcon) { const float u0 = (float)(ic % BUFF_COLS) * CC.au, v0 = (float)(ic / BUFF_COLS) * CC.av; draw_icon_cell(dev, CC.tex, gx0, cyy + (rowH - icon) * 0.5f, icon, icon, u0, u0 + CC.au, v0, v0 + CC.av); drewIcon = true; }
                // The spell name : asked for by the mode, OR as a fallback when the icon we wanted has no art (an
                // icon-only row with no icon would be blank). The person is drawn either way, so a row that has one
                // still prints in Icon mode.
                const bool rWantSpell = wantName || (rWantIcon && !haveIcon);
                if ((nm && rWantSpell) || CC.list[i].who) {
                    const float nx = gx0 + (drewIcon ? icon + icgap : 0.0f);
                    u32 baseNameCol = CC.list[i].nameCol ? CC.list[i].nameCol : tm_col(TM_NAME, dim);
                    if (CC.list[i].rem == TM_REM_MISSING) baseNameCol = flashStrong ? 0xFFFF6A6Au : 0xFFFF2020u;   // FOCUS alert : the whole "Ally - Buff" row blinks red
                    else if (C.tmSpAlert && CC.list[i].icon > 0 && CC.list[i].rem > 0 && CC.list[i].rem < 60 && is_sp_buff_status(CC.list[i].icon)) baseNameCol = flashStrong ? 0xFFFFF000u : 0xFFFF1010u;   // SP last-minute : the whole row blinks hard
                    else if (focusWarn) baseNameCol = flash ? 0xFFFF6A6Au : baseNameCol;   // +focus under the warn threshold : name blinks red
                    fN->begin(dev); drawRowName(CC.list[i], nx, cyy + rowH * 0.5f, baseNameCol, rWantSpell);   // person + name + optional coloured roll pip / song tag
                }
                fmt(r);
                u32 tc;
                if (r == TM_REM_MISSING) { tc = flashStrong ? 0xFFFF6A6Au : 0xFFFF2020u; }   // FOCUS alert : "OUT" blinks red
                else if (CC.recast) { tc = (r <= 10) ? green : (r <= 30) ? orange : red; }   // recast : INVERSE -- red just after use (long wait) -> orange -> green as it nears ready
                else if (C.tmSpAlert && CC.list[i].icon > 0 && r > 0 && r < 60 && is_sp_buff_status(CC.list[i].icon)) { tc = flashStrong ? 0xFFFFF000u : 0xFFFF1010u; }   // SP ability last minute : HARD blink bright-yellow<->red (Soul Voice -> Nitro window, etc.)
                else if (focusWarn) { tc = (r <= 10) ? (flash ? red : 0xFFFFC8C8u) : (flash ? red : orange); }   // +focus under the warn threshold : timer blinks orange<->red (harder <10s) in sync with the name
                else { tc = tm_col(TM_TIMER, white); if (r <= 10) tc = flash ? red : 0xFFFFC8C8u; else if (r <= 30) tc = orange; }   // duration : white -> orange (<30) -> flashing red (<10)
                const float tw = fT->measure(tb, zT);
                fT->begin(dev); fT->draw_lc(dev, cx + colW[c] - tw, cyy + rowH * 0.5f, tb, zT, tc, strk, oT);
                cyy += rowPit;
            }
            cx += colW[c] + midGap;
        }
        return boxW;
    };

    Col dur = { "Duration", bufs, nb, C.tmDurMode, buffAtlas, bau, bav, bcells, false };
    Col rec = { "Recast",   recs, nr, TMDISP_NAME, 0, 0.0f, 0.0f, 0, true };   // recasts : text-only ; colour INVERSE of duration
    if (ovS > 0.0f) {                 // config preview stage : reflect Fused vs Separate
        if (measureOnly) {            // Help scale-to-fit : report the total footprint, don't draw
            if (C.tmMerged) { Col cc[2] = { dur, rec }; const float w = emit(cc, 2, 0, 0, EDITBOX_TIMERS, 0, 0, 0, 0, true); if (outW) *outW = w; if (outH) *outH = measH; }
            else { const float wD = emit(&dur, 1, 0, 0, EDITBOX_TIMERS, 0, 0, 0, 0, true); const float hD = measH;
                   const float wR = emit(&rec, 1, 0, 0, EDITBOX_TIMERS_R, 0, 0, 0, 0, true); const float hR = measH;
                   const float g2 = 16.0f * S; if (outW) *outW = wD + g2 + wR; if (outH) *outH = (hD > hR ? hD : hR); }
            return;
        }
        if (C.tmMerged) { Col cc[2] = { dur, rec }; emit(cc, 2, 0, 0, EDITBOX_TIMERS, 0, 0, ovX, ovY, false); }
        else {                        // two boxes side by side, centred as a group in the stage
            const float wD = emit(&dur, 1, 0, 0, EDITBOX_TIMERS, 0, 0, 0, 0, true);
            const float wR = emit(&rec, 1, 0, 0, EDITBOX_TIMERS_R, 0, 0, 0, 0, true);
            const float g2 = 16.0f * S, tot = wD + g2 + wR;
            emit(&dur, 1, 0, 0, EDITBOX_TIMERS,   0, 0, ovX - tot * 0.5f + wD * 0.5f, ovY, false);
            emit(&rec, 1, 0, 0, EDITBOX_TIMERS_R, 0, 0, ovX + tot * 0.5f - wR * 0.5f, ovY, false);
        }
    } else if (C.tmMerged) {          // live merged : one box ; a column with nothing to show DEPOPS (empty -> gone)
        Col cc[2]; int ncc = 0;
        if (nb > 0) cc[ncc++] = dur;
        if (nr > 0) cc[ncc++] = rec;
        if (ncc > 0) emit(cc, ncc, C.tmX, C.tmY, EDITBOX_TIMERS, &ui_config().tmX, &ui_config().tmY, 0, 0, false);
    } else {                          // live separate : each box depops on its own when its column is empty
        if (nb > 0) emit(&dur, 1, C.tmX,  C.tmY,  EDITBOX_TIMERS,   &ui_config().tmX,  &ui_config().tmY,  0, 0, false);
        if (nr > 0) emit(&rec, 1, C.tmRX, C.tmRY, EDITBOX_TIMERS_R, &ui_config().tmRX, &ui_config().tmRY, 0, 0, false);
    }
}

// Live / edit path : the Hud draws the box(es) at their configured screen positions (lazy-loads its buff atlas).
// Lazy load of the shared status-icon atlas, with a BOUNDED RETRY. Both call sites (Timers and Debuffs) used to
// carry their own one-shot `if (!tried) { load; tried = true; }`, so ONE transient miss -- the updater replacing
// buff_atlas.raw at that instant, the device not ready yet -- permanently killed EVERY status icon in those boxes
// for the rest of the session. Reported right after an update as "Protect, Cocoon... no icons"; the atlas file was
// intact, only the texture was missing. Same defect class as the gear icons: a permanent give-up on a transient
// failure. ~12 tries, 300 ms apart, then stop (a genuinely absent asset must not retry forever).
// The retry itself now lives with the ONE owner (buff_atlas.cpp) : same 12-tries / 300 ms budget, same "say so
// when the budget dies" log, but one texture instead of eight. buffAtlas_ is a BORROWED handle refreshed here
// each frame -- the HUD must not Release it (buff_atlas_dispose() is the single Release site).
u32 Hud::ensure_buff_atlas(u32 dev) {
    buffAtlas_ = buff_atlas_tex(dev);
    return buffAtlas_;
}

void Hud::draw_timers(const Frame& f, bool preview, float ovX, float ovY, float ovS) {
    ensure_buff_atlas(f.dev);
    timers_draw(f, preview, ovX, ovY, ovS, (float)screenW_, (float)screenH_, buffAtlas_);
}

// The Help sample owns its own copy of the buff-icon atlas (lazy) so it can draw Duration icons without a Hud.
// The Help sample used to keep its OWN copy of the atlas, with its own forget/dispose pair wired into
// Hud::render's dev-change block. It now borrows the shared one (buff_atlas.cpp), which is forgotten and
// released once from those same two blocks -- so this file no longer owns a texture at all.
static u32 timers_help_atlas(u32 dev) { return buff_atlas_tex(dev); }

// Help sample : the REAL Timers box(es) in preview mode (config-aware), centred at (cx,cy) at scale `s`.
void timers_help_box(const Frame& f, float cx, float cy, float s) {
    timers_draw(f, true, cx, cy, s, 0.0f, 0.0f, timers_help_atlas(f.dev));
}

// Help scale-to-fit : measure at scale 1 (linear in S), pick the largest scale that fits availW (capped at maxScale).
void timers_help_fit(const Frame& f, float availW, float maxScale, float& outScale, float& outH) {
    float bw = 0.0f, bh = 0.0f;
    timers_draw(f, true, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, timers_help_atlas(f.dev), true, &bw, &bh);
    float s = (bw > 1.0f) ? (availW / bw) : maxScale;
    if (s > maxScale) s = maxScale; if (s < 0.6f) s = 0.6f;
    outScale = s; outH = bh * s;
}

} // namespace aio
