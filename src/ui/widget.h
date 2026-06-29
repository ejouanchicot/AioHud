// widget.h -- the base contract for one drawable element of the HUD.
//
// The HUD opens ONE render frame per game frame (save the device's render state,
// set a baseline, draw, restore) and asks each VISIBLE widget to draw into the shared
// device. A widget owns its own GPU resources and its own pipeline setup inside
// draw() -- it should leave nothing that corrupts the next widget.
//
// Placement comes from the layout descriptor (design/exports/layout.json): the HUD
// resolves each widget's anchor + % into a pixel origin and calls set_place(). The
// widget draws relative to px_/py_. Add a new HUD element by subclassing this,
// returning its layout `type` from type_name(), and registering it in the factory.
#pragma once
#include "gfx/d3d.h"   // brings windower.h (u32, valid_ptr)

namespace aio { namespace json { struct Value; } }   // fwd (configure receives raw config JSON)
namespace aio { class Font; class FontManager; struct GameState; struct WindowSkin; }   // fwd

namespace aio {

// cursor state for the frame, in the HUD coord space (polled via Win32, not the game). `clicked`
// is the press EDGE (down this frame, up last frame) -> a one-shot left-click.
struct MouseState { float x = 0.0f, y = 0.0f; bool down = false; bool clicked = false; };

// per-frame context handed to every widget. Everything is drawn in DIRECT D3D8 inside
// the HUD's render block: graphics via gfx/draw.h, text via the shared `font` atlas
// (gfx/font.h) -> text composites on top of the widget's own graphics.
struct Frame {
    u32   dev  = 0;          // D3D8 device (host vtbl[2])
    Font* font = nullptr;    // the DEFAULT glyph atlas (global face) -- most widgets use this
    FontManager* fonts = nullptr;   // atlas cache (per-text face/weight) -- the party box uses this
    float t    = 0.0f;       // seconds (wrapping) for animation
    const GameState* game = nullptr;   // per-frame snapshot of live game data (poll_game_state) -> read, never poll memory in draw()
    const WindowSkin* skin = nullptr;  // FFXI 9-slice window skin (shared) -> draw_window() for native box chrome
    const MouseState* mouse = nullptr; // cursor + left-click for this frame (Win32-polled)
    float screenW = 0.0f, screenH = 0.0f;   // real screen size (for fraction<->px in edit mode)
};

class Widget {
public:
    virtual ~Widget() {}

    // the layout `type` this widget implements (factory key, e.g. "PlayerHub").
    virtual const char* type_name() const = 0;

    // report the widget's content size in px (for anchoring right/bottom boxes and
    // auto width). Default: unknown (-1) -> the HUD uses the descriptor's fixed size.
    virtual void measure(float& w, float& h) const { w = -1.0f; h = -1.0f; }

    // apply the widget's config object from the descriptor (it reads the keys it knows).
    virtual void configure(const json::Value&) {}

    // (re)create any GPU resources for `dev`. Called every frame before draw();
    // implementations should no-op once their resources exist.
    virtual void ensure(u32 dev) { (void)dev; }

    // draw/update this widget for the current frame. Honour visible_ internally
    // (retained widgets must HIDE their objects when not visible).
    virtual void draw(const Frame& f) = 0;

    // the D3D device was recreated (zoning): forget GPU handles WITHOUT releasing
    // them (the old device may be dead). ensure() will rebuild them next frame.
    virtual void on_device_lost() {}

    // release all GPU resources at //unload (the device is still alive here).
    virtual void dispose() {}

    // ---- placement (set by the HUD from the layout descriptor) ----
    void set_place(float x, float y, int z, bool visible, bool bare) {
        px_ = x; py_ = y; z_ = z; visible_ = visible; bare_ = bare;
    }
    void set_scale(float s) { scale_ = s; }   // global UI scale (real screen / authored viewport)
    int  z()       const { return z_; }
    bool visible() const { return visible_; }

protected:
    float px_ = 0.0f, py_ = 0.0f;   // pixel origin (top-left) on the real screen
    float scale_ = 1.0f;            // multiply px geometry + text size by this
    int   z_ = 0;                   // draw order
    bool  visible_ = true;          // manual show/hide (job gating handled by the HUD)
    bool  bare_ = false;            // no chrome
};

} // namespace aio
