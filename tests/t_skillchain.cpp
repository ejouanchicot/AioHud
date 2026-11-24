// t_skillchain.cpp -- skillchain resolution (the pure table + combination logic).
//
// Every case below is a bug that actually shipped, taken from the commit that fixed it :
//   306c7e0 -- sc_info returned Radiance for Light+Light UNCONDITIONALLY, so a non-Aeonic weapon like
//              Ukko's Fury was told it could follow up ; and a Light / Darkness chain suggested an
//              impossible next step instead of closing.
//   57d09bd -- Scholar Immanence spells were not recognised as chain openers.
#include "check.h"
#include "model/skillchain.h"

using namespace aio;

// sc_check_props takes the ACTIVE properties of the open chain and the OPENING properties of the candidate.
static bool chain(unsigned char oldp, unsigned char newp, int& lvl, int& res) {
    const unsigned char o[1] = { oldp }, n[1] = { newp };
    return sc_check_props(o, 1, n, 1, lvl, res);
}

#include "model/abilities_gen.h"     // the Ready moves' ids (recast 102)
#include "model/ready_moves_gen.h"   // their kind : a physical one carries a skillchain property
#include "model/mobskills_gen.h"     // every monster ability id of a move's name
#include "model/spell_elem_gen.h"    // SPELL_ELEMS[] + spell_elem() : the magic-burst element
#include <string.h>

