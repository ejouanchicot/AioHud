// decisions.cpp -- the ring. See decisions.h for why this is not another armed trace.
#include "model/decisions.h"
#include "windower_debug.h"
#include <windows.h>
#include <windowsx.h>
#include <cstdio>
#include <cstdarg>

namespace aio {

static DecLine g_ring[DEC_RING];
static int     g_head = 0;     // next slot to write
static int     g_used = 0;

// ---- keeping the recorder from becoming the bug ---------------------------------------------------------
// A topic wired into a per-frame path would overwrite the whole ring every four seconds AND format 60 strings
// a second in the render thread. That is a worse defect than most of the ones this exists to find, so it is
// detected rather than trusted: a topic recording faster than DEC_RATE_MAX per second is named once and then
// throttled, instead of silently spending the budget.
struct Rate { const char* topic; unsigned windowMs; int n; unsigned char scolded; };
static Rate g_rate[8];
static int  g_rateN = 0;

static bool rate_ok(const char* topic, unsigned now) {
    Rate* r = 0;
    for (int i = 0; i < g_rateN; ++i) if (g_rate[i].topic == topic) { r = &g_rate[i]; break; }
    if (!r) {
        if (g_rateN >= 8) return true;          // more topics than we track : do not throttle what we cannot measure
        r = &g_rate[g_rateN++];
        r->topic = topic; r->windowMs = now; r->n = 0; r->scolded = 0;
    }
    if (now - r->windowMs >= 1000u) { r->windowMs = now; r->n = 0; }
    if (++r->n <= DEC_RATE_MAX) return true;
    if (!r->scolded) {
        r->scolded = 1;
        windower::debug::log("decisions: topic '%s' recorded more than %d lines in one second -- that call site is "
                             "in a per-frame path, not on a decision. Throttled ; move it or the ring is useless.",
                             topic, DEC_RATE_MAX);
    }
    return false;
}

void dec_record(const char* topic, const char* fmt, ...) {
    if (!topic || !fmt || !watch_enabled()) return;
    const unsigned now = (unsigned)GetTickCount();
    if (!rate_ok(topic, now)) return;

    DecLine& d = g_ring[g_head];
    d.ms = now; d.topic = topic;
    va_list ap; va_start(ap, fmt);
    _vsnprintf(d.text, DEC_LINE, fmt, ap);
    va_end(ap);
    d.text[DEC_LINE - 1] = 0;

    g_head = (g_head + 1) % DEC_RING;
    if (g_used < DEC_RING) ++g_used;
}

const DecLine* dec_ring(int& n, int& oldest) {
    n = g_used;
    oldest = (g_used < DEC_RING) ? 0 : g_head;
    return g_ring;
}

int dec_dump(const char* topic) {
    const unsigned now = (unsigned)GetTickCount();
    int n = 0, oldest = 0;
    const DecLine* ring = dec_ring(n, oldest);
    windower::debug::log("=== AIO DECISIONS : %d line(s)%s%s, oldest first ===",
                         n, topic ? ", topic " : "", topic ? topic : "");
    int written = 0;
    for (int i = 0; i < n; ++i) {
        const DecLine& d = ring[(oldest + i) % DEC_RING];
        if (topic && d.topic != topic) continue;
        // The AGE is the part that matters. An absolute tick count means nothing to whoever reads the log ;
        // "3.4 s ago" lines the record up against what they just watched happen on screen.
        const unsigned ago = now - d.ms;
        windower::debug::log("  -%2u.%03us  [%s] %s", ago / 1000u, ago % 1000u, d.topic, d.text);
        ++written;
    }
    if (!written) windower::debug::log("  (nothing recorded%s -- either it did not happen, or no call site covers it)",
                                       topic ? " under that topic" : "");
    windower::debug::log("=== end AIO DECISIONS ===");
    return written;
}

} // namespace aio
