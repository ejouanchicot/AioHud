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
// ---- ELEVATION. Four surfaces, and each one is a REAL step above the last. ---------------------------------
// Measured before this existed: page -> content 1.09:1, content -> card 1.12:1, card -> control 1.05:1. The
// whole interface lived inside EIGHT levels of luma, so nothing separated from anything, and the shadows that
// were supposed to do the separating turned out to draw ~1/255 at the edge (see drop_shadow). That is what
// "pas assez contraste" was: not timid values, an absent model.
//
// And the model was INVERTED where it did exist: a chip at rest was 1B2228 on a card of 1E262E -- a RAISED
// control painted DARKER than the surface holding it. It read as a hole, and no amount of shadow fixes a tone
// that says the opposite. Each step is now ~1.3:1, and up means lighter, all the way up.
const u32 C_CARD_T     = 0xF4232C35, C_CARD_B     = 0xF41A222A;   // a section card, on the content surface
const u32 C_CTL_IDLE_T = 0xFF323D49, C_CTL_IDLE_B = 0xFF262F39;   // a control, raised off the card
const u32 C_CTL_HOV_T  = 0xFF42505E, C_CTL_HOV_B  = 0xFF333E4A;   // ... under the pointer
const u32 C_TABOFF_T = 0xC0272F38, C_TABOFF_B = 0xC01B222A;
// The hovered tab LIFTS -- it does not change hue. These were a teal (0xD0233C39), which was a colour from no
// system : on a gold masthead over steel furniture with the user's own accent, a green wash under the pointer
// was the one thing on the page that answered to nothing. Same graphite, lifted -- onto the hover step of the
// elevation ramp above, so a hovered tab and a hovered chip are lifted by the same amount.
const u32 C_TABHOV_T = 0xD03B4753, C_TABHOV_B = 0xD02B353F;
const u32 C_CONTENT_T= 0xD20F161D, C_CONTENT_B= 0xD2090E13;   // slightly translucent -> the dimmed game screen shows faintly behind the controls ; DEEPER than before, to leave room above it for the card and control steps
const u32 C_SIDEBAR  = 0xF0171C22;
const u32 C_BORDER   = 0x2EFFFFFF, C_BORDERHI = 0x58FFFFFF;
const u32 C_TEXT     = 0xFFE7ECF0, C_DIM = 0xFF97A2AC, C_MUTE = 0xFF8A94A0;   // C_MUTE lifted a notch : on the (now lighter) card it measured 3.9:1, under the 4.5 AA floor. 4.6:1 now.
const u32 C_STROKE   = 0xFF000000, C_CLOSEHOV = 0xFFE0555F;
const u32 C_ONACC    = 0xFF08110E;   // dark text drawn ON a bright accent fill (chips / Save)
// preview gauges (party brief : HP green / MP blue / TP magenta) -- semantic, not themed
const u32 C_HP = 0xFF5ADC5A, C_HP_D = 0xFF148C2D, C_MP = 0xFF9597FF, C_MP_D = 0xFF3A3CE0, C_TP = 0xFFCD6EFF, C_TP_D = 0xFF5A0FBE;
const float PI_ = 3.14159265f;
const int   NUANCE_ROWS = 3;   // lightness rows per hue (light tint / base / deep shade)

// ---- MUTABLE accent family (single shared instance ; rederived each frame by apply_ui_theme) ----
extern u32 C_ACCENT, C_ACCENTHI;
extern u32 C_GOLD, C_GOLDHI, C_GOLD_DEEP;
// REAL metal, and deliberately NOT theme-derived -- unlike C_GOLD, which is an alias of C_ACCENT and is
// therefore whatever colour the user picked. The masthead carries a wordmark whose gold is baked into a
// texture and follows no theme, so the furniture around it cannot follow one either: on a steel-blue accent
// every "gold" hairline came out grey beside a gold logotype. These three are the logo art's own palette.
static const u32 C_METAL_HI   = 0xFFFFE9A8;   // polished top facet
static const u32 C_METAL      = 0xFFE3B44E;   // the body of the metal
static const u32 C_METAL_DEEP = 0xFF8A5F22;   // shadowed underside
// The SECOND metal. Two metals, two jobs, and the split is the whole point: gold says "this is the brand"
// or "this is selected", steel says "this is structure". When one colour said all three the page read as a
// skin -- the biggest field of gold on screen was the container's perimeter, which is the least important
// thing on it. Cool and slightly blue, so it sits with the dark UI instead of looking like dirty gold.
static const u32 C_STEEL_HI   = 0xFFD8DEE6;
static const u32 C_STEEL      = 0xFF8E9AA8;
static const u32 C_STEEL_DEEP = 0xFF454E5C;
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

