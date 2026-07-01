// liquid_bars.cpp -- see liquid_bars.h.
//
// Layout is faithful to the proven sandbox composition: the three bars are drawn
// in BATCHED passes (all back caps, then all liquid, then glass, then front caps)
// to minimise D3D state churn -- so the bar-specific draw helpers below are file
// statics and the per-frame composition lives in LiquidBars::draw().
#include "liquid_bars.h"
#include "palette.h"
#include "model/gamestate.h"
#include "gfx/draw.h"
#include "gfx/color.h"
#include "gfx/texture.h"
#include "gfx/noise.h"
#include <windows.h>
#include <math.h>

namespace aio {

// cap art dimensions + the raw BGRA blobs (rendered once, shared by all 3 bars).
static const int CAP_W = 164, CAP_H = 280;
static const char* CAP_FRONT = "D:\\Windower Tetsouo\\plugins\\_aiohud_re\\assets\\cap_front.bin";
static const char* CAP_BACK  = "D:\\Windower Tetsouo\\plugins\\_aiohud_re\\assets\\cap_back.bin";

// ============================ liquid body ==================================

// a liquid bar filled to `fill` (0..1) from the left. The liquid occupies width
// fw = w*fill; U span scales with fill so the texel density stays constant. The
// empty part is left clear -> the glass shows there.
static void draw_bar(u32 dev, float x, float y, float w, float h, float t, const u32* pal, float fill, int kind)
{
    float fw = w * fill;
    if (fw < 1.0f) return;

    // Per-resource animation personality:
    //  HP (0) : panics as it drains  -> low = fast & chaotic.
    //  MP (1) : magic fades as it drains -> low = slow & dim glow (no colour change).
    //  TP (2) : charges up -> more TP = more energetic.
    float spd, ampf, glowf;
    if (kind == 1) {                          // MP
        spd   = 0.30f + 1.70f * fill;
        ampf  = 0.50f + 0.90f * fill;
        glowf = 0.22f + 0.78f * fill;         // the magic effect fades at low MP
    } else if (kind == 2) {                   // TP
        spd   = 0.40f + 1.60f * fill;
        ampf  = 0.80f + 0.55f * fill;
        glowf = 1.0f;
    } else {                                  // HP
        float low = 1.0f - fill; if (low < 0) low = 0; if (low > 1) low = 1;
        spd   = 0.45f + 2.60f * low;
        ampf  = 1.00f + 1.30f * low;
        glowf = 1.0f;
    }
    float ts = t * spd;

    // HORIZONTAL FLOW is the dominant motion: the liquid STREAMS along the tube.
    // constant-velocity u-scroll + WRAP addressing = endless flow (never resets).
    float flow  = t * (0.05f + 0.10f * spd);             // body flow speed (scales with personality)
    float vWob  = 0.018f * ampf * sinf(ts * 0.7f);       // gentle vertical breathing only
    // The texture is ~8:1 (matches the bar), so we show exactly ONE tile across the
    // length. uSpan scales with fill so density is constant; <=1 so it never repeats.
    float uSpan = 1.0f * fill;

    // --- BODY : vertical gradient (light top / dark bottom) = volume ---
    float pul = 0.94f + 0.06f * sinf(ts * 1.6f);
    // TWO-TONE POTION gradient: BRIGHT accent (pal[2]) at the top, DEEP saturated
    // base (pal[0]) at the bottom -> luminous magic-fluid look.
    u32 aBody = pal[0] & 0xFF000000;
    if (kind == 2 && tp_tier(fill) < 1) {                // TP below 1000: more TRANSPARENT (charging)
        float op = 0.32f + 0.18f * (fill / 0.3333f);     // ~0.32 empty -> ~0.50 at 999, then jumps to 1.0 at 1000
        u32 ab = (u32)(((aBody >> 24) & 0xFF) * op);
        aBody = (ab << 24) | (aBody & 0x00FFFFFF);
    }
    u32 deep  = scale_rgb(pal[0], 1.30f);                // LIFTED deep base -> tonal dips stay colourful
    // MP : the blue STRENGTH tracks the MP level (full = vivid, low = faded), NO pulse.
    float bodyMul = (kind == 1) ? (0.42f + 0.58f * fill) : pul;
    dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    // TONAL VARIATION along the length: split the body into segments, each shaded a
    // little lighter/darker (BRIGHTNESS only -> stays in hue), drifting with the flow.
    const int NS = 12;
    const float TAU = 6.2831853f;
    float ph = (float)kind * 2.10f;                      // per-bar PHASE -> the 3 fioles never sync
    for (int s = 0; s < NS; s++) {
        float fL = (float)s / NS, fR = (float)(s + 1) / NS;
        float vL = 0.5f + 0.32f * sinf(fL * TAU * 1.5f + t * 0.9f + ph) + 0.18f * sinf(fL * TAU * 3.0f - t * 1.3f + ph * 1.7f);
        float vR = 0.5f + 0.32f * sinf(fR * TAU * 1.5f + t * 0.9f + ph) + 0.18f * sinf(fR * TAU * 3.0f - t * 1.3f + ph * 1.7f);
        if (vL < 0) vL = 0; if (vL > 1) vL = 1; if (vR < 0) vR = 0; if (vR > 1) vR = 1;
        // MIX THROUGH THE PALETTE'S OWN COLOURS (deep -> mid -> bright): the variation
        // is a gradient of the HUE, all saturated -> stays colourful, never black.
        u32 cTL = aBody | (scale_rgb(lerp_argb(pal[1], pal[2], vL), bodyMul) & 0x00FFFFFF);
        u32 cTR = aBody | (scale_rgb(lerp_argb(pal[1], pal[2], vR), bodyMul) & 0x00FFFFFF);
        u32 cBL = aBody | (scale_rgb(lerp_argb(deep, pal[1], vL), bodyMul) & 0x00FFFFFF);
        u32 cBR = aBody | (scale_rgb(lerp_argb(deep, pal[1], vR), bodyMul) & 0x00FFFFFF);
        float sx = x + fw * fL, sw = fw * (fR - fL);
        float su0 = flow + uSpan * fL, su1 = flow + uSpan * fR;
        tquad4(dev, sx, y, sw, h, su0, su1, vWob, vWob + 1.0f, cTL, cTR, cBL, cBR);
    }

    // --- DEPTH CAUSTIC : a fainter 2nd layer scrolling the OTHER way + a V-offset ->
    // parallax shimmer. alpha comes from the VERTEX (caller set ALPHAOP=SELECTARG1). ---
    u32 cawT = mulA(scale_rgb(pal[1], 1.10f), 0.26f);
    u32 cawB = mulA(scale_rgb(pal[1], 0.62f), 0.26f);
    float flow2 = 0.37f - t * (0.04f + 0.07f * spd);
    tquad_v(dev, x, y, fw, h, flow2, flow2 + uSpan * 1.3f, 0.23f - vWob, 1.23f - vWob, cawT, cawB);

    // --- TRAVELING SPECULAR : a bright top-third band, ADDITIVE, scrolling FASTER
    // than the body -> light glints sliding through the liquid (the #1 "fluid" cue). ---
    dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_ONE);
    float specS = t * (0.16f + 0.18f * spd);
    int sa = (int)(0x28 * glowf); if (sa > 255) sa = 255; if (sa < 0) sa = 0;
    u32 spec  = ((u32)sa << 24) | (pal[2] & 0x00FFFFFF);
    u32 spec0 = spec & 0x00FFFFFF;                        // alpha 0 -> FEATHERED ends (no hard edge)
    float su1 = specS + uSpan * 0.8f;
    tquad_v(dev, x, y + h * 0.04f, fw, h * 0.20f, specS, su1, 0.05f, 0.25f, spec0, spec);  // fade IN
    tquad_v(dev, x, y + h * 0.24f, fw, h * 0.22f, specS, su1, 0.25f, 0.45f, spec, spec0);  // fade OUT

