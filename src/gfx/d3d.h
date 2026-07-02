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

namespace aio {

using windower::u32;
using windower::vmethod;
using windower::valid_ptr;

// ---- D3D8 constants (the subset we use) ----
enum {
    D3DPT_TRIANGLESTRIP = 5, D3DPT_TRIANGLEFAN = 6,
    FVF_XYZRHW_DIFFUSE      = 0x44,    // VtxC : pos + diffuse        (glass / gradients)
    FVF_XYZRHW_DIFFUSE_TEX1 = 0x144,   // Vtx  : pos + diffuse + uv    (liquid / textures)
    D3DSBT_ALL = 1, D3DFMT_A8R8G8B8 = 21, D3DPOOL_MANAGED = 1,
    D3DRS_ZENABLE = 7, D3DRS_SRCBLEND = 19, D3DRS_DESTBLEND = 20, D3DRS_CULLMODE = 22,
    D3DRS_ALPHABLENDENABLE = 27, D3DRS_LIGHTING = 137,
    D3DRS_ALPHATESTENABLE = 15, D3DRS_FOGENABLE = 28, D3DRS_SPECULARENABLE = 29,
    D3DRS_COLORVERTEX = 141, D3DRS_COLORWRITEENABLE = 168, D3DRS_TEXTUREFACTOR = 60,
    D3DRS_WRAP0 = 128, D3DRS_BLENDOP = 171, D3DBLENDOP_ADD = 1,
    D3DTSS_TEXCOORDINDEX = 11, D3DTSS_TEXTURETRANSFORMFLAGS = 24, D3DTTFF_DISABLE = 0,
    D3DCULL_NONE = 1, D3DBLEND_ZERO = 1, D3DBLEND_ONE = 2, D3DBLEND_SRCALPHA = 5, D3DBLEND_INVSRCALPHA = 6,
    D3DTSS_COLOROP = 1, D3DTSS_COLORARG1 = 2, D3DTSS_COLORARG2 = 3,
    D3DTSS_ALPHAOP = 4, D3DTSS_ALPHAARG1 = 5, D3DTSS_ALPHAARG2 = 7,
    D3DTSS_ADDRESSU = 13, D3DTSS_ADDRESSV = 14,
    D3DTSS_MAGFILTER = 16, D3DTSS_MINFILTER = 17, D3DTSS_MIPFILTER = 18, D3DTSS_MIPMAPLODBIAS = 19,
    D3DTEXF_NONE = 0, D3DTEXF_LINEAR = 2,
    D3DTOP_DISABLE = 1, D3DTOP_SELECTARG1 = 2, D3DTOP_SELECTARG2 = 3, D3DTOP_MODULATE = 4, D3DTA_DIFFUSE = 0, D3DTA_TEXTURE = 2,
    D3DTADDRESS_WRAP = 1, D3DTADDRESS_CLAMP = 3,
};

// ---- vertex formats (must match the FVFs above byte-for-byte) ----
struct Vtx  { float x, y, z, rhw; u32 color; float u, v; };  // 28 bytes, FVF 0x144 (liquid)
struct VtxC { float x, y, z, rhw; u32 color; };              // 20 bytes, FVF 0x44  (glass)

// D3DVIEWPORT8 : the active render rectangle (== backbuffer size at HUD-draw time).
struct D3DVIEWPORT8 { u32 X, Y, Width, Height; float MinZ, MaxZ; };

// ---- IDirect3DDevice8 vtable helpers (call by reversed index, SEH-guarded by vmethod) ----
inline void dSetRS (u32 d, u32 s, u32 v)          { auto f = vmethod<long(__stdcall*)(u32,u32,u32)>(d,50);     if(f) f(d,s,v); }
inline void dSetTSS(u32 d, u32 st, u32 ty, u32 v) { auto f = vmethod<long(__stdcall*)(u32,u32,u32,u32)>(d,63);  if(f) f(d,st,ty,v); }
inline void dSetTex(u32 d, u32 st, u32 tex)       { auto f = vmethod<long(__stdcall*)(u32,u32,u32)>(d,61);      if(f) f(d,st,tex); }
inline void dSetVS (u32 d, u32 fvf)               { auto f = vmethod<long(__stdcall*)(u32,u32)>(d,76);          if(f) f(d,fvf); }
inline void dDrawUP(u32 d, u32 pt, u32 n, const void* v, u32 st){ auto f = vmethod<long(__stdcall*)(u32,u32,u32,const void*,u32)>(d,72); if(f) f(d,pt,n,v,st); }
inline u32  dCreateSB(u32 d, u32 type)            { auto f = vmethod<long(__stdcall*)(u32,u32,u32*)>(d,57); u32 t=0; if(f) f(d,type,&t); return t; }
inline bool dGetViewport(u32 d, D3DVIEWPORT8& vp) { auto f = vmethod<long(__stdcall*)(u32,D3DVIEWPORT8*)>(d,41); return f && f(d,&vp) >= 0; }   // vtbl 41 : GetViewport (current viewport)

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
inline void dApplySB(u32 d, u32 tok)              { auto f = vmethod<long(__stdcall*)(u32,u32)>(d,54);          if(f) f(d,tok); }
inline void dDelSB  (u32 d, u32 tok)              { auto f = vmethod<long(__stdcall*)(u32,u32)>(d,56);          if(f) f(d,tok); }

} // namespace aio
