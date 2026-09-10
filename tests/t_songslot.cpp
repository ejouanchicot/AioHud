// t_songslot.cpp -- the ally-song slot decision (model/song_slot.h).
//
// Every case below is a DEFECT THAT SHIPPED, reproduced from a real capture on 2026-09-09, or the behaviour
// that capture proved correct. They are written as the tape read them -- millisecond stamps and all -- because
// the three bugs were all about WHEN a piece of evidence was recorded relative to a cast, and a test that
// rounds that away tests nothing.
#include "check.h"
#include "model/song_slot.h"

using namespace aio;

// A target's rows, in one place, so a case reads like the tape it came from.
struct Rows {
    SlotEntry e[8]; int n = 0;
    Rows& song(unsigned startMs, unsigned short status, bool aoe = true) {
        e[n].startMs = startMs; e[n].status = status; e[n].aoe = aoe ? 1 : 0; e[n].isAbil = 0; ++n; return *this;
    }
    Rows& roll(unsigned startMs, unsigned short status) {
        e[n].startMs = startMs; e[n].status = status; e[n].aoe = 1; e[n].isAbil = 1; ++n; return *this;
    }
};
static SlotEvidence list(const unsigned short* ids, int n, unsigned stampMs) { SlotEvidence s; s.ids = ids; s.n = n; s.stampMs = stampMs; s.present = true; return s; }
static SlotEvidence none() { SlotEvidence s; s.ids = 0; s.n = 0; s.stampMs = 0; s.present = false; return s; }

