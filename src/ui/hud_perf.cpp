// hud_perf.cpp -- //aio perf : the overlay, the dump and the command. The measuring itself is gfx/perf.h.
//
//   //aio perf on      start measuring + show the overlay (60-frame sliding window, rows sorted by cost)
//   //aio perf off     stop (back to zero cost : every hook is one byte test)
//   //aio perf reset   clear the window and the since-reset totals
//   //aio perf dump [label]  the since-reset totals, every row, into aiohud_debug.log (AIO PERF block, tagged)
//   //aio perf ab [n]  n frames (default 600) alternating instrumented / bare, then the two frame-time averages :
//                      what the instrumentation itself costs, measured in the game
//   //aio perf auto    the whole campaign in ONE command (asked for in game : 15 commands typed 50 s apart was the
//                      first protocol) -- six phases, each set up by the plugin itself, settled, measured and dumped
//                      under its own label : base, alliance (18-member demo), chat (both boxes scrolled to the top),
//                      skillchain (the sphere demo, every chain in turn), worst (all of those at once + 32 fake buffs),
//                      config (the window open), then an ab run.
//                      Everything it changed is put back. //aio perf off stops it.
//   //aio perf filter on|off   the redundant-state filter (gfx/d3d.h, audit P4) : on by default
#include "ui/hud_internal.h"
#include "ui/config_controls.h"   // tr()
#include "ui/config_page.h"       // //aio perf auto : the config phase opens it
#include "ui/hud.h"               // scfx_toggle : the skillchain phase's sphere demo
#include "model/party_state.h"    // set_party_demo_total : the alliance phase
#include "model/ui_config.h"      // chWheel : the chat phase scrolls both boxes up
#include "perf.h"
#include "gfx/font.h"
#include "gfx/draw.h"               // the overlay card : rrect / glow / bars / the frame graph
#include <math.h>
#include "windower_debug.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

