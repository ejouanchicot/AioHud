// font.cpp -- see font.h. One GDI-baked atlas PER display size (pixel-perfect, no down-scaling).
#include "gfx/font.h"
#include "gfx/draw.h"
#include "gfx/texture.h"
#include "model/paths.h"   // plugin_path_w : runtime-derived fonts dir (gfx infra exception to the layering rule)
#include "windower_debug.h"   // debug::log -- gfx had NO instrumentation at all, so every failure here was mute
#include <windows.h>

namespace aio {

static const int FIRST = 32, LAST = 255;   // printable ASCII + Latin-1 supplement (accents for French)

// Decode the next character from a UTF-8 string and ADVANCE p past it : the full codepoint (1 to 4 bytes). 32..255
// come from the slot's own atlas, the rest from the shared wide pages (below). A broken sequence -> '?'.
static inline int utf8_next(const char*& p) {
    const unsigned b0 = (unsigned char)*p++;
    if (b0 < 0x80) return (int)b0;
    const int n = (b0 & 0xE0) == 0xC0 ? 1 : (b0 & 0xF0) == 0xE0 ? 2 : (b0 & 0xF8) == 0xF0 ? 3 : -1;
    if (n < 0) { while (((unsigned char)*p & 0xC0) == 0x80) ++p; return '?'; }
    unsigned cp = b0 & (0x3Fu >> n);
    for (int i = 0; i < n; ++i) {
        const unsigned char b = (unsigned char)*p;
        if ((b & 0xC0) != 0x80) return '?';
        cp = (cp << 6) | (b & 0x3Fu); ++p;
    }
    return (int)cp;
}

static inline int iabs(int v) { return v < 0 ? -v : v; }

// ===== WIDE glyphs : every codepoint past Latin-1, baked ON DEMAND into a few pages shared by every Font =====
// (2026-11-17 : Japanese in the chat came out as "???" -- the slot atlases only hold 32..255.) A page = one em size x
// bold / regular, a 512 x 512 single-level texture filled glyph by glyph (GDI TextOutW with a face that HAS the
// glyphs, then LockRect on that cell only) ; full -> it starts over (glyphs re-bake as they are drawn). Metrics are
// known before the texture exists (measure() needs no device).
namespace {
const int WP_W = 512, WP_H = 512, WP_MAX = 600, WP_PAGES = 6, WP_HASH = 1024;
// The ADVANCES live in their own, bigger table (review 2026-11-17) : measure() walks the whole chat history, and a
// JP-heavy one used to fill the 600 BAKE entries with glyphs never drawn -- past that, wrong widths ('?') and a page
// cleared + re-baked every frame. Metrics now never cost a bake entry, and a full bake page never forgets a width.
const int WM_HASH = 4096;
struct WGlyph { unsigned cp; float u0, v0, u1, v1, w, h, adv; bool baked; };
struct WPage {
    int em = 0; bool bold = false; u32 tex = 0; HFONT hf = 0; int cellH = 0, ascent = 0;
    int penx = 2, peny = 2, n = 0; unsigned used = 0; bool full = false;
    short idx[WP_HASH]; WGlyph g[WP_MAX];
    unsigned mcp[WM_HASH]; unsigned short madv[WM_HASH]; int mn = 0;   // codepoint -> advance (px), 0 = empty slot
};
WPage g_wp[WP_PAGES]; unsigned g_wpClock = 0; HDC g_wdc = 0; wchar_t g_wface[40] = L"";
HBITMAP g_wbm = 0; void* g_wbits = 0;   // the 128 x 128 scratch one glyph is drawn into

HDC wdc() { if (!g_wdc) g_wdc = CreateCompatibleDC(0); return g_wdc; }
// the first installed face that has Japanese : Yu Gothic UI (every Windows 10/11), else Meiryo UI, else MS UI Gothic
const wchar_t* wide_face() {
    if (g_wface[0]) return g_wface;
    static const wchar_t* C[] = { L"Yu Gothic UI", L"Meiryo UI", L"MS UI Gothic", L"MS Gothic", L"Arial Unicode MS" };
    HDC dc = wdc();
    for (int i = 0; dc && i < (int)(sizeof(C) / sizeof(C[0])); ++i) {
        HFONT f = CreateFontW(-16, 0, 0, 0, 400, 0, 0, 0, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, C[i]);
        if (!f) continue;
        HGDIOBJ o = SelectObject(dc, f); wchar_t got[64] = L""; GetTextFaceW(dc, 64, got); SelectObject(dc, o); DeleteObject(f);
        if (!lstrcmpiW(got, C[i])) { lstrcpynW(g_wface, C[i], 40); break; }
    }
    if (!g_wface[0]) lstrcpynW(g_wface, L"MS Gothic", 40);
    char a[64]; WideCharToMultiByte(CP_UTF8, 0, g_wface, -1, a, sizeof(a), 0, 0);
    windower::debug::log("FONT wide glyphs (Japanese, symbols) : face '%s'", a);
    return g_wface;
}
void page_clear(WPage& P) { P.n = 0; P.penx = 2; P.peny = 2; P.full = false; for (int i = 0; i < WP_HASH; ++i) P.idx[i] = -1; }
WPage* wide_page(int em, bool bold) {
    const unsigned clk = ++g_wpClock;
    for (int i = 0; i < WP_PAGES; ++i) if (g_wp[i].em == em && g_wp[i].bold == bold && g_wp[i].hf) { g_wp[i].used = clk; return &g_wp[i]; }
    int v = 0;
    for (int i = 0; i < WP_PAGES; ++i) { if (!g_wp[i].hf) { v = i; break; } if (g_wp[i].used < g_wp[v].used) v = i; }
    WPage& P = g_wp[v];
    if (P.tex) { release_texture(P.tex); P.tex = 0; }
    if (P.hf) { DeleteObject(P.hf); P.hf = 0; }
    P.em = em; P.bold = bold; P.used = clk; page_clear(P);
    memset(P.mcp, 0, sizeof(P.mcp)); P.mn = 0;                      // a new size : its own advances
    P.hf = CreateFontW(-em, 0, 0, 0, bold ? 600 : 400, 0, 0, 0, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                       ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, wide_face());
    HDC dc = wdc();
    if (!P.hf || !dc) { P.em = 0; return 0; }
    HGDIOBJ o = SelectObject(dc, P.hf); TEXTMETRICW tm; GetTextMetricsW(dc, &tm); SelectObject(dc, o);
    P.cellH = tm.tmHeight; P.ascent = tm.tmAscent;
    return &P;
}
inline int whash(unsigned cp) { return (int)((cp * 2654435761u) >> 22) & (WP_HASH - 1); }
// the ADVANCE of cp at this page's size (px) : the metrics table, filled from GDI on first sight (no device needed)
float wide_adv(WPage& P, unsigned cp) {
    int h = (int)((cp * 2654435761u) >> 20) & (WM_HASH - 1);
    for (int k = 0; k < WM_HASH; ++k, h = (h + 1) & (WM_HASH - 1)) {
        if (P.mcp[h] == cp) return (float)P.madv[h];
        if (!P.mcp[h]) break;
    }
    if (P.mn >= WM_HASH * 3 / 4) { memset(P.mcp, 0, sizeof(P.mcp)); P.mn = 0; h = (int)((cp * 2654435761u) >> 20) & (WM_HASH - 1); }   // 3000+ distinct : start over
    int adv = P.em;
    HDC dc = wdc();
    if (dc) { const wchar_t wc = (wchar_t)cp; SIZE sz = { 0, 0 }; HGDIOBJ o = SelectObject(dc, P.hf); GetTextExtentPoint32W(dc, &wc, 1, &sz); SelectObject(dc, o); if (sz.cx > 0) adv = sz.cx; }
    P.mcp[h] = cp; P.madv[h] = (unsigned short)adv; ++P.mn;
    return (float)adv;
}
// the glyph's BAKE entry, looked up (create = false) or added (create = true) ; 0 when absent / out of entries
WGlyph* wide_glyph(WPage& P, unsigned cp, bool create) {
    int h = whash(cp);
    for (int k = 0; k < WP_HASH; ++k, h = (h + 1) & (WP_HASH - 1)) {
        const short i = P.idx[h];
        if (i < 0) break;
        if (P.g[i].cp == cp) return &P.g[i];
    }
    if (!create || P.n >= WP_MAX) return 0;
    WGlyph& G = P.g[P.n]; G.cp = cp; G.adv = wide_adv(P, cp); G.w = G.adv + 4.0f; G.h = (float)P.cellH; G.baked = false;
    G.u0 = G.v0 = G.u1 = G.v1 = 0.0f;
    P.idx[h] = (short)P.n++;
    return &G;
}
// draw the glyph into the page's texture ; false when the page has no room left (the caller starts it over)
bool wide_bake(u32 dev, WPage& P, WGlyph& G) {
    if (G.baked) return true;
    if (!P.tex) { P.tex = make_texture_blank(dev, WP_W, WP_H); if (!P.tex) return false; }
    const int w = (int)G.w, h = P.cellH;
    if (w <= 0 || h <= 0 || w > 128 || h > 128) return false;
    if (P.penx + w + 2 > WP_W) { P.penx = 2; P.peny += h + 2; }
    if (P.peny + h + 2 > WP_H) { P.full = true; return false; }
    HDC dc = wdc(); if (!dc) return false;
    if (!g_wbm) {
        BITMAPINFO bi; ZeroMemory(&bi, sizeof(bi)); bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = 128; bi.bmiHeader.biHeight = -128; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
        g_wbm = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &g_wbits, 0, 0);
        if (!g_wbm) return false;
    }
    void* bits = g_wbits;
    memset(bits, 0, 128 * 128 * 4);
    HGDIOBJ ob = SelectObject(dc, g_wbm), of = SelectObject(dc, P.hf);
    SetTextColor(dc, RGB(255, 255, 255)); SetBkMode(dc, TRANSPARENT);
    const wchar_t wc = (wchar_t)G.cp; TextOutW(dc, 2, 0, &wc, 1); GdiFlush();
    SelectObject(dc, of); SelectObject(dc, ob);
    static u32 px[128 * 128];
    const u32* src = (const u32*)bits;
    for (int yy = 0; yy < h; ++yy) for (int xx = 0; xx < w; ++xx) { const u32 cov = (src[yy * 128 + xx] >> 16) & 0xFF; px[yy * w + xx] = (cov << 24) | 0x00FFFFFFu; }
    if (!texture_write(P.tex, P.penx, P.peny, w, h, px)) return false;
    G.u0 = (float)P.penx / WP_W; G.u1 = (float)(P.penx + w) / WP_W; G.v0 = (float)P.peny / WP_H; G.v1 = (float)(P.peny + h) / WP_H;
    G.baked = true; P.penx += w + 2;
    return true;
}
inline bool is_wide(int c) { return c > 255 && c <= 0xFFFF; }   // beyond the BMP : '?'
} // namespace

