// ep_config.cpp -- the EmpyPop module's settings panel (//aio config -> "EmpyPop").
//
// Each control is keyed by CTRL_ID (file:line hash, see config_controls.h) -- no hand-numbered uids.
// Reuses the shared sub-section flags catOpen_ [6] (Display) / [5] (Text), like every floating-box module.
// The box is placed in //aio edit like every other box.
#include "ui/config_page.h"
#include "ui/config_controls.h"    // cat_header / row_slider / row_selector / toggle_chip + palette
#include "ui/config_rows.h"        // ROW_BAND / ROW_NEXT
#include "ui/cfg_engine.h"         // the redesign : OptDef tables + the kit (draw_ep_v2)
#include "model/ui_config.h"
#include "model/nms_gen.h"         // NMS[] / NMS_N : the tracked-NM selector writes NMS[idx].key into epTrack
#include "gfx/font.h"
#include "gfx/draw.h"
#include <windows.h>               // lstrcpynA / lstrcmpiA : the epTrack key round-trip
#include <cstdio>
#include <cstring>                 // memset : the epTrack key, cleared before a new one

namespace aio {

void ConfigPage::draw_ep_config(u32 dev, Font* fo, const MouseState* mo, bool click,
                                float& ry, int& ri, float e,
                                float bandX, float bandW, float coX, float ctrlW,
                                float hdrX, float hdrW) {
    UiConfig& c = ui_config();

    // ===== sub-section : DISPLAY =====
    // The section FOLDS : cat_fold owns the eased progress, the clip and the cursor (config_controls.h).
    const float aF6_ = cat_fold(CTRL_ID, !ui_config().cfgAdv || (catOpen_[6]));
    cat_panel(dev, hdrX, ry, hdrW, cat_card_h(catH_[6], aF6_));   // the section IS a card, collapsed or not
    if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Display", "Affichage"), catOpen_[6], aF6_) && ui_config().cfgAdv) catOpen_[6] = !catOpen_[6];
    ROW_NEXT(42.0f)
    if (aF6_ > 0.0f) {
        const float top6_ = ry;
        cat_fold_clip(dev, hdrX, top6_, hdrW, catH_[6] * aF6_);
        ROW_TOGGLE(CTRL_ID, tr("Show", "Afficher"), c.epShow)   // Show
        { ROW_BAND(46.0f)   // Size (canonical : right after Show)
            const float lo = 0.50f, hi = 2.00f; char b[16]; sprintf(b, "%d%%", (int)(c.epScale * 100.0f + 0.5f));
            float v01 = (c.epScale - lo) / (hi - lo); v01 = clampf(v01, 0.0f, 1.0f);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Size", "Taille"), b, &v01)) { float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f; c.epScale = v < lo ? lo : (v > hi ? hi : v); }
        } ROW_NEXT(46.0f)
        ROW_GROW(CTRL_ID, UiConfig::GB_EP)   // which way the box grows as its content widens (ui/box_grow.h)
        cat_fold_end(dev, ry, top6_, catH_[6], aF6_);
    }   // end Display
    ry += snap(16.0f);                                 // air between this section and the next title bar
    // ===== sub-section : FRAME (the config grammar : Display / Frame / Content / Behaviour / Text, docs/architecture/config-panels.md) =====
if (ui_config().cfgAdv) {   // Advanced only (the config's depth, 2026-11-26)
    const float aF20_ = cat_fold(CTRL_ID, catOpen_[20]);
    cat_panel(dev, hdrX, ry, hdrW, cat_card_h(catH_[20], aF20_));
    if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Frame", "Cadre"), catOpen_[20], aF20_)) catOpen_[20] = !catOpen_[20];
    ROW_NEXT(42.0f)
    if (aF20_ > 0.0f) {
        const float top20_ = ry;
        cat_fold_clip(dev, hdrX, top20_, hdrW, catH_[20] * aF20_);
        draw_box_appearance(dev, fo, mo, click, ry, ri, e, bandX, bandW, coX, ctrlW, c.epBox);   // Box / Transparency / Theme / Hue / Luminosity
        cat_fold_end(dev, ry, top20_, catH_[20], aF20_);
    }   // end Frame
    ry += snap(16.0f);
}   // end Advanced only
    // ===== sub-section : CONTENT (the config grammar : Display / Frame / Content / Behaviour / Text, docs/architecture/config-panels.md) =====
    const float aF21_ = cat_fold(CTRL_ID, !ui_config().cfgAdv || (catOpen_[21]));
    cat_panel(dev, hdrX, ry, hdrW, cat_card_h(catH_[21], aF21_));
    if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Content", "Contenu"), catOpen_[21], aF21_) && ui_config().cfgAdv) catOpen_[21] = !catOpen_[21];
    ROW_NEXT(42.0f)
    if (aF21_ > 0.0f) {
        const float top21_ = ry;
        cat_fold_clip(dev, hdrX, top21_, hdrW, catH_[21] * aF21_);
        // ---- Tracked-NM picker : a grid of NAME buttons (same two-pass pattern as the job picker in
        //      tm_config). Selected = lit panel + white ring ; clicking one stores its KEY (not an index --
        //      that would re-point if the table grows) into epTrack AND turns the box ON, like //aio ept. ----
        { ROW_BAND(26.0f) fo->begin(dev);
          const char* t = tr("TRACK NM", "SUIVRE NM");
          const float tcx = coX + ctrlW * 0.5f, tw = fo->measure(t, snap(15.0f));
          fo->draw_c(dev, tcx, ry + yo + snap(13.0f), t, snap(15.0f), fa(C_GOLD), fa(C_STROKE), 1.3f);
          flat(dev, tcx - tw * 0.5f, ry + yo + snap(23.0f), tw, snap(1.5f), C_GOLD);   // gold underline
        } ROW_NEXT(32.0f)
        {
            int sel = -1; for (int i = 0; i < NMS_N; ++i) if (lstrcmpiA(c.epTrack, NMS[i].key) == 0) { sel = i; break; }
            // FEWER, WIDER columns than the 2-3 char job grid : NM display names are long ("Arch Dynamis Lord").
            const int perRow = 3; const float gap = snap(4.0f), cw = (ctrlW - (perRow - 1) * gap) / perRow, ch = snap(23.0f);
            for (int r = 0; r * perRow < NMS_N; ++r) {
                ROW_BAND(26.0f)
                // sub-pass A : panels + selection ring + click (colour quads)
                for (int col = 0; col < perRow; ++col) {
                    const int gi = r * perRow + col; if (gi >= NMS_N) break;
                    const bool seld = (gi == sel);
                    const float cx = coX + col * (cw + gap), cy = ry + yo;
                    const bool hov = inrect(mo, cx, cy, cw, ch);
                    rrect_fill(dev, cx, cy, cw, ch, snap(4.0f), seld ? C_ACCENTHI : (hov ? C_CTL_HOV_T : C_CTL_IDLE_T), seld ? C_ACCENT : (hov ? C_CTL_HOV_B : C_CTL_IDLE_B));
                    if (seld) outline(dev, cx, cy, cw, ch, 0xFFEAFBF9u);
                    if (hov && click) { lstrcpynA(c.epTrack, NMS[gi].key, (int)sizeof(c.epTrack)); c.epShow = 1; sel = gi; save_ui_config(); }
                }
                // sub-pass B : the NM NAMES (font ; dark text on the bright SELECTED panel, light otherwise)
                fo->begin(dev);
                for (int col = 0; col < perRow; ++col) {
                    const int gi = r * perRow + col; if (gi >= NMS_N) break;
                    const bool seld = (gi == sel);
                    const float cx = coX + col * (cw + gap), cy = ry + yo;
                    u32 tstk = C_STROKE;                                  // the selected tile is filled with the ACCENT,
                    const u32 tc = seld ? text_on_fill(lerpc(C_ACCENTHI, C_ACCENT, 0.5f), &tstk) : C_TEXT;   // which can be dark
                    fo->draw_c(dev, cx + cw * 0.5f, cy + ch * 0.5f, NMS[gi].en, snap(11.0f), fa(tc), fa(tstk), 1.0f);
                }
                ROW_NEXT(26.0f)
            }
            ry += snap(9.0f);   // breathing space after the grid, before the Collectable row
        }
        ROW_TOGGLE(CTRL_ID, tr("Collectable", "Collectable"), c.epColl)   // Collectable counter row
        { ROW_BAND(40.0f)   // note
            const float ty = ry + yo; fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + snap(16.0f), tr("Shows the pop + key items needed to spawn the chosen Abyssea NM.", "Montre les items de pop + key items pour faire appara\xC3\xAetre le NM Abyssea choisi."), snap(12.0f), fa(C_MUTE), fa(C_STROKE), 1.0f);
        } ROW_NEXT(40.0f)
        cat_fold_end(dev, ry, top21_, catH_[21], aF21_);
    }   // end Content
    ry += snap(16.0f);

