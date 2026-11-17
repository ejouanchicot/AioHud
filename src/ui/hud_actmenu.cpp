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
#include "model/abilities_gen.h"        // abil_info : the ability list's job abilities
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
float g_hit[5][4] = { { 0, 0, 0, 0 } };   // the drawn blocks (menu, submenu ; NPC bubble, NPC answers ; the auto-translate picker) as screen FRACTIONS : their clicks never reach the game

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
const SubRow* sub_row(const char* menu, int entry) {
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
                 u32 tex = 0; float tu0 = 0.0f, tu1 = 1.0f, tv0 = 0.0f, tv1 = 1.0f; u32 texTint = 0xFFFFFFFFu; };
                 // tex + its UV : the row's picture (Windower's file, a status / element / job cell), tinted texTint   // tex : the row's own picture (a spell / ability icon from Windower's plugins\icons), drawn in place of `icon`   // tint : the label's own colour (the NPC answers : the client's green), 0 the menus' grey-white   // center : a lone message row (the empty list), centred in the block   // cycle : the extra can be changed with RIGHT (the Tell name ; left closes the submenu) -> name >
struct Block {
    float vis = 0.0f, curY = -1.0f; unsigned openMs = 0; int lastN = 0; bool wasOpen = false; float sel[16] = { 0 };
    float x = 0, y = 0, w = 0, h = 0;           // drawn rect this frame (w = 0 : not drawn)
    float rowY0 = 0, rowStep = 0, capX = 0, capW = 0, capH = 0;
    int hoverRow = 0;
};
Block g_main, g_subB, g_listB;

