// party_gauges.cpp -- the alternative gauge SHAPES + the cursor / selection-frame primitives, split out of
// party.cpp (PURE MOVE).
//
// These are free functions with ZERO coupling to the Party class -- party.h already exports several of them
// for the config Help samples. They were simply the first 330 lines a reader of party.cpp had to scroll past
// before reaching the box itself.
#include "ui/party.h"
#include "ui/party_internal.h"   // the drawing helpers shared with party.cpp
#include "ui/ui_colors.h"        // scl() : called 10x here -- it only compiled because party_internal.h happens to include it
#include "ui/hud_internal.h"
#include "ui/liquid_bars.h"
#include "model/ui_config.h"
#include "gfx/draw.h"
#include "gfx/d3d.h"
#include "gfx/texture.h"
#include <math.h>
#include <windows.h>

namespace aio {

// ---- alternative gauge shapes (ui_config().gaugeStyle) : all sized to the player-row height ----

// SHAPE-MATCHED GLOW : a coloured border that peeks out BEHIND the gauge (WS-ready = gauge colour,
// critical-HP = red), pulsing. gauge_glow() (shared, in liquid_bars.cpp) returns the pulsing colour +
// border thickness ; each style then draws its OWN shape (rect / disc / diamond) at size + g behind it.
// RECT border (Bars / Segments / Minimal) : a rounded-rect peeking behind.
static void gauge_aura_soft(u32 dev, float gx, float gy, float gw, float gh, u32 col, float t, float pulse, float danger) {
    u32 gcol; float g;
    if (gauge_glow(col, t, pulse, danger, gcol, g)) rrect_glow(dev, gx, gy, gw, gh, gh * 0.5f, gcol, g + 2.5f);   // capsule-shaped glow hugging the fluid form
}
// DISC border (Sphere / Radial / Ring) : an AA circle peeking behind.
static void gauge_aura_round(u32 dev, float cx, float cy, float R, u32 col, float t, float pulse, float danger) {
    u32 gcol; float g;
    if (gauge_glow(col, t, pulse, danger, gcol, g)) disc(dev, cx, cy, R + g, gcol);
}

// SMOOTH liquid : a circle filled from the bottom up to the surface line yTop, drawn as an anti-aliased
// circular SEGMENT (curved arc with a feathered rim + a flat top) instead of stepped horizontal bands.
static void liquid_segment(u32 dev, float cx, float cy, float r, float yTop, u32 col) {
    cx -= 0.5f; cy -= 0.5f;                                   // D3D half-pixel rule (matches disc())
    float s = (yTop - cy) / r; if (s < -1.0f) s = -1.0f; if (s >= 1.0f) return;
    const float PI = 3.14159265f, a0 = asinf(s), a1 = PI - a0;   // arc through the bottom (theta = +pi/2, screen y down)
    const int N = 48;
    float span = (cy + r) - yTop; if (span < 1.0f) span = 1.0f;
    // solid fan from the surface midpoint to the arc. VOLUMETRIC shade : bright at the top-centre, darker
    // toward the bottom (f) AND toward the sides (ex) -> the liquid reads as a rounded 3D body, not a flat fill.
    VtxC fan[N + 2];
    fan[0] = { cx, yTop - 0.5f, 0.0f, 1.0f, scl(col, 1.34f) };
    for (int i = 0; i <= N; ++i) {
        float a = a0 + (a1 - a0) * (float)i / N, ca = cosf(a), y = cy + r * sinf(a);
        float f = (y - (yTop - 0.5f)) / span; if (f < 0) f = 0; if (f > 1) f = 1;
        fan[i + 1] = { cx + r * ca, y, 0.0f, 1.0f, scl(col, 1.34f - 1.05f * f - 0.15f * fabsf(ca)) };
    }
    dDrawUP(dev, D3DPT_TRIANGLEFAN, N, fan, sizeof(VtxC));
    // feathered rim along the ARC only (opaque at r -> transparent at r+f) = the anti-aliased edge
    const float fth = 1.2f;
    VtxC ring[2 * (N + 1)];
    for (int i = 0; i <= N; ++i) {
        float a = a0 + (a1 - a0) * (float)i / N, ca = cosf(a), sa = sinf(a), y = cy + r * sa;
        float f = (y - (yTop - 0.5f)) / span; if (f < 0) f = 0; if (f > 1) f = 1;
        u32 ci = scl(col, 1.34f - 1.05f * f - 0.15f * fabsf(ca));
        ring[2 * i]     = { cx + r * ca,         y,               0.0f, 1.0f, ci };
        ring[2 * i + 1] = { cx + (r + fth) * ca, cy + (r + fth) * sa, 0.0f, 1.0f, ci & 0x00FFFFFF };
    }
    dDrawUP(dev, D3DPT_TRIANGLESTRIP, 2 * N, ring, sizeof(VtxC));
}

// a horizontal band centred on cx : from (yT, half-width hcT, colour cT) down to (yB, half-width hcB, cB).
// Used for the level line so each edge follows the shape's half-width at ITS y -> the band hugs the shape.
static void aa_hstrip(u32 dev, float cx, float yT, float hcT, u32 cT, float yB, float hcB, u32 cB) {
    VtxC v[4] = { { cx - hcT, yT, 0.0f, 1.0f, cT }, { cx - hcB, yB, 0.0f, 1.0f, cB }, { cx + hcT, yT, 0.0f, 1.0f, cT }, { cx + hcB, yB, 0.0f, 1.0f, cB } };
    dDrawUP(dev, D3DPT_TRIANGLESTRIP, 2, v, sizeof(VtxC));
}
// per-vertex coloured triangle (for volumetric fills).
static void fill_tri_c(u32 dev, float x0, float y0, u32 c0, float x1, float y1, u32 c1, float x2, float y2, u32 c2) {
    VtxC v[3] = { { x0, y0, 0.0f, 1.0f, c0 }, { x1, y1, 0.0f, 1.0f, c1 }, { x2, y2, 0.0f, 1.0f, c2 } };
    dDrawUP(dev, D3DPT_TRIANGLEFAN, 1, v, sizeof(VtxC));
}
// vertical shade : bright at yTop (br0) -> dark at yBot (br1), for a lit-from-the-surface liquid body.
static u32 vshade(u32 col, float y, float yTop, float yBot, float br0, float br1) {
    float d = yBot - yTop; float f = d > 0.5f ? (y - yTop) / d : 0.0f; if (f < 0.0f) f = 0.0f; if (f > 1.0f) f = 1.0f;
    return scl(col, br0 - (br0 - br1) * f);
}
// the fiole-style LEVEL LINE (dark contrast band + additive glow + bright core) at yFill, using the shape's
// half-widths wM/wA/wB (at yFill, yFill-g, yFill+g) so it hugs whatever form. Call in the colour-quad state.
static float shape_hw(int shape, float rad, float cy, float y) {   // shape half-width at height y : 0 = circle, 1 = diamond
    float d = y - cy;
    if (shape == 0) { float v = rad * rad - d * d; return v > 0.0f ? sqrtf(v) : 0.0f; }
    float w = rad - fabsf(d); return w > 0.0f ? w : 0.0f;
}
// HORIZONTAL level line (Sphere / Crystal), faithfully mirroring the fiole's surface_glow : a dark contrast
// band + a WIDE additive coloured glow (moderate alpha keeps the hue -> not white) + a bright core. Each
// band uses the shape's half-width at ITS y so the whole thing hugs the form.
static void level_line(u32 dev, float cx, float cy, float yFill, u32 col, float rad, int shape) {
    const float wM = shape_hw(shape, rad, cy, yFill);
    if (wM <= 1.0f) return;
    aa_hstrip(dev, cx, yFill, wM, 0x00000000, yFill + 4.0f, shape_hw(shape, rad, cy, yFill + 4.0f), 0x5A000000);   // dark contrast band below
    dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_ONE);                                   // additive WIDE coloured glow (fiole recipe)
    const float G = 5.0f;
    const u32 c = (scl(col, 1.3f) & 0x00FFFFFF) | 0x4A000000, c0 = c & 0x00FFFFFF;
    aa_hstrip(dev, cx, yFill - G, shape_hw(shape, rad, cy, yFill - G), c0, yFill, wM, c);   // above : 0 -> glow
    aa_hstrip(dev, cx, yFill, wM, c, yFill + G, shape_hw(shape, rad, cy, yFill + G), c0);   // below : glow -> 0
    const u32 core = (scl(col, 1.9f) & 0x00FFFFFF) | 0x88000000;                  // bright core (like scale_rgb(c,1.6), alpha 0x88)
    aa_hstrip(dev, cx, yFill - 1.2f, shape_hw(shape, rad, cy, yFill - 1.2f), core, yFill + 1.2f, shape_hw(shape, rad, cy, yFill + 1.2f), core);
    dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
}
// VERTICAL level line (fiole surface_glow recipe) at xFill over [yT, yT+h] : dark contrast band + additive
// glow + bright core. For the horizontal-fill styles (Bars / Segments / Minimal).
static void level_line_v(u32 dev, float xFill, float yT, float h, u32 col) {
    if (h < 1.0f) return;
    const u32 c = (scl(col, 1.3f) & 0x00FFFFFF) | 0x4A000000, c0 = c & 0x00FFFFFF;   // like the fiole's `men` (moderate alpha)
    // (no dark contrast band : it read as a black "gap" in the fluid on the flat-tipped bars ; the flat
    //  coloured tip already IS the level, so only the additive coloured glow + bright core mark it)
    dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_ONE);                                       // additive WIDE coloured glow (6px each side)
    grad_quad(dev, xFill - 6.0f, yT, 6.0f, h, c0, c, c0, c);   // glow rising to the surface
    grad_quad(dev, xFill,        yT, 6.0f, h, c, c0, c, c0);   // glow falling past it
    const u32 core = (scl(col, 1.9f) & 0x00FFFFFF) | 0x88000000;                      // bright core (2px)
    grad_quad(dev, xFill - 1.0f, yT, 2.0f, h, core, core, core, core);
    dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
}

