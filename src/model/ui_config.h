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
    BoxLayout box[3];          // 0 = party (+cost), 1 = alliance 1, 2 = alliance 2 (independent)
    bool  border[3] = { true, true, true };   // per-box window-skin border/chrome on/off (0=party, 1=alliance1, 2=alliance2)
    bool  borderCost = true;   // the floating Cost MP / Next box border/chrome on/off (independent of the party box)
    bool  editLayout = false;  // layout edit mode : boxes draggable / resizable on the live game
    int   wheel = 0;           // pending wheel steps (mouse slot -> consumed by the hovered box in edit mode)
    float partyRefY = -1.0f;   // party "Set reference" Y (box[0].y when aligned on the native block) ; -1 = unset.
                               // Lowering the box below it adds the delta to the height (top stays pinned).
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

// selectable font faces for the Font control (index 0 = "Default" = keep the layout font).
int         ui_font_count();
const char* ui_font_label(int idx);   // display label
const char* ui_font_face(int idx);    // GDI face name ("" for Default)

} // namespace aio
