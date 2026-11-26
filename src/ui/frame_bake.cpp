// frame_bake.cpp -- the procedural iron plate (frame_bake.h). A port of the validated prototype (scratchpad proc_iron.py) :
//   metal = union(boxes, convex corners rounded) - cavities + islands - their holes
//   m     = each metal sample's distance to the nearest cut     -> a convex chamfer, then a flat polished face
//   the chamfer never wider than the local bar half-width        -> thin walls stay round, no ridge on the medial axis
//   shading : the face reflects a few soft diagonal lights ; the chamfers a chrome environment (continuous : no notch at a
//   T) ; brushed grain + scratches ALONG each bar ; a sharp specular ; dark inner contour, faint light outer rim.
// Supersampled 2x (1x when the group is large), averaged down. Everything is relative to the group's origin, so the grain
// moves WITH the boxes. Scratch memory : HeapAlloc'd per bake, freed at once (a bake happens on a layout change only).
#include "ui/frame_bake.h"
#include <windows.h>
#include <cmath>
#include <cstring>

namespace aio {

namespace {
const float BIG = 1e20f;

// ---- the exact Euclidean distance transform (Felzenszwalb & Huttenlocher), squared, along one line, in double
void dt1(const double* f, double* d, int n, int* v, double* z) {
    int k = 0; v[0] = 0; z[0] = -1e300; z[1] = 1e300;
    for (int q = 1; q < n; ++q) {
        double s = ((f[q] + (double)q * q) - (f[v[k]] + (double)v[k] * v[k])) / (2.0 * (q - v[k]));
        while (s <= z[k]) { --k; s = ((f[q] + (double)q * q) - (f[v[k]] + (double)v[k] * v[k])) / (2.0 * (q - v[k])); }
        ++k; v[k] = q; z[k] = s; z[k + 1] = 1e300;
    }
    k = 0;
    for (int q = 0; q < n; ++q) { while (z[k + 1] < (double)q) ++k; d[q] = (double)(q - v[k]) * (q - v[k]) + f[v[k]]; }
}
// out[i] = the distance (in samples) from a sample where in[i] != 0 to the nearest sample where it is 0 ; 0 elsewhere
struct Scratch { double *f, *d, *z; int* v; };
void edt(const unsigned char* in, float* out, int W, int H, const Scratch& s) {
    for (int i = 0; i < W * H; ++i) out[i] = in[i] ? BIG : 0.0f;
    for (int x = 0; x < W; ++x) {
        for (int y = 0; y < H; ++y) s.f[y] = out[y * W + x];
        dt1(s.f, s.d, H, s.v, s.z);
        for (int y = 0; y < H; ++y) out[y * W + x] = (float)s.d[y];
    }
    for (int y = 0; y < H; ++y) {
        float* r = out + y * W;
        for (int x = 0; x < W; ++x) s.f[x] = r[x];
        dt1(s.f, s.d, W, s.v, s.z);
        for (int x = 0; x < W; ++x) r[x] = (float)sqrt(s.d[x]);
    }
}
// box blur along x / y (edges clamped) ; three of them ~ a gaussian
void boxh(float* a, int W, int H, int r, float* line) {
    const float inv = 1.0f / (float)(2 * r + 1);
    for (int y = 0; y < H; ++y) {
        float* p = a + y * W; memcpy(line, p, sizeof(float) * (size_t)W);
        float acc = 0.0f;
        for (int i = -r; i <= r; ++i) acc += line[i < 0 ? 0 : (i >= W ? W - 1 : i)];
        for (int x = 0; x < W; ++x) {
            p[x] = acc * inv;
            const int o = x - r, n = x + r + 1;
            acc += line[n >= W ? W - 1 : n] - line[o < 0 ? 0 : o];
        }
    }
}
void boxv(float* a, int W, int H, int r, float* line) {
    const float inv = 1.0f / (float)(2 * r + 1);
    for (int x = 0; x < W; ++x) {
        for (int y = 0; y < H; ++y) line[y] = a[y * W + x];
        float acc = 0.0f;
        for (int i = -r; i <= r; ++i) acc += line[i < 0 ? 0 : (i >= H ? H - 1 : i)];
        for (int y = 0; y < H; ++y) {
            a[y * W + x] = acc * inv;
            const int o = y - r, n = y + r + 1;
            acc += line[n >= H ? H - 1 : n] - line[o < 0 ? 0 : o];
        }
    }
}
void gauss(float* a, int W, int H, float sigma, float* line) {
    if (sigma < 0.5f) return;
    const int passes = sigma > 1.2f ? 3 : 1;
    int r = (int)((sqrtf(12.0f * sigma * sigma / (float)passes + 1.0f) - 1.0f) * 0.5f + 0.5f); if (r < 1) r = 1;
    for (int i = 0; i < passes; ++i) { boxh(a, W, H, r, line); boxv(a, W, H, r, line); }
}
// a rounded rect (1x px, r = corner radius) into the sample mask : val at every sample centre inside it
void rrect_mask(unsigned char* m, int W, int H, int S, float x, float y, float w, float h, float r, unsigned char val, const unsigned char* within) {
    if (w <= 0.0f || h <= 0.0f) return;
    if (r > w * 0.5f) r = w * 0.5f; if (r > h * 0.5f) r = h * 0.5f;
    int sx0 = (int)floorf(x * (float)S), sy0 = (int)floorf(y * (float)S), sx1 = (int)ceilf((x + w) * (float)S), sy1 = (int)ceilf((y + h) * (float)S);
    if (sx0 < 0) sx0 = 0; if (sy0 < 0) sy0 = 0; if (sx1 > W) sx1 = W; if (sy1 > H) sy1 = H;
    for (int sy = sy0; sy < sy1; ++sy) {
        const float py = ((float)sy + 0.5f) / (float)S;
        if (py < y || py > y + h) continue;
        const float dy = py < y + r ? y + r - py : (py > y + h - r ? py - (y + h - r) : 0.0f);
        for (int sx = sx0; sx < sx1; ++sx) {
            const float px = ((float)sx + 0.5f) / (float)S;
            if (px < x || px > x + w) continue;
            const float dx = px < x + r ? x + r - px : (px > x + w - r ? px - (x + w - r) : 0.0f);
            if (dx * dx + dy * dy <= r * r && (!within || within[sy * W + sx])) m[sy * W + sx] = val;
        }
    }
}

// ---- the grain : 256x256 wrapping noise tiles, made once. Streaks (long along x), scratches (longer, sparser), fine
enum { NT = 256 };
float g_brA[NT * NT], g_brB[NT * NT], g_scA[NT * NT], g_scB[NT * NT], g_fine[NT * NT], g_ham[NT * NT];
bool g_noiseOk = false;
unsigned g_rs = 0x9E3779B9u;
float frand() { g_rs ^= g_rs << 13; g_rs ^= g_rs >> 17; g_rs ^= g_rs << 5; return (float)(g_rs & 0xFFFFFF) / 16777216.0f; }
float grand() { return (frand() + frand() + frand() + frand() - 2.0f) * 1.7320508f; }
void wrap_blur_x(float* t, float sigma) {          // three wrapping box passes along x
    int r = (int)((sqrtf(4.0f * sigma * sigma + 1.0f) - 1.0f) * 0.5f + 0.5f); if (r < 1) r = 1;
    float line[NT];
    for (int pass = 0; pass < 3; ++pass) for (int y = 0; y < NT; ++y) {
        float* p = t + y * NT; memcpy(line, p, sizeof(line));
        float acc = 0.0f; for (int i = -r; i <= r; ++i) acc += line[(i + NT) & (NT - 1)];
        for (int x = 0; x < NT; ++x) { p[x] = acc / (float)(2 * r + 1); acc += line[(x + r + 1) & (NT - 1)] - line[(x - r + NT) & (NT - 1)]; }
    }
}
void normalize(float* t) {
    double s = 0.0, s2 = 0.0; for (int i = 0; i < NT * NT; ++i) { s += t[i]; s2 += (double)t[i] * t[i]; }
    const double mean = s / (NT * NT), sd = sqrt(s2 / (NT * NT) - mean * mean) + 1e-9;
    for (int i = 0; i < NT * NT; ++i) t[i] = (float)((t[i] - mean) / sd);
}
void make_noise() {
    if (g_noiseOk) return;
    float* T[4] = { g_brA, g_brB, g_scA, g_scB };
    for (int k = 0; k < 4; ++k) { for (int i = 0; i < NT * NT; ++i) T[k][i] = grand(); wrap_blur_x(T[k], k < 2 ? 25.0f : 60.0f); normalize(T[k]); }
    for (int i = 0; i < NT * NT; ++i) g_fine[i] = grand();
    normalize(g_fine);
    {   // the hammer's dents : white noise blurred both ways (x, transpose, x, transpose back)
        static float t[NT * NT];
        for (int i = 0; i < NT * NT; ++i) g_ham[i] = grand();
        wrap_blur_x(g_ham, 1.3f);
        for (int y = 0; y < NT; ++y) for (int x = 0; x < NT; ++x) t[x * NT + y] = g_ham[y * NT + x];
        wrap_blur_x(t, 1.3f);
        for (int y = 0; y < NT; ++y) for (int x = 0; x < NT; ++x) g_ham[y * NT + x] = t[x * NT + y];
        normalize(g_ham); }
    g_noiseOk = true;
}
inline float smooth01(float t) { t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t); return t * t * (3.0f - 2.0f * t); }
inline float clamp01(float t) { return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t); }

