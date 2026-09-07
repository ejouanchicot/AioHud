// icon_dat.h -- the game's OWN status-icon sheet, decoded from ROM/119/57.DAT (file id 87) into the exact
// 1024x640 / 32px-cell grid ui/buff_atlas.h already draws from. Cell index == status id, 640 of them, which is
// why the atlas was built with 32 columns x 20 rows in the first place: that IS the DAT's own layout.
//
// WHY READ THE DAT AT ALL. The bundled assets/buff_atlas.raw is one frozen icon set. Resolving the DAT instead
// goes through the shared XIPivot-aware resolver (model/map_dat.h), so the HUD shows whatever status icons the
// player actually has installed -- a custom/HD pack overlay if they run one, the vanilla art if they don't --
// and the two can never drift apart. The bundled sheet stays as the fallback for an install we cannot read.
//
// FORMAT (reversed 2026-09-06, see docs/game-data/buffs-and-timers/status-icon-dat.md). 640 fixed 6144-byte records; inside
// each: a 16-char name ("sts_iconst00_32") at +0x285, a plain BITMAPINFOHEADER at +0x295 (40 / 32 / 32 / 1
// plane / 32 bpp), then 32*32 BGRA pixels at +0x2BD, BOTTOM-UP like any DIB. Not obfuscated (unlike the
// gear-icon DATs, which are rotl3-encoded and palettised).
#pragma once
#include "windower.h"   // u32 (windower::u32) -- model must not depend on gfx/
using windower::u32;

namespace aio {

static const int ICONDAT_STATUS_FILEID = 87;   // ROM/119/57.DAT on a current client -- resolved through VTABLE/FTABLE, never hardcoded as a path

// Where a failed decode actually stopped. "No icons" used to be indistinguishable from "no game install",
// "pack not readable" and "that DAT is not the icon sheet" -- three different bugs with three different fixes.
enum IconLoadStep { ILS_OK = 0, ILS_NO_PATH, ILS_NO_FILE, ILS_TOO_SMALL, ILS_BAD_RECORD };
struct IconLoadDiag {
    int      step;         // IconLoadStep
    char     path[260];    // resolved DAT path ("" if unresolved)
    bool     overlay;      // path came from an XIPivot overlay rather than the vanilla ROM
    unsigned fileSize;     // bytes read (0 if unread)
    int      badRecord;    // index of the first record that failed validation (-1 if none)
    int      records;      // records decoded (640 on success, 0 if it never got that far)
    int      halfAlpha;    // records whose alpha maxed at 0x80 -> rescaled (see the .cpp)
};

// Decode all 640 icons into `out` : a COLS*CELL x ROWS*CELL A8R8G8B8 grid (0xAARRGGBB, top-down), i.e. the
// caller passes the buff-atlas dimensions and gets a drop-in replacement for its .raw. Returns false and
// leaves `out` untouched if anything about the file is not the icon sheet -- a partial decode is NOT a success.
bool load_status_icons(u32* out, int atlasW, int atlasH, int cell, int cols, IconLoadDiag* diag = 0);

// Does this DAT still hold the game's ORIGINAL art? Square Enix's own sheet is uniformly 7-bit alpha (0..0x80,
// verified on an untouched copy: 640 records of 640), while anything drawn in an image editor uses the full
// range. So `halfAlpha == records` means nobody has replaced these icons -- which is what lets the HUD prefer
// its own crisp redraw over the client's soft 2002 art WITHOUT overriding a pack somebody installed straight
// over the game files (no XIPivot). See ui/buff_atlas.cpp for the order that comes out of it.
inline bool status_icons_untouched(const IconLoadDiag& d) { return d.records > 0 && d.halfAlpha == d.records; }

} // namespace aio
