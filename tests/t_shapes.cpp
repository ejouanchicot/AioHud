// t_shapes.cpp -- the rounded shapes draw THE SAME TRIANGLES, in the same order, with fewer draws (audit P8).
//
// WHY THIS EXISTS. //aio perf measured the rounded rects as the first cost of the HUD (1.05 ms a frame with a full
// alliance, up to 15 DrawPrimitiveUP per shape : 3 rects, 4 corner fans, 4 edge feathers, 4 corner rings). Merging
// them into one triangle list must not change a single pixel. A pixel comparison in game is impossible to make
// clean (the world behind the HUD moves between two captures), so the proof is taken one level lower, where it is
// exact : D3D rasterises triangle by triangle, in order, so the SAME TRIANGLES (every vertex, every colour, in the
// same order, under the same texture and vertex format) ARE the same pixels. Every draw the fake device recorded is
// expanded into its triangles exactly as D3D forms them from a strip or a fan, and hashed. The goldens below were
// taken from the code BEFORE the change (2026-11-24) ; the change must reproduce them with fewer draws.
//
// Both paths of every shape are covered : the feathered geometry, and the baked corner-mask blit (the fake device
// hands out a fake texture handle for it -- fakedev::g_fakeTexture).
#include "check.h"
#include "fake_device.h"
#include "gfx/draw.h"
#include "gfx/d3d.h"
#include "gfx/corner_mask.h"
#include <stdlib.h>

using namespace aio;

