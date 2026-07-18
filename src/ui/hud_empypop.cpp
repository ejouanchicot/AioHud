// hud_empypop.cpp -- EmpyPop box renderer : the pop items / key items needed to spawn an Abyssea empyrean NM.
// Port of the upstream boxes/empypop_box.lua + modules/empypop.lua (Empy Pop Tracker data, (c) 2020 Dean James
// (Xurion of Bismarck), BSD-3 -- see NOTICE-EmpyPop.txt). Reads party().empypop() (the model resolves the chain
// against memory at 2 Hz) -- never memory, never a poll from here.
//
// Shape : a vertical STACK of boxes -- title (NM name, centred) + one box per GLOBAL pop (its name, where it
// drops, then the indented sub-pop chain) + an optional collectable counter. A group is GREEN when you hold its
// global pop, RED when you do not ; an orange [n] flags copies sitting in the treasure pool ; every box goes
// green + the title gains "READY!" once every group is obtained.
//
// Two deliberate departures from the Lua, both forced by our chrome system :
//   1. The stack's OUTER frame is one draw_themed_box (epBox : the user's frame/transparency/theme, like every
//      other module) instead of per-box left/right/bottom borders. The upstream rule "only the LAST box draws a
//      bottom border" falls out of that for free : the container IS the outer border.
//   2. What survives per-box is the part that carries meaning : the shared 2px DIVIDER between adjacent boxes
//      (coloured by divcol(filled, prevFilled) exactly as upstream) and the green BACKGROUND of a done box.
//      allDone additionally strokes the container gold -- the upstream BR_GOLD frame.
#include "ui/hud.h"
#include "ui/hud_internal.h"
#include "model/ui_config.h"
#include "ui/text_style.h"
#include "ui/box_style.h"
#include "model/party_state.h"
#include "model/itemnames_gen.h"
#include "gfx/draw.h"
#include "gfx/d3d.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace aio {

// ============================ EMPYPOP box ============================
// per-element typography (NM title / pop name / "from <mob>" / collectable), like every other module.
static Font* ep_font(const Frame& f, int e) { return te_font(f, ui_config().epText[e]); }
static inline float ep_sz(int e, float base) { return te_sz(ui_config().epText[e], base); }
static inline float ep_ow(int e, float base) { return te_ow(ui_config().epText[e], base); }
static inline u32   ep_col(int e, u32 base)  { return te_col(ui_config().epText[e], base); }
static const char*  ep_up(int e, const char* s, char* buf, int cap) { return te_upper(ui_config().epText[e], s, buf, cap); }

// upstream palette (modules/empypop.lua + boxes/empypop_box.lua)
static const u32 C_NEED   = 0xFFFF5555u;  // {255, 85, 85}   a pop you still need
static const u32 C_HAVE   = 0xFF78F078u;  // {120,240,120}   obtained
static const u32 C_POOL   = 0xFFFFB43Cu;  // {255,180, 60}   copies in the treasure pool -> "[n]"
static const u32 C_DIM    = 0xFF969BAAu;  // {150,155,170}   the arrow / the (KI) tag
static const u32 C_POS    = 0xFFB9C3D7u;  // {185,195,215}   "from <mob> (position)"
static const u32 BG_GREEN = 0xEB204E2Au;  // {32,78,42,235}  a done box's background
static const u32 BR_GOLD  = 0xFFDEC88Cu;  // {222,200,140}   READY
static const u32 BR_GREEN = 0xFF6ED26Eu;  // {110,210,110}   a divider touching a done box
static const u32 BR_LAV   = 0xFF99BBFFu;  // theme.bgLine    the neutral divider

static inline u32 ep_mula(u32 c, float a) {   // scale a colour's alpha (the box's Transparency slider)
    if (a < 0.0f) a = 0.0f; if (a > 1.0f) a = 1.0f;
    return (c & 0x00FFFFFFu) | ((u32)(((c >> 24) & 0xFFu) * a) << 24);
}

