// luacore_root.h -- WHERE LuaCore keeps its data root `g`, resolved at runtime instead of hard-coded.
//
// WHY THIS FILE EXISTS. Every single thing AioHUD reads from the game hangs off one pointer :
//     g = *(LuaCore.dll + RVA)
// and that RVA was a compile-time constant (0x1C8400) for the whole life of the project. ffximain_rva.h
// says, at the top, that `g` lives in "a Windower DLL -- a GAME patch does not touch it". True, and it
// hid the risk that actually fired : LuaCore is recompiled by the WINDOWER team, and Windower updates
// ITSELF, silently, for everyone. 4.7.9.3 (LuaCore 2.6.8.4, 2026-08-16) slid the root by 0x2020 to
// 0x1CA420 -- so `data_root()` returned 0, `read_player` failed, `inGame` stayed false and hud.cpp hid
// EVERY box while the config panel kept drawing and reported "not logged in". Nothing was wrong with the
// plugin ; it was reading an address that no longer meant anything.
//
// A constant cannot survive that. This does, because it asks LuaCore's own CODE where the root is:
// `get_party` (and thirty other bindings) load the global and immediately dereference it at an offset we
// already know -- `*(g+0x248)` the party array, `*(g+0x3C)` the player, `*(g+0x40)` the zone info. That
// two-instruction idiom is a signature nothing else in the image reproduces : on LuaCore 2.6.8.4 exactly
// ONE global matches it, 12 times, and the outgoing constant matches it zero times.
//
// The scan reads the MAPPED IMAGE, not live game data, so it works at the login screen, needs no
// character, and gives the same answer on every machine running that Windower build -- which is why it
// may latch once (rule 10 is about transient state ; a DLL's own code section is not transient). The
// only thing it must not latch is "LuaCore isn't mapped yet".
#pragma once

namespace aio {

// Absolute address of the SLOT that holds `g` (i.e. read a u32 there to get the root itself).
// 0 while LuaCore.dll isn't mapped. This is the one function data_root() is built on.
unsigned    lc_root_addr();

unsigned    lc_root_rva();    // its RVA inside LuaCore.dll, for logs / //aio doctor
const char* lc_root_how();    // "code scan" / "seed" / "unresolved" -- an address with no stated origin is a guess
bool        lc_root_live();   // the root currently reads back as a real pointer chain (false at the login screen)

// ---- the recast block : three CONTIGUOUS pointers off the root, 4 bytes apart ----
//
// They moved TOGETHER (+4) in the same 4.7.9.3 update that moved the root, and the symptom was not a dead
// feature but a LYING one : reading the ability-ids table as if it were the spell array showed a screenful
// of spells all on the same recast. The existing guard could not catch it -- it rejected values above 2h,
// and the array is u16, so every garbage value was already "plausible". Wrong-but-plausible is why these
// are derived from LuaCore's code too, with the two bindings cross-checking each other:
//   get_spell_recasts   loads the root, derefs at X, then loops to 0x400   -> X is the spell array
//   get_ability_recasts loads the root, derefs at Y, then reads byte [Y_ptr + i*8] -> Y is the ids table
// A layout where X != Y+4 is one neither binding describes, so the two must agree before either is used.
unsigned    lc_recast_ja_timers();   // *(g+off) -> int32[32], remaining recast in 1/60 s
unsigned    lc_recast_ja_ids();      // *(g+off) -> stride-8 entries, byte[0] = that slot's recast_id
unsigned    lc_recast_spells();      // *(g+off) -> ushort[1024], indexed directly by recast_id
const char* lc_recast_how();         // "code scan" / "seed" -- same rule : state where the number came from

} // namespace aio
