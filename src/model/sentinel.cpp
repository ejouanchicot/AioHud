// sentinel.cpp -- see sentinel.h for WHY this exists and why it never repairs anything.
#include "model/sentinel.h"
#include "model/gamestate.h"
#include "model/party_state.h"
#include "windower_debug.h"
#include <cstring>
#include <cstdio>

namespace aio {

// ---------------------------------------------------------------- tuning ----
//
// These three numbers ARE the difference between an alarm that gets read and one that gets muted. A
// cross-check compares two samples taken at different instants, so a single disagreement means nothing.
//   DELAY   : frames between the packet and the comparison -- the client has to have written its own copy
//             first, and our hook runs before it does.
//   STREAK  : consecutive disagreeing comparisons, each from a DIFFERENT packet, before anything is said.
//   Any single agreement resets the streak to zero. Real breakage does not agree intermittently ; timing
//   noise does, which is exactly the distinction being made here.
static const int CMP_DELAY  = 120;   // ~2 s
static const int CMP_STREAK = 4;

struct Pair {
    const char* name;
    int   agree, disagree, streak;
    bool  diverged;
    char  evidence[120];
};
static Pair g_pair[SEN_N] = {
    { "party member (0x0DD vs member block)", 0, 0, 0, false, {0} },
    { "self buffs (0x063 order 9 vs buff array)", 0, 0, 0, false, {0} },
    { "PointWatch (0x061/0x063 vs static block)", 0, 0, 0, false, {0} },
};

// Latched once, loudly, with BOTH values. Whoever reads the log has to be able to see which side is
// implausible without re-running anything -- naming only "they disagree" would send them back in game.
static void diverge(SentinelPair p, const char* fmt, ...) {
    Pair& q = g_pair[p];
    ++q.disagree;
    if (++q.streak < CMP_STREAK) return;
    char msg[120];
    va_list ap; va_start(ap, fmt);
    wvsprintfA(msg, fmt, ap);
    va_end(ap);
    if (!q.diverged) {
        q.diverged = true;
        strncpy(q.evidence, msg, sizeof(q.evidence) - 1); q.evidence[sizeof(q.evidence) - 1] = 0;
        windower::debug::log("SENTINEL: %s DISAGREE %d times running -- %s", q.name, q.streak, msg);
        windower::debug::log("SENTINEL: this is a LAYOUT change (a field moved inside a struct or a packet), "
                             "not an address that slid -- nothing re-pins it. Do NOT trust what this feeds "
                             "until it is reversed again ; //aio doctor repeats this.");
    }
}
static void agree(SentinelPair p) { ++g_pair[p].agree; g_pair[p].streak = 0; }

// ---------------------------------------------------------------- packet side ----
//
// Values arrive on the packet thread and are compared on the render thread. `due` is written last, and a
// stale or torn sample can only ever cost one comparison -- which the streak rule absorbs by design.

struct MemberSample { unsigned id, mjob, mlvl; char name[20]; int due; bool armed; };
static MemberSample g_mem = { 0, 0, 0, {0}, 0, false };

struct BuffSample { unsigned short ids[32]; int n, due; bool armed; };
static BuffSample g_buf = { {0}, 0, 0, false };

void sentinel_packet_member(unsigned id, const char* name, unsigned mjob, unsigned mlvl) {
    if (!id || !name || !name[0] || !mjob) return;         // an incomplete row proves nothing
    g_mem.id = id; g_mem.mjob = mjob; g_mem.mlvl = mlvl;
    strncpy(g_mem.name, name, sizeof(g_mem.name) - 1); g_mem.name[sizeof(g_mem.name) - 1] = 0;
    g_mem.due = CMP_DELAY;
    g_mem.armed = true;
}

void sentinel_packet_buffs(const unsigned short* ids, int n) {
    if (!ids || n < 2) return;                             // one buff can expire between the two samples ; two cannot both vanish silently
    if (n > 32) n = 32;
    for (int i = 0; i < n; ++i) g_buf.ids[i] = ids[i];
    g_buf.n = n;
    g_buf.due = CMP_DELAY;
    g_buf.armed = true;
}

void sentinel_note_unmatched(SentinelPair p, const char* detail) {
    if (p < 0 || p >= SEN_N) return;
    diverge(p, "%s", detail ? detail : "packet values match nothing in memory");
}

// ---------------------------------------------------------------- comparisons ----

static void cmp_member() {
    if (!g_mem.armed || --g_mem.due > 0) return;
    const unsigned id = g_mem.id, mjob = g_mem.mjob, mlvl = g_mem.mlvl;
    char nm[20]; strncpy(nm, g_mem.name, sizeof(nm) - 1); nm[sizeof(nm) - 1] = 0;
    g_mem.armed = false;

    PMember pm;
    if (!member_identity_from_memory(id, pm)) return;      // left the party / block not ready : silence, not suspicion
    // NAME and JOB only. HP was the obvious thing to compare and would have been the mistake : it changes
    // between the two samples every time you are in combat, so it would disagree constantly and this alarm
    // would be noise within a minute. Identity does not drift.
    // Compare a BOUNDED prefix : the packet copies up to 19 characters and the member block up to 18, so a
    // long enough name would differ by truncation alone and the alarm would fire on nothing. FFXI names cap
    // at 15, so 16 compares the whole thing while staying immune to that asymmetry.
    const bool nameOk = (strncmp(nm, pm.name, 16) == 0);
    const bool jobOk  = ((unsigned)pm.mjob == mjob) && (mlvl == 0 || (unsigned)pm.mlvl == mlvl);
    if (nameOk && jobOk) { agree(SEN_MEMBER); return; }
    diverge(SEN_MEMBER, "packet says '%s' job %u lvl %u, memory says '%s' job %u lvl %u",
            nm, mjob, mlvl, pm.name, (unsigned)pm.mjob, (unsigned)pm.mlvl);
}

static void cmp_buffs(const GameState& gs) {
    if (!g_buf.armed || --g_buf.due > 0) return;
    const int n = g_buf.n;
    unsigned short ids[32];
    for (int i = 0; i < n; ++i) ids[i] = g_buf.ids[i];
    g_buf.armed = false;

    if (!gs.buffsOk) return;                               // the memory read itself failed -- a different problem, already reported
    // OVERLAP, not equality. Buffs expire between the two samples, so demanding identical lists would
    // disagree honestly and constantly. What cannot happen legitimately is the server naming several buffs
    // while memory recognises NONE of them : that is the array having moved, not a buff wearing off.
    int hit = 0;
    for (int i = 0; i < n; ++i)
        for (int k = 0; k < gs.nbuff; ++k)
            if (gs.buffs[k] == ids[i]) { ++hit; break; }
    if (hit > 0) { agree(SEN_BUFFS); return; }
    diverge(SEN_BUFFS, "server lists %d self buff(s) (first id %u), memory recognises none of them (nbuff=%d)",
            n, (unsigned)ids[0], gs.nbuff);
}

void sentinel_tick(const GameState& gs) {
    if (!gs.inGame) return;
    cmp_member();
    cmp_buffs(gs);
}

// ---------------------------------------------------------------- reporting ----

bool        sentinel_diverged(SentinelPair p)    { return (p >= 0 && p < SEN_N) && g_pair[p].diverged; }
const char* sentinel_report_name(SentinelPair p) { return (p >= 0 && p < SEN_N) ? g_pair[p].name : "?"; }

int sentinel_report(char out[][160], int maxOut) {
    int n = 0;
    for (int i = 0; i < SEN_N && n < maxOut; ++i) {
        const Pair& q = g_pair[i];
        if (q.diverged)
            _snprintf(out[n], 160, "%-42s DIVERGED (%d vs %d ok) -- %s", q.name, q.disagree, q.agree, q.evidence);
        else if (q.agree)
            _snprintf(out[n], 160, "%-42s agrees (%d checks)", q.name, q.agree);
        else
            // Said explicitly : "never checked" and "checked and fine" look identical if you only print the
            // good news, and a cross-check that never ran is not reassurance.
            _snprintf(out[n], 160, "%-42s not checked yet (needs the packet that carries it)", q.name);
        out[n][159] = 0; ++n;
    }
    return n;
}

} // namespace aio
