// window.cpp -- see window.h. FFXI window skin re-composed as a 9-slice.
#include "gfx/window.h"
#include "gfx/draw.h"      // tquad
#include "gfx/texture.h"   // load_raw_texture, release_texture
#include <cstdio>
#include <cstdlib>

namespace aio {

static const char* ASSET_BASE = "D:\\Windower Tetsouo\\plugins\\_aiohud_re\\assets\\window";

// Border colour for a theme = the bg's average colour LIGHTENED toward white (a clean highlight rim
// of the theme's own hue). The texture only provides the border SHAPE/alpha ; this is the flat fill.
static u32 border_from_bg(const char* bgRawPath) {
    FILE* f = fopen(bgRawPath, "rb"); if (!f) return 0xFFFFFFFF;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); return 0xFFFFFFFF; }
    unsigned char* buf = (unsigned char*)malloc((size_t)n);
    long got = buf ? (long)fread(buf, 1, (size_t)n, f) : 0; fclose(f);
    unsigned long rs = 0, gs = 0, bs = 0, cnt = 0;
    for (long i = 0; i + 3 < got; i += 4) { bs += buf[i]; gs += buf[i + 1]; rs += buf[i + 2]; ++cnt; }   // BGRA
    free(buf);
    if (!cnt) return 0xFFFFFFFF;
    int R = (int)(rs / cnt), G = (int)(gs / cnt), B = (int)(bs / cnt);
    R += (255 - R) * 55 / 100; G += (255 - G) * 55 / 100; B += (255 - B) * 55 / 100;   // ~55% toward white
    return 0xFF000000u | ((u32)R << 16) | ((u32)G << 8) | (u32)B;
}

// folder names under assets/window (converted from the game DDS by scripts/gen_window_skin.sh).
// _Bis = the user's modified variants. //aio menu N selects the N-th (1-based).
static const char* THEMES[] = {
    "14", "15", "16", "17", "19", "19_Bis", "20", "20_Bis", "21",   // 18 + 18_Bis + 21_Bis removed (bugged)
};
int         window_theme_count()      { return (int)(sizeof(THEMES) / sizeof(THEMES[0])); }
const char* window_theme_name(int i)  { return (i >= 0 && i < window_theme_count()) ? THEMES[i] : nullptr; }

bool WindowSkin::load(u32 dev, const char* themeName) {
    if (!valid_ptr(dev) || !themeName) return false;
    char p[260];
    sprintf(p, "%s\\%s\\corner.raw", ASSET_BASE, themeName); corner = load_raw_texture(dev, p, 32, 32);
    sprintf(p, "%s\\%s\\hframe.raw", ASSET_BASE, themeName); hframe = load_raw_texture(dev, p, 32, 32);
    sprintf(p, "%s\\%s\\vframe.raw", ASSET_BASE, themeName); vframe = load_raw_texture(dev, p, 32, 32);
    sprintf(p, "%s\\%s\\bg.raw",     ASSET_BASE, themeName); bg     = load_raw_texture(dev, p, 128, 128);
    borderColor = border_from_bg(p);     // derive the border colour from this theme's bg
    return ready();
}
void WindowSkin::on_device_lost() { corner = hframe = vframe = bg = 0; }
void WindowSkin::dispose() {
    release_texture(corner); release_texture(hframe); release_texture(vframe); release_texture(bg);
    corner = hframe = vframe = bg = 0;
}

