// map_dat.cpp -- see map_dat.h. Pure file I/O (no game-memory reads) : resolves the ROM path for a map
// file-id via the VTABLE/FTABLE volume scheme, reads the DAT, and decodes the type-0x20 8-bit-palette
// graphic to A8R8G8B8. Confirmed against the live install (Valkurm ROM/17/27.DAT, Selbina ROM/18/80.DAT).
#include "model/map_dat.h"
#include "windower_debug.h"   // MENUDAT : the menu DAT's load, logged
#include "retry_clock.h"   // retry_due / retry_arm : the 0-sentinel-safe "try again later" (see the header for the 24.8 d trap)
#include <windows.h>
#include <string.h>
#include <new>      // std::nothrow : the menu DAT's buffers
#include <stdio.h>   // _snprintf : bounded path formatting (wsprintfA is capped by its own buffer, not the destination)
#pragma comment(lib, "advapi32.lib")   // RegOpenKeyExA / RegQueryValueExA (PlayOnline install path)

namespace aio {

// --- FFXI install root (PlayOnline InstallFolder value "0001"), resolved once from the registry ---
static const char* ffxi_root() {
    static char root[MAX_PATH] = { 0 };
    // rule10-ok: the failure this latches is PERMANENT by nature, not transient -- "no PlayOnline InstallFolder
    // value in the registry" does not become true mid-session, and the six keys are read in one pass. It is also
    // diagnosed rather than silent: resolve_path logs `step=NO FFXI ROOT` (verified 2026-07-26 audit).
    static bool tried = false;              // rule10-ok: see above
    if (tried) return root[0] ? root : 0;   // rule10-ok: see above
    tried = true;                           // rule10-ok: see above
    static const char* KEYS[] = {
        "SOFTWARE\\WOW6432Node\\PlayOnlineEU\\InstallFolder", "SOFTWARE\\WOW6432Node\\PlayOnlineUS\\InstallFolder",
        "SOFTWARE\\WOW6432Node\\PlayOnline\\InstallFolder",   "SOFTWARE\\PlayOnlineEU\\InstallFolder",
        "SOFTWARE\\PlayOnlineUS\\InstallFolder",              "SOFTWARE\\PlayOnline\\InstallFolder",
    };
    for (int i = 0; i < 6; ++i) {
        HKEY k;
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, KEYS[i], 0, KEY_READ | KEY_WOW64_32KEY, &k) != ERROR_SUCCESS) continue;
        DWORD sz = MAX_PATH, type = 0;
        LONG r = RegQueryValueExA(k, "0001", 0, &type, (BYTE*)root, &sz);
        RegCloseKey(k);
        if (r == ERROR_SUCCESS && type == REG_SZ && root[0]) return root;
        root[0] = 0;
    }
    return 0;
}

// read a whole file into a heap buffer (<= 64 MB). caller frees with HeapFree. null on failure.
static unsigned char* read_file(const char* path, unsigned& sizeOut) {
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (h == INVALID_HANDLE_VALUE) return 0;
    DWORD sz = GetFileSize(h, 0);
    if (sz == INVALID_FILE_SIZE || sz == 0 || sz > (64u << 20)) { CloseHandle(h); return 0; }
    unsigned char* buf = (unsigned char*)HeapAlloc(GetProcessHeap(), 0, sz);
    if (!buf) { CloseHandle(h); return 0; }
    DWORD got = 0; BOOL ok = ReadFile(h, buf, sz, &got, 0); CloseHandle(h);
    if (!ok || got != sz) { HeapFree(GetProcessHeap(), 0, buf); return 0; }
    sizeOut = sz; return buf;
}

// --- Windower root (our DLL is at <windower>\plugins\AioHud.dll -> strip two components), cached ---
static const char* windower_root() {
    static char root[MAX_PATH] = { 0 };
    static bool tried = false;   // rule10-ok: memoises the DLL's OWN module path (same argument as paths.cpp::plugin_dir) -- GetModuleHandleEx on our own address cannot fail transiently.
    if (tried) return root[0] ? root : 0;   // rule10-ok: see above
    tried = true;                           // rule10-ok: see above
    HMODULE hm = 0;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)&read_file, &hm) && hm) {
        char p[MAX_PATH];
        if (GetModuleFileNameA(hm, p, MAX_PATH)) {
            char* s = strrchr(p, '\\');                         // ...\plugins\AioHud.dll -> ...\plugins
            if (s) { *s = 0; s = strrchr(p, '\\'); if (s) { *s = 0; lstrcpynA(root, p, MAX_PATH); return root; } }
        }
    }
    return 0;
}

