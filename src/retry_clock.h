// retry_clock.h -- the ONE correct way to schedule "try this again later" in this project.
//
// WHY THIS FILE EXISTS. The same four-line idiom was hand-written at half a dozen sites, and half of them got
// it wrong in the SAME way:
//
//     static unsigned nextMs = 0;
//     if ((int)(now - nextMs) < 0) return;     // <-- WRONG once uptime passes 24.8 days
//
// `nextMs == 0` is the "never scheduled, try NOW" sentinel. Comparing a raw GetTickCount() against 0 computes
// `(int)now`, which goes NEGATIVE the moment uptime passes 24.8 days (0x7FFFFFFF ms) -- so the guard returns
// early FOREVER and the retry never fires. It killed the buff atlas, then the gear-icon back-off, and the
// 2026-07-26 audit found it twice more (`map_dat.cpp` map tables, `ui_config.cpp` profile sync). Five
// occurrences of one mistake is not five bugs, it is a missing abstraction. `tex_retry.h` documented the trap
// in prose next to a correct copy; prose does not stop the sixth site from getting it wrong.
//
// WHERE IT LIVES. At the root of src/, deliberately outside every layer. The callers are in `model`
// (map_dat, ui_config), `gfx` (font registration) and `ui` (texture retries), and the dependency rule forbids
// `model` from including `gfx` -- so no existing layer can host it for all of them. This header depends on
// nothing but <windows.h>, so including it violates no direction.
//
// USE. Store an `unsigned nextMs = 0` next to whatever you are retrying:
//     if (!retry_due(nextMs)) return;          // not yet -- and correct at ANY uptime
//     ... attempt ...
//     if (failed) retry_arm(nextMs, 300);      // try again in ~300 ms
// A BUDGET is a separate concern: count attempts yourself. And remember the other half of rule 10 -- a budget
// must plateau (slow down), not terminate, unless the cause genuinely cannot change during the session.
#pragma once
#include <windows.h>

namespace aio {

// Is a retry allowed now ? True when never scheduled (nextMs == 0) or when the deadline has passed.
// The `!nextMs` test comes FIRST : that is the whole point, it removes the 0-sentinel from the arithmetic.
inline bool retry_due(unsigned nextMs) {
    return !nextMs || (int)(GetTickCount() - nextMs) >= 0;
}

// Schedule the next attempt `delayMs` from now. `| 1` keeps the stamp off the 0 "try now" sentinel, so a
// deadline that legitimately lands on tick 0 is not read as "never scheduled".
inline void retry_arm(unsigned& nextMs, unsigned delayMs) {
    nextMs = (GetTickCount() + delayMs) | 1u;
}

} // namespace aio
