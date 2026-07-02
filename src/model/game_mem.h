// game_mem.h -- read live game data straight from FFXI memory (the Windower way).
//
// Reversed from LuaCore.dll get_player(): the data root is a global in LuaCore, and
// the player / party structures hang off it. This is what makes the HUD show real
// data INSTANTLY on load (no waiting for packets). cf. memory aiohud-ffxi-packets.
#pragma once
#include "gfx/d3d.h"   // u32

namespace aio {

// --- game-memory anchors : the ONE source of truth for the pointer chains every reader hangs off.
// Module bases are cached (fixed after load) ; the data root and its children are read each call (they
// can be 0 while zoning). All return 0 -> the caller no-ops. Keeps the offsets in one place. ---
u32 ffximain_base();   // GetModuleHandleA("FFXiMain.dll"), cached
u32 luacore_base();    // GetModuleHandleA("LuaCore.dll"),  cached
u32 data_root();       // g  = *(LuaCore + 0x1C8400) -- anchor for player / party / recast chains
u32 party_ptr();       // pp = *(g + 0x248) (= &member[0] + 4) ; base = party_ptr() - 4
u32 entity_array();    // *(g + 0x24) -- the entity position-object array (index -> ent[idx])

// read the player's vitals as fractions (HP/MP in 0..1, TP in 0..1 of 3000).
// returns false if the player structure isn't available yet (loading / not in game).
bool read_player_vitals(float& hpFrac, float& mpFrac, float& tpFrac);

// the local player, read straight from memory (always present + game-accurate) ->
// shown as the self row in the party (the game never sends you your own party packet).
struct PlayerInfo {
    unsigned id;
    char name[20];
    int hp, mp, tp, hpp, mpp, mjob, sjob;
};
bool read_player(PlayerInfo& out);

// the player's own buffs (status icons). Reversed from LuaCore get_player (FUN_10072040):
// 32 x uint16 at player+0x1C, 0xFF = empty slot. Writes the non-empty ids (compacted, slot
// order) to `out` and returns the count. `out` must hold at least 32 entries.
int read_player_buffs(unsigned short* out, int maxN);

// leadership = server-id match against the alliance-info struct (Ashita allianceinfo_t),
// NOT per-member flag bits. alliance = overall alliance leader ; p1/p2/p3 = the leaders of
// party 1 (yours) / alliance parties 2 / 3. id 0 = role absent.
struct PartyLeaders { unsigned alliance, p1, p2, p3; };
bool read_party_leaders(PartyLeaders& out);

// the player's current selection: server-id of the main target <t> and the subtarget <st>
// (0 = none). Used to draw the party selection cursor (cf. XivParty get_mob_by_target).
// returns false if the target structure isn't located/ready.
struct TargetInfo { unsigned id, sid; bool locked; };   // locked = the main target is LOCK-ON'd (target_t+0x5C)
bool read_target(TargetInfo& out);

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

} // namespace aio