    // --- EMISSIVE bloom (TP ONLY): the violet fluid GLOWS from within (additive,
    // modulated by the texture). Off below 1000, steady throb 1000-2999, fast flash 3000. ---
    if (kind == 2) {
        int tier = tp_tier(fill);
        float ep;
        if (tier >= 3) { float s = 0.5f + 0.5f * sinf(t * 5.0f); s = s * s * s; ep = 0.40f + 0.60f * s; }
        else           { ep = 0.50f + 0.50f * sinf(t * 3.0f); }
        float eb = 0x28 + 0xA8 * fill;                                  // irradiation grows with TP
        if (tier < 1) eb *= 0.30f;                                      // dormant while charging (<1000)
        int ea = (int)(eb * ep); if (ea > 255) ea = 255; if (ea < 0) ea = 0;
        u32 eTop = ((u32)ea           << 24) | (pal[2] & 0x00FFFFFF);   // bright accent, top
        u32 eBot = ((u32)(ea * 2 / 3) << 24) | (pal[1] & 0x00FFFFFF);   // mid, bottom
        tquad_v(dev, x, y, fw, h, flow, flow + uSpan, vWob, vWob + 1.0f, eTop, eBot);
    }

    dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);  // leave a known blend state for the next bar
}

// a soft coloured glow centred on the liquid surface (sx), reading the exact fill
// level at a glance: a dark "thickness" band + an additive colour halo + a core line.
static void surface_glow(u32 dev, float sx, float y, float h, u32 c)
{
    dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    grad_quad(dev, sx - 7.0f, y, 7.0f, h, 0x00000000, 0x55000000, 0x00000000, 0x55000000);
    dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_ONE);   // additive
    const float gw = 6.0f;
    u32 c0 = c & 0x00FFFFFF;                       // same colour, alpha 0
    grad_quad(dev, sx - gw, y, gw, h, c0, c,  c0, c);    // fade up to the surface
    grad_quad(dev, sx,      y, gw, h, c,  c0, c,  c0);   // fade away past it
    u32 core = 0x88000000u | (scale_rgb(c, 1.6f) & 0x00FFFFFF);
    grad_quad(dev, sx - 1.0f, y, 2.0f, h, core, core, core, core);
}

// ============================ bubbles ======================================

