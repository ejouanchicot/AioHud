// t_capwatch.cpp -- noticing a fixed table that has run out of room (model/capwatch.h).
//
// The two real cases are the Timers focus list at 24 and the ally-buff table at 32, both silently truncating
// on 2026-09-10/11. The rest are the ordinary states the watcher must never accuse.
#include "check.h"
#include "model/capwatch.h"

using namespace aio;

static CapState fresh() { CapState s; cap_reset(s); return s; }

void test_capwatch() {

    SECTION("a table with room to spare says nothing, ever");
    {   CapState s = fresh();
        int said = 0;
        for (int i = 0; i < 1000; ++i) if (cap_feed(s, 12, 64, 180, 2) != CAPV_OK) ++said;
        CHECK_EQ(0, said);
    }

    SECTION("touching the cap for a moment is a zone-in, not a defect");
    {   // THE CASE THAT KEEPS IT HONEST. Every one of these tables briefly brushes its cap when a zone-in
        // delivers a whole alliance in one frame. Accusing that would fire on the most ordinary event in the
        // game, and a watcher that cries wolf buries the one report that mattered.
        CapState s = fresh();
        int full = 0;
        for (int i = 0; i < 170; ++i) if (cap_feed(s, 64, 64, 180, 2) == CAPV_FULL) ++full;  // nearly three seconds
        CHECK_EQ(0, full);                         // long, but it never held the full window
        for (int i = 0; i < 60; ++i) cap_feed(s, 30, 64, 180, 2);                            // and it drains
        int full2 = 0;
        for (int i = 0; i < 170; ++i) if (cap_feed(s, 64, 64, 180, 2) == CAPV_FULL) ++full2; // brushes it again
        CHECK_EQ(0, full2);                        // 170 + 170 is well past 180 -- but the counter RESET when it
                                                   // dropped, so two long blips are still two blips, not saturation
    }

    SECTION("sitting at the cap is saturation, and it is said once");
    {   CapState s = fresh();
        int full = 0;
        for (int i = 0; i < 600; ++i) if (cap_feed(s, 32, 32, 180, 2) == CAPV_FULL) ++full;
        CHECK_EQ(1, full);                         // not 420 times, which is what a per-frame warning would be
    }

    SECTION("nearly full arrives early, which is the whole point");
    {   // Said the FIRST time it gets close -- days before the overflow, while raising the cap is a one-line
        // change instead of a bug hunt at one in the morning.
        CapState s = fresh();
        CHECK_EQ(CAPV_OK,   cap_feed(s, 61, 64, 180, 2));
        CHECK_EQ(CAPV_NEAR, cap_feed(s, 62, 64, 180, 2));   // 64 - 2
        CHECK_EQ(CAPV_OK,   cap_feed(s, 63, 64, 180, 2));   // and only once
        CHECK_EQ(CAPV_OK,   cap_feed(s, 62, 64, 180, 2));
    }

    SECTION("the peak is remembered after the table drains");
    {   // THE OTHER CASE THAT KEEPS IT HONEST. A table that filled during a zone-in and emptied is invisible
        // to any sample taken afterwards -- and that is precisely when these tables overflow. Without the
        // high-water mark the report would say "12 of 64" about a table that had dropped nineteen entries.
        CapState s = fresh();
        for (int i = 0; i < 10; ++i) cap_feed(s, 64, 64, 180, 2);
        for (int i = 0; i < 10; ++i) cap_feed(s, 3,  64, 180, 2);
        CHECK_EQ(64, s.hi);
        CHECK_EQ(3,  s.used);
    }

    SECTION("a nonsense capacity is a caller bug, not a finding");
    {   CapState s = fresh();
        CHECK_EQ(CAPV_OK, cap_feed(s, 5, 0,  180, 2));
        CHECK_EQ(CAPV_OK, cap_feed(s, 5, -1, 180, 2));
    }
    {   // An empty table whose cap is smaller than the "nearly full" margin must not be accused of being
        // nearly full at zero -- a 2-slot table starts its life inside the margin.
        CapState s = fresh();
        int said = 0;
        for (int i = 0; i < 50; ++i) if (cap_feed(s, 0, 2, 180, 2) != CAPV_OK) ++said;
        CHECK_EQ(0, said);
    }
}
