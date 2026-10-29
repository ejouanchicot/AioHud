// t_geo.cpp -- the Geomancer box's model (src/model/geo_state.cpp) : which Geo- the luopan carries, how long its HP
// slope leaves it, and the Indi- we entrusted. Built packets, as t_readycharges.cpp does for the BST box : the 0x028
// layout is tests/fake_packets.h's, the one t_timers.cpp checks against the real parser.
#include "check.h"
#include "fake_packets.h"
#include "model/geo_state.h"
#include "model/geo_spells_gen.h"
#include "model/model_clock.h"

using namespace aio;

static const unsigned ME = 0x00012345, OTHER = 0x00054321, ALLY = 0x00067890, MOB = 0x01002A00;
static const unsigned LUO = 0x01004001, LUO2 = 0x01004002;

static void cast(unsigned actor, unsigned spell, unsigned target, unsigned category = 4) {
    fake::Packet p = fake::pkt_action(actor, category, spell, { { target, 236, 0 } });
    geo_on_packet(0x028, p.b);
}

void test_geo() {
    SECTION("geo : the spell table (scripts/gen_geo_spells.py, res/spells.lua)");
    {   const GeoSpellRow* h = geo_spell(801);
        CHECK(h != 0);
        if (h) { CHECK_STR(h->en, "Geo-Haste"); CHECK_EQ(h->status, 580); CHECK_EQ(h->element, 2); CHECK_EQ(h->geo, 1); CHECK_EQ(h->offensive, 0); }
        const GeoSpellRow* f = geo_spell(818);
        CHECK(f != 0);
        if (f) { CHECK_STR(f->en, "Geo-Frailty"); CHECK_EQ(f->status, 558); CHECK_EQ(f->offensive, 1); }   // Indi-Frailty's status
        CHECK(geo_spell(771) && geo_spell(771)->geo == 0);
        CHECK(geo_spell_by_status(580) && geo_spell_by_status(580)->id == 771);   // the Indi-, not the Geo-
        CHECK(geo_spell(1) == 0);
        int geo = 0, indi = 0; for (int i = 0; i < GEO_SPELLS_N; ++i) { if (GEO_SPELLS[i].geo) ++geo; else ++indi; CHECK(GEO_SPELLS[i].status != 0); }
        CHECK_EQ(geo, 30); CHECK_EQ(indi, 30);
    }

    SECTION("geo : Cardinal Chant, the bonus by the target's direction (BG-Wiki Cardinal_Chant)");
    {   CHECK_EQ(geo_cardinal_rank(24), 0); CHECK_EQ(geo_cardinal_rank(25), 1); CHECK_EQ(geo_cardinal_rank(64), 2);
        CHECK_EQ(geo_cardinal_rank(65), 3); CHECK_EQ(geo_cardinal_rank(99), 4);
        CardinalBonus n = geo_cardinal(0.0f, 4, true);      // due North : the whole Magic Crit., nothing else
        CHECK_EQ(n.value[CARD_N], 16); CHECK_EQ(n.value[CARD_E], 0); CHECK_EQ(n.value[CARD_W], 0);
        CardinalBonus e = geo_cardinal(90.0f, 4, true);     // due East : Magic Attack +17
        CHECK_EQ(e.value[CARD_E], 17); CHECK_EQ(e.value[CARD_N], 0);
        CardinalBonus ne = geo_cardinal(45.0f, 4, true);    // true Northeast : half of each, rounded down
        CHECK_EQ(ne.value[CARD_N], 8); CHECK_EQ(ne.value[CARD_E], 8);
        CHECK(ne.share[CARD_N] > 0.49f && ne.share[CARD_N] < 0.51f);
        CardinalBonus w = geo_cardinal(350.0f, 4, true);    // 10 degrees West of North : 7 steps of 8 toward North
        CHECK_EQ(w.value[CARD_N], 14); CHECK_EQ(w.value[CARD_W], 3);
        CardinalBonus s = geo_cardinal(180.0f, 1, false);   // rank I, not a -ra : Magic Accuracy +5
        CHECK_EQ(s.value[CARD_S], 5);
        CardinalBonus z = geo_cardinal(-90.0f, 4, true);    // -90 is West
        CHECK_EQ(z.value[CARD_W], 28);
        CardinalBonus none = geo_cardinal(90.0f, 0, true);  // under GEO 25 : no bonus
        CHECK_EQ(none.value[CARD_E], 0);
    }

    SECTION("geo : the HP slope gives the time left");
    {   LuopanSlope s;
        s.sample(100, 0);                                  // where the run starts : not a change
        s.sample(99, 1000);                                // the first step
        s.sample(95, 5000);
        CHECK_EQ(s.remaining_sec(5000), -1);               // 4 s of line : not yet
        s.sample(90, 10000);                               // 9 % over 9 s : 1 s a percent
        CHECK_EQ(s.remaining_sec(10000), 90);
        CHECK_EQ(s.remaining_sec(12500), 88);              // counted down between two steps (87.5 -> 88)
        s.sample(90, 12900);                               // the same reading again : nothing changes
        CHECK_EQ(s.remaining_sec(12900), 88);
        CHECK_EQ(s.remaining_sec(200000), 0);              // never below 0

        SECTION("geo : Life Cycle (HP up) starts the measurement over");
        s.sample(100, 13000);
        CHECK_EQ(s.remaining_sec(13000), -1);
        s.sample(99, 14000);
        CHECK_EQ(s.remaining_sec(14000), -1);              // 1 s of the new line
        s.sample(97, 19000);                               // 3 % over 6 s : 2 s a percent
        CHECK_EQ(s.remaining_sec(19000), 194);
    }
    {   LuopanSlope s;                                     // the first reading's moment inside its step is unknown : never used
        s.sample(80, 0); s.sample(79, 9000); s.sample(78, 10000);
        CHECK_EQ(s.remaining_sec(10000), -1);              // 1 s between the two steps, not 10
    }

    SECTION("geo : our Geo- names the bubble, its luopan's disappearance forgets it");
    {   geo_reset(); geo_set_me(ME);
        const unsigned t0 = model_now_ms();
        cast(ME, 818, MOB);                                // Geo-Frailty on a mob
        CHECK_EQ(geo_track().bubbleSpell, 818);
        geo_frame(LUO, 100, t0 + 500);
        CHECK_EQ(geo_track().luopanId, LUO); CHECK(geo_track().spawnKnown);
        geo_frame(0, -1, t0 + 1000);                       // one frame without it : a missed read, not Full Circle
        CHECK_EQ(geo_track().bubbleSpell, 818);
        geo_frame(LUO, 99, t0 + 1100);                     // back
        geo_frame(0, -1, t0 + 1200);
        geo_frame(0, -1, t0 + 1201 + GEO_GONE_MS);         // gone for good
        CHECK_EQ(geo_track().bubbleSpell, 0); CHECK_EQ(geo_track().luopanId, 0);
    }
    {   geo_reset(); geo_set_me(ME);                       // another player's Geo-, and a cast that did not finish
        cast(OTHER, 801, OTHER);
        CHECK_EQ(geo_track().bubbleSpell, 0);
        cast(ME, 801, ME, 8);                              // category 8 : casting begins / interrupted
        CHECK_EQ(geo_track().bubbleSpell, 0);
    }
    {   geo_reset(); geo_set_me(ME);                       // a Geo- whose luopan never came
        const unsigned t0 = model_now_ms();
        cast(ME, 801, ME);
        geo_frame(0, -1, t0 + 100);
        CHECK_EQ(geo_track().bubbleSpell, 801);
        geo_frame(0, -1, t0 + GEO_SPAWN_WAIT_MS + 500);
        CHECK_EQ(geo_track().bubbleSpell, 0);
    }
    {   geo_reset(); geo_set_me(ME);                       // the old luopan, seen once more by a lagging poll, is not the new one
        const unsigned t0 = model_now_ms();
        geo_frame(LUO, 40, t0);                            // a luopan with no cast of ours (plugin loaded with it out)
        CHECK_EQ(geo_track().luopanId, LUO); CHECK_EQ(geo_track().bubbleSpell, 0); CHECK(!geo_track().spawnKnown);
        cast(ME, 801, ME);
        geo_frame(LUO, 40, t0 + 16);
        CHECK_EQ(geo_track().luopanId, 0); CHECK_EQ(geo_track().bubbleSpell, 801);
        geo_frame(LUO2, 100, t0 + 800);
        CHECK_EQ(geo_track().luopanId, LUO2); CHECK_EQ(geo_track().bubbleSpell, 801); CHECK(geo_track().spawnKnown);
        geo_frame(LUO, 100, t0 + 900);                     // a different entity, with no cast in between : the old spell is not its
        CHECK_EQ(geo_track().bubbleSpell, 0);
    }
    {   geo_reset(); geo_set_me(ME);                       // a zone-out forgets
        cast(ME, 801, ME);
        unsigned char z[0x20] = {};
        geo_on_packet(0x00B, z);
        CHECK_EQ(geo_track().bubbleSpell, 0);
    }

    SECTION("geo : the 10-minute life caps the slope's figure");
    {   geo_reset(); geo_set_me(ME);
        const unsigned t0 = model_now_ms();
        cast(ME, 801, ME);
        geo_frame(LUO, 100, t0);
        geo_frame(LUO, 99, t0 + 1000);
        geo_frame(LUO, 98, t0 + 11000);                    // 10 s a percent : 980 s by the slope
        CHECK_EQ(geo_track().slope.remaining_sec(t0 + 11000), 980);
        CHECK_EQ(geo_bubble_left_sec(t0 + 11000), LUOPAN_MAX_SEC - 11);
    }

    SECTION("geo : an Indi- on another player is an Entrust ; on ourselves it is not");
    {   geo_reset(); geo_set_me(ME);
        cast(ME, 771, ME);                                 // Indi-Haste on us : the aura (party_state tracks it)
        CHECK_EQ(geo_track().entrustSpell, 0);
        cast(OTHER, 779, ALLY);                            // someone else's Indi- on the ally
        CHECK_EQ(geo_track().entrustSpell, 0);
        cast(ME, 779, ALLY);                               // Indi-Fury entrusted to the ally
        CHECK_EQ(geo_track().entrustSpell, 779); CHECK_EQ(geo_track().entrustTarget, ALLY); CHECK_EQ(geo_track().entrustEndMs, 0);
        geo_entrust_set_end(model_now_ms() + 300000);
        CHECK(geo_track().entrustEndMs != 0);
        CHECK_EQ(geo_track().bubbleSpell, 0);              // an Indi- names no bubble
        geo_entrust_clear();
        CHECK_EQ(geo_track().entrustSpell, 0);
    }
    geo_reset(); geo_set_me(0);
}
