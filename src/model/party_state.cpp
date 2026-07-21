// party_state.cpp -- see party_state.h.
#include "model/party_state.h"
#include "model/party_state_internal.h"   // pkt_u16 / pkt_u32 (shared with party_state_zonetracker.cpp)
#include "model/game_mem.h"
#include "model/paths.h"
#include "model/spells_gen.h"
#include "model/weapon_skills_gen.h"   // ws_info : WS/ability names for the target action bar (cat 7 "readies")
#include "model/abilities_gen.h"       // abil_info : job-ability names (cat 7)
#include "model/mobskills_gen.h"       // mobskill_info : MOB TP-move names (cat 7, ids >= 257)
#include "model/tb_debuff_gen.h"
#include "model/tb_buff_gen.h"          // spell_buff : buff spell id -> { status, base duration } (Timers "buff on ally")
#include "model/enh_dur.h"              // caster_enh_dur_pct : "Enhancing Magic eff. dur. +%" from live gear/augments
#include "model/song_dur.h"            // BRD song duration : per-item flat song-duration gear + Troubadour
#include "model/geo_dur.h"             // GEO Indi- duration : base + JP 1362 + flat Indicolure-duration gear
#include "model/action_status_gen.h"   // spell_buff_status / abil_buff_status : action id -> status (self-cast filter)
#include "model/skillchain.h"          // Skillchains : sc_info / sc_skill_lookup / add-effect anim + message tables
#include "windower.h"   // safe_read / valid_ptr (guarded game-memory reads)
#include "windower_debug.h"   // debug::log (//aio bcaptlog : buff-caster capture diagnostic)
#include <windows.h>
#include <time.h>
#include <stdio.h>
#include <cstring>
#include <ctime>        // time() for the treasure-pool expiry (unix timestamps)
#include <math.h>

namespace aio {

using windower::safe_read;
using windower::valid_ptr;

static PartyState g_party;
PartyState& party() { return g_party; }

// One role colour per job (icon tint + badge border + job text). Grouping by role.
unsigned job_role_color(int id) {
    switch (id) {
        case 3: case 5: case 15:            return 0xFF86D36F;  // WHM/RDM/SMN -> healer (green)
        case 21: case 17: case 10:          return 0xFFECC94A;  // GEO/COR/BRD -> buffer (yellow)
        case 22: case 7:                    return 0xFF7D9BF0;  // RUN/PLD     -> tank   (blue)
        case 23:                            return 0xFFB58BF0;  // SPC special trusts    -> purple
        default:                            return 0xFFE08585;  // everything else       -> DD (red)
    }
}

// ---- 0x028 action packet : drive the cast bar ------------------------------------------------
// Bit layout (verified vs Ivaar's actions.lua ; offsets in BITS from byte 0 of the packet):
//   actor id  @ bit 40 (byte 5), 32 bits      | category @ bit 82, 4 bits
//   target[0].action[0].param @ bit 213, 17 bits  -> spell id on a "begin casting" (category 8)
// category 8 = begin casting (magic) ; 4 = casting finished. Little-endian bit packing.
static unsigned getbits(const unsigned char* p, int bitoff, int width, int nbytes) {
    unsigned long long v = 0;
    int bo = bitoff >> 3;
    for (int i = 0; i < 8 && bo + i < nbytes; ++i) v |= (unsigned long long)p[bo + i] << (8 * i);   // never read past the packet
    v >>= (bitoff & 7);
    return (unsigned)(v & ((1ull << width) - 1));
}

// TARGET DEBUFFS (icons + learned countdown). The client stores NO per-mob status list and the 0x028 action
// packet carries NO remaining-time, so we IDENTIFY the debuff a cast lands by mapping the SPELL id (the 0x028
// animation field) -> { status effect, base duration } via tb_debuff_gen.h (generated from the old AioHUD
// TargetBar spell table). Mapping by spell -- NOT by the per-target `param` -- is what makes EVERY enfeeble
// show (Distract / Frazzle / Addle / Slow II ...) without a curated status-id list, AND correctly separates a
// debuff from a nuke (whose param is DAMAGE, and whose spell simply isn't in the debuff table). It also gives
// Dia/Bio the right effect even though their param is damage. The base duration seeds the countdown until a
// real wear-off (0x029) LEARNS the exact lifetime (learnedMs_). A coarse fallback covers a spell absent from
// the table (e.g. an ability/mob-skill debuff recorded with dur=0).
static unsigned debuff_fallback_ms(unsigned s) {
    switch (s) {
        case 10: return   8000;                                          // Stun (very short)
        case 7:  return  30000;                                          // Petrification (Break)
        case 4:  case 5:  case 13:                                       // Paralysis Blindness Slow
        case 128: case 129: case 130: case 131: case 132: case 133:     // elemental DoTs
            return 120000;
        default: return 90000;
    }
}
// 0x029 messages meaning "a status wore off / the target recovered from it" (tb_engine remove set).
static bool is_wearoff_msg(unsigned m) {
    switch (m) { case 64: case 204: case 206: case 321: case 322: case 350: case 531: return true; default: return false; }
}
// //aio dbflog : a countdown of target-debuff mutations still to trace to aiohud_debug.log (0 = off). File-scope
// so the static record_* helpers can log too. Each traced line decrements it, so a capture self-limits.
static int s_dbfTrace = 0;
// Says so when the budget runs out : a probe that goes quiet is indistinguishable from a bug that stopped
// reproducing, which is exactly how two captures were misread during the Timers hunt.
#define DBFTRACE(...) do { if (s_dbfTrace > 0) { --s_dbfTrace; windower::debug::log(__VA_ARGS__); \
                           if (s_dbfTrace == 0) windower::debug::log("=== DBFLOG budget spent -- re-arm with //aio dbflog ==="); } } while (0)

static inline bool is_sleep_status(unsigned s) { return s == 2 || s == 19 || s == 193; }   // Sleep, Sleep II, Lullaby (song sleep)
// Statuses that PREVENT a mob from acting : sleep-family + Petrification (7), Stun (10), Terror (28). If the mob
// ACTS, it is no longer under ANY of them -> drop them. (NOT Bind / Silence / Amnesia / Charm : a mob still acts.)
static inline bool is_incapacitate_status(unsigned s) { return is_sleep_status(s) || s == 7 || s == 10 || s == 28; }
// The only magic-block a MOB can carry is Silence (6) : nothing we can do Mutes a mob (Mute is mob->player only),
// and Omerta is a player event status. So a mob casting a spell clears exactly Silence.
static inline bool is_magic_block_status(unsigned s) { return s == 6; }
// Damage-over-time statuses (each tick deals HP damage -> wakes a slept mob) : Poison (3), Kaustra (23),
// Burn/Frost/Choke/Rasp/Shock/Drown (128-133), Dia (134), Bio (135), Helix (186), Requiem (192).
static inline bool is_dot_status(unsigned s) { return s == 3 || s == 23 || (s >= 128 && s <= 135) || s == 186 || s == 192; }

static void record_debuff(DebuffSet* tds, unsigned tid, unsigned short st, unsigned baseMs, bool bySelf) {
    const unsigned now = GetTickCount();
    const unsigned char sf = bySelf ? 1 : 0;
    if (!baseMs) baseMs = debuff_fallback_ms(st);
    int slot = -1, freeS = -1, oldest = 0;
    for (int s = 0; s < DEBUFF_SLOTS; ++s) { if (tds[s].id == tid) { slot = s; break; } if (!tds[s].id && freeS < 0) freeS = s; if (tds[s].touchMs < tds[oldest].touchMs) oldest = s; }
    if (slot < 0) slot = (freeS >= 0) ? freeS : oldest;    // all slots taken -> evict the OLDEST set, never an actively-debuffed target (was slot 0 -> a pack AoE collapsed two mobs into it)
    DebuffSet& d = tds[slot];
    if (d.id != tid) { if (d.n > 0) DBFTRACE("DBF rec-RESET slot id=%08X (n=%d) -> new tid=%08X st=%u", d.id, d.n, tid, st); d.id = tid; d.n = 0; d.th = 0; d.lastHpp = 0; }    // switched onto a new mob in this slot -> reset (incl. TH + HP watermark, else the recycle check inherits the old mob's HP)
    d.touchMs = now;
    // DoT <-> sleep are mutually exclusive on a mob : a DoT tick wakes the sleep. Our OWN wakes come through the
    // game's "no longer asleep" message (on_029) exactly, but we do NOT receive that message when ANOTHER player's
    // DoT/hit wakes the mob -> enforce it from what we DO track (every caster's debuffs) : any DoT drops any sleep,
    // and a sleep landing on a DoT'd mob is a no-op. (Hits from anyone are handled by the cat 1/2 wake path.)
    if (is_sleep_status(st)) { for (int i = 0; i < d.n; ++i) if (is_dot_status(d.ids[i])) { DBFTRACE("DBF sleep-skip (DoT %u up) tid=%08X st=%u", d.ids[i], tid, st); return; } }
    else if (is_dot_status(st)) { for (int i = 0; i < d.n; ) { if (is_sleep_status(d.ids[i])) { DBFTRACE("DBF sleep-drop (DoT %u) tid=%08X st=%u", st, tid, d.ids[i]); for (int j = i; j < d.n - 1; ++j) { d.ids[j] = d.ids[j + 1]; d.startMs[j] = d.startMs[j + 1]; d.baseMs[j] = d.baseMs[j + 1]; d.self[j] = d.self[j + 1]; } --d.n; } else ++i; } }
    for (int i = 0; i < d.n; ++i) if (d.ids[i] == st) { d.startMs[i] = now; d.baseMs[i] = baseMs; d.self[i] = sf; DBFTRACE("DBF rec-refresh tid=%08X st=%u self=%u n=%d", tid, st, sf, d.n); return; }   // already present -> refresh + caster
    if (d.n < 16) { d.ids[d.n] = st; d.startMs[d.n] = now; d.baseMs[d.n] = baseMs; d.self[d.n] = sf; ++d.n; }
    else { int o = 0; for (int i = 1; i < 16; ++i) if (d.startMs[i] < d.startMs[o]) o = i; d.ids[o] = st; d.startMs[o] = now; d.baseMs[o] = baseMs; d.self[o] = sf; }
    DBFTRACE("DBF rec-ADD tid=%08X st=%u self=%u n=%d", tid, st, sf, d.n);
}
// Treasure Hunter proc on `tid` : find/create the mob's slot and raise its TH level (TH only ever increases on a mob).
static void record_th(DebuffSet* tds, unsigned tid, unsigned char lvl) {
    int slot = -1, freeS = -1, oldest = 0;
    for (int s = 0; s < DEBUFF_SLOTS; ++s) { if (tds[s].id == tid) { slot = s; break; } if (!tds[s].id && freeS < 0) freeS = s; if (tds[s].touchMs < tds[oldest].touchMs) oldest = s; }
    if (slot < 0) slot = (freeS >= 0) ? freeS : oldest;
    DebuffSet& d = tds[slot];
    if (d.id != tid) { d.id = tid; d.n = 0; d.th = 0; d.lastHpp = 0; }
    d.touchMs = GetTickCount();
    if (lvl > d.th) d.th = lvl;
}

// POINTWATCH : the X/h rate ring (recent gains) -> points/hour over the last <=600 s (mirrors pwcore's
// analyze_points_table : sum of gains / span * 3600, 0 until ~30 s of data so a single kill doesn't spike it).
void RateReg::add(int val) {
    const unsigned now = GetTickCount();
    if (n < 128) { t[n] = now; v[n] = val; ++n; }
    else { for (int i = 1; i < 128; ++i) { t[i - 1] = t[i]; v[i - 1] = v[i]; } t[127] = now; v[127] = val; }
}
int RateReg::rate() const {
    const unsigned now = GetTickCount();
    long total = 0; unsigned maxAge = 29000u;
    for (int i = 0; i < n; ++i) { const unsigned age = now - t[i]; if (age <= 600000u) { total += v[i]; if (age > maxAge) maxAge = age; } }
    if (maxAge <= 29000u) return 0;
    return (int)((double)total / ((double)maxAge / 1000.0) * 3600.0);
}
// current time as an FFXI buff tick : Unix seconds -> ticks, minus the epoch + era offset (Windower's bufftime).
// era = 0x100000000/60 * 10 (increments ~every 2.27 years ; the signed diff at the call site tolerates a wrap).
// "Now", in the same absolute 1/60 s ticks as the 0x063 expiries. Read from the CLIENT'S OWN clock, not the PC's.
//
// REVERSED 2026-07-20 (FFXiMain 0x05D63910): the client computes now_seconds = clock->sec + serverOffset, where
// the clock struct is re-synced from the server on EVERY 0x00A zone-in packet (local sec := Timestamp1,
// offset := Timestamp2 - Timestamp1) and free-runs off timeGetTime in between. It never touches the wall clock.
// We did: `time(0)` at whole-second granularity. So any skew between the PC clock and the server -- plus up to a
// second of quantisation -- landed on every countdown we drew. The user measured a steady 2-3 s gap against the
// game's own display; a CONSTANT offset is the signature of a different time reference, not of jitter.
//
// The epoch was never the bug: serverOffset is exactly -1009810800, our EPOCH, and our ERA term is 10*2^32 which
// is 0 mod 2^32. Only the SOURCE of "now" was wrong.
//
// Falls back to the old wall-clock formula whenever the client clock is unreadable or not yet seeded (before the
// first 0x00A). A missing module or a bad pointer must degrade, never crash and never freeze the countdowns.
static const u32 CLK_PTR_RVA = 0x492E10;   // -> the clock struct (single writer, static in .data, never null after init)
static const u32 CLK_SEC_OFF = 0x0C;       // u32 unix seconds
static const u32 CLK_OFF_RVA = 0x4E0AF8;   // i32 server offset (Timestamp2 - Timestamp1)
unsigned ffxi_now_tick() {
    static unsigned s_lastMs = 0xFFFFFFFFu; static unsigned s_cached = 0;
    const unsigned nowMs = GetTickCount();
    if (nowMs == s_lastMs) return s_cached;   // called per row per frame -- one guarded read per ms is plenty
    unsigned out = 0;
    HMODULE h = GetModuleHandleA("FFXiMain.dll");
    if (h) {
        const u32 base = (u32)h; u32 clk = 0, sec = 0, off = 0;
        if (safe_read(base + CLK_PTR_RVA, &clk) && valid_ptr(clk) &&
            safe_read(clk + CLK_SEC_OFF, &sec) && safe_read(base + CLK_OFF_RVA, &off) &&
            !(sec == 0 && off == 0))                        // still zero = not yet seeded by a 0x00A
            out = (sec + off) * 60u;                        // off is negative (-EPOCH) ; u32 wrap is the intended maths
    }
    if (!out) {   // fallback : the original wall-clock derivation
        const double EPOCH = 1009810800.0, ERA = (4294967296.0 / 60.0) * 10.0;
        out = (unsigned)(((double)time(0) - EPOCH - ERA) * 60.0);
    }
    s_lastMs = nowMs; s_cached = out;
    return out;
}