// SPHERE : a round glass bulb filling bottom -> top with liquid (horizontal bands clipped to the circle).
static void gauge_sphere(u32 dev, float gx, float gy, float gw, float gh, float pct, u32 col, float t, float pulse, float danger) {
    const float cx = gx + gw * 0.5f, cy = gy + gh * 0.5f;
    float r = (gw < gh ? gw : gh) * 0.5f - 1.0f; if (r < 2.0f) r = 2.0f;
    gauge_aura_round(dev, cx, cy, r, col, t, pulse, danger);
    disc(dev, cx, cy, r + 1.0f, 0xFF243150);            // anti-aliased rim (feathered edge)
    disc(dev, cx, cy, r,        0xFF0A0E1C);            // anti-aliased dark glass bulb
    if (pct > 0.0f) {
        const float rr = r - 0.5f;                       // liquid hugs the glass (just inside the AA rim)
        const bool  full = pct >= 99.5f;                 // at full : fill the WHOLE dome, no surface line
        // push the surface a hair ABOVE the top when full so the segment covers the entire bulb (no flat
        // chord / shoulder gaps at the top) ; otherwise the fixed surface level for the partial fill.
        const float yFill = full ? (cy - rr - 1.0f) : (cy + rr - 2.0f * rr * (pct / 100.0f));
        liquid_segment(dev, cx, cy, rr, yFill, col);     // smooth AA circular segment (no stepped edge)
        if (!full) level_line(dev, cx, cy, yFill, col, rr, 0);   // level line hugging the sphere (circle)
    }
    { u32 hi = 0x66FFFFFF; soft_blob(dev, cx - r * 0.34f, cy - r * 0.42f, r * 0.5f, r * 0.42f, hi); }  // top-left gloss
}

