// skillchain.cpp -- Skillchains module data lookup (Phase 1 : the per-action property table).
// Detection state (the resonating windows fed by the 0x028 action packet) is added on top of this next.
#include "model/skillchain.h"
#include "model/skillchain_gen.h"   // SC_WEAPON_SKILLS[] etc. (generated)

namespace aio {

// binary search a sorted SkillRow[] by id.
static const SkillRow* find_row(const SkillRow* a, int n, unsigned id) {
    int lo = 0, hi = n - 1;
    while (lo <= hi) {
        const int mid = (lo + hi) >> 1;
        const unsigned m = a[mid].id;
        if (m == id) return &a[mid];
        if (m < id) lo = mid + 1; else hi = mid - 1;
    }
    return 0;
}

int sc_aeonic_weapon_of_item(unsigned item) {
    if (!item) return -1;
    for (int i = 0; i < SC_AEONIC_ITEMS_N; ++i) if (SC_AEONIC_ITEMS[i].item == item) return SC_AEONIC_ITEMS[i].weapon;   // 16 entries : a linear scan is fine
    return -1;
}
const SkillRow* sc_skill_lookup(int resource, unsigned id) {
    switch (resource) {
        case SCR_WS:    return find_row(SC_WEAPON_SKILLS,    SC_WEAPON_SKILLS_N,    id);
        case SCR_SPELL: return find_row(SC_SPELLS,           SC_SPELLS_N,           id);
        case SCR_MOB:   return find_row(SC_MONSTER_ABILITIES, SC_MONSTER_ABILITIES_N, id);
        case SCR_JA:    return find_row(SC_JOB_ABILITIES,    SC_JOB_ABILITIES_N,    id);
        case SCR_ELEM:  return find_row(SC_ELEMENTS,         SC_ELEMENTS_N,         id);
        default:        return 0;
    }
}

// the addon's check_props : for each active property (old), does the candidate's property (neu) combine ?
bool sc_check_props(const unsigned char* old, int nOld, const unsigned char* neu, int nNew, int& lvl, int& result) {
    for (int k = 0; k < nOld; ++k) {
        if (old[k] >= SCP_N) continue;
        const SCInfo& combo = sc_info(old[k]);
        for (int i = 0; i < nNew; ++i) {
            if (neu[i] >= SCP_N) continue;
            for (int c = 0; c < combo.nCombo; ++c)
                if (combo.combo[c].with == neu[i]) { lvl = combo.combo[c].lvl; result = combo.combo[c].result; return true; }
            if (nOld > 3 && combo.lvl == sc_info(neu[i]).lvl) break;   // addon's >3-active early-out
        }
    }
    return false;
}

} // namespace aio