// rising BUBBLES (boiling look) inside a bar's FILLED area. Additive, tinted.
// `bubble` is the bubble sprite; assumes textured FVF/modulate stage already set.
static void draw_bubbles(u32 dev, u32 bubble, float x, float y, float w, float h, float fill, float t, u32 rgb, float countMul, float sizeMul)
{
    if (!bubble) return;
    float fw = w * fill;
    if (fw < 6.0f) return;
    dSetTex(dev, 0, bubble);
    dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_ONE);                  // additive
    dSetTSS(dev, 0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
    dSetTSS(dev, 0, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
    dSetTSS(dev, 0, D3DTSS_ALPHAOP,   D3DTOP_SELECTARG1);        // blend alpha from VERTEX (shape in tex RGB)
    dSetTSS(dev, 0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);            // -> immune to the zoning alpha mis-sample
    int N = (int)((2.0f + fill * 24.0f) * countMul); if (N > 28) N = 28; // MORE bubbles when fuller
    float spdScale = 0.35f + 0.75f * fill;                       // more vigorous boil when full
    for (int i = 0; i < N; i++) {
        float baseSz = (2.6f + hash01(i * 7 + 3) * 4.2f) * sizeMul;
        float spd = (0.16f + hash01(i * 5 + 2) * 0.30f) * spdScale;
        float ph  = hash01(i * 3 + 5);
        float bx  = hash01(i * 2 + 1);                          // base x fraction
        float wob = 0.6f + hash01(i * 11 + 1) * 1.6f;
        float p = t * spd + ph; p -= floorf(p);                 // 0..1 rise (loops)
        float swell = 0.30f + 0.70f * p;                        // grows as it climbs
        float pulse = 1.0f + 0.22f * sinf(t * (3.0f + hash01(i * 13 + 2) * 3.0f) + i * 2.1f);
        float sz = baseSz * swell * pulse;
        float cx = x + bx * fw + sinf(t * wob + i * 1.7f) * 6.0f + sinf(t * wob * 2.3f + i) * 2.5f;
        float cy = (y + h - baseSz) - p * (h - 2.0f * baseSz);  // rise bottom -> top
        float minx = x + baseSz, maxx = x + fw - baseSz; if (maxx < minx) maxx = minx;
        if (cx < minx) cx = minx; if (cx > maxx) cx = maxx;
        float af;                                               // fade in fast, POP out near the top
        if (p < 0.10f) af = p / 0.10f; else if (p > 0.70f) af = (1.0f - p) / 0.30f; else af = 1.0f;
        if (af < 0) af = 0;
        int a = (int)(0x90 * af * (0.4f + 0.6f * fill)); if (a > 255) a = 255; if (a < 0) a = 0;
        u32 col = ((u32)a << 24) | (rgb & 0x00FFFFFF);
        glow_quad(dev, cx - sz, cy - sz, sz * 2.0f, sz * 2.0f, col);
    }
    dSetTSS(dev, 0, D3DTSS_ALPHAOP,   D3DTOP_MODULATE);          // restore texture-alpha pipeline
    dSetTSS(dev, 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    dSetTSS(dev, 0, D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
    dSetTSS(dev, 0, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
}

// ============================ caps + glass =================================

// draw `tex` (back or front cap) at both ends of every bar. Caller sets blend/FVF.
static void draw_cap_pass(u32 dev, u32 tex, const float* ys, float x, float w, float h)
{
    if (!tex) return;
    dSetTex(dev, 0, tex);
    float capH = h * 1.42f, capW = capH * ((float)CAP_W / (float)CAP_H);
    for (int i = 0; i < 3; i++) {
        float cy = ys[i] + h * 0.5f - capH * 0.5f;
        cap_quad(dev, x - capW * 0.70f,     cy, capW, capH, true);    // left end (mirrored)
        cap_quad(dev, x + w - capW * 0.30f, cy, capW, capH, false);   // right end
    }
}

// GLASS = the FRONT FACE of a horizontal glass TUBE: pure curvature shading, NO
// border/rim. Every gradient fades to alpha 0 at its inner edge so there are no
// hard lines -- it reads as a smooth curved surface. FVF 0x44.
static void glass_overlay(u32 dev, float x, float y, float w, float h)
{
    dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    grad_quad(dev, x, y, w, h,         0x14000000, 0x14000000, 0x14000000, 0x14000000); // uniform tint
    grad_quad(dev, x, y, w, h,         0x00000000, 0x00000000, 0x90000000, 0x90000000); // smooth curve darken to bottom
    grad_quad(dev, x, y, w, h * 0.40f, 0x66000000, 0x66000000, 0x00000000, 0x00000000); // soft top rim only

    dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_ONE);
    float pk = h * 0.20f;
    grad_quad(dev, x, y,      w, pk,        0x00FFFFFF, 0x00FFFFFF, 0x6CFFFFFF, 0x6CFFFFFF); // specular rise to peak
    grad_quad(dev, x, y + pk, w, h * 0.22f, 0x6CFFFFFF, 0x6CFFFFFF, 0x00FFFFFF, 0x00FFFFFF); // fall to 0

    float by = y + h * 0.72f, bh = h * 0.22f;
    grad_quad(dev, x, by,             w, bh * 0.5f, 0x00FFFFFF, 0x00FFFFFF, 0x1CFFFFFF, 0x1CFFFFFF); // faint bounce band
    grad_quad(dev, x, by + bh * 0.5f, w, bh * 0.5f, 0x1CFFFFFF, 0x1CFFFFFF, 0x00FFFFFF, 0x00FFFFFF);
}

// ============================ TP electricity ===============================

// draw ONE width pass of a precomputed polyline, up to `grow` (0..1) of its length.
// `tipTaper` thins the stroke toward the tip (0 = uniform, 0.6 = down to 40%).
static void draw_pass(u32 dev, const float* px, const float* py, int N, u32 col, float th, float grow, int reverse, float tipTaper)
{
    int drawn = (int)(grow * N + 0.999f); if (drawn < 1) drawn = 1; if (drawn > N) drawn = N;
    if (!reverse) { for (int i = 0; i < drawn; i++)          { float ww = th * (1.0f - tipTaper * ((i + 0.5f) / N)); seg_soft(dev, px[i], py[i], px[i + 1], py[i + 1], ww, col); } }
    else          { for (int i = N - 1; i >= N - drawn; i--) { float ww = th * (1.0f - tipTaper * ((i + 0.5f) / N)); seg_soft(dev, px[i], py[i], px[i + 1], py[i + 1], ww, col); } }
}

// an electric arc ENCIRCLING the round cross-section of the horizontal glass tube:
// a VERTICAL arc curved horizontally (max bulge at the front, zero at the edges) +
// a wriggle -> reads as energy wrapping AROUND the cylinder.
static void draw_ring(u32 dev, float xc, float y, float h, float bulge, float tilt, int seed, float t,
                      float grow, int reverse, float inten, float lf,
                      float clx0, float cly0, float clx1, float cly1)
{
    const int N = 20;
    const float TAU = 6.2831853f;
    float px[N + 1], py[N + 1];
    float side = (hash01(seed * 11 + 5) > 0.5f) ? 1.0f : -1.0f;     // wrap to the front-left or front-right
    float fS = 2.0f + 2.5f * hash01(seed * 3 + 1);
    float sS = 1.2f + 1.6f * hash01(seed * 7 + 3);                  // crawl/wriggle speed
    float ph = hash01(seed * 13 + 6) * TAU;
    for (int i = 0; i <= N; i++) {
        float vv = (float)i / N;                                    // 0 top .. 1 bottom
        float prof = sinf(vv * 3.14159265f);                        // 0 at edges, 1 at the front
        float xoff = tilt * (vv - 0.5f)                             // DIAGONAL slant
                   + side * bulge * prof
                   + bulge * 0.4f * prof * sinf(vv * fS * TAU + t * sS + ph); // round bulge + wriggle
        float ux = xc + xoff;
        float uy = y + h * vv;
        if (ux < clx0) ux = clx0; if (ux > clx1) ux = clx1;
        if (uy < cly0) uy = cly0; if (uy > cly1) uy = cly1;
        px[i] = ux; py[i] = uy;
    }
    int a;
    a = (int)(0x30 * inten * lf); if (a > 255) a = 255; if (a > 0) draw_pass(dev, px, py, N, ((u32)a << 24) | 0x004018C8, 46.0f, grow, reverse, 0.0f); // HUGE broad VIOLET glow
    a = (int)(0x4C * inten * lf); if (a > 255) a = 255; if (a > 0) draw_pass(dev, px, py, N, ((u32)a << 24) | 0x006A28E6, 24.0f, grow, reverse, 0.0f); // strong violet glow
    a = (int)(0x60 * inten * lf); if (a > 255) a = 255; if (a > 0) draw_pass(dev, px, py, N, ((u32)a << 24) | 0x00A878FF,  7.0f, grow, reverse, 0.0f); // mid violet
    a = (int)(0xB4 * inten * lf); if (a > 255) a = 255; if (a > 0) draw_pass(dev, px, py, N, ((u32)a << 24) | 0x00E2CCFF,  1.8f, grow, reverse, 0.0f); // violet-white core
}

// TP electricity = arcs ENCIRCLING the glass tube, drawn INSIDE the liquid (the
// glass is painted OVER them). From 2000, ramping up to 3000. Each arc lights the
// liquid LOCALLY around itself, not the whole fill.
static void draw_tp_lightning(u32 dev, float x, float y, float w, float h, float fill, float t)
{
    if (tp_tier(fill) < 2) return;                                 // from 2000, ramping up to 3000
    float fwT = w * fill; if (fwT < 12.0f) return;
    float c = (fill - 0.6667f) / 0.3333f; if (c < 0) c = 0; if (c > 1) c = 1;  // 2000 -> 3000 ramp
    int   nArcs    = (c > 0.55f) ? 2 : 1;                         // 1 small arc at 2000, 2 toward 3000
    float intenMul = 0.20f + 0.80f * c;                          // much fainter at 2000
    float curveMul = 0.40f + 0.60f * c;                          // much smaller bulge/tilt at 2000

    dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_ONE);                    // additive

    for (int k = 0; k < nArcs; k++) {
        float life  = (0.70f + 0.50f * hash01(k * 17 + 3)) * (1.6f - 0.6f * c);  // rarer at 2000
        float phase = t / life + hash01(k * 29 + 7) * 13.0f;
        int   strike = (int)phase;
        float fr = phase - (float)strike;
        if (fr > 0.36f) continue;
        int   seed = k * 1009 + strike * 131 + 1;
        float grow, inten;
        if (fr < 0.14f) { grow = fr / 0.14f;             inten = 0.6f + 0.4f * grow; }
        else            { grow = 1.0f;                   inten = 1.0f - (fr - 0.14f) / 0.22f; }
        if (inten < 0.0f) inten = 0.0f;
        inten *= (0.65f + 0.35f * hash01(strike * 53 + k * 7)) * intenMul;   // fainter at 2000
        int   reverse = (hash01(seed * 23 + 8) > 0.5f) ? 1 : 0;

        float xc    = x + (0.12f + 0.76f * hash01(seed * 5 + 2)) * fwT;
        float bulge = h * (0.18f + 0.16f * hash01(seed * 11 + 4)) * curveMul;
        float tilt  = (hash01(seed * 19 + 9) - 0.5f) * 1.7f * h * curveMul;

        // LOCALISED liquid impact: a soft violet glow column right where THIS arc is.
        int la = (int)(0x58 * inten); if (la > 255) la = 255;
        u32 lc = ((u32)la << 24) | 0x005A24DC;                    // VIOLET impact (inside the liquid)
        seg_soft(dev, xc, y, xc, y + h, h * 0.85f, lc);

        draw_ring(dev, xc, y, h, bulge, tilt, seed, t, grow, reverse, inten, 1.0f, x, y, x + fwT, y + h);
    }
}

// ============================ widget =======================================

void LiquidBars::on_device_lost()
{
    // the old device may be dead -> forget the handles WITHOUT releasing them.
    tex_[0] = tex_[1] = tex_[2] = 0;
    cap_front_ = cap_back_ = glow_ = bubble_ = 0;
    flash_prev_tier_ = -1; flash_ = 0.0f;   // re-baseline the unlock flash on the new device
}

void LiquidBars::ensure(u32 dev)
{
    if (!tex_[0]) {
        __try { for (int v = 0; v < 3; v++) tex_[v] = make_liquid_texture(dev, v); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    if (!cap_front_) {
        __try {
            cap_front_ = load_raw_texture(dev, CAP_FRONT, CAP_W, CAP_H);
            cap_back_  = load_raw_texture(dev, CAP_BACK,  CAP_W, CAP_H);
            glow_      = make_glow(dev);
            bubble_    = make_bubble(dev);
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
}

void LiquidBars::dispose()
{
    release_texture(tex_[0]); release_texture(tex_[1]); release_texture(tex_[2]);
    release_texture(cap_front_); release_texture(cap_back_); release_texture(glow_); release_texture(bubble_);
    tex_[0] = tex_[1] = tex_[2] = 0;
    cap_front_ = cap_back_ = glow_ = bubble_ = 0;
}

void LiquidBars::draw(const Frame& f)
{
    return;                                 // HIDDEN for now (set aside, not deleted) -- remove this line to re-enable
    if (!visible_) return;                  // honour the descriptor's manual hide
    u32 dev = f.dev; float t = f.t;
    if (!tex_[0] || !tex_[1] || !tex_[2]) return;

    // origin from the layout descriptor (set_place) ; bars stacked downward from py_.
    const float x = px_, w = 560.0f, h = 70.0f;
    const float ys[3]   = { py_, py_ + 114.0f, py_ + 228.0f };   // 114 = h + gap (caps don't touch)
    const u32*  pals[3] = { PAL_HP, PAL_MP, PAL_TP };
    const float toff[3] = { 0.0f, 13.7f, 27.3f };       // per-bar time offset -> desynced motion
    const float fill[3] = { state_->hp, state_->mp, state_->tp };

    dSetVS(dev, FVF_XYZRHW_DIFFUSE_TEX1);
    dSetRS(dev, D3DRS_ZENABLE, 0);
    dSetRS(dev, D3DRS_CULLMODE, D3DCULL_NONE);
    dSetRS(dev, D3DRS_LIGHTING, 0);
    // neutralise the game's environment so it can't tint our quads (fog/specular/
    // alpha-test during zoning etc.). The state block restores all this afterwards.
    dSetRS(dev, D3DRS_FOGENABLE, 0);
    dSetRS(dev, D3DRS_SPECULARENABLE, 0);
    dSetRS(dev, D3DRS_ALPHATESTENABLE, 0);
    dSetRS(dev, D3DRS_COLORWRITEENABLE, 0x0000000F);   // write RGBA
    dSetRS(dev, D3DRS_TEXTUREFACTOR, 0xFFFFFFFF);
    dSetRS(dev, D3DRS_BLENDOP, D3DBLENDOP_ADD);        // force ADD (game may leave MIN/MAX mid-load)
    dSetRS(dev, D3DRS_WRAP0, 0);                       // no cylindrical wrap on our tiled UVs
    dSetRS(dev, D3DRS_ALPHABLENDENABLE, 1);
    dSetRS(dev, D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    // final = texture * diffuse  (both colour and alpha)
    dSetTSS(dev, 0, D3DTSS_COLOROP,   D3DTOP_MODULATE);
    dSetTSS(dev, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    dSetTSS(dev, 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    dSetTSS(dev, 0, D3DTSS_ALPHAOP,   D3DTOP_MODULATE);
    dSetTSS(dev, 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    dSetTSS(dev, 0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    dSetTSS(dev, 0, D3DTSS_ADDRESSU,  D3DTADDRESS_WRAP);
    dSetTSS(dev, 0, D3DTSS_ADDRESSV,  D3DTADDRESS_WRAP);
    // force our own texture filtering so the game's filter state (esp. MIP) can't
    // mangle our single-mip textures while a zone loads.
    dSetTSS(dev, 0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
    dSetTSS(dev, 0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
    dSetTSS(dev, 0, D3DTSS_MIPFILTER, D3DTEXF_NONE);
    dSetTSS(dev, 0, D3DTSS_TEXCOORDINDEX, 0);                     // use our UV set, no game transform
    dSetTSS(dev, 0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    dSetTSS(dev, 1, D3DTSS_COLOROP, D3DTOP_DISABLE);              // kill stage 1 (game may leave multitexturing on)
    dSetTSS(dev, 1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

    // BACK cap rim (behind the liquid). CLAMP addressing for cap art.
    dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    dSetTSS(dev, 0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
    dSetTSS(dev, 0, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
    draw_cap_pass(dev, cap_back_, ys, x, w, h);
    dSetTSS(dev, 0, D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);   // wrap again for the liquid
    dSetTSS(dev, 0, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);

    // TP radiation : a rounded radial glow BEHIND the liquid (additive, tinted).
    {
        int tt = tp_tier(fill[2]);
        float gIc = (tt < 1) ? 0.0f : (0.45f + 0.55f * fill[2]);   // off <1000, grows with the charge
        if (layers_ >= 4 && glow_ && gIc > 0.02f) {
            // 1000-2999 = "WS ready": ONE steady throb. 3000 = "Aftermath ready": a
            // SPECIAL fast, sharp FLASHING pulse. Phase the halo to LAG the liquid's
            // emissive pulse so the halo arrives toward the END of the liquid's peak.
            float tg = t + toff[2];
            float gp;
            if (tt >= 3) {
                float s = 0.5f + 0.5f * sinf((tg - 0.14f) * 5.0f); s = s * s * s;  // flash, lagging the liquid
                gp = 0.32f + 0.52f * s;
            } else {
                gp = 0.52f + 0.46f * sinf(tg * 3.0f - 1.0f);            // peak ~1 rad AFTER the liquid
            }
            int ga = (int)(0xCC * gIc * gp); if (ga > 255) ga = 255; if (ga < 0) ga = 0;
            u32 gdyn[3]; tp_palette(fill[2], gdyn);                // glow tint follows the continuous palette
            // toward 3000 the halo shifts from violet to ELECTRIC BLUE (charged state).
            float bblend = (fill[2] - 0.6667f) / 0.3333f; if (bblend < 0) bblend = 0; if (bblend > 1) bblend = 1;
            u32 haloRGB = lerp_argb(gdyn[2] & 0x00FFFFFF, 0x0090DCFF, bblend) & 0x00FFFFFF;
            u32 gc = ((u32)ga << 24) | haloRGB;
            dSetTex(dev, 0, glow_);
            dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_ONE);             // additive
            dSetTSS(dev, 0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
            dSetTSS(dev, 0, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
            dSetTSS(dev, 0, D3DTSS_ALPHAOP,   D3DTOP_SELECTARG1);   // blend alpha from VERTEX (shape in tex RGB)
            dSetTSS(dev, 0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);       // -> immune to the zoning alpha mis-sample
            const float mx = 46.0f, my = 12.0f;                    // less vertical spill above/below the bar
            float fwT = w * fill[2];                                // glow only over the FILLED part
            glow_quad(dev, x - mx, ys[2] - my, fwT + 2 * mx, h + 2 * my, gc);
            dSetTSS(dev, 0, D3DTSS_ALPHAOP,   D3DTOP_MODULATE);     // restore texture-alpha pipeline
            dSetTSS(dev, 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
            dSetTSS(dev, 0, D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
            dSetTSS(dev, 0, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
        }
    }

    // keep our MANAGED fluid textures resident: the game evicts them from VRAM under
    // pressure while loading a zone, so they'd sample wrong (alpha/lum).
    for (int i = 0; i < 3; i++) preload_texture(tex_[i]);

    // liquid bars (each a different cell layout, desynced)
    dSetTSS(dev, 0, D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
    dSetTSS(dev, 0, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
    // ZONING-GLITCH FIX (docs/REFERENCE.md): a texture's ALPHA channel mis-samples as
    // ~255 while a zone loads (its RGB stays correct), so take the liquid's blend
    // alpha from the VERTEX. RGB stays MODULATE (texture * diffuse). Caps keep texture
    // alpha (they need it for shape and are stable at the safe 0/255 extremes).
    dSetTSS(dev, 0, D3DTSS_ALPHAOP,   D3DTOP_SELECTARG1);
    dSetTSS(dev, 0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    for (int i = 0; i < 3; i++) {
        const u32* pal = pals[i];
        u32 dyn[3];
        if      (i == 0) { hp_palette(fill[0], dyn); pal = dyn; }   // HP: green/orange/red
        else if (i == 2) { tp_palette(fill[2], dyn); pal = dyn; }   // TP: continuous tiers
        dSetTex(dev, 0, tex_[i]);
        draw_bar(dev, x, ys[i], w, h, t + toff[i], pal, fill[i], i);
    }
    dSetTSS(dev, 0, D3DTSS_ALPHAOP,   D3DTOP_MODULATE);   // restore texture-alpha for bubbles/caps
    dSetTSS(dev, 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);

    // RISING BUBBLES inside the MP liquid (textured, under the glass = in the fiole).
    if (layers_ >= 4)
        draw_bubbles(dev, bubble_, x, ys[1], w, h, fill[1], t, 0x00A0E8FF, 1.0f, 1.0f);   // MP : boiling magic bubbles

    // --- GLASS : untextured, pure diffuse geometry ---
    dSetVS(dev, FVF_XYZRHW_DIFFUSE);
    dSetTex(dev, 0, 0);
    dSetTSS(dev, 0, D3DTSS_COLOROP,   D3DTOP_SELECTARG1);
    dSetTSS(dev, 0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    dSetTSS(dev, 0, D3DTSS_ALPHAOP,   D3DTOP_SELECTARG1);
    dSetTSS(dev, 0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);

    // TP ELECTRICITY drawn UNDER the glass (so it reads as INSIDE the fiole). Arcs
    // (>=2000). FVF 0x44 / SELECTARG1-DIFFUSE is set.
    if (layers_ >= 4)
        draw_tp_lightning(dev, x, ys[2], w, h, fill[2], t);

    // UNLOCK FLASH: a bright blue-white pulse over the TP fill when you cross a WS
    // threshold (1000/2000/3000) upward -> satisfying "tier unlocked" feedback.
    {
        int curTier = tp_tier(fill[2]);
        if (flash_prev_tier_ < 0) flash_prev_tier_ = curTier;          // first frame: no flash
        else if (curTier > flash_prev_tier_) flash_ = 1.0f;            // crossed a threshold up
        flash_prev_tier_ = curTier;
        if (flash_ > 0.0f) {
            float fwT = w * fill[2];
            int fa = (int)(0xC0 * flash_ * flash_); if (fa > 255) fa = 255;
            u32 c  = ((u32)fa << 24) | 0x00C8E0FF;                       // bright blue-white
            u32 c0 = c & 0x00FFFFFF;
            dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_ONE);
            grad_quad(dev, x, ys[2],            fwT, h * 0.5f, c0, c0, c,  c );
            grad_quad(dev, x, ys[2] + h * 0.5f, fwT, h * 0.5f, c,  c,  c0, c0);
            flash_ -= 0.045f; if (flash_ < 0.0f) flash_ = 0.0f;          // ~0.35s decay
        }
    }

    for (int i = 0; i < 3; i++) {
        glass_overlay(dev, x, ys[i], w, h);

        // 3000 = the GLASS itself is CHARGED: a flickering electric-blue rim glow on
        // the top & bottom edges (Saint-Elmo's fire) + a faint blue sheen.
        if (i == 2 && tp_tier(fill[2]) >= 3) {
            float fl = 0.40f + 0.30f * sinf(t * 12.0f) + 0.20f * sinf(t * 29.0f + 2.1f);
            fl *= 0.65f + 0.70f * hash01((int)(t * 28.0f) * 7 + 3);              // stepped crackle
            if (fl < 0.10f) fl = 0.10f; if (fl > 1.0f) fl = 1.0f;
            float fwT = w * fill[2];
            dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_ONE);                          // additive
            int ag = (int)(0x26 * fl); if (ag > 255) ag = 255;     // subtle / transparent
            int ac = (int)(0x5C * fl); if (ac > 255) ac = 255;
            u32 cg = ((u32)ag << 24) | 0x002C9CFF, cg0 = cg & 0x00FFFFFF;        // soft inward glow
            u32 cc = ((u32)ac << 24) | 0x0090DCFF;                               // bright crackling edge line
            grad_quad(dev, x, ys[2],             fwT, h * 0.16f, cg,  cg,  cg0, cg0); // top: glow fades inward
            grad_quad(dev, x, ys[2],             fwT, 2.0f,      cc,  cc,  cc,  cc ); // top: bright edge line
            grad_quad(dev, x, ys[2] + h * 0.84f, fwT, h * 0.16f, cg0, cg0, cg,  cg ); // bottom: glow
            grad_quad(dev, x, ys[2] + h - 2.0f,  fwT, 2.0f,      cc,  cc,  cc,  cc ); // bottom: bright edge line
        }

        // meniscus : a bright line at the liquid surface = read the level at a glance
        float fwi = w * fill[i];
        if (fwi > 2.0f && fwi < w - 1.0f) {
            u32 mp[3]; const u32* pl = pals[i];
            if      (i == 0) { hp_palette(fill[0], mp); pl = mp; }
            else if (i == 2) { tp_palette(fill[2], mp); pl = mp; }
            u32 men = 0x40000000u | (scale_rgb(pl[1], 1.3f) & 0x00FFFFFF);
            surface_glow(dev, x + fwi, ys[i], h, men);   // locked at the EXACT fill level
        }
    }

    // FRONT cap (over everything). Back to textured FVF + modulate + clamp.
    dSetVS(dev, FVF_XYZRHW_DIFFUSE_TEX1);
    dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    dSetTSS(dev, 0, D3DTSS_COLOROP,   D3DTOP_MODULATE);
    dSetTSS(dev, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    dSetTSS(dev, 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    dSetTSS(dev, 0, D3DTSS_ALPHAOP,   D3DTOP_MODULATE);
    dSetTSS(dev, 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    dSetTSS(dev, 0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    dSetTSS(dev, 0, D3DTSS_ADDRESSU,  D3DTADDRESS_CLAMP);
    dSetTSS(dev, 0, D3DTSS_ADDRESSV,  D3DTADDRESS_CLAMP);
    draw_cap_pass(dev, cap_front_, ys, x, w, h);
}

// ---- shared provider : the HUD hands its LiquidBars here so party/help can borrow the real assets ----
static LiquidBars* g_vialProvider = nullptr;
void        set_vial_provider(LiquidBars* p) { g_vialProvider = p; }
LiquidBars* vial_provider()                  { return g_vialProvider; }

// Draw ONE fiole into (x,y,w,h) reusing this widget's textures + the file-static helpers above. This is
// LiquidBars::draw() reduced to a single, arbitrarily-sized bar (no per-instance unlock flash). The core
// identity -- glass caps + streaming liquid + curved glass -- scales cleanly ; the big absolute-px extras
// (TP halo / electricity) are tuned for the 70px showcase bars and are left to the full widget.
void LiquidBars::draw_vial_scaled(u32 dev, float t, float x, float y, float w, float h, int kind, float fill01, u32 col, float pulse, float danger, int layers)
{
    if (kind < 0 || kind > 2 || !vial_ready()) return;
    if (fill01 < 0.0f) fill01 = 0.0f; if (fill01 > 1.0f) fill01 = 1.0f;

    const u32* PALS[3] = { PAL_HP, PAL_MP, PAL_TP };
    const u32* pal = PALS[kind];
    u32 dyn[3];
    if      (kind == 0) { hp_palette(fill01, dyn); pal = dyn; }   // HP : green / orange / red
    else if (kind == 2) { tp_palette(fill01, dyn); pal = dyn; }   // TP : continuous tiers

    // BASE bar effects : the SAME luminous aura the bar style draws BEHIND the gauge -- WS-ready pulse
    // (TP >= 1000), critical-HP red alarm, and a faint always-on glow. Colour-quad state (untextured).
    {
        dSetVS(dev, FVF_XYZRHW_DIFFUSE); dSetTex(dev, 0, 0);
        dSetRS(dev, D3DRS_ALPHABLENDENABLE, 1); dSetRS(dev, D3DRS_SRCBLEND, D3DBLEND_SRCALPHA); dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        dSetTSS(dev, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1); dSetTSS(dev, 0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
        dSetTSS(dev, 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1); dSetTSS(dev, 0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
        dSetTSS(dev, 1, D3DTSS_COLOROP, D3DTOP_DISABLE);
        const float cyc = y + h * 0.5f;
        if (pulse > 0.0f) {                                  // WS-ready glow breathing
            float ph = 0.5f + 0.5f * sinf(t * 7.5f);
            u32 g1 = (col & 0x00FFFFFF) | ((u32)((0.30f + 0.40f * ph) * pulse * 255) << 24);
            grad_quad(dev, x - 3, y - 3, w + 6, h + 6, g1, g1, g1, g1);
            u32 g2 = (col & 0x00FFFFFF) | ((u32)((0.55f + 0.40f * ph) * pulse * 255) << 24);
            grad_quad(dev, x - 1, y - 2, w + 2, h + 4, g2, g2, g2, g2);
        }
        if (danger > 0.0f) {                                 // CRITICAL HP : red alarm halo breathing
            float dh = 0.5f + 0.5f * sinf(t * 7.5f);
            u32 d1 = 0x00FF2A2A | ((u32)((0.32f + 0.48f * dh) * danger * 255) << 24);
            grad_quad(dev, x - 3, y - 3, w + 6, h + 6, d1, d1, d1, d1);
            u32 d2 = 0x00FF2A2A | ((u32)((0.55f + 0.40f * dh) * danger * 255) << 24);
            grad_quad(dev, x - 1, y - 2, w + 2, h + 4, d2, d2, d2, d2);
        }
        { u32 gl = (col & 0x00FFFFFF) | 0x1C000000; soft_blob(dev, x + w * 0.5f, cyc, w * 0.58f, h * 0.9f, gl); }
    }

    // --- full textured state (same block as LiquidBars::draw, so it survives the game's leftovers) ---
    dSetVS(dev, FVF_XYZRHW_DIFFUSE_TEX1);
    dSetRS(dev, D3DRS_ZENABLE, 0); dSetRS(dev, D3DRS_CULLMODE, D3DCULL_NONE); dSetRS(dev, D3DRS_LIGHTING, 0);
    dSetRS(dev, D3DRS_FOGENABLE, 0); dSetRS(dev, D3DRS_SPECULARENABLE, 0); dSetRS(dev, D3DRS_ALPHATESTENABLE, 0);
    dSetRS(dev, D3DRS_COLORWRITEENABLE, 0x0000000F); dSetRS(dev, D3DRS_TEXTUREFACTOR, 0xFFFFFFFF);
    dSetRS(dev, D3DRS_BLENDOP, D3DBLENDOP_ADD); dSetRS(dev, D3DRS_WRAP0, 0);
    dSetRS(dev, D3DRS_ALPHABLENDENABLE, 1); dSetRS(dev, D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    dSetTSS(dev, 0, D3DTSS_COLOROP, D3DTOP_MODULATE); dSetTSS(dev, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE); dSetTSS(dev, 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    dSetTSS(dev, 0, D3DTSS_ALPHAOP, D3DTOP_MODULATE); dSetTSS(dev, 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE); dSetTSS(dev, 0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    dSetTSS(dev, 0, D3DTSS_MINFILTER, D3DTEXF_LINEAR); dSetTSS(dev, 0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR); dSetTSS(dev, 0, D3DTSS_MIPFILTER, D3DTEXF_NONE);
    dSetTSS(dev, 0, D3DTSS_TEXCOORDINDEX, 0); dSetTSS(dev, 0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    dSetTSS(dev, 1, D3DTSS_COLOROP, D3DTOP_DISABLE); dSetTSS(dev, 1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

    // NO caps : the liquid body fills the whole rect ; the glass overlay shapes the tube.
    const float bx = x, by = y, bw = w, bh = h;

    // LIQUID : wrap addressing + vertex-alpha (zoning-glitch fix). draw_bar carries the per-resource
    // behaviour : HP green->orange->red palette, TP transparency/emissive below vs above 1000, MP fade.
    preload_texture(tex_[kind]);
    dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    dSetTSS(dev, 0, D3DTSS_ADDRESSU, D3DTADDRESS_WRAP); dSetTSS(dev, 0, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
    dSetTSS(dev, 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1); dSetTSS(dev, 0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    dSetTex(dev, 0, tex_[kind]);
    draw_bar(dev, bx, by, bw, bh, t, pal, fill01, kind);
    dSetTSS(dev, 0, D3DTSS_ALPHAOP, D3DTOP_MODULATE); dSetTSS(dev, 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);

    // MP bubbles (only worth it when the body is tall enough to read them)
    if (layers >= 4 && kind == 1 && bh >= 26.0f)
        draw_bubbles(dev, bubble_, bx, by, bw, bh, fill01, t, 0x00A0E8FF, 1.0f, bh / 70.0f);

    // GLASS : untextured diffuse geometry
    dSetVS(dev, FVF_XYZRHW_DIFFUSE); dSetTex(dev, 0, 0);
    dSetTSS(dev, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1); dSetTSS(dev, 0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    dSetTSS(dev, 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1); dSetTSS(dev, 0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    glass_overlay(dev, bx, by, bw, bh);

    // CRITICAL HP : red wash over the fill so it visibly blinks (same as the bar style).
    dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    if (kind == 0 && danger > 0.0f) {
        float dl = 0.5f + 0.5f * sinf(t * 7.5f);
        float fwd = bw * fill01;
        u32 dw = 0x00FF1E1E | ((u32)(dl * danger * 0.55f * 255) << 24);
        grad_quad(dev, bx, by, fwd, bh, dw, dw, dw, dw);
    }

    // meniscus at the exact fill level
    float fwi = bw * fill01;
    if (fwi > 2.0f && fwi < bw - 1.0f) {
        u32 men = 0x40000000u | (scale_rgb(pal[1], 1.3f) & 0x00FFFFFF);
        surface_glow(dev, bx + fwi, by, bh, men);
    }
}

} // namespace aio
