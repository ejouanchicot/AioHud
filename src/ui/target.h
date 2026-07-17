// target.h -- the Target module : a themed box showing the current target.
//
// Draws the ACTIVE target's NAME (coloured by allegiance) + an HP% bar (with the "white damage"
// trail) + the HP% number, a detail row (movement SPEED % and Treasure Hunter level, as text or
// icon), and a debuff-icon row with approx timers. Chrome = the chosen Box Theme (matches the
// party boxes). Data comes from GameState::target (read_target_entity) + the party() singleton
// (debuffs / TH). The box FADES in on target and out when dropped ; hidden when nothing is
// targeted. Placed by the layout descriptor (type "TargetBar"). set_demo() forces a fake target
// for the config live-preview. Edit mode allows drag / wheel-resize / centre-snap / axis-lock.
#pragma once
#include "ui/widget.h"
#include "gfx/window.h"   // WindowSkin (the Target owns its OWN FFXI skin, independent of the party's)

namespace aio {

class Target : public Widget {
public:
    const char* type_name() const override { return "TargetBar"; }
    void measure(float& w, float& h) const override;   // authoritative size -> anchoring
    float drawn_height() const { return lastH_; }       // FULL last-drawn box height (incl. detail/range/action) -> config preview stacking
    void ensure(u32 dev) override;                     // lazily load the buff/debuff atlas
    void on_device_lost() override;                    // forget the atlas handle (zoning)
    void dispose() override;                            // release the atlas (unload)
    void draw(const Frame& f) override;
    // config LIVE PREVIEW : force a demo target (fake name / HP / debuffs) so the Target page shows a
    // sample even with nothing targeted. Set around the preview draw, cleared after.
    void set_demo(bool on) { demo_ = on; }

private:
    bool  demo_ = false;                       // preview mode : render a fake target ignoring GameState
    float baseW_  = 300.0f;                   // box width (base px, pre-scale)
    float appear_ = 0.0f;                     // show/hide fade 0..1 (target present)
    float hpp_    = 100.0f;                    // FOREGROUND fill : snaps ~instantly to the live HP (crisp hit read)
    float hppGhost_ = 100.0f;                  // DELAYED damage bar : holds at the old value on a hit then drains to
                                               // catch up -> the pale chunk between them = the HP just lost (fighting-game "white damage")
    float ghostHold_ = 0.0f;                   // seconds left before the delayed bar starts draining after a hit
    float lastTgt_ = 100.0f;                    // previous live HP% -> detect a fresh damage event
    float hitFlash_ = 0.0f;                    // impact "bump" 0..1 (proportional to the hit), decays -> a brief flash
    unsigned lastId_ = 0;                     // last target id -> snap the bar on a target change
    char     lastName_[24] = {0};             // last target name, kept so it fades OUT with the frame (not instantly)
    float lastT_  = -1.0f;                    // previous frame time (dt-based smoothing ; f.t wraps)
    float lastH_  = 60.0f;                     // last drawn box height (px) -> edit-mode grab hit-rect before this frame's geometry
    unsigned posId_ = 0;                        // target id the held speed belongs to (reset on a target change)
    float spdShown_ = 0.0f;                     // eased displayed speed % -> smooths the readout
    float spdWindow_ = 5.0f;                     // MOB : last PLAUSIBLE field speed (yalms/s), held during the bogus 10-17 chase spikes
    u32   buff_tex_   = 0;                     // status-icon atlas (shared layout with the party buffs) for the debuff row
    bool  buff_tried_ = false;                // attempted the load (don't retry the file every frame)
    u32   th_tex_     = 0;                     // Treasure Hunter coffer icon (icon_th_coffer.raw) for the detail row's icon mode
    bool  th_tried_   = false;                // attempted the load (don't retry the file every frame)
    WindowSkin tgtSkin_;                       // the Target's OWN FFXI window skin (its texture variant is independent of the party's)
    int   tgtSkinVar_ = -1;                    // which FFXI theme index tgtSkin_ currently holds (-1 = none) -> reload on change / device loss
};

// ---- Help live samples : draw ONE real Target element (SAME code path as the widget), for the Help tab.
// The Help owns the atlas/coffer textures : call target_help_textures() lazily to load them (returns the
// handles) and forget them on device-lost. `t` is the frame clock (drives the looping animation).
void target_help_textures(u32 dev, u32& buffTex, u32& thTex);                                      // lazy-load the two textures
void target_help_hpbar (u32 dev, float x, float y, float w, float h, float t);                     // HP bar + white-damage trail
void target_help_range (u32 dev, Font* lf, float x, float y, float w, float h, bool mob, float t); // distance/range gauge (mob vs PC zones)
void target_help_debuffs(u32 dev, Font* f, u32 buffTex, float x, float y, float cell, int n, float t); // debuff icons + timers
void target_help_th    (u32 dev, Font* f, u32 thTex, float x, float y, float size, float t);       // Treasure Hunter coffer + tier
void target_help_speed (u32 dev, Font* f, u32 buffTex, float x, float cy, int pct, bool player, bool icon); // "Spd +NN%" in the speed colour

} // namespace aio
