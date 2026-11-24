// aio_verbs.h -- the //aio commands : ONE table of command words, matched EXACTLY on the first word.
//
// WHY. The dispatcher was a chain of ~60 `if (strstr(buf, "word"))`, tried in order : a command fired if its word
// appeared ANYWHERE in the line. "//aio pop whyrm" could run `why`, an argument holding "on" or "off" reached whatever
// tested for it first, "//aio reset" ran `res`, and a dev probe named like a shipped command silently took it over
// (the 2026-09-14 and 2026-11 audits each found one). A dozen verbs had already moved to an exact first-word match
// (aio_verb) one incident at a time ; this table finishes the job for all of them.
//
// THE TABLE IS ALSO THE LIST OF WHAT SHIPS. SHIPPED = compiled in every build, the release included ; DEV = only in a
// build with the local dev tree (probes, devtools, theme kits), absent from a release. scripts/shipped_commands.py
// prints the shipped words from here and checks .claude/rules/shipped-commands.md against them -- the rule says a
// remedy a message names must be a shipped command, and this is where "shipped" is now written down, once.
//
// Pure (no Windows, no Windower) : tests/t_verbs.cpp checks the matching and the table itself.
#pragma once
#include <string.h>

namespace aio { namespace verbs {

enum Id {   // V_ : windows.h defines IN and OUT as (empty) macros
    NONE = -1,
    V_HEAP, V_VMEM, V_OUT, V_IN, V_RVA, V_DEVTOOL, V_PERF, V_HELP, V_REPORT, V_BCAPTLOG, V_SCLOG, V_SCDELAY, V_PROFILE, V_PARTY, V_EPT, V_MENU, V_SIM,
    V_UPDATE, V_KEYLOG, V_FTRACE, V_OBLOG, V_UNITSLOG, V_SONGTAPE, V_SONGROW, V_SONGDUR, V_CHAT, V_SCHLOG, V_CORNERS, V_DOCTOR, V_SELFTEST, V_WHY,
    V_WATCH, V_SELFCHECK, V_SHEOLTEST, V_KITTEST, V_EQPREVIEW, V_GEARTRACE, V_RANGELOG, V_FACING, V_SORTIELOG, V_DBFLOG, V_OMENPARSE,
    V_OMENSTATE, V_TPOOL, V_TMEM, V_CONFIG, V_TIMERS, V_EDIT, V_LAYOUT, V_RES, V_LAY,
    COUNT
};
enum Flag { SHIPPED = 1, DEV = 2 };
struct Spec { const char* word; int id; int flags; };

// One row per WORD (an alias is a second row with the same id). Lower case, no spaces : the line is lower-cased first.
static const Spec TABLE[] = {
    { "heap",      V_HEAP,      DEV },       // dev : every module's heap blocks born across a test
    { "vmem",      V_VMEM,      SHIPPED },
    { "out",       V_OUT,       SHIPPED },
    { "in",        V_IN,        SHIPPED },
    { "rva",       V_RVA,       SHIPPED },
    { "pcap",      V_DEVTOOL,   DEV },       // dev tools (dev/src) : they parse the line themselves
    { "igstate",   V_DEVTOOL,   DEV },
    { "mmset",     V_DEVTOOL,   DEV },       // dev : one minimap option by name (the option sweep)
    { "jobtab",    V_DEVTOOL,   DEV },
    { "gearskew",  V_DEVTOOL,   DEV },
    { "perf",      V_PERF,      SHIPPED },
    { "help",      V_HELP,      SHIPPED },
    { "report",    V_REPORT,    SHIPPED },
    { "bcaptlog",  V_BCAPTLOG,  SHIPPED },
    { "sclog",     V_SCLOG,     SHIPPED },
    { "scdelay",   V_SCDELAY,   SHIPPED },
    { "profile",   V_PROFILE,   SHIPPED },
    { "party",     V_PARTY,     SHIPPED },   // //aio party N | off ; a bare "party" is a dev dump (falls through)
    { "demo",      V_PARTY,     SHIPPED },   // legacy : "demo", "demo off"
    { "alliance1", V_PARTY,     SHIPPED },   // legacy : "alliance1 demo"
    { "alliance2", V_PARTY,     SHIPPED },
    { "ept",       V_EPT,       SHIPPED },   // EmpyPop
    { "pop",       V_EPT,       SHIPPED },   //   (alias)
    { "menu",      V_MENU,      SHIPPED },   // //aio menu N : window theme
    { "sim",       V_SIM,       DEV },
    { "update",    V_UPDATE,    SHIPPED },
    { "keylog",    V_KEYLOG,    SHIPPED },
    { "ftrace",    V_FTRACE,    SHIPPED },
    { "oblog",     V_OBLOG,     SHIPPED },
    { "unitslog",  V_UNITSLOG,  SHIPPED },
    { "songtape",  V_SONGTAPE,  SHIPPED },
    { "songrow",   V_SONGROW,   SHIPPED },
    { "songdur",   V_SONGDUR,   SHIPPED },
    { "chat",      V_CHAT,      SHIPPED },   // //aio chat export [all]
    { "schlog",    V_SCHLOG,    SHIPPED },
    { "corners",   V_CORNERS,   DEV },
    { "doctor",    V_DOCTOR,    SHIPPED },
    { "selftest",  V_SELFTEST,  SHIPPED },
    { "why",       V_WHY,       SHIPPED },
    { "watch",     V_WATCH,     SHIPPED },
    { "selfcheck", V_SELFCHECK, SHIPPED },
    { "sheoltest", V_SHEOLTEST, SHIPPED },
    { "kittest",   V_KITTEST,   DEV },
    { "eqpreview", V_EQPREVIEW, SHIPPED },
    { "geartrace", V_GEARTRACE, SHIPPED },
    { "rangelog",  V_RANGELOG,  SHIPPED },
    { "facing",    V_FACING,    DEV },
    { "sortielog", V_SORTIELOG, SHIPPED },
    { "dbflog",    V_DBFLOG,    SHIPPED },
    { "omenparse", V_OMENPARSE, SHIPPED },
    { "omenstate", V_OMENSTATE, SHIPPED },
    { "tpool",     V_TPOOL,     SHIPPED },
    { "tmem",      V_TMEM,      SHIPPED },
    { "config",    V_CONFIG,    SHIPPED },
    { "timers",    V_TIMERS,    SHIPPED },
    { "edit",      V_EDIT,      SHIPPED },
    { "layout",    V_LAYOUT,    SHIPPED },
    { "res",       V_RES,       SHIPPED },
    { "lay",       V_LAY,       DEV },       // dev : effect layers
};
static const int TABLE_N = (int)(sizeof(TABLE) / sizeof(TABLE[0]));

// The command WORD : the first word after optional slashes and an optional leading "aio" (Windower passes "aio ..."),
// compared exactly against the table. `rest` = the text right after the word (its leading spaces included), never
// null. NONE when the first word is not in the table -- the caller then tries the dev probes, then says "unknown".
inline int find(const char* buf, const char** rest) {
    static const char* const EMPTY = "";
    if (rest) *rest = EMPTY;
    if (!buf) return NONE;
    const char* p = buf;
    while (*p == ' ' || *p == '\t' || *p == '/') ++p;
    if (!strncmp(p, "aio", 3) && (p[3] == ' ' || p[3] == '\t')) { p += 3; while (*p == ' ' || *p == '\t') ++p; }
    int n = 0; while (p[n] && p[n] != ' ' && p[n] != '\t') ++n;
    if (!n) return NONE;
    for (int i = 0; i < TABLE_N; ++i)
        if ((int)strlen(TABLE[i].word) == n && !strncmp(p, TABLE[i].word, (size_t)n)) { if (rest) *rest = p + n; return TABLE[i].id; }
    return NONE;
}

// The line as the dispatcher reads it : lower-cased into `out` (cap bytes, terminated). Returns how many characters
// did NOT fit (0 = whole). Audit R12 : the line used to be cut at 255 without a word ; the caller counts and logs it.
inline int copy_lower(const char* in, char* out, int cap) {
    if (!out || cap <= 0) return 0;
    int i = 0;
    if (in) for (; in[i] && i < cap - 1; ++i) { const char c = in[i]; out[i] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }
    out[i] = 0;
    int lost = 0;
    if (in) while (in[i + lost]) ++lost;
    return lost;
}

}} // namespace aio::verbs
