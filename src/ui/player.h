// player.h -- the Player Hub : the local player's own box (identity + HP/MP/TP vitals + buffs).
//
// Layout type "PlayerHub". This REPLACES the old fioles-only LiquidBars as the widget the HUD
// instantiates for that type, but it EMBEDS a LiquidBars (provider()) so the party / target rows
// keep borrowing the real fiole assets (set_vial_provider). Phase 1 : identity + vitals + buffs,
// all from data already in GameState (me / hp/mp/tp / buffs). gil / speed / cast / equipment later.
#pragma once
#include "widget.h"
#include "gfx/window.h"   // WindowSkin (own box theme, like the Target box)

namespace aio {

struct GameState;
class LiquidBars;

// //aio geartrace : trace the next N gear-icon resolutions (bundled BMP -> id range -> DAT -> decode -> texture
// -> cache write) to aiohud_debug.log. Diagnoses "items show as raw IDs" without guessing which step failed.
void set_gear_trace(int n);

class Player : public Widget {
public:
    explicit Player(const GameState* state);
    ~Player() override;

    const char* type_name() const override { return "PlayerHub"; }
    void measure(float& w, float& h) const override;

    // config preview : the box's REAL drawn footprint (W/H incl. in-box equipment grid) plus how far a
    // DOCKED equipment grid extends beyond the box on each side (l/t/r/b). Lets the preview keep it in view.
    void preview_footprint(float& w, float& h, float& l, float& t, float& r, float& b) const;

    void ensure(u32 dev) override;
    void draw(const Frame& f) override;
    void on_device_lost() override;
    void dispose() override;

    // config preview : force the demo player so the box shows in the config stage even when out of game.
    void set_demo(bool on) { demo_ = on; }
    // config preview : the STANDALONE (detached) equipment box's footprint (grid + gil), at the live scale.
    void equip_footprint(float& w, float& h) const;
    // config preview : draw the DETACHED equipment box at a forced footprint top-left inside the stage (so the
    // Player preview can show the hub + the detached grid as a centred group, like Target + detached debuffs).
    void set_eq_preview(bool on, float x = 0.0f, float y = 0.0f) { pvEq_ = on; pvEqX_ = x; pvEqY_ = y; }
    // the embedded fiole widget -> the HUD registers it as the shared vial_provider (party/target borrow it).
    LiquidBars* provider() { return vials_; }

private:
    const GameState* state_;
    LiquidBars* vials_ = nullptr;    // embedded : draws the 3 fioles + is the shared vial provider
    bool  demo_ = false;
    bool  pvEq_ = false; float pvEqX_ = 0.0f, pvEqY_ = 0.0f;   // config-preview override for the DETACHED equipment box

    u32   jobicon_tex_ = 0; bool jobicon_tried_ = false;
    u32   buff_tex_    = 0; bool buff_tried_ = false;
    u32   gil_tex_     = 0; bool gil_tried_ = false;   // gil coin icon (icon_gil.raw) for the gil/speed band

    // equipment viewer : one gear-icon texture per equip slot, loaded from gearicons/<id>.bmp on demand.
    // gearId_[s] is the item id currently loaded in slot s (0 = none) -> reload only when the slot changes.
    u32            gearTex_[16] = { 0 };
    unsigned short gearId_[16]  = { 0 };
    unsigned char  gearTry_[16] = { 0 };   // failed-load retry counter per slot (bounded) -> recover from a transient texture-create failure
    unsigned       gearNextMs_[16] = { 0 };   // per-slot back-off : earliest GetTickCount to RETRY a transient ROM-decode failure (0 = now). A DAT held by AV / Controlled Folder Access on a Program Files install fails the first touch, then opens -- so the failure must be re-tried over SECONDS, not given up on the same frame (rule 10).
    bool           eqReadyPrev_ = false;   // //aio geartrace : last equipValid, so only its TRANSITIONS are logged

    WindowSkin plrSkin_;             // own FFXI window skin (for a custom FFXI-family box theme, independent of party)
    int        plrSkinVar_ = -1;     // currently loaded theme index (-1 = none) -> reload only on change
};

} // namespace aio
