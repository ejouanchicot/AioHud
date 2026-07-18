// ui_colors.h -- the small ARGB helpers the widgets share (HP gradient, RGB scale, alpha fade, lerp).
//
// These are the widgets' OWN tuned versions and are intentionally SEPARATE from gfx/color.h : the gfx ones
// ROUND (+0.5f) each channel, these TRUNCATE (and lerp_color/scl clamp only the high side) -- matching the
// exact pixels the party / target / player boxes shipped. Do not "unify" them onto gfx/color.h without
// accepting a 1-level colour shift. Byte-for-byte what party.cpp / target.cpp / player.cpp used to inline.
#pragma once
#include "gfx/d3d.h"   // u32

namespace aio {

// linear-interpolate a full ARGB colour (truncating ; t clamped 0..1).
inline u32 lerp_color(u32 a, u32 b, float t) {
    if (t < 0.0f) t = 0.0f; if (t > 1.0f) t = 1.0f;
    int ar = (a >> 16) & 0xFF, ag = (a >> 8) & 0xFF, ab = a & 0xFF, aa = (a >> 24) & 0xFF;
    int br = (b >> 16) & 0xFF, bg = (b >> 8) & 0xFF, bb = b & 0xFF, ba = (b >> 24) & 0xFF;
    int r = ar + (int)((br - ar) * t), g = ag + (int)((bg - ag) * t), bl = ab + (int)((bb - ab) * t), al = aa + (int)((ba - aa) * t);
    return ((u32)al << 24) | ((u32)r << 16) | ((u32)g << 8) | (u32)bl;
}

// HP colour as a CONTINUOUS gradient (green 100..75 -> yellow 50 -> orange 25 -> red 0).
inline u32 hp_color(float p) {
    const u32 GRN = 0xFF6FDC74, YEL = 0xFFF2E173, ORG = 0xFFF6A862, RED = 0xFFFB5A5A;
    if (p >= 75.0f) return GRN;
    if (p >= 50.0f) return YEL;   // <75% -> YELLOW like the game (a yellow->green lerp read as green at ~72%)
    if (p >= 25.0f) return lerp_color(ORG, YEL, (p - 25.0f) / 25.0f);
    return lerp_color(RED, ORG, p / 25.0f);
}

// scale a colour's RGB by f (keep alpha ; clamp high side only -- callers pass f >= 0).
inline u32 scl(u32 c, float f) {
    int r = (int)(((c >> 16) & 0xFF) * f), g = (int)(((c >> 8) & 0xFF) * f), b = (int)((c & 0xFF) * f);
    if (r > 255) r = 255; if (g > 255) g = 255; if (b > 255) b = 255;
    return (c & 0xFF000000) | (r << 16) | (g << 8) | b;
}

// multiply a colour's ALPHA by a (0..1, rounded) -> whole-box fade.
inline u32 mul_a(u32 c, float a) {
    u32 al = (u32)(((c >> 24) & 0xFF) * a + 0.5f);
    return (al << 24) | (c & 0x00FFFFFF);
}

} // namespace aio
