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

namespace aio {

// //aio timers reset (+ the config "Reset timers" button) : flush ALL live timer state -- self buff timers (0x063,
// the server re-sends them shortly), the buffs you cast on allies (estimates), and the FOCUS monitor's remembered
// buffs (a bumped generation makes timers_draw wipe its static fm[]). Clears any stuck "OUT" alert / stale row.
static unsigned g_tmResetGen = 0;
void timers_reset() { party().buff_timers_clear(); party().other_buffs_clear(); ++g_tmResetGen; }

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
    const char* first = 0; const char* only = 0; int nUsable = 0;
    for (int i = 0; i < ABILS_N; ++i) if (ABILS[i].recast_id == rid) {
        if (!first) first = ABILS[i].en;
        if (jaOk) { unsigned id = ABILS[i].id; if (id < 1024 && (jaBits[id >> 3] & (1u << (id & 7)))) { if (!only) only = ABILS[i].en; ++nUsable; } }
    }
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
static void tm_self_keys(unsigned status, unsigned& hideKey, unsigned& focusKeySpell) {
    hideKey = status; focusKeySpell = UiConfig::TM_KEY_FOCUS | status;
    const unsigned short sp = party().self_buff_spell(status);
    const SpellRow* sr = sp ? spell_info(sp) : 0;
    if (sr && sr->recast_id) {
        hideKey       = UiConfig::TM_KEY_RECAST + sr->recast_id;
        focusKeySpell = UiConfig::TM_KEY_FOCUS | (UiConfig::TM_KEY_RECAST + sr->recast_id);
    }
}
static bool tm_self_focus_on(const UiConfig& C, int job, unsigned status) {
    unsigned hk, fk; tm_self_keys(status, hk, fk);
    return C.tm_track_off(job, fk) || C.tm_track_off(job, UiConfig::TM_KEY_FOCUS | status);
}
static bool tm_self_hidden(const UiConfig& C, int job, unsigned status) {
    unsigned hk, fk; tm_self_keys(status, hk, fk);
    return C.tm_track_off(job, hk);
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
    struct Row { int rem; int icon; const char* name; int both; int order; u32 nameCol; int pip; u32 pipCol; const char* post; u32 postCol; const char* tag; u32 tagCol; int src; };   // tag : BRD song modifiers "(SV)(T)" drawn in tagCol, between the name and the AoE suffix
    static const int TM_REM_MISSING = -1000000000;   // FOCUS alert row : an ally is MISSING a critical buff -> timer shows "OUT" in red, sorts to the very top
    static Row bufs[50], recs[50]; int nb = 0, nr = 0;
    for (int i = 0; i < 50; ++i) { bufs[i].nameCol = recs[i].nameCol = 0; bufs[i].pip = recs[i].pip = 0; bufs[i].post = recs[i].post = 0; bufs[i].postCol = recs[i].postCol = 0; bufs[i].tag = recs[i].tag = 0; bufs[i].src = recs[i].src = 0; }   // clear per-frame overrides (static arrays)
    if (preview || editing) {
        static const struct { int id, rem; } SB[5] = { {43, 1490}, {57, 155}, {214, 309}, {40, 540}, {33, 28} };
        for (int i = 0; i < 5; ++i) { bufs[nb].rem = SB[i].rem; bufs[nb].icon = SB[i].id; bufs[nb].name = buff_status_name(SB[i].id); bufs[nb].both = 0; bufs[nb].order = 0; ++nb; }
        if (C.tmMine) {   // demo : an AoE song grouped for the whole party (you included) + a COR roll pip + a single-target buff on one ally
            bufs[nb].rem = 168;  bufs[nb].icon = 43;  bufs[nb].name = "Minuet V"; bufs[nb].tag = " (SV NT)"; bufs[nb].tagCol = 0xFFE8C55Au; bufs[nb].post = " (AoE 6)"; bufs[nb].both = 0; bufs[nb].order = 0; ++nb;
            bufs[nb].rem = 280;  bufs[nb].icon = 313; bufs[nb].name = "Chaos Roll"; bufs[nb].pip = 11; bufs[nb].pipCol = 0xFF74D074u; bufs[nb].tag = " (CC)"; bufs[nb].tagCol = 0xFFE8C55Au; bufs[nb].post = " (AoE 6)"; bufs[nb].both = 0; bufs[nb].order = 0; ++nb;   // pip 11 = green, under Crooked Cards
            // Band 1 = a real PLAYER's buff on you, band 2 = a TRUST's : both carry the owner in parentheses, which is
            // what the Help page has to teach. The per-ally sample below is band 5+ (it was left at 2, now "trusts").
            bufs[nb].rem = 540;  bufs[nb].icon = 33;  bufs[nb].name = "Haste"; bufs[nb].post = " (Aeryn)"; bufs[nb].postCol = 0xFF9AB0C8u; bufs[nb].both = 0; bufs[nb].order = 1; ++nb;
            bufs[nb].rem = 62;   bufs[nb].icon = 41;  bufs[nb].name = "Shell V"; bufs[nb].tag = " (SV)"; bufs[nb].tagCol = 0xFFE8C55Au; bufs[nb].post = " (Monberaux)"; bufs[nb].postCol = 0xFF9AB0C8u; bufs[nb].both = 0; bufs[nb].order = 2; ++nb;
            bufs[nb].rem = 1200; bufs[nb].icon = 40;  bufs[nb].name = "Aeryn - Haste";     bufs[nb].both = 1; bufs[nb].order = 5; ++nb;
        }
        static const struct { int icon, rem; const char* nm; } SR[4] = { {66, 8, "Mighty Strikes"}, {143, 22, "Haste"}, {160, 3, "Provoke"}, {56, 45, "Berserk"} };
        for (int i = 0; i < 4; ++i) { recs[nr].rem = SR[i].rem; recs[nr].icon = SR[i].icon; recs[nr].name = SR[i].nm; recs[nr].both = 0; recs[nr].order = 0; ++nr; }
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
            int c = meHas(st) ? 1 : 0;
            const unsigned me = party().self_id();
            for (int i = 0; i < party().count; ++i) {
                if (party().m[i].id == 0 || party().m[i].id == me) continue;
                const BuffSet* bs = party().buffs_for(party().m[i].id);
                if (bs) for (int j = 0; j < bs->n; ++j) if (bs->ids[j] == st) { ++c; break; }
            }
            return c;
        };

        // ---- pass 1 : group the buffs YOU cast on ALLIES by spell -> AoE groups (songs / rolls hit the whole party) ----
        struct AoeGrp { unsigned short spell, status; int allies, rem; unsigned expTick; unsigned char isAbil, selfHas, aoe; };   // expTick : the exact self-expiry the ally copies were frozen on -> identifies WHICH cast they came from
        static AoeGrp grp[32]; int ng = 0;
        int no = 0; const PartyState::OtherBuff* ob = 0;
        auto obRem = [&](const PartyState::OtherBuff& o) -> int {
            if (o.mirrorSelf) { int r = party().self_buff_remaining(o.status); return (r >= 0) ? r : (int)((o.startMs + o.durMs) - nowMs) / 1000; }
            if (o.expTick) return (int)(o.expTick - now) / 60;   // frozen on the exact self expiry (same FFXI clock as self rows)
            return (int)((o.startMs + o.durMs) - nowMs) / 1000;  // estimate
        };
        if (C.tmMine) {
            party().prune_other_buffs_worn();   // drop any that wore off early (dispel/overwrite/death) per the member's 0x076 icons
            ob = party().other_buffs(no);
            for (int i = 0; i < no; ++i) {
                if (obRem(ob[i]) <= 0) continue;
                int gi = -1; for (int k = 0; k < ng; ++k) if (grp[k].spell == ob[i].spell) { gi = k; break; }
                if (gi < 0 && ng < 32) { gi = ng++; grp[gi].spell = ob[i].spell; grp[gi].status = ob[i].status; grp[gi].allies = 0; grp[gi].rem = obRem(ob[i]); grp[gi].isAbil = ob[i].isAbil; grp[gi].selfHas = 0; grp[gi].aoe = 0; grp[gi].expTick = ob[i].expTick; }
                if (gi >= 0) { grp[gi].allies++; grp[gi].aoe |= ob[i].aoe; }   // aoe = this spell was ever cast as a REAL AoE (>=2 targets at once)
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
        unsigned char jaBits[128]; const bool jaOk = read_usable_ja_bits(jaBits);   // once/frame : which JAs THIS job can use (self-cast filter + shared recast_id disambiguation)
        // ONE definition of "does the buff-source filter keep this timer", shared by the row emit AND by the FOCUS
        // monitor. They used to disagree: the emit applied the filter, the monitor did not -- so under "Mine only" a
        // party WHM's Haste never warned you it was about to expire, then screamed HASTE OUT in red the moment it did.
        // Half a feature reachable, half not. Same verdict for both, so they are reachable or unreachable together.
        auto srcKeeps = [&](unsigned short status, unsigned expiry, int timerIdx) -> bool {
            if (C.tmBuffSrc == TMSRC_ALL) return true;
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
            int rem = ticks_to_sec_ceil((int)(bt[i].expiry - now));   // CEIL, like the client (see ticks_to_sec_ceil) ; not const : clamped to 0 below
            // Our timer runs ~2s AHEAD of the client (measured): at rem 0 the game still shows the icon for about
            // two more seconds. Dropping the row at 0 while the red OUT alert only fires once the buff really
            // leaves the list left a visible HOLE between the two. Hold the row at 0:00 for as long as the buff is
            // genuinely still on you, so the hand-off row -> alert is seamless. Only for a buff we still hold:
            // meHas is authoritative now that an empty list is distinguished from no data.
            if (rem > 6 * 3600) continue;
            if (rem <= 0) { if (!meHas((int)bt[i].id)) continue; rem = 0; }
            // DEBUFFS (Blind / Poison / Slow / Dia / Bio...) leak into the 0x063 self-buff list -- they are NOT buffs,
            // so never in the Duration column (they'll get their own detachable column). Dropped for everyone.
            if (is_debuff_status(bt[i].id)) continue;
            // Hidden (Unfollow) -- UNLESS it's Unfollow-Focus & expiring : then it pops under the warn threshold.
            // Key on the SPELL that produced this buff when we know it. The status key is deliberately an AND over
            // every spell sharing that status (BLU Cocoon / Reactor Cool both give Defense Boost), so hiding ONLY
            // Cocoon never set it -- its recast row hid but its BUFF row stayed visible until Reactor Cool was
            // hidden too, which read as "Hidden+Focus does nothing". The per-entry recast key is always written by
            // the config panel (tm_config.cpp entHiddenSet), so it is the precise answer; fall back to the status
            // key for buffs with no known source spell (abilities, food, gear).
            unsigned hideKey = bt[i].id;
            unsigned focusKey = UiConfig::TM_KEY_FOCUS | bt[i].id;
            // throttle : only when this status' remaining SECOND changed, else 60 identical lines a second
            bool ftrace = false;
            if (timers_focus_trace_armed() && bt[i].id < 1024) {
                const unsigned short r16 = (unsigned short)(rem < 0 ? 0 : (rem > 65000 ? 65000 : rem));
                if (s_focusLastRem[bt[i].id] != r16) { s_focusLastRem[bt[i].id] = r16; ftrace = true; }
            }
            {
                // PER-TIMER spell, exactly like the row's NAME below. self_buff_spell() is per-STATUS and holds only
                // YOUR last cast, so a trust's Honor March inherited the Hidden/Focus settings of YOUR Victory March
                // (same status 214): hiding your song silently hid theirs, and your Hidden+Focus made THEIR row alert.
                const unsigned short srcSpell = party().self_buff_spell_ranked(bt[i].id, bt[i].expiry, i);
                const SpellRow* sr = srcSpell ? spell_info(srcSpell) : 0;
                if (sr && sr->recast_id) {
                    hideKey  = UiConfig::TM_KEY_RECAST + sr->recast_id;
                    focusKey = UiConfig::TM_KEY_FOCUS | (UiConfig::TM_KEY_RECAST + sr->recast_id);
                }
            }
            if (ftrace) {
                const unsigned short srcSpell = party().self_buff_spell(bt[i].id);
                windower::debug::log("FOCUS status=%u '%s' rem=%d  trkJob=%d  srcSpell=%u  hideKey=%04X off=%d  focusKey=%04X off=%d  warn=%d  statusKeyOff=%d",
                                     (unsigned)bt[i].id, buff_status_name(bt[i].id), rem, trkJob, (unsigned)srcSpell,
                                     hideKey,  trkJob ? (C.tm_track_off(trkJob, hideKey)  ? 1 : 0) : -1,
                                     focusKey, trkJob ? (C.tm_track_off(trkJob, focusKey) ? 1 : 0) : -1,
                                     C.tmFocusWarn,
                                     trkJob ? (C.tm_track_off(trkJob, (unsigned)bt[i].id) ? 1 : 0) : -1);
            }
            // tm_self_focus_on(), not the raw focusKey : the monitor that fires the red OUT row ORs in the status
            // mirror, and the config panel writes that mirror as an OR over every same-status spell. Reading only the
            // per-spell key here made the two disagree -- a spell set to plain "Hidden" (help: "never shown. Silence.")
            // still produced a permanent OUT alert because a same-status sibling was focused.
            if (trkJob && C.tm_track_off(trkJob, hideKey)) {
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
            for (int k = 0; k < ng; ++k) if (grp[k].spell == ssid) { gi = k; break; }
            if (gi < 0 && sameSt < 2) for (int k = 0; k < ng; ++k) if (grp[k].status == bt[i].id) { gi = k; break; }
            // Fold your own copy into the ally row ONLY when that row will actually be a grouped "(AoE N)" line (real AoE
            // or "group ally buffs" on). In per-person mode a single-target spell you also put on yourself must stay a
            // SELF row -- pass 3's per-ally branch only emits allies, so folding here would make your own buff vanish.
            // Likewise if the ALLY scope hides this status (and it's not an expiring focus), pass 3 drops the group -> don't
            // fold, or a self-Tracked buff would vanish behind an ally-Hidden setting.
            const bool allyHides = trkJob && C.tm_track_off(trkJob, UiConfig::TM_KEY_ALLY | bt[i].id)
                                   && !(C.tm_track_off(trkJob, UiConfig::TM_KEY_FOCUS | UiConfig::TM_KEY_ALLY | bt[i].id) && rem < C.tmFocusWarn);
            // NB : expTick canNOT be used here as a "same cast" test. Captured : two March groups (Honor + Victory)
            // both carried expTick 682 while the Victory March self timer read 712 -- the frozen expiry is shared
            // between same-status songs, not per cast. Gating the fold on it made the fold never fire for the second
            // song, so every March was drawn TWICE (own row + group row).
            const bool folds = (gi >= 0 && countHas(bt[i].id) >= 2 && (grp[gi].aoe || C.tmAllyGroup != 0) && !allyHides);
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
            if (folds) { grp[gi].selfHas = 1; grp[gi].rem = rem; continue; }
            const SpellRow* ssp = spell_info(ssid);
            bufs[nb].rem = rem; bufs[nb].icon = bt[i].id; bufs[nb].name = (ssp && ssp->en) ? ssp->en : buff_status_name(bt[i].id); bufs[nb].both = 0;
            // BAND : what YOU cast (0), then what real PLAYERS put on you (1), then TRUSTS (2). Each band is still
            // sorted soonest-first by the comparator below. "Yours" is the per-timer caster, not "it is on me".
            const unsigned rowCaster = party().buff_caster_for(bt[i].id, bt[i].expiry, i);
            const bool rowMine = (rowCaster == 0 || rowCaster == party().self_id());   // for TAGS: unknown is probably ours
            // For BANDING, unknown is its own thing. The source filter already treats caster==0 as "infer", but the
            // sort treated it as "mine", so every unattributed row -- food, gear, a 3000-TP boost we failed to parse --
            // sorted ABOVE your own live songs, which is the exact complaint this banding was added to fix.
            bufs[nb].order = (rowCaster == party().self_id()) ? 0
                           : (rowCaster == 0)                 ? 1
                           : party().is_trust(rowCaster)      ? 3 : 2;
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
            bufs[nb].src = 2;   // pass 2 : your OWN 0x063 buff timer
            ++nb;
        }
        {   // GEO : the single Indi- you carry -> a stable row at the COMPUTED aura lifetime (base+JP+gear), not the 3s pulse
            const PartyState::GeoAura& ga = party().self_geo();
            int gr = ga.status ? party().geo_aura_remaining(ga.status) : -1;
            if (gr > 0 && nb < 50) {
                const SpellRow* gsp = spell_info(ga.spell);
                bufs[nb].rem = gr; bufs[nb].icon = ga.status; bufs[nb].name = (gsp && gsp->en) ? gsp->en : buff_status_name(ga.status); bufs[nb].both = 0; bufs[nb].order = 0; bufs[nb].src = 3; ++nb;   // GEO aura
            }
        }

        // ---- pass 3 : emit the buffs you cast on allies, COUNTED + VALIDATED against everyone's REAL buffs. So the AoE
        //      count matches who actually got it (not our 0x028 target parse -- BattleMod-accurate) and a replaced roll
        //      nobody carries drops. total >= 2 -> one "Spell (AoE N)" row (N counts you) ; exactly 1 -> "Person - Spell". ----
        static char obLabel[64][44], tagBuf[64][16];
        const bool graceOB = party().in_zone_grace();   // just zoned : the real 0x076 caches are still refilling -> trust the estimate, don't validate
        if (C.tmMine) for (int k = 0; k < ng && nb < 50; ++k) {
            const int total = countHas(grp[k].status);   // self + party members who REALLY carry it right now
            int effN = total;
            if (graceOB) { const int est = grp[k].allies + (meHas(grp[k].status) ? 1 : 0); if (est > effN) effN = est; }   // across a zone : fall back to the estimated target count
            if (effN < 1) continue;                        // nobody has it anymore (worn / replaced roll) -> drop the group
            if (trkJob && C.tm_track_off(trkJob, UiConfig::TM_KEY_ALLY | grp[k].status)) {   // hidden ally buff -- UNLESS Unfollow-Focus & expiring (pops under the warn threshold)
                if (!(C.tm_track_off(trkJob, UiConfig::TM_KEY_FOCUS | UiConfig::TM_KEY_ALLY | grp[k].status) && grp[k].rem < C.tmFocusWarn)) continue;
            }
            const char* en = grp[k].isAbil ? abil_name_by_id(grp[k].spell) : (spell_info(grp[k].spell) ? spell_info(grp[k].spell)->en : 0);   // rolls -> ability name ; spells -> spell name
            int rem = grp[k].rem;
            if (grp[k].aoe) { int sr = party().self_buff_remaining(grp[k].status); if (sr > 0) rem = sr; }   // a REAL AoE shares your exact self 0x063 timer ; a single-target buff you ALSO happen to have on yourself does NOT -> keep the ally estimate (else e.g. Haste on an ally shows YOUR self-Haste duration)
            // GROUP into one "(AoE N)" row when : it was a REAL AoE cast (Protectra / a spell under SCH Accession /
            // a roll) OR the user keeps "group ally buffs" on. Otherwise (single-target spread) -> one row PER ally.
            const bool group = (grp[k].aoe || C.tmAllyGroup != 0) && effN >= 2;
            if (group) {   // AoE : one grouped row (Minuet V (AoE 6))
                PartyState::RollInfo ri = grp[k].isAbil ? party().roll_info(grp[k].status) : PartyState::RollInfo{ 0, 0 };   // COR roll -> pip value (double-up included)
                bufs[nb].rem = rem; bufs[nb].icon = grp[k].status; bufs[nb].both = meHas(grp[k].status) ? 0 : 1;
                // Bands: yours(0) > players-on-you(1) > trusts-on-you(2) > other AoE groups(3) > per-ally(4+).
                // A group whose SELF copy folded in (grp[].selfHas) is still YOUR OWN buff -- it must stay in band 0.
                // Otherwise your own AoE song left the top band the moment the fold fired and sorted BELOW a trust's
                // buff on you, which is exactly the "my casts are not at the top" complaint.
                bufs[nb].order = grp[k].selfHas ? 0 : 4;
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
                bufs[nb].src = 4;   // pass 3 : a buff YOU cast on allies, shown as one AoE group
                ++nb;   // your AoE -> group first
            } else {   // PER-ALLY : one "Person - Spell" row for each ally who really carries it (self is in pass 2)
                for (int i = 0; i < no && nb < 50; ++i) if (ob[i].spell == grp[k].spell && obRem(ob[i]) > 0) {
                    const BuffSet* bs = party().buffs_for(ob[i].target); bool has = false; if (bs) for (int j = 0; j < bs->n; ++j) if (bs->ids[j] == grp[k].status) { has = true; break; }
                    if (!has && !graceOB) continue;   // that ally lost it (stale) -> skip ; during the zone grace trust the estimate
                    if (en) { _snprintf(obLabel[nb], sizeof(obLabel[nb]), "%s - %s", ob[i].name, en); obLabel[nb][sizeof(obLabel[nb]) - 1] = 0; bufs[nb].name = obLabel[nb]; }
                    else bufs[nb].name = ob[i].name;
                    bufs[nb].rem = obRem(ob[i]); bufs[nb].icon = ob[i].status; bufs[nb].both = 1; bufs[nb].order = 5 + party().party_order(ob[i].target); bufs[nb].src = 5; ++nb;   // individual allies -> after yours(0), others-on-you(1) and AoE(2)
                }
            }
        }
        // ---- FOCUS monitor : for buffs marked FOCUS (Haste/Refresh/Phalanx/Flurry/Composure/Reraise...), remember
        //      them once they're UP -- on YOU (Self) or on an ally (Allies you cast them on) -- and keep a RED row
        //      while the buff is MISSING, until it's re-applied. Pruned when the ally leaves the party or you zone. ----
        if (trkJob) {
            static struct FocusMem { unsigned target; unsigned short status, spell; unsigned char isAbil, self, zoneCheck; unsigned lostMs; char name[20]; } fm[24];
            static int fmN = 0; static unsigned fmZone = 0xFFFFFFFFu; static unsigned fmZoneGraceMs = 0; static unsigned fmGen = 0;
            if (fmGen != g_tmResetGen) { fmN = 0; fmGen = g_tmResetGen; }                       // //aio timers reset -> wipe the monitor
            const unsigned zone = zone_id();
            // a focus buff PERSISTS across a zone : KEEP the monitor, just grace the "lost" alerts while the 0x063 / 0x076 buff
            // lists re-populate. Every entry is flagged for post-zone re-validation (below) : one still MISSING once the lists
            // are back was removed BY THE GAME on zoning (not a real loss) -> it depops silently, no OUT alert.
            if (zone != fmZone) { fmZone = zone; fmZoneGraceMs = nowMs + 8000; }
            if (party().is_zoning()) fmZoneGraceMs = nowMs + 8000;                             // keep the grace armed through the whole loading screen
            const bool zoneGrace = (party().is_zoning() || (int)(fmZoneGraceMs - nowMs) > 0);
            const unsigned meId = party().self_id();
            // Is the buff's real list READY (populated) for its target ? (self = the memory buff list ; ally = the 0x076 cache).
            // Right after a zone these are momentarily EMPTY -- we must NOT read "empty" as "buff gone" or a survivor's
            // zoneCheck clears too early / a casualty is judged before the truth arrives.
            // buffsOk, NOT nbuff > 0 : same trap meHas() above documents. An EMPTY self buff list is a real answer,
            // and it is precisely the state after your last buff lapses -- reading it as "not ready yet" parked the
            // post-zone entry with no decision and no OUT alert until some unrelated buff repopulated the list.
            auto listReady = [&](const FocusMem& e) -> bool { return e.self ? (f.game && f.game->buffsOk) : (party().buffs_for(e.target) != 0); };
            auto focusHas = [&](const FocusMem& e) -> bool {                                    // is the buff actually on its target right now (list assumed ready)
                if (e.self) { if (!f.game) return false; for (int i = 0; i < f.game->nbuff; ++i) if ((int)f.game->buffs[i] == (int)e.status) return true; return false; }
                const BuffSet* bs = party().buffs_for(e.target); if (bs) for (int j = 0; j < bs->n; ++j) if (bs->ids[j] == e.status) return true; return false; };
            { unsigned jc[24]; const int jcn = party().job_changes(jc, 24);                    // a member (self or ally) changed job -> its buffs reset -> drop its focus rows (not a real "loss")
              for (int c = 0; c < jcn; ++c) { int wj = 0;
                for (int q = 0; q < fmN; ++q) { const bool drop = fm[q].self ? (jc[c] == meId) : (fm[q].target == jc[c]); if (!drop) { if (wj != q) fm[wj] = fm[q]; ++wj; } }
                fmN = wj; } }
            { int n2 = 0; const BuffTimer* bt2 = party().buff_timers(n2);                      // remember FOCUS buffs currently up on YOU (Self focus key 0x8000|st)
              for (int i = 0; i < n2; ++i) { const unsigned st = bt2[i].id;
                if (focus_trace_live() && st < 1024 && !is_debuff_status(st) && meHas((int)st)) {
                    unsigned hk, fk2; tm_self_keys(st, hk, fk2);
                    windower::debug::log("FOCUSMON remember? st=%u '%s' meHas=%d focusOn=%d (spellKey=%04X off=%d statusKey=%04X off=%d)",
                                         st, buff_status_name(st), meHas((int)st) ? 1 : 0, tm_self_focus_on(C, trkJob, st) ? 1 : 0,
                                         fk2, C.tm_track_off(trkJob, fk2) ? 1 : 0,
                                         UiConfig::TM_KEY_FOCUS | st, C.tm_track_off(trkJob, UiConfig::TM_KEY_FOCUS | st) ? 1 : 0);
                }
                if (st >= 1024 || is_debuff_status(st) || !meHas((int)st) || !tm_self_focus_on(C, trkJob, st)) continue;
                if (!srcKeeps((unsigned short)st, bt2[i].expiry, i)) continue;   // the source filter hides this row -> it must not alert either   // gate on meHas (same source as the emit check) -> no false alert while it's up ; per-SPELL focus key (see tm_self_keys)
                int s = -1; for (int q = 0; q < fmN; ++q) if (fm[q].self && fm[q].status == st) { s = q; break; }
                if (s < 0 && fmN < 24) { s = fmN++; fm[s].target = meId; fm[s].status = (unsigned short)st; fm[s].self = 1; fm[s].isAbil = 0; fm[s].lostMs = 0; fm[s].zoneCheck = 0; fm[s].name[0] = 0; }
                if (s >= 0) fm[s].spell = party().self_buff_spell_ranked((unsigned short)st, bt2[i].expiry, i);   // refresh the spell/tier each frame (a re-cast at a higher tier updates the OUT label)
              } }
            for (int i = 0; i < no; ++i) {                                                     // remember FOCUS buffs currently up on allies (Allies focus key 0xC000|st ; needs tmMine)
                const unsigned st = ob[i].status;
                if (!C.tm_track_off(trkJob, UiConfig::TM_KEY_FOCUS | UiConfig::TM_KEY_ALLY | st)) continue;
                int s = -1; for (int q = 0; q < fmN; ++q) if (!fm[q].self && fm[q].target == ob[i].target && fm[q].status == st) { s = q; break; }
                if (s < 0 && fmN < 24) { s = fmN++; fm[s].target = ob[i].target; fm[s].status = (unsigned short)st; fm[s].self = 0; fm[s].lostMs = 0; fm[s].zoneCheck = 0; }
                if (s >= 0) { fm[s].spell = ob[i].spell; fm[s].isAbil = ob[i].isAbil; int j = 0; for (; j < 19 && ob[i].name[j]; ++j) fm[s].name[j] = ob[i].name[j]; fm[s].name[j] = 0; }
            }
            if (zoneGrace) for (int q = 0; q < fmN; ++q) fm[q].zoneCheck = 1;                 // through the WHOLE settle : flag every entry (incl. ones just re-populated from a stale list) for post-grace validation
            int w = 0;                                                                        // prune : ally left the party/alliance, or the focus flag was turned off
            for (int q = 0; q < fmN; ++q) {
                const bool live = fm[q].self ? true : (zoneGrace || party().party_order(fm[q].target) <= 17);   // 0..5 party, 6..17 alliance ; 99 = gone (roster is unstable mid-zone -> keep during grace)
                const bool fkOn = fm[q].self ? tm_self_focus_on(C, trkJob, fm[q].status)
                                             : C.tm_track_off(trkJob, UiConfig::TM_KEY_FOCUS | UiConfig::TM_KEY_ALLY | fm[q].status);
                if (focus_trace_live() && !(live && fkOn))
                    windower::debug::log("FOCUSPRUNE st=%u '%s' DROPPED (live=%d focusOn=%d) -> no OUT row possible",
                                         (unsigned)fm[q].status, buff_status_name(fm[q].status), live ? 1 : 0, fkOn ? 1 : 0);
                if (!(live && fkOn)) continue;                                                 // gone / focus off -> drop
                if (fm[q].zoneCheck) {                                                         // pending post-zone check : decide ONLY after the grace ends AND the list is back.
                    if (zoneGrace || !listReady(fm[q])) { /* still settling : keep, no decision, no alert */ }
                    else if (focusHas(fm[q])) fm[q].zoneCheck = 0;                             //   grace over + list stable + present -> survived the zone, track normally
                    else continue;                                                            //   grace over + list stable + ABSENT -> the game dropped it on zoning -> depop, NO alert
                }                                                                             //   (deciding DURING the grace read the stale pre-zone buff list -> false survivors -> OUT)
                // a "Hidden+focus" alert that has held its full tmFocusHold with the buff still gone -> FREE the slot
                // (the emit stops drawing it at that point ; without this it lingers forever and can fill fm[24]).
                if (!fm[q].zoneCheck && fm[q].lostMs && !focusHas(fm[q])) {
                    const bool dkOn = fm[q].self ? tm_self_hidden(C, trkJob, fm[q].status)
                                                 : C.tm_track_off(trkJob, UiConfig::TM_KEY_ALLY | fm[q].status);
                    if (dkOn && (unsigned)(nowMs - fm[q].lostMs) > (unsigned)C.tmFocusHold * 1000u) continue;
                }
                if (w != q) fm[w] = fm[q]; ++w;
            }
            fmN = w;
            for (int q = 0; q < fmN && nb < 50; ++q) {                                         // emit a RED row for each MISSING focus buff (self or ally)
                bool has;
                if (fm[q].self) has = meHas(fm[q].status);
                else { const BuffSet* bs = party().buffs_for(fm[q].target); has = false; if (bs) for (int j = 0; j < bs->n; ++j) if (bs->ids[j] == fm[q].status) { has = true; break; } }
                if (focus_trace_live()) {
                    // nbuff is the crux : meHas() FAILS OPEN (returns true for everything) when the live buff list is
                    // empty, so nbuff==0 would pin has=1 forever and make the alert unreachable. Log the list too.
                    const int nb2 = f.game ? f.game->nbuff : -1;
                    char lst[160]; int o = 0; lst[0] = 0;
                    for (int j = 0; f.game && j < f.game->nbuff && j < 32 && o < 150; ++j)
                        o += _snprintf(lst + o, sizeof(lst) - o, "%u ", (unsigned)f.game->buffs[j]);
                    windower::debug::log("FOCUSEMIT st=%u '%s' self=%d has=%d lostMs=%u zoneGrace=%d nbuff=%d list=[%s]",
                                         (unsigned)fm[q].status, buff_status_name(fm[q].status), fm[q].self, has ? 1 : 0,
                                         fm[q].lostMs, zoneGrace ? 1 : 0, nb2, lst);
                }
                if (has) { fm[q].lostMs = 0; continue; }                                       // still up -> the normal row covers it, reset the loss timer
                if (zoneGrace) { fm[q].lostMs = 0; continue; }                                 // just zoned : buff lists still arriving -> don't false-alert (persist across the zone)
                if (fm[q].lostMs == 0) fm[q].lostMs = nowMs;                                   // just went missing -> stamp it
                // Unfollow-Focus = hidden + focus -> the alert holds tmFocusHold seconds then depops (Focus alone =
                // permanent until re-cast). Per-SPELL hide key : keyed on the shared STATUS this never matched for a
                // buff two spells can grant, so the "hold 15s then depop" branch was unreachable for Cocoon.
                const bool dkOn = fm[q].self ? tm_self_hidden(C, trkJob, fm[q].status)
                                             : C.tm_track_off(trkJob, UiConfig::TM_KEY_ALLY | fm[q].status);
                if (focus_trace_live())
                    windower::debug::log("FOCUSHOLD st=%u '%s' self=%d has=0 lostAgo=%ums hold=%ds dkOn=%d -> %s",
                                         (unsigned)fm[q].status, buff_status_name(fm[q].status), fm[q].self,
                                         (unsigned)(nowMs - fm[q].lostMs), C.tmFocusHold, dkOn ? 1 : 0,
                                         (dkOn && (unsigned)(nowMs - fm[q].lostMs) > (unsigned)C.tmFocusHold * 1000u) ? "DROP (hold expired)" : "DRAW red OUT row");
                if (dkOn && (unsigned)(nowMs - fm[q].lostMs) > (unsigned)C.tmFocusHold * 1000u) continue;
                if (fm[q].self) {
                    const SpellRow* sp = fm[q].spell ? spell_info(fm[q].spell) : 0;
                    _snprintf(obLabel[nb], sizeof(obLabel[nb]), "%s", (sp && sp->en) ? sp->en : buff_status_name(fm[q].status));
                } else {
                    const char* en = fm[q].isAbil ? abil_name_by_id(fm[q].spell) : (spell_info(fm[q].spell) ? spell_info(fm[q].spell)->en : 0);
                    _snprintf(obLabel[nb], sizeof(obLabel[nb]), "%s - %s", fm[q].name, en ? en : "?");
                }
                obLabel[nb][sizeof(obLabel[nb]) - 1] = 0;
                bufs[nb].name = obLabel[nb]; bufs[nb].nameCol = 0xFFFF3B3Bu; bufs[nb].rem = TM_REM_MISSING; bufs[nb].icon = fm[q].status; bufs[nb].both = 1; bufs[nb].order = 0; bufs[nb].src = 6; ++nb;   // ALL "OUT" alerts (self + ally) sort to order 0 : rem=MISSING pulls them to the very top so a small tmMax can't clip a critical alert
            }
        }
        if (f.game) for (int i = 0; i < f.game->nRecast && nr < 50; ++i) {   // recasts are TEXT-only (no menu-icon set exists)
            const GameState::RecastEntry& re = f.game->recasts[i];
            const char* nm = (re.kind == 0) ? abil_name_by_recast(re.recastId, jaBits, jaOk) : spell_name_by_recast(re.recastId);
            if (!nm) continue;
            if (trkJob && C.tm_track_off(trkJob, UiConfig::TM_KEY_RECAST + re.recastId)) continue;   // "track per job" : this recast is unchecked for your job
            recs[nr].rem = re.sec; recs[nr].icon = 0; recs[nr].name = nm; recs[nr].both = 0; recs[nr].order = 0; ++nr;
        }
    }
    if (nb == 0 && nr == 0 && !editing) return;
    // GROUP first (order : your own buffs, then allies in party order), then soonest-first, then a DETERMINISTIC
    // tiebreak (icon, name) so rows with equal remaining -- e.g. your AoE buff and its mirrored ally copy -- are stable.
    auto after = [](const Row& x, const Row& y) -> bool {   // does x sort AFTER y ?
        if (x.order != y.order) return x.order > y.order;
        if (x.rem != y.rem) return x.rem > y.rem;
        if (x.icon != y.icon) return x.icon > y.icon;
        return strcmp(x.name ? x.name : "", y.name ? y.name : "") > 0;
    };
    for (int a = 1; a < nb; ++a) { Row t = bufs[a]; int b = a - 1; while (b >= 0 && after(bufs[b], t)) { bufs[b + 1] = bufs[b]; --b; } bufs[b + 1] = t; }
    for (int a = 1; a < nr; ++a) { Row t = recs[a]; int b = a - 1; while (b >= 0 && after(recs[b], t)) { recs[b + 1] = recs[b]; --b; } recs[b + 1] = t; }
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
                sr_push("  [%-14s] st=%-4d rem=%-6d \"%s\"%s%s%s",
                        SRC[(bufs[i].src >= 0 && bufs[i].src < 7) ? bufs[i].src : 0],
                        bufs[i].icon, bufs[i].rem, bufs[i].name ? bufs[i].name : "?",
                        bufs[i].tag ? bufs[i].tag : "", bufs[i].post ? bufs[i].post : "",
                        ghost ? "   <<< GHOST : not in the game's own buff list" : "");
            }
        }
    }
#endif

