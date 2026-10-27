// hud_bst.cpp -- the Beastmaster box : the Ready dial (charges, the next one's seconds) at its heart, the pet (image,
// name, HP, TP, distance), its correlation and the Ready moves
// with what each one still waits for. Reads GameState::bst (model/bst_state.cpp, game_mem.cpp compute_bst) ; every
// element has its own on / off (UiConfig bst*). SPEC docs/design/bst-module-spec.md. //aio edit places it.
//
// The pet image is assets\bstpets\<PetNameWithoutSpaces>.raw (120 x 120 BGRA, scripts/gen_bst_pet_art.py) in the
// RUNTIME folder only : the test art comes from the BST-HUD addon (Eiffel's), never committed nor packaged ; the
// player's own art takes the same names.
#include "ui/hud.h"
#include "ui/hud_internal.h"
#include "model/ui_config.h"
#include "ui/text_style.h"
#include "ui/box_style.h"
#include "ui/buff_atlas.h"    // a buff / debuff move's status icon : the shared atlas the Timers draw from     // draw_themed_box : the same chrome options as every module (frame, transparency, theme)
#include "ui/tex_retry.h"
#include "model/gamestate.h"
#include "model/ready_charges.h"   // READY_CHARGES
#include "model/ready_moves_gen.h"  // the preview pet's moves : their kind and status icon
#include "model/bst_pets_gen.h"     // the preview pets (every jug pet and its Ready moves)
#include "model/bst_state.h"        // bst_ready_cost
#include "model/bst_correlation.h"  // the correlation line
#include "model/paths.h"
#include "gfx/draw.h"
#include "gfx/d3d.h"
#include "gfx/texture.h"
#include <stdio.h>
#include <string.h>

