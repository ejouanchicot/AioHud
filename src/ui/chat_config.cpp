// chat_config.cpp -- the Chat module's settings panel (//aio config -> "Chat").
//
// ONE set of rows edits the box picked by the "Box" selector (Chat / Battle) -- the same pattern as the per-element
// Text selector of the other modules, and the reason no control here needs a loop uid : each row has one call site.
// Reuses catOpen_[6] (Boxes) and [5] (Text). The boxes are placed in //aio edit (drag, wheel = size, corner grip =
// width + lines).
#include "ui/config_page.h"
#include "ui/config_controls.h"
#include "ui/config_rows.h"
#include "model/ui_config.h"
#include "gfx/font.h"
#include "gfx/draw.h"
#include <cstdio>

namespace aio {

static int g_chatCfgBox = 0;   // 0 = Chat, 1 = Battle (which box the rows edit)

void ConfigPage::draw_chat_config(u32 dev, Font* fo, const MouseState* mo, bool click,
                                  float& ry, int& ri, float e,
                                  float bandX, float bandW, float coX, float ctrlW,
                                  float hdrX, float hdrW) {
    UiConfig& c = ui_config();
    const int b = (g_chatCfgBox == 1) ? 1 : 0;
    const char* BOXL[2] = { tr("Chat", "Discussion"), tr("Battle", "Combat") };

    #define CHAT_NOTE(TXT) { ROW_BAND(40.0f) const float ty_ = ry + yo; fo->begin(dev); \
        fo->draw_lc(dev, coX + snap(4.0f), ty_ + snap(16.0f), (TXT), snap(12.0f), fa(C_MUTE), fa(C_STROKE), 1.0f); } ROW_NEXT(40.0f)

