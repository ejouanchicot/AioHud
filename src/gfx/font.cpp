// font.cpp -- see font.h. One GDI-baked atlas PER display size (pixel-perfect, no down-scaling).
#include "gfx/font.h"
#include "gfx/draw.h"
#include "gfx/texture.h"
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

// ---- bake the glyphs at `em` pixels into slot s, on a right-sized power-of-two atlas. ----
void Font::build(u32 dev, Slot& s, int em) {
    if (em < 7) em = 7; if (em > 36) em = 36;
    const int AW = 512;
    const int AH = (em <= 14) ? 256 : (em <= 26 ? 512 : 1024);   // just tall enough for 224 glyphs at this em

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
        if (peny + cellH > AH) break;
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

int Font::pick(u32 dev, float size) {
    int em = (int)(size + 0.5f); if (em < 7) em = 7; if (em > 36) em = 36;
    for (int i = 0; i < nslot_; ++i) if (slot_[i].em == em) return slot_[i].tex ? i : -1;
    if (nslot_ < NSLOT) {
        build(dev, slot_[nslot_], em);
        if (slot_[nslot_].tex) return nslot_++;
        return -1;
    }
    int best = -1, bd = 99999;                       // pool full -> reuse the nearest baked size
    for (int i = 0; i < nslot_; ++i) if (slot_[i].tex) { int d = iabs(slot_[i].em - em); if (d < bd) { bd = d; best = i; } }
    return best;
}

int Font::pickC(float size) const {
    int em = (int)(size + 0.5f); if (em < 7) em = 7; if (em > 36) em = 36;
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
void Font::dispose()        { for (int i = 0; i < nslot_; ++i) if (slot_[i].tex) release_texture(slot_[i].tex); nslot_ = 0; }

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
    static bool done = false; if (done) return; done = true;
    const wchar_t* dir = L"D:\\Windower Tetsouo\\plugins\\_aiohud_re\\assets\\fonts\\";
    wchar_t pat[600]; wsprintfW(pat, L"%s*.*", dir);
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
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

void Font::emit(u32 dev, u32 tex, const G* g, float x, float y, const char* s, float scale, u32 color) {
    (void)tex;   // the atlas is bound ONCE by the caller (same S.tex for every outline + main pass)
    float penx = x;
    for (const char* p = s; *p; ) {
        int c = upcase(utf8_next(p)); if (c < FIRST || c > LAST) c = '?';
        const G& gg = g[c - FIRST];
        if (c != ' ') tquad(dev, penx, y, gg.w * scale, gg.h * scale, gg.u0, gg.u1, gg.v0, gg.v1, color, color);
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
    }
    emit(dev, S.tex, S.g, x, y, s, scale, color);
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
