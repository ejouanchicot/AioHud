// resistances.h -- Odyssey (Sheol) target resistance model. Ported from the SheolHelper addon's compute_res :
// a mob NAME -> family (strip the "Nostos "/"Agon " prefix) -> RES_ROWS (generated) -> per-weapon (Slashing/
// Piercing/Blunt) and per-element (Fire..Dark) resistance %, plus the Cruel-Joke vulnerability for the current
// Sheol (A/B/C). Pure data : no engine / memory deps.
#pragma once

namespace aio {

// one displayable resistance cell : value in PERCENT (already Odyssey-adjusted) + a colour class.
struct ResCell { int pct; unsigned char color; };   // color : 0 white (=100%), 1 green (resistant/high), 2 red (weak/low)

struct ResData {
    bool          valid;
    unsigned char wtype;        // the family's type weapon : 0 Slashing, 1 Piercing, 2 Blunt, 3 Magic, 4 Unknown
    ResCell       weapon[3];    // Slashing, Piercing, Blunt
    ResCell       elem[8];      // Fire, Ice, Wind, Earth, Thunder, Water, Light, Dark
    signed char   jokeVuln;     // Cruel Joke : -1 = don't show, 0 = immune (red), 1 = vulnerable (green)
    char          family[24];   // resolved family name (for the header)
};

// mobName = the target's name ; sheolzone = 1/2/3 (Sheol A/B/C ; 0 = unknown -> Cruel-Joke default). Fills `out`.
bool compute_resistances(const char* mobName, int sheolzone, ResData& out);

inline const char* res_weapon_name(int i) {
    static const char* N[3] = { "Slashing", "Piercing", "Blunt" };
    return (i >= 0 && i < 3) ? N[i] : "";
}
// elemental cell colours (Fire..Dark), from the addon's ELE_COL, as ARGB for the small element square.
inline unsigned res_elem_color(int i) {
    static const unsigned C[8] = { 0xFFFF5A46u, 0xFF8CE1FFu, 0xFF8CE696u, 0xFFCDA55Fu,
                                   0xFFCF86FFu, 0xFF5AA0FFu, 0xFFF5F5F5u, 0xFF6B4A8Cu };   // Thunder = bright violet, Dark = deep dark violet (were both mid-purple -> indistinguishable)
    return (i >= 0 && i < 8) ? C[i] : 0xFFFFFFFFu;
}

} // namespace aio