int PartyState::self_buff_remaining(unsigned short status) const {
    const unsigned now = ffxi_now_tick();
    for (int i = 0; i < buffTimerN_; ++i) if (buffTimers_[i].id == status) { int r = ticks_to_sec_ceil((int)(buffTimers_[i].expiry - now)); return r > 0 ? r : -1; }
    return -1;
}
unsigned PartyState::self_buff_expiry(unsigned short status) const {
    for (int i = 0; i < buffTimerN_; ++i) if (buffTimers_[i].id == status) return buffTimers_[i].expiry;
    return 0;
}
int PartyState::geo_aura_remaining(unsigned short status) const {   // computed lifetime of the Indi- YOU carry (not the 3s pulse)
    if (!selfGeo_.status || selfGeo_.status != status) return -1;
    int r = ticks_to_sec_ceil((int)(selfGeo_.expTick - ffxi_now_tick()));
    return r > 0 ? r : -1;
}
// which spell/tier granted the self buff (status, expiry) -- disambiguates two same-status buffs (Minuet V + IV)
// by expiry RANK : the later-expiring buff = the more recently cast spell.
void PartyState::record_cast(unsigned short status, unsigned short spell, unsigned caster, unsigned predExp) {
    if (!status || status >= 1024) return;
    // REPLACE, don't append, when the same caster re-sings the same spell : the new cast overwrites the old buff in
    // game, so keeping both left two live candidates with the same name. Ranking then handed the SAME spell to two
    // different timers -- reported as "Victory March twice" (2026-07-20). One live entry per (status, spell, caster).
    const unsigned now = ffxi_now_tick();
    for (int i = 0; i < 64; ++i) {
        SelfCast& e = selfCasts_[i];
        if (e.status == status && e.spell == spell && e.caster == caster) {
            e.predExp = predExp; e.tick = now; return;
        }
    }
    // Evict by USEFULNESS, not round-robin: an empty slot first, then the one whose buff expired longest ago. A plain
    // rotating head threw away live casts (the player's 9-minute song) to make room for a trust's 60-second one.
    int slot = -1; unsigned oldest = 0xFFFFFFFFu;
    for (int i = 0; i < 64; ++i) {
        const SelfCast& e = selfCasts_[i];
        if (!e.spell) { slot = i; break; }
        if ((int)(e.predExp - now) > 0) continue;          // still alive -> never evict it for a new cast
        if (e.predExp < oldest) { oldest = e.predExp; slot = i; }
    }
    if (slot < 0) {   // every slot still live : sacrifice a FOREIGN cast (soonest to expire) before ever touching one
        unsigned soonest = 0xFFFFFFFFu;   // of ours -- losing our own entry is what hides our row under the source filter
        for (int i = 0; i < 64; ++i)
            if (selfCasts_[i].caster != selfId_ && selfCasts_[i].predExp < soonest) { soonest = selfCasts_[i].predExp; slot = i; }
        if (slot < 0) { slot = selfCastHead_ % 64; selfCastHead_ = (selfCastHead_ + 1) % 64; }   // all ours -> rotate
    }
    SelfCast& sc = selfCasts_[slot];
    sc.status = status; sc.spell = spell; sc.caster = caster; sc.predExp = predExp; sc.tick = now;
}
// Match by predicted-expiry ORDER, not by absolute closeness. MEASURED 2026-07-20, twice:
//   - ranking by RECENCY is wrong whenever durations differ -- a trust's Minuet is the most recent cast but the
//     soonest to expire, so it swapped names with our long one ;
//   - matching on absolute predicted expiry is wrong too : our own duration math (Troubadour/Marcato/gear/JP) was
//     off by more than any sane tolerance, so every one of OUR songs fell back to "unknown" and lost its tier.
// What IS reliable is the ORDERING : the longest-lasting cast produces the longest-lasting timer. So rank the
// same-status timers by expiry, rank the candidate casts by predicted expiry, and pair them off index by index.
// Absolute accuracy no longer matters -- only that our songs out-last a trust's, which they always do.
int PartyState::match_cast(unsigned short status, unsigned expiry, int timerIdx) const {
    if (!status || status >= 1024) return -1;
    const unsigned now = ffxi_now_tick();
    int rank = 0;   // how many same-status timers outlive this one -> our index in the expiry-sorted list
    for (int i = 0; i < buffTimerN_; ++i) {
        if (buffTimers_[i].id != status) continue;
        if (buffTimers_[i].expiry > expiry) { ++rank; continue; }
        // TIE-BREAK on the timer's own slot : two casts in ONE server tick give identical expiries, and without this
        // both rows took the same rank -> the same cast -> the same name printed twice.
        if (timerIdx >= 0 && buffTimers_[i].expiry == expiry && i < timerIdx) ++rank;
    }
    // Is a REAL 0x063 timer for this status still alive ? If so, the cast that produced it MUST stay a candidate even
    // though its PREDICTED expiry has passed -- our duration prediction is only an estimate and underpredicts (a
    // Troubadour'd song we guessed at ~6 min really runs ~8), and the row is driven by the authoritative timer, not by
    // predExp. Dropping the entry at predExp+10 s made the NT tag + tier name vanish at ~6 min while the song was still
    // up (reported 2026-07-21, every character). Only discard a stale cast when NO live timer could use it.
    bool statusLive = false;
    for (int j = 0; j < buffTimerN_; ++j) if (buffTimers_[j].id == status) { statusLive = true; break; }
    int idx[64]; unsigned pe[64]; int nc = 0;                   // candidate casts, still plausibly alive
    for (int i = 0; i < 64 && nc < 64; ++i) {
        const SelfCast& sc = selfCasts_[i];
        if (sc.status != status || !sc.spell || !sc.predExp) continue;
        if (!statusLive && (int)(sc.predExp - now) < -600) continue;   // no live timer AND long expired -> not a candidate
        idx[nc] = i; pe[nc] = sc.predExp; ++nc;
    }
    for (int a = 1; a < nc; ++a) {                              // insertion sort, longest predicted expiry first
        const int ii = idx[a]; const unsigned pp = pe[a]; int b = a - 1;
        while (b >= 0 && pe[b] < pp) { idx[b + 1] = idx[b]; pe[b + 1] = pe[b]; --b; }
        idx[b + 1] = ii; pe[b + 1] = pp;
    }
    if (rank >= nc) return -1;             // no candidate for this rank -> honest "unknown", never someone else's spell
    // DURATION SANITY, and it is deliberately asymmetric. The real buff list is the authority: a trust's cast cannot
    // produce a buff that outlives its own base duration by minutes, so if the live timer runs far past what this
    // FOREIGN cast predicted, the pairing is wrong and the row is almost certainly ours. Returning "unknown" keeps the
    // row visible; attributing it to a trust HIDES it under the buff-source filter. When unsure, never blame a trust.
    // TRUSTS only. A trust has no Troubadour / Marcato, so its cast genuinely cannot outlive its base duration and a
    // big overshoot proves a mis-pairing. A real PLAYER can: MEASURED 2026-07-20 on a second client, Tetsouo's
    // Troubadour'd songs (~600 s) were predicted at the 120 s base and this guard rejected every one of them, so the
    // row resolved to no spell at all -- generic status name, no tier, no JA tags. Guarding against players cost more
    // than it protected.
    const SelfCast& m = selfCasts_[idx[rank]];
    if (m.caster != selfId_ && is_trust(m.caster) && (int)(expiry - m.predExp) > 90 * 60) return -1;
    return idx[rank];
}
// Per-timer caster, with a CO-EXPIRY fallback for statuses the action packet never names.
// MEASURED 2026-07-20 : Monberaux's move 4255 grants Protect AND Shell, but the 0x028 reports ONLY status 40 --
// status 41 exists solely in the 0x063 timer list. There is no field to read, so the attribution has to come from
// the one thing the two share: an IDENTICAL expiry tick means they were granted by the same event. Exact equality
// only (the server stamps them from one action) -- no tolerance, or unrelated buffs would borrow a caster.
unsigned PartyState::buff_caster_for(unsigned short status, unsigned expiry, int timerIdx) const {
    // SELF-ONLY statuses : the game gives no one else any way to put these on you, so "unknown" is never the honest
    // answer -- they are yours by construction. Aftermath (270-273) comes from YOUR OWN weaponskill under a mythic /
    // relic / empyrean; there is no cast for it in the 0x028 at all, which is why the ring can never explain it.
    // Keep this list to cases that are certain: a wrong entry here would claim someone else's buff as ours.
    if (status >= 270 && status <= 273) return selfId_;
    const int k = match_cast(status, expiry, timerIdx);
    if (k >= 0) return selfCasts_[k].caster;
    // A stored caster whose id NO LONGER RESOLVES is stale, not an answer. MEASURED 2026-07-20: re-summoning the
    // trusts gave them new entity ids (Monberaux 011069C6 -> 0110682C), so Shell's attribution pointed at a dead id
    // and rendered as an empty owner. Treat it as unknown and re-derive, instead of reporting a name nobody has.
    const unsigned direct = buff_caster(status);
    if (direct && caster_resolves(direct)) return direct;
    // UNANIMITY required : borrow only if every attributed co-expiring status names the SAME caster. Two casters
    // acting in one server tick is rare but possible, and a wrong borrow is not cosmetic here -- with the buff-source
    // filter on "hide trusts", mis-attributing our own buff to a trust DROPS the row. That is exactly the bug this
    // whole session was spent fixing, so a disagreement must yield "unknown" rather than a coin flip.
    unsigned found = 0;
    for (int i = 0; i < buffTimerN_; ++i) {
        if (buffTimers_[i].id == status || buffTimers_[i].expiry != expiry) continue;
        const unsigned c = buff_caster(buffTimers_[i].id);
        if (!c || !caster_resolves(c)) continue;
        if (found && found != c) return 0;     // co-expiring statuses disagree -> don't guess
        found = c;
    }
    return found;
}
unsigned short PartyState::self_buff_spell_ranked(unsigned short status, unsigned expiry, int timerIdx) const {
    const int k = match_cast(status, expiry, timerIdx);
    if (k >= 0) return selfCasts_[k].spell;
    return 0;   // unattributable -> the caller falls back to the STATUS name (never to one of our own spells)
}

