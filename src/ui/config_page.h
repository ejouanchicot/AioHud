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

    // ---- keyboard (fed by the plugin's slot-14 hook ; only while a text field is focused) ----
    bool wants_keys() const { return open_ && tab_ == 1 && nameFocus_; }   // -> the hook consumes keys
    void feed_char(char c)  {
        if (nameLen_ >= (int)sizeof(nameBuf_) - 1) return;
        if ((unsigned char)c < 32) return;   // control chars only -- / \ : etc. are allowed (the file name %-encodes them)
        nameBuf_[nameLen_++] = c; nameBuf_[nameLen_] = 0;
    }
    void feed_backspace()   { if (nameLen_ > 0) nameBuf_[--nameLen_] = 0; }
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
    bool  nameFocus_   = false;   // the name field has keyboard focus (the slot-14 hook feeds it)
    bool  kbCommit_    = false;   // Enter was pressed -> save on the next draw
    bool  profDirty_   = true;    // rescan the profile folder on the next Profile-tab draw
    char  activeProf_[32] = { 0 };// last saved/loaded profile -> highlighted in the list
};

} // namespace aio
