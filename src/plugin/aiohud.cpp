// aiohud.cpp -- the Windower IPlugin glue: the ONE translation unit that includes
// windower_plugin.h (it defines the exported entry points + the 34-slot vtable).
//
// This layer owns nothing visual. It holds the host handle and the HUD, routes the
// per-frame draw hook to Hud::render, and turns the //aio console command into
// GameState / effect updates. Everything else lives in gfx/ ui/ model/.
//
// Command (today the data source -- a native plugin cannot receive // commands by
// name, so the alias IS GetDescription() lowercased = "aio"):
//   //aio hp 50            HP to 50%
//   //aio mp 30 tp 1500    MP 30%, TP 1500/3000
//   //aio 50 30 1500       positional HP MP TP
//   //aio lay 1            effect layers (1 = liquid only, >=4 = full effects)
#include "windower_plugin.h"
#include "windower_debug.h"
#ifdef AIOHUD_PROBES
#include "aiohud_probes.h"          // dev-only diagnostic surface (present only in the local tree ; build.bat wires it in when the file exists)
#endif
#include "ui/hud.h"
#include "ui/player.h"   // set_gear_trace : //aio geartrace
#include "model/layout.h"
#include "model/party_state.h"
#include "model/game_mem.h"
#include "model/ui_config.h"
#include "model/nms_gen.h"          // NMS / NMS_N : the //aio pop <nm> selector
#include "model/paths.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <windows.h>
#include <locale.h>

namespace aio { void timers_reset(); }   // hud_timers.cpp : //aio timers reset -> flush live buff/recast timers + focus alerts

// NB: the reverse-engineering DIAGNOSTIC surface (mem_scan / scan_word_range / th_bits / thfx_walk /
// bt_scan / pw_scan_* / collect_ptr_hits / f2s_probe + every g_*log ring + the //aio debug/dump/scan
// commands + the armed packet/text handlers) lives in aiohud_probes.cpp now. It is still COMPILED IN
// and reachable via aio::probes::command / packet_in / text_in ; this file keeps only the shipping glue.

static const char* LAYOUT_PATH()    { static char b[260]; if (!b[0]) aio::plugin_path(b, 260, "design\\exports\\layout.json");           return b; }   // runtime-derived (was a hardcoded dev path)
static const char* LAYOUT_RT_PATH() { static char b[260]; if (!b[0]) aio::plugin_path(b, 260, "design\\exports\\layout.roundtrip.json"); return b; }

using namespace windower;

static PluginManager g_host;
static aio::Hud      g_hud;

// the ONE host handle, exposed so aiohud_probes.cpp reaches the console without duplicating g_host.
windower::PluginManager& aio_host() { return g_host; }

// ---- input parsing: //aio fills GameState (later replaced by the ffxi poller) ----

static void set_fill(int idx, int v)
{
    aio::GameState& st = g_hud.state();
    if (idx == 2) {                                   // TP : 0..3000
        if (v < 0) v = 0; if (v > 3000) v = 3000;
        st.tp = v / 3000.0f;
    } else if (idx == 0 || idx == 1) {                // HP / MP : 0..100 %
        if (v < 0) v = 0; if (v > 100) v = 100;
        (idx == 0 ? st.hp : st.mp) = v / 100.0f;
    }
}

static void parse_one(const char* s, const char* tok, int idx)
{
    const char* p = strstr(s, tok);
    if (!p) return;
    p += 2;
    while (*p && (*p < '0' || *p > '9')) p++;
    if (!*p) return;
    int n = 0; while (*p >= '0' && *p <= '9') { n = n * 10 + (*p - '0'); p++; }
    set_fill(idx, n);
}

static void parse_fill_string(const char* s)
{
    if (strstr(s, "hp") || strstr(s, "mp") || strstr(s, "tp")) {
        parse_one(s, "hp", 0); parse_one(s, "mp", 1); parse_one(s, "tp", 2);   // keyword form
    } else {
        const char* p = s;                                                     // positional: HP MP TP
        for (int i = 0; i < 3; i++) {
            while (*p && (*p < '0' || *p > '9')) p++;
            if (!*p) break;
            int n = 0; while (*p >= '0' && *p <= '9') { n = n * 10 + (*p - '0'); p++; }
            set_fill(i, n);
        }
    }
}

// A plugin can't //lua load an addon itself (the host interface exposes no command executor), but Windower runs
// <Windower>\scripts\init.txt at every launch. So we register "lua load aioupdate" there (idempotent) : the
// companion updater addon then auto-loads with Windower, no manual //lua load. We insert it right AFTER the
// "load AioHud" line so it loads together with the plugin at startup (not late, after any post-login `wait`).
// Takes effect on the next Windower launch (init.txt is only read then).
static void ensure_addon_autoload()
{
    // init.txt belongs to WINDOWER, not to us, and every client rewrites it at startup. Two clients launching
    // together both read "no aioupdate line", both truncate-rewrite, and their buffered writes can interleave into
    // a truncated or duplicated file -- which breaks Windower's startup for EVERY plugin, not just this one, and
    // nobody would suspect AioHud. Serialise across processes, then write atomically (temp + rename) below.
    HANDLE mtx = CreateMutexA(NULL, FALSE, "Local\\AioHudInitTxt");
    if (mtx) WaitForSingleObject(mtx, 5000);
    struct MtxGuard { HANDLE h; ~MtxGuard() { if (h) { ReleaseMutex(h); CloseHandle(h); } } } guard{ mtx };

    char root[300];
    lstrcpynA(root, aio::plugin_dir(), sizeof(root));   // ...\plugins\AioHud
    char* s = strrchr(root, '\\'); if (s) *s = 0;        // -> ...\plugins
    s = strrchr(root, '\\'); if (s) *s = 0;              // -> ...        (Windower root)
    char ini[360]; _snprintf(ini, sizeof(ini), "%s\\scripts\\init.txt", root); ini[sizeof(ini) - 1] = 0;

    static char buf[32768]; size_t n = 0;
    FILE* f = fopen(ini, "rb");
    if (f) { n = fread(buf, 1, sizeof(buf) - 1, f); fclose(f); }
    buf[n] = 0;
    if (strstr(buf, "aioupdate")) return;                // already registered -> nothing to do
    bool full = (n >= sizeof(buf) - 1);                  // file bigger than our buffer -> don't risk a truncating rewrite

    char dir[360]; _snprintf(dir, sizeof(dir), "%s\\scripts", root); dir[sizeof(dir) - 1] = 0;
    CreateDirectoryA(dir, NULL);                         // scripts\ may not exist yet

    // Insert right after the line that loads the plugin, e.g. "\nload AioHud\n" -> keep it grouped + early.
    char* at = strstr(buf, "\nload AioHud");
    if (at && !full) {
        char* eol = strchr(at + 1, '\n');               // end of the "load AioHud" line
        size_t cut = eol ? (size_t)(eol - buf) + 1 : n;
        // temp + rename : a crash or a short write mid-rewrite must never leave Windower's init.txt truncated.
        char tmp[400]; _snprintf(tmp, sizeof(tmp), "%s.aiotmp", ini); tmp[sizeof(tmp) - 1] = 0;
        f = fopen(tmp, "wb");
        if (f) {
            const bool ok = fwrite(buf, 1, cut, f) == cut
                         && fputs("lua load aioupdate\r\n", f) >= 0
                         && fwrite(buf + cut, 1, n - cut, f) == (n - cut);
            const bool closed = (fclose(f) == 0);
            if (!ok || !closed || !MoveFileExA(tmp, ini, MOVEFILE_REPLACE_EXISTING)) DeleteFileA(tmp);
        }
    } else {
        f = fopen(ini, "ab");                            // no plugin line found (or huge file) -> just append
        if (f) { fputs("\r\nlua load aioupdate\r\n", f); fclose(f); }
    }
}

// ===== IPlugin hooks (declared in windower_plugin.h) =====

static void spawn_updater(bool checkOnly);   // defined below ; launch the no-window updater / update-check

