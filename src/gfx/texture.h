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

// procedural NEBULA background (domain-warped fbm clouds in a cosmic palette on dark space, opaque).

// MATERIAL textures for procedural box themes : grayscale luminance patterns, tinted per hue by the
// caller (MODULATE). wood grain / soft frost clouds / velvet nap / brushed steel.
u32 make_wood(u32 dev, int W, int H);
u32 make_frost(u32 dev, int W, int H);
u32 make_velvet(u32 dev, int W, int H);
u32 make_metal(u32 dev, int W, int H);

// soft capsule-shaped radial glow (for the outer halo). Shape in RGB.
u32 make_glow(u32 dev);

// small bubble sprite (faint disc + bright rim). Shape in RGB.
u32 make_bubble(u32 dev);

// load a raw BGRA blob (W*H*4, straight alpha) from disk into a texture.
u32 load_raw_texture(u32 dev, const char* path, int W, int H);
u32 load_raw_texture_mip(u32 dev, const char* path, int W, int H);   // + mip chain (crisp when minified)

// load a 32-bpp BMP (the FFXI gearicon format : V4 header, ARGB masks, bottom-up) into a mipped texture.
// W/H come from the header ; returns 0 on any IO/parse failure (missing icon -> no icon drawn).
u32 load_bmp_texture(u32 dev, const char* path);

// Gear-icon ROM fallback : decode item `id`'s 32x32 icon straight from the game's ROM DAT files (a port of
// EquipViewer's icon_extractor) INTO MEMORY -- `out_px` takes 32*32 ARGB DWORDs, top-down, ready for
// make_texture_argb_mip. Lets ANY item resolve even when it isn't in the bundled assets/gearicons/ seed
// (only 1323 of ~23500 items are seeded, so this is the NORMAL path, not an edge case).
// Returns false only when the icon is genuinely unreachable : no game install, or an id outside every DAT range.
// It deliberately does NOT touch the disk -- writing the cache used to be able to fail (read-only plugin folder)
// and take a perfectly good decode down with it, which is what showed raw item IDs on locked-down installs.
// //aio geartrace : where a decode actually stopped. The old single "ROM decode failed" conflated no-registry,
// no-DAT and no-write-permission -- three different bugs with three different fixes.
enum GearStep { GS_OK = 0, GS_NO_RANGE, GS_NO_ROMDIR, GS_NO_DAT, GS_BAD_READ };
struct GearInfo {
    int         step;     // GearStep
    const char* dat;      // "118/109" (0 if unresolved)
    const char* romdir;   // "<ffxi>\ROM" (0 if unresolved)
    const char* regkey;   // which PlayOnline* key answered (0 if none)
    long        index;    // record index inside the DAT
    int         err;      // errno from the failing fopen (0 if none)
};
bool decode_gear_icon_from_rom(unsigned id, u32* out_px, GearInfo* info = 0);

// Best-effort cache of a decoded icon as the same V4 BMP load_bmp_texture reads. Atomic (temp + rename) and
// fully checked, so a failed/partial write can never leave a corrupt BMP behind. Returns false if it couldn't
// be written -- callers should IGNORE that : the icon is already in hand, only the next-session shortcut is lost.
bool write_gear_icon_bmp(const char* out_bmp_path, const u32* px, int* out_err = 0);

// which registry key resolved the FFXI install, and to what (0 = none found). //aio geartrace reporting.
const char* ffxi_rom_dir_probe(const char** out_regkey);

// create an A8R8G8B8 texture from an in-memory ARGB buffer (W*H DWORDs, row-major).

// same, but with a full MIP CHAIN (box-filtered) -> clean minification (crisp scaled text).
u32 make_texture_argb_mip(u32 dev, int W, int H, const u32* pixels);

// party marker icons (procedural pixel-art, 32x32 A8R8G8B8 + mips, straight alpha):
// gold crown = party leader, gold star = alliance leader, green coin = quartermaster.
u32 make_dot(u32 dev);   // solid white AA disc (tinted per role for the leader/QM bullets)

// release a texture (IUnknown::Release, vtbl[2]); safe on 0.
void release_texture(u32 tex);

// keep a MANAGED texture resident (IDirect3DResource8::PreLoad, vtbl[9]); the game
// can evict our textures from VRAM under load, making them sample wrong.
void preload_texture(u32 tex);

} // namespace aio
