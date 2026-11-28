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
#include "ui/cfg_engine.h"    // the redesign : OptDef tables + the kit (draw_chat_v2)
#include "model/ui_config.h"
#include "model/chat_log.h"   // chat_export_txt : the Export button
#include "gfx/font.h"
#include "gfx/draw.h"
#include <cstdio>

namespace aio {

static char g_chatExportMsg[160] = "";   // the last export's result, under its button
static int  g_banPage = 0;               // the banned-players list : the page shown
static int  g_chanColSel = 0;            // Text > the talk channel whose colour is edited (0 say .. 9 assist)
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
    if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Display", "Affichage"), catOpen_[6], aF6_) && ui_config().cfgAdv) catOpen_[6] = !catOpen_[6];
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
            if (c.chMaxLines[b] > 0) sprintf(sb, "%d", c.chMaxLines[b]); else lstrcpynA(sb, tr("fixed height", "hauteur fixe"), sizeof(sb));   // 0 = the box keeps the fixed Height below
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
    if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Content", "Contenu"), catOpen_[21], aF21_) && ui_config().cfgAdv) catOpen_[21] = !catOpen_[21];
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

// ======== THE NEW CHAT PAGE (the config redesign, milestone 3, 2026-11-27) ========
// The box switch (Advanced : a segment, Simple : a selector), then Display [6] / Frame [20] / Tabs and header [21] /
// Messages [23] / History [24] / Banned players [25] / Behaviour [22] / Text [5] (Simple : one Content card [21]). The
// tables edit the box picked at the top (their field getters read g_chatCfgBox). The input line is the Chat box's
// alone : Advanced shows it only on that box. //aio config v1 : the old page.
static int chb() { return g_chatCfgBox == 1 ? 1 : 0; }
#define F_(name)  [](UiConfig& c) -> void* { return &c.name; }
#define FB_(name) [](UiConfig& c) -> void* { return &c.name[chb()]; }
static const char* const CH_FIT_EN[2] = { "Fixed", "Follows the content" },        * const CH_FIT_FR[2] = { "Fixe", "Suit le contenu" };
static const char* const CH_TAB_EN[4] = { "Auto", "Icon + name", "Icon", "Name" }, * const CH_TAB_FR[4] = { "Auto", "Ic\xC3\xB4ne + nom", "Ic\xC3\xB4ne", "Nom" };
static const char* const CH_DEN_EN[3] = { "Compact", "Normal", "Airy" },           * const CH_DEN_FR[3] = { "Compacte", "Normale", "A\xC3\xA9r\xC3\xA9""e" };
static const char* const CH_IN_EN[3] = { "Game's place", "Under the box", "In the box" }, * const CH_IN_FR[3] = { "Place du jeu", "Sous la box", "Dans la box" };
static const char* cw_chatonly(const UiConfig&) { return chb() == 0 ? nullptr : tr("inactive : the Chat box's only", "inactif : celui de la box Discussion"); }
static const OptDef CH_DISPLAY[] = {
    { "Show", "Afficher", OK_TOGGLE, OL_SIMPLE, FB_(chShow), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Size", "Taille", OK_PCT, OL_SIMPLE, FB_(chScale), 0.5f, 2.0f, nullptr, nullptr, 0, nullptr, 0 },
    { "Box height", "Hauteur de la box", OK_SELECT, OL_SIMPLE, FB_(chFit), 0, 0, CH_FIT_EN, CH_FIT_FR, 2, nullptr, 0 },
};
static const OptDef CH_CONTENT[] = {
    { "Tabs", "Onglets", OK_SELECT, OL_SIMPLE, FB_(chTabStyle), 0, 0, CH_TAB_EN, CH_TAB_FR, 4, nullptr, 0 },
    { "Header size", "Taille de l'en-t\xC3\xAAte", OK_PCT, OL_SIMPLE, FB_(chHeadScale), 0.6f, 1.6f, nullptr, nullptr, 0, nullptr, 0 },
    { "Density", "Densit\xC3\xA9", OK_SELECT, OL_SIMPLE, FB_(chDensity), 0, 0, CH_DEN_EN, CH_DEN_FR, 3, nullptr, 0 },
    { "Time stamps", "Horodatage", OK_TOGGLE, OL_SIMPLE, FB_(chStamp), 0, 0, nullptr, nullptr, 0, nullptr, 0 },
    { "Input line", "Barre de saisie", OK_SELECT, OL_SIMPLE, F_(chDock), 0, 0, CH_IN_EN, CH_IN_FR, 3, cw_chatonly, 0 },
};
#undef FB_
#undef F_
#define NOPT(a) ((int)(sizeof(a) / sizeof((a)[0])))

void ConfigPage::draw_chat_v2(u32 dev, Font* fo, const MouseState* mo, bool click,
                              float& ry, int& ri, float e,
                              float bandX, float bandW, float coX, float ctrlW,
                              float hdrX, float hdrW) {
    Kit K;
    if (!v2_kit(K, mo, click)) { draw_chat_config(dev, fo, mo, click, ry, ri, e, bandX, bandW, coX, ctrlW, hdrX, hdrW); return; }
    g_fade = e;
    UiConfig& c = ui_config();
    const bool adv = c.cfgAdv != 0;
    const float x = coX, w = ctrlW;
    const int base = CTRL_ID;
    const int b = chb();
    // an int slider (px, lines, seconds, percent), the old page's ranges / steps / value text
    auto islider = [&](int uid, const char* label, int* f, int shown, int lo, int hi, int step, const char* val, const char* why) {
        float v01 = ((float)shown - (float)lo) / (float)(hi - lo); v01 = v01 < 0.0f ? 0.0f : (v01 > 1.0f ? 1.0f : v01);
        if (cfg_slider_row(K, uid, x, ry, w, label, &v01, val, why, f, 4, false)) {
            int v = (int)((float)lo + v01 * (float)(hi - lo) + 0.5f); if (step > 1) v = (v / step) * step;
            *f = v < lo ? lo : (v > hi ? hi : v); }
    };
    // a row with one button at its right (the export, the colour's default, an unban) : true = pressed
    auto btn_row = [&](int uid, const char* label, const char* btn, bool modified) -> bool {
        const float h = K.px(52.0f), bw = K.px(150.0f);
        bool hit = false;
        if (ry + h >= K.clipTop && ry <= K.clipBot) {
            const int st = modified ? KS_MODIFIED : 0;
            kit_row(K, x, ry, w, h, st);
            kit_label(K, x + K.px(18.0f), ry + h * 0.5f, label, st);
            hit = kit_button(K, uid, x + w - K.px(18.0f) - bw, ry + (h - K.px(42.0f)) * 0.5f, bw, btn, 1, 0);
        }
        ry += h + K.px(4.0f);
        return hit;
    };
    char v[32];
    ry += K.px(6.0f);
    {   // WHICH BOX every section below edits : once, above them (Advanced : the Party page's switch)
        const char* BOXL[2] = { tr("Chat", "Discussion"), tr("Battle", "Combat") };
        if (adv) {
            const float segW = K.px(170.0f), sh = K.px(36.0f);
            const int hit = chrome_segment(K, ctrl_uid_i(base, 40), x + (w - 2.0f * segW) * 0.5f, ry + K.px(4.0f), segW, sh, BOXL, 2, b);
            if (hit >= 0) g_chatCfgBox = hit;
            ry += sh + K.px(16.0f);
        } else {
            if (int d = cfg_select_row(K, ctrl_uid_i(base, 1), x, ry, w, tr("Box edited below", "Box r\xC3\xA9""gl\xC3\xA9""e ci-dessous"), BOXL[b], nullptr, nullptr, 0)) g_chatCfgBox = wrap(b + d, 2);
            ry += K.px(8.0f);
        }
    }
    {   // ---- Display (shown, size, its geometry) ----
        const int nMod = cfg_count_modified(CH_DISPLAY, NOPT(CH_DISPLAY), adv) + cfg_count_modified_bytes(&c.chW[b], 4) + cfg_count_modified_bytes(&c.chMaxLines[b], 4);   // (not chH : its factory 0 means "fill from the lines", the first draw fills it -- it would read modified forever)
        if (v2_section(K, ctrl_uid_i(base, 2), 6, "Display", "Affichage", x, ry, w, nMod)) {
            cfg_rows(K, ctrl_uid_i(base, 3), CH_DISPLAY, NOPT(CH_DISPLAY), x, ry, w, adv);
            {   // which edge stays put ; the box does not move (no reset : the factory -1 is the pre-conversion marker)
                const char* GL[2] = { tr("Up (bottom stays)", "Le haut (bas fixe)"), tr("Down (top stays)", "Le bas (haut fixe)") };
                const int g = c.chGrow[b] == 1 ? 1 : 0;
                if (int d = cfg_select_row(K, ctrl_uid_i(base, 4), x, ry, w, tr("Grows toward", "Grandit vers"), GL[g], nullptr, nullptr, 0)) {
                    const int nv = wrap(g + d, 2);
                    if (nv != g && c.chEdge[b][1] > c.chEdge[b][0]) c.chY[b] = nv == 0 ? c.chEdge[b][1] : c.chEdge[b][0];
                    c.chGrow[b] = nv; save_ui_config();
                }
            }
            _snprintf(v, sizeof(v), "%d", c.chW[b]); v[31] = 0;
            islider(ctrl_uid_i(base, 5), tr("Width", "Largeur"), &c.chW[b], c.chW[b], 200, 1600, 10, v, nullptr);
            const int hh = c.chH[b] > 0 ? c.chH[b] : 260;
            _snprintf(v, sizeof(v), "%d", hh); v[31] = 0;
            {   // no bead / reset on the height (see the count above) ; 80..1400 = the clamp and Edit Layout's range
                float h01 = ((float)hh - 80.0f) / (1400.0f - 80.0f); h01 = h01 < 0.0f ? 0.0f : (h01 > 1.0f ? 1.0f : h01);
                if (cfg_slider_row(K, ctrl_uid_i(base, 6), x, ry, w, tr("Height", "Hauteur"), &h01, v,
                                   c.chMaxLines[b] == 0 ? nullptr : tr("inactive : Max lines sets the height", "inactif : Lignes max fixe la hauteur"), nullptr, 0, false)) {
                    int nv = (int)(80.0f + h01 * 1320.0f + 0.5f); nv = (nv / 10) * 10; c.chH[b] = nv < 80 ? 80 : (nv > 1400 ? 1400 : nv); }
            }
            if (c.chMaxLines[b] > 0) _snprintf(v, sizeof(v), "%d", c.chMaxLines[b]); else lstrcpynA(v, tr("fixed height", "hauteur fixe"), sizeof(v));   // 0 = the fixed Height above
            v[31] = 0;
            islider(ctrl_uid_i(base, 7), tr("Max lines", "Lignes max"), &c.chMaxLines[b], c.chMaxLines[b], 0, 40, 1, v, nullptr);
            v2_end(K, ry);
        }
    }
    if (adv) {   // ---- Frame ----
        BoxStyle& bs = c.chBox[b];
        const FrameRefs fr = { &bs.on, &bs.border, &bs.alpha, &bs.alphaOwn, &bs.themeCopy, &bs.theme, &bs.hue, &bs.lum };
        if (v2_section(K, ctrl_uid_i(base, 8), 20, "Frame", "Cadre", x, ry, w, cfg_frame_modified(fr))) { cfg_frame_rows(K, ctrl_uid_i(base, 9), x, ry, w, fr); v2_end(K, ry); }
    }
    if (!adv) {   // Simple : one Content card, as before
        if (v2_section(K, ctrl_uid_i(base, 10), 21, "Content", "Contenu", x, ry, w, cfg_count_modified(CH_CONTENT, NOPT(CH_CONTENT), false)))
            v2_rows(K, ctrl_uid_i(base, 11), CH_CONTENT, NOPT(CH_CONTENT), x, ry, w);
    } else {   // Advanced : by ELEMENT. Slices of CH_CONTENT : [0..1] tabs + header, [2..3] density + stamps, [4] the input line --
               // the Chat box's alone : shown only on that box, not greyed on the other
        static_assert(NOPT(CH_CONTENT) == 5, "the slices below index CH_CONTENT");
        if (v2_section(K, ctrl_uid_i(base, 41), 21, "Tabs and header", "Onglets et en-t\xC3\xAAte", x, ry, w, cfg_count_modified(CH_CONTENT, 2, true)))
            v2_rows(K, ctrl_uid_i(base, 42), CH_CONTENT, 2, x, ry, w);
        const int nMsg = cfg_count_modified(CH_CONTENT + 2, 2, true) + (b == 0 ? cfg_count_modified(CH_CONTENT + 4, 1, true) : 0);
        if (v2_section(K, ctrl_uid_i(base, 43), 23, "Messages", "Messages", x, ry, w, nMsg)) {
            cfg_rows(K, ctrl_uid_i(base, 44), CH_CONTENT + 2, 2, x, ry, w, true);
            if (b == 0) cfg_rows(K, ctrl_uid_i(base, 45), CH_CONTENT + 4, 1, x, ry, w, true);
            v2_end(K, ry);
        }
    }
    if (adv) {
        {   // ---- Behaviour : when nothing happens, collapse and / or fade, each on its own delay (chBehave bits 2 / 1) ----
            const int nMod = cfg_count_modified_bytes(&c.chBehave[b], 4) + cfg_count_modified_bytes(&c.chIdle[b], 4) + cfg_count_modified_bytes(&c.chFoldLines[b], 4)
                           + cfg_count_modified_bytes(&c.chFadeIdle[b], 4) + cfg_count_modified_bytes(&c.chFadeTo[b], 4);
            if (v2_section(K, ctrl_uid_i(base, 12), 22, "Behaviour", "Comportement", x, ry, w, nMod)) {
                // the two bits share one int : no per-row marker / reset (the section count still sees them)
                if (cfg_toggle_row(K, ctrl_uid_i(base, 13), x, ry, w, tr("Collapse when idle", "Repli au repos"), (c.chBehave[b] & 2) != 0, nullptr, nullptr, 0)) { c.chBehave[b] ^= 2; save_ui_config(); }
                const char* noFold = (c.chBehave[b] & 2) ? nullptr : tr("inactive : collapse off", "inactif : repli coup\xC3\xA9");
                _snprintf(v, sizeof(v), "%d s", c.chIdle[b]); v[31] = 0;
                islider(ctrl_uid_i(base, 14), tr("Each line stays", "Chaque ligne reste"), &c.chIdle[b], c.chIdle[b], 3, 120, 1, v, noFold);
                _snprintf(v, sizeof(v), "%d", c.chFoldLines[b]); v[31] = 0;
                islider(ctrl_uid_i(base, 15), tr("Lines kept", "Lignes gard\xC3\xA9""es"), &c.chFoldLines[b], c.chFoldLines[b], 1, 20, 1, v, noFold);
                if (cfg_toggle_row(K, ctrl_uid_i(base, 16), x, ry, w, tr("Fade when idle", "Fondu au repos"), (c.chBehave[b] & 1) != 0, nullptr, nullptr, 0)) { c.chBehave[b] ^= 1; save_ui_config(); }
                const char* noFade = (c.chBehave[b] & 1) ? nullptr : tr("inactive : fade off", "inactif : fondu coup\xC3\xA9");
                _snprintf(v, sizeof(v), "%d s", c.chFadeIdle[b]); v[31] = 0;
                islider(ctrl_uid_i(base, 17), tr("Fade after", "Fondu apr\xC3\xA8s"), &c.chFadeIdle[b], c.chFadeIdle[b], 3, 120, 1, v, noFade);
                _snprintf(v, sizeof(v), "%d%%", c.chFadeTo[b]); v[31] = 0;
                islider(ctrl_uid_i(base, 18), tr("Faded opacity", "Opacit\xC3\xA9 au repos"), &c.chFadeTo[b], c.chFadeTo[b], 0, 80, 1, v, noFade);
                v2_end(K, ry);
            }
        }
        // ---- History : what was said, to a text file (the Battle box : the combat log too) ----
        if (v2_section(K, ctrl_uid_i(base, 19), 24, "History", "Historique", x, ry, w, 0)) {
            if (btn_row(ctrl_uid_i(base, 20), tr("Export the history", "Exporter l'historique"), tr("Export", "Exporter"), false)) {
                char nm[96]; const int n = chat_export_txt(b == 1, nm, sizeof(nm));
                if (n >= 0) _snprintf(g_chatExportMsg, sizeof(g_chatExportMsg), tr("%d lines -> plugins\\AioHud\\%s", "%d lignes -> plugins\\AioHud\\%s"), n, nm);
                else lstrcpynA(g_chatExportMsg, tr("Export failed (see aiohud_debug.log)", "\xC3\x89""chec de l'export (voir aiohud_debug.log)"), sizeof(g_chatExportMsg));
                g_chatExportMsg[sizeof(g_chatExportMsg) - 1] = 0;
            }
            if (g_chatExportMsg[0]) cfg_note(K, x, ry, g_chatExportMsg);
            v2_end(K, ry);
        }
        // ---- Banned players (both boxes) : a paged list, its count in the title ; a button on each name unbans it ----
        const int nBan = chat_ban_count();
        char hb[64]; _snprintf(hb, sizeof(hb), tr("Banned players (%d)", "Joueurs bannis (%d)"), nBan); hb[sizeof(hb) - 1] = 0;
        if (v2_section(K, ctrl_uid_i(base, 21), 25, hb, hb, x, ry, w, 0)) {
            if (nBan == 0) cfg_note(K, x, ry, tr("Nobody. Right-click one of their lines -> Ban : their messages leave the boxes.", "Personne. Clic droit sur une de ses lignes -> Bannir : ses messages quittent les box."));
            const int BAN_PAGE = 12, banPages = (nBan + BAN_PAGE - 1) / BAN_PAGE;
            if (g_banPage >= banPages) g_banPage = banPages > 0 ? banPages - 1 : 0;
            if (banPages > 1) {
                _snprintf(v, sizeof(v), "%d / %d", g_banPage + 1, banPages); v[31] = 0;
                if (int d = cfg_select_row(K, ctrl_uid_i(base, 22), x, ry, w, tr("Page", "Page"), v, nullptr, nullptr, 0)) g_banPage = wrap(g_banPage + d, banPages);
            }
            int unban = -1;
            const int first = g_banPage * BAN_PAGE, last = first + BAN_PAGE < nBan ? first + BAN_PAGE : nBan;
            const int ubase = ctrl_uid_i(base, 23);
            for (int k = first; k < last; ++k)
                if (btn_row(ctrl_uid_i(ubase, k), chat_ban_name(k), tr("Unban", "D\xC3\xA9""bannir"), false)) unban = k;
            if (unban >= 0) { char nm[20]; lstrcpynA(nm, chat_ban_name(unban), sizeof(nm)); chat_ban_remove(nm); }   // after the loop : the list moves
            v2_end(K, ry);
        }
        // ---- Text (of the box picked at the top) + the auto-translated words' colour (both boxes) ----
        const char* lab[1] = { b == 1 ? tr("Battle", "Combat") : tr("Chat", "Discussion") };
        TextStyle* ts[1] = { &c.chText[b] };
        const bool atMod = cfg_count_modified_bytes(&c.chAtColor, 4) != 0;
        const int colMod = cfg_count_modified_bytes(&c.chColGame, 4) + cfg_count_modified_bytes(c.chChanCol, (int)sizeof(c.chChanCol));
        if (v2_section(K, ctrl_uid_i(base, 24), 5, "Text", "Texte", x, ry, w, cfg_text_modified(ts, 1) + (atMod ? 1 : 0) + colMod)) {
            int sel = 0;
            cfg_text_rows(K, ctrl_uid_i(base, 25), x, ry, w, lab, ts, 1, &sel);
            if (btn_row(ctrl_uid_i(base, 26), tr("Auto-translate colour", "Couleur des auto-translate"), tr("Default", "D\xC3\xA9""faut"), atMod)) { c.chAtColor = 0xFF9FE8CFu; save_ui_config(); }
            cfg_color_row(K, ctrl_uid_i(base, 27), x, ry, w, &c.chAtColor);
            // the TALK channels : the name and its message in ONE colour -- the game's (its Font Colors) or ours per channel
            static const char* const SRC[2][2] = { { "AioHUD's", "Celles d'AioHUD" }, { "The game's", "Celles du jeu" } };
            if (int d = cfg_select_row(K, ctrl_uid_i(base, 46), x, ry, w, tr("Channel colours", "Couleurs des canaux"),
                                       tr(SRC[c.chColGame ? 1 : 0][0], SRC[c.chColGame ? 1 : 0][1]), nullptr, &c.chColGame, 4)) { (void)d; c.chColGame = c.chColGame ? 0 : 1; save_ui_config(); }
            if (!c.chColGame) {
                static const char* const CHN[10][2] = { { "Say", "Dire" }, { "Shout", "Crier" }, { "Yell", "Hurler" }, { "Tell", "Tell" }, { "Party", "Groupe" },
                                                        { "Linkshell 1", "Linkshell 1" }, { "Linkshell 2", "Linkshell 2" }, { "Unity", "Unity" }, { "Emote", "Emote" }, { "Assist", "Assist" } };
                const int k = g_chanColSel >= 0 && g_chanColSel < 10 ? g_chanColSel : 0;
                if (int d = cfg_select_row(K, ctrl_uid_i(base, 47), x, ry, w, tr("Channel", "Canal"), tr(CHN[k][0], CHN[k][1]), nullptr, nullptr, 0)) g_chanColSel = wrap(k + d, 10);
                static const UiConfig def{};   // the factory colours (built once, not per frame)
                const bool chMod = c.chChanCol[k] != def.chChanCol[k];
                if (btn_row(ctrl_uid_i(base, 48), tr("Its colour (name and message)", "Sa couleur (nom et message)"), tr("Default", "D\xC3\xA9""faut"), chMod)) { c.chChanCol[k] = def.chChanCol[k]; save_ui_config(); }
                cfg_color_row(K, ctrl_uid_i(base, 49), x, ry, w, &c.chChanCol[k]);
            } else cfg_note(K, x, ry, tr("Each channel in the colour set in the game (Config > Font Colors) ; a line the game gives none keeps AioHUD's.",
                                         "Chaque canal dans la couleur r\xC3\xA9""gl\xC3\xA9""e dans le jeu (Config > Font Colors) ; une ligne sans couleur du jeu garde celle d'AioHUD."));
            v2_end(K, ry);
        }
    } else v2_simple_note(K, x, ry);
    g_fade = e;
}
#undef NOPT

} // namespace aio
