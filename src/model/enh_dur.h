// enh_dur.h -- Enhancing Magic duration for a buff cast on an ALLY, reproducing the Windower Timers model
// (confirmed from its debug log + gear breakdown). The regular "Enhancing magic duration+%" gear normally
// only affects self-cast buffs ; the Estoqueur's/Lethargy set "Augments Composure" bonus UNLOCKS the caster's
// full enhancing-duration gear onto party members. Final duration :
//
//   dur = (Base + JP_seconds + merit_seconds)
//        x (1 + setBonus)         // Estoqueur's/Lethargy piece count : 2/3/4/5 -> +10/20/35/50%
//        x (1 + listedGear%)      // native "Enhancing magic duration +N%" item stats (ADD within category)
//        x (1 + augmentGear%)     // augment "Enh. Mag. eff. dur. +N%" (extdata + path augments ; ADD within)
//        , capped at 1800s (30 min).
//
// The three multipliers are SEPARATE (native x augment multiply ; within a category the % add). Validated :
// Haste (180) + 20 JP, 4pc set, listed 88%, augment 49% -> (180+20)*1.35*1.88*1.49 = 756.3s == Timers.
//
// The item->% tables below are UNIVERSAL game data (same for every player). Seeded from confirmed items ;
// extend from the full Timers.dll table.
#pragma once
namespace aio {

struct EnhDurItem { unsigned short id; signed char pct; };   // pct may be NEGATIVE (Sroda Necklace -50%)

// --- Estoqueur's (relic) / Lethargy (reforged) 5-slot set : worn count -> "Augments Composure" bonus % ---
// (item ids extracted from Timers.dll ; the feet/Houseaux pieces are also in ENH_DUR_LISTED for their native %.)
static const unsigned short COMPOSURE_SET_IDS[] = {
    0x2b3c, 0x2b50, 0x2b64, 0x2b78,                          // Estoqueur's +2 : head/body/hands/legs
    0x5a74, 0x5ab7, 0x5afa, 0x5b80, 0x5bc3, 0x5c06, 0x5c49,  // Lethargy +2/+3 : body/hands/legs/head...
    0x687c, 0x687d, 0x691a, 0x691b, 0x69b4, 0x69b5, 0x6a6d, 0x6a6e,   // Lethargy / +1 : head/body/hands/legs
    0x2b8c, 0x2bf0, 0x5c8c, 0x5b3d, 0x6b1b, 0x6b1c,          // feet (Houseaux) : Estq +2/+1, Leth +3/+2/base/+1
};
static const int COMPOSURE_SET_N = (int)(sizeof(COMPOSURE_SET_IDS) / sizeof(COMPOSURE_SET_IDS[0]));
inline bool is_composure_set_piece(unsigned short id) {
    if (!id) return false;
    for (int i = 0; i < COMPOSURE_SET_N; ++i) if (COMPOSURE_SET_IDS[i] == id) return true;
    return false;
}
inline int composure_set_pct(const unsigned short ids[16]) {
    int n = 0; for (int s = 0; s < 16; ++s) if (is_composure_set_piece(ids[s])) ++n;
    switch (n) { case 0: case 1: return 0; case 2: return 10; case 3: return 20; case 4: return 35; default: return 50; }
}

} // namespace aio -- (re-open below after the generated table needs EnhDurItem)

// --- NATIVE "Enhancing magic duration +N%" gear (m2 category ; % add). PRIMARY table is GENERATED from the
// game's own item descriptions (res/item_descriptions.lua) -> universal & future-proof : any current/future
// item with a native enhancing-duration LINE is covered automatically. ---
#include "model/enh_dur_listed_gen.h"

