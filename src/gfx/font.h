// font.h -- a GDI-baked bitmap-font ATLAS, baked PER DISPLAY SIZE so text is pixel-perfect.
//
// Instead of one 32px atlas scaled down to every size (= blurry small text), each unique pixel
// size gets its OWN atlas, baked by GDI at exactly that size (hinted, antialiased). draw() then
// emits the glyphs 1:1 -> crisp at any size, no minification. Atlases are built lazily and cached
// (a small per-size "slot" pool). Codepoints 32..255 (ASCII + Latin-1 supplement -> French accents) in the slot's
// atlas ; every codepoint past that (Japanese, symbols -- 2026-11-17) on demand in the shared WIDE pages.
#pragma once
#include "d3d.h"

namespace aio {

// Reading spacing before punctuation (2026-11-16, the chat : "la ponctuation est collee a la derniere lettre").
// Segoe UI at 12-14 px leaves 2 px between a letter and '!' / '?' and a 3-4 px space ; a 1 px dark stroke eats
// that, so "Blocked! [" read as "Blocked![". Extra px BEFORE c when it follows a LETTER (never a digit : "17:05",
// "1.5" stay intact) : a thin space before ! ? ; : and a hair before . , -- `gap` = the full step in screen px.
inline bool punct_letter(unsigned char p) { return (p >= 'A' && p <= 'Z') || (p >= 'a' && p <= 'z') || (p >= 0xC0 && p != 0xD7 && p != 0xF7); }
// A word space gets half a step more too ("a vial", "the Vampire" : Roboto's 3 px space, minus the stroke, read as
// glued -- 2026-11-16, the raw text was clean at every colour change).
inline float punct_pre(unsigned char prev, unsigned char c, float gap) {
    if (gap <= 0.0f) return 0.0f;
    if (c == ' ') return (prev && prev != ' ') ? (float)(int)(gap * 0.5f + 0.5f) : 0.0f;
    if (!punct_letter(prev)) return 0.0f;
    if (c == '!' || c == '?' || c == ';' || c == ':') return (float)(int)(gap + 0.5f);          // whole px : crisp
    if (c == '.' || c == ',') return (float)(int)(gap * 0.75f + 0.5f);
    return 0.0f;
}

class Font {
public:
    void  ensure(u32 dev);                  // build the default slot once (idempotent ; rebuilds if face changed)
    void  on_device_lost();                 // forget every slot's texture (rebuilt on next use)
    void  dispose();                        // release all slot textures
    bool  ready() const { return nslot_ > 0 && slot_[0].tex != 0; }
    void  set_face(const char* face, int weight, bool italic = false);   // change the baked GDI face/weight/italic (drops the cached slots)
    void  set_upper(bool u) { upper_ = u; }   // force UPPERCASE at draw/measure time (used for the config Interface element)
    bool  upper() const { return upper_; }    // ... and read it back, so a caller can set it for ONE string and restore

    void  begin(u32 dev);                   // set textured-quad render state (the texture is bound per draw, by size)
    // draw `s` with its CELL top-left at (x,y); `size` = em px; `color` = ARGB. Returns advance width.
    // An `outline` with alpha>0 paints a stroke of width `ow` px (8 offset passes) behind the glyphs.
    float draw(u32 dev, float x, float y, const char* s, float size, u32 color, u32 outline = 0, float ow = 0.0f);
    bool baked(float size) const { const int s = pickC(size); int em = (int)(size + 0.5f); if (em < 7) em = 7; if (em > 72) em = 72; return s >= 0 && slot_[s].em == em; }   // measure() is EXACT (not a neighbour's, scaled)
    // draw() with the reading spacing before punctuation (punct_pre) ; `prev` = the Latin-1 char before s (0 none)
    float draw_punct(u32 dev, float x, float y, const char* s, float size, u32 color, u32 outline, float ow, float gap, unsigned char prev);
    float draw_lv(u32 dev, float x, float cy, const char* s, float size, u32 color, u32 outline = 0, float ow = 0.0f);
    float draw_lc(u32 dev, float x, float cy, const char* s, float size, u32 color, u32 outline = 0, float ow = 0.0f);
    float draw_c(u32 dev, float cx, float cy, const char* s, float size, u32 color, u32 outline = 0, float ow = 0.0f);
    float draw_cc(u32 dev, float cx, float cy, const char* s, float size, u32 color, u32 outline = 0, float ow = 0.0f);
    // centred, but BAKE at `bakeSize` (kept constant -> one atlas) and RENDER at `dispSize` -> animate the on-screen
    // size (pop/pulse/slam) with NO per-frame atlas re-bake. Pass a stable bakeSize and vary dispSize freely.
    float draw_c_scaled(u32 dev, float cx, float cy, const char* s, float bakeSize, float dispSize, u32 color, u32 outline = 0, float ow = 0.0f);
    // every glyph drawn until clear_rotation() is ROTATED by `ang` (rad) about (cx, cy) -- text following a curve (the
    // minimap's wheels on an arc) ; static state, off by default ; always pair it with clear_rotation()
    static void set_rotation(float cx, float cy, float ang);
    static void clear_rotation();
    float measure(const char* s, float size) const;   // pixel width of `s` at `size`
    // the INK's vertical extent of `s` at `size`, from the cell top (what draw_lc centres on). A line drawn in several
    // coloured runs must use ONE reference for all of them -- draw_lc per run centres each on its own letters.
    void  ink_v(const char* s, float size, float& top, float& bot) const;

private:
    struct G { float u0, v0, u1, v1; float w, h, adv; float il, ir, it, ib; };  // il..ib = ink bbox (rel. cell top-left), atlas px
    struct Slot { u32 tex = 0; int em = 0; unsigned used = 0; float base = 0.0f, cap_top = 0.0f, cap_h = 0.0f, ascent = 0.0f; G g[224] = {}; };  // one atlas, baked at `em` px ; `used` = LRU stamp

