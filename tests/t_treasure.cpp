// t_treasure.cpp -- the Treasure Pool box's model : packets (0x0D2 / 0x0D3) reconciled every frame against the game's
// own pool memory (*(g+0x5C), what the in-game Treasure menu draws), in the fake game (fake_game.h).
#include "check.h"
#include "fake_game.h"
#include "fake_packets.h"
#include "model/party_state.h"
#include <cstring>

using namespace aio;
using namespace fake;

namespace {
const unsigned ME = 0x00030001u;
unsigned unix_now() { return 1789000000u + now_ms() / 1000u; }   // the fake pins model_now_unix to this (fake_game.cpp)
const TreasureItem& slot(int i) { return party().treasure_slots()[i]; }
void fresh() { world({ { ME, "Tetsouo", 7, false } }); frame(); }
}  // namespace

void test_treasure() {
    if (selfcheck()) { printf("  the fake game does not reach the model -- the treasure cases would prove nothing, skipped\n"); return; }

    SECTION("treasure : items already in the pool when the plugin loads are shown -- adopted from the game's memory");
    {   // FOUND 2026-09-13 by the doctor : the plugin was reloaded with two items in the pool ; memory held them, Windower
        // showed them, the box stayed EMPTY -- packets were the only way in, and those two had been sent before the load.
        fresh();
        const unsigned drop = unix_now() - 60;
        treasure_memory({ { 0, 924, drop }, { 1, 2955, drop, 812, 0x00016500u, "Kaories" } });
        frame();
        CHECK_EQ(slot(0).itemId, 924); CHECK_EQ(slot(1).itemId, 2955);
        CHECK_EQ(slot(0).expireUnix, drop + 300);                 // the real 5-minute window, from the memory's drop time
        CHECK_EQ(slot(1).lot, 812); CHECK_STR(slot(1).lotter, "Kaories");
    }

    SECTION("treasure : an item WON (0x0D3 gone) is not brought back by a memory still showing it a moment");
    {
        fresh();
        const unsigned drop = unix_now() - 10;
        treasure_memory({ { 0, 924, drop } });
        deliver(pkt_pool_item(0, 924, drop));
        frame();
        CHECK_EQ(slot(0).itemId, 924);
        deliver(pkt_pool_lot(0, 950, "Tetsouo", true));          // won : the packet says it left the pool
        frame();
        CHECK_EQ(slot(0).itemId, 0);
        advance_ms(1000); frame();                                  // memory has not caught up yet
        CHECK_EQ(slot(0).itemId, 0);
        treasure_memory({});                                        // now it has
        advance_ms(3000); frame();
        CHECK_EQ(slot(0).itemId, 0);
    }

    SECTION("treasure : a zone-out empties the pool, and a stale memory does not refill it while loading");
    {
        fresh();
        treasure_memory({ { 0, 924, unix_now() - 30 } });
        frame();
        CHECK_EQ(slot(0).itemId, 924);
        unsigned char hdr[8] = { 0 }; hdr[0] = 0x0B; hdr[1] = (unsigned char)(2 << 1);
        packet(0x00B, hdr);                                         // zone-out : the pool is gone
        frame();
        CHECK_EQ(slot(0).itemId, 0);
        advance_ms(4000); frame();                                  // a loading screen longer than the 3 s after-clear guard
        CHECK_EQ(slot(0).itemId, 0);                                // only "zoning" keeps it out now
    }

    SECTION("treasure : an unmapped pool view adds nothing and removes nothing (unknown, rule 10)");
    {
        fresh();                                                    // memory never set : the read FAILS
        deliver(pkt_pool_item(0, 924, unix_now() - 5));
        advance_ms(5000); frame();
        CHECK_EQ(slot(0).itemId, 924);
    }

    SECTION("treasure : a packet item the mapped memory never shows is pruned after the grace (the phantom fix)");
    {
        fresh();
        treasure_memory({});
        deliver(pkt_pool_item(0, 924, unix_now() - 5));
        frame();
        CHECK_EQ(slot(0).itemId, 924);                              // fresh add : 2 s for the client to write its copy
        advance_ms(2500); frame();
        CHECK_EQ(slot(0).itemId, 0);
    }

    SECTION("treasure : an OLD drop adopted from memory gets a fresh 5 minutes, like a late packet does");
    {
        fresh();
        const unsigned drop = unix_now() - 3600;                    // an hour-old timestamp : the window is long past
        treasure_memory({ { 0, 924, drop } });
        frame();
        CHECK_EQ(slot(0).itemId, 924);
        CHECK_EQ(slot(0).expireUnix, unix_now() + 300);
    }
}
