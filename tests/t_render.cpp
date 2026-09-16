// t_render.cpp -- the first automated look at what this plugin actually DRAWS.
//
// WHY THIS EXISTS. src/ui is the largest layer in the project and nothing automated had ever looked at it:
// the offline suite covers the model, the in-game harness compares STATE against Windower, and neither can
// see a quad. Three of the ten non-negotiable rules in CLAUDE.md are about drawing, and all three had failed
// in production at least once -- a blurred edge (rule 1), a pass left additive so the next draw glowed
// (rule 3), a texture left bound for the next widget (rule 8). Each of them is a property of the CALLS, not
// of the pixels, so a device that records calls can hold them.
//
// It also gives the seven remaining 400-line draw() functions the oracle they were missing: `trace()`
// serialises every call and every vertex, so a refactor that is supposed to change nothing can be PROVEN to
// change nothing -- the same contract the session replays give the model.
#include "check.h"
#include "fake_device.h"
#include "gfx/draw.h"
#include "gfx/d3d.h"
#include <math.h>

using namespace aio;
using fakedev::device;
using fakedev::reset;

// Is `v` on the half-pixel grid the D3D8 rule demands (rule 1)? Colour quads shift by exactly -0.5, so a
// coordinate that started whole lands on N + 0.5. Anything else means the caller skipped snap().
static bool on_half_pixel(float v) {
    const float f = v - floorf(v);
    return fabsf(f - 0.5f) < 0.0005f;
}