    float sscl = C.tmScale; if (sscl < 0.5f) sscl = 0.5f; if (sscl > 2.0f) sscl = 2.0f;
    const float S = (ovS > 0.0f) ? ovS : (screenH / 1000.0f) * sscl;
    const float pad = (ui_config().tmBox.on ? 8.0f : 0.0f) * S, gap = 4.0f * S, midGap = 30.0f * S, icgap = 4.0f * S;   // pad 0 when no box chrome ; midGap : space between the Duration & Recast columns (fused)
    const u32 white = 0xFFEAF0FFu, gold = 0xFFE8C55Au, strk = 0xFF000000u, orange = 0xFFEB9660u, red = 0xFFF06060u, dim = 0xFFB4B9C8u, green = 0xFF74D074u;
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
    auto rowNameW = [&](const Row& R) -> float {
        if (!R.name) return 0.0f;
        float w = fN->measure(R.name, zN);
        if (R.pip > 0) { sprintf(pbuf, "%d", R.pip); w += fN->measure(" [", zN) + fN->measure(pbuf, zN) + fN->measure("]", zN); }
        if (R.tag) w += fN->measure(R.tag, zN);
        if (R.post) w += fN->measure(R.post, zN);
        return w;
    };
    auto drawRowName = [&](const Row& R, float nx, float cy, u32 baseCol) {   // name -> [pip] -> (tag) -> post, each its own colour
        float xx = nx;
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
        float colW[2] = { 0.0f, 0.0f }; int rowsMax = 0;
        for (int c = 0; c < nc; ++c) {
            const Col& CC = cols[c]; if (CC.n > rowsMax) rowsMax = CC.n;
            const bool wantIcon = (CC.mode == TMDISP_ICON || CC.mode == TMDISP_BOTH);
            const bool wantName = (CC.mode == TMDISP_NAME || CC.mode == TMDISP_BOTH);
            bool anyBoth = false; for (int i = 0; i < CC.n; ++i) if (CC.list[i].both) anyBoth = true;   // "buff on ally" rows force icon+name
            const bool colIcon = wantIcon || anyBoth;
            float timeW = 0.0f, nameW = 0.0f;
            for (int i = 0; i < CC.n; ++i) { const float w = fT->measure(fmt(CC.list[i].rem), zT); if (w > timeW) timeW = w;
                const bool rowName = wantName || CC.list[i].both;   // only rows that actually render a name reserve width
                if (rowName && CC.list[i].name) { const float nw = rowNameW(CC.list[i]); if (nw > nameW) nameW = nw; } }
            float leftW = colIcon ? icon : 0.0f;
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
                const bool rWantIcon = wantIcon || CC.list[i].both, rWantName = wantName || CC.list[i].both;
                const bool haveIcon = (CC.tex && ic >= 0 && ic < CC.cells);
                bool drewIcon = false;
                if (rWantIcon && haveIcon) { const float u0 = (float)(ic % BUFF_COLS) * CC.au, v0 = (float)(ic / BUFF_COLS) * CC.av; draw_icon_cell(dev, CC.tex, cx, cyy + (rowH - icon) * 0.5f, icon, icon, u0, u0 + CC.au, v0, v0 + CC.av); drewIcon = true; }
                if (nm && (rWantName || (rWantIcon && !haveIcon))) {   // name : requested, OR fallback when the wanted icon is missing
                    const float nx = cx + (drewIcon ? icon + icgap : 0.0f);
                    u32 baseNameCol = CC.list[i].nameCol ? CC.list[i].nameCol : tm_col(TM_NAME, dim);
                    if (CC.list[i].rem == TM_REM_MISSING) baseNameCol = flashStrong ? 0xFFFF6A6Au : 0xFFFF2020u;   // FOCUS alert : the whole "Ally - Buff" row blinks red
                    else if (C.tmSpAlert && CC.list[i].icon > 0 && CC.list[i].rem > 0 && CC.list[i].rem < 60 && is_sp_buff_status(CC.list[i].icon)) baseNameCol = flashStrong ? 0xFFFFF000u : 0xFFFF1010u;   // SP last-minute : the whole row blinks hard
                    fN->begin(dev); drawRowName(CC.list[i], nx, cyy + rowH * 0.5f, baseNameCol);   // name + optional coloured roll pip / song tag
                }
                fmt(r);
                u32 tc;
                if (r == TM_REM_MISSING) { tc = flashStrong ? 0xFFFF6A6Au : 0xFFFF2020u; }   // FOCUS alert : "OUT" blinks red
                else if (CC.recast) { tc = (r <= 10) ? green : (r <= 30) ? orange : red; }   // recast : INVERSE -- red just after use (long wait) -> orange -> green as it nears ready
                else if (C.tmSpAlert && CC.list[i].icon > 0 && r > 0 && r < 60 && is_sp_buff_status(CC.list[i].icon)) { tc = flashStrong ? 0xFFFFF000u : 0xFFFF1010u; }   // SP ability last minute : HARD blink bright-yellow<->red (Soul Voice -> Nitro window, etc.)
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
u32 Hud::ensure_buff_atlas(u32 dev) {
    if (!buffAtlas_ && buffAtlasTries_ < 12) {
        const unsigned nowMs = GetTickCount();
        // `!buffAtlasNextMs_` FIRST : 0 is the "never tried" sentinel (init + every device change), and comparing a
        // raw GetTickCount() against it goes NEGATIVE once uptime passes 24.8 days -- which silently killed the very
        // first attempt, so the whole bounded retry never armed and EVERY status icon stayed missing for the session.
        if (!buffAtlasNextMs_ || (int)(nowMs - buffAtlasNextMs_) >= 0) {
            buffAtlas_ = load_raw_texture(dev, buff_atlas_path(), BUFF_ATLAS_W, BUFF_ATLAS_H);
            if (!buffAtlas_) {
                ++buffAtlasTries_;
                buffAtlasNextMs_ = (nowMs + 300) | 1;   // |1 : never land back on the 0 "never tried" sentinel
                if (buffAtlasTries_ >= 12)                          // SAY SO when the budget dies -- a probe that goes quiet reads exactly like a bug that isn't happening
                    windower::debug::log("buff atlas: giving up after 12 tries (%s) -- status icons will be missing", buff_atlas_path());
            }
        }
    }
    return buffAtlas_;
}

void Hud::draw_timers(const Frame& f, bool preview, float ovX, float ovY, float ovS) {
    ensure_buff_atlas(f.dev);
    timers_draw(f, preview, ovX, ovY, ovS, (float)screenW_, (float)screenH_, buffAtlas_);
}

// The Help sample owns its own copy of the buff-icon atlas (lazy) so it can draw Duration icons without a Hud.
// File-scope, NOT function-local : Hud::render's dev-change block forgets the member handles, and a
// function-local static is unreachable from it. After a device recreate (zoning / alt-tab) the stale
// handle would go to SetTexture with its owning device destroyed. timers_help_forget() clears it from that block.
static u32 g_tmHelpTex = 0; static bool g_tmHelpTried = false;
void timers_help_forget() { g_tmHelpTex = 0; g_tmHelpTried = false; }
static u32 timers_help_atlas(u32 dev) {
    if (!g_tmHelpTried) { g_tmHelpTex = load_raw_texture(dev, buff_atlas_path(), BUFF_ATLAS_W, BUFF_ATLAS_H); g_tmHelpTried = true; }
    return g_tmHelpTex;
}

// Help sample : the REAL Timers box(es) in preview mode (config-aware), centred at (cx,cy) at scale `s`.
void timers_help_box(const Frame& f, float cx, float cy, float s) {
    timers_draw(f, true, cx, cy, s, 0.0f, 0.0f, timers_help_atlas(f.dev));
}

// Help scale-to-fit : measure at scale 1 (linear in S), pick the largest scale that fits availW (capped at maxScale).
void timers_help_fit(const Frame& f, float availW, float maxScale, float& outScale, float& outH) {
    float bw = 0.0f, bh = 0.0f;
    timers_draw(f, true, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, timers_help_atlas(f.dev), true, &bw, &bh);
    float s = (bw > 1.0f) ? (availW / bw) : maxScale;
    if (s > maxScale) s = maxScale; if (s < 0.6f) s = 0.6f;
    outScale = s; outH = bh * s;
}

} // namespace aio