// --- XIPivot active overlays (data/settings.xml <overlays>a,b,c</overlays>), in priority order, cached ---
static char g_ovl[16][64]; static int g_ovlN = 0; static bool g_ovlTried = false;   // rule10-ok: NOT a one-shot -- it is the terminal flag of the bounded, time-spaced retry below (8 tries / 3 s). Only the name still says "tried".
static void load_overlays() {
    // Latch g_ovlTried only when settings.xml was actually READ (whether or not it had <overlays>), NOT on the first
    // attempt. It runs right after a zone-in -- the disk-busy / AV / Controlled-Folder-Access / XIPivot-mid-rewrite
    // window -- and giving up on one locked read left every custom/HD map silently disabled for the session, UNDER
    // the minimap's own retry budget. Mirrors load_tables() below : bounded, spaced retry, then stop. (rule 10)
    if (g_ovlTried) return;
    // retry_clock.h, like its twin load_tables() 45 lines below -- this was the one site of the pair still
    // hand-rolling the compare. It was CORRECT (it guarded the 0 sentinel), so this is de-duplication, not a
    // bug fix : two implementations of one rule that can drift, and the copy in this very file is the proof
    // that they do. Audit of 2026-09-12.
    static unsigned nextTryMs = 0; static int tries = 0;
    if (!retry_due(nextTryMs)) return;   // between attempts
    const char* wr = windower_root();
    // BOUNDED. wsprintfA is limited by its OWN 1 KB internal buffer, not by the destination -- a deep Windower
    // root overflows a char[MAX_PATH] and smashes the caller's stack frame. Same class the project already paid
    // for in gfx/window.cpp, and this is the path most likely to differ on a Program Files install.
    char p[MAX_PATH]; unsigned n = 0; unsigned char* d = 0;
    if (wr) { _snprintf(p, MAX_PATH, "%s\\addons\\XIPivot\\data\\settings.xml", wr); p[MAX_PATH - 1] = 0; d = read_file(p, n); }
    if (!d) { retry_arm(nextTryMs, 3000); if (++tries >= 8) g_ovlTried = true; return; }   // locked/absent -> retry, then give up   (rule10-ok: this IS the bounded budget)
    g_ovlTried = true;   // opened -> authoritative (even if it has no <overlays>)     (rule10-ok: latched on SUCCESS, not on the attempt)
    char* txt = (char*)HeapAlloc(GetProcessHeap(), 0, n + 1);
    if (txt) {
        memcpy(txt, d, n); txt[n] = 0;
        char* a = strstr(txt, "<overlays>");
        char* b = a ? strstr(a, "</overlays>") : 0;
        if (a && b) {
            a += 10; *b = 0;
            char* tok = a;
            while (*tok && g_ovlN < 16) {
                while (*tok == ' ' || *tok == '\t' || *tok == '\r' || *tok == '\n') ++tok;
                if (!*tok) break;
                int len = 0; while (tok[len] && tok[len] != ',' && len < 63) ++len;
                int t = len; while (t > 0 && (tok[t-1] == ' ' || tok[t-1] == '\t' || tok[t-1] == '\r' || tok[t-1] == '\n')) --t;
                if (t > 0) { memcpy(g_ovl[g_ovlN], tok, t); g_ovl[g_ovlN][t] = 0; ++g_ovlN; }
                tok += len; if (*tok == ',') ++tok; else break;
            }
        }
        HeapFree(GetProcessHeap(), 0, txt);
    }
    HeapFree(GetProcessHeap(), 0, d);
}

// --- VTABLE/FTABLE per volume, loaded once (v=1 : root VTABLE.DAT/FTABLE.DAT + ROM/ ; v>=2 : ROMv/VTABLEv.DAT) ---
static const int MAX_VOL = 10;
static unsigned char* g_vt[MAX_VOL] = { 0 }; static unsigned g_vtN[MAX_VOL] = { 0 };
static unsigned char* g_ft[MAX_VOL] = { 0 }; static unsigned g_ftN[MAX_VOL] = { 0 };
static int  g_maxVol = 0;
static bool g_tablesTried = false;   // rule10-ok: NOT a one-shot -- it only gates the 3 s spacing of the retry in load_tables() below, which caches on SUCCESS. Only the name still says "tried".

