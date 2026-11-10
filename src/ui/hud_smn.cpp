// hud_smn.cpp -- the Summoner content of the Pet box : the avatar or spirit out (the player's picture of it in a disc,
// its HP as a ring, its TP as an inner arc, its name in its element's colour, the distance to it), the Blood Pact Rage /
// Ward recasts and the SMN job abilities, MP, and the pacts of that avatar the player knows (Rage, then Ward : the MP
// each costs, red when the player has less ; a Rage's skillchain opening). A spirit casts on its own : its spells
// instead of the pacts. Reads GameState::smn (game_mem.cpp compute_smn, smn_avatars_gen.h) ; every element has its own
// on / off (UiConfig smn*). SPEC docs/design/smn-module-spec.md. The box's place, size and frame are the Pet module's
// (hud_pet.cpp calls draw_smn ; UiConfig pet* ; docs/design/pet-module-spec.md).
//
// Two layouts (UiConfig::smnLayout, the player's pick 2026-11-10 : both, a setting) : 0 the disc ON TOP of the box with
// no box round it, as the GEO compass and the BST disc ; 1 the disc beside its name / HP / TP / distance, in the box.
// The abilities that put a status on the player (Astral Flow, Apogee, Astral Conduit, Avatar's Favor) show that
// status's icon from the buff atlas, dimmed while on recast with the part of it already gone lit from the bottom ; Rage,
// Ward, Elemental Siphon and Mana Cede have no status and AioHUD no ability icons (the Timers' recasts are text too) :
// their name (the player, 2026-11-10 : "prends les icônes d'AioHUD"). The pictures are assets\smn\<img>.raw
// (scripts/gen_smn_art.py, the player's images) ; without one, the disc has no picture.
#include "ui/hud.h"
#include "ui/hud_internal.h"
#include "model/ui_config.h"
#include "ui/text_style.h"
#include "ui/box_style.h"     // draw_themed_box : the same chrome options as every module (frame, transparency, theme)
#include "ui/pet_common.h"    // the Pet box's drag state and the helpers shared with hud_bst.cpp / hud_geo.cpp
#include "ui/tex_retry.h"
#include "model/gamestate.h"
#include "model/smn_avatars_gen.h"
#include "model/abilities_gen.h"   // the sample's pacts : their names and MP costs
#include "model/skillchain.h"      // a Rage pact's skillchain opening
#include "model/paths.h"
#include "gfx/draw.h"
#include "gfx/d3d.h"
#include "gfx/texture.h"
#include <stdio.h>
#include <string.h>

