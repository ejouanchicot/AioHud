// hud_actmenu.cpp -- the target ACTION menu (Enter on a target), drawn by AioHUD in place of the client's "playermo".
//
// A MIRROR, not a replacement : the client keeps its own menu (parked off-screen by native_ui, Game UI -> NU_ACT),
// its keyboard / gamepad input and every action it runs ; this only draws what that menu shows -- its rows (the
// command codes it built for this target), its cursor, which rows open a submenu -- where the client would have put
// it. Data : native_action_menu() (model/native_ui.h), read once a frame. Unknown codes are shown by number, never
// guessed (the code table is in native_ui.h).
//
// Look (2026-11-16, v3 after the user's "un block avec des capsules dedans, texte plus petit, la main d'AioHUD") :
// the menus' theme box (Game UI -> Menus : UiConfig::muBox, Same-as-Party by default) holding one thin glass capsule per row, each with its command's vector icon ; the
// picked row carries the PARTY's own gold glass selection (clipped to the capsule, with its moving sweep) gliding
// from row to row, and AioHUD's selection HAND bobs in the box's left gutter on the party's rhythm ; a submenu's row
// carries a small triangle. A submenu open on top dims the whole menu. Its SUBMENUS (Magic categories, Abilities) are
// drawn the same way beside the picked row (native_sub_menu) ; every open is smooth but near-instant (~70-80 ms).
#include "ui/hud.h"
#include "ui/hud_internal.h"
#include "ui/config_controls.h"   // tr()
#include "ui/box_style.h"         // draw_themed_box : the block
#include "ui/party.h"             // party_cursor / party_selframe / party_cursor_bob : AioHUD's hand and gold glass
#include "model/ui_config.h"
#include "model/map_dat.h"     // menu_label : the menus' image labels
#include "model/native_ui.h"
#include "model/gamestate.h"   // GameState (me.mp)
#include "ui/buff_atlas.h"        // buff_atlas_tex / buff_cell_uv : the status-icon fallback
#include "model/buff_groups.h"     // buff_status_name_real : the buff icons' tip
#include "ui/tex_retry.h"         // ensure_raw_tex : the element strip / job emblems (bounded retry)
#include <ctime>                   // time : Treasure's time left (the pool's unix expiries)
#include "model/skillchain.h"     // sc_skill_lookup / sc_info : a weapon skill's property -> its element
#include "model/job_track_gen.h"  // job_track : which job a job ability belongs to
#include "model/paths.h"       // plugin_dir : Windower's own plugins\icons
#include "model/spells_gen.h"  // spell_info : the spell list's names and MP
#include "model/spell_cost.h"  // spell_cost_kind : Full Cure "All", Embrava "20%" 
#include "model/abilities_gen.h"        // abil_info : the ability list's job abilities
#include "model/itemnames_gen.h"        // item_display_name : the Items list
#include "model/mounts_gen.h"           // mount_name : the mount list
#include "model/zones.h"                // zone_name : the Profile's Home Point
#include "model/chat_log.h"             // chat_decode : the search comment lines (auto-translate)
#include "model/game_mem.h"             // PersonaMem : the stat panels' data
#include "model/party_state.h"          // party().encumbrance() : the Equipment grid's locked slots
#include "model/currencies_gen.h"       // CURRENCY1_EN / CURRENCY2_EN : the client's row order and labels
#include "model/weapon_skills_gen.h"    // ws_info : its weapon skills
#include "model/item_text.h"       // the NPC answers' item card : text + flags from the item's DAT record
#include "gfx/draw.h"
#include "gfx/font.h"
#include "gfx/texture.h"           // read_item_record_head / decode_gear_icon_from_rom : the item card
#include "gfx/d3d.h"               // dTexQuadState : its icon
#include "windower_debug.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <windows.h>

namespace aio {

unsigned job_role_color(int id);   // party_state.cpp : the role tint of a job (the JA emblems)
const char* job_abbr(int id);       // party_state_roster.cpp : job id -> "WAR".."RUN" (the search results)

namespace {

enum Icon { IC_NONE, IC_SWORD, IC_SHEATHE, IC_BUBBLE, IC_BOLT, IC_STAR, IC_BAG, IC_LENS, IC_RETICLE, IC_SWAP, IC_DUO };
struct Cmd { unsigned short code; const char* en; const char* fr; bool sub; Icon icon; };
// proven 2026-11-16 (captures + the user's screenshots of the three menus) ; `sub` = the client draws an arrow
const Cmd CMDS[] = {
    {  1, "Attack",        "Attaquer",              false, IC_SWORD   },
    {  3, "Disengage",     "D\xC3\xA9sengager",     true,  IC_SHEATHE },
    {  4, "Chat",          "Discuter",              true,  IC_BUBBLE  },   // the menu opened on NOTHING (a click in the void) : its Chat (2026-11-17, "#4")
    {  5, "Chat",          "Discuter",              true,  IC_BUBBLE  },
    {  7, "Abilities",     "Aptitudes",             false, IC_BOLT    },
    {  8, "Magic",         "Magie",                 true,  IC_STAR    },
    {  9, "Items",         "Objets",                false, IC_BAG     },
    { 10, "Check",         "Examiner",              false, IC_LENS    },
    { 11, "Check",         "Examiner",              false, IC_LENS    },
    { 12, "Treasure",      "Treasure",              false, IC_BAG     },   // the game's own word, in FR too (user, 2026-11-16)   // the treasure POOL : only while items are in it (2026-11-16)
    { 18, "Switch Target", "Changer de cible",      false, IC_RETICLE },
    { 19, "Trade",         "\xC3\x89""changer",     false, IC_SWAP    },
    { 25, "Trust",         "Alter Ego",             false, IC_DUO     },
    // the other fourteen, named 2026-11-24 from the client's OWN label sprites (code table FFXiMain RVA 0x370278 :
    // +0 sprite, +2 sheet 0 keytops3 / 1 windowps -- calibrated on Attack / Abilities / Trust / Trade) ; `sub` = the
    // sprite carries the arrow (Call for Help does, like Disengage). They used to show as "#13", "#16"... Mounted,
    // fellow and event menus are THIS menu with these codes (chocopc / fellow windows are never opened).
    {  2, "Call for Help", "Appel \xC3\xA0 l'aide",  true,  IC_BUBBLE  },
    {  6, "Talk",          "Parler",                false, IC_BUBBLE  },   // your adventuring fellow
    { 13, "Fish",          "P\xC3\xAA""cher",       false, IC_NONE    },
    { 14, "Dig",           "Creuser",               false, IC_LENS    },   // chocobo digging
    { 15, "Dismount",      "Descendre",             false, IC_NONE    },
    { 16, "Invite",        "Inviter",               false, IC_DUO     },
    { 17, "Disband",       "Cong\xC3\xA9""dier",    false, IC_DUO     },   // your fellow
    { 20, "Trade",         "\xC3\x89""changer",     false, IC_SWAP    },   // with your fellow
    { 21, "Attack",        "Attaquer",              false, IC_SWORD   },   // dismount and attack
    { 22, "Quarry",        "Fouiller",              false, IC_LENS    },   // event : search the ground for a Petra
    { 23, "Scout",         "Rep\xC3\xA9rer",        false, IC_LENS    },   // event : where is the nearest Rook
    { 24, "Sprint",        "Sprint",                false, IC_NONE    },   // event : a short speed-up
    { 26, "Release",       "Lib\xC3\xA9rer",        false, IC_DUO     },   // an alter ego
    { 27, "Request",       "Demander",              false, IC_DUO     },   // to join another's party
};
const Cmd* cmd_of(unsigned short code) {
    for (int i = 0; i < (int)(sizeof(CMDS) / sizeof(CMDS[0])); ++i) if (CMDS[i].code == code) return &CMDS[i];
    return 0;
}

inline u32 with_alpha(u32 c, float a) {
    if (a >= 1.0f) return c; if (a <= 0.0f) return c & 0x00FFFFFFu;
    return (c & 0x00FFFFFFu) | ((u32)((float)(c >> 24) * a + 0.5f) << 24);
}
inline u32 mix(u32 a, u32 b, float t) {   // every channel, alpha included
    if (t <= 0.0f) return a; if (t >= 1.0f) return b;
    u32 r = 0;
    for (int sh = 0; sh < 32; sh += 8) {
        const int x = (a >> sh) & 255, y = (b >> sh) & 255;
        r |= (u32)(x + (int)((float)(y - x) * t + 0.5f)) << sh;
    }
    return r;
}
// an anti-aliased triangle (fill_poly_aa wants convex shapes : every icon below is built from convex pieces)
inline void tri(u32 dev, float x0, float y0, float x1, float y1, float x2, float y2, u32 c) {
    const float xy[6] = { x0, y0, x1, y1, x2, y2 }; fill_poly_aa(dev, xy, 3, c);
}
inline float ease_out(float t) { if (t <= 0.0f) return 0.0f; if (t >= 1.0f) return 1.0f; const float u = 1.0f - t; return 1.0f - u * u * u; }


// ---- the command icons : a few strokes in a z-sized square centred on (cx, cy) ----
void draw_icon(u32 dev, Icon ic, float cx, float cy, float z, float S, u32 c) {
    const float h = z * 0.5f, t = 1.5f * S;
    switch (ic) {
    case IC_SWORD:                                     // a blade on the diagonal, its guard and pommel
        seg_soft(dev, cx - h * 0.75f, cy + h * 0.75f, cx + h * 0.85f, cy - h * 0.85f, t, c);
        seg_soft(dev, cx - h * 0.70f, cy + h * 0.05f, cx - h * 0.05f, cy + h * 0.70f, t, c);
        disc(dev, cx - h * 0.85f, cy + h * 0.85f, 1.6f * S, c);
        break;
    case IC_SHEATHE:                                   // the blade going back into its scabbard
        seg_soft(dev, cx - h * 0.80f, cy + h * 0.80f, cx + h * 0.30f, cy - h * 0.30f, t, c);
        rrect_stroke(dev, cx - h * 0.05f, cy - h * 0.95f, h * 0.95f, h * 0.95f, h * 0.30f, c, 1.2f * S);
        break;
    case IC_BUBBLE: {                                  // a speech bubble and its tail
        const float bw = z * 0.92f, bh = z * 0.64f;
        rrect_stroke(dev, cx - bw * 0.5f, cy - h * 0.85f, bw, bh, bh * 0.42f, c, 1.3f * S);
        tri(dev, cx - h * 0.45f, cy - h * 0.85f + bh - 0.5f, cx - h * 0.05f, cy - h * 0.85f + bh - 0.5f, cx - h * 0.60f, cy + h * 0.80f, c);
        break; }
    case IC_BOLT:                                      // a lightning bolt : two triangles meeting in the middle
        tri(dev, cx + h * 0.30f, cy - h, cx - h * 0.60f, cy + h * 0.18f, cx + h * 0.18f, cy + h * 0.18f, c);
        tri(dev, cx - h * 0.18f, cy - h * 0.18f, cx + h * 0.60f, cy - h * 0.18f, cx - h * 0.30f, cy + h, c);
        break;
    case IC_STAR: {                                    // a four-point spark (two thin diamonds) and a small one
        const float o = h * 0.95f, i = h * 0.24f;
        const float v[8] = { cx, cy - o, cx + i, cy, cx, cy + o, cx - i, cy };
        const float w[8] = { cx - o, cy, cx, cy - i, cx + o, cy, cx, cy + i };
        fill_poly_aa(dev, v, 4, c); fill_poly_aa(dev, w, 4, c);
        disc(dev, cx + h * 0.80f, cy - h * 0.80f, 1.3f * S, c);
        break; }
    case IC_BAG:                                       // a pouch : body, neck tie, flap
        rrect(dev, cx - h * 0.78f, cy - h * 0.30f, h * 1.56f, h * 1.25f, h * 0.45f, c, c);
        rrect(dev, cx - h * 0.42f, cy - h * 0.82f, h * 0.84f, h * 0.42f, h * 0.18f, c, c);
        break;
    case IC_LENS:                                      // a magnifier
        rrect_stroke(dev, cx - h * 0.85f, cy - h * 0.85f, h * 1.25f, h * 1.25f, h * 0.625f, c, 1.4f * S);
        seg_soft(dev, cx + h * 0.30f, cy + h * 0.30f, cx + h * 0.90f, cy + h * 0.90f, 1.9f * S, c);
        break;
    case IC_RETICLE:                                   // a target reticle
        rrect_stroke(dev, cx - h * 0.62f, cy - h * 0.62f, h * 1.24f, h * 1.24f, h * 0.62f, c, 1.3f * S);
        seg_soft(dev, cx, cy - h, cx, cy - h * 0.40f, t, c); seg_soft(dev, cx, cy + h * 0.40f, cx, cy + h, t, c);
        seg_soft(dev, cx - h, cy, cx - h * 0.40f, cy, t, c); seg_soft(dev, cx + h * 0.40f, cy, cx + h, cy, t, c);
        disc(dev, cx, cy, 1.4f * S, c);
        break;
    case IC_SWAP:                                      // two arrows, opposite ways
        seg_soft(dev, cx - h * 0.85f, cy - h * 0.40f, cx + h * 0.55f, cy - h * 0.40f, t, c);
        tri(dev, cx + h * 0.95f, cy - h * 0.40f, cx + h * 0.40f, cy - h * 0.80f, cx + h * 0.40f, cy, c);
        seg_soft(dev, cx + h * 0.85f, cy + h * 0.45f, cx - h * 0.55f, cy + h * 0.45f, t, c);
        tri(dev, cx - h * 0.95f, cy + h * 0.45f, cx - h * 0.40f, cy + h * 0.05f, cx - h * 0.40f, cy + h * 0.85f, c);
        break;
    case IC_DUO:                                       // two companions
        disc(dev, cx - h * 0.38f, cy - h * 0.38f, h * 0.30f, c);
        rrect_topcaps(dev, cx - h * 0.92f, cy + h * 0.05f, h * 1.08f, h * 0.85f, h * 0.50f, c, c);
        disc(dev, cx + h * 0.45f, cy - h * 0.52f, h * 0.26f, with_alpha(c, 0.75f));
        rrect_topcaps(dev, cx + h * 0.02f, cy - h * 0.15f, h * 0.90f, h * 0.75f, h * 0.42f, with_alpha(c, 0.75f), with_alpha(c, 0.75f));
        break;
    default:
        disc(dev, cx, cy, h * 0.30f, c);
        break;
    }
}

u32 g_handTex = 0;   // the party's selection-hand texture, handed in by the HUD each frame (device-loss managed there)
float g_subXY[2] = { -1.0f, -1.0f };
float g_mbkAt[3] = { 0.0f, 0.0f, 0.0f };
float g_esAt[3] = { 0.0f, 0.0f, 0.0f };    // the equipment set list : left edge, the cursor row's top, 1 = drawn (mcresmn hangs there)
float g_medAt[2][3] = { { 0, 0, 0 }, { 0, 0, 0 } };   // the macro palette / the macro : right edge, top, 1 = drawn (their option menus hang there)   // the macro book list : its left edge, the cursor row's top, 1 = drawn this frame (mcrselop hangs there)     // where the main menu's submenu was last drawn (a player's options take its place)
const int HIT_N = 21;   // ... 20 the macro book list
float g_hit[HIT_N][4] = { { 0, 0, 0, 0 } };   // 13 the trade, 14 a confirm, 15 the buff bar, 16 a shop's Buy / Sell, 17 its list
struct CoinQ { float x, y, z, a; }; CoinQ g_coinQ[16]; int g_coinN = 0;   // a list's gil coins, drawn after its text pass  // the drawn blocks (menu, submenu ; NPC bubble, NPC answers ; the auto-translate picker ; the main menu, its categories, its list ; Unity Info) as screen FRACTIONS : their clicks never reach the game
// grow block i's (already set) hit rect down to `bottom` (screen px) -- a box drawn under it after the rect was set
// (a long description) ; never shrinks it
static void hit_extend(int i, float bottom, float screenH) {
    if (g_hit[i][2] <= 0.0f || screenH <= 0.0f) return;
    const float h = bottom / screenH - g_hit[i][1];
    if (h > g_hit[i][3]) g_hit[i][3] = h;
}

// ---- the action menu's submenus (native_sub_menu) : labels by ENTRY index (1-based), from their help texts ----
struct SubRow { const char* en; const char* fr; bool sub; Icon icon; };   // sub : none -- each row opens its LIST, no arrow in the client
const SubRow MAGSELEC[] = {
    { "White Magic", "Magie blanche",            false, IC_STAR },
    { "Black Magic", "Magie noire",              false, IC_STAR },
    { "Songs",       "Chants",                   false, IC_STAR },
    { "Ninjutsu",    "Ninjutsu",                 false, IC_STAR },
    { "Summoning",   "Invocation",               false, IC_STAR },
    { "Blue Magic",  "Magie bleue",              false, IC_STAR },
    { "Geomancy",    "G\xC3\xA9omancie",         false, IC_STAR },
};
const SubRow ABISELEC[] = {
    { "Job Abilities", "Aptitudes de job",       false, IC_BOLT    },
    { "Weapon Skills", "Weapon Skills",          false, IC_SWORD   },
    { "Ranged Attack", "Attaque \xC3\xA0 distance", false, IC_RETICLE },
    { "Pet Commands",  "Ordres au familier",     false, IC_DUO     },
    { "Mount",         "Monture",                false, IC_NONE    },
};
// Chat : no help texts in the client -- named from the user's reading of the client's own menu (2026-11-16)
const SubRow CHATCTRL[] = {
    { "Say",         "Dire",              false, IC_BUBBLE },
    { "Tell",        "Message priv\xC3\xA9", false, IC_BUBBLE },
    { "Party",       "Groupe",            false, IC_BUBBLE },
    { "Linkshell",   "Linkshell",         false, IC_BUBBLE },
    { "Linkshell 2", "Linkshell 2",       false, IC_BUBBLE },
    { "Unity",       "Unity",             false, IC_BUBBLE },
    { "Shout",       "Crier",             false, IC_BUBBLE },
};
// the main menu's Magic (mgcmenu) : the same seven categories in the same order, then Trust (its definition's help texts,
// docs/notes/native-menu-items-2026-11-15.txt ; its labels are images, no text -- "#1 #2" until 2026-11-18)
const SubRow MGC_TRUST = { "Trust", "Alter Ego", false, IC_DUO };
// the main menu's Status (statcom2) : 13 entries, labels are images -- named from their help texts (the definition,
// docs/notes/native-menu-items-2026-11-15.txt), by ENTRY index (the screen order is the client's, by y)
const SubRow STATCOM2[13] = {
    { "Profile",        "Profil",                         false, IC_DUO     },
    { "Job Levels",     "Niveaux de job",                 false, IC_STAR    },
    { "Combat Skills",  "Comp\xC3\xA9tences de combat", false, IC_SWORD   },
    { "Magic Skills",   "Comp\xC3\xA9tences de magie",  false, IC_STAR    },
    { "Crafts",         "Artisanat",                      false, IC_NONE    },
    { "Currencies",     "Monnaies",                       false, IC_BAG     },
    { "Merit Points",   "Points de m\xC3\xA9rite",      false, IC_STAR    },
    { "Job Points",     "Job Points",                     false, IC_STAR    },
    { "Play Time",      "Temps de jeu",                   false, IC_NONE    },
    { "Currencies 2",   "Monnaies 2",                     false, IC_BAG     },
    { "Unity",          "Unity",                          false, IC_DUO     },
    { "Master Levels",  "Master Levels",                  false, IC_STAR    },
    { "Alter Ego",      "Alter Ego",                      false, IC_DUO     },
};
// Status -> Unity (ut_menu) : 3 entries, labels are images -- the user's reading of them, 2026-11-18
const SubRow UT_MENU[3] = {
    { "Unity Info",     "Infos Unity",                    false, IC_DUO     },
    { "Ranking Info",   "Classement",                     false, IC_STAR    },
    { "Unity Lists",    "Listes Unity",                   false, IC_DUO     },
};
// a player's options from the search results (scoption) : 5 entries, labels are images -- named from their help texts
// (docs/notes/unity-lists-search-re-2026-11-18.md 3d)
const SubRow SCOPTION[5] = {
    { "Tell",                   "Tell",                           false, IC_BUBBLE  },
    { "PlayOnline Message",     "Message PlayOnline",             false, IC_BUBBLE  },
    { "Invite to Party",        "Inviter dans le groupe",         false, IC_DUO     },
    { "Express Desire to Join", "Demander \xC3\xA0 rejoindre",    false, IC_DUO     },
    { "Cancel Request",         "Annuler la demande",             false, IC_NONE    },
};
// Status -> Alter Ego (fe_main) : 7 entries, labels are images -- named from their help texts (status-panels-b C1)
const SubRow FE_MAIN[7] = {
    { "Alter Ego Points",  "Points Alter Ego",      false, IC_STAR    },
    { "Tank Celestite",    "C\xC3\xA9lestite tank",       false, IC_NONE    },
    { "Melee Celestite",   "C\xC3\xA9lestite m\xC3\xAAl\xC3\xA9""e",  false, IC_NONE    },
    { "Ranged Celestite",  "C\xC3\xA9lestite distance",   false, IC_NONE    },
    { "Magic Celestite",   "C\xC3\xA9lestite magie",      false, IC_NONE    },
    { "Healer Celestite",  "C\xC3\xA9lestite soin",       false, IC_NONE    },
    { "Support Celestite", "C\xC3\xA9lestite soutien",    false, IC_NONE    },
};
// Status -> Merit Points : merit1 (Categories / Mode Switch), merit2 (the mode : EXP / Limit Points -- the client greys
// the current one), merit3 (a merit : Raise / Lower). Labels are images -- the user's reading of them, 2026-11-18
const SubRow MERIT1[2] = { { "Categories", "CatÃ©gories", false, IC_STAR }, { "Mode Switch", "Changer de mode", false, IC_SWAP } };
const SubRow MERIT2[2] = { { "EXP", "EXP", false, IC_STAR }, { "Limit Points", "Limit Points", false, IC_STAR } };
const SubRow MERIT3[2] = { { "Raise", "Augmenter", false, IC_STAR }, { "Lower", "Diminuer", false, IC_STAR } };
// Equipment -> Storage Options (mnstorag) : the bag the list shows -- the user's reading, 2026-11-18
const SubRow MNSTORAG[9] = {
    { "Inventory", "Inventaire", false, IC_BAG }, { "Mog Wardrobe", "Mog Wardrobe", false, IC_BAG }, { "Mog Wardrobe 2", "Mog Wardrobe 2", false, IC_BAG },
    { "Mog Wardrobe 3", "Mog Wardrobe 3", false, IC_BAG }, { "Mog Wardrobe 4", "Mog Wardrobe 4", false, IC_BAG }, { "Mog Wardrobe 5", "Mog Wardrobe 5", false, IC_BAG },
    { "Mog Wardrobe 6", "Mog Wardrobe 6", false, IC_BAG }, { "Mog Wardrobe 7", "Mog Wardrobe 7", false, IC_BAG }, { "Mog Wardrobe 8", "Mog Wardrobe 8", false, IC_BAG },
};
// Items -> a row (iuse) : labels are images -- named from their help texts (items-menu-re 3)
const SubRow IUSE[2] = { { "Use", "Utiliser", false, IC_NONE }, { "Dispose", "Jeter", false, IC_NONE } };
// Synthesis (cmbmenu) and History's favourite (cmbhwin) : labels are images -- named from their help texts (synthesis-re 6)
const SubRow CMBMENU[2] = { { "Crystal Synthesis", "Synth\xC3\xA8se", false, IC_NONE }, { "History", "Historique", false, IC_NONE } };
const SubRow CMBHWIN[2] = { { "Add to favorites", "Ajouter aux favoris", false, IC_NONE }, { "Remove from favorites", "Retirer des favoris", false, IC_NONE } };
// Abilities (abimenu) : by ENTRY (the screen order is by y : 1 2 3 4 6 5) -- abilities-re-2026-11-19.md s.1
const SubRow ABIMENU[6] = {
    { "Job Traits",    "Traits de job",            false, IC_NONE }, { "Job Abilities", "Aptitudes de job", false, IC_NONE },
    { "Weapon Skills", "Weapon Skills",            false, IC_NONE }, { "Ranged Attack", "Attaque \xC3\xA0 distance", false, IC_NONE },
    { "Pet Commands",  "Ordres au familier",       false, IC_NONE }, { "Mount",         "Monture",          false, IC_NONE },
};
// Party (party-re-2026-11-19.md) : labels are images -- the user's reading of them ; by ENTRY (the screen order is by y)
const SubRow PRTY1[7] = { { "Seek Party", "Chercher un groupe", false, IC_NONE }, { "Find Member", "Trouver un membre", false, IC_NONE },
    { "Autogroup", "Groupe auto", false, IC_NONE }, { "Languages", "Langues", false, IC_NONE }, { "Request", "Demandes", false, IC_NONE },
    { "View Request", "Voir les demandes", false, IC_NONE }, { "Alarm Type", "Alarme", false, IC_NONE } };
const SubRow PRTY2[7] = { { "Member List", "Liste des membres", false, IC_NONE }, { "Distribution", "Distribution", false, IC_NONE },
    { "Leave Party", "Quitter le groupe", false, IC_NONE }, { "Rearrange", "R\xC3\xA9organiser", false, IC_NONE }, { "Focus", "Cibler", false, IC_NONE },
    { "Request", "Demandes", false, IC_NONE }, { "View Request", "Voir les demandes", false, IC_NONE } };
const SubRow PRTY3[12] = { { "Find Member", "Trouver un membre", false, IC_NONE }, { "Member List", "Liste des membres", false, IC_NONE },
    { "Distribution", "Distribution", false, IC_NONE }, { "Change Leader", "Changer de leader", false, IC_NONE }, { "Remove Member", "Retirer un membre", false, IC_NONE },
    { "Disband", "Dissoudre", false, IC_NONE }, { "Alliance", "Alliance", false, IC_NONE }, { "Level Sync", "Level Sync", false, IC_NONE },
    { "Rearrange", "R\xC3\xA9organiser", false, IC_NONE }, { "Focus", "Cibler", false, IC_NONE }, { "Request", "Demandes", false, IC_NONE },
    { "View Request", "Voir les demandes", false, IC_NONE } };
const SubRow PRTY4[2] = { { "Form Alliance", "Former une alliance", false, IC_NONE }, { "Dissolve Alliance", "Dissoudre l'alliance", false, IC_NONE } };
const SubRow PRTY8[2] = { { "Lot (pool)", "Tirage au sort", false, IC_NONE }, { "Quartermaster", "Intendant", false, IC_NONE } };
const SubRow PRTY5[5] = { { "English", "Anglais", false, IC_NONE }, { "French", "Fran\xC3\xA7""ais", false, IC_NONE }, { "German", "Allemand", false, IC_NONE },
    { "Japanese", "Japonais", false, IC_NONE }, { "Other languages", "Autres langues", false, IC_NONE } };
// Region Info (region-info-re-2026-11-19.md s.1) : the labels are images ; each opens the map in its mode
const SubRow REGION[4] = { { "Conquest", "Conqu\xC3\xAAte", true, IC_NONE }, { "Besieged", "Besieged", true, IC_NONE },
    { "Campaign", "Campagne", true, IC_NONE }, { "Colonization", "Colonisation", true, IC_NONE } };
const SubRow ALARM[8] = { { "Off", "Aucune", false, IC_NONE }, { "Sound 1", "Son 1", false, IC_NONE }, { "Sound 2", "Son 2", false, IC_NONE },
    { "Sound 3", "Son 3", false, IC_NONE }, { "Sound 4", "Son 4", false, IC_NONE }, { "Sound 5", "Son 5", false, IC_NONE },
    { "Sound 6", "Son 6", false, IC_NONE }, { "Sound 7", "Son 7", false, IC_NONE } };
// Search (search-re-2026-11-19.md s.1b / 1d / 3 / 10) : labels are images -- named from their help texts ; by ENTRY
const SubRow SEARCHMA[17] = {
    { "Area", "Zone", false, IC_NONE }, { "Name", "Nom", false, IC_NONE }, { "Job", "Job", false, IC_NONE }, { "Allegiance", "Nation", false, IC_NONE },
    { "Race", "Race", false, IC_NONE }, { "Level", "Niveau", false, IC_NONE }, { "Mission Rank", "Rang de mission", false, IC_NONE },
    { "Friend List", "Amis", false, IC_NONE }, { "Linkshell", "Linkshell", false, IC_NONE }, { "Ballista", "Ballista", false, IC_NONE },
    { "Search by Comment", "Par commentaire", false, IC_NONE }, { "Edit Your Comment", "Mon commentaire", false, IC_NONE },
    { "Monstrosity", "Monstrosity", false, IC_NONE }, { "Unity", "Unity", false, IC_NONE }, { "Linkshell 2", "Linkshell 2", false, IC_NONE },
    { "Requests", "Demandes", false, IC_NONE }, { "Master Level", "Master Level", false, IC_NONE } };
const SubRow SCSIBORI[15] = {
    { "Area", "Zone", false, IC_NONE }, { "Name", "Nom", false, IC_NONE }, { "Job", "Job", false, IC_NONE }, { "Allegiance", "Nation", false, IC_NONE },
    { "Race", "Race", false, IC_NONE }, { "Level", "Niveau", false, IC_NONE }, { "Mission Rank", "Rang de mission", false, IC_NONE },
    { "Friend List", "Amis", false, IC_NONE }, { "Linkshell", "Linkshell", false, IC_NONE }, { "Ballista", "Ballista", false, IC_NONE },
    { "Comment", "Commentaire", false, IC_NONE }, { "Unity", "Unity", false, IC_NONE }, { "Linkshell 2", "Linkshell 2", false, IC_NONE },
    { "Requests", "Demandes", false, IC_NONE }, { "Master Level", "Master Level", false, IC_NONE } };
const SubRow SEARCHJO[22] = {
    { "Warrior", "Guerrier", false, IC_NONE }, { "Monk", "Moine", false, IC_NONE }, { "White Mage", "Mage blanc", false, IC_NONE },
    { "Black Mage", "Mage noir", false, IC_NONE }, { "Red Mage", "Mage rouge", false, IC_NONE }, { "Thief", "Voleur", false, IC_NONE },
    { "Paladin", "Paladin", false, IC_NONE }, { "Dark Knight", "Chevalier noir", false, IC_NONE }, { "Beastmaster", "Dresseur", false, IC_NONE },
    { "Bard", "Barde", false, IC_NONE }, { "Ranger", "Chasseur", false, IC_NONE }, { "Samurai", "Samoura\xC3\xAF", false, IC_NONE },
    { "Ninja", "Ninja", false, IC_NONE }, { "Dragoon", "Chevalier dragon", false, IC_NONE }, { "Summoner", "Invocateur", false, IC_NONE },
    { "Blue Mage", "Mage bleu", false, IC_NONE }, { "Corsair", "Corsaire", false, IC_NONE }, { "Puppetmaster", "Marionnettiste", false, IC_NONE },
    { "Dancer", "Danseur", false, IC_NONE }, { "Scholar", "\xC3\x89rudit", false, IC_NONE }, { "Geomancer", "G\xC3\xA9omancien", false, IC_NONE },
    { "Rune Fencer", "Escrimeur runique", false, IC_NONE } };
const SubRow SEARCHNA[3] = { { "San d'Oria", "San d'Oria", false, IC_NONE }, { "Bastok", "Bastok", false, IC_NONE }, { "Windurst", "Windurst", false, IC_NONE } };
const SubRow SEARCHRA[5] = { { "Hume", "Hume", false, IC_NONE }, { "Elvaan", "Elvaan", false, IC_NONE }, { "Tarutaru", "Tarutaru", false, IC_NONE },
    { "Mithra", "Mithra", false, IC_NONE }, { "Galka", "Galka", false, IC_NONE } };
const SubRow SEARCHLE[10] = { { "Rank 1", "Rang 1", false, IC_NONE }, { "Rank 2", "Rang 2", false, IC_NONE }, { "Rank 3", "Rang 3", false, IC_NONE },
    { "Rank 4", "Rang 4", false, IC_NONE }, { "Rank 5", "Rang 5", false, IC_NONE }, { "Rank 6", "Rang 6", false, IC_NONE }, { "Rank 7", "Rang 7", false, IC_NONE },
    { "Rank 8", "Rang 8", false, IC_NONE }, { "Rank 9", "Rang 9", false, IC_NONE }, { "Rank 10", "Rang 10", false, IC_NONE } };
const SubRow LINK2[4] = { { "Members", "Membres", false, IC_NONE }, { "Unequip", "Retirer", false, IC_NONE },
    { "Create Linkpearl", "Cr\xC3\xA9""er une linkpearl", false, IC_NONE }, { "Linkshell Message", "Message de la linkshell", false, IC_NONE } };
const SubRow LINK13[3] = { { "Equip as Linkshell 1", "\xC3\x89quiper en Linkshell 1", false, IC_NONE }, { "Dispose", "Jeter", false, IC_NONE },
    { "Equip as Linkshell 2", "\xC3\x89quiper en Linkshell 2", false, IC_NONE } };
const SubRow LINK1[2] = { { "Name the Linkshell", "Nommer la linkshell", false, IC_NONE }, { "Dispose", "Jeter", false, IC_NONE } };
const SubRow LINK7[3] = { { "Tell", "Tell", false, IC_NONE }, { "Promote to Pearlsack", "Promouvoir (Pearlsack)", false, IC_NONE }, { "Kick", "Exclure", false, IC_NONE } };
const SubRow LINK8[3] = { { "Tell", "Tell", false, IC_NONE }, { "Demote to Linkpearl", "R\xC3\xA9trograder (Linkpearl)", false, IC_NONE }, { "Kick", "Exclure", false, IC_NONE } };
const SubRow LINK9[2] = { { "Tell", "Tell", false, IC_NONE }, { "Kick", "Exclure", false, IC_NONE } };
const SubRow SCNAME[3] = { { "Name", "Nom", false, IC_NONE }, { "Search", "Rechercher", false, IC_NONE }, { "Cancel", "Annuler", false, IC_NONE } };
const SubRow SCCHAR[4] = { { "Minimum", "Minimum", false, IC_NONE }, { "Maximum", "Maximum", false, IC_NONE }, { "Search", "Rechercher", false, IC_NONE }, { "Cancel", "Annuler", false, IC_NONE } };
const SubRow COMGENRE[7] = { { "Leveling", "Monter de niveau", false, IC_NONE }, { "Battle Content", "Combats", false, IC_NONE },
    { "Missions & Quests", "Missions et qu\xC3\xAAtes", false, IC_NONE }, { "Items", "Objets", false, IC_NONE }, { "Linkshells", "Linkshells", false, IC_NONE },
    { "Friends", "Amis", false, IC_NONE }, { "Other", "Autre", false, IC_NONE } };
const SubRow SCCOMPAR[3] = { { "Seeking a Party", "Cherche un groupe", false, IC_NONE }, { "Seeking Members", "Cherche des membres", false, IC_NONE }, { "Other", "Autre", false, IC_NONE } };
const SubRow SCCOMITE[3] = { { "Selling", "Vente", false, IC_NONE }, { "Buying", "Achat", false, IC_NONE }, { "Crafting", "Artisanat", false, IC_NONE } };
const SubRow SCCOMLS[3] = { { "Want to Join", "Veut rejoindre", false, IC_NONE }, { "Recruiting", "Recrute", false, IC_NONE }, { "Other", "Autre", false, IC_NONE } };
const SubRow* sub_row(const char* menu, int entry) {
    if (!strcmp(menu, "searchma") && entry >= 1 && entry <= 17) return &SEARCHMA[entry - 1];
    if (!strcmp(menu, "scsibori") && entry >= 1 && entry <= 15) return &SCSIBORI[entry - 1];
    if (!strcmp(menu, "searchjo") && entry >= 1 && entry <= 22) return &SEARCHJO[entry - 1];
    if (!strcmp(menu, "searchna") && entry >= 1 && entry <= 3) return &SEARCHNA[entry - 1];
    if (!strcmp(menu, "searchra") && entry >= 1 && entry <= 5) return &SEARCHRA[entry - 1];
    if (!strcmp(menu, "searchle") && entry >= 1 && entry <= 10) return &SEARCHLE[entry - 1];
    if ((!strcmp(menu, "scchar") || !strcmp(menu, "scmlev")) && entry >= 1 && entry <= 4) return &SCCHAR[entry - 1];
    if (!strcmp(menu, "scname") && entry >= 1 && entry <= 3) return &SCNAME[entry - 1];
    if ((!strcmp(menu, "link2") || !strcmp(menu, "link3") || !strcmp(menu, "link4")) && entry >= 1 && entry <= 4) return &LINK2[entry - 1];
    if (!strcmp(menu, "link13") && entry >= 1 && entry <= 3) return &LINK13[entry - 1];
    if (!strcmp(menu, "link1") && entry >= 1 && entry <= 2) return &LINK1[entry - 1];
    if (!strcmp(menu, "link7") && entry >= 1 && entry <= 3) return &LINK7[entry - 1];
    if (!strcmp(menu, "link8") && entry >= 1 && entry <= 3) return &LINK8[entry - 1];
    if (!strcmp(menu, "link9") && entry >= 1 && entry <= 2) return &LINK9[entry - 1];
    if (!strcmp(menu, "link10") && entry == 1) return &LINK9[0];
    if (!strcmp(menu, "comgenre") && entry >= 1 && entry <= 7) return &COMGENRE[entry - 1];
    if (!strcmp(menu, "sccompar") && entry >= 1 && entry <= 3) return &SCCOMPAR[entry - 1];
    if (!strcmp(menu, "sccomite") && entry >= 1 && entry <= 3) return &SCCOMITE[entry - 1];
    if (!strcmp(menu, "sccomls") && entry >= 1 && entry <= 3) return &SCCOMLS[entry - 1];
    if (!strcmp(menu, "prty1") && entry >= 1 && entry <= 7) return &PRTY1[entry - 1];
    if (!strcmp(menu, "prty2") && entry >= 1 && entry <= 7) return &PRTY2[entry - 1];
    if ((!strcmp(menu, "prty3") || !strcmp(menu, "prty7")) && entry >= 1 && entry <= 12) return &PRTY3[entry - 1];
    if (!strcmp(menu, "prty4") && entry >= 1 && entry <= 2) return &PRTY4[entry - 1];
    if (!strcmp(menu, "prty8") && entry >= 1 && entry <= 2) return &PRTY8[entry - 1];
    if (!strcmp(menu, "prty5us") && entry >= 1 && entry <= 5) return &PRTY5[entry - 1];
    if (!strcmp(menu, "alarm") && entry >= 1 && entry <= 8) return &ALARM[entry - 1];
    if (!strcmp(menu, "region") && entry >= 1 && entry <= 4) return &REGION[entry - 1];
    if (!strcmp(menu, "abimenu") && entry >= 1 && entry <= 6) return &ABIMENU[entry - 1];
    if (!strcmp(menu, "cmbmenu") && entry >= 1 && entry <= 2) return &CMBMENU[entry - 1];
    if (!strcmp(menu, "cmbhwin") && entry >= 1 && entry <= 2) return &CMBHWIN[entry - 1];
    if (!strcmp(menu, "iuse") && entry >= 1 && entry <= 2) return &IUSE[entry - 1];
    if (!strcmp(menu, "mnstorag") && entry >= 1 && entry <= 9) return &MNSTORAG[entry - 1];
    if (!strcmp(menu, "merit1") && entry >= 1 && entry <= 2) return &MERIT1[entry - 1];
    if (!strcmp(menu, "merit2") && entry >= 1 && entry <= 2) return &MERIT2[entry - 1];
    if (!strcmp(menu, "merit3") && entry >= 1 && entry <= 2) return &MERIT3[entry - 1];
    if (!strcmp(menu, "fe_main") && entry >= 1 && entry <= 7) return &FE_MAIN[entry - 1];
    if (!strcmp(menu, "scoption") && entry >= 1 && entry <= 5) return &SCOPTION[entry - 1];
    if (!strcmp(menu, "ut_menu") && entry >= 1 && entry <= 3) return &UT_MENU[entry - 1];
    if (!strcmp(menu, "mgcmenu") && entry >= 1 && entry <= 7) return &MAGSELEC[entry - 1];
    if (!strcmp(menu, "mgcmenu") && entry == 8) return &MGC_TRUST;
    if (!strcmp(menu, "statcom2") && entry >= 1 && entry <= 13) return &STATCOM2[entry - 1];
    if (!strcmp(menu, "chatctrl") && entry >= 1 && entry <= 7) return &CHATCTRL[entry - 1];
    if (!strcmp(menu, "magselec") && entry >= 1 && entry <= 7) return &MAGSELEC[entry - 1];
    if (!strcmp(menu, "abiselec") && entry >= 1 && entry <= 5) return &ABISELEC[entry - 1];
    return 0;
}
const SubRow* sub_row(const char* menu, int entry);
// Search : the value a criterion holds (Refine Search) / a level field (scchar, scmlev) ; false = nothing to show
bool search_value(const NativeEntryMenu& m, int e, char* out, int cap) {
    if (e < 1 || e >= 32) return false;
    const int v = m.entVal[e], v2 = m.entVal2[e];
    if (!strcmp(m.name, "scchar") || !strcmp(m.name, "scmlev")) { if ((e == 1 || e == 2) && v >= 0) { _snprintf(out, cap, "%d", v); out[cap - 1] = 0; return true; } return false; }
    if (!strcmp(m.name, "scname")) {                           // the name field : the text, a caret while typing
        if (e != 1) return false;
        const bool typing = m.entMarkKind == 3;
        _snprintf(out, cap, typing ? "%s_" : "%s", m.entName); out[cap - 1] = 0;
        if (!out[0]) lstrcpynA(out, tr("(press Enter to type)", "(Entr\xC3\xA9""e pour taper)"), cap);
        return true;
    }
    if (strcmp(m.name, "scsibori") || v < 0) return false;
    switch (e) {
    case 1: { const char* z = zone_name(v); if (v2 > 1) _snprintf(out, cap, "%s +%d", z && z[0] ? z : "?", v2 - 1); else lstrcpynA(out, z && z[0] ? z : "?", cap); out[cap - 1] = 0; break; }
    case 2: lstrcpynA(out, m.entName, cap); break;
    case 3: { const char* j = job_abbr(v); lstrcpynA(out, j && j[0] ? j : "?", cap); break; }
    case 4: lstrcpynA(out, v == 0 ? "San d'Oria" : v == 1 ? "Bastok" : v == 2 ? "Windurst" : "?", cap); break;
    case 5: { static const char* const RC[5] = { "Hume", "Elvaan", "Tarutaru", "Mithra", "Galka" }; lstrcpynA(out, v < 5 ? RC[v] : "?", cap); break; }
    case 6: case 7: case 15: if (v2 >= 0 && v2 != v) _snprintf(out, cap, "%d-%d", v, v2); else _snprintf(out, cap, "%d", v); out[cap - 1] = 0; break;
    case 11: { const SubRow* g = sub_row("comgenre", v); lstrcpynA(out, g ? tr(g->en, g->fr) : "?", cap); break; }
    default: lstrcpynA(out, tr("on", "actif"), cap); break;
    }
    return true;
}

inline u32 dev_of(const Frame& f) { return f.dev; }

// ---- ONE block : the menus' theme box, thin capsules, the party's gold glass on the picked row, the hand ----
// `extra` : what the client shows on the right of a row (the Chat submenu : the Tell name, a shell's name, the Unity
// leader) ; `pearl` : 513 linkshell / 514 linkpearl / 515 pearlsack drawn before it, tinted `pearlArgb`
struct RowSpec { const char* label; Icon icon; bool sub; bool off; const char* extra = 0; int pearl = 0; u32 pearlArgb = 0;
                 bool cycle = false; bool center = false;
                 u32 tint = 0;
                 bool mark = false;   // the row HELD by a manual sort : a pulsing gold rim
                 u32 tex = 0; float tu0 = 0.0f, tu1 = 1.0f, tv0 = 0.0f, tv1 = 1.0f; u32 texTint = 0xFFFFFFFFu;
                 u32 ltex = 0; float lw = 0.0f, lh = 0.0f, lu1 = 1.0f, lv1 = 1.0f; };   // ltex : the LABEL as the client's own image (its letters, white) -- lw x lh texels
                 // tex + its UV : the row's picture (Windower's file, a status / element / job cell), tinted texTint   // tex : the row's own picture (a spell / ability icon from Windower's plugins\icons), drawn in place of `icon`   // tint : the label's own colour (the NPC answers : the client's green), 0 the menus' grey-white   // center : a lone message row (the empty list), centred in the block   // cycle : the extra can be changed with RIGHT (the Tell name ; left closes the submenu) -> name >
struct Block {
    float vis = 0.0f, curY = -1.0f; unsigned openMs = 0; int lastN = 0; bool wasOpen = false; float sel[32] = { 0 };
    float x = 0, y = 0, w = 0, h = 0;           // drawn rect this frame (w = 0 : not drawn)
    float rowY0 = 0, rowStep = 0, capX = 0, capW = 0, capH = 0;
    int hoverRow = 0;
    float wKeep = 0.0f; unsigned wKeepMs = 0;   // keep_width : the widest width since this block opened
    float inTop = 0.0f;                         // a band above the rows (the main menu's title and page), inside the box
};
Block g_main, g_subB, g_listB;

// a LIST row from its id : kind 0 a spell id, kind 1 the client's ability index (weapon skills 0..255, job abilities
// 512 + their id -- native_ui.h). `mp` gets the MP cost (0 none) ; `ws` says it is a weapon skill (needs 1000 TP).
const char* list_row_name(int kind, unsigned id, unsigned& mp, bool& ws) {
    mp = 0; ws = false;
    if (kind == 2) { const char* m = mount_name(id); return m ? m : "?"; }   // the mount list : the id itself
    if (kind == 3) { const char* m = item_display_name(id); return m ? m : "?"; }   // the items : its display name
    if (kind == 0) { const SpellRow* sp = spell_info(id); if (!sp) return "?"; mp = sp->mp; return sp->en; }
    if (id >= 0x600 && id < 0x700) { const char* t = native_ability_text(id, false); return t && t[0] ? t : "?"; }   // a JOB TRAIT (Abilities -> Job Traits)
    if (id < 512) { const WSRow* w = ws_info(id); ws = true; return w ? w->en : "?"; }
    if (id >= 512) { const AbilRow* a = abil_info(id - 512); if (!a) { const char* t = native_ability_text(id, false); return t && t[0] ? t : "?"; } mp = a->mp; return a->en; }
    return "?";
}
// ---- the spell / ability ICONS Windower ships for its Timers plugin : <windower>\plugins\icons\spells\NNNNN.png (by spell
// id) and ...\abilities\NNNNN.png (by RECAST id ; the shared recasts 0 (the 1-hours) and 254 have one per job :
// NNNNN.JJ.png). 2026-11-17 : "pour le menu action Windower avait mis ses propres icones". Read at run time from the
// user's install -- AioHUD does not ship them ; no file : the row keeps AioHUD's drawn icon. One texture per icon, a
// small cache (a list page shows ~12), a miss remembered so a missing file is looked up once.
struct IconSlot { unsigned key; u32 tex; unsigned used; bool miss; };
IconSlot g_icons[64]; int g_iconLoads = 0; unsigned g_iconFrame = 0;
bool icon_path(unsigned key, wchar_t* out, int cap) {
    const char* d = plugin_dir(); if (!d[0]) return false;                 // ...\plugins\AioHud
    char root[MAX_PATH]; lstrcpynA(root, d, sizeof(root));
    char* slash = strrchr(root, '\\'); if (!slash) return false; *slash = 0;   // ...\plugins
    char rel[96];
    const unsigned kind = key >> 24, id = (key >> 8) & 0xFFFFu, job = key & 0xFFu;
    if (kind == 1)      _snprintf(rel, sizeof(rel), "icons\\spells\\%05u.png", id);
    else if (job)       _snprintf(rel, sizeof(rel), "icons\\abilities\\%05u.%02u.png", id, job);
    else                _snprintf(rel, sizeof(rel), "icons\\abilities\\%05u.png", id);
    rel[sizeof(rel) - 1] = 0;
    char full[MAX_PATH]; _snprintf(full, sizeof(full), "%s\\%s", root, rel); full[sizeof(full) - 1] = 0;
    return MultiByteToWideChar(CP_ACP, 0, full, -1, out, cap) > 0;
}
bool icon_file(unsigned key, u32* px) {
    if ((key >> 24) == 3) return decode_gear_icon_from_rom((key >> 8) & 0xFFFFu, px);   // an ITEM : its own icon, the game's item DAT
    wchar_t path[MAX_PATH]; return icon_path(key, path, MAX_PATH) && load_png_argb(path, 32, 32, px);
}
bool spell_file(unsigned spellId, u32* px) { return icon_file((1u << 24) | ((spellId & 0xFFFFu) << 8), px); }
// a TRUST with no file of its own. Windower's Trust pictures follow the spell's res icon_id : 1000 + 9 * weapon + colour
// (weapons : shield, sword, bow, staff, club, harp, fist ; colours : red, blue, green, white, orange, purple, pink, cyan,
// grey -- read off the 105 files, 37 of 38 icon_ids consistent). 1) another Trust with the same icon_id has a file : that
// one. 2) else rebuild it : the same WEAPON in another colour, recoloured with the ramp of the wanted COLOUR read on
// another weapon (luminance -> colour, alpha kept). 2026-11-17 : Ygnas (1043), Monberaux (1039), AAHM / AAEV (1008).
bool trust_px(unsigned icon, u32* px) {
    if (icon < 1000 || icon > 1062) return false;
    for (int i = 0; i < SPELLS_N; ++i)
        if (SPELLS[i].trust && SPELLS[i].icon == icon && spell_file(SPELLS[i].id, px)) return true;
    const unsigned w = (icon - 1000) / 9, c = (icon - 1000) % 9;
    static u32 ref[32 * 32];
    bool haveT = false, haveR = false;
    for (int i = 0; i < SPELLS_N && !haveT; ++i) {             // the shape : this weapon, any other colour
        const unsigned ic = SPELLS[i].icon;
        if (SPELLS[i].trust && ic >= 1000 && ic <= 1062 && (ic - 1000) / 9 == w && ic != icon) haveT = spell_file(SPELLS[i].id, px);
    }
    for (int i = 0; i < SPELLS_N && !haveR; ++i) {             // the colour : this colour, any other weapon
        const unsigned ic = SPELLS[i].icon;
        if (SPELLS[i].trust && ic >= 1000 && ic <= 1062 && (ic - 1000) % 9 == c && (ic - 1000) / 9 != w) haveR = spell_file(SPELLS[i].id, ref);
    }
    if (!haveT || !haveR) return false;
    auto lum = [](u32 p) { return (int)((((p >> 16) & 255) * 77 + ((p >> 8) & 255) * 150 + (p & 255) * 29) >> 8); };
    // the colour's RAMP : the mean colour per luminance band, the gaps filled from the nearest band
    float sr[16] = { 0 }, sg[16] = { 0 }, sb[16] = { 0 }; int n[16] = { 0 }; int rlo = 255, rhi = 0, tlo = 255, thi = 0;
    for (int i = 0; i < 32 * 32; ++i) if ((ref[i] >> 24) > 128) { const int L = lum(ref[i]); if (L < rlo) rlo = L; if (L > rhi) rhi = L; }
    for (int i = 0; i < 32 * 32; ++i) if ((px[i] >> 24) > 128)  { const int L = lum(px[i]);  if (L < tlo) tlo = L; if (L > thi) thi = L; }
    if (rhi <= rlo || thi <= tlo) return false;
    for (int i = 0; i < 32 * 32; ++i) if ((ref[i] >> 24) > 128) {
        const int b = (lum(ref[i]) - rlo) * 15 / (rhi - rlo);
        sr[b] += (float)((ref[i] >> 16) & 255); sg[b] += (float)((ref[i] >> 8) & 255); sb[b] += (float)(ref[i] & 255); ++n[b];
    }
    u32 ramp[16]; int last = -1;
    for (int b = 0; b < 16; ++b) if (n[b]) { ramp[b] = 0xFF000000u | ((u32)(sr[b] / n[b]) << 16) | ((u32)(sg[b] / n[b]) << 8) | (u32)(sb[b] / n[b]); last = b; }
    if (last < 0) return false;
    for (int b = 0; b < 16; ++b) if (!n[b]) {                 // nearest filled band
        int best = -1; for (int d = 1; d < 16 && best < 0; ++d) { if (b - d >= 0 && n[b - d]) best = b - d; else if (b + d < 16 && n[b + d]) best = b + d; }
        ramp[b] = ramp[best];
    }
    for (int i = 0; i < 32 * 32; ++i) {
        const u32 a = px[i] >> 24; if (!a) continue;
        int b = (lum(px[i]) - tlo) * 15 / (thi - tlo); b = b < 0 ? 0 : (b > 15 ? 15 : b);
        px[i] = (a << 24) | (ramp[b] & 0x00FFFFFFu);
    }
    return true;
}
// the icon of a list row (kind 0 : spell id ; kind 1 : the client's ability index -- WS < 256, job abilities 512 + id),
// 0 when there is none (yet). At most 3 files read a frame : a page of new rows fills over a few frames.
u32 list_icon(const Frame& f, int kind, unsigned id) {
    unsigned key = 0;
    if (kind == 3) key = (3u << 24) | ((id & 0xFFFFu) << 8);
    else if (kind == 0) key = (1u << 24) | ((id & 0xFFFFu) << 8);
    else if (id >= 0x600) return 0;                                       // job traits : no icon of their own
    else if (id >= 512) {
        const AbilRow* a = abil_info(id - 512); if (!a) return 0;
        const unsigned r = a->recast_id;
        const unsigned job = (r == 0 || r == 254) && f.game ? (unsigned)f.game->me.mjob : 0u;
        if ((r == 0 || r == 254) && !job) return 0;
        key = (2u << 24) | ((r & 0xFFFFu) << 8) | (job & 0xFFu);
    } else return 0;                                                       // weapon skills : Windower has none
    if (g_iconFrame != (unsigned)(f.t * 1000.0f)) { g_iconFrame = (unsigned)(f.t * 1000.0f); g_iconLoads = 0; }
    const unsigned now = GetTickCount();
    int lru = 0;
    for (int i = 0; i < 64; ++i) {
        if (g_icons[i].key == key) { g_icons[i].used = now; return g_icons[i].tex; }
        if (g_icons[i].used < g_icons[lru].used) lru = i;
    }
    if (g_iconLoads >= 3 || !f.dev) return 0;
    ++g_iconLoads;
    static u32 px[32 * 32]; u32 tex = 0;
    bool got = icon_file(key, px);
    if (!got && kind == 0) { const SpellRow* sp = spell_info(id); got = sp && sp->trust && trust_px(sp->icon, px); }
    if (got) tex = make_texture_argb_mip(f.dev, 32, 32, px);
    IconSlot& sl = g_icons[lru];
    if (sl.tex) release_texture(sl.tex);
    sl.key = key; sl.tex = tex; sl.used = now; sl.miss = !tex;
    static int said = 0;
    if (said < 6) { ++said; windower::debug::log("ACTMENU icon %s %u%s -> %s", kind == 3 ? "item" : kind == 0 ? "spell" : "ability recast", (key >> 8) & 0xFFFFu, (key & 0xFF) ? " (per job)" : "", tex ? "loaded" : "none (drawn icon kept)"); }
    return tex;
}
// a row with no picture from Windower, in order : the status the action gives (the game's own icon, the buff atlas) ;
// a weapon skill : its damage element (elemental WS) or its weapon damage type (physical) ; a job
// ability : its JOB's emblem in the role colour (2026-11-17 : "il manque encore des icones pour les JA ... et les WS")
// ; a Trust : the party glyph.
u32 g_elemTex = 0, g_jobTex = 0, g_wpnTex = 0; TexRetry g_elemR, g_jobR, g_wpnR;
// the GIL coin : the player box's own icon (assets/icon_gil.raw, 64x64) -- the user 2026-11-20 : "tu as mis un cercle
// jaune, on a une icone piece de gils". Drawn centred on (cx, cy), z wide ; the old drawn coin while it is not loaded.
u32 g_gilTex = 0; TexRetry g_gilR;
char g_fpsFile[MAX_PATH] = { 0 };   // the FPS hand-over file (net_fps_flag, beside netbox_draw) : removed at unload
u32 g_netTex = 0; TexRetry g_netR;   // the connection box's icons (scripts/gen_net_icons.py) : mail, GM, bag, sword, spinner
void gil_coin(u32 dev, float cx, float cy, float z, float A) {
    const u32 t = ensure_raw_tex(dev, g_gilTex, g_gilR, plugin_path_r("assets\\icon_gil.raw"), 64, 64);
    if (t) {
        dTexQuadState(dev, t);
        tquad(dev, snap(cx - z * 0.5f), snap(cy - z * 0.5f), snap(z), snap(z), 0.0f, 1.0f, 0.0f, 1.0f, with_alpha(0xFFFFFFFFu, A), with_alpha(0xFFFFFFFFu, A));
        dSetTex(dev, 0, 0); cs(dev);
        return;
    }
    const float cr = z * 0.5f;
    disc(dev, cx, cy, cr, with_alpha(0xFFD9A93Au, A)); disc(dev, cx, cy, cr * 0.72f, with_alpha(0xFFF2CD5Cu, A));
}
void set_cell(RowSpec& r, u32 tex, int cell, int cols, int cellPx, int W, int H, float crop) {
    const float au = (float)cellPx / (float)W, av = (float)cellPx / (float)H, cu = au * crop, cv = av * crop;
    const float u = (float)(cell % cols) * au, v = (float)(cell / cols) * av;
    r.tex = tex; r.tu0 = u + cu; r.tu1 = u + au - cu; r.tv0 = v + cv; r.tv1 = v + av - cv;
}
bool status_cell(u32 dev, int st, RowSpec& r) {
    if (st <= 0 || st >= BUFF_COLS * BUFF_ATLAS_ROWS) return false;
    const u32 t = buff_atlas_tex(dev); if (!t) return false;
    set_cell(r, t, st, BUFF_COLS, BUFF_CELL, BUFF_ATLAS_W, BUFF_ATLAS_H, 0.0f); return true;
}
// which job a job ability is (the main job's list first, then the support's) ; 0 unknown
int abil_job(const Frame& f, const char* en) {
    if (!f.game || !en) return 0;
    // the CATEGORY rows (each opens a submenu, absent from the per-job tables) : named here
    static const struct { const char* en; int job; } CAT[] = { { "Waltzes", 19 }, { "Rune Enchantment", 22 }, { "Ward", 22 }, { "Effusion", 22 } };
    for (int i = 0; i < 4; ++i) if (!strcmp(CAT[i].en, en)) return CAT[i].job;
    const int jobs[2] = { f.game->me.mjob, f.game->me.sjob };
    for (int j = 0; j < 2; ++j) { int n = 0; const JobBuff* b = job_track((unsigned)jobs[j], n);
        for (int i = 0; i < n; ++i) if (b[i].name && !strcmp(b[i].name, en)) return jobs[j]; }
    return 0;
}
void list_fallback(const Frame& f, int kind, unsigned id, RowSpec& r) {
    const u32 dev = f.dev;
    if (kind == 0) {
        const SpellRow* sp = spell_info(id); if (!sp) return;
        if (!status_cell(dev, sp->status, r) && sp->trust) r.icon = IC_DUO;
        return;
    }
    if (id < 256) {
        // a weapon skill : an ELEMENTAL one (magical / hybrid -- BG Wiki, scripts/gen_ws.py) its damage element ; a
        // physical one its weapon's damage type (Slashing / Piercing / Blunt). NOT the skillchain property : Shining
        // Blade is Light damage with a Scission (earth) property (2026-11-17, "Shining Blade tu as mis earth")
        const WSRow* w = ws_info(id); if (!w) return;
        if (w->elem >= 0 && w->elem < 8) {
            static const int STRIP[8] = { 0, 4, 3, 1, 5, 2, 6, 7 };   // Fire Ice Wind Earth Thunder Water Light Dark -> element_icons.raw cells
            const u32 t = ensure_raw_tex(dev, g_elemTex, g_elemR, plugin_path_r("assets\\element_icons.raw"), 256, 32);
            if (t) set_cell(r, t, STRIP[w->elem], 8, 32, 256, 32, 0.0f);
            return;
        }
        int dt = -1;                                         // weapon_icons.raw : 0 Slashing 1 Piercing 2 Blunt
        switch (w->skill) {
            case 3: case 4: case 5: case 6: case 7: case 9: case 10: dt = 0; break;   // sword GS axe GA scythe katana GK
            case 2: case 8: case 25: case 26: dt = 1; break;                        // dagger polearm archery marksmanship
            case 1: case 11: case 12: dt = 2; break;                                // H2H club staff
            default: break;
        }
        if (dt < 0) return;
        const u32 t = ensure_raw_tex(dev, g_wpnTex, g_wpnR, plugin_path_r("assets\\weapon_icons.raw"), 96, 32);
        if (t) set_cell(r, t, dt, 3, 32, 96, 32, 0.0f);
        return;
    }
    if (id < 512) return;
    const AbilRow* a = abil_info(id - 512); if (!a) return;
    if (status_cell(dev, a->status, r)) return;
    const int job = abil_job(f, a->en); if (job < 1 || job > 22) return;
    const u32 t = ensure_raw_tex(dev, g_jobTex, g_jobR, plugin_path_r("assets\\job_icons.raw"), 512, 192);
    if (!t) return;
    set_cell(r, t, job - 1, 8, 64, 512, 192, 6.0f / 64.0f);   // the emblems' baked transparent margin, cropped (party.cpp)
    r.texTint = job_role_color(job) | 0xFF000000u;
}
// ---- the shell WORN, as the inventory shows it (2026-11-17 : "on a les icones dans mon inventaire") : the item's own
// icon from the game's item DAT (513 Linkshell, 514 Linkpearl, 515 Pearlsack -- General Items 118/106), recoloured
// in the shell's colour by luminance (the relief kept, the highlights toward white). A decode miss retries (bounded,
// spaced) ; until then the drawn pearl stays.
struct PearlTex { int item; u32 argb; u32 tex; unsigned char tries; unsigned nextMs; };
PearlTex g_pearl[4];
// ---- the menus' IMAGE labels : the client's sprite letters (model/map_dat : menu_label), one texture each, cached ----
struct LabelTex { unsigned char sheet = 0; unsigned short id = 0; u32 tex = 0; int w = 0, h = 0, pw = 0, ph = 0, ox = 0, oy = 0; unsigned char tries = 0; unsigned nextMs = 0; };
LabelTex g_lbl[96]; unsigned g_lblClock = 0; int g_lblOx = 0, g_lblOy = 0;   // the last label's letters origin (a frame : where its texts sit)
// fills r.ltex... from the entry's sprite (sheet 1 windowps, 2 keytops3 ; id + 1) ; false = keep the text label
bool label_img(u32 dev, int sheet, unsigned short sprPlus1, RowSpec& r) {
    if (!dev || !sheet || !sprPlus1) return false;
    const unsigned short id = (unsigned short)(sprPlus1 - 1);
    int slot = -1; for (int i = 0; i < 96; ++i) if (g_lbl[i].sheet == sheet && g_lbl[i].id == id && (g_lbl[i].tex || g_lbl[i].tries)) { slot = i; break; }
    if (slot < 0) { for (int i = 0; i < 96 && slot < 0; ++i) if (!g_lbl[i].sheet) slot = i;
        if (slot < 0) { slot = (int)(g_lblClock++ % 96); if (g_lbl[slot].tex) release_texture(g_lbl[slot].tex); }
        g_lbl[slot] = LabelTex(); g_lbl[slot].sheet = (unsigned char)sheet; g_lbl[slot].id = id; }
    LabelTex& L = g_lbl[slot];
    if (!L.tex && L.tries < 4 && (!L.nextMs || (int)(GetTickCount() - L.nextMs) >= 0)) {
        ++L.tries; L.nextMs = GetTickCount() + 2000;
        const MenuLabel* M = menu_label(sheet == 1 ? "menu    windowps" : "menu    keytops3", id);
        if (M && M->w > 0 && M->h > 0 && M->w <= 512 && M->h <= 512) {
            int pw = 1, ph = 1; while (pw < M->w) pw <<= 1; while (ph < M->h) ph <<= 1;    // a power of two (old D3D8 parts)
            static u32 buf[512 * 512]; memset(buf, 0, sizeof(u32) * (size_t)pw * ph);
            for (int y = 0; y < M->h; ++y) memcpy(buf + (size_t)y * pw, M->px + (size_t)y * M->w, (size_t)M->w * 4);
            L.tex = make_texture_argb(dev, pw, ph, buf); L.w = M->w; L.h = M->h; L.pw = pw; L.ph = ph; L.ox = M->ox; L.oy = M->oy;
        }
    }
    if (!L.tex) return false;
    r.ltex = L.tex; r.lw = (float)L.w; r.lh = (float)L.h; r.lu1 = (float)L.w / (float)L.pw; r.lv1 = (float)L.h / (float)L.ph;
    g_lblOx = L.ox; g_lblOy = L.oy;
    return true;
}
// a menu row from an entry : its image label when the client has one (grey -> the grey look ; alt -> the toggle's other)
void row_image(u32 dev, const NativeEntryMenu& m, int i, RowSpec& r, bool alt = false) {
    if (i < 0 || i >= 32 || !m.sheet[i]) return;
    // only in the client's own language = the config's (an EN client + a French config : our French text stays)
    const int cl = native_client_lang(), ul = ui_config().lang; if (!((ul == 0 && cl == 1) || (ul == 1 && cl == 3))) return;
    const unsigned short id = alt && m.spr[i][1] ? m.spr[i][1] : m.spr[i][0];   // (grey : the normal look, dimmed by the row)
    label_img(dev, m.sheet[i], id, r);
}
u32 pearl_tex(u32 dev, int item, u32 argb) {
    if (!dev || item < 513 || item > 515) return 0;
    int slot = -1;
    for (int i = 0; i < 4; ++i) if (g_pearl[i].item == item && g_pearl[i].argb == argb) { slot = i; break; }
    if (slot < 0) {                                          // a new one : the first free slot, else the oldest replaced
        for (int i = 0; i < 4 && slot < 0; ++i) if (!g_pearl[i].item) slot = i;
        if (slot < 0) slot = 0;
        if (g_pearl[slot].tex) release_texture(g_pearl[slot].tex);
        g_pearl[slot] = PearlTex(); g_pearl[slot].item = item; g_pearl[slot].argb = argb;
    }
    PearlTex& P = g_pearl[slot];
    if (P.tex || P.tries >= 8 || (P.nextMs && (int)(GetTickCount() - P.nextMs) < 0)) return P.tex;
    static u32 px[32 * 32];
    if (decode_gear_icon_from_rom((unsigned)item, px)) {
        const float cr = (float)((argb >> 16) & 255), cg = (float)((argb >> 8) & 255), cb = (float)(argb & 255);
        for (int i = 0; i < 32 * 32; ++i) {
            const u32 q = px[i]; if (!(q >> 24)) continue;
            const float L = (float)(((q >> 16) & 255) * 77 + ((q >> 8) & 255) * 150 + (q & 255) * 29) / (256.0f * 255.0f);
            const float k = 0.20f + 1.05f * L, w = L > 0.78f ? (L - 0.78f) / 0.22f * 0.55f : 0.0f;   // shade, then a white sheen
            auto ch = [&](float c) { float v = c * k; v = v + (255.0f - v) * w; return (u32)(v > 255.0f ? 255.0f : v); };
            px[i] = (q & 0xFF000000u) | (ch(cr) << 16) | (ch(cg) << 8) | ch(cb);
        }
        P.tex = make_texture_argb_mip(dev, 32, 32, px);
    }
    if (!P.tex) { ++P.tries; P.nextMs = (GetTickCount() + 2000u) | 1u; if (P.tries == 8) windower::debug::log("ACTMENU pearl icon : item %d not decoded after 8 tries (the drawn pearl stays)", item); }
    return P.tex;
}
unsigned g_lastMs = 0;

struct Metrics { float S, tz, ow, capH, gap, icZ, pad, gutter, capPadL, icW, capPadR, triW, bi; bool icons; };   // bi : the frame theme's border a box reserves on each side (0 = normal frames)
Metrics metrics_at(float S);
Metrics metrics(const Frame& f) {
    float sc = ui_config().muScale; if (!(sc >= 0.6f && sc <= 1.6f)) sc = 1.0f;   // Game UI -> Menus : their size
    return metrics_at(f.screenH / 1000.0f * sc);
}
Metrics metrics_at(float S_) {
    Metrics k; k.S = S_; const float S = k.S;
    k.tz = 11.5f * S; k.ow = 0.9f * S; k.capH = snap(19.0f * S); k.gap = snap(3.0f * S); k.icZ = 10.0f * S;
    k.pad = snap(6.0f * S); k.gutter = snap(22.0f * S); k.icons = ui_config().muLabel == 1;
    k.capPadL = snap(9.0f * S); k.icW = k.icons ? snap(k.icZ + 7.0f * S) : 0.0f; k.capPadR = snap(6.0f * S); k.triW = snap(8.0f * S);
    k.bi = box_inset(ui_config().muBox);
    return k;
}
// the MAIN menu's chain : no icons (the client's has none -- the user, 2026-11-18) ; the spell lists keep theirs
Metrics no_icons(Metrics k) { k.icons = false; k.icW = 0.0f; return k; }
// ---- the menu boxes drawn THIS FRAME (a theme kit only) : the client's windows sit nearly flush, and a kit's thick frame
// grows every box by its border -- so neighbours overlapped. A box touching / overlapping / within a small gap of one
// already placed is GLUED to it : the two share ONE border (overlap = the border), its other edge snapped flush on the
// neighbour's when within 6 px ; a box only near a corner is pushed out. Then on screen, snapped, recorded. The JOIN mask
// (the sides glued, their ends aligned / protruding -- g_frameJoin's bits) is the one computed at the END of the previous
// frame for that site (`key`, the call site's __LINE__) : the neighbour drawn first is already drawn when this one moves.
// No kit (or Menus' frame off) : nothing moves, nothing is recorded -- the layout stays the client's, pixel for pixel.
struct MenuRect { int key; float x, y, w, h, th; };   // th : the height it is ANIMATING to (the iron frame bakes that one)
struct MenuJoin { int key, mask; };
MenuRect g_mr[48]; int g_mrN = 0;
MenuJoin g_mj[48]; int g_mjN = 0;                       // last frame's join masks
// the procedural frame (theme_kit.cpp, kittest iron) bakes a whole GROUP : last frame's rects, and the slot plates / slots
// a box asked to be milled into the metal (relative to that box's drawn origin ; kind 1 plate, 0 slot)
struct MenuCut { int key, kind; float x, y, w, h, r; };
MenuRect g_mrP[48]; int g_mrPN = 0;
MenuCut g_mc[96], g_mcP[96]; int g_mcN = 0, g_mcPN = 0;
void menu_cut(int key, int kind, float x, float y, float w, float h, float r) {
    if (!theme_kit_bakes_slots()) return;                   // only the iron frame (dev kit) reads them
    if (g_mcN < 96) { MenuCut& c = g_mc[g_mcN++]; c.key = key; c.kind = kind; c.x = x; c.y = y; c.w = w; c.h = h; c.r = r; }
}
inline float ovl(float a0, float a1, float b0, float b1) { return (a1 < b1 ? a1 : b1) - (a0 > b0 ? a0 : b0); }   // > 0 : overlap length, < 0 : gap
void menu_frame_end() {                                  // once a frame (actmenu_hits_clear) : the joins of the rects placed, then a fresh list
    const float bi = box_inset(ui_config().muBox);
    g_mjN = 0;
    // the mask (gfx/window.h g_frameJoin) : bits 0-3 the glued sides (1 L 2 T 4 R 8 B) ; then PER SIDE s (0 L 1 T 2 R 3 B)
    // four bits at 4 + 4*s : its START end (top / left) aligned with a neighbour, its END end aligned, this box PROTRUDES
    // past every neighbour of that side at the start, at the end. Per side : two sides' neighbours must not mix
    for (int i = 0; i < g_mrN && bi > 0.0f; ++i) {
        const MenuRect& a = g_mr[i]; int m = 0;
        float lo[4] = { 1e9f, 1e9f, 1e9f, 1e9f }, hi[4] = { -1e9f, -1e9f, -1e9f, -1e9f }; bool alS[4] = {}, alE[4] = {};
        for (int j = 0; j < g_mrN; ++j) {
            if (j == i) continue;
            const MenuRect& b = g_mr[j];
            const float ox = ovl(a.x, a.x + a.w, b.x, b.x + b.w), oy = ovl(a.y, a.y + a.h, b.y, b.y + b.h);
            int sd = -1; float s0 = 0, s1 = 0, n0 = 0, n1 = 0;
            if (oy > bi + 1.0f && fabsf(ox - bi) < 1.0f) { sd = a.x < b.x ? 2 : 0; s0 = a.y; s1 = a.y + a.h; n0 = b.y; n1 = b.y + b.h; }        // side by side
            else if (ox > bi + 1.0f && fabsf(oy - bi) < 1.0f) { sd = a.y < b.y ? 3 : 1; s0 = a.x; s1 = a.x + a.w; n0 = b.x; n1 = b.x + b.w; }   // stacked
            if (sd < 0) continue;
            m |= 1 << sd;
            if (n0 < lo[sd]) lo[sd] = n0; if (n1 > hi[sd]) hi[sd] = n1;
            if (fabsf(s0 - n0) <= 2.0f) alS[sd] = true; if (fabsf(s1 - n1) <= 2.0f) alE[sd] = true;
        }
        for (int sd = 0; sd < 4; ++sd) if (m & (1 << sd)) {
            const float s0 = (sd & 1) ? a.x : a.y, s1 = (sd & 1) ? a.x + a.w : a.y + a.h;
            if (alS[sd]) m |= 1 << (4 + 4 * sd); if (alE[sd]) m |= 1 << (5 + 4 * sd);
            if (!alS[sd] && s0 < lo[sd] - 2.0f) m |= 1 << (6 + 4 * sd);
            if (!alE[sd] && s1 > hi[sd] + 2.0f) m |= 1 << (7 + 4 * sd);
        }
        g_mj[g_mjN].key = a.key; g_mj[g_mjN].mask = m; ++g_mjN;
    }
    {   // MENUJOIN log : the placed rects + their masks, once per change (a theme kit only) -- the merge's ground truth
        static unsigned lastSig = 0; unsigned sig = (unsigned)g_mrN * 2654435761u;
        for (int i = 0; i < g_mrN; ++i) sig = sig * 31u + (unsigned)g_mr[i].key * 7u + (unsigned)g_mr[i].x * 13u + (unsigned)g_mr[i].y * 17u + (unsigned)g_mr[i].w * 19u + (unsigned)g_mr[i].h * 23u + (unsigned)g_mj[i].mask;
        if (sig != lastSig && g_mrN > 0) { lastSig = sig;
            windower::debug::log("MENUJOIN %d box(es), border %d :", g_mrN, (int)bi);
            for (int i = 0; i < g_mrN; ++i) windower::debug::log("MENUJOIN   line %d : x %d y %d w %d h %d (right %d bottom %d) mask %X", g_mr[i].key, (int)g_mr[i].x, (int)g_mr[i].y, (int)g_mr[i].w, (int)g_mr[i].h, (int)(g_mr[i].x + g_mr[i].w), (int)(g_mr[i].y + g_mr[i].h), g_mj[i].mask);
        }
    }
    memcpy(g_mrP, g_mr, sizeof(MenuRect) * (size_t)g_mrN); g_mrPN = g_mrN;
    memcpy(g_mcP, g_mc, sizeof(MenuCut) * (size_t)g_mcN); g_mcPN = g_mcN;
    g_mrN = 0; g_mcN = 0;
}
// the mouse HOVERS a row only when it MOVED : a box that re-lays out (grows, glues elsewhere) under a still pointer must
// not drag the client's cursor back (2026-11-22 : Missions -- down from Current opened the timeline, the box moved, the
// Current card slid under the idle pointer and the hover sent the cursor back up : "bas ne descend pas"). One pair per site.
static bool mouse_moved(const MouseState* ms, float* last) {
    if (!ms) return false;
    const bool m = fabsf(ms->x - last[0]) > 0.5f || fabsf(ms->y - last[1]) > 0.5f; last[0] = ms->x; last[1] = ms->y; return m;
}
// growW (optional) : a box STACKED on a wider neighbour takes its width + left edge -> both vertical edges run straight
// through the join (the user : "les barres verticales mal alignees") ; its content must lay out from *growW afterwards
int menu_place(const Frame& f, int key, float& x, float& y, float w, float h, float* growW = nullptr, float targetH = 0.0f) {
    const float bi = box_inset(ui_config().muBox);
    if (bi <= 0.0f || w <= 0.0f || h <= 0.0f) return 0;
    const float W = f.screenW, H = f.screenH, near_ = 2.0f * bi + 4.0f, al = 6.0f;
    auto fits = [&](float nx, float ny) { return nx >= 4.0f && nx + w <= W - 4.0f && ny >= 4.0f && ny + h <= H - 4.0f; };
    // the neighbours : this frame's placed so far, AND last frame's not placed yet -- a box drawn BEFORE an older
    // neighbour glues to it instead of shoving it (2026-11-20 : Profile, drawn before Status, pushed Status left)
    auto placedNow = [&](int k_) { for (int j = 0; j < g_mrN; ++j) if (g_mr[j].key == k_) return true; return false; };
    for (int pass = 0; pass < 4; ++pass) {
        bool moved = false;
        for (int i = 0; i < g_mrN + g_mrPN; ++i) {
            const MenuRect& r = i < g_mrN ? g_mr[i] : g_mrP[i - g_mrN];
            if (r.key == key || (i >= g_mrN && placedNow(r.key))) continue;
            const float ox = ovl(x, x + w, r.x, r.x + r.w), oy = ovl(y, y + h, r.y, r.y + r.h);
            const bool hz = oy > bi + 1.0f && ox > -near_, vt = ox > bi + 1.0f && oy > -near_;   // a side can be shared
            const bool gH = hz && fabsf(ox - bi) < 0.5f, gV = vt && fabsf(oy - bi) < 0.5f;     // already one border
            if (!gH && !gV) {
                if (!hz && !vt && (ox <= 0.0f || oy <= 0.0f)) continue;                        // apart, or only near a corner
                float best = 1e9f, dx = 0.0f, dy = 0.0f;
                auto cand = [&](float cx, float cy) { const float c = fabsf(cx) + fabsf(cy) + (fits(x + cx, y + cy) ? 0.0f : 1e6f); if (c < best) { best = c; dx = cx; dy = cy; } };
                if (hz) { cand(r.x + r.w - bi - x, 0.0f); cand(r.x - w + bi - x, 0.0f); }      // glue right / left of it
                if (vt) { cand(0.0f, r.y + r.h - bi - y); cand(0.0f, r.y - h + bi - y); }      // glue below / above it
                if (!hz && !vt) { cand(r.x + r.w - x, 0.0f); cand(r.x - w - x, 0.0f); cand(0.0f, r.y + r.h - y); cand(0.0f, r.y - h - y); }   // a corner : out
                x = snap(x + dx); y = snap(y + dy); moved = true;
                if (!hz && !vt) continue;
            }
            // glued : the other edges flush on the neighbour's when nearly so (more joins come out aligned)
            const bool side = fabsf(ovl(x, x + w, r.x, r.x + r.w) - bi) < 0.5f;
            if (side) { const float ty = fabsf(y - r.y) <= al ? r.y : fabsf(y + h - r.y - r.h) <= al ? r.y + r.h - h : y; if (ty != y) { y = ty; moved = true; } }
            else if (growW && r.w > w && r.w <= W - 8.0f) { if (x != r.x || w != r.w) { x = r.x; w = r.w; *growW = w; moved = true; } }   // stacked : the neighbour's width
            else      { const float tx = fabsf(x - r.x) <= al ? r.x : fabsf(x + w - r.x - r.w) <= al ? r.x + r.w - w : x; if (tx != x) { x = tx; moved = true; } }
        }
        if (!moved) break;
    }
    if (x + w > W - 4.0f) x = snap(W - 4.0f - w); if (x < 4.0f) x = 4.0f;
    if (y + h > H - 4.0f) y = snap(H - 4.0f - h); if (y < 4.0f) y = 4.0f;
    x = snap(x); y = snap(y);
    if (g_mrN < 48) { MenuRect& n = g_mr[g_mrN++]; n.key = key; n.x = x; n.y = y; n.w = w; n.h = h; n.th = targetH > 0.0f ? targetH : h; }
    for (int i = 0; i < g_mjN; ++i) if (g_mj[i].key == key) return g_mj[i].mask;
    return 0;
}
// the same for a box whose drawn (x, y) carries an opening slide : its rest place (ux, uy) is placed, (x, y) moved alike
int menu_place_at(const Frame& f, int key, float& x, float& y, float ux, float uy, float w, float h) {
    float px = ux, py = uy;
    const int m = menu_place(f, key, px, py, w, h);
    if (px != ux || py != uy) { x = snap(x + px - ux); y = snap(y + py - uy); }
    return m;
}
// the submenu arrow's column : only when a row has one (2026-11-16 : "trop large" -- Magic / Abilities / Chat have none)
float tri_col(const Metrics& k, const RowSpec* rows, int n) { for (int i = 0; i < n; ++i) if (rows[i].sub) return k.triW; return 0.0f; }
// the block's size for these rows
// an image label's drawn size : its letters at the text's height (the client's 9 x 11 glyphs, scaled)
float label_img_h(const Metrics& k) { return snap(k.tz * 0.82f); }
float label_img_w(const Metrics& k, const RowSpec& r) { return r.lh > 0.0f ? snap(r.lw * label_img_h(k) / r.lh) : 0.0f; }
void block_size(const Frame& f, const Metrics& k, const RowSpec* rows, int n, float& w, float& h) {
    float wLab = 0.0f, wEx = 0.0f;
    for (int i = 0; i < n; ++i) {
        const float lw = rows[i].ltex ? label_img_w(k, rows[i]) : f.font->measure(rows[i].label, k.tz); if (lw > wLab) wLab = lw;
        float ew = rows[i].extra && rows[i].extra[0] ? f.font->measure(rows[i].extra, k.tz * 0.92f) : 0.0f;
        if (rows[i].pearl) ew += k.capH + 3.0f * k.S;   // its inventory icon, the capsule's height
        if (rows[i].cycle) ew += 9.0f * k.S;
        if (ew > wEx) wEx = ew;
    }
    const float capW = snap(k.capPadL + k.icW + wLab + (wEx > 0.0f ? wEx + 8.0f * k.S : 0.0f) + k.capPadR + tri_col(k, rows, n));
    w = snap(k.gutter + capW + k.pad) + 2.0f * k.bi; h = snap((float)n * k.capH + (float)(n - 1) * k.gap + 2.0f * k.pad) + 2.0f * k.bi;   // + the frame border
}
// open / close easing ; false when nothing is to be drawn
// FIXED widths (the user, 2026-11-19 : "on veut une taille fixe") : an item list is as wide as a long item name (99 % of
// the game's display names fit ; a longer one is cut with "...") ; the area list as wide as the game's longest zone name
const char* const ITEM_NAME_REF = "Bowl of soy sauce-based ramen";
const char* const ZONE_NAME_REF = "Silver Sea route to Al Zahbi";
// `s` cut to `maxW` with "..." (into buf) ; s itself when it fits
const char* fit_text(Font* fo, const char* s, float tz, float maxW, char* buf, int cap) {
    if (!s || fo->measure(s, tz) <= maxW) return s;
    int n = lstrlenA(s); if (n > cap - 4) n = cap - 4;
    for (; n > 0; --n) { memcpy(buf, s, (size_t)n); buf[n] = '.'; buf[n + 1] = '.'; buf[n + 2] = '.'; buf[n + 3] = 0; if (fo->measure(buf, tz) <= maxW) return buf; }
    return "...";
}
// a list's width NEVER shrinks while it stays open : scrolling shows other names, the box keeps its size (it only widens
// once, if a longer name than any seen comes into view). Reset each time the block opens.
float keep_width(Block& b, float w) {
    if (b.wKeepMs != b.openMs) { b.wKeepMs = b.openMs; b.wKeep = 0.0f; }
    if (w > b.wKeep) b.wKeep = w;
    return b.wKeep;
}
bool block_ease(Block& b, bool open, unsigned now, float dt) {
    const float kFast = 1.0f - expf(-dt * 45.0f);   // ~70 ms to settle : smooth, never in the way
    if (open && !b.wasOpen) { b.openMs = now; for (int i = 0; i < 32; ++i) b.sel[i] = 0.0f; b.curY = -1.0f; }
    b.wasOpen = open;
    b.vis += ((open ? 1.0f : 0.0f) - b.vis) * kFast;
    if (b.vis < 0.01f) { b.vis = 0.0f; b.curY = -1.0f; b.w = 0.0f; return false; }
    return true;
}
// draw at (bx, by) ; returns the row (1-based) under the mouse, 0 none
int block_draw(const Frame& f, const Metrics& k, Block& b, const RowSpec* rows, int n, int cursorRow, bool focused,
               float bx, float by, float bw, float bh, float A, float dt) {
    const float triU = tri_col(k, rows, n);   // the arrow column, when one row has an arrow
    const u32 dev = f.dev; const float S = k.S;
    const float kFast = 1.0f - expf(-dt * 30.0f), kSel = 1.0f - expf(-dt * 24.0f);   // the selection glides, quickly
    b.x = bx; b.y = by; b.w = bw; b.h = bh;              // the BOX rect (incl. the frame theme's border)
    draw_themed_box(dev, f.skin, bx, by, bw, bh, ui_config().muBox, A, S);   // Game UI -> Menus : one theme for all
    bx += k.bi; by += k.bi; bw -= 2.0f * k.bi; bh -= 2.0f * k.bi;   // from here : the CONTENT area, inside the border
    const float capW = bw - k.gutter - k.pad, cx0 = bx + k.gutter, step = k.capH + k.gap, r = k.capH * 0.5f;
    const float ry0 = by + b.inTop;                     // the rows' top (below the header band, when there is one)
    b.rowY0 = ry0 + k.pad; b.rowStep = step; b.capX = cx0; b.capW = capW; b.capH = k.capH;
    const bool hasCur = cursorRow >= 1 && cursorRow <= n;
    if (hasCur) {
        const float targetY = ry0 + k.pad + (float)(cursorRow - 1) * step;
        if (b.curY < 0.0f || b.lastN != n) b.curY = targetY;
        b.curY += (targetY - b.curY) * kFast;
        if (fabsf(targetY - b.curY) < 0.3f) b.curY = targetY;
    }
    b.lastN = n;
    for (int i = 0; i < n && i < 32; ++i) {
        const bool on = i + 1 == cursorRow;
        b.sel[i] += ((on ? 1.0f : 0.0f) - b.sel[i]) * kSel;
        if (fabsf((on ? 1.0f : 0.0f) - b.sel[i]) < 0.004f) b.sel[i] = on ? 1.0f : 0.0f;
        const float y = snap(ry0 + k.pad + (float)i * step);
        const float a = A * (rows[i].off ? 0.55f : 1.0f);
        if (rows[i].center) {   // a message row : its capsule spans the block evenly (no hand column -- nothing to pick)
            const float mx = bx + k.pad, mw = bw - 2.0f * k.pad;
            rrect(dev, mx, y, mw, k.capH, r, with_alpha(0x5A1C2638u, a), with_alpha(0x5A0A0F18u, a));
            rrect_topcaps(dev, mx + 1.0f, y + 1.0f, mw - 2.0f, snap(k.capH * 0.45f), r - 1.0f, with_alpha(0x14FFFFFFu, a), with_alpha(0x00FFFFFFu, a));
            rrect_stroke(dev, mx, y, mw, k.capH, r, with_alpha(0x38C8D6EAu, a), 1.0f);
            continue;
        }
        rrect(dev, cx0, y, capW, k.capH, r, with_alpha(0x5A1C2638u, a), with_alpha(0x5A0A0F18u, a));
        rrect_topcaps(dev, cx0 + 1.0f, y + 1.0f, capW - 2.0f, snap(k.capH * 0.45f), r - 1.0f, with_alpha(0x14FFFFFFu, a), with_alpha(0x00FFFFFFu, a));
        rrect_stroke(dev, cx0, y, capW, k.capH, r, with_alpha(0x38C8D6EAu, a), 1.0f);
        if (rows[i].mark) {                                    // held by a manual sort : a gold rim, breathing
            const float p = 0.55f + 0.45f * sinf(f.t * 6.0f);
            rrect(dev, cx0, y, capW, k.capH, r, with_alpha(0x30FFDA8Au, a * p), with_alpha(0x18FFDA8Au, a * p));
            rrect_stroke(dev, cx0, y, capW, k.capH, r, with_alpha(0xF0FFDA8Au, a * p), 1.5f * S);
        }
    }
    if (hasCur) {   // the selection glides between rows : clipped to its capsule, the exact party glass + sweep, a gold rim
        const float y = snap(b.curY);
        rrect_clip_begin(dev, cx0, y, capW, k.capH, r);
        party_selframe(dev, cx0, y, capW, k.capH, f.t, A, 0);
        rrect_clip_end(dev);
        rrect_stroke(dev, cx0, y, capW, k.capH, r, with_alpha(0xC8FFDA8Au, A), 1.0f);
    }
    for (int i = 0; i < n && i < 32; ++i) {
        const float s = b.sel[i], a = A * (rows[i].off ? 0.55f : 1.0f);
        const float cy = snap(ry0 + k.pad + (float)i * step) + k.capH * 0.5f;
        if (k.icons && rows[i].tex) {                          // a picture : a touch larger than the drawn glyph, the capsule's height
            const float z = snap(k.capH - 3.0f * S), ix = snap(cx0 + k.capPadL + k.icZ * 0.5f - z * 0.5f), iy = snap(cy - z * 0.5f);
            const u32 tc = with_alpha(rows[i].texTint, a);
            dTexQuadState(dev, rows[i].tex);
            tquad(dev, ix, iy, z, z, rows[i].tu0, rows[i].tu1, rows[i].tv0, rows[i].tv1, tc, tc);
            dSetTex(dev, 0, 0); cs(dev);
        } else if (k.icons) draw_icon(dev, rows[i].icon, cx0 + k.capPadL + k.icZ * 0.5f, cy, k.icZ, S, with_alpha(mix(0xFF9FB0C6u, 0xFFFFF1CCu, s), a));
        const u32 ptex = rows[i].pearl ? pearl_tex(dev, rows[i].pearl, rows[i].pearlArgb) : 0;
        if (ptex) {                                            // the shell worn : its inventory icon, in its colour, before its name
            const float ew = rows[i].extra ? f.font->measure(rows[i].extra, k.tz * 0.92f) : 0.0f;
            const float z = snap(k.capH - 2.0f * S), ix = snap(cx0 + capW - k.capPadR - triU - ew - 5.0f * S - z), iy = snap(cy - z * 0.5f);
            dTexQuadState(dev, ptex);
            tquad(dev, ix, iy, z, z, 0.0f, 1.0f, 0.0f, 1.0f, with_alpha(0xFFFFFFFFu, a), with_alpha(0xFFFFFFFFu, a));
            dSetTex(dev, 0, 0); cs(dev);
        } else if (rows[i].pearl) {                            // (the icon not read yet) the drawn pearl, in its colour
            const float ew = rows[i].extra ? f.font->measure(rows[i].extra, k.tz * 0.92f) : 0.0f;
            const float px = cx0 + capW - k.capPadR - triU - ew - 8.0f * S, pr = 4.2f * S;
            const u32 pc = with_alpha(rows[i].pearlArgb, a);
            if (rows[i].pearl == 515) rrect_stroke(dev, px - pr - 1.6f * S, cy - pr - 1.6f * S, 2.0f * pr + 3.2f * S, 2.0f * pr + 3.2f * S, pr + 1.6f * S, with_alpha(0xD0F2C66Bu, a), 1.2f * S);   // pearlsack : a gold ring
            if (rows[i].pearl == 513) disc_glow(dev, px, cy, pr, with_alpha(0x70F2C66Bu, a), 4.0f * S);                     // linkshell : a gold halo
            disc(dev, px, cy, pr, pc);
            disc(dev, px - pr * 0.35f, cy - pr * 0.35f, pr * 0.35f, with_alpha(0x90FFFFFFu, a));                            // the pearl's sheen
        }
        if (rows[i].cycle && rows[i].extra && rows[i].extra[0]) {   // < name > : left / right changes it
            const float ew = f.font->measure(rows[i].extra, k.tz * 0.92f), ch = 3.0f * S;
            const float R = cx0 + capW - k.capPadR - triU - 9.0f * S;          // where the name ends (text pass : same)
            const float rc = R + 4.5f * S;                                     // RIGHT only : right cycles, left closes the submenu
            (void)ew;
            const u32 cc = with_alpha(mix(0xB093A6BEu, 0xFFFFDA8Au, s), a);
            tri(dev, rc + ch * 0.7f, cy, rc - ch * 0.5f, cy - ch, rc - ch * 0.5f, cy + ch, cc);
        }
        if (rows[i].sub) {
            const float tx = cx0 + capW - k.capPadR * 0.5f - k.triW * 0.5f, th = 3.2f * S;
            tri(dev, tx + th * 0.8f, cy, tx - th * 0.6f, cy - th, tx - th * 0.6f, cy + th, with_alpha(mix(0xA0B9C8DCu, 0xFFFFF1CCu, s), a));
        }
    }
    if (hasCur && focused) {   // AioHUD's hand, in the gutter of the menu being driven, bobbing on the party's rhythm
        const float hand = snap(k.capH * 1.25f), cy = b.curY + k.capH * 0.5f;
        const float hx = bx + k.gutter * 0.5f - 1.0f * S + party_cursor_bob(f.t, hand);
        if (g_handTex) party_cursor(dev, g_handTex, hx, cy, hand, 0);
        else tri(dev, hx + 5.0f * S, cy, hx - 3.5f * S, cy - 5.0f * S, hx - 3.5f * S, cy + 5.0f * S, with_alpha(0xFFFFDA8Au, A));
    }
    // the IMAGE labels (the client's letters) : a shadow, then the letters in the text's colour
    for (int i = 0; i < n && i < 32; ++i) if (rows[i].ltex && !rows[i].center) {
        const float s = b.sel[i], a = A * (rows[i].off ? 0.5f : 1.0f), y = snap(ry0 + k.pad + (float)i * step);
        const float ih = label_img_h(k), iw = label_img_w(k, rows[i]), ix = snap(cx0 + k.capPadL + k.icW), iy = snap(y + (k.capH - ih) * 0.5f);
        const u32 lc = rows[i].tint ? mix(rows[i].tint, 0xFFE4FFD6u, s * 0.6f) : mix(0xFFC9D3E0u, 0xFFFFFFFFu, s);
        dTexQuadState(dev, rows[i].ltex);
        tquad(dev, ix + 1.0f * S, iy + 1.0f * S, iw, ih, 0.0f, rows[i].lu1, 0.0f, rows[i].lv1, with_alpha(0xC8000000u, a), with_alpha(0xC8000000u, a));
        tquad(dev, ix, iy, iw, ih, 0.0f, rows[i].lu1, 0.0f, rows[i].lv1, with_alpha(lc, a), with_alpha(lc, a));
        dSetTex(dev, 0, 0); cs(dev);
    }
    // the labels, in ONE textured pass (begin() binds the glyph state -- a shape drawn before leaves the stage on plain
    // colour, which draws every glyph as a solid quad : the white bands of 2026-11-16)
    Font* fo = f.font;
    fo->begin(dev);
    for (int i = 0; i < n && i < 32; ++i) {
        const float s = b.sel[i], a = A * (rows[i].off ? 0.5f : 1.0f);
        const float y = snap(ry0 + k.pad + (float)i * step);
        if (rows[i].center) {
            fo->draw_lc(dev, snap(bx + (bw - fo->measure(rows[i].label, k.tz)) * 0.5f), snap(y + k.capH * 0.5f), rows[i].label, k.tz,
                        with_alpha(0xFFC9D3E0u, a), with_alpha(0xC8000000u, a), k.ow);
            continue;
        }
        const u32 lc = rows[i].tint ? mix(rows[i].tint, 0xFFE4FFD6u, s * 0.6f) : mix(0xFFC9D3E0u, 0xFFFFFFFFu, s);
        if (!rows[i].ltex) fo->draw_lc(dev, snap(cx0 + k.capPadL + k.icW), snap(y + k.capH * 0.5f), rows[i].label, k.tz,
                                       with_alpha(lc, a), with_alpha(0xC8000000u, a), k.ow);
        if (rows[i].extra && rows[i].extra[0]) {
            const float ez = k.tz * 0.92f, ew = fo->measure(rows[i].extra, ez);
            fo->draw_lc(dev, snap(cx0 + capW - k.capPadR - triU - ew - (rows[i].cycle ? 9.0f * k.S : 0.0f)), snap(y + k.capH * 0.5f), rows[i].extra, ez,
                        with_alpha(mix(0xFF93A6BEu, 0xFFFFF1CCu, s), a), with_alpha(0xC8000000u, a), k.ow);
        }
    }
    // the row under the mouse (the menu being driven only)
    const MouseState* ms = f.mouse;
    if (!(ms && ms->focused && focused && ms->x >= cx0 && ms->x < cx0 + capW)) return 0;
    const float ry = ms->y - (ry0 + k.pad);
    if (ry < 0.0f) return 0;
    const int row = (int)(ry / step);
    return (row < n && ry - (float)row * step < k.capH) ? row + 1 : 0;
}

} // namespace

void actmenu_set_hand_tex(u32 tex) { g_handTex = tex; }
// the NPC answers block (g_hit[3]) : the wheel over it moves the client's cursor (npc_draw consumes it)
static volatile long g_npcWheel = 0;
bool npc_answers_hit(float fx, float fy) {
    const float* h = g_hit[3];
    return h[2] > 0.0f && fx >= h[0] && fx < h[0] + h[2] && fy >= h[1] && fy < h[1] + h[3];
}
void npc_wheel_add(int d) { InterlockedExchangeAdd(&g_npcWheel, d); }
void actmenu_hits_clear() { for (int i = 0; i < HIT_N; ++i) g_hit[i][2] = 0.0f; menu_frame_end(); }   // once a frame : the menu boxes' joins too
// the glued group of the box drawn at (x, y, w, h) (hud_internal.h) : its rest rect = last frame's rect of the same size
// nearest to it (a box drawn mid-slide sits a few px off its rest place) ; the group = the rects overlapping, transitively
bool menu_group_lookup(float x, float y, float w, float h, MenuGroup& g) {
    // the rects : THIS frame's placed so far (the box itself among them, at its exact rest place) + last frame's for the
    // others (not placed yet) -- a box that just appeared (the description back after an empty slot) is found at once
    MenuRect R[96]; int nR = 0;
    for (int i = 0; i < g_mrN; ++i) R[nR++] = g_mr[i];
    for (int i = 0; i < g_mrPN && nR < 96; ++i) { bool cur = false; for (int j = 0; j < g_mrN; ++j) if (g_mr[j].key == g_mrP[i].key) cur = true; if (!cur) R[nR++] = g_mrP[i]; }
    // the same size within 60 px (a slide) ; else the same PLACE within 12 px whatever the size (a box that just resized --
    // the description on every item : unmatched, it baked a plate of its own and blinked its plain frame, 2026-11-20)
    int me = -1; float best = 1e9f;
    for (int i = 0; i < nR; ++i) {
        const MenuRect& r = R[i];
        const float d = fabsf(r.x - x) + fabsf(r.y - y);
        const bool same = fabsf(r.w - w) <= 1.5f && fabsf(r.h - h) <= 1.5f;
        if (!(same ? d < 60.0f : d < 12.0f)) continue;
        const float score = d + (same ? 0.0f : 100.0f);
        if (score < best) { best = score; me = i; }
    }
    if (me < 0) return false;
    int id[96]; int n = 0; bool in[96] = {}; id[n++] = me; in[me] = true;
    for (int q = 0; q < n && n < 16; ++q) {
        const MenuRect& a = R[id[q]];
        for (int j = 0; j < nR && n < 16; ++j) {
            if (in[j]) continue;
            const MenuRect& b = R[j];
            if (ovl(a.x, a.x + a.w, b.x, b.x + b.w) > 0.5f && ovl(a.y, a.y + a.h, b.y, b.y + b.h) > 0.5f) { in[j] = true; id[n++] = j; }
        }
    }
    // ONE order for the whole group (top to bottom, left to right) : every member must describe it identically, or each
    // box keys its own copy of the same plate (seen 2026-11-20 : 5 keys, the cache thrashed, a 260 ms bake every frame)
    for (int a = 1; a < n; ++a) for (int b = a; b > 0; --b) {
        const MenuRect &p = R[id[b - 1]], &q = R[id[b]];
        if (q.y < p.y - 0.5f || (fabsf(q.y - p.y) <= 0.5f && q.x < p.x)) { const int t = id[b]; id[b] = id[b - 1]; id[b - 1] = t; } else break;
    }
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (int q = 0; q < n; ++q) { const MenuRect& r = R[id[q]];
        if (r.x < x0) x0 = r.x; if (r.y < y0) y0 = r.y; if (r.x + r.w > x1) x1 = r.x + r.w; if (r.y + r.th > y1) y1 = r.y + r.th; }
    {   // MENUGROUP : what each box sees around it, once per change (the iron frame's grouping truth)
        static struct { int key; unsigned sig; } seen[24]; static int ns = 0;
        unsigned sig = (unsigned)n * 2654435761u; for (int q = 0; q < n; ++q) sig = sig * 31u + (unsigned)R[id[q]].key;
        int si = -1; for (int i = 0; i < ns; ++i) if (seen[i].key == R[me].key) si = i;
        if (si < 0 && ns < 24) { si = ns++; seen[si].key = R[me].key; seen[si].sig = 0; }
        if (si >= 0 && seen[si].sig != sig) {
            seen[si].sig = sig;
            windower::debug::log("MENUGROUP line %d (drawn %d,%d %dx%d) : %d box(es) of %d known (%d this frame)", R[me].key, (int)x, (int)y, (int)w, (int)h, n, nR, g_mrN);
            for (int i = 0; i < nR; ++i) windower::debug::log("MENUGROUP   %s line %d : %d,%d %dx%d%s", i < g_mrN ? "now " : "prev", R[i].key, (int)R[i].x, (int)R[i].y, (int)R[i].w, (int)R[i].h, in[i] ? "  <- in" : "");
        }
    }
    g.n = n; g.self = 0; g.np = 0; g.nc = 0;
    g.W = floorf(x1 - x0 + 0.5f); g.H = floorf(y1 - y0 + 0.5f); g.ox = floorf(x0 + 0.5f); g.oy = floorf(y0 + 0.5f);
    for (int q = 0; q < n; ++q) {
        const MenuRect& r = R[id[q]];
        if (id[q] == me) g.self = q;
        g.r[q][0] = floorf(r.x - x0 + 0.5f); g.r[q][1] = floorf(r.y - y0 + 0.5f); g.r[q][2] = floorf(r.w + 0.5f); g.r[q][3] = floorf(r.th + 0.5f);
        bool curCuts = false; for (int c = 0; c < g_mcN; ++c) if (g_mc[c].key == r.key) curCuts = true;   // this frame's, else last frame's
        const MenuCut* cl = curCuts ? g_mc : g_mcP; const int cn = curCuts ? g_mcN : g_mcPN;
        for (int c = 0; c < cn; ++c) {
            const MenuCut& k = cl[c];
            if (k.key != r.key) continue;
            if (k.kind && g.np < 8) g.po[g.np] = q; else if (!k.kind && g.nc < 64) g.co[g.nc] = q;
            float* d = k.kind ? (g.np < 8 ? g.p[g.np++] : nullptr) : (g.nc < 64 ? g.c[g.nc++] : nullptr);
            if (!d) continue;
            d[0] = floorf(g.r[q][0] + k.x + 0.5f); d[1] = floorf(g.r[q][1] + k.y + 0.5f); d[2] = floorf(k.w + 0.5f); d[3] = floorf(k.h + 0.5f); d[4] = k.r;
        }
    }
    return true;
}
// the auto-translate picker (g_hit[4]) : the wheel over it steps the client's selection (fep_draw consumes it)
static volatile long g_fepWheel = 0;
bool fep_hit(float fx, float fy) {
    const float* h = g_hit[4];
    return h[2] > 0.0f && fx >= h[0] && fx < h[0] + h[2] && fy >= h[1] && fy < h[1] + h[3];
}
void fep_wheel_add(int d) { InterlockedExchangeAdd(&g_fepWheel, d); }
bool actmenu_hit(float fx, float fy) {
    for (int i = 0; i < HIT_N; ++i) {
        const float* h = g_hit[i];
        if (h[2] > 0.0f && fx >= h[0] && fx < h[0] + h[2] && fy >= h[1] && fy < h[1] + h[3]) return true;
    }
    return false;
}

namespace {   // the item card (below, with the NPC windows) : the Items list draws it too
void card_load(u32 dev, unsigned id);
bool card_ready(unsigned id);
void card_use(int s);          // 0 the card, 1 the second one (the compare view)
extern float g_cardBottom;
void card_draw(const Frame& f, const Metrics& k, float bx, float bw, float top, float bottom, float tO, float A, bool above);

// the Items lists' card : ABOVE AioHUD's chat (else below it), its left edge on the chat's, as wide as the chat (bounded).
// The user, 2026-11-19 : the list top left, the description above the chat -- from the action menu and from the main
// menu alike. false : no chat box shown (the caller places it).
bool card_on_chat(const Frame& f, const Metrics& k, unsigned iid, float tO, float A) {
    float ax, aw, top, bottom;
    if (!iid || !chat_anchor(ax, aw, top, bottom)) return false;
    card_use(0); card_load(f.dev, iid);
    if (!card_ready(iid)) return true;
    const float S = k.S, lo = snap(300.0f * S), hi = snap(480.0f * S);
    const float cw = aw < lo ? lo : aw > hi ? hi : snap(aw);
    float cx = snap(ax); if (cx + cw > f.screenW - 4.0f) cx = snap(f.screenW - 4.0f - cw); if (cx < 4.0f) cx = 4.0f;
    card_draw(f, k, cx, cw, snap(top - 4.0f * S), snap(bottom + 4.0f * S), tO, A, true);
    return true;
}
// ---- a LIST (native_list : spells, abilities, mounts, items), shared by the action menu and the main menu ----
// its rows and its size : a FIXED width (the widest row of the WHOLE list, cached per list -- scrolling never resizes it) ;
// EMPTY : the client opens it at its FULL size and writes "No spells available." in its first row
void list_build(const Frame& f, const Metrics& k, NativeList& L, RowSpec* lrows, char (*mps)[8], float& lw, float& lh) {
    const bool empty = L.n == 0;
    if (empty) L.rows = 1;
    const bool live = f.game && f.game->me.name[0];
    const int mpNow = live ? f.game->me.mp : -1, tpNow = live ? (int)f.game->me.tp : -1;
    for (int i = 0; i < L.rows && i < 32; ++i) {
        if (empty) {
            lrows[i].label = L.kind == 3 ? tr("No items available.", "Aucun objet disponible.") : L.kind == 2 ? tr("No mounts available.", "Aucune monture disponible.") : L.kind ? tr("No abilities available.", "Aucune aptitude disponible.") : tr("No spells available.", "Aucun sort disponible.");
            lrows[i].icon = IC_NONE; lrows[i].sub = false; lrows[i].off = false; lrows[i].center = true; break;
        }
        unsigned mp; bool ws;
        lrows[i].label = list_row_name(L.kind, L.id[i], mp, ws); lrows[i].icon = L.kind == 2 ? IC_NONE : L.kind ? (ws ? IC_SWORD : IC_BOLT) : IC_STAR; lrows[i].sub = false;
        lrows[i].tex = L.kind == 2 ? 0 : list_icon(f, L.kind, L.id[i]);
        if (L.kind == 3) { mp = 0; if (L.cnt[i]) { sprintf(mps[i], "%u", (unsigned)L.cnt[i]); lrows[i].extra = mps[i]; } lrows[i].icon = IC_BAG; }   // the count, right-aligned (not on the icon)
        if (!lrows[i].tex && L.kind < 2) list_fallback(f, L.kind, L.id[i], lrows[i]);   // spells / abilities only (a mount, an item : their own)
        // a mount : no picture exists per mount (Windower has none, the client's list none, res icon_id is 86 / 87
        // for all) -- the game's own "Mounted" status icon (252), the same on every row (user's choice 2026-11-17)
        if (L.kind == 2) status_cell(f.dev, 252, lrows[i]);
        // the spells whose cost is not a number (model/spell_cost.h) : ALL the MP left, or 20 % of the maximum
        const SpellCostKind ck = L.kind == 0 ? spell_cost_kind(L.id[i], mp) : SC_NUMBER;
        const bool allMp = ck == SC_ALL_MP, pctMp = ck == SC_PCT20;
        if (allMp) { lrows[i].extra = tr("All", "Tout"); mp = 0; }
        else if (pctMp) { lrows[i].extra = "20%"; mp = 0; }
        if (mp) { sprintf(mps[i], "%u", mp); lrows[i].extra = mps[i]; }
        lrows[i].off = (mp && mpNow >= 0 && (int)mp > mpNow) || (ws && tpNow >= 0 && tpNow < 1000)   // not usable right now : dimmed
                    || (allMp && mpNow == 0) || (pctMp && live && f.game->me.mpp < 20);
        lrows[i].mark = L.kind < 2 && L.held >= 0 && L.top + i == L.held;                                              // manual sort : the held one
    }
    block_size(f, k, lrows, L.rows, lw, lh);
    if (empty) {                                        // the full list's height : as many rows as a page shows
        const int pg = L.vis > 0 ? L.vis : 12;
        lh = snap((float)pg * k.capH + (float)(pg - 1) * k.gap + 2.0f * k.pad) + 2.0f * k.bi;
    }
    static unsigned sig = 0; static float wAll = 0.0f; static float atS = 0.0f;
    unsigned h = (2166136261u ^ (unsigned)L.allN) * 16777619u ^ (unsigned)L.kind;
    for (int i = 0; i < L.allN; ++i) h = (h ^ L.all[i]) * 16777619u;
    if (h != sig || atS != k.S) {
        sig = h; atS = k.S; wAll = 0.0f;
        RowSpec one[1]; char mp1[8];
        for (int i = 0; i < L.allN; ++i) {
            unsigned mp; bool ws;
            one[0].label = list_row_name(L.kind, L.all[i], mp, ws); one[0].icon = IC_STAR; one[0].sub = false; one[0].off = false;
            if (mp) { sprintf(mp1, "%u", mp); one[0].extra = mp1; } else one[0].extra = 0;
            float w1, h1; block_size(f, k, one, 1, w1, h1); if (w1 > wAll) wAll = w1;
        }
    }
    if (wAll > lw) lw = wAll;
}
// draw it at (lx, ly) : the rows, where this page sits in the whole list (a thin rail), its click area (hit, while
// open) and, for the ITEMS, the card of the item under the cursor BESIDE it
void list_paint(const Frame& f, const Metrics& k, Block& B, const NativeList& L, const RowSpec* lrows, bool open,
                float lx, float ly, float lw, float lh, float LA, float tL, float dt, float* hit, int jm) {
    const float S = k.S;
    const bool empty = L.n == 0;
    const int cur = empty ? 0 : ((L.cursor >= L.top && L.cursor < L.top + L.rows) ? L.cursor - L.top + 1 : 0);
    if (LA <= 0.0f) return;
    g_frameJoin = jm;                                   // menu_place's join mask (a theme kit merges the shared sides)
    block_draw(f, k, B, lrows, L.rows, cur, L.focused, lx, ly, lw, lh, LA, dt);
    if (L.n > L.rows) {
        const float tx = lx + lw - k.bi - 3.0f * S, ty = ly + k.bi + k.pad, th = lh - 2.0f * k.bi - 2.0f * k.pad;   // inside the frame border
        const float th2 = th * (float)L.rows / (float)L.n, ty2 = ty + th * (float)L.top / (float)L.n;
        rrect(f.dev, tx - 1.0f * S, ty, 2.0f * S, th, 1.0f * S, with_alpha(0x30FFFFFFu, LA), with_alpha(0x30FFFFFFu, LA));
        rrect(f.dev, tx - 1.0f * S, snap(ty2), 2.0f * S, snap(th2 < 6.0f * S ? 6.0f * S : th2), 1.0f * S, with_alpha(0xC0FFDA8Au, LA), with_alpha(0xC0FFDA8Au, LA));
    }
    if (open) { hit[0] = lx / f.screenW; hit[1] = ly / f.screenH; hit[2] = lw / f.screenW; hit[3] = lh / f.screenH; }
    const bool tradeQty = native_trade().open && native_trade().qtyUp && (ui_config().nuHide & NU_TRADE);   // the trade's quantity shows this card beside it
    if (L.kind == 3 && cur >= 1 && cur <= L.rows && !tradeQty && !card_on_chat(f, k, L.id[cur - 1], tL, LA)) {
        const unsigned iid = L.id[cur - 1];
        card_use(0); card_load(f.dev, iid);
        if (card_ready(iid)) {
            const float cw = snap(270.0f * S);
            float cx = snap(lx + lw + 3.0f * S);
            if (cx + cw > f.screenW - 4.0f) cx = snap(lx - 3.0f * S - cw);
            card_draw(f, k, cx, cw, ly, ly + lh, tL, LA, false);
        }
    }
}
}
void actmenu_draw(const Frame& f) {
    const UiConfig& C = ui_config();
    const NativeMenu& M = native_action_menu();
    const NativeEntryMenu& SM = native_sub_menu();
    g_hit[0][2] = g_hit[1][2] = 0.0f;                               // published again below, only while drawn and open
    if (!(C.nuHide & NU_ACT) || !f.font || f.screenH <= 0.0f || f.screenW <= 0.0f) {
        g_main.vis = g_subB.vis = 0.0f; g_main.wasOpen = g_subB.wasOpen = false; g_main.w = g_subB.w = 0.0f; return;
    }
    const unsigned now = GetTickCount();
    float dt = g_lastMs ? (float)(now - g_lastMs) / 1000.0f : 0.0f; g_lastMs = now;
    if (dt > 0.1f) dt = 0.1f;
    const Metrics k = metrics(f);
    const float S = k.S;

    // ======== the action menu ========
    static NativeMenu last;                       // the last menu seen : it fades out with its own rows
    if (M.open) last = M;
    // a spell / ability is being AIMED (the sub-target cursor is up) : the client hides its menus until you cast or back
    // out -- so do these (2026-11-16, "le menu depop quand la magie est en target ... puis repop")
    if (f.game && f.game->hasSubTarget) {
        block_ease(g_main, false, now, dt); block_ease(g_subB, false, now, dt); block_ease(g_listB, false, now, dt);
        g_main.hoverRow = g_subB.hoverRow = 0;
        return;
    }
    if (!block_ease(g_main, M.open, now, dt) || !last.n || last.uiW <= 0 || last.uiH <= 0) { g_subB.w = 0.0f; return; }
    const NativeMenu& m = last;
    RowSpec rows[16]; char nums[16][12];
    for (int i = 0; i < m.n && i < 16; ++i) {
        const Cmd* c = cmd_of(m.code[i]);
        if (c) { rows[i].label = tr(c->en, c->fr); rows[i].icon = c->icon; rows[i].sub = c->sub; }
        else {
            sprintf(nums[i], "#%d", m.code[i]); rows[i].label = nums[i]; rows[i].icon = IC_NONE; rows[i].sub = false;
            static unsigned char said[64] = { 0 };   // an UNKNOWN code : logged once, with the whole menu, so it gets named
            if (m.code[i] < 64 && !said[m.code[i]]) {
                said[m.code[i]] = 1;
                char all[96]; int o = 0; for (int q = 0; q < m.n && o < 80; ++q) o += sprintf(all + o, "%s%d", q ? "," : "", m.code[q]);
                windower::debug::log("ACTMENU unknown command code %d (row %d ; the menu's codes : %s) -- shown as '#%d'", m.code[i], i + 1, all, m.code[i]);
            }
        }
        rows[i].off = false;
    }
    float bw, bh; block_size(f, k, rows, m.n, bw, bh);
    const float tOpen = ease_out(((float)(now - g_main.openMs) / 1000.0f) / 0.08f);
    // place : like the client, ABOVE THE CHAT -- the client opens it on top of its log (on the battle log when that one
    // sits above the talk log) ; AioHUD's chat boxes replace that log, so the menu sits on the HIGHER of the two that
    // are shown, flush with its left edge. No chat box shown : the client's own place (its bottom-left corner).
    // 2026-11-17 : the chat can be anywhere -- the anchor box (stacked : the higher, side by side : the left), above it
    // when the menu fits, else below it (place_on_chat, hud_chat.cpp)
    const float sx = f.screenW / (float)m.uiW, sy = f.screenH / (float)m.uiH;   // the client's UI units -> backbuffer px
    float bx = 0.0f, by = 0.0f;
    if (!place_on_chat(bw, bh, snap(4.0f * S), f.screenW, f.screenH, bx, by)) {
        bx = snap((float)m.rx0 * sx); by = snap((float)m.ry1 * sy - bh);
        if (bx + bw > f.screenW - 4.0f) bx = snap(f.screenW - 4.0f - bw); if (bx < 4.0f) bx = 4.0f;
        if (by < 4.0f) by = 4.0f; if (by + bh > f.screenH - 4.0f) by = snap(f.screenH - 4.0f - bh);
    }
    // recorded only while it is drawn : the spell list up (in its place) or Abilities (in its place) hide it
    const bool mHidden = (M.open && native_list().open) || (SM.open && !strcmp(SM.name, "abiselec"));
    const int jMain = mHidden ? 0 : menu_place(f, __LINE__, bx, by, bw, bh);
    // BELOW the chat (no room above it) : what opens from the menu grows DOWN, never over the chat
    bool below = false;
    { float ax_, aw_, top_, bot_; if (chat_anchor(ax_, aw_, top_, bot_)) below = by >= bot_; }
    by = snap(by + (1.0f - tOpen) * 4.0f * S);
    // ======== the SPELL list (native_list) : the client lays it over both menus -- same left, bottom anchored ========
    {
        const NativeList& NL = native_list();
        static NativeList lastL;
        if (NL.open) lastL = NL;
        if (block_ease(g_listB, NL.open && M.open, now, dt)) {
            RowSpec lrows[32]; char mps[32][8]; float lw, lh;
            list_build(f, k, lastL, lrows, mps, lw, lh);
            if (lw < bw) lw = bw;
            const float tL = ease_out(((float)(now - g_listB.openMs) / 1000.0f) / 0.07f);
            float lx = bx, ly = snap((below ? by : by + bh - lh) + (1.0f - tL) * 3.0f * S);
            if (lastL.kind == 3) { lx = snap(16.0f * sx); ly = snap(48.0f * sy + (1.0f - tL) * 3.0f * S); }   // Items : top left, as the client
            // ... a TRADE's picker : beside the trade's boxes, as the client (200,48) -- top left it lay BEHIND them (2026-11-20)
            extern float g_trBox[4];
            const bool besideTrade = lastL.kind == 3 && (ui_config().nuHide & NU_TRADE) && native_trade().open && g_trBox[2] > 0.0f;
            if (besideTrade) { lx = snap(g_trBox[0] + g_trBox[2] + 6.0f * S); ly = snap(g_trBox[1] + (1.0f - tL) * 3.0f * S); }
            if (ly + lh > f.screenH - 4.0f) ly = snap(f.screenH - 4.0f - lh); if (ly < 4.0f) ly = 4.0f;
            // IN PLACE of the menus (hidden while it is up) : placed while open only -- fading out it lies over them, as before
            int jL = 0;
            if (NL.open && M.open) {
                float ux = lx, uy = besideTrade ? snap(g_trBox[1]) : lastL.kind == 3 ? snap(48.0f * sy) : snap(below ? by : by + bh - lh);
                if (uy + lh > f.screenH - 4.0f) uy = snap(f.screenH - 4.0f - lh); if (uy < 4.0f) uy = 4.0f;
                jL = menu_place_at(f, __LINE__, lx, ly, ux, uy, lw, lh);
            }
            list_paint(f, k, g_listB, lastL, lrows, NL.open, lx, ly, lw, lh, g_listB.vis * tL, tL, dt, g_hit[0], jL);
            if (NL.open) return;                                // while it is up, the menus under it are not drawn
        }
    }

    // Abilities (abiselec) does not open BESIDE : the client puts it IN PLACE of this menu (same left, same bottom, its
    // own height) -- this menu gives way while it is up (a quick cross-fade) ; Magic's categories open beside.
    static NativeEntryMenu lastS;
    if (SM.open) lastS = SM;
    const bool replacing = lastS.n && !strcmp(lastS.name, "abiselec");
    // the client swaps them at ONCE (trace 2026-11-16 : abiselec readable the same frame playermo loses the focus) :
    // while Abilities is open this menu is not drawn at all -- no cross-fade (its top lingered ~1 s, "il reste un effet")
    const float give = (replacing && SM.open) ? 1.0f : 0.0f;
    const float A = g_main.vis * tOpen * (m.focused ? 1.0f : 0.62f) * (1.0f - give);   // a submenu on top : steps back
    if (A > 0.0f) g_frameJoin = jMain;
    const int row = A > 0.0f ? block_draw(f, k, g_main, rows, m.n, m.cursor, m.focused, bx, by, bw, bh, A, dt) : 0;
    if (M.open && give < 0.5f) {
        g_hit[0][0] = bx / f.screenW; g_hit[0][1] = by / f.screenH; g_hit[0][2] = bw / f.screenW; g_hit[0][3] = bh / f.screenH;
        // the MOUSE : hover moves the client's cursor, a click validates -- the client's own events (native_ui)
        if (row && row != g_main.hoverRow && row != M.cursor) native_action_hover(row);
        g_main.hoverRow = row;
        if (row && f.mouse && f.mouse->clicked) native_action_pick(row);
    } else g_main.hoverRow = 0;

    // ======== its submenu : Magic's categories beside it, Abilities in its place ========
    if (!block_ease(g_subB, SM.open && M.open, now, dt) || !lastS.n) return;
    RowSpec srows[32]; char snums[32][12]; int scur = 0;     // 32 = NativeEntryMenu's capacity : every row passed on is filled
    for (int i = 0; i < lastS.n && i < 32; ++i) {
        const SubRow* r = sub_row(lastS.name, lastS.entry[i]);
        if (r) { srows[i].label = tr(r->en, r->fr); srows[i].icon = r->icon; srows[i].sub = r->sub; }
        else { sprintf(snums[i], "#%d", lastS.entry[i]); srows[i].label = snums[i]; srows[i].icon = IC_NONE; srows[i].sub = false; }
        srows[i].off = lastS.state[i] == 5;
        if (lastS.entry[i] == lastS.cursor) scur = i + 1;
    }
    // Chat : what the client shows on the right (native_chat_info) -- the Tell's name, each shell's pearl + name, Unity
    static NativeChatInfo lastC;
    if (native_chat_info().ok) lastC = native_chat_info();
    if (!strcmp(lastS.name, "chatctrl") && lastC.ok) {
        for (int i = 0; i < lastS.n && i < 32; ++i) {
            const int e = lastS.entry[i];
            if (e == 2) {                                         // Tell : the target's name (-1) or the history's
                if (lastC.tellSel == -1) srows[i].extra = (f.game && f.game->target.valid) ? f.game->target.name : 0;
                else if (lastC.tellSel != -3) srows[i].extra = lastC.tell;
                srows[i].cycle = true;                            // left / right picks another name : say so
            } else if (e == 4 || e == 5) {                        // Linkshell 1 / 2
                const int L = e - 4;
                if (lastC.lsItem[L]) { srows[i].extra = lastC.ls[L]; srows[i].pearl = lastC.lsItem[L]; srows[i].pearlArgb = lastC.lsArgb[L]; }
            } else if (e == 6 && lastC.unityId) srows[i].extra = lastC.unity;
        }
    }
    float sw, sh; block_size(f, k, srows, lastS.n, sw, sh);
    const float tS = ease_out(((float)(now - g_subB.openMs) / 1000.0f) / 0.07f);
    // beside the action menu, the SAME height (top and bottom level with it). More rows than the menu : it grows UP
    // (bottoms level, the default) or DOWN (tops level) -- Game UI -> Menus. Slides out from under it.
    float sbx, sby;
    if (replacing) {                                  // in place : same left, the BOTTOM anchored, its own height
        if (sw < bw) sw = bw;
        sbx = bx; sby = snap((below ? by : by + bh - sh) + (1.0f - tS) * 3.0f * S);
    } else {
        if (sh < bh) sh = bh;
        sbx = snap(bx + bw + 3.0f * S - (1.0f - tS) * 5.0f * S);
        sby = (C.muGrow == 1 || below) ? by : snap(by + bh - sh);
        if (sbx + sw > f.screenW - 4.0f) sbx = snap(bx - 3.0f * S - sw);        // no room on the right : on the left
    }
    if (sby + sh > f.screenH - 4.0f) sby = snap(f.screenH - 4.0f - sh); if (sby < 4.0f) sby = 4.0f;
    int jSub = 0;
    {   // its rest place (no slide) : glued beside the menu (theme kit) ; in its place for Abilities (the menu is hidden then)
        float ux = sbx, uy = sby;
        if (replacing) { uy = snap(below ? by : by + bh - sh); }
        else { ux = snap(bx + bw + 3.0f * S); if (ux + sw > f.screenW - 4.0f) ux = snap(bx - 3.0f * S - sw); }
        if (uy + sh > f.screenH - 4.0f) uy = snap(f.screenH - 4.0f - sh); if (uy < 4.0f) uy = 4.0f;
        jSub = menu_place_at(f, __LINE__, sbx, sby, ux, uy, sw, sh);
    }
    const float SA = g_subB.vis * tS * (lastS.focused ? 1.0f : 0.62f);          // its own list open on top : steps back
    if (SA <= 0.0f) return;
    g_frameJoin = jSub;
    const int srow = block_draw(f, k, g_subB, srows, lastS.n, scur, lastS.focused, sbx, sby, sw, sh, SA, dt);
    if (SM.open) {
        g_hit[1][0] = sbx / f.screenW; g_hit[1][1] = sby / f.screenH; g_hit[1][2] = sw / f.screenW; g_hit[1][3] = sh / f.screenH;
        const int ent = (srow >= 1 && srow <= lastS.n) ? lastS.entry[srow - 1] : 0;
        if (ent && srow != g_subB.hoverRow && ent != SM.cursor) native_sub_hover(ent);
        g_subB.hoverRow = srow;
        if (ent && f.mouse && f.mouse->clicked) native_sub_pick(ent);
    } else g_subB.hoverRow = 0;
}

// ======== the NPC windows (Game UI -> NU_NPC) : the dialogue and the answers, mirrored ========
// The client keeps rem4line / query (parked off-screen by native_ui) and their keys ; this draws what they show, on the
// chat (place_on_chat : above the anchor box when it fits, else below). Top to bottom : a BUBBLE (the dialogue lines,
// then the question), then the ANSWERS block (the action menu's capsules, gold selection and hand). A click on the
// bubble turns the page (the client's event 5) ; hover / click on an answer drives the client's own cursor.
namespace {
Block g_npcBubble, g_npcAns, g_npcCardB;
int   g_npcView = 0;                                   // the first answer shown (AioHUD shows up to 8 of them)

// ---- the ITEM CARD : the item a shop list's answer points at (the client's 'iteminfo', parked), drawn from its DAT
//      record (model/item_text.h) -- icon, name, Rare / Ex, level + jobs for equipment, the description ----
ItemText g_card; unsigned g_cardId = 0; bool g_cardOk = false;
u32 g_cardIcon = 0; unsigned g_cardIconId = 0;
float g_cardBottom = 0.0f;   // the last card drawn : its bottom edge (a second card stacks under it)
// a SECOND card (the compare view : the item hovered over the one worn), swapped with the first by card_use
struct CardAlt { ItemText t; unsigned id = 0; bool ok = false; u32 icon = 0; unsigned iconId = 0; };
CardAlt g_cardAlt; int g_cardCur = 0;
void card_use(int s) {
    if (s == g_cardCur) return;
    static ItemText tmp; tmp = g_card; g_card = g_cardAlt.t; g_cardAlt.t = tmp;
    unsigned u = g_cardId; g_cardId = g_cardAlt.id; g_cardAlt.id = u;
    bool b = g_cardOk; g_cardOk = g_cardAlt.ok; g_cardAlt.ok = b;
    u32 x = g_cardIcon; g_cardIcon = g_cardAlt.icon; g_cardAlt.icon = x;
    u = g_cardIconId; g_cardIconId = g_cardAlt.iconId; g_cardAlt.iconId = u;
    g_cardCur = s;
}
const char* const JOBS3[23] = { "", "WAR", "MNK", "WHM", "BLM", "RDM", "THF", "PLD", "DRK", "BST", "BRD", "RNG", "SAM", "NIN", "DRG",
                                "SMN", "BLU", "COR", "PUP", "DNC", "SCH", "GEO", "RUN" };
void card_load(u32 dev, unsigned id) {
    static DWORD lastTry = 0;   // a failed read is retried every 2 s, never cached for good (rule 10)
    const DWORD nowT = GetTickCount();
    if (id == g_cardId && g_cardOk) {
        // the TEXT is read ; its ICON is retried on its own (a device not ready must not leave this item iconless --
        // review 2026-11-17)
        if (g_cardIcon || nowT - lastTry < 2000u) return;
        lastTry = nowT;
        static u32 px2[32 * 32];
        if (decode_gear_icon_from_rom(id, px2)) { g_cardIcon = make_texture_argb_mip(dev, 32, 32, px2); g_cardIconId = id; }
        return;
    }
    if (id == g_cardId && nowT - lastTry < 2000u) return;
    lastTry = nowT;
    g_cardId = id; g_cardOk = false;
    static unsigned char rec[0x2BD];
    if (id && read_item_record_head(id, rec, sizeof(rec)) && item_text_parse(rec, sizeof(rec), id, g_card)) g_cardOk = true;
    if (g_cardIcon) { release_texture(g_cardIcon); g_cardIcon = 0; }
    g_cardIconId = 0;
    if (g_cardOk) {
        static u32 px[32 * 32];
        if (decode_gear_icon_from_rom(id, px)) { g_cardIcon = make_texture_argb_mip(dev, 32, 32, px); g_cardIconId = id; }
    }
    if (id) windower::debug::log("NATIVEUI NPC item card : id %u -> %s%s", id, g_cardOk ? g_card.name : "NOT READ", g_cardOk && !g_cardIcon ? " (no icon)" : "");
}
// greedy word wrap of `s` ('\n' kept) into at most `maxL` lines of width `w`
int wrap_lines(Font* fo, const char* s, float tz, float w, char out[][160], int maxL) {
    int n = 0; char cur[160]; int cl = 0; cur[0] = 0;
    const char* p = s;
    while (*p && n < maxL) {
        if (*p == '\n') { cur[cl] = 0; lstrcpynA(out[n++], cur, 160); cl = 0; cur[0] = 0; ++p; continue; }
        // a word ends at a space, or just AFTER a '/' (the jobs line "RDM/PLD/DRK/..." breaks there, as the client's)
        const char* e = p; while (*e && *e != ' ' && *e != '\n') { if (*e == '/') { ++e; break; } ++e; }
        char word[160]; int wl = (int)(e - p); if (wl > 150) wl = 150; memcpy(word, p, (size_t)wl); word[wl] = 0;
        const bool glued = cl && cur[cl - 1] == '/';                    // after a '/' : no space between
        char trial[320]; _snprintf(trial, sizeof(trial), cl && !glued ? "%s %s" : "%s%s", cur, word); trial[sizeof(trial) - 1] = 0;
        if (cl && fo->measure(trial, tz) > w) { cur[cl] = 0; lstrcpynA(out[n++], cur, 160); lstrcpynA(cur, word, 160); cl = (int)strlen(cur); }
        else { lstrcpynA(cur, trial, 160); cl = (int)strlen(cur); }
        p = e; while (*p == ' ') ++p;
    }
    if (cl && n < maxL) lstrcpynA(out[n++], cur, 160);
    return n;
}
// THE ITEM CARD (g_card / g_cardIcon, card_load) : the NPC answers' (above their stack) and the Items list's (beside
// it). above : placed above `top` (else below `bottom`) ; else its top at `top`, kept on screen. bw : its width.
bool card_ready(unsigned id) { return id && id == g_cardId && g_cardOk; }
// the card's animated height (per card slot) and its last place : card_fold draws it folding away once it is gone
struct CardAnim { float h = 0.0f, h0 = 0.0f, t = -9.0f, gt = 0.0f, fold = 0.0f, x = 0.0f, y = 0.0f, w = 0.0f, A = 0.0f, S = 1.0f; bool above = false, ghost = false; };
CardAnim g_cardAnim[2];
enum { CARD_KEY = -1300 };                                    // its menu_place key (glued to the box it describes, theme kit)
// once a frame, AFTER the screen's card_draw calls : a card shown a moment ago and not this frame FOLDS -- its box,
// empty, shrinking into the edge it hangs from and fading -- instead of vanishing
void card_fold(const Frame& f) {
    CardAnim& an = g_cardAnim[0];
    if (an.t < -1.0f || an.t == f.t) { an.ghost = false; return; }      // never shown, or drawn this frame
    if (!an.ghost) { an.ghost = true; an.gt = an.t; an.h0 = an.h; }
    float dt = f.t - an.gt;
    if (dt < 0.0f || dt > 0.25f) { an.t = -9.0f; an.ghost = false; return; }   // the screen was away : no stale fold
    if (dt > 0.1f) dt = 0.1f;
    an.gt = f.t;
    const float bottom = an.y + an.h;
    an.h += (an.fold - an.h) * (1.0f - expf(-dt * 18.0f));
    const float a = an.A * (an.h0 - an.fold > 1.0f ? (an.h - an.fold) / (an.h0 - an.fold) : 0.0f);   // fading from its first frame
    if (an.h - an.fold < 1.0f || a <= 0.01f) { an.t = -9.0f; an.ghost = false; return; }
    const float h = snap(an.h);
    float x = an.x, y = an.above ? snap(bottom - h) : an.y;
    an.y = y;
    g_frameJoin = menu_place(f, CARD_KEY, x, y, an.w, h, nullptr, an.fold);   // folding : its final (fold) height
    draw_themed_box(f.dev, f.skin, x, y, an.w, h, ui_config().muBox, a, an.S);
}
void card_draw(const Frame& f, const Metrics& k, float bx, float bw, float top, float bottom, float tO, float A, bool above) {
        const float S = k.S; const u32 dev = f.dev; Font* fo = f.font; const UiConfig& C = ui_config();
        // THE CLIENT'S LAYOUT (its own card, screenshot 2026-11-17) : the icon top-left ; ONE text column right of it,
        // white -- the title (the log name, "Ark tachi"), the kind ("(Great Katana)All Races"), the description, then
        // "Lv.1 All Jobs" ; the enchantment "<1/1 0:30/[1:00:00, 0:30]>" right-aligned in light blue ; round Rare / Ex
        // badges in the top-right corner. AioHUD's box and font.
        const float ctz = snap(k.tz * 1.0f), clH = snap(ctz * 1.3f), cpad = snap(8.0f * S) + k.bi, icon = snap(32.0f * S);   // cpad : + the frame border (the width is the caller's : it lines up with its neighbours)
        float colX = snap(bx + cpad + icon + 6.0f * S); const float tw = bx + bw - cpad - colX;   // colX again once placed
        char title[96]; item_title(g_card, title, sizeof(title));
        char kind[96]; item_kind_line(g_card, kind, sizeof(kind));
        char ench[64]; item_enchant_line(g_card, ench, sizeof(ench));
        char lv[200] = "";
        if (g_card.level > 0) {
            int o = _snprintf(lv, sizeof(lv), "Lv.%d ", g_card.level);
            const unsigned all = 0x7FFFFEu;
            if ((g_card.jobs & all) == all) _snprintf(lv + o, sizeof(lv) - o, "All Jobs");
            else { const int o0 = o; for (int j = 1; j <= 22 && o < (int)sizeof(lv) - 6; ++j) if (g_card.jobs & (1u << j)) o += _snprintf(lv + o, sizeof(lv) - o, "%s%s", o > o0 ? "/" : "", JOBS3[j]); }
            lv[sizeof(lv) - 1] = 0;
        }
        // every line of the column, in order (the description wrapped to the column) ; then the client's page 2, when
        // its own card shows this item : the augments (their own colour), the "<...>" line, "<Item Level:n>"
        const ItemExtra* X = native_item_extra(g_cardId);
        char lines[30][160]; int nl = 0, augFrom = 99;
        nl += wrap_lines(fo, title, ctz, tw, lines + nl, 2);                    // what is longer than the box wraps, as the client
        if (kind[0]) nl += wrap_lines(fo, kind, ctz, tw, lines + nl, 2);
        if (g_card.desc[0]) nl += wrap_lines(fo, g_card.desc, ctz, tw, lines + nl, 14);
        if (lv[0] && nl < 28) nl += wrap_lines(fo, lv, ctz, tw, lines + nl, 2);
        if (X && X->aug[0] && nl < 24) { augFrom = nl; nl += wrap_lines(fo, X->aug, ctz, tw, lines + nl, 30 - nl > 6 ? 6 : 30 - nl); }
        if (X && X->line[0]) lstrcpynA(ench, X->line, sizeof(ench));             // the client's "<...>" (charges, or rank / path / RP)
        const char* ilv = X && X->ilvl[0] ? X->ilvl : 0;
        const float colH = (float)nl * clH, encH = (ench[0] ? clH : 0.0f) + (ilv ? clH : 0.0f);
        const float chT = snap(cpad + (colH > icon ? colH : icon) + encH + cpad);
        // THE HEIGHT, ANIMATED (the user 2026-11-20 : "rendre les transitions smooth") : it eases to the new item's height ;
        // shown again after a moment away, it UNFOLDS from a thin strip (or picks up its fold mid-way). The content waits
        // for the box : no text past its edge
        CardAnim& an = g_cardAnim[g_cardCur & 1];
        {   an.fold = snap(2.0f * k.bi + 6.0f * S);
            float dt = f.t - (an.ghost ? an.gt : an.t);
            if (an.t < -1.0f || dt < 0.0f || dt > 0.25f) { an.h = an.fold; dt = 0.0f; }
            if (dt > 0.1f) dt = 0.1f;
            an.h += (chT - an.h) * (1.0f - expf(-dt * 16.0f));
            if (fabsf(chT - an.h) < 0.75f) an.h = chT;
            an.t = f.t; an.ghost = false; }
        const float ch = snap(an.h);
        const float contentA = ch >= chT - 1.0f ? 1.0f : 0.0f;          // only once it is (all but) there : the text never passes its edge
        float cy = above ? snap(top - snap(4.0f * S) - ch) : top;
        if (above && cy < 4.0f) cy = snap(bottom + snap(4.0f * S));
        if (!above && cy + ch > f.screenH - 4.0f) cy = snap(f.screenH - 4.0f - ch);
        // glued to the box it describes (theme kit) ; not the compare card : it hangs under the first, past its gold arrow
        const int jm = g_cardCur == 0 ? menu_place(f, CARD_KEY, bx, cy, bw, ch, nullptr, chT) : 0;   // chT : where it is going
        colX = snap(bx + cpad + icon + 6.0f * S);
        cy = snap(cy + (1.0f - tO) * 3.0f * S);
        g_cardBottom = cy + ch;
        g_frameJoin = jm;
        draw_themed_box(dev, f.skin, bx, cy, bw, ch, C.muBox, A, S);
        an.x = bx; an.y = cy; an.w = bw; an.A = A; an.S = S; an.above = above;
        A *= contentA;                                                    // the content : once the box has (nearly) its size
        if (A <= 0.0f) return;
        const float ix = snap(bx + cpad), iy = snap(cy + cpad);
        if (g_cardIcon) {
            dTexQuadState(dev, g_cardIcon);
            tquad(dev, ix, iy, icon, icon, 0.0f, 1.0f, 0.0f, 1.0f, with_alpha(0xFFFFFFFFu, A), with_alpha(0xFFFFFFFFu, A));
            dSetTex(dev, 0, 0); cs(dev);                                  // back to the colour state : the badges' rrects follow
        }
        // the badges, in the top-right corner -- Rare (blue) then Ex (green), left to right. PILLS sized to their word at
        // a readable size (2026-11-17 : the round ones of the client's size were "pas tres lisibles")
        const char* BADGE[2] = { item_rare(g_card) ? "Rare" : 0, item_ex(g_card) ? "Ex" : 0 };
        const u32 BCOL[2] = { 0xFF2F5FB8u, 0xFF2E8A46u };
        const float btz = snap(ctz * 0.85f), bh2 = snap(btz * 1.45f), byB = snap(cy + cpad - 1.0f * S);
        float bxs[2] = { 0, 0 }, bws[2] = { 0, 0 };
        { float right = snap(bx + bw - cpad);
          for (int b = 1; b >= 0; --b) if (BADGE[b]) { bws[b] = snap(fo->measure(BADGE[b], btz) + 12.0f * S); bxs[b] = snap(right - bws[b]); right = snap(bxs[b] - 4.0f * S); } }
        for (int b = 0; b < 2; ++b) if (BADGE[b]) {
            rrect(dev, bxs[b], byB, bws[b], bh2, bh2 * 0.5f, with_alpha(BCOL[b], A), with_alpha(mix(BCOL[b], 0xFF000000u, 0.35f), A));
            rrect_topcaps(dev, bxs[b] + 1.0f, byB + 1.0f, bws[b] - 2.0f, snap(bh2 * 0.45f), bh2 * 0.5f - 1.0f, with_alpha(0x38FFFFFFu, A), with_alpha(0x00FFFFFFu, A));
            rrect_stroke(dev, bxs[b], byB, bws[b], bh2, bh2 * 0.5f, with_alpha(0xC0E8F0FFu, A), 1.0f);
        }
        fo->begin(dev);
        const u32 sh = with_alpha(0xC8000000u, A), white = with_alpha(0xFFF4F6FAu, A);
        for (int b = 0; b < 2; ++b) if (BADGE[b])
            fo->draw_lc(dev, snap(bxs[b] + (bws[b] - fo->measure(BADGE[b], btz)) * 0.5f), snap(byB + bh2 * 0.5f), BADGE[b], btz, with_alpha(0xFFFFFFFFu, A), sh, k.ow);
        for (int i = 0; i < nl; ++i) fo->draw_lc(dev, colX, snap(cy + cpad + (float)i * clH + clH * 0.5f), lines[i], ctz, i >= augFrom ? with_alpha(0xFFA8E8B4u, A) : white, sh, k.ow);
        float ry = snap(cy + ch - cpad - clH * 0.5f) - (ilv ? clH : 0.0f);
        if (ench[0]) {
            const float ew = fo->measure(ench, ctz);
            fo->draw_lc(dev, snap(bx + bw - cpad - ew), ry, ench, ctz, with_alpha(0xFF9CC8FFu, A), sh, k.ow);
        }
        if (ilv) { ry = snap(cy + ch - cpad - clH * 0.5f); fo->draw_lc(dev, snap(bx + bw - cpad - fo->measure(ilv, ctz)), ry, ilv, ctz, white, sh, k.ow); }
    }
}

void npc_card_forget()  {   // device lost : the handles are gone with it (the list icons' too -- they reload on demand)
    g_cardIcon = 0; g_cardIconId = 0; g_cardId = 0;
    g_cardAlt.icon = 0; g_cardAlt.iconId = 0; g_cardAlt.id = 0;      // the second card's handle is gone too
    for (int i = 0; i < 64; ++i) g_icons[i] = IconSlot();
    for (int i = 0; i < 4; ++i) g_pearl[i] = PearlTex();
    for (int i = 0; i < 96; ++i) g_lbl[i] = LabelTex();                 // the menus' image labels
    g_elemTex = g_jobTex = g_wpnTex = 0; g_elemR = TexRetry(); g_jobR = TexRetry(); g_wpnR = TexRetry();
    g_gilTex = 0; g_gilR = TexRetry();
    g_netTex = 0; g_netR = TexRetry();
}
void npc_card_dispose() {
    if (g_cardIcon) release_texture(g_cardIcon);
    if (g_cardAlt.icon) release_texture(g_cardAlt.icon);
    for (int i = 0; i < 64; ++i) if (g_icons[i].tex) release_texture(g_icons[i].tex);
    if (g_elemTex) release_texture(g_elemTex);
    if (g_jobTex) release_texture(g_jobTex);
    if (g_wpnTex) release_texture(g_wpnTex);
    if (g_gilTex) release_texture(g_gilTex);
    if (g_netTex) { release_texture(g_netTex); g_netTex = 0; g_netR = TexRetry(); }
    if (g_fpsFile[0]) remove(g_fpsFile);   // at unload : Windower's FPS counter comes back at once
    for (int i = 0; i < 4; ++i) if (g_pearl[i].tex) release_texture(g_pearl[i].tex);
    for (int i = 0; i < 96; ++i) if (g_lbl[i].tex) release_texture(g_lbl[i].tex);
    npc_card_forget();
}

void npc_draw(const Frame& f) {
    const UiConfig& C = ui_config();
    const NativeNpc& N = native_npc();
    g_hit[2][2] = g_hit[3][2] = 0.0f;
    if (!N.query) InterlockedExchange(&g_npcWheel, 0);    // a notch is for THESE answers, never kept for the next
    native_npc_box(0.0f, 0.0f, 0.0f, 0.0f);               // published again below while the answers are drawn
    if (!(C.nuHide & NU_NPC) || !f.font || f.screenH <= 0.0f || f.screenW <= 0.0f) {
        g_npcBubble.vis = g_npcAns.vis = 0.0f; g_npcBubble.wasOpen = g_npcAns.wasOpen = false; return;
    }
    static unsigned lastMs = 0;
    const unsigned now = GetTickCount();
    float dt = lastMs ? (float)(now - lastMs) / 1000.0f : 0.0f; lastMs = now; if (dt > 0.1f) dt = 0.1f;
    const Metrics k = metrics(f); const float S = k.S; const u32 dev = f.dev; Font* fo = f.font;

    static NativeNpc last;                             // fades out with its own text
    if (N.talk || N.query) last = N;
    // the client's own windows are back (an unreadable one) : AioHUD draws nothing -- the text is never shown twice
    const bool bubbleOn = !N.fail && (N.talk || N.query), ansOn = !N.fail && N.query && N.n > 0;
    const bool bubbleVis = block_ease(g_npcBubble, bubbleOn, now, dt), ansVis = block_ease(g_npcAns, ansOn, now, dt);
    if (!bubbleVis && !ansVis) return;

    // ---- the bubble's lines : the dialogue (when shown), then the question ----
    const char* L[6]; bool lead[6], q[6]; int nL = 0;
    if (last.talk) for (int i = 0; i < last.nLines && nL < 5; ++i) { L[nL] = last.line[i]; lead[nL] = last.lead[i]; q[nL] = false; ++nL; }
    if (last.query && last.question[0] && nL < 6) { L[nL] = last.question; lead[nL] = false; q[nL] = true; ++nL; }
    const float tz = snap(k.tz * 1.08f), lineH = snap(tz * 1.45f), pad = snap(9.0f * S) + k.bi;   // the bubble's edge -> text (+ the frame border)
    float wText = 0.0f;
    for (int i = 0; i < nL; ++i) { const float w = fo->measure(L[i], tz); if (w > wText) wText = w; }
    // the answers : the CLIENT's page -- its 3 rows from its own top (2026-11-17 : "la liste contient moins d'items dans
    // le menu du jeu") ; the client scrolls it, the rail says where it sits
    const int vis = last.n < 3 ? last.n : 3;
    g_npcView = last.top;
    if (g_npcView > last.n - vis) g_npcView = last.n - vis; if (g_npcView < 0) g_npcView = 0;
    RowSpec rows[3]; static char marked[3][80];
    for (int i = 0; i < vis; ++i) {
        const unsigned short* c = last.ctl[g_npcView + i];
        rows[i].label = last.choice[g_npcView + i]; rows[i].icon = IC_NONE; rows[i].sub = false; rows[i].off = false;
        rows[i].tint = (c[0] == 0xFEFE || (c[1] == 0xFEFE && (c[0] == 0xB9 || c[0] == 0x9C))) ? 0xFF9CE27Au : 0xFFF4F6FAu;   // the client : an answer carrying FEFE (live 2026-11-17 : every ITEM of a shop list, FEFE FEFF) green ; "Back." / plain ones white
        // a reward list's mark (the client : "(star)" or "(_)" , taken or not -- 2026-11-21) : 0xB9 the star, 0x9C the dash
        // in the text, as the client writes it ("une vraie etoile") : U+2605 from the wide-glyph face
        if (c[0] == 0xB9 || c[0] == 0x9C) { _snprintf(marked[i], sizeof(marked[i]), "%s %s", c[0] == 0xB9 ? "(\xE2\x98\x85)" : "(\xE2\x80\x95)", last.choice[g_npcView + i]); marked[i][79] = 0; rows[i].label = marked[i]; }
    }
    float aw = 0.0f, ah = 0.0f;
    if (last.query && vis > 0) block_size(f, k, rows, vis, aw, ah);
    // one width for both : the widest of the text, the answers, and a floor ; the screen's 70 % at most
    float bw = snap(wText + 2.0f * pad + 14.0f * S); if (aw > bw) bw = aw;
    if (bw < snap(320.0f * S)) bw = snap(320.0f * S); if (bw > f.screenW * 0.7f) bw = snap(f.screenW * 0.7f);
    const float bh = nL ? snap((float)nL * lineH + 2.0f * pad) : 0.0f;
    const float gap = snap(3.0f * S), H = bh + (ah > 0.0f ? (bh > 0.0f ? gap : 0.0f) + ah : 0.0f);
    float bx = 0.0f, by = 0.0f;
    if (!place_on_chat(bw, H, snap(4.0f * S), f.screenW, f.screenH, bx, by)) { bx = snap(20.0f * S); by = snap(f.screenH * 0.72f - H); }
    // the stack's top box (the bubble, else the answers) placed first : the card glues above it, the answers under it
    const int jBub = menu_place(f, __LINE__, bx, by, bw, bh > 0.0f ? bh : ah);
    // ---- the item card : the item the answer under the cursor points at, ABOVE the stack (else below it) ----
    if (N.query) card_load(dev, N.itemId); else if (!last.query) g_cardId = 0;
    const bool cardOn = !N.fail && N.query && N.itemId && g_cardOk && g_cardId == N.itemId;
    if (block_ease(g_npcCardB, cardOn, now, dt) && g_cardOk) {
        const float tO = ease_out(((float)(now - g_npcCardB.openMs) / 1000.0f) / 0.08f);
        card_draw(f, k, bx, bw, by, by + H, tO, g_npcCardB.vis * tO, true);
    }
    // the client's windows that open WITH the answers (a shop list's item description) : parked while AioHUD draws the
    // card, else brought back above this box (native_ui)
    if (N.query && !N.fail) native_npc_box(bx / f.screenW, by / f.screenH, bw / f.screenW, H / f.screenH, cardOn);

    // ---- the bubble ----
    if (bubbleVis && bh > 0.0f) {
        const float tO = ease_out(((float)(now - g_npcBubble.openMs) / 1000.0f) / 0.08f), A = g_npcBubble.vis * tO;
        const float y0 = snap(by + (1.0f - tO) * 3.0f * S);
        g_frameJoin = jBub;
        draw_themed_box(dev, f.skin, bx, y0, bw, bh, C.muBox, A, S);
        const bool waits = last.talk && !last.query;   // a page the player turns : a small bobbing chevron, bottom right
        if (waits) {
            const float cx = bx + bw - pad - 4.0f * S, cy = y0 + bh - pad * 0.9f + sinf(f.t * 5.0f) * 1.5f * S, c = 3.5f * S;
            tri(dev, cx - c, cy - c * 0.6f, cx + c, cy - c * 0.6f, cx, cy + c * 0.7f, with_alpha(0xE0FFDA8Au, A));
        }
        fo->begin(dev);
        for (int i = 0; i < nL; ++i) {
            const float x = snap(bx + pad), yc = snap(y0 + pad + (float)i * lineH + lineH * 0.5f);
            const u32 sh = with_alpha(0xC8000000u, A);
            if (q[i]) { fo->draw_lc(dev, x, yc, L[i], tz, with_alpha(0xFFF4F6FAu, A), sh, k.ow); continue; }   // the question : white, as the client
            const char* colon = lead[i] ? strstr(L[i], " : ") : 0;
            if (colon && colon - L[i] < 40) {                 // "Name : text" -> the speaker in gold
                char nm[48]; const int n = (int)(colon - L[i]); memcpy(nm, L[i], (size_t)n); nm[n] = 0;
                fo->draw_lc(dev, x, yc, nm, tz, with_alpha(0xFFFFDA8Au, A), sh, k.ow);
                fo->draw_lc(dev, snap(x + fo->measure(nm, tz)), yc, colon, tz, with_alpha(0xFFE8EEF6u, A), sh, k.ow);
            } else fo->draw_lc(dev, x, yc, L[i], tz, with_alpha(0xFFE8EEF6u, A), sh, k.ow);
        }
        if (bubbleOn) {
            g_hit[2][0] = bx / f.screenW; g_hit[2][1] = y0 / f.screenH; g_hit[2][2] = bw / f.screenW; g_hit[2][3] = bh / f.screenH;
            const MouseState* ms = f.mouse;
            if (waits && ms && ms->focused && ms->clicked && ms->x >= bx && ms->x < bx + bw && ms->y >= y0 && ms->y < y0 + bh) native_npc_next();
        }
    }

    // ---- the answers ----
    if (ansVis && ah > 0.0f) {
        const float tO = ease_out(((float)(now - g_npcAns.openMs) / 1000.0f) / 0.08f), A = g_npcAns.vis * tO;
        float ax = bx, ay = by + (bh > 0.0f ? bh + gap : 0.0f);
        const int jAns = bh > 0.0f ? menu_place(f, __LINE__, ax, ay, bw, ah) : jBub;   // under the bubble (glued, theme kit) ; alone : placed above
        ay = snap(ay + (1.0f - tO) * 3.0f * S);
        const int cur = (last.sel >= g_npcView && last.sel < g_npcView + vis) ? last.sel - g_npcView + 1 : 0;
        g_frameJoin = jAns;
        const int row = block_draw(f, k, g_npcAns, rows, vis, cur, last.queryFocused, ax, ay, bw, ah, A, dt);
        if (last.n > vis) {                              // where this page sits in all the answers : a thin rail
            const float tx = ax + bw - k.bi - 3.0f * S, ty = ay + k.bi + k.pad, th = ah - 2.0f * k.bi - 2.0f * k.pad;   // inside the frame border
            const float th2 = th * (float)vis / (float)last.n, ty2 = ty + th * (float)g_npcView / (float)last.n;
            rrect(dev, tx - 1.0f * S, ty, 2.0f * S, th, 1.0f * S, with_alpha(0x30FFFFFFu, A), with_alpha(0x30FFFFFFu, A));
            rrect(dev, tx - 1.0f * S, snap(ty2), 2.0f * S, snap(th2 < 6.0f * S ? 6.0f * S : th2), 1.0f * S, with_alpha(0xC0FFDA8Au, A), with_alpha(0xC0FFDA8Au, A));
        }
        if (ansOn) {                                     // the live answers only (not while they fade, not in the fallback)
            g_hit[3][0] = ax / f.screenW; g_hit[3][1] = ay / f.screenH; g_hit[3][2] = bw / f.screenW; g_hit[3][3] = ah / f.screenH;
            // the WHEEL over the answers : one choice per notch (up = the previous), the client scrolling its page
            const long wh = InterlockedExchange(&g_npcWheel, 0);
            if (wh && N.queryFocused) {
                int to = N.sel - (int)wh; if (to < 0) to = 0; if (to > N.n - 1) to = N.n - 1;
                if (to != N.sel) native_npc_hover(to);
            }
            const int idx = row ? g_npcView + row - 1 : -1;
            if (row && row != g_npcAns.hoverRow && idx != N.sel) native_npc_hover(idx);
            g_npcAns.hoverRow = row;
            if (idx >= 0 && f.mouse && f.mouse->clicked) native_npc_pick(idx);
        } else { g_npcAns.hoverRow = 0; InterlockedExchange(&g_npcWheel, 0); }   // no notch saved for the next question
    }
}

// ======== the auto-translate picker (Tab in the input line), mirrored on AioHUD's input field ========
// The client opens `fep` at its own input line's caret ; that line docked in AioHUD's chat box is parked, and the
// picker with it (2026-11-17 : "je n'ai pas de menu"). Its page (7 rows, the client's own paging), drawn ON the
// caret, above the field : everything white as the client, a category with an arrow (validating lists its phrases),
// the greyed part (a name's origin) on the right ; the counter "3/44" on top. Keys stay the
// client's (Tab / Shift+Tab / Enter / Escape) ; the mouse hovers / picks, the wheel steps.
Block g_fepB;
void fep_draw(const Frame& f) {
    const NativeFep& F = native_fep();
    g_hit[4][2] = 0.0f;
    static unsigned lastMs = 0;
    const unsigned now = GetTickCount();
    float dt = lastMs ? (float)(now - lastMs) / 1000.0f : 0.0f; lastMs = now; if (dt > 0.1f) dt = 0.1f;
    float ix = 0, iy = 0, iw = 0, ih = 0, cx = 0, wx = 0;
    const bool field = f.font && f.screenW > 0.0f && f.screenH > 0.0f && chat_input_rect(ix, iy, iw, ih, cx, wx);
    const bool on = field && F.open && !F.fail && F.rows > 0;
    static NativeFep last;
    if (on) { last = F; native_fep_drawn(); }
    else InterlockedExchange(&g_fepWheel, 0);
    // STILL while it is open (2026-11-17 : "il bouge en fonction de la longueur du choix") : the client writes the
    // selected phrase into the line, the caret runs with it -> the x of the caret AT OPENING, and a width that only grows
    // (a category validated closes and reopens it in a frame or two : the same opening, not a new one)
    static bool wasOn = false; static float anchorX = 0.0f, keepW = 0.0f; static unsigned onMs = 0;
    if (on && !wasOn && now - onMs > 300u) { anchorX = wx; keepW = 0.0f; }   // the caret before it opened
    if (on) onMs = now;
    wasOn = on;
    if (!block_ease(g_fepB, on, now, dt) || !field) return;
    const Metrics k = metrics(f); const float S = k.S; Font* fo = f.font; const u32 dev = f.dev;

    RowSpec rows[7]; const int n = last.rows;
    for (int i = 0; i < n; ++i) {
        rows[i].label = last.row[i]; rows[i].icon = IC_NONE; rows[i].off = false;
        rows[i].sub = last.cat[i];
        rows[i].tint = 0xFFF4F6FAu;                    // white, as the client (2026-11-17) ; a category's arrow tells it apart
        rows[i].extra = last.sub[i][0] ? last.sub[i] : 0;
    }
    float bw = 0.0f, bh = 0.0f; block_size(f, k, rows, n, bw, bh);
    char cnt[64];
    if (last.prefix[0]) _snprintf(cnt, sizeof(cnt), "%s  %d/%d", last.prefix, last.sel + 1, last.count);
    else _snprintf(cnt, sizeof(cnt), "%d/%d", last.sel + 1, last.count);
    cnt[sizeof(cnt) - 1] = 0;
    const float cz = snap(k.tz * 0.85f), lh = snap(cz * 1.5f);
    { const float need = snap(fo->measure(cnt, cz) + 2.0f * k.pad + 8.0f * S); if (bw < need) bw = need; }
    if (bw < snap(170.0f * S)) bw = snap(170.0f * S);
    if (bw < keepW) bw = keepW; else keepW = bw;
    // on the caret, its text column level with it ; inside the field, inside the screen
    float bx = snap(anchorX + 2.0f * S);                 // right after the caret (2026-11-17)
    if (bx + bw > ix + iw) bx = snap(ix + iw - bw); if (bx < ix) bx = snap(ix);
    if (bx + bw > f.screenW) bx = snap(f.screenW - bw); if (bx < 0.0f) bx = 0.0f;
    float by = snap(iy - snap(4.0f * S) - bh);
    if (by - lh < 0.0f) by = snap(iy + ih + snap(4.0f * S) + lh);    // no room above : below the field
    const float tO = ease_out(((float)(now - g_fepB.openMs) / 1000.0f) / 0.08f), A = g_fepB.vis * tO;
    by = snap(by + (1.0f - tO) * 3.0f * S);

    const int cur = last.sel - last.top + 1;
    const int row = block_draw(f, k, g_fepB, rows, n, cur, last.focused, bx, by, bw, bh, A, dt);
    if (last.count > 7) {                                // where this page sits in the whole list : a thin rail
        const float tx = bx + bw - k.bi - 3.0f * S, ty = by + k.bi + k.pad, th = bh - 2.0f * k.bi - 2.0f * k.pad;   // inside the frame border
        const float th2 = th * (float)n / (float)last.count, ty2 = ty + th * (float)last.top / (float)last.count;
        dColorQuadState(dev);
        rrect(dev, tx - 1.0f * S, ty, 2.0f * S, th, 1.0f * S, with_alpha(0x30FFFFFFu, A), with_alpha(0x30FFFFFFu, A));
        rrect(dev, tx - 1.0f * S, snap(ty2), 2.0f * S, snap(th2 < 6.0f * S ? 6.0f * S : th2), 1.0f * S, with_alpha(0xC0FFDA8Au, A), with_alpha(0xC0FFDA8Au, A));
    }
    // the counter (and the filter typed before Tab), on the block's top-right, as the client's header
    fo->begin(dev);
    { const float w = fo->measure(cnt, cz);
      fo->draw_lc(dev, snap(bx + bw - k.bi - k.pad - w), snap(by - lh * 0.5f), cnt, cz, with_alpha(0xFFB8C4D4u, A), with_alpha(0xC8000000u, A), k.ow); }

    if (!on) { g_fepB.hoverRow = 0; return; }
    g_hit[4][0] = bx / f.screenW; g_hit[4][1] = (by - lh) / f.screenH; g_hit[4][2] = bw / f.screenW; g_hit[4][3] = (bh + lh) / f.screenH;
    const long wh = InterlockedExchange(&g_fepWheel, 0);       // a notch up = the previous row (Shift+Tab), down = the next (Tab)
    if (wh && F.focused) { const int st = wh > 0 ? -1 : 1; for (int g = 0, m = wh > 0 ? (int)wh : -(int)wh; g < m && g < 7; ++g) native_fep_step(st); }
    const int idx = row ? last.top + row - 1 : -1;
    if (row && row != g_fepB.hoverRow && idx != F.sel) native_fep_hover(idx);
    g_fepB.hoverRow = row;
    if (idx >= 0 && f.mouse && f.mouse->clicked) native_fep_pick(idx);
}

// ======== the Game UI SAMPLE (config preview + Help) : the redrawn action menu with Magic's submenu open, on fixed
// rows, through the SAME block_draw as in play -- size, frame / theme, icons, and how a taller submenu grows all show.
// Its own Blocks : the live menus' easing is never touched. s = the screen-height scale ; Game UI's Menu size on top.
// draw = false only measures (outW / outH in px).
void nui_sample(const Frame& f, float cx, float cy, float s, bool draw, float* outW, float* outH) {
    if (!f.font) { if (outW) *outW = 0.0f; if (outH) *outH = 0.0f; return; }
    float sc = ui_config().muScale; if (!(sc >= 0.6f && sc <= 1.6f)) sc = 1.0f;
    const Metrics k = metrics_at(s * sc); const float S = k.S;
    static const unsigned short CODES[6] = { 1, 8, 7, 9, 19, 10 };   // Attack / Magic / Abilities / Items / Trade / Check
    RowSpec rows[6];
    for (int i = 0; i < 6; ++i) { const Cmd* c = cmd_of(CODES[i]); rows[i].label = c ? tr(c->en, c->fr) : "?"; rows[i].icon = c ? c->icon : IC_NONE; rows[i].sub = c && c->sub; rows[i].off = false; }
    // beside it : a spell LIST (White Magic), so the spell pictures show ; one row dimmed (not enough MP)
    static const unsigned SAMPLE_SPELLS[7] = { 1, 2, 3, 43, 48, 57, 108 };   // Cure / Cure II / Cure III / Protect / Shell / Haste / Regen
    RowSpec srows[7]; char mps[7][8];
    for (int i = 0; i < 7; ++i) {
        unsigned mp; bool ws; srows[i].label = list_row_name(0, SAMPLE_SPELLS[i], mp, ws); srows[i].icon = IC_STAR; srows[i].sub = false; srows[i].off = i == 2;
        if (mp) { sprintf(mps[i], "%u", mp); srows[i].extra = mps[i]; }
        if (draw) srows[i].tex = list_icon(f, 0, SAMPLE_SPELLS[i]);
    }
    float bw, bh, sw, sh; block_size(f, k, rows, 6, bw, bh); block_size(f, k, srows, 7, sw, sh);
    if (sh < bh) sh = bh;
    const float gap = snap(3.0f * S), W = bw + gap + sw, H = sh > bh ? sh : bh;
    if (outW) *outW = W;
    if (outH) *outH = H;
    if (!draw) return;
    static Block pvMain, pvSub; static unsigned last = 0;
    const unsigned now = GetTickCount(); float dt = last ? (float)(now - last) / 1000.0f : 0.0f; last = now; if (dt > 0.1f) dt = 0.1f;
    const float x0 = snap(cx - W * 0.5f), top = snap(cy - H * 0.5f);
    const bool down = ui_config().muGrow == 1;                  // the taller submenu : tops level (down) or bottoms level (up)
    const float my = down ? top : snap(top + H - bh), sy = down ? top : snap(top + H - sh);
    block_draw(f, k, pvMain, rows, 6, 2, false, x0, my, bw, bh, 0.62f, dt);       // Magic picked : the menu steps back
    block_draw(f, k, pvSub, srows, 7, 1, true, snap(x0 + bw + gap), sy, sw, sh, 1.0f, dt);
}
void nui_help_fit(const Frame& f, float availW, float maxScale, float& outScale, float& outH) {
    float w = 0.0f, h = 0.0f; nui_sample(f, 0.0f, 0.0f, 1.0f, false, &w, &h);
    float sc = w > 1.0f ? availW / w : maxScale; if (sc > maxScale) sc = maxScale; if (sc < 0.5f) sc = 0.5f;
    outScale = sc; outH = h * sc;
}
void nui_help_box(const Frame& f, float cx, float cy, float s) { nui_sample(f, cx, cy, s, true, 0, 0); }

// ======== Status's STAT PANELS (`persona`, `level` ; Game UI -> NU_MAIN), where the client puts its own ========
// What the client's two panels show (the user's screenshot 2026-11-18), from native_persona_data() (the 0x061 mirror)
// and the live vitals (f.game->me) : name ; Lv main job ; Lv support job ; iLv / Su ; HP MP TP ; the seven attributes
// with what the gear adds ; then Master Level, Exemplar Points and the next level, with its bar.
namespace {
Block g_persB, g_lvlB; unsigned g_persMs = 0;
const char* const JOB_FULL[23] = { "", "Warrior", "Monk", "White Mage", "Black Mage", "Red Mage", "Thief", "Paladin", "Dark Knight",
    "Beastmaster", "Bard", "Ranger", "Samurai", "Ninja", "Dragoon", "Summoner", "Blue Mage", "Corsair", "Puppetmaster", "Dancer",
    "Scholar", "Geomancer", "Rune Fencer" };
void thousands(unsigned v, char* out) {      // 1628503 -> "1,628,503"
    char t[16]; sprintf(t, "%u", v); const int n = (int)strlen(t); int o = 0;
    for (int i = 0; i < n; ++i) { out[o++] = t[i]; if ((n - 1 - i) % 3 == 0 && i != n - 1) out[o++] = ','; }
    out[o] = 0;
}
}
void persona_draw(const Frame& f) {
    const UiConfig& C = ui_config();
    const NativePanel& PP = native_persona(); const NativePanel& PL = native_level();
    const PersonaMem& D = native_persona_data();
    if (!(C.nuHide & NU_MAIN) || !f.font || f.screenH <= 0.0f || f.screenW <= 0.0f) { g_persB.vis = g_lvlB.vis = 0.0f; g_persB.wasOpen = g_lvlB.wasOpen = false; return; }
    const unsigned now = GetTickCount();
    float dt = g_persMs ? (float)(now - g_persMs) / 1000.0f : 0.0f; g_persMs = now; if (dt > 0.1f) dt = 0.1f;
    static PersonaMem lastD; static NativePanel lastP, lastL;
    // Currencies 1 / 2 open IN PLACE of the stat panels : the client hides them then (the user, 2026-11-18)
    const bool curOn = native_currencies().open || native_unity_info().open || native_search().open || native_job_points().open || native_faith().open || native_celestite().open || native_merit_cat().open;
    const bool onP = PP.open && D.ok && !curOn, onL = PL.open && D.ok && !curOn;
    if (D.ok) lastD = D;
    if (onP) lastP = PP;
    if (onL) lastL = PL;
    const bool drawP = block_ease(g_persB, onP, now, dt) && lastD.ok && lastP.uiW > 0;
    const bool drawL = block_ease(g_lvlB, onL, now, dt) && lastD.ok && lastL.uiW > 0;
    if (!drawP && !drawL) return;
    const Metrics k = metrics(f);
    const float S = k.S, pad = snap(5.0f * S) + k.bi;   // the box edge -> text (+ the frame border)
    float tz = k.tz, lh = 0.0f;
    const u32 dev = f.dev; Font* fo = f.font;
    const bool live = f.game && f.game->me.name[0];
    auto place = [&](const NativePanel& P, float w, float h, float& x, float& y) {
        const float sx = f.screenW / (float)P.uiW, sy = f.screenH / (float)P.uiH;
        x = snap((float)P.rx0 * sx); y = snap((float)P.ry0 * sy);
        if (x + w > f.screenW - 4.0f) x = snap(f.screenW - 4.0f - w); if (x < 4.0f) x = 4.0f;
        if (y + h > f.screenH - 4.0f) y = snap(f.screenH - 4.0f - h); if (y < 4.0f) y = 4.0f;
    };
    const u32 cLab = 0xFF93A6BEu, cVal = 0xFFE4EAF2u, cGold = 0xFFE8D9B0u;
    static const char* ATTR[7] = { "STR", "DEX", "VIT", "AGI", "INT", "MND", "CHR" };
    float persBottom = 0.0f, persX = 0.0f, persW = 0.0f;
    if (drawP) {
        const PersonaMem& d = lastD;
        char l1[40], l2[40], l3[32], hpS[24], mpS[24], tpS[12], av[7][8], ad[7][8];
        sprintf(l1, "Lv%d %s", d.mlvl, d.mjob >= 1 && d.mjob <= 22 ? JOB_FULL[d.mjob] : "?");
        if (d.sjob >= 1 && d.sjob <= 22 && d.slvl) sprintf(l2, "Lv%d %s", d.slvl, JOB_FULL[d.sjob]); else l2[0] = 0;
        if (d.ilvl > 99) { if (d.su) sprintf(l3, "iLv %d / Su%d", d.ilvl, d.su); else sprintf(l3, "iLv %d", d.ilvl); } else l3[0] = 0;
        const int hp = live ? f.game->me.hp : (int)d.maxHp, mp = live ? f.game->me.mp : (int)d.maxMp, tp = live ? f.game->me.tp : 0;
        sprintf(hpS, "%d / %u", hp, d.maxHp); sprintf(mpS, "%d / %u", mp, d.maxMp); sprintf(tpS, "%d", tp);
        for (int a = 0; a < 7; ++a) { sprintf(av[a], "%u", (unsigned)d.base[a]); if (d.added[a]) sprintf(ad[a], "%+d", (int)d.added[a]); else ad[a][0] = 0; }
        const char* nm = live ? f.game->me.name : "";
        // THE CLIENT'S SIZE : its rect, the lines fitted into it (14 lines in 190 UI units) ; the text a touch smaller
        // than the menus' when the line asks for it. Wider only if a line would not fit (a long name, French labels).
        const int nTop = 2 + (l2[0] ? 1 : 0) + (l3[0] ? 1 : 0);              // name, main job, [sub job], [iLv]
        const float gap = snap(3.0f * S);
        const float sx = f.screenW / (float)lastP.uiW, sy = f.screenH / (float)lastP.uiH;
        float bw = snap((float)(lastP.rx1 - lastP.rx0) * sx), bh = snap((float)(lastP.ry1 - lastP.ry0) * sy);
        if (bh < 120.0f * S) bh = snap(120.0f * S);
        bw += 2.0f * k.bi; bh += 2.0f * k.bi;                                  // the client's rect + the frame border
        lh = (bh - 2.0f * pad - 2.0f * gap) / (float)(nTop + 10);
        if (tz > lh * 0.92f) tz = lh * 0.92f;
        const float c1 = fo->measure("STR", tz) + 8.0f * S, c2 = fo->measure("0000", tz), c3 = fo->measure("+000", tz) + 6.0f * S;
        float w = c1 + c2 + c3;
        const float wl[4] = { fo->measure(nm, tz * 1.05f), fo->measure(l1, tz), fo->measure(l2, tz), fo->measure(l3, tz) };
        for (int i = 0; i < 4; ++i) if (wl[i] > w) w = wl[i];
        const float vw = fo->measure("HP", tz) + 8.0f * S + fo->measure(hpS, tz); if (vw > w) w = vw;
        if (w + 2.0f * pad > bw) bw = snap(w + 2.0f * pad);
        float bx, by; place(lastP, bw, bh, bx, by);
        const int jm = menu_place(f, __LINE__, bx, by, bw, bh);
        persBottom = by + bh; persX = bx; persW = bw;          // its REST place : the Master Level box hangs from it (it slides on its own)
        const float tO = ease_out(((float)(now - g_persB.openMs) / 1000.0f) / 0.08f);
        by = snap(by + (1.0f - tO) * 4.0f * S);
        const float A = g_persB.vis * tO;
        if (A > 0.0f) {
            g_frameJoin = jm;
            draw_themed_box(dev, f.skin, bx, by, bw, bh, C.muBox, A, S);
            float y = by + pad;
            const float yHair1 = y + (float)nTop * lh + gap * 0.5f, yHair2 = yHair1 + 3.0f * lh + gap;
            rrect(dev, bx + pad, snap(yHair1), bw - 2.0f * pad, 1.0f, 0.0f, with_alpha(0x30C8D6EAu, A), with_alpha(0x30C8D6EAu, A));
            rrect(dev, bx + pad, snap(yHair2), bw - 2.0f * pad, 1.0f, 0.0f, with_alpha(0x30C8D6EAu, A), with_alpha(0x30C8D6EAu, A));
            const u32 sh = with_alpha(0xC8000000u, A);
            fo->begin(dev);
            auto line = [&](const char* t, u32 c, float z) { fo->draw_lc(dev, snap(bx + pad), snap(y + lh * 0.5f), t, z, with_alpha(c, A), sh, k.ow); y += lh; };
            line(nm, 0xFFFFFFFFu, tz * 1.05f);
            line(l1, cGold, tz);
            if (l2[0]) line(l2, cVal, tz);
            if (l3[0]) line(l3, cLab, tz);
            y += gap;
            const char* vl[3] = { "HP", "MP", "TP" }; const char* vv[3] = { hpS, mpS, tpS };
            const u32 vc[3] = { 0xFFB8F0A0u, 0xFFA8D0FFu, 0xFFFFE0A0u };
            for (int i = 0; i < 3; ++i) {
                const float cy = snap(y + lh * 0.5f);
                fo->draw_lc(dev, snap(bx + pad), cy, vl[i], tz, with_alpha(cLab, A), sh, k.ow);
                fo->draw_lc(dev, snap(bx + bw - pad - fo->measure(vv[i], tz)), cy, vv[i], tz, with_alpha(vc[i], A), sh, k.ow);
                y += lh;
            }
            y += gap;
            for (int a = 0; a < 7; ++a) {
                const float cy = snap(y + lh * 0.5f);
                fo->draw_lc(dev, snap(bx + pad), cy, ATTR[a], tz, with_alpha(cLab, A), sh, k.ow);
                fo->draw_lc(dev, snap(bx + bw - pad - c3 - fo->measure(av[a], tz)), cy, av[a], tz, with_alpha(cVal, A), sh, k.ow);
                if (ad[a][0]) fo->draw_lc(dev, snap(bx + bw - pad - fo->measure(ad[a], tz)), cy, ad[a], tz, with_alpha(d.added[a] > 0 ? 0xFF8CE68Cu : 0xFFFF8C8Cu, A), sh, k.ow);
                y += lh;
            }
        }
    }
    if (drawL) {
        const PersonaMem& d = lastD;
        char ml[24], ep[24], nx[24]; sprintf(ml, "M.Level %d", d.ml); thousands(d.epCur, ep); thousands(d.epNext, nx);
        const char* lEp = tr("Exemplar Points", "Exemplar Points");
        const char* lNx = tr("Next M.Level", "Prochain M.Level");
        float w = fo->measure(ml, tz);
        const float ws[4] = { fo->measure(lEp, tz), fo->measure(ep, tz), fo->measure(lNx, tz), fo->measure(nx, tz) };
        for (int i = 0; i < 4; ++i) if (ws[i] > w) w = ws[i];
        const float sx = f.screenW / (float)lastL.uiW, sy = f.screenH / (float)lastL.uiH;
        float bw = snap((float)(lastL.rx1 - lastL.rx0) * sx), bh = snap((float)(lastL.ry1 - lastL.ry0) * sy);
        if (bh < 60.0f * S) bh = snap(60.0f * S);
        bw += 2.0f * k.bi; bh += 2.0f * k.bi;                                  // the client's rect + the frame border
        lh = (bh - 2.0f * pad - 7.0f * S) / 5.0f;
        if (tz > lh * 0.92f) tz = lh * 0.92f;
        w = fo->measure(ml, tz); { const float w2[4] = { fo->measure(lEp, tz), fo->measure(ep, tz), fo->measure(lNx, tz), fo->measure(nx, tz) }; for (int i = 0; i < 4; ++i) if (w2[i] > w) w = w2[i]; }
        if (w + 2.0f * pad > bw) bw = snap(w + 2.0f * pad);
        if (persW > bw) bw = persW;                                           // the stats' width : the two edges line up
        float bx, by; place(lastL, bw, bh, bx, by);
        // with the stats shown : RIGHT UNDER them, their left edge (2026-11-20 : the client's rect put it at the bottom of
        // the screen, on the chat, far from the stats it belongs to)
        if (persBottom > 0.0f) { bx = persX; by = snap(persBottom + 3.0f * S); if (by + bh > f.screenH - 4.0f) by = snap(f.screenH - 4.0f - bh); }
        const int jm = menu_place(f, __LINE__, bx, by, bw, bh);   // glued under the stats (theme kit)
        const float tO = ease_out(((float)(now - g_lvlB.openMs) / 1000.0f) / 0.08f);
        by = snap(by + (1.0f - tO) * 4.0f * S);
        const float A = g_lvlB.vis * tO;
        if (A > 0.0f) {
            g_frameJoin = jm;
            draw_themed_box(dev, f.skin, bx, by, bw, bh, C.muBox, A, S);
            const float y = by + pad;
            const float barY = snap(y + 5.0f * lh + 2.0f * S), barW = bw - 2.0f * pad;
            const float fr = d.epNext ? (float)d.epCur / (float)d.epNext : 0.0f;
            rrect(dev, bx + pad, barY, barW, 4.0f * S, 2.0f * S, with_alpha(0x40FFFFFFu, A), with_alpha(0x30FFFFFFu, A));
            if (fr > 0.0f) rrect(dev, bx + pad, barY, snap(barW * (fr > 1.0f ? 1.0f : fr)), 4.0f * S, 2.0f * S, with_alpha(0xE0FF8C8Cu, A), with_alpha(0xC0D86A6Au, A));
            const u32 sh = with_alpha(0xC8000000u, A);
            fo->begin(dev);
            const char* t[5] = { ml, lEp, ep, lNx, nx };
            const u32 c[5] = { cGold, cLab, cVal, cLab, cVal };
            for (int i = 0; i < 5; ++i) {
                const float cy = snap(y + (float)i * lh + lh * 0.5f);
                const bool num = i == 2 || i == 4;
                const float tx = num ? snap(bx + bw - pad - fo->measure(t[i], tz)) : snap(bx + pad);
                fo->draw_lc(dev, tx, cy, t[i], tz, with_alpha(c[i], A), sh, k.ow);
            }
        }
    }
}

// ======== Status -> JOB LEVELS / MASTER LEVELS (Game UI -> NU_MAIN), where the client puts its own ========
// The client's layout (Ghidra 2026-11-18, the draws FUN_05489500 / FUN_0548DB10) : jobs 1..22 in id order, TWO
// columns filled row by row (WAR | MNK, WHM | BLM ...), the name then its value right-aligned ; the MAIN job gold, the
// SUPPORT job steel blue, the rest grey ; no header, no paging. Job Levels lists the UNLOCKED jobs, Master Levels the
// MASTERED ones (being listed is the mark). At the client's size : 252 wide, ((n + 1) / 2) * 14 + 22 high.
namespace { Block g_jobB; unsigned g_jobMs = 0; }
void jobs_draw(const Frame& f) {
    const UiConfig& C = ui_config();
    const NativePanel& JW = native_joblevel(); const NativePanel& MW = native_masterle();
    const NativeJobs& J = native_jobs();
    if (!(C.nuHide & NU_MAIN) || !f.font || f.screenH <= 0.0f || f.screenW <= 0.0f) { g_jobB.vis = 0.0f; g_jobB.wasOpen = false; return; }
    const unsigned now = GetTickCount();
    float dt = g_jobMs ? (float)(now - g_jobMs) / 1000.0f : 0.0f; g_jobMs = now; if (dt > 0.1f) dt = 0.1f;
    static NativeJobs lastJ; static NativePanel lastW; static bool lastMaster = false;
    const bool on = J.ok && (JW.open || MW.open);
    if (on) { lastJ = J; lastMaster = !JW.open && MW.open; lastW = lastMaster ? MW : JW; }
    if (!block_ease(g_jobB, on, now, dt) || !lastJ.ok || lastW.uiW <= 0) return;
    // the jobs listed
    int ids[22]; int n = 0;
    for (int j = 1; j <= 22; ++j) {
        const bool listed = lastMaster ? ((lastJ.mastered >> j) & 1u) != 0 : (((lastJ.unlocked >> j) & 1u) != 0 || j == lastJ.mjob);
        if (listed) ids[n++] = j;
    }
    if (!n) return;
    const Metrics k = metrics(f);
    const float S = k.S;
    const float sx = f.screenW / (float)lastW.uiW, sy = f.screenH / (float)lastW.uiH;
    const int rowsN = (n + 1) / 2;
    float bw = snap(252.0f * sx) + 2.0f * k.bi, bh = snap(((float)rowsN * 14.0f + 22.0f) * sy) + 2.0f * k.bi;   // the client's size + the frame border
    const float vpad = snap(6.0f * S) + k.bi, lh = (bh - 2.0f * vpad) / (float)rowsN;
    float tz = k.tz; if (tz > lh * 0.92f) tz = lh * 0.92f;
    float bx = snap((float)lastW.rx0 * sx), by = snap((float)lastW.ry0 * sy);
    if (bx + bw > f.screenW - 4.0f) bx = snap(f.screenW - 4.0f - bw); if (bx < 4.0f) bx = 4.0f;
    if (by + bh > f.screenH - 4.0f) by = snap(f.screenH - 4.0f - bh); if (by < 4.0f) by = 4.0f;
    const int jm = menu_place(f, __LINE__, bx, by, bw, bh);
    const float tO = ease_out(((float)(now - g_jobB.openMs) / 1000.0f) / 0.08f);
    by = snap(by + (1.0f - tO) * 4.0f * S);
    const float A = g_jobB.vis * tO; if (A <= 0.0f) return;
    const u32 dev = f.dev; Font* fo = f.font;
    g_frameJoin = jm;
    draw_themed_box(dev, f.skin, bx, by, bw, bh, C.muBox, A, S);
    // the column split : a hairline between the two columns
    rrect(dev, snap(bx + bw * 0.5f), by + vpad, 1.0f, bh - 2.0f * vpad, 0.0f, with_alpha(0x24C8D6EAu, A), with_alpha(0x24C8D6EAu, A));
    const u32 sh = with_alpha(0xC8000000u, A);
    fo->begin(dev);
    const float colW = (bw - 2.0f * k.bi) * 0.5f, ip = snap(10.0f * S);   // the two columns, inside the frame border
    for (int i = 0; i < n; ++i) {
        const int j = ids[i], col = i & 1, row = i >> 1;
        const float cx = bx + k.bi + (float)col * colW, cy = snap(by + vpad + (float)row * lh + lh * 0.5f);
        const u32 nc = j == lastJ.mjob ? 0xFFE8C860u : j == lastJ.sjob ? 0xFF86AEE6u : 0xFFC9D3E0u;
        char v[8]; sprintf(v, "%d", lastMaster ? (int)lastJ.ml[j] : (int)lastJ.lvl[j]);
        fo->draw_lc(dev, snap(cx + ip), cy, JOB_FULL[j], tz, with_alpha(nc, A), sh, k.ow);
        fo->draw_lc(dev, snap(cx + colW - ip - fo->measure(v, tz)), cy, v, tz, with_alpha(j == lastJ.mjob ? 0xFFFFF1CCu : 0xFFE4EAF2u, A), sh, k.ow);
    }
}

// ======== Status -> COMBAT / MAGIC SKILLS (Game UI -> NU_MAIN), where the client puts its own ========
// The client's layout (Ghidra 2026-11-18, the draws FUN_05489980 / FUN_05489B80) : the skills of the range in id order,
// every one whose value is not exactly 0x8000, TWO columns row by row, the name then the level right-aligned -- BLUE
// when capped (bit 15), white otherwise ; no header, no pages ; 252 wide, ((n + 1) / 2) * 14 + 22 high.
namespace {
Block g_sklB; unsigned g_sklMs = 0;
const char* const SKILL_EN[48] = { "", "Hand-to-Hand", "Dagger", "Sword", "Great Sword", "Axe", "Great Axe", "Scythe", "Polearm",
    "Katana", "Great Katana", "Club", "Staff", "", "", "", "", "", "", "", "", "", "Automaton Melee", "Automaton Archery",
    "Automaton Magic", "Archery", "Marksmanship", "Throwing", "Guard", "Evasion", "Shield", "Parrying", "Divine Magic",
    "Healing Magic", "Enhancing Magic", "Enfeebling Magic", "Elemental Magic", "Dark Magic", "Summoning Magic", "Ninjutsu",
    "Singing", "String", "Wind", "Blue Magic", "Geomancy", "Handbell", "", "" };
}
void skills_draw(const Frame& f) {
    const UiConfig& C = ui_config();
    const NativePanel& BW = native_btlskill(); const NativePanel& MW = native_mgcskill();
    const NativeSkills& K = native_skills();
    if (!(C.nuHide & NU_MAIN) || !f.font || f.screenH <= 0.0f || f.screenW <= 0.0f) { g_sklB.vis = 0.0f; g_sklB.wasOpen = false; return; }
    const unsigned now = GetTickCount();
    float dt = g_sklMs ? (float)(now - g_sklMs) / 1000.0f : 0.0f; g_sklMs = now; if (dt > 0.1f) dt = 0.1f;
    static NativeSkills lastK; static NativePanel lastW; static bool lastMagic = false;
    const bool on = K.ok && (BW.open || MW.open);
    if (on) { lastK = K; lastMagic = !BW.open && MW.open; lastW = lastMagic ? MW : BW; }
    if (!block_ease(g_sklB, on, now, dt) || !lastK.ok || lastW.uiW <= 0) return;
    int ids[32]; int n = 0;
    const int lo = lastMagic ? 32 : 1, hi = lastMagic ? 47 : 31;
    for (int id = lo; id <= hi && n < 32; ++id) if (lastK.v[id] != 0x8000 && SKILL_EN[id][0]) ids[n++] = id;
    if (!n) return;
    const Metrics k = metrics(f);
    const float S = k.S;
    const float sx = f.screenW / (float)lastW.uiW, sy = f.screenH / (float)lastW.uiH;
    const int rowsN = (n + 1) / 2;
    const float bw = snap(252.0f * sx) + 2.0f * k.bi, bh = snap(((float)rowsN * 14.0f + 22.0f) * sy) + 2.0f * k.bi;   // the client's size + the frame border
    const float vpad = snap(6.0f * S) + k.bi, lh = (bh - 2.0f * vpad) / (float)rowsN;
    float tz = k.tz; if (tz > lh * 0.92f) tz = lh * 0.92f;
    float bx = snap((float)lastW.rx0 * sx), by = snap((float)lastW.ry0 * sy);
    if (bx + bw > f.screenW - 4.0f) bx = snap(f.screenW - 4.0f - bw); if (bx < 4.0f) bx = 4.0f;
    if (by + bh > f.screenH - 4.0f) by = snap(f.screenH - 4.0f - bh); if (by < 4.0f) by = 4.0f;
    const int jm = menu_place(f, __LINE__, bx, by, bw, bh);
    const float tO = ease_out(((float)(now - g_sklB.openMs) / 1000.0f) / 0.08f);
    by = snap(by + (1.0f - tO) * 4.0f * S);
    const float A = g_sklB.vis * tO; if (A <= 0.0f) return;
    const u32 dev = f.dev; Font* fo = f.font;
    g_frameJoin = jm;
    draw_themed_box(dev, f.skin, bx, by, bw, bh, C.muBox, A, S);
    rrect(dev, snap(bx + bw * 0.5f), by + vpad, 1.0f, bh - 2.0f * vpad, 0.0f, with_alpha(0x24C8D6EAu, A), with_alpha(0x24C8D6EAu, A));
    const u32 sh = with_alpha(0xC8000000u, A);
    fo->begin(dev);
    const float colW = (bw - 2.0f * k.bi) * 0.5f, ip = snap(10.0f * S);   // the two columns, inside the frame border
    for (int i = 0; i < n; ++i) {
        const int id = ids[i], col = i & 1, row = i >> 1;
        const float cx = bx + k.bi + (float)col * colW, cy = snap(by + vpad + (float)row * lh + lh * 0.5f);
        const bool capped = (lastK.v[id] & 0x8000) != 0;
        char v[8]; sprintf(v, "%d", (int)(lastK.v[id] & 0x7FFF));
        // a long name in a narrow column : a touch smaller rather than over its value
        float nz = tz; const float room = colW - 2.0f * ip - fo->measure("000", tz) - 6.0f * S;
        const float nw = fo->measure(SKILL_EN[id], nz); if (nw > room && nw > 0.0f) nz = tz * room / nw;
        fo->draw_lc(dev, snap(cx + ip), cy, SKILL_EN[id], nz, with_alpha(0xFFC9D3E0u, A), sh, k.ow);
        fo->draw_lc(dev, snap(cx + colW - ip - fo->measure(v, tz)), cy, v, tz, with_alpha(capped ? 0xFF6FB4FFu : 0xFFF2F5FAu, A), sh, k.ow);
    }
}

// ======== Status -> CRAFTS (`trdskill`) and Merit Points' panel (`levmerit`) ; Game UI -> NU_MAIN ========
// Crafts (Ghidra 2026-11-18, FUN_05489D70) : ONE column, ids 48..62 in order, every word but 0xFFFF : the craft's
// name, its RANK (blue when capped) and "(level)" right-aligned. Rank names : Windower res/synth_ranks.lua (0..10) ;
// above that the client's own pictures are unknown, so the number. Merit Points' panel (FUN_0548B390) : two lines,
// Limit Points "lp/10000" and merits "n/max P" (blue at the max), right-aligned.
namespace {
Block g_trdB, g_lmB; unsigned g_trdMs = 0;
const char* const CRAFT_EN[10] = { "Fishing", "Woodworking", "Smithing", "Goldsmithing", "Clothcraft", "Leathercraft",
    "Bonecraft", "Alchemy", "Cooking", "Synergy" };
const char* const RANK_EN[11] = { "Amateur", "Recruit", "Initiate", "Novice", "Apprentice", "Journeyman", "Craftsman",
    "Artisan", "Adept", "Veteran", "Expert" };
}
void crafts_draw(const Frame& f) {
    const UiConfig& C = ui_config();
    if (!(C.nuHide & NU_MAIN) || !f.font || f.screenH <= 0.0f || f.screenW <= 0.0f) { g_trdB.vis = g_lmB.vis = 0.0f; g_trdB.wasOpen = g_lmB.wasOpen = false; return; }
    const unsigned now = GetTickCount();
    float dt = g_trdMs ? (float)(now - g_trdMs) / 1000.0f : 0.0f; g_trdMs = now; if (dt > 0.1f) dt = 0.1f;
    const Metrics k = metrics(f);
    const float S = k.S, vpad = snap(6.0f * S) + k.bi, ip = snap(10.0f * S) + k.bi;   // the box edge -> text (+ the frame border)
    const u32 dev = f.dev; Font* fo = f.font;
    // ---- Crafts ----
    {
        const NativePanel& W = native_trdskill(); const NativeSkills& K = native_skills();
        static NativeSkills lastK; static NativePanel lastW;
        const bool on = K.ok && W.open;
        if (on) { lastK = K; lastW = W; }
        if (block_ease(g_trdB, on, now, dt) && lastK.ok && lastW.uiW > 0) {
            int ids[16]; int n = 0;
            for (int id = 48; id <= 57; ++id) if (lastK.v[id] != 0xFFFF) ids[n++] = id;
            if (n) {
                const float sx = f.screenW / (float)lastW.uiW, sy = f.screenH / (float)lastW.uiH;
                const float bw = snap(252.0f * sx) + 2.0f * k.bi, bh = snap(((float)n * 14.0f + 22.0f) * sy) + 2.0f * k.bi;   // the client's size + the frame border
                const float lh = (bh - 2.0f * vpad) / (float)n;
                float tz = k.tz; if (tz > lh * 0.92f) tz = lh * 0.92f;
                float bx = snap((float)lastW.rx0 * sx), by = snap((float)lastW.ry0 * sy);
                if (bx + bw > f.screenW - 4.0f) bx = snap(f.screenW - 4.0f - bw); if (bx < 4.0f) bx = 4.0f;
                if (by + bh > f.screenH - 4.0f) by = snap(f.screenH - 4.0f - bh); if (by < 4.0f) by = 4.0f;
                const int jm = menu_place(f, __LINE__, bx, by, bw, bh);
                const float tO = ease_out(((float)(now - g_trdB.openMs) / 1000.0f) / 0.08f);
                by = snap(by + (1.0f - tO) * 4.0f * S);
                const float A = g_trdB.vis * tO;
                if (A > 0.0f) {
                    g_frameJoin = jm;
                    draw_themed_box(dev, f.skin, bx, by, bw, bh, C.muBox, A, S);
                    const u32 sh = with_alpha(0xC8000000u, A);
                    fo->begin(dev);
                    for (int i = 0; i < n; ++i) {
                        const unsigned v = lastK.v[ids[i]];
                        const int rank = (int)(v & 0x1F), lvl = (int)((v >> 5) & 0x3FF); const bool capped = (v & 0x8000) != 0;
                        char rk[16], lv[16]; if (rank <= 10) sprintf(rk, "%s", RANK_EN[rank]); else sprintf(rk, "Rank %d", rank);
                        sprintf(lv, "(%d)", lvl);
                        const float cy = snap(by + vpad + (float)i * lh + lh * 0.5f);
                        fo->draw_lc(dev, snap(bx + ip), cy, CRAFT_EN[ids[i] - 48], tz, with_alpha(0xFFC9D3E0u, A), sh, k.ow);
                        fo->draw_lc(dev, snap(bx + k.bi + (bw - 2.0f * k.bi) * 0.44f), cy, rk, tz, with_alpha(capped ? 0xFF6FB4FFu : 0xFFF2F5FAu, A), sh, k.ow);
                        fo->draw_lc(dev, snap(bx + bw - ip - fo->measure(lv, tz)), cy, lv, tz, with_alpha(0xFFF2F5FAu, A), sh, k.ow);
                    }
                }
            }
        }
    }
    // ---- Merit Points' panel ----
    {
        const NativePanel& W = native_levmerit(); const NativeMerits& M = native_merits();
        static NativeMerits lastM; static NativePanel lastW;
        const bool on = M.ok && W.open && !native_merit_cat().open;      // Categories has its own
        if (on) { lastM = M; lastW = W; }
        if (block_ease(g_lmB, on, now, dt) && lastM.ok && lastW.uiW > 0) {
            const float sx = f.screenW / (float)lastW.uiW, sy = f.screenH / (float)lastW.uiH;
            const float bw = snap((float)(lastW.rx1 - lastW.rx0) * sx) + 2.0f * k.bi, bh = snap((float)(lastW.ry1 - lastW.ry0) * sy) + 2.0f * k.bi;   // + the frame border
            const float lh = (bh - 2.0f * vpad) / 4.0f;
            float tz = k.tz; if (tz > lh * 0.92f) tz = lh * 0.92f;
            float bx = snap((float)lastW.rx0 * sx), by = snap((float)lastW.ry0 * sy);
            if (bx + bw > f.screenW - 4.0f) bx = snap(f.screenW - 4.0f - bw); if (bx < 4.0f) bx = 4.0f;
            if (by + bh > f.screenH - 4.0f) by = snap(f.screenH - 4.0f - bh); if (by < 4.0f) by = 4.0f;
            const int jm = menu_place(f, __LINE__, bx, by, bw, bh);
            const float tO = ease_out(((float)(now - g_lmB.openMs) / 1000.0f) / 0.08f);
            by = snap(by + (1.0f - tO) * 4.0f * S);
            const float A = g_lmB.vis * tO;
            if (A > 0.0f) {
                g_frameJoin = jm;
                draw_themed_box(dev, f.skin, bx, by, bw, bh, C.muBox, A, S);
                const u32 sh = with_alpha(0xC8000000u, A);
                char a[24], b[24]; sprintf(a, "%d/10000", lastM.lp); sprintf(b, "%d/%d P", lastM.merits, lastM.maxMerits);
                const bool full = lastM.maxMerits > 0 && lastM.merits >= lastM.maxMerits;
                fo->begin(dev);
                const char* t[4] = { tr("Limit Points", "Limit Points"), a, tr("Merit Points", "Points de m\xC3\xA9rite"), b };
                for (int i = 0; i < 4; ++i) {
                    const float cy = snap(by + vpad + (float)i * lh + lh * 0.5f);
                    const bool num = (i & 1) != 0;
                    const u32 c = !num ? 0xFF93A6BEu : (i == 3 && full) ? 0xFF6FB4FFu : 0xFFF2F5FAu;
                    fo->draw_lc(dev, num ? snap(bx + bw - ip - fo->measure(t[i], tz)) : snap(bx + ip), cy, t[i], tz, with_alpha(c, A), sh, k.ow);
                }
            }
        }
    }
}

// ======== Status -> CURRENCIES 1 / 2 (`evitem` mode 1 / 2 ; Game UI -> NU_MAIN) ========
// The client's list (Ghidra 2026-11-18) : 8 rows, the currency's name then its value right-aligned, the cursor row
// picked, every row always present ; while the server's answer is awaited the client draws an empty frame -- here a
// "..." row. In the menus' list style (capsules, the gold glass, the hand, the position rail), at the client's place.
// Status -> Currencies 1 / 2 : 99 / 97 rows the client pages 8 at a time. AioHUD's box is a SEARCHABLE list (the user,
// 2026-11-18 : "il y en a beaucoup, une boite plus moderne") : typing while the list has the focus filters it (the
// typed text highlighted in each name), Backspace edits, Esc clears (a second Esc closes the window, the client's).
// The client keeps its window (parked) and its cursor : arrows / numpad / gamepad still drive it, and the box scrolls
// to that cursor whenever it is on a shown row. Every row's value is in memory (cur_read : rows + r * 8).
namespace {
Block g_curB; unsigned g_curMs = 0;
char g_curQ[32] = { 0 };                 // the search text (ASCII) ; written by the key thread, read by the draw
volatile long g_curQn = 0;
volatile long g_curKeysOn = 0;           // the draw says the list has the focus (the key thread's gate)
bool g_curAte[256] = { false };          // a key whose press was the search's : its release is too
bool ci_find(const char* hay, const char* q, int qn, int& at) {   // case-insensitive substring
    if (qn <= 0) { at = -1; return true; }
    for (int i = 0; hay[i]; ++i) {
        int j = 0;
        while (j < qn && hay[i + j] && tolower((unsigned char)hay[i + j]) == tolower((unsigned char)q[j])) ++j;
        if (j == qn) { at = i; return true; }
    }
    return false;
}
}
bool currencies_key(int dik, unsigned ch, bool pressed, bool mod) {
    if (!pressed) { if (dik >= 0 && dik < 256 && g_curAte[dik]) { g_curAte[dik] = false; return true; } return false; }
    if (!g_curKeysOn || mod) return false;
    // the numpad (the game's cursor keys, NumLock on or off) and the arrows stay the game's
    if ((dik >= 0x47 && dik <= 0x53) || dik == 0x37 || dik == 0xB5 || dik == 0x9C || dik == 0xC8 || dik == 0xD0 || dik == 0xCB || dik == 0xCD) return false;
    long n = g_curQn;
    bool mine = false;
    if (dik == 0x0E) { if (n > 0) { g_curQ[--n] = 0; mine = true; } }                       // Backspace
    else if (dik == 0x01) { if (n > 0) { n = 0; g_curQ[0] = 0; mine = true; } }              // Esc : clears first
    else if (ch && ch < 127 && (isalnum((int)ch) || ch == '\'' || ch == '-' || ch == '.' || ch == '(' || ch == ')' || (ch == ' ' && n > 0))) {
        if (n < (long)sizeof(g_curQ) - 1) { g_curQ[n++] = (char)ch; g_curQ[n] = 0; }
        mine = true;
    }
    if (!mine) return false;
    g_curQn = n;
    if (dik >= 0 && dik < 256) g_curAte[dik] = true;
    return true;
}
void currencies_draw(const Frame& f) {
    const UiConfig& C = ui_config();
    const NativeCurrencies& N = native_currencies();
    if (!(C.nuHide & NU_MAIN) || !f.font || f.screenH <= 0.0f || f.screenW <= 0.0f) { g_curB.vis = 0.0f; g_curB.wasOpen = false; g_curKeysOn = 0; return; }
    const unsigned now = GetTickCount();
    float dt = g_curMs ? (float)(now - g_curMs) / 1000.0f : 0.0f; g_curMs = now; if (dt > 0.1f) dt = 0.1f;
    static NativeCurrencies last;
    static int lastMode = 0;
    if (N.open) last = N;
    if (!N.open || N.mode != lastMode) { g_curQn = 0; g_curQ[0] = 0; }      // a new list starts unfiltered
    if (N.open) lastMode = N.mode;
    g_curKeysOn = N.open && N.focused && N.loaded ? 1 : 0;
    if (!block_ease(g_curB, N.open, now, dt) || last.uiW <= 0) return;
    const Metrics k = metrics(f);
    const float S = k.S;
    Font* fo = f.font; const u32 dev = f.dev;
    const char* const* T = last.mode == 1 ? CURRENCY1_EN : CURRENCY2_EN;
    const int TN = last.mode == 1 ? CURRENCY1_N : CURRENCY2_N;
    char q[32]; int qn = (int)g_curQn; if (qn < 0) qn = 0; if (qn > 31) qn = 31;
    memcpy(q, g_curQ, (size_t)qn); q[qn] = 0;
    // the shown rows : every row whose name holds the search
    int idx[99], at[99], m = 0;
    if (last.loaded) for (int r = 0; r < last.n && r < 99; ++r) { int p = -1; if (ci_find(r < TN ? T[r] : "?", q, qn, p)) { idx[m] = r; at[m] = p; ++m; } }
    // the size : the widest name of both lists + its value, so nothing resizes as it pages or filters
    const float tz = k.tz, vz = k.tz * 0.95f;
    static float wAll = 0.0f, atS = 0.0f;
    if (atS != S) { atS = S; wAll = 0.0f; for (int t = 0; t < 2; ++t) for (int i = 0; i < (t ? CURRENCY2_N : CURRENCY1_N); ++i) { const float w = fo->measure(t ? CURRENCY2_EN[i] : CURRENCY1_EN[i], tz); if (w > wAll) wAll = w; } }
    const int V = 12;                                                         // rows shown at once
    const float pad = snap(8.0f * S) + k.bi, rowH = snap(k.capH * 0.92f), headH = snap(18.0f * S), fieldH = snap(k.capH * 1.05f);   // pad : + the frame border
    const float bw = snap(pad + 6.0f * S + wAll + 18.0f * S + fo->measure("9,999,999", vz) + 10.0f * S + pad);
    const float listY = headH + 6.0f * S + fieldH + 6.0f * S;
    const float bh = snap(pad + listY + (float)V * rowH + pad);
    const float sx = f.screenW / (float)last.uiW, sy = f.screenH / (float)last.uiH;
    float bx = snap((float)last.rx0 * sx), by = snap((float)last.ry0 * sy);
    if (bx + bw > f.screenW - 4.0f) bx = snap(f.screenW - 4.0f - bw); if (bx < 4.0f) bx = 4.0f;
    if (by + bh > f.screenH - 4.0f) by = snap(f.screenH - 4.0f - bh); if (by < 4.0f) by = 4.0f;
    const int jm = menu_place(f, __LINE__, bx, by, bw, bh);
    const float tO = ease_out(((float)(now - g_curB.openMs) / 1000.0f) / 0.08f);
    by = snap(by + (1.0f - tO) * 4.0f * S);
    const float A = g_curB.vis * tO; if (A <= 0.0f) return;
    // the scroll : follows the client's cursor when it is on a shown row ; back to the top when the search changes
    static int scroll = 0, lastQn = -1; static char lastQ[32] = { 0 };
    if (qn != lastQn || strcmp(q, lastQ)) { scroll = 0; lastQn = qn; strcpy(lastQ, q); }
    int curPos = -1;
    for (int i = 0; i < m; ++i) if (idx[i] == last.cursor) { curPos = i; break; }
    if (curPos >= 0) { if (curPos < scroll) scroll = curPos; if (curPos >= scroll + V) scroll = curPos - V + 1; }
    if (scroll > m - V) scroll = m - V; if (scroll < 0) scroll = 0;
    g_frameJoin = jm;
    draw_themed_box(dev, f.skin, bx, by, bw, bh, C.muBox, A, S);
    const float L = bx + pad, W = bw - 2.0f * pad;
    // the header : the list's name, how many rows the search keeps ; a hairline under it
    const float hy = snap(by + pad + headH * 0.5f);
    rrect(dev, L, snap(by + pad + headH), W, 1.0f, 0.0f, with_alpha(0x30C8D6EAu, A), with_alpha(0x30C8D6EAu, A));
    // the search field : a capsule, a magnifier, the text and its caret (or the hint)
    const float fy = snap(by + pad + headH + 6.0f * S), fr = fieldH * 0.5f, fcy = fy + fr;
    const bool typing = qn > 0;
    rrect(dev, L, fy, W, fieldH, fr, with_alpha(0x8A0A0F18u, A), with_alpha(0x8A141C2Au, A));
    rrect_stroke(dev, L, fy, W, fieldH, fr, with_alpha(typing ? 0xB0FFDA8Au : 0x40C8D6EAu, A), 1.0f);
    const float mgx = L + fr + 2.0f * S, mgr = 4.2f * S;
    const u32 mgc = with_alpha(typing ? 0xFFFFDA8Au : 0xFF93A6BEu, A);
    rrect_stroke(dev, mgx - mgr, fcy - mgr - 1.0f * S, 2.0f * mgr, 2.0f * mgr, mgr, mgc, 1.4f * S);
    seg_soft(dev, mgx + mgr * 0.7f, fcy + mgr * 0.7f - 1.0f * S, mgx + mgr * 1.6f, fcy + mgr * 1.6f - 1.0f * S, 2.2f * S, mgc);
    const float tx0 = snap(mgx + mgr + 8.0f * S);
    if (typing && last.focused && ((now / 530u) & 1u) == 0u) {               // the caret
        const float cxp = snap(tx0 + fo->measure(q, tz) + 1.0f * S);
        rrect(dev, cxp, snap(fcy - tz * 0.55f), snap(1.2f * S), snap(tz * 1.1f), 0.0f, with_alpha(0xFFFFDA8Au, A), with_alpha(0xFFFFDA8Au, A));
    }
    // the rows : zebra bands, the search's text lit in each name, the client's cursor in the party glass
    const float ly = snap(by + pad + listY);
    const int shown = m - scroll < V ? m - scroll : V;
    for (int i = 0; i < shown; ++i) {
        const float y = snap(ly + (float)i * rowH);
        if (((scroll + i) & 1) == 0) rrect(dev, L, y, W, rowH, 3.0f * S, with_alpha(0x18FFFFFFu, A), with_alpha(0x10FFFFFFu, A));
        if (typing && at[scroll + i] >= 0) {
            char pre[64]; const char* nm = idx[scroll + i] < TN ? T[idx[scroll + i]] : "?";
            int pn = at[scroll + i]; if (pn > 63) pn = 63; memcpy(pre, nm, (size_t)pn); pre[pn] = 0;
            char hit[32]; memcpy(hit, nm + at[scroll + i], (size_t)qn); hit[qn] = 0;
            const float hx = snap(L + 6.0f * S + fo->measure(pre, tz)), hw = snap(fo->measure(hit, tz));
            rrect(dev, hx - 1.0f * S, snap(y + 2.0f * S), hw + 2.0f * S, rowH - 4.0f * S, 2.5f * S, with_alpha(0x50FFDA8Au, A), with_alpha(0x38E8B860u, A));
        }
    }
    const int curRow = curPos >= scroll && curPos < scroll + V ? curPos - scroll : -1;
    if (curRow >= 0) {
        const float targetY = ly + (float)curRow * rowH;
        if (g_curB.curY < 0.0f) g_curB.curY = targetY;
        g_curB.curY += (targetY - g_curB.curY) * (1.0f - expf(-dt * 30.0f));
        if (fabsf(targetY - g_curB.curY) < 0.3f) g_curB.curY = targetY;
        const float y = snap(g_curB.curY), r = rowH * 0.5f;
        rrect_clip_begin(dev, L, y, W, rowH, r);
        party_selframe(dev, L, y, W, rowH, f.t, A, 0);
        rrect_clip_end(dev);
        rrect_stroke(dev, L, y, W, rowH, r, with_alpha(0xC8FFDA8Au, A), 1.0f);
    } else g_curB.curY = -1.0f;
    if (m > V) {                                                              // where the shown rows sit : a thin rail
        const float rx = bx + bw - k.bi - (pad - k.bi) * 0.5f - 1.0f * S, th = (float)V * rowH;   // in the right margin, inside the frame border
        const float th2 = th * (float)V / (float)m, ty2 = ly + th * (float)scroll / (float)m;
        rrect(dev, rx, ly, 2.0f * S, th, 1.0f * S, with_alpha(0x30FFFFFFu, A), with_alpha(0x30FFFFFFu, A));
        rrect(dev, rx, snap(ty2), 2.0f * S, snap(th2 < 6.0f * S ? 6.0f * S : th2), 1.0f * S, with_alpha(0xC0FFDA8Au, A), with_alpha(0xC0FFDA8Au, A));
    }
    if (curRow >= 0 && last.focused) {                                        // AioHUD's hand, left of the box
        const float hand = snap(k.capH * 1.25f), cy = g_curB.curY + rowH * 0.5f;
        const float hx = bx - hand * 0.35f + party_cursor_bob(f.t, hand);
        if (g_handTex) party_cursor(dev, g_handTex, hx, cy, hand, 0);
    }
    // the text, in one pass
    const u32 sh = with_alpha(0xC8000000u, A);
    fo->begin(dev);
    fo->draw_lc(dev, L, hy, last.mode == 1 ? "Currencies" : "Currencies 2", tz * 0.95f, with_alpha(0xFFE8D9B0u, A), sh, k.ow);
    {
        char cnt[32];
        if (!last.loaded) strcpy(cnt, "...");
        else if (typing) sprintf(cnt, "%d / %d", m, last.n); else sprintf(cnt, "%d", last.n);
        fo->draw_lc(dev, snap(L + W - fo->measure(cnt, tz * 0.85f)), hy, cnt, tz * 0.85f, with_alpha(0xFF93A6BEu, A), sh, k.ow);
    }
    if (typing) fo->draw_lc(dev, tx0, snap(fcy), q, tz, with_alpha(0xFFFFFFFFu, A), sh, k.ow);
    else fo->draw_lc(dev, tx0, snap(fcy), tr("Type to search...", "Tapez pour chercher..."), tz, with_alpha(0x9093A6BEu, A), sh, k.ow);
    if (!last.loaded) fo->draw_lc(dev, snap(L + (W - fo->measure("...", tz)) * 0.5f), snap(ly + rowH * 0.5f), "...", tz, with_alpha(0xFFC9D3E0u, A), sh, k.ow);
    else if (m == 0) { const char* no = tr("No match", "Aucun r\xC3\xA9sultat"); fo->draw_lc(dev, snap(L + (W - fo->measure(no, tz)) * 0.5f), snap(ly + rowH * 0.5f), no, tz, with_alpha(0xFF93A6BEu, A), sh, k.ow); }
    for (int i = 0; i < shown; ++i) {
        const int r = idx[scroll + i];
        const float cy = snap(ly + (float)i * rowH + rowH * 0.5f);
        const bool on = i == curRow;
        const int v = last.all[r] < 0 ? 0 : last.all[r];
        char val[16]; thousands((unsigned)v, val);
        fo->draw_lc(dev, snap(L + 6.0f * S), cy, r < TN ? T[r] : "?", tz, with_alpha(on ? 0xFFFFFFFFu : 0xFFC9D3E0u, A), sh, k.ow);
        const u32 vc = v == 0 ? 0xFF6E7C8Eu : (on ? 0xFFFFF1CCu : 0xFFE8D9B0u);
        fo->draw_lc(dev, snap(L + W - 8.0f * S - fo->measure(val, vz)), cy, val, vz, with_alpha(vc, A), sh, k.ow);
    }
}

// Status -> Unity -> Unity Info (quest00 mode 1) : the client's five lines, then its chat setting's two rows (Active /
// Inactive -- the client's entries 1 / 2, driven through it : hover, click, keys). In place of the stat panels, as the
// client puts it. Data : native_unity_info (docs/notes/unity-info-re-2026-11-18.md).
namespace {
Block g_uiB; unsigned g_uiMs = 0;
// the leaders' names : d_msg cat 0xC #402 + id (ROM/165/77.DAT)
const char* const LEADER[12] = { "None", "Pieuje", "Ayame", "Invincible Shield", "Apururu", "Maat", "Aldo",
                                 "Jakoh Wahcondalo", "Naja Salaheem", "Flaviria", "Yoran-Oran", "Sylvie" };
}
void unityinfo_draw(const Frame& f) {
    const UiConfig& C = ui_config();
    const NativeUnityInfo& U = native_unity_info();
    if (!(C.nuHide & NU_MAIN) || !f.font || f.screenH <= 0.0f || f.screenW <= 0.0f) { g_uiB.vis = 0.0f; g_uiB.wasOpen = false; return; }
    const unsigned now = GetTickCount();
    float dt = g_uiMs ? (float)(now - g_uiMs) / 1000.0f : 0.0f; g_uiMs = now; if (dt > 0.1f) dt = 0.1f;
    static NativeUnityInfo last;
    const bool on = U.open && !native_unity_rank().open;                 // Ranking Results covers Ranking Info
    if (on) last = U;
    if (!block_ease(g_uiB, on, now, dt) || last.uiW <= 0 || last.rows.n != 2) return;
    const bool info = last.mode == 1;                                    // 2 : Ranking Info, its title and two rows only
    const Metrics k = no_icons(metrics(f));   // the main menu's chain : no icons
    const float S = k.S;
    Font* fo = f.font; const u32 dev = f.dev;
    const char* lab[5] = { tr("Current Unity:", "Unity actuelle :"), tr("Ranking Results:", "Classement :"),
                           tr("Partial Ranking Results:", "Classement partiel :"), tr("Personal Evaluation:", "\xC3\x89valuation personnelle :"),
                           tr("Partial Personal Evaluation:", "\xC3\x89valuation personnelle partielle :") };
    const char* title = info ? tr("Unity Information", "Informations Unity") : tr("Ranking Information", "Classements");
    const char* chatL = tr("Unity chat settings:", "Chat Unity :");
    char rk[2][8], pt[4][16];
    for (int i = 0; i < 2; ++i) { if (last.rank[i]) { sprintf(rk[i], "%d", last.rank[i]); sprintf(pt[i], "%dpt", last.pts[i]); } else { strcpy(rk[i], "--"); pt[i][0] = 0; } }
    sprintf(pt[2], "%dpt", last.personal); sprintf(pt[3], "%dpt", last.partial);
    const char* name = LEADER[last.unity >= 0 && last.unity <= 11 ? last.unity : 0];
    // the chat rows : the client's entries, the setting chosen marked
    RowSpec rows[2] = {}; int cur = 0;
    for (int i = 0; i < 2; ++i) {
        const int e = last.rows.entry[i];
        // the client's mark goes in the icon's slot (Menus -> icons on) ; without icons, a few spaces make its room
        rows[i].label = !info ? (e == 1 ? tr("Ranking Results", "Classement") : tr("Partial Ranking Results", "Classement partiel"))
                      : k.icons ? (e == 1 ? tr("Active", "Actif") : tr("Inactive", "Inactif"))
                                : (e == 1 ? tr("     Active", "     Actif") : tr("     Inactive", "     Inactif")); rows[i].icon = IC_NONE; rows[i].sub = false; rows[i].off = false;
        if (e == last.rows.cursor) cur = i + 1;
    }
    // the size : the labels' column, the values ; the rows' own width ; never under the client's
    const float tz = k.tz, lh = snap(k.capH * 0.92f), pad = k.pad;
    float labW = 0.0f; for (int i = 0; i < 5; ++i) { const float w = fo->measure(lab[i], tz); if (w > labW) labW = w; }
    float valW = fo->measure(name, tz); { const float w = fo->measure("99", tz) + 16.0f * S + fo->measure("999999pt", tz); if (w > valW) valW = w; }
    float bw = (info ? snap(k.gutter + labW + 14.0f * S + valW + k.capPadR + pad) : snap(k.gutter + fo->measure(title, tz) + k.capPadR + pad)) + 2.0f * k.bi;   // + the frame border
    { float rw, rh; block_size(f, k, rows, 2, rw, rh); if (rw > bw) bw = rw; }
    const float sx = f.screenW / (float)last.uiW, sy = f.screenH / (float)last.uiH;
    { const float nW = snap((float)(last.rx1 - last.rx0) * sx * 0.85f) + 2.0f * k.bi; if (nW > bw) bw = nW; }
    const float inTop = info ? snap(pad + lh + 4.0f * S + 5.0f * lh + 4.0f * S + lh)   // title, 5 lines, "Unity chat settings:"
                             : snap(pad + lh + 4.0f * S);                            // title
    const float bh = snap(inTop + 2.0f * k.capH + k.gap + 2.0f * k.pad) + 2.0f * k.bi;
    float bx = snap((float)last.rx0 * sx), by = snap((float)last.ry0 * sy);
    if (bx + bw > f.screenW - 4.0f) bx = snap(f.screenW - 4.0f - bw); if (bx < 4.0f) bx = 4.0f;
    if (by + bh > f.screenH - 4.0f) by = snap(f.screenH - 4.0f - bh); if (by < 4.0f) by = 4.0f;
    const int jm = menu_place(f, __LINE__, bx, by, bw, bh);
    const float tO = ease_out(((float)(now - g_uiB.openMs) / 1000.0f) / 0.08f);
    by = snap(by + (1.0f - tO) * 4.0f * S);
    const float A = g_uiB.vis * tO; if (A <= 0.0f) return;
    g_uiB.inTop = inTop;
    g_frameJoin = jm;
    const int row = block_draw(f, k, g_uiB, rows, 2, cur, last.focused, bx, by, bw, bh, A, dt);
    if (U.open) {                                         // the mouse : hover moves the client's cursor, a click validates
        g_hit[9][0] = bx / f.screenW; g_hit[9][1] = by / f.screenH; g_hit[9][2] = bw / f.screenW; g_hit[9][3] = bh / f.screenH;
        const int ent = (row >= 1 && row <= 2) ? last.rows.entry[row - 1] : 0;
        if (ent && row != g_uiB.hoverRow && ent != U.rows.cursor) native_unity_info_hover(ent);
        g_uiB.hoverRow = row;
        if (ent && f.mouse && f.mouse->clicked) native_unity_info_pick(ent);
    } else g_uiB.hoverRow = 0;
    // the client's mark, left of each label : the setting chosen gets its yellow bubble with a fluo-green "!", the other
    // a small grey bullet (the user's reading of the client's sprites, 2026-11-18)
    for (int i = 0; i < 2 && info; ++i) {
        const bool chosen = (last.rows.entry[i] == 1) == (last.chat == 1);
        const float cy = snap(g_uiB.rowY0 + (float)i * g_uiB.rowStep + k.capH * 0.5f);
        const float cx = k.icons ? snap(g_uiB.capX + k.capPadL + k.icZ * 0.5f)                // over the slot's default bullet
                                 : snap(g_uiB.capX + k.capPadL + fo->measure("     ", tz) * 0.45f);
        if (chosen) {
            const float r = snap(k.capH * 0.34f);
            tri(dev, cx - r * 0.55f, cy + r * 0.55f, cx - r * 0.05f, cy + r * 0.85f, cx - r * 1.05f, cy + r * 1.15f, with_alpha(0xFFE8C238u, A));   // the tail
            disc(dev, cx, cy, r, with_alpha(0xFFF2D24Au, A));
            disc(dev, cx - r * 0.3f, cy - r * 0.35f, r * 0.35f, with_alpha(0x70FFFFFFu, A));                                                       // its sheen
            const u32 g = with_alpha(0xFF6CFF3Au, A);
            rrect(dev, snap(cx - 0.9f * S), snap(cy - r * 0.62f), snap(1.8f * S), snap(r * 0.78f), 0.9f * S, g, g);                                // "!"
            disc(dev, cx, cy + r * 0.45f, 1.1f * S, g);
        } else {                                                     // a metal-grey ball : a dark rim, the body, a sheen
            const float r = snap(k.capH * 0.2f);
            disc(dev, cx, cy, r + 0.8f * S, with_alpha(0xFF4A525Cu, A));
            disc(dev, cx, cy, r, with_alpha(0xFFA8B2BEu, A));
            disc(dev, cx - r * 0.3f, cy - r * 0.32f, r * 0.42f, with_alpha(0xC0F4F8FCu, A));
        }
    }
    // the info band : the title and its hairline, the five lines (or the client's wait), the chat setting's caption
    const float L = snap(bx + k.bi + k.gutter), Rr = snap(bx + bw - k.bi - k.capPadR - pad), X = snap(L + labW + 14.0f * S);   // inside the frame border
    const float ty = snap(by + k.bi + pad + lh * 0.5f);
    rrect(dev, L, snap(by + k.bi + pad + lh + 1.0f * S), Rr - L, 1.0f, 0.0f, with_alpha(0x30C8D6EAu, A), with_alpha(0x30C8D6EAu, A));
    const u32 sh = with_alpha(0xC8000000u, A), cl = with_alpha(0xFF93A6BEu, A), cv = with_alpha(0xFFE4EAF2u, A), cg = with_alpha(0xFFE8D9B0u, A);
    fo->begin(dev);
    fo->draw_lc(dev, L, ty, title, tz * 0.95f, cg, sh, k.ow);
    const float y0 = by + k.bi + pad + lh + 4.0f * S;
    auto rowC = [&](int i) { return snap(y0 + (float)i * lh + lh * 0.5f); };
    if (!info) {}
    else if (last.loading) {
        fo->draw_lc(dev, L, rowC(1), tr("Preparing data.", "Pr\xC3\xA9paration des donn\xC3\xA9""es."), tz, cv, sh, k.ow);
        fo->draw_lc(dev, L, rowC(2), tr("Please wait a moment.", "Un instant, s'il vous pla\xC3\xAEt."), tz, cl, sh, k.ow);
    } else {
        for (int i = 0; i < 5; ++i) fo->draw_lc(dev, L, rowC(i), lab[i], tz, cl, sh, k.ow);
        fo->draw_lc(dev, X, rowC(0), name, tz, cg, sh, k.ow);
        for (int i = 0; i < 2; ++i) {
            fo->draw_lc(dev, X, rowC(1 + i), rk[i], tz, with_alpha(last.rank[i] == 1 ? 0xFFFFDA8Au : 0xFFE4EAF2u, A), sh, k.ow);
            if (pt[i][0]) fo->draw_lc(dev, snap(Rr - fo->measure(pt[i], tz)), rowC(1 + i), pt[i], tz, cv, sh, k.ow);
        }
        for (int i = 2; i < 4; ++i) fo->draw_lc(dev, snap(Rr - fo->measure(pt[i], tz)), rowC(1 + i), pt[i], tz, cv, sh, k.ow);
    }
    if (info) fo->draw_lc(dev, L, snap(y0 + 5.0f * lh + 4.0f * S + lh * 0.5f), chatL, tz, cl, sh, k.ow);
}

// Ranking Info -> Ranking Results / Partial Ranking Results (quest01) : the 11 Unities, rank / leader / points, the
// player's own in gold, the client's cursor in the party glass. Where the client puts its list.
namespace { Block g_urB; unsigned g_urMs = 0; }
void unityrank_draw(const Frame& f) {
    const UiConfig& C = ui_config();
    const NativeUnityRank& K = native_unity_rank();
    if (!(C.nuHide & NU_MAIN) || !f.font || f.screenH <= 0.0f || f.screenW <= 0.0f) { g_urB.vis = 0.0f; g_urB.wasOpen = false; return; }
    const unsigned now = GetTickCount();
    float dt = g_urMs ? (float)(now - g_urMs) / 1000.0f : 0.0f; g_urMs = now; if (dt > 0.1f) dt = 0.1f;
    static NativeUnityRank last;
    if (K.open) last = K;
    if (!block_ease(g_urB, K.open, now, dt) || last.uiW <= 0) return;
    const Metrics k = metrics(f);
    const float S = k.S, pad = snap(8.0f * S) + k.bi;   // the box edge -> content (+ the frame border)
    Font* fo = f.font; const u32 dev = f.dev;
    const int mine = native_unity_info().unity;
    const char* title = last.list ? tr("Partial Ranking Results", "Classement partiel") : tr("Ranking Results", "Classement");
    const float tz = k.tz, rowH = snap(k.capH * 0.92f), headH = snap(18.0f * S);
    float nameW = 0.0f; for (int i = 1; i <= 11; ++i) { const float w = fo->measure(LEADER[i], tz); if (w > nameW) nameW = w; }
    const float rankW = fo->measure("10", tz), ptW = fo->measure("999999pt", tz);
    float bw = snap(pad + 8.0f * S + rankW + 16.0f * S + nameW + 24.0f * S + ptW + 8.0f * S + pad);
    const int V = 11;
    const float listY = headH + 6.0f * S;
    const float bh = snap(pad + listY + (float)V * rowH + pad);
    const float sx = f.screenW / (float)last.uiW, sy = f.screenH / (float)last.uiH;
    { const float nW = snap((float)(last.rx1 - last.rx0) * sx * 0.85f) + 2.0f * k.bi; if (nW > bw) bw = nW; }
    float bx = snap((float)last.rx0 * sx), by = snap((float)last.ry0 * sy);
    if (bx + bw > f.screenW - 4.0f) bx = snap(f.screenW - 4.0f - bw); if (bx < 4.0f) bx = 4.0f;
    if (by + bh > f.screenH - 4.0f) by = snap(f.screenH - 4.0f - bh); if (by < 4.0f) by = 4.0f;
    const int jm = menu_place(f, __LINE__, bx, by, bw, bh);
    const float tO = ease_out(((float)(now - g_urB.openMs) / 1000.0f) / 0.08f);
    by = snap(by + (1.0f - tO) * 4.0f * S);
    const float A = g_urB.vis * tO; if (A <= 0.0f) return;
    g_frameJoin = jm;
    draw_themed_box(dev, f.skin, bx, by, bw, bh, C.muBox, A, S);
    const float L = bx + pad, W = bw - 2.0f * pad, ly = snap(by + pad + listY);
    const float hy = snap(by + pad + headH * 0.5f);
    rrect(dev, L, snap(by + pad + headH), W, 1.0f, 0.0f, with_alpha(0x30C8D6EAu, A), with_alpha(0x30C8D6EAu, A));
    const int n = last.loading ? 0 : last.n;
    for (int i = 0; i < n; ++i) {
        const float y = snap(ly + (float)i * rowH);
        if ((i & 1) == 0) rrect(dev, L, y, W, rowH, 3.0f * S, with_alpha(0x18FFFFFFu, A), with_alpha(0x10FFFFFFu, A));
        if (last.id[i] == mine) rrect_stroke(dev, L, y, W, rowH, rowH * 0.5f, with_alpha(0x60FFDA8Au, A), 1.0f);
    }
    const int cr = last.cursor >= 0 && last.cursor < n ? last.cursor : -1;
    if (cr >= 0) {
        const float targetY = ly + (float)cr * rowH;
        if (g_urB.curY < 0.0f) g_urB.curY = targetY;
        g_urB.curY += (targetY - g_urB.curY) * (1.0f - expf(-dt * 30.0f));
        if (fabsf(targetY - g_urB.curY) < 0.3f) g_urB.curY = targetY;
        const float y = snap(g_urB.curY), r = rowH * 0.5f;
        rrect_clip_begin(dev, L, y, W, rowH, r);
        party_selframe(dev, L, y, W, rowH, f.t, A, 0);
        rrect_clip_end(dev);
        rrect_stroke(dev, L, y, W, rowH, r, with_alpha(0xC8FFDA8Au, A), 1.0f);
        if (last.focused) {
            const float hand = snap(k.capH * 1.25f), cy = g_urB.curY + rowH * 0.5f;
            const float hx = bx - hand * 0.35f + party_cursor_bob(f.t, hand);
            if (g_handTex) party_cursor(dev, g_handTex, hx, cy, hand, 0);
        }
    } else g_urB.curY = -1.0f;
    const u32 sh = with_alpha(0xC8000000u, A);
    fo->begin(dev);
    fo->draw_lc(dev, L, hy, title, tz * 0.95f, with_alpha(0xFFE8D9B0u, A), sh, k.ow);
    if (last.loading) {
        const char* w = tr("Preparing data.", "Pr\xC3\xA9paration des donn\xC3\xA9""es.");
        fo->draw_lc(dev, snap(L + (W - fo->measure(w, tz)) * 0.5f), snap(ly + rowH * 0.5f), w, tz, with_alpha(0xFFC9D3E0u, A), sh, k.ow);
    }
    const float rx = snap(L + 8.0f * S + rankW), nx = snap(rx + 16.0f * S);
    for (int i = 0; i < n; ++i) {
        const float cy = snap(ly + (float)i * rowH + rowH * 0.5f);
        const bool me = last.id[i] == mine, on = i == cr;
        char rk[8], pt[16]; sprintf(rk, "%d", last.rank[i]); sprintf(pt, "%dpt", last.score[i]);
        const u32 c = me ? 0xFFFFDA8Au : (on ? 0xFFFFFFFFu : 0xFFC9D3E0u);
        fo->draw_lc(dev, snap(rx - fo->measure(rk, tz)), cy, rk, tz, with_alpha(last.rank[i] <= 3 ? 0xFFE8D9B0u : 0xFF93A6BEu, A), sh, k.ow);
        fo->draw_lc(dev, nx, cy, LEADER[last.id[i] >= 1 && last.id[i] <= 11 ? last.id[i] : 0], tz, with_alpha(c, A), sh, k.ow);
        fo->draw_lc(dev, snap(L + W - 8.0f * S - fo->measure(pt, tz)), cy, pt, tz, with_alpha(me ? 0xFFFFF1CCu : 0xFFE4EAF2u, A), sh, k.ow);
    }
}

// Status -> Unity -> Unity Lists : the Unity to list (arealist mode 4 -- all 12 rows at once, the client pages 5),
// the people found (scresult -- the client's page and cursor), a player's options (scoption, in the submenu's place).
namespace { Block g_alB, g_srB, g_soB; unsigned g_alMs = 0, g_srMs = 0, g_soMs = 0; }
void unitylists_draw(const Frame& f) {
    const UiConfig& C = ui_config();
    if (!(C.nuHide & NU_MAIN) || !f.font || f.screenH <= 0.0f || f.screenW <= 0.0f) {
        g_alB.vis = g_srB.vis = g_soB.vis = 0.0f; g_alB.wasOpen = g_srB.wasOpen = g_soB.wasOpen = false; return; }
    const unsigned now = GetTickCount();
    const Metrics k = no_icons(metrics(f));   // the main menu's chain : no icons
    const float S = k.S;
    Font* fo = f.font; const u32 dev = f.dev;
    const int own = native_unity_info().unity ? native_unity_info().unity : native_unity().unity;
    // ---- the Unity to list ----
    {
        float dt = g_alMs ? (float)(now - g_alMs) / 1000.0f : 0.0f; g_alMs = now; if (dt > 0.1f) dt = 0.1f;
        const NativeAreaList& A0 = native_arealist();
        static NativeAreaList last;
        if (A0.open) last = A0;
        if (last.mode != 4) {
            // Search -> Area / Monipulators : Current Area, Current Region, All Areas, then the regions (a region opens its
            // zones). The client shows 5 rows ; AioHUD shows 12 from the client's top (its scroll follows the hand).
            if (block_ease(g_alB, A0.open, now, dt) && last.uiW > 0 && last.arRows > 0) {
                RowSpec rows[12] = {}; char lab[12][48], cnt[12][8]; const int n = last.arRows;
                const unsigned myZone = f.game ? f.game->zone : 0;
                for (int r = 0; r < n; ++r) {
                    const int v = last.arVal[r]; const char* t = 0;
                    if (last.arTop + r == 0 && v > 0) { const char* z = zone_name(v); lstrcpynA(lab[r], z && z[0] ? z : "?", sizeof(lab[r])); t = lab[r]; rows[r].extra = tr("current", "actuelle"); rows[r].tint = 0xFFFFDA8Au; }
                    else if (v == -1) t = tr("Current Region", "R\xC3\xA9gion actuelle");
                    else if (v == 0) t = tr("All Areas", "Toutes les zones");
                    else if (v > 0) { const char* z = zone_name(v); lstrcpynA(lab[r], z && z[0] ? z : "?", sizeof(lab[r])); t = lab[r]; rows[r].icon = IC_NONE; }
                    else if (last.arLabel[r][0]) { lstrcpynA(lab[r], last.arLabel[r], sizeof(lab[r])); t = lab[r]; }
                    else { _snprintf(lab[r], sizeof(lab[r]), "%s %d", tr("Region", "R\xC3\xA9gion"), -v - 1); t = lab[r]; }
                    rows[r].label = t; rows[r].sub = v <= -2; rows[r].off = false;
                    if (v <= -2 && last.arCount[r] > 0) { _snprintf(cnt[r], sizeof(cnt[r]), "%d", last.arCount[r]); rows[r].extra = cnt[r]; }
                    if (v > 0 && (unsigned)v == myZone) rows[r].tint = 0xFFFFDA8Au;
                }
                float bw, bh; block_size(f, k, rows, n, bw, bh);
                {   // FIXED : as tall as 12 rows whatever the page holds, as wide as the widest page seen since it opened
                    RowSpec one[12] = {}; for (int r = 0; r < 12; ++r) { one[r].label = ZONE_NAME_REF; one[r].extra = tr("current", "actuelle"); one[r].sub = true; }
                    float w12, h12; block_size(f, k, one, 12, w12, h12); bw = w12; bh = h12;   // never from the page
                }
                const float sx = f.screenW / (float)last.uiW, sy = f.screenH / (float)last.uiH;
                float bx = snap((float)last.rx0 * sx), by = snap((float)last.ry0 * sy);
                { float cx, cy; if (place_on_chat(bw, bh, snap(4.0f * S), f.screenW, f.screenH, cx, cy)) { bx = cx; by = cy; } }   // above the chat, as the client
                if (bx + bw > f.screenW - 4.0f) bx = snap(f.screenW - 4.0f - bw); if (bx < 4.0f) bx = 4.0f;
                if (by + bh > f.screenH - 4.0f) by = snap(f.screenH - 4.0f - bh); if (by < 4.0f) by = 4.0f;
                const int jm = menu_place(f, __LINE__, bx, by, bw, bh);
                const float tO = ease_out(((float)(now - g_alB.openMs) / 1000.0f) / 0.08f);
                by = snap(by + (1.0f - tO) * 4.0f * S);
                const float A = g_alB.vis * tO;
                if (A > 0.0f) {
                    const int cr = last.cursor >= last.arTop && last.cursor < last.arTop + n ? last.cursor - last.arTop + 1 : 0;
                    g_frameJoin = jm;
                    const int row = block_draw(f, k, g_alB, rows, n, cr, last.focused, bx, by, bw, bh, A, dt);
                    if (last.n > n || last.arTop > 0) {                // where this page sits : a thin rail
                        const float rx = bx + bw - k.bi - 4.0f * S, ty = by + k.bi + k.pad, th = bh - 2.0f * k.bi - 2.0f * k.pad;   // inside the frame border
                        const float th2 = th * (float)n / (float)last.n, ty2 = ty + th * (float)last.arTop / (float)last.n;
                        rrect(f.dev, rx, ty, 2.0f * S, th, 1.0f * S, with_alpha(0x30FFFFFFu, A), with_alpha(0x30FFFFFFu, A));
                        rrect(f.dev, rx, snap(ty2), 2.0f * S, snap(th2 < 6.0f * S ? 6.0f * S : th2), 1.0f * S, with_alpha(0xC0FFDA8Au, A), with_alpha(0xC0FFDA8Au, A));
                    }
                    if (A0.open) {
                        g_hit[9][0] = bx / f.screenW; g_hit[9][1] = by / f.screenH; g_hit[9][2] = bw / f.screenW; g_hit[9][3] = bh / f.screenH;
                        const int abs = row ? last.arTop + row - 1 : -1;
                        if (row && row != g_alB.hoverRow && abs != A0.cursor) native_textlist_hover("arealist", abs);
                        g_alB.hoverRow = row;
                        if (row && f.mouse && f.mouse->clicked) native_textlist_pick("arealist", abs);
                    } else g_alB.hoverRow = 0;
                }
            }
        } else
        if (block_ease(g_alB, A0.open, now, dt) && last.uiW > 0 && last.n == 12) {
            RowSpec rows[12] = {}; char ownB[40];
            for (int r = 0; r < 12; ++r) {
                rows[r].icon = r ? IC_DUO : IC_STAR; rows[r].sub = false; rows[r].off = false;
                rows[r].label = r ? LEADER[r] : tr("Your Unity", "Ton Unity");
                if (r == own && r) rows[r].tint = 0xFFFFDA8Au;
            }
            if (own >= 1 && own <= 11) { sprintf(ownB, "%s", LEADER[own]); rows[0].extra = ownB; }
            float bw, bh; block_size(f, k, rows, 12, bw, bh);
            const float sx = f.screenW / (float)last.uiW, sy = f.screenH / (float)last.uiH;
            float bx = snap((float)last.rx0 * sx), by = snap((float)last.ry0 * sy);
            { float cx, cy; if (place_on_chat(bw, bh, snap(4.0f * S), f.screenW, f.screenH, cx, cy)) { bx = cx; by = cy; } }   // above the chat, as the client
            if (bx + bw > f.screenW - 4.0f) bx = snap(f.screenW - 4.0f - bw); if (bx < 4.0f) bx = 4.0f;
            if (by + bh > f.screenH - 4.0f) by = snap(f.screenH - 4.0f - bh); if (by < 4.0f) by = 4.0f;
            const int jm = menu_place(f, __LINE__, bx, by, bw, bh);
            const float tO = ease_out(((float)(now - g_alB.openMs) / 1000.0f) / 0.08f);
            by = snap(by + (1.0f - tO) * 4.0f * S);
            const float A = g_alB.vis * tO;
            if (A > 0.0f) {
                g_frameJoin = jm;
                const int row = block_draw(f, k, g_alB, rows, 12, last.cursor >= 0 ? last.cursor + 1 : 0, last.focused, bx, by, bw, bh, A, dt);
                if (A0.open) {
                    g_hit[9][0] = bx / f.screenW; g_hit[9][1] = by / f.screenH; g_hit[9][2] = bw / f.screenW; g_hit[9][3] = bh / f.screenH;
                    if (row && row != g_alB.hoverRow && row - 1 != A0.cursor) native_textlist_hover("arealist", row - 1);
                    g_alB.hoverRow = row;
                    if (row && f.mouse && f.mouse->clicked) native_textlist_pick("arealist", row - 1);
                } else g_alB.hoverRow = 0;
            }
        }
    }
    // ---- the people found ----
    {
        float dt = g_srMs ? (float)(now - g_srMs) / 1000.0f : 0.0f; g_srMs = now; if (dt > 0.1f) dt = 0.1f;
        const NativeSearch& S0 = native_search();
        static NativeSearch last;
        if (S0.open) last = S0;
        if (block_ease(g_srB, S0.open, now, dt) && last.uiW > 0) {
            const float pad = snap(8.0f * S) + k.bi, tz = k.tz, rowH = snap(k.capH * 0.92f), headH = snap(18.0f * S);   // pad : + the frame border
            const int V = last.vis;
            const float jobW = fo->measure("MNK99/WAR49", tz * 0.92f), nameW = fo->measure("Wwwwwwwwwwwwwww", tz) * 0.8f;
            // a linkshell's member list, as the client's : the member's LS item (shell / sack / pearl) as an icon, job / sub, race, name, zone
            const bool lsList = last.ctx == 4;
            const float lsIz = snap(rowH - 4.0f * S), raceW = fo->measure("Hum", tz * 0.92f);
            const float rankW = lsList ? lsIz + 6.0f * S + raceW + 10.0f * S : 0.0f;
            float bw = snap(pad + 6.0f * S + jobW + 14.0f * S + nameW + 14.0f * S + rankW + fo->measure("Southern San d'Oria [S]", tz * 0.92f) + 6.0f * S + pad);
            const float listY = headH + 6.0f * S;
            const float bh = snap(pad + listY + (float)V * rowH + pad);
            const float sx = f.screenW / (float)last.uiW, sy = f.screenH / (float)last.uiH;
            float bx = snap((float)last.rx0 * sx), by = snap((float)last.ry0 * sy);
            if (bx + bw > f.screenW - 4.0f) bx = snap(f.screenW - 4.0f - bw); if (bx < 4.0f) bx = 4.0f;
            if (by + bh > f.screenH - 4.0f) by = snap(f.screenH - 4.0f - bh); if (by < 4.0f) by = 4.0f;
            const int jm = menu_place(f, __LINE__, bx, by, bw, bh);
            const float tO = ease_out(((float)(now - g_srB.openMs) / 1000.0f) / 0.08f);
            by = snap(by + (1.0f - tO) * 4.0f * S);
            const float A = g_srB.vis * tO * (last.focused || !native_scoption().open ? 1.0f : 0.62f);
            if (A > 0.0f) {
                g_frameJoin = jm;
                draw_themed_box(dev, f.skin, bx, by, bw, bh, C.muBox, A, S);
                const float L = bx + pad, W = bw - 2.0f * pad, ly = snap(by + pad + listY), hy = snap(by + pad + headH * 0.5f);
                rrect(dev, L, snap(by + pad + headH), W, 1.0f, 0.0f, with_alpha(0x30C8D6EAu, A), with_alpha(0x30C8D6EAu, A));
                const int shown = last.n - last.top < V ? last.n - last.top : V;
                for (int i = 0; i < shown; ++i) {
                    const float y = snap(ly + (float)i * rowH);
                    if (((last.top + i) & 1) == 0) rrect(dev, L, y, W, rowH, 3.0f * S, with_alpha(0x18FFFFFFu, A), with_alpha(0x10FFFFFFu, A));
                    if (last.row[i].type == 2) rrect(dev, L + 6.0f * S, snap(y + rowH * 0.5f), W - 12.0f * S, 1.0f, 0.0f, with_alpha(0x40C8D6EAu, A), with_alpha(0x40C8D6EAu, A));
                }
                const int cr = last.cursor >= last.top && last.cursor < last.top + shown ? last.cursor - last.top : -1;
                if (cr >= 0) {
                    const float targetY = ly + (float)cr * rowH;
                    if (g_srB.curY < 0.0f) g_srB.curY = targetY;
                    g_srB.curY += (targetY - g_srB.curY) * (1.0f - expf(-dt * 30.0f));
                    if (fabsf(targetY - g_srB.curY) < 0.3f) g_srB.curY = targetY;
                    const float y = snap(g_srB.curY), r = rowH * 0.5f;
                    rrect_clip_begin(dev, L, y, W, rowH, r);
                    party_selframe(dev, L, y, W, rowH, f.t, A, 0);
                    rrect_clip_end(dev);
                    rrect_stroke(dev, L, y, W, rowH, r, with_alpha(0xC8FFDA8Au, A), 1.0f);
                    if (last.focused) {
                        const float hand = snap(k.capH * 1.25f), cy = g_srB.curY + rowH * 0.5f;
                        const float hx = bx - hand * 0.35f + party_cursor_bob(f.t, hand);
                        if (g_handTex) party_cursor(dev, g_handTex, hx, cy, hand, 0);
                    }
                } else g_srB.curY = -1.0f;
                if (last.n > V) {                                  // where this page sits : a thin rail
                    const float rx = bx + bw - k.bi - (pad - k.bi) * 0.5f - 1.0f * S, th = (float)V * rowH;   // in the right margin, inside the frame border
                    const float th2 = th * (float)V / (float)last.n, ty2 = ly + th * (float)last.top / (float)last.n;
                    rrect(dev, rx, ly, 2.0f * S, th, 1.0f * S, with_alpha(0x30FFFFFFu, A), with_alpha(0x30FFFFFFu, A));
                    rrect(dev, rx, snap(ty2), 2.0f * S, snap(th2 < 6.0f * S ? 6.0f * S : th2), 1.0f * S, with_alpha(0xC0FFDA8Au, A), with_alpha(0xC0FFDA8Au, A));
                }
                // the mouse : the hand follows, a click validates (a player : his options ; the header : refine)
                const MouseState* ms = f.mouse;
                int mrow = -1;
                if (S0.open && last.focused && ms && ms->focused && ms->x >= L && ms->x < L + W && ms->y >= ly && ms->y < ly + (float)shown * rowH)
                    mrow = last.top + (int)((ms->y - ly) / rowH);
                if (S0.open) {
                    g_hit[9][0] = bx / f.screenW; g_hit[9][1] = by / f.screenH; g_hit[9][2] = bw / f.screenW; g_hit[9][3] = bh / f.screenH;
                    const char* sw = S0.ctx == 2 ? "levelsyn" : S0.ctx == 4 ? "link12" : "scresult";
                    if (mrow >= 0 && mrow + 1 != g_srB.hoverRow && mrow != S0.cursor) native_textlist_hover(sw, mrow);
                    g_srB.hoverRow = mrow + 1;
                    if (mrow >= 0 && ms->clicked) native_textlist_pick(sw, mrow);
                }
                const u32 sh = with_alpha(0xC8000000u, A);
                fo->begin(dev);
                if (last.ctx == 4) fo->draw_lc(dev, L, hy, last.lsName[0] ? last.lsName : "Linkshell", tz * 0.95f, with_alpha(last.lsRgb ? (last.lsRgb | 0xFF000000u) : 0xFFE8D9B0u, A), sh, k.ow);
                else fo->draw_lc(dev, L, hy, last.ctx == 3 ? tr("Search", "Recherche") : last.ctx == 2 ? "Level Sync" : last.ctx == 1 ? tr("Party", "Groupe") : tr("Unity Lists", "Listes Unity"), tz * 0.95f, with_alpha(0xFFE8D9B0u, A), sh, k.ow);
                if (last.ctx != 2) {
                    char cnt[48];
                    if (last.counted) sprintf(cnt, tr("%d found", "%d trouv\xC3\xA9(s)"), last.total); else strcpy(cnt, tr("Searching...", "Recherche..."));
                    fo->draw_lc(dev, snap(L + W - fo->measure(cnt, tz * 0.85f)), hy, cnt, tz * 0.85f, with_alpha(0xFF93A6BEu, A), sh, k.ow);
                }
                if (last.n == 0 && last.empty[0]) fo->draw_lc(dev, snap(L + (W - fo->measure(last.empty, tz)) * 0.5f), snap(ly + rowH * 0.5f), last.empty, tz, with_alpha(0xFFC9D3E0u, A), sh, k.ow);
                const unsigned myZone = f.game ? f.game->zone : 0;
                const float ix = snap(L + 6.0f * S), jx = lsList ? snap(ix + lsIz + 6.0f * S) : ix;
                const float rcx = snap(jx + jobW + 8.0f * S), nx = lsList ? snap(rcx + raceW + 10.0f * S) : snap(jx + jobW + 14.0f * S);
                static const char* const RACE3[9] = { 0, "Hum", "Hum", "Elv", "Elv", "Tar", "Tar", "Mit", "Gal" };
                for (int i = 0; i < shown; ++i) {
                    const NativeSearchRow& r = last.row[i];
                    const float cy = snap(ly + (float)i * rowH + rowH * 0.5f);
                    const bool on = i == cr;
                    if (r.type == 1) {
                        const char* t = tr("Refine search", "Affiner la recherche");
                        fo->draw_lc(dev, jx, cy, t, tz * 0.92f, with_alpha(on ? 0xFFFFF1CCu : 0xFF93A6BEu, A), sh, k.ow);
                        continue;
                    }
                    if (r.type != 0) continue;
                    char job[24];
                    if (r.hasJob) {
                        if (r.sjob) sprintf(job, "%s%d/%s%d", job_abbr(r.mjob), r.mlvl, job_abbr(r.sjob), r.slvl);
                        else sprintf(job, "%s%d", job_abbr(r.mjob), r.mlvl);
                    } else strcpy(job, "Anon");
                    fo->draw_lc(dev, jx, cy, job, tz * 0.92f, with_alpha(r.hasJob ? 0xFFC9D3E0u : 0xFF7E8A98u, A), sh, k.ow);
                    // Level Sync : the client greys a member under level 10 or anonymous (it cannot be the target)
                    const bool lsOff = last.ctx == 2 && (r.mlvl < 10 || !r.hasJob);
                    const u32 nc = lsOff ? 0xFF6E7C8Eu : (r.flags & 8) ? 0xFFFFD27Au : (r.flags & 0x2000) ? 0xFF8FD0FFu : (on ? 0xFFFFFFFFu : 0xFFE4EAF2u);
                    fo->draw_lc(dev, nx, cy, r.name, tz, with_alpha(nc, A), sh, k.ow);
                    if (lsList) {   // the member's LS item : 1 the shell (513), 2 a sack (514), 3 a pearl (515) ; then the race
                        const unsigned itemId = r.lsRank == 1 ? 513u : r.lsRank == 2 ? 514u : r.lsRank == 3 ? 515u : 0u;
                        const u32 tex = itemId ? list_icon(f, 3, itemId) : 0;
                        if (tex) { dTexQuadState(dev, tex); tquad(dev, ix, snap(cy - lsIz * 0.5f), lsIz, lsIz, 0.0f, 1.0f, 0.0f, 1.0f, with_alpha(0xFFFFFFFFu, A), with_alpha(0xFFFFFFFFu, A)); dSetTex(dev, 0, 0); cs(dev); fo->begin(dev); }
                        const char* rc = r.race <= 8 ? RACE3[r.race] : 0;
                        if (rc && r.hasJob) fo->draw_lc(dev, rcx, cy, rc, tz * 0.92f, with_alpha(0xFF93A6BEu, A), sh, k.ow);
                    }
                    if (r.zone && r.zone != myZone) {
                        const char* z = zone_name(r.zone);
                        if (z && z[0]) fo->draw_lc(dev, snap(L + W - 8.0f * S - fo->measure(z, tz * 0.92f)), cy, z, tz * 0.92f, with_alpha(0xFF93A6BEu, A), sh, k.ow);
                    }
                }
            }
        }
    }
    // ---- a player's options : in the submenu's place (the client's 384,48), our menu style ----
    {
        float dt = g_soMs ? (float)(now - g_soMs) / 1000.0f : 0.0f; g_soMs = now; if (dt > 0.1f) dt = 0.1f;
        const NativeEntryMenu& O = native_scoption();
        static NativeEntryMenu last;
        if (O.open) last = O;
        if (block_ease(g_soB, O.open, now, dt) && last.n > 0 && last.uiW > 0) {
            RowSpec rows[32] = {}; char nums[32][8]; int cur = 0;   // 32 = NativeEntryMenu's capacity
            for (int i = 0; i < last.n && i < 32; ++i) {
                const SubRow* r = sub_row("scoption", last.entry[i]);
                if (r) { rows[i].label = tr(r->en, r->fr); rows[i].icon = r->icon; } else { sprintf(nums[i], "#%d", last.entry[i]); rows[i].label = nums[i]; rows[i].icon = IC_NONE; }
                rows[i].sub = false; rows[i].off = last.state[i] == 5;
                if (last.entry[i] == last.cursor) cur = i + 1;
            }
            float bw, bh; block_size(f, k, rows, last.n, bw, bh);
            const float sx = f.screenW / (float)last.uiW, sy = f.screenH / (float)last.uiH;
            float bx = snap((float)last.rx0 * sx), by = snap((float)last.ry0 * sy);
            if (g_subXY[0] >= 0.0f) { bx = g_subXY[0]; by = g_subXY[1]; }   // where Unity's menu was (it steps aside meanwhile)
            if (bx + bw > f.screenW - 4.0f) bx = snap(f.screenW - 4.0f - bw); if (bx < 4.0f) bx = 4.0f;
            if (by + bh > f.screenH - 4.0f) by = snap(f.screenH - 4.0f - bh); if (by < 4.0f) by = 4.0f;
            const int jm = menu_place(f, __LINE__, bx, by, bw, bh);
            const float tO = ease_out(((float)(now - g_soB.openMs) / 1000.0f) / 0.07f);
            by = snap(by + (1.0f - tO) * 3.0f * S);
            const float A = g_soB.vis * tO;
            if (A > 0.0f) {
                g_frameJoin = jm;
                const int row = block_draw(f, k, g_soB, rows, last.n, cur, last.focused, bx, by, bw, bh, A, dt);
                if (O.open) {
                    g_hit[6][0] = bx / f.screenW; g_hit[6][1] = by / f.screenH; g_hit[6][2] = bw / f.screenW; g_hit[6][3] = bh / f.screenH;   // the submenu's slot : it is hidden meanwhile
                    const int ent = (row >= 1 && row <= last.n) ? last.entry[row - 1] : 0;
                    if (ent && row != g_soB.hoverRow && ent != O.cursor) native_scoption_hover(ent);
                    g_soB.hoverRow = row;
                    if (ent && f.mouse && f.mouse->clicked) native_scoption_pick(ent);
                } else g_soB.hoverRow = 0;
            }
        }
    }
}

// Status -> Job Points, laid out as the client : on top the job list (merit2ca -- all its rows at once, a job under 99
// greyed, each row's Job Points on its right) or, a job validated, its page (jbpcat : categories / gifts, the two tabs
// behind two arrows) ; under it, the job's CP / JP (levmeri2) on the left and the description (bluehelp) on the right.
// The names and descriptions are the client's own, from its d_msg files (native_jp_text).
namespace {
Block g_jpB, g_j2B, g_j3B; unsigned g_jpMs = 0;
// the client's text made drawable : 81 9A (a star) -> '*', any other byte past ASCII dropped ; 0x0A kept
void jp_clean(const char* src, char* out, int cap) {
    int o = 0;
    for (int i = 0; src && src[i] && o < cap - 1; ++i) {
        const unsigned char c = (unsigned char)src[i];
        if (c == 0x81 && (unsigned char)src[i + 1] == 0x9A) { out[o++] = '*'; ++i; }
        else if (c == 0x0A || (c >= 0x20 && c < 0x7F)) out[o++] = (char)c;
    }
    out[o] = 0;
}
// greedy word wrap to `w` : up to `maxL` lines into lines[][96] ; returns the count
int jp_wrap(Font* fo, const char* text, float tz, float w, char (*lines)[96], int maxL) {
    int nl = 0; char cur[96] = ""; int cl = 0;
    const char* p = text;
    while (*p && nl < maxL) {
        if (*p == 0x0A) { memcpy(lines[nl++], cur, (size_t)cl + 1); cl = 0; cur[0] = 0; ++p; continue; }
        while (*p == ' ') ++p;
        const char* q = p; while (*q && *q != ' ' && *q != 0x0A) ++q;
        char word[96]; int wl = (int)(q - p); if (wl > 90) wl = 90; memcpy(word, p, (size_t)wl); word[wl] = 0;
        char trial[96]; _snprintf(trial, sizeof(trial), cl ? "%s %s" : "%s%s", cur, word); trial[95] = 0;
        if (cl && fo->measure(trial, tz) > w) { memcpy(lines[nl++], cur, (size_t)cl + 1); if (nl >= maxL) break; strcpy(cur, word); cl = wl; }
        else { strcpy(cur, trial); cl = (int)strlen(cur); }
        p = q;
    }
    if (cl && nl < maxL) memcpy(lines[nl++], cur, (size_t)cl + 1);
    return nl;
}
}
void jobpoints_draw(const Frame& f) {
    const UiConfig& C = ui_config();
    if (!(C.nuHide & NU_MAIN) || !f.font || f.screenH <= 0.0f || f.screenW <= 0.0f) { g_jpB.vis = g_j2B.vis = g_j3B.vis = 0.0f; g_jpB.wasOpen = g_j2B.wasOpen = g_j3B.wasOpen = false; return; }
    const unsigned now = GetTickCount();
    float dt = g_jpMs ? (float)(now - g_jpMs) / 1000.0f : 0.0f; g_jpMs = now; if (dt > 0.1f) dt = 0.1f;
    const NativeJobPoints& P0 = native_job_points(); const NativeJpCat& K0 = native_jp_cat();
    static NativeJobPoints last; static NativeJpCat lastK;
    const bool on = P0.open && P0.ok;
    if (on) last = P0;
    const bool cat = on && K0.open;                                        // a job's page over the list (jbpcat open : merit2ca +0x54 stays 0, measured)
    if (cat) lastK = K0;
    if (!block_ease(g_jpB, on, now, dt) || last.uiW <= 0 || last.n <= 0) { g_j2B.vis = g_j3B.vis = 0.0f; return; }
    const Metrics k = metrics(f);
    const float S = k.S, pad = snap(8.0f * S) + k.bi, tz = k.tz, rowH = snap(k.capH * 0.92f), headH = snap(18.0f * S), gap = snap(6.0f * S);
    Font* fo = f.font; const u32 dev = f.dev;
    const float tO = ease_out(((float)(now - g_jpB.openMs) / 1000.0f) / 0.08f);
    const float A = g_jpB.vis * tO; if (A <= 0.0f) return;
    const u32 sh = with_alpha(0xC8000000u, A);
    const int job = cat ? lastK.job : (last.cursor >= 0 && last.cursor < last.n ? last.job[last.cursor] : (last.chosen ? last.chosen : 0));
    // ---- the size : the list as wide as the widest of the job names / the page's rows ----
    const int nRows = cat ? (lastK.n - lastK.top < lastK.vis ? lastK.n - lastK.top : lastK.vis) : last.n;
    const int V = cat ? lastK.vis : last.n;
    float wName = 0.0f;
    if (cat) for (int i = 0; i < nRows; ++i) {
        const NativeJpRow& r = lastK.row[i];
        char t[96]; jp_clean(lastK.tab ? native_jp_text(2, r.gift, false, lastK.job) : native_jp_text(1, r.id, false), t, sizeof(t));
        const float w = fo->measure(t[0] ? t : "?", tz); if (w > wName) wName = w;
    }
    else for (int i = 0; i < last.n; ++i) { const int j = last.job[i]; const float w = fo->measure(j >= 1 && j <= 22 ? JOB_FULL[j] : "?", tz); if (w > wName) wName = w; }
    const float wRight = cat ? (lastK.tab ? fo->measure("2100", tz) : fo->measure("20/20", tz) + 18.0f * S + fo->measure("NEXT 999 P", tz)) : fo->measure("500 JP", tz);
    float bw = snap(pad + 8.0f * S + wName + 28.0f * S + wRight + 8.0f * S + pad);
    if (bw < 360.0f * S) bw = snap(360.0f * S);
    bw = keep_width(g_jpB, bw);
    const float listY = headH + 6.0f * S;
    const float bh = snap(pad + listY + (float)V * rowH + pad);
    const float sx = f.screenW / (float)last.uiW, sy = f.screenH / (float)last.uiH;
    // the two boxes under it
    const char* lab[3] = { tr("Capacity Points", "Capacity Points"), tr("Job Points", "Job Points"), tr("Spent", "D\xC3\xA9pens\xC3\xA9s") };
    char v[3][24] = { "", "", "" };
    if (job >= 1 && job <= 23) { sprintf(v[0], "%d/30000", last.capPts[job]); sprintf(v[1], "%d/500 P", last.jobPts[job]); sprintf(v[2], "%d", last.jpSpent[job]); }
    // a FIXED width (the longest values it can show) : it no longer changes from one job to the next (the user, 2026-11-18)
    float lw = 0.0f; for (int i = 0; i < 3; ++i) { const float a1 = fo->measure(lab[i], tz); if (a1 > lw) lw = a1; }
    { const float t1 = fo->measure("Dark Knight", tz * 0.95f); if (t1 > lw) lw = t1; }
    const float vw = fo->measure("29999/30000", tz) > fo->measure("500/500 P", tz) ? fo->measure("29999/30000", tz) : fo->measure("500/500 P", tz);
    const float jw = snap(pad + lw + 16.0f * S + vw + pad), jh = snap(pad + headH + 6.0f * S + 3.0f * rowH + pad);
    // the description beside it : wide enough for the client's texts on about two lines
    { const float need = jw + gap + snap(560.0f * S); if (bw < need) bw = snap(need); }
    // the description : what the hand is on
    char dTitle[96] = "", dBody[400] = "";
    {
        const char* t = 0; const char* d = 0;
        if (cat) {
            const int cr = lastK.cursor - lastK.top;
            if (cr >= 0 && cr < nRows) {
                const NativeJpRow& r = lastK.row[cr];
                if (lastK.tab) { t = native_jp_text(2, r.gift, false, lastK.job); d = native_jp_text(2, r.gift, true, lastK.job); }
                else { t = native_jp_text(1, r.id, false); d = native_jp_text(1, r.id, true); }
            }
        } else if (job >= 1 && job <= 22) { t = native_jp_text(0, job, false); d = native_jp_text(0, job, true); if (!t) t = JOB_FULL[job]; }
        jp_clean(t, dTitle, sizeof(dTitle)); jp_clean(d, dBody, sizeof(dBody));
    }
    float bx = snap((float)last.rx0 * sx), by = snap((float)last.ry0 * sy);
    const float totalH = bh + gap + jh;
    if (bx + bw > f.screenW - 4.0f) bx = snap(f.screenW - 4.0f - bw); if (bx < 4.0f) bx = 4.0f;
    if (by + totalH > f.screenH - 4.0f) by = snap(f.screenH - 4.0f - totalH); if (by < 4.0f) by = 4.0f;
    const int jm = menu_place(f, __LINE__, bx, by, bw, bh);
    const float by0 = by;                                    // its rest place : the boxes under it are placed from it
    by = snap(by + (1.0f - tO) * 4.0f * S);
    // ======== the list / the page ========
    g_frameJoin = jm;
    draw_themed_box(dev, f.skin, bx, by, bw, bh, C.muBox, A, S);
    const float L = bx + pad, W = bw - 2.0f * pad, ly = snap(by + pad + listY), hy = snap(by + pad + headH * 0.5f);
    rrect(dev, L, snap(by + pad + headH), W, 1.0f, 0.0f, with_alpha(0x30C8D6EAu, A), with_alpha(0x30C8D6EAu, A));
    for (int i = 0; i < nRows; ++i) if (((i + (cat ? lastK.top : 0)) & 1) == 0) rrect(dev, L, snap(ly + (float)i * rowH), W, rowH, 3.0f * S, with_alpha(0x18FFFFFFu, A), with_alpha(0x10FFFFFFu, A));
    const int curAll = cat ? lastK.cursor : last.cursor, top = cat ? lastK.top : 0;
    const int cr = curAll >= top && curAll < top + nRows ? curAll - top : -1;
    const bool foc = cat ? lastK.focused : last.focused;
    if (cr >= 0) {
        const float targetY = ly + (float)cr * rowH;
        if (g_jpB.curY < 0.0f) g_jpB.curY = targetY;
        g_jpB.curY += (targetY - g_jpB.curY) * (1.0f - expf(-dt * 30.0f));
        if (fabsf(targetY - g_jpB.curY) < 0.3f) g_jpB.curY = targetY;
        const float y = snap(g_jpB.curY), r = rowH * 0.5f;
        rrect_clip_begin(dev, L, y, W, rowH, r);
        party_selframe(dev, L, y, W, rowH, f.t, A, 0);
        rrect_clip_end(dev);
        rrect_stroke(dev, L, y, W, rowH, r, with_alpha(0xC8FFDA8Au, A), 1.0f);
        if (foc) {
            const float hand = snap(k.capH * 1.25f), cy = g_jpB.curY + rowH * 0.5f;
            const float hx = bx - hand * 0.35f + party_cursor_bob(f.t, hand);
            if (g_handTex) party_cursor(dev, g_handTex, hx, cy, hand, 0);
        }
    } else g_jpB.curY = -1.0f;
    if (cat && lastK.n > lastK.vis) {                       // where this page sits : a thin rail
        const float rx = bx + bw - k.bi - (pad - k.bi) * 0.5f - 1.0f * S, th = (float)V * rowH;
        const float th2 = th * (float)lastK.vis / (float)lastK.n, ty2 = ly + th * (float)lastK.top / (float)lastK.n;
        rrect(dev, rx, ly, 2.0f * S, th, 1.0f * S, with_alpha(0x30FFFFFFu, A), with_alpha(0x30FFFFFFu, A));
        rrect(dev, rx, snap(ty2), 2.0f * S, snap(th2 < 6.0f * S ? 6.0f * S : th2), 1.0f * S, with_alpha(0xC0FFDA8Au, A), with_alpha(0xC0FFDA8Au, A));
    }
    // the page's two arrows (the client's left / right : its other tab), clickable
    const char* title = cat ? (lastK.tab ? tr("Gifts", "Cadeaux") : tr("Job Points", "Job Points")) : tr("Job Points", "Job Points");
    const float ar = 3.6f * S, tW = fo->measure(title, tz * 0.95f);
    const float a1x = L + ar, a2x = L + ar * 2.0f + 8.0f * S + tW + 8.0f * S + ar;
    const float tx = cat ? snap(L + ar * 2.0f + 8.0f * S) : L;
    const MouseState* ms = f.mouse;
    if (cat) {
        auto over = [&](float cx) { return ms && ms->focused && fabsf(ms->x - cx) < 9.0f * S && fabsf(ms->y - hy) < 9.0f * S; };
        const bool o1 = over(a1x), o2 = over(a2x);
        tri(dev, a1x - ar, hy, a1x + ar * 0.7f, hy - ar, a1x + ar * 0.7f, hy + ar, with_alpha(o1 ? 0xFFFFF1CCu : 0xFFD8A060u, A));
        tri(dev, a2x + ar, hy, a2x - ar * 0.7f, hy - ar, a2x - ar * 0.7f, hy + ar, with_alpha(o2 ? 0xFFFFF1CCu : 0xFFD8A060u, A));
        if (K0.open && ms && ms->clicked && (o1 || o2)) native_jp_cat_tab(o1 ? -1 : 1);
    }
    // the mouse on the rows : the client's hand follows, a click validates
    {
        int mrow = -1;
        const bool live = cat ? K0.open : P0.open;
        if (live && foc && ms && ms->focused && ms->x >= L && ms->x < L + W && ms->y >= ly && ms->y < ly + (float)nRows * rowH)
            mrow = top + (int)((ms->y - ly) / rowH);
        if (live) {
            g_hit[9][0] = bx / f.screenW; g_hit[9][1] = by / f.screenH; g_hit[9][2] = bw / f.screenW; g_hit[9][3] = (bh + gap + jh) / f.screenH;
            const char* wn = cat ? "jbpcat" : "merit2ca";
            if (mrow >= 0 && mrow + 1 != g_jpB.hoverRow && mrow != curAll) native_textlist_hover(wn, mrow);
            g_jpB.hoverRow = mrow + 1;
            if (mrow >= 0 && ms->clicked) native_textlist_pick(wn, mrow);
        }
    }
    fo->begin(dev);
    fo->draw_lc(dev, tx, hy, title, tz * 0.95f, with_alpha(0xFFE8D9B0u, A), sh, k.ow);
    if (cat && job >= 1 && job <= 23) {                       // the header's right : the spent total (gifts) / JP to spend
        char hr[40];
        if (lastK.tab) sprintf(hr, tr("Combo Total: %d", "Total : %d"), last.jpSpent[job]); else sprintf(hr, "%d JP", last.jobPts[job]);
        fo->draw_lc(dev, snap(L + W - fo->measure(hr, tz * 0.9f)), hy, hr, tz * 0.9f, with_alpha(0xFF93A6BEu, A), sh, k.ow);
    }
    const u32 GREY = 0x80404040u, BLUE = 0x802060A0u;
    for (int i = 0; i < nRows; ++i) {
        const float cy = snap(ly + (float)i * rowH + rowH * 0.5f);
        const bool me = i == cr;
        if (!cat) {
            const int j = last.job[i]; const bool can = j >= 1 && j <= 23 && last.lvl[j] >= 99;
            fo->draw_lc(dev, snap(L + 8.0f * S), cy, j >= 1 && j <= 22 ? JOB_FULL[j] : "?", tz, with_alpha(!can ? 0xFF6E7C8Eu : me ? 0xFFFFFFFFu : 0xFFC9D3E0u, A), sh, k.ow);
            if (can) { char t[16]; sprintf(t, "%d JP", last.jobPts[j]); fo->draw_lc(dev, snap(L + W - 8.0f * S - fo->measure(t, tz)), cy, t, tz, with_alpha(last.jobPts[j] >= 500 ? 0xFF6FB4FFu : 0xFFE8D9B0u, A), sh, k.ow); }
            continue;
        }
        const NativeJpRow& r = lastK.row[i];
        char nm[96]; jp_clean(lastK.tab ? native_jp_text(2, r.gift, false, lastK.job) : native_jp_text(1, r.id, false), nm, sizeof(nm));
        if (!nm[0]) strcpy(nm, "...");
        if (lastK.tab) {                                       // a gift : greyed until the spent total reaches it
            const bool grey = r.colThr == GREY || r.colName == GREY;
            fo->draw_lc(dev, snap(L + 8.0f * S), cy, nm, tz, with_alpha(grey ? 0xFF6E7C8Eu : me ? 0xFFFFFFFFu : 0xFFC9D3E0u, A), sh, k.ow);
            char t[16]; sprintf(t, "%d", r.thr);
            fo->draw_lc(dev, snap(L + W - 8.0f * S - fo->measure(t, tz)), cy, t, tz, with_alpha(grey ? 0xFF6E7C8Eu : 0xFFE8D9B0u, A), sh, k.ow);
        } else {                                               // a category : level / max (blue maxed), the next cost
            const bool grey = r.colLvl == GREY, maxed = r.colLvl == BLUE;
            fo->draw_lc(dev, snap(L + 8.0f * S), cy, nm, tz, with_alpha(grey ? 0xFF6E7C8Eu : me ? 0xFFFFFFFFu : 0xFFC9D3E0u, A), sh, k.ow);
            char lv[16]; sprintf(lv, "%d/%d", r.lvl, r.max);
            const float costW = fo->measure("NEXT 999 P", tz);
            fo->draw_lc(dev, snap(L + W - 8.0f * S - costW - 18.0f * S - fo->measure(lv, tz)), cy, lv, tz, with_alpha(maxed ? 0xFF6FB4FFu : grey ? 0xFF6E7C8Eu : 0xFFF2F5FAu, A), sh, k.ow);
            if (r.cost >= 0 && !maxed) {
                char c[24]; sprintf(c, "NEXT %d P", r.cost);
                fo->draw_lc(dev, snap(L + W - 8.0f * S - fo->measure(c, tz)), cy, c, tz, with_alpha(r.colCost == GREY || grey ? 0xFF6E7C8Eu : 0xFFE8D9B0u, A), sh, k.ow);
            }
        }
    }
    // ======== under it : the CP / JP box (left), the description (right) ========
    const float y2 = snap(by + bh + gap), sl = by - by0;     // sl : the opening slide, shared by the boxes under it
    if (job >= 1 && job <= 23) {
        float jx = bx, jy = y2;
        g_frameJoin = menu_place_at(f, __LINE__, jx, jy, bx, y2 - sl, jw, jh);   // glued under the list (theme kit)
        draw_themed_box(dev, f.skin, jx, jy, jw, jh, C.muBox, A, S);
        const float Lb = jx + pad, Rb = jx + jw - pad, hyb = snap(jy + pad + headH * 0.5f), lyb = jy + pad + headH + 6.0f * S;
        rrect(dev, Lb, snap(jy + pad + headH), Rb - Lb, 1.0f, 0.0f, with_alpha(0x30C8D6EAu, A), with_alpha(0x30C8D6EAu, A));
        fo->begin(dev);
        fo->draw_lc(dev, Lb, hyb, job <= 22 ? JOB_FULL[job] : "?", tz * 0.95f, with_alpha(0xFFE8D9B0u, A), sh, k.ow);
        for (int i = 0; i < 3; ++i) {
            const float cy = snap(lyb + (float)i * rowH + rowH * 0.5f);
            const bool full = (i == 0 && last.capPts[job] >= 30000) || (i == 1 && last.jobPts[job] >= 500);
            fo->draw_lc(dev, Lb, cy, lab[i], tz, with_alpha(0xFF93A6BEu, A), sh, k.ow);
            fo->draw_lc(dev, snap(Rb - fo->measure(v[i], tz)), cy, v[i], tz, with_alpha(full ? 0xFF6FB4FFu : 0xFFF2F5FAu, A), sh, k.ow);
        }
    }
    if (dTitle[0] || dBody[0]) {
        float dx = snap(bx + jw + gap); const float dw = snap(bw - jw - gap);
        if (dw > 120.0f * S) {
            char lines[6][96]; const int nl = jp_wrap(fo, dBody, tz * 0.95f, dw - 2.0f * pad - 10.0f * S, lines, 6);
            const float dh = jh > snap(pad + headH + 6.0f * S + (float)nl * rowH + pad) ? jh : snap(pad + headH + 6.0f * S + (float)nl * rowH + pad);
            float dy = y2; g_frameJoin = menu_place_at(f, __LINE__, dx, dy, dx, y2 - sl, dw, dh);   // glued beside / under (theme kit)
            draw_themed_box(dev, f.skin, dx, dy, dw, dh, C.muBox, A, S);
            hit_extend(9, dy + dh, f.screenH);               // a long description grows past jh : its clicks stay ours
            const float Ld = dx + pad, hyd = snap(dy + pad + headH * 0.5f), lyd = dy + pad + headH + 6.0f * S;
            rrect(dev, Ld, snap(dy + pad + headH), dw - 2.0f * pad, 1.0f, 0.0f, with_alpha(0x30C8D6EAu, A), with_alpha(0x30C8D6EAu, A));
            fo->begin(dev);
            fo->draw_lc(dev, Ld, hyd, dTitle, tz * 0.95f, with_alpha(0xFFFFFFFFu, A), sh, k.ow);
            for (int i = 0; i < nl; ++i) fo->draw_lc(dev, snap(Ld + 10.0f * S), snap(lyd + (float)i * rowH + rowH * 0.5f), lines[i], tz * 0.95f, with_alpha(0xFFC9D3E0u, A), sh, k.ow);
        }
    }
}

// Status -> Alter Ego -> its points : the categories (fp_cat) then a category's abilities (fp_cat2), laid out as Job
// Points -- the list on top ; under it the points (fp_info, "n/1000") on the left and the description on the right.
// Names / descriptions : the client's faith d_msg (native_faith_text) ; numbers and colours : its own cells.
namespace { Block g_aeB; unsigned g_aeMs = 0; }
void alterego_draw(const Frame& f) {
    const UiConfig& C = ui_config();
    if (!(C.nuHide & NU_MAIN) || !f.font || f.screenH <= 0.0f || f.screenW <= 0.0f) { g_aeB.vis = 0.0f; g_aeB.wasOpen = false; return; }
    const unsigned now = GetTickCount();
    float dt = g_aeMs ? (float)(now - g_aeMs) / 1000.0f : 0.0f; g_aeMs = now; if (dt > 0.1f) dt = 0.1f;
    const NativeFaith& F0 = native_faith();
    static NativeFaith last;
    if (F0.open) last = F0;
    if (!block_ease(g_aeB, F0.open, now, dt) || last.uiW <= 0) return;
    const Metrics k = metrics(f);
    const float S = k.S, pad = snap(8.0f * S) + k.bi, tz = k.tz, rowH = snap(k.capH * 0.92f), headH = snap(18.0f * S), gap = snap(6.0f * S);
    Font* fo = f.font; const u32 dev = f.dev;
    const float tO = ease_out(((float)(now - g_aeB.openMs) / 1000.0f) / 0.08f);
    const float A = g_aeB.vis * tO; if (A <= 0.0f) return;
    const u32 sh = with_alpha(0xC8000000u, A), GREY = 0x80404040u, BLUE = 0x802060A0u;
    const bool abil = last.level == 1;
    const int nRows = last.n - last.top < last.vis ? last.n - last.top : last.vis;
    const int V = abil ? last.vis : (nRows > 0 ? nRows : 1);
    char names[16][96];
    float wName = 0.0f;
    for (int i = 0; i < nRows; ++i) { jp_clean(native_faith_text(last.row[i].id, false), names[i], sizeof(names[i])); if (!names[i][0]) strcpy(names[i], "..."); const float w = fo->measure(names[i], tz); if (w > wName) wName = w; }
    const float wRight = abil ? fo->measure("60/60", tz) + 18.0f * S + fo->measure("NEXT 99 P", tz) : 0.0f;
    float bw = snap(pad + 8.0f * S + wName + 28.0f * S + wRight + 8.0f * S + pad);
    if (bw < 360.0f * S) bw = snap(360.0f * S);
    bw = keep_width(g_aeB, bw);
    const float listY = headH + 6.0f * S, bh = snap(pad + listY + (float)V * rowH + pad);
    // the points box and the description under it
    const char* pl = tr("Alter Ego Points", "Points Alter Ego");
    char pv[24] = "--"; if (last.pointsOk) sprintf(pv, "%d/1000", last.points);
    const float jw = snap(pad + fo->measure(pl, tz) + 16.0f * S + pad), jh = snap(pad + headH + 6.0f * S + rowH + pad);
    { const float need = jw + gap + snap(560.0f * S); if (bw < need) bw = snap(need); }   // the description : about two lines
    char dTitle[96] = "", dBody[400] = "";
    const int cr = last.cursor >= last.top && last.cursor < last.top + nRows ? last.cursor - last.top : -1;
    if (cr >= 0) { strcpy(dTitle, names[cr]); jp_clean(native_faith_text(last.row[cr].id, true), dBody, sizeof(dBody)); }
    const float sx = f.screenW / (float)last.uiW, sy = f.screenH / (float)last.uiH;
    float bx = snap((float)last.rx0 * sx), by = snap((float)last.ry0 * sy);
    const float totalH = bh + gap + jh;
    if (bx + bw > f.screenW - 4.0f) bx = snap(f.screenW - 4.0f - bw); if (bx < 4.0f) bx = 4.0f;
    if (by + totalH > f.screenH - 4.0f) by = snap(f.screenH - 4.0f - totalH); if (by < 4.0f) by = 4.0f;
    const int jm = menu_place(f, __LINE__, bx, by, bw, bh);
    const float by0 = by;                                    // its rest place : the boxes under it are placed from it
    by = snap(by + (1.0f - tO) * 4.0f * S);
    // ======== the list ========
    g_frameJoin = jm;
    draw_themed_box(dev, f.skin, bx, by, bw, bh, C.muBox, A, S);
    const float L = bx + pad, W = bw - 2.0f * pad, ly = snap(by + pad + listY), hy = snap(by + pad + headH * 0.5f);
    rrect(dev, L, snap(by + pad + headH), W, 1.0f, 0.0f, with_alpha(0x30C8D6EAu, A), with_alpha(0x30C8D6EAu, A));
    for (int i = 0; i < nRows; ++i) if (((i + last.top) & 1) == 0) rrect(dev, L, snap(ly + (float)i * rowH), W, rowH, 3.0f * S, with_alpha(0x18FFFFFFu, A), with_alpha(0x10FFFFFFu, A));
    if (cr >= 0) {
        const float targetY = ly + (float)cr * rowH;
        if (g_aeB.curY < 0.0f) g_aeB.curY = targetY;
        g_aeB.curY += (targetY - g_aeB.curY) * (1.0f - expf(-dt * 30.0f));
        if (fabsf(targetY - g_aeB.curY) < 0.3f) g_aeB.curY = targetY;
        const float y = snap(g_aeB.curY), r = rowH * 0.5f;
        rrect_clip_begin(dev, L, y, W, rowH, r);
        party_selframe(dev, L, y, W, rowH, f.t, A, 0);
        rrect_clip_end(dev);
        rrect_stroke(dev, L, y, W, rowH, r, with_alpha(0xC8FFDA8Au, A), 1.0f);
        if (last.focused) {
            const float hand = snap(k.capH * 1.25f), cy = g_aeB.curY + rowH * 0.5f;
            const float hx = bx - hand * 0.35f + party_cursor_bob(f.t, hand);
            if (g_handTex) party_cursor(dev, g_handTex, hx, cy, hand, 0);
        }
    } else g_aeB.curY = -1.0f;
    if (last.n > last.vis) {
        const float rx = bx + bw - k.bi - (pad - k.bi) * 0.5f - 1.0f * S, th = (float)V * rowH;
        const float th2 = th * (float)last.vis / (float)last.n, ty2 = ly + th * (float)last.top / (float)last.n;
        rrect(dev, rx, ly, 2.0f * S, th, 1.0f * S, with_alpha(0x30FFFFFFu, A), with_alpha(0x30FFFFFFu, A));
        rrect(dev, rx, snap(ty2), 2.0f * S, snap(th2 < 6.0f * S ? 6.0f * S : th2), 1.0f * S, with_alpha(0xC0FFDA8Au, A), with_alpha(0xC0FFDA8Au, A));
    }
    {   // the mouse : the client's hand follows, a click validates (a category opens its abilities ; an ability : its yes / no)
        const MouseState* ms = f.mouse;
        int mrow = -1;
        if (F0.open && last.focused && ms && ms->focused && ms->x >= L && ms->x < L + W && ms->y >= ly && ms->y < ly + (float)nRows * rowH)
            mrow = last.top + (int)((ms->y - ly) / rowH);
        if (F0.open) {
            g_hit[9][0] = bx / f.screenW; g_hit[9][1] = by / f.screenH; g_hit[9][2] = bw / f.screenW; g_hit[9][3] = totalH / f.screenH;
            const char* wn = abil ? "fp_cat2" : "fp_cat";
            if (mrow >= 0 && mrow + 1 != g_aeB.hoverRow && mrow != F0.cursor) native_textlist_hover(wn, mrow);
            g_aeB.hoverRow = mrow + 1;
            if (mrow >= 0 && ms->clicked) native_textlist_pick(wn, mrow);
        }
    }
    fo->begin(dev);
    fo->draw_lc(dev, L, hy, tr("Alter Ego Points", "Points Alter Ego"), tz * 0.95f, with_alpha(0xFFE8D9B0u, A), sh, k.ow);
    if (last.pointsOk) { char hr[24]; sprintf(hr, "%d P", last.points); fo->draw_lc(dev, snap(L + W - fo->measure(hr, tz * 0.9f)), hy, hr, tz * 0.9f, with_alpha(0xFF93A6BEu, A), sh, k.ow); }
    for (int i = 0; i < nRows; ++i) {
        const NativeJpRow& r = last.row[i];
        const float cy = snap(ly + (float)i * rowH + rowH * 0.5f);
        const bool me = i == cr, grey = abil && r.colLvl == GREY, maxed = abil && r.colLvl == BLUE;
        fo->draw_lc(dev, snap(L + 8.0f * S), cy, names[i], tz, with_alpha(grey ? 0xFF6E7C8Eu : me ? 0xFFFFFFFFu : 0xFFC9D3E0u, A), sh, k.ow);
        if (!abil) continue;
        char lv[16]; sprintf(lv, "%d/%d", r.lvl, r.max);
        const float costW = fo->measure("NEXT 99 P", tz);
        fo->draw_lc(dev, snap(L + W - 8.0f * S - costW - 18.0f * S - fo->measure(lv, tz)), cy, lv, tz, with_alpha(maxed ? 0xFF6FB4FFu : grey ? 0xFF6E7C8Eu : 0xFFF2F5FAu, A), sh, k.ow);
        if (r.cost >= 0 && !maxed) {
            char c[24]; sprintf(c, "NEXT %d P", r.cost);
            fo->draw_lc(dev, snap(L + W - 8.0f * S - fo->measure(c, tz)), cy, c, tz, with_alpha(r.colCost == GREY || grey ? 0xFF6E7C8Eu : 0xFFE8D9B0u, A), sh, k.ow);
        }
    }
    // ======== under it : the points (left), the description (right) ========
    const float y2 = snap(by + bh + gap), sl = by - by0;     // sl : the opening slide, shared by the boxes under it
    float jx = bx, jy = y2;
    g_frameJoin = menu_place_at(f, __LINE__, jx, jy, bx, y2 - sl, jw, jh);   // glued under the list (theme kit)
    draw_themed_box(dev, f.skin, jx, jy, jw, jh, C.muBox, A, S);
    {
        const float Lb = jx + pad, Rb = jx + jw - pad, hyb = snap(jy + pad + headH * 0.5f), cy = snap(jy + pad + headH + 6.0f * S + rowH * 0.5f);
        rrect(dev, Lb, snap(jy + pad + headH), Rb - Lb, 1.0f, 0.0f, with_alpha(0x30C8D6EAu, A), with_alpha(0x30C8D6EAu, A));
        fo->begin(dev);
        fo->draw_lc(dev, Lb, hyb, pl, tz * 0.95f, with_alpha(0xFFE8D9B0u, A), sh, k.ow);
        fo->draw_lc(dev, snap(Rb - fo->measure(pv, tz)), cy, pv, tz, with_alpha(last.points > 999 ? 0xFF6FB4FFu : 0xFFF2F5FAu, A), sh, k.ow);
    }
    if (dTitle[0]) {
        float dx = snap(bx + jw + gap); const float dw = snap(bw - jw - gap);
        if (dw > 120.0f * S) {
            char lines[6][96]; const int nl = jp_wrap(fo, dBody, tz * 0.95f, dw - 2.0f * pad - 10.0f * S, lines, 6);
            const float need = snap(pad + headH + 6.0f * S + (float)nl * rowH + pad), dh = jh > need ? jh : need;
            float dy = y2; g_frameJoin = menu_place_at(f, __LINE__, dx, dy, dx, y2 - sl, dw, dh);   // glued beside / under (theme kit)
            draw_themed_box(dev, f.skin, dx, dy, dw, dh, C.muBox, A, S);
            hit_extend(9, dy + dh, f.screenH);               // a long description grows past jh : its clicks stay ours
            const float Ld = dx + pad, hyd = snap(dy + pad + headH * 0.5f), lyd = dy + pad + headH + 6.0f * S;
            rrect(dev, Ld, snap(dy + pad + headH), dw - 2.0f * pad, 1.0f, 0.0f, with_alpha(0x30C8D6EAu, A), with_alpha(0x30C8D6EAu, A));
            fo->begin(dev);
            fo->draw_lc(dev, Ld, hyd, dTitle, tz * 0.95f, with_alpha(0xFFFFFFFFu, A), sh, k.ow);
            for (int i = 0; i < nl; ++i) fo->draw_lc(dev, snap(Ld + 10.0f * S), snap(lyd + (float)i * rowH + rowH * 0.5f), lines[i], tz * 0.95f, with_alpha(0xFFC9D3E0u, A), sh, k.ow);
        }
    }
}

// Alter Ego -> Change Celestite : one box, the type's slots on the left (fe_equip : a Lv badge, the Celestite set) and the
// Celestite owned on the right (fe_inven : icon, name in the client's colour), the description under it. Both lists stay
// the client's (their cursor, their picks, the 0x0C5 it sends) ; a click on the other list hands it the focus first.
namespace {
Block g_celB; unsigned g_celMs = 0;
struct CelText { bool ok; int tries; DWORD next; char name[64]; char desc[400]; };
CelText g_celT[64];
const CelText* cel_text(int n) {                    // item 0x72F0 + n, read once from the item DAT (retried every 2 s, 5 times)
    if (n < 1 || n > 63) return 0;
    CelText& t = g_celT[n];
    if (t.ok) return &t;
    const DWORD now = GetTickCount();
    if (t.tries >= 5 || (t.tries && (int)(now - t.next) < 0)) return 0;
    ++t.tries; t.next = now + 2000;
    static unsigned char rec[0x2BD]; ItemText it;
    if (read_item_record_head(0x72F0u + (unsigned)n, rec, sizeof(rec)) && item_text_parse(rec, sizeof(rec), 0x72F0u + (unsigned)n, it)) {
        snprintf(t.name, sizeof(t.name), "%s", it.name); jp_clean(it.desc, t.desc, sizeof(t.desc)); t.ok = true; return &t;
    }
    if (t.tries == 5) windower::debug::log("NATIVEUI celestite %d : item %u not read (5 tries)", n, 0x72F0u + (unsigned)n);
    return 0;
}
u32 cel_rgb(unsigned c) {                           // the client's row colour, in ours
    if (c == 0x80804020u) return 0xFFFFB45Au;       // the Celestite picked : orange
    if (c == 0x80509050u) return 0xFF7CE07Cu;       // set in this type : green
    if (c == 0x80404040u) return 0xFF6E7C8Eu;       // not changeable here : grey
    return 0xFFE4EAF2u;
}
}
void celestite_draw(const Frame& f) {
    const UiConfig& C = ui_config();
    if (!(C.nuHide & NU_MAIN) || !f.font || f.screenH <= 0.0f || f.screenW <= 0.0f) { g_celB.vis = 0.0f; g_celB.wasOpen = false; return; }
    const unsigned now = GetTickCount();
    float dt = g_celMs ? (float)(now - g_celMs) / 1000.0f : 0.0f; g_celMs = now; if (dt > 0.1f) dt = 0.1f;
    const NativeCelestite& K0 = native_celestite();
    static NativeCelestite last;
    if (K0.open) last = K0;
    if (!block_ease(g_celB, K0.open, now, dt) || last.equip.uiW <= 0) return;
    const Metrics k = metrics(f);
    const float S = k.S, pad = snap(8.0f * S) + k.bi, tz = k.tz, rowH = snap(k.capH * 0.98f), headH = snap(18.0f * S), gap = snap(6.0f * S);
    Font* fo = f.font; const u32 dev = f.dev;
    const float tO = ease_out(((float)(now - g_celB.openMs) / 1000.0f) / 0.08f);
    const float A = g_celB.vis * tO; if (A <= 0.0f) return;
    const u32 sh = with_alpha(0xC8000000u, A);
    static const char* TYPE_EN[6] = { "Tank", "Melee Fighter", "Ranged Fighter", "Offensive Caster", "Healer", "Support" };
    static const char* TYPE_FR[6] = { "Tank", "Combattant au corps \xC3\xA0 corps", "Combattant \xC3\xA0 distance", "Lanceur offensif", "Soigneur", "Soutien" };
    const int nE = last.equip.n - last.equip.top < last.equip.vis ? last.equip.n - last.equip.top : last.equip.vis;
    const int nI = last.inven.n - last.inven.top < last.inven.vis ? last.inven.n - last.inven.top : last.inven.vis;
    float wName = fo->measure("Celestite (M. Def.)", tz);
    for (int i = 0; i < nI; ++i) { const CelText* t = cel_text(last.inven.cel[i]); if (t) { const float w = fo->measure(t->name, tz); if (w > wName) wName = w; } }
    const float badgeW = snap(fo->measure("Lv3", tz * 0.8f) + 10.0f * S), iconZ = snap(rowH - 4.0f * S);
    const float colW = snap(10.0f * S + (badgeW > iconZ ? badgeW : iconZ) + 8.0f * S + wName + 10.0f * S);
    const float bw = keep_width(g_celB, snap(pad + colW + gap + colW + pad));
    const int V = nE > nI ? nE : nI; const int Vr = V < 6 ? 6 : V;
    const float listY = headH + 6.0f * S;
    const float bh = snap(pad + listY + (float)Vr * rowH + pad);
    // the description : what the focused list's hand is on
    int selN = 0;
    if (last.inven.focused) { const int r = last.inven.cursor - last.inven.top; if (r >= 0 && r < nI) selN = last.inven.cel[r]; }
    else { const int r = last.equip.cursor - last.equip.top; if (r >= 0 && r < nE) selN = last.equip.cel[r]; }
    const CelText* sel = cel_text(selN);
    char lines[6][96]; int nl = 0;
    if (sel) nl = jp_wrap(fo, sel->desc, tz * 0.95f, bw - 2.0f * pad - 10.0f * S, lines, 6);
    const float dh = snap(pad + headH + 6.0f * S + (float)(nl > 2 ? nl : 2) * rowH + pad);
    const float sx = f.screenW / (float)last.equip.uiW, sy = f.screenH / (float)last.equip.uiH;
    float bx = snap((float)last.equip.rx0 * sx), by = snap((float)last.equip.ry0 * sy);
    const float totalH = bh + gap + dh;
    if (bx + bw > f.screenW - 4.0f) bx = snap(f.screenW - 4.0f - bw); if (bx < 4.0f) bx = 4.0f;
    if (by + totalH > f.screenH - 4.0f) by = snap(f.screenH - 4.0f - totalH); if (by < 4.0f) by = 4.0f;
    const int jm = menu_place(f, __LINE__, bx, by, bw, bh);
    const float by0 = by;                                    // its rest place : the description is placed from it
    by = snap(by + (1.0f - tO) * 4.0f * S);
    g_frameJoin = jm;
    draw_themed_box(dev, f.skin, bx, by, bw, bh, C.muBox, A, S);
    const float L1 = bx + pad, L2 = bx + pad + colW + gap, ly = snap(by + pad + listY), hy = snap(by + pad + headH * 0.5f);
    rrect(dev, L1, snap(by + pad + headH), bw - 2.0f * pad, 1.0f, 0.0f, with_alpha(0x30C8D6EAu, A), with_alpha(0x30C8D6EAu, A));
    rrect(dev, snap(L2 - gap * 0.5f), ly, 1.0f, (float)Vr * rowH, 0.0f, with_alpha(0x30C8D6EAu, A), with_alpha(0x30C8D6EAu, A));
    for (int i = 0; i < Vr; ++i) if ((i & 1) == 0) {
        rrect(dev, L1, snap(ly + (float)i * rowH), colW, rowH, 3.0f * S, with_alpha(0x14FFFFFFu, A), with_alpha(0x0CFFFFFFu, A));
        rrect(dev, L2, snap(ly + (float)i * rowH), colW, rowH, 3.0f * S, with_alpha(0x14FFFFFFu, A), with_alpha(0x0CFFFFFFu, A));
    }
    // the picks : the slot picked (a gold rim), the Celestite picked (orange, from its colour)
    if (last.picked >= last.equip.top && last.picked < last.equip.top + nE)
        rrect_stroke(dev, L1, snap(ly + (float)(last.picked - last.equip.top) * rowH), colW, rowH, rowH * 0.5f, with_alpha(0xD0FFDA8Au, A), 1.5f * S);
    if (last.pickedRow >= last.inven.top && last.pickedRow < last.inven.top + nI)
        rrect_stroke(dev, L2, snap(ly + (float)(last.pickedRow - last.inven.top) * rowH), colW, rowH, rowH * 0.5f, with_alpha(0xD0FFB45Au, A), 1.5f * S);
    // the focused list's cursor : the party glass and the hand
    const bool invF = last.inven.focused;
    const NativeCelList& FL = invF ? last.inven : last.equip;
    const int fN = invF ? nI : nE, cr = FL.cursor - FL.top;
    const float cX = invF ? L2 : L1;
    if (cr >= 0 && cr < fN) {
        const float targetY = ly + (float)cr * rowH;
        static float lastX = -1.0f;
        if (g_celB.curY < 0.0f || lastX != cX) g_celB.curY = targetY;
        lastX = cX;
        g_celB.curY += (targetY - g_celB.curY) * (1.0f - expf(-dt * 30.0f));
        if (fabsf(targetY - g_celB.curY) < 0.3f) g_celB.curY = targetY;
        const float y = snap(g_celB.curY), r = rowH * 0.5f;
        rrect_clip_begin(dev, cX, y, colW, rowH, r);
        party_selframe(dev, cX, y, colW, rowH, f.t, A, 0);
        rrect_clip_end(dev);
        rrect_stroke(dev, cX, y, colW, rowH, r, with_alpha(0xC8FFDA8Au, A), 1.0f);
        const float hand = snap(k.capH * 1.25f), cy = g_celB.curY + rowH * 0.5f;
        const float hx = cX - hand * 0.35f + party_cursor_bob(f.t, hand);
        if (g_handTex) party_cursor(dev, g_handTex, hx, cy, hand, 0);
    } else g_celB.curY = -1.0f;
    // the Lv badges (gold Lv3, silver Lv2, bronze Lv1) and the icons
    for (int i = 0; i < nE; ++i) {
        const int lv = last.equip.badge[i]; if (lv < 1 || lv > 3) continue;
        static const u32 BC[4] = { 0, 0xFFB07848u, 0xFFB8C2CEu, 0xFFE8C46Au };
        const float y = snap(ly + (float)i * rowH + 3.0f * S);
        rrect(dev, snap(L1 + 8.0f * S), y, badgeW, rowH - 6.0f * S, 3.0f * S, with_alpha(BC[lv], A * 0.95f), with_alpha(mix(BC[lv], 0xFF000000u, 0.35f), A * 0.95f));
    }
    for (int i = 0; i < nI; ++i) {
        const u32 tex = list_icon(f, 3, 0x72F0u + (unsigned)last.inven.cel[i]);
        const float ix = snap(L2 + 8.0f * S), iy = snap(ly + (float)i * rowH + 2.0f * S);
        const float a = A * (last.inven.col[i] == 0x80404040u ? 0.5f : 1.0f);
        if (tex) { dTexQuadState(dev, tex); tquad(dev, ix, iy, iconZ, iconZ, 0.0f, 1.0f, 0.0f, 1.0f, with_alpha(0xFFFFFFFFu, a), with_alpha(0xFFFFFFFFu, a)); dSetTex(dev, 0, 0); cs(dev); }
        else disc(dev, ix + iconZ * 0.5f, iy + iconZ * 0.5f, iconZ * 0.3f, with_alpha(0xFF9FB0C6u, a));
    }
    // the mouse : a row of the focused list moves its hand ; a click picks (the other list is handed the focus first)
    {
        const MouseState* ms = f.mouse;
        if (K0.open) {
            g_hit[9][0] = bx / f.screenW; g_hit[9][1] = by / f.screenH; g_hit[9][2] = bw / f.screenW; g_hit[9][3] = totalH / f.screenH;
            int list = -1, row = -1;
            if (ms && ms->focused && ms->y >= ly) {
                const int r = (int)((ms->y - ly) / rowH);
                if (ms->x >= L1 && ms->x < L1 + colW && r < nE) { list = 0; row = last.equip.top + r; }
                if (ms->x >= L2 && ms->x < L2 + colW && r < nI) { list = 1; row = last.inven.top + r; }
            }
            const int key = list < 0 ? 0 : (list * 100 + row + 1);
            if (list >= 0 && key != g_celB.hoverRow && (list == 1) == invF && row != FL.cursor) native_celestite_hover(list, row);
            g_celB.hoverRow = key;
            if (list >= 0 && ms->clicked) native_celestite_pick(list, row);
        }
    }
    fo->begin(dev);
    fo->draw_lc(dev, L1, hy, last.type >= 0 && last.type < 6 ? tr(TYPE_EN[last.type], TYPE_FR[last.type]) : "?", tz * 0.95f, with_alpha(0xFFE8D9B0u, A), sh, k.ow);
    if (!last.mayChange) { const char* m = tr("Mog House only", "Mog House uniquement"); fo->draw_lc(dev, snap(bx + bw - pad - fo->measure(m, tz * 0.85f)), hy, m, tz * 0.85f, with_alpha(0xFF93A6BEu, A), sh, k.ow); }
    for (int i = 0; i < nE; ++i) {
        const float cy = snap(ly + (float)i * rowH + rowH * 0.5f);
        const int lv = last.equip.badge[i];
        if (lv >= 1 && lv <= 3) { char b[8]; sprintf(b, "Lv%d", lv); fo->draw_lc(dev, snap(L1 + 8.0f * S + (badgeW - fo->measure(b, tz * 0.8f)) * 0.5f), cy, b, tz * 0.8f, with_alpha(0xFF1A1E26u, A), with_alpha(0x60FFFFFFu, A), 0.0f); }
        const CelText* t = cel_text(last.equip.cel[i]);
        const float nx = snap(L1 + 8.0f * S + badgeW + 8.0f * S);
        if (last.equip.cel[i]) fo->draw_lc(dev, nx, cy, t ? t->name : "...", tz, with_alpha(!invF && i == cr ? 0xFFFFFFFFu : 0xFFE4EAF2u, A), sh, k.ow);
        else fo->draw_lc(dev, nx, cy, "-", tz, with_alpha(0xFF6E7C8Eu, A), sh, k.ow);
    }
    for (int i = 0; i < nI; ++i) {
        const float cy = snap(ly + (float)i * rowH + rowH * 0.5f);
        const CelText* t = cel_text(last.inven.cel[i]);
        fo->draw_lc(dev, snap(L2 + 8.0f * S + iconZ + 8.0f * S), cy, t ? t->name : "...", tz, with_alpha(cel_rgb(last.inven.col[i]), A), sh, k.ow);
    }
    // ======== the description ========
    float dx = bx, dy = snap(by + bh + gap);
    g_frameJoin = menu_place_at(f, __LINE__, dx, dy, bx, dy - (by - by0), bw, dh);   // glued under the slots (theme kit)
    draw_themed_box(dev, f.skin, dx, dy, bw, dh, C.muBox, A, S);
    if (sel) {
        const float Ld = dx + pad, hyd = snap(dy + pad + headH * 0.5f), lyd = dy + pad + headH + 6.0f * S;
        rrect(dev, Ld, snap(dy + pad + headH), bw - 2.0f * pad, 1.0f, 0.0f, with_alpha(0x30C8D6EAu, A), with_alpha(0x30C8D6EAu, A));
        fo->begin(dev);
        fo->draw_lc(dev, Ld, hyd, sel->name, tz * 0.95f, with_alpha(0xFFFFFFFFu, A), sh, k.ow);
        for (int i = 0; i < nl; ++i) fo->draw_lc(dev, snap(Ld + 10.0f * S), snap(lyd + (float)i * rowH + rowH * 0.5f), lines[i], tz * 0.95f, with_alpha(0xFFC9D3E0u, A), sh, k.ow);
    }
}

// Status -> Merit Points -> Categories (meritcat), laid out as Job Points : on top the categories, or a category's merits
// (job categories in two groups, each under its header) ; under it, the merit points and the category's Combo Total/Max
// (meritinf) on the left, the description on the right. Names / descriptions : the client's d_msg (native_merit_text).
namespace { Block g_mcB; unsigned g_mcMs = 0; }
void meritcat_draw(const Frame& f) {
    const UiConfig& C = ui_config();
    if (!(C.nuHide & NU_MAIN) || !f.font || f.screenH <= 0.0f || f.screenW <= 0.0f) { g_mcB.vis = 0.0f; g_mcB.wasOpen = false; return; }
    const unsigned now = GetTickCount();
    float dt = g_mcMs ? (float)(now - g_mcMs) / 1000.0f : 0.0f; g_mcMs = now; if (dt > 0.1f) dt = 0.1f;
    const NativeMeritCat& K0 = native_merit_cat();
    static NativeMeritCat last;
    if (K0.open) last = K0;
    if (!block_ease(g_mcB, K0.open, now, dt) || last.uiW <= 0) return;
    const Metrics k = metrics(f);
    const float S = k.S, pad = snap(8.0f * S) + k.bi, tz = k.tz, rowH = snap(k.capH * 0.92f), headH = snap(18.0f * S), gap = snap(6.0f * S);
    Font* fo = f.font; const u32 dev = f.dev;
    const float tO = ease_out(((float)(now - g_mcB.openMs) / 1000.0f) / 0.08f);
    const float A = g_mcB.vis * tO; if (A <= 0.0f) return;
    const u32 sh = with_alpha(0xC8000000u, A), GREY = 0x80404040u, BLUE = 0x802060A0u;
    const bool merits = last.cat != 0;
    const int nRows = last.n - last.top < last.vis ? last.n - last.top : last.vis, V = last.vis;
    // the rows' texts
    char names[16][96]; int grp[16];
    { int g = last.top > 0 ? 1 : 0; for (int i = 0; i < nRows; ++i) { if (last.row[i].header) ++g; grp[i] = g; } }
    float wName = 0.0f;
    for (int i = 0; i < nRows; ++i) {
        const NativeMeritRow& r = last.row[i];
        if (r.header) sprintf(names[i], tr("Group %d", "Groupe %d"), grp[i]);
        else { jp_clean(native_merit_text(r.id, false), names[i], sizeof(names[i])); if (!names[i][0]) strcpy(names[i], "..."); }
        const float w = fo->measure(names[i], tz); if (w > wName) wName = w;
    }
    const float wRight = merits ? fo->measure("15", tz) + 18.0f * S + fo->measure("NEXT 99 P", tz) : fo->measure("G1 10/10   G2 10/10", tz * 0.9f);
    float bw = snap(pad + 8.0f * S + wName + 28.0f * S + wRight + 8.0f * S + pad);
    bw = keep_width(g_mcB, bw);
    // a category's state : spent / cap of a group (cat, or cat + 26 its group 2) ; full = capped
    auto grpTxt = [&](int c, char* out) -> int { const int sp = c >= 0 && c < 96 ? last.spentAll[c] : 0, cp = c >= 0 && c < 96 ? last.capAll[c] : -1;
        if (cp <= 0) { out[0] = 0; return 0; } sprintf(out, "%d/%d", sp, cp); return sp >= cp ? 2 : sp > 0 ? 1 : 0; };
    const char* l1 = tr("Merit Points", "Merit Points"); const char* l2 = tr("Combo Total/Max", "Total / max");
    const float jw = snap(pad + (fo->measure(l2, tz) > fo->measure(l1, tz) ? fo->measure(l2, tz) : fo->measure(l1, tz)) + 16.0f * S + fo->measure("10/10 152/152", tz) + pad);
    const float jh = snap(pad + headH + 6.0f * S + 2.0f * rowH + pad);
    { const float need = jw + gap + snap(560.0f * S); if (bw < need) bw = snap(need); }
    const float listY = headH + 6.0f * S, bh = snap(pad + listY + (float)V * rowH + pad);
    // the description : what the hand is on
    char dTitle[96] = "", dBody[400] = "";
    const int cr = last.cursor >= last.top && last.cursor < last.top + nRows ? last.cursor - last.top : -1;
    if (cr >= 0 && !last.row[cr].header) { strcpy(dTitle, names[cr]); jp_clean(native_merit_text(last.row[cr].id, true), dBody, sizeof(dBody)); }
    const float sx = f.screenW / (float)last.uiW, sy = f.screenH / (float)last.uiH;
    float bx = snap((float)last.rx0 * sx), by = snap((float)last.ry0 * sy);
    const float totalH = bh + gap + jh;
    if (bx + bw > f.screenW - 4.0f) bx = snap(f.screenW - 4.0f - bw); if (bx < 4.0f) bx = 4.0f;
    if (by + totalH > f.screenH - 4.0f) by = snap(f.screenH - 4.0f - totalH); if (by < 4.0f) by = 4.0f;
    const int jm = menu_place(f, __LINE__, bx, by, bw, bh);
    const float by0 = by;                                    // its rest place : the boxes under it are placed from it
    by = snap(by + (1.0f - tO) * 4.0f * S);
    // ======== the list ========
    g_frameJoin = jm;
    draw_themed_box(dev, f.skin, bx, by, bw, bh, C.muBox, A, S);
    const float L = bx + pad, W = bw - 2.0f * pad, ly = snap(by + pad + listY), hy = snap(by + pad + headH * 0.5f);
    rrect(dev, L, snap(by + pad + headH), W, 1.0f, 0.0f, with_alpha(0x30C8D6EAu, A), with_alpha(0x30C8D6EAu, A));
    for (int i = 0; i < nRows; ++i) {
        const float y = snap(ly + (float)i * rowH);
        if (last.row[i].header) {                              // the group's gauge : gold filling, blue once capped
            const int c = grp[i] == 2 ? last.cat + 26 : last.cat;
            const int sp = c < 96 ? last.spentAll[c] : 0, cp = last.row[i].cap > 0 ? last.row[i].cap : (c < 96 ? last.capAll[c] : 0);
            const float gx = snap(L + 4.0f * S + fo->measure(names[i], tz * 0.9f) + 14.0f * S), gw = snap(L + W - 8.0f * S - fo->measure("10/10  MAX", tz * 0.9f) - 12.0f * S - gx);
            const float gy = snap(y + rowH * 0.5f - 2.0f * S);
            rrect(dev, L, snap(y + rowH - 2.0f * S), W, 1.0f, 0.0f, with_alpha(0x40FFDA8Au, A), with_alpha(0x40FFDA8Au, A));
            if (gw > 20.0f * S && cp > 0) {
                const float fr = sp >= cp ? 1.0f : (float)sp / (float)cp;
                rrect(dev, gx, gy, gw, 4.0f * S, 2.0f * S, with_alpha(0x40FFFFFFu, A), with_alpha(0x30FFFFFFu, A));
                if (fr > 0.0f) rrect(dev, gx, gy, snap(gw * fr), 4.0f * S, 2.0f * S, with_alpha(sp >= cp ? 0xF06FB4FFu : 0xE0FFDA8Au, A), with_alpha(sp >= cp ? 0xD04A90E0u : 0xC0E8B860u, A));
            }
        }
        else if (((i + last.top) & 1) == 0) rrect(dev, L, y, W, rowH, 3.0f * S, with_alpha(0x18FFFFFFu, A), with_alpha(0x10FFFFFFu, A));
    }
    if (cr >= 0) {
        const float targetY = ly + (float)cr * rowH;
        if (g_mcB.curY < 0.0f) g_mcB.curY = targetY;
        g_mcB.curY += (targetY - g_mcB.curY) * (1.0f - expf(-dt * 30.0f));
        if (fabsf(targetY - g_mcB.curY) < 0.3f) g_mcB.curY = targetY;
        const float y = snap(g_mcB.curY), r = rowH * 0.5f;
        rrect_clip_begin(dev, L, y, W, rowH, r);
        party_selframe(dev, L, y, W, rowH, f.t, A, 0);
        rrect_clip_end(dev);
        rrect_stroke(dev, L, y, W, rowH, r, with_alpha(0xC8FFDA8Au, A), 1.0f);
        if (last.focused) {
            const float hand = snap(k.capH * 1.25f), cy = g_mcB.curY + rowH * 0.5f;
            const float hx = bx - hand * 0.35f + party_cursor_bob(f.t, hand);
            if (g_handTex) party_cursor(dev, g_handTex, hx, cy, hand, 0);
        }
    } else g_mcB.curY = -1.0f;
    if (last.n > last.vis) {
        const float rx = bx + bw - k.bi - (pad - k.bi) * 0.5f - 1.0f * S, th = (float)V * rowH;
        const float th2 = th * (float)last.vis / (float)last.n, ty2 = ly + th * (float)last.top / (float)last.n;
        rrect(dev, rx, ly, 2.0f * S, th, 1.0f * S, with_alpha(0x30FFFFFFu, A), with_alpha(0x30FFFFFFu, A));
        rrect(dev, rx, snap(ty2), 2.0f * S, snap(th2 < 6.0f * S ? 6.0f * S : th2), 1.0f * S, with_alpha(0xC0FFDA8Au, A), with_alpha(0xC0FFDA8Au, A));
    }
    {   // the mouse : the client's hand follows (its page scrolled first), a click validates
        const MouseState* ms = f.mouse;
        int mrow = -1;
        if (K0.open && last.focused && ms && ms->focused && ms->x >= L && ms->x < L + W && ms->y >= ly && ms->y < ly + (float)nRows * rowH)
            mrow = last.top + (int)((ms->y - ly) / rowH);
        if (K0.open) {
            g_hit[9][0] = bx / f.screenW; g_hit[9][1] = by / f.screenH; g_hit[9][2] = bw / f.screenW; g_hit[9][3] = totalH / f.screenH;
            if (mrow >= 0 && mrow + 1 != g_mcB.hoverRow && mrow != K0.cursor) native_textlist_hover("meritcat", mrow);
            g_mcB.hoverRow = mrow + 1;
            if (mrow >= 0 && ms->clicked) native_textlist_pick("meritcat", mrow);
        }
    }
    fo->begin(dev);
    {
        char title[96]; if (merits) { jp_clean(native_merit_text(last.cat, false), title, sizeof(title)); if (!title[0]) strcpy(title, "..."); } else strcpy(title, tr("Categories", "Cat\xC3\xA9gories"));
        fo->draw_lc(dev, L, hy, title, tz * 0.95f, with_alpha(0xFFE8D9B0u, A), sh, k.ow);
    }
    for (int i = 0; i < nRows; ++i) {
        const NativeMeritRow& r = last.row[i];
        const float cy = snap(ly + (float)i * rowH + rowH * 0.5f);
        const bool me = i == cr;
        if (r.header) {
            fo->draw_lc(dev, snap(L + 4.0f * S), cy, names[i], tz * 0.9f, with_alpha(0xFFE8D9B0u, A), sh, k.ow);
            const int c = grp[i] == 2 ? last.cat + 26 : last.cat;
            const int sp = c < 96 ? last.spentAll[c] : 0, cp = r.cap > 0 ? r.cap : (c < 96 ? last.capAll[c] : 0);
            if (cp > 0) {
                char t[24]; const bool full = sp >= cp;
                sprintf(t, full ? "%d/%d  MAX" : "%d/%d", sp, cp);
                fo->draw_lc(dev, snap(L + W - 8.0f * S - fo->measure(t, tz * 0.9f)), cy, t, tz * 0.9f, with_alpha(full ? 0xFF6FB4FFu : 0xFFF2F5FAu, A), sh, k.ow);
            }
            continue;
        }
        const bool grey = merits && r.colName == GREY, maxed = merits && r.colLvl == BLUE;
        fo->draw_lc(dev, snap(L + 8.0f * S), cy, names[i], tz, with_alpha(grey ? 0xFF6E7C8Eu : me ? 0xFFFFFFFFu : 0xFFC9D3E0u, A), sh, k.ow);
        if (!merits) {                                         // the category's state : spent / cap (a job : its two groups)
            static const u32 SC[3] = { 0xFF93A6BEu, 0xFFE8D9B0u, 0xFF6FB4FFu };   // none spent, some, capped
            const float z = tz * 0.9f; float x = L + W - 8.0f * S;
            char a1[16], a2[16]; const int s1 = grpTxt(r.id, a1);
            if (r.id >= 6 && r.id <= 28 && r.id != 26) {
                const int s2 = grpTxt(r.id + 26, a2);
                if (a2[0]) { x -= fo->measure(a2, z); fo->draw_lc(dev, snap(x), cy, a2, z, with_alpha(SC[s2], A), sh, k.ow); x -= fo->measure("G2 ", z); fo->draw_lc(dev, snap(x), cy, "G2", z, with_alpha(0xFF6E7C8Eu, A), sh, k.ow); x -= 12.0f * S; }
                if (a1[0]) { x -= fo->measure(a1, z); fo->draw_lc(dev, snap(x), cy, a1, z, with_alpha(SC[s1], A), sh, k.ow); x -= fo->measure("G1 ", z); fo->draw_lc(dev, snap(x), cy, "G1", z, with_alpha(0xFF6E7C8Eu, A), sh, k.ow); }
            } else if (a1[0]) { x -= fo->measure(a1, z); fo->draw_lc(dev, snap(x), cy, a1, z, with_alpha(SC[s1], A), sh, k.ow); }
            continue;
        }
        char lv[8]; sprintf(lv, "%d", r.lvl);
        const float costW = fo->measure("NEXT 99 P", tz);
        fo->draw_lc(dev, snap(L + W - 8.0f * S - costW - 18.0f * S - fo->measure(lv, tz)), cy, lv, tz, with_alpha(maxed ? 0xFF6FB4FFu : grey ? 0xFF6E7C8Eu : 0xFFF2F5FAu, A), sh, k.ow);
        if (r.cost >= 0 && !maxed) {
            char c[24]; sprintf(c, "NEXT %d P", r.cost);
            fo->draw_lc(dev, snap(L + W - 8.0f * S - fo->measure(c, tz)), cy, c, tz, with_alpha(r.colCost == GREY || grey ? 0xFF6E7C8Eu : 0xFFE8D9B0u, A), sh, k.ow);
        }
    }
    // ======== under it : the merit points and the Combo Total/Max (left), the description (right) ========
    const float y2 = snap(by + bh + gap), sl = by - by0;     // sl : the opening slide, shared by the boxes under it
    float jx = bx, jy = y2;
    g_frameJoin = menu_place_at(f, __LINE__, jx, jy, bx, y2 - sl, jw, jh);   // glued under the list (theme kit)
    draw_themed_box(dev, f.skin, jx, jy, jw, jh, C.muBox, A, S);
    {
        const float Lb = jx + pad, Rb = jx + jw - pad, lyb = jy + pad;
        char v1[24] = "- P", v2[40] = "";
        if (last.meritsLeft >= 0) sprintf(v1, "%d P", last.meritsLeft);
        char a2[16] = "", b2[16] = "";
        if (last.spent >= 0 && last.cap > 0) sprintf(a2, "%d/%d", last.spent, last.cap);
        if (last.spent2 >= 0 && last.cap2 > 0) sprintf(b2, "%d/%d", last.spent2, last.cap2);
        if (b2[0]) sprintf(v2, "%s  %s", b2, a2); else strcpy(v2, a2);
        const float c1 = snap(lyb + rowH * 0.5f), c2 = snap(lyb + headH + 6.0f * S + rowH * 0.5f), c3 = snap(lyb + headH + 6.0f * S + rowH * 1.5f);
        rrect(dev, Lb, snap(lyb + headH), Rb - Lb, 1.0f, 0.0f, with_alpha(0x30C8D6EAu, A), with_alpha(0x30C8D6EAu, A));
        fo->begin(dev);
        fo->draw_lc(dev, Lb, c1, l1, tz * 0.95f, with_alpha(0xFFE8D9B0u, A), sh, k.ow);
        fo->draw_lc(dev, snap(Rb - fo->measure(v1, tz)), c1, v1, tz, with_alpha(0xFFF2F5FAu, A), sh, k.ow);
        fo->draw_lc(dev, Lb, c2, l2, tz, with_alpha(0xFF93A6BEu, A), sh, k.ow);
        const bool full = last.cap > 0 && last.spent >= last.cap;
        if (v2[0]) fo->draw_lc(dev, snap(Rb - fo->measure(v2, tz)), c3, v2, tz, with_alpha(full ? 0xFF6FB4FFu : 0xFFF2F5FAu, A), sh, k.ow);
    }
    if (dTitle[0]) {
        float dx = snap(bx + jw + gap); const float dw = snap(bw - jw - gap);
        char lines[6][96]; const int nl = jp_wrap(fo, dBody, tz * 0.95f, dw - 2.0f * pad - 10.0f * S, lines, 6);
        const float need = snap(pad + headH + 6.0f * S + (float)nl * rowH + pad), dh = jh > need ? jh : need;
        float dy = y2; g_frameJoin = menu_place_at(f, __LINE__, dx, dy, dx, y2 - sl, dw, dh);   // glued beside / under (theme kit)
        draw_themed_box(dev, f.skin, dx, dy, dw, dh, C.muBox, A, S);
        hit_extend(9, dy + dh, f.screenH);                   // a long description grows past jh : its clicks stay ours
        const float Ld = dx + pad, hyd = snap(dy + pad + headH * 0.5f), lyd = dy + pad + headH + 6.0f * S;
        rrect(dev, Ld, snap(dy + pad + headH), dw - 2.0f * pad, 1.0f, 0.0f, with_alpha(0x30C8D6EAu, A), with_alpha(0x30C8D6EAu, A));
        fo->begin(dev);
        fo->draw_lc(dev, Ld, hyd, dTitle, tz * 0.95f, with_alpha(0xFFFFFFFFu, A), sh, k.ow);
        for (int i = 0; i < nl; ++i) fo->draw_lc(dev, snap(Ld + 10.0f * S), snap(lyd + (float)i * rowH + rowH * 0.5f), lines[i], tz * 0.95f, with_alpha(0xFFC9D3E0u, A), sh, k.ow);
    }
}

// The main menu's Equipment : the 4 x 4 grid (the client's order : Main Sub Range Ammo / Head Neck L.Ear R.Ear / Body
// Hands L.Ring R.Ring / Back Waist Legs Feet), Attack / Defense and the 8 resists under it ; the slot's item list beside
// it (the client's colours) ; the item card of what the hand is on. The client does the equipping (0x050).
namespace { Block g_eqB, g_elB; unsigned g_eqMs = 0; bool g_eqPreview = false; }
bool equip_preview_toggle() { g_eqPreview = !g_eqPreview; return g_eqPreview; }
void equipment_draw(const Frame& f) {
    const UiConfig& C = ui_config();
    if (!(C.nuHide & NU_MAIN) || !f.font || f.screenH <= 0.0f || f.screenW <= 0.0f) { g_eqB.vis = g_elB.vis = 0.0f; g_eqB.wasOpen = g_elB.wasOpen = false; return; }
    const unsigned now = GetTickCount();
    float dt = g_eqMs ? (float)(now - g_eqMs) / 1000.0f : 0.0f; g_eqMs = now; if (dt > 0.1f) dt = 0.1f;
    const NativeEquip& E0 = native_equip();
    static NativeEquip last;
    if (E0.open) last = E0;
    if (!block_ease(g_eqB, E0.open, now, dt) || last.uiW <= 0) { g_elB.vis = 0.0f; return; }
    unsigned encMask = party().encumbrance();
    static NativeEquip shown;                                            // //aio eqpreview : every state, on the real items (copied only then)
    if (g_eqPreview) {
        shown = last;
        encMask = 0;
        for (int s = 0; s < 16; ++s) {
            shown.ward[s] = (unsigned char)(s % 9);                     // 0 the inventory (no badge), 1..8 the wardrobes
            if (s % 4 == 3) encMask |= 1u << s;                          // every 4th slot locked (Encumbrance)
        }
        shown.id[15] = 0;                                                // Back : an empty slot
        static const unsigned COLS[4] = { 0x80509050u, 0x80404040u, 0x80804020u, 0x80808080u };   // worn, refused, held, normal
        for (int i = 0; i < 16; ++i) shown.lrow[i].col = COLS[i % 4];
    }
    const NativeEquip& lastS = g_eqPreview ? shown : last;
    const Metrics k = metrics(f);
    const float S = k.S, pad = snap(8.0f * S) + k.bi, tz = k.tz, headH = snap(18.0f * S), gap = snap(6.0f * S);
    Font* fo = f.font; const u32 dev = f.dev;
    const float tO = ease_out(((float)(now - g_eqB.openMs) / 1000.0f) / 0.08f);
    const float A = g_eqB.vis * tO; if (A <= 0.0f) return;
    const u32 sh = with_alpha(0xC8000000u, A);
    // the client's grid, row-major on screen ; its entries are column-major
    static const int SLOT_AT[16] = { 0, 1, 2, 3, 4, 9, 11, 12, 5, 6, 13, 14, 15, 10, 7, 8 };
    static const int ENTRY_OF[16] = { 1, 5, 9, 13, 2, 3, 7, 12, 16, 6, 8, 10, 14, 11, 15, 4 };   // by packet slot
    static const char* SLOT_EN[16] = { "Main", "Sub", "Range", "Ammo", "Head", "Body", "Hands", "Legs", "Feet", "Neck", "Waist", "L.Ear", "R.Ear", "L.Ring", "R.Ring", "Back" };
    const float cell = snap(k.capH * 2.1f), cg = snap(4.0f * S);
    const float gridW = 4.0f * cell + 3.0f * cg;
    const float statH = snap(k.capH * 0.9f);
    const float bw = snap(pad + gridW + pad), bh = snap(pad + headH + 6.0f * S + gridW + (last.setMode ? 0.0f : 8.0f * S + 3.0f * statH) + pad);
    const float sx = f.screenW / (float)last.uiW, sy = f.screenH / (float)last.uiH;
    float bx = snap((float)last.rx0 * sx), by = snap((float)last.ry0 * sy);
    if (bx + bw > f.screenW - 4.0f) bx = snap(f.screenW - 4.0f - bw); if (bx < 4.0f) bx = 4.0f;
    if (by + bh > f.screenH - 4.0f) by = snap(f.screenH - 4.0f - bh); if (by < 4.0f) by = 4.0f;
    const int eqKey = __LINE__, jm = menu_place(f, eqKey, bx, by, bw, bh);
    const float by0 = by;                                    // its rest place : the list beside it is placed from it
    by = snap(by + (1.0f - tO) * 4.0f * S);
    g_frameJoin = jm;
    draw_themed_box(dev, f.skin, bx, by, bw, bh, C.muBox, A, S);
    const float L = bx + pad, hy = snap(by + pad + headH * 0.5f), gy = snap(by + pad + headH + 6.0f * S);
    rrect(dev, L, snap(by + pad + headH), gridW, 1.0f, 0.0f, with_alpha(0x30C8D6EAu, A), with_alpha(0x30C8D6EAu, A));
    // the procedural frame (kittest iron) : the user 2026-11-20 -- "la plaque de metal toujours pleine, et tu creuses ce
    // qu'on a besoin" (an island plate read as a frame inside the frame ; wells in the glass were not it either)
    // the WHOLE box is the plate (a solid one : welded to its border) ; milled out of it : the 16 slots, and the stats block
    // as one glass pane from just above them to the border (left, right, bottom : no thin metal strip along the border)
    menu_cut(eqKey, 1, 0.0f, 0.0f, bw, bh, 0.0f);
    for (int c = 0; c < 16; ++c) menu_cut(eqKey, 0, snap(L + (float)(c % 4) * (cell + cg)) - bx, snap(gy + (float)(c / 4) * (cell + cg)) - by, cell, cell, 5.0f * S);
    if (!last.setMode) menu_cut(eqKey, 0, k.bi, snap(gy + gridW + 4.0f * S) - by, bw - 2.0f * k.bi, bh - k.bi - (snap(gy + gridW + 4.0f * S) - by), 6.0f * S);
    int curCell = -1;
    for (int c = 0; c < 16; ++c) if (ENTRY_OF[SLOT_AT[c]] == last.cursor) curCell = c;
    // the cells : a dark well, the item's icon (or the slot's name), the hand's cell in the party glass
    const MouseState* ms = f.mouse; int mcell = -1;
    for (int c = 0; c < 16; ++c) {
        const float cx = snap(L + (float)(c % 4) * (cell + cg)), cy = snap(gy + (float)(c / 4) * (cell + cg));
        if (!theme_kit_bakes_slots()) {                          // the iron frame bakes them as wells : not twice
            rrect(dev, cx, cy, cell, cell, 5.0f * S, with_alpha(0x70101826u, A), with_alpha(0x70080C14u, A));
            rrect_stroke(dev, cx, cy, cell, cell, 5.0f * S, with_alpha(0x30C8D6EAu, A), 1.0f);
        }
        if (c == curCell) {
            rrect_clip_begin(dev, cx, cy, cell, cell, 5.0f * S);
            party_selframe(dev, cx, cy, cell, cell, f.t, A, 0);
            rrect_clip_end(dev);
            rrect_stroke(dev, cx, cy, cell, cell, 5.0f * S, with_alpha(0xD8FFDA8Au, A), 1.5f * S);
        }
        const unsigned id = lastS.id[SLOT_AT[c]];
        if (id) {
            const u32 tex = list_icon(f, 3, id);
            const float z = snap(cell - 8.0f * S);
            if (tex) { dTexQuadState(dev, tex); tquad(dev, snap(cx + 4.0f * S), snap(cy + 4.0f * S), z, z, 0.0f, 1.0f, 0.0f, 1.0f, with_alpha(0xFFFFFFFFu, A), with_alpha(0xFFFFFFFFu, A)); dSetTex(dev, 0, 0); cs(dev); }
        }
        if (id && lastS.ward[SLOT_AT[c]]) {                     // the wardrobe it comes from : a small plate, top right
            const float bz = snap(cell * 0.34f);
            rrect(dev, snap(cx + cell - bz - 2.0f * S), snap(cy + 2.0f * S), bz, bz, 3.0f * S, with_alpha(0xE0303848u, A), with_alpha(0xE0181E28u, A));
            rrect_stroke(dev, snap(cx + cell - bz - 2.0f * S), snap(cy + 2.0f * S), bz, bz, 3.0f * S, with_alpha(0x80FFDA8Au, A), 1.0f);
        }
        // ENCUMBRANCE : a slot locked by it (0x01B flags, bit = packet slot) gets the Equip Viewer's red cross
        if ((encMask >> SLOT_AT[c]) & 1u) {
            const float m = snap(6.0f * S), th = snap(2.5f * S); const u32 red = with_alpha(0xD2FF3838u, A);
            seg_soft(dev, cx + m, cy + m, cx + cell - m, cy + cell - m, th, red);
            seg_soft(dev, cx + cell - m, cy + m, cx + m, cy + cell - m, th, red);
        }
        if (ms && ms->focused && ms->x >= cx && ms->x < cx + cell && ms->y >= cy && ms->y < cy + cell) mcell = c;
    }
    if (curCell >= 0 && last.focused) {
        const float cx = snap(L + (float)(curCell % 4) * (cell + cg)), cy = snap(gy + (float)(curCell / 4) * (cell + cg));
        const float hand = snap(k.capH * 1.25f);
        const float hx = cx - hand * 0.35f + party_cursor_bob(f.t, hand);
        if (g_handTex) party_cursor(dev, g_handTex, hx, cy + cell * 0.5f, hand, 0);
    }
    if (E0.open) {
        g_hit[9][0] = bx / f.screenW; g_hit[9][1] = by / f.screenH; g_hit[9][2] = bw / f.screenW; g_hit[9][3] = bh / f.screenH;
        const int ent = mcell >= 0 ? ENTRY_OF[SLOT_AT[mcell]] : 0;
        if (ent && ent != g_eqB.hoverRow && ent != E0.cursor && E0.focused) native_equip_hover(ent);
        g_eqB.hoverRow = ent;
        if (ent && ms->clicked) native_equip_pick(ent);
    }
    // the stats : Attack, Defense ; the 8 resists, a dot of their element's colour
    static const u32 ELC[8] = { 0xFFE85A4Au, 0xFF7FD4F0u, 0xFF6CD48Au, 0xFFC8A060u, 0xFFB07CE8u, 0xFF5A8CE8u, 0xFFF0F0E0u, 0xFF8A6AA8u };   // fire ice wind earth lightning water light dark
    const float sy0 = gy + gridW + 8.0f * S;
    const float colW = gridW / 4.0f;
    for (int i = 0; i < 8 && !last.setMode; ++i) {
        const float dx = snap(L + (float)(i / 2) * colW + 5.0f * S), dyc = snap(sy0 + statH * (1.5f + (float)(i % 2)));   // column-major, as the client
        disc(dev, dx, dyc, 3.2f * S, with_alpha(ELC[i], A));
    }
    fo->begin(dev);
    fo->draw_lc(dev, L, hy, last.setMode ? (last.setName[0] ? last.setName : tr("Equipment Set", "Set")) : tr("Equipment", "\xC3\x89quipement"), tz * 0.95f, with_alpha(0xFFE8D9B0u, A), sh, k.ow);
    if (curCell >= 0) { const char* sn = SLOT_EN[SLOT_AT[curCell]]; fo->draw_lc(dev, snap(L + gridW - fo->measure(sn, tz * 0.85f)), hy, sn, tz * 0.85f, with_alpha(0xFF93A6BEu, A), sh, k.ow); }
    for (int c = 0; c < 16; ++c) if (lastS.id[SLOT_AT[c]] && lastS.ward[SLOT_AT[c]]) {
        const float cx = snap(L + (float)(c % 4) * (cell + cg)), cy = snap(gy + (float)(c / 4) * (cell + cg));
        const float bz = snap(cell * 0.34f); char w[4]; sprintf(w, "%d", lastS.ward[SLOT_AT[c]]);
        fo->draw_lc(dev, snap(cx + cell - bz - 2.0f * S + (bz - fo->measure(w, tz * 0.75f)) * 0.5f), snap(cy + 2.0f * S + bz * 0.5f), w, tz * 0.75f, with_alpha(0xFFFFE6A8u, A), sh, k.ow);
    }
    for (int c = 0; c < 16; ++c) if (!lastS.id[SLOT_AT[c]]) {
        const float cx = snap(L + (float)(c % 4) * (cell + cg)), cy = snap(gy + (float)(c / 4) * (cell + cg));
        const char* sn = SLOT_EN[SLOT_AT[c]]; const float z = tz * 0.72f;
        const bool kp = last.setMode && last.keep[SLOT_AT[c]];                // the set keeps what is worn there
        fo->draw_lc(dev, snap(cx + (cell - fo->measure(sn, z)) * 0.5f), snap(cy + cell * (kp ? 0.36f : 0.5f)), sn, z, with_alpha(0xFF56627Au, A), sh, k.ow);
        if (kp) { const char* kt = tr("Keep", "Garder"); fo->draw_lc(dev, snap(cx + (cell - fo->measure(kt, z)) * 0.5f), snap(cy + cell * 0.68f), kt, z, with_alpha(0xFFFFDA8Au, A), sh, k.ow); }
    }
    if (last.statsOk && !last.setMode) {
        char a[24], d[24]; sprintf(a, "%d", last.atk); sprintf(d, "%d", last.def);
        const float r0 = snap(sy0 + statH * 0.5f);
        fo->draw_lc(dev, L, r0, tr("Attack", "Attaque"), tz * 0.9f, with_alpha(0xFF93A6BEu, A), sh, k.ow);
        fo->draw_lc(dev, snap(L + colW * 2.0f - 10.0f * S - fo->measure(a, tz * 0.9f)), r0, a, tz * 0.9f, with_alpha(0xFFF2F5FAu, A), sh, k.ow);
        fo->draw_lc(dev, snap(L + colW * 2.0f + 4.0f * S), r0, tr("Defense", "D\xC3\xA9""fense"), tz * 0.9f, with_alpha(0xFF93A6BEu, A), sh, k.ow);
        fo->draw_lc(dev, snap(L + gridW - fo->measure(d, tz * 0.9f)), r0, d, tz * 0.9f, with_alpha(0xFFF2F5FAu, A), sh, k.ow);
        for (int i = 0; i < 8; ++i) {
            char v[12]; sprintf(v, "%d", (int)last.res[i]);
            const float dx = snap(L + (float)(i / 2) * colW + 12.0f * S), dyc = snap(sy0 + statH * (1.5f + (float)(i % 2)));
            fo->draw_lc(dev, dx, dyc, v, tz * 0.9f, with_alpha(0xFFE4EAF2u, A), sh, k.ow);
        }
    }
    // ======== Current Gil (the client's `money` box), where it puts it ========
    if (last.money.open && last.gilOk && last.money.uiW > 0) {
        const float msx = f.screenW / (float)last.money.uiW, msy = f.screenH / (float)last.money.uiH;
        char g[32]; thousands(last.gil, g); strcat(g, " G");
        const char* gl = tr("Current Gil", "Gils");
        float gw = snap((float)(last.money.rx1 - last.money.rx0) * msx) + 2.0f * k.bi;   // the client's width + the frame border
        { const float need = snap(2.0f * pad + fo->measure(gl, tz) + 24.0f * S); if (gw < need) gw = need; const float n2 = snap(2.0f * pad + fo->measure(g, tz)); if (gw < n2) gw = n2; }
        const float gh = snap(pad + headH + 6.0f * S + k.capH + pad);
        float gx = snap((float)last.money.rx0 * msx), gyy = snap((float)last.money.ry0 * msy);
        if (gx + gw > f.screenW - 4.0f) gx = snap(f.screenW - 4.0f - gw); if (gx < 4.0f) gx = 4.0f;
        if (gyy + gh > f.screenH - 4.0f) gyy = snap(f.screenH - 4.0f - gh); if (gyy < 4.0f) gyy = 4.0f;
        g_frameJoin = menu_place(f, __LINE__, gx, gyy, gw, gh, &gw);   // glued to its neighbour (theme kit) : one panel ; stacked -> its width
        draw_themed_box(dev, f.skin, gx, gyy, gw, gh, C.muBox, A, S);
        const float Lg = gx + pad, Rg = gx + gw - pad, hyg = snap(gyy + pad + headH * 0.5f), vy = snap(gyy + pad + headH + 6.0f * S + k.capH * 0.5f);
        rrect(dev, Lg, snap(gyy + pad + headH), Rg - Lg, 1.0f, 0.0f, with_alpha(0x30C8D6EAu, A), with_alpha(0x30C8D6EAu, A));
        { const float gz = snap(k.capH * 1.1f); gil_coin(dev, snap(Rg - gz * 0.5f), hyg, gz, A); }   // the gil icon
        fo->begin(dev);
        fo->draw_lc(dev, Lg, hyg, gl, tz * 0.95f, with_alpha(0xFFE8D9B0u, A), sh, k.ow);
        fo->draw_lc(dev, snap(Rg - fo->measure(g, tz)), vy, g, tz, with_alpha(0xFFF2F5FAu, A), sh, k.ow);
    }
    // ======== the slot's item list (inventor), beside the grid ========
    float dt2 = dt;
    const bool lon = E0.open && E0.lopen;
    if (block_ease(g_elB, lon, now, dt2) && last.lvis > 0) {
        // AS TALL AS THE GRID'S BOX (the user, 2026-11-18) : the client's page of rows spread over that height
        const float rowH = snap((bh - 2.0f * pad) / (float)last.lvis), iz = snap(rowH - 4.0f * S);
        const float wName = fo->measure(ITEM_NAME_REF, tz);
        const int nRows = last.ln - last.ltop < last.lvis ? last.ln - last.ltop : last.lvis;
        const float lw = keep_width(g_elB, snap(pad + 8.0f * S + iz + 8.0f * S + wName + 16.0f * S + fo->measure("99", tz) + 8.0f * S + pad));
        const float lh = bh;
        float lx = snap(bx + bw + gap), ly = by;
        if (lx + lw > f.screenW - 4.0f) lx = snap(bx - gap - lw);
        const int jl = menu_place_at(f, __LINE__, lx, ly, lx, by0, lw, lh);   // glued beside the grid (theme kit)
        const float LA = g_elB.vis * tO;
        if (LA > 0.0f) {
            g_frameJoin = jl;
            draw_themed_box(dev, f.skin, lx, ly, lw, lh, C.muBox, LA, S);
            const float Lx = lx + pad, Wx = lw - 2.0f * pad, ry = snap(ly + pad);
            for (int i = 0; i < nRows; ++i) {
                const float y = snap(ry + (float)i * rowH);
                if (lastS.lrow[i].header) rrect(dev, Lx, snap(y + rowH - 2.0f * S), Wx, 1.0f, 0.0f, with_alpha(0x40FFDA8Au, LA), with_alpha(0x40FFDA8Au, LA));
                else if (((i + last.ltop) & 1) == 0) rrect(dev, Lx, y, Wx, rowH, 3.0f * S, with_alpha(0x18FFFFFFu, LA), with_alpha(0x10FFFFFFu, LA));
            }
            const int cr = last.lcursor >= last.ltop && last.lcursor < last.ltop + nRows ? last.lcursor - last.ltop : -1;
            if (cr >= 0) {
                const float targetY = ry + (float)cr * rowH;
                if (g_elB.curY < 0.0f) g_elB.curY = targetY;
                g_elB.curY += (targetY - g_elB.curY) * (1.0f - expf(-dt * 30.0f));
                if (fabsf(targetY - g_elB.curY) < 0.3f) g_elB.curY = targetY;
                const float y = snap(g_elB.curY), r = rowH * 0.5f;
                rrect_clip_begin(dev, Lx, y, Wx, rowH, r);
                party_selframe(dev, Lx, y, Wx, rowH, f.t, LA, 0);
                rrect_clip_end(dev);
                rrect_stroke(dev, Lx, y, Wx, rowH, r, with_alpha(0xC8FFDA8Au, LA), 1.0f);
                if (last.lfocused) {
                    const float hand = snap(k.capH * 1.25f), cy = g_elB.curY + rowH * 0.5f;
                    const float hx = lx - hand * 0.35f + party_cursor_bob(f.t, hand);
                    if (g_handTex) party_cursor(dev, g_handTex, hx, cy, hand, 0);
                }
            } else g_elB.curY = -1.0f;
            if (last.ln > last.lvis) {
                const float rx = lx + lw - k.bi - (pad - k.bi) * 0.5f - 1.0f * S, th = (float)last.lvis * rowH;
                const float th2 = th * (float)last.lvis / (float)last.ln, ty2 = ry + th * (float)last.ltop / (float)last.ln;
                rrect(dev, rx, ry, 2.0f * S, th, 1.0f * S, with_alpha(0x30FFFFFFu, LA), with_alpha(0x30FFFFFFu, LA));
                rrect(dev, rx, snap(ty2), 2.0f * S, snap(th2 < 6.0f * S ? 6.0f * S : th2), 1.0f * S, with_alpha(0xC0FFDA8Au, LA), with_alpha(0xC0FFDA8Au, LA));
            }
            for (int i = 0; i < nRows; ++i) if (!lastS.lrow[i].header && lastS.lrow[i].id) {
                const u32 tex = list_icon(f, 3, lastS.lrow[i].id);
                const float a = LA * (lastS.lrow[i].col == 0x80404040u ? 0.5f : 1.0f);
                if (tex) { dTexQuadState(dev, tex); tquad(dev, snap(Lx + 8.0f * S), snap(ry + (float)i * rowH + 2.0f * S), iz, iz, 0.0f, 1.0f, 0.0f, 1.0f, with_alpha(0xFFFFFFFFu, a), with_alpha(0xFFFFFFFFu, a)); dSetTex(dev, 0, 0); cs(dev); }
            }
            {   // the mouse : the client's hand follows, a click validates (equips, or takes off the one worn)
                int mrow = -1;
                if (E0.lopen && E0.lfocused && ms && ms->focused && ms->x >= Lx && ms->x < Lx + Wx && ms->y >= ry && ms->y < ry + (float)nRows * rowH)
                    mrow = last.ltop + (int)((ms->y - ry) / rowH);
                if (E0.lopen) {
                    g_hit[8][0] = lx / f.screenW; g_hit[8][1] = ly / f.screenH; g_hit[8][2] = lw / f.screenW; g_hit[8][3] = lh / f.screenH;
                    if (mrow >= 0 && mrow + 1 != g_elB.hoverRow && mrow != E0.lcursor) native_textlist_hover("inventor", mrow);
                    g_elB.hoverRow = mrow + 1;
                    if (mrow >= 0 && ms->clicked) native_textlist_pick("inventor", mrow);
                }
            }
            fo->begin(dev);
            for (int i = 0; i < nRows; ++i) {
                const NativeEqRow& r = lastS.lrow[i];
                const float cy = snap(ry + (float)i * rowH + rowH * 0.5f);
                if (r.header) {                                        // the client's "Storage Options" : it opens the bag chooser
                    const char* t = tr("Storage Options", "Options de stockage");
                    fo->draw_lc(dev, snap(Lx + 4.0f * S), cy, t, tz * 0.9f, with_alpha(0xFFE8D9B0u, LA), sh, k.ow);
                    const float ax = snap(Lx + 4.0f * S + fo->measure(t, tz * 0.9f) + 8.0f * S), ar = 3.2f * S;
                    tri(dev, ax + ar, cy, ax - ar * 0.6f, cy - ar, ax - ar * 0.6f, cy + ar, with_alpha(0xFFE8D9B0u, LA));
                    fo->begin(dev);
                    continue;
                }
                const char* nm = r.id ? item_display_name(r.id) : (r.text[0] ? r.text : 0);
                const u32 c = r.col == 0x80509050u ? 0xFF7CE07Cu : r.col == 0x80404040u ? 0xFF6E7C8Eu : r.col == 0x80804020u ? 0xFFFFB45Au : (i == cr ? 0xFFFFFFFFu : 0xFFE4EAF2u);
                { char cut[64]; fo->draw_lc(dev, snap(Lx + 8.0f * S + iz + 8.0f * S), cy, fit_text(fo, nm ? nm : "?", tz, wName, cut, sizeof(cut)), tz, with_alpha(c, LA), sh, k.ow); }
                if (r.count > 1) { char n[8]; sprintf(n, "%d", r.count); fo->draw_lc(dev, snap(Lx + Wx - 8.0f * S - fo->measure(n, tz)), cy, n, tz, with_alpha(0xFF93A6BEu, LA), sh, k.ow); }
            }
            // the item card : what has the hand -- the list's row, else the grid's cell (the list is up all along)
            // the compare card (itemxinf) TAKES the focus when it opens (log 2026-11-18) : the list keeps the hand then
            const bool listHand = last.lfocused || last.compare;
            const unsigned rowId = listHand && cr >= 0 && !lastS.lrow[cr].header ? lastS.lrow[cr].id : 0;
            const unsigned cellId = curCell >= 0 ? lastS.id[SLOT_AT[curCell]] : 0;
            const unsigned iid = listHand ? rowId : cellId;
            if (iid) {
                // UNDER the grid and the list, as the client (the user, 2026-11-18) : from the grid's left to the list's right
                const float cx = bx, cw = snap((lx > bx ? lx + lw : bx + bw) - bx);
                const float ctop = snap((by + bh > ly + lh ? by + bh : ly + lh) + gap);
                card_use(0); card_load(dev, iid);
                if (card_ready(iid)) {
                    card_draw(f, k, cx, cw, ctop, ctop, tO, LA, false);
                    // the compare view (LB / Ctrl) : the item worn in that slot, under it, a gold arrow between
                    if (last.compare && listHand && cellId && cellId != iid) {
                        const float ay = snap(g_cardBottom + 7.0f * S), ar = 5.0f * S, acx = snap(cx + cw * 0.5f);
                        tri(dev, acx - ar * 1.6f, ay - ar * 0.5f, acx + ar * 1.6f, ay - ar * 0.5f, acx, ay + ar * 0.7f, with_alpha(0xFFFFD23Cu, LA));
                        card_use(1); card_load(dev, cellId);
                        if (card_ready(cellId)) card_draw(f, k, cx, cw, snap(ay + ar + 4.0f * S), snap(ay + ar + 4.0f * S), tO, LA, false);
                        card_use(0);
                    }
                }
            }
        }
    } else {
        g_elB.hoverRow = 0;
        // no list : the card of the grid's cell under the hand
        if (curCell >= 0 && lastS.id[SLOT_AT[curCell]]) {
            const unsigned iid = lastS.id[SLOT_AT[curCell]];
            card_load(dev, iid);
            if (card_ready(iid)) {
                const float cw = bw > snap(300.0f * S) ? bw : snap(300.0f * S), cx = bx;   // under the grid, as the client
                card_draw(f, k, cx, cw, snap(by + bh + gap), snap(by + bh + gap), tO, A, false);
            }
        }
    }
}

// The main menu's ITEMS : the inventory list where the client opens it (top left), in its colours (the client's verdict :
// blue enchanted and ready, green worn, gold usable, grey refused, orange held in Manual Sort ; the Temporary Items first,
// a hairline under them), "used / size" in its header ; the item card under it. The sort options (itmsort2), Use / Dispose
// (iuse) and the Yes / No (sortyn) are the main menu's blocks (mainmenu_draw). The client does the rest (0x037 / 0x028 /
// 0x03A / 0x029).
namespace { Block g_itB, g_binB, g_qtyB; unsigned g_itMs = 0; }
u32 item_row_rgb(unsigned col, bool cur) {
    switch (col) {
    case 0x80407090u: return 0xFF86B8FFu;     // enchanted, worn and ready
    case 0x80509050u: return 0xFF7CE07Cu;     // worn (or the linkshell worn)
    case 0x80909040u: return 0xFFF0D878u;     // usable
    case 0x80404040u: return 0xFF6E7C8Eu;     // refused (bazaar, the whole list while the player cannot)
    case 0x80804020u: return 0xFFFFB45Au;     // held (Manual Sort)
    case 0x80806040u: return 0xFFE6C89Au;     // a temporary item
    default:          return cur ? 0xFFFFFFFFu : 0xFFE4EAF2u;
    }
}
// one Items list box : rows (the client's cells), its title and counter, the cursor, the mouse (hover / click through
// the client's text list `win`), a hit rect. Returns the cursor row's item (0 none).
struct ItemsBox { const NativeItemRow* row; int n, top, vis, cursor, tempRows; bool focused, live; const char* title; const char* counter; bool full;
                  bool prices;            // a shop's price column (the rows' hasPrice ; price -2 : none on that row)
                  const int* tax;         // a bazaar's tax per row (-1 none), drawn in gold before the price
                  const char* noItem; };  // the label of a row with no item (a guild's "Select from your inventory.")
// a price, grouped by thousands ("12,345")
void price_text(char* out, int v) {
    char t[16]; sprintf(t, "%d", v < 0 ? 0 : v);
    const int n = (int)strlen(t); int o = 0;
    for (int i = 0; i < n; ++i) { out[o++] = t[i]; if ((n - 1 - i) % 3 == 0 && i < n - 1) out[o++] = ','; }
    out[o] = 0;
}
float items_box_width(const Frame& f, const Metrics& k, const ItemsBox& L) {   // one width whatever the rows : the widest name's
    Font* fo = f.font; const float S = k.S, tz = k.tz, pad = snap(8.0f * S) + k.bi, iz = snap(snap(k.capH * 1.25f) - 4.0f * S);
    const float wName = fo->measure(ITEM_NAME_REF, tz);
    float wRight = L.prices ? fo->measure("99,999,999", tz) + snap(k.capH) + 4.0f * S : fo->measure("999", tz);
    if (L.prices) for (int i = 0; i < L.vis && L.top + i < L.n; ++i) {   // a count beside the price, a tax before it
        if (L.row[i].itemCount > 1) { wRight += fo->measure("x999", tz * 0.85f) + 10.0f * S; break; } }
    if (L.prices && L.tax) for (int i = 0; i < L.vis && L.top + i < L.n; ++i) if (L.tax[i] >= 0) { wRight += fo->measure("+99,999", tz * 0.85f) + 6.0f * S; break; }
    return snap(pad + 8.0f * S + iz + 8.0f * S + wName + 16.0f * S + wRight + 8.0f * S + pad);
}
unsigned items_box_draw(const Frame& f, const Metrics& k, Block& B, const ItemsBox& L, const char* win, float lx, float ly, float lw, float lh, float A, float dt, float* hit) {
    const UiConfig& C = ui_config(); Font* fo = f.font; const u32 dev = f.dev;
    const float S = k.S, pad = snap(8.0f * S) + k.bi, tz = k.tz, headH = snap(18.0f * S);
    const u32 sh = with_alpha(0xC8000000u, A);
    const int nRows = L.n - L.top < L.vis ? L.n - L.top : L.vis;
    const float rowH = snap(k.capH * 1.25f), iz = snap(rowH - 4.0f * S);
    draw_themed_box(dev, f.skin, lx, ly, lw, lh, C.muBox, A, S);
    const float Lx = lx + pad, Wx = lw - 2.0f * pad, hy = snap(ly + pad + headH * 0.5f), ry = snap(ly + pad + headH + 6.0f * S);
    rrect(dev, Lx, snap(ly + pad + headH), Wx, 1.0f, 0.0f, with_alpha(0x30C8D6EAu, A), with_alpha(0x30C8D6EAu, A));
    for (int i = 0; i < nRows; ++i) {
        const float y = snap(ry + (float)i * rowH);
        if (((i + L.top) & 1) == 0) rrect(dev, Lx, y, Wx, rowH, 3.0f * S, with_alpha(0x18FFFFFFu, A), with_alpha(0x10FFFFFFu, A));
        // the Temporary Items end here : a gold hairline
        if (L.row[i].tempRow && L.top + i == L.tempRows - 1) rrect(dev, Lx, snap(y + rowH - 1.0f), Wx, 1.0f, 0.0f, with_alpha(0x60FFDA8Au, A), with_alpha(0x60FFDA8Au, A));
    }
    const int cr = L.cursor >= L.top && L.cursor < L.top + nRows ? L.cursor - L.top : -1;
    if (cr >= 0 && L.focused) {                                   // the cursor : only on the list that has the hand
        const float targetY = ry + (float)cr * rowH;
        if (B.curY < 0.0f) B.curY = targetY;
        B.curY += (targetY - B.curY) * (1.0f - expf(-dt * 30.0f));
        if (fabsf(targetY - B.curY) < 0.3f) B.curY = targetY;
        const float y = snap(B.curY), r = rowH * 0.5f;
        rrect_clip_begin(dev, Lx, y, Wx, rowH, r);
        party_selframe(dev, Lx, y, Wx, rowH, f.t, A, 0);
        rrect_clip_end(dev);
        rrect_stroke(dev, Lx, y, Wx, rowH, r, with_alpha(0xC8FFDA8Au, A), 1.0f);
    } else B.curY = -1.0f;
    for (int i = 0; i < nRows; ++i) if (L.row[i].itemCol == 0x80804020u) {   // held (Manual Sort, a bin retrieve) : a gold rim breathing
        const float p = 0.55f + 0.45f * sinf(f.t * 6.0f), y = snap(ry + (float)i * rowH);
        rrect_stroke(dev, Lx, y, Wx, rowH, rowH * 0.5f, with_alpha(0xFFFFB45Au, A * p), 1.5f * S);
    }
    if (L.n > L.vis) {
        const float sxr = lx + lw - k.bi - (pad - k.bi) * 0.5f - 1.0f * S, th = (float)L.vis * rowH;
        const float th2 = th * (float)L.vis / (float)L.n, ty2 = ry + th * (float)L.top / (float)L.n;
        rrect(dev, sxr, ry, 2.0f * S, th, 1.0f * S, with_alpha(0x30FFFFFFu, A), with_alpha(0x30FFFFFFu, A));
        rrect(dev, sxr, snap(ty2), 2.0f * S, snap(th2 < 6.0f * S ? 6.0f * S : th2), 1.0f * S, with_alpha(0xC0FFDA8Au, A), with_alpha(0xC0FFDA8Au, A));
    }
    for (int i = 0; i < nRows; ++i) if (L.row[i].itemId) {
        const u32 tex = list_icon(f, 3, L.row[i].itemId);
        const float a = A * (L.row[i].itemCol == 0x80404040u ? 0.5f : 1.0f);
        if (tex) { dTexQuadState(dev, tex); tquad(dev, snap(Lx + 8.0f * S), snap(ry + (float)i * rowH + 2.0f * S), iz, iz, 0.0f, 1.0f, 0.0f, 1.0f, with_alpha(0xFFFFFFFFu, a), with_alpha(0xFFFFFFFFu, a)); dSetTex(dev, 0, 0); cs(dev); }
    }
    if (cr >= 0 && L.focused) {
        const float hand = snap(k.capH * 1.25f), cy = B.curY + rowH * 0.5f;
        const float hx = lx - hand * 0.35f + party_cursor_bob(f.t, hand);
        if (g_handTex) party_cursor(dev, g_handTex, hx, cy, hand, 0);
    }
    {   // the mouse : the client's hand follows, a click validates
        const MouseState* ms = f.mouse; int mrow = -1;
        if (L.live && L.focused && ms && ms->focused && ms->x >= Lx && ms->x < Lx + Wx && ms->y >= ry && ms->y < ry + (float)nRows * rowH)
            mrow = L.top + (int)((ms->y - ry) / rowH);
        if (L.live) {
            hit[0] = lx / f.screenW; hit[1] = ly / f.screenH; hit[2] = lw / f.screenW; hit[3] = lh / f.screenH;
            if (mrow >= 0 && mrow + 1 != B.hoverRow && mrow != L.cursor) native_textlist_hover(win, mrow);
            B.hoverRow = mrow + 1;
            if (mrow >= 0 && ms->clicked) native_textlist_pick(win, mrow);
        } else B.hoverRow = 0;
    }
    fo->begin(dev);
    fo->draw_lc(dev, Lx, hy, L.title, tz * 0.95f, with_alpha(0xFFE8D9B0u, A), sh, k.ow);
    if (L.counter && L.counter[0]) fo->draw_lc(dev, snap(Lx + Wx - fo->measure(L.counter, tz * 0.85f)), hy, L.counter, tz * 0.85f, with_alpha(L.full ? 0xFFFF8A6Au : 0xFF93A6BEu, A), sh, k.ow);
    if (!L.n) fo->draw_lc(dev, snap(Lx + 8.0f * S), snap(ry + rowH * 0.5f), tr("No items.", "Aucun objet."), tz, with_alpha(0xFF6E7C8Eu, A), sh, k.ow);
    for (int i = 0; i < nRows; ++i) {
        const NativeItemRow& r = L.row[i];
        const float cy = snap(ry + (float)i * rowH + rowH * 0.5f);
        const char* nm = r.itemId ? item_display_name(r.itemId) : (L.noItem ? L.noItem : 0);
        { char cut[64]; fo->draw_lc(dev, snap(Lx + 8.0f * S + (r.itemId ? iz + 8.0f * S : 0.0f)), cy, fit_text(fo, nm ? nm : "?", tz, fo->measure(ITEM_NAME_REF, tz), cut, sizeof(cut)), tz, with_alpha(item_row_rgb(r.itemCol, i == cr && L.focused), A), sh, k.ow); }
        if (!L.prices && !r.tempRow && r.itemCount > 1) { char n[8]; sprintf(n, "%d", r.itemCount); fo->draw_lc(dev, snap(Lx + Wx - 8.0f * S - fo->measure(n, tz)), cy, n, tz, with_alpha(0xFF93A6BEu, A), sh, k.ow); }
        if (L.prices) {                                               // the price, the gil coin after it ; "?" not asked yet (Sell)
            const float cz = snap(k.capH * 0.95f), colR = snap(Lx + Wx - 8.0f * S - cz - 4.0f * S);
            float x = colR - fo->measure("99,999,999", tz);            // left of the price column : the tax, then the count
            if (r.hasPrice && r.price != -2) {
                char p[24]; if (r.price >= 0) price_text(p, r.price); else strcpy(p, "?");
                const bool off = r.itemCol == 0x80404040u;           // refused (too dear, a requirement, cannot be sold)
                fo->draw_lc(dev, snap(colR - fo->measure(p, tz)), cy, p, tz, with_alpha(r.price < 0 ? 0xFF6E7C8Eu : off ? 0xFF8A8F99u : 0xFFFFDA8Au, A), sh, k.ow);
                g_coinQ[g_coinN < 16 ? g_coinN++ : 15] = { snap(Lx + Wx - 8.0f * S - cz * 0.5f), cy, cz, A * (off || r.price < 0 ? 0.5f : 1.0f) };
                if (L.tax && L.tax[i] >= 0) {                         // the bazaar's tax, as the client : gold, before the price
                    char t[24], q[20]; price_text(q, L.tax[i]); sprintf(t, "+%s", q);
                    x = snap(colR - fo->measure(p, tz) - 6.0f * S - fo->measure(t, tz * 0.85f));
                    fo->draw_lc(dev, x, cy, t, tz * 0.85f, with_alpha(0xFFC8B060u, A), sh, k.ow);
                    x -= 4.0f * S;
                }
            }
            if (r.itemCount > 1) { char n[8]; sprintf(n, "x%d", r.itemCount); fo->draw_lc(dev, snap(x - 10.0f * S - fo->measure(n, tz * 0.85f)), cy, n, tz * 0.85f, with_alpha(0xFF93A6BEu, A), sh, k.ow); }
        }
    }
    if (g_coinN) { for (int i = 0; i < g_coinN; ++i) gil_coin(dev, g_coinQ[i].x, g_coinQ[i].y, g_coinQ[i].z, g_coinQ[i].a); g_coinN = 0; fo->begin(dev); }
    return cr >= 0 ? L.row[cr].itemId : 0;
}
// The main menu's ITEMS : the inventory list TOP LEFT (the client's place), in its colours (blue enchanted and ready, green
// worn, gold usable, grey refused, orange held in Manual Sort ; the Temporary Items first, a hairline under them),
// "used / size" in its header ; the item card ABOVE THE CHAT (card_on_chat). The RECYCLING BIN : the bin top left, the
// inventory (all grey : nothing goes in by hand) beside it. The sort options (itmsort2), Use / Dispose (iuse) and the
// Yes / No (sortyn) are the main menu's blocks (mainmenu_draw). The client does the rest (0x037 / 0x028 / 0x03A / 0x029).
// the quantity box (itemctrl : a stack retrieved from the bin, a synthesis material) : the item, value / max ; the client's
// keys change it. Returns its height.
float qty_box(const Frame& f, const Metrics& k, unsigned qid, int val, int mx, float x, float y, float w, float QA) {
    const UiConfig& C = ui_config(); const float S = k.S, pad = snap(8.0f * S) + k.bi, headH = snap(18.0f * S);
    const float qh = snap(pad + headH + 6.0f * S + k.capH * 1.6f + pad);
    draw_themed_box(f.dev, f.skin, x, y, w, qh, C.muBox, QA, S);
    const float Lq = x + pad, Rq = x + w - pad, hq = snap(y + pad + headH * 0.5f), vq = snap(y + pad + headH + 6.0f * S + k.capH * 0.8f);
    rrect(f.dev, Lq, snap(y + pad + headH), Rq - Lq, 1.0f, 0.0f, with_alpha(0x30C8D6EAu, QA), with_alpha(0x30C8D6EAu, QA));
    const float iz = snap(k.capH * 1.3f);
    if (qid) { const u32 tex = list_icon(f, 3, qid); if (tex) { dTexQuadState(f.dev, tex); tquad(f.dev, Lq, snap(vq - iz * 0.5f), iz, iz, 0.0f, 1.0f, 0.0f, 1.0f, with_alpha(0xFFFFFFFFu, QA), with_alpha(0xFFFFFFFFu, QA)); dSetTex(f.dev, 0, 0); cs(f.dev); } }
    char q[24]; sprintf(q, "%d / %d", val, mx);
    Font* fo = f.font; const float qz = k.tz * 1.25f, qw = fo->measure(q, qz), ar = 3.5f * S;
    const float ax = snap(Rq - qw - 14.0f * S);
    const u32 ac = with_alpha(0xC0FFDA8Au, QA);
    tri(f.dev, ax, vq - ar * 2.2f, ax - ar, vq - ar * 0.6f, ax + ar, vq - ar * 0.6f, ac);
    tri(f.dev, ax, vq + ar * 2.2f, ax - ar, vq + ar * 0.6f, ax + ar, vq + ar * 0.6f, ac);
    fo->begin(f.dev);
    fo->draw_lc(f.dev, Lq, hq, tr("Quantity", "Quantit\xC3\xA9"), k.tz * 0.95f, with_alpha(0xFFE8D9B0u, QA), with_alpha(0xC8000000u, QA), k.ow);
    const char* nm = qid ? item_display_name(qid) : 0;
    if (nm) fo->draw_lc(f.dev, snap(Lq + iz + 8.0f * S), vq, nm, k.tz, with_alpha(0xFFE4EAF2u, QA), with_alpha(0xC8000000u, QA), k.ow);
    fo->draw_lc(f.dev, snap(Rq - qw), vq, q, qz, with_alpha(0xFFFFFFFFu, QA), with_alpha(0xC8000000u, QA), k.ow);
    return qh;
}


// ---- a FORM (Search -> Name, Level, Master Level) : the field(s) on one line, OK and Cancel side by side below, as the
// client lays them out (the user, 2026-11-19 : "dans le jeu c'est OK et Cancel et elles sont horizontales"). Fixed size.
bool is_form(const char* n) { return !strcmp(n, "scname") || !strcmp(n, "scchar") || !strcmp(n, "scmlev"); }
void form_size(const Frame& f, const Metrics& k, const NativeEntryMenu& L, float& w, float& h) {
    const float S = k.S, pad = snap(8.0f * S) + k.bi, headH = snap(18.0f * S), fieldH = snap(k.capH + 6.0f * S), btnH = snap(k.capH + 2.0f * S);
    const bool name = !strcmp(L.name, "scname");
    float fw = name ? f.font->measure("Mmmmmmmmmmmmmmm_", k.tz) + 20.0f * S : 2.0f * (f.font->measure("Maximum", k.tz * 0.85f) + f.font->measure("99", k.tz * 1.1f) + 30.0f * S) + 8.0f * S;
    const float hint = f.font->measure(tr("(press Enter to type)", "(Entr\xC3\xA9""e pour taper)"), k.tz * 0.9f) + 20.0f * S;
    if (name && hint > fw) fw = hint;
    const float bwMin = 2.0f * (f.font->measure(tr("Cancel", "Annuler"), k.tz) + 32.0f * S) + 8.0f * S;
    w = snap(pad + (fw > bwMin ? fw : bwMin) + pad);
    h = snap(pad + headH + 6.0f * S + fieldH + 6.0f * S + btnH + pad);
}
// returns the ROW (1-based index into L.entry) under the mouse, 0 none
int form_draw(const Frame& f, const Metrics& k, Block& B, const NativeEntryMenu& L, float x, float y, float w, float h, float A) {
    const u32 dev = f.dev; const float S = k.S, pad = snap(8.0f * S) + k.bi, headH = snap(18.0f * S), fieldH = snap(k.capH + 6.0f * S), btnH = snap(k.capH + 2.0f * S);
    Font* fo = f.font; const u32 sh = with_alpha(0xC8000000u, A);
    draw_themed_box(dev, f.skin, x, y, w, h, ui_config().muBox, A, S);
    const bool name = !strcmp(L.name, "scname");
    const float Lx = x + pad, W = w - 2.0f * pad, hy = snap(y + pad + headH * 0.5f);
    rrect(dev, Lx, snap(y + pad + headH), W, 1.0f, 0.0f, with_alpha(0x30C8D6EAu, A), with_alpha(0x30C8D6EAu, A));
    const float fy = snap(y + pad + headH + 6.0f * S), by = snap(fy + fieldH + 6.0f * S), bw = snap((W - 8.0f * S) * 0.5f);
    // the elements by ENTRY : name 1 field, 2 OK, 3 Cancel ; level 1 min, 2 max, 3 OK, 4 Cancel
    struct El { int entry; float x, y, w, h; bool field; };
    El el[4]; int ne = 0;
    if (name) el[ne++] = { 1, Lx, fy, W, fieldH, true };
    else { const float hw = snap((W - 8.0f * S) * 0.5f); el[ne++] = { 1, Lx, fy, hw, fieldH, true }; el[ne++] = { 2, snap(Lx + hw + 8.0f * S), fy, hw, fieldH, true }; }
    el[ne++] = { name ? 2 : 3, Lx, by, bw, btnH, false };
    el[ne++] = { name ? 3 : 4, snap(Lx + bw + 8.0f * S), by, bw, btnH, false };
    const MouseState* ms = f.mouse; int hover = 0;
    for (int i = 0; i < ne; ++i) {
        const El& e = el[i]; const float r = e.field ? 5.0f * S : e.h * 0.5f;
        const bool cur = L.cursor == e.entry, editing = L.entMarkKind == 3 && ((L.entMarks >> e.entry) & 1u);
        rrect(dev, e.x, e.y, e.w, e.h, r, with_alpha(e.field ? 0x70101826u : 0x70283040u, A), with_alpha(0x70080C14u, A));
        if (cur) { rrect_clip_begin(dev, e.x, e.y, e.w, e.h, r); party_selframe(dev, e.x, e.y, e.w, e.h, f.t, A, 0); rrect_clip_end(dev); }
        rrect_stroke(dev, e.x, e.y, e.w, e.h, r, with_alpha(editing ? 0xE0FFB45Au : cur ? 0xD8FFDA8Au : 0x40C8D6EAu, A), editing || cur ? 1.5f * S : 1.0f);
        if (ms && ms->focused && ms->x >= e.x && ms->x < e.x + e.w && ms->y >= e.y && ms->y < e.y + e.h)
            for (int r2 = 0; r2 < L.n; ++r2) if (L.entry[r2] == e.entry) hover = r2 + 1;
        if (cur && L.focused) { const float hand = snap(k.capH * 1.25f); if (g_handTex) party_cursor(dev, g_handTex, e.x - hand * 0.35f + party_cursor_bob(f.t, hand), e.y + e.h * 0.5f, hand, 0); }
    }
    fo->begin(dev);
    const char* title = name ? tr("Name", "Nom") : !strcmp(L.name, "scmlev") ? "Master Level" : tr("Level", "Niveau");
    fo->draw_lc(dev, Lx, hy, title, k.tz * 0.95f, with_alpha(0xFFE8D9B0u, A), sh, k.ow);
    for (int i = 0; i < ne; ++i) {
        const El& e = el[i]; const float cy = snap(e.y + e.h * 0.5f);
        if (e.field) {
            if (name) {
                const bool typing = L.entMarkKind == 3;
                char t[40]; _snprintf(t, sizeof(t), typing ? "%s_" : "%s", L.entName); t[sizeof(t) - 1] = 0;
                if (!t[0]) fo->draw_lc(dev, snap(e.x + 10.0f * S), cy, tr("(press Enter to type)", "(Entr\xC3\xA9""e pour taper)"), k.tz * 0.9f, with_alpha(0xFF6E7C8Eu, A), sh, k.ow);
                else fo->draw_lc(dev, snap(e.x + 10.0f * S), cy, t, k.tz, with_alpha(typing ? 0xFFFFFFFFu : 0xFFFFDA8Au, A), sh, k.ow);
            } else {
                const char* lb = e.entry == 1 ? tr("Minimum", "Minimum") : tr("Maximum", "Maximum");
                fo->draw_lc(dev, snap(e.x + 10.0f * S), cy, lb, k.tz * 0.85f, with_alpha(0xFF93A6BEu, A), sh, k.ow);
                char v[8]; if (L.entVal[e.entry] >= 0) sprintf(v, "%d", L.entVal[e.entry]); else strcpy(v, "-");
                fo->draw_lc(dev, snap(e.x + e.w - 10.0f * S - fo->measure(v, k.tz * 1.1f)), cy, v, k.tz * 1.1f, with_alpha(0xFFFFFFFFu, A), sh, k.ow);
            }
        } else {
            const bool ok = (name && e.entry == 2) || (!name && e.entry == 3);
            const char* lb = ok ? "OK" : tr("Cancel", "Annuler");
            fo->draw_lc(dev, snap(e.x + (e.w - fo->measure(lb, k.tz)) * 0.5f), cy, lb, k.tz, with_alpha(0xFFF2F5FAu, A), sh, k.ow);
        }
    }
    (void)B;
    return hover;
}

// ======== Search -> Edit your comment (`comment`) : 3 lines of 40, the genre, above the chat (the client's place) ========
// The client's editor drives everything (Enter / Down next line, Up previous, Escape SAVES and closes -- no discard) ;
// AioHUD shows the three lines, the one being typed with its caret, and anchors the auto-translate picker on it.
namespace { Block g_cmB; unsigned g_cmMs = 0; }
void comment_draw(const Frame& f) {
    const UiConfig& C = ui_config();
    if (!(C.nuHide & NU_MAIN) || !f.font || f.screenH <= 0.0f || f.screenW <= 0.0f) { g_cmB.vis = 0.0f; g_cmB.wasOpen = false; return; }
    const unsigned now = GetTickCount();
    float dt = g_cmMs ? (float)(now - g_cmMs) / 1000.0f : 0.0f; g_cmMs = now; if (dt > 0.1f) dt = 0.1f;
    const NativeComment& K0 = native_comment();
    static NativeComment last;
    if (K0.open) last = K0;
    if (!block_ease(g_cmB, K0.open, now, dt) || last.uiW <= 0) return;
    const Metrics k = no_icons(metrics(f));
    const float S = k.S, pad = snap(8.0f * S) + k.bi, tz = k.tz, headH = snap(18.0f * S), rowH = snap(k.capH * 1.2f);
    Font* fo = f.font; const u32 dev = f.dev;
    const float tO = ease_out(((float)(now - g_cmB.openMs) / 1000.0f) / 0.08f);
    const float A = g_cmB.vis * tO; if (A <= 0.0f) return;
    const u32 sh = with_alpha(0xC8000000u, A);
    // FIXED : 40 characters wide, 3 lines + the hint
    const float lineW = fo->measure("Wwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwww", tz) * 0.78f;
    const float bw = snap(pad + 14.0f * S + lineW + 14.0f * S + pad), bh = snap(pad + headH + 6.0f * S + 3.0f * rowH + 6.0f * S + k.capH * 0.8f + pad);
    float bx = 0.0f, by = 0.0f;
    if (!place_on_chat(bw, bh, snap(4.0f * S), f.screenW, f.screenH, bx, by)) {
        const float sx = f.screenW / (float)last.uiW, sy = f.screenH / (float)last.uiH;
        bx = snap((float)last.rx0 * sx); by = snap((float)last.ry0 * sy);
    }
    if (bx + bw > f.screenW - 4.0f) bx = snap(f.screenW - 4.0f - bw); if (bx < 4.0f) bx = 4.0f;
    if (by + bh > f.screenH - 4.0f) by = snap(f.screenH - 4.0f - bh); if (by < 4.0f) by = 4.0f;
    const int jm = menu_place(f, __LINE__, bx, by, bw, bh);
    by = snap(by + (1.0f - tO) * 4.0f * S);
    g_frameJoin = jm;
    draw_themed_box(dev, f.skin, bx, by, bw, bh, C.muBox, A, S);
    const float L = bx + pad, W = bw - 2.0f * pad, hy = snap(by + pad + headH * 0.5f), ly = snap(by + pad + headH + 6.0f * S);
    rrect(dev, L, snap(by + pad + headH), W, 1.0f, 0.0f, with_alpha(0x30C8D6EAu, A), with_alpha(0x30C8D6EAu, A));
    // the genre : major (comgenre) > sub (sccompar / sccomite / sccomls)
    char genre[96] = "";
    { const int mj = last.cmGenre >> 4, sb = last.cmGenre & 0xF;
      const SubRow* g = sub_row("comgenre", mj); const char* subMenu = mj >= 1 && mj <= 3 ? "sccompar" : mj == 4 ? "sccomite" : mj == 5 ? "sccomls" : 0;
      const SubRow* s2 = subMenu ? sub_row(subMenu, sb) : 0;
      if (g && s2) _snprintf(genre, sizeof(genre), "%s \xE2\x80\xBA %s", tr(g->en, g->fr), tr(s2->en, s2->fr)); else if (g) lstrcpynA(genre, tr(g->en, g->fr), sizeof(genre)); genre[sizeof(genre) - 1] = 0; }
    // the lines' wells ; the one being typed in the party glass
    for (int i = 0; i < 3; ++i) {
        const float y = snap(ly + (float)i * rowH); const bool cur = i == last.cmLine;
        rrect(dev, L, y, W, rowH - 2.0f * S, 4.0f * S, with_alpha(0x60101826u, A), with_alpha(0x60080C14u, A));
        if (cur) { rrect_clip_begin(dev, L, y, W, rowH - 2.0f * S, 4.0f * S); party_selframe(dev, L, y, W, rowH - 2.0f * S, f.t, A, 0); rrect_clip_end(dev); }
        rrect_stroke(dev, L, y, W, rowH - 2.0f * S, 4.0f * S, with_alpha(cur && last.typing ? 0xE0FFB45Au : cur ? 0xC8FFDA8Au : 0x30C8D6EAu, A), cur ? 1.5f * S : 1.0f);
    }
    fo->begin(dev);
    fo->draw_lc(dev, L, hy, tr("Search Comment", "Commentaire de recherche"), tz * 0.95f, with_alpha(0xFFE8D9B0u, A), sh, k.ow);
    if (genre[0]) fo->draw_lc(dev, snap(L + W - fo->measure(genre, tz * 0.85f)), hy, genre, tz * 0.85f, with_alpha(0xFF93A6BEu, A), sh, k.ow);
    for (int i = 0; i < 3; ++i) {
        const float cy = snap(ly + (float)i * rowH + (rowH - 2.0f * S) * 0.5f), tx = snap(L + 10.0f * S);
        if (i == last.cmLine && last.typing) {          // the live line : its colours, its caret ; the picker opens on it
            char t[0x170]; unsigned col[0x170]; int caret = 0;
            const int n = chat_input_decode(last.cmLive, last.cmLiveN, t, col, sizeof(t), &caret);
            float x = tx, caretX = tx;
            for (int a = 0; a < n; ) {
                int e = a + 1; while (e < n && col[e] == col[a]) ++e;
                char seg[0x170]; memcpy(seg, t + a, (size_t)(e - a)); seg[e - a] = 0;
                if (caret >= a && caret < e) { char pre[0x170]; memcpy(pre, t + a, (size_t)(caret - a)); pre[caret - a] = 0; caretX = x + fo->measure(pre, tz); }
                fo->draw_lc(dev, snap(x), cy, seg, tz, with_alpha(col[a] | 0xFF000000u, A), sh, k.ow);
                x += fo->measure(seg, tz); a = e;
            }
            if (caret >= n) caretX = x;
            const float p = 0.5f + 0.5f * sinf(f.t * 7.0f);
            rrect(dev, snap(caretX), snap(cy - k.capH * 0.38f), 1.5f * S, snap(k.capH * 0.76f), 0.0f, with_alpha(0xFFFFDA8Au, A * p), with_alpha(0xFFFFDA8Au, A * p));
            fo->begin(dev);
            chat_input_rect_set(L, snap(ly + (float)i * rowH), W, rowH, caretX);
        } else if (last.cmText[i][0]) {
            char out[1][CHAT_TEXT]; unsigned short cc[1][CHAT_TEXT]; int lens[1] = { 0 };
            if (chat_decode(last.cmText[i], out, cc, lens, 1) >= 1) { out[0][lens[0] < CHAT_TEXT ? lens[0] : CHAT_TEXT - 1] = 0; fo->draw_lc(dev, tx, cy, out[0], tz, with_alpha(0xFFE4EAF2u, A), sh, k.ow); }
        }
    }
    const char* hint = tr("Enter : next line  \xC2\xB7  Up : previous  \xC2\xB7  Esc : save and close", "Entr\xC3\xA9""e : ligne suivante  \xC2\xB7  Haut : pr\xC3\xA9""c\xC3\xA9""dente  \xC2\xB7  \xC3\x89""chap : enregistrer et fermer");
    fo->draw_lc(dev, L, snap(ly + 3.0f * rowH + 6.0f * S + k.capH * 0.4f), hint, tz * 0.8f, with_alpha(0xFF93A6BEu, A), sh, k.ow);
}

// ======== the main menu's LINKSHELL : the linkshell items carried (link5), top left -- the client's icon (LS colour, slot digit), item, LS name ========
namespace { Block g_lsB; unsigned g_lsMs = 0; }
void linkshell_draw(const Frame& f) {
    const UiConfig& C = ui_config();
    if (!(C.nuHide & NU_MAIN) || !f.font || f.screenH <= 0.0f || f.screenW <= 0.0f) { g_lsB.vis = 0.0f; g_lsB.wasOpen = false; return; }
    const unsigned now = GetTickCount();
    float dt = g_lsMs ? (float)(now - g_lsMs) / 1000.0f : 0.0f; g_lsMs = now; if (dt > 0.1f) dt = 0.1f;
    const NativeLinkshell& L0 = native_linkshell();
    static NativeLinkshell last;
    if (L0.open) last = L0;
    if (!block_ease(g_lsB, L0.open, now, dt) || last.uiW <= 0 || last.vis <= 0) return;
    const Metrics k = metrics(f);
    const float S = k.S, pad = snap(8.0f * S) + k.bi, tz = k.tz, headH = snap(22.0f * S), rowH = snap(k.capH * 1.25f), iz = snap(rowH - 4.0f * S);
    Font* fo = f.font; const u32 dev = f.dev;
    const float tO = ease_out(((float)(now - g_lsB.openMs) / 1000.0f) / 0.08f);
    const float A = g_lsB.vis * tO * (last.focused ? 1.0f : 0.8f); if (A <= 0.0f) return;
    const u32 sh = with_alpha(0xC8000000u, A);
    // FIXED, the client's 3 columns : the icon (LS1 / LS2 over its top right), the item (its status colour), the LS name (20 chars)
    const float kindW = fo->measure("Linkpearl", tz);
    const float lw = snap(pad + 4.0f * S + iz + 10.0f * S + kindW + 14.0f * S + fo->measure("Wwwwwwwwwwwwwwwwwwww", tz) * 0.8f + pad);
    const float lh = snap(pad + headH + 6.0f * S + (float)last.vis * rowH + pad);
    const float sx = f.screenW / (float)last.uiW, sy = f.screenH / (float)last.uiH;
    float lx = snap(16.0f * sx), ly = snap(48.0f * sy);
    if (lx + lw > f.screenW - 4.0f) lx = snap(f.screenW - 4.0f - lw);
    const int jm = menu_place(f, __LINE__, lx, ly, lw, lh);
    ly = snap(ly + (1.0f - tO) * 4.0f * S);
    g_frameJoin = jm;
    draw_themed_box(dev, f.skin, lx, ly, lw, lh, C.muBox, A, S);
    const float Lx = lx + pad, Wx = lw - 2.0f * pad, hy = snap(ly + pad + headH * 0.5f), ry = snap(ly + pad + headH + 6.0f * S);
    rrect(dev, Lx, snap(ly + pad + headH), Wx, 1.0f, 0.0f, with_alpha(0x30C8D6EAu, A), with_alpha(0x30C8D6EAu, A));
    const int nRows = last.n - last.top < last.vis ? last.n - last.top : last.vis;
    const int cr = last.cursor >= last.top && last.cursor < last.top + nRows ? last.cursor - last.top : -1;
    for (int i = 0; i < nRows; ++i) { const float y = snap(ry + (float)i * rowH); if (((i + last.top) & 1) == 0) rrect(dev, Lx, y, Wx, rowH, 3.0f * S, with_alpha(0x18FFFFFFu, A), with_alpha(0x10FFFFFFu, A)); }
    if (cr >= 0) {
        const float targetY = ry + (float)cr * rowH;
        if (g_lsB.curY < 0.0f) g_lsB.curY = targetY;
        g_lsB.curY += (targetY - g_lsB.curY) * (1.0f - expf(-dt * 30.0f));
        if (fabsf(targetY - g_lsB.curY) < 0.3f) g_lsB.curY = targetY;
        const float y = snap(g_lsB.curY), r = rowH * 0.5f;
        rrect_clip_begin(dev, Lx, y, Wx, rowH, r); party_selframe(dev, Lx, y, Wx, rowH, f.t, A, 0); rrect_clip_end(dev);
        rrect_stroke(dev, Lx, y, Wx, rowH, r, with_alpha(0xC8FFDA8Au, A), 1.0f);
        if (last.focused) { const float hand = snap(k.capH * 1.25f); if (g_handTex) party_cursor(dev, g_handTex, lx - hand * 0.35f + party_cursor_bob(f.t, hand), g_lsB.curY + rowH * 0.5f, hand, 0); }
    } else g_lsB.curY = -1.0f;
    // each row : the item's icon tinted with its LS colour (as the client does), the slot (1 / 2) over its top right
    for (int i = 0; i < nRows; ++i) {
        const NativeLsRow& r = last.row[i]; const float y = snap(ry + (float)i * rowH);
        const bool off = r.col == 0x80404040u;
        if (r.itemId) { const u32 tex = list_icon(f, 3, r.itemId); const float a = A * (off ? 0.5f : 1.0f);
            const u32 tint = r.rgb ? (r.rgb | 0xFF000000u) : 0xFFFFFFFFu;
            if (tex) { dTexQuadState(dev, tex); tquad(dev, snap(Lx + 4.0f * S), snap(y + 2.0f * S), iz, iz, 0.0f, 1.0f, 0.0f, 1.0f, with_alpha(tint, a), with_alpha(tint, a)); dSetTex(dev, 0, 0); cs(dev); } }
    }
    {   // the mouse
        const MouseState* ms = f.mouse; int mrow = -1;
        if (L0.open && L0.focused && ms && ms->focused && ms->x >= Lx && ms->x < Lx + Wx && ms->y >= ry && ms->y < ry + (float)nRows * rowH) mrow = last.top + (int)((ms->y - ry) / rowH);
        if (L0.open) {
            g_hit[10][0] = lx / f.screenW; g_hit[10][1] = ly / f.screenH; g_hit[10][2] = lw / f.screenW; g_hit[10][3] = lh / f.screenH;
            if (mrow >= 0 && mrow + 1 != g_lsB.hoverRow && mrow != L0.cursor) native_textlist_hover("link5", mrow);
            g_lsB.hoverRow = mrow + 1;
            if (mrow >= 0 && ms->clicked) native_textlist_pick("link5", mrow);
        } else g_lsB.hoverRow = 0;
    }
    fo->begin(dev);
    {   // the header : the linkshell under the hand, in its colour (the client's link6)
        const NativeLsRow* h = cr >= 0 ? &last.row[cr] : 0;
        const char* t = h && h->lsName[0] ? h->lsName : "Linkshell";
        fo->draw_lc(dev, Lx, hy, t, tz, with_alpha(h && h->rgb ? (h->rgb | 0xFF000000u) : 0xFFE8D9B0u, A), sh, k.ow);
        char c[16]; sprintf(c, "%d", last.n); fo->draw_lc(dev, snap(Lx + Wx - fo->measure(c, tz * 0.85f)), hy, c, tz * 0.85f, with_alpha(0xFF93A6BEu, A), sh, k.ow);
    }
    if (!last.n) fo->draw_lc(dev, snap(Lx + 8.0f * S), snap(ry + rowH * 0.5f), tr("No linkshell items.", "Aucun objet de linkshell."), tz, with_alpha(0xFF6E7C8Eu, A), sh, k.ow);
    for (int i = 0; i < nRows; ++i) {
        const NativeLsRow& r = last.row[i]; const float cy = snap(ry + (float)i * rowH + rowH * 0.5f);
        const u32 nc = r.col == 0x80509050u ? 0xFF7CE07Cu : r.col == 0x80404040u ? 0xFF6E7C8Eu : (i == cr ? 0xFFFFFFFFu : 0xFFE4EAF2u);
        static const char* const KIND[6] = { "Linkshell", "Linkshell", "Pearlsack", "Linkpearl", "Broken", "?" };   // the item (0 : an unopened shell)
        const float kx = snap(Lx + 4.0f * S + iz + 10.0f * S);
        fo->draw_lc(dev, kx, cy, KIND[r.kind < 6 ? r.kind : 5], tz, with_alpha(nc, A), sh, k.ow);
        char nm[48]; lstrcpynA(nm, r.lsName[0] ? r.lsName : (r.kind == 0 ? tr("New Linkshell", "Nouvelle linkshell") : "?"), sizeof(nm));
        fo->draw_lc(dev, snap(kx + kindW + 14.0f * S), cy, nm, tz, with_alpha(r.col == 0x80404040u ? 0xFF6E7C8Eu : (i == cr ? 0xFFFFFFFFu : 0xFFE4EAF2u), A), sh, k.ow);
        if (r.slot) {   // LS1 / LS2 : the digit over the icon's top right
            const char* d = r.slot == 1 ? "1" : "2"; const float dz = tz * 0.8f;
            fo->draw_lc(dev, snap(Lx + 4.0f * S + iz - fo->measure(d, dz) + 1.0f * S), snap(ry + (float)i * rowH + 2.0f * S + dz * 0.45f), d, dz, with_alpha(0xFFFFDA8Au, A), with_alpha(0xFF000000u, A), k.ow);
        }
    }
}

// ======== the main menu's REGION INFO : over the client's map (cnqframe), top left -- the mode's overview + its points ========
// From the server's answers (0x05E / 0x071), each value matched against the client's panels (2026-11-19) : Conquest = the
// nations' balance of power (+ the alliance) and the conquest points ; Besieged = the orders, the three strongholds, the
// imperial standing ; Campaign = the controlled areas, the allied notes ; Colonization = the bayld. The client's hover
// panels (a region's detail, a stronghold, an area) are still the client's.
namespace { Block g_rgB; unsigned g_rgMs = 0; }
static void fmt_thousands(char* out, unsigned v) {
    char t[16]; sprintf(t, "%u", v); const int n = (int)strlen(t); int o = 0;
    for (int i = 0; i < n; ++i) { out[o++] = t[i]; if ((n - i - 1) % 3 == 0 && i < n - 1) out[o++] = ','; }
    out[o] = 0;
}
void region_draw(const Frame& f) {
    const UiConfig& C = ui_config();
    if (!(C.nuHide & NU_MAIN) || !f.font || f.screenH <= 0.0f || f.screenW <= 0.0f) { g_rgB.vis = 0.0f; g_rgB.wasOpen = false; return; }
    const unsigned now = GetTickCount();
    float dt = g_rgMs ? (float)(now - g_rgMs) / 1000.0f : 0.0f; g_rgMs = now; if (dt > 0.1f) dt = 0.1f;
    const NativeRegion& R0 = native_region();
    static NativeRegion last;
    const bool up = R0.open && R0.mode >= 0;
    if (up) last = R0;
    if (!block_ease(g_rgB, up, now, dt) || last.uiW <= 0 || last.uiH <= 0 || last.mode < 0) return;
    const Metrics k = metrics(f);
    const float S = k.S, pad = snap(10.0f * S) + k.bi, tz = k.tz, rowH = snap(k.capH * 1.05f), headH = snap(22.0f * S);   // pad : + the frame border
    Font* fo = f.font; const u32 dev = f.dev;
    const float tO = ease_out(((float)(now - g_rgB.openMs) / 1000.0f) / 0.08f);
    const float A = g_rgB.vis * tO; if (A <= 0.0f) return;
    const u32 sh = with_alpha(0xC8000000u, A);
    // the rows : a label (left), a value (right) ; or a gauge (bar >= 0, 0..100) ; an arrow (1 up, 2 down) ; `ally` : joined by the bracket
    struct Row { char l[48]; char v[24]; u32 lc, vc; bool head, ally; int bar, arrow; };
    Row rows[14]; int n = 0;
    auto add = [&](const char* l, const char* v, u32 lc, u32 vc, bool head) -> Row* { if (n >= 14) return 0; Row& r = rows[n++]; lstrcpynA(r.l, l, 48); lstrcpynA(r.v, v ? v : "", 24); r.lc = lc; r.vc = vc; r.head = head; r.ally = false; r.bar = -1; r.arrow = 0; return &r; };
    const u32 TXT = 0xFFE4EAF2u, DIM = 0xFF93A6BEu, GOLD = 0xFFFFDA8Au;
    static const char* const NATION[4] = { "San d'Oria", "Bastok", "Windurst", "Beastmen" };
    static const u32 NCOL[4] = { 0xFFE87878u, 0xFF7FA8F0u, 0xFFE8D060u, 0xFFB894E0u };
    static const char* const RGNAME[19] = { "Ronfaure", "Zulkheim", "Norvallen", "Gustaberg", "Derfland", "Sarutabaruta", "Kolshushu", "Aragoneu",
        "Fauregandi", "Valdeaunia", "Qufim", "Li'Telor", "Kuzotz", "Vollbow", "Elshimo Lowlands", "Elshimo Uplands", "Tu'Lia", "Movapolos", "Tavnazian Archipelago" };
    const char* title = 0; const char* ptsLabel = 0; unsigned pts = 0; bool ptsOk = false; const char* wait = 0;
    char v[24], tl[48];
    const unsigned char* q = last.conq;
    if (last.mode == 0) {
        title = tr("Conquest", "Conqu\xC3\xAAte"); ptsLabel = tr("Conquest Points", "Points de conqu\xC3\xAAte");
        if (!last.conqOk) wait = tr("Waiting for the server...", "En attente du serveur...");
        else if (q[0x98]) wait = tr("The server is calculating the regional influences.", "Le serveur calcule les influences.");
        else if (last.hover >= 5 && last.hover <= 23) {        // a region under the cursor (the client's conquer1)
            const unsigned char* g = q + 0x06 + last.hover * 4;   // b0 ranking, b2 influence, b3 owner
            title = RGNAME[last.hover - 5];
            add(tr("Regional Control", "Contr\xC3\xB4le"), 0, DIM, 0, true);
            const int own = g[3];
            if (own >= 1 && own <= 4) add(NATION[own - 1], "", NCOL[own - 1], 0, false);
            else add(own == 0xFF ? "Jeuno" : tr("None", "Aucun"), "", TXT, 0, false);
            sprintf(tl, tr("Next tally in %u days", "Prochain d\xC3\xA9""compte : %u jours"), q[0x8C]); add(tl, "", TXT, 0, false);
            add(tr("Regional Influence", "Influence"), 0, DIM, 0, true);
            if (g[2] & 0x80) add(tr("No data.", "Aucune donn\xC3\xA9""e."), "", DIM, 0, false);
            else if (last.hover == last.home) {                 // the player's own region : the four gauges (in %)
                const int pct[4] = { (signed char)q[0x86], (signed char)q[0x87], (signed char)q[0x88], (int)q[0x94] };
                for (int nn = 0; nn < 4; ++nn) { if (Row* r = add(NATION[nn], 0, NCOL[nn], 0, false)) r->bar = pct[nn] < 0 ? 0 : pct[nn] > 100 ? 100 : pct[nn]; }
            } else {                                            // elsewhere : each nation's level ; the leader's arrow up
                static const char* const LEVEL[4] = { "Minimal", "Minor", "Major", "Dominant" };
                for (int nn = 0; nn < 3; ++nn) {
                    Row* r = add(NATION[nn], LEVEL[(g[2] >> (2 * nn)) & 3], NCOL[nn], TXT, false);
                    if (r) r->arrow = ((g[0] >> (2 * nn)) & 3) == 1 ? 1 : 2;
                }
                if (g[2] & 0x40) add(tr("Beastmen hold it", "Tenue par les hommes-b\xC3\xAAtes"), "", NCOL[3], 0, false);
            }
            memcpy(&pts, q + 0x90, 4); ptsOk = true;
        } else {                                                // nothing hovered : the balance of power (the client's conq6sta)
            add(tr("Balance of Power", "Rapport de force"), 0, DIM, 0, true);
            for (int rank = 1; rank <= 3; ++rank) for (int nn = 0; nn < 3; ++nn) if (((q[4] >> (nn * 2)) & 3) == rank) {
                char l[48]; sprintf(l, "%d.  %s", rank, NATION[nn]);
                if (Row* r = add(l, "", NCOL[nn], DIM, false)) r->ally = ((q[5] >> (nn + 1)) & 1) != 0;   // the client's ']' : the two allied nations
            }
            memcpy(&pts, q + 0x90, 4); ptsOk = true;
        }
    } else if (last.mode == 1) {
        title = "Besieged"; ptsLabel = tr("Imperial Standing", "Imperial Standing");
        if (!last.conqOk) wait = tr("Waiting for the server...", "En attente du serveur...");
        else if (!q[0x9C]) wait = tr("No Besieged data.", "Aucune donn\xC3\xA9""e Besieged.");
        else {
            unsigned bs = 0; memcpy(&bs, q + 0xA0, 4);
            static const char* const ORDERS[4] = { "Defend Al Zahbi", "Intercept Enemy", "Invade Enemy Base", "Recover the Orb" };
            static const char* const STATUS[8] = { "Training", "Advancing", "Attacking", "Retreating", "Defending", "Preparing", "?", "?" };
            static const char* const FORCE[3] = { "Mamool Ja Savages", "Troll Mercenaries", "Undead Swarm" };
            add(tr("Orders", "Ordres"), 0, DIM, 0, true);
            add(ORDERS[(bs >> 2) & 3], "", GOLD, 0, false);
            add(tr("Battle Status", "\xC3\x89tat des forces"), 0, DIM, 0, true);
            for (int i = 0; i < 3; ++i) {
                const unsigned lv = (bs >> (4 + 4 * i)) & 0xF, st = (bs >> (16 + 3 * i)) & 7;
                char l[48]; sprintf(l, "%s  Lv %u", FORCE[i], lv);
                add(l, STATUS[st], TXT, st == 2 ? 0xFFFF8A7Au : DIM, false);
            }
            memcpy(&pts, q + 0xB0, 4); ptsOk = true;
            if (last.stronghold >= 0) {                         // a stronghold under the cursor (the client's beseige2)
                static const char* const BASE[3] = { "Mamool Ja Training Grounds", "Halvung", "Arrapago Reef" };
                unsigned d = 0; memcpy(&d, q + 0xA4 + 4 * last.stronghold, 4);   // 0-2 status, 3-10 forces, 11-14 level, 15-18 mirrors, 19-22 prisoners
                n = 0; title = BASE[last.stronghold];
                const unsigned st = d & 7, forces = (d >> 3) & 0xFF;
                add(tr("Enemy Forces", "Forces ennemies"), 0, DIM, 0, true);
                if (Row* r = add("", 0, 0xFF8FB8E8u, 0, false)) { r->bar = forces > 100 ? 100 : (int)forces; sprintf(r->v, "%u", forces); r->vc = TXT; }
                char l[48]; sprintf(l, "%s  Lv %u", FORCE[last.stronghold], (d >> 11) & 0xF);
                add(l, STATUS[st], TXT, st == 2 ? 0xFFFF8A7Au : DIM, false);
                sprintf(v, "%u", (d >> 15) & 0xF); add("Archaic Mirrors", v, TXT, TXT, false);
                sprintf(v, "%u", (d >> 19) & 0xF); add(tr("Prisoners", "Prisonniers"), v, TXT, TXT, false);
            }
        }
    } else if (last.mode == 2) {
        title = tr("Campaign", "Campagne"); ptsLabel = tr("Allied Notes", "Allied Notes");
        if (!last.campOk) wait = tr("Waiting for the server...", "En attente du serveur...");
        else {
            unsigned cnt = 0; memcpy(&cnt, last.camp + 0x10, 4);
            add(tr("Controlled Areas", "Zones contr\xC3\xB4l\xC3\xA9""es"), 0, DIM, 0, true);
            for (int nn = 0; nn < 4; ++nn) { sprintf(v, "%u", (cnt >> (5 * nn)) & 0x1F); add(NATION[nn], v, NCOL[nn], TXT, false); }
            memcpy(&pts, last.camp + 0x0C, 4); ptsOk = true;
            // an area under the cursor (the client's camparea) : the hovered record's ZONE -> the area index (the client's
            // marker table 0x056C26F8 entries 16..41, byte 7)
            static const short AREA_ZONE[26] = { 80, 81, 82, 83, 84, 85, 175, 87, 88, 89, 90, 91, 92, 171, 94, 95, 96, 97, 98, 99, 164, 136, 137, 138, 155, 156 };
            static const char* const AREA_NAME[26] = { "Southern San d'Oria [S]", "East Ronfaure [S]", "Jugner Forest [S]", "Vunkerl Inlet [S]", "Batallia Downs [S]",
                "La Vaule [S]", "The Eldieme Necropolis [S]", "Bastok Markets [S]", "North Gustaberg [S]", "Grauberg [S]", "Pashhow Marshlands [S]",
                "Rolanberry Fields [S]", "Beadeaux [S]", "Crawlers' Nest [S]", "Windurst Waters [S]", "West Sarutabaruta [S]", "Fort Karugo-Narugo [S]",
                "Meriphataud Mountains [S]", "Sauromugue Champaign [S]", "Castle Oztroja [S]", "Garlaige Citadel [S]", "Beaucedine Glacier [S]",
                "Xarcabard [S]", "Castle Zvahl Baileys [S]", "Castle Zvahl Keep [S]", "Throne Room [S]" };
            int ar = -1; for (int a = 0; a < 26; ++a) if (AREA_ZONE[a] == last.hover) { ar = a; break; }
            if (ar >= 0 && last.campAreaOk[ar]) {
                const unsigned char* r = last.campArea[ar]; unsigned d0 = 0, d2 = 0; memcpy(&d0, r, 4); memcpy(&d2, r + 8, 4);
                n = 0; ptsOk = false; title = AREA_NAME[ar];
                const int own = (d0 >> 1) & 7, ow = own > 3 ? 3 : own - 1;
                if (own >= 1) add(NATION[ow], (d0 & 1) ? tr("battle", "combat") : "", NCOL[ow], 0xFFFF8A7Au, false);
                sprintf(v, "%u / %u", (d0 >> 4) & 0x3FF, d2 & 0x3FF); add(tr("Fortifications", "Fortifications"), v, TXT, TXT, false);
                sprintf(v, "%u / %u", (d0 >> 14) & 0x3FF, (d2 >> 10) & 0x3FF); add(tr("Resources", "Ressources"), v, TXT, TXT, false);
                add(tr("Influence", "Influence"), 0, DIM, 0, true);
                for (int nn = 0; nn < 4; ++nn) { int p = (int)(r[4 + nn] * 0.4f + 0.5f); if (Row* rr = add(NATION[nn], 0, NCOL[nn], 0, false)) rr->bar = p > 100 ? 100 : p; }
            }
        }
    } else {
        title = tr("Colonization", "Colonisation"); ptsLabel = "Bayld";
        if (!last.coloOk) wait = tr("Waiting for the server...", "En attente du serveur...");
        else {
            memcpy(&pts, last.colo + 0x38, 4); ptsOk = true;
            if (last.hover == 1) {                              // Adoulin under the cursor : the coalition ranks (the client's colorank ; 6 nibbles of pkt+0x10)
                static const char* const COAL[6] = { "Pioneers'", "Peacekeepers'", "Couriers'", "Scouts'", "Inventors'", "Mummers'" };
                unsigned rk = 0; memcpy(&rk, last.colo + 0x10, 4);
                add(tr("Coalition Rank", "Rang de coalition"), 0, DIM, 0, true);
                for (int i = 0; i < 6; ++i) { sprintf(v, "%u", (rk >> (4 * i)) & 0xF); add(COAL[i], v, TXT, GOLD, false); }
            }
        }
    }
    // FIXED width (the longest row of every mode) ; the height follows the rows. In the MAIN MENU's place (top right),
    // where the client shows its own overview
    const float colGap = 18.0f * S;
    const float bw = snap(pad + fo->measure("Mamool Ja Savages  Lv 15", tz) + colGap + fo->measure("Retreating", tz) + pad);
    const int lines = n + (wait ? 1 : 0);
    const float bh = snap(pad + headH + 6.0f * S + (float)lines * rowH + (ptsOk ? 8.0f * S + rowH : 0.0f) + pad);
    const float sx = f.screenW / (float)last.uiW, sy = f.screenH / (float)last.uiH;
    // the client's rule (cnqframe slot 10) : the cursor on the RIGHT half -> the panel on the left, else on the right ; eased
    static float side = 1.0f;                                   // 1 right, 0 left
    // ... but only for a region's detail (Conquest, a region hovered) : the balance of power stays right, Besieged's and
    // Campaign's overviews stay left -- the client's own places (user 2026-11-19)
    const bool regionDetail = (last.mode == 0 && last.hover >= 5 && last.hover <= 23) || (last.mode == 1 && last.stronghold >= 0)
                           || (last.mode == 2 && n > 0 && !ptsOk) || (last.mode == 3 && last.hover == 1);
    const float want = regionDetail ? (last.curX > last.uiW / 2 ? 0.0f : 1.0f) : (last.mode == 1 || last.mode == 2) ? 0.0f : 1.0f;
    side += (want - side) * (1.0f - expf(-dt * 18.0f)); if (fabsf(want - side) < 0.002f) side = want;
    const float xL = 16.0f * sx, xR = f.screenW - 16.0f * sx - bw;
    float bx = snap(xL + (xR - xL) * side), by = snap(48.0f * sy + (1.0f - tO) * 4.0f * S);
    const int jm = menu_place_at(f, __LINE__, bx, by, bx, snap(48.0f * sy), bw, bh);   // its rest place (no slide)
    static int saidMode = -9, saidHover = -9, saidN = 0; static float saidWant = -1.0f;   // a mode change always ; hovers : the first 20
    if (last.mode != saidMode || ((last.hover != saidHover || want != saidWant) && saidN < 20)) { if (last.mode == saidMode) ++saidN; saidMode = last.mode; saidHover = last.hover; saidWant = want;
        windower::debug::log("NATIVEUI region drawn : mode %d, hover %d (home %d), cursor %d,%d of %d -> %s, %d row(s), box %d,%d %dx%d", last.mode, last.hover, last.home, last.curX, last.curY, last.uiW, want > 0.5f ? "right" : "left", n, (int)bx, (int)by, (int)bw, (int)bh); }
    g_frameJoin = jm;
    draw_themed_box(dev, f.skin, bx, by, bw, bh, C.muBox, A, S);
    const float L = bx + pad, W = bw - 2.0f * pad;
    rrect(dev, L, snap(by + pad + headH), W, 1.0f, 0.0f, with_alpha(0x30C8D6EAu, A), with_alpha(0x30C8D6EAu, A));
    const float y0 = snap(by + pad + headH + 6.0f * S) + (wait ? rowH : 0.0f);
    if (ptsOk) rrect(dev, L, snap(y0 + (float)n * rowH + 4.0f * S), W, 1.0f, 0.0f, with_alpha(0x30C8D6EAu, A), with_alpha(0x30C8D6EAu, A));
    // the gauges, the arrows, the alliance bracket (shapes first, then the text)
    const float gx = snap(L + W * 0.48f), gh = snap(5.0f * S);
    int a0 = -1, a1 = -1;
    for (int i = 0; i < n; ++i) {
        const Row& r = rows[i]; const float cy = snap(y0 + (float)i * rowH + rowH * 0.5f);
        if (r.bar >= 0) {   // a row with no label : the gauge takes the row ; a value : the gauge stops before it
            const float g0 = r.l[0] ? gx : snap(L + 8.0f * S), gwr = snap(L + W - g0 - (r.v[0] ? fo->measure(r.v, tz) + 10.0f * S : 0.0f));
            rrect(dev, g0, snap(cy - gh * 0.5f), gwr, gh, gh * 0.5f, with_alpha(0x40000000u, A), with_alpha(0x40000000u, A));
            if (r.bar > 0) rrect(dev, g0, snap(cy - gh * 0.5f), snap(gwr * (float)r.bar / 100.0f), gh, gh * 0.5f, with_alpha(r.lc, A), with_alpha(r.lc, A));
        }
        if (r.arrow) {
            const float ax = snap(L + W - fo->measure("Dominant", tz) - 14.0f * S), as = 4.0f * S;
            const u32 ac = with_alpha(r.arrow == 1 ? 0xFF7CE07Cu : 0xFFE87878u, A);
            if (r.arrow == 1) fill_tri(dev, ax, cy - as, ax - as, cy + as * 0.7f, ax + as, cy + as * 0.7f, ac);
            else fill_tri(dev, ax, cy + as, ax - as, cy - as * 0.7f, ax + as, cy - as * 0.7f, ac);
        }
        if (r.ally) { if (a0 < 0) a0 = i; a1 = i; }
    }
    if (a0 >= 0 && a1 > a0) {                                   // ']' : the two allied nations
        const float bx2 = snap(L + W - 2.0f * S), t = snap(y0 + (float)a0 * rowH + rowH * 0.5f), b = snap(y0 + (float)a1 * rowH + rowH * 0.5f), lw = 1.0f * S, arm = 6.0f * S;
        const u32 bc = with_alpha(0xC8C8D6EAu, A);
        rrect(dev, bx2, t, lw, b - t + lw, 0.0f, bc, bc);
        rrect(dev, bx2 - arm, t, arm, lw, 0.0f, bc, bc);
        rrect(dev, bx2 - arm, b, arm, lw, 0.0f, bc, bc);
        fo->begin(dev);
        const char* al = tr("allied", "alli\xC3\xA9s");
        fo->draw_lc(dev, snap(bx2 - arm - 4.0f * S - fo->measure(al, tz * 0.8f)), snap((t + b) * 0.5f), al, tz * 0.8f, with_alpha(DIM, A), sh, k.ow);
    }
    fo->begin(dev);
    fo->draw_lc(dev, L, snap(by + pad + headH * 0.5f), title, tz, with_alpha(0xFFE8D9B0u, A), sh, k.ow);
    if (wait) fo->draw_lc(dev, L, snap(y0 - rowH * 0.5f), wait, tz * 0.9f, with_alpha(DIM, A), sh, k.ow);
    for (int i = 0; i < n; ++i) {
        const Row& r = rows[i]; const float cy = snap(y0 + (float)i * rowH + rowH * 0.5f);
        fo->draw_lc(dev, r.head ? L : snap(L + 8.0f * S), cy, r.l, r.head ? tz * 0.85f : tz, with_alpha(r.lc, A), sh, k.ow);
        if (r.v[0]) fo->draw_lc(dev, snap(L + W - fo->measure(r.v, tz)), cy, r.v, tz, with_alpha(r.vc, A), sh, k.ow);
    }
    if (ptsOk) {
        const float cy = snap(y0 + (float)n * rowH + 8.0f * S + rowH * 0.5f);
        fmt_thousands(v, pts);
        fo->draw_lc(dev, L, cy, ptsLabel, tz, with_alpha(DIM, A), sh, k.ow);
        fo->draw_lc(dev, snap(L + W - fo->measure(v, tz)), cy, v, tz, with_alpha(GOLD, A), sh, k.ow);
    }
}

// ======== the main menu's SYNTHESIS : the grid (tskill1) where the client opens it (top left), History beside it ========
// The crystal, the 8 slots (2 x 4 : the client's entries 1..4 / 5..8), Synthesize / Cancel ; History : the recipes, newest
// first, a gold mark on the favourites. The grid's rect is published for the materials list (items_draw) to sit beside it.
namespace { Block g_synB, g_hisB; unsigned g_synMs = 0; }
float g_synGrid[4] = { 0, 0, 0, 0 };          // the grid drawn this frame (screen px ; w = 0 : none)
void synth_draw(const Frame& f) {
    const UiConfig& C = ui_config();
    g_synGrid[2] = 0.0f;
    if (!(C.nuHide & NU_MAIN) || !f.font || f.screenH <= 0.0f || f.screenW <= 0.0f) { g_synB.vis = g_hisB.vis = 0.0f; g_synB.wasOpen = g_hisB.wasOpen = false; return; }
    const unsigned now = GetTickCount();
    float dt = g_synMs ? (float)(now - g_synMs) / 1000.0f : 0.0f; g_synMs = now; if (dt > 0.1f) dt = 0.1f;
    const NativeSynth& Y0 = native_synth();
    static NativeSynth last;
    if (Y0.open && Y0.gridOpen) last = Y0;
    else if (Y0.open) { last.histOpen = Y0.histOpen; }
    const bool gridOn = Y0.open && Y0.gridOpen;
    if (!block_ease(g_synB, gridOn, now, dt) || last.gUiW <= 0) { g_hisB.vis = 0.0f; g_hisB.wasOpen = false; return; }
    const Metrics k = metrics(f);
    const float S = k.S, pad = snap(8.0f * S) + k.bi, tz = k.tz, headH = snap(18.0f * S), gap = snap(6.0f * S);
    Font* fo = f.font; const u32 dev = f.dev;
    const float tO = ease_out(((float)(now - g_synB.openMs) / 1000.0f) / 0.08f);
    const float A0 = g_synB.vis * tO; if (A0 <= 0.0f) return;
    const float A = A0 * (last.gridFocused ? 1.0f : 0.85f);
    const u32 sh = with_alpha(0xC8000000u, A);
    const float cell = snap(k.capH * 2.1f), cg = snap(4.0f * S), sep = snap(12.0f * S);
    const char* tSyn = tr("Synthesize", "Synth\xC3\xA9tiser"); const char* tCan = tr("Cancel", "Annuler");
    float btnW = fo->measure(tSyn, tz); { const float w2 = fo->measure(tCan, tz); if (w2 > btnW) btnW = w2; } btnW = snap(btnW + 28.0f * S);
    const float gridW = 4.0f * cell + 3.0f * cg, innerH = 2.0f * cell + cg;
    const float bw = snap(pad + cell + sep + gridW + sep + btnW + pad), bh = snap(pad + headH + 6.0f * S + innerH + pad);
    const float sx = f.screenW / (float)last.gUiW, sy = f.screenH / (float)last.gUiH;
    float bx = snap((float)last.gx0 * sx), by = snap((float)last.gy0 * sy);
    if (bx + bw > f.screenW - 4.0f) bx = snap(f.screenW - 4.0f - bw); if (bx < 4.0f) bx = 4.0f;
    if (by + bh > f.screenH - 4.0f) by = snap(f.screenH - 4.0f - bh); if (by < 4.0f) by = 4.0f;
    const int jm = menu_place(f, __LINE__, bx, by, bw, bh);
    const float by0 = by;                                    // its rest place : History beside it is placed from it
    by = snap(by + (1.0f - tO) * 4.0f * S);
    g_synGrid[0] = bx; g_synGrid[1] = by; g_synGrid[2] = bw; g_synGrid[3] = bh;
    g_frameJoin = jm;
    draw_themed_box(dev, f.skin, bx, by, bw, bh, C.muBox, A, S);
    const float L = bx + pad, hy = snap(by + pad + headH * 0.5f), gy = snap(by + pad + headH + 6.0f * S);
    rrect(dev, L, snap(by + pad + headH), bw - 2.0f * pad, 1.0f, 0.0f, with_alpha(0x30C8D6EAu, A), with_alpha(0x30C8D6EAu, A));
    const float cx0 = L, cy0 = snap(gy + (innerH - cell) * 0.5f);      // the crystal, centred on the two rows
    const float sx0 = snap(L + cell + sep), bx0 = snap(sx0 + gridW + sep);
    const float bH = cell, bY0 = gy, bY1 = snap(gy + cell + cg);
    const u32 red = with_alpha(0xD2FF3838u, A);
    // the cells
    auto well = [&](float x, float y, float w, float h, bool crystal) {
        rrect(dev, x, y, w, h, 5.0f * S, with_alpha(crystal ? 0x70283040u : 0x70101826u, A), with_alpha(0x70080C14u, A));
        rrect_stroke(dev, x, y, w, h, 5.0f * S, with_alpha(crystal ? 0x60FFDA8Au : 0x30C8D6EAu, A), 1.0f);
    };
    well(cx0, cy0, cell, cell, true);
    for (int i = 0; i < 8; ++i) well(snap(sx0 + (float)(i % 4) * (cell + cg)), snap(gy + (float)(i / 4) * (cell + cg)), cell, cell, false);
    // the buttons
    for (int b = 0; b < 2; ++b) {
        const float y = b ? bY1 : bY0; const bool off = b == 0 && !last.synAllowed;
        rrect(dev, bx0, y, btnW, bH, bH * 0.5f, with_alpha(off ? 0x40202630u : (b ? 0x70283040u : 0x70305040u), A), with_alpha(off ? 0x40101418u : 0x70101826u, A));
        rrect_stroke(dev, bx0, y, btnW, bH, bH * 0.5f, with_alpha(off ? 0x20C8D6EAu : 0x50FFDA8Au, A), 1.0f);
    }
    // the cursor : a slot, or a button
    const int cur = last.gridCursor;
    float curX = 0, curY = 0, curW = 0, curH = 0, curR = 5.0f * S;
    if (cur >= 1 && cur <= 8) { curX = snap(sx0 + (float)((cur - 1) % 4) * (cell + cg)); curY = snap(gy + (float)((cur - 1) / 4) * (cell + cg)); curW = curH = cell; }
    else if (cur == 9 || cur == 10) { curX = bx0; curY = cur == 9 ? bY0 : bY1; curW = btnW; curH = bH; curR = bH * 0.5f; }
    if (curW > 0.0f) {
        rrect_clip_begin(dev, curX, curY, curW, curH, curR);
        party_selframe(dev, curX, curY, curW, curH, f.t, A, 0);
        rrect_clip_end(dev);
        rrect_stroke(dev, curX, curY, curW, curH, curR, with_alpha(0xD8FFDA8Au, A), 1.5f * S);
    }
    // the icons : the crystal, the materials ; what the inventory lacks (History) : dimmed, a red cross
    const float z = snap(cell - 8.0f * S), m = snap(6.0f * S), th = snap(2.5f * S);
    if (last.crystalId) {
        const u32 tex = list_icon(f, 3, last.crystalId); const bool miss = last.gridMode == 2 && !last.crystalHave; const float a = A * (miss ? 0.4f : 1.0f);
        if (tex) { dTexQuadState(dev, tex); tquad(dev, snap(cx0 + 4.0f * S), snap(cy0 + 4.0f * S), z, z, 0.0f, 1.0f, 0.0f, 1.0f, with_alpha(0xFFFFFFFFu, a), with_alpha(0xFFFFFFFFu, a)); dSetTex(dev, 0, 0); cs(dev); }
        if (miss) { seg_soft(dev, cx0 + m, cy0 + m, cx0 + cell - m, cy0 + cell - m, th, red); seg_soft(dev, cx0 + cell - m, cy0 + m, cx0 + m, cy0 + cell - m, th, red); }
    }
    for (int i = 0; i < 8; ++i) if (last.slotId[i]) {
        const float x = snap(sx0 + (float)(i % 4) * (cell + cg)), y = snap(gy + (float)(i / 4) * (cell + cg));
        const u32 tex = list_icon(f, 3, last.slotId[i]); const float a = A * (last.slotMiss[i] ? 0.4f : 1.0f);
        if (tex) { dTexQuadState(dev, tex); tquad(dev, snap(x + 4.0f * S), snap(y + 4.0f * S), z, z, 0.0f, 1.0f, 0.0f, 1.0f, with_alpha(0xFFFFFFFFu, a), with_alpha(0xFFFFFFFFu, a)); dSetTex(dev, 0, 0); cs(dev); }
        if (last.slotMiss[i]) { seg_soft(dev, x + m, y + m, x + cell - m, y + cell - m, th, red); seg_soft(dev, x + cell - m, y + m, x + m, y + cell - m, th, red); }
    }
    if (curW > 0.0f && last.gridFocused) {
        const float hand = snap(k.capH * 1.25f), hx = curX - hand * 0.35f + party_cursor_bob(f.t, hand);
        if (g_handTex) party_cursor(dev, g_handTex, hx, curY + curH * 0.5f, hand, 0);
    }
    {   // the mouse : the hand follows, a click validates (a slot : the materials list ; Synthesize ; Cancel)
        const MouseState* ms = f.mouse; int ent = 0;
        if (gridOn && Y0.gridFocused && ms && ms->focused) {
            for (int i = 0; i < 8 && !ent; ++i) { const float x = snap(sx0 + (float)(i % 4) * (cell + cg)), y = snap(gy + (float)(i / 4) * (cell + cg)); if (ms->x >= x && ms->x < x + cell && ms->y >= y && ms->y < y + cell) ent = i + 1; }
            if (!ent && ms->x >= bx0 && ms->x < bx0 + btnW) { if (ms->y >= bY0 && ms->y < bY0 + bH) ent = 9; else if (ms->y >= bY1 && ms->y < bY1 + bH) ent = 10; }
        }
        if (gridOn) {
            g_hit[12][0] = bx / f.screenW; g_hit[12][1] = by / f.screenH; g_hit[12][2] = bw / f.screenW; g_hit[12][3] = bh / f.screenH;
            if (ent && ent != g_synB.hoverRow && ent != Y0.gridCursor) native_synth_grid_hover(ent);
            g_synB.hoverRow = ent;
            if (ent && ms->clicked) native_synth_grid_pick(ent);
        } else g_synB.hoverRow = 0;
    }
    fo->begin(dev);
    fo->draw_lc(dev, L, hy, tr("Synthesis", "Synth\xC3\xA8se"), tz * 0.95f, with_alpha(0xFFE8D9B0u, A), sh, k.ow);
    if (last.gridMode == 2) { const char* t = tr("History", "Historique"); fo->draw_lc(dev, snap(bx + bw - pad - fo->measure(t, tz * 0.85f)), hy, t, tz * 0.85f, with_alpha(0xFF93A6BEu, A), sh, k.ow); }
    if (!last.crystalId) { const char* t = tr("Crystal", "Cristal"); const float zz = tz * 0.72f; fo->draw_lc(dev, snap(cx0 + (cell - fo->measure(t, zz)) * 0.5f), snap(cy0 + cell * 0.5f), t, zz, with_alpha(0xFF56627Au, A), sh, k.ow); }
    for (int i = 0; i < 8; ++i) if (last.slotId[i] && last.slotQty[i] > 1) {
        const float x = snap(sx0 + (float)(i % 4) * (cell + cg)), y = snap(gy + (float)(i / 4) * (cell + cg));
        char n[8]; sprintf(n, "%d", last.slotQty[i]); const float zz = tz * 0.85f;
        fo->draw_lc(dev, snap(x + cell - 4.0f * S - fo->measure(n, zz)), snap(y + cell - 8.0f * S), n, zz, with_alpha(0xFFFFFFFFu, A), sh, k.ow);
    }
    fo->draw_lc(dev, snap(bx0 + (btnW - fo->measure(tSyn, tz)) * 0.5f), snap(bY0 + bH * 0.5f), tSyn, tz, with_alpha(last.synAllowed ? 0xFFF2F5FAu : 0xFF56627Au, A), sh, k.ow);
    fo->draw_lc(dev, snap(bx0 + (btnW - fo->measure(tCan, tz)) * 0.5f), snap(bY1 + bH * 0.5f), tCan, tz, with_alpha(0xFFE4EAF2u, A), sh, k.ow);
    // ======== History (cmbhlst), beside the grid : the recipes, newest first ; the favourites in gold ========
    unsigned iid = 0;
    if (last.gridFocused && cur >= 1 && cur <= 8) iid = last.slotId[cur - 1];
    if (block_ease(g_hisB, Y0.open && Y0.histOpen, now, dt) && last.hVis > 0) {
        NativeItemRow hr[16];
        for (int i = 0; i < 16; ++i) { hr[i].itemId = last.hItem[i]; hr[i].itemCount = 0; hr[i].itemCol = last.hFav[i] ? 0x80909040u : 0x80808080u; hr[i].tempRow = false; hr[i].hasPrice = false; hr[i].price = 0; hr[i].priceCol = 0; }
        char hc[16]; sprintf(hc, "%d / 10", last.hN);
        ItemsBox HL = { hr, last.hN, last.hTop, last.hVis, last.hCursor, 0, last.histFocused, Y0.open && Y0.histOpen, tr("History", "Historique"), hc, false };
        const float hw = keep_width(g_hisB, items_box_width(f, k, HL)), hh = snap(pad + headH + 6.0f * S + (float)last.hVis * snap(k.capH * 1.25f) + pad);
        const float th2 = ease_out(((float)(now - g_hisB.openMs) / 1000.0f) / 0.08f);
        float hx = snap(bx + bw + gap), hyy = by;
        if (hx + hw > f.screenW - 4.0f) hx = snap(f.screenW - 4.0f - hw);
        const int jh = menu_place_at(f, __LINE__, hx, hyy, hx, by0, hw, hh);   // glued beside the grid (theme kit)
        g_frameJoin = jh;                                    // items_box_draw draws its box first
        const unsigned h = items_box_draw(f, k, g_hisB, HL, "cmbhlst", hx, snap(hyy + (1.0f - th2) * 4.0f * S), hw, hh, g_hisB.vis * th2 * A0 * (last.histFocused ? 1.0f : 0.85f), dt, g_hit[13]);
        if (last.histFocused) iid = h;
        // the favourites : a small gold star (a diamond) at the row's right
        const float rowH = snap(k.capH * 1.25f), ry = snap(hyy + pad + headH + 6.0f * S);
        const int nRows = last.hN - last.hTop < last.hVis ? last.hN - last.hTop : last.hVis;
        for (int i = 0; i < nRows; ++i) if (last.hFav[i]) {
            const float dcx = snap(hx + hw - pad - 10.0f * S), dcy = snap(ry + (float)i * rowH + rowH * 0.5f), r = 4.0f * S;
            const u32 gc = with_alpha(0xFFFFD23Cu, g_hisB.vis * th2 * A0);
            tri(dev, dcx, dcy - r, dcx - r * 0.75f, dcy, dcx + r * 0.75f, dcy, gc);
            tri(dev, dcx, dcy + r, dcx - r * 0.75f, dcy, dcx + r * 0.75f, dcy, gc);
        }
    } else if (!Y0.histOpen) { g_hisB.hoverRow = 0; }
    // History up and the grid has the hand (Synthesize) : the recipe's result
    if (!iid && last.gridMode == 2 && last.hCursor >= last.hTop && last.hCursor < last.hTop + last.hVis) iid = last.hItem[last.hCursor - last.hTop];
    if (iid && !card_on_chat(f, k, iid, tO, A0)) {
        card_use(0); card_load(dev, iid);
        if (card_ready(iid)) card_draw(f, k, bx, bw > snap(300.0f * S) ? bw : snap(300.0f * S), snap(by + bh + gap), snap(by + bh + gap), tO, A0, false);
    }
}

void items_draw(const Frame& f) {
    const UiConfig& C = ui_config();
    if (!(C.nuHide & NU_MAIN) || !f.font || f.screenH <= 0.0f || f.screenW <= 0.0f) { g_itB.vis = g_binB.vis = 0.0f; g_itB.wasOpen = g_binB.wasOpen = false; return; }
    const unsigned now = GetTickCount();
    float dt = g_itMs ? (float)(now - g_itMs) / 1000.0f : 0.0f; g_itMs = now; if (dt > 0.1f) dt = 0.1f;
    const NativeItems& I0 = native_items();
    static NativeItems last;
    if (I0.open) last = I0;
    if (!block_ease(g_itB, I0.open, now, dt) || last.uiW <= 0 || last.itVis <= 0) { g_binB.vis = g_qtyB.vis = 0.0f; return; }
    const Metrics k = metrics(f);
    const float S = k.S, pad = snap(8.0f * S) + k.bi, headH = snap(18.0f * S), gap = snap(6.0f * S);
    const float tO = ease_out(((float)(now - g_itB.openMs) / 1000.0f) / 0.08f);
    const float A = g_itB.vis * tO; if (A <= 0.0f) return;
    const float rowH = snap(k.capH * 1.25f);
    const bool bin = last.binMode && last.binVis > 0 && last.binUiW > 0;
    // the inventory (Synthesis : the crystals, the materials)
    char cnt[24]; if (last.invCap > 0) sprintf(cnt, "%d / %d", last.invUsed, last.invCap); else sprintf(cnt, "%d", last.invUsed);
    const char* title = last.synMode == 1 ? tr("Crystals", "Cristaux") : last.synMode == 2 ? tr("Materials", "Mat\xC3\xA9riaux") : tr("Items", "Objets");
    ItemsBox IL = { last.itRow, last.itN, last.itTop, last.itVis, last.itCursor, last.tempRows, last.focused, I0.open,
                    title, last.synMode ? "" : cnt, !last.synMode && last.invCap > 0 && last.invUsed >= last.invCap };
    const float lw = keep_width(g_itB, items_box_width(f, k, IL)), lh = snap(pad + headH + 6.0f * S + (float)last.itVis * rowH + pad);
    const float sx = f.screenW / (float)last.uiW, sy = f.screenH / (float)last.uiH;
    float lx = snap(16.0f * sx), ly = snap(48.0f * sy);       // TOP LEFT, as the client opens it (the bin / the grid push it aside)
    unsigned iid = 0;
    // where the quantity box goes : under the bin, or under the synthesis grid
    float qX = -1.0f, qY = 0.0f, qW = 0.0f; unsigned qid = 0;
    float hintY = 0.0f, hintX = 0.0f;
    if (bin) {
        char bc[24]; if (last.binCap > 0) sprintf(bc, "%d / %d", last.binN, last.binCap); else sprintf(bc, "%d", last.binN);
        static const char* const BAG_EN[18] = { "Inventory", "Mog Safe", "Storage", "Temporary", "Mog Locker", "Mog Satchel", "Mog Sack", "Mog Case", "Mog Wardrobe",
                                                "Mog Safe 2", "Mog Wardrobe 2", "Mog Wardrobe 3", "Mog Wardrobe 4", "Mog Wardrobe 5", "Mog Wardrobe 6", "Mog Wardrobe 7", "Mog Wardrobe 8", "Recycling Bin" };
        static const char* const BAG_FR[18] = { "Inventaire", "Coffre", "EntrepÃ´t", "Temporaire", "Casier", "Sacoche", "Sac", "Valise", "Garde-robe",
                                                "Coffre 2", "Garde-robe 2", "Garde-robe 3", "Garde-robe 4", "Garde-robe 5", "Garde-robe 6", "Garde-robe 7", "Garde-robe 8", "Corbeille" };
        const int bg = last.binBag >= 0 && last.binBag < 18 ? last.binBag : 0x11;
        ItemsBox BL = { last.binRow, last.binN, last.binTop, last.binVis, last.binCursor, 0, last.binFocused, I0.open && I0.binMode,
                        tr(BAG_EN[bg], BAG_FR[bg]), bc, last.binCap > 0 && last.binN >= last.binCap };
        const float bw = keep_width(g_binB, items_box_width(f, k, BL)), bh = snap(pad + headH + 6.0f * S + (float)last.binVis * rowH + pad);
        const float bsx = f.screenW / (float)last.binUiW, bsy = f.screenH / (float)last.binUiH;
        float bx = snap((float)last.bx0 * bsx), by = snap((float)last.by0 * bsy);
        if (by + bh > f.screenH - 4.0f) by = snap(f.screenH - 4.0f - bh); if (by < 4.0f) by = 4.0f; if (bx < 4.0f) bx = 4.0f;
        const int jb = menu_place(f, __LINE__, bx, by, bw, bh);
        if (block_ease(g_binB, I0.open && I0.binMode, now, dt)) {
            const float tB = ease_out(((float)(now - g_binB.openMs) / 1000.0f) / 0.08f);
            g_frameJoin = jb;
            const unsigned b = items_box_draw(f, k, g_binB, BL, "bank", bx, snap(by + (1.0f - tB) * 4.0f * S), bw, bh, g_binB.vis * tB * A, dt, g_hit[11]);
            if (last.binFocused) iid = b;
        }
        const int cr = last.binCursor - last.binTop;
        qX = bx; qY = snap(by + bh + gap); qW = bw; qid = cr >= 0 && cr < last.binVis ? last.binRow[cr].itemId : 0;
        hintX = bx; hintY = snap(by + bh + 9.0f * S);
        lx = snap(bx + bw + gap); ly = by;
    } else { g_binB.vis = 0.0f; g_binB.wasOpen = false; }
    extern float g_trBox[4];
    if ((C.nuHide & NU_TRADE) && native_trade().open && g_trBox[2] > 0.0f) {   // the TRADE's picker : beside the trade, as the client (200,48)
        lx = snap(g_trBox[0] + g_trBox[2] + gap); ly = g_trBox[1];   // (the user 2026-11-20 : it opened BEHIND the trade, top left)
    }
    if (last.synMode == 2 && g_synGrid[2] > 0.0f) {           // the materials : beside the grid, as the client (200,48)
        lx = snap(g_synGrid[0] + g_synGrid[2] + gap); ly = g_synGrid[1];
        const int cr = last.itCursor - last.itTop;
        qX = g_synGrid[0]; qY = snap(g_synGrid[1] + g_synGrid[3] + gap); qW = g_synGrid[2]; qid = cr >= 0 && cr < last.itVis ? last.itRow[cr].itemId : 0;
    }
    // the QUANTITY (itemctrl)
    if (block_ease(g_qtyB, I0.open && (I0.binMode || I0.synMode) && I0.qtyOk && qX >= 0.0f, now, dt) && qX >= 0.0f) {
        const float tq = ease_out(((float)(now - g_qtyB.openMs) / 1000.0f) / 0.07f);
        const float qh = qty_box(f, k, qid, last.qtyVal, last.qtyMax, qX, snap(qY + (1.0f - tq) * 3.0f * S), qW, g_qtyB.vis * tq * A);
        if (qid) iid = qid;
        if (bin) hintY = snap(qY + qh + 9.0f * S);
    }
    if (bin && last.binBag == 0x11) {   // the hint : what a pick does, and that the bin empties itself (the Recycling Bin only)
        Font* fo = f.font; fo->begin(f.dev);
        const char* h = tr("Pick an item : back to your inventory. The bin empties on zoning / logout.", "Choisis un objet : il revient dans l'inventaire. La corbeille se vide au changement de zone / \xC3\xA0 la d\xC3\xA9""connexion.");
        fo->draw_lc(f.dev, hintX, hintY, h, k.tz * 0.82f, with_alpha(0xFF93A6BEu, A * g_binB.vis), with_alpha(0xC8000000u, A * g_binB.vis), k.ow);
    }
    if (lx + lw > f.screenW - 4.0f) lx = snap(f.screenW - 4.0f - lw); if (lx < 4.0f) lx = 4.0f;
    if (ly + lh > f.screenH - 4.0f) ly = snap(f.screenH - 4.0f - lh); if (ly < 4.0f) ly = 4.0f;
    const int jl = menu_place(f, __LINE__, lx, ly, lw, lh);  // glued beside the bin / the grid (theme kit)
    ly = snap(ly + (1.0f - tO) * 4.0f * S);
    const float LA = A * (last.focused ? 1.0f : 0.8f);         // the sort options / Use / Dispose / the grid have the hand : it steps back a little
    g_frameJoin = jl;
    const unsigned i2 = items_box_draw(f, k, g_itB, IL, "inventor", lx, ly, lw, lh, LA, dt, g_hit[10]);
    if (last.focused) iid = i2;
    else if (!bin && !last.synMode && !iid && last.itCursor >= last.itTop && last.itCursor < last.itTop + last.itVis) iid = last.itRow[last.itCursor - last.itTop].itemId;   // Use / Dispose up : its item
    // the item card : ABOVE THE CHAT (under the list when no chat box) ; Synthesis's grid draws its own
    if (iid && !card_on_chat(f, k, iid, tO, A)) {
        const float cw = lw > snap(300.0f * S) ? lw : snap(300.0f * S), ctop = snap(ly + lh + gap);
        card_use(0); card_load(f.dev, iid);
        if (card_ready(iid)) card_draw(f, k, lx, cw, ctop, ctop, tO, A, false);
    }
}
// ======== the NPC SHOP (Game UI -> NU_NPC), in the menus' theme, where the client puts its own ========
// Buy / Sell (shopmain) where the client opens it, the list (shop) top left with each row's price and the gil coin, your
// gil in its header, the quantity (itemctrl) under it with the total, the item card above the chat. The client keeps the
// rules (greyed rows, the price query on a first Sell, the 6-yalm close) and sends every packet ; shopbuy / shopsell are
// AioHUD's confirms (dialog_draw).
namespace { Block g_shMB, g_shLB, g_shQB; unsigned g_shMs = 0; }
void shop_draw(const Frame& f) {
    const UiConfig& C = ui_config();
    if (!(C.nuHide & NU_NPC) || !f.font || f.screenH <= 0.0f || f.screenW <= 0.0f) { g_shMB.vis = g_shLB.vis = g_shQB.vis = 0.0f; g_shMB.wasOpen = g_shLB.wasOpen = g_shQB.wasOpen = false; return; }
    const unsigned now = GetTickCount();
    float dt = g_shMs ? (float)(now - g_shMs) / 1000.0f : 0.0f; g_shMs = now; if (dt > 0.1f) dt = 0.1f;
    const NativeShop& S0 = native_shop();
    static NativeShop last;
    if (S0.owner) { const bool keepList = !S0.open && last.open; const NativeShop prev = last; last = S0; if (keepList) { memcpy(last.row, prev.row, sizeof(last.row)); last.n = prev.n; last.top = prev.top; last.vis = prev.vis; last.cursor = prev.cursor; last.rx0 = prev.rx0; last.ry0 = prev.ry0; last.sell = prev.sell; } }
    if (last.uiW <= 0) return;
    const Metrics k = metrics(f);
    const float S = k.S, pad = snap(8.0f * S) + k.bi, headH = snap(18.0f * S), gap = snap(6.0f * S);
    const float sx = f.screenW / (float)last.uiW, sy = f.screenH / (float)last.uiH;
    // ---- Buy / Sell ----
    if (block_ease(g_shMB, S0.mainOpen, now, dt)) {
        RowSpec rows[2] = {};
        const bool own = last.owner == 4;                              // your bazaar : 1 Set prices, 2 Comment
        rows[0].label = own ? tr("Set prices", "Fixer les prix") : tr("Buy", "Acheter"); rows[0].icon = IC_NONE;
        rows[1].label = own ? tr("Comment", "Commentaire") : tr("Sell", "Vendre"); rows[1].icon = IC_NONE;
        const int cur = last.mainCursor == 1 || last.mainCursor == 2 ? last.mainCursor : 0;
        float bw, bh; block_size(f, k, rows, 2, bw, bh);
        float bx = snap((float)last.mx0 * sx), by = snap((float)last.my0 * sy);
        if (last.my0 * 2 > last.uiH) { float cx, cy; if (place_on_chat(bw, bh, snap(4.0f * S), f.screenW, f.screenH, cx, cy)) { bx = cx; by = cy; } }   // the bottom-left column : above the chat
        if (bx + bw > f.screenW - 4.0f) bx = snap(f.screenW - 4.0f - bw); if (bx < 4.0f) bx = 4.0f;
        if (by + bh > f.screenH - 4.0f) by = snap(f.screenH - 4.0f - bh); if (by < 4.0f) by = 4.0f;
        const int jm = menu_place(f, __LINE__, bx, by, bw, bh);
        const float tO = ease_out(((float)(now - g_shMB.openMs) / 1000.0f) / 0.07f);
        by = snap(by + (1.0f - tO) * 3.0f * S);
        const float A = g_shMB.vis * tO * (last.mainFocused || !S0.mainOpen ? 1.0f : 0.62f);   // the list has the hand : it steps back
        if (A > 0.0f) {
            g_frameJoin = jm;
            const int row = block_draw(f, k, g_shMB, rows, 2, cur, last.mainFocused, bx, by, bw, bh, A, dt);
            if (S0.mainOpen) {
                g_hit[16][0] = bx / f.screenW; g_hit[16][1] = by / f.screenH; g_hit[16][2] = bw / f.screenW; g_hit[16][3] = bh / f.screenH;
                if (S0.mainFocused) {
                    if (row >= 1 && row <= 2 && row != g_shMB.hoverRow && row != S0.mainCursor) native_shopmain_hover(row);
                    g_shMB.hoverRow = row;
                    if (row >= 1 && row <= 2 && f.mouse && f.mouse->clicked) native_shopmain_pick(row);
                } else g_shMB.hoverRow = 0;
            }
        }
    }
    // ---- the list ----
    if (!block_ease(g_shLB, S0.owner && S0.open, now, dt) || last.vis <= 0) { g_shQB.vis = 0.0f; g_shQB.wasOpen = false; return; }
    const float tO = ease_out(((float)(now - g_shLB.openMs) / 1000.0f) / 0.08f);
    const float A = g_shLB.vis * tO; if (A <= 0.0f) return;
    const float rowH = snap(k.capH * 1.25f);
    char gil[24] = ""; if (last.gilOk) { price_text(gil, (int)(last.gil > 999999999u ? 999999999u : last.gil)); strcat(gil, " G"); }
    const char* title = last.owner == 2 ? (last.sell ? tr("Guild : sell", "Guilde : vendre") : tr("Guild : buy", "Guilde : acheter"))
                      : last.owner == 3 ? tr("Bazaar", "Bazar") : last.owner == 4 ? tr("Your bazaar", "Ton bazar")
                      : last.sell ? tr("Sell", "Vendre") : tr("Buy", "Acheter");
    ItemsBox IL = { last.row, last.n, last.top, last.vis, last.cursor, 0, last.focused, S0.open, title, gil, false, true,
                    last.anyTax ? last.tax : 0, tr("Select from your inventory.", "Choisir dans ton inventaire.") };
    const float lw = keep_width(g_shLB, items_box_width(f, k, IL)), lh = snap(pad + headH + 6.0f * S + (float)last.vis * rowH + pad);
    float lx = snap((float)last.rx0 * sx), ly = snap((float)last.ry0 * sy);
    if (lx + lw > f.screenW - 4.0f) lx = snap(f.screenW - 4.0f - lw); if (lx < 4.0f) lx = 4.0f;
    if (ly + lh > f.screenH - 4.0f) ly = snap(f.screenH - 4.0f - lh); if (ly < 4.0f) ly = 4.0f;
    const int jl = menu_place(f, __LINE__, lx, ly, lw, lh);
    ly = snap(ly + (1.0f - tO) * 4.0f * S);
    g_frameJoin = jl;
    const float LA = A * (last.focused || !S0.open ? 1.0f : 0.8f);   // the quantity / the confirm have the hand
    unsigned iid = items_box_draw(f, k, g_shLB, IL, "shop", lx, ly, lw, lh, LA, dt, g_hit[17]);
    const int cr = last.cursor - last.top;
    const NativeItemRow* cur = cr >= 0 && cr < last.vis ? &last.row[cr] : 0;
    if (!iid && cur) iid = cur->itemId;                                // the quantity / a confirm up : the row they are about
    // ---- the quantity, and what it costs / brings ----
    float under = snap(ly + lh + gap);
    if (block_ease(g_shQB, S0.open && S0.qtyOk, now, dt)) {
        const float tq = ease_out(((float)(now - g_shQB.openMs) / 1000.0f) / 0.07f), QA = g_shQB.vis * tq * A;
        const float qh = qty_box(f, k, cur ? cur->itemId : 0, last.qtyVal, last.qtyMax, lx, snap(under + (1.0f - tq) * 3.0f * S), lw, QA);
        const int unit = cur && cur->price >= 0 ? cur->price + (last.anyTax && cr >= 0 && last.tax[cr] > 0 ? last.tax[cr] : 0) : -1;   // a bazaar : the tax too
        if (cur && unit >= 0 && last.qtyVal > 0) {
            char t[40], p[24]; price_text(p, (int)((long long)unit * last.qtyVal > 999999999LL ? 999999999LL : (long long)unit * last.qtyVal));
            sprintf(t, "%s %s G", last.sell ? tr("You get", "Tu re\xC3\xA7ois") : tr("Total", "Total"), p);
            Font* fo = f.font; fo->begin(f.dev);
            fo->draw_lc(f.dev, snap(lx + lw - pad - fo->measure(t, k.tz)), snap(under + qh + 9.0f * S), t, k.tz, with_alpha(0xFFFFDA8Au, QA), with_alpha(0xC8000000u, QA), k.ow);
        }
        under = snap(under + qh + gap);
    }
    // ---- the item card : above the chat (under the list when no chat box) ----
    if (iid && !card_on_chat(f, k, iid, tO, A)) {
        const float cw = lw > snap(300.0f * S) ? lw : snap(300.0f * S), ctop = snap(under + 14.0f * S);
        card_use(0); card_load(f.dev, iid);
        if (card_ready(iid)) card_draw(f, k, lx, cw, ctop, ctop, tO, A, false);
    }
}
// ======== CHECK (`inspect`, Game UI -> NU_ACT) : the target's equipment as our Equipment grid ========
// The name and the job line in the header, the 16 slots (the client's entry order), the linkshell, View wares (entry 17 :
// the target's bazaar, grey without one), the target's comment under it ; the item card above the chat. Hidden while the
// bazaar's list is up (the client opens it OVER the check, at the same place).
namespace { Block g_inB; unsigned g_inMs = 0; }
void inspect_draw(const Frame& f) {
    const UiConfig& C = ui_config();
    if (!(C.nuHide & NU_ACT) || !f.font || f.screenH <= 0.0f || f.screenW <= 0.0f) { g_inB.vis = 0.0f; g_inB.wasOpen = false; return; }
    const unsigned now = GetTickCount();
    float dt = g_inMs ? (float)(now - g_inMs) / 1000.0f : 0.0f; g_inMs = now; if (dt > 0.1f) dt = 0.1f;
    const NativeInspect& I0 = native_inspect();
    static NativeInspect last;
    if (I0.open) last = I0;
    const bool bazaarUp = native_shop().owner == 3 && native_shop().open;
    if (!block_ease(g_inB, I0.open && !bazaarUp, now, dt) || last.uiW <= 0) return;
    const Metrics k = metrics(f);
    const float S = k.S, pad = snap(8.0f * S) + k.bi, tz = k.tz, headH = snap(18.0f * S);
    Font* fo = f.font; const u32 dev = f.dev;
    const float tO = ease_out(((float)(now - g_inB.openMs) / 1000.0f) / 0.08f);
    const float A = g_inB.vis * tO; if (A <= 0.0f) return;
    const u32 sh = with_alpha(0xC8000000u, A);
    static const int SLOT_AT[16] = { 0, 1, 2, 3, 4, 9, 11, 12, 5, 6, 13, 14, 15, 10, 7, 8 };      // Equipment's grid
    static const int ENTRY_OF[16] = { 1, 5, 9, 13, 2, 3, 7, 12, 16, 6, 8, 10, 14, 11, 15, 4 };
    static const char* SLOT_EN[16] = { "Main", "Sub", "Range", "Ammo", "Head", "Body", "Hands", "Legs", "Feet", "Neck", "Waist", "L.Ear", "R.Ear", "L.Ring", "R.Ring", "Back" };
    const float cell = snap(k.capH * 2.1f), cg = snap(4.0f * S), gridW = 4.0f * cell + 3.0f * cg;
    const float lineH = snap(k.capH * 1.2f), btnH = snap(k.capH * 1.5f);
    int cmN = 0; char cm[3][CHAT_TEXT];
    if (last.cmOpen) for (int i = 0; i < 3; ++i) if (last.cmText[i][0]) {
        char out[1][CHAT_TEXT]; unsigned short cc[1][CHAT_TEXT]; int lens[1] = { 0 };
        if (chat_decode(last.cmText[i], out, cc, lens, 1) >= 1) { out[0][lens[0] < CHAT_TEXT ? lens[0] : CHAT_TEXT - 1] = 0; strcpy(cm[cmN++], out[0]); }
    }
    float bw = snap(pad + gridW + pad);
    for (int i = 0; i < cmN; ++i) { const float w = snap(pad + fo->measure(cm[i], tz * 0.9f) + pad); if (w > bw) bw = w; }
    { const float w = snap(pad + fo->measure(last.jobs, tz * 0.85f) + pad); if (w > bw) bw = w; }
    const float gridX = 0.0f;                                     // the grid, centred in the box
    const float bh = snap(pad + headH + lineH + 6.0f * S + gridW + 8.0f * S + (last.ls[0] ? lineH : 0.0f) + btnH + (cmN ? 8.0f * S + (float)cmN * lineH : 0.0f) + pad);
    const float sx = f.screenW / (float)last.uiW, sy = f.screenH / (float)last.uiH;
    float bx = snap((float)last.rx0 * sx), by = snap((float)last.ry0 * sy);
    if (bx + bw > f.screenW - 4.0f) bx = snap(f.screenW - 4.0f - bw); if (bx < 4.0f) bx = 4.0f;
    if (by + bh > f.screenH - 4.0f) by = snap(f.screenH - 4.0f - bh); if (by < 4.0f) by = 4.0f;
    const int jm = menu_place(f, __LINE__, bx, by, bw, bh);
    by = snap(by + (1.0f - tO) * 4.0f * S);
    g_frameJoin = jm;
    draw_themed_box(dev, f.skin, bx, by, bw, bh, C.muBox, A, S);
    const float L = bx + pad, W = bw - 2.0f * pad, hy = snap(by + pad + headH * 0.5f), jy = snap(by + pad + headH + lineH * 0.5f);
    const float gx = snap(L + (W - gridW) * 0.5f + gridX), gy = snap(by + pad + headH + lineH + 6.0f * S);
    rrect(dev, L, snap(by + pad + headH + lineH + 2.0f * S), W, 1.0f, 0.0f, with_alpha(0x30C8D6EAu, A), with_alpha(0x30C8D6EAu, A));
    int curCell = -1;
    for (int c = 0; c < 16; ++c) if (ENTRY_OF[SLOT_AT[c]] == last.cursor) curCell = c;
    const MouseState* ms = f.mouse; int ment = 0;
    for (int c = 0; c < 16; ++c) {
        const float cx = snap(gx + (float)(c % 4) * (cell + cg)), cy = snap(gy + (float)(c / 4) * (cell + cg));
        rrect(dev, cx, cy, cell, cell, 5.0f * S, with_alpha(0x70101826u, A), with_alpha(0x70080C14u, A));
        rrect_stroke(dev, cx, cy, cell, cell, 5.0f * S, with_alpha(0x30C8D6EAu, A), 1.0f);
        if (c == curCell && last.focused) {
            rrect_clip_begin(dev, cx, cy, cell, cell, 5.0f * S); party_selframe(dev, cx, cy, cell, cell, f.t, A, 0); rrect_clip_end(dev);
            rrect_stroke(dev, cx, cy, cell, cell, 5.0f * S, with_alpha(0xD8FFDA8Au, A), 1.5f * S);
        }
        const unsigned id = last.id[SLOT_AT[c]];
        if (id) {
            const u32 tex = list_icon(f, 3, id); const float z = snap(cell - 8.0f * S);
            if (tex) { dTexQuadState(dev, tex); tquad(dev, snap(cx + 4.0f * S), snap(cy + 4.0f * S), z, z, 0.0f, 1.0f, 0.0f, 1.0f, with_alpha(0xFFFFFFFFu, A), with_alpha(0xFFFFFFFFu, A)); dSetTex(dev, 0, 0); cs(dev); }
        }
        if (ms && ms->focused && ms->x >= cx && ms->x < cx + cell && ms->y >= cy && ms->y < cy + cell) ment = ENTRY_OF[SLOT_AT[c]];
    }
    // the linkshell, then View wares (entry 17)
    float y = snap(gy + gridW + 8.0f * S);
    const float lsY = snap(y + lineH * 0.5f); if (last.ls[0]) y = snap(y + lineH);
    const float btX = L, btW = W, btY = y;
    {   const bool on = last.cursor == 17 && last.focused;
        rrect(dev, btX, btY, btW, btnH, btnH * 0.5f, with_alpha(on ? 0x40FFDA8Au : 0x20FFFFFFu, A), with_alpha(on ? 0x30FFDA8Au : 0x14FFFFFFu, A));
        rrect_stroke(dev, btX, btY, btW, btnH, btnH * 0.5f, with_alpha(on ? 0xD8FFDA8Au : 0x40C8D6EAu, A), on ? 1.5f * S : 1.0f);
        if (ms && ms->focused && ms->x >= btX && ms->x < btX + btW && ms->y >= btY && ms->y < btY + btnH) ment = 17;
    }
    if (curCell >= 0 && last.focused) {
        const float cx = snap(gx + (float)(curCell % 4) * (cell + cg)), cy = snap(gy + (float)(curCell / 4) * (cell + cg));
        const float hand = snap(k.capH * 1.25f);
        if (g_handTex) party_cursor(dev, g_handTex, cx - hand * 0.35f + party_cursor_bob(f.t, hand), cy + cell * 0.5f, hand, 0);
    }
    if (I0.open) {
        g_hit[18][0] = bx / f.screenW; g_hit[18][1] = by / f.screenH; g_hit[18][2] = bw / f.screenW; g_hit[18][3] = bh / f.screenH;
        if (ment && ment != g_inB.hoverRow && ment != I0.cursor && I0.focused && (ment != 17 || I0.waresOk)) native_inspect_hover(ment);
        g_inB.hoverRow = ment;
        if (ment && ms && ms->clicked && (ment != 17 || I0.waresOk)) native_inspect_pick(ment);
    }
    fo->begin(dev);
    fo->draw_lc(dev, L, hy, last.name[0] ? last.name : tr("Check", "Examiner"), tz * 0.95f, with_alpha(0xFFE8D9B0u, A), sh, k.ow);
    if (curCell >= 0 && last.focused) { const char* sn = SLOT_EN[SLOT_AT[curCell]]; fo->draw_lc(dev, snap(L + W - fo->measure(sn, tz * 0.85f)), hy, sn, tz * 0.85f, with_alpha(0xFF93A6BEu, A), sh, k.ow); }
    fo->draw_lc(dev, L, jy, last.jobs, tz * 0.85f, with_alpha(0xFF93A6BEu, A), sh, k.ow);
    for (int c = 0; c < 16; ++c) if (!last.id[SLOT_AT[c]]) {
        const float cx = snap(gx + (float)(c % 4) * (cell + cg)), cy = snap(gy + (float)(c / 4) * (cell + cg));
        const char* sn = SLOT_EN[SLOT_AT[c]]; const float z = tz * 0.72f;
        fo->draw_lc(dev, snap(cx + (cell - fo->measure(sn, z)) * 0.5f), snap(cy + cell * 0.5f), sn, z, with_alpha(0xFF56627Au, A), sh, k.ow);
    }
    if (last.ls[0]) {                                              // the linkshell : its item's icon in its colour, then its name
        float tx = L;
        if (last.lsItem) {
            const u32 tex = list_icon(f, 3, last.lsItem); const float z = snap(lineH);
            if (tex) { dTexQuadState(dev, tex); tquad(dev, L, snap(lsY - z * 0.5f), z, z, 0.0f, 1.0f, 0.0f, 1.0f, with_alpha(last.lsArgb, A), with_alpha(last.lsArgb, A)); dSetTex(dev, 0, 0); cs(dev); fo->begin(dev); }
            tx = snap(L + z + 6.0f * S);
        }
        fo->draw_lc(dev, tx, lsY, last.ls, tz * 0.85f, with_alpha(0xFFC8D6EAu, A), sh, k.ow);
    }
    {   const char* t = tr("View wares", "Voir le bazar");
        fo->draw_lc(dev, snap(btX + (btW - fo->measure(t, tz)) * 0.5f), snap(btY + btnH * 0.5f), t, tz, with_alpha(last.waresOk ? 0xFFF2F5FAu : 0xFF56627Au, A), sh, k.ow); }
    for (int i = 0; i < cmN; ++i) fo->draw_lc(dev, L, snap(btY + btnH + 8.0f * S + ((float)i + 0.5f) * lineH), cm[i], tz * 0.9f, with_alpha(0xFFE4EAF2u, A), sh, k.ow);
    // the item card : the slot under the cursor
    const unsigned iid = curCell >= 0 && last.focused ? last.id[SLOT_AT[curCell]] : 0;
    if (iid && !card_on_chat(f, k, iid, tO, A)) {
        const float cw = bw > snap(300.0f * S) ? bw : snap(300.0f * S), ctop = snap(by + bh + 14.0f * S);
        card_use(0); card_load(f.dev, iid);
        if (card_ready(iid)) card_draw(f, k, bx, cw, ctop, ctop, tO, A, false);
    }
}
// ======== the MOG HOUSE's menus (Game UI -> NU_NPC) : the client's entry menus, in our menu style, where it puts them ========
// The label is the client's definition text (mogpost's are images : Receive / Send), in French by a small table.
namespace { Block g_mgB[MOG_MENUS]; unsigned g_mgMs = 0; char g_mgName[MOG_MENUS][9]; }
static const char* mog_label(const char* win, int entry, const char* def, char* buf, int cap) {
    static const struct { const char* en; const char* fr; } FR[] = {
        { "Storage", "Rangement" }, { "Delivery Box", "Bo\xC3\xAEte de livraison" }, { "Change Jobs", "Changer de job" }, { "Gardening", "Jardinage" },
        { "Layout", "Am\xC3\xA9nagement" }, { "Open Mog House", "Ouvrir la Mog House" }, { "Remodel", "R\xC3\xA9nover" },
        { "Mog Safe", "Coffre" }, { "Safe 2", "Coffre 2" }, { "Mog Locker", "Casier" }, { "Satchel", "Sacoche" }, { "Mog Sack", "Sac" }, { "Mog Case", "Valise" },
        { "Auto-sort", "Tri auto" }, { "Manual Sort", "Tri manuel" }, { "Switch", "Changer de sac" }, { "Inventory", "Inventaire" },
        { "Main Job", "Job principal" }, { "Support Job", "Job de soutien" } };
    if (!strcmp(win, "bankmenu")) { if (def && !strcmp(def, "Safe 2")) return tr("Safe 2", "Coffre 2"); if (def && !strcmp(def, "Mog Safe")) return tr("Mog Safe", "Coffre"); }
    if (!strcmp(win, "mogpost")) return entry == 1 ? tr("Receive", "Recevoir") : entry == 2 ? tr("Send", "Envoyer") : "?";
    if (!strcmp(win, "mogdoor") && def && def[0]) return def;          // the door's zone page : the zone's name (the model)
    if (!strcmp(win, "mogdoor")) {                                   // the door : image labels, named from the client's texts (cat 9)
        static const char* const EN[4] = { "Stay in your room", "Area you entered from", "Change floors", "Select an area to exit to" };
        static const char* const FRL[4] = { "Rester dans ta chambre", "Zone d'arrivÃ©""e", "Changer d'Ã©tage", "Choisir une zone de sortie" };
        return entry >= 1 && entry <= 4 ? tr(EN[entry - 1], FRL[entry - 1]) : "?";
    }
    // Layout's menus : image labels, named from their help texts (native-menu-items-2026-11-15)
    {   static const char* const MOVE[2] = { "Move", "DÃ©placer" }, *CLEAR[2] = { "Clear away", "Ranger" }, *PLACE[2] = { "Place", "Placer" },
                                *ALL[2] = { "Clear all", "Tout ranger" }, *QUIT[2] = { "Quit", "Quitter" }, *MANN[2] = { "Mannequin", "Mannequin" };
        const char* const* L2[4] = { MOVE, CLEAR, PLACE, ALL }; const char* const* L1[3] = { PLACE, ALL, QUIT }; const char* const* L4[5] = { MANN, MOVE, CLEAR, PLACE, ALL };
        if (!strcmp(win, "rmlo2") && entry >= 1 && entry <= 4) return tr(L2[entry - 1][0], L2[entry - 1][1]);
        if (!strcmp(win, "rmlo1") && entry >= 1 && entry <= 3) return tr(L1[entry - 1][0], L1[entry - 1][1]);
        if (!strcmp(win, "rmlo4") && entry >= 1 && entry <= 5) return tr(L4[entry - 1][0], L4[entry - 1][1]);
    }
    if (!def || !def[0]) { _snprintf(buf, cap, "#%d", entry); buf[cap - 1] = 0; return buf; }
    if (!strcmp(win, "storage") || !strcmp(win, "storage2") || !strcmp(win, "mogcont") || !strcmp(win, "itmstora")) {
        if (!strcmp(def, "Storage")) return tr("Storage", "Entrep\xC3\xB4t");                       // the bag, not the menu
        if (!strncmp(def, "M.Ward.", 7)) { _snprintf(buf, cap, "%s%s", tr("M.Ward.", "Garde-robe"), def + 7); buf[cap - 1] = 0; return buf; }
    }
    for (size_t i = 0; i < sizeof(FR) / sizeof(FR[0]); ++i) if (!strcmp(def, FR[i].en)) return tr(FR[i].en, FR[i].fr);
    return def;
}
void mog_draw(const Frame& f) {
    const UiConfig& C = ui_config();
    if (!(C.nuHide & NU_NPC) || !f.font || f.screenH <= 0.0f || f.screenW <= 0.0f) { for (int i = 0; i < MOG_MENUS; ++i) { g_mgB[i].vis = 0.0f; g_mgB[i].wasOpen = false; } return; }
    const unsigned now = GetTickCount();
    float dt = g_mgMs ? (float)(now - g_mgMs) / 1000.0f : 0.0f; g_mgMs = now; if (dt > 0.1f) dt = 0.1f;
    const NativeMogMenus& M0 = native_mog_menus();
    static NativeEntryMenu last[MOG_MENUS];
    const Metrics k = metrics(f); const float S = k.S;
    // a slot per menu NAME (its block keeps its fade when another one opens or closes)
    for (int i = 0; i < M0.n; ++i) {
        int s = -1; for (int j = 0; j < MOG_MENUS; ++j) if (!strcmp(g_mgName[j], M0.m[i].name)) s = j;
        if (s < 0) for (int j = 0; j < MOG_MENUS && s < 0; ++j) { bool used = false; for (int q = 0; q < M0.n; ++q) if (!strcmp(g_mgName[j], M0.m[q].name)) used = true; if (!used && g_mgB[j].vis <= 0.0f) { s = j; lstrcpynA(g_mgName[j], M0.m[i].name, 9); g_mgB[j] = Block(); } }
        if (s >= 0) last[s] = M0.m[i];
    }
    // the moogle's menu (myroom / mogext) is the ROOT, drawn first ; its submenus (Storage's bags, Change Jobs' Main /
    // Support, the Delivery Box's Receive / Send) open BESIDE it, to its right, their bottoms level (2026-11-21 : "quand on
    // fait storage il s'ouvre par dessus le menu du moogle" -- the client opens them over it, our rows are taller)
    auto is_root = [](const char* n) { return !strcmp(n, "myroom") || !strcmp(n, "mogext") || !strcmp(n, "mogdoor"); };
    auto is_child = [](const char* n) { return !strcmp(n, "storage") || !strcmp(n, "storage2") || !strcmp(n, "jobchang") || !strcmp(n, "mogpost") || !strcmp(n, "bankmenu"); };
    float rootR = -1.0f, rootB = 0.0f;                                   // the root's right edge and bottom this frame (-1 : not drawn)
    float ynX = -1.0f, ynY = 0.0f, ynW = 0.0f;                           // the storage's sort options : its Yes / No hangs under them
    for (int pass = 0; pass < 2; ++pass)
    for (int s = 0; s < MOG_MENUS; ++s) {
        if (!g_mgName[s][0]) continue;
        if ((pass == 0) != is_root(g_mgName[s])) continue;
        bool open = false; for (int i = 0; i < M0.n; ++i) if (!strcmp(M0.m[i].name, g_mgName[s])) open = true;
        // a bag's list is up : the moogle's menus under it step away (the user 2026-11-21 : "le menu du moogle reste ouvert
        // quand on va dans storage") -- only the list's own sort options stay
        { const NativeItems& It = native_items();
          if (It.open && It.binMode && It.binBag != 0x11 && strcmp(g_mgName[s], "itmsortw") && strcmp(g_mgName[s], "itmstora")) open = false; }
        // the job grid is up : the moogle's menu and Main / Support step away too (2026-11-21 : "les box restent ouvertes en
        // cascade et sont mal placees") -- the grid's title says which job is chosen
        if (native_jobsel().open && (!strcmp(g_mgName[s], "myroom") || !strcmp(g_mgName[s], "mogext") || !strcmp(g_mgName[s], "jobchang"))) open = false;
        if (native_roomlist().open && (!strcmp(g_mgName[s], "myroom") || !strcmp(g_mgName[s], "mogext"))) open = false;   // Remodel's styles : at the same place
        const NativeEntryMenu& L = last[s];
        if (!block_ease(g_mgB[s], open, now, dt) || L.n <= 0 || L.uiW <= 0) { if (!open && g_mgB[s].vis <= 0.0f) g_mgName[s][0] = 0; continue; }
        RowSpec rows[32] = {}; char bufs[32][32]; int cur = 0;
        // the moogle : Layout (5) and Remodel (7) grey while entry 6 is not = your Mog House is OPEN (live 2026-11-21 : states
        // 11/11/11 closed, 5/11/5 open ; 5/5/5 a transition, not "open")
        bool mhOpen = false;
        if (!strcmp(L.name, "myroom")) { int s5 = -1, s6 = -1, s7 = -1;
            for (int i = 0; i < L.n; ++i) { if (L.entry[i] == 5) s5 = L.state[i]; if (L.entry[i] == 6) s6 = L.state[i]; if (L.entry[i] == 7) s7 = L.state[i]; }
            mhOpen = s5 == 5 && s7 == 5 && s6 >= 0 && s6 != 5; }
        for (int i = 0; i < L.n && i < 32; ++i) {
            rows[i].label = mog_label(L.name, L.entry[i], L.label[i], bufs[i], sizeof(bufs[i])); rows[i].icon = IC_NONE;
            if (mhOpen && L.entry[i] == 6) rows[i].label = tr("Close Mog House", "Fermer la Mog House");   // the client's image toggles
            rows[i].off = L.state[i] == 5;
            row_image(f.dev, L, i, rows[i], mhOpen && L.entry[i] == 6);   // the client's own label image, when it has one
            if (L.entry[i] == L.cursor) cur = i + 1;
        }
        float bw, bh; block_size(f, k, rows, L.n, bw, bh);
        const float sx = f.screenW / (float)L.uiW, sy = f.screenH / (float)L.uiH;
        float bx = snap((float)L.rx0 * sx), by = snap((float)L.ry0 * sy);
        if (L.ry1 > L.ry0 && L.ry0 * 2 > L.uiH) {                    // the client's bottom-left column (it sits ON the chat) :
            by = snap((float)L.ry1 * sy - bh);                            // above AioHUD's chat, as the NPC boxes (2026-11-21 :
            float cx, cy; if (place_on_chat(bw, bh, snap(4.0f * S), f.screenW, f.screenH, cx, cy)) { bx = cx; by = cy; } }   // "il faut qu'il soit au-dessus du chat comme le reste")
        if (is_child(L.name) && rootR >= 0.0f) { bx = snap(rootR + 6.0f * S); by = snap(rootB - bh); }   // beside the moogle's menu
        if (bx + bw > f.screenW - 4.0f) bx = snap(f.screenW - 4.0f - bw); if (bx < 4.0f) bx = 4.0f;
        if (by + bh > f.screenH - 4.0f) by = snap(f.screenH - 4.0f - bh); if (by < 4.0f) by = 4.0f;
        const int jm = menu_place(f, __LINE__ * 16 + s, bx, by, bw, bh);
        if (is_root(L.name) && open) { rootR = bx + bw; rootB = by + bh; }
        const float tO = ease_out(((float)(now - g_mgB[s].openMs) / 1000.0f) / 0.07f);
        by = snap(by + (1.0f - tO) * 3.0f * S);
        const float A = g_mgB[s].vis * tO * (L.focused || !open ? 1.0f : 0.62f);
        if (A <= 0.0f) continue;
        g_frameJoin = jm;
        const int row = block_draw(f, k, g_mgB[s], rows, L.n, cur, L.focused, bx, by, bw, bh, A, dt);
        if (open && !strcmp(L.name, "itmsortw")) { ynX = bx; ynY = by + bh; ynW = bw; }
        if (open && L.focused) {
            hit_extend(16, by + bh, f.screenH);
            if (g_hit[16][2] <= 0.0f) { g_hit[16][0] = bx / f.screenW; g_hit[16][1] = by / f.screenH; g_hit[16][2] = bw / f.screenW; g_hit[16][3] = bh / f.screenH; }
            const int ent = (row >= 1 && row <= L.n) ? L.entry[row - 1] : 0;
            if (ent && row != g_mgB[s].hoverRow && ent != L.cursor && L.state[row - 1] != 5) native_mog_hover(L.name, ent);
            g_mgB[s].hoverRow = row;
            if (ent && L.state[row - 1] != 5 && f.mouse && f.mouse->clicked) native_mog_pick(L.name, ent);
        } else g_mgB[s].hoverRow = 0;
    }
    {   // a storage's sort : its confirmation (sortyn) under the sort options, Yes / No (as the items' own, mainmenu_draw)
        static Block g_mgYnB; const NativeEntryMenu& YN = native_main_yn(); static NativeEntryMenu lastY;
        if (YN.open) lastY = YN;
        if (block_ease(g_mgYnB, YN.open && ynX >= 0.0f, now, dt) && lastY.n && ynX >= 0.0f) {
            RowSpec yrows[4] = {}; char ynums[4][12]; int ycur = 0, yn = 0;
            for (int i = 0; i < lastY.n && yn < 4; ++i, ++yn) {
                const int e = lastY.entry[i];
                if (e == 1) yrows[yn].label = tr("Yes", "Oui"); else if (e == 2) yrows[yn].label = tr("No", "Non");
                else { sprintf(ynums[yn], "#%d", e); yrows[yn].label = ynums[yn]; }
                yrows[yn].icon = IC_NONE; if (e == lastY.cursor) ycur = yn + 1; }
            float yw, yh; block_size(f, k, yrows, yn, yw, yh);
            const float tY = ease_out(((float)(now - g_mgYnB.openMs) / 1000.0f) / 0.07f);
            float ybx = snap(ynX + ynW - yw), yby = snap(ynY + 3.0f * S + (1.0f - tY) * 3.0f * S);
            const int jy = menu_place_at(f, __LINE__, ybx, yby, ybx, snap(ynY + 3.0f * S), yw, yh);
            const float YA = g_mgYnB.vis * tY;
            if (YA > 0.0f) {
                g_frameJoin = jy;
                const int yrow = block_draw(f, k, g_mgYnB, yrows, yn, ycur, lastY.focused, ybx, yby, yw, yh, YA, dt);
                if (YN.open) {
                    g_hit[8][0] = ybx / f.screenW; g_hit[8][1] = yby / f.screenH; g_hit[8][2] = yw / f.screenW; g_hit[8][3] = yh / f.screenH;
                    const int ent = (yrow >= 1 && yrow <= yn) ? lastY.entry[yrow - 1] : 0;
                    if (ent && yrow != g_mgYnB.hoverRow && ent != YN.cursor) native_main_yn_hover(ent);
                    g_mgYnB.hoverRow = yrow;
                    if (ent && f.mouse && f.mouse->clicked) native_main_yn_pick(ent);
                } else g_mgYnB.hoverRow = 0;
            }
        }
    }
}
// ======== Remodel's styles (`roomlist`, Game UI -> NU_NPC) : the client's text rows, as a menu above the chat ========
namespace { Block g_rmB; unsigned g_rmMs = 0; }
void room_draw(const Frame& f) {
    const UiConfig& C = ui_config();
    if (!(C.nuHide & NU_NPC) || !f.font || f.screenH <= 0.0f || f.screenW <= 0.0f) { g_rmB.vis = 0.0f; g_rmB.wasOpen = false; return; }
    const unsigned now = GetTickCount();
    float dt = g_rmMs ? (float)(now - g_rmMs) / 1000.0f : 0.0f; g_rmMs = now; if (dt > 0.1f) dt = 0.1f;
    const NativeRoomList& R0 = native_roomlist();
    static NativeRoomList last;
    if (R0.open) last = R0;
    if (!block_ease(g_rmB, R0.open, now, dt) || last.uiW <= 0 || last.vis <= 0) return;
    const Metrics k = metrics(f); const float S = k.S;
    RowSpec rows[8] = {}; const int n = last.n - last.top < last.vis ? last.n - last.top : last.vis;
    for (int i = 0; i < n; ++i) { rows[i].label = last.label[i]; rows[i].icon = IC_NONE; }
    float bw, bh; block_size(f, k, rows, n, bw, bh);
    const float sx = f.screenW / (float)last.uiW, sy = f.screenH / (float)last.uiH;
    float bx = snap((float)last.rx0 * sx), by = snap((float)last.ry0 * sy);
    { float cx, cy; if (place_on_chat(bw, bh, snap(4.0f * S), f.screenW, f.screenH, cx, cy)) { bx = cx; by = cy; } }   // above the chat
    const int jm = menu_place(f, __LINE__, bx, by, bw, bh);
    const float tO = ease_out(((float)(now - g_rmB.openMs) / 1000.0f) / 0.07f);
    by = snap(by + (1.0f - tO) * 3.0f * S);
    const float A = g_rmB.vis * tO * (last.focused || !R0.open ? 1.0f : 0.62f); if (A <= 0.0f) return;
    g_frameJoin = jm;
    const int cur = last.cursor >= last.top && last.cursor < last.top + n ? last.cursor - last.top + 1 : 0;
    const int row = block_draw(f, k, g_rmB, rows, n, cur, last.focused, bx, by, bw, bh, A, dt);
    if (R0.open && R0.focused) {
        hit_extend(16, by + bh, f.screenH);
        if (g_hit[16][2] <= 0.0f) { g_hit[16][0] = bx / f.screenW; g_hit[16][1] = by / f.screenH; g_hit[16][2] = bw / f.screenW; g_hit[16][3] = bh / f.screenH; }
        if (row >= 1 && row <= n && row != g_rmB.hoverRow && last.top + row - 1 != R0.cursor) native_textlist_hover("roomlist", last.top + row - 1);
        g_rmB.hoverRow = row;
        if (row >= 1 && row <= n && f.mouse && f.mouse->clicked) native_textlist_pick("roomlist", last.top + row - 1);
    } else g_rmB.hoverRow = 0;
}
// ======== the AUCTION HOUSE (Game UI -> NU_NPC) : its menus, the item list, the price history, the price, your items ========
// The menus are images in the client : named here from their help lines (live 2026-11-21). One menu at a time -- the
// deepest open, at the client's top right ; the list top left, the history beside it, the price under the history ; the
// item card above the chat.
namespace { Block g_auMB, g_auLB, g_auHB, g_auPB, g_auIB; unsigned g_auMs = 0; char g_auMName[9]; }
// an entry's label from its HELP line (the client draws an image) : a phrase in the help -> EN / FR ; else the help
// itself, shortened ("View smithing materials on auction." -> "Smithing materials")
static const char* auc_label(const char* help, char* buf, int cap) {
    struct L { const char* key; const char* en; const char* fr; };
    static const L T[] = {
        // scrolls
        { "white magic", "White Magic", "Blanche" }, { "black magic", "Black Magic", "Noire" }, { "summoning", "Summoning", "Invocation" },
        { "ninjutsu", "Ninjutsu", "Ninjutsu" }, { "songs", "Songs", "Chants" }, { "dice", "Dice", "D\xC3\xA9s" }, { "geomanc", "Geomancy", "G\xC3\xA9omancie" },
        // auc1 / auc3 / a sort
        { "all merchandise up for auction", "Bid", "Acheter" }, { "Place unwanted items", "Sell", "Vendre" }, { "currently placed on auction", "Sales Status", "Mes ventes" },
        { "recent sales data", "Price History", "Historique" }, { "Place a bid", "Bid", "Ench\xC3\xA9rir" }, { "Remove merchandise", "Remove", "Retirer" },
        { "Rearrange the order", "Sort", "Trier" }, { "Return to list", "Back", "Retour" },
        { "highest defense", "Defense", "D\xC3\xA9""fense" }, { "elemental resistance", "Resistances", "R\xC3\xA9sistances" },
        { "required level", "Level", "Niveau" }, { "usable by current job", "Your Job", "Ton job" }, { "usable by your race", "Your Race", "Ta race" },
        { "highest damage", "Damage", "D\xC3\xA9g\xC3\xA2ts" }, { "attack delay", "Delay", "D\xC3\xA9lai" },
        // auc2
        { "View weapons", "Weapons", "Armes" }, { "View armor", "Armor", "Armures" }, { "magic scrolls", "Scrolls", "Parchemins" },
        { "View medicines", "Medicines", "Rem\xC3\xA8""des" }, { "View furnishings", "Furnishings", "Mobilier" },
        { "synthesis materials", "Materials", "Mat\xC3\xA9riaux" }, { "View food", "Food", "Nourriture" }, { "View crystals", "Crystals", "Cristaux" },
        { "other miscellaneous", "Others", "Divers" },
        // armor
        { "Shields", "Shields", "Boucliers" }, { "headwear", "Head", "T\xC3\xAAte" }, { "neckwear", "Neck", "Cou" }, { "body armor", "Body", "Corps" },
        { "hands and forearms", "Hands", "Mains" }, { "Belts", "Waist", "Taille" }, { "legwear", "Legs", "Jambes" }, { "footwear", "Feet", "Pieds" },
        { "Capes", "Back", "Dos" }, { "Earrings", "Earrings", "Oreilles" }, { "Rings", "Rings", "Anneaux" },
        // weapons
        { "hand-to-hand", "Hand-to-Hand", "Poings" }, { "Daggers", "Daggers", "Dagues" }, { "One-handed swords", "Swords", "\xC3\x89p\xC3\xA9""es" },
        { "Two-handed swords", "Great Swords", "Gr. \xC3\xA9p\xC3\xA9""es" }, { "One-handed axes", "Axes", "Haches" }, { "Two-handed axes", "Great Axes", "Gr. haches" },
        { "scythes", "Scythes", "Faux" }, { "polearms", "Polearms", "Lances" }, { "One-handed katana", "Katana", "Katana" }, { "Two-handed katana", "Great Katana", "Gr. katana" },
        { "clubs", "Clubs", "Masses" }, { "staves", "Staves", "B\xC3\xA2tons" }, { "ranged weapons", "Ranged", "Distance" },
        { "Musical instruments", "Instruments", "Instruments" }, { "Arrows, bullets", "Ammo", "Munitions" }, { "grips", "Grips", "Poign\xC3\xA9""es" },
        // materials
        { "View smithing", "Smithing", "Forge" }, { "goldsmithing", "Goldsmithing", "Orf\xC3\xA8vrerie" }, { "clothcraft", "Clothcraft", "Couture" },
        { "leatherworking", "Leathercraft", "Tannerie" }, { "bonecraft", "Bonecraft", "Os" }, { "woodworking", "Woodworking", "Menuiserie" },
        { "alchemy materials 2", "Alchemy 2", "Alchimie 2" }, { "alchemy materials", "Alchemy", "Alchimie" },
        // food
        { "View meals", "Meals", "Plats" }, { "View fish ", "Fish", "Poissons" }, { "View ammunition", "Ammunition", "Munitions" },
        { "fishing gear", "Fishing Gear", "P\xC3\xAA""che" }, { "pet items", "Pet Items", "Familiers" }, { "View ingredients", "Ingredients", "Ingr\xC3\xA9""dients" }, { "meat and egg", "Meat & Eggs", "Viandes" },
        { "seafood dishes", "Seafood", "Mer" }, { "vegetable dishes", "Vegetables", "L\xC3\xA9gumes" }, { "soups", "Soups", "Soupes" },
        { "bread and rice", "Breads & Rice", "Pains" }, { "sweets", "Sweets", "Desserts" }, { "drinks", "Drinks", "Boissons" },
    };
    if (!help || !help[0]) { lstrcpynA(buf, "?", cap); return buf; }
    for (size_t i = 0; i < sizeof(T) / sizeof(T[0]); ++i) {          // a case-insensitive phrase match
        const char* k = T[i].key; const size_t kl = strlen(k);
        for (const char* p = help; *p; ++p) if (!_strnicmp(p, k, kl)) return tr(T[i].en, T[i].fr);
    }
    // the help, shortened : no "View " / "Display ", no " on auction." / " scrolls." / ".", a capital first
    const char* p = help; if (!_strnicmp(p, "View ", 5)) p += 5; else if (!_strnicmp(p, "Display ", 8)) p += 8;
    lstrcpynA(buf, p, cap); char* e;
    if ((e = strstr(buf, " on auction")) != 0) *e = 0; if ((e = strstr(buf, " up for auction")) != 0) *e = 0;
    size_t l = strlen(buf); while (l && (buf[l - 1] == '.' || buf[l - 1] == ',' || buf[l - 1] == ' ')) buf[--l] = 0;
    for (int i = 0; buf[i]; ++i) if ((i == 0 || buf[i - 1] == ' ') && buf[i] >= 'a' && buf[i] <= 'z') buf[i] = (char)(buf[i] - 32);
    return buf;
}
// the price typed : the client's value and its edited digit (its keys change it), your gil under it. Returns its height.
static float auc_price_box(const Frame& f, const Metrics& k, const NativeAuction& a, float x, float y, float w, float A) {
    const UiConfig& C = ui_config(); const float S = k.S, pad = snap(8.0f * S) + k.bi, headH = snap(18.0f * S), tz = k.tz;
    const float lineH = snap(k.capH * 1.5f), h = snap(pad + headH + 6.0f * S + lineH + 4.0f * S + k.capH * 1.2f + pad);
    const u32 dev = f.dev, sh = with_alpha(0xC8000000u, A); Font* fo = f.font;
    draw_themed_box(dev, f.skin, x, y, w, h, C.muBox, A, S);
    const float L = x + pad, W = w - 2.0f * pad, hy = snap(y + pad + headH * 0.5f), ly = snap(y + pad + headH + 6.0f * S);
    rrect(dev, L, snap(y + pad + headH), W, 1.0f, 0.0f, with_alpha(0x30C8D6EAu, A), with_alpha(0x30C8D6EAu, A));
    rrect(dev, L, ly, W, lineH, 4.0f * S, with_alpha(0x30FFDA8Au, A), with_alpha(0x18FFDA8Au, A));
    rrect_stroke(dev, L, ly, W, lineH, 4.0f * S, with_alpha(a.priceFocused ? 0xD8FFDA8Au : 0x80FFDA8Au, A), a.priceFocused ? 1.5f * S : 1.0f * S);
    const float cz = snap(k.capH), vz = tz * 1.1f, vx = snap(L + 10.0f * S + cz), vy = snap(ly + lineH * 0.5f);
    int pos = 0; for (unsigned d = a.priceDigit; d >= 10 && pos < 9; d /= 10) ++pos;
    char raw[16]; sprintf(raw, "%u", a.priceVal); const int nd = (int)strlen(raw);
    char dig[16]; const int want = nd > pos + 1 ? nd : pos + 1;
    for (int i = 0; i < want - nd; ++i) dig[i] = '0'; memcpy(dig + (want - nd), raw, (size_t)nd + 1);
    char v[28]; int ci = -1; { int o = 0; for (int i = 0; i < want; ++i) { if (i && (want - i) % 3 == 0) v[o++] = ','; if (want - 1 - i == pos) ci = o; v[o++] = dig[i]; } v[o] = 0; }
    if (ci >= 0) { char pre[28]; lstrcpynA(pre, v, ci + 1); const float ux = fo->measure(pre, vz), cw = fo->measure("0", vz);
        rrect(dev, snap(vx + ux - 1.0f * S), snap(vy - vz * 0.6f), snap(cw + 2.0f * S), snap(vz * 1.2f), 2.0f * S, with_alpha(0x60E86A5Au, A), with_alpha(0x60E86A5Au, A)); }
    gil_coin(dev, snap(L + 5.0f * S + cz * 0.5f), vy, cz, A);
    fo->begin(dev);
    fo->draw_lc(dev, L, hy, a.sell ? tr("Selling price", "Prix de vente") : tr("Your bid", "Ton ench\xC3\xA8re"), tz * 0.95f, with_alpha(0xFFE8D9B0u, A), sh, k.ow);
    fo->draw_lc(dev, vx, vy, v, vz, with_alpha(0xFFFFFFFFu, A), sh, k.ow);
    if (a.gilOk) { char g[24], t[48]; price_text(g, (int)(a.gil > 999999999u ? 999999999u : a.gil)); _snprintf(t, sizeof(t), "%s %s G", tr("Your gil", "Tes gils"), g); t[47] = 0;
        fo->draw_lc(dev, snap(L + W - fo->measure(t, tz * 0.85f)), snap(ly + lineH + 4.0f * S + k.capH * 0.6f), t, tz * 0.85f,
                    with_alpha(!a.sell && a.priceVal > a.gil ? 0xFFFF8A6Au : 0xFF93A6BEu, A), sh, k.ow); }
    return h;
}
// the price history : the last ten sales -- the date, the seller -> the buyer, the price
static float auc_hist_width(const Frame& f, const Metrics& k) {
    Font* fo = f.font; const float S = k.S, pad = snap(8.0f * S) + k.bi, tz = k.tz;
    return snap(pad + 8.0f * S + fo->measure("00/00/00", tz * 0.85f) + 12.0f * S + 2.0f * fo->measure("Wwwwwwwwwwww", tz * 0.9f) + fo->measure(" > ", tz) + 12.0f * S
                + fo->measure("999,999,999", tz) + 8.0f * S + snap(k.capH) + 8.0f * S + pad);
}
static void auc_hist_draw(const Frame& f, const Metrics& k, const NativeAuction& a, bool live, float x, float y, float w, float h, float A, float dt) {
    const UiConfig& C = ui_config(); const float S = k.S, pad = snap(8.0f * S) + k.bi, headH = snap(18.0f * S), tz = k.tz;
    const u32 dev = f.dev, sh = with_alpha(0xC8000000u, A); Font* fo = f.font;
    const float rowH = snap(k.capH * 1.25f);
    draw_themed_box(dev, f.skin, x, y, w, h, C.muBox, A, S);
    const float L = x + pad, W = w - 2.0f * pad, hy = snap(y + pad + headH * 0.5f), ry = snap(y + pad + headH + 6.0f * S);
    rrect(dev, L, snap(y + pad + headH), W, 1.0f, 0.0f, with_alpha(0x30C8D6EAu, A), with_alpha(0x30C8D6EAu, A));
    for (int i = 0; i < a.hn; ++i) if ((i & 1) == 0) rrect(dev, L, snap(ry + (float)i * rowH), W, rowH, 3.0f * S, with_alpha(0x18FFFFFFu, A), with_alpha(0x10FFFFFFu, A));
    const int cr = a.hcursor >= 0 && a.hcursor < a.hn ? a.hcursor : -1;
    if (cr >= 0 && a.histFocused) {
        const float targetY = ry + (float)cr * rowH;
        if (g_auHB.curY < 0.0f) g_auHB.curY = targetY;
        g_auHB.curY += (targetY - g_auHB.curY) * (1.0f - expf(-dt * 30.0f)); if (fabsf(targetY - g_auHB.curY) < 0.3f) g_auHB.curY = targetY;
        const float yy = snap(g_auHB.curY);
        rrect_clip_begin(dev, L, yy, W, rowH, rowH * 0.5f); party_selframe(dev, L, yy, W, rowH, f.t, A, 0); rrect_clip_end(dev);
        rrect_stroke(dev, L, yy, W, rowH, rowH * 0.5f, with_alpha(0xC8FFDA8Au, A), 1.0f);
    } else g_auHB.curY = -1.0f;
    const float cz = snap(k.capH * 0.95f), colR = snap(L + W - 8.0f * S - cz - 4.0f * S);
    int sold = 0;
    for (int i = 0; i < a.hn; ++i) if (a.hist[i].price >= 0 || a.hist[i].seller[0]) { ++sold; gil_coin(dev, snap(L + W - 8.0f * S - cz * 0.5f), snap(ry + (float)i * rowH + rowH * 0.5f), cz, A); }
    const MouseState* ms = f.mouse;
    if (live) {
        int mrow = -1;
        if (a.histFocused && ms && ms->focused && ms->x >= L && ms->x < L + W && ms->y >= ry && ms->y < ry + (float)a.hn * rowH) mrow = (int)((ms->y - ry) / rowH);
        hit_extend(18, y + h, f.screenH);
        if (g_hit[18][2] <= 0.0f) { g_hit[18][0] = x / f.screenW; g_hit[18][1] = y / f.screenH; g_hit[18][2] = w / f.screenW; g_hit[18][3] = h / f.screenH; }
        if (mrow >= 0 && mrow + 1 != g_auHB.hoverRow && mrow != a.hcursor) native_textlist_hover("auchisto", mrow);
        g_auHB.hoverRow = mrow + 1;
    } else g_auHB.hoverRow = 0;
    fo->begin(dev);
    fo->draw_lc(dev, L, hy, tr("Price history", "Historique des prix"), tz * 0.95f, with_alpha(0xFFE8D9B0u, A), sh, k.ow);
    if (!sold) fo->draw_lc(dev, snap(L + 8.0f * S), snap(ry + rowH * 0.5f), tr("No sales yet.", "Aucune vente."), tz, with_alpha(0xFF6E7C8Eu, A), sh, k.ow);
    const float dW = fo->measure("00/00/00", tz * 0.85f), nW = fo->measure("Wwwwwwwwwwww", tz * 0.9f);
    for (int i = 0; i < a.hn; ++i) {
        const NativeAucHist& r = a.hist[i]; const float cy = snap(ry + (float)i * rowH + rowH * 0.5f);
        if (r.price < 0 && !r.seller[0]) continue;                       // a blank row (an item never sold)
        char d[16] = "--"; { const time_t t = (time_t)r.date; struct tm* tm = r.date ? gmtime(&t) : 0;
            if (tm) { if (ui_config().lang == 1) sprintf(d, "%02d/%02d/%02d", tm->tm_mday, tm->tm_mon + 1, tm->tm_year % 100); else sprintf(d, "%02d/%02d/%02d", tm->tm_mon + 1, tm->tm_mday, tm->tm_year % 100); } }
        float tx = snap(L + 8.0f * S);
        fo->draw_lc(dev, tx, cy, d, tz * 0.85f, with_alpha(0xFF93A6BEu, A), sh, k.ow); tx = snap(tx + dW + 12.0f * S);
        char cut[40];
        fo->draw_lc(dev, tx, cy, fit_text(fo, r.seller, tz * 0.9f, nW, cut, sizeof(cut)), tz * 0.9f, with_alpha(0xFFE4EAF2u, A), sh, k.ow); tx = snap(tx + nW);
        fo->draw_lc(dev, tx, cy, " > ", tz, with_alpha(0xFF6E7C8Eu, A), sh, k.ow); tx = snap(tx + fo->measure(" > ", tz));
        fo->draw_lc(dev, tx, cy, fit_text(fo, r.buyer, tz * 0.9f, nW, cut, sizeof(cut)), tz * 0.9f, with_alpha(0xFFE4EAF2u, A), sh, k.ow);
        char p[24]; price_text(p, r.price);
        fo->draw_lc(dev, snap(colR - fo->measure(p, tz)), cy, p, tz, with_alpha(0xFFFFDA8Au, A), sh, k.ow);
    }
}
void auc_draw(const Frame& f) {
    const UiConfig& C = ui_config();
    Block* all[5] = { &g_auMB, &g_auLB, &g_auHB, &g_auPB, &g_auIB };
    if (!(C.nuHide & NU_NPC) || !f.font || f.screenH <= 0.0f || f.screenW <= 0.0f) { for (Block* b : all) { b->vis = 0.0f; b->wasOpen = false; } return; }
    const unsigned now = GetTickCount();
    float dt = g_auMs ? (float)(now - g_auMs) / 1000.0f : 0.0f; g_auMs = now; if (dt > 0.1f) dt = 0.1f;
    const NativeAuction& A0 = native_auction();
    static NativeAuction last;
    if (A0.up) {   // a list closing keeps its rows while it fades
        const NativeAuction prev = last; last = A0;
        if (!A0.listOpen && prev.listOpen) { memcpy(last.row, prev.row, sizeof(last.row)); last.n = prev.n; last.top = prev.top; last.vis = prev.vis; last.cursor = prev.cursor; last.lx0 = prev.lx0; last.ly0 = prev.ly0; memcpy(last.cat, prev.cat, 9); last.catEntry = prev.catEntry; memcpy(last.catHelp, prev.catHelp, sizeof(last.catHelp)); }
        if (!A0.histOpen && prev.histOpen) { memcpy(last.hist, prev.hist, sizeof(last.hist)); last.hn = prev.hn; last.hcursor = prev.hcursor; }
        if (!A0.invOpen && prev.invOpen) { memcpy(last.invRow, prev.invRow, sizeof(last.invRow)); last.invN = prev.invN; last.invTop = prev.invTop; last.invVis = prev.invVis; last.invCursor = prev.invCursor; last.ix0 = prev.ix0; last.iy0 = prev.iy0; }
        if (!A0.menuOpen && prev.menuOpen) { last.menu = prev.menu; memcpy(last.help, prev.help, sizeof(last.help)); }
    }
    if (last.uiW <= 0) return;
    const Metrics k = metrics(f);
    const float S = k.S, pad = snap(8.0f * S) + k.bi, headH = snap(18.0f * S), gap = snap(6.0f * S), rowH = snap(k.capH * 1.25f);
    const float sx = f.screenW / (float)last.uiW, sy = f.screenH / (float)last.uiH;
    // ---- the menu : one, the deepest, at the client's top right ----
    if (A0.menuOpen && strcmp(g_auMName, A0.menu.name)) { lstrcpynA(g_auMName, A0.menu.name, 9); g_auMB.openMs = now; g_auMB.hoverRow = 0; }
    if (block_ease(g_auMB, A0.up && A0.menuOpen, now, dt) && last.menu.n > 0) {
        const NativeEntryMenu& M = last.menu;
        RowSpec rows[16] = {}; int cur = 0; const int n = M.n < 16 ? M.n : 16; static char lb[16][48];
        for (int i = 0; i < n; ++i) { rows[i].label = auc_label(last.help[i], lb[i], sizeof(lb[i])); rows[i].icon = IC_NONE; rows[i].off = M.state[i] == 5; if (M.entry[i] == M.cursor) cur = i + 1; row_image(f.dev, M, i, rows[i]); }
        float bw, bh; block_size(f, k, rows, n, bw, bh);
        float bx = snap((float)M.rx1 * sx - bw), by = snap((float)M.ry0 * sy);   // its right edge where the client's is
        if (bx + bw > f.screenW - 4.0f) bx = snap(f.screenW - 4.0f - bw); if (bx < 4.0f) bx = 4.0f;
        if (by + bh > f.screenH - 4.0f) by = snap(f.screenH - 4.0f - bh); if (by < 4.0f) by = 4.0f;
        const int jm = menu_place(f, __LINE__, bx, by, bw, bh);
        const float tO = ease_out(((float)(now - g_auMB.openMs) / 1000.0f) / 0.07f);
        by = snap(by + (1.0f - tO) * 3.0f * S);
        const float MA = g_auMB.vis * tO * (M.focused || !A0.menuOpen ? 1.0f : 0.62f);
        if (MA > 0.0f) {
            g_frameJoin = jm;
            const int row = block_draw(f, k, g_auMB, rows, n, cur, M.focused, bx, by, bw, bh, MA, dt);
            if (A0.menuOpen && M.focused) {
                g_hit[16][0] = bx / f.screenW; g_hit[16][1] = by / f.screenH; g_hit[16][2] = bw / f.screenW; g_hit[16][3] = bh / f.screenH;
                const int ent = (row >= 1 && row <= n) ? M.entry[row - 1] : 0;
                if (ent && row != g_auMB.hoverRow && ent != M.cursor && M.state[row - 1] != 5) native_auc_hover(M.name, ent);
                g_auMB.hoverRow = row;
                if (ent && M.state[row - 1] != 5 && f.mouse && f.mouse->clicked) native_auc_pick(M.name, ent);
            } else g_auMB.hoverRow = 0;
        }
    }
    // ---- the left column : the item list (Buy) or your items (Sell) ----
    unsigned iid = 0; float colR = -1.0f, colY = 0.0f, colB = 0.0f;
    if (block_ease(g_auLB, A0.up && A0.listOpen, now, dt) && last.vis > 0) {
        const float tO = ease_out(((float)(now - g_auLB.openMs) / 1000.0f) / 0.08f), LA = g_auLB.vis * tO * (last.listFocused || !A0.listOpen ? 1.0f : 0.8f);
        char tb[48]; const char* title = last.status ? tr("Your sales", "Tes ventes") : last.catHelp[0] ? auc_label(last.catHelp, tb, sizeof(tb)) : tr("Auction", "H\xC3\xB4tel des ventes");
        char cnt[16]; sprintf(cnt, "%d", last.n);
        bool salePrices = false; for (int i = 0; i < last.vis && i < 16; ++i) if (last.row[i].hasPrice) salePrices = true;   // Sales status : each sale's price
        ItemsBox IL = { last.row, last.n, last.top, last.vis, last.cursor, 0, last.listFocused, A0.listOpen, title, cnt, false, salePrices, 0, 0 };
        const float lw = keep_width(g_auLB, items_box_width(f, k, IL)), lh = snap(pad + headH + 6.0f * S + (float)last.vis * rowH + pad);
        float lx = snap((float)last.lx0 * sx), ly = snap((float)last.ly0 * sy);
        if (lx + lw > f.screenW - 4.0f) lx = snap(f.screenW - 4.0f - lw); if (lx < 4.0f) lx = 4.0f;
        if (ly + lh > f.screenH - 4.0f) ly = snap(f.screenH - 4.0f - lh); if (ly < 4.0f) ly = 4.0f;
        const int jl = menu_place(f, __LINE__, lx, ly, lw, lh);
        ly = snap(ly + (1.0f - tO) * 4.0f * S);
        if (LA > 0.0f) {
            g_frameJoin = jl;
            const unsigned r = items_box_draw(f, k, g_auLB, IL, "auclist", lx, ly, lw, lh, LA, dt, g_hit[17]);
            const int c = last.cursor - last.top;
            iid = r ? r : (c >= 0 && c < last.vis ? last.row[c].itemId : 0);   // the history / the bid up : the row they are about
            colR = lx + lw; colY = ly; colB = ly + lh;
        }
    }
    if (block_ease(g_auIB, A0.up && A0.sell && A0.invOpen, now, dt) && last.invVis > 0) {
        const float tO = ease_out(((float)(now - g_auIB.openMs) / 1000.0f) / 0.08f), IA = g_auIB.vis * tO * (last.invFocused || !A0.invOpen ? 1.0f : 0.8f);
        char gil[24] = ""; if (last.gilOk) { price_text(gil, (int)(last.gil > 999999999u ? 999999999u : last.gil)); strcat(gil, " G"); }
        ItemsBox IL = { last.invRow, last.invN, last.invTop, last.invVis, last.invCursor, 0, last.invFocused, A0.invOpen, tr("Sell", "Vendre"), gil, false, false, 0, 0 };
        const float lw = keep_width(g_auIB, items_box_width(f, k, IL)), lh = snap(pad + headH + 6.0f * S + (float)last.invVis * rowH + pad);
        float lx = snap((float)last.ix0 * sx), ly = snap((float)last.iy0 * sy);
        if (colR >= 0.0f) { lx = snap(colR + gap); ly = colY; }
        if (lx + lw > f.screenW - 4.0f) lx = snap(f.screenW - 4.0f - lw); if (lx < 4.0f) lx = 4.0f;
        if (ly + lh > f.screenH - 4.0f) ly = snap(f.screenH - 4.0f - lh); if (ly < 4.0f) ly = 4.0f;
        const int ji = menu_place(f, __LINE__, lx, ly, lw, lh);
        ly = snap(ly + (1.0f - tO) * 4.0f * S);
        if (IA > 0.0f) {
            g_frameJoin = ji;
            const unsigned r = items_box_draw(f, k, g_auIB, IL, "inventor", lx, ly, lw, lh, IA, dt, g_hit[17]);
            const int c = last.invCursor - last.invTop;
            const unsigned ii = r ? r : (c >= 0 && c < last.invVis ? last.invRow[c].itemId : 0);
            if (ii) iid = ii;
            colR = lx + lw; colY = ly; colB = ly + lh;
        }
    }
    // ---- the history beside the column, the price under it ----
    float hx = colR >= 0.0f ? snap(colR + gap) : snap((float)last.hx0 * sx), hy = colR >= 0.0f ? colY : snap((float)last.hy0 * sy);
    const float hw = auc_hist_width(f, k), hh = snap(pad + headH + 6.0f * S + 10.0f * rowH + pad);
    if (hx + hw > f.screenW - 4.0f) hx = snap(f.screenW - 4.0f - hw); if (hx < 4.0f) hx = 4.0f;
    float under = colR >= 0.0f ? snap(colB + gap) : hy;
    if (block_ease(g_auHB, A0.up && A0.histOpen, now, dt)) {
        const int jh = menu_place(f, __LINE__, hx, hy, hw, hh);
        const float tO = ease_out(((float)(now - g_auHB.openMs) / 1000.0f) / 0.08f), HA = g_auHB.vis * tO * (last.histFocused || !A0.histOpen ? 1.0f : 0.85f);
        const float yy = snap(hy + (1.0f - tO) * 4.0f * S);
        if (HA > 0.0f) { g_frameJoin = jh; auc_hist_draw(f, k, last, A0.histOpen, hx, yy, hw, hh, HA, dt); }
        under = snap(hy + hh + gap);
    }
    if (block_ease(g_auPB, A0.up && A0.priceUp, now, dt)) {
        const bool beside = A0.histOpen || g_auHB.vis > 0.0f;            // under the history ; else beside the column
        const float pw = beside ? hw : snap(300.0f * S);
        const float tO = ease_out(((float)(now - g_auPB.openMs) / 1000.0f) / 0.07f), PA = g_auPB.vis * tO;
        float pxx = beside || colR < 0.0f ? hx : snap(colR + gap);
        if (!beside && colR >= 0.0f) under = colY;
        if (pxx + pw > f.screenW - 4.0f) pxx = snap(f.screenW - 4.0f - pw);
        if (PA > 0.0f) {
            const float ph = auc_price_box(f, k, last, pxx, snap(under + (1.0f - tO) * 3.0f * S), pw, PA);
            hit_extend(18, under + ph, f.screenH);
            if (g_hit[18][2] <= 0.0f) { g_hit[18][0] = pxx / f.screenW; g_hit[18][1] = under / f.screenH; g_hit[18][2] = pw / f.screenW; g_hit[18][3] = ph / f.screenH; }
        }
    }
    // ---- the item card : above the chat ----
    if (iid && A0.up && !native_dialog().open) card_on_chat(f, k, iid, 1.0f, g_auLB.vis > g_auIB.vis ? g_auLB.vis : g_auIB.vis);   // (no chat box : no card ; a confirm up : none)
}
// ======== the GENERIC engine's menus (Game UI -> NU_NPC) : any plain one-column menu no module draws ========
// Its label : the definition's text, else its help line (auc_label : a phrase table, else the help shortened). At the
// client's place ; one in the bottom half goes above the chat.
namespace { Block g_gnB[GEN_MENUS]; unsigned g_gnMs = 0; char g_gnName[GEN_MENUS][9]; }
void gen_draw(const Frame& f) {
    const UiConfig& C = ui_config();
    if (!(C.nuHide & NU_NPC) || !f.font || f.screenH <= 0.0f || f.screenW <= 0.0f) { for (int i = 0; i < GEN_MENUS; ++i) { g_gnB[i].vis = 0.0f; g_gnB[i].wasOpen = false; } return; }
    const unsigned now = GetTickCount();
    float dt = g_gnMs ? (float)(now - g_gnMs) / 1000.0f : 0.0f; g_gnMs = now; if (dt > 0.1f) dt = 0.1f;
    const NativeGenMenus& G0 = native_gen_menus();
    static NativeEntryMenu last[GEN_MENUS]; static char lastHelp[GEN_MENUS][16][72];
    const Metrics k = metrics(f); const float S = k.S;
    for (int i = 0; i < G0.n; ++i) {                                   // a slot per menu NAME (its fade survives the others)
        int s = -1; for (int j = 0; j < GEN_MENUS; ++j) if (!strcmp(g_gnName[j], G0.m[i].name)) s = j;
        if (s < 0) for (int j = 0; j < GEN_MENUS && s < 0; ++j) { bool used = false; for (int q = 0; q < G0.n; ++q) if (!strcmp(g_gnName[j], G0.m[q].name)) used = true; if (!used && g_gnB[j].vis <= 0.0f) { s = j; lstrcpynA(g_gnName[j], G0.m[i].name, 9); g_gnB[j] = Block(); } }
        if (s >= 0) { last[s] = G0.m[i]; memcpy(lastHelp[s], G0.help[i], sizeof(lastHelp[s])); }
    }
    for (int s = 0; s < GEN_MENUS; ++s) {
        if (!g_gnName[s][0]) continue;
        bool open = false; for (int i = 0; i < G0.n; ++i) if (!strcmp(G0.m[i].name, g_gnName[s])) open = true;
        const NativeEntryMenu& L = last[s];        if (!block_ease(g_gnB[s], open, now, dt) || L.n <= 0 || L.uiW <= 0) { if (!open && g_gnB[s].vis <= 0.0f) g_gnName[s][0] = 0; continue; }
        RowSpec rows[16] = {}; static char bufs[GEN_MENUS][16][48]; int cur = 0; const int n = L.n < 16 ? L.n : 16;
        for (int i = 0; i < n; ++i) {
            rows[i].label = L.label[i][0] ? L.label[i] : auc_label(lastHelp[s][i], bufs[s][i], sizeof(bufs[s][i]));
            rows[i].icon = IC_NONE; rows[i].off = L.state[i] == 5;
            if (L.entry[i] == L.cursor) cur = i + 1;
            if (!L.label[i][0]) row_image(f.dev, L, i, rows[i]);
        }
        float bw, bh; block_size(f, k, rows, n, bw, bh);
        const float sx = f.screenW / (float)L.uiW, sy = f.screenH / (float)L.uiH;
        float bx = snap((float)L.rx0 * sx), by = snap((float)L.ry0 * sy);
        if (L.ry1 > L.ry0 && L.ry0 * 2 > L.uiH) { by = snap((float)L.ry1 * sy - bh); float cx, cy; if (place_on_chat(bw, bh, snap(4.0f * S), f.screenW, f.screenH, cx, cy)) { bx = cx; by = cy; } }
        if (L.rx0 * 2 > L.uiW) bx = snap((float)L.rx1 * sx - bw);       // a right-side menu : its right edge where the client's is
        // MacroPalette : the book list opens IN PLACE of mcrmenu (not drawn under it) ; the book's options hang beside OUR
        // list, on its row (the client places them beside its own, narrower list -- they covered ours, 2026-11-25)
        if (!strcmp(L.name, "mcrmenu") && (native_macro_books().open || native_equip_sets().open)) continue;
        if (!strcmp(L.name, "mcresmn") && g_esAt[2] > 0.0f) { bx = snap(g_esAt[0] - snap(3.0f * S) - bw); by = snap(g_esAt[1]); }
        if (!strcmp(L.name, "mcrselop") && g_mbkAt[2] > 0.0f) { bx = snap(g_mbkAt[0] - snap(3.0f * S) - bw); by = snap(g_mbkAt[1]); }
        if (!strcmp(L.name, "mcrbedit") && g_medAt[0][2] > 0.0f) { bx = snap(g_medAt[0][0] + snap(3.0f * S)); by = snap(g_medAt[0][1]); }
        if (!strcmp(L.name, "mcrledit") && g_medAt[1][2] > 0.0f) { bx = snap(g_medAt[1][0] + snap(3.0f * S)); by = snap(g_medAt[1][1]); }
        if (bx + bw > f.screenW - 4.0f) bx = snap(f.screenW - 4.0f - bw); if (bx < 4.0f) bx = 4.0f;
        if (by + bh > f.screenH - 4.0f) by = snap(f.screenH - 4.0f - bh); if (by < 4.0f) by = 4.0f;
        const int jm = menu_place(f, __LINE__ * 16 + s, bx, by, bw, bh);
        const float tO = ease_out(((float)(now - g_gnB[s].openMs) / 1000.0f) / 0.07f);
        by = snap(by + (1.0f - tO) * 3.0f * S);
        const float A = g_gnB[s].vis * tO * (L.focused || !open ? 1.0f : 0.62f);
        if (A <= 0.0f) continue;
        g_frameJoin = jm;
        const int row = block_draw(f, k, g_gnB[s], rows, n, cur, L.focused, bx, by, bw, bh, A, dt);
        if (open && L.focused) {
            hit_extend(16, by + bh, f.screenH);
            if (g_hit[16][2] <= 0.0f) { g_hit[16][0] = bx / f.screenW; g_hit[16][1] = by / f.screenH; g_hit[16][2] = bw / f.screenW; g_hit[16][3] = bh / f.screenH; }
            const int ent = (row >= 1 && row <= n) ? L.entry[row - 1] : 0;
            static float mv[4][2]; const bool moved = mouse_moved(f.mouse, mv[s & 3]);
            if (ent && moved && row != g_gnB[s].hoverRow && ent != L.cursor && L.state[row - 1] != 5) native_gen_hover(L.name, ent);
            g_gnB[s].hoverRow = row;
            if (ent && L.state[row - 1] != 5 && f.mouse && f.mouse->clicked) native_gen_pick(L.name, ent);
        } else g_gnB[s].hoverRow = 0;
    }
}
// the generic engine's LIST : an item list (icon, name, count, price) when it holds items, else a text list ; the
// title the client's top bar shows ; the item card above the chat
namespace { Block g_glB; unsigned g_glMs = 0; }
void gen_list_draw(const Frame& f) {
    const UiConfig& C = ui_config();
    if (!(C.nuHide & NU_NPC) || !f.font || f.screenH <= 0.0f || f.screenW <= 0.0f) { g_glB.vis = 0.0f; g_glB.wasOpen = false; return; }
    const unsigned now = GetTickCount();
    float dt = g_glMs ? (float)(now - g_glMs) / 1000.0f : 0.0f; g_glMs = now; if (dt > 0.1f) dt = 0.1f;
    const NativeGenMenus& G0 = native_gen_menus();
    static NativeGenMenus last; static char title[0xCC];
    if (G0.list) { last = G0; const NativeTopBar& tb = native_topbar(); if (tb.title.text[0]) lstrcpynA(title, tb.title.text, sizeof(title)); }
    if (!block_ease(g_glB, G0.list, now, dt) || last.uiW <= 0 || last.lvis <= 0) return;
    const Metrics k = metrics(f);
    const float S = k.S, pad = snap(8.0f * S) + k.bi, headH = snap(18.0f * S), tz = k.tz, rowH = snap(k.capH * 1.25f);
    const float sx = f.screenW / (float)last.uiW, sy = f.screenH / (float)last.uiH;
    const float tO = ease_out(((float)(now - g_glB.openMs) / 1000.0f) / 0.08f), A = g_glB.vis * tO * (last.listFocused || !G0.list ? 1.0f : 0.8f);
    if (A <= 0.0f) return;
    unsigned iid = 0;
    if (last.anyItem) {                                                // an ITEM list : the items box (icon, name, count, price)
        NativeItemRow rows[16]; memset(rows, 0, sizeof(rows));
        for (int i = 0; i < last.lvis; ++i) { rows[i].itemId = last.lrow[i].item; rows[i].itemCount = last.lrow[i].count > 0 ? last.lrow[i].count : 0; rows[i].itemCol = last.lrow[i].col ? last.lrow[i].col : 0x80808080u;
            rows[i].hasPrice = last.anyPrice; rows[i].price = last.lrow[i].price >= 0 ? last.lrow[i].price : -2; }
        ItemsBox IL = { rows, last.ln, last.ltop, last.lvis, last.lcursor, 0, last.listFocused, G0.list, title[0] ? title : "", 0, false, last.anyPrice, 0, 0 };
        const float lw = keep_width(g_glB, items_box_width(f, k, IL)), lh = snap(pad + headH + 6.0f * S + (float)last.lvis * rowH + pad);
        float lx = snap((float)last.lx0 * sx), ly = snap((float)last.ly0 * sy);
        if (lx + lw > f.screenW - 4.0f) lx = snap(f.screenW - 4.0f - lw); if (lx < 4.0f) lx = 4.0f;
        if (ly + lh > f.screenH - 4.0f) ly = snap(f.screenH - 4.0f - lh); if (ly < 4.0f) ly = 4.0f;
        const int jl = menu_place(f, __LINE__, lx, ly, lw, lh); g_frameJoin = jl;
        const unsigned r = items_box_draw(f, k, g_glB, IL, last.listName, lx, snap(ly + (1.0f - tO) * 4.0f * S), lw, lh, A, dt, g_hit[17]);
        const int c = last.lcursor - last.ltop; iid = r ? r : (c >= 0 && c < last.lvis ? last.lrow[c].item : 0);
    } else {                                                           // a TEXT list : one line per row, a number on the right
        RowSpec rows[16] = {}; static char nb[16][24]; int cur = 0; const int n = last.ln - last.ltop < last.lvis ? last.ln - last.ltop : last.lvis;
        for (int i = 0; i < n; ++i) {
            rows[i].label = last.lrow[i].text[0] ? last.lrow[i].text : "-"; rows[i].icon = IC_NONE;
            {   const unsigned c = last.lrow[i].col;                     // the client's row colour (grids-re s.1) : its meaning, in our palette
                if (c == 0x80509050u) rows[i].tint = 0xFF9CE27Au;           // set / equipped : green
                else if (c == 0x80909040u) rows[i].tint = 0xFFFFDA8Au;      // a command row : gold
                else if (c == 0x80404040u) rows[i].off = true;              // refused : grey
                else if (c == 0x80804020u) rows[i].mark = true; }          // picked as the swap source : the gold rim
            if (last.lrow[i].price >= 0) { price_text(nb[i], last.lrow[i].price); strcat(nb[i], " G"); rows[i].extra = nb[i]; }
            else if (last.lrow[i].count >= 0) { sprintf(nb[i], "%d", last.lrow[i].count); rows[i].extra = nb[i]; }
            if (last.ltop + i == last.lcursor) cur = i + 1;
        }
        float bw, bh; block_size(f, k, rows, n, bw, bh);
        float bx = snap((float)last.lx0 * sx), by = snap((float)last.ly0 * sy);
        if (last.ly0 * 2 > last.uiH) { float cx, cy; if (place_on_chat(bw, bh, snap(4.0f * S), f.screenW, f.screenH, cx, cy)) { bx = cx; by = cy; } }
        if (bx + bw > f.screenW - 4.0f) bx = snap(f.screenW - 4.0f - bw); if (bx < 4.0f) bx = 4.0f;
        if (by + bh > f.screenH - 4.0f) by = snap(f.screenH - 4.0f - bh); if (by < 4.0f) by = 4.0f;
        const int jm = menu_place(f, __LINE__, bx, by, bw, bh); g_frameJoin = jm;
        const int row = block_draw(f, k, g_glB, rows, n, cur, last.listFocused, bx, snap(by + (1.0f - tO) * 3.0f * S), bw, bh, A, dt);
        if (G0.list && last.listFocused) {
            g_hit[17][0] = bx / f.screenW; g_hit[17][1] = by / f.screenH; g_hit[17][2] = bw / f.screenW; g_hit[17][3] = bh / f.screenH;
            if (row >= 1 && row <= n && row != g_glB.hoverRow && last.ltop + row - 1 != last.lcursor) native_textlist_hover(last.listName, last.ltop + row - 1);
            g_glB.hoverRow = row;
            if (row >= 1 && row <= n && f.mouse && f.mouse->clicked) native_textlist_pick(last.listName, last.ltop + row - 1);
        } else g_glB.hoverRow = 0;
        (void)tz;
    }
    if (iid && G0.list && !native_dialog().open) card_on_chat(f, k, iid, tO, A);
}
// ======== MacroPalette -> Edit Macro : the BOOK LIST (mcrselec) -- 20 books a page, the one in use in gold, a row being
// renamed shows what is typed ; a header band "Macro Books  < 1 / 2 >" like the main menu's. The client keeps the keys ;
// the mouse hovers / picks a row, a click on an arrow turns the page (its entries 21 / 22). ========
namespace { Block g_mbkB; unsigned g_mbkMs = 0; }
void macro_books_draw(const Frame& f) {
    const UiConfig& C = ui_config();
    g_hit[20][2] = 0.0f; g_mbkAt[2] = 0.0f;
    if (!(C.nuHide & NU_MAIN) || !f.font || f.screenH <= 0.0f || f.screenW <= 0.0f) { g_mbkB.vis = 0.0f; g_mbkB.wasOpen = false; return; }
    const unsigned now = GetTickCount();
    float dt = g_mbkMs ? (float)(now - g_mbkMs) / 1000.0f : 0.0f; g_mbkMs = now; if (dt > 0.1f) dt = 0.1f;
    const NativeMacroBooks& B0 = native_macro_books();
    static NativeMacroBooks last;
    if (B0.open) last = B0;
    const bool editing = native_macro_edit().pal || native_macro_edit().ed;   // the editor opens in its place
    if (!block_ease(g_mbkB, B0.open && !editing, now, dt) || last.uiW <= 0 || last.uiH <= 0) return;
    const Metrics k = metrics(f), kN = no_icons(k);
    const float S = k.S;
    RowSpec rows[20]; static char lab[20][24], num[20][8]; int cur = 0;
    for (int i = 0; i < 20; ++i) {
        const int book = last.page * 20 + i;
        if (last.renaming == i + 1) { _snprintf(lab[i], sizeof(lab[i]), "%s%s", last.rename, (now / 500) % 2 ? "_" : " "); lab[i][23] = 0; rows[i].tint = 0xFFFFFFFFu; }
        else { lstrcpynA(lab[i], last.title[i][0] ? last.title[i] : "-", sizeof(lab[i])); if (book == last.current) rows[i].tint = 0xFFFFDA8Au; }
        sprintf(num[i], "%d", book + 1);
        rows[i].label = lab[i]; rows[i].icon = IC_NONE; rows[i].sub = false; rows[i].off = false; rows[i].extra = num[i];
        if (last.cursor == i + 1) cur = i + 1;
    }
    float bw = 0.0f, bh = 0.0f; block_size(f, kN, rows, 20, bw, bh);
    const float tzH = k.tz * 0.92f, headH = snap(k.capH + k.gap);
    const char* title = tr("Macro Books", "Livres de macros");
    char pg[16]; sprintf(pg, "%d / 2", last.page + 1);
    {   const float need = snap(k.gutter + f.font->measure(title, tzH) + 14.0f * S + f.font->measure("2 / 2", tzH) + 18.0f * S + k.pad) + 2.0f * k.bi;
        if (need > bw) bw = need; }
    bw = keep_width(g_mbkB, bw); bh = snap(bh + headH); g_mbkB.inTop = headH;
    const float sx = f.screenW / (float)last.uiW, sy = f.screenH / (float)last.uiH;
    float bx = (last.rx0 * 2 > last.uiW) ? snap((float)last.rx1 * sx - bw) : snap((float)last.rx0 * sx), by = snap((float)last.ry0 * sy);
    if (bx + bw > f.screenW - 4.0f) bx = snap(f.screenW - 4.0f - bw); if (bx < 4.0f) bx = 4.0f;
    if (by + bh > f.screenH - 4.0f) by = snap(f.screenH - 4.0f - bh); if (by < 4.0f) by = 4.0f;
    const int jm = menu_place(f, __LINE__, bx, by, bw, bh); g_frameJoin = jm;
    const float tO = ease_out(((float)(now - g_mbkB.openMs) / 1000.0f) / 0.08f);
    by = snap(by + (1.0f - tO) * 4.0f * S);
    const float A = g_mbkB.vis * tO * (last.focused || !B0.open ? 1.0f : 0.62f);
    if (A <= 0.0f) return;
    const int row = block_draw(f, kN, g_mbkB, rows, 20, cur, last.focused, bx, by, bw, bh, A, dt);
    g_mbkAt[0] = bx; g_mbkAt[1] = g_mbkB.rowY0 + (float)((last.cursor >= 1 && last.cursor <= 20 ? last.cursor : 1) - 1) * g_mbkB.rowStep; g_mbkAt[2] = B0.open ? 1.0f : 0.0f;
    // the header : the title, the page between its two arrows (the client's entries 21 / 22)
    const u32 dev = f.dev;
    const float hy = by + k.bi + k.pad, hc = snap(hy + (headH - k.gap) * 0.5f);
    rrect(dev, bx + k.bi + k.gutter, snap(hy + headH - k.gap * 0.5f - 1.0f), bw - 2.0f * k.bi - k.gutter - k.pad, 1.0f, 0.0f, with_alpha(0x30C8D6EAu, A), with_alpha(0x30C8D6EAu, A));
    const float pw = f.font->measure(pg, tzH), ar = 3.0f * S;
    const float rx = snap(bx + bw - k.bi - k.pad - 4.0f * S), lx = snap(rx - 2.0f * ar - 6.0f * S - pw - 6.0f * S);
    const u32 ac = with_alpha(0xC0FFDA8Au, A);
    tri(dev, rx, hc, rx - 2.0f * ar, hc - ar * 1.2f, rx - 2.0f * ar, hc + ar * 1.2f, ac);
    tri(dev, lx, hc, lx + 2.0f * ar, hc - ar * 1.2f, lx + 2.0f * ar, hc + ar * 1.2f, ac);
    Font* fo = f.font;
    fo->begin(dev);
    fo->draw_lc(dev, snap(bx + k.bi + k.gutter + k.capPadL * 0.5f), hc, title, tzH, with_alpha(0xFFE8D9B0u, A), with_alpha(0xC8000000u, A), k.ow);
    fo->draw_lc(dev, snap(lx + 2.0f * ar + 6.0f * S), hc, pg, tzH, with_alpha(0xFFC9D3E0u, A), with_alpha(0xC8000000u, A), k.ow);
    if (B0.open && last.focused && !last.renaming) {
        g_hit[20][0] = bx / f.screenW; g_hit[20][1] = by / f.screenH; g_hit[20][2] = bw / f.screenW; g_hit[20][3] = bh / f.screenH;
        if (row >= 1 && row <= 20 && row != g_mbkB.hoverRow && row != last.cursor) native_macro_books_hover(row);
        g_mbkB.hoverRow = row;
        if (f.mouse && f.mouse->clicked) {
            if (row >= 1 && row <= 20) native_macro_books_pick(row);
            else {                                                 // the header's arrows : 6 px around each
                const float mx = f.mouse->x, my = f.mouse->y;
                if (fabsf(my - hc) < 8.0f * S) {
                    if (fabsf(mx - (lx + ar)) < 8.0f * S) native_macro_books_pick(21);
                    else if (fabsf(mx - (rx - ar)) < 8.0f * S) native_macro_books_pick(22);
                }
            }
        }
    } else g_mbkB.hoverRow = 0;
}
// ======== MacroPalette -> Edit Equip. Set : the SET LIST (mcres20) -- 20 sets a page (10 pages), each with its count of
// pieces ; a row renamed shows what is typed. The client keeps the keys ; the mouse hovers / picks a row. ========
namespace { Block g_esB; unsigned g_esMs = 0; }
void equip_sets_draw(const Frame& f) {
    const UiConfig& C = ui_config();
    g_esAt[2] = 0.0f;
    if (!(C.nuHide & NU_MAIN) || !f.font || f.screenH <= 0.0f || f.screenW <= 0.0f) { g_esB.vis = 0.0f; g_esB.wasOpen = false; return; }
    const unsigned now = GetTickCount();
    float dt = g_esMs ? (float)(now - g_esMs) / 1000.0f : 0.0f; g_esMs = now; if (dt > 0.1f) dt = 0.1f;
    const NativeEquipSets& E0 = native_equip_sets();
    static NativeEquipSets last;
    if (E0.open) last = E0;
    const bool editing = native_ui_window_open("mcresed");               // the set's grid opens in its place
    if (!block_ease(g_esB, E0.open && !editing, now, dt) || last.uiW <= 0 || last.uiH <= 0) return;
    const Metrics k = metrics(f), kN = no_icons(k);
    const float S = k.S;
    RowSpec rows[20]; static char lab[20][24], num[20][12]; int cur = 0;
    for (int i = 0; i < 20; ++i) {
        int pieces = 0; for (int s = 0; s < 16; ++s) if (last.item[i][s]) ++pieces;
        if (last.renaming == i + 1) { _snprintf(lab[i], sizeof(lab[i]), "%s%s", last.rename, (now / 500) % 2 ? "_" : " "); lab[i][23] = 0; rows[i].tint = 0xFFFFFFFFu; }
        else lstrcpynA(lab[i], last.name[i][0] ? last.name[i] : "-", sizeof(lab[i]));
        if (pieces) sprintf(num[i], "%d", pieces); else num[i][0] = 0;
        rows[i].label = lab[i]; rows[i].icon = IC_NONE; rows[i].sub = false; rows[i].off = !pieces && last.renaming != i + 1; rows[i].extra = num[i][0] ? num[i] : 0;
        if (last.cursor == i + 1) cur = i + 1;
    }
    float bw = 0.0f, bh = 0.0f; block_size(f, kN, rows, 20, bw, bh);
    const float tzH = k.tz * 0.92f, headH = snap(k.capH + k.gap);
    const char* title = tr("Equipment Sets", "Sets d'equipement");
    char pg[16]; sprintf(pg, "%d / 10", last.page + 1);
    {   const float need = snap(k.gutter + f.font->measure(title, tzH) + 14.0f * S + f.font->measure("10 / 10", tzH) + 18.0f * S + k.pad) + 2.0f * k.bi;
        if (need > bw) bw = need; }
    bw = keep_width(g_esB, bw); bh = snap(bh + headH); g_esB.inTop = headH;
    const float sx = f.screenW / (float)last.uiW, sy = f.screenH / (float)last.uiH;
    float bx = (last.rx0 * 2 > last.uiW) ? snap((float)last.rx1 * sx - bw) : snap((float)last.rx0 * sx), by = snap((float)last.ry0 * sy);
    if (bx + bw > f.screenW - 4.0f) bx = snap(f.screenW - 4.0f - bw); if (bx < 4.0f) bx = 4.0f;
    if (by + bh > f.screenH - 4.0f) by = snap(f.screenH - 4.0f - bh); if (by < 4.0f) by = 4.0f;
    const int jm = menu_place(f, __LINE__, bx, by, bw, bh); g_frameJoin = jm;
    const float tO = ease_out(((float)(now - g_esB.openMs) / 1000.0f) / 0.08f);
    by = snap(by + (1.0f - tO) * 4.0f * S);
    const float A = g_esB.vis * tO * (last.focused || !E0.open ? 1.0f : 0.62f);
    if (A <= 0.0f) return;
    const int row = block_draw(f, kN, g_esB, rows, 20, cur, last.focused, bx, by, bw, bh, A, dt);
    g_esAt[0] = bx; g_esAt[1] = g_esB.rowY0 + (float)((last.cursor >= 1 && last.cursor <= 20 ? last.cursor : 1) - 1) * g_esB.rowStep; g_esAt[2] = E0.open ? 1.0f : 0.0f;
    const u32 dev = f.dev;
    const float hy = by + k.bi + k.pad, hc = snap(hy + (headH - k.gap) * 0.5f);
    rrect(dev, bx + k.bi + k.gutter, snap(hy + headH - k.gap * 0.5f - 1.0f), bw - 2.0f * k.bi - k.gutter - k.pad, 1.0f, 0.0f, with_alpha(0x30C8D6EAu, A), with_alpha(0x30C8D6EAu, A));
    const float pw = f.font->measure(pg, tzH), ar = 3.0f * S;
    const float rx = snap(bx + bw - k.bi - k.pad - 4.0f * S), lx = snap(rx - 2.0f * ar - 6.0f * S - pw - 6.0f * S);
    const u32 ac = with_alpha(0xC0FFDA8Au, A);
    tri(dev, rx, hc, rx - 2.0f * ar, hc - ar * 1.2f, rx - 2.0f * ar, hc + ar * 1.2f, ac);
    tri(dev, lx, hc, lx + 2.0f * ar, hc - ar * 1.2f, lx + 2.0f * ar, hc + ar * 1.2f, ac);
    Font* fo = f.font;
    fo->begin(dev);
    fo->draw_lc(dev, snap(bx + k.bi + k.gutter + k.capPadL * 0.5f), hc, title, tzH, with_alpha(0xFFE8D9B0u, A), with_alpha(0xC8000000u, A), k.ow);
    fo->draw_lc(dev, snap(lx + 2.0f * ar + 6.0f * S), hc, pg, tzH, with_alpha(0xFFC9D3E0u, A), with_alpha(0xC8000000u, A), k.ow);
    if (E0.open && last.focused && !last.renaming) {
        if (row >= 1 && row <= 20 && row != g_esB.hoverRow && row != last.cursor) native_equip_sets_hover(row);
        g_esB.hoverRow = row;
        if (f.mouse && f.mouse->clicked && row >= 1 && row <= 20) native_equip_sets_pick(row);
    } else g_esB.hoverRow = 0;
}
// ======== MacroPalette -> a book -> Edit : the PALETTE (mcr1edlo / mcr2edlo : Ctrl and Alt stacked) and ONE MACRO
// (`macro` : its name, its 6 lines). The client keeps the keys (and the typing) ; the mouse hovers / picks a cell, a
// field, the previous / next arrows. ========
namespace { Block g_mpB, g_meB; unsigned g_medMs = 0; }
void macro_edit_draw(const Frame& f) {
    const UiConfig& C = ui_config();
    if (!(C.nuHide & NU_MAIN) || !f.font || f.screenH <= 0.0f || f.screenW <= 0.0f) { g_mpB.vis = g_meB.vis = 0.0f; g_mpB.wasOpen = g_meB.wasOpen = false; return; }
    const unsigned now = GetTickCount();
    float dt = g_medMs ? (float)(now - g_medMs) / 1000.0f : 0.0f; g_medMs = now; if (dt > 0.1f) dt = 0.1f;
    const NativeMacroEdit& E0 = native_macro_edit();
    g_medAt[0][2] = g_medAt[1][2] = 0.0f;
    static NativeMacroEdit last;
    if (E0.pal || E0.ed) last = E0;
    if (last.uiW <= 0 || last.uiH <= 0) return;
    const Metrics k = metrics(f), kN = no_icons(k);
    const float S = k.S, tz = k.tz, pad = snap(8.0f * S) + k.bi;
    Font* fo = f.font; const u32 dev = f.dev; const u32 sh0 = 0xC8000000u;
    const float sx = f.screenW / (float)last.uiW, sy = f.screenH / (float)last.uiH;
    const MouseState* ms = f.mouse;
    // ---- the palette : a header, then Ctrl 1..0 and Alt 1..0 ----
    if (block_ease(g_mpB, E0.pal, now, dt)) {
        const float tO = ease_out(((float)(now - g_mpB.openMs) / 1000.0f) / 0.08f);
        const float A = g_mpB.vis * tO * (last.palFocused || !E0.pal ? 1.0f : 0.62f);
        if (A > 0.0f) {
            const u32 sh = with_alpha(sh0, A);
            float bw = snap((float)(last.px1 - last.px0) * sx); if (bw < 520.0f * S) bw = snap(520.0f * S);
            const float cellH = snap(k.capH * 1.55f), headH = snap(k.capH + k.gap), lab = snap(34.0f * S), rg = snap(4.0f * S);
            const float bh = snap(pad + headH + cellH + rg + cellH + pad);
            float bx = snap((float)last.px0 * sx), by = snap((float)last.py0 * sy);
            if (bx + bw > f.screenW - 4.0f) bx = snap(f.screenW - 4.0f - bw); if (bx < 4.0f) bx = 4.0f;
            if (by + bh > f.screenH - 4.0f) by = snap(f.screenH - 4.0f - bh); if (by < 4.0f) by = 4.0f;
            by = snap(by + (1.0f - tO) * 3.0f * S);
            draw_themed_box(dev, f.skin, bx, by, bw, bh, C.muBox, A, S);
            g_medAt[0][0] = bx + bw; g_medAt[0][1] = by; g_medAt[0][2] = E0.pal ? 1.0f : 0.0f;
            char head[64]; _snprintf(head, sizeof(head), "Macro Set %s (#%d)", last.bookTitle, last.set + 1); head[63] = 0;
            fo->begin(dev);
            fo->draw_lc(dev, snap(bx + pad), snap(by + pad + headH * 0.45f), head, tz * 0.92f, with_alpha(0xFFE8D9B0u, A), sh, k.ow);
            const float L = bx + pad + lab, W = bw - 2.0f * pad - lab, cg = snap(4.0f * S), cw = (W - 9.0f * cg) / 10.0f;
            int hovBar = -1, hovCell = 0;
            for (int bar = 0; bar < 2; ++bar) {
                const float cy0 = snap(by + pad + headH + (float)bar * (cellH + rg));
                const bool active = last.row == bar + 1;
                fo->begin(dev);
                fo->draw_lc(dev, snap(bx + pad), snap(cy0 + cellH * 0.5f), bar ? "Alt" : "Ctrl", tz * 0.85f, with_alpha(active ? 0xFFFFDA8Au : 0xFF93A6BEu, A), sh, k.ow);
                for (int i = 0; i < 10; ++i) {
                    const float cx = snap(L + (float)i * (cw + cg)), cwi = snap(L + (float)(i + 1) * (cw + cg) - cg) - cx;
                    const char* nm = last.name[bar * 10 + i];
                    const bool hand = last.hand[bar] == i + 1, empty = !nm[0], lit = hand && active;
                    const float r = snap(4.0f * S);
                    rrect(dev, cx, cy0, cwi, cellH, r, with_alpha(empty ? 0x14FFFFFFu : 0x22FFFFFFu, A), with_alpha(empty ? 0x0CFFFFFFu : 0x16FFFFFFu, A));
                    if (lit) { rrect_clip_begin(dev, cx, cy0, cwi, cellH, r); party_selframe(dev, cx, cy0, cwi, cellH, f.t, A, 0); rrect_clip_end(dev); }
                    if (hand) rrect_stroke(dev, cx, cy0, cwi, cellH, r, with_alpha(lit ? 0xC8FFDA8Au : 0x50FFDA8Au, A), 1.0f);
                    char key[4]; _snprintf(key, sizeof(key), "%d", (i + 1) % 10); key[3] = 0;
                    fo->begin(dev);
                    fo->draw_lc(dev, snap(cx + 4.0f * S), snap(cy0 + 7.0f * S), key, tz * 0.72f, with_alpha(0xFF93A6BEu, A), sh, k.ow);
                    if (!empty) {
                        char cut[32]; const char* s = fit_text(fo, nm, tz, cwi - 8.0f * S, cut, sizeof(cut));
                        fo->draw_lc(dev, snap(cx + (cwi - fo->measure(s, tz)) * 0.5f), snap(cy0 + cellH * 0.58f), s, tz, with_alpha(lit ? 0xFFFFFFFFu : 0xFFE4EAF2u, A), sh, k.ow);
                    }
                    if (ms && ms->x >= cx && ms->x < cx + cwi && ms->y >= cy0 && ms->y < cy0 + cellH) { hovBar = bar; hovCell = i + 1; }
                }
            }
            if (E0.pal && last.palFocused) {
                static int lastHov = 0;
                const int key = hovBar >= 0 ? hovBar * 16 + hovCell : 0;
                if (key && key != lastHov && !(hovBar == last.row - 1 && hovCell == last.hand[hovBar])) native_macro_edit_hover(hovBar, hovCell);
                lastHov = key;
                if (key && ms && ms->clicked) native_macro_edit_pick(hovBar, hovCell);
            }
        }
    }
    // ---- one macro : "< Ctrl 1 >", its name, its 6 lines ----
    if (block_ease(g_meB, E0.ed, now, dt)) {
        RowSpec rows[7]; static char lab[7][136]; static const char* const NUM[7] = { "Name", "1", "2", "3", "4", "5", "6" };
        int cur = 0;
        for (int i = 0; i < 7; ++i) {
            const char* src = i == 0 ? last.ename : last.line[i - 1];
            const bool typing = last.typing == i + 3;
            _snprintf(lab[i], sizeof(lab[i]), "%s%s", src[0] || typing ? src : "", typing ? ((now / 500) % 2 ? "_" : " ") : ""); lab[i][135] = 0;
            rows[i].label = lab[i]; rows[i].icon = IC_NONE; rows[i].sub = false; rows[i].off = false; rows[i].extra = NUM[i];
            if (typing) rows[i].tint = 0xFFFFFFFFu; else if (i == 0) rows[i].tint = 0xFFFFDA8Au;
            if (last.cursor == i + 3) cur = i + 1;
        }
        float bw = 0.0f, bh = 0.0f; block_size(f, kN, rows, 7, bw, bh);
        const float minW = snap(420.0f * S); if (bw < minW) bw = minW;
        const float headH = snap(k.capH + k.gap), tzH = tz * 0.92f;
        bw = keep_width(g_meB, bw); bh = snap(bh + headH); g_meB.inTop = headH;
        float bx = snap((float)last.ex0 * sx), by = snap((float)last.ey0 * sy);
        if (bx + bw > f.screenW - 4.0f) bx = snap(f.screenW - 4.0f - bw); if (bx < 4.0f) bx = 4.0f;
        if (by + bh > f.screenH - 4.0f) by = snap(f.screenH - 4.0f - bh); if (by < 4.0f) by = 4.0f;
        const int jm = menu_place(f, __LINE__, bx, by, bw, bh); g_frameJoin = jm;
        const float tO = ease_out(((float)(now - g_meB.openMs) / 1000.0f) / 0.08f);
        by = snap(by + (1.0f - tO) * 4.0f * S);
        const float A = g_meB.vis * tO * (last.edFocused || !E0.ed ? 1.0f : 0.62f);
        if (A > 0.0f) {
            const int row = block_draw(f, kN, g_meB, rows, 7, cur, last.edFocused, bx, by, bw, bh, A, dt);
            g_medAt[1][0] = bx + bw; g_medAt[1][1] = by; g_medAt[1][2] = E0.ed ? 1.0f : 0.0f;
            const float hy = by + k.bi + k.pad, hc = snap(hy + (headH - k.gap) * 0.5f);
            rrect(dev, bx + k.bi + k.gutter, snap(hy + headH - k.gap * 0.5f - 1.0f), bw - 2.0f * k.bi - k.gutter - k.pad, 1.0f, 0.0f, with_alpha(0x30C8D6EAu, A), with_alpha(0x30C8D6EAu, A));
            char ttl[16]; _snprintf(ttl, sizeof(ttl), "%s %d", last.k >= 10 ? "Alt" : "Ctrl", (last.k % 10 + 1) % 10); ttl[15] = 0;
            const float tw = fo->measure(ttl, tzH), ar = 3.5f * S, cx = snap(bx + bw * 0.5f);
            const float lx = snap(cx - tw * 0.5f - 10.0f * S), rx = snap(cx + tw * 0.5f + 10.0f * S);
            const u32 acL = with_alpha(last.cursor == 1 ? 0xFFFFFFFFu : 0xC0FFDA8Au, A), acR = with_alpha(last.cursor == 2 ? 0xFFFFFFFFu : 0xC0FFDA8Au, A);
            tri(dev, lx - 2.0f * ar, hc, lx, hc - ar * 1.2f, lx, hc + ar * 1.2f, acL);
            tri(dev, rx + 2.0f * ar, hc, rx, hc - ar * 1.2f, rx, hc + ar * 1.2f, acR);
            fo->begin(dev);
            fo->draw_lc(dev, snap(cx - tw * 0.5f), hc, ttl, tzH, with_alpha(0xFFE8D9B0u, A), with_alpha(sh0, A), k.ow);
            if (E0.ed && last.edFocused && !last.typing) {
                if (row >= 1 && row <= 7 && row != g_meB.hoverRow && row + 2 != last.cursor) native_macro_edit_hover(2, row + 2);
                g_meB.hoverRow = row;
                if (ms && ms->clicked) {
                    if (row >= 1 && row <= 7) native_macro_edit_pick(2, row + 2);
                    else if (fabsf(ms->y - hc) < 9.0f * S) {
                        if (fabsf(ms->x - (lx - ar)) < 9.0f * S) native_macro_edit_pick(2, 1);
                        else if (fabsf(ms->x - (rx + ar)) < 9.0f * S) native_macro_edit_pick(2, 2);
                    }
                }
            } else g_meB.hoverRow = 0;
        }
    }
}
// the generic engine's GRID (Config's screens) : the client's layout -- our box at its rect, the frame's static texts
// (its sprite's letters, where the client puts them), each entry a capsule at its rect with its own letters ; the cursor
// gold, a grey entry dimmed. The client keeps the keys ; the mouse hovers / picks an entry.
namespace { Block g_ggB; unsigned g_ggMs = 0; }
void gen_grid_draw(const Frame& f) {
    const UiConfig& C = ui_config();
    if (!(C.nuHide & NU_NPC) || !f.font || f.screenH <= 0.0f || f.screenW <= 0.0f) { g_ggB.vis = 0.0f; g_ggB.wasOpen = false; return; }
    const unsigned now = GetTickCount();
    float dt = g_ggMs ? (float)(now - g_ggMs) / 1000.0f : 0.0f; g_ggMs = now; if (dt > 0.1f) dt = 0.1f;
    const NativeGenMenus& G0 = native_gen_menus();
    static NativeGenMenus last; static char title[0xCC];
    if (G0.grid) { last = G0; const NativeTopBar& tb = native_topbar(); if (tb.title.text[0]) lstrcpynA(title, tb.title.text, sizeof(title)); }
    if (!block_ease(g_ggB, G0.grid, now, dt) || last.gm.uiW <= 0) return;
    const NativeEntryMenu& M = last.gm;
    const Metrics k = metrics(f); const float S = k.S; const u32 dev = f.dev; Font* fo = f.font;
    const float sx = f.screenW / (float)M.uiW, sy = f.screenH / (float)M.uiH;
    const float tO = ease_out(((float)(now - g_ggB.openMs) / 1000.0f) / 0.08f), A = g_ggB.vis * tO;
    if (A <= 0.0f) return;
    float bx = snap((float)M.rx0 * sx), by = snap((float)M.ry0 * sy), bw = snap((float)(M.rx1 - M.rx0) * sx), bh = snap((float)(M.ry1 - M.ry0) * sy);
    if (bw < 40.0f || bh < 20.0f) return;
    const int jm = menu_place(f, __LINE__, bx, by, bw, bh); g_frameJoin = jm;
    by = snap(by + (1.0f - tO) * 4.0f * S);
    draw_themed_box(dev, f.skin, bx, by, bw, bh, C.muBox, A, S);
    // the frame's static texts : its sprite's letters at their place (sprite origin = the window's)
    unsigned fspr = 0;
    if (menu_window_frame(M.name, fspr)) { RowSpec fr; if (label_img(dev, 1, (unsigned short)(fspr + 1), fr)) {
        const float x = snap(bx + (float)g_lblOx * sx), y = snap(by + (float)g_lblOy * sy), w = snap(fr.lw * sx), h = snap(fr.lh * sy);
        dTexQuadState(dev, fr.ltex);
        tquad(dev, x + 1.0f * S, y + 1.0f * S, w, h, 0.0f, fr.lu1, 0.0f, fr.lv1, with_alpha(0xC8000000u, A), with_alpha(0xC8000000u, A));
        tquad(dev, x, y, w, h, 0.0f, fr.lu1, 0.0f, fr.lv1, with_alpha(0xFFE8D9B0u, A), with_alpha(0xFFE8D9B0u, A));
        dSetTex(dev, 0, 0); cs(dev); } }
    // the entries
    const MouseState* ms = f.mouse; int ment = 0;
    for (int i = 0; i < M.n && i < 32; ++i) {
        const float x = snap(bx + (float)last.gx[i] * sx), y = snap(by + (float)last.gy[i] * sy), w = snap((float)last.gw[i] * sx), h = snap((float)last.gh[i] * sy);
        if (w < 6.0f || h < 4.0f) continue;
        const bool on = M.entry[i] == M.cursor && last.gridFocused, off = M.state[i] == 5;
        const bool slider = last.gfrac[i] >= 0.0f, chosen = last.gchosen[i] == 1, other = last.gchosen[i] == 0;
        const float a = A * (off ? 0.5f : 1.0f), r = h * 0.5f;
        if (slider) {                                                    // a slider : the track, its fill (value / range), the value
            rrect(dev, x, y, w, h, r, with_alpha(0x70101826u, a), with_alpha(0x70080C14u, a));
            const float fw = snap((w - 4.0f * S) * last.gfrac[i]);
            if (fw > 1.0f) rrect(dev, x + 2.0f * S, y + 2.0f * S, fw, h - 4.0f * S, (h - 4.0f * S) * 0.5f, with_alpha(on ? 0xE0FFDA8Au : 0xB0C8A860u, a), with_alpha(on ? 0xC0E8B860u : 0x90A88848u, a));
            rrect_stroke(dev, x, y, w, h, r, with_alpha(on ? 0xE0FFDA8Au : 0x38C8D6EAu, a), on ? 1.5f * S : 1.0f);
            char t[16]; if (last.gvalText[i][0]) lstrcpynA(t, last.gvalText[i], sizeof(t)); else sprintf(t, "%d", last.gval[i]);
            fo->begin(dev); fo->draw_lc(dev, snap(x + w + 6.0f * S), snap(y + h * 0.5f), t, k.tz * 0.85f, with_alpha(on ? 0xFFFFFFFFu : 0xFFC9D3E0u, a), with_alpha(0xC8000000u, a), k.ow);
            if (ms && ms->focused && ms->x >= x && ms->x < x + w && ms->y >= y && ms->y < y + h) ment = M.entry[i];
            continue;
        }
        rrect(dev, x, y, w, h, r, with_alpha(on ? 0x50FFDA8Au : chosen ? 0x40C8A860u : 0x5A1C2638u, a), with_alpha(on ? 0x38FFDA8Au : chosen ? 0x30A88848u : 0x5A0A0F18u, a));
        rrect_stroke(dev, x, y, w, h, r, with_alpha(on ? 0xE0FFDA8Au : chosen ? 0xB0FFDA8Au : 0x38C8D6EAu, a), on || chosen ? 1.5f * S : 1.0f);
        RowSpec rs; rs.ltex = 0;
        if (M.sheet[i] && label_img(dev, M.sheet[i], M.spr[i][0], rs)) {
            const float ih = h * 0.72f, iw = rs.lh > 0.0f ? rs.lw * ih / rs.lh : 0.0f, ix = snap(x + (w - iw) * 0.5f), iy = snap(y + (h - ih) * 0.5f);
            const u32 lc = on ? 0xFFFFFFFFu : chosen ? 0xFFFFE9B0u : other ? 0xFF8A96A8u : 0xFFC9D3E0u;   // the option in force bright, the others dim
            dTexQuadState(dev, rs.ltex);
            tquad(dev, ix + 1.0f * S, iy + 1.0f * S, snap(iw), snap(ih), 0.0f, rs.lu1, 0.0f, rs.lv1, with_alpha(0xC8000000u, a), with_alpha(0xC8000000u, a));
            tquad(dev, ix, iy, snap(iw), snap(ih), 0.0f, rs.lu1, 0.0f, rs.lv1, with_alpha(lc, a), with_alpha(lc, a));
            dSetTex(dev, 0, 0); cs(dev);
        }
        if (ms && ms->focused && ms->x >= x && ms->x < x + w && ms->y >= y && ms->y < y + h) ment = M.entry[i];
    }
    fo->begin(dev);
    if (title[0]) fo->draw_lc(dev, snap(bx + 10.0f * S), snap(by - 10.0f * S), title, k.tz * 0.95f, with_alpha(0xFFE8D9B0u, A), with_alpha(0xC8000000u, A), k.ow);
    if (G0.grid && last.gridFocused) {
        g_hit[16][0] = bx / f.screenW; g_hit[16][1] = by / f.screenH; g_hit[16][2] = bw / f.screenW; g_hit[16][3] = bh / f.screenH;
        static float mv[2]; const bool moved = mouse_moved(f.mouse, mv);
        if (ment && moved && ment != g_ggB.hoverRow && ment != M.cursor) native_gen_hover(M.name, ment);
        g_ggB.hoverRow = ment;
        if (ment && ms && ms->clicked) native_gen_pick(M.name, ment);
    } else g_ggB.hoverRow = 0;
}
// the generic engine's PANELS (Missions, Quests, Wide Scan... : the text capture) -- AioHUD's own layout, the client's
// CONTENT. The texts it writes are read as lines (top to bottom) and laid out anew :
//   the TITLE (the top line outside every entry)      -> the header band
//   a lone text outside the entries ("Completed:")    -> a SECTION : small gold caps, a hairline, air above
//   a text inside an entry                            -> a ROW (cursor, mouse, greyed as the client greys it) ; its value
//                                                        on the right, a count "12/30" as a progress bar
//   a label + value outside the entries ("Sparks")    -> a STAT tile (two a line, the number large) under the rows
namespace { Block g_gpB[2]; unsigned g_gpMs = 0; char g_gpName[2][9]; float g_gpCurY[2] = { -1.0f, -1.0f }; }
static bool cap_valueish(const char* t, unsigned col) {
    if ((col & 0x01000000u) || (col & 0x00FFFFFFu) == 0x407090u) return true;
    const char* p = t; if (*p == '(' || *p == '[' || *p == '+' || *p == '-') ++p;
    return *p >= '0' && *p <= '9';
}
struct PanelItem { char label[60]; char value[48]; int entry; bool grey, section, stat, pink; };   // pink : the client drew it in its cursor colour
static int panel_items(const NativeGenMenus::Panel& P, char* title, int tcap, PanelItem* it, int cap) {
    title[0] = 0; int ord[40]; const int n = P.nr < 40 ? P.nr : 40;
    for (int i = 0; i < n; ++i) ord[i] = i;
    for (int i = 1; i < n; ++i) for (int j = i; j > 0; --j) {         // by line (6 px), then x
        const NativeGenMenus::Panel::Run &A = P.run[ord[j - 1]], &B = P.run[ord[j]];
        const bool swap = (B.y < A.y - 6) || (abs(B.y - A.y) <= 6 && B.x < A.x);
        if (!swap) break; const int t = ord[j]; ord[j] = ord[j - 1]; ord[j - 1] = t;
    }
    int m = 0, lineY = -9999, lineFirst = 0;
    for (int q = 0; q < n; ++q) {
        const NativeGenMenus::Panel::Run& U = P.run[ord[q]];
        const bool newLine = abs(U.y - lineY) > 6; if (newLine) { lineY = U.y; lineFirst = m; }
        const int e = U.ent;
        if (P.titleRun >= 0 ? ord[q] == P.titleRun : (P.titleRun == -2 && q == 0 && !e && U.y < 30 && (n < 2 || abs(P.run[ord[1]].y - U.y) > 6))) { lstrcpynA(title, U.s, tcap); continue; }
        if (newLine && m > 0 && e && it[m - 1].entry == e && !it[m - 1].value[0] && cap_valueish(U.s, U.col)) { lstrcpynA(it[m - 1].value, U.s, sizeof(it[m - 1].value)); continue; }   // its value, under it
        if (!newLine && m > lineFirst) {
            PanelItem& L = it[m - 1];
            if (L.entry && (!e || e == L.entry)) {                      // beside a row : that row's value ("Plaudits 200")
                const int lv = lstrlenA(L.value); _snprintf(L.value + lv, sizeof(L.value) - lv, "%s%s", lv ? " " : "", U.s); L.value[sizeof(L.value) - 1] = 0; continue; }
            if (!L.value[0] && cap_valueish(U.s, U.col)) { lstrcpynA(L.value, U.s, sizeof(L.value)); L.section = false; L.stat = !L.entry; continue; }
        }
        if (m >= cap) break;
        PanelItem& I = it[m++]; memset(&I, 0, sizeof(I));
        lstrcpynA(I.label, U.s, sizeof(I.label));
        { char* sp = strstr(I.label, "   ");                            // "Current          12/30" : a label, a value
          if (sp) { const char* v = sp; while (*v == ' ') ++v; lstrcpynA(I.value, v, sizeof(I.value)); *sp = 0; } }
        I.entry = e; I.grey = (U.col & 0x00FFFFFFu) == 0x404040u; I.pink = (U.col & 0x00FFFFFFu) == 0x803040u;
        I.section = !e && !I.value[0]; I.stat = !e && I.value[0];
        if (I.section) { int l = lstrlenA(I.label); while (l && (I.label[l - 1] == ':' || I.label[l - 1] == ' ')) I.label[--l] = 0; }
    }
    return m;
}
static bool parse_frac(const char* v, int& a, int& b) { a = b = 0; return v && sscanf(v, "%d/%d", &a, &b) == 2 && b > 0 && a >= 0 && a <= b * 4; }
// the panel's KICKER (the family, small caps above the title) and its title when the client draws none at the top
static void panel_names(const char* nm, const char* title, const char*& kicker, const char*& fallback) {
    kicker = 0; fallback = 0;
    if (!strncmp(nm, "miss", 4)) kicker = tr("Missions", "Missions");
    else if (!strcmp(nm, "quest00")) { kicker = tr("Journal", "Journal"); fallback = tr("Quests", "Quetes"); }
    else if (!strncmp(nm, "quest", 5)) kicker = tr("Records of Eminence", "Records of Eminence");
    else if (!strcmp(nm, "scanlist")) { kicker = tr("Wide Scan", "Wide Scan"); fallback = tr("Nearby", "A proximite"); }
    if (kicker && title && title[0] && !lstrcmpiA(kicker, title)) kicker = 0;
}
// the panel's ACCENT : the nation / the expansion / the family it shows (the title's words, then the window's) -- it tints
// the header band, the kicker, the sections, the bullets and the bars
static u32 panel_accent(const char* nm, const char* title) {
    char b[64]; lstrcpynA(b, title ? title : "", sizeof(b)); CharLowerA(b);
    struct { const char* w; u32 c; } T[] = {
        { "san d'oria", 0xFFE8706Au }, { "bastok", 0xFF6AA8F0u }, { "windurst", 0xFF7CCF7Au }, { "jeuno", 0xFFE8C060u },
        { "zilart", 0xFFE8C060u }, { "promathia", 0xFFA88CF0u }, { "aht urhgan", 0xFFF09A50u }, { "goddess", 0xFF68D0D8u },
        { "adoulin", 0xFF84DCA8u }, { "eminence", 0xFF62BEF8u }, { "objective", 0xFF62BEF8u }, { "unity", 0xFFB48CFFu } };
    for (size_t i = 0; i < sizeof(T) / sizeof(T[0]); ++i) if (strstr(b, T[i].w)) return T[i].c;
    if (!strcmp(nm, "quest00")) return 0xFFE0B060u;
    if (!strncmp(nm, "quest", 5)) return 0xFF62BEF8u;
    if (!strcmp(nm, "scanlist")) return 0xFFF08A6Au;
    return 0xFFE0B060u;
}
// a stat tile's own colour, by its words
static u32 stat_colour(const char* l, u32 accent) {
    char b[60]; lstrcpynA(b, l, sizeof(b)); CharLowerA(b);
    if (strstr(b, "spark")) return 0xFFFFC857u;
    if (strstr(b, "accolade") || strstr(b, "unity")) return 0xFFC09CFFu;
    if (strstr(b, "deed")) return 0xFF6EC8FFu;
    if (strstr(b, "complet")) return 0xFF8EDB7Au;
    if (strstr(b, "plaudit") || strstr(b, "point")) return 0xFFFFA060u;
    return accent;
}
static u32 with_a(u32 c, u32 a) { return (c & 0x00FFFFFFu) | (a << 24); }
// a section's kind, from its words (the client's EN / FR) : 1 done (a green check), 2 in progress (a gold diamond)
static int section_kind(const char* l) {
    char b[60]; lstrcpynA(b, l, sizeof(b)); CharLowerA(b);
    if (strstr(b, "complet") || strstr(b, "termin") || strstr(b, "accompli")) return 1;
    if (strstr(b, "current") || strstr(b, "en cours") || strstr(b, "actuel")) return 2;
    return 0;
}
void gen_panel_draw(const Frame& f) {
    const UiConfig& C = ui_config();
    if (!(C.nuHide & NU_NPC) || !f.font || f.screenH <= 0.0f || f.screenW <= 0.0f) { for (int i = 0; i < 2; ++i) { g_gpB[i].vis = 0.0f; g_gpB[i].wasOpen = false; } return; }
    const unsigned now = GetTickCount();
    float dt = g_gpMs ? (float)(now - g_gpMs) / 1000.0f : 0.0f; g_gpMs = now; if (dt > 0.1f) dt = 0.1f;
    const NativeGenMenus& G0 = native_gen_menus();
    static NativeGenMenus::Panel last[2];
    for (int i = 0; i < G0.np; ++i) {                                  // a slot per panel NAME
        int s = -1; for (int j = 0; j < 2; ++j) if (!strcmp(g_gpName[j], G0.p[i].name)) s = j;
        if (s < 0) for (int j = 0; j < 2 && s < 0; ++j) { bool used = false; for (int q = 0; q < G0.np; ++q) if (!strcmp(g_gpName[j], G0.p[q].name)) used = true; if (!used && g_gpB[j].vis <= 0.0f) { s = j; lstrcpynA(g_gpName[j], G0.p[i].name, 9); g_gpB[j] = Block(); g_gpCurY[j] = -1.0f; } }
        if (s >= 0) last[s] = G0.p[i];
    }
    bool anyFocused = false; for (int i = 0; i < G0.np; ++i) if (G0.p[i].focused) anyFocused = true;
    const Metrics k = metrics(f); const float S = k.S; const u32 dev = f.dev; Font* fo = f.font; const MouseState* ms = f.mouse;
    // the type scale : rows a touch larger than the menus, the title larger still, the stats' numbers the largest
    const float tz = snap(k.tz * 1.08f), tzS = snap(k.tz * 0.80f), tzK = snap(k.tz * 0.74f), tzT = snap(k.tz * 1.32f), tzV = snap(k.tz * 1.45f);
    const float pad = snap(18.0f * S) + k.bi, rowH = snap(k.capH * 2.0f), secH = snap(tzS * 2.6f), secGap = snap(10.0f * S);
    const float tileH = snap(tzV * 2.9f), gapT = snap(8.0f * S), bulletW = snap(22.0f * S);
    for (int s = 0; s < 2; ++s) {
        if (!g_gpName[s][0]) continue;
        bool open = false; for (int i = 0; i < G0.np; ++i) if (!strcmp(G0.p[i].name, g_gpName[s])) open = true;
        const NativeGenMenus::Panel& P = last[s];
        if (!strcmp(P.name, "miss00") && (native_missions().ok || native_missions().detailOk)) open = false;   // Missions read as data : missions_draw draws it
        if (!strcmp(P.name, "quest01") && native_journal().ok) open = false;
        if (!strcmp(P.name, "evitem01") && native_key_items().ok) open = false;   // Key Items' question : key_items_draw asks it, in its own box   // the journal read as data : journal_draw draws it
        const bool shown = open && (P.focused || !anyFocused);              // the panel it opened on top (Quests -> a list) replaces it
        if (!block_ease(g_gpB[s], shown, now, dt) || P.uiW <= 0) { if (!open && g_gpB[s].vis <= 0.0f) g_gpName[s][0] = 0; continue; }
        static char title[2][60]; static PanelItem items[2][32];
        const int n = panel_items(P, title[s], sizeof(title[s]), items[s], 32);
        if (n <= 0) continue;
        static char tval[2][24]; tval[s][0] = 0;
        { char* sp = strstr(title[s], "   "); if (sp) { const char* v = sp; while (*v == ' ') ++v; lstrcpynA(tval[s], v, sizeof(tval[s])); *sp = 0; } }
        const char *kicker = 0, *fb = 0; panel_names(P.name, title[s], kicker, fb);
        const char* ttl = title[s][0] ? title[s] : fb;
        const u32 acc = panel_accent(P.name, ttl);
        const float headH = ttl ? snap((kicker ? tzK * 1.5f : 0.0f) + tzT * 1.6f + 14.0f * S) : 0.0f;
        // ---- each row's kind : its section's (done / in progress), an empty-state line ("No current missions.")
        int kind[32]; bool empty[32]; { int sk = 0; for (int i = 0; i < n; ++i) { const PanelItem& I = items[s][i];
            if (I.section) sk = section_kind(I.label); kind[i] = sk; empty[i] = !I.value[0] && (!strncmp(I.label, "No ", 3) || !strncmp(I.label, "Aucun", 5)); } }
        float rh[32]; bool two[32]; { const float rowH2 = snap(rowH * 1.55f);
            for (int i = 0; i < n; ++i) { int a2, b2; two[i] = items[s][i].entry && parse_frac(items[s][i].value, a2, b2); rh[i] = two[i] ? rowH2 : rowH; } }
        const bool scroll = P.lcount > 0 && P.lvis > 0 && P.lcount > P.lvis;    // a list longer than its page : a scrollbar, a footer
        const float footH = scroll ? snap(tzS * 2.2f) : 0.0f;
        // ---- measure
        float wRow = 0.0f, wStat = 0.0f, h = 0.0f; int nStat = 0; bool firstBody = true;
        for (int i = 0; i < n; ++i) {
            const PanelItem& I = items[s][i];
            if (I.stat) { ++nStat; const float w = fmaxf(fo->measure(I.label, tzS), fo->measure(I.value, tzV)) + snap(28.0f * S); if (w > wStat) wStat = w; continue; }
            if (I.section) { h += (firstBody ? 0.0f : secGap) + secH; wRow = fmaxf(wRow, fo->measure(I.label, tzS) * 1.2f + snap(40.0f * S)); }
            else { int a, b; const float bar = parse_frac(I.value, a, b) ? snap(96.0f * S) : 0.0f;
                   wRow = fmaxf(wRow, bulletW + fo->measure(I.label, tz) + (I.value[0] && !two[i] ? snap(32.0f * S) + fo->measure(I.value, tz) + bar : 0.0f) + snap(24.0f * S)); h += rh[i]; }
            firstBody = false;
        }
        float bw = fmaxf(wRow + 2.0f * pad, 2.0f * wStat + gapT + 2.0f * pad);
        if (ttl) bw = fmaxf(bw, fo->measure(ttl, tzT) + 2.0f * pad + snap(24.0f * S) + (tval[s][0] ? fo->measure(tval[s], tzS) + snap(120.0f * S) : 0.0f));
        bw = keep_width(g_gpB[s], fmaxf(bw, snap(440.0f * S)));            // a journal page : roomy (the user, 2026-11-21)
        const int statLines = (nStat + 1) / 2;
        const float statsH = statLines ? snap(16.0f * S) + statLines * tileH + (statLines - 1) * gapT : 0.0f;
        const float bh = snap((ttl ? headH : 0.0f) + pad + h + statsH + footH + pad);
        // ---- place : where the client's window is (its left / right side)
        const float sx = f.screenW / (float)P.uiW, sy = f.screenH / (float)P.uiH;
        float bx = snap((float)P.rx0 * sx), by = snap((float)P.ry0 * sy);
        if (P.rx0 * 2 > P.uiW) bx = snap((float)P.rx1 * sx - bw);
        if (covers_chat(bx, by, bw, bh)) { float cx, cy; if (place_on_chat(bw, bh, snap(4.0f * S), f.screenW, f.screenH, cx, cy)) { bx = cx; by = cy; } }   // above the chat, as the client (the Emote List landed ON it, 2026-11-25)
        if (bx + bw > f.screenW - 4.0f) bx = snap(f.screenW - 4.0f - bw); if (bx < 4.0f) bx = 4.0f;
        if (by + bh > f.screenH - 4.0f) by = snap(f.screenH - 4.0f - bh); if (by < 4.0f) by = 4.0f;
        const int jm = menu_place(f, __LINE__ * 4 + s, bx, by, bw, bh); g_frameJoin = jm;
        const float tO = ease_out(((float)(now - g_gpB[s].openMs) / 1000.0f) / 0.09f);
        by = snap(by + (1.0f - tO) * 4.0f * S);
        const float A = g_gpB[s].vis * tO;
        if (A <= 0.0f) continue;
        draw_themed_box(dev, f.skin, bx, by, bw, bh, C.muBox, A, S);
        const float x0 = bx + pad, x1 = bx + bw - pad;
        // ---- the header : the kicker (small caps, gold), the title (large), a hairline ; a band a shade lighter
        float y = by + pad * 0.5f;
        if (ttl) {
            rrect_topcaps(dev, bx + k.bi, by + k.bi, bw - 2.0f * k.bi, headH - k.bi, snap(6.0f * S), with_alpha(with_a(acc, 0x46), A), with_alpha(with_a(acc, 0x0A), A));
            float ky = by + snap(10.0f * S);
            fo->begin(dev);
            if (kicker) { char up[48]; lstrcpynA(up, kicker, sizeof(up)); CharUpperA(up);
                fo->draw_lc(dev, snap(x0), snap(ky + tzK * 0.7f), up, tzK, with_alpha(acc, A), with_alpha(0xA0000000u, A), k.ow); ky += tzK * 1.5f; }
            fo->draw_lc(dev, snap(x0), snap(ky + tzT * 0.8f), ttl, tzT, with_alpha(0xFFF4E8C8u, A), with_alpha(0xC8000000u, A), k.ow);
            if (tval[s][0]) {
                int a2, b2; const bool fr = parse_frac(tval[s], a2, b2);
                const float tw2 = fo->measure(tval[s], tzS), barW = fr ? snap(54.0f * S) : 0.0f, pw = tw2 + barW + (fr ? 34.0f : 20.0f) * S, ph = snap(tzS * 1.8f);
                const float px = snap(x1 - pw), py = snap(by + headH * 0.5f - ph * 0.5f);
                rrect(dev, px, py, pw, ph, ph * 0.5f, with_alpha(with_a(acc, 0x3C), A), with_alpha(with_a(acc, 0x22), A));
                rrect_stroke(dev, px, py, pw, ph, ph * 0.5f, with_alpha(with_a(acc, 0x80), A), 1.0f);
                float tx = px + 10.0f * S;
                if (fr) { const float bh2 = snap(5.0f * S), by2 = snap(py + ph * 0.5f - bh2 * 0.5f);
                    rrect(dev, tx, by2, barW, bh2, bh2 * 0.5f, with_alpha(0x70101826u, A), with_alpha(0x70080C14u, A));
                    const float fw = snap(barW * fminf(1.0f, (float)a2 / (float)b2)); if (fw > 1.0f) rrect(dev, tx, by2, fw, bh2, bh2 * 0.5f, with_alpha(acc, A), with_alpha(with_a(acc, 0xB0), A));
                    tx += barW + 10.0f * S; }
                fo->begin(dev); fo->draw_lc(dev, snap(tx), snap(py + ph * 0.5f), tval[s], tzS, with_alpha(0xFFF4F6FAu, A), with_alpha(0xA0000000u, A), k.ow);
            }
            rrect(dev, bx + k.bi, snap(by + headH - 1.0f), bw - 2.0f * k.bi, 1.5f, 0.0f, with_alpha(with_a(acc, 0x90), A), with_alpha(with_a(acc, 0x90), A));
            y = by + headH + pad * 0.6f;
        } else y = by + pad;
        // ---- the rows' y
        float ry[32]; { float yy = y; bool fb2 = true;
            for (int i = 0; i < n; ++i) { const PanelItem& I = items[s][i]; if (I.stat) { ry[i] = -1.0f; continue; }
                if (I.section) { yy += fb2 ? 0.0f : secGap; ry[i] = yy; yy += secH; } else { ry[i] = yy; yy += rh[i]; } fb2 = false; } }
        // ---- zebra : every other row of a run a faint band (the eye follows the line across the value)
        { int z = 0; for (int i = 0; i < n; ++i) { const PanelItem& I = items[s][i]; if (I.stat) continue; if (I.section) { z = 0; continue; }
            if (z++ & 1) rrect(dev, snap(x0 - 8.0f * S), snap(ry[i]), (x1 - x0) + 16.0f * S, rh[i], snap(4.0f * S), with_alpha(0x0EFFFFFFu, A), with_alpha(0x0EFFFFFFu, A)); } }
        // ---- the cursor : a gold capsule gliding to its row
        // the cursor's row : the one the client draws in its cursor colour (pink) -- the entry ids are a fallback
        int curI = -1; for (int i = 0; i < n; ++i) if (items[s][i].pink && !items[s][i].stat && !items[s][i].section) { curI = i; break; }
        if (curI < 0) for (int i = 0; i < n; ++i) if (items[s][i].entry && items[s][i].entry == P.cursor) { curI = i; break; }
        if (curI >= 0 && P.focused) {
            float& cy = g_gpCurY[s]; const float target = ry[curI];
            cy = cy < 0.0f ? target : cy + (target - cy) * fminf(1.0f, dt * 18.0f);
            const float cx = snap(x0 - 8.0f * S), cw = (x1 - x0) + 16.0f * S;
            rrect(dev, cx, snap(cy + 1.0f * S), cw, rh[curI] - 2.0f * S, snap(6.0f * S), with_alpha(0x50FFDA8Au, A), with_alpha(0x2EFFDA8Au, A));
            rrect_stroke(dev, cx, snap(cy + 1.0f * S), cw, rh[curI] - 2.0f * S, snap(6.0f * S), with_alpha(0xC8FFDA8Au, A), 1.5f * S);
        }
        // ---- the sections and the rows
        int ment = 0, mrow = -1;
        for (int i = 0; i < n; ++i) {
            const PanelItem& I = items[s][i]; if (I.stat) continue;
            if (I.section) {
                int cnt = 0; for (int j = i + 1; j < n && !items[s][j].section; ++j) if (!items[s][j].stat && !empty[j]) ++cnt;
                const float cy = snap(ry[i] + secH * 0.58f);
                char up[60]; lstrcpynA(up, I.label, sizeof(up)); CharUpperA(up);
                fo->begin(dev); const float tw = fo->measure(up, tzS);
                fo->draw_lc(dev, snap(x0), cy, up, tzS, with_alpha(acc, A), with_alpha(0xA0000000u, A), k.ow);
                float lineEnd = x1;
                if (cnt > 0) { char c[12]; sprintf(c, "%d", cnt); const float cw2 = fo->measure(c, tzS);       // how many, on the right : a small pill
                    const float pw = cw2 + 14.0f * S, ph = snap(tzS * 1.45f), px = snap(x1 - pw);
                    rrect(dev, px, snap(cy - ph * 0.5f), pw, ph, ph * 0.5f, with_alpha(with_a(acc, 0x38), A), with_alpha(with_a(acc, 0x22), A));
                    fo->begin(dev); fo->draw_lc(dev, snap(px + 7.0f * S), cy, c, tzS, with_alpha(0xFFF4F6FAu, A), with_alpha(0xA0000000u, A), k.ow); lineEnd = px - 8.0f * S; }
                hbar_soft(dev, snap(x0 + tw + 10.0f * S), cy, fmaxf(0.0f, lineEnd - (x0 + tw + 10.0f * S)), 1.0f, acc & 0x00FFFFFFu, 0x60, 0.0f);
                continue;
            }
            const float cy = snap(ry[i] + (two[i] ? rh[i] * 0.34f : rowH * 0.5f));
            const bool on = i == curI && P.focused;
            float lx = x0 + 4.0f * S;
            if (empty[i]) {                                               // an empty state : dim, no bullet
                fo->begin(dev); fo->draw_lc(dev, snap(lx), cy, I.label, tz, with_alpha(on ? 0xFFE6ECF4u : 0xFF7C8594u, A), with_alpha(0xA0000000u, A), k.ow);
            } else {
                if (kind[i] == 1) {                                       // done : a green check
                    const float c0 = snap(lx + 7.0f * S);
                    seg_soft(dev, c0 - 5.0f * S, cy, c0 - 1.5f * S, cy + 4.0f * S, 2.0f * S, with_alpha(I.grey ? 0xFF5A6474u : 0xFF7FD08Au, A));
                    seg_soft(dev, c0 - 1.5f * S, cy + 4.0f * S, c0 + 6.0f * S, cy - 5.0f * S, 2.0f * S, with_alpha(I.grey ? 0xFF5A6474u : 0xFF7FD08Au, A));
                } else if (kind[i] == 2) {                                // in progress : a gold diamond
                    const float c0 = snap(lx + 7.0f * S), r = 4.5f * S;
                    disc_glow(dev, c0, cy, r * 0.6f, with_alpha(with_a(acc, 0x60), A), 6.0f * S);
                    fill_tri(dev, c0, cy - r, c0 + r, cy, c0 - r, cy, with_alpha(acc, A)); fill_tri(dev, c0 - r, cy, c0 + r, cy, c0, cy + r, with_alpha(acc, A));
                } else {                                                  // a plain row : a small dot
                    disc(dev, snap(lx + 7.0f * S), cy, 2.6f * S, with_alpha(I.grey ? 0xFF4A5262u : on ? 0xFFFFDA8Au : with_a(acc, 0xC0), A));
                }
                lx += bulletW;
                const u32 lc = I.grey ? 0xFF6E7888u : on ? 0xFFFFFFFFu : I.entry ? (kind[i] == 1 ? 0xFFC9D3E0u : 0xFFE6ECF4u) : 0xFF9AA6B8u;
                fo->begin(dev); fo->draw_lc(dev, snap(lx), cy, I.label, tz, with_alpha(lc, A), with_alpha(0xC8000000u, A), k.ow);
            }
            if (I.value[0] && two[i]) {                                   // a count : its own line -- a bar across, the count at its end
                int a, b; parse_frac(I.value, a, b);
                const float cy2 = snap(ry[i] + rh[i] * 0.72f), vw = fo->measure(I.value, tzS), bx2 = snap(x0 + 4.0f * S + bulletW), bw2 = fmaxf(10.0f, x1 - vw - 12.0f * S - bx2), bh2 = snap(6.0f * S);
                rrect(dev, bx2, snap(cy2 - bh2 * 0.5f), bw2, bh2, bh2 * 0.5f, with_alpha(0x70101826u, A), with_alpha(0x70080C14u, A));
                const float fw = snap(bw2 * fminf(1.0f, (float)a / (float)b));
                if (fw > 1.0f) rrect(dev, bx2, snap(cy2 - bh2 * 0.5f), fw, bh2, bh2 * 0.5f, with_alpha(a >= b ? 0xF09CE27Au : with_a(acc, 0xF0), A), with_alpha(a >= b ? 0xD070B060u : with_a(acc, 0xB0), A));
                fo->begin(dev); fo->draw_lc(dev, snap(x1 - vw), cy2, I.value, tzS, with_alpha(I.grey ? 0xFF6E7888u : a >= b ? 0xFF9CE27Au : 0xFFFFE9B0u, A), with_alpha(0xC8000000u, A), k.ow);
            } else if (I.value[0]) {
                int a, b; const bool frac = parse_frac(I.value, a, b);
                const float vw = fo->measure(I.value, tz), vx = snap(x1 - vw);
                fo->begin(dev); fo->draw_lc(dev, vx, cy, I.value, tz, with_alpha(I.grey ? 0xFF6E7888u : frac && a >= b ? 0xFF9CE27Au : 0xFFFFE9B0u, A), with_alpha(0xC8000000u, A), k.ow);
                if (frac) {                                              // a count : a bar before it, green when full
                    const float bw2 = snap(84.0f * S), bh2 = snap(6.0f * S), bx2 = snap(vx - 12.0f * S - bw2), by2 = snap(cy - bh2 * 0.5f);
                    rrect(dev, bx2, by2, bw2, bh2, bh2 * 0.5f, with_alpha(0x70101826u, A), with_alpha(0x70080C14u, A));
                    const float fw = snap(bw2 * fminf(1.0f, (float)a / (float)b));
                    if (fw > 1.0f) rrect(dev, bx2, by2, fw, bh2, bh2 * 0.5f, with_alpha(a >= b ? 0xF09CE27Au : with_a(acc, 0xF0), A), with_alpha(a >= b ? 0xD070B060u : with_a(acc, 0xB0), A));
                }
            }
            if (I.entry && ms && ms->focused && ms->x >= x0 - 8.0f * S && ms->x < x1 + 8.0f * S && ms->y >= ry[i] && ms->y < ry[i] + rh[i]) { ment = I.entry; mrow = i; }
        }
        // ---- the stat tiles : two a line, the label small and dim, the number large and warm
        if (nStat) {
            float ty = y; for (int i = 0; i < n; ++i) if (!items[s][i].stat && ry[i] >= 0.0f) ty = fmaxf(ty, ry[i] + (items[s][i].section ? secH : rh[i]));
            ty = snap(ty + 16.0f * S);
            const float tw = snap(((x1 - x0) - gapT) * 0.5f); int c = 0;
            for (int i = 0; i < n; ++i) { const PanelItem& I = items[s][i]; if (!I.stat) continue;
                const float tx = snap(x0 + (c & 1) * (tw + gapT)), tyy = snap(ty + (c / 2) * (tileH + gapT)); ++c;
                const u32 sc = stat_colour(I.label, acc);
                rrect(dev, tx, tyy, tw, tileH, snap(7.0f * S), with_alpha(with_a(sc, 0x30), A), with_alpha(0x60101624u, A));
                rrect_stroke(dev, tx, tyy, tw, tileH, snap(7.0f * S), with_alpha(with_a(sc, 0x58), A), 1.0f);
                char up[60]; lstrcpynA(up, I.label, sizeof(up)); CharUpperA(up);
                disc(dev, snap(tx + 15.0f * S), snap(tyy + tileH * 0.30f), 3.0f * S, with_alpha(sc, A));
                fo->begin(dev);
                fo->draw_lc(dev, snap(tx + 24.0f * S), snap(tyy + tileH * 0.30f), up, tzK, with_alpha(0xFFB4BECCu, A), with_alpha(0xA0000000u, A), k.ow);
                fo->draw_lc(dev, snap(tx + 12.0f * S), snap(tyy + tileH * 0.68f), I.value, tzV, with_alpha(sc, A), with_alpha(0xC8000000u, A), k.ow);
            }
        }
        if (scroll) {
            float r0 = 1e9f, r1 = 0.0f; for (int i = 0; i < n; ++i) if (items[s][i].entry && ry[i] >= 0.0f) { r0 = fminf(r0, ry[i]); r1 = fmaxf(r1, ry[i] + rh[i]); }
            if (r1 > r0) {
                const float tx = snap(bx + bw - pad * 0.45f), tw = snap(3.0f * S), th = r1 - r0;
                rrect(dev, tx, snap(r0), tw, th, tw * 0.5f, with_alpha(0x40FFFFFFu, A), with_alpha(0x40FFFFFFu, A));
                const float kh = fmaxf(12.0f * S, th * (float)P.lvis / (float)P.lcount), ky = r0 + (th - kh) * (float)P.ltop / (float)(P.lcount - P.lvis);
                rrect(dev, tx, snap(ky), tw, snap(kh), tw * 0.5f, with_alpha(acc, A), with_alpha(with_a(acc, 0xC0), A));
            }
            const int last = P.ltop + P.lvis < P.lcount ? P.ltop + P.lvis : P.lcount;
            char ft[48]; sprintf(ft, "%d-%d / %d", P.ltop + 1, last, P.lcount);
            const float fy = snap(by + bh - pad - footH * 0.5f), fw = fo->measure(ft, tzS);
            fo->begin(dev); fo->draw_lc(dev, snap(x1 - fw), fy, ft, tzS, with_alpha(0xFF9AA6B8u, A), with_alpha(0xA0000000u, A), k.ow);
            const float ax = snap(x1 - fw - 16.0f * S), aa = 3.5f * S;
            const u32 cUp = P.ltop > 0 ? with_alpha(acc, A) : with_alpha(0x40FFFFFFu, A), cDn = last < P.lcount ? with_alpha(acc, A) : with_alpha(0x40FFFFFFu, A);
            tri(dev, ax, fy - aa * 2.0f, ax - aa, fy - aa * 0.6f, ax + aa, fy - aa * 0.6f, cUp);
            tri(dev, ax, fy + aa * 2.0f, ax - aa, fy + aa * 0.6f, ax + aa, fy + aa * 0.6f, cDn);
            if (P.ltop > 0 || last < P.lcount) { char mt[48]; sprintf(mt, tr("%d more", "%d de plus"), P.lcount - (last - P.ltop));
                fo->begin(dev); fo->draw_lc(dev, snap(x0), fy, mt, tzS, with_alpha(0xFF7C8594u, A), with_alpha(0xA0000000u, A), k.ow); }
        }
        dSetTex(dev, 0, 0); cs(dev);
        if (open && P.focused) {
            g_hit[16][0] = bx / f.screenW; g_hit[16][1] = by / f.screenH; g_hit[16][2] = bw / f.screenW; g_hit[16][3] = bh / f.screenH;
            static float mv[4][2]; const bool moved = mouse_moved(ms, mv[s & 3]);
            if (ment && moved && mrow != g_gpB[s].hoverRow && ment != P.cursor && !items[s][mrow].grey) native_gen_hover(P.name, ment);
            g_gpB[s].hoverRow = mrow;
            if (ment && !items[s][mrow].grey && ms && ms->clicked) native_gen_pick(P.name, ment);
        } else g_gpB[s].hoverRow = -1;
    }
}
// ======== MISSIONS (miss00 as data : native_missions) -- the STORY, not the client's page of five ========
// The header : MISSIONS / the nation or the expansion, how many are done (a pill) ; the CURRENT mission on a card ; the
// COMPLETED ones as a timeline (numbered in story order, a sub-mission indented under its chapter ; with chapters, the one
// holding the cursor is open, the others folded with their count). The timeline folds itself while the cursor is on the
// current mission and opens when it walks into the completed ones ; a click on its header forces it open / closed. A
// long list scrolls around the cursor ("n more" above / below). The client keeps the keys ; the mouse hovers / picks a
// row of the client's page.
namespace { Block g_msB; unsigned g_msMs = 0; int g_msPin = 0, g_msPinCat = -1; float g_msCurY = -1.0f; }
void missions_draw(const Frame& f) {
    const UiConfig& C = ui_config();
    if (!(C.nuHide & NU_NPC) || !f.font || f.screenH <= 0.0f || f.screenW <= 0.0f) { g_msB.vis = 0.0f; g_msB.wasOpen = false; return; }
    const unsigned now = GetTickCount();
    float dt = g_msMs ? (float)(now - g_msMs) / 1000.0f : 0.0f; g_msMs = now; if (dt > 0.1f) dt = 0.1f;
    const NativeMissions& M0 = native_missions();
    static NativeMissions L; const bool up = M0.open && M0.ok;
    if (up) L = M0;
    if (!block_ease(g_msB, up, now, dt) || L.count < 3) { if (!M0.open) { g_msPin = 0; g_msCurY = -1.0f; } return; }
    const NativeGenMenus& G0 = native_gen_menus(); int uiW = 0, uiH = 0; short rx0 = 16, ry0 = 48;
    for (int i = 0; i < G0.np; ++i) if (!strcmp(G0.p[i].name, "miss00")) { uiW = G0.p[i].uiW; uiH = G0.p[i].uiH; rx0 = G0.p[i].rx0; ry0 = G0.p[i].ry0; }
    const Metrics k = metrics(f); const float S = k.S; const u32 dev = f.dev; Font* fo = f.font; const MouseState* ms = f.mouse;
    const float tz = snap(k.tz * 1.06f), tzS = snap(k.tz * 0.80f), tzK = snap(k.tz * 0.74f), tzT = snap(k.tz * 1.32f), tzC = snap(k.tz * 1.22f);
    const float pad = snap(18.0f * S) + k.bi, rowH = snap(k.capH * 1.85f), secH = snap(tzS * 2.6f), indent = snap(18.0f * S);
    const u32 acc = panel_accent("miss00", L.title);
    const char* kicker = tr("Missions", "Missions");
    const char* title = L.title[0] ? L.title : kicker;
    // ---- the rows : the current one, the completed ones (their chapters)
    const int cur = L.cursor; const bool curOnDone = cur >= 3;
    int done[200], nd = 0; for (int i = 3; i < L.count; ++i) if (L.row[i].value >= 0) done[nd++] = i;
    bool depthAny = false; for (int i = 0; i < nd; ++i) if (L.row[done[i]].depth > 0) depthAny = true;
    // folded / open : the cursor in the completed ones ALWAYS opens it (a fold pinned by a click only holds while the cursor
    // is on Current) ; a pin dies with its category (2026-11-22 : RotZ, the client's cursor walked the list, the box stayed folded)
    if (g_msPinCat != L.cat) { g_msPin = 0; g_msPinCat = L.cat; }
    const bool open = curOnDone || g_msPin > 0;
    // the display list of the timeline : a row index, or a folded chapter (its row, its hidden children's count)
    struct DI { int row; int folded; int num; };
    static DI di[220]; int nDi = 0, curDi = -1;
    if (open) {
        int num = 0;
        for (int q = 0; q < nd; ++q) {
            const int r = done[q]; const bool chapter = L.row[r].depth == 0;
            if (chapter) ++num;
            if (depthAny && chapter) {                                     // a chapter : open when it holds the cursor (or the last one, the cursor elsewhere)
                int last = q; while (last + 1 < nd && L.row[done[last + 1]].depth > 0) ++last;
                const bool holds = cur >= r && cur <= done[last];
                const bool openCh = holds || (!curOnDone && last == nd - 1);
                di[nDi++] = { r, openCh ? 0 : last - q, num }; if (r == cur) curDi = nDi - 1;
                if (!openCh) { if (holds) curDi = nDi - 1; q = last; continue; }
                continue;
            }
            if (nDi < 220) { di[nDi++] = { r, 0, chapter ? num : 0 }; if (r == cur) curDi = nDi - 1; }
        }
    }
    // ---- the viewport : at most what fits (14 rows), scrolled to keep the cursor in sight
    const int maxRows = (int)fmaxf(5.0f, fminf(14.0f, (f.screenH * 0.62f - 6.0f * rowH) / rowH));
    static int vTop = 0; if (!open) vTop = 0;
    if (curDi >= 0) { if (curDi < vTop + 1) vTop = curDi - 1; if (curDi > vTop + maxRows - 2) vTop = curDi - maxRows + 2; }
    if (vTop > nDi - maxRows) vTop = nDi - maxRows; if (vTop < 0) vTop = 0;
    const int nVis = nDi < maxRows ? nDi : maxRows;
    const bool moreUp = open && vTop > 0, moreDn = open && vTop + nVis < nDi;
    // ---- measure
    const float headH = snap(tzK * 1.5f + tzT * 1.6f + 14.0f * S);
    const float cardH = snap(tzS * 1.6f + tzC * 1.5f + 18.0f * S);
    float wMax = fo->measure(title, tzT) + snap(140.0f * S);
    wMax = fmaxf(wMax, fo->measure(L.row[1].name, tzC) + snap(80.0f * S));
    for (int i = 0; i < nDi; ++i) wMax = fmaxf(wMax, indent * (1 + L.row[di[i].row].depth) + fo->measure(L.row[di[i].row].name, tz) + snap(110.0f * S));
    const float bw = keep_width(g_msB, fmaxf(snap(480.0f * S), fminf(wMax + 2.0f * pad, f.screenW * 0.5f)));
    const float listH = open ? (nVis * rowH + (moreUp ? rowH * 0.7f : 0.0f) + (moreDn ? rowH * 0.7f : 0.0f)) : (nd ? rowH : rowH);
    const float bh = snap(headH + pad * 0.6f + secH + cardH + snap(12.0f * S) + secH + listH + pad);
    // ---- place : the client's window's corner
    const float sx = uiW > 0 ? f.screenW / (float)uiW : 1.0f, sy = uiH > 0 ? f.screenH / (float)uiH : 1.0f;
    float bx = snap((float)rx0 * sx), by = snap((float)ry0 * sy);
    if (bx + bw > f.screenW - 4.0f) bx = snap(f.screenW - 4.0f - bw); if (bx < 4.0f) bx = 4.0f;
    if (by + bh > f.screenH - 4.0f) by = snap(f.screenH - 4.0f - bh); if (by < 4.0f) by = 4.0f;
    const int jm = menu_place(f, __LINE__, bx, by, bw, bh); g_frameJoin = jm;
    const float tO = ease_out(((float)(now - g_msB.openMs) / 1000.0f) / 0.09f);
    by = snap(by + (1.0f - tO) * 4.0f * S);
    const float A = g_msB.vis * tO; if (A <= 0.0f) return;
    draw_themed_box(dev, f.skin, bx, by, bw, bh, C.muBox, A, S);
    const float x0 = bx + pad, x1 = bx + bw - pad;
    // ---- the header : the band in the accent, MISSIONS, the title, how many are done
    rrect_topcaps(dev, bx + k.bi, by + k.bi, bw - 2.0f * k.bi, headH - k.bi, snap(6.0f * S), with_alpha(with_a(acc, 0x50), A), with_alpha(with_a(acc, 0x0A), A));
    {   char up[32]; lstrcpynA(up, kicker, sizeof(up)); CharUpperA(up);
        float ky = by + snap(10.0f * S);
        fo->begin(dev);
        if (lstrcmpiA(title, kicker)) { fo->draw_lc(dev, snap(x0), snap(ky + tzK * 0.7f), up, tzK, with_alpha(acc, A), with_alpha(0xA0000000u, A), k.ow); ky += tzK * 1.5f; }
        fo->draw_lc(dev, snap(x0), snap(ky + tzT * 0.8f), title, tzT, with_alpha(0xFFF4E8C8u, A), with_alpha(0xC8000000u, A), k.ow);
        char c[48]; sprintf(c, tr("%d completed", "%d accomplies"), nd);
        const float cw = fo->measure(c, tzS) + 26.0f * S, ch = snap(tzS * 1.7f), cx = snap(x1 - cw), cy = snap(by + headH * 0.5f - ch * 0.5f);
        rrect(dev, cx, cy, cw, ch, ch * 0.5f, with_alpha(0x3C7FD08Au, A), with_alpha(0x247FD08Au, A));
        rrect_stroke(dev, cx, cy, cw, ch, ch * 0.5f, with_alpha(0x707FD08Au, A), 1.0f);
        const float ckx = cx + 10.0f * S, cky = cy + ch * 0.5f;
        seg_soft(dev, ckx - 3.5f * S, cky, ckx - 1.0f * S, cky + 3.0f * S, 1.6f * S, with_alpha(0xFF9CE27Au, A));
        seg_soft(dev, ckx - 1.0f * S, cky + 3.0f * S, ckx + 4.0f * S, cky - 3.5f * S, 1.6f * S, with_alpha(0xFF9CE27Au, A));
        fo->begin(dev); fo->draw_lc(dev, snap(cx + 18.0f * S), snap(cky), c, tzS, with_alpha(0xFFE6F4E0u, A), with_alpha(0xA0000000u, A), k.ow);
        rrect(dev, bx + k.bi, snap(by + headH - 1.0f), bw - 2.0f * k.bi, 1.5f, 0.0f, with_alpha(with_a(acc, 0x90), A), with_alpha(with_a(acc, 0x90), A));
    }
    auto section = [&](float y, const char* label, int count, int chevron) {   // chevron : 0 none, 1 folded, 2 open
        const float cy = snap(y + secH * 0.58f); char up[48]; lstrcpynA(up, label, sizeof(up));
        int l = lstrlenA(up); while (l && (up[l - 1] == ':' || up[l - 1] == ' ')) up[--l] = 0; CharUpperA(up);
        float tx = x0;
        if (chevron) { const float a = 4.0f * S, c0 = snap(x0 + 5.0f * S);
            if (chevron == 1) tri(dev, c0 + a, cy, c0 - a * 0.6f, cy - a, c0 - a * 0.6f, cy + a, with_alpha(acc, A));
            else tri(dev, c0, cy + a, c0 - a, cy - a * 0.6f, c0 + a, cy - a * 0.6f, with_alpha(acc, A));
            tx = x0 + 16.0f * S; }
        fo->begin(dev); const float tw = fo->measure(up, tzS);
        fo->draw_lc(dev, snap(tx), cy, up, tzS, with_alpha(acc, A), with_alpha(0xA0000000u, A), k.ow);
        float lineEnd = x1;
        if (count >= 0) { char c[12]; sprintf(c, "%d", count); const float cw2 = fo->measure(c, tzS), pw = cw2 + 14.0f * S, ph = snap(tzS * 1.45f), px = snap(x1 - pw);
            rrect(dev, px, snap(cy - ph * 0.5f), pw, ph, ph * 0.5f, with_alpha(with_a(acc, 0x38), A), with_alpha(with_a(acc, 0x22), A));
            fo->begin(dev); fo->draw_lc(dev, snap(px + 7.0f * S), cy, c, tzS, with_alpha(0xFFF4F6FAu, A), with_alpha(0xA0000000u, A), k.ow); lineEnd = px - 8.0f * S; }
        hbar_soft(dev, snap(tx + tw + 10.0f * S), cy, fmaxf(0.0f, lineEnd - (tx + tw + 10.0f * S)), 1.0f, acc & 0x00FFFFFFu, 0x60, 0.0f);
    };
    float y = by + headH + pad * 0.6f;
    // ---- CURRENT : a card
    section(y, L.hCur[0] ? L.hCur : tr("Current", "En cours"), -1, 0); y += secH;
    int mrow = -1;
    {   const bool none = L.row[1].value < 0, on = cur == 1 && L.focused;
        const float cx = snap(x0 - 4.0f * S), cw = (x1 - x0) + 8.0f * S;
        rrect(dev, cx, snap(y), cw, cardH, snap(8.0f * S), with_alpha(none ? 0x30202A3Cu : with_a(acc, on ? 0x48 : 0x2C), A), with_alpha(0x50101624u, A));
        rrect_stroke(dev, cx, snap(y), cw, cardH, snap(8.0f * S), with_alpha(on ? 0xD0FFDA8Au : none ? 0x30C8D6EAu : with_a(acc, 0x90), A), on ? 1.6f * S : 1.0f);
        const float ly = snap(y + 9.0f * S + tzS * 0.7f), ny = snap(y + cardH - 9.0f * S - tzC * 0.75f);
        if (none) {
            fo->begin(dev); fo->draw_lc(dev, snap(x0 + 10.0f * S), snap(y + cardH * 0.5f), L.row[1].name[0] ? L.row[1].name : tr("No current mission.", "Aucune mission en cours."), tz, with_alpha(on ? 0xFFE6ECF4u : 0xFF7C8594u, A), with_alpha(0xA0000000u, A), k.ow);
        } else {
            const float c0 = snap(x0 + 12.0f * S), r = 6.0f * S;
            disc_glow(dev, c0, ny, r * 0.7f, with_alpha(with_a(acc, 0x70), A), 8.0f * S);
            fill_tri(dev, c0, ny - r, c0 + r, ny, c0 - r, ny, with_alpha(acc, A)); fill_tri(dev, c0 - r, ny, c0 + r, ny, c0, ny + r, with_alpha(acc, A));
            char up[40]; lstrcpynA(up, tr("In progress", "En cours"), sizeof(up)); CharUpperA(up);
            fo->begin(dev);
            fo->draw_lc(dev, snap(x0 + 30.0f * S), ly, up, tzK, with_alpha(0xFF9AA6B8u, A), with_alpha(0xA0000000u, A), k.ow);
            fo->draw_lc(dev, snap(x0 + 30.0f * S), ny, L.row[1].name, tzC, with_alpha(on ? 0xFFFFFFFFu : 0xFFF4E8C8u, A), with_alpha(0xC8000000u, A), k.ow);
        }
        if (ms && ms->focused && ms->x >= cx && ms->x < cx + cw && ms->y >= y && ms->y < y + cardH) mrow = 1;
        y += cardH + snap(12.0f * S);
    }
    // ---- COMPLETED : the header (a chevron, the count ; a click folds / opens), then the timeline or one summary line
    const float secY = y;
    section(y, L.hDone[0] ? L.hDone : tr("Completed", "Accomplies"), nd, nd ? (open ? 2 : 1) : 0); y += secH;
    if (ms && ms->focused && ms->clicked && nd && ms->x >= x0 && ms->x < x1 && ms->y >= secY && ms->y < secY + secH) g_msPin = open ? -1 : 1;
    if (!nd) {
        const char* t = tr("No completed missions.", "Aucune mission accomplie."); for (int i = 3; i < L.count; ++i) if (L.row[i].value < 0 && L.row[i].name[0]) t = L.row[i].name;
        fo->begin(dev); fo->draw_lc(dev, snap(x0 + 4.0f * S), snap(y + rowH * 0.5f), t, tz, with_alpha(0xFF7C8594u, A), with_alpha(0xA0000000u, A), k.ow);
    } else if (!open) {                                                   // folded : the latest one
        char t[120]; _snprintf(t, sizeof(t), "%s  %s", tr("Latest :", "Derniere :"), L.row[done[nd - 1]].name); t[sizeof(t) - 1] = 0;
        fo->begin(dev); fo->draw_lc(dev, snap(x0 + 4.0f * S), snap(y + rowH * 0.5f), t, tz, with_alpha(0xFF9AA6B8u, A), with_alpha(0xA0000000u, A), k.ow);
    } else {
        if (moreUp) { char t[32]; sprintf(t, tr("%d more", "%d de plus"), vTop); const float cy = snap(y + rowH * 0.35f);
            tri(dev, x0 + 10.0f * S, cy - 3.0f * S, x0 + 6.0f * S, cy + 2.0f * S, x0 + 14.0f * S, cy + 2.0f * S, with_alpha(0xFF8A96A8u, A));
            fo->begin(dev); fo->draw_lc(dev, snap(x0 + 22.0f * S), cy, t, tzK, with_alpha(0xFF8A96A8u, A), with_alpha(0xA0000000u, A), k.ow); y += rowH * 0.7f; }
        const float lineX = snap(x0 + 9.0f * S);
        // the spine : one hairline through the nodes
        if (nVis > 1) rrect(dev, lineX - 0.75f * S, snap(y + rowH * 0.5f), 1.5f * S, (nVis - 1) * rowH, 0.0f, with_alpha(0x507FD08Au, A), with_alpha(0x507FD08Au, A));
        // the cursor : a gold capsule gliding to its row
        if (curDi >= vTop && curDi < vTop + nVis && L.focused) {
            const float target = y + (curDi - vTop) * rowH; g_msCurY = g_msCurY < 0.0f ? target : g_msCurY + (target - g_msCurY) * fminf(1.0f, dt * 18.0f);
            const float cx = snap(x0 + 20.0f * S), cw = x1 - cx + 6.0f * S;
            rrect(dev, cx, snap(g_msCurY + 1.0f * S), cw, rowH - 2.0f * S, snap(6.0f * S), with_alpha(0x50FFDA8Au, A), with_alpha(0x2EFFDA8Au, A));
            rrect_stroke(dev, cx, snap(g_msCurY + 1.0f * S), cw, rowH - 2.0f * S, snap(6.0f * S), with_alpha(0xC8FFDA8Au, A), 1.5f * S);
        }
        for (int v = 0; v < nVis; ++v) {
            const DI& D = di[vTop + v]; const NativeMissions::Row& R = L.row[D.row];
            const float ry = y + v * rowH, cy = snap(ry + rowH * 0.5f);
            const bool on = vTop + v == curDi && L.focused, chapter = R.depth == 0;
            if (chapter) {                                                // a chapter / a mission : a green node with its check
                disc(dev, lineX, cy, 6.0f * S, with_alpha(0xFF2A4A34u, A));
                rrect_stroke(dev, lineX - 6.0f * S, cy - 6.0f * S, 12.0f * S, 12.0f * S, 6.0f * S, with_alpha(0xFF7FD08Au, A), 1.2f * S);
                seg_soft(dev, lineX - 3.0f * S, cy, lineX - 0.8f * S, cy + 2.6f * S, 1.5f * S, with_alpha(0xFF9CE27Au, A));
                seg_soft(dev, lineX - 0.8f * S, cy + 2.6f * S, lineX + 3.4f * S, cy - 3.0f * S, 1.5f * S, with_alpha(0xFF9CE27Au, A));
            } else disc(dev, lineX, cy, 2.6f * S, with_alpha(0xFF7FD08Au, A));   // a sub-mission : a small node
            float tx = x0 + 26.0f * S + indent * (float)R.depth;
            if (D.folded) { const float a = 3.5f * S; tri(dev, tx + a, cy, tx - a * 0.6f, cy - a, tx - a * 0.6f, cy + a, with_alpha(acc, A)); tx += 12.0f * S; }
            fo->begin(dev);
            fo->draw_lc(dev, snap(tx), cy, R.name, chapter ? tz : snap(tz * 0.94f), with_alpha(on ? 0xFFFFFFFFu : chapter ? 0xFFE6ECF4u : 0xFFB4BECCu, A), with_alpha(0xC8000000u, A), k.ow);
            char rt[24]; rt[0] = 0;
            if (D.folded) sprintf(rt, "+%d", D.folded); else if (D.num) sprintf(rt, "%d", D.num);
            if (rt[0]) { const float w2 = fo->measure(rt, tzS); fo->draw_lc(dev, snap(x1 - w2), cy, rt, tzS, with_alpha(D.folded ? acc : 0xFF7C8594u, A), with_alpha(0xA0000000u, A), k.ow); }
            if (ms && ms->focused && ms->x >= x0 && ms->x < x1 && ms->y >= ry && ms->y < ry + rowH) mrow = D.row;
        }
        y += nVis * rowH;
        if (moreDn) { char t[32]; sprintf(t, tr("%d more", "%d de plus"), nDi - vTop - nVis); const float cy = snap(y + rowH * 0.35f);
            tri(dev, x0 + 10.0f * S, cy + 3.0f * S, x0 + 6.0f * S, cy - 2.0f * S, x0 + 14.0f * S, cy - 2.0f * S, with_alpha(0xFF8A96A8u, A));
            fo->begin(dev); fo->draw_lc(dev, snap(x0 + 22.0f * S), cy, t, tzK, with_alpha(0xFF8A96A8u, A), with_alpha(0xA0000000u, A), k.ow); }
    }
    dSetTex(dev, 0, 0); cs(dev);
    // ---- the mouse : a row of the client's page (its entries 1.. are the page's rows top..)
    if (M0.open && L.focused) {
        g_hit[16][0] = bx / f.screenW; g_hit[16][1] = by / f.screenH; g_hit[16][2] = bw / f.screenW; g_hit[16][3] = bh / f.screenH;
        const int ent = (mrow >= L.top && mrow < L.top + L.vis) ? mrow - L.top + 1 : 0;
        static float mv[2]; const bool moved = mouse_moved(ms, mv);
        if (ent && moved && mrow != g_msB.hoverRow && mrow != cur) native_gen_hover("miss00", ent);
        g_msB.hoverRow = mrow;
        if (ent && ms && ms->clicked) native_gen_pick("miss00", ent);
    } else g_msB.hoverRow = -1;
}
// ======== the JOURNAL (quest01 as data : native_journal) -- quests and Records of Eminence, readable ========
// One page per list : the header (the family, the title -- a quest area with its neighbours for L / R, the objective
// list's path --, how many), the rows (a quest : done / current / repeatable ; a category : its count and a chevron ;
// an objective : its state, its rewards, its progress bar), scrolled around the cursor, and for an objective the DETAIL
// strip : its description and its rewards in full. The client keeps the keys ; the mouse hovers / picks a page row.
namespace { Block g_jrB; unsigned g_jrMs = 0; float g_jrCurY = -1.0f; int g_jrTop = 0; }
static const char* jr_area_name(int a) {
    static const char* EN[11] = { "San d'Oria", "Bastok", "Windurst", "Jeuno", "Other Areas", "Outlands", "Aht Urhgan", "Crystal War", "Abyssea", "Adoulin", "Coalition" };
    return a >= 0 && a < 11 ? EN[a] : "";
}
static u32 jr_area_colour(int a) {
    static const u32 C[11] = { 0xFFE8706Au, 0xFF6AA8F0u, 0xFF7CCF7Au, 0xFFE8C060u, 0xFFB4BECCu, 0xFF9CC28Au, 0xFFF09A50u, 0xFFA88CF0u, 0xFF68D0D8u, 0xFF84DCA8u, 0xFFE0B060u };
    return a >= 0 && a < 11 ? C[a] : 0xFFE0B060u;
}
// greedy word wrap into at most maxL lines (the last one ends with "..." when cut)
static int wrap_lines(Font* fo, const char* t, float size, float w, char out[][120], int maxL) {
    int n = 0; const char* p = t;
    while (*p && n < maxL) {
        while (*p == ' ') ++p; if (!*p) break;
        char line[120]; int L = 0; const char* lastSp = 0; int lastL = 0;
        while (p[0] && L < 118) { line[L] = *p; line[L + 1] = 0;
            if (fo->measure(line, size) > w) { if (lastSp) { p = lastSp + 1; L = lastL; } break; }
            if (*p == ' ') { lastSp = p; lastL = L; } ++L; ++p; }
        line[L] = 0; lstrcpynA(out[n], line, 120); ++n;
        if (n == maxL && *p) { int l2 = lstrlenA(out[n - 1]); if (l2 > 3) lstrcpyA(out[n - 1] + l2 - 3, "..."); }
    }
    return n;
}
void journal_draw(const Frame& f) {
    const UiConfig& C = ui_config();
    if (!(C.nuHide & NU_NPC) || !f.font || f.screenH <= 0.0f || f.screenW <= 0.0f) { g_jrB.vis = 0.0f; g_jrB.wasOpen = false; return; }
    const unsigned now = GetTickCount();
    float dt = g_jrMs ? (float)(now - g_jrMs) / 1000.0f : 0.0f; g_jrMs = now; if (dt > 0.1f) dt = 0.1f;
    const NativeJournal& J0 = native_journal();
    static NativeJournal L; const bool up = J0.open && J0.ok;
    if (up) L = J0;
    if (!block_ease(g_jrB, up, now, dt)) { if (!J0.open) { g_jrCurY = -1.0f; g_jrTop = 0; } return; }
    const NativeGenMenus& G0 = native_gen_menus(); int uiW = 0, uiH = 0; short rx0 = 16, ry0 = 48;
    for (int i = 0; i < G0.np; ++i) if (!strcmp(G0.p[i].name, "quest01")) { uiW = G0.p[i].uiW; uiH = G0.p[i].uiH; rx0 = G0.p[i].rx0; ry0 = G0.p[i].ry0; }
    const Metrics k = metrics(f); const float S = k.S; const u32 dev = f.dev; Font* fo = f.font; const MouseState* ms = f.mouse;
    const float tz = snap(k.tz * 1.04f), tzS = snap(k.tz * 0.80f), tzK = snap(k.tz * 0.74f), tzT = snap(k.tz * 1.30f);
    const float pad = snap(18.0f * S) + k.bi, rowH = snap(k.capH * 1.85f), rowH2 = snap(k.capH * 2.75f);
    const bool roe = L.mode >= 2, quests = !roe;
    // ---- the names : family, title, subtitle, accent
    const char* kicker = quests ? tr("Quests", "Quetes") : L.mode == 4 ? "Vana'Bout" : "Records of Eminence";
    char title[64] = "", subt[64] = "";
    if (quests) { lstrcpynA(title, jr_area_name(L.area), sizeof(title)); const char* c = strchr(L.title, ':'); lstrcpynA(subt, c ? c + 1 + (c[1] == ' ') : (L.mode ? tr("Completed Quests", "Quetes accomplies") : tr("Current Quests", "Quetes en cours")), sizeof(subt)); }
    else if (L.mode == 3) { lstrcpynA(title, L.depth > 0 && L.crumb[L.depth][0] ? L.crumb[L.depth] : tr("Objective List", "Liste des objectifs"), sizeof(title));
        int o = 0; for (int i = 0; i < L.depth && o < 60; ++i) o += _snprintf(subt + o, sizeof(subt) - o, "%s%s", i ? "  >  " : "", i == 0 ? tr("Objective List", "Liste des objectifs") : L.crumb[i]); subt[63] = 0; }
    else { lstrcpynA(title, L.title, sizeof(title)); char* sp = strstr(title, "   "); if (sp) *sp = 0; if (!title[0]) lstrcpynA(title, L.mode == 4 ? "Vana'Bout" : tr("Current", "En cours"), sizeof(title)); }
    const u32 acc = quests ? jr_area_colour(L.area) : L.mode == 4 ? 0xFFFFA060u : 0xFF62BEF8u;
    // ---- the viewport
    const int maxRows = (int)fmaxf(5.0f, fminf(roe ? 9.0f : 13.0f, (f.screenH * 0.6f) / (roe ? rowH2 : rowH)));
    if (!L.count) g_jrTop = 0;
    if (L.cursor < g_jrTop + 1) g_jrTop = L.cursor - 1; if (L.cursor > g_jrTop + maxRows - 2) g_jrTop = L.cursor - maxRows + 2;
    if (g_jrTop > L.count - maxRows) g_jrTop = L.count - maxRows; if (g_jrTop < 0) g_jrTop = 0;
    const int nVis = L.count < maxRows ? L.count : maxRows;
    auto rh_of = [&](int r) { const NativeJournal::Row& R = L.row[r]; return roe && !R.category ? rowH2 : rowH; };
    float listH = 0.0f; for (int v = 0; v < nVis; ++v) listH += rh_of(g_jrTop + v);
    // the detail strip : the cursor's objective
    const NativeJournal::Row* CR = L.count ? &L.row[L.cursor] : 0;
    const bool detail = roe && CR && !CR->category && CR->name[0];
    char dl[4][120]; int nDl = 0; const float headH = snap(tzK * 1.5f + tzT * 1.6f + (subt[0] ? tzS * 1.4f : 0.0f) + 14.0f * S);
    const float bw = keep_width(g_jrB, fmaxf(snap(500.0f * S), fminf(f.screenW * 0.42f, snap(560.0f * S))));
    const float x0r = pad, x1r = bw - pad;
    if (detail) nDl = wrap_lines(fo, native_journal_desc((unsigned)CR->value), tzS, x1r - x0r - 4.0f * S, dl, 3);
    const float detH = detail ? snap(12.0f * S + tzS * 1.5f * (float)nDl + tzS * 2.4f + 16.0f * S) : 0.0f;
    const float footH = L.count > nVis ? snap(tzS * 2.2f) : 0.0f;
    const float bh = snap(headH + pad * 0.6f + (L.count ? listH : rowH) + footH + (detail ? snap(12.0f * S) + detH : 0.0f) + pad);
    // ---- place
    const float sx = uiW > 0 ? f.screenW / (float)uiW : 1.0f, sy = uiH > 0 ? f.screenH / (float)uiH : 1.0f;
    float bx = snap((float)rx0 * sx), by = snap((float)ry0 * sy);
    if (bx + bw > f.screenW - 4.0f) bx = snap(f.screenW - 4.0f - bw); if (bx < 4.0f) bx = 4.0f;
    if (by + bh > f.screenH - 4.0f) by = snap(f.screenH - 4.0f - bh); if (by < 4.0f) by = 4.0f;
    const int jm = menu_place(f, __LINE__, bx, by, bw, bh); g_frameJoin = jm;
    const float tO = ease_out(((float)(now - g_jrB.openMs) / 1000.0f) / 0.09f);
    by = snap(by + (1.0f - tO) * 4.0f * S);
    const float A = g_jrB.vis * tO; if (A <= 0.0f) return;
    draw_themed_box(dev, f.skin, bx, by, bw, bh, C.muBox, A, S);
    const float x0 = bx + pad, x1 = bx + bw - pad;
    // ---- the header
    rrect_topcaps(dev, bx + k.bi, by + k.bi, bw - 2.0f * k.bi, headH - k.bi, snap(6.0f * S), with_alpha(with_a(acc, 0x50), A), with_alpha(with_a(acc, 0x0A), A));
    {   char up[48]; lstrcpynA(up, kicker, sizeof(up)); CharUpperA(up);
        float ky = by + snap(10.0f * S);
        fo->begin(dev);
        fo->draw_lc(dev, snap(x0), snap(ky + tzK * 0.7f), up, tzK, with_alpha(acc, A), with_alpha(0xA0000000u, A), k.ow); ky += tzK * 1.5f;
        fo->draw_lc(dev, snap(x0), snap(ky + tzT * 0.8f), title, tzT, with_alpha(0xFFF4E8C8u, A), with_alpha(0xC8000000u, A), k.ow); ky += tzT * 1.6f;
        if (subt[0]) fo->draw_lc(dev, snap(x0), snap(ky + tzS * 0.5f), subt, tzS, with_alpha(0xFFB4BECCu, A), with_alpha(0xA0000000u, A), k.ow);
        // the right : the count (RoE current : n / 30 with a bar) ; a quest area : its neighbours (L / R)
        char c[48]; int a2 = 0, b2 = 0;
        if (L.mode == 2) { sprintf(c, "%d/30", L.accepted); a2 = L.accepted; b2 = 30; }
        else if (L.mode == 3 && L.catOk && L.catTotal > 0) { sprintf(c, tr("%d/%d done", "%d/%d faits"), L.catDone, L.catTotal); a2 = L.catDone; b2 = L.catTotal; }
        else if (quests) sprintf(c, tr("%d quests", "%d quetes"), L.count);
        else sprintf(c, "%d", L.count);
        const float barW = b2 ? snap(54.0f * S) : 0.0f, cw = fo->measure(c, tzS) + barW + (b2 ? 34.0f : 20.0f) * S, ch = snap(tzS * 1.8f), cx = snap(x1 - cw), cy = snap(by + snap(10.0f * S));
        rrect(dev, cx, cy, cw, ch, ch * 0.5f, with_alpha(with_a(acc, 0x3C), A), with_alpha(with_a(acc, 0x22), A));
        rrect_stroke(dev, cx, cy, cw, ch, ch * 0.5f, with_alpha(with_a(acc, 0x80), A), 1.0f);
        float tx = cx + 10.0f * S;
        if (b2) { const float bh2 = snap(5.0f * S), by2 = snap(cy + ch * 0.5f - bh2 * 0.5f);
            rrect(dev, tx, by2, barW, bh2, bh2 * 0.5f, with_alpha(0x70101826u, A), with_alpha(0x70080C14u, A));
            const float fw = snap(barW * fminf(1.0f, (float)a2 / (float)b2)); if (fw > 1.0f) rrect(dev, tx, by2, fw, bh2, bh2 * 0.5f, with_alpha(acc, A), with_alpha(with_a(acc, 0xB0), A));
            tx += barW + 10.0f * S; }
        fo->begin(dev); fo->draw_lc(dev, snap(tx), snap(cy + ch * 0.5f), c, tzS, with_alpha(0xFFF4F6FAu, A), with_alpha(0xA0000000u, A), k.ow);
        if (quests) {                                                    // L / R : the previous / next area
            const int pa = (L.area + 10) % 11, na = (L.area + 1) % 11; char lr[80]; _snprintf(lr, sizeof(lr), "<  %s     %s  >", jr_area_name(pa), jr_area_name(na)); lr[79] = 0;
            const float lw = fo->measure(lr, tzK); fo->draw_lc(dev, snap(x1 - lw), snap(by + headH - 10.0f * S - tzK * 0.5f), lr, tzK, with_alpha(0xFF8A96A8u, A), with_alpha(0xA0000000u, A), k.ow);
        }
        rrect(dev, bx + k.bi, snap(by + headH - 1.0f), bw - 2.0f * k.bi, 1.5f, 0.0f, with_alpha(with_a(acc, 0x90), A), with_alpha(with_a(acc, 0x90), A));
    }
    float y = by + headH + pad * 0.6f;
    int mrow = -1;
    if (!L.count) {
        fo->begin(dev); fo->draw_lc(dev, snap(x0 + 4.0f * S), snap(y + rowH * 0.5f), roe ? tr("No objectives.", "Aucun objectif.") : tr("No quests here.", "Aucune quete ici."), tz, with_alpha(0xFF7C8594u, A), with_alpha(0xA0000000u, A), k.ow);
        y += rowH;
    } else {
        // the cursor
        {   float ty = y; for (int r = g_jrTop; r < L.cursor; ++r) ty += rh_of(r);
            if (L.cursor >= g_jrTop && L.cursor < g_jrTop + nVis && L.focused) {
                g_jrCurY = g_jrCurY < 0.0f ? ty : g_jrCurY + (ty - g_jrCurY) * fminf(1.0f, dt * 18.0f);
                const float h = rh_of(L.cursor), cx = snap(x0 - 8.0f * S), cw = (x1 - x0) + 16.0f * S;
                rrect(dev, cx, snap(g_jrCurY + 1.0f * S), cw, h - 2.0f * S, snap(6.0f * S), with_alpha(0x50FFDA8Au, A), with_alpha(0x2EFFDA8Au, A));
                rrect_stroke(dev, cx, snap(g_jrCurY + 1.0f * S), cw, h - 2.0f * S, snap(6.0f * S), with_alpha(0xC8FFDA8Au, A), 1.5f * S); } }
        float ry = y;
        for (int v = 0; v < nVis; ++v) {
            const int r = g_jrTop + v; const NativeJournal::Row& R = L.row[r]; const float h = rh_of(r);
            const bool on = r == L.cursor && L.focused;
            const bool tal = R.category && R.tallyOk && R.objTotal > 0, full = tal && R.objDone >= R.objTotal;   // a category's done / total
            const u32 fc = full ? 0xFF9CE27Au : acc;
            if (!on && (v & 1)) rrect(dev, snap(x0 - 8.0f * S), snap(ry), (x1 - x0) + 16.0f * S, h, snap(4.0f * S), with_alpha(0x0CFFFFFFu, A), with_alpha(0x0CFFFFFFu, A));
            const float cy = snap(ry + (h == rowH2 ? h * 0.33f : h * 0.5f)), ix = snap(x0 + 8.0f * S);
            // the state icon
            if (R.category) {                                             // a folder : a small accent tile
                rrect(dev, ix - 6.0f * S, cy - 5.0f * S, 12.0f * S, 10.0f * S, 2.0f * S, with_alpha(with_a(fc, 0xB0), A), with_alpha(with_a(fc, 0x80), A));
                rrect(dev, ix - 6.0f * S, cy - 7.0f * S, 6.0f * S, 3.0f * S, 1.0f * S, with_alpha(with_a(fc, 0xB0), A), with_alpha(with_a(fc, 0xB0), A));
            } else if (R.ready) {                                         // a reward to claim : a gold disc with a glow
                disc_glow(dev, ix, cy, 4.5f * S, with_alpha(0x90FFDA8Au, A), 7.0f * S); disc(dev, ix, cy, 4.5f * S, with_alpha(0xFFFFDA8Au, A));
            } else if (R.done || R.locked) {                              // done : a green check
                const u32 gc = R.locked ? 0xFF6E8A74u : 0xFF9CE27Au;
                seg_soft(dev, ix - 5.0f * S, cy, ix - 1.5f * S, cy + 4.0f * S, 2.0f * S, with_alpha(gc, A));
                seg_soft(dev, ix - 1.5f * S, cy + 4.0f * S, ix + 6.0f * S, cy - 5.0f * S, 2.0f * S, with_alpha(gc, A));
            } else if (R.current || R.accepted) {                         // in progress : a diamond in the accent
                const float rr = 4.5f * S; fill_tri(dev, ix, cy - rr, ix + rr, cy, ix - rr, cy, with_alpha(acc, A)); fill_tri(dev, ix - rr, cy, ix + rr, cy, ix, cy + rr, with_alpha(acc, A));
            } else disc(dev, ix, cy, 2.5f * S, with_alpha(0xFF6E7A8Cu, A));
            // the name
            const u32 lc = on ? 0xFFFFFFFFu : full ? 0xFFC8F0B4u : R.locked ? 0xFF8A94A4u : R.ready ? 0xFFFFE9B0u : 0xFFE6ECF4u;
            const float nx = snap(x0 + 26.0f * S);
            fo->begin(dev); fo->draw_lc(dev, nx, cy, R.name[0] ? R.name : "...", tz, with_alpha(lc, A), with_alpha(0xC8000000u, A), k.ow);
            // the right of the first line : a category's count, a quest's repeatable mark, an objective's rewards
            float rx = x1;
            auto chip = [&](const char* t, u32 col) { const float w = fo->measure(t, tzK) + 12.0f * S, hh = snap(tzK * 1.6f), px = snap(rx - w);
                rrect(dev, px, snap(cy - hh * 0.5f), w, hh, hh * 0.5f, with_alpha(with_a(col, 0x30), A), with_alpha(with_a(col, 0x1C), A));
                fo->begin(dev); fo->draw_lc(dev, snap(px + 6.0f * S), cy, t, tzK, with_alpha(col, A), with_alpha(0xA0000000u, A), k.ow); rx = px - 6.0f * S; };
            if (R.category) { char t[16]; sprintf(t, "%d", R.children); const float a = 3.5f * S;
                tri(dev, x1, cy, x1 - a * 1.6f, cy - a, x1 - a * 1.6f, cy + a, with_alpha(0xFF8A96A8u, A)); rx = x1 - 12.0f * S;
                if (tal) {                                                // done / total : a chip (green when all done) + a small bar
                    sprintf(t, "%d/%d", R.objDone, R.objTotal); chip(t, fc);
                    const float mw = snap(54.0f * S), mh = snap(4.0f * S), mx = snap(rx - mw), my = snap(cy - mh * 0.5f);
                    rrect(dev, mx, my, mw, mh, mh * 0.5f, with_alpha(0x70101826u, A), with_alpha(0x70080C14u, A));
                    const float fw = snap(mw * fminf(1.0f, (float)R.objDone / (float)R.objTotal));
                    if (fw > 1.0f) rrect(dev, mx, my, fw, mh, mh * 0.5f, with_alpha(with_a(fc, 0xF0), A), with_alpha(with_a(fc, 0xB0), A));
                    rx = mx - 8.0f * S; }
                else if (R.children > 0) chip(t, acc); }
            else if (quests) { if (R.repeatable) chip(tr("Repeatable", "Repetable"), 0xFF8CC8FFu); if (R.current && R.done) chip(tr("Again", "A nouveau"), acc); }
            else {
                char t[24];
                if (R.ready) chip(tr("Claim!", "A recuperer !"), 0xFFFFDA8Au);
                if (R.sparks > 0) { sprintf(t, "%d Sp", R.sparks); chip(t, 0xFFFFC857u); }
                if (R.acc > 0) { sprintf(t, "%d Acc", R.acc); chip(t, 0xFFC09CFFu); }
                if (R.repeatable) chip("R", 0xFF8CC8FFu);
            }
            // an objective's second line : its progress bar and count
            if (h == rowH2) {
                const int req = R.required > 0 ? R.required : 1, pr = R.locked && !R.accepted ? req : R.progress;
                char t[32]; sprintf(t, "%d/%d", pr, req);
                const float cy2 = snap(ry + h * 0.72f), vw = fo->measure(t, tzS), bx2 = nx, bw2 = fmaxf(10.0f, x1 - vw - 12.0f * S - bx2), bh2 = snap(5.0f * S);
                rrect(dev, bx2, snap(cy2 - bh2 * 0.5f), bw2, bh2, bh2 * 0.5f, with_alpha(0x70101826u, A), with_alpha(0x70080C14u, A));
                const float fr = fminf(1.0f, (float)pr / (float)req), fw = snap(bw2 * fr);
                const bool full = pr >= req;
                if (fw > 1.0f) rrect(dev, bx2, snap(cy2 - bh2 * 0.5f), fw, bh2, bh2 * 0.5f, with_alpha(full ? 0xF09CE27Au : R.accepted ? with_a(acc, 0xF0) : 0x90808A9Au, A), with_alpha(full ? 0xD070B060u : R.accepted ? with_a(acc, 0xB0) : 0x70606A7Au, A));
                fo->begin(dev); fo->draw_lc(dev, snap(x1 - vw), cy2, t, tzS, with_alpha(full ? 0xFF9CE27Au : R.accepted ? 0xFFFFE9B0u : 0xFF8A96A8u, A), with_alpha(0xC8000000u, A), k.ow);
            }
            if (ms && ms->focused && ms->x >= x0 - 8.0f * S && ms->x < x1 + 8.0f * S && ms->y >= ry && ms->y < ry + h) mrow = r;
            ry += h;
        }
        y = ry;
        if (L.count > nVis) {                                             // the footer : where, how many more ; a scrollbar
            const int last = g_jrTop + nVis; char ft[48]; sprintf(ft, "%d-%d / %d", g_jrTop + 1, last, L.count);
            const float fy = snap(y + footH * 0.5f), fw = fo->measure(ft, tzS);
            fo->begin(dev); fo->draw_lc(dev, snap(x1 - fw), fy, ft, tzS, with_alpha(0xFF9AA6B8u, A), with_alpha(0xA0000000u, A), k.ow);
            const float ax = snap(x1 - fw - 16.0f * S), aa = 3.5f * S;
            tri(dev, ax, fy - aa * 2.0f, ax - aa, fy - aa * 0.6f, ax + aa, fy - aa * 0.6f, g_jrTop > 0 ? with_alpha(acc, A) : with_alpha(0x40FFFFFFu, A));
            tri(dev, ax, fy + aa * 2.0f, ax - aa, fy + aa * 0.6f, ax + aa, fy + aa * 0.6f, last < L.count ? with_alpha(acc, A) : with_alpha(0x40FFFFFFu, A));
            const float tx = snap(bx + bw - pad * 0.45f), tw = snap(3.0f * S), th = listH, y0l = by + headH + pad * 0.6f;
            rrect(dev, tx, snap(y0l), tw, th, tw * 0.5f, with_alpha(0x40FFFFFFu, A), with_alpha(0x40FFFFFFu, A));
            const float kh = fmaxf(12.0f * S, th * (float)nVis / (float)L.count), ky = y0l + (th - kh) * (float)g_jrTop / (float)(L.count - nVis);
            rrect(dev, tx, snap(ky), tw, snap(kh), tw * 0.5f, with_alpha(acc, A), with_alpha(with_a(acc, 0xC0), A));
            y += footH;
        }
    }
    // ---- the detail strip : the cursor's objective -- its description, its rewards
    if (detail) {
        y += snap(12.0f * S);
        rrect(dev, snap(x0 - 6.0f * S), snap(y), (x1 - x0) + 12.0f * S, detH, snap(7.0f * S), with_alpha(0x50202A3Cu, A), with_alpha(0x60101624u, A));
        rrect_stroke(dev, snap(x0 - 6.0f * S), snap(y), (x1 - x0) + 12.0f * S, detH, snap(7.0f * S), with_alpha(with_a(acc, 0x50), A), 1.0f);
        float ty = y + 10.0f * S;
        fo->begin(dev);
        for (int i = 0; i < nDl; ++i) { fo->draw_lc(dev, snap(x0 + 4.0f * S), snap(ty + tzS * 0.7f), dl[i], tzS, with_alpha(0xFFC9D3E0u, A), with_alpha(0xA0000000u, A), k.ow); ty += tzS * 1.5f; }
        ty += tzS * 0.4f;
        char rw[160]; int o = _snprintf(rw, sizeof(rw), "%s  %d Sparks  .  %d EXP", tr("Rewards :", "Recompenses :"), CR->sparks, CR->exp);
        if (CR->cap > 0) o += _snprintf(rw + o, sizeof(rw) - o, "  .  %d CAP", CR->cap);
        if (CR->acc > 0) o += _snprintf(rw + o, sizeof(rw) - o, "  .  %d Accolades", CR->acc);
        rw[159] = 0;
        fo->draw_lc(dev, snap(x0 + 4.0f * S), snap(ty + tzS * 0.7f), rw, tzS, with_alpha(0xFFFFE9B0u, A), with_alpha(0xA0000000u, A), k.ow);
        const char* rp = CR->repeatable ? tr("Repeatable", "Repetable") : tr("Not repeatable", "Non repetable");
        const float rpw = fo->measure(rp, tzK); fo->draw_lc(dev, snap(x1 - rpw), snap(ty + tzS * 0.7f), rp, tzK, with_alpha(CR->repeatable ? 0xFF8CC8FFu : 0xFF8A96A8u, A), with_alpha(0xA0000000u, A), k.ow);
    }
    dSetTex(dev, 0, 0); cs(dev);
    // ---- the mouse : a row of the client's page
    if (J0.open && L.focused) {
        g_hit[16][0] = bx / f.screenW; g_hit[16][1] = by / f.screenH; g_hit[16][2] = bw / f.screenW; g_hit[16][3] = bh / f.screenH;
        const int ent = (mrow >= L.top && mrow < L.top + L.vis) ? mrow - L.top + 1 : 0;
        static float mv[2]; const bool moved = mouse_moved(ms, mv);
        if (ent && moved && mrow != g_jrB.hoverRow && mrow != L.cursor) native_gen_hover("quest01", ent);
        g_jrB.hoverRow = mrow;
        if (ent && ms && ms->clicked) native_gen_pick("quest01", ent);
    } else g_jrB.hoverRow = -1;
}
// ======== a MISSION's page (miss00 state 2 : a row validated) -- its name, current / completed, its orders ========
// The client draws the name and the DAT's "Mission Orders" in its 8-row window ; AioHUD : a card in the category's
// accent, the state on a chip, the orders wrapped in full (the DAT's line breaks kept). No keys of its own : the client's
// Cancel goes back to the list.
namespace { Block g_mdB; unsigned g_mdMs = 0; }
void mission_detail_draw(const Frame& f) {
    const UiConfig& C = ui_config();
    if (!(C.nuHide & NU_NPC) || !f.font || f.screenH <= 0.0f || f.screenW <= 0.0f) { g_mdB.vis = 0.0f; g_mdB.wasOpen = false; return; }
    const unsigned now = GetTickCount();
    float dt = g_mdMs ? (float)(now - g_mdMs) / 1000.0f : 0.0f; g_mdMs = now; if (dt > 0.1f) dt = 0.1f;
    const NativeMissions& M0 = native_missions();
    static NativeMissions L; const bool up = M0.open && M0.state == 2 && M0.detailOk;
    if (up) L = M0;
    if (!block_ease(g_mdB, up, now, dt) || !L.dName[0]) return;
    const NativeGenMenus& G0 = native_gen_menus(); int uiW = 0, uiH = 0; short rx0 = 16, ry0 = 48;
    for (int i = 0; i < G0.np; ++i) if (!strcmp(G0.p[i].name, "miss00")) { uiW = G0.p[i].uiW; uiH = G0.p[i].uiH; rx0 = G0.p[i].rx0; ry0 = G0.p[i].ry0; }
    const Metrics k = metrics(f); const float S = k.S; const u32 dev = f.dev; Font* fo = f.font;
    const float tz = snap(k.tz * 1.0f), tzS = snap(k.tz * 0.80f), tzK = snap(k.tz * 0.74f), tzT = snap(k.tz * 1.32f);
    const float pad = snap(18.0f * S) + k.bi;
    const u32 acc = panel_accent("miss00", L.title);
    const u32 sc = L.detailCurrent ? acc : 0xFF9CE27Au;
    // ---- the orders : each DAT line wrapped to the box
    const float bw = keep_width(g_mdB, fmaxf(snap(500.0f * S), fminf(f.screenW * 0.42f, snap(580.0f * S))));
    const float tw = bw - 2.0f * pad;
    static char dl[22][120]; int nDl = 0;
    {   const char* p = L.dDesc;
        while (*p && nDl < 22) {
            char para[700]; int n = 0; while (p[n] && p[n] != '\n' && n < 699) { para[n] = p[n]; ++n; } para[n] = 0;
            if (n) nDl += wrap_lines(fo, para, tz, tw, dl + nDl, 22 - nDl);
            p += n; if (*p == '\n') ++p;
        } }
    // no orders in the DAT (the final mission of a finished story : RotZ / CoP "The Last Verse" are an empty line, measured 2026-11-22)
    const char* none = L.detailCurrent ? tr("No further orders : this story is complete.", "Plus d'ordres : cette histoire est terminee.")
                                       : tr("No orders recorded for this mission.", "Aucun ordre pour cette mission.");
    const float headH = snap(tzK * 1.5f + tzT * 1.6f + 14.0f * S), chipH = snap(tzS * 1.8f), lineH = snap(tz * 1.45f);
    const float bh = snap(headH + pad * 0.7f + chipH + 12.0f * S + (nDl ? nDl : 1) * lineH + 10.0f * S + tzS * 1.6f + pad);
    // ---- place : the client's window's corner
    const float sx = uiW > 0 ? f.screenW / (float)uiW : 1.0f, sy = uiH > 0 ? f.screenH / (float)uiH : 1.0f;
    float bx = snap((float)rx0 * sx), by = snap((float)ry0 * sy);
    if (bx + bw > f.screenW - 4.0f) bx = snap(f.screenW - 4.0f - bw); if (bx < 4.0f) bx = 4.0f;
    if (by + bh > f.screenH - 4.0f) by = snap(f.screenH - 4.0f - bh); if (by < 4.0f) by = 4.0f;
    const int jm = menu_place(f, __LINE__, bx, by, bw, bh); g_frameJoin = jm;
    const float tO = ease_out(((float)(now - g_mdB.openMs) / 1000.0f) / 0.09f);
    by = snap(by + (1.0f - tO) * 4.0f * S);
    const float A = g_mdB.vis * tO; if (A <= 0.0f) return;
    draw_themed_box(dev, f.skin, bx, by, bw, bh, C.muBox, A, S);
    const float x0 = bx + pad, x1 = bx + bw - pad;
    // ---- the header : MISSIONS / the category, then the mission's name
    rrect_topcaps(dev, bx + k.bi, by + k.bi, bw - 2.0f * k.bi, headH - k.bi, snap(6.0f * S), with_alpha(with_a(acc, 0x50), A), with_alpha(with_a(acc, 0x0A), A));
    float y = by + snap(10.0f * S);
    {   char up[64]; _snprintf(up, sizeof(up), "%s  -  %s", tr("Missions", "Missions"), L.title[0] ? L.title : ""); up[63] = 0; CharUpperA(up);
        fo->begin(dev);
        fo->draw_lc(dev, snap(x0), snap(y + tzK * 0.7f), up, tzK, with_alpha(acc, A), with_alpha(0xA0000000u, A), k.ow); y += tzK * 1.5f;
        fo->draw_lc(dev, snap(x0), snap(y + tzT * 0.8f), L.dName, tzT, with_alpha(0xFFF4E8C8u, A), with_alpha(0xC8000000u, A), k.ow);
        rrect(dev, bx + k.bi, snap(by + headH - 1.0f), bw - 2.0f * k.bi, 1.5f, 0.0f, with_alpha(with_a(acc, 0x90), A), with_alpha(with_a(acc, 0x90), A)); }
    y = by + headH + pad * 0.7f;
    // ---- the state chip : In progress (a diamond, the accent) / Completed (a check, green)
    {   const char* t = L.detailCurrent ? tr("In progress", "En cours") : tr("Completed", "Accomplie");
        const float cw = fo->measure(t, tzS) + 30.0f * S, cy = snap(y + chipH * 0.5f);
        rrect(dev, snap(x0), snap(y), cw, chipH, chipH * 0.5f, with_alpha(with_a(sc, 0x34), A), with_alpha(with_a(sc, 0x1E), A));
        rrect_stroke(dev, snap(x0), snap(y), cw, chipH, chipH * 0.5f, with_alpha(with_a(sc, 0x90), A), 1.0f);
        const float ix = snap(x0 + 12.0f * S);
        if (L.detailCurrent) { const float r = 4.0f * S; fill_tri(dev, ix, cy - r, ix + r, cy, ix - r, cy, with_alpha(sc, A)); fill_tri(dev, ix - r, cy, ix + r, cy, ix, cy + r, with_alpha(sc, A)); }
        else { seg_soft(dev, ix - 3.5f * S, cy, ix - 1.0f * S, cy + 3.0f * S, 1.6f * S, with_alpha(sc, A)); seg_soft(dev, ix - 1.0f * S, cy + 3.0f * S, ix + 4.0f * S, cy - 3.5f * S, 1.6f * S, with_alpha(sc, A)); }
        fo->begin(dev); fo->draw_lc(dev, snap(x0 + 22.0f * S), cy, t, tzS, with_alpha(0xFFF4F6FAu, A), with_alpha(0xA0000000u, A), k.ow);
        y += chipH + 12.0f * S; }
    // ---- the orders : a line starting with a word and ':' (the DAT's "Mission Orders:") in the accent, the rest light
    fo->begin(dev);
    if (!nDl) fo->draw_lc(dev, snap(x0), snap(y + lineH * 0.5f), none, tz, with_alpha(0xFF7C8594u, A), with_alpha(0xA0000000u, A), k.ow);
    for (int i = 0; i < nDl; ++i) {
        const int l = lstrlenA(dl[i]); const bool head = l > 1 && l < 40 && dl[i][l - 1] == ':';
        fo->draw_lc(dev, snap(x0), snap(y + lineH * 0.5f), dl[i], tz, with_alpha(head ? acc : 0xFFDCE2EAu, A), with_alpha(0xB0000000u, A), k.ow);
        y += lineH; }
    if (!nDl) y += lineH;
    // ---- the hint : the client's Cancel goes back
    y += 10.0f * S;
    fo->draw_lc(dev, snap(x0), snap(y + tzS * 0.6f), tr("Cancel : back to the list", "Annuler : retour a la liste"), tzS, with_alpha(0xFF7C8594u, A), with_alpha(0xA0000000u, A), k.ow);
    (void)x1;
    dSetTex(dev, 0, 0); cs(dev);
    if (L.focused) { g_hit[16][0] = bx / f.screenW; g_hit[16][1] = by / f.screenH; g_hit[16][2] = bw / f.screenW; g_hit[16][3] = bh / f.screenH; }
}
// ======== KEY ITEMS (evitem as data : native_key_items) -- the categories with their tallies, a category's whole list ========
// The header : KEY ITEMS / the category (its colour), how many owned, how many new. The categories : a folder in its
// colour, owned / total with a bar (green when complete), a gold "n new" chip. A category's items : a bullet, a glowing
// gold dot + "New" for one never examined. Under the list : the cursor item's description (the DAT) ; the description
// page (state 3) shows it in full. The client keeps the keys ; the mouse hovers / picks a row of its page.
namespace { Block g_kiB; unsigned g_kiMs = 0; float g_kiCurY = -1.0f; int g_kiTop = 0; }
static u32 ki_colour(int c) {
    static const u32 C[12] = { 0xFFF09A50u, 0xFFE8C060u, 0xFF68D0D8u, 0xFFA88CF0u, 0xFF84DCA8u, 0xFFD8A070u, 0xFF9CD070u, 0xFF6AA8F0u, 0xFFB4BECCu, 0xFFF080B0u, 0xFFE8706Au, 0xFF7CCF7Au };
    return c >= 0 ? C[c % 12] : 0xFFE8C060u;
}
void key_items_draw(const Frame& f) {
    const UiConfig& C = ui_config();
    if (!(C.nuHide & NU_MAIN) || !f.font || f.screenH <= 0.0f || f.screenW <= 0.0f) { g_kiB.vis = 0.0f; g_kiB.wasOpen = false; return; }
    const unsigned now = GetTickCount();
    float dt = g_kiMs ? (float)(now - g_kiMs) / 1000.0f : 0.0f; g_kiMs = now; if (dt > 0.1f) dt = 0.1f;
    const NativeKeyItems& K0 = native_key_items();
    static NativeKeyItems L; const bool up = K0.open && K0.ok;
    if (up) L = K0;
    if (!block_ease(g_kiB, up, now, dt)) { if (!K0.open) { g_kiCurY = -1.0f; g_kiTop = 0; } return; }
    const Metrics k = metrics(f); const float S = k.S; const u32 dev = f.dev; Font* fo = f.font; const MouseState* ms = f.mouse;
    const float tz = snap(k.tz * 1.04f), tzS = snap(k.tz * 0.80f), tzK = snap(k.tz * 0.74f), tzT = snap(k.tz * 1.30f);
    const float pad = snap(18.0f * S) + k.bi, rowH = snap(k.capH * 1.85f);
    const bool cats = L.state == 1, page = L.state == 3;
    const u32 acc = cats ? 0xFFE8C060u : ki_colour(L.catColour);
    char title[64], subt[96] = "";
    if (cats) { lstrcpynA(title, tr("Key Items", "Objets cles"), sizeof(title)); _snprintf(subt, sizeof(subt), tr("%d owned of %d", "%d possedes sur %d"), L.ownedAll, L.totalAll); subt[95] = 0; }
    else { lstrcpynA(title, L.title[0] ? L.title : tr("Key Items", "Objets cles"), sizeof(title)); _snprintf(subt, sizeof(subt), "%s  >  %s", tr("Key Items", "Objets cles"), title); subt[95] = 0; }
    // ---- the viewport
    const int maxRows = (int)fmaxf(5.0f, fminf(13.0f, (f.screenH * 0.55f) / rowH));
    if (!L.count) g_kiTop = 0;
    if (L.cursor < g_kiTop + 1) g_kiTop = L.cursor - 1; if (L.cursor > g_kiTop + maxRows - 2) g_kiTop = L.cursor - maxRows + 2;
    if (g_kiTop > L.count - maxRows) g_kiTop = L.count - maxRows; if (g_kiTop < 0) g_kiTop = 0;
    const int nVis = L.count < maxRows ? L.count : maxRows;
    const float listH = (float)(nVis ? nVis : 1) * rowH;
    // ---- the description : under the list (3 lines), or the page in full
    const NativeKeyItems::Row* CR = L.count ? &L.row[L.cursor] : 0;
    const bool detail = !cats && CR && CR->name[0];
    const float bw = keep_width(g_kiB, fmaxf(snap(500.0f * S), fminf(f.screenW * 0.42f, snap(560.0f * S))));
    static char dl[16][120]; int nDl = 0;
    if (detail) { const char* p = L.desc; const int maxL = page ? 16 : 4;
        while (*p && nDl < maxL) { char para[700]; int n = 0; while (p[n] && p[n] != '\n' && n < 699) { para[n] = p[n]; ++n; } para[n] = 0;
            if (n) nDl += wrap_lines(fo, para, tzS, bw - 2.0f * pad - 4.0f * S, dl + nDl, maxL - nDl);
            p += n; if (*p == '\n') ++p; } }
    const float headH = snap(tzK * 1.5f + tzT * 1.6f + tzS * 1.4f + 14.0f * S);
    const float detH = detail ? snap(12.0f * S + tzS * 1.6f + tzS * 1.5f * (float)(nDl ? nDl : 1) + 12.0f * S) : 0.0f;
    const float footH = L.count > nVis ? snap(tzS * 2.2f) : 0.0f;
    const float bh = snap(headH + pad * 0.6f + listH + footH + (detail ? snap(12.0f * S) + detH : 0.0f) + pad);
    // ---- place : the client's window's corner (16,48 of its UI)
    const float sx = L.uiW > 0 ? f.screenW / (float)L.uiW : 1.0f, sy = L.uiH > 0 ? f.screenH / (float)L.uiH : 1.0f;
    float bx = snap((float)L.rx0 * sx), by = snap((float)L.ry0 * sy);
    if (bx + bw > f.screenW - 4.0f) bx = snap(f.screenW - 4.0f - bw); if (bx < 4.0f) bx = 4.0f;
    if (by + bh > f.screenH - 4.0f) by = snap(f.screenH - 4.0f - bh); if (by < 4.0f) by = 4.0f;
    const int jm = menu_place(f, __LINE__, bx, by, bw, bh); g_frameJoin = jm;
    const float tO = ease_out(((float)(now - g_kiB.openMs) / 1000.0f) / 0.09f);
    by = snap(by + (1.0f - tO) * 4.0f * S);
    const float A = g_kiB.vis * tO; if (A <= 0.0f) return;
    draw_themed_box(dev, f.skin, bx, by, bw, bh, C.muBox, A, S);
    const float x0 = bx + pad, x1 = bx + bw - pad;
    // ---- the header
    rrect_topcaps(dev, bx + k.bi, by + k.bi, bw - 2.0f * k.bi, headH - k.bi, snap(6.0f * S), with_alpha(with_a(acc, 0x50), A), with_alpha(with_a(acc, 0x0A), A));
    {   char up[48]; lstrcpynA(up, tr("Key Items", "Objets cles"), sizeof(up)); CharUpperA(up);
        float ky = by + snap(10.0f * S);
        fo->begin(dev);
        fo->draw_lc(dev, snap(x0), snap(ky + tzK * 0.7f), up, tzK, with_alpha(acc, A), with_alpha(0xA0000000u, A), k.ow); ky += tzK * 1.5f;
        fo->draw_lc(dev, snap(x0), snap(ky + tzT * 0.8f), title, tzT, with_alpha(0xFFF4E8C8u, A), with_alpha(0xC8000000u, A), k.ow); ky += tzT * 1.6f;
        fo->draw_lc(dev, snap(x0), snap(ky + tzS * 0.5f), subt, tzS, with_alpha(0xFFB4BECCu, A), with_alpha(0xA0000000u, A), k.ow);
        // the right : how many (a category : its list's size) ; the new ones (gold)
        float rx = x1; const float ch = snap(tzS * 1.8f), cy = snap(by + snap(10.0f * S));
        auto pill = [&](const char* t, u32 col) { const float cw = fo->measure(t, tzS) + 20.0f * S, cx = snap(rx - cw);
            rrect(dev, cx, cy, cw, ch, ch * 0.5f, with_alpha(with_a(col, 0x3C), A), with_alpha(with_a(col, 0x22), A));
            rrect_stroke(dev, cx, cy, cw, ch, ch * 0.5f, with_alpha(with_a(col, 0x80), A), 1.0f);
            fo->begin(dev); fo->draw_lc(dev, snap(cx + 10.0f * S), snap(cy + ch * 0.5f), t, tzS, with_alpha(0xFFF4F6FAu, A), with_alpha(0xA0000000u, A), k.ow); rx = cx - 6.0f * S; };
        char c[32];
        if (cats) { sprintf(c, "%d", L.ownedAll); pill(c, acc); if (L.newAll > 0) { sprintf(c, tr("%d new", "%d nouveaux"), L.newAll); pill(c, 0xFFFFDA8Au); } }
        else { int nn = 0; for (int i = 0; i < L.count; ++i) if (L.row[i].isNew) ++nn;
            sprintf(c, "%d", L.count); pill(c, acc); if (nn) { sprintf(c, tr("%d new", "%d nouveaux"), nn); pill(c, 0xFFFFDA8Au); } }
        rrect(dev, bx + k.bi, snap(by + headH - 1.0f), bw - 2.0f * k.bi, 1.5f, 0.0f, with_alpha(with_a(acc, 0x90), A), with_alpha(with_a(acc, 0x90), A));
    }
    float y = by + headH + pad * 0.6f;
    int mrow = -1;
    if (!L.count) { fo->begin(dev); fo->draw_lc(dev, snap(x0 + 4.0f * S), snap(y + rowH * 0.5f), tr("You do not possess any key items.", "Tu ne possedes aucun objet cle."), tz, with_alpha(0xFF7C8594u, A), with_alpha(0xA0000000u, A), k.ow); }
    else {
        // the cursor : a gold capsule gliding to its row
        if (L.cursor >= g_kiTop && L.cursor < g_kiTop + nVis && L.focused) {
            const float ty = y + (float)(L.cursor - g_kiTop) * rowH;
            g_kiCurY = g_kiCurY < 0.0f ? ty : g_kiCurY + (ty - g_kiCurY) * fminf(1.0f, dt * 18.0f);
            const float cx = snap(x0 - 8.0f * S), cw = (x1 - x0) + 16.0f * S;
            rrect(dev, cx, snap(g_kiCurY + 1.0f * S), cw, rowH - 2.0f * S, snap(6.0f * S), with_alpha(0x50FFDA8Au, A), with_alpha(0x2EFFDA8Au, A));
            rrect_stroke(dev, cx, snap(g_kiCurY + 1.0f * S), cw, rowH - 2.0f * S, snap(6.0f * S), with_alpha(0xC8FFDA8Au, A), 1.5f * S); }
        for (int v = 0; v < nVis; ++v) {
            const int r = g_kiTop + v; const NativeKeyItems::Row& R = L.row[r]; const float ry = y + (float)v * rowH, cy = snap(ry + rowH * 0.5f), ix = snap(x0 + 8.0f * S);
            const bool on = r == L.cursor && L.focused;
            if (!on && (v & 1)) rrect(dev, snap(x0 - 8.0f * S), snap(ry), (x1 - x0) + 16.0f * S, rowH, snap(4.0f * S), with_alpha(0x0CFFFFFFu, A), with_alpha(0x0CFFFFFFu, A));
            const bool full = R.category && R.total > 0 && R.owned >= R.total;
            const u32 cc = R.category ? (full ? 0xFF9CE27Au : ki_colour(R.cat)) : acc;
            if (R.category) {                                             // a folder in its category's colour
                rrect(dev, ix - 6.0f * S, cy - 5.0f * S, 12.0f * S, 10.0f * S, 2.0f * S, with_alpha(with_a(cc, 0xB0), A), with_alpha(with_a(cc, 0x80), A));
                rrect(dev, ix - 6.0f * S, cy - 7.0f * S, 6.0f * S, 3.0f * S, 1.0f * S, with_alpha(with_a(cc, 0xB0), A), with_alpha(with_a(cc, 0xB0), A));
            } else if (R.isNew) { disc_glow(dev, ix, cy, 4.0f * S, with_alpha(0x90FFDA8Au, A), 7.0f * S); disc(dev, ix, cy, 4.0f * S, with_alpha(0xFFFFDA8Au, A)); }
            else disc(dev, ix, cy, 2.8f * S, with_alpha(with_a(acc, 0xD0), A));
            const u32 lc = on ? 0xFFFFFFFFu : R.isNew ? 0xFFFFE9B0u : full ? 0xFFC8F0B4u : 0xFFE6ECF4u;
            const float nx = snap(x0 + 26.0f * S);
            // the right : a category's owned / total + bar, its new ; an item's "New"
            float rx = x1;
            auto chip = [&](const char* t, u32 col) { const float w = fo->measure(t, tzK) + 12.0f * S, hh = snap(tzK * 1.6f), px = snap(rx - w);
                rrect(dev, px, snap(cy - hh * 0.5f), w, hh, hh * 0.5f, with_alpha(with_a(col, 0x30), A), with_alpha(with_a(col, 0x1C), A));
                fo->begin(dev); fo->draw_lc(dev, snap(px + 6.0f * S), cy, t, tzK, with_alpha(col, A), with_alpha(0xA0000000u, A), k.ow); rx = px - 6.0f * S; };
            char t[32];
            if (R.category) {
                const float a = 3.5f * S; tri(dev, x1, cy, x1 - a * 1.6f, cy - a, x1 - a * 1.6f, cy + a, with_alpha(0xFF8A96A8u, A)); rx = x1 - 12.0f * S;
                sprintf(t, "%d/%d", R.owned, R.total); chip(t, cc);
                const float mw = snap(54.0f * S), mh = snap(4.0f * S), mx = snap(rx - mw), my = snap(cy - mh * 0.5f);
                rrect(dev, mx, my, mw, mh, mh * 0.5f, with_alpha(0x70101826u, A), with_alpha(0x70080C14u, A));
                const float fw = R.total > 0 ? snap(mw * fminf(1.0f, (float)R.owned / (float)R.total)) : 0.0f;
                if (fw > 1.0f) rrect(dev, mx, my, fw, mh, mh * 0.5f, with_alpha(with_a(cc, 0xF0), A), with_alpha(with_a(cc, 0xB0), A));
                rx = mx - 8.0f * S;
                if (R.fresh > 0) { sprintf(t, tr("%d new", "%d nouv."), R.fresh); chip(t, 0xFFFFDA8Au); }
            } else if (R.isNew) chip(tr("New", "Nouveau"), 0xFFFFDA8Au);
            { char cut[80]; fo->begin(dev); fo->draw_lc(dev, nx, cy, fit_text(fo, R.name[0] ? R.name : "...", tz, fmaxf(40.0f, rx - nx - 6.0f * S), cut, sizeof(cut)), tz, with_alpha(lc, A), with_alpha(0xC8000000u, A), k.ow); }
            if (ms && ms->focused && ms->x >= x0 - 8.0f * S && ms->x < x1 + 8.0f * S && ms->y >= ry && ms->y < ry + rowH) mrow = r;
        }
    }
    y += listH;
    if (L.count > nVis) {                                                 // the footer : where, how many ; the arrows
        const int last = g_kiTop + nVis; char ft[48]; sprintf(ft, "%d-%d / %d", g_kiTop + 1, last, L.count);
        const float fy = snap(y + footH * 0.5f), fw = fo->measure(ft, tzS);
        fo->begin(dev); fo->draw_lc(dev, snap(x1 - fw), fy, ft, tzS, with_alpha(0xFF9AA6B8u, A), with_alpha(0xA0000000u, A), k.ow);
        const float ax = snap(x1 - fw - 16.0f * S), aa = 3.5f * S;
        tri(dev, ax, fy - aa * 2.0f, ax - aa, fy - aa * 0.6f, ax + aa, fy - aa * 0.6f, g_kiTop > 0 ? with_alpha(acc, A) : with_alpha(0x40FFFFFFu, A));
        tri(dev, ax, fy + aa * 2.0f, ax - aa, fy + aa * 0.6f, ax + aa, fy + aa * 0.6f, last < L.count ? with_alpha(acc, A) : with_alpha(0x40FFFFFFu, A));
        y += footH;
    }
    if (detail && !page) {                                                // the cursor item : its name, its description
        y += snap(12.0f * S);
        rrect(dev, snap(x0 - 6.0f * S), snap(y), (x1 - x0) + 12.0f * S, detH, snap(7.0f * S), with_alpha(with_a(acc, 0x22), A), with_alpha(0x50101624u, A));
        rrect_stroke(dev, snap(x0 - 6.0f * S), snap(y), (x1 - x0) + 12.0f * S, detH, snap(7.0f * S), with_alpha(with_a(acc, 0x60), A), 1.0f);
        float dy = y + 10.0f * S;
        fo->begin(dev); fo->draw_lc(dev, snap(x0 + 2.0f * S), snap(dy + tzS * 0.6f), CR->name, tzS, with_alpha(acc, A), with_alpha(0xA0000000u, A), k.ow); dy += tzS * 1.6f;
        if (!nDl) fo->draw_lc(dev, snap(x0 + 2.0f * S), snap(dy + tzS * 0.6f), tr("No description.", "Aucune description."), tzS, with_alpha(0xFF7C8594u, A), with_alpha(0xA0000000u, A), k.ow);
        for (int i = 0; i < nDl; ++i) { fo->draw_lc(dev, snap(x0 + 2.0f * S), snap(dy + tzS * 0.6f), dl[i], tzS, with_alpha(0xFFDCE2EAu, A), with_alpha(0xA0000000u, A), k.ow); dy += tzS * 1.5f; }
    }
    // ---- the description PAGE (state 3, the client waits for Cancel) : a card over the list -- the list is not live
    // under it (2026-11-22 : "je peux pas naviguer tant que je ne fais pas cancel"), so it says so
    if (page && CR) {
        const float ay0 = by + headH, ah = bh - headH - k.bi;
        rrect(dev, bx + k.bi, ay0, bw - 2.0f * k.bi, ah, snap(6.0f * S), with_alpha(0xA0080C14u, A), with_alpha(0xA0080C14u, A));
        const float cx = snap(bx + pad * 0.6f), cw = snap(bw - 1.2f * pad), cy = snap(ay0 + pad * 0.5f), ch = snap(ah - pad);
        rrect(dev, cx, cy, cw, ch, snap(9.0f * S), with_alpha(with_a(acc, 0x38), A), with_alpha(0xF0141C2Au, A));
        rrect_stroke(dev, cx, cy, cw, ch, snap(9.0f * S), with_alpha(with_a(acc, 0xB0), A), 1.5f * S);
        float py = cy + 14.0f * S;
        { char cut[80]; fo->begin(dev); fo->draw_lc(dev, snap(cx + 16.0f * S), snap(py + tzT * 0.6f), fit_text(fo, CR->name, tzT, cw - 32.0f * S, cut, sizeof(cut)), tzT, with_alpha(0xFFF4E8C8u, A), with_alpha(0xC8000000u, A), k.ow); }
        py += tzT * 1.5f;
        rrect(dev, snap(cx + 16.0f * S), snap(py), cw - 32.0f * S, 1.0f, 0.0f, with_alpha(with_a(acc, 0x80), A), with_alpha(with_a(acc, 0x80), A)); py += 10.0f * S;
        fo->begin(dev);
        if (!nDl) fo->draw_lc(dev, snap(cx + 16.0f * S), snap(py + tz * 0.6f), tr("No description.", "Aucune description."), tz, with_alpha(0xFF7C8594u, A), with_alpha(0xA0000000u, A), k.ow);
        for (int i = 0; i < nDl && py < cy + ch - tz * 2.6f; ++i) { fo->draw_lc(dev, snap(cx + 16.0f * S), snap(py + tz * 0.6f), dl[i], tzS * 1.08f, with_alpha(0xFFDCE2EAu, A), with_alpha(0xA0000000u, A), k.ow); py += tzS * 1.6f; }
        const char* hint = tr("Cancel : back to the list", "Annuler : retour a la liste");
        fo->draw_lc(dev, snap(cx + cw - 16.0f * S - fo->measure(hint, tzS)), snap(cy + ch - 12.0f * S - tzS * 0.5f), hint, tzS, with_alpha(0xFF9AA6B8u, A), with_alpha(0xA0000000u, A), k.ow);
    }
    // ---- the question on an item never examined (evitem01) : a card over the list, Yes / No (the client's cursor)
    int askRow = 0;
    if (L.ask) {
        rrect(dev, bx + k.bi, by + headH, bw - 2.0f * k.bi, bh - headH - k.bi, snap(6.0f * S), with_alpha(0x90080C14u, A), with_alpha(0x90080C14u, A));
        const float cw = snap(fminf(bw - 2.0f * pad, 380.0f * S)), bH = snap(tz * 2.0f), ch = snap(tz * 2.2f + 16.0f * S + bH + 18.0f * S);
        const float cx = snap(bx + (bw - cw) * 0.5f), cy = snap(by + headH + (bh - headH - ch) * 0.45f);
        rrect(dev, cx, cy, cw, ch, snap(9.0f * S), with_alpha(with_a(acc, 0x40), A), with_alpha(0xF0141C2Au, A));
        rrect_stroke(dev, cx, cy, cw, ch, snap(9.0f * S), with_alpha(with_a(acc, 0xB0), A), 1.5f * S);
        { char cut[120]; fo->begin(dev); const char* q = fit_text(fo, L.askText, tz, cw - 24.0f * S, cut, sizeof(cut));
          fo->draw_lc(dev, snap(cx + (cw - fo->measure(q, tz)) * 0.5f), snap(cy + 14.0f * S + tz * 0.6f), q, tz, with_alpha(0xFFF4E8C8u, A), with_alpha(0xC8000000u, A), k.ow); }
        const float gap = snap(12.0f * S), btW = snap((cw - 3.0f * gap) * 0.5f), btY = snap(cy + ch - 14.0f * S - bH);
        for (int b = 1; b <= 2; ++b) {
            const float btX = snap(cx + gap + (float)(b - 1) * (btW + gap)); const bool on = L.askCursor == b;
            const u32 bc = b == 1 ? 0xFF9CE27Au : 0xFFB4BECCu;
            rrect(dev, btX, btY, btW, bH, snap(6.0f * S), with_alpha(with_a(on ? 0xFFFFDA8Au : bc, on ? 0x50 : 0x24), A), with_alpha(with_a(on ? 0xFFFFDA8Au : bc, on ? 0x30 : 0x12), A));
            rrect_stroke(dev, btX, btY, btW, bH, snap(6.0f * S), with_alpha(on ? 0xD0FFDA8Au : with_a(bc, 0x70), A), on ? 1.5f * S : 1.0f);
            const char* t = b == 1 ? tr("Yes", "Oui") : tr("No", "Non");
            fo->begin(dev); fo->draw_lc(dev, snap(btX + (btW - fo->measure(t, tz)) * 0.5f), snap(btY + bH * 0.5f), t, tz, with_alpha(on ? 0xFFFFFFFFu : 0xFFE6ECF4u, A), with_alpha(0xC8000000u, A), k.ow);
            if (ms && ms->focused && ms->x >= btX && ms->x < btX + btW && ms->y >= btY && ms->y < btY + bH) askRow = b;
        }
    }
    dSetTex(dev, 0, 0); cs(dev);
    if (K0.open && L.ask && L.askFocused) {                               // the question's mouse : Yes / No
        g_hit[16][0] = bx / f.screenW; g_hit[16][1] = by / f.screenH; g_hit[16][2] = bw / f.screenW; g_hit[16][3] = bh / f.screenH;
        static float mq[2]; static int lastAsk = 0; const bool moved = mouse_moved(ms, mq);
        if (askRow && moved && askRow != lastAsk && askRow != L.askCursor) native_key_ask_hover(askRow);
        lastAsk = askRow;
        if (askRow && ms && ms->clicked) native_key_ask_pick(askRow);
        return;
    }
    // ---- the mouse : a row of the client's page (entries 1.. = the page's rows top..)
    if (K0.open && L.focused && !page) {
        g_hit[16][0] = bx / f.screenW; g_hit[16][1] = by / f.screenH; g_hit[16][2] = bw / f.screenW; g_hit[16][3] = bh / f.screenH;
        const int ent = (mrow >= L.top && mrow < L.top + L.vis) ? mrow - L.top + 1 : 0;
        static float mv[2]; const bool moved = mouse_moved(ms, mv);
        if (ent && moved && mrow != g_kiB.hoverRow && mrow != L.cursor) native_key_hover(ent);
        g_kiB.hoverRow = mrow;
        if (ent && ms && ms->clicked) native_key_pick(ent);
    } else g_kiB.hoverRow = -1;
}
// ======== the DELIVERY BOX (Game UI -> NU_NPC) : receive (post1 + post2) / send (delivery + stringdl) ========
// The 8 slots in the client's grid (4 x 2, entries 1..8) and colours (receive : shown in states 6, 7, 0xA..0xE, usable in 7,
// 0xB, 0xD ; send : white 1, 5, 9, 0x10, grey 2, 3, 4, 8, 0xE, 0xF), the gil line (receive 9, send 11), Send / Cancel (send 9 /
// 10) ; under the box : the sender and Take / Dispose / Return, or the recipient and Name / OK / Cancel ; the inventory beside.
namespace { Block g_dvB, g_dsB, g_diB; unsigned g_dvMs = 0; }
void dlv_draw(const Frame& f) {
    const UiConfig& C = ui_config();
    if (!(C.nuHide & NU_NPC) || !f.font || f.screenH <= 0.0f || f.screenW <= 0.0f) { g_dvB.vis = g_dsB.vis = g_diB.vis = 0.0f; g_dvB.wasOpen = g_dsB.wasOpen = g_diB.wasOpen = false; return; }
    const unsigned now = GetTickCount();
    float dt = g_dvMs ? (float)(now - g_dvMs) / 1000.0f : 0.0f; g_dvMs = now; if (dt > 0.1f) dt = 0.1f;
    const NativeDelivery& D0 = native_delivery();
    static NativeDelivery last;
    if (D0.box) last = D0;
    if (!block_ease(g_dvB, D0.box != 0, now, dt) || last.uiW <= 0 || !last.box) { g_dsB.vis = g_diB.vis = 0.0f; return; }
    const bool recv = last.box == 1;
    const Metrics k = metrics(f);
    const float S = k.S, pad = snap(8.0f * S) + k.bi, tz = k.tz, headH = snap(18.0f * S), gap = snap(6.0f * S);
    Font* fo = f.font; const u32 dev = f.dev;
    const float tO = ease_out(((float)(now - g_dvB.openMs) / 1000.0f) / 0.08f);
    const float A = g_dvB.vis * tO * (last.focused || !D0.box ? 1.0f : 0.85f); if (A <= 0.0f) return;
    const u32 sh = with_alpha(0xC8000000u, A);
    const float cell = snap(k.capH * 2.1f), cg = snap(4.0f * S), gridW = 4.0f * cell + 3.0f * cg, lineH = snap(k.capH * 1.4f), btnH = snap(k.capH * 1.5f);
    const float bw = snap(pad + gridW + pad), bh = snap(pad + headH + 6.0f * S + lineH + 4.0f * S + 2.0f * cell + cg + (recv ? 0.0f : 8.0f * S + btnH) + pad);
    const float sx = f.screenW / (float)last.uiW, sy = f.screenH / (float)last.uiH;
    float bx = snap((float)last.rx0 * sx), by = snap((float)last.ry0 * sy);
    if (bx + bw > f.screenW - 4.0f) bx = snap(f.screenW - 4.0f - bw); if (bx < 4.0f) bx = 4.0f;
    if (by + bh > f.screenH - 4.0f) by = snap(f.screenH - 4.0f - bh); if (by < 4.0f) by = 4.0f;
    const int jm = menu_place(f, __LINE__, bx, by, bw, bh);
    by = snap(by + (1.0f - tO) * 4.0f * S);
    g_frameJoin = jm;
    draw_themed_box(dev, f.skin, bx, by, bw, bh, C.muBox, A, S);
    const float L = bx + pad, hy = snap(by + pad + headH * 0.5f), gly = snap(by + pad + headH + 6.0f * S), gy = snap(gly + lineH + 4.0f * S);
    rrect(dev, L, snap(by + pad + headH), gridW, 1.0f, 0.0f, with_alpha(0x30C8D6EAu, A), with_alpha(0x30C8D6EAu, A));
    auto shown = [&](unsigned st) { return recv ? (st == 6 || st == 7 || (st >= 0xA && st <= 0xE)) : (st == 1 || st == 5 || st == 9 || st == 0x10 || st == 2 || st == 3 || st == 4 || st == 8 || st == 0xE || st == 0xF); };
    auto bright = [&](unsigned st) { return recv ? (st == 7 || st == 0xB || st == 0xD) : (st == 1 || st == 5 || st == 9 || st == 0x10); };
    unsigned gilSum = 0; for (int i = 0; i < 8; ++i) if (last.slot[i].item == 0xFFFF && shown(last.slot[i].state)) gilSum += last.slot[i].count;
    const int gilEnt = recv ? 9 : 11;
    const MouseState* ms = f.mouse; int ment = 0;
    // the gil line (an entry)
    {   const bool on = last.cursor == gilEnt && last.focused;
        if (on) { rrect_clip_begin(dev, L, gly, gridW, lineH, lineH * 0.5f); party_selframe(dev, L, gly, gridW, lineH, f.t, A, 0); rrect_clip_end(dev); rrect_stroke(dev, L, gly, gridW, lineH, lineH * 0.5f, with_alpha(0xD8FFDA8Au, A), 1.5f * S); }
        gil_coin(dev, snap(L + 6.0f * S + k.capH * 0.5f), snap(gly + lineH * 0.5f), snap(k.capH), A);
        if (ms && ms->focused && ms->x >= L && ms->x < L + gridW && ms->y >= gly && ms->y < gly + lineH) ment = gilEnt;
    }
    for (int i = 0; i < 8; ++i) {
        const float cx = snap(L + (float)(i % 4) * (cell + cg)), cy = snap(gy + (float)(i / 4) * (cell + cg));
        rrect(dev, cx, cy, cell, cell, 5.0f * S, with_alpha(0x70101826u, A), with_alpha(0x70080C14u, A));
        rrect_stroke(dev, cx, cy, cell, cell, 5.0f * S, with_alpha(0x30C8D6EAu, A), 1.0f);
        if (last.cursor == i + 1 && last.focused) {
            rrect_clip_begin(dev, cx, cy, cell, cell, 5.0f * S); party_selframe(dev, cx, cy, cell, cell, f.t, A, 0); rrect_clip_end(dev);
            rrect_stroke(dev, cx, cy, cell, cell, 5.0f * S, with_alpha(0xD8FFDA8Au, A), 1.5f * S);
        }
        const NativeDlvSlot& w = last.slot[i];
        if (w.item && shown(w.state)) {
            const float a = A * (bright(w.state) ? 1.0f : 0.45f), z = snap(cell - 8.0f * S);
            if (w.item == 0xFFFF) gil_coin(dev, snap(cx + cell * 0.5f), snap(cy + cell * 0.5f), z * 0.8f, a);
            else { const u32 tex = list_icon(f, 3, w.item); if (tex) { dTexQuadState(dev, tex); tquad(dev, snap(cx + 4.0f * S), snap(cy + 4.0f * S), z, z, 0.0f, 1.0f, 0.0f, 1.0f, with_alpha(0xFFFFFFFFu, a), with_alpha(0xFFFFFFFFu, a)); dSetTex(dev, 0, 0); cs(dev); } }
        }
        if (ms && ms->focused && ms->x >= cx && ms->x < cx + cell && ms->y >= cy && ms->y < cy + cell) ment = i + 1;
    }
    // Send / Cancel (the send box)
    const float btY = snap(gy + 2.0f * cell + cg + 8.0f * S), btW = snap((gridW - gap) * 0.5f);
    if (!recv) for (int b = 0; b < 2; ++b) {
        const float x = snap(L + (float)b * (btW + gap)); const bool on = last.cursor == 9 + b && last.focused;
        rrect(dev, x, btY, btW, btnH, btnH * 0.5f, with_alpha(on ? 0x40FFDA8Au : 0x20FFFFFFu, A), with_alpha(on ? 0x30FFDA8Au : 0x14FFFFFFu, A));
        rrect_stroke(dev, x, btY, btW, btnH, btnH * 0.5f, with_alpha(on ? 0xD8FFDA8Au : 0x40C8D6EAu, A), on ? 1.5f * S : 1.0f);
        if (ms && ms->focused && ms->x >= x && ms->x < x + btW && ms->y >= btY && ms->y < btY + btnH) ment = 9 + b;
    }
    if (D0.box) {
        g_hit[17][0] = bx / f.screenW; g_hit[17][1] = by / f.screenH; g_hit[17][2] = bw / f.screenW; g_hit[17][3] = bh / f.screenH;
        if (D0.focused) {
            if (ment && ment != g_dvB.hoverRow && ment != D0.cursor) native_dlv_hover(ment);
            g_dvB.hoverRow = ment;
            if (ment && ms->clicked) native_dlv_pick(ment);
        } else g_dvB.hoverRow = 0;
    }
    fo->begin(dev);
    fo->draw_lc(dev, L, hy, recv ? tr("Delivery box", "Bo\xC3\xAEte de r\xC3\xA9""ception") : tr("Send", "Envoi"), tz * 0.95f, with_alpha(0xFFE8D9B0u, A), sh, k.ow);
    if (!recv && last.recipient[0]) { char t[48]; _snprintf(t, sizeof(t), "%s %s", tr("To", "\xC3\x80"), last.recipient); t[47] = 0; fo->draw_lc(dev, snap(L + gridW - fo->measure(t, tz * 0.85f)), hy, t, tz * 0.85f, with_alpha(0xFF93A6BEu, A), sh, k.ow); }
    if (last.gilUp && !recv) {                                        // the gil to send : the client's value, its edited digit (its keys)
        const float vz = tz * 1.05f, vx = snap(L + 12.0f * S + k.capH), gyy = snap(gly + lineH * 0.5f);
        rrect(dev, snap(vx - 4.0f * S), gly, snap(L + gridW - vx + 4.0f * S), lineH, 4.0f * S, with_alpha(0x30FFDA8Au, A), with_alpha(0x18FFDA8Au, A));
        rrect_stroke(dev, snap(vx - 4.0f * S), gly, snap(L + gridW - vx + 4.0f * S), lineH, 4.0f * S, with_alpha(0xC0FFDA8Au, A), 1.0f * S);
        int pos = 0; for (unsigned d = last.gilDigit; d >= 10 && pos < 9; d /= 10) ++pos;
        char raw[16]; sprintf(raw, "%u", last.gilVal); const int nd = (int)strlen(raw);
        char dig[16]; const int want = nd > pos + 1 ? nd : pos + 1;
        for (int i = 0; i < want - nd; ++i) dig[i] = '0'; memcpy(dig + (want - nd), raw, (size_t)nd + 1);
        char v[28]; int ci = -1; { int o = 0; for (int i = 0; i < want; ++i) { if (i && (want - i) % 3 == 0) v[o++] = ','; if (want - 1 - i == pos) ci = o; v[o++] = dig[i]; } v[o] = 0; }
        if (ci >= 0) { char pre[28]; lstrcpynA(pre, v, ci + 1); const float ux = fo->measure(pre, vz), cw = fo->measure("0", vz);
            rrect(dev, snap(vx + ux - 1.0f * S), snap(gyy - vz * 0.6f), snap(cw + 2.0f * S), snap(vz * 1.2f), 2.0f * S, with_alpha(0x60E86A5Au, A), with_alpha(0x60E86A5Au, A)); }
        fo->begin(dev); fo->draw_lc(dev, vx, gyy, v, vz, with_alpha(0xFFFFFFFFu, A), sh, k.ow);
        char mx[32], mt[40]; price_text(mx, (int)(last.gilMax > 999999999u ? 999999999u : last.gilMax)); _snprintf(mt, sizeof(mt), "max %s", mx); mt[39] = 0;
        fo->draw_lc(dev, snap(L + gridW - 6.0f * S - fo->measure(mt, tz * 0.75f)), gyy, mt, tz * 0.75f, with_alpha(0xFF93A6BEu, A), sh, k.ow);
    } else {
        char g[24], t[40]; price_text(g, (int)(gilSum > 999999999u ? 999999999u : gilSum)); _snprintf(t, sizeof(t), "%s G", g); t[39] = 0;
        fo->draw_lc(dev, snap(L + 12.0f * S + k.capH), snap(gly + lineH * 0.5f), t, tz, with_alpha(gilSum ? 0xFFFFDA8Au : 0xFF6E7C8Eu, A), sh, k.ow); }
    for (int i = 0; i < 8; ++i) {
        const NativeDlvSlot& w = last.slot[i];
        if (!w.item || w.item == 0xFFFF || !shown(w.state) || w.count <= 1) continue;
        const float cx = snap(L + (float)(i % 4) * (cell + cg)), cy = snap(gy + (float)(i / 4) * (cell + cg));
        char n[12]; sprintf(n, "%u", w.count > 9999 ? 9999 : w.count);
        fo->draw_lc(dev, snap(cx + cell - 3.0f * S - fo->measure(n, tz * 0.8f)), snap(cy + cell - 7.0f * S), n, tz * 0.8f, with_alpha(0xFFF2F5FAu, A), sh, k.ow);
    }
    if (!recv) for (int b = 0; b < 2; ++b) {
        const char* t = b ? tr("Cancel", "Annuler") : tr("Send", "Envoyer");
        fo->draw_lc(dev, snap(L + (float)b * (btW + gap) + (btW - fo->measure(t, tz)) * 0.5f), snap(btY + btnH * 0.5f), t, tz, with_alpha(0xFFF2F5FAu, A), sh, k.ow);
    }
    // ---- under it : the count (a stack placed), then the sender + Take / Dispose / Return, or the recipient + Name / OK / Cancel ----
    float under = snap(by + bh + gap);
    if (last.qtyUp && D0.box && D0.qtyUp) {
        const int ic = last.invCursor - last.invTop; const unsigned qid = ic >= 0 && ic < last.invVis ? last.invRow[ic].itemId : 0;
        under = snap(under + qty_box(f, k, qid, last.qtyVal, last.qtyMax, bx, under, bw, A) + gap);
    }
    if (block_ease(g_dsB, D0.box && D0.subOpen, now, dt)) {
        RowSpec rows[3] = {}; char nameRow[48];
        const int sc = last.cursor >= 1 && last.cursor <= 8 ? last.cursor - 1 : -1;
        if (recv) {
            rows[0].label = tr("Take", "Prendre"); rows[1].label = tr("Dispose", "Jeter"); rows[2].label = tr("Return to sender", "Renvoyer");
        } else {
            char lv[0x170] = ""; unsigned lc[0x170]; int lcar = 0;
            if (last.liveN > 0) { const int n = chat_input_decode(last.live, last.liveN, lv, lc, sizeof(lv), &lcar); lv[n < (int)sizeof(lv) ? n : (int)sizeof(lv) - 1] = 0; }
            _snprintf(nameRow, sizeof(nameRow), "%s : %s", tr("Name", "Nom"), lv[0] ? lv : last.typed[0] ? last.typed : last.recipient[0] ? last.recipient : "-"); nameRow[47] = 0;
            rows[0].label = nameRow; rows[1].label = "OK"; rows[2].label = tr("Cancel", "Annuler");
        }
        for (int i = 0; i < 3; ++i) { rows[i].icon = IC_NONE; rows[i].off = last.subGrey[i + 1] != 0; }
        float ow, oh; block_size(f, k, rows, 3, ow, oh); if (ow < bw) ow = bw;
        const float headLine = snap(lineH);
        const float tq = ease_out(((float)(now - g_dsB.openMs) / 1000.0f) / 0.07f), SA = g_dsB.vis * tq * A;
        const float oy = snap(under + headLine);
        const int cur = last.subCursor >= 1 && last.subCursor <= 3 ? last.subCursor : 0;
        if (!recv && last.typing) rows[0].label = " ";                  // the live line is drawn over it, below
        const int row = block_draw(f, k, g_dsB, rows, 3, cur, last.subFocused || last.typing, bx, oy, ow, oh, SA, dt);
        fo->begin(dev);
        if (!recv && last.typing && g_dsB.rowStep > 0.0f) {             // the name being typed : its text and caret, as the comment's line
            const float cy = snap(g_dsB.rowY0 + g_dsB.capH * 0.5f), tx = snap(g_dsB.capX > 0.0f ? g_dsB.capX + 10.0f * S : bx + pad + 10.0f * S);   // (block_draw's row 1 : absolute)
            char t[0x170]; unsigned col[0x170]; int caret = 0;
            const int n = chat_input_decode(last.live, last.liveN, t, col, sizeof(t), &caret);
            char nm[0x170]; _snprintf(nm, sizeof(nm), "%s : ", tr("Name", "Nom")); nm[sizeof(nm) - 1] = 0;
            fo->draw_lc(dev, tx, cy, nm, tz, with_alpha(0xFF93A6BEu, SA), with_alpha(0xC8000000u, SA), k.ow);
            float x = tx + fo->measure(nm, tz), caretX = x;
            for (int a = 0; a < n; ) {
                int e = a + 1; while (e < n && col[e] == col[a]) ++e;
                char seg[0x170]; memcpy(seg, t + a, (size_t)(e - a)); seg[e - a] = 0;
                if (caret >= a && caret < e) { char pre[0x170]; memcpy(pre, t + a, (size_t)(caret - a)); pre[caret - a] = 0; caretX = x + fo->measure(pre, tz); }
                fo->draw_lc(dev, snap(x), cy, seg, tz, with_alpha(col[a] | 0xFF000000u, SA), with_alpha(0xC8000000u, SA), k.ow);
                x += fo->measure(seg, tz); a = e;
            }
            if (caret >= n) caretX = x;
            const float p = 0.5f + 0.5f * sinf(f.t * 7.0f);
            rrect(dev, snap(caretX), snap(cy - k.capH * 0.38f), 1.5f * S, snap(k.capH * 0.76f), 0.0f, with_alpha(0xFFFFDA8Au, SA * p), with_alpha(0xFFFFDA8Au, SA * p));
            fo->begin(dev);
        }
        {   char t[64];
            if (recv) _snprintf(t, sizeof(t), "%s %s", tr("Sender :", "Exp\xC3\xA9""diteur :"), sc >= 0 && last.slot[sc].name[0] ? last.slot[sc].name : "-");
            else _snprintf(t, sizeof(t), "%s", tr("Recipient", "Destinataire"));
            t[63] = 0; fo->draw_lc(dev, snap(bx + pad), snap(under + headLine * 0.5f), t, tz * 0.9f, with_alpha(0xFFE8D9B0u, SA), with_alpha(0xC8000000u, SA), k.ow); }
        if (D0.subOpen && D0.subFocused) {
            if (row >= 1 && row <= 3 && row != g_dsB.hoverRow && row != D0.subCursor && !last.subGrey[row]) native_dlvsub_hover(row);
            g_dsB.hoverRow = row;
            if (row >= 1 && row <= 3 && !last.subGrey[row] && f.mouse && f.mouse->clicked) native_dlvsub_pick(row);
        } else g_dsB.hoverRow = 0;
        hit_extend(17, oy + oh, f.screenH);
        under = snap(oy + oh + gap);
    }
    // ---- the inventory beside it ----
    unsigned iid = 0;
    if (block_ease(g_diB, D0.box && D0.invOpen, now, dt) && last.invVis > 0) {
        char cnt[16]; sprintf(cnt, "%d", last.invN);
        ItemsBox IL = { last.invRow, last.invN, last.invTop, last.invVis, last.invCursor, 0, last.invFocused, D0.box && D0.invOpen, tr("Items", "Objets"), cnt, false };
        const float rowH = snap(k.capH * 1.25f);
        const float lw = keep_width(g_diB, items_box_width(f, k, IL)), lh = snap(pad + headH + 6.0f * S + (float)last.invVis * rowH + pad);
        float lx = snap(bx + bw + gap), ly = by;
        if (lx + lw > f.screenW - 4.0f) lx = snap(f.screenW - 4.0f - lw);
        const float tI = ease_out(((float)(now - g_diB.openMs) / 1000.0f) / 0.08f);
        const unsigned r = items_box_draw(f, k, g_diB, IL, "inventor", lx, snap(ly + (1.0f - tI) * 4.0f * S), lw, lh, g_diB.vis * tI * A * (last.invFocused ? 1.0f : 0.8f), dt, g_hit[18]);
        if (last.invFocused) iid = r;
    }
    // ---- the item card : the slot under the cursor (not gil), or the inventory's row ----
    if (!iid && last.focused && last.cursor >= 1 && last.cursor <= 8) { const NativeDlvSlot& w = last.slot[last.cursor - 1]; if (w.item && w.item != 0xFFFF && shown(w.state)) iid = w.item; }
    if (iid && !card_on_chat(f, k, iid, tO, A)) {
        const float cw = bw > snap(300.0f * S) ? bw : snap(300.0f * S), ctop = snap(under + 8.0f * S);
        card_use(0); card_load(f.dev, iid);
        if (card_ready(iid)) card_draw(f, k, bx, cw, ctop, ctop, tO, A, false);
    }
}
// ======== CHANGE JOBS (`jobcselu`, Game UI -> NU_NPC) : the 22 jobs in the client's pairs (2 columns x 11) ========
// Each cell : the job's emblem in its role colour, its name, its level (grey : the client refuses it) ; the main / support
// job marked. The client does the change.
namespace { Block g_jsB; unsigned g_jsMs = 0; }
void jobsel_draw(const Frame& f) {
    const UiConfig& C = ui_config();
    if (!(C.nuHide & NU_NPC) || !f.font || f.screenH <= 0.0f || f.screenW <= 0.0f) { g_jsB.vis = 0.0f; g_jsB.wasOpen = false; return; }
    const unsigned now = GetTickCount();
    float dt = g_jsMs ? (float)(now - g_jsMs) / 1000.0f : 0.0f; g_jsMs = now; if (dt > 0.1f) dt = 0.1f;
    const NativeJobSel& J0 = native_jobsel();
    static NativeJobSel last;
    if (J0.open) last = J0;
    if (!block_ease(g_jsB, J0.open, now, dt) || last.uiW <= 0) return;
    const Metrics k = metrics(f);
    const float S = k.S, pad = snap(8.0f * S) + k.bi, tz = k.tz, headH = snap(18.0f * S), gap = snap(6.0f * S);
    Font* fo = f.font; const u32 dev = f.dev;
    const float tO = ease_out(((float)(now - g_jsB.openMs) / 1000.0f) / 0.08f);
    const float A = g_jsB.vis * tO; if (A <= 0.0f) return;
    const u32 sh = with_alpha(0xC8000000u, A);
    const float rowH = snap(k.capH * 1.35f), iz = snap(rowH - 4.0f * S);
    const float colW = snap(8.0f * S + iz + 8.0f * S + fo->measure("WAR", tz) + 14.0f * S + fo->measure("Lv.99", tz * 0.9f) + 20.0f * S);
    const float bw = snap(pad + 2.0f * colW + gap + pad), bh = snap(pad + headH + 6.0f * S + 11.0f * rowH + pad);
    const float sx = f.screenW / (float)last.uiW, sy = f.screenH / (float)last.uiH;
    float bx = snap((float)last.rx0 * sx), by = snap((float)last.ry0 * sy);
    { float cx, cy; if (place_on_chat(bw, bh, snap(4.0f * S), f.screenW, f.screenH, cx, cy)) { bx = cx; by = cy; } }   // above the chat (2026-11-21 : "comme le reste")
    if (bx + bw > f.screenW - 4.0f) bx = snap(f.screenW - 4.0f - bw); if (bx < 4.0f) bx = 4.0f;
    if (by + bh > f.screenH - 4.0f) by = snap(f.screenH - 4.0f - bh); if (by < 4.0f) by = 4.0f;
    const int jm = menu_place(f, __LINE__, bx, by, bw, bh);
    by = snap(by + (1.0f - tO) * 4.0f * S);
    g_frameJoin = jm;
    draw_themed_box(dev, f.skin, bx, by, bw, bh, C.muBox, A, S);
    const float L = bx + pad, hy = snap(by + pad + headH * 0.5f), gy = snap(by + pad + headH + 6.0f * S);
    rrect(dev, L, snap(by + pad + headH), bw - 2.0f * pad, 1.0f, 0.0f, with_alpha(0x30C8D6EAu, A), with_alpha(0x30C8D6EAu, A));
    const MouseState* ms = f.mouse; int ment = 0;
    const u32 t = ensure_raw_tex(dev, g_jobTex, g_jobR, plugin_path_r("assets\\job_icons.raw"), 512, 192);
    for (int e = 1; e <= 22; ++e) {
        const int c = (e - 1) & 1, r = (e - 1) >> 1;
        const float x = snap(L + (float)c * (colW + gap)), y = snap(gy + (float)r * rowH);
        if ((r & 1) == 0) rrect(dev, x, y, colW, rowH, 3.0f * S, with_alpha(0x14FFFFFFu, A), with_alpha(0x0CFFFFFFu, A));
        const bool isMain = last.jobs.mjob == e, isSub = last.jobs.sjob == e;
        if (isMain || isSub) {                                        // your current jobs : a tinted row, a firm rim (2026-11-21 : "trop discret") --
            const u32 tc = isMain ? 0xFF6BE08Au : 0xFF7FC4F0u;        // main GREEN (the cursor is gold), support blue
            rrect(dev, x, y, colW, rowH, rowH * 0.5f, with_alpha((tc & 0x00FFFFFFu) | 0x40000000u, A), with_alpha((tc & 0x00FFFFFFu) | 0x24000000u, A));
            rrect_stroke(dev, x, y, colW, rowH, rowH * 0.5f, with_alpha((tc & 0x00FFFFFFu) | 0xD8000000u, A), 2.0f * S);
        }
        if (last.cursor == e && last.focused) {
            rrect_clip_begin(dev, x, y, colW, rowH, rowH * 0.5f); party_selframe(dev, x, y, colW, rowH, f.t, A, 0); rrect_clip_end(dev);
            rrect_stroke(dev, x, y, colW, rowH, rowH * 0.5f, with_alpha(0xD8FFDA8Au, A), 1.5f * S);
        }
        if (t) {                                                       // the emblem in its role colour (the party's atlas : 8 x 3 of 64)
            const float a = A * (last.grey[e] ? 0.4f : 1.0f), m = 6.0f / 64.0f;
            const float u0 = (float)((e - 1) % 8) * 64.0f / 512.0f, v0 = (float)((e - 1) / 8) * 64.0f / 192.0f, du = 64.0f / 512.0f, dv = 64.0f / 192.0f;
            const u32 tint = (job_role_color(e) & 0x00FFFFFFu) | 0xFF000000u;
            dTexQuadState(dev, t);
            tquad(dev, snap(x + 6.0f * S), snap(y + 2.0f * S), iz, iz, u0 + du * m, u0 + du * (1.0f - m), v0 + dv * m, v0 + dv * (1.0f - m), with_alpha(tint, a), with_alpha(tint, a));
            dSetTex(dev, 0, 0); cs(dev);
        }
        if (ms && ms->focused && ms->x >= x && ms->x < x + colW && ms->y >= y && ms->y < y + rowH) ment = e;
    }
    if (J0.open) {
        g_hit[18][0] = bx / f.screenW; g_hit[18][1] = by / f.screenH; g_hit[18][2] = bw / f.screenW; g_hit[18][3] = bh / f.screenH;
        if (J0.focused) {
            if (ment && ment != g_jsB.hoverRow && ment != J0.cursor && !J0.grey[ment]) native_jobsel_hover(ment);
            g_jsB.hoverRow = ment;
            if (ment && !J0.grey[ment] && ms->clicked) native_jobsel_pick(ment);
        } else g_jsB.hoverRow = 0;
    }
    fo->begin(dev);
    fo->draw_lc(dev, L, hy, last.sub ? tr("Support job", "Job de soutien") : tr("Main job", "Job principal"), tz * 0.95f, with_alpha(0xFFE8D9B0u, A), sh, k.ow);
    for (int e = 1; e <= 22; ++e) {
        const int c = (e - 1) & 1, r = (e - 1) >> 1;
        const float x = snap(L + (float)c * (colW + gap)), cy = snap(gy + (float)r * rowH + rowH * 0.5f);
        const bool g = last.grey[e] != 0;
        fo->draw_lc(dev, snap(x + 6.0f * S + iz + 8.0f * S), cy, job_abbr(e), tz, with_alpha(g ? 0xFF56627Au : 0xFFF2F5FAu, A), sh, k.ow);
        const int lv = last.jobs.lvl[e];
        if (lv > 0) { char l[12]; sprintf(l, "Lv.%d", lv); fo->draw_lc(dev, snap(x + colW - 8.0f * S - fo->measure(l, tz * 0.9f)), cy, l, tz * 0.9f, with_alpha(g ? 0xFF56627Au : lv >= 99 ? 0xFFFFDA8Au : 0xFFC8D6EAu, A), sh, k.ow); }
    }
}
// ======== TREASURE (`loot` / `lootope` / `lnowin`, Game UI -> NU_ACT) ========
// The pool as the client lists it (by drop time) : the icon, the name in YOUR state's colour (gold lotted, grey passed),
// the quantity, the time left (the Treasure box's timer, by pool index), the best lot and who holds it, your own lot.
// Lot / Pass beside the picked row, Pass all under the list. The client sends 0x041 / 0x042.
namespace { Block g_ltB, g_loB; unsigned g_ltMs = 0; }
void loot_draw(const Frame& f) {
    const UiConfig& C = ui_config();
    if (!(C.nuHide & NU_ACT) || !f.font || f.screenH <= 0.0f || f.screenW <= 0.0f) { g_ltB.vis = g_loB.vis = 0.0f; g_ltB.wasOpen = g_loB.wasOpen = false; return; }
    const unsigned now = GetTickCount();
    float dt = g_ltMs ? (float)(now - g_ltMs) / 1000.0f : 0.0f; g_ltMs = now; if (dt > 0.1f) dt = 0.1f;
    const NativeLoot& X0 = native_loot();
    static NativeLoot last;
    if (X0.open) last = X0;
    if (!block_ease(g_ltB, X0.open, now, dt) || last.uiW <= 0 || last.n <= 0) { g_loB.vis = 0.0f; g_loB.wasOpen = false; return; }
    const Metrics k = metrics(f);
    const float S = k.S, pad = snap(8.0f * S) + k.bi, tz = k.tz, headH = snap(18.0f * S), gap = snap(6.0f * S);
    Font* fo = f.font; const u32 dev = f.dev;
    const float tO = ease_out(((float)(now - g_ltB.openMs) / 1000.0f) / 0.08f);
    const float A = g_ltB.vis * tO * (last.focused || !X0.open ? 1.0f : 0.85f); if (A <= 0.0f) return;
    const u32 sh = with_alpha(0xC8000000u, A);
    const float rowH = snap(k.capH * 1.45f), iz = snap(rowH - 4.0f * S), btnH = snap(k.capH * 1.5f);
    const float wName = fo->measure(ITEM_NAME_REF, tz), wTime = fo->measure("00:00", tz * 0.9f), wBest = fo->measure("Mmmmmmmmmmmmmmm 999", tz * 0.85f), wOwn = fo->measure("999", tz);
    const float bw = snap(pad + 8.0f * S + iz + 8.0f * S + wName + 14.0f * S + wTime + 14.0f * S + wBest + 14.0f * S + wOwn + 8.0f * S + pad);
    const float bh = snap(pad + headH + 6.0f * S + (float)last.n * rowH + (last.allOpen ? 8.0f * S + btnH : 0.0f) + pad);
    const float sx = f.screenW / (float)last.uiW, sy = f.screenH / (float)last.uiH;
    float bx = snap((float)last.rx0 * sx), by = snap((float)last.ry0 * sy);
    if (bx + bw > f.screenW - 4.0f) bx = snap(f.screenW - 4.0f - bw); if (bx < 4.0f) bx = 4.0f;
    if (by + bh > f.screenH - 4.0f) by = snap(f.screenH - 4.0f - bh); if (by < 4.0f) by = 4.0f;
    const int jm = menu_place(f, __LINE__, bx, by, bw, bh);
    by = snap(by + (1.0f - tO) * 4.0f * S);
    g_frameJoin = jm;
    draw_themed_box(dev, f.skin, bx, by, bw, bh, C.muBox, A, S);
    const float L = bx + pad, W = bw - 2.0f * pad, hy = snap(by + pad + headH * 0.5f), ry = snap(by + pad + headH + 6.0f * S);
    rrect(dev, L, snap(by + pad + headH), W, 1.0f, 0.0f, with_alpha(0x30C8D6EAu, A), with_alpha(0x30C8D6EAu, A));
    for (int i = 0; i < last.n; ++i) if ((i & 1) == 0) rrect(dev, L, snap(ry + (float)i * rowH), W, rowH, 3.0f * S, with_alpha(0x18FFFFFFu, A), with_alpha(0x10FFFFFFu, A));
    const MouseState* ms = f.mouse; int mrow = -1;
    if (last.cursor >= 0 && last.cursor < last.n) {
        const float y = snap(ry + (float)last.cursor * rowH), r = rowH * 0.5f;
        rrect_clip_begin(dev, L, y, W, rowH, r); party_selframe(dev, L, y, W, rowH, f.t, A, 0); rrect_clip_end(dev);
        rrect_stroke(dev, L, y, W, rowH, r, with_alpha(0xC8FFDA8Au, A), 1.0f);
        if (last.focused && g_handTex) { const float hand = snap(k.capH * 1.25f); party_cursor(dev, g_handTex, bx - hand * 0.35f + party_cursor_bob(f.t, hand), y + rowH * 0.5f, hand, 0); }
    }
    for (int i = 0; i < last.n; ++i) {
        const u32 tex = list_icon(f, 3, last.row[i].item);
        const float a = A * (last.row[i].ownLot < 0 ? 0.5f : 1.0f);
        if (tex) { dTexQuadState(dev, tex); tquad(dev, snap(L + 8.0f * S), snap(ry + (float)i * rowH + 2.0f * S), iz, iz, 0.0f, 1.0f, 0.0f, 1.0f, with_alpha(0xFFFFFFFFu, a), with_alpha(0xFFFFFFFFu, a)); dSetTex(dev, 0, 0); cs(dev); }
        if (ms && ms->focused && ms->x >= L && ms->x < L + W && ms->y >= ry + (float)i * rowH && ms->y < ry + (float)(i + 1) * rowH) mrow = i;
    }
    // Pass all : a button under the list
    const float btY = snap(ry + (float)last.n * rowH + 8.0f * S); bool mAll = false;
    if (last.allOpen) {
        const bool on = last.allFocused;
        rrect(dev, L, btY, W, btnH, btnH * 0.5f, with_alpha(on ? 0x40FFDA8Au : 0x20FFFFFFu, A), with_alpha(on ? 0x30FFDA8Au : 0x14FFFFFFu, A));
        rrect_stroke(dev, L, btY, W, btnH, btnH * 0.5f, with_alpha(on ? 0xD8FFDA8Au : 0x40C8D6EAu, A), on ? 1.5f * S : 1.0f);
        mAll = ms && ms->focused && ms->x >= L && ms->x < L + W && ms->y >= btY && ms->y < btY + btnH;
    }
    if (X0.open) {
        g_hit[19][0] = bx / f.screenW; g_hit[19][1] = by / f.screenH; g_hit[19][2] = bw / f.screenW; g_hit[19][3] = bh / f.screenH;
        if (X0.focused) {
            if (mrow >= 0 && mrow + 1 != g_ltB.hoverRow && mrow != X0.cursor) native_loot_hover(mrow);
            g_ltB.hoverRow = mrow + 1;
            if (mrow >= 0 && ms->clicked) native_loot_pick(mrow);
        } else g_ltB.hoverRow = 0;
        if (mAll && ms->clicked) native_lootall_pick();
    }
    const TreasureItem* pool = party().treasure_slots();
    const unsigned unow = (unsigned)time(0);
    fo->begin(dev);
    fo->draw_lc(dev, L, hy, tr("Treasure", "Tr\xC3\xA9sor"), tz * 0.95f, with_alpha(0xFFE8D9B0u, A), sh, k.ow);
    {   char c[16]; sprintf(c, "%d / 10", last.n); fo->draw_lc(dev, snap(L + W - fo->measure(c, tz * 0.85f)), hy, c, tz * 0.85f, with_alpha(0xFF93A6BEu, A), sh, k.ow); }
    const float xName = snap(L + 8.0f * S + iz + 8.0f * S), xTime = snap(xName + wName + 14.0f * S), xBest = snap(xTime + wTime + 14.0f * S), xOwnR = snap(L + W - 8.0f * S);
    for (int i = 0; i < last.n; ++i) {
        const NativeLootRow& r = last.row[i];
        const float cy = snap(ry + (float)i * rowH + rowH * 0.5f);
        const u32 nc = r.ownLot < 0 ? 0xFF8A8F99u : r.ownLot > 0 ? 0xFFFFC870u : 0xFFF2F5FAu;   // passed grey, lotted gold
        { const char* nm = item_display_name(r.item); char cut[64], q[72];
          if (r.qty > 1) { _snprintf(q, sizeof(q), "%s x%d", nm ? nm : "?", r.qty); q[71] = 0; } else lstrcpynA(q, nm ? nm : "?", sizeof(q));
          fo->draw_lc(dev, xName, cy, fit_text(fo, q, tz, wName, cut, sizeof(cut)), tz, with_alpha(nc, A), sh, k.ow); }
        if (r.pool < 10 && pool[r.pool].itemId == r.item && pool[r.pool].expireUnix > unow) {   // the time left (the Treasure box's)
            const unsigned rem = pool[r.pool].expireUnix - unow; char t[8]; sprintf(t, "%02u:%02u", (rem / 60) % 100, rem % 60);
            fo->draw_lc(dev, xTime, cy, t, tz * 0.9f, with_alpha(rem < 60 ? 0xFFF06060u : rem > 180 ? 0xFF6BE06Bu : 0xFFF0A030u, A), sh, k.ow);
        }
        if (r.hiLot && r.lotter[0]) { char b[40]; _snprintf(b, sizeof(b), "%s %u", r.lotter, r.hiLot); b[39] = 0; fo->draw_lc(dev, xBest, cy, b, tz * 0.85f, with_alpha(0xFF7AD8F0u, A), sh, k.ow); }
        {   char o[16]; u32 oc;
            if (r.ownLot < 0) { strcpy(o, tr("Pass", "Pass\xC3\xA9")); oc = 0xFF8A8F99u; } else if (r.ownLot > 0) { sprintf(o, "%d", r.ownLot); oc = 0xFFFFC870u; } else { strcpy(o, "-"); oc = 0xFF56627Au; }
            fo->draw_lc(dev, snap(xOwnR - fo->measure(o, tz)), cy, o, tz, with_alpha(oc, A), sh, k.ow); }
    }
    if (last.allOpen) { const char* t = tr("Pass all", "Tout passer"); fo->draw_lc(dev, snap(L + (W - fo->measure(t, tz)) * 0.5f), snap(btY + btnH * 0.5f), t, tz, with_alpha(0xFFF2F5FAu, A), sh, k.ow); }
    // Lot / Pass : beside the picked row (the client opens it at the row's right edge)
    if (block_ease(g_loB, X0.open && X0.opeOpen, now, dt)) {
        RowSpec rows[2] = {};
        rows[0].label = tr("Cast lots", "Lancer les d\xC3\xA9s"); rows[0].icon = IC_NONE; rows[0].off = !last.opeLotOk;
        rows[1].label = tr("Pass", "Passer"); rows[1].icon = IC_NONE;
        float ow, oh; block_size(f, k, rows, 2, ow, oh);
        const int rr = last.cursor >= 0 ? last.cursor : 0;
        float ox = snap(bx + bw + gap), oy = snap(ry + (float)rr * rowH);
        if (ox + ow > f.screenW - 4.0f) ox = snap(bx - gap - ow); if (oy + oh > f.screenH - 4.0f) oy = snap(f.screenH - 4.0f - oh);
        const float tq = ease_out(((float)(now - g_loB.openMs) / 1000.0f) / 0.07f), OA = g_loB.vis * tq;
        const int cur = last.opeCursor == 1 || last.opeCursor == 2 ? last.opeCursor : 0;
        const int row = block_draw(f, k, g_loB, rows, 2, cur, last.opeFocused, ox, snap(oy + (1.0f - tq) * 3.0f * S), ow, oh, OA, dt);
        if (X0.opeOpen && X0.opeFocused) {
            if (row >= 1 && row <= 2 && row != g_loB.hoverRow && row != X0.opeCursor && (row != 1 || X0.opeLotOk)) native_lootope_hover(row);
            g_loB.hoverRow = row;
            if (row >= 1 && row <= 2 && (row != 1 || X0.opeLotOk) && f.mouse && f.mouse->clicked) native_lootope_pick(row);
        } else g_loB.hoverRow = 0;
        hit_extend(19, oy + oh, f.screenH);
    }
    // the item card : the row under the cursor
    const unsigned iid = last.cursor >= 0 && last.cursor < last.n ? last.row[last.cursor].item : 0;
    if (iid && !card_on_chat(f, k, iid, tO, A)) {
        const float cw = bw > snap(300.0f * S) ? bw : snap(300.0f * S), ctop = snap(by + bh + 14.0f * S);
        card_use(0); card_load(f.dev, iid);
        if (card_ready(iid)) card_draw(f, k, bx, cw, ctop, ctop, tO, A, false);
    }
}
// ======== Status -> PROFILE (Game UI -> NU_MAIN), in the menus' theme, where the client puts its own ========
// The fields the client's panel shows, each from a MEASURED source (native_profile : the 0x061 mirror + the self
// entity, 2026-11-18). Not yet here (no source proven) : Mentor, Monstrosity's rank and Prestige.
namespace { Block g_profB; unsigned g_profMs = 0; }
void profile_draw(const Frame& f) {
    const UiConfig& C = ui_config();
    const NativeProfile& P = native_profile();
    if (!(C.nuHide & NU_MAIN) || !f.font || f.screenH <= 0.0f || f.screenW <= 0.0f) { g_profB.vis = 0.0f; g_profB.wasOpen = false; return; }
    const unsigned now = GetTickCount();
    float dt = g_profMs ? (float)(now - g_profMs) / 1000.0f : 0.0f; g_profMs = now; if (dt > 0.1f) dt = 0.1f;
    static NativeProfile last;
    const bool on = P.open && P.ok && !native_currencies().open && !native_unity_info().open && !native_search().open && !native_job_points().open && !native_faith().open && !native_celestite().open && !native_merit_cat().open;     // Currencies opens in its place (the client hides it)
    if (on) last = P;
    if (!block_ease(g_profB, on, now, dt) || !last.ok || last.uiW <= 0 || last.uiH <= 0) return;
    const Metrics k = metrics(f);
    const float S = k.S;
    // COMPACT, at the client's own size (2026-11-18, "compacte le profil" -- like the stat panels) : its rect, the lines
    // fitted into it, the text a touch smaller when a line asks for it ; wider only if a line would not fit
    const bool mon = last.monOk;                                             // the Monstrosity section : header + 2 rows
    const float sx = f.screenW / (float)last.uiW, sy = f.screenH / (float)last.uiH, vpad = snap(5.0f * S) + k.bi;   // + the frame border
    float bh = snap((float)(last.ry1 - last.ry0) * sy); if (bh < 150.0f * S) bh = snap(150.0f * S);
    bh += 2.0f * k.bi;                                                       // the client's height + the frame border
    const int nLines = 7 + (mon ? 3 : 0);                                    // header, 6 rows, [header, 2 rows]
    float tz = k.tz;
    const float lh = (bh - 2.0f * vpad - (mon ? 4.0f * S : 0.0f)) / (float)nLines, headH = lh;
    if (tz > lh * 0.92f) tz = lh * 0.92f;
    // THE CLIENT'S LAYOUT (the user's reading of it, 2026-11-18) : a "Profile" section --
    //   Nation : [flag] name / Race / Home Point / Rank : n  .. "Rank Points" with its bar under it /
    //   Mastery Rank : n  .. Mentor : [flag]
    // then a "Monstrosity" section (Gladiator Rank, Prestige) -- that one and Mentor once their source is proven.
    static const char* NAT[3] = { "San d'Oria", "Bastok", "Windurst" };
    static const u32 NATC[3] = { 0xFFE0524Au, 0xFF4A86E0u, 0xFF52C46Au };
    static const char* RACE_EN[9] = { "?", "Hume", "Hume", "Elvaan", "Elvaan", "Tarutaru", "Tarutaru", "Mithra", "Galka" };
    const char* lNat = tr("Nation", "Nation");
    const char* lRace = tr("Race", "Race");
    const char* lHome = tr("Home Point", "Home Point");
    const char* lRank = tr("Rank", "Rang");
    const char* lPts = tr("Rank Points", "Rank Points");
    const char* lMas = tr("Mastery Rank", "Mastery Rank");
    char rk[16], ms[16]; sprintf(rk, "%d", last.rank); sprintf(ms, "%d", last.mastery);
    const char* hp = zone_name(last.home);
    const char* vNat = NAT[last.nation % 3];
    const char* vRace = RACE_EN[last.race >= 1 && last.race <= 8 ? last.race : 0];
    const char* vHome = hp ? hp : "?";
    Font* fo = f.font;
    const char* labs[4] = { lNat, lRace, lHome, lRank };
    float lw = fo->measure(lMas, tz);
    for (int i = 0; i < 4; ++i) { const float w = fo->measure(labs[i], tz); if (w > lw) lw = w; }
    const float pad = snap(6.0f * S) + k.bi, flag = snap(lh * 0.95f), colon = 10.0f * S;   // pad : + the frame border
    const float vx = pad + lw + colon;                                       // the values' column (from the box's left)
    float wNat = flag + 4.0f * S + fo->measure(vNat, tz), wHome = fo->measure(vHome, tz);
    const float wRight = fo->measure(lPts, tz) + 8.0f * S;                  // "Rank Points" + its bar, on the right half
    float bw = vx + (wNat > wHome ? wNat : wHome) + pad;
    const float rankRow = vx + fo->measure(rk, tz) + 24.0f * S + wRight + pad;
    if (rankRow > bw) bw = rankRow;
    { const float nW = (float)(last.rx1 - last.rx0) * sx + 2.0f * k.bi; if (bw < nW) bw = nW; }
    bw = snap(bw);
    const int rows = 6;                                                      // nation, race, home, rank, the bar, mastery
    float bx = snap((float)last.rx0 * sx), by = snap((float)last.ry0 * sy);
    if (bx + bw > f.screenW - 4.0f) bx = snap(f.screenW - 4.0f - bw); if (bx < 4.0f) bx = 4.0f;
    if (by + bh > f.screenH - 4.0f) by = snap(f.screenH - 4.0f - bh); if (by < 4.0f) by = 4.0f;
    const int jm = menu_place(f, __LINE__, bx, by, bw, bh);
    const float tO = ease_out(((float)(now - g_profB.openMs) / 1000.0f) / 0.08f);
    by = snap(by + (1.0f - tO) * 4.0f * S);
    const float A = g_profB.vis * tO; if (A <= 0.0f) return;
    const u32 dev = f.dev;
    g_frameJoin = jm;
    draw_themed_box(dev, f.skin, bx, by, bw, bh, C.muBox, A, S);
    // the section header : its title, a hairline under it
    const float hc = snap(by + vpad + headH * 0.5f);
    rrect(dev, bx + pad, snap(by + vpad + headH - 1.0f), bw - 2.0f * pad, 1.0f, 0.0f, with_alpha(0x30C8D6EAu, A), with_alpha(0x30C8D6EAu, A));
    const float y0 = by + vpad + headH, X = snap(bx + vx);
    auto rowC = [&](int i) { return snap(y0 + (float)i * lh + lh * 0.5f); };
    // Nation : the FLAG, the game's own picture (items 181..183 San d'Orian / Bastokan / Windurstian Flag, the item DAT) ;
    // until it is decoded, a dot in its colour
    {
        const u32 ft = list_icon(f, 3, 181u + (unsigned)(last.nation % 3));
        const float fy = snap(rowC(0) - flag * 0.5f);
        if (ft) { dTexQuadState(dev, ft); tquad(dev, X - 2.0f * S, fy, flag, flag, 0.0f, 1.0f, 0.0f, 1.0f, with_alpha(0xFFFFFFFFu, A), with_alpha(0xFFFFFFFFu, A)); dSetTex(dev, 0, 0); cs(dev); }
        else disc(dev, X + flag * 0.5f - 2.0f * S, rowC(0), 3.6f * S, with_alpha(NATC[last.nation % 3], A));
    }
    // Rank Points : the label on the right half of the Rank row, its bar on the row under it (full = 0xFFF, the cap)
    const float rpX = snap(bx + bw - pad - wRight + 8.0f * S), rpW = snap(bx + bw - pad - rpX);
    {
        const float ry = snap(rowC(4) - 2.0f * S), fr = (float)last.rankPts / 4095.0f;
        rrect(dev, rpX, ry, rpW, 4.0f * S, 2.0f * S, with_alpha(0x40FFFFFFu, A), with_alpha(0x30FFFFFFu, A));
        if (fr > 0.0f) rrect(dev, rpX, ry, snap(rpW * (fr > 1.0f ? 1.0f : fr)), 4.0f * S, 2.0f * S, with_alpha(0xE0FFDA8Au, A), with_alpha(0xC0E8B860u, A));
    }
    const u32 cl = with_alpha(0xFF93A6BEu, A), cv = with_alpha(0xFFE4EAF2u, A), sh = with_alpha(0xC8000000u, A);
    fo->begin(dev);
    fo->draw_lc(dev, snap(bx + pad), hc, tr("Profile", "Profil"), tz * 0.92f, with_alpha(0xFFE8D9B0u, A), sh, k.ow);
    fo->draw_lc(dev, snap(bx + pad), rowC(0), lNat, tz, cl, sh, k.ow);  fo->draw_lc(dev, snap(X + flag + 4.0f * S), rowC(0), vNat, tz, cv, sh, k.ow);
    fo->draw_lc(dev, snap(bx + pad), rowC(1), lRace, tz, cl, sh, k.ow); fo->draw_lc(dev, X, rowC(1), vRace, tz, cv, sh, k.ow);
    fo->draw_lc(dev, snap(bx + pad), rowC(2), lHome, tz, cl, sh, k.ow); fo->draw_lc(dev, X, rowC(2), vHome, tz, cv, sh, k.ow);
    fo->draw_lc(dev, snap(bx + pad), rowC(3), lRank, tz, cl, sh, k.ow); fo->draw_lc(dev, X, rowC(3), rk, tz, cv, sh, k.ow);
    fo->draw_lc(dev, rpX, rowC(3), lPts, tz, cl, sh, k.ow);
    fo->draw_lc(dev, snap(bx + pad), rowC(5), lMas, tz, cl, sh, k.ow); fo->draw_lc(dev, X, rowC(5), ms, tz, cv, sh, k.ow);
    // MENTOR : on the Mastery Rank row, right half -- its flag (bronze / silver / gold), "--" when not a mentor
    if (last.mentor >= 0) {
        const char* lMen = tr("Mentor", "Mentor");
        fo->draw_lc(dev, rpX, rowC(5), lMen, tz, cl, sh, k.ow);
        const float mx = snap(rpX + fo->measure(lMen, tz) + 8.0f * S), my = rowC(5);
        if (last.mentor == 0) fo->draw_lc(dev, mx, my, "--", tz, cv, sh, k.ow);
        else {
            static const u32 MC[4] = { 0, 0xFFE0A060u, 0xFFD8DEE8u, 0xFFFFD45Au };   // bronze, silver, gold
            const u32 c = with_alpha(MC[last.mentor], A), pole = with_alpha(0xFFB8A890u, A);
            const float h = lh * 0.78f, t = my - h * 0.5f;
            rrect(dev, mx, snap(t), snap(1.6f * S), snap(h), 0.0f, pole, pole);                 // the pole
            tri(dev, mx + 1.6f * S, t, mx + 1.6f * S + h * 0.85f, t + h * 0.26f, mx + 1.6f * S, t + h * 0.52f, c);   // the pennant
            fo->begin(dev);
        }
    }
    if (mon) {   // MONSTROSITY : the client's own words for the rank (its table : "Gla", "CG", "HCG")
        const float my0 = y0 + (float)rows * lh + 4.0f * S;
        const float mhc = snap(my0 + headH * 0.5f);
        rrect(dev, bx + pad, snap(my0 + headH - 1.0f), bw - 2.0f * pad, 1.0f, 0.0f, with_alpha(0x30C8D6EAu, A), with_alpha(0x30C8D6EAu, A));
        fo->begin(dev);
        fo->draw_lc(dev, snap(bx + pad), mhc, "Monstrosity", tz * 0.92f, with_alpha(0xFFE8D9B0u, A), sh, k.ow);
        char pr[16]; sprintf(pr, "%d", last.prestige);
        const char* lG = tr("Gladiator Rank", "Gladiator Rank");
        const char* lP = tr("Prestige", "Prestige");
        const float ry1 = snap(my0 + headH + lh * 0.5f), ry2 = snap(my0 + headH + lh * 1.5f);
        const float gx = snap(bx + pad + (fo->measure(lG, tz) > lw ? fo->measure(lG, tz) : lw) + colon);
        fo->draw_lc(dev, snap(bx + pad), ry1, lG, tz, cl, sh, k.ow); fo->draw_lc(dev, gx, ry1, last.gladName, tz, cv, sh, k.ow);
        fo->draw_lc(dev, snap(bx + pad), ry2, lP, tz, cl, sh, k.ow); fo->draw_lc(dev, gx, ry2, pr, tz, cv, sh, k.ow);
    }
}

// ======== the MAIN menu (Game UI -> NU_MAIN) : the client's two pages `menuwind` / `socialme`, mirrored ========
// The client keeps its menu (parked off-screen), its input and every window it opens ; this draws its rows, its cursor
// and its page in the menus' theme. Rows by ENTRY index (the definition's order, = the cursor's numbering ; shown in
// screen order by native_ui) ; the labels are the ones the client DRAWS (its images : "Magic", "Bazaar", "Macros" --
// the definition's texts say "Magic List", "Set Bazaar", "MacroPalette"), from the user's screenshots 2026-11-18.
namespace {
const SubRow MAIN1[12] = {
    { "Status",      "Statut",                         false, IC_LENS    },
    { "Equipment",   "\xC3\x89quipement",              false, IC_SWORD   },
    { "Magic",       "Magie",                          false, IC_STAR    },
    { "Items",       "Objets",                         false, IC_BAG     },
    { "Abilities",   "Aptitudes",                      false, IC_BOLT    },
    { "Party",       "Groupe",                         false, IC_DUO     },
    { "Trade",       "\xC3\x89""changer",              false, IC_SWAP    },
    { "Search",      "Rechercher",                     false, IC_LENS    },
    { "Linkshell",   "Linkshell",                      false, IC_BUBBLE  },
    { "Synthesis",   "Synth\xC3\xA8se",                false, IC_NONE    },
    { "Region Info", "Infos r\xC3\xA9gions",           false, IC_RETICLE },
    { "Map",         "Carte",                          false, IC_RETICLE },
};
const SubRow MAIN2[12] = {
    { "Missions",      "Missions",                     false, IC_STAR    },
    { "Quests",        "Qu\xC3\xAAtes",                false, IC_STAR    },
    { "Key Items",     "Objets cl\xC3\xA9s",           false, IC_BAG     },
    { "View House",    "Voir la maison",               false, IC_NONE    },
    { "Bazaar",        "Bazar",                        false, IC_BAG     },
    { "Macros",        "Macros",                       false, IC_NONE    },
    { "Config",        "Config",                       false, IC_NONE    },
    { "Help Desk",     "Help Desk",                    false, IC_BUBBLE  },
    { "Communication", "Communication",                false, IC_BUBBLE  },
    { "Current Time",  "Heure",                        false, IC_NONE    },
    { "Shut Down",     "Quitter le jeu",               false, IC_NONE    },
    { "Log Out",       "D\xC3\xA9""connexion",         false, IC_NONE    },
};
Block g_mainMB, g_mainSubB, g_mainListB, g_mainSortB, g_mainYnB;
unsigned g_mainLastMs = 0;
// a row the client's definition names in English : the French of the action menu's same row when there is one
const char* fr_of(const char* en) {
    if (!en || !en[0]) return en;
    for (int i = 0; i < (int)(sizeof(MAGSELEC) / sizeof(MAGSELEC[0])); ++i) if (!strcmp(MAGSELEC[i].en, en)) return tr(MAGSELEC[i].en, MAGSELEC[i].fr);
    for (int i = 0; i < (int)(sizeof(ABISELEC) / sizeof(ABISELEC[0])); ++i) if (!strcmp(ABISELEC[i].en, en)) return tr(ABISELEC[i].en, ABISELEC[i].fr);
    return en;
}
}

void mainmenu_draw(const Frame& f) {
    const UiConfig& C = ui_config();
    const NativeEntryMenu& M = native_main_menu();
    g_hit[5][2] = 0.0f;
    if (!(C.nuHide & NU_MAIN) || !f.font || f.screenH <= 0.0f || f.screenW <= 0.0f) { g_mainMB.vis = 0.0f; g_mainMB.wasOpen = false; g_mainMB.w = 0.0f; return; }
    const unsigned now = GetTickCount();
    float dt = g_mainLastMs ? (float)(now - g_mainLastMs) / 1000.0f : 0.0f; g_mainLastMs = now;
    if (dt > 0.1f) dt = 0.1f;
    static NativeEntryMenu last;                         // the last menu seen : it fades out with its own rows
    if (M.open) last = M;
    const bool chain = (M.open || native_main_sub().open || native_main_sort().open)   // the menu stays drawn under what it opened
                    && !native_region().open;                                       // ... but the Region Info map hides it (the client's do)
    if (!block_ease(g_mainMB, chain, now, dt) || !last.n || last.uiW <= 0 || last.uiH <= 0) return;
    const Metrics k = metrics(f), kN = no_icons(k);
    const float S = k.S;
    const bool page2 = !strcmp(last.name, "socialme");
    const SubRow* T = page2 ? MAIN2 : MAIN1;
    RowSpec rows[32]; char nums[32][12]; int cur = 0;   // 32 = NativeEntryMenu's capacity : every row passed on is filled
    for (int i = 0; i < last.n && i < 32; ++i) {
        const int e = last.entry[i];
        if (e >= 1 && e <= 12) { rows[i].label = tr(T[e - 1].en, T[e - 1].fr); rows[i].icon = T[e - 1].icon; }
        else { sprintf(nums[i], "#%d", e); rows[i].label = nums[i]; rows[i].icon = IC_NONE; }
        rows[i].sub = false; rows[i].off = false;
        if (e == last.cursor) cur = i + 1;
    }
    // ONE width for both pages (the widest of the 24 labels) : turning the page never resizes the box
    float bw = 0.0f, bh = 0.0f;
    {
        RowSpec one[1]; one[0].sub = false; one[0].off = false;
        for (int i = 0; i < 24; ++i) {
            const SubRow& r = i < 12 ? MAIN1[i] : MAIN2[i - 12];
            one[0].label = tr(r.en, r.fr); one[0].icon = r.icon;
            float w1, h1; block_size(f, kN, one, 1, w1, h1); if (w1 > bw) bw = w1;
        }
        float w2; block_size(f, kN, rows, last.n, w2, bh);
    }
    // the header band : the title on the left, the page on the right (< 1 / 2 >) ; left / right turn it, as in the game
    const float tzH = k.tz * 0.92f, headH = snap(k.capH + k.gap);
    const char* title = tr("Commands", "Commandes");
    const char* pg = page2 ? "2 / 2" : "1 / 2";
    {
        const float need = snap(k.gutter + f.font->measure(title, tzH) + 14.0f * S + f.font->measure(pg, tzH) + 18.0f * S + k.pad) + 2.0f * k.bi;
        if (need > bw) bw = need;
    }
    bh = snap(bh + headH);
    g_mainMB.inTop = headH;
    // place : the game's (its top-left corner, in its UI units) or the top left (the client's own left margin)
    const float sx = f.screenW / (float)last.uiW, sy = f.screenH / (float)last.uiH;
    float bx = C.muMainPos == 1 ? snap(16.0f * sx) : snap((float)last.rx1 * sx - bw);
    float by = snap((float)last.ry0 * sy);
    if (bx + bw > f.screenW - 4.0f) bx = snap(f.screenW - 4.0f - bw); if (bx < 4.0f) bx = 4.0f;
    if (by + bh > f.screenH - 4.0f) by = snap(f.screenH - 4.0f - bh); if (by < 4.0f) by = 4.0f;
    // Magic's categories open IN PLACE of this menu (the client swaps them, as the action menu's Abilities) : while they
    // are up this menu is not drawn (2026-11-18, "le sous menu remplace le main menu plutot que de se mettre a cote")
    const NativeGenMenus& GM = native_gen_menus();             // ... and so does a generic window it opened (Config & co, 2026-11-21)
    // ... and the Mog House's menus it opens (View House -> mogcont, 2026-11-22 "le menu principal reste affiche derriere"), the panels
    const bool subUp = native_main_sub().open || native_main_sort().open || native_main_native_child() || (!last.focused && (GM.n > 0 || GM.grid || GM.list || GM.np > 0 || native_mog_menus().n > 0));
    const int jm = subUp ? 0 : menu_place(f, __LINE__, bx, by, bw, bh);   // recorded only while drawn (theme kit)
    const float by0 = by;                                    // its rest place : what opens in its place is placed from it
    const float tOpen = ease_out(((float)(now - g_mainMB.openMs) / 1000.0f) / 0.08f);
    by = snap(by + (1.0f - tOpen) * 4.0f * S);
    const float A = subUp ? 0.0f : g_mainMB.vis * tOpen * (last.focused ? 1.0f : 0.62f);   // a window it opened on top : steps back
    if (A > 0.0f) {
        g_frameJoin = jm;
        const int row = block_draw(f, kN, g_mainMB, rows, last.n, cur, last.focused, bx, by, bw, bh, A, dt);
        // the header : a hairline under it, the title, the page between its two arrows
        const u32 dev = f.dev;
        const float hy = by + k.bi + k.pad, hc = snap(hy + (headH - k.gap) * 0.5f);   // inside the frame border
        rrect(dev, bx + k.bi + k.gutter, snap(hy + headH - k.gap * 0.5f - 1.0f), bw - 2.0f * k.bi - k.gutter - k.pad, 1.0f, 0.0f, with_alpha(0x30C8D6EAu, A), with_alpha(0x30C8D6EAu, A));
        const float pw = f.font->measure(pg, tzH), ar = 3.0f * S;
        const float rx = snap(bx + bw - k.bi - k.pad - 4.0f * S), lx = snap(rx - 2.0f * ar - 6.0f * S - pw - 6.0f * S);
        const u32 ac = with_alpha(0xC0FFDA8Au, A);
        tri(dev, rx, hc, rx - 2.0f * ar, hc - ar * 1.2f, rx - 2.0f * ar, hc + ar * 1.2f, ac);
        tri(dev, lx, hc, lx + 2.0f * ar, hc - ar * 1.2f, lx + 2.0f * ar, hc + ar * 1.2f, ac);
        Font* fo = f.font;
        fo->begin(dev);
        fo->draw_lc(dev, snap(bx + k.bi + k.gutter + k.capPadL * 0.5f), hc, title, tzH, with_alpha(0xFFE8D9B0u, A), with_alpha(0xC8000000u, A), k.ow);
        fo->draw_lc(dev, snap(lx + 2.0f * ar + 6.0f * S), hc, pg, tzH, with_alpha(0xFFC9D3E0u, A), with_alpha(0xC8000000u, A), k.ow);
        if (M.open) {
            g_hit[5][0] = bx / f.screenW; g_hit[5][1] = by / f.screenH; g_hit[5][2] = bw / f.screenW; g_hit[5][3] = bh / f.screenH;
            // the MOUSE : hover moves the client's cursor, a click validates -- the client's own events (native_ui)
            const int ent = (row >= 1 && row <= last.n) ? last.entry[row - 1] : 0;
            if (ent && row != g_mainMB.hoverRow && ent != M.cursor) native_main_hover(ent);
            g_mainMB.hoverRow = row;
            if (ent && f.mouse && f.mouse->clicked) native_main_pick(ent);
        } else g_mainMB.hoverRow = 0;
    } else g_mainMB.hoverRow = 0;

    // ======== what it opens, in the same style : Magic's categories (mgcmenu), then the spell list ========
    // The categories IN PLACE of the menu ; the list where the action menu's goes (above the chat). The list is the
    // action menu's own (native_list : parked by Game UI -> Action menu).
    const bool leftward = bx + bw * 0.5f > f.screenW * 0.5f;
    const float gap = snap(3.0f * S);
    float nextX = leftward ? bx - gap : bx + bw + gap;           // the next block's near edge
    const NativeEntryMenu& SM = native_main_sub();
    static NativeEntryMenu lastS;
    if (SM.open) lastS = SM;
    g_hit[6][2] = g_hit[7][2] = g_hit[8][2] = 0.0f;
    // the spell list's SORT options (mgcsortw) : open with the list, in the menu's corner -- in place of the categories
    // (2026-11-18, "on voit Options: + :Sort - Auto - Manual" ; X or numpad + gives them the cursor)
    const NativeEntryMenu& SO = native_main_sort();
    static NativeEntryMenu lastO;
    if (SO.open) lastO = SO;
    // AUTO-SORT first : the client puts its cursor back on the option used last (Manual Sort) -- each time the options
    // take the cursor, it goes on Auto-sort (2026-11-18, "c'est manual qui est selectionne par defaut, on veut auto")
    {
        static bool wasF = false;
        const bool nowF = SO.open && SO.focused;
        if (nowF && !wasF && SO.cursor != 1) native_main_sort_hover(1);
        wasF = nowF;
    }
    // Items -> a row's Use / Dispose (iuse) : in the sort options' place while it is up
    const bool iuseUp = SM.open && !strcmp(SM.name, "iuse");
    float ynX = -1.0f, ynY = 0.0f, ynW = 0.0f;               // where Yes / No hangs : under the block it answers
    if (block_ease(g_mainSortB, SO.open && !iuseUp, now, dt) && lastO.n) {
        RowSpec orows[32]; char onums[32][12]; int ocur = 0;   // 32 = NativeEntryMenu's capacity
        for (int i = 0; i < lastO.n && i < 32; ++i) {
            const int e = lastO.entry[i];
            if (e == 1 && !strcmp(lastO.name, "abisortw")) orows[i].label = tr("Manual Sort", "Tri manuel");   // its only entry
            else if (e == 1) orows[i].label = tr("Auto-sort", "Tri automatique");
            else if (e == 2) orows[i].label = tr("Manual Sort", "Tri manuel");
            else if (e == 3 && !strcmp(lastO.name, "blusortw")) orows[i].label = tr("Blue Magic", "Magie bleue");   // set the spells
            else if (e == 3 && !strcmp(lastO.name, "itmsort2")) orows[i].label = tr("Recycling Bin", "Corbeille");
            else if (lastO.label[i][0]) orows[i].label = lastO.label[i];
            else { sprintf(onums[i], "#%d", e); orows[i].label = onums[i]; }
            orows[i].icon = IC_NONE; orows[i].sub = false; orows[i].off = lastO.state[i] == 5;
            if (e == lastO.cursor) ocur = i + 1;
        }
        float ow, oh; block_size(f, kN, orows, lastO.n, ow, oh);
        const float tzH = k.tz * 0.92f, headH = snap(k.capH + k.gap);
        const char* ot = tr("Sort", "Tri");
        const char* hint = tr("X / +", "X / +");
        const float need = snap(k.gutter + f.font->measure(ot, tzH) + 14.0f * S + f.font->measure(hint, tzH * 0.9f) + k.pad + 4.0f * S) + 2.0f * k.bi;
        if (need > ow) ow = need;
        oh = snap(oh + headH); g_mainSortB.inTop = headH;
        const float tS = ease_out(((float)(now - g_mainSortB.openMs) / 1000.0f) / 0.07f);
        float obx = C.muMainPos == 1 ? bx : snap(bx + bw - ow), oby = snap(by + (1.0f - tS) * 3.0f * S);
        // in the menu's place (hidden then) ; recorded while open only : fading out it must not push what replaces it
        const int jo = SO.open && !iuseUp ? menu_place_at(f, __LINE__, obx, oby, obx, by0, ow, oh) : 0;
        const float OA = g_mainSortB.vis * tS * (lastO.focused ? 1.0f : 0.62f);
        if (OA > 0.0f) {
            g_frameJoin = jo;
            const int orow = block_draw(f, kN, g_mainSortB, orows, lastO.n, lastO.focused ? ocur : 0, lastO.focused, obx, oby, ow, oh, OA, dt);
            const float hc = snap(oby + k.bi + k.pad + (headH - k.gap) * 0.5f);   // inside the frame border
            rrect(f.dev, obx + k.bi + k.gutter, snap(oby + k.bi + k.pad + headH - k.gap * 0.5f - 1.0f), ow - 2.0f * k.bi - k.gutter - k.pad, 1.0f, 0.0f, with_alpha(0x30C8D6EAu, OA), with_alpha(0x30C8D6EAu, OA));
            f.font->begin(f.dev);
            f.font->draw_lc(f.dev, snap(obx + k.bi + k.gutter + k.capPadL * 0.5f), hc, ot, tzH, with_alpha(0xFFE8D9B0u, OA), with_alpha(0xC8000000u, OA), k.ow);
            const float hw = f.font->measure(hint, tzH * 0.9f);
            f.font->draw_lc(f.dev, snap(obx + ow - k.bi - k.pad - 4.0f * S - hw), hc, hint, tzH * 0.9f, with_alpha(0xFF93A6BEu, OA), with_alpha(0xC8000000u, OA), k.ow);
            if (SO.open) {
                g_hit[6][0] = obx / f.screenW; g_hit[6][1] = oby / f.screenH; g_hit[6][2] = ow / f.screenW; g_hit[6][3] = oh / f.screenH;
                const int ent = (orow >= 1 && orow <= lastO.n) ? lastO.entry[orow - 1] : 0;
                if (ent && orow != g_mainSortB.hoverRow && ent != SO.cursor) native_main_sort_hover(ent);
                g_mainSortB.hoverRow = orow;
                if (ent && f.mouse && f.mouse->clicked) native_main_sort_pick(ent);
            } else g_mainSortB.hoverRow = 0;
        }
        if (OA > 0.0f) { ynX = obx; ynY = oby + oh; ynW = ow; }
    }
    if (block_ease(g_mainSubB, SM.open && (!SO.open || iuseUp) && !native_scoption().open, now, dt) && lastS.n) {   // a player's options replace it
        RowSpec srows[32]; char snums[32][12]; char svals[32][32]; int scur = 0;
        for (int i = 0; i < lastS.n && i < 32; ++i) {
            const SubRow* r = sub_row(lastS.name, lastS.entry[i]);
            if (r) { srows[i].label = tr(r->en, r->fr); srows[i].icon = r->icon; }
            else if (lastS.label[i][0]) { srows[i].label = fr_of(lastS.label[i]); srows[i].icon = IC_STAR; }
            else { sprintf(snums[i], "#%d", lastS.entry[i]); srows[i].label = snums[i]; srows[i].icon = IC_STAR; }
            srows[i].sub = false; srows[i].off = lastS.state[i] == 5;
            // Mode Switch : the client greys the CURRENT mode -- shown as the one selected instead (the user, 2026-11-18)
            if (!strcmp(lastS.name, "merit2") && lastS.state[i] == 5) {
                srows[i].off = false; srows[i].mark = true; srows[i].tint = 0xFFFFDA8Au; srows[i].extra = tr("current", "actuel");
            }
            // Party : a toggle that is ON (Seek Party, Autogroup, Request), the language(s) / the alarm chosen
            if (lastS.entMarkKind && lastS.entry[i] < 32 && ((lastS.entMarks >> lastS.entry[i]) & 1u)) {
                srows[i].tint = 0xFFFFDA8Au; srows[i].extra = lastS.entMarkKind == 1 ? tr("ON", "actif") : tr("chosen", "choisi");
            }
            if (!strcmp(lastS.name, "searchma") && lastS.entry[i] == 13) srows[i].tint = 0xFFE8806Eu;   // Monstrosity : reddish, as the client draws it
            // Search : the value each criterion holds (Refine Search), the level fields (scchar / scmlev)
            if (search_value(lastS, lastS.entry[i], svals[i], sizeof(svals[i]))) { srows[i].extra = svals[i]; srows[i].tint = 0xFFFFDA8Au; }
            if (lastS.entMarkKind == 3 && lastS.entry[i] < 32 && ((lastS.entMarks >> lastS.entry[i]) & 1u)) srows[i].tint = 0xFFFFB45Au;   // the field being edited
            if (lastS.entry[i] == lastS.cursor) scur = i + 1;
        }
        float sw, sh; block_size(f, kN, srows, lastS.n, sw, sh);
        const bool form = is_form(lastS.name);
        if (form) form_size(f, kN, lastS, sw, sh);   // a fixed form : the field(s), OK / Cancel side by side
        const float tS = ease_out(((float)(now - g_mainSubB.openMs) / 1000.0f) / 0.07f);
        // in place : the menu's corner (its right edge at the game's place, its left edge on the left), its own size
        float sbx = C.muMainPos == 1 ? bx : snap(bx + bw - sw), sby = snap(by + (1.0f - tS) * 3.0f * S);
        if (sby + sh > f.screenH - 4.0f) sby = snap(f.screenH - 4.0f - sh); if (sby < 4.0f) sby = 4.0f;
        float suy = by0; if (suy + sh > f.screenH - 4.0f) suy = snap(f.screenH - 4.0f - sh); if (suy < 4.0f) suy = 4.0f;   // its rest place
        const int js = SM.open ? menu_place_at(f, __LINE__, sbx, sby, sbx, suy, sw, sh) : 0;   // in the menu's place (hidden then) ; while open only
        const float SA = g_mainSubB.vis * tS * (lastS.focused ? 1.0f : 0.62f);
        if (SA > 0.0f) {
            g_frameJoin = js;                                // form_draw / block_draw draw their box first
            const int srow = form ? form_draw(f, kN, g_mainSubB, lastS, sbx, sby, sw, sh, SA) : block_draw(f, kN, g_mainSubB, srows, lastS.n, scur, lastS.focused, sbx, sby, sw, sh, SA, dt);
            g_subXY[0] = sbx; g_subXY[1] = sby;
            if (!strcmp(lastS.name, "iuse")) { ynX = sbx; ynY = sby + sh; ynW = sw; }
            if (SM.open) {
                g_hit[6][0] = sbx / f.screenW; g_hit[6][1] = sby / f.screenH; g_hit[6][2] = sw / f.screenW; g_hit[6][3] = sh / f.screenH;
                const int ent = (srow >= 1 && srow <= lastS.n) ? lastS.entry[srow - 1] : 0;
                if (ent && srow != g_mainSubB.hoverRow && ent != SM.cursor) native_main_sub_hover(ent);
                g_mainSubB.hoverRow = srow;
                if (ent && f.mouse && f.mouse->clicked) native_main_sub_pick(ent);
            } else g_mainSubB.hoverRow = 0;
        }
        nextX = leftward ? sbx - gap : sbx + sw + gap;
    }
    {
    // the confirmation (sortyn : Auto-sort, Dispose) : Yes / No, under the block it answers, right edges level (the client's place)
    const NativeEntryMenu& YN = native_main_yn();
    static NativeEntryMenu lastY;
    if (YN.open) lastY = YN;
    if (block_ease(g_mainYnB, YN.open && ynX >= 0.0f, now, dt) && lastY.n && ynX >= 0.0f) {
        RowSpec yrows[4]; char ynums[4][12]; int ycur = 0; int yn = 0;
        for (int i = 0; i < lastY.n && yn < 4; ++i, ++yn) {
            const int e = lastY.entry[i];
            if (e == 1) yrows[yn].label = tr("Yes", "Oui");
            else if (e == 2) yrows[yn].label = tr("No", "Non");
            else { sprintf(ynums[yn], "#%d", e); yrows[yn].label = ynums[yn]; }
            yrows[yn].icon = IC_NONE; yrows[yn].sub = false; yrows[yn].off = false;
            if (e == lastY.cursor) ycur = yn + 1;
        }
        float yw, yh; block_size(f, kN, yrows, yn, yw, yh);
        const float tY = ease_out(((float)(now - g_mainYnB.openMs) / 1000.0f) / 0.07f);
        float ybx = C.muMainPos == 1 ? ynX : snap(ynX + ynW - yw), yby = snap(ynY + 3.0f * S + (1.0f - tY) * 3.0f * S);
        const int jy = menu_place_at(f, __LINE__, ybx, yby, ybx, snap(ynY + 3.0f * S), yw, yh);   // glued under the block it answers (theme kit)
        const float YA = g_mainYnB.vis * tY;
        if (YA > 0.0f) {
            g_frameJoin = jy;
            const int yrow = block_draw(f, kN, g_mainYnB, yrows, yn, ycur, lastY.focused, ybx, yby, yw, yh, YA, dt);
            if (YN.open) {
                g_hit[8][0] = ybx / f.screenW; g_hit[8][1] = yby / f.screenH; g_hit[8][2] = yw / f.screenW; g_hit[8][3] = yh / f.screenH;
                const int ent = (yrow >= 1 && yrow <= yn) ? lastY.entry[yrow - 1] : 0;
                if (ent && yrow != g_mainYnB.hoverRow && ent != YN.cursor) native_main_yn_hover(ent);
                g_mainYnB.hoverRow = yrow;
                if (ent && f.mouse && f.mouse->clicked) native_main_yn_pick(ent);
            } else g_mainYnB.hoverRow = 0;
        }
    }
    }
    // the list : only while no action menu is up (that one draws it beside itself)
    const NativeList& NL = native_list();
    static NativeList lastL;
    const bool mine = NL.open && chain && !native_action_menu().open;
    if (mine) lastL = NL;
    if (block_ease(g_mainListB, mine, now, dt)) {
        RowSpec lrows[32]; char mps[32][8]; float lw, lh;
        list_build(f, k, lastL, lrows, mps, lw, lh);
        const float tL = ease_out(((float)(now - g_mainListB.openMs) / 1000.0f) / 0.07f);
        // WHERE THE ACTION MENU'S LIST GOES (the client opens White Magic... at the same place from both menus -- user
        // 2026-11-18, "s'affiche au meme endroit que le menu magie du menu action") : above AioHUD's chat ; no chat box
        // shown : beside the categories
        float lx = 0.0f, ly = 0.0f;
        if (!place_on_chat(lw, lh, snap(4.0f * S), f.screenW, f.screenH, lx, ly)) { lx = leftward ? snap(nextX - lw) : snap(nextX); ly = by; }
        float luy = ly;                                      // its rest place (no slide), kept on screen as below
        ly = snap(ly + (1.0f - tL) * 3.0f * S);
        if (lx < 4.0f) lx = 4.0f; if (lx + lw > f.screenW - 4.0f) lx = snap(f.screenW - 4.0f - lw);
        if (ly + lh > f.screenH - 4.0f) ly = snap(f.screenH - 4.0f - lh); if (ly < 4.0f) ly = 4.0f;
        if (luy + lh > f.screenH - 4.0f) luy = snap(f.screenH - 4.0f - lh); if (luy < 4.0f) luy = 4.0f;
        const int jl = mine ? menu_place_at(f, __LINE__, lx, ly, lx, luy, lw, lh) : 0;   // while open only (theme kit)
        list_paint(f, k, g_mainListB, lastL, lrows, mine, lx, ly, lw, lh, g_mainListB.vis * tL, tL, dt, g_hit[7], jl);
        // ABILITIES' description : the client writes it to its HELP LINE -- shown by the top bar (topbar_draw), not in a box
        // above the list any more (the user 2026-11-20, "option 2" : one place for it, as in the game)
    }
}

void item_card_fold(const Frame& f) { card_fold(f); }   // hud.cpp, after every screen (the card is drawn by several)

// ======== THE TOP BAR (NU_HELP) : the client's titlewin + helpwind, in AioHUD's style (the user 2026-11-20 : "garde en
// haut, style AioHUD"). One box across the top : the menu name (gold) | the help line. The client's own behaviour : a
// spinner while it waits for the server, an over-wide line scrolls, the bar fades out ~3 s after its last text ========
namespace {
// the top bar's height (the macro bar hangs right under it)
float topbar_height(const Metrics& k) { const float pad = snap(10.0f * k.S) + k.bi; return snap(k.capH + 2.0f * pad - 2.0f * k.S); }
// ---- YOUR BUFF ICONS (the client's `buff`, NU_BUFF) : the party's icon set (the shared status atlas), where the client
// puts its own -- under the top bar, from its left (the user 2026-11-20 : "remplace-la avec nos icones de buff"). The
// game's order, 16 a row ; the name of the one under the mouse in a tip under it.
// //aio buffpreview (dev) : a full row set of 32 fake buffs in place of yours (the layout at its biggest)
bool g_buffPreview = false;
const unsigned short FAKE_BUFFS[32] = { 40, 41, 33, 42, 43, 116, 37, 36, 39, 251, 253, 195, 196, 197, 198, 199,
                                        214, 215, 216, 217, 218, 219, 220, 221, 222, 223, 180, 181, 182, 183, 184, 185 };
void buffbar_paint(const Frame& f) {
    const UiConfig& C = ui_config();
    if (!(C.nuHide & NU_BUFF) || !f.game || !f.font || f.screenH <= 0.0f || f.screenW <= 0.0f) return;
    const NativeBuffWin& W = native_buffwin();
    const bool wOk = W.open && W.uiW > 0 && W.uiH > 0;
    if (!wOk && !g_buffPreview) return;
    const GameState& g = *f.game;
    const unsigned short* ids = g_buffPreview ? FAKE_BUFFS : g.buffs;
    const int nb = g_buffPreview ? 32 : g.nbuff;
    if (!g_buffPreview && (!g.inGame || nb <= 0)) return;
    const u32 tex = buff_atlas_tex(f.dev); if (!tex) return;
    const Metrics k = metrics(f);
    const float S = k.S;
    const float z = 24.0f, gap = 2.0f;   // 3/4 of the atlas' 32 px cells, tight (the user 2026-11-20 : 1:1 "trop espaces", then "un peu plus petites")
    const int perRow = 16;
    // the client's place (its record : 142,48 units when it is not up) ; under AioHUD's top bar when that one is taller
    const float sx = wOk ? f.screenW / (float)W.uiW : 1.0f, sy = wOk ? f.screenH / (float)W.uiH : 1.0f;
    const float x0 = wOk ? snap((float)W.rx0 * sx) : snap(156.0f * S);
    float y0 = wOk ? snap((float)W.ry0 * sy) : snap(52.0f * S);
    if (C.nuHide & NU_HELP) { const float tb = snap(topbar_height(k) + 4.0f * S); if (y0 < tb) y0 = tb; }
    const u32 dev = f.dev; const MouseState* ms = f.mouse;
    float au, av, u0, v0; int hov = -1; float hx = 0.0f, hy = 0.0f;
    dTexQuadState(dev, tex);
    // THE PARTY'S ORDER (the user 2026-11-20 : "le meme rangement que ce qu'on a defini dans party") : the configured
    // group order, inside a group the arranged / built-in priority, the game's order where nothing is stated.
    // NOTHING hidden : this bar replaces the game's, which shows them all (2026-11-20 : Stoneskin, hidden from the party strip, vanished here too) -- the same keys as party.cpp. src[] keeps each icon's GAME index (the cursor).
    unsigned short ord[32], key[32]; int src[32], nOrd = 0;
    for (int j = 0; j < nb && j < 32; ++j) {
        const unsigned short id = ids[j]; if (id >= BUFF_COLS * BUFF_ATLAS_ROWS) continue;
        const unsigned char gq = buff_group(id);
        const unsigned short kk = (unsigned short)(((unsigned)C.buff_group_rank(gq) << 8) | buff_pri_effective(C, id));
        int q = nOrd++;
        for (; q > 0 && key[q - 1] > kk; --q) { key[q] = key[q - 1]; ord[q] = ord[q - 1]; src[q] = src[q - 1]; }
        key[q] = kk; ord[q] = id; src[q] = j;
    }
    // ... READ AS THE PARTY'S : the party lays a row from its RIGHT end (the first status nearest the name, party.cpp
    // "cell j's left edge = xr - (j+1)*bs") -> each row of 16 reversed here, so left to right reads the same (the user
    // 2026-11-20 : "ils sont pas ranges comme ceux de party"). The cursor's order is this drawn one.
    {   unsigned short o2[32]; int s2[32];
        for (int r0 = 0; r0 < nOrd; r0 += perRow) {
            const int c = nOrd - r0 < perRow ? nOrd - r0 : perRow;
            for (int q = 0; q < c; ++q) { o2[r0 + q] = ord[r0 + c - 1 - q]; s2[r0 + q] = src[r0 + c - 1 - q]; }
        }
        for (int q = 0; q < nOrd; ++q) { ord[q] = o2[q]; src[q] = s2[q]; }
    }
    // the time left under each icon : the server's own timers (0x063 type 9), the n-th timer of a status for its n-th
    // icon (two Marches = two timers). None (or a lasting one) : no text.
    int left[32]; for (int j = 0; j < 32; ++j) left[j] = -1;
    if (g_buffPreview) { static const int FL[8] = { 3540, 1712, 905, 300, 178, 61, 42, 7 }; for (int j = 0; j < nb && j < 32; ++j) left[j] = (j % 5 == 4) ? -1 : FL[j % 8]; }
    else {
        int nt = 0; const BuffTimer* bt = party().buff_timers(nt); const unsigned nowT = ffxi_now_tick();
        bool used[32] = { false };
        for (int j = 0; j < nb && j < 32; ++j)
            for (int t = 0; t < nt && t < 32; ++t) if (!used[t] && bt[t].id == ids[j]) {
                used[t] = true; const int d = (int)(bt[t].expiry - nowT);
                if (d > 0 && d < 60 * 60 * 60 * 24) left[j] = ticks_to_sec_ceil(d);
                break;
            }
    }
    Font* fo = f.font; const float lz = snap(k.tz * 0.86f), lh = snap(lz * 1.25f);
    const float tg = snap(7.0f * S), pitchY = z + tg + lh + gap;   // the time clear of the cursor's halo (the user 2026-11-20 : "trop pres")
    // the client's buff cursor (X / numpad +) : its GAME index -> where that icon sits in OUR order ; the order handed to
    // the model (its left / right walk it) -- only when every game icon is drawn (nothing hidden), else the client's own
    const bool curUp = !g_buffPreview && W.open && W.focused && W.cursor >= 0;
    if (!g_buffPreview) { if (nOrd == nb) native_buff_set_order(src, nOrd); else native_buff_set_order(0, 0); }
    int hovSrc = -1, curPos = -1;
    for (int i = 0; i < nOrd; ++i) {
        const int id = ord[i];
        const float x = snap(x0 + (float)(i % perRow) * (z + gap)), y = snap(y0 + (float)(i / perRow) * pitchY);
        buff_cell_uv(id, au, av, u0, v0);
        tquad(dev, x, y, z, z, u0, u0 + au, v0, v0 + av, 0xFFFFFFFFu, 0xFFFFFFFFu);
        if (ms && ms->focused && ms->x >= x && ms->x < x + z && ms->y >= y && ms->y < y + z + lh) { hov = id; hx = x; hy = y + z + tg + lh; hovSrc = src[i]; }
        if (curUp && src[i] == W.cursor) curPos = i;
        else if (g_buffPreview && i == ((int)f.t) % (nOrd > 0 ? nOrd : 1)) curPos = i;   // the preview : the cursor walks, one a second
    }
    dSetTex(dev, 0, 0); cs(dev);
    if (curPos >= 0) {                                                   // the cursor : the party's selection frame + a gold ring
        const float x = snap(x0 + (float)(curPos % perRow) * (z + gap)), y = snap(y0 + (float)(curPos / perRow) * pitchY);
        // GRAPHIC on any background (the user 2026-11-20 : "pas visible sur des fonds tres clairs") : a dark halo, a
        // thick gold ring pulsing (no hand : the user 2026-11-20, "la petite fleche blanche on n'en veut pas")
        const float o = snap(3.0f * S), bw = z + 2.0f * o, pul = 0.5f + 0.5f * sinf(f.t * 6.0f);
        rrect_stroke(dev, x - o - 2.0f * S, y - o - 2.0f * S, bw + 4.0f * S, bw + 4.0f * S, 6.0f * S, 0xD0000000u, 3.0f * S);
        rrect_clip_begin(dev, x - o, y - o, bw, bw, 4.0f * S); party_selframe(dev, x - o, y - o, bw, bw, f.t, 0.55f, 0); rrect_clip_end(dev);
        rrect_stroke(dev, x - o, y - o, bw, bw, 4.0f * S, with_alpha(0xFFFFD45Au, 0.75f + 0.25f * pul), 2.5f * S);
        rrect_stroke(dev, x - o + 2.5f * S, y - o + 2.5f * S, bw - 5.0f * S, bw - 5.0f * S, 3.0f * S, 0x90000000u, 1.0f * S);
        if (hov < 0) { hov = ord[curPos]; hx = x; hy = y + z + tg + lh; }   // its name, as the client shows it
    }
    {   static int wasCur = -2, said = 0;
        if (curUp && W.cursor != wasCur && said < 40) { ++said;
            char li[160]; int o = 0; for (int j = 0; j < nb && j < 32 && o < 140; ++j) o += _snprintf(li + o, 160 - o, "%s%u", j ? "," : "", ids[j]); li[159] = 0;
            char lo[160]; int p = 0; for (int j = 0; j < nOrd && p < 140; ++j) p += _snprintf(lo + p, 160 - p, "%s%u@%d", j ? "," : "", ord[j], src[j]); lo[159] = 0;
            const NativeTopBar& TB = native_topbar();
            windower::debug::log("BUFMAP cursor %d of %d : game list [%s] ; ours [%s] ; we ring pos %d (id %u) ; the game's help '%s'",
                W.cursor, W.count, li, lo, curPos, curPos >= 0 ? ord[curPos] : 0, TB.ok ? TB.help.text : "?"); }
        if (curUp) wasCur = W.cursor; else wasCur = -2; }
    // the mouse, while the cursor is up : over an icon -> the client's cursor there ; a click -> the client's cancel
    if (curUp && hovSrc >= 0) {
        if (hovSrc != W.cursor) native_buff_hover(hovSrc);
        if (ms->clicked) native_buff_pick(hovSrc);
    }
    if (curUp && nOrd > 0) {                                             // the bar takes the clicks while the cursor is up
        const int rows = (nOrd + perRow - 1) / perRow, cols = nOrd < perRow ? nOrd : perRow;
        g_hit[15][0] = x0 / f.screenW; g_hit[15][1] = y0 / f.screenH;
        g_hit[15][2] = ((float)cols * (z + gap)) / f.screenW; g_hit[15][3] = ((float)rows * pitchY) / f.screenH;
    }
    char tt[32][16]; bool has[32];
    for (int i = 0; i < nOrd; ++i) {                                     // the pills first (one pass), then the text
        const int sec = left[src[i]]; has[i] = sec >= 0; if (!has[i]) continue;
        if (sec >= 3600) _snprintf(tt[i], 16, "%dh", sec / 3600);
        else if (sec >= 60) _snprintf(tt[i], 16, "%dm", sec / 60);
        else _snprintf(tt[i], 16, "%d", sec);
        tt[i][15] = 0;
        const float x = snap(x0 + (float)(i % perRow) * (z + gap)), y = snap(y0 + (float)(i / perRow) * pitchY);
        rrect(dev, x, snap(y + z + tg), z, snap(lh - 1.0f * S), 3.0f * S, 0xC8080B10u, 0xC8080B10u);
    }
    fo->begin(dev);
    for (int i = 0; i < nOrd; ++i) {
        if (!has[i]) continue;
        const int sec = left[src[i]];
        const float x = snap(x0 + (float)(i % perRow) * (z + gap)), y = snap(y0 + (float)(i / perRow) * pitchY);
        const u32 col = sec <= 10 ? 0xFFFF6A5Au : sec < 60 ? 0xFFFFDA8Au : 0xFFFFFFFFu;   // red the last 10 s, gold the last minute
        fo->draw_lc(dev, snap(x + (z - fo->measure(tt[i], lz)) * 0.5f), snap(y + z + tg + lh * 0.5f), tt[i], lz, col, 0xFF000000u, k.ow * 1.6f);
    }
    if (hov >= 0) {                                                      // the tip : the status's name
        const char* nm = buff_status_name_real((unsigned)hov); if (!nm || !nm[0]) return;
        const float tz = k.tz * 0.9f, pad = snap(6.0f * S);
        const float tw = snap(fo->measure(nm, tz) + 2.0f * pad), th = snap(k.capH + pad);
        float tx = hx; if (tx + tw > f.screenW - 4.0f) tx = snap(f.screenW - 4.0f - tw);
        const float ty = snap(hy + 4.0f * S);
        rrect(dev, tx, ty, tw, th, 5.0f * S, 0xE0141A24u, 0xE00C1018u);
        rrect_stroke(dev, tx, ty, tw, th, 5.0f * S, 0x50C8D6EAu, 1.0f);
        fo->begin(dev); fo->draw_lc(dev, snap(tx + pad), snap(ty + th * 0.5f), nm, tz, 0xFFE4EAF2u, 0xC8000000u, k.ow);
    }
}
// //aio netpreview (dev) : a SCRIPTED DEMO of the box, a 16 s loop telling a zone-in (the user 2026-11-25 : "everything,
// like a demo", for a GIF) : 0-3 s both bags and gear loading, no signal yet ; 3-5.5 the bags alone, weak ; 5.5-8 all
// loaded, fair, the FPS ; 8-11 three mails arrive ; 11-14 a GM notice joins them ; 14-16 back to the FPS, good signal.
// g_netDemoSocial : how much of the notice (vs the FPS) the demo shows now, faded over 0.35 s at each switch.
bool g_netPreview = false; float g_netDemoSocial = -1.0f;
NativeNet net_shown(const Frame& f) {
    NativeNet N = native_net();
    g_netDemoSocial = -1.0f;
    if (!g_netPreview) return N;
    const float t = fmodf(f.t, 16.0f);
    N.ok = true; N.loadOk = true;
    N.bagsLoading = t < 5.5f; N.gearLoading = t < 3.0f;
    N.score  = t < 3.0f ? 0 : (t < 5.5f ? 20 : (t < 14.0f ? 45 : 100));
    N.pingMs = t < 3.0f ? -1 : (t < 5.5f ? 520 : (t < 14.0f ? 310 : 140));
    N.mail = t >= 8.0f ? 3 : 0; N.gm = t >= 11.0f ? 1 : 0;
    const float fd = 0.35f;
    g_netDemoSocial = t < 8.0f ? 0.0f : (t < 8.0f + fd ? (t - 8.0f) / fd : (t < 14.0f - fd ? 1.0f : (t < 14.0f ? (14.0f - t) / fd : 0.0f)));
    return N;
}
// the counts' text : "Mail n", "GM n", or both ; false when there is none
bool net_counts(const NativeNet& N, char* out, int cap) {
    if (N.mail > 0 && N.gm > 0) _snprintf(out, cap, "Mail %d  GM %d", N.mail, N.gm);
    else if (N.gm > 0) _snprintf(out, cap, "GM %d", N.gm);
    else if (N.mail > 0) _snprintf(out, cap, "Mail %d", N.mail);
    else { out[0] = 0; return false; }
    out[cap - 1] = 0; return true;
}
// the connection box's width : what it shows, no more (the user 2026-11-20 : "un espace a droite, pourquoi ?") -- the
// signal, the ping (room for "000 ms" : steady while it moves), the mail / GM count only when there is one
float netbox_inset(const Metrics& k) { return snap(4.0f * k.S); }   // the gap between the box and the screen's right edge
float netbox_width(const Frame& f, const Metrics& k) {
    const float S = k.S, pad = snap(10.0f * S) + k.bi, tz = k.tz;
    const NativeNet N = net_shown(f);
    float w = pad + 4.0f * (snap(4.0f * S) + snap(2.0f * S)) + 6.0f * S;
    char sc[16]; if (N.pingMs >= 1000) sprintf(sc, "%d ms", N.pingMs); else sprintf(sc, "000 ms");
    w += f.font ? f.font->measure(sc, tz) : 60.0f * S;
    // the ping's room is FIXED ("000 ms" : steady while it moves) ; the rest follows the content, below
    // what the box shows NOW (the user 2026-11-25 : "on a encore plein de place" -- the fixed room for 200 mails + 99 GM
    // notices stood empty almost always) : the FPS alone, or, while a notice is up, the envelope / GM bubble with their
    // REAL counts (or the FPS if wider : the two alternate, the box must not breathe every 4 s). The width EASES to it
    // (~0.25 s) and the right edge stays on the screen's : the capsule stretches leftwards when a mail arrives.
    const float isz = snap(tz * 1.45f), nz = tz * 0.95f;
    const float fpsw = f.font ? f.font->measure("000 fps", nz) : 60.0f * S;
    float icons = 0.0f;
    for (int q = 0; q < 2; ++q) {
        const int n = q == 0 ? N.mail : N.gm;
        if (n <= 0) continue;
        char nb[8]; sprintf(nb, "%d", n);
        if (icons > 0.0f) icons += 6.0f * S;
        icons += (q == 0 ? snap(isz * 1.15f) : snap(isz * 1.3f)) + 2.0f * S + (f.font ? f.font->measure(nb, nz) : 10.0f * S);
    }
    w += 8.0f * S + (icons > fpsw ? icons : fpsw) + pad;
    static float cur = 0.0f, lastT = -1.0f;
    if (cur <= 0.0f) cur = w;
    if (f.t != lastT) {                                        // one step per frame (the top bar asks too)
        const float dt = lastT < 0.0f ? 0.0f : f.t - lastT; lastT = f.t;
        const float kk = dt > 0.0f ? 1.0f - expf(-dt * 14.0f) : 0.0f;
        cur += (w - cur) * kk;
        if (fabsf(w - cur) < 0.5f) cur = w;
    }
    return snap(cur);
}
Block g_tbB; unsigned g_tbMs = 0, g_tbLastText = 0;
char g_tbShown[2][0xCC]; float g_tbScrollT0 = 0.0f;
// one line of text in [x, x+w] at height cy : fits -> as is ; wider -> after a 1.5 s pause it scrolls, looping
void tb_text_line(const Frame& f, const Metrics& k, const char* s, float x, float cy, float w, float tz, u32 col, float A, float t0) {
    Font* fo = f.font; const u32 dev = f.dev; const u32 sh = with_alpha(0xC8000000u, A);
    const float tw = fo->measure(s, tz);
    if (tw <= w) { fo->begin(dev); fo->draw_lc(dev, x, cy, s, tz, with_alpha(col, A), sh, k.ow); return; }
    const float gapW = snap(40.0f * k.S), loop = tw + gapW, speed = 45.0f * k.S, el = f.t - t0 - 1.5f;
    const float off = el > 0.0f ? fmodf(el * speed, loop) : 0.0f;
    const float h = snap(tz * 1.6f);
    rrect_clip_begin(dev, x, cy - h * 0.5f, w, h, 0.0f);
    fo->begin(dev);
    fo->draw_lc(dev, snap(x - off), cy, s, tz, with_alpha(col, A), sh, k.ow);
    fo->draw_lc(dev, snap(x - off + loop), cy, s, tz, with_alpha(col, A), sh, k.ow);
    rrect_clip_end(dev);
}
// the client's "busy" spinner : three dots pulsing in turn
void tb_spinner(const Frame& f, float x, float cy, float S, float A) {
    for (int i = 0; i < 3; ++i) {
        const float ph = f.t * 3.0f - (float)i * 0.35f, p = 0.5f + 0.5f * sinf(ph * 3.14159f);
        const float r = snap(2.5f * S);
        rrect(f.dev, snap(x + (float)i * 9.0f * S), snap(cy - r), 2.0f * r, 2.0f * r, r, with_alpha(0xFFFFDA8Au, A * (0.35f + 0.65f * p)), with_alpha(0xFFFFDA8Au, A * (0.35f + 0.65f * p)));
    }
}
}
void buffbar_draw(const Frame& f) { buffbar_paint(f); }   // (the paint lives in the anon namespace, beside topbar_height)
void topbar_draw(const Frame& f) {
    const UiConfig& C = ui_config();
    if (!(C.nuHide & NU_HELP) || !f.font || f.screenH <= 0.0f || f.screenW <= 0.0f) { g_tbB.vis = 0.0f; g_tbB.wasOpen = false; return; }
    const unsigned now = GetTickCount();
    float dt = g_tbMs ? (float)(now - g_tbMs) / 1000.0f : 0.0f; g_tbMs = now; if (dt > 0.1f) dt = 0.1f;
    const NativeTopBar& T = native_topbar();
    const bool any = T.ok && (T.title.up || T.help.up) && (T.title.text[0] || T.help.text[0] || T.title.busy || T.help.busy);
    if (any) g_tbLastText = now;
    const bool up = T.ok && g_tbLastText && now - g_tbLastText < 3000;   // the client's idle fade : ~3 s after its last text
    if (T.ok && (strcmp(g_tbShown[0], T.title.text) || strcmp(g_tbShown[1], T.help.text))) {   // a new text : its scroll starts over
        lstrcpynA(g_tbShown[0], T.title.text, sizeof(g_tbShown[0])); lstrcpynA(g_tbShown[1], T.help.text, sizeof(g_tbShown[1])); g_tbScrollT0 = f.t;
    }
    if (!block_ease(g_tbB, up, now, dt)) return;
    const Metrics k = metrics(f);
    const float S = k.S, tz = k.tz, pad = snap(10.0f * S) + k.bi;
    const u32 dev = f.dev;
    const float A = g_tbB.vis; if (A <= 0.0f) return;
    // the very top of the screen, its whole width (the user 2026-11-20 : "tout en haut, toute la largeur")
    // ... less the connection box on its right when AioHUD draws it (the user 2026-11-20 : "a droite de la barre d'infos")
    const float bx = 0.0f, by = 0.0f, bh = topbar_height(k);
    const float bw = snap(f.screenW - ((C.nuHide & NU_NET) && net_shown(f).ok ? netbox_width(f, k) + netbox_inset(k) + 2.0f * S : 0.0f));
    if (bw < 200.0f * S) return;
    draw_themed_box(dev, f.skin, bx, by, bw, bh, C.muBox, A, S);
    const float cy = snap(by + bh * 0.5f);
    // the menu name (titlewin) : a fixed column, as wide as the client's (cut with its scroll when longer)
    const float titleW = snap(130.0f * S), L = bx + pad;
    const NativeTopLine& tl = T.title; const NativeTopLine& hl = T.help;
    if (tl.busy) tb_spinner(f, L, cy, S, A);
    else if (tl.text[0]) tb_text_line(f, k, tl.text, L, cy, titleW, tz * 1.02f, 0xFFFFDA8Au, A, g_tbScrollT0);
    // the separator, then the help line (helpwind)
    const float sepX = snap(L + titleW + 8.0f * S);
    rrect(dev, sepX, snap(by + pad), 1.0f, snap(bh - 2.0f * pad), 0.0f, with_alpha(0x40C8D6EAu, A), with_alpha(0x40C8D6EAu, A));
    const float hx = snap(sepX + 10.0f * S), hw = bx + bw - pad - hx;
    if (hl.busy) tb_spinner(f, hx, cy, S, A);
    else if (hl.text[0]) tb_text_line(f, k, hl.text, hx, cy, hw, tz, 0xFFE4EAF2u, A, g_tbScrollT0);
}

// ======== THE CONNECTION BOX (NU_NET) : the client's netstat, top right, beside the top bar. Always shown (it is a
// status, not a help line) : the signal (4 bars, the client's bands 0 / < 30 / < 60 / >= 60), the score, and the
// PlayOnline mail / GM notice counts when there are any ========
// The FPS hand-over to the updater addon : plugins\AioHud\data\fps_<character>.txt holds a counter rewritten every 5 s
// while this box shows the FPS ; addons\aioupdate sends "showfps 0" while it keeps moving and "showfps 1" once it stops
// (box off, plugin unloaded, or crashed : a counter that no longer moves for 15 s is stale). Per character : dual-box
// clients share the data folder. File I/O only every 5 s, or when the state flips.
void net_fps_flag(const Frame& f, bool on) {
    static bool was = false; static unsigned lastW = 0, ctr = 0;
    const unsigned now = GetTickCount();
    if (on) {
        if (was && now - lastW < 5000) return;
        const char* nm = (f.game && f.game->me.name[0]) ? f.game->me.name : 0;
        if (!nm) return;
        char rel[64]; _snprintf(rel, sizeof(rel), "data\\fps_%s.txt", nm); rel[sizeof(rel) - 1] = 0;
        strncpy(g_fpsFile, plugin_path_r(rel), sizeof(g_fpsFile) - 1);
        FILE* fp = fopen(g_fpsFile, "w");
        if (fp) { fprintf(fp, "%u\n", ++ctr); fclose(fp); was = true; lastW = now; }
        return;
    }
    if (!was) return;
    if (g_fpsFile[0]) remove(g_fpsFile);
    was = false;
}

void netbox_draw(const Frame& f) {
    const UiConfig& C = ui_config();
    if (!(C.nuHide & NU_NET) || !f.font || f.screenH <= 0.0f || f.screenW <= 0.0f) { net_fps_flag(f, false); return; }
    const NativeNet N = net_shown(f);
    if (!N.ok) { net_fps_flag(f, false); return; }
    const Metrics k = metrics(f);
    const float S = k.S, tz = k.tz, pad = snap(10.0f * S) + k.bi;
    Font* fo = f.font; const u32 dev = f.dev; const float A = 1.0f; const u32 sh = with_alpha(0xC8000000u, A);
    // inset from the screen's right edge : flush, the frame's right side was off-screen (the user 2026-11-25 : "coupe a droite")
    const float bw = netbox_width(f, k), bh = topbar_height(k), bx = snap(f.screenW - bw - netbox_inset(k)), by = 0.0f;
    draw_themed_box(dev, f.skin, bx, by, bw, bh, C.muBox, A, S);
    const int band = N.score <= 0 ? 0 : (N.score < 30 ? 1 : (N.score < 60 ? 2 : 3));
    static const u32 COL[4] = { 0xFF6E7C8Eu, 0xFFE86A5Au, 0xFFE8C45Au, 0xFF7FD67Fu };   // none, weak, fair, good
    // the signal : 4 bars rising ; the lit ones = the band (+1 bar at 100)
    const int lit = band == 0 ? 0 : (N.score >= 100 ? 4 : band);
    const float bwid = snap(4.0f * S), bgap = snap(2.0f * S), maxH = snap(bh - 2.0f * pad + 2.0f * S), x0 = snap(bx + pad), yb = snap(by + bh - pad + 1.0f * S);
    for (int i = 0; i < 4; ++i) {
        const float h = snap(maxH * (0.35f + 0.65f * (float)i / 3.0f));
        const u32 c = i < lit ? COL[band] : 0x40FFFFFFu;
        rrect(dev, snap(x0 + (float)i * (bwid + bgap)), snap(yb - h), bwid, h, 1.0f * S, with_alpha(c, A), with_alpha(c, A));
    }
    // the PING (the user 2026-11-20 : "j'aimerais bien le ping") : green < 200 ms, yellow < 400, red beyond ; "--" unknown
    const float cy = snap(by + bh * 0.5f);
    float tx = snap(x0 + 4.0f * (bwid + bgap) + 6.0f * S);
    char sc[16]; if (N.pingMs >= 0) sprintf(sc, "%d ms", N.pingMs); else sprintf(sc, "-- ms");
    const u32 pc = N.pingMs < 0 ? 0xFF93A6BEu : (N.pingMs < 200 ? 0xFF7FD67Fu : (N.pingMs < 400 ? 0xFFE8C45Au : 0xFFE86A5Au));
    fo->begin(dev);
    fo->draw_lc(dev, tx, cy, sc, tz, with_alpha(pc, A), sh, k.ow);
    tx = snap(tx + fo->measure(N.pingMs >= 1000 ? sc : "000 ms", tz) + 8.0f * S);
    // the PlayOnline MAIL waiting and the GM notices : the envelope / the "GM" bubble, their count beside (the client's
    // own icons, netstat page 0) ; the plain text when the icon atlas is not loaded yet
    const u32 netTex = ensure_raw_tex_mip(dev, g_netTex, g_netR, plugin_path_r("assets\\net_icons.raw"), 512, 128);
    const u32 metal = with_alpha(box_style_border_color(dev, f.skin, C.muBox) | 0xFF000000u, A);
    auto niCell = [&](int cell, float x, float y, float sz, float ang, u32 col) {   // one cell of the atlas, centred
        const float u0 = (float)(cell % 8) / 8.0f, v0 = (float)(cell / 8) / 2.0f;
        tquad_rot_uv(dev, snap(x), snap(y), sz, sz, ang, u0, v0, u0 + 0.125f, v0 + 0.5f, col);
    };
    const float isz = snap(tz * 1.45f);
    // THE FPS (the user 2026-11-24 : Windower's own counter sat in this corner -- it moves in here, and the updater addon
    // hides Windower's while this box is up, net_fps_flag) : frames counted over half a second. With a mail / GM
    // notice it ALTERNATES with the notice, 4 s each, crossfaded.
    static unsigned fpsT0 = 0, fpsN = 0; static int fpsShown = 0;
    { const unsigned now = GetTickCount(); ++fpsN;
      if (!fpsT0) fpsT0 = now;
      else if (now - fpsT0 >= 500) { fpsShown = (int)((fpsN * 1000u + (now - fpsT0) / 2) / (now - fpsT0)); fpsN = 0; fpsT0 = now; } }
    const bool social = N.mail > 0 || N.gm > 0;
    // 0..4 s the notice, 4..8 s the FPS, each fading in / out over 0.35 s ; no notice -> the FPS alone
    const float ph = fmodf(f.t, 8.0f), half = ph < 4.0f ? ph : ph - 4.0f, fd = 0.35f;
    const float env = half < fd ? half / fd : (half > 4.0f - fd ? (4.0f - half) / fd : 1.0f);
    float aS = social ? (ph < 4.0f ? env : 0.0f) : 0.0f;
    float aFin = social ? (ph >= 4.0f ? env : 0.0f) : 1.0f;
    if (g_netDemoSocial >= 0.0f) {                             // the demo's own script : OUT, then IN (a crossfade overlapped "3" and "58 fps")
        const float v = social ? g_netDemoSocial : 0.0f;
        aS = v > 0.5f ? (v - 0.5f) * 2.0f : 0.0f; aFin = v < 0.5f ? 1.0f - v * 2.0f : 0.0f;
    }
    if (aFin > 0.0f && fpsShown > 0) {
        char fs[16]; sprintf(fs, "%d fps", fpsShown);
        const u32 fc = fpsShown >= 50 ? 0xFFD8E0EAu : (fpsShown >= 30 ? 0xFFE8C45Au : 0xFFE86A5Au);   // ~60 fine ; under 30 red
        fo->begin(dev);
        fo->draw_lc(dev, tx, cy, fs, tz * 0.95f, with_alpha(fc, A * aFin), with_alpha(0xC8000000u, A * aFin), k.ow);
    }
    const float AS = A * aS;
    if (AS > 0.0f && netTex) {
        float sx = tx;
        for (int q = 0; q < 2; ++q) {
            const int n = q == 0 ? N.mail : N.gm;
            if (n <= 0) continue;
            const u32 fillC = with_alpha(q == 0 ? 0xFF8FC8FFu : 0xFFFFB98Au, AS);
            // the GM bubble : its letters live in the FRAME layer, so the bubble is filled DARK and the frame takes the
            // bright colour (a light fill under metal letters read as a blank bubble) ; and a size up, the letters are small
            const float s1 = q == 0 ? snap(isz * 1.15f) : snap(isz * 1.3f);   // the flat envelope reads smaller than the round bubble
            dTexQuadState(dev, netTex, true, true);
            niCell(q == 0 ? 0 : 2, sx + s1 * 0.5f, cy, s1, 0.0f, q == 0 ? fillC : with_alpha(0xFF3C2414u, AS));
            niCell(q == 0 ? 1 : 3, sx + s1 * 0.5f, cy, s1, 0.0f, q == 0 ? with_alpha(metal, AS) : fillC);
            dSetTex(dev, 0, 0);
            char nb[8]; sprintf(nb, "%d", n);
            fo->begin(dev);
            fo->draw_lc(dev, snap(sx + s1 + 2.0f * S), cy, nb, tz * 0.95f, fillC, with_alpha(0xC8000000u, AS), k.ow);
            sx = snap(sx + s1 + 2.0f * S + fo->measure(nb, tz * 0.95f) + 6.0f * S);
        }
    } else if (AS > 0.0f) {
        char m[24];
        if (net_counts(N, m, sizeof(m)))
            fo->draw_lc(dev, tx, cy, m, tz * 0.95f, with_alpha(N.gm > 0 ? 0xFFFFB98Au : 0xFF8FC8FFu, AS), with_alpha(0xC8000000u, AS), k.ow);
    }
    net_fps_flag(f, true);
    // THE ZONE-IN LOADING MARKS, just LEFT of the box (where the client puts them, at the end of its help bar, which
    // NU_HELP hides ; the user 2026-11-24) : a bag while no item bag has finished loading, a sword while the equipment has not. Shown only
    // while loading, pulsing amber -- the client's own animated icons say "not ready yet" the same way.
    if (N.loadOk && (N.bagsLoading || N.gearLoading)) {
        // each mark is a small WATCH SUBDIAL, of a piece with the minimap's Dial (the user 2026-11-25 : "with the quality of
        // our map module, can't you do better ?") : a soft drop shadow, a rim in the box theme's metal (lit on top, in
        // shade below), a recessed dark face with its inner shadow, a faint track, a gold COMET sweeping it whose head
        // glows, and the bag / sword at the centre, breathing. All vector (colour-quad state) but the icon.
        const float cell = snap(bh - 2.0f * S), z = 0.72f * cell, R = cell * 0.5f, gap = snap(3.0f * S);
        const float p = 0.80f + 0.20f * sinf(f.t * 3.2f);
        const u32 c = with_alpha(0xFFE8C45Au, A * p);
        auto mixc = [](u32 x, u32 y, float t) -> u32 {            // RGB blend x -> y (alpha from x)
            const float u = 1.0f - t; u32 o = x & 0xFF000000u;
            for (int sh2 = 0; sh2 < 24; sh2 += 8) o |= (u32)((float)((x >> sh2) & 0xFF) * u + (float)((y >> sh2) & 0xFF) * t + 0.5f) << sh2;
            return o;
        };
        const u32 mBase = metal | 0xFF000000u, mLite = mixc(mBase, 0xFFFFFFFFu, 0.45f), mDark = mixc(mBase, 0xFF000000u, 0.55f);
        float ix = snap(bx - gap - R);                                 // OUTSIDE, left of the box : the end of the help bar, as the client
        for (int m = 0; m < 2; ++m) {
            if (m == 0 ? !N.bagsLoading : !N.gearLoading) continue;
            if (netTex) {
                const float rim = 1.6f * S, rf = R - rim;               // the rim's width, the face's radius
                disc_glow(dev, ix, cy + 1.0f * S, R - 0.5f * S, with_alpha(0x70000000u, A), 3.0f * S);   // drop shadow
                disc(dev, ix, cy, R, with_alpha(mDark, A));                                              // the rim, in shade
                ring_sector(dev, ix, cy, rf, R, 3.1416f + 0.25f, 6.2832f - 0.25f, with_alpha(mLite, A * 0.85f));   // lit from above
                ring_sector(dev, ix, cy, rf, R, 0.55f, 3.1416f - 0.55f, with_alpha(mixc(mBase, 0xFF000000u, 0.75f), A * 0.8f));
                disc(dev, ix, cy, rf, with_alpha(0xFF10141Au, A));                                       // the recessed face
                ring_sector(dev, ix, cy, rf - 2.2f * S, rf, 3.1416f, 6.2832f, with_alpha(0x66000000u, A));   // its inner shadow
                // the track and the comet (250 degrees, overlapping sectors tail-first : no seams), then the glowing head
                const float ro = rf - 1.2f * S, ri = ro - 2.2f * S, head = f.t * 4.2f + (float)m * 1.6f;
                ring_sector(dev, ix, cy, ri, ro, 0.0f, 6.2832f, with_alpha(0x1EFFFFFFu, A));
                for (int g = 0; g < 12; ++g) {
                    const float u1 = (float)(g + 1) / 12.0f, span = 4.36f;
                    ring_sector(dev, ix, cy, ri, ro, head - span * (1.0f - (float)g / 12.0f), head, with_alpha(0xFFE8B850u, A * 0.15f * u1));
                }
                const float hr = (ri + ro) * 0.5f, hx = ix + hr * cosf(head), hy = cy + hr * sinf(head);
                disc_glow(dev, hx, hy, 1.1f * S, with_alpha(0xB0FFD98Au, A), 3.5f * S);
                disc(dev, hx, hy, 1.3f * S, with_alpha(0xFFFFF4D6u, A));
                dTexQuadState(dev, netTex, true, true);
                niCell(m == 0 ? 4 : 6, ix, cy, cell * 0.50f, 0.0f, c);
                niCell(m == 0 ? 5 : 7, ix, cy, cell * 0.50f, 0.0f, with_alpha(mLite, A));
                dSetTex(dev, 0, 0);
            } else draw_icon(dev, m == 0 ? IC_BAG : IC_SWORD, ix, cy, z, S, c);
            ix = snap(ix - cell - gap);
        }
    }
}
bool net_preview_toggle() { g_netPreview = !g_netPreview; return g_netPreview; }
bool buff_preview_toggle() { g_buffPreview = !g_buffPreview; return g_buffPreview; }

// ======== THE PLAYER TRADE (NU_TRADE) -- ONE PANEL (the user 2026-11-20 : "moderne, propre" ; side by side ; the gil
// typed ; OK held back after a partner's change). The client's windows (trade, gift, itemctrl, moneyctr, money,
// iteminfo) stay open and parked : this panel shows their state and drives them with their OWN events, so the game
// pad / keyboard keep working and the client's cursor is always shown (the column it is in is ringed).
//   header  : "Trade with <partner>" ; a warning when the partner changed something (3 s)
//   columns : You | <partner> -- the name + a status pill (In progress / Accepted), the 8 slots (icon, count ; a slot the
//             partner just changed glows ; hover a filled one of yours : x = remove), the gil (yours : a click opens it,
//             then it is a FIELD : the digits typed, or the client's value with its edited digit, - / +)
//   footer  : your balance (or the typing hint), Cancel / OK (OK greyed for 1 s after a partner's change -- mouse only)
//   the quantity : a bubble under the slot being filled (- value / max +, 1, All) ; the item card : a tooltip on the
//   slot under the mouse (or the client's cursor), only when it holds an item ; the end : a summary for 6 s
namespace {
Block g_trB; unsigned g_trMs = 0; NativeTrade g_trLast;
char g_tgQ[12] = { 0 }; volatile long g_tgQn = 0, g_tgKeysOn = 0, g_tgEnter = 0;   // the gil typed (key thread -> draw)
bool g_tgAte[256] = { false };
unsigned g_trPeerMs = 0, g_trSlotMs[9] = { 0 }; NativeTradeSlot g_trPrev[9]; bool g_trPrevOk = false;
unsigned g_trToastMs = 0; NativeTrade g_trToast; bool g_trWasOpen = false;
}
float g_trBox[4] = { 0, 0, 0, 0 };   // the panel as drawn (last frame) : the Items picker docks beside it
bool trade_key(int dik, unsigned ch, bool pressed, bool mod) {
    if (!pressed) { if (dik >= 0 && dik < 256 && g_tgAte[dik]) { g_tgAte[dik] = false; return true; } return false; }
    if (!g_tgKeysOn || mod) return false;
    if (dik >= 0x47 && dik <= 0x53) return false;                       // the numpad : the game's cursor keys
    long n = g_tgQn; bool mine = false;
    if (ch >= '0' && ch <= '9') { if (n < 10 && !(n == 0 && ch == '0')) { g_tgQ[n++] = (char)ch; g_tgQ[n] = 0; } mine = true; }
    else if (dik == 0x0E) { if (n > 0) { g_tgQ[--n] = 0; mine = true; } }                      // Backspace
    else if (dik == 0x01) { if (n > 0) { n = 0; g_tgQ[0] = 0; mine = true; } }                  // Esc : clears the typing first
    else if ((dik == 0x1C || dik == 0x9C) && n > 0) { g_tgEnter = 1; mine = true; }            // Enter : the draw writes it and validates
    if (!mine) return false;
    g_tgQn = n;
    if (dik >= 0 && dik < 256) g_tgAte[dik] = true;
    return true;
}
void trade_draw(const Frame& f) {
    const UiConfig& C = ui_config();
    if (!(C.nuHide & NU_TRADE) || !f.font || f.screenH <= 0.0f || f.screenW <= 0.0f) { g_trB.vis = 0.0f; g_trB.wasOpen = false; g_tgKeysOn = 0; return; }
    const unsigned now = GetTickCount();
    float dt = g_trMs ? (float)(now - g_trMs) / 1000.0f : 0.0f; g_trMs = now; if (dt > 0.1f) dt = 0.1f;
    const NativeTrade& T0 = native_trade();
    // the partner's changes : which slots, when (no flash for what was there at the opening)
    if (T0.open) {
        if (!g_trWasOpen || !g_trPrevOk) { memcpy(g_trPrev, T0.theirs, sizeof(g_trPrev)); g_trPrevOk = true; for (int s = 0; s < 9; ++s) g_trSlotMs[s] = 0; g_trPeerMs = 0; }
        for (int s = 0; s < 9; ++s) if (T0.theirs[s].id != g_trPrev[s].id || T0.theirs[s].count != g_trPrev[s].count) { g_trSlotMs[s] = now; g_trPeerMs = now; g_trPrev[s] = T0.theirs[s]; }
    }
    // the end : completed (P state 7, the last 0x022 type 9) -> the summary of the last state seen
    if (g_trWasOpen && !T0.open) {
        const NativeTradeEnd& E = native_trade_end();
        // (an NPC trade has no such end : no summary)
        if (!g_trLast.npc && E.state == 7 && E.lastType == 9) { g_trToast = g_trLast; g_trToastMs = now; windower::debug::log("NATIVEUI trade : completed with '%s'", g_trLast.partner); }
        g_trPrevOk = false; g_tgQn = 0; g_tgQ[0] = 0;
    }
    g_trWasOpen = T0.open;
    if (T0.open) g_trLast = T0;
    const Metrics k = metrics(f);
    const float S = k.S, pad = snap(10.0f * S) + k.bi, tz = k.tz;
    Font* fo = f.font; const u32 dev = f.dev;
    const u32 GOLD = 0xFFFFDA8Au, DIM = 0xFF93A6BEu, TXT = 0xFFE4EAF2u, GREEN = 0xFF7FD67Fu, WARN = 0xFFFFB45Au;
    // ---- the summary, after a completed trade (6 s, fading) ----
    if (g_trToastMs && now - g_trToastMs < 6000u && !T0.open) {
        const float a = (now - g_trToastMs) > 5000u ? 1.0f - (float)(now - g_trToastMs - 5000u) / 1000.0f : 1.0f;
        const NativeTrade& Q = g_trToast;
        char got[200] = "", gave[200] = ""; int go = 0, gv = 0;
        auto list = [&](const NativeTradeSlot* sl, char* out, int& o) {
            if (sl[0].count) { char g[24]; fmt_thousands(g, sl[0].count); o += _snprintf(out + o, 200 - o, "%s G", g); }
            for (int s = 1; s <= 8 && o < 180; ++s) if (sl[s].id) { const char* nm = item_display_name(sl[s].id); o += _snprintf(out + o, 200 - o, "%s%s%s", o ? ", " : "", nm ? nm : "?", ""); if (sl[s].count > 1 && o < 190) o += _snprintf(out + o, 200 - o, " x%u", sl[s].count); }
            if (!o) o = _snprintf(out, 200, "%s", tr("nothing", "rien"));
            out[199] = 0;
        };
        list(Q.theirs, got, go); list(Q.mine, gave, gv);
        const float sx0 = Q.uiW > 0 ? f.screenW / (float)Q.uiW : 1.0f, sy0 = Q.uiH > 0 ? f.screenH / (float)Q.uiH : 1.0f;
        const float tx = snap((float)Q.rx0 * sx0), ty = snap((float)Q.ry0 * sy0), tw = snap(420.0f * S), th = snap(pad * 2.0f + k.capH * 3.2f);
        draw_themed_box(dev, f.skin, tx, ty, tw, th, C.muBox, a, S);
        const u32 sh = with_alpha(0xC8000000u, a);
        char head[64]; _snprintf(head, sizeof(head), tr("Trade with %s : done", "\xC3\x89""change avec %s : termin\xC3\xA9"), Q.partner); head[sizeof(head) - 1] = 0;
        char l1[230], l2[230]; _snprintf(l1, sizeof(l1), tr("Received : %s", "Re\xC3\xA7u : %s"), got); _snprintf(l2, sizeof(l2), tr("Given : %s", "Donn\xC3\xA9 : %s"), gave); l1[229] = l2[229] = 0;
        char c1[96], c2[96]; fo->begin(dev);
        fo->draw_lc(dev, tx + pad, snap(ty + pad + k.capH * 0.5f), head, tz, with_alpha(GREEN, a), sh, k.ow);
        fo->draw_lc(dev, tx + pad, snap(ty + pad + k.capH * 1.6f), fit_text(fo, l1, tz * 0.92f, tw - 2.0f * pad, c1, sizeof(c1)), tz * 0.92f, with_alpha(TXT, a), sh, k.ow);
        fo->draw_lc(dev, tx + pad, snap(ty + pad + k.capH * 2.6f), fit_text(fo, l2, tz * 0.92f, tw - 2.0f * pad, c2, sizeof(c2)), tz * 0.92f, with_alpha(DIM, a), sh, k.ow);
    }
    if (!block_ease(g_trB, T0.open, now, dt) || g_trLast.uiW <= 0 || g_trLast.uiH <= 0) { g_tgKeysOn = 0; return; }
    const NativeTrade& T = g_trLast;
    const float tO = ease_out(((float)(now - g_trB.openMs) / 1000.0f) / 0.08f);
    const float A = g_trB.vis * tO; if (A <= 0.0f) { g_tgKeysOn = 0; return; }
    const u32 sh = with_alpha(0xC8000000u, A);
    const MouseState* ms = f.mouse;
    const bool live = T0.open && T0.focused;
    auto over = [&](float x, float y, float w, float h) { return ms && ms->focused && ms->x >= x && ms->x < x + w && ms->y >= y && ms->y < y + h; };
    // ---- the gil typed : the key thread fills g_tgQ while the gil box is up ; written each frame, Enter validates ----
    g_tgKeysOn = (T0.open && T0.gilUp) ? 1 : 0;
    if (!T0.gilUp && g_tgQn) { g_tgQn = 0; g_tgQ[0] = 0; }
    if (T0.gilUp && g_tgQn > 0) {
        char q[12]; lstrcpynA(q, g_tgQ, sizeof(q)); const unsigned v = (unsigned)strtoul(q, 0, 10);
        native_trade_set_gil(v);
        if (g_tgEnter) { native_trade_box_event(1, 5); g_tgEnter = 0; g_tgQn = 0; g_tgQ[0] = 0; }
    } else g_tgEnter = 0;
    // ---- geometry : the panel where the client puts `trade` (top left) ----
    const float cell = snap(34.0f * S), cg = snap(4.0f * S), colW = 4.0f * cell + 3.0f * cg, colGap = snap(24.0f * S);
    const float headH = snap(k.capH * 1.1f), rowH = snap(k.capH * 1.1f), btnH = snap(k.capH * 1.35f), gap = snap(7.0f * S);
    const int nCols = T.npc ? 1 : 2;                                  // an NPC trade (handover) : your column only
    const float colsW = (float)nCols * colW + (float)(nCols - 1) * colGap;
    // the other side's name : the partner, or the NPC you trade to (your target -- the client trades the one you target)
    const char* pname = T.npc ? ((f.game && f.game->target.valid && f.game->target.name[0]) ? f.game->target.name : tr("NPC", "PNJ")) : (T.partner[0] ? T.partner : "?");
    char head[64]; _snprintf(head, sizeof(head), T.npc ? tr("Trade to %s", "Donner \xC3\xA0 %s") : tr("Trade with %s", "\xC3\x89""change avec %s"), pname); head[sizeof(head) - 1] = 0;
    char bal[48] = ""; if (T.myGilOk) { char g[24]; fmt_thousands(g, T.myGil); _snprintf(bal, sizeof(bal), tr("Balance %s G", "Solde %s G"), g); bal[sizeof(bal) - 1] = 0; }
    const char* hint = tr("Type an amount, Enter", "Tape un montant, Entr\xC3\xA9""e");
    // wide enough for the header AND what sits on its right (2026-11-20, "Donner a Ephemeral Moogle" : the balance ran over
    // it) ; capped, the name is cut past that. A single column (NPC) is centred in the wider panel.
    float rightW = fo->measure(hint, tz * 0.85f); if (bal[0]) { const float b = fo->measure(bal, tz * 0.85f); if (b > rightW) rightW = b; }
    float innerW = fo->measure(head, tz * 1.05f) + snap(16.0f * S) + rightW;
    const float innerMax = snap(f.screenW * 0.45f);
    if (innerW > innerMax) innerW = innerMax;
    if (innerW < colsW + (T.npc ? 12.0f * S : 0.0f)) innerW = colsW + (T.npc ? 12.0f * S : 0.0f);
    const float pw = snap(2.0f * pad + innerW);
    const float sx = f.screenW / (float)T.uiW, sy = f.screenH / (float)T.uiH;
    float px = snap((float)T.rx0 * sx), py = snap((float)T.ry0 * sy + (1.0f - tO) * 4.0f * S);
    // the gil ABOVE the slots (the user 2026-11-20 : "gil au-dessus ... pense en ligne") : name, gil, the 2 rows, the buttons
    const float colTop = snap(py + pad + headH + gap), gilTop = snap(colTop + rowH + gap), gridTop = snap(gilTop + rowH + gap);
    // YOUR FRAME holds everything you act on (the user 2026-11-20 : "bloque dans ce cadre avec cancel et ok") : the 8
    // slots, the gil under them, then Cancel / OK ; the partner's frame the same, its status where your buttons are
    const float footTop = snap(gridTop + 2.0f * cell + cg + gap), ph = snap(footTop + btnH + pad + 4.0f * S - py);
    if (T0.giftFocused) native_trade_keep_own();                  // the partner's side never keeps the focus
    if (px + pw > f.screenW - 4.0f) px = snap(f.screenW - 4.0f - pw); if (px < 4.0f) px = 4.0f;
    draw_themed_box(dev, f.skin, px, py, pw, ph, C.muBox, A, S);
    g_trBox[0] = px; g_trBox[1] = py; g_trBox[2] = pw; g_trBox[3] = ph;
    if (T0.open) { g_hit[13][0] = px / f.screenW; g_hit[13][1] = py / f.screenH; g_hit[13][2] = pw / f.screenW; g_hit[13][3] = ph / f.screenH; }
    const float L = px + pad, R = px + pw - pad;
    const float C0 = snap(L + (R - L - colsW) * 0.5f);              // the columns' left : centred in the panel
    // ---- header ----
    {   const float hy = snap(py + pad + headH * 0.5f);
        char hc[64]; const float headMax = R - L - snap(16.0f * S) - rightW;
        fo->begin(dev);
        fo->draw_lc(dev, L, hy, fit_text(fo, head, tz * 1.05f, headMax > 0.0f ? headMax : R - L, hc, sizeof(hc)), tz * 1.05f, with_alpha(0xFFE8D9B0u, A), sh, k.ow);
        // the right of the header : the partner's change (3 s) > the typing hint (the gil box up) > your balance
        if (g_trPeerMs && now - g_trPeerMs < 3000u) {
            const float pulse = 0.6f + 0.4f * sinf((float)(now - g_trPeerMs) * 0.012f);
            char w[64]; _snprintf(w, sizeof(w), tr("%s changed the trade", "%s a modifi\xC3\xA9 l'\xC3\xA9""change"), T.partner); w[sizeof(w) - 1] = 0;
            fo->draw_lc(dev, snap(R - fo->measure(w, tz * 0.9f)), hy, w, tz * 0.9f, with_alpha(WARN, A * pulse), sh, k.ow);
        } else if (T.gilUp) {
            fo->draw_lc(dev, snap(R - fo->measure(hint, tz * 0.85f)), hy, hint, tz * 0.85f, with_alpha(GOLD, A), sh, k.ow);
        } else if (bal[0]) {
            fo->draw_lc(dev, snap(R - fo->measure(bal, tz * 0.85f)), hy, bal, tz * 0.85f, with_alpha(DIM, A), sh, k.ow);
        }
        rrect(dev, L, snap(py + pad + headH), R - L, 1.0f, 0.0f, with_alpha(0x30C8D6EAu, A), with_alpha(0x30C8D6EAu, A));
    }
    int hoverEnt = 0; int tipSide = -1, tipSlot = 0;                       // the entry under the mouse ; the slot whose card shows
    // ---- the two columns ----
    for (int c = 0; c < nCols; ++c) {
        const bool mine = c == 0;
        const float cx = snap(C0 + (float)c * (colW + colGap));
        const NativeTradeSlot* sl = mine ? T.mine : T.theirs;
        const bool ok = mine ? T.own == 4 : T.peer == 4;
        // its frame (a soft inset) ; yours ringed in gold while the client's cursor is in it
        {   const float fx = snap(cx - 6.0f * S), fy = snap(colTop - 5.0f * S), fw = snap(colW + 12.0f * S), fh = snap(footTop + btnH - colTop + 10.0f * S);
            rrect(dev, fx, fy, fw, fh, 7.0f * S, with_alpha(0x14FFFFFFu, A), with_alpha(0x08FFFFFFu, A));
            if (mine && T.focused) rrect_stroke(dev, fx, fy, fw, fh, 7.0f * S, with_alpha(0x80FFDA8Au, A), 1.0f * S);
            else rrect_stroke(dev, fx, fy, fw, fh, 7.0f * S, with_alpha(0x28C8D6EAu, A), 1.0f); }
        // the name and its status pill
        const float ny = snap(colTop + rowH * 0.5f);
        const char* nm = mine ? (T.me[0] ? T.me : tr("You", "Toi")) : (T.partner[0] ? T.partner : "?");
        // (the status is the OK button / the partner's line at the bottom of each frame : the name only turns green)
        fo->begin(dev);
        fo->draw_lc(dev, cx, ny, nm, tz, with_alpha(ok ? GREEN : TXT, A), sh, k.ow);
        // the 8 slots
        for (int s = 1; s <= 8; ++s) {
            const float x = snap(cx + (float)((s - 1) % 4) * (cell + cg)), y = snap(gridTop + (float)((s - 1) / 4) * (cell + cg));
            const bool cur = mine ? (T.focused && T.cursor == s) : (T.giftFocused && T.giftCursor == s);
            const bool hov = over(x, y, cell, cell);
            if (hov && mine && live) hoverEnt = s;
            if (hov && sl[s].id) { tipSide = c; tipSlot = s; }
            rrect(dev, x, y, cell, cell, 5.0f * S, with_alpha(0x22FFFFFFu, A), with_alpha(0x12FFFFFFu, A));
            if (!mine && g_trSlotMs[s] && now - g_trSlotMs[s] < 3000u) {     // the partner just changed it
                const float g = 1.0f - (float)(now - g_trSlotMs[s]) / 3000.0f;
                rrect(dev, x, y, cell, cell, 5.0f * S, with_alpha(0x60FFB45Au, A * g), with_alpha(0x30FFB45Au, A * g));
                rrect_stroke(dev, x, y, cell, cell, 5.0f * S, with_alpha(0xFFFFB45Au, A * g), 1.5f * S);
            }
            if (cur) { rrect_clip_begin(dev, x, y, cell, cell, 5.0f * S); party_selframe(dev, x, y, cell, cell, f.t, A, 0); rrect_clip_end(dev);
                       rrect_stroke(dev, x, y, cell, cell, 5.0f * S, with_alpha(0xD8FFDA8Au, A), 1.5f * S); }
            else if (hov && (sl[s].id || mine)) rrect_stroke(dev, x, y, cell, cell, 5.0f * S, with_alpha(0x80FFDA8Au, A), 1.0f * S);
            if (sl[s].id) {
                const u32 tex = list_icon(f, 3, sl[s].id); const float z = snap(cell - 6.0f * S);
                if (tex) { dTexQuadState(dev, tex); tquad(dev, snap(x + 3.0f * S), snap(y + 3.0f * S), z, z, 0.0f, 1.0f, 0.0f, 1.0f, with_alpha(0xFFFFFFFFu, A), with_alpha(0xFFFFFFFFu, A)); dSetTex(dev, 0, 0); cs(dev); }
                if (sl[s].count > 1) { char n[12]; sprintf(n, "%u", sl[s].count); fo->begin(dev);
                    fo->draw_lc(dev, snap(x + cell - 2.0f * S - fo->measure(n, tz * 0.8f)), snap(y + cell - tz * 0.5f), n, tz * 0.8f, with_alpha(0xFFFFFFFFu, A), with_alpha(0xF0000000u, A), k.ow * 1.4f); }
                if (mine && hov && live) {                                   // yours, under the mouse : a click takes it back
                    const float r = snap(6.0f * S), bx2 = snap(x + cell - r - 2.0f * S), by2 = snap(y + r + 2.0f * S);
                    disc(dev, bx2, by2, r, with_alpha(0xE0C04848u, A));
                    fo->begin(dev); fo->draw_lc(dev, snap(bx2 - fo->measure("x", tz * 0.75f) * 0.5f), by2, "x", tz * 0.75f, with_alpha(0xFFFFFFFFu, A), 0, 0.0f);
                }
            }
        }
        // the gil
        const float gy = snap(gilTop + rowH * 0.5f), cr = snap(rowH * 0.5f);   // the gil icon, as tall as the row
        gil_coin(dev, snap(cx + cr), gy, 2.0f * cr, A);
        const float vx0 = snap(cx + 2.0f * cr + 6.0f * S);
        if (mine && T.gilUp) {                                               // the FIELD : typed digits, or the client's value + its edited digit
            const float fx = snap(vx0 - 4.0f * S), fw = snap(cx + colW - fx), fh = rowH;
            rrect(dev, fx, gilTop, fw, fh, 4.0f * S, with_alpha(0x30FFDA8Au, A), with_alpha(0x18FFDA8Au, A));
            rrect_stroke(dev, fx, gilTop, fw, fh, 4.0f * S, with_alpha(0xC0FFDA8Au, A), 1.0f * S);
            const float bz = snap(fh - 6.0f * S), bmx = snap(fx + fw - 2.0f * bz - 6.0f * S), bpx = snap(fx + fw - bz - 3.0f * S), bby = snap(gilTop + 3.0f * S);
            for (int b = 0; b < 2; ++b) {                                    // - / + : the client's steps
                const float bx2 = b ? bpx : bmx; const bool hb = over(bx2, bby, bz, bz);
                rrect(dev, bx2, bby, bz, bz, 3.0f * S, with_alpha(hb ? 0x50FFDA8Au : 0x26FFFFFFu, A), with_alpha(hb ? 0x30FFDA8Au : 0x14FFFFFFu, A));
                const char* g = b ? "+" : "-"; fo->begin(dev);
                fo->draw_lc(dev, snap(bx2 + (bz - fo->measure(g, tz)) * 0.5f), snap(bby + bz * 0.5f), g, tz, with_alpha(TXT, A), sh, k.ow);
                if (hb && ms && ms->clicked) { g_tgQn = 0; g_tgQ[0] = 0; native_trade_box_event(1, b ? 2 : 1); }
            }
            const float vz = tz * 1.05f, vx = snap(vx0);
            fo->begin(dev);
            if (g_tgQn > 0) {                                                // typing : the digits, a blinking caret
                char q[12]; lstrcpynA(q, g_tgQ, sizeof(q)); char v[24]; fmt_thousands(v, (unsigned)strtoul(q, 0, 10));
                fo->draw_lc(dev, vx, gy, v, vz, with_alpha(0xFFFFFFFFu, A), sh, k.ow);
                if (((now / 500u) & 1u) == 0) { const float cxr = snap(vx + fo->measure(v, vz) + 2.0f * S); rrect(dev, cxr, snap(gy - vz * 0.55f), snap(1.5f * S), snap(vz * 1.1f), 0.0f, with_alpha(GOLD, A), with_alpha(GOLD, A)); }
            } else {                                                         // the client's value, leading zeros up to its edited digit
                int pos = 0; for (unsigned d = T.gilDigit; d >= 10 && pos < 9; d /= 10) ++pos;
                char raw[16]; sprintf(raw, "%u", T.gilVal); const int nd = (int)strlen(raw);
                char dig[16]; const int want = nd > pos + 1 ? nd : pos + 1;
                for (int i = 0; i < want - nd; ++i) dig[i] = '0'; memcpy(dig + (want - nd), raw, (size_t)nd + 1);
                char v[28]; int ci = -1; { int o = 0; for (int i = 0; i < want; ++i) { if (i && (want - i) % 3 == 0) v[o++] = ','; if (want - 1 - i == pos) ci = o; v[o++] = dig[i]; } v[o] = 0; }
                if (ci >= 0) { char pre[28]; lstrcpynA(pre, v, ci + 1); const float ux = fo->measure(pre, vz), cw = fo->measure("0", vz);
                    rrect(dev, snap(vx + ux - 1.0f * S), snap(gy - vz * 0.6f), snap(cw + 2.0f * S), snap(vz * 1.2f), 2.0f * S, with_alpha(0x60E86A5Au, A), with_alpha(0x60E86A5Au, A)); }
                fo->begin(dev); fo->draw_lc(dev, vx, gy, v, vz, with_alpha(0xFFFFFFFFu, A), sh, k.ow);
            }
        } else {
            char g[24]; fmt_thousands(g, sl[0].count); lstrcpynA(g + strlen(g), " G", 4);
            const bool cur = mine && T.focused && T.cursor == 11, hov = mine && live && over(cx, gilTop, colW, rowH);
            if (hov) hoverEnt = 11;
            if (cur || hov) rrect(dev, snap(vx0 - 4.0f * S), gilTop, snap(cx + colW - vx0 + 4.0f * S), rowH, 4.0f * S, with_alpha(cur ? 0x40FFDA8Au : 0x22FFDA8Au, A), with_alpha(cur ? 0x28FFDA8Au : 0x14FFDA8Au, A));
            if (!mine && g_trSlotMs[0] && now - g_trSlotMs[0] < 3000u) rrect_stroke(dev, snap(vx0 - 4.0f * S), gilTop, snap(cx + colW - vx0 + 4.0f * S), rowH, 4.0f * S, with_alpha(WARN, A * (1.0f - (float)(now - g_trSlotMs[0]) / 3000.0f)), 1.5f * S);
            fo->begin(dev); fo->draw_lc(dev, vx0, gy, g, tz, with_alpha(sl[0].count ? 0xFFFFE08Au : 0xFF6E7C8Eu, A), sh, k.ow);
        }
    }
    // ---- your Cancel / OK, in your frame ; the partner's status in theirs ----
    {   const float fy = snap(footTop + btnH * 0.5f);
        const bool hold = g_trPeerMs && now - g_trPeerMs < 1000u;           // OK held back a moment after a partner's change
        static const int ENT[2] = { 10, 9 };
        const char* LB[2] = { tr("Cancel", "Annuler"), tr("OK", "OK") };
        const float bw2 = snap((colW - 6.0f * S) * 0.5f);
        if (!T.npc) {   const float pcx = snap(C0 + colW + colGap);           // the partner's : "Waiting" / "Accepted", centred
            const bool pok = T.peer == 4;
            const char* st = pok ? tr("Accepted", "Valid\xC3\xA9") : tr("Waiting", "En attente");
            rrect(dev, pcx, footTop, colW, btnH, btnH * 0.5f, with_alpha(pok ? 0x407FD67Fu : 0x12FFFFFFu, A), with_alpha(pok ? 0x287FD67Fu : 0x0AFFFFFFu, A));
            fo->begin(dev); fo->draw_lc(dev, snap(pcx + (colW - fo->measure(st, tz)) * 0.5f), fy, st, tz, with_alpha(pok ? GREEN : DIM, A), sh, k.ow); }
        for (int i = 0; i < 2; ++i) {
            const float bx2 = snap(C0 + (float)i * (bw2 + 6.0f * S));
            const bool isOk = ENT[i] == 9, done = isOk && T.own == 4, dis = isOk && hold;
            const bool cur = T.focused && T.cursor == ENT[i], hov = live && over(bx2, footTop, bw2, btnH);
            if (hov && !dis) hoverEnt = ENT[i];
            const float ba = dis ? A * 0.4f : A;
            const u32 c1 = done ? 0x507FD67Fu : (isOk ? 0x38FFDA8Au : 0x22FFFFFFu), c2 = done ? 0x307FD67Fu : (isOk ? 0x20FFDA8Au : 0x14FFFFFFu);
            rrect(dev, bx2, footTop, bw2, btnH, btnH * 0.5f, with_alpha(c1, ba), with_alpha(c2, ba));
            if (cur) { rrect_clip_begin(dev, bx2, footTop, bw2, btnH, btnH * 0.5f); party_selframe(dev, bx2, footTop, bw2, btnH, f.t, ba, 0); rrect_clip_end(dev);
                       rrect_stroke(dev, bx2, footTop, bw2, btnH, btnH * 0.5f, with_alpha(0xD8FFDA8Au, ba), 1.5f * S); }
            else if (hov && !dis) rrect_stroke(dev, bx2, footTop, bw2, btnH, btnH * 0.5f, with_alpha(0x80FFDA8Au, A), 1.0f * S);
            const char* lb = done ? tr("Accepted", "Valid\xC3\xA9") : LB[i];
            fo->begin(dev);
            fo->draw_lc(dev, snap(bx2 + (bw2 - fo->measure(lb, tz)) * 0.5f), fy, lb, tz, with_alpha(done ? GREEN : TXT, ba), sh, k.ow);
        }
    }
    // ---- the quantity : a bubble under the slot being filled ----
    if (T.qtyUp) {
        const int s = T.cursor >= 1 && T.cursor <= 8 ? T.cursor : 1;
        const float sxl = snap(C0 + (float)((s - 1) % 4) * (cell + cg));
        const NativeList& NL = native_list(); unsigned qid = 0;
        if (NL.open && NL.kind == 3 && NL.cursor >= NL.top && NL.cursor < NL.top + NL.rows) qid = NL.id[NL.cursor - NL.top];
        const float bwq = snap(4.0f * cell + 3.0f * cg), bhq = snap(btnH + 2.0f * 7.0f * S + k.bi * 2.0f);
        float bxq = snap(sxl - cell), byq = snap(gridTop + 2.0f * cell + cg + 3.0f * S);
        if (bxq < C0) bxq = C0; if (bxq + bwq > C0 + colW) bxq = snap(C0 + colW - bwq);
        draw_themed_box(dev, f.skin, bxq, byq, bwq, bhq, C.muBox, A, S);
        const float ip = snap(7.0f * S) + k.bi, iz = snap(bhq - 2.0f * ip), cy = snap(byq + bhq * 0.5f);
        float x = snap(bxq + ip);
        if (qid) { const u32 tex = list_icon(f, 3, qid); if (tex) { dTexQuadState(dev, tex); tquad(dev, x, snap(cy - iz * 0.5f), iz, iz, 0.0f, 1.0f, 0.0f, 1.0f, with_alpha(0xFFFFFFFFu, A), with_alpha(0xFFFFFFFFu, A)); dSetTex(dev, 0, 0); cs(dev); } x = snap(x + iz + 6.0f * S); }
        // - value / max + ; 1 ; All  (the client's events 1 / 2 / 3 / 4 ; the value validates)
        const float bz = snap(iz * 0.9f);
        struct QB { const char* t; int ev; float w; } QBs[4] = { { "-", 1, bz }, { "+", 2, bz }, { "1", 3, bz }, { tr("All", "Tout"), 4, snap(fo->measure(tr("All", "Tout"), tz * 0.85f) + 12.0f * S) } };
        char v[16]; sprintf(v, "%d / %d", T.qtyVal, T.qtyMax);
        const float vw = snap(fo->measure(v, tz * 1.05f) + 10.0f * S);
        float bx2 = x;
        auto qbtn = [&](const char* t, float w, int ev) {
            const bool hb = over(bx2, snap(cy - bz * 0.5f), w, bz);
            rrect(dev, bx2, snap(cy - bz * 0.5f), w, bz, 3.0f * S, with_alpha(hb ? 0x50FFDA8Au : 0x26FFFFFFu, A), with_alpha(hb ? 0x30FFDA8Au : 0x14FFFFFFu, A));
            fo->begin(dev); fo->draw_lc(dev, snap(bx2 + (w - fo->measure(t, tz * 0.85f)) * 0.5f), cy, t, tz * 0.85f, with_alpha(TXT, A), sh, k.ow);
            if (hb && ms && ms->clicked) native_trade_box_event(0, ev);
            bx2 = snap(bx2 + w + 3.0f * S);
        };
        qbtn(QBs[0].t, QBs[0].w, QBs[0].ev);
        {   const bool hv = over(bx2, snap(cy - bz * 0.5f), vw, bz);          // the value : a click validates it
            if (hv) rrect(dev, bx2, snap(cy - bz * 0.5f), vw, bz, 3.0f * S, with_alpha(0x30FFDA8Au, A), with_alpha(0x18FFDA8Au, A));
            fo->begin(dev); fo->draw_lc(dev, snap(bx2 + 5.0f * S), cy, v, tz * 1.05f, with_alpha(0xFFFFFFFFu, A), sh, k.ow);
            if (hv && ms && ms->clicked) native_trade_box_event(0, 5);
            bx2 = snap(bx2 + vw + 3.0f * S); }
        qbtn(QBs[1].t, QBs[1].w, QBs[1].ev); qbtn(QBs[2].t, QBs[2].w, QBs[2].ev); qbtn(QBs[3].t, QBs[3].w, QBs[3].ev);
    }
    // ---- the item card : a tooltip on the slot under the mouse, else on the client's cursor ; only with an item ----
    if (tipSide < 0) {
        if (T.focused && T.cursor >= 1 && T.cursor <= 8 && T.mine[T.cursor].id) { tipSide = 0; tipSlot = T.cursor; }
        else if (T.giftFocused && T.giftCursor >= 1 && T.giftCursor <= 8 && T.theirs[T.giftCursor].id) { tipSide = 1; tipSlot = T.giftCursor; }
    }
    if (tipSide >= 0 && !T.qtyUp && !T.gilUp) {
        const unsigned iid = (tipSide ? T.theirs : T.mine)[tipSlot].id;
        if (iid && iid != 0xFFFF) {
            const float cx = snap(C0 + (float)tipSide * (colW + colGap) + (float)((tipSlot - 1) % 4) * (cell + cg));
            const float cyb = snap(gridTop + (float)((tipSlot - 1) / 4) * (cell + cg) + cell + 4.0f * S);
            card_use(0); card_load(f.dev, iid);
            if (card_ready(iid)) card_draw(f, k, cx, snap(300.0f * S), cyb, cyb, 1.0f, A, false);
        }
    }
    // ---- the mouse : hover moves the client's cursor, a click validates that entry (the client's own events) ----
    static int lastHover = 0;
    if (live && hoverEnt && hoverEnt != lastHover && hoverEnt != T.cursor) native_trade_hover(hoverEnt);
    lastHover = hoverEnt;
    if (live && hoverEnt && ms && ms->clicked) native_trade_pick(hoverEnt);
}

// ======== THE YES / NO CONFIRMS (NU_DIALOG) : every generic confirm of the client (dispose, linkshell, Level Sync,
// raise, shop buy / sell, merits ...) in one AioHUD box where the client puts its own : the title (when the asker gives
// one), the question, then Yes | No SIDE BY SIDE (left / right move between them -- the event hook -- ; up / down too, the
// client's own). The client's cursor is shown ; the mouse hovers / validates with the client's own events ========
namespace { Block g_dlB; unsigned g_dlMs = 0; NativeDialog g_dlLast; }
void dialog_draw(const Frame& f) {
    const UiConfig& C = ui_config();
    if (!(C.nuHide & NU_DIALOG) || !f.font || f.screenH <= 0.0f || f.screenW <= 0.0f) { g_dlB.vis = 0.0f; g_dlB.wasOpen = false; return; }
    const unsigned now = GetTickCount();
    float dt = g_dlMs ? (float)(now - g_dlMs) / 1000.0f : 0.0f; g_dlMs = now; if (dt > 0.1f) dt = 0.1f;
    const NativeDialog& D0 = native_dialog();
    if (D0.open) g_dlLast = D0;
    if (!block_ease(g_dlB, D0.open, now, dt) || g_dlLast.uiW <= 0 || g_dlLast.uiH <= 0) return;
    const NativeDialog& D = g_dlLast;
    const Metrics k = metrics(f);
    const float S = k.S, pad = snap(12.0f * S) + k.bi, tz = k.tz;
    Font* fo = f.font; const u32 dev = f.dev;
    const float tO = ease_out(((float)(now - g_dlB.openMs) / 1000.0f) / 0.08f);
    const float A = g_dlB.vis * tO; if (A <= 0.0f) return;
    // shopbuy / shopsell are not a question : a 2-row MENU (the user 2026-11-20 : "dans le jeu c'est juste un menu avec
    // Sell / Cancel") -- drawn as AioHUD's menus, above the chat
    if (!strncmp(D.name, "shop", 4)) {
        const Metrics kN = no_icons(k);
        RowSpec rows[2] = {};
        rows[0].label = !strcmp(D.name, "shopbuy") ? tr("Buy", "Acheter") : tr("Sell", "Vendre"); rows[1].label = tr("Cancel", "Annuler");
        for (int i = 0; i < 2; ++i) { rows[i].icon = IC_NONE; rows[i].sub = false; rows[i].off = false; }
        float mw, mh; block_size(f, kN, rows, 2, mw, mh);
        if (mw < snap(360.0f * S)) mw = snap(360.0f * S);                 // the confirm's width : the two boxes of a sale line up (2026-11-20)
        float mx = 16.0f * S, my = snap(f.screenH * 0.5f);
        {   float cax = 0.0f, caw = 0.0f, ctop = 0.0f, cbot = 0.0f;
            if (chat_anchor(cax, caw, ctop, cbot)) { mx = snap(cax); my = snap(ctop - 6.0f * S - mh); } }
        my = snap(my + (1.0f - tO) * 3.0f * S);
        if (my < 4.0f) my = 4.0f;
        const int row = block_draw(f, kN, g_dlB, rows, 2, D.cursor, D.focused, mx, my, mw, mh, A, dt);
        if (D0.open) { g_hit[14][0] = mx / f.screenW; g_hit[14][1] = my / f.screenH; g_hit[14][2] = mw / f.screenW; g_hit[14][3] = mh / f.screenH; }
        static int lastRow = 0;
        if (D0.open && D0.focused && row >= 1 && row <= 2 && row != lastRow && row != D.cursor) native_dialog_hover(row);
        lastRow = row;
        if (D0.open && D0.focused && row >= 1 && row <= 2 && f.mouse && f.mouse->clicked) native_dialog_pick(row);
        return;
    }
    const u32 sh = with_alpha(0xC8000000u, A);
    const u32 GOLD = 0xFFFFDA8Au, TXT = 0xFFE4EAF2u, DIM = 0xFF93A6BEu;
    const float sx = f.screenW / (float)D.uiW, sy = f.screenH / (float)D.uiH;
    float bw = snap((float)(D.rx1 - D.rx0) * sx); if (bw < snap(360.0f * S)) bw = snap(360.0f * S);
    // the question : its own lines (\n), each wrapped to the box
    char lines[10][160]; int nl = 0;
    // no question of its own (shopbuy / shopsell, live 2026-11-20 : "") : the client says it on its help line -- the top bar
    const char* qtext = D.text[0] ? D.text : native_topbar().help.text;
    {   char para[400]; const char* p = qtext;
        while (*p && nl < 10) {
            int n = 0; while (p[n] && p[n] != '\n' && n < (int)sizeof(para) - 1) { para[n] = p[n]; ++n; } para[n] = 0;
            if (n) nl += wrap_lines(fo, para, tz, bw - 2.0f * pad, lines + nl, 10 - nl);
            p += n; if (*p == '\n') ++p;
        }
        if (!nl) { lstrcpynA(lines[0], "?", sizeof(lines[0])); nl = 1; }
    }
    const float lineH = snap(tz * 1.35f), titleH = D.title[0] ? snap(k.capH * 1.1f) : 0.0f, btnH = snap(k.capH * 1.4f), gap = snap(10.0f * S);
    const float bh = snap(pad + titleH + (titleH > 0.0f ? gap * 0.6f : 0.0f) + (float)nl * lineH + gap + btnH + pad + (D.cancelOk || D.hold ? lineH * 0.9f : 0.0f));
    float bx = snap((float)D.rx0 * sx), by = snap((float)D.ry0 * sy);
    // ABOVE AioHUD's CHAT, its left edge (the user 2026-11-20 : the client's place -- anchored to the bottom -- put it IN
    // the chat), as the NPC windows ; no chat box : the client's place
    {   float cax = 0.0f, caw = 0.0f, ctop = 0.0f, cbot = 0.0f;
        if (chat_anchor(cax, caw, ctop, cbot)) { bx = snap(cax); by = snap(ctop - 6.0f * S - bh); } }
    by = snap(by + (1.0f - tO) * 4.0f * S);
    if (bx + bw > f.screenW - 4.0f) bx = snap(f.screenW - 4.0f - bw); if (bx < 4.0f) bx = 4.0f;
    if (by + bh > f.screenH - 4.0f) by = snap(f.screenH - 4.0f - bh); if (by < 4.0f) by = 4.0f;
    draw_themed_box(dev, f.skin, bx, by, bw, bh, C.muBox, A, S);
    if (D0.open) { g_hit[14][0] = bx / f.screenW; g_hit[14][1] = by / f.screenH; g_hit[14][2] = bw / f.screenW; g_hit[14][3] = bh / f.screenH; }
    const float L = bx + pad, R = bx + bw - pad;
    float y = by + pad;
    fo->begin(dev);
    if (titleH > 0.0f) {
        fo->draw_lc(dev, L, snap(y + titleH * 0.5f), D.title, tz * 1.05f, with_alpha(0xFFE8D9B0u, A), sh, k.ow);
        y += titleH; rrect(dev, L, snap(y), R - L, 1.0f, 0.0f, with_alpha(0x30C8D6EAu, A), with_alpha(0x30C8D6EAu, A)); y += gap * 0.6f;
        fo->begin(dev);
    }
    for (int i = 0; i < nl; ++i) { fo->draw_lc(dev, L, snap(y + lineH * 0.5f), lines[i], tz, with_alpha(TXT, A), sh, k.ow); y += lineH; }
    y = snap(y + gap);
    // Yes | No
    const MouseState* ms = f.mouse;
    const bool live = D0.open && D0.focused;
    const float bwid = snap((R - L - 10.0f * S) * 0.5f);
    int hov = 0;
    for (int e = 1; e <= 2; ++e) {
        const float x = snap(L + (float)(e - 1) * (bwid + 10.0f * S));
        const bool cur = D.cursor == e, h = live && ms && ms->focused && ms->x >= x && ms->x < x + bwid && ms->y >= y && ms->y < y + btnH;
        if (h) hov = e;
        const u32 c1 = e == 1 ? 0x30FFDA8Au : 0x22FFFFFFu, c2 = e == 1 ? 0x18FFDA8Au : 0x14FFFFFFu;
        rrect(dev, x, y, bwid, btnH, btnH * 0.5f, with_alpha(c1, A), with_alpha(c2, A));
        if (cur) { rrect_clip_begin(dev, x, y, bwid, btnH, btnH * 0.5f); party_selframe(dev, x, y, bwid, btnH, f.t, A, 0); rrect_clip_end(dev);
                   rrect_stroke(dev, x, y, bwid, btnH, btnH * 0.5f, with_alpha(0xD8FFDA8Au, A), 1.5f * S); }
        else if (h) rrect_stroke(dev, x, y, bwid, btnH, btnH * 0.5f, with_alpha(0x80FFDA8Au, A), 1.0f * S);
        const char* lb = e == 1 ? tr("Yes", "Oui") : tr("No", "Non");
        fo->begin(dev);
        fo->draw_lc(dev, snap(x + (bwid - fo->measure(lb, tz * 1.05f)) * 0.5f), snap(y + btnH * 0.5f), lb, tz * 1.05f, with_alpha(cur ? 0xFFFFFFFFu : TXT, A), sh, k.ow);
    }
    // the hints : Escape answers No (when the asker allows it) ; a protected answer is HELD to confirm (the client's gauge)
    if (D.cancelOk || D.hold) {
        char hint[96]; hint[0] = 0;
        if (D.hold) lstrcpynA(hint, tr("Hold the button to confirm", "Maintiens la touche pour confirmer"), sizeof(hint));
        else lstrcpynA(hint, tr("Esc : No", "\xC3\x89""chap : Non"), sizeof(hint));
        fo->begin(dev);
        fo->draw_lc(dev, snap(R - fo->measure(hint, tz * 0.8f)), snap(y + btnH + lineH * 0.55f), hint, tz * 0.8f, with_alpha(D.hold ? GOLD : DIM, A), sh, k.ow);
    }
    static int lastHov = 0;
    if (live && hov && hov != lastHov && hov != D.cursor) native_dialog_hover(hov);
    lastHov = hov;
    if (live && hov && ms && ms->clicked) native_dialog_pick(hov);
}

// ======== THE MACRO BAR (NU_MACRO) : the client's Ctrl / Alt palette, AioHUD's style, shown exactly when the client
// shows its own (the user 2026-11-20 : "macros comme le jeu"). Where the client puts it (its rect, long or short) : a
// header (the book's title, the set, Ctrl / Alt), then the 10 macros (key + name) ; the hand (mouse / pad) on its slot,
// a short flash on the macro just run ========
namespace {
Block g_mbB; unsigned g_mbMs = 0; NativeMacro g_mcLast; int g_mcRunSeen[2] = { -2, -2 }; unsigned g_mcRunMs[2] = { 0, 0 };
}
void macrobar_draw(const Frame& f) {
    const UiConfig& C = ui_config();
    if (!(C.nuHide & NU_MACRO) || !f.font || f.screenH <= 0.0f || f.screenW <= 0.0f) { g_mbB.vis = 0.0f; g_mbB.wasOpen = false; return; }
    const unsigned now = GetTickCount();
    float dt = g_mbMs ? (float)(now - g_mbMs) / 1000.0f : 0.0f; g_mbMs = now; if (dt > 0.1f) dt = 0.1f;
    const NativeMacro& M0 = native_macro();
    // a macro run : remember when (the flash) -- the first reading only sets the baseline
    if (M0.ok) for (int b = 0; b < 2; ++b) {
        if (g_mcRunSeen[b] == -2) g_mcRunSeen[b] = M0.lastRun[b];
        else if (M0.lastRun[b] != g_mcRunSeen[b]) { g_mcRunSeen[b] = M0.lastRun[b]; g_mcRunMs[b] = now; }   // the SAME slot twice : no change to see (known limit)
    }
    const bool up = M0.ok && M0.open >= 1 && M0.uiW > 0 && M0.uiH > 0;
    if (up) g_mcLast = M0;
    if (!block_ease(g_mbB, up, now, dt) || g_mcLast.uiW <= 0 || g_mcLast.open < 1) return;
    const NativeMacro& M = g_mcLast;
    const Metrics k = metrics(f);
    const float S = k.S, tz = k.tz, pad = snap(8.0f * S) + k.bi;
    Font* fo = f.font; const u32 dev = f.dev;
    const float tO = ease_out(((float)(now - g_mbB.openMs) / 1000.0f) / 0.08f);
    const float A = g_mbB.vis * tO; if (A <= 0.0f) return;
    const u32 sh = with_alpha(0xC8000000u, A);
    const int bar = M.open - 1;                                          // 0 Ctrl, 1 Alt
    // the client's rect (its long / short width, where its code put it) ; our own height : a header + one row of cells
    const float sx = f.screenW / (float)M.uiW, sy = f.screenH / (float)M.uiH;
    float bw = snap((float)(M.rx1 - M.rx0) * sx);
    const float cellH = snap(k.capH * 1.55f);
    const float bh = snap(pad + cellH + pad);                              // the cells only (the user 2026-11-20 : no book / set header)
    if (bw < 300.0f * S) bw = snap(300.0f * S);
    // RIGHT UNDER THE TOP BAR, its left edge (the user 2026-11-20 : "en dessous de la barre d'infos") ; the client's
    // own place only when AioHUD's top bar is off
    float bx = snap((float)M.rx0 * sx), by = snap((float)M.ry0 * sy);
    if (C.nuHide & NU_HELP) { bx = 0.0f; by = snap(topbar_height(k) + 2.0f * S); }
    by = snap(by + (1.0f - tO) * 3.0f * S);
    (void)sy;
    if (bx + bw > f.screenW - 4.0f) bx = snap(f.screenW - 4.0f - bw); if (bx < 4.0f) bx = 4.0f;
    if (by + bh > f.screenH - 4.0f) by = snap(f.screenH - 4.0f - bh); if (by < 4.0f) by = 4.0f;
    draw_themed_box(dev, f.skin, bx, by, bw, bh, C.muBox, A, S);
    // the 10 cells : key (1..9, 0) + the macro's name ; empty ones dimmed
    const float L = bx + pad, W = bw - 2.0f * pad;
    const float cy0 = snap(by + pad), cg = snap(4.0f * S), cw = (W - 9.0f * cg) / 10.0f;
    const float flashT = (float)(now - g_mcRunMs[bar]) / 1000.0f;
    for (int i = 0; i < 10; ++i) {
        const float cx = snap(L + (float)i * (cw + cg)), cwi = snap(L + (float)(i + 1) * (cw + cg) - cg) - cx;
        const char* nm = M.name[bar * 10 + i];
        const bool hand = M.hand == i + 1, empty = !nm[0];
        const float r = snap(4.0f * S);
        rrect(dev, cx, cy0, cwi, cellH, r, with_alpha(empty ? 0x14FFFFFFu : 0x22FFFFFFu, A), with_alpha(empty ? 0x0CFFFFFFu : 0x16FFFFFFu, A));
        if (g_mcRunMs[bar] && M.lastRun[bar] == i && flashT < 0.6f) {        // just run : a gold flash fading out
            const float fa = A * (1.0f - flashT / 0.6f);
            rrect(dev, cx, cy0, cwi, cellH, r, with_alpha(0x70FFDA8Au, fa), with_alpha(0x30FFDA8Au, fa));
        }
        if (hand) {
            rrect_clip_begin(dev, cx, cy0, cwi, cellH, r);
            party_selframe(dev, cx, cy0, cwi, cellH, f.t, A, 0);
            rrect_clip_end(dev);
            rrect_stroke(dev, cx, cy0, cwi, cellH, r, with_alpha(0xC8FFDA8Au, A), 1.0f);
        }
        char key[4]; _snprintf(key, sizeof(key), "%d", (i + 1) % 10); key[sizeof(key) - 1] = 0;
        fo->begin(dev);
        fo->draw_lc(dev, snap(cx + 4.0f * S), snap(cy0 + 7.0f * S), key, tz * 0.72f, with_alpha(0xFF93A6BEu, A), sh, k.ow);
        if (!empty) {
            char cut[32]; const float nw = cwi - 8.0f * S;
            const char* s = fit_text(fo, nm, tz, nw, cut, sizeof(cut));
            fo->draw_lc(dev, snap(cx + (cwi - fo->measure(s, tz)) * 0.5f), snap(cy0 + cellH * 0.58f), s, tz, with_alpha(hand ? 0xFFFFFFFFu : 0xFFE4EAF2u, A), sh, k.ow);
        }
    }
}

} // namespace aio