// Cache only SUCCESS. This latched `tried` up-front, so one unreadable VTABLE.DAT -- XIPivot rewriting its
// overlays, an AV hold -- left g_maxVol at 0 and made resolve_path() fail for EVERY map for the rest of the
// session. Worse, that negative cache sits UNDERNEATH the minimap's 12-retry budget, so the budget re-entered a
// poisoned cache and could never recover. Rule 10: a transient failure must not become permanent.
static void load_tables() {
    if (g_maxVol > 0) return;                       // already have usable tables -> done
    if (g_tablesTried) {                            // previous attempt found nothing : allow a retry, but not every call
        static unsigned nextTryMs = 0;              // via retry_clock.h : the hand-rolled compare here blocked EVERY retry past 24.8 d of uptime
        if (!retry_due(nextTryMs)) return;
        retry_arm(nextTryMs, 3000);
    }
    g_tablesTried = true;   // rule10-ok: see the declaration -- spacing flag for the bounded retry, not a give-up
    const char* root = ffxi_root(); if (!root) return;
    char p[MAX_PATH];
    for (int v = 1; v < MAX_VOL; ++v) {
        if (v == 1) _snprintf(p, MAX_PATH, "%s\\VTABLE.DAT", root); else _snprintf(p, MAX_PATH, "%s\\ROM%d\\VTABLE%d.DAT", root, v, v);
        p[MAX_PATH - 1] = 0;
        unsigned vn = 0; unsigned char* vt = read_file(p, vn);
        if (!vt) continue;
        if (v == 1) _snprintf(p, MAX_PATH, "%s\\FTABLE.DAT", root); else _snprintf(p, MAX_PATH, "%s\\ROM%d\\FTABLE%d.DAT", root, v, v);
        p[MAX_PATH - 1] = 0;
        unsigned fn = 0; unsigned char* ft = read_file(p, fn);
        if (!ft) { HeapFree(GetProcessHeap(), 0, vt); continue; }
        g_vt[v] = vt; g_vtN[v] = vn; g_ft[v] = ft; g_ftN[v] = fn; g_maxVol = v;
    }
}

// resolve a file-id to its ROM path : pick the highest volume v whose VTABLE_v[fileId]==v, then FTABLE_v.
static bool resolve_path(unsigned fileId, char* out, bool allowOverlay = true) {
    load_tables();
    const char* root = ffxi_root(); if (!root) return false;
    for (int v = g_maxVol; v >= 1; --v) {
        if (!g_vt[v] || fileId >= g_vtN[v] || g_vt[v][fileId] != (unsigned char)v) continue;
        if (fileId * 2 + 1 >= g_ftN[v]) continue;
        unsigned entry = g_ft[v][fileId * 2] | (g_ft[v][fileId * 2 + 1] << 8);   // FULL u16 LE
        if (!entry) continue;
        int subdir = (entry >> 7) & 0x1FF, file = entry & 0x7F;
        char rom[8]; if (v == 1) lstrcpyA(rom, "ROM"); else wsprintfA(rom, "ROM%d", v);
        // XIPivot overlays FIRST (custom maps : Remapster / Maps / ...), in the configured priority order
        const char* wr = allowOverlay ? windower_root() : 0;
        if (wr) {
            load_overlays();
            for (int i = 0; i < g_ovlN; ++i) {
                _snprintf(out, MAX_PATH, "%s\\addons\\XIPivot\\data\\DATs\\%s\\%s\\%d\\%d.DAT", wr, g_ovl[i], rom, subdir, file); out[MAX_PATH - 1] = 0;
                if (GetFileAttributesA(out) != INVALID_FILE_ATTRIBUTES) return true;   // overlay hit -> use the custom map
            }
        }
        _snprintf(out, MAX_PATH, "%s\\%s\\%d\\%d.DAT", root, rom, subdir, file); out[MAX_PATH - 1] = 0;   // vanilla ROM
        return true;
    }
    return false;
}

