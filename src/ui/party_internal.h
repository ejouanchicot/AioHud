// party_internal.h -- the small drawing helpers shared by party.cpp and party_gauges.cpp.
//
// Extracted when the gauge-shape library moved out of party.cpp : these were file-statics that BOTH halves
// need. `static inline` keeps them internal to each TU exactly as before, so nothing about linkage or
// behaviour changes -- this is still a pure move.
#pragma once
#include "gfx/draw.h"
#include "gfx/d3d.h"
#include "ui/ui_colors.h"

namespace aio {


// lerp_color / hp_color / scl -> ui/ui_colors.h (shared with target/player)
static inline u32 lt(u32 c, float f) {
    int r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
    r += (int)((255 - r) * f); g += (int)((255 - g) * f); b += (int)((255 - b) * f);
    return (c & 0xFF000000) | (r << 16) | (g << 8) | b;
}
// modern "shine" : a LOCALISED soft highlight that sweeps left->right once per cycle (smoothstep
// travel + fade in/out + pause). Drawn as N strips with a RAISED-COSINE falloff -> a smooth,
// glassy glint (not a hard triangle). All clamped to [px, px+w] so it never spills outside.
static inline void shine_sweep(u32 dev, float px, float ry, float w, float h, float ph, u32 rgb, float peakA) {
    if (ph >= 0.60f) return;                                  // pause between sweeps
    const float s  = ph / 0.60f;                             // 0..1 across the sweep
    const float e  = s * s * (3.0f - 2.0f * s);              // smoothstep -> non-linear travel
    const float bw = w * 0.36f;                              // band half-width
    const float c  = px - bw + (w + 2.0f * bw) * e;          // band centre : off-left -> off-right
    const float A  = peakA * sinf(s * 3.14159265f);         // peak alpha, ramped in then out
    if (A <= 0.003f) return;
    const u32 rgbm = rgb & 0x00FFFFFF;
    const int N = 12;
    float pa = -1.0f, pxc = 0.0f;                            // previous strip alpha + x (carry the edge colour)
    for (int i = 0; i <= N; ++i) {
        float x = c - bw + (2.0f * bw) * ((float)i / N);
        if (x < px) x = px; if (x > px + w) x = px + w;
        float d = (x - c) / bw; if (d < 0) d = -d;           // 0 at centre .. 1 at edge
        float v = d >= 1.0f ? 0.0f : 0.5f + 0.5f * cosf(d * 3.14159265f);   // raised cosine
        int qa = (int)(A * v * 255.0f + 0.5f); if (qa > 255) qa = 255;
        u32 col = ((u32)qa << 24) | rgbm;
        if (pa >= 0.0f && x > pxc) {
            u32 pcol = ((u32)(int)pa << 24) | rgbm;
            grad_quad(dev, pxc, ry, x - pxc, h, pcol, col, pcol, col);
        }
        pa = (float)qa; pxc = x;
    }
}

static inline void setup_color_state(u32 dev) {
    dColorQuadState(dev);   // shared 2D colour-quad pipeline (gfx/d3d.h)
}
// standard TEXTURED-quad state (MODULATE tex*diffuse, straight alpha, CLAMP, LINEAR min/mag) : the block
// every sprite draw shares (cursor / job icon / buffs / dot markers). Binds `tex` on stage 0.
// mipLinear=false keeps a pixel-exact atlas crisp (the buff atlas) ; true = LINEAR mips elsewhere.
static inline void setup_tex_state(u32 dev, u32 tex, bool mipLinear = true) { dTexQuadState(dev, tex, mipLinear); }static inline void vgrad(u32 dev, float x, float y, float w, float h, u32 top, u32 bot) {
    grad_quad(dev, x, y, w, h, top, top, bot, bot);
}
// one HP/MP/TP gauge. `pct` is the (already-lerped) fill % ; `t` drives a subtle liquid
// shimmer ; `pulse` (0..1) brightens + adds an outer glow (used for TP >= 1000 = WS ready).
// `danger` (0..1) : critical HP -> the bar BLINKS in alarm-red, same glow+pulse principle
// as the TP WS-ready pulse but tinted red (red halo breathing + red flash over the liquid).
// DX8 has no rounded primitive : the clean way for a SOLID rounded shape is to triangulate it so the
// rrnd = an ANTI-ALIASED rounded rect (feathered corners), for gauge tracks / cells / bars.
static inline void rrnd(u32 dev, float x, float y, float w, float h, float r, u32 c) {   // rounded on all 4 corners
    rrect(dev, x, y, w, h, r, c, c);   // ANTI-ALIASED (feathered corners) -> clean rims on tracks / cells / bars
}


} // namespace aio
