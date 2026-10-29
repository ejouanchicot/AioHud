// hud_geo.cpp -- the Geomancer content of the Pet box : the luopan's bubble (its effect icon, the Geo- name in its
// element's colour, the distance to it), the luopan's HP with the time its slope leaves it, the Indi- aura we carry,
// the Indi- we entrusted, MP and the GEO job abilities. Reads GameState::geo (model/geo_state.cpp, game_mem.cpp
// compute_geo) ; every element has its own on / off (UiConfig geo*). SPEC docs/design/geo-module-spec.md. The box's
// place, size and frame are the Pet module's (hud_pet.cpp calls draw_geo ; UiConfig pet* ;
// docs/design/pet-module-spec.md).
//
// The graphic look (pet-module-spec.md lot 2, the player's images 2026-10-29 : assets\geo\, scripts/gen_geo_icons.py) :
// the luopan drawn as its bubble, tinted with the element of the Geo- it carries, its HP as a ring round it and as a
// figure in it, inside the Cardinal Chant compass (the quarter of the bonus a spell would get now lit, a dot where the
// target stands, the four bonus icons outside), the compass ON TOP of the box with no box round it (UiConfig::geoLayout
// 0, the player's pick 2026-10-29 ; 1 : beside its text, inside the box ; 2 : the compass alone, no box) ; the auras
// with a halo of their element ; the job abilities as icons that fill from the bottom as their recast runs, or a grid
// of icon + name, or the names (UiConfig::geoJaStyle). All white images the code tints. Without the luopan image (an install that lacks assets\geo) the box keeps
// its text look. The effect icons are the buff atlas's cells (the Indi- status of the same name,
// scripts/gen_geo_spells.py), the shared texture the Timers and the BST moves draw from, never released here.
#include "ui/hud.h"
#include "ui/hud_internal.h"
#include "model/ui_config.h"
#include "ui/text_style.h"
#include "ui/box_style.h"     // draw_themed_box : the same chrome options as every module (frame, transparency, theme)
#include "ui/buff_atlas.h"    // the effect icons
#include "ui/pet_common.h"    // the Pet box's drag state and the drawing helpers shared with hud_bst.cpp
#include "ui/tex_retry.h"
#include "model/gamestate.h"
#include "model/geo_spells_gen.h"
#include "model/geo_state.h"  // geo_cardinal : the Cardinal Chant bonus
#include "model/paths.h"
#include "gfx/draw.h"
#include "gfx/d3d.h"
#include "gfx/texture.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace aio {

static Font* geo_font(const Frame& f, int e) { return te_font(f, ui_config().geoText[e]); }
static inline float geo_sz(int e, float base) { return te_sz(ui_config().geoText[e], base); }
static inline float geo_ow(int e, float base) { return te_ow(ui_config().geoText[e], base); }
static inline u32   geo_col(int e, u32 base)  { return te_col(ui_config().geoText[e], base); }

// an element's colour, res numbering (0 Fire 1 Ice 2 Wind 3 Earth 4 Lightning 5 Water 6 Light 7 Dark) : the same tints
// the BST box gives a move's element, so one element reads the same in both boxes
static u32 elem_col(int e) {
    static const u32 C[8] = { 0xFFFF7A5Cu, 0xFF9BE7FFu, 0xFF7CE08Au, 0xFFD8B25Au, 0xFFC79BFFu, 0xFF6FB6FFu, 0xFFFFF4C0u, 0xFF9C7BD9u };
    return (e >= 0 && e < 8) ? C[e] : 0xFFE8EEF2u;
}
// a time left : 1:45 from a minute up, 42s under, "--" while it is not known (the slope needs ~6 s ; SPEC §2)
static void fmt_left(int sec, char* out, int cap) {
    if (sec < 0)          _snprintf(out, cap, "--");
    else if (sec >= 3600) _snprintf(out, cap, "%dh%02d", sec / 3600, (sec / 60) % 60);
    else if (sec >= 60)   _snprintf(out, cap, "%d:%02d", sec / 60, sec % 60);
    else                  _snprintf(out, cap, "%ds", sec);
    out[cap - 1] = 0;
}

// The images of assets\geo\ (scripts/gen_geo_icons.py) : slot -> file and side
enum { GT_LUOPAN = 0, GT_RING, GT_CARD, GT_JA = GT_CARD + 4 };   // GT_CARD + CARD_N/E/S/W ; GT_JA + the order below
static const struct { const char* name; int side; } GEO_TEX[] = {   // as many as Hud::GEO_TEX_N (geo_tex checks)
    { "luopan", 128 }, { "compass_ring", 128 },
    { "cardinal_crit", 64 }, { "cardinal_mab", 64 }, { "cardinal_macc", 64 }, { "cardinal_mb", 64 },
    { "ja_full_circle", 64 }, { "ja_lasting_emanation", 64 }, { "ja_ecliptic_attrition", 64 }, { "ja_life_cycle", 64 },
    { "ja_blaze_of_glory", 64 }, { "ja_dematerialize", 64 }, { "ja_entrust", 64 }, { "ja_bolster", 64 },
};
// a job ability's icon slot (game_mem.cpp GEO_JAS : Lasting Emanation and Ecliptic Attrition share one entry, under 346)
static int ja_slot(unsigned short abil) {
    switch (abil) {
        case 345: return GT_JA + 0; case 346: return GT_JA + 1; case 347: return GT_JA + 2; case 349: return GT_JA + 3;
        case 350: return GT_JA + 4; case 351: return GT_JA + 5; case 386: return GT_JA + 6; case 343: return GT_JA + 7;
    }
    return -1;
}
// the compass ring's empty centre, a share of its side (printed by scripts/gen_geo_icons.py)
static const float RING_INNER = 0.763f;
static const float GEO_TAU = 6.2831853f;