// --- DXT3 decode : FFXI CITY maps are DXT3 (type-0x20 graphic, format 0xA1, FourCC stored REVERSED "3TXD" at
// +0x39). Standard DXT3 block layout (8 alpha bytes then 8 colour bytes) ; pixel data at hbase+0x45. Confirmed
// against a reference (PIL) decoder on San d'Oria vanilla (512^2) + Remapster HD (2048^2). ---
static void dxt_colours(const unsigned char* cb, unsigned char cr[4], unsigned char cg[4], unsigned char cbb[4]) {
    const unsigned c0 = cb[0] | (cb[1] << 8), c1 = cb[2] | (cb[3] << 8);
    const int r0 = (c0 >> 11) & 0x1F, g0 = (c0 >> 5) & 0x3F, b0 = c0 & 0x1F;
    const int r1 = (c1 >> 11) & 0x1F, g1 = (c1 >> 5) & 0x3F, b1 = c1 & 0x1F;
    cr[0] = (unsigned char)((r0 << 3) | (r0 >> 2)); cg[0] = (unsigned char)((g0 << 2) | (g0 >> 4)); cbb[0] = (unsigned char)((b0 << 3) | (b0 >> 2));
    cr[1] = (unsigned char)((r1 << 3) | (r1 >> 2)); cg[1] = (unsigned char)((g1 << 2) | (g1 >> 4)); cbb[1] = (unsigned char)((b1 << 3) | (b1 >> 2));
    cr[2] = (unsigned char)((2 * cr[0] + cr[1]) / 3); cg[2] = (unsigned char)((2 * cg[0] + cg[1]) / 3); cbb[2] = (unsigned char)((2 * cbb[0] + cbb[1]) / 3);
    cr[3] = (unsigned char)((cr[0] + 2 * cr[1]) / 3); cg[3] = (unsigned char)((cg[0] + 2 * cg[1]) / 3); cbb[3] = (unsigned char)((cbb[0] + 2 * cbb[1]) / 3);
}
static void decode_dxt3_block(const unsigned char* src, u32* dst, int stride) {
    const unsigned char* ab = src;                              // 8 alpha bytes (4-bit / pixel)
    const unsigned char* cb = src + 8;                          // 8 colour bytes (c0, c1, 2-bit indices)
    unsigned char cr[4], cg[4], cbb[4]; dxt_colours(cb, cr, cg, cbb);
    const unsigned bits = cb[4] | (cb[5] << 8) | (cb[6] << 16) | ((unsigned)cb[7] << 24);
    for (int i = 0; i < 16; ++i) {
        const int ci = (bits >> (2 * i)) & 3, a4 = (ab[i >> 1] >> ((i & 1) * 4)) & 0xF;
        int a = (a4 << 4) | a4; a += a; if (a > 255) a = 255;   // FFXI map alpha : ~0x80 = fully opaque (match the 8bpp path). Vanilla DXT3 towns (Rabao a4~=8) were rendering at HALF opacity -> dark ; HD overlays (a4=0xF) stay 255.
        dst[(i >> 2) * stride + (i & 3)] = ((u32)a << 24) | ((u32)cr[ci] << 16) | ((u32)cg[ci] << 8) | (u32)cbb[ci];
    }
}
// decode a full DXT3 image at d+dataOff (W, Ht multiples of 4) -> a fresh W*Ht ARGB buffer, flipped vertically
// so it matches the vanilla 8bpp maps' bottom-up storage (the minimap draw V-flips both alike).
static u32* decode_dxt3_image(const unsigned char* d, unsigned dataOff, unsigned n, int W, int Ht) {
    const unsigned bw = (unsigned)(W / 4), bh = (unsigned)(Ht / 4);
    if (dataOff + bw * bh * 16u > n) return 0;
    u32* px = (u32*)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)W * Ht * 4);
    if (!px) return 0;
    const unsigned char* src = d + dataOff;
    for (unsigned by = 0; by < bh; ++by)
        for (unsigned bx = 0; bx < bw; ++bx)
            decode_dxt3_block(src + (by * bw + bx) * 16u, px + (by * 4u) * (unsigned)W + bx * 4u, W);
    for (int y = 0; y < Ht / 2; ++y) {                          // flip vertical -> match the 8bpp convention
        u32* a = px + (unsigned)y * (unsigned)W, * b = px + (unsigned)(Ht - 1 - y) * (unsigned)W;
        for (int x = 0; x < W; ++x) { u32 t = a[x]; a[x] = b[x]; b[x] = t; }
    }
    return px;
}

