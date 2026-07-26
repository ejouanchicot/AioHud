// buff_atlas.h -- the shared status-icon atlas geometry (assets/buff_atlas.raw, built by
// scripts/gen_buff_atlas.ps1 from XivParty's icon set). A fixed 32-col grid of 32px cells ; a status id maps
// to cell (id % COLS, id / COLS). party / player / target / timers all read the SAME atlas, so the dimensions,
// the runtime path AND the texture itself live here once (each widget keeps its own per-icon layout/draw).
//
// ONE OWNER (buff_atlas.cpp). Every widget used to load its OWN copy of this 1024x640 BGRA sheet -- NINE
// handles (party x3 : the party box + two alliance boxes ; player ; target ; timers/debuffs ; and three Help
// samples : timers, debuffs, and the target one owned by the config page) at 2.5 MiB each, up to ~23 MB of
// the same pixels. They now READ a single handle and NEVER release it, so the
// triple-Release hazard that made v1.0.47 keep them separate cannot exist: there is exactly one
// release_texture call site, in buff_atlas_dispose(). See tex_retry.h for why that decision changed.
// NOTE : buff_cell_uv() below is the intended single source for the id->UV math, but no widget calls it yet --
// the 9 draw sites still inline the same two divisions. Adopting it is a behaviour-preserving cleanup, listed
// in docs/notes/audit-technique-2026-07-26.md ; until then this header holds TWO copies of that math, not one.
#pragma once
#include "gfx/d3d.h"          // u32
#include "model/paths.h"      // plugin_path (runtime-derived asset path)

namespace aio {

static const int BUFF_ATLAS_W = 1024, BUFF_ATLAS_H = 640, BUFF_CELL = 32, BUFF_COLS = 32;
static const int BUFF_ATLAS_ROWS = BUFF_ATLAS_H / BUFF_CELL;         // 20 -> highest mappable id = COLS*ROWS - 1 (639)

inline const char* buff_atlas_path() { static char b[260]; if (!b[0]) plugin_path(b, 260, "assets\\buff_atlas.raw"); return b; }

// ---- the ONE shared texture (buff_atlas.cpp). Consumers call buff_atlas_tex() and must NEVER release it. ----
u32  buff_atlas_tex(u32 dev);      // lazily loads with the bounded retry ; returns 0 while it is unavailable
void buff_atlas_forget();          // device LOST : forget the handle (old device may be dead) + RE-ARM the retry
                                   //   budget. Never Releases -- CLAUDE.md rule 4.
void buff_atlas_dispose();         // device ALIVE (//unload) : Release once, then forget.
unsigned buff_atlas_tries();       // failed attempts so far -- for //aio selfcheck (0 = loaded or never needed)

// UV of status-icon `id`'s cell : cell size (au,av) + top-left (u0,v0). id must be in [0, COLS*ROWS).
inline void buff_cell_uv(int id, float& au, float& av, float& u0, float& v0) {
    au = (float)BUFF_CELL / (float)BUFF_ATLAS_W; av = (float)BUFF_CELL / (float)BUFF_ATLAS_H;
    u0 = (float)(id % BUFF_COLS) * au; v0 = (float)(id / BUFF_COLS) * av;
}

} // namespace aio