void test_render() {
    const u32 dev = device();

    SECTION("render : a snapped straight-edged quad lands exactly on the half-pixel grid (rule 1)");
    {
        // STRAIGHT edges only. The first version of this check asked it of rrect() and failed on 76 vertices
        // -- correctly: a rounded corner is a fan of arc points, and an anti-aliased arc CANNOT sit on the
        // grid, that is what makes it smooth. The rule is about edges that are meant to be crisp, so it is
        // asked of the primitive that has nothing else: a plain gradient quad, whose four corners are the
        // rectangle itself.
        reset();
        dColorQuadState(dev);
        grad_quad(dev, snap(100.0f), snap(50.0f), snap(220.0f), snap(64.0f),
                  0xFF203040, 0xFF304050, 0xFF101820, 0xFF202830);
        CHECK(fakedev::draws() > 0);
        CHECK_EQ(fakedev::verts(), 4);
        int off = 0;
        for (int i = 0; i < fakedev::verts(); ++i) {
            const fakedev::Vertex& v = fakedev::vertex(i);
            if (!on_half_pixel(v.x) || !on_half_pixel(v.y)) ++off;
        }
        CHECK_EQ(off, 0);
    }

    SECTION("render : an UNSNAPPED quad is off the grid -- which is what rule 1 forbids");
    {
        // The other half of the proof: the check above only means something if a caller that skips snap()
        // is visibly different. This is the blurred edge, reproduced on purpose.
        reset();
        dColorQuadState(dev);
        grad_quad(dev, 100.3f, 50.7f, 220.0f, 64.0f, 0xFF203040, 0xFF304050, 0xFF101820, 0xFF202830);
        int off = 0;
        for (int i = 0; i < fakedev::verts(); ++i) {
            const fakedev::Vertex& v = fakedev::vertex(i);
            if (!on_half_pixel(v.x) || !on_half_pixel(v.y)) ++off;
        }
        CHECK(off > 0);
    }

    SECTION("render : snap() rounds away from zero on BOTH sides");
    {
        // The bug this locks: (int)(v + 0.5f) truncates toward zero, so snap(-3.4) used to give -2 -- a whole
        // pixel, on the only axis where rule 1 is not decorative.
        CHECK_EQ((int)snap(3.4f), 3);
        CHECK_EQ((int)snap(3.6f), 4);
        CHECK_EQ((int)snap(-3.4f), -3);
        CHECK_EQ((int)snap(-3.6f), -4);
        CHECK_EQ((int)snap(0.0f), 0);
    }

    SECTION("render : an additive pass is put back before the next draw (rule 3)");
    {
        reset();
        dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_ONE);                       // a glow pass, as window.cpp does it
        CHECK_EQ(fakedev::state(D3DRS_DESTBLEND), (u32)D3DBLEND_ONE);
        dColorQuadState(dev);                                             // the shared 2D state, d3d.h
        CHECK_EQ(fakedev::state(D3DRS_DESTBLEND), (u32)D3DBLEND_INVSRCALPHA);
        CHECK_EQ(fakedev::state(D3DRS_SRCBLEND), (u32)D3DBLEND_SRCALPHA);
        CHECK_EQ(fakedev::state(D3DRS_ALPHABLENDENABLE), 1u);
    }

    SECTION("render : the colour state leaves no texture bound for the next widget (rule 8)");
    {
        reset();
        dTexQuadState(dev, 0xDEADBEEF);                                   // a widget binds its atlas
        CHECK_EQ(fakedev::bound_texture(), 0xDEADBEEFu);
        dColorQuadState(dev);                                                          // ... and hands the device on
        CHECK_EQ(fakedev::bound_texture(), 0u);
        CHECK_EQ(fakedev::state_written(D3DRS_ZENABLE), true);            // depth off : 2D over a 3D scene
        CHECK_EQ(fakedev::state(D3DRS_ZENABLE), 0u);
        CHECK_EQ(fakedev::state(D3DRS_CULLMODE), (u32)D3DCULL_NONE);      // quads must not be back-face culled
    }

    SECTION("render : a feathered rect feathers every side, never some of them (rule 2)");
    {
        // A partially feathered shape shows a 1px dark seam. The geometric signature of "this side is
        // feathered" is a vertex pair that straddles the edge; the symptom of a missed side is a bounding box
        // that is short on exactly one side. Check the extents instead of the seam: with feather 1.2 the
        // geometry reaches 1.2 px beyond the rect on all four sides, or the feather is missing there.
        reset();
        dColorQuadState(dev);
        const float X = 200.5f, Y = 120.5f, W = 160.0f, H = 48.0f, F = 1.2f;
        rrect(dev, snap(X), snap(Y), W, H, 10.0f, 0xFF404040, 0xFF202020, F);
        float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
        for (int i = 0; i < fakedev::verts(); ++i) {
            const fakedev::Vertex& v = fakedev::vertex(i);
            if (v.x < minX) minX = v.x;  if (v.x > maxX) maxX = v.x;
            if (v.y < minY) minY = v.y;  if (v.y > maxY) maxY = v.y;
        }
        const float x0 = snap(X) - 0.5f, y0 = snap(Y) - 0.5f;
        CHECK(minX <= x0 + 0.01f);                 // left edge reached (feather may extend it outward)
        CHECK(maxX >= x0 + W - 0.01f);             // right edge reached
        CHECK(minY <= y0 + 0.01f);                 // top
        CHECK(maxY >= y0 + H - 0.01f);             // bottom
        const float slackL = x0 - minX, slackR = maxX - (x0 + W);
        const float slackT = y0 - minY, slackB = maxY - (y0 + H);
        CHECK(fabsf(slackL - slackR) < 0.01f);     // the same amount on opposite sides, or one was skipped
        CHECK(fabsf(slackT - slackB) < 0.01f);
    }

    SECTION("render : the same call twice produces the same trace (the refactor oracle)");
    {
        // This is what lets a draw() be split with proof: serialise the calls, change the code, compare.
        char a[8192], b[8192];
        reset();
        dColorQuadState(dev);
        rrect(dev, snap(40.0f), snap(40.0f), snap(120.0f), snap(30.0f), snap(6.0f), 0xFF884422, 0xFF221108, 1.2f);
        grad_quad(dev, snap(40.0f), snap(40.0f), snap(120.0f), snap(30.0f), 0xFF00FF88, 0xFF0088FF, 0xFF884400, 0xFF000000);
        fakedev::trace(a, sizeof(a));
        reset();
        dColorQuadState(dev);
        rrect(dev, snap(40.0f), snap(40.0f), snap(120.0f), snap(30.0f), snap(6.0f), 0xFF884422, 0xFF221108, 1.2f);
        grad_quad(dev, snap(40.0f), snap(40.0f), snap(120.0f), snap(30.0f), 0xFF00FF88, 0xFF0088FF, 0xFF884400, 0xFF000000);
        fakedev::trace(b, sizeof(b));
        CHECK_STR(a, b);
        CHECK(a[0] != 0);                          // and it is not trivially empty
    }

    SECTION("render : a draw with no device is a no-op, not a crash");
    {
        // vfn() refuses an address outside [0x10000, 0x80000000) and every helper checks the pointer it got,
        // so the whole draw path has to survive a dead device -- which is exactly what a lost device is.
        reset();
        dColorQuadState(0);
        rrect(0, 10.0f, 10.0f, 50.0f, 20.0f, 4.0f, 0xFFFFFFFF, 0xFF000000, 1.2f);
        CHECK_EQ(fakedev::draws(), 0);
    }
}