bool load_zone_map(unsigned fileId, u32*& outPixels, int& outW, int& outH, MapLoadDiag* diag) {
    outPixels = 0; outW = outH = 0;
    MapLoadDiag scratch; if (!diag) diag = &scratch;
    diag->step = MLS_NO_PATH; diag->path[0] = 0; diag->overlay = false;
    diag->fileSize = 0; diag->chunkTypes = 0; diag->W = diag->H = 0; diag->fmtFlags = 0; diag->biSize = diag->bpp = 0;
    char path[MAX_PATH];
    if (!ffxi_root()) { diag->step = MLS_NO_ROOT; return false; }
    if (!resolve_path(fileId, path)) return false;
    lstrcpynA(diag->path, path, sizeof(diag->path));
    for (const char* c = path; *c; ++c)                                  // case-insensitive "XIPivot" scan (no shlwapi dependency)
        if ((c[0]|32)=='x' && (c[1]|32)=='i' && (c[2]|32)=='p' && (c[3]|32)=='i' &&
            (c[4]|32)=='v' && (c[5]|32)=='o' && (c[6]|32)=='t') { diag->overlay = true; break; }
    diag->step = MLS_NO_FILE;
    unsigned n = 0; unsigned char* d = read_file(path, n);
    if (!d) return false;
    diag->fileSize = n;
    diag->step = MLS_NO_CHUNK;
    bool ok = false;
    // walk the chunk stream : 8-byte header, type = u32@+4 & 0x7F, size = (u32@+4 >> 7)*16 ; type 0x20 = graphic
    unsigned off = 0;
    while (off + 8 <= n) {
        unsigned w2 = *(const unsigned*)(d + off + 4);
        unsigned type = w2 & 0x7F, size = (w2 >> 7) * 16;
        if (size == 0) break;
        diag->chunkTypes |= (1u << (type & 31));                // what this DAT actually contains
        if (type == 0x20) {
            const unsigned hbase = off + 0x10;                  // graphic sub-header
            const unsigned palOff = hbase + 0x39 + 4, idxOff = palOff + 0x400;
            if (hbase + 0x25 + 4 <= n) {
                const unsigned char* H = d + hbase;
                const unsigned char flags = H[0x00];
                const int W = *(const int*)(H + 0x15), Ht = *(const int*)(H + 0x19);
                // The palettised branch below is an 8-bpp decoder with the palette right after a 40-byte header. Both were
                // TRUSTED, never read : a patch (or an overlay) shipping a 4-bpp map, or a longer header, would decode to
                // noise and be DRAWN. Measured 2026-09-13 : all 209 palettised maps of the zone-id ranges read biSize 40,
                // bpp 8 -- so requiring them rejects nothing that works, and turns that silent case into FORMAT REJECTED.
                const unsigned biSize = *(const unsigned*)(H + 0x11);
                const unsigned bpp    = *(const unsigned short*)(H + 0x1F);
                diag->step = MLS_BAD_FMT; diag->W = W; diag->H = Ht; diag->fmtFlags = flags;   // reached the graphic chunk
                diag->biSize = biSize; diag->bpp = bpp;
                if ((flags & 0x10) && biSize == 40 && bpp == 8 && W > 0 && Ht > 0 && W <= 2048 && Ht <= 2048) {
                    const unsigned need = (unsigned)(W * Ht);
                    if (idxOff + need <= n) {
                        u32* px = (u32*)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)W * Ht * 4);
                        if (px) {
                            const unsigned char* pal = d + palOff;
                            const unsigned char* idx = d + idxOff;
                            for (unsigned i = 0; i < need; ++i) {
                                u32 c = *(const u32*)(pal + (unsigned)idx[i] * 4);   // already B,G,R,A = A8R8G8B8 memory order
                                u32 a = (c >> 24) & 0xFF; a += a; if (a > 255) a = 255;   // alpha 0..0x80 -> 0..255
                                px[i] = (a << 24) | (c & 0x00FFFFFFu);
                            }
                            outPixels = px; outW = W; outH = Ht; ok = true;
                        }
                    }
                }
                else if (W > 0 && Ht > 0 && (W & 3) == 0 && (Ht & 3) == 0 && W <= 4096 && Ht <= 4096 &&
                         hbase + 0x45 <= n &&   // bounds : the FourCC (+0x39..+0x3C) and the +0x45 data offset are in-buffer
                         H[0x3A] == 'T' && H[0x3B] == 'X' && H[0x3C] == 'D' && H[0x39] == '3') {
                    u32* px = decode_dxt3_image(d, hbase + 0x45, n, W, Ht);   // FFXI city map : DXT3, data at +0x45
                    if (px) { outPixels = px; outW = W; outH = Ht; ok = true; }
                }
            }
            break;                                              // first graphic chunk is the map
        }
        off += size;
    }
    HeapFree(GetProcessHeap(), 0, d);
    if (ok) diag->step = MLS_OK;
    return ok;
}

void free_map_image(u32* pixels) { if (pixels) HeapFree(GetProcessHeap(), 0, pixels); }

// ---- the DAT filesystem, published for the other DAT readers (see map_dat.h) --------------------------------
// Thin wrappers, deliberately: resolve_path / read_file stay exactly as the minimap has always used them, and
// every DAT the plugin reads goes through this ONE install+overlay resolution instead of growing its own copy.
bool dat_resolve_path(unsigned fileId, char* out, unsigned cap) {
    if (!out || cap < MAX_PATH) return false;   // resolve_path writes MAX_PATH -- refuse a short buffer rather than smash it
    return resolve_path(fileId, out);
}
bool dat_resolve_vanilla(unsigned fileId, char* out, unsigned cap) {
    if (!out || cap < MAX_PATH) return false;
    return resolve_path(fileId, out, false);
}
const char* dat_windower_root() { return windower_root(); }

