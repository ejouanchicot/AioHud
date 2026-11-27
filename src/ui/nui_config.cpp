// nui_config.cpp -- the "Game UI" settings panel (//aio config -> "Game UI") : the game's windows AioHUD redraws or
// hides, their look, the name plates, and moving the game's menus. The engine is model/native_ui.cpp ; this page writes
// UiConfig::nuHide / mu* / pl* / nuMenuDx / nuMenuDy. Each control is keyed by CTRL_ID (file:line hash, see
// config_controls.h). Sections : catOpen_ [6] [20] [21] [23] [24] [26] [25] [22] ; every window : Modern / FFXI.
#include "ui/config_page.h"
#include "ui/config_controls.h"    // cat_header / row_toggle / row_slider / push_btn + palette
#include "ui/config_rows.h"        // ROW_BAND / ROW_NEXT
#include "ui/cfg_engine.h"         // the redesign : OptDef tables + the kit (draw_nui_v2)
#include "ui/hud_internal.h"       // plate_font_count / plate_font_label : the name plates' fonts
#include "model/ui_config.h"
#include "model/native_ui.h"       // NativeUiBits
#include "gfx/font.h"
#include "gfx/draw.h"
#include <cstdio>
#include <cmath>                   // fabsf : the plates' gap against its factory value

