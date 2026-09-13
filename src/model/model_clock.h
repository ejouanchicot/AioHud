// model_clock.h -- the model's ONE source of "now", so the model can be driven outside the game to the millisecond.
//
// WHY. The packet handlers and the per-frame upkeep read GetTickCount() and time(0) directly -- 78 call sites on
// 2026-09-13. A handler that stamps `startMs = GetTickCount()` cannot be replayed offline: the replay would stamp
// today's clock, and every comparison against the live session would differ by a meaningless amount. So the model
// asks this header instead, and the answer depends on who is running it:
//   * in game, OUTSIDE an event : the real clock, exactly as before.
//   * in game, INSIDE an event (one packet, or one frame of model upkeep) : the clock as it was when the event
//     started, frozen until it ends. A handler that reads the tick three times gets one value -- which is also what
//     an observer of the event (model_io.h) sees.
//   * in an offline test build : whatever that build pins with model_clock_pin.
// Frozen-per-event changes nothing a player can see : an event runs in well under a millisecond.
#pragma once
#include <ctime>

namespace aio {

unsigned model_now_ms();     // GetTickCount semantics (ms, wraps at 49.7 days)
time_t   model_now_unix();   // time(0) semantics

// Bracket one model event. Nests : only the outermost begin latches. `kind` names the event for the observer
// ('P' packet, 'F' frame upkeep, 'L' load-time roster read).
void model_event_begin(char kind);
void model_event_end();

// RAII form, for the call sites.
struct ModelEvent {
    explicit ModelEvent(char kind) { model_event_begin(kind); }
    ~ModelEvent() { model_event_end(); }
    ModelEvent(const ModelEvent&) = delete;
    ModelEvent& operator=(const ModelEvent&) = delete;
};

// Test hook : pin the clock.
void model_clock_pin(unsigned ms, time_t unix);

} // namespace aio
