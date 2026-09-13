// t_timersrules.cpp -- the rules cut out of the Timers builder (lot D, step 4), one section per rule.
//
// The builder-level cases in t_timers.cpp prove the rows come out right in a whole scenario ; these pin each rule to
// its own inputs, so a regression names the rule rather than a scenario. Every case that names a report was verified
// to FAIL with the branch it protects put back (dev/scripts/unit_mutate.py).
#include "check.h"
#include "model/timers_sort.h"
#include "model/timers_rules.h"
#include "model/focus_rules.h"
#include <cstring>

using namespace aio;

namespace {
TimersRow row(int icon, int rem, const char* name, const char* who = 0, int order = 0) {
    TimersRow r; memset(&r, 0, sizeof(r));
    r.icon = icon; r.rem = rem; r.name = name; r.who = who; r.order = order;
    r.fine = TM_FINE_NONE; r.fineClk = FCLK_NONE;
    return r;
}
struct NoMemory { int operator()(const TimersRow&) const { return -1; } };   // first frame : nothing was on screen
// Last frame's order, by name : the memory the builder keeps as signatures.
struct Memory {
    const char* const* names; int n;
    int operator()(const TimersRow& r) const { for (int i = 0; i < n; ++i) if (r.name && strcmp(r.name, names[i]) == 0) return i; return -1; }
};
bool after(const TimersRow& x, const TimersRow& y, int mode = 0, bool recast = false) { return timers_row_after(x, y, mode, recast, NoMemory()); }

// An ally copy as the freshness rule sees it. Defaults : a real AoE song, past its mirror window, frozen exactly on
// your current timer, cast before your last 0x063. `asked` counts the model searches the rule made.
// A timer as the buff-source filter sees it. Defaults : a buff a real player cast on you, under "Mine only".
struct Timer {
    int flt = TMSRC_MINE; bool carried = false, mix = false, trust = false, produce = false, players = true, trusts = false;
    unsigned who = 0x200, self = 0x100;
    int filter() const { return flt; }
    bool selfCarried() const { return carried; }
    bool foreignStatMix() const { return mix; }
    unsigned caster() const { return who; }
    unsigned me() const { return self; }
    bool isTrust(unsigned) const { return trust; }
    bool selfCanProduce() const { return produce; }
    void sourceJobs(bool& p, bool& t) const { p = players; t = trusts; }
};

// A monitored entry as the two focus verdicts see it. Defaults : an ally entry whose buff is gone, list in hand,
// never seen lost before, Focus (not Hidden+Focus), nothing that would excuse the loss.
struct Watched {
    bool isSelf = false, rowsOff = false, isMuted = false, present = false, grace = false, ready = true, hid = false;
    bool filled = false, unrec = false, repl = false, geo = false, rune = false, focus = true, song = false, away = false, check = false;
    unsigned lost = 0, now = 100000; int hold = 60; int order = 1;
    bool allyRowsOff() const { return rowsOff; }
    bool muted() const { return isMuted; }
    bool up() const { return present; }
    bool has() const { return present; }
    bool zoneGrace() const { return grace; }
    bool listReady() const { return ready; }
    unsigned lostMs() const { return lost; }
    unsigned nowMs() const { return now; }
    bool hidden() const { return hid; }
    int holdSec() const { return hold; }
    bool slotsFilled() const { return filled; }
    bool unrecoverable() const { return unrec; }
    bool replaced() const { return repl; }
    bool geoReplaced() const { return geo; }
    bool runeReplaced() const { return rune; }
    bool self() const { return isSelf; }
    int partyOrder() const { return order; }
    bool focusOn() const { return focus; }
    bool isSong() const { return song; }
    bool offzone() const { return away; }
    bool zoneCheck() const { return check; }
};

// The pass-1 groups as the self-timer rules see them.
struct Grp { unsigned spell, status; bool fresh, aoe; };
struct Groups {
    const Grp* g;
    unsigned spell(int k) const { return g[k].spell; }
    unsigned status(int k) const { return g[k].status; }
    bool fresh(int k) const { return g[k].fresh; }
    bool aoe(int k) const { return g[k].aoe; }
};
// One of YOUR timers and the group it was matched to. Defaults : a real AoE group of 2 allies, you carry it, the
// 0x076 agrees (3 carriers), nothing hides the ally copy.
struct Fold {
    bool have = true, isAoe = true, lag = false, mine = true, hides = false; int allies = 2, carriers = 3;
    mutable int askedCount = 0;
    bool haveGroup() const { return have; }
    int groupAllies() const { return allies; }
    bool groupAoe() const { return isAoe; }
    bool hasLaggard() const { return lag; }
    bool meHas() const { return mine; }
    int countHas() const { ++askedCount; return carriers; }
    bool allyHides() const { return hides; }
};

// A lost monitored entry and your recorded casts, as the swap rules see them. Spells 400-499 are songs, 800-899 Indi-.
struct Cast { unsigned target, spell, startMs; bool aoe; };
struct Swap {
    bool isSelf = false; unsigned tgt = 0x200, sp = 410, st = 198, now = 100000, aura = 0; bool evict = false;
    const Cast* c = 0; int n = 0;
    bool self() const { return isSelf; }
    unsigned target() const { return tgt; }
    unsigned spell() const { return sp; }
    unsigned status() const { return st; }
    unsigned nowMs() const { return now; }
    int others() const { return n; }
    unsigned otherTarget(int i) const { return c[i].target; }
    unsigned otherSpell(int i) const { return c[i].spell; }
    unsigned otherStartMs(int i) const { return c[i].startMs; }
    bool otherAoe(int i) const { return c[i].aoe; }
    bool isSong(unsigned x) const { return x >= 400 && x < 500; }
    bool isIndi(unsigned x) const { return x >= 800 && x < 900; }
    bool evicted() const { return evict; }
    unsigned carriedAura() const { return aura; }
};

// One of YOUR 0x063 timers as the admission rules see it. Defaults : a 3-minute Haste you carry, shown by every filter.
struct Mine {
    bool perm = false, carried = false, have = true, debuff = false, hid = false, focus = false, aura = false, keeps = true;
    int rem = 180, warn = 60; unsigned st = 33;
    bool permanent() const { return perm; }
    int remSec() const { return rem; }
    bool selfCarried() const { return carried; }
    bool meHas() const { return have; }
    bool isDebuff() const { return debuff; }
    unsigned status() const { return st; }
    bool hidden() const { return hid; }
    bool focusOn() const { return focus; }
    int warnSec() const { return warn; }
    bool geoAuraTracked() const { return aura; }
    bool sourceKeeps() const { return keeps; }
};

struct Copy {
    bool abil = false, isAoe = true, mirror = false, evicted = false, grace = false, song = true;
    unsigned exp = 120000, self = 120000, cast = 5000, stamp = 6000;
    mutable int asked = 0;
    bool isAbil() const { return abil; }
    bool aoe() const { return isAoe; }
    bool mirrorSelf() const { return mirror; }
    unsigned expTick() const { return exp; }
    unsigned castMs() const { return cast; }
    unsigned selfExp() const { ++asked; return self; }
    bool evictedRecently() const { ++asked; return evicted; }
    unsigned timersStamp() const { return stamp; }
    bool zoneGrace() const { return grace; }
    bool isSong() const { return song; }
};
}  // namespace

