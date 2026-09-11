// capwatch.h -- notice a fixed table that has run out of room.
//
// WHY THIS EXISTS. This codebase allocates nothing per frame (CLAUDE.md), so every list is a fixed array with
// a cap, and every one of them ends in the same line: `if (n < MAX) arr[n++] = x;`. That is the right shape --
// but it means running out of room is COMPLETELY SILENT. The data that did not fit is simply not there, the
// display looks plausible, and nothing anywhere says a thing.
//
// It has already cost, twice, on 2026-09-10/11 alone : the Timers focus list held 24 entries and a bard's own
// songs plus an alliance's buffs went past it, so alerts about real losses were never raised ; the ally-buff
// table held 32 and silently truncated a full alliance. Both were found by reading code at 1am while chasing
// something else. Both caps were raised (64 and 128) -- and neither would have been noticed a day earlier,
// because a full table and an empty one look identical from outside.
//
// WHAT IT DOES. `capwatch("id", used, cap)`, once a frame, wherever the count is known. It says two things:
//   - NEARLY FULL, once, with the high-water mark : the cap can be raised BEFORE the evening it overflows.
//   - SATURATED, once, after it has held at the cap long enough to not be a zone-in blip : data is being
//     dropped right now, every frame, and this is the only place it will ever be said.
// It never touches the table and never changes behaviour. It is a witness.
#pragma once
#include "model/watchdogs.h"

namespace aio {

// ---- the decision, pure and testable -------------------------------------------------------------------
enum CapVerdict {
    CAPV_OK = 0,   // nothing to say -- the overwhelming majority of calls
    CAPV_NEAR,     // within CAP_NEAR_N of the cap for the first time this session
    CAPV_FULL      // at the cap, and has stayed there : it IS dropping data
};

struct CapState {
    int  used, cap, hi;    // the latest counts, and the most this table has ever held
    int  atCap;            // consecutive samples at the cap
    unsigned char saidNear, saidFull;   // each verdict is delivered ONCE : a warning per frame is noise
};

inline void cap_reset(CapState& s) { s.used = s.cap = s.hi = 0; s.atCap = 0; s.saidNear = 0; s.saidFull = 0; }

// Feed one sample. Returns non-OK at most twice in a table's life -- once when it first gets close, once when
// it has actually been saturated for `hold` samples.
inline int cap_feed(CapState& s, int used, int cap, int hold, int nearN) {
    if (cap <= 0) return CAPV_OK;                 // a table with no capacity is a caller bug, not a finding
    if (used < 0) used = 0;
    s.used = used; s.cap = cap;
    if (used > s.hi) s.hi = used;                 // THE PEAK IS THE POINT. A table that filled during a zone-in
                                                  // and drained is invisible to a sample taken afterwards, and
                                                  // that is exactly when these tables overflow.

    // SATURATION. Held at the cap, not merely touching it : every one of these tables legitimately brushes its
    // cap for a frame or two when a zone-in delivers a whole alliance at once, and accusing that would make the
    // watcher cry wolf on the single most ordinary event in the game.
    if (used >= cap) {
        if (++s.atCap >= hold && !s.saidFull) { s.saidFull = 1; return CAPV_FULL; }
    } else {
        s.atCap = 0;
    }

    // NEARLY FULL. Said as soon as it happens, because the value of this one is entirely in arriving EARLY --
    // days before the overflow, while raising the cap is a one-line change and not a bug hunt.
    if (!s.saidNear && used > 0 && used >= cap - nearN) { s.saidNear = 1; return CAPV_NEAR; }
    return CAPV_OK;
}

// ---- the registry --------------------------------------------------------------------------------------
// Same shape as flipwatch : fixed slots keyed by the string literal's ADDRESS, so the lookup is a pointer
// compare and the hot path costs a handful of integer operations.
struct CapSlot { const char* id; CapState st; };

CapSlot* cap_slots(int& n);                                    // the live table, for the watcher's check
int      capwatch(const char* id, int used, int cap);          // returns the CapVerdict, and logs it
void      cap_register_checks();                               // report saturated tables to //aio doctor

} // namespace aio
