// clip_rect.h -- the pure geometry of a NESTED clip region, extracted from the clip itself.
//
// A clip is two things: a rectangle decision and a device call. The rectangle is where the mistakes live -- a
// child that escapes its parent, a rounding that eats the first row, an empty intersection that turns into "no
// clip at all" precisely where the strictest one was asked for -- and none of it needs a device to decide. So
// it is here, pure, and tests/t_clip.cpp asks it directly.
//
// Integers on purpose: the destination is a rasterizer scissor, which is measured in whole pixels. Rounding
// OUT (floor the top-left, ceil the bottom-right) is the only choice that cannot cut a pixel the caller asked
// for -- rounding to nearest drops the first row of a band whose top lands on x.5, which is how a one-pixel
// clip is normally lost.
#pragma once

namespace aio {

// A half-open box in screen pixels : [l, r) x [t, b).
struct ClipBox { long l, t, r, b; };

inline ClipBox clip_box_of(float x, float y, float w, float h) {
    ClipBox c;
    c.l = (long)(x        >= 0.0f ? x        : 0.0f);                        // floor, coords are never negative here
    c.t = (long)(y        >= 0.0f ? y        : 0.0f);
    c.r = (long)(x + w + 0.999f >= 0.0f ? x + w + 0.999f : 0.0f);            // ceil
    c.b = (long)(y + h + 0.999f >= 0.0f ? y + h + 0.999f : 0.0f);
    return c;
}

// The child can only ever SHRINK the parent -- that is the whole invariant of a nested clip, and the reason a
// child rect is never trusted as given.
//
// An EMPTY intersection collapses to ONE pixel inside the parent rather than to nothing, because "nothing" is
// not expressible: a rasterizer scissor of zero width is rejected, and a rejected call leaves the PARENT's
// region in force -- i.e. no clipping where the tightest clipping was requested, which is the exact opposite of
// the ask. One pixel in the parent's corner is the honest answer: nothing meaningful can land in it.
inline ClipBox clip_intersect(const ClipBox& parent, const ClipBox& child) {
    ClipBox c = child;
    if (c.l < parent.l) c.l = parent.l;
    if (c.t < parent.t) c.t = parent.t;
    if (c.r > parent.r) c.r = parent.r;
    if (c.b > parent.b) c.b = parent.b;
    if (c.r <= c.l) { c.l = parent.l; c.r = (parent.r > parent.l) ? parent.l + 1 : parent.r; }
    if (c.b <= c.t) { c.t = parent.t; c.b = (parent.b > parent.t) ? parent.t + 1 : parent.b; }
    return c;
}

} // namespace aio