void font_wide_forget() { for (int i = 0; i < WP_PAGES; ++i) { g_wp[i].tex = 0; if (g_wp[i].hf) page_clear(g_wp[i]); } }
void font_wide_dispose() {
    for (int i = 0; i < WP_PAGES; ++i) {
        if (g_wp[i].tex) release_texture(g_wp[i].tex);
        if (g_wp[i].hf) DeleteObject(g_wp[i].hf);
        g_wp[i].tex = 0; g_wp[i].hf = 0; g_wp[i].em = 0;
    }
    if (g_wbm) { DeleteObject(g_wbm); g_wbm = 0; g_wbits = 0; }   // GDI objects : one set per //load otherwise
    if (g_wdc) { DeleteDC(g_wdc); g_wdc = 0; }
}

// ---- glyph batching -------------------------------------------------------------
// A whole string (all 8 outline offset passes, then the main pass) is accumulated into ONE
// vertex list and submitted with a single DrawPrimitiveUP -- instead of one submit per glyph
// (the old per-glyph tquad). A 5-char outlined label went from 9*5 = 45 DrawPrimitiveUP calls
// to 2. Rendering is single-threaded (inside EndScene), so a file-static scratch buffer is safe
// and keeps the "no per-frame heap" rule. TRIANGLELIST (6 verts/quad) so quads concatenate with
// no degenerate verts a TRIANGLESTRIP would need; geometry per quad is byte-identical to tquad().
static const int GBUF_CAP = 6144;                       // 1024 quads ; longer strings flush in chunks
static Vtx  g_gbuf[GBUF_CAP];
static int  g_gn = 0;