// Title-case a res/ name in place of the upstream ucwords : names ship LOWERCASE ("dented Gigas shield") and the
// Lua title-cased them at DRAW time, not in the data. Same word rule as the Lua pattern (%a)([%w_']*) : a word
// starts after any char that is not alphanumeric / underscore / apostrophe (0x27).
static void ep_ucwords(const char* s, char* out, int cap) {
    if (cap <= 0) return;
    if (!s) { out[0] = 0; return; }
    bool start = true; int i = 0;
    for (; s[i] && i < cap - 1; ++i) {
        const char c = s[i];
        const bool word = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == (char)0x27;
        if (!word)                               { out[i] = c; start = true; continue; }
        if (start && c >= 'a' && c <= 'z')         out[i] = (char)(c - 32);
        else if (!start && c >= 'A' && c <= 'Z')   out[i] = (char)(c + 32);
        else                                       out[i] = c;
        start = false;
    }
    out[i] = 0;
}

// The chain flattened into DRAW lines : the measure pass sizes the box to the widest line (as upstream did),
// the draw pass replays them. Fixed capacity, one stack frame -> no per-frame heap.
struct EpLine {
    char          text[64];      // the main run : a title-cased pop name, or "from <mob> (position)"
    char          tag[8];        // "  [3]" (copies in the pool) or empty
    float         indent;        // px (depth * one indent step)
    u32           col;           // the main run's colour
    unsigned char el;            // EP_POP / EP_FROM / EP_COLL -> which typography this line uses
    unsigned char arrow;         // draw the dim "-> " prefix (sub-pops only)
    unsigned char ki;            // draw the dim "  (KI)" tag (a global pop that is a key item)
};

// The DEMO chain (chloris) : built ONCE from the model's sample builder. Pure data -> the preview / Help / edit
// placeholder never touch memory and render the same on a character who owns nothing.
static const EmpyPop& ep_sample_data() {
    static EmpyPop s; static bool built = false;
    if (!built) { ep_build_sample(s); built = true; }
    return s;
}

