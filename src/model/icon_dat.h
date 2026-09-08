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

// The same decode from an EXPLICIT file, bypassing the resolver -- what the config's pack chooser needs, since
// picking a pack means naming a file the resolver would not have returned (an overlay the player has disabled,
// or the vanilla ROM under one they have enabled). `path` is reported back in the diag exactly as passed.
bool load_status_icons_at(const char* path, u32* out, int atlasW, int atlasH, int cell, int cols,
                          IconLoadDiag* diag = 0);

// ---- WHICH sheet to draw : the packs this install can actually offer (config > Interface > Status icons) ----
// Enumerated rather than typed in : an entry exists only because its file was found on disk this scan, so the
// list can never offer a pack that is gone, and a pack added while the game runs shows up on the next open.
enum IconPackKind {
    IPK_AUTO = 0,   // the built-in precedence (custom sheet > a pack the player installed > bundled > vanilla)
    IPK_BUNDLED,    // plugins\AioHud\assets\buff_atlas.raw -- what ships with the plugin
    IPK_CUSTOM,     // plugins\AioHud\icons\status_atlas.raw -- a sheet the player built with aioicons.ps1
    IPK_GAME,       // the client's own ROM file, overlays ignored
    IPK_OVERLAY,    // one XIPivot pack, named by its folder (IconsHD, VisionMaster, ...) -- enabled or not
    IPK_PACK        // a folder the player dropped in icons\\packs\\ -- see ICON_PACKS_DIR below
};

// THE PACKS FOLDER : plugins\\AioHud\\icons\\packs\\<name>\\, one subfolder per pack, and the FOLDER NAME is what
// the config row shows -- so the player names their own packs instead of living with whatever a download was
// called. Inside, the first of these that exists wins:
//     <name>\\57.DAT                the status-icon DAT on its own
//     <name>\\ROM\\119\\57.DAT      the same with the pack's own tree left intact (unzip it as it came)
//     <name>\\status_atlas.raw      an already-built sheet (what AioHudIcons / aioicons.ps1 produce)
// It lives under icons\\, never under assets\\, for the reason buff_custom_path() gives: deploy.bat and the
// updater rewrite assets\\ wholesale, and a pack a routine update silently deleted would be worse than no
// pack at all. Nothing in the release ever writes here.
struct IconPack {
    char name[48];    // what the config row shows, and what the config FILE stores (see below)
    char path[260];   // the file to read ("" for IPK_AUTO)
    int  kind;        // IconPackKind
    // Other sources that produce THE SAME PIXELS, comma-separated ("" when this entry stands alone). One pack
    // installed twice -- as an XIPivot overlay AND as your own sheet, say -- is ONE look, and listing it twice
    // turns the row into a list of file locations rather than a list of icon sets. Measured rather than
    // guessed: the fingerprint is taken on what each source DECODES to, so a .raw and a .DAT carrying the same
    // art merge, which comparing files could never do. Kept so a config naming a merged source still resolves.
    char alias[96];
    unsigned long long fp;   // fingerprint of the produced atlas (0 = unreadable, and never merged)
};
static const int ICON_PACK_MAX = 24;

// How many sheets this install can offer, Auto first, scanning once and caching. `customPath` / `bundledPath`
// come from ui/buff_atlas.h -- passed IN rather than included, because model must not depend on ui (CLAUDE.md
// dependency rule). Always >= 1. Read the entries with icon_pack_at(): the config row that walks this list runs
// every frame, so it must not copy a 7 KB table (nor allocate) to draw one label.
int  icon_pack_count(const char* customPath, const char* bundledPath);
const IconPack* icon_pack_at(int i);   // 0 when i is out of range ; valid until the next icon_pack_forget()
// Drop that cache, so the next scan looks at the disk again. Called when the config page OPENS: a scan that
// found nothing (XIPivot mid-rewrite, a locked folder) must not be the answer for the rest of the session.
void icon_pack_forget();

// The pack SELECTED in the config, resolved against a fresh scan, or 0 when the name matches nothing on this
// install any more (a pack the player deleted) -- in which case the caller falls back to Auto, which always
// draws something. Stored by NAME, never by index: an index would silently point at a different pack the day
// XIPivot gains or loses a folder.
const IconPack* icon_pack_find(const char* name, const char* customPath, const char* bundledPath);

// Does this DAT still hold the game's ORIGINAL art? Square Enix's own sheet is uniformly 7-bit alpha (0..0x80,
// verified on an untouched copy: 640 records of 640), while anything drawn in an image editor uses the full
// range. So `halfAlpha == records` means nobody has replaced these icons -- which is what lets the HUD prefer
// its own crisp redraw over the client's soft 2002 art WITHOUT overriding a pack somebody installed straight
// over the game files (no XIPivot). See ui/buff_atlas.cpp for the order that comes out of it.
inline bool status_icons_untouched(const IconLoadDiag& d) { return d.records > 0 && d.halfAlpha == d.records; }

} // namespace aio