u32 Hud::geo_tex(u32 dev, int slot) {
    static_assert(sizeof(GEO_TEX) / sizeof(GEO_TEX[0]) == GEO_TEX_N, "one GEO_TEX row per Hud::geoTex_ slot");
    if (slot < 0 || slot >= GEO_TEX_N) return 0;
    char rel[64]; _snprintf(rel, sizeof(rel), "assets\\geo\\%s.raw", GEO_TEX[slot].name); rel[sizeof(rel) - 1] = 0;
    return ensure_raw_tex(dev, geoTex_[slot], geoTexR_[slot], plugin_path_r(rel), GEO_TEX[slot].side, GEO_TEX[slot].side);
}

// the longest recast seen for each ability this session : the share of its recast left, for the fill (the game
// gives the time left, not the length, which merits and gear shorten). The sample of //aio edit and the preview
// learns nothing : half filled for each ability on recast
static float ja_recast_share(unsigned short abil, int sec, bool sample) {
    if (sample) return sec > 0 ? 0.5f : 0.0f;
    static unsigned short ids[GeoState::JA_MAX] = {}; static int longest[GeoState::JA_MAX] = {};
    int k = 0;
    while (k < GeoState::JA_MAX && ids[k] && ids[k] != abil) ++k;
    if (k == GeoState::JA_MAX) return sec > 0 ? 1.0f : 0.0f;
    ids[k] = abil;
    if (sec > longest[k]) longest[k] = sec;
    return (sec > 0 && longest[k] > 0) ? (float)sec / (float)longest[k] : 0.0f;
}

// the sample shown in //aio edit and in the config preview : a Geo-Fury for the party (a supportive bubble, so its
// distance shows), an Indi-Haste aura, an Indi-Fury entrusted to Kaories, Bolster up, a few abilities on recast, a mob
// east-north-east (Cardinal Chant : mostly Magic Attack, some Magic Crit.)
static void geo_sample(GeoState& g) {
    g = GeoState{};
    g.visible = true; g.hasLuopan = true; g.bubbleSpell = 809; g.luopanHpp = 72; g.bubbleLeftSec = 105; g.dist = 3.4f;
    g.auraSpell = 771; g.auraLeftSec = 212;
    g.entrustSpell = 779; strcpy(g.entrustTarget, "Kaories"); g.entrustLeftSec = 280;
    g.mp = 1450; g.mpp = 76;
    g.cardValid = true; g.cardDeg = 70.0f; g.cardRank = 4;
    static const struct { unsigned short abil; const char* label; int sec; bool active; } J[] = {
        {345, "FC", 0, false}, {346, "LE/EA", 252, false}, {349, "LC", 0, false}, {350, "BoG", 520, false},
        {351, "Demat", 0, false}, {386, "Entrust", 190, false}, {343, "Bolster", 4320, true} };
    for (const auto& j : J) { if (g.nja >= GeoState::JA_MAX) break; GeoJa& d = g.jas[g.nja++]; d.abil = j.abil; d.label = j.label; d.recastSec = j.sec; d.active = j.active; }
}

