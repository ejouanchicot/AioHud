// t_allygroup.cpp -- how many people a buff row speaks for, and whether it groups (model/ally_group.h).
//
// Every case is a rule the module learned the hard way, or a state read off a real capture. The three that
// carry a comment naming a symptom are shipped defects: they fail if the branch that fixed them is removed,
// which was verified before this file was kept.
#include "check.h"
#include "model/ally_group.h"

using namespace aio;

// Defaults that read like the common case: a fresh, real AoE song on a five-ally party, carried by you.
static GroupIn base() {
    GroupIn g;
    g.fresh = true; g.allies = 5; g.aoe = true; g.selfCast = true; g.meHas = true;
    g.countHas = 6; g.hasLagSameSpell = false;
    return g;
}

void test_ally_group() {
    SECTION("a normal song on the whole party");
    {   // The capture of 2026-09-09: five allies plus you, drawn as "Honor March (AoE 6)".
        GroupIn g = base();
        const GroupOut o = ally_group_verdict(g);
        CHECK_EQ(6, o.effN);
        CHECK(o.group);
        CHECK(!o.drop);
    }

    SECTION("Pianissimo : one person, its own line");
    {   // A single-target song must never borrow the server-side count. Shipped defect: Protectra V on the
        // party then Protect V on one ally gave that one-ally group N = 6 and drew "(AoE 6)".
        GroupIn g = base(); g.aoe = false; g.allies = 1; g.selfCast = false; g.meHas = false; g.countHas = 6;
        const GroupOut o = ally_group_verdict(g);
        CHECK_EQ(1, o.effN);
        CHECK(!o.group);
    }
    {   // SEVERAL single-target casts do not become an AoE by being several. Shipped defect, reported
        // 2026-09-12 : a setting folded them into one row anyway -- and it was the DEFAULT -- so three Phalanx
        // cast one by one drew "Phalanx (AoE 3)" for a player who had never used Accession. "(AoE N)" says what
        // the cast did, and no number of single-target casts can make it true. The setting is gone.
        GroupIn g = base(); g.aoe = false; g.allies = 3; g.selfCast = false; g.meHas = false; g.countHas = 6;
        const GroupOut o = ally_group_verdict(g);
        CHECK_EQ(3, o.effN);        // it still speaks for three people
        CHECK(!o.group);            // ...listed by name, one row each
        CHECK(!o.drop);
    }
    {   // ...and those same three under Accession, which is what makes it a real area cast, DO group.
        GroupIn g = base(); g.aoe = true; g.allies = 3; g.selfCast = true; g.meHas = true; g.countHas = 4;
        const GroupOut o = ally_group_verdict(g);
        CHECK_EQ(4, o.effN);
        CHECK(o.group);
    }

    SECTION("a laggard is listed by name, never merged");
    {   // The whole point of the split: a re-sing that missed two people must NAME them.
        GroupIn g = base(); g.fresh = false; g.allies = 2; g.countHas = 6;
        const GroupOut o = ally_group_verdict(g);
        CHECK_EQ(2, o.effN);
        CHECK(!o.group);          // never, whatever the count
    }
    {   // Its fresh sibling counts its OWN bucket : the server cannot tell an old copy from a fresh one, so
        // countHas would re-absorb the laggard into the group the split exists to separate.
        GroupIn g = base(); g.allies = 3; g.hasLagSameSpell = true; g.countHas = 6;
        const GroupOut o = ally_group_verdict(g);
        CHECK_EQ(4, o.effN);      // 3 allies + you, NOT the 6 the server reports
        CHECK(o.group);
    }

    SECTION("the empty party-buff cache must not shrink a real AoE");
    {   // Shipped defect: right after a plugin reload the party buff list is empty, countHas reads 0, and a
        // song AoE'd onto the party stopped regrouping as "(AoE N)" until the next zone.
        GroupIn g = base(); g.countHas = 0;
        const GroupOut o = ally_group_verdict(g);
        CHECK_EQ(6, o.effN);      // floored by our own bucket : 5 allies + you
        CHECK(o.group);
    }
    {   // And when the server knows MORE than our bucket -- an ally we cast on before a reload -- it wins.
        GroupIn g = base(); g.allies = 2; g.countHas = 6;
        const GroupOut o = ally_group_verdict(g);
        CHECK_EQ(6, o.effN);
    }

    SECTION("nobody carries it any more");
    {   GroupIn g = base(); g.allies = 0; g.countHas = 0; g.meHas = false; g.selfCast = false;
        const GroupOut o = ally_group_verdict(g);
        CHECK(o.drop);
    }

    SECTION("you are counted once, however we learn it");
    {   // selfCast and meHas are two ways of knowing the same thing; together they must not add two.
        GroupIn g = base(); g.countHas = 0; g.allies = 5; g.meHas = true; g.selfCast = true;
        CHECK_EQ(6, ally_group_verdict(g).effN);
        g.meHas = true; g.selfCast = false;   CHECK_EQ(6, ally_group_verdict(g).effN);
        g.meHas = false; g.selfCast = true;   CHECK_EQ(6, ally_group_verdict(g).effN);
        g.meHas = false; g.selfCast = false;  CHECK_EQ(5, ally_group_verdict(g).effN);
    }

    SECTION("two people is the smallest thing that can be a group");
    {   GroupIn g = base(); g.allies = 1; g.countHas = 2; g.meHas = true;
        CHECK_EQ(2, ally_group_verdict(g).effN);
        CHECK(ally_group_verdict(g).group);
        g.countHas = 1; g.allies = 0; g.meHas = true; g.selfCast = true;
        CHECK_EQ(1, ally_group_verdict(g).effN);
        CHECK(!ally_group_verdict(g).group);   // just you : a line, not a group
    }
}
