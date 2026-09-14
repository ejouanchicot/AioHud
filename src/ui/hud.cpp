// hud.cpp -- see hud.h.
#include "model/flipwatch.h"   // oscillating decisions report to the watcher too
#include "model/capwatch.h"    // saturated tables report to the watcher too
#include "hud.h"
#include "ui/hud_internal.h"   // box_edit / draw_icon_cell : shared with the per-module hud_*.cpp split TUs
#include "ui/factory.h"
#include "ui/player.h"
#include "ui/party.h"
#include "ui/target.h"
#include "ui/minimap.h"
#include "ui/gear_canary.h"
#ifdef AIOHUD_DEVTOOLS
#include "aiohud_devtools.h"   // dev-only tools (dev/src, never in a release)
#endif  // the gear-icon canary : ticks every frame, reports to the harness and to doctor
#include "ui/buff_atlas.h"   // buff_atlas_forget / buff_atlas_dispose : the ONE owner of the shared status-icon atlas
#include "model/layout.h"
#include "model/game_mem.h"
#include "model/ffximain_rva.h"   // //aio doctor : report every FFXiMain static + the client fingerprint
#include "model/luacore_root.h"   // //aio doctor : the LuaCore data root -- a WINDOWER update moves it
#include "model/sentinel.h"       // //aio doctor : the packet-vs-memory cross-checks
#include "model/gamestate.h"
#include "model/party_state.h"
#include "model/model_clock.h"   // the per-frame upkeep is one model event
#include "model/zones.h"   // zone_name -> Zone Tracker (Dynamis/Abyssea) detection
#include "model/ui_config.h"
#include "model/paths.h"          // //aio doctor : where this install lives and whether it can write there
#include "windower_debug.h"
#include "ui/edit_box.h"  // edit-mode drag for the WS popup (place it in //aio edit like the other boxes)
#include "ui/config_controls.h"   // tr() : //aio doctor speaks the language picked in the config, like every other message
#include "gfx/draw.h"     // rrect_glow / disc_glow for the WS popup burst
#include "model/skillchain.h"         // Skillchains : Resonating fields -> names / colours / elements
#include "gfx/texture.h"
#include "gfx/corner_mask.h"              // load_raw_texture / release_texture (coffer icon)
#include "gfx/d3d.h"                  // textured-quad state for the coffer icon (dSet* + FVF)
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <algorithm>
#include <string.h>
#include <ctime>          // time() for the treasure-pool item expiry

