// t_readycharges.cpp -- the Beastmaster's Ready charge pool (src/model/ready_charges.h).
//
// The cases are the ones checked on the BST-HUD addon the same day (2026-10-26) : a master with full merits, the
// job-point gift and Gleti's Breeches recharges a charge every 10 s, and that addon worked with 15 s -- one charge
// too many and a timer too low as soon as two were missing. The pool counter is the raw 1/60 s one.
#include "check.h"
#include "model/ready_charges.h"
#include "model/bst_state.h"
#include "model/ready_moves_gen.h"
#include <string.h>

using namespace aio;

// a 0x068 Pet Status as the server sends it : 4-byte header (id, size in 4-byte words in bits 9-15), then the fields
// at the offsets docs/game-data/target/hate-list.md and bst_state.cpp give
static void pet_status(unsigned char* p, unsigned owner, unsigned idx, int hpp, int mpp, unsigned tp, const char* name) {
    memset(p, 0, 0x20);
    const unsigned hdr = 0x068 | (8u << 9);              // 32 bytes
    p[0] = (unsigned char)(hdr & 0xFF); p[1] = (unsigned char)((hdr >> 8) & 0xFF);
    for (int i = 0; i < 4; ++i) { p[0x08 + i] = (unsigned char)(owner >> (8 * i)); p[0x10 + i] = (unsigned char)(tp >> (8 * i)); }
    p[0x0C] = (unsigned char)idx; p[0x0D] = (unsigned char)(idx >> 8);
    p[0x0E] = (unsigned char)hpp; p[0x0F] = (unsigned char)mpp;
    size_t n = strlen(name); if (n > 8) n = 8;
    memcpy(p + 0x18, name, n);
}

static ReadyView view(int sec, int len) { ReadyView v; ready_view(sec * 60, len, v); return v; }