static void ring_sector(u32 dev, float cx, float cy, float rIn, float rOut, float a0, float a1, u32 c) {   // ANTI-ALIASED annulus sector
    cx -= 0.5f; cy -= 0.5f;
    const int M = 56; const float fth = 1.1f; const u32 c0 = c & 0x00FFFFFF;
    VtxC body[2 * (M + 1)], of[2 * (M + 1)], nf[2 * (M + 1)];
    for (int i = 0; i <= M; ++i) {
        float a = a0 + (a1 - a0) * (float)i / M, ca = cosf(a), sa = sinf(a);
        body[2*i]   = { cx + rOut*ca,          cy + rOut*sa,          0,1, c };
        body[2*i+1] = { cx + rIn*ca,           cy + rIn*sa,           0,1, c };
        of[2*i]     = { cx + rOut*ca,          cy + rOut*sa,          0,1, c };
        of[2*i+1]   = { cx + (rOut + fth)*ca,  cy + (rOut + fth)*sa,  0,1, c0 };   // outer feather -> transparent
        nf[2*i]     = { cx + rIn*ca,           cy + rIn*sa,           0,1, c };
        nf[2*i+1]   = { cx + (rIn - fth)*ca,   cy + (rIn - fth)*sa,   0,1, c0 };   // inner feather -> transparent
    }
    dDrawUP(dev, D3DPT_TRIANGLESTRIP, 2*M, body, sizeof(VtxC));
    dDrawUP(dev, D3DPT_TRIANGLESTRIP, 2*M, of,   sizeof(VtxC));
    dDrawUP(dev, D3DPT_TRIANGLESTRIP, 2*M, nf,   sizeof(VtxC));
}
// RING : a thin full-circle progress ring.
static void gauge_ring(u32 dev, float gx, float gy, float gw, float gh, float pct, u32 col, float t, float pulse, float danger) {
    const float cx = gx + gw * 0.5f, cy = gy + gh * 0.5f;
    float R = (gw < gh ? gw : gh) * 0.5f - 1.0f; if (R < 3.0f) R = 3.0f;
    const float rOut = R, rIn = R * 0.74f, TAU = 6.2831853f, a0 = -1.5708f;       // thin ring, start at the top
    { u32 gcol; float g; if (gauge_glow(col, t, pulse, danger, gcol, g)) ring_sector(dev, cx, cy, rIn - g, rOut + g, 0.0f, TAU, gcol); }   // ring-shaped glow
    ring_sector(dev, cx, cy, rIn, rOut, 0.0f, TAU, 0xFF0A0E1C);
    if (pct > 0.0f) {
        float a1 = a0 + TAU * (pct / 100.0f);
        ring_sector(dev, cx, cy, rIn, rOut, a0, a1, col);
        if (pct < 99.5f) {   // bright COLOURED radial marker at the fill tip (normal alpha -> keeps hue)
            const u32 me = (scl(col, 1.35f) & 0x00FFFFFF) | 0xFF000000; float da = 2.6f / rOut;
            ring_sector(dev, cx, cy, rIn, rOut, a1 - da, a1, me);
        }
    }
    { u32 hi = 0x40FFFFFF; soft_blob(dev, cx - R * 0.3f, cy - R * 0.35f, R * 0.5f, R * 0.4f, hi); }
}

