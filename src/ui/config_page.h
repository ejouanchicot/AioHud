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
    void on_device_lost() { logoTex_ = 0; logoTried_ = false;
                            tgtBuffTex_ = 0; tgtThTex_ = 0; tgtTexTried_ = false;
                            mmMkPlayer_ = 0; mmMkMob_ = 0; mmElem_ = 0; mmMoonTex_ = 0; mmMoonKey_ = -1; mmMapTex_ = 0; mmMapFileId_ = 0; mmTried_ = false; }   // FORGET our GPU handles (don't Release -- device may be dead)
    void dispose();   // teardown (//unload) : RELEASE our owned textures (device is still alive) so they don't leak per reload

    // LIVE PREVIEW anchor : when open on the Configuration tab, the HUD draws the real party +
    // alliance demo boxes (bottom-right) into the page. Returns false otherwise. (rightX, bottomY)
    // is where the party box's bottom-right corner should sit, in the HUD coord space.
    bool preview_anchor(float& rightX, float& bottomY) const {
        rightX = pvRightX_; bottomY = pvBottomY_; return pvOn_;
    }
    // the live-preview stage CENTRE (for a small, self-centred widget like the Target box).
    bool preview_center(float& cx, float& cy) const { cx = pvCX_; cy = pvCY_; return pvOn_; }
    // the live-preview stage RECT (px) -> the Target preview maps its on-screen placement into this as a mini-map.
    bool preview_rect(float& sx, float& sy, float& sw, float& sh) const { sx = pvSX_; sy = pvSY_; sw = pvSW_; sh = pvSH_; return pvOn_; }
    // Help tab : if the Player module sample slot is visible, its centre + available width (the Hud renders the REAL
    // demo Player box there, since it owns the widget). Returns false otherwise.
    bool help_player_slot(float& cx, float& cy, float& availW, float& availH) const { cx = helpPlayerCx_; cy = helpPlayerCy_; availW = helpPlayerW_; availH = helpPlayerH_; return helpPlayer_; }
    // which module page is open : 0 = Party / Alliance, 1 = Target -> the HUD picks the matching preview.
    int  section() const { return section_; }

    // EDIT LAYOUT : the "Rules" toggle. While ON the reference lines are drawn and the HUD hides its boxes
    // (the HUD reads this each frame) so only the rules + the edit toolbar show. OFF -> normal box editing.
    bool edit_lines_active() const { return editShowLines_; }

    // ---- keyboard (fed by the plugin's slot-14 hook ; only while a text field is focused) ----
    // A full single-line text field : insertion CURSOR (nameCur_) with left/right/home/end, insert +
    // backspace + forward-delete AT the cursor -- the same behaviour as any OS text input.
    bool wants_keys() const { return open_ && nameFocus_; }   // -> the hook consumes keys (name field is on the Config tab)
    // cp = a Unicode codepoint (32..255). STORED AS UTF-8 to match the font renderer (font.cpp utf8_next) --
    // storing the raw Latin-1 byte (e.g. 0xE9 for 'é') showed a '?' AND could eat the next char. Latin-1 128..255
    // -> a 2-byte UTF-8 sequence. Cursor / backspace / delete below operate on whole UTF-8 chars (never split one).
    void feed_char(unsigned cp)  {
        if (cp < 32) return;   // control chars only -- / \ : etc. are allowed (the file name %-encodes them)
        char u[2]; int n;
        if (cp < 128) { u[0] = (char)cp; n = 1; }
        else          { u[0] = (char)(0xC0 | (cp >> 6)); u[1] = (char)(0x80 | (cp & 0x3F)); n = 2; }
        if (nameLen_ + n > (int)sizeof(nameBuf_) - 1) return;
        for (int i = nameLen_; i >= nameCur_; --i) nameBuf_[i + n] = nameBuf_[i];   // shift right by n (incl. the null)
        for (int k = 0; k < n; ++k) nameBuf_[nameCur_ + k] = u[k];
        nameCur_ += n; nameLen_ += n;
    }
    void feed_backspace()   {                                  // delete the whole UTF-8 char BEFORE the cursor
        if (nameCur_ <= 0) return;
        int s = nameCur_ - 1;
        while (s > 0 && ((unsigned char)nameBuf_[s] & 0xC0) == 0x80) --s;   // back up over continuation bytes to the lead
        const int n = nameCur_ - s;
        for (int i = s; i <= nameLen_ - n; ++i) nameBuf_[i] = nameBuf_[i + n];
        nameLen_ -= n; nameCur_ = s;
    }
    void feed_delete()      {                                  // delete the whole UTF-8 char AT the cursor (forward)
        if (nameCur_ >= nameLen_) return;
        int e = nameCur_ + 1;
        while (e < nameLen_ && ((unsigned char)nameBuf_[e] & 0xC0) == 0x80) ++e;   // advance past continuation bytes
        const int n = e - nameCur_;
        for (int i = nameCur_; i <= nameLen_ - n; ++i) nameBuf_[i] = nameBuf_[i + n];
        nameLen_ -= n;
    }
    void cursor_left()      { if (nameCur_ > 0) { --nameCur_; while (nameCur_ > 0 && ((unsigned char)nameBuf_[nameCur_] & 0xC0) == 0x80) --nameCur_; } }
    void cursor_right()     { if (nameCur_ < nameLen_) { ++nameCur_; while (nameCur_ < nameLen_ && ((unsigned char)nameBuf_[nameCur_] & 0xC0) == 0x80) ++nameCur_; } }
    void cursor_home()      { nameCur_ = 0; }
    void cursor_end()       { nameCur_ = nameLen_; }
    void feed_enter()       { kbCommit_ = true; }   // consumed in draw() -> save the typed profile
    void blur()             { nameFocus_ = false; }