// ---- the menu's TYPE SCALE. Six sizes, named, so a new control cannot invent a seventh by accident.
// The sizes were already nearly consistent -- what was missing was somewhere to look them up, which is how a
// 13.5 and a 12.5 appear next to a 13 and a 12 and nobody notices for a year. Use these, never a literal.
// A section header is 32 tall and every panel advances 42 past it (ROW_NEXT(42.0f)). Named because cat_panel
// has to know where a section's CONTENT starts -- the card spans the header, so anything meant for the rows
// alone must skip it.
#define CAT_HEADER_ADV snap(42.0f)
#define CAT_BAR_H      snap(34.0f)
// The card's height, in the ONE place that knows the arithmetic. CLOSED it is exactly the title bar -- a
// collapsed section is a bar and nothing else, so nothing sits under the title and the title is centred in
// what you see. OPEN it is the bar, plus the padding that separates a title from content, plus the content.
// The bar itself never changes size: what unfolds is the card under it.
inline float cat_card_h(float full, float a) { return CAT_BAR_H + (CAT_HEADER_ADV - CAT_BAR_H + full) * a; }
inline float ts_section() { return snap(13.5f); }   // a section header
inline float ts_label()   { return snap(15.0f); }   // a control's label -- the workhorse
inline float ts_value()   { return snap(14.0f); }   // the number or word a control reads out
inline float ts_chip()    { return snap(12.0f); }   // inside a chip or a small button
inline float ts_note()    { return snap(12.5f); }   // an explanatory line under a row
inline float ts_micro()   { return snap(10.5f); }   // a tile's name, a caption

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
// A label that stays legible ON a coloured fill : dark text on a bright fill, light text on a dark one, each
// with the outline that contrasts with it. Pass the fill's MIDPOINT for a gradient. Every button uses this --
// the accent is the user's colour, so a hard-coded label colour is only ever right for half the palette.
bool fill_is_bright(u32 fill);
u32  text_on_fill(u32 fill, u32* stroke = 0);

// ---- animation springs + entrance stagger ----
float ease(int id, int sub, float target, float speed = 18.0f);   // one 0..1 spring per (control id, sub-slot)
float ease(int id, float target, float speed = 18.0f);            // legacy : sub 0
// A SPRING, for the one thing ease() cannot express : direct manipulation. ease() is critically damped -- it
// approaches and stops, which is right for a hover wash or an on/off crossfade. Something the user is MOVING
// with their hand wants mass: it should overshoot a little and settle, because that is what tells the eye the
// thing has weight and that the movement is finished. Defaults are ~0.74 damping ratio -- one small overshoot,
// no wobble. Same (id, sub) slot table as ease(), so the two never fight over a value.
float spring(int id, int sub, float target, float stiffness = 220.0f, float damping = 22.0f);
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
void ctrl_release_drag();   // drop the slider/picker drag latch and persist -- call when the page stops being drawn
// The SAME latch, for a control that drags something other than a value (the buff-strip reorder). Sharing it is
// the point : a strip drag and a slider drag can then never both be live, which is exactly the bug the latch
// exists to prevent. `hot` = the pointer is over the grabbable thing.
bool ctrl_drag_begin(int id, const MouseState* mo, bool hot);   // true on the press that takes the latch
bool ctrl_drag_active(int id);                                  // is this id the one currently holding it ?
bool ctrl_drag_end(int id, const MouseState* mo);               // true on the frame the button comes up ; frees the latch
// Is ANY drag (slider / colour picker / strip reorder) holding the latch right now ? The scrolling viewport
// asks this before it nulls the mouse for rows outside its rect : a drag that wanders out of the column must
// keep receiving the pointer until the button actually comes up, or the latched row sees `mo == nullptr`,
// takes its release branch and drops the drag mid-gesture.
bool ctrl_drag_any();
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
// A REAL drop shadow : two feathered bands that hug the rounded silhouette and fall off OUTSIDE it, offset
// downward. `r` is the element's corner radius (the shadow has to have its shape) ; `alpha` is calibrated
// around 64 = a normal card.
void drop_shadow(u32 dev, float x, float y, float w, float h, float spread, u32 alpha, float r = 9.0f);
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
// The two-state chip ROW. row_choice carries the two labels ("Fused"/"Separate", "Standalone"/"In Player"...) ;
// row_toggle is its On/Off case. Same row, same geometry, one implementation -- the panels held ~60 hand-copies
// of it differing only in those two strings.
bool row_choice(u32 dev, Font* fo, const MouseState* mo, bool click, int uid,
                float coX, float y, float ctrlW, const char* label, int* field,
                const char* onText, const char* offText, float rowH = 38.0f, float chipW = 112.0f);
bool row_toggle(u32 dev, Font* fo, const MouseState* mo, bool click, int uid,                                  // On/Off toggle ; persists on change
                float coX, float y, float ctrlW, const char* label, int* field,
                float rowH = 38.0f, float chipW = 112.0f);   // geometry defaults to the standard row ; party/target/player pass 40/150 etc. so their taller rows stay pixel-identical
