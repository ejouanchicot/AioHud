// hud_absorb.cpp -- the Absorb-TP box : who in your party / alliance cast Absorb-TP, how much TP it drained, how long
// ago, and how many they landed. Replaces the AbsorbWatch addon (same source, the 0x028 ; model : PartyState::aw_,
// act_absorb_tp). Readable at a glance : the drained TP is the big coloured number, everything else is quieter.
#include "ui/hud.h"
#include "ui/hud_internal.h"
#include "model/ui_config.h"
#include "ui/text_style.h"
#include "ui/box_style.h"
#include "model/party_state.h"
#include "model/model_clock.h"   // model_now_ms : the same clock the model stamped the casts with
#include "gfx/draw.h"
#include "gfx/d3d.h"
#include <stdio.h>
#include <math.h>      // fmodf : the animated demo
#include <windows.h>   // GetTickCount : the animated demo's clock
#include <string.h>

namespace aio {

// How late a caster is, as a colour : green right after their Absorb-TP, yellow half-way to the alert threshold,
// red at it. Drives the row's left stroke AND its timer, so the box reads as a set of fuses burning down.
static u32 aw_age_color(int ago, int alertSec) {
    float t = alertSec > 0 ? (float)ago / (float)alertSec : 1.0f; if (t < 0.0f) t = 0.0f; if (t > 1.0f) t = 1.0f;
    const float g0[3] = { 0x6B, 0xE0, 0x6B }, y0[3] = { 0xF2, 0xD0, 0x4B }, r0[3] = { 0xFF, 0x50, 0x50 };
    const float* a = t < 0.5f ? g0 : y0; const float* b = t < 0.5f ? y0 : r0; const float k = t < 0.5f ? t * 2.0f : (t - 0.5f) * 2.0f;
    const u32 R = (u32)(a[0] + (b[0] - a[0]) * k), G = (u32)(a[1] + (b[1] - a[1]) * k), B = (u32)(a[2] + (b[2] - a[2]) * k);
    return 0xFF000000u | (R << 16) | (G << 8) | B;
}
// AbsorbWatch's own scale, kept so a player moving over reads the same colours : the more TP stolen, the hotter.
static u32 aw_tp_color(int tp, bool colors) {
    if (!colors) return 0xFFEAF0FFu;
    if (tp <= 0)   return 0xFF4F9BFFu;   // blue : drained nothing (resisted / no effect)
    if (tp <= 99)  return 0xFFEAF0FFu;   // white
    if (tp <= 200) return 0xFF6BE06Bu;   // green
    if (tp <= 300) return 0xFFF2E05Bu;   // yellow
    if (tp <= 400) return 0xFFF2A04Bu;   // orange
    return 0xFFFF5A5Au;                   // red
}

void Hud::draw_absorb(const Frame& f, bool preview, float ovX, float ovY, float ovS) {
    const UiConfig& C = ui_config();
    if (!C.awShow) return;
    const bool editing = C.editLayout && !preview;
    const u32 dev = f.dev;
    if (!f.font && !f.fonts) return;

    struct ARow { char name[20]; int tp; int ago; int n; unsigned last; bool late; };
    ARow rows[PartyState::AW_MAX]; int nr = 0;
    if (preview || editing) {
        // ANIMATED DEMO, fast-forwarded : each caster re-casts in a loop, a third of a cycle apart, so the preview
        // shows the whole life of a row in 12 s -- the fuse and the timer burning green -> yellow -> red, the row
        // tipping into the red alert past the threshold, then snapping back green on the next cast with a new TP
        // value and a count one higher. One cycle = 1.5 x the alert threshold, so a third of it is spent late.
        // FICTIONAL names only (never a real player in a preview). Fixed order : a demo that re-sorted on every
        // cast would be hard to follow.
        static const char* NAMES[3] = { "Aeryn", "Brakkenhold", "Selene" };
        static const int TPS[5] = { 412, 185, 0, 275, 96 };
        const float cycle = 1.5f * (float)(C.awScreen > 0 ? C.awScreen : 90);      // simulated seconds per loop
        const float sim = (float)(GetTickCount() % 1200000u) / 12000.0f * cycle;  // 12 real seconds per loop
        for (int i = 0; i < 3; ++i) {
            ARow& r = rows[nr++];
            const float t = sim + (float)i * cycle / 3.0f;
            const int loop = (int)(t / cycle);
            snprintf(r.name, sizeof(r.name), "%s", NAMES[i]);
            r.ago = (int)fmodf(t, cycle);
            r.tp = TPS[(loop + i * 2) % 5];
            r.n = 1 + (loop + i) % 5;
            r.last = 3u - (unsigned)i;
            r.late = r.ago > C.awScreen;
        }
    } else {
        const unsigned now = model_now_ms();
        int n = 0; const PartyState::AbsorbTp* a = party().absorb_tp(n);
        for (int i = 0; i < n; ++i) {
            if (!a[i].lastMs) continue;
            const unsigned age = now - a[i].lastMs;
            if (age > (unsigned)C.awMemory * 1000u) continue;           // gone only once out of the memory window
            ARow& r = rows[nr++];
            snprintf(r.name, sizeof(r.name), "%s", a[i].name[0] ? a[i].name : "?");
            r.tp = a[i].lastTp; r.ago = (int)(age / 1000u); r.last = a[i].lastMs;
            r.late = age > (unsigned)C.awScreen * 1000u;                  // no Absorb-TP for too long : the row turns red
            r.n = PartyState::absorb_tp_count(a[i], (unsigned)C.awMemory * 1000u, now);
        }
        for (int i = 1; i < nr; ++i) { ARow t = rows[i]; int j = i - 1; while (j >= 0 && rows[j].last < t.last) { rows[j + 1] = rows[j]; --j; } rows[j + 1] = t; }   // most recent first
    }
    if (nr == 0) return;

    float sscl = C.awScale; if (sscl < 0.5f) sscl = 0.5f; if (sscl > 2.0f) sscl = 2.0f;
    const float S = (ovS > 0.0f) ? ovS : ((float)screenH_ / 1000.0f) * sscl;
    const float pad = (C.awBox.on ? 7.0f : 0.0f) * S, gap = 8.0f * S;
    const u32 strk = 0xFF000000u, white = 0xFFEAF0FFu, orange = 0xFFEB9660u;
    static const TextStyle TS{};
    Font* fo = te_font(f, TS);
    if (!fo) return;
    const float zH = 12.0f * S, zN = 13.0f * S, zV = 16.0f * S, zS = 11.0f * S, ow = 1.0f * S;
    const float headH = zH + 7.0f * S, rowH = zV + 7.0f * S;

    // columns : name | TP (right-aligned) | ago | count chip -- widths from the rows actually shown
    float wName = 0.0f, wTp = fo->measure("999", zV), wAgo = 0.0f, wCnt = 0.0f;
    char agoB[PartyState::AW_MAX][12], cntB[PartyState::AW_MAX][8], tpB[PartyState::AW_MAX][8];
    for (int i = 0; i < nr; ++i) {
        const float w = fo->measure(rows[i].name, zN); if (w > wName) wName = w;
        snprintf(tpB[i], sizeof(tpB[i]), "%d", rows[i].tp < 0 ? 0 : rows[i].tp);
        snprintf(agoB[i], sizeof(agoB[i]), "%ds", rows[i].ago);
        snprintf(cntB[i], sizeof(cntB[i]), "x%d", rows[i].n);
        if (C.awTimer) { const float a = fo->measure(agoB[i], zS); if (a > wAgo) wAgo = a; }
        if (C.awCount) { const float c = fo->measure(cntB[i], zS) + 10.0f * S; if (c > wCnt) wCnt = c; }
    }
    if (C.awTimer) { const float a = fo->measure("999s", zS); if (a > wAgo) wAgo = a; }   // a steady column while seconds tick (a late row reaches minutes)
    const char* title = "Absorb-TP";
    const float inset = 10.0f * S;   // air between the row's left edge (the red alert stroke) and the name -- every row, so names stay aligned
    float contentW = inset + wName + gap + wTp + (C.awTimer ? gap + wAgo : 0.0f) + (C.awCount ? gap + wCnt : 0.0f);
    if (fo->measure(title, zH) > contentW) contentW = fo->measure(title, zH);
    const float boxW = contentW + 2.0f * pad, boxH = pad + headH + (float)nr * rowH + pad;

    float px, py;
    if (ovS > 0.0f) { px = snap(ovX - boxW * 0.5f); py = snap(ovY - boxH * 0.5f); }
    else            { px = box_grow_x((float)screenW_, UiConfig::GB_AW, 0, C.awX, boxW); py = snap(C.awY * (float)screenH_); }
    if (editing) { static EditBox g_awEdit; box_edit(f, g_awEdit, EDITBOX_ABSORB, px, py, boxW, boxH, ui_config().awScale, ui_config().awX, ui_config().awY, C.boxGrow[UiConfig::GB_AW]); }
    if (!editing && ovS <= 0.0f) box_on_screen(f, px, py, boxW, boxH);

    dColorQuadState(dev);
    draw_themed_box(dev, f.skin, px, py, boxW, boxH, C.awBox, 1.0f, S);
    const float x0 = px + pad;
    float cy = py + pad;
    fo->begin(dev);
    fo->draw_c(dev, px + boxW * 0.5f, cy + headH * 0.5f, title, zH, orange, strk, ow);   // centred, like every other module's title
    cy += headH;
    for (int i = 0; i < nr; ++i) {
        const ARow& r = rows[i];
        const float mid = cy + rowH * 0.5f;
        const u32 lateRed = 0xFFFF5050u;
        const u32 ageCol = aw_age_color(r.ago, C.awScreen);   // green -> yellow -> red as the alert threshold nears
        if (r.late) {   // LATE : a red card and a red edge -- this one stopped draining TP
            dColorQuadState(dev);
            rrect(dev, x0 - 3.0f * S, cy, contentW + 6.0f * S, rowH, 4.0f * S, 0x55C02020u, 0x40A01818u, 1.0f);
            rrect_left(dev, x0 - 3.0f * S, cy, 3.0f * S, rowH, 2.0f * S, lateRed, lateRed, 1.0f);
            fo->begin(dev);
        } else {
            dColorQuadState(dev);
            if (i & 1) rrect(dev, x0 - 3.0f * S, cy, contentW + 6.0f * S, rowH, 4.0f * S, 0x22FFFFFFu, 0x18FFFFFFu, 1.0f);   // zebra : rows stay separable at a glance
            rrect_left(dev, x0 - 3.0f * S, cy, 3.0f * S, rowH, 2.0f * S, ageCol, ageCol, 1.0f);                              // the fuse : every row has one
            fo->begin(dev);
        }
        float x = x0 + inset;
        fo->draw_lv(dev, x, mid, r.name, zN, r.late ? lateRed : white, strk, ow);
        x += wName + gap;
        const u32 tc = aw_tp_color(r.tp, C.awColors != 0);
        fo->draw_lv(dev, x + wTp - fo->measure(tpB[i], zV), mid, tpB[i], zV, tc, strk, 1.3f * S);   // the number that matters, big
        x += wTp;
        if (C.awTimer) { x += gap; fo->draw_lv(dev, x + wAgo - fo->measure(agoB[i], zS), mid, agoB[i], zS, r.late ? lateRed : ageCol, strk, ow); x += wAgo; }   // the timer burns with its stroke
        if (C.awCount) {
            x += gap;
            const float cw = fo->measure(cntB[i], zS) + 10.0f * S, ch = zS + 6.0f * S;
            dColorQuadState(dev);
            rrect(dev, x + wCnt - cw, mid - ch * 0.5f, cw, ch, ch * 0.5f, 0x55FFFFFFu, 0x40FFFFFFu, 1.0f);   // the count chip
            fo->begin(dev);
            fo->draw_lv(dev, x + wCnt - cw + 5.0f * S, mid, cntB[i], zS, white, strk, ow);
        }
        cy += rowH;
    }
}

} // namespace aio
