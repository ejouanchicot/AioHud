// font.h -- a GDI-baked bitmap-font ATLAS, baked PER DISPLAY SIZE so text is pixel-perfect.
//
// Instead of one 32px atlas scaled down to every size (= blurry small text), each unique pixel
// size gets its OWN atlas, baked by GDI at exactly that size (hinted, antialiased). draw() then
// emits the glyphs 1:1 -> crisp at any size, no minification. Atlases are built lazily and cached
// (a small per-size "slot" pool). Codepoints 32..255 (ASCII + Latin-1 supplement -> French accents).
#pragma once
#include "d3d.h"

namespace aio {

class Font {
public:
    void  ensure(u32 dev);                  // build the default slot once (idempotent ; rebuilds if face changed)
    void  on_device_lost();                 // forget every slot's texture (rebuilt on next use)
    void  dispose();                        // release all slot textures
    bool  ready() const { return nslot_ > 0 && slot_[0].tex != 0; }
    void  set_face(const char* face, int weight, bool italic = false);   // change the baked GDI face/weight/italic (drops the cached slots)

    void  begin(u32 dev);                   // set textured-quad render state (the texture is bound per draw, by size)
    // draw `s` with its CELL top-left at (x,y); `size` = em px; `color` = ARGB. Returns advance width.
    // An `outline` with alpha>0 paints a stroke of width `ow` px (8 offset passes) behind the glyphs.
    float draw(u32 dev, float x, float y, const char* s, float size, u32 color, u32 outline = 0, float ow = 0.0f);
    float draw_lv(u32 dev, float x, float cy, const char* s, float size, u32 color, u32 outline = 0, float ow = 0.0f);
    float draw_lc(u32 dev, float x, float cy, const char* s, float size, u32 color, u32 outline = 0, float ow = 0.0f);
    float draw_c(u32 dev, float cx, float cy, const char* s, float size, u32 color, u32 outline = 0, float ow = 0.0f);
    float draw_cc(u32 dev, float cx, float cy, const char* s, float size, u32 color, u32 outline = 0, float ow = 0.0f);
    float measure(const char* s, float size) const;   // pixel width of `s` at `size`

private:
    struct G { float u0, v0, u1, v1; float w, h, adv; float il, ir, it, ib; };  // il..ib = ink bbox (rel. cell top-left), atlas px
    struct Slot { u32 tex = 0; int em = 0; float base = 0.0f, cap_top = 0.0f, cap_h = 0.0f; G g[224] = {}; };  // one atlas, baked at `em` px

    void build(u32 dev, Slot& s, int em);   // bake the glyphs at `em` px into slot s (its own right-sized atlas)
    int  pick(u32 dev, float size);         // slot index for `size` (build on demand) ; -1 on failure
    int  pickC(float size) const;           // nearest already-built slot (const) ; -1 if none built yet
    void emit(u32 dev, u32 tex, const G* g, float x, float y, const char* s, float scale, u32 color);

    static const int NSLOT = 16;            // distinct cached sizes per (face,weight)
    Slot  slot_[NSLOT];
    int   nslot_ = 0;
    bool  dirty_ = false;                   // face/weight changed -> drop slots on next ensure
    char  face_[64] = "Segoe UI";           // GDI face name (configurable, global)
    int   weight_ = 600;                    // FW_SEMIBOLD
    bool  italic_ = false;                  // GDI italic flag
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

} // namespace aio
