// game_mem.h -- read live game data straight from FFXI memory (the Windower way).
//
// Reversed from LuaCore.dll get_player(): the data root is a global in LuaCore, and
// the player / party structures hang off it. This is what makes the HUD show real
// data INSTANTLY on load (no waiting for packets). cf. memory aiohud-ffxi-packets.
#pragma once
#include "windower.h"   // model must not depend on gfx/ -- take the ABI primitives straight from windower.h
using windower::u32;         // (these three were previously pulled in transitively via gfx/d3d.h's own using-decls)
using windower::valid_ptr;
using windower::vmethod;

namespace aio {

// --- game-memory anchors : the ONE source of truth for the pointer chains every reader hangs off.
// Module bases are cached (fixed after load) ; the data root and its children are read each call (they
// can be 0 while zoning). All return 0 -> the caller no-ops. Keeps the offsets in one place. ---
u32 ffximain_base();   // GetModuleHandleA("FFXiMain.dll"), cached
u32 luacore_base();    // GetModuleHandleA("LuaCore.dll"),  cached
u32 data_root();       // g  = *(LuaCore + 0x1C8400) -- anchor for player / party / recast chains
u32 party_ptr();       // pp = *(g + 0x248) (= &member[0] + 4) ; base = party_ptr() - 4
u32 entity_array();    // *(g + 0x24) -- the entity position-object array (index -> ent[idx])
u32 player_struct();   // *(g + 0x3C) -- the local player struct (main job @+0x94, main lvl @+0x98, sub job @+0x9C, sub lvl @+0xA0)
u32 self_party_base(unsigned selfId);   // id-validated self party-block base (pp-4, tolerating a 0/-4 shift) ; 0 if unavailable
u32 self_entity();     // the local player's ENTITY struct (entity_array[selfIndex]) : X@+0x04 Y@+0x08 Z@+0x0C heading@+0x18 ; 0 if unavailable
unsigned zone_id();    // current zone id (*(g+0x40)+0x02) -- matches the zones resource + map system ; 0 if unavailable

// the client map-info record for (zone, submap) : the world->map-pixel calibration. Walked from the table
// at *(g+0x10) (14-byte records, key zone u16 @+0x00 / submap u8 @+0x02 ; scale u8 @+0x05, fileIdx u16 @+0x08,
// offX s16 @+0x0A, offY s16 @+0x0C). Transform: mapX=(scale*worldX)/5-offX, mapY=(scale*-worldZ)/5-offY, on a
// 512px native map. Reversed + live-confirmed 2026-07-06 (see docs/game-data/map-system.md).
struct MapRecord { unsigned zone = 0, fileIdx = 0, flags = 0, fileId = 0; int scale = 0, offX = 0, offY = 0; bool valid = false; };
bool read_map_record(unsigned zone, int submap, MapRecord& out);   // fileId = (flags&1 ? 0xD02F : 0x14C0) + fileIdx
bool entity_name_by_id(unsigned id, char* out, int sz);      // server id -> entity name ('???' = unnamed) ; false if not found
bool entity_name_by_index(unsigned index, char* out, int sz);// entity INDEX (what packets carry) -> name ; false if the slot is empty
int  current_submap();   // current FLOOR index (multi-level zones) via the client position->floor routine ; 0 if mapless/ground
int  read_usable_weapon_skills(unsigned short* out, int maxN);   // the player's usable WS ids (get_abilities bitmask) ; count
int  read_usable_job_abilities(unsigned short* out, int maxN);   // the player's usable JA ids (incl. pet ready moves when a pet is out) ; count
bool read_usable_ja_bits(unsigned char set[128]);                // the raw usable-JA bitmap (1024 bits ; bit id set => usable) ; false on fault -- for disambiguating recast_id collisions by job
int  read_blu_spells(unsigned short* out, int maxN);             // BLU : the currently-SET blue magic spell ids (get_mjob_data) ; count

// nearby entities for the minimap : world X/Z + type (1=PC, 2=NPC, 3=mob) from the entity array (*(g+0x24)).
// type from spawnType (ent+0x1D0 : 0x01 PC / 0x02 NPC / 0x10 mob) ; skips the render-culled (ent+0x120 & 0x4000)
// and the self entity. Returns the count written (<= maxN). One guarded block-copy of the pointer array.
static const int MAP_ENT_MAX = 400;
// type 1=PC 2=NPC 3=mob ; id (+0x78) / claimId (+0x188) / pflags (+0x124) / status (+0x170 engaged) carried
// so the minimap can colour markers with the Target box's convention (claim / party / engaged).
struct MapEntity { float x, z, heading; unsigned id, claimId, pflags; unsigned char type, status; };
int read_map_entities(MapEntity* out, int maxN);

// Resolve up to `n` entity server-ids to their live vitals in ONE entity-array block-copy (rule 6/7 : the
// hate-list poller needs name/HP%/pos/claim per tracked mob without an O(n) scan-per-id). out[i] pairs with
// ids[i] ; out[i].valid=false if that id isn't currently in the array. Returns the number resolved (valid).
// Reads the SAME entity struct as read_map_entities (ENT_*_OFF). name @+0x7C, HP% @+0xEC (u8 0..100).
struct EntityVitals { unsigned id; char name[24]; int hpp; float x, z; unsigned status, claimId, spawnType; bool valid; };
int read_entities_by_id(const unsigned* ids, int n, EntityVitals* out);

// Scan the entity array for MOBS currently CLAIMED by one of the friendly ids (self + party + alliance). A pet's
// damage claims for its OWNER, so avatar / jug / automaton / trust-pet-tanked mobs surface here automatically --
// this is what makes the hate list stable (self-refreshing every frame) instead of popping only on your own hits.
// Fills out[] (live vitals) + claimOut[] (the claiming friendly id) for up to maxN mobs. One block-copy. Returns count.
int read_party_aggro_mobs(const unsigned* friendlyIds, int nFriendly, EntityVitals* out, unsigned* claimOut, int maxN);

// The server id at entity_array[index] (+0x78), or 0 if the slot is empty/invalid. Used to resolve the pet /
// owner INDEX carried by the pet packets (0x067/0x068) into a server id (to key the friendly-pet set).
unsigned entity_id_by_index(unsigned index);

// PointWatch : the MAIN JOB's Capacity Points + Job Points, read straight from the client's persistent job-point
// struct (*(g+0x48) + 0x306 + (job-1)*6 : CP u16 @+0, JP u16 @+2 ; reversed via LuaCore FUN_10091110, WAR=first).
// Available the moment you're logged in (no 0x063 packet needed) -> the CP bar fills on load. false if unavailable.
bool read_capacity_points(unsigned mainJob, unsigned& cp, unsigned& jp);

// PointWatch : Exemplar Points (ML bar), Limit Points + merits, and Master Level, read from the client's STATIC
// player/merit structs. Exemplar + merits live in FFXiMain.dll static data (LuaCore does NOT mirror them) : RVAs
// reversed live via //aio pwscan (2026-07-07 client build) -- VERSION-SPECIFIC, re-probe after a client patch.
// Master Level is in the LuaCore-mirrored player struct (*(g+0x3C)+0x93). Fills the ML + Merits rows on LOAD.
struct PwMem { unsigned xpCur = 0, xpTnl = 0, epCur = 0, epTnml = 0, lpCur = 0; int merits = 0, maxMerits = 0, masterLevel = 0;
               bool xpOk = false, epOk = false, merOk = false, mlOk = false; };
bool read_pointwatch(PwMem& out);
u32 key_items_base();  // *(g + 0x4C) -- u8[0x2000] : ONE BYTE per key-item id (non-zero = owned), NOT a bitfield.
                       // Sits immediately before items_root (base + 0x2000 == items_root). See game-data/key-items.md.
bool owns_key_item(unsigned id);   // ki_base[id] != 0. false for id >= 0x2000 or an unmapped base. Prefer this
                                   // over reading key_items_base() yourself -- the offset lives in game_mem only.
u32 items_root();      // *(g + 0x50) -- the item-container root (get_items base : gil @+0x04, bags at root + bag*0xCA8)
u32 equip_index_arr(); // *(g + 0x54) -- u8[16]  : inventory index per equip slot (0 = empty)
u32 equip_bag_arr();   // *(g + 0x58) -- s32[16] : bag/container id per equip slot

// read the player's vitals as fractions (HP/MP in 0..1, TP in 0..1 of 3000).
// returns false if the player structure isn't available yet (loading / not in game).
bool read_player_vitals(float& hpFrac, float& mpFrac, float& tpFrac);

// the local player, read straight from memory (always present + game-accurate) ->
// shown as the self row in the party (the game never sends you your own party packet).
struct PlayerInfo {
    unsigned id;
    char name[20];
    int hp, mp, tp, hpp, mpp, mjob, sjob, mlvl, slvl;
};
bool read_player(PlayerInfo& out);

// the local player's movement-speed field (self entity +0x98), for the Player Hub speed readout. The
// player struct (*(g+0x3C)) carries jobs/vitals but NOT the entity fields ; movement_speed lives on the
// ENTITY struct. Reach the self entity the SAME way party_state does : base+0x20 = the player's entity
// index -> entity_array[idx] -> +0x98. `selfId` unused, kept for API stability (self_entity() self-validates). Returns false
// (leaves ms untouched) when unavailable ; the field is STATIC base-5 for a PC (idle = 5.0 = 0%).
bool read_self_speed(unsigned selfId, float& ms);

// the local player's GIL. windower.ffxi.get_items('gil') resolves to *( *(g + 0x50) + 0x04 ) (u32) : the
// item-container root then the bag0/slot0 count (FFXI's gil pseudo-item, id 0xFFFF @+0x00). Reversed
// 2026-07-05 from LuaCore get_items (FUN_10074690, "gil\0" dispatch). Returns false (leaves gil untouched)
// when the container isn't mapped yet (zoning) ; 0 gil is a legitimate value.
bool read_player_gil(unsigned& gil);

// the 16 currently-EQUIPPED items (Equipment Viewer grid), indexed by packet equip-slot id S :
// 0 main, 1 sub, 2 range, 3 ammo, 4 head, 5 body, 6 hands, 7 legs, 8 feet, 9 neck, 10 waist,
// 11 left_ear, 12 right_ear, 13 left_ring, 14 right_ring, 15 back. Resolved via the equip index/bag
// arrays (g+0x54 / g+0x58) into the item container (items_root + bag*0xCA8 + index*0x28) : id u16 @+0x00,
// count u32 @+0x04. id 0 = EMPTY slot (index was 0). Reversed 2026-07-05 from LuaCore get_items('equipment')
// (FUN_10074690 -> FUN_10094410). See docs/game-data/player-equipment.md.
struct EquipSet { unsigned short id[16]; unsigned short count[16]; };
bool read_equipment(EquipSet& out);

// --- INVENTORY : "how many of item id X do I own, across all bags?" (see game-data/inventory.md) ---
// The item container at items_root holds 18 BAGS of 81 entries each (bag stride 0xCA8 = 81 * 0x28) :
// entry = items_root + bag*0xCA8 + slot*0x28 (id u16 @+0x00, count u32 @+0x04). Entry 0 of EVERY bag is a
// reserved header (id 0xFFFF ; in bag 0 its count is the player's GIL) -- the usable slots are 1..80, which
// is exactly the range Windower's own get_items() enumerates. Reversed 2026-07-17 from LuaCore get_items
// (FUN_10074690 -> FUN_10093360 / FUN_100935c0).
static const int ITEM_BAG_MAX   = 18;   // bags 0..17 : inventory safe storage temporary locker satchel sack case
                                        //              wardrobe safe2 wardrobe2..8 recycle
static const int ITEM_BAG_SLOTS = 80;   // usable slots per bag (1..80 ; entry 0 is the reserved header)

const char* item_bag_name(int bag);     // "inventory" .. "recycle" ; "" if bag is out of range

// per-bag metadata, three parallel u8[18] arrays the client keeps after the bag entries :
// max @items_root+0x19500+bag (capacity, 80 for a real bag / 0 temporary / 10 recycle),
// count @+0x19520+bag (occupied slots), enabled @+0x19540+bag (bag reachable right now : safe/storage/locker
// read 0 away from a moogle -- their CONTENTS are still resident and counted). Answered from the snapshot.
struct ItemBagInfo { unsigned char max, count, enabled; };
bool item_bag_info(int bag, ItemBagInfo& out);

// Take the inventory SNAPSHOT : ONE SEH-guarded block copy of the whole container (rule 5/6 -- never a
// safe_read per slot : 18 bags x 80 slots would be ~1440 guarded reads). Call once per poll, then answer
// as many ids as you like from the snapshot. false = container not mapped yet (zoning) -> readers no-op.
bool refresh_items();

// One slot out of the snapshot : bag 0..17, slot 1..80. id=0 -> empty slot (out stays 0/0). Reads the
// SNAPSHOT, never live memory -- refresh_items() first. false = bad bag/slot or no snapshot.
bool item_slot(int bag, int slot, unsigned& id, unsigned& count);

// Total count of item id `id` across ALL 18 bags (a stack of 40 -> 40 ; the same item in several
// slots/bags sums). 0 = not owned. Answered from the last refresh_items() ; auto-takes a snapshot if none
// exists yet, so a one-shot caller (probe) works standalone -- but a POLLER must call refresh_items()
// itself each cycle, else it re-reads a stale snapshot.
unsigned count_item(unsigned id);
bool     owns_item(unsigned id);        // count_item(id) != 0

// Batch form : counts n ids in ONE pass over the snapshot (the EmpyPop tracker's ~5-30 ids at ~2 Hz).
// out[i] pairs with ids[i]. Returns the number of ids with a non-zero count.
int count_items(const unsigned* ids, int n, unsigned* out);

// the 16 equipped item ids + their 24-byte EXTDATA (augment blob @ item+0x0D). On-demand (not per-frame) :
// used at cast time to read the caster's live "Enhancing Magic eff. dur." gear/augments (see enh_dur.h).
// ids[s] = 0 for an empty slot. Returns false if the item container isn't ready.
bool read_equipment_ext(unsigned short ids[16], unsigned char ext[16][24]);

// Merit level (0..5) for merit id `mid` / Job-Point gift rank for gift id `gid`, from the LuaCore-mirrored
// arrays (indexed by id>>1). Used for the caster's "Enhancing Magic Duration" merit (2320) + JP gift (338)
// -> flat seconds on buffs cast on allies. 0 if not readable.
int read_merit_level(unsigned mid);
int read_jp_gift_rank(unsigned gid);
int read_jp_u8(unsigned off);   // raw u8 at *(g+0x48)+off (BRD song-duration merits @+0x142 / Marcato @+0x148)

// the player's own buffs (status icons). Reversed from LuaCore get_player (FUN_10072040):
// 32 x uint16 at player+0x1C, 0xFF = empty slot. Writes the non-empty ids (compacted, slot
// order) to `out` and returns the count. `out` must hold at least 32 entries.
int read_player_buffs(unsigned short* out, int maxN, bool* ok = 0);   // ok : the read SUCCEEDED (an empty list is then real, not "not ready") -- see the note in the .cpp

// leadership = server-id match against the alliance-info struct (Ashita allianceinfo_t),
// NOT per-member flag bits. alliance = overall alliance leader ; p1/p2/p3 = the leaders of
// party 1 (yours) / alliance parties 2 / 3. id 0 = role absent.
struct PartyLeaders { unsigned alliance, p1, p2, p3; };
bool read_party_leaders(PartyLeaders& out);

// the player's current selection: server-id of the main target <t> and the subtarget <st>
// (0 = none). Used to draw the party selection cursor (cf. XivParty get_mob_by_target).
// returns false if the target structure isn't located/ready.
struct TargetInfo { unsigned id, sid, bt; bool locked; };   // locked = the main target is LOCK-ON'd (target_t+0x5C) ; bt = battle target (target_t+0x7C : the engaged mob, held even when the reticle <t> is off ; 0 when disengaged)
bool read_target(TargetInfo& out);

// the ACTIVE target's entity : name + HP% + id/index, for the Target HUD module. The reticle's
// entity struct is reached DIRECTLY via target_t+0x08 (Targets[0].EntityPointer -- no id->index
// scan ; the //aio tent probe confirmed EntityPointer == entity_array[Index]). Fields reversed
// 2026-07-03 via //aio tent (name @+0x7C, id @+0x78, index @+0x74, HP% @+0xEC). valid=false when
// nothing is targeted. returns false only if the target structure isn't located/ready.
struct TargetEntity { unsigned id, index; char name[24]; int hpp; bool valid; unsigned status = 0, claimId = 0, spawnType = 0, pflags = 0;
    float moveSpeed = 0.0f, posX = 0.0f, posZ = 0.0f, heading = 0.0f; };   // moveSpeed (entity+0x98) ; posX/posZ (entity+0x04/+0x0C) for move-detection ; heading (entity+0x18, float radians) for the Front/Flank/Behind badge   // status : engaged flag (entity+0x170) ; claimId : who claimed it (entity+0x188, 0 = unclaimed) ; spawnType (entity+0x1D0) : 0x01 PC / 0x02 NPC / 0x10 Mob ; pflags (entity+0x124) : bit 0x00800000 = PC is IN A PARTY (game shows the name blue)
bool read_target_entity(TargetEntity& main, TargetEntity& sub, bool& hasSub);   // main target + (during a <st> cursor) the sub-target entity

// the in-game action menu : returns true while the highlighted entry is a spell / job ability /
// weapon skill, with type (1=spell, 2=job ability, 3=weapon skill) and the action id. Identifies the
// menu by its static inline name at def+0x4E (zero-tap, no cursor move). Drives the info box.
bool read_action_menu(int& type, unsigned& id, unsigned& cursor, bool& examValid);

// remaining recast (seconds, 0 = ready) for a job-ability recast_id, read from the client's 32-slot
// recast table (g+0x22C timers / g+0x230 ids) -- the menu's exact "Next". recast_id from abilities_gen.h.
unsigned ability_recast_sec(unsigned recast_id);

// remaining recast (seconds, 0 = ready) for a SPELL recast_id, read from the client's ushort[1024]
// recast array (*(g+0x234), adjacent to the ability arrays) -- the menu's exact "Next" for Magic.
// recast_id from spells_gen.h (SpellRow::recast_id).
unsigned spell_recast_sec(unsigned recast_id);

// LIST all active recasts (Timers module) into parallel arrays : recastId[], kind[] (0 = job ability, 1 = spell),
// sec[] (remaining seconds). Returns the count. SEH-guarded (block-reads the 1024-entry spell array). Kept separate
// from poll_game_state (which hosts C++ objects and so can't use __try). Names resolved caller-side via the gen tables.
int read_recasts(unsigned short* recastId, unsigned char* kind, int* sec, int maxN);

} // namespace aio
