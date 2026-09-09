// selftest.cpp -- see selftest.h. Registry, debounce, and the report file.
#include "model/selftest.h"
#include "model/ui_config.h"
#include "model/paths.h"   // plugin_path : the config lives beside the report
#include "windower_debug.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>

namespace aio {

// How often the watcher actually runs its checks, and how many consecutive runs a failure must survive before
// it is believed. 30 s x 3 = about a minute and a half of a condition holding -- comfortably longer than the
// window right after a zone-in, which is when half of everything is legitimately not ready yet and where a
// twitchier watcher would spend its life reporting the plugin's own start-up.
static const unsigned SELFTEST_POLL_MS = 30000;
static const int      SELFTEST_CONFIRM = 3;

static const char* g_modName[SELFTEST_MODULES_MAX];
static CheckFn     g_modFn[SELFTEST_MODULES_MAX];
static int         g_modN = 0;

void selftest_add(const char* module, CheckFn fn) {
    if (!fn || g_modN >= SELFTEST_MODULES_MAX) return;
    for (int i = 0; i < g_modN; ++i) if (g_modFn[i] == fn) return;   // idempotent : registration may be re-run on a reload
    g_modName[g_modN] = module ? module : "?";
    g_modFn[g_modN]   = fn;
    ++g_modN;
}
int selftest_module_count() { return g_modN; }

int selftest_run_now(CheckFail* out, int cap) {
    if (!out || cap <= 0) return 0;
    int n = 0;
    for (int i = 0; i < g_modN && n < cap; ++i) {
        const int got = g_modFn[i](out + n, cap - n);
        if (got > 0) n += got;
    }
    return n;
}

// ---- the debounce ------------------------------------------------------------------------------------------
// One row per check id ever seen. `streak` is how many consecutive runs it has failed -- reset to 0 the moment
// it passes, because a condition that comes and goes is a different animal from one that holds, and only the
// second is worth a file. `reported` makes it once-per-session; `repeats` counts the rest, so a report can say
// "still failing, 47 runs later" without writing 47 files.
struct Seen { char id[40]; int streak; bool reported; unsigned repeats; };
static Seen g_seen[SELFTEST_FAILS_MAX * 2];
static int  g_seenN = 0;

static Seen* seen_for(const char* id) {
    for (int i = 0; i < g_seenN; ++i) if (lstrcmpA(g_seen[i].id, id) == 0) return &g_seen[i];
    if (g_seenN >= (int)(sizeof(g_seen) / sizeof(g_seen[0]))) return 0;
    Seen& s = g_seen[g_seenN++];
    lstrcpynA(s.id, id, sizeof(s.id));
    s.streak = 0; s.reported = false; s.repeats = 0;
    return &s;
}

int selftest_tick(unsigned nowMs, CheckFail* out, int cap) {
    if (!selftest_armed() || !out || cap <= 0) return 0;

    static unsigned nextMs = 0;
    if (nextMs && (int)(nowMs - nextMs) < 0) return 0;
    nextMs = nowMs + SELFTEST_POLL_MS;

    CheckFail now[SELFTEST_FAILS_MAX];
    const int nf = selftest_run_now(now, SELFTEST_FAILS_MAX);

    // Anything that did NOT fail this run loses its streak. Done first, and by absence, so a check that stops
    // failing is genuinely forgotten rather than left one run away from a report for the rest of the session.
    for (int i = 0; i < g_seenN; ++i) {
        bool still = false;
        for (int k = 0; k < nf; ++k) if (lstrcmpA(g_seen[i].id, now[k].id) == 0) { still = true; break; }
        if (!still) g_seen[i].streak = 0;
    }

    int fresh = 0;
    for (int k = 0; k < nf; ++k) {
        Seen* s = seen_for(now[k].id);
        if (!s) continue;
        ++s->streak;
        if (s->reported) { ++s->repeats; continue; }
        if (s->streak < SELFTEST_CONFIRM) continue;
        s->reported = true;
        if (fresh < cap) out[fresh++] = now[k];
    }
    return fresh;
}

// The un-said notice. One slot: a second report before the first is spoken overwrites the name, which is
// right -- the chat line exists to make the player LOOK in the folder, and the folder holds both files.
static char g_notice[64] = { 0 };

// ---- the report --------------------------------------------------------------------------------------------
// Next to AioHud.dll, like aiohud_debug.log and for the same reason (the game's CWD is unwritable on a Program
// Files install, silently). One file per report, named by the clock, so a second problem never overwrites the
// evidence for the first.
static const char* report_path(char* buf, int cap) {
    HMODULE hm = NULL;
    SYSTEMTIME t; GetLocalTime(&t);
    char name[64];
    _snprintf(name, sizeof(name), "aiohud_bugreport_%04d%02d%02d_%02d%02d%02d.txt",
              t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    name[sizeof(name) - 1] = 0;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)&report_path, &hm) && hm && GetModuleFileNameA(hm, buf, cap)) {
        char* b = buf; for (char* q = buf; *q; ++q) if (*q == '\\' || *q == '/') b = q + 1;
        lstrcpynA(b, name, (int)(cap - (b - buf)));
    } else {
        lstrcpynA(buf, name, cap);
    }
    return buf;
}

