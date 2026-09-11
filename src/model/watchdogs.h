// watchdogs.h -- every knob of the four watchers, in ONE file, on purpose.
//
// WHY THIS FILE EXISTS AT ALL. The watchers added on 2026-09-11 exist to catch the failure shapes that cost
// whole evenings : a decision two rules argue over, a fixed table that silently drops what does not fit, a
// self-healing subsystem that is lost and says so only to a log nobody reads, and a decision whose reasons
// are gone by the time somebody asks why. All four are DIAGNOSTICS -- they must never be the reason the HUD
// misbehaves, and if one of them turns out to be wrong or noisy it has to be switchable off in one move, by
// somebody who is not holding the whole design in their head.
//
// So: every threshold is here, named, with the reasoning next to it, and `watch_enable(false)` (or the
// in-game `//aio watch off`) stops all of them observing and reporting. Nothing else in the codebase reads
// game state differently when they are off -- they only look.
//
// TUNING, tomorrow, in order of likelihood:
//   - a watcher cries wolf        -> raise its threshold below, rebuild. If it still cries wolf, DELETE the
//                                    call site : selftest.h's rule is that a check which fires wrongly gets
//                                    deleted, not tuned, because noise buries the one report that mattered.
//   - a watcher is silent when it should not be -> lower the threshold, or check the call site is reached.
//   - something is wrong and a watcher is suspected -> `//aio watch off` in game, no rebuild, no reload.
#pragma once

namespace aio {

// ---- the master switch ---------------------------------------------------------------------------------
// ON by default : a diagnostic nobody armed is a diagnostic that is not there on the evening it was needed.
// These are all cheap (integer compares on values the code already computed) and none of them writes to any
// state the HUD reads -- so the cost of leaving them on is a log line when something is genuinely wrong.
bool watch_enabled();
void watch_enable(bool on);

// ---- flipwatch : a decision that cannot make up its mind (flipwatch.h) ---------------------------------
static const int FLIP_SLOTS  = 16;   // distinct decisions watched at once. Full says so rather than going quiet.
static const int FLIP_TRIP   = 6;    // six A,B,A,B alternations with never a pause : an argument, not motion.
                                     // Lower = touchier. Below 4 a legitimate add-then-undo would be accused.
static const int FLIP_STEADY = 120;  // ~2 s at 60 fps of holding still forgets the history. Raise if a slow
                                     // module legitimately alternates a few times a minute and gets accused.

// ---- capwatch : a fixed table that silently drops what does not fit (capwatch.h) ------------------------
static const int CAP_SLOTS   = 16;   // distinct tables watched at once.
static const int CAP_HOLD    = 180;  // ~3 s at its cap before it counts as saturated. A table that touches its
                                     // cap for one frame during a zone-in is normal ; one that sits there is
                                     // dropping data every frame and nobody will ever see what was dropped.
static const int CAP_NEAR_N  = 2;    // "nearly full" = this many free slots or fewer. It is a WARNING with the
                                     // high-water mark, so a cap can be raised BEFORE the evening it overflows
                                     // -- which is how both FOCUS_MAX and OB_MAX were found, the hard way.

// ---- the decision recorder : why, not just what (decisions.h) ------------------------------------------
// DISARMED by default, unlike the other three : it formats strings, so it is the only one with a cost worth
// thinking about. `//aio decisions on` arms it, and it says in chat that it is on.
static const int DEC_RING    = 256;  // lines kept, oldest overwritten. ~40 KB of static buffer with DEC_LINE.
static const int DEC_LINE    = 160;  // a recorded line is one decision and its reasons -- not a paragraph.
static const int DEC_RATE_MAX = 30;  // lines per second per topic before the recorder says the call site is in a
                                     // per-frame path and throttles it. A DECISION does not happen 30 times a second.

} // namespace aio
