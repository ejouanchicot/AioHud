// debuff_rules.h -- the one question record_debuff cannot ask the server : did this cast actually REPLACE
// what was already on the mob, or did the game refuse it ?
//
// MEASURED, 2026-09-11 (//aio dbflog, live client, two characters on one mob) :
//
//     DBF cast cat4 actor=00016500 spell=25 self=0 -> effect=134      <- Dia III, by a mate
//     DBF cat4-tgt tid=011060F5 msg=2 param=34 effect=134 self=0
//     DBF rec-ADD  tid=011060F5 st=134 self=0 n=1
//     DBF cast cat4 actor=0006BD0B spell=24 self=1 -> effect=134      <- Dia II, by me, same mob
//     DBF cat4-tgt tid=011060F5 msg=2 param=18 effect=134 self=1
//     DBF rec-refresh tid=011060F5 st=134 self=1 spell=24 n=1         <- the bug : Dia III became Dia II
//
// The second line is the whole point. The weaker Dia II came back as message 2 -- "<actor> casts <spell>.
// <target> takes 18 points of damage." -- NOT as 75 "has no effect on". The damage landed ; the status did not
// overwrite the stronger Dia III. So the server's own verdict, which is what every other path here leans on,
// says nothing at all about the status : a rider is invisible in the 0x028 (see the cat-4 block). This case,
// and only this case, has to be predicted from the table.
//
// WHY THIS IS NOT THE REFUSE-PATH THAT WAS DELETED. An earlier version of record_debuff refused a cast whenever
// `spell_overwrites_spell(existing, incoming)` was true, and it was wrong twice over -- both failures are
// answered here by a separate mechanism, not by a threshold :
//
//   1. MUTUAL pairs. The eight Threnody II list each OTHER, so "the existing one overwrites the incoming one"
//      was true in both directions and switching element froze the row on the first Threnody for the life of
//      the mob. The elemental DoTs are the same defect in a longer shape : Burn->Frost->Choke->Rasp->Shock->
//      Drown->Burn is a RING. Those edges mean "these two cannot both be up", never "this one is stronger".
//      -> the generator now marks an edge STRICT only when the other end cannot reach back through the whole
//         graph (scripts/gen_overwrites.py), and only a strict edge may refuse. Cycles of any length fall out
//         of the one test ; `spell_outranks_spell` is false in BOTH directions for every pair in a cycle.
//
//   2. GHOSTS. No wear-off packet exists for a debuff someone else cast, so an entry sits in the set long past
//      its duration -- and a long-expired Dia III vetoed every weaker re-cast until the mob died.
//      -> a refusal requires the stronger entry to still be LIVE. Same test the sleep veto above it uses, and
//         the same direction of error : an unknown duration counts as EXPIRED. Erring that way costs the tier
//         for one cast ; erring the other way is a debuff that can never be shown again.
//
// What a mistake costs, either way : refusing wrongly = the row keeps the older tier until the stronger one
// really expires. Allowing wrongly = the bug measured above. Both are visible in one `//aio dbflog` line.
#pragma once
#include "model/overwrites_gen.h"

namespace aio {

// Is the entry still running ? An unknown duration (0) is treated as expired -- see the ghost note above.
inline bool debuff_entry_live(unsigned startMs, unsigned durMs, unsigned now) {
    return durMs != 0 && (unsigned)(now - startMs) < durMs;
}

// Which debuff already on this target refuses `cast` ? Index into the arrays, or -1 when the cast lands.
// Arrays are the mob's DebuffSet columns (fixed capacity, no allocation) ; `durMs` is the entry's own base
// duration, not the remaining time.
//
// `serverNamedStatus` is the ONLY thing that outranks this whole file. When the action message is a status form
// -- "<actor> casts <spell>. <target> is <status>" -- the server has just named the effect it applied, and no
// table may contradict that. It matters because the table is not always a ladder even where it is acyclic :
// res has Slow II overwriting Hojo: San and nothing the other way, so a Hojo: San landing on a live Slow II
// would be refused here while the game was announcing it. A pure enfeeble always comes back as one form or the
// other -- named, or "no effect" (already rejected before us) -- so this leaves prediction to exactly the case
// that needs it : a cast whose message proves only that DAMAGE landed, which is what a Dia II over a Dia III
// sends. Measured (capture above), not assumed.
inline int debuff_refused_by(const unsigned short* spell, const unsigned* startMs, const unsigned* durMs,
                             int n, unsigned short cast, unsigned now, bool serverNamedStatus) {
    if (!cast || serverNamedStatus) return -1;
    for (int i = 0; i < n; ++i) {
        if (!spell[i]) continue;                                   // an entry with no known caster spell proves nothing
        if (!spell_outranks_spell(spell[i], cast)) continue;       // not stronger, or a cycle -> the cast lands
        if (!debuff_entry_live(startMs[i], durMs[i], now)) continue;   // a ghost vetoes nothing
        return i;
    }
    return -1;
}

// WHERE THE LADDER IS PROVEN, AND WHERE IT IS ONLY WRITTEN DOWN.
//
// res/spells.lua gives ~340 asymmetric pairs and we verified two families of them. That is not a detail : this
// rule CONTRADICTS the game when a pair is wrong, and the symptom -- "I cast it and it never showed" -- is the
// same silence the whole codebase is built against. So it acts only where the ladder is backed by evidence,
// and everywhere else it reports what it WOULD have done without doing it. A pair gets promoted here when a
// capture says so, never because the table looks reasonable.
//
//   Dia / Bio  -- measured here on 2026-09-11 (the capture above) AND the published ladder agrees :
//                 Dia -> Bio -> Dia II -> Bio II -> Dia III -> Bio III, "regardless of when it was cast".
//   Helix      -- not measured by us : two independent sources say tier II is never removed by tier I, and
//                 same-tier helixes overwrite each other whatever the element (which this table leaves to the
//                 same-status refresh, not to a refusal). Same shape as Dia : damage message, invisible rider.
//
// Everything else -- Poison, Slow/Hojo, Blind/Kurayami, Paralyze/Jubaku, Requiem, Elegy, Lullaby, Threnody,
// Distract/Frazzle/Addle -- is a pure enfeeble whose message NAMES the status, so the server already decides it
// and this rule is not even consulted. If one of them ever reaches here, that is itself the discovery, and the
// log line at the call site is how it arrives.
inline bool debuff_ladder_proven(unsigned a, unsigned b) {
    a = ow_canonical(a); b = ow_canonical(b);
    const bool aDia = (a >= 23 && a <= 27) || (a >= 230 && a <= 234);   // Dia I-V / Bio I-V (-ga canonicalised above)
    const bool bDia = (b >= 23 && b <= 27) || (b >= 230 && b <= 234);
    const bool aHlx = (a >= 278 && a <= 285) || (a >= 885 && a <= 892); // Helix I / Helix II
    const bool bHlx = (b >= 278 && b <= 285) || (b >= 885 && b <= 892);
    return (aDia && bDia) || (aHlx && bHlx);
}

} // namespace aio
