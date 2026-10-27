// bst_correlation.h -- monster correlation (who intimidates whom), for the BST box's correlation line
// (src/ui/hud_bst.cpp). Checked 2026-10-27 on BG-Wiki's eleven "<Family> Killer" pages ("All Beasts have this trait"
// on Lizard Killer, ...) and the JP wiki's chart (wiki.ffo.jp/html/3253.html) :
//   Beast >> Lizard >> Vermin >> Plantoid >> Beast   (four, circular)
//   Bird >> Aquan >> Amorph >> Bird                  (three, circular)
//   Undead <> Arcana, Demon <> Dragon                (each strong AND weak against the other)
// The same family : neither. Families outside these (Luminian, elementals, beastmen) : none. Tested in
// tests/t_readycharges.cpp.
#pragma once
#include <string.h>
#include "model/bst_pets_gen.h"   // each jug pet's family (bst_pet_eco)

namespace aio {

struct BstCorrelation { const char* eco; const char* strong; const char* weak; };
static const BstCorrelation BST_CORRELATION[] = {
    { "Beast",    "Lizard",   "Plantoid" },
    { "Lizard",   "Vermin",   "Beast" },
    { "Vermin",   "Plantoid", "Lizard" },
    { "Plantoid", "Beast",    "Vermin" },
    { "Bird",     "Aquan",    "Amorph" },
    { "Aquan",    "Amorph",   "Bird" },
    { "Amorph",   "Bird",     "Aquan" },
    { "Undead",   "Arcana",   "Arcana" },
    { "Arcana",   "Undead",   "Undead" },
    { "Demon",    "Dragon",   "Dragon" },
    { "Dragon",   "Demon",    "Demon" },
};

// the row of family `eco`, 0 when it has no correlation
inline const BstCorrelation* bst_correlation(const char* eco) {
    if (!eco || !eco[0]) return 0;
    for (const BstCorrelation& c : BST_CORRELATION) if (strcmp(c.eco, eco) == 0) return &c;
    return 0;
}

// the family of a jug pet by its name, 0 when the database does not know it. The game writes a pet's name without its
// spaces ("BlackbeardRandy", "Hip.Familiar", "Left-HandedYoko") where the database keeps them ("Blackbeard Randy") :
// both compared on their letters and digits only, as the art's file key (hud_bst.cpp art_key)
inline bool bst_same_name(const char* a, const char* b) {
    for (;;) {
        while (*a && !((*a >= 'A' && *a <= 'Z') || (*a >= 'a' && *a <= 'z') || (*a >= '0' && *a <= '9'))) ++a;
        while (*b && !((*b >= 'A' && *b <= 'Z') || (*b >= 'a' && *b <= 'z') || (*b >= '0' && *b <= '9'))) ++b;
        if (*a != *b) return false;
        if (!*a) return true;
        ++a; ++b;
    }
}
inline const char* bst_pet_eco(const char* name) {
    if (!name || !name[0]) return 0;
    for (int i = 0; i < BST_PETS_N; ++i) if (bst_same_name(BST_PETS[i].name, name)) return BST_PETS[i].eco;
    return 0;
}

} // namespace aio
