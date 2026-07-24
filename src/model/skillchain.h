// skillchain.h -- skillchain domain data + detection state (module : Skillchains, ported from the reference
// SkillChains addon by Ivaar ; the big per-action property table lives in the GENERATED skillchain_gen.h).
//
// A skillchain forms when a closing weaponskill/spell/TP-move's OPENING property matches the resonating
// (active) property of the last one, within a short window. The 0x028 action packet of the CLOSING hit
// carries an ADDED EFFECT whose animation (1..16) IS the resulting skillchain and whose message id marks it
// as a skillchain -> that is all Phase 1 needs to DETECT a chain (no memory reverse). The combination table
// (SC_INFO) + the per-action property table (skillchain_gen.h) drive step tracking and, in Phase 2, the list
// of your moves that would continue the chain.
#pragma once
#include <cstddef>   // pure data header : no engine deps (colours are plain unsigned)

namespace aio {

// --- skillchain PROPERTIES (keep this order in sync with scripts/gen_skillchain.py PROPS) ---
enum SCProp {
    SCP_Transfixion = 0, SCP_Compression, SCP_Liquefaction, SCP_Scission, SCP_Reverberation,
    SCP_Detonation, SCP_Impaction, SCP_Induration, SCP_Gravitation, SCP_Fragmentation,
    SCP_Distortion, SCP_Fusion, SCP_Light, SCP_Darkness, SCP_Radiance, SCP_Umbra, SCP_N
};
// --- magic-burst ELEMENTS ---
enum SCElem { EL_Fire = 0, EL_Ice, EL_Wind, EL_Earth, EL_Lightning, EL_Water, EL_Light, EL_Dark, EL_N };

// one generated per-action row : id -> up to 3 opening skillchain properties (255 = none) + TP delay + aeonic prop
// (the SCProp it gains on an Aeonic weapon, 255 = none) + weapon (the Aeonic-weapon idx that grants it, 255 = none).
struct SkillRow { unsigned short id; unsigned char prop[3]; unsigned char delay; unsigned char aeonic; unsigned char weapon; };

// --- names ---
inline const char* scprop_name(int p) {
    static const char* N[SCP_N] = { "Transfixion","Compression","Liquefaction","Scission","Reverberation",
        "Detonation","Impaction","Induration","Gravitation","Fragmentation","Distortion","Fusion",
        "Light","Darkness","Radiance","Umbra" };
    return (p >= 0 && p < SCP_N) ? N[p] : "";
}
inline const char* scelem_name(int e) {
    static const char* N[EL_N] = { "Fire","Ice","Wind","Earth","Lightning","Water","Light","Dark" };
    return (e >= 0 && e < EL_N) ? N[e] : "";
}

// --- colours (brightened to read on the midnight-blue box, from the addon's colors table) ---
inline unsigned scelem_color(int e) {
    static const unsigned C[EL_N] = { 0xFFFF5050u,0xFF00FFFFu,0xFF66FF66u,0xFFCD8C46u,0xFFFF00FFu,0xFF508CFFu,0xFFFFFFFFu,0xFF8C8CFFu };
    return (e >= 0 && e < EL_N) ? C[e] : 0xFFFFFFFFu;
}
inline unsigned scprop_color(int p) {
    // each property inherits its element's colour (Transfixion=Light, Compression=Dark, ...) ; the level-2/3/4
    // properties have their own bespoke tints.
    static const unsigned C[SCP_N] = {
        0xFFFFFFFFu, // Transfixion  (Light)
        0xFF8C8CFFu, // Compression  (Dark)
        0xFFFF5050u, // Liquefaction (Fire)
        0xFFCD8C46u, // Scission     (Earth)
        0xFF508CFFu, // Reverberation(Water)
        0xFF66FF66u, // Detonation   (Wind)
        0xFFFF00FFu, // Impaction    (Lightning)
        0xFF00FFFFu, // Induration   (Ice)
        0xFFBE8246u, // Gravitation
        0xFFFA9CF7u, // Fragmentation
        0xFF3399FFu, // Distortion
        0xFFFF6666u, // Fusion
        0xFFFFFFFFu, // Light
        0xFF8C8CFFu, // Darkness
        0xFFFFFFFFu, // Radiance     (Light)
        0xFF8C8CFFu, // Umbra        (Dark)
    };
    return (p >= 0 && p < SCP_N) ? C[p] : 0xFFFFFFFFu;
}

// --- SC_INFO : per resulting property, its burst ELEMENTS + level + the combinations OLD.with(NEW) -> {lvl, result}.
struct SCCombo { unsigned char with, lvl, result; };            // this property (OLD) + `with` (NEW) -> lvl / result
struct SCInfo  { unsigned char elem[4], nElem, lvl; SCCombo combo[3]; unsigned char nCombo; };

inline const SCInfo& sc_info(int p) {
    static const SCInfo T[SCP_N] = {
        /*Transfixion */ { {EL_Light},1,1, { {SCP_Scission,2,SCP_Distortion},{SCP_Reverberation,1,SCP_Reverberation},{SCP_Compression,1,SCP_Compression} },3 },
        /*Compression */ { {EL_Dark},1,1,  { {SCP_Transfixion,1,SCP_Transfixion},{SCP_Detonation,1,SCP_Detonation} },2 },
        /*Liquefaction*/ { {EL_Fire},1,1,  { {SCP_Impaction,2,SCP_Fusion},{SCP_Scission,1,SCP_Scission} },2 },
        /*Scission    */ { {EL_Earth},1,1, { {SCP_Liquefaction,1,SCP_Liquefaction},{SCP_Reverberation,1,SCP_Reverberation},{SCP_Detonation,1,SCP_Detonation} },3 },
        /*Reverberation*/{ {EL_Water},1,1, { {SCP_Induration,1,SCP_Induration},{SCP_Impaction,1,SCP_Impaction} },2 },
        /*Detonation  */ { {EL_Wind},1,1,  { {SCP_Compression,2,SCP_Gravitation},{SCP_Scission,1,SCP_Scission} },2 },
        /*Impaction   */ { {EL_Lightning},1,1, { {SCP_Liquefaction,1,SCP_Liquefaction},{SCP_Detonation,1,SCP_Detonation} },2 },
        /*Induration  */ { {EL_Ice},1,1,   { {SCP_Reverberation,2,SCP_Fragmentation},{SCP_Compression,1,SCP_Compression},{SCP_Impaction,1,SCP_Impaction} },3 },
        /*Gravitation */ { {EL_Earth,EL_Dark},2,2, { {SCP_Distortion,3,SCP_Darkness},{SCP_Fragmentation,2,SCP_Fragmentation} },2 },
        /*Fragmentation*/{ {EL_Wind,EL_Lightning},2,2, { {SCP_Fusion,3,SCP_Light},{SCP_Distortion,2,SCP_Distortion} },2 },
        /*Distortion  */ { {EL_Ice,EL_Water},2,2, { {SCP_Gravitation,3,SCP_Darkness},{SCP_Fusion,2,SCP_Fusion} },2 },
        /*Fusion      */ { {EL_Fire,EL_Light},2,2, { {SCP_Fragmentation,3,SCP_Light},{SCP_Gravitation,2,SCP_Gravitation} },2 },
        /*Light       */ { {EL_Fire,EL_Wind,EL_Lightning,EL_Light},4,3, { {SCP_Light,4,SCP_Light} },1 },     // Light+Light -> Light Lv.4 ; ONLY an Aeonic weapon's WS upgrades it to Radiance (done in the continuation list, gated on the equipped Aeonic + Aftermath -- NOT an Empyrean like Ukko's Fury)
        /*Darkness    */ { {EL_Earth,EL_Ice,EL_Water,EL_Dark},4,3, { {SCP_Darkness,4,SCP_Darkness} },1 },   // Darkness+Darkness -> Darkness Lv.4 ; Aeonic upgrades to Umbra the same way
        /*Radiance    */ { {EL_Fire,EL_Wind,EL_Lightning,EL_Light},4,4, {},0 },
        /*Umbra       */ { {EL_Earth,EL_Ice,EL_Water,EL_Dark},4,4, {},0 },
    };
    return T[(p >= 0 && p < SCP_N) ? p : 0];
}

// --- 0x028 ADDED-EFFECT animation (1..16) -> resulting skillchain property (the CLOSE detector). 0 = none. ---
inline int sc_from_add_effect_anim(unsigned a) {
    switch (a) {
        case 1:  return SCP_Light;         case 2:  return SCP_Darkness;
        case 3:  return SCP_Gravitation;   case 4:  return SCP_Fragmentation;
        case 5:  return SCP_Distortion;    case 6:  return SCP_Fusion;
        case 7:  return SCP_Compression;   case 8:  return SCP_Liquefaction;
        case 9:  return SCP_Induration;    case 10: return SCP_Reverberation;
        case 11: return SCP_Transfixion;   case 12: return SCP_Scission;
        case 13: return SCP_Detonation;    case 14: return SCP_Impaction;
        case 15: return SCP_Radiance;      case 16: return SCP_Umbra;
        default: return -1;
    }
}

// message ids that mark an action as a skillchain (the CLOSE add-effect message) + as a WS/spell finish (the OPEN).
inline bool sc_is_skillchain_msg(unsigned m) {
    return (m >= 288 && m <= 301) || (m >= 385 && m <= 397) || (m >= 767 && m <= 770);
}
inline bool sc_is_finish_msg(unsigned m) {
    return m == 110 || m == 185 || m == 187 || m == 317 || m == 802;   // "uses <WS>" / hits-with-add-effect finishes
}

// per-action property lookup (weapon skill / spell / mob TP / job ability / SCH element) -> the generated row, or 0.
enum SCResource { SCR_WS = 0, SCR_SPELL, SCR_MOB, SCR_JA, SCR_ELEM };
const SkillRow* sc_skill_lookup(int resource, unsigned id);

// the Aeonic-weapon idx for an equipped item id (SC_AEONIC_ITEMS), or -1 if that item is not an Aeonic. A WS whose
// SkillRow.weapon == this idx gains its aeonic opening property while you wield it (see the Phase-2 continuation list).
int sc_aeonic_weapon_of_item(unsigned item);

// Does a candidate move's opening properties `neu` CONTINUE a chain whose active properties are `old` ?
// (the addon's check_props). On success returns true + the resulting skillchain level (1..4) and property.
// Used by Phase 2 to list which of your weapon skills extend the current resonance.
bool sc_check_props(const unsigned char* old, int nOld, const unsigned char* neu, int nNew, int& lvl, int& result);

} // namespace aio