static const char* sev_word(int s) { return s == CHK_BLOCK ? "BLOCK" : s == CHK_WARN ? "WARN " : "INFO "; }

const char* selftest_write_report(const char* header, const CheckFail* fails, int n, bool watched) {
    static char path[MAX_PATH];
    report_path(path, MAX_PATH);
    HANDLE h = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        windower::debug::log("selftest: could not write %s (err=%lu)", path, (unsigned long)GetLastError());
        return 0;
    }
    char buf[1024]; DWORD w = 0;
    #define RPT(...) do { const int L = _snprintf(buf, sizeof(buf), __VA_ARGS__); \
                          if (L > 0) WriteFile(h, buf, (DWORD)L, &w, NULL); } while (0)
    RPT("=== AioHUD bug report ===\r\n\r\n");
    if (header && header[0]) { WriteFile(h, header, (DWORD)lstrlenA(header), &w, NULL); RPT("\r\n"); }
    if (watched) RPT("--- checks that failed, each having held %d consecutive runs %d s apart ---\r\n",
                     SELFTEST_CONFIRM, (int)(SELFTEST_POLL_MS / 1000));
    else         RPT("--- checks failing at the instant this report was REQUESTED (no debounce) ---\r\n");
    for (int i = 0; i < n; ++i)
        RPT("  [%s] %-24s %s\r\n", sev_word(fails[i].sev), fails[i].id, fails[i].detail);
    // THE CONFIGURATION, INLINE. Not a path to it: a report is read on a machine that does not have the file,
    // and "it does not do that here" is the usual end of a bug that could not be reproduced. Settings are the
    // cheapest half of a repro and the half most often missing.
    RPT("\r\n--- configuration (plugins\\AioHud\\data\\config.txt) ---\r\n");
    {
        char cfg[260]; plugin_path(cfg, sizeof(cfg), "data\\config.txt");
        HANDLE c = CreateFileA(cfg, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (c == INVALID_HANDLE_VALUE) { RPT("  (unreadable: %s)\r\n", cfg); }
        else {
            char chunk[4096]; DWORD got = 0;
            while (ReadFile(c, chunk, sizeof(chunk), &got, NULL) && got) WriteFile(h, chunk, got, &w, NULL);
            CloseHandle(c);
        }
    }
    RPT("\r\n--- what to do with this file ---\r\n"
        "Give it to whoever maintains AioHUD, with aiohud_debug.log from the same session.\r\n"
        "Every check id above is stable and greppable in the source.\r\n");
    #undef RPT
    CloseHandle(h);
    windower::debug::log("selftest: wrote %s (%d finding(s))", path, n);
    { const char* leaf = path; for (const char* q = path; *q; ++q) if (*q == '\\' || *q == '/') leaf = q + 1;
      lstrcpynA(g_notice, leaf, sizeof(g_notice)); }
    return path;
}

const char* selftest_take_notice() {
    if (!g_notice[0]) return 0;
    static char out[64]; lstrcpynA(out, g_notice, sizeof(out)); g_notice[0] = 0;
    return out;
}

bool selftest_armed() { return ui_config().selfTest != 0; }
void selftest_arm(bool on) { ui_config().selfTest = on ? 1 : 0; }

} // namespace aio
