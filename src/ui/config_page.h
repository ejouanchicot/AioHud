// config_page.h -- the full-screen, web-style configuration overlay (//aio config).
//
// Drawn on TOP of everything by the HUD when open : a dimmed screen + a centred page panel
// (FFXI window skin) with TABS (Configuration / Profil / Help) and, in Configuration, a LEFT
// sidebar of config sections (Party/Alliance ; more later : treasure pool, hate list, ...).
// Interaction (mouse clicks on tabs / sections / controls) is wired separately -- this is the
// layout + state. Tabs/sections can also be driven from //aio config <args>.
#pragma once
#include "ui/widget.h"   // Frame

namespace aio {

class ConfigPage {
public:
    void toggle()         { open_ = !open_; if (open_) { anim_ = 0.0f; profDirty_ = true; } }
    void set_open(bool o) { if (o && !open_) { anim_ = 0.0f; profDirty_ = true; } open_ = o; }
    bool is_open() const  { return open_; }
    void set_tab(int t);
    void set_section(int s);

    // draw the overlay for the current frame (screen size in the HUD coord space). No-op if closed.
    void draw(const Frame& f, float sw, float sh);

    // LIVE PREVIEW anchor : when open on the Configuration tab, the HUD draws the real party +
    // alliance demo boxes (bottom-right) into the page. Returns false otherwise. (rightX, bottomY)
    // is where the party box's bottom-right corner should sit, in the HUD coord space.
    bool preview_anchor(float& rightX, float& bottomY) const {
        rightX = pvRightX_; bottomY = pvBottomY_; return pvOn_;
    }

    // ---- keyboard (fed by the plugin's slot-14 hook ; only while a text field is focused) ----
    // A full single-line text field : insertion CURSOR (nameCur_) with left/right/home/end, insert +
    // backspace + forward-delete AT the cursor -- the same behaviour as any OS text input.
    bool wants_keys() const { return open_ && nameFocus_; }   // -> the hook consumes keys (name field is on the Config tab)
    void feed_char(char c)  {
        if (nameLen_ >= (int)sizeof(nameBuf_) - 1) return;
        if ((unsigned char)c < 32) return;   // control chars only -- / \ : etc. are allowed (the file name %-encodes them)
        for (int i = nameLen_; i > nameCur_; --i) nameBuf_[i] = nameBuf_[i - 1];   // make room at the cursor
        nameBuf_[nameCur_++] = c; nameBuf_[++nameLen_] = 0;
    }
    void feed_backspace()   {                                  // delete the char BEFORE the cursor
        if (nameCur_ <= 0) return;
        for (int i = nameCur_ - 1; i < nameLen_; ++i) nameBuf_[i] = nameBuf_[i + 1];
        --nameLen_; --nameCur_;
    }
    void feed_delete()      {                                  // delete the char AT the cursor (forward)
        if (nameCur_ >= nameLen_) return;
        for (int i = nameCur_; i < nameLen_; ++i) nameBuf_[i] = nameBuf_[i + 1];
        --nameLen_;
    }
    void cursor_left()      { if (nameCur_ > 0) --nameCur_; }
    void cursor_right()     { if (nameCur_ < nameLen_) ++nameCur_; }
    void cursor_home()      { nameCur_ = 0; }
    void cursor_end()       { nameCur_ = nameLen_; }
    void feed_enter()       { kbCommit_ = true; }   // consumed in draw() -> save the typed profile
    void blur()             { nameFocus_ = false; }

private:
    bool  open_    = false;
    int   tab_     = 0;       // 0 = Configuration, 1 = Profil, 2 = Help
    int   section_ = 0;       // Configuration sidebar : 0 = Party/Alliance (more later)
    // animation state (driven by the frame clock)
    float anim_     = 0.0f;   // open progress 0..1 (eased) -> fade in
    float lastT_    = -1.0f;  // previous frame time, for dt
    float tabSlide_ = -1.0f;  // interpolated x of the sliding active-tab indicator
    float hov_[3]   = { 0.0f, 0.0f, 0.0f };   // eased hover amount per tab
    // ---- Profile tab state ----
    char  nameBuf_[32] = { 0 };   // profile name being typed
    int   nameLen_     = 0;
    int   nameCur_     = 0;       // insertion cursor index within nameBuf_ (0 .. nameLen_)
    bool  nameFocus_   = false;   // the name field has keyboard focus (the slot-14 hook feeds it)
    bool  kbCommit_    = false;   // Enter was pressed -> save on the next draw
    bool  profDirty_   = true;    // rescan the profile folder on the next Profile-tab draw
    char  activeProf_[32] = { 0 };// last saved/loaded profile -> highlighted in the list
    int   helpSel_     = 0;       // Help tab : selected module in the left menu
    float helpScroll_  = 0.0f;    // Help tab vertical scroll (mouse wheel)
    // live-preview anchor published each frame for the HUD (Configuration tab only)
    bool  pvOn_        = false;
    float pvRightX_    = 0.0f, pvBottomY_ = 0.0f;
};

} // namespace aio
