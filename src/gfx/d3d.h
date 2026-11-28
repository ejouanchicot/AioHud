// d3d.h -- direct IDirect3DDevice8 access (the rendering backend).
//
// AioHUD draws by talking to the game's D3D8 device directly (host service 2),
// NOT through Windower's primitive API -- that's what unlocks UV scroll, gradients
// and arbitrary geometry (see docs/reference/d3d8-rendering.md "VERIFIED WORKING"). This header
// is the lowest layer: the D3D8 constants we use, our vertex formats, and thin
// typed wrappers around the device vtable methods (indices reversed from Hook.dll).
//
// Nothing here knows about the HUD -- it is pure GPU plumbing.
#pragma once
#include "windower.h"
#include "perf.h"   // //aio perf : the counters below (one byte test when off)
#include <string.h> // memset : the state shadow

namespace aio {

using windower::u32;
using windower::vmethod;
using windower::valid_ptr;

// ---- D3D8 constants (the subset we use) ----
enum {
    D3DPT_TRIANGLELIST = 4, D3DPT_TRIANGLESTRIP = 5, D3DPT_TRIANGLEFAN = 6,
    FVF_XYZRHW_DIFFUSE      = 0x44,    // VtxC : pos + diffuse        (glass / gradients)
    FVF_XYZRHW_DIFFUSE_TEX1 = 0x144,   // Vtx  : pos + diffuse + uv    (liquid / textures)
    D3DSBT_ALL = 1, D3DFMT_A8R8G8B8 = 21, D3DPOOL_MANAGED = 1,
    D3DRS_ZENABLE = 7, D3DRS_SRCBLEND = 19, D3DRS_DESTBLEND = 20, D3DRS_CULLMODE = 22,
    D3DRS_ALPHABLENDENABLE = 27, D3DRS_LIGHTING = 137,
    D3DRS_ALPHATESTENABLE = 15, D3DRS_FOGENABLE = 28, D3DRS_SPECULARENABLE = 29,
    D3DRS_COLORVERTEX = 141, D3DRS_COLORWRITEENABLE = 168, D3DRS_TEXTUREFACTOR = 60,
    D3DRS_WRAP0 = 128, D3DRS_BLENDOP = 171, D3DBLENDOP_ADD = 1,
    // Two states nothing here ever set, so both were whatever FFXI happened to leave behind:
    //   SHADEMODE -- FLAT shades a triangle with ONE vertex's colour. Every gradient we draw is a quad made of
    //     two triangles, so a stray FLAT is a hard break along the diagonal, and it appears and disappears with
    //     whatever the game drew last. GOURAUD is what interpolating vertex colours requires ; state we depend
    //     on has to be state we set.
    //   DITHERENABLE -- an ordered dither on the way to the framebuffer. It is the fixed-function answer to
    //     8-bit banding, and the only one there is without shaders: a long slow ramp changes level less than
    //     once per pixel, and the eye locks onto those steps as bands (Mach banding sharpens them further).
    //     Dithering trades that for noise below the level of a single bit. It matters even more if the game is
    //     running a 16-bit backbuffer, where a smooth ramp bands violently.
    D3DRS_SHADEMODE = 9, D3DSHADE_GOURAUD = 2, D3DRS_DITHERENABLE = 26,
    D3DTSS_TEXCOORDINDEX = 11, D3DTSS_TEXTURETRANSFORMFLAGS = 24, D3DTTFF_DISABLE = 0,
    D3DCULL_NONE = 1, D3DBLEND_ZERO = 1, D3DBLEND_ONE = 2, D3DBLEND_SRCALPHA = 5, D3DBLEND_INVSRCALPHA = 6,
    // src * dst + dst = dst * (1 + src) : brighten by a FACTOR instead of by an amount, so light and dark
    // keep their ratio and a lit surface keeps its modelling (config_page.cpp, the logo gleam).
    D3DBLEND_DESTCOLOR = 9,
    D3DTSS_COLOROP = 1, D3DTSS_COLORARG1 = 2, D3DTSS_COLORARG2 = 3,
    // ALPHAARG2 is 6. It was 7 here, which is D3DTSS_BUMPENVMAT00 -- so every dSetTSS(..., ALPHAARG2, ...)
    // in this program wrote a bump-mapping matrix cell and left the REAL alpha arg2 at whatever FFXI last set.
    // It went unnoticed for the life of the project because the default is D3DTA_CURRENT (= DIFFUSE at stage 0)
    // and because nothing textured had ever depended on a diffuse alpha below 255: losing the factor is
    // invisible when the factor is 1. The baked corner masks were the first thing to need it -- the tab glass
    // sheen asks for 22% white and got 100% white, a bright quarter-disc in every corner.
    D3DTSS_ALPHAOP = 4, D3DTSS_ALPHAARG1 = 5, D3DTSS_ALPHAARG2 = 6,
    D3DTSS_ADDRESSU = 13, D3DTSS_ADDRESSV = 14, D3DTSS_BORDERCOLOR = 15,
    D3DTSS_MAGFILTER = 16, D3DTSS_MINFILTER = 17, D3DTSS_MIPFILTER = 18, D3DTSS_MIPMAPLODBIAS = 19,
    D3DTEXF_NONE = 0, D3DTEXF_LINEAR = 2,
    D3DTOP_DISABLE = 1, D3DTOP_SELECTARG1 = 2, D3DTOP_SELECTARG2 = 3, D3DTOP_MODULATE = 4, D3DTA_DIFFUSE = 0, D3DTA_TEXTURE = 2,
    // Argument MODIFIER, OR-ed into a COLORARG/ALPHAARG : the stage receives (1 - arg) instead of arg. It is
    // what makes a highlight that cannot clip -- add light proportional to the COMPLEMENT of the texel and a
    // texel already at 255 receives none (config_page.cpp, the logo gleam).
    D3DTA_COMPLEMENT = 0x00000010,
    // ... and the other modifier : the stage receives the argument's ALPHA in all three colour channels.
    // It is how a texture's alpha becomes a MASK for something that is not its own colour.
    D3DTA_ALPHAREPLICATE = 0x00000020,
    D3DTADDRESS_WRAP = 1, D3DTADDRESS_CLAMP = 3, D3DTADDRESS_BORDER = 4,
};

// ---- vertex formats (must match the FVFs above byte-for-byte) ----
struct Vtx  { float x, y, z, rhw; u32 color; float u, v; };  // 28 bytes, FVF 0x144 (liquid)
struct VtxC { float x, y, z, rhw; u32 color; };              // 20 bytes, FVF 0x44  (glass)

// D3DVIEWPORT8 : the active render rectangle (== backbuffer size at HUD-draw time).
struct D3DVIEWPORT8 { u32 X, Y, Width, Height; float MinZ, MaxZ; };

// ---- THE STATE SHADOW (audit P4, 2026-11-24) ---------------------------------------------------------------------
// //aio perf counted 2 900 (town) to 10 500 (config open) SetRenderState + SetTextureStageState calls a frame, ~3 per
// draw : dColorQuadState alone sets 18 states, and it runs before nearly every shape whatever the device already
// holds. Each call is two guarded vtable reads and a driver call. The shadow remembers, per state, the value WE last
// set on this device, and drops a call that would set it again to the same value.
// WHEN IT IS FORGOTTEN (everything marked unknown) : at CreateStateBlock and ApplyStateBlock -- i.e. at the start of
// our drawing, after the game drew with its own state, and once the game's state is restored -- and whenever the
// device address changes. Between the two, nothing but us touches the device (the HUD draws inside one block). An
// entry is only ever written after the call reached the device, so "known" always means "the device holds it".
// dStateFilter(false) turns it off (//aio perf filter off : the in-game A/B, and the way out if a draw ever looks wrong).
struct StateShadow {
    u32 dev; bool off;
    u32 rs[256]; u32 tss[8][32]; u32 tex[8]; u32 fvf;
    unsigned char rsK[256], tssK[8][32], texK[8], fvfK;
};
inline StateShadow g_ss = {};
inline void dStateForget() { const bool off = g_ss.off; memset(&g_ss, 0, sizeof(g_ss)); g_ss.off = off; }
inline void dStateFilter(bool on) { dStateForget(); g_ss.off = !on; }
inline bool dStateFiltering() { return !g_ss.off; }
inline void ss_dev(u32 d) { if (g_ss.dev != d) { dStateForget(); g_ss.dev = d; } }

// ---- IDirect3DDevice8 vtable helpers (call by reversed index, SEH-guarded by vmethod) ----
// The single path every draw and state change takes : //aio perf counts here (gfx/perf.h), and only when it reached
// the device (a null vtable slot is not a call) -- so a call the shadow drops is not counted either.
// ---- DEPTH MODE (2026-11-26, the name plates) : a 2D draw that is HIDDEN BY THE SCENE as the client's name plates are --
// every vertex gets this screen z (0..1, the projected depth of what it labels) and the scene's depth buffer is tested
// (ZENABLE on, LESSEQUAL, no depth write). Off (< 0) : the normal 2D overlay (z 0, no test). dDepthBegin / dDepthEnd only.
// ---- PENDING SHAPES (2026-11-28, perf : 594 rounded rects a frame in Edit Layout, ~2 us of draw call each) : the
// untextured shapes' triangles (TriBatch) are ACCUMULATED and drawn as one list -- flushed before ANY real device change
// (a state the shadow does not already hold, a draw, the depth mode, a viewport, a state block, a screen copy) and at the
// end of the HUD's frame. Order and state are exactly as if each had drawn at once. shape_flush lives in draw.cpp.
inline int g_shPend = 0;
void shape_flush(u32 d);
inline void shape_pre(u32 d) { if (g_shPend) shape_flush(d); }
inline float g_drawZ = -1.0f, g_drawRhw = 1.0f; inline u32 g_drawZMode = 1;   // + the 1/w (a W-buffer compares w) ; the game's ZENABLE (1 z, 2 w)
inline void dSetRS (u32 d, u32 s, u32 v) {
    if (g_drawZ >= 0.0f && s == 7) v = g_drawZMode;   // D3DRS_ZENABLE : the depth mode keeps the scene's test on (its own kind)
    if (s < 256 && !g_ss.off && g_ss.dev == d && g_ss.rsK[s] && g_ss.rs[s] == v) return;   // the device already holds it
    shape_pre(d);
    auto f = vmethod<long(__stdcall*)(u32,u32,u32)>(d,50);
    if (f) { f(d,s,v); perf::on_rs(); if (s < 256) { ss_dev(d); g_ss.rs[s] = v; g_ss.rsK[s] = 1; } }
}
inline void dSetTSS(u32 d, u32 st, u32 ty, u32 v) {
    if (st < 8 && ty < 32 && !g_ss.off && g_ss.dev == d && g_ss.tssK[st][ty] && g_ss.tss[st][ty] == v) return;
    shape_pre(d);
    auto f = vmethod<long(__stdcall*)(u32,u32,u32,u32)>(d,63);
    if (f) { f(d,st,ty,v); perf::on_tss(); if (st < 8 && ty < 32) { ss_dev(d); g_ss.tss[st][ty] = v; g_ss.tssK[st][ty] = 1; } }
}
inline void dSetTex(u32 d, u32 st, u32 tex) {
    if (st < 8 && !g_ss.off && g_ss.dev == d && g_ss.texK[st] && g_ss.tex[st] == tex) return;
    shape_pre(d);
    auto f = vmethod<long(__stdcall*)(u32,u32,u32)>(d,61);
    if (f) { f(d,st,tex); perf::on_tex(); if (st < 8) { ss_dev(d); g_ss.tex[st] = tex; g_ss.texK[st] = 1; } }
}
inline void dSetVS (u32 d, u32 fvf) {
    if (!g_ss.off && g_ss.dev == d && g_ss.fvfK && g_ss.fvf == fvf) return;
    shape_pre(d);
    auto f = vmethod<long(__stdcall*)(u32,u32)>(d,76);
    if (f) { f(d,fvf); perf::on_fvf(); ss_dev(d); g_ss.fvf = fvf; g_ss.fvfK = 1; }
}
inline void dDrawUP(u32 d, u32 pt, u32 n, const void* v, u32 st){
    shape_pre(d);
    auto f = vmethod<long(__stdcall*)(u32,u32,u32,const void*,u32)>(d,72); if (!f) return;
    if (g_drawZ >= 0.0f && v && st >= 16) {   // the depth mode : a copy with z = g_drawZ (XYZRHW : z is the 3rd float)
        const u32 nv = pt == 4 ? n * 3 : pt == 2 ? n * 2 : pt == 3 ? n + 1 : pt == 1 ? n : n + 2;   // list / lines / strip / points / strip, fan
        static unsigned char buf[4096 * 40];
        if (nv * st <= sizeof(buf)) {
            memcpy(buf, v, (size_t)nv * st);
            for (u32 i = 0; i < nv; ++i) { memcpy(buf + (size_t)i * st + 8, &g_drawZ, 4); memcpy(buf + (size_t)i * st + 12, &g_drawRhw, 4); }
            f(d,pt,n,buf,st); perf::on_draw(n); return;
        }
    }
    f(d,pt,n,v,st); perf::on_draw(n); }
inline u32 dGetRS(u32 d, u32 st) { auto f = vmethod<long(__stdcall*)(u32,u32,u32*)>(d,51); u32 v = 0; if (f) f(d,st,&v); return v; }
inline void dDepthBegin(u32 d, float z, float rhw = 1.0f, u32 zmode = 0) {   // zmode 0 : the game's own (read once per call)
    shape_pre(d);                                                        // (the shapes before it : at THEIR depth -- none)
    g_drawZ = z < 0.0f ? 0.0f : (z > 1.0f ? 1.0f : z); g_drawRhw = rhw;
    if (!zmode) { g_drawZ = -1.0f; zmode = dGetRS(d, 7); if (!zmode) zmode = 1; g_drawZ = z < 0.0f ? 0.0f : (z > 1.0f ? 1.0f : z); }
    g_drawZMode = zmode;
    dStateForget();   // the shadow may hold our ZENABLE 0 while the device holds the game's : set them for real
    dSetRS(d, 7, zmode); dSetRS(d, 23, 4); dSetRS(d, 14, 0);   // ZENABLE (the game's kind), ZFUNC LESSEQUAL, ZWRITEENABLE off
}
inline void dDepthEnd(u32 d) { shape_pre(d); g_drawZ = -1.0f; dSetRS(d, 7, 0); }
inline u32  dCreateSB(u32 d, u32 type)            { shape_pre(d); dStateForget(); auto f = vmethod<long(__stdcall*)(u32,u32,u32*)>(d,57); u32 t=0; if(f) f(d,type,&t); return t; }   // forget : the game drew since our last frame
// The display mode, for ONE question we cannot answer by looking: how many bits per channel the game is
// presenting into. A gradient that bands no matter what it is drawn with is banding because the destination
// is 16-bit (R5G6B5 = 32 levels of green, 31 of red and blue), and no amount of care in our vertices changes
// that -- only dithering softens it. Format: 21=A8R8G8B8 22=X8R8G8B8 23=R5G6B5 24=X1R5G5B5 25=A1R5G5B5.
struct D3DDISPLAYMODE8 { u32 w, h, refresh, fmt; };
inline bool dGetDisplayMode(u32 d, D3DDISPLAYMODE8& m) { auto f = vmethod<long(__stdcall*)(u32,void*)>(d,8); return f && f(d,&m) >= 0; }
inline bool dGetViewport(u32 d, D3DVIEWPORT8& vp) { auto f = vmethod<long(__stdcall*)(u32,D3DVIEWPORT8*)>(d,41); return f && f(d,&vp) >= 0; }   // vtbl 41 : GetViewport (current viewport)
inline void dSetViewport(u32 d, const D3DVIEWPORT8& vp) { shape_pre(d); auto f = vmethod<long(__stdcall*)(u32,const D3DVIEWPORT8*)>(d,40); if(f) f(d,&vp); }   // vtbl 40 : SetViewport -- a sub-rect acts as a hard scissor even for XYZRHW quads

// TRUE backbuffer size (follows a windowed resize / device reset, unlike GetViewport which can
// be a temporary sub-viewport). GetBackBuffer (dev vtbl 16) -> Surface8::GetDesc (surf vtbl 8 ;
// Width@+0x18, Height@+0x1C) -> Release (surf vtbl 2).
// the device's focus window (HWND) via GetCreationParameters (dev vtbl 9 ; hFocusWindow @+0x08).
inline u32 dFocusWindow(u32 d) {
    u32 cp[4] = {0};
    auto f = vmethod<long(__stdcall*)(u32, void*)>(d, 9);
    return (f && f(d, cp) >= 0) ? cp[2] : 0;
}

inline bool dGetBackBufferSize(u32 d, u32& w, u32& h) {
    auto getbb = vmethod<long(__stdcall*)(u32,u32,u32,u32*)>(d, 16);
    if (!getbb) return false;
    u32 surf = 0;
    if (getbb(d, 0, 0, &surf) < 0 || !valid_ptr(surf)) return false;   // backbuffer 0, type MONO(0)
    u32 desc[8] = {0};
    auto getdesc = vmethod<long(__stdcall*)(u32, void*)>(surf, 8);
    bool ok = false;
    if (getdesc && getdesc(surf, desc) >= 0) { w = desc[6]; h = desc[7]; ok = (w >= 320 && h >= 240); }
    auto rel = vmethod<u32(__stdcall*)(u32)>(surf, 2);
    if (rel) rel(surf);
    return ok;
}
inline void dApplySB(u32 d, u32 tok)              { shape_pre(d); auto f = vmethod<long(__stdcall*)(u32,u32)>(d,54);          if(f) f(d,tok); dStateForget(); }   // forget : the game's state is back
inline void dDelSB  (u32 d, u32 tok)              { auto f = vmethod<long(__stdcall*)(u32,u32)>(d,56);          if(f) f(d,tok); }

// The 2D untextured colour-quad pipeline (XYZRHW diffuse, no Z/cull/light/fog/spec, standard alpha blend,
// stage-0 = pick DIFFUSE, stage-1 off, no bound texture). Was copy-pasted verbatim in every widget's
// color_state/setup_color_state -> ONE source of truth (edit the 2D state once, here). Callers that draw
// TEXTURED quads override FVF + stage-0 TSS afterwards (they only share the render-state block).
inline void dColorQuadState(u32 d) {
    dSetVS(d, FVF_XYZRHW_DIFFUSE);
    dSetRS(d, D3DRS_ZENABLE, 0); dSetRS(d, D3DRS_CULLMODE, D3DCULL_NONE); dSetRS(d, D3DRS_LIGHTING, 0);
    dSetRS(d, D3DRS_ALPHATESTENABLE, 0); dSetRS(d, D3DRS_FOGENABLE, 0); dSetRS(d, D3DRS_SPECULARENABLE, 0);
    dSetRS(d, D3DRS_ALPHABLENDENABLE, 1); dSetRS(d, D3DRS_SRCBLEND, D3DBLEND_SRCALPHA); dSetRS(d, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    dSetRS(d, D3DRS_BLENDOP, D3DBLENDOP_ADD); dSetTex(d, 0, 0);
    dSetRS(d, D3DRS_SHADEMODE, D3DSHADE_GOURAUD); dSetRS(d, D3DRS_DITHERENABLE, 1);   // see the note on both, above
    dSetTSS(d, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1); dSetTSS(d, 0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    dSetTSS(d, 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1); dSetTSS(d, 0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    dSetTSS(d, 1, D3DTSS_COLOROP, D3DTOP_DISABLE); dSetTSS(d, 1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
}

// Standard TEXTURED-quad state (MODULATE tex*diffuse, straight alpha, LINEAR min/mag), binding `tex` on
// stage 0 -- the block every sprite draw shares (icons / cursors / buffs / map). This existed as FOUR copies
// (party / target / player / minimap) that had already DIVERGED, so both axes they disagreed on are
// parameters here rather than a choice imposed on everyone :
//   mipLinear : LINEAR mips keep minified icons and the map crisp (minimap) ; NONE keeps a pixel-exact atlas
//               sharp (target / player / the buff atlas).
//   border    : BORDER addressing instead of CLAMP -- the minimap's round disc samples OUTSIDE [0,1] when
//               zoomed out near a map edge, where CLAMP would smear the edge texels into a stretch artefact.
// Addressing is always set explicitly : leaving it unset let WRAP leak in from the box chrome and fringe the
// atlas cells whose UVs touch 0 or 1.
inline void dTexQuadState(u32 d, u32 tex, bool mipLinear = false, bool border = false) {
    dSetVS(d, FVF_XYZRHW_DIFFUSE_TEX1);
    dSetRS(d, D3DRS_SHADEMODE, D3DSHADE_GOURAUD); dSetRS(d, D3DRS_DITHERENABLE, 1);   // textured quads carry vertex gradients too (the masthead gleam)
    dSetRS(d, D3DRS_ALPHABLENDENABLE, 1);
    dSetRS(d, D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    dSetRS(d, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    dSetTex(d, 0, tex);
    dSetTSS(d, 0, D3DTSS_COLOROP, D3DTOP_MODULATE); dSetTSS(d, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE); dSetTSS(d, 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    dSetTSS(d, 0, D3DTSS_ALPHAOP, D3DTOP_MODULATE); dSetTSS(d, 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE); dSetTSS(d, 0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    dSetTSS(d, 0, D3DTSS_MINFILTER, D3DTEXF_LINEAR); dSetTSS(d, 0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
    dSetTSS(d, 0, D3DTSS_MIPFILTER, mipLinear ? D3DTEXF_LINEAR : D3DTEXF_NONE);
    // ...and the LOD bias with it, for exactly the reason the addressing note above gives. Font::begin sets
    // MIPMAPLODBIAS to +0.20 (it wants its glyph atlas very slightly softened) and nothing ever set it back --
    // it is a texture-stage state, so it persisted for the rest of the frame. Every mipLinear consumer that drew
    // AFTER any text -- the minimap, the party job/buff icons -- was then sampled at LOD+0.2, blending in the
    // half-resolution mip at 1:1. Two widgets calling this same function rendered differently depending on the
    // draw order. Setting it here makes the block self-contained, like MIPFILTER and the addressing.
    { union { float f; u32 u; } lod; lod.f = 0.0f; dSetTSS(d, 0, D3DTSS_MIPMAPLODBIAS, lod.u); }
    const u32 addr = border ? D3DTADDRESS_BORDER : D3DTADDRESS_CLAMP;
    dSetTSS(d, 0, D3DTSS_ADDRESSU, addr); dSetTSS(d, 0, D3DTSS_ADDRESSV, addr);
}

} // namespace aio
