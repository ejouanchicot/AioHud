// ui_config.h -- live, user-editable HUD settings (edited by the //aio config page, read by the
// widgets + Hud each frame). A process-wide singleton, like party(). Start small (the Party/Alliance
// basics) ; grow as more controls are added. Not yet persisted to disk.
#pragma once

namespace aio {

// One movable HUD block. x/y = top-left in SCREEN FRACTION (0..1) when posSet ; else the block uses
// its default placement (party = layout anchor ; alliances = stacked above the party).
struct BoxLayout {
    bool  posSet = false;
    float x = 0.0f, y = 0.0f;
    float scale = 1.0f;        // size multiplier (font + geometry)
};

// ---- user-drawn ZONES (simplified model). A zone is a named RECTANGLE you draw by drag-and-drop in edit
// mode's "Rules: ON" screen (rubber-band), then move / resize / rename. Permissions (allow[ZPERM_*]) say which
// HUD box may sit on it -- a box is pushed OUT of any zone that forbids its type (all-false = fully forbidden).
// The party-SIZING reference lines (partyRef / allyRefY below) are a SEPARATE system, kept as-is. ----
enum { ZPERM_PARTY = 0, ZPERM_ALLIANCE, ZPERM_HUB, ZPERM_COUNT };
struct GuideGroup {
    float x = 0.40f, y = 0.42f, w = 0.20f, h = 0.16f;   // top-left + size, screen fractions
    char  name[20] = { 0 };
    bool  allow[ZPERM_COUNT] = { false, false, false };
    int   role = 0;   // 0 = normal zone ; 1..6 = PARTY box for that member count (TOP sizes it) ; 7 = ALLIANCE 1, 8 = ALLIANCE 2
};
static const int GUIDE_GROUPS_MAX = 48;

// top Y (pixels) of the party-role zone for `count` members (its rectangle top drives the party box grow-up),
// or -1 if there is no such zone (caller then falls back to partyRef). See party.cpp.
float guide_party_top(int count, float sh);
// fill ar[0..3] = {A1 top, A1 bottom, A2 top, A2 bottom} (screen fractions) from the alliance-role zones
// (role 7 = A1, role 8 = A2). Returns true only if BOTH exist ; else the caller uses allyRefY. See party.cpp.
bool  guide_alliance_refs(float* ar);

// ---- per-element typography (global : applies to every box) ----
enum { TE_NAME = 0, TE_HP, TE_MP, TE_TP, TE_CAST, TE_BADGE, TE_DIST, TE_UI, TE_COUNT };
struct TextStyle {
    int   face    = 0;     // 0 = default (layout / global face) ; else index into the ui_font list
    float size    = 1.0f;  // multiplier on the element's base size
    float outline = 1.0f;  // multiplier on the element's outline width
    bool  bold    = false, italic = false, upper = false, colorOn = false;
    unsigned color = 0xFFFFFFFFu;   // tint used when colorOn (else the element keeps its own semantic colour)
};
const char* ui_text_elem_label(int e);   // "Name" / "HP" / ... / "Interface"

struct UiConfig {
    // ---- Party / Alliance ----
    TextStyle text[TE_COUNT];   // per-element typography (Name, HP, MP, TP, Cast, Badge, Distance, Interface)
    int   skinTheme = 0;       // window-skin theme index (the Hud applies it -> all boxes)
    int   fontFace  = 0;       // 0 = layout default ; >0 = override every party/alliance text face
    float buffScale = 0.92f;   // buff-icon size as a FRACTION of the member row height (0.40 .. 1.00, capped at the row)
    // ---- PER-BOX settings : index 0 = party, 1 = alliance 1, 2 = alliance 2 (independent) ----
    float barHeight[3] = { 1.0f, 1.0f, 1.0f };   // HP/MP/TP gauge HEIGHT scale, per box
    float barWidth[3]  = { 1.0f, 1.0f, 1.0f };   // HP/MP/TP gauge WIDTH scale, per box
    int   gaugeStyle[3] = { 0, 0, 0 };           // gauge look, per box (0 Vial, 1 Bars, 2 Segments, 3 Minimal, 4 Sphere, 5 Ring, 6 Crystal, 7 Text)
    int   jobBadge[3]  = { 2, 2, 2 };            // job badge, per box (0 = off, 1 = main only, 2 = main + sub)
    bool  cast[3]      = { true, true, true };   // show the casting-spell line, per box
    bool  dist[3]   = { true, true, true };   // show the distance number, per box (0 = party, 1 = ally 1, 2 = ally 2)
    BoxLayout box[3];          // 0 = party (+cost), 1 = alliance 1, 2 = alliance 2 (independent)
    bool  border[3] = { true, true, true };   // per-box window-skin border/chrome on/off (0=party, 1=alliance1, 2=alliance2)
    bool  borderCost = true;   // the floating Cost MP / Next box border/chrome on/off (independent of the party box)
    bool  animHP = true;       // HP gauges : critical-low blink animation on/off
    bool  animTP = true;       // TP gauges : WS-ready (>= 1000) pulse animation on/off
    bool  editLayout = false;  // layout edit mode : boxes draggable / resizable on the live game
    int   wheel = 0;           // pending wheel steps (mouse slot -> consumed by the hovered box in edit mode)
    // reference LINE per native-party size : the party box grows UP to this Y (fraction of screen height)
    // so the game's native party window is covered. The native top differs by member count, so there is
    // ONE line per count : [i] = (i+1) players. -1 = unset (no grow for that count).
    float partyRef[6] = { -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f };
    float partyBottomY = -1.0f;   // line 0 : the BOTTOM of the native party window -- a VISUAL reference only
                                  // (documents where the native party ends). -1 = unset.
    float partyRefX[2] = { -1.0f, -1.0f };   // two VERTICAL reference markers : the LEFT [0] and RIGHT [1] edges of
                                             // the native party window (fraction of screen WIDTH). -1 = unset.
    // four FIXED horizontal markers for the native ALLIANCE windows (their size does NOT vary with member
    // count) : [0] = alliance 1 TOP, [1] = alliance 1 BOTTOM, [2] = alliance 2 TOP, [3] = alliance 2 BOTTOM.
    float allyRefY[4] = { -1.0f, -1.0f, -1.0f, -1.0f };
    // ---- user-drawn zones ----
    GuideGroup guideGroup[GUIDE_GROUPS_MAX];
    int        guideGroupCount = 0;
    float      zonePanelX = -1.0f, zonePanelY = -1.0f;   // draggable Zones panel top-left (fraction ; -1 = default top-right)
    // ---- Global ----
    int   lang = 0;            // config UI language : 0 = English, 1 = French (toggle in the config header)
};

UiConfig& ui_config();         // the singleton

// ---- P3 : guide ZONES = the rectangle each group forms from its rules (H rules -> top/bottom, V rules ->
// left/right ; a missing axis spans the whole screen). Permissions (allow[ZPERM_*]) say which box may sit on
// a zone -- a box is pushed OUT of any zone that forbids its type. ----
int  guide_zones(float sw, float sh, float* x, float* y, float* w, float* h, int* group, int cap);   // -> count
void guide_push_out(int perm, float sw, float sh, float& ex, float& ey, float ew, float eh);          // clamp a box out of forbidden zones

// persistence : skinTheme / fontFace / per-box position+scale are saved to disk and restored.
void load_ui_config();         // called once at startup
void save_ui_config();         // call after a change (stepper / edit-mode exit / reset)
void reset_ui_config();        // restore ALL defaults (theme/font/boxes) + save  (general Default)
void reset_boxes();            // restore only box positions + sizes + save  (edit-mode Default)

// ---- profiles : snapshot the WHOLE UiConfig under a name (files aiohud_profiles\<name>.txt, same
// key=value format as the live config). Saving more modules later just means more keys in UiConfig --
// profiles serialize all of it, no per-module work here. ----
int         profile_count();
const char* profile_name(int idx);                 // 0-based ; "" if out of range
void        profile_refresh();                     // rescan the folder (Profile tab open / after save/delete)
bool        profile_save(const char* name);        // write the CURRENT config under <name> (create or overwrite)
bool        profile_load(const char* name);        // load <name> into the live config (+ persist as current)
bool        profile_delete(const char* name);
bool        profile_exists(const char* name);
void        profile_mark_clean();                  // snapshot the current config as "saved" (call on load/save)
bool        profile_dirty();                       // true if the live config differs from that snapshot -> unsaved changes
// the LAST loaded/saved profile is remembered on disk and auto-applied at startup, so a relaunch
// comes back on the same profile (set automatically by profile_load / profile_save).
void        set_active_profile(const char* name);
const char* active_profile_name();                 // "" if none
// A bulk config load (startup, profile load, reset) replaces every box scale + position at once. The HUD
// must ADOPT those as the new baseline instead of treating them as a live resize (which would re-anchor
// the boxes and falsely mark the profile "modified"). Set on any such load ; consumed once by the HUD.
void        request_scale_baseline_reset();
bool        take_scale_baseline_reset();           // returns + clears the flag

// selectable font faces for the Font control (index 0 = "Default" = keep the layout font).
int         ui_font_count();
const char* ui_font_label(int idx);   // display label
const char* ui_font_face(int idx);    // GDI face name ("" for Default)

} // namespace aio
