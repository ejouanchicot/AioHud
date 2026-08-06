// window.h -- FFXI native window skin (9-slice), composed from the game's menu textures.
//
// The game builds EVERY window from 4 DXT1 textures per theme : a tiled background, a
// horizontal edge band (top/bottom), a vertical edge band (left/right) and a corner sheet
// (its 4 quadrants = the 4 corners). They are grayscale and TINTED by the chosen window
// colour. We convert them to straight-alpha BGRA .raw (scripts/gen_window_skin.sh) and
// re-compose them here as a 9-slice so our HUD boxes match the game's window chrome.
#pragma once
#include "gfx/d3d.h"   // u32

namespace aio {

struct WindowSkin {
    u32 corner = 0, hframe = 0, vframe = 0, bg = 0;   // the 4 skin textures (BGRA)
    u32 borderColor = 0xFFFFFFFF;                     // per-theme border colour, derived from the bg at load
    int failN = 0;                                    // consecutive failed load() attempts -> lets self_check tell
                                                      // "this theme's files are missing" from "this theme has no files"
    bool ready() const { return corner && hframe && vframe && bg; }
    bool failed() const { return failN > 0; }         // a TEXTURE theme that will not load (NOT the same as a procedural one)
    bool load(u32 dev, const char* themeName);   // assets/window/<themeName>/{corner,hframe,vframe,bg}.raw
    void on_device_lost();              // forget handles (zoning) -> reload on next load()
    void dispose();                     // release textures (unload)
};

// the theme list : the texture themes (folder names under assets/window) THEN the procedural colour
// themes. window_theme_is_proc() tells them apart ; //aio menu N selects across the whole list.
int         window_theme_count();
const char* window_theme_name(int idx);      // 0-based ; null if out of range
bool        window_theme_is_proc(int idx);   // true = a procedural colour theme (drawn, no texture to load)
int         window_tex_theme_count();        // number of FFXI texture themes (family 0)

// Box themes are organised as FAMILIES (styles) x a hue palette. Family 0 = "FFXI" (game textures,
// its variant = texture theme index) ; families 1.. are procedural (variant = hue index).
int         box_family_count();
const char* box_family_name(int idx);        // e.g. "FFXI", "Modern", "Medieval", ...
int         box_hue_count();
u32         box_hue_color(int idx);          // the swatch colour for hue `idx`
u32         box_theme_border_color(int themeIdx, u32 hueOverride = 0);   // a procedural theme's real FRAME colour (Royal gold, ...) ; 0 for FFXI themes ; hueOverride (alpha set) replaces the preset hue
// draw a procedural theme's real BORDER as a circular ring in [rInner, rOuter] (round minimap) -- the exact
// family frame (gold plate/filet, iron/steel bevel, neon tube, frost rime), not a flat-colour approximation.
void        draw_box_border_ring(u32 dev, int themeIdx, float cx, float cy, float rOuter, float rInner, float lum, u32 hueOverride = 0);
// draw a procedural theme's real BORDER on a rect (no interior bg) -- the exact family frame per small cell
// (equip-viewer cases), so their separators match the current theme's frame, not a flat colour.
void        draw_box_border_rect(u32 dev, int themeIdx, float x, float y, float w, float h, float lum, u32 hueOverride = 0);
// draw one NEON-tube separator line (colour/white/colour across the short axis) filling [x,y,w,h] ; no-op unless
// the theme is the Neon family. For the equip-grid neon lattice so it matches the round-minimap neon ring.
void        draw_neon_line(u32 dev, int themeIdx, float x, float y, float w, float h, u32 hueOverride = 0);
// draw a SEAMLESS neon-tube rectangle frame (concentric colour/white/colour outlines) in the band of width `tw`
// around [x,y,w,h] ; no-op unless Neon. For the equip-grid neon OUTER frame -- drawn last, over the lattice.
void        draw_neon_frame(u32 dev, int themeIdx, float x, float y, float w, float h, float tw, u32 hueOverride = 0);
int         window_theme_family(int themeIdx);   // 0 = FFXI, else 1.. procedural family
int         window_theme_variant(int themeIdx);  // FFXI: texture index ; procedural: hue index
int         window_theme_index(int family, int variant);   // (family, variant) -> flat theme index

// draw a PROCEDURAL colour-theme box background (dark base + styled decoration + coloured border) for
// the given GLOBAL theme index. No skin textures needed. Kept dark so party text stays readable.
// hueOverride : if its alpha byte is set, it REPLACES the theme's preset hue (a free custom colour) while
// keeping the family's style ; 0 = use the theme index's preset hue (BOX_HUES[variant]).
void draw_proc_window(u32 dev, int themeIdx, float x, float y, float w, float h, u32 tint,
                      bool openBottom = false, bool drawBorder = true, float lum = 0.0f, u32 hueOverride = 0);   // lum : -1 darker .. +1 lighter

// forget the cached procedural MATERIAL textures (wood/frost/velvet/metal) on device loss ; they are
// regenerated lazily on the next draw. Call from the HUD's device-reset handler.
void window_materials_reset();     // forget handles (device loss) -- do NOT Release
void window_materials_dispose();   // Release the material textures (device alive : //unload / shutdown)

// 9-slice the skin into [x,y,w,h] : tiled native bg + 4 edges + 4 corners, MODULATEd by `tint`
// (ARGB ; 0xFFFFFFFF = texture as-is). `scale` is ignored (textures drawn 1:1, tiled). The caller
// should pixel-snap x/y/w/h. `openBottom` : omit the bottom edge + bottom corners (the side edges
// run to the bottom) -> the box reads as joined to whatever sits flush below it.
// `drawBorder` false : paint ONLY the tiled background (skip the frame edges + corners) -> a borderless
// box that keeps its skin background (the per-box "Borders off" toggle).
void draw_window(u32 dev, const WindowSkin& s, float x, float y, float w, float h, u32 tint, float scale, bool openBottom = false, bool drawBorder = true);

} // namespace aio