const char* PartyState::pc_name_by_id(unsigned id) const {   // roster / alliance server id -> player name (0 if unknown)
    if (!id) return 0;
    for (int i = 0; i < count; ++i) if (m[i].id == id) return m[i].name;
    for (int t = 0; t < 2; ++t) for (int i = 0; i < alliN_[t]; ++i) if (alli_[t * 6 + i].id == id) return alli_[t * 6 + i].name;
    return 0;
}
int PartyState::party_order(unsigned id) const {   // roster position : 0..5 own party, 6..17 alliance ; 99 = unknown
    if (!id) return 99;
    for (int i = 0; i < count; ++i) if (m[i].id == id) return i;
    for (int t = 0; t < 2; ++t) for (int i = 0; i < alliN_[t]; ++i) if (alli_[t * 6 + i].id == id) return 6 + t * 6 + i;
    return 99;
}
bool PartyState::self_can_produce_buff(unsigned status, const unsigned char* jaBits, bool jaOk) const {
    if (!status || status >= 1024) return true;        // unknown status -> keep (never hide on a guess)
    int mj = 0, sj = 0;
    for (int i = 0; i < count; ++i) if (m[i].id == selfId_) { mj = m[i].mjob; sj = m[i].sjob; break; }
    if (mj < 1) return true;                            // main job not known yet -> keep (don't hide your own buffs)
    const unsigned mask = status_spell_jobs(status);   // jobs that can cast a SELF/PARTY buff granting this status
    if (mj <= 31 && (mask & (1u << (mj - 1)))) return true;
    if (sj >= 1 && sj <= 31 && (mask & (1u << (sj - 1)))) return true;
    if (jaOk) for (int i = 0; i < ABIL_STATUS_N; ++i) if (ABIL_STATUS[i].status == status) {   // a usable JA grants it
        const unsigned id = ABIL_STATUS[i].id;
        if (id < 1024 && (jaBits[id >> 3] & (1u << (id & 7)))) return true;
    }
    return false;
}
// ---- derived-state CACHE (survives a plugin reload ; the pol.exe process + its clocks persist) ----
static const unsigned CACHE_MAGIC = 0x43484941u;   // 'AIHC'
// Version tied to the LAYOUT, like the roster cache (party_state_roster.cpp) : OtherBuff has gained fields over
// time, and a hand-written literal 1 lets a &lt;120 s file from the previous build be re-interpreted under the new
// struct -- garbage strings and counts read as if they were valid.
static const unsigned CACHE_VER = 5u | ((unsigned)sizeof(PartyState::OtherBuff) << 8);   // 3 : + self buff timers, selfBuffSpell_, selfCasts_ (format change -> old files rejected, not misread)
static void cache_name(char* out, int cap, unsigned selfId) { _snprintf(out, cap, "data\\cache\\state_%08X.bin", selfId); out[cap - 1] = 0; }
void PartyState::save_cache(unsigned selfId) const {
    if (!selfId) return;
    char rel[48]; cache_name(rel, sizeof(rel), selfId);
    FILE* f = fopen(plugin_path_r(rel), "wb"); if (!f) return;
    unsigned ver = CACHE_VER; unsigned long long wt = (unsigned long long)time(0);
    fwrite(&CACHE_MAGIC, 4, 1, f); fwrite(&ver, 4, 1, f); fwrite(&wt, 8, 1, f);
    unsigned short cnt;
    cnt = 0; for (int i = 0; i < 1024; ++i) if (rollVal_[i]) ++cnt; fwrite(&cnt, 2, 1, f);   // roll pips
    for (int i = 0; i < 1024; ++i) if (rollVal_[i]) { unsigned short s = (unsigned short)i; fwrite(&s, 2, 1, f); fwrite(&rollVal_[i], 1, 1, f); fwrite(&rollLuck_[i], 1, 1, f); }
    cnt = 0; for (int i = 0; i < 1024; ++i) if (songMod_[i]) ++cnt; fwrite(&cnt, 2, 1, f);   // song modifier tags (by spell)
    for (int i = 0; i < 1024; ++i) if (songMod_[i]) { unsigned short s = (unsigned short)i; fwrite(&s, 2, 1, f); fwrite(&songMod_[i], 1, 1, f); }
    cnt = 0; for (int i = 0; i < 1024; ++i) if (buffCaster_[i]) ++cnt; fwrite(&cnt, 2, 1, f);   // buff casters (source filter)
    for (int i = 0; i < 1024; ++i) if (buffCaster_[i]) { unsigned short s = (unsigned short)i; fwrite(&s, 2, 1, f); fwrite(&buffCaster_[i], 4, 1, f); }
    unsigned short obn = (unsigned short)otherBuffN_; fwrite(&obn, 2, 1, f);   // buffs on allies (AoE grouping)
    if (otherBuffN_ > 0) fwrite(otherBuffs_, sizeof(OtherBuff), otherBuffN_, f);
    // SELF buff timers. Safe to cache because `expiry` is an ABSOLUTE FFXI tick derived from the wall clock
    // (ffxi_now_tick), not a relative countdown -- it stays correct across a plugin reload. Without this the rows
    // simply vanished: buffTimers_ is filled ONLY by the 0x063 order-9 full refresh, and the server does not
    // re-send it when a plugin reloads, so nothing repopulated them until the next zone or buff change.
    unsigned short btn = (unsigned short)buffTimerN_; fwrite(&btn, 2, 1, f);
    if (buffTimerN_ > 0) fwrite(buffTimers_, sizeof(BuffTimer), buffTimerN_, f);
    // Which SPELL produced each self buff -> the row label. Without it a restored song reads "Minuet" instead of
    // "Minuet V", because the tier lives in the spell, not in the buff status.
    cnt = 0; for (int i = 0; i < 1024; ++i) if (selfBuffSpell_[i]) ++cnt; fwrite(&cnt, 2, 1, f);
    for (int i = 0; i < 1024; ++i) if (selfBuffSpell_[i]) { unsigned short s = (unsigned short)i; fwrite(&s, 2, 1, f); fwrite(&selfBuffSpell_[i], 2, 1, f); }
    // Recent self casts. self_buff_spell_ranked() needs these to tell apart two buffs sharing ONE status -- it
    // ranks them by expiry and picks the matching cast. selfBuffSpell_ is indexed BY STATUS and therefore holds
    // only one spell per status, so without this ring two Valor Minuet tiers (both status 198, MEASURED) both
    // resolved to the same name after a reload.
    fwrite(selfCasts_, sizeof(SelfCast), 64, f);
    { const int head = selfCastHead_ % 64; fwrite(&head, 4, 1, f); }   // WRAPPED : selfCastHead_ is monotonic, so writing it raw made the read below reject the whole ring after 24 casts
    fclose(f);
}
void PartyState::load_cache(unsigned selfId) {
    if (!selfId) return;
    char rel[48]; cache_name(rel, sizeof(rel), selfId);
    FILE* f = fopen(plugin_path_r(rel), "rb"); if (!f) return;
    unsigned magic = 0, ver = 0; unsigned long long wt = 0;
    if (fread(&magic, 4, 1, f) != 1 || magic != CACHE_MAGIC || fread(&ver, 4, 1, f) != 1 || ver != CACHE_VER || fread(&wt, 8, 1, f) != 1) { fclose(f); return; }
    // Freshness is now PER SECTION, not all-or-nothing. Everything below is keyed on ABSOLUTE FFXI ticks (expiries,
    // predicted expiries) and stays exactly as valid after a 10-minute unload as after a 10-second one -- expired
    // entries are dropped on load anyway. The old blanket 120 s gate meant that unloading, applying an update and
    // loading again emptied the whole Duration column until the next zone, which is indistinguishable from a bug.
    // Ally rows are the one estimate-based section (GetTickCount deltas), so they keep the tight window.
    const unsigned long long age = (unsigned long long)time(0) - wt;
    if (age > 7200ull) { fclose(f); return; }   // >2 h : the session is certainly not the one that wrote this
    const bool freshEstimates = (age <= 120ull);
    (void)freshEstimates;
    unsigned short cnt = 0;
    if (fread(&cnt, 2, 1, f) == 1) for (int k = 0; k < cnt; ++k) { unsigned short s; unsigned char v, l; if (fread(&s, 2, 1, f) != 1 || fread(&v, 1, 1, f) != 1 || fread(&l, 1, 1, f) != 1) break; if (s < 1024) { rollVal_[s] = v; rollLuck_[s] = l; } }
    if (fread(&cnt, 2, 1, f) == 1) for (int k = 0; k < cnt; ++k) { unsigned short s; unsigned char mk; if (fread(&s, 2, 1, f) != 1 || fread(&mk, 1, 1, f) != 1) break; if (s < 1024) songMod_[s] = mk; }
    if (fread(&cnt, 2, 1, f) == 1) for (int k = 0; k < cnt; ++k) { unsigned short s; unsigned c; if (fread(&s, 2, 1, f) != 1 || fread(&c, 4, 1, f) != 1) break; if (s < 1024) buffCaster_[s] = c; }
    // OtherBuff::name is read RAW from disk, unlike every packet path which terminates it explicitly. Consumers
    // treat it as a C string (hud_timers passes it to _snprintf "%s" and straight to the font drawer), and %s
    // walks the SOURCE to a NUL regardless of the destination bound -- so an unterminated name reads into the
    // neighbouring entries and, at the end of the array, off it, in the RENDER path. Force-terminate on load.
    unsigned short obn = 0;
    if (fread(&obn, 2, 1, f) == 1 && obn <= 32) {
        if (fread(otherBuffs_, sizeof(OtherBuff), obn, f) == obn) {
            // READ AND DISCARD. Ally rows are deliberately NOT restored: they are validated against everyone's
            // real 0x076 buff lists before being drawn, and those lists only exist while packets are flowing.
            // After a reload they are empty (and trusts never appear in them at all), so restoring these rows
            // showed AoE groups that the live session never displayed -- visible for the ~8 s post-load grace,
            // during which the drawer trusts the estimate instead of validating, then vanishing when it expires.
            // Your OWN buff timers below ARE restored: those are exact, absolute, and are what a reload loses.
            otherBuffN_ = 0;
        }
    }
    // self buff timers (absolute FFXI-tick expiries -> valid across a reload)
    unsigned short btn = 0;
    if (fread(&btn, 2, 1, f) == 1 && btn <= 32) {
        if (fread(buffTimers_, sizeof(BuffTimer), btn, f) == btn) {
            const unsigned nowTick = ffxi_now_tick();
            int w = 0;
            for (int k = 0; k < (int)btn; ++k)                       // drop anything that expired while we were unloaded
                if ((int)(buffTimers_[k].expiry - nowTick) > 0) { if (w != k) buffTimers_[w] = buffTimers_[k]; ++w; }
            buffTimerN_ = w;
        }
    }
    // status -> source spell (row labels : "Minuet V", not "Minuet")
    if (fread(&cnt, 2, 1, f) == 1) for (int k = 0; k < cnt; ++k) {
        unsigned short s, sp;
        if (fread(&s, 2, 1, f) != 1 || fread(&sp, 2, 1, f) != 1) break;
        if (s < 1024) selfBuffSpell_[s] = sp;
    }
    // recent self casts -> lets self_buff_spell_ranked disambiguate same-status tiers again
    { SelfCast sc[64]; int head = 0;
      if (fread(sc, sizeof(SelfCast), 64, f) == 64 && fread(&head, 4, 1, f) == 1) {
          for (int k = 0; k < 64; ++k) selfCasts_[k] = sc[k];
          selfCastHead_ = ((head % 64) + 64) % 64;   // tolerate ANY stored value : self_buff_spell_ranked() scans all 24 slots
      } }                                            // and sorts by tick, so it never reads the head -- rejecting the ring over it lost the data for nothing
    fclose(f);
}
void PartyState::arm_bcapt_log(int seconds) {
    bcaptUntilMs_ = GetTickCount() + (unsigned)(seconds > 0 ? seconds : 60) * 1000u;
    bcaptClosed_  = false;
    windower::debug::log("BCAPT window OPEN for %ds -- every cat 4/6/11 action will be logged (any caster, any target)", seconds);
}
bool PartyState::bcapt_armed() {
    if (bcaptClosed_) return false;
    if ((int)(GetTickCount() - bcaptUntilMs_) >= 0) {   // SAY SO when the window dies -- silence is indistinguishable from "no bug"
        bcaptClosed_ = true;
        windower::debug::log("BCAPT window CLOSED (expired) -- re-arm with //aio bcaptlog if you need more");
        return false;
    }
    return true;
}
void PartyState::buff_source_jobs(unsigned status, bool& playerCan, bool& trustCan) const {
    playerCan = trustCan = false;
    const unsigned mask = status_spell_jobs(status);   // jobs that cast a self/party buff granting `status`
    if (!mask || status >= 1024) return;
    auto test = [&](const PMember& mm) {
        if (mm.id == 0 || mm.id == selfId_) return;
        bool can = (mm.mjob >= 1 && mm.mjob <= 31 && (mask & (1u << (mm.mjob - 1))))
                || (mm.sjob >= 1 && mm.sjob <= 31 && (mask & (1u << (mm.sjob - 1))));
        if (!can) return;
        if (mm.isTrust) trustCan = true; else playerCan = true;
    };
    for (int i = 0; i < count; ++i) test(m[i]);                                   // own party (trusts live here)
    for (int t = 0; t < 2; ++t) for (int i = 0; i < alliN_[t]; ++i) test(alli_[t * 6 + i]);   // alliance = real players
}

