// icon_dat.cpp -- see icon_dat.h. Pure file I/O (no game-memory reads) : resolve the status-icon DAT through
// the shared XIPivot-aware resolver, validate it record by record, and blit the 640 32x32 BGRA icons into the
// atlas grid. Confirmed against this install's vanilla ROM/119/57.DAT and against an "IconsHD" XIPivot overlay
// (626 of the 640 cells redrawn), both 3 932 160 bytes = 640 * 6144.
#include "model/icon_dat.h"
#include "model/map_dat.h"   // dat_resolve_path / dat_read_file / dat_free_file -- the ONE install+overlay resolver
#include <windows.h>
#include <string.h>

namespace aio {

// --- record geometry (all constants reversed from the file itself, see the header) ---
static const unsigned ICON_RECORDS = 640;      // == atlas cells : cell index IS the status id
static const unsigned ICON_STRIDE  = 6144;     // 0x1800 -- fixed, the file is exactly 640 * this
static const unsigned ICON_NAME    = 0x285;    // char[16] "sts_iconst00_32 "
static const unsigned ICON_BMPHDR  = 0x295;    // BITMAPINFOHEADER (biSize 40, W, H, planes, bpp, ...)
static const unsigned ICON_PIXELS  = 0x2BD;    // == ICON_BMPHDR + 40 : the pixel body
static const unsigned ICON_PALETTE = 0x400;   // 8-bpp records only : 256 BGRA entries, THEN the indices
static const int      ICON_SIDE    = 32;

// One record's header, validated. The name prefix is the real magic here : a DAT of the right SIZE with the
// wrong CONTENT (the client re-numbers files across patches) would otherwise decode to 640 cells of noise.
static bool record_ok(const unsigned char* r) {
    if (memcmp(r + ICON_NAME, "sts_icon", 8) != 0) return false;
    const unsigned char* h = r + ICON_BMPHDR;
    const unsigned biSize = *(const unsigned*)(h + 0);
    const int      W      = *(const int*)(h + 4), H = *(const int*)(h + 8);
    const unsigned short planes = *(const unsigned short*)(h + 12), bpp = *(const unsigned short*)(h + 14);
    // 8 bpp is NOT an edge case worth rejecting : the vanilla sheet ships exactly one palettised record
    // ("sts_iconrelim_32", status 506) among 639 32-bpp ones, and refusing it threw away the whole file --
    // every player WITHOUT an icon pack would have silently fallen back to the bundled sheet.
    return biSize == 40 && W == ICON_SIDE && H == ICON_SIDE && planes == 1 && (bpp == 32 || bpp == 8);
}

bool load_status_icons(u32* out, int atlasW, int atlasH, int cell, int cols, IconLoadDiag* diag)
{
    IconLoadDiag scratch; if (!diag) diag = &scratch;
    diag->step = ILS_NO_PATH; diag->path[0] = 0; diag->overlay = false;
    diag->fileSize = 0; diag->badRecord = -1; diag->records = 0; diag->halfAlpha = 0;
    if (!out || cell != ICON_SIDE || cols <= 0) return false;
    const int rows = (int)((ICON_RECORDS + (unsigned)cols - 1) / (unsigned)cols);
    if (atlasW < cols * cell || atlasH < rows * cell) return false;   // the grid must hold all 640

    char path[MAX_PATH];
    if (!dat_resolve_path(ICONDAT_STATUS_FILEID, path, MAX_PATH)) return false;
    lstrcpynA(diag->path, path, sizeof(diag->path));
    for (const char* c = path; *c; ++c)                               // case-insensitive "XIPivot" scan (no shlwapi dependency)
        if ((c[0]|32)=='x' && (c[1]|32)=='i' && (c[2]|32)=='p' && (c[3]|32)=='i' &&
            (c[4]|32)=='v' && (c[5]|32)=='o' && (c[6]|32)=='t') { diag->overlay = true; break; }

    diag->step = ILS_NO_FILE;
    unsigned n = 0; unsigned char* d = dat_read_file(path, n);
    if (!d) return false;
    diag->fileSize = n;

    diag->step = ILS_TOO_SMALL;
    if (n < ICON_RECORDS * ICON_STRIDE) { dat_free_file(d); return false; }

    // Validate EVERY record before writing a single pixel. A file that is the right size but only half the icon
    // sheet would otherwise leave the caller with a texture it believes is complete (rule 10 : a partial success
    // is not a success) -- and the bundled sheet, which IS complete, would never get its turn.
    diag->step = ILS_BAD_RECORD;
    for (unsigned k = 0; k < ICON_RECORDS; ++k)
        if (!record_ok(d + k * ICON_STRIDE)) { diag->badRecord = (int)k; dat_free_file(d); return false; }

    for (unsigned k = 0; k < ICON_RECORDS; ++k) {
        const unsigned char* rec  = d + k * ICON_STRIDE;
        const bool           pal8 = (*(const unsigned short*)(rec + ICON_BMPHDR + 14) == 8);
        const unsigned char* pal  = pal8 ? rec + ICON_PIXELS : 0;                              // 256 BGRA entries
        const unsigned char* px   = pal8 ? rec + ICON_PIXELS + ICON_PALETTE : rec + ICON_PIXELS;
        // TWO alpha conventions, and the file has to be asked RECORD BY RECORD. The client's own art is 7-bit
        // (0..0x80 = opaque) from end to end -- 640 records of 640 on an untouched copy -- while art drawn in an
        // image editor uses the full 0..255. A file where somebody has replaced SOME icons holds both at once
        // (measured: 622 of 640 rewritten, 18 originals left), so a per-FILE answer necessarily draws one group
        // or the other wrong: doubling everything hardens every antialiased edge of the new art, doubling
        // nothing draws the originals at half opacity, as ghosts. A record whose alpha never exceeds 0x80 is a
        // 7-bit record. For a palettised record the convention lives in the PALETTE, so that is what is scanned.
        int maxA = 0;
        if (pal8) { for (int i = 0; i < 256; ++i)                   { const int a = pal[i * 4 + 3]; if (a > maxA) maxA = a; } }
        else      { for (int i = 0; i < ICON_SIDE * ICON_SIDE; ++i) { const int a = px[i * 4 + 3];  if (a > maxA) maxA = a; } }
        const bool sevenBit = (maxA <= 0x80);
        ++diag->records;
        if (sevenBit) ++diag->halfAlpha;

        const int cx = (int)(k % (unsigned)cols) * cell, cy = (int)(k / (unsigned)cols) * cell;
        for (int y = 0; y < ICON_SIDE; ++y) {
            const int sy = ICON_SIDE - 1 - y;                                      // DIB rows are BOTTOM-UP
            u32* dst = out + (unsigned)(cy + y) * (unsigned)atlasW + (unsigned)cx;
            for (int x = 0; x < ICON_SIDE; ++x) {
                const unsigned char* c = pal8 ? pal + (unsigned)px[sy * ICON_SIDE + x] * 4
                                              : px + (sy * ICON_SIDE + x) * 4;     // B, G, R, A
                int a = c[3];
                if (sevenBit) { a += a; if (a > 255) a = 255; }
                dst[x] = ((u32)a << 24) | ((u32)c[2] << 16) | ((u32)c[1] << 8) | (u32)c[0];
            }
        }
    }
    dat_free_file(d);
    diag->step = ILS_OK;
    return true;
}

} // namespace aio
