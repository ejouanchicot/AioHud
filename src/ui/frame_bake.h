// frame_bake.h -- the PROCEDURAL frame (//aio kittest iron, 2026-11-20) : a group of glued boxes baked as ONE polished
// iron plate with the cavities milled out of it. No tiles, no corner pieces, no seam : every outer edge, shared separator,
// T-junction and cell wall comes out of the same distance field. CPU-baked once per layout (theme_kit.cpp caches it).
#pragma once
#include "gfx/d3d.h"

namespace aio {

// all rects in the group's own 1x pixels (origin = the group's top-left), integers
struct BakeShape {
    int W, H;                 // the group's size
    float b;                  // the border (the shared separator's width)
    float kb;                 // the scale of every relief constant (b / 11 : tuned at an 11 px border)
    int nb; const float (*box)[4];     // the boxes : their union = the plate ; each one's cavity = itself inset b
    int np; const float (*plate)[5];   // metal ISLANDS inside a cavity (x y w h radius) : a slot plate
    int nc; const float (*cut)[5];     // holes in those islands (x y w h radius) : the slots
    float ox, oy;                      // the group's SCREEN place : the lights and the grain are anchored to the screen
    int style;                         // 0 polished iron ; 1 blued steel + gold inlay ; 2 carved limestone ; 3 WROUGHT IRON
};
// out : W*H ARGB (straight alpha, row 0 = top). false : the scratch memory could not be had (logged by the caller)
bool bake_iron(const BakeShape& s, u32* out, int* outSuper = 0);
extern int g_bakePhaseMs[8];   // the last bake's phases (ms) : union, metal, distances, height, shadow, shading

} // namespace aio
