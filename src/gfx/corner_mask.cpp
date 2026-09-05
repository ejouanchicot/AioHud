#include "gfx/corner_mask.h"
#include "gfx/texture.h"
#include "windower_debug.h"

namespace aio {

// One atlas, blocks laid left to right by radius : r occupies [x0(r), x0(r)+r) x [0, r).
// x0 is arithmetic, not a table -- it is just the sum of the smaller radii, so it cannot fall out of step
// with the bake loop the way a hand-maintained table would.
static const int CM_W = 512, CM_H = 32;            // sum(3..26) + 24 gutters = 372 <= 512 ; tallest block 26 <= 32
static u32  s_tex   = 0;
static int  s_tries = 0;                           // bounded retry (rule 10) -- re-armed by corner_mask_forget()
static bool s_on    = true;
static bool s_gaveUpLogged = false;
static bool s_userOff = false;             // //aio corners : the A/B switch, independent of the zoning gate

// ...plus a ONE-TEXEL transparent gutter after each block. At 1:1 with a snapped rect a bilinear tap lands
// exactly on a texel centre and reads that texel alone, so the gutter is never sampled -- it is there for the
// frame where something is half a pixel off and the tap reaches sideways. Without it that tap would pull the
// NEIGHBOURING RADIUS mask into the edge of this one. The same class of bleed the texture-stage addressing
// note in d3d.h describes, and 24 texels of width is a cheap way to make it impossible.
static int cm_x0(int r) { return ((r - 1) * r - (CM_RMIN - 1) * CM_RMIN) / 2 + (r - CM_RMIN); }

// RGB is WHITE everywhere, including in the fully transparent texels. A transparent texel still HAS a colour
// and the sampler interpolates colour and alpha independently -- leave the RGB black there and every edge
// texel pulls black into itself, which is the permanent dark halo the masthead logotype was drawn with for
// three sessions (audit-config-chrome-2026-09-05, bug 3). White costs nothing and cannot fringe.
static void bake(u32* buf) {
    for (int i = 0; i < CM_W * CM_H; ++i) buf[i] = 0x00FFFFFFu;
    for (int r = CM_RMIN; r <= CM_RMAX; ++r) {
        const int   x0 = cm_x0(r);
        const float cx = (float)r, cy = (float)r;        // the disc centre sits at the block's INNER corner
        const float rr = (float)r * (float)r;
        for (int j = 0; j < r; ++j) for (int i = 0; i < r; ++i) {
            int hit = 0;                                  // 16x16 samples -> coverage to within 1/256 of a texel
            for (int sy = 0; sy < 16; ++sy) {
                const float py = (float)j + ((float)sy + 0.5f) / 16.0f - cy;
                for (int sx = 0; sx < 16; ++sx) {
                    const float px = (float)i + ((float)sx + 0.5f) / 16.0f - cx;
                    if (px * px + py * py <= rr) ++hit;
                }
            }
            const u32 a = (u32)((hit * 255 + 128) / 256);
            buf[j * CM_W + (x0 + i)] = (a << 24) | 0x00FFFFFFu;
        }
    }
}

u32 corner_mask_tex(u32 dev) {
    if (s_tex) return s_tex;
    if (s_tries >= 8) return 0;                    // budget spent : the caller keeps drawing, with geometry
    ++s_tries;
    u32* buf = (u32*)HeapAlloc(GetProcessHeap(), 0, (size_t)CM_W * CM_H * 4);
    if (!buf) return 0;
    bake(buf);
    s_tex = make_texture_argb_mip(dev, CM_W, CM_H, buf);
    HeapFree(GetProcessHeap(), 0, buf);
    // SAY SO when the budget dies (rule 10's corollary : a probe that goes quiet reads like a bug that is not
    // happening). This is not fatal -- corners keep drawing the way they did before the mask existed.
    if (!s_tex && s_tries >= 8 && !s_gaveUpLogged) {
        s_gaveUpLogged = true;
        windower::debug::log("corner mask: CreateTexture failed 8 times -- rounded corners fall back to feathered geometry");
    }
    return s_tex;
}

bool corner_mask_uv(int r, float& u0, float& v0, float& u1, float& v1) {
    if (r < CM_RMIN || r > CM_RMAX) return false;
    const float x0 = (float)cm_x0(r);
    u0 = x0 / (float)CM_W; u1 = (x0 + (float)r) / (float)CM_W;
    v0 = 0.0f;             v1 = (float)r / (float)CM_H;
    return true;
}

void corner_mask_enable(bool on) { s_on = on; }
bool corner_mask_enabled()       { return s_on && !s_userOff; }
void corner_mask_user_off(bool off) { s_userOff = off; }
bool corner_mask_user_is_off()      { return s_userOff; }
void corner_mask_forget()        { s_tex = 0; s_tries = 0; s_gaveUpLogged = false; }   // FORGET only -- never Release
void corner_mask_dispose()       { if (s_tex) release_texture(s_tex); corner_mask_forget(); }   // the ONLY Release

} // namespace aio
