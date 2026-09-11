// flipwatch.cpp -- the registry and the watcher's report. The decision itself is in flipwatch.h, pure and
// covered by tests/t_flipwatch.cpp.
#include "model/flipwatch.h"
#include "model/selftest.h"
#include "windower_debug.h"
#include <windows.h>
#include <cstdio>

namespace aio {

static FlipSlot g_slots[FLIP_SLOTS];
static int      g_n = 0;

FlipSlot* flip_slots(int& n) { n = g_n; return g_slots; }

bool flipwatch(const char* id, unsigned value, unsigned nowMs) {
    if (!id) return false;
    FlipSlot* s = 0;
    for (int i = 0; i < g_n; ++i) if (g_slots[i].id == id) { s = &g_slots[i]; break; }
    if (!s) {
        // FULL IS NOT SILENT. A watcher that quietly stops watching is the exact failure it exists to catch,
        // so say it once and carry on rather than pretend everything is being observed.
        if (g_n >= FLIP_SLOTS) {
            static windower::debug::LogOnce<2> onceFull;
            if (onceFull.first(0))
                windower::debug::log("flipwatch: all %d slots in use -- '%s' and anything after it are NOT watched", FLIP_SLOTS, id);
            return false;
        }
        s = &g_slots[g_n++];
        s->id = id; flip_reset(s->st); s->atMs = 0;
    }
    if (!flip_feed(s->st, value, FLIP_TRIP, FLIP_STEADY)) return false;
    s->atMs = nowMs;
    windower::debug::log("flipwatch: '%s' is OSCILLATING -- it has alternated between %u and %u %d times "
                         "without ever settling. Two rules are fighting over one value.",
                         id, s->st.a, s->st.b, s->st.flips);
    return true;
}

// ---- the in-game watcher -------------------------------------------------------------------------------
// Reported as a WARNING rather than a block: an oscillation is rarely fatal on its own, but it is ALWAYS a
// defect -- a value that cannot settle is two pieces of code disagreeing, and the display follows whichever
// spoke last. Every one of the four that prompted this was visible on screen as a shimmer, and each cost
// hours precisely because a single frame looks fine.
static int flip_checks(CheckFail* out, int cap) {
    int n = 0;
    for (int i = 0; i < g_n && n < cap; ++i) {
        if (!g_slots[i].st.tripped) continue;
        lstrcpynA(out[n].id, "FLIP.DECISION", sizeof(out[n].id));
        out[n].sev = CHK_WARN;
        // WHEN matters as much as WHAT. An oscillation caught at login is usually a healer still settling;
        // one that started thirty seconds ago is the thing on screen right now.
        const unsigned ago = (unsigned)(GetTickCount() - g_slots[i].atMs) / 1000u;
        _snprintf(out[n].detail, sizeof(out[n].detail),
                  "'%s' alternated between %u and %u without settling (%us ago) -- two rules are deciding one "
                  "value, and the screen follows whichever ran last",
                  g_slots[i].id, g_slots[i].st.a, g_slots[i].st.b, ago);
        out[n].detail[sizeof(out[n].detail) - 1] = 0;
        ++n;
    }
    return n;
}

void flip_register_checks() { selftest_add("flips", flip_checks); }

} // namespace aio
