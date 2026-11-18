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
float g_hit[9][4] = { { 0, 0, 0, 0 } };   // the drawn blocks (menu, submenu ; NPC bubble, NPC answers ; the auto-translate picker ; the main menu, its categories, its list) as screen FRACTIONS : their clicks never reach the game

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
const SubRow* sub_row(const char* menu, int entry) {
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
    if (id < 256) { const WSRow* w = ws_info(id); ws = true; return w ? w->en : "?"; }
    if (id >= 512) { const AbilRow* a = abil_info(id - 512); if (!a) return "?"; mp = a->mp; return a->en; }
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
void actmenu_hits_clear() { for (int i = 0; i < 9; ++i) g_hit[i][2] = 0.0f; }
// the auto-translate picker (g_hit[4]) : the wheel over it steps the client's selection (fep_draw consumes it)
static volatile long g_fepWheel = 0;
bool fep_hit(float fx, float fy) {
    const float* h = g_hit[4];
    return h[2] > 0.0f && fx >= h[0] && fx < h[0] + h[2] && fy >= h[1] && fy < h[1] + h[3];
}
void fep_wheel_add(int d) { InterlockedExchangeAdd(&g_fepWheel, d); }
bool actmenu_hit(float fx, float fy) {
    for (int i = 0; i < 9; ++i) {
        const float* h = g_hit[i];
        if (h[2] > 0.0f && fx >= h[0] && fx < h[0] + h[2] && fy >= h[1] && fy < h[1] + h[3]) return true;
    }
    return false;
}

namespace {   // the item card (below, with the NPC windows) : the Items list draws it too
void card_load(u32 dev, unsigned id);
bool card_ready(unsigned id);
void card_draw(const Frame& f, const Metrics& k, float bx, float bw, float top, float bottom, float tO, float A, bool above);

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
    if (L.kind == 3 && cur >= 1 && cur <= L.rows) {
        const unsigned iid = L.id[cur - 1];
        card_load(f.dev, iid);
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
        const char* e = p; while (*e && *e != ' ' && *e != '\n') ++e;
        char word[160]; int wl = (int)(e - p); if (wl > 150) wl = 150; memcpy(word, p, (size_t)wl); word[wl] = 0;
        char trial[320]; _snprintf(trial, sizeof(trial), cl ? "%s %s" : "%s%s", cur, word); trial[sizeof(trial) - 1] = 0;
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
        // every line of the column, in order (the description wrapped to the column)
        char lines[18][160]; int nl = 0;
        lstrcpynA(lines[nl++], title, 160);
        if (kind[0]) lstrcpynA(lines[nl++], kind, 160);
        if (g_card.desc[0]) nl += wrap_lines(fo, g_card.desc, ctz, tw, lines + nl, 14);
        if (lv[0] && nl < 18) lstrcpynA(lines[nl++], lv, 160);
        const float colH = (float)nl * clH, encH = ench[0] ? clH : 0.0f;
        const float ch = snap(cpad + (colH > icon ? colH : icon) + encH + cpad);
        float cy = above ? snap(top - snap(4.0f * S) - ch) : top;
        if (above && cy < 4.0f) cy = snap(bottom + snap(4.0f * S));
        if (!above && cy + ch > f.screenH - 4.0f) cy = snap(f.screenH - 4.0f - ch);
        cy = snap(cy + (1.0f - tO) * 3.0f * S);
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
        for (int i = 0; i < nl; ++i) fo->draw_lc(dev, colX, snap(cy + cpad + (float)i * clH + clH * 0.5f), lines[i], ctz, white, sh, k.ow);
        if (ench[0]) {
            const float ew = fo->measure(ench, ctz);
            fo->draw_lc(dev, snap(bx + bw - cpad - ew), snap(cy + ch - cpad - clH * 0.5f), ench, ctz, with_alpha(0xFF9CC8FFu, A), sh, k.ow);
        }
    }
}

void npc_card_forget()  {   // device lost : the handles are gone with it (the list icons' too -- they reload on demand)
    g_cardIcon = 0; g_cardIconId = 0; g_cardId = 0;
    for (int i = 0; i < 64; ++i) g_icons[i] = IconSlot();
    for (int i = 0; i < 4; ++i) g_pearl[i] = PearlTex();
    g_elemTex = g_jobTex = g_wpnTex = 0; g_elemR = TexRetry(); g_jobR = TexRetry(); g_wpnR = TexRetry();
}
void npc_card_dispose() {
    if (g_cardIcon) release_texture(g_cardIcon);
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
    const bool onP = PP.open && D.ok, onL = PL.open && D.ok;
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
    const bool on = P.open && P.ok;
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
    const bool chain = M.open || native_main_sub().open;   // the menu stays drawn under what it opened
    if (!block_ease(g_mainMB, chain, now, dt) || !last.n || last.uiW <= 0 || last.uiH <= 0) return;
    const Metrics k = metrics(f);
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
            float w1, h1; block_size(f, k, one, 1, w1, h1); if (w1 > bw) bw = w1;
        }
        float w2; block_size(f, k, rows, last.n, w2, bh);
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
    const bool subUp = native_main_sub().open;
    const float A = subUp ? 0.0f : g_mainMB.vis * tOpen * (last.focused ? 1.0f : 0.62f);   // a window it opened on top : steps back
    if (A > 0.0f) {
        const int row = block_draw(f, k, g_mainMB, rows, last.n, cur, last.focused, bx, by, bw, bh, A, dt);
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
    if (block_ease(g_mainSortB, SO.open, now, dt) && lastO.n) {
        RowSpec orows[16]; char onums[16][12]; int ocur = 0;
        for (int i = 0; i < lastO.n && i < 16; ++i) {
            const int e = lastO.entry[i];
            if (e == 1) orows[i].label = tr("Auto-sort", "Tri automatique");
            else if (e == 2) orows[i].label = tr("Manual Sort", "Tri manuel");
            else if (e == 3 && !strcmp(lastO.name, "blusortw")) orows[i].label = tr("Blue Magic", "Magie bleue");   // set the spells
            else if (lastO.label[i][0]) orows[i].label = lastO.label[i];
            else { sprintf(onums[i], "#%d", e); orows[i].label = onums[i]; }
            orows[i].icon = IC_NONE; orows[i].sub = false; orows[i].off = lastO.state[i] == 5;
            if (e == lastO.cursor) ocur = i + 1;
        }
        float ow, oh; block_size(f, k, orows, lastO.n, ow, oh);
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
            const int orow = block_draw(f, k, g_mainSortB, orows, lastO.n, lastO.focused ? ocur : 0, lastO.focused, obx, oby, ow, oh, OA, dt);
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
        // Auto-sort's confirmation (sortyn) : Yes / No, under the options, right edges level (the client's place)
        const NativeEntryMenu& YN = native_main_yn();
        static NativeEntryMenu lastY;
        if (YN.open) lastY = YN;
        if (block_ease(g_mainYnB, YN.open, now, dt) && lastY.n) {
            RowSpec yrows[4]; char ynums[4][12]; int ycur = 0; int yn = 0;
            for (int i = 0; i < lastY.n && yn < 4; ++i, ++yn) {
                const int e = lastY.entry[i];
                if (e == 1) yrows[yn].label = tr("Yes", "Oui");
                else if (e == 2) yrows[yn].label = tr("No", "Non");
                else { sprintf(ynums[yn], "#%d", e); yrows[yn].label = ynums[yn]; }
                yrows[yn].icon = IC_NONE; yrows[yn].sub = false; yrows[yn].off = false;
                if (e == lastY.cursor) ycur = yn + 1;
            }
            float yw, yh; block_size(f, k, yrows, yn, yw, yh);
            const float tY = ease_out(((float)(now - g_mainYnB.openMs) / 1000.0f) / 0.07f);
            const float ybx = C.muMainPos == 1 ? obx : snap(obx + ow - yw), yby = snap(oby + oh + 3.0f * S + (1.0f - tY) * 3.0f * S);
            const float YA = g_mainYnB.vis * tY;
            if (YA > 0.0f) {
                const int yrow = block_draw(f, k, g_mainYnB, yrows, yn, ycur, lastY.focused, ybx, yby, yw, yh, YA, dt);
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
    if (block_ease(g_mainSubB, SM.open && !SO.open, now, dt) && lastS.n) {
        RowSpec srows[16]; char snums[16][12]; int scur = 0;
        for (int i = 0; i < lastS.n && i < 16; ++i) {
            const SubRow* r = sub_row(lastS.name, lastS.entry[i]);
            if (r) { srows[i].label = tr(r->en, r->fr); srows[i].icon = r->icon; }
            else if (lastS.label[i][0]) { srows[i].label = fr_of(lastS.label[i]); srows[i].icon = IC_STAR; }
            else { sprintf(snums[i], "#%d", lastS.entry[i]); srows[i].label = snums[i]; srows[i].icon = IC_STAR; }
            srows[i].sub = false; srows[i].off = lastS.state[i] == 5;
            if (lastS.entry[i] == lastS.cursor) scur = i + 1;
        }
        float sw, sh; block_size(f, k, srows, lastS.n, sw, sh);
        const float tS = ease_out(((float)(now - g_mainSubB.openMs) / 1000.0f) / 0.07f);
        // in place : the menu's corner (its right edge at the game's place, its left edge on the left), its own size
        float sbx = C.muMainPos == 1 ? bx : snap(bx + bw - sw), sby = snap(by + (1.0f - tS) * 3.0f * S);
        if (sby + sh > f.screenH - 4.0f) sby = snap(f.screenH - 4.0f - sh); if (sby < 4.0f) sby = 4.0f;
        const float SA = g_mainSubB.vis * tS * (lastS.focused ? 1.0f : 0.62f);
        if (SA > 0.0f) {
            const int srow = block_draw(f, k, g_mainSubB, srows, lastS.n, scur, lastS.focused, sbx, sby, sw, sh, SA, dt);
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
    }
}

} // namespace aio
