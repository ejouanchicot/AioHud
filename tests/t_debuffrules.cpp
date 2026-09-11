// t_debuffrules.cpp -- which debuff on a mob refuses an incoming cast (model/debuff_rules.h).
//
// The case that exists : Dia III up, Dia II cast on the same mob, and the row became Dia II with a 120 s timer.
// Measured on the live client on 2026-09-11 -- the weaker cast comes back as message 2 WITH ITS DAMAGE, so the
// server never says it was refused. The header carries the capture.
//
// The three sections below it are the ones that keep this honest, because each of them is a bug this codebase
// has already shipped once : a cycle read as a strength order (the Threnody row froze for the life of the mob),
// a ghost entry vetoing forever (no wear-off packet exists for someone else's debuff), and the -ga form not
// being recognised as the same spell (a Diaga I silently downgraded a Dia II).
#include "check.h"
#include "model/debuff_rules.h"

using namespace aio;

// One mob's debuff set, in the same columns record_debuff keeps.
struct Mob {
    unsigned short spell[16] = {};
    unsigned       startMs[16] = {};
    unsigned       durMs[16] = {};
    int            n = 0;
    void put(unsigned short sp, unsigned start, unsigned dur) { spell[n] = sp; startMs[n] = start; durMs[n] = dur; ++n; }
    int refused(unsigned short cast, unsigned now, bool named = false) const { return debuff_refused_by(spell, startMs, durMs, n, cast, now, named); }
};

// The ids used below, from res : Dia I..V 23-27 · Diaga II 34 · Bio I..III 230-232 · Burn 235 · Frost 236
// Drown 240 · Fire Threnody II 871 · Ice Threnody II 872 · Poison II 221 · Poison 220.
static const unsigned short DIA = 23, DIA2 = 24, DIA3 = 25, DIAGA2 = 34;
static const unsigned short BIO = 230, BIO2 = 231, BIO3 = 232;
static const unsigned short BURN = 235, FROST = 236, DROWN = 240;
static const unsigned short THREN_FIRE2 = 871, THREN_ICE2 = 872;
static const unsigned short POISON = 220, POISON2 = 221;
static const unsigned short SLOW2 = 79, HOJO_SAN = 346;