// feather one straight edge (P->Q) outward along normal (nx,ny) : rim colour -> transparent = AA silhouette.
static void edge_feather(u32 dev, float ax, float ay, float bx, float by, float nx, float ny, u32 rim) {
    const float f = 1.3f; const u32 tr = rim & 0x00FFFFFF;
    VtxC v[4] = { { ax, ay, 0.0f, 1.0f, rim }, { ax + nx * f, ay + ny * f, 0.0f, 1.0f, tr },
                  { bx, by, 0.0f, 1.0f, rim }, { bx + nx * f, by + ny * f, 0.0f, 1.0f, tr } };
    dDrawUP(dev, D3DPT_TRIANGLESTRIP, 2, v, sizeof(VtxC));
}

// CRYSTAL : an angular diamond/gem filling bottom -> top (straight edges, feathered AA silhouette).
static void gauge_crystal(u32 dev, float gx, float gy, float gw, float gh, float pct, u32 col, float t, float pulse, float danger) {
    const float cx = gx + gw * 0.5f, cy = gy + gh * 0.5f;
    float R = (gw < gh ? gw : gh) * 0.5f - 1.0f; if (R < 3.0f) R = 3.0f;
    { u32 gcol; float g; if (gauge_glow(col, t, pulse, danger, gcol, g)) {   // diamond-shaped glow peeking behind
        float e = R + g;
        fill_tri(dev, cx, cy - e, cx + e, cy, cx - e, cy, gcol);
        fill_tri(dev, cx - e, cy, cx + e, cy, cx, cy + e, gcol);
    } }
    fill_tri(dev, cx, cy - R, cx + R, cy, cx - R, cy, 0xFF0A0E1C);      // dark glass body (diamond)
    fill_tri(dev, cx - R, cy, cx + R, cy, cx, cy + R, 0xFF0A0E1C);
    const float k = 0.70711f; const u32 rimC = 0xFF243150;             // AA silhouette : feather the 4 outer edges
    edge_feather(dev, cx, cy - R, cx + R, cy,  k, -k, rimC);
    edge_feather(dev, cx + R, cy, cx, cy + R,  k,  k, rimC);
    edge_feather(dev, cx, cy + R, cx - R, cy, -k,  k, rimC);
    edge_feather(dev, cx - R, cy, cx, cy - R, -k, -k, rimC);
    if (pct > 0.0f) {
        const float Rin = R - 1.0f;                                   // liquid stays 1px inside -> the rim frames it
        const float yFill = cy + Rin - 2.0f * Rin * (pct / 100.0f), yBot = cy + Rin;
        float hc = Rin - fabsf(yFill - cy); if (hc < 0.0f) hc = 0.0f;  // diamond half-width at the surface
        // VOLUMETRIC fill : per-vertex vertical shade (bright at the surface -> dark at the bottom tip)
        #define CS(yy) vshade(col, (yy), yFill, yBot, 1.14f, 0.52f)
        if (yFill >= cy) {                                            // lower half : straight triangle to the bottom tip
            fill_tri_c(dev, cx - hc, yFill, CS(yFill), cx + hc, yFill, CS(yFill), cx, yBot, CS(yBot));
        } else {                                                      // over half : pentagon (fan from the left surface point)
            fill_tri_c(dev, cx - hc, yFill, CS(yFill), cx + hc, yFill, CS(yFill), cx + Rin, cy, CS(cy));
            fill_tri_c(dev, cx - hc, yFill, CS(yFill), cx + Rin, cy, CS(cy),       cx, yBot, CS(yBot));
            fill_tri_c(dev, cx - hc, yFill, CS(yFill), cx, yBot, CS(yBot),         cx - Rin, cy, CS(cy));
        }
        #undef CS
        level_line(dev, cx, cy, yFill, col, Rin, 1);   // level line hugging the diamond
    }
    { u32 hi = 0x55FFFFFF; soft_blob(dev, cx - R * 0.28f, cy - R * 0.40f, R * 0.4f, R * 0.35f, hi); }
}

