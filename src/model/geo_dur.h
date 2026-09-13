// geo_dur.h -- GEO Indicolure (Indi-) spell duration for a buff cast on an ally, reproducing the Windower Timers
// model. Geomancy duration is purely ADDITIVE (flat seconds), unlike enhancing (%) or songs (multipliers) :
//
//   dur = (Base + JP(1362)*2 + SUM(equipped "Indicolure ... duration +N" seconds)) x (1 + "Indi. eff. dur. +N%" augments)
//
// - Base : res/spells.lua duration (tb_buff_gen, skill 44). - JP gift 1362 "Indicolure Spell Effect Dur." = +2 s
//   per rank (GEO main only). - gear : native "Indicolure effect duration +N" FLAT seconds (geo_dur_gen.h, from
//   the item text -> universal / future-proof). A PERCENT exists, as an AUGMENT (geo_dur_augment_pct below, measured) ;
//   NO set bonus, NO merit (GEO's duration levers are only JP +
//   gear ; the group-1 merits are Indi/Geo POTENCY). Geo- (luopan) spells are NOT modelled -- they put no bearer
//   status on an ally, so they never reach the buffs-on-allies path.
#pragma once
#include "model/geo_dur_gen.h"   // GEO_DUR_LISTED : item id -> Indicolure duration seconds
#include "model/enh_dur.h"       // ext_augment_sum : the shared extdata augment decoder
namespace aio {

inline int geo_dur_gear_sec(const unsigned short ids[16]) {   // sum of equipped Indicolure-duration gear (flat seconds)
    int s = 0;
    for (int e = 0; e < 16; ++e) { if (!ids[e]) continue;
        for (int i = 0; i < GEO_DUR_LISTED_N; ++i) if (GEO_DUR_LISTED[i].id == ids[e]) { s += GEO_DUR_LISTED[i].sec; break; }
    }
    return s;
}

// "Indi. eff. dur. +N%" -- augment 1250 (0x4E2), a PERCENT (res/augments.lua), on Gada, Lifestream / Nantosuelta's capes ...
// MEASURED 2026-09-13 : an Indi- entrusted in a set with Gada +11 and Lifestream Cape +20 lasted 355-356 s, and
// (180 + 40 JP + 51 flat gear) x 1.31 = 355.0. The flat description lines above add FIRST, the percent multiplies the sum.
// Same system-1 extdata packing as the Enhancing augment next door (enh_dur.h, ext_augment_sum).
inline int geo_dur_augment_pct(const unsigned short ids[16], const unsigned char ext[16][24]) {
    int p = 0;
    for (int s = 0; s < 16; ++s) { if (!ids[s]) continue; p += ext_augment_sum(ext[s], 0x4E2u); }
    return p;
}
inline double geo_dur_sec(int base, int jpSec, int gearSec, int augPct) {
    return (double)(base + jpSec + gearSec) * (1.0 + augPct / 100.0);
}

} // namespace aio
