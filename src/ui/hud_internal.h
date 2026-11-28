// hud_internal.h -- shared HUD-renderer helpers used by hud.cpp and its per-module split TUs
// (hud_skillchains.cpp / hud_treasure.cpp / hud_hatelist.cpp / hud_pointwatch.cpp / hud_grimoire.cpp /
// hud_zonetracker.cpp / hud_timers.cpp). Not public. The DEFINITIONS live in hud.cpp.
#pragma once
#include "ui/widget.h"     // Frame
#include "ui/edit_box.h"   // EditBox
#include "ui/box_grow.h"   // box_grow_x / box_grow_k : every box is placed by its grow direction
#include "gfx/d3d.h"       // u32
#include "gfx/draw.h"     // snap() : the ONE definition

namespace aio {


// Shared edit-mode drag for a module box : drag + snap-grid, write the new fractional origin back to
// (cfgX,cfgY), persist on drop. anchorX = which box edge the stored X pins : 0 = LEFT, 1 = CENTRE, 2 = RIGHT
// (right = the box grows LEFTWARD when its width changes, so its right edge stays put). px/py updated.
// Used by ALL module renderers ; the caller derives px from cfgX with the matching anchor.
void box_edit(const Frame& f, EditBox& eb, int editId, float& px, float& py, float boxW, float boxH,
              float& scale, float& cfgX, float& cfgY, int anchorX);   // scale BY REF : the wheel-resize writes it back

// Keep a box fully on screen (draw position only ; the stored position is untouched). Called by every module box
// OUTSIDE edit mode and preview, right after its position is computed -- in edit mode the drag handle must sit
// exactly where the stored position says.
void box_on_screen(const Frame& f, float& px, float& py, float boxW, float boxH);

// draw an atlas sub-cell [u0..u1]x[v0..v1] at (x,y,w,h) -- Sheol weapon strip (v 0..1) or the 2D buff atlas.
// Used by draw_zonetracker (weapon-type strip) and draw_timers (buff status atlas). Defaults on this DECLARATION only.
// The Chat module (hud_chat.cpp) : box 0 = chat, 1 = battle. `interactive` = tabs / pill take clicks (config closed).
// ovS > 0 = preview at (ovX, ovY) centre and that scale. chat_hit_clear() once per frame BEFORE the boxes draw, so a
// box that is not drawn this frame no longer swallows clicks.
void chat_box_draw(const Frame& f, int box, bool preview, bool interactive, float ovX, float ovY, float ovS);
bool chat_preview_size(int box, float& wU, float& hU);
void chat_stack_sample(const Frame& f, float cx, float cy, float s, bool force, bool draw, float* outW, float* outH);   // both boxes, stacked (hud_chat.cpp)
void nui_sample(const Frame& f, float cx, float cy, float s, bool draw, float* outW, float* outH);   // the redrawn action menu + submenu (hud_actmenu.cpp)   // a preview box's size / scale, as last drawn (hud_chat.cpp)
void actmenu_draw(const Frame& f);   // hud_actmenu.cpp : the target action menu, mirrored (Game UI -> NU_ACT)
void mainmenu_draw(const Frame& f);
void profile_draw(const Frame& f);
void persona_draw(const Frame& f);
void jobs_draw(const Frame& f);
void skills_draw(const Frame& f);
void crafts_draw(const Frame& f);
void unityinfo_draw(const Frame& f);    // hud_actmenu.cpp : Status -> Unity -> Unity Info, mirrored (NU_MAIN)
void unityrank_draw(const Frame& f);    // hud_actmenu.cpp : Status -> Unity -> Ranking Results, mirrored (NU_MAIN)
void unitylists_draw(const Frame& f);   // hud_actmenu.cpp : Status -> Unity -> Unity Lists (arealist, scresult, scoption), mirrored
void jobpoints_draw(const Frame& f);    // hud_actmenu.cpp : Status -> Job Points (merit2ca, levmeri2), mirrored (NU_MAIN)
void alterego_draw(const Frame& f);     // hud_actmenu.cpp : Status -> Alter Ego's points (fp_cat, fp_cat2, fp_info), mirrored
void celestite_draw(const Frame& f);    // hud_actmenu.cpp : Alter Ego -> Change Celestite (fe_equip, fe_inven), mirrored
void meritcat_draw(const Frame& f);     // hud_actmenu.cpp : Merit Points -> Categories (meritcat, meritinf), mirrored
void synth_draw(const Frame& f);        // hud_actmenu.cpp : the main menu's Synthesis (tskill1, cmbhlst), mirrored (NU_MAIN)
void items_draw(const Frame& f);        // hud_actmenu.cpp : the main menu's Items (inventor, from the main menu), mirrored (NU_MAIN)
void equipment_draw(const Frame& f);    // hud_actmenu.cpp : the main menu's Equipment (equip, inventor), mirrored (NU_MAIN)
void currencies_draw(const Frame& f);   // hud_actmenu.cpp : Status -> Currencies 1 / 2, searchable (NU_MAIN)
void actmenu_set_hand_tex(u32 tex);  // the party's selection-hand texture (its cursor)
// hud_chat.cpp : a LIVE chat box's rect this frame (false when hidden / not drawn) -- the redrawn menus sit above it
bool chat_box_rect(int box, float& x, float& y, float& w, float& h);
// the chat box AioHUD's menus open on (stacked : the higher ; side by side : the left) and the group's top / bottom
bool chat_anchor(float& ax, float& aw, float& top, float& bottom);
int  chat_input_decode(const char* raw, int rn, char* t, unsigned* col, int cap, int* caret);   // hud_chat.cpp : the input text (7F codes) -> printable + colours + caret
void chat_input_rect_set(float x, float y, float w, float h, float caretX);                      // hud_chat.cpp : anchor the auto-translate picker on another field

// theme_kit.cpp + frame_bake.cpp : the //aio kittest theme kits -- DEV ONLY (local, untracked files). build.bat compiles
// them and defines AIOHUD_THEMEKIT only when they are present ; every other build (CI, releases, package.bat) gets
// these no-ops, so the tracked code never depends on them.
#ifdef AIOHUD_THEMEKIT
void theme_kit_test_draw(const Frame& f);
void theme_kit_forget();      // on a device change
void theme_kit_dispose();     // at shutdown
bool theme_kit_bakes_slots(); // the iron frame is on -> it draws the registered slots (menu_cut) itself
#else
inline void theme_kit_test_draw(const Frame&) {}
inline void theme_kit_forget() {}
inline void theme_kit_dispose() {}
inline bool theme_kit_bakes_slots() { return false; }
#endif
// hud_actmenu.cpp -> theme_kit.cpp (the procedural iron frame) : the GLUED GROUP the box drawn at (x, y, w, h) belongs to,
// from LAST frame's placements. Rects relative to the group's top-left (its rest place) ; `self` = this box's own rect.
struct MenuGroup {
    int n, self; float r[16][4];              // the boxes
    int np; float p[8][5];                    // slot plates (x y w h radius)
    int nc; float c[64][5];                   // the slots cut in them (x y w h radius)
    int po[8], co[64];                        // the box (index in r) each plate / slot belongs to
    float W, H;                               // the group's size
    float ox, oy;                             // its top-left on screen (the rest place)
};
bool menu_group_lookup(float x, float y, float w, float h, MenuGroup& g);
void topbar_draw(const Frame& f);     // hud_actmenu.cpp : the client's top bar (menu name + help), AioHUD's style (NU_HELP)
void buffbar_draw(const Frame& f);    // hud_actmenu.cpp : your buff icons under the top bar, the party's icon set (NU_BUFF)
void scfx_gen_tick(u32 dev);       // hud_scfx.cpp : the spheres' generated textures, a few rows a frame
void gauges_draw(const Frame& f);    // hud_actmenu.cpp : the client's casttime / gaugewin, AioHUD's style
void rgbedit_draw(const Frame& f);   // hud_actmenu.cpp : Font Colors' RGB editor (textcol3)
void dialog_draw(const Frame& f);     // hud_actmenu.cpp : the client's Yes / No confirms, AioHUD's style (NU_DIALOG)
void trade_draw(const Frame& f);     // hud_actmenu.cpp : the player trade (trade + gift), AioHUD's style (NU_TRADE)
void shop_draw(const Frame& f);      // hud_actmenu.cpp : an NPC shop (Buy / Sell, the list, the quantity), AioHUD's style (NU_NPC)
void inspect_draw(const Frame& f);
void loot_draw(const Frame& f);
void mog_draw(const Frame& f);
void dlv_draw(const Frame& f);
void jobsel_draw(const Frame& f);
void gen_grid_draw(const Frame& f);
void gen_panel_draw(const Frame& f);
void missions_draw(const Frame& f);
void journal_draw(const Frame& f);
void mission_detail_draw(const Frame& f);
void key_items_draw(const Frame& f);       // Key Items (evitem) as data   // a mission's page (miss00 state 2)     // the journal (quests, Records of Eminence) as data    // Missions as data (the story)   // the generic PANELS (the text capture)  // hud_actmenu.cpp : the generic engine's grid (NU_NPC)
void macro_books_draw(const Frame& f);
void macro_edit_draw(const Frame& f);
void equip_sets_draw(const Frame& f);
void map_screen_draw(const Frame& f);   // hud_actmenu.cpp : the map (mapframe's whole-screen draw), under the HUD's widgets (NU_MAIN)
void map_screen_forget();
void map_screen_dispose();   // hud_actmenu.cpp : the map / icon atlases RELEASED (the unload)
bool map_screen_up();                  // ... shown this frame (the HUD's modules depop while it is)
void map_screen_hud(const Frame& f);
void plates_draw(const Frame& f);
void plates_sample(const Frame& f, float sx, float sy, float sw, float sh);   // the config's preview of the name plates
struct PlateFont { const char* face; int w; };
int plate_font_step(int cur, int d);   // the next INSTALLED plate font from cur (d = +1 / -1)
int plate_font_count();                // the name plates' fonts (Game UI -> Name plates font)
const char* plate_font_label(int i);      // hud_actmenu.cpp : the name plates over the characters (under the widgets)
void map_list_draw(const Frame& f);
void friends_draw(const Frame& f);
void olstat_draw(const Frame& f);      // ... its Online Status (olstat) : 3 icon buttons in a row     // hud_actmenu.cpp : Communication -> Friend List (friend)     // ... its Markers list (mapv3)    // ... its cartouche (the zone, the floor, your square), in the screens' pass               // ... its texture, on a device change
void cfg_filter_draw(const Frame& f);    // hud_actmenu.cpp : Config's chat / battle-effect filters (cfilter / fxfilter, NU_NPC)    // hud_actmenu.cpp : MacroPalette's equipment set list (mcres20, NU_MAIN)    // hud_actmenu.cpp : MacroPalette's editor (palette + one macro, NU_MAIN)   // hud_actmenu.cpp : MacroPalette's book list (mcrselec, NU_MAIN)
void gen_list_draw(const Frame& f);  // hud_actmenu.cpp : the generic engine's list (NU_NPC)
void gen_draw(const Frame& f);       // hud_actmenu.cpp : the generic engine's menus (NU_NPC)
void auc_draw(const Frame& f);       // hud_actmenu.cpp : the auction house (NU_NPC)
void room_draw(const Frame& f);      // hud_actmenu.cpp : Remodel's styles (roomlist) (NU_NPC)    // hud_actmenu.cpp : Change Jobs' grid of the 22 jobs (NU_NPC)       // hud_actmenu.cpp : the delivery box (receive / send, the inventory beside) (NU_NPC)       // hud_actmenu.cpp : the Mog House's menus (moogle, bags, Switch, post) (NU_NPC)      // hud_actmenu.cpp : Treasure (the pool list, Lot / Pass, Pass all) (NU_ACT)   // hud_actmenu.cpp : a player's Check (the equipment grid, View wares, the comment) (NU_ACT)
void netbox_draw(const Frame& f);    // hud_actmenu.cpp : the connection box (netstat), top right beside the top bar (NU_NET)
void macrobar_draw(const Frame& f);  // hud_actmenu.cpp : the Ctrl / Alt macro bar, when the client shows its own (NU_MACRO)
void item_card_fold(const Frame& f);  // hud_actmenu.cpp : once a frame, after every screen : the item card that was shown folds away
void comment_draw(const Frame& f);    // hud_actmenu.cpp : Search -> Edit your comment (the client's `comment`), mirrored (NU_MAIN)
void region_draw(const Frame& f);     // hud_actmenu.cpp : the main menu's Region Info (over the client's map), mirrored (NU_MAIN)
void linkshell_draw(const Frame& f);  // hud_actmenu.cpp : the main menu's Linkshell (link5, the items), mirrored (NU_MAIN)
bool chat_input_rect(float& x, float& y, float& w, float& h, float& caretX, float& keptX);   // keptX : the caret as it was before the auto-translate picker opened   // hud_chat.cpp : AioHUD's input field (screen px), drawn < 200 ms ago
void fep_draw(const Frame& f);   // hud_actmenu.cpp : the client's auto-translate picker (Tab), drawn on that field
// a bw x bh block on it : above the group when it fits, else below ; inside the screen. False : no chat box shown.
bool place_on_chat(float bw, float bh, float gap, float screenW, float screenH, float& bx, float& by);
bool covers_chat(float bx, float by, float bw, float bh);   // hud_chat.cpp : the block overlaps the chat group
void npc_draw(const Frame& f);   // hud_actmenu.cpp : the NPC dialogue + answers, mirrored (Game UI -> NU_NPC)
void actmenu_hits_clear();       // hud_actmenu.cpp : every redrawn menu's click area off (each frame, before the draws)
void chat_hit_clear();

void draw_icon_cell(u32 dev, u32 tex, float x, float y, float w, float h, float u0, float u1, float v0 = 0.0f, float v1 = 1.0f);

}
