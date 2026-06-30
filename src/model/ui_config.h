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

struct UiConfig {
    // ---- Party / Alliance ----
    int   skinTheme = 0;       // window-skin theme index (the Hud applies it -> all boxes)
    int   fontFace  = 0;       // 0 = layout default ; >0 = override every party/alliance text face
    float buffScale = 0.92f;   // buff-icon size as a FRACTION of the member row height (0.40 .. 1.00, capped at the row)
    float barHeight = 1.0f;    // HP/MP/TP gauge HEIGHT scale (the taller rows give vertical room)
    float barWidth  = 1.0f;    // HP/MP/TP gauge WIDTH scale (the box auto-fits wider)
    int   jobBadge  = 2;       // job badge : 0 = off (column collapses), 1 = main job only, 2 = main + sub
    bool  castParty = true;    // show the casting-spell line for PARTY members
    bool  castAlly  = true;    // show the casting-spell line for ALLIANCE members
    bool  dist[3]   = { true, true, true };   // show the distance number, per box (0 = party, 1 = ally 1, 2 = ally 2)
    BoxLayout box[3];          // 0 = party (+cost), 1 = alliance 1, 2 = alliance 2 (independent)
    bool  border[3] = { true, true, true };   // per-box window-skin border/chrome on/off (0=party, 1=alliance1, 2=alliance2)
    bool  borderCost = true;   // the floating Cost MP / Next box border/chrome on/off (independent of the party box)
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
    // ---- Global ----
    int   lang = 0;            // config UI language : 0 = English, 1 = French (toggle in the config header)
};

UiConfig& ui_config();         // the singleton

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