void aio_plugin_init(PluginManager host)
{
    g_host = host;
    setlocale(LC_NUMERIC, "C");         // dot decimals for ALL our float I/O (config.txt + layout.json strtod) whatever
                                        // the OS locale -- a comma-locale (French-Canadian, French, German...) would
                                        // otherwise misparse every saved/loaded position/scale and wreck box placement.
    ensure_addon_autoload();           // make the //aioupdate companion addon auto-load with Windower
    spawn_updater(true);               // no-window update CHECK at startup -> writes data\update\check.txt for the Update tab
    debug::clear();
    debug::log("AioHUD init: device = 0x%08X", host.service_raw(2));
    aio::load_ui_config();             // restore saved theme / font / box positions + sizes
    // NOT party().load() here : the roster cache is per character now, and nothing is logged in at init -- loading
    // blind is exactly what drew another character's party on your screen at the character-select screen. The load
    // is driven from load_from_memory() on the first frame where read_player succeeds.
    aio::party().load_from_memory();   // LIVE roster+vitals from FFXI memory -> correct party at load
    debug::log("party at load: %d member(s)", aio::party().count);
    for (int i = 0; i < aio::party().count; ++i)
        debug::log("  m%d '%s' hp=%d/%d mp=%d/%d hpp=%d zone=%d", i, aio::party().m[i].name,
                   aio::party().m[i].hp, aio::party().m[i].maxHp, aio::party().m[i].mp, aio::party().m[i].maxMp,
                   aio::party().m[i].hpp, aio::party().m[i].zone);
    g_hud.apply_layout(LAYOUT_PATH());   // place widgets from the descriptor (keeps defaults if absent)
    char banner[128]; _snprintf(banner, sizeof(banner), ">>> AioHUD v%s by ejouanchicot  --  //aio config <<<", AIOHUD_VERSION); banner[sizeof(banner) - 1] = 0;
    host.console().print(banner);   // user-facing load line : credits the author + points at the config (the //aio hp/mp/tp fill commands still work, just no longer advertised here)
}

void aio_plugin_render() {}   // slot 5 : unused (we draw on slot 6, like FFXIDB)


// the game's window handle, cached each frame from the D3D device (the mouse hook runs without a
// device, so it reads this to tell whether the GAME is the OS foreground window).
static u32 g_gameHwnd = 0;



// ---- WM_SETCURSOR subclass : the only thing that actually stops the game re-showing its pointer ----
// Two weaker attempts failed first, and each failure narrowed it down : a per-frame SetCursor(NULL) LOSES the
// race (the game re-shows on every WM_SETCURSOR, so both cursors flicker), and nulling the window CLASS cursor
// does nothing either -- which proves the game does not rely on DefWindowProc for it but calls SetCursor from
// its own WndProc. So we have to answer the message BEFORE the game sees it.
//
// The subclass is installed ONCE and left in place, rather than toggled with the overlay : repeatedly swapping
// WndProc risks breaking the chain if another addon subclasses between our install and uninstall. The overlay
// test lives INSIDE the proc, so while the config is closed we are a pure pass-through.
// It MUST be removed on unload -- leaving a WndProc pointing into a freed DLL crashes the client on the next
// message.
static WNDPROC g_oldWndProc = NULL;
static HWND    g_subclassed = NULL;

static LRESULT CALLBACK aio_wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    // Only the CLIENT area, and only while our overlay owns the pointer : the frame/resize cursors on the window
    // border must keep working, and outside the overlay the game owns its cursor completely.
    if (msg == WM_SETCURSOR && LOWORD(lp) == HTCLIENT &&
        (g_hud.config().is_open() || aio::ui_config().editLayout)) {
        SetCursor(NULL);
        return TRUE;            // handled -> the game's WndProc never runs its own SetCursor for this message
    }
    return CallWindowProcA(g_oldWndProc, h, msg, wp, lp);
}

static void aio_subclass_install(HWND hw)
{
    if (g_oldWndProc || !hw || !IsWindow(hw)) return;
    g_oldWndProc = (WNDPROC)(LONG_PTR)SetWindowLongA(hw, GWL_WNDPROC, (LONG)(LONG_PTR)aio_wndproc);
    if (g_oldWndProc) { g_subclassed = hw; windower::debug::log("cursor: WndProc subclassed on %08X", (unsigned)(LONG_PTR)hw); }
}

static void aio_subclass_remove()
{
    if (!g_oldWndProc || !g_subclassed) return;
    if (IsWindow(g_subclassed)) SetWindowLongA(g_subclassed, GWL_WNDPROC, (LONG)(LONG_PTR)g_oldWndProc);
    g_oldWndProc = NULL; g_subclassed = NULL;
}

void aio_plugin_render6()
{
    u32 dev = g_host.service_raw(2);   // host vtbl[2] = D3D8 device
    if (valid_ptr(dev)) g_gameHwnd = aio::dFocusWindow(dev);
    if (g_gameHwnd) aio_subclass_install((HWND)g_gameHwnd);   // once the device gives us the window
    g_hud.render(dev);
    // Config/edit overlay up + game focused : force-hide the OS cursor each frame (last SetCursor of the frame ->
    // wins). The game re-shows its NATIVE cursor on WM_SETCURSOR (e.g. when the mouse RE-ENTERS the window), a
    // WINDOW message our DirectInput mouse hook can't swallow -> without this it reappears on top of AioHud's own
    // pointer (the "two cursors" bug). AioHud draws its own pointer, so hiding the native one leaves exactly one.
    // ---- Own the pointer while the overlay is up ----
    if (g_gameHwnd) {
        POINT cp; HWND under = NULL;
        if (GetCursorPos(&cp)) under = WindowFromPoint(cp);
        const bool overGame = under && GetAncestor(under, GA_ROOT) == (HWND)g_gameHwnd;
        if (overGame && (g_hud.config().is_open() || aio::ui_config().editLayout)) SetCursor(NULL);   // belt and braces
    }
}

// SEH-guarded packet dispatch. The parsers read FIXED offsets off `b` ; a SHORT/truncated packet would
// fault, and neither this callback nor the m11 ABI thunk is exception-wrapped -> the fault would propagate
// into the game (hard crash). Kept in its OWN function so the __except has no C++ object unwinding to fight.
static void feed_packet(int id, const unsigned char* b)
{
    __try {
        if      (id == 0xDD)  aio::party().on_dd(b);
        else if (id == 0xDF)  aio::party().on_df(b);
        else if (id == 0x028) aio::party().on_action(b);   // cast bar + landed target debuffs
        else if (id == 0x029) { aio::party().on_029(b); aio::party().on_exp_msg(b, 0x029); }   // action message -> status wear-off + PointWatch exp gains (Abyssea)
        else if (id == 0x02D) aio::party().on_exp_msg(b, 0x02D);   // PointWatch : XP/CP/merit/EP gain messages -> live + X/h rate
        else if (id == 0x061) aio::party().on_char_stats(b);       // PointWatch : level / EXP / Master Level / Exemplar Points
        else if (id == 0x063) aio::party().on_set_update(b);       // PointWatch : merits (Order 2) + Capacity/Job Points (Order 5)
        else if (id == 0x02A) aio::party().on_2a(b);               // Zone Tracker : Abyssea zone messages (lights + visitant)
        else if (id == 0x055) aio::party().on_55(b);               // Zone Tracker : key items (Dynamis granules)
        else if (id == 0x118) aio::party().on_118(b);              // Zone Tracker : currency2 -> Mog Segments (Sheol/Odyssey run delta)
        else if (id == 0x034) aio::party().on_034(b);              // Zone Tracker : Rabao conflux menu -> Sheol A/B/C
        else if (id == 0x00E) aio::party().on_00e(b);              // Zone Tracker : NPC update -> Sheol A/B/C fallback (instance bits)
        else if (id == 0x075) aio::party().on_limbus_075(b);       // Zone Tracker : Limbus menu -> Apollyon/Temenos level (handler self-filters by the string)
        else if (id == 0x076) aio::party().on_076(b);      // party-member buffs
        else if (id == 0x01B) aio::party().on_01b(b);      // job info -> encumbrance flags (locked equip slots)
        else if (id == 0x0D2) aio::party().on_treasure_add(b);   // treasure pool : item dropped / removed
        else if (id == 0x0D3) aio::party().on_treasure_lot(b);   // treasure pool : lot info / won
        else if (id == 0x067) aio::party().on_pet_info(b);       // hate list : learn friendly pet ids (Pet Info)
        else if (id == 0x068) aio::party().on_pet_status(b);     // hate list : friendly pet id + its target mob (Pet Status)
        else if (id == 0x00B) { aio::party().set_zoning(true); aio::party().treasure_clear(); aio::party().hate_clear(); aio::party().pets_clear(); aio::party().buff_timers_clear(); }   // zone-OUT (loading) -> hide HUD + reset pool/hate/pets/self buff timers (0x063 re-sends). Ally buffs (estimates) PERSIST across a zone : the prune drops them on real wear-off / disband / death, not here.
        else if (id == 0x00A) aio::party().set_zoning(false);   // zone-IN : the new zone is ready -> show the HUD again
    } __except (EXCEPTION_EXECUTE_HANDLER) { /* short/malformed packet -> ignore, never crash the game */ }
}