namespace aio {

static Font* smn_font(const Frame& f, int e) { return te_font(f, ui_config().smnText[e]); }
static inline float smn_sz(int e, float base) { return te_sz(ui_config().smnText[e], base); }
static inline float smn_ow(int e, float base) { return te_ow(ui_config().smnText[e], base); }
static inline u32   smn_col(int e, u32 base)  { return te_col(ui_config().smnText[e], base); }

static const float SMN_TAU = 6.2831853f;
static const int   SMN_ART = 128;   // the converted picture's side, px (scripts/gen_smn_art.py)

// the sample shown in //aio edit and in the config preview : the avatar or spirit chosen there (UiConfig::smnDemo,
// the player's ask 2026-11-10 : to check each one), every pact of it known, Rage on recast, Ward ready, Avatar's Favor
// up, Astral Flow on its long recast. `avatar` < 0 : the one chosen ; else that row (the frame's Ifrit)
static void smn_sample(SmnState& s, int avatar = -1) {
    s = SmnState{};
    if (avatar < 0) avatar = ui_config().smnDemo;
    if (avatar < 0 || avatar >= SMN_AVATARS_N) avatar = 0;
    s.visible = true; s.hasPet = true; s.avatar = avatar;
    strncpy(s.name, SMN_AVATARS[avatar].en, sizeof(s.name) - 1); s.name[sizeof(s.name) - 1] = 0;
    s.hpp = 84; s.tp = 1350; s.dist = 4.2f; s.mp = 1620; s.mpp = 72;
    static const struct { unsigned short abil; const char* label; unsigned short status; int sec; bool active; } J[] = {
        {91, "Rage", 0, 38, false}, {172, "Ward", 0, 0, false}, {30, "Astral Flow", 55, 3960, false}, {385, "Apogee", 583, 0, false},
        {337, "Astral Conduit", 504, 412, false}, {250, "Avatar's Favor", 431, 0, true}, {232, "Siphon", 0, 130, false}, {296, "Mana Cede", 0, 0, false} };
    for (const auto& j : J) { if (s.nja >= SmnState::JA_MAX) break; SmnJa& d = s.jas[s.nja++]; d.abil = j.abil; d.label = j.label; d.status = j.status; d.recastSec = j.sec; d.active = j.active; }
    for (int i = 0; i < SMN_PACTS_N && s.npacts < SmnState::PACTS_MAX; ++i) {
        if (SMN_PACTS[i].avatar != s.avatar) continue;
        SmnPact& p = s.pacts[s.npacts++];
        p.abil = SMN_PACTS[i].abil; p.ward = SMN_PACTS[i].ward != 0;
        if (const AbilRow* r = abil_info(p.abil)) { p.en = r->en; p.mp = (int)r->mp; }
    }
}

// a Rage pact's first skillchain opening, -1 for none
static int pact_sc(unsigned short abil) {
    const SkillRow* r = sc_skill_lookup(SCR_JA, abil);
    return (r && r->prop[0] < SCP_N) ? (int)r->prop[0] : -1;
}

// a spirit's spells ("Fire I-V, Flare, Burn"), laid in at most 3 lines that fit `w`, a line breaking after a comma ;
// what does not fit in 3 lines is cut with a dot
static int wrap_spells(Font* fo, const char* s, float z, float w, char out[3][96]) {
    int n = 0; out[0][0] = 0;
    const char* p = s;
    while (*p && n < 3) {
        const char* e = p; while (*e && *e != ',') ++e;          // the next piece, its comma included
        char piece[64]; int len = (int)(e - p) + (*e == ',' ? 1 : 0); if (len > 63) len = 63;
        memcpy(piece, p, len); piece[len] = 0;
        char trial[96]; _snprintf(trial, sizeof(trial), "%s%s%s", out[n], out[n][0] ? " " : "", piece); trial[sizeof(trial) - 1] = 0;
        if (!out[n][0] || fo->measure(trial, z) <= w) strcpy(out[n], trial);
        else if (n + 1 < 3) { ++n; strncpy(out[n], piece, 95); out[n][95] = 0; }
        else { char cut[96]; pet_fit_cut(fo, trial, z, w, cut, sizeof(cut)); strcpy(out[n], cut); break; }
        p = (*e == ',') ? e + 1 : e; while (*p == ' ') ++p;
    }
    return out[0][0] ? n + 1 : 0;
}

void Hud::draw_smn(const Frame& f, bool preview, float ovX, float ovY, float ovS) {
    const UiConfig& C = ui_config();
    if (!C.smnShow) return;
    const bool editing = C.editLayout && !preview;
    const bool sample = preview || editing;
    u32 dev = f.dev;
    if (!f.font && !f.fonts) return;

    SmnState s;
    if (sample) smn_sample(s);
    else {
        if (!f.game) return;
        s = f.game->smn;
        if (!s.visible || (C.smnOnlyPet && !s.hasPet)) return;
    }
    SmnState frameS; smn_sample(frameS, smn_avatar_index("Ifrit"));   // //aio edit's frame and the anchoring : everything present (Ifrit's 11 pacts, the most with Diabolos and Siren)
    const SmnAvatarRow* av = (s.avatar >= 0 && s.avatar < SMN_AVATARS_N) ? &SMN_AVATARS[s.avatar] : 0;

    float sscl = C.petScale; if (sscl < 0.5f) sscl = 0.5f; if (sscl > 2.0f) sscl = 2.0f;
    const float S = (ovS > 0.0f) ? ovS : (screenH_ / 1000.0f) * sscl;
    const float k = (C.smnIconSz < 0.5f) ? 0.5f : (C.smnIconSz > 2.0f ? 2.0f : C.smnIconSz);
    const bool stacked = C.smnLayout != 1;

    // ---- the picture of the avatar out : one texture, released when another avatar comes ----
    u32 art = 0;
    if (C.smnPortrait && s.hasPet && av) {
        if (strcmp(av->img, smnArtKey_) != 0) {
            if (smnArt_) { release_texture(smnArt_); smnArt_ = 0; }
            smnArtR_ = {}; strncpy(smnArtKey_, av->img, sizeof(smnArtKey_) - 1); smnArtKey_[sizeof(smnArtKey_) - 1] = 0;
        }
        char rel[64]; _snprintf(rel, sizeof(rel), "assets\\smn\\%s.raw", av->img); rel[sizeof(rel) - 1] = 0;
        art = ensure_raw_tex(dev, smnArt_, smnArtR_, plugin_path_r(rel), SMN_ART, SMN_ART);
    }

    // ---- the sizes ----
    const float pad = snap((C.petBox.on ? 8.0f : 2.0f) * S);
    const float lineH = snap(16.0f * S), gap = snap(5.0f * S), pactH = snap(14.0f * S), cellH = snap(18.0f * S);
    const float discD = snap((stacked ? 96.0f : 76.0f) * S * k);
    const float boxW = snap((stacked ? 230.0f : 260.0f) * S);
    const float topGap = snap(6.0f * S);
    static const int JA_COLS = 2;

    // the rows an element takes ; `all` : every element present (the frame)
    struct Rows { bool disc, name, vitals, dist; int jaCells, pacts, spellLines; bool mp, pactHdrR, pactHdrW; };
    Font* fp = smn_font(f, SMN_PACT);
    const float zp = smn_sz(SMN_PACT, 10.5f) * S;
    auto rowsFor = [&](const SmnState& x) -> Rows {
        Rows r = {};
        const SmnAvatarRow* a = (x.avatar >= 0 && x.avatar < SMN_AVATARS_N) ? &SMN_AVATARS[x.avatar] : 0;
        r.disc   = x.hasPet && (C.smnPortrait || C.smnHp || C.smnTp);   // no avatar out : no empty disc
        r.name   = C.smnName != 0;
        r.vitals = (C.smnHp || C.smnTp) && x.hasPet;
        r.dist   = C.smnDist && x.hasPet && x.dist >= 0.0f;
        for (int i = 0; i < x.nja; ++i) {
            const bool bp = x.jas[i].abil == 91 || x.jas[i].abil == 172;
            if (bp ? C.smnBp : C.smnJa) ++r.jaCells;
        }
        r.mp = C.smnMp != 0;
        if (C.smnPacts && a && a->spirit && a->spells) { char tmp[3][96]; r.spellLines = wrap_spells(fp, a->spells, zp, boxW - 2.0f * pad, tmp); }
        else if (C.smnPacts) {
            for (int i = 0; i < x.npacts; ++i) { if (x.pacts[i].ward) r.pactHdrW = true; else r.pactHdrR = true; }
            r.pacts = x.npacts;
        }
        return r;
    };
    // the pet's block in the box : stacked with its disc above, its text lines only ; else the disc beside its lines
    auto petLinesFor = [&](const Rows& r) -> int { return ((r.name || r.dist) ? 1 : 0) + (r.vitals ? 1 : 0) + ((!stacked && r.name && r.dist) ? 1 : 0); };
    auto petBlockFor = [&](const Rows& r) -> float {
        const float t = lineH * (float)petLinesFor(r);
        if (stacked && r.disc) return t;
        return (r.disc && discD > t) ? discD : t;
    };
    // the box's height ; stacked, without the disc above it
    auto boxHFor = [&](const Rows& r) -> float {
        float h = 0.0f; int blocks = 0;
        const float pet = petBlockFor(r);
        const float ja = cellH * (float)((r.jaCells + JA_COLS - 1) / JA_COLS);
        const float mp = r.mp ? lineH : 0.0f;
        const float pacts = r.spellLines ? lineH + pactH * (float)r.spellLines
                          : pactH * (float)(r.pacts + (r.pactHdrR ? 1 : 0) + (r.pactHdrW ? 1 : 0));
        const float parts[4] = { pet, ja, mp, pacts };
        for (float p : parts) if (p > 0.0f) { h += p; ++blocks; }
        return blocks ? pad * 2.0f + h + gap * (float)(blocks - 1) : 0.0f;
    };
    auto heightFor = [&](const Rows& r) -> float {
        const float b = boxHFor(r);
        if (!stacked || !r.disc) return b;
        return discD + (b > 0.0f ? topGap + b : 0.0f);
    };
    const Rows rows = rowsFor(s);
    const float frameH = heightFor(rowsFor(frameS));
    if (frameH <= 0.0f) return;
    const float totW = boxW;
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

    const bool top = stacked && rows.disc;
    const float by = top ? snap(py + discD + topGap) : py;
    const float bh = top ? totH - discD - topGap : totH;
    if (bh > 0.0f) draw_themed_box(dev, f.skin, px, by, boxW, bh, C.petBox, 1.0f, S);   // shared themed chrome

    Font* fn = smn_font(f, SMN_NAME); Font* fv = smn_font(f, SMN_VALUE); Font* fj = smn_font(f, SMN_JA);
    const float zn = smn_sz(SMN_NAME, 12.0f) * S, on = smn_ow(SMN_NAME, 1.0f) * S;
    const float zv = smn_sz(SMN_VALUE, 11.0f) * S, ov = smn_ow(SMN_VALUE, 1.0f) * S;
    const float zj = smn_sz(SMN_JA, 10.0f) * S, oj = smn_ow(SMN_JA, 1.0f) * S;
    const float op = smn_ow(SMN_PACT, 1.0f) * S;
    const float x0 = px + pad, xr = px + boxW - pad;
    const u32 elemC = av ? pet_elem_col(av->element) : 0xFFE8EEF2u;

    // ---- the disc centred on (cx, cy), diameter d : a dark face, the picture, then the HP ring from the top and the TP
    //      inner arc OVER it. The picture fits inside the TP arc ; the rings come last so a horn or a wing that reaches
    //      the square's corner never hides them (the player, 2026-11-10 : "les avatars passent au-dessus") ----
    auto drawDisc = [&](float cx, float cy, float d) {
        const float r = d * 0.5f;
        dColorQuadState(dev);
        disc(dev, cx, cy, r, 0xA00A0E16u);
        disc(dev, cx, cy, r * 0.80f, (elemC & 0x00FFFFFFu) | 0x30000000u);   // the element's glow behind the picture
        if (art) { const float as = snap(2.0f * (r - snap(11.0f * S))); if (as > 0.0f) pet_tex_quad(dev, art, cx - as * 0.5f, cy - as * 0.5f, as, as, 0.0f, 1.0f, 0xFFFFFFFFu); }
        dColorQuadState(dev);
        if (s.hasPet && C.smnHp) {
            const u32 hc = s.hpp > 50 ? 0xFF6BD07Au : (s.hpp > 25 ? 0xFFE8C45Au : 0xFFFF6A5Au);
            const float hr = r - snap(3.0f * S), hw = snap(4.0f * S);
            ring_sector(dev, cx, cy, hr - hw * 0.5f, hr + hw * 0.5f, 0.0f, SMN_TAU, 0xC0262C38u);
            if (s.hpp > 0) ring_sector(dev, cx, cy, hr - hw * 0.5f, hr + hw * 0.5f, -SMN_TAU * 0.25f, -SMN_TAU * 0.25f + SMN_TAU * (float)s.hpp / 100.0f, hc);
        }
        if (s.hasPet && C.smnTp && s.tp > 0) {
            const float tr = r - snap(9.0f * S), tw = snap(2.5f * S), tf = s.tp > 3000 ? 1.0f : (float)s.tp / 3000.0f;
            ring_sector(dev, cx, cy, tr - tw * 0.5f, tr + tw * 0.5f, -SMN_TAU * 0.25f, -SMN_TAU * 0.25f + SMN_TAU * tf, pet_tp_col(s.tp));
        }
    };
    auto separator = [&](float y) {
        dColorQuadState(dev);
        rrect(dev, x0, snap(y - gap * 0.5f), boxW - 2.0f * pad, 1.0f, 0.0f, 0x30FFFFFFu, 0x30FFFFFFu, 0.0f);
    };
    char hpTxt[32] = {};
    if (s.hasPet) {
        if (C.smnHp && C.smnTp) _snprintf(hpTxt, sizeof(hpTxt), "HP %d%%   TP %d", s.hpp, s.tp);
        else if (C.smnHp)       _snprintf(hpTxt, sizeof(hpTxt), "HP %d%%", s.hpp);
        else if (C.smnTp)       _snprintf(hpTxt, sizeof(hpTxt), "TP %d", s.tp);
        hpTxt[sizeof(hpTxt) - 1] = 0;
    }
    const char* petName = s.hasPet ? (av ? av->en : (s.name[0] ? s.name : "Pet")) : "No avatar";
    const u32 nameCol = s.hasPet ? smn_col(SMN_NAME, elemC) : 0xFF8C94A0u;

    float y = by + pad;
    bool first = true;
    auto nextBlock = [&]() { if (!first) { y += gap; separator(y); } first = false; };

    if (top) {
        // ---- stacked : the disc above the box ; in the box, the name and distance, then HP / TP ----
        drawDisc(snap(px + boxW * 0.5f), snap(py + discD * 0.5f), discD);
        if (rows.name || rows.dist || rows.vitals) {
            nextBlock();
            if (rows.name || rows.dist) {
                const float cy = y + lineH * 0.5f;
                float rx = xr;
                if (rows.dist) {
                    char db[16]; _snprintf(db, sizeof(db), "%.1fy", s.dist > 999.0f ? 999.0f : s.dist); db[sizeof(db) - 1] = 0;
                    rx -= fv->measure(db, zv);
                    fv->begin(dev); fv->draw_lc(dev, rx, cy, db, zv, smn_col(SMN_VALUE, 0xFFB8C0C8u), 0xC0000000u, ov);
                    rx -= snap(8.0f * S);
                }
                if (rows.name) { char nm[32]; pet_fit_cut(fn, petName, zn, rx - x0, nm, sizeof(nm)); fn->begin(dev); fn->draw_lc(dev, x0, cy, nm, zn, nameCol, 0xC0000000u, on); }
                y += lineH;
            }
            if (rows.vitals) { fv->begin(dev); fv->draw_lc(dev, x0, y + lineH * 0.5f, hpTxt, zv, smn_col(SMN_VALUE, 0xFFE8EEF2u), 0xC0000000u, ov); y += lineH; }
        }
    } else if (petBlockFor(rows) > 0.0f) {
        // ---- side (or stacked with the disc off) : the disc on the left, its name, HP / TP and distance on the right ----
        nextBlock();
        const float blockH = petBlockFor(rows);
        float tx = x0;
        if (rows.disc) { drawDisc(snap(x0 + discD * 0.5f), snap(y + blockH * 0.5f), discD); tx = x0 + discD + snap(8.0f * S); }
        const float tw = xr - tx;
        float ly = snap(y + (blockH - lineH * (float)petLinesFor(rows)) * 0.5f);
        auto line = [&](Font* fo, float z, float o, const char* t, u32 col) {
            char cut[40]; pet_fit_cut(fo, t, z, tw, cut, sizeof(cut));
            fo->begin(dev); fo->draw_lc(dev, tx, ly + lineH * 0.5f, cut, z, col, 0xC0000000u, o); ly += lineH;
        };
        if (rows.name) line(fn, zn, on, petName, nameCol);
        if (rows.vitals) line(fv, zv, ov, hpTxt, smn_col(SMN_VALUE, 0xFFE8EEF2u));
        if (rows.dist) { char db[16]; _snprintf(db, sizeof(db), "%.1fy", s.dist > 999.0f ? 999.0f : s.dist); db[sizeof(db) - 1] = 0; line(fv, zv, ov, db, smn_col(SMN_VALUE, 0xFFB8C0C8u)); }
        y += blockH;
    }

    // ---- Rage / Ward and the job abilities : a grid of (icon,) name, then "ok" or the recast ; the one whose effect is
    //      on the player in gold on a lit pill ----
    if (rows.jaCells > 0) {
        nextBlock();
        const PetJa* cells[SmnState::JA_MAX]; int nc = 0;
        for (int i = 0; i < s.nja; ++i) {
            const bool bp = s.jas[i].abil == 91 || s.jas[i].abil == 172;
            if (bp ? C.smnBp : C.smnJa) cells[nc++] = &s.jas[i];
        }
        y += pet_ja_grid(dev, fj, zj, oj, smn_col(SMN_JA, 0xFFE8EEF2u), x0, y, boxW - 2.0f * pad, cellH, 14.0f * S * k, S, cells, nc, JA_COLS, sample);
    }

    // ---- MP ----
    if (rows.mp) {
        nextBlock();
        const float cy = y + lineH * 0.5f, gh = snap(7.0f * S), sp = snap(6.0f * S);
        char mb[12]; sprintf(mb, "%d", s.mp);
        fv->begin(dev);
        const float lw = fv->draw_lc(dev, x0, cy, "MP", zv, 0xFF8C94A0u, 0xC0000000u, ov);
        const float vw = fv->measure("00000", zv);
        const float gx = x0 + lw + sp, gw = xr - vw - sp - gx;
        pet_gauge(dev, gx, snap(cy - gh * 0.5f), gw, gh, s.mpp / 100.0f, 0xFF6FA8FFu);
        fv->begin(dev);
        fv->draw_lc(dev, xr - fv->measure(mb, zv), cy, mb, zv, smn_col(SMN_VALUE, 0xFFE8EEF2u), 0xC0000000u, ov);
        y += lineH;
    }

    // ---- the pacts (Rage, then Ward) : name, a Rage's skillchain opening, the MP it costs (red : more than the player
    //      has) ; a spirit : its spells ----
    if (rows.spellLines > 0 && av) {
        nextBlock();
        fv->begin(dev); fv->draw_lc(dev, x0, y + lineH * 0.5f, "Casts on its own", zv, 0xFF8C94A0u, 0xC0000000u, ov); y += lineH;
        char ln[3][96]; const int n = wrap_spells(fp, av->spells, zp, boxW - 2.0f * pad, ln);
        fp->begin(dev);
        for (int i = 0; i < n; ++i) { fp->draw_lc(dev, x0, y + pactH * 0.5f, ln[i], zp, smn_col(SMN_PACT, 0xFFE8EEF2u), 0xC0000000u, op); y += pactH; }
    } else if (rows.pacts > 0) {
        nextBlock();
        const float mpW = fp->measure("0000", zp), scW = snap(78.0f * S);
        for (int pass = 0; pass < 2; ++pass) {
            const bool ward = pass == 1;
            if (ward ? !rows.pactHdrW : !rows.pactHdrR) continue;
            fp->begin(dev);
            fp->draw_lc(dev, x0, y + pactH * 0.5f, ward ? "Blood Pact: Ward" : "Blood Pact: Rage", zp, 0xFF8C94A0u, 0xC0000000u, op);
            y += pactH;
            for (int i = 0; i < s.npacts; ++i) {
                const SmnPact& p = s.pacts[i];
                if (p.ward != ward) continue;
                const float cy = y + pactH * 0.5f;
                char mb[12]; sprintf(mb, "%d", p.mp);
                const bool short_ = p.mp > s.mp;
                fp->begin(dev);
                fp->draw_lc(dev, xr - fp->measure(mb, zp), cy, mb, zp, short_ ? 0xFFFF6A5Au : smn_col(SMN_PACT, 0xFFB8C0C8u), 0xC0000000u, op);
                float nameW = xr - mpW - snap(6.0f * S) - (x0 + snap(6.0f * S));
                const int sc = ward ? -1 : pact_sc(p.abil);
                if (sc >= 0) {
                    const float sx = xr - mpW - snap(6.0f * S) - scW;
                    char sb[20]; pet_fit_cut(fp, scprop_name(sc), zp * 0.9f, scW, sb, sizeof(sb));
                    fp->draw_lc(dev, sx, cy, sb, zp * 0.9f, scprop_color(sc), 0xC0000000u, op);
                    nameW = sx - snap(4.0f * S) - (x0 + snap(6.0f * S));
                }
                char nm[32]; pet_fit_cut(fp, p.en, zp, nameW, nm, sizeof(nm));
                fp->draw_lc(dev, x0 + snap(6.0f * S), cy, nm, zp, smn_col(SMN_PACT, 0xFFE8EEF2u), 0xC0000000u, op);
                y += pactH;
            }
        }
    }
}

} // namespace aio
