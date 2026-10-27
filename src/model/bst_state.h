// bst_state.h -- the Beastmaster module's packet side and the state it keeps between frames (model/bst_state.cpp).
// The per-frame part (recast, entity, equipment reads -> GameState::bst) is compute_bst, in game_mem.cpp beside
// compute_grimoire. SPEC docs/design/bst-module-spec.md.
#pragma once
#include "model/ready_charges.h"

namespace aio {

struct GameState;

// Our pet, as the 0x068 Pet Status packets describe it (owner = us). Cleared when a 0x068 says no pet, on a zone,
// or when its entity is no longer the pet (compute_bst).
struct BstPet {
    unsigned id = 0, index = 0;   // server id (from the entity array) and entity index (0x068 +0x0C)
    char name[24] = {};           // 0x068 +0x18
    int  hpp = 0, mpp = 0, tp = 0;
    unsigned seenMs = 0;          // the last 0x068 : an entity not there yet is waited for a little (compute_bst)
};

void          bst_set_me(unsigned meId);            // the local player's server id, once a frame (the 0x068 owner check)
void          bst_on_packet(int id, const unsigned char* b);   // 0x068 (our pet) and 0x028 (its Ready move landed)
const BstPet& bst_pet();
void          bst_pet_clear();
void          bst_pet_adopt(unsigned id);          // the entity is there now : its server id
void          bst_pet_seed(unsigned index);        // a pet seen in memory before any 0x068 (plugin loaded with the pet out)
void          bst_pet_fill(const char* name, int hpp);   // its name and HP% read from its entity, once (the next 0x068 refreshes them)
ReadyLearner& bst_learner();
int           bst_ready_cost(const char* en);       // charges a Ready move costs, by name (abilities_gen.h), 0 if unknown
// The learned charge length, kept per character (data\cache\bst_%08X.bin, the per-character cache rules) : read when
// the character changes, written when a use teaches a new value. 0 = none.
int           bst_cache_load(unsigned meId);
void          bst_cache_save(unsigned meId, int len);

void compute_bst(GameState& gs);   // game_mem.cpp

} // namespace aio
