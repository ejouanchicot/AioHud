// pet_common.cpp -- the helpers the Pet box's job contents share (pet_common.h). Moved here from hud_bst.cpp and
// hud_geo.cpp, which each had a copy (docs/design/pet-module-spec.md §2.2).
#include "ui/pet_common.h"
#include "gfx/draw.h"
#include "gfx/font.h"
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

} // namespace aio
