// window.cpp -- see window.h. FFXI window skin re-composed as a 9-slice.
#include "gfx/window.h"
#include "gfx/draw.h"      // tquad
#include "gfx/texture.h"   // load_raw_texture, release_texture
#include "model/paths.h"   // plugin_path : runtime-derived asset base (gfx infra exception to the layering rule)
#include "windower_debug.h"   // debug::log -- a skin that never loads must not be silent
#include <cstdio>
#include <cstdlib>

namespace aio {

static const char* ASSET_BASE() { static char b[260]; if (!b[0]) plugin_path(b, 260, "assets\\window"); return b; }   // runtime-derived (was a hardcoded dev path)

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
static const int TEX_N = (int)(sizeof(THEMES) / sizeof(THEMES[0]));

// ---- PROCEDURAL box themes : FAMILIES (styles) x a wide HUE palette. Family 0 = "FFXI" (the game
// textures above ; its variant is the theme number). Families 1.. are drawn from primitives : each
// renders ANY hue in its own look (base tint + decoration + border/relief). Always DARK -> the party
// text (light + outlined) stays readable. kind = decoration (0 flat,1 vignette,2 top-bar,3 sheen,
// 4 glow-border,5 left-bar) ; bstyle = border (0 flat,1 relief,2 engraved,3 double,4 glossy). ----
struct ProcTheme { u32 top, bot, border, accent; int kind; int bstyle; };

static const char* BOX_FAMILIES[] = { "FFXI", "Modern", "Medieval", "Heroic", "Neon", "Frost", "Royal" };
static const int BOX_FAM_N  = (int)(sizeof(BOX_FAMILIES) / sizeof(BOX_FAMILIES[0]));
static const int PROC_FAM_N = BOX_FAM_N - 1;   // procedural families (all but FFXI)

// a wide hue palette (vivid wheel + neutrals) -> "as many colours as possible" per family.
static const u32 BOX_HUES[] = {
    0xFFE0403A,0xFFE05A2E,0xFFE0802E,0xFFE0A22E,0xFFE0C22E,0xFFE0E23A,0xFFB6E03A,0xFF7AE03A,0xFF3AE04A,0xFF3AE086,
    0xFF2FD4C6,0xFF2FD0E0,0xFF2FB8E0,0xFF3A9AE0,0xFF3A7AE0,0xFF3A5AE0,0xFF5A4AE0,0xFF7A4AE0,0xFF9A4AE0,0xFFC24AE0,
    0xFFE04AC0,0xFFE04A8A,0xFFE04A6A,0xFFC0603A,0xFFE8E8EC,0xFFB8C0C8,0xFF8A94A0,0xFF5A6470,0xFF9A8A6A,0xFF6A7A5A,
};
static const int BOX_HUE_N = (int)(sizeof(BOX_HUES) / sizeof(BOX_HUES[0]));
static const int PROC_N    = PROC_FAM_N * BOX_HUE_N;   // total procedural themes (families x hues)

int  window_theme_count()      { return TEX_N + PROC_N; }
bool window_theme_is_proc(int i){ return i >= TEX_N && i < TEX_N + PROC_N; }
int  window_tex_theme_count()  { return TEX_N; }
int  box_family_count()        { return BOX_FAM_N; }
const char* box_family_name(int i) { return (i >= 0 && i < BOX_FAM_N) ? BOX_FAMILIES[i] : nullptr; }
int  box_hue_count()           { return BOX_HUE_N; }
u32  box_hue_color(int i)      { return (i >= 0 && i < BOX_HUE_N) ? BOX_HUES[i] : 0xFF808080; }

// flat theme index <-> (family, variant). family 0 = FFXI (variant = texture idx) ; family>0 = procedural (variant = hue idx).
int window_theme_family(int idx)  { if (idx < TEX_N) return 0; int p = idx - TEX_N; if (p < 0 || p >= PROC_N) return 0; return p / BOX_HUE_N + 1; }
int window_theme_variant(int idx) { if (idx < TEX_N) return idx; int p = idx - TEX_N; if (p < 0 || p >= PROC_N) return 0; return p % BOX_HUE_N; }
int window_theme_index(int family, int variant) {
    if (family <= 0) { if (variant < 0) variant = 0; if (variant >= TEX_N) variant = TEX_N - 1; return variant; }
    if (family > PROC_FAM_N) family = PROC_FAM_N;
    if (variant < 0) variant = 0; if (variant >= BOX_HUE_N) variant = BOX_HUE_N - 1;
    return TEX_N + (family - 1) * BOX_HUE_N + variant;
}
const char* window_theme_name(int i) {
    if (i < 0 || i >= TEX_N + PROC_N) return nullptr;
    if (i < TEX_N) return THEMES[i];
    return BOX_FAMILIES[window_theme_family(i)];   // family name (config shows a colour grid, so per-hue names aren't needed)
}

bool WindowSkin::load(u32 dev, const char* themeName) {
    if (!valid_ptr(dev) || !themeName) return false;
    // ASSET_BASE() can legitimately return 259 chars (plugin_path caps at 260), and each build appends ~12 more.
    // sprintf here was unbounded -> a deep install path (Program Files + a long user folder, the exact shape that
    // has bitten the NA tester before) would smash this stack buffer at load time and on every theme change.
    // ATOMIC : load into LOCALS, commit all four only if all four succeeded. The old code assigned each handle in
    // place, so (a) a caller that RETRIES load() while some handles are already set (the party path calls load()
    // every frame until ready(), hud.cpp) leaked the previous handles on each partial load, and (b) a partial load
    // left the object half-built yet ready()==false forever if the caller latched a "tried" marker. Now a partial
    // miss releases what it got and keeps the OLD handles intact for the next retry -- no leak, no half-state.
    char p[260]; u32 c = 0, hf = 0, vf = 0, b = 0;
    _snprintf(p, sizeof(p), "%s\\%s\\corner.raw", ASSET_BASE(), themeName); p[sizeof(p) - 1] = 0; c  = load_raw_texture(dev, p, 32, 32);
    _snprintf(p, sizeof(p), "%s\\%s\\hframe.raw", ASSET_BASE(), themeName); p[sizeof(p) - 1] = 0; hf = load_raw_texture(dev, p, 32, 32);
    _snprintf(p, sizeof(p), "%s\\%s\\vframe.raw", ASSET_BASE(), themeName); p[sizeof(p) - 1] = 0; vf = load_raw_texture(dev, p, 32, 32);
    _snprintf(p, sizeof(p), "%s\\%s\\bg.raw",     ASSET_BASE(), themeName); p[sizeof(p) - 1] = 0; b  = load_raw_texture(dev, p, 128, 128);
    if (!(c && hf && vf && b)) {   // partial load -> release the fragments, keep whatever we already had, retry later
        release_texture(c); release_texture(hf); release_texture(vf); release_texture(b);
        // ...and SAY SO, once per theme. The callers (hud.cpp, player.cpp, target.cpp, box_style.cpp) all retry
        // on `!ready()`, i.e. EVERY FRAME -- so a missing or unreadable window\<theme>\ folder meant four failed
        // CreateFileA per frame, forever, with no line anywhere. The visible symptom is a box with no background,
        // and //aio self_check reported it as "(none/proc)" -- the same string it prints for a genuinely
        // procedural theme, so the one diagnostic available said "working as intended".
        ++failN;
        if (failN == 1)
            windower::debug::log("SKIN '%s' FAILED to load (corner=%d hframe=%d vframe=%d bg=%d) -- base '%s'. Boxes draw with NO skin ; retrying.",
                                 themeName, c ? 1 : 0, hf ? 1 : 0, vf ? 1 : 0, b ? 1 : 0, ASSET_BASE());
        return false;
    }
    if (failN) { windower::debug::log("SKIN '%s' loaded after %d failed attempt(s)", themeName, failN); failN = 0; }
    release_texture(corner); release_texture(hframe); release_texture(vframe); release_texture(bg);   // swap in atomically
    corner = c; hframe = hf; vframe = vf; bg = b;
    borderColor = border_from_bg(p);     // derive the border colour from this theme's bg
    return true;
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
    // alpha = DIFFUSE(tint) x TEXTURE(shape). The diffuse MUST be ARG1 : with tex in ARG1 / diffuse in ARG2 the
    // diffuse alpha was ignored on this device (came out opaque) -> the tint never faded the FFXI skin box. Swapping
    // args keeps the frame's texture SHAPE while honouring the tint's transparency.
    dSetTSS(dev, 0, D3DTSS_ALPHAOP, D3DTOP_MODULATE); dSetTSS(dev, 0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE); dSetTSS(dev, 0, D3DTSS_ALPHAARG2, D3DTA_TEXTURE);
    dSetTSS(dev, 0, D3DTSS_MINFILTER, D3DTEXF_LINEAR); dSetTSS(dev, 0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR); dSetTSS(dev, 0, D3DTSS_MIPFILTER, D3DTEXF_NONE);
    dSetTSS(dev, 1, D3DTSS_COLOROP, D3DTOP_DISABLE);   // isolate from the game's texture-stage-1 state : a stage-1
                                                       // alpha-op left by the game overrode stage 0's alpha -> the
                                                       // tint's transparency was ignored (FFXI-skin box always opaque).

    // bg fills the WHOLE rect, tiled at NATIVE size (no stretch). The edge/corner pieces overlay it,
    // so their transparent parts reveal the BG -- not the game -- and there's no gap to the border.
    dSetTex(dev, 0, s.bg);
    dSetTSS(dev, 0, D3DTSS_ADDRESSU, D3DTADDRESS_WRAP); dSetTSS(dev, 0, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
    tquad(dev, x, y, w, h, 0.0f, w / bgT, 0.0f, h / bgT, tint, tint);

    if (!drawBorder) { dSetTex(dev, 0, 0); return; }   // background only -> skip the frame edges + corners

    // BORDERS : paint the texture SHAPE (its alpha = rounded corners + line) in ONE flat colour ->
    // no bevel/gradient, identical on every theme. COLOROP SELECTARG2 = take the DIFFUSE colour ;
    // alpha still comes from the texture. The border colour is derived per theme by border_from_bg into s.borderColor.
    // carry the tint's ALPHA onto the border so the FFXI-skin FRAME fades with the Transparency slider too
    // (its RGB stays the theme's border colour ; only opacity follows `tint`). Was : border always opaque
    // while only the bg faded -> a transparent FFXI box still had a solid frame.
    const u32 ta  = (tint >> 24) & 0xFFu;
    const u32 bcA = ((((s.borderColor >> 24) & 0xFFu) * ta) + 127u) / 255u;
    const u32 bc  = (s.borderColor & 0x00FFFFFFu) | (bcA << 24);
    dSetTSS(dev, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
    // FRAME alpha comes from the TEXTURE SHAPE (rounded corners + line). The bg fades via the diffuse alpha, but the
    // frame must keep its texture alpha or the corners become solid squares -- and this device can't do
    // texture x diffuse for alpha (the 2nd modulate arg is ignored). So : bg fades, frame stays crisp (opaque).
    dSetTSS(dev, 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1); dSetTSS(dev, 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);

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

// colour-only 2D pipeline state (this may be the FIRST HUD primitive of the frame -> set it all).
static void proc_state(u32 dev) {
    dSetVS(dev, FVF_XYZRHW_DIFFUSE);
    dSetRS(dev, D3DRS_ZENABLE, 0); dSetRS(dev, D3DRS_CULLMODE, D3DCULL_NONE); dSetRS(dev, D3DRS_LIGHTING, 0);
    dSetRS(dev, D3DRS_ALPHATESTENABLE, 0); dSetRS(dev, D3DRS_FOGENABLE, 0); dSetRS(dev, D3DRS_BLENDOP, D3DBLENDOP_ADD);
    dSetRS(dev, D3DRS_ALPHABLENDENABLE, 1); dSetRS(dev, D3DRS_SRCBLEND, D3DBLEND_SRCALPHA); dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    dSetTex(dev, 0, 0);
    dSetTSS(dev, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1); dSetTSS(dev, 0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    dSetTSS(dev, 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1); dSetTSS(dev, 0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    dSetTSS(dev, 1, D3DTSS_COLOROP, D3DTOP_DISABLE);
}

// add `amt` (-255..255) to each RGB channel, clamped ; keeps alpha. For bevel highlight / shadow tones.
static u32 lighten(u32 c, int amt) {
    int r = (int)((c >> 16) & 0xFF) + amt, g = (int)((c >> 8) & 0xFF) + amt, b = (int)(c & 0xFF) + amt;
    if (r < 0) r = 0; if (r > 255) r = 255; if (g < 0) g = 0; if (g > 255) g = 255; if (b < 0) b = 0; if (b > 255) b = 255;
    return (c & 0xFF000000u) | ((u32)r << 16) | ((u32)g << 8) | (u32)b;
}

// procedural-box OPACITY (0..255) : set by draw_proc_window from the tint alpha, applied via pA() to the interior
// fills AND the frame/border colours so the WHOLE box (not just the interior) honours a Transparency setting.
// 255 = opaque -> pA is a no-op (the Party box + any solid box are unchanged).
static u32 g_procA = 255;
static inline u32 pA(u32 c) { if (g_procA >= 255) return c; return ((((c >> 24) & 0xFFu) * g_procA / 255u) << 24) | (c & 0x00FFFFFFu); }

// a styled box BORDER : 0 flat, 1 bevel-raised (light top/left, dark bottom/right = relief),
// 2 bevel-inset (engraved), 3 double filet, 4 glossy (flat frame + a reflective sheen on top).
static void draw_proc_border(u32 dev, float x, float y, float w, float h, u32 bc, int style, bool openBottom) {
    const float t = 2.0f;
    if (style == 1 || style == 2) {                              // BEVEL : relief / engraved
        const u32 hi = pA(lighten(bc, 78)), lo = pA(lighten(bc, -74));
        const u32 tl = (style == 1) ? hi : lo, br = (style == 1) ? lo : hi;   // raised: light TL ; inset: dark TL
        grad_quad(dev, x, y, w, t, tl, tl, tl, tl);                                  // top
        grad_quad(dev, x, y, t, h, tl, tl, tl, tl);                                  // left
        grad_quad(dev, x + w - t, y, t, h, br, br, br, br);                          // right
        if (!openBottom) grad_quad(dev, x, y + h - t, w, t, br, br, br, br);         // bottom
        return;
    }
    // flat frame (base for styles 0, 3, 4)
    const u32 b = pA(bc);
    grad_quad(dev, x, y, w, t, b, b, b, b);
    grad_quad(dev, x, y, t, h, b, b, b, b);
    grad_quad(dev, x + w - t, y, t, h, b, b, b, b);
    if (!openBottom) grad_quad(dev, x, y + h - t, w, t, b, b, b, b);
    if (style == 3) {                                            // DOUBLE : a thin faint inner rule
        const float g = 4.0f; const u32 ic = pA((0x99u << 24) | (bc & 0x00FFFFFF));
        grad_quad(dev, x + g, y + g, w - 2 * g, 1.0f, ic, ic, ic, ic);
        grad_quad(dev, x + g, y + g, 1.0f, h - 2 * g, ic, ic, ic, ic);
        grad_quad(dev, x + w - g - 1, y + g, 1.0f, h - 2 * g, ic, ic, ic, ic);
        if (!openBottom) grad_quad(dev, x + g, y + h - g - 1, w - 2 * g, 1.0f, ic, ic, ic, ic);
    } else if (style == 4) {                                     // GLOSSY : additive reflective sheen on the top frame + inner
        dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_ONE);
        grad_quad(dev, x, y, w, t, (0x58u << 24) | 0x00FFFFFF, (0x10u << 24) | 0x00FFFFFF, (0x58u << 24) | 0x00FFFFFF, (0x10u << 24) | 0x00FFFFFF);   // bright-left -> dim-right gloss on the top edge
        grad_quad(dev, x + t, y + t, w - 2 * t, 6.0f, (0x22u << 24) | 0x00FFFFFF, (0x22u << 24) | 0x00FFFFFF, 0x00FFFFFF, 0x00FFFFFF);                 // inner top reflection fading down
        dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    }
}

// lerp two ARGB colours (keeps a's alpha). For deriving a family's tones from a hue.
static u32 cmix(u32 a, u32 b, float f) {
    int ar = (a >> 16) & 0xFF, ag = (a >> 8) & 0xFF, ab = a & 0xFF;
    int br = (b >> 16) & 0xFF, bg = (b >> 8) & 0xFF, bb = b & 0xFF;
    int r = (int)(ar + (br - ar) * f + 0.5f), g = (int)(ag + (bg - ag) * f + 0.5f), bl = (int)(ab + (bb - ab) * f + 0.5f);
    if (r < 0) r = 0; if (r > 255) r = 255; if (g < 0) g = 0; if (g > 255) g = 255; if (bl < 0) bl = 0; if (bl > 255) bl = 255;
    return (a & 0xFF000000u) | ((u32)r << 16) | ((u32)g << 8) | (u32)bl;
}

// additive on / back to normal alpha (helpers for the bespoke family art)
static inline void add_on(u32 dev)  { dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_ONE); }
static inline void add_off(u32 dev) { dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA); }
// draw a thick MATERIAL frame (a beveled plate) : light top/left, dark bottom/right + a bright rim
// line. `mat` is the metal/wood/gold tone. Used by families whose border is a real material, not a line.
static void plate_frame(u32 dev, float x, float y, float w, float h, u32 mat, float t, bool openBottom) {
    const u32 hi = pA(lighten(mat, 66)), lo = pA(lighten(mat, -66)), m = pA(mat), rim = pA(lighten(mat, 100));
    grad_quad(dev, x, y, w, t, hi, hi, m, m);                        // top : bright -> mid (rounded metal)
    grad_quad(dev, x, y, t, h, hi, m, hi, m);                       // left
    grad_quad(dev, x + w - t, y, t, h, m, lo, m, lo);              // right
    if (!openBottom) grad_quad(dev, x, y + h - t, w, t, m, m, lo, lo);   // bottom
    grad_quad(dev, x, y, w, 1.0f, rim, rim, rim, rim);              // crisp top rim highlight
}

// ---- material texture cache (procedural, generated once, forgotten on device loss like the skin) ----
static u32 g_matWood = 0, g_matFrost = 0, g_matVelvet = 0, g_matMetal = 0;
static void ensure_materials(u32 dev) {
    if (!g_matWood)   g_matWood   = make_wood(dev, 256, 256);
    if (!g_matFrost)  g_matFrost  = make_frost(dev, 256, 256);
    if (!g_matVelvet) g_matVelvet = make_velvet(dev, 256, 256);
    if (!g_matMetal)  g_matMetal  = make_metal(dev, 256, 256);
}
void window_materials_reset() { g_matWood = g_matFrost = g_matVelvet = g_matMetal = 0; }   // forget (device loss ; recreated lazily)
void window_materials_dispose() {   // RELEASE (device still alive -> //unload) : else the 4 256^2 textures leak per load cycle
    release_texture(g_matWood); release_texture(g_matFrost); release_texture(g_matVelvet); release_texture(g_matMetal);
    g_matWood = g_matFrost = g_matVelvet = g_matMetal = 0;
}

// draw a MATERIAL texture TILED (WRAP) at native `scale` px, coloured by `tint` (MODULATE ; opaque).
// Tiling (not stretching) keeps the grain/brush at a natural size -> no stretched/cut look. The textures
// are authored to tile seamlessly (integer fbm frequencies).
static void draw_mat(u32 dev, u32 tex, float x, float y, float w, float h, u32 tint, float scale) {
    if (!tex) return;
    tint = pA(tint);                 // honour the procedural-box interior opacity (translucent background)
    dSetVS(dev, FVF_XYZRHW_DIFFUSE_TEX1);
    dSetRS(dev, D3DRS_ZENABLE, 0); dSetRS(dev, D3DRS_CULLMODE, D3DCULL_NONE); dSetRS(dev, D3DRS_LIGHTING, 0);
    dSetRS(dev, D3DRS_ALPHATESTENABLE, 0); dSetRS(dev, D3DRS_FOGENABLE, 0); dSetRS(dev, D3DRS_BLENDOP, D3DBLENDOP_ADD);
    dSetRS(dev, D3DRS_ALPHABLENDENABLE, 1); dSetRS(dev, D3DRS_SRCBLEND, D3DBLEND_SRCALPHA); dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    dSetTex(dev, 0, tex);
    dSetTSS(dev, 0, D3DTSS_COLOROP, D3DTOP_MODULATE);   dSetTSS(dev, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE); dSetTSS(dev, 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    dSetTSS(dev, 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1); dSetTSS(dev, 0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);   // alpha = the tint alpha ONLY (material texture is opaque) -> honours Transparency, like the grad_quad path
    dSetTSS(dev, 1, D3DTSS_COLOROP, D3DTOP_DISABLE);   // stop after stage 0 (proc_state does this ; without it a stale stage-1 op can force alpha opaque)
    dSetTSS(dev, 0, D3DTSS_ADDRESSU, D3DTADDRESS_WRAP); dSetTSS(dev, 0, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
    dSetTSS(dev, 0, D3DTSS_MINFILTER, D3DTEXF_LINEAR); dSetTSS(dev, 0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR); dSetTSS(dev, 0, D3DTSS_MIPFILTER, D3DTEXF_NONE);
    if (scale < 1.0f) scale = 1.0f;
    tquad(dev, x, y, w, h, 0.0f, w / scale, 0.0f, h / scale, tint, tint);
    dSetTex(dev, 0, 0);
}

// Each family is a real MATERIAL : a procedural texture (wood / frost / velvet / brushed metal) coloured
// per hue, framed by a matching border. Interiors are kept dark enough that the party text reads.
static void draw_box_family(u32 dev, int family, u32 H, float x, float y, float w, float h, bool openBottom, bool drawBorder, float lum) {
    ensure_materials(dev);
    // Honour the box's Transparency on EVERYTHING : auto-wrap every interior + border grad_quad/soft_blob colour in
    // pA() (g_procA). #undef at function end. draw_mat/plate_frame/draw_proc_border already pA internally.
    #define grad_quad(d_, X_, Y_, W_, H_, a_, b_, c_, e_) grad_quad(d_, X_, Y_, W_, H_, pA(a_), pA(b_), pA(c_), pA(e_))
    #define soft_blob(d_, X_, Y_, W_, H_, a_) soft_blob(d_, X_, Y_, W_, H_, pA(a_))
    switch (family) {
    default: {   // ===== MODERN : brushed dark glass -- subtle brushed base + a top reflection + glossy edge =====
        draw_mat(dev, g_matMetal, x, y, w, h, cmix(0xFF15181D, H, 0.10f), 220.0f);       // faint dark brushed surface
        proc_state(dev);
        add_on(dev);
        grad_quad(dev, x, y, w, h * 0.46f, (0x16u << 24) | 0x00FFFFFF, (0x16u << 24) | 0x00FFFFFF, 0x00FFFFFF, 0x00FFFFFF);   // glass reflection
        add_off(dev);
        if (drawBorder) draw_proc_border(dev, x, y, w, h, cmix(H, 0xFF4A525C, 0.30f), 4, openBottom);   // glossy edge
        break; }
    case 2: {   // ===== MEDIEVAL : real WOOD grain interior + a steel-plate frame =====
        draw_mat(dev, g_matWood, x, y, w, h, cmix(0xFF4E3420, H, 0.15f), 200.0f);       // brown wood, lightly hue-tinted (fixed scale -> never squished)
        proc_state(dev);
        add_on(dev); soft_blob(dev, x + w * 0.5f, y + h * 0.16f, w * 0.6f, h * 0.5f, (0x0Eu << 24) | 0x00E0A030); add_off(dev);   // faint torch warmth
        grad_quad(dev, x, y, w, h * 0.14f, 0x38000000, 0x38000000, 0x00000000, 0x00000000);             // top depth
        if (!openBottom) grad_quad(dev, x, y + h * 0.86f, w, h * 0.14f, 0x00000000, 0x00000000, 0x38000000, 0x38000000);
        if (drawBorder) {                                                                               // THICK matte dark-iron band (chunky, distinct from the other frames)
            plate_frame(dev, x, y, w, h, 0xFF4A4E54, 4.0f, openBottom);
            grad_quad(dev, x + 4.0f, y + 4.0f, w - 8.0f, 1.0f, 0x50000000, 0x50000000, 0x50000000, 0x50000000);   // inner recessed shadow line
        }
        break; }
    case 3: {   // ===== HEROIC : a polished steel PLATE (broad sheen) + bold hue accent bars + bright frame =====
        proc_state(dev);
        const u32 s1 = cmix(0xFF2A2E34, H, 0.14f), s2 = cmix(0xFF1B1F25, H, 0.10f);   // dark polished steel, faint hue
        grad_quad(dev, x, y, w, h, s1, s1, s2, s2);
        add_on(dev);
        soft_blob(dev, x + w * 0.34f, y + h * 0.30f, w * 0.6f, h * 0.75f, (0x16u << 24) | 0x00FFFFFF);   // ONE broad polished-metal sheen (upper-left)
        add_off(dev);
        grad_quad(dev, x, y, w, 2.0f, H, H, H, H);                                                       // bold hue accent bars
        if (!openBottom) grad_quad(dev, x, y + h - 2.0f, w, 2.0f, H, H, H, H);
        if (drawBorder) {                                                                               // bright polished-steel bevel + a bright hue accent line just inside (shiny, distinct)
            plate_frame(dev, x, y, w, h, cmix(0xFF9AA2AC, H, 0.20f), 3.0f, openBottom);
            const u32 acc = (0xB0u << 24) | (H & 0x00FFFFFF);
            grad_quad(dev, x + 3.0f, y + 3.0f, w - 6.0f, 1.0f, acc, acc, acc, acc);
            grad_quad(dev, x + 3.0f, y + 3.0f, 1.0f, h - 6.0f, acc, acc, acc, acc);
            grad_quad(dev, x + w - 4.0f, y + 3.0f, 1.0f, h - 6.0f, acc, acc, acc, acc);
        }
        break; }
    case 4: {   // ===== NEON : dark box that GLOWS with the neon colour -- colour projected inward + a tube border =====
        proc_state(dev);
        const u32 top = cmix(0xFF060608, H, 0.05f), bot = cmix(0xFF050507, H, 0.04f);   // near-black, faint hue
        grad_quad(dev, x, y, w, h, top, top, bot, bot);
        const u32 hue = H & 0x00FFFFFF;
        add_on(dev);
        grad_quad(dev, x, y, w, h, (0x08u << 24) | hue, (0x08u << 24) | hue, (0x06u << 24) | hue, (0x06u << 24) | hue);   // faint colour wash over the WHOLE box (subtle projection)
        const u32 e = (0x30u << 24) | hue;                                              // gentle reception near the edges -> fades inward
        const float rc = h * 0.30f, rw = w * 0.18f;
        grad_quad(dev, x, y, w, rc, e, e, hue, hue);                                    // top
        if (!openBottom) grad_quad(dev, x, y + h - rc, w, rc, hue, hue, e, e);          // bottom
        grad_quad(dev, x, y, rw, h, e, hue, e, hue);                                    // left
        grad_quad(dev, x + w - rw, y, rw, h, hue, e, hue, e);                           // right
        if (drawBorder) {
            const u32 tube = lighten(H, 40) & 0x00FFFFFF;                               // the TUBE : neon-COLOURED (not white)
            const u32 bloom = (0x80u << 24) | tube, core = (0xC8u << 24) | (lighten(H, 70) & 0x00FFFFFF);   // core = bright neon, only a touch toward white
            grad_quad(dev, x, y, w, 7.0f, bloom, bloom, tube, tube);                                    // edge bloom
            grad_quad(dev, x, y, 7.0f, h, bloom, tube, bloom, tube);
            grad_quad(dev, x + w - 7.0f, y, 7.0f, h, tube, bloom, tube, bloom);
            if (!openBottom) grad_quad(dev, x, y + h - 7.0f, w, 7.0f, tube, tube, bloom, bloom);
            grad_quad(dev, x, y, w, 2.0f, core, core, core, core);                                      // coloured tube cores
            grad_quad(dev, x, y, 2.0f, h, core, core, core, core);
            grad_quad(dev, x + w - 2.0f, y, 2.0f, h, core, core, core, core);
            if (!openBottom) grad_quad(dev, x, y + h - 2.0f, w, 2.0f, core, core, core, core);
        }
        add_off(dev);
        break; }
    case 5: {   // ===== FROST : soft icy frost texture (no shards), cold rime at edges, ice rim =====
        draw_mat(dev, g_matFrost, x, y, w, h, cmix(0xFF24323F, H, 0.16f), 220.0f);     // DARK cold-blue so text reads (frost pattern soft, fixed scale)
        proc_state(dev);
        add_on(dev);
        const u32 fr = 0x00DCEEFF, rm = (0x2Cu << 24) | fr;                            // pale rime, soft, only near the edges
        const float rc = h * 0.30f, rw = w * 0.16f;
        grad_quad(dev, x, y, w, rc, rm, rm, fr, fr);
        if (!openBottom) grad_quad(dev, x, y + h - rc, w, rc, fr, fr, rm, rm);
        grad_quad(dev, x, y, rw, h, rm, fr, rm, fr);
        grad_quad(dev, x + w - rw, y, rw, h, fr, rm, fr, rm);
        add_off(dev);
        if (drawBorder) {                                                                               // SOFT frosted rim : a pale icy glow hugging the edges + a thin bright ice line (no hard bevel)
            add_on(dev);
            const u32 gl = (0x40u << 24) | 0x00DCEEFF, z = 0x00DCEEFF;
            grad_quad(dev, x, y, w, 6.0f, gl, gl, z, z);
            if (!openBottom) grad_quad(dev, x, y + h - 6.0f, w, 6.0f, z, z, gl, gl);
            grad_quad(dev, x, y, 6.0f, h, gl, z, gl, z);
            grad_quad(dev, x + w - 6.0f, y, 6.0f, h, z, gl, z, gl);
            add_off(dev);
            const u32 line = cmix(H, 0xFFEAF6FF, 0.70f); const float t = 1.5f;                           // thin bright ice edge (t = width, must be float)
            grad_quad(dev, x, y, w, t, line, line, line, line);
            grad_quad(dev, x, y, t, h, line, line, line, line);
            grad_quad(dev, x + w - t, y, t, h, line, line, line, line);
            if (!openBottom) grad_quad(dev, x, y + h - t, w, t, line, line, line, line);
        }
        break; }
    case 6: {   // ===== ROYAL : velvet texture + a rich gold frame with an inner filet =====
        draw_mat(dev, g_matVelvet, x, y, w, h, cmix(H, 0xFF241428, 0.52f), 220.0f);    // deep velvet (hue -> dark rich, fixed scale)
        proc_state(dev);
        grad_quad(dev, x, y, w, h * 0.22f, 0x3E000000, 0x3E000000, 0x00000000, 0x00000000);             // velvet vignette
        if (!openBottom) grad_quad(dev, x, y + h * 0.78f, w, h * 0.22f, 0x00000000, 0x00000000, 0x3E000000, 0x3E000000);
        add_on(dev); soft_blob(dev, x + w * 0.5f, y + h * 0.5f, w * 0.5f, h * 0.5f, (0x0Cu << 24) | (cmix(H, 0xFFE0C060, 0.5f) & 0x00FFFFFF)); add_off(dev);   // throne glow
        if (drawBorder) {
            const u32 gold = cmix(H, 0xFFCBA430, 0.86f);                                                 // ALWAYS gold (only a whisper of the hue) -> the frame stays royal gold on every colour
            plate_frame(dev, x, y, w, h, gold, 3.0f, openBottom);                                       // gold frame
            const u32 gf = (0xC0u << 24) | (gold & 0x00FFFFFF); const float g = 5.0f;                   // inner gold filet
            grad_quad(dev, x + g, y + g, w - 2 * g, 1.0f, gf, gf, gf, gf);
            grad_quad(dev, x + g, y + g, 1.0f, h - 2 * g, gf, gf, gf, gf);
            grad_quad(dev, x + w - g - 1, y + g, 1.0f, h - 2 * g, gf, gf, gf, gf);
            if (!openBottom) grad_quad(dev, x + g, y + h - g - 1, w - 2 * g, 1.0f, gf, gf, gf, gf);
        }
        break; }
    }
    // LUMINOSITY : darken / lighten ONLY the interior texture, NOT the border (inset past the frame).
    // Drawn before the party text so the text stays readable.
    if (lum < -0.01f || lum > 0.01f) {
        const float b = 5.0f;                                                                           // stay inside the frame
        const float ix = x + b, iy = y + b, iw = w - 2.0f * b, ih = openBottom ? (h - b) : (h - 2.0f * b);
        if (iw > 0.0f && ih > 0.0f) {
            if (lum < 0.0f) { proc_state(dev); const u32 k = ((u32)(-lum * 176.0f) << 24); grad_quad(dev, ix, iy, iw, ih, k, k, k, k); }            // darker : black veil
            else            { proc_state(dev); add_on(dev); const u32 k = ((u32)(lum * 96.0f) << 24) | 0x00FFFFFF; grad_quad(dev, ix, iy, iw, ih, k, k, k, k); add_off(dev); }   // lighter : additive white
        }
    }
    #undef grad_quad
    #undef soft_blob
}

// draw a PROCEDURAL box background for the GLOBAL theme index (family x hue past the texture themes).
void draw_proc_window(u32 dev, int themeIdx, float x, float y, float w, float h, u32 tint, bool openBottom, bool drawBorder, float lum, u32 hueOverride) {
    const int p = themeIdx - TEX_N;
    if (p < 0 || p >= PROC_N || w <= 0 || h <= 0) return;
    g_procA = (tint >> 24) & 0xFFu;   // tint alpha -> interior opacity (the frame/glow stay solid ; 255 = opaque)
    proc_state(dev);
    const u32 H = (hueOverride & 0xFF000000u) ? hueOverride : BOX_HUES[p % BOX_HUE_N];   // custom hue wins over the preset
    draw_box_family(dev, p / BOX_HUE_N + 1, H, x, y, w, h, openBottom, drawBorder, lum);
    g_procA = 255;
}

// the representative FRAME colour of a PROCEDURAL box theme -- the exact tone draw_box_family paints its border
// with (Royal gold, Medieval iron, Frost ice, Neon tube, ...), NOT the interior hue. Lets a non-rectangular
// element (round minimap ring, equip-grid separators) wear the theme's real border. Returns 0 for a non-proc
// (FFXI) theme -> the caller uses the loaded WindowSkin's borderColor instead. Kept in sync with draw_box_family.
u32 box_theme_border_color(int themeIdx, u32 hueOverride) {
    if (!window_theme_is_proc(themeIdx)) return 0;
    const int p = themeIdx - TEX_N;
    const int fam = p / BOX_HUE_N + 1;
    const u32 H = (hueOverride & 0xFF000000u) ? hueOverride : BOX_HUES[p % BOX_HUE_N];
    switch (fam) {
        case 2:  return 0xFF4A4E54u;                                     // Medieval : matte dark iron (hue-independent)
        case 3:  return cmix(0xFF9AA2AC, H, 0.20f) | 0xFF000000u;        // Heroic   : polished steel
        case 4:  return (lighten(H, 40) & 0x00FFFFFFu) | 0xFF000000u;    // Neon     : the coloured tube
        case 5:  return cmix(H, 0xFFEAF6FF, 0.70f) | 0xFF000000u;        // Frost    : bright ice line
        case 6:  return cmix(H, 0xFFCBA430, 0.86f) | 0xFF000000u;        // Royal    : gold
        default: return cmix(H, 0xFF4A525C, 0.30f) | 0xFF000000u;        // Modern   : glossy dark edge
    }
}

// one anti-aliased FILLED disc (feathered rim) -> a clean circular band edge for the ring renderer below.
static inline void aa_disc(u32 dev, float cx, float cy, float r, u32 col) {
    if (r <= 0.0f) return;
    rrect(dev, cx - r, cy - r, 2.0f * r, 2.0f * r, r, col, col, 1.2f);
}

// draw a PROCEDURAL box theme's real BORDER as a circular ring (for the round minimap). Reproduces each
// family's frame -- gold plate + inner filet (Royal), matte-iron / polished-steel bevel (Medieval / Heroic),
// neon tube glow, frost rime -- as concentric AA bands in [rInner, rOuter], drawn OUTER-first so each smaller
// disc leaves the previous band showing (the caller then draws the map disc at rInner over the interior). This
// is the SAME colour recipe as draw_box_family's border, so the round minimap wears the EXACT theme frame --
// not a flat-colour approximation. Kept in sync with draw_box_family. Needs a >= ~6px band to read the bevel.
void draw_box_border_ring(u32 dev, int themeIdx, float cx, float cy, float rOuter, float rInner, float lum, u32 hueOverride) {
    if (!window_theme_is_proc(themeIdx) || rOuter <= rInner) return;
    const int p = themeIdx - TEX_N;
    const int fam = p / BOX_HUE_N + 1;
    const u32 H = (hueOverride & 0xFF000000u) ? hueOverride : BOX_HUES[p % BOX_HUE_N];
    const u32 base = box_theme_border_color(themeIdx, hueOverride);   // single-source family frame colour (shared with the rect renderer + the colour getter)
    (void)lum;
    proc_state(dev);
    switch (fam) {
    case 2: {   // MEDIEVAL : matte dark-iron bevel band + inner recessed shadow line
        const u32 iron = base;
        aa_disc(dev, cx, cy, rOuter,        lighten(iron, -34));
        aa_disc(dev, cx, cy, rOuter - 1.0f, lighten(iron,  58));   // top-lit bevel
        aa_disc(dev, cx, cy, rOuter - 2.4f, iron);                 // matte iron plate
        aa_disc(dev, cx, cy, rInner + 1.3f, lighten(iron, -58));   // inner recessed shadow line
        break; }
    case 3: {   // HEROIC : polished-steel bevel + a bright hue accent line just inside
        const u32 steel = base;
        aa_disc(dev, cx, cy, rOuter,        lighten(steel, -44));
        aa_disc(dev, cx, cy, rOuter - 1.0f, lighten(steel,  74));  // bright bevel
        aa_disc(dev, cx, cy, rOuter - 2.6f, steel);
        aa_disc(dev, cx, cy, rInner + 1.4f, (H & 0x00FFFFFFu) | 0xFF000000u);   // hue accent line
        break; }
    case 4: {   // NEON : a real glowing TUBE -- neon COLOUR on the outer & inner edges, a hot near-WHITE core in the
        const u32 hue = H & 0x00FFFFFFu, white = lighten(H, 150) & 0x00FFFFFFu;   // middle ("couleur / blanc / couleur")
        const float band = rOuter - rInner;
        dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_ONE);
        aa_disc(dev, cx, cy, rOuter + 2.0f, (0x34u << 24) | hue);                 // soft additive outer colour bloom (glow)
        dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        aa_disc(dev, cx, cy, rOuter,                0xFF000000u | hue);           // outer colour edge
        aa_disc(dev, cx, cy, rOuter - band * 0.34f, 0xFF000000u | white);         // hot white core
        aa_disc(dev, cx, cy, rOuter - band * 0.66f, 0xFF000000u | hue);           // inner colour edge
        break; }
    case 5: {   // FROST : soft icy glow hugging the rim + a thin bright ice line
        const u32 z = 0x00DCEEFFu;
        dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_ONE);
        aa_disc(dev, cx, cy, rOuter + 2.0f, (0x38u << 24) | z);
        aa_disc(dev, cx, cy, rOuter,        (0x66u << 24) | z);
        dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        const u32 line = base;
        aa_disc(dev, cx, cy, rOuter - 1.0f, line);
        aa_disc(dev, cx, cy, rInner + 1.2f, line);
        break; }
    case 6: {   // ROYAL : gold plate bevel + a bright inner gold filet
        const u32 gold = base;
        aa_disc(dev, cx, cy, rOuter,        lighten(gold, -48));   // dark gold edge
        aa_disc(dev, cx, cy, rOuter - 0.9f, lighten(gold,  74));   // bright bevel highlight
        aa_disc(dev, cx, cy, rOuter - 2.4f, gold);                 // main gold plate
        aa_disc(dev, cx, cy, rInner + 2.0f, lighten(gold, -34));   // recess before the filet
        aa_disc(dev, cx, cy, rInner + 1.0f, (0xF0u << 24) | (gold & 0x00FFFFFFu));   // bright inner filet
        break; }
    default: {  // MODERN : glossy dark edge with a top gloss highlight
        const u32 edge = base;
        aa_disc(dev, cx, cy, rOuter,        lighten(edge, -30));
        aa_disc(dev, cx, cy, rOuter - 1.0f, lighten(edge,  56));   // gloss highlight
        aa_disc(dev, cx, cy, rOuter - 2.2f, edge);
        break; }
    }
}

// draw a PROCEDURAL theme's real BORDER on a rectangle, WITHOUT the interior bg -- the exact family frame
// (Royal gold plate + filet, Medieval iron / Heroic steel bevel, Neon tube, Frost rime, Modern glossy edge),
// the SAME recipe as draw_box_family's border block. For small framed rects (the equip-viewer cells) so their
// separators are the theme's real frame, not a flat colour. openBottom = false (a full closed frame per cell).
void draw_box_border_rect(u32 dev, int themeIdx, float x, float y, float w, float h, float lum, u32 hueOverride) {
    if (!window_theme_is_proc(themeIdx) || w <= 0 || h <= 0) return;
    const int p = themeIdx - TEX_N;
    const int fam = p / BOX_HUE_N + 1;
    const u32 H = (hueOverride & 0xFF000000u) ? hueOverride : BOX_HUES[p % BOX_HUE_N];   // custom hue wins over the preset
    const u32 base = box_theme_border_color(themeIdx, hueOverride);   // single-source family frame colour (shared with the ring renderer + the colour getter)
    (void)lum;
    proc_state(dev);
    switch (fam) {
    case 2: {   // MEDIEVAL : thick matte dark-iron band + inner recessed shadow line
        plate_frame(dev, x, y, w, h, base, 4.0f, false);
        grad_quad(dev, x + 4.0f, y + 4.0f, w - 8.0f, 1.0f, 0x50000000u, 0x50000000u, 0x50000000u, 0x50000000u);
        break; }
    case 3: {   // HEROIC : polished-steel bevel + bright hue accent lines just inside
        plate_frame(dev, x, y, w, h, base, 3.0f, false);
        const u32 acc = (0xB0u << 24) | (H & 0x00FFFFFFu);
        grad_quad(dev, x + 3.0f, y + 3.0f, w - 6.0f, 1.0f, acc, acc, acc, acc);
        grad_quad(dev, x + 3.0f, y + 3.0f, 1.0f, h - 6.0f, acc, acc, acc, acc);
        grad_quad(dev, x + w - 4.0f, y + 3.0f, 1.0f, h - 6.0f, acc, acc, acc, acc);
        break; }
    case 4: {   // NEON : additive coloured tube (edge bloom + bright cores)
        const u32 tube = lighten(H, 40) & 0x00FFFFFFu;
        const u32 bloom = (0x80u << 24) | tube, core = (0xC8u << 24) | (lighten(H, 70) & 0x00FFFFFFu);
        dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_ONE);
        grad_quad(dev, x, y, w, 5.0f, bloom, bloom, tube, tube);
        grad_quad(dev, x, y, 5.0f, h, bloom, tube, bloom, tube);
        grad_quad(dev, x + w - 5.0f, y, 5.0f, h, tube, bloom, tube, bloom);
        grad_quad(dev, x, y + h - 5.0f, w, 5.0f, tube, tube, bloom, bloom);
        grad_quad(dev, x, y, w, 1.6f, core, core, core, core);
        grad_quad(dev, x, y, 1.6f, h, core, core, core, core);
        grad_quad(dev, x + w - 1.6f, y, 1.6f, h, core, core, core, core);
        grad_quad(dev, x, y + h - 1.6f, w, 1.6f, core, core, core, core);
        dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        break; }
    case 5: {   // FROST : soft icy rim glow + thin bright ice line
        const u32 z = 0x00DCEEFFu, gl = (0x40u << 24) | z;
        dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_ONE);
        grad_quad(dev, x, y, w, 5.0f, gl, gl, z, z);
        grad_quad(dev, x, y + h - 5.0f, w, 5.0f, z, z, gl, gl);
        grad_quad(dev, x, y, 5.0f, h, gl, z, gl, z);
        grad_quad(dev, x + w - 5.0f, y, 5.0f, h, z, gl, z, gl);
        dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        const u32 line = base; const float t = 1.4f;
        grad_quad(dev, x, y, w, t, line, line, line, line);
        grad_quad(dev, x, y, t, h, line, line, line, line);
        grad_quad(dev, x + w - t, y, t, h, line, line, line, line);
        grad_quad(dev, x, y + h - t, w, t, line, line, line, line);
        break; }
    case 6: {   // ROYAL : gold plate frame only (no inner filet -- user didn't want the 2nd inner line on the equip frame)
        plate_frame(dev, x, y, w, h, base, 3.0f, false);
        break; }
    default: {  // MODERN : glossy dark edge
        draw_proc_border(dev, x, y, w, h, base, 4, false);
        break; }
    }
}

// draw ONE neon-tube separator line filling [x,y,w,h] : neon COLOUR on the two long edges, a hot near-WHITE core
// down the middle (across the SHORT axis) -- "couleur / blanc / couleur", matching the round-minimap neon ring.
// For the equip-grid neon separators. No-op if the theme isn't the Neon family.
void draw_neon_line(u32 dev, int themeIdx, float x, float y, float w, float h, u32 hueOverride) {
    if (window_theme_family(themeIdx) != 4) return;
    const int p = themeIdx - TEX_N;
    const u32 H = (hueOverride & 0xFF000000u) ? hueOverride : BOX_HUES[p % BOX_HUE_N];
    const u32 c = 0xFF000000u | (H & 0x00FFFFFFu), wc = 0xFF000000u | (lighten(H, 150) & 0x00FFFFFFu);
    proc_state(dev);
    if (w <= h) {                       // vertical line -> colour | white | colour across the width
        const float hw = w * 0.5f;
        grad_quad(dev, x,      y, hw, h, c,  wc, c,  wc);
        grad_quad(dev, x + hw, y, hw, h, wc, c,  wc, c);
    } else {                            // horizontal line -> colour | white | colour across the height
        const float hh = h * 0.5f;
        grad_quad(dev, x, y,      w, hh, c,  c,  wc, wc);
        grad_quad(dev, x, y + hh, w, hh, wc, wc, c,  c);
    }
}

// one flat rectangle OUTLINE (4 thin edges of width t, colour col) -> seams join at the corners.
static void rect_outline(u32 dev, float x, float y, float w, float h, float t, u32 col) {
    grad_quad(dev, x,         y,         w, t, col, col, col, col);   // top
    grad_quad(dev, x,         y + h - t, w, t, col, col, col, col);   // bottom
    grad_quad(dev, x,         y,         t, h, col, col, col, col);   // left
    grad_quad(dev, x + w - t, y,         t, h, col, col, col, col);   // right
}

// draw a SEAMLESS neon-tube rectangle frame filling the band of width `tw` around [x,y,w,h] : three CONCENTRIC
// outlines colour / white / colour -> a continuous glowing tube whose corners join with no seam (unlike four
// separate gradient segments). For the equip-grid neon OUTER frame, drawn last so it sits over the lattice.
void draw_neon_frame(u32 dev, int themeIdx, float x, float y, float w, float h, float tw, u32 hueOverride) {
    if (window_theme_family(themeIdx) != 4 || w <= 0 || h <= 0) return;
    const int p = themeIdx - TEX_N;
    const u32 H = (hueOverride & 0xFF000000u) ? hueOverride : BOX_HUES[p % BOX_HUE_N];
    const u32 c = 0xFF000000u | (H & 0x00FFFFFFu), wc = 0xFF000000u | (lighten(H, 150) & 0x00FFFFFFu);
    proc_state(dev);
    const float t = tw / 3.0f;                                        // each concentric stroke
    rect_outline(dev, x,           y,           w,           h,           t, c);    // outer colour edge
    rect_outline(dev, x + t,       y + t,       w - 2.0f * t, h - 2.0f * t, t, wc);  // white core
    rect_outline(dev, x + 2.0f * t, y + 2.0f * t, w - 4.0f * t, h - 4.0f * t, t, c); // inner colour edge
}

} // namespace aio