namespace aio {

// The Help samples own lazy texture copies at file scope in their own hud_*.cpp (they must draw without a Hud).
// These forget them on a device change -- see the dev != last_dev_ block in Hud::render.
void zonetracker_help_forget();
// (debuffs / timers Help samples no longer own a texture -- they borrow the shared atlas, buff_atlas.cpp)
void treasure_help_forget();
// ...and their //unload counterparts. FORGET is for a dead device (rule 4 : never Release on one) ; DISPOSE runs
// while the device is alive and must actually Release. Only the forget half existed, so every Help sample the
// user had opened leaked its texture on each //unload. Two of the four WERE copies of the 1024x640 buff atlas;
// those two now borrow the shared one, so only zonetracker / treasure still own a Help texture.
void zonetracker_help_dispose();
void treasure_help_dispose();
// The presented display mode, captured once (see the note at the capture site). 0 until the first frame.
static u32 s_dispFmt = 0, s_dispW = 0, s_dispH = 0;
static const char* disp_fmt_name(u32 f) {
    switch (f) { case 21: return "A8R8G8B8 (32-bit)"; case 22: return "X8R8G8B8 (32-bit)";
                 case 23: return "R5G6B5 (16-bit)";   case 24: return "X1R5G5B5 (16-bit)";
                 case 25: return "A1R5G5B5 (16-bit)"; default: return "?"; }
}
void box_skins_forget();    // per-box Custom->FFXI skins (box_style.cpp) : forget on a device change
void box_skins_dispose();   // ... release at shutdown

// Poll the OS cursor + left button and map into the HUD coord space. The plugin runs inside the
// game process, so Win32 gives us the cursor directly (the IPlugin mouse slot doesn't carry it).
// Cursor is client-relative to the focused (game) window, scaled by client size -> coord space.
static void poll_mouse(MouseState& m, float coordW, float coordH, HWND gameHw) {
    POINT p;
    if (!GetCursorPos(&p)) { m.clicked = false; m.down = false; m.backClicked = false; m.back = false; return; }
    HWND fg = GetForegroundWindow();
    // Two SEPARATE questions, deliberately not merged (they used to be, and that was the double-cursor bug) :
    //   focused  -> may we ACT on input ? Only when the game is the OS foreground, else a click meant for the
    //               browser would register here.
    //   overGame -> do we OWN the pointer ? True whenever it is physically over the game window, focus or not,
    //               because the game holds mouse capture and keeps reading the mouse while unfocused.
    // Position follows overGame (so our pointer can be drawn while another window has focus) ; the BUTTON
    // follows focused. See aio_plugin_mouse for the input side.
    m.focused = (gameHw != nullptr && fg == gameHw);
    // Is the pointer over the GAME window, foreground or not ? GA_ROOT so a child/render window still
    // resolves to the top-level game window. This drives who OWNS the pointer, independently of focus.
    HWND under = WindowFromPoint(p);
    m.overGame = (gameHw != nullptr && under != nullptr && GetAncestor(under, GA_ROOT) == gameHw);
    if (!m.focused && !m.overGame) { m.clicked = false; m.down = false; m.backClicked = false; m.back = false; return; }
    // Map through the GAME window, never through `fg` : when the game is not foreground, `fg` is the OTHER
    // app, and mapping into ITS client rect put our drawn pointer in the wrong place entirely.
    POINT cp = p; ScreenToClient(gameHw, &cp);
    RECT rc;
    if (GetClientRect(gameHw, &rc)) {
        float ww = (float)(rc.right - rc.left), wh = (float)(rc.bottom - rc.top);
        if (ww > 1.0f && wh > 1.0f) { m.x = (float)cp.x * coordW / ww; m.y = (float)cp.y * coordH / wh; }
    }
    // POSITION is tracked whenever the pointer is over the game (so our pointer can be drawn while another app
    // has focus) but the BUTTON is only ever read when the game is focused -- otherwise a click meant for the
    // other application would register as a config click here.
    if (!m.focused) { m.clicked = false; m.down = false; m.backClicked = false; m.back = false; return; }
    bool down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    m.clicked = down && !m.down;   // press edge = one-shot click
    m.down    = down;
    const bool bk = (GetAsyncKeyState(VK_XBUTTON1) & 0x8000) != 0;   // thumb BACK : same press edge, same focus gate
    m.backClicked = bk && !m.back;
    m.back        = bk;
}

Hud::Hud()  { add_default(); }   // show the fioles even before a layout is applied
Hud::~Hud() { clear_widgets(); }

void Hud::clear_widgets() {
    for (size_t i = 0; i < widgets_.size(); ++i) { widgets_[i]->dispose(); delete widgets_[i]; }
    widgets_.clear();
    bars_ = nullptr;
}

void Hud::add_default() {
    LiquidBars* b = new LiquidBars(&state_);   // default origin (px_=1000, py_=520)
    bars_ = b;
    widgets_.push_back(b);
}

// The per-widget "placed" lines are only worth reading right after a layout is (re)loaded -- at plugin load and on
// //aio layout. Logged on every placement pass they were 11 % of aiohud_debug.log (2026-09-14 audit) : a re-place
// follows every party-size footprint change and every resolution correction, and each one repeated ~14 lines that
// said nothing new. apply_layout ARMS the detail ; the first placement made at a size READ FROM THE DEVICE logs it
// and disarms. Not the placement inside apply_layout itself at load : that one still uses the guessed default
// resolution, and its px would be corrected a frame later -- the lines worth keeping are the real ones.
static bool s_placeDetailArmed = false;   // a layout was (re)loaded : log the next real placement widget by widget
static bool s_screenFromDevice = false;   // screenW_/screenH_ have been read from the device at least once

void Hud::apply_layout(const char* path) {
    Layout lay;
    if (!load_layout(path, lay)) {
        windower::debug::log("apply_layout: LOAD FAILED <%s> (keeping default)", path);
        return;                                 // missing/invalid -> keep the current widgets
    }
    layout_ = lay;                              // keep the descriptor so we can re-place on a resolution change
    have_layout_ = true;
    layout_path_ = path;                        // remember for hot-reload (//aio layout)
    s_placeDetailArmed = true;
    place_widgets();
}

// Build + place every widget from the stored descriptor at the CURRENT screen size.
// Split out of apply_layout so a resolution change (update_screen) can re-place without
// re-reading the file -- the px positions depend on screenW_/screenH_, which are only
// known once the real device viewport has been read (the hard-coded default is wrong).
void Hud::place_widgets() {
    if (!have_layout_) return;
    // UI scale: the % positions are resolution-independent, but the px SIZES were
    // authored against the export viewport -> scale them to the real screen width so
    // widgets keep their intended relative size (and text stays readable).
    ui_scale_ = (layout_.vpW > 1.0) ? screenW_ / (float)layout_.vpW : 1.0f;
    if (ui_scale_ < 0.5f) ui_scale_ = 0.5f; if (ui_scale_ > 3.0f) ui_scale_ = 3.0f;
    fonts_.set_default(layout_.font.c_str(), layout_.fontWeight);   // global HUD face (per-text faces resolve through the cache)
    const bool logEach = s_placeDetailArmed && s_screenFromDevice;
    if (logEach) s_placeDetailArmed = false;
    windower::debug::log("place_widgets: %d widgets (screen %dx%d, scale %d%%)%s",
                         (int)layout_.widgets.size(), (int)screenW_, (int)screenH_, (int)(ui_scale_ * 100),
                         logEach ? "" : " -- per-widget detail only after a layout (re)load");
    clear_widgets();
    for (size_t i = 0; i < layout_.widgets.size(); ++i) {
        const LWidget& lw = layout_.widgets[i];
        Widget* w = make_widget(lw.type, &state_);
        if (!w) continue;                       // type not implemented natively yet -> skip
        if (lw.type == "PlayerHub") bars_ = ((Player*)w)->provider();   // the Player Hub embeds the fioles -> that LiquidBars is the shared vial provider
        w->configure(lw.config);
        w->set_scale(ui_scale_);
        float cw = -1.0f, ch = -1.0f; w->measure(cw, ch);
        // a widget that reports its size (measure) is authoritative (it knows its own
        // scaled/boosted dimensions) ; otherwise fall back to the descriptor's fixed w.
        float effW = (cw > 0.0f) ? cw : (float)lw.w * ui_scale_;
        PxRect r = widget_px(lw, screenW_, screenH_, effW, ch);
        w->set_place(r.x, r.y, lw.z, lw.visible, lw.bare);
        widgets_.push_back(w);
        if (logEach)
            windower::debug::log("  placed %-10s %-12s -> px(%d,%d) z=%d vis=%d",
                                 lw.id.c_str(), lw.type.c_str(), (int)r.x, (int)r.y, lw.z, (int)lw.visible);
    }
    if (widgets_.empty()) add_default();        // nothing implementable -> keep the fioles visible
    std::sort(widgets_.begin(), widgets_.end(),
              [](Widget* a, Widget* b) { return a->z() < b->z(); });
    windower::debug::log("place_widgets: %d widget(s) drawable", (int)widgets_.size());
}

// Read the real backbuffer size from the device. The placement canvas is in true screen
// pixels (XYZRHW), so a wrong resolution shifts every anchored widget -- e.g. a too-short
// screenH_ leaves a gap below the bottom-right party box. Re-place when it changes.
void Hud::update_screen(u32 dev) {
    // read the TRUE backbuffer size (follows a windowed resize / snap), falling back to the
    // current viewport if GetBackBuffer isn't available.
    u32 bw = 0, bh = 0;
    if (!dGetBackBufferSize(dev, bw, bh)) {
        D3DVIEWPORT8 vp;
        if (!dGetViewport(dev, vp) || vp.Width < 640 || vp.Height < 480) return;
        bw = vp.Width; bh = vp.Height;
    }
    s_screenFromDevice = true;
    if ((float)bw == screenW_ && (float)bh == screenH_) return;
    windower::debug::log("screen resolution %dx%d -> %ux%u (re-placing widgets)",
                         (int)screenW_, (int)screenH_, bw, bh);
    screenW_ = (float)bw;
    screenH_ = (float)bh;
    place_widgets();
}

void Hud::render(u32 dev) {
    if (!valid_ptr(dev)) return;

    // THE SAFETY HARNESS. Registered here, on the first frame, rather than at static-init: the registry is a
    // plain array with no order dependency, and doing it where the module is known to be alive avoids a
    // static-init-order question for no benefit. Idempotent, so it costs one comparison per frame.
    static bool s_checksRegistered = false;   // rule10-ok: appending to an array cannot fail transiently
    if (!s_checksRegistered) { s_checksRegistered = true;
        timers_register_checks(); rva_register_checks();      // the modules that own state worth doubting
        flip_register_checks();   cap_register_checks();      // the watchers (model/watchdogs.h : all knobs, one switch)
        zt_register_checks();     lc_register_checks();
        gear_register_checks();   minimap_register_checks(); }   // game-file readers : a patch that changes a DAT format
    gear_canary_tick(GetTickCount());   // one DAT probe per frame at session start, then nothing

    // The watcher decides for itself whether it is armed and whether it is due ; on the overwhelming majority
    // of frames this returns 0 having touched nothing. When a check has held long enough to be believed, the
    // CONTEXT half of the report is ours -- version, character, zone, paths -- because that lives here.
    {
        CheckFail hits[SELFTEST_FAILS_MAX];
        const int nh = selftest_tick(GetTickCount(), hits, SELFTEST_FAILS_MAX);
        if (nh > 0) write_bug_report(hits, nh, true);   // the watcher : these held
    }

    // Process a deferred //aio layout hot-reload HERE (render thread) so the widget delete/
    // rebuild never races the draw loop (doing it from the command thread crashes the game).
    if (reload_pending_) {
        reload_pending_ = false;
        __try { if (!layout_path_.empty()) apply_layout(layout_path_.c_str()); }
        __except (EXCEPTION_EXECUTE_HANDLER) { windower::debug::log("layout reload threw (SEH) -- kept old widgets"); }
    }

    // Correct the placement canvas to the real backbuffer size (the load-time default is
    // a guess) -- this snaps anchored widgets flush to their screen edges.
    update_screen(dev);

    // The game recreates its D3D device around zoning. Our textures (incl. the font
    // atlas) belong to the OLD device -> forget them (without releasing: the old device
    // may be dead) so they rebuild on the NEW one.
    if (dev != last_dev_) {
        if (last_dev_) windower::debug::log("DEV CHANGED %08X -> %08X (rebuild)", last_dev_, dev);
        last_dev_ = dev;
        fonts_.on_device_lost();
        skin_.on_device_lost();
        window_materials_reset();   // forget the procedural box-theme material textures -> regenerated on next draw
        config_.on_device_lost();   // forget the config logo texture -> reloaded on next draw
        tpCoffer_ = 0; tpCoffer_r_ = {};   // forget the treasure-pool coffer icon (belongs to the old device)
        weaponIcons_ = 0; weaponIcons_r_ = {};   // forget the Sheol weapon-type icon atlas
        buffAtlas_ = 0; buff_atlas_forget();   // the SHARED status-icon atlas : forget the one handle every box borrows, and RE-ARM its retry
                                               // budget. This is the re-arm the shared budget depends on (buff_atlas.cpp) -- it fires on every
                                               // device recreate, i.e. every zone-in, which is exactly when a "device not ready" miss happens.
        grimLight_ = 0; grimDark_ = 0; grimClosed_ = 0; grimLight_r_ = {}; grimDark_r_ = {}; grimClosed_r_ = {};   // forget the grimoire book textures (belong to the old device)
        // The Help SAMPLES keep their own lazy copies at file scope in their hud_*.cpp -- unreachable from here
        // otherwise, so they used to survive a device recreate and hand a dead device's texture to SetTexture.
        zonetracker_help_forget(); treasure_help_forget();   // (the debuffs / timers Help samples borrow the shared atlas forgotten just above)
        box_skins_forget();        // per-box Custom->FFXI skins belong to the old device too
        corner_mask_forget();      // the baked corner-mask atlas belongs to the old device (and this re-arms its retry)
        for (size_t i = 0; i < widgets_.size(); ++i) widgets_[i]->on_device_lost();
    }
    fonts_.get(0, 0);          // register the default slot so ensure_all builds it this frame
    fonts_.ensure_all(dev);
    if (ui_config().skinTheme != skinIdx_) set_skin(ui_config().skinTheme);   // config page changed the theme
    if (ui_config().box[0].scale < 1.0f) ui_config().box[0].scale = 1.0f;   // PARTY floor = 100% : below that its footprint can't cover the native party block (it may still grow)
    {   // Re-anchor any box whose FOOTPRINT changed -- scale, bar height/width, job-badge mode, casts on/off,
        // anything. measure() captures every dimension setting (right = x+w, bottom = y+h ; exact for the
        // bottom-anchored party too). A HAND-PLACED box stores its TOP-LEFT, so we shift it by the size delta
        // to keep its BOTTOM-RIGHT corner fixed (it grows UP-LEFT). Layout-anchored boxes re-anchor via
        // place_widgets(). A config/profile load ADOPTS the new sizes as baseline (no re-anchor).
        const bool baseline = take_scale_baseline_reset();
        bool saved = false, needPlace = false;
        for (size_t i = 0; i < widgets_.size(); ++i) {
            if (strcmp(widgets_[i]->type_name(), "PartyList") != 0) continue;
            Party* p = static_cast<Party*>(widgets_[i]);
            const int b = p->tier(); if (b < 0 || b > 2) continue;
            float nw = 0.0f, nh = 0.0f; p->measure(nw, nh);
            if (baseline || lastW_[b] < 0.0f) { lastW_[b] = nw; lastH_[b] = nh; continue; }   // adopt as baseline
            if (nw == lastW_[b] && nh == lastH_[b]) continue;
            if (ui_config().box[b].posSet && screenW_ > 0.0f && screenH_ > 0.0f) {
                ui_config().box[b].x += (lastW_[b] - nw) / screenW_;   // keep bottom-right pinned
                ui_config().box[b].y += (lastH_[b] - nh) / screenH_;
                saved = true;
            } else {
                needPlace = true;                                      // layout-anchored -> re-anchor via place_widgets
            }
            lastW_[b] = nw; lastH_[b] = nh;
        }
        // DEBOUNCED, not per-frame. Dragging a size slider changes the footprint on every frame, and both of
        // these are heavy : save_ui_config() rewrites the whole config file atomically, and place_widgets()
        // tears down and rebuilds every widget -- reloading assets and re-decoding the zone map DAT with it.
        // Paying either 60x a second while the user drags is the same defect the config panels already fixed on
        // their own side (they persist on RELEASE), reintroduced here by the consumer that reacts to the result.
        // The correction above is still applied immediately, so the box tracks the slider; only the consequences
        // wait for it to settle. Re-stamping on every change means the deadline is "N ms after the LAST change".
        const unsigned nowMs2 = GetTickCount();
        if (saved)     savePendMs_  = (nowMs2 + 500u) | 1u;   // | 1 : keep 0 free as the "nothing pending" sentinel (a raw tick can BE 0)
        if (needPlace) placePendMs_ = (nowMs2 + 150u) | 1u;   // shorter : this one is visible
        if (savePendMs_  && (int)(nowMs2 - savePendMs_)  >= 0) { savePendMs_  = 0; save_ui_config(); }
        if (placePendMs_ && (int)(nowMs2 - placePendMs_) >= 0) { placePendMs_ = 0; place_widgets(); }
    }
    if (!window_theme_is_proc(skinIdx_) && !skin_.ready()) skin_.load(dev, window_theme_name(skinIdx_));   // FFXI window skin (lazy, shared by the party + Same-as-Party boxes) ; procedural themes have no texture. A Custom->FFXI box uses its OWN variant via box_ffxi_skin (box_style.cpp).

    // ONE poll of live game memory for the WHOLE frame -> the shared snapshot every widget
    // draws from (player vitals/jobs, target, leaders, action menu). Read each pointer-chain
    // once here, never in a widget's draw(). See gamestate.h.
    poll_game_state(state_);
    profile_sync_poll();   // another client on this Windower saved the profile we are on -> re-apply it (throttled 1/s)
    drain_game_text();     // Omen / Nyzul chat lines queued by the TEXT thread -> dispatched here, on the main thread
    drain_commands();      // //aio commands queued by the COMMAND thread (tid differs) -> executed here, on the main thread

    // GATE the whole HUD on "logged in / in the world". read_player STILL succeeds while zoning, so it alone can't
    // hide during a zone -- but the client sends 0x00B (zone-out) then 0x00A (zone-in), which set party().zoning_
    // (same source Windower uses). Hide when zoning, or before the first successful poll (char-select / POL). A few
    // frames of grace on the inGame side absorb a 1-frame hiccup so the HUD never blinks mid-fight ; a zone hides
    // immediately (no grace) via the explicit flag.
    const bool zoning = party().is_zoning();
    // The baked corner masks are TEXTURE ALPHA, which samples as ~255 while a zone loads (d3d8-rendering 3c) --
    // every rounded corner would square off for the length of the load. Gate them off for those frames and the
    // shapes fall back to the feathered geometry they used before the mask existed.
    corner_mask_enable(!zoning);
    const bool ready = state_.inGame && !zoning;
    if (ready) { everInGame_ = true; notReadyFrames_ = 0; }
    else ++notReadyFrames_;
    const bool worldReady = ready || (everInGame_ && !zoning && notReadyFrames_ <= 5);

    // The model upkeep (roster refresh, ally-buff wear-off, debuff/hate/skillchain/treasure prunes, zone tracker,
    // EmpyPop) is ONE model event, run by the model itself from what this frame's snapshot says -- the offline replay
    // drives the same function from a tape (model_frame_upkeep, party_state.cpp).
    {
        FrameInput in;
        in.targetId = state_.target.id; in.meId = state_.me.id;
        in.targetValid = state_.target.valid ? 1 : 0; in.targetSpawn = state_.target.spawnType; in.targetHpp = state_.target.hpp;
        in.subPresent = state_.hasSubTarget ? 1 : 0; in.subValid = state_.subTarget.valid ? 1 : 0;
        in.subId = state_.subTarget.id; in.subSpawn = state_.subTarget.spawnType; in.subHpp = state_.subTarget.hpp;
        in.zone = state_.zone;
        lstrcpynA(in.epTrack, ui_config().epTrack, sizeof(in.epTrack));
        model_event_begin('F');
        model_frame_upkeep(in);
        model_event_end();
    }
    profile_autoload_tick();   // auto-switch profile when the character's Name/Main/Sub changes (login / job change)
#ifdef AIOHUD_DEVTOOLS
    devtools::after_upkeep(state_);   // dev-only (dev/src) : the in-game test bridge's whole-model dump, after this frame's poll and upkeep
#endif

    for (size_t i = 0; i < widgets_.size(); ++i) widgets_[i]->ensure(dev);

    Frame f;
    f.dev   = dev;
    f.fonts = &fonts_;
    f.font  = fonts_.get(0, 0);   // the default atlas (global face/weight) for non-party widgets
    f.t     = (float)(GetTickCount() % 1000000) / 1000.0f;
    f.game  = &state_;            // the per-frame snapshot widgets read from
    f.skin  = &skin_;             // the shared FFXI window skin (9-slice chrome)
    poll_mouse(mouse_, screenW_, screenH_, (HWND)dFocusWindow(dev));   // cursor + click for this frame (gated on game focus)
    if (!mouse_.focused) peekHide_ = false;   // lost focus while End held -> the key-up may be missed ; un-peek so the HUD can't stay stuck hidden
    f.mouse = &mouse_;
    f.screenW = screenW_; f.screenH = screenH_;
    // Amortise the load hitch : cap NEW font-atlas bakes per frame so the ~10-15 first-use sizes spread over a few
    // frames instead of freezing frame 1. A running frame count also lets us push one-time warm-ups off the busy
    // first frames.
    static u32 s_frame = 0; ++s_frame;
    font_set_bake_budget(s_frame < 180 ? 2 : (1 << 30));   // throttle only the first ~3s (the load) ; unlimited after
    // ONE state block around ALL our drawing: save the game's render state, set ours,
    // restore afterwards (else we corrupt the game's own rendering). Retained widgets
    // (text/prims) are auto-rendered by Windower outside this block -- harmless here.
    u32 tok = dCreateSB(dev, D3DSBT_ALL);
    __try {
        // Once per session : what are we presenting INTO ? A 16-bit back buffer bands every smooth gradient no
        // matter how it is drawn, and that is a question about the game's settings, not about our geometry --
        // so it is worth knowing before anyone tunes a gradient again. //aio doctor prints it.
        if (!s_dispFmt) { D3DDISPLAYMODE8 dm; if (dGetDisplayMode(dev, dm) && dm.fmt) { s_dispFmt = dm.fmt; s_dispW = dm.w; s_dispH = dm.h; } }
        // PRE-BAKE the WS-popup font atlases off-screen ONCE, but only after the HUD has settled -> the big 58/34px
        // atlases never pile onto the load frames.
        // INSIDE the state block, and this is not cosmetic. Font::begin sets 11 render states + 13 texture-stage
        // states and Font::draw submits real geometry, so running it above the block did two things: it left the
        // game's own state trashed on the way out of EndScene, and -- worse -- dCreateSB then captured the state we
        // had already dirtied, so the ApplySB at the bottom "restored" the FONT's state instead of the game's.
        // This was the only drawing the plugin did outside its own save/restore.
        if (!wsFontWarmed_ && s_frame > 40) {
            Font* pf = fonts_.get(ui_font_face(ui_config().wsFont), 900);
            if (pf) { const float US = (screenH_ / 1400.0f) * ui_config().wsScale; pf->begin(dev);
                      pf->draw(dev, -9999.0f, -9999.0f, "0123456789", 58.0f * US, 0);
                      pf->draw(dev, -9999.0f, -9999.0f, "ABCDEFGHIJKLMNOPQRSTUVWXYZ", 34.0f * US, 0); }
            wsFontWarmed_ = true;
        }
        // When the config LIVE PREVIEW is up, the party/alliance tiers are drawn ONCE by
        // draw_config_preview (repositioned into the stage). Skip them in the normal loop so they
        // aren't drawn twice -- the double draw left the preview pass with dt=0 (animations frozen ->
        // no selection cursor) and ghosted faintly through the dim (names looked truncated).
        float pvx_ = 0.0f, pvy_ = 0.0f; const bool pvActive = config_.preview_anchor(pvx_, pvy_);
        // EDIT LAYOUT "Rules" mode : hide the WHOLE HUD so only the reference lines + the edit toolbar
        // (both drawn by config_.draw below) remain -- you align the rules onto the game's native windows.
        const bool hideForRules = ui_config().editLayout && config_.edit_lines_active();
        // Config PAGE open (but NOT edit-layout, where you must see the boxes to drag them) -> hide the live HUD
        // so the real boxes don't show through the transparent preview stage ; the config preview draws its own demos.
        const bool hideForConfig = config_.is_open() && !ui_config().editLayout;   // the preview stage redraws the boxes ; they must not also draw live
        const bool hideHud = hideForRules || peekHide_ || hideForConfig;   // peek (End held) hides the whole HUD too
        set_vial_provider(bars_);   // let the party rows / Help borrow the real fiole assets this frame (null-safe -> fallback)
        if (worldReady) {           // logged in -> draw the HUD ; not yet (login/char screen) -> only the config overlay below
        for (size_t i = 0; !hideHud && i < widgets_.size(); ++i) {
            const char* tn = widgets_[i]->type_name();
            // preview active -> the party tiers AND the target box are redrawn inside the stage by
            // draw_config_preview ; skip them here so they don't also draw at their live HUD position.
            if (pvActive) {
                // These are redrawn INSIDE the preview stage by draw_config_preview (per section) ; skip the
                // live draw so they don't also show through the transparent preview window.
                if (strcmp(tn, "PartyList") == 0 || strcmp(tn, "TargetBar") == 0 || strcmp(tn, "Minimap") == 0) continue; }
            // MASTER show/hide per module (the config preview draws these separately, so previews stay visible).
            if (strcmp(tn, "PartyList") == 0) { if (static_cast<Party*>(widgets_[i])->tier() == 0 ? !ui_config().partyShow : !ui_config().allyShow) continue; }
            else if (strcmp(tn, "TargetBar") == 0) { if (!ui_config().tgtShow) continue; }
            else if (strcmp(tn, "PlayerHub")  == 0) { if (!ui_config().plrShow && !(ui_config().plrEquip && ui_config().plrEquipDetach)) continue; }   // still run it when a STANDALONE equipment module needs drawing
            widgets_[i]->draw(f);
        }
        for (size_t i = 0; i < widgets_.size(); ++i)   // hand the Help the party's selection-hand texture (for its live cursor sample)
            if (strcmp(widgets_[i]->type_name(), "PartyList") == 0 && static_cast<Party*>(widgets_[i])->tier() == 0) { config_.set_help_cursor_tex(static_cast<Party*>(widgets_[i])->cursor_tex()); break; }
        if (!hideHud) {   // Rules mode / End-peek hide the WHOLE HUD (like the widget loop above) -- these boxes must depop too
            draw_skillchains(f);                    // skillchains box (target's active chain) -- placed via //aio edit
            draw_treasure_pool(f);                  // treasure pool box (lottery items) -- placed via //aio edit
            draw_hate_list(f);                      // hate list box (mobs aggro'd on the party) -- placed via //aio edit
            draw_pointwatch(f);                     // PointWatch box (XP/CP/ML + Merits) -- placed via //aio edit
            draw_grimoire(f);                       // Scholar grimoire (SCH only) -- placed via //aio edit
            draw_zonetracker(f);                    // Zone Tracker (Dynamis/Abyssea only) -- placed via //aio edit
            // EmpyPop -- placed via //aio edit. SKIP the live draw while its own config section is being previewed :
            // unlike ZoneTracker (zone-gated) / Timers (buff-gated), EmpyPop draws whenever epShow is on, so without
            // this it ALSO paints at epX/epY and bleeds through the transparent preview hole -> a second, off-centre
            // box next to the centred sample. pvActive reflects last frame's stage, cleared before edit-layout, so
            // this never hides the box you are dragging in //aio edit.
            if (!(pvActive && config_.section() == 12)) draw_empypop(f);
            // detached target debuffs (a Target sub-feature) -- self-gates on dbShow ; placed via //aio edit.
            // Skip the live draw while the Target config previews it (section 1 + Standalone) : it's redrawn in the stage.
            if (!(pvActive && config_.section() == 1 && ui_config().dbShow)) draw_debuffs(f);
            draw_timers(f);                         // Timers box (self buff timers, exact) -- placed via //aio edit
            draw_ws_popup(f);                       // arcade WS popup, over the HUD but under the config overlay
        }
        }   // end worldReady : boxes hidden until logged in
        config_.draw(f, screenW_, screenH_);   // full-screen config overlay, on top of everything (the Help owns + loads its own zone map)
        clip_rect_reset(f.dev);                // a clip left open would narrow everything drawn after it
        draw_config_preview(f);                // real party+alliance demo boxes inside the config preview stage
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // RATE-LIMITED, not once-per-session. A `static bool logged` meant a widget faulting on EVERY frame
        // produced exactly one line for the whole session -- indistinguishable in the log from a single transient
        // hiccup, while the HUD silently drew nothing. Report the first one immediately, then at most one line
        // every 5 s with the running count, so a REPEATING fault is visible as such (rule 10's corollary).
        // The aborted pass may have left glyphs half-accumulated in the shared text batch. Drop them: the next
        // frame would otherwise append behind them and submit this frame's leftovers -- old positions, old UVs,
        // whatever atlas is bound by then -- as a stray line of ghost text.
        font_reset_batch();
        clip_rect_reset(dev);   // ... and the same for a clip the unwind never closed : the viewport is state too

        static unsigned faults = 0, nextLogMs = 0;
        const unsigned nowMs = GetTickCount();
        ++faults;
        if (faults == 1 || (int)(nowMs - nextLogMs) >= 0) {
            nextLogMs = nowMs + 5000;
            windower::debug::log("HUD draw threw (SEH) -- %u fault(s) so far this session", faults);
        }
    }
    if (!tok) {   // no state block -> the game's own render state is NOT protected from our draws (rule 8)
        static bool sbWarned = false;
        if (!sbWarned) { sbWarned = true; windower::debug::log("WARNING: CreateStateBlock returned 0 -- drawing WITHOUT save/restore this session"); }
    }
    if (tok) { dApplySB(dev, tok); dDelSB(dev, tok); }
}


// //aio doctor -- everything that can only be checked with the game running. Each finding is written as a
// SYMPTOM plus the action that resolves it : a diagnostic that only states facts leaves the reader to guess
// the remedy, which is the part they do not have. Healthy checks are logged, not printed -- the console is a
// scarce resource and a wall of green hides the one red line.
const char* aio_version_string();   // plugin/aiohud.cpp : the build's version string (tag, or "dev")

int Hud::doctor(char out[][DOC_LINE], int maxOut) {
    int n = 0;
    #define DOC(fmt, ...) do { if (n < maxOut) { _snprintf(out[n], DOC_LINE, fmt, __VA_ARGS__); out[n][DOC_LINE-1] = 0; \
                                                 windower::debug::log("  PROBLEM : %s", out[n]); ++n; } } while (0)
    const unsigned nowMs = GetTickCount();
    windower::debug::log("=== AIO DOCTOR : %s ===", aio_version_string());

    // ---- 0. the install. A remote tester's bugs are mostly HERE, and the least visible : a Program Files install
    //         that silently cannot write (config, profiles, reports, the log itself -- all lost without a word), or
    //         another Windower build. None of it was in the doctor, so a NA tester's report had to be asked for twice.
    //         Writing is TESTED, not assumed : the log cannot report that the log is unwritable. ----
    {
        const char* dir = plugin_dir();
        char low[MAX_PATH]; int k = 0;
        for (; dir && dir[k] && k < MAX_PATH - 1; ++k) low[k] = (dir[k] >= 'A' && dir[k] <= 'Z') ? (char)(dir[k] + 32) : dir[k];
        low[k] = 0;
        const bool progFiles = strstr(low, "program files") != 0;
        // data\ holds config + profiles ; before the first save it may not exist yet, then the folder itself is the test
        char probe[MAX_PATH]; plugin_path(probe, sizeof(probe), "data");
        const DWORD da = GetFileAttributesA(probe);
        plugin_path(probe, sizeof(probe), (da != INVALID_FILE_ATTRIBUTES && (da & FILE_ATTRIBUTE_DIRECTORY)) ? "data\\doctor_write_test.tmp" : "doctor_write_test.tmp");
        HANDLE h = CreateFileA(probe, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, 0);
        const bool canWrite = h != INVALID_HANDLE_VALUE; const DWORD werr = canWrite ? 0 : GetLastError();
        if (canWrite) CloseHandle(h);   // FILE_FLAG_DELETE_ON_CLOSE : nothing is left behind
        HANDLE lh = CreateFileA(windower::debug::log_path(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
        const bool logOk = lh != INVALID_HANDLE_VALUE;
        if (logOk) CloseHandle(lh);
        windower::debug::log("  install  : dir=%s  programFiles=%d  write=%s  log=%s (%s)", dir && *dir ? dir : "<unresolved>",
                             progFiles ? 1 : 0, canWrite ? "ok" : "FAILED", logOk ? "ok" : "FAILED", windower::debug::log_path());
        // Which Windower : size and date of its two modules are a fingerprint two testers can compare line by line
        // (the keyboard lParam differs between the EU and NA builds, reference/keyboard-input.md).
        static const char* const MODS[2] = { "LuaCore.dll", "Hook.dll" };
        for (int m = 0; m < 2; ++m) {
            char mp[MAX_PATH] = { 0 }; WIN32_FILE_ATTRIBUTE_DATA fa = {}; SYSTEMTIME st = {};
            HMODULE hm = GetModuleHandleA(MODS[m]);
            const bool have = hm && GetModuleFileNameA(hm, mp, MAX_PATH) && GetFileAttributesExA(mp, GetFileExInfoStandard, &fa);
            if (have) FileTimeToSystemTime(&fa.ftLastWriteTime, &st);
            windower::debug::log("  windower : %-11s %s", MODS[m], have ? mp : "<not loaded>");
            if (have) windower::debug::log("             %lu bytes, %04u-%02u-%02u", fa.nFileSizeLow, st.wYear, st.wMonth, st.wDay);
        }
        if (!canWrite)
            DOC(tr("AioHUD cannot write in its folder (%s, error %lu) : settings, profiles and reports are LOST at every "
                "reload. %sMove Windower out of Program Files, or give your user write access to that folder",
                "AioHUD ne peut pas ecrire dans son dossier (%s, erreur %lu) : reglages, profils et rapports sont PERDUS a "
                "chaque rechargement. %sSors Windower de Program Files, ou donne a ton compte le droit d'ecrire dans ce dossier"),
                dir && *dir ? dir : "?", werr, progFiles ? tr("This is the known Program Files case. ", "C'est le cas connu de Program Files. ") : "");
        if (!logOk)
            DOC(tr("The diagnostic log cannot be written (%s) : every capture command will produce an empty file. "
                "Same remedy as above : write access to the plugins folder",
                "Le journal de diagnostic ne peut pas etre ecrit (%s) : chaque commande de capture produira un fichier vide. "
                "Meme remede : droit d'ecriture sur le dossier plugins"), windower::debug::log_path());
    }

    // ---- 1. the game link. Everything else is meaningless if this fails, so it reports first and alone. ----
    const int roster = party().count;
    windower::debug::log("  link     : inGame=%d selfId=%08X roster=%d zone=%u job=%d",
                         state_.inGame ? 1 : 0, party().self_id(), roster, state_.zone, party().self_main_job());
    windower::debug::log("  luacore  : root rva %06X (%s) live=%d",
                         lc_root_rva(), lc_root_how(), lc_root_live() ? 1 : 0);
    windower::debug::log("  surface  : %ux%u fmt=%u %s", s_dispW, s_dispH, s_dispFmt, disp_fmt_name(s_dispFmt));
    if (s_dispFmt >= 23 && s_dispFmt <= 25)
        DOC(tr("The game is presenting in 16-bit (%s) : every soft gradient will band, in our boxes as in the game, "
            "and nothing on our side will change that much. Switch FFXI to 32-bit in its own config "
            "(Bit Depth) if the banding bothers you.",
            "Le jeu presente en 16 bits (%s) : tout degrade doux affichera des bandes, chez nous comme dans le "
            "jeu, et aucun reglage de notre cote n'y changera grand-chose. Passe FFXI en 32 bits dans son "
            "config (Bit Depth / Profondeur) si les degrades te genent."), disp_fmt_name(s_dispFmt));
    if (!state_.inGame || !party().self_id()) {
        // Which of the two it is decides the remedy, so the check says which. A dead root means Windower
        // moved LuaCore's data root under us (4.7.9.3 did exactly that) and NOTHING can be read ; a live
        // root with no character just means you are at the login screen.
        if (!lc_root_live())
            DOC(tr("AioHUD can no longer find the game's memory : the LuaCore root (rva %06X, %s) does not answer. "
                "That is what happens when Windower updates and moves that address -- update AioHUD "
                "(Update tab). If you are simply at the login screen, this is normal.",
                "AioHUD ne trouve plus la memoire du jeu : la racine LuaCore (rva %06X, %s) ne repond pas. "
                "C'est ce qui arrive quand Windower se met a jour et deplace cette adresse -- mets AioHUD a "
                "jour (onglet Update). Si tu es juste a l'ecran de login, c'est normal."),
                lc_root_rva(), lc_root_how());
        DOC(tr("The plugin cannot see your character (inGame=%d, selfId=%08X). Nothing else can work. "
            "If you really are in game : the DLL does not match your Windower build -- redeploy, then //aio doctor.",
            "Le plugin ne voit pas ton personnage (inGame=%d, selfId=%08X). Rien d'autre ne peut fonctionner. "
            "Si tu es bien en jeu : la DLL ne correspond pas a ta version de Windower -- redeploie, puis //aio doctor."),
            state_.inGame ? 1 : 0, party().self_id());
        windower::debug::log("=== AIO DOCTOR : stopped -- no game link ===");
        return n;   // (no #undef here : the preprocessor knows nothing of control flow, it would kill DOC below)
    }

    // ---- 2. the three memory reads every box depends on ----
    windower::debug::log("  reads    : buffsOk=%d nbuff=%d equipValid=%d mapEnt=%d",
                         state_.buffsOk ? 1 : 0, state_.nbuff, state_.equipValid ? 1 : 0, state_.mapEntN);
    if (!state_.buffsOk)
        DOC(tr("Your own buffs do not read (buffsOk=0) : the Timers box and its filter will stay empty. "
            "Normal during a zone load -- run //aio doctor again once you have arrived%s",
            "Tes propres buffs ne se lisent pas (buffsOk=0) : la boite Timers et le filtre resteront vides. "
            "Normal pendant un chargement de zone -- relance //aio doctor une fois arrive%s"), "");
    if (!state_.equipValid)
        DOC(tr("Equipment does not read (equipValid=0) : the gear icons keep the previous cache. "
            "Normal while zoning ; persistent = the item containers are not ready%s",
            "L'equipement ne se lit pas (equipValid=0) : les icones de gear gardent le cache precedent. "
            "Normal en zoning ; persistant = les conteneurs d'objets ne sont pas prets%s"), "");

    // ---- 2b. the FFXiMain statics. Every OTHER read hangs off LuaCore, which a game patch does not touch --
    //          so when the client updates, THESE are what break, alone, and the symptom is a single feature
    //          going dead (2026-08-12 : target_t moved 0x40 bytes, the party selection cursor stopped
    //          following <t>, and nothing anywhere said why). The chain is up if it resolves at all ; whether
    //          something is currently targeted is not the question here. ----
    {
        const unsigned troot = target_root();
        windower::debug::log("  statics  : client fingerprint %08X -- target_t=%08X targetId=%08X menuType=%d",
                             fm_fingerprint(), troot, state_.targetId, state_.menuType);
        { char fl[FM_N][160]; const int fn = fm_report(fl, FM_N);
          for (int i = 0; i < fn; ++i) windower::debug::log("             %s", fl[i]); }
        // The cross-checks. A DIVERGED pair is the loudest thing this command can say : the plugin is
        // still drawing numbers, and one of the two sources feeding them no longer means what it did.
        { char sl[SEN_N][160]; const int sn = sentinel_report(sl, SEN_N);
          for (int i = 0; i < sn; ++i) windower::debug::log("  crosscheck : %s", sl[i]); }
        for (int i = 0; i < SEN_N; ++i)
            if (sentinel_diverged((SentinelPair)i))
                DOC(tr("The server and the game's memory no longer tell the same story (%s). This is NOT a moved "
                    "address -- it does not repair itself : a field moved inside a structure or a packet. What is "
                    "drawn from here on can be wrong WITHOUT looking wrong. Detail in aiohud_debug.log, SENTINEL line",
                    "Le serveur et la memoire du jeu ne racontent plus la meme chose (%s). Ce n'est PAS une "
                    "adresse deplacee -- ca ne se repare pas tout seul : un champ a bouge dans une structure "
                    "ou dans un paquet. Ce qui s'affiche a partir de la peut etre faux SANS avoir l'air faux. "
                    "Detail dans aiohud_debug.log, ligne SENTINEL"), sentinel_report_name((SentinelPair)i));
        // A CONTRADICTION, which is the only kind of static worth alarming on : the game says a menu is open
        // (menuType != 0, so the box is on screen) while the cache that fills it reads nothing usable. That
        // is the "box pops but stays empty" report, named at its cause instead of left to be re-diagnosed.
        if (state_.menuType != 0 && state_.menuAction == 0 &&
            !fm_confirmed((state_.menuType == 1) ? FM_EXAM_SPELL : FM_EXAM_ABIL))
            DOC(tr("The cost/Next box is drawn but stays EMPTY : the game does have a menu open (type %d) and "
                "the address carrying the highlighted action has not proven itself. Move the cursor over a "
                "few spells/abilities : it re-locks itself by following the highlight%s",
                "La boite cout/Next s'affiche mais reste VIDE : le jeu a bien un menu ouvert (type %d) et "
                "l'adresse qui porte l'action surlignee n'a pas fait ses preuves. Bouge le curseur sur "
                "quelques sorts/abilites : elle se recale toute seule en suivant le surlignage%s"),
                state_.menuType, "");
        if (!troot)
            DOC(tr("The TARGET chain is dead (target_t not found) : the selection cursor will no longer follow "
                "anyone in the party, and the Target box will stay empty. That is the signature of an FFXI "
                "UPDATE that moved the addresses. Target a member then run //aio rva : it finds the new "
                "address by itself%s",
                "La chaine de CIBLE est morte (target_t introuvable) : le curseur de selection ne suivra plus "
                "personne dans la party, et la boite Target restera vide. C'est la signature d'une MISE A JOUR "
                "de FFXI qui a deplace les adresses. Cible un membre puis lance //aio rva : il retrouve la "
                "nouvelle adresse tout seul%s"), "");
    }

    // ---- 3. packet flow. A box that shows nothing because NO PACKET ARRIVES looks exactly like a broken
    //         reader from the outside ; this is the check that tells the two apart. ----
    int np = 0; const PartyState::PktFlow* pf = party().pkt_flow(np);
    for (int i = 0; i < np; ++i)
        windower::debug::log("  packet   : 0x%03X n=%u last=%us ago", pf[i].id, pf[i].n,
                             pf[i].lastMs ? (nowMs - pf[i].lastMs) / 1000u : 0u);
    // A raw zero is NOT a fault : solo, no party buff packet ever arrives ; before you act, no action packet
    // does either. Only a CONTRADICTION is worth reporting -- a packet that the current situation says must be
    // flowing and is not. Anything else stays in the log, where it answers the question when one is asked.
    // (The first live run of this command flagged "no 0x076" while solo. An alarm that cries wolf on its very
    //  first use is never read again, so the rule is now stated in terms of the roster.)
    unsigned n076 = 0, n028 = 0;
    for (int i = 0; i < np; ++i) { if (pf[i].id == 0x076) n076 = pf[i].n; if (pf[i].id == 0x028) n028 = pf[i].n; }
    if (n028 == 0)
        windower::debug::log("  note     : no 0x028 seen yet -- cast or attack once, then re-run (expected right after a load)");
    // NB : 0x076 is EVENT-driven, not periodic -- it is sent when a member's buff list CHANGES. Being in a
    // party proves nothing (a trust that receives nothing produces none), which is why the earlier
    // roster-based rule was wrong twice. The genuine contradiction is tracking ally buffs while never
    // receiving the packet that confirms and prunes them : the rows then ride their estimate to the end.
    // Checked after the model section below, where the ally count is known.

    // ---- 3b. WS popup : a weaponskill whose target message is not on the (measured) whitelist fails CLOSED.
    //          Correct -- a missing popup beats one under a colliding ability's name -- but the whitelist was
    //          measured on ONE character, so on another the popup can be silently dead for every weaponskill.
    //          The evidence already lands in the log ; nobody reads a log for a missing cosmetic. Say it HERE,
    //          where the user can see it on demand, and give the exact line to send back. ----
    {
        unsigned short sid = 0, smsg = 0; const char* sname = 0;
        const unsigned nsup = party().ws_suppressed(sid, smsg, sname);
        windower::debug::log("  wspopup  : suppressed=%u last id=%u msg=%u '%s'", nsup, sid, smsg, sname ? sname : "-");
        if (nsup)
            DOC(tr("The weaponskill popup was suppressed %u times (last : '%s', id %u, message %u). If that "
                "really WAS a weaponskill, message %u must join the whitelist -- report it with this line.",
                "Le popup de weaponskill a ete supprime %u fois (dernier : '%s', id %u, message %u). Si c'ETAIT bien un "
                "weaponskill, le message %u doit rejoindre la liste blanche -- signale-le avec cette ligne."),
                nsup, sname && sname[0] ? sname : "?", (unsigned)sid, (unsigned)smsg, (unsigned)smsg);
    }

    // ---- 3c. The 0x02A message ids. These are ZONE-RELATIVE and the client renumbers them at every patch --
    //          the second family that a game update kills, after the FFXiMain statics, and the quieter one :
    //          on 2026-08-12 the Sheol box kept drawing its header and "Segments" simply stayed at 0. Odyssey
    //          and both Limbus wings now re-derive their id from the payout arithmetic (two payouts), so what
    //          is reported here is the STATE ; the only thing worth alarming on is the contradiction "the zone
    //          is talking and our id is not". Abyssea cannot prove a base, so it is counted, not healed. ----
    {
        const int ztm = party().zone_tracker().mode;
        static const char* WHO[3] = { "Odyssey (segments)", "Apollyon (unites)", "Temenos (unites)" };
        const int which = (ztm == 5) ? 0 : (party().zone_tracker().curZone == 38) ? 1 : (party().zone_tracker().curZone == 37) ? 2 : -1;
        if (which >= 0) {
            unsigned mid = 0; bool prov = false; int seen = 0, traf = 0;
            zt_msg_state(which, mid, prov, seen, traf);
            windower::debug::log("  msgid    : %s msg=%u %s seen=%d traffic=%d", WHO[which], mid,
                                 prov ? "(DERIVED)" : "(seed)", seen, traf);
            if (!prov && seen == 0 && traf >= 6)
                DOC(tr("The %s counter is silent while the zone is talking (%d messages, none carrying id %u) : a "
                    "client update renumbered the message. It re-locks BY ITSELF on the second gain -- carry on, "
                    "and if it is still 0 after two gains, send aiohud_debug.log",
                    "Le compteur %s est muet alors que la zone parle (%d messages, aucun avec l'id %u) : la mise a "
                    "jour du client a renumerote le message. Il se recale TOUT SEUL au deuxieme gain -- continue, et "
                    "s'il reste a 0 apres deux gains, envoie aiohud_debug.log"), WHO[which], traf, mid);
        }
        if (ztm == 2) {   // Abyssea : matched by an OFFSET from a per-zone base, which already drifted +23 once
            int am = 0, au = 0; zt_aby_msg_state(am, au);
            windower::debug::log("  msgid    : Abyssea base=%d matched=%d unmatched=%d", party().zone_tracker().abyOffset, am, au);
            { unsigned short mid[10]; short rel[10]; int pp[10]; int nm = 0, tot = 0;
              zt_aby_misses(mid, rel, pp, 10, nm, tot);
              for (int i = 0; i < nm; ++i)
                  windower::debug::log("             unmatched id %u -> offset %d, p1=%d (nothing is mapped there)", (unsigned)mid[i], (int)rel[i], pp[i]);
              if (nm) windower::debug::log("             %d distinct unmatched id(s) of %d message(s) -- compare these offsets with 0,1,9,10,12,45,183..189", nm, tot); }
            if (am == 0 && au >= 12)
                DOC(tr("No Abyssea message is recognised (%d received, 0 used) : the id base moved with a client "
                    "update. Unlike Odyssey, this one CANNOT be guessed -- do /heal then send aiohud_debug.log, "
                    "the new base reads out of the capture",
                    "Aucun message d'Abyssea n'est reconnu (%d recus, 0 exploite) : la base des ids a bouge avec une "
                    "mise a jour du client. Contrairement a Odyssey, celle-ci ne peut PAS se deviner -- fais /heal "
                    "puis envoie aiohud_debug.log, la nouvelle base se lit dans la capture"), au);
        }
    }

    // ---- 4. textures : a missing handle whose retry budget is SPENT is permanent for this session ----
    int texMiss = 0;
    if (!buffAtlas_)  { ++texMiss; DOC(tr("The status-icon atlas is not loaded (%u tries) : buff icons are missing everywhere. "
        "The icons come from an XIPivot pack or from the game's own DAT, otherwise from "
        "plugins\\AioHud\\assets\\buff_atlas.raw -- check that file exists, then //unload + //load",
        "L'atlas d'icones de statut n'est pas charge (%u essais) : les icones de buff manquent partout. "
                                       "Les icones viennent d'un pack XIPivot ou du DAT du jeu, sinon de "
                                       "plugins\\AioHud\\assets\\buff_atlas.raw -- verifie que ce fichier existe, puis //unload + //load"), buff_atlas_tries()); }
    if (!weaponIcons_) ++texMiss;
    if (!tpCoffer_)    ++texMiss;
    windower::debug::log("  textures : atlas=%d(t%u,%s) weapon=%d coffer=%d grim=%d/%d/%d  (missing=%d)",
                         buffAtlas_ ? 1 : 0, buff_atlas_tries(), buff_atlas_source(), weaponIcons_ ? 1 : 0, tpCoffer_ ? 1 : 0,
                         grimLight_ ? 1 : 0, grimDark_ ? 1 : 0, grimClosed_ ? 1 : 0, texMiss);
    const char* rk = 0; const char* rom = ffxi_rom_dir_probe(&rk);
    windower::debug::log("  romdir   : %s (key %s)", rom ? rom : "<unresolved>", rk ? rk : "<none>");
    { char cs[256]; gear_canary_summary(cs, sizeof(cs)); windower::debug::log("  gearcan  : %s", cs); }
    if (!rom) DOC(tr("FFXI's ROM folder cannot be found : equipment icons will show as text. Install outside the "
        "standard registry entry -- the known case is an install under Program Files%s",
        "Le dossier ROM de FFXI est introuvable : les icones d'equipement s'afficheront en texte. "
                  "Installation hors registre standard -- c'est le cas connu des installs sous Program Files%s"), "");

    // ---- 5. model state : what the boxes are actually holding right now ----
    int nob = 0; party().other_buffs(nob);
    int nbt = 0; party().buff_timers(nbt);
    int nhr = 0; party().hate_rows(nhr);
    windower::debug::log("  model    : selfTimers=%d allyBuffs=%d hateRows=%d zoneGrace=%d",
                         nbt, nob, nhr, party().in_zone_grace() ? 1 : 0);
    // Per ally-buff row : is it CONFIRMED by the member's own buff list (0x076) ? The per-ally row -- the
    // "Name - Spell" one, as opposed to the grouped "(AoE N)" -- is dropped when it is not, so this is the
    // line that explains a row that pops and then vanishes. A member with no BuffSet at all has simply never
    // been described by a 0x076.
    int unconfirmedPlayers = 0;
    {
        const PartyState::OtherBuff* obp = party().other_buffs(nob);
        for (int i = 0; i < nob && i < 8; ++i) {
            const BuffSet* bs = party().buffs_for(obp[i].target);
            bool has = false; if (bs) for (int j = 0; j < bs->n; ++j) if (bs->ids[j] == obp[i].status) { has = true; break; }
            const bool trust = party().is_trust(obp[i].target);
            windower::debug::log("  ally[%d]  : '%s' id=%08X trust=%d status=%u buffset=%s confirmed=%d",
                                 i, obp[i].name[0] ? obp[i].name : "<no name>", obp[i].target,
                                 trust ? 1 : 0, obp[i].status, bs ? "yes" : "NONE", has ? 1 : 0);
            if (!has && !trust) ++unconfirmedPlayers;
        }
    }
    // Only PLAYERS are worth reporting : the server sends no 0x076 for a trust at all, so an unconfirmed
    // trust row is the normal state, not a fault -- it rides its estimate by design.
    if (unconfirmedPlayers > 0)
        DOC(tr("%d buff(s) you put on PLAYERS are not confirmed by their own buff list (0x076) : an early "
            "end (dispel, overwrite, death) will not be detected and the row will run to the end of its "
            "estimate. Trusts are normal : the server does not broadcast their buffs.",
            "%d buff(s) que tu as poses sur des JOUEURS ne sont pas confirmes par leur liste de buffs (0x076) : "
            "leur fin anticipee (dispel, ecrasement, mort) ne sera pas detectee et la ligne tiendra jusqu'au "
            "bout de son estimation. Les trusts, eux, sont normaux : le serveur ne diffuse pas leurs buffs."), unconfirmedPlayers);

    // ---- 6. the current target's debuffs, and whether we know the TIER of each ----
    if (state_.target.valid && state_.target.id) {
        unsigned short ids[16], sp[16]; int rem[16]; unsigned char self[16], sh[16];
        const int nd = party().target_debuffs(state_.target.id, ids, rem, self, 16, sp, sh);
        int known = 0, shots = 0;
        for (int i = 0; i < nd; ++i) { if (sp[i]) ++known; if (sh[i]) ++shots; }
        windower::debug::log("  target   : '%s' debuffs=%d tierKnown=%d quickDraw=%d", state_.target.name, nd, known, shots);
        if (nd > 0 && known == 0)
            windower::debug::log("  note     : no tier known on this mob -- the plugin saw none of those casts (normal if they predate your arrival)");
    }

    // ---- 7. config / layout : the two files whose loss is silent ----
    windower::debug::log("  config   : profile='%s' dirty=%d layout=%s",
                         active_profile_name(), profile_dirty() ? 1 : 0,
                         have_layout_ ? layout_path_.c_str() : "<default, no file loaded>");
    if (!have_layout_)
        DOC(tr("No layout loaded : the boxes are at their default positions. "
            "design\\exports\\layout.json is missing or unreadable%s",
            "Aucun layout charge : les boites sont a leur position par defaut. "
            "design\\exports\\layout.json est absent ou illisible%s"), "");
    if (profile_dirty())
        windower::debug::log("  note     : the live config differs from the saved profile (unsaved changes)");

    // ---- the PASSIVE registry. Everything above is a check doctor performs itself ; these are the checks the
    // modules registered with selftest_add() -- the RVA sweeps, the saturated tables, the oscillating decisions,
    // the silent zone msgid. They were built to run on their own every 30 s, and MEASURED on 2026-09-12 they ran
    // for nobody: the periodic watcher is opt-in (`selfTest` defaults to 0) and doctor -- the one command a
    // tester is told to type -- never consulted the registry at all. So it does now, unconditionally, because a
    // diagnostic you have to arm before the bug arrives is a diagnostic you do not have.
    {
        CheckFail hits[SELFTEST_FAILS_MAX];
        const int nh = selftest_run_now(hits, SELFTEST_FAILS_MAX);
        windower::debug::log("  registry : %d module(s) checked, %d finding(s), periodic watcher %s",
                             selftest_module_count(), nh, selftest_armed() ? "ARMED" : "off");
        for (int i = 0; i < nh; ++i) {
            windower::debug::log("  %s : %s", hits[i].id, hits[i].detail);
            if (hits[i].sev == CHK_BLOCK) DOC("%s : %s", hits[i].id, hits[i].detail);   // a blocker belongs in the chat summary
        }
        if (nh > 0 && !selftest_armed())
            windower::debug::log("  note     : those findings were only seen because you typed doctor. "
                                 "`//aio selftest on` makes the same checks run every 30 s and write a report.");
    }

    windower::debug::log("=== AIO DOCTOR : %d problem(s) ===", n);
    #undef DOC
    return n;
}

// The CONTEXT half of a bug report. Every line answers a question I would otherwise have to ask, and each
// has cost a round trip before now: which build is this, where is it installed, what was on screen. The
// settings are attached by selftest_write_report itself, which copies config.txt in whole.
void Hud::write_bug_report(const CheckFail* hits, int n, bool watched) {
    char hdr[1024]; int L = 0;
    #define H(...) do { if (L < (int)sizeof(hdr) - 1) { const int w2 = _snprintf(hdr + L, sizeof(hdr) - L, __VA_ARGS__); \
                        if (w2 > 0) L += w2; } } while (0)
    H("build      : %s\r\n", aio_version_string());
    H("character  : %s   job %d/%d   zone %d\r\n", state_.me.name[0] ? state_.me.name : "(unknown)",
      (int)state_.me.mjob, (int)state_.me.sjob, (int)state_.zone);
    { int nob = 0; party().other_buffs(nob);
      H("party      : %d member(s), %d ally buff(s) tracked\r\n", party().count, nob); }
    { const char* rk = 0; const char* rom = ffxi_rom_dir_probe(&rk);
      H("rom dir    : %s\r\n", rom ? rom : "<UNRESOLVED -- gear icons show as text>"); }
    H("icon sheet : %s\r\n", buff_atlas_source());
    #undef H
    hdr[sizeof(hdr) - 1] = 0;
    selftest_write_report(hdr, hits, n, watched);
}

