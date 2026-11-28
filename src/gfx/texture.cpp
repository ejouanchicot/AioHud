// texture.cpp -- see texture.h.
#include "texture.h"
#include "gear_dat.h"   // the gear-icon decode DECISIONS (pure, tested) -- this file only does the I/O
#include "noise.h"
#include "model/paths.h"   // plugin_path_r : runtime-derived asset paths (gfx infra exception to the layering rule)
#include <windows.h>
#include <math.h>
#include <stdio.h>   // gear-icon ROM fallback : fopen/fseek/fread + _snprintf
#include <errno.h>   // //aio geartrace : distinguish "read-only folder" (EACCES) from the other write failures

#include <intrin.h>   // _ReturnAddress : who created a texture still alive at unload
#include "windower_debug.h"

namespace aio {

// ---- LIVE-TEXTURE LEDGER (2026-11-17 : each //unload + //load left ~40 MB behind ; the client then ran out of
// address space and crashed). Every texture this file creates is entered with its size and its creator's return
// address ; the LAST Release (refcount 0) takes it out. What remains after Hud::dispose is a leak, by name of caller.
struct TexRec { u32 tex; u32 bytes; u32 ra; unsigned short w, h; };
static TexRec s_tl[2048]; static int s_tlN = 0;
static void tl_add(u32 tex, int W, int H, bool mips, void* ra) {
    if (!tex || s_tlN >= 2048) return;
    const u32 b = (u32)W * (u32)H * 4u;
    TexRec& r = s_tl[s_tlN++]; r.tex = tex; r.bytes = mips ? b + b / 3 : b; r.ra = (u32)ra; r.w = (unsigned short)W; r.h = (unsigned short)H;
}
static void tl_remove(u32 tex) {
    for (int i = 0; i < s_tlN; ++i) if (s_tl[i].tex == tex) { s_tl[i] = s_tl[--s_tlN]; return; }
}
void texture_ledger_report(const char* when) {
    u32 total = 0; for (int i = 0; i < s_tlN; ++i) total += s_tl[i].bytes;
    HMODULE me = 0; GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)&texture_ledger_report, &me);
    windower::debug::log("TEXLIVE %-14s %d texture(s) alive, %u KB (managed : the same again in system memory)", when, s_tlN, total >> 10);
    if (strncmp(when, "unload", 6) != 0) return;
    for (int i = 0; i < s_tlN && i < 60; ++i)
        windower::debug::log("TEXLIVE   %4ux%-4u %6u KB  creator AioHud.dll+0x%06X", s_tl[i].w, s_tl[i].h, s_tl[i].bytes >> 10, s_tl[i].ra - (u32)me);
}

// small struct matching D3DLOCKED_RECT's first two fields (Pitch, pBits).
struct LR { int Pitch; void* pBits; };

// create an A8R8G8B8 MANAGED texture and lock its top mip for writing.
// returns the texture (0 on failure); fills lr on success.
static u32 create_locked(u32 dev, int W, int H, LR* lr)
{
    lr->Pitch = 0; lr->pBits = 0;
    auto fCreate = vmethod<long(__stdcall*)(u32,u32,u32,u32,u32,u32,u32,u32*)>(dev, 20);
    u32 tex = 0;
    if (!fCreate || fCreate(dev, W, H, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex) < 0 || !valid_ptr(tex))
        return 0;
    tl_add(tex, W, H, false, _ReturnAddress());
    auto fLock = vmethod<long(__stdcall*)(u32,u32,void*,void*,u32)>(tex, 16);
    if (!fLock || fLock(tex, 0, lr, 0, 0) < 0 || !lr->pBits) { release_texture(tex); return 0; }   // lock failed -> don't hand back an uninitialised (garbage-pixel) texture
    return tex;
}

static void unlock(u32 tex)
{
    auto fUnlock = vmethod<long(__stdcall*)(u32,u32)>(tex, 17);
    if (fUnlock) fUnlock(tex, 0);
}

u32 make_texture_blank(u32 dev, int W, int H)
{
    if (W <= 0 || H <= 0) return 0;
    LR lr; u32 tex = create_locked(dev, W, H, &lr);
    if (!tex) return 0;
    for (int y = 0; y < H; ++y) memset((char*)lr.pBits + y * lr.Pitch, 0, (size_t)W * 4);
    unlock(tex);
    return tex;
}