static inline void gbuf_flush(u32 dev) {
    if (g_gn >= 3) dDrawUP(dev, D3DPT_TRIANGLELIST, g_gn / 3, g_gbuf, sizeof(Vtx));
    g_gn = 0;
}
// An optional ROTATION of every glyph quad about a point (Font::set_rotation ; off by default) : text that follows a
// curved instrument (the minimap's wheels on an arc). Rotated glyphs lose the pixel snap -- used for short labels only.
static bool  g_rotOn = false;
static float g_rotX = 0.0f, g_rotY = 0.0f, g_rotC = 1.0f, g_rotS = 0.0f;
static inline void gbuf_quad(u32 dev, float x, float y, float w, float h,
                             float u0, float u1, float v0, float v1, u32 c) {
    if (g_gn + 6 > GBUF_CAP) gbuf_flush(dev);           // full -> submit and keep going
    x -= 0.5f; y -= 0.5f;                               // same D3D half-texel rule tquad() applied
    Vtx tl = { x,     y,     0, 1, c, u0, v0 };
    Vtx tr = { x + w, y,     0, 1, c, u1, v0 };
    Vtx bl = { x,     y + h, 0, 1, c, u0, v1 };
    Vtx br = { x + w, y + h, 0, 1, c, u1, v1 };
    if (g_rotOn) {
        Vtx* q[4] = { &tl, &tr, &bl, &br };
        for (int i = 0; i < 4; ++i) {
            const float dx = q[i]->x + 0.5f - g_rotX, dy = q[i]->y + 0.5f - g_rotY;
            q[i]->x = g_rotX + dx * g_rotC - dy * g_rotS - 0.5f; q[i]->y = g_rotY + dx * g_rotS + dy * g_rotC - 0.5f;
        }
    }
    g_gbuf[g_gn++] = tl; g_gbuf[g_gn++] = tr; g_gbuf[g_gn++] = bl;   // tri 1
    g_gbuf[g_gn++] = tr; g_gbuf[g_gn++] = br; g_gbuf[g_gn++] = bl;   // tri 2
}

