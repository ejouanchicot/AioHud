// native_ui.cpp -- hide / offset the client's own windows. See native_ui.h for the two placement paths and
// docs/game-data/native-windows.md for every offset below and how each was proven (2026-11-15).
#include "model/native_ui.h"
#include "model/ui_config.h"
#include "model/ffximain_rva.h"   // FM_MENU_PTR : the menu manager sits 0x54 before the focused-menu slot
#include "model/game_mem.h"       // ffximain_base
#include "model/paths.h"          // plugin_path : the crash-recovery file
#include "model/chat_log.h"       // chat_seed_line / chat_seed_end : the client's own log, replayed at load
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
const char* const ACT_SUB_W[] = { "magselec", "abiselec", "chatctrl", 0 };
const char* const ACT_LIST_W[] = { "magic", "ability", "mount", 0 };                   // the lists AioHUD redraws (NU_ACT)   // the action menu's submenus AioHUD redraws (NU_ACT)
const char* const NPC_W[]   = { "rem4line", "rem4li2", "query", 0 };          // the NPC windows AioHUD redraws (NU_NPC)
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
    if ((hide & NU_ACT)   && (!strcmp(n, "playermo") || in_list(n, ACT_SUB_W) || in_list(n, ACT_LIST_W))) { dy = HIDE_DY; return; }
    if ((hide & NU_NPC)   && in_list(n, NPC_W)) { dy = HIDE_DY; return; }
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
NativeFep g_fep; DWORD g_fepDrawnMs = 0; int g_fepBadLog = 0; u32 g_fepIn = 0;
int eff_hide(const UiConfig& c) {
    int h = c.nuHide;
    if (g_actFail) h &= ~NU_ACT;
    if (g_npcFail) h &= ~NU_NPC;
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

// DIAG (2026-11-15) : the first move-to calls after each config change -- caller, request, rect + scrollbar
// (obj+0x88 : x, y) before and after. It is how the relative list movers above were found ; kept, bounded.
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

bool hook_install() {
    g_hkMove.at = g_moveto;     g_hkMove.dst = (void*)&hk_moveto;
    g_hkPlace.at = g_listPlace; g_hkPlace.dst = (void*)&hk_listplace;
    const bool ok = hook_on(g_hkMove);
    if (ok && g_listPlace) hook_on(g_hkPlace);   // optional : without it only the list scrollbars lag
    return ok;
}
void hook_remove() { hook_off(g_hkPlace); hook_off(g_hkMove); }

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
    u32 node = 0; safe_read(mgr + MGR_OPEN_LIST, &node);
    for (int g = 0; valid_ptr(node) && g < 400; ++g) {
        u32 next = 0, obj = 0, def = 0; safe_read(node + NODE_NEXT, &next); safe_read(node + NODE_VAL, &obj);
        unsigned char cl = 0; short r[4] = {0}; char nm[9];
        if (valid_ptr(obj) && safe_read(obj + OBJ_DEF, &def) && valid_ptr(def) && rdname(def, nm) && rd8(obj + OBJ_CLOSING, cl) && !cl
            && rd16(obj + OBJ_RECT, r[0]) && rd16(obj + OBJ_RECT + 2, r[1]) && rd16(obj + OBJ_RECT + 4, r[2]) && rd16(obj + OBJ_RECT + 6, r[3])) {
            const int w = r[2] - r[0], h = r[3] - r[1];
            for (int i = 0; i < nD; ++i) if (D[i].def == def) {
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
    return in_list(n, PARTY_W) || in_list(n, ALLY_W) || in_list(n, CHAT_W) || in_list(n, HELP_W) || in_list(n, COST_W) || in_list(n, NET_W);
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
        g_diagN = 40;
        windower::debug::log("NATIVEUI applied : %d record(s) changed, %d window(s) moved now, hide mask %d, menus offset %d,%d -- %s",
                             changed, nD, hide, mdx, mdy, any ? "active" : "everything native");
    }
}

int  g_lastSig = -1; DWORD g_nextWalk = 0;
int  config_sig(const UiConfig& c) {
    int s = (eff_hide(c) & 0xFF) | ((c.nuMenuDx & 0xFFF) << 8) | ((c.nuMenuDy & 0xFFF) << 20);
    s ^= (c.chDock & 3) << 30 ^ (c.chInputOk + 1) << 28;      // the input-line mode, and whether its text reads
    if (c.chDock && c.chShow[0] && c.chDockRect[2] > 0.0f)   // the docked input line follows the chat box : re-apply when it moves
        s ^= (int)c.chDockRect[0] * 73856093 ^ (int)(c.chDockRect[1] + c.chDockRect[3]) * 19349663 ^ (int)c.chDockRect[5] * 83492791 ^ 0x5A5A;
    return s;
}

} // namespace

void chat_info_read(u32 obj);   // below : the Chat submenu's extras (Tell name, linkshells, Unity)
int  list_read(NativeList& L);   // below : the spell list

int inv_read(u32 focus, NativeList& L);   // below, at namespace scope : the Items list (inventor, from the action menu)
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
// One read of the open action SUBMENU (ACT_SUB_W ; the focused one when two are open). -1 none, 0 unreadable, 1 read.
int sub_read(NativeEntryMenu& m, u32* objOut = 0) {
    memset(&m, 0, sizeof(m)); if (objOut) *objOut = 0;
    const u32 mgr = manager(); if (!mgr) return -1;
    u32 focus = 0; const u32 fp = aio::fm_addr(aio::FM_MENU_PTR); if (fp) safe_read(fp, &focus);
    u32 node = 0; safe_read(mgr + MGR_OPEN_LIST, &node);
    u32 obj = 0; char name[9] = "";
    for (int g = 0; valid_ptr(node) && g < 400; ++g) {
        u32 next = 0, o = 0, def = 0; unsigned char dead = 0, cl = 0;
        safe_read(node + NODE_NEXT, &next); safe_read(node + NODE_VAL, &o); rd8(node + 0x14, dead);
        char nm[9];
        if (!dead && valid_ptr(o) && safe_read(o + OBJ_DEF, &def) && valid_ptr(def) && rdname(def, nm) && in_list(nm, ACT_SUB_W)
            && rd8(o + OBJ_CLOSING, cl) && !cl) {
            if (!obj || o == focus) { obj = o; memcpy(name, nm, 9); }
        }
        node = next;
    }
    if (!obj) return -1;
    u32 cur = 0; if (!safe_read(obj + 0x4C, &cur)) return 0;
    // the entry children, in list order (= the cursor's numbering)
    short ys[16]; int k = 0;
    u32 cn = 0; safe_read(obj + 0x14, &cn);
    for (int g = 0; valid_ptr(cn) && g < 64 && k < 32; ++g) {
        u32 cnext = 0, ch = 0; unsigned char cdead = 0; safe_read(cn, &cnext); safe_read(cn + 0x10, &ch); rd8(cn + 0x14, cdead);
        short type = -1;
        if (valid_ptr(ch) && !cdead && rd16(ch, type) && type == 1) {
            ++k;                                                         // entry index k (1-based), shown or not
            u32 st = 0; short r[4] = { 0, 0, 0, 0 };
            if (!safe_read(ch + 0x10, &st) || !rd16(ch + 0x32, r[0]) || !rd16(ch + 0x34, r[1]) || !rd16(ch + 0x36, r[2]) || !rd16(ch + 0x38, r[3])) return 0;
            // skipped / not laid out / a DECORATION, not a row (chatctrl's 8th entry : 18 x 12, never under the cursor)
            if ((st & 0x10) || r[2] - r[0] < 40 || r[3] - r[1] <= 0 || m.n >= 16) { cn = cnext; continue; }
            int at = m.n;                                                // insert by screen y
            while (at > 0 && ys[at - 1] > r[1]) { ys[at] = ys[at - 1]; m.entry[at] = m.entry[at - 1]; m.state[at] = m.state[at - 1]; --at; }
            ys[at] = r[1]; m.entry[at] = (unsigned char)k; m.state[at] = (unsigned char)(st & 0xFF); ++m.n;
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
            if (!ours && !cardOurs && !fepOurs && !invOurs && w > 0 && h > 0 && w < 4000 && h < 4000) {     // parked by its parent, not by us
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
    if (safe_read(g_cr.invPtrVar, &inv) && valid_ptr(inv)) for (int k = 0; k < 2; ++k) {
        unsigned char idx = 0, bag = 0;
        if (!rd8(inv + 0x9808 + (k + 1), idx) || !idx || !rd8(inv + 0x980F + (k + 1), bag)) continue;
        const u32 rec = inv + ((u32)bag * 0x51 + idx) * 0x2C + 0x9868;
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
    if (!resolve()) { g_act.open = false; g_sub.open = false; g_list.open = false; g_npc.talk = false; g_npc.query = false; g_fep.open = false; return; }        // (resolve() runs the crash recovery once, when it succeeds)
    if (inWorld) fulllog_read(manager()); else g_full.open = false;   // the expanded log : AioHUD's Discussion box unfolds
    act_poll(wanted && (c.nuHide & NU_ACT));
    npc_poll(wanted && (c.nuHide & NU_NPC));
    fep_poll(wanted && docking);                         // the input line in AioHUD's box : its auto-translate picker too
    // the HIDDEN HUD-like windows, EVERY frame (not only in the once-a-second walk below) : the client re-places some on
    // its own -- partywin at each member who joins (y 1176 -> 1156 -> 1136 ...), shown up to a second before the walk
    // put it back (2026-11-17, "quand quelqu'un pop dans la party ca la fait pop une seconde"). heal_hud only acts on a
    // window shown where it should be hidden (or the reverse) : on a quiet frame it is one walk of the open list.
    if (wanted && g_anyApplied && (c.nuHide & (NU_PARTY | NU_ALLY | NU_CHAT | NU_HELP | NU_COST | NU_NET))) heal_hud(manager());
    if (wanted && (c.nuHide & NU_ACT)) inv_park(manager());          // the Items list : the client's own window out of the way
    if (wanted && (eff_hide(c) & (NU_ACT | NU_NPC))) rescue_children(manager(), eff_hide(c));
    if (!wanted && !g_anyApplied) return;                  // recovery done, nothing configured
    const int sig = wanted ? config_sig(c) : 0;
    const DWORD now = GetTickCount();
    if (sig != g_lastSig || (int)(now - g_nextWalk) >= 0) {   // on a change at once, otherwise once a second
        g_lastSig = sig; g_nextWalk = now + 1000;           // (new definitions load lazily ; a relog rewrites records)
        apply(!wanted);
    }
}

void native_ui_shutdown() {
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

} // namespace aio
