// ffximain_rva.cpp -- see ffximain_rva.h for WHY the addresses are data.
#include "model/ffximain_rva.h"
#include "model/game_mem.h"        // ffximain_base / entity_array
#include "model/paths.h"           // the data dir for the cache file
#include "model/spells_gen.h"      // spell_info  : an examine cache proves itself by DECODING
#include "model/abilities_gen.h"   // abil_info
#include "model/weapon_skills_gen.h" // ws_info : the ability cache carries weapon skills too (raw id, no +0x200)
#include "model/sentinel.h"        // a packet that matches NOWHERE is a layout change, not a moved address
#include "windower.h"
#include "windower_debug.h"
#include <windows.h>
#include <cstdio>
#include <cstring>

namespace aio {

using windower::safe_read;   // u32 / valid_ptr arrive via game_mem.h's using-decls

// ---------------------------------------------------------------- the registry ----

struct Entry {
    u32         seed;      // last known-good address (the value shipped in this build)
    const char* name;
};
// Seeds = the 2026-08-12 post-patch addresses. They are a starting point, not a promise.
static const Entry ENTRIES[FM_N] = {
    { 0x5787AC, "target_t ptr  (party selection cursor)" },
    { 0x5EEDAC, "live-menu ptr (cost/Next box)"          },
    { 0x634F68, "examined SPELL id"                      },
    { 0x6345D0, "examined ABILITY id"                    },
    { 0x485684, "PointWatch block (EXP / ML / exemplar)" },
    { 0x485866, "PointWatch merits (LP / merit count)"   },
};

static u32   g_rva[FM_N];
static bool  g_healed[FM_N], g_confirmed[FM_N];
static char  g_how[FM_N][40];
static bool  g_loaded = false;   // rule10-ok: latched ONLY once FFXiMain is mapped -- see ensure_loaded, the transient "module not ready" case deliberately does not latch

// PointWatch ground truth, handed over by the packet handlers. `due` counts frames down : our hook runs
// BEFORE the client writes its own statics, so an immediate compare tests the values about to be replaced.
struct PwExpect { u32 xpCur, xpTnl, ml, epCur, epTnml; int due; bool armed; };
struct MeritExpect { u32 lp, merits, maxMerits; int due; bool armed; };
static PwExpect    g_pw    = { 0, 0, 0, 0, 0, 0, false };
static MeritExpect g_merit = { 0, 0, 0, 0, false };

// Per-cache watch state (index 0 = spell, 1 = ability) : the addresses whose value decodes, and the first
// decoded value each one showed. Adoption needs a SECOND, different one -- see heal_exam.
struct ExamWatch { u32 addr[12], first[12]; int n, sweepIn, sampleIn; };
static ExamWatch g_exam[2] = { { {0}, {0}, 0, 0, 0 }, { {0}, {0}, 0, 0, 0 } };

// ---------------------------------------------------------------- client fingerprint ----

unsigned fm_fingerprint() {
    static u32 fp = 0xFFFFFFFFu;
    if (fp != 0xFFFFFFFFu) return fp;
    fp = 0;
    const u32 ffm = ffximain_base();
    if (!ffm) { fp = 0xFFFFFFFFu; return 0; }        // not loaded yet -- do not cache a zero answer
    u32 e = 0, sz = 0, stamp = 0;
    if (safe_read(ffm + 0x3C, &e)) { safe_read(ffm + e + 0x50, &sz); safe_read(ffm + e + 0x08, &stamp); }
    fp = sz ^ stamp;
    return fp;
}

// [lo,hi) of FFXiMain's loaded image.
static void image_range(u32& lo, u32& hi) {
    lo = hi = 0;
    const u32 ffm = ffximain_base();
    if (!ffm) return;
    u32 e = 0, sz = 0;
    if (!safe_read(ffm + 0x3C, &e)) return;
    if (!safe_read(ffm + e + 0x50, &sz) || !sz || sz > 0x4000000) sz = 0xBE2000;
    lo = ffm; hi = ffm + sz;
}

// ---------------------------------------------------------------- the disk cache ----
//
// Healing happens once per client version, not once per login. The fingerprint is the whole safety of
// this : a cache written for another client must be IGNORED, never trusted, or a stale address gets read
// as live data -- the one failure mode worse than a dead feature.

static const char* cache_path() {
    static char b[300];
    if (!b[0]) plugin_path(b, sizeof(b), "data\\rva_cache.txt");
    return b;
}

// `format` is bumped whenever a HEALER changes its mind about what counts as proof. Without it, a wrong
// address adopted by an older, weaker rule comes back from disk marked "confirmed" and outlives the fix --
// which is exactly what happened when the first menu healer adopted the log-window slot.
static const int CACHE_FORMAT = 2;

static void cache_save() {
    FILE* f = fopen(cache_path(), "w");
    if (!f) return;
    fprintf(f, "# AioHUD -- FFXiMain static addresses re-derived at runtime.\n"
               "# Tied to one client build : a different fingerprint discards the whole file.\n"
               "format=%d\nfingerprint=%08X\n", CACHE_FORMAT, fm_fingerprint());
    // Only PROVEN addresses are written. A proposal (an address borrowed from another static's shift) is
    // a working hypothesis : persisting it would promote it to fact on the next login, unexamined.
    for (int i = 0; i < FM_N; ++i)
        if (g_confirmed[i]) fprintf(f, "rva%d=%X  # %s (%s)\n", i, g_rva[i], ENTRIES[i].name, g_how[i]);
    fclose(f);
}

static void seed_all() {
    for (int i = 0; i < FM_N; ++i) { g_rva[i] = ENTRIES[i].seed; g_healed[i] = g_confirmed[i] = false; g_how[i][0] = 0; }
}

static void cache_load() {
    seed_all();
    g_loaded = true;   // rule10-ok: reached only with FFXiMain mapped (ensure_loaded gates it), and a re-read would only re-parse the same file -- the retry that matters is the per-static healing, which is bounded and never latches
    FILE* f = fopen(cache_path(), "r");
    if (!f) return;
    char line[256]; u32 fp = 0; bool fpOk = false, fmtOk = false; int n = 0;
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;
        u32 v = 0; int idx = 0, fmt = 0;
        if (sscanf(line, "format=%d", &fmt) == 1) {
            fmtOk = (fmt == CACHE_FORMAT);
            if (!fmtOk) { windower::debug::log("fm: cache is format %d, this build wants %d -- discarding it and "
                                               "re-deriving (a healer's notion of proof changed)", fmt, CACHE_FORMAT); break; }
            continue;
        }
        if (sscanf(line, "fingerprint=%X", &v) == 1) {
            fp = v; fpOk = (v == fm_fingerprint());
            if (!fpOk) {
                windower::debug::log("fm: cached addresses are for client %08X, this one is %08X -- "
                                     "the game was patched, discarding the cache and re-deriving", v, fm_fingerprint());
                break;                                   // seeds stay ; healing will re-derive what moved
            }
            continue;
        }
        if (!fpOk || !fmtOk) continue;                   // a file with no format= line is pre-versioned : ignore its addresses
        if (sscanf(line, "rva%d=%X", &idx, &v) == 2 && idx >= 0 && idx < FM_N && v) {
            g_rva[idx] = v; g_healed[idx] = true; g_confirmed[idx] = true;
            strncpy(g_how[idx], "restored from cache", sizeof(g_how[0]) - 1);
            ++n;
        }
    }
    fclose(f);
    if (n) windower::debug::log("fm: %d address(es) restored from the cache for client %08X", n, fp);
}