// ---- bake the glyphs at `em` pixels into slot s, on a right-sized power-of-two atlas. ----
void Font::build(u32 dev, Slot& s, int em) {
    if (em < 7) em = 7; if (em > 72) em = 72;   // cap raised to 72 so BIG text (WS popup) bakes crisp instead of scaling a 36px atlas up
    const int AW = 512;                          // keep the atlas 512 WIDE (half the memory/bake cost) ; grow HEIGHT for big em
    const int AH = (em <= 14) ? 256 : (em <= 26 ? 512 : (em <= 40 ? 1024 : 2048));   // 512x2048 fits 224 glyphs at em=72

    HDC hdc = CreateCompatibleDC(0);
    if (!hdc) return;
    BITMAPINFO bmi; ZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = AW;
    bmi.bmiHeader.biHeight = -AH;           // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bits = 0;
    HBITMAP hbm = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &bits, 0, 0);
    if (!hbm) { DeleteDC(hdc); return; }
    HGDIOBJ oldbm = SelectObject(hdc, hbm);
    memset(bits, 0, AW * AH * 4);

    HFONT hf = CreateFontA(-em, 0, 0, 0, weight_, italic_ ? 1 : 0, 0, 0,
                           DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                           ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face_);
    HGDIOBJ oldf = SelectObject(hdc, hf);
    SetTextColor(hdc, RGB(255, 255, 255));
    SetBkMode(hdc, TRANSPARENT);

    TEXTMETRICA tm; GetTextMetricsA(hdc, &tm);
    s.ascent = (float)tm.tmAscent;              // the wide glyphs sit on this baseline (their own face's ascent differs)
    int cellH = tm.tmHeight, pad = 2;
    int penx = pad, peny = pad, rowH = cellH + pad;
    const u32* px = (const u32*)bits;
    int inkTop = cellH, inkBot = -1;

    for (int c = FIRST; c <= LAST; ++c) {
        char ch = (char)c;
        int adv; ABC abc; int inkR = 0;
        if (GetCharABCWidthsA(hdc, c, c, &abc)) { adv = abc.abcA + (int)abc.abcB + abc.abcC; inkR = abc.abcA + (int)abc.abcB; }
        else { SIZE sz; GetTextExtentPoint32A(hdc, &ch, 1, &sz); adv = sz.cx; }
        if (adv < 1) adv = (int)(em * 0.3f);
        // the CELL : the advance, widened to the ink's right edge when the glyph overhangs it (a negative C width :
        // bold digits do) plus 1 px of antialiasing. A cell cut at the advance clipped the right of the LAST character
        // of every string (the user 2026-11-25 : "the ping is cut on its right, look at the 0") -- mid-string the next
        // glyph covered it. The quad is this wide ; the pen still moves by the advance.
        const int cw = (inkR + 1 > adv ? inkR + 1 : adv);
        if (penx + cw + pad > AW) { penx = pad; peny += rowH; }
        if (peny + cellH > AH) {
            // SAY IT. Every glyph past this point keeps its default G -- w = h = adv = 0 -- and the slot is still
            // published as valid below, so those characters render as nothing AND advance nothing: they vanish
            // without even shifting the rest of the line. That is a partial success being treated as a success
            // (CLAUDE.md rule 10, third form), and it was completely silent. Reachable at the 41..72px tier,
            // where a wide face can run out of the 512x2048 sheet before the Latin-1 accents are engraved.
            windower::debug::log("FONT atlas FULL : '%s' w%d em=%d -- engraved up to codepoint %d of %d, the rest render as NOTHING",
                                 face_, weight_, em, c - 1, LAST);
            break;
        }
        if (c != ' ') TextOutA(hdc, penx, peny, &ch, 1);
        G& g = s.g[c - FIRST];
        g.u0 = (float)penx / AW;          g.u1 = (float)(penx + cw) / AW;
        g.v0 = (float)peny / AH;          g.v1 = (float)(peny + cellH) / AH;
        g.w  = (float)cw; g.h = (float)cellH; g.adv = (float)adv;

        // per-glyph INK bbox (rel. to this cell's top-left) -> true visual centring (draw_cc)
        {
            int minx = cw, maxx = -1, miny = cellH, maxy = -1;
            for (int yy = 0; yy < cellH; ++yy) {
                const u32* row = px + (peny + yy) * AW;
                for (int xx = penx; xx < penx + cw && xx < AW; ++xx)
                    if (((row[xx] >> 16) & 0xFF) > 40) {
                        int rx = xx - penx;
                        if (rx < minx) minx = rx; if (rx > maxx) maxx = rx;
                        if (yy < miny) miny = yy; if (yy > maxy) maxy = yy;
                    }
            }
            if (maxx >= 0) { g.il = (float)minx; g.ir = (float)(maxx + 1); g.it = (float)miny; g.ib = (float)(maxy + 1); }
            else           { g.il = 0; g.ir = (float)adv; g.it = 0; g.ib = (float)cellH; }
        }

        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {    // cap/digit ink extent -> vertical centring
            for (int yy = 0; yy < cellH; ++yy) {
                const u32* row = px + (peny + yy) * AW;
                bool ink = false;
                for (int xx = penx; xx < penx + adv && xx < AW; ++xx) if (((row[xx] >> 16) & 0xFF) > 40) { ink = true; break; }
                if (ink) { if (yy < inkTop) inkTop = yy; if (yy > inkBot) inkBot = yy; }
            }
        }
        penx += cw + pad;
    }

    s.em = em; s.base = (float)em;
    s.cap_top = (inkBot >= 0) ? (float)inkTop : cellH * 0.2f;
    s.cap_h   = (inkBot >= 0) ? (float)(inkBot - inkTop + 1) : cellH * 0.66f;

    // GDI wrote white AA glyphs (coverage in R). Convert -> ARGB (alpha = coverage, rgb = white).
    u32* buf = (u32*)HeapAlloc(GetProcessHeap(), 0, AW * AH * 4);
    if (buf) {
        const u32* src = (const u32*)bits;
        for (int i = 0; i < AW * AH; ++i) { u32 cov = (src[i] >> 16) & 0xFF; buf[i] = (cov << 24) | 0x00FFFFFF; }
        s.tex = make_texture_argb_mip(dev, AW, AH, buf);   // a light mip chain : crisp at native size (scale 1.0 = mip 0), gently smoothed only where slightly scaled
        HeapFree(GetProcessHeap(), 0, buf);
    }

    SelectObject(hdc, oldf); DeleteObject(hf);
    SelectObject(hdc, oldbm); DeleteObject(hbm);
    DeleteDC(hdc);
}

// Per-frame cap on NEW atlas bakes, shared across all Font instances. Baking a 512x(256..2048) GDI atlas is
// the single biggest load-time hitch (frame 1 wants ~10-15 sizes at once). We amortise : the Hud resets this to
// a small budget each frame, so new sizes bake a couple per frame and the rest transiently reuse the nearest
// already-baked size (a hair blurry for 1-2 frames, then crisp). No visible pop, no startup freeze.
static int g_fontBakeBudget = 1 << 30;
void font_set_bake_budget(int n) { g_fontBakeBudget = n; }

// Drop whatever glyphs are sitting half-accumulated in the shared batch. Called from the HUD's SEH handler: a
// widget faulting mid-Font::emit leaves g_gn pointing past vertices that were never flushed, and the NEXT frame
// appends behind them -- so the first flush of the new frame submits the faulting frame's leftovers, with their
// old screen coordinates and old UVs, against whatever atlas is bound now. One stray line of ghost text. The
// HUD's handler is explicitly built to absorb faults that repeat every frame, so this is not a corner case.
void font_reset_batch() { g_gn = 0; }

int Font::pick(u32 dev, float size) {
    int em = (int)(size + 0.5f); if (em < 7) em = 7; if (em > 72) em = 72;
    const unsigned clk = ++useClock_;
    for (int i = 0; i < nslot_; ++i) if (slot_[i].em == em) { slot_[i].used = clk; return slot_[i].tex ? i : -1; }
    // want a NEW size : bake it only if the frame's bake budget allows AND we have a fallback to show meanwhile.
    if (nslot_ < NSLOT && (g_fontBakeBudget > 0 || nslot_ == 0)) {
        build(dev, slot_[nslot_], em);
        if (slot_[nslot_].tex) { slot_[nslot_].used = clk; --g_fontBakeBudget; return nslot_++; }
        return -1;
    }
    // POOL FULL : recycle the least recently used slot instead of blurring this size forever. Without this the
    // first NSLOT sizes ever requested owned the pool for the whole session (nslot_ only ever grew), so which
    // text was crisp depended on the order the user happened to open things in -- and changed at every zone.
    // Still budget-gated: under the load-time throttle we keep falling back to the nearest size for a frame or
    // two rather than thrashing the atlas, exactly as before.
    if (nslot_ >= NSLOT && g_fontBakeBudget > 0) {
        int v = 0; for (int i = 1; i < nslot_; ++i) if (slot_[i].used < slot_[v].used) v = i;
        const u32 old = slot_[v].tex;
        Slot fresh; build(dev, fresh, em);
        if (fresh.tex) {
            // Only drop the old atlas once the new one exists -- a failed bake must not cost us a working slot.
            if (old) release_texture(old);
            slot_[v] = fresh; slot_[v].used = clk;
            --g_fontBakeBudget;
            return v;
        }
    }
    int best = -1, bd = 99999;                       // budget spent / bake failed -> reuse the nearest baked size for now
    for (int i = 0; i < nslot_; ++i) if (slot_[i].tex) { int d = iabs(slot_[i].em - em); if (d < bd) { bd = d; best = i; } }
    return best;
}

