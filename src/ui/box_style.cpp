// box_style.cpp -- the shared themed-box DRAW helper + the shared "box appearance" CONFIG rows (see box_style.h).
// One implementation reused by every module so their box chrome matches the Party/Target master.
#include "ui/box_style.h"
#include "ui/config_page.h"
#include "ui/config_controls.h"
#include "ui/config_rows.h"
#include "model/ui_config.h"
#include "gfx/window.h"
#include "gfx/draw.h"
#include "gfx/font.h"
#include <cstdio>

namespace aio {

static inline u32 mula(u32 c, float a) {                       // scale a colour's alpha by `a`
    if (a < 0.0f) a = 0.0f; if (a > 1.0f) a = 1.0f;
    return (c & 0x00FFFFFFu) | ((u32)(((c >> 24) & 0xFFu) * a) << 24);
}

// ---- Per-box FFXI window skins. A box on its OWN "Custom -> FFXI" theme picks WHICH of the game skins it wants (the
// config below already offers the chip grid, writing bs.theme). draw_themed_box used to ignore that and draw the SHARED
// party skin, so every FFXI box showed the party's skin -- and a box could not use FFXI at all unless the global theme
// was FFXI too. Give each variant its OWN lazily-loaded WindowSkin (the Target/Player pattern) so a box's FFXI choice is
// honoured independently of the party. Only a variant a box actually uses is loaded ; forgotten on a device change,
// disposed at shutdown (both wired from Hud). ----
static const int BOX_SKIN_MAX = 16;   // >= window_tex_theme_count() (currently 9)
static WindowSkin g_boxSkin[BOX_SKIN_MAX];
static const WindowSkin* box_ffxi_skin(u32 dev, int variant) {
    if (variant < 0 || variant >= BOX_SKIN_MAX || variant >= window_tex_theme_count()) return nullptr;
    if (!g_boxSkin[variant].ready()) g_boxSkin[variant].load(dev, window_theme_name(variant));   // atomic load ; retries next frame on a miss (no give-up latch)
    return &g_boxSkin[variant];
}
void box_skins_forget()  { for (int i = 0; i < BOX_SKIN_MAX; ++i) g_boxSkin[i].on_device_lost(); }   // device recreate : forget handles, reload lazily
void box_skins_dispose() { for (int i = 0; i < BOX_SKIN_MAX; ++i) g_boxSkin[i].dispose(); }           // shutdown : release textures

void draw_themed_box(u32 dev, const WindowSkin* partySkin, float x, float y, float w, float h,
                     const BoxStyle& bs, float base, float S) {
    if (!bs.on) return;
    const UiConfig& c = ui_config();
    const bool cp = bs.themeCopy != 0;
    const int   theme = cp ? c.skinTheme : bs.theme;
    const float lum   = cp ? c.skinLum   : bs.lum;
    const unsigned hue = cp ? c.skinHue  : bs.hue;
    float a = base * bs.alpha; if (a < 0.0f) a = 0.0f; if (a > 1.0f) a = 1.0f;
    const u32 tint = mula(0xFFFFFFFFu, a);
    const bool border = bs.border != 0;                          // Border off -> background only (draw_window's drawBorder=false)
    if (window_theme_is_proc(theme)) {
        draw_proc_window(dev, theme, x, y, w, h, tint, false, border, lum, hue);
    } else {
        // FFXI family : Same-as-Party reuses the SHARED party skin (f.skin) ; a Custom box uses ITS OWN variant so it can
        // pick a different FFXI window skin than the party -- and render even when the global theme is procedural.
        const WindowSkin* skin = cp ? partySkin : box_ffxi_skin(dev, window_theme_variant(theme));
        if (skin && skin->ready()) {
            draw_window(dev, *skin, x, y, w, h, tint, S, false, border);
        } else {
            const float R = 6.0f * S;                            // last-resort flat panel (skin not ready yet)
            rrect_bordered(dev, x, y, w, h, R, mula(0xFF232E54u, a), mula(0xFF080B1Au, a), mula(border ? 0x6699BBFFu : 0x00000000u, a), 1.0f);
        }
    }
}

// The effective FRAME colour (RGB, alpha 0xFF) of a themed box -> lets a widget tint an inner accent to MATCH the box's
// border. Resolves Same-as-Party / procedural-hue / FFXI-skin, exactly like draw_themed_box picks its frame. Soft-blue
// fallback while a skin is still loading. (mirrors the per-cell border recipe in player.cpp's equipment grid)
u32 box_style_border_color(u32 dev, const WindowSkin* partySkin, const BoxStyle& bs) {
    const UiConfig& c = ui_config();
    const bool cp = bs.themeCopy != 0;
    const int   theme = cp ? c.skinTheme : bs.theme;
    const unsigned hue = cp ? c.skinHue : bs.hue;
    if (window_theme_is_proc(theme)) return box_theme_border_color(theme, hue) | 0xFF000000u;
    const WindowSkin* skin = cp ? partySkin : box_ffxi_skin(dev, window_theme_variant(theme));
    return ((skin && skin->ready()) ? skin->borderColor : 0xFF6699BBu) | 0xFF000000u;
}

// ---- shared config rows (Box on/off, Transparency, Theme Same-as-Party/own family+hue+grid+luminosity) ----
// Each control is keyed by CTRL_ID (file:line hash, see config_controls.h) -- no hand-numbered uids.
void ConfigPage::draw_box_appearance(u32 dev, Font* fo, const MouseState* mo, bool click,
                                     float& ry, int& ri, float e,
                                     float bandX, float bandW, float coX, float ctrlW, BoxStyle& bs) {
    { ROW_BAND(52.0f)   // Box on/off
        const float rowH = snap(40.0f), ty = ry + yo; fo->begin(dev);
        fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, tr("Box", "Cadre"), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
        const float bbw = snap(150.0f), bbh = snap(34.0f), bx2 = coX + ctrlW - bbw, bty = ty + (rowH - bbh) * 0.5f;
        if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx2, bty, bbw, bbh, bs.on ? tr("On", "Oui") : tr("None", "Aucun"), bs.on != 0)) { bs.on = !bs.on; save_ui_config(); }
    }
    ROW_NEXT(52.0f)
    if (bs.on) {
    { ROW_BAND(52.0f)   // Border on/off (frame edges + corners ; off = background only)
        const float rowH = snap(40.0f), ty = ry + yo; fo->begin(dev);
        fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, tr("Border", "Bordure"), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
        const float bbw = snap(150.0f), bbh = snap(34.0f), bx2 = coX + ctrlW - bbw, bty = ty + (rowH - bbh) * 0.5f;
        if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx2, bty, bbw, bbh, bs.border ? tr("On", "Oui") : tr("None", "Aucun"), bs.border != 0)) { bs.border = !bs.border; save_ui_config(); }
    }
    ROW_NEXT(52.0f)
    { ROW_BAND(46.0f)   // Transparency (content stays opaque)
        const float transp = 1.0f - bs.alpha; char b[16]; sprintf(b, "%d%%", (int)(transp * 100.0f + 0.5f));
        float v01 = clampf(transp, 0.0f, 1.0f);   // full 0..100% range (100% = fully invisible box)
        if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Transparency", "Transparence"), b, &v01)) { bs.alpha = 1.0f - v01; }
    }
    ROW_NEXT(46.0f)
    { ROW_BAND(52.0f)   // Theme : Same as Party / Custom
        const float rowH = snap(40.0f), ty = ry + yo; fo->begin(dev);
        fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, tr("Theme", "Thème"), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
        const float bbw = snap(150.0f), bbh = snap(34.0f), bx2 = coX + ctrlW - bbw, bty = ty + (rowH - bbh) * 0.5f;
        if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx2, bty, bbw, bbh, bs.themeCopy ? tr("Same as Party", "Comme Party") : tr("Custom", "Perso"), bs.themeCopy != 0)) { bs.themeCopy = !bs.themeCopy; save_ui_config(); }
    }
    ROW_NEXT(52.0f)
    if (!bs.themeCopy) {
        { ROW_BAND(52.0f)   // Box Theme family
            const int fam = window_theme_family(bs.theme), var = window_theme_variant(bs.theme);
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Box Theme", "Thème de cadre"), box_family_name(fam))) { bs.theme = window_theme_index(wrap(fam + d, box_family_count()), var); save_ui_config(); }
        }
        ROW_NEXT(52.0f)
        if (window_theme_family(bs.theme) != 0) { ROW_BAND(48.0f)   // procedural : custom-hue toggle
            const float rowH = snap(38.0f), ty = ry + yo; fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, tr("Custom colour", "Couleur perso"), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
            const float bbw = snap(112.0f), bbh = snap(34.0f), bx2 = coX + ctrlW - bbw, bty = ty + (rowH - bbh) * 0.5f;
            const bool on = bs.hue != 0;
            if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx2, bty, bbw, bbh, on ? tr("On", "Oui") : tr("Off", "Non"), on)) { bs.hue = on ? 0u : (box_hue_color(window_theme_variant(bs.theme)) | 0xFF000000u); save_ui_config(); }
            ROW_NEXT(48.0f)
        }
        if (window_theme_family(bs.theme) != 0 && bs.hue != 0) {
            CFG_COLOR_PICKER(&bs.hue)
        } else {   // variant grid : FFXI theme chips / procedural hue swatches
            const int fam = window_theme_family(bs.theme), var = window_theme_variant(bs.theme);
            const bool isFFXI = (fam == 0);
            const int nVar = isFFXI ? window_tex_theme_count() : box_hue_count();
            const int COLS = isFFXI ? (nVar < 1 ? 1 : nVar) : 15;
            const int nrows = (nVar + COLS - 1) / COLS;
            const float cw = isFFXI ? snap(42.0f) : snap(22.0f), ch = isFFXI ? snap(26.0f) : snap(22.0f), cg = snap(7.0f);
            const float gridH = nrows * ch + (nrows - 1) * cg, slotH = gridH + snap(20.0f);
            ROW_BAND(slotH) (void)yo;
            fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ry + slotH * 0.5f, isFFXI ? tr("Theme", "Thème") : tr("Colour", "Couleur"), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
            const float gridW = COLS * cw + (COLS - 1) * cg;
            const float gx = coX + ctrlW - gridW, gy = ry + (slotH - gridH) * 0.5f;
            for (int k = 0; k < nVar; ++k) {
                const float xk = gx + (k % COLS) * (cw + cg), yk = gy + (k / COLS) * (ch + cg);
                const bool sel = (var == k);
                if (isFFXI) {
                    rpanel(dev, xk, yk, cw, ch, snap(6.0f), sel ? C_ROWON_T : 0x66121A18, sel ? C_ROWON_B : 0x66090D0F, sel ? C_ACCENT : C_BORDER, snap(1.2f));
                    fo->begin(dev); fo->draw_c(dev, xk + cw * 0.5f, yk + ch * 0.5f, window_theme_name(k), snap(13.0f), fa(sel ? C_ACCENTHI : C_TEXT), fa(C_STROKE), 1.0f);
                } else {
                    const u32 col = box_hue_color(k);
                    if (sel) { cs_add(dev); rrect_glow(dev, xk, yk, cw, ch, snap(6.0f), (col & 0x00FFFFFF) | 0x80000000, snap(6.0f)); cs(dev); }
                    rrect_fill(dev, xk, yk, cw, ch, snap(6.0f), col, shade(col, -0.28f));
                    outline(dev, xk, yk, cw, ch, sel ? 0xFFFFFFFF : C_BORDER);
                }
                if (inrect(mo, xk, yk, cw, ch) && click) { bs.theme = window_theme_index(fam, k); save_ui_config(); }
            }
            ROW_NEXT(slotH)
        }
        if (window_theme_family(bs.theme) != 0) { ROW_BAND(46.0f)   // Luminosity (procedural only)
            float v01 = (bs.lum + 1.0f) * 0.5f; v01 = clampf(v01, 0.0f, 1.0f);
            const int pct = (int)(bs.lum * 100.0f + (bs.lum >= 0.0f ? 0.5f : -0.5f));
            char b[16]; sprintf(b, "%+d%%", pct);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Luminosity", "Luminosité"), b, &v01)) { bs.lum = v01 * 2.0f - 1.0f; }
            ROW_NEXT(46.0f)
        }
    }   // end own-theme
    }   // end box on
}