namespace {

struct Acc { unsigned geo, st; int tris, draws; };

void fnv(unsigned& h, const void* p, int n) {
    const unsigned char* b = (const unsigned char*)p;
    for (int i = 0; i < n; ++i) { h ^= b[i]; h *= 16777619u; }
}
void fnv_u(unsigned& h, unsigned v) { fnv(h, &v, 4); }
void fnv_f(unsigned& h, float f) { unsigned u; memcpy(&u, &f, 4); fnv_u(h, u); }
void fnv_vtx(unsigned& h, const fakedev::Vertex& v) {
    fnv_f(h, v.x); fnv_f(h, v.y); fnv_f(h, v.z); fnv_f(h, v.rhw); fnv_u(h, v.color);
    fnv_u(h, v.hasUV ? 1u : 0u); if (v.hasUV) { fnv_f(h, v.u); fnv_f(h, v.v); }
}

// Fold what the fake device recorded since reset() into the accumulator : every triangle (as D3D builds it from the
// topology) under the vertex format and stage-0 texture in force, and the device's EFFECTIVE state at every draw.
// THE STATE IN FORCE, NOT THE CALLS (2026-11-24, before P4) : filtering a redundant SetRenderState changes how many
// calls reach the device and nothing about what it draws with. So the device's live state (every render state,
// every stage state of stages 0-1, the bound textures, the vertex format) is replayed call by call here and hashed
// at each draw -- which is what the pixels depend on.
void fold(Acc& A) {
    const fakedev::Log& L = fakedev::log();
    unsigned fvf = 0, tex0 = 0; int vc = 0;
    static u32 rs[256], tss[2][32], tex[2];
    memset(rs, 0, sizeof(rs)); memset(tss, 0, sizeof(tss)); memset(tex, 0, sizeof(tex));
    // hashed only when it CHANGED since the last draw : the sequence of states drawn under, independent of how many
    // draws a shape is cut into (P8) and of how many redundant calls it makes (P4)
    bool dirty = true;
    auto setv = [&dirty](u32& slot, u32 v) { if (slot != v) { slot = v; dirty = true; } };
    for (int i = 0; i < L.nCall; ++i) {
        const fakedev::Call& c = L.call[i];
        switch (c.kind) {
            case fakedev::Call::SetFVF: setv(fvf, c.a); break;
            case fakedev::Call::SetTex: if (c.a == 0) tex0 = c.b; if (c.a < 2) setv(tex[c.a], c.b); break;
            case fakedev::Call::SetRS:  if (c.a < 256) setv(rs[c.a], c.b); break;
            case fakedev::Call::SetTSS: if (c.a < 2 && c.b < 32) setv(tss[c.a][c.b], c.c); break;
            case fakedev::Call::DrawUP: {
                ++A.draws;
                if (dirty) { dirty = false; fnv(A.st, rs, sizeof(rs)); fnv(A.st, tss, sizeof(tss)); fnv(A.st, tex, sizeof(tex)); fnv_u(A.st, fvf); }
                const int n = (int)c.d, type = (int)c.a, prims = (int)c.b;
                for (int k = 0; k < prims; ++k) {
                    int i0, i1, i2;
                    if (type == 4)      { i0 = 3 * k; i1 = 3 * k + 1; i2 = 3 * k + 2; }                 // list
                    else if (type == 5) { if (k & 1) { i0 = k; i1 = k + 2; i2 = k + 1; }               // strip : odd
                                          else       { i0 = k; i1 = k + 1; i2 = k + 2; } }             //   triangles reordered
                    else                { i0 = 0; i1 = k + 1; i2 = k + 2; }                            // fan
                    if (vc + i2 >= L.nVert || i2 >= n) { ++A.tris; continue; }                          // past the log : counted, not hashed
                    fnv_u(A.geo, fvf); fnv_u(A.geo, tex0);
                    fnv_vtx(A.geo, L.vert[vc + i0]); fnv_vtx(A.geo, L.vert[vc + i1]); fnv_vtx(A.geo, L.vert[vc + i2]);
                    ++A.tris;
                }
                vc += n;
                break;
            }
        }
    }
    // what the shape leaves behind counts too (rule 8 : no texture left bound, the 2D state restored)
    fnv(A.st, rs, sizeof(rs)); fnv(A.st, tss, sizeof(tss)); fnv(A.st, tex, sizeof(tex)); fnv_u(A.st, fvf);
}

const float POS[2][2]   = { { 10.0f, 20.0f }, { 10.25f, 20.75f } };
const float SIZE[5][2]  = { { 200.0f, 60.0f }, { 30.0f, 30.0f }, { 8.0f, 8.0f }, { 3.0f, 40.0f }, { 120.0f, 7.0f } };
const float RAD[8]      = { 0.0f, 0.5f, 1.0f, 3.0f, 6.0f, 12.0f, 26.0f, 40.0f };
const float FEATHER[3]  = { 0.0f, 1.2f, 2.0f };
const u32   CT = 0xE0102030u, CB = 0x80405060u;

enum Shape { S_RRECT, S_BORDERED, S_TOPCAPS, S_TB, S_LEFT, S_GLOW, S_STROKE, S_N };
const char* const NAME[S_N] = { "rrect", "rrect_bordered", "rrect_topcaps", "rrect_tb", "rrect_left", "rrect_glow", "rrect_stroke" };

void draw_one(int s, u32 dev, float x, float y, float w, float h, float r, float f) {
    switch (s) {
        case S_RRECT:    rrect(dev, x, y, w, h, r, CT, CB, f); break;
        case S_BORDERED: rrect_bordered(dev, x, y, w, h, r, CT, CB, 0xFF808080u, 1.0f + f, f); break;
        case S_TOPCAPS:  rrect_topcaps(dev, x, y, w, h, r, CT, CB, f); break;
        case S_TB:       rrect_tb(dev, x, y, w, h, r, r * 0.5f, CT, CB, f);
                         rrect_tb(dev, x, y, w, h, r, 0.0f, CT, CB, f); break;
        case S_LEFT:     rrect_left(dev, x, y, w, h, r, CT, CB, f); break;
        case S_GLOW:     rrect_glow(dev, x, y, w, h, r, CT, f > 0.0f ? f : 3.0f); break;
        case S_STROKE:   rrect_stroke(dev, x, y, w, h, r, CT, f > 0.0f ? f : 1.0f); break;
    }
}

Acc run(int s, bool masked) {
    fakedev::g_fakeTexture = masked ? 0xABC000u : 0u;
    corner_mask_forget();              // re-arm the mask's retry budget, so this mode really gets (or misses) its texture
    corner_mask_enable(true);
    Acc A = { 2166136261u, 2166136261u, 0, 0 };
    const u32 dev = fakedev::device();
    for (int p = 0; p < 2; ++p) for (int z = 0; z < 5; ++z) for (int r = 0; r < 8; ++r) for (int f = 0; f < 3; ++f) {
        fakedev::reset();
        dStateForget();                // a fresh device as far as the shadow knows (P4) : each case starts like a frame
        draw_one(s, dev, POS[p][0], POS[p][1], SIZE[z][0], SIZE[z][1], RAD[r], FEATHER[f]);
        fold(A);
    }
    fakedev::g_fakeTexture = 0; corner_mask_forget();
    return A;
}

// THE GOLDENS : triangles + state, taken from the code before the change. draws = the draws that code needed.
// (st re-taken 2026-11-24 when it became the EFFECTIVE state instead of the call sequence -- identical on the draw.cpp
// of before the P8 merge and after it, checked by running both.)
struct Golden { unsigned geo, st; int tris, draws; };
const Golden GOLD[S_N][2] = {
    // { geometry path }, { masked path } -- taken 2026-11-24 from draw.cpp before the merge (AIO_SHAPES_PRINT=1 prints them)
    { { 0xB90F3444u, 0x238F2FC5u, 17388, 2642 }, { 0x6F9A43B0u, 0x078386A5u, 14652, 2402 } },   // rrect
    { { 0xDEECF858u, 0x238F2FC5u, 30844, 4698 }, { 0xC2055EDEu, 0xD5EC8BF8u, 27436, 4402 } },   // rrect_bordered
    { { 0x431EB193u, 0x238F2FC5u, 9772, 1818 }, { 0x93F3B325u, 0xF9BB0A65u, 5908, 1438 } },   // rrect_topcaps
    { { 0x38B59429u, 0x238F2FC5u, 23932, 4138 }, { 0xA4CAA4E6u, 0x319C6C65u, 18896, 3564 } },   // rrect_tb
    { { 0xDEDB06B0u, 0x238F2FC5u, 7472, 1284 }, { 0xDEDB06B0u, 0x238F2FC5u, 7472, 1284 } },   // rrect_left
    { { 0xA9E8B131u, 0x238F2FC5u, 12000, 1800 }, { 0xA9E8B131u, 0x238F2FC5u, 12000, 1800 } },   // rrect_glow
    { { 0xE7636737u, 0x238F2FC5u, 28800, 2640 }, { 0xE7636737u, 0x238F2FC5u, 28800, 2640 } },   // rrect_stroke
};

// ...and the draws AFTER the merge (2026-11-24) : one list per shape on the geometry path, one per rect batch and
// one per corner batch on the masked one. A later change that splits a shape again shows here.
const int MERGED_DRAWS[S_N][2] = { { 240, 258 }, { 448, 471 }, { 240, 354 }, { 480, 548 }, { 240, 240 }, { 240, 240 }, { 240, 240 } };

} // namespace