void aio_plugin_packet_in(u32 a, u32 b, u32 c, u32 d)
{
    if (!valid_ptr(b)) return;
    u32 hdr = 0; safe_read(b, &hdr);
    int id = hdr & 0x1FF;

    // BATTLEMOD FIX (//aio thfx PKTARG probe, 2026-07-10) : the slot-11 callback gets arg `a` = the ORIGINAL server
    // packet buffer and `b` = a modifiable copy. Lua addons like BattleMod blank the 0x028 action MESSAGE in the copy
    // (`b`) after reformatting the battle text, which was stealing the Treasure-Hunter add-effect message (603) from
    // us -- THTracker survives only because it hooks earlier. Reading the ORIGINAL `a` bypasses that entirely. Prefer
    // `a` whenever it's a valid parallel packet of the SAME id (SEH-guarded ; falls back to `b` if a is absent/wrong).
    const unsigned char* pkt = (const unsigned char*)b;
    if (valid_ptr(a)) {
        u32 ha = 0; __try { ha = *(volatile u32*)a; } __except (EXCEPTION_EXECUTE_HANDLER) { ha = 0; }
        if ((int)(ha & 0x1FF) == id) pkt = (const unsigned char*)a;   // a = the un-modified original -> use it
    }

    // feed the live party model. SEH-guarded : a truncated packet must never fault the game.
    feed_packet(id, pkt);

#ifdef AIOHUD_PROBES
    __try { aio::probes::packet_in(a, b, c, d, id, pkt); } __except (EXCEPTION_EXECUTE_HANDLER) {}   // dev-only armed probes (SEH-guarded)
#endif
}

// slot 9 : INCOMING TEXT (original, modified, mode). Read-only use : we read `original` (the chat line) + `*mode`
// (161 = Omen objective color) and NEVER touch `modified`/`mode` -> the line is not blocked/altered (blocking is
// done by emptying `modified`, not by the return value). SEH-guarded : a bad text pointer must never fault the game.
void aio_plugin_text_in(const char* original, char* /*modified*/, int* mode)
{
    __try {
        const int m = (mode && valid_ptr((u32)mode)) ? *mode : -1;
        if (!original || !valid_ptr((u32)original)) return;
#ifdef AIOHUD_PROBES
        aio::probes::text_in(original, m);                             // dev-only armed text probes
#endif
        const int mm = m & 0x1FF;                                       // Windower sets the 0x200 flag ; mask it off (Omen was 673 = 161|0x200)
        if (mm == 161) aio::party().on_omen_text(original);            // Omen objective lines
        else if (mm == 123 || mm == 146 || mm == 148) aio::party().on_nyzul_text(original, mm);   // Nyzul Isle floor/timer/objective lines
    } __except (EXCEPTION_EXECUTE_HANDLER) { /* malformed text pointer -> ignore, never crash the game */ }
}

// slot 13 : MOUSE (eventtype, x, y, delta, blocked). eventtype 0=move 1=Ldown 2=Lup (Windower).
// Returns the "blocked" flag : 1 swallows the event so the GAME doesn't react to it. We block ALL
// mouse while the config overlay is up (cursor is read separately via Win32 GetCursorPos).
unsigned int aio_plugin_mouse(u32 eventtype, u32 /*x*/, u32 /*y*/, u32 delta, u32 blocked)   // x/y unused : cursor read via GetCursorPos
{
    // Capture the mouse whenever the overlay OWNS the pointer -- that is, the pointer is over the game window,
    // focused or not. Gating this on focus alone was the double-cursor bug : the game holds mouse capture, so
    // losing focus never stopped it reading the mouse, and it lit up its own in-engine pointer again.
    const bool focused = g_gameHwnd && (HWND)g_gameHwnd == GetForegroundWindow();
    const bool overlay = (g_hud.config().is_open() || aio::ui_config().editLayout);
    const bool active  = focused && overlay;
    // Is the pointer physically over the game window ? The game holds mouse CAPTURE, so it keeps receiving mouse
    // events even while another app is foreground -- being unfocused is NOT enough to know it should ignore them.
    bool overGame = false;
    if (g_gameHwnd) {
        POINT cp; HWND under = NULL;
        if (GetCursorPos(&cp)) under = WindowFromPoint(cp);
        overGame = under && GetAncestor(under, GA_ROOT) == (HWND)g_gameHwnd;
    }

    // A left-click is treated as ONE gesture (down -> moves -> up) with a SINGLE decision locked at the
    // down : either we swallow the whole gesture, or we pass it. Deciding once (not per-event) is what
    // fixes the "stuck character" : the old code passed the down (game not yet focused) but swallowed the
    // up (game focused by then) -> the game saw a button stuck DOWN forever and the avatar kept walking.
    static bool inGesture = false;   // a left button is currently held
    static bool blockGest = false;   // ...and we're swallowing this gesture (vs a refocus click we pass)

    // 1) LEFT gesture : fate LOCKED at the DOWN, same fate applied to its moves + the UP. NEVER split -- the
    //    game seeing a DOWN but never the UP (or vice-versa) is the classic stuck avatar that walks forever.
    //    This holds across every focus/overlay flip that happens MID-gesture :
    //      - the refocus click : down while the game is NOT foreground -> passed so Windows activates the window ;
    //        by the up the game is focused + overlay open, but we still pass it (locked) so no stuck button ;
    //      - the click that CLOSES the config : down while open -> swallowed ; by the up the config is closed,
    //        but we still swallow it (locked) so the game never sees a lone up.
    //    (A previous "swallow everything while active" shortcut ignored this lock and reintroduced the stuck
    //     avatar : a refocus up got swallowed while its down had already reached the game.)
    if (eventtype == 1) {                 // L down -- lock the fate
        inGesture = true;
        // Swallow the whole gesture whenever the overlay owns the pointer -- focused OR not. Passing the
        // "refocus" click was what let the game light up its own in-engine cursor again the moment you clicked
        // back in (same root cause as the moves fixed above : being unfocused does not stop the game reading
        // the mouse, it holds capture). The gesture LOCK is untouched : the fate is still decided once, here,
        // and applied verbatim to the up -- so the game can never see a down without its up (stuck avatar).
        // Windows still activates a clicked window on its own (WM_MOUSEACTIVATE), independently of this hook ;
        // we nudge it explicitly as a belt-and-braces so a swallowed click cannot leave the window inactive.
        blockGest = overlay && (overGame || focused);
        if (blockGest && !focused && g_gameHwnd) SetForegroundWindow((HWND)g_gameHwnd);
        return blockGest ? 1u : blocked;
    }
    if (eventtype == 2) {                 // L up -- SAME fate as its down, whatever changed since
        const bool b = blockGest;
        inGesture = false; blockGest = false;
        return b ? 1u : blocked;
    }
    if (inGesture) return 1u;             // mid-gesture move : always swallow -> a passed refocus click stays a
                                          // STATIONARY click (down+up, no moves) -> refocus without mouselook/walk

    // 1b) THE CURSOR FIX. Measured (//aio curlog) : with the overlay open and the game NOT foreground, the FFXi
    //     window still received 184 WM_MOUSEMOVE and 195 WM_SETCURSOR while the pointer sat over another app --
    //     it holds mouse capture. Those moves are what make the game draw its OWN in-engine cursor, which no
    //     amount of SetCursor / class-cursor / WM_SETCURSOR interception can hide (three attempts proved that).
    //     What actually suppresses it when focused is THIS hook swallowing the events -- but it was gated on
    //     `focused`, so the moment you alt-tabbed the game started seeing the mouse again and its cursor came
    //     back, blinking against ours. Gate on "overlay open AND pointer over the game window" instead.
    //     Left down/up are deliberately NOT included : they are handled above under the gesture lock, so the
    //     refocus click still reaches Windows and cannot be split into a down without its up (the stuck-avatar
    //     bug a previous "swallow everything" shortcut reintroduced).
    //     The RIGHT button (et=4 down / et=5 up -- inferred from a capture, then CONFIRMED behaviourally on
    //     2026-07-19 : right-click in game, reopen the config, the wheel still scrolls. Were the pairing the other
    //     way round the lock would stay stuck on and kill the wheel for the rest of the session) is a gesture too,
    //     so it gets the SAME lock as the
    //     left one -- decided at the down, applied verbatim to the up. Without it, moving the pointer off the
    //     game window mid-drag would swallow the down and pass the up (or vice versa), which is the stuck-input
    //     bug this file already documents for the left button, transposed to mouselook.
    static bool inRGest = false, blockRGest = false;
    if (eventtype == 4) { inRGest = true;  blockRGest = overlay && (overGame || focused); return blockRGest ? 1u : blocked; }
    if (eventtype == 5) { const bool b = blockRGest; inRGest = false; blockRGest = false; return b ? 1u : blocked; }
    if (inRGest) return blockRGest ? 1u : blocked;   // mid-right-drag : same fate as its down, never split

    //     WHEEL IS EXCLUDED HERE (delta != 0). This blanket swallow sat BEFORE the wheel block below, so it ate
    //     the scroll before the config could ever see it -- config scrolling stopped working in v1.0.29. The wheel
    //     is handled below, where it can both feed the overlay and still be kept from the game.
    if (overlay && overGame && (int)delta == 0 && eventtype != 1 && eventtype != 2) return 1u;

    // 2) No left gesture in progress. Wheel : edit box-resize / Help scroll while the overlay is up ; else the
    //    minimap zoom when the cursor is over it. Consume it so the game camera never zooms underneath.
    if ((int)delta != 0) {
        if (active) { aio::ui_config().wheel += ((int)delta > 0) ? 1 : -1; return 1u; }
        // Overlay up and the pointer is over the game, but the game is NOT focused : do not ACT on the scroll
        // (that input belongs to whatever window has focus) yet do not hand it to the game either.
        if (overlay && overGame) return 1u;
        aio::UiConfig& c = aio::ui_config();
        if (c.mmVisible && focused && g_gameHwnd) {
            POINT p;
            if (GetCursorPos(&p)) {
                HWND fg = (HWND)g_gameHwnd; POINT cp = p; ScreenToClient(fg, &cp);
                RECT rc;
                if (GetClientRect(fg, &rc)) {
                    const float ww = (float)(rc.right - rc.left), wh = (float)(rc.bottom - rc.top);
                    if (ww > 1.0f && wh > 1.0f) {
                        const float fx = (float)cp.x / ww, fy = (float)cp.y / wh;
                        if (fx >= c.mmHitX && fx < c.mmHitX + c.mmHitW && fy >= c.mmHitY && fy < c.mmHitY + c.mmHitH) {
                            c.mmWheel += ((int)delta > 0) ? 1 : -1; return 1u;
                        }
                    }
                }
            }
        }
        return blocked;
    }
    // 3) A bare move, or a RIGHT / MIDDLE button event (no left gesture) : while the overlay is up the game must
    //    see NOTHING -- swallow it (no hover, no right-drag camera mouselook). Otherwise pass straight through.
    //    Right/middle aren't fate-locked like the left button, but a right gesture can only straddle the overlay
    //    boundary via a bizarre sequence (config open/close is a left-click or Esc, never mid-right-drag).
    if (active) return 1u;
    return blocked;                       // normal play : a bare move is a no-op in FFXI -> pass through
}

