// capwatch.cpp -- the registry and the watcher's report. The decision is in capwatch.h, pure and covered by
// tests/t_capwatch.cpp.
#include "model/capwatch.h"
#include "model/selftest.h"
#include "windower_debug.h"
#include <windows.h>
#include <cstdio>

namespace aio {

static CapSlot g_slots[CAP_SLOTS];
static int     g_n = 0;

CapSlot* cap_slots(int& n) { n = g_n; return g_slots; }

int capwatch(const char* id, int used, int cap) {
    if (!id || !watch_enabled()) return CAPV_OK;
    CapSlot* s = 0;
    for (int i = 0; i < g_n; ++i) if (g_slots[i].id == id) { s = &g_slots[i]; break; }
    if (!s) {
        // FULL IS NOT SILENT -- a watcher for silent overflows must not silently overflow.
        if (g_n >= CAP_SLOTS) {
            static windower::debug::LogOnce<2> onceFull;
            if (onceFull.first(0))
                windower::debug::log("capwatch: all %d slots in use -- '%s' and anything after it are NOT watched", CAP_SLOTS, id);
            return CAPV_OK;
        }
        s = &g_slots[g_n++];
        s->id = id; cap_reset(s->st);
    }
    const int v = cap_feed(s->st, used, cap, CAP_HOLD, CAP_NEAR_N);
    if (v == CAPV_FULL)
        windower::debug::log("capwatch: '%s' is SATURATED -- %d of %d slots used, and it has stayed there. "
                             "Whatever did not fit is being dropped silently, every frame.", id, s->st.used, s->st.cap);
    else if (v == CAPV_NEAR)
        windower::debug::log("capwatch: '%s' is nearly full -- %d of %d used (peak %d). Raise the cap before it "
                             "overflows, not after.", id, s->st.used, s->st.cap, s->st.hi);
    return v;
}

// ---- the in-game watcher -------------------------------------------------------------------------------
// Saturation is a WARNING, never a block: the module keeps working, it is just incomplete -- which is exactly
// what makes it dangerous. A blocked module is noticed in seconds ; a module quietly showing 24 of 31 songs
// looks right, and the missing seven are the ones you needed.
static int cap_checks(CheckFail* out, int cap) {
    int n = 0;
    for (int i = 0; i < g_n && n < cap; ++i) {
        const CapState& c = g_slots[i].st;
        if (!c.saidFull) continue;                      // nearly-full is a log line, not a report : it is advice
        lstrcpynA(out[n].id, "CAP.SATURATED", sizeof(out[n].id));
        out[n].sev = CHK_WARN;
        _snprintf(out[n].detail, sizeof(out[n].detail),
                  "'%s' filled its %d slots (peak %d) and stayed there -- entries past the cap are dropped "
                  "without a word, so this module is showing you an incomplete list",
                  g_slots[i].id, c.cap, c.hi);
        out[n].detail[sizeof(out[n].detail) - 1] = 0;
        ++n;
    }
    return n;
}

void cap_register_checks() { selftest_add("tables", cap_checks); }

} // namespace aio
