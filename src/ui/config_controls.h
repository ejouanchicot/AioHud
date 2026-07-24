// config_controls.h -- the SHARED immediate-mode config toolkit (palette + animation springs +
// AA drawing primitives + the labeled controls : selector / slider / toggle / category header).
//
// This is the reusable half of the old config_page.cpp monolith. config_page.cpp draws the page
// CHROME (tabs, sidebar, help) ; each MODULE's settings panel (target_config.cpp, ...) is drawn with
// THESE controls. Splitting it here means a new module brings its own *_config.cpp and never has to
// touch a 2500-line god-file -- and every control keeps its own ease() uid namespace.
//
// State model : one global frame clock (g_dt/g_t) + fade (g_fade) that the page sets each frame, and a
// small id->spring table behind ease(id, target). The accent palette (C_ACCENT family) is MUTABLE and
// rederived every frame by apply_ui_theme() from the chosen style+colour ; the graphite base is const.
#pragma once
#include "gfx/draw.h"   // snap() + the AA primitives
#include "ui/widget.h"   // MouseState, u32 (via gfx/d3d.h -> windower.h)
#include "gfx/font.h"    // Font

namespace aio {

// ---- graphite BASE palette (const : one copy per translation unit, never written) ----
const u32 C_DIMBG    = 0xCC05080A;
const u32 C_TABOFF_T = 0xC01A2228, C_TABOFF_B = 0xC012181D;
const u32 C_TABHOV_T = 0xD0233C39, C_TABHOV_B = 0xD0182D2B;
const u32 C_CONTENT_T= 0xD2161C22, C_CONTENT_B= 0xD20E1317;   // slightly translucent -> the nebula shows faintly behind the controls
const u32 C_SIDEBAR  = 0xF0171C22;
const u32 C_BORDER   = 0x2EFFFFFF, C_BORDERHI = 0x58FFFFFF;
const u32 C_TEXT     = 0xFFE7ECF0, C_DIM = 0xFF97A2AC, C_MUTE = 0xFF7E8894;
const u32 C_STROKE   = 0xFF000000, C_CLOSEHOV = 0xFFE0555F;
const u32 C_ONACC    = 0xFF08110E;   // dark text drawn ON a bright accent fill (chips / Save)
// preview gauges (party brief : HP green / MP blue / TP magenta) -- semantic, not themed
const u32 C_HP = 0xFF5ADC5A, C_HP_D = 0xFF148C2D, C_MP = 0xFF9597FF, C_MP_D = 0xFF3A3CE0, C_TP = 0xFFCD6EFF, C_TP_D = 0xFF5A0FBE;
const float PI_ = 3.14159265f;
const int   NUANCE_ROWS = 3;   // lightness rows per hue (light tint / base / deep shade)

// ---- MUTABLE accent family (single shared instance ; rederived each frame by apply_ui_theme) ----
extern u32 C_ACCENT, C_ACCENTHI;
extern u32 C_GOLD, C_GOLDHI, C_GOLD_DEEP;
extern u32 C_CTL_T, C_CTL_B, C_CTL_BR, C_ARROW;
extern u32 C_TABON_T, C_TABON_B;
extern u32 C_ROWON_T, C_ROWON_B;
extern u32 C_CHIP_ON_T, C_CHIP_ON_B;

// ---- frame clock + global fade (the config page writes these once per frame, before drawing) ----
extern float g_fade;   // open-animation fade, applied to every quad/text alpha via fa()
extern float g_dt;     // frame delta (seconds) -- drives ease()
extern float g_t;      // wrapping seconds -- drives the hover shine sweep

// ---- colour STYLES (the theme picker walks these ; apply_ui_theme derives the accent family) ----
struct ThemeStyle { const char* en; const char* fr; const u32* col; int n; };
extern const ThemeStyle STYLES[];
extern const int STYLE_N;

// ---- inline trivials (pure ; header-defined so every TU inlines them) ----
// snap() now lives in gfx/draw.h (one definition ; it was duplicated in seven TUs).
inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
inline bool  inrect(const MouseState* m, float x, float y, float w, float h) {
    return m && m->x >= x && m->x < x + w && m->y >= y && m->y < y + h;
}

// ---- language + theme helpers ----
const char* tr(const char* en, const char* fr);
u32  shade(u32 c, float f, u32 alpha = 0xFF);   // brighten (f>0 -> white) / darken (f<0 -> black), keep alpha
u32  nuance(u32 base, int row);                 // one hue's lightness ramp (0=tint,1=base,2=shade)
int  style_swatch_count(int style);
u32  theme_accent(int style, int color);
void apply_ui_theme(int style, int color);      // rederive the C_ACCENT family from style+colour

// ---- textured quad + colour helpers ----
u32  fa(u32 c);                       // scale a colour's alpha by the global fade
u32  lerpc(u32 a, u32 b, float t);    // linear blend of two ARGB colours

// ---- animation springs + entrance stagger ----
float ease(int id, int sub, float target, float speed = 18.0f);   // one 0..1 spring per (control id, sub-slot)
float ease(int id, float target, float speed = 18.0f);            // legacy : sub 0
float stagger(float anim, int i);                                 // staggered row entrance factor

// ---- CONTROL IDENTITY -----------------------------------------------------------------------------------------
// CTRL_ID : a stable, globally-UNIQUE id derived from the SOURCE LOCATION (file + line). Pass it as a control's
// `uid` and NEVER hand-pick a number again -- two controls can't collide because no two live on the same file:line,
// and each control keys its springs on (CTRL_ID, sub) so even a control's own multiple slots stay disjoint.
constexpr unsigned ctrl_fnv(const char* s, unsigned h = 2166136261u) { return *s ? ctrl_fnv(s + 1, (h ^ (unsigned char)*s) * 16777619u) : h; }
// mix the file hash with the line in a helper (params, not two literals -> no C4307 constant-overflow warning).
constexpr int ctrl_uid(const char* file, unsigned line) {
    unsigned h = ctrl_fnv(file);
    return (int)(h ^ (line + 0x9E3779B9u + (h << 6) + (h >> 2)));
}
#define CTRL_ID (::aio::ctrl_uid(__FILE__, (unsigned)(__LINE__)))
// For a control drawn inside a LOOP (same file:line, many items), derive a distinct uid per item : avalanche-mix
// the base (CTRL_ID) with the index so results spread across the hash space. A plain `CTRL_ID + i` offset would
// land on a neighbouring control's id and cross-wire their hover/latch state -- this does not.
constexpr int ctrl_uid_i(int base, int i) {
    unsigned h = (unsigned)base;
    return (int)(h ^ ((unsigned)i + 0x9E3779B9u + (h << 6) + (h >> 2)));
}

// ---- D3D colour-quad state + AA primitives ----
void cs(u32 dev);        // normal alpha colour-quad state
void cs_add(u32 dev);    // ADDITIVE colour state (glow / bloom / shine)
void q4(u32 dev, float x, float y, float w, float h, u32 tl, u32 tr, u32 bl, u32 br);
void flat(u32 dev, float x, float y, float w, float h, u32 c);
void vg(u32 dev, float x, float y, float w, float h, u32 t, u32 b);
void outline(u32 dev, float x, float y, float w, float h, u32 c);
void shadow_down(u32 dev, float x, float y, float w, float h, u32 top);
void halo(u32 dev, float x, float y, float w, float h, u32 col, float t);
void halo_rect(u32 dev, float x, float y, float w, float h, u32 col, float t);
void shine(u32 dev, float x, float y, float w, float h, float amt, float tsec);
void chevron(u32 dev, float cx, float cy, float s, int dir, u32 col);
void row_band(u32 dev, float x, float y, float w, float h, bool alt, float hov);
void clip_rect_begin(u32 dev, float x, float y, float w, float h);   // stencil scissor (viewport clip)
void clip_rect_end(u32 dev);
void chrome_text(u32 dev, Font* fo, float x, float y, const char* s, float size, float w,
                 u32 top, u32 bot, float bandTop, float bandBot);
void gem(u32 dev, float cx, float cy, float r, u32 col);
void qfan(u32 dev, float cx, float cy, float r, float a0, float a1, u32 col);
void rrect_fill(u32 dev, float x, float y, float w, float h, float r, u32 top, u32 bot);
void rrect_top(u32 dev, float x, float y, float w, float h, float r, u32 top, u32 bot);
void rpanel(u32 dev, float x, float y, float w, float h, float r, u32 top, u32 bot, u32 border, float bt);
void drop_shadow(u32 dev, float x, float y, float w, float h, float spread, u32 alpha);
float badge(u32 dev, Font* fo, float x, float cy, const char* text, u32 accent);

// ---- labeled controls (each keeps its own ease() uid namespace ; see the notes in the .cpp) ----
bool arrow_btn(u32 dev, Font* fo, const MouseState* mo, bool click, int uid,
               float x, float y, float s, const char* glyph);
int  row_selector(u32 dev, Font* fo, const MouseState* mo, bool click, int uid,
                  float x, float y, float w, const char* label, const char* value);
int  wrap(int v, int n);
bool row_slider(u32 dev, Font* fo, const MouseState* mo, int id,
                float x, float y, float w, const char* label, const char* valueText, float* v01);
// Higher-level rows shared by every module panel (replace the per-panel *_TOGGLE / *_PCT_SLIDER macros). Call each
// inside a ROW_BAND(48) / ROW_BAND(46) block respectively, passing y = ry + yo.
bool row_toggle(u32 dev, Font* fo, const MouseState* mo, bool click, int uid,
                float coX, float y, float ctrlW, const char* label, int* field);        // On/Off toggle ; persists on change
bool row_pct_slider(u32 dev, Font* fo, const MouseState* mo, int uid,
                    float coX, float y, float ctrlW, const char* label, float* field, float lo, float hi, float step = 0.05f);   // NN% slider ; persists on RELEASE (no per-frame save)
// HSV colour picker : an SV square + a slim vertical hue strip + a live swatch (with hex) + a preset "nuancier"
// grid (replaces the R/G/B slider triples). Two draggable zones share the slider latch -> give a UNIQUE
// (uidSV, uidHue) pair. Edits *color in place (preserves its alpha byte) ; returns true the frames it changes.
// `fo` draws the hex readout (may be null to skip it). Occupies color_picker_height() vertically.
bool  color_picker(u32 dev, Font* fo, const MouseState* mo, int uidSV, int uidHue,
                   float x, float y, float w, u32* color);
float color_picker_height();
bool toggle_chip(u32 dev, Font* fo, const MouseState* mo, bool click, int uid,
                 float x, float y, float w, float h, const char* label, bool on);
bool push_btn(u32 dev, Font* fo, const MouseState* mo, bool click, int uid,
              float x, float y, float w, float h, const char* label, int tone);
void cat_panel(u32 dev, float x, float y, float w, float h);
bool cat_header(u32 dev, Font* fo, const MouseState* mo, bool click, int uid,
                float x, float y, float w, const char* label, bool open);

} // namespace aio