// SEGMENTED : a battery of blocks that light up left -> right.
static void gauge_segmented(u32 dev, float gx, float gy, float gw, float gh, float pct, u32 col, float t, float pulse, float danger) {
    gauge_aura_soft(dev, gx, gy, gw, gh, col, t, pulse, danger);
    const int N = 5; const float g = 1.5f; float segW = (gw - (N - 1) * g) / (float)N; if (segW < 2.0f) segW = 2.0f;
    const float r = gh * 0.22f, ih = gh - 2.0f;
    for (int k = 0; k < N; ++k) {
        float x = gx + k * (segW + g);
        rrnd(dev, x, gy, segW, gh, r, 0xFF1B2540);                 // cell rim
        rrnd(dev, x + 1, gy + 1, segW - 2, gh - 2, r - 1.0f, 0xFF080B16);   // cell bore
        float lit = (pct / 100.0f) * N - k; if (lit < 0.0f) lit = 0.0f; if (lit > 1.0f) lit = 1.0f;
        if (lit > 0.01f) {
            float fw = (segW - 2) * lit;
            vgrad(dev, x + 1, gy + 1,            fw, ih * 0.5f, lt(col, 0.25f), col);
            vgrad(dev, x + 1, gy + 1 + ih * 0.5f, fw, ih * 0.5f, col, scl(col, 0.6f));
            vgrad(dev, x + 1, gy + 1,            fw, ih * 0.44f, 0x66FFFFFF, 0x00FFFFFF);
            if (lit < 0.999f) level_line_v(dev, x + 1 + fw, gy + 1, ih, col);   // fiole-style level line on the partial cell
        }
    }
}

// MINIMAL : the big value number is the star ; a thin underline bar shows the level.
static void gauge_minimal(u32 dev, float gx, float gy, float gw, float gh, float pct, u32 col, float t, float pulse, float danger) {
    float bh = gh * 0.18f; if (bh < 2.0f) bh = 2.0f;
    const float y = gy + gh - bh - 1.0f, r = bh * 0.5f;
    gauge_aura_soft(dev, gx, y, gw, bh, col, t, pulse, danger);    // rounded-rect border hugging the thin bar
    rrnd(dev, gx, y, gw, bh, r, 0xFF10182B);                       // track
    float fw = gw * (pct / 100.0f);
    if (fw > 1.0f) {
        const bool full = fw >= gw - 1.0f;
        if (full) rrnd(dev, gx, y, fw, bh, r, col);                   // full : rounded both ends (matches track)
        else { rrect_left(dev, gx, y, fw, bh, r, col, col); level_line_v(dev, gx + fw, y, bh, col); }   // partial : flat tip for the level line
    }
}

