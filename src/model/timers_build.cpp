// timers_build.cpp -- the Timers box DECISIONS : which rows exist, what they say, how they sort -- and the FOCUS
// monitor behind the red OUT alerts and //aio out / //aio in. Moved out of src/ui/hud_timers.cpp on 2026-09-13
// (lot D) so they can be tested without a device : the renderer now only draws the rows this file builds.
// The move was MECHANICAL -- the bodies below are the original lines, verified by a normalised diff -- so every
// comment still speaks of the code as it was written; the edits are listed in model/timers_build.h.
#include "model/timers_build.h"
#include "model/flipwatch.h"   // notice a row set that cannot settle
#include "model/capwatch.h"   // notice a fixed table that has quietly run out of room
#include "model/ui_config.h"
#include "model/party_state.h"
#include "model/gamestate.h"
#include "model/model_clock.h"   // model_now_ms : one frozen instant per build
#include "model/abilities_gen.h"
#include "model/spells_gen.h"
#include "model/buffs_gen.h"
#include "model/song_family_gen.h"   // song_family(spell) : identify a BRD song (spell-keyed -> no status family-collapse) for the song-OUT rule
#include "model/tb_buff_gen.h"       // spell_buff(spell)->skill : identify a GEO Indicolure (skill 44) for the Indi--replaced OUT rule
#include "model/action_status_gen.h"   // is_debuff_status : keep enfeebles (Blind/Poison/Slow) out of the buff list
#include "model/focus_rules.h"   // the FOCUS monitor's three judgements, pure and tested
#include "model/ally_group.h"   // the (AoE N) count and the group-or-name decision, pure and tested
#include "model/timers_sort.h"   // the row order : pure, tested in tests/t_timersrules.cpp
#include "model/timers_rules.h"   // one row's source decisions : pure, tested in tests/t_timersrules.cpp
#include "model/selftest.h"   // this module's own checks, run by the in-game watcher
#include "windower_debug.h"
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

