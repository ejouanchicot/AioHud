// config_rows.h -- the ROW_BAND / ROW_NEXT layout macros, shared by every config panel :
// config_page.cpp's own sections (Interface / Layout) AND the per-module *_config.cpp methods
// (target_config.cpp, party_config.cpp). Hoisted to ONE definition so the branches can't silently
// drift -- a row laid out in the party panel and one in the target panel stay pixel-identical.
//
// CONTRACT -- the including scope must provide, by these EXACT names :
//   u32 dev ; float bandX, bandW, e, ry ; int ri ; a `float anim_` (the ConfigPage member).
//   (ry / ri may be references -- they are advanced in place.) g_fade + row_band + stagger + snap
//   come from config_controls.h (included below). Every ROW_BAND MUST sit in its own { } block --
//   it declares ap/yo. #undef both at the end of the panel that used them.
//
//   ROW_BAND(slotH) : draw the zebra row band + set the staggered entrance (g_fade, yo). yo also
//                     vertically centres 40px row content inside a taller slot.
//   ROW_NEXT(adv)   : advance the running cursor ry by the slot height and bump the stagger index ri.
#pragma once
#include "ui/config_controls.h"   // row_band, stagger, snap, g_fade

#define ROW_BAND(slotH)   row_band(dev, bandX, ry, bandW, snap(slotH), (ri & 1) != 0, 0.0f); \
    float ap = stagger(anim_, ri); g_fade = e * ap; \
    float yo = (1.0f - ap) * snap(14.0f) + (snap(slotH) - snap(40.0f)) * 0.5f; (void)ap;
#define ROW_NEXT(adv)     ry += snap(adv); ri++;

// ---- the On/Off toggle ROW, hoisted here so a panel writes ONE line instead of five ----
// Six panels each carried a byte-identical `#define <MOD>_TOGGLE`, and ~40 more rows were written out longhand
// (label + chip geometry + the toggle-and-save branch, copied verbatim). They are all this:
//
//   { ROW_BAND(band) label at coX+4 ; a chip of chipW x 34 right-aligned in ctrlW ; click -> !field + save } ROW_NEXT(band)
//
// Pass CTRL_ID from the CALL SITE. (To be precise about WHY, because the older wording here was wrong and would
// send you chasing the wrong thing: __FILE__ and __LINE__ inside a macro body expand at the point of INVOCATION,
// so a CTRL_ID written inside the macro would still be unique per call site -- CFG_COLOR_PICKER_I below relies on
// exactly that and is correct. The real trap is a LOOP: several iterations invoked on the SAME source line share
// one CTRL_ID, which is how the three distance colours in party_config.cpp ended up on one spring and dragged
// together. That is what `sub` / ctrl_uid_i(CTRL_ID, i) is for.) Keeping it at the call site is still the rule
// here -- it makes the uid visible where the row is written.
//
// ROW_TOGGLE   : the standard row (band 48, content 38 high, 112-wide chip).
// ROW_TOGGLE_G : same row with explicit geometry, for the panels whose rows are deliberately taller/wider
//                (party / target / player use band 52, content 40, and a 150 or 126/140-wide chip). Passing the
//                real numbers keeps those rows PIXEL-IDENTICAL instead of quietly normalising them.
#define ROW_TOGGLE(UID, LABEL, FIELD) \
    { ROW_BAND(48.0f) row_toggle(dev, fo, mo, click, UID, coX, ry + yo, ctrlW, LABEL, &(FIELD)); } ROW_NEXT(48.0f)
#define ROW_TOGGLE_G(UID, LABEL, FIELD, BAND, ROWH, CHIPW) \
    { ROW_BAND(BAND) row_toggle(dev, fo, mo, click, UID, coX, ry + yo, ctrlW, LABEL, &(FIELD), (ROWH), (CHIPW)); } ROW_NEXT(BAND)

// Two-state chip rows whose labels are NOT On/Off ("Fused"/"Separate", "Standalone"/"In Player", ...).
#define ROW_CHOICE(UID, LABEL, FIELD, ON, OFF)     { ROW_BAND(48.0f) row_choice(dev, fo, mo, click, UID, coX, ry + yo, ctrlW, LABEL, &(FIELD), (ON), (OFF)); } ROW_NEXT(48.0f)
#define ROW_CHOICE_G(UID, LABEL, FIELD, ON, OFF, BAND, ROWH, CHIPW)     { ROW_BAND(BAND) row_choice(dev, fo, mo, click, UID, coX, ry + yo, ctrlW, LABEL, &(FIELD), (ON), (OFF), (ROWH), (CHIPW)); } ROW_NEXT(BAND)

// an HSV colour-picker row (SV square + hue bar + swatch), TOP-aligned in its tall band (ROW_BAND would
// centre it as 40px content). Portable across every panel : needs the ROW_BAND contract vars PLUS
// dev/mo/coX/ctrlW from the panel signature. USV/UHUE = two UNIQUE drag uids ; FIELDPTR = &u32 colour.
#define CFG_COLOR_PICKER(FIELDPTR) CFG_COLOR_PICKER_I(FIELDPTR, 0)

// LOOP variant. CTRL_ID is a file:LINE hash, so a picker expanded inside a `for` gives EVERY iteration the
// SAME uid -- the pickers then share one drag/hover slot and dragging one moves the others (this shipped: the
// Close / Normal / Far distance colours in party_config.cpp). Pass the loop index and each item gets its own
// pair of uids, mixed through ctrl_uid_i so they scatter instead of landing on a neighbour's id.
#define CFG_COLOR_PICKER_I(FIELDPTR, IDX) \
    { row_band(dev, bandX, ry, bandW, snap(color_picker_height()), (ri & 1) != 0, 0.0f); \
      float ap_ = stagger(anim_, ri); g_fade = e * ap_; \
      color_picker(dev, fo, mo, ::aio::ctrl_uid_i(CTRL_ID, (IDX) * 2), ::aio::ctrl_uid_i(CTRL_ID, (IDX) * 2 + 1), \
                   coX, ry + (1.0f - ap_) * snap(14.0f) + snap(6.0f), ctrlW, FIELDPTR); } \
    ROW_NEXT(color_picker_height())
