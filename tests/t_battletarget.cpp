// t_battletarget.cpp -- <bt> as the game resolves it (model/battle_target.h). Each case is one clause of the resolver
// reversed on 2026-09-13 ; unit_mutate.py removes each clause in turn and requires its case to fail.
#include "check.h"
#include "model/battle_target.h"

using namespace aio;

namespace {
const unsigned ME = 0x0006BD0Bu, KAO = 0x00016500u, STRANGER = 0x00099999u;

struct FakeEntities {
    BtEntity e[16];
    bool readable[16];
    int n = 0;
    void add(unsigned id, unsigned claim, unsigned status = 1, bool actor = true, bool ok = true) {
        e[n].id = id; e[n].claim = claim; e[n].status = status; e[n].actor = actor; readable[n] = ok; ++n;
    }
    int bt_count() const { return n; }
    bool bt_read(int i, BtEntity& out) const { if (!readable[i]) return false; out = e[i]; return true; }
};
const unsigned PARTY[2] = { ME, KAO };
}  // namespace

void test_battle_target() {
    SECTION("<bt> : nothing claimed by the party -> no battle target");
    {
        FakeEntities w; w.add(0x010670FEu, 0); w.add(0x01067104u, STRANGER);
        CHECK_EQ(battle_target_pick(PARTY, 2, w), 0u);
    }

    SECTION("<bt> : the mob YOU claimed, whatever your cursor is on (the measured Sand Hare case)");
    {
        FakeEntities w; w.add(0x01067104u, 0); w.add(0x010670FBu, ME, 1);
        CHECK_EQ(battle_target_pick(PARTY, 2, w), 0x010670FBu);
    }

    SECTION("<bt> : a mob a PARTY MEMBER claimed is your <bt> too");
    {
        FakeEntities w; w.add(0x010670FBu, KAO, 1);
        CHECK_EQ(battle_target_pick(PARTY, 2, w), 0x010670FBu);
    }

    SECTION("<bt> : claimed by someone outside the party (another party of the alliance) -> not yours");
    {
        FakeEntities w; w.add(0x010670FBu, STRANGER, 1);
        CHECK_EQ(battle_target_pick(PARTY, 2, w), 0u);
    }

    SECTION("<bt> : a dead claimed mob (status 2 or 3) is skipped");
    {
        FakeEntities w; w.add(0x010670F0u, ME, 2); w.add(0x010670F1u, ME, 3);
        CHECK_EQ(battle_target_pick(PARTY, 2, w), 0u);
        w.add(0x010670F2u, ME, 1);
        CHECK_EQ(battle_target_pick(PARTY, 2, w), 0x010670F2u);
    }

    SECTION("<bt> : an entity without an actor is skipped");
    {
        FakeEntities w; w.add(0x010670F0u, ME, 1, false); w.add(0x010670F1u, ME, 1, true);
        CHECK_EQ(battle_target_pick(PARTY, 2, w), 0x010670F1u);
    }

    SECTION("<bt> : two claimed mobs -> the FIRST by entity index, not the nearest or the newest");
    {
        FakeEntities w; w.add(0x010670A0u, KAO, 1); w.add(0x010670B0u, ME, 1);
        CHECK_EQ(battle_target_pick(PARTY, 2, w), 0x010670A0u);
    }

    SECTION("<bt> : an unreadable slot is skipped, the walk goes on");
    {
        FakeEntities w; w.add(0x010670A0u, ME, 1, true, false); w.add(0x010670B0u, ME, 1);
        CHECK_EQ(battle_target_pick(PARTY, 2, w), 0x010670B0u);
    }

    SECTION("<bt> : no party ids -> nothing can be claimed by the party");
    {
        FakeEntities w; w.add(0x010670B0u, ME, 1);
        CHECK_EQ(battle_target_pick(PARTY, 0, w), 0u);
        CHECK_EQ(battle_target_pick(0, 2, w), 0u);
    }
}
