// hud_zonetracker.cpp -- split out of hud.cpp (pure move). Zone Tracker box renderer.
#include "ui/hud.h"
#include "ui/hud_internal.h"
#include "model/ui_config.h"
#include "ui/text_style.h"
#include "ui/box_style.h"
#include "model/party_state.h"
#include "gfx/draw.h"
#include "gfx/d3d.h"
#include "model/paths.h"
#include "gfx/texture.h"
#include "model/zones.h"
#include "model/resistances.h"
#include "model/gamestate.h"
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>

namespace aio {

static const char* WEAPON_ICONS_PATH() { static char b[260]; if (!b[0]) plugin_path(b, 260, "assets\\weapon_icons.raw"); return b; }
// ============================ ZONE TRACKER box (Dynamis / Abyssea) ============================
static Font* zt_font(const Frame& f, int e) { return te_font(f, ui_config().ztText[e]); }
static inline float zt_sz(int e, float base) { return te_sz(ui_config().ztText[e], base); }
static inline float zt_ow(int e, float base) { return te_ow(ui_config().ztText[e], base); }
static inline u32   zt_col(int e, u32 base)  { return te_col(ui_config().ztText[e], base); }
// green (full) -> yellow -> red (empty), like the mockup timeRamp.
static u32 zt_time_col(float p) {
    if (p < 0.0f) p = 0.0f; if (p > 1.0f) p = 1.0f;
    float r, g; if (p > 0.5f) { r = (1.0f - p) * 2.0f; g = 1.0f; } else { r = 1.0f; g = p * 2.0f; }
    return 0xFF000000u | ((u32)(45 + r * 185) << 16) | ((u32)(45 + g * 180) << 8) | 55u;
}

// The Zone Tracker : Dynamis (run timer + 5 granule Key Items) or Abyssea (visitant timer + 7 lights), from
// party().zone_tracker() (packet-fed). Appears only in those zones ; a sample (per ztVariant) in preview/edit.
// Core renderer, extracted as a FREE function so the config PREVIEW and the Help sample reuse the EXACT same
// config-aware draw with no Hud instance. `weaponTex` = the Odyssey weapon-icons sheet (0 = none), from the caller.
void zonetracker_draw(const Frame& f, bool preview, float ovX, float ovY, float ovS, float screenW, float screenH,
                      u32 weaponTex, bool measureOnly = false, float* outW = 0, float* outH = 0, int variantOverride = -1) {
    const UiConfig& C = ui_config();
    const int vz = (variantOverride >= 0) ? variantOverride : C.ztVariant;   // Help forces a specific content variant (0 Dyn / 1 Aby / 2 Omen / 3 Nyzul / 4 Odyssey)
    if (!C.ztShow && variantOverride < 0) return;   // Help sample ignores the Show toggle
    const bool showHdr = (C.ztHeader != 0);   // the box TITLE row (Dynamis / Abyssea / Omen / Nyzul / Sheol) -- toggle
    const bool editing = C.editLayout && !preview;
    u32 dev = f.dev;
    if (!f.font && !f.fonts) return;

    // ===== OMEN (mode 3, Reisenjima Henge) : header + floor objective + bonus timer + up to 10 objective rows =====
    if ((preview || editing) ? (vz == 2) : (party().zone_tracker().mode == 3)) {
        struct ORow { char label[16]; int cur, req; unsigned char done, failed; };
        ORow orows[10]; int nrows = 0;
        char floorObj[48] = {0}; int omens = 0, bonusSec = 0;
        if (preview || editing) {
            const char* fo = "Vanquish all transcended foes"; int k = 0; for (; fo[k] && k < 47; ++k) floorObj[k] = fo[k]; floorObj[k] = 0;
            omens = 5; bonusSec = 272;
            static const struct { const char* n; int c, r; } SMP[6] = { {"WS Damage",15000,15000},{"Kills",8,8},{"MB Damage",12000,15000},{"Critical Hits",40,60},{"Magic Bursts",2,5},{"Skillchains",1,3} };
            for (int i = 0; i < 6; ++i) { ORow& r = orows[nrows]; int j = 0; for (; SMP[i].n[j] && j < 15; ++j) r.label[j] = SMP[i].n[j]; r.label[j] = 0; r.cur = SMP[i].c; r.req = SMP[i].r; r.done = (r.cur >= r.req); r.failed = 0; ++nrows; }
        } else {
            const ZoneTracker& zt = party().zone_tracker();
            int k = 0; for (; zt.floorObj[k] && k < 47; ++k) floorObj[k] = zt.floorObj[k]; floorObj[k] = 0;
            omens = zt.omens;
            bonusSec = zt.omenBonusSec - (int)((GetTickCount() - zt.omenBonusMs) / 1000u); if (bonusSec < 0) bonusSec = 0;
            for (int i = 0; i < 10; ++i) if (zt.omen[i].type) { ORow& r = orows[nrows]; const char* nm = PartyState::omen_short(zt.omen[i].type);
                int j = 0; for (; nm[j] && j < 15; ++j) r.label[j] = nm[j]; r.label[j] = 0;
                r.cur = zt.omen[i].cur; r.req = zt.omen[i].req;
                r.done = (zt.omen[i].req > 0 && r.cur >= zt.omen[i].req);
                r.failed = (bonusSec < 1 && zt.omen[i].req > 0 && r.cur < zt.omen[i].req); ++nrows; }
        }
        float sscl = C.ztScale; if (sscl < 0.5f) sscl = 0.5f; if (sscl > 2.0f) sscl = 2.0f;
        const float S = (ovS > 0.0f) ? ovS : (screenH / 1000.0f) * sscl;
        const float pad = (ui_config().ztBox.on ? 8.0f : 0.0f) * S, gap = 5.0f * S;   // no box chrome -> hug the content
        const u32 white = 0xFFEAF0FFu, gold = 0xFFE8C55Au, strk = 0xFF000000u, orange = 0xFFEB9660u, dim = 0xFFB4B9C8u, green = 0xFF6BE06Bu, red = 0xFFF06060u;
        Font* fH = zt_font(f, ZT_HEADER);
        const float zH = zt_sz(ZT_HEADER, 15.0f) * S, oH = zt_ow(ZT_HEADER, 1.1f) * S, headH = zH + 6.0f * S;
        // Each Omen row group owns a text element + a visibility toggle, so the objective line, the omen/bonus
        // counter and the numbered rows are sized / coloured / hidden independently.
        Font* fO = zt_font(f, ZT_OM_OBJ); Font* fN = zt_font(f, ZT_OM_COUNT); Font* fR = zt_font(f, ZT_OM_ROW);
        const float zO = zt_sz(ZT_OM_OBJ, 12.0f) * S, zN = zt_sz(ZT_OM_COUNT, 12.0f) * S, zR = zt_sz(ZT_OM_ROW, 12.0f) * S;
        const float oO = zt_ow(ZT_OM_OBJ, 1.0f) * S, oN = zt_ow(ZT_OM_COUNT, 1.0f) * S, oR = zt_ow(ZT_OM_ROW, 1.0f) * S;
        const float objLineH = zO + 5.0f * S, cntLineH = zN + 5.0f * S, rowLineH = zR + 5.0f * S;
        const bool hasObj = (C.ztOmObj != 0), hasCnt = (C.ztOmCount != 0), hasRows = (C.ztOmRows != 0);
        char sb[48], hb[40]; sprintf(hb, "Omens: %d   Bonus %d:%02d", omens, bonusSec / 60, bonusSec % 60);
        #define OMEN_FMT(i) (orows[i].req < 0 ? (sprintf(sb, "%d: %.14s [%d/???]", (i) + 1, orows[i].label, orows[i].cur), sb) : (sprintf(sb, "%d: %.14s [%d/%d]", (i) + 1, orows[i].label, orows[i].cur, orows[i].req), sb))
        float contentW = fH->measure("Omen", zH);
        if (hasObj && fO->measure(floorObj, zO) > contentW) contentW = fO->measure(floorObj, zO);
        if (hasCnt && fN->measure(hb, zN) > contentW) contentW = fN->measure(hb, zN);
        if (hasRows) for (int i = 0; i < nrows; ++i) { float w = fR->measure(OMEN_FMT(i), zR); if (w > contentW) contentW = w; }
        if (contentW < 150.0f * S) contentW = 150.0f * S;
        const float boxW = contentW + 2.0f * pad;
        const float boxH = pad + (showHdr ? headH + gap : 0.0f) + (hasObj ? objLineH + gap * 0.5f : 0.0f)
                         + (hasCnt ? cntLineH + 4.0f * S : 0.0f) + (hasRows ? nrows * rowLineH : 0.0f) + pad;
        if (measureOnly) { if (outW) *outW = boxW; if (outH) *outH = boxH; return; }
        float px, py;
        if (ovS > 0.0f) { px = ovX - boxW * 0.5f; py = ovY - boxH * 0.5f; }
        else            { px = C.ztX * screenW - boxW * 0.5f; py = C.ztY * screenH; }
        if (editing) { static EditBox g_ztEdit; box_edit(f, g_ztEdit, EDITBOX_ZONETRACKER, px, py, boxW, boxH, ui_config().ztScale, ui_config().ztX, ui_config().ztY, 1); }
        dColorQuadState(dev);
        const float r0 = 6.0f * S;
        draw_themed_box(dev, f.skin, px, py, boxW, boxH, ui_config().ztBox, 1.0f, S);   // shared themed chrome (frame/transp/theme)
        const float cx = px + boxW * 0.5f, x0 = px + pad;
        float cy = py + pad;
        if (showHdr) { fH->begin(dev); fH->draw_c(dev, cx, cy + headH * 0.5f, "Omen", zH, zt_col(ZT_HEADER, orange), strk, oH); cy += headH + gap; }
        if (hasObj) { fO->begin(dev); fO->draw_c(dev, cx, cy + objLineH * 0.5f, floorObj, zO, zt_col(ZT_OM_OBJ, white), strk, oO); cy += objLineH + gap * 0.5f; }
        if (hasCnt) { fN->begin(dev); fN->draw_c(dev, cx, cy + cntLineH * 0.5f, hb, zN, zt_col(ZT_OM_COUNT, dim), strk, oN); cy += cntLineH + 4.0f * S; }
        if (hasRows) for (int i = 0; i < nrows; ++i) { const char* rs = OMEN_FMT(i);
            const u32 col = orows[i].done ? green : (orows[i].failed ? red : zt_col(ZT_OM_ROW, white));
            fR->begin(dev); fR->draw_lc(dev, x0, cy + rowLineH * 0.5f, rs, zR, col, strk, oR); cy += rowLineH; }
        #undef OMEN_FMT
        return;
    }

    // ===== NYZUL ISLE (mode 4) : header + Floor / Time + Objective / Restriction / Completed / Reward Rate / Tokens =====
    if ((preview || editing) ? (vz == 3) : (party().zone_tracker().mode == 4)) {
        int floor = 0, remain = 0, completed = 0, rate = 100; long tokens = 0;
        char objective[48] = {0}, restriction[40] = {0}; unsigned char objPending = 1, restrFail = 0;
        if (preview || editing) {
            floor = 42; remain = 48; completed = 7; rate = 110; tokens = 1540;
            const char* o = "Defeat all enemies"; int k = 0; for (; o[k] && k < 47; ++k) objective[k] = o[k]; objective[k] = 0;
            const char* rr = "No magic"; k = 0; for (; rr[k] && k < 39; ++k) restriction[k] = rr[k]; restriction[k] = 0;
        } else {
            const ZoneTracker& zt = party().zone_tracker();
            floor = zt.nyFloor; remain = party().nyzul_remaining(); if (remain < 0) remain = 0;
            completed = zt.nyCompleted; tokens = (long)(zt.nyTokens + 0.5);
            double rd = 1.0; if (zt.nyArmband) rd += 0.1; if (zt.nyPartySize > 3) rd -= (zt.nyPartySize - 3) * 0.1;   // live reward rate
            rate = (int)(rd * 100.0 + 0.5);
            int k = 0; for (; zt.nyObjective[k] && k < 47; ++k) objective[k] = zt.nyObjective[k]; objective[k] = 0;
            k = 0; for (; zt.nyRestriction[k] && k < 39; ++k) restriction[k] = zt.nyRestriction[k]; restriction[k] = 0;
            objPending = zt.nyObjPending; restrFail = zt.nyRestrFail;
        }
        const bool hasRestr = (restriction[0] != 0) && (C.ztNyRestr != 0);
        float sscl = C.ztScale; if (sscl < 0.5f) sscl = 0.5f; if (sscl > 2.0f) sscl = 2.0f;
        const float S = (ovS > 0.0f) ? ovS : (screenH / 1000.0f) * sscl;
        const float pad = (ui_config().ztBox.on ? 8.0f : 0.0f) * S, gap = 5.0f * S;   // no box chrome -> hug the content
        const u32 white = 0xFFEAF0FFu, gold = 0xFFE8C55Au, strk = 0xFF000000u, orange = 0xFFEB9660u;
        const u32 yellow = 0xFFFFFA78u, green = 0xFF6BE06Bu, red = 0xFFF06060u, orst = 0xFFFFA500u;
        Font* fH = zt_font(f, ZT_HEADER);
        const float zH = zt_sz(ZT_HEADER, 15.0f) * S, oH = zt_ow(ZT_HEADER, 1.1f) * S, headH = zH + 6.0f * S;
        // One text element + one visibility toggle per Nyzul row : Floor / Time / Objective / Restriction and the
        // three stat lines (Completed / Reward Rate / Tokens), which share one element as one visual group.
        Font* fFl = zt_font(f, ZT_NY_FLOOR); Font* fTi = zt_font(f, ZT_NY_TIME);
        Font* fOb = zt_font(f, ZT_NY_OBJ);   Font* fRe = zt_font(f, ZT_NY_RESTR); Font* fSt = zt_font(f, ZT_NY_STATS);
        const float zFl = zt_sz(ZT_NY_FLOOR, 12.0f) * S, zTi = zt_sz(ZT_NY_TIME, 12.0f) * S;
        const float zOb = zt_sz(ZT_NY_OBJ, 12.0f) * S,   zRe = zt_sz(ZT_NY_RESTR, 12.0f) * S, zSt = zt_sz(ZT_NY_STATS, 12.0f) * S;
        const float oFl = zt_ow(ZT_NY_FLOOR, 1.0f) * S, oTi = zt_ow(ZT_NY_TIME, 1.0f) * S;
        const float oOb = zt_ow(ZT_NY_OBJ, 1.0f) * S,   oRe = zt_ow(ZT_NY_RESTR, 1.0f) * S, oSt = zt_ow(ZT_NY_STATS, 1.0f) * S;
        const float flH = zFl + 5.0f * S, tiH = zTi + 5.0f * S, obH = zOb + 5.0f * S, reH = zRe + 5.0f * S, stH = zSt + 5.0f * S;
        const bool hasFloor = (C.ztNyFloor != 0), hasTime = (C.ztNyTime != 0), hasObj = (C.ztNyObj != 0);
        const bool hasComp = (C.ztNyComp != 0), hasRate = (C.ztNyRate != 0), hasTok = (C.ztNyTok != 0);
        char rFloor[24], rTime[24], rComp[24], rRate[24], rTok[28];
        sprintf(rFloor, "Floor: %d", floor);
        sprintf(rTime, "Time:  %d:%02d", remain / 60, remain % 60);
        sprintf(rComp, "Completed: %d", completed);
        sprintf(rRate, "Reward Rate: %d%%", rate);
        sprintf(rTok,  "Tokens: %ld", tokens);
        // content width : header + centred Floor/Time + the left rows (objective/restriction measured label+value together)
        char rObj[64], rRestr[60];
        sprintf(rObj,   "Objective: %.40s", objective[0] ? objective : "-");
        sprintf(rRestr, "Restriction: %.36s", restriction);
        float contentW = fH->measure("Nyzul Isle", zH);
        {   // each row measured with ITS own font/size (they can differ now)
            struct MRow { bool on; Font* fn; const char* s; float z; };
            const MRow mw[7] = { { hasFloor, fFl, rFloor, zFl }, { hasTime, fTi, rTime, zTi }, { hasObj, fOb, rObj, zOb },
                                 { hasRestr, fRe, rRestr, zRe }, { hasComp, fSt, rComp, zSt }, { hasRate, fSt, rRate, zSt }, { hasTok, fSt, rTok, zSt } };
            for (int i = 0; i < 7; ++i) if (mw[i].on) { const float w = mw[i].fn->measure(mw[i].s, mw[i].z); if (w > contentW) contentW = w; }
        }
        if (contentW < 150.0f * S) contentW = 150.0f * S;
        const float boxW = contentW + 2.0f * pad;
        // spacer between the centred pair and the left rows -- only when BOTH groups actually have a row
        const float blank = ((hasFloor || hasTime) && (hasObj || hasRestr || hasComp || hasRate || hasTok)) ? (zSt + 5.0f * S) * 0.6f : 0.0f;
        const float boxH = pad + (showHdr ? headH + gap : 0.0f)
                         + (hasFloor ? flH : 0.0f) + (hasTime ? tiH : 0.0f) + blank
                         + (hasObj ? obH : 0.0f) + (hasRestr ? reH : 0.0f)
                         + (hasComp ? stH : 0.0f) + (hasRate ? stH : 0.0f) + (hasTok ? stH : 0.0f) + pad;
        if (measureOnly) { if (outW) *outW = boxW; if (outH) *outH = boxH; return; }
        float px, py;
        if (ovS > 0.0f) { px = ovX - boxW * 0.5f; py = ovY - boxH * 0.5f; }
        else            { px = C.ztX * screenW - boxW * 0.5f; py = C.ztY * screenH; }
        if (editing) { static EditBox g_ztEdit; box_edit(f, g_ztEdit, EDITBOX_ZONETRACKER, px, py, boxW, boxH, ui_config().ztScale, ui_config().ztX, ui_config().ztY, 1); }
        dColorQuadState(dev);
        const float r0 = 6.0f * S;
        draw_themed_box(dev, f.skin, px, py, boxW, boxH, ui_config().ztBox, 1.0f, S);   // shared themed chrome (frame/transp/theme)
        const float cx = px + boxW * 0.5f, x0 = px + pad;
        float cy = py + pad;
        if (showHdr) { fH->begin(dev); fH->draw_c(dev, cx, cy + headH * 0.5f, "Nyzul Isle", zH, zt_col(ZT_HEADER, orange), strk, oH); cy += headH + gap; }
        if (hasFloor) { fFl->begin(dev); fFl->draw_c(dev, cx, cy + flH * 0.5f, rFloor, zFl, zt_col(ZT_NY_FLOOR, white), strk, oFl); cy += flH; }
        if (hasTime) { fTi->begin(dev); const u32 tc = (remain < 60) ? red : zt_col(ZT_NY_TIME, white);
          fTi->draw_c(dev, cx, cy + tiH * 0.5f, rTime, zTi, tc, strk, oTi); cy += tiH; }
        cy += blank;
        if (hasObj) { const char* lbl = "Objective: "; const u32 vc = objPending ? yellow : green;   // label default, value coloured by state
          fOb->begin(dev);
          fOb->draw_lc(dev, x0, cy + obH * 0.5f, lbl, zOb, zt_col(ZT_NY_OBJ, white), strk, oOb);
          fOb->draw_lc(dev, x0 + fOb->measure(lbl, zOb), cy + obH * 0.5f, objective[0] ? objective : "-", zOb, vc, strk, oOb); cy += obH; }
        if (hasRestr) { const char* lbl = "Restriction: "; const u32 vc = restrFail ? red : orst;
          fRe->begin(dev);
          fRe->draw_lc(dev, x0, cy + reH * 0.5f, lbl, zRe, zt_col(ZT_NY_RESTR, white), strk, oRe);
          fRe->draw_lc(dev, x0 + fRe->measure(lbl, zRe), cy + reH * 0.5f, restriction, zRe, vc, strk, oRe); cy += reH; }
        fSt->begin(dev);
        if (hasComp) { fSt->draw_lc(dev, x0, cy + stH * 0.5f, rComp, zSt, zt_col(ZT_NY_STATS, white), strk, oSt); cy += stH; }
        if (hasRate) { fSt->draw_lc(dev, x0, cy + stH * 0.5f, rRate, zSt, zt_col(ZT_NY_STATS, white), strk, oSt); cy += stH; }
        if (hasTok)  { fSt->draw_lc(dev, x0, cy + stH * 0.5f, rTok,  zSt, zt_col(ZT_NY_STATS, white), strk, oSt); cy += stH; }
        return;
    }

    // ===== SHEOL / ODYSSEY (mode 5) : Sheol A/B/C header + segment counter + the target's resistances (weapons /
    // elements / Cruel Joke), ported from the SheolHelper addon. =====
    if ((preview || editing) ? (vz == 4) : (party().zone_tracker().mode == 5)) {
        int segs = 0, sheol = 0; bool lastRun = false;
        if (preview || editing) { segs = 1234; sheol = 2; }
        else { const ZoneTracker& zt = party().zone_tracker(); segs = zt.segments; sheol = zt.sheolzone; lastRun = (zt.segLastRun != 0); }
        const bool showSeg = (C.ztSheolSeg != 0);
        // ---- resistances of the current target (cached by name ; recomputed only when the target changes) ----
        static ResData rd; static char rdName[24] = {0};
        bool haveRes = false;
        if (preview || editing) { haveRes = (C.ztSheolRes != 0) && compute_resistances("Tiger", 2, rd); }
        else if (C.ztSheolRes && f.game && f.game->target.valid && f.game->target.spawnType == 0x10 && !lastRun) {
            const char* tn = f.game->target.name;
            if (strcmp(tn, rdName) != 0) { compute_resistances(tn, sheol, rd); int k = 0; for (; tn[k] && k < 23; ++k) rdName[k] = tn[k]; rdName[k] = 0; }
            haveRes = rd.valid;
        }
        const bool showJoke = haveRes && (C.ztSheolJoke != 0) && (rd.jokeVuln >= 0);
        float sscl = C.ztScale; if (sscl < 0.5f) sscl = 0.5f; if (sscl > 2.0f) sscl = 2.0f;
        const float S = (ovS > 0.0f) ? ovS : (screenH / 1000.0f) * sscl;
        const float pad = (ui_config().ztBox.on ? 8.0f : 0.0f) * S, gap = 5.0f * S;   // no box chrome -> hug the content
        const u32 white = 0xFFEAF0FFu, gold = 0xFFE8C55Au, strk = 0xFF000000u, orange = 0xFFEB9660u, green = 0xFF6BE06Bu, red = 0xFFF06060u, dim = 0xFFB4B9C8u;
        Font* fH = zt_font(f, ZT_HEADER);
        const float zH = zt_sz(ZT_HEADER, 15.0f) * S, oH = zt_ow(ZT_HEADER, 1.1f) * S, headH = zH + 6.0f * S;
        // One text element per Sheol group : the segment counter, the family name, the resistance values and the
        // Cruel Joke verdict. The icon / puck sizes are their own factors (the resist rows grow to fit them).
        Font* fSg = zt_font(f, ZT_SH_SEG); Font* fFm = zt_font(f, ZT_SH_FAM);
        Font* fRs = zt_font(f, ZT_SH_RES); Font* fJk = zt_font(f, ZT_SH_JOKE);
        const float zSg = zt_sz(ZT_SH_SEG, 12.0f) * S, zFm = zt_sz(ZT_SH_FAM, 12.0f) * S;
        const float zRs = zt_sz(ZT_SH_RES, 12.0f) * S, zJk = zt_sz(ZT_SH_JOKE, 12.0f) * S;
        const float oSg = zt_ow(ZT_SH_SEG, 1.0f) * S, oFm = zt_ow(ZT_SH_FAM, 1.0f) * S;
        const float oRs = zt_ow(ZT_SH_RES, 1.0f) * S, oJk = zt_ow(ZT_SH_JOKE, 1.0f) * S;
        const bool showFam = (C.ztShFam != 0);
        // physical damage-type ICONS (Slashing/Piercing/Blunt) -- lazy-load the 3-cell atlas ; text fallback if absent.
        const bool wIcons = (weaponTex != 0);
        float icF = C.ztShIcon; if (icF < 0.50f) icF = 0.50f; if (icF > 2.50f) icF = 2.50f;
        float dtF = C.ztShDot;  if (dtF < 0.50f) dtF = 0.50f; if (dtF > 2.50f) dtF = 2.50f;
        const float iconSz = 15.0f * S * icF;
        #define RESCOL(c) ((c) == 1 ? green : (c) == 2 ? red : white)
        char hdr[16]; if (sheol >= 1 && sheol <= 3) sprintf(hdr, "Sheol %c", (char)('A' + sheol - 1)); else sprintf(hdr, "Odyssey");
        char sb[40]; sprintf(sb, "Segments: %d%s", segs, lastRun ? " (last run)" : "");
        // measure : header, segments, then (weapons / element 2-col / joke)
        float contentW = fH->measure(hdr, zH); if (showSeg && fSg->measure(sb, zSg) > contentW) contentW = fSg->measure(sb, zSg);
        char wl[24], vb[12];
        const float dotD = 9.0f * S * dtF, dotR = dotD * 0.5f;   // element colour puck
        // the resist rows must clear the tallest thing they hold (text, weapon icon or puck) once those are scaled up
        float lineH = zRs + 5.0f * S;
        if (wIcons && iconSz + 2.0f * S > lineH) lineH = iconSz + 2.0f * S;
        if (dotD + 2.0f * S > lineH) lineH = dotD + 2.0f * S;
        const float segH = zSg + 5.0f * S, famH = zFm + 5.0f * S, jokeH = zJk + 5.0f * S;
        const float subGap = gap * 2.5f, colGap = gap * 6.0f;   // between the 2 element sub-cols (roomier so adjacent pucks/values don't crowd) / (wider) between weapons and elements
        float wCellW = 0.0f, eValW = 0.0f, eCellW = 0.0f, resW = 0.0f;
        if (haveRes) {
            if (showFam && fFm->measure(rd.family, zFm) > contentW) contentW = fFm->measure(rd.family, zFm);
            float wValW = 0.0f;
            for (int i = 0; i < 3; ++i) { sprintf(vb, "%d%%", rd.weapon[i].pct); const float w = fRs->measure(vb, zRs); if (w > wValW) wValW = w; }
            if (wIcons) wCellW = iconSz + gap * 0.5f + wValW;   // [icon] value%
            else for (int i = 0; i < 3; ++i) { sprintf(wl, "%-9s%4d%%", res_weapon_name(i), rd.weapon[i].pct); const float w = fRs->measure(wl, zRs); if (w > wCellW) wCellW = w; }
            for (int i = 0; i < 8; ++i) { sprintf(vb, "%d%%", rd.elem[i].pct); const float w = fRs->measure(vb, zRs); if (w > eValW) eValW = w; }
            eCellW = dotD + gap * 0.5f + eValW;             // element cell = [colour puck] value%
            resW = wCellW + colGap + eCellW + subGap + eCellW;   // PHYSICAL column | MAGICAL 2-col, side by side
            if (resW + gap * 2.0f > contentW) contentW = resW + gap * 2.0f;   // + breathing room so the right element column never touches the frame
            if (showJoke && fJk->measure("Cruel Joke", zJk) > contentW) contentW = fJk->measure("Cruel Joke", zJk);
        }
        if (contentW < 130.0f * S) contentW = 130.0f * S;
        const float bodyH = (showSeg ? segH : 0.0f)
                          + (haveRes ? ((showFam ? famH : 0.0f) + 4.0f * lineH + (showJoke ? jokeH : 0.0f)) : 0.0f);
        const float boxW = contentW + 2.0f * pad, boxH = pad + (showHdr ? headH + gap : 0.0f) + bodyH + pad;   // bottom margin = pad (was + an extra gap -> too much space under Cruel Joke)
        if (measureOnly) { if (outW) *outW = boxW; if (outH) *outH = boxH; return; }
        float px, py;
        if (ovS > 0.0f) { px = ovX - boxW * 0.5f; py = ovY - boxH * 0.5f; }
        else            { px = C.ztX * screenW - boxW * 0.5f; py = C.ztY * screenH; }
        if (editing) { static EditBox g_ztEditS; box_edit(f, g_ztEditS, EDITBOX_ZONETRACKER, px, py, boxW, boxH, ui_config().ztScale, ui_config().ztX, ui_config().ztY, 1); }
        dColorQuadState(dev);
        const float r0 = 6.0f * S;
        draw_themed_box(dev, f.skin, px, py, boxW, boxH, ui_config().ztBox, 1.0f, S);   // shared themed chrome (frame/transp/theme)
        const float cx = px + boxW * 0.5f, x0 = px + pad;
        float cy = py + pad;
        if (showHdr) { fH->begin(dev); fH->draw_c(dev, cx, cy + headH * 0.5f, hdr, zH, zt_col(ZT_HEADER, orange), strk, oH); cy += headH + gap; }
        if (showSeg) { fSg->begin(dev); fSg->draw_c(dev, cx, cy + segH * 0.5f, sb, zSg, zt_col(ZT_SH_SEG, white), strk, oSg); cy += segH; }
        if (haveRes) {
            if (showFam) { fFm->begin(dev); fFm->draw_c(dev, cx, cy + famH * 0.5f, rd.family, zFm, zt_col(ZT_SH_FAM, orange), strk, oFm); cy += famH; }
            // PHYSICAL (weapons) in a LEFT column | MAGICAL (elements) as a compact 2-col block on the RIGHT: a colour
            // PUCK per element (puck = element colour) + the value% (coloured by resistance). Side by side, top-aligned,
            // the whole block centred. 4 rows tall (weapons fill the first 3).
            const float blockX = cx - resW * 0.5f;
            const float xE = blockX + wCellW + colGap, xE2 = xE + eCellW + subGap;
            if (wIcons) {   // physical : the Slashing/Piercing/Blunt ICON + value% (right-aligned)
                for (int i = 0; i < 3; ++i) draw_icon_cell(dev, weaponTex, blockX, cy + i * lineH + (lineH - iconSz) * 0.5f, iconSz, iconSz, i / 3.0f, (i + 1) / 3.0f);
                fRs->begin(dev);
                for (int i = 0; i < 3; ++i) { sprintf(vb, "%d%%", rd.weapon[i].pct); const float vw = fRs->measure(vb, zRs);
                    fRs->draw_lc(dev, blockX + wCellW - vw, cy + i * lineH + lineH * 0.5f, vb, zRs, RESCOL(rd.weapon[i].color), strk, oRs); }
            } else {        // text fallback (atlas missing)
                for (int i = 0; i < 3; ++i) { sprintf(wl, "%-9s%4d%%", res_weapon_name(i), rd.weapon[i].pct);
                    fRs->begin(dev); fRs->draw_lc(dev, blockX, cy + i * lineH + lineH * 0.5f, wl, zRs, RESCOL(rd.weapon[i].color), strk, oRs); }
            }
            dColorQuadState(dev);                                       // element colour pucks (primitives) first
            for (int r = 0; r < 4; ++r) for (int c = 0; c < 2; ++c) { const int i = r * 2 + c; const float ex = (c == 0) ? xE : xE2;
                disc(dev, ex + dotR, cy + r * lineH + lineH * 0.5f, dotR, res_elem_color(i)); }
            fRs->begin(dev);                                           // then the value%, right-aligned in the cell
            for (int r = 0; r < 4; ++r) for (int c = 0; c < 2; ++c) { const int i = r * 2 + c; const float ex = (c == 0) ? xE : xE2;
                sprintf(vb, "%d%%", rd.elem[i].pct); const float vw = fRs->measure(vb, zRs);
                fRs->draw_lc(dev, ex + eCellW - vw, cy + r * lineH + lineH * 0.5f, vb, zRs, RESCOL(rd.elem[i].color), strk, oRs); }
            cy += 4 * lineH;
            if (showJoke) { fJk->begin(dev); fJk->draw_c(dev, cx, cy + jokeH * 0.5f, "Cruel Joke", zJk, zt_col(ZT_SH_JOKE, rd.jokeVuln ? green : red), strk, oJk); cy += jokeH; }
        }
        #undef RESCOL
        return;
    }

    // ===== LIMBUS (mode 6, Apollyon 38 / Temenos 37) : header + "<Area>  ·  Lv <NNN>". Floor + progress are
    // Floor line + gauge come from the 0x075 bar array (game-data/zone-tracker.md) ; each is drawn only when known. =====
    if ((preview || editing) ? (vz == 5) : (party().zone_tracker().mode == 6)) {
        char area[16] = {0}, quad[8] = {0};
        int level = 0, progress = -1, floor = -1, tem = -1, apo = -1, runUnits = 0, weekLeft = -1;
        int areaIdx = 0; unsigned char slotK[4] = {0};      // 0 = Apollyon, 1 = Temenos ; slotK = payout in k per quadrant
        if (preview || editing) { const char* a = "Apollyon"; int k = 0; for (; a[k] && k < 15; ++k) area[k] = a[k]; area[k] = 0;
                                  level = 119; quad[0] = 'S'; quad[1] = 'W'; floor = 3; progress = 62; tem = 1200; apo = 103380;
                                  runUnits = 3450; weekLeft = 2;
                                  slotK[0] = 3; slotK[1] = 3; }                          // NW5 + SW4 done at 3k
        else {
            const ZoneTracker& zt = party().zone_tracker();
            int k = 0; for (; zt.limbusArea[k] && k < 15; ++k) area[k] = zt.limbusArea[k]; area[k] = 0;
            int q = 0; for (; zt.limbusQuad[q] && q < 7; ++q) quad[q] = zt.limbusQuad[q]; quad[q] = 0;
            level = zt.limbusLevel; progress = zt.limbusProgress; floor = zt.limbusFloor;
            tem = zt.limbusTemenos; apo = zt.limbusApollyon;
            if (zt.limbusUnits >= 0) apo = zt.limbusUnits;                 // live total from 0x02A beats the stale 0x118
            runUnits = zt.limbusRunUnits; weekLeft = zt.limbusWeekLeft;
            areaIdx = (zt.curZone == 37) ? 1 : 0;                                        // Apollyon / Temenos kept apart
            const LimbusCoffers& lc = party().limbus_coffers(areaIdx);
            for (int i = 0; i < 4; ++i) slotK[i] = lc.slotK[i];
        }
        if (!area[0]) { const char* a = "Limbus"; int k = 0; for (; a[k] && k < 15; ++k) area[k] = a[k]; area[k] = 0; }   // name not seen yet
        float sscl = C.ztScale; if (sscl < 0.5f) sscl = 0.5f; if (sscl > 2.0f) sscl = 2.0f;
        const float S = (ovS > 0.0f) ? ovS : (screenH / 1000.0f) * sscl;
        const float pad = (ui_config().ztBox.on ? 8.0f : 0.0f) * S, gap = 5.0f * S;   // no box chrome -> hug the content
        const u32 white = 0xFFEAF0FFu, strk = 0xFF000000u, orange = 0xFFEB9660u, dim = 0xFFB4B9C8u;
        Font* fH = zt_font(f, ZT_HEADER); Font* fB = zt_font(f, ZT_BODY);
        const float zH = zt_sz(ZT_HEADER, 15.0f) * S, zB = zt_sz(ZT_BODY, 12.0f) * S;
        const float oH = zt_ow(ZT_HEADER, 1.1f) * S, oB = zt_ow(ZT_BODY, 1.0f) * S;
        // Each Limbus row owns a text element, so its size / outline / colour is configurable on its own, and a
        // visibility toggle so it can be hidden one by one (Config -> Zone Tracker -> Display).
        Font* fG = zt_font(f, ZT_LB_GAUGE); Font* fC = zt_font(f, ZT_LB_CUR);
        Font* fR = zt_font(f, ZT_LB_RUN);   Font* fK = zt_font(f, ZT_LB_CHIP);
        const float zG = zt_sz(ZT_LB_GAUGE, 12.0f) * S, zC = zt_sz(ZT_LB_CUR, 12.0f) * S;
        const float zR = zt_sz(ZT_LB_RUN,   12.0f) * S, zK = zt_sz(ZT_LB_CHIP, 10.0f) * S;
        const float oG = zt_ow(ZT_LB_GAUGE, 1.0f) * S,  oC = zt_ow(ZT_LB_CUR, 1.0f) * S;
        const float oR = zt_ow(ZT_LB_RUN,   1.0f) * S,  oK = zt_ow(ZT_LB_CHIP, 1.0f) * S;
        float bwF = C.ztLbBarW; if (bwF < 0.40f) bwF = 0.40f; if (bwF > 1.00f) bwF = 1.00f;   // gauge width, fraction of content
        float bhF = C.ztLbBarH; if (bhF < 0.50f) bhF = 0.50f; if (bhF > 2.50f) bhF = 2.50f;   // gauge height multiplier
        const float lineH = zB + 5.0f * S, headH = zH + 6.0f * S, barH = (zG + 6.0f * S) * bhF;
        const bool hasName = (C.ztLbName != 0);
        const float curLineH = zC + 5.0f * S, runLineH = zR + 5.0f * S;
        // The area name is drawn SEPARATELY from the level so it can carry the accent colour ; nameLine stays
        // whole for the width measurement. Two draw_lv calls keep the pair centred as one unit.
        char nameRest[24] = {0}; if (level > 0) snprintf(nameRest, sizeof(nameRest), "  \xC2\xB7  Lv %d", level);
        char nameLine[48]; snprintf(nameLine, sizeof(nameLine), "%.15s%s", area, nameRest);
        // The floor rides ON the gauge ("SW #3 · 62%") instead of owning a row : same information, one line less.
        char pctLine[32] = {0};
        if (progress >= 0) {
            if      (C.ztLbFloor && floor >= 0 && quad[0]) snprintf(pctLine, sizeof(pctLine), "%.7s #%d  \xC2\xB7  %d%%", quad, floor, progress);
            else if (C.ztLbFloor && floor >= 0)            snprintf(pctLine, sizeof(pctLine), "#%d  \xC2\xB7  %d%%", floor, progress);
            else                                           snprintf(pctLine, sizeof(pctLine), "%d%%", progress);
        }
        // Kept as THREE pieces so the currency of the zone you stand in carries the accent while the other stays
        // faded. Sized for the worst case and written with snprintf : the totals run to 6 digits and each
        // "\xC2\xB7" costs TWO bytes, which overflowed a 32-byte buffer here (103464 rendered as "1034").
        char curTv[24] = {0}, curSep[8] = {0}, curAv[24] = {0};
        const bool hasCur = C.ztLbCur && (tem >= 0 || apo >= 0);
        // Show ONLY what is known. A currency we have never received (no 0x118 since load -- it fires on zoning,
        // never mid-run) must not be rendered as "0" : that reads as a real balance of zero.
        if (tem >= 0) snprintf(curTv, sizeof(curTv), " %d", tem);
        if (apo >= 0) snprintf(curAv, sizeof(curAv), " %d", apo);
        if (curTv[0] && curAv[0]) snprintf(curSep, sizeof(curSep), "  \xC2\xB7  ");
        // ALWAYS shown, zero included : "+0" is a real answer (nothing banked yet this run), unlike the currencies
        // where a missing 0x118 means UNKNOWN and must not be printed as a balance.
        char runLine[64] = {0};
        int rw = snprintf(runLine, sizeof(runLine), "Run  +%d", runUnits < 0 ? 0 : runUnits);
        if (weekLeft >= 0)  snprintf(runLine + rw, sizeof(runLine) - rw, "   \xC2\xB7   %d run%s left", weekLeft, weekLeft == 1 ? "" : "s");
        const bool hasRun = C.ztLbRun && runLine[0] != 0;
        // No "Data <amt>" / "Last 5000" rows : with only two payouts in play the dot row already says it (red = 3k,
        // green = 5k, label = quadrant). FOUR fixed slots -- the area's quadrants, each holding one coffer on its
        // last floor -- so the row doubles as a checklist. ROUND dots (rrect with w == h and r = h/2).
        const int nSlots = 4;
        const bool hasChip = (C.ztLbChips != 0);
        const float dotD = 10.0f * S, chipGap = 8.0f * S, chipLblZ = zK;
        float slotW[4] = {0}; float chipRowW = 0.0f;
        for (int i = 0; i < nSlots; ++i) {
            const float lw = fK->measure(limbus_slot_label(areaIdx, i), chipLblZ);
            slotW[i] = (lw > dotD) ? lw : dotD;
            chipRowW += slotW[i] + (i ? chipGap : 0.0f);
        }
        const float chipBlockH = dotD + 4.0f * S + chipLblZ + 3.0f * S;
        float contentW = fH->measure("Limbus", zH);
        if (fB->measure(nameLine, zB) > contentW) contentW = fB->measure(nameLine, zB);
        const float curW = hasCur ? (fC->measure(curTv[0] ? "Temenos" : "", zC) + fC->measure(curTv, zC) + fC->measure(curSep, zC)
                                   + fC->measure(curAv[0] ? "Apollyon" : "", zC) + fC->measure(curAv, zC)) : 0.0f;
        if (curW > contentW) contentW = curW;
        if (hasRun && fR->measure(runLine, zR) > contentW) contentW = fR->measure(runLine, zR);
        if (hasChip && chipRowW > contentW) contentW = chipRowW;
        if (contentW < 130.0f * S) contentW = 130.0f * S;
        const float boxW = contentW + 2.0f * pad;
        const float boxH = pad + (showHdr ? headH + gap : 0.0f) + (hasName ? lineH : 0.0f)
                         + (progress >= 0 ? barH + gap : 0.0f)
                         + (hasCur ? curLineH : 0.0f) + (hasRun ? runLineH : 0.0f)
                         + (hasChip ? chipBlockH : 0.0f) + pad;
        if (measureOnly) { if (outW) *outW = boxW; if (outH) *outH = boxH; return; }
        float px, py;
        if (ovS > 0.0f) { px = ovX - boxW * 0.5f; py = ovY - boxH * 0.5f; }
        else            { px = C.ztX * screenW - boxW * 0.5f; py = C.ztY * screenH; }
        if (editing) { static EditBox g_ztEditL; box_edit(f, g_ztEditL, EDITBOX_ZONETRACKER, px, py, boxW, boxH, ui_config().ztScale, ui_config().ztX, ui_config().ztY, 1); }
        dColorQuadState(dev);
        draw_themed_box(dev, f.skin, px, py, boxW, boxH, ui_config().ztBox, 1.0f, S);   // shared themed chrome (frame/transp/theme)
        const float cx = px + boxW * 0.5f, x0 = px + pad;
        float cy = py + pad;
        if (showHdr) { fH->begin(dev); fH->draw_c(dev, cx, cy + headH * 0.5f, "Limbus", zH, zt_col(ZT_HEADER, orange), strk, oH); cy += headH + gap; }
        if (hasName) { fB->begin(dev); fB->draw_c(dev, cx, cy + lineH * 0.5f, nameLine, zB, zt_col(ZT_BODY, white), strk, oB); cy += lineH; }
        if (progress >= 0) {                                                            // the client's own gauge value (percent)
            const float br = barH * 0.5f, fr = progress > 100 ? 1.0f : progress * 0.01f;
            const float barW = contentW * bwF, bx0 = cx - barW * 0.5f;   // centred, width configurable
            dColorQuadState(dev);
            rrect(dev, bx0, cy, barW, barH, br, 0xFF20222Cu, 0xFF16181Fu, 1.0f);
            const float fw = (barW - 2.0f * S) * fr;
            if (fw > 1.0f) { const u32 tc = zt_time_col(fr);
                if (fw >= barW - 2.0f * S - 0.5f) rrect(dev, bx0 + 1.0f * S, cy + 1.0f * S, fw, barH - 2.0f * S, br - 1.0f * S, tc, tc, 1.0f);
                else rrect_left(dev, bx0 + 1.0f * S, cy + 1.0f * S, fw, barH - 2.0f * S, br - 1.0f * S, tc, tc, 1.0f); }
            fG->begin(dev); fG->draw_c(dev, cx, cy + barH * 0.5f, pctLine, zG, zt_col(ZT_LB_GAUGE, white), strk, oG); cy += barH + gap;
        }
        if (hasCur) {   // only the NAME of the currency you are earning right now takes the accent ; amounts stay neutral
            // The currency you are NOT earning is faded out whole (name AND amount) rather than hidden : it stays
            // readable when you want it, without competing with the one that matters right now.
            fC->begin(dev);
            const u32 cVal = zt_col(ZT_LB_CUR, white), cHere = zt_col(ZT_HEADER, orange);
            const u32 cFade = (zt_col(ZT_LB_CUR, dim) & 0x00FFFFFFu) | 0x66000000u;
            const bool inTem = (areaIdx == 1);
            const float ccy = cy + curLineH * 0.5f;
            float nx = cx - curW * 0.5f;
            if (curTv[0]) {
                nx += fC->draw_lv(dev, nx, ccy, "Temenos", zC, inTem ? cHere : cFade, strk, oC);
                nx += fC->draw_lv(dev, nx, ccy, curTv, zC, inTem ? cVal : cFade, strk, oC);
            }
            if (curSep[0]) nx += fC->draw_lv(dev, nx, ccy, curSep, zC, cFade, strk, oC);
            if (curAv[0]) {
                nx += fC->draw_lv(dev, nx, ccy, "Apollyon", zC, inTem ? cFade : cHere, strk, oC);
                fC->draw_lv(dev, nx, ccy, curAv, zC, inTem ? cFade : cVal, strk, oC);
            }
            cy += curLineH;
        }
        if (hasRun) { fR->begin(dev); fR->draw_c(dev, cx, cy + runLineH * 0.5f, runLine, zR, zt_col(ZT_LB_RUN, white), strk, oR); cy += runLineH; }
        if (hasChip) {                                     // the four quadrant coffers : round dot + fixed label
            dColorQuadState(dev);                          // leaving font state bound here would poison the next quad
            const u32 EMPTY = 0x30FFFFFFu, RED = 0xFFE0605Cu, GREEN = 0xFF5CD07Cu;
            float bx = cx - chipRowW * 0.5f;
            for (int i = 0; i < nSlots; ++i) {
                const u32 c = !slotK[i] ? EMPTY : ((slotK[i] >= 5) ? GREEN : RED);
                rrect(dev, bx + (slotW[i] - dotD) * 0.5f, cy + 2.0f * S, dotD, dotD, dotD * 0.5f, c, c, 1.0f);
                bx += slotW[i] + chipGap;
            }
            fK->begin(dev);
            bx = cx - chipRowW * 0.5f;
            // No accent on the quadrant you are in : the gauge already says it ("SW #3 - 62%"). The only thing the
            // labels encode is opened (bright) vs not (dim).
            const float lby = cy + dotD + 6.0f * S + chipLblZ * 0.5f;
            for (int i = 0; i < nSlots; ++i) {
                fK->draw_c(dev, bx + slotW[i] * 0.5f, lby, limbus_slot_label(areaIdx, i), chipLblZ,
                           zt_col(ZT_LB_CHIP, slotK[i] ? white : dim), strk, oK);
                bx += slotW[i] + chipGap;
            }
            cy += chipBlockH;
        }
        return;
    }

    int mode; int remainSec = 0, limitSec = 1; unsigned char ki[5] = {0}; int lights[7] = {0}; int visRemainSec = 0, visMax = 7200;
    if (preview || editing) {
        mode = (vz == 0) ? 1 : 2;
        if (mode == 1) { limitSec = 3600; remainSec = 3510; unsigned char k[5] = {1,1,0,0,1}; for (int i = 0; i < 5; ++i) ki[i] = k[i]; }
        else { int l[7] = {180,240,90,200,150,60,30}; for (int i = 0; i < 7; ++i) lights[i] = l[i]; visRemainSec = 18 * 60 + 42; }
    } else {
        const ZoneTracker& zt = party().zone_tracker();
        if (zt.mode == 0) return;
        mode = zt.mode;
        const unsigned now = GetTickCount();
        if (mode == 1) {
            limitSec = zt.dynLimitSec > 0 ? zt.dynLimitSec : 1;
            remainSec = limitSec - (int)((now - zt.dynEntryMs) / 1000u); if (remainSec < 0) remainSec = 0;
            for (int i = 0; i < 5; ++i) ki[i] = zt.ki[i];
        } else {
            for (int i = 0; i < 7; ++i) lights[i] = zt.lights[i];
            visRemainSec = zt.visitantMin * 60 - (int)((now - zt.visitantMs) / 1000u); if (visRemainSec < 0) visRemainSec = 0;
        }
    }

    float sscl = C.ztScale; if (sscl < 0.5f) sscl = 0.5f; if (sscl > 2.0f) sscl = 2.0f;
    const float S = (ovS > 0.0f) ? ovS : (screenH / 1000.0f) * sscl;
    const float pad = (ui_config().ztBox.on ? 8.0f : 0.0f) * S, gap = 6.0f * S;   // no box chrome -> hug the content
    const u32 white = 0xFFEAF0FFu, gold = 0xFFE8C55Au, strk = 0xFF000000u, orange = 0xFFEB9660u;
    Font* fH = zt_font(f, ZT_HEADER);
    const float zH = zt_sz(ZT_HEADER, 15.0f) * S, oH = zt_ow(ZT_HEADER, 1.1f) * S, headH = zH + 6.0f * S;
    // Dynamis and Abyssea share this tail but NOT their elements : each owns its timer caption, its body text
    // and its own bar/dot size factors, so the two zones can be styled apart.
    const bool isDyn = (mode == 1);
    const int eTimer = isDyn ? ZT_DY_TIMER : ZT_AB_TIMER;
    Font* fT = zt_font(f, eTimer);
    const float zT = zt_sz(eTimer, 12.0f) * S, oT = zt_ow(eTimer, 1.0f) * S;
    float bwF = isDyn ? C.ztDyBarW : C.ztAbBarW; if (bwF < 0.40f) bwF = 0.40f; if (bwF > 1.00f) bwF = 1.00f;
    float bhF = isDyn ? C.ztDyBarH : C.ztAbBarH; if (bhF < 0.50f) bhF = 0.50f; if (bhF > 2.50f) bhF = 2.50f;
    const float barH = (zT + 6.0f * S) * bhF;
    const bool hasTimer = isDyn ? (C.ztDyTimer != 0) : (C.ztAbTimer != 0);
    const bool hasBody  = isDyn ? (C.ztDyKi != 0)    : (C.ztAbLights != 0);

    // ---- measure -> content size (per mode) ----
    static const char* KI_NM[5] = { "Crimson", "Azure", "Amber", "Alabaster", "Obsidian" };
    static const char* LT_AB[7] = { "Pe", "Az", "Ru", "Am", "Go", "Si", "Eb" };
    static const u32    LT_CO[7] = { 0xFFEBEBF5u, 0xFF5096FFu, 0xFFE63C3Cu, 0xFFE6B43Cu, 0xFFFFD764u, 0xFFB4BEC8u, 0xFF8C78AAu };
    static const int    LT_CAP[7] = { 230, 255, 255, 255, 200, 200, 200 };
    const char* title = isDyn ? "Dynamis" : "Abyssea";
    // Dynamis : key-item rows (dot + name).  Abyssea : the 7 light columns (label / bar / value).
    Font* fK = zt_font(f, ZT_DY_KI);    const float zK = zt_sz(ZT_DY_KI, 12.0f) * S,   oK = zt_ow(ZT_DY_KI, 1.0f) * S;
    Font* fL = zt_font(f, ZT_AB_LIGHT); const float zL = zt_sz(ZT_AB_LIGHT, 12.0f) * S, oL = zt_ow(ZT_AB_LIGHT, 1.0f) * S;
    Font* fV = zt_font(f, ZT_AB_VAL);   const float zV = zt_sz(ZT_AB_VAL, 10.32f) * S,  oV = zt_ow(ZT_AB_VAL, 1.0f) * S;
    float dsF = C.ztDyDot;    if (dsF < 0.50f) dsF = 0.50f; if (dsF > 2.50f) dsF = 2.50f;
    float lwF = C.ztAbLightW; if (lwF < 0.50f) lwF = 0.50f; if (lwF > 3.00f) lwF = 3.00f;
    float lhF = C.ztAbLightH; if (lhF < 0.50f) lhF = 0.50f; if (lhF > 2.50f) lhF = 2.50f;
    const float dotD = 8.0f * S * dsF, lightH = 42.0f * S * lhF, lightBarW = 8.0f * S * lwF;
    float colW = 22.0f * S; if (lightBarW + 8.0f * S > colW) colW = lightBarW + 8.0f * S;   // column widens with its bar
    float kiRowH = zK + 5.0f * S; if (dotD + 2.0f * S > kiRowH) kiRowH = dotD + 2.0f * S;   // row clears a grown dot
    float contentW;
    if (isDyn) {
        float w = fK->measure("Alabaster", zK) + gap + dotD;   // widest KI row
        if (fH->measure(title, zH) > w) w = fH->measure(title, zH);
        if (w < 96.0f * S) w = 96.0f * S;
        contentW = w;
    } else {
        contentW = 7.0f * colW + 6.0f * (gap * 0.5f);
        if (fH->measure(title, zH) > contentW) contentW = fH->measure(title, zH);
    }
    const float bodyH = isDyn ? (5.0f * kiRowH) : (lightH + zL + 4.0f * S);
    const float boxW = contentW + 2.0f * pad;
    const float boxH = pad + (showHdr ? headH + gap : 0.0f) + (hasTimer ? barH + gap : 0.0f) + (hasBody ? bodyH : 0.0f) + pad;
    if (measureOnly) { if (outW) *outW = boxW; if (outH) *outH = boxH; return; }

    // ---- position (+ edit drag) ----
    float px, py;
    if (ovS > 0.0f) { px = ovX - boxW * 0.5f; py = ovY - boxH * 0.5f; }
    else            { px = C.ztX * screenW - boxW * 0.5f; py = C.ztY * screenH; }
    if (editing) { static EditBox g_ztEdit; box_edit(f, g_ztEdit, EDITBOX_ZONETRACKER, px, py, boxW, boxH, ui_config().ztScale, ui_config().ztX, ui_config().ztY, 1); }

    // ---- chrome ----
    dColorQuadState(dev);
    const float r0 = 6.0f * S;
    draw_themed_box(dev, f.skin, px, py, boxW, boxH, ui_config().ztBox, 1.0f, S);   // shared themed chrome (frame/transp/theme)

    const float cx = px + boxW * 0.5f, x0 = px + pad;
    float cy = py + pad;
    // header
    if (showHdr) { fH->begin(dev); fH->draw_c(dev, cx, cy + headH * 0.5f, title, zH, zt_col(ZT_HEADER, orange), strk, oH); cy += headH + gap; }
    // time bar (run timer / visitant timer) -- width + height configurable, centred
    if (hasTimer) {
        const int tsec = isDyn ? remainSec : visRemainSec;
        const int tmax = isDyn ? limitSec : (visMax > 0 ? visMax : 1);
        const float frac = (float)tsec / (float)tmax;
        const float br = barH * 0.5f, bW = contentW * bwF, bX = cx - bW * 0.5f;
        rrect(dev, bX, cy, bW, barH, br, 0xFF20222Cu, 0xFF16181Fu, 1.0f);
        const float fw = (bW - 2.0f * S) * (frac < 0.0f ? 0.0f : (frac > 1.0f ? 1.0f : frac));
        const u32 tc = zt_time_col(frac);
        if (fw > 1.0f) { if (fw >= bW - 2.0f * S - 0.5f) rrect(dev, bX + 1.0f * S, cy + 1.0f * S, fw, barH - 2.0f * S, br - 1.0f * S, tc, tc, 1.0f);
                         else rrect_left(dev, bX + 1.0f * S, cy + 1.0f * S, fw, barH - 2.0f * S, br - 1.0f * S, tc, tc, 1.0f); }
        char tb[12]; sprintf(tb, "%d:%02d", tsec / 60, tsec % 60);
        fT->begin(dev); fT->draw_c(dev, cx, cy + barH * 0.5f, tb, zT, zt_col(eTimer, white), strk, oT);
        cy += barH + gap;
    }
    // body
    if (hasBody && isDyn) {   // 5 Key-Item rows : coloured dot (green owned / red missing) + name
        char nb[16];
        for (int i = 0; i < 5; ++i) {
            const float ry = cy + kiRowH * 0.5f;
            const u32 dc = ki[i] ? 0xFF6BE06Bu : 0xFFF06060u;
            dColorQuadState(dev); rrect(dev, x0, ry - dotD * 0.5f, dotD, dotD, dotD * 0.5f, dc, dc, 1.0f);
            const char* nm = KI_NM[i]; int c2 = 0; for (; nm[c2] && c2 < 15; ++c2) nb[c2] = nm[c2]; nb[c2] = 0;
            fK->begin(dev); fK->draw_lc(dev, x0 + dotD + gap, ry, nb, zK, zt_col(ZT_DY_KI, ki[i] ? white : 0xFFB4B9C8u), strk, oK);
            cy += kiRowH;
        }
    } else if (hasBody) {   // 7 vertical light bars : label (top) + bar (value/cap) + value (bottom)
        const float step = (contentW - colW) / 6.0f;
        for (int i = 0; i < 7; ++i) {
            const float lcx = x0 + colW * 0.5f + step * i;
            fL->begin(dev); fL->draw_c(dev, lcx, cy + zL * 0.5f, LT_AB[i], zL, zt_col(ZT_AB_LIGHT, LT_CO[i]), strk, oL);
            const float bx = lcx - lightBarW * 0.5f, byy = cy + zL + 3.0f * S, bw = lightBarW, bh = lightH - zL - 3.0f * S;
            dColorQuadState(dev);
            rrect(dev, bx, byy, bw, bh, bw * 0.5f, 0xFF20222Cu, 0xFF16181Fu, 1.0f);
            float fr = (float)lights[i] / (float)LT_CAP[i]; if (fr < 0.0f) fr = 0.0f; if (fr > 1.0f) fr = 1.0f;
            const float fh = (bh - 2.0f * S) * fr;
            if (fh > 1.0f) rrect(dev, bx + 1.0f * S, byy + bh - 1.0f * S - fh, bw - 2.0f * S, fh, (bw - 2.0f * S) * 0.5f, LT_CO[i], LT_CO[i], 1.0f);
            char vb[8]; sprintf(vb, "%d", lights[i]);
            fV->begin(dev); fV->draw_c(dev, lcx, cy + lightH + zL * 0.5f - 1.0f * S, vb, zV, zt_col(ZT_AB_VAL, white), strk, oV);
        }
    }
}

// Live / edit path : the Hud draws the box at its configured screen position (lazy-loads the Odyssey weapon icons).
void Hud::draw_zonetracker(const Frame& f, bool preview, float ovX, float ovY, float ovS) {
    if (!weaponIconsTried_) { weaponIcons_ = load_raw_texture(f.dev, WEAPON_ICONS_PATH(), 96, 32); weaponIconsTried_ = true; }
    zonetracker_draw(f, preview, ovX, ovY, ovS, (float)screenW_, (float)screenH_, weaponIcons_);
}

// The Help sample owns its own copy of the Odyssey weapon-icons sheet (lazy) so it can draw without a Hud.
static u32 zonetracker_help_weapons(u32 dev) {
    static u32 tex = 0; static bool tried = false;
    if (!tried) { tex = load_raw_texture(dev, WEAPON_ICONS_PATH(), 96, 32); tried = true; }
    return tex;
}

// Help sample : the REAL Zone Tracker box in preview mode for a FORCED content `variant` (config-aware), centred.
void zonetracker_help_box(const Frame& f, float cx, float cy, float s, int variant) {
    zonetracker_draw(f, true, cx, cy, s, 0.0f, 0.0f, zonetracker_help_weapons(f.dev), false, 0, 0, variant);
}

// Help : box dimensions of one variant at scale 1 (linear in S -> caller multiplies by its scale). For grid layout.
void zonetracker_help_measure(const Frame& f, int variant, float& outW, float& outH) {
    outW = 0.0f; outH = 0.0f;
    zonetracker_draw(f, true, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, zonetracker_help_weapons(f.dev), true, &outW, &outH, variant);
}

// Help scale-to-fit for one variant : measure at scale 1 (linear in S), pick the largest scale that fits availW.
void zonetracker_help_fit(const Frame& f, float availW, float maxScale, int variant, float& outScale, float& outH) {
    float bw = 0.0f, bh = 0.0f;
    zonetracker_draw(f, true, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, zonetracker_help_weapons(f.dev), true, &bw, &bh, variant);
    float s = (bw > 1.0f) ? (availW / bw) : maxScale;
    if (s > maxScale) s = maxScale; if (s < 0.6f) s = 0.6f;
    outScale = s; outH = bh * s;
}

} // namespace aio