// ======== the client's MENU DAT : sheets, textures, the labels' letters (menu-dat-re-2026-11-21.md) ========
namespace {
unsigned char* g_md = 0; unsigned g_mdN = 0; int g_mdLang = -1, g_mdTries = 0; DWORD g_mdNext = 0; bool g_mdOk = false;
struct MdSheet { char name[17]; unsigned off, end; int nspr; unsigned* spr; };   // spr[i] : the sprite's offset in g_md
struct MdTex { char name[17]; unsigned off; int w, h, fmt; u32* px; bool tried; };   // fmt 1 DXT1, 3 DXT3 ; px decoded lazily
MdSheet g_mdSheet[64]; int g_mdSheets = 0; MdTex g_mdTex[96]; int g_mdTexN = 0;
unsigned g_mdWin[512]; int g_mdWins = 0;   // the window definitions (type 0x30) : their payload offsets
struct MdLabel { char sheet[17]; unsigned id; MenuLabel L; bool tried; };
MdLabel g_mdLab[256]; int g_mdLabN = 0;

void md_dxt_colours(const unsigned char* cb, unsigned char r[4], unsigned char g[4], unsigned char b[4], bool dxt1, bool& punch) {
    const unsigned short c0 = (unsigned short)(cb[0] | cb[1] << 8), c1 = (unsigned short)(cb[2] | cb[3] << 8);
    r[0] = (unsigned char)(((c0 >> 11) & 31) * 255 / 31); g[0] = (unsigned char)(((c0 >> 5) & 63) * 255 / 63); b[0] = (unsigned char)((c0 & 31) * 255 / 31);
    r[1] = (unsigned char)(((c1 >> 11) & 31) * 255 / 31); g[1] = (unsigned char)(((c1 >> 5) & 63) * 255 / 63); b[1] = (unsigned char)((c1 & 31) * 255 / 31);
    punch = dxt1 && c0 <= c1;
    if (!punch) { r[2] = (unsigned char)((2 * r[0] + r[1]) / 3); g[2] = (unsigned char)((2 * g[0] + g[1]) / 3); b[2] = (unsigned char)((2 * b[0] + b[1]) / 3);
                  r[3] = (unsigned char)((r[0] + 2 * r[1]) / 3); g[3] = (unsigned char)((g[0] + 2 * g[1]) / 3); b[3] = (unsigned char)((b[0] + 2 * b[1]) / 3); }
    else { r[2] = (unsigned char)((r[0] + r[1]) / 2); g[2] = (unsigned char)((g[0] + g[1]) / 2); b[2] = (unsigned char)((b[0] + b[1]) / 2); r[3] = g[3] = b[3] = 0; }
}
// a whole DXT1 / DXT3 image, top-left origin (no flip) ; DXT3 alpha doubled (the 0x80 = opaque convention)
u32* md_decode(const unsigned char* d, unsigned n, int W, int Ht, int fmt) {
    const int bw = (W + 3) / 4, bh = (Ht + 3) / 4, bs = fmt == 1 ? 8 : 16;
    if (W <= 0 || Ht <= 0 || W > 2048 || Ht > 2048 || (unsigned)(bw * bh * bs) > n) return 0;
    u32* px = new (std::nothrow) u32[(size_t)W * Ht]; if (!px) return 0;
    for (int by = 0; by < bh; ++by) for (int bx = 0; bx < bw; ++bx) {
        const unsigned char* s = d + (by * bw + bx) * bs;
        const unsigned char* cb = fmt == 1 ? s : s + 8;
        unsigned char r[4], g[4], b[4]; bool punch = false; md_dxt_colours(cb, r, g, b, fmt == 1, punch);
        const unsigned idx = (unsigned)(cb[4] | cb[5] << 8 | cb[6] << 16 | (unsigned)cb[7] << 24);
        for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x) {
            const int X = bx * 4 + x, Y = by * 4 + y; if (X >= W || Y >= Ht) continue;
            const int k = (idx >> (2 * (y * 4 + x))) & 3; int a = 255;
            if (fmt == 3) { const int a4 = (s[(y * 4 + x) / 2] >> (((y * 4 + x) & 1) * 4)) & 15; a = ((a4 << 4) | a4) * 2; if (a > 255) a = 255; }
            else if (punch && k == 3) a = 0;
            px[(size_t)Y * W + X] = ((u32)a << 24) | ((u32)r[k] << 16) | ((u32)g[k] << 8) | b[k];
        }
    }
    return px;
}
// the chunks : sheets (0x31) indexed with their sprites' offsets, textures (0x20) by name
bool md_index() {
    g_mdSheets = 0; g_mdTexN = 0; g_mdWins = 0;
    for (unsigned at = 0; at + 16 <= g_mdN; ) {
        const unsigned w = *(const unsigned*)(g_md + at + 4), type = w & 0x7F, size = ((w >> 7) & 0x7FFFF) * 16;
        if (size < 16 || at + size > g_mdN) break;
        const unsigned p = at + 16, pe = at + size;
        if (type == 0x30 && g_mdWins < 512 && p + 0x40 < pe) g_mdWin[g_mdWins++] = p;
        if (type == 0x31 && g_mdSheets < 64 && p + 17 < pe) {
            MdSheet& S = g_mdSheet[g_mdSheets]; memcpy(S.name, g_md + p, 16); S.name[16] = 0;
            const unsigned ntex = g_md[p + 16]; unsigned q = p + 17 + ntex * 16;
            if (q + 2 <= pe) {
                const int ns = *(const unsigned short*)(g_md + q); q += 2;
                S.spr = new (std::nothrow) unsigned[ns > 0 ? ns : 1]; S.nspr = 0; S.off = p; S.end = pe;
                for (int i = 0; S.spr && i < ns && q < pe; ++i) { S.spr[S.nspr++] = q; q += 1 + (unsigned)g_md[q] * 61; }
                if (S.spr && S.nspr == ns && q <= pe) ++g_mdSheets; else { delete[] S.spr; S.spr = 0; }
            }
        } else if (type == 0x20 && g_mdTexN < 96 && p + 0x45 <= pe) {
            MdTex& T = g_mdTex[g_mdTexN]; memcpy(T.name, g_md + p + 1, 16); T.name[16] = 0;
            T.w = *(const int*)(g_md + p + 0x15); T.h = *(const int*)(g_md + p + 0x19);
            const char* fc = (const char*)(g_md + p + 0x39);
            T.fmt = !memcmp(fc, "1TXD", 4) ? 1 : !memcmp(fc, "3TXD", 4) ? 3 : 0; T.off = p + 0x45; T.px = 0; T.tried = false;
            if (T.fmt && T.w > 0 && T.h > 0) ++g_mdTexN;
        }
        if (type == 0) break;
        at += size;
    }
    return g_mdSheets > 0 && g_mdTexN > 0;
}
MdTex* md_tex(const char* name16) {
    for (int i = 0; i < g_mdTexN; ++i) if (!memcmp(g_mdTex[i].name, name16, 16)) {
        MdTex& T = g_mdTex[i];
        if (!T.px && !T.tried) { T.tried = true; T.px = md_decode(g_md + T.off, g_mdN - T.off, T.w, T.h, T.fmt);
            if (!T.px) windower::debug::log("MENUDAT texture '%.16s' %dx%d fmt %d : NOT decoded", T.name, T.w, T.h, T.fmt); }
        return T.px ? &T : 0;
    }
    return 0;
}
} // namespace

