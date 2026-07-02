// texture.h -- procedural + file-backed D3D textures (A8R8G8B8, MANAGED pool).
//
// All return a raw IDirect3DTexture8* (as u32), or 0 on failure. They are tied to
// the device they were created on -- drop them when the device is recreated (see
// the zoning/device-loss handling in the HUD).
//
// IMPORTANT (zoning): a texture's ALPHA channel mis-samples as ~255 while a zone
// loads, so glow/bubble sprites store their SHAPE in the RGB (greyscale), alpha
// opaque, and are drawn with ALPHAOP=SELECTARG1/DIFFUSE. See docs/reference/d3d8-rendering.md.
#pragma once
#include "d3d.h"

namespace aio {

// per-resource liquid fill texture. variant: 0 = HP, 1 = MP (arcane swirl), 2 = TP.
u32 make_liquid_texture(u32 dev, int variant);

// soft capsule-shaped radial glow (for the outer halo). Shape in RGB.
u32 make_glow(u32 dev);

// small bubble sprite (faint disc + bright rim). Shape in RGB.
u32 make_bubble(u32 dev);

// load a raw BGRA blob (W*H*4, straight alpha) from disk into a texture.
u32 load_raw_texture(u32 dev, const char* path, int W, int H);

// create an A8R8G8B8 texture from an in-memory ARGB buffer (W*H DWORDs, row-major).
u32 make_texture_argb(u32 dev, int W, int H, const u32* pixels);

// same, but with a full MIP CHAIN (box-filtered) -> clean minification (crisp scaled text).
u32 make_texture_argb_mip(u32 dev, int W, int H, const u32* pixels);

// party marker icons (procedural pixel-art, 32x32 A8R8G8B8 + mips, straight alpha):
// gold crown = party leader, gold star = alliance leader, green coin = quartermaster.
u32 make_dot(u32 dev);   // solid white AA disc (tinted per role for the leader/QM bullets)
u32 make_icon_party_lead(u32 dev);
u32 make_icon_alliance_lead(u32 dev);
u32 make_icon_qm(u32 dev);

// release a texture (IUnknown::Release, vtbl[2]); safe on 0.
void release_texture(u32 tex);

// keep a MANAGED texture resident (IDirect3DResource8::PreLoad, vtbl[9]); the game
// can evict our textures from VRAM under load, making them sample wrong.
void preload_texture(u32 tex);

} // namespace aio
