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
#include "model/song_dur.h"
#include "model/geo_dur.h"

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
        CHECK_EQ(song_dur_m1_pct(g, 4), 0);
        const unsigned short ONE[1] = { 21175 };
        gear(g, ONE, 1);             CHECK_EQ(regen_dur_gear_sec(g), 12);
        const unsigned short OTHER[1] = { 1 };                 // an item in no Regen table
        gear(g, OTHER, 1);           CHECK_EQ(regen_dur_gear_sec(g), 0);
        CHECK_EQ(song_dur_m1_pct(g, 4), 0);
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

    SECTION("durations : BRD song m1 reproduces five SERVER measurements");
    {
        // Captured with //aio songlog on 2026-07-25 : the equipped ids at cast time and, next to them, the
        // duration the server's own 0x063 reported. These five numbers are the reason the model was rewritten
        // (it was running 22-37% short), so they are exactly what must never silently drift again -- the gear
        // table is GENERATED, and a bad regeneration would otherwise be invisible until somebody sang.
        //
        // m1 = 1 + flat gear + family gear + 0.05 JP gift. Song POTENCY counts as duration, +10% per point :
        // Gjallarhorn "All songs +4" is +40% on everything, Fili Calot +3 '"Madrigal"+1' is +10% on Madrigal.
        const unsigned short SONG[16] = {   // Carnwenhan / Kali / Gjallarhorn / - / Fili +3 set / Inyanga +2 ...
            19828, 20599, 18572, 0, 23429, 23496, 23563, 25882, 24084, 26033, 10826, 15961, 25475, 26184, 26184, 26255 };
        const unsigned short MARCH[16] = {  // the same set with Marsyas (+50%) in the instrument slot
            19828, 20599, 21398, 0, 23429, 23496, 23563, 25882, 24084, 26033, 10826, 15961, 25475, 26184, 26184, 26255 };
        const unsigned short DUMMY[16] = {  // the "dummy song" set : Daurdabla, almost no duration gear
            21589, 17440, 18571, 0, 23761, 23861, 23563, 23782, 23789, 26042, 26367, 28475, 27549, 26190, 26190, 26245 };
        const int JP = 5;   // BRD job-point gift, a flat +5% on m1

        // family ids : 4 Minuet, 5 Madrigal, 6 Prelude, 8 March, 15 Operetta(Capriccio), 0 = none (Gavotte)
        CHECK_EQ(song_dur_m1_pct(SONG,  4) + JP, 186);   // Valor Minuet IV/V -> 120 * 2.86 = 343 s (real 342)
        CHECK_EQ(song_dur_m1_pct(SONG,  5) + JP, 196);   // Blade Madrigal    -> 120 * 2.96 = 355 s (real 352)
        CHECK_EQ(song_dur_m1_pct(MARCH, 8) + JP, 196);   // Honor March       -> 120 * 2.96 = 355 s (real 352)
        CHECK_EQ(song_dur_m1_pct(DUMMY, 0) + JP,  35);   // Goblin Gavotte    -> 120 * 1.35 = 162 s (real 159)
        CHECK_EQ(song_dur_m1_pct(DUMMY, 15) + JP, 35);   // Gold Capriccio    -> 120 * 1.35 = 162 s (real 158)

        // The family term is the whole point : same sixteen ids, two songs, twelve seconds apart in game.
        CHECK(song_dur_m1_pct(SONG, 5) > song_dur_m1_pct(SONG, 4));
        // A song with no family collects flat gear only -- never a family extra it is not entitled to.
        CHECK_EQ(song_dur_m1_pct(SONG, 0) + JP, 176);
    }

    SECTION("durations : empty gear never reports a bonus");
    {
        unsigned short g[16];
        const unsigned short NONE[1] = { 0 };
        gear(g, NONE, 0);
        CHECK_EQ(composure_set_pct(g), 0);
        CHECK_EQ(enh_dur_listed_pct(g), 0);
        CHECK_EQ(regen_dur_gear_sec(g), 0);
        CHECK_EQ(song_dur_m1_pct(g, 4), 0);
    }

    SECTION("durations : Composure on YOURSELF triples the duration, up to 30 minutes, and never shortens it");
    {   // MEASURED 2026-09-13 in game, Kaories RDM99 casting on herself, the server's own 0x063 timer against the
        // duration without Composure (dev/fixtures/measures/durations.csv). Before this rule the model predicted
        // the no-Composure duration (plus the set bonus, which the server does not apply on yourself).
        CHECK_EQ((int)composure_self_sec(272.0, true), 816);     // Refresh III : 272 s -> server 817
        CHECK_EQ((int)composure_self_sec(147.0, true), 441);     // Regen II    : 147 s -> server 442
        CHECK_EQ((int)composure_self_sec(637.0, true), 1800);    // Haste II    : 637 s -> server 1800 (the cap)
        CHECK_EQ((int)composure_self_sec(1593.0, true), 1800);   // Barfira     : 1593 s -> server 1801
        CHECK_EQ((int)composure_self_sec(794.0, true), 1800);    // Aquaveil in idle gear : 794 s -> server 1800
        // A duration ALREADY past 30 minutes is left alone, not cut to the cap : Aquaveil in its duration set
        // (1975 s) and Protect IV (5797 s) read the same with and without Composure.
        CHECK_EQ((int)composure_self_sec(1975.0, true), 1975);
        CHECK_EQ((int)composure_self_sec(5795.0, true), 5795);
        CHECK_EQ((int)composure_self_sec(637.0, false), 637);    // no Composure : nothing changes
    }

    SECTION("durations : an Indi- lasts (base + job points + gear seconds) x (1 + Indi. eff. dur. %)");
    {   // MEASURED 2026-09-13 in game (Kaories GEO99) : an Indi- entrusted to Tetsouo lasted 355-356 s where the model said
        // 271 s. The Entrust set carries Gada "Indi. eff. dur. +11" and Lifestream Cape "Indi. eff. dur. +20" -- augment
        // 1250 (0x4E2), a PERCENT in the game's own resources -- which the model did not read : (180 + 40 JP + 51 flat) x
        // 1.31 = 355.0. Her self aura, cast in a set without those two pieces, was already exact (240 / 240).
        // The extdata bytes below are the REAL ones, read from her items by Windower that evening.
        static const unsigned char GADA[24]      = { 0x02,0x03,0xE2,0x54,0x05,0x0A,0x23,0x80,0x85,0x68,0x2D,0,0,0,0,0,0,0,0,0,0,0,0,0 };
        static const unsigned char LIFESTREAM[24] = { 0x02,0x03,0x2C,0x49,0xE2,0x9C,0x70,0x10,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 };
        unsigned short ids[16] = { 0 }; unsigned char ext[16][24] = { { 0 } };
        ids[0] = 21072; memcpy(ext[0], GADA, 24);          // main : Gada
        ids[15] = 28637; memcpy(ext[15], LIFESTREAM, 24);  // back : Lifestream Cape
        ids[7] = 23619; ids[8] = 23708;                    // Bagua Pants +3 (21 s), Azimuth Gaiters +3 (30 s)
        CHECK_EQ(geo_dur_augment_pct(ids, ext), 31);
        CHECK_EQ(geo_dur_gear_sec(ids), 51);
        CHECK_EQ((int)geo_dur_sec(180, 40, 51, 31), 355);  // server : 355 and 356
        CHECK_EQ((int)geo_dur_sec(180, 40, 20, 0), 240);   // her self aura set : server 240
        CHECK_EQ(enh_dur_augment_pct(ids, ext), 0);        // and the Enhancing decoder still ignores it (0x4E0 only)
    }
}