void test_songslot() {
    const char* why = 0;

    // ---- what the server confirms, we keep ------------------------------------------------------------------
    {   // Two Marches share status 214 and legitimately coexist : the list carries it twice, so both stay.
        // Measured : "0x076 Kaories n=4 : 214 214 198 199".
        Rows r; r.song(1000, 214).song(2000, 214);
        const unsigned short ids[] = { 214, 214, 198, 199 };
        const SlotEvidence ev = list(ids, 4, 3000);
        SECTION("two Marches, both listed -> the older survives"); CHECK_EQ((int)SLOT_KEEP,
                 (int)song_slot_verdict(r.e, r.n, 0, ev, none(), &why));
        SECTION("two Marches, both listed -> the newer survives"); CHECK_EQ((int)SLOT_KEEP,
                 (int)song_slot_verdict(r.e, r.n, 1, ev, none(), &why));
    }
    {   // The same two rows when the server reports the status ONCE : the older one really has been replaced.
        Rows r; r.song(1000, 198).song(2000, 198);
        const unsigned short ids[] = { 198 };
        const SlotEvidence ev = list(ids, 1, 3000);
        SECTION("one Minuet listed -> the older is gone"); CHECK_EQ((int)SLOT_DROP_MEMBER,
                 (int)song_slot_verdict(r.e, r.n, 0, ev, none(), &why));
        SECTION("one Minuet listed -> the newer stays"); CHECK_EQ((int)SLOT_KEEP,
                 (int)song_slot_verdict(r.e, r.n, 1, ev, none(), &why));
    }

    // ---- DEFECT 1 : evidence older than the row it judges ---------------------------------------------------
    // Shipped behaviour : a row was dropped 15 ms after it was created, judged against a list recorded before
    // the cast -- and nothing ever restored it. Kaories lost a Minuet the server was still reporting.
    {
        Rows r; r.song(5000, 198);
        const unsigned short ids[] = { 199 };                 // the list predates the cast : no 198 in it yet
        const SlotEvidence ev = list(ids, 1, 4000);
        SECTION("a list older than the cast cannot drop it"); CHECK_EQ((int)SLOT_KEEP,
                 (int)song_slot_verdict(r.e, r.n, 0, ev, none(), &why));
    }

    // ---- DEFECT 2 : evidence older than the row invoked AGAINST it ------------------------------------------
    // Shipped behaviour : casting Valor Minuet IV at t=27797 dropped Valor Minuet V 15 ms later, because the new
    // row already counted as "newer" while the list still described the state before it. The two coexist.
    {
        Rows r; r.song(20000, 198).song(27797, 198);          // Minuet V, then Minuet IV
        const unsigned short ids[] = { 198 };                 // list recorded between the two casts
        const SlotEvidence ev = list(ids, 1, 25000);
        SECTION("a newer row the list has not seen cannot evict the older"); CHECK_EQ((int)SLOT_KEEP,
                 (int)song_slot_verdict(r.e, r.n, 0, ev, none(), &why));
    }
    {   // ...and once the list HAS seen both, the verdict is allowed again.
        Rows r; r.song(20000, 198).song(27797, 198);
        const unsigned short ids[] = { 198 };
        const SlotEvidence ev = list(ids, 1, 30000);
        SECTION("once the list has seen both, the older is evicted"); CHECK_EQ((int)SLOT_DROP_MEMBER,
                 (int)song_slot_verdict(r.e, r.n, 0, ev, none(), &why));
    }

    // ---- DEFECT 3 : a trust nothing could contradict ---------------------------------------------------------
    // Shipped behaviour : four trusts carried six songs each while the player beside them carried four. With no
    // 0x076 for a trust, YOUR timers answer -- but only for a song that also landed on you.
    {
        Rows r; r.song(1000, 207).song(2000, 214);
        const unsigned short mineIds[] = { 214 };             // the Capriccio (207) is gone from your own list
        const SlotEvidence mine = list(mineIds, 1, 3000);
        SECTION("a trust's AoE song your timers no longer carry is dropped"); CHECK_EQ((int)SLOT_DROP_SELF,
                 (int)song_slot_verdict(r.e, r.n, 0, none(), mine, &why));
        SECTION("a trust's AoE song your timers still carry is kept"); CHECK_EQ((int)SLOT_KEEP,
                 (int)song_slot_verdict(r.e, r.n, 1, none(), mine, &why));
    }
    {   // A Pianissimo song never touched you : your statuses know nothing about it, so it must survive.
        Rows r; r.song(1000, 196, /*aoe*/false);
        const unsigned short mineIds[] = { 214 };
        const SlotEvidence mine = list(mineIds, 1, 3000);
        SECTION("a single-target song is never judged by YOUR timers"); CHECK_EQ((int)SLOT_KEEP,
                 (int)song_slot_verdict(r.e, r.n, 0, none(), mine, &why));
    }

    // ---- the rule-10 shapes ---------------------------------------------------------------------------------
    {   // An empty list is what a FAILED read returns too -- it must never be read as "they carry nothing".
        Rows r; r.song(1000, 198);
        const SlotEvidence ev = list(0, 0, 3000);
        SECTION("an empty list drops nothing"); CHECK_EQ((int)SLOT_KEEP,
                 (int)song_slot_verdict(r.e, r.n, 0, ev, none(), &why));
    }
    {   // No list at all, and no self stand-in : unverifiable, so the row stays visible.
        Rows r; r.song(1000, 198);
        SECTION("no evidence at all drops nothing"); CHECK_EQ((int)SLOT_KEEP,
                 (int)song_slot_verdict(r.e, r.n, 0, none(), none(), &why));
    }
    {   // A roll is decided by its own rule, never by song slots.
        Rows r; r.roll(1000, 308);
        const unsigned short ids[] = { 199 };
        const SlotEvidence ev = list(ids, 1, 3000);
        SECTION("a roll is not judged here"); CHECK_EQ((int)SLOT_KEEP,
                 (int)song_slot_verdict(r.e, r.n, 0, ev, none(), &why));
    }

    // ---- the reason strings are part of the contract ---------------------------------------------------------
    {   // A bug report and //aio oblog print `why` : a verdict whose reason does not name its authority is
        // exactly what made "the row is missing" unanswerable for a whole afternoon.
        Rows r; r.song(1000, 198).song(2000, 198);
        const unsigned short ids[] = { 198 };
        const SlotEvidence ev = list(ids, 1, 3000);
        song_slot_verdict(r.e, r.n, 0, ev, none(), &why);
        SECTION("a member drop names the member's list"); CHECK(why && why[0] && strstr(why, "buff list") != 0);
        const unsigned short mineIds[] = { 199 };
        const SlotEvidence mine = list(mineIds, 1, 3000);
        Rows t; t.song(1000, 198);
        song_slot_verdict(t.e, t.n, 0, none(), mine, &why);
        SECTION("a self-evidence drop names your timers"); CHECK(why && strstr(why, "your own timers") != 0);
    }
}