void party_gauge(u32 dev, float gx, float gy, float gw, float gh, float pct, u32 col, float t, float pulse, float danger, int kind, int style) {   // exposed for the Help live samples
    // untextured colour-quad state : the party rows draw gauges BEFORE any text, but the Help draws them
    // AFTER text (font texture still bound + MODULATE) -> reset so the rounded/gradient quads render right.
    setup_color_state(dev);   // the shared colour-quad state (superset of the old inline reset : also 2D-safe)
    if (pct < 0) pct = 0; if (pct > 100) pct = 100;
    // gauge shape (config) : 0 = Vial (real fiole assets), 1 = Sphere, 2 = Radial. The classic bar below is
    // kept only as the automatic fallback for Vial before the fiole textures are resident.
    if (kind >= 0) {
        switch (style) {   // per-box gauge style : 0 Vial, 1 Bars, 2 Segments, 3 Minimal, 4 Sphere, 5 Ring, 6 Crystal, 7 Text
            case 7: return;                 // Text style : no shape at all -> the value number (drawn later) carries the colour + animation
            case 2: gauge_segmented(dev, gx, gy, gw, gh, pct, col, t, pulse, danger); return;
            case 3: gauge_minimal  (dev, gx, gy, gw, gh, pct, col, t, pulse, danger); return;
            case 4: gauge_sphere   (dev, gx, gy, gw, gh, pct, col, t, pulse, danger); return;
            case 5: gauge_ring     (dev, gx, gy, gw, gh, pct, col, t, pulse, danger); return;
            case 6: gauge_crystal  (dev, gx, gy, gw, gh, pct, col, t, pulse, danger); return;
            case 1: break;                  // classic bar (drawn below)
            default: {                      // 0 = Vial : real fiole assets, bar fallback until resident
                LiquidBars* p = vial_provider();
                if (p && p->vial_ready()) { p->draw_vial_scaled(dev, t, gx, gy, gw, gh, kind, pct / 100.0f, col, pulse, danger, 4); return; }
            }
        }
    }
    float r = gh * 0.5f;                                  // full capsule -> genuinely ROUND ends

    gauge_aura_soft(dev, gx, gy, gw, gh, col, t, pulse, danger);   // adapted glow : soft radial, no box frame

    // track : OPAQUE rounded rim + recessed bg (clean AA corners) + subtle inner shading
    rrnd(dev, gx, gy, gw, gh, r, 0xFF243150);                          // rim
    rrnd(dev, gx + 1, gy + 1, gw - 2, gh - 2, r - 1.0f, 0xFF0A0E1C);   // dark recessed bg
    if (gw > 2.0f * r) vgrad(dev, gx + r, gy + 2, gw - 2 * r, gh - 3, 0xFF0F1525, 0xFF05080F);   // vertical depth in the middle

    // liquid fill : a rounded CAPSULE that grows, drawn EDGE-TO-EDGE (full height, over the rim) so the rim's
    // 1px border never shows as a dark line across the top/bottom of the FLUID -- it stays only on the empty part.
    const float innerW = gw, fillW = innerW * pct / 100.0f, fh = gh;
    if (fillW >= 1.0f) {
        float b = 1.0f + 0.34f * pulse * sinf(t * 9.4f);
        u32 c = scl(col, b > 1.6f ? 1.6f : (b < 0.5f ? 0.5f : b));
        const float fx = gx, fy = gy, fr = r;
        const bool  full = fillW >= innerW - 0.5f;   // full : both ends rounded (matches the track) ; partial : FLAT right for the level line
        if (full) rrect     (dev, fx, fy, fillW, fh, fr, lt(c, 0.22f), scl(c, 0.6f));      // rounded both ends
        else      rrect_left(dev, fx, fy, fillW, fh, fr, lt(c, 0.22f), scl(c, 0.6f));      // rounded LEFT, flat right (the level)
        if (fillW > 2.0f * fr) vgrad(dev, fx + fr, fy, fillW - 2.0f * fr, fh * 0.46f, 0x66FFFFFF, 0x00FFFFFF);   // gloss, inset to stay inside the round end
        if (danger > 0.0f) {                                          // red wash so the fill visibly blinks
            float dl = 0.5f + 0.5f * sinf(t * 7.5f);
            u32 dw = 0x00FF1E1E | ((u32)(dl * danger * 0.55f * 255) << 24);
            if (full) rrect(dev, fx, fy, fillW, fh, fr, dw, dw); else rrect_left(dev, fx, fy, fillW, fh, fr, dw, dw);   // wash matches the shape
        }
        if (!full) level_line_v(dev, fx + fillW, fy, fh, col);   // fiole-style level line at the FLAT tip
    }
}

