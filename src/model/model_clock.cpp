// model_clock.cpp -- see model_clock.h. The in-game implementation.
#include "model/model_clock.h"
#include "model/model_io.h"   // the observer hooks (no-ops in a release build)
#include <windows.h>

namespace aio {

static int      s_depth = 0;
static unsigned s_ms = 0;
static time_t   s_unix = 0;
static bool     s_pinned = false;

unsigned model_now_ms()   { return (s_depth > 0 || s_pinned) ? s_ms : (unsigned)GetTickCount(); }
time_t   model_now_unix() { return (s_depth > 0 || s_pinned) ? s_unix : time(0); }

void model_event_begin(char kind) {
    if (s_depth++ == 0) {
        if (!s_pinned) { s_ms = (unsigned)GetTickCount(); s_unix = time(0); }
        tape_event_begin(kind, s_ms, (long long)s_unix);
    }
}
void model_event_end() {
    if (s_depth > 0 && --s_depth == 0) tape_event_end();
}

void model_clock_pin(unsigned ms, time_t unix) { s_ms = ms; s_unix = unix; s_pinned = true; }

} // namespace aio
