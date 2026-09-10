// hud_timers.cpp -- split out of hud.cpp (pure move). Timers box renderer.
#include "ui/hud.h"
#include "ui/hud_internal.h"
#include "model/ui_config.h"
#include "ui/text_style.h"
#include "ui/box_style.h"
#include "model/party_state.h"
#include "gfx/draw.h"
#include "gfx/d3d.h"
#include "model/abilities_gen.h"
#include "model/spells_gen.h"
#include "model/buffs_gen.h"
#include "model/song_family_gen.h"   // song_family(spell) : identify a BRD song (spell-keyed -> no status family-collapse) for the song-OUT rule
#include "model/tb_buff_gen.h"       // spell_buff(spell)->skill : identify a GEO Indicolure (skill 44) for the Indi--replaced OUT rule
#include "model/action_status_gen.h"   // is_debuff_status : keep enfeebles (Blind/Poison/Slow) out of the buff list
#include "model/mobskills_gen.h"
#include "gfx/texture.h"
#include "model/paths.h"
#include "model/gamestate.h"
#include "windower_debug.h"   // //aio songlog layer 4 (dev diagnosis ; same precedent as ui/hud.cpp)
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <algorithm>

#include "ui/buff_atlas.h"
#include "ui/tex_retry.h"   // bounded lazy texture load (Help atlas) -- rule 10
#include "model/focus_rules.h"   // the FOCUS monitor's three judgements, pure and tested
#include "model/ally_group.h"   // the (AoE N) count and the group-or-name decision, pure and tested
#include "model/selftest.h"   // this module's own checks, run by the in-game watcher

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
struct FocusMem { unsigned target; unsigned short status, spell; unsigned char isAbil, self, zoneCheck, muted, tag, seen, alerting; unsigned lostMs, muteRef, bornMs, rank; char name[20]; };   // rank : which of two same-status songs survives -- the ally cast's startMs, or your own copy's expiry. NOT bornMs : an entry is reused across re-casts, so its birth says nothing about which cast is fresher.
// bornMs : when this entry was created. Read by NOTHING that decides anything -- it exists so the harness
// can say "this entry has been alive 4 h", which is the only way an immortal entry (the purge that never
// runs, audit S2-6) is visible from outside. A field a decision depends on could not be added this cheaply.
// `alerting` = this entry drew its red OUT row on the LAST frame. It is set where the row is emitted and nowhere
// else, so it means exactly "what you can see in red right now" -- which is what //aio out alerts takes off. The
// alternative (re-deriving the condition in the command) would be a second copy of a decision that already has
// six suppression branches (song replaced, unrecoverable 5th song, Indi- swapped, hold expired, muted, no data),
// and the day one of them moved the two copies would disagree in silence.
static FocusMem fm[24];
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
    for (unsigned char t = 1; t <= 24; ++t) {
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
static char  g_srRing[256][160];
static int   g_srHead = 0, g_srCount = 0;
static void sr_push(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    _vsnprintf(g_srRing[g_srHead], sizeof(g_srRing[0]) - 1, fmt, ap);
    va_end(ap);
    g_srRing[g_srHead][sizeof(g_srRing[0]) - 1] = 0;
    g_srHead = (g_srHead + 1) % 256; if (g_srCount < 256) ++g_srCount;
}
void songrow_ring_dump() {
    const int start = (g_srHead - g_srCount + 256) % 256;
    windower::debug::log("SONGROW ======== %d recorded row-set change(s), oldest first ========", g_srCount);
    for (int i = 0; i < g_srCount; ++i) windower::debug::log("SONGROW %s", g_srRing[(start + i) % 256]);
    g_srHead = 0; g_srCount = 0;
}
#endif

// ============================ TIMERS box (self buff timers + recasts) ============================
// Exact server-sent buff durations (0x063 type-9 -> party().buff_timers()). Each row = the buff's status icon (the
// SAME atlas as the Player / Party boxes) + a MM:SS countdown, sorted soonest-first, coloured by urgency (white ->
// orange <=30s -> flashing red <=10s). Placed via //aio edit (EDITBOX_TIMERS).
// ---- Timers typography (own per-element TextStyle : TM_HEADER column titles / TM_BODY names + MM:SS) ----
static Font* tm_font(const Frame& f, int e) { return te_font(f, ui_config().tmText[e]); }
static inline float tm_sz(int e, float base) { return te_sz(ui_config().tmText[e], base); }
static inline float tm_ow(int e, float base) { return te_ow(ui_config().tmText[e], base); }
static inline u32   tm_col(int e, u32 base)  { return te_col(ui_config().tmText[e], base); }

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
static bool tm_self_focus_on(const UiConfig& C, int /*job*/, unsigned status) {
    return C.tm_buff_off(UiConfig::TM_KEY_FOCUS | status);
}
// SELF-CARRIED buffs : Food / Aftermath / conquest (Signet, Sanction, Sigil, Ionis) / synthesis Imagery. NO job
// CASTS these, so buff_caster_for can't attribute them and self_can_produce_buff says no -> the "buff source" filter
// (srcKeeps) would classify them as "not yours" and hide them under anything but "All". But they ARE yours (you
// carry them), not someone's buff cast ON you, so they must be exempt from the source filter -- their family-filter
// toggle is their only control. Status ids mirror the EXTRA_FAM list in scripts/gen_job_track.py (keep in sync).
static inline bool tm_self_carried(unsigned st) {
    return st == 251                                            // Food
        || (st >= 270 && st <= 272)                            // Aftermath: Lv.1 / Lv.2 / Lv.3 (3 tiers ; 273 generic is legacy)
        || st == 253 || st == 256 || st == 268 || st == 512    // Signet / Sanction / Sigil / Ionis
        || (st >= 235 && st <= 243);                           // synthesis Imagery (Fishing .. Cooking)
}

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

void timers_draw(const Frame& f, bool preview, float ovX, float ovY, float ovS, float screenW, float screenH,
                 u32 buffAtlas, bool measureOnly = false, float* outW = 0, float* outH = 0) {
    const UiConfig& C = ui_config();
    if (!C.tmShow) return;
    const bool editing = C.editLayout && !preview;
    u32 dev = f.dev;
    if (!f.font && !f.fonts) return;

    // icon = buff status id ; both=1 -> icon+name (buff on an ally) ; order : 0 = your own buffs, 1+partyPos = allies.
    // COR roll : name = "Chaos Roll", pip = the coloured pip number (0 = none), post = " (AoE 6)" suffix -> drawn as
    // "Chaos Roll [5] (AoE 6)" with ONLY the pip in pipCol (unlucky=red, lucky/11=green, else white). nameCol overrides
    // the whole-name colour (unused now that only the pip is tinted).
    struct Row { int rem; int fine; unsigned char fineClk; int icon; const char* name; const char* who; int order; u32 nameCol; int pip; u32 pipCol; const char* post; u32 postCol; const char* tag; u32 tagCol; int src; int mark; };   // who : the PERSON this row is about (0 = you) -- kept SEPARATE from `name` (the spell) because the display mode governs the icon and the spell name, never the person : "Icon" = icon + who, "Name" = who + spell, "Both" = the three. Rows used to carry one "Aeryn - Haste" string and a `both` flag that forced icon+name on them, so an ally row ignored the mode outright -- and the same buff switched between the grouped form (which obeyed it) and the per-person form (which did not) as you re-cast, which is what "it does not follow" was.   // mark : the focus-monitor number (0 = not monitored) -- what //aio out takes   // tag : BRD song modifiers "(SV)(T)" drawn in tagCol, between the name and the AoE suffix
    static const int TM_REM_MISSING = -1000000000;   // FOCUS alert row : an ally is MISSING a critical buff -> timer shows "OUT" in red, sorts to the very top
    // `fine` = the same remaining time as `rem` but in TICKS (1/60 s), used ONLY to sort. `rem` is ceil-ed to whole
    // seconds for display, so two timers a fraction of a second apart show the SAME number every other second --
    // and the tie-break below (icon, then name) then ordered them the other way round from the second before.
    // The two rows swapped places once a second, forever : the "yoyo". `fine` ranks those ties by the instant the
    // buff really expires, which the second alone cannot see -- the equal-second pair then keeps ONE order for
    // its whole life. It is a tie-break only, never the primary key : the row sources round differently (a self
    // buff ceils its tick, an ally estimate floors its ms), so leading with it could sort a visible 4:05 above a
    // visible 4:04. Whatever sets `rem` must set `fine` from the SAME clock, or the row sorts by one and reads
    // the other. TM_FINE_NONE = "no sub-second source" -> fall back to rem (see fineOf) ; only rows that never
    // tick (the frozen demo/preview rows) are allowed to stay there.
    static const int TM_FINE_NONE = -2000000000;
    // WHICH CLOCK a row's `fine` was read from. Sub-second ordering only means something between rows
    // measured the SAME way: your 0x063 expiry is absolute server ticks, an ally estimate is GetTickCount
    // arithmetic, and the two drift past each other. Comparing them is comparing noise -- reported
    // 2026-09-10 as two songs on the same timer "qui n'arretent pas de passer l'une en dessous de l'autre":
    // an AoE Minuet (your own expiry) beside a Pianissimo Ballad on an ally (its estimate), whose fine
    // values crossed and re-crossed for as long as both were up.
    static const unsigned char FCLK_NONE = 0, FCLK_SELF = 1, FCLK_EST = 2;
    static Row bufs[50], recs[50]; int nb = 0, nr = 0;
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
        const unsigned now = ffxi_now_tick(), nowMs = GetTickCount();
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
        // FRESH vs LAGGARD. A song copy on an ally is FRESH only when it tracks YOUR CURRENT cast of THIS SPELL : still
        // mirroring your live self timer (just re-sung), or its frozen expTick matches your current self expiry for this
        // spell. Otherwise it is a LAGGARD -> a named per-ally row on its own timer, never folded into a self AoE :
        //   - a re-sing MISSED the member : it keeps the OLD expTick (older/shorter cast).
        //   - you no longer hold THIS SPELL yourself (selfExp==0) : e.g. Victory March was pushed off you by 4 new songs
        //     but is still on Kaories. This case is why selfExp==0 must be LAGGARD, not fresh : status ids family-collapse
        //     (Honor March + Victory March are BOTH 214), so countHas(214) would count YOUR Honor March into Victory
        //     March's group and draw a phantom "(AoE 2)". Songs are self-centred -- a real current AoE song is always on
        //     you (selfExp!=0) -- so selfExp==0 reliably means "not a self AoE, list the ally who still carries it".
        // Only REAL AoE spells split (rolls/single-target keep their existing one-row-per-ally path). Shared by the
        // pass-1 grouping and the pass-3 per-ally emit so the two ALWAYS agree on which members are laggards.
        auto obFresh = [&](const PartyState::OtherBuff& o) -> bool {
            if (o.isAbil || !o.aoe) return true;
            if (o.mirrorSelf) return true;   // just cast (< 2s, still mirroring your live self timer) -> fresh, even before your 0x063 self timer has landed (no post-cast flicker)
            const unsigned selfExp = party().self_buff_expiry_for(o.status, o.spell);
            if (!selfExp) return party().in_zone_grace() && song_family(o.spell) <= 0;   // normally selfExp==0 -> you don't hold this -> laggard. EXCEPT the post-zone repop window : your 0x063 self timer reads 0 for a few seconds while it repopulates, but an ENHANCING buff PERSISTS across a zone -> hold it FRESH (grouped, on the pre-zone estimate) during the grace so it never flashes per-person ; the re-align snaps it to the real self timer the instant it lands. Songs (lost on zone) stay laggard.
            int d = (int)(o.expTick - selfExp); if (d < 0) d = -d;
            return d <= 600;                 // 600 ticks = 10s : casts more than 10s apart are distinct generations
        };
        if (C.tmMine) {
            ob = party().other_buffs(no);   // (prune_other_buffs_worn now runs once per frame from the model tick, not from here)
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
                const unsigned char fb = obFresh(ob[i]) ? 1 : 0;   // FRESH (your current cast) vs LAGGARD (an older cast a re-sing missed) -- see obFresh
                // Key on (spell, freshness, AND delivery). A single-target re-cast is a DIFFERENT cast from the
                // AoE that preceded it : it has its own, longer timer. Keyed only by (spell, fresh) the two
                // merged, because refreshing an ally's slot clears its `aoe` flag and obFresh() calls any
                // single-target entry fresh -- so an AoE Protect on the party followed by a Protect on one ally
                // drew ONE "(AoE 3)" row carrying the SELF timer, and the ally's newer duration was invisible.
                // Single-target casts still group with each other under "Grouped" ; they simply never merge
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
        // ONE definition of "does the buff-source filter keep this timer", shared by the row emit AND by the FOCUS
        // monitor. They used to disagree: the emit applied the filter, the monitor did not -- so under "Mine only" a
        // party WHM's Haste never warned you it was about to expire, then screamed HASTE OUT in red the moment it did.
        // Half a feature reachable, half not. Same verdict for both, so they are reachable or unreachable together.
        auto srcKeeps = [&](unsigned short status, unsigned expiry, int timerIdx) -> bool {
            if (C.tmBuffSrc == TMSRC_ALL) return true;
            if (tm_self_carried(status)) return true;   // Food/Signet/Craft/Aftermath : self-carried, no external caster -> always yours ; the family-filter toggle is their sole control (else "Mine only" hides them despite being set to Show)
            // A trust/chemist multi-stat MIX boost (a STR..CHR you did NOT cast, co-expiring with a sibling boost) is
            // decided FIRST -- ahead of the caster lookup -- because a stale buffCaster_ can still name YOU on it (the
            // attribution is never cleared when the buff wears off), which would otherwise keep it as "your own".
            if (party().is_foreign_stat_mix(status, expiry, timerIdx))
                return (C.tmBuffSrc == TMSRC_TRUSTS);                           //   "me+trusts" keeps it ; "mine"/"players" hide it
            const unsigned caster = party().buff_caster_for(status, expiry, timerIdx), me = party().self_id();
            if (caster != 0 && caster == me) return true;                       // your own -> always kept
            if (caster != 0) {
                const bool trust = party().is_trust(caster);
                if (C.tmBuffSrc == TMSRC_MINE) return false;
                if (C.tmBuffSrc == TMSRC_PLAYERS && trust)  return false;
                if (C.tmBuffSrc == TMSRC_TRUSTS  && !trust) return false;
                return true;
            }
            if (party().self_can_produce_buff(status, jaBits, jaOk)) return true;   // unknown but your job can make it
            if (C.tmBuffSrc == TMSRC_MINE) return false;
            bool ph = false, th = false; party().buff_source_jobs(status, ph, th);
            if (C.tmBuffSrc == TMSRC_PLAYERS && !ph) return false;
            if (C.tmBuffSrc == TMSRC_TRUSTS  && ph && !th) return false;
            return true;
        };
        const int trkJob = party().self_main_job();   // "track per job" filter : a buff/recast whose key is in C.tmTrackOff[trkJob] is hidden
        // ---- pass 2 : your OWN self buffs (exact server timers). A self buff that matches an AoE group you cast folds
        //      INTO that group (you count, your exact timer drives it) instead of getting its own row. ----
        int n = 0; const BuffTimer* bt = party().buff_timers(n);
        for (int i = 0; i < n && nb < 50; ++i) {
            if (bt[i].expiry == FFXI_EXPIRY_PERMANENT) continue;   // client's "permanent" sentinel -> it draws no countdown, nor do we
            int fine = (int)(bt[i].expiry - now); if (fine < 0) fine = 0;   // the same instant, un-ceil-ed -> sort key (Row::fine)
            int rem = ticks_to_sec_ceil((int)(bt[i].expiry - now));   // CEIL, like the client (see ticks_to_sec_ceil) ; not const : clamped to 0 below
            // Our timer runs ~2s AHEAD of the client (measured): at rem 0 the game still shows the icon for about
            // two more seconds. Dropping the row at 0 while the red OUT alert only fires once the buff really
            // leaves the list left a visible HOLE between the two. Hold the row at 0:00 for as long as the buff is
            // genuinely still on you, so the hand-off row -> alert is seamless. Only for a buff we still hold:
            // meHas is authoritative now that an empty list is distinguished from no data.
            if (rem > 6 * 3600 && !tm_self_carried(bt[i].id)) continue;   // 6h cap drops garbage/absurd timers -- EXCEPT self-carried area buffs, which legitimately run for many hours (San d'Oria base Signet = 13h). fmt() already renders them H:MM:SS ; the family toggle stays their control
            if (rem <= 0) { if (!meHas((int)bt[i].id)) continue; rem = 0; }
            // DEBUFFS (Blind / Poison / Slow / Dia / Bio...) leak into the 0x063 self-buff list -- they are NOT buffs,
            // so never in the Duration column (they'll get their own detachable column). Dropped for everyone.
            if (is_debuff_status(bt[i].id)) continue;
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
            // Buff filter : JOB-AGNOSTIC, keyed by STATUS (the family filter). Hidden -> drop the row, UNLESS it is
            // Hidden+Focus and expiring (surface it under the warn threshold as the alert).
            if (C.tm_buff_off((unsigned)bt[i].id)) {
                if (!(tm_self_focus_on(C, trkJob, bt[i].id) && rem < C.tmFocusWarn)) continue;
            }
            // GEO aura noise : the geomancy effect status (542-556 Boosts) and "Colure Active" (612) pulse every ~3s
            // in 0x063 ; hide them (the Indi- YOU carry is redrawn as a stable computed row below).
            if (party().geo_aura_remaining(bt[i].id) >= 0) continue;
            if ((bt[i].id >= 542 && bt[i].id <= 556) || bt[i].id == 612) continue;
            if (!srcKeeps(bt[i].id, bt[i].expiry, i)) continue;   // buff-SOURCE filter (shared with the FOCUS monitor)
            if (!meHas(bt[i].id)) continue;   // cross-check the REAL buffs : drop a stale 0x063 entry (e.g. a replaced Corsair roll the game already removed). meHas already returns true when the list is unavailable, so a zone cannot wipe the rows here.
            // WHICH ally group does this self buff belong to ? Groups are built per SPELL, so matching them by STATUS
            // collapsed two different songs that share one status -- Minuet IV + Minuet V (both status 198), Honor March
            // + Victory March (both 214) -- into the FIRST group found. Both folded there, one row was emitted for two
            // songs, and grp[].rem then overwrote the survivor's countdown (captured : 4 songs in game, 3 rows drawn).
            // So resolve the SPELL first and match on it ; fall back to the status only when this status carries a
            // single timer, where the ambiguity cannot arise.
            const unsigned ssid = party().self_buff_spell_ranked(bt[i].id, bt[i].expiry, i);   // spell/tier (Valor Minuet V), disambiguating same-status buffs by expiry rank
            int sameSt = 0; for (int k = 0; k < n; ++k) if (bt[k].id == bt[i].id) ++sameSt;
            int gi = -1;
            // Fold ONLY into a FRESH group : your self buff IS the current cast, never a laggard row.
            // Since groups are keyed by delivery too, one spell can now have TWO fresh groups -- an AoE
            // generation and a single-target one. Your own copy belongs to the AoE : that is the cast that also
            // hit you and whose exact 0x063 timer the group displays. Prefer it ; fall back to a single-target
            // group only when no AoE one exists (you re-cast on one ally without touching yourself).
            for (int k = 0; k < ng; ++k) if (grp[k].spell == ssid && grp[k].fresh && grp[k].aoe) { gi = k; break; }
            if (gi < 0) for (int k = 0; k < ng; ++k) if (grp[k].spell == ssid && grp[k].fresh) { gi = k; break; }
            if (gi < 0 && sameSt < 2) for (int k = 0; k < ng; ++k) if (grp[k].status == bt[i].id && grp[k].fresh && grp[k].aoe) { gi = k; break; }
            if (gi < 0 && sameSt < 2) for (int k = 0; k < ng; ++k) if (grp[k].status == bt[i].id && grp[k].fresh) { gi = k; break; }
            // no fresh group (you re-sang on yourself only, the ally is a laggard) -> gi stays -1 : your buff emits as its
            // own row and the laggard group draws separately, which is exactly the split.
            // Fold your own copy into the ally row ONLY when that row will actually be a grouped "(AoE N)" line (real AoE
            // or "group ally buffs" on). In per-person mode a single-target spell you also put on yourself must stay a
            // SELF row -- pass 3's per-ally branch only emits allies, so folding here would make your own buff vanish.
            // Likewise if the ALLY scope hides this status (and it's not an expiring focus), pass 3 drops the group -> don't
            // fold, or a self-Tracked buff would vanish behind an ally-Hidden setting.
            const bool allyHides = C.tm_buff_off((unsigned)bt[i].id)   // one state per buff : the ally copy honours the same family filter
                                   && !(C.tm_buff_off(UiConfig::TM_KEY_FOCUS | (unsigned)bt[i].id) && rem < C.tmFocusWarn);
            // NB : expTick canNOT be used here as a "same cast" test. Captured : two March groups (Honor + Victory)
            // both carried expTick 682 while the Victory March self timer read 712 -- the frozen expiry is shared
            // between same-status songs, not per cast. Gating the fold on it made the fold never fire for the second
            // song, so every March was drawn TWICE (own row + group row).
            // Count = 0x076 real carriers (countHas), FLOORED by the allies you just sang to (grp[].allies, from ob[]).
            // After a //reload the 0x076 ally-buff cache is empty -- the server won't re-send it until a member's buffs
            // change -- so countHas saw only YOU and the "(AoE N)" group never re-formed even after a recast (captured :
            // countHas=1 while gi=0/aoe=1). ob[] holds your OWN fresh casts (restored rows are discarded on load), so it
            // is an authoritative floor ; prune_other_buffs_worn drops it once 0x076 flows again and shows a real loss.
            // When a LAGGARD sibling exists for this spell, the fresh count MUST come from ob[]'s per-cast buckets, not
            // countHas : 0x076 (countHas) sees the status on the laggard member too and can't tell it from a fresh copy,
            // so it would re-inflate the fresh "(AoE N)" back to including the laggard -- the exact merge we're undoing.
            bool hasLag = false; for (int k = 0; k < ng; ++k) if (grp[k].spell == ssid && !grp[k].fresh) { hasLag = true; break; }
            int effHas;
            if (hasLag) { effHas = (gi >= 0 ? grp[gi].allies : 0) + (meHas(bt[i].id) ? 1 : 0); }   // split : fresh members only (you + allies you re-hit) ; solo re-sing -> 1 -> no fold, own row
            else { effHas = countHas(bt[i].id); if (gi >= 0) { const int est = grp[gi].allies + (meHas(bt[i].id) ? 1 : 0); if (est > effHas) effHas = est; } }
            const bool folds = (gi >= 0 && effHas >= 2 && (grp[gi].aoe || C.tmAllyGroup != 0) && !allyHides);
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
                                     (gi >= 0) ? grp[gi].aoe : -1, C.tmAllyGroup, allyHides ? 1 : 0,
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
            // For BANDING, unknown is its own thing. The source filter already treats caster==0 as "infer", but the
            // sort treated it as "mine", so every unattributed row -- food, gear, a 3000-TP boost we failed to parse --
            // sorted ABOVE your own live songs, which is the exact complaint this banding was added to fix.
            // Display tiers (top -> bottom) : (1) YOUR buffs 0-1, (3) buffs YOU cast on allies 10-28, (2) buffs a
            // PLAYER put on you -- GROUPED BY that player (party position) 40-57, (4) TRUSTS last 90-107.
            bufs[nb].order = (rowCaster == party().self_id()) ? 0                                        // your own buffs -> very top
                           : (rowCaster == 0)                 ? 1                                        // unknown caster (food/gear) = probably yours
                           : party().is_trust(rowCaster)      ? (90 + party().party_order(rowCaster))    // a trust's buff on you -> LAST, grouped by trust
                                                              : (40 + party().party_order(rowCaster));   // a real player's buff on you (Kaories' rolls...) -> grouped BY that player
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
            gin.userGroupPref   = C.tmAllyGroup != 0;
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
            if (party().bcapt_armed()) {   // //aio bcaptlog OBGRP : the group-vs-per-ally verdict + its inputs -- WHY an Accession buff draws (AoE N) or a named per-ally row. Throttled to CHANGE (else 60 Hz flood).
                static unsigned short olS[32]; static unsigned char olF[32]; static short olN[32], olG[32]; static bool olInit = false;
                if (!olInit) { olInit = true; for (int q = 0; q < 32; ++q) { olS[q] = 0xFFFF; olF[q] = 0xFF; olN[q] = -1; olG[q] = -1; } }
                const int oidx = k & 31;
                if (olS[oidx] != grp[k].spell || olF[oidx] != grp[k].fresh || olN[oidx] != (short)effN || olG[oidx] != (short)(group ? 1 : 0)) {
                    olS[oidx] = grp[k].spell; olF[oidx] = grp[k].fresh; olN[oidx] = (short)effN; olG[oidx] = (short)(group ? 1 : 0);
                    windower::debug::log("OBGRP spell=%u \"%s\" st=%u fresh=%d aoe=%d allies=%d effN=%d selfHas=%d allyGroup=%d -> %s",
                                         grp[k].spell, en ? en : "?", grp[k].status, grp[k].fresh, grp[k].aoe, grp[k].allies, effN, grp[k].selfHas, C.tmAllyGroup, group ? "GROUPED (AoE N)" : "PER-ALLY");
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
            if (zone != fmZone) { fmZone = zone; fmZoneAtMs = nowMs; fmZoneGraceMs = nowMs + 8000; }
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
            auto focusHas = [&](const FocusMem& e) -> bool {                                    // is THIS song still up (list assumed ready)
                int newer = 0;
                for (int q2 = 0; q2 < fmN; ++q2) {
                    const FocusMem& o = fm[q2];
                    if (o.self != e.self || o.status != e.status) continue;
                    if (!e.self && o.target != e.target) continue;
                    if (focus_newer_sibling(e.rank, e.spell, o.rank, o.spell)) ++newer;
                }
                return focus_copies_cover(newer, focusCopies(e)); };
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
                                         st, buff_status_name(st), meHas((int)st) ? 1 : 0, tm_self_focus_on(C, trkJob, st) ? 1 : 0,
                                         C.tm_buff_off(UiConfig::TM_KEY_FOCUS | st) ? 1 : 0);
                }
                if (st >= 1024 || is_debuff_status(st) || !meHas((int)st) || !tm_self_focus_on(C, trkJob, st)) continue;
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
                int s = -1; for (int q = 0; q < fmN; ++q) if (fm[q].self && fm[q].status == st && fm[q].spell == selfSp) { s = q; break; }
                if (s < 0 && fmN < 24) { s = fmN++; fm[s].spell = selfSp; fm[s].target = meId; fm[s].status = (unsigned short)st; fm[s].self = 1; fm[s].isAbil = 0; fm[s].lostMs = 0; fm[s].muteRef = 0; fm[s].zoneCheck = 0; fm[s].muted = 0; fm[s].alerting = 0; fm[s].tag = fm_free_tag(); fm[s].seen = 1; fm[s].bornMs = GetTickCount(); fm[s].name[0] = 0; }
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
                if (s < 0 && fmN < 24) { s = fmN++; fm[s].spell = ob[i].spell; fm[s].target = ob[i].target; fm[s].status = (unsigned short)st; fm[s].self = 0; fm[s].lostMs = 0; fm[s].muteRef = 0; fm[s].zoneCheck = pending; fm[s].muted = 0; fm[s].alerting = 0; fm[s].tag = fm_free_tag(); fm[s].seen = 1; fm[s].bornMs = GetTickCount(); }
                else if (s < 0) { static windower::debug::LogOnce<2> onceFull;   // SAY it. A silent refusal here is indistinguishable from "no buff to watch".
                    if (onceFull.first(0)) windower::debug::log("FOCUS monitor FULL (%d entries) -- new ally focus buffs are NOT tracked this session", 24); }
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
            // A song you cast on an ally that vanished because YOU just SINGLE-TARGETed a DIFFERENT song onto that SAME
            // ally (Pianissimo) is a DELIBERATE slot swap, not a loss -> no red OUT, just depop. Signal : a newer,
            // different, SINGLE-TARGET song on the same target, cast in the last few seconds (the slot casualty leaves in
            // the same 0x076 update the replacement lands in). The `!ob[i].aoe` gate is load-bearing : an AoE song
            // re-stamps startMs on EVERY member each cast, so without it the 6s window would sit open across your whole
            // rotation and mask a real dispel on anyone you're singing to. Pianissimo is the only way to single-target a
            // song, so `!aoe` isolates the deliberate swap. Ally songs only : your own re-song is handled elsewhere.
            auto songReplaced = [&](const FocusMem& e) -> bool {
                if (song_family(e.spell) <= 0) return false;
                // (a) Pianissimo : a SINGLE-TARGET song landed on this same ally and took the slot. Ally rows only --
                //     a Pianissimo song never lands on you, so a self row can never be its casualty.
                if (!e.self)
                    for (int i = 0; i < no; ++i)
                        if (ob[i].target == e.target && ob[i].spell != e.spell && song_family(ob[i].spell) > 0 && !ob[i].aoe
                            && (unsigned)(nowMs - ob[i].startMs) < 6000u) return true;   // single-target replacer only ; 6s covers the 0x076 cadence, short enough a real later dispel still OUTs
                // (b) THE GAME PUSHED IT OUT to fit a new song. Not decided here: the model named the victim at
                //     CAST time, while the set was still intact (PartyState::song_was_evicted, model/song_slots.h).
                //     By the time we notice a loss the row has already left ob[], so this could never have been
                //     answered from here -- which is why the rule that lived here asked "am I at the cap?" of a
                //     count that skipped the very songs sung to fill slots, and silenced real dispels for a whole
                //     Clarion Call recast. Now: it went, and it was the one the game had to drop. Nothing else.
                return party().song_was_evicted(e.target, e.spell, 6000u);
            };
            // GEO Indi- : you carry exactly ONE aura (`selfGeo_`, party_state.h -- a single slot, not a list), so casting
            // a DIFFERENT Indi- REPLACES the previous one. Its status leaving the buff list is that SWAP, not a loss :
            // you cannot "put it back" without dropping the one you deliberately chose, so a red OUT is permanent and
            // wrong. Reported : Indi-Fury -> Indi-Refresh -> Indi-Regen left Fury and Refresh stuck OUT ; only the Indi-
            // you carry NOW may alert. Same shape as songReplaced above.
            // Identifying a geomancy entry : the SPELL when we attributed the cast (skill 44 = Indicolure, tb_buff_gen),
            // else the GEO-ONLY statuses -- Boosts 542-556 and "Colure Active" 612, which no other spell grants. The
            // status alone can NOT decide for the shared ones (539 Regen / 541 Refresh / 580 Haste), hence the spell
            // first : a real Refresh must keep its normal OUT.
            auto geoEntry = [&](const FocusMem& e) -> bool {
                const SpellBuff* sb = e.spell ? spell_buff(e.spell) : 0;
                return (sb && sb->skill == 44) || (e.status >= 542 && e.status <= 556) || e.status == 612;
            };
            auto geoReplaced = [&](const FocusMem& e) -> bool {
                if (!geoEntry(e)) return false;
                if (e.self) { const PartyState::GeoAura& ga = party().self_geo();
                              return ga.status && ga.status != e.status; }   // the aura you carry now is a DIFFERENT Indi- -> this one was swapped out
                // ENTRUST'd Indi- on an ally : same rule, but the model keeps no per-ally aura slot -- so require the
                // EVIDENCE, a newer Indi- cast we actually saw land on that same ally (the 6 s window mirrors the song
                // rule : long enough for the 0x076 update the swap arrives in, short enough that a later dispel OUTs).
                for (int i = 0; i < no; ++i) {
                    if (ob[i].target != e.target || ob[i].spell == e.spell) continue;
                    const SpellBuff* sb2 = spell_buff(ob[i].spell);
                    if (sb2 && sb2->skill == 44 && (unsigned)(nowMs - ob[i].startMs) < 6000u) return true;
                }
                return false;
            };
            // Settled BEFORE the compaction below, because focusHas() counts an entry's siblings and the compaction
            // leaves stale copies behind it -- counting mid-pass would see the same sibling twice and call a live
            // song lost. The verdict travels with its entry through the pass, and the emit reads the same one.
            static bool fmHas[24];
            for (int q = 0; q < fmN; ++q) fmHas[q] = focusHas(fm[q]);
            int w = 0;                                                                        // prune : ally left the party/alliance, or the focus flag was turned off
            for (int q = 0; q < fmN; ++q) {
                // <= 5, NOT <= 17. The 0x076 that feeds listReady/focusHas carries YOUR PARTY ONLY, so a monitor
                // entry on an alliance member can never be decided: listReady stays false forever, the emit below
                // resets lostMs every frame, and the only prune path here needs lostMs != 0 -- the entry became
                // IMMORTAL. It also never drew anything, so it was pure dead weight that filled the 24 slots and
                // then silently blocked new entries, including your own. Alliance targets are dropped here, and
                // refused at creation below.
                const bool live = fm[q].self ? true : (zoneGrace || party().party_order(fm[q].target) <= 5);   // 0..5 party ; 6..17 alliance and 99 = gone (roster is unstable mid-zone -> keep during grace)
                const bool fkOn = C.tm_buff_off(UiConfig::TM_KEY_FOCUS | fm[q].status);   // self & ally share ONE global focus state
                if (focus_trace_live() && !(live && fkOn))
                    windower::debug::log("FOCUSPRUNE st=%u '%s' DROPPED (live=%d focusOn=%d) -> no OUT row possible",
                                         (unsigned)fm[q].status, buff_status_name(fm[q].status), live ? 1 : 0, fkOn ? 1 : 0);
                if (!(live && fkOn)) continue;                                                 // gone / focus off -> drop
                // SONG on an ally who is no longer in YOUR zone (they stayed behind, or YOU zoned away) : their 0x076
                // stops refreshing, so the buff set FREEZES -- the row would either linger on a drifting estimate or
                // fire a wrong OUT off the stale list. User rule : CLEAN ally song rows the moment the target is
                // out-of-zone. Songs only (song_family, spell-keyed) -- ally RDM/enh buffs behave and are left alone.
                // Gated past the zone grace so the roster's per-member zone id has settled first (no false clean).
                if (!fm[q].self && !zoneGrace && song_family(fm[q].spell) > 0 && party().member_offzone(fm[q].target)) {
                    if (focus_trace_live())
                        windower::debug::log("SONGOFFZONE st=%u '%s' target=%08X out-of-zone -> CLEAN (no OUT, no stale row)",
                                             (unsigned)fm[q].status, buff_status_name(fm[q].status), fm[q].target);
                    continue;
                }
                if (fm[q].zoneCheck) {                                                         // pending post-zone check : decide ONLY after the grace ends AND the list is back.
                    if (zoneGrace || !listReady(fm[q])) { /* still settling : keep, no decision, no alert */ }
                    else if (fmHas[q]) fm[q].zoneCheck = 0;                                    //   grace over + list stable + present -> survived the zone, track normally
                    else continue;                                                            //   grace over + list stable + ABSENT -> the game dropped it on zoning -> depop, NO alert
                }                                                                             //   (deciding DURING the grace read the stale pre-zone buff list -> false survivors -> OUT)
                // a "Hidden+focus" alert that has held its full tmFocusHold with the buff still gone -> FREE the slot
                // (the emit stops drawing it at that point ; without this it lingers forever and can fill fm[24]).
                if (!fm[q].zoneCheck && fm[q].lostMs && !fmHas[q]) {
                    const bool dkOn = C.tm_buff_off((unsigned)fm[q].status);   // self & ally share ONE global hidden state
                    if (songUnrecoverable(fm[q])) continue;   // un-refillable 5th Clarion-Call song -> free the slot (no OUT will ever draw ; without this the un-drawn entry lingers and fills fm[24])
                    if (songReplaced(fm[q])) continue;        // deliberately swapped out by a new song on the same ally (Pianissimo) -> free the slot, never an OUT
                    if (geoReplaced(fm[q])) continue;         // a previous Indi- you replaced by casting another one -> free the slot, never an OUT
                    if (dkOn && (unsigned)(nowMs - fm[q].lostMs) > (unsigned)C.tmFocusHold * 1000u) continue;
                }
                if (w != q) { fm[w] = fm[q]; fmHas[w] = fmHas[q]; } ++w;
            }
            fmN = w;
            // //aio oblog stage 4 : the FOCUS monitor. It decides "OUT" from a SEPARATE input (0x076 presence of the
            // STATUS on the target) than the rows above, so a song can draw a healthy timer AND a red OUT at once --
            // and the group/row stages show nothing about why. Songs share statuses across spells (both Marches are
            // 214), so `has` answering for the family and `spell` naming one member of it is exactly where an "it IS
            // up, you re-cast it in AoE" symptom lives. Dumped for every monitored entry, kept or not.
            int alertQ[24]; int nAlert = 0;   // entries that survived every suppression gate -- drawn after the loop
            OBLOG("=== OBLOG : %d focus monitor entr(ies) ===", fmN);
            // The bard song-slot state, in one line. It is here rather than behind its own command because the
            // thing it answers -- "did the base stay honest?" -- can only be seen after a Clarion Call, and CC is
            // an HOUR of recast: nobody is going to sit through a dedicated test for it. Printed in every capture,
            // the answer instead falls out of an ordinary fight. slot=1 means the extra slot is treated as possibly
            // occupied, so nothing is being learned; base is what //aio out's song suppression measures against.
            OBLOG("  SONGSLOT  cap=%d valid=%d (learned from an eviction)  ccUp=%d ccRecast=%d ccUsable=%d  -- counts are PER PERSON, see the FOCUS lines",
                  songCap.cap, songCap.valid ? 1 : 0, ccUp ? 1 : 0, ccOnRecast ? 1 : 0, ccUsable ? 1 : 0);
            for (int q = 0; q < fmN && nb < 50; ++q) {                                         // emit a RED row for each MISSING focus buff (self or ally)
                // Honour "My buffs on allies" here too. Turning it off stops ob[] being built, so no NEW ally entry
                // is created -- but the ones already in fm[] kept emitting, leaving a red blinking "Name - Haste OUT"
                // at the top of the box for a category the user had just switched off, until they re-cast it. Worse,
                // with ob[] empty the three deliberate-swap suppressors below (song replaced / unrecoverable / geo
                // replaced) iterate over nothing, so a Pianissimo swap would raise an OUT that is normally silenced.
                if (!fm[q].self && !C.tmMine) { fm[q].lostMs = 0; continue; }
                // The same verdict the prune used -- this used to be a third, presence-only copy of the rule, which
                // is how one of two same-status songs could be dropped by one stage and reported up by the other.
                // meHas() fails open on a FAILED read; that job now belongs to listReady() below, which gates the alert.
                const bool has = fm[q].self ? (!f.game || !f.game->buffsOk || fmHas[q]) : fmHas[q];
                if (g_obLog) {
                    const char* fen = fm[q].isAbil ? abil_name_by_id(fm[q].spell) : (spell_info(fm[q].spell) ? spell_info(fm[q].spell)->en : 0);
                    OBLOG("  FOCUS q=%-2d %-16s tgt=%08X st=%-4u spell=%-5u \"%s\" self=%d  has(0x076)=%d listReady=%d lostAgo=%dms  replaced=%d unrecov=%d geoRepl=%d (aura st=%u)",
                          q, fm[q].self ? "<you>" : fm[q].name, fm[q].target, fm[q].status, fm[q].spell, fen ? fen : "?",
                          fm[q].self, has ? 1 : 0, listReady(fm[q]) ? 1 : 0,
                          fm[q].lostMs ? (int)(nowMs - fm[q].lostMs) : -1,
                          songReplaced(fm[q]) ? 1 : 0, songUnrecoverable(fm[q]) ? 1 : 0,
                          geoReplaced(fm[q]) ? 1 : 0, (unsigned)party().self_geo().status);
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
                // Muted by hand (//aio out) : never alert, and never dropped from HERE. It used to be dropped as
                // soon as `has` went false -- but `has` reads the 0x076 presence on the target, while the entry
                // is FED by ob[] (your own cast estimate). The two disagree constantly: a buff missing from a
                // 0x076 that has not arrived yet made the muted entry die and the seeding loop re-create it a
                // frame later, brand new and UN-muted, with a new number. That is why the row kept changing
                // number instead of going quiet. The drop now happens where the feeding does -- see `seen`.
                if (fm[q].muted) { fm[q].lostMs = 0; continue; }
                if (has) { fm[q].lostMs = 0; continue; }                                       // still up -> the normal row covers it, reset the loss timer
                if (zoneGrace) { fm[q].lostMs = 0; continue; }                                 // just zoned : buff lists still arriving -> don't false-alert (persist across the zone)
                if (!listReady(fm[q])) { fm[q].lostMs = 0; continue; }                         // NO DATA for this target (alliance member, or a party member out of zone : buffs_for()==0) -> "unknown", NOT "gone". The self path already fails open via meHas ; the ally path used to fire a permanent false red "OUT" here.
                if (fm[q].lostMs == 0) fm[q].lostMs = nowMs;                                   // just went missing -> stamp it
                // Unfollow-Focus = hidden + focus -> the alert holds tmFocusHold seconds then depops (Focus alone =
                // permanent until re-cast). Per-SPELL hide key : keyed on the shared STATUS this never matched for a
                // buff two spells can grant, so the "hold 15s then depop" branch was unreachable for Cocoon.
                const bool dkOn = C.tm_buff_off((unsigned)fm[q].status);   // self & ally share ONE global hidden state
                if (focus_trace_live())
                    windower::debug::log("FOCUSHOLD st=%u '%s' self=%d has=0 lostAgo=%ums hold=%ds dkOn=%d -> %s",
                                         (unsigned)fm[q].status, buff_status_name(fm[q].status), fm[q].self,
                                         (unsigned)(nowMs - fm[q].lostMs), C.tmFocusHold, dkOn ? 1 : 0,
                                         (dkOn && (unsigned)(nowMs - fm[q].lostMs) > (unsigned)C.tmFocusHold * 1000u) ? "DROP (hold expired)" : "DRAW red OUT row");
                if (dkOn && (unsigned)(nowMs - fm[q].lostMs) > (unsigned)C.tmFocusHold * 1000u) continue;
                if (songUnrecoverable(fm[q])) {   // a lost 5th Clarion-Call song can't be refilled -> suppress the OUT entirely (never even a one-frame flash before the prune frees it)
                    if (focus_trace_live())
                        windower::debug::log("SONGOUT st=%u '%s' SUPPRESSED : that person now holds %d, the cap is %d (ccUsable=%d) -> no OUT (the fifth slot is gone)",
                                             (unsigned)fm[q].status, buff_status_name(fm[q].status),
                                             party().song_slot_count(fm[q].target), songCap.cap, ccUsable ? 1 : 0);
                    continue;
                }
                if (songReplaced(fm[q])) {   // deliberately swapped out by a new song on the same ally (Pianissimo Ballad) -> no OUT, not even a one-frame flash (prune frees the slot next frame)
                    if (focus_trace_live())
                        windower::debug::log("SONGOUT st=%u '%s' target=%08X SUPPRESSED -> no OUT (song deliberately replaced on this ally)",
                                             (unsigned)fm[q].status, buff_status_name(fm[q].status), fm[q].target);
                    continue;
                }
                if (geoReplaced(fm[q])) {   // an Indi- you replaced with another Indi- -> no OUT, not even a one-frame flash (the prune frees the slot next frame)
                    if (focus_trace_live())
                        windower::debug::log("GEOOUT st=%u '%s' spell=%u self=%d SUPPRESSED (carrying st=%u now) -> no OUT (Indi- deliberately replaced)",
                                             (unsigned)fm[q].status, buff_status_name(fm[q].status), (unsigned)fm[q].spell,
                                             fm[q].self, (unsigned)party().self_geo().status);
                    continue;
                }
                if (nAlert < 24) alertQ[nAlert++] = q;   // decided : drawn below, once the whole picture is known
                fm[q].alerting = 1;
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
            for (int a = 0; a < nAlert && nb < 50; ++a) {
                const int q = alertQ[a];
                if (fm[q].spell) {   // could this be part of a group? (a spell-less entry -- food, gear -- never is)
                    bool drawn = false;
                    for (int b = 0; b < a; ++b) if (fm[alertQ[b]].spell == fm[q].spell) { drawn = true; break; }
                    if (drawn) continue;                                      // an earlier row already speaks for this loss
                }
                int same = 0;
                for (int b = 0; b < nAlert; ++b) if (fm[q].spell && fm[alertQ[b]].spell == fm[q].spell) ++same;
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
                bufs[nb].name = obLabel[nb]; bufs[nb].nameCol = 0xFFFF3B3Bu; bufs[nb].rem = TM_REM_MISSING; bufs[nb].icon = fm[q].status; bufs[nb].order = 0; bufs[nb].src = 6; bufs[nb].mark = (same >= 2) ? 0 : fm[q].tag; ++nb;   // ALL "OUT" alerts (self + ally) sort to order 0 : rem=MISSING pulls them to the very top so a small tmMax can't clip a critical alert
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
    if (nb == 0 && nr == 0 && !editing) return;
    // GROUP by `order` first, then soonest-first, then a DETERMINISTIC tiebreak (icon, name) so rows with equal
    // remaining are stable. Tiers, top->bottom : OUT alerts + YOUR buffs (0-1) ; buffs YOU cast on allies, grouped by
    // ally (10-28) ; buffs a PLAYER put on you, grouped by that player (40-57) ; TRUSTS last (90+).
    // The DISPLAYED second still decides first -- the sort must never contradict what the row reads, and the row
    // sources do not all round the same way (a self buff ceils its tick, an ally estimate floors its ms), so a
    // tick-first sort could put a visible 4:05 above a visible 4:04. `fine` only breaks the EQUAL-second ties,
    // and that is exactly where the yoyo lived : two timers a fraction of a second apart read the same number
    // every other second, and the icon/name tie-break then ordered them the opposite way from the second
    // before, so the two rows swapped places once a second forever. Ranking a tie by the exact tick pins them.
    auto fineOf = [](const Row& r) -> int {
        if (r.fine != TM_FINE_NONE) return r.fine;   // exact remaining, in ticks (server expiry / raw recast counter)
        if (r.rem > 30000000 || r.rem < -30000000) return r.rem;   // no sub-second source and out of multiply range (OUT sentinel, absurd timer)
        return r.rem * 60;                           // frozen demo rows : nothing to refine, they never tick
    };
    // `mode` picks the PRIMARY key ; everything after it is unchanged, and that matters -- rem before fine is
    // what stops the yoyo described above, so a new key may only ever go in FRONT of rem, never between them.
    //   Duration 0 : band (person) then soonest      1 : soonest, everyone mixed -- but TRUSTS STILL LAST
    //   Recast   0 : soonest                         1 : by name
    // Trusts stay last in both duration modes on purpose: they are the rows you are least likely to act on,
    // and tmMax cuts the tail, so mixing them in would let a trust's Protect push out one of your own timers.
    auto after = [&fineOf](const Row& x, const Row& y, int mode, bool recast) -> bool {   // does x sort AFTER y ?
        if (recast) {
            if (mode == 1) { const int c = strcmp(x.name ? x.name : "", y.name ? y.name : ""); if (c) return c > 0; }
        } else if (mode == 1) {
            const int tx = (x.order >= 90) ? 1 : 0, ty = (y.order >= 90) ? 1 : 0;   // 90+ = a trust's buff on you
            if (tx != ty) return tx > ty;
        } else if (x.order != y.order) return x.order > y.order;
        if (x.rem != y.rem) return x.rem > y.rem;
        // Refine by the sub-second ONLY between rows read from the same clock. Across clocks the difference
        // is drift, not order, and following it makes two rows on the same timer trade places for as long as
        // they both live. Rows that disagree fall straight through to the stable tiebreaks below.
        if (x.fineClk == y.fineClk && x.fineClk != FCLK_NONE) {
            const int fx = fineOf(x), fy = fineOf(y);
            if (fx != fy) return fx > fy;
        }
        if (x.icon != y.icon) return x.icon > y.icon;
        // The PERSON is part of the deterministic tiebreak, not just the spell : two allies carrying the same
        // buff at the same second used to differ by their "Aeryn - Haste" / "Gab - Haste" string, and since the
        // split they share one name. Tying here would leave their order to the BUILD order, which moves whenever
        // the model list is compacted -- the yoyo, in its other clothes. (The sort itself is insertion, hence
        // stable ; this only removes the last way two rows can compare equal.)
        { const int cw = strcmp(x.who ? x.who : "", y.who ? y.who : ""); if (cw) return cw > 0; }
        return strcmp(x.name ? x.name : "", y.name ? y.name : "") > 0;
    };
    g_lastRowN = nb;   // the harness reads this : hitting the 50 cap means rows are being dropped in silence
    { const int md = C.tmSortDur;
      for (int a = 1; a < nb; ++a) { Row t = bufs[a]; int b = a - 1; while (b >= 0 && after(bufs[b], t, md, false)) { bufs[b + 1] = bufs[b]; --b; } bufs[b + 1] = t; } }
    { const int md = C.tmSortRec;
      for (int a = 1; a < nr; ++a) { Row t = recs[a]; int b = a - 1; while (b >= 0 && after(recs[b], t, md, true)) { recs[b + 1] = recs[b]; --b; } recs[b + 1] = t; } }
    // THE HINT LIVES ON THE ROW THAT NEEDS IT. A red OUT for a buff you never meant to keep is a mistake, and
    // the correction has to be reachable without remembering anything -- so the offending row carries the words
    // that fix it, and they vanish with it. Only the FIRST one: repeated down a column of alerts a hint becomes
    // noise. Placed after the sort (OUT rows carry rem = TM_REM_MISSING, so they are already on top) and before
    // the tmMax clamp, so the row it lands on is always one that is actually drawn.
    if (!preview && !editing) { for (int i = 0; i < nb; ++i) if (bufs[i].src == 6 && !bufs[i].post) { bufs[i].post = "  //aio out"; bufs[i].postCol = 0xFF7E8894u; break; } }
    if (nb > C.tmMax) nb = C.tmMax; if (nr > C.tmMax) nr = C.tmMax;

#ifdef AIOHUD_PROBES
    // //aio songdump layer 4 -- the rows as BUILT, each labelled with the pass that emitted it.
    // Recorded into a RAM ring, never to disk : an earlier version wrote a line per change and the extra frame time
    // was enough to make the ghost-song bug stop reproducing. Observation must be free, so the file write is deferred
    // to //aio songdump. Only a CHANGE in the row set (status / name / pass / tag -- not the ticking countdown) is kept.
    if (!preview) {
        unsigned sig = 2166136261u;   // FNV-1a over what identifies the rows, deliberately excluding `rem`
        for (int i = 0; i < nb; ++i) {
            sig = (sig ^ (unsigned)bufs[i].icon) * 16777619u;
            sig = (sig ^ (unsigned)bufs[i].src)  * 16777619u;
            for (const char* c = bufs[i].who;  c && *c; ++c) sig = (sig ^ (unsigned char)*c) * 16777619u;   // the PERSON is part of a row's identity : without it two allies carrying the same buff hash the same
            for (const char* c = bufs[i].name; c && *c; ++c) sig = (sig ^ (unsigned char)*c) * 16777619u;
            for (const char* c = bufs[i].tag;  c && *c; ++c) sig = (sig ^ (unsigned char)*c) * 16777619u;
        }
        static unsigned lastSig = 0;
        if (sig != lastSig) {
            lastSig = sig;
            static const char* const SRC[7] = { "?", "?", "SELF-timer", "GEO-aura", "ALLY-AoE-group", "ALLY-single", "FOCUS-missing" };
            sr_push("---- Timers built %d row(s) ----", nb);
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
    }
#endif

    float sscl = C.tmScale; if (sscl < 0.5f) sscl = 0.5f; if (sscl > 2.0f) sscl = 2.0f;
    const float S = (ovS > 0.0f) ? ovS : (screenH / 1000.0f) * sscl;
    const float pad = (ui_config().tmBox.on ? 8.0f : 0.0f) * S, gap = 4.0f * S, midGap = 30.0f * S, icgap = 4.0f * S;   // pad 0 when no box chrome ; midGap : space between the Duration & Recast columns (fused)
    const u32 white = 0xFFEAF0FFu, strk = 0xFF000000u, orange = 0xFFEB9660u, red = 0xFFF06060u, dim = 0xFFB4B9C8u, green = 0xFF74D074u;
    Font* fN = tm_font(f, TM_NAME); Font* fT = tm_font(f, TM_TIMER); Font* fH = tm_font(f, TM_HEADER);
    const float zN = tm_sz(TM_NAME, 13.0f) * S, zT = tm_sz(TM_TIMER, 13.0f) * S, zH = tm_sz(TM_HEADER, 13.0f) * S;
    const float oN = tm_ow(TM_NAME, 1.0f) * S, oT = tm_ow(TM_TIMER, 1.0f) * S, oH = tm_ow(TM_HEADER, 1.0f) * S;
    float iscl = C.tmIconScale; if (iscl < 0.5f) iscl = 0.5f; if (iscl > 2.0f) iscl = 2.0f;
    const float icon = 20.0f * iscl * S, rowH = icon + 3.0f * S, headH = zH + 5.0f * S;
    float tmrg = C.tmRowGap; if (tmrg < 0.6f) tmrg = 0.6f; if (tmrg > 3.0f) tmrg = 3.0f;
    const float rowPit = rowH * tmrg;   // per-row PITCH (config: row spacing) ; content stays centred in rowH, extra gap below
    const bool showHdr = (C.tmTitle != 0);
    const float bau = (float)BUFF_CELL / (float)BUFF_ATLAS_W, bav = (float)BUFF_CELL / (float)BUFF_ATLAS_H;
    const int bcells = BUFF_COLS * (BUFF_ATLAS_H / BUFF_CELL);
    const bool flash = ((GetTickCount() / 250u) & 1u) != 0;   // ~2 Hz blink for the <=10s alarm
    const bool flashStrong = ((GetTickCount() / 140u) & 1u) != 0;   // ~3.5 Hz HARD blink (Soul Voice last-minute window -> Nitro + re-sing)
    char tb[16];
    auto fmt = [&](int r) -> const char* { if (r == TM_REM_MISSING) { strcpy(tb, "OUT"); return tb; } if (r >= 3600) sprintf(tb, "%d:%02d:%02d", r / 3600, (r % 3600) / 60, r % 60); else sprintf(tb, "%d:%02d", r / 60, r % 60); return tb; };
    // a row name may carry a COR roll pip drawn "Name [5] (AoE 6)" with ONLY the [5] tinted. These keep the width
    // measurement and the draw in exact sync (name -> " [" -> pip(colour) -> "]" -> post).
    char pbuf[8];
    // The PERSON is outside the display mode : "Icon" is icon + who, "Name" is who + spell, "Both" is all three.
    // A row that names someone always names them -- an ally row reduced to an anonymous icon would not say whose
    // Haste is running out, which is the only thing that row is for. `wantSpell` carries the mode's half.
    auto rowNameW = [&](const Row& R, bool wantSpell) -> float {
        float w = 0.0f;
        if (R.who) { w += fN->measure(R.who, zN); if (wantSpell && R.name) w += fN->measure(" - ", zN); }
        if (!wantSpell || !R.name) return w;
        w += fN->measure(R.name, zN);
        if (R.pip > 0) { sprintf(pbuf, "%d", R.pip); w += fN->measure(" [", zN) + fN->measure(pbuf, zN) + fN->measure("]", zN); }
        if (R.tag) w += fN->measure(R.tag, zN);
        if (R.post) w += fN->measure(R.post, zN);
        return w;
    };
    auto drawRowName = [&](const Row& R, float nx, float cy, u32 baseCol, bool wantSpell) {   // who -> name -> [pip] -> (tag) -> post, each its own colour
        float xx = nx;
        if (R.who) {   // the person, always
            fN->draw_lc(dev, xx, cy, R.who, zN, baseCol, strk, oN); xx += fN->measure(R.who, zN);
            if (wantSpell && R.name) { fN->draw_lc(dev, xx, cy, " - ", zN, baseCol, strk, oN); xx += fN->measure(" - ", zN); }
        }
        if (!wantSpell || !R.name) return;   // Icon mode : the person carried the row, the spell is the icon
        fN->draw_lc(dev, xx, cy, R.name, zN, baseCol, strk, oN); xx += fN->measure(R.name, zN);
        if (R.pip > 0) { char pb[8]; sprintf(pb, "%d", R.pip);
            fN->draw_lc(dev, xx, cy, " [", zN, baseCol, strk, oN); xx += fN->measure(" [", zN);
            fN->draw_lc(dev, xx, cy, pb, zN, R.pipCol, strk, oN); xx += fN->measure(pb, zN);
            fN->draw_lc(dev, xx, cy, "]", zN, baseCol, strk, oN); xx += fN->measure("]", zN); }
        if (R.tag) { fN->draw_lc(dev, xx, cy, R.tag, zN, R.tagCol, strk, oN); xx += fN->measure(R.tag, zN); }
        if (R.post) fN->draw_lc(dev, xx, cy, R.post, zN, R.postCol ? R.postCol : baseCol, strk, oN);   // postCol 0 = follow the name
    };

    float measH = 0.0f;   // emit() stashes its boxH here so the top-level measureOnly (Help scale-to-fit) can read it
    struct Col { const char* title; Row* list; int n; int mode; u32 tex; float au, av; int cells; bool recast; };
    // draw ONE box holding `nc` columns at fractional (fx,fy) ; when ovS>0 (config preview) it centres on (ovcx,ovcy).
    // measureOnly returns the box width WITHOUT drawing (so the preview can lay two separate boxes side by side). Returns boxW.
    auto emit = [&](Col* cols, int nc, float fx, float fy, int editId, float* saveFx, float* saveFy, float ovcx, float ovcy, bool measureOnly) -> float {
        // The monitor number sits in a GUTTER at the far LEFT of the column, before the icon -- not between the
        // icon and the name, where it would read as part of the buff. The gutter is one width for the whole
        // column (the widest number in it), so every icon in the column still lines up; a per-row width would
        // ragged them by a digit. Zero when nothing in the column is monitored, so a column that has no numbers
        // loses no space to them.
        float colW[2] = { 0.0f, 0.0f }, markW[2] = { 0.0f, 0.0f }; int rowsMax = 0;
        char mkb[8];
        for (int c = 0; c < nc; ++c) {
            const Col& CC = cols[c]; if (CC.n > rowsMax) rowsMax = CC.n;
            const bool wantIcon = (CC.mode == TMDISP_ICON || CC.mode == TMDISP_BOTH);
            const bool wantName = (CC.mode == TMDISP_NAME || CC.mode == TMDISP_BOTH);
            const bool colIcon = wantIcon;   // the mode alone decides the icon column now : no row forces one any more
            float timeW = 0.0f, nameW = 0.0f;
            for (int i = 0; i < CC.n; ++i) { const float w = fT->measure(fmt(CC.list[i].rem), zT); if (w > timeW) timeW = w;
                const float nw = rowNameW(CC.list[i], wantName); if (nw > nameW) nameW = nw; }   // a row with a person is measured in every mode -- it prints one
            for (int i = 0; i < CC.n; ++i) if (CC.list[i].mark > 0) {
                sprintf(mkb, "%d", CC.list[i].mark);
                const float w2 = fN->measure(mkb, zN); if (w2 > markW[c]) markW[c] = w2; }
            if (markW[c] > 0.0f) markW[c] += icgap;
            float leftW = markW[c] + (colIcon ? icon : 0.0f);
            if (nameW > 0.0f) leftW += (colIcon ? icgap : 0.0f) + nameW;
            if (leftW <= 0.0f) leftW = icon;
            float w = leftW + gap + timeW;
            if (showHdr) { const float tW = fH->measure(CC.title, zH); if (tW > w) w = tW; }
            if (w < 46.0f * S) w = 46.0f * S;
            colW[c] = w;
        }
        float boxW = pad * 2.0f;
        for (int c = 0; c < nc; ++c) { boxW += colW[c]; if (c) boxW += midGap; }
        const int bodyRows = rowsMax > 0 ? rowsMax : 1;
        const float boxH = pad + (showHdr ? headH + gap : 0.0f) + bodyRows * rowPit + pad;
        if (measureOnly) { measH = boxH; return boxW; }

        float px, py;
        if (ovS > 0.0f) { px = snap(ovcx - boxW * 0.5f); py = snap(ovcy - boxH * 0.5f); }
        else            { px = snap(fx * screenW); py = snap(fy * screenH); }
        if (editing && saveFx) {
            static EditBox eb[2]; EditBox& g = eb[editId == EDITBOX_TIMERS ? 0 : 1];
            float tfx = px / screenW, tfy = py / screenH; bool ps = true; int ch = 0, cv = 0; const bool wasDrag = g.dragging;
            if (edit_box_drag(g, editId, f, px, py, boxW, boxH, ZPERM_HUB, ps, tfx, tfy, ch, cv, ui_config().tmScale)) edit_box_grid(dev, f, g, px, py, boxW, boxH, ch != 0, cv != 0);
            *saveFx = px / screenW; *saveFy = py / screenH; if (wasDrag && !g.dragging) save_ui_config();
        }

        dColorQuadState(dev);
        draw_themed_box(dev, f.skin, px, py, boxW, boxH, C.tmBox, 1.0f, S);   // shared themed chrome (frame/transp/theme)
        float cx = px + pad;
        for (int c = 0; c < nc; ++c) {
            const Col& CC = cols[c];
            const bool wantIcon = (CC.mode == TMDISP_ICON || CC.mode == TMDISP_BOTH);
            const bool wantName = (CC.mode == TMDISP_NAME || CC.mode == TMDISP_BOTH);
            float cyTop = py + pad;
            if (showHdr) { fH->begin(dev); fH->draw_c(dev, cx + colW[c] * 0.5f, cyTop + headH * 0.5f, CC.title, zH, tm_col(TM_HEADER, orange), strk, oH); cyTop += headH + gap; }
            float cyy = cyTop;
            for (int i = 0; i < CC.n; ++i) {
                const int r = CC.list[i].rem, ic = CC.list[i].icon; const char* nm = CC.list[i].name;
                // A "+ focus" buff (Tracked OR Hidden) that drops UNDER the focus-warn threshold blinks (name + timer) as an
                // early "recast soon" cue -- the warn threshold used to affect Hidden+focus only (it made the row APPEAR) ;
                // a Tracked+focus row was already visible, so it never warned before its normal <10s red flash.
                const bool focusWarn = !CC.recast && ic > 0 && r > 0 && r < C.tmFocusWarn
                                       && C.tm_buff_off(UiConfig::TM_KEY_FOCUS | (unsigned)ic);
                const bool rWantIcon = wantIcon;
                const bool haveIcon = (CC.tex && ic >= 0 && ic < CC.cells);
                bool drewIcon = false;
                const float gx0 = cx + markW[c];   // everything after the number gutter
                if (CC.list[i].mark > 0) {         // the handle //aio out takes -- dim, it is not part of the buff
                    char mb[8]; sprintf(mb, "%d", CC.list[i].mark);
                    fN->begin(dev); fN->draw_lc(dev, cx, cyy + rowH * 0.5f, mb, zN, 0xFF6E7885u, strk, oN);
                }
                if (rWantIcon && haveIcon) { const float u0 = (float)(ic % BUFF_COLS) * CC.au, v0 = (float)(ic / BUFF_COLS) * CC.av; draw_icon_cell(dev, CC.tex, gx0, cyy + (rowH - icon) * 0.5f, icon, icon, u0, u0 + CC.au, v0, v0 + CC.av); drewIcon = true; }
                // The spell name : asked for by the mode, OR as a fallback when the icon we wanted has no art (an
                // icon-only row with no icon would be blank). The person is drawn either way, so a row that has one
                // still prints in Icon mode.
                const bool rWantSpell = wantName || (rWantIcon && !haveIcon);
                if ((nm && rWantSpell) || CC.list[i].who) {
                    const float nx = gx0 + (drewIcon ? icon + icgap : 0.0f);
                    u32 baseNameCol = CC.list[i].nameCol ? CC.list[i].nameCol : tm_col(TM_NAME, dim);
                    if (CC.list[i].rem == TM_REM_MISSING) baseNameCol = flashStrong ? 0xFFFF6A6Au : 0xFFFF2020u;   // FOCUS alert : the whole "Ally - Buff" row blinks red
                    else if (C.tmSpAlert && CC.list[i].icon > 0 && CC.list[i].rem > 0 && CC.list[i].rem < 60 && is_sp_buff_status(CC.list[i].icon)) baseNameCol = flashStrong ? 0xFFFFF000u : 0xFFFF1010u;   // SP last-minute : the whole row blinks hard
                    else if (focusWarn) baseNameCol = flash ? 0xFFFF6A6Au : baseNameCol;   // +focus under the warn threshold : name blinks red
                    fN->begin(dev); drawRowName(CC.list[i], nx, cyy + rowH * 0.5f, baseNameCol, rWantSpell);   // person + name + optional coloured roll pip / song tag
                }
                fmt(r);
                u32 tc;
                if (r == TM_REM_MISSING) { tc = flashStrong ? 0xFFFF6A6Au : 0xFFFF2020u; }   // FOCUS alert : "OUT" blinks red
                else if (CC.recast) { tc = (r <= 10) ? green : (r <= 30) ? orange : red; }   // recast : INVERSE -- red just after use (long wait) -> orange -> green as it nears ready
                else if (C.tmSpAlert && CC.list[i].icon > 0 && r > 0 && r < 60 && is_sp_buff_status(CC.list[i].icon)) { tc = flashStrong ? 0xFFFFF000u : 0xFFFF1010u; }   // SP ability last minute : HARD blink bright-yellow<->red (Soul Voice -> Nitro window, etc.)
                else if (focusWarn) { tc = (r <= 10) ? (flash ? red : 0xFFFFC8C8u) : (flash ? red : orange); }   // +focus under the warn threshold : timer blinks orange<->red (harder <10s) in sync with the name
                else { tc = tm_col(TM_TIMER, white); if (r <= 10) tc = flash ? red : 0xFFFFC8C8u; else if (r <= 30) tc = orange; }   // duration : white -> orange (<30) -> flashing red (<10)
                const float tw = fT->measure(tb, zT);
                fT->begin(dev); fT->draw_lc(dev, cx + colW[c] - tw, cyy + rowH * 0.5f, tb, zT, tc, strk, oT);
                cyy += rowPit;
            }
            cx += colW[c] + midGap;
        }
        return boxW;
    };

    Col dur = { "Duration", bufs, nb, C.tmDurMode, buffAtlas, bau, bav, bcells, false };
    Col rec = { "Recast",   recs, nr, TMDISP_NAME, 0, 0.0f, 0.0f, 0, true };   // recasts : text-only ; colour INVERSE of duration
    if (ovS > 0.0f) {                 // config preview stage : reflect Fused vs Separate
        if (measureOnly) {            // Help scale-to-fit : report the total footprint, don't draw
            if (C.tmMerged) { Col cc[2] = { dur, rec }; const float w = emit(cc, 2, 0, 0, EDITBOX_TIMERS, 0, 0, 0, 0, true); if (outW) *outW = w; if (outH) *outH = measH; }
            else { const float wD = emit(&dur, 1, 0, 0, EDITBOX_TIMERS, 0, 0, 0, 0, true); const float hD = measH;
                   const float wR = emit(&rec, 1, 0, 0, EDITBOX_TIMERS_R, 0, 0, 0, 0, true); const float hR = measH;
                   const float g2 = 16.0f * S; if (outW) *outW = wD + g2 + wR; if (outH) *outH = (hD > hR ? hD : hR); }
            return;
        }
        if (C.tmMerged) { Col cc[2] = { dur, rec }; emit(cc, 2, 0, 0, EDITBOX_TIMERS, 0, 0, ovX, ovY, false); }
        else {                        // two boxes side by side, centred as a group in the stage
            const float wD = emit(&dur, 1, 0, 0, EDITBOX_TIMERS, 0, 0, 0, 0, true);
            const float wR = emit(&rec, 1, 0, 0, EDITBOX_TIMERS_R, 0, 0, 0, 0, true);
            const float g2 = 16.0f * S, tot = wD + g2 + wR;
            emit(&dur, 1, 0, 0, EDITBOX_TIMERS,   0, 0, ovX - tot * 0.5f + wD * 0.5f, ovY, false);
            emit(&rec, 1, 0, 0, EDITBOX_TIMERS_R, 0, 0, ovX + tot * 0.5f - wR * 0.5f, ovY, false);
        }
    } else if (C.tmMerged) {          // live merged : one box ; a column with nothing to show DEPOPS (empty -> gone)
        Col cc[2]; int ncc = 0;
        if (nb > 0) cc[ncc++] = dur;
        if (nr > 0) cc[ncc++] = rec;
        if (ncc > 0) emit(cc, ncc, C.tmX, C.tmY, EDITBOX_TIMERS, &ui_config().tmX, &ui_config().tmY, 0, 0, false);
    } else {                          // live separate : each box depops on its own when its column is empty
        if (nb > 0) emit(&dur, 1, C.tmX,  C.tmY,  EDITBOX_TIMERS,   &ui_config().tmX,  &ui_config().tmY,  0, 0, false);
        if (nr > 0) emit(&rec, 1, C.tmRX, C.tmRY, EDITBOX_TIMERS_R, &ui_config().tmRX, &ui_config().tmRY, 0, 0, false);
    }
}

// Live / edit path : the Hud draws the box(es) at their configured screen positions (lazy-loads its buff atlas).
// Lazy load of the shared status-icon atlas, with a BOUNDED RETRY. Both call sites (Timers and Debuffs) used to
// carry their own one-shot `if (!tried) { load; tried = true; }`, so ONE transient miss -- the updater replacing
// buff_atlas.raw at that instant, the device not ready yet -- permanently killed EVERY status icon in those boxes
// for the rest of the session. Reported right after an update as "Protect, Cocoon... no icons"; the atlas file was
// intact, only the texture was missing. Same defect class as the gear icons: a permanent give-up on a transient
// failure. ~12 tries, 300 ms apart, then stop (a genuinely absent asset must not retry forever).
// The retry itself now lives with the ONE owner (buff_atlas.cpp) : same 12-tries / 300 ms budget, same "say so
// when the budget dies" log, but one texture instead of eight. buffAtlas_ is a BORROWED handle refreshed here
// each frame -- the HUD must not Release it (buff_atlas_dispose() is the single Release site).
u32 Hud::ensure_buff_atlas(u32 dev) {
    buffAtlas_ = buff_atlas_tex(dev);
    return buffAtlas_;
}

void Hud::draw_timers(const Frame& f, bool preview, float ovX, float ovY, float ovS) {
    ensure_buff_atlas(f.dev);
    timers_draw(f, preview, ovX, ovY, ovS, (float)screenW_, (float)screenH_, buffAtlas_);
}

// The Help sample owns its own copy of the buff-icon atlas (lazy) so it can draw Duration icons without a Hud.
// The Help sample used to keep its OWN copy of the atlas, with its own forget/dispose pair wired into
// Hud::render's dev-change block. It now borrows the shared one (buff_atlas.cpp), which is forgotten and
// released once from those same two blocks -- so this file no longer owns a texture at all.
static u32 timers_help_atlas(u32 dev) { return buff_atlas_tex(dev); }

// Help sample : the REAL Timers box(es) in preview mode (config-aware), centred at (cx,cy) at scale `s`.
void timers_help_box(const Frame& f, float cx, float cy, float s) {
    timers_draw(f, true, cx, cy, s, 0.0f, 0.0f, timers_help_atlas(f.dev));
}

// Help scale-to-fit : measure at scale 1 (linear in S), pick the largest scale that fits availW (capped at maxScale).
// ---- THIS MODULE'S CHECKS (model/selftest.h) ----------------------------------------------------------------
// Each one has a source the module does not control -- a cap, a domain, or the clock -- so none of them can be
// satisfied by the code simply agreeing with itself. Ids are stable: they are what a bug report names.
static int timers_checks(CheckFail* out, int cap) {
    int n = 0;
    const unsigned nowMs = GetTickCount();
    #define FAIL(ID, SEV, ...) do { if (n < cap) { lstrcpynA(out[n].id, ID, sizeof(out[n].id)); out[n].sev = (SEV); \
        _snprintf(out[n].detail, sizeof(out[n].detail), __VA_ARGS__); out[n].detail[sizeof(out[n].detail)-1] = 0; ++n; } } while (0)

    // 1. The focus monitor is full. Structural: 24 is the array, and past it new entries are refused with no
    //    sign -- and since the SAME list drives your OUT alerts, they stop appearing for anything new.
    if (fmN >= 24)
        FAIL("TM.FOCUS_FULL", CHK_WARN, "%d of 24 focus slots used -- new monitored buffs are refused and their OUT alerts will not appear", fmN);

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
    { int no = 0; party().other_buffs(no);
      if (no >= 32) FAIL("TM.OB_FULL", CHK_WARN, "%d of 32 ally-buff slots used -- new buffs you cast are no longer tracked", no); }

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

void timers_help_fit(const Frame& f, float availW, float maxScale, float& outScale, float& outH) {
    float bw = 0.0f, bh = 0.0f;
    timers_draw(f, true, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, timers_help_atlas(f.dev), true, &bw, &bh);
    float s = (bw > 1.0f) ? (availW / bw) : maxScale;
    if (s > maxScale) s = maxScale; if (s < 0.6f) s = 0.6f;
    outScale = s; outH = bh * s;
}

} // namespace aio