int Font::pickC(float size) const {
    int em = (int)(size + 0.5f); if (em < 7) em = 7; if (em > 72) em = 72;
    int best = -1, bd = 99999;
    for (int i = 0; i < nslot_; ++i) if (slot_[i].tex) {
        if (slot_[i].em == em) return i;
        int d = iabs(slot_[i].em - em); if (d < bd) { bd = d; best = i; }
    }
    return best;
}

void Font::ensure(u32 dev) {
    if (!valid_ptr(dev)) return;
    if (dirty_) {                                    // face/weight changed -> drop every cached size
        for (int i = 0; i < nslot_; ++i) if (slot_[i].tex) release_texture(slot_[i].tex);
        for (int i = 0; i < NSLOT; ++i) slot_[i] = Slot();
        nslot_ = 0; dirty_ = false;
    }
    if (nslot_ == 0) { build(dev, slot_[0], 18); if (slot_[0].tex) nslot_ = 1; }   // a default slot -> ready()/measure() always have a fallback
}

void Font::on_device_lost() { for (int i = 0; i < NSLOT; ++i) slot_[i].tex = 0; nslot_ = 0; }
// Release AND forget. Zeroing the handles matters now that pick() recycles slots: nslot_ = 0 alone left dangling
// handles in the array that a later build() would silently overwrite, leaking whatever they pointed at.
void Font::dispose()        { for (int i = 0; i < nslot_; ++i) if (slot_[i].tex) { release_texture(slot_[i].tex); slot_[i].tex = 0; } nslot_ = 0; }

void Font::set_face(const char* face, int weight, bool italic) {
    if (face && face[0] && lstrcmpA(face, face_) != 0) { lstrcpynA(face_, face, sizeof(face_)); dirty_ = true; }
    if (weight > 0 && weight != weight_) { weight_ = weight; dirty_ = true; }
    if (italic != italic_) { italic_ = italic; dirty_ = true; }
}

