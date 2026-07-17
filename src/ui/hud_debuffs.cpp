// hud_debuffs.cpp -- the current target debuffs as a Timers-style vertical list (mob-name header, then
// rows of icon / name / timer). The SAME data the Target box shows (party().target_debuffs), but DETACHED
// into its own placeable/edited box when C.dbShow is on (target.cpp then stops drawing its inline row).
#include "ui/hud.h"
#include "ui/hud_internal.h"
#include "model/ui_config.h"
#include "ui/text_style.h"
#include "ui/box_style.h"
#include "model/party_state.h"
#include "gfx/draw.h"
#include "gfx/d3d.h"
#include "model/buffs_gen.h"       // buff_status_name : status id -> English name
#include "gfx/texture.h"
#include "model/paths.h"
#include "model/gamestate.h"
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ui/buff_atlas.h"

namespace aio {

// ============================ DEBUFFS box (current target debuffs, detached) ============================
// per-element typography (own TextStyle : DB_HEADER mob name / DB_NAME debuff name / DB_TIMER countdown).
static Font* db_font(const Frame& f, int e) { return te_font(f, ui_config().dbText[e]); }
static inline float db_sz(int e, float base) { return te_sz(ui_config().dbText[e], base); }
static inline float db_ow(int e, float base) { return te_ow(ui_config().dbText[e], base); }
static inline u32   db_col(int e, u32 base)  { return te_col(ui_config().dbText[e], base); }

// Core renderer, extracted as a FREE function so the config PREVIEW and the Help sample reuse the EXACT same
// config-aware draw (icons / names, caster colours, unknown "???") with no Hud instance.
void debuffs_draw(const Frame& f, bool preview, float ovX, float ovY, float ovS, float screenW, float screenH,
                  u32 buffAtlas, bool measureOnly = false, float* outW = 0, float* outH = 0) {
    const UiConfig& C = ui_config();
    const bool editing = C.editLayout && !preview;
    if ((!C.dbShow || !C.tgtDebuffs) && !preview && !editing) return;   // detached only when Debuffs are ON + Standalone mode
    u32 dev = f.dev;
    if (!f.font && !f.fonts) return;

    // ---- gather rows (mob name + per-debuff icon / remaining seconds / who cast it) ----
    struct Row { int rem; int icon; const char* name; unsigned char self; };
    static Row rows[40]; int nd = 0; const char* mobName = "";
    if (preview || editing) {   // a static sample so the box renders (config preview + //aio edit) for any player
        static const struct { unsigned short id; int rem; unsigned char self; } SMP[6] = {
            { 134, 145, 1 },   // Dia       -- yours (gold, counting down)
            { 13,   90, 0 },   // Slow      -- other caster (white)
            { 3,     8, 1 },   // Poison    -- yours (gold)
            { 6,     4, 1 },   // Silence   -- yours, about to wear off (red)
            { 4,    -1, 0 },   // Paralysis -- unknown "???" (grey : exceeded / not learned)
            { 11,  300, 0 },   // Bind      -- other caster (white)
        };
        mobName = "Apademak";
        for (int i = 0; i < 6; ++i) { rows[nd].rem = SMP[i].rem; rows[nd].icon = SMP[i].id; rows[nd].name = buff_status_name(SMP[i].id); rows[nd].self = SMP[i].self; ++nd; }
    } else {
        if (!f.game) return;
        const GameState& g = *f.game;
        if (!g.target.valid || g.target.name[0] == 0) return;   // nothing targeted -> the box depops
        mobName = g.target.name;
        unsigned short ids[40]; int rem[40] = { 0 }; unsigned char self[40] = { 0 };
        int n = party().target_debuffs(g.target.id, ids, rem, self, 40);
        for (int i = 0; i < n && nd < 40; ++i) {
            if (ids[i] >= (BUFF_COLS * BUFF_ATLAS_ROWS)) continue;   // keep to mappable atlas cells
            rows[nd].rem = rem[i]; rows[nd].icon = ids[i]; rows[nd].name = buff_status_name(ids[i]); rows[nd].self = self[i]; ++nd;
        }
    }
    int cap = C.dbMax; if (cap < 1) cap = 1; if (cap > 32) cap = 32;
    if (nd > cap) nd = cap;
    if (nd == 0 && !editing) return;

    // ---- metrics (mirror the Timers box) ----
    float sscl = C.dbScale; if (sscl < 0.5f) sscl = 0.5f; if (sscl > 2.0f) sscl = 2.0f;
    const float S = (ovS > 0.0f) ? ovS : (screenH / 1000.0f) * sscl;
    const float pad = (C.dbBox.on ? 8.0f : 0.0f) * S, gap = 4.0f * S, icgap = 4.0f * S;
    const u32 strk = 0xFF000000u, gold = 0xFFF6C64Eu, goldWarn = 0xFFFF7A4Au, other = 0xFFD8DEE8u, grey = 0xFFAEB6C2u, orange = 0xFFEB9660u;
    Font* fH = db_font(f, DB_HEADER); Font* fN = db_font(f, DB_NAME); Font* fT = db_font(f, DB_TIMER);
    const float zH = db_sz(DB_HEADER, 13.0f) * S, zN = db_sz(DB_NAME, 13.0f) * S, zT = db_sz(DB_TIMER, 13.0f) * S;
    const float oH = db_ow(DB_HEADER, 1.0f) * S, oN = db_ow(DB_NAME, 1.0f) * S, oT = db_ow(DB_TIMER, 1.0f) * S;
    float iscl = C.dbIconScale; if (iscl < 0.5f) iscl = 0.5f; if (iscl > 2.0f) iscl = 2.0f;
    const float icon = 20.0f * iscl * S, rowH = icon + 3.0f * S, headH = zH + 5.0f * S;
    float rg = C.dbRowGap; if (rg < 0.6f) rg = 0.6f; if (rg > 3.0f) rg = 3.0f;
    const float rowPit = rowH * rg;   // per-row PITCH (config: row spacing) ; content stays centred in rowH
    const bool showHdr = (C.dbHeader != 0);
    const bool wantIcon = (C.dbDisp == TMDISP_ICON || C.dbDisp == TMDISP_BOTH);
    const bool wantName = (C.dbDisp == TMDISP_NAME || C.dbDisp == TMDISP_BOTH);
    const float au = (float)BUFF_CELL / (float)BUFF_ATLAS_W, av = (float)BUFF_CELL / (float)BUFF_ATLAS_H;
    const int   cells = BUFF_COLS * BUFF_ATLAS_ROWS;

    char tb[16];
    auto fmt = [&](int r) -> const char* {
        if (r < 0)        strcpy(tb, "???");                        // hit 0 / never learned but still on -> "???"
        else if (r >= 60) sprintf(tb, "%d:%02d", r / 60, r % 60);
        else              sprintf(tb, "%d", r);
        return tb;
    };
    char idb[8];
    auto rowNameStr = [&](const Row& R) -> const char* { if (R.name) return R.name; sprintf(idb, "#%d", R.icon); return idb; };

    // ---- column widths (measure) -> box size ----
    float nameW = 0.0f, timeW = 0.0f;
    for (int i = 0; i < nd; ++i) {
        const float tw = fT->measure(fmt(rows[i].rem), zT); if (tw > timeW) timeW = tw;
        if (wantName) { const float nw = fN->measure(rowNameStr(rows[i]), zN); if (nw > nameW) nameW = nw; }
    }
    const bool colIcon = wantIcon;
    float leftW = colIcon ? icon : 0.0f;
    if (nameW > 0.0f) leftW += (colIcon ? icgap : 0.0f) + nameW;
    if (leftW <= 0.0f) leftW = icon;   // display mode with neither column resolvable -> keep the icon slot
    float contentW = leftW + gap + timeW;
    if (showHdr) { const float hW = fH->measure(mobName, zH); if (hW > contentW) contentW = hW; }
    if (contentW < 46.0f * S) contentW = 46.0f * S;
    const float boxW = pad * 2.0f + contentW;
    const int   bodyRows = nd > 0 ? nd : 1;
    const float boxH = pad + (showHdr ? headH + gap : 0.0f) + bodyRows * rowPit + pad;
    if (measureOnly) { if (outW) *outW = boxW; if (outH) *outH = boxH; return; }   // Help scale-to-fit : report dims, no draw

    // ---- position (+ edit-mode drag). dbX = LEFT edge fraction, dbY = TOP (anchorX 0 : grows rightward/down). ----
    float px, py;
    if (ovS > 0.0f) { px = ovX - boxW * 0.5f; py = ovY - boxH * 0.5f; }   // preview : centre on the given point
    else            { px = C.dbX * screenW; py = C.dbY * screenH; }
    if (editing) { static EditBox g_dbEdit; box_edit(f, g_dbEdit, EDITBOX_DEBUFFS, px, py, boxW, boxH, ui_config().dbScale, ui_config().dbX, ui_config().dbY, 0); }

    // ---- box chrome (shared themed frame/transparency/theme) ----
    dColorQuadState(dev);
    draw_themed_box(dev, f.skin, px, py, boxW, boxH, C.dbBox, 1.0f, S);

    float cyTop = py + pad;
    if (showHdr) { fH->begin(dev); fH->draw_c(dev, px + boxW * 0.5f, cyTop + headH * 0.5f, mobName, zH, db_col(DB_HEADER, orange), strk, oH); cyTop += headH + gap; }
    const float cx = px + pad, tRight = px + boxW - pad;
    const bool tCustom = ui_config().dbText[DB_TIMER].colorOn;   // a custom timer colour overrides the caster/expiry tint
    float cyy = cyTop;
    for (int i = 0; i < nd; ++i) {
        const Row& R = rows[i]; const int r = R.rem;
        const bool haveIcon = (buffAtlas && R.icon >= 0 && R.icon < cells);
        bool drewIcon = false;
        if (colIcon && haveIcon) {
            const float u0 = (float)(R.icon % BUFF_COLS) * au, v0 = (float)(R.icon / BUFF_COLS) * av;
            draw_icon_cell(dev, buffAtlas, cx, cyy + (rowH - icon) * 0.5f, icon, icon, u0, u0 + au, v0, v0 + av);
            drewIcon = true;
        }
        if (wantName || (colIcon && !haveIcon)) {   // name : requested, OR a fallback when the wanted icon is missing
            const float nx = cx + (drewIcon ? icon + icgap : 0.0f);
            const u32 nc = (r < 0) ? grey : R.self ? gold : other;   // name follows the timer caster tint
            fN->begin(dev); fN->draw_lc(dev, nx, cyy + rowH * 0.5f, rowNameStr(R), zN, db_col(DB_NAME, nc), strk, oN);
        }
        // timer : "???" (grey) / MM:SS / seconds ; yours=gold (red about to wear off) / others=white / unknown=grey
        fmt(r);
        const u32 sem = (r < 0) ? grey : R.self ? ((r <= 5) ? goldWarn : gold) : other;
        const u32 tc = tCustom ? ui_config().dbText[DB_TIMER].color : db_col(DB_TIMER, sem);
        const float tw = fT->measure(tb, zT);
        fT->begin(dev); fT->draw_lc(dev, tRight - tw, cyy + rowH * 0.5f, tb, zT, tc, strk, oT);
        cyy += rowPit;
    }
}

// Live / edit path : the Hud draws the box at its configured screen position (lazy-loads its buff atlas -- the
// SAME shared atlas member the Timers box uses).
void Hud::draw_debuffs(const Frame& f, bool preview, float ovX, float ovY, float ovS) {
    if (!buffAtlasTried_) { buffAtlas_ = load_raw_texture(f.dev, buff_atlas_path(), BUFF_ATLAS_W, BUFF_ATLAS_H); buffAtlasTried_ = true; }
    debuffs_draw(f, preview, ovX, ovY, ovS, (float)screenW_, (float)screenH_, buffAtlas_);
}

// The Help sample owns its own copy of the buff-icon atlas (lazy) so it can draw debuff icons without a Hud.
static u32 debuffs_help_atlas(u32 dev) {
    static u32 tex = 0; static bool tried = false;
    if (!tried) { tex = load_raw_texture(dev, buff_atlas_path(), BUFF_ATLAS_W, BUFF_ATLAS_H); tried = true; }
    return tex;
}

// Help sample : the REAL Debuffs box in preview mode (config-aware), centred at (cx,cy) at scale s.
void debuffs_help_box(const Frame& f, float cx, float cy, float s) {
    debuffs_draw(f, true, cx, cy, s, 0.0f, 0.0f, debuffs_help_atlas(f.dev));
}

// Help scale-to-fit : measure at scale 1 (linear in S), pick the largest scale that fits availW (capped at maxScale).
void debuffs_help_fit(const Frame& f, float availW, float maxScale, float& outScale, float& outH) {
    float bw = 0.0f, bh = 0.0f;
    debuffs_draw(f, true, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, debuffs_help_atlas(f.dev), true, &bw, &bh);
    float s = (bw > 1.0f) ? (availW / bw) : maxScale;
    if (s > maxScale) s = maxScale; if (s < 0.6f) s = 0.6f;
    outScale = s; outH = bh * s;
}

} // namespace aio
