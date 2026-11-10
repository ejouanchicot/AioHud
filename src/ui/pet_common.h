// pet_common.h -- what the job contents of the Pet box share (hud_pet.cpp places the box ; hud_bst.cpp, hud_geo.cpp,
// hud_smn.cpp draw it) : the one drag state of //aio edit, and the small drawing helpers both contents had a copy of.
// docs/design/pet-module-spec.md. Not public.
#pragma once
#include "gfx/d3d.h"       // u32
#include "ui/edit_box.h"   // EditBox
#include "model/gamestate.h"   // PetJa

namespace aio {

class Font;

// the Pet box's //aio edit drag state : one box, whichever job draws it
EditBox& pet_edit_box();

// a texture drawn tinted by `col` (a white icon takes the colour, a coloured one keeps its own with 0xFFFFFFFF),
// rows v0..v1 of it only (a charge filling from the bottom)
void pet_tex_quad(u32 dev, u32 tex, float x, float y, float w, float h, float v0, float v1, u32 col);

// a thin gauge : dark track, coloured fill
void pet_gauge(u32 dev, float x, float y, float w, float h, float frac, u32 fill);

// `s` made to fit `maxW` : at `z`, else a step smaller (90 %, then 80 % : three sizes at most, so the font bakes few
// atlases), else cut at 80 % with a dot where it was cut. Returns the size to draw at.
float pet_fit_text(Font* fo, const char* s, float z, float maxW, char* out, int cap);

// `s` cut to `maxW` at `z` with a dot where it was cut (the size kept)
void pet_fit_cut(Font* fo, const char* s, float z, float maxW, char* out, int cap);

// an element's colour, res numbering (0 Fire 1 Ice 2 Wind 3 Earth 4 Lightning 5 Water 6 Light 7 Dark) : the same tints
// in every content, so one element reads the same in the BST, GEO and SMN boxes
u32 pet_elem_col(int e);

// a time left : 1:45 from a minute up, 42s under, 1h05 from an hour, "--" while it is not known (sec < 0)
void pet_fmt_left(int sec, char* out, int cap);

// the share of an ability's recast still to run, for an icon that fills as it runs : the game gives the time left,
// not the length (merits and gear shorten it), so the length is the longest recast seen for that ability this session ;
// the first time, the icon starts empty. `sample` (//aio edit, the config preview) learns nothing : half filled.
float pet_recast_share(unsigned short abil, int sec, bool sample);

// a pet's TP in the player's own TP colours (palette.cpp tp_palette, the violet of the liquid bars : dull under 1000,
// brighter at each weapon skill tier) -- blue is MP's colour everywhere in AioHUD (the player, 2026-11-10)
u32 pet_tp_col(int tp);

// a grid of job abilities (the BST and SMN contents) : `cols` columns across `w` from (x, y), each cell `cellH` high :
// the status's icon when the ability has one (dimmed while on recast, the part of the recast gone lit from the
// bottom), its name, then "ok" (green) or its recast ; the one whose effect is on the player in gold on a lit pill.
// `jas` : the cells to draw, in order. `sample` : //aio edit and the preview (pet_recast_share learns nothing).
// Returns the height drawn.
float pet_ja_grid(u32 dev, Font* fj, float zj, float oj, u32 nameCol, float x, float y, float w, float cellH,
                  float iconSz, float S, const PetJa* const* jas, int n, int cols, bool sample);

} // namespace aio
