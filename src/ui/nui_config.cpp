// nui_config.cpp -- the "Game UI" settings panel (//aio config -> "Game UI") : hide the client's own windows
// (party, alliance, chat, help bar, MP-cost case) and move its menus. The engine is model/native_ui.cpp ; this page
// only writes UiConfig::nuHide / nuMenuDx / nuMenuDy. Each control is keyed by CTRL_ID (file:line hash, see
// config_controls.h). Reuses the shared sub-section catOpen_ slots [6] and [7].
#include "ui/config_page.h"
#include "ui/config_controls.h"    // cat_header / row_toggle / row_slider / push_btn + palette
#include "ui/config_rows.h"        // ROW_BAND / ROW_NEXT
#include "model/ui_config.h"
#include "model/native_ui.h"       // NativeUiBits
#include "gfx/font.h"
#include "gfx/draw.h"
#include <cstdio>

namespace aio {

void ConfigPage::draw_nui_config(u32 dev, Font* fo, const MouseState* mo, bool click,
                                 float& ry, int& ri, float e,
                                 float bandX, float bandW, float coX, float ctrlW,
                                 float hdrX, float hdrW) {
    UiConfig& c = ui_config();

    // one muted explanatory line inside a row band
    #define NUI_NOTE(TEXT) { ROW_BAND(34.0f) fo->begin(dev); \
        fo->draw_lc(dev, coX + snap(4.0f), ry + yo + snap(14.0f), (TEXT), snap(12.0f), fa(C_MUTE), fa(C_STROKE), 1.0f); } ROW_NEXT(34.0f)

