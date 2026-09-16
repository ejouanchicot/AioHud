// game_mem.cpp -- see game_mem.h.
//
// OBSERVED READS (model/model_io.h). The functions the MODEL calls are defined below under a `__real` name, and the
// public name at the bottom of this file is a thin wrapper : the real read, then the observer hook (a no-op in a
// release build). Calls made INSIDE this file resolve to the real functions (the macros below apply to them too),
// so only what the model and the HUD ask for is observed.
#define count_item count_item__real
#define count_items count_items__real
#define data_root data_root__real
#define entity_array entity_array__real
#define entity_id_by_index entity_id_by_index__real
#define entity_name_by_index entity_name_by_index__real
#define entity_pos_verified entity_pos_verified__real
#define key_items_base key_items_base__real
#define owns_key_item owns_key_item__real
#define party_ptr party_ptr__real
#define read_capacity_points read_capacity_points__real
#define read_entities_by_id read_entities_by_id__real
#define read_equipment_ext read_equipment_ext__real
#define read_jp_gift_rank read_jp_gift_rank__real
#define read_jp_u8 read_jp_u8__real
#define read_merit_level read_merit_level__real
#define read_player read_player__real
#define read_player_buffs read_player_buffs__real
#define read_pointwatch read_pointwatch__real
#define read_treasure_pool read_treasure_pool__real
#define refresh_items refresh_items__real
#define self_party_base self_party_base__real
#define zone_id zone_id__real

#include "model/game_mem.h"
#include "model/gamestate.h"
#include "model/ffximain_rva.h"   // the FFXiMain statics : addresses as data, re-derived after a client patch
#include "model/luacore_root.h"   // where LuaCore keeps `g` : a WINDOWER update moves it, so it is derived too
#include "model/sentinel.h"       // packet-vs-memory cross-check, run once the snapshot is complete
#include "model/capwatch.h"       // notice a fixed table that has quietly run out of room
#include "model/battle_target.h"  // <bt> : the game's resolver rule, walked over the entity array here
#include "model/charsheet.h"   // the sheet decoder, shared with the packet path
#include "model/party_state.h"    // the party ids <bt> is claimed by (the roster)
#include "model/ui_config.h"   // mmShow : skip the entity-array sweep entirely when the minimap is off (model->model, no layering issue)
#include "model/model_clock.h"    // //aio schlog : the model's one source of "now" (replayable offline)
#include "windower.h"   // safe_read / valid_ptr (guarded game-memory reads)
#include <windows.h>
#include <cstring>

