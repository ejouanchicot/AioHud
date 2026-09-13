// gear_canary.cpp -- see gear_canary.h. The decisions (record proof, reference fingerprints, verdict) are in
// gfx/gear_dat.h and covered by tests/t_geardat.cpp.
#include "ui/gear_canary.h"
#include "gfx/gear_dat.h"
#include "gfx/texture.h"          // decode_gear_icon_from_rom / ffxi_rom_dir_probe : the PRODUCTION decoder is what is on trial
#include "model/selftest.h"
#include "retry_clock.h"
#include "windower_debug.h"
#include <windows.h>
#include <stdio.h>

namespace aio {

// A probe is SETTLED once it has an answer that cannot change during the session (decoded / refused layout), or
// once it has failed QUICK_TRIES times on I/O -- then it counts as an I/O finding but keeps being retried in the
// slow lane (rule 10 : a DAT locked by an AV on first touch is not a permanent fact about the world).
static const unsigned QUICK_TRIES = 3;
static const unsigned QUICK_MS    = 1000;
static const unsigned SLOW_MS     = 30000;

enum { PR_PENDING = 0, PR_OK, PR_DIFF, PR_BAD_LAYOUT, PR_IO };
struct Probe {
    int      state;
    unsigned tries;
    unsigned nextMs;
    int      step, err;
    long     stride, fileSize;
    uint32_t hash;         // refs : what the ROM decoded to
};
static const int MAX_DATS = 16, MAX_REFS = 16;
static Probe s_dat[MAX_DATS];
static Probe s_ref[MAX_REFS];
static bool  s_noRom = false;
static int   s_lastVerdict = -1;

static bool probe_open(const Probe& p) { return p.state == PR_PENDING || p.state == PR_IO; }

// One decode, classified. `refHash` = 0 for a structural DAT probe.
static void run_probe(Probe& p, unsigned id, uint32_t refHash, unsigned nowMs) {
    u32 px[32 * 32]; GearInfo gi;
    const bool ok = decode_gear_icon_from_rom(id, px, &gi);
    p.step = gi.step; p.err = gi.err; p.stride = gi.stride; p.fileSize = gi.fileSize;
    if (ok) {
        p.hash  = gear_icon_hash(px);
        p.state = (refHash == 0 || p.hash == refHash) ? PR_OK : PR_DIFF;
    } else if (gi.step == GS_BAD_LAYOUT) {
        p.state = PR_BAD_LAYOUT;
    } else if (gi.step == GS_NO_ROMDIR) {
        s_noRom = true;
    } else {                                   // GS_NO_DAT / GS_BAD_READ : file I/O, transient
        ++p.tries;
        if (p.tries >= QUICK_TRIES) p.state = PR_IO;
        retry_arm_at(p.nextMs, nowMs, p.tries < QUICK_TRIES ? QUICK_MS : SLOW_MS);
    }
}

static int verdict_from_state() {
    int nd = 0; const GearDat* t = gear_dat_table(nd); (void)t;
    int nr = 0; gear_refs(nr);
    int pending = 0, bad = 0, io = 0, diff = 0;
    for (int i = 0; i < nd && i < MAX_DATS; ++i) {
        const int s = s_dat[i].state;
        pending += s == PR_PENDING; bad += s == PR_BAD_LAYOUT; io += s == PR_IO;
    }
    for (int i = 0; i < nr && i < MAX_REFS; ++i) {
        const int s = s_ref[i].state;
        pending += s == PR_PENDING; diff += s == PR_DIFF; io += s == PR_IO;
        // a ref inside a DAT whose layout was refused has nothing to add : it is already a BAD_LAYOUT finding
    }
    return gear_canary_verdict(s_noRom, pending, bad, io, diff);
}

void gear_canary_tick(unsigned nowMs) {
    if (s_noRom) return;
    if (!ffxi_rom_dir_probe(0)) { s_noRom = true; return; }   // resolved once and cached by texture.cpp
    int nd = 0; const GearDat* t = gear_dat_table(nd);
    int nr = 0; const GearRef* r = gear_refs(nr);
    bool probed = false;
    for (int i = 0; i < nd && i < MAX_DATS && !probed; ++i)
        if (probe_open(s_dat[i]) && retry_due_at(s_dat[i].nextMs, nowMs)) {
            run_probe(s_dat[i], (unsigned)(gear_dat_first(t[i]) > 0 ? gear_dat_first(t[i]) : t[i].lo), 0, nowMs);
            probed = true;
        }
    for (int i = 0; i < nr && i < MAX_REFS && !probed; ++i)
        if (probe_open(s_ref[i]) && retry_due_at(s_ref[i].nextMs, nowMs)) {
            const GearDat* d = gear_dat_for(r[i].id);
            int di = d ? (int)(d - t) : -1;
            if (di >= 0 && di < MAX_DATS && s_dat[di].state == PR_BAD_LAYOUT) { s_ref[i].state = PR_BAD_LAYOUT; continue; }
            run_probe(s_ref[i], r[i].id, r[i].hash, nowMs);
            probed = true;
        }

    // SAY IT, success included (rule 10 corollary) : a canary that only speaks on failure reads exactly like one
    // that never ran. Logged on every change of verdict, so an I/O finding that later clears is visible too.
    const int v = verdict_from_state();
    if (v != s_lastVerdict && v != GCV_PENDING) {
        char line[256]; gear_canary_summary(line, sizeof(line));
        windower::debug::log("GEARCANARY %s", line);
        s_lastVerdict = v;
    }
}

int gear_canary_verdict_now() { return verdict_from_state(); }

const char* gear_canary_verdict_name(int v) {
    switch (v) {
        case GCV_PENDING:      return "PENDING";
        case GCV_OK:           return "OK";
        case GCV_NO_ROM:       return "NO ROM";
        case GCV_IO:           return "DAT UNREADABLE";
        case GCV_BAD_LAYOUT:   return "DAT LAYOUT UNKNOWN";
        case GCV_REF_MISMATCH: return "REFERENCE ART MISMATCH";
    }
    return "?";
}

void gear_canary_summary(char* out, int cap) {
    if (!out || cap <= 0) return;
    int nd = 0; const GearDat* t = gear_dat_table(nd);
    int nr = 0; gear_refs(nr);
    int datOk = 0, refOk = 0; long stride = 0; bool mixed = false;
    for (int i = 0; i < nd && i < MAX_DATS; ++i) if (s_dat[i].state == PR_OK) {
        ++datOk;
        if (!stride) stride = s_dat[i].stride; else if (s_dat[i].stride != stride && t[i].hi != t[i].lo) mixed = true;
    }
    for (int i = 0; i < nr && i < MAX_REFS; ++i) refOk += s_ref[i].state == PR_OK;
    _snprintf(out, cap, "verdict=%s : %d/%d DATs proved (record 0x%lX%s), %d/%d reference icons identical to their bundled art",
              gear_canary_verdict_name(verdict_from_state()), datOk, nd, stride, mixed ? ", MIXED sizes" : "", refOk, nr);
    out[cap - 1] = 0;
}

// ---- the harness : no I/O here, only the state the tick left behind ----
static int gear_checks(CheckFail* out, int cap) {
    int n = 0;
    int nd = 0; const GearDat* t = gear_dat_table(nd);
    int nr = 0; const GearRef* r = gear_refs(nr);
    for (int i = 0; i < nd && i < MAX_DATS && n < cap; ++i) {
        const Probe& p = s_dat[i];
        if (p.state == PR_BAD_LAYOUT) {
            lstrcpynA(out[n].id, "GEAR.DAT_LAYOUT", sizeof(out[n].id));
            out[n].sev = CHK_BLOCK;
            _snprintf(out[n].detail, sizeof(out[n].detail),
                      "%s.DAT (%ld bytes) : no record size gives item %ld a valid record -- every icon of ids 0x%04X-0x%04X "
                      "shows as text. A client patch changed the item DAT layout again (gfx/gear_dat.h)",
                      t[i].dat, p.fileSize, gear_dat_first(t[i]) > 0 ? gear_dat_first(t[i]) : (long)t[i].lo, t[i].lo, t[i].hi);
        } else if (p.state == PR_IO) {
            lstrcpynA(out[n].id, "GEAR.DAT_IO", sizeof(out[n].id));
            out[n].sev = CHK_WARN;
            _snprintf(out[n].detail, sizeof(out[n].detail),
                      "%s.DAT unreadable after %u tries (step %d, errno %d) -- still retried every %u s ; its icons fall back "
                      "to the cache or to text, and cache repairs are suspended", t[i].dat, p.tries, p.step, p.err, SLOW_MS / 1000);
        } else continue;
        out[n].detail[sizeof(out[n].detail) - 1] = 0;
        ++n;
    }
    int diff = 0, first = -1;
    for (int i = 0; i < nr && i < MAX_REFS; ++i) if (s_ref[i].state == PR_DIFF) { if (first < 0) first = i; ++diff; }
    if (diff > 0 && n < cap) {
        const GearDat* d = gear_dat_for(r[first].id);
        lstrcpynA(out[n].id, "GEAR.REF_MISMATCH", sizeof(out[n].id));
        out[n].sev = CHK_WARN;
        _snprintf(out[n].detail, sizeof(out[n].detail),
                  "%d of %d reference icons decode to other art than when bundled (first : %u '%s' in %s.DAT, %08X vs %08X) -- "
                  "an item-icon pack over these DATs, or the decode reads the wrong bytes. Cache rewrites suspended",
                  diff, nr, r[first].id, r[first].name, d ? d->dat : "?",
                  (unsigned)s_ref[first].hash, (unsigned)r[first].hash);
        out[n].detail[sizeof(out[n].detail) - 1] = 0;
        ++n;
    }
    return n;
}

void gear_register_checks() { selftest_add("gearicons", gear_checks); }

} // namespace aio
