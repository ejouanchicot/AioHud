// map_dat.h -- extract a zone's map image from the FFXI ROM DATs (reversed from FFXIDB, see
// docs/game-data/map-system.md section 4). Resolves a map file-id via the VTABLE/FTABLE volume scheme
// off the FFXI install (registry PlayOnline InstallFolder), reads the DAT, and decodes the 8-bit-palette
// graphic chunk to an A8R8G8B8 buffer. The stored image is horizontally MIRRORED -> flip U at draw.
#pragma once
#include "windower.h"   // u32 (windower::u32) -- model must not depend on gfx/
using windower::u32;

namespace aio {

// Load the map image for `fileId` (MapRecord::fileId). On success allocates a W*H A8R8G8B8 buffer
// (0xAARRGGBB, alpha already scaled 0..0x80 -> 0..255) and returns it via outPixels (free with
// free_map_image), with dims in outW/outH. Returns false if the install/tables/DAT/chunk aren't found.
// Where a failed load actually stopped. A black minimap (record valid, texture never loaded) used to give
// no clue which of these it was -- resolve / read / chunk-walk / format are four different bugs.
enum MapLoadStep { MLS_OK = 0, MLS_NO_ROOT, MLS_NO_PATH, MLS_NO_FILE, MLS_NO_CHUNK, MLS_BAD_FMT };
struct MapLoadDiag {
    int         step;        // MapLoadStep
    char        path[260];   // resolved DAT path ("" if unresolved)
    bool        overlay;     // path came from an XIPivot overlay rather than the vanilla ROM
    unsigned    fileSize;    // bytes read (0 if unread)
    unsigned    chunkTypes;  // bitmask of chunk types seen (bit n = type n & 31) -> was it even a map DAT?
    int         W, H;        // dims from the graphic sub-header (0 if never reached)
    unsigned    fmtFlags;    // the graphic sub-header's flags byte (bit 4 = the one we require)
};

bool load_zone_map(unsigned fileId, u32*& outPixels, int& outW, int& outH, MapLoadDiag* diag = 0);
void free_map_image(u32* pixels);

} // namespace aio
