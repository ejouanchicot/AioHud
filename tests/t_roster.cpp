// t_roster.cpp -- the party roster's packet decode and the sentinel that cross-checks it against memory, in the fake
// game (fake_game.h). Both cases below came from the in-game doctor on 2026-09-13 and were verified to FAIL against
// the code as it was before being kept.
#include "check.h"
#include "fake_game.h"
#include "fake_packets.h"
#include "model/party_state.h"
#include "model/sentinel.h"
#include "model/gamestate.h"
#include <cstring>

using namespace aio;
using namespace fake;

namespace {
const unsigned ME = 0x00020001u, KAO = 0x00020002u;

// One render frame of the sentinel : what game_mem's poll does last, with the snapshot complete.
void sentinel_frames(int n) {
    GameState gs;
    gs.inGame = true;
    gs.buffsOk = false;   // the buff pair is not what these cases judge
    for (int i = 0; i < n; ++i) sentinel_tick(gs);
}

int member_slot(unsigned id) {
    for (int i = 0; i < party().count; ++i) if (party().m[i].id == id) return i;
    return -1;
}
}  // namespace

void test_roster() {
    if (selfcheck()) { printf("  the fake game does not reach the model -- the roster cases would prove nothing, skipped\n"); return; }

    SECTION("roster : 0x0DD carries HP% at 0x1D and MP% at 0x1E (measured on real packets)");
    {   // FOUND 2026-09-13 : on_dd read them swapped. Invisible whenever memory refreshes the row the same frame, and
        // wrong for every frame it does not (zoning, login) -- a member at 91% HP and 76% MP drew 76% HP and 91% MP.
        world({ { ME, "Tetsouo", 10, false }, { KAO, "Kaories", 21, false } });
        frame();   // the roster comes from memory : a 0x0DD only updates someone already in it
        deliver(pkt_member_update({ KAO, "Kaories", 2243, 412, 0, 91, 76, 21, 99, 3, 56, 0 }));
        const int i = member_slot(KAO);
        CHECK(i >= 0);
        if (i >= 0) { CHECK_EQ(party().m[i].hpp, 91); CHECK_EQ(party().m[i].mpp, 76); CHECK_EQ(party().m[i].hp, 2243); CHECK_EQ(party().m[i].mp, 412); }
    }

    SECTION("roster : a real-size 0x0DD (52 bytes) is decoded at all -- and a short one is refused");
    {   // FOUND 2026-09-13 by capturing the raw packet : declared size 52, while on_dd required 0x3B and returned on every
        // one of them. The sentinel fed from it therefore never armed ("not checked yet" after disband / invite).
        world({ { ME, "Tetsouo", 10, false }, { KAO, "Kaories", 21, false } });
        frame();   // the roster comes from memory : a 0x0DD only updates someone already in it
        const unsigned char real[52] = { 0xDD,0x1A,0x07,0x00, 0x0B,0xBD,0x06,0x00, 0xA0,0x09,0x00,0x00, 0x86,0x01,0x00,0x00, 0,0,0,0,
                                         0x80,0,0,0, 0x63,0x04,0,0, 0x00,0x64,0x48,0x00, 0,0, 0x0A,0x63,0x13,0x35, 0x16,0x01,
                                         'T','e','t','s','o','u','o',0, 0,0,0,0 };   // Tetsouo's own, 2026-09-13 : 2464 HP 100%, 390 MP 72%, BRD99/DNC53
        unsigned char b[512] = { 0 }; memcpy(b, real, sizeof(real));
        for (int k = 0; k < 4; ++k) b[0x04 + k] = (unsigned char)(ME >> (8 * k));   // same packet, addressed to this world's ME
        b[0x08] = 0x10; b[0x09] = 0x27;                                             // HP 10000 : a value the memory row does not hold
        packet(0x0DD, b);
        CHECK_EQ(party().m[0].hp, 10000); CHECK_EQ(party().m[0].hpp, 100); CHECK_EQ(party().m[0].mpp, 72); CHECK_EQ(party().m[0].mjob, 10);

        unsigned char s[512] = { 0 }; memcpy(s, b, 0x24); s[0] = 0xDD; s[1] = (unsigned char)((0x24 / 4) << 1);   // declares 36 bytes
        s[0x08] = 0x20; s[0x09] = 0x4E;                                                                          // HP 20000
        packet(0x0DD, s);
        CHECK_EQ(party().m[0].hp, 10000);   // refused : the jobs and the name are not in it
    }

    SECTION("roster : a 0x0DD for someone outside the party (an alliance member) does not join the party");
    {   // The parser used to ADD any unknown id while the party had room -- dead code until the size fix, and wrong once
        // alive : the server sends 0x0DD for all 18 alliance members, and each add rewrote the roster cache on disk.
        world({ { ME, "Tetsouo", 10, false }, { KAO, "Kaories", 21, false } });
        frame();   // the roster comes from memory : a 0x0DD only updates someone already in it
        const int before = party().count;
        deliver(pkt_member_update({ 0x00029999u, "Stranger", 1500, 300, 0, 100, 100, 1, 99, 0, 0, 0 }));
        CHECK_EQ(party().count, before);
        CHECK_EQ(member_slot(0x00029999u), -1);
    }

    SECTION("roster : a trust's 0x0DD (job 0) keeps the job the roster resolved from the trust table");
    {   // A live on_dd would otherwise blank the trust's job icon until the next memory refresh.
        const unsigned MONB = 0x00020004u;
        world({ { ME, "Tetsouo", 10, false }, { MONB, "Monberaux", 0, true } });
        frame();
        const int i = member_slot(MONB);
        CHECK(i >= 0);
        if (i >= 0) {
            const int job = party().m[i].mjob;
            CHECK(job != 0);
            deliver(pkt_member_update({ MONB, "Monberaux", 2092, 74, 0, 100, 100, 0, 0, 0, 0, 0 }));
            CHECK_EQ(party().m[i].mjob, job);
            CHECK_EQ(party().m[i].hp, 2092);
        }
    }

    SECTION("sentinel : a 0x0DD whose member block is readable 2 s later is compared -- and agrees");
    {
        world({ { ME, "Tetsouo", 10, false }, { KAO, "Kaories", 21, false } });
        frame();   // the roster comes from memory : a 0x0DD only updates someone already in it
        int a0 = 0, d0 = 0, u0 = 0; sentinel_counts(SEN_MEMBER, a0, d0, u0);
        deliver(pkt_member_update({ KAO, "Kaories", 1000, 500, 0, 100, 100, 21, 99, 0, 49, 0 }));
        sentinel_frames(150);
        int a1 = 0, d1 = 0, u1 = 0; sentinel_counts(SEN_MEMBER, a1, d1, u1);
        CHECK_EQ(a1 - a0, 1); CHECK_EQ(d1 - d0, 0);
    }

    SECTION("sentinel : a 0x0DD that lands while the member block is unreadable is compared once memory is back");
    {   // FOUND 2026-09-13 : the doctor showed "party member ... not checked yet" after 40 minutes and 42 0x0DD. Real
        // players' 0x0DD arrive at a zone-in, when the member array is not readable 2 s later ; the comparison gave up
        // on that packet for good and waited for the next one -- which arrives at the next zone-in, in the same state.
        world({ { ME, "Tetsouo", 10, false }, { KAO, "Kaories", 21, false } });
        frame();   // the roster comes from memory : a 0x0DD only updates someone already in it
        int a0 = 0, d0 = 0, u0 = 0; sentinel_counts(SEN_MEMBER, a0, d0, u0);
        party_memory_unreadable(true);
        deliver(pkt_member_update({ KAO, "Kaories", 1000, 500, 0, 100, 100, 21, 99, 0, 49, 0 }));
        sentinel_frames(300);              // ~5 s of loading screen
        party_memory_unreadable(false);
        sentinel_frames(300);
        int a1 = 0, d1 = 0, u1 = 0; sentinel_counts(SEN_MEMBER, a1, d1, u1);
        CHECK_EQ(a1 - a0, 1); CHECK_EQ(d1 - d0, 0); CHECK_EQ(u1 - u0, 0);
    }

    SECTION("sentinel : memory that never comes back ends the retries, and SAYS so");
    {   // Bounded (rule 10) : the retry is a budget, not a loop -- and a packet it could not compare is counted, so the
        // report reads "could not be compared" instead of the "not checked yet" that looked like nothing had happened.
        world({ { ME, "Tetsouo", 10, false }, { KAO, "Kaories", 21, false } });
        frame();   // the roster comes from memory : a 0x0DD only updates someone already in it
        int a0 = 0, d0 = 0, u0 = 0; sentinel_counts(SEN_MEMBER, a0, d0, u0);
        party_memory_unreadable(true);
        deliver(pkt_member_update({ KAO, "Kaories", 1000, 500, 0, 100, 100, 21, 99, 0, 49, 0 }));
        sentinel_frames(60 * 60);          // a minute
        int a1 = 0, d1 = 0, u1 = 0; sentinel_counts(SEN_MEMBER, a1, d1, u1);
        CHECK_EQ(a1 - a0, 0); CHECK_EQ(d1 - d0, 0); CHECK_EQ(u1 - u0, 1);
        party_memory_unreadable(false);
        sentinel_frames(300);              // no zombie retry after the budget ran out
        int a2 = 0, d2 = 0, u2 = 0; sentinel_counts(SEN_MEMBER, a2, d2, u2);
        CHECK_EQ(a2 - a1, 0);
    }
}