void Hud::self_check() {
    windower::debug::log("=== AIO SELFCHECK : texture-load health (1 = handle set ; tN = retry misses so far) ===");
    windower::debug::log("  hud      : buffAtlas=%d(t%u,%s) skin=%s  grim L=%d D=%d C=%d  weapon=%d coffer=%d",
                         // three states, not two : loaded / this theme has no textures by design / its textures
                         // will not load. The last two used to print the same string, so a broken install read as
                         // normal to the one person running the diagnostic.
                         buffAtlas_ ? 1 : 0, buff_atlas_tries(), buff_atlas_source(),   // WHICH sheet is live : a custom one, an icon pack, the bundle, or the vanilla ROM
                         skin_.ready() ? "ready" : (skin_.failed() ? "MISSING (texture theme, files unreadable)" : "(none/proc)"),
                         grimLight_ ? 1 : 0, grimDark_ ? 1 : 0, grimClosed_ ? 1 : 0, weaponIcons_ ? 1 : 0, tpCoffer_ ? 1 : 0);
    const char* rk = 0; const char* rom = ffxi_rom_dir_probe(&rk);   // gear-icon ROM path (EquipViewer id-vs-icon)
    windower::debug::log("  romdir   : %s   (key: %s)", rom ? rom : "<UNRESOLVED : gear icons will be id-text>", rk ? rk : "<none>");
    for (size_t i = 0; i < widgets_.size(); ++i) widgets_[i]->self_check();
    windower::debug::log("=== end selfcheck : a `0` handle that never turns 1, or tries climbing to 12, is a stuck load ===");
}
void Hud::dispose() {
    // FLUSH the debounced re-anchor save. Without this, resizing a box and //unloading within the debounce
    // window would drop the position shift -- which is the standard iteration loop (tweak, unload, deploy),
    // i.e. exactly when it would be noticed and blamed on something else.
    if (savePendMs_) { savePendMs_ = 0; save_ui_config(); }
    for (size_t i = 0; i < widgets_.size(); ++i) widgets_[i]->dispose();
    clear_widgets();
    fonts_.dispose();
    skin_.dispose();
    box_skins_dispose();          // per-box Custom->FFXI skins (else they leak per //unload)
    window_materials_dispose();   // Release the procedural box-theme material textures (else they leak per //unload)
    if (tpCoffer_) { release_texture(tpCoffer_); tpCoffer_ = 0; }   // treasure-pool coffer icon
    if (weaponIcons_) { release_texture(weaponIcons_); weaponIcons_ = 0; }   // Sheol weapon-type icon atlas
    buffAtlas_ = 0; buff_atlas_dispose();   // the SHARED status-icon atlas : the program's ONLY Release of it (every box borrows this handle)
    if (grimLight_) { release_texture(grimLight_); grimLight_ = 0; }   // grimoire books
    if (grimDark_)  { release_texture(grimDark_);  grimDark_ = 0; }
    if (grimClosed_){ release_texture(grimClosed_);grimClosed_= 0; }   // grimoire : closed-book (no Arts) texture
    zonetracker_help_dispose(); treasure_help_dispose();   // the module-owned Help samples (file-static, not members) -- symmetric with the forget block in render()
    corner_mask_dispose();   // the baked corner-mask atlas (the ONLY Release of it)
    config_.dispose();   // Release the ConfigPage's owned Help/preview textures (zone map, logo, atlases) -- else they leak per //unload
}