bool row_toggle(u32 dev, Font* fo, const MouseState* mo, bool click, int uid,                                  // bool* overload : a few config fields are bool, not int.
                float coX, float y, float ctrlW, const char* label, bool* field,                               // It DELEGATES to the int version -- no second copy of the drawing code.
                float rowH = 38.0f, float chipW = 112.0f);
bool row_pct_slider(u32 dev, Font* fo, const MouseState* mo, int uid,
                    float coX, float y, float ctrlW, const char* label, float* field, float lo, float hi, float step = 0.05f);   // NN% slider ; persists on RELEASE (no per-frame save)
// HSV colour picker -- ONE CARD : an SV square with a VERTICAL hue strip beside it, the live swatch (hex written
// ON it) and the FAVOURITES in the column to their right, and the NUANCIER underneath -- a 13x3 chart, one hue
// per column, tint/base/shade down the rows, sharing its grammar with the theme chart in the Interface panel.
// Two draggable zones share the slider latch -> give a UNIQUE (uidSV, uidHue) pair. Edits *color in place
// (preserves its alpha byte) ; returns true the frames it changes. `fo` draws the hex + labels (may be null).
// Occupies color_picker_height() vertically -- a CONSTANT, so saving a favourite never reflows the page.
bool  color_picker(u32 dev, Font* fo, const MouseState* mo, int uidSV, int uidHue,
                   float x, float y, float w, u32* color);
float color_picker_height();
bool toggle_chip(u32 dev, Font* fo, const MouseState* mo, bool click, int uid,
                 float x, float y, float w, float h, const char* label, bool on);
bool push_btn(u32 dev, Font* fo, const MouseState* mo, bool click, int uid,
              float x, float y, float w, float h, const char* label, int tone);
// ---- THE CONTROL SURFACE : the two pieces every raised control shares with the TABS. ----------------------
// The tabs were built first and ended up with a language of their own -- a steel edging drawn as the shape one
// size larger, and a lamp along the top edge. Buttons and chips had neither, so the page read as two families
// of control that happened to sit on the same screen. These are that language, hoisted, so there is ONE of
// each rather than a copy per control (which is how the tab crown and its hover ghost drifted apart before).
//
// ctl_edge is also the `metal_edge()` the chrome audit left as pending item 3: the four hand-written edgings
// (masthead, tabs, container, sidebar) were four spellings of one gesture, and could diverge silently.
//
// WHY AN EDGING AND NOT A BORDER. A stroke laid along a rounded rectangle cuts its corners (audit rule 8). The
// shape drawn one size LARGER and then covered by the fill follows every curve by construction, and what is
// left showing IS the edge.
// The edge's COLOUR. Two decisions in it, and they are different questions:
//   * WHICH colour -- a tint of the chosen accent, not a fixed grey. Steel was the right answer while the
//     accent only ever appeared on selections, but then every rim, every chip and every bar on the working
//     area stayed neutral whatever colour you picked, and the theme stopped at the furniture.
//   * WHICH DIRECTION -- an edge exists to SEPARATE, so it takes its contrast from the surface it borders:
//     LIGHTER on a dark fill, DARKER on a bright one. Same rule as the labels (text_on_fill), for the same
//     reason, and it is what keeps an edge visible on an accent-filled chip as well as on a graphite one.
// It lands half-way back toward the metal: the furniture is TINTED by the theme, not painted in it. A fully
// saturated rim on every control turns a settings page into a colour swatch.
// NOT for the masthead, the container rim or the sidebar divider: those sit against a BAKED gold logotype and
// have to hold whatever the accent is (chrome audit, rule 7). They stay steel.
// `from` is the colour the edge is a NUANCE OF -- the accent by default. A colour SWATCH passes its own
// colour instead: the rim of a red chip should be a light red, not a light accent, or a grid of swatches ends
// up wearing one borrowed colour around thirty-nine different ones.
u32  ctl_edge_tint(u32 fill, u32 from = 0);
void ctl_edge(u32 dev, float x, float y, float w, float h, float r, float strength, u32 fill = 0xFF101418, u32 from = 0);
// Same edging for a surface that WELDS to what is under it -- an OPEN section bar and its card, a tab and the
// body. Rounded on top, square at the feet, and no rim across the join: an edge drawn through a weld is a
// seam, and the whole point of the shape is that there is not one.
void ctl_edge_top(u32 dev, float x, float y, float w, float h, float r, float strength, u32 fill = 0xFF101418, u32 from = 0);
// The lamp : two ramps of light running down into the surface, a wide faint haze, and a filament that
// dissolves at both ends (gfx/draw.cpp hbar_soft -- one cosine window, so nothing ends before anything else). `k` is the only dial ; the ALLOY is the caller's: steel says "under the
// pointer", gold says "chosen".
void ctl_crown(u32 dev, float x, float y, float w, float h, float r, u32 hiRGB, u32 loRGB, float k);
// A NAVIGATION ROW in a left rail -- the module list on the Configuration tab, the index on the Help tab.
// ONE implementation, because there were two and they had already drifted: the module list drew a rounded
// fill and a rounded gold pill, the Help list drew a SQUARE fill and a flat 3px bar, at different alphas.
// Nobody sees them side by side, which is exactly how that happens and why it never got noticed.
// (Audit rule 13: the left rail is drawn by three tabs, so a change has to touch all of them. This is also
// pending item 1 of that audit -- the rails' duplication -- closed for the two that list things.)
//
// Same elevation grammar as every other raised control on the page: NOTHING at rest, because a list of
// destinations is a list and not fourteen buttons ; the control step with a steel edging and the lamp under
// the pointer ; the card step, gold, with a shadow and the "you are here" rail when it is the one selected.
// THE BOX-THEME GRID : the FFXI window skins (named chips) or a procedural family's hues (colour swatches).
// It returns the variant clicked, or -1. FOUR copies of it existed -- two in box_style.cpp and one each in
// player_config and target_config -- and they had already started to differ. Worse, the FFXI half laid itself
// out as ONE row of nine wide chips while the procedural half was a compact block of squares, so switching
// families restructured the whole section: the label moved, the block changed shape, and the chips were drawn
// in a style (a bare rpanel with an accent border) that no other control on the page still uses.
// Same grid, same metrics, same chip grammar in both -- only the contents differ, which is the only thing that
// should. Returns the height it occupies through `slotH` so the caller can advance its own cursor.
int theme_grid(u32 dev, Font* fo, const MouseState* mo, bool click, int uid,
               float coX, float ry, float ctrlW, int fam, int var, float& slotH);