    // ===== sub-section : HIDE (the client's windows AioHUD replaces) =====
    const float aF6_ = cat_fold(CTRL_ID, catOpen_[6]);
    cat_panel(dev, hdrX, ry, hdrW, cat_card_h(catH_[6], aF6_));
    if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Hide the game's windows", "Cacher les fen\xC3\xAAtres du jeu"), catOpen_[6], aF6_)) catOpen_[6] = !catOpen_[6];
    ROW_NEXT(42.0f)
    if (aF6_ > 0.0f) {
        const float top6_ = ry;
        cat_fold_clip(dev, hdrX, top6_, hdrW, catH_[6] * aF6_);
        // one row per window ; the chip toggles that bit of nuHide (row_toggle already saved the OLD mask -> save again)
        #define NUI_HIDE_ROW(UID, LABEL, BIT) { ROW_BAND(48.0f) int on_ = (c.nuHide & (BIT)) ? 1 : 0; \
            if (row_toggle(dev, fo, mo, click, (UID), coX, ry + yo, ctrlW, (LABEL), &on_)) { c.nuHide = on_ ? (c.nuHide | (BIT)) : (c.nuHide & ~(BIT)); save_ui_config(); } \
            } ROW_NEXT(48.0f)
        NUI_HIDE_ROW(CTRL_ID, tr("Party window", "Fen\xC3\xAAtre du groupe"), NU_PARTY)
        NUI_NOTE(tr("Also hides the game's target bar, which sits on it.", "Cache aussi la barre de cible du jeu, pos\xC3\xA9""e dessus."))
        NUI_HIDE_ROW(CTRL_ID, tr("Alliance windows", "Fen\xC3\xAAtres d'alliance"), NU_ALLY)
        NUI_HIDE_ROW(CTRL_ID, tr("Chat log", "Journal du chat"), NU_CHAT)
        NUI_NOTE(tr("NPC dialogue is written there too -- keep it while AioHUD has no chat box.", "Les dialogues des PNJ y sont \xC3\xA9""crits -- garde-le tant qu'AioHUD n'a pas de chat."))
        NUI_HIDE_ROW(CTRL_ID, tr("Help bar + menu name", "Barre d'aide + nom du menu"), NU_HELP)
        NUI_HIDE_ROW(CTRL_ID, tr("MP cost / recast case", "Case co\xC3\xBBt MP / recast"), NU_COST)
        NUI_HIDE_ROW(CTRL_ID, tr("Connection + POL icons", "Ic\xC3\xB4nes connexion + POL"), NU_NET)
        NUI_NOTE(tr("A hidden window keeps working ; it is only parked off-screen.", "Une fen\xC3\xAAtre cach\xC3\xA9""e fonctionne toujours ; elle est juste hors de l'\xC3\xA9""cran."))
        #undef NUI_HIDE_ROW
        cat_fold_end(dev, ry, top6_, catH_[6], aF6_);
    }   // end Hide
    ry += snap(16.0f);

    // ===== sub-section : MENUS (main menu, sub-menus, lists, panels) =====
    const float aF7_ = cat_fold(CTRL_ID, catOpen_[7]);
    cat_panel(dev, hdrX, ry, hdrW, cat_card_h(catH_[7], aF7_));
    if (cat_header(dev, fo, mo, click, CTRL_ID, hdrX, ry, hdrW, tr("Move the game's menus", "D\xC3\xA9placer les menus du jeu"), catOpen_[7], aF7_)) catOpen_[7] = !catOpen_[7];
    ROW_NEXT(42.0f)
    if (aF7_ > 0.0f) {
        const float top7_ = ry;
        cat_fold_clip(dev, hdrX, top7_, hdrW, catH_[7] * aF7_);
        // -1000 .. +1000 screen px, 10 px steps ; persists on release (row_slider)
        #define NUI_OFFSET_ROW(UID, LABEL, FIELD) { ROW_BAND(46.0f) const float lo_ = -1000.0f, hi_ = 1000.0f; char b_[16]; \
            _snprintf(b_, sizeof(b_), "%+d px", (FIELD)); b_[sizeof(b_) - 1] = 0; \
            float v01_ = clampf(((float)(FIELD) - lo_) / (hi_ - lo_), 0.0f, 1.0f); \
            if (row_slider(dev, fo, mo, (UID), coX, ry + yo, ctrlW, (LABEL), b_, &v01_)) { \
                int v_ = (int)(lo_ + v01_ * (hi_ - lo_)); v_ = (v_ >= 0 ? (v_ + 5) : (v_ - 5)) / 10 * 10; (FIELD) = v_; } \
            } ROW_NEXT(46.0f)
        NUI_OFFSET_ROW(CTRL_ID, tr("Horizontal", "Horizontal"), c.nuMenuDx)
        NUI_OFFSET_ROW(CTRL_ID, tr("Vertical", "Vertical"), c.nuMenuDy)
        #undef NUI_OFFSET_ROW
        { ROW_BAND(48.0f)   // back to the game's own place
            const float bw = snap(150.0f);
            if (push_btn(dev, fo, mo, click, CTRL_ID, coX + ctrlW - bw, ry + yo, bw, snap(34.0f), tr("Reset", "R\xC3\xA9initialiser"), 0)) {
                c.nuMenuDx = 0; c.nuMenuDy = 0; save_ui_config();
            }
            fo->begin(dev);
            fo->draw_lc(dev, coX + snap(4.0f), ry + yo + snap(17.0f), tr("Back to the game's place", "Retour \xC3\xA0 la place du jeu"), snap(14.0f), fa(C_TEXT), fa(C_STROKE), 1.0f);
        } ROW_NEXT(48.0f)
        NUI_NOTE(tr("Moves every menu at once, the ones you never opened too.", "D\xC3\xA9place tous les menus d'un coup, m\xC3\xAAme ceux jamais ouverts."))
        NUI_NOTE(tr("Turning an option off or unloading AioHUD puts everything back.", "D\xC3\xA9sactiver une option ou d\xC3\xA9""charger AioHUD remet tout en place."))
        cat_fold_end(dev, ry, top7_, catH_[7], aF7_);
    }   // end Menus

    ry += snap(16.0f);
    #undef NUI_NOTE
    #undef ROW_BAND
    #undef ROW_NEXT
}

} // namespace aio
