// icon_dat.cpp -- see icon_dat.h. Pure file I/O (no game-memory reads) : resolve the status-icon DAT through
// the shared XIPivot-aware resolver, validate it record by record, and blit the 640 32x32 BGRA icons into the
// atlas grid. Confirmed against this install's vanilla ROM/119/57.DAT and against an "IconsHD" XIPivot overlay
// (626 of the 640 cells redrawn), both 3 932 160 bytes = 640 * 6144.
#include "model/icon_dat.h"
#include "model/map_dat.h"   // dat_resolve_path / dat_read_file / dat_free_file -- the ONE install+overlay resolver
#include <windows.h>
#include <string.h>
#include <stdio.h>   // _snprintf (the pack scan builds paths)
#include "windower_debug.h"   // the scan says what it found -- instrument the success path too

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

// The decode itself, from a path already chosen. Split out of load_status_icons so the pack chooser can name a
// file the resolver would never return -- same validation, same diagnostics, one implementation.
bool load_status_icons_at(const char* path, u32* out, int atlasW, int atlasH, int cell, int cols, IconLoadDiag* diag)
{
    IconLoadDiag scratch; if (!diag) diag = &scratch;
    diag->step = ILS_NO_PATH; diag->path[0] = 0; diag->overlay = false;
    diag->fileSize = 0; diag->badRecord = -1; diag->records = 0; diag->halfAlpha = 0;
    if (!out || cell != ICON_SIDE || cols <= 0 || !path || !path[0]) return false;
    const int rows = (int)((ICON_RECORDS + (unsigned)cols - 1) / (unsigned)cols);
    if (atlasW < cols * cell || atlasH < rows * cell) return false;   // the grid must hold all 640

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

bool load_status_icons(u32* out, int atlasW, int atlasH, int cell, int cols, IconLoadDiag* diag)
{
    char path[MAX_PATH];
    if (!dat_resolve_path(ICONDAT_STATUS_FILEID, path, MAX_PATH)) {
        if (diag) { diag->step = ILS_NO_PATH; diag->path[0] = 0; diag->overlay = false;
                    diag->fileSize = 0; diag->badRecord = -1; diag->records = 0; diag->halfAlpha = 0; }
        return false;
    }
    return load_status_icons_at(path, out, atlasW, atlasH, cell, cols, diag);
}

// ---- the pack list ------------------------------------------------------------------------------------------
// An entry is added only after its file has been STATTED, so the chooser cannot offer a sheet that is not there.
// The XIPivot scan walks the DATs folder rather than settings.xml on purpose: a pack the player has installed
// but not enabled is still a pack they may want the HUD to use, and picking one here is not the same act as
// turning it on for the whole client.
static IconPack g_packs[ICON_PACK_MAX];
static int      g_packN = 0;   // 0 = not scanned yet ; a scan always yields >= 1 (Auto), so this is unambiguous

// The atlas every source is measured against. Not a UI constant: this IS the DAT's own layout (640 records of
// 32 px, 32 to a row), which is why the buff atlas was built to match it and not the other way round.
static const int FP_COLS = 32;
static const int FP_W    = FP_COLS * ICON_SIDE;                                        // 1024
static const int FP_H    = (int)((ICON_RECORDS + FP_COLS - 1) / FP_COLS) * ICON_SIDE;  // 640

static bool file_here(const char* p) {
    const DWORD a = GetFileAttributesA(p);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
static bool path_is_raw(const char* p) {
    const int n = lstrlenA(p);
    return n > 4 && p[n-4] == '.' && (p[n-3]|32) == 'r' && (p[n-2]|32) == 'a' && (p[n-1]|32) == 'w';
}
// How much a source's name says about the ICONS, rather than about where the file happens to sit. A folder
// the player named beats the pack we ship, which beats an XIPivot folder named by whoever packaged it, which
// beats "My sheet" -- a label that identifies nothing at all, since the icon tool writes it whatever you feed
// it. "Game" ranks last: it names an installation, not an icon set.
static int name_rank(int kind) {
    switch (kind) {
        case IPK_PACK:    return 4;
        case IPK_BUNDLED: return 3;
        case IPK_OVERLAY: return 2;
        case IPK_CUSTOM:  return 1;
        default:          return 0;
    }
}
static void pack_add(int kind, const char* name, const char* path) {
    if (g_packN >= ICON_PACK_MAX) return;
    IconPack& e = g_packs[g_packN];
    e.kind = kind;
    lstrcpynA(e.name, name, sizeof(e.name));
    lstrcpynA(e.path, path ? path : "", sizeof(e.path));
    e.alias[0] = 0; e.fp = 0;
    ++g_packN;
}

// FNV-1a over the atlas a source PRODUCES. Comparing produced pixels is the entire point: the same pack
// installed once as a DAT and once as a .raw is two unrelated files and one identical look, and only a decode
// can say so. Paid once per config open (the scan is cached), never per frame.
static unsigned long long fnv1a(const unsigned char* p, unsigned n) {
    unsigned long long h = 1469598103934665603ULL;
    for (unsigned i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ULL; }
    return h;
}
static unsigned long long source_fp(const IconPack& e, u32* scratch) {
    if (!e.path[0]) return 0;
    const unsigned want = (unsigned)FP_W * (unsigned)FP_H * 4;
    if (path_is_raw(e.path)) {           // an already-built sheet : the FILE is the atlas, byte for byte
        unsigned n = 0; unsigned char* d = dat_read_file(e.path, n);
        if (!d) return 0;
        const unsigned long long h = (n == want) ? fnv1a(d, n) : 0;
        dat_free_file(d);
        return h;
    }
    if (!scratch || !load_status_icons_at(e.path, scratch, FP_W, FP_H, ICON_SIDE, FP_COLS)) return 0;
    return fnv1a((const unsigned char*)scratch, want);
}

// One pack folder -> the file to read, or false. Three shapes, most explicit first (see icon_dat.h).
static bool pack_folder_sheet(const char* dir, char* out, unsigned cap) {
    static const char* const REL[] = { "57.DAT", "ROM\\119\\57.DAT", "status_atlas.raw" };
    for (int i = 0; i < 3; ++i) {
        _snprintf(out, cap, "%s\\%s", dir, REL[i]); out[cap - 1] = 0;
        if (file_here(out)) return true;
    }
    out[0] = 0;
    return false;
}

void icon_pack_forget() { g_packN = 0; }

int icon_pack_count(const char* customPath, const char* bundledPath) {
    if (g_packN) return g_packN;

    pack_add(IPK_AUTO, "Auto", "");

    // ORDER IS NAMING. Duplicates merge into the entry seen FIRST, so the most deliberate source has to come
    // first: a folder someone named beats "my sheet", which beats a pack enabled for the whole client, which
    // beats the bundle, which beats the client's own art.
    const char* wr = dat_windower_root();
    char base[MAX_PATH];
    if (customPath && customPath[0]) {
        // packs\ sits beside the custom sheet, so it is derived from it rather than rebuilt from the root --
        // one definition of where the plugin's data lives (model/paths.cpp), not two.
        lstrcpynA(base, customPath, MAX_PATH);
        char* slash = 0;
        for (char* c = base; *c; ++c) if (*c == '\\') slash = c;
        if (slash) {
            *slash = 0;
            char pat[MAX_PATH];
            _snprintf(pat, MAX_PATH, "%s\\packs\\*", base); pat[MAX_PATH - 1] = 0;
            WIN32_FIND_DATAA fd;
            HANDLE h = FindFirstFileA(pat, &fd);
            if (h != INVALID_HANDLE_VALUE) {
                do {
                    if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == '.') continue;
                    char dir[MAX_PATH], sheet[MAX_PATH];
                    _snprintf(dir, MAX_PATH, "%s\\packs\\%s", base, fd.cFileName); dir[MAX_PATH - 1] = 0;
                    if (pack_folder_sheet(dir, sheet, MAX_PATH)) pack_add(IPK_PACK, fd.cFileName, sheet);
                } while (FindNextFileA(h, &fd) && g_packN < ICON_PACK_MAX);
                FindClose(h);
            }
        }
    }
    if (customPath  && file_here(customPath))  pack_add(IPK_CUSTOM,  "My sheet", customPath);

    // Every XIPivot folder that actually carries the status sheet, in folder order. Walking the DATs folder
    // rather than settings.xml is deliberate: a pack installed but not enabled is still one the player may want
    // the HUD to use, and picking it here is not the same act as turning it on for the whole client.
    if (wr) {
        char pat[MAX_PATH];
        _snprintf(pat, MAX_PATH, "%s\\addons\\XIPivot\\data\\DATs\\*", wr); pat[MAX_PATH - 1] = 0;
        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA(pat, &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == '.') continue;
                char p[MAX_PATH];
                _snprintf(p, MAX_PATH, "%s\\addons\\XIPivot\\data\\DATs\\%s\\ROM\\119\\57.DAT", wr, fd.cFileName);
                p[MAX_PATH - 1] = 0;
                if (file_here(p)) pack_add(IPK_OVERLAY, fd.cFileName, p);
            } while (FindNextFileA(h, &fd) && g_packN < ICON_PACK_MAX);
            FindClose(h);
        }
    }
    if (bundledPath && file_here(bundledPath)) pack_add(IPK_BUNDLED, "AioPack", bundledPath);   // the name the sheet carries on EVERY install
    // The client's own art, LAST : the least likely pick, and resolving it needs the volume tables loaded.
    char rom[MAX_PATH];
    if (dat_resolve_vanilla(ICONDAT_STATUS_FILEID, rom, MAX_PATH) && file_here(rom))
        pack_add(IPK_GAME, "Game", rom);

    // ---- MERGE WHAT LOOKS THE SAME. One scratch atlas for every decode, freed here : the scan is not on the
    // per-frame path, but it must not leave 2.5 MB behind either.
    u32* scratch = (u32*)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)FP_W * FP_H * 4);
    int merged = 0;
    for (int i = 1; i < g_packN; ++i) g_packs[i].fp = source_fp(g_packs[i], scratch);
    if (scratch) HeapFree(GetProcessHeap(), 0, scratch);
    for (int i = 1; i < g_packN; ++i) {
        if (!g_packs[i].fp) continue;                       // unreadable : never merged, and still offered --
        for (int j = i + 1; j < g_packN; ) {                // a source we cannot fingerprint may still load
            if (g_packs[j].fp != g_packs[i].fp) { ++j; continue; }
            // WHICH NAME SURVIVES. The entry keeps its slot (display order is scan order) but takes the name
            // of whichever merged source IDENTIFIES the icons best -- see name_rank. Without this, the pack we
            // ship would read as "My sheet" on the machine of anyone who had also built it with the icon tool,
            // and as "AioPack" on everyone else's: the same icons under two names, decided by an accident of
            // how they installed them. The path travels with the name; every merged source decodes to the
            // same pixels, so which file is actually read is a matter of indifference.
            if (name_rank(g_packs[j].kind) > name_rank(g_packs[i].kind)) {
                char keep[48]; lstrcpynA(keep, g_packs[i].name, sizeof(keep));
                lstrcpynA(g_packs[i].name, g_packs[j].name, sizeof(g_packs[i].name));
                lstrcpynA(g_packs[i].path, g_packs[j].path, sizeof(g_packs[i].path));
                g_packs[i].kind = g_packs[j].kind;
                lstrcpynA(g_packs[j].name, keep, sizeof(g_packs[j].name));   // the loser's name is the alias
            }
            // Remember the name we are dropping, so a config that named it still finds this entry.
            const int used = lstrlenA(g_packs[i].alias);
            if (used + (int)lstrlenA(g_packs[j].name) + 2 < (int)sizeof(g_packs[i].alias))
                _snprintf(g_packs[i].alias + used, sizeof(g_packs[i].alias) - used, "%s%s",
                          used ? "," : "", g_packs[j].name);
            for (int k = j; k + 1 < g_packN; ++k) g_packs[k] = g_packs[k + 1];
            --g_packN; ++merged;
        }
    }
    // Instrument the SUCCESS path : "which sheets did it find, and what did it fold together" is the first
    // question when the row shows something unexpected, and a scan that says nothing looks like one that never ran.
    windower::debug::log("icon packs: %d source(s), %d merged as duplicates", g_packN, merged);
    for (int i = 0; i < g_packN; ++i)
        windower::debug::log("  [%d] %-16s kind=%d %s%s%s", i, g_packs[i].name, g_packs[i].kind,
                             g_packs[i].path[0] ? g_packs[i].path : "(auto)",
                             g_packs[i].alias[0] ? "  = " : "", g_packs[i].alias);
    return g_packN;
}

const IconPack* icon_pack_at(int i) { return (i >= 0 && i < g_packN) ? &g_packs[i] : 0; }

// Match the entry's own name, then any name that MERGED into it -- a config written before two sources were
// folded together must keep working, and the name it holds may be the one that was dropped.
static bool name_matches(const IconPack& e, const char* name) {
    if (lstrcmpiA(e.name, name) == 0) return true;
    const int n = lstrlenA(name);
    for (const char* a = e.alias; *a; ) {
        const char* b = a; while (*b && *b != ',') ++b;
        if ((int)(b - a) == n && _strnicmp(a, name, (size_t)n) == 0) return true;
        a = *b ? b + 1 : b;
    }
    return false;
}

const IconPack* icon_pack_find(const char* name, const char* customPath, const char* bundledPath) {
    if (!name || !name[0]) return 0;
    icon_pack_count(customPath, bundledPath);
    for (int i = 0; i < g_packN; ++i)
        if (name_matches(g_packs[i], name)) return &g_packs[i];
    return 0;   // the pack named in the config is gone -> the caller falls back to Auto
}

} // namespace aio
