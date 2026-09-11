// flipwatch.h -- notice when a decision cannot make up its mind.
//
// WHY THIS EXISTS. Four separate bugs on 2026-09-10/11 had one shape: two rules of equal standing over one
// value, each undoing the other, frame after frame.
//
//   - two song rows on the same countdown traded places twice a second ;
//   - a red "lost" row appeared and vanished at 60 Hz, because the rule that silenced it FREED the monitor
//     entry and the seeding loop re-created it ;
//   - two RVA healers re-proposed the same address to each other, for ever ;
//   - a refuted decoy address was re-confirmed by the weaker test it had just overturned.
//
// Each cost hours, and each was obvious the moment somebody looked at consecutive frames instead of one.
// None of them is hard to SEE -- a value that alternates A,B,A,B,A,B is not a value in motion, it is an
// argument. This watches for that, and says so once.
//
// WHAT IT IS NOT. It does not know which rule is wrong, and it must not guess: it reports that a decision
// is oscillating and names it, which is the part a person cannot get from a log of results. Nor does it fire
// on ordinary change -- a countdown, a growing list, a cursor moving -- because those never come back to the
// value they just left, over and over, without ever settling.
#pragma once
#include "model/watchdogs.h"   // every threshold below lives there, with the master switch

namespace aio {

// ---- the decision, pure and testable -------------------------------------------------------------------
struct FlipState {
    unsigned a, b, last;   // the two values being argued over, and the most recent
    int      flips;        // consecutive A<->B alternations
    int      steady;       // frames the value has held still
    unsigned char armed;   // `last` means something
    unsigned char tripped; // already reported : say it once, never every frame
};

inline void flip_reset(FlipState& s) {
    s.a = s.b = s.last = 0; s.flips = 0; s.steady = 0; s.armed = 0; s.tripped = 0;
}

// Feed one sample. Returns true EXACTLY ONCE, on the call that crosses `trip`.
//
// `steadyClear` is what separates an argument from a change: a value that holds still for a while has
// settled, whatever it did before, and its history stops counting against it. Without that, a row set that
// legitimately alternates twice an hour would eventually be accused of oscillating.
inline bool flip_feed(FlipState& s, unsigned v, int trip, int steadyClear) {
    if (!s.armed) { s.last = v; s.armed = 1; return false; }

    if (v == s.last) {
        if (++s.steady >= steadyClear) { s.flips = 0; s.a = 0; s.b = 0; s.steady = 0; }
        return false;
    }
    s.steady = 0;

    // Back to the value we left, and the one before that? Then nothing is progressing.
    const bool pair = (v == s.a && s.last == s.b) || (v == s.b && s.last == s.a);
    if (pair) ++s.flips;
    else { s.a = s.last; s.b = v; s.flips = 1; }   // a new pair : start counting this argument
    s.last = v;

    if (!s.tripped && s.flips >= trip) { s.tripped = 1; return true; }
    return false;
}

// ---- the registry --------------------------------------------------------------------------------------
// Fixed slots, keyed by the literal's ADDRESS -- callers pass string literals, so the comparison is a
// pointer test and the hot path is a handful of integer operations. No allocation, nothing per frame.
// FLIP_SLOTS / FLIP_TRIP / FLIP_STEADY are in watchdogs.h, with the other watchers' knobs and the reasoning
// for each number -- one file to open when one of these turns out to be wrong.

struct FlipSlot { const char* id; FlipState st; unsigned atMs; };

FlipSlot* flip_slots(int& n);                  // the live table, for the watcher's check
bool      flipwatch(const char* id, unsigned value, unsigned nowMs);   // true on the frame it trips
void      flip_register_checks();              // report oscillating decisions to the in-game watcher

} // namespace aio