// ---- FontManager ----
void FontManager::set_default(const char* face, int weight) {
    if (face && face[0]) lstrcpynA(defFace_, face, sizeof(defFace_));
    if (weight > 0) defWeight_ = weight;
}
// BUNDLED fonts : register every .ttf/.otf in assets\fonts as PRIVATE process fonts (AddFontResourceEx,
// no system install / no admin needed). CreateFont then finds them by family name -> ship Roboto etc. with
// the plugin and they work on any PC. Safe if the folder is missing (just registers nothing).
// each bundled file : the LABEL the config uses (its file name before the dash, spaced : OpenSans -> "Open Sans"), the
// FAMILY name inside it (GDI's : name id 1 -- some static cuts name their 400 "Montserrat Thin") and its weight (OS/2)
struct BundledFace { char label[40]; char family[64]; int weight; };
static BundledFace g_bf[96]; static int g_bfN = 0;
static void bf_label(const wchar_t* fn, char* out, int cap) {
    int o = 0; wchar_t prev = 0;
    for (int i = 0; fn[i] && fn[i] != L'-' && fn[i] != L'.' && o < cap - 2; ++i) {
        const wchar_t c = fn[i];
        const bool up = c >= L'A' && c <= L'Z', dig = c >= L'0' && c <= L'9', plow = prev >= L'a' && prev <= L'z';
        if (o > 0 && ((up && plow) || (dig && !(prev >= L'0' && prev <= L'9')))) out[o++] = ' ';
        out[o++] = (char)c; prev = c;
    }
    out[o] = 0;
}
static bool bf_read(const wchar_t* path, char* fam, int cap, int* weight) {
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
    if (h == INVALID_HANDLE_VALUE) return false;
    static unsigned char b[1 << 20]; DWORD got = 0; const BOOL ok = ReadFile(h, b, sizeof(b), &got, 0); CloseHandle(h);
    if (!ok || got < 12) return false;
    auto u16 = [&](DWORD o) -> unsigned { return o + 2 <= got ? (unsigned)(b[o] << 8 | b[o + 1]) : 0u; };
    auto u32 = [&](DWORD o) -> DWORD { return o + 4 <= got ? (DWORD)b[o] << 24 | (DWORD)b[o + 1] << 16 | (DWORD)b[o + 2] << 8 | b[o + 3] : 0u; };
    const unsigned nt = u16(4); DWORD nameO = 0, os2O = 0;
    for (unsigned i = 0; i < nt && 12 + 16 * i + 16 <= got; ++i) {
        const DWORD r = 12 + 16 * i;
        if (!memcmp(b + r, "name", 4)) nameO = u32(r + 8);
        if (!memcmp(b + r, "OS/2", 4)) os2O = u32(r + 8);
    }
    *weight = os2O ? (int)u16(os2O + 4) : 400;
    fam[0] = 0; if (!nameO) return false;
    const unsigned cnt = u16(nameO + 2), so = u16(nameO + 4);
    for (unsigned i = 0; i < cnt; ++i) {
        const DWORD r = nameO + 6 + 12 * i;
        if (u16(r) == 3 && u16(r + 6) == 1) {                        // Windows platform, name id 1 : the family GDI groups by
            const unsigned len = u16(r + 8), off = u16(r + 10); int o = 0;
            for (unsigned k = 0; k + 1 < len && o < cap - 1; k += 2) { const unsigned c = u16(nameO + so + off + k); fam[o++] = c < 128 ? (char)c : '?'; }
            fam[o] = 0; return fam[0] != 0;
        }
    }
    return false;
}
// a bundled LABEL at a weight -> the family (and weight) of its nearest file ; false : not a bundled label
static bool bf_resolve(const char* label, int weight, const char*& family, int& w) {
    int best = -1, bd = 100000;
    for (int i = 0; i < g_bfN; ++i) if (!lstrcmpiA(g_bf[i].label, label)) { const int d = abs(g_bf[i].weight - weight); if (d < bd) { bd = d; best = i; } }
    if (best < 0) return false;
    family = g_bf[best].family; w = g_bf[best].weight; return true;
}
static void register_bundled_fonts_once() {
    // Latch on SUCCESS, not on the first attempt : the assets\fonts\ folder can be briefly unreadable while the
    // updater extracts the zip (the exact scenario CLAUDE.md documents), and giving up then left the bundled faces
    // unavailable all session (falling back to a system face). Retry a few times, then stop.
    static bool done = false; static int tries = 0; if (done) return;   // rule10-ok: bounded retry (8 tries), latched on SUCCESS -- this IS the prescribed shape
    wchar_t dir[MAX_PATH]; plugin_path_w(dir, MAX_PATH, L"assets\\fonts\\");   // runtime-derived (was a hardcoded dev path)
    if (!dir[0]) { if (++tries >= 8) done = true; return; }   // rule10-ok: bounded retry, see the note above
    wchar_t pat[600]; wsprintfW(pat, L"%s*.*", dir);
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) { if (++tries >= 8) done = true; return; }   // transient lock -> retry next call ; rule10-ok: bounded
    done = true;   // folder opened -> this is the authoritative pass (even if empty) ; rule10-ok: latched on SUCCESS
    do {
        int L = lstrlenW(fd.cFileName);
        if (L > 4) {
            const wchar_t* ext = fd.cFileName + L - 4;
            if (!lstrcmpiW(ext, L".ttf") || !lstrcmpiW(ext, L".otf") || !lstrcmpiW(ext, L".ttc")) {
                wchar_t path[600]; wsprintfW(path, L"%s%s", dir, fd.cFileName);
                if (AddFontResourceExW(path, FR_PRIVATE, 0) > 0 && g_bfN < 96) {
                    BundledFace& e = g_bf[g_bfN];
                    bf_label(fd.cFileName, e.label, sizeof(e.label));
                    if (bf_read(path, e.family, sizeof(e.family), &e.weight)) ++g_bfN;
                }
            }
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    windower::debug::log("FONTS bundled : %d file(s) registered from assets\\fonts", g_bfN);
}
bool font_bundled(const char* label) {
    register_bundled_fonts_once();
    for (int i = 0; i < g_bfN; ++i) if (!lstrcmpiA(g_bf[i].label, label)) return true;
    return false;
}

Font* FontManager::get(const char* face, int weight, bool italic) {
    register_bundled_fonts_once();
    const char* fc = (face && face[0]) ? face : defFace_;
    int w = weight > 0 ? weight : defWeight_;
    for (int i = 0; i < n_; ++i) if (wt_[i] == w && it_[i] == italic && lstrcmpA(face_[i], fc) == 0) return &f_[i];
    if (n_ < MAXF) {
        lstrcpynA(face_[n_], fc, 64); wt_[n_] = w; it_[n_] = italic;
        const char* rf = fc; int rw = w;
        bf_resolve(fc, w, rf, rw);                                       // a bundled label : its nearest file's own family / weight
        f_[n_].set_face(rf, rw, italic);
        return &f_[n_++];
    }
    return &f_[0];   // pool full -> fall back to the default slot
}
void FontManager::ensure_all(u32 dev)   { for (int i = 0; i < n_; ++i) f_[i].ensure(dev); }
void FontManager::on_device_lost()      { for (int i = 0; i < n_; ++i) f_[i].on_device_lost(); font_wide_forget(); }
void FontManager::dispose()             { for (int i = 0; i < n_; ++i) f_[i].dispose(); font_wide_dispose(); }

// ---- render ----
void Font::begin(u32 dev) {
    PERF_SCOPE("gfx.text");   // //aio perf : every glyph pass + the text state block, across all modules (audit P3)
    dSetVS(dev, FVF_XYZRHW_DIFFUSE_TEX1);
    dSetRS(dev, D3DRS_ZENABLE, 0);
    dSetRS(dev, D3DRS_CULLMODE, D3DCULL_NONE);
    dSetRS(dev, D3DRS_LIGHTING, 0);
    dSetRS(dev, D3DRS_ALPHATESTENABLE, 0);
    dSetRS(dev, D3DRS_FOGENABLE, 0);
    dSetRS(dev, D3DRS_ALPHABLENDENABLE, 1);
    dSetRS(dev, D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    dSetRS(dev, D3DRS_BLENDOP, D3DBLENDOP_ADD);
    dSetTSS(dev, 0, D3DTSS_COLOROP,   D3DTOP_MODULATE);
    dSetTSS(dev, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    dSetTSS(dev, 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    dSetTSS(dev, 0, D3DTSS_ALPHAOP,   D3DTOP_MODULATE);
    dSetTSS(dev, 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    dSetTSS(dev, 0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    dSetTSS(dev, 0, D3DTSS_ADDRESSU,  D3DTADDRESS_CLAMP);
    dSetTSS(dev, 0, D3DTSS_ADDRESSV,  D3DTADDRESS_CLAMP);
    dSetTSS(dev, 0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
    dSetTSS(dev, 0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
    dSetTSS(dev, 0, D3DTSS_MIPFILTER, D3DTEXF_LINEAR);   // light mips back : crisp at native size, soft only when slightly scaled
    { union { float f; u32 u; } lod; lod.f = 0.20f; dSetTSS(dev, 0, D3DTSS_MIPMAPLODBIAS, lod.u); }   // tiny positive bias -> a faint, even softening (the "light mipmap")
    dSetTSS(dev, 1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    dSetTSS(dev, 1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    // the texture is bound per draw (each size has its own atlas)
}

// One character's advance : the slot's own glyph (32..255), a wide one (its page's metrics), else '?'.
float Font::adv_of(const Slot& S, int c, float scale) const {
    if (is_wide(c)) {
        WPage* P = wide_page(S.em, weight_ >= 600);
        if (P) return wide_adv(*P, (unsigned)c) * scale;            // the metrics table : never costs a bake entry
        c = '?';
    }
    if (c < FIRST || c > LAST) c = '?';
    return S.g[c - FIRST].adv * scale;
}

// APPENDS the glyphs of ONE pass to the shared batch buffer (does not draw) -- the caller binds the texture and
// flushes. Both passes advance the pen over EVERY character, so a wide glyph lands exactly where measure() put it.
void Font::set_rotation(float cx, float cy, float ang) { g_rotOn = true; g_rotX = cx; g_rotY = cy; g_rotC = cosf(ang); g_rotS = sinf(ang); }
void Font::clear_rotation() { g_rotOn = false; }

void Font::emit(u32 dev, const Slot& S, float x, float y, const char* s, float scale, u32 color, float gap, unsigned char prev, int pass) {
    WPage* P = 0; bool hasP = false;                                   // the wide page, fetched at the first wide char
    float yw = 0.0f;
    float penx = x;
    for (const char* p = s; *p; ) {
        int c = upcase(utf8_next(p));
        if (is_wide(c)) {
            if (!hasP) { hasP = true; P = wide_page(S.em, weight_ >= 600); if (P) yw = (S.ascent - (float)P->ascent) * scale; }   // its baseline onto the slot's
            if (P) {
                if (pass == 1) {
                    const WGlyph* G = wide_glyph(*P, (unsigned)c, false);
                    if (G && G->baked) gbuf_quad(dev, penx - 2.0f * scale, y + yw, G->w * scale, G->h * scale, G->u0, G->u1, G->v0, G->v1, color);
                }
                penx += wide_adv(*P, (unsigned)c) * scale; prev = 0;   // BOTH passes advance by the same metric
                continue;
            }
            c = '?';
        }
        if (c < FIRST || c > LAST) c = '?';
        if (gap > 0.0f) { penx += punct_pre(prev, (unsigned char)c, gap); prev = (unsigned char)c; }
        const G& gg = S.g[c - FIRST];
        if (pass == 0 && c != ' ') gbuf_quad(dev, penx, y, gg.w * scale, gg.h * scale, gg.u0, gg.u1, gg.v0, gg.v1, color);
        penx += gg.adv * scale;
    }
}

// stroke + fill of the Latin glyphs (the slot's atlas), then -- only when the string has some -- of the wide ones
void Font::passes(u32 dev, const Slot& S, float x, float y, const char* s, float scale, u32 color, u32 outline, float ow, float gap, unsigned char prev) {
    PERF_SCOPE("gfx.text");   // //aio perf : every glyph pass + the text state block, across all modules (audit P3)
    static const float dx[8] = { -1, 1, 0, 0, -1, -1,  1, 1 };
    static const float dy[8] = {  0, 0, -1, 1, -1,  1, -1, 1 };
    const bool stroke = (outline >> 24) && ow > 0.0f;
    dSetTex(dev, 0, S.tex);                                   // bind the atlas ONCE for its passes
    if (stroke) {
        for (int k = 0; k < 8; ++k) emit(dev, S, x + dx[k] * ow, y + dy[k] * ow, s, scale, outline, gap, prev, 0);
        gbuf_flush(dev);                                      // all 8 offsets -> ONE draw, wholly behind the glyphs
    }
    emit(dev, S, x, y, s, scale, color, gap, prev, 0);
    gbuf_flush(dev);
    // the wide glyphs : bake what is missing FIRST (a full page starts over once, then they are all there)
    bool any = false;
    for (const char* p = s; *p; ) if (is_wide(utf8_next(p))) { any = true; break; }
    if (any) {
        WPage* P = wide_page(S.em, weight_ >= 600);
        if (P) {
            for (int round = 0; round < 2; ++round) {
                bool full = false;
                for (const char* p = s; *p; ) {
                    const int c = utf8_next(p); if (!is_wide(c)) continue;
                    WGlyph* G = wide_glyph(*P, (unsigned)c, true);     // a bake entry only for what is DRAWN
                    if (!G || !wide_bake(dev, *P, *G)) { full = P->full || !G; if (full) break; }
                }
                if (!full) break;
                page_clear(*P);                                // out of room : this string's glyphs first
            }
            if (P->tex) {
                dSetTex(dev, 0, P->tex);
                if (stroke) {
                    for (int k = 0; k < 8; ++k) emit(dev, S, x + dx[k] * ow, y + dy[k] * ow, s, scale, outline, gap, prev, 1);
                    gbuf_flush(dev);
                }
                emit(dev, S, x, y, s, scale, color, gap, prev, 1);
                gbuf_flush(dev);
            }
        }
    }
    dSetTex(dev, 0, 0);                                       // unbind (parity with draw_window/draw_mat ; no stale MODULATE)
}

float Font::measure(const char* s, float size) const {
    if (!s) return 0.0f;
    int si = pickC(size); if (si < 0) return 0.0f;
    const Slot& S = slot_[si];
    float scale = size / S.base, w = 0.0f;
    for (const char* p = s; *p; ) w += adv_of(S, upcase(utf8_next(p)), scale);
    return w;
}

float Font::draw(u32 dev, float x, float y, const char* s, float size, u32 color, u32 outline, float ow) {
    if (!s) return 0.0f;
    int si = pick(dev, size); if (si < 0 || !slot_[si].tex) return 0.0f;
    const Slot& S = slot_[si];
    x = (float)(int)(x + 0.5f); y = (float)(int)(y + 0.5f);   // pixel-snap the origin -> crisp
    const float scale = size / S.base;                        // == 1.0 for an integer `size` (its own atlas)
    passes(dev, S, x, y, s, scale, color, outline, ow, 0.0f, 0);
    float w = 0.0f;
    for (const char* p = s; *p; ) w += adv_of(S, upcase(utf8_next(p)), scale);
    return w;
}

float Font::draw_punct(u32 dev, float x, float y, const char* s, float size, u32 color, u32 outline, float ow, float gap, unsigned char prev) {
    if (!s) return 0.0f;
    int si = pick(dev, size); if (si < 0 || !slot_[si].tex) return 0.0f;
    const Slot& S = slot_[si];
    x = (float)(int)(x + 0.5f); y = (float)(int)(y + 0.5f);
    passes(dev, S, x, y, s, size / S.base, color, outline, ow, gap, prev);
    return 0.0f;   // callers place by their own advance sums (punct_pre included)
}

// cell-top y so the cap/digit ink box is centred on `cy`.
float Font::draw_lv(u32 dev, float x, float cy, const char* s, float size, u32 color, u32 outline, float ow) {
    int si = pick(dev, size); if (si < 0) return 0.0f;
    const Slot& S = slot_[si];
    float y = cy - (S.cap_top + S.cap_h * 0.5f) * (size / S.base);
    return draw(dev, x, y, s, size, color, outline, ow);
}
float Font::draw_c(u32 dev, float cx, float cy, const char* s, float size, u32 color, u32 outline, float ow) {
    float x = cx - measure(s, size) * 0.5f;
    return draw_lv(dev, x, cy, s, size, color, outline, ow);
}

// Bake at `bakeSize` (constant -> one cached atlas) but render at `dispSize` : the display size can be animated
// (pop / pulse / slam) every frame with NO atlas re-bake -- only the emitted quads scale. Centred H + V.
float Font::draw_c_scaled(u32 dev, float cx, float cy, const char* s, float bakeSize, float dispSize, u32 color, u32 outline, float ow) {
    if (!s) return 0.0f;
    int si = pick(dev, bakeSize); if (si < 0 || !slot_[si].tex) return 0.0f;
    const Slot& S = slot_[si];
    const float scale = dispSize / S.base;                    // display scale of the FIXED atlas
    float w = 0.0f;
    for (const char* p = s; *p; ) w += adv_of(S, upcase(utf8_next(p)), scale);
    const float x = (float)(int)(cx - w * 0.5f + 0.5f);
    const float y = (float)(int)(cy - (S.cap_top + S.cap_h * 0.5f) * scale + 0.5f);
    passes(dev, S, x, y, s, scale, color, outline, ow, 0.0f, 0);
    return w;
}

// left-aligned at x, vertically centred on the REAL ink box -> aligns with badge/bars regardless of face. A wide glyph
// counts as a capital (its own ink is not measured).
float Font::draw_lc(u32 dev, float x, float cy, const char* s, float size, u32 color, u32 outline, float ow) {
    if (pick(dev, size) < 0) return 0.0f;                    // bake the size first : ink_v reads the nearest slot
    float top = 0.0f, bot = 0.0f; ink_v(s, size, top, bot);
    return draw(dev, x, cy - (top + bot) * 0.5f, s, size, color, outline, ow);
}

void Font::ink_v(const char* s, float size, float& top, float& bot) const {
    top = 0.0f; bot = 0.0f;
    if (!s) return;
    int si = pickC(size); if (si < 0) return;
    const Slot& S = slot_[si];
    float scale = size / S.base, t = 1e9f, b = -1e9f;
    for (const char* p = s; *p; ) {
        int c = upcase(utf8_next(p));
        if (is_wide(c)) { const float a = S.cap_top * scale, z = (S.cap_top + S.cap_h) * scale; if (a < t) t = a; if (z > b) b = z; continue; }
        if (c < FIRST || c > LAST) c = '?';
        const G& g = S.g[c - FIRST];
        if (c != ' ') { float a = g.it * scale, z = g.ib * scale; if (a < t) t = a; if (z > b) b = z; }
    }
    if (b < t) { t = 0.0f; b = S.cap_h * scale; }
    top = t; bot = b;
}

// centre on the REAL ink bbox of `s` (both axes) -> visually centred for ANY face/size.
float Font::draw_cc(u32 dev, float cx, float cy, const char* s, float size, u32 color, u32 outline, float ow) {
    int si = pick(dev, size); if (si < 0) return 0.0f;
    const Slot& S = slot_[si];
    float scale = size / S.base, penx = 0.0f;
    float left = 1e9f, right = -1e9f, top = 1e9f, bot = -1e9f;
    for (const char* p = s; *p; ) {
        int c = upcase(utf8_next(p));
        if (is_wide(c)) {
            const float a = adv_of(S, c, scale);
            if (penx < left) left = penx; if (penx + a > right) right = penx + a;
            const float t = S.cap_top * scale, b = (S.cap_top + S.cap_h) * scale; if (t < top) top = t; if (b > bot) bot = b;
            penx += a; continue;
        }
        if (c < FIRST || c > LAST) c = '?';
        const G& g = S.g[c - FIRST];
        if (c != ' ') {
            float l = penx + g.il * scale, r = penx + g.ir * scale, t = g.it * scale, b = g.ib * scale;
            if (l < left) left = l; if (r > right) right = r; if (t < top) top = t; if (b > bot) bot = b;
        }
        penx += g.adv * scale;
    }
    if (right < left) { left = 0; right = penx; top = 0; bot = S.cap_h * scale; }
    float X = cx - (left + right) * 0.5f;
    float Y = cy - (top + bot) * 0.5f;
    return draw(dev, X, Y, s, size, color, outline, ow);
}

} // namespace aio
