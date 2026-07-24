// hud_skillchains.cpp -- split out of hud.cpp (pure move). Skillchains box renderer.
#include "ui/hud.h"
#include "ui/hud_internal.h"
#include "model/ui_config.h"
#include "ui/text_style.h"
#include "ui/box_style.h"
#include "model/party_state.h"
#include "gfx/draw.h"
#include "gfx/d3d.h"
#include "model/skillchain.h"
#include "model/weapon_skills_gen.h"
#include "model/spells_gen.h"
#include "model/abilities_gen.h"
#include "model/mobskills_gen.h"
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>

namespace aio {

// ============================ SKILLCHAINS box ============================
// per-element typography helpers (mirror the minimap clock's mm_* ) : each text element (Title / Timer / Step /
// Property / WS list) has its own face / size / outline / bold-italic-caps / colour from ui_config().scText[].
static Font* sc_font(const Frame& f, int e) { return te_font(f, ui_config().scText[e]); }
static inline float sc_sz(int e, float base) { return te_sz(ui_config().scText[e], base); }
static inline float sc_ow(int e, float base) { return te_ow(ui_config().scText[e], base); }
static inline u32   sc_col(int e, u32 base)  { return te_col(ui_config().scText[e], base); }
static const char*  sc_up(int e, const char* s, char* buf, int cap) { return te_upper(ui_config().scText[e], s, buf, cap); }   // UPPERCASE into buf if the element wants CAPS
// a small VECTOR "->" arrow, vertically centred at cy (the font atlas is Latin-1 only, so U+2192 can't be baked
// like the original addon's "→"). Caller is in colour-quad state ; returns the width consumed.
static float sc_arrow(u32 dev, float x, float cy, float sz, u32 col) {
    const float w = sz * 0.80f, th = 1.3f + sz * 0.06f, hs = sz * 0.26f;
    seg_soft(dev, x, cy, x + w, cy, th, col);                          // shaft
    seg_soft(dev, x + w - hs, cy - hs, x + w + 0.5f, cy, th, col);     // upper head
    seg_soft(dev, x + w - hs, cy + hs, x + w + 0.5f, cy, th, col);     // lower head
    return w + hs * 0.5f;
}

// A small themed box on the CURRENT target's active skillchain : title / burst timer (Wait->Go!->Burst) /
// "Step: N > <closing move>" / "[property] (burst elements)" / the WS continuation list. Data from
// party().skillchain_for() ; shown while the resonance window is live, or a looping sample in preview/edit.
// Core renderer, extracted as a FREE function so the config PREVIEW and the Help sample reuse the EXACT same
// config-aware draw with no Hud instance.
void skillchains_draw(const Frame& f, bool preview, float ovX, float ovY, float ovS, float screenW, float screenH,
                      bool measureOnly = false, float* outW = 0, float* outH = 0) {
    const UiConfig& C = ui_config();
    if (!C.scShow) return;
    const bool editing = C.editLayout && !preview;
    u32 dev = f.dev;
    if (!f.font && !f.fonts) return;

    // ---- gather the display data (live resonance, or a sample in preview/edit) ----
    int step, phase, nProp; unsigned char props[3]; bool formed; const char* actName; float timerSec;
    if (preview || editing) {
        step = 2; props[0] = SCP_Fusion; nProp = 1; formed = true; actName = "Savage Blade"; phase = 1;
        timerSec = preview ? (5.0f - (float)(GetTickCount() % 5000u) / 1000.0f) : 5.0f;   // preview counts down + loops
        if (timerSec < 0.1f) timerSec = 5.0f;
    } else {
        if (!f.game) return;
        const GameState& g = *f.game;
        // Which mob's chain to show. ALWAYS your current cursor target <t> (its own chain). The BROADER fallbacks are
        // ALL gated by config scNearby (default on) : your BATTLE target <bt> (engaged, reticle dropped) then the
        // newest LIVE chain on any nearby mob -- so a healer/support/observer sees a party member's SC. With scNearby
        // OFF the box is STRICT : only your <t> chain (the fallbacks were what made it show while engaged / not
        // targeting -- exactly the "box appears even when the toggle is off" case). The broader steps never fire while
        // your reticle is on a DIFFERENT mob (targetingMob), so the box can't linger on a mob you targeted away from.
        const bool targetingMob = g.target.valid && g.target.spawnType == 0x10;
        const Resonating* r = party().skillchain_for(g.target.valid ? g.target.id : 0);
        if (!r && !targetingMob && C.scNearby) {
            if (g.battleTargetId)   r = party().skillchain_for(g.battleTargetId);
            if (!r)                 r = party().skillchain_newest_live();
        }
        if (!r) return;
        const unsigned now = GetTickCount();
        if (now >= r->endMs && (now - r->endMs) > 250u) return;     // past the window (+ short grace) -> hide
        step = r->step; formed = r->formed != 0;
        nProp = r->nProp > 3 ? 3 : r->nProp; for (int i = 0; i < nProp; ++i) props[i] = r->prop[i];
        if (nProp <= 0) { props[0] = SCP_Fusion; nProp = 1; }
        // timer : NEVER subtract two unsigned ms when the right side is bigger (it underflows to a huge value ->
        // the "94548988.54" garbage during the grace) -> clamp to 0 once we're at/after endMs.
        // A CLOSED chain (Lv.4 / step>5) goes straight to BURST for the whole remaining window -- NO Wait/Go phase and
        // NO continuation list (the reference addon's `if closed then disp_info=''`). Without this the closed chain
        // still passed through its post-close Wait window (phase 0), which REBUILDS the continuation list -> the stale
        // "Ukko's Fury -> Light" lingered a moment before the box reached phase 2 and cleared it.
        if      (r->closed)        { phase = 2; timerSec = (now < r->endMs) ? (float)(r->endMs - now) / 1000.0f : 0.0f; }
        else if (now >= r->endMs)  { phase = 1; timerSec = 0.0f; }
        else if (now < r->delayMs) { phase = 0; timerSec = (float)(r->delayMs - now) / 1000.0f; }
        else                       { phase = 1; timerSec = (float)(r->endMs - now) / 1000.0f; }
        actName = "";
        if (r->resource == SCR_WS)         { const WSRow* w = ws_info(r->actionId);        if (w) actName = w->en; }
        else if (r->resource == SCR_SPELL) { const SpellRow* s = spell_info(r->actionId);   if (s) actName = s->en; }
        else if (r->resource == SCR_MOB)   { const MobSkillRow* m = mobskill_info(r->actionId); if (m) actName = m->en; }
        else if (r->resource == SCR_JA)    { const AbilRow* a = abil_info(r->actionId);     if (a) actName = a->en; }
    }

    // ---- continuation list : YOUR usable weapon skills that extend the chain ("Name > Lv.X property").
    // Rebuilt only when the resonance changes (a get_abilities read + check_props per usable WS). No list once
    // the chain is CLOSED (lvl4 / step>5 = burst only), mirroring the addon. ----
    struct ScLine { char name[28]; unsigned char lvl, prop; };
    static ScLine list[16]; static int listN = 0; static unsigned listKey = 0;
    if (preview || editing) {
        static const struct { const char* n; int l; int p; } SMP[3] = {
            { "Savage Blade", 2, SCP_Fusion }, { "Requiescat", 2, SCP_Gravitation }, { "Resolution", 1, SCP_Scission } };
        listN = 3;
        for (int i = 0; i < 3; ++i) { int c = 0; for (; SMP[i].n[c] && c < 27; ++c) list[i].name[c] = SMP[i].n[c]; list[i].name[c] = 0; list[i].lvl = (unsigned char)SMP[i].l; list[i].prop = (unsigned char)SMP[i].p; }
    } else if (phase == 2) {
        listN = 0; listKey = 0;                                 // closed chain -> burst only, no continuation list
    } else {
        const int mjob = f.game->me.mjob;
        // AEONIC : if your equipped MAIN or RANGED is an Aeonic weapon, a WS bound to it gains its aeonic opening
        // property (Light/Darkness) -> it can form Radiance/Umbra. Reliable only for YOU : you can read your own gear ;
        // for another player an Aeonic is indistinguishable from a look-alike, so their aeonic props are never assumed.
        int selfAeonic = sc_aeonic_weapon_of_item(f.game->equip.id[0]);
        if (selfAeonic < 0) selfAeonic = sc_aeonic_weapon_of_item(f.game->equip.id[2]);
        // Aftermath gate (the reference aeonic_am) : an Aeonic's Radiance/Umbra can only close from a high enough step
        // for your AM level (270/271/272 = Lv.1/2/3 -> AM3 from step 1, AM2 from step 2, AM1 from step 3). No AM = no
        // Radiance/Umbra offered at all. `step` here is the CURRENT resonance step ; the continuation would be step+... .
        int amLvl = 0; for (int i = 0; i < f.game->nbuff; ++i) { const int s = f.game->buffs[i]; if (s == 272) { amLvl = 3; break; } else if (s == 271) { if (amLvl < 2) amLvl = 2; } else if (s == 270) { if (amLvl < 1) amLvl = 1; } }
        const bool aeonicOk = (selfAeonic >= 0) && (amLvl >= 1) && ((3 - amLvl) < step);
        // Hash ALL active opening properties + their count, not just props[0]. A new resonance that shares the
        // primary property but differs in props[1..2]/nProp (e.g. a lone WS with two props vs a closed chain leaving
        // one) reused the stale continuation list -> wrong "what continues" suggestions until the key happened to move.
        const unsigned key = f.game->target.id * 2654435761u + (unsigned)step * 131u + (unsigned)mjob * 97u
                           + (unsigned)props[0] * 7u + (unsigned)props[1] * 1013u + (unsigned)props[2] * 65537u + (unsigned)nProp * 40503u
                           + (unsigned)(selfAeonic + 1) * 277u + (unsigned)amLvl * 1201u;   // rebuild on weapon swap / AM change (aeonic on/off changes what continues)
        if (key != listKey) {
            listKey = key; listN = 0;
            // add a candidate move (weapon skill / job ability / SCH element) IF it continues the active chain.
            auto tryAdd = [&](int resource, unsigned id, const char* nm) {
                if (listN >= 16) return;
                const SkillRow* sk = sc_skill_lookup(resource, id); if (!sk) return;
                // wsAeonic : THIS candidate is the WS of YOUR equipped Aeonic AND your Aftermath allows Radiance/Umbra.
                // Only then does it gain its aeonic opening property and can UPGRADE a Light/Darkness Lv.4 close to
                // Radiance/Umbra. An Empyrean (Ukko's Fury) or any non-Aeonic WS never does, even with its own Aftermath.
                const bool wsAeonic = (aeonicOk && sk->aeonic < SCP_N && sk->weapon != 255 && (int)sk->weapon == selfAeonic);
                unsigned char cp[3]; int ncp = 0; for (int j = 0; j < 3 && sk->prop[j] < SCP_N; ++j) cp[ncp++] = sk->prop[j];
                if (wsAeonic && ncp < 3) cp[ncp++] = sk->aeonic;   // add the aeonic opening property (Light/Darkness) so the aeonic WS can form a Light/Darkness chain
                int lv = 0, rs = 0;
                if (sc_check_props(props, nProp, cp, ncp, lv, rs)) {
                    if (wsAeonic) { if (rs == SCP_Light) rs = SCP_Radiance; else if (rs == SCP_Darkness) rs = SCP_Umbra; }   // aeonic close -> Radiance/Umbra ; otherwise the Light/Darkness Lv.4 stands
                    ScLine& L = list[listN++]; if (!nm) nm = "";
                    int c = 0; for (; nm[c] && c < 27; ++c) L.name[c] = nm[c]; L.name[c] = 0;
                    L.lvl = (unsigned char)lv; L.prop = (unsigned char)rs;
                }
            };
            unsigned short ws[128]; const int nws = read_usable_weapon_skills(ws, 128);                 // everyone : weapon skills
            for (int i = 0; i < nws; ++i) { const WSRow* w = ws_info(ws[i]); tryAdd(SCR_WS, ws[i], w ? w->en : 0); }
            unsigned short ja[256]; const int nja = read_usable_job_abilities(ja, 256);                 // BST/SMN : pet ready moves (self-gated on the pet being out) + any skillchaining JA
            for (int i = 0; i < nja; ++i) { const AbilRow* a = abil_info(ja[i]); tryAdd(SCR_JA, ja[i], a ? a->en : 0); }
            if (mjob == 16) { unsigned short bs[24]; const int nbs = read_blu_spells(bs, 24);            // BLU : your currently-SET blue magic
                for (int i = 0; i < nbs; ++i) { const SpellRow* s = spell_info(bs[i]); tryAdd(SCR_SPELL, bs[i], s ? s->en : 0); } }
            if (mjob == 20) for (int el = 0; el < 8; ++el) tryAdd(SCR_ELEM, el, scelem_name(el));        // SCH : the 8 elemental magics are always castable
            for (int i = 1; i < listN; ++i) { ScLine t = list[i]; int j = i - 1; while (j >= 0 && list[j].lvl < t.lvl) { list[j + 1] = list[j]; --j; } list[j + 1] = t; }   // highest level first
        }
    }

    float sscl = C.scScale; if (sscl < 0.5f) sscl = 0.5f; if (sscl > 2.0f) sscl = 2.0f;
    const float S = (ovS > 0.0f) ? ovS : (screenH / 1000.0f) * sscl;
    const float pad = (ui_config().scBox.on ? 10.0f : 0.0f) * S, lineH = 20.0f * S, titleH = 24.0f * S;   // no box chrome -> hug the content
    const u32 dim = 0xFFC8D2E6u, white = 0xFFEAF0FFu, gold = 0xFFE8C55Au, strk = 0xFF000000u;
    // per-element fonts / sizes / outlines
    Font* fTitle = sc_font(f, SC_TITLE); Font* fTimer = sc_font(f, SC_TIMER); Font* fStep = sc_font(f, SC_STEP);
    Font* fProp = sc_font(f, SC_PROP);   Font* fList = sc_font(f, SC_LIST);
    const float zTitle = sc_sz(SC_TITLE, 16.0f) * S, zTimer = sc_sz(SC_TIMER, 14.0f) * S, zStep = sc_sz(SC_STEP, 14.0f) * S, zProp = sc_sz(SC_PROP, 14.0f) * S, zList = sc_sz(SC_LIST, 14.0f) * S;
    const float oTitle = sc_ow(SC_TITLE, 1.2f) * S, oTimer = sc_ow(SC_TIMER, 1.2f) * S, oStep = sc_ow(SC_STEP, 1.2f) * S, oProp = sc_ow(SC_PROP, 1.2f) * S, oList = sc_ow(SC_LIST, 1.2f) * S;

    // ---- text lines (+ per-element CAPS + colour) ----
    char l2[24]; u32 tcol;
    if      (phase == 0) { sprintf(l2, "Wait  %.1f", timerSec); tcol = 0xFFFF6060u; }
    else if (phase == 1) { sprintf(l2, "Go!  %.1f",  timerSec); tcol = 0xFF66FF66u; }
    else                 { sprintf(l2, "Burst %.0f", timerSec); tcol = white; }
    char l3pre[24]; sprintf(l3pre, "Step: %d", step);
    const char* aname = (actName && actName[0]) ? actName : "?";
    char up0[24], up2[24], up3[32], up3b[32], pbuf[24];
    const char* title = sc_up(SC_TITLE, "Skillchains", up0, 24);
    const char* l2u = sc_up(SC_TIMER, l2, up2, 24);
    const char* l3preU = sc_up(SC_STEP, l3pre, up3, 32);
    const char* l3name = sc_up(SC_STEP, aname, up3b, 32);
    const float stepArrowW = zStep * 0.93f, stepGap = 4.0f * S;
    const float stepW = fStep->measure(l3preU, zStep) + stepGap + stepArrowW + stepGap + fStep->measure(l3name, zStep);
    const float listArrowW = zList * 0.93f, listGap = 4.0f * S;
    const u32 titleCol = sc_col(SC_TITLE, gold), timerCol = sc_col(SC_TIMER, tcol), stepCol = sc_col(SC_STEP, white), listNameCol = sc_col(SC_LIST, white);
    const u32 propOver = ui_config().scText[SC_PROP].colorOn ? ui_config().scText[SC_PROP].color : 0;   // 0 = keep semantic per-segment
    const SCInfo& si = sc_info(props[0]);   // elements only meaningful once formed

    // ---- measure -> box size. Property line = "[p1, p2, ...]" (+ " (e1, e2)" once a chain has FORMED). ----
    float w4 = fProp->measure("[", zProp) + fProp->measure("]", zProp);
    for (int i = 0; i < nProp; ++i) { w4 += fProp->measure(sc_up(SC_PROP, scprop_name(props[i]), pbuf, 24), zProp); if (i + 1 < nProp) w4 += fProp->measure(" · ", zProp); }
    if (formed) { w4 += fProp->measure(" (", zProp) + fProp->measure(")", zProp);
        for (int i = 0; i < si.nElem; ++i) { w4 += fProp->measure(sc_up(SC_PROP, scelem_name(si.elem[i]), pbuf, 24), zProp); if (i + 1 < si.nElem) w4 += fProp->measure(" · ", zProp); } }
    const bool shTitle = C.scTitle != 0, shTimer = C.scTimer != 0, shStep = C.scStep != 0, shProps = C.scProps != 0;
    const int  shownList = C.scList ? listN : 0;
    float scGap = C.scListGap; if (scGap < 0.6f) scGap = 0.6f; if (scGap > 3.0f) scGap = 3.0f;
    const float listPit = lineH * scGap;   // per-WS row pitch in the continuation list (config: WS spacing slider)
    float bw = shTitle ? fTitle->measure(title, zTitle) : 0.0f;
    if (shTimer) { const float a = fTimer->measure(l2u, zTimer); if (a > bw) bw = a; }
    if (shStep && stepW > bw) bw = stepW;
    if (shProps && w4 > bw) bw = w4;
    // continuation list = 3 ALIGNED columns : [name]  ->  [Lv.X]  [property]. Column widths = the max across rows,
    // so the arrow / level / property line up vertically (reused in the draw pass below).
    const float lColGap = 8.0f * S;
    float lcName = 0.0f, lcLvl = 0.0f, lcProp = 0.0f;
    for (int i = 0; i < shownList; ++i) {
        char nb[28], pb2[24], lvb[10]; sprintf(lvb, "Lv.%d", list[i].lvl);
        const float wN = fList->measure(sc_up(SC_LIST, list[i].name, nb, 28), zList);
        const float wL = fList->measure(lvb, zList);
        const float wP = fList->measure(sc_up(SC_LIST, scprop_name(list[i].prop), pb2, 24), zList);
        if (wN > lcName) lcName = wN; if (wL > lcLvl) lcLvl = wL; if (wP > lcProp) lcProp = wP;
    }
    if (shownList > 0) { const float lw = lcName + listGap + listArrowW + listGap + lcLvl + lColGap + lcProp; if (lw > bw) bw = lw; }
    if (bw < 40.0f * S) bw = 40.0f * S;   // floor : keep a sane minimum even with everything but one line off
    const int nBody = (shTimer ? 1 : 0) + (shStep ? 1 : 0) + (shProps ? 1 : 0);
    const float listH = (shownList > 0) ? (7.0f * S + shownList * listPit) : 0.0f;
    const float boxW = bw + 2.0f * pad, boxH = (shTitle ? titleH : 0.0f) + nBody * lineH + listH + 2.0f * pad;
    if (measureOnly) { if (outW) *outW = boxW; if (outH) *outH = boxH; return; }   // Help scale-to-fit : report dims, don't draw

    // ---- position (+ edit-mode drag). scX = the box's HORIZONTAL CENTRE (so it grows symmetrically as the
    // content width changes -> the anchor point stays put), scY = its TOP (it grows downward as the list grows). ----
    float px, py;
    if (ovS > 0.0f) { px = snap((ovX - boxW * 0.5f)); py = snap((ovY - boxH * 0.5f)); }        // preview : centre on the given point
    else            { px = snap(C.scX * screenW - boxW * 0.5f); py = snap(C.scY * screenH); }
    if (editing) { static EditBox g_scEdit; box_edit(f, g_scEdit, EDITBOX_SKILLCHAIN, px, py, boxW, boxH, ui_config().scScale, ui_config().scX, ui_config().scY, 1); }

    // ---- box chrome : dark rounded panel + gold border (Help-box look) ----
    dColorQuadState(dev);
    const float r = 6.0f * S;
    draw_themed_box(dev, f.skin, px, py, boxW, boxH, ui_config().scBox, 1.0f, S);   // shared themed chrome (frame/transp/theme)

    const float cx = px + boxW * 0.5f;
    float cy = py + pad;
    if (shTitle) { cy += titleH * 0.5f; fTitle->begin(dev); fTitle->draw_c(dev, cx, cy, title, zTitle, titleCol, strk, oTitle); cy += titleH * 0.5f + lineH * 0.5f; }
    else cy += lineH * 0.5f;   // no title -> first body line starts at the top pad
    if (shTimer) { fTimer->begin(dev); fTimer->draw_c(dev, cx, cy, l2u, zTimer, timerCol, strk, oTimer); cy += lineH; }
    if (shStep)  {   // "Step: N  ->  <move>" with a VECTOR arrow (font can't bake U+2192), centred
        float sx = cx - stepW * 0.5f;
        fStep->begin(dev); fStep->draw_lc(dev, sx, cy, l3preU, zStep, stepCol, strk, oStep); sx += fStep->measure(l3preU, zStep) + stepGap;
        dColorQuadState(dev); sc_arrow(dev, sx, cy, zStep, stepCol); sx += stepArrowW + stepGap;
        fStep->begin(dev); fStep->draw_lc(dev, sx, cy, l3name, zStep, stepCol, strk, oStep);
        cy += lineH;
    }
    // property line : multi-colour "[prop] (elem, ...)" (semantic colours, or the element's custom colour if set).
    if (shProps) {
        fProp->begin(dev);
        char sb[24]; float sx = cx - w4 * 0.5f;
        #define SEG(str, col) { const char* _s = (str); fProp->draw_lc(dev, sx, cy, _s, zProp, (col), strk, oProp); sx += fProp->measure(_s, zProp); }
        SEG("[", dim);
        for (int i = 0; i < nProp; ++i) { SEG(sc_up(SC_PROP, scprop_name(props[i]), sb, 24), propOver ? propOver : scprop_color(props[i])); if (i + 1 < nProp) SEG(" · ", dim); }
        SEG("]", dim);
        if (formed) { SEG(" (", dim);
            for (int i = 0; i < si.nElem; ++i) { SEG(sc_up(SC_PROP, scelem_name(si.elem[i]), sb, 24), propOver ? propOver : scelem_color(si.elem[i])); if (i + 1 < si.nElem) SEG(" · ", dim); }
            SEG(")", dim); }
        #undef SEG
        cy += lineH;
    }

    // ---- continuation list : 3 ALIGNED columns -> [name]  ->  [Lv.X]  [property]. Fixed column x from the max
    // widths measured above, so the arrow / level / property line up down the list (name in the List colour,
    // level dim, result in its semantic colour). ----
    if (shownList > 0) {
        cy += 6.0f * S;   // small gap above the list
        const float lx = px + pad;                                   // col 1 : name
        const float arrowX = lx + lcName + listGap;                  //         arrow
        const float lvlX   = arrowX + listArrowW + listGap;          // col 2 : Lv.X
        const float propX  = lvlX + lcLvl + lColGap;                 // col 3 : property
        char nb[28], pb2[24], lvb[10];
        for (int i = 0; i < shownList; ++i) {
            const char* nm = sc_up(SC_LIST, list[i].name, nb, 28);
            fList->begin(dev); fList->draw_lc(dev, lx, cy, nm, zList, listNameCol, strk, oList);
            dColorQuadState(dev); sc_arrow(dev, arrowX, cy, zList, dim);
            sprintf(lvb, "Lv.%d", list[i].lvl);
            fList->begin(dev); fList->draw_lc(dev, lvlX, cy, lvb, zList, dim, strk, oList);
            const char* pp = sc_up(SC_LIST, scprop_name(list[i].prop), pb2, 24);
            fList->draw_lc(dev, propX, cy, pp, zList, scprop_color(list[i].prop), strk, oList);
            cy += listPit;
        }
    }
}

// Live / edit path : the Hud draws the box at its configured screen position.
void Hud::draw_skillchains(const Frame& f, bool preview, float ovX, float ovY, float ovS) {
    skillchains_draw(f, preview, ovX, ovY, ovS, (float)screenW_, (float)screenH_);
}

// Help sample : the REAL Skillchains box in preview mode (config-aware), centred at (cx,cy) at scale `s`.
void skillchains_help_box(const Frame& f, float cx, float cy, float s) {
    skillchains_draw(f, true, cx, cy, s, 0.0f, 0.0f);
}

// Help scale-to-fit : measure at scale 1 (linear in S), pick the largest scale that fits availW (capped at maxScale).
void skillchains_help_fit(const Frame& f, float availW, float maxScale, float& outScale, float& outH) {
    float bw = 0.0f, bh = 0.0f;
    skillchains_draw(f, true, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, true, &bw, &bh);
    float s = (bw > 1.0f) ? (availW / bw) : maxScale;
    if (s > maxScale) s = maxScale; if (s < 0.6f) s = 0.6f;
    outScale = s; outH = bh * s;
}

} // namespace aio
