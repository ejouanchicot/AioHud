// decisions.h -- keep the REASONS, not just the result, for the last few hundred decisions.
//
// WHY THIS EXISTS. Every hard bug of the last two days was found the same way and none of them by reading the
// code: a ring buffer of WHY lines, dumped after the fact. `//aio songdump` cracked the songs hunt in one
// capture after hours of guessing, because the answer was never "what does it show" -- it was "what did it
// decide, and on what evidence". The rest of the codebase does this with ARMED traces (//aio dbflog, ftrace,
// tpool, songlog), which have one flaw in common: you have to have armed them BEFORE the thing happened. In
// practice you notice the bug first, and by then the evidence is gone and you are asking the user to make it
// happen again.
//
// This is the other half. It is always recording, into a fixed ring, and `//aio why` dumps it. There is no
// arming, so there is nothing to have forgotten.
//
// THE ONE RULE FOR CALL SITES. Record DECISIONS, not frames. "chose Minuet V over Ballad III because 41 s < 78 s"
// is a decision ; "drawing 7 rows" is a frame, and 60 of those a second turn a 256-line ring into 4 seconds of
// history and a formatting cost in the render path. The recorder polices this itself (see DEC_RATE_MAX) and
// names the offending topic rather than quietly becoming the problem it was added to find.
#pragma once
#include "model/watchdogs.h"

namespace aio {

struct DecLine { unsigned ms; const char* topic; char text[DEC_LINE]; };

// Record one decision. `topic` must be a string LITERAL (it is kept by pointer and compared by address, like
// the other watchers) -- "zone", "songs", "debuff", "rva". printf-style, no %f (windower_debug drops it).
void dec_record(const char* topic, const char* fmt, ...);

// Dump the ring to aiohud_debug.log, oldest first, each line stamped with how long ago it was recorded.
// `topic` = 0 dumps everything. Returns the number of lines written.
int  dec_dump(const char* topic);

// The raw ring, for a caller that wants to render it rather than log it. `n` = lines in use.
const DecLine* dec_ring(int& n, int& oldest);

} // namespace aio