void test_skillchain() {
    SECTION("skillchain : every physical Ready move has its properties (BG-Wiki Category:Jug_Pets)");
    {
        // the three mushrooms are physical with no property (BG-Wiki : None) ; Choke Breath is magical (Earth) with
        // none on BG-Wiki, though the kind table from BST-HUD calls it physical ; any other physical move without a row
        // is a hole the box would show as "no chain" (Fluid Toss and seven more were, until 2026-10-29)
        static const char* NONE[] = { "Queasyshroom", "Numbshroom", "Shakeshroom", "Choke Breath" };
        int holes = 0;
        for (const ReadyMoveRow& r : READY_MOVES) {
            if (strcmp(r.kind, "slashing") && strcmp(r.kind, "piercing") && strcmp(r.kind, "blunt")) continue;
            bool skip = false; for (const char* n : NONE) if (!strcmp(n, r.en)) skip = true;
            if (skip) continue;
            for (int i = 0; i < ABILS_N; ++i)
                if (ABILS[i].recast_id == 102 && !strcmp(ABILS[i].en, r.en) && !sc_skill_lookup(SCR_JA, ABILS[i].id)) {
                    printf("   no skillchain property for the Ready move %s (%u)\n", r.en, ABILS[i].id); ++holes; }
        }
        CHECK_EQ(holes, 0);
        // the pet's finish arrives as a MONSTER ability (0x028 category 11) : the jug pet ids of the eight BG-Wiki moves
        // (mobskills_gen.h 3950..3961) must resolve too -- the first fix put them in the ability table only, and
        // Fluid Spread opening a chain still showed nothing (the player, 2026-10-29)
        static const unsigned PET_MOB_IDS[] = { 3950, 3951, 3953, 3955, 3956, 3957, 3959, 3961 };
        for (unsigned id : PET_MOB_IDS) { const SkillRow* r = sc_skill_lookup(SCR_MOB, id); CHECK(r != 0); if (!r) printf("   monster ability %u : no row\n", id); }
        // every Ready move with a property, under every monster ability id of its name : a charmed pet uses the wild
        // monster's (a charmed tiger's Claw Cyclone is 273), a jug pet the 38xx-39xx one (the player, 2026-10-29)
        int mobHoles = 0;
        for (int i = 0; i < ABILS_N; ++i) {
            if (ABILS[i].recast_id != 102) continue;
            const SkillRow* ja = sc_skill_lookup(SCR_JA, ABILS[i].id);
            if (!ja) continue;
            for (const MobSkillRow& m : MOB_SKILLS) {
                if (strcmp(m.en, ABILS[i].en)) continue;
                const SkillRow* mr = sc_skill_lookup(SCR_MOB, m.id);
                if (!mr) { printf("   %s : monster ability %u has no row\n", m.en, m.id); ++mobHoles; }
                else if (mr->prop[0] != ja->prop[0] || mr->prop[1] != ja->prop[1]) { printf("   %s : monster ability %u has other properties\n", m.en, m.id); ++mobHoles; }
            }
        }
        CHECK_EQ(mobHoles, 0);
        const SkillRow* fs = sc_skill_lookup(SCR_MOB, 3957);   // Fluid Spread (pet) : Fragmentation / Transfixion
        if (fs) { CHECK_EQ(fs->prop[0], SCP_Fragmentation); CHECK_EQ(fs->prop[1], SCP_Transfixion); }
        // Bora Axe (Scission / Detonation) > Fluid Toss (Reverberation) = Reverberation, seen in game 2026-10-29
        const SkillRow* ft = sc_skill_lookup(SCR_JA, 792);
        CHECK(ft != 0);
        if (ft) CHECK_EQ(ft->prop[0], SCP_Reverberation);
        int lvl = 0, res = 0;
        CHECK(chain(SCP_Scission, SCP_Reverberation, lvl, res));
        CHECK_EQ(res, SCP_Reverberation);
    }

    SECTION("skillchain : the level-4 closers");
    {
        int lvl = 0, res = 0;
        CHECK(chain(SCP_Light, SCP_Light, lvl, res));
        CHECK_EQ(lvl, 4);
        CHECK_EQ(res, SCP_Light);          // Light + Light closes on LIGHT, not Radiance -- Radiance is Aeonic-only
        lvl = res = 0;
        CHECK(chain(SCP_Darkness, SCP_Darkness, lvl, res));
        CHECK_EQ(lvl, 4);
        CHECK_EQ(res, SCP_Darkness);
    }

    SECTION("skillchain : a known level-2 pair");
    {
        int lvl = 0, res = 0;                       // Fusion + Fragmentation = Light (the classic opener pair)
        CHECK(chain(SCP_Fusion, SCP_Fragmentation, lvl, res));
        CHECK_EQ(res, SCP_Light);
        CHECK(lvl >= 2);
    }

    SECTION("skillchain : a pair that does NOT chain stays refused");
    {
        int lvl = -1, res = -1;
        CHECK(!chain(SCP_Transfixion, SCP_Transfixion, lvl, res));   // same low property twice : no resonance
    }

    SECTION("skillchain : Aeonic add-effect animations");
    {
        CHECK_EQ(sc_from_add_effect_anim(15), SCP_Radiance);
        CHECK_EQ(sc_from_add_effect_anim(16), SCP_Umbra);
        CHECK_EQ(sc_from_add_effect_anim(0),  -1);      // not an Aeonic close
    }

    SECTION("skillchain : Aeonic weapon detection (Ukko's Fury is NOT one)");
    {
        CHECK_EQ(sc_aeonic_weapon_of_item(0), -1);      // no weapon equipped -> no Aeonic follow-up offered
    }

    SECTION("skillchain : names and colours stay inside their tables");
    {
        CHECK_STR(scprop_name(SCP_Light),  "Light");
        CHECK_STR(scelem_name(EL_Dark),    "Dark");
        CHECK(scelem_color(EL_Light) != 0);
        CHECK(scelem_color(EL_N)     != 0);             // out of range must fall back, not read past the array
        CHECK(scprop_color(SCP_N)    != 0);
    }

    SECTION("skillchain : message-id predicates");
    {
        // `|| true` used to make this line unfailable -- it was counted and reported green whatever the predicates
        // returned. Assert the MEASURED ids instead (skillchain.h documents where each one comes from), so the
        // Shield-Bash-under-Expiacion fix is locked against a regression rather than merely linked.
        CHECK(sc_is_weaponskill_msg(185));      // the one message a real weaponskill finish carries
        CHECK(!sc_is_weaponskill_msg(110));     // ...a JA finish is not one, which is the whole point of the split
        CHECK(sc_is_finish_msg(110));           // ...but it IS a chain-opening finish message
        CHECK(sc_is_finish_msg(185));
        CHECK(!sc_is_skillchain_msg(0));
        CHECK(!sc_is_finish_msg(0));
        CHECK(!sc_is_weaponskill_msg(0));
    }

    // ---- THE WHOLE TABLE (audit 5.4 #2). The reference below is the game's skillchain chart, typed here independently
    //      of sc_info -- not copied from it -- so a wrong row in either shows. Every one of the 16 x 16 single-property
    //      pairs is judged : a pair the chart has must chain to its level and result, and a pair it does not have must
    //      be REFUSED (an extra combination offers a continuation that does nothing in game).
    SECTION("skillchain : every opener x closer pair of the chart, and only those");
    {   // Mutations : one combo's result (Liquefaction+Impaction -> Fusion made Gravitation), one level (Fusion+Fragmentation
        // Lv.3 made 2), one extra combo (Transfixion's third slot Compression made Scission) -- each fails here.
        struct Ref { int open, close, lvl, result; };
        static const Ref CHART[] = {
            // level 1
            { SCP_Transfixion, SCP_Compression, 1, SCP_Compression },     { SCP_Transfixion, SCP_Reverberation, 1, SCP_Reverberation },
            { SCP_Compression, SCP_Transfixion, 1, SCP_Transfixion },     { SCP_Compression, SCP_Detonation, 1, SCP_Detonation },
            { SCP_Liquefaction, SCP_Scission, 1, SCP_Scission },
            { SCP_Scission, SCP_Liquefaction, 1, SCP_Liquefaction },      { SCP_Scission, SCP_Reverberation, 1, SCP_Reverberation },
            { SCP_Scission, SCP_Detonation, 1, SCP_Detonation },
            { SCP_Reverberation, SCP_Induration, 1, SCP_Induration },     { SCP_Reverberation, SCP_Impaction, 1, SCP_Impaction },
            { SCP_Detonation, SCP_Scission, 1, SCP_Scission },
            { SCP_Impaction, SCP_Liquefaction, 1, SCP_Liquefaction },     { SCP_Impaction, SCP_Detonation, 1, SCP_Detonation },
            { SCP_Induration, SCP_Compression, 1, SCP_Compression },      { SCP_Induration, SCP_Impaction, 1, SCP_Impaction },
            // level 2 : two level-1 properties make one
            { SCP_Transfixion, SCP_Scission, 2, SCP_Distortion },         { SCP_Liquefaction, SCP_Impaction, 2, SCP_Fusion },
            { SCP_Detonation, SCP_Compression, 2, SCP_Gravitation },      { SCP_Induration, SCP_Reverberation, 2, SCP_Fragmentation },
            // level 2 : a level-2 property continued by another
            { SCP_Gravitation, SCP_Fragmentation, 2, SCP_Fragmentation }, { SCP_Fragmentation, SCP_Distortion, 2, SCP_Distortion },
            { SCP_Distortion, SCP_Fusion, 2, SCP_Fusion },                { SCP_Fusion, SCP_Gravitation, 2, SCP_Gravitation },
            // level 3
            { SCP_Gravitation, SCP_Distortion, 3, SCP_Darkness },         { SCP_Distortion, SCP_Gravitation, 3, SCP_Darkness },
            { SCP_Fragmentation, SCP_Fusion, 3, SCP_Light },              { SCP_Fusion, SCP_Fragmentation, 3, SCP_Light },
            // level 4 : Light / Darkness continued by themselves (Radiance / Umbra only through an Aeonic : hud_skillchains)
            { SCP_Light, SCP_Light, 4, SCP_Light },                       { SCP_Darkness, SCP_Darkness, 4, SCP_Darkness },
        };
        const int N = (int)(sizeof(CHART) / sizeof(CHART[0]));
        CHECK_EQ(N, 29);
        int wrong = 0, extra = 0, missing = 0;
        for (int o = 0; o < SCP_N; ++o)
            for (int c = 0; c < SCP_N; ++c) {
                const Ref* want = 0;
                for (int i = 0; i < N; ++i) if (CHART[i].open == o && CHART[i].close == c) { want = &CHART[i]; break; }
                int lvl = -1, res = -1;
                const bool got = chain((unsigned char)o, (unsigned char)c, lvl, res);
                if (want && !got) { ++missing; printf("   missing : %s + %s should make %s Lv.%d\n", scprop_name(o), scprop_name(c), scprop_name(want->result), want->lvl); }
                else if (!want && got) { ++extra; printf("   extra   : %s + %s makes %s Lv.%d -- not in the chart\n", scprop_name(o), scprop_name(c), scprop_name(res), lvl); }
                else if (want && (lvl != want->lvl || res != want->result)) {
                    ++wrong; printf("   wrong   : %s + %s makes %s Lv.%d, the chart says %s Lv.%d\n", scprop_name(o), scprop_name(c), scprop_name(res), lvl, scprop_name(want->result), want->lvl);
                }
            }
        CHECK_EQ(missing, 0); CHECK_EQ(extra, 0); CHECK_EQ(wrong, 0);
    }

    SECTION("skillchain : each property's level and magic-burst elements");
    {   // Mutation : Distortion's elements (Ice, Water) made (Ice, Wind) -- the burst hint would name the wrong magic.
        struct Ref { int prop, lvl; unsigned char el[4]; int n; };
        static const Ref P[] = {
            { SCP_Transfixion, 1, { EL_Light }, 1 },   { SCP_Compression, 1, { EL_Dark }, 1 },      { SCP_Liquefaction, 1, { EL_Fire }, 1 },
            { SCP_Scission, 1, { EL_Earth }, 1 },      { SCP_Reverberation, 1, { EL_Water }, 1 },   { SCP_Detonation, 1, { EL_Wind }, 1 },
            { SCP_Impaction, 1, { EL_Lightning }, 1 }, { SCP_Induration, 1, { EL_Ice }, 1 },
            { SCP_Gravitation, 2, { EL_Earth, EL_Dark }, 2 },        { SCP_Fragmentation, 2, { EL_Wind, EL_Lightning }, 2 },
            { SCP_Distortion, 2, { EL_Ice, EL_Water }, 2 },          { SCP_Fusion, 2, { EL_Fire, EL_Light }, 2 },
            { SCP_Light, 3, { EL_Fire, EL_Wind, EL_Lightning, EL_Light }, 4 }, { SCP_Darkness, 3, { EL_Earth, EL_Ice, EL_Water, EL_Dark }, 4 },
            { SCP_Radiance, 4, { EL_Fire, EL_Wind, EL_Lightning, EL_Light }, 4 }, { SCP_Umbra, 4, { EL_Earth, EL_Ice, EL_Water, EL_Dark }, 4 },
        };
        CHECK_EQ((int)(sizeof(P) / sizeof(P[0])), (int)SCP_N);   // every property, none forgotten
        for (const Ref& r : P) {
            const SCInfo& s = sc_info(r.prop);
            if (s.lvl != r.lvl) printf("   %s : level %d, the chart says %d\n", scprop_name(r.prop), s.lvl, r.lvl);
            CHECK_EQ((int)s.lvl, r.lvl);
            CHECK_EQ((int)s.nElem, r.n);
            bool same = (int)s.nElem == r.n;   // the same SET (order free)
            for (int i = 0; same && i < r.n; ++i) { bool f = false; for (int j = 0; j < s.nElem; ++j) if (s.elem[j] == r.el[i]) f = true; same = f; }
            if (!same) printf("   %s : burst elements differ from the chart\n", scprop_name(r.prop));
            CHECK(same);
        }
    }

    SECTION("skillchain : a multi-property opener against a multi-property closer");
    {   // Savage Blade (Fragmentation, Scission) opens ; Knights of Round (Light, Fusion) answers with Fusion :
        // Fragmentation + Fusion = Light Lv.3 -- the higher property of the opener must be tried, not only the first.
        const unsigned char open[2] = { SCP_Fragmentation, SCP_Scission }, close[2] = { SCP_Light, SCP_Fusion };
        int lvl = 0, res = 0;
        CHECK(sc_check_props(open, 2, close, 2, lvl, res));
        CHECK_EQ(lvl, 3); CHECK_EQ(res, SCP_Light);
        // Scission alone against Liquefaction : the level-1 answer.
        const unsigned char open2[1] = { SCP_Scission }, close2[2] = { SCP_Gravitation, SCP_Liquefaction };
        CHECK(sc_check_props(open2, 1, close2, 2, lvl, res));
        CHECK_EQ(lvl, 1); CHECK_EQ(res, SCP_Liquefaction);
    }

    // ---- spell_elem (audit 5.4 #3) : the element a magic burst is drawn in. Agreement with res/spells.lua itself is
    //      check_gen_drift.py's job ; what nothing checked is the LOOKUP : a binary search, right only on a sorted table.
    SECTION("spell_elem : every row of the table is found by its own id, the table sorted, the elements in range");
    {   // Mutation : two rows swapped in spell_elem_gen.h (the generator's sort dropped) -- ids around them read -1.
        int unsorted = 0, badElem = 0, lost = 0;
        for (int i = 0; i < SPELL_ELEMS_N; ++i) {
            if (i && SPELL_ELEMS[i].id <= SPELL_ELEMS[i - 1].id) { ++unsorted; if (unsorted < 4) printf("   row %d (id %u) after id %u\n", i, SPELL_ELEMS[i].id, SPELL_ELEMS[i - 1].id); }
            if (SPELL_ELEMS[i].elem > EL_Dark) ++badElem;
            if (spell_elem(SPELL_ELEMS[i].id) != SPELL_ELEMS[i].elem) { ++lost; if (lost < 4) printf("   spell %u : lookup gives %d, the row says %d\n", SPELL_ELEMS[i].id, spell_elem(SPELL_ELEMS[i].id), SPELL_ELEMS[i].elem); }
        }
        CHECK_EQ(unsorted, 0); CHECK_EQ(badElem, 0); CHECK_EQ(lost, 0);
        CHECK(SPELL_ELEMS_N > 800);                       // the whole res table, not a stub (883 on 2026-11-24)
        CHECK_EQ(spell_elem(0), -1);                      // below the first id
        CHECK_EQ(spell_elem(65535), -1);                  // past the last
        CHECK_EQ(spell_elem(SPELL_ELEMS[0].id), SPELL_ELEMS[0].elem);
        CHECK_EQ(spell_elem(SPELL_ELEMS[SPELL_ELEMS_N - 1].id), SPELL_ELEMS[SPELL_ELEMS_N - 1].elem);
    }

    SECTION("spell_elem : the spells whose element is beyond doubt");
    {   // Mutation : Thunder (164) made Water in the table.
        CHECK_EQ(spell_elem(144), (int)EL_Fire);      CHECK_EQ(spell_elem(149), (int)EL_Ice);     // Fire, Blizzard
        CHECK_EQ(spell_elem(154), (int)EL_Wind);      CHECK_EQ(spell_elem(159), (int)EL_Earth);   // Aero, Stone
        CHECK_EQ(spell_elem(164), (int)EL_Lightning); CHECK_EQ(spell_elem(169), (int)EL_Water);   // Thunder, Water
        CHECK_EQ(spell_elem(1), (int)EL_Light);       CHECK_EQ(spell_elem(23), (int)EL_Light);    // Cure, Dia
        CHECK_EQ(spell_elem(28), (int)EL_Light);                                                  // Banish
        CHECK_EQ(spell_elem(230), (int)EL_Dark);      CHECK_EQ(spell_elem(245), (int)EL_Dark);    // Bio, Drain
        CHECK_EQ(spell_elem(253), (int)EL_Dark);                                                  // Sleep
    }
}
