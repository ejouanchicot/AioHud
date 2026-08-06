// party_state_empypop.cpp -- EmpyPop module : resolve a tracked Abyssea empyrean NM's pop chain against live
// memory. See party_state.h for the EmpyPop struct, nms_gen.h for the (generated, BSD-3) NM table, and
// game-data/key-items.md + game-data/inventory.md for the two reads this hangs off.
//
// This is the C++ rewrite of the upstream `build()`/`add_subpops()` (Empy Pop Tracker, Xurion of Bismarck).
// Two deliberate departures from the Lua :
//   1. It emits DATA, not pre-coloured text lines -- model must not know about rendering (layering rule).
//   2. The recursion is bounded and CLAMPED : fixed-capacity arrays, no heap, no unbounded descent.
#include "model/party_state.h"
#include "model/game_mem.h"       // owns_key_item / count_item / count_items / refresh_items
#include "model/nms_gen.h"        // NMS / POPS / nm_by_key  (generated)
#include "model/keyitems_gen.h"   // key_item_name           (generated)
#include "model/itemnames_gen.h"  // item_name               (generated)
#include <windows.h>              // GetTickCount
#include <string.h>               // strcmp

namespace aio {

// How many copies of `id` are sitting in the treasure pool. The pool is packet-fed (0x0D2/0x0D3) and holds ONE
// item per slot -- no count field (see TreasureItem) -- so N copies occupy N slots : count slots, don't sum.
static int ep_pool_count(const TreasureItem* pool, unsigned id) {
    int c = 0;
    for (int i = 0; i < 10; ++i) if (pool[i].itemId == id) ++c;
    return c;
}

// ---- DEMO inventory (the `sample` path : config preview / Help / edit placeholder) --------------------------
// Ported from the upstream M.sample() (modules/empypop.lua) : CHLORIS holding key items 1470 + 1471 (2 of its 4
// global pops), one fake item + one item sitting in the treasure pool + 30/50 of the collectable -> some groups
// green, some red, one orange [n], NOT ready. Exactly the shape that shows every state of the widget at once.
// It lives HERE (model) because assembling an EmpyPop means reading nms_gen.h, and ui/ must not include the
// generated tables. Being pure data, the preview NEVER touches game memory -- it renders the same on a fresh
// character who owns nothing.
static const unsigned short SMP_KI[]   = { 1470, 1471 };            // Gory Scorpion Claw + Mossy Adamantoise Shell
static const struct { unsigned short id; unsigned count; } SMP_ITEM[] = { {2917, 1}, {2928, 30} };
static const unsigned short SMP_POOL[] = { 2919 };                  // one Blood Bat drop waiting in the pool
static unsigned ep_smp_count(unsigned short id) {
    for (int i = 0; i < (int)(sizeof(SMP_ITEM) / sizeof(SMP_ITEM[0])); ++i) if (SMP_ITEM[i].id == id) return SMP_ITEM[i].count;
    return 0;
}
static int ep_smp_pool(unsigned short id) {
    int c = 0;
    for (int i = 0; i < (int)(sizeof(SMP_POOL) / sizeof(SMP_POOL[0])); ++i) if (SMP_POOL[i] == id) ++c;
    return c;
}
static bool ep_smp_owned(unsigned short id, bool isKI) {
    if (!isKI) return ep_smp_count(id) != 0;
    for (int i = 0; i < (int)(sizeof(SMP_KI) / sizeof(SMP_KI[0])); ++i) if (SMP_KI[i] == id) return true;
    return false;
}

// Append POPS[idx] and its whole sub-chain, depth-first, into e.nodes. DFS order + the per-node depth is what
// lets the widget draw the indented chain with a flat loop. Returns false once the node pool is full : the
// caller stops rather than writing past the end (a regenerated table that grew past MAX_NODES degrades to a
// truncated chain, never to corruption). `sample` swaps the two memory reads for the demo inventory above --
// the ONE recursion serves both paths, so the preview can never drift from the live box.
static bool ep_add_chain(EmpyPop& e, const TreasureItem* pool, int idx, int depth, bool sample) {
    if (e.nNodes >= EmpyPop::MAX_NODES) return false;
    if (idx < 0 || idx >= POPS_N) return true;                 // generated indices are sound ; belt-and-braces
    const NmPop& p = POPS[idx];

    EmpyPopNode& n = e.nodes[e.nNodes++];
    n.id       = p.id;
    n.isKI     = p.isKI;
    n.depth    = (unsigned char)(depth > 255 ? 255 : depth);
    n.fromName = p.fromName;
    n.name     = p.isKI ? key_item_name(p.id) : item_name(p.id);   // 0 = unknown id -> widget shows a placeholder
    // A key item is a flag (you have it or you don't) ; an item is counted, and may also be sitting unclaimed
    // in the pool -- which is NOT ownership, so it stays a separate signal the widget can surface.
    if (sample) { n.owned = ep_smp_owned(p.id, p.isKI != 0) ? 1 : 0; n.pool = p.isKI ? 0 : (unsigned char)ep_smp_pool(p.id); }
    else        { n.owned = p.isKI ? (owns_key_item(p.id) ? 1 : 0) : (count_item(p.id) ? 1 : 0);
                  n.pool  = p.isKI ? 0 : (unsigned char)ep_pool_count(pool, p.id); }

    for (int i = 0; i < p.childCount; ++i)
        if (!ep_add_chain(e, pool, p.childFirst + i, depth + 1, sample)) return false;
    return true;
}

// Resolve `nm`'s groups + collectable into `e`. Shared by the LIVE refresh and the demo sample (`sample` = the
// fake inventory above) so both produce byte-identical shapes. Assumes e is freshly zeroed and its key set.
static void ep_build(EmpyPop& e, const Nm* nm, const TreasureItem* pool, bool sample) {
    e.nmName = nm->en;
    e.allDone = true;   // rule10-ok: a data field of the row being built (cleared below per group), not a latch
    const int ng = (nm->popCount < EmpyPop::MAX_GROUPS) ? nm->popCount : EmpyPop::MAX_GROUPS;
    for (int g = 0; g < ng; ++g) {
        const int first = e.nNodes;
        if (!ep_add_chain(e, pool, nm->popFirst + g, 0, sample)) { /* node pool full -> keep what fits */ }
        if (e.nNodes == first) break;               // nothing added (pool was already full) -> stop
        EmpyPopGroup& grp = e.groups[e.nGroups++];
        grp.first    = (unsigned char)first;
        grp.count    = (unsigned char)(e.nNodes - first);
        grp.obtained = e.nodes[first].owned;        // node[first] IS the global pop -> the group's verdict
        if (!grp.obtained) e.allDone = false;
    }
    if (e.nGroups == 0) e.allDone = false;          // no groups resolved -> never claim READY

    if (nm->collectable) {
        e.hasColl    = true;
        e.collId     = nm->collectable;
        e.collTarget = nm->collectableTarget;
        e.collCount  = sample ? ep_smp_count(nm->collectable) : count_item(nm->collectable);
        e.collPool   = sample ? (unsigned char)ep_smp_pool(nm->collectable) : (unsigned char)ep_pool_count(pool, nm->collectable);
        e.collDone   = e.collTarget && e.collCount >= e.collTarget;
    }
    e.valid = true;
}

// The demo chain (see SMP_* above). No memory read, no throttle : the caller (the widget) builds it ONCE.
void ep_build_sample(EmpyPop& out) {
    out = EmpyPop{};
    const Nm* nm = nm_by_key("chloris");
    if (!nm) return;                                // generated table lost chloris -> invalid sample, widget skips
    lstrcpynA(out.key, "chloris", sizeof(out.key));
    ep_build(out, nm, 0, true);                     // pool = 0 : the sample path never dereferences it
}

// Rebuild the tracked NM's chain from live memory. Self-throttled : the item snapshot is a ~101 KB block copy,
// and the upstream addon settled on the same 2 Hz, which is far quicker than a pop item can actually change.
void PartyState::ep_refresh(const char* nmKey) {
    if (!nmKey) nmKey = "";
    const bool keyChanged = strcmp(nmKey, ep_.key) != 0;
    const unsigned now = GetTickCount();
    static unsigned s_lastMs = 0;
    // Rebuild NOW on a key change (the user just picked an NM -- a stale box for half a second reads as a bug),
    // otherwise obey the throttle. Throttle on TIME, not on ep_.valid : an unknown key leaves valid false, and
    // gating on it would rebuild every frame forever. GetTickCount wraps every ~49 days : the unsigned diff handles it.
    if (!keyChanged && s_lastMs && (now - s_lastMs) < 500) return;
    s_lastMs = now;

    ep_ = EmpyPop{};
    lstrcpynA(ep_.key, nmKey, sizeof(ep_.key));    // COPY -- see the struct comment
    if (!*nmKey) return;                           // nothing tracked -> valid stays false, widget draws nothing
    const Nm* nm = nm_by_key(nmKey);
    if (!nm) return;                               // unknown key (stale config / renamed NM) -> same

    // KEEP the last good chain when the container snapshot fails : count_item() then returns 0 for everything, and
    // ep_build would render an authoritative "you own 0 of the pop items" for a tracked NM. An unreadable container
    // is not the same answer as owning nothing (rule 10, form B).
    if (!refresh_items()) return;                  // ONE guarded block copy ; every count_item below hits the snapshot
    ep_build(ep_, nm, treasure_, false);
}

} // namespace aio
