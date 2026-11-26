// chat_config.cpp -- the Chat module's settings panel (//aio config -> "Chat").
//
// ONE set of rows edits the box picked by the selector at the top of the page (Chat / Battle) -- the same pattern as the per-element
// Text selector of the other modules, and the reason no control here needs a loop uid : each row has one call site.
// Sections, the config grammar (docs/architecture/config-panels.md) : Display [6] / Frame [20] / Content [21] /
// Behaviour [22] / History [24] / Banned players [25] / Text [5]. How the boxes are USED (Edit layout, the wheel,
// select, links, right-click) is the Help tab's page, not notes here.
#include "ui/config_page.h"
#include "ui/config_controls.h"
#include "ui/config_rows.h"
#include "model/ui_config.h"
#include "model/chat_log.h"   // chat_export_txt : the Export button
#include "gfx/font.h"
#include "gfx/draw.h"
#include <cstdio>

namespace aio {

static char g_chatExportMsg[160] = "";   // the last export's result, under its button
static int  g_banPage = 0;               // the banned-players list : the page shown
static int g_chatCfgBox = 0;   // 0 = Chat, 1 = Battle (which box the rows edit)

void ConfigPage::draw_chat_config(u32 dev, Font* fo, const MouseState* mo, bool click,
                                  float& ry, int& ri, float e,
                                  float bandX, float bandW, float coX, float ctrlW,
                                  float hdrX, float hdrW) {
    UiConfig& c = ui_config();
    const int b = (g_chatCfgBox == 1) ? 1 : 0;
    const char* BOXL[2] = { tr("Chat", "Discussion"), tr("Battle", "Combat") };

    // ===== WHICH BOX every section below edits : ONCE, above them (2026-11-26 : it sat in Display AND in Text) =====
    { ROW_BAND(52.0f)
        if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Box edited below", "Box r\xC3\xA9""gl\xC3\xA9""""e ci-dessous"), BOXL[b])) g_chatCfgBox = wrap(b + d, 2);
    } ROW_NEXT(52.0f)
    ry += snap(8.0f);
    // ===== DISPLAY (shown, size, its geometry) =====
    const float aF6_ = cat_fold(CTRL_ID, !ui_config().cfgAdv || (catOpen_[6]));
    cat_panel(dev, hdrX, ry, hdrW, cat_card_h(catH_[6], aF6_));
    if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Display", "Affichage"), catOpen_[6], aF6_)) catOpen_[6] = !catOpen_[6];
    ROW_NEXT(42.0f)
    if (aF6_ > 0.0f) {
        const float top6_ = ry;
        cat_fold_clip(dev, hdrX, top6_, hdrW, catH_[6] * aF6_);
        ROW_TOGGLE(CTRL_ID, tr("Show", "Afficher"), c.chShow[b])
        { ROW_BAND(46.0f)   // Size
            const float lo = 0.50f, hi = 2.00f; char s[16]; sprintf(s, "%d%%", (int)(c.chScale[b] * 100.0f + 0.5f));
            float v01 = clampf((c.chScale[b] - lo) / (hi - lo), 0.0f, 1.0f);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Size", "Taille"), s, &v01)) { float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f; c.chScale[b] = v < lo ? lo : (v > hi ? hi : v); }
        } ROW_NEXT(46.0f)
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
        // (the how-to lines -- Edit layout, the wheel, hiding the game's log -- are the Help tab's : Help -> Chat)
        cat_fold_end(dev, ry, top6_, catH_[6], aF6_);
    }
    ry += snap(16.0f);
    // ===== sub-section : FRAME (the config grammar : Display / Frame / Content / Behaviour / Text, docs/architecture/config-panels.md) =====
if (ui_config().cfgAdv) {   // Advanced only (the config's depth, 2026-11-26)
    const float aF20_ = cat_fold(CTRL_ID, catOpen_[20]);
    cat_panel(dev, hdrX, ry, hdrW, cat_card_h(catH_[20], aF20_));
    if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Frame", "Cadre"), catOpen_[20], aF20_)) catOpen_[20] = !catOpen_[20];
    ROW_NEXT(42.0f)
    if (aF20_ > 0.0f) {
        const float top20_ = ry;
        cat_fold_clip(dev, hdrX, top20_, hdrW, catH_[20] * aF20_);
        draw_box_appearance(dev, fo, mo, click, ry, ri, e, bandX, bandW, coX, ctrlW, c.chBox[b]);
        cat_fold_end(dev, ry, top20_, catH_[20], aF20_);
    }   // end Frame
    ry += snap(16.0f);
}   // end Advanced only
    // ===== sub-section : CONTENT (the config grammar : Display / Frame / Content / Behaviour / Text, docs/architecture/config-panels.md) =====
    const float aF21_ = cat_fold(CTRL_ID, !ui_config().cfgAdv || (catOpen_[21]));
    cat_panel(dev, hdrX, ry, hdrW, cat_card_h(catH_[21], aF21_));
    if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Content", "Contenu"), catOpen_[21], aF21_)) catOpen_[21] = !catOpen_[21];
    ROW_NEXT(42.0f)
    if (aF21_ > 0.0f) {
        const float top21_ = ry;
        cat_fold_clip(dev, hdrX, top21_, hdrW, catH_[21] * aF21_);
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
        ROW_TOGGLE(CTRL_ID, tr("Time stamps", "Horodatage"), c.chStamp[b])
        if (b == 0) { ROW_BAND(52.0f)   // where you TYPE : the game's own line, docked under the box, or inside it
            const char* IL[3] = { tr("Game's place", "Place du jeu"), tr("Under the box", "Sous la box"), tr("In the box", "Dans la box") };
            const int v = (c.chDock >= 0 && c.chDock <= 2) ? c.chDock : 2;
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Input line", "Barre de saisie"), IL[v])) c.chDock = wrap(v + d, 3);
        } if (b == 0) { ROW_NEXT(52.0f) }
        cat_fold_end(dev, ry, top21_, catH_[21], aF21_);
    }   // end Content
    ry += snap(16.0f);
    // ===== sub-section : BEHAVIOUR (the config grammar : Display / Frame / Content / Behaviour / Text, docs/architecture/config-panels.md) =====
if (ui_config().cfgAdv) {   // Advanced only (the config's depth, 2026-11-26)
    const float aF22_ = cat_fold(CTRL_ID, catOpen_[22]);
    cat_panel(dev, hdrX, ry, hdrW, cat_card_h(catH_[22], aF22_));
    if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Behaviour", "Comportement"), catOpen_[22], aF22_)) catOpen_[22] = !catOpen_[22];
    ROW_NEXT(42.0f)
    if (aF22_ > 0.0f) {
        const float top22_ = ry;
        cat_fold_clip(dev, hdrX, top22_, hdrW, catH_[22] * aF22_);
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
        cat_fold_end(dev, ry, top22_, catH_[22], aF22_);
    }   // end Behaviour
    ry += snap(16.0f);
}   // end Advanced only

    // ===== HISTORY (what was said : kept to a file). Its own section, not Behaviour's : an ACTION, not a setting,
    // and the place the history's next tools go (2026-11-17 config audit) =====
if (ui_config().cfgAdv) {   // Advanced only (the config's depth, 2026-11-26)
    const float aF24_ = cat_fold(CTRL_ID, catOpen_[24]);
    cat_panel(dev, hdrX, ry, hdrW, cat_card_h(catH_[24], aF24_));
    if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("History", "Historique"), catOpen_[24], aF24_)) catOpen_[24] = !catOpen_[24];
    ROW_NEXT(42.0f)
    if (aF24_ > 0.0f) {
        const float top24_ = ry;
        cat_fold_clip(dev, hdrX, top24_, hdrW, catH_[24] * aF24_);
        // the history to a text file (the Battle box : the combat log too)
        { ROW_BAND(48.0f)
            const float rowH = snap(38.0f), ty = ry + yo; fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, tr("Export the history", "Exporter l'historique"), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
            const float bw = snap(112.0f), bh = snap(34.0f);
            if (push_btn(dev, fo, mo, click, CTRL_ID, coX + ctrlW - bw, ty + (rowH - bh) * 0.5f, bw, bh, tr("Export", "Exporter"), 0)) {
                char nm[96]; const int n = chat_export_txt(b == 1, nm, sizeof(nm));
                if (n >= 0) _snprintf(g_chatExportMsg, sizeof(g_chatExportMsg), tr("%d lines -> plugins\\AioHud\\%s", "%d lignes -> plugins\\AioHud\\%s"), n, nm);
                else lstrcpynA(g_chatExportMsg, tr("Export failed (see aiohud_debug.log)", "\xC3\x89""chec de l'export (voir aiohud_debug.log)"), sizeof(g_chatExportMsg));
                g_chatExportMsg[sizeof(g_chatExportMsg) - 1] = 0;
            }
        } ROW_NEXT(48.0f)
        if (g_chatExportMsg[0]) ROW_NOTE(g_chatExportMsg)
        cat_fold_end(dev, ry, top24_, catH_[24], aF24_);
    }   // end History
    ry += snap(16.0f);
}   // end Advanced only

    // ===== BANNED PLAYERS (both boxes) : a LIST the player builds, so its own section, its count in the title -- it
    // can hold 200 names and must not push the settings around (2026-11-17 config audit) =====
    const int nBan = chat_ban_count();
    char hbBan[64]; _snprintf(hbBan, sizeof(hbBan), tr("Banned players (%d)", "Joueurs bannis (%d)"), nBan); hbBan[sizeof(hbBan) - 1] = 0;
if (ui_config().cfgAdv) {   // Advanced only (the config's depth, 2026-11-26)
    const float aF25_ = cat_fold(CTRL_ID, catOpen_[25]);
    cat_panel(dev, hdrX, ry, hdrW, cat_card_h(catH_[25], aF25_));
    if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, hbBan, catOpen_[25], aF25_)) catOpen_[25] = !catOpen_[25];
    ROW_NEXT(42.0f)
    if (aF25_ > 0.0f) {
        const float top25_ = ry;
        cat_fold_clip(dev, hdrX, top25_, hdrW, catH_[25] * aF25_);
        // a paged grid (2026-11-17 : "prevois si on a 100 personnes bannies") : 3 names a row, alphabetical, 8 rows a
        // page, a x on each name unbans it ; the pager only past one page
        if (nBan == 0) ROW_NOTE(tr("Nobody. Right-click one of their lines -> Ban : their messages leave the boxes.", "Personne. Clic droit sur une de ses lignes -> Bannir : ses messages quittent les box."))
        const int BAN_COLS = 3, BAN_ROWS = 8, BAN_PAGE = BAN_COLS * BAN_ROWS;
        const int banPages = (nBan + BAN_PAGE - 1) / BAN_PAGE;
        if (g_banPage >= banPages) g_banPage = banPages > 0 ? banPages - 1 : 0;
        if (banPages > 1) { ROW_BAND(46.0f)
            char pg[32]; _snprintf(pg, sizeof(pg), "%d / %d", g_banPage + 1, banPages); pg[sizeof(pg) - 1] = 0;
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Page", "Page"), pg)) g_banPage = wrap(g_banPage + d, banPages);
        } if (banPages > 1) { ROW_NEXT(46.0f) }
        {   int unban = -1;
            const int first = g_banPage * BAN_PAGE, last = first + BAN_PAGE < nBan ? first + BAN_PAGE : nBan;
            for (int r0 = first; r0 < last; r0 += BAN_COLS) {
                ROW_BAND(32.0f)
                const float ty = ry + yo - (snap(32.0f) - snap(40.0f)) * 0.5f, rowH = snap(32.0f), colW = snap(ctrlW / (float)BAN_COLS);
                for (int k = r0; k < r0 + BAN_COLS && k < last; ++k) {
                    const float cx = coX + colW * (float)(k - r0), xb = snap(26.0f);
                    fo->begin(dev);
                    fo->draw_lc(dev, snap(cx + 10.0f), ty + rowH * 0.5f, chat_ban_name(k), snap(14.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
                    if (push_btn(dev, fo, mo, click, ctrl_uid_i(CTRL_ID, k), snap(cx + colW - xb - 10.0f), ty + (rowH - xb) * 0.5f, xb, xb, "x", 0)) unban = k;
                }
                ROW_NEXT(32.0f)
            }
            if (unban >= 0) { char nm[20]; lstrcpynA(nm, chat_ban_name(unban), sizeof(nm)); chat_ban_remove(nm); }   // after the loop : the list moves
        }
        cat_fold_end(dev, ry, top25_, catH_[25], aF25_);
    }   // end Banned players
    ry += snap(16.0f);
}   // end Advanced only

    // ===== TEXT (of the box picked at the top) =====
if (ui_config().cfgAdv) {   // Advanced only (the config's depth, 2026-11-26)
    const float aF5_ = cat_fold(CTRL_ID, catOpen_[5]);
    cat_panel(dev, hdrX, ry, hdrW, cat_card_h(catH_[5], aF5_));
    if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Text", "Texte"), catOpen_[5], aF5_)) catOpen_[5] = !catOpen_[5];
    ROW_NEXT(42.0f)
    if (aF5_ > 0.0f) {
        const float top5_ = ry;
        cat_fold_clip(dev, hdrX, top5_, hdrW, catH_[5] * aF5_);
        draw_text_style(dev, fo, mo, click, ry, ri, e, bandX, bandW, coX, ctrlW, c.chText[b]);
        // the auto-translated words' colour (both boxes ; they are drawn bold, between the green / red brackets)
        { ROW_BAND(48.0f)
            const float rowH = snap(38.0f), ty = ry + yo; fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ty + rowH * 0.5f, tr("Auto-translate colour", "Couleur des auto-translate"), snap(15.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
            const float bw = snap(112.0f), bh = snap(34.0f);
            if (push_btn(dev, fo, mo, click, CTRL_ID, coX + ctrlW - bw, ty + (rowH - bh) * 0.5f, bw, bh, tr("Default", "D\xC3\xA9""faut"), 0)) { c.chAtColor = 0xFF9FE8CFu; save_ui_config(); }
        } ROW_NEXT(48.0f)
        CFG_COLOR_PICKER(&c.chAtColor)
        cat_fold_end(dev, ry, top5_, catH_[5], aF5_);
    }
    ry += snap(16.0f);
}   // end Advanced only
    if (!ui_config().cfgAdv) SIMPLE_MORE   // Simple : where the rest is
    #undef ROW_BAND
    #undef ROW_NEXT
}

} // namespace aio
