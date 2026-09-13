// fake_game.h -- a small, honest FFXI for the model to run in, offline.
//
// WHY. The Timers rows are decided by the real model (PartyState, fed by packets and a per-frame roster read) and
// the real builder (model/timers_build.cpp). A test that re-implemented either would prove only that it agrees with
// itself. So the suite links the REAL code, and replaces only what sits outside the process : the game's memory, its
// clock and the packets it sends.
//
// HOW IT STAYS HONEST.
//   * Memory is REAL memory. The party array, the alliance header, the entity table and the FFXiMain clock are laid
//     out byte for byte in buffers of this process, at the offsets the model reads (party_state_roster.cpp,
//     party_state.cpp). The model reads them through its own safe_read / model_copy -- nothing is intercepted, so an
//     offset the model changes is an offset the fake no longer serves, and the tests fail instead of passing blind.
//   * Packets are built BIT for bit in the layout the model's parser walks (fake_packets.h), and the builders are
//     checked against that parser by the suite itself before any scenario trusts them.
//   * Every read the fake cannot serve answers "failed", never an invented value.
//   * fake::selfcheck() verifies the fake actually reaches the model (roster names, clock) and FAILS the suite
//     loudly if it does not -- a harness that silently stopped feeding the model would make every test vacuous.
//
// THE CLOCK. One monotonic clock for the whole run (model_now_ms and the FFXI tick move together). It is never
// wound back between tests : the builder keeps static state across frames (its zone grace, its row order memory)
// exactly as it does in game, and a clock that jumped backwards would put those in states the game never reaches.
#pragma once
#include "model/timers_build.h"
#include <initializer_list>

namespace fake {

// A party slot. `trust` sets the entity's NPC spawn bit (0x02), which is how the model tells a trust from a player.
struct Member { unsigned id; const char* name; int mjob; bool trust; };

// A fresh world : the given party (slot 0 is YOU), everyone in the same zone, no buffs, no packets seen yet.
// Clears the model (PartyState), the Timers focus monitor and the self buff list. The clock keeps running.
void world(std::initializer_list<Member> members);

// Time passes. Moves the model clock and the FFXI tick together.
void advance_ms(unsigned ms);
unsigned now_ms();
unsigned now_tick();            // what the model's ffxi_now_tick() must read -- checked by selfcheck()

// What you have equipped, item ids and their 24-byte extdata, as read_equipment_ext serves them. Not called = no read.
void equip(const unsigned short ids[16], const unsigned char ext[16][24]);

// YOUR buffs as the game's memory lists them (player+0x1C) : what read_player_buffs and GameState::buffs report.
void self_buffs(std::initializer_list<unsigned short> ids);
void self_buffs_unreadable();
// The party member array cannot be read (every model_copy into it FAILS) : what the game's memory looks like for a few
// seconds around a zone-in, when the server's 0x0DD has already arrived. false = readable again.
void party_memory_unreadable(bool on);
// The game's own TREASURE POOL memory (*(g+0x5C), what the in-game Treasure menu draws). Not called = the view is not
// mapped (read fails). An empty list = mapped and empty.
struct PoolSlot { int slot; unsigned short item; unsigned timestamp; unsigned short lot = 0; unsigned lotId = 0; const char* lotter = 0; };
void treasure_memory(std::initializer_list<PoolSlot> slots);   // the read FAILS (ok=false) -- not the same thing as an empty list

// Leave the zone and arrive in `zone` : the zone-out packet (0x00B), a loading screen of `loadMs`, every party member
// moved with you, then the zone-in packet (0x00A). What the game sends and what its memory says, in that order.
void zone_to(unsigned zone, unsigned loadMs = 3000);

// Deliver one incoming packet to the model (model_feed_packet), bracketed as a model event like the game does.
void packet(int id, const unsigned char* bytes);
// One game frame of model upkeep (roster refresh, prunes), bracketed as model event 'F'.
void frame();
// Build the Timers rows from the current world, bracketed as model event 'T' (the renderer's call, minus drawing).
bool build(aio::TimersRows& out);

// //aio out / //aio in, as the command dispatcher applies them (to the frozen copy too, in a dev build).
int out(const char* a, const char* b);
int in(const char* a, const char* b);

// Dev build only (0 elsewhere) : frames the frozen Timers builder was compared on, and how many differed.
unsigned shadow_frames();
unsigned shadow_mismatch();

// frame() + build() -- what one rendered frame does.
bool step(aio::TimersRows& out);

// The player has been in the zone for a while : run frames past the two 8 s zone-in graces (the model's ally-buff
// grace, PartyState::prune_other_buffs_worn, and the focus monitor's own). Right after world() both are armed, exactly
// as after a real zone-in -- and during them the builder deliberately keeps estimates and raises no OUT, so a case
// about alerts that did not settle first would be testing the grace, not the rule.
void settle();

// Row lookups for assertions. find_row returns the FIRST duration row matching (icon, who) -- who = 0 for rows
// about nobody in particular (your own buffs, AoE groups). -1 when absent.
int find_row(const aio::TimersRows& r, int icon, const char* who);
int count_rows(const aio::TimersRows& r, int icon);
// Prints the duration rows, for a failing test's output.
void dump(const aio::TimersRows& r, const char* label);

// Returns the number of failures (already counted by CHECK) : the fake does not reach the model.
int selfcheck();

} // namespace fake
