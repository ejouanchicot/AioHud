// texture.cpp -- see texture.h.
#include "texture.h"
#include "noise.h"
#include "model/paths.h"   // plugin_path_r : runtime-derived asset paths (gfx infra exception to the layering rule)
#include <windows.h>
#include <math.h>
#include <stdio.h>   // gear-icon ROM fallback : fopen/fseek/fread + _snprintf
#include <errno.h>   // //aio geartrace : distinguish "read-only folder" (EACCES) from the other write failures

namespace aio {

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
    auto fLock = vmethod<long(__stdcall*)(u32,u32,void*,void*,u32)>(tex, 16);
    if (!fLock || fLock(tex, 0, lr, 0, 0) < 0 || !lr->pBits) { release_texture(tex); return 0; }   // lock failed -> don't hand back an uninitialised (garbage-pixel) texture
    return tex;
}

static void unlock(u32 tex)
{
    auto fUnlock = vmethod<long(__stdcall*)(u32,u32)>(tex, 17);
    if (fUnlock) fUnlock(tex, 0);
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

u32 make_nebula(u32 dev, int W, int H)
{
    LR lr; u32 tex = create_locked(dev, W, H, &lr);
    if (tex && lr.pBits) {
        // cosmic palette : deep blue -> violet -> teal -> magenta (clouds tint drifts across the field)
        static const float P[4][3] = { { 40, 64, 150 }, { 86, 54, 150 }, { 40, 120, 150 }, { 140, 58, 128 } };
        for (int y = 0; y < H; y++) {
            u32* row = (u32*)((char*)lr.pBits + y * lr.Pitch);
            for (int x = 0; x < W; x++) {
                const float u = (float)x / W, v = (float)y / H;
                // domain-warp the sampling coords -> wispy, organic clouds. SEAMLESS: every u/v frequency
                // multiplier is an INTEGER (fbm has period 1), and the warp is added onto an integer base,
                // so the texture wraps perfectly at the edges (no visible tile seam).
                const float wx = fbm(2.0f * u, 2.0f * v + 0.30f) - 0.5f;
                const float wy = fbm(2.0f * u + 1.7f, 2.0f * v) - 0.5f;
                const float d1 = fbm(3.0f * u + 0.7f * wx, 3.0f * v + 0.7f * wy);   // broad clouds
                const float d2 = fbm(6.0f * u, 6.0f * v);                           // finer detail
                float dens = d1 * 0.72f + d2 * 0.28f;
                dens = (dens - 0.44f) / 0.40f; if (dens < 0.0f) dens = 0.0f; if (dens > 1.0f) dens = 1.0f;
                dens = dens * dens;                                                 // soft falloff -> dark voids between clouds
                float h = fbm(2.0f * u + 3.1f, 2.0f * v + 1.3f) * 3.0f;             // hue position 0..3
                if (h < 0.0f) h = 0.0f; if (h > 2.999f) h = 2.999f;
                const int i0 = (int)h; const float ft = h - i0;
                const float cr = P[i0][0] + (P[i0 + 1][0] - P[i0][0]) * ft;
                const float cg = P[i0][1] + (P[i0 + 1][1] - P[i0][1]) * ft;
                const float cb = P[i0][2] + (P[i0 + 1][2] - P[i0][2]) * ft;
                float R = 6.0f + cr * dens, G = 9.0f + cg * dens, B = 18.0f + cb * dens;   // dark space base + clouds
                if (R > 255.0f) R = 255.0f; if (G > 255.0f) G = 255.0f; if (B > 255.0f) B = 255.0f;
                row[x] = 0xFF000000 | ((u32)R << 16) | ((u32)G << 8) | (u32)B;
            }
        }
        unlock(tex);
    }
    return tex;
}

// ---- MATERIAL textures for the procedural box themes (like the nebula : real procedural texture, not
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

// Load a 32-bpp BMP (BITMAPINFOHEADER / V4 / V5, BI_RGB or BI_BITFIELDS with the standard ARGB masks --
// the FFXI gearicon format) into a mipped texture. Each file pixel u32 is already 0xAARRGGBB, so it maps
// straight onto our A8R8G8B8 surface ; BMP rows are bottom-up (unless height<0) so we flip. Returns 0 on
// any IO/parse failure (a missing gearicon just draws no icon). W/H are read from the header.
u32 load_bmp_texture(u32 dev, const char* path)
{
    HANDLE hf = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (hf == INVALID_HANDLE_VALUE) return 0;
    DWORD fsz = GetFileSize(hf, 0);
    if (fsz < 54 || fsz > (1u << 20)) { CloseHandle(hf); return 0; }     // 54 = min BMP ; cap at 1 MB
    char* file = (char*)HeapAlloc(GetProcessHeap(), 0, fsz);
    if (!file) { CloseHandle(hf); return 0; }
    DWORD got = 0; BOOL ok = ReadFile(hf, file, fsz, &got, 0); CloseHandle(hf);
    if (!ok || got != fsz || file[0] != 'B' || file[1] != 'M') { HeapFree(GetProcessHeap(), 0, file); return 0; }
    const u32 off = *(const u32*)(file + 10);                            // bfOffBits : pixel data start
    const int W = *(const int*)(file + 18);
    int H = *(const int*)(file + 22);
    const unsigned short bpp = *(const unsigned short*)(file + 28);
    const bool topDown = H < 0; if (topDown) H = -H;
    if (bpp != 32 || W <= 0 || H <= 0 || W > 256 || H > 256 || (unsigned long long)off + (unsigned long long)W * H * 4 > fsz) {
        HeapFree(GetProcessHeap(), 0, file); return 0;
    }
    u32* buf = (u32*)HeapAlloc(GetProcessHeap(), 0, (DWORD)(W * H * 4));
    if (!buf) { HeapFree(GetProcessHeap(), 0, file); return 0; }
    const u32* src = (const u32*)(file + off);
    for (int y = 0; y < H; ++y) {
        const u32* srow = src + (topDown ? y : (H - 1 - y)) * W;         // flip : BMP is bottom-up
        u32* drow = buf + y * W;
        for (int x = 0; x < W; ++x) drow[x] = srow[x];
    }
    u32 tex = make_texture_argb_mip(dev, W, H, buf);
    HeapFree(GetProcessHeap(), 0, buf);
    HeapFree(GetProcessHeap(), 0, file);
    return tex;
}

u32 make_texture_argb(u32 dev, int W, int H, const u32* pixels)
{
    LR lr; u32 tex = create_locked(dev, W, H, &lr);
    if (tex && lr.pBits) {
        for (int y = 0; y < H; y++) {
            u32* dpix = (u32*)((char*)lr.pBits + y * lr.Pitch);
            const u32* spix = pixels + y * W;
            for (int x = 0; x < W; x++) dpix[x] = spix[x];
        }
        unlock(tex);
    }
    return tex;
}

static int mip_count(int w, int h) {
    int n = 1; while (w > 1 || h > 1) { w >>= 1; if (!w) w = 1; h >>= 1; if (!h) h = 1; n++; } return n;
}

u32 make_texture_argb_mip(u32 dev, int W, int H, const u32* pixels)
{
    int levels = mip_count(W, H);
    auto fCreate = vmethod<long(__stdcall*)(u32,u32,u32,u32,u32,u32,u32,u32*)>(dev, 20);
    u32 tex = 0;
    if (!fCreate || fCreate(dev, W, H, levels, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex) < 0 || !valid_ptr(tex))
        return 0;
    auto fLock   = vmethod<long(__stdcall*)(u32,u32,void*,void*,u32)>(tex, 16);
    auto fUnlock = vmethod<long(__stdcall*)(u32,u32)>(tex, 17);

    u32* cur = (u32*)HeapAlloc(GetProcessHeap(), 0, W * H * 4);
    if (!cur) { release_texture(tex); return 0; }   // fail CLOSED (else a live but never-filled texture = garbage pixels)
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
        if (!nxt) { HeapFree(GetProcessHeap(), 0, cur); release_texture(tex); return 0; }   // fail CLOSED like the initial alloc : a partially-filled texture would leave garbage in the upper (unlocked) MANAGED mips
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
static u32 load_icon_raw(u32 dev, const char* path) {
    const int N = 128;
    HANDLE hf = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (hf == INVALID_HANDLE_VALUE) return 0;
    DWORD need = (DWORD)(N * N * 4), got = 0;
    u32* buf = (u32*)HeapAlloc(GetProcessHeap(), 0, need);
    if (!buf) { CloseHandle(hf); return 0; }
    BOOL ok = ReadFile(hf, buf, need, &got, 0);
    CloseHandle(hf);
    u32 tex = (ok && got == need) ? make_texture_argb_mip(dev, N, N, buf) : 0;
    HeapFree(GetProcessHeap(), 0, buf);
    return tex;
}
u32 make_icon_party_lead(u32 dev)    { return load_icon_raw(dev, plugin_path_r("assets\\icon_crown.raw")); }
u32 make_icon_alliance_lead(u32 dev) { return load_icon_raw(dev, plugin_path_r("assets\\icon_laurel.raw")); }
u32 make_icon_qm(u32 dev)            { return load_icon_raw(dev, plugin_path_r("assets\\icon_chest.raw")); }

void release_texture(u32 tex)
{
    if (!tex) return;
    auto fRelease = vmethod<long(__stdcall*)(u32)>(tex, 2);
    if (fRelease) __try { fRelease(tex); } __except (EXCEPTION_EXECUTE_HANDLER) {}
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
    static const char* SUBS[] = {
        "SOFTWARE\\PlayOnlineUS\\InstallFolder",
        "SOFTWARE\\PlayOnline\\InstallFolder",
        "SOFTWARE\\PlayOnlineEU\\InstallFolder",
    };
    char base[260]; base[0] = 0;
    for (int i = 0; i < 3 && !base[0]; ++i) {
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

// id-range -> (ROM DAT relative path, id offset). Straight from EquipViewer's item_dat_map.
struct GearDat { unsigned lo, hi; const char* dat; int off; };
static const GearDat GEAR_DAT[] = {
    { 0x0001, 0x0FFF, "118/106", -1 },   // General Items
    { 0x1000, 0x1FFF, "118/107",  0 },   // Usable Items
    { 0x2000, 0x21FF, "118/110",  0 },   // Automaton Items
    { 0x2200, 0x27FF, "301/115",  0 },   // General Items 2
    { 0x2800, 0x3FFF, "118/109",  0 },   // Armor Items
    { 0x4000, 0x59FF, "118/108",  0 },   // Weapon Items
    { 0x5A00, 0x6FFF, "286/73",   0 },   // Armor Items 2
    { 0x7000, 0x73FF, "217/21",   0 },   // Maze / Basic Items
    { 0x7400, 0x77FF, "288/80",   0 },   // Instinct Items
    { 0xF000, 0xF1FF, "288/67",   0 },   // Monipulator Items
    { 0xFFFF, 0xFFFF, "174/48",   0 },   // Gil
};

// FFXI's palette bytes are bit-rotated : the decoded value is a rotate-left-by-3 of the encoded byte.
static inline unsigned char rotl3(unsigned char x) { return (unsigned char)(((x & 0x1F) << 3) | (x >> 5)); }

bool decode_gear_icon_from_rom(unsigned id, u32* out_px, GearInfo* info)
{
    GearInfo scratch; if (!info) info = &scratch;
    info->step = GS_NO_RANGE; info->dat = 0; info->romdir = 0; info->regkey = 0; info->index = -1; info->err = 0;
    if (!out_px) return false;
    const GearDat* d = nullptr;
    for (int i = 0; i < (int)(sizeof(GEAR_DAT) / sizeof(GEAR_DAT[0])); ++i)
        if (id >= GEAR_DAT[i].lo && id <= GEAR_DAT[i].hi) { d = &GEAR_DAT[i]; break; }
    if (!d) return false;
    info->dat = d->dat;
    const long idOff = (long)d->lo + d->off;
    info->index = (long)id - idOff;

    info->step = GS_NO_ROMDIR;
    const char* rom = ffxi_rom_dir_probe(&info->regkey);
    if (!rom) return false;
    info->romdir = rom;

    char path[340]; _snprintf(path, sizeof(path), "%s\\%s.DAT", rom, d->dat); path[sizeof(path) - 1] = 0;
    for (char* c = path; *c; ++c) if (*c == '/') *c = '\\';   // GEAR_DAT paths use '/'

    info->step = GS_NO_DAT;
    FILE* fp = fopen(path, "rb");
    if (!fp) { info->err = errno; return false; }
    unsigned char data[0x800];
    bool ok = (fseek(fp, info->index * 0xC00 + 0x2BD, SEEK_SET) == 0) && (fread(data, 1, sizeof(data), fp) == sizeof(data));
    fclose(fp);
    info->step = GS_BAD_READ;
    if (!ok) return false;
    info->step = GS_OK;

    // palette : 256 BGRA entries, each byte rotl3-decoded ; the alpha byte is additionally doubled + clamped.
    unsigned char pal[256][4];
    for (int g = 0; g < 256; ++g) {
        pal[g][0] = rotl3(data[g * 4 + 0]);   // B
        pal[g][1] = rotl3(data[g * 4 + 1]);   // G
        pal[g][2] = rotl3(data[g * 4 + 2]);   // R
        int a = rotl3(data[g * 4 + 3]) * 2; pal[g][3] = (unsigned char)(a < 256 ? a : 255);   // A (doubled)
    }
    // 32x32 pixel indices : each encoded byte e maps to palette entry rotl3(e). The palette is BGRA and the
    // DAT rows are BOTTOM-UP, so pack to 0xAARRGGBB and flip vertically -- that lands the caller's buffer in
    // exactly the orientation load_bmp_texture used to hand back after its own bottom-up flip (the icons must
    // look identical whether they came from the bundled BMP or straight from the DAT).
    for (int y = 0; y < 32; ++y) for (int x = 0; x < 32; ++x) {
        const unsigned char* c = pal[rotl3(data[0x400 + (31 - y) * 32 + x])];
        out_px[y * 32 + x] = ((u32)c[3] << 24) | ((u32)c[2] << 16) | ((u32)c[1] << 8) | (u32)c[0];
    }
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
    // BMP V4 header : byte-identical to EquipViewer's output (== the bundled BMPs load_bmp_texture already reads).
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

    char tmp[344]; _snprintf(tmp, sizeof(tmp), "%s.tmp", out_bmp_path); tmp[sizeof(tmp) - 1] = 0;
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

} // namespace aio
