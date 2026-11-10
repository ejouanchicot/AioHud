// t_smn.cpp -- the Summoner content's table (src/model/smn_avatars_gen.h, scripts/gen_smn_avatars.py from the player's
// grimoire, checked against BG-Wiki on 2026-11-10) : which avatar a pet name is, and which pacts are whose.
// SPEC docs/design/smn-module-spec.md.
#include "check.h"
#include "model/smn_avatars_gen.h"
#include "model/abilities_gen.h"

using namespace aio;

void test_smn() {
    SECTION("smn : a pet name finds its avatar, whatever its case and spaces ; any other name finds none");
    {   // the avatars carry their res name ; a spirit's entity name may run its words together
        const int ifrit = smn_avatar_index("Ifrit");
        CHECK(ifrit >= 0);
        if (ifrit >= 0) { CHECK_STR(SMN_AVATARS[ifrit].img, "ifrit"); CHECK_EQ(SMN_AVATARS[ifrit].element, 0); CHECK_EQ(SMN_AVATARS[ifrit].spirit, 0); }
        CHECK_EQ(smn_avatar_index("cait sith"), smn_avatar_index("Cait Sith"));
        CHECK(smn_avatar_index("Cait Sith") >= 0);
        const int fire = smn_avatar_index("FireSpirit");
        CHECK(fire >= 0);
        if (fire >= 0) { CHECK_EQ(SMN_AVATARS[fire].spirit, 1); CHECK(SMN_AVATARS[fire].spells != 0); }
        CHECK_EQ(smn_avatar_index("Air Spirit"), smn_avatar_index("AirSpirit"));
        CHECK_EQ(smn_avatar_index("Ifri"), -1);          // a prefix is not the name
        CHECK_EQ(smn_avatar_index("Ifrits"), -1);
        CHECK_EQ(smn_avatar_index("Sweet Caroline"), -1);   // a BST pet
        CHECK_EQ(smn_avatar_index(""), -1);
        CHECK_EQ(smn_avatar_index(0), -1);
    }

    SECTION("smn : every pact is a res Blood Pact, every avatar's list fits the box, spirits have none");
    {   int per[SMN_AVATARS_N] = {};
        bool allNamed = true, allMp = true;
        for (int i = 0; i < SMN_PACTS_N; ++i) {
            const SmnPactRow& p = SMN_PACTS[i];
            CHECK(p.avatar < SMN_AVATARS_N);
            if (p.avatar < SMN_AVATARS_N) ++per[p.avatar];
            const AbilRow* r = abil_info(p.abil);
            if (!r) allNamed = false;
            else if (r->recast_id != 173 && r->recast_id != 174 && r->recast_id != 0) allMp = false;   // a pact's recast is Rage's or Ward's
        }
        CHECK(allNamed);
        CHECK(allMp);
        int maxPer = 0, spiritPacts = 0;
        for (int a = 0; a < SMN_AVATARS_N; ++a) { if (per[a] > maxPer) maxPer = per[a]; if (SMN_AVATARS[a].spirit) spiritPacts += per[a]; }
        CHECK(maxPer <= 16);          // SmnState::PACTS_MAX
        CHECK_EQ(spiritPacts, 0);
        CHECK_EQ(SMN_PACTS_N, 121);   // the grimoire's count, all found in res on 2026-11-10
    }

    SECTION("smn : Rage before Ward, and BG-Wiki's Ward for Atomos and Alexander (res files them as Rage)");
    {   const int ifrit = smn_avatar_index("Ifrit"), atomos = smn_avatar_index("Atomos"), odin = smn_avatar_index("Odin");
        bool wardSeen = false, order = true; int ifritW = 0, atomosW = 0, odinR = 0;
        for (int i = 0; i < SMN_PACTS_N; ++i) {
            const SmnPactRow& p = SMN_PACTS[i];
            if (p.avatar == ifrit) { if (p.ward) { wardSeen = true; ++ifritW; } else if (wardSeen) order = false; }
            if (p.avatar == atomos && p.ward) ++atomosW;
            if (p.avatar == odin && !p.ward) ++odinR;
        }
        CHECK(order);
        CHECK_EQ(ifritW, 2);    // Crimson Howl, Inferno Howl
        CHECK_EQ(atomosW, 2);   // Deconstruction, Chronoshift
        CHECK_EQ(odinR, 1);     // Zantetsuken
    }
}