// 9-slice : tiled bg centre + 4 edges (hframe top/bottom halves, vframe left/right halves, tiled
// along their length) + 4 corners (the corner sheet's 4 quadrants). All MODULATEd by `tint`.
void draw_window(u32 dev, const WindowSkin& s, float x, float y, float w, float h, u32 tint, float scale, bool openBottom, bool drawBorder) {
    if (!s.ready() || w <= 0 || h <= 0) return;
    (void)scale;                                   // textures are drawn 1:1 (NATIVE px) and TILED -> no stretch/deformation, like the game
    const float bgT   = 128.0f;                    // bg native tile size
    const float edgeT = 32.0f;                     // edge native tile length
    float b = 16.0f;                               // border/corner thickness = native corner-quadrant size
    if (b * 2.0f > w) b = w * 0.5f;                // clamp for tiny boxes
    if (b * 2.0f > h) b = h * 0.5f;

    dSetVS(dev, FVF_XYZRHW_DIFFUSE_TEX1);
    dSetRS(dev, D3DRS_ZENABLE, 0);           // full 2D pipeline (like Font::begin) so the skin can't be
    dSetRS(dev, D3DRS_CULLMODE, D3DCULL_NONE); // culled / z-rejected by the game's ambient state if it is
    dSetRS(dev, D3DRS_LIGHTING, 0);          // the FIRST HUD primitive of the frame
    dSetRS(dev, D3DRS_ALPHATESTENABLE, 0);
    dSetRS(dev, D3DRS_FOGENABLE, 0);
    dSetRS(dev, D3DRS_BLENDOP, D3DBLENDOP_ADD);
    dSetRS(dev, D3DRS_ALPHABLENDENABLE, 1);
    dSetRS(dev, D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    dSetTSS(dev, 0, D3DTSS_COLOROP, D3DTOP_MODULATE); dSetTSS(dev, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE); dSetTSS(dev, 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    dSetTSS(dev, 0, D3DTSS_ALPHAOP, D3DTOP_MODULATE); dSetTSS(dev, 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE); dSetTSS(dev, 0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    dSetTSS(dev, 0, D3DTSS_MINFILTER, D3DTEXF_LINEAR); dSetTSS(dev, 0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR); dSetTSS(dev, 0, D3DTSS_MIPFILTER, D3DTEXF_NONE);

    // bg fills the WHOLE rect, tiled at NATIVE size (no stretch). The edge/corner pieces overlay it,
    // so their transparent parts reveal the BG -- not the game -- and there's no gap to the border.
    dSetTex(dev, 0, s.bg);
    dSetTSS(dev, 0, D3DTSS_ADDRESSU, D3DTADDRESS_WRAP); dSetTSS(dev, 0, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
    tquad(dev, x, y, w, h, 0.0f, w / bgT, 0.0f, h / bgT, tint, tint);

    if (!drawBorder) { dSetTex(dev, 0, 0); return; }   // background only -> skip the frame edges + corners

    // BORDERS : paint the texture SHAPE (its alpha = rounded corners + line) in ONE flat colour ->
    // no bevel/gradient, identical on every theme. COLOROP SELECTARG2 = take the DIFFUSE colour ;
    // alpha still comes from the texture. Change WINDOW_BORDER_COLOR to recolour all borders.
    const u32 bc = s.borderColor;
    dSetTSS(dev, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);

    // The skin textures have a 1px transparent margin on their outer edge -> draw the border pieces
    // on a rect OUTSET by `ov` so the border line lands flush on the box edge (not a px inside).
    const float ov = 1.0f;
    const float rx = x - ov, ry = y - ov, rw = w + 2.0f * ov, rh = h + 2.0f * ov;
    const float iw = rw - 2.0f * b;
    const float ih = openBottom ? (rh - b) : (rh - 2.0f * b);   // side edges run to the bottom when openBottom

    (void)edgeT;
    // Use only the TOP-LEFT region of each piece, MIRRORED (flip UVs) for the opposite side, and
    // STRETCH edges along their length (not tiled) -> a clean continuous frame, no repeated motif.
    // top / bottom edges : hframe top half (v 0..0.5) ; bottom = same, flipped vertically
    if (iw > 0) {
        dSetTex(dev, 0, s.hframe);
        dSetTSS(dev, 0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP); dSetTSS(dev, 0, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
        tquad(dev, rx + b, ry,          iw, b, 0.0f, 1.0f, 0.0f, 0.5f, bc, bc);   // top
        if (!openBottom)
            tquad(dev, rx + b, ry + rh - b, iw, b, 0.0f, 1.0f, 0.5f, 0.0f, bc, bc);   // bottom = top FLIPPED V
    }
    // left / right edges : vframe left half (u 0..0.5) ; right = same, flipped horizontally
    if (ih > 0) {
        dSetTex(dev, 0, s.vframe);
        dSetTSS(dev, 0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP); dSetTSS(dev, 0, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
        tquad(dev, rx,          ry + b, b, ih, 0.0f, 0.5f, 0.0f, 1.0f, bc, bc);   // left
        tquad(dev, rx + rw - b, ry + b, b, ih, 0.5f, 0.0f, 0.0f, 1.0f, bc, bc);   // right = left FLIPPED H
    }
    // corners : the TOP-LEFT quadrant only, mirrored to the other 3 corners
    dSetTex(dev, 0, s.corner);
    dSetTSS(dev, 0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP); dSetTSS(dev, 0, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
    tquad(dev, rx,          ry,          b, b, 0.0f, 0.5f, 0.0f, 0.5f, bc, bc);   // top-left  (as-is)
    tquad(dev, rx + rw - b, ry,          b, b, 0.5f, 0.0f, 0.0f, 0.5f, bc, bc);   // top-right (flip H)
    if (!openBottom) {
        tquad(dev, rx,          ry + rh - b, b, b, 0.0f, 0.5f, 0.5f, 0.0f, bc, bc);   // bottom-left  (flip V)
        tquad(dev, rx + rw - b, ry + rh - b, b, b, 0.5f, 0.0f, 0.5f, 0.0f, bc, bc);   // bottom-right (flip both)
    }

    dSetTex(dev, 0, 0);
}

} // namespace aio
