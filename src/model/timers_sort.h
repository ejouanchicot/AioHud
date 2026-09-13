// timers_sort.h -- the order of the Timers rows : which row sorts AFTER which.
//
// Extracted from model/timers_build.cpp on 2026-09-13 (lot D, step 4 of the quality plan : the builder is cut into
// rules that can be read and tested on their own). The text of the rule is the one the builder carried ; only its
// shape changed, from a lambda capturing the frame to a function of its inputs. tests/t_timersrules.cpp holds the
// cases, each verified to fail against the branch it protects.
//
// GROUP by `order` first, then soonest-first, then a DETERMINISTIC tiebreak (icon, name) so rows with equal
// remaining are stable. Tiers, top->bottom : OUT alerts + YOUR buffs (0-1) ; buffs YOU cast on allies, grouped by
// ally (10-28) ; buffs a PLAYER put on you, grouped by that player (40-57) ; TRUSTS last (90+).
// The DISPLAYED second still decides first -- the sort must never contradict what the row reads, and the row
// sources do not all round the same way (a self buff ceils its tick, an ally estimate floors its ms), so a
// tick-first sort could put a visible 4:05 above a visible 4:04. `fine` only breaks the EQUAL-second ties,
// and that is exactly where the yoyo lived : two timers a fraction of a second apart read the same number
// every other second, and the icon/name tie-break then ordered them the opposite way from the second
// before, so the two rows swapped places once a second forever. Ranking a tie by the exact tick pins them.
// TWO ROWS WITHIN A SECOND OF EACH OTHER KEEP THE ORDER THEY HAD. Nothing about a countdown is stable
// at that distance: `rem` is re-rounded every frame, so a pair whose real times differ by a fraction of a
// second spends half its life tied and half its life apart -- and if the tiebreak disagrees with the time
// order, which it does as often as not, the rows trade places twice a second. Reported 2026-09-10, "Ballad
// et Minuet V se battent encore".
//
// Sub-second refinement cannot fix this and made it worse in both directions: comparing across clocks
// follows drift, and refusing to compare falls back on the name. The honest answer is that under a second
// there IS no order to compute, so we stop computing one and remember the last.
//
// Identity is what a person recognises the row by -- its icon, its person, its text -- never its index,
// which shifts whenever the model list is compacted.
#pragma once
#include "model/timers_build.h"   // TimersRow, TM_FINE_NONE, FCLK_*
#include <cstring>

namespace aio {

// A row's identity for the order memory : icon, pass, name and person -- never its index.
inline unsigned timers_row_sig(const TimersRow& x) {
    unsigned h = 2166136261u;
    h = (h ^ (unsigned)x.icon) * 16777619u;
    h = (h ^ (unsigned)x.src) * 16777619u;
    for (const char* c = x.name; c && *c; ++c) h = (h ^ (unsigned char)*c) * 16777619u;
    for (const char* c = x.who;  c && *c; ++c) h = (h ^ (unsigned char)*c) * 16777619u;
    return h ? h : 1u;
}

inline int timers_fine_of(const TimersRow& r) {
    if (r.fine != TM_FINE_NONE) return r.fine;   // exact remaining, in ticks (server expiry / raw recast counter)
    if (r.rem > 30000000 || r.rem < -30000000) return r.rem;   // no sub-second source and out of multiply range (OUT sentinel, absurd timer)
    return r.rem * 60;                           // frozen demo rows : nothing to refine, they never tick
}

// Does x sort AFTER y ? `lastPos(row)` = where that row stood last frame (-1 = it was not on screen) ; it is only
// asked about rows within the hold band, which keeps the search off the common path.
// `mode` picks the PRIMARY key ; everything after it is unchanged, and that matters -- rem before fine is
// what stops the yoyo described above, so a new key may only ever go in FRONT of rem, never between them.
//   Duration 0 : band (person) then soonest      1 : soonest, everyone mixed -- but TRUSTS STILL LAST
//   Recast   0 : soonest                         1 : by name
// Trusts stay last in both duration modes on purpose: they are the rows you are least likely to act on,
// and tmMax cuts the tail, so mixing them in would let a trust's Protect push out one of your own timers.
template <class LastPos>
inline bool timers_row_after(const TimersRow& x, const TimersRow& y, int mode, bool recast, LastPos lastPos) {
    if (recast) {
        if (mode == 1) { const int c = strcmp(x.name ? x.name : "", y.name ? y.name : ""); if (c) return c > 0; }
    } else if (mode == 1) {
        const int tx = (x.order >= 90) ? 1 : 0, ty = (y.order >= 90) ? 1 : 0;   // 90+ = a trust's buff on you
        if (tx != ty) return tx > ty;
    } else if (x.order != y.order) return x.order > y.order;
    // CLOSE ROWS HOLD THE ORDER THEY HAD. Only when BOTH were on screen last frame -- a row that has just
    // appeared has no order to preserve and takes the computed one.
    //
    // The band is 2, not 1, and the reason is the whole bug. Two songs 1.8 s apart show a DISPLAYED gap that
    // alternates between 1 and 2, because each crosses its own second at its own moment. A band of 1 pinned
    // them at a gap of 1 and re-sorted them by time at 2 -- so the oscillation landed exactly on the
    // threshold and flipped the pair twice a second. The guard was doing nothing at all where it mattered.
    // Reported 2026-09-10, "le ballad de kaories continue de faire yoyo".
    if (x.rem > -1000000 && y.rem > -1000000) {
        const int d = x.rem - y.rem;
        if (d >= -2 && d <= 2) {
            const int px = lastPos(x), py = lastPos(y);
            if (px >= 0 && py >= 0 && px != py) return px > py;
        }
    }
    if (x.rem != y.rem) return x.rem > y.rem;
    // Refine by the sub-second ONLY between rows read from the same clock. Across clocks the difference
    // is drift, not order, and following it makes two rows on the same timer trade places for as long as
    // they both live. Rows that disagree fall straight through to the stable tiebreaks below.
    if (x.fineClk == y.fineClk && x.fineClk != FCLK_NONE) {
        const int fx = timers_fine_of(x), fy = timers_fine_of(y);
        if (fx != fy) return fx > fy;
    }
    if (x.icon != y.icon) return x.icon > y.icon;
    // The PERSON is part of the deterministic tiebreak, not just the spell : two allies carrying the same
    // buff at the same second used to differ by their "Aeryn - Haste" / "Gab - Haste" string, and since the
    // split they share one name. Tying here would leave their order to the BUILD order, which moves whenever
    // the model list is compacted -- the yoyo, in its other clothes. (The sort itself is insertion, hence
    // stable ; this only removes the last way two rows can compare equal.)
    { const int cw = strcmp(x.who ? x.who : "", y.who ? y.who : ""); if (cw) return cw > 0; }
    return strcmp(x.name ? x.name : "", y.name ? y.name : "") > 0;
}

} // namespace aio