// The REAL selection hand (icon_tex passed in so it stays device-loss managed by the Party widget),
// exposed for the Help live samples. sub = true tints it blue (sub-target), else white (main target).
void party_cursor(u32 dev, u32 tex, float cx, float cy, float size, int mode) {
    if (!tex) return;
    setup_tex_state(dev, tex);
    const u32 rgb  = (mode == 1) ? 0x002E9CFFu : (mode == 2) ? 0x00FF4030u : 0x00FFFFFFu;   // blue sub / red locked / white main (matches the live cursor)
    const u32 tint = 0xFF000000u | rgb;
    glow_quad(dev, cx - size * 0.5f, cy - size * 0.5f, size, size, tint);
    dSetTex(dev, 0, 0);
    dSetTSS(dev, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1); dSetTSS(dev, 0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);   // restore the untextured pipeline for the primitives that follow
    dSetTSS(dev, 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1); dSetTSS(dev, 0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
}

// The EXACT selection frame the party draws : gold glass fill + the moving glass sweep + darkened rims
// for the main target, ocean-blue fill + a stronger blue sweep for a sub-target. Exposed so the Help
// shows the identical look/rhythm. t = time (drives the sweep on the same 1.4s cycle) ; alpha 0..1.
void party_selframe(u32 dev, float x, float y, float w, float h, float t, float alpha, int mode) {
    setup_color_state(dev);
    const float a = alpha < 0.0f ? 0.0f : (alpha > 1.0f ? 1.0f : alpha);
    const float gSwT = t > 0.0f ? t : 0.0f;
    const float gSweep = gSwT / 1.4f - (float)(int)(gSwT / 1.4f);   // 0..1 sweep phase, same as the live rows
    if (mode == 1) {                                                // sub-target : ocean blue, no rims
        const u32 fillT = ((u32)(0x80 * a) << 24) | 0x00159CFF;     // vivid ocean blue
        const u32 fillB = ((u32)(0x34 * a) << 24) | 0x00064FB0;     // deeper toward the bottom
        vgrad(dev, x, y, w, h, fillT, fillB);
        shine_sweep(dev, x, y, w, h, gSweep, 0x00BFEFFF, 0.80f * a);
    } else {                                                        // main (gold) or locked (red) : glass fill + sweep + curved rims
        const bool locked = (mode == 2);                           // red when the target is LOCKED (matches the live frame)
        const u32 fillT = locked ? (((u32)(0x52 * a) << 24) | 0x00FF6A5A) : (((u32)(0x3C * a) << 24) | 0x00FFE08A);
        const u32 fillB = locked ? (((u32)(0x24 * a) << 24) | 0x00D83028) : (((u32)(0x16 * a) << 24) | 0x00FFC850);
        vgrad(dev, x, y, w, h, fillT, fillB);
        shine_sweep(dev, x, y, w, h, gSweep, locked ? 0x00FFD5C8 : 0x00FFFFFF, 0.65f * a);
        const float rw = w * 0.085f;                                // curved rims : darken the far edges (lens bulge)
        const u32 rO = ((u32)(0x3E * a) << 24), rI = 0x00000000;
        grad_quad(dev, x,          y, rw, h, rO, rI, rO, rI);       // left rim
        grad_quad(dev, x + w - rw, y, rw, h, rI, rO, rI, rO);       // right rim
    }
}

// The cursor's horizontal bob offset (px) for a given time + icon size, matching the live rhythm
// (bob = 1.5*S*sin, over an icon ~= mainBand*1.3*S -> about 6% of the icon size, on the same 4.6 clock).
float party_cursor_bob(float t, float size) {
    const float gPulseT = t - 0.35f;
    const float gPulse  = gPulseT > 0.0f ? sinf(gPulseT * 4.6f) : 0.0f;
    return size * 0.06f * gPulse;
}

} // namespace aio