private:
    // Per-module config panels, each defined in its OWN .cpp (party_config.cpp / target_config.cpp) :
    // draw() hands over its running layout cursor (ry/ri) + column geometry so the rows lay out
    // identically, and the module owns its ease() uid namespace. Same signature for every module.
    void draw_party_config (u32 dev, Font* fo, const MouseState* mo, bool click,
                            float& ry, int& ri, float e,
                            float bandX, float bandW, float coX, float ctrlW,
                            float hdrX, float hdrW);
    void draw_target_config(u32 dev, Font* fo, const MouseState* mo, bool click,
                            float& ry, int& ri, float e,
                            float bandX, float bandW, float coX, float ctrlW,
                            float hdrX, float hdrW);
    void draw_player_config(u32 dev, Font* fo, const MouseState* mo, bool click,
                            float& ry, int& ri, float e,
                            float bandX, float bandW, float coX, float ctrlW,
                            float hdrX, float hdrW);
    void draw_minimap_config(u32 dev, Font* fo, const MouseState* mo, bool click,
                            float& ry, int& ri, float e,
                            float bandX, float bandW, float coX, float ctrlW,
                            float hdrX, float hdrW);
    void draw_ws_config(u32 dev, Font* fo, const MouseState* mo, bool click,
                            float& ry, int& ri, float e,
                            float bandX, float bandW, float coX, float ctrlW,
                            float hdrX, float hdrW);
    void draw_sc_config(u32 dev, Font* fo, const MouseState* mo, bool click,
                            float& ry, int& ri, float e,
                            float bandX, float bandW, float coX, float ctrlW,
                            float hdrX, float hdrW);
    void draw_tp_config(u32 dev, Font* fo, const MouseState* mo, bool click,
                            float& ry, int& ri, float e,
                            float bandX, float bandW, float coX, float ctrlW,
                            float hdrX, float hdrW);
    void draw_hl_config(u32 dev, Font* fo, const MouseState* mo, bool click,
                            float& ry, int& ri, float e,
                            float bandX, float bandW, float coX, float ctrlW,
                            float hdrX, float hdrW);
    void draw_pw_config(u32 dev, Font* fo, const MouseState* mo, bool click,
                            float& ry, int& ri, float e,
                            float bandX, float bandW, float coX, float ctrlW,
                            float hdrX, float hdrW);
    void draw_grim_config(u32 dev, Font* fo, const MouseState* mo, bool click,
                            float& ry, int& ri, float e,
                            float bandX, float bandW, float coX, float ctrlW,
                            float hdrX, float hdrW);
    void draw_zt_config(u32 dev, Font* fo, const MouseState* mo, bool click,
                            float& ry, int& ri, float e,
                            float bandX, float bandW, float coX, float ctrlW,
                            float hdrX, float hdrW);
    void draw_tm_config(u32 dev, Font* fo, const MouseState* mo, bool click,
                            float& ry, int& ri, float e,
                            float bandX, float bandW, float coX, float ctrlW,
                            float hdrX, float hdrW);
    void draw_ep_config(u32 dev, Font* fo, const MouseState* mo, bool click,
                            float& ry, int& ri, float e,
                            float bandX, float bandW, float coX, float ctrlW,
                            float hdrX, float hdrW);
    // Configuration-tab own category blocks (INTERFACE / LAYOUT), lifted out of draw() ; same row/geometry
    // signature as the per-module config panels (config_page.cpp ; byte-identical).
    void draw_interface_category(u32 dev, Font* fo, const MouseState* mo, bool click,
                                 float& ry, int& ri, float e,
                                 float bandX, float bandW, float coX, float ctrlW,
                                 float hdrX, float hdrW);
    void draw_layout_category(u32 dev, Font* fo, const MouseState* mo, bool click,
                              float& ry, int& ri, float e,
                              float bandX, float bandW, float coX, float ctrlW,
                              float hdrX, float hdrW);
    // shared "box appearance" rows (Box/Transparency/Theme/Hue/Luminosity) operating on any module's BoxStyle.
    void draw_box_appearance(u32 dev, Font* fo, const MouseState* mo, bool click,
                            float& ry, int& ri, float e,
                            float bandX, float bandW, float coX, float ctrlW, struct BoxStyle& bs);
    // shared per-element "Text" rows (Font/Size/Outline/Bold-Italic-CAPS/Colour/HSV+Alpha) operating on any
    // module's TextStyle. The caller keeps its own element SELECTOR (labels/count differ per module) and passes
    // the selected TextStyle. Called once per panel, panels are mutually exclusive -> CTRL_ID stays collision-free.
    void draw_text_style(u32 dev, Font* fo, const MouseState* mo, bool click,
                         float& ry, int& ri, float e,
                         float bandX, float bandW, float coX, float ctrlW, struct TextStyle& ts, bool swatch = false);
    // draw() sub-branches lifted out of the ~1200-line ConfigPage::draw() (same TU, config_page.cpp ; byte-identical).
    void draw_profile_bar(u32 dev, Font* fo, const MouseState* mo, bool click, float coX, float coW, float bodyY, float pulse);   // Configuration-tab quick profile switcher ; drawn AFTER the content so scroll overflow can't cover it
    void draw_edit_layout(const Frame& f, u32 dev, Font* fo, const MouseState* mo, bool click, float sw, float sh);
    void draw_profile_tab(const Frame& f, u32 dev, Font* fo, const MouseState* mo, bool click,
                          float ix, float iw, float bodyY, float bodyH, float pageBot, float pulse, float e);
    void draw_help_tab(const Frame& f, u32 dev, Font* fo, const MouseState* mo, bool click,
                       float ix, float iw, float bodyY, float bodyH, float pageBot, float pulse);
    void draw_update_tab(const Frame& f, u32 dev, Font* fo, const MouseState* mo, bool click,
                         float ix, float iw, float bodyY, float bodyH, float pageBot, float pulse, float e);

    bool  open_    = false;
    int   tab_     = 0;       // 0 = Configuration, 1 = Profil, 2 = Help
    int   section_ = 0;       // Configuration sidebar : 0 = Party/Alliance (more later)
    int   cfgTarget_ = 0;     // which box GROUP the Per box settings edit : 0 = Party, 1 = Alliance (both alliances)
    int   cfgTextElem_ = 0;   // which text element the Typography controls edit (Name/HP/MP/TP/Cast/Badge/Distance/Cost)
    bool  catOpen_[13] = { false, false, false, true, true, false, false, false, false, false, true, false, false };  // Config categories : sub-sections + the Party/Alliance category ([1]) START COLLAPSED ; open one to reveal its controls. ([3]Interface [4]Target [10]Player tops stay open so their collapsed sub-headers show.)
    float catH_[13] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };  // each OPEN category's measured block height -> its SOLID menu-card panel is drawn a frame later (0 = closed ; [6..9,11,12] unused, sub-sections have no panel)
    int   cfgTgtTextElem_ = 0;   // which Target text element the typography sub-section edits (TGT_NAME / TGT_HP / TGT_TIMER)
    int   cfgPlrTextElem_ = 0;   // which Player text element the typography sub-section edits (PLR_NAME / PLR_LVL)
    int   cfgMmTextElem_ = 0;    // which Minimap clock text element the typography sub-section edits (MM_TIME / MM_DAY / MM_MOON / MM_REAL)
    int   cfgScTextElem_ = 0;    // which Skillchains text element the typography sub-section edits (SC_TITLE / SC_TIMER / SC_STEP / SC_PROP / SC_LIST)
    int   cfgTpTextElem_ = 0;    // which Treasure Pool text element the typography sub-section edits (TP_IDX / TP_NAME / TP_TIMER / TP_LOOT)
    int   cfgHlTextElem_ = 0;    // which Hate List text element the typography sub-section edits (HL_DIST / HL_NAME / HL_PCT / HL_TARGET)
    int   cfgPwTextElem_ = 0;    // which PointWatch text element the typography sub-section edits (PW_LABEL / PW_VALUE / PW_RATE)
    int   cfgGrimTextElem_ = 0;  // which Grimoire text element the typography sub-section edits (GRIM_CHARGE / GRIM_TIMER)
    int   cfgZtTextElem_ = 0;    // which Zone Tracker text element the typography sub-section edits (ZT_HEADER / ZT_BODY)
    int   cfgEpTextElem_ = 0;    // which EmpyPop text element the typography sub-section edits (EP_TITLE / EP_POP / EP_FROM / EP_COLL)
    int   cfgTmTextElem_ = 0;    // which Timers text element the typography sub-section edits (TM_HEADER / TM_BODY)
    int   cfgDbTextElem_ = 0;    // which Debuffs text element the typography sub-section edits (DB_HEADER / DB_NAME / DB_TIMER)
    int   trkJob_ = 0;           // Timers "track per job" : MAIN job whose checklist is shown (0 = follow current main job)
    int   trkSub_ = -1;          // SUB job for the checklist (-1 = follow current sub ; 0 = none ; 1..22 = a job)
    int   trkScope_ = 0;         // 0 = Self (buffs on you + your recasts) ; 1 = Allies (buffs you put on allies)
    bool  trkSecOpen_ = false;   // the "Track per job" collapsible section
    bool  trkCatOpen_[32] = { false };   // per-category collapsible state within the checklist (index = TrackCat)
    // animation state (driven by the frame clock)
    float anim_     = 0.0f;   // open progress 0..1 (eased) -> fade in
    float lastT_    = -1.0f;  // previous frame time, for dt
    float tabSlide_ = -1.0f;  // interpolated x of the sliding active-tab indicator
    float hov_[5]   = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };   // eased hover amount per tab (Config / Profile / Edit Layout / Help / Update) -- MUST match NTABS (5) or the last tab reads OOB
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
    bool  helpPlayer_  = false;   // this frame the Help shows the Player-box sample -> the Hud draws the demo box in its slot
    float helpPlayerCx_ = 0.0f, helpPlayerCy_ = 0.0f, helpPlayerW_ = 0.0f, helpPlayerH_ = 0.0f;
    float cfgScroll_   = 0.0f;    // Configuration tab controls vertical scroll (mouse wheel)
    float cfgMaxScroll_= 0.0f;    // its extent, remembered for next frame's wheel clamp
    float updScroll_   = 0.0f;    // Update tab "What's new" changelog vertical scroll
    float updMaxScroll_= 0.0f;    // its extent, remembered for next frame's wheel clamp
    bool  relOpen_[32] = { true };// Update tab : per-version collapse state (newest RELEASES[0] starts OPEN, rest closed). Size must be >= RELEASES_N (config_page.cpp). Was 16 and RELEASES_N reached 17 at v1.0.37 : the render loop clamps to this size, so the OLDEST releases silently stopped being listed. Grown with headroom -- check this when adding a release.
    float helpMaxScroll_ = 0.0f;  // last frame's scroll limit -> clamp the wheel BEFORE drawing (no overscroll bounce)
    // live-preview anchor published each frame for the HUD (Configuration tab only)
    bool  pvOn_        = false;
    float pvRightX_    = 0.0f, pvBottomY_ = 0.0f;
    float pvCX_        = 0.0f, pvCY_ = 0.0f;   // live-preview stage centre (for the Target box preview)
    float pvSX_ = 0.0f, pvSY_ = 0.0f, pvSW_ = 0.0f, pvSH_ = 0.0f;   // live-preview stage rect (px)
    float pvStageX_ = 0.0f, pvStageY_ = 0.0f, pvStageW_ = 0.0f, pvStageH_ = 0.0f;   // full preview stage rect -> the opaque page bg leaves a HOLE here so the real game shows (transparent preview)
    // edit-layout : "Rules" toggle + the destructive-action confirmation modal
    bool  editShowLines_ = false;   // draw reference lines AND hide the HUD boxes (only rules + toolbar show)
    int   editConfirm_   = 0;       // 0 = none, 1 = Clear-lines pending, 2 = Default pending -> confirm dialog
    bool  profResetConfirm_ = false;   // Profile tab : "Reset all settings" pending -> confirm dialog
    // user-drawn ZONES : selection + inline rename (reuses the nameBuf_ text field) + rubber-band draw
    int   groupSel_      = -1;      // selected zone (-1 = none)
    int   editZoneName_  = -1;      // zone being renamed via the keyboard (-1 = none)
    float guideScroll_   = 0.0f;    // zone-list scroll in the panel  (panel position persists in ui_config.zonePanelX/Y)
    float zoneDrawX_     = 0.0f;    // rubber-band : start corner (screen px) while zoneDrawing_ is set
    float zoneDrawY_     = 0.0f;
    bool  zoneDrawing_   = false;
    u32   helpCursorTex_ = 0;       // party selection-hand texture, handed in by the HUD for the Help live cursor
    u32   logoTex_ = 0;             // AIOHUD logo mark (assets/aiohud_logo.raw), lazily loaded ; 0 = not loaded
    bool  logoTried_ = false;       // load attempted (don't retry every frame on failure)
    u32   tgtBuffTex_ = 0;          // Help tab : the Target debuff atlas + TH coffer, for the live samples (own copy, lazily loaded)
    u32   tgtThTex_ = 0;
    bool  tgtTexTried_ = false;     // attempted the load (don't retry the file every frame)
    u32   mmMkPlayer_ = 0;          // Help tab : the Minimap live samples -- player pin + mob arrow + element atlas + moon (own copies)
    u32   mmMkMob_ = 0;
    u32   mmElem_ = 0;
    u32   mmMoonTex_ = 0;           // supersampled moon-phase texture (rebuilt only when the sample's phase bucket changes)
    int   mmMoonKey_ = -1;          // cache key : day tint / phase bucket / waning
    u32   mmMapTex_ = 0;            // OWNED : the Help's own copy of the live zone map (loaded straight from the ROM DAT, like the widget) so the radar sample never depends on the floating Minimap having drawn. Released + reloaded on a zone change.
    unsigned mmMapFileId_ = 0;      // map file-id currently loaded into mmMapTex_ (0 = none) -> reload only when the zone's map changes
    bool  mmTried_ = false;         // attempted the marker/atlas load (don't retry the files every frame)
};

} // namespace aio
