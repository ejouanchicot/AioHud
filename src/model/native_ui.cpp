// native_ui.cpp -- hide / offset the client's own windows. See native_ui.h for the two placement paths and
// docs/game-data/native-windows.md for every offset below and how each was proven (2026-11-15).
#include "model/native_ui.h"
#include "model/ui_config.h"
#include "model/ffximain_rva.h"   // FM_MENU_PTR : the menu manager sits 0x54 before the focused-menu slot
#include "model/game_mem.h"       // ffximain_base
#include "model/paths.h"          // plugin_path : the crash-recovery file
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
const char* const CHAT_W[]  = { "logwindo", "logwin2", "spoolmsg", "splmsg2", "eventlog", "log8", "rem1line", "rem2line", "rem3line", 0 };
const char* const HELP_W[]  = { "helpwind", "titlewin", 0 };
const char* const COST_W[]  = { "subwindo", 0 };
const char* const NET_W[]   = { "netstat", "polwindo", 0 };
const char* const ACT_SUB_W[] = { "magselec", "abiselec", 0 };   // the action menu's submenus AioHUD redraws (NU_ACT)
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
    if ((hide & NU_ACT)   && (!strcmp(n, "playermo") || in_list(n, ACT_SUB_W))) { dy = HIDE_DY; return; }
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
NativeMenu g_act; bool g_actFail = false; int g_actBad = 0; DWORD g_actGoodSince = 0;
u32 g_actObj = 0;          // the playermo object of the last good read (re-read before any call into the client)
int eff_hide(const UiConfig& c) { return g_actFail ? (c.nuHide & ~NU_ACT) : c.nuHide; }

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
            if ((st & 0x10) || r[2] - r[0] <= 0 || r[3] - r[1] <= 0 || m.n >= 16) { cn = cnext; continue; }   // skipped / not laid out
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
    if (!on) { g_act.open = false; g_sub.open = false; g_actBad = 0; g_actFail = false; return; }
    NativeMenu m; int r = act_read(m);
    NativeEntryMenu sm; const int rs = sub_read(sm);
    g_sub = sm; if (rs != 1) g_sub.open = false;
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
void rescue_children(u32 mgr, int hide) {
    if (!g_setter) return;
    u32 wh = 0; if (!safe_read(mgr + 0x80, &wh)) return;
    const int uiH = (short)(wh >> 16); if (uiH < 240 || uiH > 8000) return;
    u32 node = 0; safe_read(mgr + MGR_OPEN_LIST, &node);
    for (int g = 0; valid_ptr(node) && g < 400; ++g) {
        u32 next = 0, obj = 0, def = 0; safe_read(node + NODE_NEXT, &next); safe_read(node + NODE_VAL, &obj);
        unsigned char cl = 0; short r[4] = {0, 0, 0, 0}; char nm[9];
        if (valid_ptr(obj) && safe_read(obj + OBJ_DEF, &def) && valid_ptr(def) && rdname(def, nm) && rd8(obj + OBJ_CLOSING, cl) && !cl
            && rd16(obj + OBJ_RECT, r[0]) && rd16(obj + OBJ_RECT + 2, r[1]) && rd16(obj + OBJ_RECT + 4, r[2]) && rd16(obj + OBJ_RECT + 6, r[3])
            && r[1] >= uiH + HIDE_DY / 2) {
            int dx = 0, dy = 0; desired_offset(nm, 0, 0, 0, hide, 0, 0, dx, dy);
            const int w = r[2] - r[0], h = r[3] - r[1];
            if (dy != HIDE_DY && w > 0 && h > 0 && w < 4000 && h < 4000) {          // parked by its parent, not by us
                bool ok = true;
                __try { ((SetRectFn)g_setter)(obj, r[0], r[1] - HIDE_DY, w, h, 1, 0, 0); }
                __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
                if (ok) refresh_handler(obj, nm);
                if (g_rescueLog < 40) { ++g_rescueLog; windower::debug::log("NATIVEUI '%s' was parked with its parent (y %d) : %s", nm, r[1], ok ? "brought back on screen" : "setter faulted"); }
            }
        }
        node = next;
    }
}

const NativeMenu& native_action_menu() { return g_act; }
const NativeEntryMenu& native_sub_menu() { return g_sub; }

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

void native_ui_tick(bool inWorld) {
    const UiConfig& c = ui_config();
    if (!(inWorld && (c.nuHide & NU_ACT))) { g_act.open = false; g_sub.open = false; }   // never a stale menu once the option is off / logged out
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
    if (!resolve()) { g_act.open = false; g_sub.open = false; return; }        // (resolve() runs the crash recovery once, when it succeeds)
    act_poll(wanted && (c.nuHide & NU_ACT));
    if (wanted && (eff_hide(c) & NU_ACT)) rescue_children(manager(), eff_hide(c));
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

} // namespace aio
