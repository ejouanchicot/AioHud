// layout.h -- the in-memory layout descriptor (round-trip with design/exports/layout.json).
//
// This is the native twin of the mockup's `layout.json`: the SINGLE persistence format
// shared by the browser "studio" and the in-game native editor. It is MUTABLE and
// SERIALISABLE both ways -- load it at startup to place widgets, mutate it from the
// in-game editor (drag / toggle / options), save it back. Schema: docs/formats/layout-json.md.
#pragma once
#include <string>
#include <vector>
#include "io/json.h"

namespace aio {

// one widget placement + its resolved config (the config object is kept as raw JSON so
// each widget reads the keys it understands -- the schema lives in the mockup).
struct LWidget {
    std::string id;
    std::string type;             // native widget class name (factory key)
    char   ah = 'l';              // horizontal anchor : 'l' = left, 'r' = right
    char   av = 't';              // vertical anchor   : 't' = top,  'b' = bottom
    double x = 0.0, y = 0.0;      // position in % of the viewport, relative to the anchor
    double w = 0.0;               // fixed width in px (valid only if !wAuto)
    bool   wAuto = true;          // true = width adapts to content
    int    z = 0;                 // draw order (ascending = on top)
    bool   visible = true;        // manual show/hide (job gating is separate, via `jobs`)
    std::vector<std::string> jobs;// gating : shown only if main/sub in this list ; empty = always
    bool   growDown = false;      // top-anchored growth (top fixed, content varies at the bottom)
    bool   bare = false;          // no chrome (image-only box)
    json::Value config;           // typed config object (number/bool/string per the schema)
};

struct LZone {
    std::string label;
    double x = 0.0, y = 0.0, w = 0.0, h = 0.0;   // % of the viewport
    std::vector<std::string> allow;              // widget ids allowed to overlap this zone
};

struct Layout {
    int    version = 1;
    double vpW = 0.0, vpH = 0.0;  // reference viewport (px) the % were authored against
    std::string font = "Segoe UI";// GLOBAL text face (GDI name) for the whole HUD
    int    fontWeight = 600;      // GLOBAL weight (400 normal, 600 semibold, 700 bold)
    std::vector<LWidget> widgets;
    std::vector<LZone>   zones;
};

// pixel rectangle of a placed widget. w/h < 0 means "unknown" (auto size not yet measured).
struct PxRect { float x, y, w, h; };

// ---- round-trip with disk (Win32 file I/O ; returns false on any failure, never throws) ----
bool load_layout(const char* path, Layout& out);
bool save_layout(const char* path, const Layout& lay);

// Resolve a widget's top-left pixel position on a screen of (sw, sh). `contentW/contentH`
// are the widget's measured size (px) -- needed to anchor right/bottom boxes and for auto width.
// Returns w/h echoed from (fixed w or contentW) and contentH.
PxRect widget_px(const LWidget& w, float sw, float sh, float contentW, float contentH);

// Inverse (for in-game drag): given the box's pixel rect on screen, pick the nearest
// anchor (like the mockup) and set the widget's anchor + % accordingly.
void widget_set_from_px(LWidget& w, float px, float py, float ww, float hh, float sw, float sh);

} // namespace aio