namespace aio {

void ConfigPage::draw_nui_config(u32 dev, Font* fo, const MouseState* mo, bool click,
                                 float& ry, int& ri, float e,
                                 float bandX, float bandW, float coX, float ctrlW,
                                 float hdrX, float hdrW) {
    UiConfig& c = ui_config();
    // The page, the config grammar (docs/architecture/config-panels.md) : Display [6] (the redrawn menus' size and
    // places) / Frame [20] (their theme) / three named subjects -- Menus and screens [21], NPCs and trade [23], On
    // screen [24] -- each switch saying what it really covers (2026-11-26 : the reverse grew each one far beyond its
    // first name) / Game windows hidden [25] (no replacement : AioHUD's own modules) / Behaviour [22] (move the menus).
    // one row per switch of nuHide (row_toggle already saved the OLD mask -> save again)
    // every window : MODERN (AioHUD's) or FFXI (the game's own) -- the user 2026-11-26, rather than On / Off
    const char* STY[2] = { tr("FFXI", "FFXI"), tr("Modern", "Moderne") };
    #define NUI_BIT_ROW(UID, LABEL, BIT) { ROW_BAND(52.0f) const int v_ = (c.nuHide & (BIT)) ? 1 : 0; \
        if (row_selector(dev, fo, mo, click, (UID), coX, ry + yo, ctrlW, (LABEL), STY[v_])) { c.nuHide = v_ ? (c.nuHide & ~(BIT)) : (c.nuHide | (BIT)); save_ui_config(); } \
        } ROW_NEXT(52.0f)
    #define NUI_SECTION(SLOT, TITLE) const float aF##SLOT##_ = cat_fold(CTRL_ID, !c.cfgAdv || catOpen_[SLOT]);   /* Simple : open */ \
        cat_panel(dev, hdrX, ry, hdrW, cat_card_h(catH_[SLOT], aF##SLOT##_)); \
        if (cat_header(dev, fo, mo, click, ctrl_uid_i(CTRL_ID, 1), hdrX, ry, hdrW, (TITLE), catOpen_[SLOT], aF##SLOT##_) && c.cfgAdv) catOpen_[SLOT] = !catOpen_[SLOT]; \
        ROW_NEXT(42.0f) \
        if (aF##SLOT##_ > 0.0f) { const float top##SLOT##_ = ry; cat_fold_clip(dev, hdrX, top##SLOT##_, hdrW, catH_[SLOT] * aF##SLOT##_);
    #define NUI_SECTION_END(SLOT) cat_fold_end(dev, ry, top##SLOT##_, catH_[SLOT], aF##SLOT##_); } ry += snap(16.0f);

    // ===== THE WHOLE INTERFACE at once : Modern / FFXI (Mixed when the windows below differ) =====
    { ROW_BAND(52.0f)
        const int allBits = NU_ALL; const bool allM = (c.nuHide & allBits) == allBits && c.plShow, allF = (c.nuHide & allBits) == 0 && !c.plShow;
        const char* lab = allM ? STY[1] : allF ? STY[0] : tr("Mixed", "Mixte");
        if (row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Game interface", "Interface du jeu"), lab)) {
            const bool toModern = !allM; c.nuHide = toModern ? (c.nuHide | allBits) : (c.nuHide & ~allBits); c.plShow = toModern ? 1 : 0; save_ui_config(); }
    } ROW_NEXT(52.0f)
    ry += snap(8.0f);

    // ===== DISPLAY : the redrawn menus' size and places =====
    if (c.cfgAdv) {   // Advanced only (the config's depth, 2026-11-26)
    NUI_SECTION(6, tr("Display", "Affichage"))
        { ROW_BAND(46.0f)   // the menus' size : every dimension, the text included
            const float lo = 0.60f, hi = 1.60f; char sb[16]; sprintf(sb, "%d%%", (int)(c.muScale * 100.0f + 0.5f));
            float v01 = clampf((c.muScale - lo) / (hi - lo), 0.0f, 1.0f);
            if (row_slider(dev, fo, mo, CTRL_ID, coX, ry + yo, ctrlW, tr("Menu size", "Taille des menus"), sb, &v01)) { float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f; c.muScale = v < lo ? lo : (v > hi ? hi : v); }
        } ROW_NEXT(46.0f)
        { ROW_BAND(52.0f)   // the main menu : where the game puts it (top right) or top left
            const char* PL[2] = { tr("Game's place", "Place du jeu"), tr("Top left", "En haut \xC3\xA0 gauche") };
            const int v = c.muMainPos == 1 ? 1 : 0;
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Main menu position", "Position du menu principal"), PL[v])) { c.muMainPos = (v + d + 2) % 2; save_ui_config(); }
        } ROW_NEXT(52.0f)
        { ROW_BAND(52.0f)   // a submenu with more rows than its menu : grows up (bottoms level) or down (tops level)
            const char* GL[2] = { tr("Upward", "Vers le haut"), tr("Downward", "Vers le bas") };
            const int v = c.muGrow == 1 ? 1 : 0;
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Taller submenu grows", "Sous-menu plus grand : grandit"), GL[v])) { c.muGrow = (v + d + 2) % 2; save_ui_config(); }
        } ROW_NEXT(52.0f)
        { ROW_BAND(52.0f)   // the rows : text only, or icon + text
            const char* LL[2] = { tr("Text only", "Texte seul"), tr("Icon + text", "Ic\xC3\xB4ne + texte") };
            const int v = c.muLabel == 1 ? 1 : 0;
            if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Menu rows", "Lignes des menus"), LL[v])) { c.muLabel = (v + d + 2) % 2; save_ui_config(); }
        } ROW_NEXT(52.0f)
    NUI_SECTION_END(6)

    // ===== FRAME : one theme for every menu AioHUD redraws =====
    NUI_SECTION(20, tr("Frame", "Cadre"))
        draw_box_appearance(dev, fo, mo, click, ry, ri, e, bandX, bandW, coX, ctrlW, c.muBox);
    NUI_SECTION_END(20)
    }   // end Advanced only

    // ===== MENUS AND SCREENS : the game's own, redrawn =====
    NUI_SECTION(21, tr("Menus and screens", "Menus et \xC3\xA9""crans"))
        NUI_BIT_ROW(CTRL_ID, tr("Main menu", "Menu principal"), NU_MAIN)
        ROW_NOTE(tr("And every screen it opens : status, equipment, items, magic, map, Region Info, macros, missions, friend list...", "Et tous les \xC3\xA9""crans qu'il ouvre : statut, \xC3\xA9quipement, objets, magie, carte, Region Info, macros, missions, amis..."))
        NUI_BIT_ROW(CTRL_ID, tr("Action menu", "Menu d'action"), NU_ACT)
        ROW_NOTE(tr("Enter on a target : attack, magic, abilities, items, trade, check.", "Entr\xC3\xA9""e sur une cible : attaque, magie, aptitudes, objets, \xC3\xA9""change, examiner."))
        NUI_BIT_ROW(CTRL_ID, tr("Top bar (menu name + help)", "Barre du haut (nom du menu + aide)"), NU_HELP)
        NUI_BIT_ROW(CTRL_ID, tr("Yes / No confirms", "Confirmations Oui / Non"), NU_DIALOG)
    NUI_SECTION_END(21)

    // ===== NPCS AND TRADE =====
    NUI_SECTION(23, tr("NPCs and trade", "PNJ et \xC3\xA9""changes"))
        NUI_BIT_ROW(CTRL_ID, tr("NPCs", "PNJ"), NU_NPC)
        ROW_NOTE(tr("Their dialogue and answers, shops, the auction house, the delivery box, Config's lists.", "Leurs dialogues et r\xC3\xA9ponses, boutiques, h\xC3\xB4tel des ventes, bo\xC3\xAEte de livraison, listes de la Config."))
        NUI_BIT_ROW(CTRL_ID, tr("Trade (player / NPC)", "\xC3\x89""change (joueur / PNJ)"), NU_TRADE)
    NUI_SECTION_END(23)

    // ===== ON SCREEN : what the game shows over the world =====
    NUI_SECTION(24, tr("On screen", "\xC3\x80 l'\xC3\xA9""cran"))
        NUI_BIT_ROW(CTRL_ID, tr("Macro bar (Ctrl / Alt)", "Barre de macros (Ctrl / Alt)"), NU_MACRO)
        NUI_BIT_ROW(CTRL_ID, tr("Your buff icons (top)", "Tes ic\xC3\xB4nes de buff (en haut)"), NU_BUFF)
        NUI_BIT_ROW(CTRL_ID, tr("Connection box (top right)", "Box connexion (en haut \xC3\xA0 droite)"), NU_NET)
    NUI_SECTION_END(24)

    // ===== NAMES OVER THE CHARACTERS (the preview shows sample plates while this section is open) =====
    NUI_SECTION(26, tr("Names over the characters", "Noms au-dessus des persos"))
        { ROW_BAND(52.0f) const int v = c.plShow ? 1 : 0;
            if (row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Style", "Style"), STY[v])) { c.plShow = v ? 0 : 1; save_ui_config(); }
        } ROW_NEXT(52.0f)
        if (c.plShow) {
            { ROW_BAND(52.0f) int fc = c.plFont; if (fc < 0 || fc >= plate_font_count()) fc = 0;
                if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Font", "Police"), plate_font_label(fc))) { c.plFont = (fc + d + plate_font_count()) % plate_font_count(); save_ui_config(); }
            } ROW_NEXT(52.0f)
            #define NUI_SCALE_ROW(UID, LABEL, FIELD) { ROW_BAND(46.0f) const float lo_ = 0.6f, hi_ = 1.6f; char b_[16]; sprintf(b_, "%d%%", (int)((FIELD) * 100.0f + 0.5f)); \
                float v01_ = clampf(((FIELD) - lo_) / (hi_ - lo_), 0.0f, 1.0f); \
                if (row_slider(dev, fo, mo, (UID), coX, ry + yo, ctrlW, (LABEL), b_, &v01_)) { float v_ = lo_ + v01_ * (hi_ - lo_); v_ = (float)((int)(v_ / 0.05f + 0.5f)) * 0.05f; (FIELD) = v_ < lo_ ? lo_ : (v_ > hi_ ? hi_ : v_); } \
                } ROW_NEXT(46.0f)
            NUI_SCALE_ROW(CTRL_ID, tr("Text size", "Taille du texte"), c.plSize)
            { ROW_BAND(52.0f)   // the names' size : with the distance (as the game) or FIXED, every one as thin as a far name
                const int v = (c.plStyle & 0x1000) ? 1 : 0;
                const char* ZL[2] = { tr("With the distance", "Selon la distance"), tr("Fixed (thin, as far)", "Fixe (fin, comme au loin)") };
                if (row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Names size", "Taille des noms"), ZL[v])) { c.plStyle ^= 0x1000; save_ui_config(); }
            } ROW_NEXT(52.0f)
            if (c.cfgAdv) {   // Advanced : icons, stars, places, italic / caps
            NUI_SCALE_ROW(CTRL_ID, tr("Icon size (pearl...)", "Taille des ic\xC3\xB4nes (perle...)"), c.plIcon)
            NUI_SCALE_ROW(CTRL_ID, tr("Master Level stars", "\xC3\x89toiles du Master Level"), c.plStar)
            #undef NUI_SCALE_ROW
            // WHERE : the icons' gap before the name (in capital W), the stars' height above it and their spread
            #define NUI_POS_ROW(UID, LABEL, FIELD, LO, HI, FMT, K) { ROW_BAND(46.0f) const float lo_ = (LO), hi_ = (HI); char b_[16]; sprintf(b_, (FMT), (FIELD) * (K)); \
                float v01_ = clampf(((FIELD) - lo_) / (hi_ - lo_), 0.0f, 1.0f); \
                if (row_slider(dev, fo, mo, (UID), coX, ry + yo, ctrlW, (LABEL), b_, &v01_)) { float v_ = lo_ + v01_ * (hi_ - lo_); v_ = (float)((int)(v_ / 0.05f + 0.5f)) * 0.05f; (FIELD) = v_ < lo_ ? lo_ : (v_ > hi_ ? hi_ : v_); } \
                } ROW_NEXT(46.0f)
            NUI_POS_ROW(CTRL_ID, tr("Icon gap before the name", "\xC3\x89""cart ic\xC3\xB4ne / nom"), c.plGap, 0.0f, 3.0f, "%.1f W", 1.0f)
            NUI_POS_ROW(CTRL_ID, tr("Stars height", "Hauteur des \xC3\xA9toiles"), c.plStarY, 0.5f, 2.0f, "%.0f%%", 100.0f)
            NUI_POS_ROW(CTRL_ID, tr("Stars spread", "\xC3\x89""cartement des \xC3\xA9toiles"), c.plStarX, 0.5f, 2.0f, "%.0f%%", 100.0f)
            #undef NUI_POS_ROW
            }
            // the name's style : bold / italic / CAPS (one toggle each)
            #define NUI_STYLE_ROW(UID, LABEL, BITV) { ROW_BAND(48.0f) int on_ = (c.plStyle & (BITV)) ? 1 : 0; \
                if (row_toggle(dev, fo, mo, click, (UID), coX, ry + yo, ctrlW, (LABEL), &on_)) { c.plStyle = on_ ? (c.plStyle | (BITV)) : (c.plStyle & ~(BITV)); save_ui_config(); } \
                } ROW_NEXT(48.0f)
            { ROW_BAND(52.0f)   // the weight : ultra-thin (what a far name looks like) .. bold
                int w = (c.plStyle >> 8) & 7; if (!w && (c.plStyle & 1)) w = 4; if (w > 4) w = 0;
                const char* WL[5] = { tr("Normal", "Normal"), tr("Ultra-thin", "Ultra-fin"), tr("Thin", "Fin"), tr("Semi-bold", "Semi-gras"), tr("Bold", "Gras") };
                static const int ORD[5] = { 1, 2, 0, 3, 4 };   // the arrows walk thin -> bold
                int at = 0; for (int i = 0; i < 5; ++i) if (ORD[i] == w) at = i;
                if (int d = row_selector(dev, fo, mo, click, CTRL_ID, coX, ry + yo, ctrlW, tr("Weight", "Graisse"), WL[w])) {
                    at = (at + d + 5) % 5; c.plStyle = (c.plStyle & ~0x701 & ~1) | (ORD[at] << 8); save_ui_config(); }
            } ROW_NEXT(52.0f)
            if (c.cfgAdv) {
            NUI_STYLE_ROW(CTRL_ID, tr("Italic", "Italique"), 2)
            NUI_STYLE_ROW(CTRL_ID, tr("CAPS", "Majuscules"), 4)
            }
            #undef NUI_STYLE_ROW
        }
    NUI_SECTION_END(26)

    // ===== GAME WINDOWS REPLACED : no redraw -- AioHUD's own modules take their place : Shown / Hidden (audit 2026-11-26) =====
    const char* SHW[2] = { tr("Shown", "Affich\xC3\xA9""e"), tr("Hidden", "Cach\xC3\xA9""e") };
    #define NUI_HIDE_ROW(UID, LABEL, BIT) { ROW_BAND(52.0f) const int v_ = (c.nuHide & (BIT)) ? 1 : 0; \
        if (row_selector(dev, fo, mo, click, (UID), coX, ry + yo, ctrlW, (LABEL), SHW[v_])) { c.nuHide = v_ ? (c.nuHide & ~(BIT)) : (c.nuHide | (BIT)); save_ui_config(); } \
        } ROW_NEXT(52.0f)
    NUI_SECTION(25, tr("Game windows replaced by AioHUD", "Fen\xC3\xAAtres du jeu remplac\xC3\xA9""es par AioHUD"))
        ROW_NOTE(tr("The party and alliance windows follow AioHUD's boxes : hidden while they show.", "Les fen\xC3\xAAtres de groupe et d'alliance suivent les bo\xC3\xAEtes AioHUD : masqu\xC3\xA9""es tant qu'elles s'affichent."))
        NUI_HIDE_ROW(CTRL_ID, tr("Chat log", "Journal du chat"), NU_CHAT)
    NUI_SECTION_END(25)
    #undef NUI_HIDE_ROW

    // ===== BEHAVIOUR : move the game's menus =====
    if (c.cfgAdv) {   // Advanced only
    NUI_SECTION(22, tr("Behaviour", "Comportement"))
        // -1000 .. +1000 screen px, 10 px steps ; persists on release (row_slider)
        #define NUI_OFFSET_ROW(UID, LABEL, FIELD) { ROW_BAND(46.0f) const float lo_ = -1000.0f, hi_ = 1000.0f; char b_[16]; \
            _snprintf(b_, sizeof(b_), "%+d px", (FIELD)); b_[sizeof(b_) - 1] = 0; \
            float v01_ = clampf(((float)(FIELD) - lo_) / (hi_ - lo_), 0.0f, 1.0f); \
            if (row_slider(dev, fo, mo, (UID), coX, ry + yo, ctrlW, (LABEL), b_, &v01_)) { \
                int v_ = (int)(lo_ + v01_ * (hi_ - lo_)); v_ = (v_ >= 0 ? (v_ + 5) : (v_ - 5)) / 10 * 10; (FIELD) = v_; } \
            } ROW_NEXT(46.0f)
        NUI_OFFSET_ROW(CTRL_ID, tr("Move the menus : horizontal", "D\xC3\xA9placer les menus : horizontal"), c.nuMenuDx)
        NUI_OFFSET_ROW(CTRL_ID, tr("Move the menus : vertical", "D\xC3\xA9placer les menus : vertical"), c.nuMenuDy)
        #undef NUI_OFFSET_ROW
        { ROW_BAND(48.0f)   // back to the game's own place
            const float bw = snap(150.0f);
            if (push_btn(dev, fo, mo, click, CTRL_ID, coX + ctrlW - bw, ry + yo, bw, snap(34.0f), tr("Reset", "R\xC3\xA9initialiser"), 0)) {
                c.nuMenuDx = 0; c.nuMenuDy = 0; save_ui_config();
            }
            fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ry + yo + snap(17.0f), tr("Back to the game's place", "Retour \xC3\xA0 la place du jeu"), snap(14.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
        } ROW_NEXT(48.0f)
        ROW_NOTE(tr("Turning an option off or unloading AioHUD puts everything back.", "D\xC3\xA9sactiver une option ou d\xC3\xA9""charger AioHUD remet tout en place."))
    NUI_SECTION_END(22)
    }   // end Advanced only
    if (!c.cfgAdv) SIMPLE_MORE

    #undef NUI_SECTION
    #undef NUI_SECTION_END
    #undef NUI_BIT_ROW
    #undef ROW_BAND
    #undef ROW_NEXT
}


// ======== THE NEW GAME UI PAGE (the config redesign, milestone 3, 2026-11-26) ========
// The old page's sections, in its order. Most rows are BITS of nuHide / plStyle : the engine marks / resets a whole
// field, so those rows carry no marker of their own (a right-click would reset every bit) ; their section's count is
// worked out here, bit by bit, against the factory value. The names' rows the old page hid with the game's style are
// hidden in Advanced too (they exist only for the Modern names) and GREYED with the reason in Simple.
#define F_(name) [](UiConfig& c) -> void* { return &c.name; }
static const char* const N_STY_EN[2] = { "FFXI", "Modern" },                     * const N_STY_FR[2] = { "FFXI", "Moderne" };
static const char* const N_PL_EN[2]  = { "Game's place", "Top left" },           * const N_PL_FR[2]  = { "Place du jeu", "En haut \xC3\xA0 gauche" };
static const char* const N_GR_EN[2]  = { "Upward", "Downward" },                 * const N_GR_FR[2]  = { "Vers le haut", "Vers le bas" };
static const char* const N_LB_EN[2]  = { "Text only", "Icon + text" },           * const N_LB_FR[2]  = { "Texte seul", "Ic\xC3\xB4ne + texte" };
static const char* nw_plates(const UiConfig& c) { return c.plShow ? nullptr : tr("inactive : names in FFXI style", "inactif : noms en style FFXI"); }

static const OptDef NUI_DISPLAY[] = {
    { "Menu size", "Taille des menus", OK_PCT, OL_ADV, F_(muScale), 0.60f, 1.60f, nullptr, nullptr, 0, nullptr, 0 },
    { "Main menu position", "Position du menu principal", OK_SELECT, OL_ADV, F_(muMainPos), 0, 0, N_PL_EN, N_PL_FR, 2, nullptr, 0 },
    { "Taller submenu grows", "Sous-menu plus grand : grandit", OK_SELECT, OL_ADV, F_(muGrow), 0, 0, N_GR_EN, N_GR_FR, 2, nullptr, 0 },
    { "Menu rows", "Lignes des menus", OK_SELECT, OL_ADV, F_(muLabel), 0, 0, N_LB_EN, N_LB_FR, 2, nullptr, 0 },
};
static const OptDef NUI_PL_STYLE[] = {
    { "Style", "Style", OK_SELECT, OL_SIMPLE, F_(plShow), 0, 0, N_STY_EN, N_STY_FR, 2, nullptr, 0 },
};
static const OptDef NUI_PL_SIZE[] = {
    { "Text size", "Taille du texte", OK_PCT, OL_SIMPLE, F_(plSize), 0.6f, 1.6f, nullptr, nullptr, 0, nw_plates, 0 },
};
static const OptDef NUI_PL_ICONS[] = {
    { "Icon size (pearl...)", "Taille des ic\xC3\xB4nes (perle...)", OK_PCT, OL_ADV, F_(plIcon), 0.6f, 1.6f, nullptr, nullptr, 0, nw_plates, 0 },
    { "Master Level stars", "\xC3\x89toiles du Master Level", OK_PCT, OL_ADV, F_(plStar), 0.6f, 1.6f, nullptr, nullptr, 0, nw_plates, 0 },
};
static const OptDef NUI_PL_STARS[] = {
    { "Stars height", "Hauteur des \xC3\xA9toiles", OK_PCT, OL_ADV, F_(plStarY), 0.5f, 2.0f, nullptr, nullptr, 0, nw_plates, 0 },
    { "Stars spread", "\xC3\x89""cartement des \xC3\xA9toiles", OK_PCT, OL_ADV, F_(plStarX), 0.5f, 2.0f, nullptr, nullptr, 0, nw_plates, 0 },
};
#undef F_
#define NOPT(a) ((int)(sizeof(a) / sizeof((a)[0])))
static const UiConfig& nui_factory() { static const UiConfig F{}; return F; }
static int nui_bits_mod(int cur, int fac, int mask) { int d = (cur ^ fac) & mask, k = 0; while (d) { d &= d - 1; ++k; } return k; }

void ConfigPage::draw_nui_v2(u32 dev, Font* fo, const MouseState* mo, bool click,
                             float& ry, int& ri, float e,
                             float bandX, float bandW, float coX, float ctrlW,
                             float hdrX, float hdrW) {
    Kit K;
    if (!v2_kit(K, mo, click)) { draw_nui_config(dev, fo, mo, click, ry, ri, e, bandX, bandW, coX, ctrlW, hdrX, hdrW); return; }
    g_fade = e;
    UiConfig& c = ui_config();
    const UiConfig& F = nui_factory();
    const bool adv = c.cfgAdv != 0;
    const float x = coX, w = ctrlW;
    const int base = CTRL_ID;
    const char* STY[2] = { tr("FFXI", "FFXI"), tr("Modern", "Moderne") };
    const char* SHW[2] = { tr("Shown", "Affich\xC3\xA9""e"), tr("Hidden", "Cach\xC3\xA9""e") };
    // one row per nuHide bit : names[1] = the bit set (Modern / Hidden)
    auto bitRow = [&](int k, const char* label, int bit, const char* const* names) {
        const int v = (c.nuHide & bit) ? 1 : 0;
        if (cfg_select_row(K, ctrl_uid_i(base, k), x, ry, w, label, names[v], nullptr, nullptr, 0)) { c.nuHide = v ? (c.nuHide & ~bit) : (c.nuHide | bit); save_ui_config(); }
    };
    ry += K.px(6.0f);
    {   // THE WHOLE INTERFACE at once : Modern / FFXI (Mixed when the windows below differ)
        const int allBits = NU_ALL; const bool allM = (c.nuHide & allBits) == allBits && c.plShow, allF = (c.nuHide & allBits) == 0 && !c.plShow;
        const char* lab = allM ? STY[1] : allF ? STY[0] : tr("Mixed", "Mixte");
        if (cfg_select_row(K, ctrl_uid_i(base, 1), x, ry, w, tr("Game interface", "Interface du jeu"), lab, nullptr, nullptr, 0)) {
            const bool toModern = !allM; c.nuHide = toModern ? (c.nuHide | allBits) : (c.nuHide & ~allBits); c.plShow = toModern ? 1 : 0; save_ui_config(); }
        ry += K.px(8.0f);
    }
    if (adv) {
        if (v2_section(K, ctrl_uid_i(base, 2), 6, "Display", "Affichage", x, ry, w, cfg_count_modified(NUI_DISPLAY, NOPT(NUI_DISPLAY), true)))
            v2_rows(K, ctrl_uid_i(base, 3), NUI_DISPLAY, NOPT(NUI_DISPLAY), x, ry, w);
        BoxStyle& bs = c.muBox;
        const FrameRefs fr = { &bs.on, &bs.border, &bs.alpha, &bs.alphaOwn, &bs.themeCopy, &bs.theme, &bs.hue, &bs.lum };
        if (v2_section(K, ctrl_uid_i(base, 4), 20, "Frame", "Cadre", x, ry, w, cfg_frame_modified(fr))) { cfg_frame_rows(K, ctrl_uid_i(base, 5), x, ry, w, fr); v2_end(K, ry); }
    }
    if (v2_section(K, ctrl_uid_i(base, 6), 21, "Menus and screens", "Menus et \xC3\xA9""crans", x, ry, w, nui_bits_mod(c.nuHide, F.nuHide, NU_MAIN | NU_ACT | NU_HELP | NU_DIALOG))) {
        bitRow(7, tr("Main menu", "Menu principal"), NU_MAIN, STY);
        cfg_note(K, x, ry, tr("And every screen it opens : status, equipment, items, magic, map, Region Info, macros, missions, friend list...", "Et tous les \xC3\xA9""crans qu'il ouvre : statut, \xC3\xA9quipement, objets, magie, carte, Region Info, macros, missions, amis..."));
        bitRow(8, tr("Action menu", "Menu d'action"), NU_ACT, STY);
        cfg_note(K, x, ry, tr("Enter on a target : attack, magic, abilities, items, trade, check.", "Entr\xC3\xA9""e sur une cible : attaque, magie, aptitudes, objets, \xC3\xA9""change, examiner."));
        bitRow(9, tr("Top bar (menu name + help)", "Barre du haut (nom du menu + aide)"), NU_HELP, STY);
        bitRow(10, tr("Yes / No confirms", "Confirmations Oui / Non"), NU_DIALOG, STY);
        v2_end(K, ry);
    }
    if (v2_section(K, ctrl_uid_i(base, 11), 23, "NPCs and trade", "PNJ et \xC3\xA9""changes", x, ry, w, nui_bits_mod(c.nuHide, F.nuHide, NU_NPC | NU_TRADE))) {
        bitRow(12, tr("NPCs", "PNJ"), NU_NPC, STY);
        cfg_note(K, x, ry, tr("Their dialogue and answers, shops, the auction house, the delivery box, Config's lists.", "Leurs dialogues et r\xC3\xA9ponses, boutiques, h\xC3\xB4tel des ventes, bo\xC3\xAEte de livraison, listes de la Config."));
        bitRow(13, tr("Trade (player / NPC)", "\xC3\x89""change (joueur / PNJ)"), NU_TRADE, STY);
        v2_end(K, ry);
    }
    if (v2_section(K, ctrl_uid_i(base, 14), 24, "On screen", "\xC3\x80 l'\xC3\xA9""cran", x, ry, w, nui_bits_mod(c.nuHide, F.nuHide, NU_MACRO | NU_BUFF | NU_NET))) {
        bitRow(15, tr("Macro bar (Ctrl / Alt)", "Barre de macros (Ctrl / Alt)"), NU_MACRO, STY);
        bitRow(16, tr("Your buff icons (top)", "Tes ic\xC3\xB4nes de buff (en haut)"), NU_BUFF, STY);
        bitRow(17, tr("Connection box (top right)", "Box connexion (en haut \xC3\xA0 droite)"), NU_NET, STY);
        v2_end(K, ry);
    }
    {   // NAMES OVER THE CHARACTERS (the preview shows sample plates while this section is open)
        int nMod = cfg_count_modified(NUI_PL_STYLE, 1, adv) + cfg_count_modified(NUI_PL_SIZE, 1, adv) + cfg_count_modified(NUI_PL_ICONS, NOPT(NUI_PL_ICONS), adv)
                 + cfg_count_modified(NUI_PL_STARS, NOPT(NUI_PL_STARS), adv) + (c.plFont != F.plFont) + nui_bits_mod(c.plStyle, F.plStyle, 0x1000) + (((c.plStyle ^ F.plStyle) & 0x701) != 0);
        if (adv) nMod += (fabsf(c.plGap - F.plGap) > 0.0005f) + nui_bits_mod(c.plStyle, F.plStyle, 2 | 4);
        // Advanced : the rows below exist only for the Modern names -- on FFXI's they are not shown (Simple greys them)
        const bool plRows = !adv || c.plShow;
        if (!plRows) nMod = cfg_count_modified(NUI_PL_STYLE, 1, true);
        if (v2_section(K, ctrl_uid_i(base, 18), 26, "Names over the characters", "Noms au-dessus des persos", x, ry, w, nMod)) {
            const char* off = nw_plates(c);
            cfg_rows(K, ctrl_uid_i(base, 19), NUI_PL_STYLE, 1, x, ry, w, adv);
            if (plRows) {
            {   // the font : the plate-font list (read at run time)
                const int nf = plate_font_count() < 1 ? 1 : plate_font_count();
                int fc = c.plFont; if (fc < 0 || fc >= nf) fc = 0;
                if (int d = cfg_select_row(K, ctrl_uid_i(base, 20), x, ry, w, tr("Font", "Police"), plate_font_label(fc), off, &c.plFont, 4)) { c.plFont = (fc + d + nf) % nf; save_ui_config(); }
            }
            cfg_rows(K, ctrl_uid_i(base, 21), NUI_PL_SIZE, 1, x, ry, w, adv);
            {   // the names' size : with the distance (as the game) or FIXED, every one as thin as a far name
                const int v = (c.plStyle & 0x1000) ? 1 : 0;
                const char* ZL[2] = { tr("With the distance", "Selon la distance"), tr("Fixed (thin, as far)", "Fixe (fin, comme au loin)") };
                if (cfg_select_row(K, ctrl_uid_i(base, 22), x, ry, w, tr("Names size", "Taille des noms"), ZL[v], off, nullptr, 0)) { c.plStyle ^= 0x1000; save_ui_config(); }
            }
            if (adv) {   // icons, stars, places
                cfg_rows(K, ctrl_uid_i(base, 23), NUI_PL_ICONS, NOPT(NUI_PL_ICONS), x, ry, w, true);
                {   // the icons' gap before the name, in capital W (0 .. 3)
                    const float lo = 0.0f, hi = 3.0f;
                    float v01 = (c.plGap - lo) / (hi - lo); v01 = v01 < 0.0f ? 0.0f : (v01 > 1.0f ? 1.0f : v01);
                    char b[16]; _snprintf(b, sizeof(b), "%.1f W", c.plGap); b[15] = 0;
                    if (cfg_slider_row(K, ctrl_uid_i(base, 24), x, ry, w, tr("Icon gap before the name", "\xC3\x89""cart ic\xC3\xB4ne / nom"), &v01, b, off, &c.plGap, 4, true)) {
                        float v = lo + v01 * (hi - lo); v = (float)((int)(v / 0.05f + 0.5f)) * 0.05f; c.plGap = v < lo ? lo : (v > hi ? hi : v); }
                }
                cfg_rows(K, ctrl_uid_i(base, 25), NUI_PL_STARS, NOPT(NUI_PL_STARS), x, ry, w, true);
            }
            {   // the weight : ultra-thin (what a far name looks like) .. bold
                int wt = (c.plStyle >> 8) & 7; if (!wt && (c.plStyle & 1)) wt = 4; if (wt > 4) wt = 0;
                const char* WL[5] = { tr("Normal", "Normal"), tr("Ultra-thin", "Ultra-fin"), tr("Thin", "Fin"), tr("Semi-bold", "Semi-gras"), tr("Bold", "Gras") };
                static const int ORD[5] = { 1, 2, 0, 3, 4 };   // the arrows walk thin -> bold
                int at = 0; for (int i = 0; i < 5; ++i) if (ORD[i] == wt) at = i;
                if (int d = cfg_select_row(K, ctrl_uid_i(base, 26), x, ry, w, tr("Weight", "Graisse"), WL[wt], off, nullptr, 0)) {
                    at = (at + d + 5) % 5; c.plStyle = (c.plStyle & ~0x701 & ~1) | (ORD[at] << 8); save_ui_config(); }
            }
            if (adv) {   // italic / caps
                if (cfg_toggle_row(K, ctrl_uid_i(base, 27), x, ry, w, tr("Italic", "Italique"), (c.plStyle & 2) != 0, off, nullptr, 0)) { c.plStyle ^= 2; save_ui_config(); }
                if (cfg_toggle_row(K, ctrl_uid_i(base, 28), x, ry, w, tr("CAPS", "Majuscules"), (c.plStyle & 4) != 0, off, nullptr, 0)) { c.plStyle ^= 4; save_ui_config(); }
            }
            }   // plRows
            v2_end(K, ry);
        }
    }
    if (v2_section(K, ctrl_uid_i(base, 29), 25, "Game windows replaced by AioHUD", "Fen\xC3\xAAtres du jeu remplac\xC3\xA9""es par AioHUD", x, ry, w, nui_bits_mod(c.nuHide, F.nuHide, NU_CHAT))) {
        cfg_note(K, x, ry, tr("The party and alliance windows follow AioHUD's boxes : hidden while they show.", "Les fen\xC3\xAAtres de groupe et d'alliance suivent les bo\xC3\xAEtes AioHUD : masqu\xC3\xA9""es tant qu'elles s'affichent."));
        bitRow(32, tr("Chat log", "Journal du chat"), NU_CHAT, SHW);
        v2_end(K, ry);
    }
    if (adv) {   // BEHAVIOUR : move the game's menus
        const int nMod = (c.nuMenuDx != F.nuMenuDx) + (c.nuMenuDy != F.nuMenuDy);
        if (v2_section(K, ctrl_uid_i(base, 34), 22, "Behaviour", "Comportement", x, ry, w, nMod)) {
            // -1000 .. +1000 screen px, 10 px steps ; saved on release
            auto offRow = [&](int k, const char* label, int* f) {
                const float lo = -1000.0f, hi = 1000.0f;
                char b[16]; _snprintf(b, sizeof(b), "%+d px", *f); b[15] = 0;
                float v01 = ((float)*f - lo) / (hi - lo); v01 = v01 < 0.0f ? 0.0f : (v01 > 1.0f ? 1.0f : v01);
                if (cfg_slider_row(K, ctrl_uid_i(base, k), x, ry, w, label, &v01, b, nullptr, f, 4, false)) {
                    int v = (int)(lo + v01 * (hi - lo)); v = (v >= 0 ? (v + 5) : (v - 5)) / 10 * 10; *f = v; }
            };
            offRow(35, tr("Move the menus : horizontal", "D\xC3\xA9placer les menus : horizontal"), &c.nuMenuDx);
            offRow(36, tr("Move the menus : vertical", "D\xC3\xA9placer les menus : vertical"), &c.nuMenuDy);
            {   // back to the game's own place : a row with its button
                const float h = K.px(52.0f);
                if (ry + h >= K.clipTop && ry <= K.clipBot) {
                    kit_row(K, x, ry, w, h, 0);
                    kit_label(K, x + K.px(18.0f), ry + h * 0.5f, tr("Back to the game's place", "Retour \xC3\xA0 la place du jeu"), 0);
                    const float bw = K.px(170.0f);
                    if (kit_button(K, ctrl_uid_i(base, 37), x + w - K.px(18.0f) - bw, ry + (h - K.px(42.0f)) * 0.5f, bw, tr("Reset", "R\xC3\xA9initialiser"), 1, 0)) {
                        c.nuMenuDx = 0; c.nuMenuDy = 0; save_ui_config(); }
                }
                ry += h + K.px(4.0f);
            }
            cfg_note(K, x, ry, tr("Turning an option off or unloading AioHUD puts everything back.", "D\xC3\xA9sactiver une option ou d\xC3\xA9""charger AioHUD remet tout en place."));
            v2_end(K, ry);
        }
    } else v2_simple_note(K, x, ry);
    g_fade = e;
}
#undef NOPT

} // namespace aio
