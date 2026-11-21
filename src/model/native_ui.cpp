// native_ui.cpp -- hide / offset the client's own windows. See native_ui.h for the two placement paths and
// docs/game-data/native-windows.md for every offset below and how each was proven (2026-11-15).
#include "model/native_ui.h"
#include "model/ui_config.h"
#include "model/ffximain_rva.h"   // FM_MENU_PTR : the menu manager sits 0x54 before the focused-menu slot
#include "model/game_mem.h"       // ffximain_base
#include "model/paths.h"          // plugin_path : the crash-recovery file
#include "model/chat_log.h"       // chat_seed_line / chat_seed_end : the client's own log, replayed at load
#include "model/item_text.h"      // item_text_detail::decode : the item card page 2
#include "model/map_dat.h"        // dat_resolve_path / dat_read_file : the client's d_msg files (Job Points' texts)
#include "windower.h"
#include "windower_debug.h"
#include <windows.h>
#include <intrin.h>        // _ReturnAddress (NUIDIAG)
#include <cstdio>
#include <cstring>

namespace aio {

using windower::u32;
using windower::valid_ptr;
using windower::safe_read;

u32 aob_masked(const char* pat);   // below : a masked, unique AOB in FFXiMain
bool jobs_read(NativeJobs& J);      // below : the Job / Master Levels
u32 menu_event_fn();                // below : the client's menu event function (found by signature) -- the trade's lines hook it

namespace {

// ---- the client's structures (FFXiMain, proven on the 2026-11-15 client) ----------------------------------------
const u32 MGR_BEFORE_MENU_SLOT = 0x54;   // manager = &focused-menu slot - 0x54
const u32 MGR_OPEN_LIST  = 0x00;         // list of OPEN menu objects   (FUN_05DBDB20 find-open-by-name)
const u32 MGR_DEF_LIST   = 0x1C;         // list of every loaded DEFINITION (FUN_05DBDF40 find-def-by-name)
const u32 NODE_NEXT = 0x00, NODE_VAL = 0x10;
const u32 DEF_LAYOUT = 0x08, DEF_TAG = 0x46, DEF_NAME = 0x4E;   // layout = *(def+8), record = *layout
const u32 REC_X = 0x02, REC_Y = 0x04, REC_ANCHOR = 0x13;        // 512x448 virtual screen, anchor 1 right / 2 bottom
const u32 OBJ_DEF = 0x04, OBJ_HANDLER = 0x0C, OBJ_RECT = 0x3A, OBJ_CLOSING = 0x6E;
const int HIDE_DY = 3000;                // a hidden window is parked this far below its place (screen px)

typedef void (__thiscall* SetRectFn)(u32 self, int x, int y, int w, int h, u32 a6, int keepKids, int a8);
typedef void (__thiscall* MoveToFn)(u32 self, int x, int y);
typedef void (__thiscall* HandlerFn)(u32 self);

// ---- guarded little readers / writers (a bad pointer degrades to "nothing", rule 5) ----
bool rd16(u32 a, short& v)  { __try { v = *(const short*)a; return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; } }
bool rd8 (u32 a, unsigned char& v) { __try { v = *(const unsigned char*)a; return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; } }
bool wr16(u32 a, short v)   { __try { *(short*)a = v; return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; } }
bool rdname(u32 def, char out[9]) {
    memset(out, 0, 9);
    __try {
        if (memcmp((const void*)(def + DEF_TAG), "menu    ", 8)) return false;   // not a menu definition
        memcpy(out, (const void*)(def + DEF_NAME), 8);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    for (int i = 7; i >= 0 && out[i] == ' '; --i) out[i] = 0;                    // "inline  " -> "inline"
    return out[0] != 0;
}
u32 layout_rec(u32 def) {
    u32 lay = 0, rec = 0;
    if (!safe_read(def + DEF_LAYOUT, &lay) || !valid_ptr(lay) || !safe_read(lay, &rec) || !valid_ptr(rec)) return 0;
    return rec;
}

// ---- which windows belong to which option ----
bool in_list(const char* n, const char* const* L) { for (; *L; ++L) if (!strcmp(n, *L)) return true; return false; }
const char* const PARTY_W[] = { "partywin", "targetwi", 0 };
const char* const ALLY_W[]  = { "raid1", "raid2", 0 };
const char* const CHAT_W[]  = { "logwindo", "logwin2", "fulllog", "spoolmsg", "splmsg2", "eventlog", "log8", "rem1line", "rem2line", "rem3line", 0 };
const char* const HELP_W[]  = { "helpwind", "titlewin", 0 };
const char* const COST_W[]  = { "subwindo", 0 };
const char* const NET_W[]   = { "netstat", "polwindo", 0 };
const char* const BUFF_W[]  = { "buff", 0 };                                             // your status icons (NU_BUFF)
const char* const MACRO_W[] = { "mcr1pall", "mcr2pall", "mcr1long", "mcr2long", 0 };   // the macro bar (NU_MACRO) : placed by code (move-to, hooked)
const char* const TRADE_W[] = { "trade", "gift", "handover", 0 };                      // the player / NPC trade (NU_TRADE) : records only
const char* const DIALOG_W[] = { "comyn", "merityn", "resyn", "resynus", "resynde", "resynfr", "shopbuy", "shopsell", 0 };   // the Yes / No confirms (NU_DIALOG)
const char* const ACT_SUB_W[] = { "magselec", "abiselec", "chatctrl", 0 };
const char* const ACT_LIST_W[] = { "magic", "ability", "mount", 0 };                   // the lists AioHUD redraws (NU_ACT)   // the action menu's submenus AioHUD redraws (NU_ACT)
const char* const NPC_W[]   = { "rem4line", "rem4li2", "query", 0 };
bool g_profDataOk = false;   // the Profile's data reads (read_profile_mem) : AioHUD may draw it, so park the client's
bool g_persDataOk = false;   // the stat panels' data reads (read_persona_mem) : same rule for `persona` / `level`
bool g_skillsOkRule = false; // the skill array reads : same rule for `btlskill` / `mgcskill` (mirrors g_skillsOk)
extern bool g_eqRule;
bool g_eqUp = false;        // AioHUD draws the Equipment screen (the item cards' rule)       // the equipment reads : `equip` born parked (defined with g_equip)
extern bool g_mcatRule;     // the merit list reads : `meritinf` born parked (defined with g_mcat)
bool g_faithRule = false;   // the faith points read : `fp_info` (Alter Ego's points) is born parked, as levmeri2
bool g_jpRule = false;       // the JP mirror reads : `levmeri2` (Job Points' CP / JP) is born parked -- its numbers are drawn
                             // where it opened, so a park after the fact left them on screen (2026-11-18)
extern NativeRegion g_rg;    // defined below (Region Info) : its panels are born parked only under the map
extern NativeTrade g_trade;  // defined below (the player trade) : its Current Gil is born parked
DWORD g_tradeBornMs = 0;     // when `trade` was born : the `money` box born with it, before trade_poll sees it
bool g_shopMainUp = false;   // shop_poll : an NPC shop's `shopmain` is up (its list and `money` are then born parked)
DWORD g_rgMapBornMs = 0;    // when cnqframe was born : its panels born in the same moment, before region_poll sees it open
const char* const MAIN_W[]  = { "menuwind", "socialme", 0 };                  // the main menu's two pages (NU_MAIN)
const char* const MAIN_SUB_W[] = { "mgcmenu", "statcom2", "ut_menu", "fe_main", "merit1", "merit2", "merit3", "mnstorag", "iuse", "cmbmenu", "cmbhwin", "abimenu", "prty1", "prty2", "prty3", "prty7", "prty4", "prty8", "prty5us", "alarm",
    "searchma", "scsibori", "searchjo", "searchna", "searchra", "searchle", "scchar", "scmlev", "comgenre", "sccompar", "sccomite", "sccomls", "scname",
    "link1", "link2", "link3", "link4", "link13", "link7", "link8", "link9", "link10", "region", 0 };   // later = opened over the earlier ones      // what it opens that AioHUD redraws (NU_MAIN) : Magic's categories, Status's list, Status -> Unity's (opened OVER statcom2 : listed after it, it wins)
const char* const MAIN_SORT_W[] = { "mgcsortw", "blusortw", "itmsort2", "abisortw", 0 };               // the spell list's sort options (NU_MAIN) ; Blue Magic's has a 3rd row
const char* const MAIN_YN_W[] = { "sortyn", 0 };                               // Auto-sort's Yes / No (parked by inv-style code, not by name)          // the NPC windows AioHUD redraws (NU_NPC)
const char* const INPUT_W[] = { "inline", "inlink", "inparty", "intell", "inshout", "insay", "msgline", "passinpu", "log1", 0 };

// the offset a window SHOULD carry under the current config
void desired_offset(const char* n, short ox, short oy, unsigned char anc, int hide, int mdx, int mdy, int& dx, int& dy) {
    dx = dy = 0;
    if ((hide & NU_PARTY) && in_list(n, PARTY_W)) { dy = HIDE_DY; return; }
    if ((hide & NU_ALLY)  && in_list(n, ALLY_W))  { dy = HIDE_DY; return; }
    if ((hide & NU_CHAT)  && in_list(n, CHAT_W))  { dy = HIDE_DY; return; }
    if ((hide & NU_HELP)  && in_list(n, HELP_W))  { dy = HIDE_DY; return; }
    if ((hide & NU_COST)  && in_list(n, COST_W))  { dy = HIDE_DY; return; }
    if ((hide & NU_NET)   && in_list(n, NET_W))   { dy = HIDE_DY; return; }
    if ((hide & NU_BUFF)  && in_list(n, BUFF_W))  { dy = HIDE_DY; return; }
    if ((hide & NU_MACRO) && in_list(n, MACRO_W)) { dy = HIDE_DY; return; }
    if ((hide & NU_TRADE) && in_list(n, TRADE_W)) { if (!strcmp(n, "trade") || !strcmp(n, "handover")) g_tradeBornMs = GetTickCount() | 1; dy = HIDE_DY; return; }
    if ((hide & NU_DIALOG) && in_list(n, DIALOG_W)) { dy = HIDE_DY; return; }   // BORN parked : AioHUD draws every confirm
    // the trade's Current Gil (`money`) draws its digits WHERE IT OPENED and does not follow a later park (2026-11-20 :
    // "on voit les chiffres de gil ... ceux du jeu") : BORN parked while a trade is up (or `trade` was born a moment ago)
    if ((hide & NU_TRADE) && !strcmp(n, "money") && (g_trade.open || (g_tradeBornMs && GetTickCount() - g_tradeBornMs < 1000))) { dy = HIDE_DY; return; }
    if ((hide & NU_ACT)   && (!strcmp(n, "playermo") || in_list(n, ACT_SUB_W) || in_list(n, ACT_LIST_W))) { dy = HIDE_DY; return; }
    // the NPC SHOP : Buy / Sell, then its list and its Current Gil born parked (the client RESIZES `shop` once it is up and
    // `money` draws where it was born : shops-re s.7, the trade's lesson) -- only under an NPC shop's menu (the bazaar and
    // the guild shops reuse `shop` : the client's)
    if ((hide & NU_NPC) && (!strcmp(n, "shopmain") || (g_shopMainUp && (!strcmp(n, "shop") || !strcmp(n, "money"))))) { dy = HIDE_DY; return; }
    if ((hide & NU_NPC)   && in_list(n, NPC_W)) { dy = HIDE_DY; return; }
    if ((hide & NU_MAIN)  && (in_list(n, MAIN_W) || in_list(n, MAIN_SUB_W) || in_list(n, MAIN_SORT_W))) { dy = HIDE_DY; return; }
    // Status -> Profile : BORN parked (its record) -- its flag, rank bar and icons are drawn where it OPENED and do not
    // follow a later move (2026-11-18, "plein d'elements sont toujours la, la barre, l'icone") ; only while its data
    // reads (g_profDataOk), else the client's own panel stays
    if ((hide & NU_MAIN)  && g_profDataOk && !strcmp(n, "profile")) { dy = HIDE_DY; return; }
    // Region Info's panels (the points, the overview, a region's detail) : AioHUD draws them -- BORN parked, else the one
    // the client opens on each hover shows for the frames before it is moved (user 2026-11-19 "ca pop le temps de se replacer")
    // Only under AioHUD's map (cnqframe up, or born just before them this frame) : one opened anywhere else stays the client's
    if ((hide & NU_MAIN) && !strcmp(n, "cnqframe")) g_rgMapBornMs = GetTickCount() | 1;
    { static const char* const RG_BORN[] = { "conq3pts", "imperial", "allied", "colopoin", "conq6sta", "beseige1", "campresu", "conquer1", "beseige2", "camparea", "colorank", 0 };
      const bool mapUp = g_rg.open || (g_rgMapBornMs && GetTickCount() - g_rgMapBornMs < 1000);
      if ((hide & NU_MAIN) && mapUp && in_list(n, RG_BORN)) { dy = HIDE_DY; return; } }
    // Status's stat panels : the same (born parked, only while their data reads)
    if ((hide & NU_MAIN)  && g_persDataOk && (!strcmp(n, "persona") || !strcmp(n, "level"))) { dy = HIDE_DY; return; }
    if ((hide & NU_MAIN)  && g_jpRule && !strcmp(n, "levmeri2")) { dy = HIDE_DY; return; }
    if ((hide & NU_MAIN)  && g_faithRule && !strcmp(n, "fp_info")) { dy = HIDE_DY; return; }
    if ((hide & NU_MAIN)  && g_mcatRule && !strcmp(n, "meritinf")) { dy = HIDE_DY; return; }
    if ((hide & NU_MAIN)  && g_eqRule && !strcmp(n, "equip")) { dy = HIDE_DY; return; }
    // while AioHUD draws the Equipment screen, the client's item cards : it re-places them at every item and every page
    // (Y / numpad -), and a park after the fact let them show for a frame each time (2026-11-18, "ca fait pop les
    // vieilles box") -- the record's offset makes the move-to hook put them away as they are placed
    if ((hide & NU_MAIN)  && g_eqUp && (!strcmp(n, "iteminfo") || !strcmp(n, "itemxinf"))) { dy = HIDE_DY; return; }
    if ((hide & NU_MAIN)  && g_skillsOkRule && (!strcmp(n, "btlskill") || !strcmp(n, "mgcskill") || !strcmp(n, "trdskill"))) { dy = HIDE_DY; return; }
    // the HUD-like windows never follow the MENUS offset -- they have their own option above
    if (in_list(n, PARTY_W) || in_list(n, ALLY_W) || in_list(n, CHAT_W) || in_list(n, COST_W) || in_list(n, NET_W) || in_list(n, INPUT_W)) return;
    if (ox == 0 && oy == 0 && anc == 0) return;   // frames the client places by CODE (mapframe, cnqframe...)
    dx = mdx; dy = mdy;
}

// ---- one entry per layout RECORD we know (a record can be shared ; keying on it never shifts one twice) ----
struct Entry { u32 rec, def; char name[9]; short ox, oy; unsigned char anc; short ax, ay; bool alive; };

// ---- the INPUT LINE docked under AioHUD's chat box (UiConfig::chDock) ----
// Records are in the client's UI units, not backbuffer px : the UI is scaled (2560x1400 backbuffer, a 929-high UI
// on 2026-11-15). The UI screen size is the pair of shorts at manager+0x80 (lo W, hi H) -- what the client's own
// re-layout reads (FUN_05D7A820). A record's place on that screen : x (+ W-512 when anchored right), y (+ H-448
// when anchored bottom). Target = the chat box's bottom-left, or its top-left minus the line's height when the line
// would leave the screen below it.
struct Dock { bool on; int tx, ty, tyAbove; int W, H; };
Dock g_dock = { false, 0, 0, 0, 0, 0 };
void dock_compute(u32 mgr) {
    const UiConfig& c = ui_config();
    g_dock.on = false;
    const float* R = c.chDockRect;
    // docked under the box : mode 1, and mode 2 whenever the client's text cannot be read (never type blind)
    const bool dockMode = c.chDock == 1 || (c.chDock == 2 && c.chInputOk == 0);
    if (!dockMode || !c.chShow[0] || R[2] <= 0.0f || R[4] <= 0.0f || R[5] <= 0.0f) return;
    u32 wh = 0; if (!safe_read(mgr + 0x80, &wh)) return;
    const int W = (short)(wh & 0xFFFF), H = (short)(wh >> 16);
    if (W < 320 || H < 240 || W > 8000 || H > 8000) return;               // not a screen size : leave the line alone
    const float sx = (float)W / R[4], sy = (float)H / R[5];               // backbuffer px -> UI units
    g_dock.on = true; g_dock.W = W; g_dock.H = H;
    g_dock.tx = (int)(R[0] * sx + 0.5f);
    g_dock.ty = (int)((R[1] + R[3]) * sy + 0.5f) + 2;
    g_dock.tyAbove = (int)(R[1] * sy + 0.5f) - 2;                         // minus the line's own height, per window
}
bool dock_offset(const Entry& e, int& dx, int& dy) {
    // mode 2 (the line drawn INSIDE the chat box) : the client's own `inline` is parked off-screen while its text reads
    const UiConfig& c = ui_config();
    if (c.chDock == 2 && c.chShow[0] && c.chInputOk != 0 && !strcmp(e.name, "inline")) { dx = 0; dy = HIDE_DY; return true; }
    if (!g_dock.on || !in_list(e.name, INPUT_W) || !strcmp(e.name, "passinpu")) return false;
    const int nx = e.ox + ((e.anc & 1) ? g_dock.W - 512 : 0), ny = e.oy + ((e.anc & 2) ? g_dock.H - 448 : 0);
    short h = 22; rd16(e.rec + 0x0C, h); if (h <= 0 || h > 200) h = 22;
    int ty = g_dock.ty; if (ty + h > g_dock.H) ty = g_dock.tyAbove - h;   // no room below : above the box
    dx = g_dock.tx - nx; dy = ty - ny;
    return true;
}
Entry g_e[768]; int g_eN = 0;
bool  g_anyApplied = false;

// ---- the ACTION menu mirror (NU_ACT) ----
// If its rows stop reading while the client's menu is parked off-screen, the player would see NO menu at all : after
// 3 bad frames the client's own menu comes back (g_actFail), and goes away again after 5 s of good reads.
NativeEntryMenu g_sub;
NativeChatInfo  g_chat;
NativeList      g_list;
NativeMenu g_act; bool g_actFail = false; int g_actBad = 0; DWORD g_actGoodSince = 0;
u32 g_actObj = 0;          // the playermo object of the last good read (re-read before any call into the client)
// the same fallback for the NPC windows : an NPC window open but unreadable 3 frames -> the client's own come back
NativeNpc g_npc; bool g_npcFail = false; int g_npcBad = 0; DWORD g_npcGoodSince = 0;
// and for the MAIN menu
NativeEntryMenu g_mainM, g_mainSub, g_mainSort, g_mainYn; bool g_mainFail = false;
NativeTopBar g_tb; bool g_tbFail = false;                                               // the top bar (titlewin + helpwind)
NativeMacro g_mc; bool g_mcFail = false;                                                // the macro bar
NativeNet g_net; bool g_nsFail = false;                                                 // the connection box (netstat)
NativeShop g_shop;                                                                        // the NPC shop (shop_poll)
const char* const SHOPMAIN_W[] = { "shopmain", 0 };
NativeTrade g_trade; bool g_trFail = false;                                             // the player trade
NativeDialog g_dlg; bool g_dlgFail = false; u32 g_dlgObj = 0;                           // the Yes / No confirms (+ the open one's window, the driver)
NativeProfile g_prof;
NativePanel g_persona, g_levelW; PersonaMem g_persData;
NativePanel g_jobW, g_mlW; NativeJobs g_jobs; bool g_jobsOurs = false;   // Status -> Job / Master Levels
NativePanel g_btlW, g_mgcW; NativeSkills g_skills; bool g_skillsOk = false;   // Status -> Combat / Magic Skills
NativePanel g_utW; NativeUnity g_unity; bool g_unityOk = false;
ItemExtra g_iix[2];                                                                   // iteminfo / itemxinf : their page 2
NativeEquip g_equip; bool g_eqRule = false;                                            // the main menu's Equipment
NativeItems g_items;                                                                     // the main menu's Items
NativeSynth g_syn;                                                                       // the main menu's Synthesis
NativeComment g_cmt;                                                                     // Search -> Edit your comment
NativeLinkshell g_ls;
NativeRegion g_rg;                                                                       // Region Info : the map + the data                                                                    // the main menu's Linkshell
NativeMeritCat g_mcat; bool g_mcatRule = false;                                        // Merit Points -> Categories
NativeCelestite g_cel;                                                                // Alter Ego -> Change Celestite
NativeFaith g_faith;                                         // Status -> Alter Ego (fp_cat / fp_cat2 / fp_info)
NativeJpCat g_jcat;
NativeJobPoints g_jpts;                                                               // Status -> Job Points (merit2ca, levmeri2)
NativeAreaList g_area; NativeSearch g_search; NativeEntryMenu g_scopt;               // Unity Lists : arealist, scresult, scoption
const char* const SCOPT_W[] = { "scoption", 0 };
NativeUnityRank g_urank;                                                              // Ranking Info -> its two lists (quest01)
NativeUnityInfo g_uinfo;                                                              // Status -> Unity -> Unity Info (quest00 mode 1)
const char* const QUEST_W[] = { "quest00", 0 };                  // Status -> Unity's points box
NativePanel g_trdW, g_lmW; NativeMerits g_merits; bool g_meritsOk = false;     // Status -> Crafts ; Merit Points' panel
NativeCurrencies g_cur;                                                          // Status -> Currencies 1 / 2
// one read of `evitem` in its Currencies modes ; false = not open, not in mode 1 / 2, or unreadable
bool cur_read(u32 mgr, u32 focus, NativeCurrencies& C) {
    memset(&C, 0, sizeof(C));
    u32 node = 0; if (!mgr || !safe_read(mgr + MGR_OPEN_LIST, &node)) return false;
    u32 obj = 0;
    for (int g = 0; valid_ptr(node) && g < 400 && !obj; ++g) {
        u32 next = 0, o = 0, def = 0; unsigned char dead = 0, cl = 0; char nm[9];
        safe_read(node + NODE_NEXT, &next); safe_read(node + NODE_VAL, &o); rd8(node + 0x14, dead);
        if (!dead && valid_ptr(o) && safe_read(o + OBJ_DEF, &def) && valid_ptr(def) && rdname(def, nm) && !strcmp(nm, "evitem") && rd8(o + OBJ_CLOSING, cl) && !cl) obj = o;
        node = next;
    }
    if (!obj) return false;
    u32 hd = 0, rows = 0; unsigned char mode = 0, loaded = 0; int n = 0, top = 0, cur = 0, vis = 0;
    if (!safe_read(obj + OBJ_HANDLER, &hd) || !valid_ptr(hd) || !rd8(hd + 0x88, mode) || (mode != 1 && mode != 2) || !rd8(hd + 0x8A, loaded)
        || !safe_read(hd + 0x40, (u32*)&n) || !safe_read(hd + 0x38, (u32*)&top) || !safe_read(hd + 0x30, (u32*)&cur) || !safe_read(hd + 0x48, (u32*)&vis)
        || !safe_read(hd + 0x20, &rows)) return false;
    const int cap = mode == 1 ? 99 : 97;
    if (n < 0 || n > cap || vis < 1 || vis > 8 || top < 0 || (n > 0 && (!valid_ptr(rows) || top >= n))) return false;
    C.mode = mode; C.loaded = loaded != 0 && n > 0; C.n = n; C.top = top; C.cursor = cur; C.vis = vis;
    for (int i = 0; i < vis && top + i < n; ++i) { u32 v = 0; if (!safe_read(rows + (u32)(top + i) * 8, &v)) return false; C.value[i] = (int)v; }
    for (int r = 0; r < n; ++r) { u32 v = 0; if (!safe_read(rows + (u32)r * 8, &v)) return false; C.all[r] = (int)v; }
    if (!rd16(obj + OBJ_RECT, C.rx0) || !rd16(obj + OBJ_RECT + 2, C.ry0) || !rd16(obj + OBJ_RECT + 4, C.rx1) || !rd16(obj + OBJ_RECT + 6, C.ry1)) return false;
    u32 wh = 0; if (!safe_read(mgr + 0x80, &wh)) return false;
    C.uiW = (short)(wh & 0xFFFF); C.uiH = (short)(wh >> 16);
    if (C.uiW < 320 || C.uiH < 240 || C.uiW > 8000 || C.uiH > 8000) return false;
    if (C.ry0 >= C.uiH) { C.ry0 = (short)(C.ry0 - HIDE_DY); C.ry1 = (short)(C.ry1 - HIDE_DY); }
    C.focused = focus == obj;
    C.open = true;
    return true;
}
int entry_read(const char* const* W, NativeEntryMenu& m, u32* objOut);   // below
// Unity Info's addresses, from the client's own code (docs/notes/unity-info-re-2026-11-18.md section 7), cross-checked :
// R1 the 0x116 re-request (have-data[2] at +0x02 == +0x1E, loaded[2] at +0x25 == have + 2), R3 the mode-1 draw (list 0
// at +0x06 == have + 4, list 1 at +0x16 == list 0 + 0x1000), P the evaluations' getter (the stats-refresh flags at +0x02,
// 0x061 + 0x5C at +0x0E == FM_PW_BLOCK + 0x4C). Retried every 5 s until it holds, logged both ways.
struct UnityRes { u32 loaded, list0, list1, gate, eval; };
UnityRes g_ur; bool g_urOk = false; int g_urTries = 0; DWORD g_urNext = 0;
bool unity_res() {
    if (g_urOk) return true;
    const DWORD now = GetTickCount();
    if (g_urTries && (int)(now - g_urNext) < 0) return false;
    ++g_urTries; g_urNext = now + 5000;
    const u32 r1 = aob_masked("8A 8E ?? ?? ?? ?? 84 C9 74 ?? 84 C0 0F 95 C0 50 E8 ?? ?? ?? ?? 83 C4 04 84 C0 74 ?? C6 86 ?? ?? ?? ?? 00 C6 86 ?? ?? ?? ?? 00 B0 01");
    const u32 r3 = aob_masked("68 ?? ?? ?? ?? 68 ?? ?? ?? ?? 53 E8 ?? ?? ?? ?? 68 ?? ?? ?? ?? 68 ?? ?? ?? ?? 53 A2 ?? ?? ?? ?? E8 ?? ?? ?? ?? A2 ?? ?? ?? ?? 83 C4 18 C6 46 6C 00");
    const u32 p = aob_masked("8A 0D ?? ?? ?? ?? 33 C0 F6 C1 01 74 05 A1 ?? ?? ?? ?? C3 90 90 90 90 90 90 90 90 90 90 90 90 90 E8 DB FF FF FF 25 FF FF 00 00 C3 90 90 90 90 90 E8 CB FF FF FF C1 E8 10 C3");
    u32 h1 = 0, h2 = 0, ld = 0, l0 = 0, l1 = 0, gate = 0, ev = 0;
    const u32 blk = fm_addr(FM_PW_BLOCK);
    if (r1 && r3 && p && blk && safe_read(r1 + 0x02, &h1) && safe_read(r1 + 0x1E, &h2) && safe_read(r1 + 0x25, &ld)
        && safe_read(r3 + 0x06, &l0) && safe_read(r3 + 0x16, &l1) && safe_read(p + 0x02, &gate) && safe_read(p + 0x0E, &ev)
        && h1 == h2 && ld == h1 + 2 && l0 == h1 + 4 && l1 == l0 + 0x1000 && ev == blk + 0x4C && valid_ptr(l0) && valid_ptr(gate)) {
        g_ur.loaded = ld; g_ur.list0 = l0; g_ur.list1 = l1; g_ur.gate = gate; g_ur.eval = ev; g_urOk = true;
        windower::debug::log("NATIVEUI unity info resolved (try %d) : loaded FFXiMain+%X, lists +%X / +%X, gate +%X, evaluations +%X", g_urTries,
                             ld - ffximain_base(), l0 - ffximain_base(), l1 - ffximain_base(), gate - ffximain_base(), ev - ffximain_base());
    } else if (g_urTries == 1 || g_urTries % 12 == 0)
        windower::debug::log("NATIVEUI unity info NOT resolved (try %d : R1 %d R3 %d P %d block %d)", g_urTries, !!r1, !!r3, !!p, !!blk);
    return g_urOk;
}
// a list's score of one Unity : T[2][id] / T[1][id] ; false on a bad read
bool unity_score(u32 list, int id, int& score) {
    u32 t1 = 0, t2 = 0;
    if (!safe_read(list + (u32)(0x20 + id) * 4, &t1) || !safe_read(list + (u32)(0x40 + id) * 4, &t2)) return false;
    score = t1 ? (int)(t2 / t1) : 0;
    return true;
}
bool uinfo_read(u32 mgr, u32 focus, NativeUnityInfo& U) {
    memset(&U, 0, sizeof(U));
    if (!unity_res()) return false;
    u32 node = 0; if (!mgr || !safe_read(mgr + MGR_OPEN_LIST, &node)) return false;
    u32 obj = 0; bool utMenu = false;
    for (int g = 0; valid_ptr(node) && g < 400; ++g) {
        u32 next = 0, o = 0, def = 0; unsigned char dead = 0, cl = 0; char nm[9];
        safe_read(node + NODE_NEXT, &next); safe_read(node + NODE_VAL, &o); rd8(node + 0x14, dead);
        if (!dead && valid_ptr(o) && safe_read(o + OBJ_DEF, &def) && valid_ptr(def) && rdname(def, nm) && rd8(o + OBJ_CLOSING, cl) && !cl) {
            if (!strcmp(nm, "quest00")) obj = o;
            if (!strcmp(nm, "ut_menu")) utMenu = true;
        }
        node = next;
    }
    if (!obj || !utMenu) return false;                                  // quest00 is Records of Eminence's too
    u32 hd = 0; unsigned char mode = 0, chat = 0;
    if (!safe_read(obj + OBJ_HANDLER, &hd) || !valid_ptr(hd) || !rd8(hd + 0x68, mode) || (mode != 1 && mode != 2) || !rd8(hd + 0x6E, chat)) return false;
    U.mode = mode;
    if (!rd16(obj + OBJ_RECT, U.rx0) || !rd16(obj + OBJ_RECT + 2, U.ry0) || !rd16(obj + OBJ_RECT + 4, U.rx1) || !rd16(obj + OBJ_RECT + 6, U.ry1)) return false;
    u32 wh = 0; if (!safe_read(mgr + 0x80, &wh)) return false;
    U.uiW = (short)(wh & 0xFFFF); U.uiH = (short)(wh >> 16);
    if (U.uiW < 320 || U.uiH < 240 || U.uiW > 8000 || U.uiH > 8000) return false;
    if (U.ry0 >= U.uiH) { U.ry0 = (short)(U.ry0 - HIDE_DY); U.ry1 = (short)(U.ry1 - HIDE_DY); }
    // the chat setting's rows (entries 1 / 2 only : 3 / 4 are hidden, 5 is not the setting's)
    NativeEntryMenu m; if (entry_read(QUEST_W, m, 0) != 1) return false;
    for (int i = 0; i < m.n; ++i) if (m.entry[i] == 1 || m.entry[i] == 2) {
        const int k = U.rows.n++; U.rows.entry[k] = m.entry[i]; U.rows.state[k] = m.state[i];
    }
    if (U.rows.n != 2) return false;
    U.rows.cursor = m.cursor; U.rows.open = true;
    U.chat = chat ? 1 : 0;
    // the data
    unsigned char gate = 0, ld0 = 0, ld1 = 0; u32 w = 0, ev = 0;
    const u32 blk = fm_addr(FM_PW_BLOCK);
    if (!blk || !rd8(g_ur.gate, gate) || !rd8(g_ur.loaded, ld0) || !rd8(g_ur.loaded + 1, ld1) || !safe_read(blk + 0x48, &w) || !safe_read(g_ur.eval, &ev)) return false;
    U.unity = (int)(w & 0x1F); if (U.unity > 11) U.unity = 0;
    U.loading = !(gate & 1) || !ld0 || !ld1;
    U.personal = (int)(ev >> 16); U.partial = (int)(ev & 0xFFFF);
    for (int k = 0; k < 2 && !U.loading; ++k) {
        const u32 list = k ? g_ur.list1 : g_ur.list0;
        int sc[12] = { 0 }; bool any = false;
        for (int id = 1; id <= 11; ++id) { if (!unity_score(list, id, sc[id])) return false; if (sc[id] > 0) any = true; }
        if (!any || !U.unity) continue;                                 // rank 0 : "--"
        int r = 1; for (int id = 1; id <= 11; ++id) if (sc[id] > sc[U.unity]) ++r;
        U.rank[k] = r; U.pts[k] = sc[U.unity] > 999999 ? 999999 : sc[U.unity];
    }
    U.focused = focus == obj;
    U.open = true;
    return true;
}
// Ranking Info's list : quest01 open while quest00 (Ranking Info, mode 2) and ut_menu are. Which list : quest01's mode
// (5 / 6) when it reads so, else quest00's cursor (its entry 1 / 2 opened it).
bool urank_read(u32 mgr, u32 focus, const NativeUnityInfo& Q, NativeUnityRank& K) {
    memset(&K, 0, sizeof(K)); K.cursor = -1;
    if (!Q.open || Q.mode != 2 || !unity_res()) return false;
    u32 node = 0; if (!mgr || !safe_read(mgr + MGR_OPEN_LIST, &node)) return false;
    u32 obj = 0;
    for (int g = 0; valid_ptr(node) && g < 400 && !obj; ++g) {
        u32 next = 0, o = 0, def = 0; unsigned char dead = 0, cl = 0; char nm[9];
        safe_read(node + NODE_NEXT, &next); safe_read(node + NODE_VAL, &o); rd8(node + 0x14, dead);
        if (!dead && valid_ptr(o) && safe_read(o + OBJ_DEF, &def) && valid_ptr(def) && rdname(def, nm) && !strcmp(nm, "quest01") && rd8(o + OBJ_CLOSING, cl) && !cl) obj = o;
        node = next;
    }
    if (!obj) return false;
    if (!rd16(obj + OBJ_RECT, K.rx0) || !rd16(obj + OBJ_RECT + 2, K.ry0) || !rd16(obj + OBJ_RECT + 4, K.rx1) || !rd16(obj + OBJ_RECT + 6, K.ry1)) return false;
    u32 wh = 0; if (!safe_read(mgr + 0x80, &wh)) return false;
    K.uiW = (short)(wh & 0xFFFF); K.uiH = (short)(wh >> 16);
    if (K.uiW < 320 || K.uiH < 240 || K.uiW > 8000 || K.uiH > 8000) return false;
    if (K.ry0 >= K.uiH) { K.ry0 = (short)(K.ry0 - HIDE_DY); K.ry1 = (short)(K.ry1 - HIDE_DY); }
    u32 hd = 0; unsigned char mode = 0; int cur = -1, cnt = -1;
    const bool hOk = safe_read(obj + OBJ_HANDLER, &hd) && valid_ptr(hd) && rd8(hd + 0x68, mode)
                  && safe_read(hd + 0x30, (u32*)&cur) && safe_read(hd + 0x40, (u32*)&cnt);
    K.list = (hOk && (mode == 5 || mode == 6)) ? mode - 5 : (Q.rows.cursor == 2 ? 1 : 0);
    if (hOk && cnt >= 1 && cnt <= 11 && cur >= 0 && cur < cnt) K.cursor = cur;
    {   static int said = 0;                                // what the list class holds, measured (bounded)
        if (said < 3) { ++said; windower::debug::log("NATIVEUI unity ranking : quest01 handler %s (mode %d, cursor %d, count %d, quest00 cursor %d) -> list %d",
                                                     hOk ? "read" : "UNREADABLE", (int)mode, cur, cnt, Q.rows.cursor, K.list); } }
    K.loading = Q.loading;
    if (!K.loading) {
        const u32 list = K.list ? g_ur.list1 : g_ur.list0;
        int sc[12] = { 0 };
        for (int id = 1; id <= 11; ++id) if (!unity_score(list, id, sc[id])) return false;
        int ord[11]; for (int i = 0; i < 11; ++i) ord[i] = i + 1;
        for (int i = 1; i < 11; ++i) {                      // by score, high first ; then by id
            const int v = ord[i]; int j = i;
            while (j > 0 && (sc[ord[j - 1]] < sc[v] || (sc[ord[j - 1]] == sc[v] && ord[j - 1] > v))) { ord[j] = ord[j - 1]; --j; }
            ord[j] = v;
        }
        for (int i = 0; i < 11; ++i) {
            K.id[i] = ord[i]; K.score[i] = sc[ord[i]] > 999999 ? 999999 : sc[ord[i]];
            K.rank[i] = (i > 0 && sc[ord[i]] == sc[ord[i - 1]]) ? K.rank[i - 1] : i + 1;   // ties share a rank
        }
        K.n = 11;
    }
    K.focused = focus == obj;
    K.open = true;
    return true;
}
// one guarded block copy
bool rdblk(u32 a, void* out, unsigned n) { __try { memcpy(out, (const void*)a, n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; } }
// a TEXT LIST window by name : its window object and handler (handler +0x08 == the window : the class check), its page
struct TextList { u32 obj, h; int vis, top, count, cursor; short rx0, ry0, rx1, ry1; int uiW, uiH; };
bool tl_find(u32 mgr, const char* want, TextList& T) {
    memset(&T, 0, sizeof(T)); T.cursor = -1;
    u32 node = 0; if (!mgr || !safe_read(mgr + MGR_OPEN_LIST, &node)) return false;
    for (int g = 0; valid_ptr(node) && g < 400 && !T.obj; ++g) {
        u32 next = 0, o = 0, def = 0; unsigned char dead = 0, cl = 0; char nm[9];
        safe_read(node + NODE_NEXT, &next); safe_read(node + NODE_VAL, &o); rd8(node + 0x14, dead);
        if (!dead && valid_ptr(o) && safe_read(o + OBJ_DEF, &def) && valid_ptr(def) && rdname(def, nm) && !strcmp(nm, want) && rd8(o + OBJ_CLOSING, cl) && !cl) T.obj = o;
        node = next;
    }
    if (!T.obj) return false;
    u32 back = 0; short vis = 0, top = 0, cnt = 0, ent = 0;
    if (!safe_read(T.obj + OBJ_HANDLER, &T.h) || !valid_ptr(T.h) || !safe_read(T.h + 0x08, &back) || back != T.obj
        || !rd16(T.h + 0x18, vis) || !rd16(T.h + 0x1E, top) || !rd16(T.h + 0x20, cnt) || !rd16(T.obj + 0x4C, ent)) return false;
    if (vis < 1 || vis > 16 || cnt < 0 || cnt > 700 || top < 0 || (cnt > 0 && top >= cnt)) return false;
    T.vis = vis; T.top = top; T.count = cnt;
    if (ent >= 1 && ent <= vis && top + ent - 1 < cnt) T.cursor = top + ent - 1;
    if (!rd16(T.obj + OBJ_RECT, T.rx0) || !rd16(T.obj + OBJ_RECT + 2, T.ry0) || !rd16(T.obj + OBJ_RECT + 4, T.rx1) || !rd16(T.obj + OBJ_RECT + 6, T.ry1)) return false;
    u32 wh = 0; if (!safe_read(mgr + 0x80, &wh)) return false;
    T.uiW = (short)(wh & 0xFFFF); T.uiH = (short)(wh >> 16);
    if (T.uiW < 320 || T.uiH < 240 || T.uiW > 8000 || T.uiH > 8000) return false;
    if (T.ry0 >= T.uiH) { T.ry0 = (short)(T.ry0 - HIDE_DY); T.ry1 = (short)(T.ry1 - HIDE_DY); }
    return true;
}
bool area_read(u32 mgr, u32 focus, NativeAreaList& A) {
    memset(&A, 0, sizeof(A));
    TextList T; if (!tl_find(mgr, "arealist", T)) return false;
    unsigned char mode = 0; if (!rd8(T.h + 0x6C, mode)) return false;
    if (mode == 4 && T.count != 12) return false;                       // the Unity rows
    if (mode != 4 && mode != 0 && mode != 3) return false;             // 1 / 2 : an NPC's zone picker, not ours
    A.mode = mode; A.n = T.count; A.top = T.top; A.vis = T.vis; A.cursor = T.cursor;
    if (mode != 4) {
        u32 rows = 0, vals = 0;
        if (!safe_read(T.h + 0x38, &rows) || (T.count > 0 && !valid_ptr(rows)) || !safe_read(T.h + 0x64, &vals)) return false;
        static int saidAr = 0;
        A.arTop = A.top; if (A.arTop > A.n - 12) A.arTop = A.n - 12; if (A.arTop < 0) A.arTop = 0;
        for (int i = 0; i < 12 && A.arTop + i < A.n; ++i) {
            unsigned char b[0x54]; if (!rdblk(rows + (u32)(A.arTop + i) * 0x54, b, sizeof(b))) return false;
            int v = 0x7FFFFFFF; if (valid_ptr(vals)) safe_read(vals + (u32)(A.arTop + i) * 8, (u32*)&v);
            A.arVal[i] = v; A.arCount[i] = -1; A.arLabel[i][0] = 0;
            for (int c = 0; c < 8; ++c) {
                u32 cv = 0; memcpy(&cv, b + 0x28 + 4 * c, 4);
                if (!b[c]) continue;
                if (!A.arLabel[i][0] && valid_ptr(cv)) {               // a text cell : printable ASCII, 2 chars at least
                    char t[32]; if (rdblk(cv, t, sizeof(t))) { int k = 0; for (; k < 31 && t[k] >= 0x20 && t[k] < 0x7F; ++k) A.arLabel[i][k] = t[k];
                        if (k >= 2 && (k == 31 || !t[k])) A.arLabel[i][k] = 0; else A.arLabel[i][0] = 0; }
                } else if (cv < 1000 && A.arCount[i] < 0 && c > 0) A.arCount[i] = (int)cv;
            }
            if (saidAr < 14) { ++saidAr; windower::debug::log("NATIVEUI search : arealist mode %d row %d : value %d, cells %02X %02X %02X %02X = %08X %08X %08X %08X, label '%s', count %d",
                mode, A.arTop + i, v, b[0], b[1], b[2], b[3], *(u32*)(b + 0x28), *(u32*)(b + 0x2C), *(u32*)(b + 0x30), *(u32*)(b + 0x34), A.arLabel[i], A.arCount[i]); }
            ++A.arRows;
        }
    }
    A.rx0 = T.rx0; A.ry0 = T.ry0; A.rx1 = T.rx1; A.ry1 = T.ry1; A.uiW = T.uiW; A.uiH = T.uiH;
    A.focused = focus == T.obj; A.open = true;
    return true;
}
bool search_read(u32 mgr, u32 focus, NativeSearch& S, const char* win = "scresult", unsigned lsId = 0) {
    memset(&S, 0, sizeof(S)); S.cursor = -1;
    TextList T; if (!tl_find(mgr, win, T)) return false;
    S.n = T.count; S.top = T.top; S.vis = T.vis > 16 ? 16 : T.vis; S.cursor = T.cursor;
    S.rx0 = T.rx0; S.ry0 = T.ry0; S.rx1 = T.rx1; S.ry1 = T.ry1; S.uiW = T.uiW; S.uiH = T.uiH;
    u32 rows = 0; if (!safe_read(T.h + 0x38, &rows)) return false;
    if (S.n > 0 && !valid_ptr(rows)) return false;
    if (S.n == 0) {                                                   // the client's own words for an empty list
        u32 tp = 0;
        char t[64];
        if (safe_read(T.h + 0x3C, &tp) && valid_ptr(tp) && rdblk(tp, t, sizeof(t))) {   // printable ASCII only, up to the NUL
            int k = 0; for (; k < 63 && t[k]; ++k) S.empty[k] = ((unsigned char)t[k] >= 0x20 && (unsigned char)t[k] < 0x7F) ? t[k] : ' ';
            S.empty[k] = 0;
        }
    }
    for (int i = 0; i < S.vis && S.top + i < S.n; ++i) {
        u32 rec = 0; if (!safe_read(rows + (u32)(S.top + i) * 0x54 + 0x48, &rec) || !valid_ptr(rec)) return false;
        unsigned char b[0x4C]; if (!rdblk(rec, b, sizeof(b))) return false;
        NativeSearchRow& r = S.row[i];
        r.type = b[0];
        if (r.type == 1) {                                            // the header : " [Searching]" or " [N]"
            char t[24]; int k = 0; for (; k < 23 && b[4 + k]; ++k) t[k] = (char)b[4 + k]; t[k] = 0;
            const char* o = strchr(t, '[');
            if (o && o[1] >= '0' && o[1] <= '9') { S.counted = true; S.total = atoi(o + 1); }
        } else if (r.type == 0) {
            const unsigned char* e = b + 4;
            int k = 0; for (; k < 15 && e[4 + k]; ++k) r.name[k] = ((e[4 + k] >= 0x20 && e[4 + k] < 0x7F) ? (char)e[4 + k] : '?'); r.name[k] = 0;
            u32 mask = 0; memcpy(&mask, e, 4); memcpy(&r.flags, e + 0x30, 4);
            if (lsId) for (int q = 0; q < 3; ++q) { u32 id = 0; memcpy(&id, b + 0x40 + 4 * q, 4); if (id == lsId) { r.lsRank = b[0x3C + q]; break; } }
            r.mjob = e[0x20]; r.sjob = e[0x21]; r.mlvl = e[0x22]; r.slvl = e[0x23]; r.ml = e[0x24]; r.mlShown = (e[0x25] & 1) != 0;
            unsigned short z = 0; memcpy(&z, e + 0x28, 2); r.zone = z;
            r.race = e[0x1D] & 0x0F;
            r.hasJob = (mask & 0x40) && !(r.flags & 0x4000);
        }
    }
    S.focused = focus == T.obj; S.open = true;
    return true;
}
bool panel_read(u32 mgr, const char* want, NativePanel& P);   // below
bool inStatusNow();              // below : Status's list (statcom2) is open
bool jpts_read(u32 mgr, u32 focus, NativeJobPoints& P) {
    memset(&P, 0, sizeof(P)); P.cursor = -1;
    TextList T; if (!tl_find(mgr, "merit2ca", T) || T.count < 1 || T.count > 32) return false;
    P.n = T.count; P.top = T.top; P.vis = T.vis; P.cursor = T.cursor;
    P.rx0 = T.rx0; P.ry0 = T.ry0; P.rx1 = T.rx1; P.ry1 = T.ry1; P.uiW = T.uiW; P.uiH = T.uiH;
    u32 rows = 0, ch = 0; if (!safe_read(T.h + 0x38, &rows) || !valid_ptr(rows) || !safe_read(T.h + 0x54, &ch)) return false;
    P.chosen = (int)(ch & 0xFF);
    for (int r = 0; r < P.n; ++r) { u32 id = 0; if (!safe_read(rows + (u32)r * 0x54 + 0x48, &id) || id < 1 || id > 23) return false; P.job[r] = (int)id; }
    // the JP mirror
    const u32 mer = fm_addr(FM_PW_MERIT); unsigned char fl = 0;
    if (mer && rd8(mer - 2, fl) && (fl & 4)) {
        unsigned char b[0x94];
        if (rdblk(mer + 0x0A, b, sizeof(b))) {
            P.ok = true;
            for (int j = 0; j < 24; ++j) {
                unsigned short cp = 0, jp = 0, sp = 0;
                memcpy(&cp, b + 4 + 6 * j, 2); memcpy(&jp, b + 6 + 6 * j, 2); memcpy(&sp, b + 8 + 6 * j, 2);
                if (cp > 30000 || jp > 500) { P.ok = false; break; }            // implausible : not the mirror
                P.capPts[j] = cp; P.jobPts[j] = jp; P.jpSpent[j] = sp;
            }
        }
    }
    NativeJobs J; if (jobs_read(J)) for (int j = 1; j <= 22; ++j) P.lvl[j] = J.lvl[j];
    panel_read(mgr, "levmeri2", P.box);
    P.focused = focus == T.obj; P.open = true;
    return true;
}
bool jcat_read(u32 mgr, u32 focus, NativeJpCat& K) {
    memset(&K, 0, sizeof(K)); K.cursor = -1;
    TextList T; if (!tl_find(mgr, "jbpcat", T)) return false;
    unsigned char job = 0, tab = 0;
    if (!rd8(T.h + 0x54, job) || !rd8(T.h + 0x58, tab) || job < 1 || job > 22 || tab > 1) return false;
    K.job = job; K.tab = tab; K.n = T.count; K.top = T.top; K.vis = T.vis > 16 ? 16 : T.vis; K.cursor = T.cursor;
    K.rx0 = T.rx0; K.ry0 = T.ry0; K.rx1 = T.rx1; K.ry1 = T.ry1; K.uiW = T.uiW; K.uiH = T.uiH;
    u32 rows = 0; if (!safe_read(T.h + 0x38, &rows) || (K.n > 0 && !valid_ptr(rows))) return false;
    for (int i = 0; i < K.vis && K.top + i < K.n; ++i) {
        unsigned char b[0x54]; if (!rdblk(rows + (u32)(K.top + i) * 0x54, b, sizeof(b))) return false;
        NativeJpRow& r = K.row[i]; r.id = r.gift = -1;
        for (int c = 0; c < 8; ++c) {
            u32 v = 0, col = 0; memcpy(&v, b + 0x28 + 4 * c, 4); memcpy(&col, b + 8 + 4 * c, 4);
            switch (b[c]) {
            case 0x35: r.id = (int)v; r.colName = col; break;
            case 0x36: r.lvl = (int)v - 1; r.colLvl = col; break;
            case 0x37: r.max = (int)v - 1; break;
            case 0x38: r.cost = v ? (int)v - 1 : -1; r.colCost = col; break;     // 0 : not drawn (maxed)
            case 0x39: r.gift = (int)v - 1; r.colName = col; break;
            case 0x3A: r.thr = (int)v; r.colThr = col; break;
            default: break;
            }
        }
    }
    K.focused = focus == T.obj; K.open = true;
    return true;
}
// ---- the client's d_msg files (the format : header fields at 0x18 = header size, toc, entry size, data size, count ;
// u16 at 0x0A = the body XOR 0xFF ; an entry = { u32 field count ; { u32 offset, u32 type }... }, a type-0 field =
// its text at entry + offset + 0x1C) ----
struct Dmsg { unsigned id; unsigned char* buf; unsigned size, hs, toc, esz, n; int tries; DWORD next; bool ok; };
Dmsg g_dm[6] = { { 55694, 0, 0, 0, 0, 0, 0, 0, 0, false }, { 55674, 0, 0, 0, 0, 0, 0, 0, 0, false }, { 55683, 0, 0, 0, 0, 0, 0, 0, 0, false }, { 55686, 0, 0, 0, 0, 0, 0, 0, 0, false },
                 { 55701, 0, 0, 0, 0, 0, 0, 0, 0, false }, { 55733, 0, 0, 0, 0, 0, 0, 0, 0, false } };   // + the abilities' names / descriptions (by the client's ability id)
bool dmsg_ready(Dmsg& D) {
    if (D.ok) return true;
    const DWORD now = GetTickCount();
    if (D.tries >= 8 || (D.tries && (int)(now - D.next) < 0)) return false;
    ++D.tries; D.next = now + 5000;
    char path[MAX_PATH + 8];
    const char* last = D.tries >= 8 ? " -- giving up : its texts stay blank this session" : "";
    if (!dat_resolve_path(D.id, path, sizeof(path))) { windower::debug::log("NATIVEUI d_msg %u : no path (try %d/8)%s", D.id, D.tries, last); return false; }
    unsigned sz = 0; unsigned char* b = dat_read_file(path, sz);
    if (!b || sz < 0x40) { if (b) dat_free_file(b); windower::debug::log("NATIVEUI d_msg %u : unreadable '%s' (try %d/8)%s", D.id, path, D.tries, last); return false; }
    unsigned hs = 0, toc = 0, esz = 0, n = 0; unsigned short enc = 0;
    memcpy(&hs, b + 0x18, 4); memcpy(&toc, b + 0x1C, 4); memcpy(&esz, b + 0x20, 4); memcpy(&n, b + 0x28, 4); memcpy(&enc, b + 0x0A, 2);
    if (hs < 0x40 || hs >= sz || n == 0 || n > 200000 || (toc && toc > sz - hs) || (!toc && (!esz || (unsigned long long)esz * n > sz - hs))) {
        dat_free_file(b); windower::debug::log("NATIVEUI d_msg %u : not a d_msg file (header %u, toc %u, count %u)", D.id, hs, toc, n); return false; }
    if (enc) for (unsigned i = hs; i < sz; ++i) b[i] ^= 0xFF;
    D.buf = b; D.size = sz; D.hs = hs; D.toc = toc; D.esz = esz; D.n = n; D.ok = true;
    windower::debug::log("NATIVEUI d_msg %u : %u strings from '%s' (try %d)", D.id, n, path, D.tries);
    return true;
}
const char* dmsg_str(Dmsg& D, unsigned i) {
    if (!dmsg_ready(D) || i >= D.n) return 0;
    const unsigned char* body = D.buf + D.hs; const unsigned blen = D.size - D.hs;
    unsigned base = 0, len = 0;
    if (D.toc) { if ((unsigned long long)i * 8 + 8 > blen) return 0; unsigned off = 0; memcpy(&off, body + i * 8, 4); memcpy(&len, body + i * 8 + 4, 4); base = D.toc + off; }
    else { base = i * D.esz; len = D.esz; }
    if (base >= blen || len < 12 || len > blen - base) return 0;
    const unsigned char* e = body + base;
    unsigned cnt = 0, fo = 0, ft = 0; memcpy(&cnt, e, 4); memcpy(&fo, e + 4, 4); memcpy(&ft, e + 8, 4);
    if (!cnt || ft != 0 || fo + 0x1C >= len) return 0;
    const char* s = (const char*)e + fo + 0x1C;
    for (unsigned k = fo + 0x1C; k < len; ++k) if (!e[k]) return s;          // terminated inside its entry
    return 0;
}
// the faith points : the client's accessor F (&P at +0x01 == +0x12, the points' offset at +0x0B) and its gate G (&P at +0x01,
// the flag byte's offset at +0x0E) -- docs/notes/status-panels-b-re-2026-11-18.md C4. Retried every 5 s, logged.
struct FaithRes { u32 pVar, ptsOff, flagOff; };
FaithRes g_fr; bool g_frOk = false; int g_frTries = 0; DWORD g_frNext = 0;
bool faith_res() {
    if (g_frOk) return true;
    const DWORD now = GetTickCount();
    if (g_frTries && (int)(now - g_frNext) < 0) return false;
    ++g_frTries; g_frNext = now + 5000;
    const u32 f = aob_masked("A1 ?? ?? ?? ?? 85 C0 75 01 C3 05 ?? ?? ?? ?? C3 8B 0D ?? ?? ?? ?? 85 C9 75 06 66 33 C0 C2 04 00 8B 44 24 04 66 3D 20 00 72 06 66 33 C0 C2 04 00 25 FF FF 00 00 66 8B 84 41 ?? ?? ?? ?? C2 04 00");
    const u32 g = aob_masked("A1 ?? ?? ?? ?? 85 C0 75 03 32 C0 C3 8A 80 ?? ?? ?? ?? C0 E8 04 24 01");
    u32 p1 = 0, p2 = 0, p3 = 0, pts = 0, fl = 0;
    if (f && g && safe_read(f + 0x01, &p1) && safe_read(f + 0x12, &p2) && safe_read(f + 0x0B, &pts) && safe_read(g + 0x01, &p3) && safe_read(g + 0x0E, &fl)
        && p1 == p2 && p1 == p3 && valid_ptr(p1) && pts < 0x100000 && fl < 0x100000) {
        g_fr.pVar = p1; g_fr.ptsOff = pts; g_fr.flagOff = fl; g_frOk = true;
        windower::debug::log("NATIVEUI faith resolved (try %d) : *(FFXiMain+%X), points +0x%X, loaded flag +0x%X", g_frTries, p1 - ffximain_base(), pts, fl);
    } else if (g_frTries == 1 || g_frTries % 12 == 0)
        windower::debug::log("NATIVEUI faith NOT resolved (try %d : F %d G %d)", g_frTries, !!f, !!g);
    return g_frOk;
}
// The equipped linkshells : the client's own reader of LS1 / LS2 (LS_SLOT, FUN_05429870 -- linkshell-re-2026-11-19.md
// s.10) holds the three P offsets : +0x18 the slots' item index (0x9808), +0x2B their container (0x980F), +0x41 / +0x49
// the item array (0x9868 ; stride 0x2C, 0x51 per bag in its fixed bytes) ; +0x02 &P. Taken from the code, never hard-
// coded : a client patch that moves them is followed, one that breaks the pattern says so. Retried every 5 s, logged.
struct LsRes { u32 pVar, idx, bag, items; };
LsRes g_lsr; bool g_lsrOk = false; int g_lsrTries = 0; DWORD g_lsrNext = 0;
bool ls_res() {
    if (g_lsrOk) return true;
    const DWORD now = GetTickCount();
    if (g_lsrTries && (int)(now - g_lsrNext) < 0) return false;
    ++g_lsrTries; g_lsrNext = now + 5000;
    const u32 a = aob_masked("8B 15 ?? ?? ?? ?? 56 85 D2 74 44 8B 74 24 14 81 E6 FF 00 00 00 8A 8C 16 ?? ?? 00 00 84 C9 74 2F 33 C0 81 E1 FF 00 00 00 8A 84 16 ?? ?? 00 00 8D 04 C0 8D 04 C0 03 C1 8D 0C 80 8D 04 48 66 83 BC 82 ?? ?? 00 00 00 8D 84 82 ?? ?? 00 00 75 04");
    u32 p = 0, ix = 0, bg = 0, it = 0, it2 = 0;
    if (a && safe_read(a + 0x02, &p) && safe_read(a + 0x18, &ix) && safe_read(a + 0x2B, &bg) && safe_read(a + 0x41, &it) && safe_read(a + 0x49, &it2)
        && valid_ptr(p) && it == it2 && bg == ix + 7 && it > bg && it < 0x100000) {
        g_lsr.pVar = p; g_lsr.idx = ix; g_lsr.bag = bg; g_lsr.items = it; g_lsrOk = true;
        windower::debug::log("NATIVEUI linkshell slots resolved (try %d) : *(FFXiMain+%X), index +0x%X, container +0x%X, items +0x%X%s", g_lsrTries,
            p - ffximain_base(), ix, bg, it, (ix == 0x9808 && it == 0x9868) ? "" : " -- MOVED since 2026-11-19 (was 0x9808 / 0x980F / 0x9868)");
    } else if (g_lsrTries == 1 || g_lsrTries % 12 == 0)
        windower::debug::log("NATIVEUI linkshell slots NOT resolved (try %d : pattern %s) -- the equipped LS name / colour / rank stay blank", g_lsrTries, a ? "found, operands off" : "missing");
    return g_lsrOk;
}
bool faith_points(int& pts) {
    if (!faith_res()) return false;
    u32 P = 0; unsigned char fl = 0; short v = 0;
    if (!safe_read(g_fr.pVar, &P) || !valid_ptr(P) || !rd8(P + g_fr.flagOff, fl) || !(fl & 0x10) || !rd16(P + g_fr.ptsOff, v)) return false;
    pts = (unsigned short)v; return pts <= 60000;
}
bool faith_read(u32 mgr, u32 focus, NativeFaith& F) {
    memset(&F, 0, sizeof(F)); F.cursor = -1;
    TextList T; F.level = 1;
    if (!tl_find(mgr, "fp_cat2", T)) { F.level = 0; if (!tl_find(mgr, "fp_cat", T)) return false; }
    F.n = T.count; F.top = T.top; F.vis = T.vis > 16 ? 16 : T.vis; F.cursor = T.cursor;
    F.rx0 = T.rx0; F.ry0 = T.ry0; F.rx1 = T.rx1; F.ry1 = T.ry1; F.uiW = T.uiW; F.uiH = T.uiH;
    u32 rows = 0; if (!safe_read(T.h + 0x38, &rows) || (F.n > 0 && !valid_ptr(rows))) return false;
    for (int i = 0; i < F.vis && F.top + i < F.n; ++i) {
        unsigned char b[0x54]; if (!rdblk(rows + (u32)(F.top + i) * 0x54, b, sizeof(b))) return false;
        NativeJpRow& r = F.row[i]; r.id = -1; r.cost = -1;
        u32 rid = 0; memcpy(&rid, b + 0x48, 4); F.rowId[i] = (int)rid;
        for (int c = 0; c < 8; ++c) {
            u32 v = 0, col = 0; memcpy(&v, b + 0x28 + 4 * c, 4); memcpy(&col, b + 8 + 4 * c, 4);
            switch (b[c]) {
            case 0x49: r.id = (int)v; r.colName = col; break;
            case 0x4A: r.lvl = (int)v - 1; r.colLvl = col; break;
            case 0x4B: r.max = (int)v - 1; break;
            case 0x4C: r.cost = v ? (int)v - 1 : -1; r.colCost = col; break;
            default: break;
            }
        }
    }
    F.pointsOk = faith_points(F.points);
    F.focused = focus == T.obj; F.open = true;
    return true;
}
bool cel_list(u32 mgr, u32 focus, const char* name, NativeCelList& L, TextList& T) {
    memset(&L, 0, sizeof(L)); L.cursor = -1;
    if (!tl_find(mgr, name, T)) return false;
    L.n = T.count; L.top = T.top; L.vis = T.vis > 16 ? 16 : T.vis; L.cursor = T.cursor;
    L.rx0 = T.rx0; L.ry0 = T.ry0; L.rx1 = T.rx1; L.ry1 = T.ry1; L.uiW = T.uiW; L.uiH = T.uiH;
    u32 rows = 0; if (!safe_read(T.h + 0x38, &rows) || (L.n > 0 && !valid_ptr(rows))) return false;
    for (int i = 0; i < L.vis && L.top + i < L.n; ++i) {
        unsigned char b[0x54]; if (!rdblk(rows + (u32)(L.top + i) * 0x54, b, sizeof(b))) return false;
        u32 n = 0; memcpy(&n, b + 0x48, 4); if (n > 255) return false;
        L.cel[i] = (int)n;
        for (int c = 0; c < 8; ++c) {
            u32 v = 0, col = 0; memcpy(&v, b + 0x28 + 4 * c, 4); memcpy(&col, b + 8 + 4 * c, 4);
            if (b[c] == 0x0E) L.badge[i] = (int)v;                    // the Lv badge (its raw value : logged)
            if (b[c] == 0x19) L.col[i] = col;                         // the name, in the client's colour
        }
    }
    L.focused = focus == T.obj; L.open = true;
    return true;
}
bool cel_read(u32 mgr, u32 focus, NativeCelestite& K) {
    memset(&K, 0, sizeof(K));
    TextList TE, TI;
    if (!cel_list(mgr, focus, "fe_equip", K.equip, TE) || !cel_list(mgr, focus, "fe_inven", K.inven, TI)) return false;
    int type = -1, ps = -1, pr = -1; unsigned char may = 0;
    if (!safe_read(TE.h + 0x58, (u32*)&type) || type < 0 || type > 5 || !safe_read(TE.h + 0x54, (u32*)&ps) || !rd8(TE.h + 0x50, may)
        || !safe_read(TI.h + 0x50, (u32*)&pr)) return false;
    K.type = type; K.picked = ps; K.pickedRow = pr; K.mayChange = may != 0;
    // the Lv badge : the cell's value when it is the level (1..3) or the keytops3 index (0x235 + level) ; else the slot's
    // place (the client lists Lv3, Lv2, Lv1 top down)
    for (int i = 0; i < K.equip.vis && K.equip.top + i < K.equip.n; ++i) {
        const int v = K.equip.badge[i];
        K.equip.badge[i] = (v >= 1 && v <= 3) ? v : (v >= 0x236 && v <= 0x238) ? v - 0x235 : (K.equip.n - (K.equip.top + i));
    }
    K.open = true;
    return true;
}
// the 0x08C merit list in P : MLIST_GET (FUN_05424C70) holds its two offsets (list +0x0A, count +0x19) ; &P from faith_res
u32 g_mlList = 0, g_mlCount = 0; bool g_mlOk = false; int g_mlTries = 0; DWORD g_mlNext = 0;
bool mlist_res() {
    if (g_mlOk) return true;
    const DWORD now = GetTickCount();
    if (g_mlTries && (int)(now - g_mlNext) < 0) return false;
    ++g_mlTries; g_mlNext = now + 5000;
    const u32 a = aob_masked("8B 54 24 04 85 D2 74 20 8B 8A ?? ?? ?? ?? 85 C9 74 16 8B 44 24 08 66 3B 82 ?? ?? ?? ?? 73 09 25 FF FF 00 00 8D 04 81 C3");
    u32 l = 0, c = 0;
    if (a && safe_read(a + 0x0A, &l) && safe_read(a + 0x19, &c) && l == c + 2 && l < 0x100000) {
        g_mlList = l; g_mlCount = c; g_mlOk = true;
        windower::debug::log("NATIVEUI merit list resolved (try %d) : P + 0x%X (count + 0x%X)", g_mlTries, l, c);
    } else if (g_mlTries == 1 || g_mlTries % 12 == 0) windower::debug::log("NATIVEUI merit list NOT resolved (try %d : AOB %d)", g_mlTries, !!a);
    return g_mlOk;
}
// the levels spent in a category : the P entries {u16 id = 2i, u8 cost, u8 level} whose id >> 6 == cat ; -1 unreadable
int merit_spent(int cat) {
    if (!faith_res() || !mlist_res()) return -1;
    u32 P = 0, lst = 0; short n = 0;
    if (!safe_read(g_fr.pVar, &P) || !valid_ptr(P) || !rd16(P + g_mlCount, n) || n < 0 || n > 3072 || !safe_read(P + g_mlList, &lst)) return -1;
    if (n > 0 && !valid_ptr(lst)) return -1;
    int s = 0;
    for (int k = 0; k < n; ++k) { u32 e = 0; if (!safe_read(lst + (u32)k * 4, &e)) return -1; if (((e & 0xFFFF) >> 6) == (u32)cat) s += (int)(e >> 24); }
    return s;
}
// the list is there (cheap : its count and pointer, every frame) -- meritinf's born-parked rule
bool merit_list_ok() {
    if (!faith_res() || !mlist_res()) return false;
    u32 P = 0, lst = 0; short n = 0;
    return safe_read(g_fr.pVar, &P) && valid_ptr(P) && rd16(P + g_mlCount, n) && n > 0 && n <= 3072 && safe_read(P + g_mlList, &lst) && valid_ptr(lst);
}
// every category's spent levels in ONE walk of the list ; false unreadable
bool merit_spent_all(short out[96]) {
    memset(out, 0, sizeof(short) * 96);
    if (!faith_res() || !mlist_res()) return false;
    u32 P = 0, lst = 0; short n = 0;
    if (!safe_read(g_fr.pVar, &P) || !valid_ptr(P) || !rd16(P + g_mlCount, n) || n < 0 || n > 3072 || !safe_read(P + g_mlList, &lst)) return false;
    if (n > 0 && !valid_ptr(lst)) return false;
    for (int k = 0; k < n; ++k) { u32 e = 0; if (!safe_read(lst + (u32)k * 4, &e)) return false; const u32 c = (e & 0xFFFF) >> 6; if (c < 96) out[c] = (short)(out[c] + (e >> 24)); }
    return true;
}
// the categories' caps : file 0x59 (96 x 12 B) read once ; the entry whose +3 == cat, its +4. Checked : entry 1 = HP/MP (id 1)
unsigned char g_mcap[96 * 12]; bool g_mcapOk = false; int g_mcapTries = 0; DWORD g_mcapNext = 0;
int merit_cap(int cat) {
    if (!g_mcapOk) {
        const DWORD now = GetTickCount();
        if (g_mcapTries >= 5 || (g_mcapTries && (int)(now - g_mcapNext) < 0)) return -1;
        ++g_mcapTries; g_mcapNext = now + 5000;
        char path[MAX_PATH + 8]; unsigned sz = 0; unsigned char* b = 0;
        if (dat_resolve_path(0x59, path, sizeof(path)) && (b = dat_read_file(path, sz)) != 0 && sz >= sizeof(g_mcap)) {
            memcpy(g_mcap, b, sizeof(g_mcap));
            short id1 = 0; memcpy(&id1, g_mcap + 12, 2);
            g_mcapOk = id1 == 1 && g_mcap[12 + 3] == 1 && g_mcap[12 + 4] > 0;
        }
        if (b) dat_free_file(b);
        windower::debug::log("NATIVEUI merit caps (file 0x59) : %s (try %d)", g_mcapOk ? "read" : "NOT a plain table -- the caps stay unknown", g_mcapTries);
        if (!g_mcapOk) return -1;
    }
    for (int e = 0; e < 96; ++e) if (g_mcap[e * 12 + 3] == (unsigned char)cat && g_mcap[e * 12 + 4]) {
        int c = g_mcap[e * 12 + 4];
        if (cat == 26) { if (owns_key_item(3224)) c += 5; if (owns_key_item(3225)) c += 5; if (c > 100) c = 100; }
        return c;
    }
    return -1;
}
bool mcat_read(u32 mgr, u32 focus, NativeMeritCat& K) {
    memset(&K, 0, sizeof(K)); K.cursor = -1; K.meritsLeft = -1; K.cap = K.cap2 = -1;
    TextList T; if (!tl_find(mgr, "meritcat", T)) return false;
    signed char cat = 0; if (!rd8(T.h + 0x50, *(unsigned char*)&cat) || cat < 0 || cat > 95) return false;
    K.cat = cat; K.n = T.count; K.top = T.top; K.vis = T.vis > 16 ? 16 : T.vis; K.cursor = T.cursor;
    K.rx0 = T.rx0; K.ry0 = T.ry0; K.rx1 = T.rx1; K.ry1 = T.ry1; K.uiW = T.uiW; K.uiH = T.uiH;
    u32 rows = 0; if (!safe_read(T.h + 0x38, &rows) || (K.n > 0 && !valid_ptr(rows))) return false;
    for (int i = 0; i < K.vis && K.top + i < K.n; ++i) {
        unsigned char b[0x54]; if (!rdblk(rows + (u32)(K.top + i) * 0x54, b, sizeof(b))) return false;
        NativeMeritRow& r = K.row[i]; r.cost = -1;
        int rid = 0; memcpy(&rid, b + 0x48, 4);
        r.header = rid == -1; r.id = cat ? -1 : rid;                        // a category row : its id ; a merit row : below
        for (int c = 0; c < 8; ++c) {
            u32 v = 0, col = 0; memcpy(&v, b + 0x28 + 4 * c, 4); memcpy(&col, b + 8 + 4 * c, 4);
            switch (b[c]) {
            case 0x1E: if (cat) r.id = (int)v; r.colName = col; break;
            case 0x08: r.lvl = (int)v - 1; r.colLvl = col; break;
            case 0x1F: r.cost = v > 1 ? (int)v - 1 : -1; r.colCost = col; break;
            case 0x07: r.cap = (int)v; break;
            default: break;
            }
        }
    }
    // meritinf's numbers : merits left (the merit mirror), the category under the hand / shown, and its group 2 (jobs)
    PwMem pw; if (read_pointwatch(pw) && pw.merOk) K.meritsLeft = pw.merits;
    int cc = cat;
    if (!cc) { const int r = K.cursor - K.top; if (r >= 0 && r < K.vis && K.top + r < K.n) cc = K.row[r].id; }
    K.comboCat = cc;
    const bool all = merit_spent_all(K.spentAll);
    for (int c = 0; c < 96; ++c) K.capAll[c] = all ? (short)merit_cap(c) : (short)-1;
    if (cc > 0 && all) {
        K.spent = K.spentAll[cc]; K.cap = K.capAll[cc];
        if (cc >= 6 && cc <= 28 && cc != 26) { K.spent2 = K.spentAll[cc + 26]; K.cap2 = K.capAll[cc + 26]; }
    } else if (cc > 0) K.spent = K.spent2 = -1;
    K.focused = focus == T.obj; K.open = true;
    return true;
}
bool equip_read(u32 mgr, u32 focus, NativeEquip& E) {
    memset(&E, 0, sizeof(E)); E.lcursor = -1;
    u32 node = 0; if (!mgr || !safe_read(mgr + MGR_OPEN_LIST, &node)) return false;
    u32 obj = 0;
    for (int g = 0; valid_ptr(node) && g < 400 && !obj; ++g) {
        u32 next = 0, o = 0, def = 0; unsigned char dead = 0, cl = 0; char nm[9];
        safe_read(node + NODE_NEXT, &next); safe_read(node + NODE_VAL, &o); rd8(node + 0x14, dead);
        if (!dead && valid_ptr(o) && safe_read(o + OBJ_DEF, &def) && valid_ptr(def) && rdname(def, nm) && !strcmp(nm, "equip") && rd8(o + OBJ_CLOSING, cl) && !cl) obj = o;
        node = next;
    }
    if (!obj) return false;
    short cur = 0;
    if (!rd16(obj + 0x4C, cur) || !rd16(obj + OBJ_RECT, E.rx0) || !rd16(obj + OBJ_RECT + 2, E.ry0) || !rd16(obj + OBJ_RECT + 4, E.rx1) || !rd16(obj + OBJ_RECT + 6, E.ry1)) return false;
    u32 wh = 0; if (!safe_read(mgr + 0x80, &wh)) return false;
    E.uiW = (short)(wh & 0xFFFF); E.uiH = (short)(wh >> 16);
    if (E.uiW < 320 || E.uiH < 240 || E.uiW > 8000 || E.uiH > 8000) return false;
    if (E.ry0 >= E.uiH) { E.ry0 = (short)(E.ry0 - HIDE_DY); E.ry1 = (short)(E.ry1 - HIDE_DY); }
    E.cursor = cur >= 1 && cur <= 16 ? cur : 0;
    EquipSet es; if (read_equipment(es)) { E.idsOk = true; for (int s = 0; s < 16; ++s) E.id[s] = es.id[s]; }
    {   // each slot's bag : the grid's copy at handler +0x34 + s*4 (bags 8, 10..16 = Mog Wardrobe 1, 2..8)
        u32 hd = 0;
        if (safe_read(obj + OBJ_HANDLER, &hd) && valid_ptr(hd)) for (int s = 0; s < 16; ++s) {
            u32 bag = 0; if (!safe_read(hd + 0x34 + (u32)s * 4, &bag)) break;
            E.ward[s] = (unsigned char)(bag == 8 ? 1 : (bag >= 10 && bag <= 16) ? bag - 8 : 0);
        }
    }
    const u32 blk = fm_addr(FM_PW_BLOCK); unsigned char b[0x14];
    if (blk && rdblk(blk + 0x20, b, sizeof(b))) {
        unsigned short a = 0, d = 0; memcpy(&a, b, 2); memcpy(&d, b + 2, 2); E.atk = a; E.def = d;
        for (int i = 0; i < 8; ++i) memcpy(&E.res[i], b + 4 + 2 * i, 2);
        E.statsOk = a < 10000 && d < 10000;
    }
    E.focused = focus == obj; E.open = true;
    panel_read(mgr, "money", E.money);                                   // Current Gil
    { NativePanel X; E.compare = panel_read(mgr, "itemxinf", X) && X.open;
      static int was = -1, said = 0;                                      // measured, not guessed : each change, bounded
      if ((int)E.compare != was && said < 20) { ++said; was = (int)E.compare; windower::debug::log("NATIVEUI equipment : compare card %s (itemxinf rect %d,%d)", E.compare ? "UP" : "down", X.rx0, X.ry0); } }
    E.gilOk = read_player_gil(E.gil);
    // the slot's item list (inventor in its equipment mode)
    TextList T;
    if (tl_find(mgr, "inventor", T)) {
        u32 rows = 0;
        if (safe_read(T.h + 0x38, &rows) && (T.count == 0 || valid_ptr(rows))) {
            E.ln = T.count; E.ltop = T.top; E.lvis = T.vis > 16 ? 16 : T.vis; E.lcursor = T.cursor;
            E.lx0 = T.rx0; E.ly0 = T.ry0; E.lx1 = T.rx1; E.ly1 = T.ry1;
            bool ok = true;
            for (int i = 0; i < E.lvis && E.ltop + i < E.ln && ok; ++i) {
                unsigned char r[0x54]; if (!rdblk(rows + (u32)(E.ltop + i) * 0x54, r, sizeof(r))) { ok = false; break; }
                int rec = 0; u32 id = 0, cnt = 0, col = 0; memcpy(&rec, r + 0x48, 4); memcpy(&id, r + 0x2C, 4); memcpy(&cnt, r + 0x30, 4); memcpy(&col, r + 0x0C, 4);
                NativeEqRow& w = E.lrow[i]; w.header = rec == -1; w.id = (unsigned short)(id & 0xFFFF); w.count = (int)(cnt > 9999 ? 9999 : cnt); w.col = col;
            }
            E.lopen = ok; E.lfocused = focus == T.obj;
        }
    }
    return true;
}
// the main menu's Synthesis : the grid (tskill1), History (cmbhlst) -- docs/notes/synthesis-re-2026-11-19.md
u32 win_named(u32 mgr, const char* want) {
    u32 node = 0; if (!mgr || !safe_read(mgr + MGR_OPEN_LIST, &node)) return 0;
    for (int g = 0; valid_ptr(node) && g < 400; ++g) {
        u32 next = 0, o = 0, def = 0; unsigned char dead = 0, cl = 0; char nm[9];
        safe_read(node + NODE_NEXT, &next); safe_read(node + NODE_VAL, &o); rd8(node + 0x14, dead);
        if (!dead && valid_ptr(o) && safe_read(o + OBJ_DEF, &def) && valid_ptr(def) && rdname(def, nm) && !strcmp(nm, want) && rd8(o + OBJ_CLOSING, cl) && !cl) return o;
        node = next;
    }
    return 0;
}
// the k-th entry child's state (1-based ; -1 unreadable)
int entry_state(u32 obj, int want) {
    u32 cn = 0; safe_read(obj + 0x14, &cn); int k = 0;
    for (int g = 0; valid_ptr(cn) && g < 64; ++g) {
        u32 cnext = 0, ch = 0; unsigned char cdead = 0; short type = -1; safe_read(cn, &cnext); safe_read(cn + 0x10, &ch); rd8(cn + 0x14, cdead);
        if (valid_ptr(ch) && !cdead && rd16(ch, type) && type == 1 && ++k == want) { u32 st = 0; return safe_read(ch + 0x10, &st) ? (int)(st & 0xFF) : -1; }
        cn = cnext;
    }
    return -1;
}
bool syn_read(u32 mgr, u32 focus, NativeSynth& Y) {
    memset(&Y, 0, sizeof(Y)); Y.hCursor = -1;
    { NativePanel CM; if (!panel_read(mgr, "cmbmenu", CM)) return false; }
    Y.open = true;
    const u32 to = win_named(mgr, "tskill1");
    u32 th = 0, back = 0;
    if (to && safe_read(to + OBJ_HANDLER, &th) && valid_ptr(th) && safe_read(th + 8, &back) && back == to) {
        unsigned char b[0x78]; short cur = 0;
        if (rdblk(th, b, sizeof(b)) && rd16(to + 0x4C, cur) && rd16(to + OBJ_RECT, Y.gx0) && rd16(to + OBJ_RECT + 2, Y.gy0)) {
            u32 wh = 0; safe_read(mgr + 0x80, &wh); Y.gUiW = (short)(wh & 0xFFFF); Y.gUiH = (short)(wh >> 16);
            if (Y.gUiW >= 320 && Y.gUiH >= 240 && Y.gUiW <= 8000 && Y.gUiH <= 8000) {
                if (Y.gy0 >= Y.gUiH) Y.gy0 = (short)(Y.gy0 - HIDE_DY);
                u32 mode = 0; memcpy(&mode, b + 0x70, 4);
                Y.gridMode = (int)mode; Y.gridCursor = cur >= 1 && cur <= 10 ? cur : 0;
                memcpy(&Y.crystalId, b + 0x20, 2); Y.crystalHave = b[0x14] != 0;
                for (int i = 0; i < 8; ++i) {
                    memcpy(&Y.slotId[i], b + 0x22 + 2 * i, 2); Y.slotQty[i] = b[0x36 + i];
                    Y.slotMiss[i] = Y.slotId[i] && mode == 2 && (signed char)b[0x15 + i] < 0;
                }
                Y.synAllowed = entry_state(to, 9) != 5;
                Y.gridFocused = focus == to; Y.gridOpen = mode <= 2;
            }
        }
    }
    TextList T;
    if (tl_find(mgr, "cmbhlst", T)) {
        u32 rows = 0;
        if (safe_read(T.h + 0x38, &rows) && (T.count == 0 || valid_ptr(rows))) {
            Y.hN = T.count; Y.hTop = T.top; Y.hVis = T.vis > 16 ? 16 : T.vis; Y.hCursor = T.cursor;
            Y.hx0 = T.rx0; Y.hy0 = T.ry0; Y.hUiW = T.uiW; Y.hUiH = T.uiH;
            bool ok = true;
            for (int i = 0; i < Y.hVis && Y.hTop + i < Y.hN && ok; ++i) {
                u32 node = 0; short id = 0; unsigned char fav = 0;
                ok = safe_read(rows + (u32)(Y.hTop + i) * 0x54 + 0x48, &node) && valid_ptr(node) && rd16(node + 6, id) && rd8(node + 9, fav);
                Y.hItem[i] = (unsigned short)id; Y.hFav[i] = (fav & 0x80) != 0;
            }
            Y.histOpen = ok; Y.histFocused = focus == T.obj;
        }
    }
    return true;
}
// the quantity box `itemctrl` (a stack : the bin's retrieve, a synthesis material) : handler +0x22 max, +0x23 value
void qty_read(u32 mgr, u32 focus, NativeItems& I) {
    {
        u32 node = 0, qo = 0; safe_read(mgr + MGR_OPEN_LIST, &node);
        for (int g = 0; valid_ptr(node) && g < 400 && !qo; ++g) {
            u32 next = 0, o = 0, def = 0; unsigned char dead = 0, cl = 0; char nm[9];
            safe_read(node + NODE_NEXT, &next); safe_read(node + NODE_VAL, &o); rd8(node + 0x14, dead);
            if (!dead && valid_ptr(o) && safe_read(o + OBJ_DEF, &def) && valid_ptr(def) && rdname(def, nm) && !strcmp(nm, "itemctrl") && rd8(o + OBJ_CLOSING, cl) && !cl) qo = o;
            node = next;
        }
        u32 qh = 0; unsigned char mx = 0, v = 0;
        if (qo && safe_read(qo + OBJ_HANDLER, &qh) && valid_ptr(qh) && rd8(qh + 0x22, mx) && rd8(qh + 0x23, v)) {
            I.qtyOk = mx >= 1 && v <= mx; I.qtyVal = v; I.qtyMax = mx; I.qtyFocused = focus == qo;
        }
        static int saidQ = 0; static bool wasQ = false;
        if (!!qo != wasQ && saidQ < 10) { ++saidQ; wasQ = !!qo;
            windower::debug::log("NATIVEUI items : quantity box (itemctrl) %s -- value %d / max %d%s", qo ? "open" : "closed", I.qtyVal, I.qtyMax, qo ? (I.qtyOk ? " : AioHUD draws it" : " : NOT plausible, the client's stays") : ""); }
    }
}
// the main menu's Items : inventor with handler +0x90 == 1 (set by the main menu's opener only ; Equipment's and the action
// menu's leave it 0), the Recycling Bin (bank) not open
bool items_read(u32 mgr, u32 focus, NativeItems& I) {
    memset(&I, 0, sizeof(I)); I.itCursor = -1; I.binCursor = -1;
    TextList T; if (!tl_find(mgr, "inventor", T)) return false;
    {   // `bank` open : the Recycling Bin when its delegate's bag is 0x11 (else a Mog House storage : not ours)
        NativePanel BP;
        if (panel_read(mgr, "bank", BP)) {
            TextList B; u32 dl = 0, bag = 0; static int said = 0;
            const bool tl = tl_find(mgr, "bank", B);
            const bool dOk = tl && safe_read(B.h + 0x58, &dl) && valid_ptr(dl) && safe_read(dl + 8, &bag);
            if (said < 6) { ++said; windower::debug::log("NATIVEUI items : 'bank' open -- text list %s (rows %d, top %d, vis %d, cursor %d), delegate %08X bag %u%s",
                tl ? "read" : "UNREADABLE", B.count, B.top, B.vis, B.cursor, dl, bag, dOk && bag == 0x11 ? " -> the Recycling Bin, AioHUD draws it" : " -- not the bin : the client's"); }
            if (!dOk || bag != 0x11) return false;
            u32 rows = 0; if (!safe_read(B.h + 0x38, &rows) || (B.count > 0 && !valid_ptr(rows))) return false;
            I.binMode = true; I.binN = B.count; I.binTop = B.top; I.binVis = B.vis > 16 ? 16 : B.vis; I.binCursor = B.cursor;
            I.bx0 = B.rx0; I.by0 = B.ry0; I.binUiW = B.uiW; I.binUiH = B.uiH; I.binFocused = focus == B.obj;
            for (int i = 0; i < I.binVis && I.binTop + i < I.binN; ++i) {
                unsigned char r[0x54]; if (!rdblk(rows + (u32)(I.binTop + i) * 0x54, r, sizeof(r))) return false;
                u32 id = 0, cnt = 0, col = 0; memcpy(&id, r + 0x2C, 4); memcpy(&cnt, r + 0x30, 4); memcpy(&col, r + 0x0C, 4);
                NativeItemRow& w = I.binRow[i];
                w.itemId = (unsigned short)(id & 0xFFFF); w.itemCount = (int)(cnt > 9999 ? 9999 : cnt); w.itemCol = col; w.tempRow = false;
            }
            { ItemBagInfo bi; if (item_bag_info(0x11, bi) && bi.max) I.binCap = bi.max; }
            static int saidR = 0;
            if (saidR < 4) { ++saidR; windower::debug::log("NATIVEUI items : bin rows %d/%d, row 0 id %u count %d colour %08X", I.binN, I.binCap, I.binRow[0].itemId, I.binRow[0].itemCount, I.binRow[0].itemCol); }
        }
    }
    if (!I.binMode) {
        unsigned char fromMain = 0; rd8(T.h + 0x90, fromMain);
        // a TRADE's picker (player or NPC handover) : AioHUD draws it beside its trade panel, whatever the open mode says
        // (2026-11-20, an NPC trade : "l'inventaire a cote c'est celui du jeu" -- the handover's mode is not 1)
        static int saidTm = 0; static unsigned char lastTm = 0xFF;
        if (g_trade.open && fromMain != lastTm && saidTm < 8) { ++saidTm; lastTm = fromMain; windower::debug::log("NATIVEUI items : the trade's picker (open mode %u, %s) -- AioHUD draws it", fromMain, g_trade.npc ? "NPC trade" : "player trade"); }
        if (fromMain != 1 && !g_trade.open) {      // Synthesis's pickers (cmbmenu up) : the crystals, or the materials beside tskill1
            NativePanel CM, TS; if (!panel_read(mgr, "cmbmenu", CM)) return false;
            I.synMode = panel_read(mgr, "tskill1", TS) ? 2 : 1;
        }
    }
    if (I.binMode || I.synMode) qty_read(mgr, focus, I);
    short tmp = 0; if (!rd16(T.h + 0x54, tmp) || tmp < 0 || tmp > T.count) return false;
    u32 rows = 0; if (!safe_read(T.h + 0x38, &rows) || (T.count > 0 && !valid_ptr(rows))) return false;
    I.itN = T.count; I.itTop = T.top; I.itVis = T.vis > 16 ? 16 : T.vis; I.itCursor = T.cursor; I.tempRows = tmp;
    I.rx0 = T.rx0; I.ry0 = T.ry0; I.rx1 = T.rx1; I.ry1 = T.ry1; I.uiW = T.uiW; I.uiH = T.uiH;
    for (int i = 0; i < I.itVis && I.itTop + i < I.itN; ++i) {
        unsigned char r[0x54]; if (!rdblk(rows + (u32)(I.itTop + i) * 0x54, r, sizeof(r))) return false;
        u32 id = 0, cnt = 0, col = 0; memcpy(&id, r + 0x2C, 4); memcpy(&cnt, r + 0x30, 4); memcpy(&col, r + 0x0C, 4);
        NativeItemRow& w = I.itRow[i];
        w.itemId = (unsigned short)(id & 0xFFFF); w.itemCount = (int)(cnt > 9999 ? 9999 : cnt); w.itemCol = col; w.tempRow = I.itTop + i < tmp;
    }
    I.invUsed = I.itN - tmp;
    { ItemBagInfo bi; if (item_bag_info(0, bi) && bi.max) I.invCap = bi.max; }
    I.focused = focus == T.obj; I.open = true;
    return true;
}
// the two item cards' page 2, read from their handler (+0x24 the item, +0x1B8 augments, +0x2C "<...>", +0x278 item level)
void item_extra_poll(u32 mgr) {
    static const char* const W[2] = { "iteminfo", "itemxinf" };
    for (int c = 0; c < 2; ++c) {
        ItemExtra& X = g_iix[c]; X.id = 0;
        u32 node = 0; if (!mgr || !safe_read(mgr + MGR_OPEN_LIST, &node)) continue;
        u32 obj = 0;
        for (int g = 0; valid_ptr(node) && g < 400 && !obj; ++g) {
            u32 next = 0, o = 0, def = 0; unsigned char dead = 0, cl = 0; char nm[9];
            safe_read(node + NODE_NEXT, &next); safe_read(node + NODE_VAL, &o); rd8(node + 0x14, dead);
            if (!dead && valid_ptr(o) && safe_read(o + OBJ_DEF, &def) && valid_ptr(def) && rdname(def, nm) && !strcmp(nm, W[c]) && rd8(o + OBJ_CLOSING, cl) && !cl) obj = o;
            node = next;
        }
        u32 h = 0; short id = 0;
        if (!obj || !safe_read(obj + OBJ_HANDLER, &h) || !valid_ptr(h) || !rd16(h + 0x24, id) || !id) continue;
        unsigned char a[0xC0], l[0x80], v[0xC0];
        if (!rdblk(h + 0x1B8, a, sizeof(a)) || !rdblk(h + 0x2C, l, sizeof(l)) || !rdblk(h + 0x278, v, sizeof(v))) continue;
        item_text_detail::decode(a, sizeof(a), X.aug, sizeof(X.aug));
        item_text_detail::decode(l, sizeof(l), X.line, sizeof(X.line));
        item_text_detail::decode(v, sizeof(v), X.ilvl, sizeof(X.ilvl));
        X.id = (unsigned)(unsigned short)id;
        static unsigned lastId[2] = { 0, 0 }; static int said = 0;
        if (X.id != lastId[c] && said < 30) { ++said; lastId[c] = X.id;
            char a60[64]; lstrcpynA(a60, X.aug, sizeof(a60)); for (char* p = a60; *p; ++p) if (*p == '\n') *p = '|';
            windower::debug::log("NATIVEUI %s page 2 : item %u, augments '%s', line '%s', '%s'", W[c], X.id, a60, X.line, X.ilvl); }
    }
}
// the skill array : FM_PW_BLOCK + 0x60 (the 0x062 handler's copy) ; plausible = every level <= 1000
bool skills_read(NativeSkills& K) {
    memset(&K, 0, sizeof(K));
    const u32 blk = fm_addr(FM_PW_BLOCK); if (!blk) return false;
    for (int i = 0; i < 64; i += 2) {
        u32 w = 0; if (!safe_read(blk + 0x60 + (u32)i * 2, &w)) return false;
        K.v[i] = (unsigned short)(w & 0xFFFF); K.v[i + 1] = (unsigned short)(w >> 16);
    }
    bool any = false;
    for (int i = 1; i < 48; ++i) { if ((K.v[i] & 0x7FFF) > 1000) return false; if (K.v[i] != 0x8000 && K.v[i]) any = true; }
    K.ok = any;
    return K.ok;
}
// an open panel by name : its rect (parked : where it would be) ; false when not open
bool panel_read(u32 mgr, const char* want, NativePanel& P) {
    memset(&P, 0, sizeof(P));
    u32 node = 0; if (!mgr || !safe_read(mgr + MGR_OPEN_LIST, &node)) return false;
    for (int g = 0; valid_ptr(node) && g < 400; ++g) {
        u32 next = 0, o = 0, def = 0; unsigned char dead = 0, cl = 0; char nm[9];
        safe_read(node + NODE_NEXT, &next); safe_read(node + NODE_VAL, &o); rd8(node + 0x14, dead);
        if (!dead && valid_ptr(o) && safe_read(o + OBJ_DEF, &def) && valid_ptr(def) && rdname(def, nm) && !strcmp(nm, want)
            && rd8(o + OBJ_CLOSING, cl) && !cl && rd16(o + OBJ_RECT, P.rx0) && rd16(o + OBJ_RECT + 2, P.ry0) && rd16(o + OBJ_RECT + 4, P.rx1) && rd16(o + OBJ_RECT + 6, P.ry1)) {
            u32 wh = 0; if (!safe_read(mgr + 0x80, &wh)) return false;
            P.uiW = (short)(wh & 0xFFFF); P.uiH = (short)(wh >> 16);
            if (P.uiW < 320 || P.uiH < 240 || P.uiW > 8000 || P.uiH > 8000) return false;
            if (P.ry0 >= P.uiH) { P.ry0 = (short)(P.ry0 - HIDE_DY); P.ry1 = (short)(P.ry1 - HIDE_DY); }
            P.open = true; return true;
        }
        node = next;
    }
    return false;
} int g_mainBad = 0; DWORD g_mainGoodSince = 0;
NativeFep g_fep; DWORD g_fepDrawnMs = 0; int g_fepBadLog = 0; u32 g_fepIn = 0;
int eff_hide(const UiConfig& c) {
    int h = c.nuHide;
    if (g_actFail) h &= ~NU_ACT;
    if (g_npcFail) h &= ~NU_NPC;
    if (g_mainFail) h &= ~NU_MAIN;
    if (g_tbFail) h &= ~NU_HELP;     // AioHUD's top bar cannot read the client's : the client's own shows
    if (g_mcFail) h &= ~NU_MACRO;    // the same for the macro bar
    if (g_nsFail) h &= ~NU_NET;      // ... and the connection box
    if (g_trFail) h &= ~NU_TRADE;    // ... and the trade
    if (g_dlgFail) h &= ~NU_DIALOG;  // ... and the confirms
    return h;
}

// ---- resolution (bounded retry : a miss right after load must not kill the feature for the session, rule 10) ----
u32   g_setter = 0, g_moveto = 0;
// The two client functions that move a LIST RELATIVE to where it already is (found 2026-11-15, both optional : without
// them the engine works, a list's scrollbar just lags) :
//   g_listPlace  FUN_054A0D60 (stdcall(holder, list), ret 8) puts a list beside its parent menu : x = the list's CURRENT x, y
//                computed ; then it writes the scrollbar itself (obj+0x88 = old right - 8, that y) AFTER the move.
//   g_listRelRet the return address of FUN_05563DE0's move-to call : x = current x, y = current y + delta.
u32   g_listPlace = 0, g_listRelRet = 0;
int   g_tries = 0; DWORD g_nextTry = 0; bool g_loggedSlow = false;
bool  g_recovered = false;
char  g_why[96] = "not needed (nothing hidden or moved)";

void image_range(u32& lo, u32& hi) {
    lo = hi = 0; const u32 b = ffximain_base(); if (!b) return;
    u32 e = 0, sz = 0; if (!safe_read(b + 0x3C, &e) || !safe_read(b + e + 0x50, &sz) || !sz || sz > 0x4000000) return;
    lo = b; hi = b + sz;
}
// AOB with a wildcard window [wlo,whi) ; returns the address only when it is UNIQUE
u32 aob_unique(const unsigned char* P, int n, int wlo, int whi) {
    u32 lo = 0, hi = 0; image_range(lo, hi); if (!lo) return 0;
    int hits = 0; u32 at = 0;
    __try {
        for (const unsigned char* q = (const unsigned char*)lo; q + n < (const unsigned char*)hi; ++q) {
            if (q[0] != P[0]) continue;
            bool ok = true;
            for (int i = 1; i < n; ++i) { if (i >= wlo && i < whi) continue; if (q[i] != P[i]) { ok = false; break; } }
            if (ok) { ++hits; at = (u32)q; if (hits > 1) break; }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    return hits == 1 ? at : 0;
}

u32 manager() {
    const u32 slot = fm_addr(FM_MENU_PTR);
    return slot ? slot - MGR_BEFORE_MENU_SLOT : 0;
}
// the manager is PROVEN, not assumed : its definition list must hold real menu definitions
bool manager_ok(u32 mgr) {
    u32 node = 0; if (!mgr || !safe_read(mgr + MGR_DEF_LIST, &node)) return false;
    int good = 0; char nm[9];
    for (int g = 0; valid_ptr(node) && g < 200; ++g) {
        u32 next = 0, def = 0; safe_read(node + NODE_NEXT, &next); safe_read(node + NODE_VAL, &def);
        if (valid_ptr(def) && rdname(def, nm)) ++good;
        node = next;
    }
    return good >= 50;
}

const char* undo_path() { static char p[MAX_PATH] = {0}; if (!p[0]) plugin_path(p, sizeof(p), "data\\native_ui_undo.txt"); return p; }

u32 def_by_name(u32 mgr, const char* want) {
    u32 node = 0; safe_read(mgr + MGR_DEF_LIST, &node); char nm[9];
    for (int g = 0; valid_ptr(node) && g < 2000; ++g) {
        u32 next = 0, def = 0; safe_read(node + NODE_NEXT, &next); safe_read(node + NODE_VAL, &def);
        if (valid_ptr(def) && rdname(def, nm) && !strcmp(nm, want)) return def;
        node = next;
    }
    return 0;
}

// A previous session that ended WITHOUT shutdown (crash, killed client) left records shifted in game memory -- the
// client keeps them until it restarts. The file names every record we had moved and where it really belongs.
void recover_from_file(u32 mgr) {
    FILE* f = fopen(undo_path(), "r"); if (!f) return;
    char ln[96]; int k = 0, miss = 0;
    while (fgets(ln, sizeof(ln), f)) {
        char nm[12] = {0}; int x = 0, y = 0;
        if (sscanf(ln, "%11[^|]|%d|%d", nm, &x, &y) != 3) continue;
        const u32 rec = layout_rec(def_by_name(mgr, nm));
        if (rec && wr16(rec + REC_X, (short)x) && wr16(rec + REC_Y, (short)y)) ++k; else ++miss;
    }
    fclose(f);
    DeleteFileA(undo_path());
    windower::debug::log("NATIVEUI recovery : %d record(s) put back from the last session's undo file (%d not found)", k, miss);
}

void save_undo() {
    if (!g_anyApplied) { DeleteFileA(undo_path()); return; }
    FILE* f = fopen(undo_path(), "w");
    if (!f) { windower::debug::log("NATIVEUI undo file NOT written (%s) -- a crash now would leave windows moved until the client restarts", undo_path()); return; }
    for (int i = 0; i < g_eN; ++i) if (g_e[i].ax || g_e[i].ay) fprintf(f, "%s|%d|%d\n", g_e[i].name, g_e[i].ox, g_e[i].oy);
    fclose(f);
}

// true when a CALL rel32 at `at` lands on the client's move-to function
bool calls_moveto(u32 at) {
    if (!at || !g_moveto) return false;
    unsigned char op = 0; int rel = 0;
    __try { op = *(const unsigned char*)at; rel = *(const int*)(at + 1); } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    return op == 0xE8 && at + 5 + (u32)rel == g_moveto;
}

bool resolve() {
    if (g_setter && g_moveto) return true;
    const DWORD now = GetTickCount();
    if (g_tries && (int)(now - g_nextTry) < 0) return false;
    ++g_tries;
    g_nextTry = now + (g_tries < 30 ? 2000u : 30000u);   // fast for a minute, then slow -- never "give up forever"
    const u32 mgr = manager();
    if (!manager_ok(mgr)) { lstrcpynA(g_why, "menu manager not found yet", sizeof(g_why)); goto miss; }
    if (!g_setter) {   // 83 EC 08 56 8B F1 8D 4E 14 E8 ?? ?? ?? ?? 85 C0 89 44 24 08 C7 44 24 04 00 00 00 00 0F 84
        static const unsigned char P[] = { 0x83,0xEC,0x08,0x56,0x8B,0xF1,0x8D,0x4E,0x14,0xE8,0,0,0,0,0x85,0xC0,0x89,0x44,0x24,0x08,0xC7,0x44,0x24,0x04,0,0,0,0,0x0F,0x84 };
        g_setter = aob_unique(P, sizeof(P), 10, 14);
    }
    if (!g_moveto) {   // 83 EC 0C 66 8B 44 24 14 53 55 8B 6C 24 18 56 8B
        static const unsigned char P[] = { 0x83,0xEC,0x0C,0x66,0x8B,0x44,0x24,0x14,0x53,0x55,0x8B,0x6C,0x24,0x18,0x56,0x8B };
        g_moveto = aob_unique(P, sizeof(P), 0, 0);
    }
    if (!g_setter || !g_moveto) { _snprintf(g_why, sizeof(g_why), "client functions not found (setter %s, move-to %s)", g_setter ? "ok" : "MISSING", g_moveto ? "ok" : "MISSING"); g_why[sizeof(g_why) - 1] = 0; goto miss; }
    windower::debug::log("NATIVEUI resolved after %d tr%s : manager %08X, setter FFXiMain+0x%X, move-to FFXiMain+0x%X",
                         g_tries, g_tries > 1 ? "ies" : "y", mgr, g_setter - ffximain_base(), g_moveto - ffximain_base());
    {   // relative list movers : each must CALL move-to at a known spot, or it is not the function we think it is
        static const unsigned char LP[] = { 0x8B,0x44,0x24,0x04,0x83,0xEC,0x18,0x53,0x56,0x57,0x8B,0x38,0x33,0xDB,0x3B,0xFB,0x0F,0x84 };
        static const unsigned char LR[] = { 0x83,0xEC,0x08,0x56,0x8B,0xF1,0x57,0x33,0xFF,0x39,0x7E,0x24,0x74,0x3A,0x8B,0x4E,0x08,0x8D,0x44,0x24,0x08,0x50 };
        const u32 lp = aob_unique(LP, sizeof(LP), 0, 0), lr = aob_unique(LR, sizeof(LR), 0, 0);
        g_listPlace  = calls_moveto(lp ? lp + 0x126 : 0) ? lp : 0;
        g_listRelRet = calls_moveto(lr ? lr + 0x43 : 0) ? lr + 0x48 : 0;
        windower::debug::log("NATIVEUI list movers : beside-parent %s, relative %s", g_listPlace ? "found" : "NOT found (list scrollbars may lag)",
                             g_listRelRet ? "found" : "NOT found (a scrolled list may take the offset twice)");
    }
    if (!g_recovered) { g_recovered = true; recover_from_file(mgr); }
    g_why[0] = 0;
    return true;
miss:
    if (g_tries <= 3 || g_tries == 30 || (g_tries > 30 && !g_loggedSlow)) {
        windower::debug::log("NATIVEUI not ready (try %d) : %s%s", g_tries, g_why, g_tries >= 30 ? " -- retrying every 30 s from now" : "");
        if (g_tries > 30) g_loggedSlow = true;
    }
    return false;
}

// ---- the HOOKS : a window placed by its screen's code gets its offset before it is ever drawn ----
// A 5-byte JMP over a function's first `len` bytes (whole instructions, no relative operand) ; the trampoline replays
// them and jumps back. Trampolines are allocated ONCE and never freed : a frame could still be inside one after an unhook.
struct Hook { u32 at; const unsigned char* pro; int len; void* dst; unsigned char* tramp; unsigned char orig[8]; bool on; const char* what; };
const unsigned char PRO_MOVETO[8]    = { 0x83,0xEC,0x0C,0x66,0x8B,0x44,0x24,0x14 };
const unsigned char PRO_LISTPLACE[7] = { 0x8B,0x44,0x24,0x04,0x83,0xEC,0x18 };
Hook g_hkMove  = { 0, PRO_MOVETO,    8, 0, 0, {0}, false, "move-to" };
Hook g_hkPlace = { 0, PRO_LISTPLACE, 7, 0, 0, {0}, false, "list beside-parent" };
typedef int (__fastcall* ListPlaceFn)(u32 ecx_, u32 edx_, int* holder, u32 list);   // ret 8 : callee pops ; ecx passed through untouched (cdecl here crashed the client, 2026-11-15)

bool hook_on(Hook& h) {
    if (h.on) return true;
    if (!h.at) return false;
    if (memcmp((const void*)h.at, h.pro, h.len)) { windower::debug::log("NATIVEUI %s prologue changed -- NOT hooked", h.what); return false; }
    if (!h.tramp) {
        h.tramp = (unsigned char*)VirtualAlloc(0, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (!h.tramp) return false;
    }
    memcpy(h.tramp, (const void*)h.at, h.len);
    h.tramp[h.len] = 0xE9; *(u32*)(h.tramp + h.len + 1) = (h.at + h.len) - ((u32)h.tramp + h.len + 5);
    memcpy(h.orig, (const void*)h.at, h.len);
    unsigned char j[8]; j[0] = 0xE9; *(u32*)(j + 1) = (u32)h.dst - (h.at + 5); for (int k = 5; k < h.len; ++k) j[k] = 0x90;
    DWORD old = 0;
    if (!VirtualProtect((void*)h.at, h.len, PAGE_EXECUTE_READWRITE, &old)) return false;
    memcpy((void*)h.at, j, h.len);
    VirtualProtect((void*)h.at, h.len, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void*)h.at, h.len);
    h.on = true;
    windower::debug::log("NATIVEUI %s hooked", h.what);
    return true;
}
void hook_off(Hook& h) {
    if (!h.on) return;
    DWORD old = 0;
    if (VirtualProtect((void*)h.at, h.len, PAGE_EXECUTE_READWRITE, &old)) {
        memcpy((void*)h.at, h.orig, h.len);
        VirtualProtect((void*)h.at, h.len, old, &old);
        FlushInstructionCache(GetCurrentProcess(), (void*)h.at, h.len);
    }
    h.on = false;
    windower::debug::log("NATIVEUI %s unhooked", h.what);
}

// DIAG (2026-11-15) : the next N move-to calls on a window AioHUD offsets -- caller, request, rect + scrollbar
// (obj+0x88 : x, y) before and after. It is how the relative list movers above were found. DEV ONLY now : armed by
// `//aio movediag` (aiohud_probes.cpp, native_ui_movediag) -- it used to re-arm at every record change, and with the
// redrawn menus parking windows on each open / close the parked log window (logwin2) spent ~1300 lines a session.
int g_diagN = 0;
void diag_state(u32 obj, short& rx, short& ry, int& sx, int& sy) {
    rx = ry = 0; sx = sy = -1; u32 sb = 0;
    rd16(obj + OBJ_RECT, rx); rd16(obj + OBJ_RECT + 2, ry);
    if (safe_read(obj + 0x88, &sb) && valid_ptr(sb)) { u32 a = 0, b2 = 0; safe_read(sb, &a); safe_read(sb + 4, &b2); sx = (int)a; sy = (int)b2; }
}

int find_entry_by_obj(u32 obj) {
    u32 def = 0;
    if (valid_ptr(obj) && safe_read(obj + OBJ_DEF, &def)) for (int i = 0; i < g_eN; ++i) if (g_e[i].def == def) return i;
    return -1;
}

u32 g_placingList = 0;   // the list FUN_054A0D60 is placing right now (main thread only)

void __fastcall hk_moveto(u32 obj, u32 /*edx*/, int x, int y) {
    const u32 ret = (u32)_ReturnAddress();
    const int k = find_entry_by_obj(obj); const int rx0 = (short)x, ry0 = (short)y;
    int addX = 0, addY = 0;
    if (k >= 0) {
        addX = g_e[k].ax; addY = g_e[k].ay;
        if (g_listRelRet && ret == g_listRelRet) addX = addY = 0;   // from where the list already is : offset inside
        else if (obj == g_placingList) addX = 0;                    // beside its parent : x is the list's current x
        x = (short)x + addX; y = (short)y + addY;
    }
    const bool diag = k >= 0 && g_diagN > 0 && (g_e[k].ax || g_e[k].ay);
    short bx = 0, by = 0; int bsx = -1, bsy = -1; if (diag) diag_state(obj, bx, by, bsx, bsy);
    ((MoveToFn)g_hkMove.tramp)(obj, x, y);
    if (diag) {
        --g_diagN; short axx = 0, ayy = 0; int asx = -1, asy = -1; diag_state(obj, axx, ayy, asx, asy);
        windower::debug::log("NUIDIAG moveto '%s' from FFXiMain+0x%X : asked %d,%d (+%d,%d) | before %d,%d sb %d,%d | after %d,%d sb %d,%d",
                             g_e[k].name, ret - ffximain_base(), rx0, ry0, addX, addY, bx, by, bsx, bsy, axx, ayy, asx, asy);
    }
}

// FUN_054A0D60 writes the scrollbar from ITS OWN numbers after the move (old right - 8, the y it asked for) : put
// it back where the client's rule puts it for the window as it now stands -- x = right - 8, y = top.
int __fastcall hk_listplace(u32 ecx_, u32 edx_, int* holder, u32 list) {
    const u32 prev = g_placingList; g_placingList = list;
    const int r = ((ListPlaceFn)g_hkPlace.tramp)(ecx_, edx_, holder, list);
    g_placingList = prev;
    const int k = find_entry_by_obj(list);
    if (k >= 0 && (g_e[k].ax || g_e[k].ay)) {
        u32 sb = 0; short top = 0, right = 0;
        if (safe_read(list + 0x88, &sb) && valid_ptr(sb) && rd16(list + OBJ_RECT + 2, top) && rd16(list + OBJ_RECT + 4, right)) {
            __try { *(int*)sb = right - 8; *(int*)(sb + 4) = top; } __except (EXCEPTION_EXECUTE_HANDLER) {}
            if (g_diagN > 0) { --g_diagN; windower::debug::log("NUIDIAG listplace '%s' : scrollbar -> %d,%d", g_e[k].name, right - 8, top); }
        }
    }
    return r;
}

// ---- the TRADE's navigation, in LINES (the user 2026-11-20) : gil / slots 1-4 / slots 5-8 / Cancel - OK. Every input
// (keyboard, GAME PAD, mouse) reaches a menu through the client's event function (live 2026-11-16, //aio mhook) : hooked,
// the 4 directions sent to `trade` are AioHUD's -- the cursor set where the lines say, the client's own "cursor was set"
// event (9) replayed so it refreshes ; everything else, and every other window, passes untouched.
//   up from the top row -> the gil ; down from the bottom row -> Cancel (columns 1-2) / OK (3-4) ; left / right at a
//   row's end : nothing (the client went to OK / Cancel there, and down from the bottom row into the partner's side)
typedef int (__thiscall* MenuEventFn)(u32 obj, int ev);          // (the same as below, by the event function)
const unsigned char PRO_MENUEVENT[6] = { 0x51, 0x55, 0x56, 0x8B, 0xF1, 0x57 };
Hook g_hkEvent = { 0, PRO_MENUEVENT, 6, 0, 0, {0}, false, "menu events (the trade's lines)" };
extern u32 g_tradeObj;   // below (trade_poll) : `trade`'s window object this frame
u32 g_bwObj = 0, g_bwH = 0;  // `buff`'s window object and handler this frame (buffwin_poll fills them)
int g_bwOrd[32] = { 0 }; int g_bwOrdN = 0; DWORD g_bwOrdMs = 0; bool g_bwInner = false;
int g_trNavCol = 1;
int trade_nav_to(int cur, int dx, int dy) {
    if (cur >= 1 && cur <= 8) g_trNavCol = (cur - 1) % 4 + 1;
    const int col = g_trNavCol;
    if (cur == 11) return dy > 0 ? col : 11;                                        // the gil : down -> the top row
    if (cur >= 1 && cur <= 4) { if (dy < 0) return 11; if (dy > 0) return cur + 4; const int c = cur + dx; return c >= 1 && c <= 4 ? c : cur; }
    if (cur >= 5 && cur <= 8) { if (dy < 0) return cur - 4; if (dy > 0) return (cur - 5) < 2 ? 10 : 9; const int c = cur + dx; return c >= 5 && c <= 8 ? c : cur; }
    if (cur == 10) { if (dy < 0) return col <= 2 ? col + 4 : 5; return dx > 0 ? 9 : 10; }      // Cancel (bottom left)
    if (cur == 9)  { if (dy < 0) return col >= 3 ? col + 4 : 8; return dx < 0 ? 10 : 9; }      // OK (bottom right)
    return cur;
}
int __fastcall hk_menuevent(u32 obj, u32 /*edx*/, int ev) {
    // a CONFIRM drawn by AioHUD lays Yes / No side by side : left -> Yes (1), right -> No (2) ; up / down stay the client's
    // (shopbuy / shopsell are a 2-row MENU -- Buy or Sell / Cancel, live 2026-11-20 -- : vertical, the client's own moves)
    if ((ev == 3 || ev == 4) && obj && obj == g_dlgObj && (ui_config().nuHide & NU_DIALOG) && !g_dlgFail && strncmp(g_dlg.name, "shop", 4)) {
        short cur = 0;
        if (rd16(obj + 0x4C, cur) && (cur == 1 || cur == 2)) {
            const short to = ev == 4 ? 1 : 2;                                       // 4 = LEFT, 3 = RIGHT (live 2026-11-20)
            if (to != cur && wr16(obj + 0x4C, to)) return ((MenuEventFn)g_hkEvent.tramp)(obj, 9);
            return 0;
        }
    }
    if (ev >= 1 && ev <= 4 && obj && obj == g_tradeObj && (ui_config().nuHide & NU_TRADE) && !g_trFail) {
        short cur = 0;
        if (rd16(obj + 0x4C, cur) && cur >= 1 && cur <= 11) {
            // 3 = RIGHT, 4 = LEFT (the note said the reverse ; the user saw it mirrored 2026-11-20 -- and moneyctr agrees :
            // 4 multiplies the step, i.e. moves to the digit on the LEFT)
            const int dx = ev == 3 ? 1 : ev == 4 ? -1 : 0, dy = ev == 1 ? 1 : ev == 2 ? -1 : 0;
            const int to = trade_nav_to(cur, dx, dy);
            if (to != cur && wr16(obj + 0x4C, (short)to)) return ((MenuEventFn)g_hkEvent.tramp)(obj, 9);   // the client refreshes for its new cursor
            return 0;                                                               // a row's end : nothing
        }
    }
    // YOUR BUFFS drawn in the party's order : left / right walk that order. The client's own step is kept (it does the
    // help line, its sound) : its cursor is put on the GAME index just before the target (right ; it steps +1, wrapping)
    // or just after it (left), then the same event is passed on. Only while the drawn order is fresh and the whole list.
    if ((ev == 3 || ev == 4) && !g_bwInner && obj && obj == g_bwObj && g_bwH && (ui_config().nuHide & NU_BUFF)
        && g_bwOrdN > 1 && GetTickCount() - g_bwOrdMs < 500) {
        u32 cu = 0, cn = 0;
        if (safe_read(g_bwH + 0x30, &cu) && safe_read(g_bwH + 0x40, &cn) && (int)cn == g_bwOrdN && cu < cn) {
            int pos = -1; for (int i = 0; i < g_bwOrdN; ++i) if (g_bwOrd[i] == (int)cu) { pos = i; break; }
            if (pos >= 0) {
                const int n = g_bwOrdN, np = ev == 3 ? (pos + 1) % n : (pos + n - 1) % n, to = g_bwOrd[np];
                const u32 pre = (u32)(ev == 3 ? (to + n - 1) % n : (to + 1) % n);
                bool ok = true;
                __try { *(volatile u32*)(g_bwH + 0x30) = pre; } __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
                if (ok) return ((MenuEventFn)g_hkEvent.tramp)(obj, ev);
            }
        }
    }
    return ((MenuEventFn)g_hkEvent.tramp)(obj, ev);
}
bool trade_nav_hook() {                                                             // installed once the event function is known
    if (g_hkEvent.on) return true;
    const u32 fn = menu_event_fn(); if (!fn) return false;
    g_hkEvent.at = fn; g_hkEvent.dst = (void*)&hk_menuevent;
    return hook_on(g_hkEvent);
}
// the event hook, for the trade's lines and the confirms' left / right : a bounded retry (20 x 5 s), logged, then given up
// (the client's own moves stay -- nothing breaks)
void ensure_event_hook() {
    static int tries = 0; static DWORD next = 0;
    if (g_hkEvent.on || tries >= 20 || (int)(GetTickCount() - next) < 0) return;
    ++tries; next = GetTickCount() + 5000;
    if (!trade_nav_hook() && (tries == 1 || tries == 20))
        windower::debug::log("NATIVEUI the menu-event hook is not installed (try %d)%s -- the client's own moves stay in the trade / confirms", tries, tries == 20 ? ", giving up" : "");
}
bool hook_install() {
    g_hkMove.at = g_moveto;     g_hkMove.dst = (void*)&hk_moveto;
    g_hkPlace.at = g_listPlace; g_hkPlace.dst = (void*)&hk_listplace;
    const bool ok = hook_on(g_hkMove);
    if (ok && g_listPlace) hook_on(g_hkPlace);   // optional : without it only the list scrollbars lag
    return ok;
}
void hook_remove() { hook_off(g_hkPlace); hook_off(g_hkMove); }   // (the trade's event hook : its own life, off at shutdown)

// After the setter, the window's handler refreshes the origins it caches : slot 5 is what the client itself calls
// on every re-show (query's question line) ; titlewin's slot 5 is empty and its text origin is copied by slot 1.
void refresh_handler(u32 obj, const char* name) {
    u32 hd = 0, vt = 0, f5 = 0, f1 = 0;
    if (!safe_read(obj + OBJ_HANDLER, &hd) || !valid_ptr(hd) || !safe_read(hd, &vt) || !valid_ptr(vt)) return;
    __try {
        if (safe_read(vt + 0x14, &f5) && valid_ptr(f5)) ((HandlerFn)f5)(hd);
        if (!strcmp(name, "titlewin") && safe_read(vt + 0x04, &f1) && valid_ptr(f1)) ((HandlerFn)f1)(hd);
    } __except (EXCEPTION_EXECUTE_HANDLER) { windower::debug::log("NATIVEUI '%s' handler refresh faulted", name); }
}

// move every OPEN window whose record just changed by its delta (they were placed before the change)
struct Delta { u32 def; int dx, dy; };
void move_open(u32 mgr, const Delta* D, int nD) {
    if (!nD || !g_setter) return;
    u32 wh = 0; const int uiH = safe_read(mgr + 0x80, &wh) ? (short)(wh >> 16) : 0;
    u32 node = 0; safe_read(mgr + MGR_OPEN_LIST, &node);
    for (int g = 0; valid_ptr(node) && g < 400; ++g) {
        u32 next = 0, obj = 0, def = 0; safe_read(node + NODE_NEXT, &next); safe_read(node + NODE_VAL, &obj);
        unsigned char cl = 0; short r[4] = {0}; char nm[9];
        if (valid_ptr(obj) && safe_read(obj + OBJ_DEF, &def) && valid_ptr(def) && rdname(def, nm) && rd8(obj + OBJ_CLOSING, cl) && !cl
            && rd16(obj + OBJ_RECT, r[0]) && rd16(obj + OBJ_RECT + 2, r[1]) && rd16(obj + OBJ_RECT + 4, r[2]) && rd16(obj + OBJ_RECT + 6, r[3])) {
            const int w = r[2] - r[0], h = r[3] - r[1];
            for (int i = 0; i < nD; ++i) if (D[i].def == def) {
                // a PARK onto a window already parked, or an unpark of one already on screen, is a double move : skipped.
                // (2026-11-18 : a reload with Status open -- the stat panels were parked by name before this first apply,
                // which parked them again ; the box read 3000 too low and sat on the chat)
                const bool parked = uiH >= 240 && r[1] >= uiH;
                const bool twice = (D[i].dy >= HIDE_DY / 2 && parked) || (D[i].dy <= -HIDE_DY / 2 && !parked && uiH >= 240);
                if (twice) { static int said = 0; if (said < 20) { ++said; windower::debug::log("NATIVEUI '%s' at y %d : its move of %d not applied (already there)", nm, r[1], D[i].dy); } break; }
                if (w > 0 && h > 0 && w < 4000 && h < 4000) {
                    __try { ((SetRectFn)g_setter)(obj, r[0] + D[i].dx, r[1] + D[i].dy, w, h, 1, 0, 0); }
                    __except (EXCEPTION_EXECUTE_HANDLER) { windower::debug::log("NATIVEUI setter faulted on '%s'", nm); }
                    refresh_handler(obj, nm);
                }
                break;
            }
        }
        node = next;
    }
}

// ---- the HUD-like windows the client places STRAIGHT from their record : keep the open one where its record says ----
// 2026-11-16 ("Party window ON -> the game's party window SHOWS") : with everything native the open partywin sat at
// y -1844 -- its place (record 298 + H-448 = 1156) MINUS HIDE_DY, moved twice by a delta at some point -- so every
// toggle after that was inverted (ON added 3000 and brought it ON screen). A delta can only carry a drift forward ;
// these windows have an absolute truth (x + W-512 when anchored right, y + H-448 when anchored bottom, UI units), so
// any open one off it by half a park or more is put back there (a smaller difference is the client's own business).
static bool hud_like(const char* n) {
    return in_list(n, PARTY_W) || in_list(n, ALLY_W) || in_list(n, CHAT_W) || in_list(n, HELP_W) || in_list(n, COST_W) || in_list(n, NET_W) || in_list(n, BUFF_W);
}
static int g_healLog = 0;
void heal_hud(u32 mgr) {
    if (!g_setter) return;
    u32 wh = 0; if (!safe_read(mgr + 0x80, &wh)) return;
    const int W = (short)(wh & 0xFFFF), H = (short)(wh >> 16);
    if (W < 320 || H < 240 || W > 8000 || H > 8000) return;
    u32 node = 0; safe_read(mgr + MGR_OPEN_LIST, &node);
    for (int g = 0; valid_ptr(node) && g < 400; ++g) {
        u32 next = 0, obj = 0, def = 0; safe_read(node + NODE_NEXT, &next); safe_read(node + NODE_VAL, &obj);
        unsigned char cl = 0; short r[4] = {0, 0, 0, 0}; char nm[9];
        if (valid_ptr(obj) && safe_read(obj + OBJ_DEF, &def) && valid_ptr(def) && rdname(def, nm) && hud_like(nm) && rd8(obj + OBJ_CLOSING, cl) && !cl
            && rd16(obj + OBJ_RECT, r[0]) && rd16(obj + OBJ_RECT + 2, r[1]) && rd16(obj + OBJ_RECT + 4, r[2]) && rd16(obj + OBJ_RECT + 6, r[3])) {
            const int k = find_entry_by_obj(obj);
            if (k >= 0 && (g_e[k].ox || g_e[k].oy || g_e[k].anc)) {
                const Entry& e = g_e[k];
                const int tx = e.ox + e.ax + ((e.anc & 1) ? W - 512 : 0), ty = e.oy + e.ay + ((e.anc & 2) ? H - 448 : 0);
                const int w = r[2] - r[0], h = r[3] - r[1];
                const int ddx = r[0] - tx, ddy = r[1] - ty;
                // only when it SHOWS where it should be hidden, or the reverse : a window the client places relative to
                // another (logwin2 under the log) can sit elsewhere off-screen, and fighting it there is noise
                const bool onNow = r[1] < H && r[3] > 0 && r[0] < W && r[2] > 0, onWant = ty < H && ty + h > 0 && tx < W && tx + w > 0;
                if (onNow != onWant && (ddx >= HIDE_DY / 2 || ddx <= -HIDE_DY / 2 || ddy >= HIDE_DY / 2 || ddy <= -HIDE_DY / 2) && w > 0 && h > 0 && w < 4000 && h < 4000) {
                    // the formula is only good to a few dozen units (2026-11-16 : it parked partywin at 4076, the client's own
                    // place was 4156 -> shown back 80 too high) : it only COUNTS the whole parks the window is off by, and
                    // exactly those are taken out -- the client's own fine placement is kept
                    const int kx = (ddx + (ddx >= 0 ? HIDE_DY / 2 : -HIDE_DY / 2)) / HIDE_DY, ky = (ddy + (ddy >= 0 ? HIDE_DY / 2 : -HIDE_DY / 2)) / HIDE_DY;
                    const int nx = r[0] - kx * HIDE_DY, ny = r[1] - ky * HIDE_DY;
                    bool ok = true;
                    __try { ((SetRectFn)g_setter)(obj, nx, ny, w, h, 1, 0, 0); }
                    __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
                    if (ok) refresh_handler(obj, nm);
                    if (g_healLog < 40) { ++g_healLog; windower::debug::log("NATIVEUI '%s' was at %d,%d, its record says ~%d,%d (applied %d,%d) : %d,%d park(s) off -> %d,%d : %s", nm, r[0], r[1], tx, ty, e.ax, e.ay, kx, ky, nx, ny, ok ? "put back" : "setter faulted"); }
                }
            }
        }
        node = next;
    }
}

// Walk every definition, keep our picture of each record TRUE, and bring each record to its desired offset.
// zero=true : every desired offset is 0 (shutdown / nothing configured).
void apply(bool zero) {
    const u32 mgr = manager(); if (!manager_ok(mgr)) return;
    const UiConfig& c = ui_config();
    const int hide = zero ? 0 : eff_hide(c), mdx = zero ? 0 : c.nuMenuDx, mdy = zero ? 0 : c.nuMenuDy;
    if (zero) g_dock.on = false; else dock_compute(mgr);
    for (int i = 0; i < g_eN; ++i) g_e[i].alive = false;
    static Delta D[768]; int nD = 0; int changed = 0;
    u32 node = 0; safe_read(mgr + MGR_DEF_LIST, &node);
    for (int g = 0; valid_ptr(node) && g < 2000; ++g) {
        u32 next = 0, def = 0; safe_read(node + NODE_NEXT, &next); safe_read(node + NODE_VAL, &def);
        char nm[9]; const u32 rec = (valid_ptr(def) && rdname(def, nm)) ? layout_rec(def) : 0;
        short cx = 0, cy = 0; unsigned char anc = 0;
        if (rec && rd16(rec + REC_X, cx) && rd16(rec + REC_Y, cy) && rd8(rec + REC_ANCHOR, anc)) {
            int at = -1; for (int i = 0; i < g_eN; ++i) if (g_e[i].rec == rec) { at = i; break; }
            if (at < 0 && g_eN < (int)(sizeof(g_e) / sizeof(g_e[0]))) {
                Entry& e = g_e[g_eN]; memset(&e, 0, sizeof(e)); e.rec = rec; e.def = def; memcpy(e.name, nm, 9);
                e.ox = cx; e.oy = cy; e.anc = anc; at = g_eN++;
            }
            if (at >= 0) {
                Entry& e = g_e[at]; e.alive = true;
                if (cx != e.ox + e.ax || cy != e.oy + e.ay) {    // the CLIENT rewrote it (relog, DAT reload) : re-learn
                    if (!(cx == e.ox && cy == e.oy)) { e.ox = cx; e.oy = cy; }
                    e.ax = e.ay = 0;
                }
                int dx = 0, dy = 0;
                if (zero || !dock_offset(e, dx, dy)) desired_offset(e.name, e.ox, e.oy, e.anc, hide, mdx, mdy, dx, dy);
                if (dx != e.ax || dy != e.ay) {
                    if (wr16(rec + REC_X, (short)(e.ox + dx)) && wr16(rec + REC_Y, (short)(e.oy + dy))) {
                        if (nD < 768) { D[nD].def = e.def; D[nD].dx = dx - e.ax; D[nD].dy = dy - e.ay; ++nD; }
                        e.ax = (short)dx; e.ay = (short)dy; ++changed;
                    }
                }
            }
        }
        node = next;
    }
    int k = 0; bool any = false;   // forget records that are gone -- never write to one we did not just see
    for (int i = 0; i < g_eN; ++i) if (g_e[i].alive) { g_e[k++] = g_e[i]; if (g_e[i].ax || g_e[i].ay) any = true; }
    g_eN = k;
    if (any) hook_install();          // before moving : a window re-placed during the move must already get its offset
    move_open(mgr, D, nD);
    heal_hud(mgr);                    // a HUD-like window off its record by a park (a drift) : back to its absolute place
    if (!any) hook_remove();
    if (changed || any != g_anyApplied) {
        g_anyApplied = any;
        save_undo();
        windower::debug::log("NATIVEUI applied : %d record(s) changed, %d window(s) moved now, hide mask %d, menus offset %d,%d -- %s",
                             changed, nD, hide, mdx, mdy, any ? "active" : "everything native");
    }
}

int  g_lastSig = -1; DWORD g_nextWalk = 0;
int  config_sig(const UiConfig& c) {
    // every NativeUiBits bit (was & 0xFF : NU_MAIN / NU_MACRO toggles waited for the once-a-second walk) ; offsets 11 bits each
    int s = (eff_hide(c) & NU_ALL) ^ ((c.nuMenuDx & 0x7FF) << 11) ^ ((c.nuMenuDy & 0x7FF) << 21);   // a change detector : XOR overlap is fine
    if (g_eqUp) s ^= 0x2A5B3C1D;                              // the Equipment screen's item-card rule : applied at once
    s ^= (c.chDock & 3) << 30 ^ (c.chInputOk + 1) << 28;      // the input-line mode, and whether its text reads
    if (c.chDock && c.chShow[0] && c.chDockRect[2] > 0.0f)   // the docked input line follows the chat box : re-apply when it moves
        s ^= (int)c.chDockRect[0] * 73856093 ^ (int)(c.chDockRect[1] + c.chDockRect[3]) * 19349663 ^ (int)c.chDockRect[5] * 83492791 ^ 0x5A5A;
    return s;
}

} // namespace

void chat_info_read(u32 obj);   // below : the Chat submenu's extras (Tell name, linkshells, Unity)
int  list_read(NativeList& L);   // below : the spell list

int inv_read(u32 focus, NativeList& L);   // below, at namespace scope : the Items list (inventor, from the action menu)
bool comment_read(u32 mgr, u32 focus, NativeComment& K);   // below, at namespace scope : Search -> Edit your comment
bool ls_read(u32 mgr, u32 focus, NativeLinkshell& L);       // below : the Linkshell items
bool ls_equipped(int n, unsigned& lsId, unsigned& rgb, char* name, int cap);   // below : equipped LS n (1 / 2)
void mon_read(NativeProfile& P);          // below : the Profile's Mentor + Monstrosity (statics found by signature)
bool jobs_read(NativeJobs& J);            // below : the Job / Master Levels (the player object, found by signature)
namespace {
// One read of "playermo" : open (in the manager's open list, not dying), its handler's row codes, cursor, rect.
// Returns -1 when it is not open, 0 when open but unreadable, 1 when read.
int act_read(NativeMenu& m, u32* objOut = 0) {
    memset(&m, 0, sizeof(m)); if (objOut) *objOut = 0;
    const u32 mgr = manager(); if (!mgr) return -1;
    u32 node = 0; safe_read(mgr + MGR_OPEN_LIST, &node);
    u32 obj = 0;
    for (int g = 0; valid_ptr(node) && g < 400; ++g) {
        u32 next = 0, o = 0, def = 0; unsigned char dead = 0;
        safe_read(node + NODE_NEXT, &next); safe_read(node + NODE_VAL, &o); rd8(node + 0x14, dead);
        char nm[9];
        if (!dead && valid_ptr(o) && safe_read(o + OBJ_DEF, &def) && valid_ptr(def) && rdname(def, nm) && !strcmp(nm, "playermo")) { obj = o; break; }
        node = next;
    }
    if (!obj) return -1;
    unsigned char closing = 0; rd8(obj + OBJ_CLOSING, closing);
    if (closing) return -1;
    u32 hd = 0, cur = 0;
    if (!safe_read(obj + OBJ_HANDLER, &hd) || !valid_ptr(hd) || !safe_read(obj + 0x4C, &cur)) return 0;
    unsigned char n = 0; if (!rd8(hd + 0x53, n) || n < 1 || n > 16) return 0;
    for (int i = 0; i < n; ++i) {
        short v = 0; if (!rd16(hd + 0x1C + 2 * i, v) || v < 1 || v > 0x1B) return 0;   // the client's code table holds 1..0x1B
        m.code[i] = (unsigned short)v;
    }
    m.n = n;
    m.cursor = (int)(cur & 0xFFFF); if (m.cursor < 1 || m.cursor > n) m.cursor = 1;
    if (!rd16(obj + OBJ_RECT, m.rx0) || !rd16(obj + OBJ_RECT + 2, m.ry0) || !rd16(obj + OBJ_RECT + 4, m.rx1) || !rd16(obj + OBJ_RECT + 6, m.ry1)) return 0;
    u32 wh = 0; if (!safe_read(mgr + 0x80, &wh)) return 0;
    m.uiW = (short)(wh & 0xFFFF); m.uiH = (short)(wh >> 16);
    if (m.uiW < 320 || m.uiH < 240 || m.uiW > 8000 || m.uiH > 8000) return 0;
    if (m.ry0 >= m.uiH) { m.ry0 = (short)(m.ry0 - HIDE_DY); m.ry1 = (short)(m.ry1 - HIDE_DY); }   // parked : where it would be
    u32 focus = 0; const u32 fp = aio::fm_addr(aio::FM_MENU_PTR); if (fp) safe_read(fp, &focus);
    m.focused = focus == obj;
    m.open = true;
    if (objOut) *objOut = obj;
    return 1;
}
// the label of entry k (1-based) as the menu's DEFINITION writes it : *(def+4) = the entries, 0x14 each, +0x0C the label
// (section 8 ; the client's own FUN_0545C770). Printable ASCII only ; "" when there is none.
void def_label(u32 obj, int k, char* out, int cap) {
    out[0] = 0;
    u32 def = 0, arr = 0, cnt = 0, lp = 0;
    if (!safe_read(obj + OBJ_DEF, &def) || !valid_ptr(def) || !safe_read(def + 0x04, &arr) || !safe_read(def + 0x18, &cnt)) return;
    if (!valid_ptr(arr) || k < 1 || k > (short)cnt || !safe_read(arr + (u32)(k - 1) * 0x14 + 0x0C, &lp) || !valid_ptr(lp)) return;
    int n = 0;
    for (; n < cap - 1; ++n) { unsigned char b = 0; if (!rd8(lp + (u32)n, b) || b < 0x20 || b >= 0x7F) break; out[n] = (char)b; }
    out[n] = 0;
}
// One read of an open ENTRY menu named in W (the action SUBMENUS ACT_SUB_W, the MAIN menu's pages MAIN_W ; the focused
// one when two are open). -1 none, 0 unreadable, 1 read.
int entry_read(const char* const* W, NativeEntryMenu& m, u32* objOut) {
    memset(&m, 0, sizeof(m)); if (objOut) *objOut = 0;
    const u32 mgr = manager(); if (!mgr) return -1;
    u32 focus = 0; const u32 fp = aio::fm_addr(aio::FM_MENU_PTR); if (fp) safe_read(fp, &focus);
    u32 node = 0; safe_read(mgr + MGR_OPEN_LIST, &node);
    u32 obj = 0; char name[9] = ""; int objAt = -1;
    auto at_of = [&](const char* n) { for (int i = 0; W[i]; ++i) if (!strcmp(W[i], n)) return i; return -1; };
    for (int g = 0; valid_ptr(node) && g < 400; ++g) {
        u32 next = 0, o = 0, def = 0; unsigned char dead = 0, cl = 0;
        safe_read(node + NODE_NEXT, &next); safe_read(node + NODE_VAL, &o); rd8(node + 0x14, dead);
        char nm[9];
        if (!dead && valid_ptr(o) && safe_read(o + OBJ_DEF, &def) && valid_ptr(def) && rdname(def, nm) && in_list(nm, W)
            && rd8(o + OBJ_CLOSING, cl) && !cl) {
            // the focused one ; else the one listed LAST (a menu opened over another : Unity's over Status's, whose child
            // -- ut_point, quest00 -- then has the focus)
            const int a = at_of(nm);
            if (!obj || o == focus || (obj != focus && a > objAt)) { obj = o; memcpy(name, nm, 9); objAt = a; }
        }
        node = next;
    }
    if (!obj) return -1;
    u32 cur = 0; if (!safe_read(obj + 0x4C, &cur)) return 0;
    // the entry children, in list order (= the cursor's numbering)
    short ys[32]; int k = 0;
    u32 cn = 0; safe_read(obj + 0x14, &cn);
    for (int g = 0; valid_ptr(cn) && g < 64 && k < 32; ++g) {
        u32 cnext = 0, ch = 0; unsigned char cdead = 0; safe_read(cn, &cnext); safe_read(cn + 0x10, &ch); rd8(cn + 0x14, cdead);
        short type = -1;
        if (valid_ptr(ch) && !cdead && rd16(ch, type) && type == 1) {
            ++k;                                                         // entry index k (1-based), shown or not
            u32 st = 0; short r[4] = { 0, 0, 0, 0 };
            if (!safe_read(ch + 0x10, &st) || !rd16(ch + 0x32, r[0]) || !rd16(ch + 0x34, r[1]) || !rd16(ch + 0x36, r[2]) || !rd16(ch + 0x38, r[3])) return 0;
            // skipped / not laid out / a DECORATION, not a row (chatctrl's 8th entry : 18 x 12, never under the cursor)
            if ((st & 0x10) || r[2] - r[0] < 24 || r[3] - r[1] <= 0 || m.n >= 32) { cn = cnext; continue; }
            int at = m.n;                                                // insert by screen y
            while (at > 0 && ys[at - 1] > r[1]) { ys[at] = ys[at - 1]; m.entry[at] = m.entry[at - 1]; m.state[at] = m.state[at - 1]; memcpy(m.label[at], m.label[at - 1], sizeof(m.label[at])); --at; }
            ys[at] = r[1]; m.entry[at] = (unsigned char)k; m.state[at] = (unsigned char)(st & 0xFF);
            def_label(obj, k, m.label[at], sizeof(m.label[at]));
            ++m.n;
        }
        cn = cnext;
    }
    if (m.n < 1) return 0;
    m.cursor = (int)(cur & 0xFFFF);
    if (!rd16(obj + OBJ_RECT, m.rx0) || !rd16(obj + OBJ_RECT + 2, m.ry0) || !rd16(obj + OBJ_RECT + 4, m.rx1) || !rd16(obj + OBJ_RECT + 6, m.ry1)) return 0;
    u32 wh = 0; if (!safe_read(mgr + 0x80, &wh)) return 0;
    m.uiW = (short)(wh & 0xFFFF); m.uiH = (short)(wh >> 16);
    if (m.uiW < 320 || m.uiH < 240 || m.uiW > 8000 || m.uiH > 8000) return 0;
    if (m.ry0 >= m.uiH) { m.ry0 = (short)(m.ry0 - HIDE_DY); m.ry1 = (short)(m.ry1 - HIDE_DY); }
    memcpy(m.name, name, 9);
    m.focused = focus == obj;
    m.open = true;
    if (objOut) *objOut = obj;
    return 1;
}
int sub_read(NativeEntryMenu& m, u32* objOut = 0) { return entry_read(ACT_SUB_W, m, objOut); }
int main_read(NativeEntryMenu& m, u32* objOut = 0) { return entry_read(MAIN_W, m, objOut); }

// The MAIN menu : the same fallback as the action menu -- 3 unreadable frames while open, the client's own comes back ;
// 5 s of good reads, AioHUD's again. Logged both ways.
// Status's list is up -- or a menu opened over it that is itself a main-menu submenu (Merit Points' merit1 / merit2 /
// merit3 : the submenu read then names THAT one, 2026-11-18 -- Categories was never read)
bool inStatusNow() {
    if (!g_mainSub.open) return false;
    const char* n = g_mainSub.name;
    return !strcmp(n, "statcom2") || !strcmp(n, "merit1") || !strcmp(n, "merit2") || !strcmp(n, "merit3");
}
void main_poll(bool on) {
    if (!on) { g_eqUp = false; g_items.open = false; g_syn.open = false; g_cmt.open = false; g_ls.open = false; g_equip.open = false; g_equip.lopen = false; g_eqRule = false; g_mcat.open = false; g_mcatRule = false; g_cel.open = false; g_faith.open = false; g_faithRule = false; g_jpRule = false; g_jcat.open = false; g_jpts.open = false; g_jpts.box.open = false; g_area.open = false; g_search.open = false; g_scopt.open = false; g_urank.open = false; g_uinfo.open = false; g_utW.open = false; g_unityOk = false; g_cur.open = false; g_meritsOk = false; g_trdW.open = g_lmW.open = false; g_skillsOk = false; g_skillsOkRule = false; g_btlW.open = g_mgcW.open = false; g_jobsOurs = false; g_jobW.open = g_mlW.open = false; g_profDataOk = false; g_persDataOk = false; g_persona.open = false; g_levelW.open = false; g_prof.open = false; g_mainM.open = false; g_mainSub.open = false; g_mainSort.open = false; g_mainYn.open = false; g_mainBad = 0; g_mainFail = false; return; }
    NativeEntryMenu m; int r = main_read(m);
    NativeEntryMenu sm; u32 smObj = 0; const int rs = entry_read(MAIN_SUB_W, sm, &smObj);
    for (int e = 0; e < 32; ++e) sm.entVal[e] = sm.entVal2[e] = -1;
    if (rs == 1 && smObj && (!strcmp(sm.name, "scchar") || !strcmp(sm.name, "scmlev"))) {
        // two number fields : handler +0x14 editing, +0x16 the field (0 min, 1 max), +0x18 min, +0x1A max (search-re s.6)
        u32 hd = 0; unsigned char b[0x1C];
        if (safe_read(smObj + OBJ_HANDLER, &hd) && valid_ptr(hd) && rdblk(hd, b, sizeof(b))) {
            short mn = 0, mx = 0; memcpy(&mn, b + 0x18, 2); memcpy(&mx, b + 0x1A, 2);
            if (mn >= 0 && mn <= 99 && mx >= 0 && mx <= 99) { sm.entVal[1] = mn; sm.entVal[2] = mx; }
            if (b[0x14] && b[0x16] <= 1) { sm.entMarks = 1u << (b[0x16] + 1); sm.entMarkKind = 3; }
        }
    }
    if (rs == 1 && smObj && !strcmp(sm.name, "scname")) {
        // the name : handler +0x14 = the text input while typing (its display copy +0x3A0, length +0x690 ; 7F FF = the caret),
        // else +0x18 = the name committed (search-re s.5)
        u32 hd = 0, in = 0;
        if (safe_read(smObj + OBJ_HANDLER, &hd) && valid_ptr(hd) && safe_read(hd + 0x14, &in)) {
            unsigned char t[64]; int o = 0;
            if (valid_ptr(in)) {
                u32 len = 0; safe_read(in + 0x690, &len); if (len > 60) len = 60;
                if (rdblk(in + 0x3A0, t, sizeof(t))) for (u32 i = 0; i < len && t[i] && o < 15; ++i) {
                    if (t[i] == 0x7F) { ++i; continue; }                     // the caret / a control pair
                    if (t[i] >= 0x20 && t[i] < 0x7F) sm.entName[o++] = (char)t[i];
                }
                sm.entMarks = 1u << 1; sm.entMarkKind = 3;                  // typing : the field is being edited
            } else if (rdblk(hd + 0x18, t, 16)) { for (int i = 0; i < 15 && t[i]; ++i) if (t[i] >= 0x20 && t[i] < 0x7F) sm.entName[o++] = (char)t[i]; }
            sm.entName[o] = 0; sm.entVal[1] = 1;
        }
    }
    if (rs == 1 && !strcmp(sm.name, "scsibori")) {
        // Refine Search : the request R = scresult's handler + 0x5C (search-re s.2) -- what the search holds now
        TextList T; unsigned char q[0x6C];
        if (tl_find(manager(), "scresult", T) && rdblk(T.h + 0x5C, q, sizeof(q))) {
            u32 fl = 0, sp = 0; memcpy(&fl, q, 4); memcpy(&sp, q + 0x60, 4);
            if (fl & 0x2) { short z = 0; memcpy(&z, q + 0x24, 2); int n = 0; for (int i = 0; i < 16; ++i) { short zz = 0; memcpy(&zz, q + 0x24 + 2 * i, 2); if (zz < 1) break; ++n; }
                            sm.entVal[1] = z; sm.entVal2[1] = n; }
            if (fl & 0x1) { int k = 0; for (; k < 15 && q[0x0C + k]; ++k) sm.entName[k] = (q[0x0C + k] >= 0x20 && q[0x0C + k] < 0x7F) ? (char)q[0x0C + k] : '?'; sm.entName[k] = 0; if (k) sm.entVal[2] = 1; }
            if (fl & 0x8) sm.entVal[3] = q[0x46];
            if (fl & 0x4) sm.entVal[4] = q[0x44];
            if (fl & 0x20) sm.entVal[5] = q[0x48];
            short lmn = 0, lmx = 0; memcpy(&lmn, q + 0x1C, 2); memcpy(&lmx, q + 0x1E, 2);
            if (fl & 0x10) { sm.entVal[6] = lmn; sm.entVal2[6] = lmx; }
            if (fl & 0x80) { sm.entVal[15] = lmn; sm.entVal2[15] = lmx; }
            if (fl & 0x10000) { sm.entVal[7] = q[0x4C]; sm.entVal2[7] = q[0x4D]; }
            if (fl & 0x1000) sm.entVal[8] = 1;
            if (fl & 0x800) sm.entVal[9] = 1;
            if (fl & 0x80000) sm.entVal[13] = 1;
            if (fl & 0x20000) sm.entVal[11] = q[0x5C] >> 4;
            if ((fl & 0x40) && (sp & 0x10000000)) sm.entVal[14] = 1;
            if ((fl & 0x40) && (sp & 0x000F0000)) sm.entVal[10] = 1;
            if ((fl & 0x40) && (sp & 0x0FC00000)) sm.entVal[12] = 1;
            static int saidR = 0; static u32 wasF = 0xFFFFFFFF;
            if (fl != wasF && saidR < 10) { ++saidR; wasF = fl; windower::debug::log("NATIVEUI search : refine -- request flags %08X special %08X (area %d x%d, job %d, level %d-%d, name '%s')", fl, sp, sm.entVal[1], sm.entVal2[1], sm.entVal[3], sm.entVal[6], sm.entVal2[6], sm.entName); }
        }
    }
    if (rs == 1 && smObj && !strncmp(sm.name, "prty", 4) || (rs == 1 && smObj && !strcmp(sm.name, "alarm"))) {
        // party-re-2026-11-19.md : the toggles = the player flags P+0x2CFD8.. (seek bit 0 ; autogroup +1 bit 7 ; request +3
        // bit 5) ; Alarm Type : handler +0x14 = the chosen entry - 1 ; Languages (EN client) : handler +0x2C = the mask,
        // entry 1 English bit 1, 2 French bit 3, 3 German bit 2, 4 Japanese bit 0, 5 other bit 4
        u32 hd = 0; safe_read(smObj + OBJ_HANDLER, &hd);
        if (!strcmp(sm.name, "alarm")) { u32 v = 0; if (valid_ptr(hd) && safe_read(hd + 0x14, &v) && v < 8) { sm.entMarks = 1u << (v + 1); sm.entMarkKind = 2; } }
        else if (!strcmp(sm.name, "prty5us")) {
            u32 m = 0; static const int BIT[6] = { 0, 1, 3, 2, 0, 4 };
            if (valid_ptr(hd) && safe_read(hd + 0x2C, &m)) { for (int e = 1; e <= 5; ++e) if (m & (1u << BIT[e])) sm.entMarks |= 1u << e; sm.entMarkKind = 2; }
        } else if (faith_res()) {
            u32 P = 0; unsigned char f[4] = { 0, 0, 0, 0 };
            if (safe_read(g_fr.pVar, &P) && valid_ptr(P) && rdblk(P + 0x2CFD8, f, 4)) {
                const bool seek = (f[0] & 1) != 0, ag = (f[1] & 0x80) != 0, req = (f[3] & 0x20) != 0;
                if (!strcmp(sm.name, "prty1")) sm.entMarks = (seek ? 1u << 1 : 0) | (ag ? 1u << 3 : 0) | (req ? 1u << 5 : 0);
                else if (!strcmp(sm.name, "prty2")) sm.entMarks = req ? 1u << 6 : 0;
                else if (!strcmp(sm.name, "prty3") || !strcmp(sm.name, "prty7")) sm.entMarks = req ? 1u << 11 : 0;
                sm.entMarkKind = 1;
            }
        }
    }
    g_mainSub = sm; if (rs != 1) g_mainSub.open = false;
    {   // the first sight of each sub menu : its rows as read (bounded) -- what the labels are, measured not guessed
        static int said = 0;
        if (rs == 1 && said < 4) { ++said; char b[400]; int o = 0;
            for (int i = 0; i < sm.n && o < 360; ++i) o += _snprintf(b + o, sizeof(b) - o, "%s%d '%s'", i ? ", " : "", sm.entry[i], sm.label[i]);
            b[sizeof(b) - 1] = 0; windower::debug::log("NATIVEUI '%s' (from the main menu) : %d row(s) : %s", sm.name, sm.n, b); }
    }
    NativeEntryMenu so; const int ro = entry_read(MAIN_SORT_W, so, 0);
    g_mainSort = so; if (ro != 1) g_mainSort.open = false;
    NativeEntryMenu yn; memset(&yn, 0, sizeof(yn)); const int ry = g_mainSort.open ? entry_read(MAIN_YN_W, yn, 0) : -1;
    g_mainYn = yn; if (ry != 1) g_mainYn.open = false;
    if (rs == 0 || ro == 0 || ry == 0) r = 0;
    {   // Status -> Profile : the panel's rect, and its data (drawn by AioHUD only when every field reads)
        NativeProfile P; memset(&P, 0, sizeof(P));
        const u32 mgr = manager(); u32 node = 0; if (mgr) safe_read(mgr + MGR_OPEN_LIST, &node);
        for (int g = 0; valid_ptr(node) && g < 400 && !P.open; ++g) {
            u32 next = 0, o = 0, def = 0; unsigned char dead = 0, cl = 0; char nm[9];
            safe_read(node + NODE_NEXT, &next); safe_read(node + NODE_VAL, &o); rd8(node + 0x14, dead);
            if (!dead && valid_ptr(o) && safe_read(o + OBJ_DEF, &def) && valid_ptr(def) && rdname(def, nm) && !strcmp(nm, "profile")
                && rd8(o + OBJ_CLOSING, cl) && !cl && rd16(o + OBJ_RECT, P.rx0) && rd16(o + OBJ_RECT + 2, P.ry0) && rd16(o + OBJ_RECT + 4, P.rx1) && rd16(o + OBJ_RECT + 6, P.ry1)) {
                u32 wh = 0;
                if (safe_read(mgr + 0x80, &wh)) { P.uiW = (short)(wh & 0xFFFF); P.uiH = (short)(wh >> 16); }
                if (P.uiW >= 320 && P.uiH >= 240 && P.uiW <= 8000 && P.uiH <= 8000) {
                    if (P.ry0 >= P.uiH) { P.ry0 = (short)(P.ry0 - HIDE_DY); P.ry1 = (short)(P.ry1 - HIDE_DY); }
                    P.open = true;
                }
            }
            node = next;
        }
        { ProfileMem D0; g_profDataOk = read_profile_mem(D0); }
        {   // the JP mirror : valid (flags & 4) and plausible -> levmeri2's rule (before it opens)
            const u32 mer = fm_addr(FM_PW_MERIT); unsigned char fl = 0, b[0x94]; bool ok = false;
            if (mer && rd8(mer - 2, fl) && (fl & 4) && rdblk(mer + 0x0A, b, sizeof(b))) {
                ok = true;
                for (int j = 0; j < 24 && ok; ++j) { unsigned short cp = 0, jp = 0; memcpy(&cp, b + 4 + 6 * j, 2); memcpy(&jp, b + 6 + 6 * j, 2); if (cp > 30000 || jp > 500) ok = false; }
            }
            g_jpRule = ok;
        }
        { int fp = 0; g_faithRule = faith_points(fp); }
        {   // the stat panels
            PersonaMem D; g_persDataOk = read_persona_mem(D); g_persData = D;
            panel_read(mgr, "persona", g_persona); panel_read(mgr, "level", g_levelW);
            // Job / Master Levels : ours only while STATUS's list is up (the Mog House's job change reuses joblevel)
            panel_read(mgr, "joblevel", g_jobW); panel_read(mgr, "masterle", g_mlW);
            panel_read(mgr, "btlskill", g_btlW); panel_read(mgr, "mgcskill", g_mgcW);
            panel_read(mgr, "trdskill", g_trdW); panel_read(mgr, "levmerit", g_lmW);
            {   // Currencies 1 / 2 (evitem in its mode 1 / 2)
                u32 focus = 0; const u32 fp = aio::fm_addr(aio::FM_MENU_PTR); if (fp) safe_read(fp, &focus);
                cur_read(mgr, focus, g_cur);
                static int saidC = 0;
                if (g_cur.open && g_cur.loaded && saidC < 3) { ++saidC; windower::debug::log("NATIVEUI currencies %d : read -- AioHUD draws it (%d rows, top %d, cursor %d, first %d)", g_cur.mode, g_cur.n, g_cur.top, g_cur.cursor, g_cur.value[0]); }
            }
            {   // Unity Info (quest00 mode 1, under ut_menu)
                u32 focus = 0; const u32 fp = aio::fm_addr(aio::FM_MENU_PTR); if (fp) safe_read(fp, &focus);
                uinfo_read(mgr, focus, g_uinfo);
                urank_read(mgr, focus, g_uinfo, g_urank);
                // Job Points : under Status's list only (the Mog House has its own way there)
                if (!(inStatusNow() && jpts_read(mgr, focus, g_jpts))) { g_jpts.open = false; g_jpts.box.open = false; }
                if (!(g_jpts.open && jcat_read(mgr, focus, g_jcat))) g_jcat.open = false;
                // Alter Ego : under fe_main only
                const bool feUp = g_mainSub.open && !strcmp(g_mainSub.name, "fe_main");
                if (!(feUp && faith_read(mgr, focus, g_faith))) g_faith.open = false;
                if (!(feUp && cel_read(mgr, focus, g_cel))) g_cel.open = false;
                // Equipment (from the main menu) : the grid, the slot's item list
                if (!equip_read(mgr, focus, g_equip)) { g_equip.open = false; g_equip.lopen = false; }
                // Items (from the main menu) : the list ; the item cards' rule as for Equipment
                if (!syn_read(mgr, focus, g_syn)) g_syn.open = false;
                // the search comment (mode 2 only : the bazaar / linkshell messages stay the client's)
                if (!comment_read(mgr, focus, g_cmt) || g_cmt.cmMode != 2) g_cmt.open = false;
                static int saidCm = 0; static int wasCm = -1;
                if ((int)g_cmt.open * 4 + (int)g_cmt.typing * 2 != wasCm && saidCm < 12) { ++saidCm; wasCm = (int)g_cmt.open * 4 + (int)g_cmt.typing * 2;
                    windower::debug::log("NATIVEUI search comment : %s (mode %d, genre %02X, line %d, typing %d : %d bytes) line 0 '%.40s'", g_cmt.open ? "open -- AioHUD draws it" : "closed", g_cmt.cmMode, g_cmt.cmGenre, g_cmt.cmLine, (int)g_cmt.typing, g_cmt.cmLiveN, g_cmt.cmText[0]); }
                if (g_equip.open || !items_read(mgr, focus, g_items)) g_items.open = false;
                g_eqUp = g_equip.open || g_items.open || g_syn.open;
                static int saidSy = 0; static int wasSy = -1;
                { const int st = (int)g_syn.open + 2 * (int)g_syn.gridOpen + 4 * (int)g_syn.histOpen + 8 * g_syn.gridMode;
                  if (st != wasSy && saidSy < 16) { ++saidSy; wasSy = st;
                    windower::debug::log("NATIVEUI synthesis : screen %d, grid %d (mode %d, cursor %d, focus %d, crystal %u have %d, slot 0 id %u qty %u miss %d, synthesize %s), history %d (%d rows, cursor %d, row 0 item %u fav %d)",
                        (int)g_syn.open, (int)g_syn.gridOpen, g_syn.gridMode, g_syn.gridCursor, (int)g_syn.gridFocused, g_syn.crystalId, (int)g_syn.crystalHave,
                        g_syn.slotId[0], g_syn.slotQty[0], (int)g_syn.slotMiss[0], g_syn.synAllowed ? "allowed" : "refused", (int)g_syn.histOpen, g_syn.hN, g_syn.hCursor, g_syn.hItem[0], (int)g_syn.hFav[0]); } }
                static int saidIt = 0; static int wasI = 0;
                if ((int)g_items.open + 2 * (int)g_items.binMode != wasI && saidIt < 12) { ++saidIt; wasI = (int)g_items.open + 2 * (int)g_items.binMode;
                    windower::debug::log("NATIVEUI items : list %s%s (%d rows, %d temporary, top %d, cursor %d, focus %d, %d/%d ; row 0 id %u count %d colour %08X)",
                        g_items.open ? "read -- AioHUD draws it" : "gone", g_items.binMode ? " with the Recycling Bin" : "", g_items.itN, g_items.tempRows, g_items.itTop, g_items.itCursor, (int)g_items.focused,
                        g_items.invUsed, g_items.invCap, g_items.itRow[0].itemId, g_items.itRow[0].itemCount, g_items.itRow[0].itemCol); }
                { EquipSet es; g_eqRule = read_equipment(es); }                   // the equipment reads : equip born parked
                static int saidE = 0, saidL = 0;
                if (g_equip.open && saidE < 3) { ++saidE; windower::debug::log("NATIVEUI equipment : grid read (cursor %d, ids %s : main %u head %u, stats %s : atk %d def %d fire %d)",
                    g_equip.cursor, g_equip.idsOk ? "ok" : "UNREADABLE", g_equip.id[0], g_equip.id[4], g_equip.statsOk ? "ok" : "UNREADABLE", g_equip.atk, g_equip.def, g_equip.res[0]); }
                if (g_equip.lopen && saidL < 4) { ++saidL; windower::debug::log("NATIVEUI equipment : list read (%d rows, top %d, cursor %d, focus %d ; row 0 header %d id %u ; row 1 id %u count %d colour %08X)",
                    g_equip.ln, g_equip.ltop, g_equip.lcursor, (int)g_equip.lfocused, (int)g_equip.lrow[0].header, g_equip.lrow[0].id, g_equip.lrow[1].id, g_equip.lrow[1].count, g_equip.lrow[1].col); }
                // Merit Points -> Categories : under Status's list
                if (!(inStatusNow() && mcat_read(mgr, focus, g_mcat))) g_mcat.open = false;
                g_mcatRule = merit_list_ok();                                      // the list reads : meritinf born parked
                static int saidM2 = 0;
                if (g_mcat.open && saidM2 < 5) { ++saidM2; windower::debug::log("NATIVEUI merits : meritcat read (cat %d, %d rows, top %d, cursor %d ; row 0 : header %d id %d lvl %d cost %d cap %d colours %08X %08X ; left %d, combo cat %d : %d/%d, group 2 %d/%d)",
                    g_mcat.cat, g_mcat.n, g_mcat.top, g_mcat.cursor, (int)g_mcat.row[0].header, g_mcat.row[0].id, g_mcat.row[0].lvl, g_mcat.row[0].cost, g_mcat.row[0].cap, g_mcat.row[0].colLvl, g_mcat.row[0].colCost,
                    g_mcat.meritsLeft, g_mcat.comboCat, g_mcat.spent, g_mcat.cap, g_mcat.spent2, g_mcat.cap2); }
                static int saidC2 = 0;
                if (g_cel.open && saidC2 < 4) { ++saidC2; windower::debug::log("NATIVEUI celestite : type %d, may change %d, picked slot %d row %d ; equip %d rows (cursor %d, focus %d, row 0 : n %d badge %d) ; inven %d rows (cursor %d, focus %d, row 0 : n %d colour %08X)",
                    g_cel.type, (int)g_cel.mayChange, g_cel.picked, g_cel.pickedRow, g_cel.equip.n, g_cel.equip.cursor, (int)g_cel.equip.focused, g_cel.equip.cel[0], g_cel.equip.badge[0],
                    g_cel.inven.n, g_cel.inven.cursor, (int)g_cel.inven.focused, g_cel.inven.cel[0], g_cel.inven.col[0]); }
                static int saidF = 0;
                if (g_faith.open && saidF < 5) { ++saidF; windower::debug::log("NATIVEUI alter ego : %s read (%d rows, top %d, cursor %d ; row 0 : name %d id %d lvl %d max %d cost %d colours %08X %08X ; points %s %d)",
                    g_faith.level ? "fp_cat2" : "fp_cat", g_faith.n, g_faith.top, g_faith.cursor, g_faith.row[0].id, g_faith.rowId[0], g_faith.row[0].lvl, g_faith.row[0].max, g_faith.row[0].cost, g_faith.row[0].colLvl, g_faith.row[0].colCost, g_faith.pointsOk ? "ok" : "UNREADABLE", g_faith.points); }
                static int saidJ3 = 0;
                if (g_jcat.open && saidJ3 < 4) { ++saidJ3; windower::debug::log("NATIVEUI job points : jbpcat read (job %d, tab %d, %d rows, top %d, cursor %d ; row 0 : id %d lvl %d max %d cost %d gift %d thr %d colours %08X %08X)",
                    g_jcat.job, g_jcat.tab, g_jcat.n, g_jcat.top, g_jcat.cursor, g_jcat.row[0].id, g_jcat.row[0].lvl, g_jcat.row[0].max, g_jcat.row[0].cost, g_jcat.row[0].gift, g_jcat.row[0].thr, g_jcat.row[0].colLvl, g_jcat.row[0].colCost); }
                static int saidJ2 = 0;
                if (g_jpts.open && saidJ2 < 3) { ++saidJ2; windower::debug::log("NATIVEUI job points : merit2ca read (%d rows, top %d, cursor %d, first job %d, chosen %d, mirror %s : WAR cp %d jp %d spent %d)",
                    g_jpts.n, g_jpts.top, g_jpts.cursor, g_jpts.job[0], g_jpts.chosen, g_jpts.ok ? "ok" : "UNREADABLE", g_jpts.capPts[1], g_jpts.jobPts[1], g_jpts.jpSpent[1]); }
                // Unity Lists : under ut_menu only (the main menu's Search reuses these windows)
                const bool utUp = g_mainSub.open && !strcmp(g_mainSub.name, "ut_menu");
                // Party (prty* : Find Member / Member List / View Request = scresult ; Level Sync = levelsyn, the same records)
                const bool ptUp = g_mainSub.open && !strncmp(g_mainSub.name, "prty", 4);
                // Search (searchma, Refine Search scsibori and their pickers) : the results are theirs
                static const char* const SR_W[] = { "searchma", "scsibori", "searchjo", "searchna", "searchra", "searchle", "scchar", "scmlev", "comgenre", "sccompar", "sccomite", "sccomls", "scname", 0 };
                const bool srUp = g_mainSub.open && in_list(g_mainSub.name, SR_W);
                // Linkshell : the items (link5), its menus (link*) and the members (link12)
                if (!ls_read(mgr, focus, g_ls)) g_ls.open = false;
                const bool lsUp = g_ls.open || (g_mainSub.open && !strncmp(g_mainSub.name, "link", 4)) || win_named(mgr, "link12");
                if (!(utUp || srUp) || !area_read(mgr, focus, g_area)) g_area.open = false;
                if (utUp && search_read(mgr, focus, g_search)) g_search.ctx = 0;
                else if (ptUp && search_read(mgr, focus, g_search)) g_search.ctx = 1;
                else if (srUp && search_read(mgr, focus, g_search)) g_search.ctx = 3;
                else if (lsUp) {                                   // a linkshell's members : link12, its LS = handler +0x894
                    TextList T12; unsigned char slot = 0; unsigned id = 0, rgb = 0; char nm[24] = "";
                    if (tl_find(mgr, "link12", T12) && rd8(T12.h + 0x894, slot) && !ls_equipped(slot, id, rgb, nm, sizeof(nm))) {
                        static int saidLs = 0;                          // the members still show, without their shell's name / colour / ranks : say why
                        if (saidLs < 3) { ++saidLs; windower::debug::log("NATIVEUI link12 : the shell of slot %u not read (slots %s) -- members shown without its name, colour and ranks", slot, g_lsrOk ? "resolved" : "NOT resolved"); }
                    }
                    if (search_read(mgr, focus, g_search, "link12", id)) { g_search.ctx = 4; lstrcpynA(g_search.lsName, nm, sizeof(g_search.lsName)); g_search.lsRgb = rgb;
                        static unsigned saidSig = 0; unsigned sig = (unsigned)g_search.n * 131u + (unsigned)g_search.top;   // once per list / page : the raw race + rank of each member
                        for (int q = 0; q < g_search.vis && q < 16; ++q) sig = sig * 31u + g_search.row[q].race * 7u + g_search.row[q].lsRank;
                        static int saidPages = 0;                       // the first few pages only : a long scroll stays out of the log
                        if (sig != saidSig && saidPages < 6) { saidSig = sig; ++saidPages;
                            for (int q = 0; q < g_search.vis && q < 16 && g_search.top + q < g_search.n; ++q) if (g_search.row[q].type == 0)
                                windower::debug::log("NATIVEUI link12 member '%s' race %u rank %u job %u/%u", g_search.row[q].name, g_search.row[q].race, g_search.row[q].lsRank, g_search.row[q].mjob, g_search.row[q].sjob); } }
                    else g_search.open = false;
                }
                else if (ptUp && search_read(mgr, focus, g_search, "levelsyn")) g_search.ctx = 2;
                else g_search.open = false;
                { NativeEntryMenu so; if ((utUp || ptUp || srUp || lsUp) && entry_read(SCOPT_W, so, 0) == 1) g_scopt = so; else g_scopt.open = false; }
                static int saidPt = 0, wasPt = -1;
                if ((ptUp ? g_search.ctx * 2 + (int)g_search.open : -1) != wasPt && saidPt < 12) { ++saidPt; wasPt = ptUp ? g_search.ctx * 2 + (int)g_search.open : -1;
                    windower::debug::log("NATIVEUI party : submenu '%s' (marks %X kind %d), list %s (%d rows, cursor %d)", ptUp ? g_mainSub.name : "-", g_mainSub.entMarks, g_mainSub.entMarkKind,
                        !g_search.open ? "none" : g_search.ctx == 2 ? "levelsyn" : "scresult", g_search.n, g_search.cursor); }
                static int saidA = 0, saidS = 0;
                if (g_area.open && saidA < 3) { ++saidA; windower::debug::log("NATIVEUI unity lists : arealist read (mode %d, %d rows, top %d, visible %d, cursor %d)", g_area.mode, g_area.n, g_area.top, g_area.vis, g_area.cursor); }
                if (g_search.open && saidS < 6) { ++saidS; windower::debug::log("NATIVEUI unity lists : scresult read (%d rows, top %d, cursor %d, %s %d, first '%s' job %d/%d lv %d zone %d flags %08X)",
                    g_search.n, g_search.top, g_search.cursor, g_search.counted ? "total" : "searching", g_search.total, g_search.row[1].name, g_search.row[1].mjob, g_search.row[1].sjob, g_search.row[1].mlvl, g_search.row[1].zone, g_search.row[1].flags); }
                static int saidI = 0;
                if (g_uinfo.open && saidI < 3) { ++saidI; windower::debug::log("NATIVEUI unity info : read -- AioHUD draws it (%s, unity %d, ranks %d / %d, pts %d / %d, personal %d / %d, chat %d, cursor %d)",
                    g_uinfo.loading ? "loading" : "loaded", g_uinfo.unity, g_uinfo.rank[0], g_uinfo.rank[1], g_uinfo.pts[0], g_uinfo.pts[1], g_uinfo.personal, g_uinfo.partial, g_uinfo.chat, g_uinfo.rows.cursor); }
            }
            {   // Unity's points box : the 0x061 mirror's Unity word (FM_PW_BLOCK + 0x48)
                panel_read(mgr, "ut_point", g_utW);
                NativeUnity U; memset(&U, 0, sizeof(U));
                const u32 blk = fm_addr(FM_PW_BLOCK); u32 v = 0;
                if (blk && safe_read(blk + 0x48, &v)) {
                    U.unity = (int)(v & 0x1F); U.rank = (int)((v >> 5) & 0x1F); U.points = (int)((v >> 10) & 0x1FFFF); U.maxPoints = 99999;
                    U.ok = U.unity >= 1 && U.unity <= 11 && U.points <= 99999;     // in a Unity, a plausible count
                }
                g_unity = U; g_unityOk = U.ok;
                static int saidU = 0;
                if (g_utW.open && saidU < 3) { ++saidU; windower::debug::log("NATIVEUI unity points : %s (word %08X : unity %d rank %d points %d)", U.ok ? "read -- AioHUD draws it" : "data unreadable -- the client's own stays", v, U.unity, U.rank, U.points); }
            }
            {   // Merit Points' panel : AioHUD's own PointWatch read of the merit mirror (FM_PW_MERIT)
                PwMem pw; read_pointwatch(pw);
                NativeMerits M; memset(&M, 0, sizeof(M));
                if (pw.merOk) { M.ok = true; M.lp = (int)pw.lpCur; M.merits = pw.merits; M.maxMerits = pw.maxMerits; }
                g_merits = M; g_meritsOk = M.ok;
                static int saidM = 0;
                if (g_lmW.open && saidM < 3) { ++saidM; windower::debug::log("NATIVEUI merit panel : %s (lp %d merits %d/%d)", M.ok ? "read -- AioHUD draws it" : "data unreadable -- the client's own stays", M.lp, M.merits, M.maxMerits); }
            }
            { NativeSkills K; g_skillsOk = skills_read(K); g_skills = K; g_skillsOkRule = g_skillsOk; }
            static int saidK = 0;
            if ((g_btlW.open || g_mgcW.open || g_trdW.open) && saidK < 3) { ++saidK; windower::debug::log("NATIVEUI skills panel : %s (sword %04X evasion %04X divine %04X)", g_skillsOk ? "read -- AioHUD draws it" : "data unreadable -- the client's own stays", g_skills.v[3], g_skills.v[29], g_skills.v[32]); }
            const bool inStatus = g_mainSub.open && !strcmp(g_mainSub.name, "statcom2");
            NativeJobs J; memset(&J, 0, sizeof(J));
            if ((g_jobW.open || g_mlW.open) && inStatus) jobs_read(J);
            g_jobs = J; g_jobsOurs = inStatus && J.ok;
            static int saidJ = 0;
            if ((g_jobW.open || g_mlW.open) && saidJ < 3) { ++saidJ; windower::debug::log("NATIVEUI job levels panel : %s (status %d, main %d sub %d, mastered %08X)", g_jobsOurs ? "read -- AioHUD draws it" : "the client's own", (int)inStatus, J.mjob, J.sjob, J.mastered); }
            static int said = 0;
            if ((g_persona.open || g_levelW.open) && said < 3) { ++said; windower::debug::log("NATIVEUI stat panels : %s (job %d/%d sub %d/%d hp %u mp %u ml %d)", D.ok ? "read -- AioHUD draws them" : "data unreadable -- the client's own stay", D.mjob, D.mlvl, D.sjob, D.slvl, D.maxHp, D.maxMp, D.ml); }
        }
        if (P.open) {
            ProfileMem D;
            if (read_profile_mem(D)) { P.ok = true; P.nation = D.nation; P.race = D.race; P.home = D.home; P.rank = D.rank; P.rankPts = D.rankPts; P.mastery = D.mastery; P.title = D.title; }
            mon_read(P);
            static int said = 0;
            if (said < 3) { ++said; windower::debug::log("NATIVEUI 'profile' : %s (nation %d race %d home %d rank %d pts %d mastery %d)", P.ok ? "read -- AioHUD draws it" : "data unreadable -- the client's own stays", P.nation, P.race, P.home, P.rank, P.rankPts, P.mastery); }
        }
        g_prof = P;
    }                        // a parked submenu that cannot be read : the client's own back
    if (r == 0) {
        g_mainM.open = false; g_mainGoodSince = 0;
        if (++g_mainBad == 3 && !g_mainFail) { g_mainFail = true; windower::debug::log("NATIVEUI main menu : rows unreadable -- the client's own menu is shown"); }
        return;
    }
    g_mainBad = 0;
    if (r < 0) { g_mainM.open = false; return; }
    g_mainM = m;
    if (g_mainFail) {
        const DWORD now = GetTickCount();
        if (!g_mainGoodSince) g_mainGoodSince = now;
        else if (now - g_mainGoodSince >= 5000) { g_mainFail = false; windower::debug::log("NATIVEUI main menu : rows read again -- AioHUD's menu back"); }
    }
}

void act_poll(bool on) {
    if (!on) { g_act.open = false; g_sub.open = false; g_list.open = false; g_actBad = 0; g_actFail = false; return; }
    NativeMenu m; int r = act_read(m);
    NativeEntryMenu sm; u32 subObj = 0; const int rs = sub_read(sm, &subObj);
    g_sub = sm; if (rs != 1) g_sub.open = false;
    if (rs == 1 && !strcmp(sm.name, "chatctrl")) chat_info_read(subObj); else g_chat.ok = false;
    NativeList lst; int rl = list_read(lst);
    if (rl < 0 && r == 1) { u32 focus = 0; const u32 fp = aio::fm_addr(aio::FM_MENU_PTR); if (fp) safe_read(fp, &focus); rl = inv_read(focus, lst); }   // Items : the inventory list
    g_list = lst; if (rl != 1) g_list.open = false;
    if (rl == 0) r = 0;                                   // a parked list that cannot be read : the client's own back
    if (rs == 0) r = 0;                                   // a parked submenu that cannot be read : same fallback
    const DWORD now = GetTickCount();
    if (r == 0) {
        g_act.open = false;
        if (++g_actBad == 3 && !g_actFail) { g_actFail = true; windower::debug::log("NATIVEUI action menu : rows unreadable -- the client's own menu is shown"); }
        g_actGoodSince = 0;
        return;
    }
    g_actBad = 0;
    if (r == 1) {
        g_act = m; g_actObj = 0;
        if (g_actFail) {
            if (!g_actGoodSince) g_actGoodSince = now;
            else if (now - g_actGoodSince >= 5000) { g_actFail = false; windower::debug::log("NATIVEUI action menu : rows read again -- AioHUD's menu back"); }
        }
    } else g_act.open = false;
}
} // namespace

// The client places a SUBMENU beside its parent (Chat -> chatctrl, Magic -> magselec / magic, Abilities -> abiselec /
// ability / mount...) : with playermo parked HIDE_DY below the screen, its submenus are born parked too (2026-11-16 :
// "on n'a pas les sous-menus"). Any OPEN window we did not hide that sits that far below the UI screen is brought
// back by -HIDE_DY through the client's setter -- whatever path placed it. Logged (bounded), the success path too.
int g_rescueLog = 0;
// AioHUD's NPC box this frame (native_npc_box) : the windows that open with the answers go beside it
float g_npcBoxR[4] = { 0, 0, 0, 0 }; DWORD g_npcBoxMs = 0; bool g_npcCard = false;
// The Items list's own window : `inventor` is placed by its CODE at every open (the inventory has 17 placement modes,
// section 3), never parked with the action menu -- so it popped over AioHUD's list (2026-11-17, "la box items du jeu
// pop toujours"). While AioHUD draws the list (g_list kind 3 : only when it came from the action menu), it is moved
// HIDE_DY down through the client's setter ; once AioHUD stops (closed, unreadable), rescue_children (invOurs false)
// brings it back like any window parked under its parent. Logged (bounded), the success path too.
// 2026-11-17 ("elle pop moins d'une seconde") : parked as soon as it shows WHILE the action menu is up -- not after the
// list's first good read -- and the frames between its first sight and the park are logged, to measure what is left.
// Only for ITEMS : the action menu open with its cursor on Items (code 9) -- Trade (19) opens the inventory too, and that
// one AioHUD does not draw.
bool act_on_items() { return g_act.open && g_act.cursor >= 1 && g_act.cursor <= g_act.n && g_act.code[g_act.cursor - 1] == 9; }
int g_invParkLog = 0; u32 g_invSeenObj = 0; int g_invSeenFrames = 0;
void inv_park(u32 mgr) {
    if (!g_setter || !(act_on_items() || (g_list.open && g_list.kind == 3))) { g_invSeenObj = 0; return; }
    u32 wh = 0; if (!safe_read(mgr + 0x80, &wh)) return;
    const int uiH = (short)(wh >> 16); if (uiH < 240 || uiH > 8000) return;
    u32 node = 0; safe_read(mgr + MGR_OPEN_LIST, &node);
    for (int g = 0; valid_ptr(node) && g < 400; ++g) {
        u32 next = 0, obj = 0, def = 0; unsigned char cl = 0; short r[4] = { 0, 0, 0, 0 }; char nm[9];
        safe_read(node + NODE_NEXT, &next); safe_read(node + NODE_VAL, &obj);
        if (valid_ptr(obj) && safe_read(obj + OBJ_DEF, &def) && valid_ptr(def) && rdname(def, nm) && !strcmp(nm, "inventor") && rd8(obj + OBJ_CLOSING, cl) && !cl
            && rd16(obj + OBJ_RECT, r[0]) && rd16(obj + OBJ_RECT + 2, r[1]) && rd16(obj + OBJ_RECT + 4, r[2]) && rd16(obj + OBJ_RECT + 6, r[3])) {
            const int w = r[2] - r[0], h = r[3] - r[1];
            if (obj != g_invSeenObj) { g_invSeenObj = obj; g_invSeenFrames = 0; } else ++g_invSeenFrames;
            if (r[1] < uiH + HIDE_DY / 2 && w > 0 && h > 0 && w < 4000 && h < 4000) {
                bool ok = true;
                __try { ((SetRectFn)g_setter)(obj, r[0], r[1] + HIDE_DY, w, h, 1, 0, 0); }
                __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
                if (ok) refresh_handler(obj, nm);
                if (g_invParkLog < 20) { ++g_invParkLog; windower::debug::log("NATIVEUI 'inventor' (Items, from the action menu) : %s (y %d, %d frame(s) after it showed)", ok ? "parked -- AioHUD draws the list" : "setter faulted", r[1], g_invSeenFrames); }
            }
            return;
        }
        node = next;
    }
    g_invSeenObj = 0;
}
// Auto-sort's confirmation `sortyn` : placed by its CODE (never born under a parked parent -- it showed at once, 2026-11-18
// "auto sort ouvre un autre menu ... Yes No"). Parked while the spell list's sort options are AioHUD's ; anywhere else
// (the inventory, the Mog House) it stays the client's. Once they close, rescue_children brings a parked one back.
int g_ynParkLog = 0, g_profParkLog = 0;
void park_named(u32 mgr, const char* want, int& logN, const char* what);
bool g_parkClosing = false;   // park_named also takes a CLOSING instance (Region Info : the panel the client fades out on a hover)
// Region Info : the map is up (cnqframe) ; its mode = the always-shown panel open ; the client's own panels AioHUD redraws
const char* const RG_PTS[4] = { "conq3pts", "imperial", "allied", "colopoin" };      // the points (one per mode)
const char* const RG_TOP[4] = { "conq6sta", "beseige1", "campresu", 0 };             // the overview (Colonization : none)
// The client's copies (region-info-re-2026-11-19.md s.9 : the anchors, unique in two client builds ; their operands are
// the live addresses). G = the 0x05E copy from pkt+4 (0xB0 B) + its valid byte ; the campaign block's pointer ; the
// colonization points. Resolved once (bounded retry, logged) ; read only while the map is up, SEH-guarded.
namespace {
u32 g_rgG = 0, g_rgGValid = 0, g_rgCampPtr = 0, g_rgColoPts = 0; int g_rgTries = 0; DWORD g_rgNext = 0; bool g_rgResolved = false;
void region_resolve() {
    if (g_rgResolved || g_rgTries >= 5 || (g_rgTries && (int)(GetTickCount() - g_rgNext) < 0)) return;
    ++g_rgTries; g_rgNext = GetTickCount() + 5000;
    const u32 a = aob_masked("56 8B 74 24 08 57 B9 2C 00 00 00 BF ?? ?? ?? ?? F3 A5 E8 ?? ?? ?? ?? 8B 0D ?? ?? ?? ?? 6A 02 6A 00 A3 ?? ?? ?? ?? C6 05 ?? ?? ?? ?? 01");
    const u32 b = aob_masked("A1 ?? ?? ?? ?? 85 C0 75 1C 6A 02 6A 06 6A 09");
    const u32 c = aob_masked("33 C9 33 C0 A3 ?? ?? ?? ?? 89 0D ?? ?? ?? ?? 66 A3 ?? ?? ?? ?? B8 ?? ?? ?? ?? BA 03 00 00 00 88 48 FF 88 08 88 48 01");
    if (a) { safe_read(a + 0x0C, &g_rgG); safe_read(a + 0x28, &g_rgGValid); }
    if (b) safe_read(b + 1, &g_rgCampPtr);
    if (c) safe_read(c + 0x0B, &g_rgColoPts);
    g_rgResolved = valid_ptr(g_rgG) && valid_ptr(g_rgGValid) && valid_ptr(g_rgCampPtr) && valid_ptr(g_rgColoPts);
    windower::debug::log("NATIVEUI region copies %s (try %d) : conquest G %08X valid %08X, campaign ptr %08X, colonization points %08X",
        g_rgResolved ? "resolved" : "NOT resolved", g_rgTries, g_rgG, g_rgGValid, g_rgCampPtr, g_rgColoPts);
    if (!g_rgResolved && g_rgTries >= 5)   // a pattern miss is the client's code, not a timing : stop, and say what that costs
        windower::debug::log("NATIVEUI region copies : giving up after %d tries -- Region Info waits for the server's packets (no client copy)", g_rgTries);
}
// fill what no packet has given us from the client's copies (a packet received this session always wins)
void region_from_client() {
    region_resolve(); if (!g_rgResolved) return;
    static bool said[3] = {};
    unsigned char v = 0;
    if (!g_rg.conqOk && rd8(g_rgGValid, v) && v == 1) {
        unsigned char t[0xB0];
        if (rdblk(g_rgG, t, sizeof(t))) { memset(g_rg.conq, 0, sizeof(g_rg.conq)); memcpy(g_rg.conq + 4, t, sizeof(t)); g_rg.conqOk = true;
            if (!said[0]) { said[0] = true; windower::debug::log("NATIVEUI region : conquest / besieged taken from the client's copy (no 0x05E since the load)"); } }
    }
    u32 cp = 0;
    if (!g_rg.campOk && safe_read(g_rgCampPtr, &cp) && valid_ptr(cp)) {
        u32 notes = 0, cnt = 0;
        // the pointer IS the valid flag : zeroed at each request, set by the callback once the second half of the 0x071
        // type 2 has been copied (note s.2a / 2c) -- a non-null pointer means real data, zeros included
        if (safe_read(cp, &notes) && safe_read(cp + 4, &cnt)) { memset(g_rg.camp, 0, sizeof(g_rg.camp)); memcpy(g_rg.camp + 0x0C, &notes, 4); memcpy(g_rg.camp + 0x10, &cnt, 4); g_rg.campOk = true;
            for (int a = 0; a < 26; ++a) if (rdblk(cp + 0x24 + (u32)a * 12, g_rg.campArea[a], 12)) g_rg.campAreaOk[a] = true;   // CP+0x24 : the area records
            if (!said[1]) { said[1] = true; windower::debug::log("NATIVEUI region : campaign taken from the client's copy"); } }
    }
    u32 pts = 0;   // no valid flag : zero until the client's first answer -- a zero is "not yet", not "0 bayld"
    if (!g_rg.coloOk && safe_read(g_rgColoPts, &pts) && pts) {
        memset(g_rg.colo, 0, sizeof(g_rg.colo)); memcpy(g_rg.colo + 0x38, &pts, 4); g_rg.coloOk = true;
        if (!said[2]) { said[2] = true; windower::debug::log("NATIVEUI region : colonization taken from the client's copy"); }
    }
}
}   // namespace
void region_poll(u32 mgr) {
    const bool was = g_rg.open; const int wasMode = g_rg.mode;
    g_rg.open = win_named(mgr, "cnqframe") != 0;    g_rg.mode = -1;
    if (g_rg.open) for (int m = 0; m < 4; ++m) if (win_named(mgr, RG_PTS[m])) { g_rg.mode = m; break; }
    g_rg.hover = -1; g_rg.home = -1;
    if (const u32 o = g_rg.open ? win_named(mgr, "cnqframe") : 0) {          // the map's handler (its back-pointer checked)
        u32 h = 0, back = 0; short hv = -1, hm = -1, cx = 0, cy = 0;
        if (safe_read(o + OBJ_HANDLER, &h) && valid_ptr(h) && safe_read(h + 0x08, &back) && back == o && rd16(h + 0x16C, hv) && rd16(h + 0x170, hm)) { g_rg.hover = hv; g_rg.home = hm; if (rd16(h + 0x1C, cx) && rd16(h + 0x1E, cy)) { g_rg.curX = cx; g_rg.curY = cy; } }
    }
    u32 wh = 0; if (safe_read(mgr + 0x80, &wh)) { g_rg.uiW = (short)(wh & 0xFFFF); g_rg.uiH = (short)(wh >> 16); }
    if (g_rg.open) region_from_client();
    g_rg.stronghold = -1;                                                       // Besieged : the stronghold detail the client shows
    if (const u32 o = (g_rg.open && g_rg.mode == 1) ? win_named(mgr, "beseige2") : 0) {
        u32 h = 0; unsigned char v = 0xFF;
        if (safe_read(o + OBJ_HANDLER, &h) && valid_ptr(h) && rd8(h + 0x3C, v) && v <= 2) g_rg.stronghold = v;
    }
    if (g_rg.open != was || g_rg.mode != wasMode)
        windower::debug::log("NATIVEUI region : map %s, mode %d (data : conquest %d, campaign %d, colonization %d)", g_rg.open ? "up" : "closed", g_rg.mode, g_rg.conqOk, g_rg.campOk, g_rg.coloOk);
}
void yn_park(u32 mgr) {
    region_poll(mgr);
    static int rgLog[8] = {};
    if (g_rg.open && g_rg.mode >= 0) {
        g_parkClosing = true;   // a hover closes the overview and opens a region's detail (and back) : the client FADES the closing one out, on screen
        park_named(mgr, RG_PTS[g_rg.mode], rgLog[g_rg.mode], "Region Info -> the points");
        if (RG_TOP[g_rg.mode]) park_named(mgr, RG_TOP[g_rg.mode], rgLog[4 + g_rg.mode], "Region Info -> the overview");
        static int c1Log = 0;
        if (g_rg.mode == 0) park_named(mgr, "conquer1", c1Log, "Region Info -> a region's detail");
        static int dLog[3] = {};
        if (g_rg.mode == 1) park_named(mgr, "beseige2", dLog[0], "Region Info -> a stronghold's detail");
        if (g_rg.mode == 2) park_named(mgr, "camparea", dLog[1], "Region Info -> an area's detail");
        if (g_rg.mode == 3) park_named(mgr, "colorank", dLog[2], "Region Info -> the coalition ranks");
        g_parkClosing = false;
    }
    if (g_mainSort.open) park_named(mgr, "sortyn", g_ynParkLog, "Auto-sort / Dispose, from the spell list or the items");
    if (g_prof.open && g_prof.ok) park_named(mgr, "profile", g_profParkLog, "Status -> Profile");   // placed by code too
    // the stat panels are BORN parked by their record ; a late one (opened before the record took) is moved too
    static int persLog = 0, lvlLog = 0;
    if (g_persDataOk && g_persona.open) park_named(mgr, "persona", persLog, "Status -> stats");
    if (g_persDataOk && g_levelW.open) park_named(mgr, "level", lvlLog, "Status -> Master Level");
    static int jlLog = 0, mlLog = 0;                     // placed by code (joblevel through move-to) : moved once shown
    if (g_jobsOurs && g_jobW.open) park_named(mgr, "joblevel", jlLog, "Status -> Job Levels");
    if (g_jobsOurs && g_mlW.open) park_named(mgr, "masterle", mlLog, "Status -> Master Levels");
    static int bsLog = 0, msLog = 0;                     // placed by their record : born parked (the name rule) ; a late one moved
    if (g_skillsOk && g_btlW.open) park_named(mgr, "btlskill", bsLog, "Status -> Combat Skills");
    if (g_skillsOk && g_mgcW.open) park_named(mgr, "mgcskill", msLog, "Status -> Magic Skills");
    static int tsLog = 0, lmLog = 0;
    if (g_skillsOk && g_trdW.open) park_named(mgr, "trdskill", tsLog, "Status -> Crafts");
    if (g_meritsOk && g_lmW.open) park_named(mgr, "levmerit", lmLog, "Status -> Merit Points' panel");
    static int utLog = 0, uiLog = 0;
    if (g_uinfo.open) park_named(mgr, "quest00", uiLog, "Status -> Unity Info / Ranking Info");
    static int jpLog = 0, j2Log = 0, jcLog = 0, bhLog = 0;
    if (g_jcat.open && g_jpts.ok) park_named(mgr, "jbpcat", jcLog, "Status -> Job Points' categories / gifts");
    static int fcLog = 0, f2Log = 0, fhLog = 0, ceLog = 0, ciLog = 0, cbLog = 0, mcLog = 0, mhLog = 0, eiLog = 0, ecLog = 0;
    if (g_equip.open && g_equip.lopen) park_named(mgr, "inventor", eiLog, "Equipment -> a slot's items");
    if (g_equip.open) park_named(mgr, "iteminfo", ecLog, "Equipment -> the item card");
    static int itLog = 0, icLog = 0;
    if (g_items.open) { park_named(mgr, "inventor", itLog, "Items -> the list"); park_named(mgr, "iteminfo", icLog, "Items -> the item card"); }
    static int bnLog = 0; if (g_items.open && g_items.binMode) park_named(mgr, "bank", bnLog, "Items -> the Recycling Bin");
    static int qtLog = 0; if (g_items.open && (g_items.binMode || g_items.synMode) && g_items.qtyOk) park_named(mgr, "itemctrl", qtLog, "Items / Synthesis -> the quantity");
    static int tsLog2 = 0, hlLog = 0, siLog = 0;
    if (g_syn.open && g_syn.gridOpen) park_named(mgr, "tskill1", tsLog2, "Synthesis -> the grid");
    if (g_syn.open && g_syn.histOpen) park_named(mgr, "cmbhlst", hlLog, "Synthesis -> History");
    if (g_syn.open) park_named(mgr, "iteminfo", siLog, "Synthesis -> the item card");
    static int cmLog = 0; if (g_cmt.open) park_named(mgr, "comment", cmLog, "Search -> Edit your comment");
    static int gmLog = 0; if (g_equip.open && g_equip.gilOk) park_named(mgr, "money", gmLog, "Equipment -> Current Gil");
    static int gxLog = 0; if (g_equip.open && g_equip.compare) park_named(mgr, "itemxinf", gxLog, "Equipment -> the compare card");
    if (g_mcat.open) { park_named(mgr, "meritcat", mcLog, "Merit Points -> Categories"); park_named(mgr, "bluehelp", mhLog, "Merit Points -> description"); }
    if (g_cel.open) { park_named(mgr, "fe_equip", ceLog, "Alter Ego -> Celestite slots"); park_named(mgr, "fe_inven", ciLog, "Alter Ego -> Celestite owned");
                      park_named(mgr, "bluehelp", cbLog, "Alter Ego -> Celestite description"); }
    if (g_faith.open && g_faith.level == 0) park_named(mgr, "fp_cat", fcLog, "Status -> Alter Ego's categories");
    if (g_faith.open && g_faith.level == 1) park_named(mgr, "fp_cat2", f2Log, "Status -> Alter Ego's abilities");
    if (g_faith.open) park_named(mgr, "bluehelp", fhLog, "Status -> Alter Ego's description");
    if (g_jpts.open && g_jpts.ok) park_named(mgr, "bluehelp", bhLog, "Status -> Job Points' description");
    if (g_jpts.open && g_jpts.ok) park_named(mgr, "merit2ca", jpLog, "Status -> Job Points");
    if (g_jpts.open && g_jpts.ok && g_jpts.box.open) park_named(mgr, "levmeri2", j2Log, "Status -> Job Points' CP / JP");
    static int alLog = 0, srLog = 0, soLog = 0;
    if (g_area.open) park_named(mgr, "arealist", alLog, "Status -> Unity -> Unity Lists");
    if (g_search.open) park_named(mgr, g_search.ctx == 2 ? "levelsyn" : g_search.ctx == 4 ? "link12" : "scresult", srLog, "Unity Lists / Party / Search / Linkshell -> the list");
    static int l5Log = 0, l6Log = 0;
    if (g_ls.open) park_named(mgr, "link5", l5Log, "Linkshell -> the items");
    if (g_ls.open || (g_search.open && g_search.ctx == 4)) park_named(mgr, "link6", l6Log, "Linkshell -> the header");
    if (g_scopt.open) park_named(mgr, "scoption", soLog, "Status -> Unity -> a player's options");
    static int urLog = 0;
    if (g_urank.open) park_named(mgr, "quest01", urLog, "Status -> Unity -> Ranking Results");
    if (g_unityOk && g_utW.open) park_named(mgr, "ut_point", utLog, "Status -> Unity Points");
    static int evLog = 0;                                // evitem : ONLY in its Currencies modes (mode 0 = an NPC's item pick)
    if (g_cur.open) park_named(mgr, "evitem", evLog, "Status -> Currencies");
}
// a window the client places by CODE (never born under a parked parent), moved HIDE_DY down through its setter, logged
void park_named(u32 mgr, const char* want, int& logN, const char* what) {
    if (!g_setter) return;
    u32 wh = 0; if (!safe_read(mgr + 0x80, &wh)) return;
    const int uiH = (short)(wh >> 16); if (uiH < 240 || uiH > 8000) return;
    u32 node = 0; safe_read(mgr + MGR_OPEN_LIST, &node);
    for (int g = 0; valid_ptr(node) && g < 400; ++g) {
        u32 next = 0, obj = 0, def = 0; unsigned char cl = 0, dead = 0; short r[4] = { 0, 0, 0, 0 }; char nm[9];
        safe_read(node + NODE_NEXT, &next); safe_read(node + NODE_VAL, &obj); rd8(node + 0x14, dead);
        if (dead) { node = next; continue; }   // a closed instance still listed (Region Info's panels : the live one comes later)
        if (valid_ptr(obj) && safe_read(obj + OBJ_DEF, &def) && valid_ptr(def) && rdname(def, nm) && !strcmp(nm, want) && rd8(obj + OBJ_CLOSING, cl) && (!cl || g_parkClosing)
            && rd16(obj + OBJ_RECT, r[0]) && rd16(obj + OBJ_RECT + 2, r[1]) && rd16(obj + OBJ_RECT + 4, r[2]) && rd16(obj + OBJ_RECT + 6, r[3])) {
            const int w = r[2] - r[0], h = r[3] - r[1];
            if (r[1] < uiH + HIDE_DY / 2 && w > 0 && h > 0 && w < 4000 && h < 4000) {
                bool ok = true;
                __try { ((SetRectFn)g_setter)(obj, r[0], r[1] + HIDE_DY, w, h, 1, 0, 0); }
                __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
                if (ok) refresh_handler(obj, nm);
                if (logN < 20) { ++logN; windower::debug::log("NATIVEUI '%s' (%s) : %s (y %d)", want, what, ok ? "parked -- AioHUD draws it" : "setter faulted", r[1]); }
            }
            if (!g_parkClosing) return;   // closing instances : park every one of that name (the fading one AND the new one)
        }
        node = next;
    }
}
void rescue_children(u32 mgr, int hide) {
    if (!g_setter) return;
    u32 wh = 0; if (!safe_read(mgr + 0x80, &wh)) return;
    const int uiW = (short)(wh & 0xFFFF), uiH = (short)(wh >> 16); if (uiH < 240 || uiH > 8000 || uiW < 320 || uiW > 8000) return;
    const bool besideNpc = g_npc.query && g_npcBoxR[2] > 0.0f && GetTickCount() - g_npcBoxMs < 500u;
    u32 node = 0; safe_read(mgr + MGR_OPEN_LIST, &node);
    for (int g = 0; valid_ptr(node) && g < 400; ++g) {
        u32 next = 0, obj = 0, def = 0; safe_read(node + NODE_NEXT, &next); safe_read(node + NODE_VAL, &obj);
        unsigned char cl = 0; short r[4] = {0, 0, 0, 0}; char nm[9];
        if (valid_ptr(obj) && safe_read(obj + OBJ_DEF, &def) && valid_ptr(def) && rdname(def, nm) && rd8(obj + OBJ_CLOSING, cl) && !cl
            && rd16(obj + OBJ_RECT, r[0]) && rd16(obj + OBJ_RECT + 2, r[1]) && rd16(obj + OBJ_RECT + 4, r[2]) && rd16(obj + OBJ_RECT + 6, r[3])
            && r[1] >= uiH + HIDE_DY / 2) {
            int dx = 0, dy = 0; desired_offset(nm, 0, 0, 0, hide, 0, 0, dx, dy);
            // parked by US for ANY reason (an option, the chat's input line "in the box" -- dock_offset) : leave it. The
            // record table is the truth : desired_offset alone missed the input line, which this then un-parked at
            // every open (2026-11-16, "le input du jeu, on veut pas le voir" ; CHATIN trace : inline at y1188, read OK)
            const int ek = find_entry_by_obj(obj);
            const bool ours = dy == HIDE_DY || (ek >= 0 && g_e[ek].ay >= HIDE_DY / 2);
            const int w = r[2] - r[0], h = r[3] - r[1];
            // AioHUD draws the item card itself : the client's description stays where its parked parent put it
            const bool cardOurs = besideNpc && g_npcCard && !strcmp(nm, "iteminfo");
            // AioHUD draws the auto-translate picker itself (above its input line) : the client's stays parked
            const bool fepOurs = g_fep.open && GetTickCount() - g_fepDrawnMs < 500u && !strcmp(nm, "fep");
            // AioHUD draws the ITEMS list (and its item card) opened from the action menu : the client's stay parked
            const bool invOurs = (act_on_items() || (g_list.open && g_list.kind == 3)) && (!strcmp(nm, "inventor") || !strcmp(nm, "iteminfo"));
            // AioHUD draws Auto-sort's Yes / No while the spell list's sort options are up
            const bool ynOurs = (g_rg.open && g_rg.mode >= 0 && (!strcmp(nm, RG_PTS[g_rg.mode]) || (RG_TOP[g_rg.mode] && !strcmp(nm, RG_TOP[g_rg.mode])) || (g_rg.mode == 0 && !strcmp(nm, "conquer1")) || (g_rg.mode == 1 && !strcmp(nm, "beseige2"))
                                 || (g_rg.mode == 2 && !strcmp(nm, "camparea")) || (g_rg.mode == 3 && !strcmp(nm, "colorank"))))
                             || (g_mainSort.open && !strcmp(nm, "sortyn")) || (g_prof.open && g_prof.ok && !strcmp(nm, "profile"))
                             || (g_persDataOk && (!strcmp(nm, "persona") || !strcmp(nm, "level")))
                             || (g_jobsOurs && (!strcmp(nm, "joblevel") || !strcmp(nm, "masterle")))
                             || (g_skillsOk && (!strcmp(nm, "btlskill") || !strcmp(nm, "mgcskill") || !strcmp(nm, "trdskill")))
                             || (g_meritsOk && !strcmp(nm, "levmerit"))
                             || (g_unityOk && !strcmp(nm, "ut_point"))
                             || (g_uinfo.open && !strcmp(nm, "quest00"))
                             || (g_urank.open && !strcmp(nm, "quest01"))
                             || (g_jpts.open && g_jpts.ok && (!strcmp(nm, "merit2ca") || !strcmp(nm, "levmeri2") || !strcmp(nm, "bluehelp") || !strcmp(nm, "jbpcat")))
                             || (g_items.open && (!strcmp(nm, "inventor") || !strcmp(nm, "iteminfo") || (g_items.binMode && !strcmp(nm, "bank")) || ((g_items.binMode || g_items.synMode) && g_items.qtyOk && !strcmp(nm, "itemctrl"))))
                             || (g_cmt.open && !strcmp(nm, "comment"))
                             || (g_syn.open && ((g_syn.gridOpen && !strcmp(nm, "tskill1")) || (g_syn.histOpen && !strcmp(nm, "cmbhlst")) || !strcmp(nm, "iteminfo")))
                             || (g_equip.open && (!strcmp(nm, "equip") || !strcmp(nm, "inventor") || !strcmp(nm, "iteminfo") || !strcmp(nm, "money") || !strcmp(nm, "itemxinf")))
                             || (g_mcat.open && (!strcmp(nm, "meritcat") || !strcmp(nm, "bluehelp") || !strcmp(nm, "meritinf")))
                             || (g_cel.open && (!strcmp(nm, "fe_equip") || !strcmp(nm, "fe_inven") || !strcmp(nm, "bluehelp")))
                             || (g_faith.open && (!strcmp(nm, "fp_cat") || !strcmp(nm, "fp_cat2") || !strcmp(nm, "bluehelp") || !strcmp(nm, "fp_info")))
                             || (g_area.open && !strcmp(nm, "arealist")) || (g_search.open && !strcmp(nm, g_search.ctx == 2 ? "levelsyn" : g_search.ctx == 4 ? "link12" : "scresult"))
                             || (g_ls.open && !strcmp(nm, "link5")) || ((g_ls.open || (g_search.open && g_search.ctx == 4)) && !strcmp(nm, "link6"))
                             || (g_scopt.open && !strcmp(nm, "scoption"))
                             || (g_shop.mainOpen && (!strcmp(nm, "shopmain") || (g_shop.open && (!strcmp(nm, "shop") || !strcmp(nm, "iteminfo") || !strcmp(nm, "money") || (g_shop.qtyOk && !strcmp(nm, "itemctrl"))))))
                             || (g_cur.open && !strcmp(nm, "evitem"))
                             || (g_trade.open && (!strcmp(nm, "moneyctr") || !strcmp(nm, "itemctrl") || !strcmp(nm, "iteminfo") || !strcmp(nm, "gift") || !strcmp(nm, "money")));
            if (!ours && !cardOurs && !fepOurs && !invOurs && !ynOurs && w > 0 && h > 0 && w < 4000 && h < 4000) {     // parked by its parent, not by us
                int nx = r[0], ny = r[1] - HIDE_DY;
                // the NPC answers are up : ABOVE AioHUD's box, left edges level (user 2026-11-17 "on la veut au-dessus") ;
                // no room above : below it ; always inside the UI screen
                if (besideNpc) {
                    const int L = (int)(g_npcBoxR[0] * uiW), T = (int)(g_npcBoxR[1] * uiH), B = (int)((g_npcBoxR[1] + g_npcBoxR[3]) * uiH);
                    nx = L; if (nx + w > uiW) nx = uiW - w; if (nx < 0) nx = 0;
                    ny = T - 4 - h; if (ny < 0) ny = B + 4; if (ny + h > uiH) ny = uiH - h; if (ny < 0) ny = 0;
                }
                bool ok = true;
                __try { ((SetRectFn)g_setter)(obj, nx, ny, w, h, 1, 0, 0); }
                __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
                if (ok) refresh_handler(obj, nm);
                if (g_rescueLog < 40) { ++g_rescueLog; windower::debug::log("NATIVEUI '%s' was parked with its parent (y %d) : %s", nm, r[1], !ok ? "setter faulted" : besideNpc ? "put beside AioHUD's NPC box" : "brought back on screen"); }
            }
        }
        node = next;
    }
}

const NativeMenu& native_action_menu() { return g_act; }
const NativeEntryMenu& native_sub_menu() { return g_sub; }
const NativeEntryMenu& native_main_menu() { return g_mainM; }
const NativeEntryMenu& native_main_sub() { return g_mainSub; }
const NativeEntryMenu& native_main_sort() { return g_mainSort; }
const NativeEntryMenu& native_main_yn() { return g_mainYn; }
const NativeProfile& native_profile() { return g_prof; }
const NativePanel& native_persona() { return g_persona; }
const NativePanel& native_level() { return g_levelW; }
const PersonaMem& native_persona_data() { return g_persData; }
const NativePanel& native_joblevel() { return g_jobW; }
const NativePanel& native_masterle() { return g_mlW; }
const NativeJobs& native_jobs() { return g_jobs; }
const NativePanel& native_btlskill() { return g_btlW; }
const NativePanel& native_mgcskill() { return g_mgcW; }
const NativeSkills& native_skills() { return g_skills; }
const NativePanel& native_trdskill() { return g_trdW; }
const NativePanel& native_levmerit() { return g_lmW; }
const NativeMerits& native_merits() { return g_merits; }
const NativePanel& native_utpoint() { return g_utW; }
const NativeUnity& native_unity() { return g_unity; }
const NativeUnityInfo& native_unity_info() { return g_uinfo; }
const NativeUnityRank& native_unity_rank() { return g_urank; }
const NativeAreaList& native_arealist() { return g_area; }
const NativeSearch& native_search() { return g_search; }
const NativeJobPoints& native_job_points() { return g_jpts; }
const NativeJpCat& native_jp_cat() { return g_jcat; }
const NativeFaith& native_faith() { return g_faith; }
const NativeCelestite& native_celestite() { return g_cel; }
const NativeMeritCat& native_merit_cat() { return g_mcat; }
const NativeEquip& native_equip() { return g_equip; }
const NativeItems& native_items() { return g_items; }
const NativeSynth& native_synth() { return g_syn; }
const NativeComment& native_comment() { return g_cmt; }
const NativeLinkshell& native_linkshell() { return g_ls; }
const NativeRegion& native_region() { return g_rg; }
void native_ui_movediag(int n) { g_diagN = n < 0 ? 0 : (n > 2000 ? 2000 : n); }
// `//aio winlist` (aiohud_probes.cpp) : every OPEN window, its name, rect (UI units and backbuffer px), closing flag,
// handler, and whether AioHUD parked it -- to find a window by where it sits on screen (the buff icons, 2026-11-20).
int native_ui_winlist(int screenW, int screenH) {
    const u32 mgr = manager(); if (!mgr) { windower::debug::log("WINLIST : no manager"); return -1; }
    u32 wh = 0; safe_read(mgr + 0x80, &wh);
    const int uiW = (short)(wh & 0xFFFF), uiH = (short)(wh >> 16);
    const float kx = uiW > 0 ? (float)screenW / (float)uiW : 1.0f, ky = uiH > 0 ? (float)screenH / (float)uiH : 1.0f;
    windower::debug::log("WINLIST === UI %dx%d units, backbuffer %dx%d (x%d.%03d) ===", uiW, uiH, screenW, screenH, (int)kx, (int)(kx * 1000.0f) % 1000);
    u32 node = 0; safe_read(mgr + MGR_OPEN_LIST, &node); int n = 0;
    for (int g = 0; valid_ptr(node) && g < 400; ++g) {
        u32 next = 0, o = 0, def = 0, hd = 0; unsigned char dead = 0, cl = 0; char nm[9] = "?"; short r[4] = { 0, 0, 0, 0 };
        safe_read(node + NODE_NEXT, &next); safe_read(node + NODE_VAL, &o); rd8(node + 0x14, dead);
        if (valid_ptr(o) && safe_read(o + OBJ_DEF, &def) && valid_ptr(def) && rdname(def, nm)) {
            rd8(o + OBJ_CLOSING, cl); safe_read(o + OBJ_HANDLER, &hd);
            rd16(o + OBJ_RECT, r[0]); rd16(o + OBJ_RECT + 2, r[1]); rd16(o + OBJ_RECT + 4, r[2]); rd16(o + OBJ_RECT + 6, r[3]);
            const bool parked = uiH > 0 && r[1] >= uiH + HIDE_DY / 2;
            const int py0 = parked ? r[1] - HIDE_DY : r[1];
            windower::debug::log("WINLIST %2d '%-8s' units %d,%d -> %d,%d (%dx%d) ; px %d,%d %dx%d%s%s%s ; obj %08X handler %08X",
                n, nm, r[0], r[1], r[2], r[3], r[2] - r[0], r[3] - r[1],
                (int)(r[0] * kx), (int)(py0 * ky), (int)((r[2] - r[0]) * kx), (int)((r[3] - r[1]) * ky),
                parked ? " PARKED" : "", cl ? " closing" : "", dead ? " dead" : "", o, hd);
            // the buff icons' window : its handler dumped (0x180 bytes, dwords) -- to find the count / the icon size
            if (!strcmp(nm, "buff") && valid_ptr(hd)) {
                for (u32 off = 0; off < 0x180; off += 0x20) {
                    u32 d[8] = { 0 }; for (int i = 0; i < 8; ++i) safe_read(hd + off + (u32)i * 4, &d[i]);
                    windower::debug::log("WINLIST    buff h+%03X : %08X %08X %08X %08X  %08X %08X %08X %08X", off, d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7]);
                }
            }
            ++n;
        }
        node = next;
    }
    windower::debug::log("WINLIST === %d window(s) ===", n);
    return n;
}

// ---- the TOP BAR (titlewin + helpwind) -- docs/notes/top-bar-re-2026-11-20.md (static + live 2026-11-20) ----
// Every global and field offset from the client's own code (self-healing, rule 7) : TB_FADE (the fade test : both
// globals, both help-line offsets), TB_PICK (the slot base, the ticker flags), TB_EMPTY (busy / shown slot / text ptr).
// Retried every 5 s, logged ; three misses -> g_tbFail : the client's own bar stays (eff_hide), never a blank one.
namespace {
struct TbRes { u32 glob[2], soff[2], busy, slot, ptr, base, tick; };   // [0] titlewin, [1] helpwind
TbRes g_tbr; bool g_tbrOk = false; int g_tbrTries = 0; DWORD g_tbrNext = 0;
bool tb_res() {
    if (g_tbrOk) return true;
    const DWORD now = GetTickCount();
    if (g_tbrTries && (int)(now - g_tbrNext) < 0) return false;
    ++g_tbrTries; g_tbrNext = now + 5000;
    const u32 f = aob_masked("8B 35 ?? ?? ?? ?? 8D 8E ?? ?? ?? ?? E8 ?? ?? ?? ?? 84 C0 0F 84 ?? ?? ?? ?? 66 83 BE ?? ?? ?? ?? 00 0F 85 ?? ?? ?? ?? 8B 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 84 C0 0F 84 ?? ?? ?? ?? A1 ?? ?? ?? ?? 8D 48 ?? E8");
    const u32 p = aob_masked("A1 ?? ?? ?? ?? 53 55 56 8B F1 8A 48 ?? 33 DB 57 3A CB 74 ?? 8D BE ?? ?? ?? ?? 83 C9 FF 33 C0 F2 AE F7 D1 49 74 ?? BA 03 00 00 00 8D 04 92 89 56 ?? 8D 0C 80 8D 04 8A 8D 0C 16 8D 44 41 ?? 50 89 46 ?? E8 ?? ?? ?? ?? 8B 4E ?? 83 C4 04 3B C1 89 46 ?? 89 5E ?? 7F ?? 88 9E");
    const u32 e = aob_masked("8B 41 ?? 85 C0 74 03 32 C0 C3 83 79 ?? 04 7C 03 B0 01 C3 8B 41 ?? 85 C0 75 03 B0 01 C3 8B 41 ?? 85 C0 0F 94 C0 C3");
    TbRes r = {}; unsigned char hs = 0, b1 = 0, b2 = 0, b3 = 0, sb = 0;
    bool ok = f && p && e && safe_read(f + 0x02, &r.glob[0]) && safe_read(f + 0x08, &r.soff[0]) && safe_read(f + 0x3B, &r.glob[1]) && rd8(f + 0x41, hs)
           && rd8(e + 0x02, b1) && rd8(e + 0x0C, b2) && rd8(e + 0x15, b3) && rd8(p + 0x3D, sb) && safe_read(p + 0x59, &r.tick);
    if (ok) {
        r.soff[1] = hs; r.busy = b1; r.slot = b2; r.ptr = b3; r.base = sb;
        ok = valid_ptr(r.glob[0]) && valid_ptr(r.glob[1]) && r.soff[0] < 0x400 && r.base + 4 * 0xCB <= 0x3A0 && r.tick + 2 <= 0x3A4
             && r.busy < r.base && r.slot < r.base && r.ptr < r.base;
    }
    if (ok) { g_tbr = r; g_tbrOk = true; g_tbFail = false;
        windower::debug::log("NATIVEUI top bar resolved (try %d) : titlewin FFXiMain+%X (line +0x%X), helpwind FFXiMain+%X (line +0x%X) ; busy +0x%X, shown slot +0x%X, text +0x%X, slots +0x%X, ticker +0x%X",
            g_tbrTries, r.glob[0] - ffximain_base(), r.soff[0], r.glob[1] - ffximain_base(), r.soff[1], r.busy, r.slot, r.ptr, r.base, r.tick); }
    else {
        if (g_tbrTries == 3 && !g_tbFail) g_tbFail = true;
        if (g_tbrTries == 1 || g_tbrTries == 3 || g_tbrTries % 12 == 0)
            windower::debug::log("NATIVEUI top bar NOT resolved (try %d : fade %d pick %d empty %d)%s", g_tbrTries, !!f, !!p, !!e, g_tbFail ? " -- the client's own bar is shown" : "");
    }
    return g_tbrOk;
}
bool tb_line(int i, NativeTopLine& L) {
    memset(&L, 0, sizeof(L));
    u32 h = 0, win = 0;
    if (!safe_read(g_tbr.glob[i], &h) || !valid_ptr(h)) return false;
    static unsigned char S[0x3A4];
    if (!rdblk(h + g_tbr.soff[i], S, sizeof(S))) return false;
    L.up = safe_read(h + 0x08, &win) && win != 0;
    u32 busy = 0, tick = 0; int slot = 4;
    memcpy(&busy, S + g_tbr.busy, 4); memcpy(&slot, S + g_tbr.slot, 4); memcpy(&tick, S + g_tbr.ptr, 4);
    L.busy = busy != 0;
    if (slot >= 0 && slot < 4 && tick) {                    // shown : the slot's text (the ticker pointer only says "something is shown")
        const unsigned char* t = S + g_tbr.base + slot * 0xCB;
        int n = 0; while (n < 0xCA && t[n]) { L.text[n] = (char)t[n]; ++n; }
        L.text[n] = 0;
        if (S[g_tbr.tick + 1] && n >= 2 && L.text[n - 1] == ' ' && L.text[n - 2] == ' ') L.text[n - 2] = 0;   // the ticker's loop padding
    }
    return true;
}
void topbar_poll(bool on) {
    if (!on) { g_tb.ok = false; return; }
    if (!tb_res()) { g_tb.ok = false; return; }
    NativeTopBar t; t.ok = tb_line(0, t.title) && tb_line(1, t.help);
    static int bad = 0;
    if (!t.ok) { if (++bad == 30) windower::debug::log("NATIVEUI top bar : unreadable for 30 frames (handlers gone ? a zone) -- nothing drawn meanwhile"); }
    else { if (bad >= 30) windower::debug::log("NATIVEUI top bar : read again"); bad = 0; }
    g_tb = t;
}

// ---- the MACRO BAR (mcr1pall / mcr2pall) -- docs/notes/macro-bar-re-2026-11-20.md (static + live 2026-11-20) ----
// MCR_INIT gives M's slot (M = *slot), the controller C and the Ctrl bar's handler (Alt = +4) ; MCR_BOOKNAME the book /
// title offsets ; MCR_EXEC the macro stride's base ; MCR_LONG the manager's long-bar byte. Retried, logged, g_mcFail.
struct McRes { u32 mSlot, cGlob, bar[2], book, title, longB; };
McRes g_mcr; bool g_mcrOk = false; int g_mcrTries = 0; DWORD g_mcrNext = 0;
bool mc_res() {
    if (g_mcrOk) return true;
    const DWORD now = GetTickCount();
    if (g_mcrTries && (int)(now - g_mcrNext) < 0) return false;
    ++g_mcrTries; g_mcrNext = now + 5000;
    const u32 a = aob_masked("53 E8 ?? ?? ?? ?? 6A 24 A3 ?? ?? ?? ?? E8 ?? ?? ?? ?? 33 DB 83 C4 04 3B C3 74 15 8B 0D ?? ?? ?? ?? 51 8B C8 E8 ?? ?? ?? ?? A3 ?? ?? ?? ?? EB 06 89 1D ?? ?? ?? ?? 6A 50 E8 ?? ?? ?? ?? 83 C4 04 3B C3 74 1C 8B 15 ?? ?? ?? ?? 8B 0D ?? ?? ?? ?? 52 51 8B C8 E8 ?? ?? ?? ?? A3 ?? ?? ?? ?? EB 06 89 1D ?? ?? ?? ?? 6A 50");
    const u32 b = aob_masked("8B 44 24 04 85 C0 7C 05 83 F8 28 7C 06 8B 81 C0 1D 00 00 C1 E0 04 8D 84 08 C6 1D 00 00 C2 04 00");
    const u32 l = aob_masked("8A 86 B5 00 00 00 84 C0 0F 85 ?? ?? ?? ?? BF ?? ?? ?? ?? 8B F3 B9 11 00 00 00 33 D2 F3 A6 75 17");
    const u32 d = aob_masked("83 EC 1C 53 8B D9 8B 43 08 85 C0 0F 84 ?? ?? ?? ?? 66 8B 48 4C 50 66 89 4C 24 10 B9 ?? ?? ?? ??");   // MCR_PALDRAW head : +0x1C MGR
    McRes r = {}; u32 mgr = 0;
    bool ok = a && b && l && d && safe_read(a + 0x09, &r.mSlot) && safe_read(a + 0x2A, &r.cGlob) && safe_read(a + 0x5A, &r.bar[0])
           && safe_read(b + 0x0F, &r.book) && safe_read(b + 0x19, &r.title) && safe_read(d + 0x1C, &mgr);
    if (ok) { r.bar[1] = r.bar[0] + 4; r.longB = mgr + 0xB5;
        ok = valid_ptr(r.mSlot) && valid_ptr(r.cGlob) && valid_ptr(r.bar[0]) && valid_ptr(mgr) && r.book < 0x2360 && r.title < 0x2360 && r.title + 40 * 16 <= 0x2360; }
    if (ok) { g_mcr = r; g_mcrOk = true; g_mcFail = false;
        windower::debug::log("NATIVEUI macro bar resolved (try %d) : data FFXiMain+%X, controller FFXiMain+%X, bars FFXiMain+%X, book +0x%X, titles +0x%X, long byte FFXiMain+%X",
            g_mcrTries, r.mSlot - ffximain_base(), r.cGlob - ffximain_base(), r.bar[0] - ffximain_base(), r.book, r.title, r.longB - ffximain_base()); }
    else {
        if (g_mcrTries == 3 && !g_mcFail) g_mcFail = true;
        if (g_mcrTries == 1 || g_mcrTries == 3 || g_mcrTries % 12 == 0)
            windower::debug::log("NATIVEUI macro bar NOT resolved (try %d : init %d book %d long %d draw %d)%s", g_mcrTries, !!a, !!b, !!l, !!d, g_mcFail ? " -- the client's own bar is shown" : "");
    }
    return g_mcrOk;
}
void macro_poll(bool on, u32 mgr) {
    NativeMacro& K = g_mc;
    if (!on || !mc_res()) { K.ok = false; K.open = 0; return; }
    NativeMacro t; memset(&t, 0, sizeof(t)); t.lastRun[0] = t.lastRun[1] = -1;
    u32 M = 0, C = 0;
    if (!safe_read(g_mcr.mSlot, &M) || !valid_ptr(M) || !safe_read(g_mcr.cGlob, &C) || !valid_ptr(C)) { K.ok = false; K.open = 0; return; }
    unsigned char c[0x1C]; u32 book = 0, set2 = 0;
    if (!rdblk(C, c, sizeof(c)) || !safe_read(M + g_mcr.book, &book) || !safe_read(M + 0x1DB8, &set2) || book >= 40 || set2 >= 20) { K.ok = false; K.open = 0; return; }
    t.book = (int)book; t.set = (int)(set2 / 2);
    t.open = c[0x0C] <= 2 ? c[0x0C] : 0;
    t.lastRun[0] = c[0x0E] < 10 ? c[0x0E] : -1; t.lastRun[1] = c[0x0F] < 10 ? c[0x0F] : -1;
    rdblk(M + g_mcr.title + book * 16, t.bookTitle, 16); t.bookTitle[16] = 0;
    for (int i = 0; i < 16 && t.bookTitle[i]; ++i) if ((unsigned char)t.bookTitle[i] < 0x20) t.bookTitle[i] = '?';
    for (int k = 0; k < 20; ++k) {
        if (!rdblk(M + 4 + (u32)k * 0x17C + 0x172, t.name[k], 8)) t.name[k][0] = 0;
        t.name[k][8] = 0;
    }
    unsigned char lb = 1; rd8(g_mcr.longB, lb); t.longBar = lb == 0;
    if (t.open) {                                              // the open bar's window : its rect (parked : where it would be), its hand
        const u32 o = win_named(mgr, t.open == 1 ? (t.longBar ? "mcr1long" : "mcr1pall") : (t.longBar ? "mcr2long" : "mcr2pall"));
        const u32 o2 = o ? o : win_named(mgr, t.open == 1 ? (t.longBar ? "mcr1pall" : "mcr1long") : (t.longBar ? "mcr2pall" : "mcr2long"));
        u32 wh = 0; short hc = 0;
        if (o2 && rd16(o2 + OBJ_RECT, t.rx0) && rd16(o2 + OBJ_RECT + 2, t.ry0) && rd16(o2 + OBJ_RECT + 4, t.rx1) && rd16(o2 + OBJ_RECT + 6, t.ry1) && safe_read(mgr + 0x80, &wh)) {
            t.uiW = (short)(wh & 0xFFFF); t.uiH = (short)(wh >> 16);
            if (t.ry0 >= t.uiH) { t.ry0 = (short)(t.ry0 - HIDE_DY); t.ry1 = (short)(t.ry1 - HIDE_DY); }
            if (rd16(o2 + 0x4C, hc) && hc >= 1 && hc <= 10) t.hand = hc;
        } else t.open = 0;                                     // the controller says open, the window is not up yet (the next tick)
    }
    t.ok = true;
    if (t.book != K.book || t.set != K.set || !K.ok) windower::debug::log("NATIVEUI macro bar : book %d '%s' set %d (%s bar)", t.book + 1, t.bookTitle, t.set + 1, t.longBar ? "long" : "short");
    K = t;
}
// ---- the CONNECTION box (netstat) -- docs/notes/netstat-re-2026-11-20.md ----
// NS_CREATE : the netstat handler's global ; NS_STATS : &P and the stats block (+0 recv, +4 sent, +8 score) ; NS_ROLL :
// the packet-gap field ; NS_ICON : the GM count's offset. Handler : +0x1C the mailbox count. Retried, logged, g_nsFail.
struct NsRes { u32 glob, pVar, stats, gap, gmOff; };
NsRes g_nsr; bool g_nsrOk = false; int g_nsrTries = 0; DWORD g_nsrNext = 0;
bool ns_res() {
    if (g_nsrOk) return true;
    const DWORD now = GetTickCount();
    if (g_nsrTries && (int)(now - g_nsrNext) < 0) return false;
    ++g_nsrTries; g_nsrNext = now + 5000;
    const u32 c = aob_masked("6A 7C E8 ?? ?? ?? ?? 8B F0 83 C4 04 3B F3 74 ?? 8D 4E 04 E8 ?? ?? ?? ?? 8D 4E ?? C7 06 ?? ?? ?? ?? 66 89 7E 0E 89 5E 08 C6 46 0C 01 E8 ?? ?? ?? ?? 8D 4E ?? E8 ?? ?? ?? ?? C7 06 ?? ?? ?? ?? C7 46 ?? 01 00 00 00 89 5E ?? 89 35 ?? ?? ?? ??");
    const u32 s = aob_masked("A1 ?? ?? ?? ?? 53 05 ?? ?? ?? ?? 55 50 E8 ?? ?? ?? ?? 50 8D 4C 24 ?? 68 ?? ?? ?? ?? 51 E8");
    const u32 r = aob_masked("68 B8 0B 00 00 6A 01 E8 ?? ?? ?? ?? 83 C4 08 84 C0 74 ?? 56 8B 74 24 08 68 B8 0B 00 00 8B 46 ?? 50 56 E8 ?? ?? ?? ?? 8B 4E ?? 68 B8 0B 00 00 51 56 E8 ?? ?? ?? ?? 33 C0 68 B8 0B 00 00 89 46 ?? 89 46 ?? 8B 15 ?? ?? ?? ?? 8B 82 ?? ?? ?? ??");
    const u32 i = aob_masked("E8 ?? ?? ?? ?? 25 FF FF 00 00 89 46 ?? 7E 0C C7 46 ?? 02 00 00 00 5F 5E C2 04 00 E8 ?? ?? ?? ?? 66 85 C0 74 0C C7 46 ?? 01 00 00 00");
    NsRes q = {}; u32 p2 = 0; unsigned char gm = 0;
    bool ok = c && s && r && i && safe_read(c + 0x4B, &q.glob) && safe_read(s + 0x01, &q.pVar) && safe_read(s + 0x07, &q.stats)
           && safe_read(r + 0x45, &p2) && safe_read(r + 0x4B, &q.gap) && rd8(i + 0x0C, gm);
    if (ok) { q.gmOff = gm; ok = valid_ptr(q.glob) && valid_ptr(q.pVar) && p2 == q.pVar && q.stats < 0x100000 && q.gap < 0x100000 && q.gmOff + 4 <= 0x7C; }
    if (ok) { g_nsr = q; g_nsrOk = true; g_nsFail = false;
        windower::debug::log("NATIVEUI connection box resolved (try %d) : netstat FFXiMain+%X, P FFXiMain+%X, stats +0x%X, packet gap +0x%X, GM +0x%X",
            g_nsrTries, q.glob - ffximain_base(), q.pVar - ffximain_base(), q.stats, q.gap, q.gmOff); }
    else {
        if (g_nsrTries == 3 && !g_nsFail) g_nsFail = true;
        if (g_nsrTries == 1 || g_nsrTries == 3 || g_nsrTries % 12 == 0)
            windower::debug::log("NATIVEUI connection box NOT resolved (try %d : create %d stats %d roll %d icon %d)%s", g_nsrTries, !!c, !!s, !!r, !!i, g_nsFail ? " -- the client's own is shown" : "");
    }
    return g_nsrOk;
}
void net_poll(bool on) {
    if (!on || !ns_res()) { g_net.ok = false; return; }
    NativeNet t; memset(&t, 0, sizeof(t));
    u32 P = 0, N = 0, st[3] = { 0, 0, 0 }, gap = 0;
    if (!safe_read(g_nsr.pVar, &P) || !valid_ptr(P) || !rdblk(P + g_nsr.stats, st, sizeof(st))) { g_net.ok = false; return; }
    safe_read(P + g_nsr.gap, &gap);
    t.recvBps = st[0]; t.sentBps = st[1]; t.score = st[2] > 100 ? 100 : (int)st[2]; t.gapMs = gap;
    if (safe_read(g_nsr.glob, &N) && valid_ptr(N)) {
        u32 m = 0, g = 0;
        if (safe_read(N + 0x1C, &m) && m <= 200) t.mail = (int)m;
        if (safe_read(N + g_nsr.gmOff, &g) && g <= 999) t.gm = (int)g;
    }
    // THE PING (docs/notes/ping-re-2026-11-20.md, live 2026-11-20 : ~300 ms, 242..508, one sample per 400 ms). P : u16 +6 our
    // next seq, +0xA the server's ack of ours, u8 +0xC state (3 in game), u32 +0x41A0 the last send's time, +0x41A4 the last
    // receive's (the client's ms clock ; same offsets in both dumps). A receive acking our last datagram -> RTT = rx - tx.
    {
        static u32 lastRx = 0; static int ring[5] = { 0, 0, 0, 0, 0 }; static int nRing = 0, at = 0; static DWORD lastSample = 0;
        unsigned short seq = 0, ack = 0; unsigned char st = 0; u32 tx = 0, rx = 0;
        if (rd16(P + 6, *(short*)&seq) && rd16(P + 0xA, *(short*)&ack) && rd8(P + 0xC, st) && safe_read(P + 0x41A0, &tx) && safe_read(P + 0x41A4, &rx) && rx != lastRx) {
            const u32 d = rx - tx;
            if (st == 3 && (unsigned short)(seq - 1) == ack && d > 0 && d < 5000) { ring[at] = (int)d; at = (at + 1) % 5; if (nRing < 5) ++nRing; lastSample = GetTickCount(); }
            lastRx = rx;
        }
        if (nRing && GetTickCount() - lastSample < 10000) { int s = 0; for (int i = 0; i < nRing; ++i) s += ring[i]; t.pingMs = s / nRing; }
        else t.pingMs = -1;                                   // no sample for 10 s (a zone, a stall) : unknown, never a stale number
    }
    t.ok = true;
    static int lastBand = -1;
    const int band = t.score == 0 ? 0 : (t.score < 30 ? 1 : (t.score < 60 ? 2 : 3));
    if (band != lastBand) { windower::debug::log("NATIVEUI connection : score %d (band %d), gap %u ms, R %u S %u B/s, mail %d GM %d", t.score, band, t.gapMs, t.recvBps, t.sentBps, t.mail, t.gm); lastBand = band; }
    g_net = t;
}
}   // namespace
bool menu_event(u32 obj, int ev);                              // below : the client's own event entry (hover 9 / validate 5)
namespace {
// ---- the PLAYER TRADE (trade + gift) -- docs/notes/trade-re-2026-11-20.md (static + live 2026-11-20) ----
// P offsets from the client's own packet handlers (TR_OWN 0x025, TR_PEER 0x023, TR_STATE 0x022, TR_NAME : the own name
// MOVED between two client builds) ; the trade handler's offsets (+0x7C partner, +0xC0 / +0xC4 the states) are pinned by
// TR_PEERUI's fixed bytes -- its presence is the check. Retried, logged, g_trFail.
struct TrRes { u32 pVar, own, peer, state, name; };
TrRes g_trr; bool g_trrOk = false; int g_trrTries = 0; DWORD g_trrNext = 0;
bool tr_res() {
    if (g_trrOk) return true;
    const DWORD now = GetTickCount();
    if (g_trrTries && (int)(now - g_trrNext) < 0) return false;
    ++g_trrTries; g_trrNext = now + 5000;
    const u32 o = aob_masked("8B 44 24 0C 33 C9 56 8B 35 ?? ?? ?? ?? 8A 48 0A 8A 50 0B 88 94 CE ?? ?? ?? ?? 8B 35 ?? ?? ?? ?? 8B 50 04 33 C9 8A 48 0A 89 94 CE ?? ?? ?? ?? 66 8B 50 08 33 C9 5E 8A 48 0A A1 ?? ?? ?? ?? 66 89 94 C8 ?? ?? ?? ?? B0 01 C3");
    const u32 p = aob_masked("8B 54 24 04 53 55 8B 6C 24 14 56 33 DB 8A 45 0C 57 88 82 ?? ?? ?? ?? 33 C0 8A 45 0D 8D 0C 80 8D 04 48 66 8B 4D 0A 66 89 8C 82 ?? ?? ?? ?? 33 C0 8A 45 0D 05 ?? ?? 00 00 8D 0C 80 8D 04 48 8B 4D 04 89 0C 82");
    const u32 s = aob_masked("56 8B 74 24 10 8B 46 08 83 F8 09 0F 87 ?? ?? ?? ?? 33 C9 8A 88 ?? ?? ?? ?? FF 24 8D ?? ?? ?? ?? 8B 15 ?? ?? ?? ?? C7 82 ?? ?? ?? ?? 02 00 00 00 A1 ?? ?? ?? ?? C6 80 ?? ?? ?? ?? 01 8B 0D ?? ?? ?? ?? 8B 46 08 5E 89 81 ?? ?? ?? ??");
    const u32 n = aob_masked("A1 ?? ?? ?? ?? 85 C0 75 06 B8 ?? ?? ?? ?? C3 A1 ?? ?? ?? ?? 85 C0 75 06 B8 ?? ?? ?? ?? C3 05 ?? ?? ?? ?? C3");
    const u32 u = aob_masked("8B 44 24 04 66 8B 54 24 08 25 FF 00 00 00 66 89 94 C1 C8 00 00 00 8B 54 24 0C 89 94 C1 CC 00 00 00 B8 03 00 00 00 89 81 C4 00 00 00 89 81 C0 00 00 00 C2 0C 00");
    TrRes r = {}; u32 p2 = 0, p3 = 0;
    bool ok = o && p && s && n && u && safe_read(o + 0x09, &r.pVar) && safe_read(o + 0x2B, &r.own) && safe_read(p + 0x2A, &r.peer)
           && safe_read(s + 0x28, &r.state) && safe_read(n + 0x10, &p2) && safe_read(n + 0x1F, &r.name) && safe_read(o + 0x3A, &p3);
    if (ok) ok = valid_ptr(r.pVar) && p2 == r.pVar && p3 == r.pVar && r.state == r.own + 0x50 && r.own < 0x100000 && r.peer < 0x100000 && r.name < 0x100000;
    if (ok) { g_trr = r; g_trrOk = true; g_trFail = false;
        windower::debug::log("NATIVEUI trade resolved (try %d) : P FFXiMain+%X, own offer +0x%X, partner offer +0x%X, state +0x%X, own name +0x%X",
            g_trrTries, r.pVar - ffximain_base(), r.own, r.peer, r.state, r.name); }
    else {
        if (g_trrTries == 3 && !g_trFail) g_trFail = true;
        if (g_trrTries == 1 || g_trrTries == 3 || g_trrTries % 12 == 0)
            windower::debug::log("NATIVEUI trade NOT resolved (try %d : own %d peer %d state %d name %d ui %d)%s", g_trrTries, !!o, !!p, !!s, !!n, !!u, g_trFail ? " -- the client's own trade is shown" : "");
    }
    return g_trrOk;
}
u32 g_tradeObj = 0, g_qtyObj = 0, g_gilObj = 0, g_giftObj = 0;   // `trade` / itemctrl / moneyctr / gift window objects this frame (the drivers)
NativeTradeEnd g_trEnd = { 0, 0 };
void trade_poll(bool on, u32 mgr) {
    u32 focus = 0; { const u32 fp = aio::fm_addr(aio::FM_MENU_PTR); if (fp) safe_read(fp, &focus); }
    NativeTrade& K = g_trade; g_tradeObj = g_qtyObj = g_gilObj = g_giftObj = 0;
    if (!on || !tr_res()) { K.open = false; return; }
    ensure_event_hook();                                          // the trade's lines (hk_menuevent)
    { u32 P0 = 0, st = 0, lt = 0;                             // the end state : every frame, the windows gone or not
      if (safe_read(g_trr.pVar, &P0) && valid_ptr(P0) && safe_read(P0 + g_trr.state, &st) && safe_read(P0 + g_trr.state + 4, &lt)) { g_trEnd.state = (int)st; g_trEnd.lastType = (int)lt; } }
    // `trade` (a player), else `handover` (an NPC : trade-re s.5 -- H+0x14 + s*4 = u16 id, u8 index, u8 count for s 1..8,
    // u32 gil H+0x3C, the NPC's index / id H+0x44 / +0x48 ; the same 11 entries, 9 OK, 10 Cancel, 11 the gil)
    u32 o = win_named(mgr, "trade"); bool npc = false;
    if (!o) { o = win_named(mgr, "handover"); npc = o != 0; }
    if (!o) { K.open = false; return; }
    u32 T = 0, P = 0, wh = 0;
    if (!safe_read(o + OBJ_HANDLER, &T) || !valid_ptr(T) || !safe_read(g_trr.pVar, &P) || !valid_ptr(P)) { K.open = false; return; }
    NativeTrade t; memset(&t, 0, sizeof(t));
    rdblk(P + g_trr.name, t.me, 16); t.me[16] = 0;
    for (int i = 0; i < 17 && t.me[i]; ++i) if ((unsigned char)t.me[i] < 0x20) t.me[i] = '?';
    if (npc) {
        unsigned char hb[0x4C];
        if (!rdblk(T, hb, sizeof(hb))) { K.open = false; return; }
        t.npc = true; t.own = 3; t.peer = 3;
        for (int s = 1; s <= 8; ++s) { unsigned short id = 0; memcpy(&id, hb + 0x14 + s * 4, 2); if (id && id != 0xFFFF && hb[0x14 + s * 4 + 3]) { t.mine[s].id = id; t.mine[s].count = hb[0x14 + s * 4 + 3]; } }
        u32 gil = 0; memcpy(&gil, hb + 0x3C, 4); if (gil) { t.mine[0].id = 0xFFFF; t.mine[0].count = gil; }
        u32 ni = 0; memcpy(&ni, hb + 0x44, 4); t.npcIndex = (int)ni;
    } else {
        u32 own = 0, peer = 0; safe_read(T + 0xC0, &own); safe_read(T + 0xC4, &peer); t.own = (int)own; t.peer = (int)peer;
        rdblk(T + 0x7C, t.partner, 32); t.partner[32] = 0;
        for (int i = 0; i < 33 && t.partner[i]; ++i) if ((unsigned char)t.partner[i] < 0x20) t.partner[i] = '?';
        unsigned char ob[9 * 8], pb[9 * 0x2C];
        if (!rdblk(P + g_trr.own, ob, sizeof(ob)) || !rdblk(P + g_trr.peer, pb, sizeof(pb))) { K.open = false; return; }
        for (int s = 0; s < 9; ++s) {
            u32 c = 0; unsigned short id = 0; memcpy(&c, ob + s * 8, 4); memcpy(&id, ob + s * 8 + 4, 2);
            if (c) { t.mine[s].count = c; t.mine[s].id = s == 0 ? 0xFFFF : id; }
            unsigned short pid = 0; u32 pc = 0; memcpy(&pid, pb + s * 0x2C, 2); memcpy(&pc, pb + s * 0x2C + 4, 4);
            if (s == 0) { t.theirs[0].count = pc; t.theirs[0].id = pc ? 0xFFFF : 0; }
            else if (pid && pid != 0xFFFF && pc) { t.theirs[s].id = pid; t.theirs[s].count = pc; }
        }
    }
    short cur = 0; rd16(o + 0x4C, cur); t.cursor = cur >= 1 && cur <= 11 ? cur : 0;
    if (!rd16(o + OBJ_RECT, t.rx0) || !rd16(o + OBJ_RECT + 2, t.ry0) || !rd16(o + OBJ_RECT + 4, t.rx1) || !rd16(o + OBJ_RECT + 6, t.ry1) || !safe_read(mgr + 0x80, &wh)) { K.open = false; return; }
    t.uiW = (short)(wh & 0xFFFF); t.uiH = (short)(wh >> 16);
    if (t.ry0 >= t.uiH) { t.ry0 = (short)(t.ry0 - HIDE_DY); t.ry1 = (short)(t.ry1 - HIDE_DY); }
    t.focused = focus == o;
    if (const u32 g = win_named(mgr, "gift")) {                // the partner's side : where the client stacks it
        if (rd16(g + OBJ_RECT, t.gx0) && rd16(g + OBJ_RECT + 2, t.gy0) && rd16(g + OBJ_RECT + 4, t.gx1) && rd16(g + OBJ_RECT + 6, t.gy1)) {
            if (t.gy0 >= t.uiH) { t.gy0 = (short)(t.gy0 - HIDE_DY); t.gy1 = (short)(t.gy1 - HIDE_DY); }
            t.giftUp = true;
            short gc = 0; rd16(g + 0x4C, gc); t.giftCursor = gc >= 1 && gc <= 8 ? gc : 0;
            t.giftFocused = focus == g; g_giftObj = g;
        }
    }
    if (const u32 q = win_named(mgr, "itemctrl")) {             // the quantity box (trade-re s.6 : handler +0x22 max, +0x23 value, s8)
        u32 h = 0; signed char mx = 0, v = 0;
        if (safe_read(q + OBJ_HANDLER, &h) && valid_ptr(h) && rd8(h + 0x22, *(unsigned char*)&mx) && rd8(h + 0x23, *(unsigned char*)&v)) { t.qtyUp = true; t.qtyMax = mx; t.qtyVal = v; g_qtyObj = q; }
    }
    if (const u32 m = win_named(mgr, "moneyctr")) {             // the gil box (+0x20 the digit's step, +0x24 max, +0x28 value)
        u32 h = 0;
        if (safe_read(m + OBJ_HANDLER, &h) && valid_ptr(h) && safe_read(h + 0x20, &t.gilDigit) && safe_read(h + 0x24, &t.gilMax) && safe_read(h + 0x28, &t.gilVal)) { t.gilUp = true; g_gilObj = m; }
    }
    { NativePanel P2;                                          // the three boxes' places
      if (t.qtyUp && panel_read(mgr, "itemctrl", P2)) { t.qx0 = P2.rx0; t.qy0 = P2.ry0; t.qx1 = P2.rx1; t.qy1 = P2.ry1; }
      if (t.gilUp && panel_read(mgr, "moneyctr", P2)) { t.mx0 = P2.rx0; t.my0 = P2.ry0; t.mx1 = P2.rx1; t.my1 = P2.ry1; }
      if (panel_read(mgr, "money", P2)) { t.moneyUp = true; t.wx0 = P2.rx0; t.wy0 = P2.ry0; t.wx1 = P2.rx1; t.wy1 = P2.ry1; }
      t.myGilOk = read_player_gil(t.myGil); }
    t.open = true; g_tradeObj = o;
    if (!K.open) {
        if (t.npc) windower::debug::log("NATIVEUI trade : an NPC trade (handover, NPC index %d) -- AioHUD draws it", t.npcIndex);
        else windower::debug::log("NATIVEUI trade : open with '%s' (states %d / %d) -- AioHUD draws it", t.partner, t.own, t.peer);
    }
    K = t;
}
}   // namespace
const NativeTrade& native_trade() { return g_trade; }
const NativeTradeEnd& native_trade_end() { return g_trEnd; }
bool native_trade_box_event(int box, int ev) {
    const u32 o = box == 0 ? g_qtyObj : g_gilObj;
    if (!g_trade.open || !o || ev < 1 || ev > 6) return false;
    if (ev == 5 && !wr16(o + 0x4C, 1)) return false;           // validate acts on entry 1 (the value)
    return menu_event(o, ev);
}
// ---- the YES / NO CONFIRMS (comyn class) -- docs/notes/dialogs-loot-inspect-re-2026-11-20.md s.1 ----
// The class's vtable from its constructor (DLG_CTOR +0x1F) : a window is a confirm when its handler carries it (the 8 names
// alone are not enough -- the check is the client's class). Retried, logged, g_dlgFail.
namespace {
u32 g_dlgVt = 0; bool g_dlgResOk = false; int g_dlgTries = 0; DWORD g_dlgNext = 0;
bool dlg_res() {
    if (g_dlgResOk) return true;
    const DWORD now = GetTickCount();
    if (g_dlgTries && (int)(now - g_dlgNext) < 0) return false;
    ++g_dlgTries; g_dlgNext = now + 5000;
    const u32 a = aob_masked("53 56 8B F1 8D 4E 04 E8 ?? ?? ?? ?? 33 DB 8B CE 66 C7 46 0E FF FF 89 5E 08 C6 46 0C 01 C7 06 ?? ?? ?? ?? 89 5E 34 E8 ?? ?? ?? ?? 8B 4C 24 0C 8D 46 20 8B 11 89 10");
    u32 vt = 0, lo = 0, hi = 0; image_range(lo, hi);
    if (a && safe_read(a + 0x1F, &vt) && lo && vt >= lo && vt < hi) { g_dlgVt = vt; g_dlgResOk = true; g_dlgFail = false;
        windower::debug::log("NATIVEUI confirms resolved (try %d) : the class vtable FFXiMain+%X", g_dlgTries, vt - ffximain_base()); }
    else {
        if (g_dlgTries == 3 && !g_dlgFail) g_dlgFail = true;
        if (g_dlgTries == 1 || g_dlgTries == 3 || g_dlgTries % 12 == 0)
            windower::debug::log("NATIVEUI confirms NOT resolved (try %d : pattern %s)%s", g_dlgTries, a ? "found, vtable off" : "missing", g_dlgFail ? " -- the client's own are shown" : "");
    }
    return g_dlgResOk;
}
// the question as the client keeps it : single-byte text with control codes (0x1E / 0x1F colour + 1 byte, 0x7F + 1 byte),
// newlines ; a Shift-JIS pair (a JP client) shows as '?'
void dlg_text(u32 p, char* out, int cap) {
    out[0] = 0; if (!p || !valid_ptr(p)) return;
    unsigned char b[0x200]; if (!rdblk(p, b, sizeof(b) - 1)) return; b[sizeof(b) - 1] = 0;
    int o = 0;
    for (int i = 0; b[i] && o < cap - 1; ++i) {
        const unsigned char c = b[i];
        if (c == 0x1E || c == 0x1F || c == 0x7F) { if (b[i + 1]) ++i; continue; }
        if (c == 0x0A) { out[o++] = '\n'; continue; }
        if (c < 0x20) continue;
        if ((c >= 0x81 && c <= 0x9F) || (c >= 0xE0 && c <= 0xFC)) { if (b[i + 1]) ++i; out[o++] = '?'; continue; }
        out[o++] = c < 0x80 ? (char)c : '?';
    }
    out[o] = 0;
}
void dialog_poll(bool on, u32 mgr) {
    g_dlgObj = 0;
    if (!on || !dlg_res()) { g_dlg.open = false; return; }
    u32 focus = 0; { const u32 fp = aio::fm_addr(aio::FM_MENU_PTR); if (fp) safe_read(fp, &focus); }
    u32 pick = 0, pickH = 0; const char* pickName = 0;
    for (const char* const* w = DIALOG_W; *w; ++w) {
        const u32 o = win_named(mgr, *w); if (!o) continue;
        u32 h = 0, vt = 0;
        if (!safe_read(o + OBJ_HANDLER, &h) || !valid_ptr(h) || !safe_read(h, &vt) || vt != g_dlgVt) continue;
        u32 cb = 0; safe_read(h + 0x14, &cb); if (!cb) continue;                       // no question pending : closing
        if (!pick || o == focus) { pick = o; pickH = h; pickName = *w; }
        if (o == focus) break;
    }
    if (!pick) { if (g_dlg.open) windower::debug::log("NATIVEUI confirm : closed"); g_dlg.open = false; return; }
    NativeDialog d; memset(&d, 0, sizeof(d));
    lstrcpynA(d.name, pickName, sizeof(d.name));
    u32 q = 0, t = 0; safe_read(pickH + 0x1C, &q); safe_read(pickH + 0x34, &t);
    dlg_text(q, d.text, sizeof(d.text)); dlg_text(t, d.title, sizeof(d.title));
    unsigned char cok = 0, hold = 0; rd8(pickH + 0x39, cok); rd8(pickH + 0x40, hold); d.cancelOk = cok != 0; d.hold = hold != 0;
    short cur = 0; rd16(pick + 0x4C, cur); d.cursor = cur == 1 || cur == 2 ? cur : 2;
    u32 wh = 0;
    if (!rd16(pick + OBJ_RECT, d.rx0) || !rd16(pick + OBJ_RECT + 2, d.ry0) || !rd16(pick + OBJ_RECT + 4, d.rx1) || !rd16(pick + OBJ_RECT + 6, d.ry1) || !safe_read(mgr + 0x80, &wh)) { g_dlg.open = false; return; }
    d.uiW = (short)(wh & 0xFFFF); d.uiH = (short)(wh >> 16);
    if (d.ry0 >= d.uiH) { d.ry0 = (short)(d.ry0 - HIDE_DY); d.ry1 = (short)(d.ry1 - HIDE_DY); }
    d.focused = pick == focus; d.open = true; g_dlgObj = pick;
    if (!g_dlg.open || strcmp(g_dlg.text, d.text)) windower::debug::log("NATIVEUI confirm '%s' : \"%s\" (title '%s', cursor %d, escape %d, hold %d) -- AioHUD draws it", d.name, d.text, d.title, d.cursor, (int)d.cancelOk, (int)d.hold);
    g_dlg = d;
}
}   // namespace
const NativeDialog& native_dialog() { return g_dlg; }
bool native_dialog_hover(int entry) {
    if (!g_dlg.open || !g_dlg.focused || !g_dlgObj || (entry != 1 && entry != 2)) return false;
    if (!wr16(g_dlgObj + 0x4C, (short)entry)) return false;
    return menu_event(g_dlgObj, 9);
}
bool native_dialog_pick(int entry) {
    if (!g_dlg.open || !g_dlg.focused || !g_dlgObj || (entry != 1 && entry != 2)) return false;
    short cur = 0; rd16(g_dlgObj + 0x4C, cur);
    if (cur != entry && !native_dialog_hover(entry)) return false;
    return menu_event(g_dlgObj, 5);
}
bool native_trade_keep_own() {
    if (!g_trade.open || !g_trade.giftFocused || !g_giftObj) return false;
    return menu_event(g_giftObj, 8);                            // the client's own "switch window" : the focus back on `trade`
}
bool native_trade_set_gil(unsigned v) {
    if (!g_trade.open || !g_gilObj) return false;
    u32 h = 0, mx = 0;
    if (!safe_read(g_gilObj + OBJ_HANDLER, &h) || !valid_ptr(h) || !safe_read(h + 0x24, &mx)) return false;
    if (mx && v > mx) v = mx;                                  // never above what the client allows (0 = its own rule)
    __try { *(u32*)(h + 0x28) = v; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    return true;
}
bool native_trade_hover(int entry) {
    if (!g_trade.open || !g_trade.focused || !g_tradeObj || entry < 1 || entry > 11) return false;
    if (!wr16(g_tradeObj + 0x4C, (short)entry)) return false;
    return menu_event(g_tradeObj, 9);
}
bool native_trade_pick(int entry) {
    if (!g_trade.open || !g_trade.focused || !g_tradeObj || entry < 1 || entry > 11) return false;
    short cur = 0; rd16(g_tradeObj + 0x4C, cur);
    if (cur != entry && !native_trade_hover(entry)) return false;
    return menu_event(g_tradeObj, 5);
}
const NativeNet& native_net() { return g_net; }
// the buff window's place : the open `buff`, its rect less the park (read each frame while NU_BUFF is on)
NativeBuffWin g_bw;   // (g_bwObj / g_bwH : defined with the event hook, above)
// ---- the NPC SHOP (docs/notes/shops-re-2026-11-20.md ; live 2026-11-21 : 0x03E +4 = the row count, 0x03C 12-B records,
// `shop` count +0x20 / top +0x1E / visible +0x18 = 10, rows +0x38 rebuilt every update, itemctrl +0x22 max / +0x23 value) ----
void shop_poll(bool on, u32 mgr) {
    NativeShop S; memset(&S, 0, sizeof(S)); S.cursor = -1;
    if (!on || !mgr) { g_shop = S; g_shopMainUp = false; return; }
    u32 focus = 0; { const u32 fp = fm_addr(FM_MENU_PTR); if (fp) safe_read(fp, &focus); }
    NativeEntryMenu m; u32 mo = 0;
    const int er = entry_read(SHOPMAIN_W, m, &mo);
    S.mainOpen = er == 1 && mo; g_shopMainUp = mo != 0;           // (the born-park rule : the window, even before it reads)
    static int saidM = 0; static int wasM = -1;
    const int stM = mo ? (S.mainOpen ? 2 : 1) : 0;
    if (stM != wasM && saidM < 12) { ++saidM; wasM = stM;
        windower::debug::log("NATIVEUI shop : Buy / Sell menu %s", !mo ? "closed" : S.mainOpen ? "open -- AioHUD draws it" : "open but UNREADABLE (entry_read) -- nothing drawn for it"); }
    if (!S.mainOpen) { g_shop = S; return; }
    S.mainFocused = focus == mo; S.mainCursor = m.cursor; S.mx0 = m.rx0; S.my0 = m.ry0; S.uiW = m.uiW; S.uiH = m.uiH;
    while (S.uiH > 0 && S.my0 >= S.uiH) S.my0 = (short)(S.my0 - HIDE_DY);
    { unsigned g = 0; S.gilOk = read_player_gil(g); S.gil = g; }
    TextList T;
    if (tl_find(mgr, "shop", T)) {
        u32 rows = 0;
        if (safe_read(T.h + 0x38, &rows) && (T.count == 0 || valid_ptr(rows))) {
            S.n = T.count; S.top = T.top; S.vis = T.vis > 16 ? 16 : T.vis; S.cursor = T.cursor;
            S.rx0 = T.rx0; S.ry0 = T.ry0; S.uiW = T.uiW; S.uiH = T.uiH;
            S.sell = m.cursor == 2;
            bool ok = true; unsigned char r0[0x54] = { 0 };
            for (int i = 0; i < S.vis && S.top + i < S.n && ok; ++i) {
                unsigned char r[0x54]; if (!rdblk(rows + (u32)(S.top + i) * 0x54, r, sizeof(r))) { ok = false; break; }
                if (!i) memcpy(r0, r, sizeof(r0));
                u32 v0 = 0, v2 = 0, c1 = 0, c2 = 0; memcpy(&v0, r + 0x28, 4); memcpy(&v2, r + 0x30, 4); memcpy(&c1, r + 0x0C, 4); memcpy(&c2, r + 0x10, 4);
                short id = 0;
                if (valid_ptr(v0)) { if (!rd16(v0, id)) id = 0; } else id = (short)(v0 & 0xFFFF);   // the icon cell : a pointer to the id (buy), or the id
                NativeItemRow& w = S.row[i];
                w.itemId = (unsigned short)id; w.itemCount = 0; w.itemCol = c1; w.tempRow = false;
                w.hasPrice = true; w.price = v2 ? (int)(v2 - 1) : -1; w.priceCol = c2;
            }
            S.open = ok; S.focused = focus == T.obj;
            static int saidL = 0; static int wasN = -1, wasSell = -1;
            if (ok && (S.n != wasN || (int)S.sell != wasSell) && saidL < 12) { ++saidL; wasN = S.n; wasSell = (int)S.sell;
                u32 rec = 0, ic = 0, pc = 0; memcpy(&rec, r0 + 0x48, 4); memcpy(&ic, r0 + 0x28, 4); memcpy(&pc, r0 + 0x30, 4);
                windower::debug::log("NATIVEUI shop : %s list, %d rows (top %d, vis %d, cursor %d) -- row 0 : types %02X %02X %02X %02X, icon cell %08X -> item %u, name colour %08X, price cell %08X (-> %d) colour %08X, record %08X ; gil %u%s",
                    S.sell ? "SELL" : "BUY", S.n, S.top, S.vis, S.cursor, r0[0], r0[1], r0[2], r0[3], ic, S.row[0].itemId, S.row[0].itemCol,
                    pc, S.row[0].price, S.row[0].priceCol, rec, S.gil, S.gilOk ? "" : " (gil UNREADABLE)"); }
            if (!ok) { static int saidF = 0; if (saidF < 6) { ++saidF; windower::debug::log("NATIVEUI shop : a row UNREADABLE (%d rows, top %d) -- the list is not drawn this frame", S.n, S.top); } }
        }
    }
    if (S.open) { NativeItems q; memset(&q, 0, sizeof(q)); qty_read(mgr, focus, q); S.qtyOk = q.qtyOk; S.qtyFocused = q.qtyFocused; S.qtyVal = q.qtyVal; S.qtyMax = q.qtyMax; }
    g_shop = S;
}
void buffwin_poll(bool on, u32 mgr) {
    if (!on || !mgr) { g_bw.open = false; g_bwObj = g_bwH = 0; return; }
    NativeBuffWin B; memset(&B, 0, sizeof(B));
    u32 wh = 0; if (!safe_read(mgr + 0x80, &wh)) { g_bw.open = false; return; }
    B.uiW = (short)(wh & 0xFFFF); B.uiH = (short)(wh >> 16);
    if (B.uiW < 320 || B.uiH < 240 || B.uiW > 8000 || B.uiH > 8000) { g_bw.open = false; return; }
    u32 node = 0; safe_read(mgr + MGR_OPEN_LIST, &node);
    for (int g = 0; valid_ptr(node) && g < 400 && !B.open; ++g) {
        u32 next = 0, o = 0, def = 0; unsigned char dead = 0, cl = 0; char nm[9];
        safe_read(node + NODE_NEXT, &next); safe_read(node + NODE_VAL, &o); rd8(node + 0x14, dead);
        if (!dead && valid_ptr(o) && safe_read(o + OBJ_DEF, &def) && valid_ptr(def) && rdname(def, nm) && !strcmp(nm, "buff") && rd8(o + OBJ_CLOSING, cl) && !cl
            && rd16(o + OBJ_RECT, B.rx0) && rd16(o + OBJ_RECT + 2, B.ry0) && rd16(o + OBJ_RECT + 4, B.rx1) && rd16(o + OBJ_RECT + 6, B.ry1)) {
            while (B.ry0 >= B.uiH + HIDE_DY / 2) { B.ry0 = (short)(B.ry0 - HIDE_DY); B.ry1 = (short)(B.ry1 - HIDE_DY); }
            B.open = true;
            // THE CURSOR : the focus on `buff` (X / numpad +), the index at handler+0x30, the count at +0x40
            u32 focus = 0; { const u32 fp = fm_addr(FM_MENU_PTR); if (fp) safe_read(fp, &focus); }
            u32 hd = 0, cu = 0, cn = 0;
            g_bwObj = o; g_bwH = 0;
            if (safe_read(o + OBJ_HANDLER, &hd) && valid_ptr(hd) && safe_read(hd + 0x30, &cu) && safe_read(hd + 0x40, &cn) && cn <= 32) {
                g_bwH = hd; B.count = (int)cn;
                B.focused = focus == o; B.cursor = (B.focused && cu < cn) ? (int)cu : -1;
            } else B.cursor = -1;
            static int saidC = 0; static int wasC = -2;
            if (B.cursor != wasC && saidC < 30) { ++saidC; wasC = B.cursor; windower::debug::log("NATIVEUI buff cursor : %d of %d%s", B.cursor, B.count, B.focused ? "" : " (no focus)"); }
        }
        node = next;
    }
    static int said = 0; static int was = -1;
    if ((int)B.open != was && said < 6) { ++said; was = (int)B.open;
        windower::debug::log("NATIVEUI buff icons : %s (%d,%d -> %d,%d, UI %dx%d)", B.open ? "window up -- AioHUD draws the icons" : "window gone", B.rx0, B.ry0, B.rx1, B.ry1, B.uiW, B.uiH); }
    g_bw = B;
}
const NativeBuffWin& native_buffwin() { return g_bw; }
// the mouse : the client's cursor moved by its own field (+0x34 the previous one, as its own move leaves it), then
// validate through its event entry -- the client decides (a buff that cannot be cancelled stays). Logged, bounded.
bool native_buff_hover(int idx) {
    if (!g_bw.open || !g_bw.focused || !g_bwObj || !g_bwH || idx < 0 || idx >= g_bw.count) return false;
    u32 cu = 0; if (!safe_read(g_bwH + 0x30, &cu)) return false;
    if ((int)cu == idx) return true;
    bool ok = true;
    const int n = g_bw.count;
    __try { *(volatile u32*)(g_bwH + 0x30) = (u32)((idx + n - 1) % n); }   // just before it : the client's RIGHT lands on it
    __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    if (!ok) return false;
    g_bwInner = true; const bool ev = menu_event(g_bwObj, 3); g_bwInner = false;
    if (!ev) { __try { *(volatile u32*)(g_bwH + 0x30) = (u32)idx; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; } }
    g_bw.cursor = idx;
    return true;
}
void native_buff_set_order(const int* order, int n) {
    if (!order || n < 0 || n > 32) { g_bwOrdN = 0; return; }
    for (int i = 0; i < n; ++i) g_bwOrd[i] = order[i];
    g_bwOrdN = n; g_bwOrdMs = GetTickCount();
}
bool native_buff_pick(int idx) {
    if (!native_buff_hover(idx)) return false;
    const bool ok = menu_event(g_bwObj, 5);
    static int said = 0; if (said < 12) { ++said; windower::debug::log("NATIVEUI buff click : index %d of %d -> validate %s", idx, g_bw.count, ok ? "sent" : "FAILED"); }
    return ok;
}
const NativeTopBar& native_topbar() { return g_tb; }
const NativeMacro& native_macro() { return g_mc; }
const ItemExtra* native_item_extra(unsigned id) { for (int c = 0; c < 2; ++c) if (id && g_iix[c].id == id) return &g_iix[c]; return 0; }
// the client's ability table ids (0..0x1FF weapon skills, 0x200.. job abilities, 0x600..0x6FF job traits) : d_msg 55701
// the name, 55733 the description -- abilities-re-2026-11-19.md (all 124 traits / 626 abilities checked against it)
const char* native_ability_text(unsigned id, bool desc) { return dmsg_str(g_dm[desc ? 5 : 4], id); }
const char* native_merit_text(int index, bool desc) { return index < 0 ? 0 : dmsg_str(g_dm[3], (unsigned)(2 * index + (desc ? 1 : 0))); }
const char* native_faith_text(int id, bool desc) { return id < 0 ? 0 : dmsg_str(g_dm[2], (unsigned)(2 * id + (desc ? 1 : 0))); }
const char* native_jp_text(int kind, int id, bool desc, int job) {
    if (id < 0) return 0;
    if (kind == 0 || kind == 1) return dmsg_str(g_dm[0], (unsigned)(2 * id + (desc ? 1 : 0)));          // a job, a category
    if (kind == 2 && job >= 1 && job <= 22 && id < 256) return dmsg_str(g_dm[1], (unsigned)(2 * ((job - 1) * 256 + id) + (desc ? 1 : 0)));
    return 0;
}
const NativeEntryMenu& native_scoption() { return g_scopt; }
const NativeCurrencies& native_currencies() { return g_cur; }
const NativeChatInfo& native_chat_info() { return g_chat; }
const NativeList& native_list() { return g_list; }

u32 aob_masked(const char* pat);   // below : a masked, unique AOB in FFXiMain
// ---- the spell list : FUN_0556DD40's global (the spell slots), by signature ----
u32 g_spellSlotsVar = 0; int g_slTries = 0; DWORD g_slNext = 0;
bool spell_slots_res() {
    if (g_spellSlotsVar) return true;
    const DWORD now = GetTickCount();
    if (g_slTries && (int)(now - g_slNext) < 0) return false;          // retried every 5 s, never given up
    ++g_slTries; g_slNext = now + 5000;
    const u32 at = aob_masked("8B 44 24 04 85 C0 7C 0F 3B 41 44 7D 0A 8B 49 20 8D 04 C1 85 C0 75 06 83 C8 FF C2 04 00 8B 10 8B 0D ?? ?? ?? ?? 33 C0 66 8B 84 51 80 00 00 00 C2");
    if (at && safe_read(at + 33, &g_spellSlotsVar)) windower::debug::log("NATIVEUI spell list mapper found (try %d)", g_slTries);
    else { g_spellSlotsVar = 0; if (g_slTries == 1 || g_slTries % 12 == 0) windower::debug::log("NATIVEUI spell list mapper NOT found (try %d)", g_slTries); }
    return g_spellSlotsVar != 0;
}
// ---- the ability list : FUN_05565730's global (the ability slots), by signature -- the same code, +0x160 ----
u32 g_abilSlotsVar = 0; int g_alTries = 0; DWORD g_alNext = 0;
bool abil_slots_res() {
    if (g_abilSlotsVar) return true;
    const DWORD now = GetTickCount();
    if (g_alTries && (int)(now - g_alNext) < 0) return false;          // retried every 5 s, never given up
    ++g_alTries; g_alNext = now + 5000;
    const u32 at = aob_masked("8B 44 24 04 85 C0 7C 0F 3B 41 44 7D 0A 8B 49 20 8D 04 C1 85 C0 75 06 83 C8 FF C2 04 00 8B 10 8B 0D ?? ?? ?? ?? 33 C0 66 8B 84 51 60 01 00 00 C2");
    if (at && safe_read(at + 33, &g_abilSlotsVar)) windower::debug::log("NATIVEUI ability list mapper found (try %d)", g_alTries);
    else { g_abilSlotsVar = 0; if (g_alTries == 1 || g_alTries % 12 == 0) windower::debug::log("NATIVEUI ability list mapper NOT found (try %d)", g_alTries); }
    return g_abilSlotsVar != 0;
}
// The client's MOUNT list (Abilities -> Mount, "mount") : the magic / ability list class (handler +0x40 count, +0x38 top,
// +0x48 rows shown, +0x30 cursor, +0x20 8-byte items) -- but an item's int0 is the MOUNT ID itself (res/mounts.lua),
// no slot table between. Live capture 2026-11-17 : 16 mounts, 12 rows, items 1 2 3 4 5 6 7 8 A D E F..., the cursor
// 0..F and the top 0 -> 4 as the list scrolled (docs/game-data/native-windows.md).
int mount_read(u32 obj, u32 focus, NativeList& L) {
    L.kind = 2;
    u32 hd = 0, items = 0; int n = 0, top = 0, vis = 0, cur = 0, cap = 0;
    if (!safe_read(obj + OBJ_HANDLER, &hd) || !valid_ptr(hd) || !safe_read(hd + 0x40, (u32*)&n) || !safe_read(hd + 0x38, (u32*)&top)
        || !safe_read(hd + 0x48, (u32*)&vis) || !safe_read(hd + 0x30, (u32*)&cur) || !safe_read(hd + 0x44, (u32*)&cap)
        || !safe_read(hd + 0x20, &items)) return 0;
    if (n < 0 || n > 256 || vis < 1 || vis > 32 || top < 0 || cap < n || (n > 0 && !valid_ptr(items))) return 0;
    L.n = n; L.top = top; L.vis = vis; L.cursor = cur;
    for (int i = 0; i < n && i < 512; ++i) {
        u32 id = 0; if (!safe_read(items + (u32)i * 8, &id) || id > 255) return 0;
        L.all[L.allN++] = (unsigned short)id;
        if (i >= top && i < top + vis) L.id[L.rows++] = (unsigned short)id;
    }
    L.focused = focus == obj;
    L.open = true;
    return 1;
}

// The ITEMS list -- `inventor` opened from the ACTION menu (Items : the usable ones, the client's own filter). The same
// window serves the main menu's inventory, the equipment screen and trades : those are never read here (only while
// playermo is open) and never parked (they are not born under a parked parent). Live captures 2026-11-17
// (WINPROBE / INVPROBE 1-3) and 2026-11-18 (INVLOG, the whole list by name, both boxes side by side -- "j'ai voulu
// manger miso ramen +1 ca utilise abdhals seal" : the page top was read 2 rows high and the list 4 rows short) :
//   h+0x38 -> the rows, 0x54 bytes each : +0x2C the item id (u16), +0x30 the count, +0x28 its container entry ;
//   h+0x20 = (the page top's max << 16) | the count of rows -- 0x000C0016 : 22 rows, top 0..12 with 10 shown ;
//   h+0x24 = (?? << 16) | the cursor's row IN THE PAGE (1-based, 1..10) -- its high word (18 of those 22) is NOT the count ;
//   h+0x18 = (page top << 16) | rows shown -- 0 at the list's top ;
//   h+0x1C = (the same top << 16) | its end. No submenu : picking an item opens the target cursor (as a spell).
int inv_read(u32 focus, NativeList& L) {
    const u32 mgr = manager(); if (!mgr) return -1;
    u32 node = 0; safe_read(mgr + MGR_OPEN_LIST, &node); u32 obj = 0;
    for (int g = 0; valid_ptr(node) && g < 400 && !obj; ++g) {
        u32 next = 0, o = 0, def = 0; unsigned char dead = 0, cl = 0; char nm[9];
        safe_read(node + NODE_NEXT, &next); safe_read(node + NODE_VAL, &o); rd8(node + 0x14, dead);
        if (!dead && valid_ptr(o) && safe_read(o + OBJ_DEF, &def) && valid_ptr(def) && rdname(def, nm) && !strcmp(nm, "inventor")
            && rd8(o + OBJ_CLOSING, cl) && !cl) obj = o;
        node = next;
    }
    if (!obj) return -1;
    u32 hd = 0, rows = 0, h18 = 0, h20 = 0, h24 = 0;
    if (!safe_read(obj + OBJ_HANDLER, &hd) || !valid_ptr(hd) || !safe_read(hd + 0x18, &h18) || !safe_read(hd + 0x20, &h20)
        || !safe_read(hd + 0x24, &h24) || !safe_read(hd + 0x38, &rows)) return 0;
    // opened from the MAIN menu (handler +0x90 == 1 ; the action menu stays open, dimmed, under it) : items_read's, not this
    { unsigned char fromMain = 0; if (rd8(hd + 0x90, fromMain) && fromMain == 1) return -1; }
    const int n = (int)(h20 & 0xFFFF), vis = (int)(h18 & 0xFFFF), curInPage = (int)(h24 & 0xFFFF);
    if (n < 0 || n > 80 || vis < 1 || vis > 32 || (n > 0 && !valid_ptr(rows))) return 0;
    int top = (int)(h18 >> 16); const int maxTop = n > vis ? n - vis : 0;
    if (top < 0) top = 0; if (top > maxTop) top = maxTop;
    memset(&L, 0, sizeof(L));
    L.kind = 3; L.n = n; L.top = top; L.vis = vis; L.cursor = curInPage >= 1 ? top + curInPage - 1 : -1;
    for (int i = 0; i < n; ++i) {
        u32 id = 0, cnt = 0;
        if (!safe_read(rows + (u32)i * 0x54 + 0x2C, &id) || !safe_read(rows + (u32)i * 0x54 + 0x30, &cnt)) return 0;
        id &= 0xFFFF; if (!id) return 0;
        L.all[L.allN++] = (unsigned short)id;
        if (i >= top && i < top + vis && L.rows < 32) { L.id[L.rows] = (unsigned short)id; L.cnt[L.rows] = (unsigned short)(cnt > 9999 ? 9999 : cnt); ++L.rows; }
    }
    L.focused = focus == obj;
    L.open = true;
    return 1;
}

// -1 none open, 0 unreadable, 1 read
int list_read(NativeList& L) {
    memset(&L, 0, sizeof(L));
    const u32 mgr = manager(); if (!mgr) return -1;
    u32 focus = 0; const u32 fp = aio::fm_addr(aio::FM_MENU_PTR); if (fp) safe_read(fp, &focus);
    u32 node = 0; safe_read(mgr + MGR_OPEN_LIST, &node);
    u32 obj = 0; int kind = 0;
    for (int g = 0; valid_ptr(node) && g < 400; ++g) {
        u32 next = 0, o = 0, def = 0; unsigned char dead = 0, cl = 0;
        safe_read(node + NODE_NEXT, &next); safe_read(node + NODE_VAL, &o); rd8(node + 0x14, dead);
        char nm[9];
        if (!dead && valid_ptr(o) && safe_read(o + OBJ_DEF, &def) && valid_ptr(def) && rdname(def, nm) && in_list(nm, ACT_LIST_W)
            && rd8(o + OBJ_CLOSING, cl) && !cl) { obj = o; kind = !strcmp(nm, "magic") ? 0 : !strcmp(nm, "ability") ? 1 : 2; break; }
        node = next;
    }
    if (!obj) return -1;
    if (kind == 2) return mount_read(obj, focus, L);          // the same list class, its rows hold the mount id itself
    if (!(kind ? abil_slots_res() : spell_slots_res())) return 0;
    const u32 slotsVar = kind ? g_abilSlotsVar : g_spellSlotsVar, slotsOff = kind ? 0x160u : 0x80u;
    L.kind = kind;
    u32 hd = 0, items = 0, slots = 0; int n = 0, top = 0, vis = 0, cur = 0, cap = 0;
    if (!safe_read(obj + OBJ_HANDLER, &hd) || !valid_ptr(hd) || !safe_read(hd + 0x40, (u32*)&n) || !safe_read(hd + 0x38, (u32*)&top)
        || !safe_read(hd + 0x48, (u32*)&vis) || !safe_read(hd + 0x30, (u32*)&cur) || !safe_read(hd + 0x44, (u32*)&cap)
        || !safe_read(hd + 0x20, &items) || !safe_read(slotsVar, &slots)) return 0;
    if (n < 0 || n > 2048 || vis < 1 || vis > 32 || top < 0 || cap < n || !valid_ptr(slots)) return 0;
    L.n = n; L.top = top; L.vis = vis; L.cursor = cur;
    // MANUAL SORT (the main menu's Magic -> X -> Manual Sort ; live capture MSORT 2026-11-18) : h+0x6C = 0 off, 1 on,
    // 2 a spell HELD ; h+0x70 = the held one's index in the list -- validating another row puts it ABOVE that row
    { u32 mode = 0, hi = 0; L.held = -1;
      const u32 mo = kind ? 0x68u : 0x6Cu;                  // the ABILITY list : +0x68 / +0x6C (abilities-re 2026-11-19)
      if (safe_read(hd + mo, &mode) && mode == 2 && safe_read(hd + mo + 4, &hi) && (int)hi >= 0 && (int)hi < n) L.held = (int)hi; }
    if (n > 0 && !valid_ptr(items)) return 0;
    for (int i = 0; i < vis && top + i < n; ++i) {
        u32 slot = 0; short id = 0;
        if (!safe_read(items + (u32)(top + i) * 8, &slot) || slot > 4096 || !rd16(slots + slotsOff + slot * 2, id)) return 0;
        L.id[i] = (unsigned short)id; ++L.rows;
    }
    // the whole list, read again only when it CHANGES (another category, a spell learned) -- not every frame
    static u32 cItems = 0; static int cN = -1, cKind = -1; static unsigned short cAll[512]; static int cAllN = 0;
    if (items != cItems || n != cN || kind != cKind) {
        cAllN = 0;
        for (int i = 0; i < n && i < 512; ++i) {
            u32 slot = 0; short id = 0;
            if (!safe_read(items + (u32)i * 8, &slot) || slot > 4096 || !rd16(slots + slotsOff + slot * 2, id)) { cAllN = 0; break; }
            cAll[cAllN++] = (unsigned short)id;
        }
        cItems = items; cKind = kind; cN = cAllN || !n ? n : -1;   // a failed walk is retried next frame
    }
    L.allN = cAllN; memcpy(L.all, cAll, sizeof(unsigned short) * (size_t)cAllN);
    L.focused = focus == obj;
    L.open = true;
    return 1;
}

// ---- the Chat submenu's extras : found by SIGNATURE (masked AOB), globals read from the code's own operands ----
// A pattern is "8B 15 ?? ?? ..." ; unique in FFXiMain's image or nothing.
u32 aob_masked(const char* pat) {
    unsigned char P[96]; bool W[96]; int n = 0;
    for (const char* q = pat; *q && n < 96; ) {
        while (*q == ' ') ++q; if (!*q) break;
        if (q[0] == '?') { W[n] = true; P[n++] = 0; q += 2; continue; }
        unsigned v = 0; for (int i = 0; i < 2; ++i) { const char ch = q[i]; v = v * 16 + (unsigned)(ch <= '9' ? ch - '0' : (ch | 0x20) - 'a' + 10); }
        W[n] = false; P[n++] = (unsigned char)v; q += 2;
    }
    u32 lo = 0, hi = 0; image_range(lo, hi); if (!lo || n < 8) return 0;
    int hits = 0; u32 at = 0;
    __try {
        for (const unsigned char* q = (const unsigned char*)lo; q + n < (const unsigned char*)hi; ++q) {
            if (q[0] != P[0]) continue;
            int i = 1; for (; i < n; ++i) if (!W[i] && q[i] != P[i]) break;
            if (i == n) { ++hits; at = (u32)q; if (hits > 1) break; }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    return hits == 1 ? at : 0;
}
struct ChatRes { u32 invPtrVar, tellPtrVar, unityFlag, unityId, decode, str; };
ChatRes g_cr = { 0, 0, 0, 0, 0, 0 }; bool g_crOk = false; int g_crTries = 0; DWORD g_crNext = 0;
bool chat_res() {
    if (g_crOk) return true;
    const DWORD now = GetTickCount();
    if (g_crTries && (int)(now - g_crNext) < 0) return false;          // retried every 5 s, never given up
    ++g_crTries; g_crNext = now + 5000;
    const u32 ls = aob_masked("8B 15 ?? ?? ?? ?? 56 85 D2 74 44 8B 74 24 14 81 E6 FF 00 00 00 8A 8C 16 08 98 00 00 84 C9 74 2F 33 C0 81 E1 FF 00 00 00 8A 84 16 0F 98 00 00");
    const u32 un = aob_masked("8A 0D ?? ?? ?? ?? 33 C0 F6 C1 01 74 05 A1 ?? ?? ?? ?? C3 90 90 90 90 90 90 90 90 90 90 90 90 90 E8 DB FF FF FF 24 1F");
    const u32 te = aob_masked("53 56 57 8B 7C 24 10 8B F1 66 01 7E 14 66 8B 46 14 66 3D FD FF 7E 69 8A 5C 24 14 0F BF C0 83 F8 FE 74 40 83 F8 FF 74 2E 8B 0D ?? ?? ?? ?? 3B 81 88 00 00 00");
    const u32 de = aob_masked("8B 44 24 04 50 6A 15 68 ?? ?? ?? ?? E8 ?? ?? ?? ?? 83 C4 0C B8 ?? ?? ?? ?? C3");
    const u32 st = aob_masked("56 57 8B 7C 24 0C 33 F6 39 3C F5 ?? ?? ?? ?? 74 14 46 83 FE 1C 72 F1 8B 04 BD ?? ?? ?? ?? 85 C0");
    if (ls && un && te && de && st && safe_read(ls + 2, &g_cr.invPtrVar) && safe_read(un + 2, &g_cr.unityFlag)
        && safe_read(un + 14, &g_cr.unityId) && safe_read(te + 42, &g_cr.tellPtrVar)) {
        g_cr.decode = de; g_cr.str = st; g_crOk = true;
        windower::debug::log("NATIVEUI chat submenu extras resolved (try %d)", g_crTries);
    } else if (g_crTries == 1 || g_crTries % 12 == 0)
        windower::debug::log("NATIVEUI chat submenu extras NOT resolved (try %d : ls %d unity %d tell %d decode %d text %d)", g_crTries, !!ls, !!un, !!te, !!de, !!st);
    return g_crOk;
}
// ---- the Profile's MENTOR and MONSTROSITY : read where the client's own panel reads them (Ghidra 2026-11-18, the
// profile handler's draw FUN_05488F90 ; docs/game-data/native-windows.md). Found by SIGNATURE, every address taken from
// the code's own operands -- the block moved between the June and November clients -- and adopted only when the
// relations held in both clients hold : prestige = rank + 8, gate = rank - 4 + 0x1C0, the gate getter's exact bytes.
//   rank      (u32 @rankVar >> 2) & 3, named by the client's table (0 "Gla", 1 "CG", 2 "HCG") -- packet 0x063 order 3 +0x0C
//   prestige  u16 @presVar -- packet 0x063 order 3 +0x14 ;  gate byte @gateVar : 0 = the client draws no Monstrosity
//   mentor    byte *( *mentorPtrVar + mentorOff ) -- packet 0x01B +0x65 (0 none, else the flag)
// Bounded retry (every 5 s, logged at the first try and every minute), never given up (rule 10).
struct MonRes { u32 rankVar, presVar, gateVar, nameTbl, mentorPtrVar, mentorOff; };
MonRes g_mon; bool g_monOk = false, g_mentorOk = false; int g_monTries = 0; DWORD g_monNext = 0;
bool mon_res() {
    if (g_monOk && g_mentorOk) return true;
    const DWORD now = GetTickCount();
    if (g_monTries && (int)(now - g_monNext) < 0) return g_monOk || g_mentorOk;
    ++g_monTries; g_monNext = now + 5000;
    if (!g_monOk) {
        const u32 a = aob_masked("E8 ?? ?? ?? ?? 84 C0 0F 84 ?? ?? ?? ?? A1 ?? ?? ?? ?? C1 E8 02 83 E0 03 7C ?? 83 F8 02 7F ?? 8B 04 85 ?? ?? ?? ??");
        const u32 b = aob_masked("83 C4 1C 8B 15 ?? ?? ?? ?? 8D 44 24 ?? 81 E2 FF FF 00 00 52 68 ?? ?? ?? ?? 50 E8 ?? ?? ?? ?? 66 8B 56 16 66 8B 46 14");
        u32 rel = 0, rk = 0, tbl = 0, pr = 0, gate = 0;
        if (a && b && safe_read(a + 1, &rel) && safe_read(a + 0x0E, &rk) && safe_read(a + 0x22, &tbl) && safe_read(b + 5, &pr)) {
            const u32 fn = a + 5 + rel;                      // the gate getter : A0 <imm32> 84 C0 0F 95 C0 C3
            unsigned char op[11] = { 0 }; bool fnOk = true;
            for (int i = 0; i < 11 && fnOk; ++i) fnOk = rd8(fn + (u32)i, op[i]);
            if (fnOk && op[0] == 0xA0 && op[5] == 0x84 && op[6] == 0xC0 && op[7] == 0x0F && op[8] == 0x95 && op[9] == 0xC0 && op[10] == 0xC3) memcpy(&gate, op + 1, 4);
            if (gate && pr == rk + 8 && gate == rk - 4 + 0x1C0 && valid_ptr(tbl)) {
                g_mon.rankVar = rk; g_mon.presVar = pr; g_mon.gateVar = gate; g_mon.nameTbl = tbl; g_monOk = true;
                windower::debug::log("NATIVEUI profile Monstrosity resolved (try %d) : rank FFXiMain+%X, prestige +%X, gate +%X", g_monTries, rk - ffximain_base(), pr - ffximain_base(), gate - ffximain_base());
            }
        }
        if (!g_monOk && (g_monTries == 1 || g_monTries % 12 == 0))
            windower::debug::log("NATIVEUI profile Monstrosity NOT resolved (try %d : draw %d prestige %d)", g_monTries, !!a, !!b);
    }
    if (!g_mentorOk) {
        const u32 d = aob_masked("A1 ?? ?? ?? ?? 83 C4 1C 8A 80 ?? ?? ?? ?? 84 C0 76");
        u32 pv = 0, off = 0;
        if (d && safe_read(d + 1, &pv) && safe_read(d + 0x0A, &off) && valid_ptr(pv) && off > 0 && off < 0x100000) {
            g_mon.mentorPtrVar = pv; g_mon.mentorOff = off; g_mentorOk = true;
            windower::debug::log("NATIVEUI profile Mentor resolved (try %d) : *(FFXiMain+%X) + 0x%X", g_monTries, pv - ffximain_base(), off);
        } else if (g_monTries == 1 || g_monTries % 12 == 0)
            windower::debug::log("NATIVEUI profile Mentor NOT resolved (try %d : found %d)", g_monTries, !!d);
    }
    return g_monOk || g_mentorOk;
}
void mon_read(NativeProfile& P) {
    P.mentor = -1; P.monOk = false; P.gladName[0] = 0; P.prestige = 0;
    if (!mon_res()) return;
    if (g_mentorOk) {
        u32 pl = 0; unsigned char m = 0;
        if (safe_read(g_mon.mentorPtrVar, &pl) && valid_ptr(pl) && rd8(pl + g_mon.mentorOff, m) && m <= 3) P.mentor = m;
    }
    if (g_monOk) {
        unsigned char gate = 0; u32 rk = 0; short pr = 0;
        if (rd8(g_mon.gateVar, gate) && gate && safe_read(g_mon.rankVar, &rk) && rd16(g_mon.presVar, pr)) {
            const u32 r = (rk >> 2) & 3; u32 sp = 0;
            if (r <= 2 && safe_read(g_mon.nameTbl + r * 4, &sp) && valid_ptr(sp)) {
                int n = 0; for (; n < 7; ++n) { unsigned char c = 0; if (!rd8(sp + (u32)n, c) || c < 0x20 || c >= 0x7F) break; P.gladName[n] = (char)c; }
                P.gladName[n] = 0;
                P.prestige = (unsigned short)pr; P.monOk = n > 0;
            }
        }
    }
}
// ---- Status -> Job / Master Levels : the client's player object P = *(&P), what its 0x01B handler keeps (Ghidra
// 2026-11-18, docs/notes/job-levels-re-2026-11-18.md section 5) -- every offset from the code's own operands, adopted
// only when the cross-checks of both clients hold : the same &P in A and B, ML = mastered + 4, levels = block + 0x44.
//   block (raw 0x01B from +0x04) : main job +4, sub job +7, unlocked mask u32 +8 (bit 0 = the support job, ignored)
//   levels P + lvOff + j (24) ; mastered mask u32 P + masOff ; master levels P + mlOff + j
struct JobsRes { u32 pVar, blockOff, lvOff, masOff, mlOff; };
JobsRes g_jr; bool g_jrOk = false; int g_jrTries = 0; DWORD g_jrNext = 0;
bool jobs_res() {
    if (g_jrOk) return true;
    const DWORD now = GetTickCount();
    if (g_jrTries && (int)(now - g_jrNext) < 0) return false;
    ++g_jrTries; g_jrNext = now + 5000;
    const u32 a = aob_masked("A1 ?? ?? ?? ?? 85 C0 75 01 C3 05 ?? ?? ?? ?? C3 8B 0D ?? ?? ?? ?? 85 C9 74 ?? 8B 44 24 04 3C 10 73 ?? 25 FF 00 00 00 8A 84 08 ?? ?? ?? ?? C3 3C 18 73 ?? 25 FF 00 00 00 8A 84 08 ?? ?? ?? ?? C3");
    const u32 b = aob_masked("8B 0D ?? ?? ?? ?? 85 C9 74 ?? 8B 44 24 04 3C 18 73 ?? 25 FF 00 00 00 8A 84 08 ?? ?? ?? ?? C3 32 C0 C3");
    const u32 c = aob_masked("8B 15 ?? ?? ?? ?? 85 D2 74 ?? 8A 4C 24 04 80 F9 18 73 ?? B8 01 00 00 00 D3 E0 23 82 ?? ?? ?? ?? F7 D8");
    u32 pA = 0, blk = 0, lv = 0, pB = 0, ml = 0, mas = 0;
    if (a && b && c && safe_read(a + 0x01, &pA) && safe_read(a + 0x0B, &blk) && safe_read(a + 0x3B, &lv)
        && safe_read(b + 0x02, &pB) && safe_read(b + 0x1A, &ml) && safe_read(c + 0x1C, &mas)
        && pA == pB && ml == mas + 4 && lv == blk + 0x44 && valid_ptr(pA) && blk < 0x100000) {
        g_jr.pVar = pA; g_jr.blockOff = blk; g_jr.lvOff = lv; g_jr.masOff = mas; g_jr.mlOff = ml; g_jrOk = true;
        windower::debug::log("NATIVEUI job levels resolved (try %d) : *(FFXiMain+%X), block +0x%X, levels +0x%X, mastered +0x%X, ML +0x%X", g_jrTries, pA - ffximain_base(), blk, lv, mas, ml);
    } else if (g_jrTries == 1 || g_jrTries % 12 == 0)
        windower::debug::log("NATIVEUI job levels NOT resolved (try %d : A %d B %d C %d)", g_jrTries, !!a, !!b, !!c);
    return g_jrOk;
}
bool jobs_read(NativeJobs& J) {
    memset(&J, 0, sizeof(J));
    if (!jobs_res()) return false;
    u32 P = 0; if (!safe_read(g_jr.pVar, &P) || !valid_ptr(P)) return false;
    unsigned char mj = 0, sj = 0;
    if (!rd8(P + g_jr.blockOff + 4, mj) || !rd8(P + g_jr.blockOff + 7, sj) || !safe_read(P + g_jr.blockOff + 8, &J.unlocked)
        || !safe_read(P + g_jr.masOff, &J.mastered)) return false;
    for (int j = 1; j <= 22; ++j) {
        if (!rd8(P + g_jr.lvOff + (u32)j, J.lvl[j]) || !rd8(P + g_jr.mlOff + (u32)j, J.ml[j])) return false;
        if (J.lvl[j] > 99 || J.ml[j] > 50) return false;
    }
    J.mjob = mj; J.sjob = sj;
    // the client's own override : once the stats are in, the MAIN job's level and ML come from the 0x061 mirror
    if (g_persData.ok && g_persData.mjob >= 1 && g_persData.mjob <= 22) {
        J.mjob = g_persData.mjob; J.sjob = g_persData.sjob;
        J.lvl[J.mjob] = (unsigned char)g_persData.mlvl; J.ml[J.mjob] = (unsigned char)g_persData.ml;
    }
    J.ok = J.mjob >= 1 && J.mjob <= 22 && J.lvl[J.mjob] >= 1;
    return J.ok;
}
// keep printable bytes only (names are ASCII ; the client's text may carry control codes)
void copy_name(char* dst, int cap, const char* src) {
    int n = 0;
    __try { for (; src && src[n] && n < cap - 1; ++n) dst[n] = ((unsigned char)src[n] >= 0x20 && (unsigned char)src[n] < 0x7F) ? src[n] : ' '; }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
    dst[n] = 0;
}
typedef const char* (__cdecl* DecodeFn)(const unsigned char* src);
typedef const char* (__cdecl* TextFn)(int table, int id);
void chat_info_read(u32 obj) {
    NativeChatInfo c; memset(&c, 0, sizeof(c)); c.tellSel = -3;
    if (!chat_res()) { g_chat = c; return; }
    // Tell : the handler's choice, the tell history
    u32 hd = 0; short sel = -3;
    if (safe_read(obj + OBJ_HANDLER, &hd) && valid_ptr(hd)) rd16(hd + 0x14, sel);
    c.tellSel = sel;
    u32 tg = 0;
    if (safe_read(g_cr.tellPtrVar, &tg) && valid_ptr(tg)) {
        char nm[17] = { 0 };
        if (sel == -2) { __try { memcpy(nm, (const void*)(tg + 0x10E), 16); } __except (EXCEPTION_EXECUTE_HANDLER) { nm[0] = 0; } }
        else if (sel >= 0) {
            u32 cnt = 0;
            if (safe_read(tg + 0x88, &cnt) && cnt > 0 && cnt < 64) { const int i = sel >= (int)cnt ? (int)cnt - 1 : sel;
                __try { memcpy(nm, (const void*)(tg + 0x8C + i * 0x10), 16); } __except (EXCEPTION_EXECUTE_HANDLER) { nm[0] = 0; } }
        }
        copy_name(c.tell, sizeof(c.tell), nm);
    }
    // Linkshell 1 / 2 : the item worn in that slot, its colour, its shell's name (the client's own decoder)
    u32 inv = 0;
    if (ls_res() && safe_read(g_cr.invPtrVar, &inv) && valid_ptr(inv)) for (int k = 0; k < 2; ++k) {
        unsigned char idx = 0, bag = 0;
        if (!rd8(inv + g_lsr.idx + (k + 1), idx) || !idx || !rd8(inv + g_lsr.bag + (k + 1), bag)) continue;
        const u32 rec = inv + ((u32)bag * 0x51 + idx) * 0x2C + g_lsr.items;
        short id = 0; if (!rd16(rec, id) || id < 513 || id > 515) continue;
        const u32 ext = rec + 0x10;
        unsigned char b6 = 0, b7 = 0; rd8(ext + 6, b6); rd8(ext + 7, b7);
        const unsigned w6 = (unsigned)b6 | ((unsigned)b7 << 8);
        unsigned r = (w6 & 0xF) * 0xD + 0x38, g = ((w6 >> 4) & 0xF) * 0xD + 0x38, b = (b7 & 0xF) * 0xD + 0x38;   // half-intensity, as drawn
        r = r * 2 > 255 ? 255 : r * 2; g = g * 2 > 255 ? 255 : g * 2; b = b * 2 > 255 ? 255 : b * 2;
        c.lsItem[k] = id; c.lsArgb[k] = 0xFF000000u | (r << 16) | (g << 8) | b;
        const char* name = 0;
        __try { name = ((DecodeFn)g_cr.decode)((const unsigned char*)(ext + 9)); } __except (EXCEPTION_EXECUTE_HANDLER) { name = 0; }
        copy_name(c.ls[k], sizeof(c.ls[k]), name);
    }
    // Unity : the leader's name from the client's text table
    unsigned char fl = 0; u32 uid = 0;
    if (rd8(g_cr.unityFlag, fl) && (fl & 1) && safe_read(g_cr.unityId, &uid)) {
        uid &= 0x1F; if (uid > 0xB) uid = 0;
        c.unityId = (int)uid;
        if (uid) {
            const char* t = 0;
            __try { t = ((TextFn)g_cr.str)(0xC, 0x1A2 + (int)uid); } __except (EXCEPTION_EXECUTE_HANDLER) { t = 0; }
            copy_name(c.unity, sizeof(c.unity), t);
        }
    }
    c.ok = true;
    g_chat = c;
}

// ---- the client's menu EVENT function (FUN_054596F0 on the 2026-11-15 client) ----
// thiscall(menu object, event) ret 4 : 1-4 arrows, 5 validate, 6 cancel, 9 "the cursor was set" (the mouse), ...
typedef int (__thiscall* MenuEventFn)(u32 obj, int ev);
u32 g_menuEvent = 0; DWORD g_menuEventNext = 0; int g_menuEventTries = 0;
u32 menu_event_fn() {
    if (g_menuEvent) return g_menuEvent;
    const DWORD now = GetTickCount();
    if (g_menuEventTries && (int)(now - g_menuEventNext) < 0) return 0;    // a miss : retried every 5 s, never given up
    ++g_menuEventTries; g_menuEventNext = now + 5000;
    static const unsigned char SIG[21] = { 0x51, 0x55, 0x56, 0x8B, 0xF1, 0x57, 0x83, 0x7E, 0x10, 0x0E, 0x75, 0x09,
                                           0x5F, 0x5E, 0x33, 0xC0, 0x5D, 0x59, 0xC2, 0x04, 0x00 };
    const u32 at = aob_unique(SIG, 21, 0, 0);
    if (at) { g_menuEvent = at; windower::debug::log("NATIVEUI menu event function found : FFXiMain+0x%X (try %d)", at - ffximain_base(), g_menuEventTries); }
    else if (g_menuEventTries == 1 || g_menuEventTries % 12 == 0) windower::debug::log("NATIVEUI menu event function NOT found (try %d) -- the menu keeps working by keyboard / gamepad", g_menuEventTries);
    return g_menuEvent;
}
bool menu_event(u32 obj, int ev) {
    const u32 fn = menu_event_fn(); if (!fn) return false;
    __try { ((MenuEventFn)fn)(obj, ev); }
    __except (EXCEPTION_EXECUTE_HANDLER) { windower::debug::log("NATIVEUI menu event %d faulted", ev); return false; }
    return true;
}
// the menu, read NOW (not last frame's) : open, focused (no submenu on top), the row in range
u32 act_live_obj(int row) {
    NativeMenu m; u32 obj = 0;
    if (act_read(m, &obj) != 1 || !m.focused || row < 1 || row > m.n) return 0;
    return obj;
}

bool native_action_hover(int row) {
    const u32 obj = act_live_obj(row); if (!obj) return false;
    if (!wr16(obj + 0x4C, (short)row)) return false;
    return menu_event(obj, 9);
}
// the submenu, read NOW : open, focused, that entry shown and not refused
u32 sub_live_obj(int entry) {
    NativeEntryMenu m; u32 obj = 0;
    if (sub_read(m, &obj) != 1 || !m.focused) return 0;
    for (int i = 0; i < m.n; ++i) if (m.entry[i] == entry) return m.state[i] == 5 ? 0 : obj;
    return 0;
}
bool native_sub_hover(int entry) {
    const u32 obj = sub_live_obj(entry); if (!obj) return false;
    if (!wr16(obj + 0x4C, (short)entry)) return false;
    return menu_event(obj, 9);
}
bool native_sub_pick(int entry) {
    const u32 obj = sub_live_obj(entry); if (!obj) return false;
    short cur = 0; rd16(obj + 0x4C, cur);
    if (cur != entry && !native_sub_hover(entry)) return false;
    return menu_event(obj, 5);
}

// the main menu, read NOW : open, focused, that entry shown
u32 main_live_obj(int entry) {
    NativeEntryMenu m; u32 obj = 0;
    if (main_read(m, &obj) != 1 || !m.focused) return 0;
    for (int i = 0; i < m.n; ++i) if (m.entry[i] == entry) return obj;
    return 0;
}
bool native_main_hover(int entry) {
    const u32 obj = main_live_obj(entry); if (!obj) return false;
    if (!wr16(obj + 0x4C, (short)entry)) return false;
    return menu_event(obj, 9);
}
bool native_main_pick(int entry) {
    const u32 obj = main_live_obj(entry); if (!obj) return false;
    short cur = 0; rd16(obj + 0x4C, cur);
    if (cur != entry && !native_main_hover(entry)) return false;
    return menu_event(obj, 5);
}
// an entry menu named in W, read NOW : open, focused, that entry shown and not refused
u32 entry_live_obj(const char* const* W, int entry) {
    NativeEntryMenu m; u32 obj = 0;
    if (entry_read(W, m, &obj) != 1 || !m.focused) return 0;
    for (int i = 0; i < m.n; ++i) if (m.entry[i] == entry) return m.state[i] == 5 ? 0 : obj;
    return 0;
}
bool entry_hover(const char* const* W, int entry) {
    const u32 obj = entry_live_obj(W, entry); if (!obj) return false;
    if (!wr16(obj + 0x4C, (short)entry)) return false;
    return menu_event(obj, 9);
}
bool entry_pick(const char* const* W, int entry) {
    const u32 obj = entry_live_obj(W, entry); if (!obj) return false;
    short cur = 0; rd16(obj + 0x4C, cur);
    if (cur != entry && !entry_hover(W, entry)) return false;
    return menu_event(obj, 5);
}
bool native_main_sub_hover(int entry) { return entry_hover(MAIN_SUB_W, entry); }
bool native_unity_info_hover(int entry) { return entry_hover(QUEST_W, entry); }
bool native_unity_info_pick(int entry) { return entry_pick(QUEST_W, entry); }
bool native_scoption_hover(int entry) { return entry_hover(SCOPT_W, entry); }
bool native_shopmain_hover(int entry) { return g_shop.mainOpen && entry_hover(SHOPMAIN_W, entry); }
bool native_shopmain_pick(int entry) { return g_shop.mainOpen && entry_pick(SHOPMAIN_W, entry); }
const NativeShop& native_shop() { return g_shop; }
bool native_scoption_pick(int entry) { return entry_pick(SCOPT_W, entry); }
// a text list's row : its page scrolled first (codes 0x16 / 0x17, one row each -- bounded), then the hand on it (9),
// then validated (5) when asked. The list must have the focus.
bool textlist_goto(const char* name, int row, bool pick) {
    const u32 mgr = manager(); if (!mgr) return false;
    u32 focus = 0; const u32 fp = aio::fm_addr(aio::FM_MENU_PTR); if (fp) safe_read(fp, &focus);
    TextList T; if (!tl_find(mgr, name, T) || T.obj != focus || row < 0 || row >= T.count) return false;
    for (int g = 0; g < 32 && (row < T.top || row >= T.top + T.vis); ++g) {
        const int was = T.top;
        if (!menu_event(T.obj, row < T.top ? 0x16 : 0x17)) return false;
        if (!tl_find(mgr, name, T) || T.top == was) return false;          // it did not scroll : stop
    }
    if (row < T.top || row >= T.top + T.vis) return false;
    const short ent = (short)(row - T.top + 1);
    short cur = 0; rd16(T.obj + 0x4C, cur);
    if (cur != ent) { if (!wr16(T.obj + 0x4C, ent) || !menu_event(T.obj, 9)) return false; }
    return pick ? menu_event(T.obj, 5) : true;
}
bool native_textlist_hover(const char* name, int row) { return textlist_goto(name, row, false); }
// an LS item's colour from its extdata (+6 low R, high G ; +7 low B ; 0..15), as the client draws it (x13 + 0x38)
unsigned ls_rgb(const unsigned char* ext) {
    const unsigned r = ext[6] & 15, g = ext[6] >> 4, b = ext[7] & 15;
    return 0xFF000000u | ((r * 13 + 0x38) << 16) | ((g * 13 + 0x38) << 8) | (b * 13 + 0x38);
}
// the LS name packed in the extdata (+9 : 6-bit chars, 1..26 a-z, 27..52 A-Z, 0x3E space ; 0 / 0x3F / any other code
// ends it, as the client's decoder FUN_0542D790 ; up to 20 -- the last one's second byte would be ext[0x18] : 0)
void ls_name(const unsigned char* ext, char* out, int cap) {
    int o = 0;
    for (int i = 0; i < 20 && o < cap - 1; ++i) {
        const int bit = i * 6, by = 9 + bit / 8, sh = bit % 8;
        const unsigned v = ((unsigned)ext[by] << 8 | (by + 1 < 0x18 ? (unsigned)ext[by + 1] : 0u)) >> (10 - sh) & 0x3F;
        if (!v || (v > 52 && v != 0x3E)) break;
        out[o++] = v <= 26 ? (char)('a' + v - 1) : v <= 52 ? (char)('A' + v - 27) : ' ';
    }
    out[o] = 0;
}
bool ls_item(u32 P, int container, int index, unsigned short& id, unsigned char* ext) {
    if (!g_lsrOk || container < 0 || container > 17 || index < 0 || index > 80) return false;
    const u32 it = P + g_lsr.items + (u32)(container * 0x51 + index) * 0x2C;
    return rd16(it, *(short*)&id) && rdblk(it + 0x10, ext, 0x18);
}
bool ls_equipped(int n, unsigned& lsId, unsigned& rgb, char* name, int cap) {
    lsId = 0; rgb = 0; if (cap) name[0] = 0;
    if (n < 1 || n > 2 || !ls_res()) return false;
    u32 P = 0; unsigned char ix = 0, ct = 0, ext[0x18]; unsigned short id = 0;
    if (!safe_read(g_lsr.pVar, &P) || !valid_ptr(P) || !rd8(P + g_lsr.idx + (u32)n, ix) || !rd8(P + g_lsr.bag + (u32)n, ct) || !ix) return false;
    if (!ls_item(P, ct, ix, id, ext) || !id) return false;
    memcpy(&lsId, ext, 4); rgb = ls_rgb(ext); ls_name(ext, name, cap);
    return true;
}
bool ls_read(u32 mgr, u32 focus, NativeLinkshell& L) {
    memset(&L, 0, sizeof(L)); L.cursor = -1;
    TextList T; if (!tl_find(mgr, "link5", T)) return false;
    u32 rows = 0; if (!safe_read(T.h + 0x38, &rows) || (T.count > 0 && !valid_ptr(rows))) return false;
    L.n = T.count; L.top = T.top; L.vis = T.vis > 16 ? 16 : T.vis; L.cursor = T.cursor;
    L.rx0 = T.rx0; L.ry0 = T.ry0; L.rx1 = T.rx1; L.ry1 = T.ry1; L.uiW = T.uiW; L.uiH = T.uiH;
    u32 P = 0; const bool pOk = ls_res() && safe_read(g_lsr.pVar, &P) && valid_ptr(P);
    unsigned char eq[2][2] = { { 0, 0 }, { 0, 0 } };
    if (pOk) for (int n = 1; n <= 2; ++n) { rd8(P + g_lsr.idx + (u32)n, eq[n - 1][0]); rd8(P + g_lsr.bag + (u32)n, eq[n - 1][1]); }
    for (int i = 0; i < L.vis && L.top + i < L.n; ++i) {
        u32 rec = 0, col = 0;
        if (!safe_read(rows + (u32)(L.top + i) * 0x54 + 0x48, &rec) || !safe_read(rows + (u32)(L.top + i) * 0x54 + 0x0C, &col)) return false;
        NativeLsRow& r = L.row[i]; r.col = col;
        unsigned char b[0x18];
        if (!valid_ptr(rec) || !rdblk(rec, b, 0x17)) continue;
        r.kind = b[2];
        int k = 0; for (; k < 20 && b[3 + k]; ++k) r.lsName[k] = (b[3 + k] >= 0x20 && b[3 + k] < 0x7F) ? (char)b[3 + k] : '?'; r.lsName[k] = 0;
        unsigned char ext[0x18]; unsigned short id = 0;
        if (pOk && ls_item(P, b[1], b[0], id, ext)) { r.itemId = id; r.rgb = ls_rgb(ext); if (!r.lsName[0]) ls_name(ext, r.lsName, sizeof(r.lsName)); }
        for (int n = 0; n < 2; ++n) if (b[0] && eq[n][0] == b[0] && eq[n][1] == b[1]) r.slot = (unsigned char)(n + 1);
    }
    static int said = 0;
    if (said < 6) { ++said; windower::debug::log("NATIVEUI linkshell : link5 %d rows (top %d, cursor %d) ; row 0 : item %u kind %d slot %d '%s' rgb %06X colour %08X", L.n, L.top, L.cursor,
        L.row[0].itemId, L.row[0].kind, L.row[0].slot, L.row[0].lsName, L.row[0].rgb & 0xFFFFFF, L.row[0].col); }
    L.focused = focus == T.obj; L.open = true;
    return true;
}
bool comment_read(u32 mgr, u32 focus, NativeComment& K) {
    memset(&K, 0, sizeof(K));
    const u32 o = win_named(mgr, "comment"); if (!o) return false;
    u32 h = 0; unsigned char b[0x20];
    if (!safe_read(o + OBJ_HANDLER, &h) || !valid_ptr(h) || !rdblk(h, b, sizeof(b))) return false;
    K.cmMode = b[0x16]; K.cmGenre = b[0x17]; K.cmLine = (signed char)b[0x14];
    if (K.cmMode > 6) return false;
    for (int i = 0; i < 3; ++i) {
        unsigned char t[0x29]; if (!rdblk(h + 0x1A + (u32)K.cmMode * 0x7B + (u32)i * 0x29, t, sizeof(t))) return false;
        memcpy(K.cmText[i], t, 0x28); K.cmText[i][0x28] = 0;
    }
    u32 in = 0; unsigned char act = 0;
    if (safe_read(h + 0x378, &in) && valid_ptr(in) && rd8(in + 0x694, act) && act == 1) {
        u32 len = 0; safe_read(in + 0x690, &len); if (len > 0x168) len = 0x168;
        if (rdblk(in + 0x3A0, K.cmLive, len)) { K.cmLiveN = (int)len; K.typing = true; }
    }
    if (!rd16(o + OBJ_RECT, K.rx0) || !rd16(o + OBJ_RECT + 2, K.ry0) || !rd16(o + OBJ_RECT + 4, K.rx1) || !rd16(o + OBJ_RECT + 6, K.ry1)) return false;
    u32 wh = 0; if (!safe_read(mgr + 0x80, &wh)) return false;
    K.uiW = (short)(wh & 0xFFFF); K.uiH = (short)(wh >> 16);
    if (K.uiW < 320 || K.uiH < 240 || K.uiW > 8000 || K.uiH > 8000) return false;
    if (K.ry0 >= K.uiH) { K.ry0 = (short)(K.ry0 - HIDE_DY); K.ry1 = (short)(K.ry1 - HIDE_DY); }
    K.focused = focus == o; K.open = true;
    return true;
}
// tskill1's entries : the hand on it (9), then validated (5) ; tskill1 must have the focus, a refused entry is not picked
bool synth_goto(int entry, bool pick) {
    const u32 mgr = manager(); if (!mgr || entry < 1 || entry > 10) return false;
    u32 focus = 0; const u32 fp = aio::fm_addr(aio::FM_MENU_PTR); if (fp) safe_read(fp, &focus);
    const u32 to = win_named(mgr, "tskill1"); if (!to || to != focus) return false;
    if (entry_state(to, entry) == 5) return false;
    short cur = 0; rd16(to + 0x4C, cur);
    if (cur != entry && (!wr16(to + 0x4C, (short)entry) || !menu_event(to, 9))) return false;
    return pick ? menu_event(to, 5) : true;
}
bool native_synth_grid_hover(int entry) { return synth_goto(entry, false); }
bool native_synth_grid_pick(int entry) { return synth_goto(entry, true); }
// a Celestite list's row : the other list handed the focus first (code 8 switches them), then its row
bool cel_goto(int list, int row, bool pick) {
    const u32 mgr = manager(); if (!mgr) return false;
    u32 focus = 0; const u32 fp = aio::fm_addr(aio::FM_MENU_PTR); if (fp) safe_read(fp, &focus);
    const char* want = list ? "fe_inven" : "fe_equip"; const char* other = list ? "fe_equip" : "fe_inven";
    TextList T; if (!tl_find(mgr, want, T)) return false;
    if (T.obj != focus) {
        TextList O; if (!tl_find(mgr, other, O) || O.obj != focus || !menu_event(O.obj, 8)) return false;
    }
    return textlist_goto(want, row, pick);
}
bool native_celestite_hover(int list, int row) { return cel_goto(list, row, false); }
// the equipment grid : its cell (entry 1..16) under the hand (9), validated (5) -- the grid must have the focus
bool equip_goto(int entry, bool pick) {
    const u32 mgr = manager(); if (!mgr || entry < 1 || entry > 16) return false;
    u32 focus = 0; const u32 fp = aio::fm_addr(aio::FM_MENU_PTR); if (fp) safe_read(fp, &focus);
    u32 node = 0; safe_read(mgr + MGR_OPEN_LIST, &node); u32 obj = 0;
    for (int g = 0; valid_ptr(node) && g < 400 && !obj; ++g) {
        u32 next = 0, o = 0, def = 0; unsigned char dead = 0, cl = 0; char nm[9];
        safe_read(node + NODE_NEXT, &next); safe_read(node + NODE_VAL, &o); rd8(node + 0x14, dead);
        if (!dead && valid_ptr(o) && safe_read(o + OBJ_DEF, &def) && valid_ptr(def) && rdname(def, nm) && !strcmp(nm, "equip") && rd8(o + OBJ_CLOSING, cl) && !cl) obj = o;
        node = next;
    }
    if (!obj || obj != focus) return false;
    short cur = 0; rd16(obj + 0x4C, cur);
    if (cur != entry) { if (!wr16(obj + 0x4C, (short)entry) || !menu_event(obj, 9)) return false; }
    return pick ? menu_event(obj, 5) : true;
}
bool native_equip_hover(int entry) { return equip_goto(entry, false); }
bool native_equip_pick(int entry) { return equip_goto(entry, true); }
bool native_celestite_pick(int list, int row) { return cel_goto(list, row, true); }
bool native_jp_cat_tab(int dir) {                          // codes 3 / 4 : the client's left / right (its tabs)
    const u32 mgr = manager(); if (!mgr) return false;
    u32 focus = 0; const u32 fp = aio::fm_addr(aio::FM_MENU_PTR); if (fp) safe_read(fp, &focus);
    TextList T; if (!tl_find(mgr, "jbpcat", T) || T.obj != focus) return false;
    return menu_event(T.obj, dir < 0 ? 3 : 4);
}
bool native_textlist_pick(const char* name, int row) { return textlist_goto(name, row, true); }
bool native_main_sub_pick(int entry) { return entry_pick(MAIN_SUB_W, entry); }
bool native_main_sort_hover(int entry) { return entry_hover(MAIN_SORT_W, entry); }
bool native_main_sort_pick(int entry) { return entry_pick(MAIN_SORT_W, entry); }
bool native_main_yn_hover(int entry) { return entry_hover(MAIN_YN_W, entry); }
bool native_main_yn_pick(int entry) { return entry_pick(MAIN_YN_W, entry); }

bool native_action_pick(int row) {
    const u32 obj = act_live_obj(row); if (!obj) return false;
    short cur = 0; rd16(obj + 0x4C, cur);
    if (cur != row && !native_action_hover(row)) return false;
    return menu_event(obj, 5);
}

// ---- the EXPANDED log "fulllog" : open, focus, its tab and scroll (native_ui.h) ----
NativeFullLog g_full = { false, false, 0, 0, 0, -1 };
const NativeFullLog& native_fulllog() { return g_full; }
static void fulllog_read(u32 mgr) {
    NativeFullLog F = { false, false, 0, 0, 0, -1 };
    u32 focus = 0; const u32 fp = aio::fm_addr(aio::FM_MENU_PTR); if (fp) safe_read(fp, &focus);
    {   // WHICH window holds the focus : its name (the small log windows light AioHUD's boxes up)
        char fn[9] = { 0 }; u32 def = 0;
        if (valid_ptr(focus) && safe_read(focus + OBJ_DEF, &def) && valid_ptr(def) && rdname(def, fn)) {
            if (!strcmp(fn, "logwindo")) F.logFocus = 0; else if (!strcmp(fn, "logwin2")) F.logFocus = 1;
        } else fn[0] = 0;
        // Closing the EXPANDED log hands the focus back to the small one (fulllog -> logwindo ; one more cancel -> none,
        // measured 2026-11-16). For the player that cancel CLOSED the chat : no highlight until it is selected anew.
        static bool fromFull = false; static char prevFn[9] = { 0 };
        if (strcmp(fn, prevFn)) { fromFull = F.logFocus >= 0 && !strcmp(prevFn, "fulllog"); lstrcpynA(prevFn, fn, sizeof(prevFn)); }
        if (fromFull) F.logFocus = -1;
        static char last[9] = { 1 }; static int said = 0;   // every change, bounded (a menu-heavy session must not flood)
        if (strcmp(fn, last) && said < 300) { ++said; lstrcpynA(last, fn, sizeof(last)); windower::debug::log("NATIVEUI focus -> '%s'", fn[0] ? fn : "(none)"); }
    }
    u32 node = 0; safe_read(mgr + MGR_OPEN_LIST, &node);
    for (int g = 0; valid_ptr(node) && g < 400; ++g) {
        u32 next = 0, o = 0, def = 0; unsigned char dead = 0, cl = 0; char nm[9];
        safe_read(node + NODE_NEXT, &next); safe_read(node + NODE_VAL, &o); rd8(node + 0x14, dead);
        if (!dead && valid_ptr(o) && safe_read(o + OBJ_DEF, &def) && valid_ptr(def) && rdname(def, nm) && !strcmp(nm, "fulllog")
            && rd8(o + OBJ_CLOSING, cl) && !cl) {
            u32 hd = 0; int tab = 0, back = 0, top = 0;
            if (safe_read(o + OBJ_HANDLER, &hd) && valid_ptr(hd) && safe_read(hd + 0x54, (u32*)&tab) && safe_read(hd + 0x20, (u32*)&back)
                && safe_read(hd + 0x1C, (u32*)&top) && tab >= 0 && tab <= 10 && back >= 0 && back < 100000) {
                F.open = true; F.focused = focus == o; F.tab = tab; F.back = back; F.top = top;
            }
            break;
        }
        node = next;
    }
    static bool was = false;
    if (F.open != was) { was = F.open; windower::debug::log("NATIVEUI expanded log %s (tab %d)", F.open ? "opened" : "closed", F.tab); }
    g_full = F;
}

// ---- the NPC windows (NU_NPC) : dialogue rem4line / rem4li2 and answers query (native_ui.h) ----
const NativeNpc& native_npc() { return g_npc; }
namespace {
// glyphs -> ASCII : one u16 per character, code = ASCII - 0x20 ; >= 0x5F a control (dropped). Returns false on a fault.
bool npc_glyphs(u32 p, int n, char* out, int cap, unsigned short* ctl = 0) {
    out[0] = 0; if (n < 0 || n > 0x102) return false;
    unsigned short g[0x102];
    __try { memcpy(g, (const void*)p, (size_t)n * 2); } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    int o = 0, nc = 0;
    for (int i = 0; i < n && o < cap - 1; ++i) {
        if (g[i] < 0x5F) out[o++] = (char)(g[i] + 0x20);
        else if (ctl && nc < 4) ctl[nc++] = g[i];
    }
    while (o > 0 && out[o - 1] == ' ') --o;
    out[o] = 0; return true;
}
// the open, not closing object of one of these windows (0 none)
u32 open_obj(u32 mgr, const char* const* names, char* nameOut = 0) {
    u32 node = 0; safe_read(mgr + MGR_OPEN_LIST, &node);
    for (int g = 0; valid_ptr(node) && g < 400; ++g) {
        u32 next = 0, o = 0, def = 0; unsigned char dead = 0, cl = 0; char nm[9];
        safe_read(node + NODE_NEXT, &next); safe_read(node + NODE_VAL, &o); rd8(node + 0x14, dead);
        if (!dead && valid_ptr(o) && safe_read(o + OBJ_DEF, &def) && valid_ptr(def) && rdname(def, nm) && in_list(nm, names)
            && rd8(o + OBJ_CLOSING, cl) && !cl) { if (nameOut) memcpy(nameOut, nm, 9); return o; }
        node = next;
    }
    return 0;
}
const char* const NPC_TALK_W[] = { "rem4line", "rem4li2", 0 };
const char* const NPC_QUERY_W[] = { "query", 0 };
// the dialogue : -1 none shown, 0 unreadable, 1 read. Handler : +0x24 lines held, +0x58 the first shown (list node),
// a line value : +0x04 glyphs, +0x208 u16 their count (the leading controls counted).
int npc_talk_read(u32 mgr, NativeNpc& N) {
    const u32 obj = open_obj(mgr, NPC_TALK_W); if (!obj) return -1;
    u32 hd = 0, cnt = 0, node = 0;
    if (!safe_read(obj + OBJ_HANDLER, &hd) || !valid_ptr(hd) || !safe_read(hd + 0x24, &cnt) || !safe_read(hd + 0x58, &node)) return 0;
    if (cnt == 0 || cnt > 4096 || !node) return -1;                  // open between two pages : nothing to show
    for (int g = 0; N.nLines < 4 && valid_ptr(node) && g < 16; ++g) {
        u32 val = 0, next = 0; unsigned char dead = 0; short len = 0;
        if (!safe_read(node + NODE_VAL, &val) || !safe_read(node + NODE_NEXT, &next) || !rd8(node + 0x14, dead)) return 0;
        if (!dead && valid_ptr(val)) {
            unsigned short g0 = 0;
            if (!rd16(val + 0x208, len) || len < 0 || len > 0x102 || !rd16(val + 4, (short&)g0)) return 0;
            if (!npc_glyphs(val + 4, len, N.line[N.nLines], sizeof(N.line[0]))) return 0;
            N.lead[N.nLines] = g0 == 0xFEFF;
            if (N.line[N.nLines][0]) ++N.nLines;
        }
        node = next;
    }
    N.talk = N.nLines > 0;
    return N.talk ? 1 : -1;
}
// the answers : -1 none, 0 unreadable, 1 read. Handler : +0x14 the choice list (+0x24 count), +0x30 selected, +0x32 top,
// +0x36 / +0x38 the question's length / glyphs ; a choice value : +0x04 glyphs, +0x106 u8 their count.
int npc_query_read(u32 mgr, NativeNpc& N, u32* objOut = 0) {
    if (objOut) *objOut = 0;
    const u32 obj = open_obj(mgr, NPC_QUERY_W); if (!obj) return -1;
    u32 hd = 0, cnt = 0, node = 0; short sel = 0, top = 0, qn = 0;
    if (!safe_read(obj + OBJ_HANDLER, &hd) || !valid_ptr(hd) || !safe_read(hd + 0x24, &cnt) || !safe_read(hd + 0x14, &node)
        || !rd16(hd + 0x30, sel) || !rd16(hd + 0x32, top) || !rd16(hd + 0x36, qn)) return 0;
    if (cnt == 0) return -1;                                          // answered, closing
    if (cnt > 64 || sel < 0 || top < 0 || qn < 0 || qn > 0x100) return 0;
    if (!npc_glyphs(hd + 0x38, qn, N.question, sizeof(N.question))) return 0;
    for (int g = 0; N.n < 32 && valid_ptr(node) && g < 80; ++g) {
        u32 val = 0, next = 0; unsigned char dead = 0, len = 0;
        if (!safe_read(node + NODE_VAL, &val) || !safe_read(node + NODE_NEXT, &next) || !rd8(node + 0x14, dead)) return 0;
        if (!dead && valid_ptr(val)) {
            if (!rd8(val + 0x106, len) || len > 0x7E || !npc_glyphs(val + 4, len, N.choice[N.n], sizeof(N.choice[0]), N.ctl[N.n])) return 0;
            ++N.n;
        }
        node = next;
    }
    if (N.n < 1) return 0;
    N.sel = sel; N.top = top;
    {   // the item description opened WITH the answers (a shop list) : the item it shows
        static const char* const II[] = { "iteminfo", 0 };
        const u32 io = open_obj(mgr, II); u32 ih = 0; short id = 0;
        if (io && safe_read(io + OBJ_HANDLER, &ih) && valid_ptr(ih) && rd16(ih + 0x24, id)) N.itemId = (unsigned short)id;
    }
    u32 focus = 0; const u32 fp = aio::fm_addr(aio::FM_MENU_PTR); if (fp) safe_read(fp, &focus);
    N.queryFocused = focus == obj;
    N.query = true;
    if (objOut) *objOut = obj;
    return 1;
}
void npc_poll(bool on) {
    if (!on) { memset(&g_npc, 0, sizeof(g_npc)); g_npcBad = 0; g_npcFail = false; return; }
    const u32 mgr = manager();
    static NativeNpc N; memset(&N, 0, sizeof(N));
    const int rt = mgr ? npc_talk_read(mgr, N) : -1;
    const int rq = mgr ? npc_query_read(mgr, N) : -1;
    if (rt == 0) { N.talk = false; N.nLines = 0; }
    if (rq == 0) { N.query = false; N.n = 0; }
    g_npc = N;
    const DWORD now = GetTickCount();
    if (rt == 0 || rq == 0) {   // shown by the client but not readable : never leave the player without its NPC text
        if (++g_npcBad == 3 && !g_npcFail) { g_npcFail = true; windower::debug::log("NATIVEUI NPC windows : unreadable (talk %d, query %d) -- the client's own are shown", rt, rq); }
        g_npcGoodSince = 0;
        g_npc.fail = g_npcFail;
        return;
    }
    g_npcBad = 0;
    if (g_npcFail && (rt == 1 || rq == 1)) {
        if (!g_npcGoodSince) g_npcGoodSince = now;
        else if (now - g_npcGoodSince >= 5000) { g_npcFail = false; windower::debug::log("NATIVEUI NPC windows : read again -- AioHUD's back"); }
    }
    g_npc.fail = g_npcFail;                                   // during the 5 s recovery the client's are still the ones shown
    static bool wasT = false, wasQ = false;   // the success path, logged (rule 10) : one line per open
    if (N.talk != wasT) { wasT = N.talk; if (N.talk) windower::debug::log("NATIVEUI NPC dialogue : %d line(s), first '%.60s'", N.nLines, N.line[0]); }
    if (N.query != wasQ) {
        wasQ = N.query;
        if (N.query) {
            windower::debug::log("NATIVEUI NPC question '%.60s' : %d choice(s)", N.question, N.n);
            // the control glyphs of each answer (the client's colours, being mapped 2026-11-17) : only answers that carry some
            for (int i = 0; i < N.n && i < 32; ++i) if (N.ctl[i][0])
                windower::debug::log("NATIVEUI NPC   answer %d '%.40s' controls %04X %04X %04X %04X", i, N.choice[i], N.ctl[i][0], N.ctl[i][1], N.ctl[i][2], N.ctl[i][3]);
        }
    }
}
// the answers, read NOW, open and driven by the player
u32 npc_live_query(NativeNpc& N) {
    const u32 mgr = manager(); if (!mgr) return 0;
    memset(&N, 0, sizeof(N)); u32 obj = 0;
    if (npc_query_read(mgr, N, &obj) != 1 || !N.queryFocused) return 0;
    return obj;
}
} // namespace

bool native_npc_hover(int choice) {
    NativeNpc N; const u32 obj = npc_live_query(N);
    if (!obj || choice < 0 || choice >= N.n) return false;
    if (N.sel == choice) return true;
    if (choice >= N.top && choice < N.top + 3) {                     // on the client's page : its mouse hover (event 9)
        if (!wr16(obj + 0x4C, (short)(choice - N.top + 1))) return false;
        return menu_event(obj, 9);
    }
    // off its page : step there with the arrows (1 down, 2 up), the client scrolling as it does for the keys
    for (int g = 0; g < 40 && N.sel != choice; ++g) {
        if (!menu_event(obj, choice > N.sel ? 1 : 2)) return false;
        NativeNpc M; if (npc_live_query(M) != obj) return false;
        if (M.sel == N.sel) return false;                             // the client did not move : stop, never loop
        N = M;
    }
    return N.sel == choice;
}
bool native_npc_pick(int choice) {
    if (!native_npc_hover(choice)) return false;
    NativeNpc N; const u32 obj = npc_live_query(N);
    if (!obj || N.sel != choice || choice < N.top || choice >= N.top + 3) return false;
    if (!wr16(obj + 0x4C, (short)(choice - N.top + 1))) return false;   // validate reads the row : selected = top + row - 1
    return menu_event(obj, 5);
}
void native_npc_box(float x, float y, float w, float h, bool card) {
    g_npcBoxR[0] = x; g_npcBoxR[1] = y; g_npcBoxR[2] = w; g_npcBoxR[3] = h; g_npcCard = card && w > 0.0f;
    if (w > 0.0f) g_npcBoxMs = GetTickCount();
}
bool native_npc_next() {
    const u32 mgr = manager(); if (!mgr) return false;
    const u32 obj = open_obj(mgr, NPC_TALK_W); if (!obj) return false;
    return menu_event(obj, 5);
}

// ---- the auto-translate picker `fep` (native_ui.h ; live 2026-11-17, //aio feplog) ----
const NativeFep& native_fep() { return g_fep; }
void native_fep_drawn() { g_fepDrawnMs = GetTickCount(); }
namespace {
const char* const FEP_W[] = { "fep", 0 };
// cp932 bytes -> UTF-8 (the client's text : ASCII, its 【】 brackets, Japanese on a JP client)
void fep_utf8(const unsigned char* s, int n, char* out, int cap) {
    out[0] = 0; if (n <= 0) return;
    wchar_t w[200]; const int wn = MultiByteToWideChar(932, 0, (const char*)s, n, w, 199);
    if (wn <= 0) return;
    const int o = WideCharToMultiByte(CP_UTF8, 0, w, wn, out, cap - 1, 0, 0);
    out[o > 0 ? o : 0] = 0;
}
// -1 not open, 0 open but unreadable, 1 read. objOut : the open object.
int fep_read(u32 mgr, NativeFep& F, u32* objOut = 0) {
    if (objOut) *objOut = 0;
    const u32 obj = open_obj(mgr, FEP_W); if (!obj) return -1;
    u32 hd = 0, back = 0, in = 0, cnt = 0, sel = 0, items = 0; unsigned char on = 0, ext = 0;
    if (!safe_read(obj + OBJ_HANDLER, &hd) || !valid_ptr(hd) || !safe_read(hd + 0x08, &back) || back != obj) return 0;
    if (!safe_read(hd + 0x14, &in) || !valid_ptr(in) || !rd8(in + 0x8C81, on)) return 0;
    g_fepIn = in;                                                    // the input system : read alone while the window is shut
    if (!on) return -1;                                               // validated / escaped : closing
    if (!safe_read(in + 0x8C84, &sel) || !safe_read(hd + 0x1C, &cnt) || !rd8(hd + 0x21F8, ext)) return 0;
    if (cnt == 0) return -1;
    if (cnt > 4096 || sel >= cnt) return 0;
    if (ext) { if (!safe_read(hd + 0x21E8, &items) || !valid_ptr(items)) return 0; } else items = hd + 0x20;
    F.count = (int)cnt; F.sel = (int)sel; F.top = (F.sel / 7) * 7;
    F.rows = F.count - F.top < 7 ? F.count - F.top : 7;
    { const u32 pa = ffximain_base() ? ffximain_base() + 0x663F10 : 0; unsigned char t[40] = { 0 };   // 0x059A3F10 - 0x05340000
      if (pa) { __try { memcpy(t, (const void*)pa, 39); } __except (EXCEPTION_EXECUTE_HANDLER) { t[0] = 0; } }
      int n = 0; while (n < 39 && t[n] >= 0x20 && t[n] < 0x7F) ++n;
      memcpy(F.prefix, t, (size_t)n); F.prefix[n] = 0; }
    // the TOP level, nothing typed : every row is a category (live 2026-11-17 : 43 "Key Items" / 44 "Items" carry
    // neither the brackets nor a category id -- they open the item search)
    u32 path = 1; safe_read(in + 0xEC2C, &path);
    const bool allCats = path == 0 && !F.prefix[0];
    for (int i = 0; i < F.rows; ++i) {
        const u32 it = items + (u32)(F.top + i) * 0x10; u32 tp = 0, len = 0, id = 0;
        if (!safe_read(it + 4, &tp) || !safe_read(it + 8, &len) || !safe_read(it + 0xC, &id) || !valid_ptr(tp) || len > 190) return 0;
        unsigned char b[192] = { 0 };
        __try { memcpy(b, (const void*)tp, len); } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
        int a = (int)len, sb = -1;                                    // the main part ; the greyed part after 0x06 / a NUL
        for (int j = 0; j < (int)len; ++j) if (b[j] == 0x06 || b[j] == 0) { a = j; sb = j + 1; break; }
        const unsigned char* m = b; int mn = a;
        if (mn >= 4 && m[0] == 0x81 && m[1] == 0x79 && m[mn - 2] == 0x81 && m[mn - 1] == 0x7A) { F.cat[i] = true; m += 2; mn -= 4; }
        // 〔Key Items〕 (id 00000213) / 〔Items〕 (00000207) : the item searches, in the other brackets (live 2026-11-17)
        else if (mn >= 4 && m[0] == 0x81 && m[1] == 0x6B && m[mn - 2] == 0x81 && m[mn - 1] == 0x6C) { F.cat[i] = true; m += 2; mn -= 4; }
        // the auto-translate id (lang, 02, category, index) : index 0 = the CATEGORY itself (live : 00010202 Greetings ..
        // 00150202 ; a phrase 0C020202). The last two of the list (43 / 44, 2026-11-17) carry 〔〕, handled above.
        if (!F.cat[i] && (allCats || ((id & 0xFFFF) == 0x0202 && (id >> 24) == 0 && ((id >> 16) & 0xFF) != 0))) {
            F.cat[i] = true;
            static int said = 0;
            if (said < 8) { ++said; char hx[100]; int o = 0; for (int j = 0; j < (int)len && j < 30; ++j) o += _snprintf(hx + o, sizeof(hx) - o, "%02X", b[j]);
                            windower::debug::log("NATIVEUI auto-translate picker : category %08X without brackets, %u byte(s) %s", id, len, hx); }
        }
        fep_utf8(m, mn, F.row[i], sizeof(F.row[0]));
        if (sb >= 0 && sb < (int)len) fep_utf8(b + sb, (int)len - sb, F.sub[i], sizeof(F.sub[0]));
    }
    u32 focus = 0; const u32 fp = aio::fm_addr(aio::FM_MENU_PTR); if (fp) safe_read(fp, &focus);
    F.focused = focus == obj;
    F.open = true;
    if (objOut) *objOut = obj;
    return 1;
}
void fep_poll(bool on) {
    if (!on) { memset(&g_fep, 0, sizeof(g_fep)); return; }
    const u32 mgr = manager();
    static NativeFep F; memset(&F, 0, sizeof(F));
    const int r = mgr ? fep_read(mgr, F) : -1;
    if (r == 0) {                                                     // shown by the client, not readable : the client's own
        memset(&F, 0, sizeof(F)); F.fail = true;
        if (g_fepBadLog < 20) { ++g_fepBadLog; windower::debug::log("NATIVEUI auto-translate picker : unreadable -- the client's own is brought back"); }
    }
    static bool was = false;
    if (F.open != was) { was = F.open; if (F.open) windower::debug::log("NATIVEUI auto-translate picker : %d row(s), prefix '%s', first '%.40s'%s", F.count, F.prefix, F.row[0], F.cat[0] ? " (categories)" : ""); }
    if (F.open) F.picking = true;
    else if (g_fepIn) { unsigned char on2 = 0; F.picking = valid_ptr(g_fepIn) && rd8(g_fepIn + 0x8C81, on2) && on2 != 0; }
    g_fep = F;
}
u32 fep_live(NativeFep& F) {
    const u32 mgr = manager(); if (!mgr) return 0;
    memset(&F, 0, sizeof(F)); u32 obj = 0;
    if (fep_read(mgr, F, &obj) != 1 || !F.focused) return 0;
    return obj;
}
} // namespace
bool native_fep_hover(int idx) {
    NativeFep F; const u32 obj = fep_live(F);
    if (!obj || idx < F.top || idx >= F.top + F.rows) return false;
    if (F.sel == idx) return true;
    if (!wr16(obj + 0x4C, (short)(idx - F.top + 2))) return false;  // the client's rows are 2..8 (1 = its counter)
    return menu_event(obj, 9);
}
bool native_fep_pick(int idx) {
    if (!native_fep_hover(idx)) return false;
    NativeFep F; const u32 obj = fep_live(F);
    if (!obj || F.sel != idx) return false;
    return menu_event(obj, 5);
}
bool native_fep_step(int d) {
    NativeFep F; const u32 obj = fep_live(F);
    return obj && menu_event(obj, d > 0 ? 1 : 2);
}

void native_ui_tick(bool inWorld) {
    const UiConfig& c = ui_config();
    g_full.open = false; g_full.logFocus = -1;             // read again below this frame, or not open (never stale)
    if (!(inWorld && (c.nuHide & NU_ACT))) { g_act.open = false; g_sub.open = false; g_list.open = false; }   // never a stale menu once the option is off / logged out
    if (!(inWorld && (c.nuHide & NU_NPC))) { g_npc.talk = false; g_npc.query = false; }
    if (!(inWorld && (c.nuHide & NU_MAIN))) { g_mainM.open = false; g_mainSub.open = false; g_mainSort.open = false; }
    // only while a character is in the world : the title, login and char-select menus are the client's own business
    const bool docking = c.chDock && c.chShow[0] && (c.chDock == 2 || c.chDockRect[2] > 0.0f);   // the game's input line : under / inside AioHUD's chat box
    const bool wanted = inWorld && (c.nuHide != 0 || c.nuMenuDx != 0 || c.nuMenuDy != 0 || docking);
    // A crashed session's undo file must be honoured even when nothing is configured any more -- otherwise the
    // early-out below would never resolve, and the records it names would stay shifted until the client restarts.
    static int pendingRecovery = -1;
    if (pendingRecovery < 0) pendingRecovery = (GetFileAttributesA(undo_path()) != INVALID_FILE_ATTRIBUTES) ? 1 : 0;
    if (pendingRecovery == 1 && g_recovered) pendingRecovery = 0;
    if (!wanted && !g_anyApplied && pendingRecovery == 0) {   // the normal case for most players : two compares
        const bool configured = c.nuHide != 0 || c.nuMenuDx != 0 || c.nuMenuDy != 0;
        const char* why = configured ? "waiting : no character logged in (title / char-select stay native)" : "not needed (nothing hidden or moved)";
        if (g_lastSig != 0 || strcmp(g_why, why)) { g_lastSig = 0; lstrcpynA(g_why, why, sizeof(g_why)); }
        return;
    }
    if (!resolve()) { g_mainM.open = false; g_mainSub.open = false; g_mainSort.open = false; g_act.open = false; g_sub.open = false; g_list.open = false; g_npc.talk = false; g_npc.query = false; g_fep.open = false; g_tb.ok = false; g_mc.ok = false; g_mc.open = 0; g_net.ok = false; g_trade.open = false; g_dlg.open = false; return; }        // (resolve() runs the crash recovery once, when it succeeds)
    if (inWorld) fulllog_read(manager()); else g_full.open = false;   // the expanded log : AioHUD's Discussion box unfolds
    act_poll(wanted && (c.nuHide & NU_ACT));
    main_poll(wanted && (c.nuHide & NU_MAIN));
    if (wanted) item_extra_poll(manager()); else g_iix[0].id = g_iix[1].id = 0;   // the item cards' page 2
    npc_poll(wanted && (c.nuHide & NU_NPC));
    topbar_poll(wanted && (c.nuHide & NU_HELP));          // the top bar (menu name + help) : AioHUD's own
    macro_poll(wanted && (c.nuHide & NU_MACRO), manager());   // the Ctrl / Alt macro bar : AioHUD's own
    net_poll(wanted && (c.nuHide & NU_NET));                                              // the connection box (netstat) : AioHUD's own
    buffwin_poll(wanted && (c.nuHide & NU_BUFF), manager());   // your buff icons : AioHUD's own, where the client puts them
    if (wanted && (eff_hide(c) & NU_BUFF)) ensure_event_hook();   // the buff cursor's left / right walk AioHUD's order
    trade_poll(wanted && (c.nuHide & NU_TRADE), manager());   // the player trade : AioHUD's own
    dialog_poll(wanted && (c.nuHide & NU_DIALOG), manager());  // the Yes / No confirms : AioHUD's own
    shop_poll(wanted && (c.nuHide & NU_NPC), manager());       // the NPC shop : Buy / Sell, its list, the quantity
    if (wanted && (c.nuHide & NU_NPC) && g_shop.mainOpen) {
        static int smLog = 0, slLog = 0, sqLog = 0, siLog = 0, sgLog = 0;
        park_named(manager(), "shopmain", smLog, "Shop -> Buy / Sell");
        if (g_shop.open) {
            park_named(manager(), "shop", slLog, "Shop -> the list");
            park_named(manager(), "iteminfo", siLog, "Shop -> the item card");
            park_named(manager(), "money", sgLog, "Shop -> Current Gil");
            if (g_shop.qtyOk) park_named(manager(), "itemctrl", sqLog, "Shop -> the quantity");
        }
    }
    if (wanted && (eff_hide(c) & NU_DIALOG)) ensure_event_hook();   // the confirms' left / right (the same event hook as the trade)
    if (wanted && (eff_hide(c) & NU_TRADE) && g_trade.open) {   // ... and the boxes it opens : AioHUD draws them (the user 2026-11-20 : they still popped)
        static int tgLog = 0, tqLog = 0, tiLog = 0;
        park_named(manager(), "moneyctr", tgLog, "Trade -> the gil");
        park_named(manager(), "itemctrl", tqLog, "Trade -> the quantity");
        park_named(manager(), "iteminfo", tiLog, "Trade -> the item card");
        static int tmLog = 0; park_named(manager(), "money", tmLog, "Trade -> Current Gil");
    }
    fep_poll(wanted && docking);                        // the input line in AioHUD's box : its auto-translate picker too
    // the HIDDEN HUD-like windows, EVERY frame (not only in the once-a-second walk below) : the client re-places some on
    // its own -- partywin at each member who joins (y 1176 -> 1156 -> 1136 ...), shown up to a second before the walk
    // put it back (2026-11-17, "quand quelqu'un pop dans la party ca la fait pop une seconde"). heal_hud only acts on a
    // window shown where it should be hidden (or the reverse) : on a quiet frame it is one walk of the open list.
    if (wanted && g_anyApplied && (c.nuHide & (NU_PARTY | NU_ALLY | NU_CHAT | NU_HELP | NU_COST | NU_NET | NU_BUFF))) heal_hud(manager());
    if (wanted && (c.nuHide & NU_ACT)) inv_park(manager());          // the Items list : the client's own window out of the way
    if (wanted && (c.nuHide & NU_MAIN)) yn_park(manager());          // Auto-sort's Yes / No (from the spell list's sort options)
    if (wanted && (eff_hide(c) & (NU_ACT | NU_NPC | NU_MAIN))) rescue_children(manager(), eff_hide(c));
    if (!wanted && !g_anyApplied) return;                  // recovery done, nothing configured
    const int sig = wanted ? config_sig(c) : 0;
    const DWORD now = GetTickCount();
    if (sig != g_lastSig || (int)(now - g_nextWalk) >= 0) {   // on a change at once, otherwise once a second
        g_lastSig = sig; g_nextWalk = now + 1000;           // (new definitions load lazily ; a relog rewrites records)
        apply(!wanted);
    }
}

void native_ui_shutdown() {
    hook_off(g_hkEvent);      // the trade's lines : ALWAYS off at an unload (a JMP into an unloaded DLL would crash the client)
    if (!g_anyApplied && !g_hkMove.on) return;
    apply(true);              // records back, open windows back by their delta, hook removed, undo file deleted
    hook_remove();
    windower::debug::log("NATIVEUI shutdown : every native window restored");
}

// The client's chat INPUT text, read the way its own draw reads it (FUN_054F95A0, inline handler vtable slot 4) :
// (*(handler+0x1C))->vtbl[4](buf, 0x168) on an "FsTextInput" object, thiscall, returns the length. The text carries
// the caret as 7F FF, a leading 7F FE, auto-translate as 7F 69 .. 7F 64, a Tab candidate as 7F 66 .. (live
// 2026-11-15). The getter and its vtable must sit in FFXiMain's image, or nothing is called.
typedef int (__thiscall* FsGetTextFn)(u32 self, char* buf, int cap);
int native_input_read(char* buf, int cap, int* mode) {
    if (!buf || cap < 2) return -2;
    buf[0] = 0; if (mode) *mode = -1;
    const u32 mgr = manager(); if (!mgr) return -1;
    u32 lo = 0, hi = 0; image_range(lo, hi); if (!lo) return -2;
    u32 node = 0; safe_read(mgr + MGR_OPEN_LIST, &node);
    for (int g = 0; valid_ptr(node) && g < 400; ++g) {
        u32 next = 0, obj = 0, def = 0; safe_read(node + NODE_NEXT, &next); safe_read(node + NODE_VAL, &obj);
        char nm[9]; unsigned char cl = 1;
        if (valid_ptr(obj) && safe_read(obj + OBJ_DEF, &def) && valid_ptr(def) && rdname(def, nm) && !strcmp(nm, "inline")
            && rd8(obj + OBJ_CLOSING, cl) && !cl) {
            u32 hd = 0, ti = 0, vt = 0, fn = 0;
            if (!safe_read(obj + OBJ_HANDLER, &hd) || !valid_ptr(hd) || !safe_read(hd + 0x1C, &ti) || !valid_ptr(ti)
                || !safe_read(ti, &vt) || vt < lo || vt >= hi || !safe_read(vt + 0x10, &fn) || fn < lo || fn >= hi) return -2;
            int n = -2;
            __try { n = ((FsGetTextFn)fn)(ti, buf, cap - 1); } __except (EXCEPTION_EXECUTE_HANDLER) { n = -2; }
            if (n < 0 || n >= cap) { buf[0] = 0; return -2; }
            buf[n] = 0;
            if (mode) { u32 m = 0; *mode = (safe_read(hd + 0x2C, &m) && m < 256) ? (int)m : -1; }
            return n;
        }
        node = next;
    }
    return -1;   // the input line is not open
}

int native_ui_report(char out[][160], int maxOut) {
    int n = 0;
    const UiConfig& c = ui_config();
    if (n < maxOut) { _snprintf(out[n], 160, "native windows : hide mask %d, menus offset %d,%d, %s", c.nuHide, c.nuMenuDx, c.nuMenuDy,
                                g_anyApplied ? "ACTIVE" : "nothing applied"); out[n][159] = 0; ++n; }
    if (n < maxOut && g_why[0]) { _snprintf(out[n], 160, "native windows : %s", g_why); out[n][159] = 0; ++n; }
    if (n < maxOut && (g_setter || g_moveto)) { _snprintf(out[n], 160, "native windows : setter %s, move-to %s, hook %s, %d records tracked",
                                g_setter ? "found" : "missing", g_moveto ? "found" : "missing", g_hkMove.on ? "ON" : "off", g_eN); out[n][159] = 0; ++n; }
    return n;
}


// ---- the CLIENT's own chat log, replayed into AioHUD's history at load (2026-11-16 : "au reload les chats sont vides,
// recupere les messages du vrai chat") -- docs/game-data/chat-log.md "The client's log store".
// The client keeps every log line twice : on disk, FFXI\TEMP\<character>\<window>_<page>.log, a ring of 20 pages of
// 50 lines (u16 offsets[50], then the records), and the page being filled in memory, in the SAME layout. A record is
// "mode,flags,..,seq,..,len,..,cont,<raw text>\0" : 21 hex fields, the 1st the chat mode, the 21st 01 = a continuation
// of the message above (the client wraps at its window width ; it starts with 1E 01 81 40, an indent), the last piece
// ends with 7F 31. The fulllog draw (FUN_05479420) holds the view : MOV ECX,[view] -> view+0x64044 = the reader,
// reader+4 = the page in memory, reader+0xC = the store (+0x54 pages written, +0x58 ring start, +0x5C window number).
// Measured live on two clients 2026-11-16 : the store's ring start lands on the oldest file, the memory page carries
// the lines written after the last full page.
namespace {
u32 g_logViewVar = 0;
u32 log_view_var() {
    if (g_logViewVar) return g_logViewVar;
    const u32 at = aob_masked("83 EC 0C 53 55 56 8B F1 57 33 ED 8B 46 54 89 6C 24 14 50 E8 ?? ?? ?? ?? 8B 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 8B 7E 20 83 CB FF 66 3B C3");
    if (!at || !safe_read(at + 26, &g_logViewVar)) g_logViewVar = 0;
    return g_logViewVar;
}
// one message, re-joined from the client's wrapped pieces
struct SeedMsg { char t[1500]; int n, mode, hhmm; bool open; };
int g_seedMsgs = 0;
int hexv(const char* p, int n) {
    int v = 0;
    for (int i = 0; i < n; ++i) {
        const int c = p[i] | 0x20;
        v = v * 16 + (p[i] >= '0' && p[i] <= '9' ? p[i] - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : 0);
    }
    return v;
}
void seed_flush(SeedMsg& m) {
    if (!m.open) return;
    while (m.n >= 2 && (unsigned char)m.t[m.n - 2] == 0x7F && m.t[m.n - 1] == '1') m.n -= 2;   // the end marker
    m.t[m.n] = 0;
    if (m.n > 0) { chat_seed_line(m.t, m.mode, m.hhmm); ++g_seedMsgs; }
    m.open = false; m.n = 0;
}
// one record (`len` bytes) -> the message being built
void seed_record(SeedMsg& m, const char* r, int len) {
    int i = 0, commas = 0, f20 = -1;
    for (; i < len && commas < 21; ++i) if (r[i] == ',') { ++commas; if (commas == 20) f20 = i + 1; }
    if (commas < 21 || f20 < 0) return;                          // not a record
    const int mode = hexv(r, 2), cont = hexv(r + f20, 2);
    const char* t = r + i; int tn = len - i;
    if (!cont || !m.open) {
        seed_flush(m);
        m.open = true; m.n = 0; m.mode = mode; m.hhmm = -1;
        for (int k = 0; k + 6 < tn && k < 16; ++k) {               // its time : "[hh:mm" past the colour codes
            const unsigned char c = (unsigned char)t[k];
            if (c == 0x1E || c == 0x1F) { ++k; continue; }
            if (c == '[' && t[k + 3] == ':') m.hhmm = ((t[k + 1] - '0') * 10 + (t[k + 2] - '0')) * 100 + (t[k + 4] - '0') * 10 + (t[k + 5] - '0');
            break;
        }
    } else if (tn >= 4 && (unsigned char)t[0] == 0x1E && (unsigned char)t[2] == 0x81 && (unsigned char)t[3] == 0x40) { t += 4; tn -= 4; }   // the wrap indent
    for (int k = 0; k < tn && m.n < (int)sizeof(m.t) - 1; ++k) m.t[m.n++] = t[k];
}
// a page = u16 offsets[50] + the records (offsets from the records' start)
int seed_page(SeedMsg& m, const unsigned char* recs, int used, const short* offs, int count) {
    int n = 0;
    for (int k = 0; k < count && k < 50; ++k) {
        const int o = offs[k]; if (o < 0 || o >= used) break;
        int e = o; while (e < used && recs[e]) ++e;
        seed_record(m, (const char*)recs + o, e - o); ++n;
    }
    return n;
}
// a page file : its lines into `m`, or (firstSeq) only the line number of its first record ; -1 unreadable
// How the page files open. Measured 2026-11-17 : CreateFileA on the real, existing path failed with error 3 (path not
// found) inside the game process for all 20 pages, while the same call from another process opened them -- a file
// hook in the process (the Sandbox plugin redirects the FFXI folder) rewrites the ANSI path. The "\\?\" wide form
// bypasses the path rewriting ; the first way that works is kept for the rest of the load, and logged once.
int g_openWay = -1;                                            // -1 not yet known, 0 ANSI, 1 wide "\\?\"
HANDLE open_page(const char* path) {
    const DWORD share = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
    HANDLE h = INVALID_HANDLE_VALUE;
    if (g_openWay != 1) {
        h = CreateFileA(path, GENERIC_READ, share, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
        if (h != INVALID_HANDLE_VALUE) { if (g_openWay < 0) { g_openWay = 0; windower::debug::log("CHATSEED pages open by their path"); } return h; }
    }
    if (g_openWay != 0) {
        const DWORD errA = GetLastError();
        wchar_t w[MAX_PATH + 8] = L"\\\\?\\";
        if (MultiByteToWideChar(CP_ACP, 0, path, -1, w + 4, MAX_PATH)) {
            h = CreateFileW(w, GENERIC_READ, share, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
            if (h != INVALID_HANDLE_VALUE) { if (g_openWay < 0) { g_openWay = 1; windower::debug::log("CHATSEED pages open by the wide \\\\?\\ path (the ANSI one failed, error %u)", (unsigned)errA); } return h; }
        }
        SetLastError(errA);
    }
    return INVALID_HANDLE_VALUE;
}
int seed_file(SeedMsg& m, const char* path, unsigned* firstSeq) {
    HANDLE h = open_page(path);
    if (h == INVALID_HANDLE_VALUE) return -1;
    static unsigned char buf[65536]; DWORD got = 0;
    const BOOL ok = ReadFile(h, buf, sizeof(buf) - 1, &got, 0); CloseHandle(h);
    if (!ok || got <= 100) return -1;
    short offs[50]; memcpy(offs, buf, 100);
    if (firstSeq) {
        *firstSeq = 0;
        const char* r = (const char*)buf + 100 + (offs[0] > 0 ? offs[0] : 0); int c = 0, i = 0;
        for (; i < 80 && c < 5 && 100 + i < (int)got; ++i) if (r[i] == ',') ++c;
        if (c == 5) *firstSeq = (unsigned)hexv(r + i, 8);
        return 0;
    }
    return seed_page(m, buf + 100, (int)got - 100, offs, 50);
}
} // namespace

void chat_backlog_tick(const char* charName) {
    static int state = 0; static DWORD t0 = 0;                   // 0 waiting, 1 done (once per load)
    if (state) return;
    const DWORD now = GetTickCount(); if (!t0) t0 = now;
    if (!charName || !charName[0]) {                             // not logged in yet : the live lines wait, bounded
        if ((int)(now - t0) > 20000) { state = 1; chat_seed_end(false); windower::debug::log("CHATSEED no character after 20 s : live lines only"); }
        return;
    }
    state = 1;
    char dir[MAX_PATH] = { 0 };
    {   HMODULE fm = GetModuleHandleA("FFXiMain.dll");
        if (fm && GetModuleFileNameA(fm, dir, MAX_PATH)) { char* sl = strrchr(dir, '\\'); if (sl) sl[1] = 0; else dir[0] = 0; } else dir[0] = 0; }
    if (!dir[0]) { chat_seed_end(false); windower::debug::log("CHATSEED FFXiMain path unknown : live lines only"); return; }
    // the store, from memory : which file is the oldest, how many are written, and the page being filled
    int nPages = -1, start = 0, win = 1, memCount = 0, memUsed = 0;
    static unsigned char memBuf[0x10000]; short memOffs[50];
    {   u32 var = log_view_var(), view = 0, rdr = 0, page = 0, store = 0, buf = 0; unsigned char np = 0, cnt = 0; short used = 0;
        if (var && safe_read(var, &view) && valid_ptr(view) && safe_read(view + 0x64044, &rdr) && valid_ptr(rdr)
            && safe_read(rdr + 4, &page) && valid_ptr(page) && safe_read(rdr + 0xC, &store) && valid_ptr(store)
            && rd8(store + 0x54, np) && safe_read(store + 0x58, (u32*)&start) && safe_read(store + 0x5C, (u32*)&win)
            && np <= 20 && start >= 0 && (win == 1 || win == 2)
            && rd8(page + 0xC8, cnt) && cnt <= 50 && safe_read(page + 0xCC, &buf) && rd16(page + 0xD8, used)) {
            nPages = np;
            bool ok = true;
            __try { memcpy(memOffs, (const void*)page, 100); if (cnt && used > 0 && valid_ptr(buf)) memcpy(memBuf, (const void*)buf, (unsigned short)used); }
            __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
            if (ok) { memCount = cnt; memUsed = (unsigned short)used; }
        }
    }
    char base[MAX_PATH]; _snprintf(base, MAX_PATH, "%sTEMP\\%s\\", dir, charName); base[MAX_PATH - 1] = 0;
    SeedMsg* m = (SeedMsg*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(SeedMsg));
    if (!m) { chat_seed_end(false); return; }
    g_seedMsgs = 0; int files = 0, lines = 0;
    char path[MAX_PATH];
    bool byOrder = false;
    if (nPages >= 0) {                                           // the client's own order : oldest file first
        for (int p = 0; p < nPages; ++p) {
            _snprintf(path, MAX_PATH, "%s%d_%d.log", base, win, (start + p) % 20); path[MAX_PATH - 1] = 0;
            const int n = seed_file(*m, path, 0);
            if (n >= 0) { ++files; lines += n; }
            else if (p == 0) windower::debug::log("CHATSEED cannot read '%s' (error %u ; store : %d pages, start %d, window %d)", path, (unsigned)GetLastError(), nPages, start, win);
        }
    }
    if (files == 0) {                                            // no store, or its order read nothing : the files, by their first line number
        byOrder = true;
        unsigned seq[20]; int idx[20], nf = 0;
        for (int k = 0; k < 20; ++k) {
            _snprintf(path, MAX_PATH, "%s1_%d.log", base, k); path[MAX_PATH - 1] = 0;
            unsigned sq = 0;
            if (seed_file(*m, path, &sq) == 0) { int j = nf++; while (j > 0 && seq[j - 1] > sq) { seq[j] = seq[j - 1]; idx[j] = idx[j - 1]; --j; } seq[j] = sq; idx[j] = k; }
        }
        for (int j = 0; j < nf; ++j) {
            _snprintf(path, MAX_PATH, "%s1_%d.log", base, idx[j]); path[MAX_PATH - 1] = 0;
            const int n = seed_file(*m, path, 0); if (n >= 0) { ++files; lines += n; }
        }
        if (!nf) windower::debug::log("CHATSEED no page file under '%s' (error %u)", base, (unsigned)GetLastError());
    }
    lines += seed_page(*m, memBuf, memUsed, memOffs, memCount);   // then the newest lines : the page in memory
    seed_flush(*m);
    HeapFree(GetProcessHeap(), 0, m);
    chat_seed_end(g_seedMsgs > 0);
    windower::debug::log("CHATSEED %s : %d messages (%d lines : %d page files%s, %d in memory ; store %d pages, start %d) in %u ms", charName, g_seedMsgs, lines, files,
                         byOrder ? (nPages >= 0 ? " BY LINE NUMBER, the store order read nothing" : " BY LINE NUMBER, store not found") : "", memCount, nPages, start,
                         (unsigned)(GetTickCount() - now));
}

// ---- Region Info : the server's answers (capture stage -- docs/notes/region-info-re-2026-11-19.md) ----
void native_region_packet(int id, const unsigned char* b) {
    const int len = (b[1] & 0xFE) * 2;                                  // the header's size, in 4-byte words
    if (len < 8 || len > 0x100) { windower::debug::log("RGPKT 0x%03X refused : size %d", id, len); return; }
    int type = -1;
    if (id == 0x05E) { if (len < (int)sizeof(g_rg.conq)) { windower::debug::log("RGPKT 0x05E refused : %d bytes", len); return; } memcpy(g_rg.conq, b, sizeof(g_rg.conq)); g_rg.conqOk = true; }
    else if (id == 0x071) {
        type = b[4];
        if (type == 2 && len >= (int)sizeof(g_rg.camp)) {
            memcpy(g_rg.camp, b, sizeof(g_rg.camp)); g_rg.campOk = true;   // both halves carry the same header
            const int s = b[9];                                              // then up to 13 area records from pkt+0x30
            for (int k = 0; k < 13 && s + k < 26 && 0x30 + 12 * (k + 1) <= len; ++k) { memcpy(g_rg.campArea[s + k], b + 0x30 + 12 * k, 12); g_rg.campAreaOk[s + k] = true; }
        }
        else if (type == 3 && len >= (int)sizeof(g_rg.colo)) { memcpy(g_rg.colo, b, sizeof(g_rg.colo)); g_rg.coloOk = true; }
    }
    else return;
    // the capture : the first few of each kind in full (the meanings left open in the note), then nothing
    static int said[5] = {};
    const int kind = id == 0x05E ? 0 : (type >= 1 && type <= 3 ? type : 4);
    if (said[kind] >= 3) return;
    if (++said[kind] == 3) windower::debug::log("RGPKT 0x%03X type %d : 3 captured -- no more of this kind logged", id, type);
    windower::debug::log("RGPKT 0x%03X%s%d : %d bytes", id, type >= 0 ? " type " : " ", type >= 0 ? type : 0, len);
    char line[3 * 32 + 16];
    for (int o = 0; o < len; o += 32) {
        int n = 0; n += sprintf(line + n, "+%02X ", o);
        for (int k = o; k < o + 32 && k < len; ++k) n += sprintf(line + n, "%02X%s", b[k], ((k + 1) % 4) ? "" : " ");
        windower::debug::log("RGPKT   %s", line);
    }
}

} // namespace aio