static int state_calls() {
    int n = 0; const fakedev::Log& L = fakedev::log();
    for (int i = 0; i < L.nCall; ++i) if (L.call[i].kind != fakedev::Call::DrawUP) ++n;
    return n;
}

void test_shapes() {
    const bool print = getenv("AIO_SHAPES_PRINT") != 0;
    SECTION("state shadow (P4) : a state the device already holds is not sent again ; forgetting re-sends it");
    {
        const u32 dev = fakedev::device();
        fakedev::reset();
        dColorQuadState(dev);
        const int first = state_calls();
        CHECK(first >= 18);                          // the whole 2D block, the first time
        const u32 zen = fakedev::state(D3DRS_SRCBLEND);
        dColorQuadState(dev);
        CHECK_EQ(state_calls(), first);              // nothing new : every one of them is already in force
        CHECK_EQ(fakedev::state(D3DRS_SRCBLEND), zen);
        dSetRS(dev, D3DRS_SRCBLEND, D3DBLEND_ONE);   // a real change goes through...
        CHECK_EQ(state_calls(), first + 1);
        CHECK_EQ(fakedev::state(D3DRS_SRCBLEND), (u32)D3DBLEND_ONE);
        dColorQuadState(dev);                        // ...and so does putting it back, alone
        CHECK_EQ(state_calls(), first + 2);
        CHECK_EQ(fakedev::state(D3DRS_SRCBLEND), (u32)D3DBLEND_SRCALPHA);
        dCreateSB(dev, D3DSBT_ALL);                  // the frame boundary : the game drew in between
        dColorQuadState(dev);
        CHECK_EQ(state_calls(), 2 * first + 2);      // everything again
        dApplySB(dev, 0xB10Cu);                      // the game's state restored : forgotten too
        dColorQuadState(dev);
        CHECK_EQ(state_calls(), 3 * first + 2);
        dStateFilter(false);                         // //aio perf filter off : every call sent, as before P4
        dColorQuadState(dev); dColorQuadState(dev);
        CHECK_EQ(state_calls(), 5 * first + 2);
        dStateFilter(true);
        CHECK(dStateFiltering());
        // another device inherits nothing from the first
        static struct { void* vt; } other = { *(void**)(uintptr_t)dev };
        const u32 dev2 = (u32)(uintptr_t)&other;
        const int before2 = state_calls();
        dColorQuadState(dev); dColorQuadState(dev2);
        CHECK_EQ(state_calls(), before2 + 2 * first);   // dev re-sent (forgotten by the filter switch), dev2 sent in full
    }

    SECTION("shapes : every rounded shape draws the triangles it drew before the merge, with fewer draws (P8)");
    for (int s = 0; s < S_N; ++s) for (int m = 0; m < 2; ++m) {
        const Acc A = run(s, m == 1);
        if (print) printf("    { 0x%08Xu, 0x%08Xu, %d, %d },   // %s %s\n", A.geo, A.st, A.tris, A.draws, NAME[s], m ? "masked" : "geometry");
        const Golden& G = GOLD[s][m];
        CHECK_EQ(A.geo, G.geo);
        CHECK_EQ(A.st, G.st);
        CHECK_EQ(A.tris, G.tris);
        CHECK(A.draws < G.draws);                    // fewer draws than before the merge...
        CHECK(A.draws <= MERGED_DRAWS[s][m]);        // ...and no more than the merge made of it
    }
}