// Corsair roll -> lucky/unlucky flag for a given pip, by roll ability id (battlemod corsair_rolls). 0 = normal.
static unsigned char roll_luck_of(unsigned aid, unsigned pip) {
    static const struct { unsigned short id; unsigned char lucky, unlucky; } RL[] = {
        {98,5,9},{99,3,7},{100,3,7},{101,5,9},{102,4,8},{103,5,9},{104,3,7},{105,4,8},
        {106,4,8},{107,2,6},{108,4,8},{109,2,6},{110,4,8},{111,4,8},{112,5,9},{113,2,6},
        {114,5,9},{115,3,7},{116,3,7},{117,2,6},{118,3,9},{119,2,7},{120,3,9},{121,4,9},
        {122,5,8},{302,3,10},{303,5,7},{304,2,10},{305,4,8} };
    for (unsigned r = 0; r < sizeof(RL) / sizeof(RL[0]); ++r) if (RL[r].id == aid)
        return (pip == RL[r].lucky) ? 1 : (pip == RL[r].unlucky) ? 2 : 0;
    return 0;
}
void PartyState::on_action(const unsigned char* p) {
    u32 hdr = (u32)p[0] | ((u32)p[1] << 8);
    int size = (int)((hdr >> 9) & 0x7F) * 4;               // packet size in bytes
    if (size < 30) return;                                 // begin-cast needs the action block (bit 213 -> byte 26..)
    u32 cat = getbits(p, 82, 4, size);
    u32 actor = getbits(p, 40, 32, size);

    // ---- SLEEP WAKE BY ACTION : a slept mob that ACTS (it's the ACTOR of an action -- melee / readies a TP move /
    // begins or finishes a spell or job ability) has clearly woken -> drop its sleep. Catches wakes we get no
    // "no longer asleep" message for (someone else woke it), from the mob's OWN broadcast action ; the actor id
    // names the exact mob, so several mobs acting at once are disambiguated cleanly. ----
    if (actor && (actor >> 24) == 0x01 &&
        (cat == 1 || cat == 2 || cat == 3 || cat == 4 || cat == 6 || cat == 7 || cat == 8 || cat == 11)) {
        for (int s = 0; s < DEBUFF_SLOTS; ++s) if (tdebuffs_[s].id == actor) {
            DebuffSet& d = tdebuffs_[s];
            const bool magicCast = (cat == 4 || cat == 8);   // the mob cast a spell -> not Silenced. Melee / TP move / ability do NOT clear Silence.
            for (int i = 0; i < d.n; ) {
                const unsigned cur = d.ids[i];
                if (is_incapacitate_status(cur) || (magicCast && is_magic_block_status(cur))) { DBFTRACE("DBF actor-wake tid=%08X cat=%u st=%u", actor, cat, cur);
                    for (int j = i; j < d.n - 1; ++j) { d.ids[j] = d.ids[j + 1]; d.startMs[j] = d.startMs[j + 1]; d.baseMs[j] = d.baseMs[j + 1]; d.self[j] = d.self[j + 1]; } --d.n; }
                else ++i;
            }
            break;
        }
    }

    // ---- TREASURE HUNTER : detected EXACTLY like the reference addons on RETAIL (Krizz's THTracker + Ariel's easyTH,
    // web-verified 2026-07-10) : a TH proc is an ADD-EFFECT with **animation 7** and **message 603**, and the tier is
    // that add-effect's **param**. On a weaponskill (cat 3) it lands as the action's MAIN message 608, tier = main
    // param. The MESSAGE (603/608) is the discriminator : Enlight is ALSO add-effect animation 7 but with a DIFFERENT
    // message, so gating on 603 cleanly excludes it (the phantom-TH-from-Enlight bug). We walk every action so a proc
    // on any hit of a multi-hit round is caught. Recorded from your / your party's hits onto the struck mob.
    if ((cat == 1 || cat == 2 || cat == 3) && (actor == selfId_ || is_party_or_pet(actor))) {
        // 6 bits, NOT 10 -- PROVEN from the client's own parser (FFXiMain 0x05DEC950, Ghidra 2026-07-20). Bits 78-81
        // are a SEPARATE unknown field ; folding them in inflated the count by multiples of 64 whenever they were
        // non-zero, and the clamp below then silently hid it. Every other target-count read in this file used 6.
        unsigned tc = getbits(p, 72, 6, size); if (tc < 1) tc = 1; if (tc > 16) tc = 16;
        int off = 150;
        for (unsigned t = 0; t < tc; ++t) {
            if (off + 36 > size * 8) break;
            const u32 tgt = getbits(p, off, 32, size);
            unsigned ac = getbits(p, off + 32, 4, size); off += 36;
            for (unsigned a = 0; a < ac && a < 12; ++a) {
                if (off + 86 > size * 8) { off = size * 8; break; }
                const unsigned mainMsg   = getbits(p, off + 44, 10, size);   // action main message @ off+44
                const unsigned mainParam = getbits(p, off + 27, 17, size);   // action main param   @ off+27
                const unsigned hasAdd    = getbits(p, off + 85, 1, size);
                off += 86;
                unsigned aeAnim = 0, aeParam = 0, aeMsg = 0;
                if (hasAdd) { aeAnim = getbits(p, off, 6, size); aeParam = getbits(p, off + 10, 17, size); aeMsg = getbits(p, off + 27, 10, size); off += 37; }
                if (getbits(p, off, 1, size)) off += 35; else off += 1;      // spike block (+1 flag, +34 body)
                if (tgt && (tgt >> 24) == 0x01) {                            // struck a real mob
                    unsigned tier = 0;
                    if ((cat == 1 || cat == 2) && hasAdd && aeAnim == 7 && aeMsg == 603) tier = aeParam;   // melee/ranged proc
                    else if (cat == 3 && mainMsg == 608)                               tier = mainParam;   // weaponskill proc
                    if (tier >= 1 && tier <= 20) record_th(tdebuffs_, tgt, (unsigned char)tier);
                }
            }
        }
    }

    // ---- HATE LIST (module) : any action pairing one of our PCs (you / a party/alliance member) with a non-PC
    // entity records that entity as an aggroing mob (hate_[]) -- this is now the SOLE membership source for the hate
    // list (refresh_hate no longer scans claims ; it only reads these tracked mobs' vitals). We DON'T classify
    // mob-ness by id range here (that wrongly dropped real mobs) : an "enemy candidate" is simply any id >= 0x01000000
    // (mob OR pet), and refresh_hate verifies spawnType 0x10 in memory at display (the addon's is_npc via
    // get_mob_by_id). our PC = self / a roster id. Loops ALL targets (AoE / cleave).
    {
        // friendly = self / a roster PC / a tracked friendly pet (is_party_or_pet) ; enemy candidate = any id
        // >= 0x01000000 that isn't friendly. refresh_hate verifies spawnType 0x10 in memory at display.
        const bool actorFriend = is_party_or_pet(actor);
        const bool actorEnemy = (actor >= 0x01000000u) && !actorFriend;
        u32 tcount = getbits(p, 72, 6, size); if (tcount < 1) tcount = 1; if (tcount > 16) tcount = 16;
        for (u32 i = 0; i < tcount; ++i) {
            const int base = 150 + (int)i * 123;
            if (base + 32 > size * 8) break;
            const u32 tid = getbits(p, base, 32, size);
            unsigned mob = 0, pc = 0;
            if (actorFriend) pc = actor; else if (actorEnemy) mob = actor;
            if (is_party_or_pet(tid)) pc = tid; else if (tid >= 0x01000000u) mob = tid;
            if (mob && pc) record_hate(hate_, mob, pet_owner(pc));   // show the OWNER (not the pet) in the Target column
        }
    }

    // ---- SKILLCHAINS (module) : a WS (cat 3) / spell (cat 4) FINISH either OPENS a step-1 resonance (its own
    // skillchain property, from skillchain_gen) or, when the hit carries a skillchain ADDED EFFECT, CLOSES one
    // (step+1). Pure 0x028 bit-reads (offsets in skillchain.h). GATED to a FRIENDLY actor (you / a party or
    // alliance member / a tracked pet) -- otherwise a random passer-by's skillchain popped in the box. ----
    int scRes = -1;                                        // 0x028 category -> skillchain resource (finish categories only)
    if      (cat == 3)  scRes = SCR_WS;                    // weaponskill_finish
    else if (cat == 4)  scRes = SCR_SPELL;                 // spell_finish (SCH/BLU/nukes)
    else if (cat == 11) scRes = SCR_MOB;                   // mob_tp_finish : BST charmed/jug PET ready moves
    else if (cat == 6 || cat == 13 || cat == 14 || cat == 15) scRes = SCR_JA;   // job_ability / avatar blood pact / *_run
    if (scRes >= 0 && is_party_or_pet(actor) && (size * 8) >= 309) {
        const u32 tid = getbits(p, 150, 32, size);         // target[0].id
        if (tid && (tid >> 24) == 0x01) {
            const u32 aid = getbits(p, 86, 16, size);      // action id (actor.param, all finish categories)
            if (getbits(p, 271, 1, size) && sc_is_skillchain_msg(getbits(p, 299, 10, size))) {   // CLOSE
                const int prop = sc_from_add_effect_anim(getbits(p, 272, 6, size));
                if (prop >= 0) { const SkillRow* sk = sc_skill_lookup(scRes, aid); sc_close(tid, aid, scRes, prop, sk ? sk->delay : 3); }
            } else if (sc_is_finish_msg(getbits(p, 230, 10, size))) {                            // OPEN
                const SkillRow* sk = sc_skill_lookup(scRes, aid);
                if (sk) sc_open(tid, aid, scRes, sk->prop, sk->delay);
            }
        }
    }
    // MY WEAPONSKILL finish -> arcade "ULTRA COMBO" popup. GATE on a genuine weaponskill-finish MESSAGE (the same
    // sc_is_finish_msg the skillchain block trusts) : some THF damage abilities (Mug id45, Despoil id228) also arrive
    // as cat 3, and their action id COLLIDES with a WS id in the shared id space (ws_info(45)=Atonement,
    // ws_info(228)=Final Paradise) -> without this, /ja Mug popped "Atonement". Their action message is NOT a finish
    // message (that's why the skillchain box never fired for them), so this cleanly excludes them while keeping every
    // real weaponskill (physical AND magical).
    // //aio bcaptlog : dump EVERY self action that could feed the WS popup -- category, action id, target message,
    // and how BOTH tables resolve that id -- so a Jump / Weapon Bash capture shows exactly why it is mistaken for a
    // WS. Jump(66) High Jump(67) Weapon Bash(77) Shield Bash(46) are JA ids that collide with WS ids 1-255.
    if (actor == selfId_ && (cat == 3 || cat == 6 || cat == 7) && bcapt_armed()) {
        const u32 aid2 = getbits(p, 86, 16, size);
        const unsigned tmsg = getbits(p, 230, 10, size);
        const WSRow* wr = ws_info(aid2); const AbilRow* ar = abil_info(aid2);
        windower::debug::log("WSPOP? cat=%u id=%u tmsg=%u finishMsg=%d  ws=\"%s\" abil=\"%s\"",
                             cat, aid2, tmsg, sc_is_finish_msg(tmsg) ? 1 : 0,
                             wr ? wr->en : "-", ar ? ar->en : "-");
    }
    // The WS popup must fire for real weaponskills ONLY. Damage JAs (Jump, High Jump, Weapon Bash, Shield Bash...)
    // arrive as cat 3 too, with an id that COLLIDES with a WS id (id 66 = both "Jump" and the WS "Gale Axe"), so
    // neither the category nor the id nor abil-table lookup can tell them apart -- MEASURED 2026-07-21, Savage Blade
    // (id 42) even resolves to the JA "Flee". The ONE field that differs is the target message: a real WS uses 185
    // (Savage Blade, measured), a damage JA uses 317 (Jump, measured). So the popup excludes the JA-damage message.
    // 317 stays in sc_is_finish_msg for skillchains, which is a separate concern.
    const unsigned wsMsg = getbits(p, 230, 10, size);
    if (cat == 3 && actor == selfId_ && sc_is_finish_msg(wsMsg) && wsMsg != 317) {
        const u32 wsid = getbits(p, 86, 16, size);         //   WS id = actor.param @bit 86 (like cat 4/6)
        const u32 dmg  = getbits(p, 213, 17, size);        //   damage = target[0].param @bit 213 (target base 150 + 63)
        const WSRow* w = ws_info(wsid); const char* nm = w ? w->en : "Weapon Skill";
        int i = 0; for (; nm[i] && i < 39; ++i) wsPop_.name[i] = nm[i]; wsPop_.name[i] = 0;
        wsPop_.dmg = (int)dmg; wsPop_.startMs = GetTickCount();
        return;
    }
    // ---- BUFF CASTER ATTRIBUTION (Timers "self-cast only" filter) : a buff spell (cat 4) / job ability (cat 6) /
    //      Trust or mob TP move (cat 11) landing on ME -> remember WHO cast it, keyed by the status it grants. Lets
    //      the box hide buffs others put on you (Haste / songs / rolls / Trust stat boosts) while keeping your
    //      self-casts. Runs before the debuff block (a spell is buff XOR debuff, so no overlap).
    //      Status source : the id->status table (spell_buff_status/abil_buff_status) is precise but only covers
    //      normal spells/JAs -- Trust moves and anything unmapped fall through to the per-target result : when that
    //      block's MESSAGE is a "... gains the effect of X" message (battlemod target.gains set), its param field
    //      holds the granted status id. That single mechanism catches Trust buffs no table has. ----
    if ((cat == 4 || cat == 6 || cat == 11) && selfId_) {
        const u32 aid = getbits(p, 86, 16, size);          // actor.param = spell id (cat 4) / ability id (cat 6)
        const unsigned bstTab = (cat == 4) ? spell_buff_status(aid) : (cat == 6) ? abil_buff_status(aid) : 0;
        // Walk the targets with the SAME variable stride as the TH block above (id 32 + action_count 4, then per
        // action : 86-bit main block + optional 37-bit add-effect + optional 35-bit spike). The old fixed 150+i*123
        // stride misaligned the moment ANY earlier target carried an add-effect, so an AoE party buff (Protectra/
        // Shellra/marches...) that hit YOU as target[1+] never matched selfId_ -> no caster -> "self-cast only"
        // couldn't hide it. Walking properly finds your block at any rank.
        unsigned tc2 = getbits(p, 72, 6, size); if (tc2 < 1) tc2 = 1; if (tc2 > 16) tc2 = 16;
        const bool bcapt = bcapt_armed();
        if (bcapt) {   // ONE header line per cast : who cast what, on how many -- the "qui a cast quoi sur qui" spine
            const char* an = pc_name_by_id(actor); const char* sn = 0;
            if (cat == 4) { const SpellRow* sr = spell_info(aid); sn = sr ? sr->en : 0; }
            windower::debug::log("BCAPT CAST actor=%08X \"%s\"%s%s cat=%u id=%u \"%s\" bstTab=%u targets=%u",
                                 actor, an ? an : "?", is_trust(actor) ? " [TRUST]" : "",
                                 (actor == selfId_) ? " [ME]" : "", cat, aid, sn ? sn : "?", bstTab, tc2);
            // CAN WE SEE ANOTHER CASTER'S JOB-ABILITY STATE ? The song tags (SV/NT/TR/M) and the COR (CC) tag are read
            // from read_player_buffs today -- OUR OWN memory list -- so they only ever apply to what WE cast. On the
            // other client they would have to come from the 0x076 party-buff cache instead. This dumps exactly that:
            // the caster's buff set as WE have it, at the instant we process their cast. The open question is Marcato
            // (231), which the song CONSUMES -- it may already be gone from the 0x076 by the time this runs. SV (52),
            // Nightingale (347), Troubadour (348) and Crooked Cards (601) last long enough to be safe bets.
            // Logs the NEGATIVE case too (no buff set cached at all) -- silence here would read as "no JAs were up".
            if (actor != selfId_ && (cat == 4 || cat == 6)) {
                const BuffSet* cb = buffs_for(actor);
                if (!cb) windower::debug::log("BCAPT   JA-VIS: no 0x076 buff set cached for this caster (out of party / alliance ?)");
                else {
                    char ids[256]; int w = 0; ids[0] = 0;
                    bool sv = false, nt = false, tr = false, mc = false, cc = false;
                    for (int q = 0; q < cb->n; ++q) {
                        const unsigned short b2 = cb->ids[q];
                        if (b2 == 52) sv = true; else if (b2 == 347) nt = true; else if (b2 == 348) tr = true;
                        else if (b2 == 231) mc = true; else if (b2 == 601) cc = true;
                        if (w < (int)sizeof(ids) - 8) w += _snprintf(ids + w, sizeof(ids) - w - 1, "%u ", b2);
                    }
                    ids[sizeof(ids) - 1] = 0;
                    windower::debug::log("BCAPT   JA-VIS: n=%d SV=%d NT=%d TR=%d Marcato=%d CC=%d  [%s]",
                                         cb->n, sv, nt, tr, mc, cc, ids);
                }
            }
        }
        int off = 150;
        for (unsigned t = 0; t < tc2; ++t) {
            if (off + 36 > size * 8) break;
            const u32 tgt = getbits(p, off, 32, size);
            unsigned ac = getbits(p, off + 32, 4, size); off += 36;
            const bool isMe = (tgt == selfId_);
            for (unsigned a = 0; a < ac && a < 12; ++a) {
                if (off + 86 > size * 8) { off = size * 8; break; }
                const unsigned mMsg   = getbits(p, off + 44, 10, size);   // this action's main message
                const unsigned mParam = getbits(p, off + 27, 17, size);   // this action's main param
                const unsigned hasAdd = getbits(p, off + 85, 1, size);
                off += 86;
                if (hasAdd) off += 37;                                     // add-effect body (anim 6 / param 17 / msg 10)
                if (getbits(p, off, 1, size)) off += 35; else off += 1;    // spike block (+1 flag, +34 body)
                if (bcapt) {   // every target/action : shows WHO was touched, and where the bit-stride would drift
                    const char* tn = pc_name_by_id(tgt);
                    windower::debug::log("BCAPT   tgt[%u/%u]=%08X \"%s\"%s act[%u/%u] mMsg=%u mParam=%u hasAdd=%u  (prevCaster[%u]=%08X)",
                                         t + 1, tc2, tgt, tn ? tn : "?", isMe ? " [ME]" : "", a + 1, ac,
                                         mMsg, mParam, hasAdd,
                                         (mParam && mParam < 1024) ? mParam : 0,
                                         (mParam && mParam < 1024) ? buffCaster_[mParam] : 0);
                }
                if (isMe) {
                    // Record the caster for EVERY status this action grants YOU -- a 3000-TP Trust move (e.g. Ygnas)
                    // can stack several buffs (CHR/Attack/Enlight...) in ONE packet as separate result blocks, so we
                    // must NOT stop at the first. Per result : a "... gains the effect of X" message (battlemod
                    // target.gains set) carries the status in its param ; else the ability's own primary status
                    // (bstTab, on the first result). Old code kept only result[0] -> every extra buff leaked the filter.
                    // "... gains the effect of X" message ids (battlemod target.gains). 194/280 = generic buff
                    // (primary / AoE-echo) ; 365/762 = the STAT-BOOST variant used by chemist Mix drinks & Trust
                    // 3000-TP moves (CHR/VIT/STR/Attack Boost...) -- without these the stat boosts leaked the filter.
                    unsigned st = 0;
                    if (mMsg == 186 || mMsg == 194 || mMsg == 205 || mMsg == 230 || mMsg == 266 || mMsg == 280 || mMsg == 319 || mMsg == 365 || mMsg == 762) st = mParam;
                    else if (a == 0 && bstTab) st = bstTab;
                    // DON'T let someone else's cast steal an attribution that is still YOURS. MEASURED 2026-07-20:
                    // all marches share status 214 and all madrigals 199, so when Ulmia/Joachim sang over the
                    // player, buffCaster_[214] went Tetsouo -> Ulmia -> Joachim while the player's OWN Honor March
                    // was still live at 11 min. With the buff-source filter on "mine + players" (tmBuffSrc=1) the
                    // trust attribution then dropped the player's own row: the song vanished from Timers while it
                    // was plainly still up in game. The client keeps NO per-status caster (proven in Ghidra, see
                    // docs/game-data/action-packet.md), so a status-keyed map can hold exactly one caster -- the
                    // only correct rule is that a live self-cast wins over a later foreign cast on the same status.
                    if (st && st < 1024) {
                        const bool mineAndLive = (buffCaster_[st] == selfId_) && (self_buff_expiry((unsigned short)st) != 0);
                        if (!mineAndLive || actor == selfId_) buffCaster_[st] = actor;
                        // A FOREIGN cast landing on us : record it too, with the spell's BASE duration (a trust has no
                        // Troubadour/Marcato). Without this the ring held only our own casts, so a trust's song took a
                        // rank slot and was handed OUR spell -- two "Honor March MNT" rows for one real song.
                        if (cat == 4 && actor != selfId_) {
                            const SpellBuff* sb = spell_buff(aid);
                            if (sb && sb->effect == st) {
                                // Refine the prediction with the caster's OWN job abilities, which the 0x076 cache
                                // does carry (MEASURED). Troubadour doubles a song; Soul Voice / Marcato add half on
                                // the buffing families. Without this a player's song was predicted at its 120 s base
                                // while it really ran ~600 s, and the ordering that pairs timers to casts drifted.
                                double sec = (double)sb->durSec;
                                if (sb->skill == 40) {
                                    const BuffSet* cs = buffs_for(actor);
                                    if (cs) { bool tr = false, sv = false, mc = false;
                                        for (int q = 0; q < cs->n; ++q)
                                            switch (cs->ids[q]) { case 348: tr = true; break; case 52: sv = true; break; case 231: mc = true; break; }
                                        if (tr) sec *= 2.0;
                                        if (sv || mc) sec *= 1.5;
                                    }
                                }
                                record_cast((unsigned short)st, (unsigned short)aid, actor, ffxi_now_tick() + (unsigned)(sec * 60.0));
                            }
                        }
                    }
                }
            }
        }
    }
    // ---- SONG JA TAGS FOR SOMEONE ELSE'S CAST. The block below reads OUR OWN memory buff list, so SV/NT/TR/M
    //      only ever tagged what WE cast. MEASURED 2026-07-20 on a second client: the 0x076 party-buff cache DOES
    //      carry the caster's Nightingale (347) and Troubadour (348) at the moment we process their 0x028, and
    //      Marcato (231) was caught there too -- the song consumes it, but only AFTER this packet. Soul Voice (52)
    //      was never exercised in that capture, so it is included on the same reasoning, not on evidence.
    //      Party only: 0x076 has 5 member slots, so an alliance caster has no buff set and simply gets no tags.
    if (cat == 4 && actor != selfId_ && selfId_) {
        const u32 fsid = getbits(p, 86, 16, size);
        const SpellBuff* fb = spell_buff(fsid);
        const BuffSet* cb = (fb && fb->skill == 40 && fsid < 1024) ? buffs_for(actor) : 0;
        if (cb) {
            unsigned char fm = 0;
            for (int q = 0; q < cb->n; ++q)
                switch (cb->ids[q]) { case 52: fm |= 1; break; case 347: fm |= 2; break;
                                      case 348: fm |= 4; break; case 231: fm |= 8; break; }
            songMod_[fsid] = fm;   // keyed by SPELL, like the self path -- same tier from two casters shares a tag
        }
        // Instrument EVERY branch, success included. The first cut of this block silently did nothing and looked
        // exactly like "the game does not expose the data" -- which the JA-VIS probe had already disproved.
        if (bcapt_armed())
            windower::debug::log("SONGMOD: actor=%08X spell=%u fb=%d skill=%d set=%d -> songMod_[%u]=%02X",
                                 actor, fsid, fb ? 1 : 0, fb ? (int)fb->skill : -1, cb ? cb->n : -1,
                                 fsid, (fsid < 1024) ? songMod_[fsid] : 0);
    }
    // ---- GEO Entrust (JA 386) : arms the NEXT Indi- to be a FIXED buff on an ally (the effect does not move/pulse),
    //      so unlike a normal Indi- aura we DO want to show it on that ally. Remember when it was used. ----
    if (cat == 6 && actor == selfId_ && getbits(p, 86, 16, size) == 386) entrustTick_ = GetTickCount();
    // ---- BUFFS YOU cast on OTHER players (Timers "buff on ally" rows) : a buff spell (cat 4) YOU cast that lands
    //      on a party/alliance member (not yourself) -> record { person, status, ESTIMATED timer } from tb_buff_gen.
    //      The client sends no per-buff timer for other players, so the base duration is an estimate (it ignores
    //      the caster's skill / gear / merits). Refreshed on recast ; expired entries pruned here. ----
    if (cat == 4 && actor == selfId_) {
        const u32 sid = getbits(p, 86, 16, size);              // actor.param = the cast spell id
        const SpellBuff* b = spell_buff(sid);
        if (b) {
            const unsigned nowMs = GetTickCount();
            int w = 0;                                          // prune expired before (re)recording -> keep the list tight
            for (int k = 0; k < otherBuffN_; ++k) if ((int)((otherBuffs_[k].startMs + otherBuffs_[k].durMs) - nowMs) > 0) { if (w != k) otherBuffs_[w] = otherBuffs_[k]; ++w; }
            otherBuffN_ = w;
            // Enhancing Magic duration on ALLIES (reproduces the Windower Timers model, see enh_dur.h + docs/
            // game-data/buffs-on-allies.md). The duration-GEAR (native "listed" + augments) applies to buffs on
            // others for ANY caster/job. Job-specific EXTRA multipliers on top :
            //   dur = (Base + flatSec) x (1+setBonus) x (1+listed%) x (1+augment%) x Perpetuance , cap 1800s
            // - flatSec : RDM (main job) "Enhancing Magic Duration" JP gift 338 (+1s/rank) + merit 2320 (+6s/lvl).
            // - setBonus : RDM Estoqueur's/Lethargy set, only while Composure (status 419) is active (it extends
            //   Composure to party members). - Perpetuance : SCH (status 469) x2..x2.65 by Arbatel bracers.
            unsigned short eids[16]; unsigned char eext[16][24];
            int setPct = 0, listedPct = 0, augPct = 0;
            if (read_equipment_ext(eids, eext)) { setPct = composure_set_pct(eids); listedPct = enh_dur_listed_pct(eids); augPct = enh_dur_augment_pct(eids, eext); }
            bool composure = false, perpetuance = false, troubadour = false, soulvoice = false, marcato = false, nightingale = false;
            { unsigned short mb[32]; int mn = read_player_buffs(mb, 32); for (int k = 0; k < mn; ++k) {
                switch (mb[k]) { case 419: composure = true; break; case 469: perpetuance = true; break; case 348: troubadour = true; break;
                                 case 52: soulvoice = true; break; case 231: marcato = true; break; case 347: nightingale = true; break; } } }   // 499 Clarion Call / 455 Tenuto : no duration effect, deliberately not read
            PlayerInfo me; const bool haveMe = read_player(me);
            const int    flatSec  = (haveMe && me.mjob == 5) ? (read_jp_gift_rank(338) + read_merit_level(2320) * 6) : 0;   // RDM main only
            const double setMult  = (composure && setPct > 0) ? (1.0 + setPct / 100.0) : 1.0;                              // RDM set, needs Composure
            const double perpMult = perpetuance ? perpetuance_mult(eids) : 1.0;                                            // SCH Perpetuance
            const double gearMult = (1.0 + listedPct / 100.0) * (1.0 + augPct / 100.0);                                   // gear : ALL jobs
            // --- BRD song (skill 40) : dur = 120 x m1 x m2 x m3 + a3 (Miracle Cheer -> flat 900). m1 = flat + per-
            // family gear + JP(+5% BRD main) ; m2 = x2 Troubadour ; m3/a3 : Soul Voice/Marcato/Clarion/Tenuto. ---
            const int    songFam  = song_family(sid);
            const int    songJp   = (haveMe && me.mjob == 10) ? 5 : 0;
            // --- GEO Indi- (skill 44) : dur = Base + JP 1362 (+2 s/rank, GEO main = job 21) + flat Indicolure gear ---
            const int    geoJpSec = (haveMe && me.mjob == 21) ? read_jp_gift_rank(1362) * 2 : 0;
            const int    geoGear  = geo_dur_gear_sec(eids);
            const double songM1   = 1.0 + (song_dur_m1_pct(eids, songFam) + songJp) / 100.0;
            const double songM2   = troubadour ? 2.0 : 1.0;
            const double songM3   = ((soulvoice || marcato) && (songFam == 11 || songFam == 12 || songFam == 14)) ? 1.5 : 1.0;
            // flat seconds. Marcato ONLY : neither Clarion Call nor Tenuto touches song duration (confirmed by the
            // player, who mains the job) -- Clarion Call adds a song SLOT, Tenuto protects a song on you from being
            // overwritten. Both used to feed a bogus duration bonus here, and the exclusive ternary also let either of
            // them SUPPRESS Marcato's real one.
            const int    songA3   = marcato ? read_jp_u8(0x148) : 0;
            const bool   miracle  = has_miracle_cheer(eids);
            u32 tc = getbits(p, 72, 6, size); if (tc < 1) tc = 1; if (tc > 16) tc = 16;
            bool aoeSelf = false;   // an AoE buff that ALSO landed on YOU -> the ally copies share your EXACT 0x063 timer
            for (u32 i = 0; i < tc; ++i) { const int b2 = 150 + (int)i * 123; if (b2 + 32 > size * 8) break; if (getbits(p, b2, 32, size) == selfId_) { aoeSelf = true; break; } }
            if (aoeSelf && b->effect < 1024) {   // remember the spell/tier for the self row name (+ a ring for same-status doubles)
                selfBuffSpell_[b->effect] = (unsigned short)sid;
                // Our OWN cast : predict the expiry from the duration we actually computed for it (songs get the full
                // m1/m2/m3 + Marcato math, so a Troubadour'd Minuet predicts ~10 min, not the 120 s base). That accuracy
                // is what lets match_cast tell our long song from a trust's short one on the SAME status.
                double selfSec = (double)b->durSec;
                if (b->skill == 40) selfSec = miracle ? 900.0 : ((double)b->durSec * songM1 * songM2 * songM3 + songA3);
                else if (b->skill == 34) { selfSec = ((double)b->durSec + (double)flatSec) * setMult * gearMult * perpMult; if (selfSec > 1800.0) selfSec = 1800.0; }
                else if (b->skill == 44) selfSec = (double)((int)b->durSec + geoJpSec + geoGear);
                record_cast((unsigned short)b->effect, (unsigned short)sid, selfId_, ffxi_now_tick() + (unsigned)(selfSec * 60.0));
            }
            if (b->skill == 40 && sid < 1024) {   // BRD song : snapshot the song-enhancing JAs UP at cast, keyed by SPELL id
                songMod_[sid] = (unsigned char)((soulvoice ? 1 : 0) | (nightingale ? 2 : 0) | (troubadour ? 4 : 0) | (marcato ? 8 : 0));   // by spell (not status) so two same-family songs (Advancing + Victory March) keep separate tags
            }
            // GEO Indi- (skill 44) is an AURA (the pulse refreshes the effect every ~3s) : normally we make NO per-ally
            // rows. Two exceptions : (1) it landed on YOU -> record the aura you carry with its COMPUTED lifetime (drawn
            // instead of the 3s-looping 0x063 status) ; (2) ENTRUST (JA 386) just used -> the Indi- is a FIXED buff on
            // one ally, so we DO record that ally row. A skill-44 cast consumes the Entrust window either way.
            const bool geoEntrust = (b->skill == 44) && ((unsigned)(nowMs - entrustTick_) < 15000u);
            if (b->skill == 44) {
                if (aoeSelf) record_geo_aura((unsigned short)b->effect, (unsigned short)sid, ffxi_now_tick() + (unsigned)((int)b->durSec + geoJpSec + geoGear) * 60u);
                entrustTick_ = 0;
            }
            if (b->skill == 44 && !geoEntrust) {} else   // normal Indi- (aura) -> skip the ally loop ; entrusted -> run it
            for (u32 i = 0; i < tc; ++i) {
                const int base = 150 + (int)i * 123; if (base + 32 > size * 8) break;
                const u32 tid = getbits(p, base, 32, size);
                if (!tid || tid == selfId_) continue;                          // skip empties / yourself (your own buffs come from 0x063)
                const char* nm = pc_name_by_id(tid); if (!nm || !nm[0]) continue;   // the RELIABLE ally gate : resolves only real party/alliance members (PC ids don't all use the 0x01 mob top-byte)
                int slot = -1;   // key by (target, SPELL) so two tiers of the same song (Minuet V + IV, same status) are two rows
                for (int k = 0; k < otherBuffN_; ++k) if (otherBuffs_[k].target == tid && otherBuffs_[k].spell == (unsigned short)sid) { slot = k; break; }
                if (slot < 0) {                                 // new entry : append, else steal the oldest slot
                    if (otherBuffN_ < 32) slot = otherBuffN_++;
                    else { int o = 0; for (int k = 1; k < 32; ++k) if (otherBuffs_[k].startMs < otherBuffs_[o].startMs) o = k; slot = o; }
                }
                otherBuffs_[slot].target = tid; otherBuffs_[slot].status = (unsigned short)b->effect; otherBuffs_[slot].spell = (unsigned short)sid;
                otherBuffs_[slot].startMs = nowMs;
                otherBuffs_[slot].mirrorSelf = aoeSelf ? 1 : 0;   // AoE-on-self -> the drawer uses your exact self timer
                otherBuffs_[slot].aoe = (tc >= 2) ? 1 : 0;        // the cast hit >=2 targets -> a REAL AoE (Protectra / a spell under SCH Accession) ; 1-target = single-cast, don't force-group it
                unsigned long long ms;
                if (b->skill == 34) {   // Enhancing Magic : (Base + flatSec) x set x gear x Perpetuance, cap 30 min
                    double sec = ((double)b->durSec + (double)flatSec) * setMult * gearMult * perpMult;
                    if (sec > 1800.0) sec = 1800.0;   // 30-min hard cap (Timers : cap=1800s)
                    ms = (unsigned long long)(sec * 1000.0);
                } else if (b->skill == 40) {   // BRD song : 120 x m1 x m2 x m3 + a3 (Miracle Cheer -> flat 900)
                    double sec = miracle ? 900.0 : ((double)b->durSec * songM1 * songM2 * songM3 + songA3);
                    ms = (unsigned long long)(sec * 1000.0);
                } else if (b->skill == 44) {   // GEO Entrust'd Indi- on an ally : additive Base + JP 1362 + Indicolure gear
                    ms = (unsigned long long)((int)b->durSec + geoJpSec + geoGear) * 1000ull;
                } else {
                    ms = (unsigned long long)b->durSec * 1000ull;
                }
                otherBuffs_[slot].durMs = (unsigned)ms;
                otherBuffs_[slot].isAbil = 0;
                int j = 0; for (; j < 19 && nm[j]; ++j) otherBuffs_[slot].name[j] = nm[j]; otherBuffs_[slot].name[j] = 0;
            }
        }
    }
    // ---- ROLLS YOU cast on OTHER players (Corsair Phantom Roll family : cat 6 job ability, NOT cat 4). A roll ALSO
    //      lands on YOU, so every ally row mirrors your EXACT 0x063 self timer (no roll-duration model needed, exactly
    //      like an AoE song). Keyed by (target, ability) ; a re-roll refreshes ; pruned when the ally loses it. ----
    // ---- COR ROLL PIP (Timers "Chaos Roll (7)") : the pip total = target[0].param on the roll's 0x028 (confirmed vs
    //      crb_addon / GearInfo). Double-Up (abil 123, no status of its own) re-rolls the CURRENT roll -> updates its
    //      pip. Colour by the roll's lucky/unlucky numbers (battlemod corsair_rolls). ----
    if (cat == 6) {
        const u32 raid = getbits(p, 86, 16, size);
        const unsigned rst = abil_buff_status(raid);
        const bool isRoll = (rst >= 310 && (rst <= 339 || rst == 600));
        const bool bySelf = (actor == selfId_);
        // Capture the roll PIP for rolls YOU cast (incl. Double-Up, abil 123) AND for a PARTY member COR's roll
        // that lands on YOU (AoE) -> an ally's rolls then show their pip on your Timers row, not a bare name. For
        // another caster we only handle a real roll (not their Double-Up -- we don't track their building roll).
        if (bySelf ? (isRoll || raid == 123) : isRoll) {
            bool landsOnMe = bySelf;                                        // your own roll always applies to you
            if (!bySelf) {                                                  // else confirm the AoE roll actually hit you
                u32 tc = getbits(p, 72, 6, size); if (tc < 1) tc = 1; if (tc > 16) tc = 16;
                for (u32 i = 0; i < tc; ++i) { const int b2 = 150 + (int)i * 123; if (b2 + 32 > size * 8) break; if (getbits(p, b2, 32, size) == selfId_) { landsOnMe = true; break; } }
            }
            const unsigned pip = getbits(p, 150 + 63, 17, size);           // target[0] action param = the pip total (1..12), global to the roll
            if (landsOnMe && pip >= 1 && pip <= 12) {
                const unsigned short tst  = isRoll ? (unsigned short)rst  : lastRollStatus_;   // Double-Up -> the roll it is building
                const unsigned short taid = isRoll ? (unsigned short)raid : lastRollAid_;
                // Crooked Cards (status 601) is CONSUMED by the FIRST roll, so on a Double-Up it's already gone -- but the
                // crooked bonus STAYS on the roll. Double-Up may arrive as abil 123 OR as the roll id itself, so don't
                // rely on that : CC = Crooked Cards up NOW (fresh crooked roll) OR (this roll status is already active AND
                // was already crooked = a Double-Up continuing it). A bust / wear-off drops the buff -> the row just stops.
                // (CC reads YOUR buffs, so it only tags YOUR crooked rolls ; an ally's crooked roll just shows no CC tag.)
                bool ccNow = false, active = false;
                // Crooked Cards (601) : from OUR memory list when the roll is ours, from the 0x076 cache when a
                // party COR rolled it -- same substitution as the song tags above, same 0x076 party-only limit.
                if (bySelf) { unsigned short mb[32]; int mn = read_player_buffs(mb, 32); for (int k = 0; k < mn; ++k) { if (mb[k] == 601) ccNow = true; if (tst && mb[k] == tst) active = true; } }
                else { const BuffSet* cb = buffs_for(actor);
                       if (cb) for (int k = 0; k < cb->n; ++k) { if (cb->ids[k] == 601) ccNow = true; }
                       if (tst) { unsigned short mb[32]; int mn = read_player_buffs(mb, 32); for (int k = 0; k < mn; ++k) if (mb[k] == tst) active = true; } }
                const bool ccOld = (tst && tst < 1024) && ((rollLuck_[tst] >> 2) & 1);
                const bool cc = ccNow || (active && ccOld);
                if (tst && tst < 1024) { rollVal_[tst] = (unsigned char)pip; rollLuck_[tst] = (unsigned char)(roll_luck_of(taid, pip) | (cc ? 0x04 : 0)); }
                if (bySelf && isRoll) { lastRollStatus_ = (unsigned short)rst; lastRollAid_ = (unsigned short)raid; }   // track only YOUR building roll
            }
        }
    }
    if (cat == 6 && actor == selfId_) {
        const u32 aid = getbits(p, 86, 16, size);                          // actor.param = the roll's ability id
        const unsigned st = abil_buff_status(aid);
        if (st >= 310 && (st <= 339 || st == 600)) {                       // a Phantom Roll status (310-339 + Runeist's Roll 600)
            const unsigned nowMs = GetTickCount();
            int w = 0;                                                     // prune expired before (re)recording -> keep the list tight
            for (int k = 0; k < otherBuffN_; ++k) if ((int)((otherBuffs_[k].startMs + otherBuffs_[k].durMs) - nowMs) > 0) { if (w != k) otherBuffs_[w] = otherBuffs_[k]; ++w; }
            otherBuffN_ = w;
            u32 tc = getbits(p, 72, 6, size); if (tc < 1) tc = 1; if (tc > 16) tc = 16;
            bool aoeSelf = false;                                          // the roll landed on YOU -> ally copies mirror your exact 0x063 timer
            for (u32 i = 0; i < tc; ++i) { const int b2 = 150 + (int)i * 123; if (b2 + 32 > size * 8) break; if (getbits(p, b2, 32, size) == selfId_) { aoeSelf = true; break; } }
            // NB : do NOT seed selfBuffSpell_/selfCasts_ here -- those map a status to a SPELL id for the self-row name ;
            // a roll has no tiers, so the self row resolves fine via buff_status_name. Seeding it would mis-name the row.
            for (u32 i = 0; i < tc; ++i) {
                const int base = 150 + (int)i * 123; if (base + 32 > size * 8) break;
                const u32 tid = getbits(p, base, 32, size);
                if (!tid || tid == selfId_) continue;                     // skip empties / yourself (your own roll comes from 0x063)
                const char* nm = pc_name_by_id(tid); if (!nm || !nm[0]) continue;   // resolve real party/alliance members only
                int slot = -1;                                            // key by (target, ABILITY) -- one row per roll type per member
                for (int k = 0; k < otherBuffN_; ++k) if (otherBuffs_[k].target == tid && otherBuffs_[k].spell == (unsigned short)aid) { slot = k; break; }
                if (slot < 0) {
                    if (otherBuffN_ < 32) slot = otherBuffN_++;
                    else { int o = 0; for (int k = 1; k < 32; ++k) if (otherBuffs_[k].startMs < otherBuffs_[o].startMs) o = k; slot = o; }
                }
                otherBuffs_[slot].target = tid; otherBuffs_[slot].status = (unsigned short)st; otherBuffs_[slot].spell = (unsigned short)aid;
                otherBuffs_[slot].startMs = nowMs; otherBuffs_[slot].expTick = 0; otherBuffs_[slot].mirrorSelf = aoeSelf ? 1 : 0;
                otherBuffs_[slot].durMs = 300000u;                        // fallback only ; the mirror uses your exact self timer
                otherBuffs_[slot].isAbil = 1;                             // `spell` holds an ABILITY id -> name via ABILS, not SPELLS
                otherBuffs_[slot].aoe = 1;                                // a Phantom Roll is party-wide -> a real AoE, always group it
                int j = 0; for (; j < 19 && nm[j]; ++j) otherBuffs_[slot].name[j] = nm[j]; otherBuffs_[slot].name[j] = 0;
            }
        }
    }
    if (cat == 4) {                                        // FINISH : a spell resolved -> record the debuff it lands on each target.
        // Reversed via //aio act (bit-level scan) : target[0].id @ bit 150 (32b), per-target STRIDE = 123 bits,
        // The RELIABLE spell id is the ACTOR-level param @ bit 86 (16b) -- this is what parse_action calls
        // actor.param (Bind=258 / Silence=59 confirmed in the log). Do NOT use the per-target ANIMATION field :
        // it coincides with the spell id for single-target casts but a -ga (AoE) spell shares one animation id
        // that mis-maps (Sleepga showed a Frost icon). The 0x028 carries NO per-action "message" for the caster
        // (reads 0 -- verified), so we can't tell enfeeble from nuke by message ; instead we map the SPELL id ->
        // { status, base duration } (tb_debuff_gen, from the old addon's table). A spell absent from the table
        // (nuke/cure) yields nothing, so a nuke's damage param can never fake a debuff. This is what makes
        // Distract/Frazzle/Addle/... show. Tracks EVERY caster -> AoE (Sleepga) hits all mobs, mates' debuffs too.
        const bool bySelf = (actor == selfId_);
        const u32 spellId = getbits(p, 86, 16, size);                // actor.param = the cast spell id (258=Bind, 59=Silence)
        const SpellDebuff* de = spell_debuff(spellId);
        DBFTRACE("DBF cast cat4 actor=%08X spell=%u self=%u -> effect=%d", actor, spellId, bySelf ? 1u : 0u, de ? (int)de->effect : -1);
        if (!de) return;                                             // spell lands no trackable debuff (nuke / cure / buff)
        u32 tcount = getbits(p, 72, 6, size);
        if (tcount < 1) tcount = 1; if (tcount > 16) tcount = 16;
        // VARIABLE stride, same walk as the TH / buff-caster blocks above. The old fixed `150 + i*123` was
        // documented as broken there and rewritten in both -- but not here. It desynchronises the moment ANY
        // earlier target carries an add-effect (+37 bits) or a spike block (+35), so an AoE debuff (Sleepga,
        // Horde Lullaby, a cleaved Dia) read a garbage id AND a garbage message for every target after the
        // first. Silent : it recorded plausible-looking debuffs on the wrong mobs.
        int off = 150;
        for (u32 i = 0; i < tcount; ++i) {
            if (off + 36 > size * 8) break;                // past the packet
            const u32 tid = getbits(p, off, 32, size);
            unsigned ac = getbits(p, off + 32, 4, size); off += 36;
            u32 amsg = 0;                                  // the FIRST action's main message -- what `base + 80` used to read
            for (unsigned a = 0; a < ac && a < 12; ++a) {
                if (off + 86 > size * 8) { off = size * 8; break; }
                if (a == 0) amsg = getbits(p, off + 44, 10, size);
                const unsigned hasAdd = getbits(p, off + 85, 1, size);
                off += 86;
                if (hasAdd) off += 37;                     // optional add-effect block
                if (getbits(p, off, 1, size)) off += 35; else off += 1;   // optional spike block (+1 flag, +34 body)
            }
            if (tid && (tid >> 24) == 0x01) {              // a real server id (top byte 0x01)
                DBFTRACE("DBF cat4-tgt tid=%08X msg=%u effect=%u self=%u", tid, amsg, de->effect, bySelf ? 1u : 0u);
                // "No effect" (msg 75, reversed via //aio dbflog : land = 236, no-effect = 75) : the cast did NOT
                // (re)apply -- the target resisted or already has it -> do NOT add or refresh its timer. Without
                // this, recasting Sleep/Lullaby on an already-slept mob wrongly reset the countdown to full.
                if (amsg == 75) continue;
                record_debuff(tdebuffs_, tid, de->effect, de->durSec * 1000u, bySelf);
            }
        }
        return;
    }
    if (cat == 1 || cat == 2) {                            // auto-attack / ranged : a DAMAGING hit wakes Sleep/Lullaby
        // on the struck mob. FFXI sends NO wear-off packet for a broken sleep, so without this the Sleep icon
        // lingered until its estimated timer (the "hit a slept add, icon stays" bug). Only target[0] is read (the
        // mob you struck) : id @150, param @+63 = damage (>0 = a landed hit, not a miss/shadow). No message field
        // exists on the 0x028, so we gate on damage alone -- a miss has param 0 and correctly does NOT wake sleep.
        const u32 tid = getbits(p, 150, 32, size);
        const u32 st  = getbits(p, 150 + 63, 17, size);    // param : >0 means damage was dealt (not a miss/shadow)
        if (s_dbfTrace > 0 && tid && (tid >> 24) == 0x01) {   // DIAGNOSTIC : log every cat 1/2 hit that lands on a mob we track WITH a sleep, even dmg=0 (reveals a bad damage read / missed wake)
            for (int s = 0; s < DEBUFF_SLOTS; ++s) if (tdebuffs_[s].id == tid) {
                bool slept = false; for (int i = 0; i < tdebuffs_[s].n; ++i) if (is_sleep_status(tdebuffs_[s].ids[i])) slept = true;
                if (slept) DBFTRACE("DBF hit-on-slept tid=%08X cat=%u dmg=%u", tid, cat, st);
                break;
            }
        }
        if (tid && (tid >> 24) == 0x01 && st > 0) {
            for (int s = 0; s < DEBUFF_SLOTS; ++s) if (tdebuffs_[s].id == tid) {
                DebuffSet& d = tdebuffs_[s];
                for (int i = 0; i < d.n; ) {
                    unsigned short id = d.ids[i];
                    if (is_sleep_status(id)) {                 // Sleep, Sleep II, Lullaby (song sleep)
                        DBFTRACE("DBF wake-remove tid=%08X st=%u dmg=%u (n was %d)", tid, id, st, d.n);
                        for (int j = i; j < d.n - 1; ++j) { d.ids[j] = d.ids[j + 1]; d.startMs[j] = d.startMs[j + 1]; d.baseMs[j] = d.baseMs[j + 1]; d.self[j] = d.self[j + 1]; }
                        --d.n;
                    } else ++i;
                }
                break;
            }
        }
        // (Treasure Hunter is detected by the dedicated add-effect walk at the top of on_action -- not here.)
        return;
    }
    if (cat != 8 && cat != 7 && cat != 6) return;          // begin casting a spell (8) ; readies a weaponskill / mob TP
                                                           // move (7) ; a JOB ABILITY (6, instant -> brief flash). cat 4
                                                           // (finish) handled above ; we DON'T clear the bar on it.
    // id field : cat 8 (begin cast) / cat 7 (readies) carry it in the target block @bit 213 ; cat 6 (job ability,
    // a "finish"-style action) carries it in actor.param @bit 86 (same as cat 4's spell id). Verified live : a
    // Blood Rage (abil id 267) read 267 @bit 86 (and garbage @bit 213).
    u32 aid = (cat == 6) ? getbits(p, 86, 16, size) : getbits(p, 213, 17, size);
    // claim a cast slot for this actor (reuse same-id / free / oldest)
    int slot = -1, oldest = 0;
    for (int k = 0; k < 18; ++k) {
        if (casts_[k].id == actor) { slot = k; break; }
        if (!casts_[k].id) { if (slot < 0) slot = k; }
        if (casts_[k].startMs < casts_[oldest].startMs) oldest = k;
    }
    if (slot < 0) slot = oldest;
    casts_[slot].id = actor; casts_[slot].spell = aid; casts_[slot].startMs = GetTickCount();
    if      (cat == 8) { const SpellRow* sp = spell_info(aid); casts_[slot].kind = 0; casts_[slot].durMs = sp ? sp->cast_ms : 0; }
    else if (cat == 7) { casts_[slot].kind = 1; casts_[slot].durMs = 3000; }   // readies (mob TP / WS) : no reliable duration field -> estimate ~3s
    else               { casts_[slot].kind = 2; casts_[slot].durMs = 1500; }   // cat 6 job ability : instant -> a brief 1.5s flash of its name
}

