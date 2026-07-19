// hud.cpp -- see hud.h.
#include "hud.h"
#include "ui/hud_internal.h"   // box_edit / draw_icon_cell : shared with the per-module hud_*.cpp split TUs
#include "ui/factory.h"
#include "ui/player.h"
#include "ui/party.h"
#include "ui/target.h"
#include "ui/minimap.h"
#include "model/layout.h"
#include "model/game_mem.h"
#include "model/gamestate.h"
#include "model/party_state.h"
#include "model/zones.h"   // zone_name -> Zone Tracker (Dynamis/Abyssea) detection
#include "model/resistances.h"   // Sheol/Odyssey : target resistance model (compute_resistances)
#include "model/ui_config.h"
#include "windower_debug.h"
#include "ui/edit_box.h"  // edit-mode drag for the WS popup (place it in //aio edit like the other boxes)
#include "gfx/draw.h"     // rrect_glow / disc_glow for the WS popup burst
#include "model/skillchain.h"         // Skillchains : Resonating fields -> names / colours / elements
#include "model/weapon_skills_gen.h"  // ws_info (closing WS name)
#include "model/spells_gen.h"         // spell_info (closing spell name)
#include "model/abilities_gen.h"      // abil_info (job-ability / avatar / pet ready-move names)
#include "model/buffs_gen.h"          // buff_status_name : status id -> name (Timers Duration column, name mode)
#include "ui/box_style.h"             // draw_themed_box : shared themed chrome for every module box
#include "ui/text_style.h"            // te_font/te_sz/te_ow/te_col : the ONE TextStyle-resolve impl (per-module helpers delegate here)
#include "model/mobskills_gen.h"      // mobskill_info (BST charmed/jug pet TP-move names)
#include "model/itemnames_gen.h"      // item_name (treasure-pool item labels)
#include "model/paths.h"              // plugin_path (coffer icon asset)
#include "gfx/texture.h"              // load_raw_texture / release_texture (coffer icon)
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
void debuffs_help_forget();
void timers_help_forget();
void treasure_help_forget();

