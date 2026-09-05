// corner_mask.h -- BAKED quarter-disc coverage masks : perfect rounded corners without shaders.
//
// WHY, over the feathered geometry in draw.cpp. A feathered corner is a triangle fan plus a 1.2px ring whose
// alpha ramps to 0. It is good, and it is what every other shape here uses, but it approximates coverage twice:
// the arc is a polygon (a chord always cuts inside the true curve) and the ramp is a fixed 1.2px linear falloff
// that sits ENTIRELY OUTSIDE the radius, so a corner reads slightly fat and slightly soft next to the crisp,
// pixel-aligned straight edges beside it.
//
// The baked mask answers both. Each radius gets its own r x r block whose texel alpha is the REAL coverage of
// the quarter disc, integrated at 16x16 samples per texel -- i.e. the analytic answer, computed once at load
// instead of approximated per frame. Drawn ONE TEXEL PER PIXEL, so there is no minification, no magnification
// and no filter guesswork: what the bake computed is what the pixel gets. This is the fixed-function way to
// get a perfect curve, and it predates shaders by a decade.
//
// A separate block PER RADIUS is the whole point, and the reason one big mask scaled to size would not do:
// anti-aliasing is a property of the PIXEL, not of the shape. A 64px mask drawn at 10px squeezes its one-texel
// transition into a sixth of a pixel (aliased again); drawn at 128px it smears it over two (soft again). At
// 1:1 the transition is exactly one pixel wide at every radius, which is the definition of correct coverage.
//
// THE ZONING RULE, and how this obeys it. docs/reference/d3d8-rendering.md 3c: while a zone loads, a MANAGED
// texture's ALPHA samples as ~255 -- so anything driving blend alpha from texture alpha renders opaque for the
// length of the load. This mask IS texture alpha, so it would draw square corners during a load. It is
// therefore GATED: the HUD calls corner_mask_enable(false) while party().is_zoning(), and draw.cpp falls back
// to the feathered geometry for those frames. The rule is respected by handling the case, not by pretending it
// cannot happen -- and the fallback is the code that shipped for a year, so the worst case is last week's look.
#pragma once
#include "d3d.h"

namespace aio {

// Radii with a baked block. Covers everything the UI uses (chips 7, cards 12, tabs 10 and their 13px edging)
// with room either side; outside this range the caller keeps the feathered path.
const int CM_RMIN = 3, CM_RMAX = 26;

u32  corner_mask_tex(u32 dev);        // ensure (bounded retry) + return the atlas ; 0 = unavailable, use geometry
bool corner_mask_uv(int r, float& u0, float& v0, float& u1, float& v1);   // the block for radius r (TOP-LEFT orientation)
void corner_mask_enable(bool on);     // the UI gates this OFF while a zone loads (see the note above)
bool corner_mask_enabled();
// A/B switch for a human, kept SEPARATE from the zoning gate so neither can clobber the other. //aio corners
// flips it. It exists because of rule 4 of the chrome audit: when a change is supposed to be visible and is
// not, the cheapest move is to CUT the suspect and ask a binary question -- "does turning this off change
// what you see?" answers in one frame what an hour of tuning does not.
void corner_mask_user_off(bool off);
bool corner_mask_user_is_off();
void corner_mask_forget();            // device lost : FORGET the handle -- never Release (the old device may be dead)
void corner_mask_dispose();           // the ONLY Release

} // namespace aio