    // ===== sub-section : TEXT (per-element typography) =====
    // The section FOLDS : cat_fold owns the eased progress, the clip and the cursor (config_controls.h).
if (ui_config().cfgAdv) {   // Advanced only (the config's depth, 2026-11-26)
    const float aF5_ = cat_fold(CTRL_ID, catOpen_[5]);
    cat_panel(dev, hdrX, ry, hdrW, cat_card_h(catH_[5], aF5_));   // the section IS a card, collapsed or not
    if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Text", "Texte"), catOpen_[5], aF5_)) catOpen_[5] = !catOpen_[5];
    ROW_NEXT(42.0f)
    if (aF5_ > 0.0f) {
        const float top5_ = ry;
        cat_fold_clip(dev, hdrX, top5_, hdrW, catH_[5] * aF5_);
        { ROW_BAND(52.0f)   // element selector
            const char* TLBL[EP_TE_COUNT] = { tr("Title", "Titre"), tr("Pop", "Pop"), tr("From", "De"), tr("Collectable", "Collectable") };
            int te = (cfgEpTextElem_ < 0 || cfgEpTextElem_ >= EP_TE_COUNT) ? 0 : cfgEpTextElem_;
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Element", "\xC3\x89l\xC3\xA9ment"), TLBL[te])) { cfgEpTextElem_ = wrap(te + d, EP_TE_COUNT); }
        }
        ROW_NEXT(52.0f)
        draw_text_style(dev, fo, mo, click, ry, ri, e, bandX, bandW, coX, ctrlW,
                        c.epText[(cfgEpTextElem_ < 0 || cfgEpTextElem_ >= EP_TE_COUNT) ? 0 : cfgEpTextElem_]);
        cat_fold_end(dev, ry, top5_, catH_[5], aF5_);
    }   // end Text

    ry += snap(16.0f);
}   // end Advanced only
    if (!ui_config().cfgAdv) SIMPLE_MORE   // Simple : where the rest is
    #undef ROW_BAND
    #undef ROW_NEXT
}


