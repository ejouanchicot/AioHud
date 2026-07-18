// minimap.h -- the Minimap module (phase 1a : placeholder). Draws a square panel with the player dot +
// heading arrow placed via the reversed world->map-pixel transform (docs/game-data/map-system.md). The
// real zone map image (extracted from the ROM DATs) replaces the placeholder frame in phase 1b.
#pragma once
#include "widget.h"

namespace aio {

struct GameState;

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
    int      mapRetries_ = 0;    // remaining (re)load attempts when the DAT read failed (transient : data not ready on zone-in / a registry hiccup) -> retry a bounded number of times instead of giving up until the next zone
    unsigned mapRetryAt_ = 0;    // GetTickCount of the next allowed retry (throttle)
    int      mapW_ = 0, mapH_ = 0;
    u32      mkPlayer_ = 0, mkMob_ = 0;   // marker icons : player Location pin + mob Arrow (white, tinted)
    u32      elemTex_ = 0;                 // 8-cell elemental-icon atlas (clock header day icon)
    u32      moonTex_ = 0;                 // supersampled moon-phase texture (rebuilt only when the phase/day changes)
    int      moonKey_ = -1;                // cache key : dayIdx / moonPct / waning
    bool     mkTried_ = false;

    // per-mob eased facing angle (the raw heading arrives in network steps -> ease it so the arrow turns
    // smoothly instead of snapping). Keyed by entity id ; reset on a zone/map change.
    struct MobAng { unsigned id; float ang; };
    MobAng   mobAng_[256];
    int      mobAngN_ = 0;
    float    eased_angle(unsigned id, float target);
};

// ---- Help live samples : draw the REAL minimap elements (SAME code path / colours as the widget) for the
// Help tab. The Help owns the marker + element-atlas + moon textures : call minimap_help_textures() lazily to
// load the three marker/atlas handles, and pass the caller-owned moon handle/key into minimap_help_moon() (it
// rebuilds it on a phase change). Forget every handle on device-lost. `t` is the frame clock (animation).
void        minimap_help_textures(u32 dev, u32& mkPlayer, u32& mkMob, u32& elemTex);                                       // lazy-load the 3 textures
void        minimap_help_disc  (u32 dev, const Frame& f, Font* fo, u32 mkPlayer, u32 mkMob, u32 mapTex, float cx, float cy, float r, float t);   // a live round minimap : brass bezel FIRST, then the REAL current-zone map (mapTex = caller-owned, config-loaded) + the REAL live entity markers + player pin -- same transform/colours as the widget
float       minimap_help_legend(u32 dev, Font* fo, u32 mkPlayer, u32 mkMob, float x, float y, float rowH, float top, float bot, int lang);   // the marker legend (dot/arrow/pin + meaning) ; returns the new y
const char* minimap_help_moon  (u32 dev, u32& moonTex, int& moonKey, float cx, float cy, float r, float t, int lang);      // the moon sweeping New->Full->New ; returns its phase name
const char* minimap_help_day   (u32 dev, Font* fo, u32 elemTex, float x, float cy, float t, int lang);                     // the 8 elemental-day icons, active one cycling ; returns its day name

} // namespace aio
