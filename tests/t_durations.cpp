// t_durations.cpp -- the buff-duration model (Composure set, Perpetuance, Regen-specific gear).
//
// This is the part of the codebase that has produced the most shipped bugs -- 3a35df8 (Regen on an ally read
// far too short), 2c55a1c / ba67143 (song timers). It is also entirely pure : header-only tables and integer
// arithmetic, no game, no clock. There was no reason for it to be untested except that nothing could run it.
//
// The Composure case below is not invented : enh_dur.h carries it as a worked example in a comment, verified
// in game. It had never been executed once.
#include "check.h"
#include "model/enh_dur.h"
#include "model/regen_dur.h"

using namespace aio;

// Build a 16-slot equipment array from an initialiser list of item ids.
static void gear(unsigned short out[16], const unsigned short* ids, int n) {
    for (int i = 0; i < 16; ++i) out[i] = 0;
    for (int i = 0; i < n && i < 16; ++i) out[i] = ids[i];
}

void test_durations() {
    SECTION("durations : Composure set bonus is a step function of PIECE COUNT");
    {
        // Estoqueur's +2 : head / body / hands / legs (the ids the header lists as set pieces).
        const unsigned short ESTQ[4] = { 0x2b3c, 0x2b50, 0x2b64, 0x2b78 };
        unsigned short g[16];
        gear(g, ESTQ, 0); CHECK_EQ(composure_set_pct(g),  0);
        gear(g, ESTQ, 1); CHECK_EQ(composure_set_pct(g),  0);   // one piece is not a set
        gear(g, ESTQ, 2); CHECK_EQ(composure_set_pct(g), 10);
        gear(g, ESTQ, 3); CHECK_EQ(composure_set_pct(g), 20);
        gear(g, ESTQ, 4); CHECK_EQ(composure_set_pct(g), 35);   // the value the worked example uses
    }

    SECTION("durations : a set piece is recognised, an unrelated item is not");
    {
        CHECK(is_composure_set_piece(0x2b3c));
        CHECK(!is_composure_set_piece(0));
        CHECK(!is_composure_set_piece(1));        // an id that is in no table at all
    }

    SECTION("durations : the worked example from enh_dur.h finally runs");
    {
        // (180 + 20) * 1.35 * 1.88 * 1.49 = 756.3 s -- Composure 4pc (+35%) is the 1.35 factor.
        unsigned short g[16];
        const unsigned short ESTQ[4] = { 0x2b3c, 0x2b50, 0x2b64, 0x2b78 };
        gear(g, ESTQ, 4);
        const double composure = 1.0 + composure_set_pct(g) / 100.0;
        const double total = (180.0 + 20.0) * composure * 1.88 * 1.49;
        CHECK(total > 756.0 && total < 757.0);
    }

    SECTION("durations : Regen-specific gear is Regen-specific");
    {
        // Item 21175 grants +12 s of Regen duration and NOTHING to any other buff -- the whole point of
        // 3a35df8 was that Regen is the only spell with its own duration gear.
        unsigned short g[16];
        const unsigned short NONE[1] = { 0 };
        gear(g, NONE, 0);            CHECK_EQ(regen_dur_gear_sec(g), 0);
        const unsigned short ONE[1] = { 21175 };
        gear(g, ONE, 1);             CHECK_EQ(regen_dur_gear_sec(g), 12);
        const unsigned short OTHER[1] = { 1 };                 // an item in no Regen table
        gear(g, OTHER, 1);           CHECK_EQ(regen_dur_gear_sec(g), 0);
        const unsigned short TWO[2] = { 21175, 23062 };        // 12 + 24 : each item counted once
        gear(g, TWO, 2);             CHECK_EQ(regen_dur_gear_sec(g), 36);
    }

    SECTION("durations : Perpetuance is x2 bare, and the bracers raise the factor");
    {
        // NOTE : this returns the multiplier Perpetuance ITSELF applies (x2), not a gear bonus on top -- the
        // caller only uses it while the buff is up. Writing this test is what pinned the contract down.
        unsigned short g[16];
        const unsigned short NONE[1] = { 0 };
        gear(g, NONE, 0);
        CHECK(perpetuance_mult(g) == 2.0);                 // no bracer -> x2.0, never 0 and never 1.0
        const unsigned short SAV1[1] = { 0x2bd7 };         // Savant's Bracers +1 -> x2.25
        gear(g, SAV1, 1);
        CHECK(perpetuance_mult(g) == 2.25);
        const unsigned short SAV2[1] = { 0x2b73 };         // Savant's Bracers +2 -> x2.50
        gear(g, SAV2, 1);
        CHECK(perpetuance_mult(g) == 2.50);
    }

    SECTION("durations : empty gear never reports a bonus");
    {
        unsigned short g[16];
        const unsigned short NONE[1] = { 0 };
        gear(g, NONE, 0);
        CHECK_EQ(composure_set_pct(g), 0);
        CHECK_EQ(enh_dur_listed_pct(g), 0);
        CHECK_EQ(regen_dur_gear_sec(g), 0);
    }
}
