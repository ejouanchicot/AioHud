// t_clip.cpp -- the rectangle half of a nested clip (gfx/clip_rect.h).
//
// WHY THIS EXISTS. The config page's clip was a stencil mask, and no depth-stencil surface is bound at
// HUD-draw time (measured, minimap.cpp:680) -- so it clipped NOTHING for as long as it existed, in every tab,
// and the bug that surfaced it was a user watching his module options scroll over the masthead. The mechanism
// is now a sub-viewport, which is a hard rasterizer scissor; what it needs from us is a RECTANGLE, and the
// rectangle is where clipping goes wrong: a child escaping its parent, a half-pixel eating the first row, an
// empty intersection turning into "no clip at all".
//
// That part needs no device, so it is asked here directly.
#include "check.h"
#include "gfx/clip_rect.h"

using namespace aio;

void test_clip() {
    SECTION("clip : a child can only ever shrink its parent");
    const ClipBox parent = { 100, 200, 400, 600 };
    {
        const ClipBox c = clip_intersect(parent, ClipBox{ 50, 150, 450, 650 });   // bigger on all four sides
        CHECK_EQ(c.l, 100); CHECK_EQ(c.t, 200); CHECK_EQ(c.r, 400); CHECK_EQ(c.b, 600);
    }
    {
        const ClipBox c = clip_intersect(parent, ClipBox{ 150, 250, 300, 500 });   // strictly inside : kept as is
        CHECK_EQ(c.l, 150); CHECK_EQ(c.t, 250); CHECK_EQ(c.r, 300); CHECK_EQ(c.b, 500);
    }
    {
        const ClipBox c = clip_intersect(parent, ClipBox{ 150, 150, 500, 500 });   // overlapping : each side clamped
        CHECK_EQ(c.l, 150); CHECK_EQ(c.t, 200); CHECK_EQ(c.r, 400); CHECK_EQ(c.b, 500);
    }

    SECTION("clip : an empty intersection is ONE pixel, never the parent");
    // A zero-width scissor is rejected by the device, and a rejected call leaves the PARENT's region in force
    // -- no clipping exactly where the tightest clipping was asked for. That is the failure this case locks:
    // a scrolled row whose band has left the viewport must not be drawn over the whole page.
    {
        const ClipBox c = clip_intersect(parent, ClipBox{ 10, 20, 40, 60 });      // wholly above and left
        CHECK(c.r > c.l && c.b > c.t);                                            // a usable rect ...
        CHECK_EQ(c.r - c.l, 1); CHECK_EQ(c.b - c.t, 1);                           // ... of exactly one pixel
        CHECK(c.l >= parent.l && c.r <= parent.r && c.t >= parent.t && c.b <= parent.b);
    }
    {
        const ClipBox c = clip_intersect(parent, ClipBox{ 150, 900, 300, 950 });   // wholly below : only Y collapses
        CHECK_EQ(c.l, 150); CHECK_EQ(c.r, 300);
        CHECK_EQ(c.b - c.t, 1);
    }
    {   // a zero-height child -- what a folding section asks for on the frame it opens
        const ClipBox c = clip_intersect(parent, ClipBox{ 150, 300, 300, 300 });
        CHECK_EQ(c.b - c.t, 1);
        CHECK(c.t >= parent.t);
    }

    SECTION("clip : the rect rounds OUT, so a half-pixel never eats a row");
    // Rounding to NEAREST is how a one-pixel clip is normally lost: a band whose top lands on y.5 loses its
    // first row, and a 46px row loses its bottom edge. Floor the top-left, ceil the bottom-right.
    {
        const ClipBox c = clip_box_of(10.5f, 20.5f, 100.0f, 46.0f);
        CHECK_EQ(c.l, 10); CHECK_EQ(c.t, 20);           // floored
        CHECK_EQ(c.r, 111); CHECK_EQ(c.b, 67);          // ceiled : 110.5 -> 111, 66.5 -> 67
        CHECK(c.r - c.l >= 100 && c.b - c.t >= 46);     // never SMALLER than what was asked for
    }
    {   // whole numbers must not gain a pixel on every nesting level
        const ClipBox c = clip_box_of(10.0f, 20.0f, 100.0f, 46.0f);
        CHECK_EQ(c.l, 10); CHECK_EQ(c.t, 20); CHECK_EQ(c.r, 110); CHECK_EQ(c.b, 66);
    }
    {   // A FRACTION BELOW A HALF is the only input that tells rounding-out from rounding-to-nearest, and the
        // first version of this section did not have one : both answers agreed on .5 and on .8, so a mutant
        // that rounded to nearest passed. 110.2 is where they part -- out says 111, nearest says 110, and
        // nearest is the one that cuts the last pixel row off every band.
        const ClipBox c = clip_box_of(10.2f, 20.2f, 100.0f, 46.0f);
        CHECK_EQ(c.r, 111); CHECK_EQ(c.b, 67);
    }
}