void Hud::draw_geo(const Frame& f, bool preview, float ovX, float ovY, float ovS) {
    const UiConfig& C = ui_config();
    if (!C.geoShow) return;
    const bool editing = C.editLayout && !preview;
    u32 dev = f.dev;
    if (!f.font && !f.fonts) return;

    GeoState g;
    if (preview || editing) geo_sample(g);
    else {
        if (!f.game) return;
        g = f.game->geo;
        if (!g.visible || (C.geoOnlyLuopan && !g.hasLuopan)) return;
    }

    float sscl = C.petScale; if (sscl < 0.5f) sscl = 0.5f; if (sscl > 2.0f) sscl = 2.0f;
    const float S = (ovS > 0.0f) ? ovS : (screenH_ / 1000.0f) * sscl;
    const float kIcon = (C.geoIconSz < 0.5f) ? 0.5f : (C.geoIconSz > 2.0f ? 2.0f : C.geoIconSz);

    // ---- the look : graphic while the luopan image is there (assets\geo\), else the text rows. Graphic, two layouts
    //      (UiConfig::geoLayout) : 0 the compass and its bubble ON TOP of the box, with no box round them, the text in
    //      the box below (the player's pick 2026-10-29, maquette v3 : the HP % in the bubble, no HP bar) ; 1 the
    //      compass beside its text column, inside the box ----
    const u32 luTex = geo_tex(dev, GT_LUOPAN);
    const bool graphic = luTex != 0;
    const u32 ringTex = (graphic && C.geoCompass) ? geo_tex(dev, GT_RING) : 0;
    const bool compass = ringTex != 0;
    // the abilities (UiConfig::geoJaStyle) : 0 a row of icons, 1 the grid with each icon before its name, 2 the names
    const bool jaIcons = graphic && C.geoJaStyle == 0 && geo_tex(dev, GT_JA) != 0;
    const bool jaGridIcons = graphic && C.geoJaStyle == 1;
    // geoLayout 2 : the compass (or the bubble) alone, no box -- its bubble and its HP, the player's ask 2026-10-29
    const bool bubbleOnly = graphic && C.geoLayout == 2;
    const bool stacked = graphic && (C.geoLayout == 0 || bubbleOnly);

    // ---- the layout, top to bottom : the luopan (stacked : the compass above the box, then a line with its name,
    //      time and distance, and one with the Cardinal Chant bonus ; side : the compass and its column ; text : the
    //      bubble row and the HP gauge), the aura, the Entrust, MP, the abilities (graphic : a row of icons ; text : a
    //      3-column grid). //aio edit places a FRAME : everything enabled present, so what is dragged never changes
    //      size (docs/architecture/layout-reserve-dont-measure.md) ; in play the box takes the size of what is there
    //      and keeps the frame's edge nearer its screen side, as the BST box does. ----
    const float pad = snap((C.petBox.on ? 8.0f : 2.0f) * S);
    const float lineH = snap(16.0f * S), iconSz = snap(18.0f * S * kIcon), gap = snap(5.0f * S), jaH = snap(14.0f * S);
    const float iconRowH = iconSz + snap(2.0f * S) > lineH ? iconSz + snap(2.0f * S) : lineH;
    const float ringD = snap((stacked ? 100.0f : 92.0f) * S * kIcon), cardSz = snap(15.0f * S * kIcon), cardOut = cardSz + snap(4.0f * S);
    const float bubD = compass ? snap(ringD * 0.56f) : snap(58.0f * S * kIcon);
    const float topSide = compass ? ringD + 2.0f * cardOut : bubD + snap(14.0f * S * kIcon);
    const float sideH = topSide > lineH * 5.0f ? topSide : lineH * 5.0f;   // side layout : the compass beside 5 lines
    const float boxW = snap((stacked ? 210.0f : 250.0f) * S);
    const float jaIsz = snap((stacked ? 20.0f : 22.0f) * S * kIcon), jaRowH = jaIsz + snap(13.0f * S);
    const float jaCellH = jaGridIcons ? snap(18.0f * S) : jaH;   // a grid row : taller with its icon
    static const int JA_COLS = 3;
    // the rows an element takes, present or reserved
    // bubble : the Geo-'s name (UiConfig::geoBubble) ; hp : the ring and the % ; time : the estimate from the HP slope
    // (UiConfig::geoTime, off by default : the player, 2026-10-29, it never settles while the luopan's HP keep going
    // back up) ; dist : the distance to it
    struct Rows { bool top, bubble, hp, time, dist, card, aura, entrust, mp; int jaRows; };
    auto rowsFor = [&](const GeoState& s, bool all) -> Rows {
        Rows r;
        r.bubble  = C.geoBubble  && (all || s.hasLuopan);
        r.hp      = C.geoHp      && (all || s.hasLuopan);
        r.time    = C.geoTime    && (all || s.hasLuopan);
        r.dist    = C.geoDist    && (all || (s.hasLuopan && s.dist >= 0.0f));
        r.top     = graphic && (compass || r.bubble || r.hp);
        r.card    = compass && C.geoCardText && (all || (s.cardValid && s.cardRank > 0));
        r.aura    = C.geoAura    && (all || s.auraSpell);
        r.entrust = C.geoEntrust && (all || s.entrustSpell);
        r.mp      = C.geoMp != 0;
        const int n = all ? 7 : s.nja;
        r.jaRows  = (C.geoJa && n > 0) ? (jaIcons ? 1 : (n + JA_COLS - 1) / JA_COLS) : 0;
        return r;
    };
    // the luopan's text lines in the stacked box : its name / time / distance, the Cardinal Chant bonus
    auto infoLines = [&](const Rows& r) -> int { return ((r.bubble || r.time || r.dist) ? 1 : 0) + (r.card ? 1 : 0); };
    // the box's height (stacked : without the compass above it)
    auto boxHFor = [&](const Rows& r) -> float {
        float h = 0.0f; int blocks = 0;
        const float luo = stacked ? lineH * (float)infoLines(r)
                        : graphic ? (r.top ? sideH : 0.0f) : (r.bubble ? iconRowH : 0.0f) + (r.hp ? lineH : 0.0f);
        const float ind = (r.aura ? iconRowH : 0.0f) + (r.entrust ? iconRowH : 0.0f);
        const float mp = r.mp ? lineH : 0.0f, ja = (jaIcons ? jaRowH : jaCellH) * (float)r.jaRows;
        const float parts[4] = { luo, ind, mp, ja };
        for (float p : parts) if (p > 0.0f) { h += p; ++blocks; }
        return blocks ? pad * 2.0f + h + gap * (float)(blocks - 1) : 0.0f;
    };
    const float topGap = snap(6.0f * S);
    // the whole height : the compass above the box when stacked
    auto heightFor = [&](const Rows& r) -> float {
        const float b = boxHFor(r);
        if (!stacked || !r.top) return b;
        if (bubbleOnly) return topSide;
        return topSide + (b > 0.0f ? topGap + b : 0.0f);
    };
    const Rows rows = rowsFor(g, false);
    const float frameH = heightFor(rowsFor(g, true));
    if (frameH <= 0.0f) return;
    const float totW = (stacked && topSide > boxW) ? topSide : boxW;
    const float totH = editing ? frameH : heightFor(rows);
    if (totH <= 0.0f) return;

    float px, py;
    if (ovS > 0.0f) { px = snap(ovX - totW * 0.5f); py = snap(ovY - totH * 0.5f); }
    else            { px = box_grow_x(screenW_, UiConfig::GB_PET, 0, C.petX, totW); py = snap(C.petY * screenH_); }
    if (editing) box_edit(f, pet_edit_box(), EDITBOX_PET, px, py, totW, totH, ui_config().petScale, ui_config().petX, ui_config().petY, C.boxGrow[UiConfig::GB_PET]);
    if (!editing && ovS <= 0.0f) {
        if (py + frameH * 0.5f > screenH_ * 0.5f) py = snap(py + frameH - totH);   // lower half : the frame's bottom
        box_on_screen(f, px, py, totW, totH);
    }

    // the box : under the compass when stacked (its own height, the frame's in //aio edit so what is dragged keeps its
    // size), the whole of it otherwise
    const bool top = stacked && rows.top;
    const float bx = snap(px + (totW - boxW) * 0.5f);
    const float by = top ? snap(py + topSide + topGap) : py;
    const float bh = top ? totH - topSide - topGap : totH;
    if (bh > 0.0f) draw_themed_box(dev, f.skin, bx, by, boxW, bh, C.petBox, 1.0f, S);   // shared themed chrome (Same-as-Party by default)

    Font* fn = geo_font(f, GEO_NAME); Font* fv = geo_font(f, GEO_VALUE);
    const float zn = geo_sz(GEO_NAME, 12.0f) * S, on = geo_ow(GEO_NAME, 1.0f) * S;
    const float zv = geo_sz(GEO_VALUE, 11.0f) * S, ov = geo_ow(GEO_VALUE, 1.0f) * S;
    const float x0 = bx + pad, xr = bx + boxW - pad;
    const u32 stex = buff_atlas_tex(dev);   // shared, never released here (buff_atlas.h) ; 0 while it loads -> a dot instead

    // an effect icon : its status's atlas cell, else a dot in the element's colour (the atlas not there yet, or a
    // status past its last cell) ; in the graphic look, on a halo of its element
    auto icon = [&](float x, float cy, const GeoSpellRow* s) {
        if (graphic && s) { dColorQuadState(dev); disc(dev, snap(x + iconSz * 0.5f), snap(cy), iconSz * 0.72f, (elem_col(s->element) & 0x00FFFFFFu) | 0x50000000u); }
        const int si = s ? (int)s->status : 0;
        if (stex && si > 0 && si < BUFF_COLS * BUFF_ATLAS_ROWS) {
            float au, av, u0, v0; buff_cell_uv(si, au, av, u0, v0);
            draw_icon_cell(dev, stex, x, snap(cy - iconSz * 0.5f), iconSz, iconSz, u0, u0 + au, v0, v0 + av);
        } else {
            dColorQuadState(dev);
            disc(dev, snap(x + iconSz * 0.5f), snap(cy), iconSz * 0.3f, s ? elem_col(s->element) : 0xFF8C94A0u);
        }
    };
    // one icon row : the icon, a name in its element's colour (cut to leave the right-hand text room), the right text
    auto iconRow = [&](float y, const GeoSpellRow* s, const char* fallback, const char* suffix, const char* right, u32 rightCol) {
        const float cy = y + iconRowH * 0.5f;
        icon(x0, cy, s);
        const float tx = x0 + iconSz + snap(6.0f * S);
        float rw = 0.0f;
        if (right && right[0]) { rw = fv->measure(right, zv) + snap(6.0f * S); fv->begin(dev); fv->draw_lc(dev, xr - fv->measure(right, zv), cy, right, zv, rightCol, 0xC0000000u, ov); }
        char full[48]; _snprintf(full, sizeof(full), "%s%s", s ? s->en : fallback, suffix ? suffix : ""); full[sizeof(full) - 1] = 0;
        // an ally's long name beside the time
        char nm[48]; pet_fit_cut(fn, full, zn, xr - rw - tx, nm, sizeof(nm));
        fn->begin(dev);
        fn->draw_lc(dev, tx, cy, nm, zn, geo_col(GEO_NAME, s ? elem_col(s->element) : 0xFFB8C0C8u), 0xC0000000u, on);
    };
    auto separator = [&](float y) {
        dColorQuadState(dev);
        rrect(dev, x0, snap(y - gap * 0.5f), boxW - 2.0f * pad, 1.0f, 0.0f, 0x30FFFFFFu, 0x30FFFFFFu, 0.0f);
    };

    const GeoSpellRow* bs = geo_spell(g.bubbleSpell);
    const u32 hc = g.luopanHpp > 50 ? 0xFF6BD07Au : (g.luopanHpp > 25 ? 0xFFE8C45Au : 0xFFFF6A5Au);
    CardinalBonus cb;
    const bool card = compass && g.cardValid;
    if (card) cb = geo_cardinal(g.cardDeg, g.cardRank, true);
    // the Cardinal Chant bonuses a -ra would get now, the larger first : "MAB +12", "Crit +4"
    char cardTxt[2][24] = { {0}, {0} };
    if (card && g.cardRank > 0) {
        static const char* LBL[4] = { "Crit", "MAB", "M.Acc", "MB" };
        int order[4] = { 0, 1, 2, 3 };
        for (int i = 0; i < 3; ++i) for (int j = i + 1; j < 4; ++j) if (cb.value[order[j]] > cb.value[order[i]]) { int t = order[i]; order[i] = order[j]; order[j] = t; }
        for (int i = 0; i < 2; ++i) if (cb.value[order[i]] > 0) { _snprintf(cardTxt[i], sizeof(cardTxt[i]), "%s +%d", LBL[order[i]], cb.value[order[i]]); cardTxt[i][sizeof(cardTxt[i]) - 1] = 0; }
    }

    // ---- the compass block centred on (cx, cy) : the lit quarter, the ring, the four bonus icons, the target's dot,
    //      the luopan's HP ring and its bubble ; stacked, a faint disc behind it (it sits on the game with no box) and
    //      the HP % in the bubble ----
    auto drawCompass = [&](float cx, float cy) {
        if (compass) {
            const float rIn = ringD * 0.5f * RING_INNER, rOut = ringD * 0.5f * 0.985f;
            dColorQuadState(dev);
            if (stacked) disc(dev, cx, cy, ringD * 0.5f, 0x960A0E16u);
            if (card) {   // each cardinal's quarter lit by its share (true North-east : half of each)
                for (int k = 0; k < 4; ++k) {
                    if (cb.share[k] <= 0.0f) continue;
                    const float a0 = -GEO_TAU * 0.25f + GEO_TAU * 0.25f * (float)k - GEO_TAU * 0.125f;
                    ring_sector(dev, cx, cy, rIn, rOut, a0, a0 + GEO_TAU * 0.25f, ((u32)(0x80 * cb.share[k]) << 24) | 0x00FFC864u);
                }
            }
            pet_tex_quad(dev, ringTex, cx - ringD * 0.5f, cy - ringD * 0.5f, ringD, ringD, 0.0f, 1.0f, 0xFFC8D2E1u);
            static const float DX[4] = { 0.0f, 1.0f, 0.0f, -1.0f }, DY[4] = { -1.0f, 0.0f, 1.0f, 0.0f };
            const float rc = ringD * 0.5f + cardOut * 0.5f;
            for (int k = 0; k < 4; ++k) {
                const u32 t = geo_tex(dev, GT_CARD + k);
                if (!t) continue;
                const bool lit = card && cb.share[k] > 0.0f;
                pet_tex_quad(dev, t, snap(cx + DX[k] * rc - cardSz * 0.5f), snap(cy + DY[k] * rc - cardSz * 0.5f), cardSz, cardSz, 0.0f, 1.0f, lit ? 0xFFFFC864u : 0x60FFFFFFu);
            }
            if (card) {   // where the target stands (0 North, clockwise : the screen's angle from the top)
                const float a = -GEO_TAU * 0.25f + g.cardDeg * (GEO_TAU / 360.0f), rr = (rIn + rOut) * 0.5f;
                dColorQuadState(dev);
                disc(dev, snap(cx + rr * cosf(a)), snap(cy + rr * sinf(a)), snap(3.5f * S * kIcon), 0xFFFF6A5Au);
            }
        } else if (stacked) {
            dColorQuadState(dev);
            disc(dev, cx, cy, topSide * 0.5f, 0x960A0E16u);
        }
        if (g.hasLuopan && rows.hp) {   // the HP ring, filling clockwise from the top
            const float hr = compass ? ringD * 0.5f * 0.70f : bubD * 0.5f + snap(4.0f * S * kIcon), ht = snap(3.5f * S * kIcon);
            dColorQuadState(dev);
            ring_sector(dev, cx, cy, hr - ht * 0.5f, hr + ht * 0.5f, 0.0f, GEO_TAU, 0xC0262C38u);
            if (g.luopanHpp > 0) ring_sector(dev, cx, cy, hr - ht * 0.5f, hr + ht * 0.5f, -GEO_TAU * 0.25f, -GEO_TAU * 0.25f + GEO_TAU * (float)g.luopanHpp / 100.0f, hc);
        }
        // the bubble : its element's colour while a luopan is out, a faint white one otherwise
        pet_tex_quad(dev, luTex, cx - bubD * 0.5f, cy - bubD * 0.5f, bubD, bubD, 0.0f, 1.0f, g.hasLuopan ? (bs ? elem_col(bs->element) : 0xFFE8EEF2u) : 0x40FFFFFFu);
        if (stacked && g.hasLuopan && rows.hp) {   // its HP % in the middle, white on a dark outline whatever the element
            char hb[8]; _snprintf(hb, sizeof(hb), "%d%%", g.luopanHpp); hb[sizeof(hb) - 1] = 0;
            fn->begin(dev);
            fn->draw_cc(dev, cx, cy, hb, geo_sz(GEO_NAME, 15.0f) * S * kIcon, 0xFFFFFFFFu, 0xE0000000u, geo_ow(GEO_NAME, 1.6f) * S);
        }
    };

    float y = by + pad;
    bool first = true;
    auto nextBlock = [&]() { if (!first) { y += gap; separator(y); } first = false; };

    if (top) {
        // ---- stacked : the compass above the box ; in the box, the luopan's line and the bonus line ----
        drawCompass(snap(px + totW * 0.5f), snap(py + topSide * 0.5f));
        if (bubbleOnly) return;   // no box : nothing below
        if (infoLines(rows) > 0) {
            nextBlock();
            if (rows.bubble || rows.time || rows.dist) {   // its name on the left ; its time, then its distance, on the right
                const float cy = y + lineH * 0.5f;
                float rx = xr;
                if (rows.dist && g.dist >= 0.0f) {
                    char db[16]; _snprintf(db, sizeof(db), "%.1fy", g.dist > 999.0f ? 999.0f : g.dist); db[sizeof(db) - 1] = 0;
                    rx -= fv->measure(db, zv);
                    fv->begin(dev); fv->draw_lc(dev, rx, cy, db, zv, g.outOfRange ? 0xFFFF6A5Au : geo_col(GEO_VALUE, 0xFFB8C0C8u), 0xC0000000u, ov);   // red : outside its radius
                    rx -= snap(10.0f * S);
                }
                if (g.hasLuopan && rows.time) {
                    char tb[16]; fmt_left(g.bubbleLeftSec, tb, sizeof(tb));
                    rx -= fv->measure(tb, zv);
                    fv->begin(dev); fv->draw_lc(dev, rx, cy, tb, zv, g.bubbleLeftSec >= 0 ? geo_col(GEO_VALUE, 0xFFFFF4D6u) : 0xFF8C94A0u, 0xC0000000u, ov);
                    rx -= snap(8.0f * S);
                }
                if (rows.bubble) {
                    const char* nm0 = g.hasLuopan ? (bs ? bs->en : "Luopan") : "No luopan";
                    char nm[48]; pet_fit_cut(fn, nm0, zn, rx - x0, nm, sizeof(nm));
                    fn->begin(dev);
                    fn->draw_lc(dev, x0, cy, nm, zn, g.hasLuopan ? geo_col(GEO_NAME, bs ? elem_col(bs->element) : 0xFFB8C0C8u) : 0xFF8C94A0u, 0xC0000000u, on);
                }
                y += lineH;
            }
            if (rows.card) {   // the two largest bonuses side by side
                const float cy = y + lineH * 0.5f;
                fv->begin(dev);
                float x = x0;
                for (int i = 0; i < 2; ++i) if (cardTxt[i][0]) x += fv->draw_lc(dev, x, cy, cardTxt[i], zv, 0xFFFFC864u, 0xC0000000u, ov) + snap(14.0f * S);
                y += lineH;
            }
        }
    } else if (graphic && rows.top) {
        // ---- side : on the left the compass, on the right its name, HP and time, distance, then the bonus ----
        nextBlock();
        drawCompass(snap(x0 + topSide * 0.5f), snap(y + sideH * 0.5f));
        const float tx = x0 + topSide + snap(8.0f * S), tw = xr - tx;
        float ly = snap(y + (sideH - lineH * 5.0f) * 0.5f);
        auto line = [&](Font* fo, float z, float o, const char* s, u32 col) {
            char cut[48]; pet_fit_cut(fo, s, z, tw, cut, sizeof(cut));
            fo->begin(dev); fo->draw_lc(dev, tx, ly + lineH * 0.5f, cut, z, col, 0xC0000000u, o); ly += lineH;
        };
        if (rows.bubble) line(fn, zn, on, bs ? bs->en : "Luopan", geo_col(GEO_NAME, bs ? elem_col(bs->element) : 0xFFB8C0C8u));
        else if (!g.hasLuopan) line(fv, zv, ov, "No luopan", 0xFF8C94A0u);
        if (g.hasLuopan && (rows.hp || rows.time)) {
            char tb[16]; fmt_left(g.bubbleLeftSec, tb, sizeof(tb));
            char hb[32];
            if (rows.hp && rows.time) _snprintf(hb, sizeof(hb), "%d%%   %s", g.luopanHpp, tb);
            else if (rows.hp)         _snprintf(hb, sizeof(hb), "%d%%", g.luopanHpp);
            else                      _snprintf(hb, sizeof(hb), "%s", tb);
            hb[sizeof(hb) - 1] = 0;
            line(fv, zv, ov, hb, geo_col(GEO_VALUE, 0xFFE8EEF2u));
        }
        if (rows.dist && g.dist >= 0.0f) {
            char db[16]; _snprintf(db, sizeof(db), "%.1fy", g.dist > 999.0f ? 999.0f : g.dist); db[sizeof(db) - 1] = 0;
            line(fv, zv, ov, db, g.outOfRange ? 0xFFFF6A5Au : geo_col(GEO_VALUE, 0xFFB8C0C8u));   // red : outside its radius
        }
        for (int i = 0; i < 2; ++i) if (cardTxt[i][0]) line(fn, zn, on, cardTxt[i], 0xFFFFC864u);
        y += sideH;
    } else if (!graphic && (rows.bubble || rows.hp)) {
        // ---- the luopan, text : the bubble, then its HP and the time its slope leaves it ----
        nextBlock();
        if (rows.bubble) {
            char db[16] = {};
            if (C.geoDist && g.dist >= 0.0f) { _snprintf(db, sizeof(db), "%.1fy", g.dist > 999.0f ? 999.0f : g.dist); db[sizeof(db) - 1] = 0; }
            iconRow(y, bs, "Luopan", 0, db, g.outOfRange ? 0xFFFF6A5Au : geo_col(GEO_VALUE, 0xFFB8C0C8u));   // red : the player is outside its radius
            y += iconRowH;
        }
        if (rows.hp) {
            const float cy = y + lineH * 0.5f, gh = snap(7.0f * S);
            char tb[16]; fmt_left(g.bubbleLeftSec, tb, sizeof(tb));
            char hb[12]; sprintf(hb, "%d%%", g.luopanHpp);
            // the time on the right, the HP figure left of it, the gauge filling what is left
            const float tw = rows.time ? fv->measure("00:00", zv) : 0.0f, hw = fv->measure("100%", zv), sp = snap(6.0f * S);
            const float gw = (xr - tw - sp - hw - sp) - x0;
            pet_gauge(dev, x0, snap(cy - gh * 0.5f), gw, gh, g.luopanHpp / 100.0f, hc);
            fv->begin(dev);
            fv->draw_lc(dev, x0 + gw + sp, cy, hb, zv, geo_col(GEO_VALUE, 0xFFE8EEF2u), 0xC0000000u, ov);
            if (rows.time) fv->draw_lc(dev, xr - fv->measure(tb, zv), cy, tb, zv, g.bubbleLeftSec >= 0 ? geo_col(GEO_VALUE, 0xFFFFF4D6u) : 0xFF8C94A0u, 0xC0000000u, ov);
            y += lineH;
        }
    }

    // ---- the Indi- : the aura we carry, the one we entrusted ----
    if (rows.aura || rows.entrust) {
        nextBlock();
        if (rows.aura) {
            char tb[16]; fmt_left(g.auraLeftSec, tb, sizeof(tb));
            iconRow(y, geo_spell(g.auraSpell), "Indi-", 0, tb, geo_col(GEO_VALUE, 0xFFE8EEF2u));
            y += iconRowH;
        }
        if (rows.entrust) {
            char tb[16]; fmt_left(g.entrustLeftSec, tb, sizeof(tb));
            char sfx[28]; _snprintf(sfx, sizeof(sfx), " > %s", g.entrustTarget); sfx[sizeof(sfx) - 1] = 0;
            iconRow(y, geo_spell(g.entrustSpell), "Indi-", sfx, tb, geo_col(GEO_VALUE, 0xFFE8EEF2u));
            y += iconRowH;
        }
    }

    // ---- MP ----
    if (rows.mp) {
        nextBlock();
        const float cy = y + lineH * 0.5f, gh = snap(7.0f * S), sp = snap(6.0f * S);
        char mb[12]; sprintf(mb, "%d", g.mp);
        fv->begin(dev);
        const float lw = fv->draw_lc(dev, x0, cy, "MP", zv, 0xFF8C94A0u, 0xC0000000u, ov);
        const float vw = fv->measure("00000", zv);
        const float gx = x0 + lw + sp, gw = xr - vw - sp - gx;
        pet_gauge(dev, gx, snap(cy - gh * 0.5f), gw, gh, g.mpp / 100.0f, 0xFF6FA8FFu);
        fv->begin(dev);
        fv->draw_lc(dev, xr - fv->measure(mb, zv), cy, mb, zv, geo_col(GEO_VALUE, 0xFFE8EEF2u), 0xC0000000u, ov);
        y += lineH;
    }

    // ---- the job abilities ----
    if (rows.jaRows > 0) {
        nextBlock();
        Font* fj = geo_font(f, GEO_JA);
        const float zj = geo_sz(GEO_JA, 10.0f) * S, oj = geo_ow(GEO_JA, 1.0f) * S;
        if (jaIcons) {
            // a row of icons : bright when ready, gold while its effect is on us (Bolster, Blaze of Glory, a pending
            // Entrust) ; on recast, dim, the part of the recast already gone filling it from the bottom (the BST box's
            // Ready paws ; the player's pick 2026-10-29 : nothing drawn over the icon), the time under it ; an ability
            // without its image keeps its label
            const float cw = (boxW - 2.0f * pad) / (float)(g.nja > 0 ? g.nja : 1);
            for (int i = 0; i < g.nja; ++i) {
                const GeoJa& j = g.jas[i];
                const float jx = snap(x0 + cw * ((float)i + 0.5f)), jy = snap(y + jaIsz * 0.5f);
                const bool ready = j.recastSec <= 0;
                const u32 col = j.active ? 0xFFFFC864u : (ready ? geo_col(GEO_JA, 0xFFE8EEF2u) : 0x46FFFFFFu);
                const float gone = ready ? 1.0f : 1.0f - ja_recast_share(j.abil, j.recastSec, preview || editing);
                const float ix = jx - jaIsz * 0.5f, iy = jy - jaIsz * 0.5f;
                if (const u32 t = geo_tex(dev, ja_slot(j.abil))) {
                    pet_tex_quad(dev, t, ix, iy, jaIsz, jaIsz, 0.0f, 1.0f, col);
                    if (!ready && gone > 0.0f) pet_tex_quad(dev, t, ix, iy + jaIsz * (1.0f - gone), jaIsz, jaIsz * gone, 1.0f - gone, 1.0f, 0xFFBECBD7u);
                }
                else { fj->begin(dev); fj->draw_cc(dev, jx, jy, j.label, zj, col, 0xC0000000u, oj); }
                if (!ready) {
                    char rb[12]; fmt_left(j.recastSec, rb, sizeof(rb));
                    fj->begin(dev); fj->draw_cc(dev, jx, snap(y + jaIsz + snap(6.5f * S)), rb, zj, 0xFF8C94A0u, 0xC0000000u, oj);
                }
            }
            y += jaRowH;
        } else {
            // the grid : (its icon,) its label, then "ok" (green) or its recast ; the one whose effect is on us in gold on
            // a lit pill, so it reads at a glance
            const float cw = (boxW - 2.0f * pad) / (float)JA_COLS, cpad = snap(3.0f * S), gi = jaCellH - snap(3.0f * S);
            for (int i = 0; i < g.nja; ++i) {
                const GeoJa& j = g.jas[i];
                const float cx = snap(x0 + cw * (float)(i % JA_COLS)), cy = y + jaCellH * ((float)(i / JA_COLS) + 0.5f);
                if (j.active) {
                    dColorQuadState(dev);
                    rrect(dev, cx, snap(cy - jaCellH * 0.5f + 1.0f), cw - snap(2.0f * S), jaCellH - 2.0f, snap(3.0f * S), 0x60FFC864u, 0x40FFC864u, 1.0f);
                }
                float lx = cx + cpad;
                if (jaGridIcons) {   // its icon before its name, dim while on recast
                    if (const u32 t = geo_tex(dev, ja_slot(j.abil)))
                        pet_tex_quad(dev, t, lx, snap(cy - gi * 0.5f), gi, gi, 0.0f, 1.0f, j.active ? 0xFFFFC864u : (j.recastSec > 0 ? 0x60FFFFFFu : geo_col(GEO_JA, 0xFFE8EEF2u)));
                    lx += gi + snap(3.0f * S);
                }
                char rb[12]; if (j.recastSec > 0) fmt_left(j.recastSec, rb, sizeof(rb)); else strcpy(rb, "ok");
                const float rx = cx + cw - snap(2.0f * S) - cpad - fj->measure(rb, zj);
                char lb[16]; pet_fit_cut(fj, j.label, zj, rx - snap(3.0f * S) - lx, lb, sizeof(lb));   // "Entrust" beside "1h12"
                fj->begin(dev);
                fj->draw_lc(dev, lx, cy, lb, zj, j.active ? 0xFFFFC864u : geo_col(GEO_JA, 0xFFB8C0C8u), 0xC0000000u, oj);
                fj->draw_lc(dev, rx, cy, rb, zj, j.recastSec > 0 ? 0xFF8C94A0u : 0xFF9BE89Bu, 0xC0000000u, oj);
            }
            y += jaCellH * (float)rows.jaRows;
        }
    }
}

} // namespace aio
