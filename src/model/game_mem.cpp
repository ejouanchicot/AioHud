// game_mem.cpp -- see game_mem.h.
#include "model/game_mem.h"
#include "model/gamestate.h"
#include "windower.h"   // safe_read / valid_ptr (guarded game-memory reads)
#include <windows.h>
#include <cstring>

namespace aio {

using windower::safe_read;

// --- game-memory anchors : ONE source of truth for the pointer chains every reader hangs off.
// Module bases are fixed after load (cached) ; the data root `g` is a heap ptr that can be 0 while
// zoning (read each call). All return 0 -> the caller no-ops. Offsets live HERE, nowhere else. ---
u32 ffximain_base() { static u32 b = 0; if (!b) b = (u32)GetModuleHandleA("FFXiMain.dll"); return b; }
u32 luacore_base()  { static u32 b = 0; if (!b) b = (u32)GetModuleHandleA("LuaCore.dll");  return b; }
u32 data_root()   { u32 lc = luacore_base(); u32 g = 0; return (lc && safe_read(lc + 0x1C8400, &g) && valid_ptr(g)) ? g : 0; }   // g = *(LuaCore+0x1C8400)
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

bool read_player_vitals(float& hpFrac, float& mpFrac, float& tpFrac) {
    u32 pl = player_struct();
    if (!pl) return false;
    u32 mhp = 0, hpp = 0, mpp = 0, tp = 0;
    safe_read(pl + 0x60, &mhp);                     // max_hp -> validity gate
    if (mhp == 0 || mhp > 0x100000) return false;   // struct not ready yet
    // hpp/mpp = the game's own % (0..100), so the bar always reads "full = full",
    // adapting to the CURRENT max (gear/food included).
    safe_read(pl + 0x64, &hpp);  safe_read(pl + 0x70, &mpp);  safe_read(pl + 0x74, &tp);
    hpp &= 0xFF;  mpp &= 0xFF;  if (hpp > 100) hpp = 100;  if (mpp > 100) mpp = 100;
    hpFrac = hpp / 100.0f;
    mpFrac = mpp / 100.0f;
    tpFrac = (float)tp / 3000.0f;  if (tpFrac > 1) tpFrac = 1; if (tpFrac < 0) tpFrac = 0;
    return true;
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
static const u32 ENT_SPAWN_OFF   = 0x1D0;   // u32 SpawnType : 0x01 PC, 0x02 NPC, 0x10 Mob

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
        if (sp == 0x01) type = 1; else if (sp == 0x02) type = 2; else if (sp == 0x10) type = 3; else continue;
        u32 rf = 0; safe_read(p + ENT_RENDER_OFF, &rf); if (rf & 0x4000) continue;   // render/valid flag : filters despawned/invalid ghost slots
        u32 xx = 0, zz = 0; if (!safe_read(p + ENT_X_OFF, &xx) || !safe_read(p + ENT_Z_OFF, &zz)) continue;
        const float ex = *(float*)&xx, ez = *(float*)&zz;
        if (ex == 0.0f && ez == 0.0f) continue;                            // unloaded / invalid slot
        u32 id = 0, cl = 0, pf = 0, st = 0, hh = 0;
        safe_read(p + ENT_ID_OFF, &id); safe_read(p + ENT_CLAIM_OFF, &cl); safe_read(p + ENT_PFLAGS_OFF, &pf); safe_read(p + ENT_STATUS_OFF, &st); safe_read(p + ENT_HEADING_OFF, &hh);
        out[n].x = ex; out[n].z = ez; out[n].heading = *(float*)&hh; out[n].type = type;
        out[n].id = id; out[n].claimId = cl; out[n].pflags = pf; out[n].status = (unsigned char)(st & 0xFF);
        ++n;
    }
    return n;
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

// Mobs claimed by a friendly (self / party / alliance ; pet claims land on the owner). One entity-array block-copy.
int read_party_aggro_mobs(const unsigned* friendlyIds, int nFriendly, EntityVitals* out, unsigned* claimOut, int maxN) {
    if (!out || !claimOut || !friendlyIds || nFriendly <= 0 || maxN <= 0) return 0;
    u32 ent = entity_array();
    if (!ent) return 0;
    static u32 ptrs[0x900];
    __try { memcpy(ptrs, (const void*)ent, sizeof(ptrs)); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    int n = 0;
    for (unsigned i = 0; i < 0x900 && n < maxN; ++i) {
        u32 p = ptrs[i];
        if (!valid_ptr(p)) continue;
        u32 sp = 0; if (!safe_read(p + ENT_SPAWN_OFF, &sp) || (sp & 0xFF) != 0x10) continue;   // mobs only (SpawnType 0x10)
        u32 rf = 0; safe_read(p + ENT_RENDER_OFF, &rf); if (rf & 0x4000) continue;             // hidden / despawned ghost
        u32 cl = 0; if (!safe_read(p + ENT_CLAIM_OFF, &cl) || !cl) continue;                   // unclaimed -> skip (packet path handles it)
        bool friendly = false; for (int k = 0; k < nFriendly; ++k) if (friendlyIds[k] == cl) { friendly = true; break; }
        if (!friendly) continue;
        u32 hp = 0; safe_read(p + ENT_HPP_OFF, &hp); if ((hp & 0xFF) == 0) continue;           // dead
        u32 id = 0, st = 0, xx = 0, zz = 0;
        safe_read(p + ENT_ID_OFF, &id); safe_read(p + ENT_STATUS_OFF, &st); safe_read(p + ENT_X_OFF, &xx); safe_read(p + ENT_Z_OFF, &zz);
        EntityVitals& v = out[n];
        v.id = id; v.hpp = (int)(hp & 0xFF); v.status = st; v.claimId = cl; v.spawnType = 0x10;
        v.x = *(float*)&xx; v.z = *(float*)&zz; v.valid = true;
        __try { const char* nm = (const char*)(p + ENT_NAME_OFF); int j = 0; for (; j < 23 && nm[j]; ++j) v.name[j] = nm[j]; v.name[j] = 0; }
        __except (EXCEPTION_EXECUTE_HANDLER) { v.name[0] = 0; }
        claimOut[n] = cl; ++n;
    }
    return n;
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

// PointWatch : Exemplar Points + Limit Points/merits from FFXiMain static data ; Master Level from the LuaCore
// player struct. FFXiMain RVAs (0x48569C exemplar cur/req, 0x485826 limit-points/merit block) reversed via
// //aio pwscan -- client-version-specific. The merit block matches the 0x063 order-2 payload : LP u16 @+0,
// merit-count byte @+2 (low 7 bits), max-merit byte @+4.
static const u32 PW_FM_EXP       = 0x485644;   // u16 Current EXP, u16 Required EXP @+2 (= exemplar-0x58, packet-mirror)
static const u32 PW_FM_MLVL      = 0x485699;   // u8 Master Level (packet 0x65 = exemplar-0x03, same static struct)
static const u32 PW_FM_EXEMPLAR = 0x48569C;   // u32 Current, u32 Required @+4
static const u32 PW_FM_MERIT     = 0x485826;   // u16 Limit Points, byte MeritCount @+2, byte MaxMerit @+4
bool read_pointwatch(PwMem& out) {
    out = PwMem{};
    u32 fm = ffximain_base();
    if (fm) {
        u32 xw = 0;
        if (safe_read(fm + PW_FM_EXP, &xw)) { out.xpCur = xw & 0xFFFF; out.xpTnl = (xw >> 16) & 0xFFFF; out.xpOk = (out.xpTnl != 0); }
        u32 ep = 0, et = 0;
        if (safe_read(fm + PW_FM_EXEMPLAR, &ep) && safe_read(fm + PW_FM_EXEMPLAR + 4, &et)) {
            out.epCur = ep; out.epTnml = et; out.epOk = (et != 0);
        }
        u32 lp = 0, mx = 0;
        if (safe_read(fm + PW_FM_MERIT, &lp) && safe_read(fm + PW_FM_MERIT + 4, &mx)) {
            out.lpCur = lp & 0xFFFF;               // Limit Points (u16 @+0)
            out.merits = (int)((lp >> 16) & 0x7F); // merit count (byte @+2, low 7 bits)
            out.maxMerits = (int)(mx & 0xFF);      // max merits (byte @+4)
            out.merOk = (out.maxMerits > 0);
        }
    }
    if (fm) { u32 v = 0; if (safe_read(fm + PW_FM_MLVL, &v)) { out.masterLevel = (int)(v & 0xFF); out.mlOk = true; } }   // Master Level (FFXiMain static struct)
    return out.epOk || out.merOk || out.mlOk;
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

// The client map-info record for (zone, submap). Linear scan of the table at *(g+0x10) : 14-byte records,
// key (zone u16 @+0x00, submap u8 @+0x02). Returns false (valid=false) when the zone/submap has no record
// (e.g. a zone with no map). SEH-guarded per read. See docs/game-data/map-system.md.
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
bool     owns_item(unsigned id)  { return count_item(id) != 0; }

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
        u32 item = ir + bag * 0xCA8 + idx * 0x28;
        if (!valid_ptr(item)) continue;
        u32 id = 0, cnt = 0; safe_read(item + 0x00, &id); safe_read(item + 0x04, &cnt);
        out.id[s]    = (unsigned short)(id  & 0xFFFF);
        out.count[s] = (unsigned short)(cnt & 0xFFFF);
        if (out.id[s] != 0 && out.id[s] != 0xFFFF) ++found;
    }
    return ready || found > 0;
}

// ids + 24-byte extdata per equipped slot. The item entry (items_root + bag*0xCA8 + idx*0x28) holds the
// extdata (augment blob) at +0x0D (24 bytes) -- per the reversed item struct (id@0, slot@2, count@4, bazaar@8,
// status@0xC, extdata char[0x18]@0x0D ; see docs/game-data/player-equipment.md).
bool read_equipment_ext(unsigned short ids[16], unsigned char ext[16][24]) {
    for (int s = 0; s < 16; ++s) { ids[s] = 0; for (int k = 0; k < 24; ++k) ext[s][k] = 0; }
    u32 ir = items_root(), ia = equip_index_arr(), ba = equip_bag_arr();
    if (!ir || !ia || !ba) return false;
    for (int s = 0; s < 16; ++s) {
        u32 idx = 0; safe_read(ia + s, &idx); idx &= 0xFF;
        if (idx == 0) continue;
        u32 bag = 0; safe_read(ba + s * 4, &bag);
        u32 item = ir + bag * 0xCA8 + idx * 0x28;
        if (!valid_ptr(item)) continue;
        u32 id = 0; safe_read(item + 0x00, &id);
        ids[s] = (unsigned short)(id & 0xFFFF);
        for (int k = 0; k < 24; ++k) { u32 b = 0; if (!safe_read(item + 0x0D + k, &b)) break; ext[s][k] = (unsigned char)(b & 0xFF); }
    }
    return true;
}

// Player buffs, reversed from LuaCore get_player (FUN_10072040). The same player struct
// pl = *(g + 0x3C) holds a 32-entry uint16 status-icon array at pl+0x1C (loop runs the ushorts
// in [pl+0x1C, pl+0x5C) ); 0xFF marks an EMPTY slot. We compact the non-empty ids in slot order.
int read_player_buffs(unsigned short* out, int maxN) {
    if (!out || maxN <= 0) return 0;
    u32 pl = player_struct();
    if (!pl) return 0;
    u32 mhp = 0; safe_read(pl + 0x60, &mhp);
    if (mhp == 0 || mhp > 0x100000) return 0;           // struct not ready yet
    int n = 0;
    for (int i = 0; i < 32 && n < maxN; ++i) {
        u32 v = 0; if (!safe_read(pl + 0x1C + i * 2, &v)) break;
        v &= 0xFFFF;
        if (v == 0xFF) continue;                        // empty slot (game's sentinel)
        out[n++] = (unsigned short)v;
    }
    return n;
}

// allianceinfo_t : g=*(LuaCore+0x1C8400) ; pp=*(g+0x248) (=&member[0]+4) ; allianceinfo = *(pp).
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
//   *(FFXiMain.dll + 0x57876C)            -> target_t base (heap)
//   target_t + 0x04   u32  Targets[0].ServerId   = the ACTIVE reticle (sub when <st> open, else main)
//   target_t + 0x2C   u32  Targets[1].ServerId   = the LOCKED main (while a <st> cursor is open)
//   target_t + 0x50   u32  flags ; bit 0x00010000 = sub-target CURSOR open (clears on BOTH
//                          confirm and cancel ; NB: the byte at +0x78 is sticky -> do NOT use it)
// id 0x04000000 is the "nothing" sentinel. (Old flat cache lived at FFXiMain+0x487F60.)
static const u32 TARGET_T_PTR_RVA = 0x57876C;
static const u32 T0_ID_OFF = 0x04, T1_ID_OFF = 0x2C, FLAGS_OFF = 0x50, LOCK_OFF = 0x5C;
// BT_ID_OFF : the BATTLE-TARGET ServerId (the mob you're engaged with). Unlike the reticle T0, this stays set
// when you drop the cursor <t> off the mob while still fighting it, and CLEARS to 0 on disengage. Reversed
// 2026-07-10 via //aio bt (self-calibrating id/index scan across the 3 states) : off the Targets[] grid at
// 0x04 + 3*0x28. Mirrors Windower's <bt>, used so the Skillchains box shows a party member's chain when your
// reticle is off the mob (the "SC from others doesn't always trigger" fix).
static const u32 BT_ID_OFF = 0x7C;
static const u32 SUB_CURSOR_BIT = 0x00010000;
static const u32 NO_TARGET = 0x04000000;
// LOCK_OFF : the LOCK-ON flag is BIT 0 of the byte at +0x5C (the upper bits carry other target flags -- on a
// PC/party target the byte reads 0xF0 unlocked / 0xF1 locked, so mask bit 0 ; a bare !=0 false-locks on allies).
// Reversed 2026-07-02 via //aio tlock (mob: +0x5C flipped 0->1) ; corrected 2026-07-05 (friendly targets carry
// the upper flags). The locked id is T0 @+0x04.

bool read_target(TargetInfo& o) {
    o.id = o.sid = o.bt = 0; o.locked = false;
    u32 ffm = ffximain_base();
    if (!ffm) return false;
    u32 tp = 0; safe_read(ffm + TARGET_T_PTR_RVA, &tp);
    if (!valid_ptr(tp)) return true;                    // target system not ready
    u32 t0 = 0, t1 = 0, bt = 0, flags = 0, lk = 0;
    safe_read(tp + T0_ID_OFF, &t0);                     // active reticle
    safe_read(tp + T1_ID_OFF, &t1);                     // locked main (valid during sub-target)
    safe_read(tp + BT_ID_OFF, &bt);                     // battle target (engaged mob, held when the reticle is off)
    safe_read(tp + FLAGS_OFF, &flags);
    safe_read(tp + LOCK_OFF,  &lk);                     // lock-on flag (1 = locked)
    if (flags & SUB_CURSOR_BIT) { o.sid = t0; o.id = t1; }   // <st> cursor open : sub = reticle, main = locked
    else                        { o.id = t0; }               // normal : main = reticle, no sub
    if (bt != NO_TARGET) o.bt = bt;                     // only a real id ; the 0x04000000 "nothing" sentinel -> 0
    if (o.id  == NO_TARGET) o.id  = 0;
    if (o.sid == NO_TARGET) o.sid = 0;
    o.locked = (lk & 0x01) != 0 && (o.id != 0);         // locked ON the main target : bit 0 of +0x5C (the upper bits carry OTHER flags on friendly/PC targets -- a bare !=0 false-locked on party members)
    return true;
}

// The ACTIVE target's ENTITY (name / HP% / id / index) for the Target HUD module. Reversed
// 2026-07-03 via //aio tent (see docs/game-data/target-substruct.md). The entity struct is reached
// DIRECTLY off target_t (no id->index scan) :
//   target_t + 0x08   u32  Targets[0].EntityPointer  -> the reticle's entity struct  (match=1 in every probe)
//   entity   + 0x74   u16  Index
//   entity   + 0x78   u32  ServerId
//   entity   + 0x7C   char[0x18]  Name (ASCII, NUL-padded)
//   entity   + 0xEC   u8   HP% (0..100)  -- the ONLY 0..100 byte that tracked damage : a 9% mob read 9 here
//                          while +0xDC/+0xE0 stayed pinned at 100 (so those are NOT HP%).
static const u32 T0_EPTR_OFF = 0x08;      // Targets[0].EntityPointer (reticle = main unless a <st> cursor is up) -- a TARGET-system
                                          // field, not an entity field ; the ENT_*_OFF struct offsets live near read_map_entities.

static const u32 T1_EPTR_OFF = 0x30;      // Targets[1].EntityPointer (the LOCKED main, valid while a <st> cursor is up).
                                          // Targets stride = T1_ID(0x2C) - T0_ID(0x04) = 0x28 ; so T1_EPTR = T0_EPTR(0x08) + 0x28.

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
      o.status = st; o.claimId = cl; o.spawnType = sp; o.pflags = pf; }
    { u32 ms = 0, xx = 0, zz = 0, hh = 0;  // floats read as raw dwords (safe_read is u32-typed), then bit-copied
      safe_read(ep + ENT_SPEED_OFF, &ms); safe_read(ep + ENT_X_OFF, &xx); safe_read(ep + ENT_Z_OFF, &zz); safe_read(ep + ENT_HEADING_OFF, &hh);
      memcpy(&o.moveSpeed, &ms, 4); memcpy(&o.posX, &xx, 4); memcpy(&o.posZ, &zz, 4); memcpy(&o.heading, &hh, 4); }   // movement speed + position + facing (radians)
    o.valid = true;
}