// Poll the OS cursor + left button and map into the HUD coord space. The plugin runs inside the
// game process, so Win32 gives us the cursor directly (the IPlugin mouse slot doesn't carry it).
// Cursor is client-relative to the focused (game) window, scaled by client size -> coord space.
static void poll_mouse(MouseState& m, float coordW, float coordH, HWND gameHw) {
    POINT p;
    if (!GetCursorPos(&p)) { m.clicked = false; m.down = false; return; }
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
    if (!m.focused && !m.overGame) { m.clicked = false; m.down = false; return; }
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
    if (!m.focused) { m.clicked = false; m.down = false; return; }
    bool down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    m.clicked = down && !m.down;   // press edge = one-shot click
    m.down    = down;
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

void Hud::apply_layout(const char* path) {
    Layout lay;
    if (!load_layout(path, lay)) {
        windower::debug::log("apply_layout: LOAD FAILED <%s> (keeping default)", path);
        return;                                 // missing/invalid -> keep the current widgets
    }
    layout_ = lay;                              // keep the descriptor so we can re-place on a resolution change
    have_layout_ = true;
    layout_path_ = path;                        // remember for hot-reload (//aio layout)
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
    windower::debug::log("place_widgets: %d widgets (screen %dx%d, scale %d%%)",
                         (int)layout_.widgets.size(), (int)screenW_, (int)screenH_, (int)(ui_scale_ * 100));
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
    if ((float)bw == screenW_ && (float)bh == screenH_) return;
    windower::debug::log("screen resolution %dx%d -> %ux%u (re-placing widgets)",
                         (int)screenW_, (int)screenH_, bw, bh);
    screenW_ = (float)bw;
    screenH_ = (float)bh;
    place_widgets();
}

void Hud::render(u32 dev) {
    if (!valid_ptr(dev)) return;

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
        tpCoffer_ = 0; tpCofferTried_ = false;   // forget the treasure-pool coffer icon (belongs to the old device)
        weaponIcons_ = 0; weaponIconsTried_ = false;   // forget the Sheol weapon-type icon atlas
        buffAtlas_ = 0; buffAtlasTries_ = 0; buffAtlasNextMs_ = 0;   // forget the buff-timers status-icon atlas (+ re-arm the retry budget)
        grimLight_ = 0; grimDark_ = 0; grimClosed_ = 0; grimTried_ = false;   // forget the grimoire book textures (belong to the old device)
        // The Help SAMPLES keep their own lazy copies at file scope in their hud_*.cpp -- unreachable from here
        // otherwise, so they used to survive a device recreate and hand a dead device's texture to SetTexture.
        zonetracker_help_forget(); debuffs_help_forget(); timers_help_forget(); treasure_help_forget();
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
        if (saved) save_ui_config();
        if (needPlace) place_widgets();
    }
    if (!window_theme_is_proc(skinIdx_) && !skin_.ready()) skin_.load(dev, window_theme_name(skinIdx_));   // FFXI window skin (lazy) ; procedural colour themes have no texture to load

    // ONE poll of live game memory for the WHOLE frame -> the shared snapshot every widget
    // draws from (player vitals/jobs, target, leaders, action menu). Read each pointer-chain
    // once here, never in a widget's draw(). See gamestate.h.
    poll_game_state(state_);
    profile_sync_poll();   // another client on this Windower saved the profile we are on -> re-apply it (throttled 1/s)

    // GATE the whole HUD on "logged in / in the world". read_player STILL succeeds while zoning, so it alone can't
    // hide during a zone -- but the client sends 0x00B (zone-out) then 0x00A (zone-in), which set party().zoning_
    // (same source Windower uses). Hide when zoning, or before the first successful poll (char-select / POL). A few
    // frames of grace on the inGame side absorb a 1-frame hiccup so the HUD never blinks mid-fight ; a zone hides
    // immediately (no grace) via the explicit flag.
    const bool zoning = party().is_zoning();
    const bool ready = state_.inGame && !zoning;
    if (ready) { everInGame_ = true; notReadyFrames_ = 0; }
    else ++notReadyFrames_;
    const bool worldReady = ready || (everInGame_ && !zoning && notReadyFrames_ <= 5);

    // the party ROSTER (party + alliance, member-array slots 0..17) is the one big table ->
    // also refreshed once per frame, into the party() singleton. Mirrors XivParty's per-tick
    // get_party(); a freshly-summoned trust / new alliance member appears at once.
    party().load_from_memory();
    party().set_target_ctx(state_.target.id, state_.me.id);   // context for the debuff tracker (on_action attributes YOUR debuffs to the current target)
    if (state_.target.valid && state_.target.spawnType == 0x10) party().note_mob_hp(state_.target.id, state_.target.hpp);   // debuff tracker : drop a mob's debuffs on death / when its server id gets recycled (Apex farming)
    if (state_.hasSubTarget && state_.subTarget.valid && state_.subTarget.spawnType == 0x10) party().note_mob_hp(state_.subTarget.id, state_.subTarget.hpp);
    party().refresh_hate();   // hate list : resolve tracked aggro mobs -> display rows (needs the fresh self pos + <t>)
    party().prune_skillchains();   // skillchains : drop resonance windows whose mob has died (so the box doesn't linger)
    party().zt_set_zone((int)state_.zone, zone_name((int)state_.zone));   // zone tracker : detect Dynamis/Abyssea + reset on change
    party().ep_refresh(ui_config().epTrack);   // EmpyPop : resolve the tracked NM's pop chain (self-throttled 2 Hz ; rebuilds instantly on a key change)
    profile_autoload_tick();   // auto-switch profile when the character's Name/Main/Sub changes (login / job change)

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
    if (!wsFontWarmed_ && s_frame > 40) {   // PRE-BAKE the WS-popup font atlases off-screen ONCE, but only after the
                                            // HUD has settled -> the big 58/34px atlases never pile onto the load frames
        Font* pf = fonts_.get(ui_font_face(ui_config().wsFont), 900);
        if (pf) { const float US = (screenH_ / 1400.0f) * ui_config().wsScale; pf->begin(dev);
                  pf->draw(dev, -9999.0f, -9999.0f, "0123456789", 58.0f * US, 0);
                  pf->draw(dev, -9999.0f, -9999.0f, "ABCDEFGHIJKLMNOPQRSTUVWXYZ", 34.0f * US, 0); }
        wsFontWarmed_ = true;
    }

    // ONE state block around ALL our drawing: save the game's render state, set ours,
    // restore afterwards (else we corrupt the game's own rendering). Retained widgets
    // (text/prims) are auto-rendered by Windower outside this block -- harmless here.
    u32 tok = dCreateSB(dev, D3DSBT_ALL);
    __try {
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
        const bool hideForConfig = config_.is_open() && !ui_config().editLayout;
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
        draw_config_preview(f);                // real party+alliance demo boxes inside the config preview stage
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        static bool logged = false;
        if (!logged) { logged = true; windower::debug::log("HUD draw threw (SEH)"); }
    }
    if (tok) { dApplySB(dev, tok); dDelSB(dev, tok); }
}


void Hud::dispose() {
    for (size_t i = 0; i < widgets_.size(); ++i) widgets_[i]->dispose();
    clear_widgets();
    fonts_.dispose();
    skin_.dispose();
    window_materials_dispose();   // Release the procedural box-theme material textures (else they leak per //unload)
    if (tpCoffer_) { release_texture(tpCoffer_); tpCoffer_ = 0; }   // treasure-pool coffer icon
    if (weaponIcons_) { release_texture(weaponIcons_); weaponIcons_ = 0; }   // Sheol weapon-type icon atlas
    if (buffAtlas_) { release_texture(buffAtlas_); buffAtlas_ = 0; }   // buff-timers status-icon atlas
    if (grimLight_) { release_texture(grimLight_); grimLight_ = 0; }   // grimoire books
    if (grimDark_)  { release_texture(grimDark_);  grimDark_ = 0; }
    if (grimClosed_){ release_texture(grimClosed_);grimClosed_= 0; }   // grimoire : closed-book (no Arts) texture
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
