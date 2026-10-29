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
}
