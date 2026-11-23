// hud_absorb.cpp -- the Absorb-TP box : who in your party / alliance cast Absorb-TP, how much TP it drained, how long
// ago, and how many they landed. Replaces the AbsorbWatch addon (same source, the 0x028 ; model : PartyState::aw_,
// act_absorb_tp). Readable at a glance : the drained TP is the big coloured number, everything else is quieter.
#include "ui/hud.h"
#include "ui/hud_internal.h"
#include "model/ui_config.h"
#include "ui/text_style.h"
#include "ui/box_style.h"
#include "model/party_state.h"
#include "ui/config_controls.h"   // tr()
#include "ui/sc_circle.h"      // aw_dial_fx : the absorb effect, drawn by the Skillchains engine (DX8 distortion)
#include "model/model_clock.h"   // model_now_ms : the same clock the model stamped the casts with
#include "gfx/draw.h"
#include "gfx/color.h"     // lerp_argb : the badge plates
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

    struct ARow { char name[20]; int tp; int ago; int n; unsigned last; bool late; int job; float agoMs, agoS; };   // agoMs : the animation's clock (real ms) ; agoS : the fuse's (seconds)
    ARow rows[PartyState::AW_MAX]; int nr = 0;
    if (preview || editing) {
        // ANIMATED DEMO, fast-forwarded : each caster re-casts in a loop, a third of a cycle apart, so the preview
        // shows the whole life of a row in 12 s -- the fuse and the timer burning green -> yellow -> red, the row
        // tipping into the red alert past the threshold, then snapping back green on the next cast with a new TP
        // value and a count one higher. One cycle = 1.5 x the alert threshold, so a third of it is spent late.
        // FICTIONAL names only (never a real player in a preview). Fixed order : a demo that re-sorted on every
        // cast would be hard to follow.
        static const char* NAMES[4] = { "Aeryn", "Brakkenhold", "Selene", "Corvin" };
        static const int JOBS[4] = { 8, 8, 4, 22 };
        static const int TPS[5] = { 412, 185, 0, 275, 96 };
        const float cycle = 1.5f * (float)(C.awScreen > 0 ? C.awScreen : 90);      // simulated seconds per loop
        const float sim = (float)(GetTickCount() % 1200000u) / 12000.0f * cycle;  // 12 real seconds per loop
        const int ND = C.awStyle == 0 ? 4 : 3;
        for (int i = 0; i < ND; ++i) {
            ARow& r = rows[nr++];
            const float t = sim + (float)i * cycle / (float)ND;
            const int loop = (int)(t / cycle);
            snprintf(r.name, sizeof(r.name), "%s", NAMES[i]);
            r.ago = (int)fmodf(t, cycle); r.agoMs = fmodf(t, cycle) * 1000.0f / (cycle / 12.0f); r.agoS = fmodf(t, cycle); r.job = JOBS[i];
            r.tp = TPS[(loop + i * 2) % 5];
            r.n = 1 + (loop + i) % 5;
            r.last = 3u - (unsigned)i;
            r.late = r.ago > C.awScreen;
        }
        for (int i = 1; i < nr; ++i) { ARow t = rows[i]; int j = i - 1; while (j >= 0 && rows[j].ago > t.ago) { rows[j + 1] = rows[j]; --j; } rows[j + 1] = t; }   // most recent first (the dial reads the rotation)
    } else {
        const unsigned now = model_now_ms();
        int n = 0; const PartyState::AbsorbTp* a = party().absorb_tp(n);
        for (int i = 0; i < n; ++i) {
            if (!a[i].lastMs) continue;
            const unsigned age = now - a[i].lastMs;
            if (age > (unsigned)C.awMemory * 1000u) continue;           // gone only once out of the memory window
            ARow& r = rows[nr++];
            snprintf(r.name, sizeof(r.name), "%s", a[i].name[0] ? a[i].name : "?");
            r.tp = a[i].lastTp; r.ago = (int)(age / 1000u); r.last = a[i].lastMs; r.agoMs = (float)age; r.agoS = (float)age / 1000.0f;
            r.job = 0; for (int q = 0; q < party().count; ++q) if (party().m[q].id == a[i].actor) { r.job = party().m[q].mjob; break; }   // the caster's job (party only ; an alliance caster : its initials)
            r.late = age > (unsigned)C.awScreen * 1000u;                  // no Absorb-TP for too long : the row turns red
            r.n = PartyState::absorb_tp_count(a[i], (unsigned)C.awMemory * 1000u, now);
        }
        for (int i = 1; i < nr; ++i) { ARow t = rows[i]; int j = i - 1; while (j >= 0 && rows[j].last < t.last) { rows[j + 1] = rows[j]; --j; } rows[j + 1] = t; }   // most recent first
    }
    if (nr == 0) return;
    if (C.awStyle == 0) {   // ======== the ANIMATED LIST ========
        // On top, the BEACON : an orb holding the last drain (the absorb animation plays round it), the last caster's
        // name beside it -- or, when nobody has cast for too long, a red WARNING. Under it, the casters in order, the
        // most recent first : a medallion (the job, a fuse ring), the name, a fuse bar burning down, the TP, the seconds.
        float sscl2 = C.awScale; if (sscl2 < 0.5f) sscl2 = 0.5f; if (sscl2 > 2.0f) sscl2 = 2.0f;
        const float S = (ovS > 0.0f) ? ovS : ((float)screenH_ / 1000.0f) * sscl2;
        static const TextStyle TS2{};
        Font* fo = te_font(f, TS2); if (!fo) return;
        const int n = nr;
        const u32 strk = 0xFF000000u, white = 0xFFEAF0FFu, red = 0xFFFF5050u, gold = 0xFFFFD060u;
        const float ow = 1.0f * S, A0 = -1.5707963f, TWO_PI = 6.2831853f;
        const int lay = (C.awLayout >= 0 && C.awLayout <= 2) ? C.awLayout : 0, jobs = (C.awJobs >= 0 && C.awJobs <= 2) ? C.awJobs : 1;
        const float bs = C.awBadge < 0.7f ? 0.7f : (C.awBadge > 1.6f ? 1.6f : C.awBadge);
        const float ts = C.awText < 0.7f ? 0.7f : (C.awText > 1.6f ? 1.6f : C.awText), os = C.awOrb < 0.6f ? 0.6f : (C.awOrb > 1.8f ? 1.8f : C.awOrb);
        const float Ro = 17.0f * S * os, gap = 8.0f * S * ts;
        const float hdrTxt = 2.0f * 8.5f * S * ts + 14.0f * S * ts, hdrH = lay == 2 ? 0.0f : ((2.0f * Ro > hdrTxt ? 2.0f * Ro : hdrTxt) + 10.0f * S);
        const float zB = 10.5f * S * bs, bh = 17.0f * S * bs;   // the job badge : a coloured plate, its text big enough to read
        const float zHn = 14.0f * S * ts, zHs = 10.5f * S * ts, zOrb = 13.0f * S * os, zN = 12.0f * S * ts, zV = 14.0f * S * ts, zS = 10.0f * S * ts;
        const ARow& L0 = rows[0];
        const bool warn = L0.late;   // nobody drained TP for longer than the alert threshold
        const float tk = (float)GetTickCount();
        const float pulse = 0.5f + 0.5f * sinf(tk * 0.009f);
        // the beacon's two lines
        char h1[40], h2[48];
        if (warn) { snprintf(h1, sizeof(h1), "%s", "ABSORB-TP !"); snprintf(h2, sizeof(h2), tr("Nobody for %ds", "Personne depuis %ds"), L0.ago); }
        else      { snprintf(h1, sizeof(h1), "%s", L0.name); snprintf(h2, sizeof(h2), tr("Last  %ds ago", "Dernier  il y a %ds"), L0.ago); }
        float hW = fo->measure(h1, zHn); { const float w2 = fo->measure(h2, zHs); if (w2 > hW) hW = w2; }
        const float headW = 2.0f * Ro + 12.0f * S + hW;
        // the rows' columns
        float wName = 0.0f, wTp = fo->measure("999", zV), wAgo = 0.0f, wCnt = 0.0f;
        char agoB[PartyState::AW_MAX][12], cntB[PartyState::AW_MAX][8], tpB[PartyState::AW_MAX][8];
        for (int i = 0; i < n; ++i) {
            const float w = fo->measure(rows[i].name, zN); if (w > wName) wName = w;
            snprintf(tpB[i], sizeof(tpB[i]), "%d", rows[i].tp < 0 ? 0 : rows[i].tp);
            snprintf(agoB[i], sizeof(agoB[i]), "%ds", rows[i].ago);
            snprintf(cntB[i], sizeof(cntB[i]), "x%d", rows[i].n);
            if (C.awCount) { const float c = fo->measure(cntB[i], zS) + 9.0f * S; if (c > wCnt) wCnt = c; }
        }
        if (wName < 60.0f * S) wName = 60.0f * S;   // the fuse bar under the name needs some length
        if (C.awTimer) wAgo = fo->measure("999s", zS);
        const float bw = jobs ? fo->measure(jobs == 2 ? "WW" : "WAR", zB) + 10.0f * S * bs : 0.0f;
        const float rowH = (bh + 7.0f * S > 26.0f * S * ts) ? bh + 7.0f * S : 26.0f * S * ts;
        const float nameX = jobs ? 4.0f * S + bw + 8.0f * S : 8.0f * S;
        const float rowW = nameX + wName + gap + wTp + (C.awTimer ? gap + wAgo : 0.0f) + (C.awCount ? gap + wCnt : 0.0f) + 4.0f * S;
        const float contentW = (lay == 2 || rowW > headW) ? rowW : headW;
        const float pad = (C.awBox.on ? 7.0f * S : 2.0f * S) + box_inset(C.awBox);
        const float boxW = snap(contentW + 2.0f * pad), boxH = snap(pad + hdrH + (lay == 2 ? 0.0f : 5.0f * S) + (float)n * rowH + pad);
        float px, py;
        if (ovS > 0.0f) { px = snap(ovX - boxW * 0.5f); py = snap(ovY - boxH * 0.5f); }
        else            { px = box_grow_x((float)screenW_, UiConfig::GB_AW, 0, C.awX, boxW); py = snap(C.awY * (float)screenH_); }
        if (editing) { static EditBox g_awEditL; box_edit(f, g_awEditL, EDITBOX_ABSORB, px, py, boxW, boxH, ui_config().awScale, ui_config().awX, ui_config().awY, C.boxGrow[UiConfig::GB_AW]); }
        if (!editing && ovS <= 0.0f) box_on_screen(f, px, py, boxW, boxH);
        if (C.awBox.on) { dColorQuadState(dev); draw_themed_box(dev, f.skin, px, py, boxW, boxH, C.awBox, 1.0f, S); }
        const float x0 = px + pad;
        const float rows0 = lay == 0 ? py + pad + hdrH + 5.0f * S : py + pad;
        const float top = lay == 1 ? rows0 + (float)n * rowH + 5.0f * S : py + pad;   // the beacon's band
        const float OX = snap(x0 + Ro + 2.0f * S), OY = snap(top + hdrH * 0.5f);
        const float bx0 = x0 + 4.0f * S + bw * 0.5f;   // the badges' centre
        const u32 tpc = aw_tp_color(L0.tp, C.awColors != 0) & 0xFFFFFFu;
        // 1. the beacon's band : a warning turns it into a red pulsing plate
        dColorQuadState(dev);
        if (lay != 2) {
        if (warn) {
            const u32 aT = (u32)(70.0f + 70.0f * pulse), aB = (u32)(40.0f + 40.0f * pulse);
            rrect(dev, x0, top, contentW, hdrH, 6.0f * S, (aT << 24) | 0xB01818u, (aB << 24) | 0x701010u, 1.0f);
        }
        // 2. the ABSORB round the orb (the Skillchains engine : the scene drawn in, matter streaming up from the
        // caster's row, a closing shockwave ; then a quiet breath -- red in a warning). Only 0.9 s after a cast.
        if (C.awFx) aw_dial_fx(f, OX, OY, Ro, 0.6f * S, L0.agoMs, warn ? 0xFF4040u : tpc, bx0, rows0 + rowH * 0.5f, C.awDistort != 0, warn);
        dColorQuadState(dev);
        // 3. the ORB : a dark well, the time since the last cast as a ring burning green -> red
        {
            const float fr = C.awScreen > 0 ? L0.agoS / (float)C.awScreen : 1.0f, frc = fr > 1.0f ? 1.0f : (fr < 0.0f ? 0.0f : fr);
            const u32 ageC = aw_age_color(L0.ago, C.awScreen);
            if (L0.agoMs < 1000.0f) { const float fl = 1.0f - L0.agoMs / 1000.0f; disc_glow(dev, OX, OY, Ro, ((u32)(200.0f * fl) << 24) | tpc, 10.0f * S * fl + 2.0f * S); }
            disc(dev, OX, OY, Ro + 1.5f * S, 0xFF000000u);
            disc(dev, OX, OY, Ro, 0xF0080B12u);
            ring_sector(dev, OX, OY, Ro - 3.5f * S, Ro - 0.8f * S, 0.0f, TWO_PI, 0x40FFFFFFu);
            if (warn) ring_sector(dev, OX, OY, Ro - 4.0f * S, Ro - 0.3f * S, 0.0f, TWO_PI, ((u32)(110.0f + 145.0f * pulse) << 24) | 0xFF4848u);
            else if (frc > 0.0f) ring_sector(dev, OX, OY, Ro - 4.0f * S, Ro - 0.3f * S, A0, A0 + frc * TWO_PI, ageC);
            grad_quad(dev, OX - Ro * 0.6f, OY - Ro * 0.85f, Ro * 1.2f, Ro * 0.45f, 0x1CFFFFFFu, 0x1CFFFFFFu, 0x00FFFFFFu, 0x00FFFFFFu);   // its glass
            fo->begin(dev);
            if (warn) {
                const float z = zOrb * 1.5f * (1.0f + 0.08f * pulse);
                fo->draw_c_scaled(dev, OX, OY, "!", zOrb * 1.5f, z, red, strk, 1.4f * S);
            } else {   // the last drain : counting up as it lands, popping
                const float cu = L0.agoMs < 300.0f ? 1.0f - (1.0f - L0.agoMs / 300.0f) * (1.0f - L0.agoMs / 300.0f) : 1.0f;
                char tb[12]; snprintf(tb, sizeof(tb), "%d", (int)((L0.tp < 0 ? 0 : L0.tp) * cu + 0.5f));
                float sc = 1.0f; if (L0.agoMs < 120.0f) sc = 0.5f + 0.75f * (L0.agoMs / 120.0f); else if (L0.agoMs < 260.0f) sc = 1.25f - 0.25f * ((L0.agoMs - 120.0f) / 140.0f);
                fo->draw_c_scaled(dev, OX, OY, tb, zOrb, zOrb * sc, aw_tp_color(L0.tp, C.awColors != 0), strk, 1.3f * S);
            }
            // the beacon's text : who, when (or the warning)
            const float tx = OX + Ro + 10.0f * S;
            fo->draw_lv(dev, tx, OY - 7.5f * S * ts, h1, zHn, warn ? (((u32)(160.0f + 95.0f * pulse) << 24) | 0xFF5050u) : white, strk, ow);
            fo->draw_lv(dev, tx, OY + 8.5f * S * ts, h2, zHs, warn ? 0xFFFFB0A0u : ageC, strk, ow);
        }
        }   // the beacon
        // 4. the LIST, most recent first
        const int nextI = n >= 2 ? n - 1 : -1;   // the one who has waited longest : next in the rotation
        for (int i = 0; i < n; ++i) {
            const ARow& r = rows[i];
            const float ry = rows0 + (float)i * rowH, mid = ry + rowH * 0.5f;
            const u32 ac = r.late ? red : aw_age_color(r.ago, C.awScreen);
            const u32 tc = aw_tp_color(r.tp, C.awColors != 0);
            const float f2 = C.awScreen > 0 ? r.agoS / (float)C.awScreen : 1.0f, fc = f2 > 1.0f ? 1.0f : (f2 < 0.0f ? 0.0f : f2);
            const bool isNext = i == nextI && !r.late;
            dColorQuadState(dev);
            // the card : the last caster lit in its TP colour, a late one red, the others a quiet zebra
            if (r.late)      rrect(dev, x0, ry + 1.0f * S, contentW, rowH - 2.0f * S, 5.0f * S, 0x50C02020u, 0x38A01818u, 1.0f);
            else if (i == 0) rrect(dev, x0, ry + 1.0f * S, contentW, rowH - 2.0f * S, 5.0f * S, (0x40u << 24) | (tc & 0xFFFFFFu), (0x18u << 24) | (tc & 0xFFFFFFu), 1.0f);
            if (r.agoMs < 1000.0f) {   // just cast : the card flashes
                const float fl = 1.0f - r.agoMs / 1000.0f;
                rrect(dev, x0, ry + 1.0f * S, contentW, rowH - 2.0f * S, 5.0f * S, ((u32)(120.0f * fl) << 24) | (tc & 0xFFFFFFu), ((u32)(60.0f * fl) << 24) | (tc & 0xFFFFFFu), 1.0f);
            }
            // the BADGE : a plate in the job's role colour, the job (or the initials) in white -- read at a glance
            const char* jb = 0; char ini[4] = { 0 };
            const float bxl = x0 + 4.0f * S, byt = mid - bh * 0.5f;
            if (jobs) {
                jb = jobs == 1 ? job_abbr(r.job) : 0;
                if (!jb || !jb[0]) { ini[0] = r.name[0]; ini[1] = r.name[0] ? r.name[1] : 0; if (ini[1] >= 'a' && ini[1] <= 'z') ini[1] = (char)(ini[1] - 32); jb = ini; }
                const u32 rc = (jobs == 1 && r.job) ? (job_role_color(r.job) & 0xFFFFFFu) : 0x8090A8u;
                if (isNext) { const float pu = 0.5f + 0.5f * sinf(tk * 0.006f);   // next in the rotation : a gold rim breathing
                    rrect(dev, bxl - 2.5f * S, byt - 2.5f * S, bw + 5.0f * S, bh + 5.0f * S, 5.5f * S * bs, ((u32)(90.0f + 140.0f * pu) << 24) | 0xFFD060u, ((u32)(90.0f + 140.0f * pu) << 24) | 0xE0A030u, 1.0f); }
                rrect(dev, bxl - 1.0f * S, byt - 1.0f * S, bw + 2.0f * S, bh + 2.0f * S, 4.5f * S * bs, r.late ? 0xFFFF5050u : 0xFF000000u, r.late ? 0xFFC03030u : 0xFF000000u, 1.0f);
                rrect(dev, bxl, byt, bw, bh, 3.5f * S * bs, lerp_argb(0xFF000000u | rc, 0xFFFFFFFFu, 0.10f), lerp_argb(0xFF000000u | rc, 0xFF000000u, 0.45f), 1.0f);
                rrect(dev, bxl + 1.0f * S, byt + 1.0f * S, bw - 2.0f * S, bh * 0.45f, 2.5f * S * bs, 0x40FFFFFFu, 0x08FFFFFFu, 1.0f);   // its gloss
            }
            // the fuse bar under the name : what is left before the alert, burning down green -> red
            const float nx = x0 + nameX, fbY = mid + 7.0f * S * ts, fbW = wName, fbH = 2.0f * S;
            rrect(dev, nx, fbY, fbW, fbH, 1.0f * S, 0x30FFFFFFu, 0x30FFFFFFu, 1.0f);
            if (!r.late && fc < 1.0f) rrect(dev, nx, fbY, fbW * (1.0f - fc), fbH, 1.0f * S, ac, ac, 1.0f);
            fo->begin(dev);
            if (jobs && jb) fo->draw_c(dev, bxl + bw * 0.5f, mid, jb, zB, 0xFFFFFFFFu, strk, 1.3f * S * bs);
            fo->draw_lv(dev, nx, mid - 2.5f * S * ts, r.name, zN, r.late ? red : (isNext ? gold : white), strk, ow);
            float x = nx + wName + gap;
            fo->draw_lv(dev, x + wTp - fo->measure(tpB[i], zV), mid, tpB[i], zV, tc, strk, 1.2f * S);
            x += wTp;
            if (C.awTimer) { x += gap; fo->draw_lv(dev, x + wAgo - fo->measure(agoB[i], zS), mid, agoB[i], zS, ac, strk, ow); x += wAgo; }
            if (C.awCount) {
                x += gap;
                const float cw = fo->measure(cntB[i], zS) + 9.0f * S, ch = zS + 5.0f * S;
                dColorQuadState(dev);
                rrect(dev, x + wCnt - cw, mid - ch * 0.5f, cw, ch, ch * 0.5f, 0x48FFFFFFu, 0x34FFFFFFu, 1.0f);
                fo->begin(dev);
                fo->draw_lv(dev, x + wCnt - cw + 4.5f * S, mid, cntB[i], zS, white, strk, ow);
            }
        }
        return;
    }

    float sscl = C.awScale; if (sscl < 0.5f) sscl = 0.5f; if (sscl > 2.0f) sscl = 2.0f;
    const float S = (ovS > 0.0f) ? ovS : ((float)screenH_ / 1000.0f) * sscl;
    const float pad = (C.awBox.on ? 7.0f : 0.0f) * S + box_inset(C.awBox), gap = 8.0f * S;   // + the frame theme's border
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
