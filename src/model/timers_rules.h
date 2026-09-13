// timers_rules.h -- the decisions of the Timers builder that are about ONE row's source, each as a function of its inputs.
//
// Cut out of model/timers_build.cpp on 2026-09-13 (lot D, step 4). The comments are the ones the builder carried :
// every branch below is a defect that reached a player, and the text says which. tests/t_timersrules.cpp pins each
// rule to its inputs ; tests/t_timers.cpp runs them inside whole scenarios.
//
// LAZY INPUTS. These rules run for every ally entry, for every group, every frame, and several of their inputs are
// searches of the model (your timer for a spell, the eviction notes). The builder's code evaluated them only on the
// branch that needed them, and a rule taking a filled-in struct would have evaluated all of them for every call. So a
// rule takes a SOURCE -- any type with the named member functions -- and calls each one where the old code read it.
// The builder passes an adapter over the model ; a test passes plain values.
#pragma once
#include "model/ui_config.h"   // TMSRC_*

namespace aio {

// ---- FRESH vs LAGGARD : does this ally copy of a buff track YOUR CURRENT cast of it ? --------------------------
//
// A song copy on an ally is FRESH only when it tracks YOUR CURRENT cast of THIS SPELL : still mirroring your live
// self timer (just re-sung), or its frozen expTick matches your current self expiry for this spell. Otherwise it is
// a LAGGARD -> a named per-ally row on its own timer, never folded into a self AoE :
//   - a re-sing MISSED the member : it keeps the OLD expTick (older/shorter cast).
//   - you no longer hold THIS SPELL yourself (selfExp==0) : e.g. Victory March was pushed off you by 4 new songs
//     but is still on Kaories. This case is why selfExp==0 must be LAGGARD, not fresh : status ids family-collapse
//     (Honor March + Victory March are BOTH 214), so countHas(214) would count YOUR Honor March into Victory
//     March's group and draw a phantom "(AoE 2)". Songs are self-centred -- a real current AoE song is always on
//     you (selfExp!=0) -- so selfExp==0 reliably means "not a self AoE, list the ally who still carries it".
// Only REAL AoE spells split (rolls/single-target keep their existing one-row-per-ally path). Shared by the
// pass-1 grouping and the pass-3 per-ally emit so the two ALWAYS agree on which members are laggards.
//
// Source : isAbil() aoe() mirrorSelf() expTick() castMs()              -- the entry itself
//          selfExp()        your 0x063 expiry for this status AND spell, 0 = you hold no timer for it
//          evictedRecently() the model named this song as the one the game pushed out of YOUR set, < 6 s ago
//          timersStamp()    when your 0x063 timer list last arrived
//          zoneGrace()      the post-zone repop window
//          isSong()         the spell is a BRD song
template <class Src>
inline bool ally_copy_fresh(const Src& o) {
    if (o.isAbil() || !o.aoe()) return true;
    if (o.mirrorSelf()) return true;   // just cast (< 2s, still mirroring your live self timer) -> fresh, even before your 0x063 self timer has landed (no post-cast flicker)
    const unsigned selfExp = o.selfExp();
    // A SONG THE GAME JUST PUSHED OUT OF YOUR SET IS NOT A LAGGARD. When a new song lands at the cap,
    // your own 0x063 drops the victim FIRST -- the allies keep their copy for another moment -- and
    // "you hold it, they do not" is exactly the shape of a re-cast that missed people. So an evicted
    // song exploded into one named row per member for a second, then vanished as the 0x076 caught up.
    // Reported 2026-09-10, replacing a rotation with Paeons. The model named that victim at cast time,
    // so we can simply ask: on its way out, and it stays ONE row until the prune takes it.
    if (!selfExp && o.evictedRecently()) return true;
    // YOUR LIST HAS NOT SPOKEN ABOUT THIS CAST YET. selfExp == 0 normally means "you do not hold it",
    // but right after a cast it only means the 0x063 has not landed. Treating that silence as an answer
    // is what made a freshly sung AoE song scatter into one row per member for a moment before pulling
    // back into its group -- reported 2026-09-10, singing Paeons over a rotation.
    //
    // mirrorSelf covers the same window and was meant to prevent exactly this, but it lapses at 2 s
    // whether or not your timer list has arrived. So ask the question that has an answer: is this cast
    // NEWER than anything the 0x063 has told us? Then it has said nothing about it. Same rule the prune
    // and the post-zone check already obey -- evidence older than the event cannot rule on it -- and no
    // new delay to tune, which is what every previous attempt at this class of bug reached for.
    if (!selfExp && (int)(o.castMs() - o.timersStamp()) > 0) return true;
    if (!selfExp) return o.zoneGrace() && !o.isSong();   // normally selfExp==0 -> you don't hold this -> laggard. EXCEPT the post-zone repop window : your 0x063 self timer reads 0 for a few seconds while it repopulates, but an ENHANCING buff PERSISTS across a zone -> hold it FRESH (grouped, on the pre-zone estimate) during the grace so it never flashes per-person ; the re-align snaps it to the real self timer the instant it lands. Songs (lost on zone) stay laggard.
    int d = (int)(o.expTick() - selfExp); if (d < 0) d = -d;
    return d <= 600;                 // 600 ticks = 10s : casts more than 10s apart are distinct generations
}


// ---- SELF-CARRIED buffs -------------------------------------------------------------------------------------------
// SELF-CARRIED buffs : Food / Aftermath / conquest (Signet, Sanction, Sigil, Ionis) / synthesis Imagery. NO job
// CASTS these, so buff_caster_for can't attribute them and self_can_produce_buff says no -> the "buff source" filter
// (srcKeeps) would classify them as "not yours" and hide them under anything but "All". But they ARE yours (you
// carry them), not someone's buff cast ON you, so they must be exempt from the source filter -- their family-filter
// toggle is their only control. Status ids mirror the EXTRA_FAM list in scripts/gen_job_track.py (keep in sync).
inline bool timers_self_carried(unsigned st) {
    return st == 251                                            // Food
        || (st >= 270 && st <= 272)                            // Aftermath: Lv.1 / Lv.2 / Lv.3 (3 tiers ; 273 generic is legacy)
        || st == 253 || st == 256 || st == 268 || st == 512    // Signet / Sanction / Sigil / Ionis
        || (st >= 235 && st <= 243);                           // synthesis Imagery (Fishing .. Cooking)
}

// ---- THE BUFF-SOURCE FILTER : does "Mine only / + players / + trusts / All" keep this timer ? ---------------------
//
// ONE definition of "does the buff-source filter keep this timer", shared by the row emit AND by the FOCUS
// monitor. They used to disagree: the emit applied the filter, the monitor did not -- so under "Mine only" a
// party WHM's Haste never warned you it was about to expire, then screamed HASTE OUT in red the moment it did.
// Half a feature reachable, half not. Same verdict for both, so they are reachable or unreachable together.
//
// Source : filter()          UiConfig::tmBuffSrc (TMSRC_*)
//          selfCarried()     timers_self_carried(status)
//          foreignStatMix()  a trust/chemist STR..CHR boost you did not cast (PartyState::is_foreign_stat_mix)
//          caster() me()     who cast this timer (0 = unknown) ; your id
//          isTrust(id)       ; selfCanProduce() : your current job can make this status
//          sourceJobs(p, t)  which kinds of caster can produce it at all (players / trusts)
template <class Src>
inline bool buff_source_keeps(const Src& s) {
    const int filter = s.filter();
    if (filter == TMSRC_ALL) return true;
    if (s.selfCarried()) return true;   // Food/Signet/Craft/Aftermath : self-carried, no external caster -> always yours ; the family-filter toggle is their sole control (else "Mine only" hides them despite being set to Show)
    // A trust/chemist multi-stat MIX boost (a STR..CHR you did NOT cast, co-expiring with a sibling boost) is
    // decided FIRST -- ahead of the caster lookup -- because a stale buffCaster_ can still name YOU on it (the
    // attribution is never cleared when the buff wears off), which would otherwise keep it as "your own".
    if (s.foreignStatMix())
        return (filter == TMSRC_TRUSTS);                                //   "me+trusts" keeps it ; "mine"/"players" hide it
    const unsigned caster = s.caster(), me = s.me();
    if (caster != 0 && caster == me) return true;                       // your own -> always kept
    if (caster != 0) {
        const bool trust = s.isTrust(caster);
        if (filter == TMSRC_MINE) return false;
        if (filter == TMSRC_PLAYERS && trust)  return false;
        if (filter == TMSRC_TRUSTS  && !trust) return false;
        return true;
    }
    if (s.selfCanProduce()) return true;   // unknown but your job can make it
    if (filter == TMSRC_MINE) return false;
    bool ph = false, th = false; s.sourceJobs(ph, th);
    if (filter == TMSRC_PLAYERS && !ph) return false;
    if (filter == TMSRC_TRUSTS  && ph && !th) return false;
    return true;
}

// ---- THE BAND of one of YOUR buff rows (the first key of the sort, model/timers_sort.h) ---------------------------
//
// For BANDING, unknown is its own thing. The source filter already treats caster==0 as "infer", but the
// sort treated it as "mine", so every unattributed row -- food, gear, a 3000-TP boost we failed to parse --
// sorted ABOVE your own live songs, which is the exact complaint this banding was added to fix.
// Display tiers (top -> bottom) : (1) YOUR buffs 0-1, (3) buffs YOU cast on allies 10-28, (2) buffs a
// PLAYER put on you -- GROUPED BY that player (party position) 40-57, (4) TRUSTS last 90-107.
//
// `partyOrder` = the caster's roster position (PartyState::party_order).
inline int self_row_band(unsigned caster, unsigned me, bool casterIsTrust, int partyOrder) {
    return (caster == me)    ? 0                    // your own buffs -> very top
         : (caster == 0)     ? 1                    // unknown caster (food/gear) = probably yours
         : casterIsTrust     ? (90 + partyOrder)    // a trust's buff on you -> LAST, grouped by trust
                             : (40 + partyOrder);   // a real player's buff on you (Kaories' rolls...) -> grouped BY that player
}

// ---- WHICH ally group one of YOUR timers belongs to (-1 = none : it keeps its own row) -----------------------------
//
// WHICH ally group does this self buff belong to ? Groups are built per SPELL, so matching them by STATUS
// collapsed two different songs that share one status -- Minuet IV + Minuet V (both status 198), Honor March
// + Victory March (both 214) -- into the FIRST group found. Both folded there, one row was emitted for two
// songs, and grp[].rem then overwrote the survivor's countdown (captured : 4 songs in game, 3 rows drawn).
// So resolve the SPELL first and match on it ; fall back to the status only when this status carries a
// single timer, where the ambiguity cannot arise.
// Fold ONLY into a FRESH group : your self buff IS the current cast, never a laggard row.
// Since groups are keyed by delivery too, one spell can now have TWO fresh groups -- an AoE
// generation and a single-target one. Your own copy belongs to the AoE : that is the cast that also
// hit you and whose exact 0x063 timer the group displays. Prefer it ; fall back to a single-target
// group only when no AoE one exists (you re-cast on one ally without touching yourself).
//
// Groups : spell(k) status(k) fresh(k) aoe(k) for k < n. `sameStatus` = how many of YOUR timers carry this status.
template <class Groups>
inline int self_timer_group(const Groups& g, int n, unsigned spell, unsigned status, int sameStatus) {
    int gi = -1;
    for (int k = 0; k < n; ++k) if (g.spell(k) == spell && g.fresh(k) && g.aoe(k)) { gi = k; break; }
    if (gi < 0) for (int k = 0; k < n; ++k) if (g.spell(k) == spell && g.fresh(k)) { gi = k; break; }
    if (gi < 0 && sameStatus < 2) for (int k = 0; k < n; ++k) if (g.status(k) == status && g.fresh(k) && g.aoe(k)) { gi = k; break; }
    if (gi < 0 && sameStatus < 2) for (int k = 0; k < n; ++k) if (g.status(k) == status && g.fresh(k)) { gi = k; break; }
    return gi;
}

// ---- does YOUR timer fold into that group's "(AoE N)" row instead of drawing its own ? ---------------------------
//
// no fresh group (you re-sang on yourself only, the ally is a laggard) -> gi stays -1 : your buff emits as its
// own row and the laggard group draws separately, which is exactly the split.
// Fold your own copy into the ally row ONLY when that row will actually be a grouped "(AoE N)" line -- a real
// AoE (the "group ally buffs" setting that once forced it is gone, 2026-09-12). A single-target spell you also put on yourself must stay a
// SELF row -- pass 3's per-ally branch only emits allies, so folding here would make your own buff vanish.
// Likewise if the ALLY scope hides this status (and it's not an expiring focus), pass 3 drops the group -> don't
// fold, or a self-Tracked buff would vanish behind an ally-Hidden setting.
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
//
// Source : haveGroup() groupAllies() groupAoe()   the group self_timer_group picked
//          hasLaggard()   a LAGGARD group of the same spell exists
//          meHas()        you carry the status (your memory list) ; countHas() carriers per the 0x076, you included
//          allyHides()    the family filter hides the ally copy (and it is not an expiring Focus)
template <class Src>
inline bool self_timer_folds(const Src& s) {
    int effHas;
    if (s.hasLaggard()) { effHas = (s.haveGroup() ? s.groupAllies() : 0) + (s.meHas() ? 1 : 0); }   // split : fresh members only (you + allies you re-hit) ; solo re-sing -> 1 -> no fold, own row
    else { effHas = s.countHas(); if (s.haveGroup()) { const int est = s.groupAllies() + (s.meHas() ? 1 : 0); if (est > effHas) effHas = est; } }
    return (s.haveGroup() && effHas >= 2 && s.groupAoe() && !s.allyHides());   // your own row folds into a group only when that group is a REAL AoE
}

// ---- YOUR 0x063 timer : what it counts down, or nothing (-1) ------------------------------------------------------
//
// Our timer runs ~2s AHEAD of the client (measured): at rem 0 the game still shows the icon for about
// two more seconds. Dropping the row at 0 while the red OUT alert only fires once the buff really
// leaves the list left a visible HOLE between the two. Hold the row at 0:00 for as long as the buff is
// genuinely still on you, so the hand-off row -> alert is seamless. Only for a buff we still hold:
// meHas is authoritative now that an empty list is distinguished from no data.
// DEBUFFS (Blind / Poison / Slow / Dia / Bio...) leak into the 0x063 self-buff list -- they are NOT buffs,
// so never in the Duration column (they'll get their own detachable column). Dropped for everyone.
//
// Source : permanent()  the client's FFXI_EXPIRY_PERMANENT sentinel ; remSec()  the countdown, ceil-ed like the client
//          selfCarried() (timers_self_carried) ; meHas()  you carry it per your memory list ; isDebuff()
template <class Src>
inline int self_timer_countdown(const Src& s) {
    if (s.permanent()) return -1;                           // client's "permanent" sentinel -> it draws no countdown, nor do we
    int rem = s.remSec();
    if (rem > 6 * 3600 && !s.selfCarried()) return -1;     // 6h cap drops garbage/absurd timers -- EXCEPT self-carried area buffs (San d'Oria base Signet = 13h)
    if (rem <= 0) { if (!s.meHas()) return -1; rem = 0; }   // held at 0:00 while the game still lists it : no hole before the OUT alert
    if (s.isDebuff()) return -1;                            // debuffs leak into the 0x063 : never in the Duration column
    return rem;
}

// ---- YOUR 0x063 timer : shown, or filtered out -------------------------------------------------------------------
//
// Buff filter : JOB-AGNOSTIC, keyed by STATUS (the family filter). Hidden -> drop the row, UNLESS it is
// Hidden+Focus and expiring (surface it under the warn threshold as the alert).
// GEO aura noise : the geomancy effect status (542-556 Boosts) and "Colure Active" (612) pulse every ~3s
// in 0x063 ; hide them (the Indi- YOU carry is redrawn as a stable computed row below).
//
// Source : status() remSec() ; hidden() focusOn() warnSec()   the family filter and its Hidden+Focus exception
//          geoAuraTracked()   the model redraws this GEO aura as its own stable row
//          sourceKeeps()      buff_source_keeps for this timer ; meHas()
template <class Src>
inline bool self_timer_shown(const Src& s) {
    if (s.hidden() && !(s.focusOn() && s.remSec() < s.warnSec())) return false;   // Hidden, unless Hidden+Focus and expiring
    if (s.geoAuraTracked()) return false;
    const unsigned st = s.status();
    if ((st >= 542 && st <= 556) || st == 612) return false;   // geomancy Boosts / Colure Active : a 3 s pulse, never a row
    if (!s.sourceKeeps()) return false;                      // buff-SOURCE filter (shared with the FOCUS monitor)
    if (!s.meHas()) return false;                            // a stale 0x063 entry the game already removed (a replaced roll)
    return true;
}

} // namespace aio
