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
//                      skillchain (the sphere demo, every chain in turn), config (the window open), then an ab run.
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
const int  AUTO_PHASES = 6;                 // 5 measured + the ab run
const u32  AUTO_SETTLE = 90, AUTO_MEASURE = 600;
const char* const AUTO_LABEL[AUTO_PHASES] = { "auto.base", "auto.alliance", "auto.chat", "auto.skillchain", "auto.config", "auto.ab" };
struct Auto { int phase = -1; u32 frame = 0; bool prevWant = false; bool cfgWasOpen = false; };
Auto g_auto;

void auto_restore(ConfigPage* cfg) {
    char m[160];
    set_party_demo_total(0);
    ui_config().chWheel[0] = ui_config().chWheel[1] = -100000;   // back to the newest line (clamped at 0)
    scfx_toggle("off", m, sizeof(m));
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
                case 4: scfx_toggle("off", m, sizeof(m)); cfg.set_open(true); break;
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

void perf_overlay_draw(const Frame& f) {
    if (!perf::g_perf.want || g_auto.phase >= 0 || !f.font || !f.dev) return;   // perf auto measures without it (it was 0.34 ms of a 1.57 ms frame)
    PERF_SCOPE("perf.overlay");
    const perf::State& P = perf::g_perf;
    const u32 dev = f.dev;
    const float S = f.screenH > 0.0f ? f.screenH / 1080.0f : 1.0f;
    const float z = 13.0f * S, lh = 16.0f * S, pad = 8.0f * S;
    int idx[perf::CAP]; const int n = sorted_rows(idx, false);
    const int shown = n < 24 ? n : 24;
    // columns : name | avg | max | draws | states | tex
    const float cName = 150.0f * S, cNum = 58.0f * S;
    const float W = snap(pad * 2.0f + cName + cNum * 5.0f), H = snap(pad * 2.0f + lh * (float)(shown + 3));
    const float X = snap(12.0f * S), Y = snap(f.screenH * 0.12f);
    dColorQuadState(dev);
    rrect(dev, X, Y, W, H, 6.0f * S, 0xE0101418u, 0xE0080A0Cu);

    Font* fo = f.font;
    fo->begin(dev);
    char a[32], b[32], line[160];
    float y = Y + pad;
    {
        unsigned long long d = 0, s = 0, t = 0;
        for (int i = 0; i < P.n; ++i) { d += P.e[i].wDraws; s += P.e[i].wStates; t += P.e[i].wTex; }
        fmt_ms(a, sizeof(a), perf::win_total_avg_us()); fmt_ms(b, sizeof(b), perf::win_total_max_us());
        _snprintf(line, sizeof(line), tr("AioHUD perf  %u frames   total %s ms  (max %s)   draws %u  states %u  tex %u",
                                         "AioHUD perf  %u images   total %s ms  (max %s)   draws %u  etats %u  tex %u"),
                  P.framesInWin, a, b, perf::win_avg(d), perf::win_avg(s), perf::win_avg(t));
        line[sizeof(line) - 1] = 0;
        fo->draw(dev, X + pad, y, line, z, 0xFFF0D890u, 0xFF000000u, 1.0f);
        y += lh;
        // the WHOLE frame (game + Present + us) and our share of it -- what decides whether our ms cost frames
        const u32 iv = perf::win_int_avg_us(), sh = perf::win_share_x10();
        fmt_ms(a, sizeof(a), iv); fmt_ms(b, sizeof(b), perf::win_int_max_us());
        _snprintf(line, sizeof(line), tr("whole frame %s ms (max %s)  %u fps   AioHUD %u.%u %%",
                                         "image entiere %s ms (max %s)  %u fps   AioHUD %u.%u %%"),
                  a, b, iv ? 1000000u / iv : 0u, sh / 10, sh % 10);
        line[sizeof(line) - 1] = 0;
        fo->draw(dev, X + pad, y, line, z, 0xFFF0D890u, 0xFF000000u, 1.0f);
        y += lh;
    }
    const char* hdr[6] = { tr("module", "module"), tr("avg ms", "moy ms"), "max ms", "draws", tr("states", "etats"), "tex" };
    fo->draw(dev, X + pad, y, hdr[0], z, 0xFF9AA4B4u, 0xFF000000u, 1.0f);
    for (int c = 1; c < 6; ++c) fo->draw(dev, X + pad + cName + cNum * (float)c - fo->measure(hdr[c], z), y, hdr[c], z, 0xFF9AA4B4u, 0xFF000000u, 1.0f);
    y += lh;
    for (int r = 0; r < shown; ++r) {
        const perf::Entry& E = P.e[idx[r]];
        char col[5][24];
        fmt_ms(col[0], 24, perf::win_avg(E.wSelfUs));
        fmt_ms(col[1], 24, perf::win_max_us(E));
        _snprintf(col[2], 24, "%u", perf::win_avg(E.wDraws));  col[2][23] = 0;
        _snprintf(col[3], 24, "%u", perf::win_avg(E.wStates)); col[3][23] = 0;
        _snprintf(col[4], 24, "%u", perf::win_avg(E.wTex));    col[4][23] = 0;
        const u32 c0 = idx[r] == 0 ? 0xFFB0B0B0u : 0xFFE8ECF4u;
        fo->draw(dev, X + pad, y, row_name(idx[r]), z, c0, 0xFF000000u, 1.0f);
        for (int c = 0; c < 5; ++c) fo->draw(dev, X + pad + cName + cNum * (float)(c + 1) - fo->measure(col[c], z), y, col[c], z, c0, 0xFF000000u, 1.0f);
        y += lh;
    }
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
