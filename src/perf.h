// perf.h -- //aio perf : what one frame of AioHUD actually costs, measured in the game, per module.
//
// WHY THIS EXISTS. The 2026-11 audit (docs/audits/AUDIT_AIOHUD.md, section 2) listed ten performance suspects,
// P1..P10, and every cost in that table was an estimate read off the code. Optimising from estimates is how a
// week goes into the cheap thing while the expensive one stays. This measures first.
//
// WHAT IT MEASURES, per named scope, per frame:
//   self time  -- the scope's own QueryPerformanceCounter time MINUS its child scopes', so the rows ADD UP to the
//                 frame (a nested scope -- the skillchain spheres inside the Skillchains box -- is not counted
//                 twice). The root scope "(frame)" keeps what no scope claimed : its self time is "(other)".
//   draws      -- DrawPrimitiveUP calls, and the primitives they carried
//   rs / tss / tex / fvf -- SetRenderState, SetTextureStageState, SetTexture, SetVertexShader calls. CALLS, not
//                 changes : nothing filters a redundant one today (that is P4), so a call IS a state change sent
//                 to the driver, and the number is exactly what a filter would have to beat.
// The D3D counters sit in d3d.h's wrappers, the single path every draw and state goes through (the fake-device test
// t_perf.cpp proves the counts equal the calls the device received). They are charged to the INNERMOST open scope.
//
// ZERO COST WHEN OFF. Every hook is `if (g_perf.on)` on one global byte -- no clock read, no store, no call -- and
// `on` only changes at a frame boundary (frame_begin), so a scope can never be opened in one state and closed in the
// other. No allocation, ever : fixed tables, the names copied into them.
//
// Layer : NONE -- it sits at the root of src/ beside retry_clock.h, because every layer needs to time itself : gfx
// counts its D3D calls here, ui and the plugin open scopes, and model/ times the pieces of poll_game_state (the
// first in-game run found a 12-16 ms spike there, 2026-11-24 ; under gfx/ the dependency rule kept model out). It
// knows nothing of the game or of D3D. The scope NAMES are the callers' business.
#pragma once
#include "windower.h"
#include <algorithm>   // std::nth_element : the ab medians