// The cache is only READABLE once the fingerprint can be computed, i.e. once FFXiMain is mapped. Before
// that we hand out the seeds but do NOT latch : latching early would compare the file against fingerprint
// 0, discard a perfectly good cache, and re-derive everything for nothing (rule 10 -- a transient
// condition must not become a permanent state).
static void ensure_loaded() {
    if (g_loaded) return;
    if (!ffximain_base()) { seed_all(); return; }
    cache_load();
}

// ---------------------------------------------------------------- accessors ----

unsigned    fm_rva(FmStatic s)       { ensure_loaded(); return (s >= 0 && s < FM_N) ? g_rva[s] : 0; }
const char* fm_name(FmStatic s)      { return (s >= 0 && s < FM_N) ? ENTRIES[s].name : "?"; }
bool        fm_healed(FmStatic s)    { ensure_loaded(); return (s >= 0 && s < FM_N) && g_healed[s]; }
bool        fm_confirmed(FmStatic s) { ensure_loaded(); return (s >= 0 && s < FM_N) && g_confirmed[s]; }

unsigned fm_addr(FmStatic s) {
    const u32 ffm = ffximain_base();
    return ffm ? (ffm + fm_rva(s)) : 0;
}

void fm_adopt(FmStatic s, unsigned rva, const char* how, bool confirmed) {
    ensure_loaded();
    if (s < 0 || s >= FM_N || !rva) return;
    const bool moved = (g_rva[s] != rva);
    if (!moved && g_healed[s] && (g_confirmed[s] || !confirmed)) return;   // nothing new to say
    if (moved)
        windower::debug::log("fm: %s moved -- FFXiMain+0x%X -> +0x%X (%c0x%X from the seed) [%s]",
                             ENTRIES[s].name, g_rva[s], rva,
                             (rva > ENTRIES[s].seed) ? '+' : '-',
                             (rva > ENTRIES[s].seed) ? (rva - ENTRIES[s].seed) : (ENTRIES[s].seed - rva), how);
    else
        windower::debug::log("fm: %s CONFIRMED at FFXiMain+0x%X [%s]", ENTRIES[s].name, rva, how);
    g_rva[s] = rva; g_healed[s] = true;
    if (confirmed) g_confirmed[s] = true;
    strncpy(g_how[s], how, sizeof(g_how[0]) - 1); g_how[s][sizeof(g_how[0]) - 1] = 0;
    cache_save();
}