namespace aio {
// SUPPLEMENT : old relic/AF pieces whose description is QUALITATIVE ("Increases enhancing magic effect
// duration", no number) so the generator can't extract a % -- values confirmed from Timers.dll. (Sroda
// Necklace -50% IS in the generated table : its description spells out the number.)
static const EnhDurItem ENH_DUR_LISTED_EXTRA[] = {
    { 0x3f4c,  10 },   // Estoqueur's Cape
    { 0x2b8c,  20 },   // Estq. Houseaux +2
    { 0x2bf0,  10 },   // Estq. Houseaux +1
    { 0x6d2b,  15 },   // Atrophy Gloves (base)
};
static const int ENH_DUR_LISTED_EXTRA_N = (int)(sizeof(ENH_DUR_LISTED_EXTRA) / sizeof(ENH_DUR_LISTED_EXTRA[0]));

// --- PATH-augment "Enh. Mag. eff. dur." on Delve necks (value from the item's path/rank ; Timers delegates
// to the client augment decoder -- we approximate the common max-rank values). Colada/Ghostfyre etc. come
// through the extdata 0x4E0 decode below, not here. ---
static const EnhDurItem ENH_DUR_PATH[] = {
    { 0x6363, 25 },   // Duelist's Torque +2 (Path A, R25)
    { 0x6362, 20 },   // Duelist's Torque +1
    { 0x6361, 15 },   // Duelist's Torque
};
static const int ENH_DUR_PATH_N = (int)(sizeof(ENH_DUR_PATH) / sizeof(ENH_DUR_PATH[0]));

inline int enh_dur_table(const EnhDurItem* t, int n, unsigned short id) {
    if (!id) return 0; for (int i = 0; i < n; ++i) if (t[i].id == id) return t[i].pct; return 0;
}

// --- SCH Perpetuance : DOUBLES enhancing duration (x2 base), amplified by the equipped hands (extracted from
// Timers.dll). factor is the (mult-1)x100 : x2.0 base, up to x2.65 with Arbatel Bracers +3. ---
struct PerpBracer { unsigned short id; unsigned char f; };
static const PerpBracer PERP_BRACERS[] = {
    { 0x2bd7, 125 },   // Savant's Bracers +1  -> x2.25
    { 0x2b73, 150 },   // Savant's Bracers +2  -> x2.50
    { 0x69d2, 150 },   // Arbatel Bracers      -> x2.50
    { 0x69d3, 155 },   // Arbatel Bracers +1   -> x2.55
    { 0x5ac6, 160 },   // Arbatel Bracers +2   -> x2.60
    { 0x5c15, 165 },   // Arbatel Bracers +3   -> x2.65
};
static const int PERP_BRACERS_N = (int)(sizeof(PERP_BRACERS) / sizeof(PERP_BRACERS[0]));
inline double perpetuance_mult(const unsigned short ids[16]) {
    int f = 100;   // no bracer -> factor 1.0 -> x2.0
    for (int s = 0; s < 16; ++s) for (int i = 0; i < PERP_BRACERS_N; ++i) if (ids[s] == PERP_BRACERS[i].id) f = PERP_BRACERS[i].f;
    return 1.0 + f / 100.0;
}

// decode augment id 0x4E0 ("Enh. Mag. eff. dur.") from one item's 24-byte extdata (system-1 packing, see
// docs/game-data/player/player-equipment.md + extdata.lua). % = value + 1. Non-augmented / other systems -> 0.
inline int enh_dur_augment_ext(const unsigned char ext[24]) {
    const unsigned flag2 = ext[1];
    if (flag2 & 0x08) return 0;                 // crafting shield
    if (flag2 & 0x20) return 0;                 // system 2 (Delve path stat augments live elsewhere)
    if (flag2 == 131) return 0;                 // system 4 (path only ; its dur is in ENH_DUR_PATH)
    if (flag2 & 0x80) return 0;                 // system 3 (Evolith)
    const bool trial = (flag2 & 0x40) != 0;
    const int last = trial ? 9 : 11;
    int pct = 0;
    for (int i = 2; i + 1 <= last; i += 2) {
        const unsigned id = ext[i] + (ext[i + 1] & 7u) * 256u, val = ext[i + 1] >> 3;
        if (id == 0x4E0u) pct += (int)val + 1;
    }
    return pct;
}

// total NATIVE listed duration % over the 16 equipped ids (m2 = 1 + this/100). Generated table (res
// descriptions) + the hidden-stat supplement ; each item is in at most one, so summing both is safe.
inline int enh_dur_listed_pct(const unsigned short ids[16]) {
    int p = 0;
    for (int s = 0; s < 16; ++s) { p += enh_dur_table(ENH_DUR_LISTED, ENH_DUR_LISTED_N, ids[s]);
                                   p += enh_dur_table(ENH_DUR_LISTED_EXTRA, ENH_DUR_LISTED_EXTRA_N, ids[s]); }
    return p;
}
// total AUGMENT duration % (extdata 0x4E0 + path-augment table) (m3 = 1 + this/100).
inline int enh_dur_augment_pct(const unsigned short ids[16], const unsigned char ext[16][24]) {
    int p = 0;
    for (int s = 0; s < 16; ++s) { if (!ids[s]) continue; p += enh_dur_augment_ext(ext[s]); p += enh_dur_table(ENH_DUR_PATH, ENH_DUR_PATH_N, ids[s]); }
    return p;
}

} // namespace aio
