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

    SECTION("the song maximum is learned only in a window that cannot lie");
    {   ClarionBase b; b.base = 0; b.valid = false; b.slotOpen = false;
        b = clarion_learn(b, 4, /*ccUp*/false, /*ccOnRecast*/false);          // Clarion Call fully available
        CHECK(b.valid);
        CHECK_EQ(4, b.base);
        b = clarion_learn(b, 3, false, false);                                // a smaller count never lowers it
        CHECK_EQ(4, b.base);
        b = clarion_learn(b, 5, /*ccUp*/true, false);                         // under Clarion Call : carries a bonus
        CHECK_EQ(4, b.base);
        b = clarion_learn(b, 5, false, /*ccOnRecast*/true);                   // just after it dropped : still poisoned
        CHECK_EQ(4, b.base);
    }
    {   // Nothing is learned from an empty rotation -- zero songs says nothing about the maximum.
        ClarionBase b; b.base = 0; b.valid = false; b.slotOpen = false;
        b = clarion_learn(b, 0, false, false);
        CHECK(!b.valid);
    }

    SECTION("Clarion Call opens the slot, it does not hold it open");
    {   // Told 2026-09-10, and it undoes an assumption the rule was built on: the fifth song OUTLIVES the buff, and
        // may be re-sung over itself with no Clarion Call at all. The slot closes only when that song is really lost.
        //
        // So the recast finishing is NOT proof of an unaided song set. A bard who keeps the fifth song refreshed
        // through the recast reaches "Clarion Call ready, five songs up" -- and the old gate read that as an honest
        // window and learned base = 5 for the rest of the session. From then on the genuinely lost fifth song raised
        // the permanent red alert this rule exists to prevent.
        ClarionBase b; b.base = 0; b.valid = false; b.slotOpen = false;
        b = clarion_learn(b, 4, false, false);                                 // learned honestly: four
        CHECK_EQ(4, b.base);
        b = clarion_learn(b, 5, /*ccUp*/true,  false);                         // Clarion Call opens the fifth slot
        b = clarion_learn(b, 5, false, /*ccOnRecast*/true);                    // buff over, recast running, fifth held
        b = clarion_learn(b, 5, /*ccUp*/false, /*ccOnRecast*/false);           // recast READY, fifth song STILL up
        CHECK_EQ(4, b.base);                                                   // <- the whole point: still four
    }
    {   // And the latch must not jam shut, or the base could never grow again -- a bard who buys a better instrument
        // would keep a stale maximum forever. An emptied song set proves nothing occupies the extra slot.
        ClarionBase b; b.base = 4; b.valid = true; b.slotOpen = true;
        b = clarion_learn(b, 0, false, false);                                 // everything gone (zoned, died, logged in)
        b = clarion_learn(b, 5, false, false);                                 // five held with no Clarion Call at all
        CHECK_EQ(5, b.base);
    }
    {   // A plugin loaded mid-session never saw the buff go up, only the recast running -- that must latch too, or
        // the very first frame would learn a Clarion-Call-aided count as the base.
        ClarionBase b; b.base = 0; b.valid = false; b.slotOpen = false;
        b = clarion_learn(b, 5, /*ccUp*/false, /*ccOnRecast*/true);
        CHECK(!b.valid);
        b = clarion_learn(b, 5, false, false);                                 // recast finishes, fifth song still held
        CHECK(!b.valid);
    }

    SECTION("the fifth song is allowed to go quietly");
    {   // "pour 4 songs c'est ok" : a song beyond the no-Clarion-Call maximum sits in a slot that disappears
        // when Clarion Call is spent. Losing it must not raise a permanent red OUT nobody can clear.
        ClarionBase b; b.base = 4; b.valid = true;
        // The slot really went : the song left YOUR list too (stillOnYou = false).
        CHECK(song_unrecoverable(b, /*onSelf*/false, /*isSong*/true, /*ccUp*/false, /*ccOnRecast*/true,
                                 /*count*/4, /*stillOnYou*/false));
    }
    {   // Every condition is load-bearing : drop any one and a real loss would be silenced.
        ClarionBase b; b.base = 4; b.valid = true;
        CHECK(!song_unrecoverable(b, false, true,  false, true,  3, false));   // below the base : refillable, alert it
        CHECK(!song_unrecoverable(b, false, true,  false, false, 4, false));   // Clarion Call ready : refillable
        CHECK(!song_unrecoverable(b, false, true,  true,  true,  4, false));   // Clarion Call up : the slot exists
        CHECK(!song_unrecoverable(b, true,  true,  false, true,  4, false));   // on yourself : always your problem
        CHECK(!song_unrecoverable(b, false, false, false, true,  4, false));   // not a song at all
        ClarionBase unlearned; unlearned.base = 0; unlearned.valid = false;
        CHECK(!song_unrecoverable(unlearned, false, true, false, true, 4, false));   // an unknown base silences nothing
    }

    SECTION("one ally losing a song is not the Clarion Call slot going");
    {   // MEASURED 2026-09-10, two allies, four songs, Clarion Call spent. Every live ally song came back from the
        // FOCUS dump flagged unrecoverable -- so a dispel on any one of them would have been swallowed in silence,
        // and its monitor entry freed, for the WHOLE hour of an SP2 recast. Rule 10, in its purest form.
        //
        // The count cannot tell the two apart : `songCount` is the distinct song SPELLS alive across ALL allies, so
        // Kaories being dispelled while Monberaux still carries the song does not move it. What separates them is
        // whether YOU still hold the song -- and you do, which is why you can simply sing it again.
        ClarionBase b; b.base = 4; b.valid = true;
        CHECK(!song_unrecoverable(b, false, true, false, true, /*count*/4, /*stillOnYou*/true));
    }
    {   // ...and being at the base is not, on its own, evidence of anything : the ordinary steady state of a bard
        // who has used Clarion Call once is exactly "at the base, recast running". Silence must cost more than that.
        ClarionBase b; b.base = 4; b.valid = true;
        CHECK(!song_unrecoverable(b, false, true, false, true, /*count*/5, /*stillOnYou*/true));
    }
    {   // THE COUNT MUST LAND EXACTLY ON THE BASE. Measured 2026-09-10: a plugin reload wipes the learned base and
        // it climbs back one rotation at a time, so it sat at 1 while four songs were up. Read as "at or above",
        // losing any one of the four gave "3 >= 1" and was silenced -- the very failure this rule had just been
        // fixed for, handed back by a reload. Landing exactly on the base is what "only the extra song went"
        // means: you held base + 1, you lost one, you are at base.
        ClarionBase low; low.base = 1; low.valid = true;
        CHECK(!song_unrecoverable(low, false, true, false, true, /*count*/3, /*stillOnYou*/false));   // four songs, base one : alert
        ClarionBase b; b.base = 4; b.valid = true;
        CHECK(song_unrecoverable(b,  false, true, false, true, /*count*/4, /*stillOnYou*/false));     // held 5, lost the extra
        CHECK(!song_unrecoverable(b, false, true, false, true, /*count*/5, /*stillOnYou*/false));     // still at 5 : nothing went
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
