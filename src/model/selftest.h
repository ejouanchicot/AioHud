// selftest.h -- the in-game safety harness: a registry of CHECKS that run on their own, and a bug report
// written the moment one of them holds.
//
// WHY A REGISTRY AND NOT ONE BIG FUNCTION. //aio doctor already answers "is this install healthy" on demand,
// once, for the whole plugin. This answers a different question -- "did something stop behaving while I was
// playing" -- and it has to grow module by module, each module declaring its own checks in its own file, the
// way each already owns its *_config.cpp and its self_check(). A single function that knew every module's
// internals would be a second copy of all of them.
//
// WHAT MAKES A CHECK WORTH WRITING. A check that recomputes the value the same way the code does compares a
// module to itself and can only ever pass. A check earns its place when it has a SECOND, INDEPENDENT source
// (an ally estimate against the server's own timer) or an OUTSIDE bound:
//   - a domain bound      : a status id below 640, a remaining time under any plausible duration
//   - a structural bound  : no duplicate ids, no array at its cap, no entry past its limit
//   - a liveness bound    : something that MUST move has not moved for N seconds
//   - a presence bound    : a module that should have rows has had none for N minutes
// Anything of the shape "the row says what the model says" belongs in a unit test, not here.
//
// NOISE IS THE FAILURE MODE. Hundreds of checks that cry wolf bury the one that matters, and a report nobody
// reads is worse than none -- so a check must hold for several consecutive passes before it is believed, and
// each one reports ONCE per session with a repeat count. A check that fires wrongly gets deleted, not tuned.
#pragma once

namespace aio {

enum CheckSeverity {
    CHK_INFO = 0,   // worth knowing, nothing is broken
    CHK_WARN,       // a feature is degraded or silently doing nothing
    CHK_BLOCK       // this module cannot work in its current state
};

// One failing check. `id` is STABLE and namespaced by module ("TM.FOCUS_FULL") -- it is what a report names
// and what I grep for, so renaming one loses the thread back to its history. `detail` carries the VALUES that
// failed, never a restatement of the id: "24 of 24 slots used, oldest 41 min" is actionable, "the focus list
// is full" is not.
struct CheckFail {
    char id[40];
    int  sev;
    char detail[192];
};

// A module fills `out` with its failures and returns how many. Called from the watcher, off the render thread's
// normal path but on its timing -- so it must not read game memory, allocate, or block.
typedef int (*CheckFn)(CheckFail* out, int cap);

static const int SELFTEST_MODULES_MAX = 16;
static const int SELFTEST_FAILS_MAX   = 32;

void selftest_add(const char* module, CheckFn fn);   // called once per module at init
int  selftest_module_count();

// Run every registered check now, whatever the watcher thinks. `//aio selftest` uses this: it reports what is
// failing AT THIS INSTANT, with no debounce, which is what you want when you are looking at the bug.
int  selftest_run_now(CheckFail* out, int cap);

// The watcher. Call every frame; it decides when to actually run (SELFTEST_POLL_MS) and applies the debounce.
// Returns the number of failures that just became CONFIRMED (held long enough, not yet reported) and fills
// `out` with them -- 0 the rest of the time, which is the normal case and costs nothing.
int  selftest_tick(unsigned nowMs, CheckFail* out, int cap);

// Write the report. `header` is the caller's context block (version, zone, job, paths, config deltas) -- the
// watcher owns WHEN, the caller owns WHAT, because the context lives in ui and this file must not reach into
// it. Returns the path written, or 0. The file sits next to AioHud.dll, beside aiohud_debug.log, for the same
// reason: the plugins folder is writable even on a Program Files install.
// `watched` = these findings came from the WATCHER, having each held SELFTEST_CONFIRM runs. False = someone
// asked for a report now, so the findings are a single instant with no debounce behind them. The file says
// which, because a report that overstates its own evidence sends whoever reads it down a false trail -- the
// first one ever produced claimed a finding had held three runs when it had been requested by hand.
const char* selftest_write_report(const char* header, const CheckFail* fails, int n, bool watched);

// The watcher is OFF unless armed (ui_config().selfTest). Dormant in the product, on for whoever is hunting.
bool selftest_armed();
void selftest_arm(bool on);

// A report was written and nobody has been told yet -> the leaf file name, else 0, and taking it clears it.
// The plugin layer drains this and says it in the chat log: a report written where nobody looks is the same
// as no report, and this is the one moment the plugin knows something is wrong while the person who can act
// on it is sitting right there. It is a HANDOFF rather than a call because ui/ and model/ must not reach
// into the host (CLAUDE.md dependency rule) -- only src/plugin owns add_to_chat.
const char* selftest_take_notice();

} // namespace aio