#include "windower_debug.h"   // set_tag : every client of a multi-box appends to the SAME log file
namespace aio {

using windower::safe_read;

// --- game-memory anchors : ONE source of truth for the pointer chains every reader hangs off.
// Module bases are fixed after load (cached) ; the data root `g` is a heap ptr that can be 0 while
// zoning (read each call). All return 0 -> the caller no-ops. Offsets live HERE, nowhere else. ---
u32 ffximain_base() { static u32 b = 0; if (!b) b = (u32)GetModuleHandleA("FFXiMain.dll"); return b; }
u32 luacore_base()  { static u32 b = 0; if (!b) b = (u32)GetModuleHandleA("LuaCore.dll");  return b; }
// g = *(LuaCore + root rva). The RVA is NOT a constant : Windower recompiles LuaCore and updates itself
// silently, and 4.7.9.3 slid the root from 0x1C8400 to 0x1CA420 -- which zeroed every read in this file and
// blanked the whole HUD. lc_root_addr() derives it from LuaCore's own code (see model/luacore_root.h).
u32 data_root()   { u32 s = lc_root_addr(); u32 g = 0; return (s && safe_read(s, &g) && valid_ptr(g)) ? g : 0; }
u32 party_ptr()   { u32 g = data_root(); u32 pp = 0; return (g && safe_read(g + 0x248, &pp) && valid_ptr(pp)) ? pp : 0; }         // *(g+0x248) = &member[0]+4
u32 entity_array(){ u32 g = data_root(); u32 e  = 0; return (g && safe_read(g + 0x24,  &e)  && valid_ptr(e))  ? e  : 0; }         // *(g+0x24) = entity array
u32 key_items_base(){ u32 g = data_root(); u32 kb = 0; return (g && safe_read(g + 0x4C, &kb) && valid_ptr(kb)) ? kb : 0; }        // *(g+0x4C) = u8[0x2000], one BYTE per key-item id (game-data/key-items.md)
u32 items_root()  { u32 g = data_root(); u32 ir = 0; return (g && safe_read(g + 0x50,  &ir) && valid_ptr(ir)) ? ir : 0; }         // *(g+0x50) = item-container root (gil @+0x04)
u32 equip_index_arr(){ u32 g = data_root(); u32 a = 0; return (g && safe_read(g + 0x54, &a) && valid_ptr(a)) ? a : 0; }           // *(g+0x54) = u8[16] equip inventory index
u32 equip_bag_arr()  { u32 g = data_root(); u32 a = 0; return (g && safe_read(g + 0x58, &a) && valid_ptr(a)) ? a : 0; }           // *(g+0x58) = s32[16] equip bag id

// player = *(g + 0x3C).
u32 player_struct() {
    u32 g = data_root();
    u32 pl = 0;
    return (g && safe_read(g + 0x3C, &pl) && valid_ptr(pl)) ? pl : 0;
}

// read_player is hit ~5x per poll cycle (poll + self_entity + read_map_entities' self_entity +
// read_self_speed + load_from_memory), each ~30 SEH-guarded reads. Cache it for the DURATION OF ONE
// poll only : PlayerCacheScope (RAII, on the poll_game_state stack) turns caching on ; outside the poll
// (e.g. //aio probes) caching is off, so those always read fresh. Not thread-safe by design -- the game
// calls the plugin on one thread.
static bool       s_plCacheOn = false;
static bool       s_plValid   = false;
static PlayerInfo s_plCache;
struct PlayerCacheScope { PlayerCacheScope() { s_plCacheOn = true; s_plValid = false; } ~PlayerCacheScope() { s_plCacheOn = false; } };

bool read_player(PlayerInfo& o) {
    if (s_plCacheOn && s_plValid) { o = s_plCache; return true; }
    u32 pl = player_struct();
    if (!pl) return false;
    u32 mhp = 0; safe_read(pl + 0x60, &mhp);
    if (mhp == 0 || mhp > 0x100000) return false;
    u32 hp = 0, mp = 0, hpp = 0, mpp = 0, tp = 0, mj = 0, sj = 0, lv = 0, sv = 0;
    safe_read(pl + 0x5C, &hp);  safe_read(pl + 0x64, &hpp);
    safe_read(pl + 0x68, &mp);  safe_read(pl + 0x70, &mpp);
    safe_read(pl + 0x74, &tp);  safe_read(pl + 0x94, &mj);  safe_read(pl + 0x98, &lv);  safe_read(pl + 0x9C, &sj);   // job ids: u32 fields (main@+0x94, lvl@+0x98, sub@+0x9C)
    safe_read(pl + 0xA0, &sv);   // SUB-job level (u8, dword-spaced after subJob@+0x9C) -- already the DISPLAYED/capped value (e.g. 54 with Master levels raising the sub cap). Reversed via //aio jlvl.
    o.hp = (int)hp;  o.mp = (int)mp;  o.tp = (int)tp;
    o.hpp = hpp & 0xFF;  o.mpp = mpp & 0xFF;
    if (o.hpp > 100) o.hpp = 100;  if (o.mpp > 100) o.mpp = 100;  if (o.tp > 3000) o.tp = 3000;
    o.mjob = mj & 0xFF;  o.sjob = sj & 0xFF;  o.mlvl = lv & 0xFF;  o.slvl = sv & 0xFF;
    u32 id = 0; safe_read(pl + 0x00, &id); o.id = id;   // server id @+0x00
    int i = 0;                                          // name : ASCII at player+0x08
    for (; i < 19; ++i) { u32 c = 0; if (!safe_read(pl + 0x08 + i, &c)) break; char ch = (char)(c & 0xFF); if (!ch) break; o.name[i] = ch; }
    o.name[i] = 0;
    if (s_plCacheOn) { s_plCache = o; s_plValid = true; }   // seed the poll-cycle cache on the first successful read
    return true;
}

// The id-validated self party-block base : pp = *(g+0x248), the self block sits at pp-4 (tolerating a
// 0/-4 framing shift : some client states frame it at pp). Validated against `selfId` (player server id
// @ base+0x1C). ONE source of truth for the anchor -- shared by self_entity, read_self_speed, and
// party_state::load_from_memory. 0 if the party pointer is down or the id doesn't match. SEH-guarded.
u32 self_party_base(unsigned selfId) {
    u32 pp = party_ptr();
    if (!pp) return 0;
    u32 id0 = 0;
    if (safe_read(pp - 4 + 0x1C, &id0) && id0 == selfId) return pp - 4;
    if (safe_read(pp     + 0x1C, &id0) && id0 == selfId) return pp;   // tolerate a 0/-4 framing shift
    return 0;
}

// The local player's ENTITY struct pointer (position/heading live here, NOT in the player struct) :
// self party-block base+0x20 = entity index -> entity_array[idx]. SEH-guarded.
u32 self_entity() {
    PlayerInfo me; if (!read_player(me)) return 0;
    u32 base = self_party_base(me.id);
    if (!base) return 0;
    u32 ent = entity_array();
    if (!ent) return 0;
    u32 pidx = 0; safe_read(base + 0x20, &pidx); pidx &= 0xFFFF;
    if (!pidx || pidx >= 0x900) return 0;
    u32 p = 0; if (!safe_read(ent + pidx * 4, &p) || !valid_ptr(p)) return 0;
    return p;
}

// Current zone id : *(g+0x40) = the session/zone-info struct, zone u16 @+0x02 (Windower get_info().zone).
unsigned zone_id() {
    u32 g = data_root();
    if (!g) return 0;
    u32 zi = 0; if (!safe_read(g + 0x40, &zi) || !valid_ptr(zi)) return 0;
    u32 z = 0; safe_read(zi + 0x02, &z); return z & 0xFFFF;
}

// ---- Entity struct field offsets : ONE source of truth (rule 7) for both read_map_entities and
// read_target_entity, which read the same entity_array[idx] struct. Reversed via //aio tent / minimap
// probes ; see docs/game-data. (T0_EPTR_OFF below is a TARGET-system field, not an entity field.)
static const u32 ENT_X_OFF       = 0x04;    // float world X
static const u32 ENT_Z_OFF       = 0x0C;    // float world Z (Y @+0x08 = height, unused)
static const u32 ENT_HEADING_OFF = 0x18;    // float facing (radians)
static const u32 ENT_INDEX_OFF   = 0x74;    // u16 Index
static const u32 ENT_ID_OFF      = 0x78;    // u32 ServerId
static const u32 ENT_NAME_OFF    = 0x7C;    // ASCII, up to 0x18
static const u32 ENT_SPEED_OFF   = 0x98;    // float movement speed (base 5.0 = 0%)
static const u32 ENT_HPP_OFF     = 0xEC;    // u8, 0..100
static const u32 ENT_RENDER_OFF  = 0x120;   // u32 render/valid flag : &0x4000 = hidden/despawned ghost slot
static const u32 ENT_PFLAGS_OFF  = 0x124;   // u32 render flags : bit 0x00800000 (byte 0x126 & 0x80) = PC IN A PARTY
static const u32 ENT_STATUS_OFF  = 0x170;   // u32 : 0 = idle, 1 = engaged/in-combat
static const u32 ENT_CLAIM_OFF   = 0x188;   // u32 : claiming player's server id (0 = unclaimed)
static const u32 ENT_SPAWN_OFF   = 0x1D0;   // u8 SpawnType (read as a dword -> mask 0xFF) : 0x01 PC, 0x02 NPC, 0x10 Mob

// Nearby entities for the minimap. The entity array (*(g+0x24)) is 0x900 pointers ; block-copy it once
// (one SEH frame) then read each live entity's type (spawnType), render flag (& 0x4000 = hidden) and
// world X/Z. Skips self, hidden, and non PC/NPC/mob. Returns the count.
int read_map_entities(MapEntity* out, int maxN) {
    if (!out || maxN <= 0) return 0;
    u32 ent = entity_array();
    if (!ent) return 0;
    static u32 ptrs[0x900];
    __try { memcpy(ptrs, (const void*)ent, sizeof(ptrs)); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    const u32 self = self_entity();
    int n = 0;
    for (unsigned i = 0; i < 0x900 && n < maxN; ++i) {
        u32 p = ptrs[i];
        if (!valid_ptr(p) || p == self) continue;
        u32 sp = 0; if (!safe_read(p + ENT_SPAWN_OFF, &sp)) continue; sp &= 0xFF;
        unsigned char type;
        // BIT test, not equality. SpawnType packs a type bit (0x01 PC / 0x02 NPC / 0x10 mob) with membership
        // bits -- Windower's own FUN_1008DB90 derives in_party from bit 2 and in_alliance from bit 3 of THIS byte.
        // So a party PC reads 0x0D (PC|party|alliance) and a party trust 0x0E, and the old `== 0x01` / `== 0x02`
        // silently dropped every party and alliance member from the minimap while solo players still showed.
        if (sp & 0x01) type = 1; else if (sp & 0x02) type = 2; else if (sp & 0x10) type = 3; else continue;
        u32 rf = 0; safe_read(p + ENT_RENDER_OFF, &rf); if (rf & 0x4000) continue;   // render/valid flag : filters despawned/invalid ghost slots
        // A DEAD mob (corpse, HP% 0) keeps its stale claim id, so allegiance_color would render it RED until it
        // despawns -- at a heavy camp (Crawlers' Nest [S], 100+ mobs) the map fills with red dead-claim dots that
        // never clear. A corpse is not a threat -> drop it. Guard the read : a FAILED read (hp stays 0) must NOT
        // drop a live mob, so require the read to SUCCEED before treating 0 as dead.
        if (type == 3) { u32 hp = 0; if (safe_read(p + ENT_HPP_OFF, &hp) && (hp & 0xFF) == 0) continue; }
        u32 xx = 0, zz = 0; if (!safe_read(p + ENT_X_OFF, &xx) || !safe_read(p + ENT_Z_OFF, &zz)) continue;
        const float ex = *(float*)&xx, ez = *(float*)&zz;
        if (ex == 0.0f && ez == 0.0f) continue;                            // unloaded / invalid slot
        u32 id = 0, cl = 0, pf = 0, st = 0, hh = 0;
        safe_read(p + ENT_ID_OFF, &id); safe_read(p + ENT_CLAIM_OFF, &cl); safe_read(p + ENT_PFLAGS_OFF, &pf); safe_read(p + ENT_STATUS_OFF, &st); safe_read(p + ENT_HEADING_OFF, &hh);
        out[n].x = ex; out[n].z = ez; out[n].heading = *(float*)&hh; out[n].type = type;
        out[n].id = id; out[n].claimId = cl; out[n].pflags = pf; out[n].status = (unsigned char)(st & 0xFF);
        out[n].spawn = (unsigned char)(sp & 0xFF);   // party/alliance bits ride along
        ++n;
    }
    return n;
}

// <bt> as the GAME resolves it (model/battle_target.h has the rule and why). ENT_ACTOR_OFF : the game's resolver
// skips an entity whose +0xA0 is null -- read as "actor loaded" (Ashita's ActorPointer), not measured in game yet.
static const u32 ENT_ACTOR_OFF = 0xA0;
static bool copy_entity_ptrs(u32 ent, u32* ptrs, unsigned n) {
    bool ok = false;
    __try { memcpy(ptrs, (const void*)(uintptr_t)ent, n * sizeof(u32)); ok = true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    return ok;
}
bool read_battle_target(const unsigned* partyIds, int n, unsigned& out) {
    out = 0;
    if (!partyIds || n <= 0 || !self_entity()) return false;   // no roster or no player entity yet : unavailable
    const u32 ent = entity_array();
    if (!ent) return false;
    static u32 ptrs[0x900];
    if (!copy_entity_ptrs(ent, ptrs, 0x900)) return false;
    struct Src {
        const u32* p;
        int bt_count() const { return 0x900; }
        bool bt_read(int i, BtEntity& e) const {
            const u32 q = p[i];
            if (!valid_ptr(q)) return false;
            u32 cl = 0;
            if (!safe_read(q + ENT_CLAIM_OFF, &cl)) return false;
            e.claim = cl;
            if (!cl) return true;                                // unclaimed : nothing else to read
            u32 actor = 0, st = 0, id = 0;
            if (!safe_read(q + ENT_ACTOR_OFF, &actor) || !safe_read(q + ENT_STATUS_OFF, &st) || !safe_read(q + ENT_ID_OFF, &id)) return false;
            e.actor = actor != 0; e.status = st; e.id = id;
            return true;
        }
    } src = { ptrs };
    out = battle_target_pick(partyIds, n, src);
    return true;                                                  // read fine : out = 0 means genuinely no <bt>
}

// Resolve a set of entity ids -> live vitals in one entity-array block-copy (shared ENT_*_OFF offsets).
// Used by the hate list : for each tracked mob id, fetch name / HP% / world pos / claim / status without
// re-copying the array per id. out[i] stays valid=false unless a matching entity is found.
int read_entities_by_id(const unsigned* ids, int n, EntityVitals* out) {
    if (!ids || !out || n <= 0) return 0;
    for (int i = 0; i < n; ++i) { out[i].id = ids[i]; out[i].valid = false; out[i].name[0] = 0; out[i].hpp = 0; out[i].x = out[i].z = 0.0f; out[i].status = out[i].claimId = out[i].spawnType = 0; }
    u32 ent = entity_array();
    if (!ent) return 0;
    static u32 ptrs[0x900];
    __try { memcpy(ptrs, (const void*)ent, sizeof(ptrs)); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    int found = 0;
    for (unsigned i = 0; i < 0x900 && found < n; ++i) {
        u32 p = ptrs[i];
        if (!valid_ptr(p)) continue;
        u32 id = 0; if (!safe_read(p + ENT_ID_OFF, &id) || !id) continue;
        int slot = -1;
        for (int k = 0; k < n; ++k) if (!out[k].valid && ids[k] == id) { slot = k; break; }
        if (slot < 0) continue;
        EntityVitals& v = out[slot];
        u32 hp = 0, st = 0, cl = 0, sp = 0, xx = 0, zz = 0;
        safe_read(p + ENT_HPP_OFF, &hp); safe_read(p + ENT_STATUS_OFF, &st); safe_read(p + ENT_CLAIM_OFF, &cl);
        safe_read(p + ENT_SPAWN_OFF, &sp); safe_read(p + ENT_X_OFF, &xx); safe_read(p + ENT_Z_OFF, &zz);
        v.hpp = (int)(hp & 0xFF); v.status = st; v.claimId = cl; v.spawnType = sp & 0xFF;
        v.x = *(float*)&xx; v.z = *(float*)&zz;
        __try { const char* nm = (const char*)(p + ENT_NAME_OFF); int j = 0; for (; j < 23 && nm[j]; ++j) v.name[j] = nm[j]; v.name[j] = 0; }
        __except (EXCEPTION_EXECUTE_HANDLER) { v.name[0] = 0; }
        v.valid = true; ++found;
    }
    return found;
}

// PointWatch : main-job Capacity Points + Job Points from the client's persistent struct *(g+0x48). CP u16 @+0,
// JP u16 @+2 are adjacent -> one u32 read (cp = low 16, jp = high 16). Reversed via LuaCore FUN_10091110.
bool read_capacity_points(unsigned mainJob, unsigned& cp, unsigned& jp) {
    if (mainJob < 1 || mainJob > 23) return false;
    u32 g = data_root(); if (!g) return false;
    u32 base = 0; if (!safe_read(g + 0x48, &base) || !valid_ptr(base)) return false;
    const u32 entry = base + 0x306 + (mainJob - 1) * 6;
    u32 w = 0; if (!safe_read(entry, &w)) return false;
    cp = w & 0xFFFF; jp = (w >> 16) & 0xFFFF;
    return true;
}

// PointWatch : Exemplar Points + Limit Points/merits, read out of FFXiMain static data. These are not four
// independent addresses -- they are TWO structs, and saying so is what makes them repairable. The client
// copies packet 0x061's body verbatim from body+0x10 onward into the first one (disassembled in
// docs/game-data/luacore-verified-offsets.md), so every field below is that packet at a fixed offset :
//   block + 0x00   u16 Current EXP, u16 Required EXP @+2        (= body +0x10 / +0x12)
//   block + 0x55   u8  Master Level                             (= body +0x65)
//   block + 0x58   u32 Exemplar Current, u32 Required @+4       (= body +0x68 / +0x6C)
// and the merit block mirrors 0x063 order 2 : LP u16 @+0, merit count byte @+2 (low 7 bits), max @+4.
// The BASE of each block lives in ffximain_rva.cpp, which re-derives it from those very packets when a
// client patch moves it -- which is why nothing here hard-codes an address any more.
static const u32 PW_MLVL_OFF = 0x55, PW_EXEMPLAR_OFF = 0x58;

bool read_pointwatch(PwMem& out) {
    out = PwMem{};
    const u32 blk = fm_addr(FM_PW_BLOCK), mer = fm_addr(FM_PW_MERIT);
    if (blk) {
        u32 xw = 0, ep = 0, et = 0, ml = 0;
        const bool xwRead = safe_read(blk, &xw);
        const bool epRead = safe_read(blk + PW_EXEMPLAR_OFF, &ep) && safe_read(blk + PW_EXEMPLAR_OFF + 4, &et);
        const bool mlRead = safe_read(blk + PW_MLVL_OFF, &ml);
        pw_decode_block(xwRead, xw, epRead, ep, et, mlRead, ml, out);   // a zeroed block (zoning) is NOT "ML 0" : see game_mem.h
    }
    if (mer) {
        u32 lp = 0, mx = 0;
        if (safe_read(mer, &lp) && safe_read(mer + 4, &mx)) {
            out.lpCur = lp & 0xFFFF;               // Limit Points (u16 @+0)
            out.merits = (int)((lp >> 16) & 0x7F); // merit count (byte @+2, low 7 bits)
            out.maxMerits = (int)(mx & 0xFF);      // max merits (byte @+4)
            out.merOk = (out.maxMerits > 0);
        }
    }
    return out.epOk || out.merOk || out.mlOk;
}

// THE CHARACTER SHEET, READ FROM THE CLIENT'S OWN MIRROR OF THE 0x061 BODY.
//
// The packet only comes on a login, a job change or a zone -- so after a plugin reload the sheet stayed empty
// until the player happened to zone, which reads to them as a feature that does not work. The client keeps the
// packet body in a FFXiMain static (that is what PointWatch already reads, and what fm_pw_expect finds again
// after a patch), starting at the packet's own offset 0x10 -- proven by the fields already taken from it:
// Master Level at packet 0x65 sits at block +0x55, Exemplar at packet 0x68 at block +0x58.
//
// So the mirror is copied back into a buffer AT THAT OFFSET and handed to the SAME decoder the packet path
// uses (model/charsheet.h). One decoder, two sources : the two can never drift apart, which is the whole
// reason not to write a second reader here.
bool model_copy(u32 addr, void* out, unsigned n);   // model_io.h is included at the FOOT of this file (it
                                                   // renames the observed reads) : declare the one helper used here.
// THE 22 JOB LEVELS, FROM THE CLIENT'S OWN TABLE. Packet 0x01B carries them, but it arrives only on a login or
// a JOB CHANGE -- not on a zone -- so after a plugin reload the sheet had no job table for as long as the player
// kept the same job, and the page showed nothing where twenty-two numbers belong.
//
// MEASURED 2026-09-15 with `//aio jobtab` (dev/src/jobscan.cpp), which is given the real table from the outside
// and asked to find where it is written, rather than told an offset and asked whether it looks plausible:
//   - levels        : *(g+0x48) + 0x3C6D, 22 bytes, job id 1..22
//   - master levels : *(g+0x48) + 0x3C91, the same 22 in the same order (levels + 0x24)
// Confirmed on TWO characters in two client processes with different allocation bases and different values
// (Tetsouo, every job 99 ; Kaories, 1/99/72 mixed), and the bytes read back were IDENTICAL to what Windower's
// own packet parser decoded from 0x01B -- which is what makes this a measurement and not a lucky offset.
// The region is a private (heap) allocation, NOT an FFXiMain image, so there is no RVA to self-heal: the only
// stable route is this pointer chain, the same *(g+0x48) read_job_spent already uses for the Job Points.
bool read_job_table_mem(CharSheet& cs, unsigned mainJob, unsigned mainLvl) {
    u32 g = data_root(); if (!g) return false;
    u32 base = 0; if (!safe_read(g + 0x48, &base) || !valid_ptr(base)) return false;
    unsigned char lv[22] = {0}, ml[22] = {0};
    if (!model_copy(base + 0x3C6D, lv, sizeof(lv))) return false;
    if (!model_copy(base + 0x3C91, ml, sizeof(ml))) return false;
    // VALIDATE BEFORE ADOPTING. A pointer that has moved, or a block the client is rewriting, must read as
    // "no data" and leave the packet's table alone -- never as a character with impossible jobs.
    for (int i = 0; i < 22; ++i) {
        if (lv[i] > 99) return false;      // a job level is 0..99
        if (ml[i] > 50) return false;      // a master level is 0..50
    }
    // The strongest check available for free : we already know the main job and its level from another reader.
    // If this table disagrees about the job the player is standing in, it is not this player's table.
    if (mainJob >= 1 && mainJob <= 22 && mainLvl && lv[mainJob - 1] != (unsigned char)mainLvl) return false;
    bool anyLevel = false;
    for (int i = 0; i < 22; ++i) if (lv[i]) { anyLevel = true; break; }
    if (!anyLevel) return false;           // all zero is the blanked state during a zone, not a character
    for (int j = 1; j < CS_JOB_N && j <= 22; ++j) { cs.jobLvl[j] = lv[j - 1]; cs.masterLvl[j] = ml[j - 1]; }
    cs.jobsOk = true;                      // the LEVELS are now usable ; `mastered` stays whatever the packet said
    return true;
}

bool read_charsheet_mem(CharSheet& cs) {
    const u32 blk = fm_addr(FM_PW_BLOCK);
    if (!blk) return false;
    unsigned char buf[0x4C];
    memset(buf, 0, sizeof(buf));
    // 0x3A bytes from the mirror cover packet 0x10..0x49 : EXP, the attributes, what the gear adds, Attack,
    // Defense, the eight resistances, the title and the rank. maxHP/maxMP and the jobs live BEFORE 0x10 and are
    // therefore not in this block -- they are left to the packet path, and to the fields the roster already has.
    if (!model_copy(blk, buf + 0x10, 0x3A)) return false;
    CharSheet tmp = cs;                      // keep whatever the packet path already proved
    if (!cs_read_061(buf, 0x4A, tmp)) return false;
    // A ZEROED BLOCK IS NOT A CHARACTER. During a zone the client blanks it, and a sheet of zeroes would read
    // as a measurement -- the same trap pw_decode_block was written for. One attribute at zero is impossible
    // on a real character, so that is the test.
    bool allZero = true;
    for (int a = 0; a < CS_ATTR_N; ++a) if (tmp.base[a]) { allZero = false; break; }
    if (allZero) return false;
    tmp.maxHp = cs.maxHp; tmp.maxMp = cs.maxMp;   // not in this block : never overwrite what the packet gave
    tmp.mjob = cs.mjob; tmp.mlvl = cs.mlvl; tmp.sjob = cs.sjob; tmp.slvl = cs.slvl;
    cs = tmp;
    return true;
}

// entity_array[index] -> server id (+0x78). One indexed read (not a scan). 0 if empty/invalid.
unsigned entity_id_by_index(unsigned index) {
    if (index == 0 || index >= 0x900) return 0;
    u32 ent = entity_array();
    if (!ent) return 0;
    u32 p = 0; if (!safe_read(ent + index * 4, &p) || !valid_ptr(p)) return 0;
    u32 id = 0; if (!safe_read(p + ENT_ID_OFF, &id)) return 0;
    return id;
}

// Position of entity_array[index], but ONLY if that entity is still `expectId` and is not a despawned ghost.
//
// This exists because the party/alliance distance was computed from ent[member+0x20] with NEITHER check, and a
// party-member entity index is not a stable handle: the client DESPAWNS a player who goes past its tracking
// range (~100 yalms) and is free to hand that slot to somebody else. Two failure shapes follow, both of which
// produce a perfectly plausible number rather than a missing one:
//   * ghost slot  -> the entity object survives with its LAST position, so the distance FREEZES and keeps
//                    counting a member who is nowhere near there ;
//   * recycled    -> the slot now holds another entity, so the distance jumps to a stranger's position.
// Both were reported in game as "the distance went haywire" after a member ran far away and came back.
// The identity check is what makes it honest: no match, no number. Callers get false and must degrade to
// UNKNOWN (dist < 0 -> the HUD draws nothing), never to a guess.
//
// The render-flag half is not new knowledge either -- the minimap scan above already filters 0x4000 with the
// same comment. This path was simply the one place that skipped it.
bool entity_pos_verified(unsigned index, unsigned expectId, float& x, float& y, float& z, bool* despawned) {
    if (despawned) *despawned = false;
    if (index == 0 || index >= 0x900 || !expectId) return false;
    u32 ent = entity_array();
    if (!ent) return false;
    u32 p = 0; if (!safe_read(ent + index * 4, &p) || !valid_ptr(p)) return false;
    u32 id = 0; if (!safe_read(p + ENT_ID_OFF, &id) || id != expectId) return false;   // recycled / not this member
    u32 rf = 0;
    if (!safe_read(p + ENT_RENDER_OFF, &rf)) return false;
    if (rf & 0x4000) { if (despawned) *despawned = true; return false; }   // ghost : right member, frozen position
    u32 a = 0, b = 0, c = 0;
    if (!safe_read(p + ENT_X_OFF, &a) || !safe_read(p + 0x08, &b) || !safe_read(p + ENT_Z_OFF, &c)) return false;
    x = *(float*)&a; y = *(float*)&b; z = *(float*)&c;   // Y @+0x08 : height, read for the probe only
    return true;
}

// server id -> entity NAME (ENT_NAME_OFF, ASCII up to 0x18). The reverse of entity_id_by_index : a linear scan,
// since ids are not an index. Used to tell a Limbus COFFER from a POINT OF INTEREST, which the award message id
// cannot (it keys on the wing) : a point of interest is the unnamed '???', a coffer is 'Apollyon Coffer #4'.
// SEH-guarded per read ; false (and out[0]=0) when the id isn't in the array.
// entity INDEX -> name. Packets carry a target INDEX (a slot in the entity array), not a server id -- reading
// the 0x02A "target" field as an id is what made the first coffer/point-of-interest capture come back
// <unresolved>. One indexed read, no scan. SEH-guarded ; false (out[0]=0) if the slot is empty.
bool entity_name_by_index(unsigned index, char* out, int sz) {
    if (out && sz > 0) out[0] = 0;
    if (!out || sz <= 0 || index == 0 || index >= 0x900) return false;
    u32 ent = entity_array();
    if (!ent) return false;
    u32 p = 0; if (!safe_read(ent + index * 4, &p) || !valid_ptr(p)) return false;
    int j = 0;
    for (; j < sz - 1; ++j) {
        u32 c = 0; if (!safe_read(p + ENT_NAME_OFF + j, &c)) break;
        const char ch = (char)(c & 0xFF); if (!ch) break;
        out[j] = ch;
    }
    out[j] = 0;
    return j > 0;
}

// The client map-info record for (zone, submap). Linear scan of the table at *(g+0x10) : 14-byte records,
// key (zone u16 @+0x00, submap u8 @+0x02). Returns false (valid=false) when the zone/submap has no record
// (e.g. a zone with no map). SEH-guarded per read. See docs/game-data/world/map-system.md.
bool read_map_record(unsigned zone, int submap, MapRecord& out) {
    out = MapRecord{}; out.zone = zone;
    if (!zone) return false;
    u32 g = data_root();
    if (!g) return false;
    u32 tb = 0; if (!safe_read(g + 0x10, &tb) || !valid_ptr(tb)) return false;
    u32 rec = tb;
    for (int i = 0; i < 2048; ++i, rec += 0x0E) {
        u32 z0 = 0; if (!safe_read(rec, &z0)) break; z0 &= 0xFFFF;
        u32 sm = 0; safe_read(rec + 0x02, &sm); sm &= 0xFF;
        if (z0 == zone && sm == (unsigned)submap) {
            u32 sc = 0, fi = 0, fl = 0, ox = 0, oy = 0;
            safe_read(rec + 0x05, &sc); safe_read(rec + 0x08, &fi); safe_read(rec + 0x04, &fl);
            safe_read(rec + 0x0A, &ox); safe_read(rec + 0x0C, &oy);
            out.scale   = (int)(sc & 0xFF);
            out.fileIdx = fi & 0xFFFF;
            out.flags   = fl & 0xFFFF;
            out.fileId  = ((fl & 1) ? 0xD02Fu : 0x14C0u) + (fi & 0xFFFF);   // map DAT file id
            out.offX    = (short)(ox & 0xFFFF);
            out.offY    = (short)(oy & 0xFFFF);
            out.valid   = true;
            return true;
        }
        if (z0 == 0 && i > 8) break;                            // ran off the end of the table
    }
    return false;
}

// The current sub-map / FLOOR index for a multi-floor zone (dungeons, towers, ...). Unlike every other
// datum we take, the client does NOT store this as a plain integer -- it PARTITIONS the world by the
// player's position via a routine at *(g+0x20) (context = *(g+0x1C)), returning the floor index (or
// 0xFFFFFFFF for "no floor / mapless"). This is FFXIDB's exact method, called every frame on this same
// game thread (reversed 2026-07-06 from FFXIDB FUN_1006b540 : `(*(g+0x20))(*(g+0x1c), 0, x,y,z)`).
// It is the plugin's ONE indirect CALL into client code -- so it is SEH-guarded like a raw read : any
// fault, a null ctx/fn, or the sentinel all DEGRADE TO 0 (ground floor = today's behaviour), never crash.
int current_submap() {
    u32 g = data_root();
    if (!g) return 0;
    u32 ctx = 0, fn = 0;
    if (!safe_read(g + 0x1C, &ctx) || !valid_ptr(ctx)) return 0;   // map-system context (0 = no map system)
    if (!safe_read(g + 0x20, &fn)  || !valid_ptr(fn))  return 0;   // position -> floor routine
    u32 ent = self_entity();
    if (!ent) return 0;
    u32 xb = 0, yb = 0, zb = 0;                                     // (x@+0x04, y/height@+0x08, z@+0x0C) -- the routine takes all three
    if (!safe_read(ent + 0x04, &xb) || !safe_read(ent + 0x08, &yb) || !safe_read(ent + 0x0C, &zb)) return 0;
    const float x = *(float*)&xb, y = *(float*)&yb, z = *(float*)&zb;
    u32 r = 0xFFFFFFFFu;
    __try {
        typedef u32 (__fastcall *floor_fn)(void*, int, float, float, float);
        r = ((floor_fn)fn)((void*)ctx, 0, x, y, z);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    if (r == 0xFFFFFFFFu || r > 0xFF) return 0;                     // sentinel / out-of-range -> ground floor (record key is a u8)
    return (int)r;
}

// The player's currently-USABLE weapon-skill ids (Windower get_abilities().weapon_skills). Reversed 2026-07-07
// from LuaCore get_abilities (FUN_100732B0) : the lists live as a 32-byte BITMASK at +0x04 of a 512-byte
// "block 0xAC" fetched through the runtime resource manager at *(g+4) via two virtual calls (this-on-stack,
// callee-cleans -> __stdcall(this,...)). Usable WS iff `block[0x04 + (id>>3)] & (1 << (id&7))` (id 1..255).
// Replicating the getter is the plugin's 2nd CALL into client code (cf current_submap) -> SEH-guarded end to
// end + gated on the manager being live (0 until login) ; ANY fault -> 0 usable (the list just stays empty).
// copy the first `n` bytes of the get_abilities "block 0xAC" (the usable-ability bitmaps) into buf. SEH-guarded
// getter (2 virtual calls, see the notes above) ; false on any fault / not-ready. Weapon skills live at
// block[0x04..0x24) (id 1..255), job abilities at block[0x44..0xC4) (id 0..1023) -- the JA bitmap already
// self-gates on the pet being out (a pet's ready moves are "usable" only while it is summoned).
static bool read_ability_block(unsigned char* buf, int n) {
    u32 g = data_root(); if (!g) return false;
    u32 mgr = 0; if (!safe_read(g + 4, &mgr) || !valid_ptr(mgr)) return false;   // resource manager (null until login)
    __try {
        typedef void* (__stdcall *GetSub)(void*);
        typedef void* (__stdcall *GetBlk)(void*, int, int);
        const u32 vt1 = *(const u32*)mgr;                       if (!valid_ptr(vt1)) return false;
        void* sub = ((GetSub)(*(const u32*)(vt1 + 0x18)))((void*)mgr);          // vtbl slot 6 -> sub-manager
        if (!valid_ptr((u32)sub)) return false;
        const u32 vt2 = *(const u32*)sub;                       if (!valid_ptr(vt2)) return false;
        void* blk = ((GetBlk)(*(const u32*)(vt2 + 0x0C)))(sub, 0xAC, 0);        // vtbl slot 3 -> the 0xAC block
        if (!valid_ptr((u32)blk)) return false;
        if ((*(const u32*)blk & 0x1FF) == 0) return false;                      // block header not populated yet
        memcpy(buf, (const unsigned char*)blk, n);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    return true;
}
int read_usable_weapon_skills(unsigned short* out, int maxN) {   // WS bitmap @ block[0x04..0x24), ids 1..255
    if (!out || maxN <= 0) return 0;
    unsigned char b[0x24]; if (!read_ability_block(b, 0x24)) return 0;
    int n = 0; for (int id = 1; id < 256 && n < maxN; ++id) if (b[0x04 + (id >> 3)] & (1u << (id & 7))) out[n++] = (unsigned short)id;
    return n;
}
int read_usable_job_abilities(unsigned short* out, int maxN) {   // JA bitmap @ block[0x44..0xC4), ids 0..1023 (pet ready moves included when a pet is out)
    if (!out || maxN <= 0) return 0;
    unsigned char b[0xC4]; if (!read_ability_block(b, 0xC4)) return 0;
    int n = 0; for (int id = 0; id < 1024 && n < maxN; ++id) if (b[0x44 + (id >> 3)] & (1u << (id & 7))) out[n++] = (unsigned short)id;
    return n;
}
bool read_usable_ja_bits(unsigned char set[128]) {   // raw 1024-bit usable-JA bitmap (block[0x44..0xC4) is exactly 0x80 = 128 bytes)
    if (!set) return false;
    unsigned char b[0xC4]; if (!read_ability_block(b, 0xC4)) return false;
    memcpy(set, b + 0x44, 128);
    return true;
}

// The BLU (Blue Mage) player's currently-SET blue magic spell ids (Windower get_mjob_data().spells). Reversed
// 2026-07-07 from LuaCore get_mjob_data (FUN_10072000 -> builder FUN_1008E270) : a decoded "mjob data" struct
// at *(g+0x60), tag @+0x00 (0x10 = BLU set-spells) ; the set spells are a 20-byte u8 array @+0x05 (0 = empty
// slot), the full spell id = byte + 0x200. PURE pointer chain (no client call) -> plain SEH-guarded reads.
int read_blu_spells(unsigned short* out, int maxN) {
    if (!out || maxN <= 0) return 0;
    u32 g = data_root(); if (!g) return 0;
    u32 M = 0; if (!safe_read(g + 0x60, &M) || !valid_ptr(M)) return 0;
    u32 tag = 0; if (!safe_read(M + 0x00, &tag) || tag != 0x10) return 0;    // 0x10 = BLU (0x12/0x17 = PUP)
    int n = 0;
    for (int i = 0; i < 20 && n < maxN; ++i) {
        u32 b = 0; if (!safe_read(M + 0x05 + i, &b)) break; b &= 0xFF;       // one set-spell slot (0 = empty)
        if (b) out[n++] = (unsigned short)(0x200 + b);
    }
    return n;
}

// The local player's movement speed = self entity +0x98 (movement_speed lives on the ENTITY struct, not
// the player struct ; base 5.0 for a PC -> 0%). `selfId` is unused now that self_entity() self-validates
// the anchor -- kept for API stability. SEH-guarded ; no-op (false) on a bad ptr.
bool read_self_speed(unsigned selfId, float& ms) {
    (void)selfId;
    u32 p = self_entity();
    if (!p) return false;
    u32 raw = 0; if (!safe_read(p + ENT_SPEED_OFF, &raw)) return false;
    memcpy(&ms, &raw, 4);                                  // float read as a dword, then bit-copied (safe_read is u32)
    return true;
}

// The local player's GIL. get_items('gil') = *( *(g+0x50) + 0x04 ) : the item-container root (g+0x50), then
// the u32 count of bag0/slot0 -- FFXI's gil pseudo-item (id 0xFFFF @+0x00). Reversed 2026-07-05 from LuaCore
// get_items (FUN_10074690). SEH-guarded ; no-op (false) while the container is unmapped (zoning).
bool read_player_gil(unsigned& gil) {
    u32 ir = items_root();
    if (!ir) return false;
    u32 v = 0; if (!safe_read(ir + 0x04, &v)) return false;
    gil = v;
    return true;
}

// ================================ INVENTORY (all 18 bags) ================================
// ONE source of truth for the item-container layout (game-data/inventory.md). Reversed 2026-07-17 from
// LuaCore's own get_items binding (FUN_10074690) : the no-arg path calls FUN_100935c0(L, *(g+0x50)) which
// dumps bags 0..0x11 via FUN_10093360(L, bag), whose entry address is
//     *(g+0x50) + (slot + bag*0x51) * 0x28        with slot looped 1..0x50
// i.e. 81 entries per bag (0x51 * 0x28 = 0xCA8 -- the SAME stride read_equipment() already uses), entry 0
// reserved (id 0xFFFF), slots 1..80 usable. FUN_100935c0 then publishes max_*/count_*/enabled_* from three
// u8[18] arrays at +0x19500 / +0x19520 / +0x19540. Those bounds are Windower's own -> as safe to read as
// get_items() itself.
static const u32 ITEM_ENTRY_SZ   = 0x28;     // one item entry (id u16 @+0x00, count u32 @+0x04)
static const u32 ITEM_BAG_ENTRIES= 0x51;     // 81 entries per bag (0 = reserved header, 1..80 = items)
static const u32 ITEM_META_MAX   = 0x19500;  // u8[18] capacity per bag
static const u32 ITEM_META_COUNT = 0x19520;  // u8[18] occupied slots per bag
static const u32 ITEM_META_ENAB  = 0x19540;  // u8[18] bag currently reachable
static const u32 ITEM_BLOCK_SZ   = 0x19552;  // bags + the three metadata arrays : the whole container, one copy

static const char* const ITEM_BAG_NAMES[ITEM_BAG_MAX] = {
    "inventory", "safe", "storage", "temporary", "locker", "satchel", "sack", "case",
    "wardrobe", "safe2", "wardrobe2", "wardrobe3", "wardrobe4", "wardrobe5", "wardrobe6",
    "wardrobe7", "wardrobe8", "recycle" };
const char* item_bag_name(int bag) { return (bag >= 0 && bag < ITEM_BAG_MAX) ? ITEM_BAG_NAMES[bag] : ""; }

// The snapshot : fixed-capacity static (no per-frame heap alloc), filled by ONE guarded block copy.
static unsigned char s_items[ITEM_BLOCK_SZ];
static bool          s_itemsOk = false;

bool refresh_items() {
    s_itemsOk = false;
    u32 ir = items_root();
    if (!ir) return false;                                   // container unmapped (zoning) -> no-op
    __try { memcpy(s_items, (const void*)ir, sizeof(s_items)); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }   // bad pointer degrades to a no-op, never a crash
    s_itemsOk = true;
    return true;
}

// address of one entry INSIDE the snapshot (never a live pointer)
static inline const unsigned char* item_entry(int bag, int slot) {
    return s_items + ((u32)slot + (u32)bag * ITEM_BAG_ENTRIES) * ITEM_ENTRY_SZ;
}

bool item_bag_info(int bag, ItemBagInfo& out) {
    out.max = out.count = out.enabled = 0;
    if (bag < 0 || bag >= ITEM_BAG_MAX) return false;
    if (!s_itemsOk && !refresh_items()) return false;
    out.max     = s_items[ITEM_META_MAX   + bag];
    out.count   = s_items[ITEM_META_COUNT + bag];
    out.enabled = s_items[ITEM_META_ENAB  + bag];
    return true;
}

bool item_slot(int bag, int slot, unsigned& id, unsigned& count) {
    id = 0; count = 0;
    if (bag < 0 || bag >= ITEM_BAG_MAX || slot < 1 || slot > ITEM_BAG_SLOTS) return false;
    if (!s_itemsOk && !refresh_items()) return false;
    const unsigned char* e = item_entry(bag, slot);
    const unsigned raw = (unsigned)e[0] | ((unsigned)e[1] << 8);
    if (raw == 0xFFFF) return true;                          // reserved header value -> report as empty
    id = raw;
    if (id) count = *(const u32*)(e + 4);
    return true;
}

int count_items(const unsigned* ids, int n, unsigned* out) {
    for (int i = 0; i < n; ++i) out[i] = 0;
    if (!ids || n <= 0 || !out) return 0;
    if (!s_itemsOk && !refresh_items()) return 0;            // no snapshot -> everything reads 0
    for (int b = 0; b < ITEM_BAG_MAX; ++b) {
        if (s_items[ITEM_META_COUNT + b] == 0) continue;     // client says the bag is empty -> skip its 80 slots
        for (int s = 1; s <= ITEM_BAG_SLOTS; ++s) {
            const unsigned char* e = item_entry(b, s);
            const unsigned id = (unsigned)e[0] | ((unsigned)e[1] << 8);
            if (id == 0 || id == 0xFFFF) continue;           // empty slot / the reserved header
            for (int i = 0; i < n; ++i)
                if (ids[i] == id) { out[i] += *(const u32*)(e + 4); break; }
        }
    }
    int hit = 0;
    for (int i = 0; i < n; ++i) if (out[i]) ++hit;
    return hit;
}

unsigned count_item(unsigned id) { unsigned c = 0; count_items(&id, 1, &c); return c; }

// KEY ITEMS. The store is a FLAT u8[0x2000] -- one byte per id, non-zero = owned -- NOT the 0x055 packet's
// bitfield, so there is no id/512 table arithmetic here (that scheme is packet-only ; game-data/key-items.md).
// A caller only ever asks for a handful of ids (an NM's pop chain is <= 15), so a guarded read each beats
// snapshotting 8 KB. safe_read pulls a u32 : at the top id (0x1FFF) that spills 3 bytes into items_root, which
// sits at base+0x2000 and is mapped -- so it cannot fault. Mask to the byte we asked for.
bool owns_key_item(unsigned id) {
    if (id >= 0x2000) return false;                          // outside the array -> not owned, never a read
    const u32 kb = key_items_base();
    if (!kb) return false;                                   // unmapped (zoning / not in game) -> no-op
    u32 b = 0;
    return safe_read(kb + id, &b) && (b & 0xFF) != 0;        // BOOL array : test != 0, not == 1
}

// The 16 equipped items (Equipment Viewer). get_items('equipment') reads two parallel 16-entry arrays
// keyed by the packet equip-slot id S : index = u8 @(g+0x54)+S, bag = s32 @(g+0x58)+S*4 ; the equipped
// item is then items_root + bag*0xCA8 + index*0x28 (id u16 @+0x00, count u32 @+0x04). index==0 = empty.
// Reversed 2026-07-05 from LuaCore FUN_10074690 -> FUN_10094410. SEH-guarded per read ; no-op on bad ptr.
// The return value means "this read RESOLVED", not "the pointers were non-null". It used to mean the latter,
// which made it useless as a readiness flag : during the half-ready window after a zone the three roots are
// already mapped but the containers aren't filled, so every slot read 0 and the caller was told all-empty is
// AUTHORITATIVE. The Equipment Viewer then released all 16 cached icons and reloaded them a frame later
// (src/ui/player.cpp) -- the reload storm its equipValid guard exists to prevent.
//
// Readiness = the item container is POPULATED, taken from the container's own metadata (inventory capacity,
// bag 0 of the u8[18] at +ITEM_META_MAX) rather than from what happens to be equipped. That separates the two
// cases an all-zero read conflates : containers not ready yet (capacity 0 -> not resolved, keep the cached
// icons) vs a genuinely unequipped player (capacity non-zero -> resolved, empty grid is the truth).
// The `found` fallback covers the reverse skew : if slots resolved, the data is plainly there whatever the
// metadata says.
bool read_equipment(EquipSet& out) {
    for (int s = 0; s < 16; ++s) { out.id[s] = 0; out.count[s] = 0; }
    u32 ir = items_root(), ia = equip_index_arr(), ba = equip_bag_arr();
    if (!ir || !ia || !ba) return false;
    u32 cap = 0;                                                // inventory capacity : 0 until the containers fill
    const bool ready = safe_read(ir + ITEM_META_MAX, &cap) && (cap & 0xFF) != 0;
    int found = 0;
    for (int s = 0; s < 16; ++s) {
        u32 idx = 0; safe_read(ia + s, &idx); idx &= 0xFF;      // u8 index (0 = empty)
        if (idx == 0) continue;
        u32 bag = 0; safe_read(ba + s * 4, &bag);               // s32 bag id
        // BOUND both indices before computing an address. item_slot() in this same file enforces exactly these
        // invariants; these two paths did not. A garbage bag during the half-ready window after a zone makes
        // bag * 0xCA8 wrap and lands `item` anywhere valid_ptr accepts -- safe_read contains the fault, so the
        // result is not a crash but a WRONG item id, which then feeds the enhancing-duration maths.
        if (bag >= (u32)ITEM_BAG_MAX || idx >= ITEM_BAG_ENTRIES) continue;
        u32 item = ir + bag * 0xCA8 + idx * 0x28;
        if (!valid_ptr(item)) continue;
        u32 id = 0, cnt = 0; safe_read(item + 0x00, &id); safe_read(item + 0x04, &cnt);
        out.id[s]    = (unsigned short)(id  & 0xFFFF);
        out.count[s] = (unsigned short)(cnt & 0xFFFF);
        if (out.id[s] != 0 && out.id[s] != 0xFFFF) ++found;
    }
    return ready || found > 0;
}

// TREASURE POOL ground truth : *(g+0x5C), 10 slots x 0x30. ONE SEH-guarded block copy of the 0x1E0-byte array
// (rule 5/6 : snapshot, not a safe_read per field), then parse the local copy. Returns false when the view is
// not mapped (zoning) so the caller keeps the packet-fed pool instead of wiping it (rule 10 : unknown != empty).
static u32 treasure_base() { u32 g = data_root(); u32 t = 0; return (g && safe_read(g + 0x5C, &t) && valid_ptr(t)) ? t : 0; }
bool read_treasure_pool(TreasureSlot out[10]) {
    for (int i = 0; i < 10; ++i) { out[i] = TreasureSlot{}; }
    const u32 base = treasure_base();
    if (!base) return false;
    unsigned char buf[0x1E0];
    bool ok = false;
    __try { memcpy(buf, (const void*)base, sizeof(buf)); ok = true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    if (!ok) return false;
    for (int i = 0; i < 10; ++i) {
        const unsigned char* s = buf + i * 0x30;
        out[i].occupied  = (s[0x00] != 0);
        out[i].item_id   = (unsigned short)(s[0x02] | (s[0x03] << 8));
        out[i].lot       = (unsigned short)(s[0x04] | (s[0x05] << 8));
        out[i].lot_id    = (unsigned)(s[0x08] | (s[0x09] << 8) | (s[0x0A] << 16) | (s[0x0B] << 24));
        out[i].timestamp = (unsigned)(s[0x28] | (s[0x29] << 8) | (s[0x2A] << 16) | (s[0x2B] << 24));
        int k = 0; for (; k < 19 && s[0x10 + k]; ++k) out[i].lot_name[k] = (char)s[0x10 + k];
        out[i].lot_name[k] = 0;
    }
    return true;
}

// ids + 24-byte extdata per equipped slot. The item entry (items_root + bag*0xCA8 + idx*0x28) holds the
// extdata (augment blob) at +0x0D (24 bytes) -- per the reversed item struct (id@0, slot@2, count@4, bazaar@8,
// status@0xC, extdata char[0x18]@0x0D ; see docs/game-data/player/player-equipment.md).
bool read_equipment_ext(unsigned short ids[16], unsigned char ext[16][24]) {
    for (int s = 0; s < 16; ++s) { ids[s] = 0; for (int k = 0; k < 24; ++k) ext[s][k] = 0; }
    u32 ir = items_root(), ia = equip_index_arr(), ba = equip_bag_arr();
    if (!ir || !ia || !ba) return false;
    u32 cap = 0;                                                // inventory capacity : 0 until the containers fill
    const bool ready = safe_read(ir + ITEM_META_MAX, &cap) && (cap & 0xFF) != 0;
    int found = 0;
    for (int s = 0; s < 16; ++s) {
        u32 idx = 0; safe_read(ia + s, &idx); idx &= 0xFF;
        if (idx == 0) continue;
        u32 bag = 0; safe_read(ba + s * 4, &bag);
        if (bag >= (u32)ITEM_BAG_MAX || idx >= ITEM_BAG_ENTRIES) continue;   // same bounds as read_equipment / item_slot
        u32 item = ir + bag * 0xCA8 + idx * 0x28;
        if (!valid_ptr(item)) continue;
        u32 id = 0; safe_read(item + 0x00, &id);
        ids[s] = (unsigned short)(id & 0xFFFF);
        if (ids[s] != 0 && ids[s] != 0xFFFF) ++found;
        for (int k = 0; k < 24; ++k) { u32 b = 0; if (!safe_read(item + 0x0D + k, &b)) break; ext[s][k] = (unsigned char)(b & 0xFF); }
    }
    // Same readiness semantics as read_equipment above (this ended in a bare `return true`, so an all-zero read
    // during the half-ready window after a zone was reported as authoritative). The caller computes Composure /
    // enhancing-duration multipliers from these ids -- on an all-zero "success" they silently collapse to base.
    return ready || found > 0;
}

// Player buffs, reversed from LuaCore get_player (FUN_10072040). The same player struct
// pl = *(g + 0x3C) holds a 32-entry uint16 status-icon array at pl+0x1C (loop runs the ushorts
// in [pl+0x1C, pl+0x5C) ); 0xFF marks an EMPTY slot. We compact the non-empty ids in slot order.
// `ok` (optional) separates "the read WORKED and you genuinely have no buffs" from "the read failed / not ready".
// The count alone conflates them -- it is 0 in both cases -- and a caller that guesses gets it wrong: the Timers
// FOCUS monitor read "count == 0" as "no data, assume every buff is still up", so once your last buff expired it
// believed everything was still active and could never fire a lost-buff alert. Same defect class as equipValid.
int read_player_buffs(unsigned short* out, int maxN, bool* ok) {
    if (ok) *ok = false;
    if (!out || maxN <= 0) return 0;
    u32 pl = player_struct();
    if (!pl) return 0;
    u32 mhp = 0; safe_read(pl + 0x60, &mhp);
    if (mhp == 0 || mhp > 0x100000) return 0;           // struct not ready yet
    if (ok) *ok = true;                                 // past here the player struct IS readable : an empty result is REAL
    int n = 0;
    for (int i = 0; i < 32 && n < maxN; ++i) {
        u32 v = 0; if (!safe_read(pl + 0x1C + i * 2, &v)) break;
        v &= 0xFFFF;
        if (v == 0xFF) continue;                        // empty slot (game's sentinel)
        out[n++] = (unsigned short)v;
    }
    return n;
}

// allianceinfo_t : g=data_root() ; pp=*(g+0x248) (=&member[0]+4) ; allianceinfo = *(pp).
// Fields (verified in-game 2026-06-26): +0x00 alliance leader id, +0x04/+0x08/+0x0C party
// 1/2/3 leader ids. A member is that role iff its serverid matches.
bool read_party_leaders(PartyLeaders& o) {
    o.alliance = o.p1 = o.p2 = o.p3 = 0;
    u32 pp = party_ptr();
    if (!pp) return false;
    u32 ai = 0;
    if (!safe_read(pp, &ai) || !valid_ptr(ai)) return false;            // allianceinfo_t = *(g+0x248)
    safe_read(ai + 0x00, &o.alliance);
    safe_read(ai + 0x04, &o.p1);
    safe_read(ai + 0x08, &o.p2);
    safe_read(ai + 0x0C, &o.p3);
    return true;
}

// Target + sub-target. Reversed in-game (2026-06-27, retail) matching Ashita's target_t
// layout (see plugins/sdk/ffxi/target.h). A STATIC pointer in FFXiMain.dll points at a heap
// target_t (ASLR-shifted base resolved at runtime). Located via the //aio tgt2 probe.
//   *(FFXiMain.dll + 0x5787AC)            -> target_t base (heap ; 0x57876C before the 2026-08-12 patch)
//   target_t + 0x04   u32  Targets[0].ServerId   = the ACTIVE reticle (sub when <st> open, else main)
//   target_t + 0x2C   u32  Targets[1].ServerId   = the LOCKED main (while a <st> cursor is open)
//   target_t + 0x50   u32  flags ; bit 0x00010000 = sub-target CURSOR open (clears on BOTH
//                          confirm and cancel ; NB: the byte at +0x78 is sticky -> do NOT use it)
// id 0x04000000 is the "nothing" sentinel. (Old flat cache lived at FFXiMain+0x487F60.)
//
// 2026-08-12 : an FFXI CLIENT PATCH rewrote FFXiMain.dll (+20 KB) and moved this static 0x57876C -> 0x5787AC.
// The chains hung off LuaCore survived, so the party kept drawing and ONLY the selection cursor died -- the
// failure looked like a widget bug, not a patch. The address itself now lives in ffximain_rva.cpp (FM_TARGET_T)
// and is re-derived at runtime ; there is deliberately NO copy of it here, because a second copy is exactly
// what turns the next patch into three edits and one forgotten one.
static const u32 T0_ID_OFF = 0x04, T1_ID_OFF = 0x2C, FLAGS_OFF = 0x50, LOCK_OFF = 0x5C;
static const u32 T0_EPTR_OFF = 0x08;      // Targets[0].EntityPointer (reticle = main unless a <st> cursor is up) -- a TARGET-system
                                          // field, not an entity field ; the ENT_*_OFF struct offsets live near read_map_entities.
                                          // (Declared with its siblings, not next to read_target_entity : target_root's signature
                                          //  check below needs it, and rule 7 wants one declaration, not a second literal 0x08.)
static const u32 T1_EPTR_OFF = 0x30;      // Targets[1].EntityPointer (the LOCKED main, valid while a <st> cursor is up).
                                          // Targets stride = T1_ID(0x2C) - T0_ID(0x04) = 0x28 ; so T1_EPTR = T0_EPTR(0x08) + 0x28.
// <bt> is NOT read here any more. target_t+0x7C was taken for the battle target on 2026-07-10 ; measured in combat on
// 2026-09-13 it follows the RETICLE target, and the game's own <bt> is not stored at all -- it is recomputed from the
// entities' claims (read_battle_target below, rule in model/battle_target.h).
static const u32 SUB_CURSOR_BIT = 0x00010000;
static const u32 NO_TARGET = 0x04000000;
// LOCK_OFF : the LOCK-ON flag is BIT 0 of the byte at +0x5C (the upper bits carry other target flags -- on a
// PC/party target the byte reads 0xF0 unlocked / 0xF1 locked, so mask bit 0 ; a bare !=0 false-locks on allies).
// Reversed 2026-07-02 via //aio tlock (mob: +0x5C flipped 0->1) ; corrected 2026-07-05 (friendly targets carry
// the upper flags). The locked id is T0 @+0x04.

// ---- target_root : the ONE resolution of target_t, and the ONE place that survives a client patch ----
// A hard-coded RVA is a bet that the game's layout never moves ; on 2026-08-12 it moved and the selection
// cursor died silently for everyone. So the RVA is a VARIABLE seeded with the known-good value, and when the
// static stops being a pointer at all (the exact broken state observed : it read 0) we RE-DERIVE it with the
// same structural signature //aio rva uses -- a pointer whose Targets[0].ServerId (+0x04) equals the ServerId
// of the entity at +0x08, that entity being the one entity_array holds at its own index (+0x74). Three
// mutually-confirming facts : nothing else in the image satisfies them, so a hit is the answer, not a guess.
//
// Bounded, and it says so both ways (rule 10) : a scan needs SOMETHING TARGETED to prove itself, so it retries
// on a timer -- window around the seed only (a patch shifts by bytes, this one by 0x40), at most HEAL_MAX
// times. When the budget runs out it logs that it closed, because a healer that dies quietly reads exactly
// like an RVA that was fine all along.
static const u32 HEAL_WINDOW = 0x4000;   // +/- around the seed : covers a patch-sized shift, ~8k candidates
static const int HEAL_EVERY  = 120;      // frames between attempts (~2 s)
static const int HEAL_MAX    = 60;       // ~2 min of play -- long enough that the player targets something

static bool target_sig_ok(u32 v) {
    if (!valid_ptr(v)) return false;
    u32 id0 = 0, e0 = 0;
    safe_read(v + T0_ID_OFF, &id0); safe_read(v + T0_EPTR_OFF, &e0);
    if (!id0 || id0 == NO_TARGET || !valid_ptr(e0)) return false;   // nothing targeted -> cannot prove it, not now
    u32 eid = 0, eidx = 0, back = 0;
    safe_read(e0 + ENT_ID_OFF, &eid); safe_read(e0 + ENT_INDEX_OFF, &eidx); eidx &= 0xFFFF;
    if (eid != id0 || !eidx || eidx >= 0x900) return false;
    u32 ent = entity_array();
    return ent && safe_read(ent + eidx * 4, &back) && back == e0;
}

int target_root_rescan(unsigned* rvaOut, int cap) {
    const int n = fm_image_find(target_sig_ok, rvaOut, cap);
    if (n == 1) fm_adopt(FM_TARGET_T, rvaOut[0], "//aio rva whole-image signature");
    return n;
}

u32 target_root() {
    static int s_frames = 0, s_tries = 0;
    const u32 ffm = ffximain_base();
    if (!ffm) return 0;
    const u32 rva = fm_rva(FM_TARGET_T);
    u32 tp = 0; safe_read(ffm + rva, &tp);
    if (valid_ptr(tp)) {
        // Healthy. Take the first chance to CONFIRM it (needs a target up) so the address gets cached and
        // the next session skips the whole question.
        if (!fm_confirmed(FM_TARGET_T) && target_sig_ok(tp)) fm_adopt(FM_TARGET_T, rva, "structural signature");
        return tp;
    }
    if (s_tries >= HEAL_MAX) return 0;                  // budget spent -- already logged, stay quiet
    if (++s_frames < HEAL_EVERY) return 0;
    s_frames = 0; ++s_tries;
    const u32 lo = (rva > HEAL_WINDOW) ? (rva - HEAL_WINDOW) : 0;
    for (u32 r = lo; r <= rva + HEAL_WINDOW; r += 4) {
        u32 v = 0;
        if (!safe_read(ffm + r, &v) || !target_sig_ok(v)) continue;
        fm_adopt(FM_TARGET_T, r, "structural signature");
        return v;
    }
    if (s_tries == HEAL_MAX)
        windower::debug::log("target_root: FFXiMain+0x%X still dead after %d tries -- selection cursor stays off. "
                             "Target a party member and run //aio rva (the shift is bigger than the 0x%X window).",
                             rva, s_tries, HEAL_WINDOW);
    return 0;
}

bool read_target(TargetInfo& o) {
    o.id = o.sid = 0; o.locked = false;
    u32 tp = target_root();
    if (!tp) return true;                               // target system not ready (or the static moved -- see target_root)
    u32 t0 = 0, t1 = 0, flags = 0, lk = 0;
    safe_read(tp + T0_ID_OFF, &t0);                     // active reticle
    safe_read(tp + T1_ID_OFF, &t1);                     // locked main (valid during sub-target)
    safe_read(tp + FLAGS_OFF, &flags);
    safe_read(tp + LOCK_OFF,  &lk);                     // lock-on flag (1 = locked)
    if (flags & SUB_CURSOR_BIT) { o.sid = t0; o.id = t1; }   // <st> cursor open : sub = reticle, main = locked
    else                        { o.id = t0; }               // normal : main = reticle, no sub
    if (o.id  == NO_TARGET) o.id  = 0;
    if (o.sid == NO_TARGET) o.sid = 0;
    o.locked = (lk & 0x01) != 0 && (o.id != 0);         // locked ON the main target : bit 0 of +0x5C (the upper bits carry OTHER flags on friendly/PC targets -- a bare !=0 false-locked on party members)
    return true;
}

// The ACTIVE target's ENTITY (name / HP% / id / index) for the Target HUD module. Reversed
// 2026-07-03 via //aio tent (see docs/game-data/target/target-substruct.md). The entity struct is reached
// DIRECTLY off target_t (no id->index scan) :
//   target_t + 0x08   u32  Targets[0].EntityPointer  -> the reticle's entity struct  (match=1 in every probe)
//   entity   + 0x74   u16  Index
//   entity   + 0x78   u32  ServerId
//   entity   + 0x7C   char[0x18]  Name (ASCII, NUL-padded)
//   entity   + 0xEC   u8   HP% (0..100)  -- the ONLY 0..100 byte that tracked damage : a 9% mob read 9 here
//                          while +0xDC/+0xE0 stayed pinned at 100 (so those are NOT HP%).
// Fill a TargetEntity from an entity-struct pointer (name / HP% / id / status / claim / spawn / speed / pos /
// heading). Leaves o invalid (o.valid=false) on a bad pointer or an empty/sentinel id.
static void read_entity_fields(u32 ep, TargetEntity& o) {
    o = TargetEntity{};
    if (!valid_ptr(ep)) return;
    u32 id = 0; safe_read(ep + ENT_ID_OFF, &id);
    if (id == 0 || id == NO_TARGET) return;             // empty / sentinel
    u32 idx = 0, hpp = 0;
    safe_read(ep + ENT_INDEX_OFF, &idx);
    safe_read(ep + ENT_HPP_OFF,   &hpp);
    int i = 0;                                          // name : ASCII at entity+0x7C (as read_player does)
    for (; i < 23; ++i) { u32 c = 0; if (!safe_read(ep + ENT_NAME_OFF + i, &c)) break; char ch = (char)(c & 0xFF); if (!ch) break; o.name[i] = ch; }
    o.name[i] = 0;
    o.id = id; o.index = idx & 0xFFFF;
    o.hpp = (int)(hpp & 0xFF); if (o.hpp > 100) o.hpp = 100;
    { u32 st = 0, cl = 0, sp = 0, pf = 0; safe_read(ep + ENT_STATUS_OFF, &st); safe_read(ep + ENT_CLAIM_OFF, &cl);
      safe_read(ep + ENT_SPAWN_OFF, &sp); safe_read(ep + ENT_PFLAGS_OFF, &pf);
      o.status = st; o.claimId = cl; o.spawnType = sp & 0xFF; o.pflags = pf; }   // the type is ONE byte : the next one is set on self (0x20D read whole)
    { u32 ms = 0, xx = 0, zz = 0, hh = 0;  // floats read as raw dwords (safe_read is u32-typed), then bit-copied
      safe_read(ep + ENT_SPEED_OFF, &ms); safe_read(ep + ENT_X_OFF, &xx); safe_read(ep + ENT_Z_OFF, &zz); safe_read(ep + ENT_HEADING_OFF, &hh);
      memcpy(&o.moveSpeed, &ms, 4); memcpy(&o.posX, &xx, 4); memcpy(&o.posZ, &zz, 4); memcpy(&o.heading, &hh, 4); }   // movement speed + position + facing (radians)
    o.valid = true;
}

// Read the MAIN target entity AND, when a <st> sub-target cursor is up, the SUB-target entity. Normal targeting :
// main = Targets[0] (reticle), no sub. <st> open : main = Targets[1] (the locked target), sub = Targets[0] (reticle).
bool read_target_entity(TargetEntity& main, TargetEntity& sub, bool& hasSub) {
    main = TargetEntity{}; sub = TargetEntity{}; hasSub = false;
    u32 tp = target_root();                             // same resolution as read_target : patch-healing lives there
    if (!tp) return true;                               // target system not ready
    u32 flags = 0; safe_read(tp + FLAGS_OFF, &flags);
    u32 mep = 0;
    if (flags & SUB_CURSOR_BIT) {                       // <st> cursor : main = locked (Targets[1]), sub = reticle (Targets[0])
        u32 sep = 0; safe_read(tp + T0_EPTR_OFF, &sep); read_entity_fields(sep, sub); hasSub = sub.valid;
        safe_read(tp + T1_EPTR_OFF, &mep);
    } else {
        safe_read(tp + T0_EPTR_OFF, &mep);              // normal : main = reticle, no sub
    }
    read_entity_fields(mep, main);
    return true;
}

// Action menu (reversed 2026-06-27). Statics in FFXiMain :
//   +0x5EEDAC  u32  live-menu pointer : 0 closed, heap ptr while any menu is open ; *(ptr+0x04) = menu DEF
//   +0x634F68  u32  "examined SPELL" id   (the game writes it in the Magic menu to show MP Cost / recast)
//   +0x6345D0  u32  "examined ABILITY" id + 0x200 (Job-Ability / Weapon-Skill menus ; -0x200 = raw id)
//   (all three are the 2026-08-12 post-patch addresses ; pre-patch they were 0x5EED6C / 0x634F28 / 0x634590)
//
// ZERO-TAP menu type (reversed 2026-06-27 via //aio menu) : the def carries the menu's INTERNAL NAME
// inline -- two 8-byte fields at def+0x46 : a constant "menu    " tag then the menu name at def+0x4E.
//   "magic   " -> spell list (examine cache 0x634F28 holds the highlighted spell id)
//   "ability " -> the JOB-ABILITY *and* WEAPON-SKILL lists -- SAME menu name. The examine cache
//                 0x634590 disambiguates by FFXI's unified id space : Job Abilities are id + 0x200
//                 (araw >= 0x200), Weapon Skills are the raw id in the low range (araw < 0x200).
//   "abiselec" -> the Abilities CATEGORY selector (no item examined -> cache is 0xFFFFFFFF) -> ignored
// This replaces the old "learn which def changed the cache" trick : no cursor tap needed, and it is
// stable across sessions (it's read from the def, not the per-session heap pointer value).
// 2026-08-12 client patch : the live-menu pointer moved 0x5EED6C -> 0x5EEDAC, the same +0x40 shift that hit
// target_t. PROVEN by running //aio rva twice : that slot's def name read 'magic' with the Magic menu open and
// 'ability' with the Abilities menu open, while the three other menu-shaped slots never changed. (An always-
// 'ability' slot at 0x455C60 is a cache, not the focus -- it stayed 'ability' while Magic was open.)
// The three addresses now live in ffximain_rva.cpp (fm_addr), which re-derives them after a client patch :
// the menu pointer by watching which slot's name follows the menu you open, the examine caches by decoding
// their value back to a real spell / ability name. Only the STRUCTURE offsets stay here -- those belong to
// the menu object, not to the module, and a recompile does not move them.
static const u32 MENU_NAME_OFF = 0x4E, MENU_TAG_OFF = 0x46;   // def+0x46 = "menu    ", def+0x4E = name

// read `n` (<=8) bytes of inline ASCII at addr into out (NUL-terminated).
static void read_tag(u32 addr, char* out, int n) {
    int i = 0; for (; i < n; ++i) { u32 c = 0; if (!safe_read(addr + i, &c)) break; out[i] = (char)(c & 0xFF); }
    out[i] = 0;
}

bool read_action_menu(int& type, unsigned& id, unsigned& cursor, bool& examValid) {
    type = 0; id = 0; cursor = 0; examValid = false;
    u32 ffm = ffximain_base();
    if (!ffm) return false;
    u32 mptr = 0; safe_read(fm_addr(FM_MENU_PTR), &mptr);
    if (!valid_ptr(mptr)) return false;                   // no menu open
    safe_read(mptr + 0x4C, &cursor);                      // 1-based highlight index -> stale-examine detection
    u32 def = 0; safe_read(mptr + 0x04, &def);            // menu category definition
    if (!valid_ptr(def)) return false;
    // The menu's shared examine-DESCRIPTION object (*(mptr+0x0C), a singleton) is the structural "is there a
    // real examinable item here" signal. A no-magic job's EMPTY magic list leaves it ALL-ZERO ; a REAL spell
    // populates it. Two fields matter (reversed via a WHM-vs-WAR/DNC same-state dump, both on the first spell):
    //  - +0x34 (dsent) : 0xFFFFFFFF when empty, else a non-sentinel value -- BUT the game only clears it once
    //    the cursor SETTLES/moves, so on the very first open it is 0xFFFFFFFF even for a real spell.
    //  - +0x3C : the description's text metric, non-zero the moment the menu is BUILT for a real item, 0 when
    //    empty. THIS is what shows the auto-selected FIRST spell immediately without a cursor nudge.
    // So a real item = (dsent != 0xFFFFFFFF) OR (+0x3C != 0) ; the WAR/DNC ghost is all-zero -> stays hidden.
    { u32 desc = 0; safe_read(mptr + 0x0C, &desc);
      if (valid_ptr(desc)) { u32 dsent = 0xFFFFFFFF, d3C = 0; safe_read(desc + 0x34, &dsent); safe_read(desc + 0x3C, &d3C);
                             examValid = (dsent != 0xFFFFFFFF) || (d3C != 0); } }
    char tag[9]; read_tag(def + MENU_TAG_OFF, tag, 4);    // self-validate : real menu defs start "menu"
    if (tag[0] != 'm' || tag[1] != 'e' || tag[2] != 'n' || tag[3] != 'u') return false;
    char nm[9]; read_tag(def + MENU_NAME_OFF, nm, 8);     // 8-byte menu name -> menu type




    if (nm[0]=='m' && nm[1]=='a' && nm[2]=='g' && nm[3]=='i' && nm[4]=='c') {        // "magic   " (real spells AND trusts)
        u32 spell = 0; safe_read(fm_addr(FM_EXAM_SPELL), &spell);
        type = 1; id = (spell == 0 || spell > 0x4000) ? 0 : spell; return true;     // menu IS open (frame shows) ;
        // GHOST gate = the description-object sentinel (examValid, computed above : dsent != 0xFFFFFFFF). A
        // no-magic job's EMPTY magic menu never populates it (stays 0xFFFFFFFF) -> the stale EXAM_SPELL ghost
        // stays hidden. A real spell populates it as soon as the cursor settles/moves. NB: WHM and WAR magic
        // menus are byte-identical at the mptr level on the very first frame (mptr+0x24 is the 14-row display
        // CAPACITY, not the spell count ; the real count is buried in a +0x04-linked item chain) -> the first
        // spell shows once you nudge the cursor. Not gating on the item chain to keep the poller cheap.
        // id 0 = nothing valid examined yet. The caller filters the GHOST via the live-examine check + the
        // box draws an EMPTY frame whenever the magic menu is open (id may be a trust / stale until proven live).
    }
    if (nm[0]=='a' && nm[1]=='b' && nm[2]=='i' && nm[3]=='l') {                      // "ability " (JA + WS list)
        u32 araw = 0; safe_read(fm_addr(FM_EXAM_ABIL), &araw);                         // NB "abiselec" (nm[3]='s') is excluded
        if (araw >= 0x200 && araw <= 0x200 + 0x4000) { type = 2; id = araw - 0x200; return true; }  // Job Ability
        if (araw >= 1 && araw < 0x200)               { type = 3; id = araw;         return true; }  // Weapon Skill
        return false;                                                               // 0 / 0xFFFFFFFF : not populated
    }
    return false;                                         // any other menu : no box
}

// Ability/Job-Ability RECAST, reversed from LuaCore's get_ability_recasts. The client keeps two parallel
// 32-slot arrays hung off the data root, at offsets that are NOT constants -- Windower 4.7.9.3 slid the
// whole recast block by +4 (timers 0x22C->0x230, ids 0x230->0x234, spells 0x234->0x238), so they are
// derived from LuaCore's own code like the root itself (model/luacore_root.h) :
//   *(g + lc_recast_ja_timers()) -> int32[32]  remaining recast in 1/60 s (frames) ; seconds = timer / 60
//   *(g + lc_recast_ja_ids())    -> stride-8 entries, byte[0] = the slot's recast_id
// Windower builds recasts[id] = timer/60. We do the inverse : given the highlighted JA's recast_id,
// scan the 32 slots for an ACTIVE one (timer>0) whose id matches -> its remaining seconds (0 = ready).
// recast_id comes from abilities_gen.h (caller side). This is the menu's exact "Next".
unsigned ability_recast_sec(unsigned recast_id, unsigned* ticksOut) {
    if (ticksOut) *ticksOut = 0;
    u32 g = data_root(); if (!g) return 0;
    u32 idsP = 0, timersP = 0;
    safe_read(g + lc_recast_ja_ids(), &idsP); safe_read(g + lc_recast_ja_timers(), &timersP);
    if (!valid_ptr(idsP) || !valid_ptr(timersP)) return 0;
    for (int s = 0; s < 32; ++s) {
        u32 t = 0; safe_read(timersP + s * 4, &t);
        if ((int)t <= 0 || t > 60u * 7200u) continue;          // empty/ready slot, or garbage (>2h)
        u32 idb = 0; safe_read(idsP + s * 8, &idb);
        if ((idb & 0xFF) == recast_id) {
            if (ticksOut) *ticksOut = t;                       // the RAW 1/60 s counter : a charge pool needs it
            return (t + 59) / 60;                              // ceil to whole seconds (the "Next")
        }
    }
    return 0;                                                  // not on recast
}

// Spell RECAST ("Next" for the Magic menu), reversed from LuaCore's get_spell_recasts (the cclosure pushed
// just before the "get_spell_recasts" setfield ; the memory's old FUN_100732B0 guess was wrong, that one is
// get_abilities). Far simpler than abilities : NO 32-slot scan -- a flat array.
//   base = *(g + lc_recast_spells()) -> ushort[1024], indexed directly by recast_id, remaining in 1/60 s.
//   Windower builds recasts[id] = base[id] ; seconds = base[id] / 60. It sits one dword after the ability
//   ids table, which is how the two derivations cross-check each other (luacore_root.h). recast_id comes
//   from spells_gen.h (SpellRow::recast_id).
// LIST all active recasts -> parallel arrays. Job abilities : the 32-slot table. Spells : ushort[1024]
// indexed by recast_id (block-copied under SEH, then scanned).
int read_recasts(unsigned short* rid, unsigned char* kind, int* sec, int maxN, int* ticks) {
    int n = 0;
    u32 g = data_root(); if (!g) return 0;
    u32 idsP = 0, timersP = 0, spellB = 0;
    safe_read(g + lc_recast_ja_ids(), &idsP); safe_read(g + lc_recast_ja_timers(), &timersP);
    safe_read(g + lc_recast_spells(), &spellB);
    if (valid_ptr(idsP) && valid_ptr(timersP)) {
        for (int s = 0; s < 32 && n < maxN; ++s) {
            u32 t = 0; safe_read(timersP + s * 4, &t);
            if ((int)t <= 0 || t > 60u * 7200u) continue;          // ready / empty / garbage
            u32 idb = 0; safe_read(idsP + s * 8, &idb);
            rid[n] = (unsigned short)(idb & 0xFF); kind[n] = 0; sec[n] = ((int)t + 59) / 60; if (ticks) ticks[n] = (int)t; ++n;
        }
    }
    if (valid_ptr(spellB)) {
        static unsigned short sr[1024]; bool ok = true;
        __try { memcpy(sr, (const void*)spellB, sizeof(sr)); }
        __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
        if (ok) for (int i = 0; i < 1024 && n < maxN; ++i) {
            const unsigned v = sr[i];
            if (v == 0 || v > 60u * 7200u) continue;
            rid[n] = (unsigned short)i; kind[n] = 1; sec[n] = ((int)v + 59) / 60; if (ticks) ticks[n] = (int)v; ++n;
        }
    }
    return n;
}
unsigned spell_recast_sec(unsigned recast_id) {
    if (recast_id >= 0x400) return 0;                          // array is exactly 1024 entries
    u32 g = data_root(); if (!g) return 0;
    u32 base = 0; if (!safe_read(g + lc_recast_spells(), &base) || !valid_ptr(base)) return 0;
    u32 v = 0; if (!safe_read(base + recast_id * 2, &v)) return 0;   // 32-bit read, keep the low ushort
    v &= 0xFFFF;                                               // little-endian : this entry, ignore the next
    if (v == 0 || v > 60u * 7200u) return 0;                   // ready, or garbage (>2h)
    return (v + 59) / 60;                                     // ceil to whole seconds (the "Next")
}

// Per-frame snapshot : read each pointer-chain ONCE so widgets never touch memory in draw().
// See gamestate.h. read_player gates "in game" (vitals populate before names after a zone).
// SCHOLAR GRIMOIRE : compute the book/charges/timer once/frame (ported from AioHUD targetbar/sch.lua).
static bool read_job_spent(unsigned job, unsigned& spent) {   // Spent Job Points for `job` (job_point_info +0x04)
    if (job < 1 || job > 23) return false;
    u32 g = data_root(); if (!g) return false;
    u32 base = 0; if (!safe_read(g + 0x48, &base) || !valid_ptr(base)) return false;
    u32 w = 0; if (!safe_read(base + 0x306 + (job - 1) * 6 + 4, &w)) return false;
    spent = w & 0xFFFF; return true;
}
// Merit level (0..5) for merit id `mid` and Job-Point gift rank (0..) for gift id `gid`. Both LuaCore-mirrored
// arrays are indexed by (id>>1) : merit struct = *(g+0x44), jp struct = *(g+0x48). Reversed from the get_player()
// builders (merit 2320 "Enhancing Magic Duration" -> +0x488 ; RDM JP gift 338 -> +0xA9). u8 field.
int read_merit_level(unsigned mid) {
    u32 g = data_root(); if (!g) return 0;
    u32 base = 0; if (!safe_read(g + 0x44, &base) || !valid_ptr(base)) return 0;
    u32 v = 0; if (!safe_read(base + (mid >> 1), &v)) return 0;
    return (int)(v & 0xFF);
}
int read_jp_gift_rank(unsigned gid) {
    u32 g = data_root(); if (!g) return 0;
    u32 base = 0; if (!safe_read(g + 0x48, &base) || !valid_ptr(base)) return 0;
    u32 v = 0; if (!safe_read(base + (gid >> 1), &v)) return 0;
    return (int)(v & 0xFF);
}
// raw u8 at *(g+0x48)+off (the job-point block). Used for BRD JOB POINT ranks -- NOT merits, and the distinction
// cost a real misunderstanding: Marcato and Tenuto have no merit category at all, they are job-point categories
// (20 ranks each, bought with capacity points at 99). +0x142 = "Tenuto Effect" (+2 s of song duration per rank,
// measured +40 s at 20/20) ; +0x148 = "Marcato Effect" (+1 s per rank, measured +20 s at 20/20).
// Timers.dll reads these off the same stat block.
int read_jp_u8(unsigned off) {
    u32 g = data_root(); if (!g) return 0;
    u32 base = 0; if (!safe_read(g + 0x48, &base) || !valid_ptr(base)) return 0;
    u32 v = 0; if (!safe_read(base + off, &v)) return 0;
    return (int)(v & 0xFF);
}
// STRATAGEM CHARGES -- the rule the client implements, not a table of magic numbers.
// The client keeps ONE recast slot (id 231) for the WHOLE stratagem pool, and it counts down to the pool
// being FULL again, never to the next charge. Charges come from the SCHOLAR LEVEL in play (10/30/50/70/90
// -> 1/2/3/4/5) and the pool always refills in 240 s, so one charge = 240 / charges (240/120/80/60/48 s).
// The Job-Point GIFT "Stratagem Recast Time" (SCH, 550 JP SPENT) takes 15 s off that interval : 48 -> 33 s.
// GIFTS ARE MAIN-JOB ONLY. The ported sch.lua applied the gift FIRST and then clamped a subjob to a flat
// "3 charges / 80 s". Read gs.me.slvl instead and the clamp is not needed : MASTER LEVELS raise the subjob
// level past the old 49 cap (measured : PLD ML 48 -> sub level 58), so a /SCH on a mastered character is
// over 50 and the table gives it 3 charges / 80 s by itself -- while a character with little or no Master
// Level gets the 2 charges / 120 s that ARE right for a level-49 subjob, which the flat clamp got wrong.
// The same ordering also took a level-restricted SCH main (mlvl under 90, gift bought) to the wrong row.
static void sch_recast_info(int level, int jpSpent, bool isMain, int& interval, int& charges) {
    if      (level >= 90) charges = 5;
    else if (level >= 70) charges = 4;
    else if (level >= 50) charges = 3;
    else if (level >= 30) charges = 2;
    else if (level >= 10) charges = 1;
    else                  charges = 0;                        // below 10 there is no stratagem at all
    interval = charges ? 240 / charges : 240;
    if (isMain && level >= 90 && jpSpent >= 550) interval -= 15;   // the 550 JP gift : 48 -> 33 s per charge
}

// //aio schlog -- the stratagem pool, every time it MOVES. It logs the DECISION (level, spent JP, and the
// interval / max charges they produce) next to the raw 1/60 s counter, and it MEASURES the interval the
// client actually uses : spending a charge adds exactly ONE interval to the pool timer, so the jump between
// two frames is that interval minus the frame that elapsed. A measured value that disagrees with the table
// is the whole point of the capture -- the line says so.
static unsigned g_schLogUntil = 0;
static int      g_schPrevTicks = -1;    // raw pool counter last frame (-1 = nothing seen yet)
static unsigned g_schPrevLogMs = 0;     // when that sample was taken : a jump has to be corrected for the frame that passed
static int      g_schPrevSec   = -1;    // last whole second logged (one line per second, not per frame)
static int      g_schPrevCount = -1;
void sch_log_arm(int seconds) {
    const int sec = (seconds > 0) ? seconds : 180;
    g_schLogUntil = model_now_ms() + (unsigned)sec * 1000u;
    g_schPrevTicks = -1; g_schPrevSec = -1; g_schPrevCount = -1;
    windower::debug::log("SCHLOG armed for %d s -- spend stratagems (main AND /SCH) so the jumps measure the interval", sec);
}
static void sch_log_tick(int ticks, int interval, int tableInt, bool learned, int maxCharges, int jpSpent,
                         int level, bool isMain, int count, int nextSec) {
    if (!g_schLogUntil) return;
    if ((int)(model_now_ms() - g_schLogUntil) >= 0) {   // window over : SAY SO. A probe that dies quietly
        windower::debug::log("SCHLOG window closed");   // reads exactly like a bug that is not happening.
        g_schLogUntil = 0; return;
    }
    const int jump = (g_schPrevTicks >= 0 && ticks > g_schPrevTicks) ? (ticks - g_schPrevTicks) : 0;
    if (jump > 0) {   // a charge was just spent : jump + what the pool lost between the two samples IS one interval
        unsigned dms = model_now_ms() - g_schPrevLogMs; if (dms > 2000u) dms = 2000u;
        const int elapsed = (int)(dms * 60u / 1000u);
        const int meas = (jump + elapsed + 30) / 60;   // nearest second -- debug::log takes no %f
        windower::debug::log("SCHLOG SPENT : pool %d -> %d ticks (+%d, +%d elapsed) => measured interval %d s -- table says %d s%s",
                             g_schPrevTicks, ticks, jump, elapsed, meas, tableInt,
                             (meas == tableInt) ? " OK" : "  <<< the table is not what the client uses -- the learned value wins");
    }
    const int sec = ticks / 60;
    if (jump > 0 || sec != g_schPrevSec || count != g_schPrevCount) {
        windower::debug::log("SCHLOG %s SCH lvl=%d jpSpent=%d -> interval=%ds (%s, table %ds) max=%d | pool=%d ticks (%d s) charges=%d next=%ds%s",
                             isMain ? "main" : "sub", level, jpSpent, interval,
                             learned ? "LEARNED off a spend" : "table", tableInt, maxCharges,
                             ticks, sec, count, nextSec,
                             (maxCharges > 0 && ticks > maxCharges * interval * 60)
                                 ? "  <<< pool LONGER than max*interval : the interval is wrong" : "");
    }
    g_schPrevTicks = ticks; g_schPrevSec = sec; g_schPrevCount = count; g_schPrevLogMs = model_now_ms();
}
static void compute_grimoire(GameState& gs) {
    // buffsOk false = the buff list is UNAVAILABLE this frame (a transient read miss), NOT "no buffs". Recomputing
    // the book from an empty list flickered the grimoire to the CLOSED (no-Arts) art. Keep last frame's state.
    if (!gs.buffsOk) return;
    GrimoireState g{};
    const int SCH = 20;
    auto hasBuff = [&](int id) { for (int i = 0; i < gs.nbuff; ++i) if (gs.buffs[i] == (unsigned short)id) return true; return false; };
    const bool isMain = gs.me.mjob == SCH, isSub = gs.me.sjob == SCH;
    bool visible = false;
    if (isMain) visible = true;
    else if (isSub && !hasBuff(157)) {                       // 157 = sub-job restriction
        const bool zoneRestrict = (gs.zone == 298 || gs.zone == 39 || gs.zone == 40 || gs.zone == 41 || gs.zone == 42) && gs.me.slvl == 0;
        if (!zoneRestrict) visible = true;
    }
    if (!visible) { gs.grimoire = g; return; }
    g.visible = true;
    if      (hasBuff(401)) { g.book = 0; g.addendum = true; }   // Addendum: White
    else if (hasBuff(402)) { g.book = 1; g.addendum = true; }   // Addendum: Black
    else if (hasBuff(358)) { g.book = 0; }                      // Light Arts
    else if (hasBuff(359)) { g.book = 1; }                      // Dark Arts
    else                   { g.book = 0; g.dim = true; g.closed = true; }   // no Arts / Addendum at all -> the CLOSED book (no charges / recast)
    const int level = isMain ? gs.me.mlvl : gs.me.slvl;
    unsigned spent = 0; if (isMain) read_job_spent(SCH, spent);   // a gift only counts on the MAIN job -- do not even read it for a /SCH
    int interval = 240, maxCharges = 0; sch_recast_info(level, (int)spent, isMain, interval, maxCharges);
    const int tableInt = interval;   // what the level/JP table says, kept for the log : the learned value overwrites `interval`
    // The pool timer in RAW 1/60 s, not the ceil-ed seconds every other reader gets. Spending one charge sets
    // the pool to EXACTLY one interval, and a ceil-ed "33" against a 33 s interval falls on the wrong side of
    // every `<` in a charge ladder -- one stratagem out of a full book used to read as two spent, for a second.
    // Read the slot DIRECTLY rather than looking it up in gs.recasts : that snapshot stops at 40 entries, and a
    // pool silently truncated away would read as "every charge available".
    unsigned rawTicks = 0; ability_recast_sec(231, &rawTicks);
    const int ticks = (int)rawTicks;                           // 0 = the slot is not on recast at all = pool full
    // LEARN the interval instead of trusting the table. Spending a stratagem adds EXACTLY one interval to the
    // pool, so the client states its own number every time you use one. Two corrections make that jump usable :
    //  * the pool ALSO fell between the two polls, by however long the frame took -- add that back, or a hitch
    //    of half a second reads as one second less. Measured : a first pass took the SMALLEST jump seen and
    //    learned 47 s on a client whose four clean spends had all said 48 (//aio schlog, 2026-09-16). The
    //    minimum is not the truth : double spends push a jump UP, slow frames push it DOWN.
    //  * a jump can never be LONGER than the table (the 550 JP gift only shortens it), which is exactly what
    //    two stratagems inside one frame would produce -- so reject those, and keep the LARGEST of what is
    //    left. Slow frames can then only lose to a cleaner measurement, and a first bad value self-repairs.
    // The table stays the answer before the first spend of a session.
    static unsigned g_schKey = 0, g_schPrevMs = 0;             // what the learned value belongs to / when it was last sampled
    static int g_schLearned = 0, g_schLastTicks = -1;          // seconds (0 = nothing learned yet)
    const unsigned nowMs = model_now_ms();
    const unsigned key = (unsigned)(isMain ? 1 : 0) | ((unsigned)level << 1) | ((unsigned)maxCharges << 9);
    if (key != g_schKey) { g_schKey = key; g_schLearned = 0; g_schLastTicks = -1; }   // job / level changed : forget
    if (hasBuff(377)) { g_schLearned = 0; g_schLastTicks = -1; }   // Tabula Rasa refills the pool and moves its pace : never learn from it
    else {
        if (g_schLastTicks >= 0 && ticks > g_schLastTicks) {
            unsigned dms = nowMs - g_schPrevMs; if (dms > 2000u) dms = 2000u;          // a zone-in is not a frame
            const int elapsed = (int)(dms * 60u / 1000u);                              // what the pool lost while we were away
            const int meas = (ticks - g_schLastTicks + elapsed + 30) / 60;             // one interval, to the nearest second
            if (meas >= 15 && meas <= tableInt && meas > g_schLearned) g_schLearned = meas;
        }
        g_schLastTicks = ticks; g_schPrevMs = nowMs;
    }
    if (g_schLearned > 0) interval = g_schLearned;
    // Last resort, table or learned : the pool can never run LONGER than maxCharges intervals. A pool that does
    // proves the interval is too short -- fall back to the ungifted 240/charges rather than report zero charges
    // to a Scholar holding three. (The gifted 33 s row is the one value no capture has confirmed : the Scholar
    // measured on 2026-09-16 did not own the gift, so it rests on GearInfo's Gifts.lua alone.)
    if (maxCharges > 0 && ticks > maxCharges * interval * 60) { interval = 240 / maxCharges; g_schLearned = 0; }
    const int per = interval * 60;                             // one charge, in 1/60 s
    const int recharging = (ticks > 0 && per > 0) ? (ticks + per - 1) / per : 0;   // ceil : charges still coming back
    int count = maxCharges - recharging; if (count < 0) count = 0;
    g.charges  = count;
    g.timerSec = (ticks > 0) ? ((ticks - (recharging - 1) * per + 59) / 60) : -1;   // to the NEXT charge (ceil), -1 = pool full
    g.interval = interval; g.maxCharges = maxCharges;   // what the count was decided from : the harness judges THESE, not a table it re-derives
    gs.grimoire = g;
    sch_log_tick(ticks, interval, tableInt, g_schLearned > 0, maxCharges, (int)spent, level, isMain, count, g.timerSec);
}

void poll_game_state(GameState& gs) {
    fm_tick();                                               // FFXiMain statics : validate / re-derive whatever is due
    PlayerCacheScope _plc;                                   // cache read_player for this whole poll cycle (hit ~5x below)
    PlayerInfo me;
    if (!read_player(me)) { gs.inGame = false; return; }     // not ready (zoning) -> keep last-good
    gs.inGame = true;
    gs.me = me;
    windower::debug::set_tag(me.name);   // stamp every log line with WHO wrote it : all clients of a multi-box share one log file

    gs.hp = me.hpp / 100.0f;
    gs.mp = me.mpp / 100.0f;
    gs.tp = me.tp / 3000.0f; if (gs.tp > 1.0f) gs.tp = 1.0f; if (gs.tp < 0.0f) gs.tp = 0.0f;
    gs.nbuff = read_player_buffs(gs.buffs, 32, &gs.buffsOk);   // self status icons -> the Player Hub buff tray (snapshot, not poll-in-draw)
    { unsigned short rid[40]; unsigned char kd[40]; int sc[40]; int tk[40] = { 0 };   // Timers module : active JA + spell recasts (snapshot)
      const int nr = read_recasts(rid, kd, sc, 40, tk); gs.nRecast = (nr > 40) ? 40 : nr;
      for (int i = 0; i < gs.nRecast; ++i) { gs.recasts[i].recastId = rid[i]; gs.recasts[i].kind = kd[i]; gs.recasts[i].sec = sc[i]; gs.recasts[i].ticks = tk[i]; } }
    { float ms = 0.0f; gs.meSpeed = read_self_speed(me.id, ms) ? ms : 0.0f; }   // own movement speed -> Player Hub speed band
    { unsigned gv = 0; gs.meGil = read_player_gil(gv) ? gv : 0; }               // own gil -> Player Hub gil band
    gs.jaOk = read_usable_ja_bits(gs.jaBits);                                    // usable-JA bitmap -> snapshot, NOT re-read from the draw path
    gs.equipValid = read_equipment(gs.equip);                                    // 16 equipped items -> Equipment Viewer grid
    if (!gs.equipValid) gs.equip = EquipSet{};                                   // not ready (zone / not-logged-in) : zero it AND flag it so the viewer keeps its cached icons

    // minimap : zone + self world position/heading + the zone's map calibration record (snapshot once/frame)
    gs.zone = zone_id();
    { u32 ent = self_entity();
      if (ent) { u32 xx = 0, zz = 0, hh = 0; safe_read(ent + ENT_X_OFF, &xx); safe_read(ent + ENT_Z_OFF, &zz); safe_read(ent + ENT_HEADING_OFF, &hh);
                 gs.meX = *(float*)&xx; gs.meZ = *(float*)&zz; gs.meHeading = *(float*)&hh; } }
    read_map_record(gs.zone, current_submap(), gs.map);   // real floor (multi-level zones) -> right map page
    // Gated on the minimap being ON. This is a 9 KB guarded block copy + a 2304-slot sweep + ~7 safe_read per live
    // slot, EVERY frame -- in a crowded city that is thousands of guarded reads for data nobody draws when the
    // module is switched off (Minimap::draw returns on !mmShow long after the work is already paid for).
    gs.mapEntN = ui_config().mmShow ? read_map_entities(gs.mapEnts, MAP_ENT_MAX) : 0;   // PC/NPC/mob markers
    // 400 slots against a zone that can hold more entities than that -- a city at prime time, Escha with two
    // alliances up. What does not fit is simply not on the map, and a minimap missing blips looks exactly like
    // a quiet zone. (Sampled only while the minimap is ON : with it off the count is 0 by construction.)
    if (ui_config().mmShow) capwatch("model.mapents", gs.mapEntN, MAP_ENT_MAX);
    gs.vana = vana_clock_now();                                // Vana'diel clock (computed, no memory read)

    TargetInfo tg;
    if (read_target(tg)) { gs.targetId = tg.id; gs.subTargetId = tg.sid; gs.targetLocked = tg.locked; }
    else                 { gs.targetId = gs.subTargetId = 0; gs.targetLocked = false; }
    // <bt> : YOU and your PARTY, from the roster (last frame's refresh : the poll runs before the model upkeep).
    { const PartyState& ps = party();
      unsigned ids[6]; int n = 0;
      for (int i = 0; i < ps.count && n < 6; ++i) if (ps.m[i].id) ids[n++] = ps.m[i].id;
      unsigned bt = 0;
      gs.battleTargetOk = read_battle_target(ids, n, bt);   // false = could not be read : NOT the same as "no <bt>"
      gs.battleTargetId = gs.battleTargetOk ? bt : 0; }

    { TargetEntity te, se; bool hasSub = false;
      read_target_entity(te, se, hasSub);
      gs.target = te; gs.subTarget = se; gs.hasSubTarget = hasSub && se.valid; }

    PartyLeaders ld;
    if (read_party_leaders(ld)) { gs.allianceLeader = ld.alliance; gs.partyLead1 = ld.p1; gs.partyLead2 = ld.p2; gs.partyLead3 = ld.p3; }
    else                        { gs.allianceLeader = gs.partyLead1 = gs.partyLead2 = gs.partyLead3 = 0; }

    int mt = 0; unsigned ma = 0, mc = 0; bool mev = false;
    if (read_action_menu(mt, ma, mc, mev)) { gs.menuType = mt; gs.menuAction = ma; gs.menuCursor = mc; gs.menuExamValid = mev; }
    else                                   { gs.menuType = 0; gs.menuAction = 0; gs.menuCursor = mc; gs.menuExamValid = false; }
    // RAW ability examine, read EVERY frame -> a change = the game examined a real ability, used to tell a
    // live Job-Ability/WS selection from a stale one (the Magic box uses menuExamValid instead ; see party.cpp).
    { u32 ffm2 = ffximain_base(); u32 ea = 0;
      if (ffm2) safe_read(fm_addr(FM_EXAM_ABIL), &ea);
      gs.examAbilRaw = ea; }

    // party-window picker : the focused menu is "partywin" with a 1-based cursor index at +0x4C.
    // (Reversed via //aio pcur: +0x4C tracks the hovered member, +0x08 = its row object.)
    gs.partyMenuSel = 0;
    u32 ffm = ffximain_base();
    if (ffm) {
        u32 mptr = 0; safe_read(fm_addr(FM_MENU_PTR), &mptr);
        u32 def = 0; if (valid_ptr(mptr)) safe_read(mptr + 0x04, &def);
        if (valid_ptr(def)) {
            char nm[6] = {0}; for (int i = 0; i < 5; ++i) { u32 c = 0; safe_read(def + MENU_NAME_OFF + i, &c); nm[i] = (char)(c & 0xFF); }
            if (nm[0]=='p' && nm[1]=='a' && nm[2]=='r' && nm[3]=='t' && nm[4]=='y') {     // "partywin"
                u32 idx = 0; safe_read(mptr + 0x4C, &idx);
                if (idx >= 1 && idx <= 6) gs.partyMenuSel = (int)idx;
            }
        }
    }

    // THE CHARACTER SHEET, while the packet has not spoken. 0x061 comes on a login, a job change or a zone, so
    // after a plugin reload the sheet would sit empty until the player happened to walk through a zone line --
    // a feature that looks broken for a reason nobody could guess. The client's own mirror answers immediately.
    // EVERY FRAME, not every fourth. It used to run on a quarter cadence -- 58 bytes, nothing needs 60 Hz --
    // and that "harmless" optimisation put the sheet OUT OF PHASE with the equipment, which IS read every
    // frame. For up to three frames after a gear swap the sixteen item ids were the new set while the gear
    // column still held the old one, and any reader sampling in that window saw a character whose stats did
    // not match their gear. Measured 2026-09-15 on a Holy Circle swap (11 slots) : the in-game harness read
    // "the set moved and the column did not" on 13 of 16 actions, and the column had moved -- one frame later.
    // A snapshot is supposed to be ONE instant ; skewing part of it to save a 58-byte copy is not a saving.
    { CharSheet& cs_ = party().charsheet_mut();
      read_charsheet_mem(cs_);
      // The job table is a SEPARATE read : the 0x061 mirror does not carry it, and it is the one
      // block whose packet may not come back for hours (0x01B needs a job change, not a zone).
      read_job_table_mem(cs_, gs.me.mjob, gs.me.mlvl); }

    compute_grimoire(gs);   // SCH grimoire (book + charges + timer) from buffs / jobs / stratagem recast

    // Last, with the snapshot complete : cross-check what the server said against what memory says. It
    // repairs nothing -- it exists so the day a struct or packet field shifts, we hear about it that day
    // instead of noticing three weeks later that a number has quietly been wrong.
    sentinel_tick(gs);
}

} // namespace aio

// ================================ TAPE WRAPPERS (see the note at the top) ================================
#undef count_item
#undef count_items
#undef data_root
#undef entity_array
#undef entity_id_by_index
#undef entity_name_by_index
#undef entity_pos_verified
#undef key_items_base
#undef owns_key_item
#undef party_ptr
#undef read_capacity_points
#undef read_entities_by_id
#undef read_equipment_ext
#undef read_jp_gift_rank
#undef read_jp_u8
#undef read_merit_level
#undef read_player
#undef read_player_buffs
#undef read_pointwatch
#undef read_treasure_pool
#undef refresh_items
#undef self_party_base
#undef zone_id
#include "model/model_io.h"

namespace aio {

static inline void tap_u32(uint16_t fn, u32 a, u32 b, u32 v) { if (tape_recording()) tape_note(fn, a, b, v ? 1 : 0, &v, 4); }

u32 data_root()                       { const u32 r = data_root__real();        tap_u32(TF_DATA_ROOT, 0, 0, r); return r; }
u32 entity_array()                    { const u32 r = entity_array__real();     tap_u32(TF_ENTITY_ARRAY, 0, 0, r); return r; }
u32 party_ptr()                       { const u32 r = party_ptr__real();        tap_u32(TF_PARTY_PTR, 0, 0, r); return r; }
u32 key_items_base()                  { const u32 r = key_items_base__real();   tap_u32(TF_KEY_ITEMS_BASE, 0, 0, r); return r; }
u32 self_party_base(unsigned selfId)  { const u32 r = self_party_base__real(selfId); tap_u32(TF_SELF_PARTY_BASE, selfId, 0, r); return r; }
unsigned zone_id()                    { const unsigned r = zone_id__real();     tap_u32(TF_ZONE_ID, 0, 0, r); return r; }
unsigned count_item(unsigned id)      { const unsigned r = count_item__real(id); tap_u32(TF_COUNT_ITEM, id, 0, r); return r; }
unsigned entity_id_by_index(unsigned index) { const unsigned r = entity_id_by_index__real(index); tap_u32(TF_ENTITY_ID_BY_INDEX, index, 0, r); return r; }
bool owns_key_item(unsigned id)       { const bool r = owns_key_item__real(id); if (tape_recording()) tape_note(TF_OWNS_KEY_ITEM, id, 0, r ? 1 : 0, 0, 0); return r; }
bool refresh_items()                  { const bool r = refresh_items__real();   if (tape_recording()) tape_note(TF_REFRESH_ITEMS, 0, 0, r ? 1 : 0, 0, 0); return r; }
int read_jp_gift_rank(unsigned gid)   { const int r = read_jp_gift_rank__real(gid); if (tape_recording()) tape_note(TF_READ_JP_GIFT_RANK, gid, 0, r, 0, 0); return r; }
int read_jp_u8(unsigned off)          { const int r = read_jp_u8__real(off);     if (tape_recording()) tape_note(TF_READ_JP_U8, off, 0, r, 0, 0); return r; }
int read_merit_level(unsigned mid)    { const int r = read_merit_level__real(mid); if (tape_recording()) tape_note(TF_READ_MERIT_LEVEL, mid, 0, r, 0, 0); return r; }

int count_items(const unsigned* ids, int n, unsigned* out) {
    const int r = count_items__real(ids, n, out);
    if (tape_recording() && n > 0 && ids && out) tape_note(TF_COUNT_ITEMS, tape_hash(ids, (unsigned)n * 4), (u32)n, r, out, (unsigned)n * 4);
    return r;
}
bool entity_name_by_index(unsigned index, char* out, int sz) {
    const bool r = entity_name_by_index__real(index, out, sz);
    if (tape_recording() && out && sz > 0) tape_note(TF_ENTITY_NAME_BY_INDEX, index, (u32)sz, r ? 1 : 0, out, (unsigned)sz);
    return r;
}
bool entity_pos_verified(unsigned index, unsigned expectId, float& x, float& y, float& z, bool* despawned) {
    bool ghost = false;
    const bool r = entity_pos_verified__real(index, expectId, x, y, z, &ghost);
    if (despawned) *despawned = ghost;
    if (tape_recording()) { struct { float x, y, z; unsigned char g; } d = { x, y, z, (unsigned char)(ghost ? 1 : 0) };
                            tape_note(TF_ENTITY_POS_VERIFIED, index, expectId, r ? 1 : 0, &d, sizeof(d)); }
    return r;
}
bool read_capacity_points(unsigned mainJob, unsigned& cp, unsigned& jp) {
    const bool r = read_capacity_points__real(mainJob, cp, jp);
    if (tape_recording()) { unsigned d[2] = { cp, jp }; tape_note(TF_READ_CAPACITY_POINTS, mainJob, 0, r ? 1 : 0, d, sizeof(d)); }
    return r;
}
int read_entities_by_id(const unsigned* ids, int n, EntityVitals* out) {
    const int r = read_entities_by_id__real(ids, n, out);
    if (tape_recording() && n > 0 && ids && out) tape_note(TF_READ_ENTITIES_BY_ID, tape_hash(ids, (unsigned)n * 4), (u32)n, r, out, (unsigned)(sizeof(EntityVitals) * n));
    return r;
}
bool read_equipment_ext(unsigned short ids[16], unsigned char ext[16][24]) {
    const bool r = read_equipment_ext__real(ids, ext);
    if (tape_recording()) { unsigned char d[16 * 2 + 16 * 24]; memcpy(d, ids, 32); memcpy(d + 32, ext, 16 * 24);
                            tape_note(TF_READ_EQUIPMENT_EXT, 0, 0, r ? 1 : 0, d, sizeof(d)); }
    return r;
}
bool read_player(PlayerInfo& o) {
    const bool r = read_player__real(o);
    if (tape_recording()) tape_note(TF_READ_PLAYER, 0, 0, r ? 1 : 0, &o, sizeof(o));
    return r;
}
int read_player_buffs(unsigned short* out, int maxN, bool* ok) {
    bool okv = false;
    const int r = read_player_buffs__real(out, maxN, &okv);
    if (ok) *ok = okv;
    if (tape_recording() && out && maxN > 0) {
        unsigned char d[1 + 2 * 64]; const int n = maxN < 64 ? maxN : 64;
        d[0] = okv ? 1 : 0; memcpy(d + 1, out, (size_t)n * 2);
        tape_note(TF_READ_PLAYER_BUFFS, (u32)maxN, 0, r, d, 1u + (unsigned)n * 2);
    }
    return r;
}
bool read_pointwatch(PwMem& out) {
    const bool r = read_pointwatch__real(out);
    if (tape_recording()) tape_note(TF_READ_POINTWATCH, 0, 0, r ? 1 : 0, &out, sizeof(out));
    return r;
}
bool read_treasure_pool(TreasureSlot out[10]) {
    const bool r = read_treasure_pool__real(out);
    if (tape_recording()) tape_note(TF_READ_TREASURE_POOL, 0, 0, r ? 1 : 0, out, sizeof(TreasureSlot) * 10);
    return r;
}

} // namespace aio
