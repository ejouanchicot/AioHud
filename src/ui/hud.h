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
#include "model/selftest.h"   // CheckFail : Hud::write_bug_report carries the harness's findings
#include "gfx/font.h"
#include "gfx/window.h"
#include "ui/tex_retry.h"   // TexRetry : bounded-retry lazy texture load
#include "model/gamestate.h"
#include "model/timers_build.h"   // songrow_ring_dump / timers_focus_trace / timers_register_checks (the Timers model)
#include "model/layout.h"
#include "ui/widget.h"
#include "ui/liquid_bars.h"
#include "ui/config_page.h"

namespace aio {

// hud_actmenu.cpp : is this screen point (fractions) on the redrawn action menu ? -- its clicks are AioHUD's
bool actmenu_hit(float fx, float fy);
bool npc_answers_hit(float fx, float fy);   // the NPC answers block (hud_actmenu.cpp) : the wheel over it is ours
void npc_wheel_add(int d);                  // +1 a notch up, -1 down (window thread ; npc_draw consumes it)
bool fep_hit(float fx, float fy);          // the auto-translate picker (hud_actmenu.cpp) : the wheel over it is ours
void fep_wheel_add(int d);                 // +1 a notch up, -1 down (window thread ; fep_draw consumes it)
// Status -> Currencies' search (hud_actmenu.cpp) : the key thread hands it each key while the list has the focus ;
// true = the key is the search's (the game must not see it). ch = the typed character (0 none) ; mod = Ctrl / Alt held
bool currencies_key(int dik, unsigned ch, bool pressed, bool mod);
// The trade's gil box (hud_actmenu.cpp) : while it is up, the digits typed (top row), Backspace, Esc (clears the typing
// first) and Enter (validates the typed amount) are AioHUD's ; true = the game must not see the key
bool trade_key(int dik, unsigned ch, bool pressed, bool mod);
// //aio eqpreview : the Equipment screen shows every state a cell / a list row can take (a visual check) ; returns the new state
bool equip_preview_toggle();
bool scfx_toggle(const char* arg, char* msg, int cap);   // //aio scfx <prop> [scale] (dev) : the skillchain resonance-circle demo, looping ; off = stop
void scfx_draw(const Frame& f, float screenW, float screenH);
void scfx_forget(); void scfx_dispose();
bool buff_preview_toggle();    // //aio buffpreview (dev) : 32 fake buffs in place of yours, under the top bar
bool net_preview_toggle();     // //aio netpreview (dev) : the connection box shows every state it can (mail, GM, the 4 bands)
// //aio kittest (dev build) : the painted theme kit (Eclat de Holla), one trial party box -- theme_kit.cpp ; returns the new state
bool theme_kit_test_toggle(const char* id);   // null / "" : the Holla v4 trial ; else assets/themes/kits/<id> ; the same kit again = off

class Hud {
public:
    Hud();
    ~Hud();