namespace aio {

namespace {

// Rows by a key, descending -- insertion sort over at most 64 indices, no allocation.
int sorted_rows(int* idx, bool bySinceReset) {
    const perf::State& P = perf::g_perf;
    int n = 0;
    for (int i = 0; i < P.n; ++i) {
        const perf::Entry& E = P.e[i];
        const bool live = bySinceReset ? (E.sumCalls > 0) : (E.wCalls > 0);
        if (live) idx[n++] = i;
    }
    for (int a = 1; a < n; ++a) {
        const int v = idx[a];
        const unsigned long long kv = bySinceReset ? P.e[v].sumUs : P.e[v].wSelfUs;
        int b = a - 1;
        while (b >= 0 && (bySinceReset ? P.e[idx[b]].sumUs : P.e[idx[b]].wSelfUs) < kv) { idx[b + 1] = idx[b]; --b; }
        idx[b + 1] = v;
    }
    return n;
}

// "1234" us -> "1.23" ms, integer only (no %f : see probe-and-tooling notes)
void fmt_ms(char* o, int cap, u32 us) { _snprintf(o, cap, "%u.%02u", us / 1000, (us % 1000) / 10); o[cap - 1] = 0; }

// "(frame)" self time is what no scope claimed
const char* row_name(int i) { return i == 0 ? "(other)" : perf::g_perf.e[i].name; }

// One message for the console, taken by the plugin (ui never reaches the host).
char g_notice[200]; bool g_noticeReady = false;
void post(const char* m) { lstrcpynA(g_notice, m, sizeof(g_notice)); g_noticeReady = true; }

// //aio perf auto
const int  AUTO_PHASES = 7;                 // 6 measured + the ab run
const u32  AUTO_SETTLE = 90, AUTO_MEASURE = 600;
// "worst" : every demo AT ONCE -- the 18-member alliance, both chats scrolled to the top, the sphere demo, 32 fake buffs
// under the top bar. A combat stand-in made only of the demos (asked for 2026-11-24 instead of a measure in a real
// alliance) : nothing is injected into the model, so nothing real is touched, and all of it is put back.
const char* const AUTO_LABEL[AUTO_PHASES] = { "auto.base", "auto.alliance", "auto.chat", "auto.skillchain", "auto.worst", "auto.config", "auto.ab" };
struct Auto { int phase = -1; u32 frame = 0; bool prevWant = false; bool cfgWasOpen = false; bool buffDemo = false; };
Auto g_auto;

void auto_restore(ConfigPage* cfg) {
    char m[160];
    set_party_demo_total(0);
    ui_config().chWheel[0] = ui_config().chWheel[1] = -100000;   // back to the newest line (clamped at 0)
    scfx_toggle("off", m, sizeof(m));
    if (g_auto.buffDemo) { g_auto.buffDemo = false; if (buff_preview_toggle()) buff_preview_toggle(); }   // a toggle : make sure it ends OFF
    if (cfg) cfg->set_open(g_auto.cfgWasOpen);
}

} // namespace

int perf_dump(const char* label);

void perf_auto_tick(ConfigPage& cfg) {
    if (g_auto.phase < 0) return;
    perf::State& P = perf::g_perf;
    char m[200];
    const int ph = g_auto.phase;
    if (ph < AUTO_PHASES - 1) {
        if (g_auto.frame == 0) {
            switch (ph) {
                case 1: set_party_demo_total(18); break;
                case 2: ui_config().chWheel[0] = ui_config().chWheel[1] = 100000; break;   // as far up as the history goes
                case 3: set_party_demo_total(0); ui_config().chWheel[0] = ui_config().chWheel[1] = -100000;
                        scfx_toggle("all", m, sizeof(m)); break;
                case 4: set_party_demo_total(18); ui_config().chWheel[0] = ui_config().chWheel[1] = 100000;   // + the spheres, still on
                        if (!g_auto.buffDemo) { g_auto.buffDemo = true; if (!buff_preview_toggle()) buff_preview_toggle(); } break;
                case 5: set_party_demo_total(0); ui_config().chWheel[0] = ui_config().chWheel[1] = -100000;
                        scfx_toggle("off", m, sizeof(m));
                        if (g_auto.buffDemo) { g_auto.buffDemo = false; if (buff_preview_toggle()) buff_preview_toggle(); }
                        cfg.set_open(true); break;
                default: break;
            }
            _snprintf(m, sizeof(m), tr(">>> AioHud perf auto %d/%d : %s <<<", ">>> AioHud perf auto %d/%d : %s <<<"), ph + 1, AUTO_PHASES, AUTO_LABEL[ph] + 5);
            m[sizeof(m) - 1] = 0; post(m);
        }
        if (g_auto.frame == AUTO_SETTLE) perf::request_reset();               // applied at the next frame_begin
        if (g_auto.frame == AUTO_SETTLE + AUTO_MEASURE + 1) {                  // AUTO_MEASURE frames folded in since
            perf_dump(AUTO_LABEL[ph]);
            ++g_auto.phase; g_auto.frame = 0;
            return;
        }
        ++g_auto.frame;
        return;
    }
    // the last phase : what the instrumentation costs, everything else put back first
    if (g_auto.frame == 0) {
        auto_restore(&cfg);
        perf::set_on(false);
        perf::ab_start(1200);
        windower::debug::log("AIO PERF AB start : 1200 frames, alternating instrumented / bare (perf auto)");
        _snprintf(m, sizeof(m), tr(">>> AioHud perf auto %d/%d : ab <<<", ">>> AioHud perf auto %d/%d : ab <<<"), AUTO_PHASES, AUTO_PHASES);
        m[sizeof(m) - 1] = 0; post(m);
        g_auto.frame = 1;
        return;
    }
    if (P.abLeft || P.abDone) return;                                          // the plugin takes the ab result first
    g_auto.phase = -1;
    perf::set_on(g_auto.prevWant);
    windower::debug::log("AIO PERF auto done : %d phases dumped (auto.*) + one AB run", AUTO_PHASES - 1);
    post(tr(">>> AioHud perf auto : done -- aiohud_debug.log (AIO PERF dump [auto.*] + AB) <<<",
            ">>> AioHud perf auto : termine -- aiohud_debug.log (AIO PERF dump [auto.*] + AB) <<<"));
}

// The overlay : a monitoring card, not a text dump.
//   header   : a live dot, the title, a STATUS pill from AioHUD's share of the whole frame (OK / WATCH / HEAVY)
//   tiles    : AioHUD cost (avg + worst), the whole frame (ms + fps), the share (with a gauge), the draw calls
//   graph    : the last 60 frames -- the whole-frame time as a line over the 60 fps reference, AioHUD's part as bars
//   modules  : one row per scope, its cost as a coloured bar behind the numbers (green / amber / red), aligned columns
// Everything is read from the 60-frame window perf.h already keeps ; no allocation, no extra state.
// the largest size <= `sz` at which `t` fits in `maxW` (never below 70 % : past that, the layout is what is wrong)
static float perf_fit(Font* fo, const char* t, float sz, float maxW) { float s = sz; while (s > sz * 0.7f && fo->measure(t, s) > maxW) s *= 0.93f; return s; }
static u32 perf_sev_col(u32 us) { return us < 150u ? 0xFF4FC98Au : (us < 400u ? 0xFFE8B04Au : 0xFFE8605Au); }

void perf_overlay_draw(const Frame& f) {
    if (!perf::g_perf.want || g_auto.phase >= 0 || !f.font || !f.dev) return;   // perf auto measures without it (it was 0.34 ms of a 1.57 ms frame)
    PERF_SCOPE("perf.overlay");
    const perf::State& P = perf::g_perf;
    const u32 dev = f.dev;
    const float S = f.screenH > 0.0f ? f.screenH / 1080.0f : 1.0f;
    Font* fo = f.font;
    int idx[perf::CAP]; const int n = sorted_rows(idx, false);
    const int shown = n < 16 ? n : 16;

    // ---- the numbers ----
    unsigned long long dSum = 0, sSum = 0, tSum = 0;
    for (int i = 0; i < P.n; ++i) { dSum += P.e[i].wDraws; sSum += P.e[i].wStates; tSum += P.e[i].wTex; }
    const u32 ourAvg = perf::win_total_avg_us(), ourMax = perf::win_total_max_us();
    const u32 frAvg = perf::win_int_avg_us(), frMax = perf::win_int_max_us(), share = perf::win_share_x10();   // share : tenths of %
    const u32 fps = frAvg ? 1000000u / frAvg : 0u;
    const int status = ourAvg < 2500u ? 0 : (ourAvg < 5000u ? 1 : 2);   // by what AioHUD COSTS a frame (ms), not its share : 2 ms of a 17 ms frame is a normal full HUD
    static const u32 STC[3] = { 0xFF4FC98Au, 0xFFE8B04Au, 0xFFE8605Au };
    const char* stTxt[3] = { "OK", tr("WATCH", "A SURVEILLER"), tr("HEAVY", "LOURD") };

    // ---- layout ----
    const float pad = snap(12.0f * S), W = snap(500.0f * S);
    const float hdrH = snap(34.0f * S), tileH = snap(52.0f * S), graphH = snap(64.0f * S), rowH = snap(19.0f * S), thH = snap(20.0f * S), footH = snap(22.0f * S);
    const float H = snap(hdrH + pad + tileH + pad + graphH + pad + thH + rowH * (float)shown + footH + pad * 0.5f);
    const float X = snap(14.0f * S), Y = snap(f.screenH * 0.10f);
    char a[32], b[32], line[160];

    // ---- the card ----
    dColorQuadState(dev);
    rrect_glow(dev, X, Y + snap(3.0f * S), W, H, snap(10.0f * S), 0x90000000u, snap(14.0f * S));               // drop shadow
    rrect(dev, X, Y, W, H, snap(10.0f * S), 0xF2161B22u, 0xF20B0E13u);                                         // body
    rrect_stroke(dev, X, Y, W, H, snap(10.0f * S), 0x30FFFFFFu, 1.0f);                                          // hairline edge
    rrect_tb(dev, X + 1.0f, Y + 1.0f, W - 2.0f, hdrH, snap(9.0f * S), 0.0f, 0x16FFFFFFu, 0x04FFFFFFu);        // header lift
    grad_quad(dev, X + snap(10.0f * S), Y + 1.0f, W - snap(20.0f * S), 1.0f, 0x00FFFFFFu, 0x00FFFFFFu, 0x00FFFFFFu, 0x00FFFFFFu);
    grad_quad(dev, X + pad, Y + hdrH, W - 2.0f * pad, 1.0f, 0x14FFFFFFu, 0x14FFFFFFu, 0x14FFFFFFu, 0x14FFFFFFu);  // header rule

    // ---- header ----
    const float hy = Y + hdrH * 0.5f;
    const float pulse = 0.55f + 0.45f * (0.5f + 0.5f * sinf(f.t * 4.0f));
    disc(dev, snap(X + pad + 4.0f * S), snap(hy), snap(4.5f * S), (u32)(pulse * 90.0f) << 24 | (STC[status] & 0x00FFFFFFu));
    disc(dev, snap(X + pad + 4.0f * S), snap(hy), snap(3.0f * S), STC[status]);
    const float pillW = snap(fo->measure(stTxt[status], snap(11.0f * S)) + 18.0f * S), pillH = snap(18.0f * S);
    const float pillX = X + W - pad - pillW;
    rrect(dev, pillX, snap(hy - pillH * 0.5f), pillW, pillH, pillH * 0.5f, (STC[status] & 0x00FFFFFFu) | 0x30000000u, (STC[status] & 0x00FFFFFFu) | 0x24000000u);
    rrect_stroke(dev, pillX, snap(hy - pillH * 0.5f), pillW, pillH, pillH * 0.5f, (STC[status] & 0x00FFFFFFu) | 0x90000000u, 1.0f);
    fo->begin(dev);
    fo->draw_lc(dev, snap(X + pad + 14.0f * S), snap(hy), "AioHUD", snap(15.0f * S), 0xFFF4F6FAu, 0xFF000000u, 0.8f);
    fo->draw_lc(dev, snap(X + pad + 14.0f * S + fo->measure("AioHUD ", snap(15.0f * S))), snap(hy), tr("Performance", "Performance"), snap(15.0f * S), 0xFF8E9AAAu, 0xFF000000u, 0.8f);
    _snprintf(line, sizeof(line), tr("%u frames", "%u images"), P.framesInWin); line[sizeof(line) - 1] = 0;
    fo->draw_lc(dev, snap(pillX - 10.0f * S - fo->measure(line, snap(11.0f * S))), snap(hy), line, snap(11.0f * S), 0xFF6E7A8Au, 0xFF000000u, 0.6f);
    fo->draw_c(dev, snap(pillX + pillW * 0.5f), snap(hy), stTxt[status], snap(11.0f * S), STC[status], 0xFF000000u, 0.6f);

    // ---- tiles ----
    const float ty = Y + hdrH + pad, gap = snap(8.0f * S), tw = snap((W - 2.0f * pad - 3.0f * gap) / 4.0f);
    for (int t = 0; t < 4; ++t) {
        const float tx = X + pad + (tw + gap) * (float)t;
        dColorQuadState(dev);
        rrect(dev, tx, ty, tw, tileH, snap(6.0f * S), 0x12FFFFFFu, 0x06FFFFFFu);
        rrect_stroke(dev, tx, ty, tw, tileH, snap(6.0f * S), 0x18FFFFFFu, 1.0f);
        const char* lab = t == 0 ? "AIOHUD" : t == 1 ? tr("FRAME", "IMAGE") : t == 2 ? tr("SHARE", "PART") : tr("DRAW CALLS", "APPELS");
        char big[32], sub[48]; u32 bigCol = 0xFFF4F6FAu;
        if (t == 0) { fmt_ms(a, sizeof(a), ourAvg); fmt_ms(b, sizeof(b), ourMax); _snprintf(big, 32, "%s ms", a); _snprintf(sub, 48, "max %s", b); bigCol = STC[status]; }
        else if (t == 1) { fmt_ms(a, sizeof(a), frAvg); fmt_ms(b, sizeof(b), frMax); _snprintf(big, 32, "%s ms", a); _snprintf(sub, 48, "%u fps  -  max %s", fps, b); }
        else if (t == 2) { _snprintf(big, 32, "%u.%u %%", share / 10, share % 10); sub[0] = 0; bigCol = STC[status]; }
        else { _snprintf(big, 32, "%u", perf::win_avg(dSum)); _snprintf(sub, 48, tr("states %u  -  tex %u", "etats %u  -  tex %u"), perf::win_avg(sSum), perf::win_avg(tSum)); }
        big[31] = 0; sub[47] = 0;
        if (t == 2) {                                                      // the share gauge (0..20 %)
            const float gx = tx + snap(10.0f * S), gw = tw - snap(20.0f * S), gy = ty + tileH - snap(12.0f * S), gh = snap(4.0f * S);
            const float k = clampf((float)share / 200.0f, 0.0f, 1.0f);
            dColorQuadState(dev);
            rrect(dev, gx, gy, gw, gh, gh * 0.5f, 0x22FFFFFFu, 0x22FFFFFFu);
            if (k > 0.0f) rrect(dev, gx, gy, snap(gw * k) < gh ? gh : snap(gw * k), gh, gh * 0.5f, STC[status], STC[status]);
        }
        fo->begin(dev);
        const float inner = tw - snap(20.0f * S);                     // every line FITS its tile (measured, shrunk if needed)
        fo->draw_lc(dev, snap(tx + 10.0f * S), snap(ty + 12.0f * S), lab, perf_fit(fo, lab, snap(10.0f * S), inner), 0xFF7C8898u, 0xFF000000u, 0.5f);
        fo->draw_lc(dev, snap(tx + 10.0f * S), snap(ty + 29.0f * S), big, perf_fit(fo, big, snap(17.0f * S), inner), bigCol, 0xFF000000u, 0.8f);
        if (sub[0]) fo->draw_lc(dev, snap(tx + 10.0f * S), snap(ty + 44.0f * S), sub, perf_fit(fo, sub, snap(10.0f * S), inner), 0xFF7C8898u, 0xFF000000u, 0.5f);
    }

    // ---- the graph : last 60 frames ----
    const float gx0 = X + pad, gy0 = ty + tileH + pad, gw = W - 2.0f * pad, gh = graphH;
    dColorQuadState(dev);
    rrect(dev, gx0, gy0, gw, gh, snap(6.0f * S), 0x0CFFFFFFu, 0x04FFFFFFu);
    {
        const u32 N = P.framesInWin;
        u32 top = 33333u;                                                   // scale : at least 30 fps worth, more if a frame went past it
        for (u32 i = 0; i < N; ++i) if (P.rIntUs[i] > top) top = P.rIntUs[i];
        const float ix = gx0 + snap(6.0f * S), iw = gw - snap(12.0f * S), iy = gy0 + snap(6.0f * S), ih = gh - snap(12.0f * S);
        const float ref = iy + ih * (1.0f - 16667.0f / (float)top);         // the 60 fps line
        for (float xx = ix; xx < ix + iw; xx += snap(6.0f * S)) grad_quad(dev, snap(xx), snap(ref), snap(3.0f * S), 1.0f, 0x40FFFFFFu, 0x40FFFFFFu, 0x40FFFFFFu, 0x40FFFFFFu);
        fo->begin(dev);
        fo->draw_lc(dev, snap(ix + 2.0f * S), snap(ref - 7.0f * S), "60 fps", snap(9.0f * S), 0xFF6E7A8Au, 0xFF000000u, 0.5f);
        {   // the graph's legend, top right : the white line = the whole frame, the blue bars = AioHUD
            const char* l1 = tr("frame", "image"); const char* l2 = "AioHUD";
            const float ls = snap(9.0f * S), lx2 = ix + iw - fo->measure(l2, ls), lx1 = lx2 - snap(22.0f * S) - fo->measure(l1, ls), ly = iy + snap(4.0f * S);
            fo->draw_lc(dev, snap(lx1), snap(ly), l1, ls, 0xFFB4BDCAu, 0xFF000000u, 0.5f);
            fo->draw_lc(dev, snap(lx2), snap(ly), l2, ls, 0xFF5AB4FFu, 0xFF000000u, 0.5f);
            dColorQuadState(dev);
            grad_quad(dev, snap(lx1 - 12.0f * S), snap(ly), snap(9.0f * S), snap(1.6f * S), 0xE0E8ECF2u, 0xE0E8ECF2u, 0xE0E8ECF2u, 0xE0E8ECF2u);
            grad_quad(dev, snap(lx2 - 9.0f * S), snap(ly - 3.0f * S), snap(5.0f * S), snap(6.0f * S), 0xC05AB4FFu, 0xC05AB4FFu, 0xC05AB4FFu, 0xC05AB4FFu);
        }
        dColorQuadState(dev);
        if (N > 1) {
            const float step = iw / (float)(perf::WIN - 1);
            float px = 0.0f, py = 0.0f;
            for (u32 k = 0; k < N; ++k) {
                const int slot = (int)((P.ring + perf::WIN - N + k) % perf::WIN);   // oldest -> newest
                unsigned long long ours = 0; for (int e = 0; e < P.n; ++e) ours += P.e[e].rSelfUs[slot];
                const float xk = ix + step * (float)(perf::WIN - N + k);
                const float bh = ih * 0.5f * clampf((float)ours / 5000.0f, 0.0f, 1.0f);       // AioHUD's part : a bar from the floor, its OWN scale (5 ms = half the graph) -- at the frame's scale 2 ms was 2 px
                if (bh > 0.5f) grad_quad(dev, snap(xk - step * 0.35f), snap(iy + ih - bh), snap(step * 0.7f) < 1.0f ? 1.0f : snap(step * 0.7f), snap(bh) < 1.0f ? 1.0f : snap(bh),
                                         0xC05AB4FFu, 0xC05AB4FFu, 0x605AB4FFu, 0x605AB4FFu);
                const float yk = iy + ih * (1.0f - clampf((float)P.rIntUs[slot] / (float)top, 0.0f, 1.0f));   // the whole frame : a line
                if (k > 0 && P.rIntUs[slot]) seg_soft(dev, px, py, xk, yk, snap(1.6f * S), 0xE0E8ECF2u);
                px = xk; py = yk;
            }
        }
    }

    // ---- the module table ----
    float y = gy0 + gh + pad;
    const float cName = X + pad + snap(8.0f * S), cR[5] = { X + W - pad - snap(206.0f * S), X + W - pad - snap(154.0f * S), X + W - pad - snap(102.0f * S), X + W - pad - snap(50.0f * S), X + W - pad - snap(6.0f * S) };
    const char* hdr[6] = { tr("MODULE", "MODULE"), tr("AVG ms", "MOY ms"), "MAX ms", "DRAWS", tr("STATES", "ETATS"), "TEX" };
    fo->begin(dev);
    fo->draw_lc(dev, snap(cName), snap(y + thH * 0.5f), hdr[0], snap(10.0f * S), 0xFF7C8898u, 0xFF000000u, 0.5f);
    for (int c = 0; c < 5; ++c) fo->draw_lc(dev, snap(cR[c] - fo->measure(hdr[c + 1], snap(10.0f * S))), snap(y + thH * 0.5f), hdr[c + 1], snap(10.0f * S), 0xFF7C8898u, 0xFF000000u, 0.5f);
    y += thH;
    u32 rowMax = 1; for (int r = 0; r < shown; ++r) { const u32 v = perf::win_avg(P.e[idx[r]].wSelfUs); if (v > rowMax) rowMax = v; }
    for (int r = 0; r < shown; ++r) {
        const perf::Entry& E = P.e[idx[r]];
        const u32 avg = perf::win_avg(E.wSelfUs);
        dColorQuadState(dev);
        if (r & 1) grad_quad(dev, X + pad, y, W - 2.0f * pad, rowH, 0x08FFFFFFu, 0x08FFFFFFu, 0x08FFFFFFu, 0x08FFFFFFu);   // zebra
        const float bw = (cR[0] - cName - snap(14.0f * S)) * clampf((float)avg / (float)rowMax, 0.0f, 1.0f);              // the cost bar, under the name
        const u32 sc = perf_sev_col(avg);
        if (bw > 1.0f) rrect(dev, cName - snap(4.0f * S), y + snap(3.0f * S), bw, rowH - snap(6.0f * S), snap(3.0f * S), (sc & 0x00FFFFFFu) | 0x38000000u, (sc & 0x00FFFFFFu) | 0x20000000u);
        char col[5][24];
        fmt_ms(col[0], 24, avg);
        fmt_ms(col[1], 24, perf::win_max_us(E));
        _snprintf(col[2], 24, "%u", perf::win_avg(E.wDraws));  col[2][23] = 0;
        _snprintf(col[3], 24, "%u", perf::win_avg(E.wStates)); col[3][23] = 0;
        _snprintf(col[4], 24, "%u", perf::win_avg(E.wTex));    col[4][23] = 0;
        const u32 c0 = idx[r] == 0 ? 0xFF8A94A4u : 0xFFE2E7EEu;
        fo->begin(dev);
        fo->draw_lc(dev, snap(cName), snap(y + rowH * 0.5f), row_name(idx[r]), perf_fit(fo, row_name(idx[r]), snap(12.0f * S), cR[0] - cName - snap(48.0f * S)), c0, 0xFF000000u, 0.6f);
        for (int c = 0; c < 5; ++c)
            fo->draw_lc(dev, snap(cR[c] - fo->measure(col[c], snap(12.0f * S))), snap(y + rowH * 0.5f), col[c], snap(12.0f * S),
                        c == 0 ? sc : (c == 1 ? 0xFFB4BDCAu : 0xFF8A94A4u), 0xFF000000u, 0.6f);
        y += rowH;
    }
    // ---- footer ----
    fo->begin(dev);
    fo->draw_lc(dev, snap(X + pad), snap(y + footH * 0.5f), "//aio perf off  -  reset  -  dump", snap(10.0f * S), 0xFF5E6A7Au, 0xFF000000u, 0.5f);
    dColorQuadState(dev);
}

// Every row since the last reset -> aiohud_debug.log. Per-frame figures are averages over the frames measured.
int perf_dump(const char* label) {
    const perf::State& P = perf::g_perf;
    const u32 fr = P.frames ? P.frames : 1;
    unsigned long long tot = 0, d = 0, rs = 0, tss = 0, tex = 0;
    for (int i = 0; i < P.n; ++i) { tot += P.e[i].sumUs; d += P.e[i].sumDraws; rs += P.e[i].sumRs; tss += P.e[i].sumTss; tex += P.e[i].sumTex; }
    windower::debug::log("AIO PERF dump [%s] : %u frame(s) measured since reset ; per frame : total %u us, draws %u, rs %u, tss %u, tex %u",
                         label && *label ? label : "-", P.frames, (u32)(tot / fr), (u32)(d / fr), (u32)(rs / fr), (u32)(tss / fr), (u32)(tex / fr));
    // the whole frame : the interval between two of our frames (the game's work, the Present, us), and our share
    { const u32 ivAvg = P.nInt ? (u32)(P.sumIntUs / P.nInt) : 0u, ours = (u32)(tot / fr);
      const u32 shx10 = ivAvg ? (u32)((unsigned long long)ours * 1000 / ivAvg) : 0u;
      windower::debug::log("AIO PERF   whole frame : avg %u us, max %u us over %u interval(s) (%u fps) ; AioHUD %u us of it = %u.%u %%",
                           ivAvg, P.maxIntUs, P.nInt, ivAvg ? 1000000u / ivAvg : 0u, ours, shx10 / 10, shx10 % 10); }
    windower::debug::log("AIO PERF   %-20s %8s %8s %8s %6s %7s %8s %7s %7s %6s %5s %6s", "scope", "avg_us", "max_us", "incl_us",
                         "pct", "draws", "prims", "rs", "tss", "tex", "fvf", "runs");
    int idx[perf::CAP]; const int n = sorted_rows(idx, true);
    for (int r = 0; r < n; ++r) {
        const perf::Entry& E = P.e[idx[r]];
        const u32 pctx10 = tot ? (u32)(E.sumUs * 1000 / tot) : 0;
        windower::debug::log("AIO PERF   %-20s %8u %8u %8u %4u.%u %7u %8u %7u %7u %6u %5u %6u", row_name(idx[r]),
                             (u32)(E.sumUs / fr), E.maxUs, (u32)(E.sumInclUs / fr), pctx10 / 10, pctx10 % 10,
                             (u32)(E.sumDraws / fr), (u32)(E.sumPrims / fr), (u32)(E.sumRs / fr), (u32)(E.sumTss / fr),
                             (u32)(E.sumTex / fr), (u32)(E.sumFvf / fr), E.framesSeen);
    }
    windower::debug::log("AIO PERF end (avg = self time per frame over every measured frame ; max = worst single frame ; incl = with nested scopes ; runs = frames the scope ran in)");
    return n;
}

// The command. `rest` = what follows "perf". Writes one console line into `msg`.
void perf_command(const char* rest, char* msg, int cap) {
    while (*rest == ' ') ++rest;
    if (!strncmp(rest, "on", 2)) {
        perf::set_on(true); perf::request_reset();
        _snprintf(msg, cap, "%s", tr(">>> AioHud perf : ON (overlay ; //aio perf dump writes the log, //aio perf off stops) <<<",
                                     ">>> AioHud perf : ON (surimpression ; //aio perf dump ecrit le log, //aio perf off arrete) <<<"));
    } else if (!strncmp(rest, "auto", 4)) {
        if (g_auto.phase >= 0) { _snprintf(msg, cap, "%s", tr(">>> AioHud perf auto : already running (//aio perf off stops it) <<<", ">>> AioHud perf auto : deja en cours (//aio perf off l'arrete) <<<")); msg[cap - 1] = 0; return; }
        g_auto = Auto(); g_auto.phase = 0; g_auto.prevWant = perf::wanted(); g_auto.cfgWasOpen = false;
        perf::set_on(true);
        windower::debug::log("AIO PERF auto start : %d phases x (%u settle + %u measured frames), then AB", AUTO_PHASES - 1, AUTO_SETTLE, AUTO_MEASURE);
        _snprintf(msg, cap, tr(">>> AioHud perf auto : about %d s, stay put -- the plugin sets up each case itself <<<",
                               ">>> AioHud perf auto : environ %d s, ne bouge pas -- le plugin prepare chaque cas lui-meme <<<"),
                  (int)((AUTO_PHASES - 1) * (AUTO_SETTLE + AUTO_MEASURE) + 1200) / 60);
    } else if (!strncmp(rest, "filter", 6)) {   // //aio perf filter on|off : the P4 state shadow (gfx/d3d.h), for an A/B or as the way out
        const char* q = rest + 6; while (*q == ' ') ++q;
        if (!strncmp(q, "off", 3)) dStateFilter(false); else if (!strncmp(q, "on", 2)) dStateFilter(true);
        windower::debug::log("AIO PERF state filter %s", dStateFiltering() ? "ON" : "OFF");
        _snprintf(msg, cap, tr(">>> AioHud perf : redundant-state filter %s <<<", ">>> AioHud perf : filtre des etats redondants %s <<<"),
                  dStateFiltering() ? "ON" : "OFF");
    } else if (!strncmp(rest, "off", 3)) {
        if (g_auto.phase >= 0) { auto_restore(nullptr); g_auto.phase = -1; windower::debug::log("AIO PERF auto stopped by //aio perf off"); }
        perf::set_on(false);
        _snprintf(msg, cap, "%s", tr(">>> AioHud perf : OFF <<<", ">>> AioHud perf : OFF <<<"));
    } else if (!strncmp(rest, "reset", 5)) {
        perf::request_reset();
        _snprintf(msg, cap, "%s", tr(">>> AioHud perf : reset <<<", ">>> AioHud perf : remis a zero <<<"));
    } else if (!strncmp(rest, "dump", 4)) {
        const char* lab = rest + 4; while (*lab == ' ') ++lab;   // //aio perf dump <label> : names the scenario in the log
        const int n = perf::g_perf.frames ? perf_dump(lab) : 0;
        if (n) _snprintf(msg, cap, tr(">>> AioHud perf : %d row(s), %u frame(s) -> aiohud_debug.log (AIO PERF) <<<",
                                      ">>> AioHud perf : %d ligne(s), %u image(s) -> aiohud_debug.log (AIO PERF) <<<"), n, perf::g_perf.frames);
        else   _snprintf(msg, cap, "%s", tr(">>> AioHud perf : nothing measured yet -- //aio perf on first <<<",
                                            ">>> AioHud perf : rien de mesure -- //aio perf on d'abord <<<"));
    } else if (!strncmp(rest, "ab", 2)) {
        u32 frames = 600; { const char* q = rest + 2; while (*q == ' ') ++q; if (*q >= '0' && *q <= '9') frames = (u32)atoi(q); }
        if (frames < 60) frames = 60;
        if (frames > 2 * perf::State::AB_MAX) frames = 2 * perf::State::AB_MAX;
        perf::ab_start(frames);
        windower::debug::log("AIO PERF AB start : %u frames, alternating instrumented / bare", frames);
        _snprintf(msg, cap, tr(">>> AioHud perf ab : %u frames, alternating with / without instrumentation -- result in the chat <<<",
                               ">>> AioHud perf ab : %u images, avec / sans instrumentation en alternance -- resultat dans le chat <<<"), frames);
    } else {
        _snprintf(msg, cap, "%s", tr("//aio perf on | off | reset | dump [label] | ab [frames] | auto | filter on|off", "//aio perf on | off | reset | dump [etiquette] | ab [images] | auto | filter on|off"));
    }
    msg[cap - 1] = 0;
}

// A finished //aio perf ab : log it and phrase it once (the caller prints). false = nothing to report.
bool perf_ab_take(char* msg, int cap) {
    perf::State& P = perf::g_perf;
    if (g_noticeReady) { g_noticeReady = false; lstrcpynA(msg, g_notice, cap); return true; }   // perf auto's progress first
    if (!P.abDone) return false;
    P.abDone = false;
    const u32 on  = P.abN[0] ? (u32)(P.abSum[0] / P.abN[0]) : 0;
    const u32 off = P.abN[1] ? (u32)(P.abSum[1] / P.abN[1]) : 0;
    const u32 medOn = perf::ab_median(0), medOff = perf::ab_median(1);
    const int diff = (int)medOn - (int)medOff;
    windower::debug::log("AIO PERF AB done : instrumented %u frame(s) median %u us mean %u us ; bare %u frame(s) median %u us mean %u us ; cost (medians) %d us/frame",
                         P.abN[0], medOn, on, P.abN[1], medOff, off, diff);
    char a[32], b[32]; fmt_ms(a, sizeof(a), medOn); fmt_ms(b, sizeof(b), medOff);
    _snprintf(msg, cap, tr(">>> AioHud perf ab : with %s ms, without %s ms per frame, medians (%d us) -- aiohud_debug.log <<<",
                           ">>> AioHud perf ab : avec %s ms, sans %s ms par image, medianes (%d us) -- aiohud_debug.log <<<"), a, b, diff);
    msg[cap - 1] = 0;
    return true;
}

} // namespace aio
