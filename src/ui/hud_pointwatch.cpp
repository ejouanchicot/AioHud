// hud_pointwatch.cpp -- split out of hud.cpp (pure move). PointWatch box renderer.
#include "ui/hud.h"
#include "ui/hud_internal.h"
#include "model/ui_config.h"
#include "ui/text_style.h"
#include "ui/box_style.h"
#include "model/party_state.h"
#include "gfx/draw.h"
#include "gfx/d3d.h"

#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>

namespace aio {

// ============================ POINTWATCH box ============================
// per-element typography for PointWatch (Label / Value / Rate), like the other modules' helpers.
static Font* pw_font(const Frame& f, int e) { return te_font(f, ui_config().pwText[e]); }
static inline float pw_sz(int e, float base) { return te_sz(ui_config().pwText[e], base); }
static inline float pw_ow(int e, float base) { return te_ow(ui_config().pwText[e], base); }
static inline u32   pw_col(int e, u32 base)  { return te_col(ui_config().pwText[e], base); }
static const char*  pw_up(int e, const char* s, char* buf, int cap) { return te_upper(ui_config().pwText[e], s, buf, cap); }
// number -> compact "6.8k" (pwcore's pw_kf : k with no decimal when whole, else one decimal).
static const char* pw_kf(unsigned n, char* b) {
    if (n >= 1000) { double k = n / 1000.0; if (k == (double)(long)k) sprintf(b, "%ldk", (long)k); else sprintf(b, "%.1fk", k); }
    else sprintf(b, "%u", n);
    return b;
}

// ONE progression bar by job stage (< 99 XP / 99 CP / master ML) + Merits always, each with its value + X/h rate,
// from party().pointwatch() (packet-fed). A representative sample in preview / edit. Placed via //aio edit.
// Core renderer, extracted as a FREE function so the config PREVIEW and the Help sample reuse the EXACT same
// config-aware draw (layout / text styles / bars / rate slot) with no Hud instance.
void pointwatch_draw(const Frame& f, bool preview, float ovX, float ovY, float ovS, float screenW, float screenH,
                     bool measureOnly = false, float* outW = 0, float* outH = 0, bool helpSample = false) {
    const UiConfig& C = ui_config();
    if (!C.pwShow) return;
    const bool editing = C.editLayout && !preview;
    u32 dev = f.dev;
    if (!f.font && !f.fonts) return;

    const u32 cGreen = 0xFF78CD78u, cBlue = 0xFF50AFEBu, cOrange = 0xFFFF962Du, cPurple = 0xFFBE8CE1u;
    const u32 cVal = 0xFFF0F0F8u, cJp = 0xFF78AAC8u, cPool = 0xFFAA8CC8u, cRate = 0xFF82E182u, strk = 0xFF000000u;
    struct PWRow { char label[12]; u32 lcol; unsigned cur, mx; char val[28]; char extra[16]; u32 extraCol; char rate[12]; unsigned char rateSlot; };
    PWRow rows[2]; int nrows = 0;
    char kb1[12], kb2[12];

    if (preview || editing) {
        // //aio edit uses the WIDEST realistic values so the box you place reserves the max in-game footprint
        // (it never grows past this in live) : max master level, near-cap EP, a high rate, full merits.
        { PWRow& r = rows[nrows]; strcpy(r.label, helpSample ? "ML30" : "ML50"); r.lcol = cOrange; r.cur = 999800; r.mx = 999900; strcpy(r.val, helpSample ? "45.2k/50k EP" : "999.9k/999.9k EP"); r.extra[0] = 0; r.extraCol = cVal;
          if (C.pwRate) { strcpy(r.rate, helpSample ? "2.4k/h" : "999.9k/h"); r.rateSlot = 1; } else { r.rate[0] = 0; r.rateSlot = 0; }   // mirror the live 'Show rate' toggle so //aio edit == in-game
          ++nrows; }
        { PWRow& r = rows[nrows]; strcpy(r.label, "Merits"); r.lcol = cPurple; r.cur = 9900; r.mx = 10000; strcpy(r.val, "9.9k/10k"); strcpy(r.extra, helpSample ? "12/15" : "75/75"); r.extraCol = cPool; r.rate[0] = 0; r.rateSlot = 0; ++nrows; }
    } else {
        const PointWatch& pw = party().pointwatch();
        if (!pw.valid && pw.jobLevel <= 0) return;   // no data at all yet (not logged in) -> hide
        int mode = C.pwMode;
        if (mode == 0) { if (pw.jobLevel > 0 && pw.jobLevel < 99) mode = 1; else if (pw.masterLevel < 1) mode = 2; else mode = 3; }
        if (mode == 1) {   // XP
            PWRow& r = rows[nrows]; strcpy(r.label, "XP"); r.lcol = cGreen; r.cur = pw.xpCur; r.mx = pw.xpTnl ? pw.xpTnl : 1;
            sprintf(r.val, "%s/%s", pw_kf(pw.xpCur, kb1), pw_kf(pw.xpTnl, kb2)); r.extra[0] = 0; r.extraCol = cVal;
            const int rt = pw.xpReg.rate(); if (C.pwRate && rt > 0) sprintf(r.rate, "%.1fk/h", (double)(rt / 100) / 10.0); else r.rate[0] = 0; ++nrows;
        } else if (mode == 2) {   // CP
            PWRow& r = rows[nrows]; strcpy(r.label, "CP"); r.lcol = cBlue; r.cur = pw.cpCur; r.mx = 30000;
            sprintf(r.val, "%s/%s", pw_kf(pw.cpCur, kb1), pw_kf(30000, kb2)); sprintf(r.extra, "JP%d", pw.cpJp); r.extraCol = cJp;
            const int rt = pw.cpReg.rate(); if (C.pwRate && rt > 0) sprintf(r.rate, "%.1fk/h", (double)(rt / 100) / 10.0); else r.rate[0] = 0; ++nrows;
        } else {   // ML / EP
            PWRow& r = rows[nrows]; sprintf(r.label, "ML%d", pw.masterLevel); r.lcol = cOrange; r.cur = pw.epCur; r.mx = pw.epTnml ? pw.epTnml : 1;
            sprintf(r.val, "%s/%s EP", pw_kf(pw.epCur, kb1), pw_kf(pw.epTnml, kb2)); r.extra[0] = 0; r.extraCol = cVal;
            const int rt = pw.epReg.rate(); if (C.pwRate && rt > 0) sprintf(r.rate, "%.1fk/h", (double)(rt / 100) / 10.0); else r.rate[0] = 0; ++nrows;
        }
        { PWRow& r = rows[nrows]; strcpy(r.label, "Merits"); r.lcol = cPurple; r.cur = pw.lpCur; r.mx = 10000;   // merits always
            sprintf(r.val, "%s/%s", pw_kf(pw.lpCur, kb1), pw_kf(10000, kb2)); sprintf(r.extra, "%d/%d", pw.merits, pw.maxMerits); r.extraCol = cPool; r.rate[0] = 0; ++nrows; }
        // rate-slot reservation : ONLY the progression stat keeps a fixed X/h slot (so gaining<->idle doesn't resize
        // the box) ; Merits never has a rate. Set after building so it survives the readiness overrides below.
        rows[0].rateSlot = (C.pwRate != 0) ? 1 : 0;
        if (nrows > 1) rows[1].rateSlot = 0;
        // per-row readiness : every stat now fills straight from client memory (xpMem / cpMem / epMem / merMem)
        // on load -> "..." only shows in the brief window before the first memory read resolves.
        const bool progReady = (mode == 1) ? (pw.valid || pw.xpMem) : (mode == 2) ? (pw.valid || pw.cpMem) : (pw.valid || pw.epMem);
        if (!progReady) { strcpy(rows[0].val, "..."); rows[0].extra[0] = 0; rows[0].rate[0] = 0; rows[0].cur = 0; rows[0].mx = 1; }
        if (nrows > 1 && !(pw.valid || pw.merMem)) { strcpy(rows[1].val, "..."); rows[1].extra[0] = 0; rows[1].rate[0] = 0; rows[1].cur = 0; rows[1].mx = 1; }
    }
    if (nrows == 0) return;

    float sscl = C.pwScale; if (sscl < 0.5f) sscl = 0.5f; if (sscl > 2.0f) sscl = 2.0f;
    const float S = (ovS > 0.0f) ? ovS : (screenH / 1000.0f) * sscl;
    // when the box chrome (frame + background) is OFF, drop the padding so the box hugs the bare text/bars exactly
    // -> no phantom margin, and the anchor (pwX centre / pwY top) sits on the visible content for precise placement.
    const float pad = (C.pwBox.on ? 6.0f : 0.0f) * S, gap = 6.0f * S;
    Font* fLabel = pw_font(f, PW_LABEL); Font* fVal = pw_font(f, PW_VALUE); Font* fRate = pw_font(f, PW_RATE);
    const float zLabel = pw_sz(PW_LABEL, 12.0f) * S, zVal = pw_sz(PW_VALUE, 11.5f) * S, zRate = pw_sz(PW_RATE, 11.0f) * S;
    const float oLabel = pw_ow(PW_LABEL, 1.0f) * S, oVal = pw_ow(PW_VALUE, 1.0f) * S, oRate = pw_ow(PW_RATE, 1.0f) * S;
    const float barH = 6.0f * S, headH = (zLabel > zVal ? zLabel : zVal) + 4.0f * S, rowGap = 6.0f * S;

    // ---- measure content width (each row : label + value [+ extra] .... rate) ----
    // the min bar width scales with the TEXT size too (not just the box scale) so a smaller font shrinks the box
    // instead of leaving the content stranded left of a fixed-width bar.
    float txScale = C.pwText[PW_LABEL].size;
    if (C.pwText[PW_VALUE].size > txScale) txScale = C.pwText[PW_VALUE].size;
    if (C.pwText[PW_RATE].size  > txScale) txScale = C.pwText[PW_RATE].size;
    char lb[12], vb[28], eb[16], rb[12];
    // Measure EACH stat's own width. min bar width scales with the text size ; TEXT-ONLY (pwDisplay 1) has NO bar
    // -> no min. rowW[i] = that stat's real width ; contentW = the widest (drives the vertical box + aligned bars).
    // Horizontal layout uses rowW[i] PER column, so a rate-less stat (Merits -- never has an X/h, and gains nothing
    // once merit-capped) doesn't inherit the progression stat's rate slot and stretch the box with a phantom column.
    const float minW = (C.pwDisplay == 1) ? 0.0f : 150.0f * S * txScale;
    const float rateSlotW = fRate->measure(helpSample ? "9.9k/h" : "999.9k/h", zRate);   // X/h slot : MAX rate reservation live/edit ; the Help sample hugs its compact value
    float rowW[2] = { minW, minW }; float contentW = minW;
    for (int i = 0; i < nrows; ++i) {
        // value + extra reserved at a fixed MAX template (row 0 = progression, row 1 = Merits) so the //aio edit
        // sample and the live box compute the SAME width, and changing digits never resize the box (>= actual).
        const char* vtmpl = helpSample ? rows[i].val : ((i == 0) ? "999.9k/999.9k EP" : "9.9k/10k");
        float vw = fVal->measure(pw_up(PW_VALUE, vtmpl, vb, 28), zVal);
        { float aw = fVal->measure(pw_up(PW_VALUE, rows[i].val, vb, 28), zVal); if (aw > vw) vw = aw; }
        float lw = fLabel->measure(pw_up(PW_LABEL, rows[i].label, lb, 12), zLabel) + gap + vw;
        if (rows[i].extra[0]) {
            const char* etmpl = helpSample ? rows[i].extra : ((i == 1) ? "75/75" : "JP9999");   // Merits (75/75) vs CP job-points (JP####)
            float ew = fVal->measure(pw_up(PW_VALUE, etmpl, eb, 16), zVal);
            { float aw = fVal->measure(pw_up(PW_VALUE, rows[i].extra, eb, 16), zVal); if (aw > ew) ew = aw; }
            lw += gap * 0.6f + ew;
        }
        if (rows[i].rateSlot) {   // progression stat : ALWAYS reserve the X/h slot (>= the actual rate width)
            float rw = rateSlotW; if (rows[i].rate[0]) { float aw = fRate->measure(pw_up(PW_RATE, rows[i].rate, rb, 12), zRate); if (aw > rw) rw = aw; }
            lw += gap * 1.5f + rw;
        } else if (rows[i].rate[0]) lw += gap * 1.5f + fRate->measure(pw_up(PW_RATE, rows[i].rate, rb, 12), zRate);
        rowW[i] = (lw < minW) ? minW : lw;
        if (rowW[i] > contentW) contentW = rowW[i];
    }
    const bool  horiz = (C.pwLayout == 1);                       // 0 = rows stacked vertically ; 1 = side by side
    const bool  showHead = (C.pwDisplay != 2);                   // pwDisplay : 0 both / 1 text only / 2 bar only
    const bool  showBar  = (C.pwDisplay != 1);
    const float colGap = 14.0f * S;
    const float rowH = (showHead ? headH : 0.0f) + (showHead && showBar ? 2.0f * S : 0.0f) + (showBar ? barH : 0.0f);
    float horizW = (nrows - 1) * colGap + 2.0f * pad; for (int i = 0; i < nrows; ++i) horizW += rowW[i];
    const float boxW = horiz ? horizW : (contentW + 2.0f * pad);
    const float boxH = horiz ? (2.0f * pad + rowH) : (2.0f * pad + nrows * rowH + (nrows - 1) * rowGap);
    if (measureOnly) { if (outW) *outW = boxW; if (outH) *outH = boxH; return; }   // Help scale-to-fit : report dims, don't draw

    // ---- position (+ edit drag) : pwX = horizontal centre, pwY = top ----
    float px, py;
    if (ovS > 0.0f) { px = snap((ovX - boxW * 0.5f)); py = snap((ovY - boxH * 0.5f)); }        // preview/override : centred in the stage
    else            { px = snap(C.pwX * screenW - boxW); py = snap(C.pwY * screenH); }      // live : RIGHT-anchored (pwX = right edge) -> width changes move the LEFT edge only
    if (editing) { static EditBox g_pwEdit; box_edit(f, g_pwEdit, EDITBOX_POINTWATCH, px, py, boxW, boxH, ui_config().pwScale, ui_config().pwX, ui_config().pwY, 2); }   // anchorX=2 (RIGHT) : grows left, right edge stays put when the width changes (e.g. XP/CP value width at zone-in)

    // ---- box chrome ----
    dColorQuadState(dev);
    const float r0 = 5.0f * S;
    draw_themed_box(dev, f.skin, px, py, boxW, boxH, ui_config().pwBox, 1.0f, S);   // shared themed chrome (frame/transp/theme)

    const float x0 = px + pad;
    float colX = x0;                                                                  // running left edge (horizontal columns)
    for (int i = 0; i < nrows; ++i) {
        const PWRow& R = rows[i];
        const float cw = horiz ? rowW[i] : contentW;                                  // this stat's OWN width (horizontal) or shared (vertical)
        const float xB = horiz ? colX : x0;
        const float yB = horiz ? (py + pad) : (py + pad + i * (rowH + rowGap));        // top of this stat's block
        if (showHead) {   // head line : label (stage colour) + value [+ extra] .... rate
            const float hcy = yB + headH * 0.5f;
            const char* lbl = pw_up(PW_LABEL, R.label, lb, 12);
            fLabel->begin(dev); fLabel->draw_lc(dev, xB, hcy, lbl, zLabel, pw_col(PW_LABEL, R.lcol), strk, oLabel);
            float vx = xB + fLabel->measure(lbl, zLabel) + gap;
            const char* vv = pw_up(PW_VALUE, R.val, vb, 28);
            fVal->begin(dev); fVal->draw_lc(dev, vx, hcy, vv, zVal, pw_col(PW_VALUE, cVal), strk, oVal);
            if (R.extra[0]) { vx += fVal->measure(vv, zVal) + gap * 0.6f; const char* ex = pw_up(PW_VALUE, R.extra, eb, 16); fVal->draw_lc(dev, vx, hcy, ex, zVal, R.extraCol, strk, oVal); }
            if (R.rate[0]) { const char* rr = pw_up(PW_RATE, R.rate, rb, 12); fRate->begin(dev); fRate->draw_lc(dev, xB + cw - fRate->measure(rr, zRate), hcy, rr, zRate, pw_col(PW_RATE, cRate), strk, oRate); }
        }
        if (showBar) {   // progress bar : dark track + stage-coloured fill
            dColorQuadState(dev);
            const float barY = yB + (showHead ? headH + 2.0f * S : 0.0f), br = barH * 0.5f;
            rrect(dev, xB, barY, cw, barH, br, 0xFF20222Cu, 0xFF16181Fu, 1.0f);
            const float frac = R.mx ? (float)R.cur / (float)R.mx : 0.0f; const float fw = cw * (frac < 0.0f ? 0.0f : (frac > 1.0f ? 1.0f : frac));
            if (fw > 1.0f) {
                if (fw >= cw - 0.5f) rrect(dev, xB, barY, fw, barH, br, R.lcol, R.lcol, 1.0f);
                else                 rrect_left(dev, xB, barY, fw, barH, br, R.lcol, R.lcol, 1.0f);
            }
        }
        if (horiz) colX += cw + colGap;
    }
}

// Live / edit path : the Hud draws the box at its configured screen position.
void Hud::draw_pointwatch(const Frame& f, bool preview, float ovX, float ovY, float ovS) {
    pointwatch_draw(f, preview, ovX, ovY, ovS, (float)screenW_, (float)screenH_);
}

// Help sample : the REAL PointWatch box in preview mode (config-aware layout / text / bars), centred at (cx,cy).
void pointwatch_help_box(const Frame& f, float cx, float cy, float s) {
    pointwatch_draw(f, true, cx, cy, s, 0.0f, 0.0f, false, 0, 0, true);   // compact Help sample (no max-footprint reservation)
}

// Help scale-to-fit : measure the box at scale 1 (linear in S), then pick the LARGEST scale that fits availW (capped
// at maxScale). Fills the chosen scale + the resulting box height so the caller can reserve the row.
void pointwatch_help_fit(const Frame& f, float availW, float maxScale, float& outScale, float& outH) {
    float bw = 0.0f, bh = 0.0f;
    pointwatch_draw(f, true, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, true, &bw, &bh, true);   // measure the compact Help sample
    float s = (bw > 1.0f) ? (availW / bw) : maxScale;
    if (s > maxScale) s = maxScale; if (s < 0.6f) s = 0.6f;
    outScale = s; outH = bh * s;
}

} // namespace aio
