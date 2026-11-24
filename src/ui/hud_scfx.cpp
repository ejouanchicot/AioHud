// hud_scfx.cpp -- the skillchain "resonance circle" redesign, as a STANDALONE animated demo (//aio scfx).
//
// A test bench for the new Skillchains look, apart from the real box (hud_skillchains.cpp) : it plays a scripted
// scene on a loop so each skillchain's animation can be judged in game without fighting. Layout from the HTML
// prototype (one prototype px = one px of a 2560x1349 screen, scaled by k) ; the EFFECTS are Direct3D 8 :
//
//   * two procedural textures, generated once : a NOISE of ridged fbm veins, and a SOFT round sprite ;
//   * ring MESHES around the circle carrying the noise on TWO texture stages that scroll against each other
//     (MODULATE2X) -> a turbulence that never repeats (lava, water caustics, frost, plasma, wind, void) ; the
//     outer edge displaced every frame, the vertex colours running hot -> cold -> transparent ;
//   * RIBBONS : soft triangle strips along any polyline (lightning bolts, wind gusts, spiral arms, rays, tendrils) ;
//   * sprites whose colour follows the property's ramp over their life, stretched along their velocity ;
//   * an explosive START on every chain : a bloom flash, a noise-torn shockwave, a burst of particles.
//
//   each scene : a weapon skill OPENS (no skillchain yet : neutral grey ring, WAIT -> GO, what closes it listed
//   below) -> a second weapon skill closes the chain (the blast, then the property's own animation, its burst
//   elements around the ring, WAIT -> GO to extend it, everything that continues it below) -> a rest -> loop.
//   The WS pairs and lists are REAL : Windower res/weapon_skills.lua x AioHUD's sc_info combos (scratchpad
//   gen_scenes.py).
//
// No per-frame heap : fixed particle pool and vertex arrays. Every texture is owned here : scfx_forget() on device
// loss (handles set to 0, rule 4), scfx_dispose() Releases. Every pass restores the blend and unbinds stage 1 before
// returning (rules 3 and 8).
#include "ui/hud.h"
#include "ui/hud_internal.h"
#include "ui/config_controls.h"   // tr()
#include "ui/tex_retry.h"
#include "model/skillchain.h"
#include "model/paths.h"
#include "gfx/draw.h"
#include "gfx/d3d.h"
#include "gfx/font.h"
#include "gfx/texture.h"
#include "gfx/noise.h"
#include "windower_debug.h"
#include "model/ui_config.h"
#include "ui/text_style.h"
#include "ui/box_style.h"
#include "ui/sc_circle.h"
#include "ui/box_grow.h"
#include "ui/tune.h"          // TUNE("name", default) : data\tune.txt in a dev build, the default in a release
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

namespace aio {

namespace {

const float PI = 3.14159265f, TAU = 6.2831853f;
const u32 FVF_TEX2 = 0x244;                 // XYZRHW | DIFFUSE | TEX2
const u32 TOP_MODULATE2X = 5, TA_CURRENT = 1;

// ---------------------------------------------------------------- the scenes (generated, real) ----
struct Row   { int prop, lvl; const char* ws[7]; int n; };
struct Scene { int prop; const char* opener; const char* closer; int npre; Row pre[4]; int nnext; Row next[4]; };
const Scene SCENES[] = {
    { SCP_Liquefaction, "Flat Blade", "Burning Blade", 2, { { SCP_Detonation, 1, { "Imperator" }, 1 }, { SCP_Liquefaction, 1, { "Burning Blade", "Red Lotus Blade" }, 2 } }, 2, { { SCP_Fusion, 2, { "Flat Blade", "Circle Blade" }, 2 }, { SCP_Scission, 1, { "Fast Blade", "Shining Blade", "Seraph Blade", "Vorpal Blade", "Savage Blade", "Expiacion", "Requiescat" }, 7 } } },   // skill 3 : Flat Blade ['Impaction'] -> Burning Blade ['Liquefaction']
    { SCP_Impaction, "Circle Blade", "Flat Blade", 3, { { SCP_Detonation, 1, { "Imperator" }, 1 }, { SCP_Impaction, 1, { "Flat Blade", "Circle Blade", "Vorpal Blade" }, 3 }, { SCP_Liquefaction, 1, { "Burning Blade", "Red Lotus Blade" }, 2 } }, 2, { { SCP_Detonation, 1, { "Imperator" }, 1 }, { SCP_Liquefaction, 1, { "Burning Blade", "Red Lotus Blade" }, 2 } } },   // skill 3 : Circle Blade ['Reverberation', 'Impaction'] -> Flat Blade ['Impaction']
    { SCP_Detonation, "Fast Blade", "Imperator", 3, { { SCP_Detonation, 1, { "Imperator" }, 1 }, { SCP_Liquefaction, 1, { "Burning Blade", "Red Lotus Blade" }, 2 }, { SCP_Reverberation, 1, { "Circle Blade", "Atonement" }, 2 } }, 2, { { SCP_Gravitation, 2, { "Imperator" }, 1 }, { SCP_Scission, 1, { "Fast Blade", "Shining Blade", "Seraph Blade", "Vorpal Blade", "Savage Blade", "Expiacion", "Requiescat" }, 7 } } },   // skill 3 : Fast Blade ['Scission'] -> Imperator ['Detonation', 'Compression', 'Distortion']
    { SCP_Scission, "Burning Blade", "Fast Blade", 2, { { SCP_Fusion, 2, { "Flat Blade", "Circle Blade" }, 2 }, { SCP_Scission, 1, { "Fast Blade", "Shining Blade", "Seraph Blade", "Vorpal Blade", "Savage Blade", "Expiacion", "Requiescat" }, 7 } }, 3, { { SCP_Detonation, 1, { "Imperator" }, 1 }, { SCP_Liquefaction, 1, { "Burning Blade", "Red Lotus Blade" }, 2 }, { SCP_Reverberation, 1, { "Circle Blade", "Atonement" }, 2 } } },   // skill 3 : Burning Blade ['Liquefaction'] -> Fast Blade ['Scission']
    { SCP_Reverberation, "Fast Blade", "Circle Blade", 3, { { SCP_Detonation, 1, { "Imperator" }, 1 }, { SCP_Liquefaction, 1, { "Burning Blade", "Red Lotus Blade" }, 2 }, { SCP_Reverberation, 1, { "Circle Blade", "Atonement" }, 2 } }, 1, { { SCP_Impaction, 1, { "Flat Blade", "Circle Blade", "Vorpal Blade" }, 3 } } },   // skill 3 : Fast Blade ['Scission'] -> Circle Blade ['Reverberation', 'Impaction']
    { SCP_Induration, "Shockwave", "Frostbite", 2, { { SCP_Impaction, 1, { "Sickle Moon" }, 1 }, { SCP_Induration, 1, { "Frostbite", "Freezebite", "Herculean Slash" }, 3 } }, 3, { { SCP_Fragmentation, 2, { "Shockwave" }, 1 }, { SCP_Compression, 1, { "Fimbulvetr" }, 1 }, { SCP_Impaction, 1, { "Sickle Moon", "Herculean Slash" }, 2 } } },   // skill 4 : Shockwave ['Reverberation'] -> Frostbite ['Induration']
    { SCP_Compression, "Power Slash", "Fimbulvetr", 3, { { SCP_Distortion, 2, { "Hard Slash", "Crescent Moon", "Sickle Moon", "Resolution" }, 4 }, { SCP_Compression, 1, { "Fimbulvetr" }, 1 }, { SCP_Reverberation, 1, { "Shockwave" }, 1 } }, 2, { { SCP_Detonation, 1, { "Freezebite", "Herculean Slash", "Fimbulvetr" }, 3 }, { SCP_Transfixion, 1, { "Power Slash" }, 1 } } },   // skill 4 : Power Slash ['Transfixion'] -> Fimbulvetr ['Detonation', 'Compression', 'Distortion']
    { SCP_Transfixion, "Fimbulvetr", "Power Slash", 4, { { SCP_Fusion, 2, { "Scourge" }, 1 }, { SCP_Detonation, 1, { "Freezebite", "Herculean Slash", "Fimbulvetr" }, 3 }, { SCP_Scission, 1, { "Hard Slash", "Crescent Moon", "Sickle Moon", "Resolution" }, 4 }, { SCP_Transfixion, 1, { "Power Slash" }, 1 } }, 3, { { SCP_Distortion, 2, { "Hard Slash", "Crescent Moon", "Sickle Moon", "Resolution" }, 4 }, { SCP_Compression, 1, { "Fimbulvetr" }, 1 }, { SCP_Reverberation, 1, { "Shockwave" }, 1 } } },   // skill 4 : Fimbulvetr ['Detonation', 'Compression', 'Distortion'] -> Power Slash ['Transfixion']
    { SCP_Fusion, "Burning Blade", "Flat Blade", 2, { { SCP_Fusion, 2, { "Flat Blade", "Circle Blade" }, 2 }, { SCP_Scission, 1, { "Fast Blade", "Shining Blade", "Seraph Blade", "Vorpal Blade", "Savage Blade", "Expiacion", "Requiescat" }, 7 } }, 2, { { SCP_Light, 3, { "Savage Blade", "Death Blossom", "Uriel Blade" }, 3 }, { SCP_Gravitation, 2, { "Swift Blade", "Requiescat" }, 2 } } },   // skill 3 : Burning Blade ['Liquefaction'] -> Flat Blade ['Impaction']
    { SCP_Fragmentation, "Swift Blade", "Savage Blade", 2, { { SCP_Darkness, 3, { "Expiacion", "Chant du Cygne", "Imperator" }, 3 }, { SCP_Fragmentation, 2, { "Savage Blade", "Death Blossom", "Uriel Blade" }, 3 } }, 2, { { SCP_Light, 3, { "Knights of Round", "Atonement", "Fast Blade II", "Glory Slash" }, 4 }, { SCP_Distortion, 2, { "Death Blossom", "Expiacion", "Chant du Cygne", "Imperator" }, 4 } } },   // skill 3 : Swift Blade ['Gravitation'] -> Savage Blade ['Fragmentation', 'Scission']
    { SCP_Distortion, "Savage Blade", "Death Blossom", 4, { { SCP_Light, 3, { "Knights of Round", "Atonement", "Fast Blade II", "Glory Slash" }, 4 }, { SCP_Distortion, 2, { "Death Blossom", "Expiacion", "Chant du Cygne" }, 3 }, { SCP_Detonation, 1, { "Imperator" }, 1 }, { SCP_Liquefaction, 1, { "Burning Blade", "Red Lotus Blade" }, 2 } }, 2, { { SCP_Darkness, 3, { "Swift Blade", "Requiescat" }, 2 }, { SCP_Fusion, 2, { "Knights of Round", "Atonement", "Fast Blade II", "Glory Slash" }, 4 } } },   // skill 3 : Savage Blade ['Fragmentation', 'Scission'] -> Death Blossom ['Fragmentation', 'Distortion']
    { SCP_Gravitation, "Knights of Round", "Swift Blade", 3, { { SCP_Light, 4, { "Knights of Round", "Chant du Cygne", "Knights of Rotund", "Uriel Blade", "Glory Slash" }, 5 }, { SCP_Light, 3, { "Savage Blade", "Death Blossom" }, 2 }, { SCP_Gravitation, 2, { "Swift Blade", "Requiescat" }, 2 } }, 2, { { SCP_Darkness, 3, { "Expiacion", "Chant du Cygne", "Imperator" }, 3 }, { SCP_Fragmentation, 2, { "Savage Blade", "Death Blossom", "Uriel Blade" }, 3 } } },   // skill 3 : Knights of Round ['Light', 'Fusion'] -> Swift Blade ['Gravitation']
    { SCP_Light, "Savage Blade", "Knights of Round", 4, { { SCP_Light, 3, { "Knights of Round", "Atonement", "Fast Blade II", "Glory Slash" }, 4 }, { SCP_Distortion, 2, { "Death Blossom", "Expiacion", "Chant du Cygne" }, 3 }, { SCP_Detonation, 1, { "Imperator" }, 1 }, { SCP_Liquefaction, 1, { "Burning Blade", "Red Lotus Blade" }, 2 } }, 1, { { SCP_Light, 4, { "Knights of Round", "Chant du Cygne", "Knights of Rotund", "Uriel Blade", "Glory Slash" }, 5 } } },   // skill 3 : Savage Blade ['Fragmentation', 'Scission'] -> Knights of Round ['Light', 'Fusion']
    { SCP_Darkness, "Swift Blade", "Expiacion", 2, { { SCP_Darkness, 3, { "Expiacion", "Chant du Cygne", "Imperator" }, 3 }, { SCP_Fragmentation, 2, { "Savage Blade", "Death Blossom", "Uriel Blade" }, 3 } }, 0, {} },   // skill 3 : Swift Blade ['Gravitation'] -> Expiacion ['Distortion', 'Scission']
    { SCP_Radiance, "Torcleaver", "Resolution", 1, { { SCP_Radiance, 4, { "Resolution (Aeonic)" }, 1 } }, 0, {} },   // skill 4 : Torcleaver [Light] -> the Aeonic Resolution (its Light), Aftermath
    { SCP_Umbra, "Catastrophe", "Entropy", 1, { { SCP_Umbra, 4, { "Entropy (Aeonic)" }, 1 } }, 0, {} },   // skill 7 : Catastrophe [Darkness] -> the Aeonic Entropy (its Darkness), Aftermath
};
const int NSCENES = (int)(sizeof(SCENES) / sizeof(SCENES[0]));

// ---------------------------------------------------------------- state ----
bool     sx_on = false, sx_all = false, sx_loop = false, sx_iso = false;
enum { REV_WATER = 3, REV_BOUNCE = 4, REV_A2 = 5 };   // F (REV_A2) : the ORIGINAL A, only bluer and with shorter big circles
int      sx_rv = REV_A2;   // Reverberation's variant, //aio scfx variant a|b|c|d|e ; E = water + trapped waves bouncing + splashes (dev, A/B captures) : 0 A the original ; 1 B the waves out, bubbles cut ; 2 C = B + the contact and its crests
int      sx_shotN = 0, sx_shotI = 0; unsigned sx_shotT0 = 0; float sx_shotSpan = 2.6f, sx_shotCX = 0, sx_shotCY = 0, sx_shotHalf = 0;   // //aio scfx shot
char     sx_shotSub[48] = "";   // the series' own folder under data\scfxshot\ ("" = the folder itself)
struct ShotSeries { int n; float span; char sub[48]; };
ShotSeries sx_shotQ[4]; int sx_shotQn = 0, sx_shotQi = 0;   // //aio scfx capture : the series still to take, one after another   // iso : //aio scfx <name> ... matter -- the material crown ALONE (no particles, waves, breath, blast, distortion)   // loop : the skillchain animation alone, over and over (no opener / wait)
int      sx_scene = 0;
float    sx_zoom = 1.25f;    // the sphere, its animation, its texts and icons (//aio scfx size)
float    sx_lzoom = 1.25f;   // the list under it (//aio scfx list)
bool rev_orig() { return sx_rv == 0 || sx_rv == REV_A2; }
int  sx_lq = 1;   // Liquefaction : 0 A the original, 1 B lava (cracks web, the centre made of lava, heavy drops) -- //aio scfx variant liq a|b
bool lq_new(int P) { return P == SCP_Liquefaction && sx_lq == 1; }
int  sx_im = 2;   // Impaction (default C, chosen 2026-11-23 : bold Tesla arcs) : 0 A the original, 1 B (the template : a charged centre, charge -> discharge, short bolts, an electric wave, sparks)
bool im_new(int P) { return P == SCP_Impaction && sx_im >= 1; }   // B thin Tesla arcs, C bold Tesla arcs, D a plasma globe (all on the template)
bool sx_rippleJag = false;   // water_ripple, ELECTRIC (Impaction B) : jagged, broken, flickering -- not a vaporous ring
u32  sx_rippleTex = 0;   // water_ripple's texture for one call (0 = the caustics) : Impaction B rides its wave on the ridged filaments
// the variant switch of a property (//aio scfx capture <prop> <letter> sets it) ; 0 = none (no variants yet)
int* var_of(int prop) { return prop == SCP_Reverberation ? &sx_rv : prop == SCP_Liquefaction ? &sx_lq : prop == SCP_Impaction ? &sx_im : 0; }   // Impaction : a b c d   // A and F : the original drawing (its waves, its bubbles)
bool water_variant(int prop) { return prop == SCP_Reverberation && (sx_rv == REV_WATER || sx_rv == REV_BOUNCE); }   // D and E : their own drawing (no shared blast / breath / distortion / particles)
float    sx_t = 0.0f;
float    sx_clock = 0.0f, sx_bt = -1.0f;   // the animation's clock and the seconds since the chain formed -- the demo's or the live box's
LARGE_INTEGER sx_last = {}, sx_freq = {};
u32      sx_elem = 0; TexRetry sx_elemR;
u32      sx_caus = 0;   // CAUSTICS (Reverberation F) : a tileable web of curved, uneven bright lines -- water seen on a pool floor, not the plasma filaments of sx_noise
u32      sx_noiseS = 0, sx_ringTex = 0;
float    sx_vScale = 1.0f;   // noise_ring's texture scale ACROSS the band, for one call (the caustics : bigger, round cells)   // noiseS : SMOOTH fbm (no ridges -- the ridged one reads as plasma filaments) for water ; ringTex : noise_ring's texture for one call (0 = sx_noise)
u32      sx_noise = 0, sx_soft = 0, sx_atlas = 0;   // sx_atlas : 8 particle shapes, one per element (256x32)
// the current skillchain's ELEMENTS, as particle cells (every particle spawned takes the next one : a Lv.2 throws its two
// elements' particles, a Lv.3 its four) ; its LEVEL scales how spectacular it is (Lv.1 subtle .. Lv.3 / 4 grand)
// REACH : how far the effects spread round the sphere (1 = as designed ; the sphere's own size is unchanged)
float REACH = 0.65f;             // set per draw : the config's scReach (the demo pins 0.65)
float sx_partMul = 1.0f;          // particle density 0..1 (scParticles) : a spawn is dropped with that probability
unsigned sx_cUse = 0;             // the last frame the live circle drew (the distortion copy is given back 5 s after)
float    sx_R = 36.0f;   // the sphere's radius this frame (spawn scales the reach with it)
float    sx_rise = 1.0f;   // the loop rising out of the blast (0.12 .. 1 over its first 0.7 s)
int      sx_els[4] = { 0 }, sx_nels = 1, sx_elc = 0; float sx_lvf = 1.0f, sx_pf = 1.0f;   // sx_lvf : waves / flashes ; sx_pf : particle count
unsigned char sx_genTries = 0; unsigned sx_genNext = 0;

const float T_WAIT = 2.6f, T_GO = 3.2f, T_BURST = 6.0f, T_REST = 1.4f, CW = 1.4f;
const float FIRE_AT = T_WAIT + T_GO, T_END = FIRE_AT + T_BURST + T_REST;

enum { PM_EMBER = 0, PM_DROP, PM_GLIDE, PM_SWIRL, PM_OUT, PM_IN, PM_ROCK, PM_PULL, PM_ORBIT };
struct Part { float x, y, vx, vy, l, life, s, pa, pr, w, sd; unsigned char m, el; };   // el : its element's particle (atlas cell)   // sd : a per-particle seed (a rock's shape)
const int NPART = 700;
Part  sx_p[NPART]; int sx_np = 0;
float sx_acc[3] = {};
unsigned sx_rng = 0x2468ACEu;
float rnd() { sx_rng = sx_rng * 1103515245u + 12345u; return (float)((sx_rng >> 8) & 0xFFFF) / 65535.0f; }
void  add(const Part& p) { if (sx_np < NPART) sx_p[sx_np++] = p; }

struct V2 { float x, y, z, rhw; u32 c; float u0, v0, u1, v1; };
Vtx sx_q[NPART * 6 + 64];
V2  sx_m[2][2 * 129];
Vtx sx_rb[2 * 64];
// PERF : (1) fx_state remembers what it set -- the same texture / stages / blend again costs nothing (any other
// state path : cqs(), blend(), fx_end(), icon(), text() forgets it) ; (2) every ADDITIVE soft ribbon / star inside
// a bat_begin() .. bat_end() scope goes into ONE triangle list, one draw (additive blending is order-free, so the
// result is the same) -- a bolt was 3 draws (x forks), a ring of stars one draw each.
u32 sx_fs = 0, sx_fsTex = 0, sx_fsMode = 0;
bool sx_dOk = false;
float sx_fdt = 0.0f;   // Fragmentation : this frame's step (its storm's random strikes)   // the scene distortion ran this frame (the drawn waves step back)
Vtx sx_bq[12288]; int sx_bn = 0; bool sx_bat = false;
int sx_pDraw = 0, sx_pVtx = 0;   // this frame's own draws / vertices (the perf line in the log)

u32 C(u32 rgb, float a) { if (a < 0.0f) a = 0.0f; if (a > 1.0f) a = 1.0f; return ((u32)(a * 255.0f + 0.5f) << 24) | (rgb & 0xFFFFFFu); }
u32 lerp(u32 a, u32 b, float t) {
    if (t < 0.0f) t = 0.0f; if (t > 1.0f) t = 1.0f;
    const int r = (int)(((a >> 16) & 255) + (((int)((b >> 16) & 255) - (int)((a >> 16) & 255)) * t));
    const int g = (int)(((a >> 8) & 255) + (((int)((b >> 8) & 255) - (int)((a >> 8) & 255)) * t));
    const int bl = (int)((a & 255) + (((int)(b & 255) - (int)(a & 255)) * t));
    return ((u32)r << 16) | ((u32)g << 8) | (u32)bl;
}

// ---------------------------------------------------------------- palettes ----
// per property : hot / mid / cold (the colour ramp of everything it emits) ; dark = drawn as a shadow, not a light
struct Pal { u32 hot, mid, cold; bool dark; };
Pal pal(int p) {
    switch (p) {
        case SCP_Liquefaction:  return { 0xFFF4D0, 0xFF8A2A, 0xC8280A, false };
        case SCP_Impaction:     return { 0xFFFFFF, 0xC9A7FF, 0x6A3AD0, false };
        case SCP_Detonation:    return { 0xE9FFF0, 0x3FD46A, 0x1A7A3A, false };
        case SCP_Scission:      return { 0xFFE8C0, 0xC08A48, 0x6A4220, false };
        case SCP_Reverberation: if (sx_rv == 0) return { 0xE6F6FF, 0x4AA8FF, 0x1A4A9A, false };   // A : the ORIGINAL, untouched
                                if (sx_rv == REV_A2) return { 0xBFE6FF, 0x3A9BFF, 0x1A4A9A, false };   // F : A, a luminous WATER blue
                                return { 0x9FDCFF, 0x2A86F0, 0x0C3A8A, false };   // WATER : deep blue, light only in its highlights (white read as ice = Induration)
        case SCP_Induration:    return { 0xF0FFFF, 0x7FE6FF, 0x2A8A9A, false };
        case SCP_Compression:   return { 0xFFD0EA, 0xE0479F, 0x5A1040, false };
        case SCP_Transfixion:   return { 0xFFFFFF, 0xFFF3C4, 0xD8C890, false };
        case SCP_Fusion:        return { 0xFFFFFF, 0xFF8A3A, 0xE0300E, false };
        case SCP_Fragmentation: return { 0xF0FFF4, 0x3FD46A, 0xB98CFF, false };
        case SCP_Distortion:    return { 0xF0FFFF, 0x7FE6FF, 0x2A6AD0, false };
        case SCP_Gravitation:   return { 0xF0C8FF, 0xA060C0, 0x5A2A3A, false };
        case SCP_Light:         return { 0xFFFFFF, 0xFFF6DA, 0xFFD76A, false };
        case SCP_Darkness:      return { 0xE0C8FF, 0x8A4AF0, 0x2A0A4A, false };
        case SCP_Radiance:      return { 0xFFFFFF, 0xFFF0B0, 0xFFC040, false };
        default:                return { 0xFFC0CC, 0xD03060, 0x3A0A14, false };   // Umbra
    }
}
u32 heat(float h) {   // molten fire cooling : 1 white-hot .. 0 ash
    if (h > 0.8f) return lerp(0xFFE9A8, 0xFFFFF4, (h - 0.8f) / 0.2f);
    if (h > 0.55f) return lerp(0xFF9A2A, 0xFFE9A8, (h - 0.55f) / 0.25f);
    if (h > 0.3f) return lerp(0xD8320C, 0xFF9A2A, (h - 0.3f) / 0.25f);
    return lerp(0x3A0A04, 0xD8320C, h / 0.3f);
}
u32 ramp(int p, float h) {
    if (p == SCP_Liquefaction || p == SCP_Fusion) return heat(h);
    const Pal q = pal(p);
    return h > 0.5f ? lerp(q.mid, q.hot, (h - 0.5f) * 2.0f) : lerp(q.cold, q.mid, h * 2.0f);
}
u32 elem_rgb(int e) {
    static const u32 E[8] = { 0xFF5F4F, 0x7FE6FF, 0x3FD46A, 0xD39A5A, 0xB98CFF, 0x4AA8FF, 0xFFFFFF, 0xA050E0 };   // SCElem order
    return E[e & 7];
}

// ---------------------------------------------------------------- render states ----
// shadow colour : how much each channel is taken away (black at alpha a = a everywhere ; a tint keeps its hue)
u32 shade(u32 c) {
    const float a = (float)(c >> 24) / 255.0f;
    const u32 r = (u32)(a * (255 - ((c >> 16) & 255))), g = (u32)(a * (255 - ((c >> 8) & 255))), b = (u32)(a * (255 - (c & 255)));
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}
void blend(u32 dev, bool add) { sx_fs = 0; dSetRS(dev, D3DRS_SRCBLEND, D3DBLEND_SRCALPHA); dSetRS(dev, D3DRS_DESTBLEND, add ? D3DBLEND_ONE : D3DBLEND_INVSRCALPHA); }
// textured : colour = texture x diffuse (the shape lives in the texture's RGB ; alpha from the diffuse only -- a
// texture's alpha mis-samples while a zone loads), WRAP ; `two` = the same texture again on stage 1, its own UVs
void fdraw(u32 dev, u32 pt, int prims, const void* v, u32 st) { ++sx_pDraw; sx_pVtx += prims; dDrawUP(dev, pt, (u32)prims, v, st); }
void cqs(u32 dev) { sx_fs = 0; dColorQuadState(dev); }
void fx_state(u32 dev, u32 tex, bool two, bool add = true) {
    const u32 mode = (two ? 1u : 0u) | (add ? 2u : 0u);
    if (sx_fs && sx_fsTex == tex && sx_fsMode == mode) return;   // already set, nothing touched it since
    dSetVS(dev, two ? FVF_TEX2 : FVF_XYZRHW_DIFFUSE_TEX1);
    dSetRS(dev, D3DRS_ALPHABLENDENABLE, 1); blend(dev, add);
    if (!add) { dSetRS(dev, D3DRS_SRCBLEND, D3DBLEND_ZERO); dSetRS(dev, D3DRS_DESTBLEND, 4 /* INVSRCCOLOR */); }   // the shadow
    dSetRS(dev, D3DRS_SHADEMODE, D3DSHADE_GOURAUD);
    for (int s = 0; s < (two ? 2 : 1); ++s) {
        dSetTex(dev, s, tex);
        dSetTSS(dev, s, D3DTSS_TEXCOORDINDEX, s);
        dSetTSS(dev, s, D3DTSS_COLOROP, s ? TOP_MODULATE2X : D3DTOP_MODULATE);
        dSetTSS(dev, s, D3DTSS_COLORARG1, D3DTA_TEXTURE); dSetTSS(dev, s, D3DTSS_COLORARG2, s ? TA_CURRENT : D3DTA_DIFFUSE);
        dSetTSS(dev, s, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1); dSetTSS(dev, s, D3DTSS_ALPHAARG1, s ? TA_CURRENT : D3DTA_DIFFUSE);
        dSetTSS(dev, s, D3DTSS_ADDRESSU, D3DTADDRESS_WRAP); dSetTSS(dev, s, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
        dSetTSS(dev, s, D3DTSS_MINFILTER, D3DTEXF_LINEAR); dSetTSS(dev, s, D3DTSS_MAGFILTER, D3DTEXF_LINEAR); dSetTSS(dev, s, D3DTSS_MIPFILTER, D3DTEXF_NONE);
        { union { float f; u32 u; } lod; lod.f = 0.0f; dSetTSS(dev, s, D3DTSS_MIPMAPLODBIAS, lod.u); }   // Font::begin leaves +0.2
    }
    if (!two) { dSetTSS(dev, 1, D3DTSS_COLOROP, D3DTOP_DISABLE); dSetTSS(dev, 1, D3DTSS_ALPHAOP, D3DTOP_DISABLE); }
    dSetTSS(dev, 2, D3DTSS_COLOROP, D3DTOP_DISABLE); dSetTSS(dev, 2, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    sx_fs = 1; sx_fsTex = tex; sx_fsMode = mode;
}
void fx_end(u32 dev) {
    dSetTex(dev, 1, 0); dSetTSS(dev, 1, D3DTSS_COLOROP, D3DTOP_DISABLE); dSetTSS(dev, 1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    dSetTSS(dev, 1, D3DTSS_TEXCOORDINDEX, 1); dSetTSS(dev, 0, D3DTSS_TEXCOORDINDEX, 0);
    // the address modes back to D3D's default (WRAP) on both stages : distort() leaves stage 0 on CLAMP, and the next
    // widget's textured draw would inherit it (rule 8 : no dirty state for the next widget)
    for (int s = 0; s < 2; ++s) { dSetTSS(dev, s, D3DTSS_ADDRESSU, D3DTADDRESS_WRAP); dSetTSS(dev, s, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP); }
    blend(dev, false); cqs(dev);
}

void ensure_gen(u32 dev) {
    if ((sx_noise && sx_soft && sx_atlas && sx_noiseS && sx_caus) || sx_genTries >= 8) return;
    if (sx_genNext && (int)(GetTickCount() - sx_genNext) < 0) return;
    static u32 px[128 * 128];
    if (!sx_noise) {
        for (int y = 0; y < 128; ++y) for (int x = 0; x < 128; ++x) {
            const float n = fbm(x / 128.0f, y / 128.0f), m = fbm(x / 128.0f + 0.37f, y / 128.0f + 0.71f);
            float r = 1.0f - fabsf(2.0f * n - 1.0f); r = r * r * r;
            float v = 0.18f + 0.62f * r + 0.35f * m * m; if (v > 1.0f) v = 1.0f;
            const u32 g = (u32)(v * 255.0f); px[y * 128 + x] = 0xFF000000u | (g << 16) | (g << 8) | g;
        }
        sx_noise = make_texture_argb(dev, 128, 128, px);
    }
    if (!sx_caus) {   // CAUSTICS : a jittered 5x5 cell web (F2 - F1 edges), the domain bent by periodic sines (curved lines), the line
                      // width varying -- tileable on both axes (the cells wrap, the bends are whole periods)
        const int G = 5; float pxs[G][G], pys[G][G];
        auto hh = [](int i, int j, float s2) { const float v = sinf(i * 127.1f + j * 311.7f + s2 * 74.7f) * 43758.5453f; return v - floorf(v); };
        for (int i = 0; i < G; ++i) for (int j = 0; j < G; ++j) { pxs[i][j] = (i + 0.15f + 0.7f * hh(i, j, 1.0f)) / G; pys[i][j] = (j + 0.15f + 0.7f * hh(i, j, 2.0f)) / G; }
        for (int y = 0; y < 128; ++y) for (int x = 0; x < 128; ++x) {
            const float u0 = (x + 0.5f) / 128.0f, v0 = (y + 0.5f) / 128.0f;
            float u = u0 + 0.045f * sinf(TAU * (2.0f * v0 + 0.3f)) + 0.025f * sinf(TAU * (3.0f * v0 + u0 + 0.7f));
            float v = v0 + 0.045f * sinf(TAU * (2.0f * u0 + 0.1f)) + 0.025f * sinf(TAU * (3.0f * u0 - v0 + 0.4f));
            u -= floorf(u); v -= floorf(v);
            const float wid = 0.07f + 0.05f * (0.5f + 0.5f * sinf(TAU * (u0 + 2.0f * v0)));
            const int ci = (int)(u * G), cj = (int)(v * G); float d1 = 9.0f, d2 = 9.0f;
            for (int di = -1; di <= 1; ++di) for (int dj = -1; dj <= 1; ++dj) {
                const int i = ci + di, j = cj + dj, im = ((i % G) + G) % G, jm = ((j % G) + G) % G;
                const float ox = pxs[im][jm] + floorf((float)i / G), oy = pys[im][jm] + floorf((float)j / G);
                const float d = sqrtf((u - ox) * (u - ox) + (v - oy) * (v - oy));
                if (d < d1) { d2 = d1; d1 = d; } else if (d < d2) d2 = d;
            }
            const float e = (d2 - d1) * G; float val = 0.10f + 0.90f * expf(-e / wid) + 0.08f * (1.0f - (d1 * G < 1.0f ? d1 * G : 1.0f));
            if (val > 1.0f) val = 1.0f;
            const u32 g = (u32)(val * 255.0f); px[y * 128 + x] = 0xFF000000u | (g << 16) | (g << 8) | g;
        }
        sx_caus = make_texture_argb(dev, 128, 128, px);
    }
    if (!sx_noiseS) {   // WATER's body : broad smooth swells, no filaments
        for (int y = 0; y < 128; ++y) for (int x = 0; x < 128; ++x) {
            const float n = fbm(x / 128.0f + 0.13f, y / 128.0f + 0.41f), m = fbm(x / 128.0f + 0.57f, y / 128.0f + 0.29f);
            float v = 0.22f + 0.55f * n + 0.30f * m * m; if (v > 1.0f) v = 1.0f; if (v < 0.0f) v = 0.0f;
            const u32 g = (u32)(v * 255.0f); px[y * 128 + x] = 0xFF000000u | (g << 16) | (g << 8) | g;
        }
        sx_noiseS = make_texture_argb(dev, 128, 128, px);
    }
    if (!sx_soft) {
        for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x) {
            const float dx = (x + 0.5f) / 32.0f - 1.0f, dy = (y + 0.5f) / 32.0f - 1.0f, d = sqrtf(dx * dx + dy * dy);
            float v = d >= 1.0f ? 0.0f : expf(-d * d * 4.5f) * (1.0f - d) + 0.55f * expf(-d * d * 40.0f);
            if (v > 1.0f) v = 1.0f; const u32 g = (u32)(v * 255.0f); px[y * 64 + x] = 0xFF000000u | (g << 16) | (g << 8) | g;
        }
        sx_soft = make_texture_argb(dev, 64, 64, px);
    }
    if (!sx_atlas) {   // 8 cells of 32x32 : the shape lives in the RGB (grey), like every sprite here
        static u32 at[256 * 32];
        auto segd = [](float px, float py, float ax, float ay, float bx, float by) {   // distance to a segment
            const float vx = bx - ax, vy = by - ay, wx = px - ax, wy = py - ay, l2 = vx * vx + vy * vy;
            float u = l2 > 0.0f ? (wx * vx + wy * vy) / l2 : 0.0f; if (u < 0.0f) u = 0.0f; if (u > 1.0f) u = 1.0f;
            const float dx = px - (ax + vx * u), dy = py - (ay + vy * u); return sqrtf(dx * dx + dy * dy); };
        for (int c = 0; c < 8; ++c) for (int y = 0; y < 32; ++y) for (int x = 0; x < 32; ++x) {
            const float X = (x + 0.5f) / 16.0f - 1.0f, Y = (y + 0.5f) / 16.0f - 1.0f, d = sqrtf(X * X + Y * Y), th = atan2f(Y, X);
            float v = 0.0f;
            switch (c) {
            case 0: v = expf(-d * d * 3.5f) * (1.0f - d) + 0.6f * expf(-d * d * 30.0f); break;                                  // EMBER : a hot soft core
            case 1: v = expf(-d * d * 40.0f) + 0.85f * (expf(-X * X * 150.0f) * (1.0f - fabsf(Y)) + expf(-Y * Y * 150.0f) * (1.0f - fabsf(X)))
                      + 0.5f * (expf(-(X - Y) * (X - Y) * 120.0f) + expf(-(X + Y) * (X + Y) * 120.0f)) * (1.0f - d); break;      // SPARK : a point, crossed rays
            case 2: { const float dd = sqrtf(X * X + (Y - 0.45f) * (Y - 0.45f)), ang = atan2f(Y - 0.45f, X);                 // WISP : a curl of air
                      const float span = 1.0f - fabsf(ang + 1.5708f) / 1.5f; v = expf(-(dd - 0.62f) * (dd - 0.62f) * 120.0f) * (span > 0.0f ? span : 0.0f); } break;
            case 3: { const float rr = 0.6f + 0.06f * sinf(th * 5.0f + 1.0f) + 0.08f * sinf(th * 3.0f); const float q = 1.0f - d / rr;   // DUST : a grain
                      v = q > 0.0f ? powf(q, 0.6f) * (0.7f + 0.3f * fbm(x / 32.0f, y / 32.0f)) : 0.0f; } break;
            case 4: v = expf(-(d - 0.72f) * (d - 0.72f) * 90.0f) + (d < 0.72f ? 0.12f : 0.0f) + expf(-((X + 0.3f) * (X + 0.3f) + (Y + 0.3f) * (Y + 0.3f)) * 70.0f); break;   // BUBBLE : a rim, a glint
            case 5: { float m = 9.0f;                                                                                       // SNOWFLAKE : six arms, branched
                      for (int a = 0; a < 6; ++a) { const float ca = cosf(a * 1.0472f), sa = sinf(a * 1.0472f);
                          m = fminf(m, segd(X, Y, 0, 0, ca * 0.9f, sa * 0.9f));
                          for (int b = -1; b <= 1; b += 2) { const float bx = ca * 0.5f, by = sa * 0.5f, ba = a * 1.0472f + b * 0.7f; m = fminf(m, segd(X, Y, bx, by, bx + cosf(ba) * 0.28f, by + sinf(ba) * 0.28f)); } }
                      v = expf(-m * m * 260.0f) + 0.5f * expf(-d * d * 25.0f); } break;
            case 6: v = expf(-d * d * 30.0f) + 0.9f * expf(-X * X * 110.0f) * fmaxf(0.0f, 1.0f - fabsf(Y)) + 0.9f * expf(-Y * Y * 110.0f) * fmaxf(0.0f, 1.0f - fabsf(X)); break;   // SPARKLE : a 4-point star
            default: v = expf(-d * d * 2.5f) * (1.0f - d) * (0.45f + 0.7f * fbm(x / 32.0f * 2.0f + 0.3f, y / 32.0f * 2.0f)); break;   // SMOKE : a ragged puff
            }
            if (d >= 1.0f) v = 0.0f; if (v < 0.0f) v = 0.0f; if (v > 1.0f) v = 1.0f;
            const u32 g = (u32)(v * 255.0f); at[y * 256 + c * 32 + x] = 0xFF000000u | (g << 16) | (g << 8) | g;
        }
        sx_atlas = make_texture_argb(dev, 256, 32, at);
    }
    // Rule 10 : the success AND the give-up are said. Before, a miss right after a zone-in left the spheres textureless
    // for the session with no line at all (8 tries 300 ms apart, then silence ; only a device change re-arms it).
    if (!sx_noise || !sx_soft || !sx_atlas || !sx_noiseS || !sx_caus) {
        ++sx_genTries; sx_genNext = (GetTickCount() + 300u) | 1u;
        if (sx_genTries >= 8)
            windower::debug::log("SCFX generated textures : gave up after %d tries (missing :%s%s%s%s%s) -- re-armed by the next device change",
                                 (int)sx_genTries, sx_noise ? "" : " noise", sx_soft ? "" : " soft", sx_atlas ? "" : " atlas", sx_noiseS ? "" : " noiseS", sx_caus ? "" : " caustics");
    } else windower::debug::log("SCFX generated textures : ready (try %d)", (int)sx_genTries + 1);
}

// ---------------------------------------------------------------- primitives ----
const int CELL[8] = { 0, 4, 3, 1, 5, 2, 6, 7 };   // SCElem -> element_icons.raw cell (0 Fire 1 Earth 2 Water 3 Wind 4 Ice 5 Lightning 6 Light 7 Dark)
void icon(u32 dev, int el, float cx, float cy, float sz, float a) {
    if (!sx_elem) return;
    const int c = CELL[el & 7];
    sx_fs = 0; dTexQuadState(dev, sx_elem);
    tquad(dev, snap(cx - sz * 0.5f), snap(cy - sz * 0.5f), sz, sz, c / 8.0f, (c + 1) / 8.0f, 0.0f, 1.0f, C(0xFFFFFF, a), C(0xFFFFFF, a));
    cqs(dev);
}
// a quad into the sprite batch ; (ax,ay) = unit long axis, lx/ly = half length / width
int put(int n, float cx, float cy, float ax, float ay, float lx, float ly, u32 col, float u0 = 0.0f, float u1 = 1.0f) {
    if (n + 6 > (int)(sizeof(sx_q) / sizeof(sx_q[0]))) return n;
    const float px = -ay, py = ax;
    const float X[4] = { cx - ax * lx - px * ly, cx + ax * lx - px * ly, cx + ax * lx + px * ly, cx - ax * lx + px * ly };
    const float Y[4] = { cy - ay * lx - py * ly, cy + ay * lx - py * ly, cy + ay * lx + py * ly, cy - ay * lx + py * ly };
    const float U[4] = { u0, u1, u1, u0 }; static const float W[4] = { 0, 0, 1, 1 };
    static const int ix[6] = { 0, 1, 2, 0, 2, 3 };
    for (int j = 0; j < 6; ++j) { Vtx& v = sx_q[n + j]; const int q = ix[j]; v.x = X[q] - 0.5f; v.y = Y[q] - 0.5f; v.z = 0; v.rhw = 1; v.color = col; v.u = U[q]; v.v = W[q]; }
    return n + 6;
}
// a TAIL behind a particle : a tapered soft triangle, full at the head, nothing at the tip -- a motion blur, not a stick
int tail(int n, float x, float y, float ax, float ay, float len, float w, u32 col, float uc = 0.5f) {
    if (n + 3 > (int)(sizeof(sx_q) / sizeof(sx_q[0]))) return n;
    const float px = -ay * w, py = ax * w;
    const float X[3] = { x + px, x - px, x - ax * len }, Y[3] = { y + py, y - py, y - ay * len }, U[3] = { uc, uc, uc }, V[3] = { 0.0f, 1.0f, 0.5f };
    const u32 Cc[3] = { col, col, col & 0x00FFFFFFu };
    for (int j = 0; j < 3; ++j) { Vtx& v = sx_q[n + j]; v.x = X[j] - 0.5f; v.y = Y[j] - 0.5f; v.z = 0; v.rhw = 1; v.color = Cc[j]; v.u = U[j]; v.v = V[j]; }
    return n + 3;
}
// a textured triangle into the batch (crystal faces : the noise shimmers through them)
int tri(int n, float x0, float y0, float x1, float y1, float x2, float y2, u32 c0, u32 c1, u32 c2, float uo) {
    if (n + 3 > (int)(sizeof(sx_q) / sizeof(sx_q[0]))) return n;
    const float X[3] = { x0, x1, x2 }, Y[3] = { y0, y1, y2 }; const u32 Cc[3] = { c0, c1, c2 };
    for (int j = 0; j < 3; ++j) { Vtx& v = sx_q[n + j]; v.x = X[j] - 0.5f; v.y = Y[j] - 0.5f; v.z = 0; v.rhw = 1; v.color = Cc[j]; v.u = X[j] * 0.01f + uo; v.v = Y[j] * 0.01f; }
    return n + 3;
}
void flush(u32 dev, int n, u32 tex, bool add = true) { if (n < 3 || !tex) return; if (!add) for (int i = 0; i < n; ++i) sx_q[i].color = shade(sx_q[i].color); fx_state(dev, tex, false, add); fdraw(dev, D3DPT_TRIANGLELIST, n / 3, sx_q, sizeof(Vtx)); }
void bat_flush(u32 dev) { if (sx_bn >= 3 && sx_soft) { fx_state(dev, sx_soft, false, true); fdraw(dev, D3DPT_TRIANGLELIST, sx_bn / 3, sx_bq, sizeof(Vtx)); } sx_bn = 0; }
void bat_begin() { sx_bat = true; sx_bn = 0; }
void bat_end(u32 dev) { bat_flush(dev); sx_bat = false; }
void bat_add(u32 dev, const Vtx* v, int n) {   // n vertices of a triangle list
    if (sx_bn + n > (int)(sizeof(sx_bq) / sizeof(sx_bq[0]))) bat_flush(dev);
    if (n > (int)(sizeof(sx_bq) / sizeof(sx_bq[0]))) return;
    memcpy(sx_bq + sx_bn, v, n * sizeof(Vtx)); sx_bn += n;
}
void bloom(u32 dev, float cx, float cy, float r, u32 col, bool add = true) { if (r > sx_R * 2.8f) r = sx_R * 2.8f; flush(dev, put(0, cx, cy, 1.0f, 0.0f, r, r, col), sx_soft, add); }
void star(u32 dev, float x, float y, float z, u32 col) {   // a 4-point twinkle : two crossed streaks + a glow
    int n = put(0, x, y, 1.0f, 0.0f, z, z * 0.12f, col); n = put(n, x, y, 0.0f, 1.0f, z, z * 0.12f, col);
    n = put(n, x, y, 1.0f, 0.0f, z * 0.45f, z * 0.45f, col);
    if (sx_bat) bat_add(dev, sx_q, n); else flush(dev, n, sx_soft);
}
// a soft RIBBON along a polyline : per-point half width + colour ; the soft sprite sampled across (v) = soft edges
void ribbon(u32 dev, const float* xs, const float* ys, int n, const float* hw, const u32* col, bool add = true) {
    if (n < 2 || n > 64 || !sx_soft) return;
    for (int i = 0; i < n; ++i) {
        const int a = i > 0 ? i - 1 : 0, b = i < n - 1 ? i + 1 : n - 1;
        float dx = xs[b] - xs[a], dy = ys[b] - ys[a]; const float l = sqrtf(dx * dx + dy * dy) + 1e-4f; dx /= l; dy /= l;
        Vtx& L = sx_rb[2 * i]; Vtx& Rr = sx_rb[2 * i + 1];
        L.x = xs[i] - dy * hw[i] - 0.5f; L.y = ys[i] + dx * hw[i] - 0.5f; Rr.x = xs[i] + dy * hw[i] - 0.5f; Rr.y = ys[i] - dx * hw[i] - 0.5f;
        L.z = Rr.z = 0; L.rhw = Rr.rhw = 1; L.color = Rr.color = col[i]; L.u = Rr.u = 0.5f; L.v = 0.0f; Rr.v = 1.0f;
    }
    if (sx_bat && add) {   // into the batch : the strip unrolled to a list
        if (sx_bn + 6 * (n - 1) > (int)(sizeof(sx_bq) / sizeof(sx_bq[0]))) bat_flush(dev);
        for (int i = 0; i + 1 < n; ++i) { Vtx* o = sx_bq + sx_bn; const Vtx* q = sx_rb + 2 * i;
            o[0] = q[0]; o[1] = q[1]; o[2] = q[2]; o[3] = q[1]; o[4] = q[3]; o[5] = q[2]; sx_bn += 6; }
        return;
    }
    if (!add) for (int i = 0; i < 2 * n; ++i) sx_rb[i].color = shade(sx_rb[i].color);
    fx_state(dev, sx_soft, false, add);
    fdraw(dev, D3DPT_TRIANGLESTRIP, 2 * n - 2, sx_rb, sizeof(Vtx));
}
// a lightning bolt from radius r0 to r1 at angle a : jagged, a wide coloured glow + a white core
void bolt(u32 dev, float CX, float CY, float a, float r0, float r1, float w, u32 glow, float al) {
    float xs[8], ys[8], hw[8]; u32 c[8]; float aa = a;
    for (int i = 0; i < 8; ++i) { const float r = r0 + (r1 - r0) * i / 7.0f; if (i) aa += (rnd() - 0.5f) * 0.32f; xs[i] = CX + cosf(aa) * r; ys[i] = CY + sinf(aa) * r; }
    for (int i = 0; i < 8; ++i) { const float tp = 1.0f - i / 9.0f; hw[i] = w * 3.0f * tp; c[i] = C(glow, 0.45f * al); }
    ribbon(dev, xs, ys, 8, hw, c);
    for (int i = 0; i < 8; ++i) { hw[i] = w * 0.9f * (1.0f - i / 9.0f); c[i] = C(0xFFFFFF, al); }
    ribbon(dev, xs, ys, 8, hw, c);
    if (rnd() < 0.7f) {   // a branch
        const int j = 3 + (int)(rnd() * 3.0f); const float ba = atan2f(ys[j] - CY, xs[j] - CX) + (rnd() < 0.5f ? -0.45f : 0.45f), br = sqrtf((xs[j] - CX) * (xs[j] - CX) + (ys[j] - CY) * (ys[j] - CY));
        float bx[4], by[4], bh[4]; u32 bc[4]; float b2 = ba;
        for (int i = 0; i < 4; ++i) { const float r = br + (r1 - r0) * 0.35f * i / 3.0f; if (i) b2 += (rnd() - 0.5f) * 0.4f; bx[i] = CX + cosf(b2) * r; by[i] = CY + sinf(b2) * r; bh[i] = w * 1.4f * (1.0f - i / 4.0f); bc[i] = C(glow, 0.7f * al); }
        ribbon(dev, bx, by, 4, bh, bc);
    }
}
// LIGHTNING by midpoint displacement : between A and B, each midpoint is pushed sideways by a random amount that
// halves at each level -> a fractal, chaotic path at every scale (not a straight line with a few kinks).
// `along` (optional) bends the base path : the points first follow the ring (CX,CY,r) from angle a0 to a1.
int lightning_path(float* xs, float* ys, float ax, float ay, float bx, float by, float disp) {
    const int N = 33; xs[0] = ax; ys[0] = ay; xs[N - 1] = bx; ys[N - 1] = by;
    for (int step = (N - 1) / 2; step >= 1; step /= 2) {
        for (int i = step; i < N - 1; i += 2 * step) {
            const float x0 = xs[i - step], y0 = ys[i - step], x1 = xs[i + step], y1 = ys[i + step];
            float nx = -(y1 - y0), ny = x1 - x0; const float l = sqrtf(nx * nx + ny * ny) + 1e-4f; nx /= l; ny /= l;
            const float d = (rnd() - 0.5f) * 2.0f * disp;
            xs[i] = (x0 + x1) * 0.5f + nx * d; ys[i] = (y0 + y1) * 0.5f + ny * d;
        }
        disp *= 0.55f;
    }
    return N;
}
void lightning_draw(u32 dev, const float* xs, const float* ys, int n, float w, u32 glow, float al) {
    float hw[33]; u32 c[33];
    // the three layers : wide faint glow, mid glow, white core. TUNE : data\tune.txt in a dev build (ui/tune.h).
    const struct { float wm, am; bool white; } L[3] = {
        { TUNE("bolt.glow_w", 3.4f), TUNE("bolt.glow_a", 0.2f), false },
        { TUNE("bolt.mid_w", 1.7f), TUNE("bolt.mid_a", 0.55f), false },
        { TUNE("bolt.core_w", 0.6f), 1.0f, true } };
    for (int l = 0; l < 3; ++l) {
        for (int i = 0; i < n; ++i) { const float tp = 0.55f + 0.45f * sinf(PI * i / (n - 1)); hw[i] = w * L[l].wm * tp; c[i] = C(L[l].white ? 0xFFFFFF : glow, L[l].am * al); }
        ribbon(dev, xs, ys, n, hw, c);
    }
}
// a chaotic bolt from A to B, with forks
// AN ELECTRIC WAVE (Impaction B, 2026-11-23 : the matter-ripple read as a pale jagged fog) : a ring of LIGHTNING spreading
// out -- several thin bright arcs with gaps between them, re-jittered every frame (they crackle), fading as they go
void arc_bolt(u32 dev, float CX, float CY, float a0, float a1, float r, float jit, float w, u32 glow, float al);   // below
void electric_wave(u32 dev, float CX, float CY, float r, float k, float a, float seed) {
    if (a <= 0.01f) return;
    const int NS = 6;
    for (int i = 0; i < NS; ++i) {
        const float a0 = seed + i * TAU / NS + 0.25f * sinf(seed * 3.1f + i), span = 0.55f + 0.30f * (0.5f + 0.5f * sinf(seed * 1.7f + i * 2.3f));
        arc_bolt(dev, CX, CY, a0, a0 + span, r, 3.2f * k, 0.9f * k, 0xB98CFF, a);
    }
    fx_end(dev);
}
void zap(u32 dev, float ax, float ay, float bx, float by, float disp, float w, u32 glow, float al) {
    float xs[33], ys[33]; const int n = lightning_path(xs, ys, ax, ay, bx, by, disp);
    lightning_draw(dev, xs, ys, n, w, glow, al);
    const int forks = (int)(rnd() * 1.8f);
    for (int f = 0; f < forks; ++f) {
        const int j = 6 + (int)(rnd() * 20.0f); const float dx = bx - ax, dy = by - ay, l = sqrtf(dx * dx + dy * dy) + 1e-4f;
        const float ang = atan2f(dy, dx) + (rnd() < 0.5f ? -1.0f : 1.0f) * (0.4f + rnd() * 0.7f), fl = l * (0.2f + rnd() * 0.3f);
        float fx[33], fy[33]; const int m = lightning_path(fx, fy, xs[j], ys[j], xs[j] + cosf(ang) * fl, ys[j] + sinf(ang) * fl, disp * 0.5f);
        lightning_draw(dev, fx, fy, m, w * 0.55f, glow, al * 0.85f);
    }
}
// an ARC of lightning crawling along the ring from a0 to a1 : jagged in and out, glow + white core
void arc_bolt(u32 dev, float CX, float CY, float a0, float a1, float r, float jit, float w, u32 glow, float al) {
    float xs[12], ys[12], hw[12]; u32 c[12];
    for (int i = 0; i < 12; ++i) { const float u = i / 11.0f, a = a0 + (a1 - a0) * u, rr = r + ((i && i < 11) ? (rnd() - 0.5f) * 2.0f * jit : 0.0f);
        xs[i] = CX + cosf(a) * rr; ys[i] = CY + sinf(a) * rr; hw[i] = w * 3.2f; c[i] = C(glow, 0.5f * al); }
    ribbon(dev, xs, ys, 12, hw, c);
    for (int i = 0; i < 12; ++i) { hw[i] = w; c[i] = C(0xFFFFFF, al); }
    ribbon(dev, xs, ys, 12, hw, c);
}
// an arc ribbon (wind gusts, spiral arms, tendrils) : from (a0,r0) to (a1,r1), tapered, alpha rising to the head
void arc_ribbon(u32 dev, float CX, float CY, float a0, float a1, float r0, float r1, float w, u32 rgb, float al, float wob, float t, bool add = true) {
    float xs[24], ys[24], hw[24]; u32 c[24];
    for (int i = 0; i < 24; ++i) {
        const float u = i / 23.0f, a = a0 + (a1 - a0) * u, r = r0 + (r1 - r0) * u + wob * sinf(t * 4.0f + u * 6.0f);
        xs[i] = CX + cosf(a) * r; ys[i] = CY + sinf(a) * r; hw[i] = w * (0.25f + 0.75f * sinf(u * PI)); c[i] = C(rgb, al * (0.2f + 0.8f * u));
    }
    ribbon(dev, xs, ys, 24, hw, c, add);
}

// a NOISE RING mesh : r0 -> r1 -> r2 (outer edge displaced), colours c0 c1 -> transparent ; UVs flow around (uSpd), out
// or in (vSpd), spiral (twist) ; two stages scroll against each other. dark = drawn as a shadow (normal blend)
// BUMPS : where ice forms, the water is pushed out of shape -- up to 4 (angle, height in px), refreshed each frame
float sx_bA[4] = { 0 }, sx_bH[4] = { 0 }, sx_bMax = 1.0f; int sx_nbm = 0;
float bumpf(float th) {
    float s2 = 0.0f;
    for (int i = 0; i < sx_nbm; ++i) { float d = th - sx_bA[i]; d -= TAU * floorf((d + PI) / TAU); s2 += sx_bH[i] * expf(-d * d * 20.0f); }
    return s2;
}
float sx_nrA0 = 0.0f, sx_nrSpan = 6.2831853f;   // noise_ring : the arc it covers (the whole ring by default)
struct RingFx { float r0, r1, r2, wave, wfreq, wspd; u32 c0, c1, c2; float a0, a1, uRep, uSpd, vSpd, twist; bool dark; bool inward; float patch; bool bumps; };   // patch 1 : drifting patches ; 2 : only on the bumps. bumps : the band pushed by them   // patch : > 0 = only in drifting patches (forming / melting)   // inward : both stages flow in (no counter-scroll)
// ---- CREST MODULATION (Reverberation C) : the band's OWN grain lit and swollen where a crest runs -- brightness, tint
// and outer radius driven together by the distance to the crest (no separate ribbon : those read as bars spinning) ----
struct CrestMod { bool on; float base, sig, ext; int n; float ang[4], amp[4]; u32 tint; };
CrestMod sx_cm = {};
const float REV_TH0 = 0.7854f;   // the contact : 4:30 (screen angles, y down) -- clear of the title, the cards, the icons' axis
// Reverberation C's state at bt : the dimmed rest of the crown, two crests running apart from the contact, their echo
void rev_crest(float bt, bool lobes) {
    const float PER = 2.6f, bb = bt < 0.0f ? 0.0f : bt; const int n = (int)(bb / PER); const float tau = bb - n * PER, amp = n ? 0.65f : 1.0f;
    // MEASURED (captures 2026-11-23) : the original band sits SATURATED white (250..255) almost all round -- a crest
    // from 68 to 100 % changed nothing visible. So the rest of the crown dims HARD while the gesture runs, then
    // comes back up by 0.8 s : the crests are the only white for that moment.
    float base = 0.88f; const float dip = 0.88f - 0.30f * amp - 0.25f;   // 0.33 at the first strike, 0.44 at a repeat
    if (tau < 0.06f) base = 0.88f - (0.88f - dip) * (tau / 0.06f); else if (tau < 0.55f) base = dip; else if (tau < 0.85f) base = dip + (0.88f - dip) * ((tau - 0.55f) / 0.30f);
    sx_cm.on = true; sx_cm.base = base; sx_cm.sig = 0.22f; sx_cm.tint = 0xFFFFFF; sx_cm.n = 0;
    if (!lobes) return;
    auto eo = [](float u) { return 1.0f - (1.0f - u) * (1.0f - u); };
    auto env = [](float u) { return (u < 0.15f ? u / 0.15f : 1.0f) * (u > 0.7f ? (1.0f - u) / 0.3f : 1.0f); };
    if (tau >= 0.05f && tau <= 0.43f) { const float u = (tau - 0.05f) / 0.38f, ph = 2.0944f * eo(u), e = env(u) * amp;      // the front : 120 deg in 380 ms, each way
        sx_cm.ang[sx_cm.n] = REV_TH0 + ph; sx_cm.amp[sx_cm.n++] = e; sx_cm.ang[sx_cm.n] = REV_TH0 - ph; sx_cm.amp[sx_cm.n++] = e; }
    if (tau >= 0.20f && tau <= 0.58f) { const float u = (tau - 0.20f) / 0.38f, ph = 1.5708f * eo(u), e = 0.4f * env(u) * amp;   // its echo : 90 deg, 40 %
        sx_cm.ang[sx_cm.n] = REV_TH0 + ph; sx_cm.amp[sx_cm.n++] = e; sx_cm.ang[sx_cm.n] = REV_TH0 - ph; sx_cm.amp[sx_cm.n++] = e; }
    sx_cm.ext = 6.0f;   // px (x k) outward at a crest's top
}
float crest_at(float th) {   // the strongest crest here (the MAX, never the sum : two lobes over the contact would burn white)
    float L = 0.0f;
    for (int i = 0; i < sx_cm.n; ++i) { float d = th - sx_cm.ang[i]; d -= TAU * floorf((d + PI) / TAU); const float v = sx_cm.amp[i] * expf(-(d * d) / (sx_cm.sig * sx_cm.sig)); if (v > L) L = v; }
    return L;
}
void noise_ring(u32 dev, float CX, float CY, float R, float k, float t, const RingFx& f) {
    if (!sx_noise) return;
    const int N = 128;
    static float tc[N + 1], ts[N + 1]; static bool tok = false;
    if (!tok) { for (int i = 0; i <= N; ++i) { tc[i] = cosf(i * TAU / N); ts[i] = sinf(i * TAU / N); } tok = true; }
    // the second stage reads an INTEGER number of tiles round the ring (uRep x 0.67 rounded) : 7 x 0.67 = 4.69 broke the
    // pattern at 3 o'clock -- the seam on the right (2026-11-23)
    for (int i = 0; i <= N; ++i) {
        const bool full = sx_nrSpan >= TAU - 0.001f;
        const float th = full ? i * TAU / N : sx_nrA0 + sx_nrSpan * i / N, c = full ? tc[i] : cosf(th), s = full ? ts[i] : sinf(th), ua = th / TAU;
        const float wv = f.wave * (sinf(th * f.wfreq - t * f.wspd) * 0.6f + sinf(th * (f.wfreq * 1.8f + 1.0f) + t * f.wspd * 0.7f) * 0.4f);
        const float bp = (f.bumps || f.patch > 1.5f) ? bumpf(th) : 0.0f, bb = f.bumps ? bp : 0.0f;
        float rr[3] = { R + f.r0 * k, R + (f.r1 + 0.5f * wv) * k + 0.5f * bb, R + (f.r2 + wv) * k + bb };
        float pm = 1.0f, cg = 1.0f, ct = 0.0f;
        if (sx_cm.on) { const float L = crest_at(th); cg = sx_cm.base + (1.0f - sx_cm.base) * L; ct = L; rr[1] += 0.5f * sx_cm.ext * L * k; rr[2] += sx_cm.ext * L * k; }
        if (f.patch > 2.5f) { const float q = bp / sx_bMax; pm = 1.0f - 0.92f * (q < 0.0f ? 0.0f : (q > 1.0f ? 1.0f : q)); }
        else if (f.patch > 1.5f) { pm = bp / sx_bMax; pm = pm < 0.0f ? 0.0f : (pm > 1.0f ? 1.0f : pm); }
        else if (f.patch > 0.0f) { const float m = sinf(th * 3.0f + t * 1.7f) * 0.6f + sinf(th * 5.0f - t * 2.3f + 1.3f) * 0.4f + sinf(th * 2.0f + t * 0.9f) * 0.3f;
            pm = (m - 0.05f) * 2.2f; pm = pm < 0.0f ? 0.0f : (pm > 1.0f ? 1.0f : pm); pm = pm * pm * (3.0f - 2.0f * pm); }
        const u32 cc[3] = { C(ct > 0.0f ? lerp(f.c0, sx_cm.tint, 0.6f * ct) : f.c0, f.a0 * pm * cg), C(ct > 0.0f ? lerp(f.c1, sx_cm.tint, 0.6f * ct) : f.c1, f.a1 * pm * cg), C(f.c2, 0.0f) };
        V2 v[3];
        for (int j = 0; j < 3; ++j) {
            const float rad = (rr[j] - R) / R;
            v[j].x = CX + c * rr[j] - 0.5f; v[j].y = CY + s * rr[j] - 0.5f; v[j].z = 0; v[j].rhw = 1; v[j].c = cc[j];
            v[j].u0 = ua * f.uRep + t * f.uSpd + rad * f.twist; v[j].v0 = rad * 1.6f * sx_vScale - t * f.vSpd;
            v[j].u1 = ua * floorf(f.uRep * 0.67f + 0.5f) - t * f.uSpd * 0.6f - rad * f.twist * 0.5f; v[j].v1 = rad * 2.3f * sx_vScale + (f.inward ? -t * f.vSpd * 0.6f : t * f.vSpd * 0.6f);
        }
        sx_m[0][2 * i] = v[0]; sx_m[0][2 * i + 1] = v[1]; sx_m[1][2 * i] = v[1]; sx_m[1][2 * i + 1] = v[2];
    }
    if (f.dark) for (int i = 0; i < 2 * (N + 1); ++i) { sx_m[0][i].c = shade(sx_m[0][i].c); sx_m[1][i].c = shade(sx_m[1][i].c); }
    fx_state(dev, sx_ringTex ? sx_ringTex : sx_noise, true, !f.dark);
    fdraw(dev, D3DPT_TRIANGLESTRIP, 2 * (N + 1) - 2, sx_m[0], sizeof(V2));
    fdraw(dev, D3DPT_TRIANGLESTRIP, 2 * (N + 1) - 2, sx_m[1], sizeof(V2));
}
// the blast's shockwave : a noise band torn outward, ragged edge (u = 0..1 progress)
void shockwave(u32 dev, float CX, float CY, float R, float k, float u, float t, u32 inner, u32 outer, float reach, bool dark = false) {
    if (!sx_noise) return;
    const int N = 96; const float e = (1.0f - u) * (1.0f - u);
    static float tc[N + 1], ts[N + 1]; static bool tok = false;
    if (!tok) { for (int i = 0; i <= N; ++i) { tc[i] = cosf(i * TAU / N); ts[i] = sinf(i * TAU / N); } tok = true; }
    const float rm = R + u * R * reach, w = (14.0f * (1.0f - u) + 3.0f) * k;
    for (int i = 0; i <= N; ++i) {
        const float th = i * TAU / N, c = tc[i], s = ts[i];
        const float rag = (4.0f * sinf(th * 9.0f + t * 7.0f) + 3.0f * sinf(th * 23.0f - t * 11.0f)) * k * u;
        const float ri = rm - w * 0.6f, ro = rm + w * 0.4f + rag;
        V2& a = sx_m[0][2 * i]; V2& b = sx_m[0][2 * i + 1];
        a.x = CX + c * ri - 0.5f; a.y = CY + s * ri - 0.5f; b.x = CX + c * ro - 0.5f; b.y = CY + s * ro - 0.5f;
        a.z = b.z = 0; a.rhw = b.rhw = 1; a.c = C(inner, 0.0f); b.c = C(outer, 0.95f * e);
        a.u0 = b.u0 = (float)i / N * 5.0f; a.v0 = u; b.v0 = 0.5f + u; a.u1 = b.u1 = (float)i / N * 3.0f + 0.3f; a.v1 = 0.2f - u; b.v1 = 0.9f - u;
    }
    if (dark) for (int i = 0; i < 2 * (N + 1); ++i) sx_m[0][i].c = shade(sx_m[0][i].c);
    fx_state(dev, sx_noise, true, !dark);
    fdraw(dev, D3DPT_TRIANGLESTRIP, 2 * (N + 1) - 2, sx_m[0], sizeof(V2));
}
// A RIPPLE MADE OF WATER (Reverberation F) -- not pulse_ring's perfect uniform circle : its radius wobbles round the
// ring, its width varies, it breaks into gaps, a light leading edge with a deeper blue trail behind it, flecks of foam
// on its strongest parts. One additive strip (4 rows), a few blooms.
void water_ripple(u32 dev, float CX, float CY, float r, float w, float k, u32 rgb, float a, float seed, float t) {
    if (a <= 0.004f || w <= 0.1f) return;
    // MADE OF THE CROWN'S WATER (2026-11-23 : "the water round it should be part of the wave, the wave the colour of the
    // water") : the ripple carries the same caustics as the crown, at the same cell size, in water BLUE -- a ring of the
    // sphere's water travelling out, not a pale line. (No caustics yet : plain colour through the soft sprite's white centre.)
    const int SEG = 72; static Vtx v[3][2 * (SEG + 1)];
    auto sm2 = [](float x) { x = x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); return x * x * (3.0f - 2.0f * x); };
    const u32 light = lerp(rgb, 0xFFFFFF, sx_rippleJag ? 0.55f : 0.25f), deep = sx_rippleJag ? lerp(rgb, 0x2A0A6A, 0.4f) : lerp(rgb, 0x0A3A9A, 0.50f);
    const u32 rtex = sx_rippleTex ? sx_rippleTex : sx_caus;
    const u32 tex = rtex ? rtex : sx_soft; const bool tx = rtex != 0;
    for (int i = 0; i <= SEG; ++i) {
        const float th = i * TAU / SEG, c = cosf(th), s2 = sinf(th);
        float dr = w * 1.2f * (0.6f * sinf(3.0f * th + seed + 0.7f * t) + 0.4f * sinf(5.0f * th - 2.0f * seed - t) + 0.25f * sinf(11.0f * th + 3.0f * seed));
        const float wv = w * (0.65f + 0.35f * sinf(4.0f * th + seed * 1.9f + 0.5f * t));
        float pt = sm2(0.45f + 0.45f * sinf(2.0f * th + seed) + 0.35f * sinf(7.0f * th - 1.3f * seed + 0.5f * t));
        if (sx_rippleJag) {   // ELECTRIC : a jagged outline racing round, sharp gaps, the light flickering
            dr += w * (0.9f * sinf(23.0f * th + 40.0f * t + seed) + 0.6f * sinf(37.0f * th - 55.0f * t + 2.0f * seed));
            pt = sm2(pt * 1.9f - 0.55f) * (0.7f + 0.3f * sinf(60.0f * t + 9.0f * th + seed));
        }
        const float rr[4] = { r + dr - 2.2f * wv, r + dr - 0.9f * wv, r + dr, r + dr + 0.6f * wv };
        const float al = tx ? 1.6f : 1.0f;   // the caustics' dark cells take light away : a little more to carry the same water
        const u32 col[4] = { C(deep, 0.0f), C(deep, (0.45f * a * pt * al > 1.0f ? 1.0f : 0.45f * a * pt * al)), C(light, (a * pt * al > 1.0f ? 1.0f : a * pt * al)), C(light, 0.0f) };
        for (int j = 0; j < 3; ++j) for (int e = 0; e < 2; ++e) {
            Vtx& q = v[j][2 * i + e]; const float R2 = rr[j + e];
            q.x = CX + c * R2 - 0.5f; q.y = CY + s2 * R2 - 0.5f; q.z = 0.0f; q.rhw = 1.0f; q.color = col[j + e];
            q.u = tx ? (float)i / SEG * 4.0f - 0.03f * t : 0.5f; q.v = tx ? R2 / (61.0f * k) + 0.04f * t : 0.5f;   // the crown's cell size (4 tiles round, ~61 px across)
        }
    }
    {   // its AURA : a wide soft blue glow under the ripple (the water's colour round the wave, not only its lines)
        static Vtx g[2][2 * (SEG + 1)];
        for (int i = 0; i <= SEG; ++i) {
            const float th = i * TAU / SEG, c = cosf(th), s2 = sinf(th);
            const float dr = w * 1.2f * (0.6f * sinf(3.0f * th + seed + 0.7f * t) + 0.4f * sinf(5.0f * th - 2.0f * seed - t) + 0.25f * sinf(11.0f * th + 3.0f * seed));
            const float pt = 0.45f + 0.55f * sm2(0.45f + 0.45f * sinf(2.0f * th + seed) + 0.35f * sinf(7.0f * th - 1.3f * seed + 0.5f * t));
            const float rr[3] = { r + dr - 4.5f * w, r + dr, r + dr + 2.5f * w };
            const u32 col[3] = { C(rgb, 0.0f), C(rgb, 0.40f * a * pt), C(rgb, 0.0f) };
            for (int j = 0; j < 2; ++j) for (int e = 0; e < 2; ++e) {
                Vtx& q = g[j][2 * i + e]; const float R2 = rr[j + e];
                q.x = CX + c * R2 - 0.5f; q.y = CY + s2 * R2 - 0.5f; q.z = 0.0f; q.rhw = 1.0f; q.color = col[j + e]; q.u = 0.5f; q.v = 0.5f;
            }
        }
        fx_state(dev, sx_soft, false, true);
        for (int j = 0; j < 2; ++j) fdraw(dev, D3DPT_TRIANGLESTRIP, 2 * SEG, g[j], sizeof(Vtx));
    }
    fx_state(dev, tex, false, true);
    for (int j = 0; j < 3; ++j) fdraw(dev, D3DPT_TRIANGLESTRIP, 2 * SEG, v[j], sizeof(Vtx));
    cqs(dev);
    for (int q = 0; q < 7; ++q) {   // flecks of foam where it is strongest
        const float th = q * TAU / 7.0f + seed * 0.37f, pt = sm2(0.45f + 0.45f * sinf(2.0f * th + seed) + 0.35f * sinf(7.0f * th - 1.3f * seed + 0.5f * t));
        if (pt < 0.7f) continue;
        const float dr = w * 1.2f * (0.6f * sinf(3.0f * th + seed + 0.7f * t) + 0.4f * sinf(5.0f * th - 2.0f * seed - t) + 0.25f * sinf(11.0f * th + 3.0f * seed));
        bloom(dev, CX + cosf(th) * (r + dr), CY + sinf(th) * (r + dr), 1.2f * k, C(0xBFE6FF, 0.7f * a * pt));   // blue-white, small
    }
    fx_end(dev);
}
void pulse_ring(u32 dev, float CX, float CY, float r, float w, u32 rgb, float a) {   // a thin soft ring (color state)
    cqs(dev); blend(dev, true);
    ring_sector(dev, CX, CY, r - w * 0.5f, r + w * 0.5f, 0.0f, TAU, C(rgb, a));
    blend(dev, false);
}

// ---------------------------------------------------------------- particles ----
void spawn(int m, float x, float y, float vx, float vy, float life, float s, float pa = 0, float pr = 0, float w = 0) {
    if (sx_partMul < 0.999f && rnd() >= sx_partMul) return;   // the density setting
    vx *= REACH; vy *= REACH; if (pr > sx_R) pr = sx_R + (pr - sx_R) * REACH;   // everything flies and starts less far
    Part p = {}; p.sd = rnd(); p.el = (unsigned char)sx_els[(sx_elc++) % sx_nels]; p.m = (unsigned char)m; p.x = x; p.y = y; p.vx = vx; p.vy = vy; p.life = p.l = life; p.s = s; p.pa = pa; p.pr = pr; p.w = w; add(p);
}
// GRAVITATION : chunks of EARTH torn out at the blast and held LEVITATING round a sphere of darkness -- each its own
// slot, a slow orbit, a bob, a tumble ; risen from the sphere over the first 0.9 s
const int NCHUNK = 10;
float gh(float i) { const float v = sinf(i * 12.9898f + 4.1f) * 43758.5453f; return v - floorf(v); }
void chunk_at(int i, float bt, float t, float CX, float CY, float R, float k, float& x, float& y, float& sz, float& rot) {
    const float h0 = gh(i + 0.3f), h1 = gh(i + 7.7f), h2 = gh(i + 3.1f);
    const float rise = bt / 0.9f < 0.0f ? 0.0f : (bt / 0.9f > 1.0f ? 1.0f : bt / 0.9f), e = 1.0f - (1.0f - rise) * (1.0f - rise) * (1.0f - rise);
    const float a = i * TAU / NCHUNK + (h0 - 0.5f) * 0.5f + t * 0.12f, r = R + (R * (0.25f + 0.3f * h1) + 5.0f * k) * e;   // close to the sphere : held by it
    x = CX + cosf(a) * r; y = CY + sinf(a) * r + 3.5f * k * sinf(t * 1.3f + i * 1.7f) * e;
    sz = (7.0f + 6.0f * h2) * k; rot = h0 * 6.0f + t * (h1 - 0.5f) * 0.7f + (1.0f - e) * 4.0f;
}
void emit(int P, float dt, float bt, float CX, float CY, float R, float k) {
    auto every = [&](int slot, float rate) { int n = 0; sx_acc[slot] += dt * rate * sx_pf; while (sx_acc[slot] >= 1.0f) { sx_acc[slot] -= 1.0f; ++n; } return n; };
    switch (P) {
    case SCP_Liquefaction:
        if (sx_lq == 1) {   // B : the lava is HEAVY -- drops gather and fall from the BOTTOM of the crown ; a few embers rise off it
            for (int n = every(0, 3.0f); n--; ) { const float a = PI * 0.5f + (rnd() - 0.5f) * PI * 0.8f, r0 = R + 9.0f * k; spawn(PM_DROP, CX + cosf(a) * r0, CY + sinf(a) * r0, cosf(a) * 3.0f * k, 0.0f, 1.3f + rnd() * 0.4f, (5.0f + rnd() * 2.0f) * k); }   // heavy beads : they hang, then fall (bigger : they must read at scale 1)
            for (int n = every(1, 6.0f); n--; ) {   // EMBERS (2026-11-23 : "the circling particles make no sense -- embers like a fire's, floating up") :
                const float a = rnd() * TAU, r0 = R + (4.0f + rnd() * 8.0f) * k;   // born on the burning crown, they drift UP slowly, wavering, and go out
                spawn(PM_OUT, CX + cosf(a) * r0, CY + sinf(a) * r0, (rnd() - 0.5f) * 14.0f * k, -(14.0f + rnd() * 22.0f) * k, 1.3f + rnd() * 0.9f, (1.6f + rnd() * 1.2f) * k);   // bigger (2026-11-23 : too discreet at scale 1)
            }
            break;
        }
        for (int n = every(0, 18.0f); n--; ) { const float a = rnd() * TAU, r0 = R + 7.0f * k; spawn(PM_DROP, CX + cosf(a) * r0, CY + sinf(a) * r0, cosf(a) * 16.0f * k, (sinf(a) * 10.0f - 6.0f) * k, 1.1f + rnd() * 0.5f, (2.2f + rnd() * 1.6f) * k); }
        for (int n = every(1, 30.0f); n--; ) spawn(PM_GLIDE, 0, 0, 0, 0, 0.9f + rnd() * 0.6f, (2.5f + rnd() * 2.5f) * k, rnd() * TAU, R + (2.0f + rnd() * 8.0f) * k, 1.3f + rnd() * 0.9f);
        break;
    case SCP_Impaction:
        if (bt - dt <= 0.0f) for (int i = 0; i < (int)((sx_im == 1 ? 60 : 200) * sx_pf); ++i) { const float a = rnd() * TAU, sp = (sx_im == 1 ? 50.0f + rnd() * 140.0f : 60.0f + rnd() * 320.0f) * k;   // the explosion (B : 60, kept close)
            spawn(PM_OUT, CX + cosf(a) * R, CY + sinf(a) * R, cosf(a) * sp, sinf(a) * sp, 0.4f + rnd() * 0.6f, (1.4f + rnd() * 2.2f) * k); }
        for (int n = every(0, sx_im == 1 ? 10.0f : 18.0f); n--; ) { const float a = rnd() * TAU, r = R + (2.0f + rnd() * 6.0f) * k, sp = (20.0f + rnd() * 40.0f) * k;   // then sparks crackling round it
            spawn(PM_OUT, CX + cosf(a) * r, CY + sinf(a) * r, -sinf(a) * sp * (rnd() < 0.5f ? 1.0f : -1.0f), cosf(a) * sp, 0.25f + rnd() * 0.25f, (1.2f + rnd() * 1.2f) * k); }
        break;
    case SCP_Detonation:
        if (bt - dt <= 0.0f) for (int i = 0; i < (int)(90 * sx_pf); ++i) { const float a = rnd() * TAU, sp = (90.0f + rnd() * 140.0f) * k;   // the boom
            spawn(PM_OUT, CX + cosf(a) * R, CY + sinf(a) * R, cosf(a) * sp, sinf(a) * sp, 0.4f + rnd() * 0.4f, (1.4f + rnd() * 1.6f) * k); }
        for (int n = every(0, 10.0f); n--; ) { const float a = rnd() * TAU, r = R + (6.0f + rnd() * 16.0f) * k, sp = (40.0f + rnd() * 40.0f) * k;   // then specks carried by the wind
            spawn(PM_OUT, CX + cosf(a) * r, CY + sinf(a) * r, -sinf(a) * sp, cosf(a) * sp, 0.5f + rnd() * 0.3f, (1.0f + rnd()) * k); }
        break;
    case SCP_Reverberation:
        if (sx_rv == REV_WATER || sx_rv == REV_BOUNCE) break;
        // bubbles born EVENLY all round the ring (golden-angle walk : no clump, no gap), carried OUT with the ripples and
        // along with the ring's own flow (counter-clockwise on screen, its texture's way)
        if (sx_rv == 2) {   // C : 3 DROPS thrown from the struck point, a 45 deg cone, at each strike (2.6 s)
            const float PER = 2.6f; const int n0 = bt - dt < 0.0f ? -1 : (int)((bt - dt) / PER), n1 = bt < 0.0f ? -1 : (int)(bt / PER);
            if (n1 >= 0 && n1 != n0) for (int i = 0; i < 3; ++i) { const float a = REV_TH0 + (rnd() - 0.5f) * 0.785f, sp = (28.0f + rnd() * 22.0f) * k, tg = 12.0f * k * (i == 1 ? -1.0f : 1.0f);
                spawn(PM_OUT, CX + cosf(a) * (R + 3.0f * k), CY + sinf(a) * (R + 3.0f * k), cosf(a) * sp - sinf(a) * tg, sinf(a) * sp + cosf(a) * tg, 0.18f + rnd() * 0.14f, (0.75f + 0.25f * i) * k); }
        }
        if (bt - dt <= 0.0f && rev_orig()) { const bool f2 = sx_rv == REV_A2; const int N = (int)((f2 ? 30 : 90) * sx_pf);   // A : the splash, a full crown of bubbles (F : 30 -- 90 buried the water)
            for (int i = 0; i < N; ++i) { const float a = (i + rnd() * 0.6f) * TAU / N, sp = (70.0f + rnd() * 90.0f) * k, tg = 25.0f * k;
                spawn(PM_OUT, CX + cosf(a) * R, CY + sinf(a) * R, cosf(a) * sp + sinf(a) * tg, sinf(a) * sp - cosf(a) * tg, 1.0f + rnd() * 0.6f, (f2 ? 1.2f + rnd() * 1.0f : 1.6f + rnd() * 1.4f) * k); } }
        { static float ga = 0.0f;
          for (int n = every(0, sx_rv == REV_A2 ? 4.0f : (rev_orig() ? 12.0f : 1.5f)); n--; ) { ga = fmodf(ga + 2.39996f, TAU); const float a = ga, r = R + 4.0f * k, sp = (18.0f + rnd() * 16.0f) * k, tg = 14.0f * k;   // then a steady stream of them (B / C : 1-2 a second)
              spawn(PM_OUT, CX + cosf(a) * r, CY + sinf(a) * r, cosf(a) * sp + sinf(a) * tg, sinf(a) * sp - cosf(a) * tg, 1.3f + rnd() * 0.6f, (sx_rv == REV_A2 ? 1.0f + rnd() * 0.8f : 1.3f + rnd() * 1.0f) * k); } }
        break;
    case SCP_Induration: case SCP_Transfixion: case SCP_Light: case SCP_Radiance:
        for (int n = every(0, P == SCP_Induration ? 14.0f : 26.0f); n--; ) { const float a = rnd() * TAU, r = R + (4.0f + rnd() * R * 0.5f / k) * k, sp = (8.0f + rnd() * 22.0f) * k;
            spawn(PM_OUT, CX + cosf(a) * r, CY + sinf(a) * r, cosf(a) * sp, sinf(a) * sp, 0.8f + rnd() * 0.7f, (1.4f + rnd() * 1.6f) * k); }
        break;
    case SCP_Fusion:           // embers and sparks of light thrown off the burning sphere
        for (int n = every(0, 24.0f); n--; ) { const float a = rnd() * TAU, r = R + 6.0f * k, sp = (20.0f + rnd() * 30.0f) * k;
            spawn(PM_EMBER, CX + cosf(a) * r, CY + sinf(a) * r, cosf(a) * sp, sinf(a) * sp - 10.0f * k, 0.9f + rnd() * 0.6f, (1.6f + rnd() * 1.4f) * k); }
        break;
    case SCP_Distortion:       // water and frost drawn into the whirlpool
        for (int n = every(0, 34.0f); n--; ) spawn(PM_SWIRL, 0, 0, 0, 0, 1.4f, (1.6f + rnd() * 1.6f) * k, rnd() * TAU, R * (2.0f + rnd() * 0.4f), -1.1f);
        break;
    case SCP_Compression:      // matter swallowed : streams of shadow drawn straight into the ring
        for (int n = every(0, 40.0f); n--; ) spawn(PM_SWIRL, 0, 0, 0, 0, 1.6f, (1.4f + rnd() * 1.6f) * k, rnd() * TAU, R * (2.0f + rnd() * 0.5f), 0.0f);   // straight IN
        break;
    case SCP_Darkness: case SCP_Umbra:   // motes of darkness rising off the crown
        for (int n = every(0, 22.0f); n--; ) { const float a = rnd() * TAU, r = R + (6.0f + rnd() * 10.0f) * k, sp = (12.0f + rnd() * 20.0f) * k;
            spawn(PM_OUT, CX + cosf(a) * r, CY + sinf(a) * r, cosf(a) * sp, sinf(a) * sp, 0.9f + rnd() * 0.6f, (1.6f + rnd() * 1.6f) * k); }
        break;
    case SCP_Gravitation:
        for (int n = every(0, 7.0f); n--; ) { float x, y, sz, rot; chunk_at((int)(rnd() * NCHUNK) % NCHUNK, bt, sx_clock, CX, CY, R, k, x, y, sz, rot);   // crumbs falling off a chunk
            spawn(PM_DROP, x + (rnd() - 0.5f) * sz, y + sz * 0.5f, (rnd() - 0.5f) * 8.0f * k, 6.0f * k, 0.7f + rnd() * 0.4f, (0.9f + rnd() * 0.8f) * k); }
        for (int n = every(1, 9.0f); n--; ) { const float a = rnd() * TAU, r = R + 5.0f * k, sp = (10.0f + rnd() * 14.0f) * k;   // darkness seeping off the sphere
            spawn(PM_OUT, CX + cosf(a) * r, CY + sinf(a) * r, cosf(a) * sp, sinf(a) * sp, 0.9f + rnd() * 0.5f, (1.6f + rnd() * 1.4f) * k); }
        break;
    case SCP_Fragmentation:    // the storm's own grit and sparks, carried round
        for (int n = every(0, 12.0f); n--; ) { const float a = rnd() * TAU, r = R + (6.0f + rnd() * 14.0f) * k, sp = (40.0f + rnd() * 40.0f) * k;
            spawn(PM_OUT, CX + cosf(a) * r, CY + sinf(a) * r, -sinf(a) * sp, cosf(a) * sp, 0.5f + rnd() * 0.3f, (1.2f + rnd()) * k); }
        break;
    default: break;
    }
}
void step_parts(float dt, float CX, float CY, float R, float k, bool over) {
    const float drag = expf(-1.8f * dt);
    int o = 0;
    for (int i = 0; i < sx_np; ++i) {
        Part& p = sx_p[i];
        switch (p.m) {
        case PM_GLIDE: { p.pa += p.w * dt; const float r = p.pr + 2.0f * k * sinf(p.pa * 5.0f - sx_clock * 4.0f); p.x = CX + cosf(p.pa) * r; p.y = CY + sinf(p.pa) * r; } break;
        case PM_SWIRL: p.pr -= dt * R * 0.9f; p.pa += dt * p.w * (2.2f + 18.0f * k / (p.pr > 6.0f * k ? p.pr : 6.0f * k));
                       if (p.w != 0.0f) { p.vx = -sinf(p.pa) * p.w * p.pr; p.vy = cosf(p.pa) * p.w * p.pr; } else { p.vx = -cosf(p.pa) * R; p.vy = -sinf(p.pa) * R; }   // its motion, for the tail
                       p.x = CX + cosf(p.pa) * p.pr; p.y = CY + sinf(p.pa) * p.pr; if (p.pr < R + 2.0f * k) p.l = 0.0f; break;
        case PM_IN:    p.vx = (CX - p.x) * 3.2f; p.vy = (CY - p.y) * 3.2f; p.x += p.vx * dt; p.y += p.vy * dt;
                       if ((p.x - CX) * (p.x - CX) + (p.y - CY) * (p.y - CY) < (R + 3.0f * k) * (R + 3.0f * k)) p.l = 0.0f; break;
        case PM_DROP: case PM_ROCK: p.x += p.vx * dt; p.y += p.vy * dt; p.vy += (p.m == PM_ROCK ? 70.0f : 95.0f) * k * dt; break;
        case PM_OUT:   p.x += p.vx * dt; p.y += p.vy * dt; p.vx *= drag; p.vy *= drag; break;
        case PM_PULL: {   // a rock thrown out, then dragged back in by the gravity until it hits the ring
            const float dx = p.x - CX, dy = p.y - CY, d = sqrtf(dx * dx + dy * dy) + 0.01f;
            p.vx -= dx / d * 900.0f * k * dt; p.vy -= dy / d * 900.0f * k * dt; p.x += p.vx * dt; p.y += p.vy * dt;
            if (d < R + 5.0f * k && (p.vx * dx + p.vy * dy) < 0.0f) p.l = 0.0f; } break;
        case PM_ORBIT:    // debris circling the sphere, spiralling down into its ring
            p.pa += p.w * dt; p.pr -= R * 0.07f * dt; p.x = CX + cosf(p.pa) * p.pr; p.y = CY + sinf(p.pa) * p.pr;
            if (p.pr < R + 5.0f * k) p.l = 0.0f; break;
        default:       p.x += p.vx * dt; p.y += p.vy * dt; p.vx *= drag; p.vy = p.vy * drag + 30.0f * k * dt; break;
        }
        if (p.m == PM_OUT || p.m == PM_EMBER || p.m == PM_DROP || p.m == PM_IN) switch (p.el) {   // each element moves like itself
            case 4: if (p.m != PM_IN) {   // a BUBBLE : water drag kills the throw fast, then it drifts slowly OUT from the sphere
                        // (the way the ripples go), zig-zagging across its path
                        const float bx = p.x - CX, by = p.y - CY, bd = sqrtf(bx * bx + by * by) + 0.01f, ux = bx / bd, uy = by / bd;
                        const float wd = expf(-4.0f * dt), wob = sinf(sx_clock * 5.0f + p.sd * 20.0f) * 16.0f * k * dt;
                        p.vx = p.vx * wd + ux * 70.0f * k * dt; p.vy = p.vy * wd + uy * 70.0f * k * dt;
                        p.x += -uy * wob; p.y += ux * wob; }
                    break;
            case 5: p.vx *= 1.0f - 1.5f * dt; p.vy = p.vy * (1.0f - 1.5f * dt) + 14.0f * k * dt; p.x += sinf(sx_clock * 2.0f + p.sd * 10.0f) * 12.0f * k * dt; break;   // a flake drifts down
            case 7: p.vy -= 16.0f * k * dt; break;                                                             // smoke rises
            case 0: if (p.m != PM_DROP) p.vy -= 12.0f * k * dt; break;                                         // an ember lifts
            case 2: { const float ca = cosf(2.4f * dt), sa = sinf(2.4f * dt), vx = p.vx; p.vx = vx * ca - p.vy * sa; p.vy = vx * sa + p.vy * ca; } break;   // a wisp curls
            case 3: p.vy += 30.0f * k * dt; break;                                                             // dust settles
            default: break;
        }
        p.l -= dt; if (over) p.l -= dt * 3.0f;
        const float dx = p.x - CX, dy = p.y - CY;
        const float d2 = dx * dx + dy * dy;
        const float rf = R * (1.0f + 1.4f * REACH), rl = R * (1.0f + 3.0f * REACH);
        if (d2 > rf * rf && p.l > 0.0f) { const float past = (sqrtf(d2) - rf) / (R * 1.2f * REACH); p.l -= dt * p.life * 2.5f * (past < 1.0f ? past : 1.0f); }   // fading out toward the edge
        if (p.l > 0.0f && d2 < rl * rl) sx_p[o++] = p;
    }
    sx_np = o;
}
void draw_parts(u32 dev, int P, float k) {
    if (!sx_np || water_variant(P)) return;
    const bool dark = pal(P).dark;
    int n = 0;
    for (int i = 0; i < sx_np; ++i) {
        const Part& p = sx_p[i]; if (p.m == PM_ROCK || p.m == PM_PULL || p.m == PM_ORBIT) continue;   // rocks : their own pass
        const float h = p.l / p.life, fade = h < 0.25f ? h * 4.0f : 1.0f;
        const float sp = sqrtf(p.vx * p.vx + p.vy * p.vy), ax = sp > 0.05f ? p.vx / sp : 1.0f, ay = sp > 0.05f ? p.vy / sp : 0.0f;
        const int el = p.el & 7; const float u0 = el / 8.0f, u1 = (el + 1) / 8.0f, uc = (el + 0.5f) / 8.0f;
        u32 rgb; float sz;
        switch (el) {
        case 0:  rgb = lq_new(P) ? heat(0.38f + 0.37f * h) : heat(0.35f + 0.65f * h); sz = 1.1f; break;   // ember (Liquefaction B : glowing orange, never white-hot)
        case 1:  rgb = lerp(0xC8B0FF, 0xFFFFFF, h);                     sz = 1.5f; break;   // spark
        case 2:  rgb = 0xC8FFD8;                                        sz = 2.0f; break;   // wisp
        case 3:  rgb = 0xC8A070;                                        sz = 1.0f; break;   // dust
        case 4:  rgb = (P == SCP_Reverberation && sx_rv == REV_A2) ? 0x4FA8FF : 0xB8E0FF; sz = 1.4f; break;   // bubble (Reverberation F : water blue)
        case 5:  rgb = 0xF0FCFF;                                        sz = 1.7f; break;   // flake
        case 6:  rgb = lerp(0xFFE7A0, 0xFFFFFF, h);                     sz = 1.8f; break;   // sparkle
        default: rgb = lerp(pal(P).mid, pal(P).hot, h);                 sz = 2.1f; break;   // smoke (the chain's own dark hue)
        }
        float hs = p.s * sz, al = (el == 7 ? 0.6f : 0.95f) * fade;
        if (el == 1 && im_new(P)) {   // Impaction B : an electric SPARK -- a hot point with a short streak, violet-white, no star
            const float sl = (sp * 0.05f < 9.0f * k ? sp * 0.05f : 9.0f * k) + hs;
            n = tail(n, p.x, p.y, ax, ay, sl, hs * 0.35f, C(lerp(0xB98CFF, 0xFFFFFF, h), al), 0.5f / 8.0f);
            n = put(n, p.x, p.y, 1.0f, 0.0f, hs * 0.6f, hs * 0.6f, C(0xF4ECFF, al), sx_atlas ? 0.0f : 0.0f, sx_atlas ? 1.0f / 8.0f : 1.0f);
            continue;
        }
        if (el == 4) {   // a bubble swells a little as it rises, then POPS : its rim flies open and vanishes in a blink
            hs *= 0.8f + 0.35f * (1.0f - h);
            if (h < 0.12f) { const float q = 1.0f - h / 0.12f; hs *= 1.0f + 0.7f * q; al = 0.95f * (1.0f - q) * (1.0f - q); } else al = 0.95f;
        }
        const u32 col = C(rgb, al);
        float rx = 1.0f, ry = 0.0f;                                                              // orientation : flakes and sparkles turn, wisps follow their path
        if (el == 5 || el == 6) { const float r = p.sd * 6.0f + sx_clock * (p.sd - 0.5f) * 2.0f; rx = cosf(r); ry = sinf(r); }
        else if (el == 2) { rx = ax; ry = ay; }
        const float len = sp * 0.06f < 12.0f * k ? sp * 0.06f : 12.0f * k;
        if ((el == 0 || el == 1 || el == 3) && len > p.s * 0.8f) n = tail(n, p.x, p.y, ax, ay, len + hs * 0.5f, hs * 0.35f, col, uc);   // a motion trail for the fast ones
        n = put(n, p.x, p.y, rx, ry, hs, hs, col, sx_atlas ? u0 : 0.0f, sx_atlas ? u1 : 1.0f);
    }
    flush(dev, n, sx_atlas ? sx_atlas : sx_soft, !dark);   // ONE pass : a 2nd one through the soft sprite drew a 1/8 slice of it stretched over every particle (the deformed bubbles)
    if (dark) {   // a shadow is not lit : a faint rim of their own colour so they still read
        n = 0;
        for (int i = 0; i < sx_np; ++i) { const Part& p = sx_p[i]; if (p.m == PM_ROCK || p.m == PM_PULL || p.m == PM_ORBIT) continue; n = put(n, p.x, p.y, 1.0f, 0.0f, p.s * 0.6f, p.s * 0.6f, C(pal(P).hot, 0.5f * p.l / p.life)); }
        flush(dev, n, sx_soft, true);
    }
    // ROCKS : irregular stones, a dark body and a lit face, a purple aura behind them
    n = 0;
    for (int i = 0; i < sx_np; ++i) { const Part& p = sx_p[i]; if (p.m != PM_ROCK && p.m != PM_PULL && p.m != PM_ORBIT) continue; const float h = p.l / p.life; n = put(n, p.x, p.y, 1.0f, 0.0f, p.s * 2.6f, p.s * 2.6f, C(0xA050E0, 0.55f * (h < 0.2f ? h * 5.0f : 1.0f))); }
    flush(dev, n, sx_soft, true);
    cqs(dev);
    for (int i = 0; i < sx_np; ++i) {
        const Part& p = sx_p[i]; if (p.m != PM_ROCK && p.m != PM_PULL && p.m != PM_ORBIT) continue;
        const float h = p.l / p.life, fade = h < 0.2f ? h * 5.0f : 1.0f, rot = p.sd * 6.0f + sx_clock * (p.sd - 0.5f) * 3.0f;
        float pts[12], hi[12];
        for (int j = 0; j < 6; ++j) { const float a = rot + j * TAU / 6, rr = p.s * (0.75f + 0.45f * fabsf(sinf(p.sd * 37.0f + j * 2.3f)));
            pts[2 * j] = p.x + cosf(a) * rr; pts[2 * j + 1] = p.y + sinf(a) * rr; hi[2 * j] = p.x - 0.18f * p.s + cosf(a) * rr * 0.55f; hi[2 * j + 1] = p.y - 0.18f * p.s + sinf(a) * rr * 0.55f; }
        fill_poly_aa(dev, pts, 6, C(0x3A2616, 0.95f * fade));
        fill_poly_aa(dev, hi, 6, C(0x8A6644, 0.8f * fade));
    }
}

// an arc of electricity hugging the sphere : from angle a0 over `span`, at radius r ; every point pushed in / out by
// a sum of sines whose phases run with time -> a jagged line that writhes continuously (pinned at both ends)
void live_arc(u32 dev, float CX, float CY, float a0, float span, float r, float amp, float t, float seed, float w, u32 glow, float al) {
    float xs[33], ys[33];
    for (int i = 0; i < 33; ++i) {
        const float u = i / 32.0f, pin = sinf(PI * u);
        const float d = 0.50f * sinf(i * 0.85f + t * 6.0f + seed) + 0.30f * sinf(i * 2.30f - t * 9.5f + seed * 1.7f)
                      + 0.20f * sinf(i * 5.70f + t * 15.0f + seed * 2.9f);   // three octaves : the big bends, the zigzag, the crackle
        const float a = a0 + span * u, rr = r + amp * d * pin;
        xs[i] = CX + cosf(a) * rr; ys[i] = CY + sinf(a) * rr;
    }
    lightning_draw(dev, xs, ys, 33, w, glow, al);
}
// a bolt reaching OUT of the sphere along angle a, length L, writhing the same way
void live_bolt(u32 dev, float CX, float CY, float a, float r0, float L, float t, float seed, float w, u32 glow, float al) {
    float xs[33], ys[33];
    const float nx = -sinf(a), ny = cosf(a);
    for (int i = 0; i < 33; ++i) {
        const float u = i / 32.0f, side = L * 0.18f * (0.5f * sinf(i * 0.9f + t * 5.0f + seed) + 0.3f * sinf(i * 2.6f - t * 9.0f + seed * 1.3f) + 0.2f * sinf(i * 6.1f + t * 14.0f + seed * 2.1f)) * u;
        const float r = r0 + L * u;
        xs[i] = CX + cosf(a) * r + nx * side; ys[i] = CY + sinf(a) * r + ny * side;
    }
    lightning_draw(dev, xs, ys, 33, w, glow, al);
}

// ---- credible lightning (Impaction) ----
void md33(float* d, float rough) {   // 33 fractal offsets, ends 0 (midpoint displacement)
    for (int i = 0; i < 33; ++i) d[i] = 0.0f;
    float amp = 1.0f;
    for (int st = 16; st >= 1; st >>= 1) { for (int i = st; i < 32; i += 2 * st) d[i] = (d[i - st] + d[i + st]) * 0.5f + (rnd() - 0.5f) * 2.0f * amp; amp *= rough; }
}
void bolt_stroke(u32 dev, const float* xs, const float* ys, float w, float al, u32 glow = 0x965AFF) {   // wide faint halo, glow, thin white core
    float hw[33]; u32 c[33];
    const struct { float wm, am; u32 col; } L[3] = { { 4.0f, 0.16f, glow }, { 2.0f, 0.42f, lerp(glow, 0xFFFFFF, 0.35f) }, { 0.7f, 1.0f, 0xFFFAFF } };
    for (int l = 0; l < 3; ++l) { for (int i = 0; i < 33; ++i) { hw[i] = w * L[l].wm; c[i] = C(L[l].col, L[l].am * al); } ribbon(dev, xs, ys, 33, hw, c); }
}
struct Bolt { float born, life, a0, span, r, amp, len, w; unsigned seed; u32 glow; unsigned char kind; };   // kind 0 = wraps the sphere, 1 = straight out
Bolt sx_b[40]; int sx_nb = 0; float sx_bAcc = 0.0f, sx_bLast = -1.0f;
void bolt_add(float t, unsigned char kind, float a0, float span, float r, float amp, float len, float w, float life, u32 glow = 0x965AFF) {
    if (sx_nb >= 40) return;
    Bolt b; b.born = t; b.life = life; b.kind = kind; b.glow = glow; b.a0 = a0; b.span = span; b.r = r; b.amp = amp; b.len = len * REACH; b.w = w;
    b.seed = ((unsigned)(rnd() * 65535.0f) << 16) ^ (unsigned)(rnd() * 65535.0f) ^ 0x5bd1e995u; sx_b[sx_nb++] = b;
}
void bolts_draw(u32 dev, float CX, float CY, float t) {
    const unsigned keep = sx_rng; bat_begin();
    int o = 0;
    for (int i = 0; i < sx_nb; ++i) {
        const Bolt& b = sx_b[i]; const float age = (t - b.born) / b.life;
        if (age < 0.0f || age >= 1.0f) continue;
        sx_b[o++] = b;
        sx_rng = b.seed * 7u + (unsigned)((t - b.born) / 0.03f);                           // a re-strike every ~30 ms
        const float lvl = 0.45f + 0.55f * rnd(), tail = age > 0.6f ? 1.0f - (age - 0.6f) / 0.4f : 1.0f, al = (age < 0.2f ? 1.0f : lvl) * tail * (b.kind == 2 ? sx_rise : 1.0f);   // the ring's own strands rise with the loop
        float xs[33], ys[33], d[33];
        sx_rng = b.seed; md33(d, 0.58f);                                                     // the SAME channel every frame of its life
        for (int j = 0; j < 33; ++j) {
            const float u = j / 32.0f;
            if (b.kind != 1) { const float a = b.a0 + b.span * u, r = b.r + d[j] * b.amp; xs[j] = CX + cosf(a) * r; ys[j] = CY + sinf(a) * r; }
            else { const float r = b.r + b.len * u, off = d[j] * b.len * 0.16f; xs[j] = CX + cosf(b.a0) * r - sinf(b.a0) * off; ys[j] = CY + sinf(b.a0) * r + cosf(b.a0) * off; }
        }
        bolt_stroke(dev, xs, ys, b.w, al, b.glow);
        sx_rng = b.seed * 13u; const int nf = b.kind == 2 ? 0 : (int)(rnd() * 3.0f);   // no forks off the ring itself (they would reach into the text)                          // forks : forward, thinner
        for (int f = 0; f < nf; ++f) {
            const int j = 6 + (int)(rnd() * 18.0f); const float ang = atan2f(ys[j + 2] - ys[j], xs[j + 2] - xs[j]) + (rnd() < 0.5f ? -1.0f : 1.0f) * (0.35f + rnd() * 0.5f);
            const float L = sqrtf((xs[32] - xs[0]) * (xs[32] - xs[0]) + (ys[32] - ys[0]) * (ys[32] - ys[0])) * (0.25f + rnd() * 0.25f);
            const unsigned next = sx_rng; sx_rng = b.seed * 31u + f; float fd[33], fx[33], fy[33]; md33(fd, 0.58f);
            for (int q = 0; q < 33; ++q) { const float u = q / 32.0f, off = fd[q] * L * 0.18f; fx[q] = xs[j] + cosf(ang) * L * u - sinf(ang) * off; fy[q] = ys[j] + sinf(ang) * L * u + cosf(ang) * off; }
            bolt_stroke(dev, fx, fy, b.w * 0.55f, al * 0.8f, b.glow); sx_rng = next;
        }
    }
    sx_nb = o; sx_rng = keep; bat_end(dev);
}

// ---- Scission : KATANA cuts, manga style ----
// slash i : its time and its angle -- two crossing cuts to open (an X), then one every 1.1 s, alternating
float slash_time(int i) { return i == 0 ? 0.0f : (i == 1 ? 0.22f : 1.2f + (i - 2) * 1.1f); }
float slash_angle(int i) { const float base = (i & 1) ? 0.62f : -0.50f; return base + 0.18f * sinf(i * 2.7f); }
// one cut : a thin crescent of white light swept across the circle in 70 ms (tapered to points), a gold glow, the
// two lips of the cut opening apart, then it fades and thins
// SCISSION : a SOLID SPHERE OF ROCK -- a thick stone shell (dark body, a lit rim, its grain) ; each katana cut SPLITS it :
// the two halves, either side of the blade's line, slide apart along it and settle back together
void rock_half(u32 dev, float cx, float cy, float R, float k, float a0, float span, float A) {   // one piece of the stone shell
    cqs(dev);
    ring_sector(dev, cx, cy, R - 7.0f * k, R + 7.0f * k, a0, a0 + span, C(0x2E1C10, 0.97f));                 // the stone's body
    ring_sector(dev, cx, cy, R + 5.2f * k, R + 7.0f * k, a0, a0 + span, C(0x8A6440, 0.9f));                  // its lit outer rim
    RingFx f = { -6, 0, 7, 0.5f, 5, 0.0f, 0xE8C890, 0x8A5A30, 0x3A2414, 0.5f * A, 0.45f * A, 6, 0.0f, 0.0f, 0.0f, false };
    sx_nrA0 = a0; sx_nrSpan = span; noise_ring(dev, cx, cy, R, k, 3.0f, f); sx_nrA0 = 0.0f; sx_nrSpan = TAU;  // its grain, still
}
// the shell keeps EVERY cut : each line through the centre splits it ; the pieces between the lines stay apart by a
// small gap (the old cuts), and the newest cut opens wide, then settles to the same gap -- cut after cut, it shatters
void rock_sphere(u32 dev, float CX, float CY, float R, float k, float bt, float A) {
    float ang[8], amt[8]; int nc = 0, act = -1; float actAge = 0.0f;
    const float G = 2.2f * k, OPEN = 6.0f * k;
    for (int i = 0; i < 8; ++i) {
        const float age = bt - slash_time(i); if (age < 0.05f) continue;
        const float o = age < 0.15f ? (age - 0.05f) / 0.1f * OPEN : (age < 0.3f ? OPEN : OPEN - (OPEN - G) * ((age - 0.3f) / 0.3f < 1.0f ? (age - 0.3f) / 0.3f : 1.0f));
        ang[nc] = slash_angle(i); amt[nc] = o; if (age < 0.6f) { act = nc; actAge = age; } ++nc;
    }
    if (!nc) { rock_half(dev, CX, CY, R, k, 0.0f, TAU, A); return; }
    float bd[16]; int nb = 0;   // the piece boundaries : both ends of every cut line, in [0, TAU)
    for (int c = 0; c < nc; ++c) for (int e = 0; e < 2; ++e) { float v = ang[c] + e * PI; v -= TAU * floorf(v / TAU); bd[nb++] = v; }
    for (int i = 1; i < nb; ++i) { const float v = bd[i]; int j = i - 1; while (j >= 0 && bd[j] > v) { bd[j + 1] = bd[j]; --j; } bd[j + 1] = v; }
    for (int i = 0; i < nb; ++i) {
        const float a0 = bd[i], a1 = i + 1 < nb ? bd[i + 1] : bd[0] + TAU, span = a1 - a0; if (span < 0.01f) continue;
        const float m = (a0 + a1) * 0.5f; float ox = 0.0f, oy = 0.0f;
        for (int c = 0; c < nc; ++c) { const float sd = sinf(m - ang[c]) > 0.0f ? 1.0f : -1.0f; ox += -sinf(ang[c]) * sd * amt[c]; oy += cosf(ang[c]) * sd * amt[c]; }   // pushed off every cut it borders
        rock_half(dev, CX + ox, CY + oy, R, k, a0, span, A);
    }
    if (act >= 0 && actAge < 0.45f) for (int sd = -1; sd <= 1; sd += 2) {   // the newest CUT FACES, hot from the blade
        const float ex = CX + cosf(ang[act]) * R * sd, ey = CY + sinf(ang[act]) * R * sd;
        bloom(dev, ex, ey, 7.0f * k, C(0xFFE0A0, 0.8f * (1.0f - actAge / 0.45f)));
    }
    fx_end(dev);
}
void slash(u32 dev, float CX, float CY, float R, float k, float ang, float age, float side) {
    if (age < 0.0f || age > 0.55f) return;
    const float rev = age / 0.07f < 1.0f ? age / 0.07f : 1.0f, fade = age < 0.12f ? 1.0f : 1.0f - (age - 0.12f) / 0.43f;
    const float thin = 1.0f - 0.6f * ((age - 0.1f) / 0.45f > 0.0f ? (age - 0.1f) / 0.45f : 0.0f), L = R * 2.5f, W = 3.4f * k * thin;
    const float dx = cosf(ang), dy = sinf(ang), nx = -dy * side, ny = dx * side;
    const int N = 32; float xs[N], ys[N], hw[N], hg[N]; u32 c[N], cg[N];
    const int n = 2 + (int)((N - 2) * rev);
    for (int i = 0; i < n; ++i) {
        const float s2 = -1.0f + 2.0f * i / (N - 1), bow = R * 0.22f * (1.0f - s2 * s2);   // a slight curve : the blade's arc
        xs[i] = CX + dx * L * s2 + nx * bow; ys[i] = CY + dy * L * s2 + ny * bow;
        const float pr = powf(sinf(PI * (s2 + 1.0f) * 0.5f), 0.8f);                         // tapered to a point at each end
        hw[i] = W * pr; hg[i] = W * pr * 3.6f + 0.5f * k;
        c[i] = C(0xFFFFFF, fade); cg[i] = C(0xF0D090, 0.4f * fade);
    }
    ribbon(dev, xs, ys, n, hg, cg);   // the glow
    ribbon(dev, xs, ys, n, hw, c);    // the edge
    if (age > 0.06f) {                // the two lips of the cut, opening apart
        const float op = (age - 0.06f) / 0.49f, off = (1.5f + 4.0f * op) * k;
        for (int sgn = -1; sgn <= 1; sgn += 2) {
            float lx[N], ly[N], lw[N]; u32 lc[N];
            for (int i = 0; i < n; ++i) { const float s2 = -1.0f + 2.0f * i / (N - 1); lx[i] = xs[i] + nx * off * sgn; ly[i] = ys[i] + ny * off * sgn;
                lw[i] = 0.9f * k * powf(sinf(PI * (s2 + 1.0f) * 0.5f), 0.8f); lc[i] = C(0xFFF0C8, 0.55f * fade * (1.0f - op)); }
            ribbon(dev, lx, ly, n, lw, lc);
        }
    }
    if (age < 0.15f) bloom(dev, CX, CY, R * (1.0f + 1.5f * age / 0.15f), C(0xFFF6D8, 0.7f * (1.0f - age / 0.15f)));   // the flash of the stroke
}

// ---- the ring BECOMES the skillchain's matter// ---- the ring BECOMES the skillchain's matter (Liquefaction's lava, Impaction's electricity, ... ) ----
// a band (noise mesh) on the ring's own radius : per property its colours and its motion
bool ring_band(int P, RingFx& f, float A) {
    switch (P) {
    case SCP_Detonation:    f = { -5, 1, 10, 2.0f, 5, 6.0f, 0xE9FFF0, 0x3FD46A, 0x0A4A1A, A, A, 5, 2.4f, 0.3f, 1.5f, false }; return true;
    case SCP_Scission:      f = { -5, 1, 8, 0.6f, 5, 0.5f, 0xF0DCB0, 0xA8743A, 0x2A1A0A, A, A, 6, 0.08f, 0.05f, 0.0f, false }; return true;   // rock : barely moving
    case SCP_Reverberation: if (sx_rv == REV_A2) { f = { -5, 1, 10, 2.0f, 5, 1.5f, 0xA8DCFF, 0x2E8CFF, 0x0A2A6A, 0.9f * A, A, 4, 0.03f, 0.04f, 0.15f, false }; return true; }   // (its texture : the caustics, set at the call)   // F : A in a luminous water blue (sums to bright blue, not white)
                            if (sx_rv == 0) { f = { -5, 1, 10, 2.5f, 8, 4.0f, 0xE6F6FF, 0x4AA8FF, 0x0A2A6A, A, A, 7, 0.5f, -0.5f, 0.6f, false }; return true; }   // A : the ORIGINAL
                            f = { -5, 1, 10, 2.5f, 8, 4.0f, 0x7CC8FF, 0x1F6FE0, 0x06205A, 0.75f * A, 0.80f * A, 7, 0.5f, -0.5f, 0.6f, false }; return true;   // the current : deep blue
    case SCP_Induration: return false;   // its own band : liquid, then frozen (fx_property)
    case SCP_Compression: return false;   // its own band (fx_property)
    case SCP_Transfixion:   f = { -5, 1, 9, 0.8f, 4, 1.0f, 0xFFFFFF, 0xFFF3C4, 0x6A5A2A, A, A, 4, 0.2f, 0.3f, 0.0f, false }; return true;
    case SCP_Fusion:        f = { -5, 1, 10, 2.0f, 7, 4.0f, 0xFFFFFF, 0xFF7A2A, 0x8A1004, A, A, 6, 1.2f, 0.6f, 2.2f, false }; return true;
    case SCP_Fragmentation: f = { -5, 1, 9, 2.0f, 9, 5.0f, 0xF0FFF4, 0x3FD46A, 0x3A1A8A, A, A, 6, 1.4f, 0.4f, 0.5f, false, false, 3.0f, true }; return true;   // the air, torn where it is struck
    case SCP_Distortion:    f = { -5, 2, 11, 2.0f, 4, 3.0f, 0xF0FFFF, 0x5FD0E0, 0x0A2A6A, A, A, 4, 0.9f, 1.2f, 3.5f, false }; return true;
    case SCP_Gravitation: return false;   // its own band (fx_property)
    case SCP_Light:         f = { -5, 2, 11, 1.5f, 8, 2.0f, 0xFFFFFF, 0xFFF6DA, 0x6A5A3A, A, A, 6, 0.8f, 0.6f, 0.0f, false }; return true;
    case SCP_Radiance:      f = { -5, 2, 11, 1.5f, 8, 2.0f, 0xFFFFFF, 0xFFE08A, 0x6A5A3A, A, A, 6, 0.8f, 0.6f, 0.0f, false }; return true;
    case SCP_Darkness: return false;   // its own band (fx_property)
    case SCP_Umbra: return false;   // its own band (fx_property)
    default: return false;   // Liquefaction / Impaction draw their own
    }
}
// bright currents flowing ON the ring (Liquefaction's highlights, generalised) : n arcs, their speed (rad/s, sign =
// direction), their length, two alternating colours
void flow_arcs(u32 dev, float CX, float CY, float R, float k, float t, int n, float spd, float len, u32 c1, u32 c2, float al, float w) {
    cqs(dev); blend(dev, true);
    for (int q = 0; q < n; ++q) {
        const float a = t * spd + q * TAU / n, l = len * (0.7f + 0.3f * sinf(t * 2.0f + q)), r = R + (1.0f + 1.5f * sinf(a * 6.0f - t * 3.2f)) * k;
        ring_sector(dev, CX, CY, r - w * k * 0.5f, r + w * k * 0.5f, a, a + l, C(q & 1 ? c1 : c2, al));
    }
    blend(dev, false);
}
// glints riding round the ring : 4-point stars
void glints(u32 dev, float CX, float CY, float R, float k, float t, int n, float spd, u32 col, float al, float sz) {
    bat_begin();
    for (int q = 0; q < n; ++q) {
        const float a = t * spd + q * TAU / n, tw = 0.4f + 0.6f * fabsf(sinf(t * 4.0f + q * 1.7f));
        star(dev, CX + cosf(a) * (R + 1.0f * k), CY + sinf(a) * (R + 1.0f * k), (sz * (0.6f + 0.6f * tw)) * k, C(col, tw * al));
    }
    bat_end(dev);
}
// the bolt pool's clock : the time since its last frame (0 when it jumped : a loop or a new scene, the pool emptied)
float bolt_clock(float t) {
    if (sx_bLast > t || t - sx_bLast > 0.5f) { sx_nb = 0; sx_bAcc = 0.0f; sx_bLast = t; return 0.0f; }
    const float d = t - sx_bLast; sx_bLast = t; return d;
}
// strands of lightning ON the ring, relaying each other all the way round (Impaction's electric ring, generalised)
void ring_strands(float t, float R, float k, int want, u32 g1, u32 g2) {
    int n = 0; for (int i = 0; i < sx_nb; ++i) if (sx_b[i].kind == 2) ++n;
    while (n < want) { bolt_add(t - rnd() * 0.1f, 2, rnd() * TAU, TAU / 3.0f + rnd() * 0.6f, R, 2.6f * k, 0.0f, 1.2f * k, 0.18f + rnd() * 0.18f, (n & 1) ? g2 : g1); ++n; }
}
// TRANSFIXION : lances of light PIERCING the sphere from inside out -- six at once at the start, then one every
// 0.28 s at a new angle ; each shoots out in 0.1 s (from under the text to well past the ring), a white-hot point
// where it breaks through, then fades. Drawn in the front pass : over the sphere's dark disc, under the text.
float hash1(float i) { const float v = sinf(i * 12.9898f) * 43758.5453f; return v - floorf(v); }
void lances(u32 dev, float CX, float CY, float R, float k, float bt, float per = 0.28f, int n0 = 6, u32 glow = 0xFFE7A0) {
    if (bt < 0.0f) return;
    bat_begin();
    const int last = (int)((bt - 0.3f) / per);
    for (int i = -n0; i <= last; ++i) {
        const float ts = i < 0 ? 0.0f : 0.3f + i * per, age = bt - ts;
        if (age < 0.0f || age > 0.45f) continue;
        const float a = i < 0 ? (i + n0) * TAU / n0 + 0.3f : hash1((float)i + 0.5f) * TAU, ext = age < 0.1f ? age / 0.1f : 1.0f;
        const float fade = age < 0.1f ? 1.0f : 1.0f - (age - 0.1f) / 0.35f, L = R * REACH * (i < 0 ? 2.2f : 1.6f + 0.6f * hash1((float)i + 7.3f));
        const float r0 = R * 0.72f, r1 = r0 + (R + L - r0) * ext, ca = cosf(a), sa = sinf(a);
        float xs[8], ys[8], hw[8], hg[8]; u32 c[8], cg[8];
        for (int j = 0; j < 8; ++j) { const float u = j / 7.0f, r = r0 + (r1 - r0) * u, sh = u < 0.35f ? u / 0.35f : 1.0f - (u - 0.35f) / 0.65f * 0.92f;   // a spear : fullest at the break, a point at the tip
            xs[j] = CX + ca * r; ys[j] = CY + sa * r; hw[j] = 1.6f * k * sh; hg[j] = 5.0f * k * sh; c[j] = C(0xFFFFFF, fade); cg[j] = C(glow, 0.45f * fade); }
        ribbon(dev, xs, ys, 8, hg, cg); ribbon(dev, xs, ys, 8, hw, c);
        if (age < 0.25f) star(dev, CX + ca * R, CY + sa * R, (10.0f - 20.0f * age) * k, C(0xFFFFFF, 1.0f - age / 0.25f));   // breaking through
    }
    bat_end(dev);
}
// what flows ON each ring (drawn in the FRONT pass : over the backing, under the burst icons and the text)
void ring_front(u32 dev, int P, float CX, float CY, float R, float k, float t, float A) {
    switch (P) {
    case SCP_Detonation:    break;   // the gusts are in fx_property (gathering / exploding)
    case SCP_Scission:      break;   // the stone holds still : only the cuts move
    case SCP_Reverberation: break;   // no flowing arcs : they read as bars spinning on the ring ; the water band + bubbles carry it
    case SCP_Induration:    { const float sb = sx_bt; if (sb > 1.6f) glints(dev, CX, CY, R, k, t, 6, 0.3f, 0xFFFFFF, A * (sb - 1.6f < 0.4f ? (sb - 1.6f) / 0.4f : 1.0f), 7.0f); } break;   // the ice glints once set
    case SCP_Compression:   break;   // nothing turning : it all falls in (fx_property)
    case SCP_Transfixion:   lances(dev, CX, CY, R, k, sx_bt); break;
    case SCP_Fusion:        lances(dev, CX, CY, R, k, sx_bt, 0.7f, 4, 0xFFB040); break;   // the light breaking out of the fire
    case SCP_Fragmentation: bolts_draw(dev, CX, CY, t); break;
    case SCP_Distortion:    break;   // the vortex is in fx_property
    case SCP_Gravitation:   break;   // the sphere and its chunks are in fx_property
    case SCP_Light: case SCP_Radiance:
        flow_arcs(dev, CX, CY, R, k, t, 8, 1.0f, 0.4f, 0xFFFFFF, P == SCP_Radiance ? 0xFFE08A : 0xFFF3C4, 0.9f * A, 2.2f); glints(dev, CX, CY, R, k, t, 4, 0.7f, 0xFFFFFF, A, 9.0f); break;
    case SCP_Darkness:      pulse_ring(dev, CX, CY, R - 5.0f * k, 1.4f * k, 0x9A5AF0, 0.85f * A); pulse_ring(dev, CX, CY, R + 7.0f * k, 1.6f * k, 0x7D3AD8, 0.75f * A); break;   // the eclipse's violet rims
    case SCP_Umbra:         pulse_ring(dev, CX, CY, R - 5.0f * k, 1.4f * k, 0xFF5A7A, 0.85f * A); pulse_ring(dev, CX, CY, R + 7.0f * k, 1.6f * k, 0xC02050, 0.75f * A); break;
    default: break;
    }
}

// ---------------------------------------------------------------- each property's animation ----
// drawn BEHIND the ring, the text and the burst icons ; `front` = the pass drawn over the ring (Scission's disc)
// Insert in hud_scfx.cpp, in its anonymous namespace, immediately before fx_property.
// Screen-space lengths below are specified at k = 1. No owned GPU resources.
namespace reverb_water {
enum { Segments = 72, Rows = 15, Pair = 2 * (Segments + 1),
       StripSize = (Rows - 1) * Pair + 2 * (Rows - 2) };
const float Inner = -3.0f, Outer = 14.0f, ShoreSwing = 2.5f;
const float FirstSpeed = 33.0f, FirstDuration = 0.80f, EchoDelay = 0.22f;
const float HoldPeriod = 1.45f, HoldSpacing = 22.0f;
const float FirstStrength = 0.93f, EchoStrength = 0.52f, HoldStrength = 0.56f;
const float RedCeiling = 44.0f, GreenCeiling = 143.0f, BlueCeiling = 195.0f;
float unit(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
float smooth01(float x) { x = unit(x); return x*x*(3.0f - 2.0f*x); }

// Wider face behind the moving crest, short face towards the leading edge.
float face(float distance, float width) {
    const float span = distance < 0.0f ? width : 0.52f * width;
    return smooth01(1.0f - fabsf(distance) / span);
}

// Signed distance to the nearest repeating crest. Called only for the quiet swell.
float wrapped(float distance) {
    return distance - HoldSpacing * floorf(distance / HoldSpacing + 0.5f);
}
}

// ---- REVERBERATION, VARIANT E : WATER, with TRAPPED WAVES that BOUNCE (the word : a wave that reflects and repeats) ----
// One additive surface (colour per vertex, capped : never white, never ice). The band is a channel of water between the
// disc's edge and an outer shore ; a wave launched at the inner wall runs out, BOUNCES off the shore, runs back,
// bounces again -- losing strength at each bounce (the echo). Where an outgoing and a returning crest cross they add up
// (interference). A crest is never a perfect circle : its position wobbles round the ring, its strength is broken into
// patches, and it only shines where it faces the light. At every shore bounce DROPS splash out (own small pool, gravity),
// a whole crown of them at the formation ; GLINTS ride the crest tops.
namespace rev_e {
enum { SEG = 96, ROWS = 16, STRIP = (ROWS - 1) * 2 * (SEG + 1) + 2 * (ROWS - 2), MAXD = 64, NP = 3 };
const float CH_IN = -3.0f, CH_OUT = 13.0f;              // the channel, px at k = 1 from R (the disc covers up to -4)
const float SPEED = 42.0f;                        // px / s across the channel : out and back in ~0.76 s
const float BOUNCE = 0.62f, FADE = 0.35f;         // kept at each bounce ; slow loss over time
const float WIDTH = 2.8f, TRAIL = 1.7f;           // a crest's leading half width (px) ; its trailing side is TRAIL x wider
const float HOLD_FIRST = 1.0f, HOLD_EVERY = 1.6f, HOLD_AMP = 0.55f;   // the hold : the water re-excited gently
const float LIGHT = -2.3f;                        // the light comes from 10:30
const float GRAV = 300.0f;                        // drops : px / s^2 (x k)
struct Drop { float x, y, vx, vy, age, life, sz; };
Drop dr[MAXD]; int nd = 0;
float lastBt = -1.0f, lastT = 0.0f; int hits[NP] = { 0, 0, 0 }; int launchedId[NP] = { -1, -1, -1 };
float sm(float x) { x = x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); return x * x * (3.0f - 2.0f * x); }
// the pulses alive at bt : the formation's (id 0, strength 1) and the hold's re-excitations (id 1.., gentler)
int pulses(float bt, float* t0, float* a0, int* id) {
    int n = 0;
    if (bt >= 0.0f) { t0[n] = 0.0f; a0[n] = 1.0f; id[n] = 0; ++n; }
    if (bt >= HOLD_FIRST) {
        const int j = (int)((bt - HOLD_FIRST) / HOLD_EVERY);
        for (int q = j - 1; q <= j && n < NP; ++q) if (q >= 0) { t0[n] = HOLD_FIRST + q * HOLD_EVERY; a0[n] = HOLD_AMP; id[n] = 1 + q; ++n; }
    }
    return n;
}
// where pulse (t0, a0) is across the channel (0 .. L), its strength, which way it runs (+1 out, -1 in), its bounce count
bool pulse_at(float bt, float t0, float a0, float L, float& xp, float& amp, float& dir, int& n) {
    const float age = bt - t0; if (age < 0.0f) return false;
    const float sdist = SPEED * age; n = (int)(sdist / L); const float f = sdist - n * L;
    xp = (n & 1) ? L - f : f; dir = (n & 1) ? -1.0f : 1.0f;
    amp = a0 * powf(BOUNCE, (float)n) * expf(-age * FADE);
    return amp > 0.02f;
}
float crest_profile(float d, float dir) {   // asymmetric : short in front, long behind
    const float w = (d * dir < 0.0f) ? WIDTH * TRAIL : WIDTH; const float u = 1.0f - fabsf(d) / w; return u > 0.0f ? u * u * (3.0f - 2.0f * u) : 0.0f;
}
float patch(float th, float t) {   // the crest broken into patches round the ring, drifting
    const float m = 0.55f + 0.30f * sinf(3.0f * th + 0.5f * t) + 0.25f * sinf(5.0f * th - 0.8f * t + 1.3f);
    return m < 0.0f ? 0.0f : (m > 1.0f ? 1.0f : m);
}
float facing(float th) { return 0.5f + 0.5f * cosf(th - LIGHT); }
void spawn_drop(float x, float y, float vx, float vy, float life, float sz) {
    if (nd >= MAXD) return; Drop& d = dr[nd++]; d.x = x; d.y = y; d.vx = vx; d.vy = vy; d.age = 0.0f; d.life = life; d.sz = sz;
}
}

void fx_rev_e(u32 dev, float CX, float CY, float R, float k, float t, float bt, float A, bool front) {
    using namespace rev_e;
    (void)A;   // its own envelope : the formation's slow fade-in would hide the splash
    if (front || k <= 0.0f) return;
    static VtxC mesh[ROWS][SEG], strip[STRIP];
    static float cs[SEG], sn[SEG]; static bool ready = false;
    if (!ready) { for (int i = 0; i < SEG; ++i) { cs[i] = cosf(TAU * i / SEG); sn[i] = sinf(TAU * i / SEG); } ready = true; }
    // the clock : a restart (the demo loops, a new chain) clears the drops and the bounce counters
    float dt = t - lastT; if (dt < 0.0f || dt > 0.05f) dt = 0.0f; lastT = t;
    const bool restart = bt < lastBt - 0.01f || lastBt < 0.0f;
    if (restart && bt >= 0.0f) {   // THE FORMATION : a crown of drops splashing out all round
        nd = 0; for (int q = 0; q < NP; ++q) { hits[q] = 0; launchedId[q] = -1; }
        for (int i = 0; i < 22; ++i) {
            const float a = (i + rnd() * 0.7f) * TAU / 22.0f, sp = (70.0f + rnd() * 55.0f) * k, r0 = R + CH_OUT * k;
            spawn_drop(CX + cosf(a) * r0, CY + sinf(a) * r0, cosf(a) * sp, sinf(a) * sp - 35.0f * k, 0.45f + rnd() * 0.25f, (1.4f + rnd() * 1.2f) * k);
        }
    }
    lastBt = bt;
    if (bt < 0.0f) return;
    float t0[NP], a0[NP]; int id[NP]; const int np = pulses(bt, t0, a0, id);
    const float L = CH_OUT - CH_IN;
    // SPLASHES : each time a pulse reaches the outer shore, a few drops where it is strong and lit
    for (int q = 0; q < np; ++q) {
        float xp, amp, dir; int n; if (!pulse_at(bt, t0[q], a0[q], L, xp, amp, dir, n)) continue;
        int slot = -1; for (int z = 0; z < NP; ++z) if (launchedId[z] == id[q]) slot = z;
        if (slot < 0) { for (int z = 0; z < NP; ++z) if (launchedId[z] < 0 || launchedId[z] < id[q] - 2) { slot = z; launchedId[z] = id[q]; hits[z] = 0; break; } }
        if (slot < 0) continue;
        const int outerHits = (n + 1) / 2;   // n odd = it has met the shore (n+1)/2 times
        if (outerHits > hits[slot]) {
            hits[slot] = outerHits;
            const int cnt = (int)(3.0f + 5.0f * amp);
            for (int i = 0; i < cnt; ++i) {
                float a = rnd() * TAU; for (int tries = 0; tries < 4 && patch(a, t) * facing(a) < 0.35f; ++tries) a = rnd() * TAU;   // where the crest is strong
                const float sp = (30.0f + rnd() * 35.0f) * k * (0.5f + 0.5f * amp), r0 = R + CH_OUT * k;
                spawn_drop(CX + cosf(a) * r0, CY + sinf(a) * r0, cosf(a) * sp, sinf(a) * sp - 25.0f * k, 0.30f + rnd() * 0.25f, (1.0f + rnd() * 0.9f) * k);
            }
        }
    }
    // THE SURFACE
    const float flash = bt < 0.25f ? 0.30f * (1.0f - bt / 0.25f) : 0.0f;   // the water lights up as the chain forms
    const float level = (0.70f + 0.30f * sm(bt / 0.30f)) + flash;
    for (int i = 0; i < SEG; ++i) {
        const float th = TAU * i / SEG;
        const float shore = CH_OUT + 1.5f * sinf(2.0f * th + 0.8f * t) + 0.6f * sinf(5.0f * th - 1.3f * t);
        const float wob = 1.1f * sinf(4.0f * th + 1.3f * t) + 0.5f * sinf(7.0f * th - 0.9f * t);   // the crest never a perfect circle
        const float pm = patch(th, t), fc = facing(th);
        for (int row = 0; row < ROWS; ++row) {
            const float v = (float)row / (ROWS - 1), off = CH_IN + (shore - CH_IN) * v, x = off - CH_IN;
            float crest = 0.0f;
            for (int q = 0; q < np; ++q) { float xp, amp, dir; int n; if (pulse_at(bt, t0[q], a0[q], L, xp, amp, dir, n)) crest += amp * crest_profile(x - (xp + wob), dir); }   // interference : they add
            crest *= 1.3f * pm * (0.40f + 0.60f * fc); if (crest > 1.2f) crest = 1.2f;
            const float fin = sm((off - CH_IN) / 2.0f), fout = sm((shore - off) / 3.0f);
            const float men = 0.45f * expf(-((v - 0.10f) * (v - 0.10f)) / 0.006f) + 0.60f * expf(-((v - 0.86f) * (v - 0.86f)) / 0.010f);   // the two banks catch the light
            const float shim = 0.85f + 0.15f * sinf(3.0f * th + 2.1f * v + 0.9f * t) * sinf(7.0f * th - 0.6f * t + 3.0f * v);
            const float body = (0.42f + men) * shim * (0.85f + 0.15f * fc);
            const float g = level * fin * fout;
            float r = g * (2.0f * body + 55.0f * crest), gg = g * (38.0f * body + 165.0f * crest), b = g * (105.0f * body + 205.0f * crest);   // a saturated BLUE body (additive on a warm ground goes grey otherwise), bright crests
            if (r > 75.0f) r = 75.0f; if (gg > 200.0f) gg = 200.0f; if (b > 255.0f) b = 255.0f;
            VtxC& pv = mesh[row][i];
            const float rad = R + k * off;
            pv.x = CX + rad * cs[i] - 0.5f; pv.y = CY + rad * sn[i] - 0.5f; pv.z = 0.0f; pv.rhw = 1.0f;
            pv.color = 0xFF000000u | ((u32)r << 16) | ((u32)gg << 8) | (u32)b;
        }
    }
    int cnt = 0;
    for (int row = 0; row < ROWS - 1; ++row) {
        if (row > 0) { strip[cnt] = strip[cnt - 1]; ++cnt; strip[cnt++] = mesh[row][0]; }
        for (int i = 0; i <= SEG; ++i) { const int c = i == SEG ? 0 : i; strip[cnt++] = mesh[row][c]; strip[cnt++] = mesh[row + 1][c]; }
    }
    cqs(dev); dSetRS(dev, D3DRS_SHADEMODE, D3DSHADE_GOURAUD); dSetRS(dev, D3DRS_ALPHABLENDENABLE, 1); blend(dev, true);
    fdraw(dev, D3DPT_TRIANGLESTRIP, cnt - 2, strip, sizeof(VtxC));
    fx_end(dev);
    // GLINTS : points of light riding the crest tops, where they face the light
    for (int i = 0; i < 8; ++i) {
        const float th = i * TAU / 8.0f + 0.15f * t + 0.3f * sinf(0.4f * t + i);
        const float pm = patch(th, t), fc = facing(th); if (pm * fc < 0.30f) continue;
        for (int q = 0; q < np; ++q) {
            float xp, amp, dir; int n; if (!pulse_at(bt, t0[q], a0[q], L, xp, amp, dir, n)) continue;
            const float in = amp * pm * fc * (0.6f + 0.4f * sinf(17.0f * t + i * 2.3f)); if (in < 0.22f) continue;
            const float wob = 1.1f * sinf(4.0f * th + 1.3f * t) + 0.5f * sinf(7.0f * th - 0.9f * t), rad = R + k * (CH_IN + xp + wob);
            const float gx = CX + cosf(th) * rad, gy = CY + sinf(th) * rad;
            bloom(dev, gx, gy, 5.0f * k, C(0x7FD8FF, 0.35f * in)); bloom(dev, gx, gy, 1.8f * k, C(0xF2FBFF, 0.95f * in));
        }
    }
    // the DROPS : fly out, slow, fall ; a blue bead with a point of light
    for (int i = 0; i < nd; ) {
        Drop& d = dr[i]; d.age += dt;
        if (d.age >= d.life) { dr[i] = dr[--nd]; continue; }
        d.vx *= 1.0f - 1.8f * dt; d.vy = d.vy * (1.0f - 1.8f * dt) + GRAV * k * dt; d.x += d.vx * dt; d.y += d.vy * dt;
        const float u = d.age / d.life, fa = u < 0.15f ? u / 0.15f : (1.0f - u) / 0.85f;
        bloom(dev, d.x, d.y, d.sz * 1.6f, C(0x3F9CFF, 0.55f * fa)); bloom(dev, d.x - 0.3f * d.sz, d.y - 0.3f * d.sz, d.sz * 0.6f, C(0xEAF8FF, 0.85f * fa));
        ++i;
    }
    fx_end(dev);
}

void fx_reverberation(u32 dev, float CX, float CY, float R, float k,
                      float t, float bt, float A, bool front) {
    using namespace reverb_water;
    if (front || bt < 0.0f || k <= 0.0f || R <= 4.0f*k) return;
    static VtxC mesh[Rows][Segments], strip[StripSize];
    static float cs[Segments], sn[Segments];
    static bool ready = false;
    if (!ready) {
        for (int i = 0; i < Segments; ++i) {
            cs[i] = cosf(TAU*i/Segments);
            sn[i] = sinf(TAU*i/Segments);
        }
        ready = true;
    }

    const float lead = FirstStrength * (1.0f - smooth01((bt - 0.50f)/0.30f));
    const float echoAge = bt - EchoDelay;
    const float echo = EchoStrength * smooth01(echoAge/0.08f) *
                       (1.0f - smooth01((bt - 0.59f)/(FirstDuration - 0.59f)));
    const float calm = HoldStrength * (0.24f + 0.76f*smooth01((bt - 0.36f)/0.44f));
    const float level = 0.76f + 0.24f * unit(A);
    const float tide = fmodf(bt, HoldPeriod) * (HoldSpacing/HoldPeriod);
    // Use t for a subtle change of reflection; bt sets every propagation phase.
    const float drift = 0.2f * sinf(0.67f*t);
    for (int i = 0; i < Segments; ++i) {
        const float a = TAU*i/Segments;
        const float swell = sinf(2.0f*a + 0.82f*bt) + 0.36f*sinf(3.0f*a - 1.1f*bt);
        const float shore = Outer + ShoreSwing*swell;
        // Off-centre wavefronts: opposing banks meet the wave at different times.
        const float bend = 5.2f*cs[i] - 3.4f*sn[i] + 1.5f*sinf(3.0f*a + drift);
        const float movingBend = bend + 1.5f*swell;
        // Only slopes facing the reflected light show a strong crest. The other
        // sectors retain the quieter water colour; no closed specular outline.
        const float exposure = smooth01((0.75f*cosf(2.0f*a - 0.62f + drift)
                                     + 0.40f*sn[i] + 0.35f)/1.05f);
        const float width = 3.5f + 1.4f*exposure;
        for (int row = 0; row < Rows; ++row) {
            const float v = (float)row/(Rows - 1);
            const float offset = Inner + (shore - Inner)*v;
            const float radius = R + k*offset;
            const float d = offset - bend;
            const float primary = lead * face(d + 2.0f - FirstSpeed*bt, width);
            const float repeat = echo * face(d + 2.0f - FirstSpeed*echoAge, width*0.88f);
            const float quiet = calm * face(wrapped(offset - movingBend - tide), width);
            const float crest = unit(primary + repeat + quiet) * exposure;
            // A broad blue shoulder, an asymmetric bright slope, and a feather
            // at both shores. One blended surface: no stacked additive passes.
            const float edge = smooth01((offset-Inner)/2.4f) * smooth01((shore-offset)/3.6f);
            const float shelf = smooth01(1.0f - fabsf(v - 0.40f)/0.64f);
            const float body = (0.30f + 0.70f*shelf) * (0.78f + 0.22f*exposure);
            const float gain = level*edge;
            const u32 red   = (u32)(gain*(2.0f*body + (RedCeiling-2.0f)*crest));
            const u32 green = (u32)(gain*(25.0f*body + (GreenCeiling-25.0f)*crest));
            const u32 blue  = (u32)(gain*(64.0f*body + (BlueCeiling-64.0f)*crest));
            VtxC& p = mesh[row][i];
            p.x = CX + radius*cs[i] - 0.5f;
            p.y = CY + radius*sn[i] - 0.5f;
            p.z = 0.0f; p.rhw = 1.0f;
            p.color = 0xFF000000u | (red<<16) | (green<<8) | blue;
        }
    }
    // Same winding as radial_disc. Duplicate endpoints between radial strips;
    // the four joining triangles are degenerate and contribute no light.
    int count = 0;
    for (int row = 0; row < Rows - 1; ++row) {
        if (row > 0) {
            strip[count] = strip[count - 1]; ++count;
            strip[count++] = mesh[row][0];
        }
        for (int i = 0; i <= Segments; ++i) {
            const int col = i == Segments ? 0 : i;
            strip[count++] = mesh[row][col];
            strip[count++] = mesh[row + 1][col];
        }
    }
    cqs(dev); // FVF 0x44, diffuse colour
    dSetRS(dev, D3DRS_SHADEMODE, D3DSHADE_GOURAUD);
    dSetRS(dev, D3DRS_ALPHABLENDENABLE, 1);
    blend(dev, true);
    fdraw(dev, D3DPT_TRIANGLESTRIP, count - 2, strip, sizeof(VtxC));
    fx_end(dev); // restores SRCALPHA / INVSRCALPHA
}

void radial_disc(u32 dev, float cx, float cy, const float* rs, const u32* cs, int nr);   // below (the medallion's) ; Liquefaction B's crust uses it
void fx_property(u32 dev, int P, float CX, float CY, float R, float k, float t, float bt, float A, bool front) {
    if (water_variant(P)) { if (sx_rv == REV_BOUNCE) fx_rev_e(dev, CX, CY, R, k, t, bt, A, front); else fx_reverberation(dev, CX, CY, R, k, t, bt, A, front); return; }
    const Pal q = pal(P);
    if (P == SCP_Scission) {   // KATANA cuts : only the cut -- a white crescent swept across, its lips opening, sparks
        if (!front) { rock_sphere(dev, CX, CY, R, k, bt, (A > 0.6f * sx_rise ? A : 0.6f * sx_rise)); return; }   // the stone, split by each cut
        ring_front(dev, P, CX, CY, R, k, t, (A > 0.6f * sx_rise ? A : 0.6f * sx_rise));
        bat_begin(); for (int i = 0; i < 8; ++i) slash(dev, CX, CY, R, k, slash_angle(i), bt - slash_time(i), (i & 1) ? 1.0f : -1.0f); bat_end(dev);
        return;
    }
    if (front) {
        if (P == SCP_Impaction) bolts_draw(dev, CX, CY, t); else ring_front(dev, P, CX, CY, R, k, t, (A > 0.6f * sx_rise ? A : 0.6f * sx_rise)); return; }   // what flows ON the ring
    { RingFx band; if (P == SCP_Reverberation && sx_rv == 2) rev_crest(bt, false);
      if (ring_band(P, band, (A > 0.6f * sx_rise ? A : 0.6f * sx_rise))) {
          if (P == SCP_Reverberation && sx_rv == REV_A2) { sx_ringTex = sx_caus; sx_vScale = 0.45f; }   // F : WATER caustics (round cells, slow) -- not plasma filaments
          if (P == SCP_Reverberation && (sx_rv == 1 || sx_rv == 2)) { band.wave = 3.5f; band.wfreq = 5.0f; band.wspd = 1.6f; band.uSpd = 0.15f; band.vSpd = -0.12f; sx_ringTex = sx_noiseS; }   // WATER : a slow broad swell on a smooth body
          noise_ring(dev, CX, CY, R, k, t, band); sx_ringTex = 0; sx_vScale = 1.0f; }
      sx_cm.on = false; }   // the ring BECOMES the matter
    if (P == SCP_Fragmentation) { sx_fdt = bolt_clock(t); ring_strands(t, R, k, 5, 0x3FD46A, 0xB98CFF); }        // wind + lightning : green and violet strands
    switch (P) {
    case SCP_Liquefaction: {
        if (lq_new(P)) {   // B : LAVA -- crust plates bound by glowing CRACKS (the cell web), heavy (it barely moves), its heat pulsing slowly
            const float heatp = 0.85f + 0.15f * sinf(t * 1.3f);
            {   // the CRUST : cooled lava, near black, OPAQUE (additive light cannot darken) -- the cracks glow over it
                const float rs[5] = { R - 5.0f * k, R - 3.0f * k, R + 8.0f * k, R + 11.5f * k, R + 14.0f * k };
                const u32 cs[5] = { C(0x1C0905, 0.0f), C(0x1C0905, 0.88f * A), C(0x240C06, 0.86f * A), C(0x2A0E06, 0.45f * A), C(0x2A0E06, 0.0f) };
                radial_disc(dev, CX, CY, rs, cs, 5); fx_end(dev);
            }
            RingFx f = { -3, 4, 13, 2.5f, 5, 1.2f, 0xFFD27A, 0xFF5A12, 0x5A0A02, 0.95f * A * heatp, 0.9f * A * heatp, 4, 0.02f, 0.03f, 0.1f, false };
            sx_ringTex = sx_caus; sx_vScale = 0.6f;   // plates ~10 px : one or two across the band
            noise_ring(dev, CX, CY, R, k, t, f);
            sx_ringTex = 0; sx_vScale = 1.0f;
            {   // THE WHOLE SPHERE BURNS -- FLAMES, not a wave (2026-11-23) : big tongues of fire rising UP from all round the
                // magma (their root IN the crust, at full strength from the formation), overlapping, flickering hard ; an
                // orange body with a red tip, a white-gold core. Those of the lower half rise BEHIND the disc : the sphere sits
                // in the blaze, the seconds stay clear. Under them, the magma's surface smoulders.
                const float fl = 0.9f + 0.1f * sinf(t * 9.0f) * sinf(t * 5.3f);
                RingFx ember = { -3, 3, 10, 2.0f, 6, 3.0f, 0xFFD27A, 0xFF5A12, 0x6A0A02, 0.55f * fl, 0.45f * fl, 6, 0.05f, 0.6f, 0.2f, false };   // the surface smouldering
                noise_ring(dev, CX, CY, R, k, t, ember); fx_end(dev);
                // ONE FLAME round the whole sphere (2026-11-23 : "not lots of small flames -- the sphere burning by its heat, one
                // general flame") : wide round the magma, rising into a single TIP above it, its edge licking upward, the tip
                // swaying. Three nested layers like a real flame : the red envelope, the orange body, the white-gold core.
                struct FlameLayer { float scale, lenTop; u32 cIn, cMid, cOut; float aIn, seed; };
                static const FlameLayer FL[3] = {
                    { 1.00f, 16.0f, 0xFF8A2A, 0xE0400C, 0x801004, 0.43f, 0.0f },   // the envelope
                    { 0.75f, 12.0f, 0xFFB040, 0xFF6A1A, 0xC0280A, 0.46f, 1.7f },   // the body
                    { 0.50f,  7.0f, 0xFFF0C0, 0xFFC060, 0xFF7A20, 0.50f, 3.1f } }; // the core   (2026-11-23 : between the first 0.55-0.65 "too intense" and 0.30-0.36 "too transparent")
                // (2026-11-23, against the original : "a smooth halo, the lava lost under it") -- the flame now carries the RIDGED
                // noise (its filaments, plasma on water, are exactly flame tongues here) flowing OUT, and it stays faint over the
                // crust so the cracks show through : the fire round the magma, not on top of it.
                const int SEG = 96, ROWS = 6; static Vtx fv[ROWS - 1][2 * (SEG + 1)];
                for (int L = 0; L < 3; ++L) {
                    const FlameLayer& F = FL[L];
                    for (int i = 0; i <= SEG; ++i) {
                        const float th = i * TAU / SEG, c = cosf(th), sn = sinf(th), up = -sn > 0.0f ? -sn : 0.0f;
                        (void)up;
                        const float lick = 5.0f * k * F.scale * (0.6f * sinf(6.0f * th + 7.0f * t + F.seed) + 0.4f * sinf(11.0f * th - 9.0f * t + 2.0f * F.seed));   // licking all round, the same everywhere
                        const float rEdge = R + 7.0f * k * F.scale + F.lenTop * k * F.scale * (0.85f + 0.15f * sinf(t * 6.0f + F.seed + 3.0f * th)) + lick;   // RADIAL : from the centre outward, evenly (2026-11-23 : "not rising -- uniformly out from the centre")
                        const float sway = 0.0f;   // (no rising tip any more)
                        const float r0 = R - 2.0f * k;
                        float px[ROWS], py[ROWS], tu[ROWS], tv[ROWS]; u32 cc[ROWS];
                        for (int j = 0; j < ROWS; ++j) {
                            const float u = (float)j / (ROWS - 1), rr = r0 + (rEdge - r0) * u;
                            px[j] = CX + c * rr + sway * u - 0.5f; py[j] = CY + sn * rr - 0.5f;
                            const u32 col = u < 0.5f ? lerp(F.cIn, F.cMid, u / 0.5f) : lerp(F.cMid, F.cOut, (u - 0.5f) / 0.5f);
                            const float x2 = (rr - (R + 2.0f * k)) / (8.0f * k), overCrust = 0.30f + 0.70f * (x2 < 0.0f ? 0.0f : (x2 > 1.0f ? 1.0f : x2 * x2 * (3.0f - 2.0f * x2)));   // faint over the crust : the cracks show
                            float al = 1.45f * F.aIn * powf(1.0f - u, 0.6f) * overCrust; if (al > 1.0f) al = 1.0f;   // x1.45 : the texture's dark grain takes light away
                            cc[j] = C(col, al);
                            tu[j] = (float)i / SEG * 6.0f + F.seed; tv[j] = (rr - R) / (18.0f * k) - t * 1.3f;   // the filaments run OUT : tongues
                        }
                        for (int j = 0; j < ROWS - 1; ++j) for (int e = 0; e < 2; ++e) {
                            Vtx& q = fv[j][2 * i + e]; q.x = px[j + e]; q.y = py[j + e]; q.z = 0.0f; q.rhw = 1.0f; q.color = cc[j + e]; q.u = tu[j + e]; q.v = tv[j + e];
                        }
                    }
                    fx_state(dev, sx_noise ? sx_noise : sx_soft, false, true);
                    for (int j = 0; j < ROWS - 1; ++j) fdraw(dev, D3DPT_TRIANGLESTRIP, 2 * SEG, fv[j], sizeof(Vtx));
                    fx_end(dev);
                }
                bloom(dev, CX, CY, R * 1.8f, C(0xFF6A1A, 0.24f)); fx_end(dev);   // the heat, all round
            }
            break;
        }
        RingFx f = { -3, 4, 13, 3.5f, 6, 3.2f, 0xFFE6A0, 0xFF6A1A, 0x8A0E04, 0.95f * A, 0.9f * A, 6, 0.22f, 0.35f, 0, false };
        noise_ring(dev, CX, CY, R, k, t, f);
    } break;
    case SCP_Impaction: {   // a violent start (the explosion), then the sphere stays electric in a loop to the end of the window
        RingFx f = { -5, 1, 9, 1.5f, 14, 13.0f, 0xFFFFFF, 0xB98CFF, 0x4A1AA0, A, A, 8, 2.2f, 0.9f, 0.4f, false };   // the plasma tube : the ring itself
        noise_ring(dev, CX, CY, R, k, t, f);
        const float dtb = bolt_clock(t);
        {   // the ELECTRIC RING : strands of lightning on the ring's radius, each a third of the way round, relaying each
            // other (own seed, own life) so the whole circle crackles without a gap
            int ringN = 0; for (int i = 0; i < sx_nb; ++i) if (sx_b[i].kind == 2) ++ringN;
            while (ringN < 7) { bolt_add(t - rnd() * 0.1f, 2, rnd() * TAU, TAU / 3.0f + rnd() * 0.6f, R, 2.6f * k, 0.0f, 1.2f * k, 0.18f + rnd() * 0.18f); ++ringN; }
        }
        if (sx_im == 3) {   // D : a PLASMA GLOBE -- filaments living INSIDE the crown : from the disc's edge they snake out to the
            // crown's outer edge and strike it on a bright point that glides slowly (a hand on a plasma ball) ; they crackle
            // (drawn anew every frame) ; at each discharge they flare. Nothing leaves the sphere's own area.
            const float A2 = A > 0.6f * sx_rise ? A : 0.6f * sx_rise;
            const float sp = fmodf(bt - 0.8f, 1.1f), surge = (bt > 0.8f && sp < 0.25f) ? 1.0f - sp / 0.25f : 0.0f, form = bt < 0.5f ? 1.0f - bt / 0.5f : 0.0f;
            for (int i = 0; i < 8; ++i) {
                const float ea = i * TAU / 8.0f + 0.5f * sinf(t * (0.35f + 0.07f * i) + i * 2.1f) + 0.2f * sinf(t * 1.3f + i);   // the contact point, gliding
                const float sa = ea + 0.35f * sinf(t * 0.9f + i * 1.3f);                                                     // the root on the disc, lagging
                const float r0 = R - 3.0f * k, r1 = R + (16.0f + 4.0f * surge + 5.0f * form) * k;
                const float flick = 0.65f + 0.35f * rnd(), al = flick * A2 * (0.8f + 0.2f * surge);
                zap(dev, CX + cosf(sa) * r0, CY + sinf(sa) * r0, CX + cosf(ea) * r1, CY + sinf(ea) * r1, (r1 - r0) * 0.35f, (1.2f + 0.6f * surge) * k, 0xC08CFF, al);
                bloom(dev, CX + cosf(ea) * r1, CY + sinf(ea) * r1, (4.5f + 2.0f * surge) * k, C(0xD8B8FF, 0.75f * al));   // where it strikes
                bloom(dev, CX + cosf(ea) * r1, CY + sinf(ea) * r1, 1.6f * k, C(0xFFFFFF, 0.9f * al));
            }
            fx_end(dev);
            if (bt < 0.5f) { const float v = bt / 0.5f; if (v < 0.7f) { bloom(dev, CX, CY, R * (1.8f + 1.0f * v), C(0xFFFFFF, 0.7f * (1.0f - v / 0.7f))); fx_end(dev); } }
            bloom(dev, CX, CY, R * 1.9f, C(0xB98CFF, (0.25f + 0.15f * sinf(t * 5.0f) + 0.3f * surge) * A2)); fx_end(dev);
            break;
        }
        if (sx_im == 1 || sx_im == 2) {   // B / C : a sphere CHARGED with electricity -- TESLA ARCS (C : few, BOLD, a contact point) (2026-11-23 : "a charged sphere with tesla-style arcs")
            // Arcs always alive, a plasma globe's : each leaves a point of the crown, reaches out, forks, crackles (its path drawn
            // anew every frame), drifts round the sphere, lights and dies, and is born again elsewhere. Every 1.1 s the sphere
            // DISCHARGES : the arcs surge longer and brighter for an instant. At the formation they leap out longer. No wave.
            const float A2 = A > 0.6f * sx_rise ? A : 0.6f * sx_rise;
            struct TArc { float a, len, life, age, dr; };
            const bool bold = sx_im == 2; const int NA = bold ? 4 : 7;
            static TArc ta[7]; static bool init = false; static float lastT = 0.0f;
            float dt = t - lastT; if (dt < 0.0f || dt > 0.05f) dt = 0.0f; lastT = t;
            if (!init) { for (int i = 0; i < 7; ++i) { ta[i].a = rnd() * TAU; ta[i].len = R * (0.30f + rnd() * 0.35f); ta[i].life = 0.3f + rnd() * 0.5f; ta[i].age = rnd() * ta[i].life; ta[i].dr = (rnd() - 0.5f) * 1.2f; } init = true; }
            const float sp = fmodf(bt - 0.8f, 1.1f), surge = (bt > 0.8f && sp < 0.25f) ? 1.0f - sp / 0.25f : 0.0f;   // the discharge
            const float charge = (bt > 0.8f && sp > 0.65f) ? (sp - 0.65f) / 0.45f : 0.0f;                         // the charge before it
            const float form = bt < 0.5f ? 1.0f - bt / 0.5f : 0.0f;                                             // the formation's leap
            for (int i = 0; i < NA; ++i) {
                TArc& q = ta[i]; q.age += dt;
                if (q.age >= q.life) { q.a = rnd() * TAU; q.len = R * (bold ? 0.55f + rnd() * 0.35f : 0.30f + rnd() * 0.35f); q.life = bold ? 0.5f + rnd() * 0.4f : 0.3f + rnd() * 0.5f; q.age = 0.0f; q.dr = (rnd() - 0.5f) * 1.2f; }
                q.a += q.dr * dt;
                const float env = sinf(PI * q.age / q.life), flick = 0.6f + 0.4f * rnd();
                const float L = q.len * (1.0f + 0.7f * form + 0.6f * surge + 0.15f * charge);
                const float r0 = R + 2.0f * k, ea = q.a + 0.25f * sinf(t * 3.0f + i * 1.7f);
                const float sx2 = CX + cosf(q.a) * r0, sy2 = CY + sinf(q.a) * r0, ex = CX + cosf(ea) * (r0 + L), ey = CY + sinf(ea) * (r0 + L);
                const float al = env * (bold ? 0.85f + 0.15f * rnd() : flick) * A2 * (0.75f + 0.25f * surge + 0.3f * form > 1.0f ? 1.0f : 0.75f + 0.25f * surge + 0.3f * form);
                zap(dev, sx2, sy2, ex, ey, L * 0.18f, (bold ? 2.3f + 0.8f * surge : 1.0f + 0.5f * surge) * k, bold ? 0xC89CFF : 0xB98CFF, al);
                if (bold) { bloom(dev, ex, ey, (6.0f + 3.0f * surge) * k, C(0xD8B8FF, 0.8f * al)); bloom(dev, ex, ey, 2.2f * k, C(0xFFFFFF, al)); bloom(dev, sx2, sy2, 3.5f * k, C(0xE6D8FF, 0.6f * al)); }   // where it strikes ; its root
            }
            fx_end(dev);
            if (bt < 0.5f) { const float v = bt / 0.5f; if (v < 0.7f) { bloom(dev, CX, CY, R * (1.8f + 1.0f * v), C(0xFFFFFF, 0.7f * (1.0f - v / 0.7f))); fx_end(dev); } }   // the formation's flash
            bloom(dev, CX, CY, R * (1.9f - 0.3f * charge), C(0xB98CFF, (0.25f + 0.15f * sinf(t * 5.0f) + 0.35f * charge * charge + 0.3f * surge) * A2)); fx_end(dev);   // the charge glowing, the discharge flaring
            break;
        }
        if (bt - dtb <= 0.0f || (dtb == 0.0f && bt < 0.05f)) for (int b = 0; b < 9; ++b) bolt_add(t, 1, b * TAU / 9 + rnd() * 0.3f, 0.0f, R + 2.0f * k, 0.0f, R * (1.4f + rnd() * 0.6f), 1.5f * k, 0.25f + rnd() * 0.1f);
        if (bt < 0.5f) {   // the explosion
            const float v = bt / 0.5f;
            if (v < 0.7f) { bloom(dev, CX, CY, R * (2.0f + 2.5f * v), C(0xFFFFFF, 0.95f * (1.0f - v / 0.7f))); bloom(dev, CX, CY, R * 3.2f, C(0xB98CFF, 0.6f * (1.0f - v / 0.7f))); }
        }
        if (bt > 0.25f) {   // the loop : the sphere crackling, lightning wrapping it and lashing out, a glow breathing
            bloom(dev, CX, CY, R * 1.9f, C(0xB98CFF, (0.25f + 0.15f * sinf(t * 5.0f)) * A));
            const float sp = fmodf(bt - 0.8f, 1.1f);
            if (bt > 0.8f) {   // a SHOCK : the sphere discharges -- a flash, a ring of force, bolts thrown straight out
                if (dtb > 0.0f && sp < dtb) for (int b = 0; b < 5; ++b) bolt_add(t, 1, b * TAU / 5 + rnd() * 0.8f, 0.0f, R + 2.0f * k, 0.0f, R * (1.1f + rnd() * 0.5f), 1.4f * k, 0.18f + rnd() * 0.08f);
                if (sp < 0.3f) { const float v = sp / 0.3f;
                    bloom(dev, CX, CY, R * (1.3f + 0.6f * v), C(0xFFFFFF, 0.55f * (1.0f - v)));
                    pulse_ring(dev, CX, CY, R + v * R * 1.1f, (5.0f * (1.0f - v) + 1.0f) * k, 0xC9A7FF, 0.85f * (1.0f - v)); }
            }
            sx_bAcc += dtb * 9.0f;
            while (sx_bAcc >= 1.0f) { sx_bAcc -= 1.0f;
                if (rnd() < 0.3f) bolt_add(t, 0, rnd() * TAU, (0.6f + rnd()) * (rnd() < 0.5f ? 1.0f : -1.0f), R + (4.0f + rnd() * 6.0f) * k, 4.0f * k, 0.0f, 1.1f * k, 0.10f + rnd() * 0.15f);
                else bolt_add(t, 1, rnd() * TAU, 0.0f, R + 3.0f * k, 0.0f, R * (0.7f + rnd() * 0.6f), 1.3f * k, 0.10f + rnd() * 0.15f);
            }
        }
    } break;
    case SCP_Detonation: {
        RingFx f = { -1, 3, 10, 2.5f, 5, 6.0f, 0xE9FFF0, 0x3FD46A, 0x0A4A1A, 0.45f * A, 0.6f * A, 5, 1.2f, 0.2f, 1.2f, false };
        noise_ring(dev, CX, CY, R, k, t, f);
        // the cycle (1.6 s) : the air GATHERS round the sphere (gusts spiralling in, faster and faster), then it EXPLODES
        const float A2 = A > 0.6f * sx_rise ? A : 0.6f * sx_rise;
        if (bt > 0.6f) {
            static float s_ph = 0.0f;
            const float ph = fmodf(bt - 0.6f, 1.6f) / 1.6f;
            if (ph < 0.65f) {   // gathering
                const float g = ph / 0.65f, g2 = g * g;
                bat_begin();
                for (int i = 0; i < 6; ++i) { const float r = R + 4.0f * k + (1.0f - g2) * R * 1.1f + (i & 1) * 5.0f * k, a0 = t * (3.0f + 5.0f * g) + i * TAU / 6;
                    arc_ribbon(dev, CX, CY, a0, a0 + 0.8f, r, r - 6.0f * k, (1.8f + 1.6f * g) * k, i & 1 ? 0xB8FFCC : 0x3FD46A, (0.3f + 0.6f * g) * A2, 1.0f * k, t + i); }
                bat_end(dev);
                bloom(dev, CX, CY, R * 1.5f, C(0x9DFFB8, 0.25f * g2 * A2));   // the pressure building
            } else {            // the EXPLOSION : a flash, three pressure waves, the air thrown out
                const float r = (ph - 0.65f) / 0.35f, e = 1.0f - (1.0f - r) * (1.0f - r);
                if (s_ph < 0.65f) for (int i = 0; i < (int)(40 * sx_pf); ++i) { const float a = rnd() * TAU, v = (90.0f + rnd() * 120.0f) * k;
                    spawn(PM_OUT, CX + cosf(a) * R, CY + sinf(a) * R, cosf(a) * v, sinf(a) * v, 0.4f + rnd() * 0.3f, (1.4f + rnd()) * k); }
                if (r < 0.3f) bloom(dev, CX, CY, R * (1.3f + r), C(0xE9FFF0, 0.6f * (1.0f - r / 0.3f)));
                pulse_ring(dev, CX, CY, R + e * R * 1.6f, ((sx_dOk ? 2.0f : 5.0f) * (1.0f - r) + 1.0f) * k, 0x9DFFB8, (sx_dOk ? 0.5f : 0.9f) * (1.0f - r) * A2);
            }
            s_ph = ph;
        }
    } break;
    case SCP_Reverberation: {
        const float Af = sx_rv == 2 ? (A > 0.85f ? A : 0.85f) : A;   // C : the band at once -- crests riding the formation's slow A would be lost
        RingFx f = { -2, 4, 12, 3.0f, 8, 4.0f, 0xA8E4FF, 0x2C8CF2, 0x0A2A6A, 0.50f * Af, 0.62f * Af, 7, 0.3f, 0.6f, 0.6f, false };   // the water flowing OUT : its highlights (no longer summing to white)
        if (sx_rv == 0) { f.c0 = 0xE6F6FF; f.c1 = 0x4AA8FF; f.a0 = 0.7f * A; f.a1 = 0.85f * A; }   // A : the ORIGINAL
        if (sx_rv == REV_A2) { f.c0 = 0xB8E4FF; f.c1 = 0x3A96FF; f.a0 = 0.65f * A; f.a1 = 0.85f * A; f.uRep = 4; f.uSpd = -0.04f; sx_vScale = 0.45f; f.vSpd = 0.07f; f.twist = 0.1f; f.wave = 2.0f; f.wfreq = 5; f.wspd = 1.5f; sx_ringTex = sx_caus; }   // F : A, bluer, its caustics drifting the other way (the two layers morph like water)
        if (sx_rv == 1 || sx_rv == 2) { f.wave = 4.0f; f.wfreq = 5.0f; f.wspd = 1.6f; f.uSpd = 0.10f; f.vSpd = 0.15f; sx_ringTex = sx_noiseS; }   // WATER, not plasma : slow, broad, smooth
        if (sx_rv == 2) rev_crest(bt, true);   // C : the crests run in THIS band (the first one carries the current)
        noise_ring(dev, CX, CY, R, k, t, f);
        sx_cm.on = false; sx_ringTex = 0; sx_vScale = 1.0f;
        if (sx_rv == 1 || sx_rv == 2) {
            const float A2 = A > 0.6f * sx_rise ? A : 0.6f * sx_rise;
            // REVERBERATION = ripples that repeat : short concentric rings born at the water's edge, one every 0.3 s, gone
            // within 20 px -- a stone in water, over and over (not the big circles that wandered round the title)
            for (int j = 0; j < 3; ++j) {
                const float ph = fmodf(t * 1.1f + j / 3.0f, 1.0f), in = ph * 8.0f < 1.0f ? ph * 8.0f : 1.0f, fo = (1.0f - ph) * sqrtf(1.0f - ph);
                pulse_ring(dev, CX, CY, R + 9.0f * k + ph * 20.0f * k, (1.6f - 1.0f * ph) * k, 0x8FD0FF, 0.50f * fo * in * A2);
            }
            // the GLINTS : a few short tangent highlights sliding slowly on the surface (light on water)
            cqs(dev); blend(dev, true);
            for (int q = 0; q < 4; ++q) {
                const float a = t * 0.22f + q * TAU / 4.0f + 0.4f * sinf(t * 0.7f + q * 2.1f), tw = 0.5f + 0.5f * sinf(t * 2.3f + q * 1.9f);
                const float x = CX + cosf(a) * (R + 2.0f * k), y = CY + sinf(a) * (R + 2.0f * k), tx = -sinf(a), ty = cosf(a), L = (3.0f + 2.0f * tw) * k;
                seg_soft(dev, x - tx * L, y - ty * L, x + tx * L, y + ty * L, 1.1f * k, C(0xEAF8FF, 0.65f * tw * A2));
            }
            blend(dev, false);
        }
        if (sx_rv == 2) {   // C : the CONTACT -- its own envelope (not the formation's slow A), a point of the edge taking the light
            const float PER = 2.6f, bb = bt < 0.0f ? 0.0f : bt; const int n = (int)(bb / PER); const float tau = bb - n * PER, amp = n ? 0.65f : 1.0f;
            if (tau < 0.18f) {
                const float e = tau < 0.03f ? tau / 0.03f : 1.0f - (tau - 0.03f) / 0.15f, px = CX + cosf(REV_TH0) * (R + 3.0f * k), py = CY + sinf(REV_TH0) * (R + 3.0f * k);
                bloom(dev, px, py, 9.0f * k, C(0x7FE6FF, 0.95f * e * amp)); bloom(dev, px, py, 3.0f * k, C(0xFFFFFF, e * amp)); fx_end(dev);
            }
        }
        if (!sx_iso && rev_orig()) { const float A2 = A > 0.6f * sx_rise ? A : 0.6f * sx_rise;   // A only : WAVES leaving the sphere, one after the other, spreading and thinning
          for (int j = 0; j < 2; ++j) { const float ph = fmodf(bt * 0.6f + j * 0.5f, 1.0f), in = ph * 6.0f < 1.0f ? ph * 6.0f : 1.0f;
              if (sx_rv == REV_A2) {   // F : a ripple MADE OF WATER, staying round the sphere (they used to reach the title and the cards)
                  const float cyc = floorf(bt * 0.6f + j * 0.5f);
                  water_ripple(dev, CX, CY, R + 3.0f * k + ph * R * 0.65f, (2.0f * (1.0f - ph) + 0.8f) * 1.6f * k, k, 0x3A96FF, 0.75f * (1.0f - ph) * in * A2, j * 2.3f + cyc * 1.7f, t);
              } else
              pulse_ring(dev, CX, CY, R + 3.0f * k + ph * R * 1.6f, (2.0f * (1.0f - ph) + 0.8f) * k, 0xCFEAFF, (sx_dOk ? 0.3f : 0.55f) * (1.0f - ph) * in * A2); } }
    } break;
    case SCP_Induration: {
        // the freeze : 0.2 -> 1.6 s. The band's own clock SLOWS to a stop (its flow, its waves die), its colour runs
        // from deep water to white ice ; a bright crystallisation front while it sets ; then the spikes shoot out
        const float A2 = A > 0.6f * sx_rise ? A : 0.6f * sx_rise;
        const float x = bt - 0.2f < 0.0f ? 0.0f : (bt - 0.2f > 1.4f ? 1.4f : bt - 0.2f), fz = x / 1.4f, fs = fz * fz * (3.0f - 2.0f * fz);
        const float tl = 30.0f + (bt < 0.2f ? bt : 0.2f) + x - x * x / 2.8f;   // the liquid's time, frozen once set
        RingFx liq = { -5, 1 + fs, 11 - 3 * fs, 3.0f * (1.0f - fs), 7, 4.0f, lerp(0xBFE6FF, 0xFFFFFF, fs), lerp(0x2A8AE0, 0x9FEFFF, fs), lerp(0x0A2A6A, 0x1A5A7A, fs),
                       A2, A2, 6, 0.6f, 0.5f, 0.6f, false };
        noise_ring(dev, CX, CY, R, k, tl, liq);
        if (fz > 0.0f && fz < 1.0f) pulse_ring(dev, CX, CY, R + 2.0f * k, 2.4f * k, 0xFFFFFF, 0.9f * 4.0f * fz * (1.0f - fz));   // crystallising
        if (bt > 1.6f && bt < 1.9f) bloom(dev, CX, CY, R * (1.4f + (bt - 1.6f) * 2.0f), C(0xE8FFFF, 0.7f * (1.0f - (bt - 1.6f) / 0.3f)));   // SET : a cold flash
        int n = 0; const float g0 = (bt - 1.5f) / 0.35f, g = g0 < 0.0f ? 0.0f : (g0 > 1.0f ? 1.0f : 1.0f - (1.0f - g0) * (1.0f - g0) * (1.0f - g0));
        for (int i = 0; i < 12; ++i) {   // ice spikes growing out of the ring, frost shimmering through them
            const float a = i * TAU / 12 + (i & 1 ? 0.12f : 0.0f), L = R * (i & 1 ? 0.55f : 0.85f) * g * (1.0f + 0.04f * sinf(t * 3.0f + i)), w = R * (i & 1 ? 0.09f : 0.13f);
            const float bx = CX + cosf(a) * (R + 3.0f * k), by = CY + sinf(a) * (R + 3.0f * k), tx = CX + cosf(a) * (R + 3.0f * k + L), ty = CY + sinf(a) * (R + 3.0f * k + L), nx = -sinf(a) * w, ny = cosf(a) * w;
            n = tri(n, bx + nx, by + ny, tx, ty, bx, by, C(0xBFF4FF, 0.85f * A), C(0x7FE6FF, 0.2f * A), C(0xFFFFFF, 0.95f * A), t * 0.05f);
            n = tri(n, bx - nx, by - ny, tx, ty, bx, by, C(0x5FB8D0, 0.75f * A), C(0x7FE6FF, 0.2f * A), C(0xFFFFFF, 0.9f * A), t * 0.05f);
        }
        flush(dev, n, sx_noise);
        bat_begin();
        for (int i = 0; i < 5; ++i) { const float a = i * TAU / 5 + t * 0.2f, tw = 0.4f + 0.6f * fabsf(sinf(t * 4.0f + i * 1.3f)); star(dev, CX + cosf(a) * R * 1.55f, CY + sinf(a) * R * 1.55f, (4.0f + 5.0f * tw) * k, C(0xFFFFFF, tw * A * g)); }
        bat_end(dev);
    } break;
    case SCP_Compression: {   // a BLACK HOLE forming : a black core, a blazing accretion disc spiralling in, light bent round it
        // after the sphere's own burst : MATTER falls back in -- streams of it, each from its own direction, accelerating,
        // bending a little as they fall, heating up as they reach the sphere and swallowed there (no ring, no circle)
        const float cb = bt - 0.4f;
        if (cb >= 0.45f && cb < 0.85f) { const float v = (cb - 0.45f) / 0.4f; bloom(dev, CX, CY, R * (3.0f - 1.4f * v), C(0xFFB0DA, 0.8f * (1.0f - v))); }   // the burst's light swallowed
        if (cb > -0.1f) {
            const int ns = cb < 0.85f ? 12 : 7;   // a rush at first, then a steady fall
            bat_begin();
            for (int i = 0; i < ns; ++i) {
                const float per = 0.9f + 0.4f * hash1(i * 1.7f + 0.2f), cyc = (cb + 0.1f) / per + hash1(i * 4.1f), ph = cyc - floorf(cyc);
                const float a = hash1(floorf(cyc) * 5.0f + i * 2.9f) * TAU, e = powf(ph, 1.6f);
                const float rh = R + 2.0f * k + (1.0f - e) * R * 1.4f, rt = rh + R * (0.25f + 0.45f * (1.0f - ph)), bend = 0.35f * (i & 1 ? 1.0f : -1.0f);
                const float fa = (ph < 0.15f ? ph / 0.15f : 1.0f) * (ph > 0.85f ? (1.0f - ph) / 0.15f : 1.0f);
                arc_ribbon(dev, CX, CY, a + bend, a, rt, rh, (1.6f + 1.4f * e) * k, lerp(0xC0306F, 0xFFE0F0, e), 0.9f * fa * A, 0.0f, t);
            }
            bat_end(dev);
        }
        bloom(dev, CX, CY, R * 1.9f, C(0x000000, 0.75f * A), false);                              // the hole's shadow
        const float br = sinf(t * 1.8f);
        RingFx disk = { -3, 4, 17 + 2 * br, 3.0f, 6, 4.0f, 0xFFE0F0, 0xE0479F, 0x3A0A2A, 0.95f * A, 0.9f * A, 6, 0.15f, -1.4f, 0.4f, false, true };
        noise_ring(dev, CX, CY, R, k, t, disk);                                                   // the matter FALLING in (no spin)
        RingFx hot = { -3, 1, 6, 1.0f, 9, 6.0f, 0xFFFFFF, 0xFF8AD0, 0x5A1040, 0.8f * A, 0.7f * A, 9, 0.2f, -2.0f, 0.5f, false, true };
        noise_ring(dev, CX, CY, R, k, t, hot);                                                    // its white-hot inner edge
        bloom(dev, CX, CY, R * 2.3f, C(0xE0479F, 0.25f * A));
    } break;
    case SCP_Transfixion: {
        RingFx f = { -1, 3, 10, 1.2f, 4, 1.0f, 0xFFFFFF, 0xFFF3C4, 0x6A5A2A, 0.5f * A, 0.6f * A, 4, 0.1f, 0.3f, 0.0f, false };
        noise_ring(dev, CX, CY, R, k, t, f);
        bloom(dev, CX, CY, R * 1.6f, C(0xFFF3C4, (0.25f + 0.1f * sinf(t * 6.0f)) * A));   // the light inside, pressing out
    } break;
    case SCP_Fusion: {   // FIRE + LIGHT : a sun being born -- a sphere of fire, flares looping out of it, light breaking through
        RingFx f = { -2, 4, 12, 2.5f, 7, 4.0f, 0xFFFFFF, 0xFF7A2A, 0x8A1004, 0.85f * A, 0.9f * A, 6, 0.35f, 0.6f, 0.4f, false };
        noise_ring(dev, CX, CY, R, k, t, f);
        bloom(dev, CX, CY, R * 1.8f, C(0xFFB040, (0.28f + 0.08f * sinf(t * 3.0f)) * A));   // the glow of a star
        bat_begin();
        for (int i = 0; i < 3; ++i) {   // SOLAR FLARES : arches of fire rising off the surface and falling back, 1.5 s each
            const float cyc = bt * 0.67f + i / 3.0f, ph = cyc - floorf(cyc), h = sinf(ph * PI), a = hash1(floorf(cyc) * 3.0f + i + 0.7f) * TAU;
            float xs[16], ys[16], hw[16], hg[16]; u32 c[16], cg[16];
            for (int j = 0; j < 16; ++j) { const float u = j / 15.0f, ang = a + (u - 0.5f) * 0.75f, bow = sinf(u * PI), r = R + 2.0f * k + bow * R * 0.75f * h;
                xs[j] = CX + cosf(ang) * r; ys[j] = CY + sinf(ang) * r; hw[j] = (1.2f + 1.6f * bow) * k; hg[j] = hw[j] * 3.0f;
                c[j] = C(lerp(0xFF6A1A, 0xFFF4D0, bow * h), 0.95f * A * (h * 2.0f < 1.0f ? h * 2.0f : 1.0f)); cg[j] = C(0xFF4A10, 0.35f * A * h); }
            ribbon(dev, xs, ys, 16, hg, cg); ribbon(dev, xs, ys, 16, hw, c);
        }
        bat_end(dev);
    } break;
    case SCP_Fragmentation: {   // WIND + LIGHTNING : the AIR TORN BY THE LIGHTNING -- a storm round the sphere ; at three
        // points in turn a bolt strikes through the band of air and RIPS it open (the band split, its lips flung apart, the
        // scene torn with it) ; the wind rushes back to close the wound with a shudder ; every 1.6 s a thunderclap
        const float A2 = A > 0.6f * sx_rise ? A : 0.6f * sx_rise, hmax = 14.0f * k;
        sx_bMax = hmax; sx_nbm = 0;
        static float s_fl = 0.0f, s_ph = 0.0f, s_tp[3] = { 0 };
        for (int i = 0; i < 3; ++i) {   // the tears
            const float cyc = bt * 0.85f + i / 3.0f, ph = cyc - floorf(cyc), a = hash1(floorf(cyc) * 3.0f + i + 0.3f) * TAU;
            float h = 0.0f;
            if (bt > 0.35f) {
                if (ph < 0.08f) h = hmax * (ph / 0.08f);                                                    // ripped open
                else if (ph < 0.35f) h = hmax;                                                               // held open
                else { const float x = (ph - 0.35f) / 0.65f; h = hmax * expf(-5.0f * x) * cosf(x * 11.0f); } // the air rushing back
                if (ph < s_tp[i]) { s_fl = 1.0f;                                                             // the STRIKE that tears it
                    bolt_add(t, 1, a, 0.0f, R - 2.0f * k, 0.0f, R * (1.3f + rnd() * 0.6f), 1.6f * k, 0.3f, 0xB98CFF);
                    bolt_add(t, 0, a, (0.7f + rnd() * 0.6f) * (rnd() < 0.5f ? 1.0f : -1.0f), R + 6.0f * k, 4.0f * k, 0.0f, 1.1f * k, 0.2f, 0xB98CFF);
                    for (int q2 = 0; q2 < 8; ++q2) { const float aa = a + (rnd() - 0.5f) * 1.4f, v = (60.0f + rnd() * 90.0f) * k;
                        spawn(PM_OUT, CX + cosf(a) * (R + 8.0f * k), CY + sinf(a) * (R + 8.0f * k), cosf(aa) * v, sinf(aa) * v, 0.35f + rnd() * 0.25f, (1.2f + rnd()) * k); } }
            }
            s_tp[i] = ph; sx_bA[i] = a; sx_bH[i] = h; ++sx_nbm;
        }
        bloom(dev, CX, CY, R * 2.5f, C(0x000000, 0.45f * A2), false);                                 // the storm's dark sky
        bloom(dev, CX, CY, R * 1.8f, C(0x3A1A8A, (0.2f + 0.08f * sinf(t * 7.0f)) * A2));             // charged
        RingFx f = { -1, 3, 12, 2.0f, 9, 5.0f, 0xF0FFF4, 0x3FD46A, 0x0A3A1A, 0.6f * A, 0.7f * A, 6, 1.0f, 0.4f, 0.5f, false, false, 3.0f, true };
        noise_ring(dev, CX, CY, R, k, t, f);                                                       // the wind, torn
        bat_begin();
        for (int i = 0; i < 3; ++i) if (sx_bH[i] > hmax * 0.3f) {   // the LIPS of each tear : two bright edges flung apart
            const float o = 0.07f + 0.1f * sx_bH[i] / hmax;
            for (int sd = -1; sd <= 1; sd += 2) { const float a0 = sx_bA[i] + sd * o;
                arc_ribbon(dev, CX, CY, a0, a0 + sd * 0.35f, R + 4.0f * k + sx_bH[i] * 0.5f, R + 3.0f * k, 1.8f * k, 0xE8FFF0, 0.9f * A2 * sx_bH[i] / hmax, 0.0f, t, true); } }
        for (int i = 0; i < 4; ++i) { const float r = R + (8.0f + (i & 1) * 9.0f) * k, a0 = t * (3.0f + i * 0.4f) + i * 1.9f;   // the gale
            arc_ribbon(dev, CX, CY, a0, a0 + 0.7f, r, r - 3.0f * k, 2.0f * k, i & 1 ? 0xB8FFCC : 0x3FD46A, 0.6f * A2, 1.5f * k, t + i); }
        bat_end(dev);
        if (bt > 0.6f) {   // the THUNDERCLAP
            const float ph = fmodf(bt - 0.6f, 1.6f) / 1.6f;
            if (ph < s_ph) { s_fl = 2.2f;
                for (int b2 = 0; b2 < 6; ++b2) bolt_add(t, 1, b2 * TAU / 6 + rnd() * 0.5f, 0.0f, R + 2.0f * k, 0.0f, R * (1.3f + rnd() * 0.6f), 1.6f * k, 0.22f + rnd() * 0.1f, 0xB98CFF); }
            s_ph = ph;
        }
        if (s_fl > 0.0f) { bloom(dev, CX, CY, R * 2.3f, C(0xE8E0FF, 0.3f * (s_fl < 1.0f ? s_fl : 1.0f) * A2)); s_fl -= sx_fdt * 7.0f; }   // the clouds lit by it
    } break;
    case SCP_Distortion: {   // WATER + ICE : the water DEFORMED by the ice -- at four points round the sphere ice grows
        // out of it, the water bulging and stretching round each crystal ; the ice holds, cracks, SHATTERS, and the water
        // snaps back with a shudder (the scene bent with it) ... and freezes again elsewhere : the matter never settles
        const float hmax = 15.0f * k; sx_bMax = hmax; sx_nbm = 0;
        static float s_cp[4] = { 0 };
        float gs[4];
        for (int i = 0; i < 4; ++i) {
            const float cyc = bt * 0.5f + i / 4.0f + 0.1f, ph = cyc - floorf(cyc), a = i * TAU / 4 + 0.4f + (hash1(floorf(cyc) + i * 2.3f) - 0.5f) * 1.2f;
            const float gr = ph < 0.3f ? ph / 0.3f : 1.0f, g = ph < 0.55f ? gr * gr * (3.0f - 2.0f * gr) : 0.0f;
            float h = g * hmax;
            if (ph >= 0.55f) { const float x = (ph - 0.55f) / 0.45f; h = -0.45f * hmax * expf(-4.0f * x) * cosf(x * 14.0f); }   // the water snapping back
            if (ph >= 0.55f && s_cp[i] < 0.55f && bt > 0.3f) {   // SHATTERED : frost shards and drops thrown off the break
                const float sx2 = CX + cosf(a) * (R + 10.0f * k), sy2 = CY + sinf(a) * (R + 10.0f * k);
                for (int q2 = 0; q2 < 10; ++q2) { const float aa = a + (rnd() - 0.5f) * 1.8f, v = (50.0f + rnd() * 80.0f) * k;
                    spawn(PM_OUT, sx2, sy2, cosf(aa) * v, sinf(aa) * v, 0.5f + rnd() * 0.35f, (1.4f + rnd() * 1.2f) * k); } }
            s_cp[i] = ph; gs[i] = g;
            sx_bA[i] = a; sx_bH[i] = h; ++sx_nbm;
        }
        RingFx f = { -2, 5, 16, 2.0f, 4, 3.0f, 0xF0FFFF, 0x5FD0E0, 0x0A2A6A, 0.6f * A, 0.75f * A, 4, 0.6f, -1.0f, 3.5f, false, true, 0.0f, true };
        noise_ring(dev, CX, CY, R, k, t, f);                                                       // the water, pushed by the ice
        RingFx ice = { -3, 3, 13, 0.3f, 6, 0.4f, 0xFFFFFF, 0xDFF8FF, 0x7FC8E0, 1.0f * A, 0.95f * A, 5, 0.0f, 0.0f, 0.0f, false, false, 2.0f, true };
        noise_ring(dev, CX, CY, R, k, 7.0f, ice);                                                  // the ICE itself, frozen still (its own clock stopped)
        bat_begin();
        for (int q = 0; q < 3; ++q) { const float a0 = q * TAU / 3 - t * 1.4f;   // the water's currents
            arc_ribbon(dev, CX, CY, a0, a0 + 2.0f, R * 1.85f, R + 2.0f * k, 1.8f * k, q & 1 ? 0x4AA8FF : 0x9FD6FF, 0.5f * A, 0.0f, t); }
        bat_end(dev);
        int n = 0;
        for (int i = 0; i < 4; ++i) {   // the CRYSTALS : a cluster of three spikes out of each bulge
            const float g = gs[i]; if (g <= 0.01f) continue;
            for (int sp2 = -1; sp2 <= 1; ++sp2) {
                const float a = sx_bA[i] + sp2 * 0.14f, L = R * (sp2 ? 0.38f : 0.62f) * g, w = R * (sp2 ? 0.06f : 0.09f), base = R + 3.0f * k + bumpf(a) * 0.6f;
                const float bx = CX + cosf(a) * base, by = CY + sinf(a) * base, tx = CX + cosf(a) * (base + L), ty = CY + sinf(a) * (base + L), nx = -sinf(a) * w, ny = cosf(a) * w;
                n = tri(n, bx + nx, by + ny, tx, ty, bx, by, C(0xDFF8FF, 0.95f * A), C(0xFFFFFF, 0.5f * A), C(0xFFFFFF, A), t * 0.05f);
                n = tri(n, bx - nx, by - ny, tx, ty, bx, by, C(0x7FC8E0, 0.85f * A), C(0xBFF4FF, 0.4f * A), C(0xFFFFFF, 0.95f * A), t * 0.05f);
            }
        }
        flush(dev, n, sx_noise);
        bat_begin();
        for (int i = 0; i < 4; ++i) if (gs[i] > 0.95f) { const float a = sx_bA[i], r = R + 3.0f * k + bumpf(a) * 0.6f + R * 0.62f, tw = 0.5f + 0.5f * sinf(t * 9.0f + i * 2.0f);   // the ice creaking : a glint at each tip
            star(dev, CX + cosf(a) * r, CY + sinf(a) * r, (5.0f + 4.0f * tw) * k, C(0xFFFFFF, 0.9f * tw * A)); }
        bat_end(dev);
    } break;
    case SCP_Gravitation: {   // EARTH + DARK : chunks of earth LEVITATING round a sphere of darkness
        const float A2 = A > 0.6f * sx_rise ? A : 0.6f * sx_rise;
        bloom(dev, CX, CY, R * 2.6f, C(0x000000, 0.6f * A2), false);                                  // the darkness weighing on the area
        bloom(dev, CX, CY, R * 2.0f, C(0x8A3AC0, (0.22f + 0.08f * sinf(t * 1.6f)) * A2));            // its violet pull
        RingFx cor = { 1, 6, 15, 3.0f, 9, 3.0f, 0xE8D8FF, 0x9A5AF0, 0x1A0430, 0.85f * A2, 0.8f * A2, 9, 0.25f, 0.9f, 0.3f, false };
        noise_ring(dev, CX, CY, R, k, t, cor);                                                        // the sphere of darkness : its violet rim, licking
        RingFx vd = { -5, 1, 6, 0.5f, 5, 1.0f, 0x000000, 0x000000, 0x000000, 0.98f * A2, 0.95f * A2, 5, -0.3f, -0.5f, 1.5f, true };
        noise_ring(dev, CX, CY, R, k, t, vd);                                                         // its black edge
        // the CHUNKS : the violet field holding each one up (under it), then the earth -- a dark body, its lit top, a crust line
        { int n = 0;
          for (int i = 0; i < NCHUNK; ++i) { float x, y, sz, rot; chunk_at(i, bt, t, CX, CY, R, k, x, y, sz, rot);
              n = put(n, x, y + sz * 0.7f, 1.0f, 0.0f, sz * 2.2f, sz * 1.3f, C(0xA050E0, (0.45f + 0.15f * sinf(t * 2.0f + i)) * A2)); }
          flush(dev, n, sx_soft); }
        cqs(dev);
        for (int i = 0; i < NCHUNK; ++i) {
            float x, y, sz, rot; chunk_at(i, bt, t, CX, CY, R, k, x, y, sz, rot);
            float pts[14], top[14];
            for (int j = 0; j < 7; ++j) { const float a = rot + j * TAU / 7, rr = sz * (0.7f + 0.45f * gh(i * 9.0f + j * 1.3f));
                pts[2 * j] = x + cosf(a) * rr; pts[2 * j + 1] = y + sinf(a) * rr * 0.85f;
                top[2 * j] = x - 0.15f * sz + cosf(a) * rr * 0.62f; top[2 * j + 1] = y - 0.28f * sz + sinf(a) * rr * 0.45f; }
            fill_poly_aa(dev, pts, 7, C(0x2A1A0E, 1.0f));          // the earth's body, in shadow
            fill_poly_aa(dev, top, 7, C(0x8E6A42, 1.0f));          // its lit top
            const float cr[8] = { top[0], top[1], top[2], top[3], top[4], top[5], top[6], top[7] };
            fill_poly_aa(dev, cr, 4, C(0xA8865C, 1.0f));            // a brighter crust facet
        }
    } break;
    case SCP_Light: case SCP_Radiance: {
        const bool rad = P == SCP_Radiance; const float A2 = (A > 0.6f * sx_rise ? A : 0.6f * sx_rise);
        bloom(dev, CX, CY, R * 2.5f, C(rad ? 0xFFE9A0 : 0xFFFFFF, 0.5f * A2));
        RingFx f = { -2, 5, 16, 2.0f, 8, 2.0f, 0xFFFFFF, rad ? 0xFFE08Au : 0xFFF6DAu, 0x6A5A3A, 0.8f * A2, 0.8f * A2, 6, 0.5f, 0.6f, 0.0f, false };
        noise_ring(dev, CX, CY, R, k, t, f);
        bat_begin();
        for (int i = 0; i < 16; ++i) { const float a = i * TAU / 16 + bt * 0.35f, L = R * (1.6f + 0.45f * sinf(t * 2.4f + i * 1.3f));
            float xs[6], ys[6], hw[6]; u32 c[6]; for (int j = 0; j < 6; ++j) { const float u = j / 5.0f, r = R + (L - R) * u; xs[j] = CX + cosf(a) * r; ys[j] = CY + sinf(a) * r; hw[j] = (i & 1 ? 2.0f : 3.2f) * k * (1.0f - 0.6f * u); c[j] = C(i % 4 == 3 ? 0xFFE7A0 : 0xFFFFFF, 0.9f * A2 * (1.0f - u)); }
            ribbon(dev, xs, ys, 6, hw, c); }
        bat_end(dev);
        const float pul = fmodf(bt, 1.4f) / 1.4f;
        for (int j = 0; j < 2; ++j) { const float pj = fmodf(pul + j * 0.5f, 1.0f); pulse_ring(dev, CX, CY, R + 4.0f * k + pj * R * 1.3f, (3.5f * (1.0f - pj) + 0.8f) * k, j ? 0xFFF3C4 : 0xFFFFFF, (1.0f - pj) * 0.9f * A2); }
        { int n = 0; for (int e = 0; e < 4; ++e) { const float a0 = t * 1.7f + e * TAU / 4; for (int q2 = 0; q2 < 12; ++q2) { const float a = a0 - q2 * 0.065f, r = R + 11.0f * k; n = put(n, CX + cosf(a) * r, CY + sinf(a) * r, 1.0f, 0.0f, 3.6f * k * (1.0f - q2 / 14.0f), 3.6f * k * (1.0f - q2 / 14.0f), C(0xFFFFFF, (1.0f - q2 / 12.0f) * 0.95f * A2)); } } flush(dev, n, sx_soft); }
        if (rad) { float xs[9], ys[9], hw[9]; u32 c[9]; for (int i = 0; i < 9; ++i) { const float u = i / 8.0f; xs[i] = CX; ys[i] = CY - R * 3.0f + u * R * 6.0f; hw[i] = R * 0.3f * sinf(u * PI); c[i] = C(0xFFF0B0, 0.7f * A2 * sinf(u * PI)); } ribbon(dev, xs, ys, 9, hw, c); }
    } break;
    case SCP_Darkness: case SCP_Umbra: {   // an EXPLOSION of darkness, then a BLACK SUN : a black disc, a corona of violet flames
        const bool um = P == SCP_Umbra; const float A2 = (A > 0.6f * sx_rise ? A : 0.6f * sx_rise);
        const u32 v0 = um ? 0xFFD0DA : 0xE8D8FF, v1 = um ? 0xFF5A7A : 0x9A5AF0, v2 = um ? 0xC02050 : 0x6A2FC0, v3 = um ? 0x3A0A14 : 0x1A0430;
        if (bt < 0.9f) {   // the start : a wave of darkness swallowing the area round it, a violet blast in it
            const float v = bt / 0.9f;
            bloom(dev, CX, CY, R * (1.6f + 4.5f * sqrtf(v)), C(0x000000, 0.9f * (1.0f - v)), false);
            if (v < 0.4f) bloom(dev, CX, CY, R * (1.8f + 2.5f * v), C(v1, 0.9f * (1.0f - v / 0.4f)));
            shockwave(dev, CX, CY, R, k, v, t, v0, v1, 3.0f);
        }
        bloom(dev, CX, CY, R * 2.0f, C(0x000000, 0.75f * A2), false);                            // the eclipse : black round the disc
        RingFx cor = { 1, 9, 28, 7.0f, 9, 5.0f, v0, v1, v3, 0.95f * A2, 0.9f * A2, 9, 0.4f, 1.5f, 0.3f, false };
        noise_ring(dev, CX, CY, R, k, t, cor);                                                    // the CORONA : violet flames licking out
        RingFx cor2 = { 1, 6, 18, 5.0f, 13, 7.0f, 0xFFFFFF, v2, v3, 0.6f * A2, 0.7f * A2, 13, -0.6f, 2.0f, 0.5f, false };
        noise_ring(dev, CX, CY, R, k, t, cor2);                                                   // a second tongue of it, faster
        RingFx vd = { -5, 1, 6, 0.5f, 5, 1.0f, 0x000000, 0x000000, 0x000000, 0.98f * A2, 0.95f * A2, 5, -0.3f, -0.5f, 1.5f, true };
        noise_ring(dev, CX, CY, R, k, t, vd);                                                     // the black disc's edge
        bloom(dev, CX, CY, R * 2.6f, C(v2, 0.3f * A2));
        { const float pj = fmodf(bt, 1.4f) / 1.4f; pulse_ring(dev, CX, CY, R + 8.0f * k + pj * R * 1.4f, (4.0f * (1.0f - pj) + 1.0f) * k, v1, 0.7f * (1.0f - pj) * A2); }   // waves going out
        static const u32 ET[4] = { 0xC09060, 0x6A8AFF, 0x8FE8FF, 0xC090FF };   // the four elements circling
        { int n = 0; for (int e = 0; e < 4; ++e) { const float a0 = -t * 1.5f + e * TAU / 4;
            for (int q2 = 0; q2 < 12; ++q2) { const float a = a0 + q2 * 0.065f, r = R + 14.0f * k, z = 3.6f * k * (1.0f - q2 / 14.0f); n = put(n, CX + cosf(a) * r, CY + sinf(a) * r, 1.0f, 0.0f, z, z, C(um ? (e & 1 ? 0xFF5A7A : 0xFFB0C0) : ET[e], (1.0f - q2 / 12.0f) * 0.9f * A2)); } }
            flush(dev, n, sx_soft); }
    } break;
    default: break;
    }
}

// ---------------------------------------------------------------- DISTORTION (the scene behind, bent) ----
// The real thing, in fixed function : the render target round the circle is COPIED into a texture (CopyRects, GPU to
// GPU), then drawn back over itself through a polar mesh whose UVs are pushed -- a refraction ring riding out of the
// blast, heat shimmer, water ripples, the scene sucked in. Opaque, and the identity at both edges of the mesh, so it
// cannot show a seam ; drawn FIRST, under every effect.
// The copy target is a D3DPOOL_DEFAULT render-target texture : it is held only while an animation uses it (released
// after 1.5 s idle, on scfx off, on dispose ; forgotten on device loss, rule 4). Every step is checked and logged ; a
// failure retries 3 times 0.5 s apart, then the distortion is OFF (logged) until the next device -- and the effects
// fall back to their drawn waves.
u32 sx_dTex = 0, sx_dSurf = 0; int sx_dTS = 0; u32 sx_dFmt = 0; int sx_dFail = 0; bool sx_dOff = false;
u32 sx_dNext = 0, sx_dUsed = 0;
Vtx sx_dm[64 * 10 * 6];
void d_rel(u32 o) { auto f = vmethod<u32(__stdcall*)(u32)>(o, 2); if (f) f(o); }
void dist_release() { if (sx_dSurf) d_rel(sx_dSurf); if (sx_dTex) release_texture(sx_dTex); sx_dSurf = sx_dTex = 0; sx_dTS = 0; }
bool dist_fail(const char* step, long hr) {
    ++sx_dFail; dist_release();
    if (sx_dFail >= 3) { sx_dOff = true; windower::debug::log("SCFX distort OFF : %s failed hr=0x%08X (3 tries) -- drawn waves instead until the next device", step, (unsigned)hr); }
    else { sx_dNext = (GetTickCount() + 500u) | 1u; windower::debug::log("SCFX distort : %s failed hr=0x%08X (try %d/3)", step, (unsigned)hr, sx_dFail); }
    return false;
}
// the current render target (AddRef'd : the caller releases it) and the copy texture, made to match
bool dist_ensure(u32 dev, int TS, u32& rt, int& W, int& H) {
    rt = 0;
    if (sx_dOff || (sx_dNext && (int)(GetTickCount() - sx_dNext) < 0)) return false;
    auto getrt = vmethod<long(__stdcall*)(u32, u32*)>(dev, 32);   // GetRenderTarget
    if (!getrt) return false;
    u32 srf = 0; long hr = getrt(dev, &srf);
    if (hr < 0 || !valid_ptr(srf)) return dist_fail("GetRenderTarget", hr);
    u32 desc[8] = { 0 }; auto gd = vmethod<long(__stdcall*)(u32, void*)>(srf, 8);
    if (!gd || (hr = gd(srf, desc)) < 0) { d_rel(srf); return dist_fail("GetDesc", hr); }
    if (desc[5] != 0) { d_rel(srf); sx_dOff = true; windower::debug::log("SCFX distort OFF : the render target is multisampled (%u) -- drawn waves instead", desc[5]); return false; }
    W = (int)desc[6]; H = (int)desc[7];
    if (TS > W || TS > H) { d_rel(srf); return false; }
    if (!sx_dTex || sx_dTS != TS || sx_dFmt != desc[0]) {
        dist_release();
        auto ct = vmethod<long(__stdcall*)(u32, u32, u32, u32, u32, u32, u32, u32*)>(dev, 20);   // CreateTexture
        u32 tx = 0; hr = ct ? ct(dev, (u32)TS, (u32)TS, 1, 1 /* RENDERTARGET */, desc[0], 0 /* POOL_DEFAULT */, &tx) : -1;
        if (hr < 0 || !valid_ptr(tx)) { d_rel(srf); return dist_fail("CreateTexture", hr); }
        auto gs = vmethod<long(__stdcall*)(u32, u32, u32*)>(tx, 15);   // GetSurfaceLevel
        u32 ts = 0; hr = gs ? gs(tx, 0, &ts) : -1;
        if (hr < 0 || !valid_ptr(ts)) { release_texture(tx); d_rel(srf); return dist_fail("GetSurfaceLevel", hr); }
        sx_dTex = tx; sx_dSurf = ts; sx_dTS = TS; sx_dFmt = desc[0];
        windower::debug::log("SCFX distort ready : %dx%d copy, format %u, target %dx%d", TS, TS, desc[0], W, H);
    }
    rt = srf; return true;
}
struct Warp { float shR, shW, shA, rip, pull, swirl, haze, bump; };
// what bends the scene, per skillchain, at this moment
Warp warp_of(int P, float bt, float t, float R, float k) {
    Warp w = {};
    if (bt < 0.7f) {   // the common start : the refraction ring of the sphere's explosion
        const float u = bt / 0.7f, ex = 1.0f - (1.0f - u) * (1.0f - u) * (1.0f - u);
        w.shR = R + ex * R * 2.0f * REACH; w.shW = (6.0f + 10.0f * u) * k; w.shA = 8.0f * k * (1.0f - u) * (sx_lvf < 1.0f ? 0.9f : 1.2f);
    }
    switch (P) {
    case SCP_Liquefaction: w.haze = 1.6f; break;                                     // heat shimmer
    case SCP_Impaction:
        if (bt > 0.8f) { const float sp = fmodf(bt - 0.8f, 1.1f); if (sp < 0.35f) { const float v = sp / 0.35f;   // each shock
            w.shR = R + v * R * 1.2f; w.shW = (5.0f + 6.0f * v) * k; w.shA = 5.0f * k * (1.0f - v); } }
        break;
    case SCP_Detonation:
        if (bt > 0.6f) { const float ph = fmodf(bt - 0.6f, 1.6f) / 1.6f;
            if (ph < 0.65f) { const float g = ph / 0.65f; w.pull = 0.10f * g * g; }   // the air drawn in
            else { const float r = (ph - 0.65f) / 0.35f, e = 1.0f - (1.0f - r) * (1.0f - r); w.shR = R + e * R * 1.6f; w.shW = (6.0f + 8.0f * r) * k; w.shA = 10.0f * k * (1.0f - r); } }
        break;
    case SCP_Reverberation: if (sx_rv != 2) w.rip = 2.0f * k * (bt / 0.5f < 1.0f ? bt / 0.5f : 1.0f); break;   // water rings (C : none -- its crests carry the propagation)
    case SCP_Induration: { const float x = bt - 0.2f < 0.0f ? 0.0f : (bt - 0.2f > 1.4f ? 1.4f : bt - 0.2f), fz = x / 1.4f; w.rip = 1.4f * k * (1.0f - fz * fz * (3.0f - 2.0f * fz)); } break;   // liquid, then still
    case SCP_Compression: { const float cb = bt - 0.4f;
        w.pull = 0.13f * (cb / 0.6f < 0.0f ? 0.0f : (cb / 0.6f > 1.0f ? 1.0f : cb / 0.6f)); } break;   // then the scene sucked in
    case SCP_Transfixion: w.haze = 0.7f; break;
    case SCP_Fusion: w.haze = 1.6f; break;
    case SCP_Fragmentation:
        w.swirl = 0.05f * sinf(t * 2.0f); w.bump = 1.0f;                                      // the gale ; the scene torn where it is struck
        if (bt > 0.6f) { const float ph = fmodf(bt - 0.6f, 1.6f) / 1.6f; if (ph < 0.22f) { const float r = ph / 0.22f, e = 1.0f - (1.0f - r) * (1.0f - r);   // the thunderclap
            w.shR = R + e * R * 1.7f; w.shW = (6.0f + 8.0f * r) * k; w.shA = 10.0f * k * (1.0f - r); } }
        break;
    case SCP_Distortion: w.swirl = 0.12f * sinf(t * 1.7f); w.rip = 0.9f * k; w.bump = 0.9f; break;   // wet, the ice shoving it, never settled
    case SCP_Gravitation: w.pull = 0.08f; w.swirl = 0.05f * sinf(t * 0.8f); break;
    case SCP_Light: case SCP_Radiance: w.haze = 0.8f; break;
    case SCP_Darkness: case SCP_Umbra: w.pull = 0.07f; w.swirl = 0.12f * sinf(t * 0.9f); break;
    default: break;
    }
    return w;
}
// copy the area round (CX,CY) and draw it back bent ; false = not available (draw the waves instead)
bool distort(u32 dev, float CX, float CY, float R, float k, float t, const Warp& w) {
    if (w.shA == 0.0f && w.rip == 0.0f && w.pull == 0.0f && w.swirl == 0.0f && w.haze == 0.0f && w.bump == 0.0f) return sx_dTex != 0 || !sx_dOff;
    const float RdW = R * (1.0f + 2.3f * REACH), Rd = RdW < 250.0f ? RdW : 250.0f, Rin = R * 0.85f < Rd * 0.6f ? R * 0.85f : Rd * 0.6f;   // capped : 512 px of copy, at any size
    const int TS = 512;
    u32 rt; int W, H; if (!dist_ensure(dev, TS, rt, W, H)) return false;
    int x0 = (int)CX - TS / 2, y0 = (int)CY - TS / 2;
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x0 > W - TS) x0 = W - TS; if (y0 > H - TS) y0 = H - TS;
    RECT rc = { x0, y0, x0 + TS, y0 + TS }; POINT pt = { 0, 0 };
    auto cr = vmethod<long(__stdcall*)(u32, u32, const RECT*, u32, u32, const POINT*)>(dev, 28);   // CopyRects
    const long hr = cr ? cr(dev, rt, &rc, 1, sx_dSurf, &pt) : -1;
    d_rel(rt);
    if (hr < 0) return dist_fail("CopyRects", hr);
    sx_dFail = 0; sx_dUsed = GetTickCount();
    const int NA = 64, NJ = 10; const float inv = 1.0f / TS;
    static float vx[NJ + 1][NA + 1], vy[NJ + 1][NA + 1], vu[NJ + 1][NA + 1], vv[NJ + 1][NA + 1];
    for (int j = 0; j <= NJ; ++j) {
        const float r = Rin + (Rd - Rin) * j / NJ, q = (float)j / NJ;
        const float win = (q < 0.25f ? q / 0.25f : 1.0f) * (q > 0.6f ? (1.0f - q) / 0.4f : 1.0f), wn = win * win * (3.0f - 2.0f * win);   // 0 at both edges : no seam
        for (int i = 0; i <= NA; ++i) {
            const float th = i * TAU / NA; float dr = 0.0f, dth = 0.0f;
            if (w.shA != 0.0f) { const float x = (r - w.shR) / w.shW; dr += w.shA * x * expf(-x * x) * 2.33f; }   // a lens ring
            if (w.rip != 0.0f) dr += w.rip * sinf(r / (5.0f * k) - t * 7.0f);
            if (w.pull != 0.0f) dr += w.pull * (Rd - r);                                                          // sampled further out : drawn in
            if (w.swirl != 0.0f) dth += w.swirl * (1.0f - q);
            if (w.bump != 0.0f) dr -= w.bump * bumpf(th) * (1.0f - q);   // sampled from nearer in : pushed OUT
            if (w.haze != 0.0f) { dr += w.haze * k * sinf(th * 9.0f + t * 4.0f + r * 0.08f); dth += w.haze * 0.004f * sinf(th * 5.0f - t * 3.0f + r * 0.05f); }
            dr *= wn; dth *= wn;
            const float rs = r + dr, ts2 = th + dth;
            vx[j][i] = CX + cosf(th) * r; vy[j][i] = CY + sinf(th) * r;
            vu[j][i] = (CX + cosf(ts2) * rs - x0) * inv; vv[j][i] = (CY + sinf(ts2) * rs - y0) * inv;
        }
    }
    int n = 0;
    for (int j = 0; j < NJ; ++j) for (int i = 0; i < NA; ++i) {
        static const int cj[6] = { 0, 0, 1, 0, 1, 1 }, ci[6] = { 0, 1, 1, 0, 1, 0 };
        for (int e = 0; e < 6; ++e) { const int a = j + cj[e], b = i + ci[e]; Vtx& o = sx_dm[n++];
            o.x = vx[a][b] - 0.5f; o.y = vy[a][b] - 0.5f; o.z = 0; o.rhw = 1; o.color = 0xFFFFFFFFu; o.u = vu[a][b]; o.v = vv[a][b]; }
    }
    fx_state(dev, sx_dTex, false, true);
    dSetRS(dev, D3DRS_ALPHABLENDENABLE, 0);
    dSetTSS(dev, 0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP); dSetTSS(dev, 0, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
    fdraw(dev, D3DPT_TRIANGLELIST, n / 3, sx_dm, sizeof(Vtx));
    dSetRS(dev, D3DRS_ALPHABLENDENABLE, 1); sx_fs = 0;   // the next fx_state sets everything again
    return true;
}

// ---------------------------------------------------------------- the clock / scene ----
// what a skillchain spawns : its elements (each particle the next one's shape), its level's weight, fewer particles
// the bigger it is drawn (rel = its size relative to the default : the same pixels to fill)
void sim_setup(int P, float rel) {
    static const int CELLE[8] = { 0, 5, 2, 3, 1, 4, 6, 7 };   // SCElem (Fire Ice Wind Earth Lightning Water Light Dark) -> ember flake wisp dust spark bubble sparkle smoke
    const SCInfo& si = sc_info(P); sx_nels = si.nElem < 1 ? 1 : (si.nElem > 4 ? 4 : si.nElem);
    for (int i = 0; i < sx_nels; ++i) sx_els[i] = CELLE[si.elem[i] & 7];
    sx_lvf = si.lvl <= 1 ? 0.55f : si.lvl == 2 ? 0.9f : si.lvl == 3 ? 1.2f : 1.4f;
    sx_pf  = si.lvl <= 1 ? 0.5f : si.lvl == 2 ? 0.55f : si.lvl == 3 ? 0.6f : 0.65f;
    const float zr = rel > 0.01f ? 1.0f / rel : 1.0f, zf = zr * zr; sx_pf *= zf > 1.0f ? 1.0f : (zf < 0.15f ? 0.15f : zf);
}
// one step of it : the blast as the chain forms (prevBt < 0 <= bt), Scission's cut sparks, the property's own
// emission while the window lasts (bt < burstEnd), the particles moving
void sim_step(int P, float prevBt, float bt, float dt, float CX, float CY, float R, float k, float burstEnd) {
    if (prevBt < 0.0f && bt >= 0.0f && bt < 0.6f && lq_new(P)) {   // Liquefaction B's formation : embers rising off it, a few drops of lava thrown out
        for (int i = 0; i < (int)(40 * sx_pf); ++i) { const float a = rnd() * TAU, r0 = R + (2.0f + rnd() * 8.0f) * k, sp = (20.0f + rnd() * 40.0f) * k;
            spawn(PM_OUT, CX + cosf(a) * r0, CY + sinf(a) * r0, cosf(a) * sp, sinf(a) * sp * 0.5f - (20.0f + rnd() * 25.0f) * k, 1.2f + rnd() * 0.8f, (1.6f + rnd() * 1.3f) * k); }
        for (int i = 0; i < 8; ++i) { const float a = -PI * 0.5f + (rnd() - 0.5f) * PI * 1.4f, sp = (50.0f + rnd() * 40.0f) * k;
            spawn(PM_DROP, CX + cosf(a) * (R + 8.0f * k), CY + sinf(a) * (R + 8.0f * k), cosf(a) * sp, sinf(a) * sp - 30.0f * k, 1.1f + rnd() * 0.4f, (4.2f + rnd() * 1.8f) * k); }
    } else
    if (prevBt < 0.0f && bt >= 0.0f && bt < 0.6f && P != SCP_Gravitation && !water_variant(P) && !im_new(P)) {   // the blast : particles flung out of the whole circle (Impaction B : its own, fewer)
        for (int i = 0; i < (int)(110 * sx_pf); ++i) {
            const float a = rnd() * TAU, sp = (40.0f + rnd() * 110.0f) * k, r0 = R * (0.7f + rnd() * 0.4f);
            const bool in = P == SCP_Compression;   // Compression : the blast falls IN
            spawn(P == SCP_Liquefaction ? PM_EMBER : (in ? PM_SWIRL : PM_OUT), CX + cosf(a) * r0, CY + sinf(a) * r0, cosf(a) * sp, sinf(a) * sp - (P == SCP_Liquefaction ? 20.0f * k : 0.0f),
                  0.7f + rnd() * 0.9f, (2.0f + rnd() * 3.0f) * k, a, R * (2.0f + rnd() * 0.5f), 0.0f);
        }
    }
    if (P == SCP_Scission && bt > 0.0f && bt < burstEnd)   // each cut throws sparks off its edge, sideways
        for (int i = 0; i < 8; ++i) { const float ts = slash_time(i); if (!(bt >= ts && prevBt < ts)) continue;
            const float ang = slash_angle(i), dx = cosf(ang), dy = sinf(ang);
            for (int q = 0; q < 40; ++q) { const float s2 = (rnd() * 2.0f - 1.0f) * 0.9f, sgn = rnd() < 0.5f ? -1.0f : 1.0f, v = (40.0f + rnd() * 90.0f) * k;
                spawn(PM_OUT, CX + dx * R * 2.5f * s2, CY + dy * R * 2.5f * s2, (-dy * sgn + dx * (rnd() - 0.5f)) * v, (dx * sgn + dy * (rnd() - 0.5f)) * v, 0.35f + rnd() * 0.3f, (1.0f + rnd() * 1.2f) * k); }
            for (int q = 0; q < 8; ++q) { const float end = q & 1 ? 1.0f : -1.0f, sgn = q & 2 ? 1.0f : -1.0f, v = (60.0f + rnd() * 70.0f) * k;   // CHIPS of the stone knocked off where the blade crosses the sphere
                spawn(PM_ROCK, CX + dx * R * end, CY + dy * R * end, (-dy * sgn + dx * end * 0.4f) * v, (dx * sgn + dy * end * 0.4f) * v - 30.0f * k, 0.9f + rnd() * 0.3f, (1.3f + rnd() * 1.1f) * k); } }
    if (bt > 0.0f && bt < burstEnd) emit(P, dt, bt, CX, CY, R, k);
    step_parts(dt, CX, CY, R, k, bt >= burstEnd);
}
// the demo's clock : its scripted scene, then the same simulation
void update(float dt, float CX, float CY, float R, float k) {
    sx_R = R;
    float prev = sx_t;
    sx_t += dt;
    if (sx_loop && sx_t > FIRE_AT + (sx_all ? 5.0f : T_BURST)) {   // loop : the animation alone ; with all, 5 s each, straight into the next
        sx_np = 0; if (sx_all) sx_scene = (sx_scene + 1) % NSCENES;
        prev = FIRE_AT - 0.0001f; sx_t = FIRE_AT + 0.0001f;   // this very frame is the next blast
    }
    if (sx_t > T_END) { sx_t = 0.0f; sx_np = 0; if (sx_all) sx_scene = (sx_scene + 1) % NSCENES; return; }
    const int P = SCENES[sx_scene].prop;
    sx_clock = sx_t; sx_bt = sx_t - FIRE_AT;
    sim_setup(P, sx_zoom / 1.25f);
    sim_step(P, prev - FIRE_AT, sx_t - FIRE_AT, dt, CX, CY, R, k, T_BURST);
}

// ow : a thin outline, NOT grown with the size (it smeared at size 4) ; 0 = none
void text(Font* fn, u32 dev, float cx, float cy, const char* s, float sz, u32 col, float ow = 1.6f) { sx_fs = 0; fn->begin(dev); fn->draw_c(dev, cx, cy, s, sz, col, 0xFF000000u, ow); }

// ---------------------------------------------------------------- the MEDALLION (the centre : state, time, TP) ----
// A lens of the chain's own colour : dark in the middle where the text sits, lit toward its edge, a glass sheen on
// top. Round it, the window as a COUNTDOWN ARC that empties clockwise from 12 o'clock (or fills, red, while it is
// too early), a bright head on it. Inside : the state on a coloured pill, the seconds big with a glow of the state
// colour, TP as a small gauge.
void radial_disc(u32 dev, float cx, float cy, const float* rs, const u32* cs, int nr) {   // concentric bands, colour per radius
    const int NS = 64; static VtxC v[2 * (NS + 1)];
    static float tc[NS + 1], ts[NS + 1]; static bool tok = false;
    if (!tok) { for (int i = 0; i <= NS; ++i) { tc[i] = cosf(i * TAU / NS); ts[i] = sinf(i * TAU / NS); } tok = true; }
    cqs(dev);
    for (int b = 0; b < nr; ++b) {
        const float r0 = b ? rs[b - 1] : 0.0f, r1 = rs[b]; const u32 c0 = b ? cs[b - 1] : cs[0], c1 = cs[b];
        for (int i = 0; i <= NS; ++i) { VtxC& a = v[2 * i]; VtxC& o = v[2 * i + 1];
            a.x = cx + tc[i] * r0 - 0.5f; a.y = cy + ts[i] * r0 - 0.5f; o.x = cx + tc[i] * r1 - 0.5f; o.y = cy + ts[i] * r1 - 0.5f;
            a.z = o.z = 0; a.rhw = o.rhw = 1; a.color = c0; o.color = c1; }
        fdraw(dev, D3DPT_TRIANGLESTRIP, 2 * NS, v, sizeof(VtxC));
    }
}
void medallion_base(u32 dev, float CX, float CY, float R, float k, float t, u32 tint, float frac, u32 st) {
    // THE CENTRE (2026-11-23 : "too raw, too sharp, too simple") : no longer a hard black coin with a drawn line round
    // it. A deep well whose EDGE DISSOLVES into the crown (a long feather, no stroke), the crown's own light BLEEDING
    // INWARD over its rim (additive, the chain's colour, breathing slowly), and an arc that GLOWS. The middle stays
    // calm and dark : the seconds are read there.
    const float rb = R - 4.0f * k;
    const float rs[5] = { rb * 0.55f, rb * 0.82f, rb - 3.0f * k, rb + 0.5f * k, rb + 4.5f * k };
    const u32 cs[5] = { C(lerp(0x04060A, tint, 0.04f), 0.97f), C(lerp(0x060810, tint, 0.08f), 0.96f), C(lerp(0x080C14, tint, 0.20f), 0.95f),
                        C(lerp(0x0A0E16, tint, 0.35f), 0.80f), C(lerp(0x0A0E16, tint, 0.35f), 0.0f) };
    radial_disc(dev, CX, CY, rs, cs, 5);
    {   // the crown's light bleeding in over the rim : additive, the chain's colour, a slow breath
        const float br = 0.85f + 0.15f * sinf(t * 1.7f);
        const float gr[4] = { rb - 11.0f * k, rb - 4.0f * k, rb - 0.5f * k, rb + 3.0f * k };
        const u32 gc[4] = { C(tint, 0.0f), C(tint, 0.10f * br), C(lerp(tint, 0xFFFFFF, 0.35f), 0.32f * br), C(tint, 0.0f) };
        blend(dev, true); radial_disc(dev, CX, CY, gr, gc, 4); blend(dev, false);
    }
    cqs(dev);
    const float r0 = R - 10.0f * k, r1 = R - 7.2f * k, a0 = -PI * 0.5f, f = frac < 0.0f ? 0.0f : (frac > 1.0f ? 1.0f : frac), a1 = a0 + TAU * f;
    ring_sector(dev, CX, CY, r0, r1, 0.0f, TAU, C(0x000000, 0.40f));                                           // the track
    if (f > 0.002f) {
        blend(dev, true); ring_sector(dev, CX, CY, r0 - 1.6f * k, r1 + 1.6f * k, a0, a1, C(st, 0.22f)); blend(dev, false);   // its glow
        ring_sector(dev, CX, CY, r0, r1, a0, a1, C(st, 0.95f));                                                 // what is left
        const float hr = (r0 + r1) * 0.5f, hx = CX + cosf(a1) * hr, hy = CY + sinf(a1) * hr;
        bloom(dev, hx, hy, 6.0f * k, C(st, 0.85f)); bloom(dev, hx, hy, 2.6f * k, C(0xFFFFFF, 0.9f + 0.1f * sinf(t * 12.0f)));   // its head
    }
    fx_end(dev);
}
// THE CENTRE MADE OF WATER (Reverberation F, 2026-11-23 : "the sphere must marry the matter round it") : no object laid
// in the middle of the water any more -- the centre IS the water, seen deeper. No border : the deep-blue well fades into
// the crown, the crown's own caustics run on inward over the rim and die out in the depth, and the time left is a
// FILAMENT of light on the water (no black rail under it). The middle stays dark and calm : the seconds are read there.
// the matter's palette for its centre : the depth (mid, rim), the texture running in (hot, mid), the time's faint trace
struct MatterRim { u32 deepMid, deepRim, cHot, cMid, cCold, trace; };
void medallion_matter(u32 dev, float CX, float CY, float R, float k, float t, float frac, u32 st, const MatterRim& M) {
    const float rb = R - 4.0f * k;
    {   // the depth : near black in the middle, deep blue toward the rim, translucent at it -- the crown shows through
        const float rs[5] = { rb * 0.55f, rb * 0.78f, rb - 6.0f * k, rb, rb + 3.0f * k };
        const u32 cs[5] = { C(lerp(0x020408, M.deepMid, 0.25f), 0.97f), C(lerp(0x020408, M.deepMid, 0.55f), 0.96f), C(M.deepMid, 0.90f), C(M.deepRim, 0.55f), C(M.deepRim, 0.0f) };
        radial_disc(dev, CX, CY, rs, cs, 5);
    }
    if (sx_caus) {   // the crown's caustics running on inward, dimming with the depth (same mapping as the crown : they continue)
        RingFx f = { -4, -9, -17, 0.8f, 5, 1.5f, M.cHot, M.cMid, M.cCold, 0.34f, 0.16f, 4, -0.03f, 0.04f, 0.1f, false };
        sx_ringTex = sx_ringTex ? sx_ringTex : sx_caus; sx_vScale = 0.45f;   // the caller may choose the texture (Impaction : its filaments)
        noise_ring(dev, CX, CY, R, k, t, f);
        sx_ringTex = 0; sx_vScale = 1.0f; fx_end(dev);
    }
    cqs(dev);
    // the time left : a FILAMENT of light on the water, its trace faint where it has gone
    const float ra = R - 8.6f * k, a0 = -PI * 0.5f, f2 = frac < 0.0f ? 0.0f : (frac > 1.0f ? 1.0f : frac), a1 = a0 + TAU * f2;
    blend(dev, true);
    ring_sector(dev, CX, CY, ra - 0.5f * k, ra + 0.5f * k, 0.0f, TAU, C(M.trace, 0.18f));   // the trace : a faint line of water, no black band
    blend(dev, false);
    if (f2 > 0.002f) {   // NORMAL blend, not additive : added onto the blue, red turned pink and green cyan -- the state's colour must stay exact
        ring_sector(dev, CX, CY, ra - 2.4f * k, ra + 2.4f * k, a0, a1, C(st, 0.22f));            // its glow
        ring_sector(dev, CX, CY, ra - 0.9f * k, ra + 0.9f * k, a0, a1, C(st, 1.0f));             // the filament
    }
    if (f2 > 0.002f) {
        const float hx = CX + cosf(a1) * ra, hy = CY + sinf(a1) * ra;
        bloom(dev, hx, hy, 6.0f * k, C(st, 0.85f)); bloom(dev, hx, hy, 2.4f * k, C(0xFFFFFF, 0.9f + 0.1f * sinf(t * 12.0f)));   // its head
    }
    fx_end(dev);
}
void medallion_text(u32 dev, Font* fn, float CX, float CY, float k, const char* label, u32 labelCol, const char* num, u32 numCol, int tp, bool showTimer, bool showTP) {
    // READABILITY FIRST : plain text on the black centre, no outline needed, nothing lit behind a letter. The Timer
    // text element's typography (size / colour override) ; the state colours otherwise.
    const TextStyle& ts = ui_config().scText[SC_TIMER];
    if (showTimer) {
        text(fn, dev, CX, CY - 17.0f * k, label, te_sz(ts, 11.0f * k), te_col(ts, C(labelCol, 1.0f)), 0.0f);
        text(fn, dev, CX, CY + (showTP ? 1.0f : 5.0f) * k, num, te_sz(ts, 25.0f * k), te_col(ts, C(numCol, 1.0f)), 0.0f);
    }
    if (showTP) {
        const float ty = showTimer ? 19.0f * k : 0.0f;
        char b[16]; _snprintf(b, sizeof(b), "TP %d", tp);
        text(fn, dev, CX, CY + ty, b, te_sz(ts, 10.5f * k), tp >= 1000 ? 0xFFFFD76Au : 0xFF9AA3B2u, 0.0f);
        const float w = 26.0f * k, h = 2.0f * k, x = snap(CX - w * 0.5f), y = snap(CY + ty + 7.0f * k), f = tp >= 1000 ? 1.0f : tp / 1000.0f;
        const u32 fc = tp >= 1000 ? 0xFFD76A : 0x8FA6C8;
        cqs(dev); rrect(dev, x, y, w, h, h * 0.5f, C(0x000000, 0.8f), C(0x000000, 0.8f));
        if (f > 0.02f) rrect(dev, x, y, snap(w * f), h, h * 0.5f, C(fc, 1.0f), C(fc, 1.0f));
    }
}
// the chain's NAME, in its colour on a glow, a line of light under it, and its level as pips (filled = the level)
// the TITLE PLATE's shape : a cartouche with pointed ends (45 deg), cut in horizontal bands at the level fractions
// fy[] (-1 top .. +1 bottom, 0 included), each level its colour. mode 0 : flat colour ; 1 : the NOISE on two stages,
// additive, scrolling (the chain's matter flowing through the plate) ; 2 : the noise stretched flat, additive (brushed
// metal streaks)
void plate_bands(u32 dev, float x0, float x1, float yy, float hh, const float* fy, const u32* col, int nl, int mode, float t, float spd) {
    static V2 vb[96]; static VtxC vc[96]; int n = 0;
    for (int l = 0; l + 1 < nl && n + 6 <= 96; ++l) {
        const float ya = yy + fy[l] * hh, yb = yy + fy[l + 1] * hh, da = fabsf(ya - yy), db = fabsf(yb - yy);
        const float X[4] = { x0 + da, x1 - da, x1 - db, x0 + db }, Y[4] = { ya, ya, yb, yb };
        const u32 Cc[4] = { col[l], col[l], col[l + 1], col[l + 1] };
        static const int ix[6] = { 0, 1, 2, 0, 2, 3 };
        for (int j = 0; j < 6; ++j) { const int q = ix[j];
            if (mode == 0) { VtxC& v = vc[n + j]; v.x = X[q] - 0.5f; v.y = Y[q] - 0.5f; v.z = 0; v.rhw = 1; v.color = Cc[q]; }
            else { V2& v = vb[n + j]; v.x = X[q] - 0.5f; v.y = Y[q] - 0.5f; v.z = 0; v.rhw = 1; v.c = Cc[q];
                   if (mode == 1) { v.u0 = X[q] * 0.010f + t * spd; v.v0 = Y[q] * 0.030f; v.u1 = X[q] * 0.007f - t * spd * 0.6f; v.v1 = Y[q] * 0.045f + 0.3f; }
                   else           { v.u0 = X[q] * 0.0015f; v.v0 = Y[q] * 0.22f; v.u1 = X[q] * 0.0011f + 0.4f; v.v1 = Y[q] * 0.31f; } } }
        n += 6;
    }
    if (n < 3) return;
    if (mode == 0) { cqs(dev); fdraw(dev, D3DPT_TRIANGLELIST, n / 3, vc, sizeof(VtxC)); }
    else if (sx_noise) { fx_state(dev, sx_noise, true, true); fdraw(dev, D3DPT_TRIANGLELIST, n / 3, vb, sizeof(V2)); fx_end(dev); }
}
// the name in spaced capitals, centred on CX ; `engrave` : a light line under each letter (cut into metal)
void plate_name(u32 dev, Font* fn, float CX, float yy, float z, const char* name, u32 col, float ow, bool engrave) {
    const float trk = z * 0.14f; float tw = 0.0f; char ch[2] = { 0, 0 };
    for (const char* c = name; *c; ++c) { ch[0] = *c; tw += fn->measure(ch, z) + (c[1] ? trk : 0.0f); }
    sx_fs = 0; fn->begin(dev);
    for (int pass = engrave ? 0 : 1; pass < 2; ++pass) {
        float x = CX - tw * 0.5f;
        for (const char* c = name; *c; ++c) { ch[0] = *c;
            if (pass == 0) fn->draw_lc(dev, x, yy + 1.0f, ch, z, 0x80FFFFFFu, 0, 0.0f);   // the cut's lit lower lip
            else           fn->draw_lc(dev, x, yy, ch, z, col, engrave ? 0 : 0xFF000000u, engrave ? 0.0f : ow);
            x += fn->measure(ch, z) + trk; }
    }
}
// the TITLE PLATE. A skillchain's : dark, its MATTER flowing through it (the noise in its colours, darker in the
// middle where the name sits), lit edges pulsing in its colour, a sheen every 3.5 s, the level as gems hanging from
// it. An opening move's (Q = 0) : BRUSHED STEEL -- light-to-dark bands, streaks, a bevel, a rivet at each end, the
// name ENGRAVED.
void title_plate(u32 dev, Font* fn, float CX, float y, float k, float t, const char* name, int lvl, const Pal* Q, u32 nameCol, float pop) {
    const TextStyle& ts = ui_config().scText[SC_TITLE];
    const float z = te_sz(ts, 11.0f * k) * pop, trk = z * 0.14f;
    float tw = 0.0f; char ch[2] = { 0, 0 };
    for (const char* c = name; *c; ++c) { ch[0] = *c; tw += fn->measure(ch, z) + (c[1] ? trk : 0.0f); }
    const float H = snap(z * 1.8f), W = snap(tw + z * 2.6f + H), x0 = snap(CX - W * 0.5f), x1 = x0 + W, hh = H * 0.5f, yy = snap(y);
    if (Q) {   // ===== a skillchain's : its matter =====
        { const float fy[3] = { -1.0f, 0.0f, 1.0f }; const u32 cl[3] = { C(0x05070B, 0.92f), C(0x05070B, 0.92f), C(0x05070B, 0.92f) };
          plate_bands(dev, x0, x1, yy, hh, fy, cl, 3, 0, t, 0.0f); }
        { const float fy[5] = { -1.0f, -0.45f, 0.0f, 0.45f, 1.0f };
          const u32 cl[5] = { C(Q->hot, 0.55f), C(Q->mid, 0.42f), C(Q->cold, 0.16f), C(Q->mid, 0.42f), C(Q->hot, 0.5f) };
          plate_bands(dev, x0, x1, yy, hh, fy, cl, 5, 1, t, 0.18f); }   // the matter, flowing
        const u32 ec = lerp(Q->mid, Q->hot, 0.45f); const float pu = 0.75f + 0.25f * sinf(t * 2.6f);
        const float half = (W - H) * 0.5f; const u32 c0 = C(ec, 0.2f), c1 = C(ec, pu);
        cqs(dev);
        grad_quad(dev, x0 + hh, yy - hh, half, 1.0f, c0, c1, c0, c1); grad_quad(dev, x0 + hh + half, yy - hh, half, 1.0f, c1, c0, c1, c0);
        grad_quad(dev, x0 + hh, yy + hh - 1.0f, half, 1.0f, c0, c1, c0, c1); grad_quad(dev, x0 + hh + half, yy + hh - 1.0f, half, 1.0f, c1, c0, c1, c0);
        const u32 ce = C(ec, 0.45f);
        seg_soft(dev, x0, yy, x0 + hh, yy - hh, 1.0f, ce); seg_soft(dev, x0, yy, x0 + hh, yy + hh, 1.0f, ce);
        seg_soft(dev, x1, yy, x1 - hh, yy - hh, 1.0f, ce); seg_soft(dev, x1, yy, x1 - hh, yy + hh, 1.0f, ce);
    } else {   // ===== an opening move's : brushed steel =====
        { const float fy[6] = { -1.0f, -0.55f, -0.1f, 0.0f, 0.4f, 1.0f };
          const u32 cl[6] = { C(0xDDE2EA, 1.0f), C(0xB3BBC7, 1.0f), C(0x8E97A5, 1.0f), C(0x838C9A, 1.0f), C(0x9AA3B0, 1.0f), C(0x5C6472, 1.0f) };
          plate_bands(dev, x0, x1, yy, hh, fy, cl, 6, 0, t, 0.0f); }
        { const float fy[3] = { -1.0f, 0.0f, 1.0f }; const u32 cl[3] = { C(0xFFFFFF, 0.22f), C(0xFFFFFF, 0.16f), C(0xFFFFFF, 0.2f) };
          plate_bands(dev, x0, x1, yy, hh, fy, cl, 3, 2, t, 0.0f); }   // the brushing
        const float half = (W - H) * 0.5f;
        cqs(dev);
        grad_quad(dev, x0 + hh, yy - hh, half, 1.0f, C(0xFFFFFF, 0.5f), C(0xFFFFFF, 0.95f), C(0xFFFFFF, 0.5f), C(0xFFFFFF, 0.95f));   // the bevel : lit top ...
        grad_quad(dev, x0 + hh + half, yy - hh, half, 1.0f, C(0xFFFFFF, 0.95f), C(0xFFFFFF, 0.5f), C(0xFFFFFF, 0.95f), C(0xFFFFFF, 0.5f));
        grad_quad(dev, x0 + hh, yy + hh - 1.0f, W - H, 1.0f, C(0x1A1E26, 0.95f), C(0x1A1E26, 0.95f), C(0x1A1E26, 0.95f), C(0x1A1E26, 0.95f));   // ... dark bottom
        seg_soft(dev, x0, yy, x0 + hh, yy - hh, 1.0f, C(0xF0F4F8, 0.8f)); seg_soft(dev, x1, yy, x1 - hh, yy - hh, 1.0f, C(0xF0F4F8, 0.6f));
        seg_soft(dev, x0, yy, x0 + hh, yy + hh, 1.0f, C(0x2A2F38, 0.9f)); seg_soft(dev, x1, yy, x1 - hh, yy + hh, 1.0f, C(0x2A2F38, 0.9f));
        for (int sd = -1; sd <= 1; sd += 2) {   // a rivet at each end
            const float rx = sd < 0 ? x0 + hh * 0.95f : x1 - hh * 0.95f, rr = 1.7f * k;
            disc(dev, rx, yy, rr, C(0x3A404A, 1.0f)); disc(dev, rx - rr * 0.3f, yy - rr * 0.3f, rr * 0.45f, C(0xF2F5F9, 0.85f));
        }
    }
    {   // the sheen : a soft light crossing the plate, every 3.5 s
        const float ph = fmodf(t, 3.5f) / 1.1f;
        if (ph < 1.0f) { const float sx = x0 + hh + (W - H) * ph; flush(dev, put(0, sx, yy, 1.0f, 0.0f, H * 0.9f, hh * 0.9f, C(0xFFFFFF, (Q ? 0.16f : 0.35f) * sinf(ph * PI))), sx_soft); fx_end(dev); }
    }
    plate_name(dev, fn, CX, yy, z, name, C(nameCol, 1.0f), te_ow(ts, 1.2f), Q == 0);
    if (Q && lvl > 0) {   // the level : gems hanging from the bottom edge, the level lit
        cqs(dev);
        for (int i = 0; i < 4; ++i) {
            const float px = CX + (i - 1.5f) * 7.0f * k, py = yy + hh + 0.5f * k, d = 2.8f * k;
            const float dm[8] = { px, py - d, px + d, py, px, py + d, px - d, py };
            const float dmi[8] = { px, py - d + 1.0f, px + d - 1.0f, py, px, py + d - 1.0f, px - d + 1.0f, py };
            fill_poly_aa(dev, dm, 4, C(0x000000, 0.9f));
            fill_poly_aa(dev, dmi, 4, i < lvl ? C(lerp(Q->hot, 0xFFFFFF, 0.25f), 1.0f) : C(0x3A4250, 0.95f));
        }
    }
    cqs(dev);
}

// ---------------------------------------------------------------- the WS LIST ----
// WHAT CAN FOLLOW, at a glance and out of the way : ONE column split on an axis, one line per move : its NAME left-
// aligned in its column (the WS list text element), and across the axis the skillchain it makes, WRITTEN (its level
// as pips, its name in its colour -- the Property text element -- the elements to burst on). Best level first.
// Where it sits : under / above / left / right of the sphere (scListPos), how far (scListOff) ; its frame = the
// grammar of every box (scListBox : none = a faint wash).
float side_gap(float R, float k, float kl);
// the list's place : 0 under 1 above 2 left 3 right ; 4 = BOTH SIDES (list_draw splits it, each half drawn as 2 / 3)
int sx_posOv = -1;
inline int lpos() { return sx_posOv >= 0 ? sx_posOv : ui_config().scListPos; }
// the element icons a skillchain shows IN THE LIST : 0 when they are switched off (scListElem), else up to 4
inline int list_ne(const SCInfo& si) { return !ui_config().scListElem ? 0 : (si.nElem < 4 ? si.nElem : 4); }
// MEASURE mode (sc_circle_list_rect) : a list style records the rect it would take, frame margin included, and draws nothing
bool sx_meas = false, sx_mok = false; float sx_mx = 0, sx_my = 0, sx_mw = 0, sx_mh = 0;
void sx_mrec(float x, float y, float w, float h, float kl) {
    const UiConfig& U = ui_config(); const float p = U.scListBox.on ? 8.0f * kl + box_inset(U.scListBox) : 10.0f * kl;
    const float x0 = x - p, y0 = y - p, x1 = x + w + p, y1 = y + h + p;
    if (sx_mok) {   // a second half (both sides) : the union
        const float ux0 = sx_mx < x0 ? sx_mx : x0, uy0 = sx_my < y0 ? sx_my : y0;
        const float ux1 = sx_mx + sx_mw > x1 ? sx_mx + sx_mw : x1, uy1 = sx_my + sx_mh > y1 ? sx_my + sx_mh : y1;
        sx_mx = ux0; sx_my = uy0; sx_mw = ux1 - ux0; sx_mh = uy1 - uy0; return; }
    sx_mx = x0; sx_my = y0; sx_mw = x1 - x0; sx_mh = y1 - y0; sx_mok = true;
}
void list_rows(const Frame& f, u32 dev, const ScCircleItem* it, int n, float CX, float CY, float R, float k, float kl) {
    if (n <= 0 || !it) return;
    const UiConfig& U = ui_config();
    Font* fW = te_font(f, U.scText[SC_LIST]); Font* fS = te_font(f, U.scText[SC_PROP]);
    if (!fW || !fS) return;
    const float fzW = te_sz(U.scText[SC_LIST], 10.0f * kl), fzS = te_sz(U.scText[SC_PROP], 10.0f * kl);
    const float gm = U.scListGap < 0.6f ? 0.6f : (U.scListGap > 3.0f ? 3.0f : U.scListGap);
    const float lh = 14.0f * kl * gm, gap = 7.0f * kl;
    const int shown = n > 16 ? 16 : n, lines = shown;   // all of them, never a "+N"
    float maxw = 0.0f, maxs = 0.0f; int maxe = ui_config().scListElem ? 1 : 0;
    for (int i = 0; i < shown; ++i) { const float a = fW->measure(it[i].ws ? it[i].ws : "", fzW), b = fS->measure(scprop_name(it[i].prop), fzS); if (a > maxw) maxw = a; if (b > maxs) maxs = b;
        const int ne = list_ne(sc_info(it[i].prop)); if (ne > maxe) maxe = ne; }
    // the right side's COLUMNS : pips | skillchain name | element icons -- the icons aligned in their own column, the
    // block only as wide as the widest row (no room kept for 4 icons nobody has)
    const float ic = U.scListIcon < 0.5f ? 0.5f : (U.scListIcon > 2.0f ? 2.0f : U.scListIcon);
    const float pipW = 4 * 4.6f * kl + 3.0f * kl, leftW = gap + maxw, iconX0 = gap + pipW + maxs + 5.0f * kl, rightW = iconX0 + maxe * 10.0f * kl * ic;
    const float off = (U.scListOff < -200.0f ? -200.0f : (U.scListOff > 80.0f ? 80.0f : U.scListOff)) * k;   // < 0 : pulled in, over the sphere if wanted
    float axis = CX + (leftW - rightW) * 0.5f, y0;   // under / above : the WHOLE block centred on the sphere, not its axis
    switch (lpos()) {
    case 1:  y0 = CY - R - 68.0f * k - off - (lines - 1) * lh; break;                                  // above (over the title)
    case 2:  axis = CX - side_gap(R, k, kl) - 2.0f * kl - off - rightW; y0 = CY - (lines - 1) * lh * 0.5f; break;    // left : past the element icons
    case 3:  axis = CX + side_gap(R, k, kl) + 2.0f * kl + off + leftW;  y0 = CY - (lines - 1) * lh * 0.5f; break;    // right
    default: y0 = CY + R + 40.0f * k + 12.0f * kl + off; break;                                       // under
    }
    axis = snap(axis);
    if (sx_meas) { sx_mrec(axis - leftW, y0 - lh * 0.5f, leftW + rightW, lines * lh, kl); return; }
    const float padX = 10.0f * kl, padY = 8.0f * kl;
    if (U.scListBox.on) {   // its frame : the same box as every module (Same as Party, border, transparency, theme)
        const float ins = box_inset(U.scListBox);
        cqs(dev); draw_themed_box(dev, f.skin, snap(axis - leftW - padX - ins), snap(y0 - lh * 0.5f - padY - ins), snap(leftW + rightW + 2.0f * (padX + ins)), snap(lines * lh + 2.0f * (padY + ins)), U.scListBox, 1.0f, kl * 1.08f);
        sx_fs = 0;
    } else {                // no frame : a faint dark wash under it all, readable over a bright scene
        flush(dev, put(0, axis + (rightW - leftW) * 0.5f, y0 + (lines - 1) * lh * 0.5f, 1.0f, 0.0f, (leftW + rightW) * 0.5f + 30.0f * kl, lines * lh * 0.5f + 12.0f * kl, C(0x000000, 0.45f)), sx_soft, false); fx_end(dev);
    }
    { cqs(dev); const u32 c0 = C(0xFFFFFF, 0.0f), c1 = C(0xFFFFFF, 0.12f);   // the axis : a hairline, fading at both ends
      const float top = snap(y0 - lh * 0.5f), half = lines * lh * 0.5f;
      grad_quad(dev, axis, top, 1.0f, half, c0, c0, c1, c1); grad_quad(dev, axis, top + half, 1.0f, half, c1, c1, c0, c0); }
    const float wx = snap(axis - gap - maxw);
    const u32 wsCol = te_col(U.scText[SC_LIST], 0xFFE8ECF2u);
    for (int i = 0; i < shown; ++i) {
        const ScCircleItem& e = it[i]; const SCInfo& si = sc_info(e.prop); const Pal q = pal(e.prop);
        const float y = y0 + i * lh;
        sx_fs = 0; fW->begin(dev);
        fW->draw_lc(dev, wx, y, e.ws ? e.ws : "", fzW, wsCol, 0xFF000000u, te_ow(U.scText[SC_LIST], 1.1f * kl));   // the move
        cqs(dev);
        float x = axis + gap;
        for (int pp = 0; pp < 4; ++pp) {   // its level : 4 pips, the level lit
            const float px = x + 2.2f * kl + pp * 4.6f * kl, d = 1.9f * kl;
            const float dm[8] = { px, y - d, px + d, y, px, y + d, px - d, y };
            fill_poly_aa(dev, dm, 4, pp < e.lvl ? C(lerp(q.hot, 0xFFFFFF, 0.2f), 1.0f) : C(0x3A4250, 0.8f));
        }
        x += pipW;
        const char* nm = scprop_name(e.prop);
        sx_fs = 0; fS->begin(dev);
        fS->draw_lc(dev, x, y, nm, fzS, te_col(U.scText[SC_PROP], C(lerp(q.mid, 0xFFFFFF, 0.35f), 1.0f)), 0xFF000000u, te_ow(U.scText[SC_PROP], 1.1f * kl));   // the SKILLCHAIN, written
        const int ne2 = list_ne(si);
        for (int el = 0; el < ne2; ++el) icon(dev, si.elem[el], axis + iconX0 + (4.5f + el * 10.0f) * kl * ic, y, 9.5f * kl * ic, 1.0f);   // what it bursts on : its own aligned column
    }
    cqs(dev);
}

// ---------------------------------------------------------------- the other LIST STYLES (scListStyle 1..3) ----
// Same information, other shapes, all COMPACT (no title plates) : 1 the TREE (trunk, bar, columns of skillchains,
// their moves hanging under), 2 the CARDS (small cards in a grid), 3 GROUPED (one line per skillchain : chip + moves).
struct LGroup { int prop, lvl, n; const char* ws[8]; unsigned char oth[8], sk[8]; };   // oth : another weapon's move ; sk : its weapon
int group_items(const ScCircleItem* it, int n, LGroup* g, int maxg) {
    int ng = 0;
    for (int i = 0; i < n; ++i) {
        int f2 = -1; for (int j = 0; j < ng; ++j) if (g[j].prop == it[i].prop) f2 = j;
        if (f2 < 0) { if (ng >= maxg) continue; f2 = ng++; g[f2].prop = it[i].prop; g[f2].lvl = it[i].lvl; g[f2].n = 0; }
        if (it[i].lvl > g[f2].lvl) g[f2].lvl = it[i].lvl;
        if (g[f2].n < 8) {   // the weapon in hand's moves before the others' (stable)
            int at = g[f2].n++;
            if (!it[i].other) while (at > 0 && g[f2].oth[at - 1]) { g[f2].ws[at] = g[f2].ws[at - 1]; g[f2].oth[at] = g[f2].oth[at - 1]; g[f2].sk[at] = g[f2].sk[at - 1]; --at; }
            g[f2].ws[at] = it[i].ws ? it[i].ws : ""; g[f2].oth[at] = it[i].other; g[f2].sk[at] = it[i].wsk;
        }
    }
    for (int i = 1; i < ng; ++i) { const LGroup e = g[i]; int j = i - 1; while (j >= 0 && g[j].lvl < e.lvl) { g[j + 1] = g[j]; --j; } g[j + 1] = e; }
    return ng;
}
// a small plate's size (title_plate at scale kp) : width, height (its hanging level gems not counted)
void plate_size(Font* fn, const char* name, float kp, float& w, float& h) {
    const float z = te_sz(ui_config().scText[SC_TITLE], 11.0f * kp), trk = z * 0.14f; float tw = 0.0f; char ch[2] = { 0, 0 };
    for (const char* c = name; *c; ++c) { ch[0] = *c; tw += fn->measure(ch, z) + (c[1] ? trk : 0.0f); }
    h = snap(z * 1.8f); w = snap(tw + z * 2.6f + h);
}
void upper(const char* s, char* b, int cap) { int i = 0; for (; s[i] && i < cap - 1; ++i) b[i] = (s[i] >= 'a' && s[i] <= 'z') ? (char)(s[i] - 'a' + 'A') : s[i]; b[i] = 0; }
// how far from the sphere's centre a list on its SIDE starts : past the element icons, plus its frame / wash margin
float side_gap(float R, float k, float kl) {
    const UiConfig& U = ui_config();
    return R + 50.0f * k + (U.scListBox.on ? 8.0f * kl + box_inset(U.scListBox) : 14.0f * kl);
}
// where a block of w x h goes round the sphere (scListPos / scListOff) -> its top-left
void block_place(float CX, float CY, float R, float k, float kl, float w, float h, float& x, float& y) {
    const UiConfig& U = ui_config();
    const float off = (U.scListOff < -200.0f ? -200.0f : (U.scListOff > 80.0f ? 80.0f : U.scListOff)) * k;
    switch (lpos()) {
    case 1:  x = CX - w * 0.5f; y = CY - R - 64.0f * k - off - h; break;
    case 2:  x = CX - side_gap(R, k, kl) - off - w; y = CY - h * 0.5f; break;
    case 3:  x = CX + side_gap(R, k, kl) + off; y = CY - h * 0.5f; break;
    default: x = CX - w * 0.5f; y = CY + R + 34.0f * k + off; break;
    }
    x = snap(x); y = snap(y);
}
void block_back(const Frame& f, u32 dev, float x, float y, float w, float h, float kl) {   // the list's frame, or its faint wash
    const UiConfig& U = ui_config();
    if (U.scListBox.on) { const float ins = box_inset(U.scListBox), p = 8.0f * kl + ins;
        cqs(dev); draw_themed_box(dev, f.skin, snap(x - p), snap(y - p), snap(w + 2.0f * p), snap(h + 2.0f * p), U.scListBox, 1.0f, kl * 1.08f); sx_fs = 0; }
    else { flush(dev, put(0, x + w * 0.5f, y + h * 0.5f, 1.0f, 0.0f, w * 0.5f + 30.0f * kl, h * 0.5f + 14.0f * kl, C(0x000000, 0.45f)), sx_soft, false); fx_end(dev); }
}
void elem_icons(u32 dev, int prop, float x, float y, float kl, float sz) {   // a skillchain's burst elements, left to right from x
    const SCInfo& si = sc_info(prop); const int ne = list_ne(si);
    for (int el = 0; el < ne; ++el) icon(dev, si.elem[el], x + (sz * 0.5f + el * (sz + 1.0f * kl)), y, sz, 1.0f);
}

// a skillchain's level, compact : only the lit gems (lvl of them), from x, centred on y -> the width taken
float lvl_gems(u32 dev, float x, float y, int lvl, u32 hot, float kl) {
    if (lvl < 1) lvl = 1; if (lvl > 4) lvl = 4;
    for (int pp = 0; pp < lvl; ++pp) { const float px = x + 2.0f * kl + pp * 4.2f * kl, d = 1.8f * kl;
        const float dm[8] = { px, y - d, px + d, y, px, y + d, px - d, y }; fill_poly_aa(dev, dm, 4, C(lerp(hot, 0xFFFFFF, 0.25f), 1.0f)); }
    return lvl * 4.2f * kl + 2.5f * kl;
}
// a skillchain's header on one line : gems | NAME (its colour) | burst icons -> its width
float sc_head_w(Font* fS, float fzS, const LGroup& g, float isz, float kl) {
    const SCInfo& si = sc_info(g.prop); const int ne = list_ne(si);
    const int lv = g.lvl < 1 ? 1 : (g.lvl > 4 ? 4 : g.lvl);
    return lv * 4.2f * kl + 2.5f * kl + fS->measure(scprop_name(g.prop), fzS) + 4.0f * kl + ne * (isz + 1.0f * kl);
}
void sc_head(u32 dev, Font* fS, float fzS, const LGroup& g, float x, float y, float isz, float kl, float sizeMul) {
    const UiConfig& U = ui_config(); const Pal q = pal(g.prop);
    cqs(dev); float xx = x + lvl_gems(dev, x, y, g.lvl, q.hot, kl);
    sx_fs = 0; fS->begin(dev);
    fS->draw_lc(dev, xx, y, scprop_name(g.prop), fzS, te_col(U.scText[SC_PROP], C(lerp(q.mid, 0xFFFFFF, 0.35f), 1.0f)), 0xFF000000u, te_ow(U.scText[SC_PROP], 1.1f * kl));
    xx += fS->measure(scprop_name(g.prop), fzS) + 4.0f * kl;
    cqs(dev); elem_icons(dev, g.prop, xx, y, kl, isz); (void)sizeMul;
}

// 1 -- the TREE, compact : a trunk from the sphere, a bar, a column per branch ; on each branch a skillchain (gems,
// name, icons) and its moves hanging under it. Up to 3 columns under / above, one column left / right.
void list_tree(const Frame& f, u32 dev, const ScCircleItem* it, int n, float CX, float CY, float R, float k, float kl, float t) {
    const UiConfig& U = ui_config();
    Font* fS = te_font(f, U.scText[SC_PROP]); Font* fW = te_font(f, U.scText[SC_LIST]); if (!fS || !fW) return;
    LGroup g[8]; const int ng = group_items(it, n, g, 8); if (!ng) return;
    const float fzS = te_sz(U.scText[SC_PROP], 9.0f * kl), fzW = te_sz(U.scText[SC_LIST], 9.0f * kl);
    const float ic = U.scListIcon < 0.5f ? 0.5f : (U.scListIcon > 2.0f ? 2.0f : U.scListIcon);
    const float lh = (fzS > 8.0f * kl * ic ? fzS : 8.0f * kl * ic) * 1.15f + 1.0f * kl, isz = 8.0f * kl * ic, ind = 8.0f * kl, lm = 7.0f * kl, cgap = 12.0f * kl, ngap = 4.0f * kl;
    const bool horiz = lpos() <= 1, leftSide = lpos() == 2;
    const int uc = ui_config().scListCols;   // 0 auto ; left / right : one column (the stems hang off the sphere's side)
    const int cols = !horiz ? 1 : (uc > 0 ? (uc < ng ? uc : ng) : (ng <= 3 ? ng : (ng == 4 ? 2 : 3)));
    float nw[8], colW[4] = { 0, 0, 0, 0 }, colH[4] = { 0, 0, 0, 0 };
    for (int i = 0; i < ng; ++i) {
        float w = sc_head_w(fS, fzS, g[i], isz, kl);
        for (int j = 0; j < g[i].n; ++j) { const float a2 = ind + fW->measure(g[i].ws[j], fzW); if (a2 > w) w = a2; }
        nw[i] = w; const int c = i % cols;
        if (w + lm > colW[c]) colW[c] = w + lm;
        colH[c] += (colH[c] > 0.0f ? ngap : 0.0f) + lh * (1 + g[i].n);
    }
    float bw = 0.0f, bh = 0.0f; for (int c = 0; c < cols; ++c) { bw += colW[c] + (c ? cgap : 0.0f); if (colH[c] > bh) bh = colH[c]; }
    float x, y; block_place(CX, CY, R, k, kl, bw, bh, x, y);
    if (sx_meas) { sx_mrec(x, y, bw, bh, kl); return; }
    block_back(f, dev, x, y, bw, bh, kl);
    const u32 lc = C(0xC8D0DC, 0.30f);
    float colX[4]; { float cx2 = x; for (int c = 0; c < cols; ++c) { colX[c] = cx2; cx2 += colW[c] + cgap; } }
    // the wood : trunk, bar, the columns' stems
    cqs(dev);
    const float barY = snap(lpos() == 1 ? y + bh + 4.0f * kl : y - 4.0f * kl);
    if (horiz) {
        const float lx0 = snap(colX[0] + 2.0f * kl), lx1 = snap(colX[cols - 1] + 2.0f * kl);
        if (cols > 1) grad_quad(dev, lx0, barY, lx1 - lx0 + 1.0f, 1.0f, lc, lc, lc, lc);
        if (lpos() == 0) { const float ty = snap(CY + R + 3.0f * k); if (barY > ty) {   // the trunk, a light running down it
            const float tx = snap(CX); grad_quad(dev, tx, ty, 1.0f, barY - ty, C(0xC8D0DC, 0.0f), C(0xC8D0DC, 0.0f), lc, lc);
            if (tx < lx0 || tx > lx1) { const float a0 = tx < lx0 ? tx : lx1, a1 = tx < lx0 ? lx0 : tx; grad_quad(dev, a0, barY, a1 - a0 + 1.0f, 1.0f, lc, lc, lc, lc); }
            const float ph = fmodf(t * 0.6f, 1.0f); bat_begin(); star(dev, CX, ty + (barY - ty) * ph, 3.5f * kl, C(0xFFFFFF, 0.7f * sinf(ph * PI))); bat_end(dev); cqs(dev); } }
    } else {   // left / right : a short trunk from the sphere's side to the stem
        const float sx0 = snap(leftSide ? CX - R - 48.0f * k : CX + R + 48.0f * k), stx = snap(leftSide ? x + bw + 2.0f * kl : x + 2.0f * kl), cy2 = snap(CY);
        const float a0 = sx0 < stx ? sx0 : stx, a1 = sx0 < stx ? stx : sx0; grad_quad(dev, a0, cy2, a1 - a0 + 1.0f, 1.0f, lc, lc, lc, lc);
    }
    // the branches : each column's stem and ticks, the skillchain, its moves
    float cyc[4] = { y, y, y, y };
    for (int i = 0; i < ng; ++i) {
        const int c = i % cols; const Pal q = pal(g[i].prop);
        const float hy = cyc[c] + lh * 0.5f, stemX = snap(horiz ? colX[c] + 2.0f * kl : (leftSide ? x + bw + 2.0f * kl : x + 2.0f * kl));
        const float hx = horiz || !leftSide ? colX[c] + lm : x + bw - nw[i] - 3.0f * kl;
        cqs(dev);
        {   // the stem down to this branch (from the bar, or from the previous branch)
            const float s0 = snap(horiz ? (cyc[c] == y ? (lpos() == 1 ? hy : barY) : cyc[c] - ngap) : (i == 0 ? (y < CY ? y + lh * 0.5f : CY) : cyc[c] - ngap));
            float s1 = snap(hy); if (horiz && lpos() == 1 && cyc[c] == y) s1 = barY;
            if (!horiz && i == ng - 1 && CY > hy) s1 = snap(CY);
            if (s1 > s0) grad_quad(dev, stemX, s0, 1.0f, s1 - s0 + 1.0f, lc, lc, lc, lc);
            const float t0 = horiz || !leftSide ? stemX : hx + nw[i] + 1.0f * kl, t1 = horiz || !leftSide ? hx - 2.0f * kl : stemX;   // the tick, in its colour
            if (t1 > t0) grad_quad(dev, snap(t0), snap(hy), snap(t1 - t0), 1.0f, lc, C(q.mid, 0.8f), lc, C(q.mid, 0.8f));
        }
        sc_head(dev, fS, fzS, g[i], snap(hx), hy, isz, kl, 1.0f);
        sx_fs = 0; fW->begin(dev);
        for (int j = 0; j < g[i].n; ++j) fW->draw_lc(dev, snap(hx + ind), hy + (j + 1) * lh, g[i].ws[j], fzW, te_col(U.scText[SC_LIST], 0xFFE8ECF2u), 0xFF000000u, te_ow(U.scText[SC_LIST], 1.1f * kl));
        cyc[c] += lh * (1 + g[i].n) + ngap;
    }
    cqs(dev);
}

// 2 -- the CARDS, compact : small cards in a narrow grid (2 a row), the move on top, the skillchain under
// it with its icons ; the level as gems on the top edge, the chain's colour as the card's tint. Best first.
void list_cards(const Frame& f, u32 dev, const ScCircleItem* it, int n, float CX, float CY, float R, float k, float kl, float t) {
    const UiConfig& U = ui_config();
    Font* fW = te_font(f, U.scText[SC_LIST]); Font* fS = te_font(f, U.scText[SC_PROP]); if (!fW || !fS) return;
    (void)t; const int m = n > 16 ? 16 : n; if (m <= 0) return;   // all of them : a move is either shown or not offered, never a "+N"
    const float ic = U.scListIcon < 0.5f ? 0.5f : (U.scListIcon > 2.0f ? 2.0f : U.scListIcon), cs = U.scCardScale < 0.6f ? 0.6f : (U.scCardScale > 1.6f ? 1.6f : U.scCardScale);
    const float fzW = te_sz(U.scText[SC_LIST], 9.0f * kl), fzS = te_sz(U.scText[SC_PROP], 8.0f * kl), isz = 7.5f * kl * ic, gp = 3.0f * kl * cs;
    const float l2 = fzS > isz ? fzS : isz, ch = snap((fzW + l2) * 1.05f + 8.0f * kl * cs);   // the card follows its text / icons
    float cw = 44.0f * kl * cs;
    for (int i = 0; i < m; ++i) {
        const float a2 = fW->measure(it[i].ws ? it[i].ws : "", fzW) + 10.0f * kl * cs; if (a2 > cw) cw = a2;
        const SCInfo& si = sc_info(it[i].prop); const int ne = list_ne(si);
        const float b2 = fS->measure(scprop_name(it[i].prop), fzS) + 3.0f * kl + ne * (isz + 1.0f * kl) + 10.0f * kl * cs; if (b2 > cw) cw = b2;
    }
    cw = snap(cw);
    const bool vert = lpos() >= 2;
    // NARROW : 2 a row at most under / above (about the sphere's own width), 1 a row left / right up to 4
    const int uc = U.scListCols;   // 0 auto
    const int cols = uc > 0 ? (uc < m ? uc : m) : (vert ? (m <= 4 ? 1 : 2) : (m < 2 ? m : 2)), rows = (m + cols - 1) / cols;
    const float bw = cols * cw + (cols - 1) * gp, bh = rows * ch + (rows - 1) * gp;
    float x, y; block_place(CX, CY, R, k, kl, bw, bh, x, y);
    if (sx_meas) { sx_mrec(x, y, bw, bh, kl); return; }
    block_back(f, dev, x, y, bw, bh, kl);
    for (int i = 0; i < m; ++i) {
        const ScCircleItem& e = it[i]; const Pal q = pal(e.prop);
        const int r = i / cols, c = i % cols, inRow = (r == rows - 1) ? m - r * cols : cols;
        const float cx = snap(x + (bw - (inRow * cw + (inRow - 1) * gp)) * 0.5f + c * (cw + gp)), cy = snap(y + r * (ch + gp));
        cqs(dev);
        rrect(dev, cx, cy, cw, ch, 3.0f * kl, C(lerp(0x0A0E16, q.mid, 0.30f), 0.95f), C(0x06080C, 0.95f));
        rrect_stroke(dev, cx, cy, cw, ch, 3.0f * kl, C(lerp(q.mid, q.hot, 0.4f), 0.85f), 1.0f);
        const int lv = e.lvl < 1 ? 1 : (e.lvl > 4 ? 4 : e.lvl);
        lvl_gems(dev, cx + cw * 0.5f - (lv * 4.2f * kl + 2.5f * kl) * 0.5f, cy, lv, q.hot, kl);   // its level, on the top edge
        sx_fs = 0; fW->begin(dev);
        fW->draw_c(dev, cx + cw * 0.5f, cy + 4.0f * kl * cs + fzW * 0.55f, e.ws ? e.ws : "", fzW, te_col(U.scText[SC_LIST], 0xFFF0F3F7u), 0xFF000000u, te_ow(U.scText[SC_LIST], 1.1f * kl));
        const SCInfo& si = sc_info(e.prop); const int ne = list_ne(si);
        const float sw = fS->measure(scprop_name(e.prop), fzS), lw = sw + 3.0f * kl + ne * (isz + 1.0f * kl), lx = snap(cx + (cw - lw) * 0.5f), ly = cy + ch - 4.0f * kl * cs - l2 * 0.55f;
        sx_fs = 0; fS->begin(dev);
        fS->draw_lc(dev, lx, ly, scprop_name(e.prop), fzS, te_col(U.scText[SC_PROP], C(lerp(q.mid, 0xFFFFFF, 0.35f), 1.0f)), 0xFF000000u, te_ow(U.scText[SC_PROP], 1.0f * kl));
        cqs(dev); elem_icons(dev, e.prop, lx + sw + 3.0f * kl, ly, kl, isz);
    }
    cqs(dev);
}

// a grouped row's height : its tallest of chip text / moves text / icons, plus air
float grp_rh(float fzP, float fzW, float isz, float kl) { float m = fzP > fzW ? fzP : fzW; if (isz > m) m = isz; return m * 1.3f + 3.0f * kl; }
// a weapon type's short tag (res skill id), for a move of ANOTHER weapon than the one in hand
const char* wsk_tag(int s) {
    switch (s) { case 1: return "H2H"; case 2: return "DAG"; case 3: return "SWD"; case 4: return "GS"; case 5: return "AXE"; case 6: return "GA";
    case 7: return "SCY"; case 8: return "POL"; case 9: return "KAT"; case 10: return "GK"; case 11: return "CLB"; case 12: return "STF";
    case 25: return "ARC"; case 26: return "MRK"; }
    return "";
}
// one skillchain's moves FLOWED from x0 : the weapon in hand's first, full size ; another weapon's smaller and dimmer,
// its weapon's tag before it ; "  ·  " between ; wrapped at xMax back to x0 (never cut, never "+N").
// draw = false : measures only. -> the lines used ; *wUsed = the right-most x reached minus x0
int grp_flow(u32 dev, Font* fW, const LGroup& g, float x0, float y0, float xMax, float fz, float lineH, float kl, bool draw, float* wUsed) {
    const UiConfig& U = ui_config();
    const float fzO = fz * 0.86f, fzT = fz * 0.66f, ow = te_ow(U.scText[SC_LIST], 1.1f * kl);
    const char* SEP = "  \xC2\xB7  "; const float sepW = fW->measure(SEP, fz * 0.9f);
    float x = x0, mx = x0; int line = 0;
    if (draw) { sx_fs = 0; fW->begin(dev); }
    for (int j = 0; j < g.n; ++j) {
        const bool oth = g.oth[j] != 0; const char* tag = oth ? wsk_tag(g.sk[j]) : "";
        const float tw = tag[0] ? fW->measure(tag, fzT) + 3.0f * kl : 0.0f, nw = fW->measure(g.ws[j], oth ? fzO : fz), tok = tw + nw;
        if (j && x + sepW + tok > xMax && x > x0) { ++line; x = x0; }
        else if (j) { if (draw) fW->draw_lc(dev, snap(x), y0 + line * lineH, SEP, fz * 0.9f, 0xFF6A7282u, 0xFF000000u, ow); x += sepW; }
        const float y = y0 + line * lineH;
        if (draw) {
            if (tw > 0.0f) fW->draw_lc(dev, snap(x), y, tag, fzT, 0xFF8A93A3u, 0xFF000000u, ow);
            fW->draw_lc(dev, snap(x + tw), y, g.ws[j], oth ? fzO : fz, oth ? 0xFFA9B1BFu : te_col(U.scText[SC_LIST], 0xFFE8ECF2u), 0xFF000000u, ow);
        }
        x += tok; if (x > mx) mx = x;
    }
    if (wUsed) *wUsed = mx - x0;
    return line + 1;
}
// 3 -- GROUPED : one block per skillchain -- a tinted chip (gems, name, icons) then its moves flowed after it, the
// weapon in hand's first ; a block wraps to a 2nd line rather than growing wide. Lesser levels a little smaller.
void list_grouped(const Frame& f, u32 dev, const ScCircleItem* it, int n, float CX, float CY, float R, float k, float kl, float t) {
    const UiConfig& U = ui_config();
    Font* fS = te_font(f, U.scText[SC_PROP]); Font* fW = te_font(f, U.scText[SC_LIST]); if (!fS || !fW) return;
    (void)t; LGroup g[8]; const int ng = group_items(it, n, g, 8); if (!ng) return;
    float sz[8], chipW = 0.0f, maxTok = 0.0f;
    const float gm = U.scListGap < 0.6f ? 0.6f : (U.scListGap > 3.0f ? 3.0f : U.scListGap);
    const float ic = U.scListIcon < 0.5f ? 0.5f : (U.scListIcon > 2.0f ? 2.0f : U.scListIcon);
    for (int i = 0; i < ng; ++i) {
        sz[i] = g[i].lvl >= g[0].lvl ? 1.0f : 0.88f;
        const float hw = sc_head_w(fS, te_sz(U.scText[SC_PROP], 9.0f * kl), g[i], 8.0f * kl * ic, kl); if (hw > chipW) chipW = hw;
        const float fz = te_sz(U.scText[SC_LIST], 9.0f * kl * sz[i]);
        for (int j = 0; j < g[i].n; ++j) { const float w = fW->measure(g[i].ws[j], fz) + (g[i].oth[j] ? fW->measure(wsk_tag(g[i].sk[j]), fz * 0.66f) + 3.0f * kl : 0.0f); if (w > maxTok) maxTok = w; }
    }
    chipW += 8.0f * kl;   // every chip as wide as the widest : the moves start in one column
    const float mx0 = chipW + 6.0f * kl;
    // ONE pitch for every line (a chip's line or a move's wrapped line) and ONE chip size : the chips stay evenly spaced
    const float fzP = te_sz(U.scText[SC_PROP], 9.0f * kl), fz1 = te_sz(U.scText[SC_LIST], 9.0f * kl), isz = 8.0f * kl * ic;
    const float P = grp_rh(fzP, fz1, isz, kl) * gm, chH = snap((fzP > isz ? fzP : isz) * 1.3f + 1.0f * kl);
    // wrap only as a last resort : the widest group on ONE line, unless that passes ~2x the sphere's box
    float one = 0.0f;
    for (int i = 0; i < ng; ++i) { float u = 0.0f; grp_flow(0, fW, g[i], 0.0f, 0.0f, 1.0e9f, fz1 * sz[i], P, kl, false, &u); if (u > one) one = u; }
    const int uc = U.scListCols, cols = uc > 0 ? (uc < ng ? uc : ng) : 1;   // 0 auto = one column
    const float cgap = 14.0f * kl, cap = (2.0f * (R + 40.0f * k) * 1.9f) / cols - mx0;
    float wrapW = one < cap ? one + 1.0f : cap; if (wrapW < maxTok) wrapW = maxTok;
    int nl[8]; float colW[4] = { 0, 0, 0, 0 }, colH[4] = { 0, 0, 0, 0 };
    for (int i = 0; i < ng; ++i) { float u = 0.0f; nl[i] = grp_flow(0, fW, g[i], 0.0f, 0.0f, wrapW, fz1 * sz[i], P, kl, false, &u);
        const int c = i % cols; colH[c] += nl[i] * P; if (mx0 + u > colW[c]) colW[c] = mx0 + u; }
    float bw = 0.0f, bh = 0.0f; for (int c = 0; c < cols; ++c) { bw += colW[c] + (c ? cgap : 0.0f); if (colH[c] > bh) bh = colH[c]; }
    float x, y; block_place(CX, CY, R, k, kl, bw, bh, x, y);
    if (sx_meas) { sx_mrec(x, y, bw, bh, kl); return; }
    block_back(f, dev, x, y, bw, bh, kl);
    float colX[4], cyc[4] = { y, y, y, y }; { float xx = x; for (int c = 0; c < cols; ++c) { colX[c] = xx; xx += colW[c] + cgap; } }
    for (int i = 0; i < ng; ++i) {
        const int c = i % cols; const float x = colX[c];
        const Pal q = pal(g[i].prop); const float my = snap(cyc[c] + P * 0.5f);
        cqs(dev);
        rrect(dev, snap(x), snap(my - chH * 0.5f), snap(chipW), chH, chH * 0.5f, C(lerp(0x0A0E16, q.mid, 0.40f), 0.90f), C(lerp(0x06080C, q.mid, 0.15f), 0.90f));
        rrect_stroke(dev, snap(x), snap(my - chH * 0.5f), snap(chipW), chH, chH * 0.5f, C(lerp(q.mid, q.hot, 0.4f), 0.6f), 1.0f);
        sc_head(dev, fS, fzP, g[i], snap(x + 4.0f * kl), my, isz, kl, 1.0f);
        grp_flow(dev, fW, g[i], x + mx0, my, x + mx0 + wrapW, fz1 * sz[i], P, kl, true, 0);   // the lesser levels' moves a little smaller, on the same grid
        cyc[c] += nl[i] * P;
    }
    cqs(dev);
}

// ---------------------------------------------------------------- the CIRCLE (one frame of it) ----
// Shared by the demo and the live box : the sphere (opened : grey, neutral ; formed : the skillchain's animation),
// the countdown arc, the centre's text, the burst elements, the title, the list. Honours the config : animations /
// distortion / which lines / the list's place and frame / the typography.
// where burst element i of n sits : in a COLUMN on each side of the sphere (left takes the extra one), 3 at most a
// side, centred on its height -- never under the title, wherever it is moved
void elem_pos(int i, int n, float CX, float CY, float R, float k, float& x, float& y) {
    const int nl = (n + 1) / 2, nr = n - nl, left = i < nl, j = left ? i : i - nl, cnt = left ? nl : nr;
    x = CX + (left ? -1.0f : 1.0f) * (R + 27.0f * k);
    y = CY + (j - (cnt - 1) * 0.5f) * 28.0f * k;
}
// the list, in its configured style
void list_style(const Frame& f, u32 dev, const ScCircleItem* it, int n, float CX, float CY, float R, float k, float kl, float t);
void list_draw(const Frame& f, u32 dev, const ScCircleItem* it, int n, float CX, float CY, float R, float k, float kl, float t) {
    if (n <= 0 || !it) return;
    if (ui_config().scListPos == 4) {   // BOTH SIDES : the skillchains dealt left / right in turn (best first) -- one = left only
        ScCircleItem L[32], Rr[32]; int nl = 0, nr = 0, props[16], np = 0;
        for (int i = 0; i < n && i < 32; ++i) {
            int s2 = -1; for (int j = 0; j < np; ++j) if (props[j] == it[i].prop) s2 = j;
            if (s2 < 0 && np < 16) { s2 = np; props[np++] = it[i].prop; }
            if (s2 < 0 || (s2 & 1) == 0) L[nl++] = it[i]; else Rr[nr++] = it[i];
        }
        sx_posOv = 2; list_style(f, dev, L, nl, CX, CY, R, k, kl, t);
        if (nr > 0) { sx_posOv = 3; list_style(f, dev, Rr, nr, CX, CY, R, k, kl, t); }
        sx_posOv = -1; return;
    }
    list_style(f, dev, it, n, CX, CY, R, k, kl, t);
}
void list_style(const Frame& f, u32 dev, const ScCircleItem* it, int n, float CX, float CY, float R, float k, float kl, float t) {
    if (n <= 0 || !it) return;
    switch (ui_config().scListStyle) {
    case 1:  list_tree(f, dev, it, n, CX, CY, R, k, kl, t); break;
    case 2:  list_cards(f, dev, it, n, CX, CY, R, k, kl, t); break;
    case 3:  list_grouped(f, dev, it, n, CX, CY, R, k, kl, t); break;
    default: list_rows(f, dev, it, n, CX, CY, R, k, kl); break;
    }
}
struct CircleIn {
    int P; bool closed; int phase; float timer, frac, bt, t; int tp; const char* opener;
    const ScCircleItem* items; int nItems; float CX, CY, R, k, kl; bool preview;
    int hitKind; const char* hitName; int hitDmg; int hitElem; bool hitHasDmg; float hitMs;
};
// ---- the HIT popup : YOUR weaponskill (arcade : name + damage slam, impact, in the chain's colour) or YOUR magic
// burst (in the colour of the element that burst : "MAGIC BURST", damage, the move ; a beam from its burst icon).
// The same timing and grammar as the Arcade WS module (2.4 s : pop-in, heartbeat, fade), drawn ON the sphere. ----
const float HIT_LIFE = 2400.0f;
bool hit_on(const CircleIn& in) { return in.hitKind > 0 && in.hitMs >= 0.0f && in.hitMs < HIT_LIFE; }
u32 lerp_argb(u32 a, u32 b, float t) {
    if (t < 0.0f) t = 0.0f; if (t > 1.0f) t = 1.0f;
    return lerp(a & 0xFFFFFF, b & 0xFFFFFF, t) | 0xFF000000u;
}
// where the hit popup goes : NEVER over the sphere (its timer keeps running under a burst) -- the free side, given
// where the list is (scHitPos 0 auto : list under -> above the title ; above -> under ; left -> right ; right ->
// left ; both sides / no list -> above), or the side chosen
void hit_anchor(const CircleIn& in, float hs, float& ax, float& ay) {
    const UiConfig& U = ui_config(); const float CX = in.CX, CY = in.CY, R = in.R, k = in.k;
    int side = U.scHitPos;   // 1 above 2 under 3 left 4 right
    if (side < 1 || side > 4) { const int lp = U.scList ? U.scListPos : 4; side = lp == 1 ? 2 : lp == 2 ? 4 : lp == 3 ? 3 : 1; }
    const float titleOff = (U.scTitleOff < -120.0f ? -120.0f : (U.scTitleOff > 80.0f ? 80.0f : U.scTitleOff)) * k;
    switch (side) {
    case 2:  ax = CX; ay = CY + R + 40.0f * k + 30.0f * k * hs; break;
    case 3:  ax = CX - side_gap(R, k, in.kl) - 70.0f * k * hs; ay = CY; break;
    case 4:  ax = CX + side_gap(R, k, in.kl) + 70.0f * k * hs; ay = CY; break;
    default: ax = CX; ay = CY - R - 50.0f * k - titleOff - (U.scTitle ? 22.0f * k : 0.0f) - 34.0f * k * hs; break;
    }
    const float mx = U.scHitX < -200.0f ? -200.0f : (U.scHitX > 200.0f ? 200.0f : U.scHitX), my = U.scHitY < -200.0f ? -200.0f : (U.scHitY > 200.0f ? 200.0f : U.scHitY);
    ax += mx * k; ay -= my * k;   // the whole block moved by hand
    ax = snap(ax); ay = snap(ay);
}
// a cheap hash -> 0..1 (the sparks' fixed randomness : the same burst every frame, no state)
float hrand(unsigned i) { i = (i ^ 61u) ^ (i >> 16); i *= 9u; i ^= i >> 4; i *= 0x27d4eb2du; i ^= i >> 15; return (float)(i & 0xFFFFu) / 65535.0f; }
void hit_fx(const Frame& f, u32 dev, const CircleIn& in) {
    if (!hit_on(in)) return;
    const UiConfig& U = ui_config();
    Font* fo = f.fonts ? f.fonts->get(ui_font_face(U.scHitFont), 900) : f.font; if (!fo) fo = f.font; if (!fo) return;
    const float hs = U.scHitScale < 0.5f ? 0.5f : (U.scHitScale > 2.0f ? 2.0f : U.scHitScale);
    const float k = in.k, K = k * hs;   // K : the popup's own scale
    const float yN = -(U.scHitNameY < -80.0f ? -80.0f : (U.scHitNameY > 80.0f ? 80.0f : U.scHitNameY)) * k;   // > 0 : higher
    const float yD = -(U.scHitDmgY < -80.0f ? -80.0f : (U.scHitDmgY > 80.0f ? 80.0f : U.scHitDmgY)) * k;
    float ax, ay; hit_anchor(in, hs, ax, ay);
    const float e = in.hitMs, CX = in.CX, CY = in.CY, R = in.R;
    float a = 1.0f; if (e < 70.0f) a = e / 70.0f; else if (e > HIT_LIFE - 500.0f) a = (HIT_LIFE - e) / 500.0f;
    a = a < 0.0f ? 0.0f : (a > 1.0f ? 1.0f : a);
    const bool mb = in.hitKind == 2;
    const u32 tint = mb ? (in.hitElem >= 0 ? elem_rgb(in.hitElem) : 0xFFFFFF) : (in.P >= 0 ? pal(in.P).hot : 0xFFD070);
    const u32 hotT = lerp(tint, 0xFFFFFF, 0.55f), dark = lerp(tint, 0x000000, 0.82f);
    (void)dark;   // the older text used it ; the styles take their own shades
    auto eo3 = [](float t) { if (t < 0.0f) t = 0.0f; if (t > 1.0f) t = 1.0f; const float u = 1.0f - t; return 1.0f - u * u * u; };
    const float beat = 0.5f + 0.5f * sinf(e * 0.010f), es = e / 1000.0f;
    const unsigned seed = (unsigned)(in.hitDmg * 2654435761u) ^ (unsigned)in.hitKind * 97u;
    cqs(dev); blend(dev, true);
    if (U.scHitFx) {
        if (e < 620.0f) {   // 1. IMPACT : a white-hot core, a TORN shockwave (the chains' own), twin rings, light spokes
            const float t = e / 620.0f, et = eo3(t), fd = 1.0f - t, fd2 = fd * fd;
            const float cr = (26.0f + 70.0f * et) * K;
            soft_blob(dev, ax, ay, cr * 1.5f, cr, C(hotT, 0.9f * a * fd2));
            for (int r2 = 0; r2 < 2; ++r2) {
                const float tk = (e - r2 * 90.0f) / 620.0f; if (tk <= 0.0f || tk >= 1.0f) continue;
                const float rr = (14.0f + 120.0f * eo3(tk)) * K, fk = (1.0f - tk) * (1.0f - tk);
                rrect_stroke(dev, ax - rr * 1.4f, ay - rr, 2.8f * rr, 2.0f * rr, rr, C(r2 ? tint : hotT, 0.8f * a * fk), (3.0f - 1.8f * tk) * K + 1.0f);
            }
            const int ns = mb ? 14 : 10; const float s0 = 18.0f * K, s1 = (40.0f + 150.0f * et) * K;
            for (int sp = 0; sp < ns; ++sp) {
                const float ang = sp * TAU / ns + t * 0.4f + hrand(seed + sp) * 0.25f, c2 = cosf(ang) * 1.35f, s2 = sinf(ang);
                seg_soft(dev, ax + s0 * c2, ay + s0 * s2, ax + s1 * c2, ay + s1 * s2, (2.4f * fd + 0.4f) * K, C(tint, 0.5f * a * fd2));
            }
            fx_end(dev);
            shockwave(dev, ax, ay, 16.0f * K, K, et, in.t, hotT, tint, 5.0f);
            fx_end(dev); cqs(dev); blend(dev, true);
        }
        {   // 2. SPARKS : flung out of the impact, slowing, falling, fading -- streaks, not dots
            const int NS = mb ? 34 : 26;
            for (int sp = 0; sp < NS; ++sp) {
                const float life = 0.55f + 0.6f * hrand(seed + 100u + sp), tt = es - 0.03f * hrand(seed + 200u + sp);
                if (tt <= 0.0f || tt >= life) continue;
                const float ang = hrand(seed + 300u + sp) * TAU, sp0 = (140.0f + 260.0f * hrand(seed + 400u + sp)) * K;
                const float dr = 2.2f, d = sp0 * (1.0f - expf(-dr * tt)) / dr, g = 60.0f * K * tt * tt;
                const float vx = cosf(ang) * 1.3f, vy = sinf(ang);
                const float px = ax + vx * d, py = ay + vy * d + g, sv = sp0 * expf(-dr * tt) * 0.035f + 2.0f * K;
                const float fa = 1.0f - tt / life;
                seg_soft(dev, px, py, px - vx * sv, py - vy * sv - 2.0f * K * tt, (1.1f + 0.8f * fa) * K, C(sp & 3 ? tint : hotT, 0.9f * a * fa));
            }
        }
        if (mb) {   // 3. a magic burst : a beam from the icon of the element that burst
            if (in.P >= 0 && U.scProps && in.hitElem >= 0 && e < 900.0f) {
                const SCInfo& SI = sc_info(in.P); const int n = SI.nElem < 1 ? 1 : (SI.nElem > 4 ? 4 : SI.nElem); const float fd = 1.0f - e / 900.0f;
                for (int i = 0; i < n; ++i) if (SI.elem[i] == in.hitElem) {
                    float ix, iy; elem_pos(i, n, CX, CY, R, k, ix, iy);
                    seg_soft(dev, ix, iy, ax, ay, (4.0f * fd + 1.0f) * k, C(tint, 0.7f * a * fd));
                    soft_blob(dev, ix, iy, 24.0f * k * (2.0f - fd), 24.0f * k * (2.0f - fd), C(hotT, 0.9f * a * fd));
                }
            }
        }
        if (e > 200.0f) {   // 4. a few twinkles round the text while it holds
            bat_begin();
            for (int s2 = 0; s2 < 5; ++s2) {
                const float ph = fmodf(es * 1.3f + hrand(seed + 500u + s2), 1.0f), tw = sinf(ph * PI);
                star(dev, ax + (hrand(seed + 600u + s2) - 0.5f) * 200.0f * K, ay + (hrand(seed + 700u + s2) - 0.5f) * 50.0f * K, 9.0f * K * tw, C(hotT, 0.8f * a * tw));
            }
            bat_end(dev); cqs(dev); blend(dev, true);
        }
    }
    {   // 5. the BREATH behind the text ; the sphere's ring lit in the hit's colour (thin : its timer stays readable)
        const float g = 80.0f * K * (1.0f + 0.06f * beat);
        soft_blob(dev, ax, ay, g * 1.3f, g * 0.55f, C(tint, (0.14f + 0.08f * beat) * a));
        ring_sector(dev, CX, CY, R + 1.0f * k, R + 3.5f * k, 0.0f, TAU, C(tint, (0.30f + 0.25f * beat) * a));
    }
    blend(dev, false); sx_fs = 0;
    // 6. the TEXT, in the style chosen (scHitStyle) : four fighting-game looks. In all four the text LANDS (a
    // stamp, a hit-stop shake of 130 ms at most) and then stays perfectly still -- the life is round it.
    //   0 VERSUS   : heavy italic, banded white / gold / orange fill, a red then a black stroke ; the name in a
    //                slanted black banner with a rim, wiped in ; a white flash, the scene's refraction ring.
    //   1 COMIC    : a jagged starburst behind the damage, white letters with a fat black stroke ; the name on two
    //                slanted strips (colour, black) snapping in ; speed lines.
    //   2 IMPACT   : a CHROME number (steel bands, a dark horizon) with a rim of the hit's colour, a halo behind ;
    //                radial lines out of it, a white flash, the refraction ring ; the name thin, wide, slashed under.
    //   3 CRITICAL : the digits stamped one by one, left to right ; a crit glint on the corner ; a halo behind.
    // DX8 : the bands are cut with the VIEWPORT (a hard scissor) ; the ring is the render-target copy (distort).
    char up[48]; { int i = 0; const char* nm = in.hitName ? in.hitName : ""; for (; nm[i] && i < 47; ++i) { const char c = nm[i]; up[i] = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c; } up[i] = 0; }
    const bool hasD = in.hitDmg > 0 && in.hitHasDmg;
    const int HS = (U.scHitStyle >= 0 && U.scHitStyle <= 3) ? U.scHitStyle : 0;
    Font* fi = f.fonts ? f.fonts->get(U.scHitFont > 0 ? ui_font_face(U.scHitFont) : "Impact", U.scHitFont > 0 ? 700 : 400, true) : 0; if (!fi) fi = fo;   // the fighting-game face : heavy, italic
    const float nameY = ay + (hasD ? -17.0f * K : 0.0f) + yN, dmgY = ay + 13.0f * K + yD;
    const char* nameStr = mb ? "MAGIC BURST" : up;
    const bool showName = mb ? U.scMbLabel != 0 : up[0] != 0;
    const char* subStr = (mb && U.scMbName) ? up : "";
    const u32 hot = mb ? lerp(tint, 0xFFFFFF, 0.55f) : (U.scHitDmgCol1 & 0xFFFFFFu), deep = mb ? tint : (U.scHitDmgCol2 & 0xFFFFFFu);
    const u32 deeper = lerp(deep, 0x000000, 0.5f), rim = mb ? hot : (U.scHitNameCol & 0xFFFFFFu), glow = mb ? tint : lerp(hot, deep, 0.5f);
    char db[16]; db[0] = 0; if (hasD) _snprintf(db, sizeof(db), "%d", in.hitDmg);
    D3DVIEWPORT8 vpFull; const bool vpOk = dGetViewport(dev, vpFull);
    auto clip = [&](float x0, float y0, float x1, float y1) {   // a sub-rect of the screen : the viewport is a hard scissor
        if (!vpOk) return;
        const int L = (int)vpFull.X, T = (int)vpFull.Y, R2 = L + (int)vpFull.Width, B2 = T + (int)vpFull.Height;
        int l = (int)x0, t0 = (int)y0, r = (int)(x1 + 0.999f), bb = (int)(y1 + 0.999f);
        if (l < L) l = L; if (t0 < T) t0 = T; if (r > R2) r = R2; if (bb > B2) bb = B2;
        if (r <= l) { if (l >= R2) l = R2 - 1; r = l + 1; } if (bb <= t0) { if (t0 >= B2) t0 = B2 - 1; bb = t0 + 1; }   // never an empty viewport (rejected)
        D3DVIEWPORT8 vp = vpFull; vp.X = (u32)l; vp.Y = (u32)t0; vp.Width = (u32)(r - l); vp.Height = (u32)(bb - t0); dSetViewport(dev, vp);
    };
    auto unclip = [&]() { if (vpOk) dSetViewport(dev, vpFull); };
    struct Band { float y0, y1; u32 c; };   // a band of the fill : from y0 to y1, fractions of the display size from the centre
    // a string centred on (x, y) at display size zd (baked at z) : its strokes (outer, then inner), then the fill in bands
    // a string centred on (x, y) at display size zd (baked at z) : a drop shadow, a SMOOTH black stroke (w1 : two
    // rings, the font's stroke being 8 offset copies -- lumpy past ~2 px alone), a thin rim of colour (o2, w2), a solid
    // fill. Readable first : no banded fill on the letters (B / nb kept for the call sites, unused).
    auto metal = [&](Font* F, const char* str, float x, float y, float z, float zd, u32 base, const Band* B, int nb, u32 o1, float w1, u32 o2, float w2, float al) {
        (void)B; (void)nb;
        if (al <= 0.003f || !str[0]) return;
        blend(dev, false); F->begin(dev);
        const float sh = 0.06f * zd;
        F->draw_c_scaled(dev, x + sh * 0.7f, y + sh, str, z, zd, C(0x000000, 0.55f * al), C(0x000000, 0.40f * al), w1 > 0.0f ? w1 : 1.0f);   // the shadow
        if (w1 > 0.0f) { F->draw_c_scaled(dev, x, y, str, z, zd, C(base, al), C(o1, al), w1); F->draw_c_scaled(dev, x, y, str, z, zd, C(base, al), C(o1, al), w1 * 0.5f); }
        if (w2 > 0.0f) F->draw_c_scaled(dev, x, y, str, z, zd, C(base, al), C(o2, al), w2);
        F->draw_c_scaled(dev, x, y, str, z, zd, C(base, al), 0, 0.0f);
    };
    auto stampS = [](float t, float from, float dur) -> float {   // the landing : from big to 1, fast, then exactly 1
        if (t < 0.0f) return 0.0f; if (t >= dur) return 1.0f; const float u = 1.0f - t / dur; return from - (from - 1.0f) * (1.0f - u * u * u); };
    auto shk = [&](float t, float amp, unsigned sd, float& sx, float& sy) {   // the hit-stop : 4 jolts in 130 ms, then still
        sx = sy = 0.0f; if (t < 0.0f || t > 130.0f) return; const float fdd = 1.0f - t / 130.0f; const unsigned st = (unsigned)(t / 32.0f);
        sx = (hrand(sd + st) - 0.5f) * 2.0f * amp * fdd; sy = (hrand(sd + st + 50u) - 0.5f) * 2.0f * amp * fdd; };
    auto ripple = [&](float x, float y, float t, float r0) {   // the scene bent : a refraction ring out of the impact (DX8 copy)
        if (!U.scDistort || t < 0.0f || t > 380.0f) return;
        const float u = t / 380.0f; Warp w = {}; w.shR = r0 * (1.0f + 2.0f * eo3(u)); w.shW = 9.0f * K; w.shA = 9.0f * K * (1.0f - u);
        const float keep = REACH; REACH = 1.0f; distort(dev, x, y, r0, K, in.t, w); REACH = keep; fx_end(dev); sx_cUse = GetTickCount(); };
    auto para = [&](float cx, float cy, float w, float h, float sl, u32 col) {   // a slanted plate
        const float pp[8] = { cx - w * 0.5f + sl, cy - h * 0.5f, cx + w * 0.5f + sl, cy - h * 0.5f, cx + w * 0.5f - sl, cy + h * 0.5f, cx - w * 0.5f - sl, cy + h * 0.5f };
        fill_poly_aa(dev, pp, 4, col); };
    auto addOn = [&]() { cqs(dev); blend(dev, true); };
    auto addOff = [&]() { blend(dev, false); cqs(dev); };
    const float clampE80 = e < 80.0f ? e / 80.0f : 1.0f;

    if (HS == 0) {   // ======== VERSUS ========
        if (showName) {
            const float zN = 13.0f * K, tw = fi->measure(nameStr, zN), bw = tw + 46.0f * K, bh = zN * 1.9f, sl = 9.0f * K, rev = eo3(e / 160.0f);
            const float wx0 = ax - bw * 0.5f - sl - 4.0f * K, wx1 = wx0 + (bw + 2.0f * sl + 8.0f * K) * rev;
            addOff();
            clip(wx0, nameY - bh, wx1, nameY + bh);   // the wipe : the banner revealed left -> right, never moving
            para(ax, nameY, bw + 5.0f * K, bh + 5.0f * K, sl, C(rim, a));
            para(ax, nameY, bw, bh, sl, C(0x07070B, 0.92f * a));
            para(ax + 2.0f * K, nameY - bh * 0.26f, bw - 10.0f * K, bh * 0.36f, sl * 0.4f, C(0xFFFFFF, 0.07f * a));   // its gloss
            metal(fi, nameStr, ax, nameY, zN, zN, 0xFFFFFF, 0, 0, 0x000000, 1.6f * K, 0, 0.0f, a);
            unclip();
            if (rev < 1.0f) { addOn(); seg_soft(dev, wx1 - 2.0f * K, nameY - bh * 0.7f, wx1 - 6.0f * K, nameY + bh * 0.7f, 2.4f * K, C(lerp(rim, 0xFFFFFF, 0.6f), a)); addOff(); }   // the wipe's bright edge
        }
        if (hasD) {
            const float ed = e - 110.0f, sc = stampS(ed, 1.7f, 90.0f);
            if (sc > 0.0f) {
                float sx, sy; shk(ed, 6.0f * K, seed, sx, sy);
                ripple(ax, dmgY, ed, 30.0f * K);
                addOn();
                if (ed < 170.0f) soft_blob(dev, ax, dmgY, 170.0f * K, 80.0f * K, C(0xFFFFFF, 0.6f * (1.0f - ed / 170.0f) * a));   // the flash
                soft_blob(dev, ax, dmgY, 90.0f * K, 34.0f * K, C(glow, 0.30f * a));
                const float z = 26.0f * K, zd = z * sc;
                const Band Bv[3] = { { 0.24f, 0.9f, deeper }, { -0.9f, 0.03f, hot }, { -0.9f, -0.21f, lerp(hot, 0xFFFFFF, 0.8f) } };
                metal(fi, db, ax + sx, dmgY + sy, z, zd, lerp(hot, 0xFFFFFF, 0.25f), Bv, 3, 0x000000, 2.4f * K * sc, deep, 1.1f * K * sc, a);
            }
        }
        if (subStr[0]) metal(fi, subStr, ax, dmgY + 25.0f * K, 9.0f * K, 9.0f * K, lerp(hot, 0xFFFFFF, 0.5f), 0, 0, 0x000000, 1.6f * K, 0, 0.0f, a * clampE80);
    } else if (HS == 1) {   // ======== COMIC ========
        // Hard graphic shapes, no ornament (the starburst read as kitsch) : the name on a black slab, the damage on a
        // black blade-shaped shard, each with a red twin kicked out behind and a white edge, wiped in left -> right.
        const float dY = dmgY + 6.0f * K;
        if (hasD) {   // the damage on a SHARD : a black blade-shaped plate, a red one kicked out behind, wiped in ; no ornament
            const float ed = e - 120.0f, sc = stampS(ed, 1.45f, 80.0f);
            if (sc > 0.0f) {
                ripple(ax, dY, ed, 32.0f * K);
                const float z = 24.0f * K, tw = fi->measure(db, z), W = tw + 54.0f * K, H = 34.0f * K, rv = eo3(ed / 110.0f);
                auto shard = [&](float cx, float cy, float w, float h, u32 col) {   // a long hexagon, slanted, pointed at both ends
                    const float sk = 0.32f * h;
                    const float pp[12] = { cx - w * 0.5f - sk * 0.2f, cy + h * 0.10f,   cx - w * 0.5f + sk * 0.9f, cy - h * 0.5f,   cx + w * 0.5f + sk * 0.6f, cy - h * 0.5f,
                                           cx + w * 0.5f + sk * 0.2f, cy - h * 0.10f,   cx + w * 0.5f - sk * 0.9f, cy + h * 0.5f,   cx - w * 0.5f - sk * 0.6f, cy + h * 0.5f };
                    fill_poly_aa(dev, pp, 6, col); };
                const float x0 = ax - W * 0.5f - 30.0f * K;
                addOn(); if (ed < 120.0f) soft_blob(dev, ax, dY, W * 1.1f, H * 1.6f, C(0xFFFFFF, 0.45f * (1.0f - ed / 120.0f) * a)); addOff();
                clip(x0, dY - 60.0f * K, x0 + (W + 60.0f * K) * rv, dY + 60.0f * K);
                shard(ax + 8.0f * K, dY + 6.0f * K, W, H, C(mb ? deep : 0xD8141C, a));   // the red, kicked out
                shard(ax, dY, W + 4.0f * K, H + 4.0f * K, C(0xFFFFFF, a));               // a white edge
                shard(ax, dY, W, H, C(0x0A0A0E, a));                                      // the black
                unclip();
                metal(fi, db, ax, dY, z, z * sc, 0xFFFFFF, 0, 0, 0x000000, 1.4f * K * sc, 0, 0.0f, a);
            }
        }
        if (showName) {   // a black slab, a red one kicked out behind, snapping in from its left end
            const float zN = 14.0f * K, tw = fi->measure(nameStr, zN), W = tw + 44.0f * K, H = 22.0f * K, sl = 10.0f * K, rv = eo3(e / 130.0f);
            const float x0 = ax - W * 0.5f - sl - 12.0f * K, x1 = x0 + (W + 2.0f * sl + 24.0f * K) * rv;
            addOff();
            clip(x0, nameY - 40.0f * K, x1, nameY + 40.0f * K);
            para(ax + 7.0f * K, nameY + 5.0f * K, W, H, sl, C(mb ? deep : 0xE8141E, a));     // the red, kicked out
            para(ax, nameY, W + 4.0f * K, H + 4.0f * K, sl, C(0xFFFFFF, a));                  // a white edge
            para(ax, nameY, W, H, sl, C(0x0A0A0E, a));                                         // the black slab
            unclip();
            if (e > 60.0f) metal(fi, nameStr, ax, nameY, zN, zN, 0xFFFFFF, 0, 0, 0x000000, 1.2f * K, 0, 0.0f, a * (e < 120.0f ? (e - 60.0f) / 60.0f : 1.0f));
        }
        if (subStr[0]) metal(fi, subStr, ax, dY + 30.0f * K, 10.0f * K, 10.0f * K, hot, 0, 0, 0x000000, 1.5f * K, 0, 0.0f, a * clampE80);
    } else if (HS == 2) {   // ======== IMPACT ========
        const float ed = e - 60.0f;
        if (hasD) ripple(ax, dmgY, ed, 30.0f * K);
        if (hasD && ed >= 0.0f && ed < 540.0f) {
            addOn();
            if (U.scHitFx) {   // the radial lines, out of the number
                const float fd = 1.0f - ed / 540.0f, re = eo3(ed / 300.0f);
                for (int i = 0; i < 36; ++i) {
                    const float an = i * TAU / 36.0f + hrand(seed + 900u + i) * 0.1f, r0 = (34.0f + 50.0f * re) * K, r1 = r0 + (70.0f + 210.0f * hrand(seed + 940u + i)) * K * re;
                    seg_soft(dev, ax + cosf(an) * r0 * 1.6f, dmgY + sinf(an) * r0, ax + cosf(an) * r1 * 1.6f, dmgY + sinf(an) * r1, (0.8f + 2.4f * hrand(seed + 980u + i)) * K * fd + 0.4f, C(lerp(glow, 0xFFFFFF, 0.3f), 0.75f * fd * a));
                }
            }
            if (ed < 120.0f) soft_blob(dev, ax, dmgY, 180.0f * K, 95.0f * K, C(0xFFFFFF, 0.7f * (1.0f - ed / 120.0f) * a));
            addOff();
        }
        if (showName) {   // thin, wide-spaced, the plain face ; an underline slash drawn in
            const float zN = 11.0f * K, trk = zN * 0.55f; char ch[2] = { 0, 0 }; float tw = 0.0f; int n = 0; float cw[48];
            for (const char* c = nameStr; *c && n < 48; ++c, ++n) { ch[0] = *c; cw[n] = fo->measure(ch, zN); tw += cw[n] + (c[1] ? trk : 0.0f); }
            blend(dev, false); fo->begin(dev);
            float x = ax - tw * 0.5f;
            for (int i = 0; i < n; ++i) { ch[0] = nameStr[i]; fo->draw_c(dev, x + cw[i] * 0.5f, nameY, ch, zN, C(0xFFFFFF, a * clampE80), C(0x000000, a * clampE80), 1.6f * K); x += cw[i] + trk; }
            const float sl = eo3(e / 220.0f), ux = ax - tw * 0.5f - 10.0f * K, uw = (tw + 20.0f * K) * sl;
            addOn();
            seg_soft(dev, ux, nameY + 10.0f * K, ux + uw, nameY + 8.0f * K, 3.0f * K, C(glow, 0.5f * a));
            seg_soft(dev, ux, nameY + 10.0f * K, ux + uw, nameY + 8.0f * K, 1.1f * K, C(lerp(hot, 0xFFFFFF, 0.5f), a));
            addOff();
        }
        if (hasD && ed >= 0.0f) {
            const float sc = stampS(ed, 1.35f, 70.0f); float sx, sy; shk(ed, 3.5f * K, seed, sx, sy);
            const float z = 27.0f * K, zd = z * sc, tw = fi->measure(db, z) * sc;
            addOn(); soft_blob(dev, ax, dmgY, tw * 0.7f + 20.0f * K, zd * 0.75f, C(glow, 0.55f * a)); addOff();   // the halo
            const Band Bm[4] = { { -0.9f, -0.02f, 0xE6ECF6 }, { -0.9f, -0.24f, 0xFFFFFF }, { -0.02f, 0.08f, 0x4E5869 }, { 0.30f, 0.9f, 0x7D889A } };
            metal(fi, db, ax + sx, dmgY + sy, z, zd, 0xFFFFFF, Bm, 4, 0x05070C, 2.4f * K * sc, lerp(glow, 0xFFFFFF, 0.2f), 1.2f * K * sc, a);
        }
        if (subStr[0]) metal(fo, subStr, ax, dmgY + 26.0f * K, 9.0f * K, 9.0f * K, lerp(hot, 0xFFFFFF, 0.5f), 0, 0, 0x000000, 1.5f * K, 0, 0.0f, a * clampE80);
    } else {   // ======== CRITICAL ========
        if (showName) { const Band Bn[1] = { { -0.9f, -0.02f, 0xFFFFFF } };
            metal(fi, nameStr, ax, nameY, 13.0f * K, 13.0f * K, lerp(hot, 0xFFFFFF, 0.55f), Bn, 1, 0x000000, 1.6f * K, 0, 0.0f, a * clampE80); }
        if (hasD) {
            const float z = 25.0f * K; const int n = (int)strlen(db); float w[16], tw = 0.0f; char ch[2] = { 0, 0 };
            for (int i = 0; i < n; ++i) { ch[0] = db[i]; w[i] = fi->measure(ch, z); tw += w[i]; }
            addOn(); soft_blob(dev, ax, dmgY, tw * 0.8f + 30.0f * K, z * 1.1f, C(glow, 0.40f * (e < 300.0f ? (e > 100.0f ? (e - 100.0f) / 200.0f : 0.0f) : 1.0f) * a)); addOff();
            const Band Bd[2] = { { -0.9f, 0.02f, hot }, { -0.9f, -0.22f, lerp(hot, 0xFFFFFF, 0.8f) } };
            float x = ax - tw * 0.5f;
            for (int i = 0; i < n; ++i) {
                const float ed = e - 120.0f - i * 55.0f, sc = stampS(ed, 1.9f, 70.0f), cx = x + w[i] * 0.5f; x += w[i];
                if (sc <= 0.0f) continue;
                if (ed < 110.0f) { addOn(); soft_blob(dev, cx, dmgY, 26.0f * K, 26.0f * K, C(0xFFFFFF, 0.7f * (1.0f - ed / 110.0f) * a)); addOff(); }   // each digit's spark
                ch[0] = db[i];
                metal(fi, ch, cx, dmgY, z, z * sc, lerp(hot, 0xFFFFFF, 0.25f), Bd, 2, 0x000000, 2.4f * K * sc, deep, 1.1f * K * sc, a);
            }
            const float es = e - 120.0f - n * 55.0f;
            if (es > 0.0f) {   // the crit glint on the corner
                const float pp = es < 160.0f ? es / 160.0f : 1.0f, tw2 = 0.85f + 0.15f * sinf(e * 0.006f);
                addOn(); bat_begin();
                star(dev, ax + tw * 0.5f + 10.0f * K, dmgY - 12.0f * K, (30.0f + 18.0f * (1.0f - pp)) * K * tw2, C(0xFFFFFF, a));
                star(dev, ax + tw * 0.5f + 10.0f * K, dmgY - 12.0f * K, 16.0f * K * tw2, C(hot, a));
                bat_end(dev); addOff();
            }
        }
        if (subStr[0]) metal(fi, subStr, ax, dmgY + 25.0f * K, 9.5f * K, 9.5f * K, lerp(hot, 0xFFFFFF, 0.4f), 0, 0, 0x000000, 1.6f * K, 0, 0.0f, a * clampE80);
    }
    unclip(); blend(dev, false);
    sx_fs = 0; cqs(dev);
}
void circle_core(const Frame& f, const CircleIn& in) {
    const UiConfig& U = ui_config();
    const u32 dev = f.dev;
    const float CX = in.CX, CY = in.CY, R = in.R, k = in.k, t = in.t, bt = in.bt; const int P = in.P;
    const bool anim = U.scAnim != 0;
    sx_clock = t; sx_bt = bt;
    const float titleOff = (U.scTitleOff < -120.0f ? -120.0f : (U.scTitleOff > 80.0f ? 80.0f : U.scTitleOff)) * k;   // > 0 higher ; < 0 lower, over the sphere if wanted
    ensure_raw_tex(dev, sx_elem, sx_elemR, plugin_path_r("assets\\element_icons.raw"), 256, 32);
    ensure_gen(dev);
    Font* fT = te_font(f, U.scText[SC_TITLE]); Font* fM = te_font(f, U.scText[SC_TIMER]);
    if (!fT || !fM) return;
    const u32 GREY = 0x7D8696, RED = 0xFF6B5F, GREEN = 0x58E08A, GOLD = 0xFFD76A;
    const bool late = in.phase == 1 && in.frac < 0.2f, ready = in.tp >= 1000;
    const u32 flash = (sinf(t * 30.0f) > 0.0f) ? RED : 0xFFB04F;
    char num[16]; _snprintf(num, sizeof(num), "%.1f", in.timer < 0.0f ? 0.0f : in.timer);
    cqs(dev);
    if (P < 0) {   // ================= OPENED : a lone move, no skillchain yet -- a neutral grey sphere =================
        const u32 st = in.phase == 0 ? RED : late ? flash : ready ? GOLD : GREEN;
        medallion_base(dev, CX, CY, R, k, t, GREY, in.frac, st); cqs(dev);
        ring_sector(dev, CX, CY, R - 3.5f * k, R + 3.5f * k, 0.0f, TAU, C(GREY, in.phase == 0 ? 0.5f * (0.85f + 0.15f * sinf(t * 8.0f)) : 1.0f));
        if (in.phase == 1 && ready && !late) { const float pu = 0.5f + 0.4f * sinf(t * 9.0f); ring_sector(dev, CX, CY, R + 4.2f * k, R + 5.8f * k, 0.0f, TAU, C(GOLD, pu)); }
        if (U.scTitle && in.opener && in.opener[0]) {   // the opening move : the same plate as a skillchain's, in steel, no level
            char up[32]; _snprintf(up, sizeof(up), "%s", in.opener); for (char* c = up; *c; ++c) if (*c >= 'a' && *c <= 'z') *c = (char)(*c - 'a' + 'A');
            const TextStyle& tt = U.scText[SC_TITLE];
            title_plate(dev, fT, CX, CY - R - 50.0f * k - titleOff, k, t, up, 0, 0, tt.colorOn ? (tt.color & 0x00FFFFFFu) : 0x1C2028, 1.0f);   // steel, the name engraved
        }   // the SAME place as a skillchain's title : nothing jumps when the chain forms, no empty band above
        medallion_text(dev, fM, CX, CY, k, in.phase == 0 ? tr("WAIT", "ATTENDS") : "GO", in.phase == 0 ? 0xFF8A7F : st, num, in.phase == 0 ? 0xFF8A7F : st, in.tp, U.scTimer != 0, U.scTP != 0);
        if (U.scList) list_draw(f, dev, in.items, in.nItems, CX, CY, R, k, in.kl, t);
        fx_end(dev);
        hit_fx(f, dev, in);
        return;
    }
    // ================= the SKILLCHAIN formed : its animation =================
    const SCInfo& SI = sc_info(P); const Pal Q = pal(P);
    const float kk = 1.0f - (bt / 8.0f < 1.0f ? (bt > 0.0f ? bt / 8.0f : 0.0f) : 1.0f), b7 = bt / 0.7f;
    const float rise = b7 >= 1.0f ? 1.0f : (b7 <= 0.0f ? 0.0f : b7 * b7 * (3.0f - 2.0f * b7));
    const float A = (0.55f + 0.45f * kk) * (0.12f + 0.88f * rise);   // the loop's strength : rising out of the blast
    sx_rise = 0.12f + 0.88f * rise;
    sx_dOk = false;
    if (anim) {
        if (U.scDistort && !sx_iso && !water_variant(P)) sx_dOk = distort(dev, CX, CY, R, k, t, warp_of(P, bt, t, R, k));
        if (!Q.dark && !sx_iso && !water_variant(P)) { const bool fw = (P == SCP_Reverberation && sx_rv == REV_A2) || lq_new(P) || im_new(P);   // F : a stronger blue AURA (the colour says "water")
            bloom(dev, CX, CY, R * (1.0f + ((fw ? 1.6f : 1.3f) + 0.15f * sinf(t * 2.1f)) * REACH), C(Q.mid, (fw ? 0.55f : 0.3f) * A)); }       // the breath behind it
        if (bt >= 0.0f && bt < 0.6f && !sx_iso && !water_variant(P) && !(P == SCP_Reverberation && sx_rv == 2)) {   // the blast (Reverberation C : its own contact instead) : the SPHERE explodes -- it flashes, a big pulse, a torn shockwave
            const float u = bt / 0.6f, e = (1.0f - u) * (1.0f - u);
            if (Q.dark) bloom(dev, CX, CY, R * (1.4f + 2.0f * u), C(Q.cold, 0.9f * e), false);
            bloom(dev, CX, CY, R * (1.4f + 2.2f * u * sx_lvf * REACH), C(Q.hot, (Q.dark ? 0.5f : 0.95f) * e * (sx_lvf < 1.0f ? sx_lvf + 0.2f : 1.0f)));
            if (!sx_dOk) shockwave(dev, CX, CY, R, k, u, t, Q.hot, Q.mid, 1.6f * sx_lvf * REACH);
            if (!sx_dOk && SI.lvl >= 2) shockwave(dev, CX, CY, R, k, u * 0.8f, t + 3.0f, elem_rgb(SI.elem[1 % SI.nElem]), Q.mid, 1.2f * sx_lvf * REACH);   // a Lv.2+ : a second wave, its other element
            if (SI.lvl >= 3 && u < 0.5f) bloom(dev, CX, CY, R * (1.0f + (2.0f + 3.0f * u) * REACH), C(0xFFFFFF, (Q.dark ? 0.0f : 0.6f) * (1.0f - u * 2.0f)));   // a Lv.3+ : a blinding flash
            if (u < 0.3f) bloom(dev, CX, CY, R * 1.25f, C(Q.hot, 0.95f * (1.0f - u / 0.3f)));                        // the sphere itself flashes
            { const float ex = 1.0f - (1.0f - u) * (1.0f - u) * (1.0f - u), fa = powf(1.0f - u, 1.3f);              // the BIG PULSE : a thick ring flung out
              if (im_new(P)) { }   // Impaction B : no wave -- its Tesla arcs leap out instead
              else if ((P == SCP_Reverberation && sx_rv == REV_A2) || lq_new(P)) water_ripple(dev, CX, CY, R + ex * R * 0.9f * REACH, ((sx_dOk ? 3.0f : 9.0f) * (1.0f - u) + 1.5f) * k, k, Q.mid, 0.9f * fa, 0.7f, t);   // F : a wave of water, kept near the sphere
              else pulse_ring(dev, CX, CY, R + ex * R * 2.0f * REACH, ((sx_dOk ? 3.0f : 12.0f) * (1.0f - u) + 1.0f) * k, lerp(Q.mid, 0xFFFFFF, 0.5f), (sx_dOk ? 0.55f : 0.9f) * fa);
              sx_rippleTex = 0; sx_rippleJag = false; }
        }
        fx_property(dev, P, CX, CY, R, k, t, bt, A, false);
        if (!sx_iso) draw_parts(dev, P, k);
        fx_end(dev);
    }
    const bool cWait = in.phase == 0, closed = in.closed || in.phase == 2, ok2 = ready && in.phase == 1;
    const u32 gcol = closed ? 0xFFFFFF : late ? flash : ok2 ? GOLD : GREEN;
    static const MatterRim WATER = { 0x061A3A, 0x0C3266, 0x7FC8FF, 0x2E8CFF, 0x0A2A6A, 0x3A7FD0 };
    static const MatterRim LAVA  = { 0x2A0804, 0x6A1A06, 0xFFB04A, 0xD8320C, 0x3A0602, 0xA0401A };
    if (P == SCP_Reverberation && sx_rv == REV_A2) medallion_matter(dev, CX, CY, R, k, t, in.frac, cWait ? RED : gcol, WATER);   // the centre made of water
    else if (lq_new(P)) medallion_matter(dev, CX, CY, R, k, t, in.frac, cWait ? RED : gcol, LAVA);                               // the centre made of lava
    else if (im_new(P)) {   // the centre CHARGED : violet depth, the ring's filaments crawling in over the rim
        static const MatterRim VOLT = { 0x1A0A3A, 0x3A1A7A, 0xE6D8FF, 0x9A6AFF, 0x2A0A6A, 0x7A5AD0 };
        sx_ringTex = sx_noise; medallion_matter(dev, CX, CY, R, k, t, in.frac, cWait ? RED : gcol, VOLT); sx_ringTex = 0;
    }
    else medallion_base(dev, CX, CY, R, k, t, Q.mid, in.frac, cWait ? RED : gcol);
    cqs(dev);
    if (!anim) {   // still : the ring lit in the chain's elements (each centred on its burst icon)
        const int n = SI.nElem < 1 ? 1 : (SI.nElem > 4 ? 4 : SI.nElem); const float half = PI / n;
        ring_sector(dev, CX, CY, R - 3.5f * k, R + 3.5f * k, 0.0f, TAU, C(Q.mid, 0.22f));
        for (int i = 0; i < n; ++i) { float ex, ey; elem_pos(i, n, CX, CY, R, k, ex, ey); const float c0 = atan2f(ey - CY, ex - CX); ring_sector(dev, CX, CY, R - 3.5f * k, R + 3.5f * k, c0 - half, c0 + half, C(elem_rgb(SI.elem[i]), 1.0f)); }
    }
    // (the gold "WS ready" ring round the sphere is GONE, 2026-11-23 : the TP text already turns gold at 1000 -- a perfect
    // circle pulsing over every animation only polluted it)
    if (anim) fx_property(dev, P, CX, CY, R, k, t, bt, A, true);   // what passes IN FRONT of the ring
    fx_end(dev);
    if (U.scProps) {   // the burst elements : around the ring, opaque, black border -- always readable over the effects
        const int n = SI.nElem < 1 ? 1 : (SI.nElem > 4 ? 4 : SI.nElem);
        for (int i = 0; i < n; ++i) {
            float ix, iy; elem_pos(i, n, CX, CY, R, k, ix, iy); const float pul = 1.0f + 0.05f * sinf(t * 5.0f + i);
            const u32 ec = elem_rgb(SI.elem[i]);
            bloom(dev, ix, iy, 20.0f * k, C(ec, 0.5f)); fx_end(dev);
            disc(dev, ix, iy, 12.4f * k, 0xFF000000u);
            disc(dev, ix, iy, 11.6f * k, C(ec, 0.9f));
            disc(dev, ix, iy, 10.6f * k, 0xFF0C1018u);
            icon(dev, SI.elem[i], ix, iy, 18.0f * pul * k, 1.0f);
        }
    }
    if (U.scTitle) {
        char up[24]; _snprintf(up, sizeof(up), "%s", scprop_name(P)); for (char* c = up; *c; ++c) if (*c >= 'a' && *c <= 'z') *c = (char)(*c - 'a' + 'A');
        const float pop = (bt >= 0.0f && bt < 0.3f) ? 1.0f + 0.3f * sinf(bt / 0.3f * PI) : 1.0f;
        const TextStyle& tt = U.scText[SC_TITLE];
        title_plate(dev, fT, CX, CY - R - 50.0f * k - titleOff, k, t, up, SI.lvl, &Q, tt.colorOn ? (tt.color & 0x00FFFFFFu) : (Q.dark ? Q.hot : lerp(Q.hot, 0xFFF0C0, 0.3f)), pop);
    }
    medallion_text(dev, fM, CX, CY, k, closed ? "BURST" : cWait ? tr("WAIT", "ATTENDS") : "GO", cWait ? 0xFF8A7F : (closed ? Q.hot : gcol), num, cWait ? 0xFF8A7F : (closed ? Q.mid : gcol), in.tp, U.scTimer != 0, U.scTP != 0);
    if (!closed && U.scList) list_draw(f, dev, in.items, in.nItems, CX, CY, R, k, in.kl, t);
    fx_end(dev);
    hit_fx(f, dev, in);
}

} // namespace

bool scfx_toggle(const char* arg, char* msg, int cap) {
    char prop[32] = {}; float z = 0.0f;
    char w2[16] = {}, w3[16] = {}, w4[16] = {};
    if (arg) sscanf(arg, "%31s %15s %15s %15s", prop, w2, w3, w4);
    const bool keepLoop = sx_loop; const char* W[3] = { w2, w3, w4 };
    sx_loop = false; const bool iso = !_stricmp(w2, "solo") || !_stricmp(w3, "solo") || !_stricmp(w4, "solo");   // //aio scfx <name> [loop] [matter] [scale]
    for (int i = 0; i < 3; ++i) { if (!_stricmp(W[i], "loop") || !_stricmp(W[i], "solo")) sx_loop = true; else if (W[i][0] >= '0' && W[i][0] <= '9') z = (float)atof(W[i]); }
    for (char* c = prop; *c; ++c) if (*c >= 'A' && *c <= 'Z') *c = (char)(*c - 'A' + 'a');
    if (!strcmp(prop, "variant") || !strcmp(prop, "shot") || !strcmp(prop, "capture")) sx_loop = keepLoop;   // these act ON the running demo : its loop stays as it was (a "shot" used to switch it off, and its restart never ran)
    if (!strcmp(prop, "size") || !strcmp(prop, "list")) {   // //aio scfx size <x> : the sphere + animation ; //aio scfx list <x> : the list
        sx_loop = keepLoop; const bool isList = prop[0] == 'l'; const float v = (float)atof(w2);
        float& z2 = isList ? ui_config().scListScale : ui_config().scScale;   // the config's own (Skillchains > Display)
        const float lo = isList ? 0.5f : 0.4f, hi = isList ? 2.0f : 3.0f;
        if (v >= lo && v <= hi) z2 = v;
        _snprintf(msg, cap, ">>> scfx %s scale %.2f (%.1f .. %.1f) -- the Skillchains box's own setting <<<", isList ? "list" : "size", z2, lo, hi);
        return sx_on;
    }
    if (!strcmp(prop, "variant")) {   // //aio scfx variant a|b|c|d : Reverberation's A / B / C (dev captures ; the live box too)
        if ((w2[0] | 0x20) == 'l' && w2[1]) {   // //aio scfx variant liq a|b : Liquefaction
            const char lv = (char)(w3[0] | 0x20); if (lv == 'a' || lv == 'b') sx_lq = lv - 'a';
            _snprintf(msg, cap, ">>> scfx variant : Liquefaction %c (%s) <<<", 'A' + sx_lq, sx_lq ? "B lava : cracks, the centre made of lava, heavy drops" : "A the original"); return sx_on;
        }
        const char v = (char)(w2[0] | 0x20);
        if (v >= 'a' && v <= 'f') sx_rv = v - 'a';
        static const char* NM[6] = { "A the original", "B waves out + bubbles cut", "C = B + the contact and its crests", "D water surface and radial echoes", "E water, trapped waves bouncing, splashes", "F = A, water blue, shorter circles" };
        _snprintf(msg, cap, ">>> scfx variant %c : Reverberation %s <<<", 'A' + sx_rv, NM[sx_rv]);
        return sx_on;
    }
    if (!strcmp(prop, "capture")) {   // //aio scfx capture <skillchain> [a|b|c|d] [scale] : EVERYTHING in one go
        // the skillchain's own effect alone (solo, looping), the variant, the size ; then two series, each in its own
        // folder : the formation up close (20 photos over 0.8 s) and the whole cycle (20 over 2.6 s)
        char nm2[32]; lstrcpynA(nm2, w2, sizeof(nm2)); for (char* c = nm2; *c; ++c) if (*c >= 'A' && *c <= 'Z') *c = (char)(*c - 'A' + 'a');
        int fnd = -1;
        for (int i = 0; nm2[0] && i < NSCENES; ++i) { char nm[32]; _snprintf(nm, sizeof(nm), "%s", scprop_name(SCENES[i].prop)); for (char* c = nm; *c; ++c) if (*c >= 'A' && *c <= 'Z') *c = (char)(*c - 'A' + 'a');
            if (!strncmp(nm, nm2, strlen(nm2))) { fnd = i; break; } }
        if (fnd < 0) { _snprintf(msg, cap, ">>> scfx capture <skillchain> [a|b|c|d] [scale] -- e.g. //aio scfx capture reverb c <<<"); return sx_on; }
        float zz = 2.5f; char vv = 0; bool full = false;
        for (int i = 1; i < 3; ++i) { if (W[i][0] >= '0' && W[i][0] <= '9') zz = (float)atof(W[i]); else if (!_stricmp(W[i], "full")) full = true; else if (W[i][0] && !W[i][1]) vv = (char)(W[i][0] | 0x20); }
        { char w5[16] = {}; if (arg) sscanf(arg, "%*s %*s %*s %*s %15s", w5); if (!_stricmp(w5, "full")) full = true; }   // full may come 4th
        int* pv = var_of(SCENES[fnd].prop);   // the captured property's own variant (a letter never touches another property's)
        if (pv && vv >= 'a' && vv <= 'f') *pv = vv - 'a';
        const char cur = (char)('a' + (pv ? *pv : 0));
        if (zz >= 0.4f && zz <= 3.0f) ui_config().scScale = zz;
        sx_on = true; sx_all = false; sx_loop = true; sx_iso = !full; sx_scene = fnd; sx_np = 0; sx_last.QuadPart = 0;
        sx_t = FIRE_AT - 0.0001f - 0.3f;   // 0.3 s ahead : the first photo lands on the formation
        char base[24]; _snprintf(base, sizeof(base), "%s", scprop_name(SCENES[fnd].prop)); for (char* c = base; *c; ++c) if (*c >= 'A' && *c <= 'Z') *c = (char)(*c - 'A' + 'a');
        _snprintf(sx_shotSub, sizeof(sx_shotSub), "%s_%c%s_formation", base, cur, full ? "_full" : "");
        sx_shotN = 20; sx_shotI = 0; sx_shotSpan = 0.8f; sx_shotT0 = GetTickCount() + 300u;
        sx_shotQn = 1; sx_shotQi = 0; sx_shotQ[0].n = 20; sx_shotQ[0].span = 2.6f; _snprintf(sx_shotQ[0].sub, sizeof(sx_shotQ[0].sub), "%s_%c%s_cycle", base, cur, full ? "_full" : "");
        _snprintf(msg, cap, ">>> scfx capture %s, variant %c, scale %.1f : 2 series (formation 0.8 s, cycle 2.6 s) -> data\\scfxshot\\%s_%c_* -- wait ~5 s <<<",
                  scprop_name(SCENES[fnd].prop), cur - 32, (double)ui_config().scScale, base, cur);
        return true;
    }
    if (!strcmp(prop, "shot")) {   // //aio scfx shot [n] [seconds] : photograph the running demo
        if (!sx_on) { _snprintf(msg, cap, ">>> scfx shot : start a demo first (//aio scfx reverb matter 2.5) <<<"); return false; }
        int n = atoi(w2); if (n < 1) n = 16; if (n > 60) n = 60;
        float sp = (float)atof(w3); if (sp < 0.2f) sp = 2.6f; if (sp > 20.0f) sp = 20.0f;
        sx_shotN = n; sx_shotI = 0; sx_shotSpan = sp; sx_shotT0 = GetTickCount(); sx_shotSub[0] = 0; sx_shotQn = sx_shotQi = 0;
        if (sx_loop) { sx_t = FIRE_AT - 0.0001f; sx_np = 0; sx_last.QuadPart = 0; }   // a looping demo restarts : frame 0 = the formation, every series comparable
        _snprintf(msg, cap, ">>> scfx shot : %d frame(s) over %.1f s -> plugins\\AioHud\\data\\scfxshot\\ <<<", n, (double)sp);
        return true;
    }
    if (!prop[0] || !strcmp(prop, "off")) { sx_on = false; sx_iso = false; _snprintf(msg, cap, ">>> scfx off <<<"); return false; }
    int found = -1; sx_all = !strcmp(prop, "all"); if (sx_all) sx_loop = true;   // all = the skillchains alone, one after another
    if (sx_all) found = 0;
    else for (int i = 0; i < NSCENES; ++i) {
        char nm[32]; _snprintf(nm, sizeof(nm), "%s", scprop_name(SCENES[i].prop));
        for (char* c = nm; *c; ++c) if (*c >= 'A' && *c <= 'Z') *c = (char)(*c - 'A' + 'a');
        if (!strncmp(nm, prop, strlen(prop))) { found = i; break; }   // a prefix is enough : liq, imp, det, sci, ...
    }
    if (found < 0) { _snprintf(msg, cap, ">>> scfx : unknown skillchain '%s' (liquefaction, impaction, ... umbra, or all) <<<", prop); return sx_on; }
    if (z >= 0.4f && z <= 3.0f) ui_config().scScale = z;   // the Skillchains box's sphere size
    sx_iso = iso;   // the property's own effect alone, looping
    sx_on = true; sx_scene = found; sx_t = sx_loop ? FIRE_AT - 0.0001f : 0.0f; sx_np = 0; sx_last.QuadPart = 0;
    if (iso) { _snprintf(msg, cap, ">>> scfx %s SOLO (scale %.2f) : the property's own effect alone, looping -- //aio scfx size <0.4..3> to resize, //aio scfx off to stop <<<", scprop_name(SCENES[found].prop), ui_config().scScale); return true; }
    if (sx_all) _snprintf(msg, cap, ">>> scfx ALL (scale %.2f) : every skillchain in turn -- //aio scfx off to stop <<<", ui_config().scScale);
    else if (sx_loop) _snprintf(msg, cap, ">>> scfx %s LOOP (scale %.2f) : the animation alone, over and over -- //aio scfx off to stop <<<", scprop_name(SCENES[found].prop), ui_config().scScale);
    else _snprintf(msg, cap, ">>> scfx %s (scale %.2f) : %s opens, %s closes -- //aio scfx off to stop <<<", scprop_name(SCENES[found].prop), ui_config().scScale, SCENES[found].opener, SCENES[found].closer);
    return true;
}

void scfx_forget()  { sx_dTex = sx_dSurf = 0; sx_dTS = 0; sx_dFail = 0; sx_dOff = false; sx_dNext = 0; sx_elem = sx_noise = sx_soft = sx_atlas = sx_noiseS = sx_caus = 0; sx_ringTex = 0; sx_elemR = TexRetry(); sx_genTries = 0; sx_genNext = 0; }
void scfx_dispose() {
    if (sx_elem) release_texture(sx_elem);
    if (sx_noise) release_texture(sx_noise);
    if (sx_noiseS) release_texture(sx_noiseS);
    if (sx_caus) release_texture(sx_caus);
    if (sx_soft) release_texture(sx_soft);
    if (sx_atlas) release_texture(sx_atlas);
    dist_release();
    scfx_forget();
}

// one log line per animation cycle : its frames, CPU time of this function, its own draws (ring_sector / disc /
// fonts not counted) -- so each skillchain's cost reads straight out of //aio scfx all
struct Perf { int scene, frames, sumUs, maxUs, sumDraw, maxDraw, sumVtx, maxParts; float lastT; };
static Perf sx_perf = { -1 };
static void perf_flush() {
    Perf& q = sx_perf;
    if (q.scene >= 0 && q.frames > 0)
        windower::debug::log("SCFX perf %s Lv%d : %d frames, cpu avg %d us max %d us, own draws avg %d max %d, tris avg %d, parts max %d",
            scprop_name(SCENES[q.scene].prop), sc_info(SCENES[q.scene].prop).lvl, q.frames, q.sumUs / q.frames, q.maxUs,
            q.sumDraw / q.frames, q.maxDraw, q.sumVtx / q.frames, q.maxParts);
    const int sc = sx_scene; q = Perf(); q.scene = sc;
}
static void scfx_draw_inner(const Frame& f, float screenW, float screenH);
// ---- //aio scfx shot [n] [seconds] : the demo's sphere PHOTOGRAPHED n times over `seconds`, as BMP files in
// data\scfxshot\ -- so the animation can be looked at frame by frame outside the game. The render target's square round
// the sphere is copied into a system-memory surface (CopyRects), locked, written. Dev only (the scfx demo).
bool shot_one(u32 dev, int idx) {
    auto getrt = vmethod<long(__stdcall*)(u32, u32*)>(dev, 32);   // GetRenderTarget
    u32 srf = 0; if (!getrt || getrt(dev, &srf) < 0 || !valid_ptr(srf)) { windower::debug::log("SCFXSHOT %d : no render target", idx); return false; }
    u32 desc[8] = { 0 }; auto gd = vmethod<long(__stdcall*)(u32, void*)>(srf, 8);
    if (!gd || gd(srf, desc) < 0) { d_rel(srf); return false; }
    const u32 fmt = desc[0]; const int W = (int)desc[6], H = (int)desc[7];
    if (fmt != 21 && fmt != 22) { d_rel(srf); windower::debug::log("SCFXSHOT %d : render target format %u not 32-bit -- not supported", idx, fmt); return false; }
    int x0 = (int)(sx_shotCX - sx_shotHalf), y0 = (int)(sx_shotCY - sx_shotHalf), w = (int)(2.0f * sx_shotHalf), h = w;
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x0 + w > W) w = W - x0; if (y0 + h > H) h = H - y0;
    if (w < 8 || h < 8) { d_rel(srf); return false; }
    auto cis = vmethod<long(__stdcall*)(u32, u32, u32, u32, u32*)>(dev, 27);   // CreateImageSurface (system memory)
    u32 img = 0; long hr = cis ? cis(dev, (u32)w, (u32)h, fmt, &img) : -1;
    if (hr < 0 || !valid_ptr(img)) { d_rel(srf); windower::debug::log("SCFXSHOT %d : CreateImageSurface %08X", idx, (unsigned)hr); return false; }
    RECT rc = { x0, y0, x0 + w, y0 + h }; POINT pt = { 0, 0 };
    auto cr = vmethod<long(__stdcall*)(u32, u32, const RECT*, u32, u32, const POINT*)>(dev, 28);   // CopyRects
    hr = cr ? cr(dev, srf, &rc, 1, img, &pt) : -1;
    d_rel(srf);
    if (hr < 0) { d_rel(img); windower::debug::log("SCFXSHOT %d : CopyRects %08X", idx, (unsigned)hr); return false; }
    struct LR { int pitch; void* bits; } lr = { 0, 0 };
    auto lock = vmethod<long(__stdcall*)(u32, LR*, const RECT*, u32)>(img, 9), unlock = (decltype(lock))0;
    auto unl = vmethod<long(__stdcall*)(u32)>(img, 10);
    if (!lock || lock(img, &lr, 0, 0x10 /* READONLY */) < 0 || !lr.bits) { d_rel(img); windower::debug::log("SCFXSHOT %d : LockRect failed", idx); return false; }
    (void)unlock;
    char dir[MAX_PATH]; plugin_path(dir, sizeof(dir), "data\\scfxshot"); CreateDirectoryA(dir, 0);
    if (sx_shotSub[0]) { const size_t l = strlen(dir); _snprintf(dir + l, sizeof(dir) - l, "\\%s", sx_shotSub); dir[sizeof(dir) - 1] = 0; CreateDirectoryA(dir, 0); }
    char path[MAX_PATH]; _snprintf(path, sizeof(path), "%s\\f%02d.bmp", dir, idx); path[sizeof(path) - 1] = 0;
    bool ok = false;
    if (FILE* fp = fopen(path, "wb")) {
        const unsigned rowB = (unsigned)w * 4, imgB = rowB * (unsigned)h;
        unsigned char fh[14] = { 'B', 'M' }; const unsigned fsz = 54 + imgB, off = 54;
        memcpy(fh + 2, &fsz, 4); memcpy(fh + 10, &off, 4);
        unsigned char ih[40] = { 0 }; const int hh = -h; const unsigned sz40 = 40; const unsigned short pl = 1, bpp = 32;
        memcpy(ih, &sz40, 4); memcpy(ih + 4, &w, 4); memcpy(ih + 8, &hh, 4); memcpy(ih + 12, &pl, 2); memcpy(ih + 14, &bpp, 2); memcpy(ih + 20, &imgB, 4);
        fwrite(fh, 1, 14, fp); fwrite(ih, 1, 40, fp);
        for (int y = 0; y < h; ++y) fwrite((const unsigned char*)lr.bits + y * lr.pitch, 1, rowB, fp);
        fclose(fp); ok = true;
    }
    if (unl) unl(img);
    d_rel(img);
    if (!ok) windower::debug::log("SCFXSHOT %d : cannot write %s", idx, path);
    return ok;
}
void scfx_draw(const Frame& f, float screenW, float screenH) {
    if (!sx_on || !f.dev || !f.font) { if (sx_perf.frames) perf_flush(); sx_perf.scene = -1; return; }   // the copy : sc_circle_idle gives it back
    LARGE_INTEGER a, b; QueryPerformanceCounter(&a);
    sx_pDraw = sx_pVtx = 0; sx_fs = 0;   // the rest of the HUD drew in between : nothing is known
    scfx_draw_inner(f, screenW, screenH);
    if (sx_shotN > 0 && sx_shotI < sx_shotN) {   // //aio scfx shot : the next photo when it is due
        const unsigned due = sx_shotT0 + (unsigned)(sx_shotSpan * 1000.0f * sx_shotI / sx_shotN);
        if ((int)(GetTickCount() - due) >= 0) {
            shot_one(f.dev, sx_shotI); ++sx_shotI;
            if (sx_shotI >= sx_shotN) { char dir[MAX_PATH]; plugin_path(dir, sizeof(dir), "data\\scfxshot");
                windower::debug::log("SCFXSHOT done : %d frame(s) over %d ms in %s\\%s (%s, Reverberation variant %c, solo %d)", sx_shotN, (int)(sx_shotSpan * 1000.0f), dir, sx_shotSub,
                                     scprop_name(SCENES[sx_scene].prop), 'A' + sx_rv, sx_iso ? 1 : 0); sx_shotN = 0;   // (no %f in debug::log)
                if (sx_shotQi < sx_shotQn) {   // the next series : the demo restarts, so its frame 0 is the formation again
                    const ShotSeries& q = sx_shotQ[sx_shotQi++];
                    sx_shotN = q.n; sx_shotI = 0; sx_shotSpan = q.span; lstrcpynA(sx_shotSub, q.sub, sizeof(sx_shotSub)); sx_shotT0 = GetTickCount() + 300u;
                    sx_t = FIRE_AT - 0.0001f - 0.3f; sx_np = 0;   // 0.3 s ahead : its first photo lands on the formation
                } else windower::debug::log("SCFXSHOT capture complete");
            }
        }
    }
    QueryPerformanceCounter(&b);
    if (sx_perf.scene != sx_scene || sx_t < sx_perf.lastT) perf_flush();   // a new cycle : the last one's line
    Perf& q = sx_perf; const int us = sx_freq.QuadPart ? (int)((b.QuadPart - a.QuadPart) * 1000000 / sx_freq.QuadPart) : 0;
    ++q.frames; q.sumUs += us; if (us > q.maxUs) q.maxUs = us; q.sumDraw += sx_pDraw; if (sx_pDraw > q.maxDraw) q.maxDraw = sx_pDraw;
    q.sumVtx += sx_pVtx; if (sx_np > q.maxParts) q.maxParts = sx_np; q.lastT = sx_t;
}
static void scfx_draw_inner(const Frame& f, float screenW, float screenH) {
    if (!sx_dTex && !sx_dOff && f.dev) { u32 rt = 0; int W = 0, H = 0; if (dist_ensure(f.dev, 512, rt, W, H)) d_rel(rt); }   // made NOW, not at the blast
    const u32 dev = f.dev;
    if (!sx_freq.QuadPart) QueryPerformanceFrequency(&sx_freq);
    LARGE_INTEGER now; QueryPerformanceCounter(&now);
    float dt = sx_last.QuadPart ? (float)(now.QuadPart - sx_last.QuadPart) / (float)sx_freq.QuadPart : 0.0f;
    sx_last = now; if (dt > 0.05f) dt = 0.05f;

    const UiConfig& U = ui_config();
    const float sc = U.scScale < 0.4f ? 0.4f : (U.scScale > 3.0f ? 3.0f : U.scScale), ls = U.scListScale < 0.5f ? 0.5f : (U.scListScale > 2.0f ? 2.0f : U.scListScale);
    sx_zoom = 1.25f * sc; sx_lzoom = 1.25f * ls;
    const float k = screenH / 1349.0f * sx_zoom, kl = screenH / 1349.0f * sx_lzoom;
    const float R = sc_circle_radius(k), cw = 2.0f * (R + 40.0f * k), top = R + 58.0f * k;
    const float px = box_grow_x(screenW, UiConfig::GB_SC, 0, U.scX, cw), py = snap(U.scY * screenH);
    const float CX = snap(px + cw * 0.5f), CY = snap(py + top);   // the live box's own place
    sx_shotCX = CX; sx_shotCY = CY; sx_shotHalf = R * 2.6f;   // //aio scfx shot : the square round the sphere and its effects
    REACH = U.scReach < 0.3f ? 0.3f : (U.scReach > 1.0f ? 1.0f : U.scReach);
    sx_partMul = U.scParticles < 0.0f ? 0.0f : (U.scParticles > 1.0f ? 1.0f : U.scParticles);
    update(dt, CX, CY, R, k);
    const Scene& S = SCENES[sx_scene]; const int P = S.prop; const SCInfo& SI = sc_info(P);
    const float t = sx_t, bt = t - FIRE_AT;
    // the scene's rows (per skillchain, its moves) -> one entry per move, its best result, best first
    const Row* rows = t < FIRE_AT ? S.pre : S.next; const int nr = t < FIRE_AT ? S.npre : S.nnext;
    ScCircleItem it[32]; int ni = 0;
    for (int i = 0; i < nr; ++i) for (int w = 0; w < rows[i].n; ++w) {
        const char* nm = rows[i].ws[w]; int f2 = -1;
        for (int j = 0; j < ni; ++j) if (!strcmp(it[j].ws, nm)) f2 = j;
        if (f2 >= 0) { if (rows[i].lvl > it[f2].lvl) { it[f2].lvl = (unsigned char)rows[i].lvl; it[f2].prop = (unsigned char)rows[i].prop; } }
        else if (ni < 32) { it[ni].ws = nm; it[ni].prop = (unsigned char)rows[i].prop; it[ni].lvl = (unsigned char)rows[i].lvl; it[ni].other = 0; it[ni].wsk = 0; ++ni; }
    }
    for (int i = 1; i < ni; ++i) { const ScCircleItem e = it[i]; int j = i - 1; while (j >= 0 && it[j].lvl < e.lvl) { it[j + 1] = it[j]; --j; } it[j + 1] = e; }
    CircleIn in = {};
    in.CX = CX; in.CY = CY; in.R = R; in.k = k; in.kl = kl; in.t = t; in.items = it; in.nItems = ni; in.preview = false;
    if (t < FIRE_AT) {   // the opener : a weapon skill, no skillchain yet
        const bool go = t >= T_WAIT; const float gt = t - T_WAIT;
        in.P = -1; in.bt = -1.0f; in.closed = false; in.phase = go ? 1 : 0;
        in.timer = go ? T_GO - gt : T_WAIT - t; in.frac = go ? (T_GO - gt) / T_GO : 1.0f;
        in.tp = (int)(t * 230.0f) > 3000 ? 3000 : (int)(t * 230.0f); in.opener = S.opener;
        if (go && gt < 0.2f) { bloom(dev, CX, CY, R * 1.6f, C(0xFFFFFF, 0.5f * (1.0f - gt / 0.2f))); fx_end(dev); }
        circle_core(f, in);
    } else if (bt < T_BURST) {   // the skillchain formed
        const bool closed = SI.lvl >= 4 || S.nnext == 0, cWait = !closed && bt < CW;
        in.P = P; in.bt = bt; in.closed = closed; in.phase = closed ? 2 : (cWait ? 0 : 1);
        in.timer = cWait ? CW - bt : T_BURST - bt; in.frac = cWait ? 1.0f : (T_BURST - bt) / (closed ? T_BURST : T_BURST - CW);
        in.tp = (int)(bt * 230.0f) > 1000 ? 1000 : (int)(bt * 230.0f); in.opener = S.closer;
        circle_core(f, in);
    } else {
        draw_parts(dev, P, k);
    }
    fx_end(dev);
}

// ---------------------------------------------------------------- the LIVE circle (the Skillchains box) ----
void sc_circle_draw(const Frame& f, const ScCircleLive& L) {
    if (!f.dev || (!f.font && !f.fonts)) return;
    PERF_SCOPE("sc_spheres");   // //aio perf : the resonance spheres, nested inside the "skillchains" row (audit P9)
    const UiConfig& U = ui_config();
    const u32 dev = f.dev;
    REACH = U.scReach < 0.3f ? 0.3f : (U.scReach > 1.0f ? 1.0f : U.scReach);
    sx_partMul = U.scParticles < 0.0f ? 0.0f : (U.scParticles > 1.0f ? 1.0f : U.scParticles);
    sx_cUse = GetTickCount();
    if (U.scAnim && U.scDistort && !sx_dTex && !sx_dOff) { u32 rt = 0; int W = 0, H = 0; if (dist_ensure(dev, 512, rt, W, H)) d_rel(rt); }   // made at the first sight of a window, not at the blast
    sx_fs = 0;
    if (!sx_freq.QuadPart) QueryPerformanceFrequency(&sx_freq);
    static LARGE_INTEGER s_last = {};
    LARGE_INTEGER now; QueryPerformanceCounter(&now);
    float dt = s_last.QuadPart ? (float)(now.QuadPart - s_last.QuadPart) / (float)sx_freq.QuadPart : 0.0f;
    s_last = now; if (dt > 0.05f) dt = 0.05f;
    static unsigned s_key = 0; static float s_bt = -1.0f;
    const float R = sc_circle_radius(L.k);
    sx_R = R;
    if (L.key != s_key) { s_key = L.key; sx_np = 0; sx_nb = 0; s_bt = -1.0f; }   // another chain : its own start
    const float t = (float)(GetTickCount() % 3600000u) / 1000.0f;
    sx_clock = t; sx_bt = L.bt;
    if (L.prop >= 0 && U.scAnim) { sim_setup(L.prop, L.scaleRel); sim_step(L.prop, s_bt, L.bt, dt, L.CX, L.CY, R, L.k, 1.0e9f); }
    else if (sx_np) step_parts(dt, L.CX, L.CY, R, L.k, true);
    s_bt = L.bt;
    CircleIn in = {};
    in.P = L.prop; in.closed = L.closed; in.phase = L.phase; in.timer = L.timer; in.frac = L.frac; in.bt = L.bt; in.t = t;
    in.tp = L.tp; in.opener = L.opener; in.items = L.items; in.nItems = L.nItems;
    in.CX = L.CX; in.CY = L.CY; in.R = R; in.k = L.k; in.kl = L.kl; in.preview = L.preview;
    in.hitKind = L.hitKind; in.hitName = L.hitName; in.hitDmg = L.hitDmg; in.hitElem = L.hitElem; in.hitHasDmg = L.hitHasDmg; in.hitMs = L.hitMs;
    circle_core(f, in);
}
// the distortion copy (a DEFAULT-pool render target) is held only while something uses it : 5 s after the last
// live circle and with the demo off, it is given back
// the rect the list takes round the sphere (edit layout : the box is the WHOLE content) ; false = no list
bool sc_circle_list_rect(const Frame& f, const ScCircleLive& L, float& x, float& y, float& w, float& h) {
    if (!L.items || L.nItems <= 0 || !ui_config().scList) return false;
    sx_meas = true; sx_mok = false;
    list_draw(f, 0, L.items, L.nItems, L.CX, L.CY, sc_circle_radius(L.k), L.k, L.kl, 0.0f);
    sx_meas = false;
    if (!sx_mok) return false;
    x = sx_mx; y = sx_my; w = sx_mw; h = sx_mh; return true;
}
// the hit popup's ROOM round its anchor (the same anchor hit_fx draws at) : the edit-layout box takes it in, so
// placing the Skillchains box places the Arcade WS / Magic Burst popup with it. The widest style (a banner / shard
// round a long WS name) sets the width ; the name line on top, the damage, the burst's spell name under.
bool sc_circle_hit_rect(const ScCircleLive& L, float& x, float& y, float& w, float& h) {
    const UiConfig& U = ui_config();
    if (!U.scArcade && !U.scMbFx) return false;
    CircleIn in = {}; in.CX = L.CX; in.CY = L.CY; in.k = L.k; in.kl = L.kl; in.R = sc_circle_radius(L.k);
    const float hs = U.scHitScale < 0.5f ? 0.5f : (U.scHitScale > 2.0f ? 2.0f : U.scHitScale), K = L.k * hs;
    float ax, ay; hit_anchor(in, hs, ax, ay);
    const float yN = -(U.scHitNameY < -80.0f ? -80.0f : (U.scHitNameY > 80.0f ? 80.0f : U.scHitNameY)) * L.k;
    const float yD = -(U.scHitDmgY < -80.0f ? -80.0f : (U.scHitDmgY > 80.0f ? 80.0f : U.scHitDmgY)) * L.k;
    const float nameY = ay - 17.0f * K + yN, dmgY = ay + 13.0f * K + yD;
    const float t0 = (nameY < dmgY ? nameY : dmgY) - 22.0f * K, b0 = (nameY > dmgY ? nameY : dmgY) + (U.scMbFx && U.scMbName ? 62.0f : 30.0f) * K;
    x = ax - 115.0f * K; w = 230.0f * K; y = t0; h = b0 - t0; return true;
}
void aw_dial_fx(const Frame& f, float CX, float CY, float R, float k, float ageMs, unsigned rgb, float srcX, float srcY, bool distortOn, bool late) {
    if (!f.dev) return;
    const u32 dev = f.dev;
    ensure_gen(dev);
    sx_cUse = GetTickCount(); sx_fs = 0;
    const float keepReach = REACH, keepR = sx_R; REACH = 0.5f; sx_R = R;
    const float t = (float)(GetTickCount() % 3600000u) / 1000.0f;
    if (ageMs >= 0.0f && ageMs < 900.0f) {
        const float u = ageMs / 900.0f, eu = 1.0f - (1.0f - u) * (1.0f - u);
        if (distortOn) {   // the scene DRAWN IN : a pull, a twist, an inward refraction ring
            Warp w = {}; w.pull = 0.14f * sinf(u * PI); w.swirl = 0.10f * (1.0f - u);
            w.shR = R * (3.0f - 2.2f * eu); w.shW = 8.0f * k; w.shA = 7.0f * k * (1.0f - u);
            distort(dev, CX, CY, R, k, t, w); fx_end(dev);
        }
        // the inward shockwave : torn, closing on the dial
        shockwave(dev, CX, CY, R * 0.9f, k, 1.0f - eu, t, lerp(rgb, 0xFFFFFF, 0.4f), rgb, 2.6f); fx_end(dev);
        // the MATTER : streams spiralling in from the caster's side
        const float sa = atan2f(srcY - CY, srcX - CX), sd = sqrtf((srcX - CX) * (srcX - CX) + (srcY - CY) * (srcY - CY));
        for (int i = 0; i < 5; ++i) {
            const float a0 = sa + (i - 2) * 0.22f, rs = (sd > R * 1.5f ? sd : R * 2.6f) * (1.0f - 0.55f * eu), re = R * 0.35f;
            if (rs <= re) continue;
            arc_ribbon(dev, CX, CY, a0, a0 + 1.1f + i * 0.08f, rs, re, (2.6f - 1.4f * u) * k, rgb, 0.75f * (1.0f - u), 2.0f * k, t + i);
        }
        fx_end(dev);
        bloom(dev, CX, CY, R * (0.7f + 0.8f * u), C(lerp(rgb, 0xFFFFFF, 0.5f), 1.6f * u * (1.0f - u)));   // the drained TP arriving
        fx_end(dev);
    }
    {   // the breath : the dial's colour, quiet ; a late dial (nobody cast for too long) breathes red
        const float br = 0.5f + 0.5f * sinf(t * (late ? 5.0f : 2.0f));
        bloom(dev, CX, CY, R * (1.35f + 0.1f * br), C(late ? 0xFF4040 : rgb, (late ? 0.30f : 0.12f) * (0.6f + 0.4f * br)));
        fx_end(dev);
    }
    REACH = keepReach; sx_R = keepR; sx_fs = 0;
}
void sc_circle_idle() {
    if (sx_dTex && !sx_on && GetTickCount() - sx_cUse > 5000u) dist_release();
}

} // namespace aio
