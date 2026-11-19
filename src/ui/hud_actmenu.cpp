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
#include "model/native_ui.h"
#include "model/gamestate.h"   // GameState (me.mp)
#include "ui/buff_atlas.h"        // buff_atlas_tex / buff_cell_uv : the status-icon fallback
#include "ui/tex_retry.h"         // ensure_raw_tex : the element strip / job emblems (bounded retry)
#include "model/skillchain.h"     // sc_skill_lookup / sc_info : a weapon skill's property -> its element
#include "model/job_track_gen.h"  // job_track : which job a job ability belongs to
#include "model/paths.h"       // plugin_dir : Windower's own plugins\icons
#include "model/spells_gen.h"  // spell_info : the spell list's names and MP
#include "model/spell_cost.h"  // spell_cost_kind : Full Cure "All", Embrava "20%" 
#include "model/abilities_gen.h"        // abil_info : the ability list's job abilities
#include "model/itemnames_gen.h"        // item_display_name : the Items list
#include "model/mounts_gen.h"           // mount_name : the mount list
#include "model/zones.h"                // zone_name : the Profile's Home Point
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
float g_subXY[2] = { -1.0f, -1.0f };     // where the main menu's submenu was last drawn (a player's options take its place)
float g_hit[14][4] = { { 0, 0, 0, 0 } };  // the drawn blocks (menu, submenu ; NPC bubble, NPC answers ; the auto-translate picker ; the main menu, its categories, its list ; Unity Info) as screen FRACTIONS : their clicks never reach the game

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
const SubRow* sub_row(const char* menu, int entry) {
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

inline u32 dev_of(const Frame& f) { return f.dev; }

// ---- ONE block : the menus' theme box, thin capsules, the party's gold glass on the picked row, the hand ----
// `extra` : what the client shows on the right of a row (the Chat submenu : the Tell name, a shell's name, the Unity
// leader) ; `pearl` : 513 linkshell / 514 linkpearl / 515 pearlsack drawn before it, tinted `pearlArgb`
struct RowSpec { const char* label; Icon icon; bool sub; bool off; const char* extra = 0; int pearl = 0; u32 pearlArgb = 0;
                 bool cycle = false; bool center = false;
                 u32 tint = 0;
                 bool mark = false;   // the row HELD by a manual sort : a pulsing gold rim
                 u32 tex = 0; float tu0 = 0.0f, tu1 = 1.0f, tv0 = 0.0f, tv1 = 1.0f; u32 texTint = 0xFFFFFFFFu; };
                 // tex + its UV : the row's picture (Windower's file, a status / element / job cell), tinted texTint   // tex : the row's own picture (a spell / ability icon from Windower's plugins\icons), drawn in place of `icon`   // tint : the label's own colour (the NPC answers : the client's green), 0 the menus' grey-white   // center : a lone message row (the empty list), centred in the block   // cycle : the extra can be changed with RIGHT (the Tell name ; left closes the submenu) -> name >
struct Block {
    float vis = 0.0f, curY = -1.0f; unsigned openMs = 0; int lastN = 0; bool wasOpen = false; float sel[16] = { 0 };
    float x = 0, y = 0, w = 0, h = 0;           // drawn rect this frame (w = 0 : not drawn)
    float rowY0 = 0, rowStep = 0, capX = 0, capW = 0, capH = 0;
    int hoverRow = 0;
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

struct Metrics { float S, tz, ow, capH, gap, icZ, pad, gutter, capPadL, icW, capPadR, triW; bool icons; };
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
    return k;
}
// the MAIN menu's chain : no icons (the client's has none -- the user, 2026-11-18) ; the spell lists keep theirs
Metrics no_icons(Metrics k) { k.icons = false; k.icW = 0.0f; return k; }
// the submenu arrow's column : only when a row has one (2026-11-16 : "trop large" -- Magic / Abilities / Chat have none)
float tri_col(const Metrics& k, const RowSpec* rows, int n) { for (int i = 0; i < n; ++i) if (rows[i].sub) return k.triW; return 0.0f; }
// the block's size for these rows
void block_size(const Frame& f, const Metrics& k, const RowSpec* rows, int n, float& w, float& h) {
    float wLab = 0.0f, wEx = 0.0f;
    for (int i = 0; i < n; ++i) {
        const float lw = f.font->measure(rows[i].label, k.tz); if (lw > wLab) wLab = lw;
        float ew = rows[i].extra && rows[i].extra[0] ? f.font->measure(rows[i].extra, k.tz * 0.92f) : 0.0f;
        if (rows[i].pearl) ew += k.capH + 3.0f * k.S;   // its inventory icon, the capsule's height
        if (rows[i].cycle) ew += 9.0f * k.S;
        if (ew > wEx) wEx = ew;
    }
    const float capW = snap(k.capPadL + k.icW + wLab + (wEx > 0.0f ? wEx + 8.0f * k.S : 0.0f) + k.capPadR + tri_col(k, rows, n));
    w = snap(k.gutter + capW + k.pad); h = snap((float)n * k.capH + (float)(n - 1) * k.gap + 2.0f * k.pad);
}
// open / close easing ; false when nothing is to be drawn
bool block_ease(Block& b, bool open, unsigned now, float dt) {
    const float kFast = 1.0f - expf(-dt * 45.0f);   // ~70 ms to settle : smooth, never in the way
    if (open && !b.wasOpen) { b.openMs = now; for (int i = 0; i < 16; ++i) b.sel[i] = 0.0f; b.curY = -1.0f; }
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
    const float capW = bw - k.gutter - k.pad, cx0 = bx + k.gutter, step = k.capH + k.gap, r = k.capH * 0.5f;
    const float ry0 = by + b.inTop;                     // the rows' top (below the header band, when there is one)
    b.x = bx; b.y = by; b.w = bw; b.h = bh; b.rowY0 = ry0 + k.pad; b.rowStep = step; b.capX = cx0; b.capW = capW; b.capH = k.capH;

    draw_themed_box(dev, f.skin, bx, by, bw, bh, ui_config().muBox, A, S);   // Game UI -> Menus : one theme for all
    const bool hasCur = cursorRow >= 1 && cursorRow <= n;
    if (hasCur) {
        const float targetY = ry0 + k.pad + (float)(cursorRow - 1) * step;
        if (b.curY < 0.0f || b.lastN != n) b.curY = targetY;
        b.curY += (targetY - b.curY) * kFast;
        if (fabsf(targetY - b.curY) < 0.3f) b.curY = targetY;
    }
    b.lastN = n;
    for (int i = 0; i < n && i < 16; ++i) {
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
    for (int i = 0; i < n && i < 16; ++i) {
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
    // the labels, in ONE textured pass (begin() binds the glyph state -- a shape drawn before leaves the stage on plain
    // colour, which draws every glyph as a solid quad : the white bands of 2026-11-16)
    Font* fo = f.font;
    fo->begin(dev);
    for (int i = 0; i < n && i < 16; ++i) {
        const float s = b.sel[i], a = A * (rows[i].off ? 0.5f : 1.0f);
        const float y = snap(ry0 + k.pad + (float)i * step);
        if (rows[i].center) {
            fo->draw_lc(dev, snap(bx + (bw - fo->measure(rows[i].label, k.tz)) * 0.5f), snap(y + k.capH * 0.5f), rows[i].label, k.tz,
                        with_alpha(0xFFC9D3E0u, a), with_alpha(0xC8000000u, a), k.ow);
            continue;
        }
        const u32 lc = rows[i].tint ? mix(rows[i].tint, 0xFFE4FFD6u, s * 0.6f) : mix(0xFFC9D3E0u, 0xFFFFFFFFu, s);
        fo->draw_lc(dev, snap(cx0 + k.capPadL + k.icW), snap(y + k.capH * 0.5f), rows[i].label, k.tz,
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
void actmenu_hits_clear() { for (int i = 0; i < 14; ++i) g_hit[i][2] = 0.0f; }
// the auto-translate picker (g_hit[4]) : the wheel over it steps the client's selection (fep_draw consumes it)
static volatile long g_fepWheel = 0;
bool fep_hit(float fx, float fy) {
    const float* h = g_hit[4];
    return h[2] > 0.0f && fx >= h[0] && fx < h[0] + h[2] && fy >= h[1] && fy < h[1] + h[3];
}
void fep_wheel_add(int d) { InterlockedExchangeAdd(&g_fepWheel, d); }
bool actmenu_hit(float fx, float fy) {
    for (int i = 0; i < 14; ++i) {
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
        lh = snap((float)pg * k.capH + (float)(pg - 1) * k.gap + 2.0f * k.pad);
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
                float lx, float ly, float lw, float lh, float LA, float tL, float dt, float* hit) {
    const float S = k.S;
    const bool empty = L.n == 0;
    const int cur = empty ? 0 : ((L.cursor >= L.top && L.cursor < L.top + L.rows) ? L.cursor - L.top + 1 : 0);
    if (LA <= 0.0f) return;
    block_draw(f, k, B, lrows, L.rows, cur, L.focused, lx, ly, lw, lh, LA, dt);
    if (L.n > L.rows) {
        const float tx = lx + lw - 3.0f * S, ty = ly + k.pad, th = lh - 2.0f * k.pad;
        const float th2 = th * (float)L.rows / (float)L.n, ty2 = ty + th * (float)L.top / (float)L.n;
        rrect(f.dev, tx - 1.0f * S, ty, 2.0f * S, th, 1.0f * S, with_alpha(0x30FFFFFFu, LA), with_alpha(0x30FFFFFFu, LA));
        rrect(f.dev, tx - 1.0f * S, snap(ty2), 2.0f * S, snap(th2 < 6.0f * S ? 6.0f * S : th2), 1.0f * S, with_alpha(0xC0FFDA8Au, LA), with_alpha(0xC0FFDA8Au, LA));
    }
    if (open) { hit[0] = lx / f.screenW; hit[1] = ly / f.screenH; hit[2] = lw / f.screenW; hit[3] = lh / f.screenH; }
    if (L.kind == 3 && cur >= 1 && cur <= L.rows && !card_on_chat(f, k, L.id[cur - 1], tL, LA)) {
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
            if (ly + lh > f.screenH - 4.0f) ly = snap(f.screenH - 4.0f - lh); if (ly < 4.0f) ly = 4.0f;
            list_paint(f, k, g_listB, lastL, lrows, NL.open, lx, ly, lw, lh, g_listB.vis * tL, tL, dt, g_hit[0]);
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
    RowSpec srows[16]; char snums[16][12]; int scur = 0;
    for (int i = 0; i < lastS.n && i < 16; ++i) {
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
        for (int i = 0; i < lastS.n && i < 16; ++i) {
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
    const float SA = g_subB.vis * tS * (lastS.focused ? 1.0f : 0.62f);          // its own list open on top : steps back
    if (SA <= 0.0f) return;
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
void card_draw(const Frame& f, const Metrics& k, float bx, float bw, float top, float bottom, float tO, float A, bool above) {
        const float S = k.S; const u32 dev = f.dev; Font* fo = f.font; const UiConfig& C = ui_config();
        // THE CLIENT'S LAYOUT (its own card, screenshot 2026-11-17) : the icon top-left ; ONE text column right of it,
        // white -- the title (the log name, "Ark tachi"), the kind ("(Great Katana)All Races"), the description, then
        // "Lv.1 All Jobs" ; the enchantment "<1/1 0:30/[1:00:00, 0:30]>" right-aligned in light blue ; round Rare / Ex
        // badges in the top-right corner. AioHUD's box and font.
        const float ctz = snap(k.tz * 1.0f), clH = snap(ctz * 1.3f), cpad = snap(8.0f * S), icon = snap(32.0f * S);
        const float colX = snap(bx + cpad + icon + 6.0f * S), tw = bx + bw - cpad - colX;
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
        const float ch = snap(cpad + (colH > icon ? colH : icon) + encH + cpad);
        float cy = above ? snap(top - snap(4.0f * S) - ch) : top;
        if (above && cy < 4.0f) cy = snap(bottom + snap(4.0f * S));
        if (!above && cy + ch > f.screenH - 4.0f) cy = snap(f.screenH - 4.0f - ch);
        cy = snap(cy + (1.0f - tO) * 3.0f * S);
        g_cardBottom = cy + ch;
        draw_themed_box(dev, f.skin, bx, cy, bw, ch, C.muBox, A, S);
        const float ix = snap(bx + cpad), iy = snap(cy + cpad);
        if (g_cardIcon) {
            dTexQuadState(dev, g_cardIcon);
            tquad(dev, ix, iy, icon, icon, 0.0f, 1.0f, 0.0f, 1.0f, with_alpha(0xFFFFFFFFu, A), with_alpha(0xFFFFFFFFu, A));
            dSetTex(dev, 0, 0);
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
    g_elemTex = g_jobTex = g_wpnTex = 0; g_elemR = TexRetry(); g_jobR = TexRetry(); g_wpnR = TexRetry();
}
void npc_card_dispose() {
    if (g_cardIcon) release_texture(g_cardIcon);
    if (g_cardAlt.icon) release_texture(g_cardAlt.icon);
    for (int i = 0; i < 64; ++i) if (g_icons[i].tex) release_texture(g_icons[i].tex);
    if (g_elemTex) release_texture(g_elemTex);
    if (g_jobTex) release_texture(g_jobTex);
    if (g_wpnTex) release_texture(g_wpnTex);
    for (int i = 0; i < 4; ++i) if (g_pearl[i].tex) release_texture(g_pearl[i].tex);
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
    const float tz = snap(k.tz * 1.08f), lineH = snap(tz * 1.45f), pad = snap(9.0f * S);
    float wText = 0.0f;
    for (int i = 0; i < nL; ++i) { const float w = fo->measure(L[i], tz); if (w > wText) wText = w; }
    // the answers : the CLIENT's page -- its 3 rows from its own top (2026-11-17 : "la liste contient moins d'items dans
    // le menu du jeu") ; the client scrolls it, the rail says where it sits
    const int vis = last.n < 3 ? last.n : 3;
    g_npcView = last.top;
    if (g_npcView > last.n - vis) g_npcView = last.n - vis; if (g_npcView < 0) g_npcView = 0;
    RowSpec rows[3];
    for (int i = 0; i < vis; ++i) { rows[i].label = last.choice[g_npcView + i]; rows[i].icon = IC_NONE; rows[i].sub = false; rows[i].off = false; rows[i].tint = last.ctl[g_npcView + i][0] == 0xFEFE ? 0xFF9CE27Au : 0xFFF4F6FAu; }   // the client : an answer carrying FEFE (live 2026-11-17 : every ITEM of a shop list, FEFE FEFF) green ; "Back." / plain ones white
    float aw = 0.0f, ah = 0.0f;
    if (last.query && vis > 0) block_size(f, k, rows, vis, aw, ah);
    // one width for both : the widest of the text, the answers, and a floor ; the screen's 70 % at most
    float bw = snap(wText + 2.0f * pad + 14.0f * S); if (aw > bw) bw = aw;
    if (bw < snap(320.0f * S)) bw = snap(320.0f * S); if (bw > f.screenW * 0.7f) bw = snap(f.screenW * 0.7f);
    const float bh = nL ? snap((float)nL * lineH + 2.0f * pad) : 0.0f;
    const float gap = snap(3.0f * S), H = bh + (ah > 0.0f ? (bh > 0.0f ? gap : 0.0f) + ah : 0.0f);
    float bx = 0.0f, by = 0.0f;
    if (!place_on_chat(bw, H, snap(4.0f * S), f.screenW, f.screenH, bx, by)) { bx = snap(20.0f * S); by = snap(f.screenH * 0.72f - H); }
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
        const float ay = snap(by + (bh > 0.0f ? bh + gap : 0.0f) + (1.0f - tO) * 3.0f * S);
        const int cur = (last.sel >= g_npcView && last.sel < g_npcView + vis) ? last.sel - g_npcView + 1 : 0;
        const int row = block_draw(f, k, g_npcAns, rows, vis, cur, last.queryFocused, bx, ay, bw, ah, A, dt);
        if (last.n > vis) {                              // where this page sits in all the answers : a thin rail
            const float tx = bx + bw - 3.0f * S, ty = ay + k.pad, th = ah - 2.0f * k.pad;
            const float th2 = th * (float)vis / (float)last.n, ty2 = ty + th * (float)g_npcView / (float)last.n;
            rrect(dev, tx - 1.0f * S, ty, 2.0f * S, th, 1.0f * S, with_alpha(0x30FFFFFFu, A), with_alpha(0x30FFFFFFu, A));
            rrect(dev, tx - 1.0f * S, snap(ty2), 2.0f * S, snap(th2 < 6.0f * S ? 6.0f * S : th2), 1.0f * S, with_alpha(0xC0FFDA8Au, A), with_alpha(0xC0FFDA8Au, A));
        }
        if (ansOn) {                                     // the live answers only (not while they fade, not in the fallback)
            g_hit[3][0] = bx / f.screenW; g_hit[3][1] = ay / f.screenH; g_hit[3][2] = bw / f.screenW; g_hit[3][3] = ah / f.screenH;
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
        const float tx = bx + bw - 3.0f * S, ty = by + k.pad, th = bh - 2.0f * k.pad;
        const float th2 = th * (float)n / (float)last.count, ty2 = ty + th * (float)last.top / (float)last.count;
        dColorQuadState(dev);
        rrect(dev, tx - 1.0f * S, ty, 2.0f * S, th, 1.0f * S, with_alpha(0x30FFFFFFu, A), with_alpha(0x30FFFFFFu, A));
        rrect(dev, tx - 1.0f * S, snap(ty2), 2.0f * S, snap(th2 < 6.0f * S ? 6.0f * S : th2), 1.0f * S, with_alpha(0xC0FFDA8Au, A), with_alpha(0xC0FFDA8Au, A));
    }
    // the counter (and the filter typed before Tab), on the block's top-right, as the client's header
    fo->begin(dev);
    { const float w = fo->measure(cnt, cz);
      fo->draw_lc(dev, snap(bx + bw - k.pad - w), snap(by - lh * 0.5f), cnt, cz, with_alpha(0xFFB8C4D4u, A), with_alpha(0xC8000000u, A), k.ow); }

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
    const float S = k.S, pad = snap(5.0f * S);
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
    float persBottom = 0.0f;
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
        lh = (bh - 2.0f * pad - 2.0f * gap) / (float)(nTop + 10);
        if (tz > lh * 0.92f) tz = lh * 0.92f;
        const float c1 = fo->measure("STR", tz) + 8.0f * S, c2 = fo->measure("0000", tz), c3 = fo->measure("+000", tz) + 6.0f * S;
        float w = c1 + c2 + c3;
        const float wl[4] = { fo->measure(nm, tz * 1.05f), fo->measure(l1, tz), fo->measure(l2, tz), fo->measure(l3, tz) };
        for (int i = 0; i < 4; ++i) if (wl[i] > w) w = wl[i];
        const float vw = fo->measure("HP", tz) + 8.0f * S + fo->measure(hpS, tz); if (vw > w) w = vw;
        if (w + 2.0f * pad > bw) bw = snap(w + 2.0f * pad);
        float bx, by; place(lastP, bw, bh, bx, by);
        const float tO = ease_out(((float)(now - g_persB.openMs) / 1000.0f) / 0.08f);
        by = snap(by + (1.0f - tO) * 4.0f * S);
        const float A = g_persB.vis * tO;
        if (A > 0.0f) {
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
        persBottom = by + bh;
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
        lh = (bh - 2.0f * pad - 7.0f * S) / 5.0f;
        if (tz > lh * 0.92f) tz = lh * 0.92f;
        w = fo->measure(ml, tz); { const float w2[4] = { fo->measure(lEp, tz), fo->measure(ep, tz), fo->measure(lNx, tz), fo->measure(nx, tz) }; for (int i = 0; i < 4; ++i) if (w2[i] > w) w = w2[i]; }
        if (w + 2.0f * pad > bw) bw = snap(w + 2.0f * pad);
        if (drawP && g_persB.w > bw) bw = g_persB.w;
        float bx, by; place(lastL, bw, bh, bx, by);
        if (persBottom > 0.0f && by < persBottom + 3.0f * S) by = snap(persBottom + 3.0f * S);   // never under the stats
        const float tO = ease_out(((float)(now - g_lvlB.openMs) / 1000.0f) / 0.08f);
        by = snap(by + (1.0f - tO) * 4.0f * S);
        const float A = g_lvlB.vis * tO;
        if (A > 0.0f) {
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
    float bw = snap(252.0f * sx), bh = snap(((float)rowsN * 14.0f + 22.0f) * sy);
    const float vpad = snap(6.0f * S), lh = (bh - 2.0f * vpad) / (float)rowsN;
    float tz = k.tz; if (tz > lh * 0.92f) tz = lh * 0.92f;
    float bx = snap((float)lastW.rx0 * sx), by = snap((float)lastW.ry0 * sy);
    if (bx + bw > f.screenW - 4.0f) bx = snap(f.screenW - 4.0f - bw); if (bx < 4.0f) bx = 4.0f;
    if (by + bh > f.screenH - 4.0f) by = snap(f.screenH - 4.0f - bh); if (by < 4.0f) by = 4.0f;
    const float tO = ease_out(((float)(now - g_jobB.openMs) / 1000.0f) / 0.08f);
    by = snap(by + (1.0f - tO) * 4.0f * S);
    const float A = g_jobB.vis * tO; if (A <= 0.0f) return;
    const u32 dev = f.dev; Font* fo = f.font;
    draw_themed_box(dev, f.skin, bx, by, bw, bh, C.muBox, A, S);
    // the column split : a hairline between the two columns
    rrect(dev, snap(bx + bw * 0.5f), by + vpad, 1.0f, bh - 2.0f * vpad, 0.0f, with_alpha(0x24C8D6EAu, A), with_alpha(0x24C8D6EAu, A));
    const u32 sh = with_alpha(0xC8000000u, A);
    fo->begin(dev);
    const float colW = bw * 0.5f, ip = snap(10.0f * S);
    for (int i = 0; i < n; ++i) {
        const int j = ids[i], col = i & 1, row = i >> 1;
        const float cx = bx + (float)col * colW, cy = snap(by + vpad + (float)row * lh + lh * 0.5f);
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
    const float bw = snap(252.0f * sx), bh = snap(((float)rowsN * 14.0f + 22.0f) * sy);
    const float vpad = snap(6.0f * S), lh = (bh - 2.0f * vpad) / (float)rowsN;
    float tz = k.tz; if (tz > lh * 0.92f) tz = lh * 0.92f;
    float bx = snap((float)lastW.rx0 * sx), by = snap((float)lastW.ry0 * sy);
    if (bx + bw > f.screenW - 4.0f) bx = snap(f.screenW - 4.0f - bw); if (bx < 4.0f) bx = 4.0f;
    if (by + bh > f.screenH - 4.0f) by = snap(f.screenH - 4.0f - bh); if (by < 4.0f) by = 4.0f;
    const float tO = ease_out(((float)(now - g_sklB.openMs) / 1000.0f) / 0.08f);
    by = snap(by + (1.0f - tO) * 4.0f * S);
    const float A = g_sklB.vis * tO; if (A <= 0.0f) return;
    const u32 dev = f.dev; Font* fo = f.font;
    draw_themed_box(dev, f.skin, bx, by, bw, bh, C.muBox, A, S);
    rrect(dev, snap(bx + bw * 0.5f), by + vpad, 1.0f, bh - 2.0f * vpad, 0.0f, with_alpha(0x24C8D6EAu, A), with_alpha(0x24C8D6EAu, A));
    const u32 sh = with_alpha(0xC8000000u, A);
    fo->begin(dev);
    const float colW = bw * 0.5f, ip = snap(10.0f * S);
    for (int i = 0; i < n; ++i) {
        const int id = ids[i], col = i & 1, row = i >> 1;
        const float cx = bx + (float)col * colW, cy = snap(by + vpad + (float)row * lh + lh * 0.5f);
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
    const float S = k.S, vpad = snap(6.0f * S), ip = snap(10.0f * S);
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
                const float bw = snap(252.0f * sx), bh = snap(((float)n * 14.0f + 22.0f) * sy);
                const float lh = (bh - 2.0f * vpad) / (float)n;
                float tz = k.tz; if (tz > lh * 0.92f) tz = lh * 0.92f;
                float bx = snap((float)lastW.rx0 * sx), by = snap((float)lastW.ry0 * sy);
                if (bx + bw > f.screenW - 4.0f) bx = snap(f.screenW - 4.0f - bw); if (bx < 4.0f) bx = 4.0f;
                if (by + bh > f.screenH - 4.0f) by = snap(f.screenH - 4.0f - bh); if (by < 4.0f) by = 4.0f;
                const float tO = ease_out(((float)(now - g_trdB.openMs) / 1000.0f) / 0.08f);
                by = snap(by + (1.0f - tO) * 4.0f * S);
                const float A = g_trdB.vis * tO;
                if (A > 0.0f) {
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
                        fo->draw_lc(dev, snap(bx + bw * 0.44f), cy, rk, tz, with_alpha(capped ? 0xFF6FB4FFu : 0xFFF2F5FAu, A), sh, k.ow);
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
            const float bw = snap((float)(lastW.rx1 - lastW.rx0) * sx), bh = snap((float)(lastW.ry1 - lastW.ry0) * sy);
            const float lh = (bh - 2.0f * vpad) / 4.0f;
            float tz = k.tz; if (tz > lh * 0.92f) tz = lh * 0.92f;
            float bx = snap((float)lastW.rx0 * sx), by = snap((float)lastW.ry0 * sy);
            if (bx + bw > f.screenW - 4.0f) bx = snap(f.screenW - 4.0f - bw); if (bx < 4.0f) bx = 4.0f;
            if (by + bh > f.screenH - 4.0f) by = snap(f.screenH - 4.0f - bh); if (by < 4.0f) by = 4.0f;
            const float tO = ease_out(((float)(now - g_lmB.openMs) / 1000.0f) / 0.08f);
            by = snap(by + (1.0f - tO) * 4.0f * S);
            const float A = g_lmB.vis * tO;
            if (A > 0.0f) {
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
    const float pad = snap(8.0f * S), rowH = snap(k.capH * 0.92f), headH = snap(18.0f * S), fieldH = snap(k.capH * 1.05f);
    const float bw = snap(pad + 6.0f * S + wAll + 18.0f * S + fo->measure("9,999,999", vz) + 10.0f * S + pad);
    const float listY = headH + 6.0f * S + fieldH + 6.0f * S;
    const float bh = snap(pad + listY + (float)V * rowH + pad);
    const float sx = f.screenW / (float)last.uiW, sy = f.screenH / (float)last.uiH;
    float bx = snap((float)last.rx0 * sx), by = snap((float)last.ry0 * sy);
    if (bx + bw > f.screenW - 4.0f) bx = snap(f.screenW - 4.0f - bw); if (bx < 4.0f) bx = 4.0f;
    if (by + bh > f.screenH - 4.0f) by = snap(f.screenH - 4.0f - bh); if (by < 4.0f) by = 4.0f;
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
        const float rx = bx + bw - pad * 0.5f - 1.0f * S, th = (float)V * rowH;
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
    float bw = info ? snap(k.gutter + labW + 14.0f * S + valW + k.capPadR + pad) : snap(k.gutter + fo->measure(title, tz) + k.capPadR + pad);
    { float rw, rh; block_size(f, k, rows, 2, rw, rh); if (rw > bw) bw = rw; }
    const float sx = f.screenW / (float)last.uiW, sy = f.screenH / (float)last.uiH;
    { const float nW = snap((float)(last.rx1 - last.rx0) * sx * 0.85f); if (nW > bw) bw = nW; }
    const float inTop = info ? snap(pad + lh + 4.0f * S + 5.0f * lh + 4.0f * S + lh)   // title, 5 lines, "Unity chat settings:"
                             : snap(pad + lh + 4.0f * S);                            // title
    const float bh = snap(inTop + 2.0f * k.capH + k.gap + 2.0f * k.pad);
    float bx = snap((float)last.rx0 * sx), by = snap((float)last.ry0 * sy);
    if (bx + bw > f.screenW - 4.0f) bx = snap(f.screenW - 4.0f - bw); if (bx < 4.0f) bx = 4.0f;
    if (by + bh > f.screenH - 4.0f) by = snap(f.screenH - 4.0f - bh); if (by < 4.0f) by = 4.0f;
    const float tO = ease_out(((float)(now - g_uiB.openMs) / 1000.0f) / 0.08f);
    by = snap(by + (1.0f - tO) * 4.0f * S);
    const float A = g_uiB.vis * tO; if (A <= 0.0f) return;
    g_uiB.inTop = inTop;
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
    const float L = snap(bx + k.gutter), Rr = snap(bx + bw - k.capPadR - pad), X = snap(L + labW + 14.0f * S);
    const float ty = snap(by + pad + lh * 0.5f);
    rrect(dev, L, snap(by + pad + lh + 1.0f * S), Rr - L, 1.0f, 0.0f, with_alpha(0x30C8D6EAu, A), with_alpha(0x30C8D6EAu, A));
    const u32 sh = with_alpha(0xC8000000u, A), cl = with_alpha(0xFF93A6BEu, A), cv = with_alpha(0xFFE4EAF2u, A), cg = with_alpha(0xFFE8D9B0u, A);
    fo->begin(dev);
    fo->draw_lc(dev, L, ty, title, tz * 0.95f, cg, sh, k.ow);
    const float y0 = by + pad + lh + 4.0f * S;
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
    const float S = k.S, pad = snap(8.0f * S);
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
    { const float nW = snap((float)(last.rx1 - last.rx0) * sx * 0.85f); if (nW > bw) bw = nW; }
    float bx = snap((float)last.rx0 * sx), by = snap((float)last.ry0 * sy);
    if (bx + bw > f.screenW - 4.0f) bx = snap(f.screenW - 4.0f - bw); if (bx < 4.0f) bx = 4.0f;
    if (by + bh > f.screenH - 4.0f) by = snap(f.screenH - 4.0f - bh); if (by < 4.0f) by = 4.0f;
    const float tO = ease_out(((float)(now - g_urB.openMs) / 1000.0f) / 0.08f);
    by = snap(by + (1.0f - tO) * 4.0f * S);
    const float A = g_urB.vis * tO; if (A <= 0.0f) return;
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
            const float tO = ease_out(((float)(now - g_alB.openMs) / 1000.0f) / 0.08f);
            by = snap(by + (1.0f - tO) * 4.0f * S);
            const float A = g_alB.vis * tO;
            if (A > 0.0f) {
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
            const float pad = snap(8.0f * S), tz = k.tz, rowH = snap(k.capH * 0.92f), headH = snap(18.0f * S);
            const int V = last.vis;
            const float jobW = fo->measure("MNK99/WAR49", tz * 0.92f), nameW = fo->measure("Wwwwwwwwwwwwwww", tz) * 0.8f;
            float bw = snap(pad + 6.0f * S + jobW + 14.0f * S + nameW + 14.0f * S + fo->measure("Southern San d'Oria [S]", tz * 0.92f) + 6.0f * S + pad);
            const float listY = headH + 6.0f * S;
            const float bh = snap(pad + listY + (float)V * rowH + pad);
            const float sx = f.screenW / (float)last.uiW, sy = f.screenH / (float)last.uiH;
            float bx = snap((float)last.rx0 * sx), by = snap((float)last.ry0 * sy);
            if (bx + bw > f.screenW - 4.0f) bx = snap(f.screenW - 4.0f - bw); if (bx < 4.0f) bx = 4.0f;
            if (by + bh > f.screenH - 4.0f) by = snap(f.screenH - 4.0f - bh); if (by < 4.0f) by = 4.0f;
            const float tO = ease_out(((float)(now - g_srB.openMs) / 1000.0f) / 0.08f);
            by = snap(by + (1.0f - tO) * 4.0f * S);
            const float A = g_srB.vis * tO * (last.focused || !native_scoption().open ? 1.0f : 0.62f);
            if (A > 0.0f) {
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
                    const float rx = bx + bw - pad * 0.5f - 1.0f * S, th = (float)V * rowH;
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
                    if (mrow >= 0 && mrow + 1 != g_srB.hoverRow && mrow != S0.cursor) native_textlist_hover("scresult", mrow);
                    g_srB.hoverRow = mrow + 1;
                    if (mrow >= 0 && ms->clicked) native_textlist_pick("scresult", mrow);
                }
                const u32 sh = with_alpha(0xC8000000u, A);
                fo->begin(dev);
                fo->draw_lc(dev, L, hy, tr("Unity Lists", "Listes Unity"), tz * 0.95f, with_alpha(0xFFE8D9B0u, A), sh, k.ow);
                {
                    char cnt[48];
                    if (last.counted) sprintf(cnt, tr("%d found", "%d trouv\xC3\xA9(s)"), last.total); else strcpy(cnt, tr("Searching...", "Recherche..."));
                    fo->draw_lc(dev, snap(L + W - fo->measure(cnt, tz * 0.85f)), hy, cnt, tz * 0.85f, with_alpha(0xFF93A6BEu, A), sh, k.ow);
                }
                if (last.n == 0 && last.empty[0]) fo->draw_lc(dev, snap(L + (W - fo->measure(last.empty, tz)) * 0.5f), snap(ly + rowH * 0.5f), last.empty, tz, with_alpha(0xFFC9D3E0u, A), sh, k.ow);
                const unsigned myZone = f.game ? f.game->zone : 0;
                const float jx = snap(L + 6.0f * S), nx = snap(jx + jobW + 14.0f * S);
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
                    const u32 nc = (r.flags & 8) ? 0xFFFFD27Au : (r.flags & 0x2000) ? 0xFF8FD0FFu : (on ? 0xFFFFFFFFu : 0xFFE4EAF2u);
                    fo->draw_lc(dev, nx, cy, r.name, tz, with_alpha(nc, A), sh, k.ow);
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
            RowSpec rows[16] = {}; char nums[16][8]; int cur = 0;
            for (int i = 0; i < last.n && i < 16; ++i) {
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
            const float tO = ease_out(((float)(now - g_soB.openMs) / 1000.0f) / 0.07f);
            by = snap(by + (1.0f - tO) * 3.0f * S);
            const float A = g_soB.vis * tO;
            if (A > 0.0f) {
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
    const float S = k.S, pad = snap(8.0f * S), tz = k.tz, rowH = snap(k.capH * 0.92f), headH = snap(18.0f * S), gap = snap(6.0f * S);
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
    by = snap(by + (1.0f - tO) * 4.0f * S);
    // ======== the list / the page ========
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
        const float rx = bx + bw - pad * 0.5f - 1.0f * S, th = (float)V * rowH;
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
    const float y2 = snap(by + bh + gap);
    if (job >= 1 && job <= 23) {
        draw_themed_box(dev, f.skin, bx, y2, jw, jh, C.muBox, A, S);
        const float Lb = bx + pad, Rb = bx + jw - pad, hyb = snap(y2 + pad + headH * 0.5f), lyb = y2 + pad + headH + 6.0f * S;
        rrect(dev, Lb, snap(y2 + pad + headH), Rb - Lb, 1.0f, 0.0f, with_alpha(0x30C8D6EAu, A), with_alpha(0x30C8D6EAu, A));
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
        const float dx = snap(bx + jw + gap), dw = snap(bw - jw - gap);
        if (dw > 120.0f * S) {
            char lines[6][96]; const int nl = jp_wrap(fo, dBody, tz * 0.95f, dw - 2.0f * pad - 10.0f * S, lines, 6);
            const float dh = jh > snap(pad + headH + 6.0f * S + (float)nl * rowH + pad) ? jh : snap(pad + headH + 6.0f * S + (float)nl * rowH + pad);
            draw_themed_box(dev, f.skin, dx, y2, dw, dh, C.muBox, A, S);
            const float Ld = dx + pad, hyd = snap(y2 + pad + headH * 0.5f), lyd = y2 + pad + headH + 6.0f * S;
            rrect(dev, Ld, snap(y2 + pad + headH), dw - 2.0f * pad, 1.0f, 0.0f, with_alpha(0x30C8D6EAu, A), with_alpha(0x30C8D6EAu, A));
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
    const float S = k.S, pad = snap(8.0f * S), tz = k.tz, rowH = snap(k.capH * 0.92f), headH = snap(18.0f * S), gap = snap(6.0f * S);
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
    by = snap(by + (1.0f - tO) * 4.0f * S);
    // ======== the list ========
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
        const float rx = bx + bw - pad * 0.5f - 1.0f * S, th = (float)V * rowH;
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
        const bool me = i == cr, grey = (abil ? r.colLvl : r.colName) == GREY && abil, maxed = abil && r.colLvl == BLUE;
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
    const float y2 = snap(by + bh + gap);
    draw_themed_box(dev, f.skin, bx, y2, jw, jh, C.muBox, A, S);
    {
        const float Lb = bx + pad, Rb = bx + jw - pad, hyb = snap(y2 + pad + headH * 0.5f), cy = snap(y2 + pad + headH + 6.0f * S + rowH * 0.5f);
        rrect(dev, Lb, snap(y2 + pad + headH), Rb - Lb, 1.0f, 0.0f, with_alpha(0x30C8D6EAu, A), with_alpha(0x30C8D6EAu, A));
        fo->begin(dev);
        fo->draw_lc(dev, Lb, hyb, pl, tz * 0.95f, with_alpha(0xFFE8D9B0u, A), sh, k.ow);
        fo->draw_lc(dev, snap(Rb - fo->measure(pv, tz)), cy, pv, tz, with_alpha(last.points > 999 ? 0xFF6FB4FFu : 0xFFF2F5FAu, A), sh, k.ow);
    }
    if (dTitle[0]) {
        const float dx = snap(bx + jw + gap), dw = snap(bw - jw - gap);
        if (dw > 120.0f * S) {
            char lines[6][96]; const int nl = jp_wrap(fo, dBody, tz * 0.95f, dw - 2.0f * pad - 10.0f * S, lines, 6);
            const float need = snap(pad + headH + 6.0f * S + (float)nl * rowH + pad), dh = jh > need ? jh : need;
            draw_themed_box(dev, f.skin, dx, y2, dw, dh, C.muBox, A, S);
            const float Ld = dx + pad, hyd = snap(y2 + pad + headH * 0.5f), lyd = y2 + pad + headH + 6.0f * S;
            rrect(dev, Ld, snap(y2 + pad + headH), dw - 2.0f * pad, 1.0f, 0.0f, with_alpha(0x30C8D6EAu, A), with_alpha(0x30C8D6EAu, A));
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
    const float S = k.S, pad = snap(8.0f * S), tz = k.tz, rowH = snap(k.capH * 0.98f), headH = snap(18.0f * S), gap = snap(6.0f * S);
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
    const float bw = snap(pad + colW + gap + colW + pad);
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
    by = snap(by + (1.0f - tO) * 4.0f * S);
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
    const float y2 = snap(by + bh + gap);
    draw_themed_box(dev, f.skin, bx, y2, bw, dh, C.muBox, A, S);
    if (sel) {
        const float Ld = bx + pad, hyd = snap(y2 + pad + headH * 0.5f), lyd = y2 + pad + headH + 6.0f * S;
        rrect(dev, Ld, snap(y2 + pad + headH), bw - 2.0f * pad, 1.0f, 0.0f, with_alpha(0x30C8D6EAu, A), with_alpha(0x30C8D6EAu, A));
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
    const float S = k.S, pad = snap(8.0f * S), tz = k.tz, rowH = snap(k.capH * 0.92f), headH = snap(18.0f * S), gap = snap(6.0f * S);
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
    by = snap(by + (1.0f - tO) * 4.0f * S);
    // ======== the list ========
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
        const float rx = bx + bw - pad * 0.5f - 1.0f * S, th = (float)V * rowH;
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
    const float y2 = snap(by + bh + gap);
    draw_themed_box(dev, f.skin, bx, y2, jw, jh, C.muBox, A, S);
    {
        const float Lb = bx + pad, Rb = bx + jw - pad, lyb = y2 + pad;
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
        const float dx = snap(bx + jw + gap), dw = snap(bw - jw - gap);
        char lines[6][96]; const int nl = jp_wrap(fo, dBody, tz * 0.95f, dw - 2.0f * pad - 10.0f * S, lines, 6);
        const float need = snap(pad + headH + 6.0f * S + (float)nl * rowH + pad), dh = jh > need ? jh : need;
        draw_themed_box(dev, f.skin, dx, y2, dw, dh, C.muBox, A, S);
        const float Ld = dx + pad, hyd = snap(y2 + pad + headH * 0.5f), lyd = y2 + pad + headH + 6.0f * S;
        rrect(dev, Ld, snap(y2 + pad + headH), dw - 2.0f * pad, 1.0f, 0.0f, with_alpha(0x30C8D6EAu, A), with_alpha(0x30C8D6EAu, A));
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
    NativeEquip shown = last;                                            // //aio eqpreview : every state, on the real items
    if (g_eqPreview) {
        encMask = 0;
        for (int s = 0; s < 16; ++s) {
            shown.ward[s] = (unsigned char)(s % 9);                     // 0 the inventory (no badge), 1..8 the wardrobes
            if (s % 4 == 3) encMask |= 1u << s;                          // every 4th slot locked (Encumbrance)
        }
        shown.id[15] = 0;                                                // Back : an empty slot
        static const unsigned COLS[4] = { 0x80509050u, 0x80404040u, 0x80804020u, 0x80808080u };   // worn, refused, held, normal
        for (int i = 0; i < 16; ++i) shown.lrow[i].col = COLS[i % 4];
    }
    const NativeEquip& lastS = shown;
    const Metrics k = metrics(f);
    const float S = k.S, pad = snap(8.0f * S), tz = k.tz, headH = snap(18.0f * S), gap = snap(6.0f * S);
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
    const float bw = snap(pad + gridW + pad), bh = snap(pad + headH + 6.0f * S + gridW + 8.0f * S + 3.0f * statH + pad);
    const float sx = f.screenW / (float)last.uiW, sy = f.screenH / (float)last.uiH;
    float bx = snap((float)last.rx0 * sx), by = snap((float)last.ry0 * sy);
    if (bx + bw > f.screenW - 4.0f) bx = snap(f.screenW - 4.0f - bw); if (bx < 4.0f) bx = 4.0f;
    if (by + bh > f.screenH - 4.0f) by = snap(f.screenH - 4.0f - bh); if (by < 4.0f) by = 4.0f;
    by = snap(by + (1.0f - tO) * 4.0f * S);
    draw_themed_box(dev, f.skin, bx, by, bw, bh, C.muBox, A, S);
    const float L = bx + pad, hy = snap(by + pad + headH * 0.5f), gy = snap(by + pad + headH + 6.0f * S);
    rrect(dev, L, snap(by + pad + headH), gridW, 1.0f, 0.0f, with_alpha(0x30C8D6EAu, A), with_alpha(0x30C8D6EAu, A));
    int curCell = -1;
    for (int c = 0; c < 16; ++c) if (ENTRY_OF[SLOT_AT[c]] == last.cursor) curCell = c;
    // the cells : a dark well, the item's icon (or the slot's name), the hand's cell in the party glass
    const MouseState* ms = f.mouse; int mcell = -1;
    for (int c = 0; c < 16; ++c) {
        const float cx = snap(L + (float)(c % 4) * (cell + cg)), cy = snap(gy + (float)(c / 4) * (cell + cg));
        rrect(dev, cx, cy, cell, cell, 5.0f * S, with_alpha(0x70101826u, A), with_alpha(0x70080C14u, A));
        rrect_stroke(dev, cx, cy, cell, cell, 5.0f * S, with_alpha(0x30C8D6EAu, A), 1.0f);
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
    for (int i = 0; i < 8; ++i) {
        const float dx = snap(L + (float)(i / 2) * colW + 5.0f * S), dyc = snap(sy0 + statH * (1.5f + (float)(i % 2)));   // column-major, as the client
        disc(dev, dx, dyc, 3.2f * S, with_alpha(ELC[i], A));
    }
    fo->begin(dev);
    fo->draw_lc(dev, L, hy, tr("Equipment", "\xC3\x89quipement"), tz * 0.95f, with_alpha(0xFFE8D9B0u, A), sh, k.ow);
    if (curCell >= 0) { const char* sn = SLOT_EN[SLOT_AT[curCell]]; fo->draw_lc(dev, snap(L + gridW - fo->measure(sn, tz * 0.85f)), hy, sn, tz * 0.85f, with_alpha(0xFF93A6BEu, A), sh, k.ow); }
    for (int c = 0; c < 16; ++c) if (lastS.id[SLOT_AT[c]] && lastS.ward[SLOT_AT[c]]) {
        const float cx = snap(L + (float)(c % 4) * (cell + cg)), cy = snap(gy + (float)(c / 4) * (cell + cg));
        const float bz = snap(cell * 0.34f); char w[4]; sprintf(w, "%d", lastS.ward[SLOT_AT[c]]);
        fo->draw_lc(dev, snap(cx + cell - bz - 2.0f * S + (bz - fo->measure(w, tz * 0.75f)) * 0.5f), snap(cy + 2.0f * S + bz * 0.5f), w, tz * 0.75f, with_alpha(0xFFFFE6A8u, A), sh, k.ow);
    }
    for (int c = 0; c < 16; ++c) if (!lastS.id[SLOT_AT[c]]) {
        const float cx = snap(L + (float)(c % 4) * (cell + cg)), cy = snap(gy + (float)(c / 4) * (cell + cg));
        const char* sn = SLOT_EN[SLOT_AT[c]]; const float z = tz * 0.72f;
        fo->draw_lc(dev, snap(cx + (cell - fo->measure(sn, z)) * 0.5f), snap(cy + cell * 0.5f), sn, z, with_alpha(0xFF56627Au, A), sh, k.ow);
    }
    if (last.statsOk) {
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
        float gw = snap((float)(last.money.rx1 - last.money.rx0) * msx);
        { const float need = snap(2.0f * pad + fo->measure(gl, tz) + 24.0f * S); if (gw < need) gw = need; const float n2 = snap(2.0f * pad + fo->measure(g, tz)); if (gw < n2) gw = n2; }
        const float gh = snap(pad + headH + 6.0f * S + k.capH + pad);
        float gx = snap((float)last.money.rx0 * msx), gyy = snap((float)last.money.ry0 * msy);
        if (gx + gw > f.screenW - 4.0f) gx = snap(f.screenW - 4.0f - gw); if (gx < 4.0f) gx = 4.0f;
        if (gyy + gh > f.screenH - 4.0f) gyy = snap(f.screenH - 4.0f - gh); if (gyy < 4.0f) gyy = 4.0f;
        draw_themed_box(dev, f.skin, gx, gyy, gw, gh, C.muBox, A, S);
        const float Lg = gx + pad, Rg = gx + gw - pad, hyg = snap(gyy + pad + headH * 0.5f), vy = snap(gyy + pad + headH + 6.0f * S + k.capH * 0.5f);
        rrect(dev, Lg, snap(gyy + pad + headH), Rg - Lg, 1.0f, 0.0f, with_alpha(0x30C8D6EAu, A), with_alpha(0x30C8D6EAu, A));
        const float cr = 5.0f * S, ccx = snap(Rg - cr), ccy = hyg;                 // a gold coin
        disc(dev, ccx, ccy, cr, with_alpha(0xFFD9A93Au, A));
        disc(dev, ccx, ccy, cr * 0.72f, with_alpha(0xFFF2CD5Cu, A));
        disc(dev, ccx - cr * 0.3f, ccy - cr * 0.3f, cr * 0.28f, with_alpha(0xB0FFFFFFu, A));
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
        float wName = fo->measure("Wwwwwwwwwwwwwwwwwwww", tz) * 0.85f;
        const int nRows = last.ln - last.ltop < last.lvis ? last.ln - last.ltop : last.lvis;
        for (int i = 0; i < nRows; ++i) if (!lastS.lrow[i].header && lastS.lrow[i].id) { const char* nm = item_display_name(lastS.lrow[i].id); if (nm) { const float w = fo->measure(nm, tz); if (w > wName) wName = w; } }
        const float lw = snap(pad + 8.0f * S + iz + 8.0f * S + wName + 16.0f * S + fo->measure("99", tz) + 8.0f * S + pad);
        const float lh = bh;
        float lx = snap(bx + bw + gap), ly = by;
        if (lx + lw > f.screenW - 4.0f) lx = snap(bx - gap - lw);
        const float LA = g_elB.vis * tO;
        if (LA > 0.0f) {
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
                const float rx = lx + lw - pad * 0.5f - 1.0f * S, th = (float)last.lvis * rowH;
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
                const char* nm = r.id ? item_display_name(r.id) : 0;
                const u32 c = r.col == 0x80509050u ? 0xFF7CE07Cu : r.col == 0x80404040u ? 0xFF6E7C8Eu : r.col == 0x80804020u ? 0xFFFFB45Au : (i == cr ? 0xFFFFFFFFu : 0xFFE4EAF2u);
                fo->draw_lc(dev, snap(Lx + 8.0f * S + iz + 8.0f * S), cy, nm ? nm : "?", tz, with_alpha(c, LA), sh, k.ow);
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
struct ItemsBox { const NativeItemRow* row; int n, top, vis, cursor, tempRows; bool focused, live; const char* title; const char* counter; bool full; };
float items_box_width(const Frame& f, const Metrics& k, const ItemsBox& L) {
    Font* fo = f.font; const float S = k.S, tz = k.tz, pad = snap(8.0f * S), iz = snap(snap(k.capH * 1.25f) - 4.0f * S);
    const int nRows = L.n - L.top < L.vis ? L.n - L.top : L.vis;
    float wName = fo->measure("Wwwwwwwwwwwwwwwwwwww", tz) * 0.85f;
    for (int i = 0; i < nRows; ++i) if (L.row[i].itemId) { const char* nm = item_display_name(L.row[i].itemId); if (nm) { const float w = fo->measure(nm, tz); if (w > wName) wName = w; } }
    return snap(pad + 8.0f * S + iz + 8.0f * S + wName + 16.0f * S + fo->measure("999", tz) + 8.0f * S + pad);
}
unsigned items_box_draw(const Frame& f, const Metrics& k, Block& B, const ItemsBox& L, const char* win, float lx, float ly, float lw, float lh, float A, float dt, float* hit) {
    const UiConfig& C = ui_config(); Font* fo = f.font; const u32 dev = f.dev;
    const float S = k.S, pad = snap(8.0f * S), tz = k.tz, headH = snap(18.0f * S);
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
        const float sxr = lx + lw - pad * 0.5f - 1.0f * S, th = (float)L.vis * rowH;
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
        const char* nm = r.itemId ? item_display_name(r.itemId) : 0;
        fo->draw_lc(dev, snap(Lx + 8.0f * S + iz + 8.0f * S), cy, nm ? nm : "?", tz, with_alpha(item_row_rgb(r.itemCol, i == cr && L.focused), A), sh, k.ow);
        if (!r.tempRow && r.itemCount > 1) { char n[8]; sprintf(n, "%d", r.itemCount); fo->draw_lc(dev, snap(Lx + Wx - 8.0f * S - fo->measure(n, tz)), cy, n, tz, with_alpha(0xFF93A6BEu, A), sh, k.ow); }
    }
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
    const UiConfig& C = ui_config(); const float S = k.S, pad = snap(8.0f * S), headH = snap(18.0f * S);
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
    const float S = k.S, pad = snap(8.0f * S), tz = k.tz, headH = snap(18.0f * S), gap = snap(6.0f * S);
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
    by = snap(by + (1.0f - tO) * 4.0f * S);
    g_synGrid[0] = bx; g_synGrid[1] = by; g_synGrid[2] = bw; g_synGrid[3] = bh;
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
        for (int i = 0; i < 16; ++i) { hr[i].itemId = last.hItem[i]; hr[i].itemCount = 0; hr[i].itemCol = last.hFav[i] ? 0x80909040u : 0x80808080u; hr[i].tempRow = false; }
        char hc[16]; sprintf(hc, "%d / 10", last.hN);
        ItemsBox HL = { hr, last.hN, last.hTop, last.hVis, last.hCursor, 0, last.histFocused, Y0.open && Y0.histOpen, tr("History", "Historique"), hc, false };
        const float hw = items_box_width(f, k, HL), hh = snap(pad + headH + 6.0f * S + (float)last.hVis * snap(k.capH * 1.25f) + pad);
        const float th2 = ease_out(((float)(now - g_hisB.openMs) / 1000.0f) / 0.08f);
        float hx = snap(bx + bw + gap), hyy = by;
        if (hx + hw > f.screenW - 4.0f) hx = snap(f.screenW - 4.0f - hw);
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
    const float S = k.S, pad = snap(8.0f * S), headH = snap(18.0f * S), gap = snap(6.0f * S);
    const float tO = ease_out(((float)(now - g_itB.openMs) / 1000.0f) / 0.08f);
    const float A = g_itB.vis * tO; if (A <= 0.0f) return;
    const float rowH = snap(k.capH * 1.25f);
    const bool bin = last.binMode && last.binVis > 0 && last.binUiW > 0;
    // the inventory (Synthesis : the crystals, the materials)
    char cnt[24]; if (last.invCap > 0) sprintf(cnt, "%d / %d", last.invUsed, last.invCap); else sprintf(cnt, "%d", last.invUsed);
    const char* title = last.synMode == 1 ? tr("Crystals", "Cristaux") : last.synMode == 2 ? tr("Materials", "Mat\xC3\xA9riaux") : tr("Items", "Objets");
    ItemsBox IL = { last.itRow, last.itN, last.itTop, last.itVis, last.itCursor, last.tempRows, last.focused, I0.open,
                    title, last.synMode ? "" : cnt, !last.synMode && last.invCap > 0 && last.invUsed >= last.invCap };
    const float lw = items_box_width(f, k, IL), lh = snap(pad + headH + 6.0f * S + (float)last.itVis * rowH + pad);
    const float sx = f.screenW / (float)last.uiW, sy = f.screenH / (float)last.uiH;
    float lx = snap(16.0f * sx), ly = snap(48.0f * sy);       // TOP LEFT, as the client opens it (the bin / the grid push it aside)
    unsigned iid = 0;
    // where the quantity box goes : under the bin, or under the synthesis grid
    float qX = -1.0f, qY = 0.0f, qW = 0.0f; unsigned qid = 0;
    float hintY = 0.0f, hintX = 0.0f;
    if (bin) {
        char bc[24]; if (last.binCap > 0) sprintf(bc, "%d / %d", last.binN, last.binCap); else sprintf(bc, "%d", last.binN);
        ItemsBox BL = { last.binRow, last.binN, last.binTop, last.binVis, last.binCursor, 0, last.binFocused, I0.open && I0.binMode,
                        tr("Recycling Bin", "Corbeille"), bc, false };
        const float bw = items_box_width(f, k, BL), bh = snap(pad + headH + 6.0f * S + (float)last.binVis * rowH + pad);
        const float bsx = f.screenW / (float)last.binUiW, bsy = f.screenH / (float)last.binUiH;
        float bx = snap((float)last.bx0 * bsx), by = snap((float)last.by0 * bsy);
        if (by + bh > f.screenH - 4.0f) by = snap(f.screenH - 4.0f - bh); if (by < 4.0f) by = 4.0f; if (bx < 4.0f) bx = 4.0f;
        if (block_ease(g_binB, I0.open && I0.binMode, now, dt)) {
            const float tB = ease_out(((float)(now - g_binB.openMs) / 1000.0f) / 0.08f);
            const unsigned b = items_box_draw(f, k, g_binB, BL, "bank", bx, snap(by + (1.0f - tB) * 4.0f * S), bw, bh, g_binB.vis * tB * A, dt, g_hit[11]);
            if (last.binFocused) iid = b;
        }
        const int cr = last.binCursor - last.binTop;
        qX = bx; qY = snap(by + bh + gap); qW = bw; qid = cr >= 0 && cr < last.binVis ? last.binRow[cr].itemId : 0;
        hintX = bx; hintY = snap(by + bh + 9.0f * S);
        lx = snap(bx + bw + gap); ly = by;
    } else { g_binB.vis = 0.0f; g_binB.wasOpen = false; }
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
    if (bin) {   // the hint : what a pick does, and that the bin empties itself
        Font* fo = f.font; fo->begin(f.dev);
        const char* h = tr("Pick an item : back to your inventory. The bin empties on zoning / logout.", "Choisis un objet : il revient dans l'inventaire. La corbeille se vide au changement de zone / \xC3\xA0 la d\xC3\xA9""connexion.");
        fo->draw_lc(f.dev, hintX, hintY, h, k.tz * 0.82f, with_alpha(0xFF93A6BEu, A * g_binB.vis), with_alpha(0xC8000000u, A * g_binB.vis), k.ow);
    }
    if (lx + lw > f.screenW - 4.0f) lx = snap(f.screenW - 4.0f - lw); if (lx < 4.0f) lx = 4.0f;
    if (ly + lh > f.screenH - 4.0f) ly = snap(f.screenH - 4.0f - lh); if (ly < 4.0f) ly = 4.0f;
    ly = snap(ly + (1.0f - tO) * 4.0f * S);
    const float LA = A * (last.focused ? 1.0f : 0.8f);         // the sort options / Use / Dispose / the grid have the hand : it steps back a little
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
    const float sx = f.screenW / (float)last.uiW, sy = f.screenH / (float)last.uiH, vpad = snap(5.0f * S);
    float bh = snap((float)(last.ry1 - last.ry0) * sy); if (bh < 150.0f * S) bh = snap(150.0f * S);
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
    const float pad = snap(6.0f * S), flag = snap(lh * 0.95f), colon = 10.0f * S;
    const float vx = pad + lw + colon;                                       // the values' column (from the box's left)
    float wNat = flag + 4.0f * S + fo->measure(vNat, tz), wHome = fo->measure(vHome, tz);
    const float wRight = fo->measure(lPts, tz) + 8.0f * S;                  // "Rank Points" + its bar, on the right half
    float bw = vx + (wNat > wHome ? wNat : wHome) + pad;
    const float rankRow = vx + fo->measure(rk, tz) + 24.0f * S + wRight + pad;
    if (rankRow > bw) bw = rankRow;
    { const float nW = (float)(last.rx1 - last.rx0) * sx; if (bw < nW) bw = nW; }
    bw = snap(bw);
    const int rows = 6;                                                      // nation, race, home, rank, the bar, mastery
    float bx = snap((float)last.rx0 * sx), by = snap((float)last.ry0 * sy);
    if (bx + bw > f.screenW - 4.0f) bx = snap(f.screenW - 4.0f - bw); if (bx < 4.0f) bx = 4.0f;
    if (by + bh > f.screenH - 4.0f) by = snap(f.screenH - 4.0f - bh); if (by < 4.0f) by = 4.0f;
    const float tO = ease_out(((float)(now - g_profB.openMs) / 1000.0f) / 0.08f);
    by = snap(by + (1.0f - tO) * 4.0f * S);
    const float A = g_profB.vis * tO; if (A <= 0.0f) return;
    const u32 dev = f.dev;
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
    const bool chain = M.open || native_main_sub().open || native_main_sort().open;   // the menu stays drawn under what it opened
    if (!block_ease(g_mainMB, chain, now, dt) || !last.n || last.uiW <= 0 || last.uiH <= 0) return;
    const Metrics k = metrics(f), kN = no_icons(k);
    const float S = k.S;
    const bool page2 = !strcmp(last.name, "socialme");
    const SubRow* T = page2 ? MAIN2 : MAIN1;
    RowSpec rows[16]; char nums[16][12]; int cur = 0;
    for (int i = 0; i < last.n && i < 16; ++i) {
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
        const float need = snap(k.gutter + f.font->measure(title, tzH) + 14.0f * S + f.font->measure(pg, tzH) + 18.0f * S + k.pad);
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
    const float tOpen = ease_out(((float)(now - g_mainMB.openMs) / 1000.0f) / 0.08f);
    by = snap(by + (1.0f - tOpen) * 4.0f * S);
    // Magic's categories open IN PLACE of this menu (the client swaps them, as the action menu's Abilities) : while they
    // are up this menu is not drawn (2026-11-18, "le sous menu remplace le main menu plutot que de se mettre a cote")
    const bool subUp = native_main_sub().open || native_main_sort().open;
    const float A = subUp ? 0.0f : g_mainMB.vis * tOpen * (last.focused ? 1.0f : 0.62f);   // a window it opened on top : steps back
    if (A > 0.0f) {
        const int row = block_draw(f, kN, g_mainMB, rows, last.n, cur, last.focused, bx, by, bw, bh, A, dt);
        // the header : a hairline under it, the title, the page between its two arrows
        const u32 dev = f.dev;
        const float hy = by + k.pad, hc = snap(hy + (headH - k.gap) * 0.5f);
        rrect(dev, bx + k.gutter, snap(hy + headH - k.gap * 0.5f - 1.0f), bw - k.gutter - k.pad, 1.0f, 0.0f, with_alpha(0x30C8D6EAu, A), with_alpha(0x30C8D6EAu, A));
        const float pw = f.font->measure(pg, tzH), ar = 3.0f * S;
        const float rx = snap(bx + bw - k.pad - 4.0f * S), lx = snap(rx - 2.0f * ar - 6.0f * S - pw - 6.0f * S);
        const u32 ac = with_alpha(0xC0FFDA8Au, A);
        tri(dev, rx, hc, rx - 2.0f * ar, hc - ar * 1.2f, rx - 2.0f * ar, hc + ar * 1.2f, ac);
        tri(dev, lx, hc, lx + 2.0f * ar, hc - ar * 1.2f, lx + 2.0f * ar, hc + ar * 1.2f, ac);
        Font* fo = f.font;
        fo->begin(dev);
        fo->draw_lc(dev, snap(bx + k.gutter + k.capPadL * 0.5f), hc, title, tzH, with_alpha(0xFFE8D9B0u, A), with_alpha(0xC8000000u, A), k.ow);
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
        RowSpec orows[16]; char onums[16][12]; int ocur = 0;
        for (int i = 0; i < lastO.n && i < 16; ++i) {
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
        const float need = snap(k.gutter + f.font->measure(ot, tzH) + 14.0f * S + f.font->measure(hint, tzH * 0.9f) + k.pad + 4.0f * S);
        if (need > ow) ow = need;
        oh = snap(oh + headH); g_mainSortB.inTop = headH;
        const float tS = ease_out(((float)(now - g_mainSortB.openMs) / 1000.0f) / 0.07f);
        const float obx = C.muMainPos == 1 ? bx : snap(bx + bw - ow), oby = snap(by + (1.0f - tS) * 3.0f * S);
        const float OA = g_mainSortB.vis * tS * (lastO.focused ? 1.0f : 0.62f);
        if (OA > 0.0f) {
            const int orow = block_draw(f, kN, g_mainSortB, orows, lastO.n, lastO.focused ? ocur : 0, lastO.focused, obx, oby, ow, oh, OA, dt);
            const float hc = snap(oby + k.pad + (headH - k.gap) * 0.5f);
            rrect(f.dev, obx + k.gutter, snap(oby + k.pad + headH - k.gap * 0.5f - 1.0f), ow - k.gutter - k.pad, 1.0f, 0.0f, with_alpha(0x30C8D6EAu, OA), with_alpha(0x30C8D6EAu, OA));
            f.font->begin(f.dev);
            f.font->draw_lc(f.dev, snap(obx + k.gutter + k.capPadL * 0.5f), hc, ot, tzH, with_alpha(0xFFE8D9B0u, OA), with_alpha(0xC8000000u, OA), k.ow);
            const float hw = f.font->measure(hint, tzH * 0.9f);
            f.font->draw_lc(f.dev, snap(obx + ow - k.pad - 4.0f * S - hw), hc, hint, tzH * 0.9f, with_alpha(0xFF93A6BEu, OA), with_alpha(0xC8000000u, OA), k.ow);
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
        RowSpec srows[16]; char snums[16][12]; int scur = 0;
        for (int i = 0; i < lastS.n && i < 16; ++i) {
            const SubRow* r = sub_row(lastS.name, lastS.entry[i]);
            if (r) { srows[i].label = tr(r->en, r->fr); srows[i].icon = r->icon; }
            else if (lastS.label[i][0]) { srows[i].label = fr_of(lastS.label[i]); srows[i].icon = IC_STAR; }
            else { sprintf(snums[i], "#%d", lastS.entry[i]); srows[i].label = snums[i]; srows[i].icon = IC_STAR; }
            srows[i].sub = false; srows[i].off = lastS.state[i] == 5;
            // Mode Switch : the client greys the CURRENT mode -- shown as the one selected instead (the user, 2026-11-18)
            if (!strcmp(lastS.name, "merit2") && lastS.state[i] == 5) {
                srows[i].off = false; srows[i].mark = true; srows[i].tint = 0xFFFFDA8Au; srows[i].extra = tr("current", "actuel");
            }
            if (lastS.entry[i] == lastS.cursor) scur = i + 1;
        }
        float sw, sh; block_size(f, kN, srows, lastS.n, sw, sh);
        const float tS = ease_out(((float)(now - g_mainSubB.openMs) / 1000.0f) / 0.07f);
        // in place : the menu's corner (its right edge at the game's place, its left edge on the left), its own size
        float sbx = C.muMainPos == 1 ? bx : snap(bx + bw - sw), sby = snap(by + (1.0f - tS) * 3.0f * S);
        if (sby + sh > f.screenH - 4.0f) sby = snap(f.screenH - 4.0f - sh); if (sby < 4.0f) sby = 4.0f;
        const float SA = g_mainSubB.vis * tS * (lastS.focused ? 1.0f : 0.62f);
        if (SA > 0.0f) {
            const int srow = block_draw(f, kN, g_mainSubB, srows, lastS.n, scur, lastS.focused, sbx, sby, sw, sh, SA, dt);
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
        const float ybx = C.muMainPos == 1 ? ynX : snap(ynX + ynW - yw), yby = snap(ynY + 3.0f * S + (1.0f - tY) * 3.0f * S);
        const float YA = g_mainYnB.vis * tY;
        if (YA > 0.0f) {
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
        ly = snap(ly + (1.0f - tL) * 3.0f * S);
        if (lx < 4.0f) lx = 4.0f; if (lx + lw > f.screenW - 4.0f) lx = snap(f.screenW - 4.0f - lw);
        if (ly + lh > f.screenH - 4.0f) ly = snap(f.screenH - 4.0f - lh); if (ly < 4.0f) ly = 4.0f;
        list_paint(f, k, g_mainListB, lastL, lrows, mine, lx, ly, lw, lh, g_mainListB.vis * tL, tL, dt, g_hit[7]);
        // ABILITIES : the description under the cursor (the client writes it to its help line) -- the user, 2026-11-19
        const int cr = lastL.cursor - lastL.top;
        if (lastL.kind == 1 && cr >= 0 && cr < lastL.rows) {
            const unsigned aid = lastL.id[cr];
            const char* d = native_ability_text(aid, true);
            unsigned mp = 0; bool ws = false; const char* nm = list_row_name(1, aid, mp, ws);
            if (d && d[0]) {
                char body[400], title[96]; jp_clean(d, body, sizeof(body)); jp_clean(nm, title, sizeof(title));
                for (char* p = body; *p; ++p) if (*p == '\n') *p = ' ';
                const float pad = snap(8.0f * S), tz = k.tz, headH = snap(18.0f * S), rowH = snap(k.capH * 0.95f), dw = lw;
                char lines[6][96]; const int nl = jp_wrap(f.font, body, tz * 0.92f, dw - 2.0f * pad, lines, 6);
                const float dh = snap(pad + headH + 6.0f * S + (float)nl * rowH + pad);
                float dy = snap(ly - 3.0f * S - dh); if (dy < 4.0f) dy = snap(ly + lh + 3.0f * S);
                const float DA = g_mainListB.vis * tL;
                draw_themed_box(f.dev, f.skin, lx, dy, dw, dh, C.muBox, DA, S);
                rrect(f.dev, lx + pad, snap(dy + pad + headH), dw - 2.0f * pad, 1.0f, 0.0f, with_alpha(0x30C8D6EAu, DA), with_alpha(0x30C8D6EAu, DA));
                f.font->begin(f.dev);
                const u32 shd = with_alpha(0xC8000000u, DA);
                f.font->draw_lc(f.dev, snap(lx + pad), snap(dy + pad + headH * 0.5f), title, tz * 0.95f, with_alpha(0xFFE8D9B0u, DA), shd, k.ow);
                for (int i = 0; i < nl; ++i) f.font->draw_lc(f.dev, snap(lx + pad), snap(dy + pad + headH + 6.0f * S + (float)i * rowH + rowH * 0.5f), lines[i], tz * 0.92f, with_alpha(0xFFC9D3E0u, DA), shd, k.ow);
            }
        }
    }
}

} // namespace aio
