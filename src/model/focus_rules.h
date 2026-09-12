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

// ---- 2 and 3 : the learned cap and the unrecoverable fifth song -- MOVED to model/song_slots.h -------
//
// clarion_learn() took the cap as a high-water mark of observed song counts. That is structurally wrong:
// the limit follows the equipped INSTRUMENT, so the mark keeps the maximum reached under a Daurdabla long
// after the swap -- and a plugin reload or a job change reset it to 1, from where it silenced every song
// loss until a full rotation rebuilt it (measured 2026-09-10).
//
// The count both rules compared against was wrong twice over: it SKIPPED the fake songs, the very ones
// sung to occupy a slot, and it was global where the game counts per (singer, target).
//
// song_slots.h learns the cap from an EVICTION instead -- the game only makes room when there is none
// left, so the count at that moment is the limit, exactly -- and counts per person.

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


// ---- 6. a GROUPED red alert is a statement about the cast, exactly like "(AoE N)" on a healthy row ---------
//
// When a monitored buff goes missing the box draws a red OUT row. Several people losing the SAME buff can be
// one event or several, and the two want opposite things :
//   * one AoE cast (Protectra, a spell under SCH Accession, a COR roll) expires for everybody at once. That is
//     ONE fact, and six red lines for it is noise -- so it folds into "Phalanx (AoE 3)".
//   * three casts placed one by one on three people are THREE facts, and the NAME of each person is the whole
//     reason the per-person layout exists. Folding them drops exactly the information the alert is for.
//
// The rule shipped counting only "these alerts share a spell id", so three single-target Phalanx lost together
// came out as one nameless "Phalanx (AoE 3)". Reported from play on 2026-09-12 ("plusieurs personnes ont perdu
// Phalanx, ca les groupe alors qu'on ne veut pas"). It is the TWIN of the healthy-row defect fixed the same day
// in ally_group.h (`out.group = in.fresh && in.aoe && effN >= 2`) -- one of the two paths was corrected and the
// other was left, which is how a fix creates a report instead of closing one.
//
// So the cast decides here too: only entries whose cast actually named 2+ targets may group, and they group
// only with each other. `aoe` comes from the model (OtherBuff::aoe, set from the 0x028 target count).
struct AlertEntry {
    unsigned short spell;   // 0 = no known cast (food, gear) : never groupable
    unsigned char  aoe;     // the cast named 2+ targets
};

// How many alerts does entry `q` speak for ? 1 = draw it alone, WITH the name of whoever lost it.
// >= 2 = draw one grouped line for them all. Entries other than `q` that fold into it are found by the same
// rule, so the caller can skip any entry an earlier one already speaks for.
inline int focus_alert_speaks_for(const AlertEntry* a, int n, int q) {
    if (!a || q < 0 || q >= n) return 0;
    if (!a[q].spell || !a[q].aoe) return 1;          // a single-target cast (or none) stands alone, always
    int same = 0;
    for (int i = 0; i < n; ++i)
        if (a[i].spell == a[q].spell && a[i].aoe) ++same;   // only AoE entries fold, and only into an AoE entry
    return same < 1 ? 1 : same;
}

// Is `q` already covered by an earlier grouped alert ? Same rule, read from the other end.
inline bool focus_alert_covered(const AlertEntry* a, int n, int q) {
    if (!a || q <= 0 || q >= n) return false;
    if (!a[q].spell || !a[q].aoe) return false;      // a lone alert is never spoken for by anybody
    for (int i = 0; i < q; ++i)
        if (a[i].spell == a[q].spell && a[i].aoe) return true;
    return false;
}

} // namespace aio
