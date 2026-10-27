// bst_state.cpp -- the Beastmaster module's packet side (model_feed_packet -> bst_on_packet) and the state kept
// between frames : our pet from the 0x068 packets, the cost of each Ready move it lands (0x028 category 11) for the
// charge-length learner, and the learned length cached per character. SPEC docs/design/bst-module-spec.md.
//
// 0x068 Pet Status (Windower addons/libs/packets/fields.lua ; documented in docs/game-data/target/hate-list.md) :
//   +0x08 owner id (u32)  +0x0C pet index (u16)  +0x0E HP% (u8)  +0x0F MP% (u8)  +0x10 TP (u32)  +0x14 target id
//   +0x18 pet name (char*, to the end of the packet). Pet index 0 = the pet is gone.
#include "model/bst_state.h"
#include "model/game_mem.h"               // entity_id_by_index
#include "model/party_state.h"            // model_decode_action
#include "model/party_state_internal.h"   // pkt_u16 / pkt_u32 / pkt_short / pkt_bytes
#include "model/model_clock.h"
#include "model/paths.h"
#include "model/abilities_gen.h"
#include "model/mobskills_gen.h"
#include <stdio.h>
#include <string.h>

namespace aio {

static unsigned     g_meId = 0;
static BstPet       g_pet;
static ReadyLearner g_learner;

void          bst_set_me(unsigned meId) { g_meId = meId; }
const BstPet& bst_pet() { return g_pet; }
void          bst_pet_clear() { g_pet = BstPet{}; }
void          bst_pet_adopt(unsigned id) { g_pet.id = id; }
void          bst_pet_seed(unsigned index) { g_pet = BstPet{}; g_pet.index = index; g_pet.seenMs = model_now_ms() | 1u; }
void          bst_pet_fill(const char* name, int hpp) { strncpy(g_pet.name, name, sizeof(g_pet.name) - 1); g_pet.name[sizeof(g_pet.name) - 1] = 0; g_pet.hpp = hpp; }
ReadyLearner& bst_learner() { return g_learner; }

// The Ready moves share the Ready recast (102) ; Sic (72) shares it too but is not a move
int bst_ready_cost(const char* en) {
    if (!en || !en[0]) return 0;
    for (int i = 0; i < ABILS_N; ++i)
        if (ABILS[i].recast_id == 102 && ABILS[i].id != 72 && strcmp(ABILS[i].en, en) == 0) return (int)ABILS[i].mp;
    return 0;
}

static void on_pet_status(const unsigned char* p) {
    if (pkt_short(p, 0x18)) return;
    if (!g_meId || pkt_u32(p, 0x08) != g_meId) return;   // another player's pet
    const unsigned idx = pkt_u16(p, 0x0C);
    if (!idx) { bst_pet_clear(); return; }
    if (idx != g_pet.index) { g_pet = BstPet{}; g_pet.index = idx; }
    const unsigned eid = entity_id_by_index(idx);
    if (eid) g_pet.id = eid;                             // 0 while the entity is not spawned yet : compute_bst adopts it
    g_pet.seenMs = model_now_ms() | 1u;
    g_pet.hpp = p[0x0E]; g_pet.mpp = p[0x0F];
    g_pet.tp  = (int)pkt_u32(p, 0x10);
    const int len = pkt_bytes(p) - 0x18;
    if (len > 0) {
        int n = len < (int)sizeof(g_pet.name) - 1 ? len : (int)sizeof(g_pet.name) - 1;
        memcpy(g_pet.name, p + 0x18, n); g_pet.name[n] = 0;
    }
}

// a Ready move our pet landed : 0x028 category 11 (mob_tp_finish), actor = the pet, param = its mob-skill id. The
// mob skill and the job ability carry the same English name (Fluid Toss : mob skill 432, ability 792).
static void on_action(const unsigned char* p) {
    ActionDecode a;
    if (!model_decode_action(p, a)) return;
    if (a.category == 6 && g_meId && a.actor == g_meId && a.param == 72) { g_learner.sic(model_now_ms()); return; }   // our Sic
    if (!g_pet.id || a.category != 11 || a.actor != g_pet.id) return;
    const MobSkillRow* ms = mobskill_info(a.param);
    const int cost = ms ? bst_ready_cost(ms->en) : 0;
    if (cost > 0 && g_learner.move(cost, model_now_ms()) && g_learner.learned) bst_cache_save(g_meId, g_learner.learned);
}

void bst_on_packet(int id, const unsigned char* b) {
    if (id == 0x068) on_pet_status(b);
    else if (id == 0x028) on_action(b);
    else if (id == 0x00B) bst_pet_clear();               // zone-out : the pet is re-announced after the zone
}

static bool cache_path(unsigned meId, char* out, int cap) {
    if (!meId) return false;                             // no character resolved : never touch a shared file
    char rel[80]; _snprintf(rel, sizeof(rel), "data\\cache\\bst_%08X.bin", meId); rel[sizeof(rel) - 1] = 0;
    plugin_path(out, cap, rel);
    return out[0] != 0;
}
int bst_cache_load(unsigned meId) {
    char path[300]; if (!cache_path(meId, path, sizeof(path))) return 0;
    FILE* f = fopen(path, "rb"); if (!f) return 0;
    int v = 0; const size_t n = fread(&v, sizeof(v), 1, f); fclose(f);
    return (n == 1 && v >= READY_LEN_MIN && v <= READY_LEN_MAX) ? v : 0;
}
void bst_cache_save(unsigned meId, int len) {
    char path[300]; if (!cache_path(meId, path, sizeof(path))) return;
    FILE* f = fopen(path, "wb"); if (!f) return;
    fwrite(&len, sizeof(len), 1, f); fclose(f);
}

} // namespace aio
