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
        // ...and an entry is never newer than ITSELF : the builder compares every entry with every entry, itself
        // included, so a tie that counted would make every watched buff one copy short -- all of them OUT at once.
        // Found 2026-09-14 : the mutation `otherSpell >= spell` failed five builder scenarios and not this section.
        CHECK(!focus_newer_sibling(5000, 397, 5000, 397));
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

    SECTION("focus : a GROUPED red alert is a statement about the cast, not about a shared spell id");
    // REPORTED FROM PLAY, 2026-09-12 : "plusieurs personnes ont perdu Phalanx, ca les groupe alors qu'on ne
    // veut pas". The rule shipped counting only "these alerts share a spell id", so three Phalanx placed one by
    // one on three people came out as ONE nameless "Phalanx (AoE 3)" -- and the two other alerts were dropped
    // as already-spoken-for, which is how the names disappeared. The per-person layout exists precisely to say
    // WHO is missing one.
    // It is the twin of the healthy-row defect fixed the same morning (ally_group.h : group only when
    // `fresh && aoe && effN >= 2`). One path was corrected, this one was left -- so the fix produced a report
    // instead of closing one, and that is the case these tests pin.
    {
        // three single-target casts : three facts, three named lines, nobody folded
        const AlertEntry single3[3] = { {116, 0}, {116, 0}, {116, 0} };   // 116 = Phalanx
        CHECK_EQ(focus_alert_speaks_for(single3, 3, 0), 1);
        CHECK_EQ(focus_alert_speaks_for(single3, 3, 1), 1);
        CHECK_EQ(focus_alert_speaks_for(single3, 3, 2), 1);
        CHECK(!focus_alert_covered(single3, 3, 1));   // the SECOND one must still be drawn
        CHECK(!focus_alert_covered(single3, 3, 2));   // ... and the third
    }
    {
        // one AoE cast (Accession / Protectra / a COR roll) reaching three people : ONE fact, one line
        const AlertEntry aoe3[3] = { {116, 1}, {116, 1}, {116, 1} };
        CHECK_EQ(focus_alert_speaks_for(aoe3, 3, 0), 3);
        CHECK(!focus_alert_covered(aoe3, 3, 0));      // the first speaks for them
        CHECK(focus_alert_covered(aoe3, 3, 1));       // the others fold into it
        CHECK(focus_alert_covered(aoe3, 3, 2));
    }
    {
        // MIXED, which is the case that tells the two rules apart : an Accession Phalanx on two people plus one
        // placed by hand on a third. Two facts : one grouped line for the pair, one NAMED line for the single.
        const AlertEntry mixed[3] = { {116, 1}, {116, 1}, {116, 0} };
        CHECK_EQ(focus_alert_speaks_for(mixed, 3, 0), 2);
        CHECK(focus_alert_covered(mixed, 3, 1));
        CHECK_EQ(focus_alert_speaks_for(mixed, 3, 2), 1);   // the hand-placed one keeps its own line ...
        CHECK(!focus_alert_covered(mixed, 3, 2));           // ... and its name
    }
    {
        // THE SECOND REPORT, same evening : "il avait pareil sur regen qui groupait aussi quand plusieurs
        // etaient out". Confirming it is spell-AGNOSTIC is the point -- the defect was never about Phalanx, it
        // was about counting a shared spell id as a shared event, so the rule must be checked on a spell whose
        // AoE form is reached differently (Regen goes AoE under SCH Accession, Phalanx too, but a Regen is far
        // more often placed one at a time). Regen II = 110.
        const AlertEntry regen3[3] = { {110, 0}, {110, 0}, {110, 0} };
        CHECK_EQ(focus_alert_speaks_for(regen3, 3, 0), 1);
        CHECK(!focus_alert_covered(regen3, 3, 1));
        CHECK(!focus_alert_covered(regen3, 3, 2));
        // ... and different TIERS were never the problem : they carry different spell ids, so they could not
        // fold even under the shipped rule. Pinned so nobody "fixes" that into existence.
        const AlertEntry tiers[3] = { {108, 1}, {110, 1}, {111, 1} };   // Regen, Regen II, Regen III
        CHECK_EQ(focus_alert_speaks_for(tiers, 3, 0), 1);
        CHECK_EQ(focus_alert_speaks_for(tiers, 3, 1), 1);
        CHECK(!focus_alert_covered(tiers, 3, 2));
    }
    {
        // different spells never fold together, whatever their AoE-ness
        const AlertEntry two[2] = { {116, 1}, {43, 1} };     // Phalanx + Refresh
        CHECK_EQ(focus_alert_speaks_for(two, 2, 0), 1);
        CHECK_EQ(focus_alert_speaks_for(two, 2, 1), 1);
        CHECK(!focus_alert_covered(two, 2, 1));
    }
    {
        // a spell-less entry (food, gear, a self-cast whose 0x028 was never seen) stands alone, always
        const AlertEntry nospell[2] = { {0, 1}, {0, 1} };
        CHECK_EQ(focus_alert_speaks_for(nospell, 2, 0), 1);
        CHECK_EQ(focus_alert_speaks_for(nospell, 2, 1), 1);
        CHECK(!focus_alert_covered(nospell, 2, 1));
        // and the out-of-range answers are defined, because the caller indexes with a loop variable
        CHECK_EQ(focus_alert_speaks_for(nospell, 2, 5), 0);
        CHECK(!focus_alert_covered(nospell, 2, 0));
    }

}