// switch the window skin theme (0-based). Release the current textures -> render() lazily reloads
// the new theme next frame (keeps all GPU work on the render thread).
void Hud::set_skin(int idx) {
    int n = window_theme_count();
    if (idx < 0) idx = 0; if (idx >= n) idx = n - 1;
    skinIdx_ = idx;
    ui_config().skinTheme = idx;     // keep the config page + //aio menu in sync
    skin_.dispose();
}

static u32 ws_lerp(u32 a, u32 b, float t) {   // per-channel ARGB lerp (for the flashy damage colour cycle)
    if (t < 0.0f) t = 0.0f; if (t > 1.0f) t = 1.0f;
    const int ar = (a >> 16) & 0xFF, ag = (a >> 8) & 0xFF, ab = a & 0xFF, aa = (a >> 24) & 0xFF;
    const int br = (b >> 16) & 0xFF, bg = (b >> 8) & 0xFF, bb = b & 0xFF, ba = (b >> 24) & 0xFF;
    return ((u32)(aa + (int)((ba - aa) * t)) << 24) | ((u32)(ar + (int)((br - ar) * t)) << 16)
         | ((u32)(ag + (int)((bg - ag) * t)) << 8)  |  (u32)(ab + (int)((bb - ab) * t));
}

// Arcade "ULTRA COMBO" popup : when YOU land a weaponskill, flash its name + total damage centre-screen with a
// pop-in overshoot, a heartbeat pulse and a fade-out (party().wsPop_, set on the 0x028 cat-3 finish).
void Hud::draw_ws_popup(const Frame& f, bool preview, float ovCx, float ovCy, float ovUS) {
    const UiConfig& C = ui_config();
    if (!C.wsShow) return;
    const bool editing = C.editLayout && !preview;
    WsPopup sample; const WsPopup* wpp; unsigned el;
    const float LIFE = 2400.0f;
    if (preview || editing) {   // config / edit-layout live SAMPLE : "SAVAGE BLADE / 12345"
        static const char* SN = "SAVAGE BLADE"; int i = 0; for (; SN[i] && i < 39; ++i) sample.name[i] = SN[i]; sample.name[i] = 0;
        sample.dmg = 12345; el = preview ? (GetTickCount() % 2900u) : (GetTickCount() % (unsigned)LIFE);   // both LOOP the full animation (edit loops within LIFE -> always visible ; the fixed grab box makes it easy to grab anyway)
        wpp = &sample;
    } else {
        wpp = &party().ws_popup();
        if (!wpp->startMs || !wpp->name[0]) return;
        el = GetTickCount() - wpp->startMs;
    }
    const WsPopup& wp = *wpp;
    if ((float)el > LIFE) return;
    Font* fo = f.fonts ? f.fonts->get(ui_font_face(C.wsFont), 900) : f.font;   // configured face, heavy weight, own size pool
    if (!fo) fo = f.font;
    if (!fo) return;
    u32 dev = f.dev;

    // EDIT-LAYOUT : drag the popup to place it (wsX/wsY = centre) + wheel over it = resize (wsScale). A grab box
    // is centred on the popup ; edit_box_drag persists a TOP-LEFT fraction, so we convert back to the centre.
    if (editing) {
        static EditBox g_wsEdit;
        const float gU = (screenH_ / 1400.0f) * C.wsScale, gw = 300.0f * gU, gh = 130.0f * gU;
        float px = screenW_ * C.wsX - gw * 0.5f, py = screenH_ * C.wsY - gh * 0.5f;
        float tfx = px / screenW_, tfy = py / screenH_; bool ps = true; int ch = 0, cv = 0;
        const bool wasDrag = g_wsEdit.dragging;
        if (edit_box_drag(g_wsEdit, EDITBOX_WS, f, px, py, gw, gh, ZPERM_HUB, ps, tfx, tfy, ch, cv, ui_config().wsScale))
            edit_box_grid(dev, f, g_wsEdit, px, py, gw, gh, ch != 0, cv != 0);   // highlight the centre axis when snapped
        ui_config().wsX = (px + gw * 0.5f) / screenW_;   // live top-left -> centre
        ui_config().wsY = (py + gh * 0.5f) / screenH_;
        if (wasDrag && !g_wsEdit.dragging) save_ui_config();
    }

    const float e = (float)el;
    float a = 1.0f;                                                        // global fade in/out
    if (e < 70.0f) a = e / 70.0f; else if (e > LIFE - 500.0f) a = (LIFE - e) / 500.0f;
    if (a < 0.0f) a = 0.0f; if (a > 1.0f) a = 1.0f;
    const bool  ov = ovUS > 0.0f;                                                         // config-stage override (centre + scale)
    const float US = ov ? ovUS : (screenH_ / 1400.0f) * C.wsScale;                        // resolution scale x the user Size
    const float beat = 0.5f + 0.5f * sinf(e * 0.010f);                                    // ongoing heartbeat (cheap : NO font re-bake)
    const float cx = ov ? ovCx : screenW_ * C.wsX, cy = ov ? ovCy : screenH_ * C.wsY;     // configurable centre (or stage centre in the preview)
    #define MULA(col, aa) ((u32)(((((col) >> 24) & 0xFFu) * (aa))) << 24 | ((col) & 0x00FFFFFFu))

    // ===== ARCADE WEAPONSKILL POPUP -- layered additive FX, smooth ease-out timing =====
    auto eo3 = [](float t) { if (t < 0.0f) t = 0.0f; if (t > 1.0f) t = 1.0f; const float u = 1.0f - t; return 1.0f - u * u * u; };

    // 1. IMPACT BURST (first ~560ms) : white-hot core + twin delayed shockwave rings + an 8-spoke light burst.
    if (C.wsFx && e < 560.0f) {
        dColorQuadState(dev); dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_ONE);
        const float t = e / 560.0f, et = eo3(t), fd = 1.0f - t, fd2 = fd * fd;
        const float cr = (60.0f + 260.0f * et) * US;
        soft_blob(dev, cx, cy, cr, cr * 0.78f, MULA(0xC8FFF2CCu, a * fd2));                          // core flash
        for (int k = 0; k < 2; ++k) {                                                               // twin shockwave rings
            const float tk = (e - k * 80.0f) / 560.0f; if (tk <= 0.0f || tk >= 1.0f) continue;
            const float ek = eo3(tk), rr = (18.0f + 372.0f * ek) * US, fk = (1.0f - tk) * (1.0f - tk);
            rrect_stroke(dev, cx - rr, cy - rr, 2.0f * rr, 2.0f * rr, rr, MULA(k ? 0x8CFFC060u : 0xB4FFDA88u, a * fk), (3.4f - 2.0f * tk) * US + 1.0f);
        }
        const float s0 = 26.0f * US, s1 = (58.0f + 300.0f * et) * US;                               // 8 tapered light spokes
        for (int k = 0; k < 8; ++k) {
            const float ang = (float)k * 0.7853982f + t * 0.35f, c2 = cosf(ang), s2 = sinf(ang);
            seg_soft(dev, cx + s0 * c2, cy + s0 * s2, cx + s1 * c2, cy + s1 * s2, (2.6f * fd + 0.4f) * US, MULA(0x66FFD070u, a * fd2));
        }
        dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    }

    // 2. AMBIENT BLOOM behind the text : a soft warm glow that breathes (keeps it luminous + alive).
    if (C.wsFx) {
        dColorQuadState(dev); dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_ONE);
        const float g = (150.0f + 24.0f * beat) * US;
        soft_blob(dev, cx, cy + 4.0f * US, g, g * 0.60f, MULA(0x34FFB63Cu, a * (0.5f + 0.5f * beat)));
        dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    }

    // 3. NAME PLATE : a slim gradient bar (transparent ends) + an accent underline that sweeps open from the centre.
    { dColorQuadState(dev);
      const float bw = 600.0f * US, bh = 74.0f * US, bx = cx - bw * 0.5f, by = cy - bh * 0.5f - 8.0f * US;
      const u32 mid = MULA(0x9E070A14u, a), edge = MULA(0x00070A14u, a);
      grad_quad(dev, bx, by, bw * 0.5f, bh, edge, mid, edge, mid);
      grad_quad(dev, bx + bw * 0.5f, by, bw * 0.5f, bh, mid, edge, mid, edge);
      const float aw = bw * 0.46f * eo3(e / 300.0f);
      const u32 ac = MULA(0xCEFFCB4Cu, a), ac0 = MULA(0x00FFCB4Cu, a);
      grad_quad(dev, cx - aw, cy + 15.0f * US, aw, 2.2f * US, ac0, ac, ac0, ac);
      grad_quad(dev, cx,      cy + 15.0f * US, aw, 2.2f * US, ac, ac0, ac, ac0);
    }

    char up[48]; { int i = 0; for (; wp.name[i] && i < 47; ++i) { char c = wp.name[i]; up[i] = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c; } up[i] = 0; }
    fo->begin(dev);
    // WS name : SIZE animated -- pop-in overshoot then a gentle heartbeat pulse (draw_c_scaled : atlas baked ONCE
    // at nbk, only the display size varies -> no per-frame re-bake).
    float nsc; if (e < 140.0f) nsc = 0.45f + 0.75f * (e / 140.0f); else if (e < 260.0f) nsc = 1.20f - 0.20f * ((e - 140.0f) / 120.0f); else nsc = 1.0f + 0.03f * sinf((e - 260.0f) * 0.011f);
    const float nbk = 34.0f * US, ny = cy - 26.0f * US;
    fo->draw_c_scaled(dev, cx, ny, up, nbk, nbk * nsc, MULA(C.wsNameCol, a), MULA(0xFF401000u, a), 2.6f * US);   // configurable name colour + deep-red outline
    // damage : big SLAM-in overshoot (0.3 -> 1.6 -> 1.0) then a continuous pulse -- the arcade bounce ; FLASHY colour cycle A<->B.
    if (wp.dmg > 0) {
        char db[16]; sprintf(db, "%d", wp.dmg);
        float dsc; if (e < 120.0f) dsc = 0.30f + 1.30f * (e / 120.0f); else if (e < 250.0f) dsc = 1.60f - 0.60f * ((e - 120.0f) / 130.0f); else dsc = 1.0f + 0.06f * sinf((e - 250.0f) * 0.012f);
        const float flash = 0.5f + 0.5f * sinf(e * 0.022f);                       // fast colour flicker
        const u32 dmgCol = ws_lerp(C.wsDmgCol1, C.wsDmgCol2, flash);              // configurable damage colours A <-> B
        const float dbk = 58.0f * US, dy = cy + 40.0f * US;
        fo->draw_c_scaled(dev, cx, dy, db, dbk, dbk * dsc, MULA(dmgCol, a), MULA(0xFFC81400u, a), 3.6f * US);   // + deep-red outline
    }
    #undef MULA
}