    void render(u32 dev);   // one game frame (slot-6 hook)
    // a character is LOGGED IN and in the world : false at the title / char-select screens and ~2 s after a logout.
    // A zone keeps it true (the player poll keeps succeeding through most of a load). Game UI gates on it.
    bool logged_in() const { return everInGame_ && notInGameFrames_ <= 120; }
    void dispose();         // release all resources (at //unload)
    void self_check();      // //aio selfcheck : log the health of every texture-load subsystem (stuck latches, missing icons)
    void write_bug_report(const CheckFail* hits, int n, bool watched);   // assemble the context and write the file
    // //aio doctor : run every runtime check we cannot make offline (game link, packet flow, texture health,
    // model state) and fill `out` with ONE LINE PER PROBLEM, each ending in what to do about it. Returns the
    // number of lines written ; 0 means everything checked is healthy. The full detail always goes to the log.
    static const int DOC_LINE = 200;
    int doctor(char out[][DOC_LINE], int maxOut);

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
    void draw_hate_list(const Frame& f, bool preview = false, float ovX = 0.0f, float ovY = 0.0f, float ovS = 0.0f);
    void draw_absorb(const Frame& f, bool preview = false, float ovX = 0.0f, float ovY = 0.0f, float ovS = 0.0f);
#ifdef AIOHUD_DEVTOOLS
    void draw_facing_probe(const Frame& f);   // DEV ONLY (//aio facing) : where you face, as the Sortie arrow computes it
#endif   // Absorb-TP box (hud_absorb.cpp) : who drained how much TP ; preview shows a sample   // hate list box (mobs aggro'd on the party, HP bars) ; preview shows a sample
    void draw_pointwatch(const Frame& f, bool preview = false, float ovX = 0.0f, float ovY = 0.0f, float ovS = 0.0f);   // PointWatch box (XP/CP/ML progress + Merits) ; preview shows a sample
    void draw_grimoire(const Frame& f, bool preview = false, float ovX = 0.0f, float ovY = 0.0f, float ovS = 0.0f);
    void draw_pet(const Frame& f, bool preview = false, float ovX = 0.0f, float ovY = 0.0f, float ovS = 0.0f);   // Pet box (hud_pet.cpp) : the main job's content below, one place for all ; preview shows the config tab's sample
    void draw_geo(const Frame& f, bool preview = false, float ovX = 0.0f, float ovY = 0.0f, float ovS = 0.0f);   // the Pet box's Geomancer content (luopan bubble + aura + Entrust + MP + job abilities, hud_geo.cpp) ; only draw_pet calls it
    void draw_smn(const Frame& f, bool preview = false, float ovX = 0.0f, float ovY = 0.0f, float ovS = 0.0f);   // the Pet box's Summoner content (avatar + Blood Pact recasts + job abilities + MP + pacts, hud_smn.cpp) ; only draw_pet calls it
    void draw_bst(const Frame& f, bool preview = false, float ovX = 0.0f, float ovY = 0.0f, float ovS = 0.0f);   // the Pet box's Beastmaster content (pet + Ready charges + moves, hud_bst.cpp) ; only draw_pet calls it   // Scholar grimoire (book + charges/timer + Addendum aura) ; preview shows a sample
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
    TexRetry             tpCoffer_r_;
    u32                  grimLight_ = 0, grimDark_ = 0, grimClosed_ = 0;   // grimoire book textures (forgotten on device change) ; closed = no Arts
    TexRetry             grimLight_r_, grimDark_r_, grimClosed_r_;
    u32                  bstArt_ = 0;              // BST box : the current pet's art (assets\bstpets\<key>.raw) ; forgotten on device change
    TexRetry             bstArtR_;
    char                 bstArtKey_[40] = {};      // which pet bstArt_ is (<key> or <key>_solo) ; a new pet releases it and loads its own
    u32                  bstElem_ = 0;             // BST box : the 8-element icon strip (assets\element_icons.raw, 256x32) ; the weapon icons share weaponIcons_
    u32                  bstPaw_ = 0;              // BST box : the Ready charge's paw (assets\bstpets\charge.raw, white, tinted) ; none -> the ring dial
    TexRetry             bstPawR_;
    u32                  bstEco_[3] = {};          // BST box : the correlation's three family icons (assets\bstpets\eco_<Family>.raw)
    TexRetry             bstEcoR_[3];
    char                 bstEcoKey_[3][16] = {};   // which family each slot holds ; another family releases it and loads its own
    u32 bst_eco_icon(u32 dev, int slot, const char* family);   // hud_bst.cpp
    static const int GEO_TEX_N = 14;   // the GEO content's images (assets\geo\, scripts/gen_geo_icons.py) : hud_geo.cpp GEO_TEX
    u32                  geoTex_[GEO_TEX_N] = {};
    TexRetry             geoTexR_[GEO_TEX_N];
    u32 geo_tex(u32 dev, int slot);    // hud_geo.cpp ; 0 while the file is not there
    u32                  smnArt_ = 0;              // SMN content : the current avatar's picture (assets\smn\<img>.raw) ; forgotten on device change
    TexRetry             smnArtR_;
    char                 smnArtKey_[24] = {};      // which picture smnArt_ is ; another avatar releases it and loads its own
    TexRetry             bstElemR_;   // PER-TEXTURE retry : one shared flag left a partial load (2 of 3) dead-ending the third for the session
    u32                  weaponIcons_ = 0;         // Sheol resistances : Slashing/Piercing/Blunt icon atlas (96x32, 3 cells)
    TexRetry             weaponIcons_r_;
    u32  ensure_buff_atlas(u32 dev);               // lazy load with a BOUNDED retry -- shared by Timers and Debuffs
    u32                  buffAtlas_ = 0;           // Timers box : the shared status-icon atlas (buff_atlas.raw), like Player/Party
    bool                 reload_pending_ = false;  // //aio layout requested -> reload at next render()
    std::string          layout_path_;             // path of the last applied layout (for hot-reload)
    float screenW_ = 2560.0f, screenH_ = 1400.0f;  // real game resolution (read from the device each frame)
    float ui_scale_ = 1.0f;                         // real screen / authored viewport width
    float lastW_[3] = { -1.0f, -1.0f, -1.0f };      // last measured box footprint -> re-anchor (bottom-right pinned)
    float lastH_[3] = { -1.0f, -1.0f, -1.0f };      // when ANY dimension setting changes (scale, bar size, badge, casts)
    // The re-anchor's two EXPENSIVE consequences, debounced. The geometry correction itself stays immediate --
    // the box must track the slider -- but a footprint changes on EVERY frame of a drag, and each change used to
    // trigger a full atomic rewrite of config.txt and/or a place_widgets() (destroy + rebuild every widget,
    // reload its assets, re-decode the zone map DAT). Per slider notch, with the HUD on screen. Coalesced now:
    // flag it, stamp a deadline, act once the size has settled. 0 = nothing pending.
    unsigned savePendMs_  = 0;                      // config.txt rewrite owed (pure I/O -> can wait longer)
    unsigned placePendMs_ = 0;                      // widget re-placement owed (visible -> shorter, still ~9 frames coalesced)
    u32   last_dev_ = 0;
    bool  everInGame_ = false;    // latched true on the first successful player poll -> gate ALL boxes on "logged in"
    int   notReadyFrames_ = 0;    // consecutive frames the player poll failed -> a SUSTAINED run = logout (re-hide)
    int   notInGameFrames_ = 0;   // the same WITHOUT the zoning flag : a zone must not read as a logout (logged_in)
};

// The Timers rows, their FOCUS monitor, //aio songdump / ftrace and the module's checks : model/timers_build.h
// (moved out of the renderer on 2026-09-13). The //aio songdump ring records into RAM, never to disk -- writing a
// line per event once made the ghost-song bug disappear while observed.
void minimap_register_checks();   // MAP.LOAD_FAILED (minimap.cpp)

} // namespace aio