// 0x029 action-message : a "wore off / recovered from" message (is_wearoff_msg : 64/204/206/321/322/350/531,
// per tb_engine) = a status effect ended on <target>. `param @0x0C` = the status id that wore off, and UNLIKE
// the 0x028 packet the target id @0x08 is a valid server id -> remove that exact status from the target's
// tracked debuffs (the inverse of record_debuff) so the icon drops the instant it wears, not on the approximate
// debuff_base_ms timeout. Reversed via //aio wear (Stun: msg 204, param 10).
void PartyState::on_029(const unsigned char* p) {
    u32 hdr = (u32)p[0] | ((u32)p[1] << 8);
    int size = (int)((hdr >> 9) & 0x7F) * 4;
    if (size < 0x1C) return;                               // need up to the message id @0x18
    u32 msg = ((u32)p[0x18] | ((u32)p[0x19] << 8)) & 0x7FFF;
    if (s_dbfTrace > 0) {                                  // DIAGNOSTIC : log EVERY 0x029 landing on a mob we track -> catch the "<mob> is no longer asleep" message id (any wake cause)
        u32 dt = pkt_u32(p, 0x08);
        if (dt && (dt >> 24) == 0x01) for (int s = 0; s < DEBUFF_SLOTS; ++s) if (tdebuffs_[s].id == dt) { DBFTRACE("DBF 029 msg=%u tid=%08X p0C=%u p10=%u", msg, dt, pkt_u32(p, 0x0C), pkt_u32(p, 0x10)); break; }
    }
    if (!is_wearoff_msg(msg)) return;                      // only "wore off / recovered from" messages
    u32 tid = pkt_u32(p, 0x08);                               // target server id (reliable on 0x029)
    u32 st  = pkt_u32(p, 0x0C);                               // param = the status id that wore off
    if (!tid || !st) return;
    for (int s = 0; s < DEBUFF_SLOTS; ++s) {
        if (tdebuffs_[s].id != tid) continue;
        DebuffSet& d = tdebuffs_[s];
        for (int i = 0; i < d.n; ) {
            const unsigned cur = d.ids[i];
            // Remove the exact status that wore off, OR -- when the game reports the generic "<mob> is no longer
            // asleep" (msg 204, param 2, reversed via //aio dbflog) -- EVERY sleep variant : a Lullaby'd mob wakes
            // with param 2 though we track it as 193, so an exact match missed it (coup / DoT / natural wake alike).
            if (cur == st || (is_sleep_status(st) && is_sleep_status(cur))) {
                const unsigned life = GetTickCount() - d.startMs[i];       // LEARN the real duration (keep the longest = the unresisted full duration)
                if (cur < 256 && life >= 2000 && life <= 1800000 && life > learnedMs_[cur]) learnedMs_[cur] = life;
                DBFTRACE("DBF 029-remove tid=%08X st=%u (msg=%u param=%u)", tid, cur, msg, st);
                for (int j = i; j < d.n - 1; ++j) { d.ids[j] = d.ids[j + 1]; d.startMs[j] = d.startMs[j + 1]; d.baseMs[j] = d.baseMs[j + 1]; d.self[j] = d.self[j + 1]; }
                --d.n;                                     // remove the entry (compact)
            } else ++i;
        }
        break;
    }
}

