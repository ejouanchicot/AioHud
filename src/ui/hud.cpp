// hud.cpp -- see hud.h.
#include "hud.h"
#include "ui/factory.h"
#include "model/layout.h"
#include "model/game_mem.h"
#include "model/gamestate.h"
#include "model/party_state.h"
#include "model/ui_config.h"
#include "windower_debug.h"
#include <windows.h>
#include <algorithm>

namespace aio {

// Poll the OS cursor + left button and map into the HUD coord space. The plugin runs inside the
// game process, so Win32 gives us the cursor directly (the IPlugin mouse slot doesn't carry it).
// Cursor is client-relative to the focused (game) window, scaled by client size -> coord space.
static void poll_mouse(MouseState& m, float coordW, float coordH) {
    POINT p;
    if (!GetCursorPos(&p)) { m.clicked = false; return; }
    HWND hw = GetForegroundWindow();
    if (hw) {
        POINT cp = p; ScreenToClient(hw, &cp);
        RECT rc;
        if (GetClientRect(hw, &rc)) {
            float ww = (float)(rc.right - rc.left), wh = (float)(rc.bottom - rc.top);
            if (ww > 1.0f && wh > 1.0f) { m.x = (float)cp.x * coordW / ww; m.y = (float)cp.y * coordH / wh; }
        }
    }
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
        if (lw.type == "PlayerHub") bars_ = (LiquidBars*)w;
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
        for (size_t i = 0; i < widgets_.size(); ++i) widgets_[i]->on_device_lost();
    }
    fonts_.get(0, 0);          // register the default slot so ensure_all builds it this frame
    fonts_.ensure_all(dev);
    if (ui_config().skinTheme != skinIdx_) set_skin(ui_config().skinTheme);   // config page changed the theme
    {   // re-measure + re-anchor when any box scale changed (boxes grow/shrink IN PLACE)
        bool changed = false;
        for (int b = 0; b < 3; ++b) if (ui_config().box[b].scale != lastScale_[b]) { lastScale_[b] = ui_config().box[b].scale; changed = true; }
        if (changed) place_widgets();
    }
    if (!skin_.ready()) skin_.load(dev, window_theme_name(skinIdx_));   // FFXI window skin (lazy ; rebuilds after a device loss / theme change)

    // ONE poll of live game memory for the WHOLE frame -> the shared snapshot every widget
    // draws from (player vitals/jobs, target, leaders, action menu). Read each pointer-chain
    // once here, never in a widget's draw(). See gamestate.h.
    poll_game_state(state_);

    // the party ROSTER (party + alliance, member-array slots 0..17) is the one big table ->
    // also refreshed once per frame, into the party() singleton. Mirrors XivParty's per-tick
    // get_party(); a freshly-summoned trust / new alliance member appears at once.
    party().load_from_memory();

    for (size_t i = 0; i < widgets_.size(); ++i) widgets_[i]->ensure(dev);

    Frame f;
    f.dev   = dev;
    f.fonts = &fonts_;
    f.font  = fonts_.get(0, 0);   // the default atlas (global face/weight) for non-party widgets
    f.t     = (float)(GetTickCount() % 1000000) / 1000.0f;
    f.game  = &state_;            // the per-frame snapshot widgets read from
    f.skin  = &skin_;             // the shared FFXI window skin (9-slice chrome)
    poll_mouse(mouse_, screenW_, screenH_);   // cursor + click for this frame
    f.mouse = &mouse_;
    f.screenW = screenW_; f.screenH = screenH_;

    // ONE state block around ALL our drawing: save the game's render state, set ours,
    // restore afterwards (else we corrupt the game's own rendering). Retained widgets
    // (text/prims) are auto-rendered by Windower outside this block -- harmless here.
    u32 tok = dCreateSB(dev, D3DSBT_ALL);
    __try {
        for (size_t i = 0; i < widgets_.size(); ++i) widgets_[i]->draw(f);
        config_.draw(f, screenW_, screenH_);   // full-screen config overlay, on top of everything
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

} // namespace aio
