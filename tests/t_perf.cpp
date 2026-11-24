// t_perf.cpp -- //aio perf counts what the device REALLY received (gfx/perf.h, the hooks in gfx/d3d.h).
//
// A profiler that miscounts is worse than none : the audit's P1-P10 will be ranked on these numbers. So the counts
// are checked against the only independent witness there is offline -- the fake device's own call log -- per scope,
// with nesting, and the "off" state is checked to touch nothing at all.
#include "check.h"
#include "fake_device.h"
#include "gfx/draw.h"
#include "gfx/d3d.h"
#include "perf.h"

using namespace aio;
using fakedev::device;
using fakedev::reset;

namespace {

struct Seen { int draws, prims, rs, tss, tex, fvf; };

// What the fake device logged between two call indices.
Seen seen(int from, int to) {
    Seen s = {};
    const fakedev::Log& L = fakedev::log();
    for (int i = from; i < to && i < L.nCall; ++i) {
        const fakedev::Call& c = L.call[i];
        switch (c.kind) {
            case fakedev::Call::DrawUP: ++s.draws; s.prims += (int)c.b; break;
            case fakedev::Call::SetRS:  ++s.rs;  break;
            case fakedev::Call::SetTSS: ++s.tss; break;
            case fakedev::Call::SetTex: ++s.tex; break;
            case fakedev::Call::SetFVF: ++s.fvf; break;
        }
    }
    return s;
}

// A frame's counters for one entry (this frame's, before frame_end folds them away).
void check_entry(const perf::Entry& E, const Seen& s) {
    CHECK_EQ(E.c.draws, s.draws); CHECK_EQ(E.c.prims, s.prims);
    CHECK_EQ(E.c.rs, s.rs); CHECK_EQ(E.c.tss, s.tss); CHECK_EQ(E.c.tex, s.tex); CHECK_EQ(E.c.fvf, s.fvf);
}

// Representative widget work, from primitives that open NO scope of their own (rrect / disc / text do : gfx.*,
// checked in their own section below) : the 2D state block, gradients, a glow, a triangle.
void workload_a(u32 dev) {
    dColorQuadState(dev);
    grad_quad(dev, 10.0f, 10.0f, 200.0f, 60.0f, 0xE0101418u, 0xE0101418u, 0xE0080A0Cu, 0xE0080A0Cu);
    grad_quad(dev, 20.0f, 20.0f, 50.0f, 8.0f, 0xFF203040u, 0xFF304050u, 0xFF101820u, 0xFF202830u);
    fill_tri(dev, 0.0f, 0.0f, 10.0f, 0.0f, 5.0f, 8.0f, 0xFFFFFFFFu);
}
void workload_b(u32 dev) {
    dTexQuadState(dev, 0x1234u, true, false);
    glow_quad(dev, 90.0f, 90.0f, 20.0f, 20.0f, 0xFFFFFFFFu);
    fill_tri(dev, 100.0f, 100.0f, 104.0f, 100.0f, 102.0f, 104.0f, 0xFF00FF00u);
    dSetTex(dev, 0, 0);
}

} // namespace

