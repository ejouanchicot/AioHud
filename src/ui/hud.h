// hud.h -- owns the widgets and drives one render frame.
//
// The HUD holds the shared GameState and a list of widgets BUILT FROM THE LAYOUT
// DESCRIPTOR (design/exports/layout.json): apply_layout() loads it, asks the factory
// for each `type`, resolves anchor+% into a pixel origin, and sorts by z. Each game
// frame it:
//   1. detects a D3D device change (zoning) and tells widgets to drop GPU handles,
//   2. lets each widget (re)create its resources,
//   3. opens ONE render-state block (so we never corrupt the game's own rendering),
//   4. draws every VISIBLE widget (z order), then restores the state block.
#pragma once
#include <vector>
#include <string>
#include "gfx/d3d.h"
#include "gfx/font.h"
#include "gfx/window.h"
#include "model/gamestate.h"
#include "model/layout.h"
#include "ui/widget.h"
#include "ui/liquid_bars.h"
#include "ui/config_page.h"

namespace aio {

class Hud {
public:
    Hud();
    ~Hud();

    void render(u32 dev);   // one game frame (slot-6 hook)
    void dispose();         // release all resources (at //unload)

    // (re)build the widget list from the layout descriptor (load + place + sort by z).
    // No-op-safe: on a missing/invalid file it keeps the current widgets.
    void apply_layout(const char* path);

    // ask for a hot-reload of the layout file. SAFE from the //aio command thread: the
    // actual reload (which deletes + recreates widgets) is deferred to the next render(),
    // so it never races the draw loop -- doing it inline from the command crashes the game.
    void request_reload() { reload_pending_ = true; }

    GameState&  state() { return state_; }
    LiquidBars* bars()  { return bars_; }   // the HP/MP/TP fioles (may be null before layout)
    // switch the FFXI window skin (0-based index into window_theme_name) ; reloaded next frame.
    void set_skin(int idx);
    int  skin_index() const { return skinIdx_; }
    ConfigPage& config() { return config_; }   // the //aio config overlay
    float screenW() const { return screenW_; }   // resolution detected at RENDER time (reliable)
    float screenH() const { return screenH_; }

private:
    void clear_widgets();   // delete every owned widget
    void add_default();     // a single LiquidBars at its default position (fallback)
    void place_widgets();   // (re)build + place widgets from layout_ at the current screen size
    void update_screen(u32 dev);   // read the real viewport ; re-place if the resolution changed

    GameState            state_;
    FontManager          fonts_;                   // atlas cache (default + per-text faces/weights)
    WindowSkin           skin_;                    // FFXI 9-slice window skin (loaded lazily)
    int                  skinIdx_ = 0;             // index into window_theme_name() (//aio menu N)
    ConfigPage           config_;                  // full-screen config overlay (//aio config)
    MouseState           mouse_;                    // cursor + click, polled via Win32 each frame
    std::vector<Widget*> widgets_;                 // OWNED ; deleted on rebuild / dispose
    LiquidBars*          bars_ = nullptr;          // cached (PlayerHub vitals) for //aio lay
    Layout               layout_;                  // last loaded descriptor (kept for re-placement on resize)
    bool                 have_layout_ = false;     // a valid layout_ has been loaded
    bool                 reload_pending_ = false;  // //aio layout requested -> reload at next render()
    std::string          layout_path_;             // path of the last applied layout (for hot-reload)
    float screenW_ = 2560.0f, screenH_ = 1400.0f;  // real game resolution (read from the device each frame)
    float ui_scale_ = 1.0f;                         // real screen / authored viewport width
    float lastScale_[3] = { 1.0f, 1.0f, 1.0f };     // re-anchor widgets when any box scale changes
    u32   last_dev_ = 0;
};

} // namespace aio
