// text_style.h -- the ONE implementation of "resolve a TextStyle for drawing", shared by every module.
// Each module used to re-declare an identical <m>_font/_sz/_ow/_col quartet over its own ui_config().<m>Text[]
// array (~30 copies). Now those are one-line wrappers around te_* : the font-resolution / size / outline /
// colour logic lives here once. Behaviour is byte-for-byte what the per-module copies did.
#pragma once
#include "gfx/font.h"
#include "model/ui_config.h"   // TextStyle, ui_font_face
#include "ui/widget.h"         // Frame

namespace aio {

// the element's Font : configured face (0 = layout default), 700/400 weight from bold, italic. Null-safe fallback.
inline Font* te_font(const Frame& f, const TextStyle& t) {
    if (!f.fonts) return f.font;
    Font* r = f.fonts->get(t.face > 0 ? ui_font_face(t.face) : 0, t.bold ? 700 : 400, t.italic);
    return r ? r : f.font;
}
inline float te_sz (const TextStyle& t, float base) { return base * t.size; }         // scaled base size
inline float te_ow (const TextStyle& t, float base) { return base * t.outline; }      // scaled outline width
inline u32   te_col(const TextStyle& t, u32 base)   { return t.colorOn ? t.color : base; }   // custom colour override
// UPPERCASE `s` into `buf` (cap = buffer size incl. NUL) when the element's style wants CAPS ; else return `s`
// unchanged. Same ASCII a-z -> A-Z transform every module's per-element "_up" helper used to inline.
// Truncate `s` with a trailing "." run until it fits `maxW` at `sz` (CSS text-overflow: ellipsis ; the font
// atlas has no U+2026, so dots stand in). Returns `s` untouched when it already fits, else `buf`.
// Three dots everywhere (the Hate List used two until it was aligned). `dots` stays a parameter for the
// rare caller with a very narrow column, but "..." is the house style. Bounded by `cap` in all cases.
inline const char* fit_ellipsis(Font* fo, const char* s, float sz, float maxW, char* buf, int cap, int dots = 3) {
    if (!s || !fo || fo->measure(s, sz) <= maxW) return s;
    if (dots < 1) dots = 1; if (dots > 3) dots = 3;
    int n = 0; while (s[n]) ++n;
    for (int len = n - 1; len >= 1; --len) {
        int c = 0; for (; c < len && c < cap - (dots + 1); ++c) buf[c] = s[c];
        for (int d = 0; d < dots; ++d) buf[c++] = '.';
        buf[c] = 0;
        if (fo->measure(buf, sz) <= maxW) return buf;
    }
    buf[0] = s[0]; buf[1] = 0; return buf;
}

inline const char* te_upper(const TextStyle& t, const char* s, char* buf, int cap) {
    if (!t.upper || !s) return s;
    int i = 0; for (; s[i] && i < cap - 1; ++i) { char c = s[i]; buf[i] = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c; } buf[i] = 0; return buf;
}

} // namespace aio
