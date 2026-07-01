// draw.cpp -- see draw.h.
#include "draw.h"
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
    const int N = 28;
    const float f = 1.3f;                           // feather width -> anti-aliased edge
    const u32 c0 = col & 0x00FFFFFF;                // same colour, alpha 0
    // solid core (triangle fan)
    VtxC fan[N + 2];
    fan[0] = { cx, cy, 0, 1, col };
    for (int i = 0; i <= N; ++i) {
        float a = (float)i / N * 6.2831853f;
        fan[i + 1] = { cx + r * cosf(a), cy + r * sinf(a), 0, 1, col };
    }
    dDrawUP(dev, D3DPT_TRIANGLEFAN, N, fan, sizeof(VtxC));
    // feathered rim (triangle strip): opaque at r -> transparent at r+f
    VtxC ring[2 * (N + 1)];
    for (int i = 0; i <= N; ++i) {
        float a = (float)i / N * 6.2831853f, ca = cosf(a), sa = sinf(a);
        ring[2 * i]     = { cx + r * ca,       cy + r * sa,       0, 1, col };
        ring[2 * i + 1] = { cx + (r + f) * ca, cy + (r + f) * sa, 0, 1, c0  };
    }
    dDrawUP(dev, D3DPT_TRIANGLESTRIP, 2 * N, ring, sizeof(VtxC));
}

static const float PI_ = 3.14159265f;

// per-channel ARGB lerp (used for the vertical gradient across the rounded rect).
static inline u32 lerp_argb(u32 a, u32 b, float t)
{
    if (t < 0.0f) t = 0.0f; if (t > 1.0f) t = 1.0f;
    int aa = (a >> 24) & 255, ar = (a >> 16) & 255, ag = (a >> 8) & 255, ab = a & 255;
    int ba = (b >> 24) & 255, br = (b >> 16) & 255, bg = (b >> 8) & 255, bb = b & 255;
    int oa = aa + (int)((ba - aa) * t + 0.5f);
    int orr = ar + (int)((br - ar) * t + 0.5f);
    int og = ag + (int)((bg - ag) * t + 0.5f);
    int ob = ab + (int)((bb - ab) * t + 0.5f);
    return ((u32)oa << 24) | ((u32)orr << 16) | ((u32)og << 8) | (u32)ob;
}

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

void rrect(u32 dev, float x, float y, float w, float h, float r, u32 cTop, u32 cBot, float feather)
{
    if (w <= 0.0f || h <= 0.0f) return;
    x -= 0.5f; y -= 0.5f;                                        // D3D half-pixel rule (applied ONCE here)
    if (r > w * 0.5f) r = w * 0.5f;
    if (r > h * 0.5f) r = h * 0.5f;
    // colour at any scanline y' (linear top->bottom across the whole box height)
    #define CY(yy) lerp_argb(cTop, cBot, ((yy) - y) / h)
    if (r < 0.75f) {                                             // no radius -> a feathered plain rect
        vrect_raw(dev, x, y, w, h, cTop, cBot);
    } else {
        // --- solid interior : centre column full-height + the two side bands + 4 quarter-disc corners ---
        vrect_raw(dev, x + r,     y,     w - 2 * r, h,         cTop,       cBot);          // centre column
        vrect_raw(dev, x,         y + r, r,         h - 2 * r, CY(y + r),  CY(y + h - r)); // left band
        vrect_raw(dev, x + w - r, y + r, r,         h - 2 * r, CY(y + r),  CY(y + h - r)); // right band
        const int Nc = 6;                                       // arc segments per corner (uniform everywhere)
        struct Corner { float cx, cy, a0; };
        const Corner cs4[4] = {
            { x + r,     y + r,     PI_        },   // TL : 180 -> 270
            { x + w - r, y + r,     1.5f * PI_ },   // TR : 270 -> 360
            { x + w - r, y + h - r, 0.0f       },   // BR :   0 ->  90
            { x + r,     y + h - r, 0.5f * PI_ },   // BL :  90 -> 180
        };
        for (int k = 0; k < 4; ++k) {
            const float ccx = cs4[k].cx, ccy = cs4[k].cy, a0 = cs4[k].a0;
            const u32 cc = CY(ccy);
            float px = ccx + r * cosf(a0), py = ccy + r * sinf(a0);
            for (int i = 1; i <= Nc; ++i) {
                const float a = a0 + (0.5f * PI_) * (float)i / (float)Nc;
                const float nx = ccx + r * cosf(a), ny = ccy + r * sinf(a);
                VtxC t[3] = { { ccx, ccy, 0, 1, cc }, { px, py, 0, 1, CY(py) }, { nx, ny, 0, 1, CY(ny) } };
                dDrawUP(dev, D3DPT_TRIANGLEFAN, 1, t, sizeof(VtxC));
                px = nx; py = ny;
            }
        }
        // --- feathered outer rim : full-alpha on the path, alpha 0 at path+feather (crisp AA everywhere) ---
        if (feather > 0.0f) {
            const float f = feather;
            // top edge : full cTop on the path (y), alpha 0 at y-f
            {
                const u32 tA = cTop & 0x00FFFFFF;
                VtxC T[4] = { { x + r, y - f, 0,1, tA }, { x + w - r, y - f, 0,1, tA },
                              { x + r, y,     0,1, cTop }, { x + w - r, y,     0,1, cTop } };
                dDrawUP(dev, D3DPT_TRIANGLESTRIP, 2, T, sizeof(VtxC));
            }
            // bottom edge : full cBot on the path (y+h), alpha 0 at y+h+f
            {
                const u32 bA = cBot & 0x00FFFFFF;
                VtxC B[4] = { { x + r, y + h,     0,1, cBot }, { x + w - r, y + h,     0,1, cBot },
                              { x + r, y + h + f, 0,1, bA },   { x + w - r, y + h + f, 0,1, bA } };
                dDrawUP(dev, D3DPT_TRIANGLESTRIP, 2, B, sizeof(VtxC));
            }
            // left/right edges feather sideways with the vertical colour gradient preserved
            {
                const u32 c0 = CY(y + r), c1 = CY(y + h - r);
                // left : x-f..x
                VtxC L[4] = { { x - f, y + r, 0,1, c0 & 0x00FFFFFF }, { x, y + r, 0,1, c0 },
                              { x - f, y + h - r, 0,1, c1 & 0x00FFFFFF }, { x, y + h - r, 0,1, c1 } };
                dDrawUP(dev, D3DPT_TRIANGLESTRIP, 2, L, sizeof(VtxC));
                VtxC R[4] = { { x + w, y + r, 0,1, c0 }, { x + w + f, y + r, 0,1, c0 & 0x00FFFFFF },
                              { x + w, y + h - r, 0,1, c1 }, { x + w + f, y + h - r, 0,1, c1 & 0x00FFFFFF } };
                dDrawUP(dev, D3DPT_TRIANGLESTRIP, 2, R, sizeof(VtxC));
            }
            // 4 corner arcs : radius r (full) -> r+f (alpha 0)
            for (int k = 0; k < 4; ++k) {
                const float ccx = cs4[k].cx, ccy = cs4[k].cy, a0 = cs4[k].a0;
                VtxC ring[2 * (Nc + 1)];
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
