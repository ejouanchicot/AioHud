// ep_config.cpp -- the EmpyPop module's settings panel (//aio config -> "EmpyPop").
//
// Each control is keyed by CTRL_ID (file:line hash, see config_controls.h) -- no hand-numbered uids.
// Reuses the shared sub-section flags catOpen_ [6] (Display) / [5] (Text), like every floating-box module.
// The box is placed in //aio edit like every other box.
#include "ui/config_page.h"
#include "ui/config_controls.h"    // cat_header / row_slider / row_selector / toggle_chip + palette
#include "ui/config_rows.h"        // ROW_BAND / ROW_NEXT
#include "model/ui_config.h"
#include "model/nms_gen.h"         // NMS[] / NMS_N : the tracked-NM selector writes NMS[idx].key into epTrack
#include "gfx/font.h"
#include "gfx/draw.h"
#include <windows.h>               // lstrcpynA / lstrcmpiA : the epTrack key round-trip
#include <cstdio>

namespace aio {

void ConfigPage::draw_ep_config(u32 dev, Font* fo, const MouseState* mo, bool click,
                                float& ry, int& ri, float e,
                                float bandX, float bandW, float coX, float ctrlW,
                                float hdrX, float hdrW) {
    UiConfig& c = ui_config();

    // ===== sub-section : DISPLAY =====
    if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Display", "Affichage"), catOpen_[6])) catOpen_[6] = !catOpen_[6];
    ROW_NEXT(42.0f)
    if (catOpen_[6]) {
        { ROW_BAND(48.0f)   // Show
            const float rowH = snap(38.0f), ty = ry + yo; fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, tr("Show", "Afficher"), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
            const float bbw = snap(112.0f), bbh = snap(34.0f), bx2 = coX + ctrlW - bbw, bty = ty + (rowH - bbh) * 0.5f;
            if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx2, bty, bbw, bbh, c.epShow ? tr("On", "Oui") : tr("Off", "Non"), c.epShow != 0)) { c.epShow = !c.epShow; save_ui_config(); }
        } ROW_NEXT(48.0f)
        { ROW_BAND(46.0f)   // Size (canonical : right after Show)
            const float lo = 0.50f, hi = 2.00f; char b[16]; sprintf(b, "%d%%", (int)(c.epScale * 100.0f + 0.5f));
            float v01 = (c.epScale - lo) / (hi - lo); v01 = clampf(v01, 0.0f, 1.0f);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Size", "Taille"), b, &v01)) { float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f; c.epScale = v < lo ? lo : (v > hi ? hi : v); }
        } ROW_NEXT(46.0f)
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
                    rrect_fill(dev, cx, cy, cw, ch, snap(4.0f), seld ? C_ACCENTHI : (hov ? 0xFF2C363Eu : 0xFF1B2228u), seld ? C_ACCENT : (hov ? 0xFF20292Fu : 0xFF141A1Fu));
                    if (seld) outline(dev, cx, cy, cw, ch, 0xFFEAFBF9u);
                    if (hov && click) { lstrcpynA(c.epTrack, NMS[gi].key, (int)sizeof(c.epTrack)); c.epShow = 1; sel = gi; save_ui_config(); }
                }
                // sub-pass B : the NM NAMES (font ; dark text on the bright SELECTED panel, light otherwise)
                fo->begin(dev);
                for (int col = 0; col < perRow; ++col) {
                    const int gi = r * perRow + col; if (gi >= NMS_N) break;
                    const bool seld = (gi == sel);
                    const float cx = coX + col * (cw + gap), cy = ry + yo;
                    const u32 tc = seld ? 0xFF0B1014u : C_TEXT;
                    fo->draw_c(dev, cx + cw * 0.5f, cy + ch * 0.5f, NMS[gi].en, snap(11.0f), fa(tc), fa(seld ? 0x66FFFFFFu : C_STROKE), 1.0f);
                }
                ROW_NEXT(26.0f)
            }
            ry += snap(9.0f);   // breathing space after the grid, before the Collectable row
        }
        { ROW_BAND(48.0f)   // Collectable counter row
            const float rowH = snap(38.0f), ty = ry + yo; fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, tr("Collectable", "Collectable"), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
            const float bbw = snap(112.0f), bbh = snap(34.0f), bx2 = coX + ctrlW - bbw, bty = ty + (rowH - bbh) * 0.5f;
            if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx2, bty, bbw, bbh, c.epColl ? tr("On", "Oui") : tr("Off", "Non"), c.epColl != 0)) { c.epColl = !c.epColl; save_ui_config(); }
        } ROW_NEXT(48.0f)
        draw_box_appearance(dev, fo, mo, click, ry, ri, e, bandX, bandW, coX, ctrlW, c.epBox);   // Box / Transparency / Theme / Hue / Luminosity
        { ROW_BAND(40.0f)   // note
            const float ty = ry + yo; fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + snap(16.0f), tr("Shows the pop + key items needed to spawn the chosen Abyssea NM.", "Montre les items de pop + key items pour faire appara\xC3\xAetre le NM Abyssea choisi."), snap(12.0f), fa(C_MUTE), fa(C_STROKE), 1.0f);
        } ROW_NEXT(40.0f)
    }   // end Display

    // ===== sub-section : TEXT (per-element typography) =====
    if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Text", "Texte"), catOpen_[5])) catOpen_[5] = !catOpen_[5];
    ROW_NEXT(42.0f)
    if (catOpen_[5]) {
        { ROW_BAND(52.0f)   // element selector
            const char* TLBL[EP_TE_COUNT] = { tr("Title", "Titre"), tr("Pop", "Pop"), tr("From", "De"), tr("Collectable", "Collectable") };
            int te = (cfgEpTextElem_ < 0 || cfgEpTextElem_ >= EP_TE_COUNT) ? 0 : cfgEpTextElem_;
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Element", "\xC3\x89l\xC3\xA9ment"), TLBL[te])) { cfgEpTextElem_ = wrap(te + d, EP_TE_COUNT); }
        }
        ROW_NEXT(52.0f)
        draw_text_style(dev, fo, mo, click, ry, ri, e, bandX, bandW, coX, ctrlW,
                        c.epText[(cfgEpTextElem_ < 0 || cfgEpTextElem_ >= EP_TE_COUNT) ? 0 : cfgEpTextElem_]);
    }   // end Text

    ry += snap(10.0f);
    #undef ROW_BAND
    #undef ROW_NEXT
}

} // namespace aio
