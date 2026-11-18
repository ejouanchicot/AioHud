// spell_cost.h -- the spells whose MP cost is not a number. Windower's res/spells.lua writes mp_cost 1 for them ;
// their real cost (BG Wiki, read 2026-11-18 -- "Full Cure dit 1 MP mais c'est full MP") :
//   Full Cure (893), Death (367) : ALL the MP left -- castable with 1 MP, not with 0
//   Embrava (478), Kaustra (502) : 20 % of the MAXIMUM MP
// One rule for every place that shows a spell's cost (the redrawn spell lists, the party's Cost box).
#pragma once

namespace aio {

enum SpellCostKind { SC_NUMBER = 0, SC_ALL_MP = 1, SC_PCT20 = 2 };

inline SpellCostKind spell_cost_kind(unsigned id, unsigned mp) {
    if (mp != 1) return SC_NUMBER;
    if (id == 893 || id == 367) return SC_ALL_MP;
    if (id == 478 || id == 502) return SC_PCT20;
    return SC_NUMBER;
}

} // namespace aio
