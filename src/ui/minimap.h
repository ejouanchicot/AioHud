// minimap.h -- the Minimap module (phase 1a : placeholder). Draws a square panel with the player dot +
// heading arrow placed via the reversed world->map-pixel transform (docs/game-data/world/map-system.md). The
// real zone map image (extracted from the ROM DATs) replaces the placeholder frame in phase 1b.
#pragma once
#include "widget.h"
#include "ui/tex_retry.h"   // TexRetry : the Help marker/atlas load is a BOUNDED retry, not a one-shot latch (rule 10)

namespace aio {

struct GameState;

// the ASTRAL compass kit's pieces (assets/astral/laiton_*.raw, scripts/gen_astral.py)
enum { AS_SHADOW, AS_BASE, AS_TICKS, AS_GLINT, AS_PAST, AS_LN, AS_LE, AS_LS, AS_LW, AS_N };
static const int AS_SZ[AS_N] = { 512, 512, 512, 512, 64, 64, 64, 64, 64 };

class Minimap : public Widget {
public:
    explicit Minimap(const GameState* state);

    const char* type_name() const override { return "Minimap"; }
    void draw(const Frame& f) override;
    void on_device_lost() override;
    void dispose() override;

private:
    const GameState* state_;
    u32      mapTex_ = 0;        // current zone/submap map texture (from the ROM DAT)
    unsigned mapFileId_ = 0;     // map file-id currently loaded (0 = none) -> reload only on change
    // (re)load attempts made for THIS zone's map. The schedule PLATEAUS, it does not terminate :
    //   0..2   next frame   -- a CreateTexture miss right after a zone-in recovers at once (no black flash)
    //   3..11  300 ms apart -- the normal "data not ready yet" window
    //   >= 12  every 15 s, FOREVER (MAP_SLOW_AFTER / MAP_SLOW_MS in minimap.cpp)
    // It used to be a budget of 12 that simply STOPPED -- ~2.7 s after the zone-in -- leaving the map black for
    // the WHOLE visit if the DAT was still held (AV / Controlled Folder Access on a Program Files install, a
    // slow disk). Reported by the NA tester as "random black maps". A bound must mean "stop hammering", never
    // "stay blind for the session" (CLAUDE.md rule 10). The panel still WAITS instead of drawing black only
    // while in the fast lane (< MAP_SLOW_AFTER) ; after that it renders its normal empty frame and the slow
    // lane keeps trying underneath, so a map that becomes readable later simply appears.
    int      mapTries_ = 0;
    unsigned mapRetryAt_ = 0;    // GetTickCount of the next allowed retry (throttle)
    int      mapW_ = 0, mapH_ = 0;
    u32      mkPlayer_ = 0, mkMob_ = 0;   // marker icons : player Location pin + mob Arrow (white, tinted)
    u32      elemTex_ = 0;                 // 8-cell elemental-icon atlas (clock header day icon)
    u32      grainTex_ = 0; TexRetry grainR_;      // the Dial's metal grain (assets/metal_grain.raw, scripts/gen_metal_grain.py)
    u32      skyTex_ = 0; TexRetry skyR_;          // the sky wheel's twelve painted scenes (assets/sky_frames.raw, scripts/gen_sky_frames.py)
    u32      moonPhTex_ = 0; TexRetry moonPhR_;     // the moon pod's 8 upright phases (assets/moon_phases.raw, scripts/gen_moon_phases.py)
    u32      moonDialTex_ = 0; TexRetry moonDialR_;   // the moon-phase dial's inked parchment face (assets/moon_dial.raw, scripts/gen_moon_dial.py)
    u32      wxTex_ = 0; TexRetry wxR_;    // the WEATHER's 8 element icons, HD (assets/weather_icons.raw, scripts/gen_weather_icons.py) : the Dial's weather orbs only
    u32      moonTex_ = 0;                 // supersampled moon-phase texture (rebuilt only when the phase/day changes)
    int      moonKey_ = -1;                // cache key : dayIdx / moonPct / waning
    // per-entity LEVEL-OF-DETAIL state : the dot <-> icon morph is ANIMATED and remembered (an entity that is an icon
    // keeps its place against an equal one), so a moving camp no longer snaps / ping-pongs between dots and arrows.
    // A small open-addressing table keyed by entity id ; a slot unseen for a few frames is free again.
    struct LodSt { unsigned id; float a; unsigned frame; };
    LodSt    lod_[1024] = {};
    unsigned lodFrame_ = 0;
    float    lodT_ = 0.0f;
    LodSt*   lod_slot(unsigned id);
    u32      mkAtlas_ = 0;                // the v3 marker icon atlas (assets/map_markers.raw, 512 x 256) ...
    TexRetry mkAtlasR_;                   // ... under its bounded retry
    u32      ast_[AS_N] = {};             // the Astral kit textures (0 = not loaded) ...
    TexRetry astR_[AS_N];                 // ... each under its own bounded retry (rule 10)
    u32      astG_[AS_N] = {};            // the same kit in GREY, tinted by the theme's metal (Frame = Theme / Custom)
    TexRetry astGR_[AS_N];
    int      mkTries_ = 0;      // bounded retry budget for the marker/element textures (was a one-shot bool)
    unsigned mkNextMs_ = 0;

    // per-mob eased facing angle (the raw heading arrives in network steps -> ease it so the arrow turns
    // smoothly instead of snapping). Keyed by entity id ; reset on a zone/map change.
    struct MobAng { unsigned id; float ang; };
    MobAng   mobAng_[256];
    int      mobAngN_ = 0;
    float    eased_angle(unsigned id, float target);
};

// ---- Help live samples : draw the REAL minimap elements (SAME code path / colours as the widget) for the
// Help tab. The Help owns the marker + element-atlas + moon textures : call minimap_help_textures() EVERY FRAME
// (it self-gates on its own bounded retry, like target_help_textures) and pass the caller-owned moon handle/key
// into minimap_help_moon() (it rebuilds it on a phase change). Forget every handle AND re-arm every TexRetry on
// device-lost. `t` is the frame clock (animation).
void        minimap_help_textures(u32 dev, u32& mkPlayer, u32& mkMob, u32& elemTex,
                                  TexRetry& rPlayer, TexRetry& rMob, TexRetry& rElem);                                     // lazy-load the 3 textures under a bounded retry (caller owns the state)
void        minimap_help_disc  (u32 dev, const Frame& f, Font* fo, u32 mkPlayer, u32 mkMob, u32 mapTex, float cx, float cy, float r, float t);   // a live round minimap : brass bezel FIRST, then the REAL current-zone map (mapTex = caller-owned, config-loaded) + the REAL live entity markers + player pin -- same transform/colours as the widget
float       minimap_help_legend(u32 dev, Font* fo, u32 mkPlayer, u32 mkMob, float x, float y, float rowH, float top, float bot, int lang);   // the marker legend (dot/arrow/pin + meaning) ; returns the new y
const char* minimap_help_moon  (u32 dev, u32& moonTex, int& moonKey, float cx, float cy, float r, float t, int lang);      // the moon sweeping New->Full->New ; returns its phase name
const char* minimap_help_day   (u32 dev, Font* fo, u32 elemTex, float x, float cy, float t, int lang);                     // the 8 elemental-day icons, active one cycling ; returns its day name

} // namespace aio