void* halloc(size_t n) { return HeapAlloc(GetProcessHeap(), 0, n); }
void hfree(void* p) { if (p) HeapFree(GetProcessHeap(), 0, p); }
}   // namespace

int g_bakePhaseMs[8];
namespace {
// van Herk / Gil-Werman running max (window 2r+1, outside = 0) along one line : O(n) whatever r
void vhgw(const float* in, float* out, int n, int r, float* g, float* h) {
    const int k = 2 * r + 1, m = n + 2 * r;
    for (int i = 0; i < m; ++i) { const int s = i - r; const float v = (s < 0 || s >= n) ? 0.0f : in[s];
        g[i] = (i % k == 0) ? v : (g[i - 1] > v ? g[i - 1] : v); }
    for (int i = m - 1; i >= 0; --i) { const int s = i - r; const float v = (s < 0 || s >= n) ? 0.0f : in[s];
        h[i] = (i % k == k - 1 || i == m - 1) ? v : (h[i + 1] > v ? h[i + 1] : v); }
    for (int x = 0; x < n; ++x) { const float a = h[x], b = g[x + k - 1]; out[x] = a > b ? a : b; }
}
void maxf2(float* a, int W, int H, int r, float* l0, float* l1, float* g, float* h) {
    for (int y = 0; y < H; ++y) { memcpy(l0, a + y * W, sizeof(float) * (size_t)W); vhgw(l0, a + y * W, W, r, g, h); }
    for (int x = 0; x < W; ++x) { for (int y = 0; y < H; ++y) l0[y] = a[y * W + x]; vhgw(l0, l1, H, r, g, h); for (int y = 0; y < H; ++y) a[y * W + x] = l1[y]; }
}
inline float bil(const float* p, int W, int H, float x, float y) {   // bilinear, in that plane's pixel units (centres at +0.5)
    x -= 0.5f; y -= 0.5f; if (x < 0.0f) x = 0.0f; if (y < 0.0f) y = 0.0f;
    int x0 = (int)x, y0 = (int)y; if (x0 > W - 2) x0 = W - 2; if (y0 > H - 2) y0 = H - 2; if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0;
    float fx = x - (float)x0, fy = y - (float)y0; if (fx > 1.0f) fx = 1.0f; if (fy > 1.0f) fy = 1.0f;
    const float* q = p + y0 * W + x0;
    return (q[0] * (1.0f - fx) + q[1] * fx) * (1.0f - fy) + (q[W] * (1.0f - fx) + q[W + 1] * fx) * fy;
}
}   // namespace

