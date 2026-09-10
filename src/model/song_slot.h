// song_slot.h -- THE decision: does this ally-buff row still exist, or has it been pushed out of its song slot?
//
// Extracted from PartyState::prune_other_buffs_worn on 2026-09-09, as a PURE function over explicit inputs, for
// one reason: it produced three separate user-visible defects in a single day and none of them could be
// reproduced without the game running. Every input it needs is now passed in, so the whole decision is testable
// on a desk (tests/t_songslot.cpp), and each of those three defects is a test case that fails if it returns.
//
// WHAT IT DECIDES. A song occupies a SLOT on its target. A bard holds two by default, +2 with the right
// instrument and +1 under Clarion Call, for five at most; a song replaces itself, and past the cap a new song
// pushes out the shortest-lived one. We never know that cap directly -- so we do not model it. We count what
// the SERVER says the target is carrying and compare it with the rows we track: if strictly more of our rows
// for that status are newer than this one, this one no longer has a slot.
//
// THE EVIDENCE, AND ITS DATE. That comparison is only honest against evidence that has SEEN the events it is
// being used to judge. Both halves matter, and each was a real bug:
//   - evidence older than the row being judged cannot say the row is gone (the row was dropped 15 ms after it
//     was created, and nothing ever restored it);
//   - evidence older than a NEWER row cannot say that newer row took this one's place (a song cast a moment
//     earlier "replaced" one that was still perfectly alive -- its group then lost every ally and drew without
//     its (AoE N) marker).
//
// TRUSTS HAVE NO EVIDENCE OF THEIR OWN. The server sends a buff list for players only, so a trust's rows used
// to be unverifiable and simply accumulated: four trusts carried six songs each while the one real player in
// the same party carried the four the server confirmed. An AoE song lands on YOU at the same instant it lands
// on them, so your own timer list answers for them -- same server, same cast. That substitution is allowed for
// AoE songs only: a Pianissimo song never touches you, so your statuses know nothing about it.
#pragma once

namespace aio {

// One tracked row, reduced to what the decision reads.
struct SlotEntry {
    unsigned       startMs;   // when this row was created (the cast landed)
    unsigned short status;    // the status id the song grants -- several songs can share one
    unsigned char  aoe;       // the cast hit 2+ targets, so it hit you too
    unsigned char  isAbil;    // a Corsair roll, not a song : judged elsewhere, never here
};

// What a server list says, and WHEN it said it. `present` is the third state the callers used to collapse into
// "empty": a list we do not have at all is not a list saying nothing.
struct SlotEvidence {
    const unsigned short* ids;
    int                   n;
    unsigned              stampMs;
    bool                  present;
};

enum SlotVerdict { SLOT_KEEP = 0, SLOT_DROP_MEMBER, SLOT_DROP_SELF };

// `entries` holds the rows for ONE target; `idx` is the row being judged. `member` is that target's own buff
// list (present=false for a trust); `mine` is your own timer list, used only as the stand-in described above.
// `why` receives a short, stable reason -- it is what a bug report and //aio oblog print.
inline SlotVerdict song_slot_verdict(const SlotEntry* entries, int n, int idx,
                                     const SlotEvidence& member, const SlotEvidence& mine,
                                     const char** why) {
    const SlotEntry& e = entries[idx];
    *why = "kept";
    if (e.isAbil) { *why = "kept (a roll is not judged by song slots)"; return SLOT_KEEP; }

    // Pick the authority: the target's own list, else YOUR list for an AoE song, else nothing at all.
    const SlotEvidence* ev = 0; bool viaSelf = false;
    if (member.present)        ev = &member;
    else if (e.aoe && mine.present && mine.n > 0) { ev = &mine; viaSelf = true; }
    if (!ev) { *why = "kept (nothing can testify about this target)"; return SLOT_KEEP; }

    // An empty list is not proof of an empty target -- it is also what a failed read returns.
    if (ev->n <= 0) { *why = "kept (the list is empty, which is not the same as knowing it is empty)"; return SLOT_KEEP; }

    // Evidence recorded before this row cannot rule on it.
    if (ev->stampMs && (int)(e.startMs - ev->stampMs) > 0) {
        *why = "kept (this list predates the cast -- it cannot know about it)";
        return SLOT_KEEP;
    }

    int has = 0;
    for (int i = 0; i < ev->n; ++i) if (ev->ids[i] == e.status) ++has;

    // ...and it cannot testify that a row was replaced by one it has not seen either.
    int newer = 0;
    for (int j = 0; j < n; ++j) {
        if (j == idx) continue;
        const SlotEntry& o = entries[j];
        if (o.isAbil || o.status != e.status) continue;
        if (o.startMs <= e.startMs) continue;
        if (ev->stampMs && (int)(o.startMs - ev->stampMs) > 0) continue;
        ++newer;
    }

    if (newer >= has) {
        *why = viaSelf ? "slot rule (your own timers : this AoE song no longer fits)"
                       : "slot rule (the target's own buff list : replaced or worn)";
        return viaSelf ? SLOT_DROP_SELF : SLOT_DROP_MEMBER;
    }
    return SLOT_KEEP;
}

} // namespace aio
