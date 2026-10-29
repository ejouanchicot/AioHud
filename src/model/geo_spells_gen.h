// geo_spells_gen.h -- AUTO-GENERATED from res/spells.lua (do not edit).
// Regenerate: python scripts/gen_geo_spells.py -- every Geo- / Indi- spell for the GEO box (model/geo_state.cpp).
#pragma once

namespace aio {

// element : res numbering (0 Fire 1 Ice 2 Wind 3 Earth 4 Lightning 5 Water 6 Light 7 Dark) ; status : the Indi- of
// the same name's (a Geo- has none in res) ; geo : 1 Geo- (luopan), 0 Indi- ; offensive : targets an enemy
struct GeoSpellRow { unsigned short id; const char* en; unsigned char element; unsigned short status; unsigned char geo; unsigned char offensive; };
static const GeoSpellRow GEO_SPELLS[] = {
    {768, "Indi-Regen", 6, 539, 0, 0},
    {769, "Indi-Poison", 5, 540, 0, 0},
    {770, "Indi-Refresh", 6, 541, 0, 0},
    {771, "Indi-Haste", 2, 580, 0, 0},
    {772, "Indi-STR", 0, 542, 0, 0},
    {773, "Indi-DEX", 4, 543, 0, 0},
    {774, "Indi-VIT", 3, 544, 0, 0},
    {775, "Indi-AGI", 2, 545, 0, 0},
    {776, "Indi-INT", 1, 546, 0, 0},
    {777, "Indi-MND", 5, 547, 0, 0},
    {778, "Indi-CHR", 6, 548, 0, 0},
    {779, "Indi-Fury", 0, 549, 0, 0},
    {780, "Indi-Barrier", 3, 550, 0, 0},
    {781, "Indi-Acumen", 1, 551, 0, 0},
    {782, "Indi-Fend", 5, 552, 0, 0},
    {783, "Indi-Precision", 4, 553, 0, 0},
    {784, "Indi-Voidance", 2, 554, 0, 0},
    {785, "Indi-Focus", 7, 555, 0, 0},
    {786, "Indi-Attunement", 6, 556, 0, 0},
    {787, "Indi-Wilt", 5, 557, 0, 0},
    {788, "Indi-Frailty", 2, 558, 0, 0},
    {789, "Indi-Fade", 0, 559, 0, 0},
    {790, "Indi-Malaise", 4, 560, 0, 0},
    {791, "Indi-Slip", 3, 561, 0, 0},
    {792, "Indi-Torpor", 1, 562, 0, 0},
    {793, "Indi-Vex", 6, 563, 0, 0},
    {794, "Indi-Languor", 7, 564, 0, 0},
    {795, "Indi-Slow", 3, 565, 0, 0},
    {796, "Indi-Paralysis", 1, 566, 0, 0},
    {797, "Indi-Gravity", 2, 567, 0, 0},
    {798, "Geo-Regen", 6, 539, 1, 0},
    {799, "Geo-Poison", 5, 540, 1, 1},
    {800, "Geo-Refresh", 6, 541, 1, 0},
    {801, "Geo-Haste", 2, 580, 1, 0},
    {802, "Geo-STR", 0, 542, 1, 0},
    {803, "Geo-DEX", 4, 543, 1, 0},
    {804, "Geo-VIT", 3, 544, 1, 0},
    {805, "Geo-AGI", 2, 545, 1, 0},
    {806, "Geo-INT", 1, 546, 1, 0},
    {807, "Geo-MND", 5, 547, 1, 0},
    {808, "Geo-CHR", 6, 548, 1, 0},
    {809, "Geo-Fury", 0, 549, 1, 0},
    {810, "Geo-Barrier", 3, 550, 1, 0},
    {811, "Geo-Acumen", 1, 551, 1, 0},
    {812, "Geo-Fend", 5, 552, 1, 0},
    {813, "Geo-Precision", 4, 553, 1, 0},
    {814, "Geo-Voidance", 2, 554, 1, 0},
    {815, "Geo-Focus", 7, 555, 1, 0},
    {816, "Geo-Attunement", 6, 556, 1, 0},
    {817, "Geo-Wilt", 5, 557, 1, 1},
    {818, "Geo-Frailty", 2, 558, 1, 1},
    {819, "Geo-Fade", 0, 559, 1, 1},
    {820, "Geo-Malaise", 4, 560, 1, 1},
    {821, "Geo-Slip", 3, 561, 1, 1},
    {822, "Geo-Torpor", 1, 562, 1, 1},
    {823, "Geo-Vex", 6, 563, 1, 1},
    {824, "Geo-Languor", 7, 564, 1, 1},
    {825, "Geo-Slow", 3, 565, 1, 1},
    {826, "Geo-Paralysis", 1, 566, 1, 1},
    {827, "Geo-Gravity", 2, 567, 1, 1},
};
static const int GEO_SPELLS_N = 60;

inline const GeoSpellRow* geo_spell(unsigned id) {
    for (int i = 0; i < GEO_SPELLS_N; ++i) if (GEO_SPELLS[i].id == id) return &GEO_SPELLS[i];
    return 0;
}
// the Geo- / Indi- whose effect is `status` (the aura AioHUD tracks by its status) ; the Indi- one
inline const GeoSpellRow* geo_spell_by_status(unsigned status) {
    if (status) for (int i = 0; i < GEO_SPELLS_N; ++i) if (!GEO_SPELLS[i].geo && GEO_SPELLS[i].status == status) return &GEO_SPELLS[i];
    return 0;
}

} // namespace aio
