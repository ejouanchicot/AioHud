// pet_common.h -- what the job contents of the Pet box share (hud_pet.cpp places the box ; hud_bst.cpp, hud_geo.cpp
// draw it) : the one drag state of //aio edit, and the small drawing helpers both contents had a copy of.
// docs/design/pet-module-spec.md. Not public.
#pragma once
#include "gfx/d3d.h"       // u32
#include "ui/edit_box.h"   // EditBox

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

} // namespace aio
