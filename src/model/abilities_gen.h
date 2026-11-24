// abilities_gen.h -- AUTO-GENERATED from Windower res/job_abilities.lua (do not edit).
// Regenerate: python scripts/gen_actions.py
// Declarations only : the table is defined ONCE, in abilities_gen.cpp.
#pragma once

namespace aio {

struct AbilRow { unsigned id; const char* en; unsigned mp; unsigned recast_id; unsigned short status; };
extern const AbilRow ABILS[];
extern const int ABILS_N;

const AbilRow* abil_info(unsigned id);     // binary search on id ; 0 when unknown

} // namespace aio
