// focus_rules.h -- the FOCUS monitor's three judgements: when a hand-mute is lifted, when a muted entry is
// forgotten, and when a lost song must NOT raise a red alert.
//
// Extracted from hud_timers.cpp on 2026-09-09 (element 4 of docs/audits/plan-harnais-2026-09-09.md). The
// monitor is the piece of Timers a PERSON has to be able to correct -- it is what //aio out and //aio in act
// on -- and its rules had been written three times over three incidents. Behind a test they can be read.
#pragma once

namespace aio {

// ---- 1. a hand-mute, and what ends it -----------------------------------------------------------------------
//
// //aio out silences ONE cast, not a spell. The correction that follows is almost always another cast of the
// same buff, often on the very person you took off -- and Haste, Refresh and Regen OVERWRITE themselves rather
// than lapsing, so the entry is never starved and would stay hidden for as long as you kept the buff up. That
// was the defect: a row removed by hand never came back.
//
// So a mute remembers WHICH cast it silenced, and a strictly NEWER cast lifts it. Strictly newer, never merely
// "different": when one of a same-status pair is pruned the maximum goes DOWN, and a smaller number is not a
// new cast. A muted entry that nothing refreshed this frame is forgotten -- you silenced one mistake, not the
// spell, so a later deliberate cast starts a fresh, watched entry.
enum FocusMute { FOCUS_KEEP = 0, FOCUS_LIFT, FOCUS_FORGET };

inline FocusMute focus_mute_verdict(bool muted, bool seenThisFrame, unsigned castRef, unsigned muteRef) {
    if (!muted) return FOCUS_KEEP;                       // a watched entry must outlive its buff to raise the OUT
    if (!seenThisFrame) return FOCUS_FORGET;             // nothing feeds it any more
    if (castRef && (int)(castRef - muteRef) > 0) return FOCUS_LIFT;
    return FOCUS_KEEP;
}

// ---- 2 and 3 : the learned cap and the unrecoverable fifth song -- MOVED to model/song_slots.h -------
//
// clarion_learn() took the cap as a high-water mark of observed song counts. That is structurally wrong:
// the limit follows the equipped INSTRUMENT, so the mark keeps the maximum reached under a Daurdabla long
// after the swap -- and a plugin reload or a job change reset it to 1, from where it silenced every song
// loss until a full rotation rebuilt it (measured 2026-09-10).
//
// The count both rules compared against was wrong twice over: it SKIPPED the fake songs, the very ones
// sung to occupy a slot, and it was global where the game counts per (singer, target).
//
// song_slots.h learns the cap from an EVICTION instead -- the game only makes room when there is none
// left, so the count at that moment is the limit, exactly -- and counts per person.

// ---- 4. two songs, one status --------------------------------------------------------------------------------
//
// A bard runs Valor Minuet IV and V at once, or two Marches: DIFFERENT songs granting the SAME status id. The
// monitor used to hold one entry per (person, status), so the second song of a pair simply overwrote the first
// and was watched by nobody -- lose it and the row just vanished, with no red OUT. Measured 2026-09-10: four
// songs up, six monitor entries, Valor Minuet V absent from both its own row and Kaories'.
//
// Keying by SPELL gives each song its entry. Deciding whether it is still up cannot be done by spell, though --
// the 0x076 carries presence-only status ids (`214 198 198 199`), and no packet anywhere says which Minuet is
// which. What it DOES say is HOW MANY copies are up, and that is enough: hold N entries for one status, see M
// copies, and N - M of them are gone.
//
// Which N - M? The same answer the model's slot rule already gives (model/song_slot.h, tested): the survivors
// are the most recently cast, because the copy that runs out is the one with least time left, and that is
// normally the older cast. Being consistent with the prune matters more than being right in the rare dispel
// case -- if the two disagreed, a row could be dropped by one and alerted by the other, forever.
//
// AND THAT IS WHY `rank` IS THE CAST, NOT THE ENTRY. The first cut of this ordered by when the monitor entry was
// created, which is not the same thing at all: an entry is REUSED when you sing the song again, so its birth can
// date from twenty minutes and several rotations ago. Measured 2026-09-10 -- the model kept Minuet V while the
// monitor kept Minuet IV and reported V lost, the exact split this rule exists to prevent. Rank is the ally
// cast's startMs (what the prune itself compares) or, for your own copy, its expiry; larger always survives.
inline bool focus_newer_sibling(unsigned rank, unsigned short spell,
                                unsigned otherRank, unsigned short otherSpell) {
    const int d = (int)(otherRank - rank);
    if (d != 0) return d > 0;
    return otherSpell > spell;   // cast in the same instant -> an arbitrary but STABLE order, so nobody is counted twice
}

inline bool focus_copies_cover(int newerSiblings, int copiesPresent) {
    return newerSiblings < copiesPresent;
}
// ...AND IT IS ONLY EVER ASKED OF A BUFF THAT CAN RUN SEVERAL COPIES -- a song, a rune (section 11). Asked of a
// single-instance buff it turns a TIER CHANGE into a loss: two entries for status 116, one live Phalanx, and the
// tier you replaced reads as the copy that went. `several` is that gate, and it is a rule rather than a line in
// the caller so that a test can hold it: one status is one buff unless the game really runs several of it.
inline bool focus_entry_up(bool several, int newerSiblings, int copiesPresent) {
    return several ? focus_copies_cover(newerSiblings, copiesPresent) : copiesPresent > 0;
}



// ---- 5. a song pushed out of its slot -- MOVED to model/song_slots.h on 2026-09-10 ------------------
//
// It lived here as song_evicted(), and it compared a song count to a learned cap. Both were wrong:
// the count skipped the FAKE songs sung for the sole purpose of holding a slot, and it was global
// while the game counts per (singer, target). The replacement needs no cap at all -- the model names
// the victim at CAST time, while the set is still intact, and the monitor only asks whether the song
// that went is the one that was named.


// ---- 6. a GROUPED red alert is a statement about the cast, exactly like "(AoE N)" on a healthy row ---------
//
// When a monitored buff goes missing the box draws a red OUT row. Several people losing the SAME buff can be
// one event or several, and the two want opposite things :
//   * one AoE cast (Protectra, a spell under SCH Accession, a COR roll) expires for everybody at once. That is
//     ONE fact, and six red lines for it is noise -- so it folds into "Phalanx (AoE 3)".
//   * three casts placed one by one on three people are THREE facts, and the NAME of each person is the whole
//     reason the per-person layout exists. Folding them drops exactly the information the alert is for.
//
// The rule shipped counting only "these alerts share a spell id", so three single-target Phalanx lost together
// came out as one nameless "Phalanx (AoE 3)". Reported from play on 2026-09-12 ("plusieurs personnes ont perdu
// Phalanx, ca les groupe alors qu'on ne veut pas"). It is the TWIN of the healthy-row defect fixed the same day
// in ally_group.h (`out.group = in.fresh && in.aoe && effN >= 2`) -- one of the two paths was corrected and the
// other was left, which is how a fix creates a report instead of closing one.
//
// So the cast decides here too: only entries whose cast actually named 2+ targets may group, and they group
// only with each other. `aoe` comes from the model (OtherBuff::aoe, set from the 0x028 target count).
struct AlertEntry {
    unsigned short spell;   // 0 = no known cast (food, gear) : never groupable
    unsigned char  aoe;     // the cast named 2+ targets
};

// How many alerts does entry `q` speak for ? 1 = draw it alone, WITH the name of whoever lost it.
// >= 2 = draw one grouped line for them all. Entries other than `q` that fold into it are found by the same
// rule, so the caller can skip any entry an earlier one already speaks for.
inline int focus_alert_speaks_for(const AlertEntry* a, int n, int q) {
    if (!a || q < 0 || q >= n) return 0;
    if (!a[q].spell || !a[q].aoe) return 1;          // a single-target cast (or none) stands alone, always
    int same = 0;
    for (int i = 0; i < n; ++i)
        if (a[i].spell == a[q].spell && a[i].aoe) ++same;   // only AoE entries fold, and only into an AoE entry
    return same < 1 ? 1 : same;
}

// Is `q` already covered by an earlier grouped alert ? Same rule, read from the other end.
inline bool focus_alert_covered(const AlertEntry* a, int n, int q) {
    if (!a || q <= 0 || q >= n) return false;
    if (!a[q].spell || !a[q].aoe) return false;      // a lone alert is never spoken for by anybody
    for (int i = 0; i < q; ++i)
        if (a[i].spell == a[q].spell && a[i].aoe) return true;
    return false;
}


// ---- 7. a monitored buff is not there : red OUT, or silence -- and WHY ------------------------------------------
//
// Cut out of model/timers_build.cpp on 2026-09-13 (lot D, step 4). The verdict carries its REASON, not a bool : the
// builder's //aio ftrace lines name the branch that decided (FOCUSHOLD, SONGOUT, GEOOUT), and the first five reasons
// also mean "no loss is running" -- the caller clears the entry's loss stamp for them and stamps it for the others.
//
// Source (lazy, like model/timers_rules.h) :
//   allyRowsOff()   an ALLY entry while "My buffs on allies" (tmMine) is off
//   muted()         taken off by //aio out
//   up()            the buff is present (for YOUR entry, also true when your buff list could not be READ)
//   zoneGrace()     the post-zone settle
//   listReady()     we hold a current buff list for that target (model: timers_build.cpp listReady)
//   lostMs() nowMs() when the loss was first seen (0 = not yet) ; now
//   hidden() holdSec()  "Hidden + focus" (the alert holds tmFocusHold seconds, then departs)
//   slotsFilled()   that person's song slots are full again (a learned cap, model/song_slots.h)
//   unrecoverable() replaced() geoReplaced() runeReplaced()   the four deliberate-swap suppressors
enum FocusAlert {
    FA_CATEGORY_OFF = 0, FA_MUTED, FA_UP, FA_ZONE_GRACE, FA_NO_DATA,   // no loss is running : clear the stamp
    FA_HOLD_EXPIRED, FA_SLOTS_FILLED, FA_UNRECOVERABLE, FA_REPLACED, FA_GEO_REPLACED, FA_RUNE_REPLACED,   // lost, and silent on purpose
    FA_ALERT                                                           // draw the red OUT row
};
inline bool focus_alert_clears_loss(FocusAlert v) { return v <= FA_NO_DATA; }

template <class Src>
inline FocusAlert focus_alert_verdict(const Src& e) {
    // Honour "My buffs on allies" here too. Turning it off stops ob[] being built, so no NEW ally entry
    // is created -- but the ones already in fm[] kept emitting, leaving a red blinking "Name - Haste OUT"
    // at the top of the box for a category the user had just switched off, until they re-cast it. Worse,
    // with ob[] empty the three deliberate-swap suppressors below (song replaced / unrecoverable / geo
    // replaced) iterate over nothing, so a Pianissimo swap would raise an OUT that is normally silenced.
    if (e.allyRowsOff()) return FA_CATEGORY_OFF;
    // Muted by hand (//aio out) : never alert, and never dropped from HERE. It used to be dropped as
    // soon as `has` went false -- but `has` reads the 0x076 presence on the target, while the entry
    // is FED by ob[] (your own cast estimate). The two disagree constantly: a buff missing from a
    // 0x076 that has not arrived yet made the muted entry die and the seeding loop re-create it a
    // frame later, brand new and UN-muted, with a new number. That is why the row kept changing
    // number instead of going quiet. The drop now happens where the feeding does -- see `seen`.
    if (e.muted()) return FA_MUTED;
    if (e.up()) return FA_UP;                                // still up -> the normal row covers it, reset the loss timer
    if (e.zoneGrace()) return FA_ZONE_GRACE;                 // just zoned : buff lists still arriving -> don't false-alert (persist across the zone)
    if (!e.listReady()) return FA_NO_DATA;                   // NO DATA for this target (alliance member, or a party member out of zone : buffs_for()==0) -> "unknown", NOT "gone". The self path already fails open via meHas ; the ally path used to fire a permanent false red "OUT" here.
    const unsigned lost = e.lostMs() ? e.lostMs() : e.nowMs();   // just went missing -> the caller stamps it
    // Unfollow-Focus = hidden + focus -> the alert holds tmFocusHold seconds then depops (Focus alone =
    // permanent until re-cast). Per-SPELL hide key : keyed on the shared STATUS this never matched for a
    // buff two spells can grant, so the "hold 15s then depop" branch was unreachable for Cocoon.
    if (e.hidden() && (unsigned)(e.nowMs() - lost) > (unsigned)e.holdSec() * 1000u) return FA_HOLD_EXPIRED;
    // THE SLOT IT IS ASKING FOR HAS BEEN FILLED. An OUT says "you lost this, sing it again"; once that
    // person's slots are full of your songs again the room it wants is gone, and you are the one who
    // used it. Say nothing -- but KEEP the entry.
    //
    // Freeing it here is what made the box shimmer. The model still holds the row, so the seeding loop
    // re-creates the entry on the very next frame, brand new, with lostMs = 0 -- and an entry with no
    // lostMs cannot be freed by the prune, so it survives to here and DRAWS. Then it has a lostMs, is
    // freed, and vanishes. On, off, on, off, at 60 Hz. Measured 2026-09-11: the same focus verdict on
    // both frames, `lost` alternating 0/1, the red row appearing with it.
    //
    // The lesson generalises past this line: never FREE a monitor entry for a condition that outlives
    // one frame, because whatever created it will create it again.
    if (e.slotsFilled()) return FA_SLOTS_FILLED;
    if (e.unrecoverable()) return FA_UNRECOVERABLE;          // a lost 5th Clarion-Call song can't be refilled -> suppress the OUT entirely (never even a one-frame flash before the prune frees it)
    if (e.replaced()) return FA_REPLACED;                    // deliberately swapped out by a new song on the same ally (Pianissimo Ballad) -> no OUT, not even a one-frame flash (prune frees the slot next frame)
    if (e.geoReplaced()) return FA_GEO_REPLACED;             // an Indi- you replaced with another Indi- -> no OUT, not even a one-frame flash (the prune frees the slot next frame)
    if (e.runeReplaced()) return FA_RUNE_REPLACED;           // a rune pushed out by a newer rune -> no OUT (section 10)
    return FA_ALERT;
}

// ---- 8. which monitored entries are forgotten this frame ----------------------------------------------------------
//
// Cut out of model/timers_build.cpp on 2026-09-13 with section 7. KEEP_SURVIVED means "keep, and the post-zone check
// is settled" : the caller clears the entry's zoneCheck.
//
// Source (lazy) : self() zoneGrace() partyOrder() focusOn() isSong() offzone() zoneCheck() listReady() has()
//                 lostMs() nowMs() hidden() holdSec() unrecoverable() replaced() geoReplaced() runeReplaced()
enum FocusPrune {
    FP_KEEP = 0, FP_KEEP_SURVIVED_ZONE,
    FP_DROP_GONE_OR_OFF, FP_DROP_SONG_OFFZONE, FP_DROP_ZONE_CASUALTY,
    FP_DROP_UNRECOVERABLE, FP_DROP_REPLACED, FP_DROP_GEO_REPLACED, FP_DROP_HOLD_EXPIRED, FP_DROP_RUNE_REPLACED,
    FP_DROP_TIER_REPLACED
};
inline bool focus_prune_drops(FocusPrune v) { return v >= FP_DROP_GONE_OR_OFF; }

template <class Src>
inline FocusPrune focus_prune_verdict(const Src& e) {
    // <= 5, NOT <= 17. The 0x076 that feeds listReady/focusHas carries YOUR PARTY ONLY, so a monitor
    // entry on an alliance member can never be decided: listReady stays false forever, the emit below
    // resets lostMs every frame, and the only prune path here needs lostMs != 0 -- the entry became
    // IMMORTAL. It also never drew anything, so it was pure dead weight that filled the 24 slots and
    // then silently blocked new entries, including your own. Alliance targets are dropped here, and
    // refused at creation below.
    const bool live = e.self() ? true : (e.zoneGrace() || e.partyOrder() <= 5);   // 0..5 party ; 6..17 alliance and 99 = gone (roster is unstable mid-zone -> keep during grace)
    if (!(live && e.focusOn())) return FP_DROP_GONE_OR_OFF;                       // gone / focus off -> drop
    // SONG on an ally who is no longer in YOUR zone (they stayed behind, or YOU zoned away) : their 0x076
    // stops refreshing, so the buff set FREEZES -- the row would either linger on a drifting estimate or
    // fire a wrong OUT off the stale list. User rule : CLEAN ally song rows the moment the target is
    // out-of-zone. Songs only (song_family, spell-keyed) -- ally RDM/enh buffs behave and are left alone.
    // Gated past the zone grace so the roster's per-member zone id has settled first (no false clean).
    if (!e.self() && !e.zoneGrace() && e.isSong() && e.offzone()) return FP_DROP_SONG_OFFZONE;
    // THE SAME BUFF AT ANOTHER TIER TOOK ITS PLACE (section 11). Dropped whether the buff is up or not, and
    // BEFORE the loss stamp can be read : the replacement is not a loss, and leaving the old entry to be
    // decided later is how it became a permanent red OUT. It cannot be put back either -- putting it back
    // IS the other tier -- so there is nothing for an alert to ask for.
    if (e.tierReplaced()) return FP_DROP_TIER_REPLACED;
    if (e.zoneCheck()) {                                                            // pending post-zone check : decide ONLY after the grace ends AND the list is back.
        if (e.zoneGrace() || !e.listReady()) return FP_KEEP;                        //   still settling : keep, no decision, no alert
        if (e.has()) return FP_KEEP_SURVIVED_ZONE;                                  //   grace over + list stable + present -> survived the zone, track normally
        return FP_DROP_ZONE_CASUALTY;                                               //   grace over + list stable + ABSENT -> the game dropped it on zoning -> depop, NO alert
    }                                                                               //   (deciding DURING the grace read the stale pre-zone buff list -> false survivors -> OUT)
    // a "Hidden+focus" alert that has held its full tmFocusHold with the buff still gone -> FREE the slot
    // (the emit stops drawing it at that point ; without this it lingers forever and can fill the monitor).
    if (e.lostMs() && !e.has()) {
        // THE SLOT IT IS ASKING FOR HAS BEEN FILLED. An OUT says "you lost this, sing it again". Once
        // that person's song slots are full again -- with something else, because this one is still
        // missing -- the alert is asking for room that no longer exists, and you are the one who used
        // it. Reported 2026-09-10: songs left in OUT, a fresh rotation sung over them, and the old
        // alerts stayed. Forgetting them is not hiding a loss; it is noticing you replaced it.
        //
        // A real dispel does NOT hit this: losing a song drops the count BELOW the cap, so the alert
        // stands until you either sing it back (present again) or fill the slot with another song.
        if (e.unrecoverable()) return FP_DROP_UNRECOVERABLE;   // un-refillable 5th Clarion-Call song -> free the slot (no OUT will ever draw ; without this the un-drawn entry lingers and fills the monitor)
        if (e.replaced()) return FP_DROP_REPLACED;             // deliberately swapped out by a new song on the same ally (Pianissimo) -> free the slot, never an OUT
        if (e.geoReplaced()) return FP_DROP_GEO_REPLACED;      // a previous Indi- you replaced by casting another one -> free the slot, never an OUT
        if (e.runeReplaced()) return FP_DROP_RUNE_REPLACED;    // a rune a newer rune pushed out -> free the slot, never an OUT (not re-seeded : it is no longer on you)
        if (e.hidden() && (unsigned)(e.nowMs() - e.lostMs()) > (unsigned)e.holdSec() * 1000u) return FP_DROP_HOLD_EXPIRED;
    }
    return FP_KEEP;
}


// ---- 9. a buff that went because YOU replaced it : a swap, never a red OUT ----------------------------------------
//
// Cut out of model/timers_build.cpp on 2026-09-13 with sections 7 and 8.
//
// Source : self() target() spell() status()        the monitored entry
//          nowMs() ; others() otherTarget(i) otherSpell(i) otherStartMs(i) otherAoe(i)   your recorded casts on allies
//          isSong(spell) isIndi(spell)             BRD song ; GEO Indicolure (skill 44)
//          evicted()       the model named THIS song as the one the game pushed out of that person, < 6 s ago
//          carriedAura()   the status of the Indi- you carry now (0 = none)
//
// A song you cast on an ally that vanished because YOU just SINGLE-TARGETed a DIFFERENT song onto that SAME
// ally (Pianissimo) is a DELIBERATE slot swap, not a loss -> no red OUT, just depop. Signal : a newer,
// different, SINGLE-TARGET song on the same target, cast in the last few seconds (the slot casualty leaves in
// the same 0x076 update the replacement lands in). The `!ob[i].aoe` gate is load-bearing : an AoE song
// re-stamps startMs on EVERY member each cast, so without it the 6s window would sit open across your whole
// rotation and mask a real dispel on anyone you're singing to. Pianissimo is the only way to single-target a
// song, so `!aoe` isolates the deliberate swap. Ally songs only : your own re-song is handled elsewhere.
template <class Src>
inline bool focus_song_replaced(const Src& s) {
    if (!s.isSong(s.spell())) return false;
    // (a) Pianissimo : a SINGLE-TARGET song landed on this same ally and took the slot. Ally rows only --
    //     a Pianissimo song never lands on you, so a self row can never be its casualty.
    if (!s.self())
        for (int i = 0; i < s.others(); ++i)
            if (s.otherTarget(i) == s.target() && s.otherSpell(i) != s.spell() && s.isSong(s.otherSpell(i)) && !s.otherAoe(i)
                && (unsigned)(s.nowMs() - s.otherStartMs(i)) < 6000u) return true;   // single-target replacer only ; 6s covers the 0x076 cadence, short enough a real later dispel still OUTs
    // (b) THE GAME PUSHED IT OUT to fit a new song. Not decided here: the model named the victim at
    //     CAST time, while the set was still intact (PartyState::song_was_evicted, model/song_slots.h).
    //     By the time we notice a loss the row has already left ob[], so this could never have been
    //     answered from here -- which is why the rule that lived here asked "am I at the cap?" of a
    //     count that skipped the very songs sung to fill slots, and silenced real dispels for a whole
    //     Clarion Call recast. Now: it went, and it was the one the game had to drop. Nothing else.
    return s.evicted();
}

// GEO Indi- : you carry exactly ONE aura (`selfGeo_`, party_state.h -- a single slot, not a list), so casting
// a DIFFERENT Indi- REPLACES the previous one. Its status leaving the buff list is that SWAP, not a loss :
// you cannot "put it back" without dropping the one you deliberately chose, so a red OUT is permanent and
// wrong. Reported : Indi-Fury -> Indi-Refresh -> Indi-Regen left Fury and Refresh stuck OUT ; only the Indi-
// you carry NOW may alert. Same shape as songReplaced above.
// Identifying a geomancy entry : the SPELL when we attributed the cast (skill 44 = Indicolure, tb_buff_gen),
// else the GEO-ONLY statuses -- Boosts 542-556 and "Colure Active" 612, which no other spell grants. The
// status alone can NOT decide for the shared ones (539 Regen / 541 Refresh / 580 Haste), hence the spell
// first : a real Refresh must keep its normal OUT.
template <class Src>
inline bool focus_geo_entry(const Src& s) {
    return s.isIndi(s.spell()) || (s.status() >= 542 && s.status() <= 556) || s.status() == 612;
}
template <class Src>
inline bool focus_geo_replaced(const Src& s) {
    if (!focus_geo_entry(s)) return false;
    if (s.self()) { const unsigned aura = s.carriedAura();
                    return aura && aura != s.status(); }   // the aura you carry now is a DIFFERENT Indi- -> this one was swapped out
    // ENTRUST'd Indi- on an ally : same rule, but the model keeps no per-ally aura slot -- so require the
    // EVIDENCE, a newer Indi- cast we actually saw land on that same ally (the 6 s window mirrors the song
    // rule : long enough for the 0x076 update the swap arrives in, short enough that a later dispel OUTs).
    for (int i = 0; i < s.others(); ++i) {
        if (s.otherTarget(i) != s.target() || s.otherSpell(i) == s.spell()) continue;
        if (s.isIndi(s.otherSpell(i)) && (unsigned)(s.nowMs() - s.otherStartMs(i)) < 6000u) return true;
    }
    return false;
}

// ---- 10. a rune pushed out by a newer rune : a swap, never a red OUT ----------------------------------------------
//
// A Rune Fencer changes runes constantly (Ignis -> Flabra -> Tenebrae, on the mob's weakness), and the game holds a
// fixed number of them : placing one more when every slot is full drops the OLDEST. Every rune pushed out that way
// used to raise a permanent red OUT -- reported from play 2026-09-14 on PLD/RUN and RUN ("il passe tous en out quand
// on change de rune ce qu'on fait tout le temps") -- although you replaced it on purpose and cannot put it back
// without dropping the one you just chose. Only the runes you placed LAST may alert : 2 on a RUN sub, 3 on a RUN main.
//
// ONE ENTRY PER RUNE PLACED, not per element. Two Ignis are two runes, and losing both must say so twice -- the first
// cut watched the status, so a PLD/RUN whose two Ignis ran out saw a single "Ignis" OUT (reported the same night).
// A rune has no spell to tell two copies apart, but each copy carries its own expiry in the 0x063, and every rune
// lasts the same time : the expiry IS the placement, and the entry's rank is it (focus_rune_match).
//
// No placement history is needed beyond that. The runes you placed last are the ones on you now plus the most
// recent of the ones you lost -- as many of those as there are FREE slots. So a lost rune stays silent when at least
// (cap - runes on you) lost runes are newer than it : with every slot full that is all of them (pushed out by a new
// rune), with Ignis x2 run out on a free board it is neither (two OUT), and once a new rune takes one of the two
// slots the older Ignis goes quiet. Consumed runes (Lunge, Gambit...) free their slots, so they keep their OUT.
//
// The cap follows the RUN level that is in effect -- main, or sub under another main -- through Rune Enchantment :
// 1 rune from level 5, 2 from 35, 3 from 65. Rune statuses : Ignis 523 ... Tenebrae 530 (buffs_gen.h), YOUR list only.
inline bool focus_rune_status(unsigned st) { return st >= 523 && st <= 530; }

inline int focus_rune_cap(int mjob, int mlvl, int sjob, int slvl) {
    const int RUN = 22;
    const int lv = mjob == RUN ? mlvl : (sjob == RUN ? slvl : 0);
    return lv >= 65 ? 3 : lv >= 35 ? 2 : lv >= 5 ? 1 : 0;
}

// newerLost : YOUR rune entries, of any element, that are lost too and were placed after this one (focus_rune_newer_lost).
inline bool focus_rune_replaced(bool self, unsigned status, int cap, int runesUp, int newerLost) {
    return self && focus_rune_status(status) && cap > 0 && newerLost >= cap - runesUp;
}

// Which monitor entry is THIS copy of a rune ? The unclaimed entry of that status whose rank (the expiry it was seen
// with) is nearest `expiry`, within a second -- two runes are never placed that close, and a re-sent 0x063 may move
// a timer by a tick. -1 = a rune placed just now : the caller creates its entry. `Mem` : self status rank seen.
template <class Mem>
inline int focus_rune_match(const Mem* fm, int n, unsigned status, unsigned expiry) {
    int best = -1; unsigned bestD = 61;
    for (int q = 0; q < n; ++q) {
        if (!fm[q].self || fm[q].status != status || fm[q].seen) continue;
        const unsigned d = fm[q].rank > expiry ? fm[q].rank - expiry : expiry - fm[q].rank;
        if (d < bestD) { bestD = d; best = q; }
    }
    return best;
}

// How many of YOUR other rune entries are lost as well AND newer than entry q. lost[i] : the entry's buff is gone
// (for your own list, only when that list was READ). Same rank on two entries : the later index counts as newer, so
// nobody is counted twice.
template <class Mem>
inline int focus_rune_newer_lost(const Mem* fm, const bool* lost, int n, int q) {
    if (q < 0 || q >= n || !fm[q].self || !focus_rune_status(fm[q].status)) return 0;
    int c = 0;
    for (int i = 0; i < n; ++i) {
        if (i == q || !lost[i] || !fm[i].self || !focus_rune_status(fm[i].status)) continue;
        const int d = (int)(fm[i].rank - fm[q].rank);
        if (d > 0 || (d == 0 && i > q)) ++c;
    }
    return c;
}

// ---- 11. one status, ONE buff : a newer TIER is a replacement, never a loss ---------------------------------------
//
// Phalanx and Phalanx II are the SAME buff -- status 116 -- and the game holds exactly one of it: cast either tier
// over the other and the first is simply gone, replaced. The model already knows that (a landed non-song cast drops
// that target's other rows on the same status, party_state.cpp "OB replace"), but the monitor is a SEPARATE table,
// keyed by (person, status, SPELL) because section 4 needs it to be, and nothing told it. So the entry for the old
// tier stayed behind, stopped being fed, counted the surviving entry as a newer sibling over one live copy -- and
// read the replacement as a loss. Reported from play 2026-09-15: Phalanx II on the party, then Accession Phalanx as
// RDM/SCH, and every Phalanx II sat in a permanent red OUT while everyone plainly had Phalanx. ANY tier change does
// it: Haste -> Haste II, Regen II -> Regen III, Protect IV -> Protect V.
//
// Section 4's copy arithmetic only ever made sense for the buffs that really do run several copies at once -- a
// bard's two Marches, a Rune Fencer's runes. `several[i]` marks those two kinds; everywhere else one status is one
// buff, whatever the tier. This rule reads the rest: an entry NOTHING fed this frame, whose (person, status) another
// entry IS feeding under a different spell, was replaced by it.
//
// `seen` is the signal, not the rank. Rank would be the natural-looking test and it is wrong: an entry that stopped
// being fed keeps the rank it last saw, so a SHORTER new tier over a longer old one (Phalanx over the remains of
// Phalanx II) ranks BELOW the corpse, and the rule would drop the live one instead. What is fed this frame is not
// a comparison at all -- it is the model's own verdict, arriving from the same place that dropped the row.
// `Mem` : self target status spell seen.
template <class Mem>
inline bool focus_tier_replaced(const Mem* fm, const bool* several, int n, int q) {
    if (!fm || !several || q < 0 || q >= n) return false;
    if (several[q] || fm[q].seen) return false;            // still fed -> this IS the tier that is up
    for (int i = 0; i < n; ++i) {
        if (i == q || several[i] || !fm[i].seen) continue;
        if (fm[i].self != fm[q].self || fm[i].status != fm[q].status) continue;
        if (!fm[q].self && fm[i].target != fm[q].target) continue;
        if (fm[i].spell == fm[q].spell) continue;          // the same cast re-affirmed, not another tier
        return true;
    }
    return false;
}

} // namespace aio
