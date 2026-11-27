// cfg_kit.h -- the config REDESIGN's component kit (2026-11-26, step 3 / milestone 1 groundwork).
//
// One drawing of each control, in every state, sized in a single unit `u` (1 u = 1 px at 1080p, scaled to the screen
// and the text-size preference) -- the old config draws in FIXED pixels, which is why it is illegible at 1440p / 4K.
// Design : docs of the redesign (artifact "Conception config AioHUD") ; base = the Gemini mockup (calm, real toggles,
// graduated diamond slider, gold "modified" corner) + the ChatGPT one's FFXI cursor for the focus and the sidebar's
// status dots. GOLD is reserved for what is active or asks for an action.
//
// Immediate mode, like the rest of the config : call each frame ; ids are CTRL_ID / ctrl_uid_i (rule 9). A component
// draws its shapes then its text and leaves the colour-quad state behind it.
#pragma once
#include "ui/widget.h"

namespace aio {

// the palette (ARGB) -- contrast measured on KIT_BG : text 15:1, text-2 7:1
enum : u32 {
    KIT_BG       = 0xFF101318u,   // the window
    KIT_SURF     = 0xFF1A1F27u,   // section cards, the sidebar
    KIT_SURF_HI  = 0xFF232A34u,   // section header, a hovered row
    KIT_STEEL    = 0xFF4A5563u,   // outlines, rails, a toggle off
    KIT_TEXT     = 0xFFEEF1F5u,
    KIT_TEXT2    = 0xFFA3AEBCu,
    KIT_GOLD     = 0xFFD2A64Eu,   // active, the main action, focus, modified
    KIT_OFF      = 0xFF6B7480u,   // unavailable (always with its reason in text)
    KIT_ON_DOT   = 0xFF5CC08Au,   // a module switched on (sidebar)
    KIT_DANGER   = 0xFFD9675Au,
};
// component states (combinable) : forced on the sheet, computed live elsewhere
enum { KS_HOVER = 1, KS_DISABLED = 2, KS_FOCUS = 4, KS_MODIFIED = 8, KS_DRAG = 16 };

struct Kit {
    u32 dev = 0;
    Font* fr = nullptr;     // Segoe UI regular
    Font* fs = nullptr;     // Segoe UI semibold
    const MouseState* mo = nullptr;
    bool click = false;
    float u = 1.0f;         // 1 u = 1 px at 1080p
    float px(float v) const;  // v u -> snapped screen px
    float clipTop = -1e9f, clipBot = 1e9f;   // the visible band : a row wholly outside it is not drawn (it only advances y)
};
bool kit_begin(Kit& K, const Frame& f, float sh, float textScale);   // fonts + unit ; false = not drawable this frame

// the parts -- each returns its interaction (clicked / changed / step) ; `st` forces states (KS_*)
bool  kit_toggle(Kit& K, int id, float x, float y, bool on, int st);                     // 46 x 26 u
bool  kit_slider(Kit& K, int id, float x, float y, float w, float* v01, const char* val, int st);
int   kit_selector(Kit& K, int id, float x, float y, float w, const char* val, int st);  // -1 / 0 / +1
bool  kit_section(Kit& K, int id, float x, float y, float w, const char* title, bool open, int nModified, int st);
void  kit_row(Kit& K, float x, float y, float w, float h, int st);                        // the row's ground : hover, focus cursor, modified corner
void  kit_label(Kit& K, float x, float cy, const char* s, int st, const char* reason = nullptr);
bool  kit_button(Kit& K, int id, float x, float y, float w, const char* s, int kind, int st);   // kind 0 main, 1 secondary, 2 destructive
void  kit_search(Kit& K, float x, float y, float w, const char* text, int st);
void  kit_result(Kit& K, float x, float y, float w, const char* label, const char* path, const char* val, int st);
bool  kit_swatch(Kit& K, int id, float x, float y, u32 col, int st);
bool  kit_nav(Kit& K, int id, float x, float y, float w, const char* s, bool active, bool moduleOn, int st);
void  kit_nav_group(Kit& K, float x, float y, const char* s);
bool  kit_tab(Kit& K, int id, float x, float y, const char* s, bool active, int st);
void  kit_focus_cursor(Kit& K, float x, float cy);                                        // the FFXI pointer, tip at (x, cy)

// the CHROME : the window around the pages, same metal, drawn into the caller's px rects (cfg_kit.cpp)
void  chrome_plate(Kit& K, float x, float y, float w, float h);
float chrome_tag(Kit& K, float x, float cy, const char* s);                                  // returns its right edge
int   chrome_segment(Kit& K, int id, float x, float y, float segW, float h, const char* const* labels, int n, int sel);   // clicked index / -1
bool  chrome_close(Kit& K, int id, float x, float y, float s);
void  chrome_tab_text(Kit& K, float cx, float cy, const char* s, bool active, float t, float sz);
u32   chrome_tab_ink(bool active, float t);
void  chrome_nav(Kit& K, float x, float y, float w, float h, const char* s, bool active, float t);
void  chrome_nav_group(Kit& K, float x, float cy, const char* s);
void  chrome_rivet(Kit& K, float cx, float cy);
bool  chrome_keybtn(Kit& K, int id, float x, float y, float w, float h, const char* s, int kind, bool enabled, bool housing = true);   // kind 0 steel, 1 brass, 2 destructive (steel), 3 latched (held down : the current state), 4 armed / confirm (red lacquer)
void  chrome_label(Kit& K, float x, float cy, const char* s, float sz, int kind);   // 0 metal, 1 note, 2 brass, 3 red, 4 group caps
float chrome_label_w(Kit& K, const char* s, float sz);
void  chrome_field(Kit& K, float x, float y, float w, float h, float focus);
void  chrome_medallion(Kit& K, float cx, float cy, float r, const char* s);
void  chrome_rowplate(Kit& K, float x, float y, float w, float h, bool active, float t);
float chrome_tag_steel(Kit& K, float x, float cy, const char* s);
void  chrome_plaque(Kit& K, float x, float y, float w, float h, const char* s, float sz);   // a brass plate, the words in ink   // a key in a socket that presses
int   chrome_stepper(Kit& K, int id, float x, float y, float w, float h, float kw, const char* s, bool dim, bool housing = true);
void  chrome_casting(Kit& K, float x, float by, float bw, float bh, float railW, float railBot);   // the beam + the rail plate, ONE piece
void  chrome_rail_rivets(Kit& K, float x, float y, float w, float h);
void  chrome_housing(Kit& K, float x, float y, float w, float h);   // housing + 2 pressing keys + window ; -1 / +1
void  chrome_mullion(Kit& K, float x, float y, float w, float h);                          // the bar between two openings of one frame
void  chrome_slot(Kit& K, float x, float y, float w, float h);                             // a slot milled in a plate
float chrome_tabkey(Kit& K, int id, float x, float y, float w, float h, bool active);     // returns the hover ease
void  chrome_transom(Kit& K, float x, float y, float w, float h);                          // the horizontal bar between two openings
void  chrome_weld(Kit& K, float ax, float ay, float bx, float by);                         // a weld seam along a joint
void  chrome_floor(Kit& K, float x, float y, float w, float h);
void  chrome_bezel(Kit& K, float x, float y, float w, float h, float r);   // a recess cut in the steel
int   chrome_hue_strip(Kit& K, int id, float x, float y, float w, float h, const u32* cols, int n, int sel, bool disabled);   // a colour ruler ; clicked index / -1
u32   kit_ground_alpha();   // the config ground's alpha (Interface : Background)
// the LEDGER (a hunt board) : its column heads (a band), a row (lamp + engraved-glass cells ; colAlign 0 left / 2 right)
void  chrome_ledger_head(Kit& K, float x, float y, float w, float h, const char* const* cols, const float* colX, const int* colAlign, int n);
bool  chrome_ledger_row(Kit& K, int id, float x, float y, float w, float h, const char* const* cells, const float* colX, const int* colAlign, int n, bool sel, bool odd);                            // an opening's floor (no edge)                    // sunken bed (the settings)                    // a recessed zone (no outline)

// the component SHEET (//aio config kit) : every part in every state, live where it can be ; true = its Close was clicked
bool kit_sheet_draw(const Frame& f, float sw, float sh);

} // namespace aio
