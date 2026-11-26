// theme_kit.cpp -- a TEST of a painted theme kit (2026-11-19) : "Eclat de Holla" (ivory Zilart stone holding sapphire
// crystal), painted by an image generator, sliced by scratchpad holla/export3.py into assets/themes/holla/ : 4 edge tiles
// (the top one, the others its mirrors) and 4 corners (the top-left with its crystal shard, the others mirrors of one
// plain corner). manifest.txt gives each piece's size ; a corner also gives its ANCHOR (the box's outer corner inside the
// piece -- the shard overhangs the box) and its EXTENTS along the box's edges. Only `//aio kittest` (a dev-build
// command) turns it on : it then frames EVERY HUD box (gfx/window.h g_frameOverride). Nothing else uses it.
#include "ui/hud_internal.h"
#include "ui/config_controls.h"   // cs()
#include "model/paths.h"
#include "gfx/draw.h"
#include "gfx/d3d.h"
#include "gfx/texture.h"
#include "gfx/window.h"
#include "ui/frame_bake.h"
#include "windower_debug.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cmath>

namespace aio {

namespace {
struct Piece { const char* name; int w, h, ax, ay, ex, ey; u32 tex; int tw, th; };
Piece P[] = {
    { "e_t", 0, 0, 0, 0, 0, 0, 0, 0, 0 }, { "e_b", 0, 0, 0, 0, 0, 0, 0, 0, 0 }, { "e_l", 0, 0, 0, 0, 0, 0, 0, 0, 0 }, { "e_r", 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { "c_tl", 0, 0, 0, 0, 0, 0, 0, 0, 0 }, { "c_tr", 0, 0, 0, 0, 0, 0, 0, 0, 0 }, { "c_bl", 0, 0, 0, 0, 0, 0, 0, 0, 0 }, { "c_br", 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { "c_tlp", 0, 0, 0, 0, 0, 0, 0, 0, 0 },
};
enum { ET, EB, EL, ER, CTL, CTR, CBL, CBR, CTLP, NP };
bool g_on = false; int g_tries = 0; DWORD g_next = 0; bool g_loaded = false; u32 g_dev = 0;
float g_screenH = 1440.0f;

int pow2(int v) { int p = 1; while (p < v) p <<= 1; return p; }
// the manifest, then every piece into a power-of-two mipmapped texture (padded with its own edge pixels) ; bounded retry, logged
bool load(u32 dev) {
    if (g_loaded && g_dev == dev) return true;
    const DWORD now = GetTickCount();
    if (g_tries >= 6 || (g_tries && (int)(now - g_next) < 0)) return false;
    ++g_tries; g_next = now + 3000;
    {
        char mp[MAX_PATH]; _snprintf(mp, sizeof(mp), "%s\\assets\\themes\\holla\\manifest.txt", plugin_dir()); mp[sizeof(mp) - 1] = 0;
        FILE* fm = fopen(mp, "r");
        if (!fm) { windower::debug::log("KITTEST no manifest '%s'", mp); return false; }
        char line[160];
        while (fgets(line, sizeof(line), fm)) {
            char nm[32]; int v[6] = { 0, 0, 0, 0, 0, 0 };
            const int n = sscanf(line, "%31s %d %d %d %d %d %d", nm, &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]);
            if (n < 3) continue;
            for (int i = 0; i < NP; ++i) if (!strcmp(P[i].name, nm)) { P[i].w = v[0]; P[i].h = v[1]; P[i].ax = v[2]; P[i].ay = v[3]; P[i].ex = v[4]; P[i].ey = v[5]; }
        }
        fclose(fm);
    }
    int ok = 0; static u32 px[512 * 512], pot[512 * 512];
    for (int i = 0; i < NP; ++i) {
        Piece& p = P[i];
        if (p.tex) { release_texture(p.tex); p.tex = 0; }
        if (p.w <= 0 || p.h <= 0 || p.w > 512 || p.h > 512) continue;
        wchar_t path[MAX_PATH]; char a[MAX_PATH];
        _snprintf(a, sizeof(a), "%s\\assets\\themes\\holla\\%s.png", plugin_dir(), p.name); a[sizeof(a) - 1] = 0;
        MultiByteToWideChar(CP_ACP, 0, a, -1, path, MAX_PATH);
        if (!load_png_argb(path, p.w, p.h, px)) continue;
        p.tw = pow2(p.w); p.th = pow2(p.h);
        for (int y = 0; y < p.th; ++y) { const int sy = y < p.h ? y : p.h - 1;
            for (int x = 0; x < p.tw; ++x) pot[y * p.tw + x] = px[sy * p.w + (x < p.w ? x : p.w - 1)]; }
        p.tex = make_texture_argb_mip(dev, p.tw, p.th, pot);
        if (p.tex) ++ok;
    }
    g_loaded = ok == NP; g_dev = dev;
    windower::debug::log("KITTEST holla v4 pieces : %d / %d loaded (try %d)%s", ok, NP, g_tries, g_loaded ? "" : " -- missing files in assets\\themes\\holla ?");
    return g_loaded;
}
// a piece's sub-rectangle (sx, sy, sw, sh in its native pixels) drawn at (x, y) size (w, h)
void piece(u32 dev, const Piece& p, float x, float y, float w, float h, float sx, float sy, float sw, float sh, u32 c) {
    if (!p.tex || w <= 0.0f || h <= 0.0f) return;
    dTexQuadState(dev, p.tex, true);
    tquad(dev, x, y, w, h, sx / (float)p.tw, (sx + sw) / (float)p.tw, sy / (float)p.th, (sy + sh) / (float)p.th, c, c);
}
// an edge tile repeated along [a, b) at its native length x k ; horizontal (top / bottom) or vertical (left / right)
void edge(u32 dev, const Piece& p, bool horiz, float a, float b, float fixed, float k, u32 c) {
    const float tl = (horiz ? p.w : p.h) * k, th = (horiz ? p.h : p.w) * k;
    for (float o = a; o < b; o += tl) {
        const float run = (b - o) < tl ? (b - o) : tl, src = run / k;
        if (horiz) piece(dev, p, o, fixed, run, th, 0.0f, 0.0f, src, (float)p.h, c);
        else       piece(dev, p, fixed, o, th, run, 0.0f, 0.0f, (float)p.w, src, c);
    }
}
// the whole frame at scale k : the 4 edges between the corners' extents, then the 4 corners anchored on the box's corners
void frame(u32 dev, float x, float y, float w, float h, u32 c, float k, bool accent) {
    const int TLI = accent ? CTL : CTLP;                            // the crystal shard : the party box only
    const float E = P[ET].h * k;                                       // the border's thickness
    // the inside : the slate glass, a little translucent, under the edges' inner half
    const u32 a = c >> 24, top = ((a * 0xD8 / 255) << 24) | 0x00132447u, bot = ((a * 0xE6 / 255) << 24) | 0x000B1730u;
    dSetTex(dev, 0, 0); cs(dev);
    rrect(dev, x + E * 0.5f, y + E * 0.5f, w - E, h - E, 0.0f, top, bot);
    edge(dev, P[ET], true,  x + P[TLI].ex * k, x + w - P[CTR].ex * k, y, k, c);
    edge(dev, P[EB], true,  x + P[CBL].ex * k, x + w - P[CBR].ex * k, y + h - E, k, c);
    edge(dev, P[EL], false, y + P[TLI].ey * k, y + h - P[CBL].ey * k, x, k, c);
    edge(dev, P[ER], false, y + P[CTR].ey * k, y + h - P[CBR].ey * k, x + w - E, k, c);
    const struct { int i; float cx, cy; } CO[4] = { { CTR, x + w, y }, { CBL, x, y + h }, { CBR, x + w, y + h }, { TLI, x, y } };   // the shard last, on top
    for (const auto& q : CO) { const Piece& p = P[q.i];
        piece(dev, p, q.cx - p.ax * k, q.cy - p.ay * k, p.w * k, p.h * k, 0.0f, 0.0f, (float)p.w, (float)p.h, c); }
}
// ---- the tileable kits (assets/themes/kits/<id> : the 11 "cadres tuilables", game-scale pieces ; manifest.txt is
// generated from each theme.json by scratchpad kits_import.py). A 12 px border, 20 px corners ; each rail = its 1-px JOINT
// stretched, its tile repeated (the last fades in over one border), the SHORT band filling what is left when the kit has
// one (rendu.js) ; the fill tiled from the window's origin ; the corners ; the ornaments (top-left ones : the PARTY only)
enum { KMAX = 32 };
char g_kname[KMAX][40]; Piece K[KMAX]; int g_kn = 0;
struct KOrn { int piece, anchor, dx, dy; };   // anchor 0 top left, 1 top right, 2 top centre
KOrn g_korn[4]; int g_kornN = 0;
int g_kb = 12, g_kc = 20; bool g_kinset = false, g_kshort = false, g_kseam = false;   // seamless : the rail's tile wraps onto itself -> the last one CUT
int KI[4][3], KCORN[4], KFOND = -1;          // per side (haut bas gauche droit) : bord, raccord, court
char g_kid[24] = "";
int g_kit = 0;                               // 0 Holla v4 (the first trial), 1 a tileable kit (g_kid), 2 the procedural iron
int g_istyle = 0;                            // the procedural frame's style (frame_bake.h) : 0 polished iron (`iron`), 3 wrought iron (`fer`)
float iron_border() { return snap((g_istyle == 3 ? 8.0f : 12.0f) * (g_screenH / 1440.0f)); }   // the wrought iron : 16 -> 11 -> 8 (2026-11-20 "encore plus fin")
int g_btries = 0; DWORD g_bnext = 0; bool g_bloaded = false; u32 g_bdev = 0;
int kfind(const char* n) { for (int i = 0; i < g_kn; ++i) if (!strcmp(g_kname[i], n)) return i; return -1; }
bool load_kit(u32 dev) {
    if (g_bloaded && g_bdev == dev) return true;
    const DWORD now = GetTickCount();
    if (g_btries >= 6 || (g_btries && (int)(now - g_bnext) < 0)) return false;
    ++g_btries; g_bnext = now + 3000;
    for (int i = 0; i < g_kn; ++i) if (K[i].tex) { release_texture(K[i].tex); K[i].tex = 0; }
    g_kn = 0; g_kornN = 0; g_kb = 12; g_kc = 20; g_kinset = g_kshort = g_kseam = false;
    char swap[4][40] = {};
    char mp[MAX_PATH]; _snprintf(mp, sizeof(mp), "%s\\assets\\themes\\kits\\%s\\manifest.txt", plugin_dir(), g_kid); mp[sizeof(mp) - 1] = 0;
    FILE* fm = fopen(mp, "r");
    if (!fm) { windower::debug::log("KITTEST kit '%s' : no manifest '%s' (try %d)", g_kid, mp, g_btries); return false; }
    char line[160];
    while (fgets(line, sizeof(line), fm)) {
        char kw[16] = {}, nm[40] = {}; int a = 0, b = 0, c = 0;
        if (sscanf(line, "%15s", kw) != 1) continue;
        if (!strcmp(kw, "border")) sscanf(line, "%*s %d", &g_kb);
        else if (!strcmp(kw, "corner")) sscanf(line, "%*s %d", &g_kc);
        else if (!strcmp(kw, "inset")) { sscanf(line, "%*s %d", &a); g_kinset = a != 0; }
        else if (!strcmp(kw, "short")) { sscanf(line, "%*s %d", &a); g_kshort = a != 0; }
        else if (!strcmp(kw, "seamless")) { sscanf(line, "%*s %d", &a); g_kseam = a != 0; }
        else if (!strcmp(kw, "piece") && g_kn < KMAX && sscanf(line, "%*s %39s %d %d", nm, &a, &b) == 3) {
            strcpy(g_kname[g_kn], nm); Piece& p = K[g_kn++]; memset(&p, 0, sizeof(p)); p.name = g_kname[g_kn - 1]; p.w = a; p.h = b;
        }
        else if (!strcmp(kw, "orn") && g_kornN < 4 && sscanf(line, "%*s %39s %d %d %d", nm, &a, &b, &c) == 4) {
            const int i = kfind(nm); if (i >= 0) g_korn[g_kornN++] = { i, a, b, c };
        }
        else if (!strcmp(kw, "swap") && sscanf(line, "%*s %d %39s", &a, nm) == 2 && a >= 0 && a < 4) strcpy(swap[a], nm);
    }
    fclose(fm);
    static const char* const SIDE[4] = { "haut", "bas", "gauche", "droit" };
    static const char* const CORN[4] = { "coin_haut_gauche", "coin_haut_droit", "coin_bas_gauche", "coin_bas_droit" };
    char n[48]; bool need = true;
    for (int s = 0; s < 4; ++s) {
        sprintf(n, "bord_%s", SIDE[s]); KI[s][0] = kfind(n);
        sprintf(n, "raccord_%s", SIDE[s]); KI[s][1] = kfind(n);
        sprintf(n, "court_%s", SIDE[s]); KI[s][2] = kfind(n);
        KCORN[s] = kfind(swap[s][0] ? swap[s] : CORN[s]);
        need = need && KI[s][0] >= 0 && KI[s][1] >= 0 && KCORN[s] >= 0 && (!g_kshort || KI[s][2] >= 0);
    }
    KFOND = kfind("fond"); need = need && KFOND >= 0;
    int ok = 0; static u32 px[256 * 256], pot[256 * 256];
    for (int i = 0; i < g_kn; ++i) {
        Piece& p = K[i];
        if (p.w <= 0 || p.h <= 0 || p.w > 128 || p.h > 128) continue;
        wchar_t path[MAX_PATH]; char a[MAX_PATH];
        _snprintf(a, sizeof(a), "%s\\assets\\themes\\kits\\%s\\%s.png", plugin_dir(), g_kid, p.name); a[sizeof(a) - 1] = 0;
        MultiByteToWideChar(CP_ACP, 0, a, -1, path, MAX_PATH);
        if (!load_png_argb(path, p.w, p.h, px)) continue;
        p.tw = pow2(p.w); p.th = pow2(p.h);
        for (int y = 0; y < p.th; ++y) { const int sy = y < p.h ? y : p.h - 1;
            for (int x = 0; x < p.tw; ++x) pot[y * p.tw + x] = px[sy * p.w + (x < p.w ? x : p.w - 1)]; }
        p.tex = make_texture_argb_mip(dev, p.tw, p.th, pot);
        if (p.tex) ++ok;
    }
    g_bloaded = need && ok == g_kn && g_kn > 0; g_bdev = dev;
    windower::debug::log("KITTEST kit '%s' : %d / %d pieces loaded, %d ornament(s), border %d corner %d inset %d short %d (try %d)%s",
        g_kid, ok, g_kn, g_kornN, g_kb, g_kc, (int)g_kinset, (int)g_kshort, g_btries, g_bloaded ? "" : (need ? " -- a texture failed" : " -- a required piece is missing"));
    return g_bloaded;
}
u32 fade(u32 c, float a) { if (a < 0.0f) a = 0.0f; if (a > 1.0f) a = 1.0f; a = a * a * (3.0f - 2.0f * a);
    return ((u32)((float)(c >> 24) * a) << 24) | (c & 0x00FFFFFFu); }
// a motif at (x, y) ; with a short band its two end pixels along the run are left to the joint below (rendu.js drawMotif)
void motif(u32 dev, const Piece& m, bool horiz, float x, float y, float k, u32 c) {
    const int sx = g_kshort && horiz ? 1 : 0, sy = g_kshort && !horiz ? 1 : 0;
    piece(dev, m, x + sx * k, y + sy * k, (m.w - 2 * sx) * k, (m.h - 2 * sy) * k, (float)sx, (float)sy, (float)(m.w - 2 * sx), (float)(m.h - 2 * sy), c);
}
void rail(u32 dev, int side, bool horiz, float x, float y, float len, float k, u32 c) {
    const Piece& tile = K[KI[side][0]]; const Piece& joint = K[KI[side][1]];
    const float b = g_kb * k, step = (horiz ? tile.w : tile.h) * k;
    if (len <= 0.0f || step <= 0.0f) return;
    piece(dev, joint, x, y, horiz ? len : b, horiz ? b : len, 0.0f, 0.0f, (float)joint.w, (float)joint.h, c);
    if (g_kseam) {                                                  // a seamless tile : repeated, the last one cut at the corner
        // the tiles are anchored to the SCREEN (not to the box) : two boxes drawing the same shared side, or two glued boxes'
        // rails in line, show one continuous pattern
        const float start = horiz ? x : y, ph = start - floorf(start / step) * step;
        for (float pos = -ph; pos < len - 0.01f; pos += step) {
            const float a0 = pos < 0.0f ? 0.0f : pos, a1 = pos + step < len ? pos + step : len;
            if (a1 - a0 <= 0.01f) continue;
            const float s0 = (a0 - pos) / k, sn = (a1 - a0) / k;
            if (horiz) piece(dev, tile, x + a0, y, a1 - a0, b, s0, 0.0f, sn, (float)tile.h, c);
            else       piece(dev, tile, x, y + a0, b, a1 - a0, 0.0f, s0, (float)tile.w, sn, c);
        }
        return;
    }
    const int count = (int)(len / step); const float rest = len - (float)count * step;
    if (g_kshort && KI[side][2] >= 0) {
        const Piece& sm = K[KI[side][2]]; const float pitch = (horiz ? sm.w : sm.h) * k;
        if (pitch > 0.0f) {
            auto fill = [&](float start, float span) {
                const int units = (int)(span / pitch); const float frac = (span - (float)units * pitch) / b;
                for (int i = 0; i < units; ++i) { const float pos = start + (float)i * pitch;
                    motif(dev, sm, horiz, horiz ? x + pos : x, horiz ? y : y + pos, k, i == units - 1 ? fade(c, frac) : c); }
            };
            if (count && rest < b) fill((float)(count - 1) * step, step);
            fill((float)count * step, rest);
        }
    }
    for (int i = 0; i < count; ++i) { const float pos = (float)i * step;
        motif(dev, tile, horiz, horiz ? x + pos : x, horiz ? y : y + pos, k, i == count - 1 ? fade(c, rest / b) : c); }
}
// MERGED boxes (g_frameJoin, set by the menu screens' placement) : two glued boxes share ONE border (the neighbour's
// rect overlaps this one by b). Per corner : off[0] = where the HORIZONTAL rail starts from that corner, off[1] = the
// VERTICAL one ; a normal corner = (cc, cc) + its piece. A corner on a glued side loses its piece :
//   edges aligned there        -> the outer edge runs straight on : vertical side (0, b) ; horizontal side (b, 0)
//   this box ends inside the neighbour's span (a T against its rail) : vertical side (b, 0) ; horizontal side (0, b)
//   this box protrudes past the neighbour there -> keeps its corner
void join_corner(int jm, int corner, float b, float cc, float off[2], bool& piece_) {
    const bool left = !(corner & 1), top = !(corner & 2);
    off[0] = off[1] = cc; piece_ = true;
    // per side sd (0 L 1 T 2 R 3 B) : bits 4+4sd start aligned, 5+4sd end aligned, 6+4sd / 7+4sd protrudes at start / end
    const int vs = left ? 0 : 2, hs = top ? 1 : 3, vside = 1 << vs, hside = 1 << hs;
    const int ve = top ? 0 : 1, he = left ? 0 : 1;                   // which end of the side this corner is
    const bool vAl = (jm & (1 << (4 + ve + 4 * vs))) != 0, vPr = (jm & (1 << (6 + ve + 4 * vs))) != 0;
    const bool hAl = (jm & (1 << (4 + he + 4 * hs))) != 0, hPr = (jm & (1 << (6 + he + 4 * hs))) != 0;
    if ((jm & vside) && (vAl || !vPr)) { piece_ = false; if (vAl) { off[0] = 0.0f; off[1] = b; } else { off[0] = b; off[1] = 0.0f; } return; }
    if ((jm & hside) && (hAl || !hPr)) { piece_ = false; if (hAl) { off[0] = b; off[1] = 0.0f; } else { off[0] = 0.0f; off[1] = b; } }
}
void frame_kit(u32 dev, float x, float y, float w, float h, u32 c, float k, bool accent, int jm) {
    const float b = g_kb * k, cc = g_kc * k;
    {   // the fill, tiled from the window's origin, clipped to [border-1 inside] (or the Bastok cut : 1 px inside)
        const Piece& f = K[KFOND]; const float fw = f.w * k, fh = f.h * k;
        const float in = g_kinset ? (g_kb - 1) * k : 1.0f, ox = g_kinset ? x : x + 1.0f, oy = g_kinset ? y : y + 1.0f;
        const float x0 = x + ((jm & 1) ? b : in), y0 = y + ((jm & 2) ? b : in), x1 = x + w - in, y1 = y + h - in;   // a glued left / top : the neighbour's rail is there
        for (float ty = oy; ty < y1; ty += fh) for (float tx = ox; tx < x1; tx += fw) {
            const float ax = tx > x0 ? tx : x0, ay = ty > y0 ? ty : y0;
            const float bx = tx + fw < x1 ? tx + fw : x1, by = ty + fh < y1 ? ty + fh : y1;
            if (bx <= ax || by <= ay) continue;
            piece(dev, f, ax, ay, bx - ax, by - ay, (ax - tx) / k, (ay - ty) / k, (bx - ax) / k, (by - ay) / k, c);
        }
    }
    float o[4][2]; bool pc[4];
    for (int i = 0; i < 4; ++i) join_corner(jm, i, b, cc, o[i], pc[i]);   // 0 TL 1 TR 2 BL 3 BR
    // a SHARED side is drawn by both boxes at the same place : both must use the same texture, or the separator's profile
    // flips with the draw order (seen as a 1-2 px shift) -> a glued BOTTOM uses the TOP rail, a glued RIGHT the LEFT one : the
    // line a separator continues into (a box's outer left edge below it) is a left rail too -> one texture all along
    rail(dev, 0, true,  x + o[0][0], y, w - o[0][0] - o[1][0], k, c);
    rail(dev, (jm & 8) ? 0 : 1, true,  x + o[2][0], y + h - b, w - o[2][0] - o[3][0], k, c);
    rail(dev, 2, false, x, y + o[0][1], h - o[0][1] - o[2][1], k, c);
    rail(dev, (jm & 4) ? 2 : 3, false, x + w - b, y + o[1][1], h - o[1][1] - o[3][1], k, c);
    for (int i = 0; i < 4; ++i) { if (!pc[i]) continue; const Piece& p = K[KCORN[i]];
        piece(dev, p, (i & 1) ? x + w - p.w * k : x, (i & 2) ? y + h - p.h * k : y, p.w * k, p.h * k, 0.0f, 0.0f, (float)p.w, (float)p.h, c); }
    for (int i = 0; i < g_kornN; ++i) { const KOrn& o = g_korn[i]; const Piece& p = K[o.piece];
        if (o.anchor == 0 && !accent) continue;                       // the top-left crest : the party box only
        float ox = x + o.dx * k;
        if (o.anchor == 1) ox += w - p.w * k; else if (o.anchor == 2) ox += (float)(int)((w - p.w * k) * 0.5f);
        piece(dev, p, snap(ox), snap(y + o.dy * k), p.w * k, p.h * k, 0.0f, 0.0f, (float)p.w, (float)p.h, c); }
}
// ---- the procedural IRON frame (//aio kittest iron, frame_bake.cpp) : ONE baked plate per glued group, cut in 256 px
// tiles (any size, no power-of-two waste), cached by the group's SHAPE (relative rects + slots) : a slide or a move reuses
// it, only a layout change bakes. The bake (~150 ms) runs on a WORKER THREAD : the game never waits for it (2026-11-20 :
// baked in the frame, it dropped Equipment to 3 FPS). Until it lands, a box draws its last plate stretched to its new size.
enum { ICH = 256, IMAXC = 64, IENT = 16 };
struct IronEntry { unsigned key; int W, H, cx, cy; u32 tex[IMAXC]; DWORD used; int n; float r[16][4]; float ox, oy; };
IronEntry g_ie[IENT];
void iron_free(IronEntry& e, bool release) {
    for (int i = 0; i < IMAXC; ++i) { if (release && e.tex[i]) release_texture(e.tex[i]); e.tex[i] = 0; }
    e.key = 0; e.cx = e.cy = 0; e.n = 0;
}
unsigned iron_key(const MenuGroup& g, float b) {
    unsigned h = 2166136261u;
    auto mix = [&](float v) { h = (h ^ (unsigned)(int)floorf(v * 4.0f + 0.5f)) * 16777619u; };
    mix((float)g_istyle); mix(b); mix(g.W); mix(g.H); mix(g.ox); mix(g.oy); mix((float)g.n); mix((float)g.np); mix((float)g.nc);
    for (int i = 0; i < g.n; ++i) for (int j = 0; j < 4; ++j) mix(g.r[i][j]);
    for (int i = 0; i < g.np; ++i) for (int j = 0; j < 5; ++j) mix(g.p[i][j]);
    for (int i = 0; i < g.nc; ++i) for (int j = 0; j < 5; ++j) mix(g.c[i][j]);
    return h ? h : 1u;
}
// the worker : one job at a time. state 0 idle, 1 the worker owns the job, 2 done (the main thread takes the result)
struct IronJob { unsigned key; MenuGroup g; float b; int W, H; u32* buf; int sup; bool ok; DWORD ms; int style; };
IronJob g_job;
volatile LONG g_jobState = 0, g_quit = 0;
HANDLE g_thr = 0, g_evt = 0;
DWORD g_ifailUntil = 0; int g_ibakesWin = 0; DWORD g_iwin = 0;
DWORD WINAPI iron_worker(LPVOID) {
    for (;;) {
        WaitForSingleObject(g_evt, INFINITE);
        if (g_quit) return 0;
        if (g_jobState != 1) continue;
        const DWORD t0 = GetTickCount();
        IronJob& j = g_job;
        j.buf = (u32*)HeapAlloc(GetProcessHeap(), 0, sizeof(u32) * (size_t)j.W * (size_t)j.H);
        const BakeShape s = { j.W, j.H, j.b, j.b / 11.0f, j.g.n, j.g.r, j.g.np, j.g.p, j.g.nc, j.g.c, j.g.ox, j.g.oy, j.style };
        j.sup = 0; j.ok = j.buf && bake_iron(s, j.buf, &j.sup);
        j.ms = GetTickCount() - t0;
        InterlockedExchange(&g_jobState, 2);
    }
}
bool iron_thread() {
    if (g_thr) return true;
    if (!g_evt) g_evt = CreateEventA(0, FALSE, FALSE, 0);
    g_quit = 0;
    if (g_evt) g_thr = CreateThread(0, 0, iron_worker, 0, 0, 0);
    if (!g_thr) windower::debug::log("IRONBAKE no worker thread (err %lu) -- the iron frame is off", GetLastError());
    return g_thr != 0;
}
void iron_thread_stop() {
    if (!g_thr) return;
    InterlockedExchange(&g_quit, 1); SetEvent(g_evt);
    if (WaitForSingleObject(g_thr, 5000) != WAIT_OBJECT_0) windower::debug::log("IRONBAKE the worker did not stop in 5 s");
    CloseHandle(g_thr); g_thr = 0; if (g_evt) { CloseHandle(g_evt); g_evt = 0; }
    if (g_jobState == 2 && g_job.buf) { HeapFree(GetProcessHeap(), 0, g_job.buf); g_job.buf = 0; }
    g_jobState = 0;
}
// a finished job -> its tiles (the main thread : D3D) into the least recently used entry ; logged either way (rule 10)
void iron_land(u32 dev) {
    if (g_jobState != 2) return;
    IronJob& j = g_job;
    const int cx = (j.W + ICH - 1) / ICH, cy = (j.H + ICH - 1) / ICH;
    IronEntry* v = &g_ie[0];
    for (auto& e : g_ie) { if (!e.key) { v = &e; break; } if (e.used < v->used) v = &e; }
    iron_free(*v, true);
    bool ok = j.ok; int made = 0;
    if (ok) {
        static u32 tile[ICH * ICH];
        for (int ty = 0; ty < cy && ok; ++ty) for (int tx = 0; tx < cx && ok; ++tx) {
            for (int y = 0; y < ICH; ++y) for (int x = 0; x < ICH; ++x) {
                const int gx = tx * ICH + x, gy = ty * ICH + y;
                tile[y * ICH + x] = gx < j.W && gy < j.H ? j.buf[gy * j.W + gx] : 0u;
            }
            u32& t = v->tex[ty * cx + tx]; t = make_texture_argb(dev, ICH, ICH, tile);
            if (t) ++made; else ok = false;
        }
    }
    const DWORD now = GetTickCount();
    if (!ok) {
        iron_free(*v, true); g_ifailUntil = now + 500;
        windower::debug::log("IRONBAKE FAILED %dx%d : %s (%d / %d tiles) -- retry in 0.5 s", j.W, j.H, j.buf ? (j.ok ? "a texture could not be made" : "the bake failed") : "no scratch memory", made, cx * cy);
    } else {
        v->key = j.key; v->W = j.W; v->H = j.H; v->cx = cx; v->cy = cy; v->used = now; v->n = j.g.n;
        memcpy(v->r, j.g.r, sizeof(v->r)); v->ox = j.g.ox; v->oy = j.g.oy;
        windower::debug::log("IRONBAKE %dx%d : %d box(es), %d plate(s), %d slot(s), border %d, %dx super : %lu ms (worker), %d tile(s)",
            j.W, j.H, j.g.n, j.g.np, j.g.nc, (int)j.b, j.sup, j.ms, cx * cy);
    }
    if (j.buf) { HeapFree(GetProcessHeap(), 0, j.buf); j.buf = 0; }
    InterlockedExchange(&g_jobState, 0);
}
IronEntry* iron_get(u32 dev, const MenuGroup& g, float b) {
    iron_land(dev);
    const unsigned key = iron_key(g, b); const DWORD now = GetTickCount();
    for (auto& e : g_ie) if (e.key == key) { e.used = now; return &e; }
    if (g_jobState != 0 || (int)(now - g_ifailUntil) < 0 || !iron_thread()) return nullptr;
    const int W = (int)g.W, H = (int)g.H, cx = (W + ICH - 1) / ICH, cy = (H + ICH - 1) / ICH;
    if (W <= 0 || H <= 0 || cx * cy > IMAXC) {
        windower::debug::log("IRONBAKE refused : group %dx%d (> %d tiles)", W, H, IMAXC); g_ifailUntil = now + 2000; return nullptr; }
    if ((int)(now - g_iwin) > 1000) { if (g_ibakesWin > 10) windower::debug::log("IRONBAKE %d bakes in the last second -- a layout changing every frame ?", g_ibakesWin); g_iwin = now; g_ibakesWin = 0; }
    ++g_ibakesWin;
    g_job.key = key; g_job.g = g; g_job.b = b; g_job.style = g_istyle; g_job.W = W; g_job.H = H; g_job.buf = 0; g_job.ok = false;
    InterlockedExchange(&g_jobState, 1); SetEvent(g_evt);
    return nullptr;
}
// draw the rect (sx, sy, sw, sh) of an entry's plate at (X, Y) size (w, h) : 1:1 when the sizes match, stretched while waiting
void iron_blit(u32 dev, const IronEntry& e, int sx, int sy, int sw, int sh, float X, float Y, float w, float h, u32 c) {
    const float fx = w / (float)sw, fy = h / (float)sh, inv = 1.0f / (float)ICH;
    for (int ty = sy / ICH; ty < e.cy && ty * ICH < sy + sh; ++ty) for (int tx = sx / ICH; tx < e.cx && tx * ICH < sx + sw; ++tx) {
        const u32 t = e.tex[ty * e.cx + tx]; if (!t) continue;
        const int x0 = sx > tx * ICH ? sx : tx * ICH, x1 = sx + sw < (tx + 1) * ICH ? sx + sw : (tx + 1) * ICH;
        const int y0 = sy > ty * ICH ? sy : ty * ICH, y1 = sy + sh < (ty + 1) * ICH ? sy + sh : (ty + 1) * ICH;
        if (x1 <= x0 || y1 <= y0) continue;
        dTexQuadState(dev, t);
        tquad(dev, X + (float)(x0 - sx) * fx, Y + (float)(y0 - sy) * fy, (float)(x1 - x0) * fx, (float)(y1 - y0) * fy,
              (float)(x0 - tx * ICH) * inv, (float)(x1 - tx * ICH) * inv, (float)(y0 - ty * ICH) * inv, (float)(y1 - ty * ICH) * inv, c, c);
    }
}
// per drawn box (found by its screen place) : the plate key it shows now and the one before -- the cross-fade
struct IronBox { float x, y; unsigned cur, prev; DWORD sw, seen; float alpha; bool out; };   // out : fading OUT (closing)
IronBox g_ibox[24];
IronBox& iron_box(float X, float Y, DWORD now) {
    IronBox* v = &g_ibox[0];
    for (auto& b : g_ibox) {
        if ((int)(now - b.seen) < 1000 && fabsf(b.x - X) + fabsf(b.y - Y) < 12.0f) { b.x = X; b.y = Y; b.seen = now; return b; }
        if (b.seen < v->seen) v = &b;
    }
    v->x = X; v->y = Y; v->seen = now; v->cur = v->prev = 0; v->sw = now; v->alpha = -1.0f; v->out = false;
    return *v;
}
// a box drawn at (X, Y) this frame or the last one, else null
const IronBox* iron_box_find(float X, float Y, DWORD now) {
    for (const auto& b : g_ibox) if ((int)(now - b.seen) < 200 && fabsf(b.x - X) + fabsf(b.y - Y) < 12.0f) return &b;
    return nullptr;
}
// a neighbour FADING OUT (a closing Profile, the folding item card) leaves the group AT ONCE : the boxes staying show
// their plate without its joints while it fades with its own (2026-11-20 : "les morceaux de jointure restent une
// seconde avant de depop"). Rebuilt around `self` : what stays connected, its new origin, the slots of the boxes kept
void iron_drop_fading(MenuGroup& g, DWORD now) {
    bool drop[16] = {}; int nd = 0;
    for (int i = 0; i < g.n; ++i) { if (i == g.self) continue;
        const IronBox* b = iron_box_find(g.ox + g.r[i][0], g.oy + g.r[i][1], now);
        if (b && b->out) { drop[i] = true; ++nd; } }
    if (!nd) return;
    int keep[16], nk = 0; bool in[16] = {}; keep[nk++] = g.self; in[g.self] = true;
    for (int q = 0; q < nk; ++q) for (int j = 0; j < g.n; ++j) {
        if (in[j] || drop[j]) continue;
        const float* a = g.r[keep[q]]; const float* c = g.r[j];
        const float ox = (a[0] + a[2] < c[0] + c[2] ? a[0] + a[2] : c[0] + c[2]) - (a[0] > c[0] ? a[0] : c[0]);
        const float oy = (a[1] + a[3] < c[1] + c[3] ? a[1] + a[3] : c[1] + c[3]) - (a[1] > c[1] ? a[1] : c[1]);
        if (ox > 0.5f && oy > 0.5f) { in[j] = true; keep[nk++] = j; }
    }
    for (int a = 1; a < nk; ++a) for (int b = a; b > 0; --b) {   // the same order as menu_group_lookup : one key per shape
        const float *p = g.r[keep[b - 1]], *q = g.r[keep[b]];
        if (q[1] < p[1] - 0.5f || (fabsf(q[1] - p[1]) <= 0.5f && q[0] < p[0])) { const int t = keep[b]; keep[b] = keep[b - 1]; keep[b - 1] = t; } else break;
    }
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (int q = 0; q < nk; ++q) { const float* r = g.r[keep[q]];
        if (r[0] < x0) x0 = r[0]; if (r[1] < y0) y0 = r[1]; if (r[0] + r[2] > x1) x1 = r[0] + r[2]; if (r[1] + r[3] > y1) y1 = r[1] + r[3]; }
    MenuGroup n; n.n = nk; n.self = 0; n.np = n.nc = 0;
    int map[16]; for (int i = 0; i < 16; ++i) map[i] = -1;
    for (int q = 0; q < nk; ++q) { const float* r = g.r[keep[q]]; map[keep[q]] = q; if (keep[q] == g.self) n.self = q;
        n.r[q][0] = r[0] - x0; n.r[q][1] = r[1] - y0; n.r[q][2] = r[2]; n.r[q][3] = r[3]; }
    for (int i = 0; i < g.np; ++i) if (map[g.po[i]] >= 0) { float* d = n.p[n.np]; n.po[n.np++] = map[g.po[i]];
        d[0] = g.p[i][0] - x0; d[1] = g.p[i][1] - y0; d[2] = g.p[i][2]; d[3] = g.p[i][3]; d[4] = g.p[i][4]; }
    for (int i = 0; i < g.nc; ++i) if (map[g.co[i]] >= 0) { float* d = n.c[n.nc]; n.co[n.nc++] = map[g.co[i]];
        d[0] = g.c[i][0] - x0; d[1] = g.c[i][1] - y0; d[2] = g.c[i][2]; d[3] = g.c[i][3]; d[4] = g.c[i][4]; }
    n.W = x1 - x0; n.H = y1 - y0; n.ox = g.ox + x0; n.oy = g.oy + y0;
    g = n;
}
// a box drawn from a plate of ANOTHER size : 1:1 everywhere (the lights are anchored to the screen, so the part that did
// not move matches the true plate pixel for pixel) -- the edge bands at their ends, the MIDDLE REPEATED, never stretched
// (stretched, a vertical bar's lights slid along it : "la description continue de changer d'eclairage", 2026-11-20)
int iron_segs(int s, int d, int band, int seg[][3]) {             // (source offset, length, dest offset) along one axis
    int bnd = band; if (bnd * 2 > s) bnd = s / 2; if (bnd * 2 > d) bnd = d / 2;
    int n = 0; seg[n][0] = 0; seg[n][1] = bnd; seg[n][2] = 0; ++n;
    const int sm = s - 2 * bnd, dm = d - 2 * bnd;
    for (int pos = 0; sm > 0 && pos < dm && n < 30; pos += sm) { seg[n][0] = bnd; seg[n][1] = dm - pos < sm ? dm - pos : sm; seg[n][2] = bnd + pos; ++n; }
    seg[n][0] = s - bnd; seg[n][1] = bnd; seg[n][2] = d - bnd; ++n;
    return n;
}
void iron_blit9(u32 dev, const IronEntry& e, int sx, int sy, int sw, int sh, float X, float Y, float w, float h, u32 c, int band) {
    const int dw = (int)(w + 0.5f), dh = (int)(h + 0.5f);
    if (dw == sw && dh == sh) { iron_blit(dev, e, sx, sy, sw, sh, X, Y, w, h, c); return; }
    int hs[32][3], vs[32][3];
    const int nh = iron_segs(sw, dw, band, hs), nv = iron_segs(sh, dh, band, vs);
    for (int j = 0; j < nv; ++j) for (int i = 0; i < nh; ++i) {
        if (hs[i][1] <= 0 || vs[j][1] <= 0) continue;
        iron_blit(dev, e, sx + hs[i][0], sy + vs[j][0], hs[i][1], vs[j][1], X + (float)hs[i][2], Y + (float)vs[j][2], (float)hs[i][1], (float)vs[j][1], c);
    }
}
bool iron_frame(u32 dev, float x, float y, float w, float h, u32 tint) {
    const float b = iron_border();
    if (w < 2.0f * b + 4.0f || h < 2.0f * b + 4.0f) return false;
    MenuGroup g;
    if (!menu_group_lookup(x, y, w, h, g)) {                         // not a menu box (or its first frame) : a group of one
        g.n = 1; g.self = 0; g.np = g.nc = 0;
        g.r[0][0] = 0.0f; g.r[0][1] = 0.0f; g.r[0][2] = floorf(w + 0.5f); g.r[0][3] = floorf(h + 0.5f); g.W = g.r[0][2]; g.H = g.r[0][3];
        g.ox = floorf(snap(x) + 0.5f); g.oy = floorf(snap(y) + 0.5f);
    }
    const u32 c = ((tint >> 24) << 24) | 0x00FFFFFFu;
    const float X = snap(x), Y = snap(y);
    {   const DWORD tnow = GetTickCount(); IronBox& me = iron_box(X, Y, tnow);
        const float a = (float)(tint >> 24) / 255.0f;
        if (me.alpha >= 0.0f) me.out = a < me.alpha - 0.001f || (me.out && a < 0.999f && a <= me.alpha);
        me.alpha = a;
        if (!me.out) iron_drop_fading(g, tnow); }                  // a box fading out keeps its group : it goes with it
    const int sx = (int)g.r[g.self][0], sy = (int)g.r[g.self][1], sw = (int)g.r[g.self][2];
    const float dw = floorf(w + 0.5f), dh = floorf(h + 0.5f);       // drawn at ITS size (mid-animation : shorter / taller than its plate)
    const int band = (int)(b + 8.0f);                                // the edge bands : the border + its relief
    const IronEntry* e = iron_get(dev, g, b);
    // WHICH PLATE : one with this box AT THIS SCREEN PLACE (the lights and the grain are the screen's -- a plate of another
    // place showed another texture, 2026-11-20), at its width, TALL ENOUGH to be CROPPED to the drawn height (the top and
    // the sides 1:1, the bottom band at the drawn bottom : the very pixels of the final plate). Its own plate first ;
    // else the shortest cached one that is tall enough ; else the closest (repeated middle) ; nothing : its own frame
    const float ax = g.ox + (float)sx, ay = g.oy + (float)sy;
    const IronEntry* d = nullptr; int dr = -1; bool tall = false; float dH = 0.0f;
    auto consider = [&](const IronEntry& k) {
        for (int i = 0; i < k.n; ++i) {
            if (fabsf(k.ox + k.r[i][0] - ax) > 1.5f || fabsf(k.oy + k.r[i][1] - ay) > 1.5f || fabsf(k.r[i][2] - (float)sw) > 1.5f) continue;
            const float hh = k.r[i][3]; const bool t = hh >= dh - 0.5f;
            const bool better = !d || (t && !tall) || (t == tall && (t ? hh < dH : hh > dH));
            if (better) { d = &k; dr = i; tall = t; dH = hh; }
            return;
        }
    };
    if (e) consider(*e);
    if (!d || !tall) for (const auto& k : g_ie) if (k.key && &k != e) consider(k);
    // MOVED (Edit Layout drags a box every frame : each place is a new bake, landing after the box has moved on -- the
    // box showed its ORIGINAL frame between two bakes, 2026-11-20 "ca alterne entre le theme d'origine et fer") : a plate
    // baked elsewhere with the same box count, the CLOSEST size (a resize too : iron_blit9 repeats its middle, the bands
    // stay 1:1) ; equal sizes : the most recently used. Its lights are those of another place -- a drag moves them anyway ;
    // the true plate takes over once it lands
    if (!d) {
        float bestD = 1e9f; DWORD bestU = 0;
        for (const auto& k : g_ie) {
            if (!k.key || k.n != g.n) continue;
            const int i = g.n == 1 ? 0 : g.self;                       // the same slot of the same-count group
            if (i >= k.n) continue;
            const float dd = fabsf(k.r[i][2] - (float)sw) + fabsf(k.r[i][3] - dh);
            if (!d || dd < bestD - 0.5f || (dd < bestD + 0.5f && k.used > bestU)) { d = &k; dr = i; tall = k.r[i][3] >= dh - 0.5f; dH = k.r[i][3]; bestD = dd; bestU = k.used; }
        }
    }
    if (!d) return false;                                            // nothing of this shape yet (the very first open)
    const DWORD now = GetTickCount();
    IronBox& ib = iron_box(X, Y, now);
    if (ib.cur != d->key) {
        // IRONSHOW : what this box shows from now on -- the transitions' truth
        windower::debug::log("IRONSHOW box %d,%d %dx%d : %s plate %08X (its rect %dx%d, %s)", (int)X, (int)Y, (int)dw, (int)dh,
            d == e ? "OWN" : "OTHER", d->key, (int)d->r[dr][2], (int)d->r[dr][3], tall ? "cropped" : "middle repeated");
        ib.prev = ib.cur; ib.cur = d->key; ib.sw = now;
    }
    iron_blit9(dev, *d, (int)d->r[dr][0], (int)d->r[dr][1], (int)d->r[dr][2], (int)d->r[dr][3], X, Y, dw, dh, c, band);
    dSetTex(dev, 0, 0); cs(dev);
    return true;
}
}   // namespace

// the frame hook (gfx/window.h) : the kit's frame in place of the box's skin. The pieces are 2x of the 1440p size ; a box
// too small for the corners' extents shrinks the whole frame to fit (and keeps its own frame below a third of the size)
bool kit_frame(u32 dev, float x, float y, float w, float h, u32 tint, bool /*openBottom*/) {
    const int jm = g_frameJoin; g_frameJoin = 0;                    // consumed on every path : only this box's frame call carries it
    if (!g_on) return false;
    if (g_kit == 2) { g_frameAccent = false; return iron_frame(dev, x, y, w, h, tint); }
    if (g_kit == 1) {                                               // a tileable kit : game-scale pieces (1 px = 1 px at 1440 lines)
        if (!load_kit(dev)) return false;
        const bool accent = g_frameAccent; g_frameAccent = false;
        const float k = g_screenH / 1440.0f;
        if (w < 2.0f * g_kc * k + 4.0f || h < 2.0f * g_kc * k + 4.0f) return false;   // too small : its own frame
        const u32 c = ((tint >> 24) << 24) | 0x00FFFFFFu;
        // the box reserved the border (frame_inset) : the frame is drawn INSIDE its rect
        frame_kit(dev, snap(x), snap(y), snap(w), snap(h), c, k, accent, jm);
        dSetTex(dev, 0, 0); cs(dev);
        return true;
    }
    if (!load(dev)) return false;
    float k = 0.5f * (g_screenH / 1440.0f);
    const bool accent = g_frameAccent; g_frameAccent = false;      // consumed : only the party's own frame call carries it
    const int TLI = accent ? CTL : CTLP;
    const float needW = (float)(P[TLI].ex + P[CTR].ex) + 8.0f, needH = (float)(P[TLI].ey + P[CBL].ey) + 8.0f;
    const float kw = w / needW, kh = h / needH;
    if (kw < k) k = kw; if (kh < k) k = kh;
    if (k < 0.5f * (g_screenH / 1440.0f) * 0.33f) return false;
    const u32 a = tint >> 24, c = (a << 24) | 0x00FFFFFFu;
    // the box reserved the border (frame_inset) : the frame is drawn INSIDE its rect
    frame(dev, x, y, w, h, c, k, accent);
    dSetTex(dev, 0, 0); cs(dev);
    return true;
}
// the kits' textures : FORGET on a device change (the old device may be dead -- rule 4), RELEASE at shutdown (else they leak per //unload)
void theme_kit_forget() {
    for (auto& p : P) p.tex = 0; for (auto& p : K) p.tex = 0;
    for (auto& e : g_ie) iron_free(e, false);
    g_loaded = g_bloaded = false; g_tries = g_btries = 0;
}
void theme_kit_dispose() {
    for (auto& p : P) if (p.tex) { release_texture(p.tex); p.tex = 0; }
    for (auto& p : K) if (p.tex) { release_texture(p.tex); p.tex = 0; }
    iron_thread_stop();                                              // the bake worker first : nothing may run after an unload
    for (auto& e : g_ie) iron_free(e, true);
    g_loaded = g_bloaded = false; g_frameOverride = 0; g_frameInset = 0.0f; g_on = false;
}
bool theme_kit_bakes_slots() { return g_on && g_kit == 2; }
bool theme_kit_test_toggle(const char* id) {
    const bool fer = id && !_stricmp(id, "fer");
    const int kit = !(id && *id) ? 0 : (!_stricmp(id, "iron") || fer ? 2 : 1);
    if (kit == 2) { if (g_on && g_kit == 2 && g_istyle != (fer ? 3 : 0)) g_on = false; g_istyle = fer ? 3 : 0; }   // iron <-> fer : a switch, not off
    if (g_on && (kit != g_kit || (kit == 1 && _stricmp(id, g_kid)))) g_on = false;   // another kit asked : switch to it
    g_kit = kit; if (kit == 1) { _snprintf(g_kid, sizeof(g_kid), "%s", id); g_kid[sizeof(g_kid) - 1] = 0; }
    g_on = !g_on; if (g_on) { g_tries = 0; g_loaded = false; g_btries = 0; g_bloaded = false; }
    g_frameOverride = g_on ? kit_frame : 0;
    return g_on;
}
// called once a frame (hud.cpp) : the screen height the frame scale follows
void theme_kit_test_draw(const Frame& f) {
    if (f.screenH > 0.0f) g_screenH = f.screenH;
    // the border every box reserves next frame : the kit's own thickness at this screen's scale (0 = the normal frames)
    if (!g_on) g_frameInset = 0.0f;
    else if (g_kit == 1) g_frameInset = snap((float)g_kb * (g_screenH / 1440.0f));
    else if (g_kit == 2) g_frameInset = iron_border();
    else g_frameInset = snap((P[ET].h > 0 ? (float)P[ET].h : 36.0f) * 0.5f * (g_screenH / 1440.0f));
}

}   // namespace aio