namespace aio { namespace perf {

using windower::u32;

const int CAP   = 96;   // named scopes (+ the root). One more name is charged to the overflow row, never dropped silently.
                        // 64 at first ; the fm_tick rows (2026-11-24) brought the registered names to ~61, and a row
                        // lost to "(overflow)" is exactly the row being looked for.
const int WIN   = 60;   // the sliding window, in frames
const int DEPTH = 16;   // nesting

struct Counters { u32 draws, prims, rs, tss, tex, fvf; };

struct Entry {
    char     name[24];
    // this frame (reset at frame_begin)
    long long selfT, inclT;    // QPC ticks
    u32      calls;
    Counters c;
    // the window : one slot per frame, and running sums kept in step (no 60-wide re-sum per frame)
    u32      rSelfUs[WIN], rDraws[WIN], rStates[WIN], rTex[WIN], rCalls[WIN];
    unsigned long long wSelfUs; unsigned long long wDraws, wStates, wTex, wCalls;
    // since //aio perf reset : the totals the dump prints
    unsigned long long sumUs, sumInclUs, sumDraws, sumPrims, sumRs, sumTss, sumTex, sumFvf, sumCalls;
    u32      maxUs;            // worst single frame (self), since reset
    u32      framesSeen;       // frames in which this scope ran at least once
};

struct State {
    bool     on;               // counting THIS frame
    bool     want;             // what //aio perf on/off asked for -> applied at the next frame_begin
    bool     wantReset;
    bool     inFrame;
    int      n;                // entries in use ; [0] = "(frame)", [1] = "(overflow)"
    Entry    e[CAP];
    int      stack[DEPTH]; long long t0[DEPTH], childT[DEPTH]; int depth;
    Counters* top;             // the innermost open scope's counters : what the D3D hooks bump
    Counters  sink;            // where they go between frames (never read)
    int      ring;             // the slot the NEXT frame_end writes
    u32      framesInWin;      // 0..WIN
    u32      frames;           // since reset
    long long freq;
    // //aio perf ab : alternate a fully-instrumented frame with a bare one (only the frame total timed) -- the
    // in-game measurement of what the instrumentation itself costs. 0 = not running.
    u32      abLeft;           // frames still to run
    bool     abBare;           // this frame is a bare one
    bool     abFrame;          // this frame BEGAN in an ab run. //aio perf ab is typed mid-frame (drain_commands) : the
                               // first version timed that frame's end against an unset t0 -- one sample of "the uptime"
                               // (6.5 s per frame on average, measured in game 2026-11-24). Only a begun frame counts.
    long long abT0;
    unsigned long long abSum[2]; u32 abN[2];   // [0] instrumented, [1] bare : frame totals (us)
    // ...and every sample, for the MEDIAN. The first in-game run gave the mean only, and one 15 ms poll spike landing
    // on one side turned it into "the instrumentation makes the frame 126 us FASTER" -- the mean of a frame time with
    // rare spikes measures the spikes. The median does not.
    static const u32 AB_MAX = 18000;
    u32      abS[2][AB_MAX];
    bool     abDone;           // a finished run waiting to be reported
    // THE WHOLE FRAME, not just ours (2026-11-24) : the interval from one frame_begin to the next -- the game's own
    // work, the Present (and any vsync wait in it), and AioHUD. AioHUD runs on the game's thread, so its share of this
    // is what decides whether its milliseconds cost frames. Measured while counting ; 0 = no interval this frame (the
    // first after perf was switched on, or a gap over 2 s : a load, a minimised window).
    long long lastBegin;
    u32      curIntUs;
    u32      rIntUs[WIN];
    unsigned long long wIntUs; u32 wIntN;                     // window : sum and count of the valid intervals
    unsigned long long sumIntUs; u32 nInt, maxIntUs;          // since reset
};

inline State g_perf = {};

inline long long now() { LARGE_INTEGER c; QueryPerformanceCounter(&c); return c.QuadPart; }
inline u32 to_us(long long t) {
    if (g_perf.freq <= 0) { LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_perf.freq = f.QuadPart > 0 ? f.QuadPart : 1; }
    return t <= 0 ? 0u : (u32)(t * 1000000 / g_perf.freq);
}

// ---- the D3D hooks (d3d.h) : one byte test when off ----
inline void on_draw(u32 prims) { if (g_perf.on) { ++g_perf.top->draws; g_perf.top->prims += prims; } }
inline void on_rs()  { if (g_perf.on) ++g_perf.top->rs; }
inline void on_tss() { if (g_perf.on) ++g_perf.top->tss; }
inline void on_tex() { if (g_perf.on) ++g_perf.top->tex; }
inline void on_fvf() { if (g_perf.on) ++g_perf.top->fvf; }

// ---- the table ----
inline int find(const char* name) {
    for (int i = 0; i < g_perf.n; ++i) if (!lstrcmpA(g_perf.e[i].name, name)) return i;
    return -1;
}
inline int reg(const char* name) {           // name -> id ; the overflow row when the table is full
    if (g_perf.n == 0) {                      // the two fixed rows
        lstrcpynA(g_perf.e[0].name, "(frame)", sizeof(g_perf.e[0].name));
        lstrcpynA(g_perf.e[1].name, "(overflow)", sizeof(g_perf.e[1].name));
        g_perf.n = 2;
    }
    const int k = find(name);
    if (k >= 0) return k;
    if (g_perf.n >= CAP) return 1;
    lstrcpynA(g_perf.e[g_perf.n].name, name, sizeof(g_perf.e[0].name));
    return g_perf.n++;
}

inline void begin(int id) {
    if (!g_perf.on || !g_perf.inFrame) return;
    if (id < 0 || id >= g_perf.n) id = 1;
    if (g_perf.depth >= DEPTH) return;        // too deep : charged to the parent, which is still open
    const int d = g_perf.depth++;
    g_perf.stack[d] = id; g_perf.childT[d] = 0; g_perf.t0[d] = now();
    ++g_perf.e[id].calls;
    g_perf.top = &g_perf.e[id].c;
}
// Close the innermost scope. `id` must match what is open -- a mismatch (a begin skipped by an early return, an SEH
// unwind) is closed up to it, never left to corrupt the parents.
inline void end(int id) {
    if (!g_perf.on || !g_perf.inFrame || g_perf.depth <= 1) return;   // depth 1 = only the root : never closed here
    int at = g_perf.depth - 1;
    while (at >= 1 && g_perf.stack[at] != id) --at;
    if (at < 1) return;                       // not open (its begin was skipped) : nothing to close
    const long long t = now();
    while (g_perf.depth - 1 >= at) {
        const int d = --g_perf.depth;
        const long long incl = t - g_perf.t0[d];
        Entry& E = g_perf.e[g_perf.stack[d]];
        E.inclT += incl; E.selfT += incl - g_perf.childT[d];
        g_perf.childT[d - 1] += incl;
    }
    g_perf.top = &g_perf.e[g_perf.stack[g_perf.depth - 1]].c;
}
inline int  depth() { return g_perf.depth; }
// After an SEH unwind : close everything opened above `d` (the scopes the fault skipped).
inline void unwind_to(int d) { if (g_perf.on && g_perf.inFrame && d >= 1 && g_perf.depth > d) end(g_perf.stack[d]); }

// ---- the frame ----
inline void frame_begin() {
    State& P = g_perf;
    if (P.wantReset) {
        P.wantReset = false;
        for (int i = 0; i < CAP; ++i) { char nm[24]; lstrcpynA(nm, P.e[i].name, 24); P.e[i] = Entry(); lstrcpynA(P.e[i].name, nm, 24); }
        P.ring = 0; P.framesInWin = 0; P.frames = 0;
        P.wIntUs = 0; P.wIntN = 0; P.sumIntUs = 0; P.nInt = 0; P.maxIntUs = 0;
        for (int i = 0; i < WIN; ++i) P.rIntUs[i] = 0;
    }
    P.abFrame = P.abLeft != 0;
    if (P.abFrame) { P.abBare = !P.abBare; P.on = !P.abBare; P.abT0 = now(); }
    else          P.on = P.want;
    P.inFrame = true;
    P.top = &P.sink;
    if (!P.on) { P.lastBegin = 0; P.curIntUs = 0; return; }   // an interval spanning frames we did not watch means nothing
    { const long long t = now();
      const u32 iv = P.lastBegin ? to_us(t - P.lastBegin) : 0u;
      P.curIntUs = iv < 2000000u ? iv : 0u;
      P.lastBegin = t; }
    if (P.n == 0) reg("(frame)");
    for (int i = 0; i < P.n; ++i) { Entry& E = P.e[i]; E.selfT = E.inclT = 0; E.calls = 0; E.c = Counters(); }
    P.depth = 0;
    P.stack[0] = 0; P.childT[0] = 0; P.t0[0] = now(); P.depth = 1;
    ++P.e[0].calls;
    P.top = &P.e[0].c;
}

inline void frame_book();   // below : close the scopes, fold this frame into the window and the totals
inline void frame_end() {
    State& P = g_perf;
    if (!P.inFrame) return;
    P.inFrame = false;
    if (P.on) frame_book();
    P.top = &P.sink;
    if (P.abFrame && P.abLeft) {              // AFTER the bookkeeping : what it costs is part of what is measured
        const u32 us = to_us(now() - P.abT0);
        const int k = P.abBare ? 1 : 0;
        if (P.abN[k] < State::AB_MAX) P.abS[k][P.abN[k]] = us;
        P.abSum[k] += us; ++P.abN[k];
        if (--P.abLeft == 0) { P.abDone = true; P.abBare = false; }   // rule10-ok: a result waiting to be reported (perf_ab_take clears it ; every ab re-arms), not a give-up latch
    }
}

inline void frame_book() {
    State& P = g_perf;
    // close what an early return or a fault left open, then the root
    if (P.depth > 1) end(P.stack[1]);
    { const long long incl = now() - P.t0[0];
      P.e[0].inclT += incl; P.e[0].selfT += incl - P.childT[0]; P.depth = 0; }
    P.top = &P.sink;
    const int s = P.ring;
    for (int i = 0; i < P.n; ++i) {
        Entry& E = P.e[i];
        const u32 us = to_us(E.selfT), inclUs = to_us(E.inclT);
        const u32 st = E.c.rs + E.c.tss + E.c.fvf;
        if (P.framesInWin == (u32)WIN) {      // the slot about to be overwritten leaves the sums
            E.wSelfUs -= E.rSelfUs[s]; E.wDraws -= E.rDraws[s]; E.wStates -= E.rStates[s]; E.wTex -= E.rTex[s]; E.wCalls -= E.rCalls[s];
        }
        E.rSelfUs[s] = us; E.rDraws[s] = E.c.draws; E.rStates[s] = st; E.rTex[s] = E.c.tex; E.rCalls[s] = E.calls;
        E.wSelfUs += us; E.wDraws += E.c.draws; E.wStates += st; E.wTex += E.c.tex; E.wCalls += E.calls;
        E.sumUs += us; E.sumInclUs += inclUs; E.sumDraws += E.c.draws; E.sumPrims += E.c.prims;
        E.sumRs += E.c.rs; E.sumTss += E.c.tss; E.sumTex += E.c.tex; E.sumFvf += E.c.fvf; E.sumCalls += E.calls;
        if (us > E.maxUs) E.maxUs = us;
        if (E.calls) ++E.framesSeen;
    }
    // the frame interval, into its own window and totals
    if (P.framesInWin == (u32)WIN && P.rIntUs[s]) { P.wIntUs -= P.rIntUs[s]; --P.wIntN; }
    P.rIntUs[s] = P.curIntUs;
    if (P.curIntUs) { P.wIntUs += P.curIntUs; ++P.wIntN; P.sumIntUs += P.curIntUs; ++P.nInt; if (P.curIntUs > P.maxIntUs) P.maxIntUs = P.curIntUs; }
    P.ring = (s + 1) % WIN;
    if (P.framesInWin < (u32)WIN) ++P.framesInWin;
    ++P.frames;
}

// The window, per entry : average and worst self time over the last framesInWin frames.
inline u32 win_max_us(const Entry& E) { u32 m = 0; for (u32 i = 0; i < g_perf.framesInWin; ++i) if (E.rSelfUs[i] > m) m = E.rSelfUs[i]; return m; }
inline u32 win_avg(unsigned long long sum) { return g_perf.framesInWin ? (u32)((sum + g_perf.framesInWin / 2) / g_perf.framesInWin) : 0u; }

// The frame TOTAL (the root's inclusive time) over the window : avg and worst.
inline u32 win_total_avg_us() {
    if (!g_perf.framesInWin) return 0;
    unsigned long long s = 0;
    for (int i = 0; i < g_perf.n; ++i) s += g_perf.e[i].wSelfUs;     // the self times add up to the total
    return (u32)(s / g_perf.framesInWin);
}
inline u32 win_total_max_us() {
    u32 m = 0;
    for (u32 f = 0; f < g_perf.framesInWin; ++f) {
        unsigned long long s = 0;
        for (int i = 0; i < g_perf.n; ++i) s += g_perf.e[i].rSelfUs[f];
        if (s > m) m = (u32)s;
    }
    return m;
}

// The frame interval over the window : avg and worst (us, 0 = none yet) ; AioHUD's share of it, in tenths of a percent.
inline u32 win_int_avg_us() { return g_perf.wIntN ? (u32)(g_perf.wIntUs / g_perf.wIntN) : 0u; }
inline u32 win_int_max_us() { u32 m = 0; for (u32 i = 0; i < g_perf.framesInWin; ++i) if (g_perf.rIntUs[i] > m) m = g_perf.rIntUs[i]; return m; }
inline u32 win_share_x10()  { const u32 iv = win_int_avg_us(); return iv ? (u32)((unsigned long long)win_total_avg_us() * 1000 / iv) : 0u; }

// ---- the commands' side ----
inline void set_on(bool v)  { g_perf.want = v; }
inline bool wanted()        { return g_perf.want; }
inline void request_reset() { g_perf.wantReset = true; }
inline u32 ab_median(int k) {             // sorts in place : call once the run is done
    const u32 n = g_perf.abN[k] < State::AB_MAX ? g_perf.abN[k] : State::AB_MAX;
    if (!n) return 0;
    u32* a = g_perf.abS[k];
    std::nth_element(a, a + n / 2, a + n);
    return a[n / 2];
}
inline void ab_start(u32 frames) { g_perf.abSum[0] = g_perf.abSum[1] = 0; g_perf.abN[0] = g_perf.abN[1] = 0; g_perf.abDone = false; g_perf.abBare = true; g_perf.abLeft = frames & ~1u; }

// PERF_SCOPE("name") : a named scope until the end of the enclosing block. The id is resolved once, and only
// while perf is on (off = one byte test). NOT usable in a function that also has a __try (C2712) : use
// PERF_BEGIN / PERF_END there.
struct Scope {
    int id;
    explicit Scope(int i) : id(i) { begin(id); }
    ~Scope() { end(id); }
    Scope(const Scope&) = delete; Scope& operator=(const Scope&) = delete;
};

}} // namespace aio::perf

#define AIO_PERF_CAT2(a, b) a##b
#define AIO_PERF_CAT(a, b)  AIO_PERF_CAT2(a, b)
// one static id per call site, registered on first use WHILE ON
#define AIO_PERF_ID(name) ([]() -> int { static int s_id = -1; if (s_id < 0) s_id = ::aio::perf::reg(name); return s_id; })
#define PERF_SCOPE(name) \
    ::aio::perf::Scope AIO_PERF_CAT(aioPerfScope_, __LINE__)(::aio::perf::g_perf.on ? AIO_PERF_ID(name)() : -1)
#define PERF_BEGIN(name) do { if (::aio::perf::g_perf.on) ::aio::perf::begin(AIO_PERF_ID(name)()); } while (0)
#define PERF_END(name)   do { if (::aio::perf::g_perf.on) ::aio::perf::end(AIO_PERF_ID(name)()); } while (0)
