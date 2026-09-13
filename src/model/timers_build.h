// timers_build.h -- the Timers box rows and the FOCUS monitor, as MODEL code (no device, no font).
//
// Moved out of src/ui/hud_timers.cpp on 2026-09-13 (lot D : take the decisions out of the drawing code). The move
// was MECHANICAL, and these are its only edits :
//   * `struct Row` became TimersRow, and it and the TM_REM_MISSING / TM_FINE_NONE / FCLK_* constants moved here ;
//   * `static Row bufs[50], recs[50]` became storage the caller owns (TimersRows) ;
//   * the early `return` when there is nothing to draw became `return false` ;
//   * the body reads `f.game` through a one-member local, so the original lines are unchanged.
// Everything else -- every branch, every comment -- is the original text. src/ui/hud_timers.cpp draws the rows.
#pragma once
#include "windower.h"   // u32 (windower::u32) -- model must not depend on gfx/
using windower::u32;

namespace aio {

struct GameState;

// icon = buff status id ; both=1 -> icon+name (buff on an ally) ; order : 0 = your own buffs, 1+partyPos = allies.
// COR roll : name = "Chaos Roll", pip = the coloured pip number (0 = none), post = " (AoE 6)" suffix -> drawn as
// "Chaos Roll [5] (AoE 6)" with ONLY the pip in pipCol (unlucky=red, lucky/11=green, else white). nameCol overrides
// the whole-name colour (unused now that only the pip is tinted).
struct TimersRow { int rem; int fine; unsigned char fineClk; int icon; const char* name; const char* who; int order; u32 nameCol; int pip; u32 pipCol; const char* post; u32 postCol; const char* tag; u32 tagCol; int src; int mark; };   // who : the PERSON this row is about (0 = you) -- kept SEPARATE from `name` (the spell) because the display mode governs the icon and the spell name, never the person : "Icon" = icon + who, "Name" = who + spell, "Both" = the three. Rows used to carry one "Aeryn - Haste" string and a `both` flag that forced icon+name on them, so an ally row ignored the mode outright -- and the same buff switched between the grouped form (which obeyed it) and the per-person form (which did not) as you re-cast, which is what "it does not follow" was.   // mark : the focus-monitor number (0 = not monitored) -- what //aio out takes   // tag : BRD song modifiers "(SV)(T)" drawn in tagCol, between the name and the AoE suffix
static const int TM_REM_MISSING = -1000000000;   // FOCUS alert row : an ally is MISSING a critical buff -> timer shows "OUT" in red, sorts to the very top
// `fine` = the same remaining time as `rem` but in TICKS (1/60 s), used ONLY to sort. `rem` is ceil-ed to whole
// seconds for display, so two timers a fraction of a second apart show the SAME number every other second --
// and the tie-break below (icon, then name) then ordered them the other way round from the second before.
// The two rows swapped places once a second, forever : the "yoyo". `fine` ranks those ties by the instant the
// buff really expires, which the second alone cannot see -- the equal-second pair then keeps ONE order for
// its whole life. It is a tie-break only, never the primary key : the row sources round differently (a self
// buff ceils its tick, an ally estimate floors its ms), so leading with it could sort a visible 4:05 above a
// visible 4:04. Whatever sets `rem` must set `fine` from the SAME clock, or the row sorts by one and reads
// the other. TM_FINE_NONE = "no sub-second source" -> fall back to rem (see fineOf) ; only rows that never
// tick (the frozen demo/preview rows) are allowed to stay there.
static const int TM_FINE_NONE = -2000000000;
// WHICH CLOCK a row's `fine` was read from. Sub-second ordering only means something between rows
// measured the SAME way: your 0x063 expiry is absolute server ticks, an ally estimate is GetTickCount
// arithmetic, and the two drift past each other. Comparing them is comparing noise -- reported
// 2026-09-10 as two songs on the same timer "qui n'arretent pas de passer l'une en dessous de l'autre":
// an AoE Minuet (your own expiry) beside a Pianissimo Ballad on an ally (its estimate), whose fine
// values crossed and re-crossed for as long as both were up.
static const unsigned char FCLK_NONE = 0, FCLK_SELF = 1, FCLK_EST = 2;

struct TimersRows {
    TimersRow bufs[50];   // the Duration column
    TimersRow recs[50];   // the Recast column
    int nb = 0, nr = 0;
};

// Build this frame's rows. `game` may be null. preview / editing -> the demo rows. false = nothing to draw.
bool timers_build_rows(const GameState* game, bool preview, bool editing, TimersRows& out);

// ---- the FOCUS monitor, as people reach it ----
int  timers_focus_list(char out[][64], int max);               // //aio out list
int  timers_focus_forget(const char* a, const char* b);         // //aio out <n|name|spell|all|alerts>
int  timers_focus_restore(const char* a, const char* b);        // //aio in
void timers_reset();                                            // //aio timers reset + the config button
void timers_oblog_arm();                                        // //aio oblog
void timers_focus_trace(int seconds);                           // //aio ftrace
bool timers_focus_trace_armed();
void timers_register_checks();                                  // the in-game watcher (model/selftest.h)
#ifdef AIOHUD_PROBES
void songrow_ring_dump();                                       // //aio songdump
#endif

} // namespace aio
