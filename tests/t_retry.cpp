// t_retry.cpp -- the "try this again later" clock (src/retry_clock.h).
//
// WHY THIS EXISTS. This four-line idiom is the most COPIED decision in the project, and its header opens with
// the list of what copying it cost: the same mistake at five sites, and it killed the buff atlas, then the
// gear-icon back-off, then the map tables, then the profile sync. The mistake is always the same one --
//
//     if ((int)(now - nextMs) < 0) return;      // WRONG once uptime passes 24.8 days
//
// -- because `nextMs == 0` means "never scheduled, try NOW", and `(int)now` goes negative past 0x7FFFFFFF ms
// of uptime, so the guard returns early FOREVER and the retry never fires again. A machine left running for
// four weeks is not exotic; it is a Windows box that nobody rebooted.
//
// The whole point of the file under test is that nobody should ever write that compare again. The whole point
// of THIS file is that the one surviving implementation is pinned, including at the uptime where every copy
// of it failed. It can be tested at all because of the injected-clock pair (`retry_due_at` / `retry_arm_at`)
// added on 2026-09-12: with the wall clock hidden inside, the 24.8-day case could only be reasoned about.
#include "check.h"
#include "retry_clock.h"

using namespace aio;

void test_retry() {
    SECTION("retry : the 0 sentinel means TRY NOW, at every uptime");
    // This is the case that broke five times. 0 must fire whatever the clock says -- including at the tick
    // where `(int)nowMs` is negative, which is exactly where the hand-rolled compares gave up for good.
    CHECK(retry_due_at(0, 0u));
    CHECK(retry_due_at(0, 1000u));
    CHECK(retry_due_at(0, 0x7FFFFFFFu));          // 24.8 days, the last "positive" tick
    CHECK(retry_due_at(0, 0x80000000u));          // one millisecond later : the exact failure point
    CHECK(retry_due_at(0, 0xFFFFFFFFu));          // 49.7 days, the tick before the counter wraps

    SECTION("retry : before the deadline it does not fire, after it does");
    CHECK(!retry_due_at(2000u, 1999u));
    CHECK(retry_due_at(2000u, 2000u));            // AT the deadline is due -- a >= comparison, not >
    CHECK(retry_due_at(2000u, 2001u));

    SECTION("retry : arming never lands on the sentinel");
    // A deadline that legitimately falls on tick 0 would otherwise read as "never scheduled" and fire
    // immediately -- the one-in-2^32 nuisance the `| 1` removes. Also checks arming stays within a
    // millisecond of what was asked for, since that is the other half of the contract.
    {
        unsigned next = 0;
        retry_arm_at(next, 0xFFFFFFFFu, 1u);      // 0xFFFFFFFF + 1 wraps to exactly 0
        CHECK(next != 0);
        CHECK(!retry_due_at(next, 0xFFFFFFFFu));  // and it is still in the future from where we armed it
    }
    {
        unsigned next = 0;
        retry_arm_at(next, 5000u, 300u);
        CHECK(next == 5300u || next == 5301u);    // `| 1` may add one millisecond, never more
        CHECK(!retry_due_at(next, 5299u));
        CHECK(retry_due_at(next, 5302u));
    }

    SECTION("retry : the counter WRAPPING does not freeze a pending retry");
    // 49.7 days of uptime and the tick counter goes back to 0. A deadline armed just before the wrap must
    // still fire just after it -- the unsigned subtraction is what makes that work, and it is the reason the
    // comparison is written on the difference and never on the two values.
    {
        unsigned next = 0;
        retry_arm_at(next, 0xFFFFFF00u, 0x200u);  // deadline is 0x100 ms PAST the wrap
        CHECK(!retry_due_at(next, 0xFFFFFF80u));  // before the wrap : not yet
        CHECK(!retry_due_at(next, 0x00000050u));  // after the wrap, still early : not yet
        CHECK(retry_due_at(next, 0x00000180u));   // past the deadline : fires
    }

    SECTION("retry : a plateau is a schedule, not a stop (the shape rule 10 asks for)");
    // Not a property of the header -- a property of how every caller uses it. Three immediate attempts, then
    // spaced ones, and the spacing is what turns a 60 Hz hammer into something that still recovers by itself.
    // Written as a case because the minimap's marker budget got this wrong in the other direction (it stopped
    // after 2.4 s) and nothing in the suite would have noticed.
    {
        unsigned next = 0, clock = 1000u;
        int fired = 0;
        for (int tries = 0; tries < 6; ++tries) {
            if (retry_due_at(next, clock)) {
                ++fired;
                retry_arm_at(next, clock, tries < 3 ? 1u : 2500u);   // the minimap's own schedule
            }
            clock += 20u;                                            // ~60 Hz : a frame
        }
        CHECK_EQ(fired, 4);          // three immediate, then the fourth waits : the hammer is gone
        CHECK(!retry_due_at(next, clock));
        CHECK(retry_due_at(next, clock + 2500u));   // ... and it comes back on its own
    }
}