bool texture_write(u32 tex, int x, int y, int w, int h, const u32* px)
{
    if (!tex || !px || w <= 0 || h <= 0) return false;
    auto fLock = vmethod<long(__stdcall*)(u32,u32,void*,const RECT*,u32)>(tex, 16);
    if (!fLock) return false;
    RECT r = { x, y, x + w, y + h }; LR lr = { 0, 0 };
    bool ok = false;
    __try {
        if (fLock(tex, 0, &lr, &r, 0) >= 0 && lr.pBits) {
            for (int yy = 0; yy < h; ++yy) memcpy((char*)lr.pBits + yy * lr.Pitch, px + yy * w, (size_t)w * 4);
            unlock(tex); ok = true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    return ok;
}

u32 make_liquid_texture(u32 dev, int variant)
{
    const int W = 512, H = 64;   // ~8:1, MATCHES the bar's aspect -> shown 1x (no tiling, no repeat)
    LR lr; u32 tex = create_locked(dev, W, H, &lr);
    if (tex && lr.pBits) {
        for (int y = 0; y < H; y++) {
            u32* row = (u32*)((char*)lr.pBits + y * lr.Pitch);
            for (int x = 0; x < W; x++) {
                float u = (float)x / W, v = (float)y / H;
                // SMOOTH liquid: broad, soft, LOW-CONTRAST light variation -- no thin
                // filaments, no busy detail. The "liquid" feel comes from the MOTION
                // (flow + traveling specular + meniscus), not from a detailed texture.
                float bright;
                if (variant == 1) {                                  // MP : slow arcane swirl (gentle, low freq)
                    float wx = fbm(1 * u, 1 * v + 0.30f) - 0.5f;
                    float n  = fbm(3 * u + 0.6f * wx, 1 * v);
                    bright = 0.45f + 0.48f * n;
                } else {                                             // HP / TP : soft broad liquid (8:1 texture)
                    int fu = (variant == 2) ? 5 : 3;                 // horizontal cycles (texture is 8:1 -> gentle streak)
                    int fv = 1;                                      // 1 -> large soft vertical variation (no tiny detail)
                    float n  = fbm(fu * u, fv * v);
                    float c  = (variant == 2) ? 0.50f : 0.42f;       // TP a touch more contrast (energetic)
                    bright = (0.5f - 0.5f * c) + c * n;
                }
                bright = 0.62f + 0.38f * bright;                     // HIGH floor -> the grey texture barely dims the vivid colour
                if (bright > 1.0f) bright = 1.0f; if (bright < 0.0f) bright = 0.0f;
                u32 b = (u32)(bright * 255.0f);
                // mostly opaque -> colour reads saturated over the dark game background.
                float al = 0.74f + 0.24f * bright;
                u32 av = (u32)(al * 255.0f);
                row[x] = (av << 24) | (b << 16) | (b << 8) | b;
            }
        }
        unlock(tex);
    }
    return tex;
}

// ---- MATERIAL textures for the procedural box themes (real procedural textures, not
// flat quads). Each is a GRAYSCALE luminance pattern ; the box colours it per hue via MODULATE. ----

// WOOD : horizontal planks with fbm grain + soft seams (stretched to the box -> ~5 planks).
u32 make_wood(u32 dev, int W, int H) {
    LR lr; u32 tex = create_locked(dev, W, H, &lr);
    if (tex && lr.pBits) for (int y = 0; y < H; y++) {
        u32* row = (u32*)((char*)lr.pBits + y * lr.Pitch);
        for (int x = 0; x < W; x++) {
            const float u = (float)x / W, v = (float)y / H;
            const float warp = fbm(2.0f * u, 2.0f * v) - 0.5f;                    // domain warp -> the grain FLOWS (GENERAL wood surface, no per-row planks)
            const float g  = fbm(2.0f * u, 20.0f * v + 6.0f * warp);              // flowing horizontal wood grain
            const float g2 = fbm(3.0f * u, 6.0f * v);                             // broad tone variation
            float L = 0.60f + 0.30f * g + 0.10f * (g2 - 0.5f); if (L < 0) L = 0; if (L > 1) L = 1;
            const unsigned char b = (unsigned char)(L * 255.0f);
            row[x] = 0xFF000000u | ((u32)b << 16) | ((u32)b << 8) | (u32)b;
        }
    }
    unlock(tex); return tex;
}

// FROST : soft cold cloud noise (SOFT -- no sharp lines / broken-glass look).
u32 make_frost(u32 dev, int W, int H) {
    LR lr; u32 tex = create_locked(dev, W, H, &lr);
    if (tex && lr.pBits) for (int y = 0; y < H; y++) {
        u32* row = (u32*)((char*)lr.pBits + y * lr.Pitch);
        for (int x = 0; x < W; x++) {
            const float u = (float)x / W, v = (float)y / H;
            const float c = fbm(3.0f * u, 3.0f * v), c2 = fbm(6.0f * u + 1.3f, 6.0f * v + 0.7f), c3 = fbm(12.0f * u, 12.0f * v);
            float f = 0.5f * c + 0.32f * c2 + 0.18f * c3;
            float L = 0.62f + 0.38f * f; if (L < 0) L = 0; if (L > 1) L = 1;      // pale, soft
            const unsigned char b = (unsigned char)(L * 255.0f);
            row[x] = 0xFF000000u | ((u32)b << 16) | ((u32)b << 8) | (u32)b;
        }
    }
    unlock(tex); return tex;
}

// VELVET : soft low-contrast mottled nap (dark).
u32 make_velvet(u32 dev, int W, int H) {
    LR lr; u32 tex = create_locked(dev, W, H, &lr);
    if (tex && lr.pBits) for (int y = 0; y < H; y++) {
        u32* row = (u32*)((char*)lr.pBits + y * lr.Pitch);
        for (int x = 0; x < W; x++) {
            const float u = (float)x / W, v = (float)y / H;
            const float n = fbm(5.0f * u, 5.0f * v), n2 = fbm(11.0f * u, 11.0f * v);
            float mottle = 0.62f * n + 0.38f * n2;
            float L = 0.52f + 0.42f * mottle; if (L < 0) L = 0; if (L > 1) L = 1;  // mid, soft (tint makes it dark velvet)
            const unsigned char b = (unsigned char)(L * 255.0f);
            row[x] = 0xFF000000u | ((u32)b << 16) | ((u32)b << 8) | (u32)b;
        }
    }
    unlock(tex); return tex;
}

// METAL : brushed steel -- fine horizontal brush noise + a broad diagonal sheen.
u32 make_metal(u32 dev, int W, int H) {
    LR lr; u32 tex = create_locked(dev, W, H, &lr);
    if (tex && lr.pBits) for (int y = 0; y < H; y++) {
        u32* row = (u32*)((char*)lr.pBits + y * lr.Pitch);
        for (int x = 0; x < W; x++) {
            const float u = (float)x / W, v = (float)y / H;
            const float brush = fbm(2.0f * u, 55.0f * v);                         // smooth horizontal brush streaks (tiles : integer freqs)
            const float fine  = fbm(4.0f * u, 110.0f * v);                        // subtle fine grain
            const float sheen = 0.5f + 0.5f * sinf((u + v) * 6.2831853f);         // diagonal polished sheen (period 1 -> tiles)
            float L = 0.54f + 0.20f * (brush - 0.5f) * 2.0f + 0.06f * (fine - 0.5f) * 2.0f + 0.16f * sheen; if (L < 0) L = 0; if (L > 1) L = 1;
            const unsigned char b = (unsigned char)(L * 255.0f);
            row[x] = 0xFF000000u | ((u32)b << 16) | ((u32)b << 8) | (u32)b;
        }
    }
    unlock(tex); return tex;
}

u32 make_glow(u32 dev)
{
    const int W = 128, H = 64;
    LR lr; u32 tex = create_locked(dev, W, H, &lr);
    if (tex && lr.pBits) {
        for (int y = 0; y < H; y++) {
            u32* row = (u32*)((char*)lr.pBits + y * lr.Pitch);
            for (int x = 0; x < W; x++) {
                float nx = ((x + 0.5f) / W) * 2.0f - 1.0f;
                float ny = ((y + 0.5f) / H) * 2.0f - 1.0f;
                // CAPSULE / stadium falloff: flat bright core with SEMICIRCULAR rounded
                // ends. cx = straight half-length; kx compresses the end caps in
                // texture-x so they look round once the quad is stretched (~3.6x) wide.
                const float cx = 0.72f, kx = 3.6f;
                float dx = fabsf(nx) - cx; if (dx < 0) dx = 0;
                float d  = sqrtf((dx * kx) * (dx * kx) + ny * ny);   // 0 core .. 1 edges, rounded ends
                float a  = 1.0f - d; if (a < 0) a = 0; if (a > 1) a = 1;
                a = a * (2.0f - a);                          // ease-out -> SOFT tail, melts to 0
                u32 av = (u32)(a * 255.0f);
                row[x] = 0xFF000000 | (av << 16) | (av << 8) | av;   // shape in RGB, alpha opaque
            }
        }
        unlock(tex);
    }
    return tex;
}

u32 make_bubble(u32 dev)
{
    const int W = 32, H = 32;
    LR lr; u32 tex = create_locked(dev, W, H, &lr);
    if (tex && lr.pBits) {
        for (int y = 0; y < H; y++) {
            u32* row = (u32*)((char*)lr.pBits + y * lr.Pitch);
            for (int x = 0; x < W; x++) {
                float nx = ((x + 0.5f) / W) * 2.0f - 1.0f;
                float ny = ((y + 0.5f) / H) * 2.0f - 1.0f;
                float d = sqrtf(nx * nx + ny * ny);
                float body = 1.0f - d; if (body < 0) body = 0; body = body * body * 0.45f; // faint fill
                float rim = 1.0f - fabsf(d - 0.72f) * 4.5f; if (rim < 0) rim = 0;           // bright thin ring
                float a = body + rim * 0.95f; if (a > 1) a = 1; if (a < 0) a = 0;
                u32 av = (u32)(a * 255.0f);
                row[x] = 0xFF000000 | (av << 16) | (av << 8) | av;   // shape in RGB, alpha opaque
            }
        }
        unlock(tex);
    }
    return tex;
}

// NB on the share mode used by every asset reader here: FILE_SHARE_WRITE | FILE_SHARE_DELETE, not just
// FILE_SHARE_READ. Sharing READ alone locks OTHER processes out of WRITING the file for as long as we hold the
// handle -- and that broke the updater in dual-box: the addon unloads the plugin on both clients, the updater
// starts extracting, one client reloads and draws its first frame, the lazy asset load opens cap_front.bin, and
// Expand-Archive fails with "access denied" on it. The whole file is read into memory and the handle closed
// immediately, so letting a writer replace it underneath us costs nothing (we keep the bytes we already read).
u32 load_raw_texture(u32 dev, const char* path, int W, int H)
{
    HANDLE hf = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (hf == INVALID_HANDLE_VALUE) return 0;
    DWORD need = (DWORD)(W * H * 4), got = 0;
    char* buf = (char*)HeapAlloc(GetProcessHeap(), 0, need);
    if (!buf) { CloseHandle(hf); return 0; }
    BOOL ok = ReadFile(hf, buf, need, &got, 0);
    CloseHandle(hf);
    if (!ok || got != need) { HeapFree(GetProcessHeap(), 0, buf); return 0; }

    LR lr; u32 tex = create_locked(dev, W, H, &lr);
    if (tex && lr.pBits) {
        for (int y = 0; y < H; y++) {
            u32* dpix = (u32*)((char*)lr.pBits + y * lr.Pitch);
            u32* spix = (u32*)(buf + y * W * 4);
            for (int xx = 0; xx < W; xx++) dpix[xx] = spix[xx];
        }
        unlock(tex);
    }
    HeapFree(GetProcessHeap(), 0, buf);
    return tex;
}

// The in-memory twin of load_raw_texture (same single-level surface, same straight copy) -- for pixels that
// were decoded rather than read from an .raw. Kept mip-free ON PURPOSE : it backs the status-icon atlas, which
// has always been a single level, and a sheet that silently changed filtering when its SOURCE changed would be
// a visual regression disguised as a data change.
u32 make_texture_argb(u32 dev, int W, int H, const u32* pixels)
{
    if (!pixels || W <= 0 || H <= 0) return 0;
    LR lr; u32 tex = create_locked(dev, W, H, &lr);
    if (!tex) return 0;
    for (int y = 0; y < H; ++y) {
        u32* dpix = (u32*)((char*)lr.pBits + y * lr.Pitch);
        const u32* spix = pixels + (unsigned)y * (unsigned)W;
        for (int x = 0; x < W; ++x) dpix[x] = spix[x];
    }
    unlock(tex);
    return tex;
}

// same as load_raw_texture but with a full MIP CHAIN -> clean minification (icons drawn much smaller than
// their native size stay crisp instead of aliasing).
u32 load_raw_texture_mip(u32 dev, const char* path, int W, int H)
{
    HANDLE hf = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (hf == INVALID_HANDLE_VALUE) return 0;
    DWORD need = (DWORD)(W * H * 4), got = 0;
    u32* buf = (u32*)HeapAlloc(GetProcessHeap(), 0, need);
    if (!buf) { CloseHandle(hf); return 0; }
    BOOL ok = ReadFile(hf, buf, need, &got, 0);
    CloseHandle(hf);
    u32 tex = (ok && got == need) ? make_texture_argb_mip(dev, W, H, buf) : 0;
    HeapFree(GetProcessHeap(), 0, buf);
    return tex;
}

static int mip_count(int w, int h) {
    int n = 1; while (w > 1 || h > 1) { w >>= 1; if (!w) w = 1; h >>= 1; if (!h) h = 1; n++; } return n;
}

u32 make_texture_argb_mip(u32 dev, int W, int H, const u32* pixels, long* out_why)
{
    if (out_why) *out_why = 0;
    int levels = mip_count(W, H);
    auto fCreate = vmethod<long(__stdcall*)(u32,u32,u32,u32,u32,u32,u32,u32*)>(dev, 20);
    u32 tex = 0;
    if (!fCreate) { if (out_why) *out_why = 1; return 0; }
    const long hr = fCreate(dev, W, H, levels, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex);
    if (hr < 0)            { if (out_why) *out_why = hr; return 0; }
    // A texture the runtime says it created but whose handle we refuse must still be RELEASED -- returning 0 on
    // it used to leak the object outright (and it fired for real : see valid_ptr in include/windower.h).
    if (!valid_ptr(tex))   { if (out_why) *out_why = 2;  release_texture(tex); return 0; }
    tl_add(tex, W, H, levels > 1, _ReturnAddress());
    auto fLock   = vmethod<long(__stdcall*)(u32,u32,void*,void*,u32)>(tex, 16);
    auto fUnlock = vmethod<long(__stdcall*)(u32,u32)>(tex, 17);

    u32* cur = (u32*)HeapAlloc(GetProcessHeap(), 0, W * H * 4);
    if (!cur) { if (out_why) *out_why = 3; release_texture(tex); return 0; }   // fail CLOSED (else a live but never-filled texture = garbage pixels)
    for (int i = 0; i < W * H; ++i) cur[i] = pixels[i];

    int lw = W, lh = H;
    for (int lvl = 0; lvl < levels; ++lvl) {
        LR lr; lr.Pitch = 0; lr.pBits = 0;
        if (fLock && fLock(tex, lvl, &lr, 0, 0) >= 0 && lr.pBits) {
            for (int y = 0; y < lh; ++y) {
                u32* d = (u32*)((char*)lr.pBits + y * lr.Pitch);
                for (int x = 0; x < lw; ++x) d[x] = cur[y * lw + x];
            }
            if (fUnlock) fUnlock(tex, lvl);
        }
        if (lvl + 1 >= levels) break;
        int nw = lw > 1 ? lw / 2 : 1, nh = lh > 1 ? lh / 2 : 1;
        u32* nxt = (u32*)HeapAlloc(GetProcessHeap(), 0, nw * nh * 4);
        if (!nxt) { if (out_why) *out_why = 3; HeapFree(GetProcessHeap(), 0, cur); release_texture(tex); return 0; }   // fail CLOSED like the initial alloc : a partially-filled texture would leave garbage in the upper (unlocked) MANAGED mips
        for (int y = 0; y < nh; ++y) for (int x = 0; x < nw; ++x) {
            int x0 = x * 2, y0 = y * 2, x1 = (x0 + 1 < lw) ? x0 + 1 : x0, y1 = (y0 + 1 < lh) ? y0 + 1 : y0;
            u32 a = cur[y0 * lw + x0], b = cur[y0 * lw + x1], c = cur[y1 * lw + x0], e = cur[y1 * lw + x1];
            u32 A = (((a>>24)&0xFF)+((b>>24)&0xFF)+((c>>24)&0xFF)+((e>>24)&0xFF)) / 4;
            u32 R = (((a>>16)&0xFF)+((b>>16)&0xFF)+((c>>16)&0xFF)+((e>>16)&0xFF)) / 4;
            u32 G = (((a>>8) &0xFF)+((b>>8) &0xFF)+((c>>8) &0xFF)+((e>>8) &0xFF)) / 4;
            u32 Bl = ((a&0xFF)+(b&0xFF)+(c&0xFF)+(e&0xFF)) / 4;
            nxt[y * nw + x] = (A << 24) | (R << 16) | (G << 8) | Bl;
        }
        HeapFree(GetProcessHeap(), 0, cur); cur = nxt; lw = nw; lh = nh;
    }
    HeapFree(GetProcessHeap(), 0, cur);
    return tex;
}

// ---- procedural party dot : a solid white AA disc, drawn TINTED (COLOROP=MODULATE) as a marker ------
u32 make_dot(u32 dev) {                              // solid white disc with a smooth (AA) edge
    const int S = 32; u32 buf[32 * 32];
    const float cx = 16.0f, cy = 16.0f, R = 12.0f, fth = 1.5f;   // 4px transparent margin -> edge clip/wrap never touches the disc
    for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) {
        float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
        float d = sqrtf(dx * dx + dy * dy);
        float a = (R - d) / fth + 0.5f; if (a < 0) a = 0; if (a > 1) a = 1;   // 1 inside -> 0 just past R
        u32 av = (u32)(a * 255.0f + 0.5f);
        buf[y * S + x] = (av << 24) | 0x00FFFFFF;                            // white, alpha = coverage
    }
    return make_texture_argb_mip(dev, S, S, buf);
}

// ---- party marker icons (real PNGs from game-icons.net) -------------------------
// White silhouettes converted to 128x128 BGRA raw under assets/ (PowerShell pre-step,
// no PNG decoder in the plugin). Loaded into an A8R8G8B8 + mip texture; the party
// widget draws them TINTED (COLOROP=MODULATE with a diffuse colour). To change an
// icon: drop a new PNG, re-run the convert step -> assets/icon_*.raw.

void release_texture(u32 tex)
{
    if (!tex) return;
    auto fRelease = vmethod<long(__stdcall*)(u32)>(tex, 2);
    long left = 1;
    if (fRelease) __try { left = fRelease(tex); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    if (left == 0) tl_remove(tex);
}

void preload_texture(u32 tex)
{
    if (!tex) return;
    auto fPre = vmethod<void(__stdcall*)(u32)>(tex, 9);
    if (fPre) __try { fPre(tex); } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// ===== Gear-icon ROM-DAT fallback (faithful port of EquipViewer's icon_extractor, Rubenator 2021) =====
// The FFXI install folder lives in the registry under PlayOnline*<region>\InstallFolder, value "0001".
// Cached : resolve once. Returns "<ffxi>\ROM" or nullptr if no install is found.
static const char* s_romRegKey = 0;   // which SUBS[] entry answered (//aio geartrace)

static const char* ffxi_rom_dir()
{
    static char rom[300]; static int tried = 0;
    if (tried) return rom[0] ? rom : nullptr;
    tried = 1; rom[0] = 0;
    // Must match map_dat.cpp's ffxi_root() key list. They diverged: that one probes the explicit WOW6432Node
    // forms too, this one did not -- so a machine where only those resolve got zone maps but NO gear icons (or
    // the reverse), which reads as an unrelated bug on one tester's machine only.
    static const char* SUBS[] = {
        "SOFTWARE\\WOW6432Node\\PlayOnlineEU\\InstallFolder",
        "SOFTWARE\\WOW6432Node\\PlayOnlineUS\\InstallFolder",
        "SOFTWARE\\WOW6432Node\\PlayOnline\\InstallFolder",
        "SOFTWARE\\PlayOnlineEU\\InstallFolder",
        "SOFTWARE\\PlayOnlineUS\\InstallFolder",
        "SOFTWARE\\PlayOnline\\InstallFolder",
    };
    char base[260]; base[0] = 0;
    for (int i = 0; i < (int)(sizeof(SUBS) / sizeof(SUBS[0])) && !base[0]; ++i) {   // sizeof, not a literal : the list grew and the bound did not follow
        HKEY h;   // 32-bit plugin -> SOFTWARE already maps to Wow6432Node ; KEY_WOW64_32KEY is explicit + harmless.
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, SUBS[i], 0, KEY_READ | KEY_WOW64_32KEY, &h) != ERROR_SUCCESS) continue;
        DWORD type = 0, sz = sizeof(base) - 1;   // -1 : leave room to force-terminate below
        if (RegQueryValueExA(h, "0001", NULL, &type, (LPBYTE)base, &sz) != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) base[0] = 0;
        else { base[(sz < sizeof(base) - 1) ? sz : sizeof(base) - 1] = 0; s_romRegKey = SUBS[i]; }   // RegQueryValueExA does NOT guarantee a NUL
        RegCloseKey(h);
    }
    if (!base[0]) return nullptr;
    _snprintf(rom, sizeof(rom), "%s\\ROM", base); rom[sizeof(rom) - 1] = 0;
    return rom;
}

const char* ffxi_rom_dir_probe(const char** out_regkey)
{
    const char* r = ffxi_rom_dir();
    if (out_regkey) *out_regkey = s_romRegKey;
    return r;
}

// The decisions (id-range table, stride proof, record proof, icon decode) live in gear_dat.h, pure and covered by
// tests/t_geardat.cpp ; this is only the file I/O around them.
static bool gear_fread_at(void* ctx, long off, unsigned char* buf, long n) {
    FILE* fp = (FILE*)ctx;
    return fseek(fp, off, SEEK_SET) == 0 && fread(buf, 1, (size_t)n, fp) == (size_t)n;
}

bool decode_gear_icon_from_rom(unsigned id, u32* out_px, GearInfo* info)
{
    GearInfo scratch; if (!info) info = &scratch;
    info->step = GS_NO_RANGE; info->dat = 0; info->romdir = 0; info->regkey = 0; info->index = -1; info->err = 0; info->stride = 0; info->fileSize = 0;
    if (!out_px) return false;
    const GearDat* d = gear_dat_for(id);
    if (!d) return false;
    info->dat = d->dat;
    info->index = (long)id - gear_dat_first(*d);

    info->step = GS_NO_ROMDIR;
    const char* rom = ffxi_rom_dir_probe(&info->regkey);
    if (!rom) return false;
    info->romdir = rom;

    char path[340]; _snprintf(path, sizeof(path), "%s\\%s.DAT", rom, d->dat); path[sizeof(path) - 1] = 0;
    for (char* c = path; *c; ++c) if (*c == '/') *c = '\\';   // table paths use '/'

    info->step = GS_NO_DAT;
    FILE* fp = fopen(path, "rb");
    if (!fp) { info->err = errno; return false; }

    // RECORD SIZE and RECORD IDENTITY : proven, never assumed. The 2026-09-10 client patch grew every item record
    // from 0xC00 to 0x1400 bytes, and a hard-coded 0xC00 -- EquipViewer's, ported verbatim -- read the WRONG record
    // for every id and "succeeded" : blank icons (the tester's Quicksilver), other items' art, garbage, all drawn
    // AND cached. gear_find_stride keeps only a size whose record names this id and carries a 32x32 8-bpp header.
    info->step = GS_BAD_READ;
    long fsz = -1;
    if (fseek(fp, 0, SEEK_END) == 0) fsz = ftell(fp);
    if (fsz <= 0) { fclose(fp); return false; }
    info->fileSize = fsz;
    const long stride = gear_find_stride(fsz, *d, id, gear_fread_at, fp);
    info->stride = stride;
    if (!stride) { fclose(fp); info->step = GS_BAD_LAYOUT; return false; }   // no size proves this record : refuse, never guess

    unsigned char data[GEAR_ICON];
    const bool ok = gear_fread_at(fp, info->index * stride + GEAR_HEAD, data, GEAR_ICON);
    fclose(fp);
    if (!ok) return false;
    info->step = GS_OK;
    gear_decode_icon(data, out_px);   // top-down, 0xAARRGGBB : the orientation read_gear_icon_bmp returns too
    return true;
}

// The DECODED head of `id`'s record (its first `n` bytes, n <= GEAR_HEAD) -- the same DAT, stride proof and record
// proof as the icon above ; the text and flags are parsed by item_text.h (2026-11-17, the NPC answers' item card).
bool read_item_record_head(unsigned id, unsigned char* out, long n)
{
    if (!out || n <= 0 || n > GEAR_HEAD) return false;
    const GearDat* d = gear_dat_for(id); if (!d) return false;
    const char* rom = ffxi_rom_dir_probe(0); if (!rom) return false;
    char path[340]; _snprintf(path, sizeof(path), "%s\\%s.DAT", rom, d->dat); path[sizeof(path) - 1] = 0;
    for (char* c = path; *c; ++c) if (*c == '/') *c = '\\';
    FILE* fp = fopen(path, "rb"); if (!fp) return false;
    long fsz = -1; if (fseek(fp, 0, SEEK_END) == 0) fsz = ftell(fp);
    const long stride = fsz > 0 ? gear_find_stride(fsz, *d, id, gear_fread_at, fp) : 0;
    const bool ok = stride && gear_fread_at(fp, ((long)id - gear_dat_first(*d)) * stride, out, n);
    fclose(fp);
    if (!ok) return false;
    for (long i = 0; i < n; ++i) out[i] = gear_rotl3(out[i]);
    return true;
}

// Best-effort disk cache. The decode above no longer depends on this : if the folder is read-only (a Windower
// install under Program Files with a non-elevated token, Controlled Folder Access, an AV hold) the icon still
// renders, we just re-decode it next session. Written to a temp file then MoveFileEx'd over the target, and
// every write checked -- a half-written BMP used to poison the slot PERMANENTLY and on disk (the "file exists"
// gates then skipped both the re-decode and the give-up, so it re-parsed a corrupt file every frame forever).
bool write_gear_icon_bmp(const char* out_bmp_path, const u32* px, int* out_err)
{
    if (out_err) *out_err = 0;
    if (!out_bmp_path || !px) return false;
    // BMP V4 header : byte-identical to EquipViewer's output (== the bundled BMPs read_gear_icon_bmp reads).
    static const unsigned char HDR[122] = {
        'B','M', 0x7A,0x10,0,0, 0,0, 0,0, 0x7A,0,0,0,                                   // file size 0x107A, pixels @ 0x7A
        0x6C,0,0,0, 0x20,0,0,0, 0x20,0,0,0, 0x01,0, 0x20,0, 0x03,0,0,0, 0x00,0x10,0,0,   // V4, 32x32, 32bpp, BI_BITFIELDS, image 0x1000
        0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
        0x00,0x00,0xFF,0x00,  0x00,0xFF,0x00,0x00,  0xFF,0x00,0x00,0x00,  0x00,0x00,0x00,0xFF,
        's','R','G','B',
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0, 0,0,0,0, 0,0,0,0
    };
    // flip back to the BMP's bottom-up order (decode_gear_icon_from_rom handed us a top-down buffer).
    u32 rows[1024];
    for (int y = 0; y < 32; ++y) for (int x = 0; x < 32; ++x) rows[y * 32 + x] = px[(31 - y) * 32 + x];

    // PER-PROCESS temp name : two clients sharing this plugin folder can decode the same icon at once, and with a
    // shared "<target>.tmp" one client's failure path would DeleteFileA the other's finished file.
    char tmp[344]; _snprintf(tmp, sizeof(tmp), "%s.%lu.tmp", out_bmp_path, (unsigned long)GetCurrentProcessId()); tmp[sizeof(tmp) - 1] = 0;
    FILE* out = fopen(tmp, "wb");
    if (!out) { if (out_err) *out_err = errno; return false; }   // folder read-only -> skip the cache, nothing else
    const bool wrote = fwrite(HDR,  1, sizeof(HDR),  out) == sizeof(HDR)
                    && fwrite(rows, 1, sizeof(rows), out) == sizeof(rows);
    const bool closed = fclose(out) == 0;                        // buffered-write failures surface HERE, not at fwrite
    if (!wrote || !closed) { if (out_err) *out_err = errno; DeleteFileA(tmp); return false; }   // never leave a truncated BMP behind
    if (!MoveFileExA(tmp, out_bmp_path, MOVEFILE_REPLACE_EXISTING)) {
        if (out_err) *out_err = -(int)GetLastError();            // negative = Win32, positive = errno
        DeleteFileA(tmp); return false;
    }
    return true;
}

bool read_gear_icon_bmp(const char* bmp_path, u32* out_px)
{
    if (!bmp_path || !out_px) return false;
    HANDLE hf = CreateFileA(bmp_path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (hf == INVALID_HANDLE_VALUE) return false;
    unsigned char file[0x200 + 32 * 32 * 4];                     // header (0x7A in practice) + one 32x32 32bpp body
    DWORD got = 0; const BOOL ok = ReadFile(hf, file, sizeof(file), &got, 0); CloseHandle(hf);
    if (!ok || got < 54 || file[0] != 'B' || file[1] != 'M') return false;
    const u32 off = *(const u32*)(file + 10);
    const int W = *(const int*)(file + 18), Hs = *(const int*)(file + 22);
    const unsigned short bpp = *(const unsigned short*)(file + 28);
    if (W != 32 || (Hs != 32 && Hs != -32) || bpp != 32 || (unsigned long long)off + 32 * 32 * 4 > got) return false;
    const u32* src = (const u32*)(file + off);
    for (int y = 0; y < 32; ++y) for (int x = 0; x < 32; ++x)
        out_px[y * 32 + x] = src[(Hs < 0 ? y : 31 - y) * 32 + x];   // BMP rows are bottom-up unless the height is negative
    return true;
}

// ---- PNG through GDI+'s flat C API, bound at run time (no gdiplus.lib, no C++ headers) ----
namespace {
struct GpStartIn { unsigned version; void* debugCb; BOOL noBgThread; BOOL noExtCodecs; };
struct GpRect { int X, Y, Width, Height; };
struct GpData { unsigned Width, Height; int Stride; int PixelFormat; void* Scan0; UINT_PTR Reserved; };
typedef int (WINAPI *GpStartup)(ULONG_PTR*, const GpStartIn*, void*);
typedef int (WINAPI *GpFromFile)(const wchar_t*, void**);
typedef int (WINAPI *GpGetDim)(void*, unsigned*);
typedef int (WINAPI *GpLock)(void*, const GpRect*, unsigned, int, GpData*);
typedef int (WINAPI *GpUnlock)(void*, GpData*);
typedef int (WINAPI *GpDispose)(void*);
struct Gdip { bool ok; GpFromFile fromFile; GpGetDim width, height; GpLock lock; GpUnlock unlock; GpDispose dispose; };
const Gdip& gdip() {
    static Gdip g = { false, 0, 0, 0, 0, 0, 0 };
    static bool tried = false;   // rule10-ok: gdiplus.dll is a system DLL -- absent or broken it stays so for the session ; the miss is logged, and every caller falls back to its own icon
    if (tried) return g;         // rule10-ok: see above
    tried = true;                // rule10-ok: see above
    HMODULE m = LoadLibraryA("gdiplus.dll");
    GpStartup st = m ? (GpStartup)GetProcAddress(m, "GdiplusStartup") : 0;
    g.fromFile = m ? (GpFromFile)GetProcAddress(m, "GdipCreateBitmapFromFile") : 0;
    g.width    = m ? (GpGetDim)GetProcAddress(m, "GdipGetImageWidth") : 0;
    g.height   = m ? (GpGetDim)GetProcAddress(m, "GdipGetImageHeight") : 0;
    g.lock     = m ? (GpLock)GetProcAddress(m, "GdipBitmapLockBits") : 0;
    g.unlock   = m ? (GpUnlock)GetProcAddress(m, "GdipBitmapUnlockBits") : 0;
    g.dispose  = m ? (GpDispose)GetProcAddress(m, "GdipDisposeImage") : 0;
    ULONG_PTR token = 0; GpStartIn in = { 1, 0, FALSE, FALSE };
    const int rc = (st && g.fromFile && g.width && g.height && g.lock && g.unlock && g.dispose) ? st(&token, &in, 0) : -1;
    g.ok = rc == 0;   // started once, never shut down : the module stays loaded (a pinned system DLL), its thread is its own
    windower::debug::log("PNG : GDI+ %s (rc %d)", g.ok ? "ready" : "UNAVAILABLE -- the PNG icons fall back to the drawn ones", rc);
    return g;
}
} // namespace

bool load_png_argb(const wchar_t* path, int W, int H, u32* out) {
    if (!path || !out || W <= 0 || H <= 0) return false;
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) return false;   // the common miss : no GDI+ work for it
    const Gdip& g = gdip(); if (!g.ok) return false;
    void* bmp = 0;
    if (g.fromFile(path, &bmp) != 0 || !bmp) return false;
    unsigned w = 0, h = 0; bool ok = false;
    if (g.width(bmp, &w) == 0 && g.height(bmp, &h) == 0 && w > 0 && h > 0 && w <= 1024 && h <= 1024) {
        GpRect r = { 0, 0, (int)w, (int)h }; GpData d = { 0, 0, 0, 0, 0, 0 };
        if (g.lock(bmp, &r, 1 /*ImageLockModeRead*/, 0x26200A /*PixelFormat32bppARGB*/, &d) == 0 && d.Scan0) {
            for (int y = 0; y < H; ++y) {
                const u32* row = (const u32*)((const char*)d.Scan0 + (ptrdiff_t)((unsigned)y * h / (unsigned)H) * d.Stride);   // a stride can be negative
                for (int x = 0; x < W; ++x) out[y * W + x] = row[(unsigned)x * w / (unsigned)W];
            }
            g.unlock(bmp, &d); ok = true;
        }
    }
    g.dispose(bmp);
    return ok;
}

} // namespace aio
