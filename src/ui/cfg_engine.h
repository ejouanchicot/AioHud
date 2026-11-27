// cfg_engine.h -- the config REDESIGN's option engine (milestone 1, 2026-11-26).
//
// An option is DESCRIBED once (OptDef) ; the engine draws it with the kit (cfg_kit.h), saves it, greys it with its reason
// when a dependency makes it inert, marks it "modified" when it differs from the factory value, and resets it to that
// value on a right-click. A page is a list of these tables between section headers. Pilot page : Target
// (target_config.cpp draw_target_v2).
//
// "Modified" / reset work on ANY field that lives inside UiConfig : its factory twin is found at the same offset in a
// value-initialised UiConfig (cfg_factory_of). So the shared blocks (Frame on a BoxStyle, Text on a TextStyle) get the
// marker without a table of their own.
#pragma once
#include "ui/cfg_kit.h"
#include "model/ui_config.h"

namespace aio {

enum OptKind : unsigned char {
    OK_TOGGLE,     // int 0 / 1
    OK_PCT,        // float lo..hi, shown in %
    OK_INT,        // int lo..hi
    OK_SELECT,     // int index into choices
    OK_GROW,       // a box's "grows toward" (boxGrow[growBox], applied through box_grow_set)
    OK_NOTE,       // a line of text, no field
    OK_BTOGGLE,    // bool 0 / 1 (the older fields : border[], cast[], dist[], anim*)
};
enum : unsigned char { OL_ADV = 0, OL_SIMPLE = 1 };   // shown in Advanced only / in both modes

struct OptDef {
    const char* en; const char* fr;
    OptKind kind; unsigned char level;
    void* (*field)(UiConfig&);                 // the setting (null for a note)
    float lo, hi;                              // PCT / INT range
    const char* const* chEn; const char* const* chFr; int nCh;   // SELECT choices
    const char* (*why)(const UiConfig&);       // non-null and returning text : greyed, with this reason (a dependency)
    int growBox;                               // OK_GROW : UiConfig::GrowBox
};

// ---- the ROWS : one per control kind. `p`/`size` name the setting for the modified marker and the right-click
// reset (null : neither) ; `why` non-null greys the row with that reason. y advances past the row. ----
bool cfg_toggle_row(Kit& K, int uid, float x, float& y, float w, const char* label, bool on, const char* why, void* p, int size);   // true = clicked
bool cfg_slider_row(Kit& K, int uid, float x, float& y, float w, const char* label, float* v01, const char* val, const char* why, void* p, int size, bool isFloat);   // true = moved (saved on release)
int  cfg_select_row(Kit& K, int uid, float x, float& y, float w, const char* label, const char* val, const char* why, void* p, int size);   // -1 / +1 = an arrow
int  cfg_hue_row(Kit& K, int uid, float x, float& y, float w, const char* label, const u32* cols, int n, int sel, const char* why, void* p, int size);   // clicked hue / -1
void cfg_note(Kit& K, float x, float& y, const char* text);
void cfg_color_row(Kit& K, int uidBase, float x, float& y, float w, unsigned* col);   // the colour picker, under the row that chose "custom"
void cfg_flush(const MouseState* mo);   // end of page : a slider moved this gesture is saved once the button is up

// draws the rows of `d` that the mode shows (adv = Advanced), from y ; returns true if one changed (it is saved).
bool cfg_rows(Kit& K, int uidBase, const OptDef* d, int n, float x, float& y, float w, bool adv);
// how many of `d` (in this mode) differ from the factory value -- the section header's count
int  cfg_count_modified(const OptDef* d, int n, bool adv);
int  cfg_count_modified_bytes(const void* p, int size);   // a whole block (a BoxStyle, a TextStyle) : 1 if it differs
// a section header ; foldable in Advanced (click toggles *open), always open in Simple. Advances y past it.
bool cfg_section(Kit& K, int uid, float x, float& y, float w, const char* title, bool* open, int nMod, bool foldable);

// ---- the SHARED blocks, in the kit (every page's Frame and Text) ----
struct FrameRefs {   // a box's frame settings. on / alphaOwn / themeCopy may be null (that row does not exist) ; the others are REQUIRED
    int* on; int* border; float* alpha; int* alphaOwn; int* themeCopy; int* theme; unsigned* hue; float* lum;
    bool* borderB = nullptr;   // the Party / Alliance boxes keep their border as a bool : given instead of `border`
};
int  cfg_frame_modified(const FrameRefs& r);   // how many of its rows differ from the factory
void cfg_frame_rows(Kit& K, int uidBase, float x, float& y, float w, const FrameRefs& r);
int  cfg_text_modified(TextStyle* const* ts, int n);
// the element selector (sel = the page's remembered index) then that element's Font / Size / Outline / Style / Colour
void cfg_text_rows(Kit& K, int uidBase, float x, float& y, float w, const char* const* labels, TextStyle* const* ts, int n, int* sel);

} // namespace aio