// Read the MAIN target entity AND, when a <st> sub-target cursor is up, the SUB-target entity. Normal targeting :
// main = Targets[0] (reticle), no sub. <st> open : main = Targets[1] (the locked target), sub = Targets[0] (reticle).
bool read_target_entity(TargetEntity& main, TargetEntity& sub, bool& hasSub) {
    main = TargetEntity{}; sub = TargetEntity{}; hasSub = false;
    u32 ffm = ffximain_base();
    if (!ffm) return false;
    u32 tp = 0; safe_read(ffm + TARGET_T_PTR_RVA, &tp);
    if (!valid_ptr(tp)) return true;                    // target system not ready
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
//   +0x5EED6C  u32  live-menu pointer : 0 closed, heap ptr while any menu is open ; *(ptr+0x04) = menu DEF
//   +0x634F28  u32  "examined SPELL" id   (the game writes it in the Magic menu to show MP Cost / recast)
//   +0x634590  u32  "examined ABILITY" id + 0x200 (Job-Ability / Weapon-Skill menus ; -0x200 = raw id)
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
static const u32 MENU_PTR_RVA = 0x5EED6C, EXAM_SPELL_RVA = 0x634F28, EXAM_ABIL_RVA = 0x634590;
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
    u32 mptr = 0; safe_read(ffm + MENU_PTR_RVA, &mptr);
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
        u32 spell = 0; safe_read(ffm + EXAM_SPELL_RVA, &spell);
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
        u32 araw = 0; safe_read(ffm + EXAM_ABIL_RVA, &araw);                         // NB "abiselec" (nm[3]='s') is excluded
        if (araw >= 0x200 && araw <= 0x200 + 0x4000) { type = 2; id = araw - 0x200; return true; }  // Job Ability
        if (araw >= 1 && araw < 0x200)               { type = 3; id = araw;         return true; }  // Weapon Skill
        return false;                                                               // 0 / 0xFFFFFFFF : not populated
    }
    return false;                                         // any other menu : no box
}

