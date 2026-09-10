// ally_group.h -- how many people a buff row speaks for, and whether it draws as one "(AoE N)" line or as one
// line per person.
//
// Extracted from hud_timers.cpp on 2026-09-09 (element 2 of docs/audits/plan-harnais-2026-09-09.md). It is the
// densest decision in the module and every branch of it commemorates a shipped defect -- which is exactly why
// it belongs behind a test rather than inside a 750-line draw function.
//
// WHAT THE PLAYER ASKED FOR, in their words: a song sung normally shows one line marked (AoE N) with the number
// of people HIT; a Pianissimo song shows its own line; and when a re-sing misses somebody, that person is named
// instead of being hidden inside a group that claims everything is fine.
//
// THE COUNT HAS THREE SOURCES, and picking the wrong one is where the bugs came from:
//   - `allies`   : the rows in this group's own bucket -- your own recent casts, honest but blind to anyone the
//                  0x028 target walk never named.
//   - `countHas` : how many party members the SERVER says carry this status. Authoritative, but STATUS-scoped
//                  (the party buff list carries no spell id), so it counts the carriers of a DIFFERENT song
//                  that happens to share the status.
//   - `meHas` / `selfCast` : whether you are one of the carriers.
//
// The rules that follow are not preferences; each one is a bug that reached a player:
//   - a SINGLE-TARGET group must never use countHas. Protectra V on the party, then Protect V on one ally, gave
//     that one-ally group N = 6 and drew a "(AoE 6)" row for a cast that touched one person.
//   - a REAL AoE takes countHas but FLOORED by its own bucket: right after a plugin reload the party buff list
//     is empty, countHas reads 0, and a song you had AoE'd onto the party stopped regrouping until a zone.
//   - a FRESH group whose spell also has a laggard must count its own bucket, not countHas: the server cannot
//     tell an old copy from a fresh one, so countHas would re-absorb the laggard into the very group the split
//     exists to separate.
//   - a LAGGARD group NEVER groups. Its people are listed by name, which is the whole point of the split.
#pragma once

namespace aio {

struct GroupIn {
    bool fresh;             // this is the freshest cast of its spell (false = a laggard the re-sing missed)
    int  allies;            // ally rows in this group's own bucket
    bool aoe;               // the cast named 2+ targets, so it reached you too
    bool selfCast;          // you cast it, and it landed on you
    bool meHas;             // you currently carry this status
    int  countHas;          // party members the server says carry this status (status-scoped, no spell id)
    bool hasLagSameSpell;   // another group of the SAME spell is a laggard
    bool userGroupPref;     // config: group ally buffs even when the cast was not an AoE
};

struct GroupOut {
    int  effN;      // the N in "(AoE N)" -- people this row speaks for
    bool group;     // one grouped row, or one row per person
    bool drop;      // nobody carries it any more : draw nothing
};

inline GroupOut ally_group_verdict(const GroupIn& in) {
    GroupOut out; out.effN = 0; out.group = false; out.drop = false;
    const int me = (in.meHas || in.selfCast) ? 1 : 0;

    if (!in.fresh) {
        // A laggard speaks only for the people it still holds, and never merges them into a count.
        out.effN = in.allies;
    } else if (in.hasLagSameSpell) {
        out.effN = in.allies + me;
    } else if (!in.aoe) {
        // Single target: the 0x028 walk cannot have missed a target you named, so there is nothing for the
        // server-side count to recover -- and using it here counts other groups' people.
        out.effN = in.allies + (in.selfCast ? 1 : 0);
    } else {
        out.effN = in.countHas;
        const int floor = in.allies + me;             // the empty-cache case: never count fewer than we cast on
        if (floor > out.effN) out.effN = floor;
    }

    if (out.effN < 1) { out.drop = true; return out; }
    out.group = in.fresh && (in.aoe || in.userGroupPref) && out.effN >= 2;
    return out;
}

} // namespace aio