namespace aio {

// //aio timers reset (+ the config "Reset timers" button) : flush ALL live timer state -- self buff timers (0x063,
// the server re-sends them shortly), the buffs you cast on allies (estimates), and the FOCUS monitor's remembered
// buffs (a bumped generation makes timers_draw wipe its static fm[]). Clears any stuck "OUT" alert / stale row.
static unsigned g_tmResetGen = 0;
static const char* abil_name_by_id(unsigned id);   // defined below (COR rolls name their ABILITY, not a spell)
static int fm_tag_of(unsigned target, unsigned status, unsigned short spell, bool self);   // the number to print on a row, 0 = not monitored
static bool fm_muted_for(unsigned target, unsigned status, unsigned short spell, bool self);   // ... and whether this row was removed by hand

// ---- THE FOCUS MONITOR : which buffs we have promised to keep up, and on whom. ------------------------------
// It lives at file scope rather than inside timers_draw because it is the one piece of timer state a PERSON has
// to be able to correct. A monitored entry is born the moment a focus-flagged buff YOU cast is seen on a party
// member -- and nothing at that instant distinguishes "I meant to Haste him" from "I clicked the wrong name".
// The mistake only becomes visible later, as a red OUT for a buff you never intended to maintain.
//
// `muted` is the correction. Muting an entry stops the OUT, and the entry is DROPPED as soon as its buff is
// gone -- so the mute lives exactly as long as the cast that caused it, and a deliberate re-cast on that same
// person later starts a fresh, un-muted entry. That is why none of this needs to be saved to disk: there is no
// lasting state, only "ignore the one that is currently up".
// `muteRef` is what makes that second half TRUE. "Dropped when the buff is gone" only ends the mute for a buff
// that ENDS -- and the usual correction is the opposite: you Haste the wrong name, //aio out it, then Haste the
// RIGHT person, or that same person again on purpose. Haste overwrites itself, so the buff never lapses, the
// entry is fed without interruption, and the row stayed hidden for as long as it was maintained. So the mute
// also remembers WHICH CAST it silenced (fm_cast_ref) and lifts itself the moment a newer one lands.
// `tag` is the number you see on the row and type into //aio out. It is assigned once, at creation, and never
// changes while the entry lives -- which is the whole point: the ROWS are sorted by remaining time and shuffle
// as things tick, so a number that meant "third row from the top" would mean something else by the time you had
// finished typing it. A number that belongs to the ENTRY is the same number whenever you read it.
struct FocusMem { unsigned target; unsigned short status, spell; unsigned char isAbil, self, zoneCheck, muted, tag, seen, alerting, aoe; unsigned lostMs, muteRef, bornMs, rank; char name[20]; };   // rank : which of two same-status songs survives -- the ally cast's startMs, or your own copy's expiry. NOT bornMs : an entry is reused across re-casts, so its birth says nothing about which cast is fresher.
// bornMs : when this entry was created. Read by NOTHING that decides anything -- it exists so the harness
// can say "this entry has been alive 4 h", which is the only way an immortal entry (the purge that never
// runs, audit S2-6) is visible from outside. A field a decision depends on could not be added this cheaply.
// `alerting` = this entry drew its red OUT row on the LAST frame. It is set where the row is emitted and nowhere
// else, so it means exactly "what you can see in red right now" -- which is what //aio out alerts takes off. The
// alternative (re-deriving the condition in the command) would be a second copy of a decision that already has
// six suppression branches (song replaced, unrecoverable 5th song, Indi- swapped, hold expired, muted, no data),
// and the day one of them moved the two copies would disagree in silence.
// FOCUS_MAX -- how many buffs the monitor can watch at once. It was 24, and 24 is not a number that means
// anything: a full party is SIX people, and a bard holds up to five songs on each, which is thirty before a
// single Haste or Refresh is counted. The harness reported it itself on 2026-09-10 -- TM.FOCUS_FULL, held over
// three consecutive checks -- and a refused entry is silent by nature: the buff is simply never watched, so its
// OUT alert cannot happen and nothing says why.
// 64 covers six people at five songs plus a full set of other focus buffs, and the array is a few kilobytes.
static const int FOCUS_MAX = TM_FOCUS_MAX;   // timers_build.h
static FocusMem fm[FOCUS_MAX];
// HIGH-WATER MARKS, because the watcher SAMPLES. Both tables fill during a rotation change and drain again
// within a second or two -- and while full they evict rows, which is what scrambles the display. A check that
// reads the instantaneous count looks every 30 s and sees nothing wrong; the ally table overflowed for two
// hours of hunting on 2026-09-10 with TM.OB_FULL never once firing. A peak, unlike a sample, cannot be missed.
static int g_fmPeak = 0, g_obPeak = 0;

#ifdef AIOHUD_PROBES
// ---- WHY a row took the shape it did -----------------------------------------------------------------
// The row recorder logs what was DRAWN. Six hours went into a shimmer that could not be explained from
// that alone, because the drawn row never says which input produced it -- fresh or laggard, grouped or
// per person, present or missing. Every explanation had to be guessed from the effect, and four in a row
// fitted one observation and failed the next.
//
// So the INPUTS are captured too, and captured the cheap way: raw numbers assigned during the frame,
// formatted only when the row set actually changes. Formatting every frame is what the ring's own note
// warns about -- writing per event once slowed the frame enough to make a bug disappear while watched.
struct WhyOb  { unsigned target; unsigned short spell; unsigned expTick, selfExp, castMs, stamp;
                unsigned char fresh, mirrorSelf, aoe, evicted; };
struct WhyGrp { unsigned short spell; int allies, countHas, effN;
                unsigned char fresh, aoe, meHas, selfCast, hasLag, group, drop; };
struct WhyFm  { unsigned target; unsigned short spell, status; int copies, newer;
                unsigned char self, has, listReady, lost; };
static WhyOb  g_whyOb[64];  static int g_whyObN  = 0;
static WhyGrp g_whyGrp[32]; static int g_whyGrpN = 0;
static WhyFm  g_whyFm[64];  static int g_whyFmN  = 0;
#endif
static int fmN = 0;
static int g_lastRowN = 0;   // rows the last build produced (harness only)
// The IDENTITY of the cast an entry currently stands for -- the tick of the cast that put the buff there.
// Ally : the ob[] entry's castMs (the NEWEST of them, because two same-status songs are two entries and one of
// them can be pruned without any new cast having happened). Self : the self-cast ring, YOUR casts only.
// The two clocks differ (GetTickCount vs the FFXI tick) and are never compared to each other -- only an entry's
// own ref, of its own kind, to itself over time.
static unsigned fm_cast_ref(const FocusMem& e) {
    if (e.self) return party().self_cast_tick(e.status);
    int n = 0; const PartyState::OtherBuff* ob = party().other_buffs(n); unsigned t = 0;
    for (int i = 0; i < n; ++i)
        if (ob[i].target == e.target && ob[i].status == e.status && (!t || (int)(ob[i].castMs - t) > 0)) t = ob[i].castMs;
    return t;
}
static unsigned char fm_free_tag() {   // lowest number not in use : the list stays 1,2,3... as entries come and go
    for (unsigned char t = 1; t <= FOCUS_MAX; ++t) {   // one number per entry the monitor can hold : it stopped at 24 when the monitor grew to 64, so entries 25+ drew no number and no //aio out could name them
        bool used = false;
        for (int q = 0; q < fmN; ++q) if (fm[q].tag == t) { used = true; break; }
        if (!used) return t;
    }
    return 0;
}

// The tracked list, as text. NOT printed by default: the numbers are drawn on the rows themselves, and on a
// job like RDM a printed list would be twenty lines of chat to find one of them. Kept because it is the only
// way to see the monitor's contents when a row is NOT on screen -- clipped by Max per column, or hidden by a
// filter -- which is exactly the case a diagnosis needs.
int timers_focus_list(char out[][64], int max) {
    int n = 0;
    for (int q = 0; q < fmN && n < max; ++q) {
        const char* sp = fm[q].isAbil ? abil_name_by_id(fm[q].spell)
                                      : (spell_info(fm[q].spell) ? spell_info(fm[q].spell)->en : 0);
        _snprintf(out[n], 64, "%u. %s - %s%s", (unsigned)fm[q].tag, fm[q].self ? "you" : (fm[q].name[0] ? fm[q].name : "?"),
                  sp ? sp : buff_status_name(fm[q].status), fm[q].muted ? " (ignored)" : "");
        out[n][63] = 0; ++n;
    }
    return n;
}
// Forget entries. `a` / `b` are free-form: an index (1-based, as listed), "all", or a case-insensitive PREFIX
// of a person's name or of the spell's -- in either order, because the row on screen reads "Name - Spell" and
// nobody should have to remember which half comes first. Returns how many were forgotten.
static bool tf_pre(const char* hay, const char* pre) {
    if (!hay || !pre || !*pre) return false;
    for (int i = 0; pre[i]; ++i) {
        const char h = hay[i], q = pre[i];
        if (!h) return false;
        const char H = (h >= 'A' && h <= 'Z') ? (char)(h + 32) : h;
        const char Q = (q >= 'A' && q <= 'Z') ? (char)(q + 32) : q;
        if (H != Q) return false;
    }
    return true;
}
// Two songs can grant ONE status (Minuet IV and V, two Marches), so a row is found by (person, status, SPELL).
// A zero on either side means "unknown spell" -- food, gear, a cast we never saw -- and still matches on status,
// which is the behaviour every non-song buff had before songs forced the distinction.
static bool fm_same_row(const FocusMem& e, unsigned target, unsigned status, unsigned short spell, bool self) {
    if (e.self != (self ? 1 : 0) || e.status != status) return false;
    if (!self && e.target != target) return false;
    return e.spell == 0 || spell == 0 || e.spell == spell;
}
int fm_tag_of(unsigned target, unsigned status, unsigned short spell, bool self) {
    for (int q = 0; q < fmN; ++q)
        if (fm_same_row(fm[q], target, status, spell, self))
            return fm[q].muted ? 0 : fm[q].tag;   // muted -> no number : the row is no longer watched, and it SHOWS
    return 0;
}
// //aio out REMOVES THE LINE, it does not merely stop the alert. That is what the number is for -- a handle on
// a row you did not want -- so a row you take off has to actually go, name, timer and all. The buff itself is
// untouched (it is really on that person); what disappears is our decision to show it and to watch it.
bool fm_muted_for(unsigned target, unsigned status, unsigned short spell, bool self) {
    for (int q = 0; q < fmN; ++q)
        if (fm[q].muted && fm_same_row(fm[q], target, status, spell, self)) return true;
    return false;
}
// Does this entry answer to the argument pair? Shared by //aio out and //aio in, because a correction and its
// undo have to accept EXACTLY the same words -- anything you can type to take a row off, you can type to put it
// back, without learning a second vocabulary.
static bool fm_matches(const FocusMem& e, const char* a, const char* b) {
    if (a && (tf_pre("all", a) || tf_pre("tout", a))) return true;
    int idx = -1;   // a NUMBER is the entry's tag -- the one drawn on its row -- not a position in this array
    if (a && a[0] >= '0' && a[0] <= '9') { idx = 0; for (const char* c = a; *c >= '0' && *c <= '9'; ++c) idx = idx * 10 + (*c - '0'); }
    if (idx >= 0) return idx > 0 && e.tag == (unsigned char)idx;
    const char* sp = e.isAbil ? abil_name_by_id(e.spell)
                              : (spell_info(e.spell) ? spell_info(e.spell)->en : 0);
    const char* who = e.self ? "you" : e.name;
    const char* args[2] = { a, b };
    int need = 0, got = 0;
    for (int k = 0; k < 2; ++k) {
        if (!args[k] || !args[k][0]) continue;
        ++need;
        if (tf_pre(who, args[k]) || tf_pre(sp, args[k]) || tf_pre(buff_status_name(e.status), args[k])) ++got;
    }
    return (need > 0 && got == need);
}
// "alerts" is a THIRD thing to name, next to a number and a person : the rows that are shouting at you right
// now. //aio out all is the blunt instrument -- it takes off everything, including the timers that were doing
// their job -- and after a wipe or a long fight the only rows you actually want gone are the red ones.
// Checked BEFORE "all" and only from three letters, so "a" and "al" still mean all ("all" itself cannot match
// "alert" : the third letter differs). FR spelling accepted, like "tout".
static bool tf_alert_word(const char* a) {
    if (!a || !a[0] || !a[1] || !a[2]) return false;   // 1-2 letters stay "all" -- see above
    return tf_pre("alerts", a) || tf_pre("alertes", a);
}
int timers_focus_forget(const char* a, const char* b) {
    const bool alertsOnly = tf_alert_word(a);
    int hit = 0;
    for (int q = 0; q < fmN; ++q) {
        if (alertsOnly) { if (!fm[q].alerting) continue; }
        else if (!fm_matches(fm[q], a, b)) continue;
        fm[q].muted = 1; fm[q].lostMs = 0; fm[q].muteRef = fm_cast_ref(fm[q]); ++hit;   // the emit skips it ; it is dropped when its buff ends, or un-muted by a NEWER cast (muteRef)
    }
    return hit;
}
// //aio in -- THE UNDO. Everything above ends a mute on its own terms (the buff ends, or you cast it again), and
// both can be a long wait: a Haste you keep up on the wrong person never lapses, and a spell that will not
// overwrite what is already there records no new cast to lift it. This is the way back that costs nothing.
// The NO-ARGUMENT form means ALL, which `out` deliberately refuses -- and the asymmetry is the point: a muted
// entry draws no row, so it shows no number, and "everything I took off" is the only thing you can name from
// the screen. (`//aio out list` still prints them, marked "(ignored)", when you want just one of them back.)
// Only entries that were really muted are counted, so "nothing to put back" stays a distinct, honest answer.
int timers_focus_restore(const char* a, const char* b) {
    int hit = 0;
    for (int q = 0; q < fmN; ++q) {
        if (!fm[q].muted) continue;
        if (a && a[0] && !fm_matches(fm[q], a, b)) continue;
        fm[q].muted = 0; fm[q].lostMs = 0; fm[q].muteRef = 0; ++hit;   // watched again, with the number it already had
    }
    return hit;
}

void timers_reset() { party().buff_timers_clear(); party().other_buffs_clear(); ++g_tmResetGen; }

// //aio oblog -- ONE-FRAME dump of the whole ally-buff pipeline, in the three stages it actually has :
//   PRUNE  : what the model THREW AWAY before the drawer ever saw it, and the 0x076 evidence for each drop
//   MODEL  : what on_action recorded, per entry, with every field a later decision reads
//   GROUP  : what the entries were merged into, with the key that merged them and the timer chosen
//   ROW    : what was finally emitted, and by which branch
// A symptom -- "that row is missing", "that timer is wrong", "why are these two on one line" -- can come from
// any of the three, and no description can tell them apart. Guessing which one it was is how a good fix and a
// regression get written on the same afternoon. One capture answers it.
// Armed for a SINGLE frame : this runs at 60 Hz and the interesting state is one instant, not a stream.
static int g_obLog = 0;
// Also arms the MODEL-side prune trace (stage 0). The prune runs on the model tick, so it fires just before the
// next draw -> the OBPRUNE block lands directly above the OBLOG block in the same log, reading in pipeline order.
void timers_oblog_arm() { g_obLog = 1; party().arm_ob_prune_trace(); }
#define OBLOG(...) do { if (g_obLog) windower::debug::log(__VA_ARGS__); } while (0)

// Timers box : the SAME status-icon atlas as the Player / Party boxes (buff_atlas.raw : 1024x640, 32px cells, 32-col
// grid ; a status id maps to cell (id%32, id/32)).
// recast_id -> ability / spell NAME (linear scan of the generated tables ; only ever run for the few active recasts).
static const char* abil_name_by_recast(unsigned rid, const unsigned char* jaBits, bool jaOk) {
    // Many JAs share one recast_id. Three shapes : (a) cross-job SP abilities -- rc 254 holds all 20 SP2s (Clarion
    // Call / Brazen Rush / ...) and rc 0 holds all 20 SP1s (Soul Voice / Mighty Strikes / ...) -> EXACTLY ONE is
    // usable on any job, so pick it ; (b) same-job families sharing a timer (Blood Pacts, Steps, Rolls...) -> MANY
    // usable at once, and the wanted label is the family header = the first table row. So : exactly one usable ->
    // that ability ; otherwise the first row -- EXCEPT rc 0, whose first row (Mighty Strikes) would mislabel every
    // job, so bail there instead.
    // Shared-charge families whose FIRST table row is a real ability, not a header. SCH stratagems all share
    // recast_id 231 and Penury (id 215) is row one, so every stratagem -- Accession, Addendum, Manifestation... --
    // used to show "Penury" (reported). The recast is the charge-recharge timer, not any one stratagem, so the
    // honest label is the family name. (Blood Pacts / Steps / Rolls work by the first-row rule because their first
    // row genuinely IS the family header.)
    if (rid == 231) return "Stratagem";
    // INDEXED, like spell_name_by_recast below -- this used to walk all ABILS_N (626) rows for EVERY active recast,
    // EVERY frame: up to 40 x 626 row tests a frame. The answer is not a pure function of rid (it depends on the
    // job's usable-ability bitmap), but that bitmap only changes on a job change -- so cache the whole table and
    // rebuild it when the bitmap actually differs, not once per frame.
    struct Entry { const char* first; const char* only; int nUsable; };
    static Entry idx[4096];
    static unsigned char builtBits[128];
    static bool built = false, builtOk = false;
    if (!built || builtOk != jaOk || memcmp(builtBits, jaBits, 128) != 0) {
        built = true; builtOk = jaOk; memcpy(builtBits, jaBits, 128);
        for (int r = 0; r < 4096; ++r) { idx[r].first = 0; idx[r].only = 0; idx[r].nUsable = 0; }
        for (int i = 0; i < ABILS_N; ++i) {
            const unsigned r = ABILS[i].recast_id; if (r >= 4096) continue;
            if (!idx[r].first) idx[r].first = ABILS[i].en;
            if (jaOk) { const unsigned id = ABILS[i].id;
                if (id < 1024 && (jaBits[id >> 3] & (1u << (id & 7)))) { if (!idx[r].only) idx[r].only = ABILS[i].en; ++idx[r].nUsable; } }
        }
    }
    if (rid >= 4096) return 0;
    const char* first = idx[rid].first; const char* only = idx[rid].only; const int nUsable = idx[rid].nUsable;
    if (nUsable == 1) return only;
    if (jaOk && nUsable == 0 && first) return 0;   // NONE of this recast's abilities is usable by your job -> it's a cross-job
                                                   //   phantom slot the client reuses (e.g. DNC Chocobo Jig II drives slot 242 =
                                                   //   RUN Vivacious Pulse) -> don't show a wrong name ; the real jig sits on rc 218.
    return (rid == 0) ? 0 : first;
}
// INDEXED once, not scanned per frame. This walked all SPELLS_N (957) rows for EVERY active recast, every frame --
// up to 40 x 957 row tests a frame for a mapping that is constant (the table is generated and never changes).
// Pure function of rid, unlike abil_name_by_recast whose answer depends on the job's usable-ability bitmap.
static const char* spell_name_by_recast(unsigned rid) {
    static const char* idx[4096];
    static bool built = false;
    if (!built) {   // first row wins, exactly as the old linear scan did
        built = true;
        for (int i = 0; i < SPELLS_N; ++i) { const unsigned r = SPELLS[i].recast_id; if (r < 4096 && !idx[r]) idx[r] = SPELLS[i].en; }
    }
    return (rid < 4096) ? idx[rid] : 0;
}
static const char* abil_name_by_id(unsigned id) {   // for buffs-on-allies rows whose `spell` is an ABILITY (COR rolls)
    const AbilRow* a = abil_info(id); return a ? a->en : 0;   // abil_info = binary search (not a linear scan of all 626)
}


#ifdef AIOHUD_PROBES
// ---- //aio songdump : in-RAM record of the Timers rows (see hud.h). Fixed ring, no heap, no file I/O. ----
// 512 entries, because a row set that changes every frame burns them fast : one change costs 1 + nb lines,
// so 256 held barely twenty changes -- less than one second of the churn we are trying to watch.
static char  g_srRing[1024][200];   // the WHY lines are long, and one change now costs rows + groups + entries + focus
static int   g_srHead = 0, g_srCount = 0;
static void sr_push(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    _vsnprintf(g_srRing[g_srHead], sizeof(g_srRing[0]) - 1, fmt, ap);
    va_end(ap);
    g_srRing[g_srHead][sizeof(g_srRing[0]) - 1] = 0;
    g_srHead = (g_srHead + 1) % 1024; if (g_srCount < 1024) ++g_srCount;
}
void songrow_ring_dump() {
    const int start = (g_srHead - g_srCount + 1024) % 1024;
    windower::debug::log("SONGROW ======== %d recorded row-set change(s), oldest first ========", g_srCount);
    for (int i = 0; i < g_srCount; ++i) windower::debug::log("SONGROW %s", g_srRing[(start + i) % 256]);
    g_srHead = 0; g_srCount = 0;
}
#endif

// BRD song modifiers -> a compact tag " (SV NT M)" for the song's Timers row (bit0 Soul Voice, bit1 Nightingale,
// bit2 Troubadour, bit3 Marcato). Nightingale+Troubadour (the standard pair) merge to "NT" to stay short. 0 -> none.
static const char* song_mod_tag(unsigned char m, char* buf, int cap) {
    if (!m) return 0;
    char t[24]; int n = 0;
    auto add = [&](const char* s) { if (n && n < (int)sizeof(t) - 1) t[n++] = ' '; while (*s && n < (int)sizeof(t) - 1) t[n++] = *s++; };
    if (m & 1) add("SV");
    if (m & 8) add("M");   // SV and M ALWAYS before NT (else "NT M" reads badly in FR)
    if ((m & 2) && (m & 4)) add("NT"); else if (m & 2) add("N"); else if (m & 4) add("T");
    t[n] = 0;
    _snprintf(buf, cap, " (%s)", t); buf[cap - 1] = 0;
    return buf;
}
// Core renderer, extracted as a FREE function so the config PREVIEW and the Help sample reuse the EXACT same
// config-aware draw (fused / separate, icons / names, colours) with no Hud instance.
// Per-SPELL track keys for a self buff, with the status keys as fallback. The STATUS keys are deliberately shared
// by every spell granting the same buff (BLU Cocoon / Reactor Cool both give Defense Boost) -- the hide mirror is
// an AND over them, the focus mirror an OR -- so for one specific spell they answer the wrong question.
// selfBuffSpell_ remembers which spell actually produced the buff, which makes the per-entry key reachable.
// HIDE uses the per-spell key when known (an OR there would re-hide the sibling spell). FOCUS accepts EITHER key,
// matching the OR semantics the config panel already writes.
// The Timers buff filter is now JOB-AGNOSTIC and keyed purely by STATUS (the config writes c.tm_buff_off, one state
// per buff family, shared across every job). HIDDEN = tm_buff_off(status) ; FOCUS = tm_buff_off(TM_KEY_FOCUS|status).
// No recast / ally / status-mirror keys anymore, and recasts are no longer filtered (always shown). The `job` params
// are kept only so the many call sites don't all have to change their arguments.
// The `job` parameter is gone with the per-job track list it used to index (retired 2026-09-12, ui_config.h):
// an ignored parameter named `job` sitting in a focus test is an invitation to spend an hour looking for
// per-job behaviour that has not existed for months.
static bool tm_self_focus_on(const UiConfig& C, unsigned status) {
    return C.tm_buff_off(UiConfig::TM_KEY_FOCUS | status);
}
// SELF-CARRIED buffs (Food / Aftermath / conquest / Imagery) : model/timers_rules.h, timers_self_carried().

// //aio ftrace : armed for a DURATION, not a row count. A per-row countdown burned out in seconds -- it decrements
// once per buff PER FRAME (~60 Hz), so it never survived long enough to observe the one moment that matters, the
// buff expiring. Twice in a row the trace died at rem=89 then rem=19. Time-based, it always covers the whole life
// of a buff plus the alert window afterwards.
static unsigned s_focusUntil = 0;                  // GetTickCount deadline (0 = off)
static int      s_focusTrace = 0;                  // >0 while armed : the emit/prune traces gate on this
static unsigned short s_focusLastRem[1024] = { 0 };// last rem logged per status -> one line per SECOND, not per frame
void timers_focus_trace(int seconds) {
    s_focusUntil = GetTickCount() + (unsigned)seconds * 1000u;
    s_focusTrace = 1;
    for (int i = 0; i < 1024; ++i) s_focusLastRem[i] = 0xFFFF;
    const UiConfig& C = ui_config();
    windower::debug::log("=== FTRACE armed for %ds ===  mainJob=%d  tmFocusWarn=%ds  tmFocusHold=%ds",
                         seconds, party().self_main_job(), C.tmFocusWarn, C.tmFocusHold);
}
static bool focus_trace_live() {
    if (!s_focusTrace) return false;
    if ((int)(GetTickCount() - s_focusUntil) >= 0) { s_focusTrace = 0; windower::debug::log("=== FTRACE window closed ==="); return false; }
    return true;
}
// per-ROW gate : live AND this status' remaining time actually changed (one line per second per buff).
bool timers_focus_trace_armed() { return focus_trace_live(); }

// ============================ the row builder (was the top half of timers_draw) ============================
bool timers_build_rows(const GameState* game, bool preview, bool editing, TimersRows& rowsOut) {
    const UiConfig& C = ui_config();
    const struct { const GameState* game; } f = { game };   // the original body reads f.game ; kept verbatim
    using Row = TimersRow;
    // icon = buff status id ; both=1 -> icon+name (buff on an ally) ; order : 0 = your own buffs, 1+partyPos = allies.
    // COR roll : name = "Chaos Roll", pip = the coloured pip number (0 = none), post = " (AoE 6)" suffix -> drawn as
    // "Chaos Roll [5] (AoE 6)" with ONLY the pip in pipCol (unlucky=red, lucky/11=green, else white). nameCol overrides
    // the whole-name colour (unused now that only the pip is tinted).
    Row* const bufs = rowsOut.bufs; Row* const recs = rowsOut.recs; int nb = 0, nr = 0;   // was : static Row bufs[50], recs[50] (the caller owns the storage now)
    for (int i = 0; i < 50; ++i) { bufs[i].fineClk = recs[i].fineClk = FCLK_NONE; bufs[i].nameCol = recs[i].nameCol = 0; bufs[i].pip = recs[i].pip = 0; bufs[i].post = recs[i].post = 0; bufs[i].postCol = recs[i].postCol = 0; bufs[i].tag = recs[i].tag = 0; bufs[i].src = recs[i].src = 0; bufs[i].mark = recs[i].mark = 0; bufs[i].who = recs[i].who = 0; bufs[i].fine = recs[i].fine = TM_FINE_NONE; }   // clear per-frame overrides (static arrays)
    if (preview || editing) {
        static const struct { int id, rem; } SB[5] = { {43, 1490}, {57, 155}, {214, 309}, {40, 540}, {33, 28} };
        for (int i = 0; i < 5; ++i) { bufs[nb].rem = SB[i].rem; bufs[nb].icon = SB[i].id; bufs[nb].name = buff_status_name(SB[i].id); bufs[nb].order = 0; ++nb; }
        if (C.tmMine) {   // demo : an AoE song grouped for the whole party (you included) + a COR roll pip + a single-target buff on one ally
            bufs[nb].rem = 168;  bufs[nb].icon = 198; bufs[nb].name = "Minuet V"; bufs[nb].tag = " (SV NT)"; bufs[nb].tagCol = 0xFFE8C55Au; bufs[nb].post = " (AoE 6)"; bufs[nb].order = 0; ++nb;   // 198 = Minuet (was 43 = Refresh -- wrong icon)
            bufs[nb].rem = 280;  bufs[nb].icon = 313; bufs[nb].name = "Chaos Roll"; bufs[nb].pip = 11; bufs[nb].pipCol = 0xFF74D074u; bufs[nb].tag = " (CC)"; bufs[nb].tagCol = 0xFFE8C55Au; bufs[nb].post = " (AoE 6)"; bufs[nb].order = 0; ++nb;   // pip 11 = green, under Crooked Cards
            // Preview the display tiers : your ally-cast (11) sits above a player's buff on you (40) which sits above
            // a trust's (90), so the sample shows the "mine -> my ally-casts -> players grouped -> trusts last" order.
            bufs[nb].rem = 1200; bufs[nb].icon = 33;  bufs[nb].who = "Aeryn"; bufs[nb].name = "Haste"; bufs[nb].order = 11; ++nb;   // a buff YOU put on an ally (your ally-casts tier) ; 33 = Haste
            bufs[nb].rem = 540;  bufs[nb].icon = 33;  bufs[nb].name = "Haste"; bufs[nb].post = " (Aeryn)"; bufs[nb].postCol = 0xFF9AB0C8u; bufs[nb].order = 40; ++nb;   // a real PLAYER's buff on you -> grouped by that player
            bufs[nb].rem = 62;   bufs[nb].icon = 41;  bufs[nb].name = "Shell V"; bufs[nb].tag = " (SV)"; bufs[nb].tagCol = 0xFFE8C55Au; bufs[nb].post = " (Monberaux)"; bufs[nb].postCol = 0xFF9AB0C8u; bufs[nb].order = 90; ++nb;   // a TRUST's buff on you -> last
        }
        // Reflect the live BUFF FILTER in the preview : drop a demo buff the family filter HIDES, unless it's Hidden+focus
        // AND "low" (rem < warn) -- exactly what the real HUD does, so hiding a family empties it here too. (Recasts unfiltered.)
        { int w = 0;
          for (int r = 0; r < nb; ++r) {
              const unsigned st = (unsigned)bufs[r].icon;
              const bool focusLow = C.tm_buff_off(UiConfig::TM_KEY_FOCUS | st) && bufs[r].rem >= 0 && bufs[r].rem < C.tmFocusWarn;
              if (C.tm_buff_off(st) && !focusLow) continue;   // Hidden (and not a low Hidden+focus) -> filtered out of the preview
              if (w != r) bufs[w] = bufs[r];
              ++w;
          }
          nb = w;
        }
        static const struct { int icon, rem; const char* nm; } SR[4] = { {66, 8, "Mighty Strikes"}, {143, 22, "Haste"}, {160, 3, "Provoke"}, {56, 45, "Berserk"} };
        for (int i = 0; i < 4; ++i) { recs[nr].rem = SR[i].rem; recs[nr].icon = SR[i].icon; recs[nr].name = SR[i].nm; recs[nr].order = 0; ++nr; }
    } else {
        const unsigned now = ffxi_now_tick(), nowMs = model_now_ms();   // the model clock : ONE instant for the whole build (the caller brackets it as a model event)
        // the player's REAL current buffs (status ids read from memory ; the same list the Player Hub shows). Authoritative
        // and instant, unlike the 0x063 timer list which can keep a replaced Corsair roll. meHas(0) skips (padding).
        auto meHas = [&](int st) -> bool {
            // Fail open only when the read FAILED. An EMPTY list is a real answer ("you have no buffs") -- treating
            // nbuff==0 as "no data" made this return true for everything the moment your last buff expired, which is
            // exactly when the FOCUS monitor needs to see a buff go missing. Measured: nbuff=0 list=[] with has=1 on
            // 7783 consecutive samples, so the lost-buff alert could never fire. buffsOk splits the two cases.
            if (st == 0 || !f.game || !f.game->buffsOk) return true;
            for (int i = 0; i < f.game->nbuff; ++i) if ((int)f.game->buffs[i] == st) return true;
            return false;
        };
        // how many people ACTUALLY carry `st` right now : you (real buffs) + party members (their 0x076 icons). This is
        // the AoE count, robust vs the 0x028 target parse (songs/rolls only hit your party, so no alliance).
        auto countHas = [&](int st) -> int {
            // Count SELF only when your copy is actually YOUR cast. If someone re-cast the buff over you (an ally
            // re-Protects you over your own Majesty AoE), your copy is no longer part of THIS group -- counting it
            // kept a stale "(AoE 2)" and let your row FOLD into the group, hiding the real caster (only the timer
            // updated). buff_caster_for resolves the single live timer to the cast that produced it (see match_cast).
            const unsigned me = party().self_id();
            // Count SELF when ANY of your same-status timers resolves to YOUR cast. Two same-family songs (Honor +
            // Victory March, both 214) run TWO timers ; self_buff_expiry returned only the FIRST, so after a reload the
            // first timer (a song not re-sung yet -> its caster not re-learned) read "unknown", self went uncounted,
            // effN fell below 2, and the "(AoE N)" group never re-formed even after a recast -- the song drew as if it
            // were only on you. Scanning every same-status timer lets a SINGLE recast re-count self and repop the group,
            // while a single-instance buff an ally re-cast over you (Majesty Protect) still has its ONE timer resolve to
            // the ally -> correctly NOT counted.
            bool selfMine = false;
            if (meHas(st)) { int ntc = 0; const BuffTimer* btc = party().buff_timers(ntc);
                for (int t = 0; t < ntc; ++t) if (btc[t].id == st && party().buff_caster_for((unsigned short)st, btc[t].expiry, t) == me) { selfMine = true; break; } }
            int c = selfMine ? 1 : 0;
            for (int i = 0; i < party().count; ++i) {
                if (party().m[i].id == 0 || party().m[i].id == me) continue;
                const BuffSet* bs = party().buffs_for(party().m[i].id);
                if (bs) for (int j = 0; j < bs->n; ++j) if (bs->ids[j] == st) { ++c; break; }
            }
            return c;
        };

        // ---- pass 1 : group the buffs YOU cast on ALLIES by spell -> AoE groups (songs / rolls hit the whole party) ----
        struct AoeGrp { unsigned short spell, status; int allies, rem, fine; unsigned expTick; unsigned char isAbil, selfHas, aoe, fresh, selfCast; };   // expTick : the exact self-expiry the ally copies were frozen on -> identifies WHICH cast they came from ; fresh=1 : the ally copies track YOUR current cast ; fresh=0 : a LAGGARD group (members left on an older, shorter cast because a re-sing missed them) ; selfCast=1 : this cast ALSO landed on YOU (mirrorSelf) -> count yourself in the AoE total IMMEDIATELY, before your own 0x063/memory buff has landed (kills the ~1s post-cast "named-ally then group" flicker)
        static AoeGrp grp[32]; int ng = 0;
        int no = 0; const PartyState::OtherBuff* ob = 0;
        // Every countdown in this module rounds the SAME way -- up, like the client. An estimate that truncated
        // instead read a second LOW, so an ally row could show a smaller number than a row that actually expires
        // first : the two displayed values crossed, and the list re-ordered them back and forth once a second.
        // (The expTick branch was already ceil-ed for exactly this reason ; the ms estimate was the one left out.)
        auto ms_to_sec_ceil = [](int ms) -> int { return (ms > 0) ? (ms + 999) / 1000 : 0; };
        auto obRem = [&](const PartyState::OtherBuff& o) -> int {
            if (o.mirrorSelf) { int r = party().self_buff_remaining_for(o.status, o.spell); return (r >= 0) ? r : ms_to_sec_ceil((int)((o.startMs + o.durMs) - nowMs)); }   // per-SPELL : two same-status songs (Honor+Victory March) must not borrow each other's self timer
            if (o.expTick) return ticks_to_sec_ceil((int)(o.expTick - now));   // frozen on the exact self expiry (same FFXI clock as self rows) -- CEIL like the client, or this row reads 1 s under the self row right above it
            return ms_to_sec_ceil((int)((o.startMs + o.durMs) - nowMs));  // estimate -- CEIL like every other source (see ms_to_sec_ceil)
        };
        // The SAME remaining time in ticks, un-ceil-ed, for the sort only (Row::fine -- see the "yoyo" note there).
        // It must read the same three sources in the same order as obRem, or a row would sort by one clock and
        // display another. ms -> ticks is *3/50 (60/1000) ; the estimate is the only branch that has to convert.
        auto obFine = [&](const PartyState::OtherBuff& o) -> int {
            if (o.mirrorSelf) { const unsigned e = party().self_buff_expiry_for(o.status, o.spell); if (e) return (int)(e - now); return (int)((o.startMs + o.durMs) - nowMs) * 3 / 50; }
            if (o.expTick) return (int)(o.expTick - now);
            return (int)((o.startMs + o.durMs) - nowMs) * 3 / 50;
        };
        // FRESH vs LAGGARD : model/timers_rules.h (ally_copy_fresh). The adapter below answers its questions from the
        // model, each only when the rule asks it -- the same evaluation order the inline code had.
        struct FreshSrc {
            const PartyState::OtherBuff& e;
            bool isAbil() const { return e.isAbil != 0; }
            bool aoe() const { return e.aoe != 0; }
            bool mirrorSelf() const { return e.mirrorSelf != 0; }
            unsigned expTick() const { return e.expTick; }
            unsigned castMs() const { return e.castMs; }
            unsigned selfExp() const { return party().self_buff_expiry_for(e.status, e.spell); }
            bool evictedRecently() const { return party().song_was_evicted(party().self_id(), e.spell, 6000u); }
            unsigned timersStamp() const { return party().buff_timers_stamp(); }
            bool zoneGrace() const { return party().in_zone_grace(); }
            bool isSong() const { return song_family(e.spell) > 0; }
        };
        auto obFresh = [](const PartyState::OtherBuff& o) -> bool { return ally_copy_fresh(FreshSrc{ o }); };
        if (C.tmMine) {
            ob = party().other_buffs(no);   // (prune_other_buffs_worn now runs once per frame from the model tick, not from here)
            if (no > g_obPeak) g_obPeak = no;   // the peak is what the watcher can act on -- see g_fmPeak/g_obPeak
            if (g_obLog) {   // ---- stage 1 : the MODEL, before any grouping decision touches it ----
                windower::debug::log("=== OBLOG : %d ally entr(ies) in the model ===", no);
                for (int i = 0; i < no; ++i) {
                    const PartyState::OtherBuff& o = ob[i];
                    const char* sp = o.isAbil ? abil_name_by_id(o.spell) : (spell_info(o.spell) ? spell_info(o.spell)->en : 0);
                    const unsigned selfExp = party().self_buff_expiry_for(o.status, o.spell);
                    const BuffSet* bs = party().buffs_for(o.target);
                    bool has = false; if (bs) for (int j = 0; j < bs->n; ++j) if (bs->ids[j] == o.status) { has = true; break; }
                    windower::debug::log("  MODEL[%d] %-16s %-18s st=%-4u aoe=%d mirrorSelf=%d fresh=%d | est=%ds (start+%ums dur=%ums) expTick=%u selfExp=%u dTick=%d | buffset=%s has=%d",
                                         i, o.name[0] ? o.name : "<no name>", sp ? sp : "<unknown spell>", o.status,
                                         o.aoe, o.mirrorSelf, obFresh(o) ? 1 : 0,
                                         obRem(o), o.startMs, o.durMs, o.expTick, selfExp,
                                         (o.expTick && selfExp) ? (int)(o.expTick - selfExp) : 0,
                                         bs ? "yes" : "NONE", has ? 1 : 0);
                }
            }
            for (int i = 0; i < no; ++i) {
                const int r = obRem(ob[i]);
                if (r <= 0) continue;
                const unsigned char fb = obFresh(ob[i]) ? 1 : 0;
#ifdef AIOHUD_PROBES
                if (g_whyObN < 64) { WhyOb& w = g_whyOb[g_whyObN++];
                    w.target = ob[i].target; w.spell = ob[i].spell; w.expTick = ob[i].expTick;
                    w.selfExp = party().self_buff_expiry_for(ob[i].status, ob[i].spell);
                    w.castMs = ob[i].castMs; w.stamp = party().buff_timers_stamp();
                    w.fresh = fb; w.mirrorSelf = ob[i].mirrorSelf; w.aoe = ob[i].aoe;
                    w.evicted = party().song_was_evicted(party().self_id(), ob[i].spell, 6000u) ? 1 : 0; }
#endif   // FRESH (your current cast) vs LAGGARD (an older cast a re-sing missed) -- see obFresh
                // Key on (spell, freshness, AND delivery). A single-target re-cast is a DIFFERENT cast from the
                // AoE that preceded it : it has its own, longer timer. Keyed only by (spell, fresh) the two
                // merged, because refreshing an ally's slot clears its `aoe` flag and obFresh() calls any
                // single-target entry fresh -- so an AoE Protect on the party followed by a Protect on one ally
                // drew ONE "(AoE 3)" row carrying the SELF timer, and the ally's newer duration was invisible.
                // Single-target casts form their own bucket ; they simply never merge
                // with an AoE generation again.
                int gi = -1; for (int k = 0; k < ng; ++k) if (grp[k].spell == ob[i].spell && grp[k].fresh == fb && grp[k].aoe == ob[i].aoe) { gi = k; break; }
                if (gi < 0 && ng < 32) { gi = ng++; grp[gi].spell = ob[i].spell; grp[gi].status = ob[i].status; grp[gi].allies = 0; grp[gi].rem = r; grp[gi].fine = obFine(ob[i]); grp[gi].isAbil = ob[i].isAbil; grp[gi].selfHas = 0; grp[gi].aoe = ob[i].aoe; grp[gi].expTick = ob[i].expTick; grp[gi].fresh = fb; grp[gi].selfCast = 0; }
                if (gi >= 0) { grp[gi].allies++; grp[gi].selfCast |= ob[i].mirrorSelf; const int rf = obFine(ob[i]); if (r < grp[gi].rem || (r == grp[gi].rem && rf < grp[gi].fine)) { grp[gi].rem = r; grp[gi].fine = rf; } }   // aoe is part of the KEY now, no longer OR-ed in ; selfCast |= mirrorSelf = the cast hit you too (still fresh, < 2s) ; rem = SHORTEST in the group
            }
        }

        // Per-frame tag buffers for pass 2. Sized for the ROW count, not for songs: this pool started as "song
        // modifier tags for solo songs" (a handful), then also became the "(caster name)" tag for every buff someone
        // else put on us. With a full trust party that is 20+ rows, and at 8 slots the overflow silently dropped the
        // owner name off most of them -- reported as "plein de buff sans porteur" while the model had them all.
        // 32 chars holds the JA tag AND the owner: " (SV NT) (Fifteencharname)" + NUL.
        // Sized to bufs[] capacity ON PURPOSE : this pool must never be the thing that runs out. It was 8 (song
        // modifier tags only), and when it also became the "(caster name)" tag it silently dropped the owner off
        // most rows. Raising it to 32 only moved the cliff -- 50 is the row cap, so the tag can always be written.
        static char selfTag[50][32]; int stN = 0;
        const int stMax = (int)(sizeof(selfTag) / sizeof(selfTag[0]));
        // Which JAs THIS job can use (self-cast filter + shared recast_id disambiguation). Taken from the frame
        // SNAPSHOT: read_usable_ja_bits makes two indirect calls into the client's own resource manager, and doing
        // that from draw() put client code on the render path 60 times a second for a value that only changes on a
        // job change. The poller reads it now (rule 6).
        static const unsigned char JA_NONE[128] = {};
        const unsigned char* jaBits = f.game ? f.game->jaBits : JA_NONE;
        const bool jaOk = f.game ? f.game->jaOk : false;
        // THE BUFF-SOURCE FILTER : model/timers_rules.h (buff_source_keeps) -- one verdict for the row emit AND the FOCUS
        // monitor. The adapter answers the rule's questions from the model, each only when asked.
        struct SrcAdapter {
            const UiConfig& C; unsigned short status; unsigned expiry; int timerIdx; const unsigned char* jaBits; bool jaOk;
            int filter() const { return C.tmBuffSrc; }
            bool selfCarried() const { return timers_self_carried(status); }
            bool foreignStatMix() const { return party().is_foreign_stat_mix(status, expiry, timerIdx); }
            unsigned caster() const { return party().buff_caster_for(status, expiry, timerIdx); }
            unsigned me() const { return party().self_id(); }
            bool isTrust(unsigned id) const { return party().is_trust(id); }
            bool selfCanProduce() const { return party().self_can_produce_buff(status, jaBits, jaOk); }
            void sourceJobs(bool& players, bool& trusts) const { party().buff_source_jobs(status, players, trusts); }
        };
        auto srcKeeps = [&](unsigned short status, unsigned expiry, int timerIdx) -> bool {
            return buff_source_keeps(SrcAdapter{ C, status, expiry, timerIdx, jaBits, jaOk });
        };
        const int trkJob = party().self_main_job();   // 0 = not logged in yet ; the FOCUS monitor below is gated on it
        // ---- pass 2 : your OWN self buffs (exact server timers). A self buff that matches an AoE group you cast folds
        //      INTO that group (you count, your exact timer drives it) instead of getting its own row. ----
        int n = 0; const BuffTimer* bt = party().buff_timers(n);
        for (int i = 0; i < n && nb < 50; ++i) {
            // THE COUNTDOWN : model/timers_rules.h (self_timer_countdown) -- permanent, absurd, held at 0:00, debuff.
            int fine = (int)(bt[i].expiry - now); if (fine < 0) fine = 0;   // the same instant, un-ceil-ed -> sort key (Row::fine)
            using SelfMeHasFn = decltype(meHas); using SelfSrcKeepsFn = decltype(srcKeeps);   // aliases : a member named like the lambda cannot also use its name
            struct CountdownSrc {
                const BuffTimer& t; unsigned now; const SelfMeHasFn* me;
                bool permanent() const { return t.expiry == FFXI_EXPIRY_PERMANENT; }
                int remSec() const { return ticks_to_sec_ceil((int)(t.expiry - now)); }
                bool selfCarried() const { return timers_self_carried(t.id); }
                bool meHas() const { return (*me)((int)t.id); }
                bool isDebuff() const { return is_debuff_status(t.id); }
            };
            int rem = self_timer_countdown(CountdownSrc{ bt[i], now, &meHas });
            if (rem < 0) continue;
            if (party().bcapt_armed() && bt[i].id < 1024) {   // //aio bcaptlog ATTR : the FULL ownership decision per self-buff (logged on CHANGE only)
                const unsigned owner = party().buff_caster_for(bt[i].id, bt[i].expiry, i);   // what buff_caster_for resolves (ring -> direct -> co-expiry)
                const unsigned direct = party().buff_caster(bt[i].id);                        // raw buffCaster_[status]
                const bool selfProd = party().self_can_produce_buff(bt[i].id, jaBits, jaOk);  // can YOUR current job even make this ?
                static unsigned char al[1024] = { 0 };
                const unsigned char pk = (unsigned char)(1 + (owner == party().self_id() ? 2 : 0) + (owner && party().is_trust(owner) ? 4 : 0) + (direct ? 8 : 0) + (selfProd ? 16 : 0) + (owner ? 32 : 0));
                if (al[bt[i].id] != pk) { al[bt[i].id] = pk;
                    windower::debug::log("ATTR st=%-4u exp=%u owner=%08X trust=%d direct=%08X ring=%d spell=%-5u selfProd=%d meHas=%d self=%08X",
                                         bt[i].id, bt[i].expiry, owner, owner ? (int)party().is_trust(owner) : -1, direct,
                                         party().self_cast_ring_count(bt[i].id), party().self_buff_spell_ranked(bt[i].id, bt[i].expiry, i),
                                         selfProd ? 1 : 0, meHas((int)bt[i].id) ? 1 : 0, party().self_id());
                }
            }
            // Hidden (Unfollow) -- UNLESS it's Unfollow-Focus & expiring : then it pops under the warn threshold.
            // Key on the SPELL that produced this buff when we know it. The status key is deliberately an AND over
            // every spell sharing that status (BLU Cocoon / Reactor Cool both give Defense Boost), so hiding ONLY
            // Cocoon never set it -- its recast row hid but its BUFF row stayed visible until Reactor Cool was
            // hidden too, which read as "Hidden+Focus does nothing". The per-entry recast key is always written by
            // the config panel (tm_config.cpp entHiddenSet), so it is the precise answer; fall back to the status
            // key for buffs with no known source spell (abilities, food, gear).
            // throttle : only when this status' remaining SECOND changed, else 60 identical lines a second
            bool ftrace = false;
            if (timers_focus_trace_armed() && bt[i].id < 1024) {
                const unsigned short r16 = (unsigned short)(rem < 0 ? 0 : (rem > 65000 ? 65000 : rem));
                if (s_focusLastRem[bt[i].id] != r16) { s_focusLastRem[bt[i].id] = r16; ftrace = true; }
            }
            if (ftrace) {
                windower::debug::log("FOCUS status=%u '%s' rem=%d  hidden=%d  focus=%d  warn=%d",
                                     (unsigned)bt[i].id, buff_status_name(bt[i].id), rem,
                                     C.tm_buff_off((unsigned)bt[i].id) ? 1 : 0,
                                     C.tm_buff_off(UiConfig::TM_KEY_FOCUS | (unsigned)bt[i].id) ? 1 : 0, C.tmFocusWarn);
            }
            // SHOWN OR FILTERED : model/timers_rules.h (self_timer_shown) -- family filter, GEO aura noise, source, stale 0x063.
            struct ShownSrc {
                const BuffTimer& t; int idx; int rem; const UiConfig& cfg; const SelfSrcKeepsFn* src; const SelfMeHasFn* me;
                unsigned status() const { return t.id; }
                int remSec() const { return rem; }
                bool hidden() const { return cfg.tm_buff_off((unsigned)t.id); }
                bool focusOn() const { return tm_self_focus_on(cfg, t.id); }
                int warnSec() const { return cfg.tmFocusWarn; }
                bool geoAuraTracked() const { return party().geo_aura_remaining(t.id) >= 0; }
                bool sourceKeeps() const { return (*src)(t.id, t.expiry, idx); }
                bool meHas() const { return (*me)((int)t.id); }
            };
            if (!self_timer_shown(ShownSrc{ bt[i], i, rem, C, &srcKeeps, &meHas })) continue;
            const unsigned ssid = party().self_buff_spell_ranked(bt[i].id, bt[i].expiry, i);   // spell/tier (Valor Minuet V), disambiguating same-status buffs by expiry rank
            int sameSt = 0; for (int k = 0; k < n; ++k) if (bt[k].id == bt[i].id) ++sameSt;
            // WHICH GROUP, AND WHETHER YOUR ROW FOLDS INTO IT : model/timers_rules.h (self_timer_group, self_timer_folds).
            struct GroupsView { const AoeGrp* g;
                unsigned spell(int k) const { return g[k].spell; } unsigned status(int k) const { return g[k].status; }
                bool fresh(int k) const { return g[k].fresh != 0; } bool aoe(int k) const { return g[k].aoe != 0; } };
            const int gi = self_timer_group(GroupsView{ grp }, ng, ssid, bt[i].id, sameSt);
            const bool allyHides = C.tm_buff_off((unsigned)bt[i].id)   // one state per buff : the ally copy honours the same family filter
                                   && !(C.tm_buff_off(UiConfig::TM_KEY_FOCUS | (unsigned)bt[i].id) && rem < C.tmFocusWarn);
            bool hasLag = false; for (int k = 0; k < ng; ++k) if (grp[k].spell == ssid && !grp[k].fresh) { hasLag = true; break; }
            using CountHasFn = decltype(countHas); using MeHasFn = decltype(meHas);
            struct FoldSrc {
                const AoeGrp* g; int gi; bool lag; bool hides; unsigned st; const CountHasFn* cnt; const MeHasFn* me;
                bool haveGroup() const { return gi >= 0; }
                int groupAllies() const { return g[gi].allies; }
                bool groupAoe() const { return g[gi].aoe != 0; }
                bool hasLaggard() const { return lag; }
                bool meHas() const { return (*me)((int)st); }
                int countHas() const { return (*cnt)((int)st); }
                bool allyHides() const { return hides; }
            };
            const bool folds = self_timer_folds(FoldSrc{ grp, gi, hasLag, allyHides, bt[i].id, &countHas, &meHas });
            // THROTTLE : this runs per timer per FRAME. Unthrottled it is 60 Hz x the whole window -- the log would be
            // useless and huge. Log a status only when its verdict or its whole-second countdown actually changed.
            static unsigned sfKey[32] = { 0 }; static int sfRem[32] = { 0 }; static int sfFold[32] = { 0 };
            const int sfi = i & 31; const unsigned sfk = ((unsigned)bt[i].id << 16) ^ bt[i].expiry;
            const bool sfNew = (sfKey[sfi] != sfk) || (sfRem[sfi] != rem) || (sfFold[sfi] != (folds ? 1 : 0));
            if (sfNew) { sfKey[sfi] = sfk; sfRem[sfi] = rem; sfFold[sfi] = folds ? 1 : 0; }
            if (sfNew && party().bcapt_armed()) {   // //aio bcaptlog : the fold VERDICT, success path included -- two consecutive
                const SpellRow* dsp = spell_info(ssid);   // FOLDED lines sharing one gi = the double-fold that eats a row
                windower::debug::log("SONGFOLD st=%-4u exp=%u rem=%-5d ssid=%-5u \"%s\" sameSt=%d ringN=%d gi=%d countHas=%d aoe=%d allyGroup=%d allyHides=%d -> %s",
                                     bt[i].id, bt[i].expiry, rem, ssid, (dsp && dsp->en) ? dsp->en : "?",
                                     sameSt, party().self_cast_ring_count(bt[i].id), gi, countHas(bt[i].id),
                                     (gi >= 0) ? grp[gi].aoe : -1, 0, allyHides ? 1 : 0,
                                     folds ? "FOLDED (self row suppressed)" : "OWN ROW");
            }
            if (folds) { grp[gi].selfHas = 1; grp[gi].rem = rem; grp[gi].fine = fine; continue; }
            const SpellRow* ssp = spell_info(ssid);
            bufs[nb].rem = rem; bufs[nb].fine = fine; bufs[nb].fineClk = FCLK_SELF; bufs[nb].icon = bt[i].id; bufs[nb].name = (ssp && ssp->en) ? ssp->en : buff_status_name(bt[i].id);
            // BAND : what YOU cast (0), then what real PLAYERS put on you (1), then TRUSTS (2). Each band is still
            // sorted soonest-first by the comparator below. "Yours" is the per-timer caster, not "it is on me".
            const unsigned rowCaster = party().buff_caster_for(bt[i].id, bt[i].expiry, i);
            // OWNER RESOLUTION, self-reporting. Reported 2026-07-26 : Protect / Shell / Regen actually cast by the
            // trust Monberaux display "(Kaories)" -- a COR, a job that cannot produce any of those three -- so the
            // row passes the "me + players" filter under a player's name. Neither of the two mechanisms I reasoned
            // through explains it: the per-status latch is only ever written by a cast that GRANTS that status, and
            // a Corsair grants none of them. So the resolution itself has to be observed rather than argued about.
            // One line per DISTINCT (status, resolved caster), always-on and deduped -> a handful of lines a
            // session, no probe to arm, and it survives the //unload+//load that wipes the log.
            {
                // MEASURED 2026-07-27 : this emitted 103 380 lines for 184 distinct ones in one session. The old
                // hand-rolled set stopped RECORDING at 48 but kept LOGGING, so past saturation every row wrote two
                // lines every frame -- each one a CreateFile/WriteFile/CloseHandle on the render thread. LogOnce
                // goes quiet instead, and says so once. (windower_debug.h carries the full note.)
                static windower::debug::LogOnce<48> onceOwn;
                const unsigned key = ((unsigned)bt[i].id << 20) ^ rowCaster;
                if (onceOwn.first(key)) {
                    const char* onm = rowCaster ? party().pc_name_by_id(rowCaster) : 0;
                    unsigned latch = 0; const unsigned ring = party().caster_paths(bt[i].id, bt[i].expiry, i, latch);
                    const char* rnm = ring ? party().pc_name_by_id(ring) : 0; const char* lnm = latch ? party().pc_name_by_id(latch) : 0;
                    windower::debug::log("BUFFPATH st=%u ring=%08X '%s'  latch=%08X '%s'  -> resolved=%08X",
                                         (unsigned)bt[i].id, ring, rnm ? rnm : "-", latch, lnm ? lnm : "-", rowCaster);
                    windower::debug::log("BUFFOWNER st=%u '%s' exp=%u idx=%d -> caster=%08X '%s' trust=%d mine=%d | srcKeeps=%d (filter=%d)",
                                         (unsigned)bt[i].id, buff_status_name(bt[i].id), bt[i].expiry, i,
                                         rowCaster, onm ? onm : "<unresolved>",
                                         rowCaster ? (party().is_trust(rowCaster) ? 1 : 0) : -1,
                                         (rowCaster == party().self_id()) ? 1 : 0,
                                         srcKeeps(bt[i].id, bt[i].expiry, i) ? 1 : 0, (int)C.tmBuffSrc);
                }
            }
            const bool rowMine = (rowCaster == 0 || rowCaster == party().self_id());   // for TAGS: unknown is probably ours
            // THE BAND : model/timers_rules.h (self_row_band) -- yours, unknown, a player's (by player), a trust's (last).
            bufs[nb].order = self_row_band(rowCaster, party().self_id(), rowCaster ? party().is_trust(rowCaster) : false, party().party_order(rowCaster));
            // Owner name for anything you did not cast -- rolls included. This used to live only in the song branch,
            // so a party COR's roll on you showed its pips with no idea whose roll it was.
            const char* rowOwner = rowMine ? 0 : party().pc_name_by_id(rowCaster);
            PartyState::RollInfo sri = party().roll_info(bt[i].id);   // COR roll : "Chaos Roll [7]", pip tinted by luck
            if (sri.value) {
                bufs[nb].pip = sri.value; bufs[nb].pipCol = (sri.luck == 2) ? 0xFFF06060u : (sri.luck == 1 || sri.value == 11) ? 0xFF74D074u : 0xFFEAF0FFu;
                if (sri.cc) { bufs[nb].tag = " (CC)"; bufs[nb].tagCol = 0xFFE8C55Au; }   // amber, like the song JA tags
                if (rowOwner && rowOwner[0] && stN < stMax) { _snprintf(selfTag[stN], sizeof(selfTag[0]) - 1, " (%s)", rowOwner); selfTag[stN][sizeof(selfTag[0]) - 1] = 0;
                    bufs[nb].post = selfTag[stN]; bufs[nb].postCol = 0xFF9AB0C8u; ++stN; }
            }
            else {
                // Song JA tags (SV/NT/TR/M) describe OUR buffs at OUR cast time -- only ever put them on a row we cast.
                // They were leaking onto trust songs that shared a status, printing "Honor March MNT" twice.
                // JA tags AND the owner on the same row. song_mods() is now filled for OTHER casters too (from the
                // 0x076 party-buff cache), so a song a party BRD puts on you can show "(SV NT) (Tetsouo)" -- you can
                // finally tell a Soul Voice'd march from a plain one when you are not the one singing.
                // TWO segments : the JA tag says WHAT was up at cast time (amber, same as your own rows), the owner
                // says WHO cast it (grey, secondary). One colour for both made the name compete with the tag.
                char mtag[16]; const char* tg = 0;
                { const unsigned char sm = party().song_mods(ssid); if (sm) tg = song_mod_tag(sm, mtag, sizeof(mtag));
                  // read-back probe : proves whether the DISPLAY sees what the packet path wrote, and under which
                  // spell id. Throttled to the SONGFOLD gate so it cannot flood at 60 Hz.
                  if (sfNew && party().bcapt_armed())
                      windower::debug::log("SONGTAG: st=%u ssid=%u mods=%02X mine=%d owner=%s",
                                           (unsigned)bt[i].id, ssid, sm, rowMine ? 1 : 0, rowOwner ? rowOwner : "-"); }
                if (tg && stN < stMax) { _snprintf(selfTag[stN], sizeof(selfTag[0]) - 1, "%s", tg); selfTag[stN][sizeof(selfTag[0]) - 1] = 0;
                    bufs[nb].tag = selfTag[stN]; bufs[nb].tagCol = 0xFFE8C55Au; ++stN; }
                if (rowOwner && rowOwner[0] && stN < stMax) { _snprintf(selfTag[stN], sizeof(selfTag[0]) - 1, " (%s)", rowOwner); selfTag[stN][sizeof(selfTag[0]) - 1] = 0;
                    bufs[nb].post = selfTag[stN]; bufs[nb].postCol = 0xFF9AB0C8u; ++stN; }
            }
            if (fm_muted_for(0, bt[i].id, (unsigned short)ssid, true)) continue;   // taken off by //aio out : no row at all
            bufs[nb].src = 2;   // pass 2 : your OWN 0x063 buff timer
            bufs[nb].mark = fm_tag_of(0, bt[i].id, (unsigned short)ssid, true);
            ++nb;
        }
        {   // GEO : the single Indi- you carry -> a stable row at the COMPUTED aura lifetime (base+JP+gear), not the 3s pulse
            const PartyState::GeoAura& ga = party().self_geo();
            int gr = ga.status ? party().geo_aura_remaining(ga.status) : -1;
            if (gr > 0 && nb < 50) {
                const SpellRow* gsp = spell_info(ga.spell);
                if (ga.expTick) { bufs[nb].fine = (int)(ga.expTick - now); bufs[nb].fineClk = FCLK_SELF; }   // same instant as geo_aura_remaining, un-ceil-ed -> sort key
                bufs[nb].rem = gr; bufs[nb].icon = ga.status; bufs[nb].name = (gsp && gsp->en) ? gsp->en : buff_status_name(ga.status); bufs[nb].order = 0; bufs[nb].src = 3; ++nb;   // GEO aura
            }
        }

        // ---- pass 3 : emit the buffs you cast on allies, COUNTED + VALIDATED against everyone's REAL buffs. So the AoE
        //      count matches who actually got it (not our 0x028 target parse -- BattleMod-accurate) and a replaced roll
        //      nobody carries drops. total >= 2 -> one "Spell (AoE N)" row (N counts you) ; exactly 1 -> "Person - Spell". ----
        if (g_obLog) {   // ---- stage 2 : the GROUPS the entries were merged into, and the key that merged them ----
            windower::debug::log("=== OBLOG : %d group(s) formed  [key = spell + fresh + aoe] ===", ng);
            for (int k = 0; k < ng; ++k) {
                const char* sp = grp[k].isAbil ? abil_name_by_id(grp[k].spell) : (spell_info(grp[k].spell) ? spell_info(grp[k].spell)->en : 0);
                const int selfRem = party().self_buff_remaining_for(grp[k].status, grp[k].spell);
                windower::debug::log("  GROUP[%d] %-18s st=%-4u fresh=%d aoe=%d allies=%d selfHas=%d selfCast=%d | rem(shortest est)=%ds selfRem=%ds -> displayed=%ds",
                                     k, sp ? sp : "<unknown spell>", grp[k].status, grp[k].fresh, grp[k].aoe,
                                     grp[k].allies, grp[k].selfHas, grp[k].selfCast,
                                     grp[k].rem, selfRem, (grp[k].aoe && selfRem > 0) ? selfRem : grp[k].rem);
            }
        }
        static char obLabel[64][44], tagBuf[64][16];
        const bool graceOB = party().in_zone_grace();   // just zoned : the real 0x076 caches are still refilling -> trust the estimate, don't validate
        if (C.tmMine) for (int k = 0; k < ng && nb < 50; ++k) {
            const bool lag = !grp[k].fresh;   // LAGGARD group : members left on an older/shorter cast a re-sing missed
            // Count. For a FRESH group the "(AoE N)" number comes from its own ob[] bucket when a laggard sibling exists
            // (countHas / 0x076 can't tell an old copy from a fresh one, so it would re-absorb the laggard back into the
            // AoE count -- the exact merge we're undoing). For a LAGGARD group effN is just the draw-guard : how many
            // un-refreshed members there are to LIST by name below (they don't share a count row).
            // THE COUNT AND THE GROUPING DECISION LIVE IN model/ally_group.h -- pure, and covered by
            // tests/t_allygroup.cpp, whose cases were each verified to FAIL against the branch they protect
            // before being kept. Everything here is now the gathering of its inputs.
            GroupIn gin;
            gin.fresh           = grp[k].fresh != 0;
            gin.allies          = grp[k].allies;
            gin.aoe             = grp[k].aoe != 0;
            gin.selfCast        = grp[k].selfCast != 0;
            gin.meHas           = meHas(grp[k].status);
            gin.countHas        = countHas(grp[k].status);
            gin.hasLagSameSpell = false;
            for (int j = 0; j < ng; ++j) if (grp[j].spell == grp[k].spell && !grp[j].fresh) { gin.hasLagSameSpell = true; break; }
            const GroupOut gout = ally_group_verdict(gin);
            const int effN = gout.effN;
            if (gout.drop) continue;                        // nobody has it anymore (worn / replaced roll) -> drop the group
            if (C.tm_buff_off((unsigned)grp[k].status)) {   // hidden buff (family filter) -- UNLESS Hidden+Focus & expiring (pops under the warn threshold)
                if (!(C.tm_buff_off(UiConfig::TM_KEY_FOCUS | (unsigned)grp[k].status) && grp[k].rem < C.tmFocusWarn)) continue;
            }
            const char* en = grp[k].isAbil ? abil_name_by_id(grp[k].spell) : (spell_info(grp[k].spell) ? spell_info(grp[k].spell)->en : 0);   // rolls -> ability name ; spells -> spell name
            int fine = grp[k].fine; unsigned char fclk = FCLK_EST;   // sort key, kept in lock-step with `rem` just below -- and with the clock it was read from
            int rem = grp[k].rem;   // used by the GROUP branch only (fresh) ; the per-ally branch reads each member's own obRem
            if (grp[k].aoe) { int sr = party().self_buff_remaining_for(grp[k].status, grp[k].spell); if (sr > 0) { rem = sr; const unsigned se = party().self_buff_expiry_for(grp[k].status, grp[k].spell); if (se) { fine = (int)(se - now); fclk = FCLK_SELF; } } }   // a REAL AoE shares your exact self 0x063 timer -- for the SPECIFIC song (two Marches run two 214 timers ; borrowing the first showed Victory March with Honor's countdown). A single-target buff you ALSO have on yourself keeps the ally estimate (else Haste on an ally shows YOUR self-Haste duration)
            // GROUP into one "(AoE N)" row when : it was a REAL AoE cast (Protectra / a spell under SCH Accession /
            // a roll) OR the user keeps "group ally buffs" on. Otherwise (single-target spread) -> one row PER ally.
            // A LAGGARD group NEVER groups : it lists each un-refreshed person by NAME (Kaories, Gab, ...) on their own
            // timer in the per-ally branch below -- that named-per-person listing is the whole point of the split.
            const bool group = gout.group;
#ifdef AIOHUD_PROBES
            if (g_whyGrpN < 32) { WhyGrp& w = g_whyGrp[g_whyGrpN++];
                w.spell = grp[k].spell; w.allies = gin.allies; w.countHas = gin.countHas; w.effN = gout.effN;
                w.fresh = gin.fresh ? 1 : 0; w.aoe = gin.aoe ? 1 : 0; w.meHas = gin.meHas ? 1 : 0;
                w.selfCast = gin.selfCast ? 1 : 0; w.hasLag = gin.hasLagSameSpell ? 1 : 0;
                w.group = group ? 1 : 0; w.drop = gout.drop ? 1 : 0; }
#endif
            if (party().bcapt_armed()) {   // //aio bcaptlog OBGRP : the group-vs-per-ally verdict + its inputs -- WHY an Accession buff draws (AoE N) or a named per-ally row. Throttled to CHANGE (else 60 Hz flood).
                static unsigned short olS[32]; static unsigned char olF[32]; static short olN[32], olG[32]; static bool olInit = false;
                if (!olInit) { olInit = true; for (int q = 0; q < 32; ++q) { olS[q] = 0xFFFF; olF[q] = 0xFF; olN[q] = -1; olG[q] = -1; } }
                const int oidx = k & 31;
                if (olS[oidx] != grp[k].spell || olF[oidx] != grp[k].fresh || olN[oidx] != (short)effN || olG[oidx] != (short)(group ? 1 : 0)) {
                    olS[oidx] = grp[k].spell; olF[oidx] = grp[k].fresh; olN[oidx] = (short)effN; olG[oidx] = (short)(group ? 1 : 0);
                    windower::debug::log("OBGRP spell=%u \"%s\" st=%u fresh=%d aoe=%d allies=%d effN=%d selfHas=%d allyGroup=%d -> %s",
                                         grp[k].spell, en ? en : "?", grp[k].status, grp[k].fresh, grp[k].aoe, grp[k].allies, effN, grp[k].selfHas, 0, group ? "GROUPED (AoE N)" : "PER-ALLY");
                }
            }
            if (group) {   // AoE : one grouped row (Minuet V (AoE 6))
                PartyState::RollInfo ri = grp[k].isAbil ? party().roll_info(grp[k].status) : PartyState::RollInfo{ 0, 0 };   // COR roll -> pip value (double-up included)
                bufs[nb].rem = rem; bufs[nb].fine = fine; bufs[nb].fineClk = fclk; bufs[nb].icon = grp[k].status;   // no `who` : a group is about SEVERAL people, so it renders like your own buffs and follows the display mode (it used to force icon+name whenever you did not hold the buff yourself, which is half of why the presentation flipped as you re-cast)
                // A group whose SELF copy folded in (grp[].selfHas) is YOUR OWN buff -> stays in the top tier (0).
                // One you only put on allies goes to the "your ally-casts" tier (10), above the players-on-you tier (40+).
                bufs[nb].order = (grp[k].selfHas || grp[k].selfCast) ? 0 : 10;   // selfCast : keep it in YOUR tier from the first frame too (else the row jumps tier 10 -> 0 when the fold lands ~1s later)
                if (grp[k].isAbil && ri.value && en) {   // roll : "Chaos Roll [11] (CC) (AoE 6)" -- ONLY the pip tinted
                    _snprintf(obLabel[nb], sizeof(obLabel[nb]), " (AoE %d)", effN); obLabel[nb][sizeof(obLabel[nb]) - 1] = 0;
                    bufs[nb].name = en; bufs[nb].pip = ri.value; bufs[nb].post = obLabel[nb];
                    bufs[nb].pipCol = (ri.luck == 2) ? 0xFFF06060u : (ri.luck == 1 || ri.value == 11) ? 0xFF74D074u : 0xFFEAF0FFu;   // unlucky=red, lucky/11=green, else white
                    if (ri.cc) { bufs[nb].tag = " (CC)"; bufs[nb].tagCol = 0xFFE8C55Au; }   // cast under Crooked Cards
                } else if (en) {   // song / spell AoE : "Minuet V (SV NT) (AoE 6)" -- name + gold modifier tag + AoE suffix
                    _snprintf(obLabel[nb], sizeof(obLabel[nb]), " (AoE %d)", effN); obLabel[nb][sizeof(obLabel[nb]) - 1] = 0;
                    bufs[nb].name = en; bufs[nb].post = obLabel[nb];
                    const char* tg = song_mod_tag(party().song_mods(grp[k].spell), tagBuf[nb], sizeof(tagBuf[nb]));   // keyed by SPELL -> same-family songs keep separate tags
                    if (tg) { bufs[nb].tag = tg; bufs[nb].tagCol = 0xFFE8C55Au; }   // gold
                } else {
                    _snprintf(obLabel[nb], sizeof(obLabel[nb]), "(AoE %d)", effN); obLabel[nb][sizeof(obLabel[nb]) - 1] = 0; bufs[nb].name = obLabel[nb];
                }
                // NO `mark` HERE, AND THAT IS THE DESIGN. A focus entry keys on ONE person + one status, so an
                // "(AoE 6)" row stands for SIX of them -- a single number could not say which to silence.
                // Nothing is lost: the moment the buff drops off somebody, the red alert that replaces it IS
                // per-person and DOES carry its number (see the src=6 row below), which is the only moment you
                // ever need to aim. Reported 2026-09-09 as "no number to //aio out a song"; the per-ally and
                // Pianissimo rows number normally, and this asymmetry is why.
                bufs[nb].src = 4;   // pass 3 : a buff YOU cast on allies, shown as one AoE group
                OBLOG("  ROW  GROUPED   \"%s%s\"  rem=%ds  order=%d   [group %d, effN=%d]", bufs[nb].name ? bufs[nb].name : "?", bufs[nb].post ? bufs[nb].post : "", bufs[nb].rem, bufs[nb].order, k, effN);
                ++nb;   // your AoE -> group first
            } else {   // PER-ALLY : one "Person - Spell" row for each ally who really carries it (self is in pass 2).
                       // For a LAGGARD group this is the whole point : one NAMED row per un-refreshed person (Kaories,
                       // Gab, ...) on THEIR own shorter timer, as a block AFTER your fresh ally-casts (order 30+ vs 11-28).
                const int poBase = lag ? 30 : 11;
                // Selector must match the group KEY exactly -- spell, freshness AND delivery. Missing `aoe` here
                // while it was part of the key made the single-target group re-emit every AoE member as well :
                // //aio oblog showed one per-ally row per ally all tagged [group 0], next to the correct
                // "(AoE 4)" from group 1. Whenever the key gains a field, this line gains it too.
                for (int i = 0; i < no && nb < 50; ++i) if (ob[i].spell == grp[k].spell && (obFresh(ob[i]) ? 1 : 0) == grp[k].fresh && ob[i].aoe == grp[k].aoe && obRem(ob[i]) > 0) {   // ONLY this generation's members -> a laggard row lists exactly the people the re-sing missed
                    const BuffSet* bs = party().buffs_for(ob[i].target); bool has = false; if (bs) for (int j = 0; j < bs->n; ++j) if (bs->ids[j] == grp[k].status) { has = true; break; }
                    // Drop the row ONLY on positive evidence that the ally lost the buff : we hold that member's
                    // 0x076 list AND the status is not in it. buffs_for() returning null is "we have never been
                    // told anything about this member", not "the buff is gone" -- and treating the two the same
                    // is rule 10 ("empty is not unavailable") in its most expensive form. It emptied the row for
                    // every trust, and for real players too whenever no 0x076 had arrived : verified in game --
                    // five trusts AND a player (Kaories, trust=0) all reporting buffset=NONE while the model held
                    // the right name, status and timer for each of them.
                    // Cost of keeping it : an early wear-off (dispel, death) is invisible while we have no list,
                    // so the row rides its estimate to the end -- the same trade the zone grace already makes.
                    const bool known = (bs != 0);   // do we hold this member's buff list at all ?
                    if (known && !has && !graceOB) continue;   // told about them, and the buff is not there -> gone
                    if (fm_muted_for(ob[i].target, ob[i].status, ob[i].spell, false)) continue;   // taken off by //aio out : no row at all
                    bufs[nb].who = ob[i].name;   // the person always shows ; the spell only when the mode asks for a name
                    bufs[nb].name = en;          // 0 = spell unknown -> the person alone carries the row
                    bufs[nb].rem = obRem(ob[i]); bufs[nb].fine = obFine(ob[i]); bufs[nb].fineClk = FCLK_EST; bufs[nb].icon = ob[i].status; bufs[nb].order = poBase + party().party_order(ob[i].target); bufs[nb].src = 5; bufs[nb].mark = fm_tag_of(ob[i].target, ob[i].status, ob[i].spell, false); ++nb;   // ally-cast rows GROUPED BY ally ; laggards form a named block after the fresh ones
                    OBLOG("  ROW  per-ally  \"%s - %s\"  rem=%ds  order=%d   [group %d, %s]", bufs[nb-1].who ? bufs[nb-1].who : "?", bufs[nb-1].name ? bufs[nb-1].name : "?", bufs[nb-1].rem, bufs[nb-1].order, k, lag ? "laggard" : "fresh");
                }
            }
        }
        OBLOG("=== OBLOG : ally rows done (%d row(s) emitted so far this frame) ===", nb);
        // NB : do NOT disarm here. The FOCUS monitor below is stage 4 and runs AFTER this point -- clearing
        // g_obLog now made that whole stage unreachable, so a capture came back with no focus lines at all and
        // read exactly like "no focus buff is being watched". The disarm belongs at the true end of the frame.
        // ---- FOCUS monitor : for buffs marked FOCUS (Haste/Refresh/Phalanx/Flurry/Composure/Reraise...), remember
        //      them once they're UP -- on YOU (Self) or on an ally (Allies you cast them on) -- and keep a RED row
        //      while the buff is MISSING, until it's re-applied. Pruned when the ally leaves the party or you zone. ----
        if (trkJob) {
            static unsigned fmZone = 0xFFFFFFFFu; static unsigned fmZoneGraceMs = 0; static unsigned fmGen = 0; static unsigned fmZoneAtMs = 0;
            if (fmGen != g_tmResetGen) { fmN = 0; fmGen = g_tmResetGen; }                       // //aio timers reset -> wipe the monitor
            const unsigned zone = f.game ? f.game->zone : 0;   // the SNAPSHOT, not a second read of the same offset (rules 6 & 7)
            // a focus buff PERSISTS across a zone : KEEP the monitor, just grace the "lost" alerts while the 0x063 / 0x076 buff
            // lists re-populate. Every entry is flagged for post-zone re-validation (below) : one still MISSING once the lists
            // are back was removed BY THE GAME on zoning (not a real loss) -> it depops silently, no OUT alert.
            if (zone != fmZone) {
                fmZone = zone; fmZoneAtMs = nowMs; fmZoneGraceMs = nowMs + 8000;
                // A ZONE FORGETS AN ALLY SONG -- and nothing else. The model drops exactly those rows on the
                // zone-out packet, so the entries that watched them have nothing left to watch, and keeping them
                // would alert on songs whose fate we cannot know for several seconds. Every other ally entry
                // stays, because its row stays: dropping them here is how a zone lost the Haste you had just put
                // on the party (reported 2026-09-12). Your OWN entries stay too -- the 0x063 comes back whole.
                int w2 = 0;
                for (int q = 0; q < fmN; ++q) {
                    const bool allySong = !fm[q].self && song_family(fm[q].spell) > 0;
                    if (allySong) continue;
                    if (w2 != q) fm[w2] = fm[q];
                    ++w2;
                }
                fmN = w2;
            }
            if (party().is_zoning()) { fmZoneAtMs = nowMs; fmZoneGraceMs = nowMs + 8000; }       // keep the grace armed through the whole loading screen
            const bool zoneGrace = (party().is_zoning() || (int)(fmZoneGraceMs - nowMs) > 0);
            const unsigned meId = party().self_id();
            // Is the buff's real list READY (populated) for its target ? (self = the memory buff list ; ally = the 0x076 cache).
            // Right after a zone these are momentarily EMPTY -- we must NOT read "empty" as "buff gone" or a survivor's
            // zoneCheck clears too early / a casualty is judged before the truth arrives.
            // buffsOk, NOT nbuff > 0 : same trap meHas() above documents. An EMPTY self buff list is a real answer,
            // and it is precisely the state after your last buff lapses -- reading it as "not ready yet" parked the
            // post-zone entry with no decision and no OUT alert until some unrelated buff repopulated the list.
            auto listReady = [&](const FocusMem& e) -> bool {
                if (e.self) return f.game && f.game->buffsOk;                                  // your own list is 0x063, and buffsOk already splits empty from failed
                const BuffSet* bs = party().buffs_for(e.target);
                if (!bs) return false;
                // A LIST FROM BEFORE THE ZONE CANNOT SAY WHAT SURVIVED IT. The 0x076 cache is not cleared when you
                // zone -- it holds the last set until a new packet lands -- so a non-null pointer used to read as
                // "ready" while the contents were minutes old and pre-zone. The post-zone check above would then
                // see the stale songs, conclude they survived, and start tracking; the real list arriving after
                // that turned into a red OUT for every song, on every ally. Reported 2026-09-10 ("quand je zone je
                // vois encore les alertes Kaories x3"). It is the same rule the prune already obeys in
                // model/song_slot.h: evidence recorded before the event cannot rule on it.
                if (!fmZoneAtMs || (int)(bs->stampMs - fmZoneAtMs) >= 0) return true;
                // ...and a wait must not become a silence. If no fresh list has arrived a full minute after the
                // zone, take the cached one rather than never alerting again -- and SAY SO, because a probe that
                // dies quietly reads exactly like a bug that is not happening.
                if ((unsigned)(nowMs - fmZoneAtMs) > 60000u) {
                    static windower::debug::LogOnce<2> onceStale;
                    if (onceStale.first(0)) windower::debug::log("FOCUS : no post-zone 0x076 after 60s -- falling back to the cached buff list (alerts resume)");
                    return true;
                }
                return false; };
            // HOW MANY copies of the status are on the target -- not whether there is one. A bard holds two songs
            // on a single status (Minuet IV + V, two Marches) and the 0x076 shows them as `198 198`; asking "is 198
            // present" answered yes while one of the two was already gone. See model/focus_rules.h rule 4.
            auto focusCopies = [&](const FocusMem& e) -> int {
                int c = 0;
                if (e.self) { if (!f.game) return 0; for (int i = 0; i < f.game->nbuff; ++i) if ((int)f.game->buffs[i] == (int)e.status) ++c; return c; }
                const BuffSet* bs = party().buffs_for(e.target); if (bs) for (int j = 0; j < bs->n; ++j) if (bs->ids[j] == e.status) ++c; return c; };
            // ONE STATUS, ONE BUFF -- except for the two kinds that really run several copies at once: a bard's
            // songs (focus_rules.h section 4) and a Rune Fencer's runes (section 10). Everything else holds a
            // single instance whatever the TIER, so Phalanx and Phalanx II are one buff and its presence answers
            // for both. Asking the copy arithmetic about them read a tier change as a loss (section 11).
            auto focusSeveral = [&](const FocusMem& e) -> bool { return song_family(e.spell) > 0 || focus_rune_status(e.status); };
            auto focusHas = [&](const FocusMem& e) -> bool {                                    // is THIS song still up (list assumed ready)
                const bool several = focusSeveral(e);
                int newer = 0;
                if (several) for (int q2 = 0; q2 < fmN; ++q2) {
                    const FocusMem& o = fm[q2];
                    if (o.self != e.self || o.status != e.status) continue;
                    if (!e.self && o.target != e.target) continue;
                    if (focus_newer_sibling(e.rank, e.spell, o.rank, o.spell)) ++newer;
                }
                return focus_entry_up(several, newer, focusCopies(e)); };
            { unsigned jc[24]; const int jcn = party().job_changes(jc, 24);                    // a member (self or ally) changed job -> its buffs reset -> drop its focus rows (not a real "loss")
              for (int c = 0; c < jcn; ++c) { int wj = 0;
                for (int q = 0; q < fmN; ++q) { const bool drop = fm[q].self ? (jc[c] == meId) : (fm[q].target == jc[c]); if (!drop) { if (wj != q) fm[wj] = fm[q]; ++wj; } }
                fmN = wj; } }
            // YOU swapped MAIN job -> also drop the ALLY focus rows. Their buffs are still really up (only your own
            // are stripped), but on BRD a Regen you cast as SCH is noise : you will not recast it, so it must not
            // sit there counting down, and it must not raise an OUT alert when it finally wears. The model already
            // cleared the tracked entries that seed these rows ; fm[] is static, so it needs telling too.
            if (party().self_main_job_changed()) { int wj = 0;
              for (int q = 0; q < fmN; ++q) if (fm[q].self) { if (wj != q) fm[wj] = fm[q]; ++wj; }
              fmN = wj; }
            for (int q = 0; q < fmN; ++q) { fm[q].seen = 0; fm[q].alerting = 0; }              // ... cleared before the two seeding loops below (alerting is re-set by the OUT emit at the end of this block)
            { int n2 = 0; const BuffTimer* bt2 = party().buff_timers(n2);                      // remember FOCUS buffs currently up on YOU (Self focus key 0x8000|st)
              for (int i = 0; i < n2; ++i) { const unsigned st = bt2[i].id;
                if (focus_trace_live() && st < 1024 && !is_debuff_status(st) && meHas((int)st)) {
                    windower::debug::log("FOCUSMON remember? st=%u '%s' meHas=%d focusOn=%d (focusKey off=%d)",
                                         st, buff_status_name(st), meHas((int)st) ? 1 : 0, tm_self_focus_on(C, st) ? 1 : 0,
                                         C.tm_buff_off(UiConfig::TM_KEY_FOCUS | st) ? 1 : 0);
                }
                if (st >= 1024 || is_debuff_status(st) || !meHas((int)st) || !tm_self_focus_on(C, st)) continue;
                if (!srcKeeps((unsigned short)st, bt2[i].expiry, i)) continue;   // the source filter hides this row -> it must not alert either   // gate on meHas (same source as the emit check) -> no false alert while it's up ; per-SPELL focus key (see tm_self_keys)
                // OUT alerts fire ONLY for buffs YOU cast : a buff someone ELSE put on YOU (a box-mate's Corsair roll,
                // a trust's Protect) is not yours to keep up -- losing it (the roller re-rolls and replaces it) is
                // normal, so it must just DEPOP, never a red OUT. Buffs YOU cast on OTHERS keep alerting -- they live
                // in the separate ally-focus path below (an RDM still wants his Haste/Refresh on a party-mate to OUT).
                // Unknown caster (food / gear / a self-cast whose 0x028 we never saw) counts as yours -> keep alerting.
                { const unsigned oc = party().buff_caster_for((unsigned short)st, bt2[i].expiry, i);
                  if (oc && oc != meId) continue; }
                // Keyed by SPELL as well as status: two Marches are two songs on one status id, and holding a
                // single entry for them meant the second was watched by nobody at all.
                const unsigned short selfSp = (unsigned short)party().self_buff_spell_ranked((unsigned short)st, bt2[i].expiry, i);
                int s = -1;
                if (focus_rune_status(st)) s = focus_rune_match(fm, fmN, st, bt2[i].expiry);   // one entry per rune PLACED : two Ignis are two (focus_rules.h section 10)
                else for (int q = 0; q < fmN; ++q) if (fm[q].self && fm[q].status == st && fm[q].spell == selfSp) { s = q; break; }
                if (s < 0 && fmN < FOCUS_MAX) { s = fmN++; fm[s].spell = selfSp; fm[s].target = meId; fm[s].status = (unsigned short)st; fm[s].self = 1; fm[s].isAbil = 0; fm[s].aoe = 0; /* there is one of you : a self alert never groups */ fm[s].lostMs = 0; fm[s].muteRef = 0; fm[s].zoneCheck = 0; fm[s].muted = 0; fm[s].alerting = 0; fm[s].tag = fm_free_tag(); fm[s].seen = 1; fm[s].bornMs = model_now_ms(); fm[s].name[0] = 0; }
                if (s >= 0) { fm[s].seen = 1; fm[s].spell = selfSp; fm[s].rank = bt2[i].expiry; }   // the spell/tier is part of the key now, so this only re-affirms it ; rank refreshed every frame
              } }
            for (int i = 0; i < no; ++i) {                                                     // remember FOCUS buffs currently up on allies (Allies focus key 0xC000|st ; needs tmMine)
                const unsigned st = ob[i].status;
                if (!C.tm_buff_off(UiConfig::TM_KEY_FOCUS | st)) continue;   // ally focus = the same global buff-focus state
                // Only PARTY targets can ever be monitored -- their buff list comes from the 0x076, which stops at
                // your party (see the `live` note in the prune). Creating an entry we could never decide is what
                // filled fm[] on an alliance run and starved your own rows.
                if (party().party_order(ob[i].target) > 5) continue;
                int s = -1; for (int q = 0; q < fmN; ++q) if (!fm[q].self && fm[q].target == ob[i].target && fm[q].status == st && fm[q].spell == ob[i].spell) { s = q; break; }   // per SPELL : two Minuets on one status are two rows to watch
                // A NEW ENTRY IS ONLY "PRESENT" IF SOMETHING SAYS SO. Seeded from ob[] -- OUR record of OUR cast --
                // it asserts nothing about what the ally actually carries, so when we hold no populated buff list
                // for that target it starts PENDING instead, and the post-zone check decides once one arrives.
                // Without this, a zone produced a red OUT out of nothing: the prune rightly KEEPS its ally row on an
                // empty 0x076 ("empty is not the same as knowing it is empty", model/song_slot.h), so once the 8 s
                // grace lapsed this loop re-created the entry as known-present and it alerted on the next frame.
                // Reported 2026-09-10, "je viens de zone et une song reste en out".
                { const BuffSet* bs0 = party().buffs_for(ob[i].target);
                  const unsigned char pending = (!bs0 || bs0->n <= 0) ? 1 : 0;
                if (s < 0 && fmN < FOCUS_MAX) { s = fmN++; fm[s].spell = ob[i].spell; fm[s].target = ob[i].target; fm[s].status = (unsigned short)st; fm[s].self = 0; fm[s].aoe = ob[i].aoe; /* the CAST's own fact (0x028 target count) : it decides whether losing it is one event or three */ fm[s].lostMs = 0; fm[s].muteRef = 0; fm[s].zoneCheck = pending; fm[s].muted = 0; fm[s].alerting = 0; fm[s].tag = fm_free_tag(); fm[s].seen = 1; fm[s].bornMs = model_now_ms(); }
                else if (s < 0) { static windower::debug::LogOnce<2> onceFull;   // SAY it. A silent refusal here is indistinguishable from "no buff to watch".
                    if (onceFull.first(0)) windower::debug::log("FOCUS monitor FULL (%d entries) -- new ally focus buffs are NOT tracked this session", FOCUS_MAX); }
                if (s >= 0) { fm[s].seen = 1; fm[s].spell = ob[i].spell; fm[s].rank = ob[i].startMs; fm[s].isAbil = ob[i].isAbil; int j = 0; for (; j < 19 && ob[i].name[j]; ++j) fm[s].name[j] = ob[i].name[j]; fm[s].name[j] = 0; } }
            }
            // A NEWER CAST LIFTS THE MUTE. //aio out silences ONE cast, not the spell -- and the correction that
            // follows it is almost always another cast of the same buff, often on the very person you took off
            // (you Hasted him by mistake, then decided he should have it after all). That cast overwrites the
            // buff instead of ending it, so the entry is never starved and the prune below never fires : without
            // this the row would stay hidden for the whole time you kept the buff up. Strictly NEWER, never just
            // "different" -- an ob[] entry dropped from a same-status pair lowers the max, and that is not a cast.
            // The rule itself is in model/focus_rules.h, behind tests/t_focusrules.cpp -- including the case
            // that made it necessary (a row taken off by hand never came back while you kept the buff up) and
            // the one that keeps it honest (a SMALLER cast reference is a pruned sibling, not a new cast).
            for (int q = 0; q < fmN; ++q)
                if (focus_mute_verdict(fm[q].muted != 0, fm[q].seen != 0, fm_cast_ref(fm[q]), fm[q].muteRef) == FOCUS_LIFT)
                    { fm[q].muted = 0; fm[q].lostMs = 0; fm[q].muteRef = 0; }   // watched again, with the number it already had
            // A MUTED entry lives exactly as long as the thing that feeds it. Once no live source refreshed it
            // this frame, it is gone for good -- and a later, deliberate cast on that person creates a fresh
            // entry, un-muted and watched again, which is the whole intent: you silenced ONE mistake, not the
            // spell. Only muted entries are pruned here ; a normal one must survive its buff to raise the OUT.
            { int wj = 0;
              for (int q = 0; q < fmN; ++q) {
                  if (focus_mute_verdict(fm[q].muted != 0, fm[q].seen != 0, fm_cast_ref(fm[q]), fm[q].muteRef) == FOCUS_FORGET) continue;
                  if (wj != q) fm[wj] = fm[q]; ++wj; }
              fmN = wj; }
            if (zoneGrace) for (int q = 0; q < fmN; ++q) fm[q].zoneCheck = 1;                 // through the WHOLE settle : flag every entry (incl. ones just re-populated from a stale list) for post-grace validation
            // ---- BRD song "OUT" suppression : a song BEYOND your no-Clarion-Call maximum, once Clarion Call is spent,
            //      cannot be re-cast -- so a lost 5th (Clarion Call) song must NOT raise a permanent red OUT ("pour 4
            //      songs c'est ok"). The count and the max are NOT in memory (Ghidra 2026-07-22 : both server-side, and
            //      the status array family-collapses two same-family songs), so both are RECONSTRUCTED :
            //        count = distinct song SPELLS you are maintaining, from ob[] (keyed on SPELL, so two Marches count
            //                as two ; single-target etudes on allies are included -- neither is true of the status array).
            //        base  = high-water of that count while Clarion Call is FULLY AVAILABLE (buff DOWN and recast READY).
            //                That window carries no CC bonus, so the count IS your no-CC max -- and it auto-learns the
            //                instrument / merit setup (Daurdabla / Loughnashade +2, Blurred Harp +1, song merits) with
            //                ZERO gear reads. Gating learning on "recast ready" excludes the post-CC window, so the
            //                transient extra-song count right after CC drops can never poison the base upward.
            //      Clarion Call : buff = status 499 ; recast = the SP2 shared recast id 254 (a BRD's SP2 IS Clarion Call).
            bool ccUp = false, ccOnRecast = false;
            if (f.game) {
                for (int i = 0; i < f.game->nbuff; ++i) if (f.game->buffs[i] == 499) { ccUp = true; break; }
                for (int i = 0; i < f.game->nRecast; ++i) if (f.game->recasts[i].kind == 0 && f.game->recasts[i].recastId == 254 && f.game->recasts[i].sec > 0) { ccOnRecast = true; break; }
            }
            // THE CAP IS NOT LEARNED HERE ANY MORE. It came from a high-water mark of song counts, which is
            // structurally wrong -- the limit follows the equipped INSTRUMENT, so the mark keeps the maximum
            // reached under a Daurdabla long after the swap, and a plugin reload or a job change reset it to 1,
            // silencing every song loss until a rotation rebuilt it (measured 2026-09-10).
            // The model learns it from an EVICTION instead: the game only makes room when there is none left,
            // so the count at that moment is the limit, exactly. See model/song_slots.h.
            const SlotCap songCap = party().song_cap();
            // A MISSING ALLY SONG IS UNRECOVERABLE -- and so goes quietly -- only when the cap is known from an
            // eviction, the song is on an ALLY, Clarion Call cannot be used, that person now holds EXACTLY the
            // cap, and you have lost the song too. Everything else keeps its permanent red OUT.
            //
            // The rule that stood here compared a count across ALL allies at once. Two things were wrong with it,
            // and either alone was fatal: that count skipped the FAKE songs -- the ones sung purely to hold a slot
            // -- and it was global, where the game counts per (singer, target). Measured 2026-09-10: one ally
            // dispelled while another still carried the song never moved it, so the rule stayed true for a whole
            // Clarion Call recast and swallowed EVERY ally song loss, silently. Six live songs, six flagged.
            // Clarion Call is USABLE when its buff is up or its recast is ready -- either way the fifth slot
            // can be refilled, so a song lost from it keeps its normal alert.
            const bool ccUsable = ccUp || !ccOnRecast;
            auto songUnrecoverable = [&](const FocusMem& e) -> bool {
                const bool stillOnYou = party().self_buff_remaining_for(e.status, e.spell) >= 0;
                // The count is the one the GAME keeps : your songs on THAT person, fake songs included.
                // The old one skipped the fake songs -- the very ones sung to hold a slot -- and was global,
                // a number that exists nowhere in the game.
                if (stillOnYou) return false;   // you still hold it, so it is re-singable : this is not the lost fifth
                return song_unrecoverable(songCap, e.self != 0, song_family(e.spell) > 0,
                                          ccUsable, party().song_slot_count(e.target));
            };
            // A DELIBERATE SWAP, NOT A LOSS : model/focus_rules.h section 9 (Pianissimo, an eviction, an Indi- replaced).
            struct SwapSrc {
                const FocusMem& e; const PartyState::OtherBuff* ob; int no; unsigned now;
                bool self() const { return e.self != 0; }
                unsigned target() const { return e.target; }
                unsigned spell() const { return e.spell; }
                unsigned status() const { return e.status; }
                unsigned nowMs() const { return now; }
                int others() const { return no; }
                unsigned otherTarget(int i) const { return ob[i].target; }
                unsigned otherSpell(int i) const { return ob[i].spell; }
                unsigned otherStartMs(int i) const { return ob[i].startMs; }
                bool otherAoe(int i) const { return ob[i].aoe != 0; }
                bool isSong(unsigned sp) const { return song_family(sp) > 0; }
                bool isIndi(unsigned sp) const { const SpellBuff* sb = sp ? spell_buff(sp) : 0; return sb && sb->skill == 44; }
                bool evicted() const { return party().song_was_evicted(e.target, e.spell, 6000u); }
                unsigned carriedAura() const { return party().self_geo().status; }
            };
            auto songReplaced = [&](const FocusMem& e) -> bool { return focus_song_replaced(SwapSrc{ e, ob, no, nowMs }); };
            auto geoReplaced = [&](const FocusMem& e) -> bool { return focus_geo_replaced(SwapSrc{ e, ob, no, nowMs }); };
            // RUNES : model/focus_rules.h section 10. The runes on you now against the slots your RUN level gives.
            const int runeCap = f.game ? focus_rune_cap(f.game->me.mjob, f.game->me.mlvl, f.game->me.sjob, f.game->me.slvl) : 0;
            int runesUp = 0;
            if (f.game && f.game->buffsOk) for (int i = 0; i < f.game->nbuff; ++i) if (focus_rune_status(f.game->buffs[i])) ++runesUp;
            // Settled BEFORE the compaction below, because focusHas() counts an entry's siblings and the compaction
            // leaves stale copies behind it -- counting mid-pass would see the same sibling twice and call a live
            // song lost. The verdict travels with its entry through the pass, and the emit reads the same one.
            static bool fmHas[FOCUS_MAX];   // ONE PER MONITOR ENTRY. It stayed [24] when FOCUS_MAX went to 64 (2026-09-11) : past 24 watched buffs every frame wrote beyond it (tests/t_timers.cpp, caught by the address sanitizer)
            for (int q = 0; q < fmN; ++q) fmHas[q] = focusHas(fm[q]);
            // ...and WHICH ENTRIES A NEWER TIER REPLACED, for the same reason again (focus_rules.h section 11).
            // `several` is the gate the copy arithmetic always needed: songs and runes really run several copies
            // on one status, nothing else does.
            static bool fmSeveral[FOCUS_MAX]; static bool fmTierRepl[FOCUS_MAX];
            for (int q = 0; q < fmN; ++q) fmSeveral[q] = focusSeveral(fm[q]);
            for (int q = 0; q < fmN; ++q) fmTierRepl[q] = focus_tier_replaced(fm, fmSeveral, fmN, q);
            // ...and the rune count travels with it, for the same reason (focus_rules.h section 10).
            static int fmRuneNewer[FOCUS_MAX];
            { bool lost[FOCUS_MAX];
              for (int q = 0; q < fmN; ++q) lost[q] = fm[q].self ? (f.game && f.game->buffsOk && !fmHas[q]) : !fmHas[q];
              for (int q = 0; q < fmN; ++q) fmRuneNewer[q] = focus_rune_newer_lost(fm, lost, fmN, q); }
#ifdef AIOHUD_PROBES
            g_whyFmN = 0;
            for (int q = 0; q < fmN && g_whyFmN < 64; ++q) { WhyFm& w = g_whyFm[g_whyFmN++];
                w.target = fm[q].target; w.spell = fm[q].spell; w.status = fm[q].status;
                w.copies = focusCopies(fm[q]);
                int nw = 0; for (int q2 = 0; q2 < fmN; ++q2) { const FocusMem& o = fm[q2];
                    if (o.self != fm[q].self || o.status != fm[q].status) continue;
                    if (!fm[q].self && o.target != fm[q].target) continue;
                    if (focus_newer_sibling(fm[q].rank, fm[q].spell, o.rank, o.spell)) ++nw; }
                w.newer = nw; w.self = fm[q].self; w.has = fmHas[q] ? 1 : 0;
                w.listReady = listReady(fm[q]) ? 1 : 0; w.lost = fm[q].lostMs ? 1 : 0; }
#endif
            // WHICH ENTRIES ARE FORGOTTEN : model/focus_rules.h section 8 (focus_prune_verdict). The adapter answers
            // from the frame, lazily ; the traces below name the branch that decided, as before.
            using ReadyFn = decltype(listReady); using UnrecFn = decltype(songUnrecoverable);
            using ReplFn = decltype(songReplaced); using GeoFn = decltype(geoReplaced);
            struct PruneSrc {
                const FocusMem& e; bool hasNow; bool grace; unsigned now; const UiConfig& cfg;
                const ReadyFn* ready; const UnrecFn* unrec; const ReplFn* repl; const GeoFn* geo;
                bool tierRepl;
                bool tierReplaced() const { return tierRepl; }
                bool self() const { return e.self != 0; }
                bool zoneGrace() const { return grace; }
                int partyOrder() const { return party().party_order(e.target); }
                bool focusOn() const { return cfg.tm_buff_off(UiConfig::TM_KEY_FOCUS | e.status); }   // self & ally share ONE global focus state
                bool isSong() const { return song_family(e.spell) > 0; }
                bool offzone() const { return party().member_offzone(e.target); }
                bool zoneCheck() const { return e.zoneCheck != 0; }
                bool listReady() const { return (*ready)(e); }
                bool has() const { return hasNow; }
                unsigned lostMs() const { return e.lostMs; }
                unsigned nowMs() const { return now; }
                bool hidden() const { return cfg.tm_buff_off((unsigned)e.status); }   // self & ally share ONE global hidden state
                int holdSec() const { return cfg.tmFocusHold; }
                bool unrecoverable() const { return (*unrec)(e); }
                bool replaced() const { return (*repl)(e); }
                bool geoReplaced() const { return (*geo)(e); }
                int rcap; int rup;
                int rnew;
                bool runeReplaced() const { return focus_rune_replaced(e.self != 0, e.status, rcap, rup, rnew); }
            };
            int w = 0;
            for (int q = 0; q < fmN; ++q) {
                const FocusPrune pv = focus_prune_verdict(PruneSrc{ fm[q], fmHas[q], zoneGrace, nowMs, C, &listReady, &songUnrecoverable, &songReplaced, &geoReplaced, fmTierRepl[q], runeCap, runesUp, fmRuneNewer[q] });
                if (pv == FP_DROP_GONE_OR_OFF && focus_trace_live()) {
                    const bool live = fm[q].self ? true : (zoneGrace || party().party_order(fm[q].target) <= 5);
                    const bool fkOn = C.tm_buff_off(UiConfig::TM_KEY_FOCUS | fm[q].status);
                    windower::debug::log("FOCUSPRUNE st=%u '%s' DROPPED (live=%d focusOn=%d) -> no OUT row possible",
                                         (unsigned)fm[q].status, buff_status_name(fm[q].status), live ? 1 : 0, fkOn ? 1 : 0);
                }
                if (pv == FP_DROP_TIER_REPLACED && focus_trace_live())
                    windower::debug::log("TIERREPL st=%u '%s' spell=%u target=%08X self=%d DROPPED -> no OUT (another tier of the same buff took its place)",
                                         (unsigned)fm[q].status, buff_status_name(fm[q].status), (unsigned)fm[q].spell, fm[q].target, fm[q].self);
                if (pv == FP_DROP_SONG_OFFZONE && focus_trace_live())
                    windower::debug::log("SONGOFFZONE st=%u '%s' target=%08X out-of-zone -> CLEAN (no OUT, no stale row)",
                                         (unsigned)fm[q].status, buff_status_name(fm[q].status), fm[q].target);
                if (focus_prune_drops(pv)) continue;
                if (pv == FP_KEEP_SURVIVED_ZONE) fm[q].zoneCheck = 0;
                if (w != q) { fm[w] = fm[q]; fmHas[w] = fmHas[q]; fmRuneNewer[w] = fmRuneNewer[q]; } ++w;
            }
            fmN = w;
            // //aio oblog stage 4 : the FOCUS monitor. It decides "OUT" from a SEPARATE input (0x076 presence of the
            // STATUS on the target) than the rows above, so a song can draw a healthy timer AND a red OUT at once --
            // and the group/row stages show nothing about why. Songs share statuses across spells (both Marches are
            // 214), so `has` answering for the family and `spell` naming one member of it is exactly where an "it IS
            // up, you re-cast it in AoE" symptom lives. Dumped for every monitored entry, kept or not.
            int alertQ[FOCUS_MAX]; int nAlert = 0;   // entries that survived every suppression gate -- drawn after the loop
            OBLOG("=== OBLOG : %d focus monitor entr(ies) ===", fmN);
            // The bard song-slot state, in one line. It is here rather than behind its own command because the
            // thing it answers -- "did the base stay honest?" -- can only be seen after a Clarion Call, and CC is
            // an HOUR of recast: nobody is going to sit through a dedicated test for it. Printed in every capture,
            // the answer instead falls out of an ordinary fight. slot=1 means the extra slot is treated as possibly
            // occupied, so nothing is being learned; base is what //aio out's song suppression measures against.
            OBLOG("  SONGSLOT  cap=%d valid=%d (learned from an eviction)  ccUp=%d ccRecast=%d ccUsable=%d  -- counts are PER PERSON, see the FOCUS lines",
                  songCap.cap, songCap.valid ? 1 : 0, ccUp ? 1 : 0, ccOnRecast ? 1 : 0, ccUsable ? 1 : 0);
            // RED OUT, OR SILENCE -- AND WHY : model/focus_rules.h section 7 (focus_alert_verdict). The adapter answers
            // from the frame, lazily ; the loss stamp and the //aio ftrace lines stay here, keyed on the reason.
            struct AlertSrc {
                const FocusMem& e; bool hasNow; bool grace; unsigned now; const UiConfig& cfg; SlotCap cap;
                const ReadyFn* ready; const UnrecFn* unrec; const ReplFn* repl; const GeoFn* geo;
                bool allyRowsOff() const { return !e.self && !cfg.tmMine; }
                bool muted() const { return e.muted != 0; }
                bool up() const { return hasNow; }
                bool zoneGrace() const { return grace; }
                bool listReady() const { return (*ready)(e); }
                unsigned lostMs() const { return e.lostMs; }
                unsigned nowMs() const { return now; }
                bool hidden() const { return cfg.tm_buff_off((unsigned)e.status); }   // self & ally share ONE global hidden state
                int holdSec() const { return cfg.tmFocusHold; }
                bool slotsFilled() const { return cap.valid && party().song_slot_count(e.target) >= cap.cap; }
                bool unrecoverable() const { return (*unrec)(e); }
                bool replaced() const { return (*repl)(e); }
                bool geoReplaced() const { return (*geo)(e); }
                int rcap; int rup;
                int rnew;
                bool runeReplaced() const { return focus_rune_replaced(e.self != 0, e.status, rcap, rup, rnew); }
            };
            for (int q = 0; q < fmN && nb < 50; ++q) {                                         // emit a RED row for each MISSING focus buff (self or ally)
                // The same verdict the prune used -- this used to be a third, presence-only copy of the rule, which
                // is how one of two same-status songs could be dropped by one stage and reported up by the other.
                // meHas() fails open on a FAILED read; that job now belongs to listReady() below, which gates the alert.
                const bool has = fm[q].self ? (!f.game || !f.game->buffsOk || fmHas[q]) : fmHas[q];
                const FocusAlert av = focus_alert_verdict(AlertSrc{ fm[q], has, zoneGrace, nowMs, C, songCap, &listReady, &songUnrecoverable, &songReplaced, &geoReplaced, runeCap, runesUp, fmRuneNewer[q] });
                if (av == FA_CATEGORY_OFF) { fm[q].lostMs = 0; continue; }
                if (g_obLog) {
                    const char* fen = fm[q].isAbil ? abil_name_by_id(fm[q].spell) : (spell_info(fm[q].spell) ? spell_info(fm[q].spell)->en : 0);
                    OBLOG("  FOCUS q=%-2d %-16s tgt=%08X st=%-4u spell=%-5u \"%s\" self=%d  has(0x076)=%d listReady=%d lostAgo=%dms  replaced=%d unrecov=%d geoRepl=%d (aura st=%u)",
                          q, fm[q].self ? "<you>" : fm[q].name, fm[q].target, fm[q].status, fm[q].spell, fen ? fen : "?",
                          fm[q].self, has ? 1 : 0, listReady(fm[q]) ? 1 : 0,
                          fm[q].lostMs ? (int)(nowMs - fm[q].lostMs) : -1,
                          songReplaced(fm[q]) ? 1 : 0, songUnrecoverable(fm[q]) ? 1 : 0,
                          geoReplaced(fm[q]) ? 1 : 0, (unsigned)party().self_geo().status);
                    if (focus_rune_status(fm[q].status))
                        OBLOG("    RUNE st=%u rank=%u runesUp=%d cap=%d newerLost=%d (jobs %d/%d lv %d/%d) -> runeReplaced=%d",
                              (unsigned)fm[q].status, fm[q].rank, runesUp, runeCap, fmRuneNewer[q], f.game ? f.game->me.mjob : -1, f.game ? f.game->me.sjob : -1,
                              f.game ? f.game->me.mlvl : -1, f.game ? f.game->me.slvl : -1,
                              focus_rune_replaced(fm[q].self != 0, fm[q].status, runeCap, runesUp, fmRuneNewer[q]) ? 1 : 0);
                }
                if (focus_trace_live()) {
                    // nbuff is the crux : meHas() FAILS OPEN (returns true for everything) when the live buff list is
                    // empty, so nbuff==0 would pin has=1 forever and make the alert unreachable. Log the list too.
                    const int nb2 = f.game ? f.game->nbuff : -1;
                    const int okv = f.game ? (f.game->buffsOk ? 1 : 0) : -1;   // THE discriminator : buffsOk=1 nbuff=0 = a REAL "no buffs" (bug if they persisted) ; buffsOk=0 = read not ready (correct to wait)
                    char lst[160]; int o = 0; lst[0] = 0;
                    for (int j = 0; f.game && j < f.game->nbuff && j < 32 && o < 150; ++j)
                        o += _snprintf(lst + o, sizeof(lst) - o, "%u ", (unsigned)f.game->buffs[j]);
                    lst[sizeof(lst) - 1] = 0;   // force-terminate : _snprintf does not, on truncation (party_state.cpp carries the full note)
                    windower::debug::log("FOCUSEMIT st=%u '%s' self=%d has=%d lostMs=%u zoneGrace=%d buffsOk=%d nbuff=%d list=[%s]",
                                         (unsigned)fm[q].status, buff_status_name(fm[q].status), fm[q].self, has ? 1 : 0,
                                         fm[q].lostMs, zoneGrace ? 1 : 0, okv, nb2, lst);
                }
                if (focus_alert_clears_loss(av)) { fm[q].lostMs = 0; continue; }
                if (fm[q].lostMs == 0) fm[q].lostMs = nowMs;                                   // just went missing -> stamp it
                if (focus_trace_live()) {
                    const bool dkOn = C.tm_buff_off((unsigned)fm[q].status);
                    windower::debug::log("FOCUSHOLD st=%u '%s' self=%d has=0 lostAgo=%ums hold=%ds dkOn=%d -> %s",
                                         (unsigned)fm[q].status, buff_status_name(fm[q].status), fm[q].self,
                                         (unsigned)(nowMs - fm[q].lostMs), C.tmFocusHold, dkOn ? 1 : 0,
                                         av == FA_HOLD_EXPIRED ? "DROP (hold expired)" : "DRAW red OUT row");
                    if (av == FA_UNRECOVERABLE)
                        windower::debug::log("SONGOUT st=%u '%s' SUPPRESSED : that person now holds %d, the cap is %d (ccUsable=%d) -> no OUT (the fifth slot is gone)",
                                             (unsigned)fm[q].status, buff_status_name(fm[q].status),
                                             party().song_slot_count(fm[q].target), songCap.cap, ccUsable ? 1 : 0);
                    if (av == FA_REPLACED)
                        windower::debug::log("SONGOUT st=%u '%s' target=%08X SUPPRESSED -> no OUT (song deliberately replaced on this ally)",
                                             (unsigned)fm[q].status, buff_status_name(fm[q].status), fm[q].target);
                    if (av == FA_GEO_REPLACED)
                        windower::debug::log("GEOOUT st=%u '%s' spell=%u self=%d SUPPRESSED (carrying st=%u now) -> no OUT (Indi- deliberately replaced)",
                                             (unsigned)fm[q].status, buff_status_name(fm[q].status), (unsigned)fm[q].spell,
                                             fm[q].self, (unsigned)party().self_geo().status);
                    if (av == FA_RUNE_REPLACED)
                        windower::debug::log("RUNEOUT st=%u '%s' SUPPRESSED : %d rune(s) on you + %d newer lost >= %d slot(s) -> no OUT (not among the runes placed last)",
                                             (unsigned)fm[q].status, buff_status_name(fm[q].status), runesUp, fmRuneNewer[q], runeCap);
                }
                if (av != FA_ALERT) continue;
                if (nAlert < FOCUS_MAX) alertQ[nAlert++] = q;   // decided : drawn below, once the whole picture is known
            }
            // ---- draw the alerts, GROUPED the way the healthy rows are ------------------------------------------
            // An AoE song is ONE row while it is up ("Valor Minuet V (AoE 3)") and used to become one red row PER
            // PERSON the moment it went -- two here, SIX in a real party, all for a single event. Reported
            // 2026-09-10. The two cases want opposite things, and the count tells them apart: everybody lost it at
            // once -> the SONG is what ended, one line says it; ONE person lost it -> that is a dispel, and the
            // name is the whole point of the alert.
            // A grouped alert carries no number, for the same reason a grouped healthy row carries none: it stands
            // for several monitored entries and one number could not say which to silence. A lone alert keeps its
            // number, which is exactly the case where aiming at somebody makes sense.
            // ONLY A REAL AoE MAY GROUP -- the rule lives in model/focus_rules.h and is tested there. It used to
            // count nothing but "these alerts share a spell id", so three Phalanx placed one by one on three
            // people, all lost, came out as a single nameless "Phalanx (AoE 3)" and the two other alerts were
            // dropped as already-spoken-for. Reported from play 2026-09-12. It is the twin of the healthy-row
            // defect fixed the same morning in ally_group.h : one path was corrected, this one was not.
            AlertEntry ae[FOCUS_MAX];
            for (int a = 0; a < nAlert; ++a) { ae[a].spell = fm[alertQ[a]].spell; ae[a].aoe = fm[alertQ[a]].aoe; }
            for (int a = 0; a < nAlert && nb < 50; ++a) {
                const int q = alertQ[a];
                if (focus_alert_covered(ae, nAlert, a)) continue;              // an earlier GROUPED row speaks for this loss
                // ONE SONG, ONE STATEMENT PER FRAME. A row already drawn from YOUR timer says you hold this
                // song; an OUT saying you lost it cannot be true in the same image. When the two disagreed the
                // red row appeared and vanished on alternate frames -- measured 2026-09-10 with four Army's
                // Paeons sharing status 195: "Army's Paeon V (AoE 6)" and "Army's Paeon V OUT", together, at
                // 60 Hz. Attributing four same-status timers to four spells is fragile by nature, and this
                // makes the display immune to it instead of hoping the attribution never slips.
                //
                // SELF alerts only. An ally alert must still fire while a group row shows the song up on the
                // others -- that is the whole point of naming the one person who lost it.
                if (fm[q].self) {
                    const char* an = fm[q].isAbil ? abil_name_by_id(fm[q].spell)
                                                  : (fm[q].spell && spell_info(fm[q].spell) ? spell_info(fm[q].spell)->en : 0);
                    bool drawnUp = false;
                    for (int d = 0; d < nb && !drawnUp; ++d)
                        if (bufs[d].src != 6 && bufs[d].icon == (int)fm[q].status
                            && an && bufs[d].name && strcmp(bufs[d].name, an) == 0) drawnUp = true;
                    if (drawnUp) { fm[q].lostMs = 0; continue; }   // it is on screen as yours : say one thing, not two
                }
                const int same = focus_alert_speaks_for(ae, nAlert, a);
                const char* en = fm[q].isAbil ? abil_name_by_id(fm[q].spell)
                                              : (fm[q].spell && spell_info(fm[q].spell) ? spell_info(fm[q].spell)->en : 0);
                if (!en) en = buff_status_name(fm[q].status);
                if (same >= 2) {
                    _snprintf(obLabel[nb], sizeof(obLabel[nb]), "%s (AoE %d)", en ? en : "?", same);
                } else {
                    if (!fm[q].self) bufs[nb].who = fm[q].name;              // WHO lost it : the one thing a lone alert must never drop
                    _snprintf(obLabel[nb], sizeof(obLabel[nb]), "%s", en ? en : "?");
                }
                obLabel[nb][sizeof(obLabel[nb]) - 1] = 0;
                bufs[nb].name = obLabel[nb]; bufs[nb].nameCol = 0xFFFF3B3Bu; bufs[nb].rem = TM_REM_MISSING; bufs[nb].icon = fm[q].status; bufs[nb].order = 0; bufs[nb].src = 6; bufs[nb].mark = (same >= 2) ? 0 : fm[q].tag; fm[q].alerting = 1; ++nb;   // set HERE and nowhere else : it means "this entry drew its red row", and an entry folded into a grouped alert draws none   // ALL "OUT" alerts (self + ally) sort to order 0 : rem=MISSING pulls them to the very top so a small tmMax can't clip a critical alert
            }
        }
        if (g_obLog) {   // ---- every stage done, focus monitor included. Disarm : one frame is the whole point. ----
            windower::debug::log("=== OBLOG : end (%d row(s) total this frame) ===", nb);
            g_obLog = 0;
        }
        if (f.game) for (int i = 0; i < f.game->nRecast && nr < 50; ++i) {   // recasts are TEXT-only (no menu-icon set exists)
            const GameState::RecastEntry& re = f.game->recasts[i];
            const char* nm = (re.kind == 0) ? abil_name_by_recast(re.recastId, jaBits, jaOk) : spell_name_by_recast(re.recastId);
            if (!nm) continue;
            // recasts are ALWAYS shown now (the family filter is buff-only ; recasts are your own cooldowns).
            recs[nr].rem = re.sec; recs[nr].icon = 0; recs[nr].name = nm; recs[nr].order = 0;
            if (re.ticks > 0) recs[nr].fine = re.ticks;   // the raw 1/60 s counter `sec` was ceil-ed from -> two recasts one second apart keep a stable order (Row::fine)
            // SCH stratagems : the raw recast 231 is the FULL charge-bar time, meaningless as a cooldown. The grimoire
            // poller already turns it into (charges available now, seconds to the NEXT charge) using the level/JP
            // interval -- reuse that. Show "Stratagem [3]" counting down to the next charge, so a full bar (no recast
            // entry) simply shows nothing and a recharging bar shows what you can spend right now.
            if (re.recastId == 231 && f.game->grimoire.visible) {
                recs[nr].pip = f.game->grimoire.charges;
                recs[nr].pipCol = f.game->grimoire.charges > 0 ? 0xFF74D074u : 0xFF9AB0C8u;   // green when you have some, grey at zero
                const int t = f.game->grimoire.timerSec;
                recs[nr].rem = (t >= 0) ? t : re.sec;   // seconds to the next charge (fallback to raw if somehow -1)
                // the charge countdown is the raw recast modulo one interval, and only its SECONDS survive that
                // arithmetic. Re-attach the sub-second part of the raw counter (the fraction the ceil added) so
                // this row sorts on the same fine scale as every other recast rather than on a whole second.
                if (t >= 0) { const int frac = (re.ticks > 0) ? (re.sec * 60 - re.ticks) : 0; const int ft = t * 60 - frac; recs[nr].fine = (ft > 0) ? ft : 0; }
            }
            ++nr;
        }
    }
    if (nb == 0 && nr == 0 && !editing) { rowsOut.nb = 0; rowsOut.nr = 0; return false; }   // was : return (nothing to draw)
    // THE ORDER OF THE ROWS : model/timers_sort.h (the rule, its history, and why close rows hold their place).
    // What stays here is the memory it needs -- where each row stood last frame.
    static unsigned lastSig[64]; static int lastN = 0;
    auto lastPos = [&](const Row& x) -> int {
        const unsigned s = timers_row_sig(x);
        for (int i = 0; i < lastN; ++i) if (lastSig[i] == s) return i;
        return -1;   // not on screen last frame -> nothing to hold on to
    };
    auto after = [&lastPos](const Row& x, const Row& y, int mode, bool recast) -> bool { return timers_row_after(x, y, mode, recast, lastPos); };
    g_lastRowN = nb;   // the harness reads this : hitting the 50 cap means rows are being dropped in silence
    { const int md = C.tmSortDur;
      for (int a = 1; a < nb; ++a) { Row t = bufs[a]; int b = a - 1; while (b >= 0 && after(bufs[b], t, md, false)) { bufs[b + 1] = bufs[b]; --b; } bufs[b + 1] = t; } }
    { lastN = nb < 64 ? nb : 64;   // what this frame settled on, so the next one can hold it
      for (int a = 0; a < lastN; ++a) lastSig[a] = timers_row_sig(bufs[a]); }
    { const int md = C.tmSortRec;
      for (int a = 1; a < nr; ++a) { Row t = recs[a]; int b = a - 1; while (b >= 0 && after(recs[b], t, md, true)) { recs[b + 1] = recs[b]; --b; } recs[b + 1] = t; } }
    // THE HINT LIVES ON THE ROW THAT NEEDS IT. A red OUT for a buff you never meant to keep is a mistake, and
    // the correction has to be reachable without remembering anything -- so the offending row carries the words
    // that fix it, and they vanish with it. Only the FIRST one: repeated down a column of alerts a hint becomes
    // noise. Placed after the sort (OUT rows carry rem = TM_REM_MISSING, so they are already on top) and before
    // the tmMax clamp, so the row it lands on is always one that is actually drawn.
    if (!preview && !editing) { for (int i = 0; i < nb; ++i) if (bufs[i].src == 6 && !bufs[i].post) { bufs[i].post = "  //aio out"; bufs[i].postCol = 0xFF7E8894u; break; } }
    if (nb > C.tmMax) nb = C.tmMax; if (nr > C.tmMax) nr = C.tmMax;

    // THE WATCHERS BELOW ARE NOT GUARDED, and that is the point of this block's shape. They used to sit inside
    // `#ifdef AIOHUD_PROBES` with the songdump ring, so they existed on the dev box and NOWHERE ELSE : a release
    // had no row-set flipwatch and two fewer capwatch tables, which is precisely backwards -- a witness for a
    // silent defect is worth nothing on the one machine where the defect is already being watched by hand.
    // (`//aio watch` reported 12 tables here and would have reported 10 to a tester. Found by the audit of
    // 2026-09-12, axes A and H, independently.) The cost in a release is the FNV hash below over at most 50 rows
    // once a frame ; the songdump RING, which writes, stays guarded.
    if (!preview) {
#ifdef AIOHUD_PROBES
        g_whyObN = 0; g_whyGrpN = 0;   // refilled each frame by the passes above -- reset before the next one
#endif
        unsigned sig = 2166136261u;   // FNV-1a over what identifies the rows, deliberately excluding `rem`
        for (int i = 0; i < nb; ++i) {
            sig = (sig ^ (unsigned)bufs[i].icon) * 16777619u;
            sig = (sig ^ (unsigned)bufs[i].src)  * 16777619u;
            for (const char* c = bufs[i].who;  c && *c; ++c) sig = (sig ^ (unsigned char)*c) * 16777619u;   // the PERSON is part of a row's identity : without it two allies carrying the same buff hash the same
            for (const char* c = bufs[i].name; c && *c; ++c) sig = (sig ^ (unsigned char)*c) * 16777619u;
            for (const char* c = bufs[i].tag;  c && *c; ++c) sig = (sig ^ (unsigned char)*c) * 16777619u;
        }
        // Two rules deciding one row set is what made songs trade places and a red row blink at 60 Hz. The
        // signature IS the decision, so it is the right thing to watch: if it alternates between two values
        // without ever settling, something is arguing (model/flipwatch.h).
        flipwatch("timers.rowset", sig, (unsigned)GetTickCount());
        // The monitor simply stops accepting when it is full (`if (s < 0 && fmN < FOCUS_MAX)`), so past the cap
        // a buff is never watched and its loss is never alerted -- the feature degrades into silence. It held
        // 24 until 2026-09-11, and a bard's own songs plus an alliance's buffs went past that every fight.
        capwatch("timers.focus", fmN, FOCUS_MAX);
        // The buff FILTER : one key per buff family the user has hidden or put on focus, job-agnostic, and it
        // simply stops accepting when full -- so past the cap a checkbox in the Timers panel silently does
        // nothing at all. Sampled from here rather than from the panel because the filter is read every frame
        // in play, and a saturated filter is a problem long before the user next opens the config.
        capwatch("config.tmbuffoff", ui_config().tmBuffOffN, UiConfig::TM_TRACK_MAX);
#ifdef AIOHUD_PROBES
        // //aio songdump layer 4 -- the rows as BUILT, each labelled with the pass that emitted it.
        // Recorded into a RAM ring, never to disk : an earlier version wrote a line per change and the extra frame
        // time was enough to make the ghost-song bug stop reproducing. Observation must be free, so the file write is
        // deferred to //aio songdump. Only a CHANGE in the row set (status / name / pass / tag) is kept.
        static unsigned lastSig = 0;
        if (sig != lastSig) {
            lastSig = sig;
            static const char* const SRC[7] = { "?", "?", "SELF-timer", "GEO-aura", "ALLY-AoE-group", "ALLY-single", "FOCUS-missing" };
            // STAMPED. Without a time this records the ORDER of changes but not their rhythm, and the whole
            // question is whether a row set flickers every frame or flips once a second -- two different defects
            // with two different remedies. Absolute ticks: the differences are what matter.
            sr_push("---- t=%u : Timers built %d row(s) ----", (unsigned)GetTickCount(), nb);
            for (int w = 0; w < g_whyGrpN; ++w) { const WhyGrp& g = g_whyGrp[w];
                const SpellRow* sp = spell_info(g.spell);
                sr_push("    WHY group  %-18s fresh=%d aoe=%d meHas=%d selfCast=%d lag=%d | allies=%d countHas=%d -> effN=%d %s",
                        (sp && sp->en) ? sp->en : "?", g.fresh, g.aoe, g.meHas, g.selfCast, g.hasLag,
                        g.allies, g.countHas, g.effN, g.drop ? "DROP" : (g.group ? "GROUPED" : "per-person")); }
            for (int w = 0; w < g_whyObN; ++w) { const WhyOb& o = g_whyOb[w];
                const SpellRow* sp = spell_info(o.spell);
                sr_push("    WHY entry  %-18s tgt=%08X %s | mirror=%d aoe=%d evicted=%d expTick=%u selfExp=%u dTick=%d castMs-stamp=%d",
                        (sp && sp->en) ? sp->en : "?", o.target, o.fresh ? "FRESH " : "LAGGARD",
                        o.mirrorSelf, o.aoe, o.evicted, o.expTick, o.selfExp,
                        o.selfExp ? (int)(o.expTick - o.selfExp) : 0, (int)(o.castMs - o.stamp)); }
            for (int w = 0; w < g_whyFmN; ++w) { const WhyFm& f2 = g_whyFm[w];
                const SpellRow* sp = spell_info(f2.spell);
                sr_push("    WHY focus  %-18s tgt=%08X self=%d st=%u | copies=%d newer=%d -> %s  listReady=%d lost=%d",
                        (sp && sp->en) ? sp->en : "?", f2.target, f2.self, f2.status,
                        f2.copies, f2.newer, f2.has ? "up" : "MISSING", f2.listReady, f2.lost); }
            for (int i = 0; i < nb; ++i) {
                // flag a row the game itself no longer carries : that is precisely what a ghost is.
                bool inMem = false;   // (meHas is scoped to the build block above -- read the same source directly)
                if (f.game) for (int k = 0; k < f.game->nbuff; ++k) if ((int)f.game->buffs[k] == bufs[i].icon) { inMem = true; break; }
                const bool ghost = (bufs[i].icon > 0 && bufs[i].src != 6 && !inMem);
                sr_push("  [%-14s] st=%-4d rem=%-6d \"%s%s%s\"%s%s%s",
                        SRC[(bufs[i].src >= 0 && bufs[i].src < 7) ? bufs[i].src : 0],
                        bufs[i].icon, bufs[i].rem, bufs[i].who ? bufs[i].who : "", bufs[i].who ? " - " : "", bufs[i].name ? bufs[i].name : "?",
                        bufs[i].tag ? bufs[i].tag : "", bufs[i].post ? bufs[i].post : "",
                        ghost ? "   <<< GHOST : not in the game's own buff list" : "");
            }
        }
#endif
    }
    rowsOut.nb = nb; rowsOut.nr = nr;
    return true;
}

// ---- THIS MODULE'S CHECKS (model/selftest.h) ----------------------------------------------------------------
// Each one has a source the module does not control -- a cap, a domain, or the clock -- so none of them can be
// satisfied by the code simply agreeing with itself. Ids are stable: they are what a bug report names.
static int timers_checks(CheckFail* out, int cap) {
    int n = 0;
    const unsigned nowMs = GetTickCount();
    #define FAIL(ID, SEV, ...) do { if (n < cap) { lstrcpynA(out[n].id, ID, sizeof(out[n].id)); out[n].sev = (SEV); \
        _snprintf(out[n].detail, sizeof(out[n].detail), __VA_ARGS__); out[n].detail[sizeof(out[n].detail)-1] = 0; ++n; } } while (0)

    // 1. The focus monitor is full. Structural: FOCUS_MAX is the array, and past it new entries are refused with no
    //    sign -- and since the SAME list drives your OUT alerts, they stop appearing for anything new.
    if (fmN > g_fmPeak) g_fmPeak = fmN;
    if (g_fmPeak >= FOCUS_MAX)
        FAIL("TM.FOCUS_FULL", CHK_WARN, "the focus monitor PEAKED at %d of %d -- refused entries are never watched, so their OUT alerts cannot happen", g_fmPeak, FOCUS_MAX);

    // 2. An entry that has outlived any buff. No buff in the game is maintained for four hours by one cast, so
    //    an entry that old is one the purge never reached (audit S2-6: an alliance target kept lostMs at 0
    //    every frame, and the only purge branch requires lostMs != 0).
    for (int q = 0; q < fmN; ++q) {
        const unsigned age = nowMs - fm[q].bornMs;
        if (fm[q].bornMs && age > 4u * 3600u * 1000u)
            FAIL("TM.FOCUS_STALE", CHK_WARN, "focus entry #%u (%s status %u) has been alive %u min -- it is never being purged",
                 (unsigned)fm[q].tag, fm[q].name[0] ? fm[q].name : "you", (unsigned)fm[q].status, age / 60000u);
    }

    // 3. The row builder hit its cap. Domain: bufs[50] is the array, and beyond it rows are dropped by arrival
    //    order rather than by importance -- so what you stop seeing is arbitrary.
    if (g_lastRowN >= 50)
        FAIL("TM.ROWS_CAP", CHK_WARN, "the last build produced %d rows and the cap is 50 -- rows are being dropped", g_lastRowN);

    // 4. The ally-buff cache is full. Same shape, different array (otherBuffs_[32]): once full, a buff you cast
    //    on someone new is simply not tracked.
    if (g_obPeak >= PartyState::OB_MAX)
        FAIL("TM.OB_FULL", CHK_WARN, "the ally-buff table PEAKED at %d of %d -- past the cap the oldest row is evicted, so groups lose members and rows scatter", g_obPeak, PartyState::OB_MAX);

    // 5. Expired timers are not being pruned. The clock is the independent source: a timer whose expiry passed
    //    ten minutes ago has no business still being in the list.
    //    THIS CHECK WAS WRONG WHEN FIRST WRITTEN, and its own first report is what showed it -- twice, on two
    //    characters, claiming a timer had expired 1 651 684 393 seconds ago. Two defects, both here:
    //      - a PERMANENT buff (Signet, Sanction, ...) carries the sentinel expiry 0x7FFFFFFF, and subtracting it
    //        from the current tick produces exactly that nonsense. It is not late; it never expires.
    //      - a tick is 1/60 s (ticks_to_sec_ceil), so the old threshold of 600 meant TEN SECONDS, not ten
    //        minutes -- it would have fired on any timer a few seconds past its end, mid-prune.
    //    Kept rather than deleted because the CONCEPT is sound and the failure was arithmetic. A check whose
    //    idea is noisy gets deleted; one with a bug gets fixed, once the bug is understood.
    { int nb2 = 0; const BuffTimer* bt = party().buff_timers(nb2);
      const unsigned tick = ffxi_now_tick(); int stuck = 0; int worstSec = 0;
      for (int i = 0; i < nb2; ++i) {
          if (!bt[i].expiry || bt[i].expiry == FFXI_EXPIRY_PERMANENT) continue;   // no countdown to be late for
          const int lateTicks = (int)(tick - bt[i].expiry);                       // signed : u32 wrap is the intended maths
          if (lateTicks > 60 * 600) { ++stuck; const int s = lateTicks / 60; if (s > worstSec) worstSec = s; } }
      if (stuck) FAIL("TM.EXPIRED_STUCK", CHK_WARN, "%d self timer(s) expired up to %d s ago are still listed -- pruning has stopped", stuck, worstSec); }

    // 6. A status id outside the icon grid. Domain: the atlas is 640 cells and the cell index IS the status id,
    //    so anything past it draws from outside the sheet (or nothing) and means the source was misread.
    { int nb2 = 0; const BuffTimer* bt = party().buff_timers(nb2);
      for (int i = 0; i < nb2; ++i) if (bt[i].id >= 640) { FAIL("TM.STATUS_RANGE", CHK_WARN, "self timer carries status id %u, outside the 0..639 the icon sheet holds", (unsigned)bt[i].id); break; }
      int no = 0; const PartyState::OtherBuff* ob = party().other_buffs(no);
      for (int i = 0; i < no; ++i) if (ob[i].status >= 640) { FAIL("TM.STATUS_RANGE_ALLY", CHK_WARN, "an ally buff carries status id %u, outside the 0..639 the icon sheet holds", (unsigned)ob[i].status); break; } }

    #undef FAIL
    return n;
}

void timers_register_checks() { selftest_add("timers", timers_checks); }

} // namespace aio