// a LIST row from its id : kind 0 a spell id, kind 1 the client's ability index (weapon skills 0..255, job abilities
// 512 + their id -- native_ui.h). `mp` gets the MP cost (0 none) ; `ws` says it is a weapon skill (needs 1000 TP).
const char* list_row_name(int kind, unsigned id, unsigned& mp, bool& ws) {
    mp = 0; ws = false;
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
bool icon_file(unsigned key, u32* px) { wchar_t path[MAX_PATH]; return icon_path(key, path, MAX_PATH) && load_png_argb(path, 32, 32, px); }
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
    if (kind == 0) key = (1u << 24) | ((id & 0xFFFFu) << 8);
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
    if (said < 6) { ++said; windower::debug::log("ACTMENU icon %s %u%s -> %s", kind == 0 ? "spell" : "ability recast", (key >> 8) & 0xFFFFu, (key & 0xFF) ? " (per job)" : "", tex ? "loaded" : "none (drawn icon kept)"); }
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
        if (rows[i].pearl) ew += 14.0f * k.S;
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
    b.x = bx; b.y = by; b.w = bw; b.h = bh; b.rowY0 = by + k.pad; b.rowStep = step; b.capX = cx0; b.capW = capW; b.capH = k.capH;

    draw_themed_box(dev, f.skin, bx, by, bw, bh, ui_config().muBox, A, S);   // Game UI -> Menus : one theme for all
    const bool hasCur = cursorRow >= 1 && cursorRow <= n;
    if (hasCur) {
        const float targetY = by + k.pad + (float)(cursorRow - 1) * step;
        if (b.curY < 0.0f || b.lastN != n) b.curY = targetY;
        b.curY += (targetY - b.curY) * kFast;
        if (fabsf(targetY - b.curY) < 0.3f) b.curY = targetY;
    }
    b.lastN = n;
    for (int i = 0; i < n && i < 16; ++i) {
        const bool on = i + 1 == cursorRow;
        b.sel[i] += ((on ? 1.0f : 0.0f) - b.sel[i]) * kSel;
        if (fabsf((on ? 1.0f : 0.0f) - b.sel[i]) < 0.004f) b.sel[i] = on ? 1.0f : 0.0f;
        const float y = snap(by + k.pad + (float)i * step);
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
        const float cy = snap(by + k.pad + (float)i * step) + k.capH * 0.5f;
        if (k.icons && rows[i].tex) {                          // a picture : a touch larger than the drawn glyph, the capsule's height
            const float z = snap(k.capH - 3.0f * S), ix = snap(cx0 + k.capPadL + k.icZ * 0.5f - z * 0.5f), iy = snap(cy - z * 0.5f);
            const u32 tc = with_alpha(rows[i].texTint, a);
            dTexQuadState(dev, rows[i].tex);
            tquad(dev, ix, iy, z, z, rows[i].tu0, rows[i].tu1, rows[i].tv0, rows[i].tv1, tc, tc);
            dSetTex(dev, 0, 0); cs(dev);
        } else if (k.icons) draw_icon(dev, rows[i].icon, cx0 + k.capPadL + k.icZ * 0.5f, cy, k.icZ, S, with_alpha(mix(0xFF9FB0C6u, 0xFFFFF1CCu, s), a));
        if (rows[i].pearl) {                                   // the shell worn, in its colour, right-aligned before its name
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
        const float y = snap(by + k.pad + (float)i * step);
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
    const float ry = ms->y - (by + k.pad);
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
void actmenu_hits_clear() { for (int i = 0; i < 5; ++i) g_hit[i][2] = 0.0f; }
// the auto-translate picker (g_hit[4]) : the wheel over it steps the client's selection (fep_draw consumes it)
static volatile long g_fepWheel = 0;
bool fep_hit(float fx, float fy) {
    const float* h = g_hit[4];
    return h[2] > 0.0f && fx >= h[0] && fx < h[0] + h[2] && fy >= h[1] && fy < h[1] + h[3];
}
void fep_wheel_add(int d) { InterlockedExchangeAdd(&g_fepWheel, d); }
bool actmenu_hit(float fx, float fy) {
    for (int i = 0; i < 5; ++i) {
        const float* h = g_hit[i];
        if (h[2] > 0.0f && fx >= h[0] && fx < h[0] + h[2] && fy >= h[1] && fy < h[1] + h[3]) return true;
    }
    return false;
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
            RowSpec lrows[32]; char mps[32][8];
            // EMPTY : the client opens the list at its FULL size and writes "No spells available." in its first row
            const bool empty = lastL.n == 0;
            if (empty) lastL.rows = 1;
            const bool live = f.game && f.game->me.name[0];
            const int mpNow = live ? f.game->me.mp : -1, tpNow = live ? (int)f.game->me.tp : -1;
            for (int i = 0; i < lastL.rows && i < 32; ++i) {
                if (empty) {
                    lrows[i].label = lastL.kind ? tr("No abilities available.", "Aucune aptitude disponible.") : tr("No spells available.", "Aucun sort disponible.");
                    lrows[i].icon = IC_NONE; lrows[i].sub = false; lrows[i].off = false; lrows[i].center = true; break;
                }
                unsigned mp; bool ws;
                lrows[i].label = list_row_name(lastL.kind, lastL.id[i], mp, ws); lrows[i].icon = lastL.kind ? (ws ? IC_SWORD : IC_BOLT) : IC_STAR; lrows[i].sub = false;
                lrows[i].tex = list_icon(f, lastL.kind, lastL.id[i]);
                if (!lrows[i].tex) list_fallback(f, lastL.kind, lastL.id[i], lrows[i]);
                if (mp) { sprintf(mps[i], "%u", mp); lrows[i].extra = mps[i]; }
                lrows[i].off = (mp && mpNow >= 0 && (int)mp > mpNow) || (ws && tpNow >= 0 && tpNow < 1000);   // not usable right now : dimmed
            }
            float lw, lh; block_size(f, k, lrows, lastL.rows, lw, lh);
            if (empty) {                                        // the full list's height : as many rows as a page shows
                const int pg = lastL.vis > 0 ? lastL.vis : 12;
                lh = snap((float)pg * k.capH + (float)(pg - 1) * k.gap + 2.0f * k.pad);
            }
            // a FIXED width : the widest spell of the WHOLE list (cached per list), so scrolling never resizes it
            {
                static unsigned sig = 0; static float wAll = 0.0f; static float atS = 0.0f;
                unsigned h = (2166136261u ^ (unsigned)lastL.allN) * 16777619u ^ (unsigned)lastL.kind;
                for (int i = 0; i < lastL.allN; ++i) h = (h ^ lastL.all[i]) * 16777619u;
                if (h != sig || atS != k.S) {
                    sig = h; atS = k.S; wAll = 0.0f;
                    RowSpec one[1]; char mp1[8];
                    for (int i = 0; i < lastL.allN; ++i) {
                        unsigned mp; bool ws;
                        one[0].label = list_row_name(lastL.kind, lastL.all[i], mp, ws); one[0].icon = IC_STAR; one[0].sub = false; one[0].off = false;
                        if (mp) { sprintf(mp1, "%u", mp); one[0].extra = mp1; } else one[0].extra = 0;
                        float w1, h1; block_size(f, k, one, 1, w1, h1); if (w1 > wAll) wAll = w1;
                    }
                }
                if (wAll > lw) lw = wAll;
            }
            if (lw < bw) lw = bw;
            const float tL = ease_out(((float)(now - g_listB.openMs) / 1000.0f) / 0.07f);
            float lx = bx, ly = snap((below ? by : by + bh - lh) + (1.0f - tL) * 3.0f * S);
            if (ly + lh > f.screenH - 4.0f) ly = snap(f.screenH - 4.0f - lh); if (ly < 4.0f) ly = 4.0f;
            const float LA = g_listB.vis * tL;
            const int cur = empty ? 0 : ((lastL.cursor >= lastL.top && lastL.cursor < lastL.top + lastL.rows) ? lastL.cursor - lastL.top + 1 : 0);
            if (LA > 0.0f) {
                block_draw(f, k, g_listB, lrows, lastL.rows, cur, lastL.focused, lx, ly, lw, lh, LA, dt);
                if (lastL.n > lastL.rows) {                     // where this page sits in the whole list : a thin rail
                    const float tx = lx + lw - 3.0f * S, ty = ly + k.pad, th = lh - 2.0f * k.pad;
                    const float th2 = th * (float)lastL.rows / (float)lastL.n, ty2 = ty + th * (float)lastL.top / (float)lastL.n;
                    rrect(dev_of(f), tx - 1.0f * S, ty, 2.0f * S, th, 1.0f * S, with_alpha(0x30FFFFFFu, LA), with_alpha(0x30FFFFFFu, LA));
                    rrect(dev_of(f), tx - 1.0f * S, snap(ty2), 2.0f * S, snap(th2 < 6.0f * S ? 6.0f * S : th2), 1.0f * S, with_alpha(0xC0FFDA8Au, LA), with_alpha(0xC0FFDA8Au, LA));
                }
                if (NL.open) { g_hit[0][0] = lx / f.screenW; g_hit[0][1] = ly / f.screenH; g_hit[0][2] = lw / f.screenW; g_hit[0][3] = lh / f.screenH; }
            }
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
}

void npc_card_forget()  {   // device lost : the handles are gone with it (the list icons' too -- they reload on demand)
    g_cardIcon = 0; g_cardIconId = 0; g_cardId = 0;
    for (int i = 0; i < 64; ++i) g_icons[i] = IconSlot();
    g_elemTex = g_jobTex = g_wpnTex = 0; g_elemR = TexRetry(); g_jobR = TexRetry(); g_wpnR = TexRetry();
}
void npc_card_dispose() {
    if (g_cardIcon) release_texture(g_cardIcon);
    for (int i = 0; i < 64; ++i) if (g_icons[i].tex) release_texture(g_icons[i].tex);
    if (g_elemTex) release_texture(g_elemTex);
    if (g_jobTex) release_texture(g_jobTex);
    if (g_wpnTex) release_texture(g_wpnTex);
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
        float cy = snap(by - snap(4.0f * S) - ch);
        if (cy < 4.0f) cy = snap(by + H + snap(4.0f * S));
        const float tO = ease_out(((float)(now - g_npcCardB.openMs) / 1000.0f) / 0.08f), A = g_npcCardB.vis * tO;
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

} // namespace aio