// ---- the FRAME section, shared by the two boxes that have one in the same panel ----
// party_config.cpp used to carry this block TWICE, ~90 lines each, differing only in which fields they wrote.
// Its file header explained why: a helper called twice would reuse each control's CTRL_ID (a __FILE__:__LINE__
// hash) and the two groups' sliders would drag together. That was true when it was written and is not any more --
// ctrl_uid_i(CTRL_ID, group) is exactly the escape, and the config-panels doc says so. The justification outlived
// the problem, which is the usual way a duplication becomes permanent.
//
// `themeCopy` null   = this box IS the master (the party box) : no Same-as-Party row, the theme always shows.
// `borderExtra` null = one border chip instead of two (only the party box owns the floating Cost box).
// Two-column layout is recomputed here rather than passed : it is a function of ctrlW alone, and a caller that
// derived it differently would drift from the rows this draws.
void ConfigPage::draw_frame_section(u32 dev, Font* fo, const MouseState* mo, bool click,
                                    float& ry, int& ri, float e,
                                    float bandX, float bandW, float coX, float ctrlW,
                                    int group, int* themeCopy,
                                    int* theme, unsigned* hue, float* lum, float* alpha,
                                    bool* border, bool* borderExtra, const char* extraLabel) {
    const bool  twoCol = ctrlW >= snap(720.0f);
    const float gutter = snap(44.0f);
    const float halfW  = twoCol ? (ctrlW - gutter) * 0.5f : ctrlW;
    const float col2X  = coX + (twoCol ? halfW + gutter : 0.0f);
    const float sepX   = snap(coX + halfW + gutter * 0.5f);
    auto sepv = [&](float y, float h, bool on) {
        if (twoCol && on) flat(dev, sepX, snap(y + snap(7.0f)), snap(1.0f), snap(h) - snap(14.0f), 0x1EFFFFFFu);
    };
    const int uidBase = ctrl_uid_i(CTRL_ID, group);   // every control below hangs off this, so two calls never share a spring

    // Follow the master box, or have its own. Everything under it exists only in the second case, which is why the
    // choice is the row that OPENS the block rather than one buried inside it.
    if (themeCopy) {
        { ROW_BAND(48.0f)
            const float rowH = snap(38.0f), ty = ry + yo; fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, tr("Theme", "Th\xC3\xA8me"), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
            const float bbw = snap(150.0f), bbh = snap(34.0f), bx2 = coX + ctrlW - bbw, bty = ty + (rowH - bbh) * 0.5f;
            if (toggle_chip(dev, fo, mo, click, ctrl_uid_i(uidBase, 1), bx2, bty, bbw, bbh,
                            *themeCopy ? tr("Same as Party", "Comme Party") : tr("Custom", "Perso"), *themeCopy != 0)) {
                *themeCopy = !*themeCopy; save_ui_config(); }
            ROW_NEXT(48.0f)
        }
    }
    const bool own = (!themeCopy || !*themeCopy);
    if (own) {
        { const bool proc = (window_theme_family(*theme) != 0);
          const float bh2 = (twoCol || !proc) ? snap(48.0f) : snap(96.0f);
          ROW_BAND(bh2) (void)yo;
          const float yA = ry + (1.0f - ap) * snap(14.0f) + ((twoCol || !proc) ? (bh2 - snap(40.0f)) * 0.5f : snap(4.0f));
          const float yB = twoCol ? yA : yA + snap(48.0f);
          const float xB = twoCol ? col2X : coX;
          sepv(ry, bh2, proc);   // an empty second half has no split to state
          { const int fam = window_theme_family(*theme), var = window_theme_variant(*theme);
            if (int d = row_selector(dev, fo, mo, click, ctrl_uid_i(uidBase, 2), coX, yA, proc ? halfW : ctrlW,
                                     tr("Box Theme", "Th\xC3\xA8me de cadre"), box_family_name(fam))) {
                *theme = window_theme_index(wrap(fam + d, box_family_count()), var); save_ui_config(); } }
          if (proc) {   // FFXI skins have no hue of their own -- the switch would control nothing
              const float rowH = snap(38.0f);
              fo->begin(dev);
              fo->draw_lc(dev, xB + snap(4.0f), yB + rowH * 0.5f, tr("Custom colour", "Couleur perso"), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
              const float bbw = snap(112.0f), bbh = snap(34.0f), bx2 = xB + halfW - bbw, bty = yB + (rowH - bbh) * 0.5f;
              const bool on = (*hue != 0);
              if (toggle_chip(dev, fo, mo, click, ctrl_uid_i(uidBase, 3), bx2, bty, bbw, bbh, on ? tr("On", "Oui") : tr("Off", "Non"), on)) {
                  *hue = on ? 0u : (box_hue_color(window_theme_variant(*theme)) | 0xFF000000u); save_ui_config(); }
          }
          ROW_NEXT(bh2)
        }
        // The picker / swatch grid keeps the full width : it is a grid, it cannot share a row with anything.
        if (window_theme_family(*theme) != 0 && *hue != 0) {
            CFG_COLOR_PICKER_I(hue, group)
        } else
        {   // variant grid : FFXI -> theme-number chips ; procedural family -> hue swatches (click to pick)
          const int fam = window_theme_family(*theme), var = window_theme_variant(*theme);
          const bool isFFXI = (fam == 0);
          const int nVar = isFFXI ? window_tex_theme_count() : box_hue_count();
          const int COLS = isFFXI ? (nVar < 1 ? 1 : nVar) : 15;
          const int nrows = (nVar + COLS - 1) / COLS;
          const float cw = isFFXI ? snap(42.0f) : snap(22.0f), ch = isFFXI ? snap(26.0f) : snap(22.0f), cg = snap(7.0f);
          const float gridH = nrows * ch + (nrows - 1) * cg, slotH = gridH + snap(20.0f);
          ROW_BAND(slotH) (void)yo;
          fo->begin(dev);
          fo->draw_lc(dev, coX + snap(4.0f), ry + slotH * 0.5f, isFFXI ? tr("Theme", "Th\xC3\xA8me") : tr("Colour", "Couleur"), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
          const float gridW = COLS * cw + (COLS - 1) * cg;
          const float gx = coX + ctrlW - gridW, gy = ry + (slotH - gridH) * 0.5f;
          for (int k = 0; k < nVar; ++k) {
              const float xk = gx + (k % COLS) * (cw + cg), yk = gy + (k / COLS) * (ch + cg);
              const bool sel = (var == k);
              if (isFFXI) {
                  rpanel(dev, xk, yk, cw, ch, snap(6.0f), sel ? C_ROWON_T : 0x66121A18, sel ? C_ROWON_B : 0x66090D0F, sel ? C_ACCENT : C_BORDER, snap(1.2f));
                  fo->begin(dev); fo->draw_c(dev, xk + cw * 0.5f, yk + ch * 0.5f, window_theme_name(k), snap(13.0f), fa(sel ? C_ACCENTHI : C_TEXT), fa(C_STROKE), 1.0f);
              } else {
                  const u32 c = box_hue_color(k);
                  if (sel) { cs_add(dev); rrect_glow(dev, xk, yk, cw, ch, snap(6.0f), (c & 0x00FFFFFF) | 0x80000000, snap(6.0f)); cs(dev); }
                  rrect_fill(dev, xk, yk, cw, ch, snap(6.0f), c, shade(c, -0.28f));
                  outline(dev, xk, yk, cw, ch, sel ? 0xFFFFFFFF : C_BORDER);
              }
              if (inrect(mo, xk, yk, cw, ch) && click) { *theme = window_theme_index(fam, k); save_ui_config(); }
          }
          ROW_NEXT(slotH)
        }
    }
    // Luminosity + Transparency : both "how much of the frame you see". Luminosity exists only on a procedural
    // theme this box owns ; transparency always does, so the pair collapses to one when it must.
    { const bool proc = (own && window_theme_family(*theme) != 0);
      const float bh2 = (twoCol || !proc) ? snap(46.0f) : snap(92.0f);
      ROW_BAND(bh2) (void)yo;
      const float yA = ry + (1.0f - ap) * snap(14.0f) + ((twoCol || !proc) ? (bh2 - snap(40.0f)) * 0.5f : snap(3.0f));
      const float yB = twoCol ? yA : yA + snap(46.0f);
      const float xB = proc ? (twoCol ? col2X : coX) : coX;
      const float wB = proc ? halfW : ctrlW;
      sepv(ry, bh2, proc);
      if (proc) {
          float v01 = (*lum + 1.0f) * 0.5f; v01 = clampf(v01, 0.0f, 1.0f);
          const int pct = (int)(*lum * 100.0f + (*lum >= 0.0f ? 0.5f : -0.5f));
          char b[16]; sprintf(b, "%+d%%", pct);
          if (row_slider(dev, fo, mo, ctrl_uid_i(uidBase, 4), coX, yA, halfW, tr("Luminosity", "Luminosit\xC3\xA9"), b, &v01)) {
              *lum = v01 * 2.0f - 1.0f; }
      }
      { const float transp = 1.0f - *alpha; char b[16]; sprintf(b, "%d%%", (int)(transp * 100.0f + 0.5f));
        float v01 = clampf(transp, 0.0f, 1.0f);
        if (row_slider(dev, fo, mo, ctrl_uid_i(uidBase, 5), xB, yB, wB, tr("Transparency", "Transparence"), b, &v01)) {
            *alpha = 1.0f - v01; } }
      ROW_NEXT(bh2)
    }
    { ROW_BAND(48.0f)   // the frame edges, and (party only) the floating Cost box that rides on them
        const float rowH = snap(38.0f), ty = ry + yo; fo->begin(dev);
        fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, tr("Border", "Bordure"), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
        const float bbw = snap(112.0f), bgap = snap(8.0f), bbh = snap(34.0f), bty = ty + (rowH - bbh) * 0.5f;
        const int nchips = borderExtra ? 2 : 1;
        const float bx0 = coX + ctrlW - (nchips * bbw + (nchips - 1) * bgap);
        if (toggle_chip(dev, fo, mo, click, ctrl_uid_i(uidBase, 6), bx0, bty, bbw, bbh, tr("Box", "Bo\xC3\xAEte"), *border)) { *border = !*border; save_ui_config(); }
        if (borderExtra && toggle_chip(dev, fo, mo, click, ctrl_uid_i(uidBase, 7), bx0 + bbw + bgap, bty, bbw, bbh, extraLabel, *borderExtra)) { *borderExtra = !*borderExtra; save_ui_config(); }
        ROW_NEXT(48.0f)
    }
}

// shared per-element "Text" style rows -- see config_page.h. The caller draws the element SELECTOR and passes the
// chosen TextStyle ; these are the identical Font/Size/Outline/Style/Colour/Alpha controls every module's Text
// sub-section used to copy-paste. CTRL_ID is collision-free (one call per panel, panels mutually exclusive), like
// draw_box_appearance above. (ROW_BAND/ROW_NEXT stay defined from config_rows.h through both functions -> #undef'd
// only after this one.)
void ConfigPage::draw_text_style(u32 dev, Font* fo, const MouseState* mo, bool click,
                                 float& ry, int& ri, float e,
                                 float bandX, float bandW, float coX, float ctrlW, TextStyle& ts, bool swatch) {
    { ROW_BAND(52.0f)   // Font
        int fc = ts.face; if (fc < 0 || fc >= ui_font_count()) fc = 0;
        if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Font", "Police"), ui_font_label(fc))) { ts.face = wrap(fc + d, ui_font_count()); save_ui_config(); }
    }
    ROW_NEXT(52.0f)
    { ROW_BAND(46.0f)   // Size
        const float lo = 0.50f, hi = 2.00f; char b[16]; sprintf(b, "%d%%", (int)(ts.size * 100.0f + 0.5f));
        float v01 = (ts.size - lo) / (hi - lo); v01 = clampf(v01, 0.0f, 1.0f);
        if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Size", "Taille"), b, &v01)) { float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f; ts.size = v < lo ? lo : (v > hi ? hi : v); }
    }
    ROW_NEXT(46.0f)
    { ROW_BAND(46.0f)   // Outline
        const float lo = 0.00f, hi = 2.00f; char b[16]; sprintf(b, "%d%%", (int)(ts.outline * 100.0f + 0.5f));
        float v01 = (ts.outline - lo) / (hi - lo); v01 = clampf(v01, 0.0f, 1.0f);
        if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Outline", "Contour"), b, &v01)) { float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f; ts.outline = v < lo ? lo : (v > hi ? hi : v); }
    }
    ROW_NEXT(46.0f)
    { ROW_BAND(52.0f)   // Bold / Italic / CAPS
        const float rowH = snap(40.0f), ty = ry + yo; fo->begin(dev);
        fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, tr("Style", "Style"), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
        const float bbw = snap(80.0f), bgap = snap(8.0f), bbh = snap(34.0f), bty = ty + (rowH - bbh) * 0.5f, bx0 = coX + ctrlW - (3 * bbw + 2 * bgap);
        if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx0, bty, bbw, bbh, tr("Bold", "Gras"), ts.bold)) { ts.bold = !ts.bold; save_ui_config(); }
        if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx0 + bbw + bgap, bty, bbw, bbh, tr("Italic", "Ital."), ts.italic)) { ts.italic = !ts.italic; save_ui_config(); }
        if (toggle_chip(dev, fo, mo, click, CTRL_ID, bx0 + 2 * (bbw + bgap), bty, bbw, bbh, tr("CAPS", "MAJ"), ts.upper)) { ts.upper = !ts.upper; save_ui_config(); }
    }
    ROW_NEXT(52.0f)
    { ROW_BAND(52.0f)   // Colour : Default / Custom
        const float rowH = snap(40.0f), ty = ry + yo; fo->begin(dev);
        fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, tr("Colour", "Couleur"), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
        const float bbh = snap(34.0f), bty = ty + (rowH - bbh) * 0.5f, tgw = snap(96.0f), onx = coX + ctrlW - tgw;
        if (toggle_chip(dev, fo, mo, click, CTRL_ID, onx, bty, tgw, bbh, ts.colorOn ? tr("Custom", "Perso") : tr("Default", "D\xC3\xA9""faut"), ts.colorOn)) {
            ts.colorOn = !ts.colorOn; if (ts.colorOn && (ts.color >> 24) == 0) ts.color |= 0xFF000000u; save_ui_config(); }
        if (swatch && ts.colorOn) {   // live colour preview (Target / Player / Minimap panels) : checkerboard + colour
            const float pw = snap(58.0f), pxs = onx - snap(12.0f) - pw;
            flat(dev, pxs, bty, pw * 0.5f, bbh, 0xFFFFFFFF); flat(dev, pxs + pw * 0.5f, bty, pw * 0.5f, bbh, 0xFF262A31);
            flat(dev, pxs, bty, pw, bbh, ts.color); outline(dev, pxs, bty, pw, bbh, C_BORDER);
        }
    }
    ROW_NEXT(52.0f)
    if (ts.colorOn) {   // HSV picker + Alpha
        CFG_COLOR_PICKER(&ts.color)
        { ROW_BAND(40.0f)
            int a = (int)((ts.color >> 24) & 0xFFu); char vb[8]; sprintf(vb, "%d", a); float v01 = a / 255.0f;
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, "A", vb, &v01)) {
                int nv = (int)(v01 * 255.0f + 0.5f); if (nv < 0) nv = 0; if (nv > 255) nv = 255;
                ts.color = (ts.color & 0x00FFFFFFu) | ((u32)nv << 24); }
        }
        ROW_NEXT(40.0f)
    }
    #undef ROW_BAND
    #undef ROW_NEXT
}

} // namespace aio
