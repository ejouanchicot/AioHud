// map_dat.cpp -- see map_dat.h. Pure file I/O (no game-memory reads) : resolves the ROM path for a map
// file-id via the VTABLE/FTABLE volume scheme, reads the DAT, and decodes the type-0x20 8-bit-palette
// graphic to A8R8G8B8. Confirmed against the live install (Valkurm ROM/17/27.DAT, Selbina ROM/18/80.DAT).
#include "model/map_dat.h"
#include <windows.h>
#include <string.h>
#pragma comment(lib, "advapi32.lib")   // RegOpenKeyExA / RegQueryValueExA (PlayOnline install path)

namespace aio {

// --- FFXI install root (PlayOnline InstallFolder value "0001"), resolved once from the registry ---
static const char* ffxi_root() {
    static char root[MAX_PATH] = { 0 };
    static bool tried = false;
    if (tried) return root[0] ? root : 0;
    tried = true;
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
    static bool tried = false;
    if (tried) return root[0] ? root : 0;
    tried = true;
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
static char g_ovl[16][64]; static int g_ovlN = 0; static bool g_ovlTried = false;
static void load_overlays() {
    if (g_ovlTried) return; g_ovlTried = true;
    const char* wr = windower_root(); if (!wr) return;
    char p[MAX_PATH]; wsprintfA(p, "%s\\addons\\XIPivot\\data\\settings.xml", wr);
    unsigned n = 0; unsigned char* d = read_file(p, n);
    if (!d) return;
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
static bool g_tablesTried = false;

static void load_tables() {
    if (g_tablesTried) return; g_tablesTried = true;
    const char* root = ffxi_root(); if (!root) return;
    char p[MAX_PATH];
    for (int v = 1; v < MAX_VOL; ++v) {
        if (v == 1) wsprintfA(p, "%s\\VTABLE.DAT", root); else wsprintfA(p, "%s\\ROM%d\\VTABLE%d.DAT", root, v, v);
        unsigned vn = 0; unsigned char* vt = read_file(p, vn);
        if (!vt) continue;
        if (v == 1) wsprintfA(p, "%s\\FTABLE.DAT", root); else wsprintfA(p, "%s\\ROM%d\\FTABLE%d.DAT", root, v, v);
        unsigned fn = 0; unsigned char* ft = read_file(p, fn);
        if (!ft) { HeapFree(GetProcessHeap(), 0, vt); continue; }
        g_vt[v] = vt; g_vtN[v] = vn; g_ft[v] = ft; g_ftN[v] = fn; g_maxVol = v;
    }
}

// resolve a file-id to its ROM path : pick the highest volume v whose VTABLE_v[fileId]==v, then FTABLE_v.
static bool resolve_path(unsigned fileId, char* out) {
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
        const char* wr = windower_root();
        if (wr) {
            load_overlays();
            for (int i = 0; i < g_ovlN; ++i) {
                wsprintfA(out, "%s\\addons\\XIPivot\\data\\DATs\\%s\\%s\\%d\\%d.DAT", wr, g_ovl[i], rom, subdir, file);
                if (GetFileAttributesA(out) != INVALID_FILE_ATTRIBUTES) return true;   // overlay hit -> use the custom map
            }
        }
        wsprintfA(out, "%s\\%s\\%d\\%d.DAT", root, rom, subdir, file);   // vanilla ROM
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
    diag->fileSize = 0; diag->chunkTypes = 0; diag->W = diag->H = 0; diag->fmtFlags = 0;
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
                diag->step = MLS_BAD_FMT; diag->W = W; diag->H = Ht; diag->fmtFlags = flags;   // reached the graphic chunk
                if ((flags & 0x10) && W > 0 && Ht > 0 && W <= 2048 && Ht <= 2048) {
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

} // namespace aio