// Ability/Job-Ability RECAST, reversed from LuaCore's get_ability_recasts (FUN_1006FF00). The client
// keeps two parallel 32-slot arrays hung off the data root g = *(LuaCore+0x1C8400) :
//   *(g + 0x22C) -> int32[32]  remaining recast in 1/60 s (frames)   ; seconds = timer / 60
//   *(g + 0x230) -> stride-8 entries, byte[0] = the slot's recast_id
// Windower builds recasts[id] = timer/60. We do the inverse : given the highlighted JA's recast_id,
// scan the 32 slots for an ACTIVE one (timer>0) whose id matches -> its remaining seconds (0 = ready).
// recast_id comes from abilities_gen.h (caller side). This is the menu's exact "Next".
unsigned ability_recast_sec(unsigned recast_id) {
    u32 g = data_root(); if (!g) return 0;
    u32 idsP = 0, timersP = 0;
    safe_read(g + 0x230, &idsP); safe_read(g + 0x22C, &timersP);
    if (!valid_ptr(idsP) || !valid_ptr(timersP)) return 0;
    for (int s = 0; s < 32; ++s) {
        u32 t = 0; safe_read(timersP + s * 4, &t);
        if ((int)t <= 0 || t > 60u * 7200u) continue;          // empty/ready slot, or garbage (>2h)
        u32 idb = 0; safe_read(idsP + s * 8, &idb);
        if ((idb & 0xFF) == recast_id) return (t + 59) / 60;   // ceil to whole seconds (the "Next")
    }
    return 0;                                                  // not on recast
}