// slot 14 : KEYBOARD (key, down, blocked). `key` is a DirectInput scan code (DIK_*). We translate it
// through the OS keyboard LAYOUT (ToAsciiEx) so AZERTY / accents / Shift / AltGr / every symbol come
// out right -- NOT a hard-coded QWERTY table. While a config text field is focused we feed the char
// and CONSUME the key (return 1) so the game never sees it ; otherwise the key passes straight through.
static bool g_keyLog = false;   // //aio keylog -> dump every key event to aiohud_debug.log for diagnosing input bugs
unsigned int aio_plugin_key(u32 key, u32 b, u32 c) {
    // Press/release lives in bit 0x40 of b's LOW BYTE (b's low byte = the key-state flags Windower passes ;
    // 0x40 set = key DOWN / make, clear = key UP / break). RE'd from two live captures (2026-07-17, //aio keylog) :
    // it holds identically on both a working setup and the one that doubled -- 39/39 and every event respectively.
    //   History of the wrong turns : `pressed = (b > 0xFFFF)` read a pointer-ish high part of b -> broke where the
    //   RELEASE also carried a large b. `pressed = (c & 0x80000000)` read c's WM_KEY* transition bit -- clean and
    //   machine-independent on MY box, but a friend's Windower feeds GARBAGE in c (stack addresses : 001AEC00,
    //   18BA0000, FFFFFF00...), so its bit 31 was 0 on BOTH make and break -> every key fed TWICE ("g" -> "gg").
    //   b's 0x40 bit was the one signal stable across both machines -> that's the reliable press/release flag.
    //   `b`'s low byte also carries the MODIFIER bits (0x01/0x04/0x08), read separately below.
    const int  dik     = (int)key & 0xFF;
    const bool pressed = (b & 0x40u) != 0;   // b low-byte bit 0x40 : set = key DOWN (make) ; clear = key UP (break)
    // Windower hands us a US-POSITIONAL scan code (built from the VK via the US layout), NOT the raw hardware
    // scan -> recover the real VK by mapping it back through the US layout, then translate that VK to a char
    // with the USER's layout (AZERTY / accents / any locale -- e.g. AZERTY 'a' arrives as US scan 0x1E).
    static HKL usHkl = LoadKeyboardLayoutA("00000409", 0x80 /*KLF_NOTELLSHELL : load US, don't activate*/);
    const DWORD tid    = g_gameHwnd ? GetWindowThreadProcessId((HWND)g_gameHwnd, NULL) : 0;
    const HKL   layout = GetKeyboardLayout(tid);           // the user's real layout (e.g. French 040C)
    const UINT  vk     = MapVirtualKeyExA((UINT)dik, 1 /*MAPVK_VSC_TO_VK*/, usHkl);   // US scan -> real VK

    // MODIFIER STATE lives in b's LOW BYTE on EVERY event (verified over 200+ captured events) : bit 0x01 = Shift,
    // 0x04 = Ctrl, 0x08 = Alt. Reading it here is ROCK-SOLID. We do NOT track the modifier KEYS' press/release :
    // Windower streams those as a FLICKERING press/release burst while the mouse moves, which used to leave the
    // edit-mode axis-lock stuck on BOTH axes and type the wrong case. AltGr sets Ctrl+Alt here on its own.
    const bool mShift = (b & 0x01) != 0, mCtrl = (b & 0x04) != 0, mAlt = (b & 0x08) != 0;
    BYTE ks[256] = { 0 };
    if (mShift) ks[VK_SHIFT]   = 0x80;
    if (mCtrl)  ks[VK_CONTROL] = 0x80;
    if (mAlt)   ks[VK_MENU]    = 0x80;
    if (GetKeyState(VK_CAPITAL) & 1) ks[VK_CAPITAL] = 0x01;   // Caps Lock toggle (best-effort)
    aio::edit_set_modifiers(mShift, mCtrl, mAlt);              // edit-mode axis-lock (Shift/Ctrl) + Alt = free placement : always the TRUE state now

    int nc = 0; WCHAR wbuf[8] = { 0 };
    // Pass the ORIGINAL scan code (a valid US scan) -- recomputing the user-layout scan returns 0 for many
    // OEM/punctuation VKs, which made ToUnicode fail (the missing , ; : ! ? . / etc.).
    if (pressed && vk)
        nc = ToUnicodeEx(vk, (UINT)dik, ks, wbuf, 8, 0x4, layout);   // 0x4 = don't mutate kernel dead-key state
    if (g_keyLog) debug::log("KEY key=%08X b=%08X c=%08X dik=%02X vk=%02X pr=%d nc=%d ch=U+%04X ch1=U+%04X sh=%d ct=%d al=%d want=%d",
                             (unsigned)key, (unsigned)b, (unsigned)c, dik, vk, (int)pressed, nc, (unsigned)wbuf[0], (unsigned)wbuf[1],
                             (ks[VK_SHIFT] & 0x80) != 0, (ks[VK_CONTROL] & 0x80) != 0, (ks[VK_MENU] & 0x80) != 0,
                             (int)g_hud.config().wants_keys());

    // End (HELD) = "peek" : hide the ENTIRE HUD while End is down, restore it on release. Only when NOT typing
    // (inside a text field End = cursor-to-end, handled below). ONLY the dedicated End (DIK 0xCF, above the arrows) --
    // NOT the numpad 1/End (0x4F), which must stay a normal game key. Was: also 0x4F when NumLock off.
    // with NumLock off (nc == 0). Consume it so the game never sees End.
    if (!g_hud.config().wants_keys() && dik == 0xCF) {
        if (aio::ui_config().hidePeekMode) {          // TOGGLE : each fresh PRESS flips hidden/shown ; ignore the release
            static bool s_peek = false;
            if (pressed) { s_peek = !s_peek; g_hud.set_peek(s_peek); }
        } else {
            g_hud.set_peek(pressed);                   // HOLD : pressed = hide, release = show
        }
        return 1u;
    }

    // DIK_RETURN key-UP guard : Enter's key-DOWN commits + blurs the field the SAME frame, so by the time
    // its key-UP arrives wants_keys() is false and the stray release would reach the game (opening chat).
    // If we consumed the DOWN while typing, consume the matching UP too, regardless of the (now-cleared) focus.
    static bool s_ateReturn = false;
    if (dik == 0x1C) {
        if (pressed && g_hud.config().wants_keys()) s_ateReturn = true;
        else if (!pressed && s_ateReturn)         { s_ateReturn = false; return 1u; }
    }
    if (!g_hud.config().wants_keys()) return 0u;            // not typing -> never block the game
    // Insert toggles the Windower console -> NEVER swallow it, even while typing (extended DIK 0xD2, or the
    // numpad-0 scan 0x52 when NumLock is off so it produced no character).
    if (dik == 0xD2 || (dik == 0x52 && nc == 0)) return 0u;
    if (pressed) {                                          // feed on every press (taps once, holds auto-repeat)
        if      (dik == 0x0E) g_hud.config().feed_backspace();   // DIK_BACK
        else if (dik == 0x1C) g_hud.config().feed_enter();       // DIK_RETURN -> commit
        else if (dik == 0x01) g_hud.config().blur();             // DIK_ESCAPE -> leave the field
        // cursor / editing keys. Windower may give us the extended DIK (0xCB ...) OR the US-positional
        // scan that collides with the numpad (0x4B ...) -> accept the latter only when it produced NO
        // character (numlock off = navigation ; numlock on types a digit and falls through to feed_char).
        else if (dik == 0xCB || (dik == 0x4B && nc == 0)) g_hud.config().cursor_left();    // Left
        else if (dik == 0xCD || (dik == 0x4D && nc == 0)) g_hud.config().cursor_right();   // Right
        else if (dik == 0xC7 || (dik == 0x47 && nc == 0)) g_hud.config().cursor_home();    // Home
        else if (dik == 0xCF || (dik == 0x4F && nc == 0)) g_hud.config().cursor_end();     // End
        else if (dik == 0xD3 || (dik == 0x53 && nc == 0)) g_hud.config().feed_delete();    // Delete
        else if (nc != 0) {   // nc 1 = char ; nc -1 = dead key (^ ¨ ...) -> feed its spacing form ; nc 2 = combined
            unsigned w = (unsigned)wbuf[0];
            if (w >= 32 && w < 256 && w != 127) g_hud.config().feed_char(w);   // codepoint -> feed_char UTF-8-encodes it
        }
    }
    return 1u;   // consume EVERY key while typing -> the game stays quiet
}

