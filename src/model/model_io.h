// model_io.h -- the model's reads of the OUTSIDE world, in one place : game memory seams and an observer hook.
//
// WHY. The packet handlers and the per-frame upkeep read game memory directly (safe_read on raw pointers, and the
// game_mem functions) and could only ever be exercised inside the running game. Routing those reads through named
// seams makes the model's inputs explicit : a development build can observe them (and feed them back offline),
// while a release build pays one predictable branch per read.
//
// THE OBSERVER. tape_recording() / tape_note() / tape_event_* / tape_packet() / tape_frame() are hooks. In a release
// build they are no-ops defined in model_io.cpp ; a development build (AIOHUD_DEVTOOLS) provides the real ones.
// Every observed read names its function below. APPEND ONLY : recorded data stores these numbers.
#pragma once
#include "windower.h"
#include <cstdint>

namespace aio {

using windower::u32;

enum TapeFn : uint16_t {
    TF_READ_U32 = 1, TF_COPY, TF_MODULE_BASE,
    TF_COUNT_ITEM, TF_COUNT_ITEMS, TF_DATA_ROOT, TF_ENTITY_ARRAY, TF_ENTITY_ID_BY_INDEX, TF_ENTITY_NAME_BY_INDEX,
    TF_ENTITY_POS_VERIFIED, TF_KEY_ITEMS_BASE, TF_OWNS_KEY_ITEM, TF_PARTY_PTR, TF_READ_CAPACITY_POINTS,
    TF_READ_ENTITIES_BY_ID, TF_READ_EQUIPMENT_EXT, TF_READ_JP_GIFT_RANK, TF_READ_JP_U8, TF_READ_MERIT_LEVEL,
    TF_READ_PLAYER, TF_READ_PLAYER_BUFFS, TF_READ_POINTWATCH, TF_READ_TREASURE_POOL, TF_REFRESH_ITEMS,
    TF_SELF_PARTY_BASE, TF_ZONE_ID,
    TF_COUNT_
};

// ---- the seams the model reads through ----
bool model_read_u32(u32 addr, u32* out);             // windower::safe_read
bool model_copy(u32 addr, void* out, unsigned n);    // one SEH-guarded block copy
u32  model_module_base(const char* name);            // GetModuleHandleA, as an address (0 = not loaded)

// ---- the observer hooks (no-ops in a release build) ----
bool tape_recording();
void tape_note(uint16_t fn, u32 a, u32 b, int ret, const void* data, unsigned len);   // one read and its result
void tape_event_begin(char kind, unsigned tickMs, long long unixTime);                // a model event starts (model_clock.h)
void tape_event_end();
void tape_packet(int id, const unsigned char* b);                                     // the packet of the current event
void tape_frame(const void* input, unsigned len);                                     // the upkeep inputs of the current event

// FNV-1a, for the call arguments a key cannot hold as numbers.
inline u32 tape_hash(const void* p, unsigned n) {
    const unsigned char* c = (const unsigned char*)p; u32 h = 2166136261u;
    for (unsigned i = 0; i < n; ++i) { h ^= c[i]; h *= 16777619u; }
    return h;
}

} // namespace aio
