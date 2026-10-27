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

} // namespace aio