// ---------------------------------------------------------------- SEH-guarded scanners ----
// Plain-C bodies, no C++ objects (C2712). Each walks the FFXiMain image once and returns the FIRST
// address satisfying its proof, or 0.

// The PointWatch block : 14 bytes of exact match spread over a 0x60 span, straight out of packet 0x061.
static u32 scan_region_pw(u32 rl, u32 rh, u32 xpCur, u32 xpTnl, u32 ml, u32 epCur, u32 epTnml) {
    __try {
        for (u32 a = rl; a + 0x60 < rh; a += 4) {
            if (*(const unsigned short*)a != (unsigned short)xpCur)          continue;
            if (*(const unsigned short*)(a + 2) != (unsigned short)xpTnl)    continue;
            if (*(const unsigned*)(a + 0x58) != epCur)                       continue;
            if (*(const unsigned*)(a + 0x5C) != epTnml)                      continue;
            if (*(const unsigned char*)(a + 0x55) != (unsigned char)ml)      continue;
            return a;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return 0;
}

// The merit block : only ~4 bytes of signal, so it is searched in a WINDOW (near the 0x061 block, which
// the disassembly puts it beside) rather than across the image, where it would collide constantly.
// Returns the match NEAREST `anchor`, not the first one : with only ~4 bytes of signal a window this size
// can hold a coincidence, and the disassembly says the real block sits beside the 0x061 one. Distance is
// the tie-breaker the signature itself cannot provide.
static u32 scan_region_merit(u32 rl, u32 rh, u32 lp, u32 merits, u32 maxMerits, u32 anchor, u32 best) {
    __try {
        for (u32 a = rl; a + 8 < rh; a += 2) {
            if (*(const unsigned short*)a != (unsigned short)lp)                     continue;
            if ((*(const unsigned char*)(a + 2) & 0x7F) != (unsigned char)merits)    continue;
            if (*(const unsigned char*)(a + 4) != (unsigned char)maxMerits)          continue;
            const u32 d    = (a > anchor) ? (a - anchor) : (anchor - a);
            const u32 dBest = !best ? 0xFFFFFFFFu : ((best > anchor) ? (best - anchor) : (anchor - best));
            if (d < dBest) best = a;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return best;
}

// Does this raw value decode to a real action for that cache ? The ONLY test either cache ever gets to
// pass -- kept in one place so the healer and the confirmation can never drift apart.
static bool exam_decodes(FmStatic s, u32 v) {
    if (s == FM_EXAM_SPELL) return v != 0 && v <= 0x4000 && spell_info(v) != 0;
    // The ability cache serves BOTH lists : job abilities carry id+0x200, weapon skills the raw id (that
    // is how read_action_menu tells them apart). Accepting only the job-ability half would leave the cache
    // unprovable for anyone who opens the WS menu and never the JA one.
    if (v >= 0x200) return v <= 0x4200 && abil_info(v - 0x200) != 0;
    return v >= 1 && ws_info(v) != 0;
}

// Addresses in [rl,rh) currently holding something that decodes. A wide net on purpose : which of them
// actually TRACKS the highlight is decided by watching, not by this pass.
static int scan_region_exam(u32 rl, u32 rh, FmStatic s, u32* out, int cap) {
    int n = 0;
    __try {
        for (u32 a = rl; a + 4 < rh && n < cap; a += 4)
            if (exam_decodes(s, *(const unsigned*)a)) out[n++] = a;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return n;
}

// Menu-shaped slots : a pointer to an object whose def carries the inline "menu" tag at def+0x46.
// Collects candidates ; which one is FOCUSED is decided later, by watching which one changes.
static int scan_region_menu(u32 rl, u32 rh, u32* out, int cap, int n) {
    __try {
        for (u32 a = rl; a + 4 < rh && n < cap; a += 4) {
            const u32 v = *(const unsigned*)a;
            if (!valid_ptr(v)) continue;
            u32 def = 0; if (!safe_read(v + 0x04, &def) || !valid_ptr(def)) continue;
            u32 tag = 0; if (!safe_read(def + 0x46, &tag) || tag != 0x756E656Du) continue;   // "menu"
            out[n++] = a;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return n;
}

// Walk the image's readable regions, feeding one scanner. `kind` picks it (keeps the region walk single).
static u32 image_scan(int kind, const u32* args, u32* menuOut, int menuCap, int* menuN) {
    u32 lo = 0, hi = 0, best = 0; image_range(lo, hi);
    if (!lo) return 0;
    const u32 MASK_RD = PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY;
    MEMORY_BASIC_INFORMATION mbi;
    for (u32 a = lo; a < hi; ) {
        if (!VirtualQuery((void*)a, &mbi, sizeof(mbi))) break;
        const u32 base = (u32)mbi.BaseAddress, sz = (u32)mbi.RegionSize;
        if (!sz) break;
        const bool rd = (mbi.State == MEM_COMMIT) && (mbi.Protect & MASK_RD) && !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS));
        if (rd) {
            u32 rl = base > lo ? base : lo, rh = (base + sz) < hi ? (base + sz) : hi;
            if (kind == 1 && rh > rl + 0x60) { const u32 h = scan_region_pw(rl, rh, args[0], args[1], args[2], args[3], args[4]); if (h) return h; }
            if (kind == 2 && rh > rl + 8) {
                if (rl < args[0]) rl = args[0];                     // merit : window, not whole image
                if (rh > args[1]) rh = args[1];
                if (rh > rl + 8) best = scan_region_merit(rl, rh, args[2], args[3], args[4], args[5], best);
            }
            if (kind == 3 && rh > rl + 4) *menuN = scan_region_menu(rl, rh, menuOut, menuCap, *menuN);
        }
        a = base + sz;
    }
    return best;
}

// ---------------------------------------------------------------- packet ground truth ----

void fm_pw_expect(unsigned xpCur, unsigned xpTnl, unsigned ml, unsigned epCur, unsigned epTnml) {
    if (!epTnml && !xpTnl) return;                       // an all-zero payload proves nothing
    g_pw.xpCur = xpCur; g_pw.xpTnl = xpTnl; g_pw.ml = ml; g_pw.epCur = epCur; g_pw.epTnml = epTnml;
    g_pw.due = 30;                                       // ~half a second : let the client write its own copy first
    g_pw.armed = true;                                   // set LAST : the reader takes the fields only once this flips
}

void fm_pw_merit_expect(unsigned lp, unsigned merits, unsigned maxMerits) {
    if (!maxMerits) return;
    g_merit.lp = lp; g_merit.merits = merits; g_merit.maxMerits = maxMerits;
    g_merit.due = 30;
    g_merit.armed = true;
}

// ---------------------------------------------------------------- the healers ----
//
// Each returns quietly when its static is healthy. Bounded, and they SAY when a budget runs out -- a
// healer that dies silently reads exactly like an address that was fine all along (CLAUDE.md rule 10).

static void heal_pw_block() {
    if (!g_pw.armed || --g_pw.due > 0) return;
    // Copy BEFORE disarming : the packet thread may re-arm at any moment, and half of one payload mixed
    // with half of the next would search for a set of values that never existed together.
    const u32 xpCur = g_pw.xpCur, xpTnl = g_pw.xpTnl, ml = g_pw.ml, epCur = g_pw.epCur, epTnml = g_pw.epTnml;
    g_pw.armed = false;
    const u32 a = fm_addr(FM_PW_BLOCK);
    if (!a) return;
    u32 xw = 0, ec = 0, et = 0;
    safe_read(a, &xw); safe_read(a + 0x58, &ec); safe_read(a + 0x5C, &et);
    if ((xw & 0xFFFF) == xpCur && ((xw >> 16) & 0xFFFF) == xpTnl && ec == epCur && et == epTnml) {
        if (!g_confirmed[FM_PW_BLOCK]) fm_adopt(FM_PW_BLOCK, fm_rva(FM_PW_BLOCK), "packet 0x061");
        return;                                          // healthy : the static agrees with the packet
    }
    const u32 args[5] = { xpCur, xpTnl, ml, epCur, epTnml };
    const u32 hit = image_scan(1, args, 0, 0, 0);
    if (hit) { fm_adopt(FM_PW_BLOCK, hit - ffximain_base(), "packet 0x061"); return; }
    // No address in the whole image holds what the server just sent. That is not an address that slid --
    // it is the block's layout, or the packet's, having changed. Nothing re-pins that, so it goes to the
    // sentinel, which is where "we are now reading something we no longer understand" belongs.
    { char d[120];
      _snprintf(d, sizeof(d), "no address holds the 0x061 values (exp %u/%u ep %u/%u)", xpCur, xpTnl, epCur, epTnml);
      d[sizeof(d) - 1] = 0;
      sentinel_note_unmatched(SEN_POINTWATCH, d); }
}

static void heal_pw_merit() {
    if (!g_merit.armed || --g_merit.due > 0) return;
    const u32 lp = g_merit.lp, mc = g_merit.merits, mx = g_merit.maxMerits;   // copy before disarming (see heal_pw_block)
    g_merit.armed = false;
    const u32 a = fm_addr(FM_PW_MERIT);
    if (!a) return;
    u32 v0 = 0, v4 = 0;
    safe_read(a, &v0); safe_read(a + 4, &v4);
    if ((v0 & 0xFFFF) == lp && ((v0 >> 16) & 0x7F) == mc && (v4 & 0xFF) == mx) {
        if (!g_confirmed[FM_PW_MERIT]) fm_adopt(FM_PW_MERIT, fm_rva(FM_PW_MERIT), "packet 0x063 order 2");
        return;
    }
    // Search around where the 0x061 block sits : the disassembly puts the two side by side, and this
    // signature is too short to be searched image-wide without collecting collisions.
    const u32 ffm = ffximain_base();
    const u32 anchor = ffm + fm_rva(FM_PW_BLOCK) + 0x1E2;
    const u32 args[6] = { anchor - 0x4000, anchor + 0x4000, lp, mc, mx, anchor };
    const u32 hit = image_scan(2, args, 0, 0, 0);
    if (hit) fm_adopt(FM_PW_MERIT, hit - ffm, "packet 0x063 order 2");
}

// The focused-menu slot. Detection is the interesting half : 0 is the NORMAL value (no menu open), so
// "not a pointer" cannot be the test. What the broken state actually looked like on 2026-08-12 was a
// small integer (14, 15) -- a different variable entirely. That IS the test.
static void heal_menu_ptr() {
    static u32 cand[16]; static int nCand = 0;
    static u32 lastName[16];
    static int fullSweeps = 0, sampleIn = 0, sweepIn = 0, grace = 600;
    if (g_confirmed[FM_MENU_PTR]) return;

    const u32 a = fm_addr(FM_MENU_PTR);
    if (!a) return;
    u32 v = 0; safe_read(a, &v);
    if (valid_ptr(v)) {                                  // a plausible object : is it actually a menu ?
        u32 def = 0, tag = 0;
        if (safe_read(v + 0x04, &def) && valid_ptr(def) && safe_read(def + 0x46, &tag) && tag == 0x756E656Du) {
            fm_adopt(FM_MENU_PTR, fm_rva(FM_MENU_PTR), "def carries the \"menu\" tag");
            return;
        }
    }
    // Deliberately NOT keyed on what the dead slot reads. The 2026-08-12 failure happened to leave a small
    // integer there, but 0 is also the value of a HEALTHY closed menu -- keying on the symptom we happened
    // to see would build a healer that only ever fixes the bug we already fixed. So : if the slot has not
    // proven itself after a grace period of play, go looking, whatever it currently reads.
    if (grace > 0) { --grace; return; }
    // RE-SWEEP, not sweep-once. A slot only looks like a menu WHILE a menu is open, so a single pass taken
    // with everything closed cannot see the one we are after -- it sees only the always-present decoys, and
    // then patiently watches the wrong thing forever. (That is precisely what the first version did.) So the
    // candidate set keeps growing as menus come and go.
    if (--sweepIn <= 0) {
        sweepIn = 180;                                    // ~3 s between sweeps
        // Cheap first : a window around the seed. A recompile shifts things by bytes (this one by 0x40), so
        // 64 KB finds it in microseconds. The 12 MB whole-image sweep is the rare fallback -- run every time
        // it would stutter the game for as long as no menu had ever been opened.
        u32 fresh[16];
        int nf = scan_region_menu(a - 0x8000, a + 0x8000, fresh, 16, 0);
        if (!nf && !nCand && fullSweeps < 2) {
            ++fullSweeps;
            image_scan(3, 0, fresh, 16, &nf);
            if (!nf && fullSweeps == 2)
                windower::debug::log("fm: no menu-shaped slot anywhere in FFXiMain -- the cost/Next box stays "
                                     "off ; run //aio rva with a menu open and send the log");
        }
        int n = nCand;
        for (int i = 0; i < nf && n < 16; ++i) {           // union : never drop a slot we already watch
            bool known = false;
            for (int k = 0; k < n; ++k) if (cand[k] == fresh[i]) { known = true; break; }
            if (!known) { cand[n] = fresh[i]; lastName[n] = 0xFFFFFFFFu; ++n; }
        }
        if (n != nCand) {
            windower::debug::log("fm: menu ptr unproven (reads %08X) -- watching %d menu-shaped slot(s). Open a "
                                 "couple of DIFFERENT menus : only the focused one ever shows two names.", v, n);
            nCand = n;
        }
    }
    if (!nCand) return;
    if (--sampleIn > 0) return;
    sampleIn = 15;                                       // ~4 samples/second : fast enough to catch a menu opening
    u32 bestAddr = 0, bestDist = 0xFFFFFFFFu;
    const u32 seedAddr = ffximain_base() + ENTRIES[FM_MENU_PTR].seed;
    for (int i = 0; i < nCand; ++i) {
        u32 pv = 0, def = 0, nm = 0;
        safe_read(cand[i], &pv);
        if (valid_ptr(pv) && safe_read(pv + 0x04, &def) && valid_ptr(def)) safe_read(def + 0x4E, &nm);
        // EMPTY IS NOT A NAME. Treating "no menu right now" as a changed name is what made the first
        // version adopt the log window : that slot blinks empty for a frame and instantly looked like it
        // was following the player. Only NON-EMPTY names count, and it takes TWO DIFFERENT ones --
        // 'logwindo' and 'inline' are always themselves, so they can never qualify, while the focused
        // slot reads 'magic' then 'ability' as you move around. This is the manual two-run differential,
        // exactly, and nothing weaker is evidence.
        if (!nm) continue;
        if (lastName[i] == 0xFFFFFFFFu) { lastName[i] = nm; continue; }
        if (lastName[i] == nm) continue;
        const u32 d = (cand[i] > seedAddr) ? (cand[i] - seedAddr) : (seedAddr - cand[i]);
        if (d < bestDist) { bestDist = d; bestAddr = cand[i]; }   // tie-break : a patch shifts by bytes
    }
    if (bestAddr) {
        fm_adopt(FM_MENU_PTR, bestAddr - ffximain_base(), "two different menu names on one slot");
        nCand = 0;
    }
}

// The two examine caches. They hold bare integers, so nothing about them is self-evident in isolation --
// but a shift PROVEN by another static is a legitimate proposal, and decoding the value back to a real
// spell / ability name is the confirmation. Proposed silently, confirmed loudly.
// Which menu is open right now, as the first 4 chars of its def name ('magi', 'abil', ...). 0 = none.
// Only meaningful once FM_MENU_PTR itself is trusted.
static u32 open_menu_tag() {
    if (!g_confirmed[FM_MENU_PTR]) return 0;
    u32 p = 0, def = 0, nm = 0;
    if (!safe_read(fm_addr(FM_MENU_PTR), &p) || !valid_ptr(p)) return 0;
    if (!safe_read(p + 0x04, &def) || !valid_ptr(def)) return 0;
    safe_read(def + 0x4E, &nm);
    return nm;
}

static void heal_exam(FmStatic s, FmStatic anchor) {
    if (g_confirmed[s]) return;
    const u32 base = ffximain_base();
    if (!base) return;
    // Decoding alone is NOT proof : plenty of stray integers fall in the spell-id range and would decode
    // to some name, which would confirm a wrong address -- the one outcome worse than a dead box. The
    // proof is the CORRELATION : the value decodes *while the matching menu is open*. A cache that holds
    // a real spell id exactly when the Magic menu is up is the spell cache.
    const u32 tag = open_menu_tag();
    const bool rightMenu = (s == FM_EXAM_SPELL) ? (tag == 0x6967616Du)     // "magi"
                                                : (tag == 0x6C696261u);    // "abil"
    u32 v = 0; safe_read(base + fm_rva(s), &v);
    const bool ok = rightMenu && exam_decodes(s, v);
    if (ok) { fm_adopt(s, fm_rva(s), "decodes while its own menu is open"); return; }

    // (1) Borrow the shift from a static that PROVED one. A shift of ZERO is a legitimate answer -- it says
    // "this static did not move", which is still information, and refusing to act on it left the caches
    // stranded whenever the anchor came back to its own seed (the poison test hit exactly that).
    if (g_confirmed[anchor]) {
        const u32 proposed = ENTRIES[s].seed + (g_rva[anchor] - ENTRIES[anchor].seed);
        if (proposed != g_rva[s]) { fm_adopt(s, proposed, "proposed from a proven shift", false); return; }
    }

    // (2) Prove it independently. Borrowing only works when the two statics moved TOGETHER ; nothing
    // guarantees that, and a cache that moved on its own would leave the box permanently empty with no way
    // back. So, while the right menu is open, watch every nearby address that decodes to a real action and
    // adopt the one that CHANGES to a DIFFERENT real action as the cursor moves. One value that decodes is
    // a coincidence ; a value that keeps decoding while tracking the highlight is the cache.
    if (!rightMenu) return;
    ExamWatch& w = g_exam[(s == FM_EXAM_SPELL) ? 0 : 1];
    if (--w.sweepIn <= 0) {
        w.sweepIn = 120;
        const u32 seedAddr = base + ENTRIES[s].seed;
        u32 fresh[12]; const int nf = scan_region_exam(seedAddr - 0x8000, seedAddr + 0x8000, s, fresh, 12);
        for (int i = 0; i < nf && w.n < 12; ++i) {
            bool known = false;
            for (int k = 0; k < w.n; ++k) if (w.addr[k] == fresh[i]) { known = true; break; }
            if (!known) { w.addr[w.n] = fresh[i]; w.first[w.n] = 0; ++w.n; }
        }
    }
    if (--w.sampleIn > 0) return;
    w.sampleIn = 10;
    const u32 seedAddr = base + ENTRIES[s].seed;
    u32 bestAddr = 0, bestDist = 0xFFFFFFFFu;
    for (int i = 0; i < w.n; ++i) {
        u32 cv = 0; safe_read(w.addr[i], &cv);
        if (!exam_decodes(s, cv)) continue;               // only real actions count, here as everywhere
        if (!w.first[i]) { w.first[i] = cv; continue; }
        if (w.first[i] == cv) continue;
        const u32 d = (w.addr[i] > seedAddr) ? (w.addr[i] - seedAddr) : (seedAddr - w.addr[i]);
        if (d < bestDist) { bestDist = d; bestAddr = w.addr[i]; }
    }
    if (bestAddr) { fm_adopt(s, bestAddr - base, "tracks the highlighted action"); w.n = 0; }
}

void fm_tick() {
    ensure_loaded();
    if (!ffximain_base()) return;
    heal_pw_block();
    heal_pw_merit();
    heal_menu_ptr();
    // The menu pointer is the right anchor for the examine caches : it lives in the same region, and it is
    // the one that proves itself the moment the player opens the very menu those caches serve.
    heal_exam(FM_EXAM_SPELL, FM_MENU_PTR);
    heal_exam(FM_EXAM_ABIL,  FM_MENU_PTR);
}

// ---------------------------------------------------------------- test hook ----
//
// This whole file is machinery that fires roughly once a year, which means that without a way to trigger
// it on demand it would go UNTESTED until the exact day everything depends on it -- and code first run in
// anger is code first debugged in anger. fm_poison() breaks the addresses on purpose so the healers can be
// watched doing their job. In memory only : the cache on disk is untouched, so a reload restores sanity.
void fm_poison(unsigned delta) {
    ensure_loaded();
    for (int i = 0; i < FM_N; ++i) {
        g_rva[i] = ENTRIES[i].seed + delta;
        g_healed[i] = g_confirmed[i] = false;
        strncpy(g_how[i], "POISONED (test)", sizeof(g_how[0]) - 1);
    }
    windower::debug::log("fm: POISONED every static by +0x%X -- this is a TEST. Expect : target_t back within "
                         "a few seconds of targeting someone ; PointWatch on the next 0x061 (change zone or "
                         "open the Status menu) ; the menu ptr after you open a menu. Reload the plugin to undo.", delta);
}

// ---------------------------------------------------------------- reporting ----

int fm_report(char out[][160], int maxOut) {
    ensure_loaded();
    int n = 0;
    for (int i = 0; i < FM_N && n < maxOut; ++i) {
        u32 v = 0; const u32 a = fm_addr((FmStatic)i);
        if (a) safe_read(a, &v);
        _snprintf(out[n], 160, "%-38s FFXiMain+0x%06X = %08X  %s%s%s",
                  ENTRIES[i].name, g_rva[i], v,
                  g_confirmed[i] ? "PROVEN" : (g_healed[i] ? "proposed" : "seed"),
                  g_how[i][0] ? " -- " : "", g_how[i][0] ? g_how[i] : "");
        out[n][159] = 0; ++n;
    }
    return n;
}

} // namespace aio