// ======== THE NEW EMPYPOP PAGE (the config redesign, milestone 3, 2026-11-26) ========
// Display / Frame / Content / Text, as the old page. The tracked NM : the name grid, as keys of the kit.
#define F_(name) [](UiConfig& c) -> void* { return &c.name; }
static const OptDef EP_DISPLAY[] = {
    { "Show", "Afficher", OK_TOGGLE, OL_SIMPLE, F_(epShow), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Size", "Taille", OK_PCT, OL_SIMPLE, F_(epScale), 0.5f, 2.0f, nullptr, nullptr, 0, nullptr, 0 },
    { "Grows toward", "Grandit vers", OK_GROW, OL_SIMPLE, nullptr, 0, 0, nullptr, nullptr, 0, nullptr, UiConfig::GB_EP },
};
static const OptDef EP_CONTENT[] = {
    { "Collectable", "Collectable", OK_TOGGLE, OL_SIMPLE, F_(epColl), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Shows the pop + key items needed to spawn the chosen Abyssea NM.", "Montre les items de pop + key items pour faire appara\xC3\xAetre le NM Abyssea choisi.", OK_NOTE, OL_SIMPLE, nullptr, 0, 0, nullptr, nullptr, 0, nullptr, 0 },
};
#undef F_
#define NOPT(a) ((int)(sizeof(a) / sizeof((a)[0])))

void ConfigPage::draw_ep_v2(u32 dev, Font* fo, const MouseState* mo, bool click,
                            float& ry, int& ri, float e,
                            float bandX, float bandW, float coX, float ctrlW,
                            float hdrX, float hdrW) {
    Kit K;
    if (!v2_kit(K, mo, click)) { draw_ep_config(dev, fo, mo, click, ry, ri, e, bandX, bandW, coX, ctrlW, hdrX, hdrW); return; }
    g_fade = e;
    UiConfig& c = ui_config();
    const bool adv = c.cfgAdv != 0;
    const float x = coX, w = ctrlW;
    const int base = CTRL_ID;
    ry += K.px(6.0f);
    if (v2_section(K, ctrl_uid_i(base, 1), 6, "Display", "Affichage", x, ry, w, cfg_count_modified(EP_DISPLAY, NOPT(EP_DISPLAY), adv)))
        v2_rows(K, ctrl_uid_i(base, 2), EP_DISPLAY, NOPT(EP_DISPLAY), x, ry, w);
    if (adv) {
        BoxStyle& bs = c.epBox;
        const FrameRefs fr = { &bs.on, &bs.border, &bs.alpha, &bs.alphaOwn, &bs.themeCopy, &bs.theme, &bs.hue, &bs.lum };
        if (v2_section(K, ctrl_uid_i(base, 3), 20, "Frame", "Cadre", x, ry, w, cfg_frame_modified(fr))) { cfg_frame_rows(K, ctrl_uid_i(base, 4), x, ry, w, fr); v2_end(K, ry); }
    }
    const int nMod = cfg_count_modified(EP_CONTENT, NOPT(EP_CONTENT), adv);
    if (v2_section(K, ctrl_uid_i(base, 5), 21, "Content", "Contenu", x, ry, w, nMod)) {
        // the tracked NM : a HUNT BOARD (2026-11-27 : "comme un tableau de chasse", the wide keys were too big for a name) --
        // two ledgers side by side, a row per NM : its lamp (lit green on the tracked one), the name, how many pops, the
        // collectable's target. A click stores the KEY (not an index) and turns the box on, like //aio ept.
        int sel = -1; for (int i = 0; i < NMS_N; ++i) if (lstrcmpiA(c.epTrack, NMS[i].key) == 0) { sel = i; break; }
        const int perCol = (NMS_N + 1) / 2;
        const float x0 = x + K.px(14.0f), gw = w - K.px(28.0f), gap = K.px(14.0f);
        const float lw = (gw - gap) * 0.5f, rh = K.px(28.0f), rg = K.px(4.0f), hh = K.px(28.0f), pad = 0.0f, hg = K.px(6.0f);
        const float bh = pad + hh + hg + perCol * (rh + rg) - rg + pad;
        const char* heads[3] = { tr("Notorious Monster", "Monstre notoire"), tr("Pops", "Pops"), tr("Collect.", "Collect.") };
        const int al[3] = { 0, 2, 2 };
        for (int col = 0; col < 2; ++col) {
            const float bx = x0 + col * (lw + gap);
            if (ry + bh < K.clipTop || ry > K.clipBot) continue;
            const float cxs[3] = { bx + pad + K.px(30.0f), bx + lw - pad - K.px(100.0f), bx + lw - pad - K.px(14.0f) };
            chrome_ledger_head(K, bx + pad, ry + pad, lw - 2.0f * pad, hh, heads, cxs, al, 3);
            for (int r = 0; r < perCol; ++r) {
                const int gi = col * perCol + r; if (gi >= NMS_N) break;
                const float yy = ry + pad + hh + hg + r * (rh + rg);
                char pops[8], coll[8];
                _snprintf(pops, sizeof(pops), "%u", (unsigned)NMS[gi].popCount); pops[7] = 0;
                if (NMS[gi].collectableTarget) _snprintf(coll, sizeof(coll), "%u", (unsigned)NMS[gi].collectableTarget); else lstrcpynA(coll, "-", sizeof(coll));
                coll[7] = 0;
                const char* cells[3] = { NMS[gi].en, pops, coll };
                if (chrome_ledger_row(K, ctrl_uid_i(base, 100 + gi), bx + pad, yy, lw - 2.0f * pad, rh, cells, cxs, al, 3, gi == sel, (r & 1) != 0) && gi != sel) {
                    lstrcpynA(c.epTrack, NMS[gi].key, (int)sizeof(c.epTrack)); c.epShow = 1; save_ui_config(); }
            }
        }
        ry += bh + K.px(12.0f);
        v2_rows(K, ctrl_uid_i(base, 7), EP_CONTENT, NOPT(EP_CONTENT), x, ry, w);
    }
    if (adv) {
        const char* lab[EP_TE_COUNT] = { tr("Title", "Titre"), tr("Pop", "Pop"), tr("From", "De"), tr("Collectable", "Collectable") };
        TextStyle* ts[EP_TE_COUNT]; for (int i = 0; i < EP_TE_COUNT; ++i) ts[i] = &c.epText[i];
        if (v2_section(K, ctrl_uid_i(base, 8), 5, "Text", "Texte", x, ry, w, cfg_text_modified(ts, EP_TE_COUNT))) { cfg_text_rows(K, ctrl_uid_i(base, 9), x, ry, w, lab, ts, EP_TE_COUNT, &cfgEpTextElem_); v2_end(K, ry); }
    } else v2_simple_note(K, x, ry);
    g_fade = e;
}
#undef NOPT

} // namespace aio
