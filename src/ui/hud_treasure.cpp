// hud_treasure.cpp -- split out of hud.cpp (pure move). Treasure Pool box renderer.
#include "ui/hud.h"
#include "ui/hud_internal.h"
#include "model/ui_config.h"
#include "ui/text_style.h"
#include "ui/box_style.h"
#include "model/party_state.h"
#include "gfx/draw.h"
#include "gfx/d3d.h"
#include "model/paths.h"
#include "gfx/texture.h"
#include "model/itemnames_gen.h"
#include <ctime>
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>

namespace aio {

// ============================ TREASURE POOL box ============================
static const char* COFFER_PATH() { static char b[260]; if (!b[0]) plugin_path(b, 260, "assets\\icon_chest.raw"); return b; }
// draw a textured quad (the coffer icon) at (x,y,w,h) -- self-contained state, restores the tex binding.
static void tp_draw_icon(u32 dev, u32 tex, float x, float y, float w, float h) {
    dSetVS(dev, FVF_XYZRHW_DIFFUSE_TEX1);
    dSetRS(dev, D3DRS_ALPHABLENDENABLE, 1);
    dSetRS(dev, D3DRS_SRCBLEND, D3DBLEND_SRCALPHA); dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    dSetTex(dev, 0, tex);
    dSetTSS(dev, 0, D3DTSS_COLOROP, D3DTOP_MODULATE); dSetTSS(dev, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE); dSetTSS(dev, 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    dSetTSS(dev, 0, D3DTSS_ALPHAOP, D3DTOP_MODULATE); dSetTSS(dev, 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE); dSetTSS(dev, 0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    dSetTSS(dev, 0, D3DTSS_MINFILTER, D3DTEXF_LINEAR); dSetTSS(dev, 0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR); dSetTSS(dev, 0, D3DTSS_MIPFILTER, D3DTEXF_LINEAR);
    tquad(dev, x, y, w, h, 0.0f, 1.0f, 0.0f, 1.0f, 0xFFFFFFFFu, 0xFFFFFFFFu);
    dSetTex(dev, 0, 0);
}
// per-element typography for the Treasure Pool (Index / Name / Timer / Lotter), like the minimap/skillchains helpers.
static Font* tp_font(const Frame& f, int e) { return te_font(f, ui_config().tpText[e]); }
static inline float tp_sz(int e, float base) { return te_sz(ui_config().tpText[e], base); }
static inline float tp_ow(int e, float base) { return te_ow(ui_config().tpText[e], base); }
static inline u32   tp_col(int e, u32 base)  { return te_col(ui_config().tpText[e], base); }
static const char*  tp_up(int e, const char* s, char* buf, int cap) { return te_upper(ui_config().tpText[e], s, buf, cap); }

// The lottery pool : a coffer icon + one row per item = "idx  name  MM:SS  lotter: lot", the timer coloured
// (green > 3 min, red < 1 min, else orange). Built from the drop packets (party().treasure_slots()) ; a sample
// pool in preview / edit-layout. Placed via //aio edit (scX = horizontal centre, scY = top), like the other boxes.
// Core renderer, extracted as a FREE function so the config PREVIEW and the Help sample reuse the EXACT same
// config-aware draw with no Hud instance. `cofferTex` = the coffer icon (0 = none), provided by the caller.
void treasure_draw(const Frame& f, bool preview, float ovX, float ovY, float ovS, float screenW, float screenH,
                   u32 cofferTex, bool measureOnly = false, float* outW = 0, float* outH = 0) {
    const UiConfig& C = ui_config();
    if (!C.tpShow) return;
    const bool editing = C.editLayout && !preview;
    u32 dev = f.dev;
    if (!f.font && !f.fonts) return;

    struct TRow { char idx[4]; const char* name; char time[8]; u32 tcol; char loot[36]; };
    TRow rows[10]; int nrows = 0;
    const int maxN = (C.tpCount < 1) ? 1 : (C.tpCount > 10 ? 10 : C.tpCount);
    if (preview || editing) {
        static const struct { const char* n; const char* t; int tier; const char* loot; } SMP[10] = {
            {"Heavy Metal Plate","09:05",0,0}, {"Riftcinder","08:10",1,"PlayerName1: 900"}, {"Beitetsu","07:15",2,0},
            {"Riftborn Stone","06:20",0,"PlayerName2: 800"}, {"Pixie Hairpin +1","05:25",1,0}, {"Ginganauts Pole","04:30",2,"PlayerName3: 700"},
            {"Crepuscular Knife","03:35",0,0}, {"Mariselle's Pole","02:40",1,"PlayerName1: 600"}, {"Defiant Scarf","01:45",2,0}, {"Ababinili","00:50",0,"PlayerName2: 500"} };
        static const u32 TC[3] = { 0xFF6BE06Bu, 0xFFF0A030u, 0xFFF06060u };
        for (int i = 0; i < 10 && nrows < maxN; ++i) { TRow& r = rows[nrows]; sprintf(r.idx, "%d", nrows + 1); r.name = SMP[i].n;
            int c = 0; for (; SMP[i].t[c]; ++c) r.time[c] = SMP[i].t[c]; r.time[c] = 0; r.tcol = TC[SMP[i].tier];
            if (SMP[i].loot) { c = 0; for (; SMP[i].loot[c] && c < 35; ++c) r.loot[c] = SMP[i].loot[c]; r.loot[c] = 0; } else r.loot[0] = 0; ++nrows; }
    } else {
        const TreasureItem* pool = party().treasure_slots();
        const unsigned now = (unsigned)time(0);
        int order[10], no = 0;
        for (int i = 0; i < 10; ++i) if (pool[i].itemId && pool[i].expireUnix > now) order[no++] = i;
        for (int a = 1; a < no; ++a) { int t = order[a], b = a - 1; while (b >= 0 && pool[order[b]].timestamp > pool[t].timestamp) { order[b + 1] = order[b]; --b; } order[b + 1] = t; }
        for (int k = 0; k < no && nrows < maxN; ++k) {
            const TreasureItem& it = pool[order[k]]; TRow& r = rows[nrows];
            sprintf(r.idx, "%d", nrows + 1);
            const char* nm = item_name(it.itemId); r.name = nm ? nm : "?";
            const unsigned rem = it.expireUnix - now;
            sprintf(r.time, "%02u:%02u", (rem / 60) % 100, rem % 60);
            r.tcol = (rem < 60) ? 0xFFF06060u : (rem > 180 ? 0xFF6BE06Bu : 0xFFF0A030u);
            if (it.lot > 0 && it.lotter[0]) sprintf(r.loot, "%.20s: %u", it.lotter, it.lot); else r.loot[0] = 0;
            ++nrows;
        }
    }
    if (nrows == 0) return;

    float sscl = C.tpScale; if (sscl < 0.5f) sscl = 0.5f; if (sscl > 2.0f) sscl = 2.0f;
    const float S = (ovS > 0.0f) ? ovS : (screenH / 1000.0f) * sscl;
    const float pad = (ui_config().tpBox.on ? 8.0f : 0.0f) * S, gap = 10.0f * S;   // no box chrome -> hug the content
    const float iconW = C.tpIcon ? 34.0f * S : 0.0f, iconH = 36.0f * S, iconGap = C.tpIcon ? 8.0f * S : 0.0f;
    const u32 white = 0xFFEAF0FFu, dim = 0xFFC8D2E6u, cyan = 0xFF7AD8F0u, gold = 0xFFE8C55Au, strk = 0xFF000000u;
    // per-element fonts / sizes / outlines / colours
    Font* fIdx = tp_font(f, TP_IDX); Font* fName = tp_font(f, TP_NAME); Font* fTime = tp_font(f, TP_TIMER); Font* fLoot = tp_font(f, TP_LOOT);
    const float zIdx = tp_sz(TP_IDX, 14.0f) * S, zName = tp_sz(TP_NAME, 14.0f) * S, zTime = tp_sz(TP_TIMER, 14.0f) * S, zLoot = tp_sz(TP_LOOT, 14.0f) * S;
    const float oIdx = tp_ow(TP_IDX, 1.2f) * S, oName = tp_ow(TP_NAME, 1.2f) * S, oTime = tp_ow(TP_TIMER, 1.2f) * S, oLoot = tp_ow(TP_LOOT, 1.2f) * S;
    const u32 idxCol = tp_col(TP_IDX, dim), nameCol = tp_col(TP_NAME, white), lootCol = tp_col(TP_LOOT, cyan);
    float rowH = zIdx; if (zName > rowH) rowH = zName; if (zTime > rowH) rowH = zTime; if (zLoot > rowH) rowH = zLoot;
    rowH += 6.0f * S;   // line height follows the biggest element

    // ---- measure columns (each with its own font/size + CAPS) ----
    char nb[48], ib[8], lb[40];
    float wIdx = 0, wName = 0, wTime = fTime->measure("00:00", zTime), wLoot = 0;
    for (int i = 0; i < nrows; ++i) {
        float a = fIdx->measure(tp_up(TP_IDX, rows[i].idx, ib, 8), zIdx);   if (a > wIdx) wIdx = a;
        float b = fName->measure(tp_up(TP_NAME, rows[i].name, nb, 48), zName); if (b > wName) wName = b;
        if (rows[i].loot[0]) { float d = fLoot->measure(tp_up(TP_LOOT, rows[i].loot, lb, 40), zLoot); if (d > wLoot) wLoot = d; }
    }
    const float contentW = iconW + iconGap + wIdx + gap + wName + gap + wTime + (wLoot > 0 ? gap + wLoot : 0);
    const float boxW = contentW + 2.0f * pad;
    const float contentH = nrows * rowH;
    const float boxH = (contentH > iconH ? contentH : iconH) + 2.0f * pad;
    if (measureOnly) { if (outW) *outW = boxW; if (outH) *outH = boxH; return; }   // Help scale-to-fit : report dims, don't draw

    // ---- position (+ edit drag) : tpX = horizontal centre, tpY = top ----
    float px, py;
    if (ovS > 0.0f) { px = snap((ovX - boxW * 0.5f)); py = snap((ovY - boxH * 0.5f)); }
    else            { px = snap(C.tpX * screenW - boxW * 0.5f); py = snap(C.tpY * screenH); }
    if (editing) { static EditBox g_tpEdit; box_edit(f, g_tpEdit, EDITBOX_TREASURE, px, py, boxW, boxH, ui_config().tpScale, ui_config().tpX, ui_config().tpY, 1); }

    // ---- box chrome ----
    dColorQuadState(dev);
    const float r = 6.0f * S;
    draw_themed_box(dev, f.skin, px, py, boxW, boxH, ui_config().tpBox, 1.0f, S);   // shared themed chrome (frame/transp/theme)

    // ---- coffer icon (left, vertically centred) ----
    if (C.tpIcon && cofferTex) tp_draw_icon(dev, cofferTex, px + pad, py + (boxH - iconH) * 0.5f, iconW, iconH);

    // ---- rows (per-element font / size / outline / colour ; timer keeps its semantic colour unless Custom) ----
    const float x0 = px + pad + iconW + iconGap;
    const float xIdx = x0 + wIdx, xName = x0 + wIdx + gap, xTime = xName + wName + gap, xLoot = xTime + wTime + gap;
    float cy = py + pad + rowH * 0.5f;
    for (int i = 0; i < nrows; ++i) {
        const char* si = tp_up(TP_IDX, rows[i].idx, ib, 8);
        fIdx->begin(dev);  fIdx->draw_lc(dev, xIdx - fIdx->measure(si, zIdx), cy, si, zIdx, idxCol, strk, oIdx);   // idx right-aligned
        fName->begin(dev); fName->draw_lc(dev, xName, cy, tp_up(TP_NAME, rows[i].name, nb, 48), zName, nameCol, strk, oName);
        fTime->begin(dev); fTime->draw_lc(dev, xTime, cy, rows[i].time, zTime, tp_col(TP_TIMER, rows[i].tcol), strk, oTime);
        if (rows[i].loot[0]) { fLoot->begin(dev); fLoot->draw_lc(dev, xLoot, cy, tp_up(TP_LOOT, rows[i].loot, lb, 40), zLoot, lootCol, strk, oLoot); }
        cy += rowH;
    }
}

// Live / edit path : the Hud draws the pool at its configured screen position (lazy-loads its coffer icon).
void Hud::draw_treasure_pool(const Frame& f, bool preview, float ovX, float ovY, float ovS) {
    ensure_raw_tex_mip(f.dev, tpCoffer_, tpCoffer_r_, COFFER_PATH(), 128, 128);
    treasure_draw(f, preview, ovX, ovY, ovS, (float)screenW_, (float)screenH_, tpCoffer_);
}

// The Help sample owns its own copy of the coffer icon (lazy) so it can draw it without a Hud.
// File-scope, NOT function-local : Hud::render's dev-change block forgets the member handles, and a
// function-local static is unreachable from it. After a device recreate (zoning / alt-tab) the stale
// handle would go to SetTexture with its owning device destroyed. treasure_help_forget() clears it from that block.
static u32 g_tpHelpTex = 0; static TexRetry g_tpHelpRetry;
void treasure_help_forget() { g_tpHelpTex = 0; g_tpHelpRetry = TexRetry{}; }   // device recreate : drop the handle AND re-arm the bounded retry
static u32 treasure_help_coffer(u32 dev) {   // bounded retry (rule 10) -- was a one-shot latch that stranded the Help icon on a single miss
    return ensure_raw_tex_mip(dev, g_tpHelpTex, g_tpHelpRetry, COFFER_PATH(), 128, 128);
}

// Help sample : the REAL Treasure Pool box in preview mode (config-aware), centred at (cx,cy) at scale `s`.
void treasure_help_box(const Frame& f, float cx, float cy, float s) {
    treasure_draw(f, true, cx, cy, s, 0.0f, 0.0f, treasure_help_coffer(f.dev));
}

// Help scale-to-fit : measure at scale 1 (linear in S), pick the largest scale that fits availW (capped at maxScale).
void treasure_help_fit(const Frame& f, float availW, float maxScale, float& outScale, float& outH) {
    float bw = 0.0f, bh = 0.0f;
    treasure_draw(f, true, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, treasure_help_coffer(f.dev), true, &bw, &bh);
    float s = (bw > 1.0f) ? (availW / bw) : maxScale;
    if (s > maxScale) s = maxScale; if (s < 0.6f) s = 0.6f;
    outScale = s; outH = bh * s;
}

} // namespace aio
