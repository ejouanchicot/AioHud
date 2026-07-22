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

// A collapsible COLOUR FIELD (accordion) : a compact "[caret] label ... [swatch]" row that expands ONE colour picker
// below it on click. Only one field open at a time (global latch) -> a panel with several colours stays tidy instead
// of stacking a full picker per colour. LABEL = const char* ; FIELDPTR = &u32 ; IDX = unique per loop iteration.
#define CFG_COLOR_FIELD_I(LABEL, FIELDPTR, IDX) \
    { const int cfu_ = ::aio::ctrl_uid_i(CTRL_ID, 900 + (IDX)); const bool cfo_ = ::aio::color_field_open(cfu_); \
      { row_band(dev, bandX, ry, bandW, snap(42.0f), (ri & 1) != 0, 0.0f); \
        float ap_ = stagger(anim_, ri); g_fade = e * ap_; \
        if (::aio::color_field_row(dev, fo, mo, coX, ry + (1.0f - ap_) * snap(14.0f) + snap(2.0f), ctrlW, LABEL, *(FIELDPTR), cfo_)) \
            ::aio::color_field_toggle(cfu_); } \
      ROW_NEXT(42.0f) \
      if (cfo_) { CFG_COLOR_PICKER_I(FIELDPTR, IDX) } }
#define CFG_COLOR_FIELD(LABEL, FIELDPTR) CFG_COLOR_FIELD_I(LABEL, FIELDPTR, 0)
