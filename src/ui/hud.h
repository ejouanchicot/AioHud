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
    void set_peek(bool on) { peekHide_ = on; }  // hold-End "peek" : hide the ENTIRE HUD while held, restore on release
    float screenW() const { return screenW_; }   // resolution detected at RENDER time (reliable)
    float screenH() const { return screenH_; }

private:
    void clear_widgets();   // delete every owned widget
    void add_default();     // a single LiquidBars at its default position (fallback)
    void place_widgets();   // (re)build + place widgets from layout_ at the current screen size
    void update_screen(u32 dev);   // read the real viewport ; re-place if the resolution changed
    void draw_config_preview(const Frame& f);   // draw the real party+alliance demo boxes into the config page's preview stage
    void draw_ws_popup(const Frame& f, bool preview = false, float ovCx = 0.0f, float ovCy = 0.0f, float ovUS = 0.0f);   // preview=true loops a sample ; ov* override the centre/scale (config stage)
    void draw_skillchains(const Frame& f, bool preview = false, float ovX = 0.0f, float ovY = 0.0f, float ovS = 0.0f);   // skillchains box (target's active chain) ; preview loops a sample
    void draw_treasure_pool(const Frame& f, bool preview = false, float ovX = 0.0f, float ovY = 0.0f, float ovS = 0.0f);   // treasure pool box (lottery items + timers) ; preview shows a sample
    void draw_hate_list(const Frame& f, bool preview = false, float ovX = 0.0f, float ovY = 0.0f, float ovS = 0.0f);   // hate list box (mobs aggro'd on the party, HP bars) ; preview shows a sample
    void draw_pointwatch(const Frame& f, bool preview = false, float ovX = 0.0f, float ovY = 0.0f, float ovS = 0.0f);   // PointWatch box (XP/CP/ML progress + Merits) ; preview shows a sample
    void draw_grimoire(const Frame& f, bool preview = false, float ovX = 0.0f, float ovY = 0.0f, float ovS = 0.0f);   // Scholar grimoire (book + charges/timer + Addendum aura) ; preview shows a sample
    void draw_zonetracker(const Frame& f, bool preview = false, float ovX = 0.0f, float ovY = 0.0f, float ovS = 0.0f);   // Zone Tracker (Dynamis KIs+timer / Abyssea lights+visitant) ; preview shows a sample
    void draw_empypop(const Frame& f, bool preview = false, float ovX = 0.0f, float ovY = 0.0f, float ovS = 0.0f);   // EmpyPop (the tracked Abyssea NM's pop chain) ; preview shows the chloris demo chain
    void draw_timers(const Frame& f, bool preview = false, float ovX = 0.0f, float ovY = 0.0f, float ovS = 0.0f);   // Timers box (self buff timers, exact from 0x063 type-9 ; same buff-icon atlas) ; preview shows a sample
    void draw_debuffs(const Frame& f, bool preview = false, float ovX = 0.0f, float ovY = 0.0f, float ovS = 0.0f);   // Debuffs box (current target's debuffs, detached from the Target box ; same buff-icon atlas) ; preview shows a sample

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
    bool                 peekHide_ = false;        // End held -> hide the whole HUD (peek at the game) ; reset on focus loss
    bool                 wsFontWarmed_ = false;    // the WS-popup font's big atlases pre-baked (so the first WS never hitches)
    u32                  tpCoffer_ = 0;            // treasure-pool coffer icon texture (forgotten on device change)
    bool                 tpCofferTried_ = false;
    u32                  grimLight_ = 0, grimDark_ = 0, grimClosed_ = 0;   // grimoire book textures (forgotten on device change) ; closed = no Arts
    bool                 grimTried_ = false;
    u32                  weaponIcons_ = 0;         // Sheol resistances : Slashing/Piercing/Blunt icon atlas (96x32, 3 cells)
    bool                 weaponIconsTried_ = false;
    u32                  buffAtlas_ = 0;           // Timers box : the shared status-icon atlas (buff_atlas.raw), like Player/Party
    bool                 buffAtlasTried_ = false;
    bool                 reload_pending_ = false;  // //aio layout requested -> reload at next render()
    std::string          layout_path_;             // path of the last applied layout (for hot-reload)
    float screenW_ = 2560.0f, screenH_ = 1400.0f;  // real game resolution (read from the device each frame)
    float ui_scale_ = 1.0f;                         // real screen / authored viewport width
    float lastW_[3] = { -1.0f, -1.0f, -1.0f };      // last measured box footprint -> re-anchor (bottom-right pinned)
    float lastH_[3] = { -1.0f, -1.0f, -1.0f };      // when ANY dimension setting changes (scale, bar size, badge, casts)
    u32   last_dev_ = 0;
    bool  everInGame_ = false;    // latched true on the first successful player poll -> gate ALL boxes on "logged in"
    int   notReadyFrames_ = 0;    // consecutive frames the player poll failed -> a SUSTAINED run = logout (re-hide)
};

} // namespace aio
