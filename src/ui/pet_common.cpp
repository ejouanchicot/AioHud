// pet_common.cpp -- the helpers the Pet box's job contents share (pet_common.h). Moved here from hud_bst.cpp and
// hud_geo.cpp, which each had a copy (docs/design/pet-module-spec.md §2.2) ; the element colour, the time format and
// the recast share moved from hud_geo.cpp when hud_smn.cpp needed them too (docs/design/smn-module-spec.md).
#include "ui/pet_common.h"
#include "gfx/draw.h"
#include "gfx/font.h"
#include "ui/palette.h"   // pet_tp_col : the player's TP palette
#include "ui/buff_atlas.h" // pet_ja_grid : the abilities' status icons
#include "ui/hud_internal.h"   // draw_icon_cell
#include <stdio.h>
#include <string.h>

namespace aio {

EditBox& pet_edit_box() { static EditBox eb; return eb; }

void pet_tex_quad(u32 dev, u32 tex, float x, float y, float w, float h, float v0, float v1, u32 col) {
    dSetVS(dev, FVF_XYZRHW_DIFFUSE_TEX1);
    dSetRS(dev, D3DRS_ALPHABLENDENABLE, 1);
    dSetRS(dev, D3DRS_SRCBLEND, D3DBLEND_SRCALPHA); dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    dSetTex(dev, 0, tex);
    dSetTSS(dev, 0, D3DTSS_COLOROP, D3DTOP_MODULATE); dSetTSS(dev, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE); dSetTSS(dev, 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    dSetTSS(dev, 0, D3DTSS_ALPHAOP, D3DTOP_MODULATE); dSetTSS(dev, 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE); dSetTSS(dev, 0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    dSetTSS(dev, 0, D3DTSS_MINFILTER, D3DTEXF_LINEAR); dSetTSS(dev, 0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR); dSetTSS(dev, 0, D3DTSS_MIPFILTER, D3DTEXF_NONE);
    tquad(dev, x, y, w, h, 0.0f, 1.0f, v0, v1, col, col);
    dSetTex(dev, 0, 0);
}

void pet_gauge(u32 dev, float x, float y, float w, float h, float frac, u32 fill) {
    if (frac < 0.0f) frac = 0.0f; if (frac > 1.0f) frac = 1.0f;
    dColorQuadState(dev);
    rrect(dev, x, y, w, h, h * 0.5f, 0xC0141A28u, 0xC00C101Au, 1.0f);
    if (frac > 0.0f) rrect(dev, x, y, snap(w * frac), h, h * 0.5f, fill, (fill & 0x00FFFFFFu) | 0xB0000000u, 1.0f);
}

float pet_fit_text(Font* fo, const char* s, float z, float maxW, char* out, int cap) {
    strncpy(out, s, cap - 1); out[cap - 1] = 0;
    if (maxW <= 0.0f) return z;
    static const float STEPS[3] = { 1.0f, 0.9f, 0.8f };
    for (int i = 0; i < 3; ++i) if (fo->measure(out, z * STEPS[i]) <= maxW) return z * STEPS[i];
    const float zs = z * STEPS[2];
    int n = (int)strlen(out);
    while (n > 1) { out[--n] = 0; out[n - 1] = '.'; if (fo->measure(out, zs) <= maxW) break; }
    return zs;
}

void pet_fit_cut(Font* fo, const char* s, float z, float maxW, char* out, int cap) {
    strncpy(out, s, cap - 1); out[cap - 1] = 0;
    int n = (int)strlen(out);
    while (n > 1 && fo->measure(out, z) > maxW) { out[--n] = 0; out[n - 1] = '.'; }
}

u32 pet_elem_col(int e) {
    static const u32 C[8] = { 0xFFFF7A5Cu, 0xFF9BE7FFu, 0xFF7CE08Au, 0xFFD8B25Au, 0xFFC79BFFu, 0xFF6FB6FFu, 0xFFFFF4C0u, 0xFF9C7BD9u };
    return (e >= 0 && e < 8) ? C[e] : 0xFFE8EEF2u;
}

void pet_fmt_left(int sec, char* out, int cap) {
    if (sec < 0)          _snprintf(out, cap, "--");
    else if (sec >= 3600) _snprintf(out, cap, "%dh%02d", sec / 3600, (sec / 60) % 60);
    else if (sec >= 60)   _snprintf(out, cap, "%d:%02d", sec / 60, sec % 60);
    else                  _snprintf(out, cap, "%ds", sec);
    out[cap - 1] = 0;
}

u32 pet_tp_col(int tp) {
    u32 p[3]; tp_palette((float)(tp < 0 ? 0 : (tp > 3000 ? 3000 : tp)) / 3000.0f, p);
    return (p[1] & 0x00FFFFFFu) | 0xFF000000u;   // the palette's mid tone, opaque : an arc and a gauge are thin
}

float pet_ja_grid(u32 dev, Font* fj, float zj, float oj, u32 nameCol, float x, float y, float w, float cellH,
                  float iconSz, float S, const PetJa* const* jas, int n, int cols, bool sample) {
    if (n <= 0 || cols <= 0) return 0.0f;
    const u32 stex = buff_atlas_tex(dev);   // shared, never released here (buff_atlas.h) ; 0 while it loads -> no icon
    const float cw = w / (float)cols, cpad = snap(3.0f * S);
    const float gi = snap(iconSz > cellH - 2.0f ? cellH - 2.0f : iconSz);
    for (int i = 0; i < n; ++i) {
        const PetJa& j = *jas[i];
        const float cx = snap(x + cw * (float)(i % cols)), cy = y + cellH * ((float)(i / cols) + 0.5f);
        if (j.active) {
            dColorQuadState(dev);
            rrect(dev, cx, snap(cy - cellH * 0.5f + 1.0f), cw - snap(2.0f * S), cellH - 2.0f, snap(3.0f * S), 0x60FFC864u, 0x40FFC864u, 1.0f);
        }
        float lx = cx + cpad;
        if (j.status && stex && j.status < BUFF_COLS * BUFF_ATLAS_ROWS) {
            float au, av, u0, v0; buff_cell_uv(j.status, au, av, u0, v0);
            const float iy = snap(cy - gi * 0.5f);
            draw_icon_cell(dev, stex, lx, iy, gi, gi, u0, u0 + au, v0, v0 + av);
            if (j.recastSec > 0) {
                const float dark = snap(gi * pet_recast_share(j.abil, j.recastSec, sample));
                dColorQuadState(dev);
                if (dark > 0.0f) rrect(dev, lx, iy, gi, dark, 0.0f, 0xA0101420u, 0xA0101420u, 0.0f);
            }
            lx += gi + snap(3.0f * S);
        }
        char rb[12]; if (j.recastSec > 0) pet_fmt_left(j.recastSec, rb, sizeof(rb)); else strcpy(rb, "ok");
        const float rx = cx + cw - snap(2.0f * S) - cpad - fj->measure(rb, zj);
        char lb[24]; pet_fit_cut(fj, j.label, zj, rx - snap(3.0f * S) - lx, lb, sizeof(lb));
        fj->begin(dev);
        fj->draw_lc(dev, lx, cy, lb, zj, j.active ? 0xFFFFC864u : nameCol, 0xC0000000u, oj);
        fj->draw_lc(dev, rx, cy, rb, zj, j.recastSec > 0 ? 0xFF8C94A0u : 0xFF9BE89Bu, 0xC0000000u, oj);
    }
    return cellH * (float)((n + cols - 1) / cols);
}

float pet_recast_share(unsigned short abil, int sec, bool sample) {
    if (sample) return sec > 0 ? 0.5f : 0.0f;
    static const int N = 32;   // the GEO and SMN abilities together, with room ; ids never collide between jobs
    static unsigned short ids[N] = {}; static int longest[N] = {};
    int k = 0;
    while (k < N && ids[k] && ids[k] != abil) ++k;
    if (k == N) return sec > 0 ? 1.0f : 0.0f;
    ids[k] = abil;
    if (sec > longest[k]) longest[k] = sec;
    return (sec > 0 && longest[k] > 0) ? (float)sec / (float)longest[k] : 0.0f;
}

} // namespace aio