// LEARNED real duration (from the 0x029 wear-off) if this status was ever observed, else the coarse fallback.
// (Per-cast base durations from the spell table are stored per DebuffSet entry and preferred by target_debuffs;
// this is only the last-resort estimate for a status we have neither learned nor a stored base for.)
unsigned PartyState::debuff_dur_ms(unsigned status) const {
    if (status < 256 && learnedMs_[status]) return learnedMs_[status];
    return debuff_fallback_ms(status);
}

const char* PartyState::cast_label(unsigned id, float& pctOut, float& alphaOut, int* kindOut) const {
    pctOut = 0.0f; alphaOut = 0.0f; if (kindOut) *kindOut = 0;
    if (!id) return 0;
    const CastSlot* c = 0;
    for (int k = 0; k < 18; ++k) if (casts_[k].id == id && casts_[k].spell) { c = &casts_[k]; break; }
    if (!c) return 0;
    unsigned dur = c->durMs ? c->durMs : 1;
    unsigned el  = GetTickCount() - c->startMs;
    const unsigned FADE = 350;                             // pop-in / depop window (ms)
    if (el > dur + FADE) return 0;                         // fully gone
    pctOut = el >= dur ? 1.0f : (float)el / (float)dur;
    float a = 1.0f;
    if (el < 150)        a = (float)el / 150.0f;           // smooth POP in
    else if (el > dur)   a = 1.0f - (float)(el - dur) / (float)FADE;   // smooth DEPOP after the cast finishes
    alphaOut = a < 0.0f ? 0.0f : a;
    if (kindOut) *kindOut = c->kind;
    if (c->kind == 1) {                                    // cat 7 "readies" : mob TP move (or a weaponskill)
        const MobSkillRow* ms = mobskill_info(c->spell); if (ms) return ms->en;   // mob TP moves (ids >= 257) : the common case
        const WSRow* w = ws_info(c->spell); if (w) return w->en;                  // a weaponskill (id < 256)
        return "TP Move";
    }
    if (c->kind == 2) {                                    // cat 6 : a JOB ABILITY (id @bit 86, raw)
        const AbilRow* ab = abil_info(c->spell); if (ab) return ab->en;
        return "Ability";
    }
    const SpellRow* sp = spell_info(c->spell);             // kind 0 : spell
    return sp ? sp->en : "Casting";
}

