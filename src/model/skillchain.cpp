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
unsigned sc_job_weapon_mask(int job) {
    // the weapons each job really fights with (its usable combat skills with weapon skills worth offering), not every
    // weapon it can merely equip at E rank
    enum { H2H = 1u << 1, DAG = 1u << 2, SWD = 1u << 3, GS = 1u << 4, AXE = 1u << 5, GA = 1u << 6, SCY = 1u << 7, POL = 1u << 8,
           KAT = 1u << 9, GK = 1u << 10, CLB = 1u << 11, STF = 1u << 12, ARC = 1u << 25, MRK = 1u << 26 };
    static const unsigned M[23] = { 0,
        GA | AXE | GS | SCY | POL | SWD | CLB | DAG | STF,   // 1 WAR
        H2H | STF | CLB,                                     // 2 MNK
        CLB | STF,                                           // 3 WHM
        STF | CLB | SCY | DAG,                               // 4 BLM
        SWD | DAG | CLB | ARC,                               // 5 RDM
        DAG | SWD | MRK | ARC | H2H,                         // 6 THF
        SWD | GS | CLB | STF | POL | DAG,                    // 7 PLD
        SCY | GS | SWD | AXE | GA | CLB | DAG,               // 8 DRK
        AXE | SCY | CLB | SWD | DAG,                         // 9 BST
        DAG | SWD | STF | CLB,                               // 10 BRD
        ARC | MRK | DAG | SWD | AXE,                         // 11 RNG
        GK | POL | ARC | SWD | DAG,                          // 12 SAM
        KAT | DAG | SWD | MRK | H2H | ARC,                   // 13 NIN
        POL | SWD | STF | CLB | DAG,                         // 14 DRG
        STF | CLB | DAG,                                     // 15 SMN
        SWD | CLB,                                           // 16 BLU
        DAG | SWD | MRK,                                     // 17 COR
        H2H | DAG | CLB | STF,                               // 18 PUP
        DAG | SWD | H2H,                                     // 19 DNC
        STF | CLB | DAG,                                     // 20 SCH
        CLB | STF | DAG,                                     // 21 GEO
        GS | SWD | GA | AXE | CLB };                         // 22 RUN
    return (job >= 1 && job <= 22) ? M[job] : 0u;
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