bool bake_iron(const BakeShape& s, u32* out, int* outSuper) {
    LARGE_INTEGER qf, q0, q1; QueryPerformanceFrequency(&qf); QueryPerformanceCounter(&q0); int lastPh = -1;
    auto PH = [&](int ph) { QueryPerformanceCounter(&q1); if (lastPh >= 0) g_bakePhaseMs[lastPh] = (int)((q1.QuadPart - q0.QuadPart) * 1000 / qf.QuadPart); q0 = q1; lastPh = ph; };
    if (s.W <= 0 || s.H <= 0 || !out) return false;
    make_noise();
    const int S = s.W * s.H <= 700000 ? 2 : 1;
    if (outSuper) *outSuper = S;
    // a 2 px margin all round : the distance transform must see 'outside' past the group's own edges
    const int P = 2, W1 = s.W + 2 * P, H1 = s.H + 2 * P, W = W1 * S, H = H1 * S, N = W * H, N1 = W1 * H1;
    const int L = (W > H ? W : H) + 64;
    const float o = (float)P;
    // full-resolution planes : the masks, m, the height ; the slow fields (half-width, direction, shadow) at 1x
    unsigned char* uni = (unsigned char*)halloc((size_t)N);
    unsigned char* met = (unsigned char*)halloc((size_t)N);
    float* A = (float*)halloc(sizeof(float) * (size_t)N);    // m : the distance to the nearest cut (px)
    float* B = (float*)halloc(sizeof(float) * (size_t)N);    // the height
    float* HF = (float*)halloc(sizeof(float) * (size_t)N1);  // 1x : the local half-width
    float* CD = (float*)halloc(sizeof(float) * (size_t)N1);  // 1x : m blurred -> the bar's direction
    float* SH = (float*)halloc(sizeof(float) * (size_t)N1);  // 1x : the cavities' shadow
    float* l0 = (float*)halloc(sizeof(float) * (size_t)L); float* l1 = (float*)halloc(sizeof(float) * (size_t)L);
    float* lg = (float*)halloc(sizeof(float) * (size_t)L); float* lh = (float*)halloc(sizeof(float) * (size_t)L);
    Scratch sc = { (double*)halloc(sizeof(double) * (size_t)L), (double*)halloc(sizeof(double) * (size_t)L), (double*)halloc(sizeof(double) * (size_t)(L + 1)), (int*)halloc(sizeof(int) * (size_t)L) };
    const bool ok = uni && met && A && B && HF && CD && SH && l0 && l1 && lg && lh && sc.f && sc.d && sc.z && sc.v;
    if (ok) {
        const float kb = s.kb > 0.1f ? s.kb : 1.0f, b = s.b, fS = (float)S;
        const float R_OUT = 6.0f * kb, R_CAV = 4.0f * kb, BEV = 3.6f * kb;
        auto covered = [&](float px, float py) {
            for (int j = 0; j < s.nb; ++j) if (px >= s.box[j][0] && px < s.box[j][0] + s.box[j][2] && py >= s.box[j][1] && py < s.box[j][1] + s.box[j][3]) return true;
            return false; };
        PH(0);
        // 1. the union of the boxes ; each CONVEX corner (nothing just outside it, either way or diagonally) rounded by R_OUT
        memset(uni, 0, (size_t)N);
        for (int i = 0; i < s.nb; ++i) rrect_mask(uni, W, H, S, o + s.box[i][0], o + s.box[i][1], s.box[i][2], s.box[i][3], 0.0f, 1, 0);
        for (int i = 0; i < s.nb; ++i) for (int c = 0; c < 4; ++c) {
            const float sx = (c & 1) ? 1.0f : -1.0f, sy = (c & 2) ? 1.0f : -1.0f;
            const float cx = (c & 1) ? s.box[i][0] + s.box[i][2] : s.box[i][0], cy = (c & 2) ? s.box[i][1] + s.box[i][3] : s.box[i][1];
            if (covered(cx + sx * 0.5f, cy - sy * 0.5f) || covered(cx - sx * 0.5f, cy + sy * 0.5f) || covered(cx + sx * 0.5f, cy + sy * 0.5f)) continue;
            const float ccx = cx - sx * R_OUT, ccy = cy - sy * R_OUT;
            const int x0 = (int)floorf((o + (sx > 0 ? ccx : cx)) * fS), x1 = (int)ceilf((o + (sx > 0 ? cx : ccx)) * fS);
            const int y0 = (int)floorf((o + (sy > 0 ? ccy : cy)) * fS), y1 = (int)ceilf((o + (sy > 0 ? cy : ccy)) * fS);
            for (int y = y0 < 0 ? 0 : y0; y < y1 && y < H; ++y) for (int x = x0 < 0 ? 0 : x0; x < x1 && x < W; ++x) {
                const float px = ((float)x + 0.5f) / fS - o, py = ((float)y + 0.5f) / fS - o;
                const float dx = px - ccx, dy = py - ccy;
                if (dx * sx <= 0.0f || dy * sy <= 0.0f) continue;
                if (s.style >= 2 ? (dx * sx + dy * sy <= R_OUT * 1.2f) : (dx * dx + dy * dy <= R_OUT * R_OUT)) continue;
                bool other = false;                                  // another box covering it keeps it
                for (int j = 0; j < s.nb && !other; ++j) if (j != i && px >= s.box[j][0] && px < s.box[j][0] + s.box[j][2] && py >= s.box[j][1] && py < s.box[j][1] + s.box[j][3]) other = true;
                if (!other) uni[y * W + x] = 0;
            }
        }
        PH(1);
        // 2. the metal : the union, minus each box's cavity, plus the islands, minus their holes
        memcpy(met, uni, (size_t)N);
        for (int i = 0; i < s.nb; ++i) rrect_mask(met, W, H, S, o + s.box[i][0] + b, o + s.box[i][1] + b, s.box[i][2] - 2.0f * b, s.box[i][3] - 2.0f * b, R_CAV, 0, 0);
        for (int i = 0; i < s.np; ++i) {
            // an island close to its box's border is WELDED to it : no thin sliver of cavity between the two (a seam on a seam)
            float px0 = s.plate[i][0], py0 = s.plate[i][1], px1 = px0 + s.plate[i][2], py1 = py0 + s.plate[i][3];
            const float cxm = (px0 + px1) * 0.5f, cym = (py0 + py1) * 0.5f, weld = 1.5f * b;
            for (int j = 0; j < s.nb; ++j) {
                const float bx0 = s.box[j][0], by0 = s.box[j][1], bx1 = bx0 + s.box[j][2], by1 = by0 + s.box[j][3];
                if (cxm < bx0 || cxm > bx1 || cym < by0 || cym > by1) continue;
                if (px0 - (bx0 + b) < weld) px0 = bx0; if ((bx1 - b) - px1 < weld) px1 = bx1;
                if (py0 - (by0 + b) < weld) py0 = by0; if ((by1 - b) - py1 < weld) py1 = by1;
                break;
            }
            rrect_mask(met, W, H, S, o + px0, o + py0, px1 - px0, py1 - py0, s.plate[i][4], 1, uni);
        }
        for (int i = 0; i < s.nc; ++i) rrect_mask(met, W, H, S, o + s.cut[i][0], o + s.cut[i][1], s.cut[i][2], s.cut[i][3], s.cut[i][4], 0, 0);
        PH(2);
        // 3. m : each metal sample's distance to the nearest cut
        edt(met, A, W, H, sc); for (int i = 0; i < N; ++i) A[i] = met[i] ? (A[i] - 0.5f) / fS : 0.0f;
        PH(3);
        // 4. the slow fields at 1x : the half-width (block max, running max, blur), the direction (block mean, blur), the shadow
        {   const int sh = (int)(2.0f * kb + 0.5f);
            for (int Y = 0; Y < H1; ++Y) for (int X = 0; X < W1; ++X) {
                float mx = 0.0f, mean = 0.0f;
                for (int j = 0; j < S; ++j) for (int i = 0; i < S; ++i) { const float v = A[(Y * S + j) * W + X * S + i]; if (v > mx) mx = v; mean += v; }
                HF[Y * W1 + X] = mx; CD[Y * W1 + X] = mean / (float)(S * S);
                const int ox = X - sh, oy = Y - sh; float m = 0.0f;
                if (ox >= 0 && oy >= 0) for (int j = 0; j < S; ++j) for (int i = 0; i < S; ++i) m += met[(oy * S + j) * W + ox * S + i] ? 1.0f : 0.0f;
                SH[Y * W1 + X] = m / (float)(S * S);
            }
            maxf2(HF, W1, H1, (int)(3.0f * kb + 0.5f), l0, l1, lg, lh);
            gauss(HF, W1, H1, 1.5f * kb, l0);
            gauss(CD, W1, H1, 1.0f * kb, l0);
            gauss(SH, W1, H1, 2.5f * kb, l0);
        }
        // the height : a convex chamfer no wider than the local half-width, a flat face
        for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
            const int i = y * W + x;
            if (!met[i]) { B[i] = 0.0f; continue; }
            const float half = bil(HF, W1, H1, ((float)x + 0.5f) / fS, ((float)y + 0.5f) / fS);
            float bev = half < BEV ? half : BEV; if (bev < 0.8f * kb) bev = 0.8f * kb;
            const float t = clamp01(A[i] / bev), u = 1.0f - t;
            B[i] = (1.0f - u * u * sqrtf(sqrtf(u))) * sqrtf(bev / BEV < 1.0f ? bev / BEV : 1.0f);   // ~ 1 - (1-t)^2.25
            if (s.style == 3) {
                const float bv = (half < 4.0f * kb ? half : 4.0f * kb) > 0.8f * kb ? (half < 4.0f * kb ? half : 4.0f * kb) : 0.8f * kb;
                const float tw = clamp01(A[i] / bv), uw = 1.0f - tw;
                const float dome = half > 0.5f ? clamp01(A[i] / half) : 1.0f;
                const int hx_ = ((int)(((float)x + 0.5f) / fS - o + s.ox) + 4096) & (NT - 1), hy_ = ((int)(((float)y + 0.5f) / fS - o + s.oy) + 4096) & (NT - 1);
                B[i] = 0.70f * (1.0f - uw * uw) + 0.12f * sqrtf(dome) + 0.014f * g_ham[hy_ * NT + hx_] * tw;
            }
            if (s.style == 2) {
                const float bv = (half < 3.2f * kb ? half : 3.2f * kb) > 0.8f * kb ? (half < 3.2f * kb ? half : 3.2f * kb) : 0.8f * kb;
                const float tl = clamp01(A[i] / bv);
                const int hx_ = ((int)(((float)x + 0.5f) / fS - o + s.ox) + 4096) & (NT - 1), hy_ = ((int)(((float)y + 0.5f) / fS - o + s.oy) + 4096) & (NT - 1);
                B[i] = 0.72f * tl + 0.05f * g_ham[hy_ * NT + hx_] * tl;
            }
            if (s.style == 1) {
                // KNIGHT : the face hammered ; along the CENTRE LINE of every full-width bar (m = b/2 : one line on a
                // separator, a filet framing each cut on a broad plate) a channel, a gold wire rising in it. The lines
                // meet at the junctions by themselves (they are iso-lines of the same distance field)
                const float c = 0.5f * b, d = fabsf(A[i] - c), chW = 2.2f * kb, wr = 0.9f * kb;
                const int hx_ = ((int)(((float)x + 0.5f) / fS - o + s.ox) + 4096) & (NT - 1), hy_ = ((int)(((float)y + 0.5f) / fS - o + s.oy) + 4096) & (NT - 1);
                B[i] = B[i] * 0.8f + 0.010f * g_ham[hy_ * NT + hx_] * t;
                if (half >= c - 0.6f * kb) {
                    const float step = smooth01((chW - d) / (0.7f * kb));
                    B[i] -= 0.30f * step;
                    if (d < wr) B[i] += 0.46f * sqrtf(1.0f - (d / wr) * (d / wr));
                }
            }
        }
        gauss(B, W, H, 0.35f * kb * fS, l0);
        PH(4);
        PH(5);
        // 5. shade every sample, average each SxS block (premultiplied) into a 1x pixel
        const float Lx = -0.5f, Ly = -0.7f, Lz = 0.75f, Ln = sqrtf(Lx * Lx + Ly * Ly + Lz * Lz);
        const float lx = Lx / Ln, ly = Ly / Ln, lz = Lz / Ln;
        float hx = lx, hy = ly, hz = lz + 1.0f; { const float n = sqrtf(hx * hx + hy * hy + hz * hz); hx /= n; hy /= n; hz /= n; }
        const float slope = 4.5f * kb * fS;
        const int RR = (int)ceilf(2.0f * fS);
        for (int Y = 0; Y < s.H; ++Y) for (int X = 0; X < s.W; ++X) {
            float ar = 0.0f, ag = 0.0f, ab = 0.0f, aa = 0.0f;
            const int X1 = X + P, Y1 = Y + P;
            // per 1x pixel : the face's light, the grain's direction, the cavity's shadow
            // the SCREEN position : one box shows the same light and grain whatever group it is baked in (2026-11-20)
            const float px = (float)X + 0.5f + s.ox, py = (float)Y + 0.5f + s.oy;
            const int xl = X1 > 0 ? X1 - 1 : X1, xr = X1 < W1 - 1 ? X1 + 1 : X1, yu = Y1 > 0 ? Y1 - 1 : Y1, yd = Y1 < H1 - 1 ? Y1 + 1 : Y1;
            const float mx = CD[Y1 * W1 + xr] - CD[Y1 * W1 + xl], my = CD[yd * W1 + X1] - CD[yu * W1 + X1];
            // a vertical bar -> vertical grain ; a BROAD plate (far from any cut) is brushed horizontally : the direction field
            // flips along its medial axis there, which showed as diagonal seams across a solid plate (2026-11-20)
            const float cdv = CD[Y1 * W1 + X1], broad = cdv > 5.0f * kb ? (cdv > 8.0f * kb ? 1.0f : (cdv - 5.0f * kb) / (3.0f * kb)) : 0.0f;
            const float wv = mx * mx / (mx * mx + my * my + 1e-12f) * (1.0f - broad);
            // the face's soft lights run ALONG each bar (a horizontal one varies with x only, a vertical one with y only) :
            // a box's bottom bar looks the same wherever the box ends (2026-11-20 : the description's bottom crossed a
            // different diagonal band at every item's height -- "elle continue de changer d'eclairage")
            const float along = px * (1.0f - wv) + py * wv;
            const float diag = along / (90.0f * kb), ph = diag - 4.0f * floorf(diag / 4.0f);
            const float e1 = (ph - 1.3f) / 0.45f, e2 = (ph - 2.3f) / 0.18f, e3 = (ph - 3.2f) / 0.6f;
            const float refl = 0.34f + 0.36f * expf(-e1 * e1) + 0.16f * expf(-e2 * e2) + 0.08f * expf(-e3 * e3);
            const int ix = ((int)px + 4096) & (NT - 1), iy = ((int)py + 4096) & (NT - 1);
            const float brush = g_brA[iy * NT + ix] * (1.0f - wv) + g_brB[ix * NT + iy] * wv;
            const float scr = (g_scA[iy * NT + ix] > 1.8f ? g_scA[iy * NT + ix] - 1.8f : 0.0f) * (1.0f - wv)
                            + (g_scB[ix * NT + iy] > 1.8f ? g_scB[ix * NT + iy] - 1.8f : 0.0f) * wv;
            const float grain = 1.0f + 0.10f * brush + 0.04f * g_fine[iy * NT + ix];
            const float shade = 1.0f - 0.6f * SH[Y1 * W1 + X1];
            for (int j = 0; j < S; ++j) for (int i = 0; i < S; ++i) {
                const int x = X1 * S + i, y = Y1 * S + j, p = y * W + x;
                if (!uni[p]) continue;
                float r, g, bl, a;
                if (!met[p]) {                                            // a cavity : dark slate glass, shadowed under the metal
                    r = 18.0f / 255.0f * shade; g = 20.0f / 255.0f * shade; bl = 27.0f / 255.0f * shade; a = 0.88f;
                    if (s.style >= 2) {
                        const float qx0 = ((float)x + 0.5f) / fS - o + s.ox, qy0 = ((float)y + 0.5f) / fS - o + s.oy;
                        const float weave = 0.5f + 0.25f * (sinf(qx0 * 2.1f) * sinf(qy0 * 2.1f)) + 0.06f * g_fine[((int)(qy0 + 4096.0f) & (NT - 1)) * NT + ((int)(qx0 + 4096.0f) & (NT - 1))];
                        const float k3 = shade * (0.92f + 0.10f * weave);
                        r = 33.0f / 255.0f * k3; g = 27.0f / 255.0f * k3; bl = 32.0f / 255.0f * k3; a = 0.93f;
                    }
                    // a SLOT (a cut in the glass, no metal around it -- 2026-11-20 : the slot plate read as a frame inside
                    // the frame) : a WELL sunk in the glass -- darker inside, a soft shadow along its top-left inner edge,
                    // a fine light lip along its bottom-right outer edge
                    const float qx = ((float)x + 0.5f) / fS - o, qy = ((float)y + 0.5f) / fS - o;
                    for (int ci = 0; ci < s.nc; ++ci) {
                        const float* cr = s.cut[ci];
                        if (qx < cr[0] - 2.0f || qy < cr[1] - 2.0f || qx > cr[0] + cr[2] + 2.0f || qy > cr[1] + cr[3] + 2.0f) continue;
                        auto sd = [&](float px_, float py_) {          // the rounded rect's signed distance (px, < 0 inside)
                            const float hw = cr[2] * 0.5f, hh = cr[3] * 0.5f, rr = cr[4] < hw && cr[4] < hh ? cr[4] : (hw < hh ? hw : hh);
                            const float dx = fabsf(px_ - (cr[0] + hw)) - (hw - rr), dy = fabsf(py_ - (cr[1] + hh)) - (hh - rr);
                            const float ex = dx > 0.0f ? dx : 0.0f, ey = dy > 0.0f ? dy : 0.0f;
                            return sqrtf(ex * ex + ey * ey) + (dx > dy ? (dx < 0.0f ? dx : 0.0f) : (dy < 0.0f ? dy : 0.0f)) - rr; };
                        const float d0 = sd(qx, qy);
                        if (d0 < 0.0f) {                                // inside : deeper, the top-left edge's shadow
                            const float ds = sd(qx - 2.0f * kb, qy - 2.0f * kb);
                            const float shw = ds > 0.0f ? 1.0f : expf(ds / (1.2f * kb));
                            const float k2 = 0.62f * (1.0f - 0.55f * shw);
                            r *= k2; g *= k2; bl *= k2; a = 0.94f;
                        } else {                                        // just outside, bottom-right : the lip catches the light
                            const float dl = sd(qx - 1.0f * kb, qy - 1.0f * kb);
                            if (dl < 0.0f && d0 < 1.2f * kb) { const float l = 0.16f * (1.0f - d0 / (1.2f * kb));
                                r += l * 0.80f; g += l * 0.86f; bl += l * 0.95f; }
                        }
                        break;
                    }
                } else {
                    const int sxl = x > 0 ? x - 1 : x, sxr = x < W - 1 ? x + 1 : x, syu = y > 0 ? y - 1 : y, syd = y < H - 1 ? y + 1 : y;
                    const float gx = (B[y * W + sxr] - B[y * W + sxl]) / (float)(sxr - sxl > 0 ? sxr - sxl : 1) * slope;
                    const float gy = (B[syd * W + x] - B[syu * W + x]) / (float)(syd - syu > 0 ? syd - syu : 1) * slope;
                    float nx = -gx, ny = -gy, nz = 1.0f; { const float n = sqrtf(nx * nx + ny * ny + 1.0f); nx /= n; ny /= n; nz /= n; }
                    // the chamfers : a chrome environment (sky above, a dark horizon, ground) -- CONTINUOUS
                    const float Ry = 2.0f * nz * ny, Rx = 2.0f * nz * nx, u = Ry + 0.35f * Rx;
                    const float tt = smooth01((u + 0.15f) / 0.55f);
                    const float env = (1.0f - 0.35f * clamp01(-u - 0.3f)) * (1.0f - tt) + (0.14f + 0.30f * clamp01((u - 0.4f) / 0.6f)) * tt;
                    const float n2 = nz * nz, n4 = n2 * n2, n8 = n4 * n4, face = n8 * n4 * n2;   // nz^14
                    const float base = (refl * face + env * (1.0f - face)) * grain;
                    const float sp = nx * hx + ny * hy + nz * hz;
                    float spc = 0.0f;
                    if (sp > 0.0f) { const float s2 = sp * sp, s4 = s2 * s2, s8 = s4 * s4, s14 = s8 * s4 * s2; spc = (sp > 0.9f ? expf(120.0f * logf(sp)) : 0.0f) + 0.25f * s14; }
                    r = base * 0.78f + 0.25f * scr + spc; g = base * 0.84f + 0.25f * scr + spc; bl = base * 0.94f + 0.25f * scr + spc;
                    if (s.style == 3) {
                        const float dif3 = nx * lx + ny * ly + nz * lz > 0.0f ? nx * lx + ny * ly + nz * lz : 0.0f;
                        const float qx0 = ((float)x + 0.5f) / fS - o + s.ox, qy0 = ((float)y + 0.5f) / fS - o + s.oy;
                        const float hm = g_ham[(((int)qy0 + 4096) & (NT - 1)) * NT + (((int)qx0 + 4096) & (NT - 1))];
                        const float wear = 1.0f - nz;                                 // the slopes : rubbed bright by use
                        const float lit = 0.45f + 0.55f * dif3;                       // 2026-11-20 "plus clair" : was 0.30 + 0.55
                        const float v = (0.36f + 0.85f * wear * (0.5f + 0.5f * dif3)) * lit * (1.0f + 0.03f * g_fine[iy * NT + ix] + 0.03f * brush);   // face was 0.20
                        const float rust = hm < -1.6f ? clamp01(-hm - 1.6f) * 0.12f : 0.0f;
                        const float sp3 = sp > 0.0f ? sp * sp * sp : 0.0f, s8 = sp3 * sp3 * sp3 * sp3 * 1.5f;
                        r = v * (1.0f + 0.5f * rust) + 0.30f * s8; g = v * (0.98f + 0.1f * rust) + 0.30f * s8; bl = v * (1.02f - 0.3f * rust) + 0.32f * s8;
                    }
                    if (s.style == 2) {
                        const float qx0 = ((float)x + 0.5f) / fS - o + s.ox, qy0 = ((float)y + 0.5f) / fS - o + s.oy;
                        const int ix2 = ((int)(qx0 * 0.35f) + 4096) & (NT - 1), iy2 = ((int)(qy0 * 0.35f) + 4096) & (NT - 1);
                        const float mott = g_ham[iy2 * NT + ix2];                                  // broad mottling (the stone's beds)
                        const float fine2 = g_fine[iy * NT + ix];
                        const float pore = fine2 < -2.0f ? (fine2 + 2.0f) * 0.25f : 0.0f;           // a few dark pores
                        const float dif2 = nx * lx + ny * ly + nz * lz > 0.0f ? nx * lx + ny * ly + nz * lz : 0.0f;
                        const float lit = 0.40f + 0.58f * dif2;
                        const float tone = (1.0f + 0.022f * mott + 0.03f * fine2 + pore) * lit;
                        r = 0.76f * tone; g = 0.706f * tone; bl = 0.616f * tone;
                        const float sp2 = sp > 0.0f ? sp * sp * sp * sp : 0.0f; r += 0.05f * sp2; g += 0.05f * sp2; bl += 0.045f * sp2;
                    }
                    if (s.style == 1) {
                        const float hf = bil(HF, W1, H1, ((float)x + 0.5f) / fS, ((float)y + 0.5f) / fS);
                        const float c = 0.5f * b, d = fabsf(A[p] - c), wide = hf >= c - 0.6f * kb ? 1.0f : 0.0f;
                        const float gold = wide * (d < 0.9f * kb ? 1.0f : 0.0f), chan = wide * smooth01((2.2f * kb - d) / (0.7f * kb));
                        const float stl = (refl * 0.55f * face + env * (1.0f - face)) * (1.0f + 0.05f * brush + 0.05f * g_fine[iy * NT + ix]);
                        const float pat = 1.0f - 0.45f * chan * (1.0f - gold);           // patina in the channel
                        r = stl * 0.50f * pat + 0.08f * scr + spc * 0.8f; g = stl * 0.56f * pat + 0.08f * scr + spc * 0.8f; bl = stl * 0.68f * pat + 0.08f * scr + spc * 0.85f;
                        if (gold > 0.0f) {                                               // the gold wire : warm, its own sheen
                            const float gv = 0.10f + 0.80f * env * env;
                            r = 0.86f * gv + spc * 0.9f; g = 0.62f * gv + spc * 0.75f; bl = 0.26f * gv + spc * 0.45f;
                        }
                    }
                    const float m = A[p];
                    if (m < 2.0f) {
                        // the distance to the OUTSIDE, searched nearby : the outer edge gets a faint light rim, a cut a dark contour
                        int best = RR * RR * 4 + 1;
                        for (int dy = -RR; dy <= RR; ++dy) { const int yy = y + dy; if (yy < 0 || yy >= H) continue;
                            for (int dx = -RR; dx <= RR; ++dx) { const int xx = x + dx; if (xx < 0 || xx >= W) continue;
                                if (!uni[yy * W + xx]) { const int d2 = dx * dx + dy * dy; if (d2 < best) best = d2; } } }
                        const float D = best <= RR * RR * 4 ? (sqrtf((float)best) - 0.5f) / fS : 99.0f;
                        if (D <= m + 0.01f) {
                            const float e = expf(-(D / 0.5f) * (D / 0.5f));
                            r = r * (1.0f - 0.5f * e) + 0.75f * 0.45f * e; g = g * (1.0f - 0.5f * e) + 0.80f * 0.45f * e; bl = bl * (1.0f - 0.5f * e) + 0.88f * 0.45f * e;
                        } else if (m < 1.5f) {
                            const float e = 1.0f - 0.7f * expf(-(m / 0.45f) * (m / 0.45f)); r *= e; g *= e; bl *= e;
                        }
                    }
                    a = 1.0f;
                }
                ar += clamp01(r) * a; ag += clamp01(g) * a; ab += clamp01(bl) * a; aa += a;
            }
            const float n = (float)(S * S);
            const float fa = aa / n;
            u32 pxl = 0;
            if (fa > 0.0f) {
                const u32 A8 = (u32)(fa * 255.0f + 0.5f), R8 = (u32)(ar / aa * 255.0f + 0.5f), G8 = (u32)(ag / aa * 255.0f + 0.5f), B8 = (u32)(ab / aa * 255.0f + 0.5f);
                pxl = (A8 > 255 ? 255u : A8) << 24 | (R8 > 255 ? 255u : R8) << 16 | (G8 > 255 ? 255u : G8) << 8 | (B8 > 255 ? 255u : B8);
            }
            out[Y * s.W + X] = pxl;
        }
    }
    PH(6);
    hfree(uni); hfree(met); hfree(A); hfree(B); hfree(HF); hfree(CD); hfree(SH);
    hfree(l0); hfree(l1); hfree(lg); hfree(lh);
    hfree(sc.f); hfree(sc.d); hfree(sc.z); hfree(sc.v);
    return ok;
}

} // namespace aio
