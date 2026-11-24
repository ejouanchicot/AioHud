// xy_write.h -- a window record's X and Y are written as ONE change, or not at all (audit R8).
//
// THE BUG. native_ui's apply() moved a client window with `wr16(X) && wr16(Y)`. When X was written and Y was not, the
// record held the NEW X while the tracked offset (ax, ay) still said "not moved". The next frame read X, saw it differ
// from origin + offset, took that for "the CLIENT rewrote it (relog, DAT reload)" and RE-LEARNED THE MOVED X AS THE
// ORIGIN. From then on every offset was applied on top of a displaced origin, and save_undo wrote that origin into the
// crash-recovery file : a window shifted for good, across reloads, with nothing in the log.
//
// THE RULE. Write X, then Y. If Y fails, put X back to what it held. If even that fails, the tracking follows what the
// record really holds (X moved, Y not) -- so the next frame sees no difference to explain, and never re-learns. The
// caller logs every outcome that is not XY_OK.
//
// Pure (the writer is a parameter) : tests/t_undo.cpp drives it against a fake record whose writes can fail.
#pragma once

namespace aio {

enum XyWrite {
    XY_OK = 0,              // both written
    XY_X_FAILED,            // nothing written
    XY_Y_FAILED_X_RESTORED, // Y refused ; X put back : the record is as it was
    XY_Y_FAILED_X_STUCK,    // Y refused and X could not be put back : the record holds the new X, the old Y
};

// wr(address, value) -> bool. oldX = what X held before (the value read this frame).
template <class Wr>
inline int write_xy(Wr wr, unsigned addrX, unsigned addrY, short x, short y, short oldX) {
    if (!wr(addrX, x)) return XY_X_FAILED;
    if (wr(addrY, y)) return XY_OK;
    return wr(addrX, oldX) ? XY_Y_FAILED_X_RESTORED : XY_Y_FAILED_X_STUCK;
}

// The tracked offsets after a write_xy : they follow what the record HOLDS, whatever happened.
inline void xy_track(int rc, short dx, short dy, short& ax, short& ay) {
    if (rc == XY_OK)               { ax = dx; ay = dy; }
    else if (rc == XY_Y_FAILED_X_STUCK) { ax = dx; }      // X did move ; Y did not
    // XY_X_FAILED / XY_Y_FAILED_X_RESTORED : the record is unchanged, so are the offsets
}

// Did the CLIENT rewrite this record (relog, DAT reload) ? -- what apply() re-learns the origin on.
inline bool xy_client_rewrote(short cx, short cy, short ox, short oy, short ax, short ay) {
    return cx != ox + ax || cy != oy + ay;
}

inline const char* xy_write_name(int rc) {
    switch (rc) {
        case XY_OK: return "ok";
        case XY_X_FAILED: return "X refused (nothing written)";
        case XY_Y_FAILED_X_RESTORED: return "Y refused, X put back";
        default: return "Y refused and X could NOT be put back (tracked as moved)";
    }
}

} // namespace aio
