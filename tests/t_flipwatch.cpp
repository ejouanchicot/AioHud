// t_flipwatch.cpp -- noticing a decision that cannot settle (model/flipwatch.h).
//
// Every case is one of the four oscillations that cost 2026-09-10/11, or one of the ordinary behaviours the
// watcher must never accuse.
#include "check.h"
#include "model/flipwatch.h"

using namespace aio;

static FlipState fresh() { FlipState s; flip_reset(s); return s; }

void test_flipwatch() {

    SECTION("A,B,A,B never settling is an argument, and it says so once");
    {   // The shape of all four: a red row drawn on one frame and gone the next, two song rows trading
        // places, two RVA healers re-proposing to each other. Six alternations with never a pause.
        FlipState s = fresh();
        CHECK(!flip_feed(s, 9, 6, 120));          // the first sample only arms it
        int trips = 0;
        for (int i = 0; i < 20; ++i) if (flip_feed(s, (i & 1) ? 7u : 9u, 6, 120)) ++trips;
        CHECK_EQ(1, trips);                        // reported EXACTLY once, not on every frame after
    }

    SECTION("ordinary change is not an argument");
    {   // A countdown, a growing list, a cursor walking a menu: each value is new, none comes back.
        FlipState s = fresh();
        bool tripped = false;
        for (unsigned v = 100; v > 40; --v) if (flip_feed(s, v, 6, 120)) tripped = true;
        CHECK(!tripped);
    }
    {   // Holding still is not an argument either, however long.
        FlipState s = fresh();
        bool tripped = false;
        for (int i = 0; i < 500; ++i) if (flip_feed(s, 42, 6, 120)) tripped = true;
        CHECK(!tripped);
    }
    {   // Three values cycling is still motion, not a pair fighting -- and the pair test must not mistake it.
        FlipState s = fresh();
        bool tripped = false;
        const unsigned cyc[3] = { 1, 2, 3 };
        for (int i = 0; i < 60; ++i) if (flip_feed(s, cyc[i % 3], 6, 120)) tripped = true;
        CHECK(!tripped);
    }

    SECTION("settling forgets the argument");
    {   // THE CASE THAT KEEPS IT HONEST. A row set that alternates twice, then holds for two seconds, then
        // alternates twice again, has not oscillated -- it changed, twice. Without this the accusation would
        // accumulate over a session and eventually fire on anything that ever flipped.
        FlipState s = fresh();
        bool tripped = false;
        for (int round = 0; round < 4; ++round) {
            for (int i = 0; i < 4; ++i) if (flip_feed(s, (i & 1) ? 2u : 1u, 6, 120)) tripped = true;
            for (int i = 0; i < 130; ++i) if (flip_feed(s, 1u, 6, 120)) tripped = true;   // settles
        }
        CHECK(!tripped);
    }
    {   // ...but a pause SHORTER than the settle window does not wipe the slate -- an argument with a breath
        // in it is still an argument, which is what a 60 Hz flicker interrupted by one slow frame looks like.
        FlipState s = fresh();
        bool tripped = false;
        for (int round = 0; round < 6; ++round) {
            for (int i = 0; i < 2; ++i) if (flip_feed(s, (i & 1) ? 2u : 1u, 6, 120)) tripped = true;
            for (int i = 0; i < 10; ++i) if (flip_feed(s, 1u, 6, 120)) tripped = true;    // too short to settle
        }
        CHECK(tripped);
    }

    SECTION("a new pair restarts the count, it does not inherit one");
    {   // Two frames of A/B, then a different argument entirely between C and D. The second must earn its
        // own six -- otherwise one unrelated flip earlier in the session pre-charges the next accusation.
        FlipState s = fresh();
        CHECK(!flip_feed(s, 1, 6, 120));
        CHECK(!flip_feed(s, 2, 6, 120));
        CHECK(!flip_feed(s, 1, 6, 120));
        int trips = 0;
        const unsigned cd[2] = { 8, 9 };
        for (int i = 0; i < 5; ++i) if (flip_feed(s, cd[i % 2], 6, 120)) ++trips;
        CHECK_EQ(0, trips);                        // five alternations of the NEW pair is not yet six
        for (int i = 0; i < 4; ++i) if (flip_feed(s, cd[(i + 1) % 2], 6, 120)) ++trips;
        CHECK_EQ(1, trips);
    }
    {   // The same rule, made sharp enough to feel it. Argue A/B almost to the threshold, then start an
        // entirely different argument: the new pair must earn its own six. Carrying the count over would let
        // one flip early in a session pre-charge an accusation against something unrelated later -- which is
        // how a watcher starts crying wolf, and a noisy watcher buries the one report that mattered.
        FlipState s = fresh();
        bool tripped = false;
        const unsigned ab[6] = { 1, 2, 1, 2, 1, 2 };          // arms, then four alternations : flips = 5
        for (int i = 0; i < 6; ++i) if (flip_feed(s, ab[i], 6, 120)) tripped = true;
        CHECK(!tripped);                                      // one short of the threshold
        const unsigned cd[4] = { 8, 9, 8, 9 };                // a DIFFERENT argument, two alternations in
        for (int i = 0; i < 4; ++i) if (flip_feed(s, cd[i], 6, 120)) tripped = true;
        CHECK(!tripped);                                      // starts at one : 1 + 2 = 3, nowhere near six
        // Inheriting would make it 5 + 2 = 7 here and fire on an argument two frames old.
    }
}