// Shared edit-mode drag for a module box : drag + snap-grid, write the new fractional origin back to
// (cfgX,cfgY), persist on drop. centerX = the stored X is the box CENTRE (else its left edge). px/py updated.
void box_edit(const Frame& f, EditBox& eb, int editId, float& px, float& py, float boxW, float boxH,
                     float& scale, float& cfgX, float& cfgY, int anchorX) {
    float tfx = px / f.screenW, tfy = py / f.screenH; bool ps = true; int ch = 0, cv = 0; const bool wasDrag = eb.dragging;
    const float scale0 = scale;   // edit_box_drag WRITES scale on a wheel-resize -> now it reaches the config field
    if (edit_box_drag(eb, editId, f, px, py, boxW, boxH, ZPERM_HUB, ps, tfx, tfy, ch, cv, scale))
        edit_box_grid(f.dev, f, eb, px, py, boxW, boxH, ch != 0, cv != 0);
    cfgX = (px + (anchorX == 1 ? boxW * 0.5f : anchorX == 2 ? boxW : 0.0f)) / f.screenW;   // 0 left / 1 centre / 2 right
    cfgY = py / f.screenH;
    // persist on a drag-drop OR a wheel-resize (the latter has no drag, so the old drop-only save missed it ->
    // box_edit boxes appeared not to resize at all : the scale was written to a by-VALUE copy and discarded).
    if ((wasDrag && !eb.dragging) || scale != scale0) save_ui_config();
}