    void build(u32 dev, Slot& s, int em);   // bake the glyphs at `em` px into slot s (its own right-sized atlas)
    int  pick(u32 dev, float size);         // slot index for `size` (build on demand) ; -1 on failure
    int  pickC(float size) const;           // nearest already-built slot (const) ; -1 if none built yet
    // pass 0 = the Latin glyphs (this slot's atlas), pass 1 = the WIDE ones (codepoints > 255 : the shared wide page)
    void emit(u32 dev, const Slot& S, float x, float y, const char* s, float scale, u32 color, float gap, unsigned char prev, int pass);
    float adv_of(const Slot& S, int c, float scale) const;   // one character's advance, Latin or wide
    // the whole string : stroke + fill for the Latin glyphs, then (when it has any) the same for the wide ones
    void passes(u32 dev, const Slot& S, float x, float y, const char* s, float scale, u32 color, u32 outline, float ow, float gap, unsigned char prev);

    // Distinct cached sizes per (face,weight). This was 16 with NO recycling, and the config page alone asks for
    // 13+ distinct pixel sizes from the SAME Font object the HUD draws with -- so opening //aio over a live HUD
    // exhausted the pool, and every size requested afterwards was silently rendered by STRETCHING the nearest
    // baked atlas. That is precisely the "no minification, crisp at any size" property this class exists for.
    // Dragging a text-size slider was the reliable way to see it: the transient sizes claimed the slots and the
    // size you settled on stayed blurry until the next zone (device loss) reshuffled which sizes won.
    // 32 slots + LRU recycling: the pool now yields to the sizes actually in use instead of to whoever asked first.
    static const int NSLOT = 32;
    Slot  slot_[NSLOT];
    int   nslot_ = 0;
    unsigned useClock_ = 0;                 // monotonic, bumped on every pick -> Slot::used (LRU victim = smallest)
    bool  dirty_ = false;                   // face/weight changed -> drop slots on next ensure
    char  face_[64] = "Segoe UI";           // GDI face name (configurable, global)
    int   weight_ = 600;                    // FW_SEMIBOLD
    bool  italic_ = false;                  // GDI italic flag
    bool  upper_ = false;                   // force uppercase (ASCII + Latin-1) at draw/measure
    int   upcase(int c) const { if (upper_) { if (c >= 'a' && c <= 'z') c -= 32; else if (c >= 0xE0 && c <= 0xFE && c != 0xF7) c -= 32; } return c; }
};

// Cache of Font atlases keyed by (face, weight). Lets different text use different faces
// and weights (bold) -- each unique (face,weight) combo gets its own Font, built lazily.
class FontManager {
public:
    void  set_default(const char* face, int weight);
    Font* get(const char* face, int weight, bool italic = false);   // face ""/null = default ; weight 0 = default
    void  ensure_all(u32 dev);
    void  on_device_lost();
    void  dispose();
private:
    static const int MAXF = 48;   // enough for cycling many faces x weights (a full pool falls back to f_[0])
    Font  f_[MAXF];
    char  face_[MAXF][64];
    int   wt_[MAXF];
    bool  it_[MAXF];
    int   n_ = 0;
    char  defFace_[64] = "Segoe UI";
    int   defWeight_ = 600;
};

// Cap how many NEW font-atlas sizes may bake THIS frame (amortises the startup hitch : the Hud sets a small
// budget each frame so ~10-15 first-use sizes spread over a few frames instead of freezing frame 1).
void font_set_bake_budget(int n);
// Discard a half-built glyph batch (see the note at the definition). Call it when a draw pass was ABORTED --
// i.e. from a SEH handler -- not in normal flow, where every Font::draw flushes its own batch.
void font_reset_batch();
// The WIDE glyphs (every codepoint past Latin-1 : Japanese from the chat, symbols) live in a few pages SHARED by every
// Font, baked one glyph at a time with a face that has them (Yu Gothic UI, else Meiryo UI, else MS UI Gothic).
// Device lost : forget the page textures (re-baked on use) ; dispose : release them and their GDI fonts.
void font_wide_forget();
void font_wide_dispose();

} // namespace aio
