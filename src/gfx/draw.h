// draw.h -- generic geometry submission helpers (one quad / strip per call).
//
// These build a vertex array and hand it to DrawPrimitiveUP. They do NOT touch
// render state (blend mode, texture, FVF) -- the caller sets that up first. The
// textured helpers expect FVF 0x144 to be selected; the *_quad colour-only ones
// expect FVF 0x44. Coordinates are screen pixels (XYZRHW, 2560x1400 here).
#pragma once
#include "d3d.h"

namespace aio {

// Round to the nearest whole pixel. The half-pixel offset every builder below applies only produces a crisp
// 1px edge if the coordinate handed in is already ON the pixel grid -- see CLAUDE.md rule 1. This lived as
// SEVEN byte-identical copies across the ui/ TUs ; one drifting copy would mean one silently blurry widget,
// so it is defined once, here, next to the code whose contract depends on it.
inline float snap(float v) { return (float)(int)(v + 0.5f); }

// --- textured (FVF 0x144) ---

// textured quad, explicit UVs, left/right vertex colours (horizontal gradient).
void tquad(u32 dev, float x, float y, float w, float h,
           float u0, float u1, float v0, float v1, u32 cL, u32 cR);

// full-texture quad (size w x h) rotated `ang` radians about centre (cx,cy), tinted `col` (MODULATE) --
// directional markers : rotate an up/right-pointing icon by the entity heading.
void tquad_rot(u32 dev, float cx, float cy, float w, float h, float ang, u32 col);

// filled TEXTURED circle (triangle fan) centred at (cx,cy) radius r, tinted `col`. Vertex UV = (u0+(vx-cx)*du,
// v0+(vy-cy)*dv) : the caller passes the linear screen->UV mapping so a map can be drawn genuinely round with
// no stencil (round minimap). Centre UV = (u0,v0). Draw under a textured (0x144) state, tex bound.
void tdisc(u32 dev, float cx, float cy, float r, float u0, float v0, float du, float dv, u32 col);

// textured quad, VERTICAL colour gradient (top vs bottom) -> liquid volume.
void tquad_v(u32 dev, float x, float y, float w, float h,
             float u0, float u1, float v0, float v1, u32 cTop, u32 cBot);

// textured quad, FOUR independent corner colours (h+v gradient) + explicit UVs.
void tquad4(u32 dev, float x, float y, float w, float h,
            float u0, float u1, float v0, float v1,
            u32 cTL, u32 cTR, u32 cBL, u32 cBR);

// textured quad over UV 0..1 with a single diffuse tint (sprites: glow, bubble).
void glow_quad(u32 dev, float x, float y, float w, float h, u32 color);

// textured quad over UV 0..1, white diffuse, optional horizontal mirror (cap art).
void cap_quad(u32 dev, float x, float y, float w, float h, bool flip);

// --- colour-only (FVF 0x44) ---

// vertex-coloured quad (4 corner colours = a bilinear gradient).
void grad_quad(u32 dev, float x, float y, float w, float h, u32 cTL, u32 cTR, u32 cBL, u32 cBR);

// a SOFT (feathered) line segment A->B, `th` px wide: full colour at the centre
// line, alpha 0 at the two long edges -> a glowing band, not a hard rectangle.
void seg_soft(u32 dev, float ax, float ay, float bx, float by, float th, u32 col);

// a soft glowing BLOB centred at (cx,cy): bright centre fading to 0 every way.
void soft_blob(u32 dev, float cx, float cy, float hw, float hh, u32 col);

// a filled CIRCLE (triangle fan) centred at (cx,cy), radius r -> crisp round dot/bullet.
void disc(u32 dev, float cx, float cy, float r, u32 col);

// a filled TRIANGLE (3 corners), one solid colour -> selection arrow / chevrons. HARD edges (no AA) : prefer
// fill_poly_aa for any small on-screen triangle/chevron/diamond ; keep fill_tri only for interiors that are
// feathered separately (edge_feather) or fully covered.
void fill_tri(u32 dev, float x0, float y0, float x1, float y1, float x2, float y2, u32 col);

// an ANTI-ALIASED filled convex polygon (`n` = 3..12 points, CW or CCW) : solid core + feathered rim (disc's
// recipe) -> crisp AA triangles / diamonds / chevrons with no texture. Call in the colour-quad state.
void fill_poly_aa(u32 dev, const float* xy, int n, u32 col);

// feather the OUTER ARC (radius r, a0..a1, `seg` facets) of a solid quarter-disc fan -> AA rounded-rect
// corners. Call right after the solid fan, sampled at the same seg count / angles.
void arc_feather(u32 dev, float cx, float cy, float r, float a0, float a1, int seg, u32 col);

// an ANTI-ALIASED rounded rectangle : vertical-gradient fill (cTop..cBot) with a feathered outer rim
// (alpha 0 at edge+`feather`), so the four rounded corners are CRISP at any size/radius/tint -- no texture.
// Same recipe as disc() (solid core + feathered ring). Corners are uniform because every corner shares the
// SAME arc segment count + feather width. `feather` in px (0 = hard edge). Radius clamps to min(w,h)/2.
void rrect(u32 dev, float x, float y, float w, float h, float r, u32 cTop, u32 cBot, float feather = 1.2f);

// AA rounded rect with a border : draws a `bt`-px border ring in `border`, then the inner fill on top.
void rrect_bordered(u32 dev, float x, float y, float w, float h, float r,
                    u32 cTop, u32 cBot, u32 border, float bt, float feather = 1.2f);

// Rounded CLIP via the stencil buffer : write a rounded-rect mask (feather 0), then everything drawn until
// rrect_clip_end() is limited to that shape -> genuinely ROUND ends with TRANSPARENT corners (the vial liquid,
// the target HP track). No stencil in the back-buffer -> ops ignored, falls back to square (never black).
void rrect_clip_begin(u32 dev, float x, float y, float w, float h, float r);
void rrect_clip_end(u32 dev);

// AA rounded rect with only the LEFT corners rounded (right edge FLAT / vertical) -> a bar fill whose left
// end is a capsule cap while its right end stays a clean vertical level (aligns with a level marker).
void rrect_left(u32 dev, float x, float y, float w, float h, float r, u32 cTop, u32 cBot, float feather = 1.2f);

// a SMOOTH glow that hugs a rounded-rect silhouette : one feathered band per edge/corner from the path
// (peak = `col` alpha) fading LINEARLY to 0 at path+`glowW`. No interior fill, no concentric bands. Draw
// under ADDITIVE blend (SRCALPHA/ONE) for a clean luminous halo behind a button. Uniform corners.
void rrect_glow(u32 dev, float x, float y, float w, float h, float r, u32 col, float glowW);

// a SMOOTH round glow : a feathered ring from radius r (peak = `col` alpha) fading to 0 at r+`glowW`.
// Draw under ADDITIVE blend for a clean circular halo (e.g. behind a slider knob). No banding.
void disc_glow(u32 dev, float cx, float cy, float r, u32 col, float glowW);

// stroke the OUTLINE of a rounded rect : a `bt`-px border in `col` that hugs the capsule shape (feathered
// outer arc = AA corners), WITHOUT filling the interior. A border that follows the fluid form. Draw on top.
void rrect_stroke(u32 dev, float x, float y, float w, float h, float r, u32 col, float bt);

} // namespace aio