// draw an atlas sub-cell [u0..u1]x[v0..v1] at (x,y,w,h) -- Sheol weapon strip (v 0..1) or the 2D buff atlas.
void draw_icon_cell(u32 dev, u32 tex, float x, float y, float w, float h, float u0, float u1, float v0, float v1) {
    dSetVS(dev, FVF_XYZRHW_DIFFUSE_TEX1);
    dSetRS(dev, D3DRS_ALPHABLENDENABLE, 1);
    dSetRS(dev, D3DRS_SRCBLEND, D3DBLEND_SRCALPHA); dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    dSetTex(dev, 0, tex);
    dSetTSS(dev, 0, D3DTSS_COLOROP, D3DTOP_MODULATE); dSetTSS(dev, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE); dSetTSS(dev, 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    dSetTSS(dev, 0, D3DTSS_ALPHAOP, D3DTOP_MODULATE); dSetTSS(dev, 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE); dSetTSS(dev, 0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    dSetTSS(dev, 0, D3DTSS_MINFILTER, D3DTEXF_LINEAR); dSetTSS(dev, 0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR); dSetTSS(dev, 0, D3DTSS_MIPFILTER, D3DTEXF_NONE);
    tquad(dev, x, y, w, h, u0, u1, v0, v1, 0xFFFFFFFFu, 0xFFFFFFFFu);
    dSetTex(dev, 0, 0);
}


} // namespace aio
