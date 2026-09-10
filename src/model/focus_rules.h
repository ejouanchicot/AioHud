// focus_rules.h -- the FOCUS monitor's three judgements: when a hand-mute is lifted, when a muted entry is
// forgotten, and when a lost song must NOT raise a red alert.
//
// Extracted from hud_timers.cpp on 2026-09-09 (element 4 of docs/audits/plan-harnais-2026-09-09.md). The
// monitor is the piece of Timers a PERSON has to be able to correct -- it is what //aio out and //aio in act
// on -- and its rules had been written three times over three incidents. Behind a test they can be read.
#pragma once

namespace aio {

// ---- 1. a hand-mute, and what ends it -----------------------------------------------------------------------
//
// //aio out silences ONE cast, not a spell. The correction that follows is almost always another cast of the
// same buff, often on the very person you took off -- and Haste, Refresh and Regen OVERWRITE themselves rather
// than lapsing, so the entry is never starved and would stay hidden for as long as you kept the buff up. That
// was the defect: a row removed by hand never came back.
//
// So a mute remembers WHICH cast it silenced, and a strictly NEWER cast lifts it. Strictly newer, never merely
// "different": when one of a same-status pair is pruned the maximum goes DOWN, and a smaller number is not a
// new cast. A muted entry that nothing refreshed this frame is forgotten -- you silenced one mistake, not the
// spell, so a later deliberate cast starts a fresh, watched entry.
enum FocusMute { FOCUS_KEEP = 0, FOCUS_LIFT, FOCUS_FORGET };

inline FocusMute focus_mute_verdict(bool muted, bool seenThisFrame, unsigned castRef, unsigned muteRef) {
    if (!muted) return FOCUS_KEEP;                       // a watched entry must outlive its buff to raise the OUT
    if (!seenThisFrame) return FOCUS_FORGET;             // nothing feeds it any more
    if (castRef && (int)(castRef - muteRef) > 0) return FOCUS_LIFT;
    return FOCUS_KEEP;
}

// ---- 2. how many songs you can hold, learned rather than read ------------------------------------------------
//
// A bard's song maximum is not in memory (both the count and the cap are server-side), so it is reconstructed:
// the high-water mark of the songs you maintain WHILE Clarion Call is fully available -- its buff down and its
// recast ready. That window carries no Clarion Call bonus, so the count observed in it IS your no-CC maximum,
// and it learns the instrument and merit setup with no gear reads at all.
//
// Gating on "recast ready" is not enough on its own, and believing it was cost us the rule below. CLARION CALL
// OPENS THE SLOT; IT DOES NOT HOLD IT OPEN. Once the fifth song is up it SURVIVES the buff ending, and you may
// re-sing it over itself for as long as you like without Clarion Call -- the slot closes only when that song is
// actually LOST (dispelled or run out) with the recast still running. So a bard who keeps refreshing a fifth song
// through the whole recast arrives at "Clarion Call ready again, five songs up", which the old gate read as an
// honest window: it learned base = 5, permanently, and from then on a genuinely lost fifth song raised exactly
// the un-clearable red alert this whole rule exists to prevent.
//
// Hence the latch. Clarion Call being up -- or merely on recast, which is how a mid-session plugin load sees a
// slot that was opened before we were watching -- marks the extra slot as possibly OCCUPIED, and only a song set
// that has emptied out proves it is not: with zero songs there is nothing left occupying anything. Nothing is
// learned while that latch is set, so the high-water mark can only ever be taken from a set we know is unaided.
struct ClarionBase { int base; bool valid; bool slotOpen; };

inline ClarionBase clarion_learn(ClarionBase cur, int songCount, bool ccUp, bool ccOnRecast) {
    if (ccUp || ccOnRecast) cur.slotOpen = true;   // an extra song may be alive -- learning here would poison the base
    if (songCount == 0) cur.slotOpen = false;      // nothing is up at all, so nothing occupies the extra slot
    if (!cur.slotOpen && songCount > 0) {
        if (songCount > cur.base) cur.base = songCount;
        cur.valid = true;
    }
    return cur;
}

// ---- 3. a lost song that cannot be brought back ---------------------------------------------------------------
//
// A song sung BEYOND the no-Clarion-Call maximum lives in a slot that disappears when Clarion Call is spent.
// Losing it is not a mistake to shout about -- "pour 4 songs c'est ok" -- so it must not raise a permanent red
// OUT. Every condition below is required, and each one narrows a way of being wrong:
//   - the base must be KNOWN (an unlearned base would silence everything);
//   - the song must be on an ALLY, not yourself;
//   - you must be EXACTLY AT the base, so the song that went is the extra one and nothing else;
//   - Clarion Call must be down AND on recast: with it available the slot is refillable, and a refillable song
//     keeps its normal alert, which is the point of having one;
//   - AND YOU MUST HAVE LOST THE SONG TOO. This last one was missing, and without it the rule silenced every
//     ally song loss for a full Clarion Call recast -- an hour of silence bought by one use of an SP2.
//
// AND THE COUNT MUST LAND EXACTLY ON THE BASE, not merely reach it. "At or above" reads as true for any count
// once the base is low, and the base IS low every time it has not been learned yet -- it resets on a job change
// and on a plugin reload, then climbs back one rotation at a time. Measured 2026-09-10: a reload left base = 1,
// and from that moment losing any one of four songs satisfied "3 >= 1" and was silenced. Landing exactly on the
// base is what "the extra song, and only the extra song, went" actually means: you held base + 1, you lost one,
// you are at base. Four songs on a base of one is not that shape, and now says so.
//
// That last condition is the one that separates the two situations, and the count alone never could. `songCount`
// is the number of distinct song SPELLS alive across ALL your allies, so one person being dispelled does not move
// it while anybody else still carries that song: the rule stayed true forever and the alert was suppressed
// (hud_timers.cpp) AND its entry freed, with nothing said. Measured 2026-09-10 with two allies -- six live,
// perfectly re-singable songs, all six flagged unrecoverable.
//
// Losing it FROM YOURSELF is what closes the slot -- not Clarion Call ending, which the fifth song outlives. So
// "gone from you too" is not a proxy for the slot closing, it IS the slot closing; and while you still hold the
// song, an ally missing it is simply someone to sing to again. Your own list is 0x063 -- server-exact, and never
// the "unverifiable" a trust's missing 0x076 leaves us with -- so it is the one signal here that cannot lie. A
// Pianissimo song you never held yourself is covered by the count instead: losing it drops `songCount` below the
// base, which reopens the alert.
inline bool song_unrecoverable(const ClarionBase& b, bool onSelf, bool isSong,
                               bool ccUp, bool ccOnRecast, int songCount, bool stillOnYou) {
    return b.valid && !onSelf && isSong && !ccUp && ccOnRecast && songCount == b.base && !stillOnYou;
}


// ---- 4. two songs, one status --------------------------------------------------------------------------------
//
// A bard runs Valor Minuet IV and V at once, or two Marches: DIFFERENT songs granting the SAME status id. The
// monitor used to hold one entry per (person, status), so the second song of a pair simply overwrote the first
// and was watched by nobody -- lose it and the row just vanished, with no red OUT. Measured 2026-09-10: four
// songs up, six monitor entries, Valor Minuet V absent from both its own row and Kaories'.
//
// Keying by SPELL gives each song its entry. Deciding whether it is still up cannot be done by spell, though --
// the 0x076 carries presence-only status ids (`214 198 198 199`), and no packet anywhere says which Minuet is
// which. What it DOES say is HOW MANY copies are up, and that is enough: hold N entries for one status, see M
// copies, and N - M of them are gone.
//
// Which N - M? The same answer the model's slot rule already gives (model/song_slot.h, tested): the survivors
// are the most recently cast, because the copy that runs out is the one with least time left, and that is
// normally the older cast. Being consistent with the prune matters more than being right in the rare dispel
// case -- if the two disagreed, a row could be dropped by one and alerted by the other, forever.
//
// AND THAT IS WHY `rank` IS THE CAST, NOT THE ENTRY. The first cut of this ordered by when the monitor entry was
// created, which is not the same thing at all: an entry is REUSED when you sing the song again, so its birth can
// date from twenty minutes and several rotations ago. Measured 2026-09-10 -- the model kept Minuet V while the
// monitor kept Minuet IV and reported V lost, the exact split this rule exists to prevent. Rank is the ally
// cast's startMs (what the prune itself compares) or, for your own copy, its expiry; larger always survives.
inline bool focus_newer_sibling(unsigned rank, unsigned short spell,
                                unsigned otherRank, unsigned short otherSpell) {
    const int d = (int)(otherRank - rank);
    if (d != 0) return d > 0;
    return otherSpell > spell;   // cast in the same instant -> an arbitrary but STABLE order, so nobody is counted twice
}

inline bool focus_copies_cover(int newerSiblings, int copiesPresent) {
    return newerSiblings < copiesPresent;
}


// ---- 5. a song pushed out of its slot -- MOVED to model/song_slots.h on 2026-09-10 ------------------
//
// It lived here as song_evicted(), and it compared a song count to a learned cap. Both were wrong:
// the count skipped the FAKE songs sung for the sole purpose of holding a slot, and it was global
// while the game counts per (singer, target). The replacement needs no cap at all -- the model names
// the victim at CAST time, while the set is still intact, and the monitor only asks whether the song
// that went is the one that was named.

} // namespace aio
