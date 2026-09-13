// t_pointwatch.cpp -- the PointWatch memory block's decode (game_mem.h pw_decode_block). The in-game doctor found the
// Master Level dropping to 0 after every zone (2026-09-13) : the client zeroes the 0x061 mirror block until the next
// 0x061, and the Master Level was the one field read with no validity guard.
#include "check.h"
#include "model/game_mem.h"

using namespace aio;

void test_pointwatch() {
    SECTION("pointwatch memory : a live block gives XP, Exemplar and the Master Level");
    {
        PwMem m;
        pw_decode_block(true, 55999u | (56000u << 16), true, 709120u, 2936001u, true, 48u, m);
        CHECK(m.xpOk); CHECK_EQ(m.xpCur, 55999u); CHECK_EQ(m.xpTnl, 56000u);
        CHECK(m.epOk); CHECK_EQ(m.epCur, 709120u); CHECK_EQ(m.epTnml, 2936001u);
        CHECK(m.mlOk); CHECK_EQ(m.masterLevel, 48);
    }

    SECTION("pointwatch memory : a ZEROED block (the client during a zone) is not a Master Level of 0");
    {
        PwMem m;
        pw_decode_block(true, 0u, true, 0u, 0u, true, 0u, m);
        CHECK(!m.xpOk); CHECK(!m.epOk);
        CHECK(!m.mlOk);                 // the model then keeps the ML the last 0x061 gave (party_state_roster.cpp)
    }

    SECTION("pointwatch memory : a live block with Master Level 0 (a job without one) IS a Master Level of 0");
    {
        PwMem m;
        pw_decode_block(true, 1200u | (8000u << 16), true, 0u, 0u, true, 0u, m);
        CHECK(m.xpOk); CHECK(m.mlOk); CHECK_EQ(m.masterLevel, 0);
    }

    SECTION("pointwatch memory : a Master Level byte that could not be read gives no Master Level");
    {
        PwMem m;
        pw_decode_block(true, 55999u | (56000u << 16), true, 709120u, 2936001u, false, 48u, m);
        CHECK(!m.mlOk);
    }
}
