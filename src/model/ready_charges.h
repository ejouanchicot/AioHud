// ready_charges.h -- the Beastmaster's READY charge pool : the pure rules (no memory, no clock), so the tests can
// pin them (tests/t_readycharges.cpp). The poller feeds them (model/bst_state.cpp). SPEC docs/design/bst-module-spec.md.
//
// The client keeps ONE recast slot (id 102) for the whole pool of 3 charges, and it counts down to the pool being
// FULL again, never to the next charge -- the same shape as the SCH stratagem pool (game_mem.cpp compute_grimoire).
// One charge is 30 s, -2 s a level of the Sic Recast merit, -5 s from the 100 JP gift, -5 s a piece of "Sic and
// Ready ability delay -5" gear worn when the move goes off ; never under 10 s (BG Wiki Ready).
//
// Unlike a stratagem, a Ready move costs 1, 2 or 3 charges, so the jump of the pool on a use is `cost x length`
// and the length can only be LEARNED once the cost of that move is known : the pet's own action (0x028 category 11,
// its mob-skill name) gives it. ReadyLearner pairs the two, whichever comes first.
#pragma once

namespace aio {

static const int READY_CHARGES = 3, READY_LEN_MIN = 10, READY_LEN_MAX = 30;

// Seconds a charge before any measurement : merit level 0..5, job points spent, "-5" pieces worn now.
inline int ready_len_fallback(int meritLevel, int jpSpent, int gearPieces) {
    const int v = 30 - 2 * meritLevel - (jpSpent >= 100 ? 5 : 0) - 5 * gearPieces;
    return v < READY_LEN_MIN ? READY_LEN_MIN : v;
}

// What the pool says : charges available, seconds to the next one (ceil, -1 = pool full). `ticks` is the raw 1/60 s
// counter : a ceil-ed second lands on the wrong side of a charge boundary (10 s left on a 10 s charge is one charge
// recharging, not two). False when the pool runs longer than 3 charges of that length : the length is wrong.
struct ReadyView { int charges = READY_CHARGES; int nextSec = -1; };
inline bool ready_view(int ticks, int lenSec, ReadyView& out) {
    out = ReadyView{};
    if (lenSec <= 0) return false;
    const int per = lenSec * 60;
    if (ticks <= 0) return true;
    const int recharging = (ticks + per - 1) / per;
    if (recharging > READY_CHARGES) return false;
    out.charges = READY_CHARGES - recharging;
    out.nextSec = (ticks - (recharging - 1) * per + 59) / 60;
    return true;
}

// Seconds until a move costing `cost` charges can go (0 = now).
inline int ready_wait_sec(int ticks, int lenSec, int cost) {
    const int w = ticks - (READY_CHARGES - cost) * lenSec * 60;
    return w <= 0 ? 0 : (w + 59) / 60;
}

// The length one use states : the pool's jump (1/60 s, the frame that passed already added back) over the cost,
// to the nearest second ; 0 when it cannot be a charge length.
inline int ready_len_from_jump(int jumpTicks, int cost) {
    if (cost < 1 || cost > READY_CHARGES || jumpTicks <= 0) return 0;
    const int s = (jumpTicks + cost * 30) / (cost * 60);
    return (s >= READY_LEN_MIN && s <= READY_LEN_MAX) ? s : 0;
}

// Pairs a jump of the pool with the cost of the move that made it. The pool jumps when the move is SENT, the pet's
// action arrives when it lands (seconds later, more if it has to walk) ; a cost seen first waits for its jump a
// little. Two jumps with no cost between them cannot be told apart : nothing is learned from them.
struct ReadyLearner {
    static const unsigned PAIR_MS = 20000u;   // a jump waits this long for its move
    static const unsigned COST_MS = 3000u;    // a move seen first waits this long for its jump
    int      lastTicks = -1;  unsigned lastMs = 0;
    int      pendJump = 0;    unsigned pendMs = 0;   // -1 = two jumps unpaired : ambiguous
    int      pendCost = 0;    unsigned costMs = 0;
    int      learned = 0;     // seconds a charge, 0 = nothing learned
    unsigned quietUntil = 0;  // learn nothing before this (0 = no quiet) : Sic was just used

    void reset() { *this = ReadyLearner{}; }
    // once a frame with the raw pool counter ; true when `learned` changed
    bool frame(int ticks, unsigned nowMs) {
        bool changed = false;
        if (quiet(nowMs)) { pendJump = 0; pendCost = 0; lastTicks = ticks; lastMs = nowMs; return false; }
        if (pendJump && nowMs - pendMs > PAIR_MS) pendJump = 0;
        if (pendCost && nowMs - costMs > COST_MS) pendCost = 0;
        if (lastTicks >= 0 && ticks > lastTicks) {
            // the pool also ran down between the two samples, by the frame that passed (a zone-in is not a frame)
            // -- unless it was full : a full pool does not run
            unsigned dms = nowMs - lastMs; if (dms > 2000u) dms = 2000u;
            int ran = (int)(dms * 60u / 1000u); if (ran > lastTicks) ran = lastTicks;
            const int jump = ticks - lastTicks + ran;
            if (pendCost) { changed = take(jump, pendCost); pendCost = 0; }
            else if (pendJump) pendJump = -1;
            else { pendJump = jump; pendMs = nowMs; }
        }
        lastTicks = ticks; lastMs = nowMs;
        return changed;
    }
    // the pet's Ready move landed (its cost in charges) ; true when `learned` changed
    bool move(int cost, unsigned nowMs) {
        if (quiet(nowMs)) return false;
        if (pendJump > 0) { const int j = pendJump; pendJump = 0; return take(j, cost); }
        if (pendJump < 0) { pendJump = 0; return false; }
        pendCost = cost; costMs = nowMs;
        return false;
    }
    // Sic (job ability 72) shares the pool : its jump and the move the pet then picks on its own teach nothing
    void sic(unsigned nowMs) { quietUntil = (nowMs + PAIR_MS) | 1u; pendJump = 0; pendCost = 0; }
    bool quiet(unsigned nowMs) const { return quietUntil && (int)(quietUntil - nowMs) > 0; }
    bool take(int jump, int cost) {
        const int s = ready_len_from_jump(jump, cost);
        if (!s || s == learned) return false;
        learned = s;
        return true;
    }
};

} // namespace aio