void test_ready_charges() {
    SECTION("ready : the length before any measurement");
    CHECK_EQ(ready_len_fallback(0, 0, 0), 30);
    CHECK_EQ(ready_len_fallback(5, 2100, 0), 15);        // full merits + gift
    CHECK_EQ(ready_len_fallback(5, 2100, 1), 10);        // + Gleti's Breeches : the floor
    CHECK_EQ(ready_len_fallback(5, 2100, 2), 10);        // never under 10 s (BG Wiki)

    SECTION("ready : charges and the next one, 10 s a charge");
    CHECK_EQ(view(0, 10).charges, 3);  CHECK_EQ(view(0, 10).nextSec, -1);
    CHECK_EQ(view(5, 10).charges, 2);  CHECK_EQ(view(5, 10).nextSec, 5);
    CHECK_EQ(view(10, 10).charges, 2); CHECK_EQ(view(10, 10).nextSec, 10);   // exactly one charge out : 10, not 0
    CHECK_EQ(view(15, 10).charges, 1); CHECK_EQ(view(15, 10).nextSec, 5);
    CHECK_EQ(view(20, 10).charges, 1); CHECK_EQ(view(20, 10).nextSec, 10);
    CHECK_EQ(view(30, 10).charges, 0); CHECK_EQ(view(30, 10).nextSec, 10);
    { ReadyView v; CHECK(ready_view(26 * 60 + 36, 10, v)); CHECK_EQ(v.charges, 0); CHECK_EQ(v.nextSec, 7); }   // 26.6 s
    { ReadyView v; CHECK(!ready_view(31 * 60, 10, v)); }  // longer than 3 charges : the length is wrong, say so

    SECTION("ready : a move's wait by its cost");
    CHECK_EQ(ready_wait_sec(0, 10, 3), 0);
    CHECK_EQ(ready_wait_sec(5 * 60, 10, 3), 5);
    CHECK_EQ(ready_wait_sec(15 * 60, 10, 1), 0);
    CHECK_EQ(ready_wait_sec(15 * 60, 10, 2), 5);
    CHECK_EQ(ready_wait_sec(30 * 60, 10, 1), 10);
    CHECK_EQ(ready_wait_sec(30 * 60, 10, 2), 20);
    CHECK_EQ(ready_wait_sec(30 * 60, 10, 3), 30);

    SECTION("ready : the length a jump states");
    CHECK_EQ(ready_len_from_jump(600, 1), 10);
    CHECK_EQ(ready_len_from_jump(1200, 2), 10);
    CHECK_EQ(ready_len_from_jump(1182, 2), 10);          // 0.3 s lost to the frame : still 10
    CHECK_EQ(ready_len_from_jump(1200, 1), 20);
    CHECK_EQ(ready_len_from_jump(300, 1), 0);            // 5 s : no charge is that short
    CHECK_EQ(ready_len_from_jump(600, 0), 0);

    SECTION("ready : the learner pairs a jump with its move, either order");
    {   ReadyLearner L;
        L.frame(0, 1000);
        CHECK(!L.frame(600, 1016));                      // full pool, a 1-charge move : the jump waits for its move
        CHECK(L.move(1, 3000));                          // the pet's action, 2 s later
        CHECK_EQ(L.learned, 10);
    }
    {   ReadyLearner L;                                  // the timer was already running : the frame is added back
        L.frame(420, 1000);                              // 7 s on the pool
        CHECK(!L.frame(420 - 24 + 1200, 1400));          // 0.4 s later, a 2-charge move
        CHECK(L.move(2, 2000));
        CHECK_EQ(L.learned, 10);
    }
    {   ReadyLearner L;                                  // the move seen first
        L.frame(0, 1000);
        CHECK(!L.move(1, 1500));
        CHECK(L.frame(600, 1600));
        CHECK_EQ(L.learned, 10);
    }
    {   ReadyLearner L;                                  // two jumps before any move : nothing learned
        L.frame(0, 1000);
        L.frame(1200, 1100);                             // a 2-charge move
        L.frame(1800, 1200);                             // and a 1-charge one before the first landed
        CHECK(!L.move(2, 2000));
        CHECK(!L.move(1, 2100));
        CHECK_EQ(L.learned, 0);
    }
    {   ReadyLearner L;                                  // Sic shares the pool : nothing learned from it
        L.frame(0, 1000);
        L.sic(1050);
        CHECK(!L.frame(600, 1100));                      // Sic's jump
        CHECK(!L.move(2, 3000));                         // the move the pet picked on its own
        CHECK_EQ(L.learned, 0);
        L.frame(500, 1050 + ReadyLearner::PAIR_MS + 1);  // quiet over : a real move teaches again
        CHECK(!L.frame(500 + 600, 1050 + ReadyLearner::PAIR_MS + 17));
        CHECK(L.move(1, 1050 + ReadyLearner::PAIR_MS + 900));
        CHECK_EQ(L.learned, 10);
    }

    SECTION("ready : each move's icon (scripts/ready_move_icons.txt, checked on BG-Wiki 2026-10-27)");
    CHECK_EQ(ready_move_row("Bubble Curtain")->status, 41);   // Shell
    CHECK_EQ(ready_move_row("Sheep Song")->status, 2);        // sleep
    CHECK_EQ(ready_move_row("Gloeosuccus")->status, 13);      // slow
    CHECK_STR(ready_move_row("Cursed Sphere")->kind, "dark"); // the game data says light, BG-Wiki dark
    CHECK_STR(ready_move_row("Needleshot")->kind, "piercing");  // absent from BST-HUD
    CHECK_STR(ready_move_row("Sweeping Gouge")->kind, "blunt");      // as BST-HUD gives it
    CHECK_EQ(ready_move_row("Sweeping Gouge")->status, 0);
    CHECK(ready_move_row("Not A Move") == 0);

    SECTION("ready : our pet from the 0x068, another player's ignored");
    {   unsigned char p[0x20];
        bst_pet_clear(); bst_set_me(0x00012345);
        pet_status(p, 0x00099999, 0x0400, 50, 10, 900, "Other");
        bst_on_packet(0x068, p);
        CHECK_EQ(bst_pet().index, 0);                    // not ours
        pet_status(p, 0x00012345, 0x0401, 87, 40, 1250, "Randy");
        bst_on_packet(0x068, p);
        CHECK_EQ(bst_pet().index, 0x0401); CHECK_EQ(bst_pet().hpp, 87); CHECK_EQ(bst_pet().mpp, 40); CHECK_EQ(bst_pet().tp, 1250);
        CHECK_STR(bst_pet().name, "Randy");
        pet_status(p, 0x00012345, 0, 0, 0, 0, "");       // pet index 0 : the pet is gone
        bst_on_packet(0x068, p);
        CHECK_EQ(bst_pet().index, 0);
        CHECK_EQ(bst_ready_cost("Fluid Toss"), 1);
        CHECK_EQ(bst_ready_cost("Sic"), 0);              // shares the recast, is not a move
        bst_set_me(0);
    }
    {   ReadyLearner L;                                  // a jump nobody claims expires
        L.frame(0, 1000);
        L.frame(600, 1100);
        L.frame(500, 1100 + ReadyLearner::PAIR_MS + 1);
        CHECK(!L.move(1, 1100 + ReadyLearner::PAIR_MS + 2));
        CHECK_EQ(L.learned, 0);
    }
}
