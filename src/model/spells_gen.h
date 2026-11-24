// spells_gen.h -- AUTO-GENERATED from Windower res/spells.lua (do not edit).
// Regenerate: python scripts/gen_actions.py
// Declarations only : the table is defined ONCE, in spells_gen.cpp.
#pragma once

namespace aio {

struct SpellRow { unsigned id; const char* en; unsigned cast_ms; unsigned mp; unsigned recast_id; unsigned short status; unsigned char trust; unsigned short icon; };
extern const SpellRow SPELLS[];
extern const int SPELLS_N;

const SpellRow* spell_info(unsigned id);   // binary search on id ; 0 when unknown

} // namespace aio