// 0x076 "party buffs" : 5 member slots of 48 bytes, payload at +4 (same base as 0x0DD). Per slot k:
//   id   = u32 @ k*48 + 4   (0 = empty slot ; the LOCAL player is never present here)
//   buff i (0..31) : low = p[k*48+20+i] ; hi2 = (p[k*48+12 + i/4] >> 2*(i&3)) & 3 ; buff = low + 256*hi2
//   255 = empty buff. (Credit: Kenshi/PartyBuffs + Byrth/GearSwap, via XivParty.)
void PartyState::on_076(const unsigned char* p) {
    if (pkt_bytes(p) < 0xF4) return;                           // 5 slots x 48 : reads up to p[4*48+20+31] = p[0xF3]
    for (int k = 0; k < 5; ++k) {
        const int base = k * 48;
        unsigned mid = pkt_u32(p, base + 4);
        if (!mid) continue;
        int slot = -1, free = -1;                              // reuse same-id / first free / overwrite slot 0
        for (int s = 0; s < 18; ++s) {
            if (buffs_[s].id == mid) { slot = s; break; }
            if (!buffs_[s].id && free < 0) free = s;
        }
        if (slot < 0) slot = (free >= 0) ? free : 0;
        BuffSet& bs = buffs_[slot];
        bs.id = mid; bs.n = 0;
        for (int i = 0; i < 32; ++i) {
            unsigned low = p[base + 20 + i];
            unsigned hi2 = (p[base + 12 + (i >> 2)] >> (2 * (i & 3))) & 3;
            unsigned buff = low + 256u * hi2;
            if (buff == 255) continue;                         // empty buff
            bs.ids[bs.n++] = (unsigned short)buff;
        }
    }
}

