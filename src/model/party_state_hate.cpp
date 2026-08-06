// party_state_hate.cpp -- Hate List module (mobs with aggro on you / your party),
// split out of party_state.cpp. PURE MOVE : refresh_hate (per-frame row build), the friendly-pet
// registry (is_party_or_pet / pet_owner / on_pet_info 0x067 / on_pet_status 0x068 + register_pet),
// and the record_hate enmity tracker (now external : on_action in party_state.cpp also calls it).
#include "model/party_state.h"
#include "model/party_state_internal.h"   // pkt_u16 / pkt_u32 + record_hate declaration
#include "model/game_mem.h"               // read_entities_by_id / entity_id_by_index / entity_array / EntityVitals
#include <windows.h>                        // GetTickCount
#include <math.h>                           // sqrtf

namespace aio {

// HATE LIST : build the display rows once per frame from a SINGLE, STABLE source -- the sticky 0x028 enmity tracker
// hate_[] (fed by record_hate on any friendly<->mob action, MELEE and PETS included ; enemybar2's tracked_enmity
// model). Membership no longer comes from a per-frame memory CLAIM SCAN, which popped mobs in/out as their claim or
// distance flickered -> the "epileptic" list in 100+ mob pulls (Crawlers' Nest [S]). Each frame we only READ the
// tracked mobs' live vitals to sort them, and prune the truly-gone (idle > 3 s AND dead/idle/far, the addon's 1 Hz
// prune). Rows are reduced to the LOWEST-HP mobs, sorted HP-ascending (id tie-broken -> deterministic).
// Uses selfX_/selfZ_ (load_from_memory) + curTarget_ (set_target_ctx) -> call AFTER both, each frame.
void PartyState::refresh_hate() {
    const unsigned now = GetTickCount();

    auto add_row = [&](const EntityVitals& v, unsigned pcId) {
        HateRow r{};
        r.id = v.id; r.hpp = v.hpp;
        const float dx = v.x - selfX_, dz = v.z - selfZ_; r.dist = sqrtf(dx * dx + dz * dz);
        r.red = (v.claimId != 0 && !is_party_or_pet(v.claimId)) ? 1 : 0;   // contested = claimed by a non-party/pet entity
        r.target = (curTarget_ && v.id == curTarget_) ? 1 : 0;
        int j = 0; for (; j < 23 && v.name[j]; ++j) r.mob[j] = v.name[j]; r.mob[j] = 0;
        const char* pn = pc_name_by_id(pcId ? pcId : v.claimId);
        if (pn) { int k = 0; for (; k < 19 && pn[k]; ++k) r.pc[k] = pn[k]; r.pc[k] = 0; }
        // Keep only the LOWEST-HP mobs : with more live aggro than row slots, evict the WORST kept row (greatest by
        // the total order (hpp, id)) -> the displayed set is the unique lowest-HP mobs, identical every frame.
        if (hateN_ < 24) { hateRows_[hateN_++] = r; return; }
        int hi = 0;
        for (int k = 1; k < 24; ++k)
            if (hateRows_[k].hpp > hateRows_[hi].hpp || (hateRows_[k].hpp == hateRows_[hi].hpp && hateRows_[k].id > hateRows_[hi].id)) hi = k;
        if (r.hpp < hateRows_[hi].hpp || (r.hpp == hateRows_[hi].hpp && r.id < hateRows_[hi].id)) hateRows_[hi] = r;
    };

    hateN_ = 0;
    // SINGLE SOURCE : membership is the sticky tracker hate_[] only. Batch-read the tracked mobs' live vitals, prune
    // the truly-gone (idle > 3 s AND dead/idle/far), then turn every still-live tracked mob into a display row.
    // NO per-frame distance gate on DISPLAY (only the prune culls by distance) -> a mob flickering across 50 yalms
    // no longer pops in and out. hate_[] holds up to 128 aggroing mobs (a Crawlers' Nest [S] pull) so the tracked
    // set doesn't thrash from ring eviction while you AoE.
    unsigned ids[128]; int idx[128]; int nq = 0;
    for (int i = 0; i < 128; ++i) if (hate_[i].mob) { ids[nq] = hate_[i].mob; idx[nq] = i; ++nq; }
    if (nq > 0) {
        static EntityVitals ev[128];   // ~7 KB kept off the per-frame stack (single-threaded, once/frame)
        const int got = read_entities_by_id(ids, nq, ev);
        // Rule 10 : read_entities_by_id answers 0 both for "the entity array is not mapped right now" (zoning, a
        // re-map window) and for "none of these ids are live". Taking the first for the second purges every tracked
        // mob idle > 3 s -- so a mob holding aggro but not acting vanished from the list on a single missed read.
        // A miss is UNKNOWN : keep what we have and re-ask next frame. reconcile_treasure already does exactly this.
        if (got == 0 && !entity_array()) return;
        for (int q = 0; q < nq; ++q) {
            HateEntry& he = hate_[idx[q]];
            const EntityVitals& v = ev[q];
            if (v.valid && v.hpp <= 0 && v.id) clear_debuffs(v.id);   // a tracked mob died -> drop its debuffs (covers AoE / mobs killed while not your <t>, before the id is recycled)
            const bool stale = (now - he.lastMs) > 3000u;
            if (stale) {   // prune only once idle > 3 s : gone / dead / not-a-mob / idle-status / > 50 yalms
                if (!v.valid || v.hpp <= 0 || v.spawnType != 0x10 || v.status == 0) { he = HateEntry{}; continue; }
                const float dx = v.x - selfX_, dz = v.z - selfZ_; if (sqrtf(dx * dx + dz * dz) > 50.0f) { he = HateEntry{}; continue; }
            }
            if (v.valid && v.spawnType == 0x10 && v.hpp > 0) add_row(v, he.pc);   // a live tracked mob -> a display row
        }
    }
    // sort by HP ascending (lowest-HP mob first / top of the box) ; tie-break by mob id so equal-HP mobs keep a
    // DETERMINISTIC order (the scan order is unstable, so without this two mobs at the same HP swap every frame).
    for (int a = 1; a < hateN_; ++a) { HateRow t = hateRows_[a]; int b = a - 1;
        while (b >= 0 && (hateRows_[b].hpp > t.hpp || (hateRows_[b].hpp == t.hpp && hateRows_[b].id > t.id))) { hateRows_[b + 1] = hateRows_[b]; --b; }
        hateRows_[b + 1] = t; }
}

// HATE LIST : friendly-pet registry (fed by 0x067/0x068). Keeps the owning PC so the Target column + red-claim
// test resolve the OWNER, not the pet. Reuses an existing slot / a free one / slot 0 when full.
static void register_pet(unsigned* ids, unsigned* owners, unsigned petId, unsigned ownerId) {
    if (!petId) return;
    for (int i = 0; i < 16; ++i) if (ids[i] == petId) { owners[i] = ownerId; return; }
    for (int i = 0; i < 16; ++i) if (!ids[i]) { ids[i] = petId; owners[i] = ownerId; return; }
    ids[0] = petId; owners[0] = ownerId;
}

bool PartyState::is_party_or_pet(unsigned id) const {
    if (!id) return false;
    if (id == selfId_) return true;
    for (int i = 0; i < 16; ++i) if (petId_[i] == id) return true;   // our pets (pet ids are >= 0x01000000)
    if (id >= 0x01000000u) return false;                            // any other non-PC id is not friendly
    for (int i = 0; i < count; ++i) if (m[i].id == id) return true;
    for (int t = 0; t < 2; ++t) for (int i = 0; i < alliN_[t]; ++i) if (alli_[t * 6 + i].id == id) return true;
    return false;
}
unsigned PartyState::pet_owner(unsigned id) const {
    for (int i = 0; i < 16; ++i) if (petId_[i] == id) return petOwner_[i] ? petOwner_[i] : id;
    return id;
}
void PartyState::on_pet_info(const unsigned char* p) {     // 0x067 Pet Info : Pet ID @+0x08, Owner Index @+0x0C
    if (pkt_bytes(p) < 0x0E) return;                       // floor on the highest field read (owner index @0x0C..0x0D)
    const unsigned petId = pkt_u32(p, 0x08), ownerIdx = pkt_u16(p, 0x0C);
    if (!petId || !ownerIdx) return;
    const unsigned ownerId = entity_id_by_index(ownerIdx);
    if (ownerId && is_party_or_pet(ownerId)) register_pet(petId_, petOwner_, petId, ownerId);
}
void PartyState::on_pet_status(const unsigned char* p) {   // 0x068 Pet Status : Owner ID @+0x08, Pet Index @+0x0C, Target ID @+0x14
    if (pkt_bytes(p) < 0x18) return;                       // floor on the highest field read (target id @0x14..0x17)
    const unsigned ownerId = pkt_u32(p, 0x08);
    if (!ownerId || !is_party_or_pet(ownerId)) return;
    const unsigned petIdx = pkt_u16(p, 0x0C);
    if (petIdx) { const unsigned pid = entity_id_by_index(petIdx); if (pid) register_pet(petId_, petOwner_, pid, ownerId); }
    const unsigned tgt = pkt_u32(p, 0x14);
    if (tgt >= 0x01000000u) record_hate(hate_, tgt, ownerId);   // the pet's current target = a mob aggro'd on our party
}

// HATE LIST : record/refresh the mob->PC enmity pair (find its slot / a free one / the oldest). lastMs drives
// the staleness prune in refresh_hate. Mirrors the reference addon's tracked[] table (fed from the 0x028).
void record_hate(HateEntry* h, unsigned mob, unsigned pc) {
    const unsigned now = GetTickCount();
    int freeS = -1, oldest = 0;
    for (int i = 0; i < 128; ++i) {
        if (h[i].mob == mob) { h[i].pc = pc; h[i].lastMs = now; return; }
        if (!h[i].mob && freeS < 0) freeS = i;
        if (h[i].lastMs < h[oldest].lastMs) oldest = i;
    }
    const int s = (freeS >= 0) ? freeS : oldest;
    h[s].mob = mob; h[s].pc = pc; h[s].lastMs = now;
}

} // namespace aio
