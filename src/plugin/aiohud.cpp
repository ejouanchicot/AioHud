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
#include "model/layout.h"
#include "model/party_state.h"
#include "model/game_mem.h"
#include "model/ui_config.h"
#include "model/paths.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <windows.h>

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
// companion updater addon then auto-loads with Windower, no manual //lua load needed. Takes effect next launch.
static void ensure_addon_autoload()
{
    char root[300];
    lstrcpynA(root, aio::plugin_dir(), sizeof(root));   // ...\plugins\AioHud
    char* s = strrchr(root, '\\'); if (s) *s = 0;        // -> ...\plugins
    s = strrchr(root, '\\'); if (s) *s = 0;              // -> ...        (Windower root)
    char ini[360]; _snprintf(ini, sizeof(ini), "%s\\scripts\\init.txt", root); ini[sizeof(ini) - 1] = 0;

    FILE* f = fopen(ini, "rb");
    if (f) {
        static char buf[16384];
        size_t n = fread(buf, 1, sizeof(buf) - 1, f); buf[n] = 0; fclose(f);
        if (strstr(buf, "aioupdate")) return;            // already registered -> nothing to do
    }
    char dir[360]; _snprintf(dir, sizeof(dir), "%s\\scripts", root); dir[sizeof(dir) - 1] = 0;
    CreateDirectoryA(dir, NULL);                         // scripts\ may not exist yet
    f = fopen(ini, "ab");                                // append (don't clobber the user's other init lines)
    if (f) { fputs("\r\nlua load aioupdate\r\n", f); fclose(f); }
}

// ===== IPlugin hooks (declared in windower_plugin.h) =====

void aio_plugin_init(PluginManager host)
{
    g_host = host;
    ensure_addon_autoload();           // make the //aioupdate companion addon auto-load with Windower
    debug::clear();
    debug::log("AioHUD init: device = 0x%08X", host.service_raw(2));
    aio::load_ui_config();             // restore saved theme / font / box positions + sizes
    aio::party().load();               // cached roster (instant even before the game is ready)
    aio::party().load_from_memory();   // LIVE roster+vitals from FFXI memory -> correct party at load
    debug::log("party at load: %d member(s)", aio::party().count);
    for (int i = 0; i < aio::party().count; ++i)
        debug::log("  m%d '%s' hp=%d/%d mp=%d/%d hpp=%d zone=%d", i, aio::party().m[i].name,
                   aio::party().m[i].hp, aio::party().m[i].maxHp, aio::party().m[i].mp, aio::party().m[i].maxMp,
                   aio::party().m[i].hpp, aio::party().m[i].zone);
    g_hud.apply_layout(LAYOUT_PATH());   // place widgets from the descriptor (keeps defaults if absent)
    host.console().print(">>> AioHUD -- //aio hp|mp|tp <n>  |  lay 1-4  |  layout (reload) <<<");
}

void aio_plugin_render() {}   // slot 5 : unused (we draw on slot 6, like FFXIDB)


// the game's window handle, cached each frame from the D3D device (the mouse hook runs without a
// device, so it reads this to tell whether the GAME is the OS foreground window).
static u32 g_gameHwnd = 0;