// Spell RECAST ("Next" for the Magic menu), reversed from LuaCore's get_spell_recasts (FUN_1006FE80 --
// the cclosure pushed just before the "get_spell_recasts" setfield ; the memory's old FUN_100732B0 guess
// was wrong, that one is get_abilities). Far simpler than abilities : NO 32-slot scan -- a flat array.
//   base = *(g + 0x234) -> ushort[1024], indexed directly by recast_id, remaining recast in 1/60 s.
//   Windower builds recasts[id] = base[id] ; seconds = base[id] / 60. (0x234 sits right after the
//   ability timers/ids at 0x22C/0x230.) recast_id comes from spells_gen.h (SpellRow::recast_id).
// LIST all active recasts -> parallel arrays. Job abilities : 32-slot table (timers @0x22C /4, ids @0x230 /8,
// byte[0]=recast_id). Spells : ushort[1024] @0x234, indexed by recast_id (block-copied under SEH, then scanned).
int read_recasts(unsigned short* rid, unsigned char* kind, int* sec, int maxN) {
    int n = 0;
    u32 g = data_root(); if (!g) return 0;
    u32 idsP = 0, timersP = 0, spellB = 0;
    safe_read(g + 0x230, &idsP); safe_read(g + 0x22C, &timersP); safe_read(g + 0x234, &spellB);
    if (valid_ptr(idsP) && valid_ptr(timersP)) {
        for (int s = 0; s < 32 && n < maxN; ++s) {
            u32 t = 0; safe_read(timersP + s * 4, &t);
            if ((int)t <= 0 || t > 60u * 7200u) continue;          // ready / empty / garbage
            u32 idb = 0; safe_read(idsP + s * 8, &idb);
            rid[n] = (unsigned short)(idb & 0xFF); kind[n] = 0; sec[n] = ((int)t + 59) / 60; ++n;
        }
    }
    if (valid_ptr(spellB)) {
        static unsigned short sr[1024]; bool ok = true;
        __try { memcpy(sr, (const void*)spellB, sizeof(sr)); }
        __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
        if (ok) for (int i = 0; i < 1024 && n < maxN; ++i) {
            const unsigned v = sr[i];
            if (v == 0 || v > 60u * 7200u) continue;
            rid[n] = (unsigned short)i; kind[n] = 1; sec[n] = ((int)v + 59) / 60; ++n;
        }
    }
    return n;
}
unsigned spell_recast_sec(unsigned recast_id) {
    if (recast_id >= 0x400) return 0;                          // array is exactly 1024 entries
    u32 g = data_root(); if (!g) return 0;
    u32 base = 0; if (!safe_read(g + 0x234, &base) || !valid_ptr(base)) return 0;
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
// raw u8 at *(g+0x48)+off (the JP/merit block). Used for BRD merit levels +0x142 (Clarion/Tenuto song-duration
// seconds) / +0x148 (Marcato seconds). Timers reads these off the same stat block.
int read_jp_u8(unsigned off) {
    u32 g = data_root(); if (!g) return 0;
    u32 base = 0; if (!safe_read(g + 0x48, &base) || !valid_ptr(base)) return 0;
    u32 v = 0; if (!safe_read(base + off, &v)) return 0;
    return (int)(v & 0xFF);
}
static void sch_recast_info(int level, int jp, int& interval, int& charges) {   // stratagem interval (s) + max charges
    if (jp >= 550)       { interval = 33;  charges = 5; }
    else if (level >= 90){ interval = 48;  charges = 5; }
    else if (level >= 70){ interval = 60;  charges = 4; }
    else if (level >= 50){ interval = 80;  charges = 3; }
    else if (level >= 30){ interval = 120; charges = 2; }
    else                 { interval = 240; charges = 1; }
}
static void compute_grimoire(GameState& gs) {
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
    unsigned spent = 0; read_job_spent(SCH, spent);
    int interval = 240, charges = 1; sch_recast_info(level, (int)spent, interval, charges);
    if (!isMain) { if (charges > 3) charges = 3; if (interval < 80) interval = 80; }   // sub SCH : capped
    const int recast = (int)ability_recast_sec(231);           // Stratagems recast
    int count = charges;
    if      (recast == 0)             count = charges;
    else if (recast < interval)       count = charges - 1;
    else if (recast < 2 * interval)   count = charges - 2;
    else if (recast < 3 * interval)   count = charges - 3;
    else if (recast < 4 * interval)   count = charges - 4;
    else                              count = 0;
    if (count < 0) count = 0;
    g.charges  = count;
    g.timerSec = (recast > 0) ? (recast % interval) : -1;
    gs.grimoire = g;
}

void poll_game_state(GameState& gs) {
    PlayerCacheScope _plc;                                   // cache read_player for this whole poll cycle (hit ~5x below)
    PlayerInfo me;
    if (!read_player(me)) { gs.inGame = false; return; }     // not ready (zoning) -> keep last-good
    gs.inGame = true;
    gs.me = me;
    gs.hp = me.hpp / 100.0f;
    gs.mp = me.mpp / 100.0f;
    gs.tp = me.tp / 3000.0f; if (gs.tp > 1.0f) gs.tp = 1.0f; if (gs.tp < 0.0f) gs.tp = 0.0f;
    gs.nbuff = read_player_buffs(gs.buffs, 32);   // self status icons -> the Player Hub buff tray (snapshot, not poll-in-draw)
    { unsigned short rid[40]; unsigned char kd[40]; int sc[40];   // Timers module : active JA + spell recasts (snapshot)
      const int nr = read_recasts(rid, kd, sc, 40); gs.nRecast = (nr > 40) ? 40 : nr;
      for (int i = 0; i < gs.nRecast; ++i) { gs.recasts[i].recastId = rid[i]; gs.recasts[i].kind = kd[i]; gs.recasts[i].sec = sc[i]; } }
    { float ms = 0.0f; gs.meSpeed = read_self_speed(me.id, ms) ? ms : 0.0f; }   // own movement speed -> Player Hub speed band
    { unsigned gv = 0; gs.meGil = read_player_gil(gv) ? gv : 0; }               // own gil -> Player Hub gil band
    gs.equipValid = read_equipment(gs.equip);                                    // 16 equipped items -> Equipment Viewer grid
    if (!gs.equipValid) gs.equip = EquipSet{};                                   // not ready (zone / not-logged-in) : zero it AND flag it so the viewer keeps its cached icons

    // minimap : zone + self world position/heading + the zone's map calibration record (snapshot once/frame)
    gs.zone = zone_id();
    { u32 ent = self_entity();
      if (ent) { u32 xx = 0, zz = 0, hh = 0; safe_read(ent + ENT_X_OFF, &xx); safe_read(ent + ENT_Z_OFF, &zz); safe_read(ent + ENT_HEADING_OFF, &hh);
                 gs.meX = *(float*)&xx; gs.meZ = *(float*)&zz; gs.meHeading = *(float*)&hh; } }
    read_map_record(gs.zone, current_submap(), gs.map);   // real floor (multi-level zones) -> right map page
    gs.mapEntN = read_map_entities(gs.mapEnts, MAP_ENT_MAX);    // PC/NPC/mob markers
    gs.vana = vana_clock_now();                                // Vana'diel clock (computed, no memory read)

    TargetInfo tg;
    if (read_target(tg)) { gs.targetId = tg.id; gs.subTargetId = tg.sid; gs.targetLocked = tg.locked; gs.battleTargetId = tg.bt; }
    else                 { gs.targetId = gs.subTargetId = 0; gs.targetLocked = false; gs.battleTargetId = 0; }

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
      if (ffm2) safe_read(ffm2 + EXAM_ABIL_RVA, &ea);
      gs.examAbilRaw = ea; }

    // party-window picker : the focused menu is "partywin" with a 1-based cursor index at +0x4C.
    // (Reversed via //aio pcur: +0x4C tracks the hovered member, +0x08 = its row object.)
    gs.partyMenuSel = 0;
    u32 ffm = ffximain_base();
    if (ffm) {
        u32 mptr = 0; safe_read(ffm + MENU_PTR_RVA, &mptr);
        u32 def = 0; if (valid_ptr(mptr)) safe_read(mptr + 0x04, &def);
        if (valid_ptr(def)) {
            char nm[6] = {0}; for (int i = 0; i < 5; ++i) { u32 c = 0; safe_read(def + MENU_NAME_OFF + i, &c); nm[i] = (char)(c & 0xFF); }
            if (nm[0]=='p' && nm[1]=='a' && nm[2]=='r' && nm[3]=='t' && nm[4]=='y') {     // "partywin"
                u32 idx = 0; safe_read(mptr + 0x4C, &idx);
                if (idx >= 1 && idx <= 6) gs.partyMenuSel = (int)idx;
            }
        }
    }

    compute_grimoire(gs);   // SCH grimoire (book + charges + timer) from buffs / jobs / stratagem recast
}

} // namespace aio
