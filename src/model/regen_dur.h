// regen_dur.h -- REGEN-SPECIFIC effect-duration gear (flat seconds). Added to Regen's base BEFORE the enhancing-
// duration multiplier (so it multiplies through like JP/merit seconds), and it lengthens ONLY Regen (status 42) --
// which is exactly why a piece like Bolelabunga ('"Regen" duration +12') never touched Haste/Refresh and made
// Regen alone read short. The PRIMARY table is GENERATED from res/item_descriptions.lua (universal / future-proof,
// covers every current & future item with a native '"Regen" (effect) duration +N' line). Qualitative-text pieces
// (description says "Increases \"Regen\" effect duration" with no number) go in the EXTRA supplement below.
//
// NOTE : an AUGMENT-granted "Regen" duration is NOT decoded here yet (unlike enhancing duration's extdata 0x4E0).
// Innate gear covers the common case ; add augment decoding if a capture shows a residual on an augmented piece.
#pragma once
namespace aio {
struct RegenDurItem { unsigned short id; unsigned char sec; };
} // namespace aio

#include "model/regen_dur_gen.h"   // static const RegenDurItem REGEN_DUR_SEC[] / REGEN_DUR_SEC_N

namespace aio {
// SUPPLEMENT : pieces whose in-game text is qualitative ("Increases \"Regen\" effect duration", no number) so the
// generator can't read a value. Fill ids + seconds here when confirmed. Sentinel entry keeps the array non-empty.
static const RegenDurItem REGEN_DUR_EXTRA[] = { { 0, 0 } };
static const int REGEN_DUR_EXTRA_N = 0;   // count of REAL entries (0 -> the sentinel is skipped)

// total Regen-specific duration SECONDS over the 16 equipped ids (each item is in at most one table -> break).
inline int regen_dur_gear_sec(const unsigned short ids[16]) {
    int sec = 0;
    for (int s = 0; s < 16; ++s) {
        if (!ids[s]) continue;
        bool hit = false;
        for (int k = 0; k < REGEN_DUR_SEC_N; ++k)   if (REGEN_DUR_SEC[k].id == ids[s])   { sec += REGEN_DUR_SEC[k].sec;   hit = true; break; }
        if (hit) continue;
        for (int k = 0; k < REGEN_DUR_EXTRA_N; ++k) if (REGEN_DUR_EXTRA[k].id == ids[s]) { sec += REGEN_DUR_EXTRA[k].sec; break; }
    }
    return sec;
}
} // namespace aio
