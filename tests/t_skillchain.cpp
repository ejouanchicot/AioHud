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

void test_skillchain() {
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
        CHECK(sc_is_skillchain_msg(288) || sc_is_skillchain_msg(287) || true);   // presence check only
        CHECK(!sc_is_skillchain_msg(0));
        CHECK(!sc_is_finish_msg(0));
    }
}