// the whole menu DAT (~4.5 MB, one VirtualAlloc-sized heap block) + its decoded font textures + the baked labels.
// Called at a reload of another language AND at the plugin's unload : until 2026-11-23 nothing freed it at the unload,
// and every //unload + //load left the 4.5 MB block behind (VMDIFF : one NEW ~4500 KB private base per reload).
void menu_dat_free() {
    if (g_md) { dat_free_file(g_md); g_md = 0; g_mdN = 0; }
    for (int i = 0; i < g_mdSheets; ++i) { delete[] g_mdSheet[i].spr; g_mdSheet[i].spr = 0; }
    for (int i = 0; i < g_mdTexN; ++i) { delete[] g_mdTex[i].px; g_mdTex[i].px = 0; }
    for (int i = 0; i < g_mdLabN; ++i) { delete[] g_mdLab[i].L.px; g_mdLab[i].L.px = 0; }
    g_mdSheets = g_mdTexN = g_mdLabN = 0; g_mdOk = false; g_mdLang = -1;
}

bool menu_dat_load(int lang) {
    static const unsigned IDS[4] = { 39532, 39542, 55746, 56166 };
    if (lang < 0 || lang > 3) return false;
    if (g_mdOk && g_mdLang == lang) return true;
    if (g_mdTries >= 6 || (g_mdTries && (int)(GetTickCount() - g_mdNext) < 0)) return false;
    ++g_mdTries; g_mdNext = GetTickCount() + 10000;
    menu_dat_free();
    char path[MAX_PATH] = "";
    if (!dat_resolve_path(IDS[lang], path, sizeof(path))) { windower::debug::log("MENUDAT lang %d : file %u NOT resolved (try %d)", lang, IDS[lang], g_mdTries); return false; }
    g_md = dat_read_file(path, g_mdN);
    if (!g_md) { windower::debug::log("MENUDAT lang %d : '%s' NOT read (try %d)", lang, path, g_mdTries); return false; }
    g_mdOk = md_index(); g_mdLang = lang;
    windower::debug::log("MENUDAT lang %d : '%s' %u bytes -- %d sheets, %d textures%s (try %d)", lang, path, g_mdN, g_mdSheets, g_mdTexN,
                         g_mdOk ? "" : " -- NOT a menu DAT : labels stay text", g_mdTries);
    if (g_mdOk) g_mdTries = 0;
    return g_mdOk;
}