void test_perf() {
    const u32 dev = device();
    perf::State& P = perf::g_perf;

    SECTION("perf : OFF counts nothing, registers nothing, opens nothing");
    {
        reset();
        perf::set_on(false);
        const int n0 = P.n;
        perf::frame_begin();
        CHECK(!P.on);
        { PERF_SCOPE("t.off"); workload_a(dev); workload_b(dev); }
        perf::frame_end();
        CHECK(fakedev::draws() > 0);           // the work really happened...
        CHECK_EQ(P.n, n0);                     // ...but no name was registered
        CHECK_EQ(P.frames, 0);                 // and no frame was folded in
        CHECK_EQ(perf::find("t.off"), -1);
        CHECK_EQ(P.sink.draws, 0);             // the hooks did not even reach the sink
    }

    SECTION("perf : ON, each scope's draws / prims / rs / tss / tex / fvf == what the device received in it");
    {
        perf::set_on(true); perf::request_reset();
        perf::frame_begin();                   // applies the request : on from this frame
        CHECK(P.on);
        reset();
        const int ia = perf::reg("t.a"), ib = perf::reg("t.b");
        const int c0 = fakedev::log().nCall;
        dSetRS(dev, D3DRS_ZENABLE, 0);         // outside any named scope : the root's "(other)"
        const int c1 = fakedev::log().nCall;
        perf::begin(ia);
        workload_a(dev);
        const int c2 = fakedev::log().nCall;
        perf::begin(ib);                       // NESTED : b's calls are b's, not a's
        workload_b(dev);
        const int c3 = fakedev::log().nCall;
        perf::end(ib);
        workload_a(dev);                       // back in a
        const int c4 = fakedev::log().nCall;
        perf::end(ia);

        Seen sa = seen(c1, c2), sa2 = seen(c3, c4);
        sa.draws += sa2.draws; sa.prims += sa2.prims; sa.rs += sa2.rs; sa.tss += sa2.tss; sa.tex += sa2.tex; sa.fvf += sa2.fvf;
        CHECK(sa.draws > 0 && sa.rs > 0 && sa.tss > 0);   // the workload really exercises every counter it should
        check_entry(P.e[ia], sa);
        const Seen sb = seen(c2, c3);
        CHECK(sb.draws >= 2 && sb.tex >= 2 && sb.fvf >= 1);
        check_entry(P.e[ib], sb);
        check_entry(P.e[0], seen(c0, c1));
        // and the whole frame, every row together, is every call the device saw
        const Seen all = seen(c0, c4);
        u32 d = 0, r = 0;
        for (int i = 0; i < P.n; ++i) { d += P.e[i].c.draws; r += P.e[i].c.rs + P.e[i].c.tss + P.e[i].c.tex + P.e[i].c.fvf; }
        CHECK_EQ(d, all.draws);
        CHECK_EQ(r, all.rs + all.tss + all.tex + all.fvf);
        CHECK_EQ(P.e[ia].calls, 1); CHECK_EQ(P.e[ib].calls, 1);
        perf::frame_end();

        // folded into the window and the totals
        CHECK_EQ(P.frames, 1); CHECK_EQ(P.framesInWin, 1);
        CHECK_EQ((int)P.e[ia].wDraws, sa.draws);
        CHECK_EQ((int)P.e[ia].wStates, sa.rs + sa.tss + sa.fvf);
        CHECK_EQ((int)P.e[ib].sumTex, sb.tex);
        // self times add up to the root's inclusive time (the rows sum to the frame)
        CHECK(P.e[0].inclT >= P.e[ia].inclT);
        CHECK(P.e[ia].inclT >= P.e[ib].inclT);
        CHECK_EQ(P.e[0].selfT + P.e[ia].selfT + P.e[ib].selfT, P.e[0].inclT);
    }

    SECTION("perf : rrect / disc charge their draws to gfx.rrect / gfx.disc, nested in the caller ; nothing is lost");
    {
        reset();
        perf::frame_begin();
        const int ia = perf::reg("t.a");
        const int c0 = fakedev::log().nCall;
        perf::begin(ia);
        dColorQuadState(dev);                  // the caller's own
        const int c1 = fakedev::log().nCall;
        rrect(dev, 10.0f, 10.0f, 200.0f, 60.0f, 6.0f, 0xE0101418u, 0xE0080A0Cu);
        const int c2 = fakedev::log().nCall;
        disc(dev, 100.0f, 100.0f, 4.0f, 0xFFFFFFFFu);
        const int c3 = fakedev::log().nCall;
        perf::end(ia);
        const int ir = perf::find("gfx.rrect"), id = perf::find("gfx.disc");
        CHECK(ir >= 2); CHECK(id >= 2);
        check_entry(P.e[ia], seen(c0, c1));
        if (ir >= 0) check_entry(P.e[ir], seen(c1, c2));
        if (id >= 0) check_entry(P.e[id], seen(c2, c3));
        CHECK(seen(c1, c2).draws > 0 && seen(c2, c3).draws > 0);
        if (ir >= 0) CHECK(P.e[ia].inclT >= P.e[ir].inclT + (id >= 0 ? P.e[id].inclT : 0));   // nested : inside the caller's time
        perf::frame_end();
    }

    SECTION("perf : PERF_SCOPE registers once per call site and counts like begin/end");
    {
        reset();
        perf::frame_begin();
        const int c0 = fakedev::log().nCall;
        for (int k = 0; k < 3; ++k) { PERF_SCOPE("t.macro"); workload_b(dev); }
        const int c1 = fakedev::log().nCall;
        const int id = perf::find("t.macro");
        CHECK(id >= 2);
        CHECK_EQ(P.e[id].calls, 3);
        check_entry(P.e[id], seen(c0, c1));
        perf::frame_end();
        CHECK_EQ(P.e[id].framesSeen, 1);
    }

    SECTION("perf : a scope left open (early return / SEH) is closed by its parent and by frame_end");
    {
        reset();
        perf::frame_begin();
        const int ia = perf::reg("t.a"), ib = perf::reg("t.b");
        const int d0 = perf::depth();
        perf::begin(ia); perf::begin(ib);
        CHECK_EQ(perf::depth(), d0 + 2);
        perf::end(ia);                         // b was never closed : closing a closes b too
        CHECK_EQ(perf::depth(), d0);
        perf::end(ib);                         // not open any more : ignored, the root stays
        CHECK_EQ(perf::depth(), d0);
        perf::begin(ia); perf::begin(ib);
        perf::unwind_to(d0);                   // the Hud::render fault path
        CHECK_EQ(perf::depth(), d0);
        perf::begin(ia);                       // and one never closed at all
        perf::frame_end();
        CHECK_EQ(perf::depth(), 0);
        CHECK(P.top == &P.sink);               // between frames the hooks write nowhere that is read
    }

    SECTION("perf : the 60-frame window slides (old frames leave the sums)");
    {
        perf::request_reset();
        const int ia = perf::reg("t.a");
        for (int fr = 0; fr < perf::WIN + 10; ++fr) {
            reset();
            perf::frame_begin();
            perf::begin(ia);
            const int draws = fr < 10 ? 5 : 1; // ten heavy frames first, then light ones
            for (int k = 0; k < draws; ++k) grad_quad(dev, 0.0f, 0.0f, 4.0f, 4.0f, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu);
            perf::end(ia);
            perf::frame_end();
        }
        CHECK_EQ(P.framesInWin, perf::WIN);
        CHECK_EQ(P.frames, perf::WIN + 10);
        CHECK_EQ((int)P.e[ia].wDraws, perf::WIN);                    // the heavy frames slid out
        CHECK_EQ((int)P.e[ia].sumDraws, 10 * 5 + perf::WIN * 1);    // the since-reset total kept them
        CHECK_EQ(perf::win_avg(P.e[ia].wDraws), 1);
    }

    SECTION("perf : //aio perf ab alternates bare and instrumented frames, and reports once");
    {
        perf::set_on(false);
        // typed MID-frame, as in the game (drain_commands runs inside Hud::render) : that frame must not count
        perf::frame_begin();
        perf::ab_start(64);
        perf::frame_end();
        CHECK_EQ(P.abN[0] + P.abN[1], 0);
        CHECK_EQ(P.abLeft, 64);
        int on = 0, off = 0;
        for (int fr = 0; fr < 64; ++fr) {
            perf::frame_begin();
            if (P.on) ++on; else ++off;
            perf::frame_end();
        }
        CHECK_EQ(on, 32); CHECK_EQ(off, 32);
        CHECK(P.abDone);
        CHECK_EQ(P.abN[0], 32); CHECK_EQ(P.abN[1], 32);
        CHECK(P.abSum[1] / 32 < 1000000);      // no sample of "the uptime" : a bare frame here is microseconds
        perf::frame_begin();
        CHECK(!P.on);                          // back to what //aio perf asked for : off
        perf::frame_end();
    }

    SECTION("perf : the ab result is a MEDIAN -- one spike on one side does not decide it");
    {
        // the first in-game run : one 15 ms poll spike made the mean say the instrumentation saved 126 us a frame
        P.abN[0] = P.abN[1] = 9;
        for (int i = 0; i < 9; ++i) { P.abS[0][i] = 1300 + (u32)i; P.abS[1][i] = 1290 + (u32)i; }
        P.abS[1][4] = 15000;                   // the spike, in the middle of the bare run
        CHECK_EQ(perf::ab_median(0), 1304);
        CHECK_EQ(perf::ab_median(1), 1295);    // 1290..1298 without 1294, plus 15000 : the middle one is 1295
        P.abN[0] = P.abN[1] = 0;
    }

    SECTION("perf : a full table charges the 65th name to the overflow row");
    {
        char nm[24];
        for (int k = 0; k < perf::CAP + 4; ++k) { _snprintf(nm, sizeof(nm), "t.fill%d", k); nm[23] = 0; perf::reg(nm); }
        CHECK_EQ(P.n, perf::CAP);
        CHECK_EQ(perf::reg("t.one.too.many"), 1);
        CHECK_STR(P.e[1].name, "(overflow)");
    }
    perf::set_on(false);
}