void test_timers_rules() {
    SECTION("timers sort : the band comes first, then the displayed second");
    {
        const TimersRow mine = row(33, 900, "Haste", 0, 0), ally = row(33, 10, "Haste", "Kaories", 11);
        CHECK(after(ally, mine));          // your own tier above your ally-casts, however soon theirs ends
        CHECK(!after(mine, ally));
        TimersRow a = row(40, 245, "Protect"), b = row(41, 244, "Shell");
        a.fine = 244 * 60; a.fineClk = FCLK_SELF;   // sub-second says a is sooner...
        b.fine = 244 * 60 + 30; b.fineClk = FCLK_SELF;
        CHECK(after(a, b));                // ...but the row READS 4:05 against 4:04 : what is displayed decides
    }

    SECTION("timers sort : 'soonest first' mode still puts trusts last");
    {   // tmMax cuts the tail : a trust's Protect must never push out one of your own timers.
        const TimersRow trust = row(40, 5, "Protect", 0, 90), yours = row(33, 900, "Haste", 0, 0);
        CHECK(after(trust, yours, 1));
        CHECK(!after(yours, trust, 1));
        const TimersRow player = row(33, 5, "Haste", 0, 40);
        CHECK(after(yours, player, 1));    // players and you mix by time in this mode
    }

    SECTION("timers sort : two rows within 2 s keep last frame's order -- the yoyo");
    {   // Reported 2026-09-10, "le ballad de kaories continue de faire yoyo" : two songs 1.8 s apart read gaps of 1 and 2
        // on alternate seconds. A hold band of 1 re-sorted them at every gap of 2, twice a second.
        // Mutation : band `d >= -2 && d <= 2` -> `d >= -1 && d <= 1`.
        const TimersRow ballad = row(196, 101, "Mage's Ballad III"), minuet = row(198, 99, "Valor Minuet V");
        static const char* const lastFrame[] = { "Mage's Ballad III", "Valor Minuet V" };   // Ballad was ABOVE
        const Memory mem = { lastFrame, 2 };
        CHECK(!timers_row_after(ballad, minuet, 0, false, mem));   // gap of 2 : Ballad keeps its place
        CHECK(timers_row_after(minuet, ballad, 0, false, mem));
        const TimersRow minuetFar = row(198, 98, "Valor Minuet V");
        CHECK(timers_row_after(ballad, minuetFar, 0, false, mem)); // gap of 3 : time decides again
        // A row that just appeared has no order to keep.
        static const char* const onlyBallad[] = { "Mage's Ballad III" };
        const Memory mem1 = { onlyBallad, 1 };
        CHECK(timers_row_after(ballad, minuet, 0, false, mem1));
    }

    SECTION("timers sort : the hold never applies to OUT alerts");
    {   // An OUT row carries rem = TM_REM_MISSING ; holding two of them by position would freeze a stale order on the
        // alerts that matter most. Mutation : the `x.rem > -1000000 && y.rem > -1000000` guard removed.
        const TimersRow a = row(33, TM_REM_MISSING, "Haste"), b = row(43, TM_REM_MISSING, "Refresh");
        static const char* const last[] = { "Refresh", "Haste" };
        const Memory mem = { last, 2 };
        CHECK(!timers_row_after(a, b, 0, false, mem));             // icon order, not last frame's
    }

    SECTION("timers sort : sub-seconds break ties only between rows read from the same clock");
    {   // Reported 2026-09-10 : an AoE Minuet (your exact 0x063 expiry) beside a Pianissimo Ballad on an ally (its
        // wall-clock estimate) "n'arretent pas de passer l'une en dessous de l'autre" -- the two clocks drift.
        // Mutation : `x.fineClk == y.fineClk &&` removed from the refine condition.
        TimersRow self = row(198, 120, "Valor Minuet V"), est = row(196, 120, "Mage's Ballad III");
        self.fine = 120 * 60 - 40; self.fineClk = FCLK_SELF;
        est.fine = 120 * 60 - 10;  est.fineClk = FCLK_EST;          // the estimate's drift CLAIMS it ends later
        CHECK(!after(est, self));                                   // icon 196 < 198 : the stable tiebreak, not the drift
        CHECK(after(self, est));
        TimersRow s2 = est; s2.fineClk = FCLK_SELF;                 // the same numbers read from ONE clock ...
        CHECK(after(s2, self));                                     // ... and the exact tick decides
        CHECK(!after(self, s2));
    }

    SECTION("timers sort : same buff, same second, two people -- the person breaks the tie");
    {   // Tying here left the order to the build order, which moves when the model list is compacted.
        const TimersRow gab = row(33, 60, "Haste", "Gab", 11), aeryn = row(33, 60, "Haste", "Aeryn", 11);
        CHECK(after(gab, aeryn));
        CHECK(!after(aeryn, gab));
    }

    SECTION("timers sort : recasts by name when asked");
    {
        const TimersRow berserk = row(0, 5, "Berserk"), provoke = row(0, 60, "Provoke");
        CHECK(after(berserk, provoke, 0, true) == false);           // soonest first
        CHECK(after(provoke, berserk, 1, true) == true);            // by name : Berserk < Provoke
        CHECK(after(berserk, provoke, 1, true) == false);
    }

    SECTION("timers sort : a row's identity is its icon, pass, name and person -- never its position");
    {
        TimersRow a = row(33, 60, "Haste", "Gab", 11); a.src = 5;
        TimersRow b = a; b.rem = 12; b.order = 30;
        CHECK_EQ(timers_row_sig(a), timers_row_sig(b));             // time and band move ; the row is the same row
        TimersRow c = a; c.who = "Aeryn";
        CHECK(timers_row_sig(a) != timers_row_sig(c));
        TimersRow d = a; d.src = 6;
        CHECK(timers_row_sig(a) != timers_row_sig(d));              // the OUT alert for it is a different row
    }

    SECTION("timers fresh : rolls and single-target casts never split");
    {   // Only a real AoE has a "current cast" the others can lag behind. And the rule must not search the model for them.
        Copy roll; roll.abil = true; roll.self = 0;
        CHECK(ally_copy_fresh(roll));
        CHECK_EQ(roll.asked, 0);
        Copy single; single.isAoe = false; single.self = 0;
        CHECK(ally_copy_fresh(single));
    }

    SECTION("timers fresh : a copy on your current cast is fresh, one a re-sing missed is a laggard");
    {
        Copy c;                                   // frozen on your current expiry
        CHECK(ally_copy_fresh(c));
        c.exp = c.self - 600; CHECK(ally_copy_fresh(c));      // 10 s apart : still the same generation
        c.exp = c.self - 601; CHECK(!ally_copy_fresh(c));     // the re-sing missed them : the OLD, shorter cast
        Copy m; m.mirror = true; m.self = 0;      // just cast, your 0x063 has not landed
        CHECK(ally_copy_fresh(m));
    }

    SECTION("timers fresh : a song no longer on YOU is listed by name -- unless the game just pushed it out");
    {   // Victory March pushed off you by four new songs, still on Kaories : a laggard, or the family-collapsed status
        // (Honor + Victory March both 214) would draw a phantom "(AoE 2)".
        Copy gone; gone.self = 0;
        CHECK(!ally_copy_fresh(gone));
        // Reported 2026-09-10, replacing a rotation with Paeons : the song the game evicted from your set leaves YOUR
        // 0x063 first, and "you hold it, they do not" exploded it into one row per member for a second.
        // Mutation : `if (!selfExp && o.evictedRecently()) return true;` removed.
        Copy evicted = gone; evicted.evicted = true;
        CHECK(ally_copy_fresh(evicted));
    }

    SECTION("timers fresh : your timer list has not spoken about a cast newer than it");
    {   // Reported 2026-09-10, singing Paeons : right after a cast selfExp == 0 only means the 0x063 has not landed,
        // and reading that silence as "you do not hold it" scattered a fresh song into one row per member.
        // Mutation : `if (!selfExp && (int)(o.castMs() - o.timersStamp()) > 0) return true;` removed.
        Copy c; c.self = 0; c.cast = 9000; c.stamp = 8000;
        CHECK(ally_copy_fresh(c));
        c.stamp = 9500;                           // the list arrived AFTER the cast and does not carry it : it is gone
        CHECK(!ally_copy_fresh(c));
    }

    SECTION("timers fresh : right after a zone, an enhancing buff holds its group ; a song does not");
    {   // Your 0x063 reads empty for a few seconds while it repopulates. An enhancing buff persists across the zone, so
        // it must not flash into per-person rows ; songs are lost on a zone and stay laggard.
        // Mutation : `return o.zoneGrace() && !o.isSong();` -> `return o.zoneGrace();`.
        Copy protect; protect.self = 0; protect.grace = true; protect.song = false;
        CHECK(ally_copy_fresh(protect));
        Copy march = protect; march.song = true;
        CHECK(!ally_copy_fresh(march));
        protect.grace = false;
        CHECK(!ally_copy_fresh(protect));
    }

    SECTION("timers source : what YOU cast is always kept, and All keeps everything");
    {
        Timer mine; mine.who = mine.self;
        for (int f = TMSRC_MINE; f <= TMSRC_ALL; ++f) { mine.flt = f; CHECK(buff_source_keeps(mine)); }
        Timer other; other.flt = TMSRC_ALL; other.trust = true;
        CHECK(buff_source_keeps(other));
    }

    SECTION("timers source : a known caster -- players and trusts by the filter");
    {
        Timer t;                                     // a real player
        t.flt = TMSRC_MINE;    CHECK(!buff_source_keeps(t));
        t.flt = TMSRC_PLAYERS; CHECK(buff_source_keeps(t));
        t.flt = TMSRC_TRUSTS;  CHECK(!buff_source_keeps(t));
        t.trust = true;                              // a trust
        t.flt = TMSRC_PLAYERS; CHECK(!buff_source_keeps(t));
        t.flt = TMSRC_TRUSTS;  CHECK(buff_source_keeps(t));
    }

    SECTION("timers source : Food, Signet, Aftermath, Imagery are yours under any filter");
    {   // No job casts them, so no caster resolves and your job cannot "produce" them : without the exemption, "Mine only"
        // hid food. Mutation : `if (s.selfCarried()) return true;` removed.
        Timer food; food.who = 0; food.carried = true; food.players = false;
        food.flt = TMSRC_MINE; CHECK(buff_source_keeps(food));
        CHECK(timers_self_carried(251));             // Food
        CHECK(timers_self_carried(272) && timers_self_carried(270) && !timers_self_carried(273));   // Aftermath Lv.1-3, not the legacy generic
        CHECK(timers_self_carried(253) && timers_self_carried(512));   // Signet, Ionis
        CHECK(timers_self_carried(235) && timers_self_carried(243) && !timers_self_carried(244));   // Imagery
        CHECK(!timers_self_carried(33));             // Haste is somebody's cast
    }

    SECTION("timers source : a trust's stat mix is judged before the caster, which can still name you");
    {   // buffCaster_ is never cleared when a buff wears, so a stale attribution named YOU on a Monberaux STR boost and kept
        // it as "your own". Mutation : the foreignStatMix() test moved after the caster check.
        Timer mix; mix.who = mix.self; mix.mix = true;
        mix.flt = TMSRC_MINE;    CHECK(!buff_source_keeps(mix));
        mix.flt = TMSRC_PLAYERS; CHECK(!buff_source_keeps(mix));
        mix.flt = TMSRC_TRUSTS;  CHECK(buff_source_keeps(mix));
    }

    SECTION("timers source : an unknown caster is inferred from who COULD cast it");
    {
        Timer u; u.who = 0;
        u.produce = true; u.flt = TMSRC_MINE; CHECK(buff_source_keeps(u));     // your job makes it : probably yours
        u.produce = false; CHECK(!buff_source_keeps(u));
        u.flt = TMSRC_PLAYERS; u.players = false; CHECK(!buff_source_keeps(u)); // no player can make it
        u.players = true; CHECK(buff_source_keeps(u));
        u.flt = TMSRC_TRUSTS; u.trusts = false; CHECK(!buff_source_keeps(u));  // a player-only buff, under "+ trusts"
        u.trusts = true; CHECK(buff_source_keeps(u));
    }

    SECTION("timers band : yours, unknown, players by player, trusts last");
    {   // Unknown used to band as "mine", so food, gear and a failed parse sorted ABOVE your own live songs.
        // Mutation : `(caster == 0) ? 1` -> `(caster == 0) ? 0`.
        CHECK_EQ(self_row_band(0x100, 0x100, false, 0), 0);
        CHECK_EQ(self_row_band(0, 0x100, false, 99), 1);
        CHECK_EQ(self_row_band(0x200, 0x100, false, 2), 42);
        CHECK_EQ(self_row_band(0x300, 0x100, true, 3), 93);
        CHECK(self_row_band(0x300, 0x100, true, 1) > self_row_band(0x200, 0x100, false, 5));   // any trust after any player
    }

    SECTION("focus alert : a loss is a red OUT unless something says otherwise");
    {
        Watched w;
        CHECK_EQ(focus_alert_verdict(w), FA_ALERT);
        w.lost = 100000 - 90000;                                   // ninety seconds ago : Focus alone is permanent until re-cast
        CHECK_EQ(focus_alert_verdict(w), FA_ALERT);
    }

    SECTION("focus alert : muted, present, zoning or unknown -- no loss is running");
    {   // Each of these clears the loss stamp. NO DATA is the costly one : buffs_for() == null for an alliance member or an
        // out-of-zone ally used to fire a PERMANENT red OUT. Mutation : `if (!e.listReady()) return FA_NO_DATA;` removed.
        Watched w; w.isMuted = true; w.present = true;
        CHECK_EQ(focus_alert_verdict(w), FA_MUTED);               // muted wins, and the entry is never dropped from here
        Watched up; up.present = true;   CHECK_EQ(focus_alert_verdict(up), FA_UP);
        Watched z; z.grace = true;       CHECK_EQ(focus_alert_verdict(z), FA_ZONE_GRACE);
        Watched n; n.ready = false;      CHECK_EQ(focus_alert_verdict(n), FA_NO_DATA);
        CHECK(focus_alert_clears_loss(FA_NO_DATA) && !focus_alert_clears_loss(FA_HOLD_EXPIRED) && !focus_alert_clears_loss(FA_ALERT));
    }

    SECTION("focus alert : switching 'my buffs on allies' off silences an ally entry before anything else");
    {   // Mutation : `if (e.allyRowsOff()) return FA_CATEGORY_OFF;` removed.
        Watched w; w.rowsOff = true;
        CHECK_EQ(focus_alert_verdict(w), FA_CATEGORY_OFF);
    }

    SECTION("focus alert : Hidden+Focus holds the alert for its hold time, measured from the loss");
    {   // Mutation : `e.lostMs() ? e.lostMs() : e.nowMs()` -> `e.lostMs()` -- a loss seen THIS frame (stamp 0) would read
        // as lost since the clock began, and a Hidden+Focus buff would never draw its alert at all.
        Watched w; w.hid = true; w.hold = 60;
        CHECK_EQ(focus_alert_verdict(w), FA_ALERT);                 // lost just now
        w.lost = w.now - 59000; CHECK_EQ(focus_alert_verdict(w), FA_ALERT);
        w.lost = w.now - 61000; CHECK_EQ(focus_alert_verdict(w), FA_HOLD_EXPIRED);
    }

    SECTION("focus alert : slots refilled, the unrecoverable fifth song, a deliberate swap -- silent, in that order");
    {   // Silent is not forgotten : the prune decides that, and freeing the entry here made the red row blink at 60 Hz
        // (measured 2026-09-11). Mutation : `if (e.slotsFilled()) return FA_SLOTS_FILLED;` removed.
        Watched w; w.filled = true;
        CHECK_EQ(focus_alert_verdict(w), FA_SLOTS_FILLED);
        Watched u; u.unrec = true; u.repl = true; CHECK_EQ(focus_alert_verdict(u), FA_UNRECOVERABLE);
        Watched r; r.repl = true; r.geo = true;   CHECK_EQ(focus_alert_verdict(r), FA_REPLACED);
        Watched g; g.geo = true;                  CHECK_EQ(focus_alert_verdict(g), FA_GEO_REPLACED);
        Watched rn; rn.rune = true;               CHECK_EQ(focus_alert_verdict(rn), FA_RUNE_REPLACED);
    }

    SECTION("focus prune : an alliance member cannot be judged, so it is not watched");
    {   // Their 0x076 never arrives : listReady stayed false forever and the entry became immortal, filling the monitor.
        // Mutation : `e.partyOrder() <= 5` -> `e.partyOrder() <= 17`.
        Watched w; w.present = true;
        w.order = 5; CHECK_EQ(focus_prune_verdict(w), FP_KEEP);
        w.order = 6; CHECK_EQ(focus_prune_verdict(w), FP_DROP_GONE_OR_OFF);
        w.grace = true; CHECK_EQ(focus_prune_verdict(w), FP_KEEP);   // mid-zone the roster is unstable : keep
        Watched you; you.isSelf = true; you.order = 99; you.present = true; CHECK_EQ(focus_prune_verdict(you), FP_KEEP);
        Watched off; off.focus = false; CHECK_EQ(focus_prune_verdict(off), FP_DROP_GONE_OR_OFF);
    }

    SECTION("focus prune : a song on an ally outside your zone is cleaned, past the grace, songs only");
    {   // Mutation : `!e.zoneGrace() && ` removed from the song-offzone test.
        Watched w; w.song = true; w.away = true;
        CHECK_EQ(focus_prune_verdict(w), FP_DROP_SONG_OFFZONE);
        w.grace = true; CHECK(focus_prune_verdict(w) != FP_DROP_SONG_OFFZONE);
        Watched haste; haste.away = true; CHECK_EQ(focus_prune_verdict(haste), FP_KEEP);
    }

    SECTION("focus prune : after a zone, decide only once the grace is over AND the list is back");
    {   // Deciding during the grace read the stale pre-zone list : false survivors, then a red OUT for every song.
        // Mutation : `if (e.zoneGrace() || !e.listReady()) return FP_KEEP;` -> `if (e.zoneGrace()) return FP_KEEP;`.
        Watched w; w.check = true;
        w.ready = false; CHECK_EQ(focus_prune_verdict(w), FP_KEEP);
        w.ready = true;  CHECK_EQ(focus_prune_verdict(w), FP_DROP_ZONE_CASUALTY);   // the game dropped it on zoning : no alert
        w.present = true; CHECK_EQ(focus_prune_verdict(w), FP_KEEP_SURVIVED_ZONE);
    }

    SECTION("focus prune : only a RUNNING loss is freed, and a plain Focus loss never is");
    {   // Mutation : `if (e.lostMs() && !e.has())` -> `if (!e.has())`.
        Watched w; w.unrec = true;                                   // lost this frame : no stamp yet -> keep (the emit decides)
        CHECK_EQ(focus_prune_verdict(w), FP_KEEP);
        w.lost = 1; CHECK_EQ(focus_prune_verdict(w), FP_DROP_UNRECOVERABLE);
        Watched f; f.lost = 10000;                                   // Focus alone : lost ninety seconds ago, still watched
        CHECK_EQ(focus_prune_verdict(f), FP_KEEP);
        f.hid = true; CHECK_EQ(focus_prune_verdict(f), FP_DROP_HOLD_EXPIRED);
        Watched rn; rn.rune = true; CHECK_EQ(focus_prune_verdict(rn), FP_KEEP);      // lost this frame : the emit decides first
        rn.lost = 1; CHECK_EQ(focus_prune_verdict(rn), FP_DROP_RUNE_REPLACED);
    }

    SECTION("focus swap : a rune a newer rune pushed out is not a loss ; one that ran out with a slot free is");
    {   // Reported 2026-09-14 on PLD/RUN and RUN : every rune changed raised a permanent red OUT.
        // Mutations : `runesUp >= cap` -> `runesUp > 0` ; the cap's `mjob == RUN ? mlvl` branch dropped ; `st <= 530` -> `st < 530`.
        CHECK_EQ(focus_rune_cap(22, 99, 7, 49), 3);                  // RUN main
        CHECK_EQ(focus_rune_cap(7, 99, 22, 49), 2);                  // RUN sub under PLD
        CHECK_EQ(focus_rune_cap(7, 99, 22, 59), 2);                  // Master Level sub : still 2
        CHECK_EQ(focus_rune_cap(22, 40, 7, 20), 2);                  // level-synced RUN main
        CHECK_EQ(focus_rune_cap(22, 4, 7, 2), 0);
        CHECK_EQ(focus_rune_cap(7, 99, 3, 49), 0);                   // no RUN at all
        CHECK(focus_rune_status(523) && focus_rune_status(530) && !focus_rune_status(522) && !focus_rune_status(531));
        CHECK(focus_rune_replaced(true, 523, 2, 2, 0));              // PLD/RUN, two runes still on you : Ignis was pushed out
        CHECK(!focus_rune_replaced(true, 523, 2, 1, 0));             // a slot is free : it expired, keep the OUT
        CHECK(focus_rune_replaced(true, 523, 2, 1, 1));              // ...unless a NEWER lost rune is the one that slot speaks for
        CHECK(!focus_rune_replaced(true, 523, 2, 0, 1));             // Ignis x2 both run out : the older is still one of the last two
        CHECK(focus_rune_replaced(true, 523, 2, 0, 2));
        CHECK(!focus_rune_replaced(true, 523, 3, 2, 0));             // RUN main, third slot free : keep the OUT
        CHECK(focus_rune_replaced(true, 530, 3, 3, 0));
        CHECK(!focus_rune_replaced(true, 523, 0, 3, 5));             // cap unknown (not a RUN) : never silence
        CHECK(!focus_rune_replaced(false, 523, 2, 2, 0));            // an ally entry is never a rune of yours
        CHECK(!focus_rune_replaced(true, 33, 2, 2, 0));              // Haste is not a rune
    }

    SECTION("focus rune copies : each rune placed is its own entry, matched by its expiry ; newer losses counted once");
    {   // Reported 2026-09-14 : two Ignis ran out and ONE Ignis OUT was drawn. Mutations : focus_rune_match without the
        // `seen` test (both copies claim the same entry) ; `d == 0 && i > q` -> `d >= 0`.
        struct M { unsigned char self, seen; unsigned short status; unsigned rank; };
        M fm[] = { { 1, 0, 523, 1000 }, { 1, 0, 523, 1600 }, { 1, 0, 525, 2200 }, { 0, 0, 523, 1600 } };
        CHECK_EQ(focus_rune_match(fm, 4, 523, 1000), 0);
        CHECK_EQ(focus_rune_match(fm, 4, 523, 1601), 1);             // a tick of drift still finds its own copy
        CHECK_EQ(focus_rune_match(fm, 4, 523, 5000), -1);            // a new placement
        fm[1].seen = 1; CHECK_EQ(focus_rune_match(fm, 4, 523, 1601), -1);   // already claimed this frame : the other copy is too far
        fm[1].seen = 0;
        bool lost[] = { true, true, false, true };
        CHECK_EQ(focus_rune_newer_lost(fm, lost, 4, 0), 1);          // the later Ignis ; the ally entry never counts
        CHECK_EQ(focus_rune_newer_lost(fm, lost, 4, 1), 0);
        M tie[] = { { 1, 0, 523, 700 }, { 1, 0, 524, 700 } }; bool both[] = { true, true };
        CHECK_EQ(focus_rune_newer_lost(tie, both, 2, 0) + focus_rune_newer_lost(tie, both, 2, 1), 1);
    }

    SECTION("self timer group : two songs on one status fold by SPELL, never by the first status match");
    {   // Captured : four songs in game, three rows drawn -- Minuet IV and V (both status 198) folded into the FIRST group
        // found, and its countdown overwrote the survivor's. Mutation : the status fallbacks without `sameStatus < 2`.
        static const Grp g[] = { { 397, 198, true, true }, { 398, 198, true, true } };
        CHECK_EQ(self_timer_group(Groups{ g }, 2, 398, 198, 2), 1);
        CHECK_EQ(self_timer_group(Groups{ g }, 2, 0, 198, 2), -1);    // spell unresolved and two timers : no guess
        CHECK_EQ(self_timer_group(Groups{ g }, 2, 0, 198, 1), 0);     // one timer on the status : the status is enough
    }

    SECTION("self timer group : the AoE generation wins over a single-target one of the same spell ; laggards never");
    {   // Your own copy is the AoE cast that also hit you. Mutation : the first loop (spell + fresh + aoe) removed.
        static const Grp g[] = { { 47, 40, true, false }, { 47, 40, true, true }, { 47, 40, false, true } };
        CHECK_EQ(self_timer_group(Groups{ g }, 3, 47, 40, 1), 1);
        static const Grp lagOnly[] = { { 47, 40, false, true } };
        CHECK_EQ(self_timer_group(Groups{ lagOnly }, 1, 47, 40, 1), -1);
    }

    SECTION("self timer fold : after a reload the 0x076 is empty -- the group you just cast still re-forms");
    {   // Captured : countHas=1 while the group had two allies and aoe=1, so "(AoE N)" never re-formed after a recast.
        // Mutation : the `est > effHas` floor removed.
        Fold f; f.carriers = 1;
        CHECK(self_timer_folds(f));
    }

    SECTION("self timer fold : with a laggard of the same spell, only the fresh bucket counts");
    {   // The 0x076 cannot tell an old copy from a fresh one, so countHas would re-absorb the laggard. A solo re-sing is
        // then ONE carrier : your own row, not a fold. Mutation : the laggard branch -> countHas.
        Fold solo; solo.lag = true; solo.allies = 0; solo.carriers = 4;
        CHECK(!self_timer_folds(solo));
        CHECK_EQ(solo.askedCount, 0);                              // and the 0x076 was not even consulted
        Fold two = solo; two.allies = 1;
        CHECK(self_timer_folds(two));                                // you + one ally re-hit : a real fresh group
    }

    SECTION("self timer fold : only into a REAL AoE, and never behind a filter that hides the ally copy");
    {   // Folding into a single-target group made your own buff vanish (the per-ally branch only draws allies) ; folding
        // behind an ally-Hidden setting hid a self buff you track. Mutation : `&& s.groupAoe()` removed.
        Fold single; single.isAoe = false;
        CHECK(!self_timer_folds(single));
        Fold hidden; hidden.hides = true;
        CHECK(!self_timer_folds(hidden));
        Fold none; none.have = false;
        CHECK(!self_timer_folds(none));
    }

    SECTION("focus swap : a Pianissimo song on the same person took the slot -- no red OUT");
    {   // Mutation : `&& !s.otherAoe(i)` removed -- an AoE song re-stamps every member at each cast, and the window would
        // then stay open over your whole rotation, hiding a real dispel on anyone you sing to.
        static const Cast pianissimo[] = { { 0x200, 420, 97000, false } };
        Swap w; w.c = pianissimo; w.n = 1;
        CHECK(focus_song_replaced(w));
        static const Cast aoeSong[] = { { 0x200, 420, 97000, true } };
        Swap a2; a2.c = aoeSong; a2.n = 1;
        CHECK(!focus_song_replaced(a2));
        Swap late = w; late.now = 97000 + 6000;                       // six seconds : a later loss is a real one
        CHECK(!focus_song_replaced(late));
        Swap other = w; other.tgt = 0x300;                            // somebody else's slot
        CHECK(!focus_song_replaced(other));
        Swap you = w; you.isSelf = true;                               // a Pianissimo song never lands on you
        CHECK(!focus_song_replaced(you));
        Swap haste = w; haste.sp = 57;                                 // not a song : never a slot swap
        haste.evict = true;
        CHECK(!focus_song_replaced(haste));
    }

    SECTION("focus swap : the song the game pushed out to fit a new one is not a loss");
    {   // Mutation : `return s.evicted();` -> `return false;`.
        Swap w; w.evict = true;
        CHECK(focus_song_replaced(w));
    }

    SECTION("focus swap : an Indi- you replaced with another Indi- is not a loss ; a real Refresh still is");
    {   // Reported : Indi-Fury -> Indi-Refresh -> Indi-Regen left Fury and Refresh stuck OUT.
        // Mutations : self branch `aura && aura != s.status()` -> `aura != 0` ; the Indi- entry test without the spell.
        Swap fury; fury.isSelf = true; fury.sp = 810; fury.st = 542; fury.aura = 541;
        CHECK(focus_geo_replaced(fury));
        Swap still = fury; still.aura = 542;                           // the one you carry now : it may alert
        CHECK(!focus_geo_replaced(still));
        Swap none = fury; none.aura = 0;                               // no aura at all : a real loss
        CHECK(!focus_geo_replaced(none));
        Swap indiRefresh = fury; indiRefresh.st = 541; indiRefresh.aura = 539;   // shared status 541, recognised by its SPELL
        CHECK(focus_geo_replaced(indiRefresh));
        Swap refresh; refresh.isSelf = true; refresh.sp = 109; refresh.st = 43; refresh.aura = 539;   // a plain Refresh
        CHECK(!focus_geo_replaced(refresh));
    }

    SECTION("focus swap : an Entrusted Indi- on an ally is replaced only on evidence of a newer Indi- there");
    {
        static const Cast newer[] = { { 0x200, 820, 98000, false } };
        Swap w; w.sp = 810; w.st = 542; w.c = newer; w.n = 1;
        CHECK(focus_geo_replaced(w));
        Swap stale = w; stale.now = 98000 + 7000;
        CHECK(!focus_geo_replaced(stale));
        Swap noEvidence = w; noEvidence.n = 0;
        CHECK(!focus_geo_replaced(noEvidence));
    }

    SECTION("self timer countdown : held at 0:00 while the game still lists the buff, gone once it does not");
    {   // Our timer runs ~2 s ahead of the client : dropping the row at 0 left a HOLE before the red OUT alert.
        // Mutation : `if (rem <= 0) { if (!s.meHas()) return -1; rem = 0; }` -> `if (rem <= 0) return -1;`.
        Mine m; m.rem = 0;
        CHECK_EQ(self_timer_countdown(m), 0);
        m.rem = -2; CHECK_EQ(self_timer_countdown(m), 0);
        m.have = false; CHECK_EQ(self_timer_countdown(m), -1);
        Mine live; CHECK_EQ(self_timer_countdown(live), 180);
    }

    SECTION("self timer countdown : no row for the permanent sentinel, a debuff, or an absurd timer -- Signet excepted");
    {   // Mutation : `&& !s.selfCarried()` removed from the 6 h cap (San d'Oria base Signet runs 13 h).
        Mine perm; perm.perm = true; CHECK_EQ(self_timer_countdown(perm), -1);
        Mine blind; blind.debuff = true; CHECK_EQ(self_timer_countdown(blind), -1);
        Mine absurd; absurd.rem = 6 * 3600 + 1; CHECK_EQ(self_timer_countdown(absurd), -1);
        Mine signet = absurd; signet.carried = true; CHECK_EQ(self_timer_countdown(signet), 6 * 3600 + 1);
    }

    SECTION("self timer shown : Hidden hides it, Hidden+Focus surfaces it under the warn threshold");
    {   // Mutation : the Hidden+Focus exception removed -- `if (s.hidden()) return false;`.
        Mine m; m.hid = true;
        CHECK(!self_timer_shown(m));
        m.focus = true; CHECK(!self_timer_shown(m));                 // 180 s left, the warn threshold is 60
        m.rem = 59; CHECK(self_timer_shown(m));                      // expiring : the alert surfaces
        m.rem = 60; CHECK(!self_timer_shown(m));
    }

    SECTION("self timer shown : GEO aura pulses, the source filter, and a stale 0x063 entry");
    {   // Mutations : the 542..556 / 612 range test removed ; `if (!s.meHas()) return false;` removed (a Corsair roll the
        // game already replaced stayed on screen from the 0x063).
        Mine boost; boost.st = 548; CHECK(!self_timer_shown(boost));
        Mine colure; colure.st = 612; CHECK(!self_timer_shown(colure));
        Mine tracked; tracked.aura = true; CHECK(!self_timer_shown(tracked));
        Mine filtered; filtered.keeps = false; CHECK(!self_timer_shown(filtered));
        Mine stale; stale.have = false; CHECK(!self_timer_shown(stale));
        Mine normal; CHECK(self_timer_shown(normal));
    }
}