void test_debuffrules() {

    SECTION("THE MEASURED CASE : a live Dia III refuses a Dia II");
    {   Mob m; m.put(DIA3, 1000, 180000);
        CHECK_EQ(0, m.refused(DIA2, 1000 + 5000));      // 5 s into a 180 s Dia III
        CHECK_EQ(0, m.refused(DIA, 1000 + 5000));       // and a Dia I likewise
    }

    SECTION("the stronger cast still lands : Dia II up, Dia III lands");
    {   Mob m; m.put(DIA2, 1000, 120000);
        CHECK_EQ(-1, m.refused(DIA3, 1000 + 5000));
    }

    SECTION("a re-cast of the same spell is a refresh, never a refusal");
    {   Mob m; m.put(DIA3, 1000, 180000);
        CHECK_EQ(-1, m.refused(DIA3, 1000 + 5000));
    }

    SECTION("GHOSTS : an expired Dia III vetoes nothing");
    {   // No wear-off packet exists for a debuff someone else cast, so the entry outlives the effect. Refusing
        // on it is how the FIRST refuse-path died : a mate's ten-minute-old Dia blocked every re-cast until the
        // mob was dead. The boundary is exact -- one ms before the end it still refuses, at the end it does not.
        Mob m; m.put(DIA3, 1000, 180000);
        CHECK_EQ(0,  m.refused(DIA2, 1000 + 179999));
        CHECK_EQ(-1, m.refused(DIA2, 1000 + 180000));
        CHECK_EQ(-1, m.refused(DIA2, 1000 + 600000));
    }

    SECTION("an unknown duration counts as expired, not as forever");
    {   Mob m; m.put(DIA3, 1000, 0);
        CHECK_EQ(-1, m.refused(DIA2, 1000 + 5000));
    }

    SECTION("CYCLES : the eight Threnody II overwrite each other, so neither is the stronger");
    {   // All on status 217, all listing each other. Read as strength this froze the row on the first Threnody
        // for the life of the mob -- switching element must always land.
        Mob m; m.put(THREN_FIRE2, 1000, 120000);
        CHECK_EQ(-1, m.refused(THREN_ICE2, 1000 + 5000));
        Mob n; n.put(THREN_ICE2, 1000, 120000);
        CHECK_EQ(-1, n.refused(THREN_FIRE2, 1000 + 5000));
    }

    SECTION("CYCLES : the six elemental DoTs are a ring, not a ladder");
    {   // Burn->Frost->Choke->Rasp->Shock->Drown->Burn. Every pair replaces the other in game ; a 6-cycle is the
        // same defect as the mutual pair above, which is why the generator tests reachability and not the edge.
        Mob m; m.put(DROWN, 1000, 90000);
        CHECK_EQ(-1, m.refused(BURN, 1000 + 5000));
        Mob n; n.put(BURN, 1000, 90000);
        CHECK_EQ(-1, n.refused(FROST, 1000 + 5000));
    }

    SECTION("the relation crosses families : Dia III refuses Bio II, Bio III refuses Dia III");
    {   Mob m; m.put(DIA3, 1000, 180000);
        CHECK_EQ(0, m.refused(BIO2, 1000 + 5000));
        CHECK_EQ(0, m.refused(BIO,  1000 + 5000));
        Mob n; n.put(BIO3, 1000, 180000);
        CHECK_EQ(0, n.refused(DIA3, 1000 + 5000));
    }

    SECTION("the -ga form is the same spell : a live Dia III refuses a Diaga II");
    {   Mob m; m.put(DIA3, 1000, 180000);
        CHECK_EQ(0, m.refused(DIAGA2, 1000 + 5000));
    }

    SECTION("an unrelated debuff never refuses, whatever else is up");
    {   Mob m; m.put(DIA3, 1000, 180000); m.put(POISON2, 1000, 120000);
        CHECK_EQ(-1, m.refused(POISON2, 1000 + 5000));   // same spell -> refresh
        CHECK_EQ(1,  m.refused(POISON,  1000 + 5000));   // Poison II outranks Poison
        CHECK_EQ(-1, m.refused(THREN_FIRE2, 1000 + 5000));
    }

    SECTION("THE SERVER WINS : a message that NAMES the status is never second-guessed");
    {   // "<actor> casts <spell>. <target> is <status>" is the game announcing the effect it just applied. The
        // table is not a ladder even where it is acyclic -- res has Slow II overwriting Hojo: San and nothing
        // back -- so without this a Hojo: San landing on a live Slow II would be refused here while the game was
        // saying it landed. A pure enfeeble always comes back named or as "no effect", so prediction is left to
        // the one shape that needs it : a message proving only that damage landed.
        Mob m; m.put(SLOW2, 1000, 180000);
        CHECK_EQ(0,  m.refused(HOJO_SAN, 1000 + 5000, false));   // damage-only / unnamed -> the table decides
        CHECK_EQ(-1, m.refused(HOJO_SAN, 1000 + 5000, true));    // the server named it -> it landed, full stop
        Mob d; d.put(DIA3, 1000, 180000);
        CHECK_EQ(-1, d.refused(DIA2, 1000 + 5000, true));        // holds for the measured case too
    }

    SECTION("an empty set, an unknown spell and a zero cast all land");
    {   Mob empty;
        CHECK_EQ(-1, empty.refused(DIA2, 5000));
        Mob m; m.put(DIA3, 1000, 180000);
        CHECK_EQ(-1, m.refused(0, 1000 + 5000));         // no spell known for the cast -> nothing to compare
        CHECK_EQ(-1, m.refused(9999, 1000 + 5000));      // not in the table at all
        Mob anon; anon.put(0, 1000, 180000);             // an entry whose caster spell was never known
        CHECK_EQ(-1, anon.refused(DIA2, 1000 + 5000));
    }

    SECTION("the tick counter wrapping does not resurrect a ghost");
    {   // GetTickCount wraps every 49.7 days and the subtraction is unsigned on purpose : an entry started just
        // before the wrap must still age normally across it.
        const unsigned justBefore = 0xFFFFF000u;
        Mob m; m.put(DIA3, justBefore, 180000);
        CHECK_EQ(0,  m.refused(DIA2, justBefore + 5000));        // 5 s in, across the wrap
        CHECK_EQ(-1, m.refused(DIA2, justBefore + 181000));      // and expired on the other side
    }
}