namespace aio {

static const int BST_ART = 120;   // the converted art's side, px

static Font* bst_font(const Frame& f, int e) { return te_font(f, ui_config().bstText[e]); }
static inline float bst_sz(int e, float base) { return te_sz(ui_config().bstText[e], base); }
static inline float bst_ow(int e, float base) { return te_ow(ui_config().bstText[e], base); }
static inline u32   bst_col(int e, u32 base)  { return te_col(ui_config().bstText[e], base); }

// the colour of a move's kind (ready_moves_gen.h : what BST-HUD shows)
static u32 kind_col(const char* k) {
    if (!k) return 0xFFC8B89Cu;
    if (!strcmp(k, "slashing") || !strcmp(k, "piercing") || !strcmp(k, "blunt")) return 0xFFE8C9A0u;
    if (!strcmp(k, "fire")) return 0xFFFF7A5Cu;      if (!strcmp(k, "water")) return 0xFF6FB6FFu;
    if (!strcmp(k, "wind")) return 0xFF7CE08Au;      if (!strcmp(k, "earth")) return 0xFFD8B25Au;
    if (!strcmp(k, "lightning")) return 0xFFC79BFFu; if (!strcmp(k, "ice")) return 0xFF9BE7FFu;
    if (!strcmp(k, "dark")) return 0xFF9C7BD9u;      if (!strcmp(k, "light")) return 0xFFFFF4C0u;
    if (!strcmp(k, "buff")) return 0xFF8CC06Au;      if (!strcmp(k, "debuff")) return 0xFFE07AD0u;
    return 0xFFFF9BC8u;   // magic
}
// The icon of a move's kind : the weapon strip (assets/weapon_icons.raw, 0 Slashing 1 Piercing 2 Blunt,
// scripts/gen_weapon_icons.py) or the element strip (assets/element_icons.raw, 0 Fire 1 Earth 2 Water 3 Wind 4 Ice
// 5 Lightning 6 Light 7 Dark, scripts/gen_element_icons.py) ; -1 when there is none (magic, buff, debuff)
static int kind_weapon(const char* k) { if (!k) return -1; return !strcmp(k, "slashing") ? 0 : !strcmp(k, "piercing") ? 1 : !strcmp(k, "blunt") ? 2 : -1; }
static int kind_element(const char* k) {
    static const char* E[8] = { "fire", "earth", "water", "wind", "ice", "lightning", "light", "dark" };
    if (k) for (int i = 0; i < 8; ++i) if (!strcmp(k, E[i])) return i;
    return -1;
}
// the art's file key : the pet's name without spaces nor punctuation ("Blackbeard Randy" -> BlackbeardRandy)
static void art_key(const char* name, char* out, int cap) {
    int n = 0;
    for (const char* p = name; *p && n < cap - 1; ++p)
        if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9')) out[n++] = *p;
    out[n] = 0;
}

// the sample shown in //aio edit and in the config preview : the jug pet chosen there (UiConfig::bstDemo), its own
// art and Ready moves (bst_pets_gen.h), with 1 charge left and 5 s to the next on a 10 s charge
static void bst_sample(BstState& b) {
    b = BstState{};
    const int pi = (ui_config().bstDemo < 0 || ui_config().bstDemo >= BST_PETS_N) ? 0 : ui_config().bstDemo;
    const BstPetRow& row = BST_PETS[pi];
    b.visible = true; b.hasPet = true; strncpy(b.name, row.name, sizeof(b.name) - 1);
    b.hpp = 87; b.mpp = 0; b.tp = 1250; b.dist = 4.2f; b.charges = 1; b.nextSec = 5; b.chargeLen = 10; b.learned = true;
    for (int i = 0; i < 8 && row.moves[i] && b.nmoves < BstState::MOVES_MAX; ++i) {
        BstMove& m = b.moves[b.nmoves++]; const ReadyMoveRow* rr = ready_move_row(row.moves[i]);
        m.en = row.moves[i]; m.kind = rr ? rr->kind : 0; m.status = rr ? rr->status : 0;
        m.cost = bst_ready_cost(row.moves[i]); m.waitSec = ready_wait_sec(15 * 60, 10, m.cost);
    }
}

// The box's colours are nature tones (the player's pick 2026-10-27, "a beast master, not a computer") : amber and
// honey for the charges, moss for what can go, brick for what beats the pet, bark for faces and empty tracks,
// parchment for the text. Each text element's colour can still be changed (UiConfig bstText)
static const float BST_TAU = 6.2831853f;
// `s` cut to fit `maxW` at `z`, a dot where it was cut ("Swooping Zhivago" -> "Swooping Zh.")
static void fit_text(Font* fo, const char* s, float z, float maxW, char* out, int cap) {
    strncpy(out, s, cap - 1); out[cap - 1] = 0;
    if (maxW <= 0.0f || fo->measure(out, z) <= maxW) return;
    int n = (int)strlen(out);
    while (n > 1) { out[--n] = 0; out[n - 1] = '.'; if (fo->measure(out, z) <= maxW) return; }
}
// a thin gauge : dark track, coloured fill, its value on the right
static void bst_gauge(u32 dev, Font* fo, float x, float y, float w, float h, float frac, u32 fill, const char* txt, float z, float ow, u32 tcol) {
    if (frac < 0.0f) frac = 0.0f; if (frac > 1.0f) frac = 1.0f;
    dColorQuadState(dev);
    rrect(dev, x, y, w, h, h * 0.5f, 0xC0281E16u, 0xC0140E0Au, 1.0f);
    if (frac > 0.0f) rrect(dev, x, y, snap(w * frac), h, h * 0.5f, fill, (fill & 0x00FFFFFFu) | 0xB0000000u, 1.0f);
    fo->begin(dev); fo->draw_lc(dev, x + w + snap(6.0f * (h / 9.0f)), y + h * 0.5f, txt, z, tcol, 0xC0000000u, ow);
}

void Hud::draw_bst(const Frame& f, bool preview, float ovX, float ovY, float ovS) {
    const UiConfig& C = ui_config();
    if (!C.bstShow) return;
    const bool editing = C.editLayout && !preview;
    u32 dev = f.dev;
    if (!f.font && !f.fonts) return;

    BstState b;
    if (preview || editing) bst_sample(b);
    else {
        if (!f.game) return;
        b = f.game->bst;
        if (!b.visible || (C.bstOnlyPet && !b.hasPet)) return;
    }

    float sscl = C.bstScale; if (sscl < 0.5f) sscl = 0.5f; if (sscl > 2.0f) sscl = 2.0f;
    const float S = (ovS > 0.0f) ? ovS : (screenH_ / 1000.0f) * sscl;

    // ---- the pet art (one texture, the current pet's ; reloaded when the pet changes) ----
    u32 art = 0;
    // the art's place : the art, the pet's correlation as text, or both (UiConfig::bstArtMode)
    const int artMode = (C.bstArtMode < 0 || C.bstArtMode > 2) ? 0 : C.bstArtMode;
    const bool showArt = C.bstImage && artMode != 1, showCorr = C.bstImage && artMode != 0;
    if (showArt && b.hasPet) {
        char key[32]; art_key(b.name, key, sizeof(key));
        if (strcmp(key, bstArtKey_) != 0) {
            if (bstArt_) { release_texture(bstArt_); bstArt_ = 0; }
            bstArtR_ = {}; strcpy(bstArtKey_, key);
        }
        if (key[0]) { char rel[64]; _snprintf(rel, sizeof(rel), "assets\\bstpets\\%s.raw", key); rel[sizeof(rel) - 1] = 0;
            art = ensure_raw_tex(dev, bstArt_, bstArtR_, plugin_path_r(rel), BST_ART, BST_ART); }
    }

    // ---- the layout ("Ready dial", the player's pick 2026-10-27 : Ready is the heart, the correlation and the moves
    //      only hints). On the left the Ready dial : three 120-degree segments, a full charge lit, the one recharging
    //      filling like a clock, its seconds in the centre. On its right the pet (art, name, HP, TP), then, small, its
    //      correlation and the charge count. The moves below, small too, each with its wait and its cost.
    //      //aio edit places a FRAME : the box at its tallest (7 rows of moves, Brainy Waluis has 7,
    //      addons/BST_JUG_PETS_DATABASE.md), so what is dragged never changes size
    //      (docs/architecture/layout-reserve-dont-measure.md). In play the box takes the size of the pet's moves and
    //      sits against the side of that frame nearer to its screen edge : a frame in the lower half keeps its BOTTOM
    //      (the box grows upward), one in the upper half keeps its TOP. ----
    static const int MOVE_ROWS = 7;
    const float pad = snap((C.bstBox.on ? 8.0f : 2.0f) * S), W = snap(270.0f * S);
    const float rowH = snap(15.0f * S), lineH = snap(16.0f * S), smallH = snap(14.0f * S), gap = snap(6.0f * S);
    const float dialD = C.bstCharges ? snap(76.0f * S) : 0.0f;
    const float artSz = showArt ? snap(50.0f * S) : 0.0f;
    const int petLines = (C.bstName || C.bstDist ? 1 : 0) + (C.bstHp ? 1 : 0) + (C.bstTp ? 1 : 0);
    float petBlock = lineH * (float)petLines;
    if (artSz > petBlock) petBlock = artSz;
    const float corrH = showCorr ? smallH : 0.0f, cntH = C.bstCharges ? smallH : 0.0f;
    const float rightH = petBlock + corrH + cntH;
    const float topH = rightH > dialD ? rightH : dialD;
    // a box's height for `rows` rows of moves
    auto heightFor = [&](int rows) -> float {
        const float mh = C.bstMoves ? rowH * (float)rows : 0.0f;
        const int blocks = (topH > 0.0f ? 1 : 0) + (mh > 0.0f ? 1 : 0);
        return blocks ? pad * 2.0f + topH + mh + gap * (float)(blocks - 1) : 0.0f;
    };
    const float frameH = heightFor(b.nmoves > MOVE_ROWS ? b.nmoves : MOVE_ROWS);
    if (frameH <= 0.0f) return;
    const bool frame = editing;   // //aio edit drags the frame ; the config preview and play show the box itself
    const float boxW = W, boxH = frame ? frameH : heightFor(b.nmoves);
    if (boxH <= 0.0f) return;
    const float movH = (C.bstMoves && b.nmoves) ? rowH * (float)b.nmoves : 0.0f;   // the rows drawn below

    float px, py;
    if (ovS > 0.0f) { px = snap(ovX - boxW * 0.5f); py = snap(ovY - boxH * 0.5f); }
    else            { px = box_grow_x(screenW_, UiConfig::GB_BST, 0, C.bstX, boxW); py = snap(C.bstY * screenH_); }
    if (editing) { static EditBox g_bstEdit; box_edit(f, g_bstEdit, EDITBOX_BST, px, py, boxW, boxH, ui_config().bstScale, ui_config().bstX, ui_config().bstY, C.boxGrow[UiConfig::GB_BST]); }
    if (!editing && ovS <= 0.0f) {
        if (py + frameH * 0.5f > screenH_ * 0.5f) py = snap(py + frameH - boxH);   // lower half : the frame's bottom
        box_on_screen(f, px, py, boxW, boxH);
    }

    draw_themed_box(dev, f.skin, px, py, boxW, boxH, C.bstBox, 1.0f, S);   // shared themed chrome (frame / transparency / theme, Same-as-Party by default)

    const float top = py + pad;
    Font* fv = bst_font(f, BST_VALUE);
    const float zv = bst_sz(BST_VALUE, 11.0f) * S, ov = bst_ow(BST_VALUE, 1.0f) * S;

    // ---- the Ready dial : a dark face, three segments (a gap between them), full ones gold with a glow, the one
    //      recharging filled by the share of its length gone ; in the centre the seconds to the next one, MAX when
    //      the pool is full ----
    if (dialD > 0.0f) {
        const float cx = snap(px + pad + dialD * 0.5f), cy = snap(top + topH * 0.5f);
        const float rOut = dialD * 0.5f - snap(2.0f * S), rIn = rOut - snap(7.0f * S), glow = snap(2.5f * S);
        const float segA = BST_TAU / (float)READY_CHARGES, gapA = 0.12f;
        const float frac = (b.nextSec >= 0 && b.chargeLen > 0) ? 1.0f - (float)b.nextSec / (float)b.chargeLen : 0.0f;
        dColorQuadState(dev);
        disc(dev, cx, cy, rIn - snap(3.0f * S), 0xC0181210u);
        for (int i = 0; i < READY_CHARGES; ++i) {
            const float a0 = -BST_TAU * 0.25f + segA * (float)i + gapA * 0.5f, a1 = a0 + segA - gapA;
            ring_sector(dev, cx, cy, rIn, rOut, a0, a1, 0xFF3A2E24u);
            if (i < b.charges) {
                ring_sector(dev, cx, cy, rIn - glow, rOut + glow, a0, a1, 0x40E8A84Au);
                ring_sector(dev, cx, cy, rIn, rOut, a0, a1, 0xFFE8A84Au);
            } else if (i == b.charges && frac > 0.0f) {
                const float af = a0 + (a1 - a0) * (frac > 1.0f ? 1.0f : frac);
                ring_sector(dev, cx, cy, rIn, rOut, a0, af, 0xFF9C6E34u);
            }
        }
        Font* fc = bst_font(f, BST_CHARGE);
        const float zc = bst_sz(BST_CHARGE, 20.0f) * S, oc = bst_ow(BST_CHARGE, 1.2f) * S;
        char cb[12];
        if (b.nextSec >= 0) sprintf(cb, "%ds", b.nextSec); else strcpy(cb, "MAX");
        fc->begin(dev);
        fc->draw_cc(dev, cx, cy, cb, zc, b.nextSec >= 0 ? bst_col(BST_CHARGE, 0xFFF2E6CCu) : 0xFFE8A84Au, 0xC0000000u, oc);
    }

    // ---- the right column, centred on the dial : the pet (art beside name / HP / TP), its correlation, the count ----
    const float x0 = px + pad + (dialD > 0.0f ? dialD + pad : 0.0f), xr = px + boxW - pad;
    float ry = snap(top + (topH - rightH) * 0.5f);
    if (b.hasPet) {
        if (art) {
            dSetVS(dev, FVF_XYZRHW_DIFFUSE_TEX1);
            dSetRS(dev, D3DRS_ALPHABLENDENABLE, 1);
            dSetRS(dev, D3DRS_SRCBLEND, D3DBLEND_SRCALPHA); dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
            dSetTex(dev, 0, art);
            dSetTSS(dev, 0, D3DTSS_COLOROP, D3DTOP_MODULATE); dSetTSS(dev, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE); dSetTSS(dev, 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
            dSetTSS(dev, 0, D3DTSS_ALPHAOP, D3DTOP_MODULATE); dSetTSS(dev, 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE); dSetTSS(dev, 0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
            dSetTSS(dev, 0, D3DTSS_MINFILTER, D3DTEXF_LINEAR); dSetTSS(dev, 0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR); dSetTSS(dev, 0, D3DTSS_MIPFILTER, D3DTEXF_NONE);
            tquad(dev, x0, ry + (petBlock - artSz) * 0.5f, artSz, artSz, 0.0f, 1.0f, 0.0f, 1.0f, 0xFFFFFFFFu, 0xFFFFFFFFu);
            dSetTex(dev, 0, 0);
        }
        const float tx = x0 + (artSz > 0.0f ? artSz + snap(6.0f * S) : 0.0f), tw = xr - tx;   // the art's square stays, drawn or not
        float ly = ry + (petBlock - lineH * (float)petLines) * 0.5f;
        if (C.bstName || C.bstDist) {
            float distW = 0.0f;
            if (C.bstDist && b.dist >= 0.0f) {
                char db[16]; _snprintf(db, sizeof(db), "%.1fy", b.dist > 999.0f ? 999.0f : b.dist); db[sizeof(db) - 1] = 0;
                distW = fv->measure(db, zv) + snap(6.0f * S);
                fv->begin(dev);
                fv->draw_lc(dev, xr - fv->measure(db, zv), ly + lineH * 0.5f, db, zv, bst_col(BST_VALUE, b.dist > 20.0f ? 0xFFE0805Cu : 0xFFC8B89Cu), 0xC0000000u, ov);
            }
            if (C.bstName) {
                Font* fn = bst_font(f, BST_NAME);
                const float zn = bst_sz(BST_NAME, 12.5f) * S, on = bst_ow(BST_NAME, 1.0f) * S;
                char nm[sizeof(b.name)]; fit_text(fn, b.name, zn, tw - distW, nm, sizeof(nm));
                fn->begin(dev);
                fn->draw_lc(dev, tx, ly + lineH * 0.5f, nm, zn, bst_col(BST_NAME, 0xFFF2E6CCu), 0xC0000000u, on);
            }
            ly += lineH;
        }
        const float gh = snap(7.0f * S), gw = tw - snap(38.0f * S);
        if (C.bstHp) {
            char hb[12]; sprintf(hb, "%d%%", b.hpp);
            const u32 hc = b.hpp > 50 ? 0xFF7CB860u : (b.hpp > 25 ? 0xFFD8A848u : 0xFFD8604Cu);
            bst_gauge(dev, fv, tx, ly + (lineH - gh) * 0.5f, gw, gh, b.hpp / 100.0f, hc, hb, zv, ov, bst_col(BST_VALUE, 0xFFEDE2CAu));
            ly += lineH;
        }
        if (C.bstTp) {
            char tb[12]; sprintf(tb, "%d", b.tp);
            bst_gauge(dev, fv, tx, ly + (lineH - gh) * 0.5f, gw, gh, b.tp / 3000.0f, b.tp >= 1000 ? 0xFF6FA8C0u : 0xFF4E7488u, tb, zv, ov, bst_col(BST_VALUE, 0xFFEDE2CAu));
        }
    }
    ry += petBlock;   // the pet's room is kept even with no pet out

    // ---- the correlation, a hint : green arrow up, the family it beats ; red arrow down, the one that beats it ;
    //      its family from the jug database (the game's name has no spaces, "BlackbeardRandy" : bst_pet_eco compares
    //      letters and digits only), the cycles from bst_correlation.h ----
    const float zs = bst_sz(BST_VALUE, 10.0f) * S;
    if (corrH > 0.0f) {
        const BstCorrelation* cr = bst_correlation(b.hasPet ? bst_pet_eco(b.name) : 0);
        if (cr) {
            const float cy = ry + corrH * 0.5f, t = snap(3.5f * S);
            float x = x0;
            fv->begin(dev);
            x += fv->draw_lc(dev, x, cy, cr->eco, zs, bst_col(BST_VALUE, 0xFFE8A84Au), 0xC0000000u, ov) + snap(8.0f * S);
            dColorQuadState(dev);
            { const float up[6] = { x + t, cy - t, x + 2.0f * t, cy + t * 0.8f, x, cy + t * 0.8f }; fill_poly_aa(dev, up, 3, 0xFF8CC06Au); }
            x += 2.0f * t + snap(3.0f * S);
            fv->begin(dev);
            x += fv->draw_lc(dev, x, cy, cr->strong, zs, 0xFFA8D07Au, 0xC0000000u, ov) + snap(8.0f * S);
            dColorQuadState(dev);
            { const float dn[6] = { x, cy - t * 0.8f, x + 2.0f * t, cy - t * 0.8f, x + t, cy + t }; fill_poly_aa(dev, dn, 3, 0xFFD8604Cu); }
            x += 2.0f * t + snap(3.0f * S);
            fv->begin(dev);
            fv->draw_lc(dev, x, cy, cr->weak, zs, 0xFFE08A70u, 0xC0000000u, ov);
        }
        ry += corrH;
    }
    // ---- the charge count beside the dial's seconds : READY 2/3 ----
    if (cntH > 0.0f) {
        const float cy = ry + cntH * 0.5f;
        char cb[12]; sprintf(cb, "%d/%d", b.charges, READY_CHARGES);
        fv->begin(dev);
        const float lw = fv->draw_lc(dev, x0, cy, "READY", zs, 0xFF9A8C76u, 0xC0000000u, ov);
        fv->draw_lc(dev, x0 + lw + snap(5.0f * S), cy, cb, zs, b.charges > 0 ? 0xFFE8A84Au : 0xFF9A8C76u, 0xC0000000u, ov);
    }

    // ---- the Ready moves, small : number, kind icon (or a colour tick), name (green when it can go, grey while
    //      charges are short), its wait when it has one, its cost ----
    if (movH > 0.0f) {
        float y = top + topH + gap;
        dColorQuadState(dev);
        rrect(dev, px + pad, snap(y - gap * 0.5f), boxW - 2.0f * pad, 1.0f, 0.0f, 0x40C8A070u, 0x40C8A070u, 0.0f);   // the line between the parts
        Font* fm = bst_font(f, BST_MOVE);
        const float zm = bst_sz(BST_MOVE, 10.5f) * S, om = bst_ow(BST_MOVE, 1.0f) * S, isz = snap(12.0f * S);
        u32 wtex = 0, etex = 0, stex = 0;
        if (C.bstIcons) {
            wtex = ensure_raw_tex(dev, weaponIcons_, weaponIcons_r_, plugin_path_r("assets\\weapon_icons.raw"), 96, 32);
            etex = ensure_raw_tex(dev, bstElem_, bstElemR_, plugin_path_r("assets\\element_icons.raw"), 256, 32);
            stex = buff_atlas_tex(dev);   // shared, never released here (buff_atlas.h)
        }
        for (int i = 0; i < b.nmoves; ++i) {
            const BstMove& m = b.moves[i];
            // its number first : the N of GearSwap's //gs c rdymove N, which sorts the moves by ability id as this list
            // does (shared/jobs/bst/functions/logic/pet_manager.lua) ; the column reserved for one digit
            const float numW = fm->measure("8", zm) + snap(6.0f * S), cy = y + rowH * (i + 0.5f), ix = px + pad + numW;
            { char nb[4]; sprintf(nb, "%d", i + 1); fm->begin(dev);
              fm->draw_lc(dev, px + pad, cy, nb, zm, bst_col(BST_MOVE, 0xFFB89650u), 0xC0000000u, om); }
            const int wi = kind_weapon(m.kind), ei = kind_element(m.kind);
            const int si = (int)m.status;
            if (si > 0 && si < BUFF_COLS * BUFF_ATLAS_ROWS && stex) {   // a buff / debuff : the status it gives
                float au, av, u0, v0; buff_cell_uv(si, au, av, u0, v0);
                draw_icon_cell(dev, stex, ix, snap(cy - isz * 0.5f), isz, isz, u0, u0 + au, v0, v0 + av);
            }
            else if (wi >= 0 && wtex) draw_icon_cell(dev, wtex, ix, snap(cy - isz * 0.5f), isz, isz, wi / 3.0f, (wi + 1) / 3.0f);
            else if (ei >= 0 && etex) draw_icon_cell(dev, etex, ix, snap(cy - isz * 0.5f), isz, isz, ei / 8.0f, (ei + 1) / 8.0f);
            else {   // no icon for that kind (magic, buff, debuff), or the icons are off / not loaded : the colour tick
                const float tk = snap(3.0f * S);
                dColorQuadState(dev);
                rrect(dev, ix + (isz - tk) * 0.5f, cy - snap(5.0f * S), tk, snap(10.0f * S), tk * 0.5f, kind_col(m.kind), kind_col(m.kind), 1.0f);
            }
            const bool ready = m.waitSec <= 0;
            fm->begin(dev);
            fm->draw_lc(dev, ix + isz + snap(5.0f * S), cy, m.en, zm, ready ? bst_col(BST_MOVE, 0xFFA8D07Au) : 0xFF9A8C76u, 0xC0000000u, om);
            // its cost as 1..3 dots in a pill, the same gold as the dial's charges when they are there, grey when short.
            // No green / orange / red per cost : the player's call (2026-10-27), red reads as "unavailable" and green
            // as "ready", which a cost is not -- the name's colour already says that
            const float dr = snap(2.4f * S), dstep = snap(6.5f * S), pillW = snap(7.0f * S) + dstep * 3.0f, pillH = snap(10.0f * S);
            const float pillX = xr - pillW, pillY = snap(cy - pillH * 0.5f);
            // the wait before it can go (the player's pick 2026-10-27), left of the pill
            if (!ready) {
                char wb[12]; sprintf(wb, "%ds", m.waitSec);
                fm->begin(dev);
                fm->draw_lc(dev, pillX - snap(5.0f * S) - fm->measure(wb, zm), cy, wb, zm, 0xFFC8B89Cu, 0xC0000000u, om);
            }
            dColorQuadState(dev);
            rrect(dev, pillX, pillY, pillW, pillH, pillH * 0.5f, 0xA0281E16u, 0xA0140E0Au, 1.0f);
            const u32 dc = ready ? 0xFFE8A84Au : 0xFF5C5044u;
            const int nd = m.cost < 1 ? 1 : (m.cost > 3 ? 3 : m.cost);
            const float d0 = pillX + pillW * 0.5f - dstep * (nd - 1) * 0.5f;   // centred in the pill (it is sized for 3)
            for (int k = 0; k < nd; ++k) rrect(dev, snap(d0 + dstep * k) - dr, cy - dr, 2.0f * dr, 2.0f * dr, dr, dc, dc, 1.0f);
        }
    }
}

} // namespace aio
