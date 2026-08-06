// font.cpp -- see font.h. One GDI-baked atlas PER display size (pixel-perfect, no down-scaling).
#include "gfx/font.h"
#include "gfx/draw.h"
#include "gfx/texture.h"
#include "model/paths.h"   // plugin_path_w : runtime-derived fonts dir (gfx infra exception to the layering rule)
#include "windower_debug.h"   // debug::log -- gfx had NO instrumentation at all, so every failure here was mute
#include <windows.h>

namespace aio {

static const int FIRST = 32, LAST = 255;   // printable ASCII + Latin-1 supplement (accents for French)

// Decode the next character from a UTF-8 string and ADVANCE p past it. We bake codepoints 32..255
// (Latin-1), which covers every accented French letter, so source strings are plain UTF-8. ASCII stays
// one byte (English unchanged) ; a 2-byte sequence yields its Latin-1 codepoint ; anything else -> '?'.
static inline int utf8_next(const char*& p) {
    unsigned char b0 = (unsigned char)*p++;
    if (b0 < 0x80) return b0;
    if ((b0 & 0xE0) == 0xC0 && ((unsigned char)*p & 0xC0) == 0x80) {
        unsigned char b1 = (unsigned char)*p++;
        return ((b0 & 0x1F) << 6) | (b1 & 0x3F);
    }
    while (((unsigned char)*p & 0xC0) == 0x80) ++p;
    return '?';
}

static inline int iabs(int v) { return v < 0 ? -v : v; }

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
static inline void gbuf_quad(u32 dev, float x, float y, float w, float h,
                             float u0, float u1, float v0, float v1, u32 c) {
    if (g_gn + 6 > GBUF_CAP) gbuf_flush(dev);           // full -> submit and keep going
    x -= 0.5f; y -= 0.5f;                               // same D3D half-texel rule tquad() applied
    const Vtx tl = { x,     y,     0, 1, c, u0, v0 };
    const Vtx tr = { x + w, y,     0, 1, c, u1, v0 };
    const Vtx bl = { x,     y + h, 0, 1, c, u0, v1 };
    const Vtx br = { x + w, y + h, 0, 1, c, u1, v1 };
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
    int cellH = tm.tmHeight, pad = 2;
    int penx = pad, peny = pad, rowH = cellH + pad;
    const u32* px = (const u32*)bits;
    int inkTop = cellH, inkBot = -1;

    for (int c = FIRST; c <= LAST; ++c) {
        char ch = (char)c;
        int adv; ABC abc;
        if (GetCharABCWidthsA(hdc, c, c, &abc)) adv = abc.abcA + (int)abc.abcB + abc.abcC;
        else { SIZE sz; GetTextExtentPoint32A(hdc, &ch, 1, &sz); adv = sz.cx; }
        if (adv < 1) adv = (int)(em * 0.3f);
        if (penx + adv + pad > AW) { penx = pad; peny += rowH; }
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
        g.u0 = (float)penx / AW;          g.u1 = (float)(penx + adv) / AW;
        g.v0 = (float)peny / AH;          g.v1 = (float)(peny + cellH) / AH;
        g.w  = (float)adv; g.h = (float)cellH; g.adv = (float)adv;

        // per-glyph INK bbox (rel. to this cell's top-left) -> true visual centring (draw_cc)
        {
            int minx = adv, maxx = -1, miny = cellH, maxy = -1;
            for (int yy = 0; yy < cellH; ++yy) {
                const u32* row = px + (peny + yy) * AW;
                for (int xx = penx; xx < penx + adv && xx < AW; ++xx)
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
        penx += adv + pad;
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
                AddFontResourceExW(path, FR_PRIVATE, 0);
            }
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

Font* FontManager::get(const char* face, int weight, bool italic) {
    register_bundled_fonts_once();
    const char* fc = (face && face[0]) ? face : defFace_;
    int w = weight > 0 ? weight : defWeight_;
    for (int i = 0; i < n_; ++i) if (wt_[i] == w && it_[i] == italic && lstrcmpA(face_[i], fc) == 0) return &f_[i];
    if (n_ < MAXF) {
        lstrcpynA(face_[n_], fc, 64); wt_[n_] = w; it_[n_] = italic;
        f_[n_].set_face(fc, w, italic);
        return &f_[n_++];
    }
    return &f_[0];   // pool full -> fall back to the default slot
}
void FontManager::ensure_all(u32 dev)   { for (int i = 0; i < n_; ++i) f_[i].ensure(dev); }
void FontManager::on_device_lost()      { for (int i = 0; i < n_; ++i) f_[i].on_device_lost(); }
void FontManager::dispose()             { for (int i = 0; i < n_; ++i) f_[i].dispose(); }

// ---- render ----
void Font::begin(u32 dev) {
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

// APPENDS the glyphs to the shared batch buffer (does not draw) -- the caller flushes once per pass.
void Font::emit(u32 dev, u32 tex, const G* g, float x, float y, const char* s, float scale, u32 color) {
    (void)tex;   // the atlas is bound ONCE by the caller (same S.tex for every outline + main pass)
    float penx = x;
    for (const char* p = s; *p; ) {
        int c = upcase(utf8_next(p)); if (c < FIRST || c > LAST) c = '?';
        const G& gg = g[c - FIRST];
        if (c != ' ') gbuf_quad(dev, penx, y, gg.w * scale, gg.h * scale, gg.u0, gg.u1, gg.v0, gg.v1, color);
        penx += gg.adv * scale;
    }
}

float Font::measure(const char* s, float size) const {
    if (!s) return 0.0f;
    int si = pickC(size); if (si < 0) return 0.0f;
    const Slot& S = slot_[si];
    float scale = size / S.base, w = 0.0f;
    for (const char* p = s; *p; ) { int c = upcase(utf8_next(p)); if (c < FIRST || c > LAST) c = '?'; w += S.g[c - FIRST].adv * scale; }
    return w;
}

float Font::draw(u32 dev, float x, float y, const char* s, float size, u32 color, u32 outline, float ow) {
    if (!s) return 0.0f;
    int si = pick(dev, size); if (si < 0 || !slot_[si].tex) return 0.0f;
    const Slot& S = slot_[si];
    x = (float)(int)(x + 0.5f); y = (float)(int)(y + 0.5f);   // pixel-snap the origin -> crisp
    float scale = size / S.base;                              // == 1.0 for an integer `size` (its own atlas)
    dSetTex(dev, 0, S.tex);                                   // bind the atlas ONCE (was rebound in every emit pass)
    if ((outline >> 24) && ow > 0.0f) {                       // stroke : 8 offset passes behind
        static const float dx[8] = { -1, 1, 0, 0, -1, -1,  1, 1 };
        static const float dy[8] = {  0, 0, -1, 1, -1,  1, -1, 1 };
        for (int k = 0; k < 8; ++k) emit(dev, S.tex, S.g, x + dx[k] * ow, y + dy[k] * ow, s, scale, outline);
        gbuf_flush(dev);                                      // all 8 offsets -> ONE draw, wholly behind the glyphs
    }
    emit(dev, S.tex, S.g, x, y, s, scale, color);
    gbuf_flush(dev);                                          // main pass -> ONE draw, on top of the stroke
    dSetTex(dev, 0, 0);                                       // unbind the atlas (parity with draw_window/draw_mat ; no stale MODULATE)
    float w = 0.0f;
    for (const char* p = s; *p; ) { int c = upcase(utf8_next(p)); if (c < FIRST || c > LAST) c = '?'; w += S.g[c - FIRST].adv * scale; }
    return w;
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
    for (const char* p = s; *p; ) { int c = upcase(utf8_next(p)); if (c < FIRST || c > LAST) c = '?'; w += S.g[c - FIRST].adv * scale; }
    const float x = (float)(int)(cx - w * 0.5f + 0.5f);
    const float y = (float)(int)(cy - (S.cap_top + S.cap_h * 0.5f) * scale + 0.5f);
    dSetTex(dev, 0, S.tex);
    if ((outline >> 24) && ow > 0.0f) {
        static const float dx[8] = { -1, 1, 0, 0, -1, -1,  1, 1 };
        static const float dy[8] = {  0, 0, -1, 1, -1,  1, -1, 1 };
        for (int k = 0; k < 8; ++k) emit(dev, S.tex, S.g, x + dx[k] * ow, y + dy[k] * ow, s, scale, outline);
        gbuf_flush(dev);
    }
    emit(dev, S.tex, S.g, x, y, s, scale, color);
    gbuf_flush(dev);
    dSetTex(dev, 0, 0);
    return w;
}

// left-aligned at x, vertically centred on the REAL ink box -> aligns with badge/bars regardless of face.
float Font::draw_lc(u32 dev, float x, float cy, const char* s, float size, u32 color, u32 outline, float ow) {
    int si = pick(dev, size); if (si < 0) return 0.0f;
    const Slot& S = slot_[si];
    float scale = size / S.base, top = 1e9f, bot = -1e9f;
    for (const char* p = s; *p; ) {
        int c = upcase(utf8_next(p)); if (c < FIRST || c > LAST) c = '?';
        const G& g = S.g[c - FIRST];
        if (c != ' ') { float t = g.it * scale, b = g.ib * scale; if (t < top) top = t; if (b > bot) bot = b; }
    }
    if (bot < top) { top = 0; bot = S.cap_h * scale; }
    return draw(dev, x, cy - (top + bot) * 0.5f, s, size, color, outline, ow);
}

// centre on the REAL ink bbox of `s` (both axes) -> visually centred for ANY face/size.
float Font::draw_cc(u32 dev, float cx, float cy, const char* s, float size, u32 color, u32 outline, float ow) {
    int si = pick(dev, size); if (si < 0) return 0.0f;
    const Slot& S = slot_[si];
    float scale = size / S.base, penx = 0.0f;
    float left = 1e9f, right = -1e9f, top = 1e9f, bot = -1e9f;
    for (const char* p = s; *p; ) {
        int c = upcase(utf8_next(p)); if (c < FIRST || c > LAST) c = '?';
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
