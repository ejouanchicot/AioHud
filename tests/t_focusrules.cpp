// t_focusrules.cpp -- the FOCUS monitor's judgements (model/focus_rules.h).
//
// The monitor is what //aio out and //aio in act on, so a wrong rule here is a row a person removed on purpose
// that never comes back, or a red alert for something they cannot fix. Both happened.
#include "check.h"
#include "model/focus_rules.h"

using namespace aio;

void test_focus_rules() {

    SECTION("a hand-mute is lifted by a NEWER cast, not by a different one");
    {   // The shipped defect: you Haste the wrong person, //aio out the row, then Haste the right one -- or that
        // same person on purpose. Haste overwrites itself, so the entry is never starved and the row stayed
        // hidden for as long as the buff was maintained.
        CHECK_EQ((int)FOCUS_LIFT,   (int)focus_mute_verdict(true, true, /*cast*/2000, /*mute*/1000));
        CHECK_EQ((int)FOCUS_KEEP,   (int)focus_mute_verdict(true, true, /*cast*/1000, /*mute*/1000));   // the same cast
    }
    {   // Strictly newer. When one of a same-status pair is pruned the maximum goes DOWN, and a smaller number
        // is not a cast -- reading "different" as "newer" would un-mute a row nobody re-cast.
        CHECK_EQ((int)FOCUS_KEEP,   (int)focus_mute_verdict(true, true, /*cast*/500, /*mute*/1000));
    }
    {   // No cast reference at all is not a newer cast either.
        CHECK_EQ((int)FOCUS_KEEP,   (int)focus_mute_verdict(true, true, /*cast*/0, /*mute*/1000));
    }

    SECTION("a muted entry lives exactly as long as what feeds it");
    {   CHECK_EQ((int)FOCUS_FORGET, (int)focus_mute_verdict(true,  false, 2000, 1000));
        // ...while a WATCHED entry must survive its buff, or it could never raise the alert it exists for.
        CHECK_EQ((int)FOCUS_KEEP,   (int)focus_mute_verdict(false, false, 2000, 1000));
    }

    SECTION("two songs on one status are two things to watch");
    {   // MEASURED 2026-09-10: four songs up, six monitor entries -- Valor Minuet V watched by nobody, because it
        // shares status 198 with Minuet IV and the monitor held one entry per (person, status). Lose it and the row
        // just vanished: no red OUT, while Honor March and Madrigal alerted normally.
        //
        // No packet names which Minuet is which -- the 0x076 is presence-only (`214 198 198 199`). But it does say
        // HOW MANY, and that settles it: two entries, two copies -> both up; two entries, one copy -> one is gone.
        CHECK(focus_copies_cover(/*newerSiblings*/0, /*copies*/2));   // the newest of two, both up
        CHECK(focus_copies_cover(1, 2));                              // the older of two, both up
        CHECK(focus_copies_cover(0, 1));                              // the newest survives when one is left
        CHECK(!focus_copies_cover(1, 1));                             // ...and the older one is the one that went
        CHECK(!focus_copies_cover(0, 0));                             // nothing on the target at all
    }
    {   // The survivor is the most recent cast -- the copy that runs out is the one with least time left, normally
        // the older. It matters far more that this AGREES with the model's slot rule (model/song_slot.h) than that
        // it is right in a rare dispel: if they disagreed, a row could be pruned by one stage and alerted by the
        // other, for ever.
        CHECK(focus_newer_sibling(/*rank*/1000, /*spell*/397, /*otherRank*/2000, /*otherSpell*/398));
        CHECK(!focus_newer_sibling(2000, 398, 1000, 397));
    }
    {   // Two songs cast in the SAME instant must still order strictly, or each counts the other as newer, both
        // decide they are the one that went, and two live songs alert at once.
        CHECK(focus_newer_sibling(/*rank*/5000, /*spell*/397, /*otherRank*/5000, /*otherSpell*/398));
        CHECK(!focus_newer_sibling(5000, 398, 5000, 397));
    }
    {   // RANK IS THE CAST, NEVER THE MONITOR ENTRY. Measured in game 2026-09-10: ranked by entry birth, the model
        // kept Valor Minuet V while the monitor kept Minuet IV and called V lost for 22 s -- because an entry is
        // REUSED when you sing the song again, so its birth dated from a rotation twenty minutes earlier while the
        // cast it stood for was seconds old. Two stages, two different survivors, which is the one outcome this
        // rule must never produce. Here: the entry born FIRST carries the FRESHER cast, and it is the survivor.
        const unsigned bornOld = 1000,  bornNew = 90000;      // when the monitor first saw each song
        const unsigned castNew = 88000, castOld = 60000;      // ...and when the cast behind it actually landed
        (void)bornOld; (void)bornNew;
        CHECK(focus_newer_sibling(/*rank*/castOld, 398, /*otherRank*/castNew, 397));   // ranked by CAST : 397 survives
        CHECK(!focus_newer_sibling(castNew, 397, castOld, 398));
        CHECK(focus_newer_sibling(bornOld, 397, bornNew, 398));   // ranked by BIRTH it would have been the other way
    }
}
