// hud_grimoire.cpp -- split out of hud.cpp (pure move). Scholar Grimoire box renderer.
#include "ui/hud.h"
#include "ui/hud_internal.h"
#include "model/ui_config.h"
#include "ui/text_style.h"
#include "ui/box_style.h"
#include "model/party_state.h"
#include "gfx/draw.h"
#include "gfx/d3d.h"
#include "model/paths.h"
#include "gfx/texture.h"
#include "model/gamestate.h"
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>

namespace aio {

// ============================ SCHOLAR GRIMOIRE box ============================
static const char* GRIM_LIGHT_PATH() { static char b[260]; if (!b[0]) plugin_path(b, 260, "assets\\grimoire_light.raw"); return b; }
static const char* GRIM_DARK_PATH()  { static char b[260]; if (!b[0]) plugin_path(b, 260, "assets\\grimoire_dark.raw"); return b; }
static const char* GRIM_CLOSED_PATH(){ static char b[260]; if (!b[0]) plugin_path(b, 260, "assets\\grimoire_closed.raw"); return b; }
// draw the book texture (512x342 straight-alpha) at (x,y,w,h) tinted by `col` (alpha dims an inactive book).
static void grim_draw_book(u32 dev, u32 tex, float x, float y, float w, float h, u32 col) {
    dSetVS(dev, FVF_XYZRHW_DIFFUSE_TEX1);
    dSetRS(dev, D3DRS_ALPHABLENDENABLE, 1);
    dSetRS(dev, D3DRS_SRCBLEND, D3DBLEND_SRCALPHA); dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    dSetTex(dev, 0, tex);
    dSetTSS(dev, 0, D3DTSS_COLOROP, D3DTOP_MODULATE); dSetTSS(dev, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE); dSetTSS(dev, 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    dSetTSS(dev, 0, D3DTSS_ALPHAOP, D3DTOP_MODULATE); dSetTSS(dev, 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE); dSetTSS(dev, 0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    dSetTSS(dev, 0, D3DTSS_MINFILTER, D3DTEXF_LINEAR); dSetTSS(dev, 0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR); dSetTSS(dev, 0, D3DTSS_MIPFILTER, D3DTEXF_NONE);
    tquad(dev, x, y, w, h, 0.0f, 1.0f, 0.0f, 1.0f, col, col);
    dSetTex(dev, 0, 0);
}
// per-element typography for the grimoire numbers (Charge / Timer).
static Font* grim_font(const Frame& f, int e) { return te_font(f, ui_config().grimText[e]); }
static inline float grim_sz(int e, float base) { return te_sz(ui_config().grimText[e], base); }
static inline float grim_ow(int e, float base) { return te_ow(ui_config().grimText[e], base); }
static inline u32   grim_col(int e, u32 base)  { return te_col(ui_config().grimText[e], base); }
// a "pastille" number token (dark rounded pill + art-coloured rim + cream text), centred on (cx,cy).
static void grim_pastille(u32 dev, Font* fo, float cx, float cy, const char* s, float z, float ow, u32 artCol, u32 txtCol, float S) {
    const float tw = fo->measure(s, z), ph = z + 4.0f * S, pw = tw + 12.0f * S, r = ph * 0.5f;
    const float x = cx - pw * 0.5f, y = cy - ph * 0.5f;
    dColorQuadState(dev);
    rrect(dev, x, y, pw, ph, r, 0xD10A0E1Cu, 0xD1060812u, 1.0f);      // dark pill (rgba 10,14,28,.82)
    rrect_stroke(dev, x, y, pw, ph, r, artCol, 1.0f * S);             // art-tinted rim
    fo->begin(dev); fo->draw_c(dev, cx, cy, s, z, txtCol, 0xC0000000u, ow);
}

// The Scholar grimoire : the Light/Dark book texture + a pulsing Addendum aura + charge/recast pastilles. Shown only
// for a SCH main/sub (GrimoireState from the poller) ; a sample (Addendum White) in preview/edit. //aio edit places it.
void Hud::draw_grimoire(const Frame& f, bool preview, float ovX, float ovY, float ovS) {
    const UiConfig& C = ui_config();
    if (!C.grimShow) return;
    const bool editing = C.editLayout && !preview;
    u32 dev = f.dev;
    if (!f.font && !f.fonts) return;

    int book; bool addendum, dim, closed; int charges, timerSec;
    if (preview || editing) {
        int art = (C.grimArt < 0 || C.grimArt > 3) ? 2 : C.grimArt;
        book = (art == 1 || art == 3) ? 1 : 0; addendum = (art >= 2); dim = false; closed = false; charges = 4; timerSec = 20;
    } else {
        if (!f.game) return;
        const GrimoireState& g = f.game->grimoire;
        if (!g.visible) return;
        book = g.book; addendum = g.addendum; dim = g.dim; closed = g.closed; charges = g.charges; timerSec = g.timerSec;
    }

    if (!grimTried_) { grimLight_ = load_raw_texture(dev, GRIM_LIGHT_PATH(), 512, 342); grimDark_ = load_raw_texture(dev, GRIM_DARK_PATH(), 512, 342); grimClosed_ = load_raw_texture(dev, GRIM_CLOSED_PATH(), 512, 342); grimTried_ = true; }
    u32 tex = closed ? grimClosed_ : (book ? grimDark_ : grimLight_);
    if (closed && !tex) { closed = false; tex = grimLight_; }   // closed art missing -> fall back to the dim light book
    if (!tex) return;

    float sscl = C.grimScale; if (sscl < 0.5f) sscl = 0.5f; if (sscl > 2.0f) sscl = 2.0f;
    const float S = (ovS > 0.0f) ? ovS : (screenH_ / 1000.0f) * sscl;
    const float bookH = 102.0f * S, bookW = bookH * (512.0f / 342.0f);
    const float auraPad = 11.0f * S;                       // the box (snap rect) includes the aura halo
    const float boxW = bookW + 2.0f * auraPad, boxH = bookH + 2.0f * auraPad;

    float px, py;
    if (ovS > 0.0f) { px = snap((ovX - boxW * 0.5f)); py = snap((ovY - boxH * 0.5f)); }
    else            { px = snap(C.grimX * screenW_ - boxW * 0.5f); py = snap(C.grimY * screenH_); }
    if (editing) { static EditBox g_grimEdit; box_edit(f, g_grimEdit, EDITBOX_GRIMOIRE, px, py, boxW, boxH, ui_config().grimScale, ui_config().grimX, ui_config().grimY, 1); }

    const float bx = px + auraPad, by = py + auraPad;      // book quad
    const u32 artCol = book ? 0xFFB98CFFu : 0xFFFFD766u;   // Dark=purple / Light=gold

    // ---- Addendum aura : a FILLED soft glow behind the book (not a hollow ring -- a ring left an empty gap
    // around the book when the texture's transparent margin pushed its peak off the visible edge). A feathered
    // rrect, solid in the centre (covered by the book) and fading outward, so the glow emerges continuously right
    // at the book edge on every side. Additive so only the outward halo + the through-margin bleed show. ----
    if (addendum) {
        float ph = f.t / 2.8f; ph -= (float)(int)ph;                          // 0..1 over 2.8 s
        const float tri = ph < 0.5f ? ph * 2.0f : (1.0f - ph) * 2.0f;         // 0..1..0 breathing
        const u32 aa = (u32)((0.34f + 0.24f * tri) * 255.0f);
        const u32 col = (artCol & 0x00FFFFFFu) | (aa << 24);
        const float g = 1.5f * S;                                             // how far the solid glow bleeds past the book before the feather
        dColorQuadState(dev);
        dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_ONE);                           // additive
        rrect(dev, bx - g, by - g, bookW + 2.0f * g, bookH + 2.0f * g, 6.0f * S, col, col, 9.0f * S);
        dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);                   // restore blend for the next draw
    }

    // ---- the book ----
    grim_draw_book(dev, tex, bx, by, bookW, bookH, dim ? 0x90FFFFFFu : 0xFFFFFFFFu);

    // ---- charge (left page) + recast timer (right page) pastilles ---- (a CLOSED book = no Arts -> no stratagem UI)
    if (closed) return;
    char cb[8], tb[8];
    sprintf(cb, "%d", charges < 0 ? 0 : charges);
    Font* fC = grim_font(f, GRIM_CHARGE); const float zC = grim_sz(GRIM_CHARGE, 14.0f) * S, oC = grim_ow(GRIM_CHARGE, 1.0f) * S;
    grim_pastille(dev, fC, bx + bookW * 0.17f, by + bookH * 0.78f, cb, zC, oC, artCol, grim_col(GRIM_CHARGE, 0xFFFFF4D6u), S);
    if (timerSec >= 0) {
        sprintf(tb, "%02d", timerSec > 99 ? 99 : timerSec);
        Font* fT = grim_font(f, GRIM_TIMER); const float zT = grim_sz(GRIM_TIMER, 14.0f) * S, oT = grim_ow(GRIM_TIMER, 1.0f) * S;
        grim_pastille(dev, fT, bx + bookW * 0.81f, by + bookH * 0.78f, tb, zT, oT, artCol, grim_col(GRIM_TIMER, 0xFFFFF4D6u), S);
    }
}

} // namespace aio