void aio_plugin_render6()
{
    u32 dev = g_host.service_raw(2);   // host vtbl[2] = D3D8 device
    if (valid_ptr(dev)) g_gameHwnd = aio::dFocusWindow(dev);
    g_hud.render(dev);
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
    // Only capture the mouse when the GAME window is the OS foreground. Otherwise a click coming back
    // INTO the game (from a browser / the desktop) must reach Windows so it re-activates the window.
    const bool focused = g_gameHwnd && (HWND)g_gameHwnd == GetForegroundWindow();
    const bool active  = focused && (g_hud.config().is_open() || aio::ui_config().editLayout);

    // A left-click is treated as ONE gesture (down -> moves -> up) with a SINGLE decision locked at the
    // down : either we swallow the whole gesture, or we pass it. Deciding once (not per-event) is what
    // fixes the "stuck character" : the old code passed the down (game not yet focused) but swallowed the
    // up (game focused by then) -> the game saw a button stuck DOWN forever and the avatar kept walking.
    static bool inGesture = false;   // a left button is currently held
    static bool blockGest = false;   // ...and we're swallowing this gesture (vs a refocus click we pass)

    // Wheel : while the config/edit overlay is up, capture it (edit -> box resize ; Help tab -> scroll)
    // and consume it so the game camera doesn't zoom underneath. Otherwise pass through.
    if ((int)delta != 0) {
        if (active) { aio::ui_config().wheel += ((int)delta > 0) ? 1 : -1; return 1u; }
        // normal mode : zoom the minimap (player-centred) when the cursor is over it -> swallow so the game camera doesn't zoom too.
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
    if (eventtype == 1) {                 // Ldown : lock the gesture's fate now
        inGesture = true;
        blockGest = active;               // focused + overlay -> swallow ; else pass (let Windows activate)
        return blockGest ? 1u : blocked;
    }
    if (eventtype == 2) {                 // Lup : end the gesture with the SAME fate as its down
        const bool b = blockGest;
        inGesture = false; blockGest = false;
        return b ? 1u : blocked;
    }
    // mid-gesture move : swallow it either way. While swallowing it's obvious ; while PASSING (a refocus
    // click) we still eat the moves so a drag can't trigger FFXI mouselook -> the down+up alone is a
    // stationary click that just re-focuses, the avatar never turns.
    if (inGesture) return 1u;
    return blocked;                       // bare hover : pass through (a move is a no-op in FFXI)
}

// slot 14 : KEYBOARD (key, down, blocked). `key` is a DirectInput scan code (DIK_*). We translate it
// through the OS keyboard LAYOUT (ToAsciiEx) so AZERTY / accents / Shift / AltGr / every symbol come
// out right -- NOT a hard-coded QWERTY table. While a config text field is focused we feed the char
// and CONSUME the key (return 1) so the game never sees it ; otherwise the key passes straight through.
unsigned int aio_plugin_key(u32 key, u32 b, u32 /*c*/) {
    // RE'd from a live capture : a = DirectInput scan code ; b distinguishes press vs release --
    // a PRESS carries a large value (a pointer, b > 0xFFFF), a RELEASE a small one (0 / modifier
    // bits). (My earlier "b != 0 = pressed" was wrong : a release WITH a modifier held is small but
    // non-zero -> it looked pressed -> keys stuck.) c is junk.
    const int  dik     = (int)key & 0xFF;
    const bool pressed = (b > 0xFFFF);
    // Windower hands us a US-positional scan code (it built it from the VK via the US layout), NOT the
    // hardware scan code -> mapping it back through the user's layout double-shifts it (AZERTY 'a' came
    // in as US scan 0x1E, which French maps to 'q' = the QWERTY symptom). So : (1) undo with the US
    // layout to recover the real VK, (2) translate that VK to a char with the USER's (game) layout.
    static HKL usHkl = LoadKeyboardLayoutA("00000409", 0x80 /*KLF_NOTELLSHELL : load US, don't activate*/);
    DWORD tid = g_gameHwnd ? GetWindowThreadProcessId((HWND)g_gameHwnd, NULL) : 0;
    HKL   frHkl = GetKeyboardLayout(tid);                   // the user's real layout (e.g. French 040C)
    static BYTE ks[256] = { 0 };
    const UINT vk = MapVirtualKeyExA((UINT)dik, 1 /*MAPVK_VSC_TO_VK*/, usHkl);   // US scan -> real VK
    if (vk) ks[vk & 0xFF] = pressed ? 0x80 : 0x00;          // keep Shift/Ctrl/Alt/AltGr current
    aio::edit_set_modifiers((ks[VK_SHIFT] & 0x80) != 0, (ks[VK_CONTROL] & 0x80) != 0);   // expose to the render thread (edit-mode axis-lock)

    int nc = 0; WCHAR wbuf[8] = { 0 };
    // Pass the ORIGINAL scan code (a valid US scan) -- recomputing the user-layout scan returns 0 for
    // many OEM/punctuation VKs, which made ToUnicode fail -> the missing , ; : ! ? . / etc.
    if (pressed && vk) {
        // AltGr = Ctrl+Alt. Windower may forward only Alt -> force Ctrl while Alt is held so ToUnicode
        // emits the AltGr glyphs (@ # { [ | ...). Temporary, restored right after.
        const BYTE savedCtrl = ks[VK_CONTROL];
        if (ks[VK_MENU] & 0x80) ks[VK_CONTROL] = 0x80;
        nc = ToUnicodeEx(vk, (UINT)dik, ks, wbuf, 8, 0x4, frHkl);   // 0x4 = don't mutate kernel state
        ks[VK_CONTROL] = savedCtrl;
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
            if (w >= 32 && w < 256 && w != 127) g_hud.config().feed_char((char)w);
        }
    }
    return 1u;   // consume EVERY key while typing -> the game stays quiet
}

void aio_plugin_unload()
{
    g_hud.dispose();
}

#ifndef AIOHUD_VERSION
#define AIOHUD_VERSION "dev"
#endif
// Launch the updater PowerShell script WITH NO WINDOW (native CreateProcess + CREATE_NO_WINDOW -- a Lua-spawned
// process always flashes a cmd console). The script checks the latest GitHub release, downloads it, WAITS for the
// AioHud.dll to unlock (the companion Lua addon //unloads it), extracts over plugins\, and writes data\update\done.txt.
// The spawned process is detached, so it survives this plugin being unloaded mid-update.
static void spawn_updater()
{
    char ps1[300], data[300], plugins[300];
    aio::plugin_path(ps1, sizeof(ps1), "assets\\aioupdate.ps1");
    aio::plugin_path(data, sizeof(data), "data");
    lstrcpynA(plugins, aio::plugin_dir(), sizeof(plugins));          // ...\plugins\AioHud
    char* s = strrchr(plugins, '\\'); if (s) *s = 0;                 // -> ...\plugins
    char cmd[1400];
    _snprintf(cmd, sizeof(cmd),
        "powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File \"%s\" -Current \"%s\" -Repo \"Tetsouo/AioHud\" -Plugins \"%s\" -Data \"%s\"",
        ps1, AIOHUD_VERSION, plugins, data);
    cmd[sizeof(cmd) - 1] = 0;
    STARTUPINFOA si; ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
    PROCESS_INFORMATION pi; ZeroMemory(&pi, sizeof(pi));
    if (CreateProcessA(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {   // CREATE_NO_WINDOW = hidden console ; the child survives this DLL unloading (its parent is pol.exe, not the DLL)
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    } else {
        char e[96]; _snprintf(e, sizeof(e), ">>> AioHud : updater launch failed (CreateProcess err %lu) <<<", GetLastError());
        g_host.console().print(e);
    }
}

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
        spawn_updater();           // silent on purpose : //aioupdate must produce no console output
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