// 0x01B job-info : cache the 'Encumbrance Flags' bitfield (u32 @+0x60 ; bit sid = equip slot sid is locked).
// The bit order matches our equip-slot ids exactly (main=0 .. back=15) -> the equipment viewer indexes it
// directly. Resent by the game on job change / zone / encumbrance change, so caching stays current.
void PartyState::on_01b(const unsigned char* p) {
    if (pkt_bytes(p) < 0x64) return;          // reads the encumbrance flags @0x60..0x63
    encumber_ = pkt_u32(p, 0x60);
}

const BuffSet* PartyState::buffs_for(unsigned id) const {
    if (!id) return 0;
    for (int s = 0; s < 18; ++s) if (buffs_[s].id == id) return &buffs_[s];
    return 0;
}

// Reconcile "buffs you cast on an ally" against the member's LIVE status icons (0x076) : keep, per status, only
// as many entries as the member actually shows -- newest first. This drops buffs that ended early (dispel /
// wear-off) AND songs pushed out of the BRD song slots when the limit is hit (incl. same-status tiers). A short
// grace period lets a fresh cast register in the 0x076 before it can be pruned.
void PartyState::prune_other_buffs_worn() {
    const unsigned now = GetTickCount();
    const unsigned z = zone_id();                                 // ZONING grace : a zone change (or the loading screen) blanks the
    if (is_zoning() || z != obZone_) obZoneGraceMs_ = now + 8000; //   0x076 party-buff cache for a moment. Enhancing buffs PERSIST across
    obZone_ = z;                                                  //   a zone, so during the grace we keep ally buffs on their estimate
    const bool zoneGrace = (int)(obZoneGraceMs_ - now) > 0;       //   and skip the "worn / disband" checks (they'd false-drop on the empty cache).
    obZoneGrace_ = zoneGrace;                                     //   the drawer (in_zone_grace) uses this to keep showing ally/AoE rows from the estimate.
    bool drop[32]; for (int k = 0; k < otherBuffN_; ++k) drop[k] = false;
    for (int k = 0; k < otherBuffN_; ++k) {
        OtherBuff& ob = otherBuffs_[k];
        if (!zoneGrace && party_order(ob.target) > 17) { drop[k] = true; continue; }   // target left the party/alliance (disband) -> drop
        if (ob.isAbil) {   // ROLLS are always on you + the party TOGETHER : lock the ally copy to YOUR live 0x063 roll
            const unsigned e = self_buff_expiry(ob.status);   // timer, and drop it the moment you lose the roll (worn OR replaced by a newer roll).
            if (e != 0) { ob.expTick = e; ob.mirrorSelf = 0; }
            else if (!zoneGrace && (int)(now - ob.startMs) > 3000) drop[k] = true;   // grace : let the fresh cast's 0x063 arrive first (and survive a zone)
            continue;
        }
        // AoE-on-self : mirror your live self timer only briefly, then SNAPSHOT the exact remaining + freeze (so a
        // later self-only re-cast can't bump this ally row). A cast that re-hits the ally re-arms mirrorSelf.
        if (ob.mirrorSelf && (int)(now - ob.startMs) > 2000) {
            // GUARDED, exactly like the rolls branch six lines up. self_buff_expiry returns 0 for "I have no
            // timer for that status" -- which is the state right after a //unload + //load, because buffTimers_
            // is filled ONLY by the 0x063 order-9 full refresh and the server does not re-send it on a plugin
            // reload. Assigning that 0 unconditionally destroyed the good expiry restored from the cache and
            // dropped back to the estimate, which then expired almost immediately: songs vanished from the
            // Timers box a few seconds after every reload. Keep what we have until a real timer shows up.
            const unsigned e = self_buff_expiry(ob.status);
            if (e != 0) { ob.expTick = e; ob.mirrorSelf = 0; }
        }
        if (ob.expTick) { if ((int)(ob.expTick - ffxi_now_tick()) <= 0) { drop[k] = true; continue; } }   // frozen on the self expiry
        else if ((int)((ob.startMs + ob.durMs) - now) <= 0) { drop[k] = true; continue; }                 // estimate elapsed
        if ((int)(now - ob.startMs) <= 3000) continue;             // grace : let the 0x076 reflect a fresh cast
        if (zoneGrace) continue;                                   // just zoned : the buff persists, the 0x076 cache is still refilling -> keep
        const BuffSet* bs = buffs_for(ob.target);
        if (!bs) continue;                                         // member buffs not cached (alliance/out of zone) -> keep
        int has = 0; for (int i = 0; i < bs->n; ++i) if (bs->ids[i] == ob.status) ++has;   // active copies of this status
        int newer = 0;                                             // fresher entries of the same status on this member
        for (int j = 0; j < otherBuffN_; ++j) if (j != k && otherBuffs_[j].target == ob.target && otherBuffs_[j].status == ob.status && otherBuffs_[j].startMs > ob.startMs) ++newer;
        if (newer >= has) drop[k] = true;                          // beyond the member's active slot count -> replaced/worn
    }
    int w = 0;
    for (int k = 0; k < otherBuffN_; ++k) if (!drop[k]) { if (w != k) otherBuffs_[w] = otherBuffs_[k]; ++w; }
    otherBuffN_ = w;
}

int PartyState::target_th(unsigned id) const {
    if (!id) return 0;
    for (int s = 0; s < DEBUFF_SLOTS; ++s) if (tdebuffs_[s].id == id) return (int)tdebuffs_[s].th;
    return 0;
}

void PartyState::set_debuff_trace(int n) { s_dbfTrace = n; }   // //aio dbflog

void PartyState::clear_debuffs(unsigned id) {
    if (!id) return;
    for (int s = 0; s < DEBUFF_SLOTS; ++s) if (tdebuffs_[s].id == id) { if (tdebuffs_[s].n > 0) DBFTRACE("DBF CLEAR (death via hate) id=%08X n=%d", id, tdebuffs_[s].n); tdebuffs_[s] = DebuffSet{}; return; }   // reset the whole slot (id/n/th/lastHpp) -> the recycled id starts clean
}
// Per-frame HP feed for a mob (the current target). FFXI recycles a mob's SERVER id when it dies and another
// spawns in the same entity slot, so a fresh Apex mob can inherit a corpse's debuffs. Detect the transition:
//   hpp <= 0                    -> the mob DIED : drop its debuffs before the id is recycled.
//   was near-dead, now near-full-> a NEW mob RECYCLED this id (a live mob never jumps ~0 -> ~full) : drop the stale set.
// Either way each live mob keeps its OWN debuffs and nothing is purged from a mob that's simply still alive.
void PartyState::note_mob_hp(unsigned id, int hpp) {
    if (!id) return;
    for (int s = 0; s < DEBUFF_SLOTS; ++s) {
        if (tdebuffs_[s].id != id) continue;
        DebuffSet& d = tdebuffs_[s];
        if (hpp <= 0) { if (d.n > 0) DBFTRACE("DBF hp-DEATH id=%08X hpp=%d n=%d", id, hpp, d.n); d = DebuffSet{}; return; }                       // death
        if (d.lastHpp > 0 && d.lastHpp <= 40 && hpp >= 90) { if (d.n > 0) DBFTRACE("DBF hp-RECYCLE id=%08X last=%d hpp=%d -> n was %d", id, d.lastHpp, hpp, d.n); d = DebuffSet{}; return; }   // recycled id -> fresh spawn (lastHpp>0 : a never-observed slot defaults to 0 and must NOT count as near-death, else a debuff on a high-HP mob is wiped next frame)
        d.lastHpp = (unsigned char)(hpp > 100 ? 100 : hpp);
        return;
    }
}

int PartyState::target_debuffs(unsigned id, unsigned short* out, int* remainSec, unsigned char* isSelf, int maxN) const {
    if (!id || !out || maxN <= 0) return 0;
    const DebuffSet* d = 0;
    for (int s = 0; s < DEBUFF_SLOTS; ++s) if (tdebuffs_[s].id == id) { d = &tdebuffs_[s]; break; }
    if (!d) return 0;
    const unsigned now = GetTickCount();
    static const unsigned SAFETY_MS = 1200000;                            // 20 min : YOUR debuff self-clears on this cap only if its wear-off packet was missed (out of range)
    int n = 0;
    for (int i = 0; i < d->n && n < maxN; ++i) {
        const unsigned st = d->ids[i], el = now - d->startMs[i];
        const bool self = d->self[i] != 0;
        if (self && el > SAFETY_MS) continue;                          // YOURS : a safety cap clears a MISSED wear-off. OTHERS : never auto-removed (no wear-off packet).
        // countdown duration : a LEARNED real lifetime (from your 0x029 wear-offs) wins ; else this cast's base
        // duration (the spell table value stored when recorded) ; else the coarse per-status fallback.
        unsigned dur = (st < 256 && learnedMs_[st]) ? learnedMs_[st] : (d->baseMs[i] ? d->baseMs[i] : debuff_dur_ms(st));
        if (remainSec) remainSec[n] = (el < dur) ? (int)((dur - el + 999) / 1000) : -(int)((el - dur + 999) / 1000);   // countdown ; past the estimate -> NEGATIVE (-0:30 = 30 s over the estimate), icon kept
        if (isSelf) isSelf[n] = self ? 1 : 0;
        out[n] = (unsigned short)st;
        ++n;
    }
    return n;
}

// ---- TREASURE POOL (module) : 0x0D2 item added/removed, 0x0D3 lot info / won ----------------------------------
// 0x0D2 : Item @0x10 (u16), Index @0x14 (u8, slot 0..9), Timestamp @0x18 (u32 unix). Item 0xFFFF = "no change",
// 0 = slot cleared. Each item drops out of the pool ~5 min after its drop timestamp.
void PartyState::on_treasure_add(const unsigned char* p) {
    const unsigned item = (unsigned)p[0x10] | ((unsigned)p[0x11] << 8);
    const unsigned idx  = p[0x14];
    if (idx >= 10) return;
    if (item == 0xFFFF) return;                                   // marker : nothing changed
    if (item == 0) { treasure_[idx] = TreasureItem{}; return; }   // slot emptied
    const unsigned ts = (unsigned)p[0x18] | ((unsigned)p[0x19] << 8) | ((unsigned)p[0x1A] << 16) | ((unsigned)p[0x1B] << 24);
    if (treasure_[idx].itemId == (unsigned short)item && treasure_[idx].timestamp == ts) return;   // already have it -> keep its lot info
    const unsigned now = (unsigned)time(0);
    const unsigned natural = ts + 300;                            // 5-min lottery window
    const unsigned exp = (natural >= now && natural - now <= 300) ? natural : (now + 300);   // fresh drop -> real window ; old item -> give it a fresh 5 min
    treasure_[idx] = TreasureItem{};
    treasure_[idx].itemId = (unsigned short)item;
    treasure_[idx].timestamp = ts;
    treasure_[idx].expireUnix = exp;
}
// 0x0D3 : Index @0x14 (u8), Drop @0x15 (u8 ; !=0 -> won/floored -> gone), Highest Lot @0x0E (u16),
// Highest Lotter Name @0x16 (char[16]).
void PartyState::on_treasure_lot(const unsigned char* p) {
    const unsigned idx = p[0x14];
    if (idx >= 10) return;
    if (p[0x15] != 0) { treasure_[idx] = TreasureItem{}; return; }   // item left the pool (won or dropped to floor)
    if (!treasure_[idx].itemId) return;
    treasure_[idx].lot = (unsigned short)((unsigned)p[0x0E] | ((unsigned)p[0x0F] << 8));
    int i = 0; for (; i < 16 && p[0x16 + i]; ++i) treasure_[idx].lotter[i] = (char)p[0x16 + i];
    treasure_[idx].lotter[i] = 0;
}

} // namespace aio