void nav_row(u32 dev, Font* fo, float x, float y, float w, float h, const char* label, bool active, float t, float pulse);
void cat_panel(u32 dev, float x, float y, float w, float h);

// ---- COLLAPSIBLE SECTIONS THAT FOLD, in three calls. ----
// The pattern was written out five times in party_config.cpp and every module still to come would have copied
// it again -- which is exactly how five copies drift into five behaviours. It is here once instead.
//
//   const float a = cat_fold(CTRL_ID, isOpen);                       // eased + smoothstepped 0..1
//   if (a > 0.0f) cat_panel(dev, hdrX, ry, hdrW, CAT_HEADER_ADV + full * a);
//   if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, "Label", isOpen)) isOpen = !isOpen;
//   ROW_NEXT(42.0f)
//   if (a > 0.0f) {
//       const float top = ry;  cat_fold_clip(dev, hdrX, top, hdrW, full * a);
//       ... the section's rows, laid out in FULL ...
//       cat_fold_end(dev, ry, top, full, a);
//   }
//
// WHY IT WORKS AT ALL: the rows are laid out completely and only the DRAWING is clipped, so `full` -- the
// section's natural height -- is measured for free on every frame, including the frames where almost none of it
// is visible. You cannot reveal a height you have not measured and you cannot measure one you have not laid
// out; doing both in the same pass is the whole trick. cat_fold_end then puts ry back to the REVEALED height so
// everything below rides the fold instead of waiting for it.
//
// The caller owns `full` (one float per section, persisted across frames) and the open flag. The progress is
// keyed on the call site's CTRL_ID, so no extra state is needed for it.
float cat_fold(int uid, bool open);
void  cat_fold_clip(u32 dev, float x, float top, float w, float visH);
void  cat_fold_end(u32 dev, float& ry, float top, float& full, float a);
// (No summary parameter. One was added when the header was a caret and a word, to give a collapsed page
//  something to read ; once the header became a real title bar the bar itself carried that weight, and a
//  value crowded against the disclosure triangle was two things competing for the same end of the same
//  object. The emptiness it was answering is a LAYOUT problem, and a caption is not a layout.)
// `a` is the FOLD'S OWN PROGRESS -- the same number the caller passes to cat_card_h, from the same cat_fold.
// The bar needs it because two of its decisions are decisions ABOUT THE CARD: whether its feet are square (are
// they welded to anything?) and how far its tint and lamp have crossed over. Deriving those from a second,
// private spring inside the header only APPROXIMATED the fold -- and an approximation of an exponential decay
// spends a long tail near zero, which is exactly the delay: the card had finished collapsing while the bar was
// still drawing square feet. With the real number there is nothing to approximate and no threshold to tune.
bool cat_header(u32 dev, Font* fo, const MouseState* mo, bool click, int uid,
                float x, float y, float w, const char* label, bool open, float a);

} // namespace aio
