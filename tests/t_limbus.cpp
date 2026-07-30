// t_limbus.cpp -- the Limbus weekly-allowance boundary.
//
// A date calculation is the classic thing that is "obviously right" and wrong twice a year, and this one can
// only be observed on a Sunday afternoon -- so a bug in it would sit unnoticed for a week at a time and then
// reset the counter an hour early or late. limbus_week.h takes the time as a parameter precisely so this can be
// checked in milliseconds instead.
//
// The expected values were computed INDEPENDENTLY in Python from real calendar dates, not by running the C++ and
// writing down what it printed -- otherwise the test would only prove the code agrees with itself.
#include "check.h"
#include "model/limbus_week.h"

using namespace aio;

void test_limbus() {
    SECTION("limbus : the weekly reset is one UTC instant, Sunday 15:00");

    // Sunday 2026-07-26 15:00 UTC is the reset. Either side of it must land on a DIFFERENT week.
    CHECK_EQ(limbus_week_start(1785077940LL), 1784473200LL);   // Sun 14:59 -> still LAST Sunday's week
    CHECK_EQ(limbus_week_start(1785078000LL), 1785078000LL);   // Sun 15:00 -> the boundary itself, inclusive
    CHECK_EQ(limbus_week_start(1785078060LL), 1785078000LL);   // Sun 15:01 -> the new week

    // Everything from the boundary until the NEXT Sunday 15:00 belongs to the same week.
    CHECK_EQ(limbus_week_start(1785117600LL), 1785078000LL);   // Monday 02:00
    CHECK_EQ(limbus_week_start(1785625200LL), 1785078000LL);   // Saturday 23:00
    CHECK_EQ(limbus_week_start(1785682740LL), 1785078000LL);   // the following Sunday, 14:59 -- one minute short

    SECTION("limbus : a stored count is stale exactly when its week has rolled");

    const long long sunday1500 = 1785078000LL;                 // the reset
    const long long saturday   = 1785625200LL;                 // same week, six days later
    const long long nextSunday = 1785682800LL;                 // the NEXT reset (14:59 + 60 s)

    CHECK(!limbus_week_rolled(sunday1500, saturday));           // seen this week, read this week -> keep it
    CHECK(limbus_week_rolled(sunday1500, nextSunday));          // seen last week -> the allowance is full again
    CHECK(limbus_week_rolled(1784473200LL, sunday1500));        // seen the previous week, read at the boundary

    // No stamp at all = nothing to trust. Must report "rolled" so the display falls back to a full allowance
    // rather than showing a count it cannot date -- the failure mode that started this: a number that survived
    // a zone change but not a week, so it was silently a week out of date.
    CHECK(limbus_week_rolled(0, saturday));
    CHECK(limbus_week_rolled(-1, saturday));

    // A clock that is not set yet (nowUtc near 0) pushes the boundary negative. That must not fault or invert:
    // it degrades to "rolled", i.e. a full allowance, which is the safe direction.
    CHECK(limbus_week_rolled(0, 0));
    CHECK(limbus_week_start(0) < 0);

    SECTION("limbus : five runs a week");
    CHECK_EQ(LIMBUS_WEEK_RUNS, 5);

    SECTION("limbus : 'never observed' must not be reported as a full week");
    // The rule the display depends on, stated as a test because it was got WRONG on the first attempt: showing
    // 5 for "I have never seen the count" is indistinguishable from a measured 5, and it told a player who had
    // already spent runs that they had a full allowance. Only a REAL observation from a past week earns the 5.
    //
    // This mirrors PartyState::limbus_runs_left(), which cannot be linked here (it pulls in the whole packet
    // layer). Keeping the decision as a tiny pure lambda means the invariant is still executable.
    auto runs_left = [](int storedLeft, long long stamp, long long now) -> int {
        if (storedLeft < 0) return -1;                                  // never observed -> UNKNOWN
        if (limbus_week_rolled(stamp, now)) return LIMBUS_WEEK_RUNS;    // real, but last week -> full again
        return storedLeft > LIMBUS_WEEK_RUNS ? LIMBUS_WEEK_RUNS : storedLeft;
    };
    CHECK_EQ(runs_left(-1, 0,           saturday), -1);                 // nothing seen : unknown, NOT five
    CHECK_EQ(runs_left(-1, sunday1500,  saturday), -1);                 // a stamp without a count is still unknown
    CHECK_EQ(runs_left(2,  sunday1500,  saturday),  2);                 // seen this week -> the measured value
    CHECK_EQ(runs_left(2,  sunday1500,  nextSunday), LIMBUS_WEEK_RUNS); // seen last week -> the reset granted five
    CHECK_EQ(runs_left(0,  sunday1500,  saturday),  0);                 // none left is a real answer, not "unknown"
    CHECK_EQ(runs_left(9,  sunday1500,  saturday),  LIMBUS_WEEK_RUNS);  // a bad read is clamped, never printed raw
}
