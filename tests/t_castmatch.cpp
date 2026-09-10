// t_castmatch.cpp -- pairing a buff timer with the cast that produced it (model/cast_match.h).
//
// A wrong answer here puts somebody else's name on your buff, and decides what "Mine only" hides. The three
// cases with a symptom in their comment are shipped defects.
#include "check.h"
#include "model/cast_match.h"

using namespace aio;

void test_cast_match() {
    const char* why = 0;

    SECTION("several live timers : paired by rank");
    {   // Two Marches run two timers. Their predictions are too loose to compare one by one -- a Troubadour'd
        // song can run double -- so the ordering is the answer, not the arithmetic.
        const CastCand c[] = { { 9000, false }, { 6000, false } };
        CHECK_EQ(0, cast_match(c, 2, /*expiry*/8800, /*liveTimers*/2, /*rank*/0, &why));
        CHECK_EQ(1, cast_match(c, 2, /*expiry*/6200, /*liveTimers*/2, /*rank*/1, &why));
    }

    SECTION("one live timer, several casts : paired by closeness");
    {   // Shipped defect: "a trust wrote over my Protect but the row still shows my name", and the reverse.
        // Rank returns the LONGEST prediction, which here is the cast that was overwritten.
        const CastCand c[] = { { 9000, false }, { 5100, true } };   // yours predicted long, the trust's shorter
        CHECK_EQ(1, cast_match(c, 2, /*expiry*/5000, /*liveTimers*/1, /*rank*/0, &why));
        CHECK(strstr(why, "closeness") != 0);
    }
    {   // ...and the same shape the other way round : the trust's cast was the one overwritten.
        const CastCand c[] = { { 9000, false }, { 5100, true } };
        CHECK_EQ(0, cast_match(c, 2, /*expiry*/8950, 1, 0, &why));
    }
    {   // Closeness applies only when there is ONE timer. With two, rank keeps its authority even if a
        // prediction happens to sit nearer.
        const CastCand c[] = { { 9000, false }, { 5100, true } };
        CHECK_EQ(1, cast_match(c, 2, /*expiry*/8950, /*liveTimers*/2, /*rank*/1, &why));
        CHECK(strstr(why, "rank") != 0);
    }

    SECTION("when unsure, never blame a trust");
    {   // A trust has no Troubadour and no Marcato: a timer running far past its cast's prediction was not made
        // by it. Answering "unknown" leaves the row visible; naming a trust HIDES it behind the source filter,
        // so the asymmetry is deliberate.
        const CastCand c[] = { { 1000, true } };
        CHECK_EQ(-1, cast_match(c, 1, /*expiry*/1000 + CAST_TRUST_OVERSHOOT + 1, 1, 0, &why));
        CHECK(strstr(why, "unknown") != 0);
        CHECK_EQ(0,  cast_match(c, 1, /*expiry*/1000 + CAST_TRUST_OVERSHOOT - 1, 1, 0, &why));
    }
    {   // The same overshoot from YOUR cast is normal -- Troubadour doubles a song, and guarding against it
        // cost more than it protected: measured 2026-07-20, every Troubadour'd song was rejected and the row
        // lost its spell name, its tier and its tags.
        const CastCand c[] = { { 1000, false } };
        CHECK_EQ(0, cast_match(c, 1, /*expiry*/1000 + CAST_TRUST_OVERSHOOT * 5, 1, 0, &why));
    }

    SECTION("no candidate is an honest answer");
    {   const CastCand c[] = { { 1000, false } };
        CHECK_EQ(-1, cast_match(c, 1, 1000, 1, /*rank*/1, &why));   // asked for a rank nobody fills
        CHECK_EQ(-1, cast_match(c, 0, 1000, 1, 0, &why));           // nothing in the ring at all
    }
}