// Core renderer, extracted as a FREE function so the config PREVIEW and the Help sample reuse the EXACT same
// config-aware draw with no Hud instance. measureOnly -> report the box size at the caller's scale and draw
// nothing (the geometry is linear in S) : that is what makes the Help's scale-to-fit free.
void empypop_draw(const Frame& f, bool preview, float ovX, float ovY, float ovS, float screenW, float screenH,
                  bool measureOnly = false, float* outW = 0, float* outH = 0, bool sampleData = false) {
    const UiConfig& C = ui_config();
    const bool editing = C.editLayout && !preview;   // //aio edit -> place the box even with epShow off
    if (!C.epShow && !preview && !editing) return;
    u32 dev = f.dev;
    if (!f.font && !f.fonts) return;

    // Data source. The HELP page (sampleData) shows the chloris demo -- a stranger has no tracked NM. Everything
    // else -- the live box, the //aio edit placeholder AND the config-tab preview -- shows the REAL tracked NM
    // (party().empypop(), refreshed every frame from epTrack) so the preview is true WYSIWYG and follows the
    // "Follow NM" pick. Fall back to the demo only when nothing valid is tracked, so preview/edit always has
    // something to show/place. The live data is a once-per-frame snapshot, never a draw()-time memory read.
    const EmpyPop& live = party().empypop();
    const bool liveOk = live.valid && live.nGroups > 0;
    const bool useSample = sampleData || ((preview || editing) && !liveOk);
    const EmpyPop& E = useSample ? ep_sample_data() : live;
    if (!E.valid || E.nGroups == 0) return;
    const bool hasColl = E.hasColl && C.epColl != 0;
    const bool ad = E.allDone;

    float sscl = C.epScale; if (sscl < 0.5f) sscl = 0.5f; if (sscl > 2.0f) sscl = 2.0f;
    const float S = (ovS > 0.0f) ? ovS : (screenH / 1000.0f) * sscl;
    const float pad  = 6.0f * S;         // upstream BPAD
    const float divH = 2.0f * S;         // upstream LHB : the divider SHARED by two adjacent boxes
    const float ind  = 10.0f * S;        // one indent level (the upstream two-space indent)
    const u32   strk = 0xFF000000u, white = 0xFFEAF0FFu;

    Font* fT = ep_font(f, EP_TITLE); Font* fP = ep_font(f, EP_POP); Font* fF = ep_font(f, EP_FROM); Font* fC = ep_font(f, EP_COLL);
    const float zT = ep_sz(EP_TITLE, 15.0f) * S, zP = ep_sz(EP_POP, 13.0f) * S, zF = ep_sz(EP_FROM, 12.0f) * S, zC = ep_sz(EP_COLL, 13.0f) * S;
    const float oT = ep_ow(EP_TITLE, 1.2f) * S, oP = ep_ow(EP_POP, 1.2f) * S, oF = ep_ow(EP_FROM, 1.0f) * S, oC = ep_ow(EP_COLL, 1.2f) * S;
    const float lhP = zP + 4.0f * S, lhF = zF + 3.0f * S, lhC = zC + 4.0f * S, titleH = zT + 4.0f * S;   // upstream LH / SLH

    // ---- flatten : title + per-group lines (pop name, "from", then the sub-pop chain) + the collectable ----
    char title[64], nb[48];
    ep_ucwords(E.nmName ? E.nmName : "?", nb, 48);
    if (ad) sprintf(title, "%.40s  -  READY!", nb); else sprintf(title, "%.48s", nb);

    EpLine lines[EmpyPop::MAX_NODES * 2 + 1]; int nl = 0;
    const int LCAP = (int)(sizeof(lines) / sizeof(lines[0]));
    int gFirst[EmpyPop::MAX_GROUPS] = {0}, gCount[EmpyPop::MAX_GROUPS] = {0};   // each stacked box's line range
    const int ng = E.nGroups;
    for (int g = 0; g < ng; ++g) {
        const EmpyPopGroup& G = E.groups[g];
        gFirst[g] = nl;
        for (int k = 0; k < G.count && nl <= LCAP - 2; ++k) {
            const EmpyPopNode& n = E.nodes[G.first + k];
            EpLine& L = lines[nl++];                                 // the pop / sub-pop itself
            ep_ucwords(n.name ? n.name : "Unknown", L.text, sizeof(L.text));
            L.indent = n.depth * ind;
            L.col    = n.owned ? C_HAVE : C_NEED;
            L.el     = EP_POP;
            L.arrow  = (n.depth > 0) ? 1 : 0;                        // upstream : only a sub-pop gets the dim arrow
            L.ki     = (n.depth == 0 && n.isKI) ? 1 : 0;             // ...and only a GLOBAL pop shows "(KI)"
            if (n.pool) sprintf(L.tag, "  [%u]", (unsigned)n.pool); else L.tag[0] = 0;
            EpLine& R = lines[nl++];                                 // "from <mob> (position)"
            sprintf(R.text, "from %.55s", n.fromName ? n.fromName : "?");
            R.indent = (n.depth == 0) ? 0.0f : (n.depth + 1) * ind;  // upstream indents a sub-pop's source one deeper
            R.col = C_POS; R.el = EP_FROM; R.arrow = 0; R.ki = 0; R.tag[0] = 0;
        }
        gCount[g] = nl - gFirst[g];
    }
    int collLine = -1;
    if (hasColl && nl < LCAP) {
        collLine = nl;
        EpLine& L = lines[nl++];
        const char* cn = item_name(E.collId); ep_ucwords(cn ? cn : "?", nb, 48);
        sprintf(L.text, "%.36s:  %u/%u", nb, E.collCount, (unsigned)E.collTarget);
        L.indent = 0.0f; L.col = E.collDone ? C_HAVE : white; L.el = EP_COLL; L.arrow = 0; L.ki = 0;
        if (E.collPool) sprintf(L.tag, "  [%u]", (unsigned)E.collPool); else L.tag[0] = 0;
    }

    // ---- measure : the widest line drives the width (upstream measured every line too) ----
    #define EP_LFONT(L) ((L).el == EP_POP ? fP : ((L).el == EP_FROM ? fF : fC))
    #define EP_LSIZE(L) ((L).el == EP_POP ? zP : ((L).el == EP_FROM ? zF : zC))
    #define EP_LLH(L)   ((L).el == EP_POP ? lhP : ((L).el == EP_FROM ? lhF : lhC))
    char ub[80];
    float maxw = fT->measure(ep_up(EP_TITLE, title, ub, 80), zT);
    for (int i = 0; i < nl; ++i) {
        const EpLine& L = lines[i]; Font* lf = EP_LFONT(L); const float lz = EP_LSIZE(L);
        float w = L.indent + lf->measure(ep_up(L.el, L.text, ub, 80), lz);
        if (L.arrow)  w += lf->measure("-> ", lz);
        if (L.ki)     w += lf->measure("  (KI)", lz);
        if (L.tag[0]) w += lf->measure(L.tag, lz);
        if (w > maxw) maxw = w;
    }
    const float boxW = (maxw > 1.0f ? maxw : 180.0f * S) + 2.0f * pad;
    const float tboxH = titleH + 2.0f * pad;
    float boxH = tboxH;
    for (int g = 0; g < ng; ++g) { float h = 2.0f * pad; for (int i = 0; i < gCount[g]; ++i) h += EP_LLH(lines[gFirst[g] + i]); boxH += h; }
    if (collLine >= 0) boxH += 2.0f * pad + lhC;
    if (measureOnly) { if (outW) *outW = boxW; if (outH) *outH = boxH; return; }   // Help scale-to-fit : report dims, don't draw

    // ---- position (+ edit drag) : epX = the LEFT edge (anchorX 0, unlike the centred boxes), epY = the top ----
    float px, py;
    if (ovS > 0.0f) { px = snap((ovX - boxW * 0.5f)); py = snap((ovY - boxH * 0.5f)); }   // preview / Help : centred on the stage
    else            { px = snap(C.epX * screenW - boxW); py = snap(C.epY * screenH); }   // epX = the RIGHT edge : the box grows LEFT (and down) as content widens, so the top-RIGHT corner stays where it was dropped
    if (editing) { static EditBox g_epEdit; box_edit(f, g_epEdit, EDITBOX_EMPYPOP, px, py, boxW, boxH, ui_config().epScale, ui_config().epX, ui_config().epY, 2); }   // anchorX 2 = right edge -> box_edit stores epX as the right edge

    // ---- chrome : ONE themed container, then the per-box semantics on top ----
    draw_themed_box(dev, f.skin, px, py, boxW, boxH, ui_config().epBox, 1.0f, S);   // shared themed chrome (frame/transp/theme)
    // draw_themed_box (draw_window) leaves the TEX1 FVF + skin texture bound with COLOROP=MODULATE ; the coloured
    // rrect fills/dividers/stroke below push diffuse-only verts, so re-establish the colour-quad state HERE, AFTER
    // the chrome -- doing it before (the obvious spot) is dead : the themed box immediately clobbers it.
    dColorQuadState(dev);
    const float ca = ui_config().epBox.on ? ui_config().epBox.alpha : 1.0f;         // fills follow the box's transparency
    const float fi = 3.0f * S;                                                      // inset : stay INSIDE the container's rounded corners
    const float fx = px + fi, fw = boxW - 2.0f * fi;
    // A box's background : green once it is done (upstream boxcol) ; the container supplies the navy otherwise.
    // Clamped to the container's inset rect so a fill never squares off a rounded corner. Hard edges (r=0,
    // feather=0) : an interior slab, uniformly unfeathered -- no partial-feather black seam.
    #define EP_FILL(yy, hh, on) do { if (on) { float y0 = (yy), y1 = (yy) + (hh); const float lo = py + fi, hi = py + boxH - fi; \
        if (y0 < lo) y0 = lo; if (y1 > hi) y1 = hi; \
        if (y1 > y0) rrect(dev, fx, y0, fw, y1 - y0, 0.0f, ep_mula(BG_GREEN, ca), ep_mula(BG_GREEN, ca), 0.0f); } } while (0)
    // The divider SHARED by two adjacent boxes (upstream divcol : gold when READY, green if either side is done).
    #define EP_DIV(yy, filled, prev) do { const u32 dc = ad ? BR_GOLD : (((filled) || (prev)) ? BR_GREEN : BR_LAV); \
        rrect(dev, fx, (yy) - divH * 0.5f, fw, divH, 0.0f, ep_mula(dc, ca), ep_mula(dc, ca), 0.0f); } while (0)
    float cy = py;
    EP_FILL(cy, tboxH, ad);                                  // title box : green only when READY (upstream boxcol)
    cy += tboxH;
    bool prevOb = false;
    for (int g = 0; g < ng; ++g) {
        float h = 2.0f * pad; for (int i = 0; i < gCount[g]; ++i) h += EP_LLH(lines[gFirst[g] + i]);
        EP_FILL(cy, h, ad || E.groups[g].obtained != 0);
        EP_DIV(cy, E.groups[g].obtained != 0, prevOb);
        prevOb = E.groups[g].obtained != 0;
        cy += h;
    }
    if (collLine >= 0) {
        const float h = 2.0f * pad + lhC;
        EP_FILL(cy, h, ad || E.collDone);
        EP_DIV(cy, E.collDone, prevOb);
        cy += h;
    }
    if (ad) rrect_stroke(dev, px, py, boxW, boxH, 6.0f * S, BR_GOLD, 2.0f * S);   // READY -> the upstream gold frame

    // ---- text : title (centred) then each box's lines, segment by segment (arrow / name / (KI) / [pool]) ----
    fT->begin(dev);
    fT->draw_c(dev, px + boxW * 0.5f, py + pad + titleH * 0.5f, ep_up(EP_TITLE, title, ub, 80), zT,
               ep_col(EP_TITLE, ad ? C_HAVE : white), strk, oT);
    cy = py + tboxH;
    for (int g = 0; g < ng + (collLine >= 0 ? 1 : 0); ++g) {   // the groups, then (g == ng) the collectable box
        const int first = (g < ng) ? gFirst[g] : collLine, cnt = (g < ng) ? gCount[g] : 1;
        float ly = cy + pad;
        for (int i = 0; i < cnt; ++i) {
            const EpLine& L = lines[first + i]; Font* lf = EP_LFONT(L);
            const float lz = EP_LSIZE(L), lh = EP_LLH(L), ow = (L.el == EP_POP) ? oP : (L.el == EP_FROM ? oF : oC);
            const float ty = ly + lh * 0.5f;
            float tx = px + pad + L.indent;
            lf->begin(dev);
            if (L.arrow) { lf->draw_lc(dev, tx, ty, "-> ", lz, C_DIM, strk, ow); tx += lf->measure("-> ", lz); }
            const char* s = ep_up(L.el, L.text, ub, 80);
            lf->draw_lc(dev, tx, ty, s, lz, ep_col(L.el, L.col), strk, ow); tx += lf->measure(s, lz);
            if (L.ki)     { lf->draw_lc(dev, tx, ty, "  (KI)", lz, C_DIM, strk, ow); tx += lf->measure("  (KI)", lz); }
            if (L.tag[0])   lf->draw_lc(dev, tx, ty, L.tag, lz, C_POOL, strk, ow);
            ly += lh;
        }
        cy = ly + pad;
    }
    #undef EP_LFONT
    #undef EP_LSIZE
    #undef EP_LLH
    #undef EP_FILL
    #undef EP_DIV
}

// Live / edit path : the Hud draws the stack at its configured screen position.
void Hud::draw_empypop(const Frame& f, bool preview, float ovX, float ovY, float ovS) {
    empypop_draw(f, preview, ovX, ovY, ovS, (float)screenW_, (float)screenH_);
}

// Help sample : the chloris demo chain (sampleData=true), centred at (cx,cy) at scale `s`. The Help page has no
// "tracked NM" context, so it always showcases the demo, not whatever the player happens to follow.
void empypop_help_box(const Frame& f, float cx, float cy, float s) {
    empypop_draw(f, true, cx, cy, s, 0.0f, 0.0f, false, 0, 0, true);
}

// Help scale-to-fit : measure at scale 1 (linear in S), pick the largest scale that fits availW (capped at maxScale).
void empypop_help_fit(const Frame& f, float availW, float maxScale, float& outScale, float& outH) {
    float bw = 0.0f, bh = 0.0f;
    empypop_draw(f, true, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, true, &bw, &bh, true);
    float s = (bw > 1.0f) ? (availW / bw) : maxScale;
    if (s > maxScale) s = maxScale; if (s < 0.6f) s = 0.6f;
    outScale = s; outH = bh * s;
}

} // namespace aio
