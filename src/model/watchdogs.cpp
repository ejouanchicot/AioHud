// watchdogs.cpp -- the master switch. One bool, deliberately in its own file so that every watcher can reach
// it without any of them owning it.
#include "model/watchdogs.h"
#include "windower_debug.h"

namespace aio {

static bool g_on = true;   // see watchdogs.h : on by default, because a diagnostic nobody armed is not there
                           // on the evening it was needed.

bool watch_enabled() { return g_on; }

void watch_enable(bool on) {
    if (g_on == on) return;
    g_on = on;
    // SAY IT. A switch that silently turns the diagnostics off is how a quiet log gets mistaken for a healthy
    // session -- the exact confusion these watchers exist to end.
    windower::debug::log("watchdogs: %s -- flipwatch and capwatch %s observing.",
                         on ? "ON" : "OFF", on ? "are" : "are NOT");
}

} // namespace aio