void aio_plugin_unload()
{
    aio_subclass_remove();   // MUST come first : a WndProc pointing into a freed DLL crashes the client

    g_hud.dispose();
}

#ifndef AIOHUD_VERSION
#define AIOHUD_VERSION "dev"
#endif
// Launch the updater PowerShell script WITH NO WINDOW (native CreateProcess + CREATE_NO_WINDOW -- a Lua-spawned
// process always flashes a cmd console). The script checks the latest GitHub release, downloads it, WAITS for the
// AioHud.dll to unlock (the companion Lua addon //unloads it), extracts over plugins\, and writes data\update\done.txt.
// The spawned process is detached, so it survives this plugin being unloaded mid-update.
static void spawn_updater(bool checkOnly)
{
    // De-dupe FULL updates : the config button now spawns directly AND the AioUpdate addon may also send
    // `aio update` (from request.txt or a typed //aioupdate) -> without this, two PowerShell updaters could race
    // the same download/extract. One spawn per 5s window is plenty (checkOnly probes are unaffected).
    if (!checkOnly) {
        static DWORD s_lastFullSpawn = 0;
        const DWORD now = GetTickCount();
        if (s_lastFullSpawn != 0 && now - s_lastFullSpawn < 5000) return;
        s_lastFullSpawn = now;
    }
    char ps1[300], data[300], plugins[300];
    aio::plugin_path(ps1, sizeof(ps1), "assets\\aioupdate.ps1");
    aio::plugin_path(data, sizeof(data), "data");
    lstrcpynA(plugins, aio::plugin_dir(), sizeof(plugins));          // ...\plugins\AioHud
    char* s = strrchr(plugins, '\\'); if (s) *s = 0;                 // -> ...\plugins
    char cmd[1400];
    _snprintf(cmd, sizeof(cmd),
        "powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File \"%s\" -Current \"%s\" -Repo \"ejouanchicot/AioHud\" -Plugins \"%s\" -Data \"%s\"%s",
        ps1, AIOHUD_VERSION, plugins, data, checkOnly ? " -CheckOnly" : "");
    cmd[sizeof(cmd) - 1] = 0;
    STARTUPINFOA si; ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
    PROCESS_INFORMATION pi; ZeroMemory(&pi, sizeof(pi));
    if (CreateProcessA(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {   // CREATE_NO_WINDOW = hidden console ; the child survives this DLL unloading (its parent is pol.exe, not the DLL)
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    } else if (!checkOnly) {
        char e[96]; _snprintf(e, sizeof(e), ">>> AioHud : updater launch failed (CreateProcess err %lu) <<<", GetLastError());
        g_host.console().print(e);
    }
}

// ===== Update-tab bridge (called from the config page, which lives in namespace aio) =====
namespace aio {
// read data\update\check.txt -> 0 unknown/checking, 1 up-to-date, 2 update available, 3 error ; fills `ver`.
int aio_update_check_status(char* ver, int n)
{
    if (ver && n > 0) ver[0] = 0;
    char p[300]; aio::plugin_path(p, sizeof(p), "data\\update\\check.txt");
    FILE* f = fopen(p, "rb"); if (!f) return 0;
    char buf[160]; size_t got = fread(buf, 1, sizeof(buf) - 1, f); buf[got] = 0; fclose(f);
    char word[32] = { 0 }, v[96] = { 0 }; sscanf(buf, "%31s %95s", word, v);
    if (ver && n > 0) lstrcpynA(ver, v, n);
    if (!strcmp(word, "UPTODATE"))  return 1;
    if (!strcmp(word, "AVAILABLE")) return 2;
    if (!strcmp(word, "ERROR"))     return 3;
    return 0;
}
// re-run the no-window check (Update tab "Check again").
void aio_update_spawn_check() { spawn_updater(true); }
// ---- live update-progress state (Update tab shows "Checking.../Downloading..." from the click until the addon
//      //unloads this DLL) : the PS writes phases to data\update\done.txt (READY/OK/UPTODATE/ERROR). We latch the
//      FINAL phase in memory because the addon deletes done.txt as soon as it acts on it. ----
static bool  g_updBusy  = false;  // an update was requested from the Update tab this session
static int   g_updFinal = 0;      // latched terminal phase : 0 none, 1 up-to-date, 2 ok, 3 error
static char  g_updMsg[96] = { 0 };// latched version (up-to-date/ok) or error message
static DWORD g_updReqT  = 0;      // tick at request -> soft-timeout if the addon never answers (not loaded?)

// write the request flag the AioUpdate addon polls -> it runs the full //aioupdate (unload/download/load).
void aio_update_request()
{
    char dir[300]; aio::plugin_path(dir, sizeof(dir), "data\\update");
    CreateDirectoryA(dir, NULL);
    char done[340]; _snprintf(done, sizeof(done), "%s\\done.txt", dir); done[sizeof(done) - 1] = 0;
    DeleteFileA(done);   // drop any stale phase so the tab reads only THIS run's progress
    char p[320]; _snprintf(p, sizeof(p), "%s\\request.txt", dir); p[sizeof(p) - 1] = 0;
    FILE* f = fopen(p, "w"); if (f) { fputs("go", f); fclose(f); }   // legacy signal (an old addon still watches this)
    g_updBusy = true; g_updFinal = 0; g_updMsg[0] = 0; g_updReqT = GetTickCount();
    spawn_updater(false);   // START THE DOWNLOAD NOW -> no longer waits on the addon's poll ; the addon only does the //unload + //load (de-dupe guard prevents a double PS)
}
// forget the in-progress state (Update tab's terminal-state button -> back to the normal check flow).
void aio_update_clear() { g_updBusy = false; g_updFinal = 0; g_updMsg[0] = 0; }
// progress for the Update tab : 0 idle, 1 working (spinner/bar), 2 up-to-date, 3 updated, 4 error. Fills `msg`
// with the version (2/3) or the error text (4). Reads data\update\done.txt, latching the terminal phase.
int aio_update_progress(char* msg, int n)
{
    if (msg && n > 0) msg[0] = 0;
    if (!g_updBusy) return 0;
    if (g_updFinal) { if (msg && n > 0) lstrcpynA(msg, g_updMsg, n); return g_updFinal + 1; }
    char p[320]; aio::plugin_path(p, sizeof(p), "data\\update\\done.txt");
    FILE* f = fopen(p, "rb");
    if (!f) {   // requested but no phase yet -> "starting/checking" ; time out if the addon never answers (not loaded?)
        if (GetTickCount() - g_updReqT > 30000u) { g_updFinal = 3; lstrcpynA(g_updMsg, "no response (is AioUpdate loaded? type //aioupdate)", sizeof(g_updMsg)); return 4; }
        return 1;
    }
    char buf[160]; size_t got = fread(buf, 1, sizeof(buf) - 1, f); buf[got] = 0; fclose(f);
    char word[32] = { 0 }; int off = 0; sscanf(buf, "%31s%n", word, &off);
    const char* rest = buf + off; while (*rest == ' ' || *rest == '\t') ++rest;   // remainder = version or error text
    char clean[96]; lstrcpynA(clean, rest, sizeof(clean));
    for (char* q = clean; *q; ++q) if (*q == '\r' || *q == '\n') { *q = 0; break; }
    if (!strcmp(word, "READY")) { if (msg && n > 0) lstrcpynA(msg, clean, n); return 1; }   // downloaded, unload imminent
    if (!strcmp(word, "UPTODATE")) { g_updFinal = 1; lstrcpynA(g_updMsg, clean, sizeof(g_updMsg)); if (msg && n > 0) lstrcpynA(msg, clean, n); return 2; }
    if (!strcmp(word, "OK"))       { g_updFinal = 2; lstrcpynA(g_updMsg, clean, sizeof(g_updMsg)); if (msg && n > 0) lstrcpynA(msg, clean, n); return 3; }
    if (!strcmp(word, "ERROR"))    { g_updFinal = 3; lstrcpynA(g_updMsg, clean, sizeof(g_updMsg)); if (msg && n > 0) lstrcpynA(msg, clean, n); return 4; }
    return 1;
}
const char* aio_version_string() { return AIOHUD_VERSION; }
}   // namespace aio

void aio_plugin_command(const char* cmd)
{
    if (!cmd) return;
    // lower-case a copy so keywords are case-insensitive (//AIO HP 50 == //aio hp 50).
    char buf[256]; int i = 0;
    for (; cmd[i] && i < 255; i++) { char c = cmd[i]; buf[i] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }
    buf[i] = 0;

    // //aio profile save|load|delete <name> | profile list -> named snapshots of the whole config.
    // Dispatched FIRST : the free-text <name> can contain other command keywords ("mydemo", "sim1",
    // "myconfig", ...) that the substring-matched branches below (strstr over the whole buffer) would
    // otherwise capture. (The dispatch is substring-based by design -- e.g. "party demo" -- so a proper
    // verb tokenizer would be the fuller fix ; this keeps the free-text command safe for now.)
    if (strstr(buf, "profile")) {
        const char* a = strstr(buf, "profile") + 7; while (*a == ' ' || *a == '\t') ++a;
        if (strncmp(a, "list", 4) == 0) {
            aio::profile_refresh();
            const int n = aio::profile_count();
            char msg[256];
            if (n == 0) { g_host.console().print(">>> profiles: (none) <<<"); return; }
            int off = _snprintf(msg, sizeof(msg), ">>> profiles (%d): ", n);
            if (off < 0) off = 0;                                     // truncated already (shouldn't happen)
            for (int k = 0; k < n && off < (int)sizeof(msg) - 8; ++k) {   // leave room for " <<<" + NUL
                int w = _snprintf(msg + off, sizeof(msg) - off, "%s%s", k ? ", " : "", aio::profile_name(k));
                if (w < 0) { off = (int)sizeof(msg) - 5; break; }     // name overflowed the buffer -> stop
                off += w;
            }
            _snprintf(msg + off, sizeof(msg) - off, " <<<");
            msg[sizeof(msg) - 1] = 0;                                 // _snprintf may not NUL-terminate on truncation
            g_host.console().print(msg);
            return;
        }
        int op = 0; const char* verb = 0;
        if      (strncmp(a, "save", 4)   == 0) { op = 1; verb = a + 4; }
        else if (strncmp(a, "load", 4)   == 0) { op = 2; verb = a + 4; }
        else if (strncmp(a, "delete", 6) == 0) { op = 3; verb = a + 6; }
        else if (strncmp(a, "del", 3)    == 0) { op = 3; verb = a + 3; }
        if (op) {
            while (*verb == ' ' || *verb == '\t') ++verb;
            const char* name = cmd + (verb - buf);                       // same offset in the ORIGINAL -> case preserved
            char nm[64]; int j = 0; while (name[j] && j < 63) { nm[j] = name[j]; ++j; } nm[j] = 0;
            while (j > 0 && (nm[j-1] == ' ' || nm[j-1] == '\t' || nm[j-1] == '\r' || nm[j-1] == '\n')) nm[--j] = 0;
            if (j == 0) { g_host.console().print(">>> profile: a name is required <<<"); return; }
            bool ok = (op == 1) ? aio::profile_save(nm) : (op == 2) ? aio::profile_load(nm) : aio::profile_delete(nm);
            char msg[128]; sprintf(msg, ">>> profile %s '%s' : %s <<<", op == 1 ? "save" : op == 2 ? "load" : "delete", nm, ok ? "OK" : "FAILED");
            g_host.console().print(msg);
            return;
        }
        g_host.console().print(">>> usage: //aio profile save|load|delete <name>  |  profile list <<<");
        return;
    }


    // //aio party N  -> fake roster for previewing the layout. N = TOTAL member count 0-18 ; YOU
    // (the player) are always member #1, so the boxes fill party -> alliance 1 -> alliance 2 :
    //   0        -> off (back to live data)          1-6   -> a party of N
    //   7-12     -> full party (6) + alliance 1 (N-6)
    //   13-18    -> full party (6) + alliance 1 (6) + alliance 2 (N-12)
    // //aio party off (or demo off) -> live.  Legacy still accepted : "demo", "alliance1/2 demo".
    // Caught here first so a "party N" / "demo" / "off" isn't shadowed by a later "party"-prefixed handler.
    {
        const bool isDemo  = strstr(buf, "demo")      != 0;
        const bool isParty = strstr(buf, "party")     != 0;
        const bool a1      = strstr(buf, "alliance1") != 0;
        const bool a2      = strstr(buf, "alliance2") != 0;
        const bool off     = strstr(buf, "off")       != 0;
        int num = -1;                                    // the only number these commands take = the count
        for (const char* p = buf; *p; ++p) if (*p >= '0' && *p <= '9') { num = 0; do num = num * 10 + (*p++ - '0'); while (*p >= '0' && *p <= '9'); break; }
        // a BARE "//aio party" (no count / off / alliance) is the debug dump -> don't intercept it.
        if (isDemo || a1 || a2 || (isParty && (off || num >= 0))) {
            int total = off ? 0 : a2 ? 18 : a1 ? 12 : num >= 0 ? num : 6;   // bare "demo"/"party demo" -> party of 6
            aio::set_party_demo_total(total);
            return;
        }
    }

    // //aio ept <nm> -> track that Abyssea NM in the EmpyPop box + show it. "//aio ept" alone toggles the box ;
    // "ept list" prints the NM names ; "ept off"/"hide" hides it. "pop" is kept as an alias. The friendly driver
    // for the module (no config needed). Case is already folded (buf is lowercased), so "//aio EPT" works too.
    // "ept"/"pop" are 3 chars each, and NEITHER is the dev probe token "ep" (tok_arg requires a delimiter after
    // "ep", which "ept" lacks) -> no clash. Placed after the party/demo block ; NM keys are letters+spaces only,
    // so none collide with the keywords above, and we fully handle + return here so the name never leaks below.
    if (const char* pp = (strstr(buf, "ept") ? strstr(buf, "ept") : strstr(buf, "pop"))) {
        const char* a = pp + 3; while (*a == ' ' || *a == '\t') ++a;
        aio::UiConfig& C = aio::ui_config();
        // Output goes to the FFXI CHAT LOG (not the Windower console) via the reversed ffxi()->add_to_chat, so
        // it reads like the GearSwap message system. Colour is IN-BAND : a 0x1F byte + a palette index switches
        // colour mid-line (160 gray, 50 yellow, 158 green) -- built with %c, never "\xNN" (which would merge with
        // a following hex digit). `mode` sets the base channel ; 1 = a normal always-visible system line.
        const int GRAY = 160, YEL = 50, GRN = 158, MODE = 1;
        auto chat = [](const char* s) { g_host.ffxi().add_to_chat(MODE, s); };
        if (!*a) {                                                  // bare "//aio ept" -> toggle
            C.epShow = !C.epShow; aio::save_ui_config();
            char m[64]; _snprintf(m, sizeof(m), "%c%c[EmpyPop] %s", 0x1F, C.epShow ? GRN : GRAY, C.epShow ? "shown" : "hidden");
            chat(m); return;
        }
        if (strncmp(a, "off", 3) == 0 || strncmp(a, "hide", 4) == 0) {
            C.epShow = 0; aio::save_ui_config();
            char m[48]; _snprintf(m, sizeof(m), "%c%c[EmpyPop] hidden", 0x1F, GRAY);
            chat(m); return;
        }
        if (strncmp(a, "list", 4) == 0) {                           // the NM names, coloured, in the chat log
            // FFXI resets colour to WHITE at every line break -- INCLUDING the game's own word-wrap, whose position
            // depends on each player's chat width, so we can't predict it. Fix (as suggested) : re-emit the colour
            // byte before EVERY name, so wherever the game wraps, each name still carries its own colour. We only
            // split into separate add_to_chat calls to stay under a single FFXI chat line's byte cap.
            const int BUDGET = 120;
            char sep[40]; _snprintf(sep, sizeof(sep), "%c%c%s", 0x1F, GRAY, "==============================");
            char hdr[64]; _snprintf(hdr, sizeof(hdr), "%c%c%s", 0x1F, YEL, "[EmpyPop] NM list  //aio ept <name>");
            chat(sep); chat(hdr); chat(sep);
            char line[256]; int w = 0;
            for (int k = 0; k < aio::NMS_N; ++k) {
                const char* nm = aio::NMS[k].en;
                const int col = (lstrcmpiA(aio::NMS[k].key, C.epTrack) == 0) ? YEL : GRN;   // tracked NM -> yellow
                const int need = (w ? 2 : 0) + 2 + (int)strlen(nm);   // ", " + colour(2) + name
                if (w && w + need > BUDGET) { chat(line); w = 0; }    // flush -> new chat line (stays under the cap)
                if (w) { line[w++] = ','; line[w++] = ' '; }
                line[w++] = 0x1F; line[w++] = (char)col;              // colour re-stated before THIS name
                w += _snprintf(line + w, sizeof(line) - w, "%s", nm);
            }
            if (w) chat(line);
            chat(sep);
            return;
        }
        // otherwise `a` is an NM-name pattern : case-insensitive substring on the (lowercase) key. buf is already
        // lowercased, and NMS[].key is lowercase, so a plain strstr matches ("dynamis" -> Arch Dynamis Lord).
        int hit = -1, nhit = 0;
        for (int k = 0; k < aio::NMS_N; ++k) if (strstr(aio::NMS[k].key, a)) { if (!nhit) hit = k; ++nhit; }
        if (nhit == 0) {
            char m[96]; _snprintf(m, sizeof(m), "%c%c[EmpyPop] no NM matches -- try //aio ept list", 0x1F, YEL);
            chat(m);
        } else if (nhit > 1) {                                      // ambiguous -> show the matches so the user can refine
            char line[240]; int w = _snprintf(line, sizeof(line), "%c%c[EmpyPop] several match: %c%c", 0x1F, YEL, 0x1F, GRN);
            bool first = true;
            for (int k = 0; k < aio::NMS_N && w < (int)sizeof(line) - 24; ++k)
                if (strstr(aio::NMS[k].key, a)) { w += _snprintf(line + w, sizeof(line) - w, "%s%s", first ? "" : ", ", aio::NMS[k].en); first = false; }
            chat(line);
        } else {
            lstrcpynA(C.epTrack, aio::NMS[hit].key, sizeof(C.epTrack));
            C.epShow = 1; aio::save_ui_config();
            char m[96]; _snprintf(m, sizeof(m), "%c%c[EmpyPop] tracking %c%c%s", 0x1F, GRAY, 0x1F, YEL, aio::NMS[hit].en); m[sizeof(m) - 1] = 0;
            chat(m);
        }
        return;
    }

    // //aio menu N -> select the N-th window theme (1-based). The skin-select digit form is caught here.
    if (strstr(buf, "menu")) {
        const char* mp = strstr(buf, "menu") + 4; while (*mp == ' ') ++mp;
        if (*mp >= '0' && *mp <= '9') { g_hud.set_skin(atoi(mp) - 1); return; }
    }

#ifdef AIOHUD_PROBES
    // Dev-only diagnostic commands. Runs here so no shipping command above is shadowed ; anything it
    // doesn't recognise falls through to the shipping handlers below.
    if (aio::probes::command(buf)) return;
#endif

    // //aio sim [N] -> append N (0-5) FAKE members to the LIVE party, so the box grows and the alliances
    // react to the main-party size for testing. //aio sim 0 (or sim off) -> back to the real size.
    if (strstr(buf, "sim")) {
        const char* p = strstr(buf, "sim") + 3; while (*p == ' ' || *p == '\t') ++p;
        const int v = strstr(buf, "off") ? 0 : ((*p >= '0' && *p <= '5') ? (*p - '0') : 1);
        aio::set_party_sim_extra(v);
        return;
    }

    // //aio config -> toggle the full-screen configuration overlay. "config N" = select tab N (1..3).
    if (strstr(buf, "update")) {   // //aio update -> spawn the no-window updater (the AioUpdate Lua addon drives the unload/load)
        aio::g_updBusy = true; aio::g_updFinal = 0; aio::g_updMsg[0] = 0; aio::g_updReqT = GetTickCount();   // Update tab shows live progress whether the trigger was the button or a typed //aioupdate
        spawn_updater(false);      // silent on purpose : //aioupdate must produce no console output
        return;
    }
    if (strstr(buf, "keylog")) {   // //aio keylog -> toggle dumping every key event to aiohud_debug.log (input diagnosis)
        g_keyLog = !g_keyLog;
        g_host.console().print(g_keyLog ? ">>> AioHud : key log ON (type in the profile name field, then send aiohud_debug.log) <<<"
                                        : ">>> AioHud : key log OFF <<<");
        return;
    }
    // NB the name: NOT "focustrace". Command dispatch is a chain of strstr(), and the pre-existing `//aio focus`
    // probe runs earlier -- it matches the "focus" INSIDE "focustrace" and swallows the command, so the trace
    // silently never armed while printing the other probe's output. Any new command must not contain an existing
    // one as a substring.
    if (strstr(buf, "ftrace")) {   // //aio ftrace -> explain the Timers "track per job" decision for the next N self-buff rows
        aio::timers_focus_trace(180);   // 180 SECONDS -- long enough for a full buff cycle plus the alert window
        g_host.console().print(">>> AioHud : ftrace ARMED (have the Hidden+Focus buff up, then send Windower\\plugins\\aiohud_debug.log ; look for FOCUS lines) <<<");
        return;
    }
    if (strstr(buf, "geartrace")) {   // //aio geartrace -> trace the next N gear-icon resolutions to aiohud_debug.log (raw-item-ID diagnosis)
        aio::set_gear_trace(120);
        g_host.console().print(">>> AioHud : gear trace ARMED (open the equipment viewer / change gear, then send Windower\\plugins\\aiohud_debug.log ; look for GEAR lines) <<<");
        return;
    }
    if (strstr(buf, "dbflog")) {   // //aio dbflog -> trace the next N target-debuff mutations to aiohud_debug.log (debuff-box diagnosis)
        aio::party().set_debuff_trace(400);
        g_host.console().print(">>> AioHud : debuff log ARMED (cast a debuff on a mob, melee/sleep it, then send Windower\\plugins\\aiohud_debug.log ; look for DBF lines) <<<");
        return;
    }
    if (strstr(buf, "config")) {
        // In edit layout the config flag is kept "open" (toolbar + mouse capture) -> a bare //aio config
        // must act like the "Done" button : leave edit mode (persisting positions/sizes) AND close, not
        // just drop the toolbar while editLayout silently stays on.
        if (aio::ui_config().editLayout) {
            aio::ui_config().editLayout = false;
            aio::save_ui_config();
            g_hud.config().set_open(false);
            return;
        }
        const char* cp = strstr(buf, "config") + 6; while (*cp == ' ') ++cp;
        if (*cp >= '1' && *cp <= '3') { g_hud.config().set_tab(atoi(cp) - 1); g_hud.config().set_open(true); }
        else                          g_hud.config().toggle();
        return;
    }
    if (strstr(buf, "timers")) {                          // //aio timers reset -> flush live buff/recast timers + focus "OUT" alerts
        if (strstr(buf, "reset")) { aio::timers_reset(); g_host.console().print(">>> AioHud : timers reset <<<"); }
        return;
    }
    if (strstr(buf, "edit")) {                            // toggle layout edit mode (drag/resize boxes on the live game)
        bool e = !aio::ui_config().editLayout;
        aio::ui_config().editLayout = e;
        g_hud.config().set_open(e);                       // keep config "open" so the mouse is captured + toolbar shows
        if (!e) aio::save_ui_config();                    // persist positions/sizes when leaving edit mode
        return;
    }

    // //aio layout -> load design/exports/layout.json, log the widgets, and save a round-trip copy.
    // (checked BEFORE "lay" because "layout" contains "lay".)
    if (strstr(buf, "layout")) {
        aio::Layout lay;
        if (aio::load_layout(LAYOUT_PATH(), lay)) {
            char msg[160];
            sprintf(msg, ">>> layout OK : %d widgets, %d zones, ref viewport %dx%d <<<",
                    (int)lay.widgets.size(), (int)lay.zones.size(), (int)lay.vpW, (int)lay.vpH);
            g_host.console().print(msg);
            for (size_t k = 0; k < lay.widgets.size(); ++k) {
                const aio::LWidget& w = lay.widgets[k];
                // NB: debug::log uses wvsprintfA which has NO %f -> log the % positions as
                // integer.tenths via %d to avoid the float-format crash.
                debug::log("  %-10s %-12s %c%c pos=%d.%d,%d.%d w=%-4s z=%2d vis=%d jobs=%d",
                           w.id.c_str(), w.type.c_str(), w.av, w.ah,
                           (int)w.x, (int)((w.x < 0 ? -w.x : w.x) * 10) % 10,
                           (int)w.y, (int)((w.y < 0 ? -w.y : w.y) * 10) % 10,
                           w.wAuto ? "auto" : "fix", w.z, (int)w.visible, (int)w.jobs.size());
            }
            if (aio::save_layout(LAYOUT_RT_PATH(), lay)) debug::log("  round-trip -> layout.roundtrip.json");
            g_hud.request_reload();            // defer the actual rebuild to the render thread (no draw-race crash)
            g_host.console().print(">>> layout reload queued (applies next frame) <<<");
        } else {
            g_host.console().print(">>> layout LOAD FAILED (design/exports/layout.json) <<<");
        }
        return;
    }

    if (strstr(buf, "res")) {                             // backbuffer vs window-client size -> the 1:1 resolution to set
        u32 dev = g_host.service_raw(2);
        u32 hwnd = valid_ptr(dev) ? aio::dFocusWindow(dev) : 0;
        RECT rc = {0}; int cw = 0, ch = 0;
        if (hwnd && GetClientRect((HWND)hwnd, &rc)) { cw = rc.right - rc.left; ch = rc.bottom - rc.top; }
        int bbw = (int)g_hud.screenW(), bbh = (int)g_hud.screenH();
        char msg[200];
        if (cw > 0 && (cw != bbw || ch != bbh))
            sprintf(msg, ">>> backbuffer %dx%d, window %dx%d -- game is SCALED. Set game res to %dx%d for 1:1 (no artifacts) <<<", bbw, bbh, cw, ch, cw, ch);
        else
            sprintf(msg, ">>> render %dx%d = window %dx%d : 1:1, no scaling <<<", bbw, bbh, cw, ch);
        debug::log("RES: backbuffer %dx%d  window-client %dx%d", bbw, bbh, cw, ch);
        g_host.console().print(msg);
        return;
    }

    const char* lp = strstr(buf, "lay");                 // //aio lay N -> effect layers
    if (lp) {
        const char* p = lp + 3; while (*p && (*p < '0' || *p > '9')) p++;
        if (*p && g_hud.bars()) g_hud.bars()->set_layers(*p - '0');
        return;
    }
    // catch-all : the debug FILL command (//aio hp 50 / //aio 100 50 30). Only fire it when the command
    // actually looks like one (an hp/mp/tp keyword, or a leading digit) -> a random typo no longer silently
    // rewrites the player's HP/MP/TP fills.
    { const char* p = buf; while (*p == ' ' || *p == '\t') ++p;
      if (strstr(buf, "hp") || strstr(buf, "mp") || strstr(buf, "tp") || (*p >= '0' && *p <= '9')) parse_fill_string(buf);
      else g_host.console().print(">>> aio: unknown command <<<"); }
}

const char* aio_plugin_name()        { return "AioHud"; }
// NB: Windower uses GetDescription (lowercased) as the plugin's CONSOLE COMMAND
// alias -> this string IS the //command name. Keep it a single clean word.
const char* aio_plugin_description() { return "aio"; }
