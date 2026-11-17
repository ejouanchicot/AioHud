// mounts_gen.h -- AUTO-GENERATED from Windower res/mounts.lua (do not edit).
// Regenerate: python scripts/gen_mounts.py
#pragma once

namespace aio {

struct MountRow { unsigned id; const char* en; };
static const MountRow MOUNTS[] = {
    {0, "Chocobo"},
    {1, "Raptor"},
    {2, "Tiger"},
    {3, "Crab"},
    {4, "Red Crab"},
    {5, "Bomb"},
    {6, "Sheep"},
    {7, "Morbol"},
    {8, "Crawler"},
    {9, "Fenrir"},
    {10, "Beetle"},
    {11, "Moogle"},
    {12, "Magic Pot"},
    {13, "Tulfaire"},
    {14, "Warmachine"},
    {15, "Xzomit"},
    {16, "Hippogryph"},
    {17, "Spectral Chair"},
    {18, "Spheroid"},
    {19, "Omega"},
    {20, "Coeurl"},
    {21, "Goobbue"},
    {22, "Raaz"},
    {23, "Levitus"},
    {24, "Adamantoise"},
    {25, "Dhalmel"},
    {26, "Doll"},
    {27, "Golden Bomb"},
    {28, "Buffalo"},
    {29, "Wivre"},
    {30, "Red Raptor"},
    {31, "Iron Giant"},
    {32, "Byakko"},
    {33, "Noble Chocobo"},
    {34, "Ixion"},
    {35, "Phuabo"},
    {36, "Craklaw"},
    {37, "Alicorn"},
    {38, "Bubble Crab"},
};
static const int MOUNTS_N = 39;

inline const char* mount_name(unsigned id) {
    for (int i = 0; i < MOUNTS_N; ++i) if (MOUNTS[i].id == id) return MOUNTS[i].en;
    return 0;
}

} // namespace aio
