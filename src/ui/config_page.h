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

    // the HUD hands us the party's selection-hand texture each frame so the Help can show the real cursor.
    void set_help_cursor_tex(u32 t) { helpCursorTex_ = t; }

    // LIVE PREVIEW anchor : when open on the Configuration tab, the HUD draws the real party +
    // alliance demo boxes (bottom-right) into the page. Returns false otherwise. (rightX, bottomY)
    // is where the party box's bottom-right corner should sit, in the HUD coord space.
    bool preview_anchor(float& rightX, float& bottomY) const {
        rightX = pvRightX_; bottomY = pvBottomY_; return pvOn_;
    }

    // EDIT LAYOUT : the "Rules" toggle. While ON the reference lines are drawn and the HUD hides its boxes
    // (the HUD reads this each frame) so only the rules + the edit toolbar show. OFF -> normal box editing.
    bool edit_lines_active() const { return editShowLines_; }

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
    int   cfgTarget_ = 0;     // which box the Party/Alliance settings edit : 0 = party, 1 = alliance 1, 2 = alliance 2
    int   cfgTextElem_ = 0;   // which text element the Typography controls edit (Name/HP/MP/TP/Cast/Badge/Distance/Interface)
    bool  catOpen_[3] = { true, true, false };    // collapsible Configuration categories : Global / Per box / Advanced (Advanced holds Typography + Layout/Zones)
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
    bool  profSynced_  = false;   // one-time sync of activeProf_ from the persisted active profile (startup auto-load)
    int   helpSel_     = 0;       // Help tab : selected module in the left menu
    float helpScroll_  = 0.0f;    // Help tab vertical scroll (mouse wheel)
    float cfgScroll_   = 0.0f;    // Configuration tab controls vertical scroll (mouse wheel)
    float cfgMaxScroll_= 0.0f;    // its extent, remembered for next frame's wheel clamp
    float helpMaxScroll_ = 0.0f;  // last frame's scroll limit -> clamp the wheel BEFORE drawing (no overscroll bounce)
    // live-preview anchor published each frame for the HUD (Configuration tab only)
    bool  pvOn_        = false;
    float pvRightX_    = 0.0f, pvBottomY_ = 0.0f;
    // edit-layout : "Rules" toggle + the destructive-action confirmation modal
    bool  editShowLines_ = false;   // draw reference lines AND hide the HUD boxes (only rules + toolbar show)
    int   editConfirm_   = 0;       // 0 = none, 1 = Clear-lines pending, 2 = Default pending -> confirm dialog
    // user-drawn ZONES : selection + inline rename (reuses the nameBuf_ text field) + rubber-band draw
    int   groupSel_      = -1;      // selected zone (-1 = none)
    int   editZoneName_  = -1;      // zone being renamed via the keyboard (-1 = none)
    float guideScroll_   = 0.0f;    // zone-list scroll in the panel  (panel position persists in ui_config.zonePanelX/Y)
    float zoneDrawX_     = 0.0f;    // rubber-band : start corner (screen px) while zoneDrawing_ is set
    float zoneDrawY_     = 0.0f;
    bool  zoneDrawing_   = false;
    u32   helpCursorTex_ = 0;       // party selection-hand texture, handed in by the HUD for the Help live cursor
};

} // namespace aio