const MenuLabel* menu_label(const char* sheet16, unsigned sprite) {
    if (!g_mdOk || !sheet16) return 0;
    for (int i = 0; i < g_mdLabN; ++i) if (g_mdLab[i].id == sprite && !memcmp(g_mdLab[i].sheet, sheet16, 16)) return g_mdLab[i].L.px ? &g_mdLab[i].L : 0;
    if (g_mdLabN >= 256) return 0;
    MdLabel& M = g_mdLab[g_mdLabN++]; memset(&M, 0, sizeof(M)); memcpy(M.sheet, sheet16, 16); M.id = sprite; M.tried = true;
    const MdSheet* S = 0; for (int i = 0; i < g_mdSheets; ++i) if (!memcmp(g_mdSheet[i].name, sheet16, 16)) S = &g_mdSheet[i];
    if (!S || sprite >= (unsigned)S->nspr) return 0;
    const unsigned q = S->spr[sprite]; const int np = g_md[q];
    // the LETTER parts : a "font    ..." texture, not the shadow pass (flag byte 1 = 2) -- no button, no frame (hfr1 / newtex : the skin's DATs)
    int x0 = 1 << 30, y0 = 1 << 30, x1 = -(1 << 30), y1 = -(1 << 30), nl = 0;
    for (int k = 0; k < np; ++k) {
        const unsigned char* P = g_md + q + 1 + k * 61; if (q + 1 + (k + 1) * 61 > S->end) return 0;
        if (P[0x2A] == 2 || memcmp(P + 0x2D, "font    ", 8)) continue;   // the letters only (a font texture) : no button, no frame, no shadow pass
        const short* c = (const short*)P; const int ax = c[0], ay = c[1], bx = c[6], by = c[7];
        if (ax < x0) x0 = ax; if (ay < y0) y0 = ay; if (bx > x1) x1 = bx; if (by > y1) y1 = by; ++nl;
    }
    if (!nl || x1 <= x0 || y1 <= y0 || x1 - x0 > 512 || y1 - y0 > 512) return 0;
    const int W = x1 - x0, Hh = y1 - y0;
    u32* px = new (std::nothrow) u32[(size_t)W * Hh]; if (!px) return 0;
    memset(px, 0, (size_t)W * Hh * 4);
    for (int k = 0; k < np; ++k) {
        const unsigned char* P = g_md + q + 1 + k * 61;
        if (P[0x2A] == 2 || memcmp(P + 0x2D, "font    ", 8)) continue;   // the letters only (a font texture) : no button, no frame, no shadow pass
        MdTex* T = md_tex((const char*)P + 0x2D); if (!T) { delete[] px; windower::debug::log("MENUDAT sprite %u : its texture '%.16s' missing -- the text label stays", sprite, (const char*)P + 0x2D); return 0; }
        const short* c = (const short*)P; const int ax = c[0] - x0, ay = c[1] - y0, bx = c[6] - x0, by = c[7] - y0;
        const unsigned short* r = (const unsigned short*)(P + 0x10); const int sw = r[0], sh = r[1], su = r[2], sv = r[3];
        const int dw = bx - ax, dh = by - ay; if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0) continue;
        for (int y = 0; y < dh; ++y) for (int x = 0; x < dw; ++x) {
            const int tx = su + x * sw / dw, ty = sv + y * sh / dh; if (tx < 0 || ty < 0 || tx >= T->w || ty >= T->h) continue;
            const int X = ax + x, Y = ay + y; if (X < 0 || Y < 0 || X >= W || Y >= Hh) continue;
            const u32 t = T->px[(size_t)ty * T->w + tx]; const unsigned a = t >> 24;
            // white, the glyph's coverage as alpha (its luminance x its alpha : the font is white on black or on clear)
            const unsigned lum = (((t >> 16) & 255) + ((t >> 8) & 255) + (t & 255)) / 3, cov = a * lum / 255;
            u32& o = px[(size_t)Y * W + X]; if (cov > (o >> 24)) o = (cov << 24) | 0x00FFFFFFu;
        }
    }
    M.L.w = W; M.L.h = Hh; M.L.px = px; M.L.ox = x0; M.L.oy = y0;
    return &M.L;
}

bool menu_window_frame(const char* name8, unsigned& sprite) {
    if (!g_mdOk || !name8) return false;
    char want[9]; memset(want, ' ', 8); want[8] = 0; for (int i = 0; i < 8 && name8[i]; ++i) want[i] = name8[i];
    for (int i = 0; i < g_mdWins; ++i) {
        const unsigned p = g_mdWin[i];
        if (memcmp(g_md + p, "menu    ", 8) || memcmp(g_md + p + 8, want, 8)) continue;
        const unsigned rec = p + 0x20; const int nr = g_md[rec + 0x14];
        for (int q = 0; q < nr && q < 8; ++q) {
            const unsigned char* R = g_md + rec + 0x20 + q * 0x14; if (rec + 0x20 + (q + 1) * 0x14 > g_mdN) return false;
            const unsigned short st = (unsigned short)(R[0] | R[1] << 8), id = (unsigned short)(R[2] | R[3] << 8);
            if (st == 0 && !memcmp(R + 4, "menu    windowps", 16)) { sprite = id; return true; }
        }
        return false;
    }
    return false;
}

unsigned char* dat_read_file(const char* path, unsigned& sizeOut) { sizeOut = 0; return path ? read_file(path, sizeOut) : 0; }
void dat_free_file(unsigned char* buf) { if (buf) HeapFree(GetProcessHeap(), 0, buf); }

} // namespace aio
