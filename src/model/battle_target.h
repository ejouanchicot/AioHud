// battle_target.h -- <bt>, resolved the way the GAME resolves it. Pure : the memory walk lives in game_mem.cpp.
//
// WHY. <bt> is not stored anywhere. Reversed 2026-09-13 (static analysis, decompile and disassembly agreeing) :
//   * FFXiMain's target-token parser (dump RVA 0x7A0F0) sends "<bt>" to a resolver (dump RVA 0x77CB0) that gathers the
//     ids of YOU and your PARTY (not the alliance), walks the entity array in INDEX order, and returns the FIRST entity
//     that has an actor loaded (+0xA0 != 0), is claimed (+0x188) by one of those ids, and is not dead (+0x170 not 2/3).
//   * Windower's get_mob_by_target('bt') recomputes it too, with three differences : the whole ALLIANCE in zone counts
//     as claimers, dead claimed mobs are still returned, and only entities within 50 yalms qualify.
// AioHUD used to read target_t+0x7C, which follows the reticle target -- measured in combat : Windower <bt> was the
// Sand Hare claimed by Tetsouo while +0x7C held the Goblin Mugger under his cursor. This copies the GAME's rule,
// since that is what a <bt> macro aims at.
#pragma once

namespace aio {

struct BtEntity {
    unsigned id = 0, claim = 0, status = 0;
    bool     actor = false;   // +0xA0 non-null : the game skips an entity without it
};

// Src : int bt_count() const ; bool bt_read(int index, BtEntity& e) const -- false = empty or unreadable slot, skipped.
// partyIds : you and your party members (trusts included), n of them. Returns the <bt> server id, 0 = none.
template <class Src>
inline unsigned battle_target_pick(const unsigned* partyIds, int n, const Src& src) {
    if (!partyIds || n <= 0) return 0;
    const int count = src.bt_count();
    for (int i = 0; i < count; ++i) {                  // INDEX order : the game takes the first, not the nearest
        BtEntity e;
        if (!src.bt_read(i, e) || !e.claim) continue;  // unclaimed : the cheapest reject, and nearly every entity
        int k = 0;
        while (k < n && partyIds[k] != e.claim) ++k;
        if (k == n) continue;                          // claimed by someone outside your party
        if (!e.actor) continue;
        if (e.status == 2 || e.status == 3) continue;  // dead
        return e.id;
    }
    return 0;
}

} // namespace aio
