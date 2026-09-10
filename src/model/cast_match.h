// cast_match.h -- which cast produced this buff timer?
//
// Extracted from PartyState::match_cast on 2026-09-09 (element 6 of docs/audits/plan-harnais-2026-09-09.md).
// The answer names the row's owner, so getting it wrong shows somebody else's name on your buff -- or yours on
// theirs -- and it decides what the "Mine only" filter hides. Eight of the last audit's findings live in this
// file's neighbourhood.
//
// THE TIMER SAYS WHEN, NEVER WHO. FFXI's buff list carries an id and an expiry and nothing else, so the caster
// has to be inferred from the casts we saw. Two shapes, and they need opposite treatments:
//
//   - SEVERAL live timers of one status (two Marches, two Minuets) : pair them by RANK, longest predicted
//     expiry to longest real one. Their predictions are too loose to compare individually -- a Troubadour'd
//     song can run double -- so closeness would pick at random.
//
//   - ONE live timer but several casts in the ring (Protect re-cast by a trust over yours, or a tier change) :
//     rank would return the LONGEST-predicted cast, which may be the one that was OVERWRITTEN. Our own casts
//     are predicted with gear and job points, a foreign one lands near its base, so the cast whose prediction
//     sits NEAREST the real timer is the one that made it.
//
// AND WHEN UNSURE, NEVER BLAME A TRUST. A trust has no Troubadour and no Marcato, so its cast cannot produce a
// buff outliving its own base by a minute and a half; if the real timer runs far past what a trust's cast
// predicted, the pairing is wrong. Returning "unknown" keeps the row visible, while attributing it to a trust
// HIDES it under the buff-source filter -- the asymmetry is deliberate.
#pragma once

namespace aio {

struct CastCand {
    unsigned predExp;    // expiry this cast predicted, in FFXI ticks
    bool     isTrust;    // cast by a trust (never by you)
};

// 90 s in FFXI ticks (1/60 s). Not "some slack": it is the largest gap a correct pairing can show once the
// prediction model has done its work, measured against the server.
static const int CAST_TRUST_OVERSHOOT = 90 * 60;

// `cands` must already be ordered longest-predicted-expiry first, which is how the caller holds them.
// Returns the index of the cast that produced a timer expiring at `expiry`, or -1 for an honest "unknown".
inline int cast_match(const CastCand* cands, int n, unsigned expiry, int liveTimers, int rank,
                      const char** why) {
    *why = "no candidate";
    if (n <= 0 || rank < 0 || rank >= n) return -1;

    int pick = rank;
    *why = "paired by rank";

    if (liveTimers <= 1 && n > 1) {
        int best = 0, bestD = 0x7FFFFFFF;
        for (int a = 0; a < n; ++a) {
            int d = (int)(cands[a].predExp - expiry);
            if (d < 0) d = -d;
            if (d < bestD) { bestD = d; best = a; }
        }
        pick = best;
        *why = "paired by closeness (one live timer, several casts)";
    }

    if (cands[pick].isTrust && (int)(expiry - cands[pick].predExp) > CAST_TRUST_OVERSHOOT) {
        *why = "unknown (this timer outruns what a trust's cast could produce)";
        return -1;
    }
    return pick;
}

} // namespace aio
