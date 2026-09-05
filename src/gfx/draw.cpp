// draw.cpp -- see draw.h.
#include "draw.h"
#include "color.h"   // lerp_argb (one implementation, shared)
#include "gfx/corner_mask.h"   // the baked quarter-disc coverage masks (rrect_topcaps prefers them)
#include <math.h>

namespace aio {

void tquad(u32 dev, float x, float y, float w, float h,
           float u0, float u1, float v0, float v1, u32 cL, u32 cR)
{
    x -= 0.5f; y -= 0.5f;                            // D3D half-texel rule: align texels to pixels
    Vtx q[4] = {
        { x,     y,     0, 1, cL, u0, v0 },
        { x + w, y,     0, 1, cR, u1, v0 },
        { x,     y + h, 0, 1, cL, u0, v1 },
        { x + w, y + h, 0, 1, cR, u1, v1 },
    };
    dDrawUP(dev, D3DPT_TRIANGLESTRIP, 2, q, sizeof(Vtx));
}

// full-texture quad of size w x h, ROTATED `ang` radians about its centre (cx,cy), tinted `col`. For
// directional markers (rotate an up/right-pointing icon by the entity heading + tint via MODULATE).
void tquad_rot(u32 dev, float cx, float cy, float w, float h, float ang, u32 col)
{
    const float ca = cosf(ang), sa = sinf(ang), hx = w * 0.5f, hy = h * 0.5f;
    #define RX(lx,ly) (cx + (lx)*ca - (ly)*sa - 0.5f)
    #define RY(lx,ly) (cy + (lx)*sa + (ly)*ca - 0.5f)
    Vtx q[4] = {
        { RX(-hx,-hy), RY(-hx,-hy), 0, 1, col, 0.0f, 0.0f },
        { RX( hx,-hy), RY( hx,-hy), 0, 1, col, 1.0f, 0.0f },
        { RX(-hx, hy), RY(-hx, hy), 0, 1, col, 0.0f, 1.0f },
        { RX( hx, hy), RY( hx, hy), 0, 1, col, 1.0f, 1.0f },
    };
    #undef RX
    #undef RY
    dDrawUP(dev, D3DPT_TRIANGLESTRIP, 2, q, sizeof(Vtx));
}

// filled TEXTURED circle (triangle fan), centred at (cx,cy) radius r, tinted `col` (MODULATE). Each vertex's
// UV is the linear map (u0 + (vx-cx)*du, v0 + (vy-cy)*dv) -> the caller supplies the screen->UV mapping so a
// map quad can be drawn as a GENUINE circle (round minimap) with NO stencil buffer. Centre UV = (u0,v0).
void tdisc(u32 dev, float cx, float cy, float r, float u0, float v0, float du, float dv, u32 col)
{
    const int MAXN = 160;
    int N = (int)(r * 1.3f); if (N < 48) N = 48; if (N > MAXN) N = MAXN;   // segments scale with radius -> a big radar lens stays perfectly round (no facets at the brass edge)
    const float fth = 1.3f;                             // feathered outer rim width -> anti-aliased circular edge
    Vtx fan[MAXN + 2];
    fan[0] = { cx - 0.5f, cy - 0.5f, 0, 1, col, u0, v0 };
    for (int i = 0; i <= N; ++i) {
        const float a = (float)i / (float)N * 6.28318530f;
        const float vx = cx + r * cosf(a), vy = cy + r * sinf(a);
        fan[i + 1] = { vx - 0.5f, vy - 0.5f, 0, 1, col, u0 + (vx - cx) * du, v0 + (vy - cy) * dv };
    }
    dDrawUP(dev, D3DPT_TRIANGLEFAN, N, fan, sizeof(Vtx));
    // feathered rim : a ring from r (full alpha) to r+fth (alpha 0) so the circular edge fades cleanly (AA)
    // into whatever sits beneath (the minimap's border ring) instead of a hard polygonal edge.
    const u32 col0 = col & 0x00FFFFFFu;
    Vtx ring[(MAXN + 1) * 2];
    for (int i = 0; i <= N; ++i) {
        const float a = (float)i / (float)N * 6.28318530f, ca = cosf(a), sa = sinf(a);
        const float ix = cx + r * ca, iy = cy + r * sa, ox = cx + (r + fth) * ca, oy = cy + (r + fth) * sa;
        ring[i * 2 + 0] = { ix - 0.5f, iy - 0.5f, 0, 1, col,  u0 + (ix - cx) * du, v0 + (iy - cy) * dv };
        ring[i * 2 + 1] = { ox - 0.5f, oy - 0.5f, 0, 1, col0, u0 + (ox - cx) * du, v0 + (oy - cy) * dv };
    }
    dDrawUP(dev, D3DPT_TRIANGLESTRIP, N * 2, ring, sizeof(Vtx));
}

void tquad_v(u32 dev, float x, float y, float w, float h,
             float u0, float u1, float v0, float v1, u32 cTop, u32 cBot)
{
    x -= 0.5f; y -= 0.5f;
    Vtx q[4] = {
        { x,     y,     0, 1, cTop, u0, v0 },
        { x + w, y,     0, 1, cTop, u1, v0 },
        { x,     y + h, 0, 1, cBot, u0, v1 },
        { x + w, y + h, 0, 1, cBot, u1, v1 },
    };
    dDrawUP(dev, D3DPT_TRIANGLESTRIP, 2, q, sizeof(Vtx));
}

void tquad4(u32 dev, float x, float y, float w, float h,
            float u0, float u1, float v0, float v1,
            u32 cTL, u32 cTR, u32 cBL, u32 cBR)
{
    x -= 0.5f; y -= 0.5f;
    Vtx q[4] = {
        { x,     y,     0, 1, cTL, u0, v0 },
        { x + w, y,     0, 1, cTR, u1, v0 },
        { x,     y + h, 0, 1, cBL, u0, v1 },
        { x + w, y + h, 0, 1, cBR, u1, v1 },
    };
    dDrawUP(dev, D3DPT_TRIANGLESTRIP, 2, q, sizeof(Vtx));
}

void glow_quad(u32 dev, float x, float y, float w, float h, u32 color)
{
    x -= 0.5f; y -= 0.5f;
    Vtx q[4] = {
        { x,     y,     0, 1, color, 0.0f, 0.0f },
        { x + w, y,     0, 1, color, 1.0f, 0.0f },
        { x,     y + h, 0, 1, color, 0.0f, 1.0f },
        { x + w, y + h, 0, 1, color, 1.0f, 1.0f },
    };
    dDrawUP(dev, D3DPT_TRIANGLESTRIP, 2, q, sizeof(Vtx));
}

void cap_quad(u32 dev, float x, float y, float w, float h, bool flip)
{
    x -= 0.5f; y -= 0.5f;
    float u0 = flip ? 1.0f : 0.0f, u1 = flip ? 0.0f : 1.0f;
    Vtx q[4] = {
        { x,     y,     0, 1, 0xFFFFFFFF, u0, 0.0f },
        { x + w, y,     0, 1, 0xFFFFFFFF, u1, 0.0f },
        { x,     y + h, 0, 1, 0xFFFFFFFF, u0, 1.0f },
        { x + w, y + h, 0, 1, 0xFFFFFFFF, u1, 1.0f },
    };
    dDrawUP(dev, D3DPT_TRIANGLESTRIP, 2, q, sizeof(Vtx));
}

void grad_quad(u32 dev, float x, float y, float w, float h, u32 cTL, u32 cTR, u32 cBL, u32 cBR)
{
    x -= 0.5f; y -= 0.5f;                            // D3D half-pixel rule (avoids dropped bottom/right row)
    VtxC q[4] = {
        { x,     y,     0, 1, cTL },
        { x + w, y,     0, 1, cTR },
        { x,     y + h, 0, 1, cBL },
        { x + w, y + h, 0, 1, cBR },
    };
    dDrawUP(dev, D3DPT_TRIANGLESTRIP, 2, q, sizeof(VtxC));
}

void fill_tri(u32 dev, float x0, float y0, float x1, float y1, float x2, float y2, u32 col)
{
    VtxC v[3] = {
        { x0 - 0.5f, y0 - 0.5f, 0, 1, col },
        { x1 - 0.5f, y1 - 0.5f, 0, 1, col },
        { x2 - 0.5f, y2 - 0.5f, 0, 1, col },
    };
    dDrawUP(dev, D3DPT_TRIANGLEFAN, 1, v, sizeof(VtxC));   // 3 verts = 1 triangle
}

// an ANTI-ALIASED filled CONVEX polygon (`n` points, 3..12) : solid centroid-fan core + a feathered skirt
// (each vertex extruded outward from the centroid, alpha 0 at the rim) -> crisp AA silhouette on triangles,
// diamonds and chevrons, the same recipe as disc(). Call in the colour-quad state. Points in CW or CCW order.
void fill_poly_aa(u32 dev, const float* xy, int n, u32 col)
{
    if (n < 3 || n > 12) return;
    float cx = 0.0f, cy = 0.0f;
    for (int i = 0; i < n; ++i) { cx += xy[2 * i]; cy += xy[2 * i + 1]; }
    cx /= (float)n; cy /= (float)n;
    const u32 c0 = col & 0x00FFFFFFu; const float f = 1.2f;
    VtxC fan[14];                                         // centroid + n verts + closing repeat = n+2 verts -> n tris
    fan[0] = { cx - 0.5f, cy - 0.5f, 0, 1, col };
    for (int i = 0; i <= n; ++i) { const int k = i % n; fan[i + 1] = { xy[2 * k] - 0.5f, xy[2 * k + 1] - 0.5f, 0, 1, col }; }
    dDrawUP(dev, D3DPT_TRIANGLEFAN, n, fan, sizeof(VtxC));
    VtxC ring[2 * 13];                                    // feathered skirt : vertex (solid) -> vertex + outward*f (alpha 0)
    for (int i = 0; i <= n; ++i) {
        const int k = i % n;
        float ox = xy[2 * k] - cx, oy = xy[2 * k + 1] - cy, len = sqrtf(ox * ox + oy * oy); if (len < 0.001f) len = 1.0f;
        const float nx = ox / len, ny = oy / len;
        ring[2 * i]     = { xy[2 * k] - 0.5f,          xy[2 * k + 1] - 0.5f,          0, 1, col };
        ring[2 * i + 1] = { xy[2 * k] - 0.5f + nx * f, xy[2 * k + 1] - 0.5f + ny * f, 0, 1, c0  };
    }
    dDrawUP(dev, D3DPT_TRIANGLESTRIP, 2 * n, ring, sizeof(VtxC));
}

// feather the OUTER ARC (radius r, angles a0..a1, `seg` facets) : rim colour at r -> transparent at r+1.1px,
// an AA curved edge WITHOUT filling the interior. Call right after a solid quarter-disc fan sampled at the
// SAME segment count / angles so the feather sits exactly on the facet edge (rounded-rect corners).
void arc_feather(u32 dev, float cx, float cy, float r, float a0, float a1, int seg, u32 col)
{
    if (seg < 1) seg = 1; if (seg > 32) seg = 32;
    cx -= 0.5f; cy -= 0.5f;
    const float f = 1.1f; const u32 c0 = col & 0x00FFFFFFu;
    VtxC rim[2 * (32 + 1)];
    for (int i = 0; i <= seg; ++i) {
        const float a = a0 + (a1 - a0) * (float)i / (float)seg, ca = cosf(a), sa = sinf(a);
        rim[2 * i]     = { cx + r * ca,           cy + r * sa,           0, 1, col };
        rim[2 * i + 1] = { cx + (r + f) * ca,     cy + (r + f) * sa,     0, 1, c0  };
    }
    dDrawUP(dev, D3DPT_TRIANGLESTRIP, 2 * seg, rim, sizeof(VtxC));
}

void seg_soft(u32 dev, float ax, float ay, float bx, float by, float th, u32 col)
{
    ax -= 0.5f; ay -= 0.5f; bx -= 0.5f; by -= 0.5f;
    float dx = bx - ax, dy = by - ay;
    float l = sqrtf(dx * dx + dy * dy); if (l < 0.01f) return;
    float nx = -dy / l * th * 0.5f, ny = dx / l * th * 0.5f;
    u32 c0 = col & 0x00FFFFFF;                                   // transparent at the edges
    VtxC q[6] = {
        { ax + nx, ay + ny, 0, 1, c0 },
        { bx + nx, by + ny, 0, 1, c0 },
        { ax,      ay,      0, 1, col },
        { bx,      by,      0, 1, col },
        { ax - nx, ay - ny, 0, 1, c0 },
        { bx - nx, by - ny, 0, 1, c0 },
    };
    dDrawUP(dev, D3DPT_TRIANGLESTRIP, 4, q, sizeof(VtxC));       // 6 verts = 4 tris (two feathered halves)
}

void disc(u32 dev, float cx, float cy, float r, u32 col)
{
    cx -= 0.5f; cy -= 0.5f;                          // D3D half-pixel rule
    const int MAXN = 96;
    int N = (int)(r * 1.15f); if (N < 24) N = 24; if (N > MAXN) N = MAXN;   // segments scale with radius -> big discs stay ROUND (no facets)
    const float f = 1.3f;                           // feather width -> anti-aliased edge
    const u32 c0 = col & 0x00FFFFFF;                // same colour, alpha 0
    // solid core (triangle fan)
    VtxC fan[MAXN + 2];
    fan[0] = { cx, cy, 0, 1, col };
    for (int i = 0; i <= N; ++i) {
        float a = (float)i / N * 6.2831853f;
        fan[i + 1] = { cx + r * cosf(a), cy + r * sinf(a), 0, 1, col };
    }
    dDrawUP(dev, D3DPT_TRIANGLEFAN, N, fan, sizeof(VtxC));
    // feathered rim (triangle strip): opaque at r -> transparent at r+f
    VtxC ring[2 * (MAXN + 1)];
    for (int i = 0; i <= N; ++i) {
        float a = (float)i / N * 6.2831853f, ca = cosf(a), sa = sinf(a);
        ring[2 * i]     = { cx + r * ca,       cy + r * sa,       0, 1, col };
        ring[2 * i + 1] = { cx + (r + f) * ca, cy + (r + f) * sa, 0, 1, c0  };
    }
    dDrawUP(dev, D3DPT_TRIANGLESTRIP, 2 * N, ring, sizeof(VtxC));
}

static const float PI_ = 3.14159265f;

// lerp_argb (per-channel ARGB interpolation, used for the vertical gradient across the rounded rect)
// lives in color.h -> one implementation shared with the rest of the renderer.

// a plain vertical-gradient rect (raw verts, half-pixel already applied by the caller).
static void vrect_raw(u32 dev, float x, float y, float w, float h, u32 cT, u32 cB)
{
    if (w <= 0.0f || h <= 0.0f) return;
    VtxC q[4] = {
        { x,     y,     0, 1, cT },
        { x + w, y,     0, 1, cT },
        { x,     y + h, 0, 1, cB },
        { x + w, y + h, 0, 1, cB },
    };
    dDrawUP(dev, D3DPT_TRIANGLESTRIP, 2, q, sizeof(VtxC));
}

// The BAKED-MASK path for a fully rounded rect -- the four-corner sibling of topcaps_masked above, and the
// same deal: real coverage in the corners, crisp pixel-aligned straight edges, no feather to be inconsistent
// about. Returns false when the mask cannot be used, and the caller then draws the geometry exactly as before.
//
// TWO GUARDS, both of them load-bearing:
//   * feather > 0 is checked by the CALLER. An rrect with feather 0 is a STENCIL MASK (d3d8-rendering 10c),
//     and during a mask pass alpha-test is off and colour-write is off -- a textured quad would write stencil
//     across its whole rectangle regardless of the mask's alpha, squaring off every rounded clip in the HUD.
//   * the rect must ALREADY be on the pixel grid. One texel per pixel is the entire point, and it means
//     nothing on a half-pixel. Snapping it here instead would be worse than not helping: a gauge fill whose
//     width slides smoothly would start stepping a pixel at a time. Aligned callers (all the UI chrome) get
//     the mask; smoothly-animated sub-pixel geometry keeps the feather it was designed with.
static bool px_aligned(float v) { float d = v - (float)(int)(v < 0.0f ? v - 0.5f : v + 0.5f); return d < 0.02f && d > -0.02f; }

static bool rrect_masked(u32 dev, float x, float y, float w, float h, int r, u32 cTop, u32 cBot)
{
    if (!corner_mask_enabled()) return false;
    if (r < CM_RMIN || r > CM_RMAX) return false;
    if (!px_aligned(x) || !px_aligned(y) || !px_aligned(w) || !px_aligned(h)) return false;
    const float R = (float)r;
    if (w < 2.0f * R || h < 2.0f * R) return false;
    float u0, v0, u1, v1;
    if (!corner_mask_uv(r, u0, v0, u1, v1)) return false;
    const u32 tex = corner_mask_tex(dev);
    if (!tex) return false;

    const u32 cA = lerp_argb(cTop, cBot, R / h);          // the gradient where the top arcs end
    const u32 cB = lerp_argb(cTop, cBot, (h - R) / h);    // ... and where the bottom ones begin

    // --- the straight body : three crisp rects, no feather ---
    vrect_raw(dev, x + R - 0.5f,     y - 0.5f,     w - 2.0f * R, h,            cTop, cBot);   // centre column, full height
    vrect_raw(dev, x - 0.5f,         y + R - 0.5f, R,            h - 2.0f * R, cA,   cB);     // left band, between the arcs
    vrect_raw(dev, x + w - R - 0.5f, y + R - 0.5f, R,            h - 2.0f * R, cA,   cB);     // right band

    // --- the four arcs : ONE baked block, mirrored in u and/or v ---
    dTexQuadState(dev, tex);
    const float X0 = x - 0.5f, X1 = x + R - 0.5f, X2 = x + w - R - 0.5f, X3 = x + w - 0.5f;
    const float Y0 = y - 0.5f, Y1 = y + R - 0.5f, Y2 = y + h - R - 0.5f, Y3 = y + h - 0.5f;
    struct C { float x0, x1, y0, y1; float ua, ub, va, vb; u32 ct, cb; };
    const C cs4[4] = {
        { X0, X1, Y0, Y1, u0, u1, v0, v1, cTop, cA },   // TL : as baked
        { X2, X3, Y0, Y1, u1, u0, v0, v1, cTop, cA },   // TR : u mirrored
        { X0, X1, Y2, Y3, u0, u1, v1, v0, cB,   cBot }, // BL : v mirrored
        { X2, X3, Y2, Y3, u1, u0, v1, v0, cB,   cBot }, // BR : both
    };
    for (int k = 0; k < 4; ++k) {
        const C& c = cs4[k];
        Vtx q[4] = {
            { c.x0, c.y0, 0, 1, c.ct, c.ua, c.va },
            { c.x1, c.y0, 0, 1, c.ct, c.ub, c.va },
            { c.x0, c.y1, 0, 1, c.cb, c.ua, c.vb },
            { c.x1, c.y1, 0, 1, c.cb, c.ub, c.vb },
        };
        dDrawUP(dev, D3DPT_TRIANGLESTRIP, 2, q, sizeof(Vtx));
    }
    dColorQuadState(dev);   // never leave a bound texture for the next widget (rule 8)
    return true;
}

void rrect(u32 dev, float x, float y, float w, float h, float r, u32 cTop, u32 cBot, float feather)
{
    if (w <= 0.0f || h <= 0.0f) return;
    x -= 0.5f; y -= 0.5f;                                        // D3D half-pixel rule (applied ONCE here)
    if (r > w * 0.5f) r = w * 0.5f;
    if (r > h * 0.5f) r = h * 0.5f;
    // The BAKED corner masks first -- strictly better where they apply. feather > 0 is part of the gate : a
    // feather-0 rrect is a stencil MASK, and a textured quad would write that mask square (see rrect_masked).
    if (feather > 0.0f && rrect_masked(dev, x + 0.5f, y + 0.5f, w, h, (int)(r + 0.5f), cTop, cBot)) return;
    // colour at any scanline y' (linear top->bottom across the whole box height)
    #define CY(yy) lerp_argb(cTop, cBot, ((yy) - y) / h)
    if (r < 0.75f && feather <= 0.0f) {                          // no radius, no feather -> a plain crisp rect
        vrect_raw(dev, x, y, w, h, cTop, cBot);
    } else {
        if (r < 0.75f) { r = 0.75f; if (r > w * 0.5f) r = w * 0.5f; if (r > h * 0.5f) r = h * 0.5f; }   // feather asked with ~no radius -> a hair of corner so the rounded path's CONSISTENT edge+corner feather applies (rule 2)
        // --- solid interior : centre column full-height + the two side bands + 4 quarter-disc corners ---
        vrect_raw(dev, x + r,     y,     w - 2 * r, h,         cTop,       cBot);          // centre column
        vrect_raw(dev, x,         y + r, r,         h - 2 * r, CY(y + r),  CY(y + h - r)); // left band
        vrect_raw(dev, x + w - r, y + r, r,         h - 2 * r, CY(y + r),  CY(y + h - r)); // right band
        const int NcMax = 24;                                   // stack-array bound (raised : big circles/discs -- e.g. the minimap brass lens -- were faceted at 14/corner)
        int Nc = (int)(r * 0.7f); if (Nc < 6) Nc = 6; if (Nc > NcMax) Nc = NcMax;   // more segments on bigger radii -> rounder
        struct Corner { float cx, cy, a0; };
        const Corner cs4[4] = {
            { x + r,     y + r,     PI_        },   // TL : 180 -> 270
            { x + w - r, y + r,     1.5f * PI_ },   // TR : 270 -> 360
            { x + w - r, y + h - r, 0.0f       },   // BR :   0 ->  90
            { x + r,     y + h - r, 0.5f * PI_ },   // BL :  90 -> 180
        };
        for (int k = 0; k < 4; ++k) {                            // ONE triangle-fan per corner (was Nc draws each)
            const float ccx = cs4[k].cx, ccy = cs4[k].cy, a0 = cs4[k].a0;
            VtxC fan[NcMax + 2];
            fan[0] = { ccx, ccy, 0, 1, CY(ccy) };                // fan centre
            for (int i = 0; i <= Nc; ++i) {
                const float a = a0 + (0.5f * PI_) * (float)i / (float)Nc;
                const float vx = ccx + r * cosf(a), vy = ccy + r * sinf(a);
                fan[i + 1] = { vx, vy, 0, 1, CY(vy) };
            }
            dDrawUP(dev, D3DPT_TRIANGLEFAN, Nc, fan, sizeof(VtxC));   // Nc triangles, one submit
        }
        // --- feather ALL edges + corners CONSISTENTLY, so the silhouette extends by the same `feather`
        // everywhere. (If only the corners feathered, the rounded ends would reach ~1px further than the
        // straight centre -> the centre looks 1px short and the backdrop shows as a thin line there.) ---
        if (feather > 0.0f) {
            const float f = feather;
            { const u32 tA = cTop & 0x00FFFFFF;                                       // top edge : y -> y-f (alpha 0)
              VtxC T[4] = { { x + r, y - f, 0,1, tA }, { x + w - r, y - f, 0,1, tA },
                            { x + r, y,     0,1, cTop }, { x + w - r, y,     0,1, cTop } };
              dDrawUP(dev, D3DPT_TRIANGLESTRIP, 2, T, sizeof(VtxC)); }
            { const u32 bA = cBot & 0x00FFFFFF;                                       // bottom edge : y+h -> y+h+f
              VtxC B[4] = { { x + r, y + h,     0,1, cBot }, { x + w - r, y + h,     0,1, cBot },
                            { x + r, y + h + f, 0,1, bA },   { x + w - r, y + h + f, 0,1, bA } };
              dDrawUP(dev, D3DPT_TRIANGLESTRIP, 2, B, sizeof(VtxC)); }
            { const u32 c0 = CY(y + r), c1 = CY(y + h - r);                           // left + right edges
              VtxC L[4] = { { x - f, y + r, 0,1, c0 & 0x00FFFFFF }, { x, y + r, 0,1, c0 },
                            { x - f, y + h - r, 0,1, c1 & 0x00FFFFFF }, { x, y + h - r, 0,1, c1 } };
              dDrawUP(dev, D3DPT_TRIANGLESTRIP, 2, L, sizeof(VtxC));
              VtxC R[4] = { { x + w, y + r, 0,1, c0 }, { x + w + f, y + r, 0,1, c0 & 0x00FFFFFF },
                            { x + w, y + h - r, 0,1, c1 }, { x + w + f, y + h - r, 0,1, c1 & 0x00FFFFFF } };
              dDrawUP(dev, D3DPT_TRIANGLESTRIP, 2, R, sizeof(VtxC)); }
            // 4 corner arcs : radius r (full) -> r+f (alpha 0)
            for (int k = 0; k < 4; ++k) {
                const float ccx = cs4[k].cx, ccy = cs4[k].cy, a0 = cs4[k].a0;
                VtxC ring[2 * (NcMax + 1)];
                for (int i = 0; i <= Nc; ++i) {
                    const float a = a0 + (0.5f * PI_) * (float)i / (float)Nc, ca = cosf(a), sa = sinf(a);
                    const u32 ci = CY(ccy + r * sa);
                    ring[2 * i]     = { ccx + r * ca,       ccy + r * sa,       0, 1, ci };
                    ring[2 * i + 1] = { ccx + (r + f) * ca, ccy + (r + f) * sa, 0, 1, ci & 0x00FFFFFF };
                }
                dDrawUP(dev, D3DPT_TRIANGLESTRIP, 2 * Nc, ring, sizeof(VtxC));
            }
        }
    }
    #undef CY
}

void rrect_bordered(u32 dev, float x, float y, float w, float h, float r,
                    u32 cTop, u32 cBot, u32 border, float bt, float feather)
{
    rrect(dev, x, y, w, h, r, border, border, feather);
    const float ir = (r - bt > 0.0f) ? r - bt : 0.0f;
    rrect(dev, x + bt, y + bt, w - 2 * bt, h - 2 * bt, ir, cTop, cBot, feather);
}

// D3D8 stencil render states not in the d3d.h enum (passed as raw ids to dSetRS).
enum { RS_STENCILENABLE = 52, RS_STENCILFAIL = 53, RS_STENCILZFAIL = 54, RS_STENCILPASS = 55,
       RS_STENCILFUNC = 56, RS_STENCILREF = 57, RS_STENCILMASK = 58, RS_STENCILWRITEMASK = 59 };
void rrect_clip_begin(u32 dev, float x, float y, float w, float h, float r) {
    dSetVS(dev, FVF_XYZRHW_DIFFUSE); dSetTex(dev, 0, 0);
    dSetRS(dev, D3DRS_ALPHATESTENABLE, 0);
    dSetRS(dev, D3DRS_ALPHABLENDENABLE, 0);
    dSetRS(dev, RS_STENCILENABLE, 1);
    dSetRS(dev, RS_STENCILMASK, 0xFF); dSetRS(dev, RS_STENCILWRITEMASK, 0xFF);
    dSetRS(dev, D3DRS_COLORWRITEENABLE, 0);                                   // mask pass : stencil only, no colour
    dSetRS(dev, RS_STENCILFUNC, 8);                                          // ALWAYS
    dSetRS(dev, RS_STENCILFAIL, 3); dSetRS(dev, RS_STENCILZFAIL, 3); dSetRS(dev, RS_STENCILPASS, 3);   // REPLACE
    dSetRS(dev, RS_STENCILREF, 0);                                           // pass A : clear the region to 0
    grad_quad(dev, x - 2.0f, y - 2.0f, w + 4.0f, h + 4.0f, 0, 0, 0, 0);
    dSetRS(dev, RS_STENCILREF, 1);                                          // pass B : set 1 inside the rounded rect
    rrect(dev, x, y, w, h, r, 0xFF000000, 0xFF000000, 0.0f);                 // feather 0 : mask must match the track EXACTLY (default 1.2px feather + alpha-test off would overflow the round cap ~1px)
    dSetRS(dev, D3DRS_COLORWRITEENABLE, 0x0000000F);                         // content : colour ONLY where stencil == 1
    dSetRS(dev, RS_STENCILFUNC, 3);                                         // EQUAL
    dSetRS(dev, RS_STENCILFAIL, 1); dSetRS(dev, RS_STENCILZFAIL, 1); dSetRS(dev, RS_STENCILPASS, 1);   // KEEP (don't touch stencil while drawing)
    dSetRS(dev, D3DRS_ALPHABLENDENABLE, 1);
}
void rrect_clip_end(u32 dev) {
    dSetRS(dev, RS_STENCILENABLE, 0);
    dSetRS(dev, D3DRS_COLORWRITEENABLE, 0x0000000F);
}

// ---- A BAR OF LIGHT, and the haze around it, sharing one WINDOW along their length. ------------------------
// Both fade in and out along x with a COSINE taper (a Tukey window): flat in the middle, and at each end a
// taper whose slope is zero at BOTH the tip and the junction with the flat part.
//
// The reason is the failure it replaces. A bar built as three quads -- fade in, hold, fade out -- is
// piecewise LINEAR, so where the ramp meets the plateau the slope jumps. The eye is a derivative detector
// (Mach banding): it draws a line at that junction, and the bar reads as CUT a little before each end. The
// value is continuous there; only its slope is not, and that is enough to see. A cosine taper has no such
// corner anywhere.
//
// Second half of the same problem: the haze used to be an rrect_glow inset by a fixed number of pixels while
// the bar tapered over a FRACTION of its width, so the haze ended somewhere in the middle of the taper --
// a second, unrelated edge. Here the two take the same `taper`, so they can only dissolve together.
//
// Alpha varies along x alone, so the two triangles of every column agree exactly -- the same rule as the S/V
// square (see docs/reference/d3d8-rendering.md, four-corner gradients).
static void hwindow(float* k, int n, float taper) {          // the shared window, sampled at n+1 points
    if (taper < 0.02f) taper = 0.02f; if (taper > 0.5f) taper = 0.5f;
    for (int i = 0; i <= n; ++i) {
        const float t = (float)i / (float)n;
        if      (t < taper)          k[i] = 0.5f - 0.5f * cosf(PI_ * (t / taper));
        else if (t > 1.0f - taper)   k[i] = 0.5f - 0.5f * cosf(PI_ * ((1.0f - t) / taper));
        else                         k[i] = 1.0f;
    }
}
void hbar_soft(u32 dev, float x, float y, float w, float h, u32 rgb, u32 peakAlpha, float taper)
{
    if (w <= 0.0f || h <= 0.0f || peakAlpha == 0) return;
    x -= 0.5f; y -= 0.5f;
    const int N = 40;
    float k[41]; hwindow(k, N, taper);
    const u32 c = rgb & 0x00FFFFFF;
    VtxC v[2 * (N + 1)];
    for (int i = 0; i <= N; ++i) {
        const u32 col = c | ((u32)((float)peakAlpha * k[i] + 0.5f) << 24);
        const float vx = x + w * ((float)i / (float)N);
        v[2 * i]     = { vx, y,     0, 1, col };
        v[2 * i + 1] = { vx, y + h, 0, 1, col };
    }
    dDrawUP(dev, D3DPT_TRIANGLESTRIP, 2 * N, v, sizeof(VtxC));
}
void hglow_soft(u32 dev, float x, float cy, float w, float halfH, u32 rgb, u32 peakAlpha, float taper)
{
    if (w <= 0.0f || halfH <= 0.0f || peakAlpha == 0) return;
    x -= 0.5f; cy -= 0.5f;
    const int N = 40;
    float k[41]; hwindow(k, N, taper);
    const u32 c = rgb & 0x00FFFFFF;
    VtxC v[2 * (N + 1)];
    for (int half = 0; half < 2; ++half) {                   // upper half, then lower : peak on the centre line
        const float ey = cy + (half ? halfH : -halfH);
        for (int i = 0; i <= N; ++i) {
            const float vx = x + w * ((float)i / (float)N);
            const u32 col = c | ((u32)((float)peakAlpha * k[i] + 0.5f) << 24);
            v[2 * i]     = { vx, ey, 0, 1, c };              // outer edge : alpha 0
            v[2 * i + 1] = { vx, cy, 0, 1, col };            // centre line : the window
        }
        dDrawUP(dev, D3DPT_TRIANGLESTRIP, 2 * N, v, sizeof(VtxC));
    }
}

// The BAKED-MASK path for a top-capped rounded rect. Returns false if the mask is unavailable (no texture
// yet, gated off during a zone load, or a radius outside the baked range) -- the caller then draws the
// feathered geometry exactly as before.
//
// Two things make this better than the fan+feather, and they are the same two things:
//   * the arc's coverage is the REAL integral of the quarter disc (baked at 16x16 samples per texel), not a
//     polygon plus a linear ramp bolted outside the radius;
//   * the straight edges are drawn CRISP, with no feather at all. On a pixel-aligned rect an edge that lands
//     on a pixel boundary IS already perfectly anti-aliased -- feathering it only adds a halo. So the shape
//     snaps to the grid here, which is the project's own rule 1, and then nothing on it is approximate.
// That also removes the mismatch rule 2 exists for: there is no feather to be inconsistent about.
static bool topcaps_masked(u32 dev, float x, float y, float w, float h, int r, u32 cTop, u32 cBot)
{
    if (!corner_mask_enabled()) return false;
    if (r < CM_RMIN || r > CM_RMAX) return false;
    float u0, v0, u1, v1;
    if (!corner_mask_uv(r, u0, v0, u1, v1)) return false;
    const u32 tex = corner_mask_tex(dev);
    if (!tex) return false;

    // TEXEL-for-PIXEL only means something on the pixel grid, so land the rect on it.
    x = (float)(int)(x + 0.5f); y = (float)(int)(y + 0.5f);
    w = (float)(int)(w + 0.5f); h = (float)(int)(h + 0.5f);
    if (w < 2.0f * (float)r || h < (float)r) return false;   // too small for this radius : let the caller clamp and feather
    const float R = (float)r;
    const u32   cMid = lerp_argb(cTop, cBot, R / h);         // the gradient where the arcs end

    // --- the straight body : three crisp rects, no feather ---
    vrect_raw(dev, x + R - 0.5f,     y - 0.5f,     w - 2.0f * R, h,     cTop, cBot);   // centre column, full height
    vrect_raw(dev, x - 0.5f,         y + R - 0.5f, R,            h - R, cMid, cBot);   // left band, below the arc
    vrect_raw(dev, x + w - R - 0.5f, y + R - 0.5f, R,            h - R, cMid, cBot);   // right band

    // --- the two arcs : the baked mask, 1:1, mirrored in u for the right-hand one ---
    dTexQuadState(dev, tex);                                  // MODULATE tex x diffuse, on colour AND alpha
    Vtx q[4] = {
        { x - 0.5f,     y - 0.5f,     0, 1, cTop, u0, v0 },   // TL corner
        { x + R - 0.5f, y - 0.5f,     0, 1, cTop, u1, v0 },
        { x - 0.5f,     y + R - 0.5f, 0, 1, cMid, u0, v1 },
        { x + R - 0.5f, y + R - 0.5f, 0, 1, cMid, u1, v1 },
    };
    dDrawUP(dev, D3DPT_TRIANGLESTRIP, 2, q, sizeof(Vtx));
    Vtx q2[4] = {
        { x + w - R - 0.5f, y - 0.5f,     0, 1, cTop, u1, v0 },   // TR : the same block, u mirrored
        { x + w - 0.5f,     y - 0.5f,     0, 1, cTop, u0, v0 },
        { x + w - R - 0.5f, y + R - 0.5f, 0, 1, cMid, u1, v1 },
        { x + w - 0.5f,     y + R - 0.5f, 0, 1, cMid, u0, v1 },
    };
    dDrawUP(dev, D3DPT_TRIANGLESTRIP, 2, q2, sizeof(Vtx));
    dColorQuadState(dev);                                     // never leave a bound texture for the next widget (rule 8)
    return true;
}

// AA rounded rect with only the TOP corners rounded -- a TAB : its bottom is not a silhouette, it is a
// junction with the surface underneath. Same recipe as rrect(), and for the same four reasons -- every one of
// which the hand-rolled version in config_controls.cpp got wrong, which is why the tabs looked faceted:
//   * the half-pixel offset is applied here, ONCE, like every other shape. Without it the tab sits half a
//     pixel off every rrect() drawn beside it and both read as blurred (rule 1);
//   * the corner fans take the GRADIENT's colour at each vertex instead of a flat cTop -- a flat corner meets
//     the band below it at a different colour, and that step shows on any tab tall enough to have a gradient;
//   * the straight edges are feathered as well as the arcs. Feathering only the corners is the classic 1px
//     seam (rule 2) : the arcs then reach a pixel further out than the flat edges between them;
//   * the segment count follows the RADIUS instead of a fixed 8, so a big corner stops being a polygon.
void rrect_topcaps(u32 dev, float x, float y, float w, float h, float r, u32 cTop, u32 cBot, float feather)
{
    if (w <= 0.0f || h <= 0.0f) return;
    x -= 0.5f; y -= 0.5f;                                        // D3D half-pixel rule (applied ONCE here)
    if (r > w * 0.5f) r = w * 0.5f;
    if (r > h)        r = h;
    // The BAKED mask first : it is strictly better when it is available (real coverage, crisp edges, nothing
    // approximated). Everything below is the fallback -- no mask texture yet, a radius outside the baked set,
    // or a zone loading, where a texture's alpha would sample opaque and square off every corner.
    if (topcaps_masked(dev, x + 0.5f, y + 0.5f, w, h, (int)(r + 0.5f), cTop, cBot)) return;
    if (r < 0.75f && feather <= 0.0f) { vrect_raw(dev, x, y, w, h, cTop, cBot); return; }
    if (r < 0.75f) { r = 0.75f; if (r > w * 0.5f) r = w * 0.5f; if (r > h) r = h; }
    #define CY(yy) lerp_argb(cTop, cBot, ((yy) - y) / h)
    // --- solid interior : centre column full height + the two side bands, which run to the BOTTOM (only the
    //     top corners are cut away) ---
    vrect_raw(dev, x + r,     y,     w - 2 * r, h,     cTop,      cBot);
    vrect_raw(dev, x,         y + r, r,         h - r, CY(y + r), cBot);
    vrect_raw(dev, x + w - r, y + r, r,         h - r, CY(y + r), cBot);
    const int NcMax = 24;
    int Nc = (int)(r * 0.7f); if (Nc < 6) Nc = 6; if (Nc > NcMax) Nc = NcMax;
    struct Corner { float cx, cy, a0; };
    const Corner cs2[2] = { { x + r,     y + r, PI_        },     // TL : 180 -> 270
                            { x + w - r, y + r, 1.5f * PI_ } };   // TR : 270 -> 360
    for (int k = 0; k < 2; ++k) {
        const float ccx = cs2[k].cx, ccy = cs2[k].cy, a0 = cs2[k].a0;
        VtxC fan[NcMax + 2];
        fan[0] = { ccx, ccy, 0, 1, CY(ccy) };
        for (int i = 0; i <= Nc; ++i) {
            const float a = a0 + (0.5f * PI_) * (float)i / (float)Nc;
            const float vx = ccx + r * cosf(a), vy = ccy + r * sinf(a);
            fan[i + 1] = { vx, vy, 0, 1, CY(vy) };
        }
        dDrawUP(dev, D3DPT_TRIANGLEFAN, Nc, fan, sizeof(VtxC));
    }
    // --- feather the VISIBLE perimeter (top edge, both sides, both arcs) by the SAME width. The bottom is
    //     deliberately left crisp : it is covered by the body the tab melts into, so feathering it would only
    //     put a soft line inside an opaque surface. ---
    if (feather > 0.0f) {
        const float f = feather;
        { const u32 tA = cTop & 0x00FFFFFF;
          VtxC T[4] = { { x + r, y - f, 0,1, tA },   { x + w - r, y - f, 0,1, tA },
                        { x + r, y,     0,1, cTop }, { x + w - r, y,     0,1, cTop } };
          dDrawUP(dev, D3DPT_TRIANGLESTRIP, 2, T, sizeof(VtxC)); }
        { const u32 c0 = CY(y + r);
          VtxC L[4] = { { x - f, y + r, 0,1, c0 & 0x00FFFFFF },   { x, y + r, 0,1, c0 },
                        { x - f, y + h, 0,1, cBot & 0x00FFFFFF }, { x, y + h, 0,1, cBot } };
          dDrawUP(dev, D3DPT_TRIANGLESTRIP, 2, L, sizeof(VtxC));
          VtxC R[4] = { { x + w, y + r, 0,1, c0 },   { x + w + f, y + r, 0,1, c0 & 0x00FFFFFF },
                        { x + w, y + h, 0,1, cBot }, { x + w + f, y + h, 0,1, cBot & 0x00FFFFFF } };
          dDrawUP(dev, D3DPT_TRIANGLESTRIP, 2, R, sizeof(VtxC)); }
        for (int k = 0; k < 2; ++k) {
            const float ccx = cs2[k].cx, ccy = cs2[k].cy, a0 = cs2[k].a0;
            VtxC ring[2 * (NcMax + 1)];
            for (int i = 0; i <= Nc; ++i) {
                const float a = a0 + (0.5f * PI_) * (float)i / (float)Nc, ca = cosf(a), sa = sinf(a);
                const u32 ci = CY(ccy + r * sa);
                ring[2 * i]     = { ccx + r * ca,       ccy + r * sa,       0, 1, ci };
                ring[2 * i + 1] = { ccx + (r + f) * ca, ccy + (r + f) * sa, 0, 1, ci & 0x00FFFFFF };
            }
            dDrawUP(dev, D3DPT_TRIANGLESTRIP, 2 * Nc, ring, sizeof(VtxC));
        }
    }
    #undef CY
}

// The BAKED-MASK path for a two-radius rounded rect. Same idea as rrect_masked, with the corners taken from
// two different blocks -- and it exists so that a shape whose radius is ANIMATING never has to change
// technique. It used to: the fold drew feathered geometry while it moved and switched to the mask at the
// ends, so on the last frame the whole silhouette went from 1.2px soft to crisp at once. That is the flash
// at the end of the animation -- not a colour change, a change of renderer.
// A radius the mask cannot serve (below CM_RMIN) is drawn SQUARE, which is what it is within a pixel or two
// anyway; the straight edges stay crisp the whole way, so nothing about the silhouette pops.
static bool rrect_tb_masked(u32 dev, float x, float y, float w, float h, int rT, int rB, u32 cTop, u32 cBot)
{
    if (!corner_mask_enabled()) return false;
    if (rT < CM_RMIN || rT > CM_RMAX) return false;
    if (rB && (rB < CM_RMIN || rB > CM_RMAX)) return false;
    if (!px_aligned(x) || !px_aligned(y) || !px_aligned(w) || !px_aligned(h)) return false;
    const float RT = (float)rT, RB = (float)rB;
    if (w < 2.0f * (RT > RB ? RT : RB) || h < RT + RB) return false;
    float uT0, vT0, uT1, vT1;
    float uB0 = 0.0f, vB0 = 0.0f, uB1 = 0.0f, vB1 = 0.0f;   // only read when rB != 0, but the compiler cannot see that
    if (!corner_mask_uv(rT, uT0, vT0, uT1, vT1)) return false;
    if (rB && !corner_mask_uv(rB, uB0, vB0, uB1, vB1)) return false;
    const u32 tex = corner_mask_tex(dev);
    if (!tex) return false;

    const u32 cA = lerp_argb(cTop, cBot, RT / h);
    const u32 cB = lerp_argb(cTop, cBot, (h - RB) / h);
    // bands : the middle spans the full width ; the top and bottom bands sit between their own corners
    if (h - RT - RB > 0.0f) vrect_raw(dev, x - 0.5f, y + RT - 0.5f, w, h - RT - RB, cA, cB);
    vrect_raw(dev, x + RT - 0.5f, y - 0.5f, w - 2.0f * RT, RT, cTop, cA);
    if (rB) vrect_raw(dev, x + RB - 0.5f, y + h - RB - 0.5f, w - 2.0f * RB, RB, cB, cBot);
    else    vrect_raw(dev, x - 0.5f,      y + h - 0.5f,      w,             0.0f, cB, cBot);   // (no-op : square feet)

    dTexQuadState(dev, tex);
    struct C { float x0, x1, y0, y1; float ua, ub, va, vb; u32 ct, cb; };
    C q[4]; int n = 0;
    const float X0 = x - 0.5f, X3 = x + w - 0.5f, Y0 = y - 0.5f, Y3 = y + h - 0.5f;
    q[n++] = { X0, X0 + RT, Y0, Y0 + RT, uT0, uT1, vT0, vT1, cTop, cA };          // TL
    q[n++] = { X3 - RT, X3, Y0, Y0 + RT, uT1, uT0, vT0, vT1, cTop, cA };          // TR (u mirrored)
    if (rB) {
        q[n++] = { X0, X0 + RB, Y3 - RB, Y3, uB0, uB1, vB1, vB0, cB, cBot };      // BL (v mirrored)
        q[n++] = { X3 - RB, X3, Y3 - RB, Y3, uB1, uB0, vB1, vB0, cB, cBot };      // BR (both)
    }
    for (int k = 0; k < n; ++k) {
        const C& c = q[k];
        Vtx v[4] = {
            { c.x0, c.y0, 0, 1, c.ct, c.ua, c.va },
            { c.x1, c.y0, 0, 1, c.ct, c.ub, c.va },
            { c.x0, c.y1, 0, 1, c.cb, c.ua, c.vb },
            { c.x1, c.y1, 0, 1, c.cb, c.ub, c.vb },
        };
        dDrawUP(dev, D3DPT_TRIANGLESTRIP, 2, v, sizeof(Vtx));
    }
    dColorQuadState(dev);
    return true;
}

// AA rounded rect with INDEPENDENT top and bottom radii -- the shape a section's title bar takes while it
// folds. Closed it is fully rounded ; open its feet are square and welded to the card ; and in between the
// bottom radius simply interpolates, which is the only way to get from one to the other without a pop. The
// two end states are drawn by the crisp masked paths (rrect / rrect_topcaps) ; this one carries the motion,
// where a tenth of a pixel of anti-aliasing is not what anybody is looking at.
//
// Same recipe as rrect otherwise: half-pixel applied once, corner fans coloured from the gradient, and ONE
// feather width across the whole perimeter (rule 2 -- feather everything or nothing).
void rrect_tb(u32 dev, float x, float y, float w, float h, float rT, float rB, u32 cTop, u32 cBot, float feather)
{
    if (w <= 0.0f || h <= 0.0f) return;
    x -= 0.5f; y -= 0.5f;
    const float rmax = w * 0.5f;
    if (rT > rmax) rT = rmax;
    if (rB > rmax) rB = rmax;
    if (rT < 0.0f) rT = 0.0f;
    if (rB < 0.0f) rB = 0.0f;
    if (rT + rB > h) { const float k = h / (rT + rB); rT *= k; rB *= k; }
    // The mask first, at whole-pixel radii -- so an animating radius keeps the SAME renderer from end to end.
    // A bottom radius under the smallest baked block reads as square within a pixel, and is drawn square.
    { const int iT = (int)(rT + 0.5f), iB = (rB < 2.5f) ? 0 : (int)(rB + 0.5f);
      if (feather > 0.0f && rrect_tb_masked(dev, x + 0.5f, y + 0.5f, w, h, iT, iB, cTop, cBot)) return; }
    #define CY(yy) lerp_argb(cTop, cBot, ((yy) - y) / h)
    // middle : full width, between the two corner zones -- the left and right edges are straight there
    if (h - rT - rB > 0.0f) vrect_raw(dev, x, y + rT, w, h - rT - rB, CY(y + rT), CY(y + h - rB));
    if (rT > 0.0f) vrect_raw(dev, x + rT, y,          w - 2.0f * rT, rT, cTop,           CY(y + rT));
    if (rB > 0.0f) vrect_raw(dev, x + rB, y + h - rB, w - 2.0f * rB, rB, CY(y + h - rB), cBot);
    const int NcMax = 24;
    struct Corner { float cx, cy, a0, r; };
    const Corner cs4[4] = {
        { x + rT,     y + rT,     PI_,        rT },   // TL
        { x + w - rT, y + rT,     1.5f * PI_, rT },   // TR
        { x + w - rB, y + h - rB, 0.0f,       rB },   // BR
        { x + rB,     y + h - rB, 0.5f * PI_, rB },   // BL
    };
    for (int k = 0; k < 4; ++k) {
        const float r = cs4[k].r;
        if (r < 0.35f) continue;                       // a square corner needs no fan : the bands already cover it
        int Nc = (int)(r * 0.7f); if (Nc < 6) Nc = 6; if (Nc > NcMax) Nc = NcMax;
        const float ccx = cs4[k].cx, ccy = cs4[k].cy, a0 = cs4[k].a0;
        VtxC fan[NcMax + 2];
        fan[0] = { ccx, ccy, 0, 1, CY(ccy) };
        for (int i = 0; i <= Nc; ++i) {
            const float a = a0 + (0.5f * PI_) * (float)i / (float)Nc;
            const float vx = ccx + r * cosf(a), vy = ccy + r * sinf(a);
            fan[i + 1] = { vx, vy, 0, 1, CY(vy) };
        }
        dDrawUP(dev, D3DPT_TRIANGLEFAN, Nc, fan, sizeof(VtxC));
    }
    if (feather > 0.0f) {
        const float f = feather;
        { const u32 tA = cTop & 0x00FFFFFF;
          VtxC T[4] = { { x + rT, y - f, 0,1, tA },   { x + w - rT, y - f, 0,1, tA },
                        { x + rT, y,     0,1, cTop }, { x + w - rT, y,     0,1, cTop } };
          dDrawUP(dev, D3DPT_TRIANGLESTRIP, 2, T, sizeof(VtxC)); }
        { const u32 bA = cBot & 0x00FFFFFF;
          VtxC B[4] = { { x + rB, y + h,     0,1, cBot }, { x + w - rB, y + h,     0,1, cBot },
                        { x + rB, y + h + f, 0,1, bA },   { x + w - rB, y + h + f, 0,1, bA } };
          dDrawUP(dev, D3DPT_TRIANGLESTRIP, 2, B, sizeof(VtxC)); }
        { const u32 c0 = CY(y + rT), c1 = CY(y + h - rB);
          VtxC L[4] = { { x - f, y + rT, 0,1, c0 & 0x00FFFFFF }, { x, y + rT, 0,1, c0 },
                        { x - f, y + h - rB, 0,1, c1 & 0x00FFFFFF }, { x, y + h - rB, 0,1, c1 } };
          dDrawUP(dev, D3DPT_TRIANGLESTRIP, 2, L, sizeof(VtxC));
          VtxC R[4] = { { x + w, y + rT, 0,1, c0 }, { x + w + f, y + rT, 0,1, c0 & 0x00FFFFFF },
                        { x + w, y + h - rB, 0,1, c1 }, { x + w + f, y + h - rB, 0,1, c1 & 0x00FFFFFF } };
          dDrawUP(dev, D3DPT_TRIANGLESTRIP, 2, R, sizeof(VtxC)); }
        for (int k = 0; k < 4; ++k) {
            const float r = cs4[k].r;
            if (r < 0.35f) continue;
            int Nc = (int)(r * 0.7f); if (Nc < 6) Nc = 6; if (Nc > NcMax) Nc = NcMax;
            const float ccx = cs4[k].cx, ccy = cs4[k].cy, a0 = cs4[k].a0;
            VtxC ring[2 * (NcMax + 1)];
            for (int i = 0; i <= Nc; ++i) {
                const float a = a0 + (0.5f * PI_) * (float)i / (float)Nc, ca = cosf(a), sa = sinf(a);
                const u32 ci = CY(ccy + r * sa);
                ring[2 * i]     = { ccx + r * ca,       ccy + r * sa,       0, 1, ci };
                ring[2 * i + 1] = { ccx + (r + f) * ca, ccy + (r + f) * sa, 0, 1, ci & 0x00FFFFFF };
            }
            dDrawUP(dev, D3DPT_TRIANGLESTRIP, 2 * Nc, ring, sizeof(VtxC));
        }
    }
    #undef CY
}

void rrect_left(u32 dev, float x, float y, float w, float h, float r, u32 cTop, u32 cBot, float feather)
{
    if (w <= 0.0f || h <= 0.0f) return;
    x -= 0.5f; y -= 0.5f;
    if (r > w) r = w;
    if (r > h * 0.5f) r = h * 0.5f;
    if (r < 0.0f) r = 0.0f;
    #define CY(yy) lerp_argb(cTop, cBot, ((yy) - y) / h)
    if (r < 0.75f) { vrect_raw(dev, x, y, w, h, cTop, cBot); }
    else {
        vrect_raw(dev, x + r, y,     w - r, h,         cTop,       cBot);          // body (flat right edge = the level)
        vrect_raw(dev, x,     y + r, r,     h - 2 * r, CY(y + r),  CY(y + h - r)); // left band
        const int NcMax = 14;
        int Nc = (int)(r * 0.9f); if (Nc < 6) Nc = 6; if (Nc > NcMax) Nc = NcMax;   // rounder on bigger radii
        const float cc[2][3] = { { x + r, y + r,     PI_ },          // TL : 180 -> 270
                                 { x + r, y + h - r, 0.5f * PI_ } };  // BL :  90 -> 180
        for (int k = 0; k < 2; ++k) {                            // ONE triangle-fan per (left) corner
            const float ccx = cc[k][0], ccy = cc[k][1], a0 = cc[k][2];
            VtxC fan[NcMax + 2];
            fan[0] = { ccx, ccy, 0, 1, CY(ccy) };                // fan centre
            for (int i = 0; i <= Nc; ++i) {
                const float a = a0 + (0.5f * PI_) * (float)i / (float)Nc;
                const float vx = ccx + r * cosf(a), vy = ccy + r * sinf(a);
                fan[i + 1] = { vx, vy, 0, 1, CY(vy) };
            }
            dDrawUP(dev, D3DPT_TRIANGLEFAN, Nc, fan, sizeof(VtxC));
        }
        if (feather > 0.0f) {
            const float f = feather;
            const u32 c0 = CY(y + r), c1 = CY(y + h - r);
            VtxC L[4] = { { x, y + r, 0, 1, c0 }, { x, y + h - r, 0, 1, c1 },
                          { x - f, y + r, 0, 1, c0 & 0x00FFFFFF }, { x - f, y + h - r, 0, 1, c1 & 0x00FFFFFF } };
            dDrawUP(dev, D3DPT_TRIANGLESTRIP, 2, L, sizeof(VtxC));   // left edge feather
            // top + bottom edge feathers (x+r..x+w ; the flat RIGHT edge stays crisp = the level tip) so the
            // straight edges extend by the same `feather` as the rounded left cap (no 1px centre mismatch).
            { const u32 tA = cTop & 0x00FFFFFF;
              VtxC T[4] = { { x + r, y - f, 0,1, tA }, { x + w, y - f, 0,1, tA },
                            { x + r, y,     0,1, cTop }, { x + w, y,     0,1, cTop } };
              dDrawUP(dev, D3DPT_TRIANGLESTRIP, 2, T, sizeof(VtxC)); }
            { const u32 bA = cBot & 0x00FFFFFF;
              VtxC B[4] = { { x + r, y + h,     0,1, cBot }, { x + w, y + h,     0,1, cBot },
                            { x + r, y + h + f, 0,1, bA },   { x + w, y + h + f, 0,1, bA } };
              dDrawUP(dev, D3DPT_TRIANGLESTRIP, 2, B, sizeof(VtxC)); }
            for (int k = 0; k < 2; ++k) {
                const float ccx = cc[k][0], ccy = cc[k][1], a0 = cc[k][2];
                VtxC ring[2 * (NcMax + 1)];
                for (int i = 0; i <= Nc; ++i) {
                    const float a = a0 + (0.5f * PI_) * (float)i / (float)Nc, ca = cosf(a), sa = sinf(a);
                    const u32 ci = CY(ccy + r * sa);
                    ring[2 * i]     = { ccx + r * ca,        ccy + r * sa,        0, 1, ci };
                    ring[2 * i + 1] = { ccx + (r + f) * ca,  ccy + (r + f) * sa,  0, 1, ci & 0x00FFFFFF };
                }
                dDrawUP(dev, D3DPT_TRIANGLESTRIP, 2 * Nc, ring, sizeof(VtxC));
            }
        }
    }
    #undef CY
}

void rrect_glow(u32 dev, float x, float y, float w, float h, float r, u32 col, float glowW)
{
    if (w <= 0.0f || h <= 0.0f || glowW <= 0.0f) return;
    x -= 0.5f; y -= 0.5f;
    if (r > w * 0.5f) r = w * 0.5f;
    if (r > h * 0.5f) r = h * 0.5f;
    if (r < 0.0f) r = 0.0f;
    const float f = glowW;
    const u32 c0 = col & 0x00FFFFFF;
    // 4 straight edges : peak on the path, alpha 0 outward
    { VtxC v[4] = { { x + r, y, 0,1, col }, { x + w - r, y, 0,1, col },
                    { x + r, y - f, 0,1, c0 }, { x + w - r, y - f, 0,1, c0 } };            // top
      dDrawUP(dev, D3DPT_TRIANGLESTRIP, 2, v, sizeof(VtxC)); }
    { VtxC v[4] = { { x + r, y + h, 0,1, col }, { x + w - r, y + h, 0,1, col },
                    { x + r, y + h + f, 0,1, c0 }, { x + w - r, y + h + f, 0,1, c0 } };      // bottom
      dDrawUP(dev, D3DPT_TRIANGLESTRIP, 2, v, sizeof(VtxC)); }
    { VtxC v[4] = { { x, y + r, 0,1, col }, { x, y + h - r, 0,1, col },
                    { x - f, y + r, 0,1, c0 }, { x - f, y + h - r, 0,1, c0 } };              // left
      dDrawUP(dev, D3DPT_TRIANGLESTRIP, 2, v, sizeof(VtxC)); }
    { VtxC v[4] = { { x + w, y + r, 0,1, col }, { x + w, y + h - r, 0,1, col },
                    { x + w + f, y + r, 0,1, c0 }, { x + w + f, y + h - r, 0,1, c0 } };      // right
      dDrawUP(dev, D3DPT_TRIANGLESTRIP, 2, v, sizeof(VtxC)); }
    // 4 corner arcs : radius r (peak) -> r+f (alpha 0)
    if (r > 0.25f) {
        const int Nc = 6;
        const float cs4[4][3] = {
            { x + r,     y + r,     PI_        },   // TL
            { x + w - r, y + r,     1.5f * PI_ },   // TR
            { x + w - r, y + h - r, 0.0f       },   // BR
            { x + r,     y + h - r, 0.5f * PI_ },   // BL
        };
        for (int k = 0; k < 4; ++k) {
            const float ccx = cs4[k][0], ccy = cs4[k][1], a0 = cs4[k][2];
            VtxC ring[2 * (Nc + 1)];
            for (int i = 0; i <= Nc; ++i) {
                const float a = a0 + (0.5f * PI_) * (float)i / (float)Nc, ca = cosf(a), sa = sinf(a);
                ring[2 * i]     = { ccx + r * ca,       ccy + r * sa,       0, 1, col };
                ring[2 * i + 1] = { ccx + (r + f) * ca, ccy + (r + f) * sa, 0, 1, c0 };
            }
            dDrawUP(dev, D3DPT_TRIANGLESTRIP, 2 * Nc, ring, sizeof(VtxC));
        }
    }
}

void disc_glow(u32 dev, float cx, float cy, float r, u32 col, float glowW)
{
    if (r <= 0.0f || glowW <= 0.0f) return;
    cx -= 0.5f; cy -= 0.5f;
    const int N = 32;
    const u32 c0 = col & 0x00FFFFFF;
    VtxC ring[2 * (N + 1)];
    for (int i = 0; i <= N; ++i) {
        const float a = (float)i / N * 6.2831853f, ca = cosf(a), sa = sinf(a);
        ring[2 * i]     = { cx + r * ca,             cy + r * sa,             0, 1, col };
        ring[2 * i + 1] = { cx + (r + glowW) * ca,   cy + (r + glowW) * sa,   0, 1, c0  };
    }
    dDrawUP(dev, D3DPT_TRIANGLESTRIP, 2 * N, ring, sizeof(VtxC));
}

void rrect_stroke(u32 dev, float x, float y, float w, float h, float r, u32 col, float bt)
{
    if (w <= 0.0f || h <= 0.0f || bt <= 0.0f) return;
    x -= 0.5f; y -= 0.5f;
    if (r > w * 0.5f) r = w * 0.5f;
    if (r > h * 0.5f) r = h * 0.5f;
    if (r < 0.0f) r = 0.0f;
    const float ri = (r - bt > 0.0f) ? r - bt : 0.0f;
    const float fth = 1.0f;                                   // AA feather on the outer arc
    const u32 c0 = col & 0x00FFFFFF;
    // 4 straight edges (axis-aligned -> no aliasing, solid bands from the tangent points)
    { VtxC v[4] = { { x + r, y, 0,1, col }, { x + w - r, y, 0,1, col },
                    { x + r, y + bt, 0,1, col }, { x + w - r, y + bt, 0,1, col } };                 // top
      dDrawUP(dev, D3DPT_TRIANGLESTRIP, 2, v, sizeof(VtxC)); }
    { VtxC v[4] = { { x + r, y + h - bt, 0,1, col }, { x + w - r, y + h - bt, 0,1, col },
                    { x + r, y + h, 0,1, col }, { x + w - r, y + h, 0,1, col } };                    // bottom
      dDrawUP(dev, D3DPT_TRIANGLESTRIP, 2, v, sizeof(VtxC)); }
    { VtxC v[4] = { { x, y + r, 0,1, col }, { x + bt, y + r, 0,1, col },
                    { x, y + h - r, 0,1, col }, { x + bt, y + h - r, 0,1, col } };                   // left
      dDrawUP(dev, D3DPT_TRIANGLESTRIP, 2, v, sizeof(VtxC)); }
    { VtxC v[4] = { { x + w - bt, y + r, 0,1, col }, { x + w, y + r, 0,1, col },
                    { x + w - bt, y + h - r, 0,1, col }, { x + w, y + h - r, 0,1, col } };            // right
      dDrawUP(dev, D3DPT_TRIANGLESTRIP, 2, v, sizeof(VtxC)); }
    // 4 corner arcs : a quarter annulus ri..r + a feathered rim r..r+fth
    if (r > 0.25f) {
        const int Nc = 8;
        const float cs4[4][3] = {
            { x + r,     y + r,     PI_        },   // TL
            { x + w - r, y + r,     1.5f * PI_ },   // TR
            { x + w - r, y + h - r, 0.0f       },   // BR
            { x + r,     y + h - r, 0.5f * PI_ },   // BL
        };
        for (int k = 0; k < 4; ++k) {
            const float ccx = cs4[k][0], ccy = cs4[k][1], a0 = cs4[k][2];
            VtxC ann[2 * (Nc + 1)], rim[2 * (Nc + 1)];
            for (int i = 0; i <= Nc; ++i) {
                const float a = a0 + (0.5f * PI_) * (float)i / (float)Nc, ca = cosf(a), sa = sinf(a);
                ann[2 * i]     = { ccx + r * ca,        ccy + r * sa,        0, 1, col };
                ann[2 * i + 1] = { ccx + ri * ca,       ccy + ri * sa,       0, 1, col };
                rim[2 * i]     = { ccx + r * ca,        ccy + r * sa,        0, 1, col };
                rim[2 * i + 1] = { ccx + (r + fth) * ca, ccy + (r + fth) * sa, 0, 1, c0 };
            }
            dDrawUP(dev, D3DPT_TRIANGLESTRIP, 2 * Nc, ann, sizeof(VtxC));
            dDrawUP(dev, D3DPT_TRIANGLESTRIP, 2 * Nc, rim, sizeof(VtxC));
        }
    }
}

void soft_blob(u32 dev, float cx, float cy, float hw, float hh, u32 col)
{
    u32 c0 = col & 0x00FFFFFF;                                   // transparent at the edges
    grad_quad(dev, cx - hw, cy - hh, hw, hh, c0, c0, c0, col);   // TL quadrant (bright at centre = BR)
    grad_quad(dev, cx,      cy - hh, hw, hh, c0, c0, col, c0);   // TR (bright at BL)
    grad_quad(dev, cx - hw, cy,      hw, hh, c0, col, c0, c0);   // BL (bright at TR)
    grad_quad(dev, cx,      cy,      hw, hh, col, c0, c0, c0);   // BR (bright at TL)
}

} // namespace aio