    // ===== BOXES =====
    const float aF6_ = cat_fold(CTRL_ID, catOpen_[6]);
    cat_panel(dev, hdrX, ry, hdrW, cat_card_h(catH_[6], aF6_));
    if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Boxes", "Box"), catOpen_[6], aF6_)) catOpen_[6] = !catOpen_[6];
    ROW_NEXT(42.0f)
    if (aF6_ > 0.0f) {
        const float top6_ = ry;
        cat_fold_clip(dev, hdrX, top6_, hdrW, catH_[6] * aF6_);
        { ROW_BAND(52.0f)
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Box", "Box"), BOXL[b])) g_chatCfgBox = wrap(b + d, 2);
        } ROW_NEXT(52.0f)
        CHAT_NOTE(b == 0 ? tr("Talk, NPC and system lines, with tabs (All / Party / LS / Tell / Say / System).",
                              "Discussions, PNJ et messages syst\xC3\xA8me, avec onglets (Tout / Groupe / LS / Tell / Dire / Syst\xC3\xA8me).")
                         : tr("The combat log, with tabs (All / Me : the lines about you).",
                              "Le journal de combat, avec onglets (Tout / Moi : les lignes qui te concernent)."))
        ROW_TOGGLE(CTRL_ID, tr("Show", "Afficher"), c.chShow[b])
        { ROW_BAND(46.0f)   // Size
            const float lo = 0.50f, hi = 2.00f; char s[16]; sprintf(s, "%d%%", (int)(c.chScale[b] * 100.0f + 0.5f));
            float v01 = clampf((c.chScale[b] - lo) / (hi - lo), 0.0f, 1.0f);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Size", "Taille"), s, &v01)) { float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f; c.chScale[b] = v < lo ? lo : (v > hi ? hi : v); }
        } ROW_NEXT(46.0f)
        { ROW_BAND(52.0f)   // how the tabs show
            const char* TL[4] = { tr("Auto", "Auto"), tr("Icon + name", "Ic\xC3\xB4ne + nom"), tr("Icon", "Ic\xC3\xB4ne"), tr("Name", "Nom") };
            const int v = (c.chTabStyle[b] >= 0 && c.chTabStyle[b] <= 3) ? c.chTabStyle[b] : 0;
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Tabs", "Onglets"), TL[v])) c.chTabStyle[b] = wrap(v + d, 4);
        } ROW_NEXT(52.0f)
        { ROW_BAND(46.0f)   // the header's size
            const float lo = 0.60f, hi = 1.60f; char sb[16]; sprintf(sb, "%d%%", (int)(c.chHeadScale[b] * 100.0f + 0.5f));
            float v01 = clampf((c.chHeadScale[b] - lo) / (hi - lo), 0.0f, 1.0f);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Header size", "Taille de l'en-t\xC3\xAAte"), sb, &v01)) { float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f; c.chHeadScale[b] = v < lo ? lo : (v > hi ? hi : v); }
        } ROW_NEXT(46.0f)
        { ROW_BAND(52.0f)   // density : how much air around the text
            const char* DL[3] = { tr("Compact", "Compacte"), tr("Normal", "Normale"), tr("Airy", "A\xC3\xA9r\xC3\xA9""e") };
            const int v = (c.chDensity[b] >= 0 && c.chDensity[b] <= 2) ? c.chDensity[b] : 0;
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Density", "Densit\xC3\xA9"), DL[v])) c.chDensity[b] = wrap(v + d, 3);
        } ROW_NEXT(52.0f)
        { ROW_BAND(52.0f)   // fixed size, or grows with its content
            const char* FL[2] = { tr("Fixed", "Fixe"), tr("Follows the content", "Suit le contenu") };
            const int v = c.chFit[b] == 1 ? 1 : 0;
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Box height", "Hauteur de la box"), FL[v])) c.chFit[b] = wrap(v + d, 2);
        } ROW_NEXT(52.0f)
        { ROW_BAND(52.0f)   // which edge stays put
            const char* GL[2] = { tr("Up (bottom stays)", "Le haut (bas fixe)"), tr("Down (top stays)", "Le bas (haut fixe)") };
            const int v = c.chGrow[b] == 1 ? 1 : 0;
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Grows toward", "Grandit vers"), GL[v])) {
                const int nv = wrap(v + d, 2);
                if (nv != v && c.chEdge[b][1] > c.chEdge[b][0]) c.chY[b] = nv == 0 ? c.chEdge[b][1] : c.chEdge[b][0];   // the box does not move
                c.chGrow[b] = nv;
            }
        } ROW_NEXT(52.0f)
        { ROW_BAND(46.0f)   // Width
            const float lo = 200.0f, hi = 1600.0f; char s[16]; sprintf(s, "%d", c.chW[b]);
            float v01 = clampf(((float)c.chW[b] - lo) / (hi - lo), 0.0f, 1.0f);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Width", "Largeur"), s, &v01)) { int v = (int)(lo + v01 * (hi - lo) + 0.5f); v = (v / 10) * 10; c.chW[b] = v < 200 ? 200 : (v > 1600 ? 1600 : v); }
        } ROW_NEXT(46.0f)
        if (c.chMaxLines[b] == 0) { ROW_BAND(46.0f)   // Height (only when "Max lines" is "all")
            const float lo = 80.0f, hi = 1000.0f; char s[16]; sprintf(s, "%d", c.chH[b] > 0 ? c.chH[b] : 260);
            float v01 = clampf(((float)(c.chH[b] > 0 ? c.chH[b] : 260) - lo) / (hi - lo), 0.0f, 1.0f);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Height", "Hauteur"), s, &v01)) { int v = (int)(lo + v01 * (hi - lo) + 0.5f); v = (v / 10) * 10; c.chH[b] = v < 80 ? 80 : (v > 1000 ? 1000 : v); }
        } if (c.chMaxLines[b] == 0) { ROW_NEXT(46.0f) }
        { ROW_BAND(46.0f)   // lines shown normally
            const float lo = 0.0f, hi = 40.0f; char sb[24];
            if (c.chMaxLines[b] > 0) sprintf(sb, "%d", c.chMaxLines[b]); else lstrcpynA(sb, tr("all", "tout"), sizeof(sb));
            float v01 = clampf(((float)c.chMaxLines[b] - lo) / (hi - lo), 0.0f, 1.0f);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Max lines", "Lignes max"), sb, &v01)) { int v = (int)(lo + v01 * (hi - lo) + 0.5f); c.chMaxLines[b] = v < 0 ? 0 : (v > 40 ? 40 : v); }
        } ROW_NEXT(46.0f)
        ROW_TOGGLE(CTRL_ID, tr("Time stamps", "Horodatage"), c.chStamp[b])
        // ---- when nothing happens : collapse and / or fade, each on its own delay ----
        { int on = (c.chBehave[b] & 2) ? 1 : 0;
          { ROW_BAND(48.0f) if (row_toggle(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Collapse when idle", "Repli au repos"), &on)) c.chBehave[b] = on ? (c.chBehave[b] | 2) : (c.chBehave[b] & ~2); } ROW_NEXT(48.0f) }
        if (c.chBehave[b] & 2) { ROW_BAND(46.0f)   // collapse delay
            const float lo = 3.0f, hi = 120.0f; char sb[16]; sprintf(sb, "%d s", c.chIdle[b]);
            float v01 = clampf(((float)c.chIdle[b] - lo) / (hi - lo), 0.0f, 1.0f);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("  each line stays", "  chaque ligne reste"), sb, &v01)) { int v = (int)(lo + v01 * (hi - lo) + 0.5f); c.chIdle[b] = v < 3 ? 3 : (v > 120 ? 120 : v); }
        } if (c.chBehave[b] & 2) { ROW_NEXT(46.0f) }
        if (c.chBehave[b] & 2) { ROW_BAND(46.0f)   // lines kept when collapsed
            const float lo = 1.0f, hi = 20.0f; char sb[16]; sprintf(sb, "%d", c.chFoldLines[b]);
            float v01 = clampf(((float)c.chFoldLines[b] - lo) / (hi - lo), 0.0f, 1.0f);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("  lines kept", "  lignes gard\xC3\xA9""es"), sb, &v01)) { int v = (int)(lo + v01 * (hi - lo) + 0.5f); c.chFoldLines[b] = v < 1 ? 1 : (v > 20 ? 20 : v); }
        } if (c.chBehave[b] & 2) { ROW_NEXT(46.0f) }
        { int on = (c.chBehave[b] & 1) ? 1 : 0;
          { ROW_BAND(48.0f) if (row_toggle(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Fade when idle", "Fondu au repos"), &on)) c.chBehave[b] = on ? (c.chBehave[b] | 1) : (c.chBehave[b] & ~1); } ROW_NEXT(48.0f) }
        if (c.chBehave[b] & 1) { ROW_BAND(46.0f)   // fade delay
            const float lo = 3.0f, hi = 120.0f; char sb[16]; sprintf(sb, "%d s", c.chFadeIdle[b]);
            float v01 = clampf(((float)c.chFadeIdle[b] - lo) / (hi - lo), 0.0f, 1.0f);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("  fade after", "  fondu apr\xC3\xA8s"), sb, &v01)) { int v = (int)(lo + v01 * (hi - lo) + 0.5f); c.chFadeIdle[b] = v < 3 ? 3 : (v > 120 ? 120 : v); }
        } if (c.chBehave[b] & 1) { ROW_NEXT(46.0f) }
        if (c.chBehave[b] & 1) { ROW_BAND(46.0f)   // opacity kept when faded
            const float lo = 0.0f, hi = 80.0f; char sb[16]; sprintf(sb, "%d%%", c.chFadeTo[b]);
            float v01 = clampf(((float)c.chFadeTo[b] - lo) / (hi - lo), 0.0f, 1.0f);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("  faded opacity", "  opacit\xC3\xA9 au repos"), sb, &v01)) { int v = (int)(lo + v01 * (hi - lo) + 0.5f); c.chFadeTo[b] = v < 0 ? 0 : (v > 80 ? 80 : v); }
        } if (c.chBehave[b] & 1) { ROW_NEXT(46.0f) }
        if (b == 0) { ROW_BAND(52.0f)   // where you TYPE : the game's own line, docked under the box, or inside it
            const char* IL[3] = { tr("Game's place", "Place du jeu"), tr("Under the box", "Sous la box"), tr("In the box", "Dans la box") };
            const int v = (c.chDock >= 0 && c.chDock <= 2) ? c.chDock : 2;
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Input line", "Barre de saisie"), IL[v])) c.chDock = wrap(v + d, 3);
        } if (b == 0) { ROW_NEXT(52.0f) }
        draw_box_appearance(dev, fo, mo, click, ry, ri, e, bandX, bandW, coX, ctrlW, c.chBox[b]);
        CHAT_NOTE(tr("Move it in Edit layout ; the corner grip sets its width and height.",
                     "D\xC3\xA9place-la dans \xC3\x89""diter dispo ; la poign\xC3\xA9""e du coin r\xC3\xA8gle largeur et hauteur."))
        CHAT_NOTE(tr("Wheel over a box = scroll back. Hide the game's own log in Game UI.",
                     "Molette sur une box = remonter. Cache le journal du jeu dans Interface du jeu."))
        cat_fold_end(dev, ry, top6_, catH_[6], aF6_);
    }
    ry += snap(16.0f);

    // ===== TEXT (of the box picked above) =====
    const float aF5_ = cat_fold(CTRL_ID, catOpen_[5]);
    cat_panel(dev, hdrX, ry, hdrW, cat_card_h(catH_[5], aF5_));
    if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Text", "Texte"), catOpen_[5], aF5_)) catOpen_[5] = !catOpen_[5];
    ROW_NEXT(42.0f)
    if (aF5_ > 0.0f) {
        const float top5_ = ry;
        cat_fold_clip(dev, hdrX, top5_, hdrW, catH_[5] * aF5_);
        { ROW_BAND(52.0f)
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Box", "Box"), BOXL[b])) g_chatCfgBox = wrap(b + d, 2);
        } ROW_NEXT(52.0f)
        draw_text_style(dev, fo, mo, click, ry, ri, e, bandX, bandW, coX, ctrlW, c.chText[b]);
        cat_fold_end(dev, ry, top5_, catH_[5], aF5_);
    }
    ry += snap(16.0f);
    #undef CHAT_NOTE
    #undef ROW_BAND
    #undef ROW_NEXT
}

} // namespace aio
