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
#include "ui/hud.h"
#include "model/layout.h"
#include "model/party_state.h"
#include "model/game_mem.h"
#include "model/ui_config.h"
#include "model/spells_gen.h"
#include "model/abilities_gen.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <windows.h>

// RE: scan all readable memory for a byte pattern and log every hit + its context.
// Used to locate the in-memory party structure (search the player's name), then a
// static pointer to it (search the structure's address).
static void mem_scan(const unsigned char* pat, int len) {
    if (len < 3) return;
    MEMORY_BASIC_INFORMATION mbi;
    unsigned addr = 0x00010000, hits = 0;
    const unsigned MASK_RD = PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY;
    while (addr < 0x7FFE0000 && hits < 60) {
        if (!VirtualQuery((void*)addr, &mbi, sizeof(mbi))) break;
        unsigned base = (unsigned)mbi.BaseAddress, sz = (unsigned)mbi.RegionSize;
        bool rd = (mbi.State == MEM_COMMIT) && (mbi.Protect & MASK_RD) && !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS));
        if (rd && sz) {
            __try {
                const unsigned char* e = (const unsigned char*)base + sz - len;
                for (const unsigned char* p = (const unsigned char*)base; p <= e && hits < 60; ++p) {
                    if (p[0] == pat[0] && !memcmp(p, pat, len)) {
                        windower::debug::log("hit @%08X (region %08X prot %X)", (unsigned)p, base, mbi.Protect);
                        windower::debug::hexdump("  ", (unsigned)p - 16, 80);
                        ++hits;
                    }
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
        addr = base + sz;
    }
    windower::debug::log("scan done: %u hits", hits);
}

// RE: collect every readable 4-byte-aligned address whose u32 == `want` (cap `cap`).
// Separate function so the SEH __try has no C++ object unwinding (C2712) in the caller.
static int collect_u32_hits(unsigned want, unsigned* out, int cap) {
    int n = 0;
    MEMORY_BASIC_INFORMATION mbi; unsigned addr = 0x00010000;
    const unsigned MASK_RD = PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY;
    while (addr < 0x7FFE0000 && n < cap) {
        if (!VirtualQuery((void*)addr, &mbi, sizeof(mbi))) break;
        unsigned base = (unsigned)mbi.BaseAddress, sz = (unsigned)mbi.RegionSize;
        bool rd = (mbi.State == MEM_COMMIT) && (mbi.Protect & MASK_RD) && !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS));
        if (rd && sz >= 4) {
            __try {
                const unsigned char* e = (const unsigned char*)base + sz - 4;
                for (const unsigned char* p = (const unsigned char*)base; p <= e && n < cap; p += 4)
                    if (*(const unsigned*)p == want) out[n++] = (unsigned)p;
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
        addr = base + sz;
    }
    return n;
}

// SEH-guarded 4-byte read (separate fn so the SEH has no C++ object unwinding, C2712).
static unsigned guarded_read32(unsigned addr) {
    __try { return *(volatile unsigned*)addr; } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

// SEH-guarded byte copy: grab up to n bytes at addr (separate fn, no C++ unwinding).
static int guarded_copy(unsigned addr, unsigned char* out, int n) {
    __try { for (int i = 0; i < n; ++i) out[i] = *(volatile unsigned char*)(addr + i); return n; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

// RE: scan committed memory for a u32 (and u16) in [lo,hi], logging each hit + context. Used to
// find the ability-recast ARRAY : 0x63449C only holds ONE action's recast, so we scan for any
// other word holding ~the same live frame-count -> that's the real array slot. `excl` = the address
// to skip (0x63449C itself). Caller passes a tight window so drift between frames is tolerated.
static int scan_word_range(unsigned lo, unsigned hi, unsigned excl) {
    MEMORY_BASIC_INFORMATION mbi; unsigned addr = 0x00010000; int hits = 0;
    const unsigned MASK_RD = PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY;
    while (addr < 0x7FFE0000 && hits < 40) {
        if (!VirtualQuery((void*)addr, &mbi, sizeof(mbi))) break;
        unsigned base = (unsigned)mbi.BaseAddress, sz = (unsigned)mbi.RegionSize;
        bool rd = (mbi.State == MEM_COMMIT) && (mbi.Protect & MASK_RD) && !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS));
        if (rd && sz >= 4) {
            __try {
                const unsigned char* e = (const unsigned char*)base + sz - 4;
                for (const unsigned char* p = (const unsigned char*)base; p <= e && hits < 40; p += 2) {  // 2-byte step: catch u16 + u32
                    unsigned a = (unsigned)p; if (a >= excl - 4 && a <= excl + 4) continue;
                    unsigned v32 = *(const unsigned*)p; unsigned v16 = *(const unsigned short*)p;
                    bool h32 = (v32 >= lo && v32 <= hi), h16 = (v16 >= lo && v16 <= hi);
                    if (h32 || h16) {
                        char mod[64]; unsigned mb = windower::debug::module_base_of(a, mod, sizeof(mod));
                        windower::debug::log("hit @%08X %s%s v32=%u v16=%u  (%s+0x%X)", a, h32?"[u32]":"", h16?"[u16]":"",
                                             v32, v16, mb?mod:"heap", mb?(a-mb):a);
                        windower::debug::hexdump("  ctx", a - 16, 48);
                        ++hits;
                    }
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
        addr = base + sz;
    }
    windower::debug::log("rcfind: %d hit(s) in [%u,%u]", hits, lo, hi);
    return hits;
}

// RE: dump an object + chase every dword as a possible char* and log any ASCII run it
// points at. The action-menu reverse needs a STATIC menu identifier (the menu's internal
// name string, e.g. "menu    mag") so we can tell Magic from Ability the instant a menu
// opens -- without the cursor-tap "learn" trick. This surfaces those strings.
static void dump_obj_ascii(const char* label, unsigned base, int len) {
    if (!windower::valid_ptr(base)) { windower::debug::log("%s: invalid base %08X", label, base); return; }
    windower::debug::hexdump(label, base, len);
    for (int off = 0; off < len; off += 4) {
        unsigned v = guarded_read32(base + off);
        if (!windower::valid_ptr(v)) continue;
        unsigned char s[33]; int got = guarded_copy(v, s, 32);
        if (got < 4) continue;
        int pr = 0; while (pr < 32 && s[pr] >= 0x20 && s[pr] < 0x7F) ++pr;
        if (pr >= 4) { s[pr] = 0; windower::debug::log("  %s+0x%03X -> %08X '%s'", label, off, v, s); }
    }
    // also test the object itself for an INLINE ascii name (some FFXI menu structs embed it)
    unsigned char s[33]; if (guarded_copy(base, s, 32) >= 8) {
        int pr = 0; while (pr < 32 && s[pr] >= 0x20 && s[pr] < 0x7F) ++pr;
        if (pr >= 4) { s[pr] = 0; windower::debug::log("  %s inline -> '%s'", label, s); }
    }
}

// candidate target-field addresses from the last //aio tgt diff (for //aio anchor).
static unsigned g_cand[16]; static int g_ncand = 0;

// RE: find pointers that land inside [cand-window, cand] for any candidate struct base.
// Returns (location, value) pairs so the caller can annotate the location's module.
struct PtrHit { unsigned loc, val, cand; };
static int collect_ptr_hits(const unsigned* cands, int nc, unsigned window, PtrHit* out, int cap) {
    int n = 0;
    MEMORY_BASIC_INFORMATION mbi; unsigned addr = 0x00010000;
    const unsigned MASK_RD = PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY;
    while (addr < 0x7FFE0000 && n < cap) {
        if (!VirtualQuery((void*)addr, &mbi, sizeof(mbi))) break;
        unsigned base = (unsigned)mbi.BaseAddress, sz = (unsigned)mbi.RegionSize;
        bool rd = (mbi.State == MEM_COMMIT) && (mbi.Protect & MASK_RD) && !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS));
        if (rd && sz >= 4) {
            __try {
                const unsigned char* e = (const unsigned char*)base + sz - 4;
                for (const unsigned char* p = (const unsigned char*)base; p <= e && n < cap; p += 4) {
                    unsigned v = *(const unsigned*)p;
                    for (int c = 0; c < nc; ++c) if (v <= cands[c] && cands[c] - v <= window) {
                        out[n].loc = (unsigned)p; out[n].val = v; out[n].cand = cands[c]; ++n; break;
                    }
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
        addr = base + sz;
    }
    return n;
}

static const char* LAYOUT_PATH    = "D:\\Windower Tetsouo\\plugins\\_aiohud_re\\design\\exports\\layout.json";
static const char* LAYOUT_RT_PATH = "D:\\Windower Tetsouo\\plugins\\_aiohud_re\\design\\exports\\layout.roundtrip.json";

using namespace windower;

static PluginManager g_host;
static aio::Hud      g_hud;

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

// ===== IPlugin hooks (declared in windower_plugin.h) =====

void aio_plugin_init(PluginManager host)
{
    g_host = host;
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
    g_hud.apply_layout(LAYOUT_PATH);   // place widgets from the descriptor (keeps defaults if absent)
    host.console().print(">>> AioHUD -- //aio hp|mp|tp <n>  |  lay 1-4  |  layout (reload) <<<");
}

void aio_plugin_render() {}   // slot 5 : unused (we draw on slot 6, like FFXIDB)

// RE: sub-target (<st>) offset finder. The <st> cursor is transient (closes the moment
// you type a command), so we scan the target-struct window EVERY frame while enabled and
// log any dword that matches a party member's server id. Sub-target a member -> its offset
// lights up in the log. The known MAIN target id is at FFXiMain+0x487F60 ; the OTHER hit
// is the sub-target field. Toggle with //aio sub.
static int g_sub_probe = 0;
static int g_sub_tick  = 0;

// RE: party-DISTRIBUTION cursor finder (quartermaster/lottery). That cursor is NOT in target_t
// (probed: stays 0x04000000), so it's a MENU cursor. While enabled, watch the live menu struct
// (*(FFXiMain+0x5EED6C)) and log, on each change, the menu name + any offset whose dword equals a
// party-member id (= the highlighted member). Solo is enough: open the picker -> it lands on YOU,
// so your id appears where it wasn't before. Toggle with //aio pcur.
static int g_pcur_probe = 0;
static int g_pcur_tick  = 0;

void aio_plugin_render6()
{
    g_hud.render(g_host.service_raw(2));   // host vtbl[2] = D3D8 device

    if (g_sub_probe) {                                    // FIND the sub-active flag : follow target_t = *(FFXiMain+0x57876C), dump on change
        static u32 prevkey = 0;
        u32 ffm = (u32)GetModuleHandleA("FFXiMain.dll");
        if (ffm && (++g_sub_tick % 6 == 0)) {
            u32 tp = 0; safe_read(ffm + 0x57876C, &tp);   // static anchor -> target_t base
            if (valid_ptr(tp)) {
                u32 t0 = 0, t1 = 0, f50 = 0, f54 = 0, f58 = 0, f78 = 0;
                safe_read(tp + 0x04, &t0); safe_read(tp + 0x2C, &t1);
                safe_read(tp + 0x50, &f50); safe_read(tp + 0x54, &f54);
                safe_read(tp + 0x58, &f58); safe_read(tp + 0x78, &f78);
                u32 key = (t0 ^ (t1*131)) + (f50<<1) + (f54<<2) + (f58<<3) + (f78<<4);
                if (key != prevkey) {
                    prevkey = key;
                    const char* n0="?"; const char* n1="?";
                    for (int i=0;i<aio::party().count;++i){ if(aio::party().m[i].id==t0)n0=aio::party().m[i].name; if(aio::party().m[i].id==t1)n1=aio::party().m[i].name; }
                    debug::log("ST: T0=%08X'%s' T1=%08X'%s' | +50=%08X +54=%08X +58=%08X +78=%08X",
                               t0, n0, t1, n1, f50, f54, f58, f78);
                }
            }
        }
    }

    if (g_pcur_probe) {                                   // FIND the 'stpt' index in the targeting struct *(g+0x2C)
        static u32 prevkey = 0;                           // (get_mob_by_target -> FUN_1008b7d0 indexes *(g+0x24) by *(g+0x2C)+off)
        u32 lc = (u32)GetModuleHandleA("LuaCore.dll");
        if (lc && (++g_pcur_tick % 6 == 0)) {
            u32 ffm2 = (u32)GetModuleHandleA("FFXiMain.dll");
            u32 mptr = 0; if (ffm2) safe_read(ffm2 + 0x5EED6C, &mptr);          // currently-focused menu object
            u32 def = 0; if (valid_ptr(mptr)) safe_read(mptr + 0x04, &def);
            char nm[9] = {0}; if (valid_ptr(def)) for (int i = 0; i < 8; ++i) { u32 c = 0; safe_read(def + 0x4E + i, &c); nm[i] = (char)(c & 0xFF); }
            bool isParty = (nm[0] == 'p' && nm[1] == 'a' && nm[2] == 'r' && nm[3] == 't');   // "partywin" only
            if (isParty && valid_ptr(mptr)) {
                // resolve entity array for the index->id route ; scan the menu object both ways.
                u32 g = 0, ent = 0; safe_read(lc + 0x1C8400, &g); if (valid_ptr(g)) safe_read(g + 0x24, &ent);
                aio::PlayerInfo me; bool okMe = aio::read_player(me);
                int idOff = -1; u32 idVal = 0; int ixOff = -1; u32 ixVal = 0;
                for (int o = 0; o < 0x80; o += 4) { u32 v = 0; safe_read(mptr + o, &v);            // a member SERVER-ID stored directly?
                    if (okMe && v == me.id) { idOff = o; idVal = v; } for (int k = 0; k < aio::party().count; ++k) if (v == aio::party().m[k].id) { idOff = o; idVal = v; } }
                if (valid_ptr(ent)) for (int o = 0; o < 0x80; o += 2) { u32 raw = 0; safe_read(mptr + o, &raw); u32 ix = raw & 0xFFFF;   // or a USHORT entity index?
                    if (ix == 0 || ix >= 0x900) continue; u32 mob = 0; safe_read(ent + ix * 4, &mob); if (!valid_ptr(mob)) continue; u32 sid = 0; safe_read(mob, &sid);
                    if ((okMe && sid == me.id)) { ixOff = o; ixVal = sid; } for (int k = 0; k < aio::party().count; ++k) if (sid == aio::party().m[k].id) { ixOff = o; ixVal = sid; } }
                (void)idOff; (void)idVal; (void)ixOff; (void)ixVal;
                // CONFIRMED earlier: 'partywin'+0x4C = 1-based cursor index, +0x08 = selected-row ptr.
                // Resolve the row (and its neighbour at -0xD0/+0xD0) -> find the member id/entity it carries.
                u32 cidx = 0; safe_read(mptr + 0x4C, &cidx);
                u32 row = 0; safe_read(mptr + 0x08, &row);
                u32 rowHit = 0;                                            // a party id found anywhere in the selected row [0,0x60)
                if (valid_ptr(row)) for (int o = 0; o < 0x60; o += 4) { u32 v = 0; safe_read(row + o, &v); if (okMe && v == me.id) rowHit = v; for (int k = 0; k < aio::party().count; ++k) if (v == aio::party().m[k].id) rowHit = v; }
                u32 key = cidx ^ (row * 131u) ^ rowHit;
                if (key != prevkey) {
                    prevkey = key;
                    debug::log("PCUR 'partywin' idx@+0x4C=%u row@+0x08=%08X rowHasPartyId=%08X (me=%08X)", cidx, row, rowHit, okMe ? me.id : 0);
                    if (valid_ptr(row)) debug::hexdump("  row", row, 0x60);
                }
            }
        }
    }
}

// RE instrumentation. Reversed: b = decoded inbound packet (u16@+0 = id|size, u16@+2 = sync).
// //aio pkt [hexid] logs the next packets ; default filter = party packets 0xDD / 0xDF.
static int g_pkt_log = 0;
static int g_pkt_filter = -1;   // -1 = {0xDD, 0xDF} ; else a specific id

void aio_plugin_packet_in(u32 a, u32 b, u32 c, u32 d)
{
    (void)c; (void)d;
    if (!valid_ptr(b)) return;
    u32 hdr = 0; safe_read(b, &hdr);
    int id = hdr & 0x1FF;

    // feed the live party model (b = decoded packet ; safe, fully-populated buffer).
    if (id == 0xDD) {
        aio::party().on_dd((const unsigned char*)b);
        u32 mhp = 0; safe_read(b + 0x08, &mhp);          // auto-capture an OUT-OF-ZONE member's
        static int ozdump = 0;                            // 0x0DD (hp==0) -> reveals the zone field
        if (mhp == 0 && ozdump < 4) { ozdump++; debug::log("OFFZONE 0xDD"); debug::hexdump("  oz", b, 64); }
    }
    else if (id == 0xDF) aio::party().on_df((const unsigned char*)b);
    else if (id == 0x028) aio::party().on_action((const unsigned char*)b);   // cast bar
    else if (id == 0x076) aio::party().on_076((const unsigned char*)b);      // party-member buffs

    // optional RE logging (armed by //aio pkt). Dump the SOURCE `a` too -> lead to the
    // in-memory party structure (for instant read on load).
    if (g_pkt_log > 0) {
        bool want = (g_pkt_filter >= 0) ? (id == g_pkt_filter) : (id == 0xDD || id == 0xDF);
        if (want) {
            g_pkt_log--;
            debug::log("PKT id=0x%03X size=%d  a=%08X", id, ((hdr >> 9) & 0x7F) * 4, a);
            debug::hexdump("  b", b, 64);
            if (valid_ptr(a)) debug::hexdump("  a", a, 320);
        }
    }
}

// slot 13 : MOUSE (eventtype, x, y, delta, blocked). eventtype 0=move 1=Ldown 2=Lup (Windower).
// Returns the "blocked" flag : 1 swallows the event so the GAME doesn't react to it. We block ALL
// mouse while the config overlay is up (cursor is read separately via Win32 GetCursorPos).
static int g_mouse_probe = 0;
unsigned int aio_plugin_mouse(u32 eventtype, u32 x, u32 y, u32 delta, u32 blocked)
{
    if (g_mouse_probe) {
        static int cnt = 0;
        if (cnt < 500) { ++cnt; debug::log("MOUSE et=%u x=%d y=%d d=%d blocked=%u", eventtype, (int)x, (int)y, (int)delta, blocked); }
    }
    static bool held = false;                  // a press began while we were blocking
    const bool active = g_hud.config().is_open() || aio::ui_config().editLayout;
    if (active || held) {
        // Wheel : in edit mode it resizes the hovered box ; consume it. (delta != 0 => a scroll event.)
        if (aio::ui_config().editLayout && (int)delta != 0) { aio::ui_config().wheel += ((int)delta > 0) ? 1 : -1; return 1u; }
        // Block only CLICKS (with a block-until-release latch). Plain MOVES pass through -> avoids the
        // high-frequency move-event flood that tanked the framerate, and a bare move is a no-op in FFXI.
        if (eventtype == 1) { held = true;  return 1u; }   // Ldown : swallow + latch
        if (eventtype == 2) { held = false; return 1u; }   // Lup   : swallow + release latch
        if (held)             return 1u;                   // mid-press : keep swallowing (move + the up)
        return blocked;                                    // moves / hover : let them through
    }
    return blocked;
}

void aio_plugin_unload()
{
    g_hud.dispose();
}

void aio_plugin_command(const char* cmd)
{
    if (!cmd) return;
    // lower-case a copy so keywords are case-insensitive (//AIO HP 50 == //aio hp 50).
    char buf[256]; int i = 0;
    for (; cmd[i] && i < 255; i++) { char c = cmd[i]; buf[i] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }
    buf[i] = 0;

    // //aio [party|alliance1|alliance2] demo [off] -> fake roster for previewing the layout.
    //   party demo      -> a full 6-man party
    //   alliance1 demo  -> party + 1 alliance party (12)
    //   alliance2 demo  -> party + 2 alliance parties (18)
    //   demo off        -> back to live data
    if (strstr(buf, "demo")) {
        int level = strstr(buf, "off")       ? 0 :
                    strstr(buf, "alliance2")  ? 3 :
                    strstr(buf, "alliance1")  ? 2 : 1;   // bare "party demo" / "demo" -> party only
        aio::set_party_demo_level(level);
        const char* what = level == 0 ? "off" : level == 1 ? "party" :
                           level == 2 ? "party + alliance1" : "party + 2 alliances";
        char msg[96]; sprintf(msg, ">>> demo: %s <<<", what);
        g_host.console().print(msg);
        return;
    }

    // //aio dist -> probe each party member's ENTITY position vs the player, to calibrate the
    // entity position offsets + verify the distance for the "out of cast range" grey-out.
    if (strstr(buf, "dist")) {
        u32 lc = (u32)GetModuleHandleA("LuaCore.dll");
        u32 g = 0; if (lc) safe_read(lc + 0x1C8400, &g);
        u32 ent = 0, pl = 0;
        if (valid_ptr(g)) { safe_read(g + 0x24, &ent); safe_read(g + 0x3C, &pl); }
        debug::log("DIST g=%08X ent=%08X pl=%08X  (my id=%08X)", g, ent, pl, aio::party().count ? aio::party().m[0].id : 0);
        // dump the g struct pointers -> spot the real entity array + player entity
        if (valid_ptr(g)) for (int o = 0; o < 0x80; o += 4) { u32 v = 0; safe_read(g + o, &v); debug::log("  g+0x%02X = %08X%s", o, v, valid_ptr(v) ? " *" : ""); }
        // hexdump the candidate player entity -> find the name 'Tetsouo', the server id, and the position floats
        if (valid_ptr(pl)) debug::hexdump("pl", pl, 0x100);
        // CHAIN: party-struct (id @+0x00, entity index @+0x04) -> ent[index] = position object
        // (X @+0x04, Y @+0x08, Z @+0x0C). Compute distance player<->member on both planes (XZ/XY).
        u32 plIdx = 0; if (valid_ptr(pl)) safe_read(pl + 0x04, &plIdx);
        float px = 0, py = 0, pz = 0;
        if (valid_ptr(ent) && plIdx < 0x900) { u32 p = 0; safe_read(ent + plIdx * 4, &p);
            if (valid_ptr(p)) { u32 a = 0, b = 0, c = 0; safe_read(p + 4, &a); safe_read(p + 8, &b); safe_read(p + 0xC, &c);
                px = *(float*)&a; py = *(float*)&b; pz = *(float*)&c; } }
        debug::log("player idx=%u (0x%X) pos x=%d y=%d z=%d (cm)", plIdx, plIdx, (int)(px * 100), (int)(py * 100), (int)(pz * 100));
        // The party-MEMORY array (g+0x248 -> -4, stride 0x7C) holds all members. Dump each member's
        // 0x7C struct -> find the ENTITY INDEX field (the player's is 0x43F = 1087, shown above).
        u32 pbase4 = 0, pbase = 0; if (valid_ptr(g)) { safe_read(g + 0x248, &pbase4); pbase = pbase4 - 4; }
        debug::log("party-mem base=%08X (look for 043F = player entity index)", pbase);
        for (int i = 0; i < 6; ++i) {
            u32 mb = pbase + (u32)i * 0x7C; u32 sid = 0; safe_read(mb + 0x1C, &sid); if (!sid) continue;
            char nm[10] = {0}; for (int j = 0; j < 9; ++j) { u32 c = 0; safe_read(mb + 0x0A + j, &c); nm[j] = (char)(c & 0xFF); }
            debug::log(" member[%d] sid=%08X name='%s'", i, sid, nm);
            debug::hexdump("  mb", mb, 0x7C);
        }
        // distance via party-mem +0x20 (entity index) -> ent[idx] position object -> X/Z floats
        for (int i = 0; i < 6; ++i) {
            u32 mb = pbase + (u32)i * 0x7C; u32 sid = 0; safe_read(mb + 0x1C, &sid); if (!sid) continue;
            u32 idx = 0; safe_read(mb + 0x20, &idx); idx &= 0xFFFF;
            char nm[10] = {0}; for (int j = 0; j < 9; ++j) { u32 c = 0; safe_read(mb + 0x0A + j, &c); nm[j] = (char)(c & 0xFF); }
            float mx = 0, my = 0, mz = 0;
            if (valid_ptr(ent) && idx < 0x900) { u32 p = 0; safe_read(ent + idx * 4, &p);
                if (valid_ptr(p)) { u32 a = 0, b = 0, c = 0; safe_read(p + 4, &a); safe_read(p + 8, &b); safe_read(p + 0xC, &c);
                    mx = *(float*)&a; my = *(float*)&b; mz = *(float*)&c; } }
            float dx = mx - px, dy = my - py, dz = mz - pz;
            debug::log(" DIST %-12s idx=0x%X x=%d y=%d z=%d  dXZ=%d dXY=%d cm", nm, idx,
                       (int)(mx * 100), (int)(my * 100), (int)(mz * 100),
                       (int)(sqrtf(dx * dx + dz * dz) * 100), (int)(sqrtf(dx * dx + dy * dy) * 100));
        }
        g_host.console().print(">>> dist struct dump -> aiohud_debug.log <<<");
        return;
    }

    // //aio config -> toggle the full-screen configuration overlay. "config N" = select tab N (1..3).
    if (strstr(buf, "config")) {
        const char* cp = strstr(buf, "config") + 6; while (*cp == ' ') ++cp;
        if (*cp >= '1' && *cp <= '3') { g_hud.config().set_tab(atoi(cp) - 1); g_hud.config().set_open(true); }
        else                          g_hud.config().toggle();
        g_host.console().print(g_hud.config().is_open() ? ">>> config OPEN <<<" : ">>> config closed <<<");
        return;
    }
    if (strstr(buf, "edit")) {                            // toggle layout edit mode (drag/resize boxes on the live game)
        bool e = !aio::ui_config().editLayout;
        aio::ui_config().editLayout = e;
        g_hud.config().set_open(e);                       // keep config "open" so the mouse is captured + toolbar shows
        if (!e) aio::save_ui_config();                    // persist positions/sizes when leaving edit mode
        g_host.console().print(e ? ">>> edit layout ON (drag boxes ; //aio edit to exit) <<<" : ">>> edit layout OFF <<<");
        return;
    }
    if (strstr(buf, "mouse")) {                           // toggle the mouse-slot probe (decode a/b/c)
        g_mouse_probe = !g_mouse_probe;
        if (g_mouse_probe) debug::log("MOUSE-PROBE ON. Move to TOP-LEFT, then BOTTOM-RIGHT, then LEFT-CLICK & RIGHT-CLICK.");
        g_host.console().print(g_mouse_probe ? ">>> mouse-probe ON -> aiohud_debug.log <<<" : ">>> mouse-probe OFF <<<");
        return;
    }

    // //aio layout -> load design/exports/layout.json, log the widgets, and save a round-trip copy.
    // (checked BEFORE "lay" because "layout" contains "lay".)
    if (strstr(buf, "layout")) {
        aio::Layout lay;
        if (aio::load_layout(LAYOUT_PATH, lay)) {
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
            if (aio::save_layout(LAYOUT_RT_PATH, lay)) debug::log("  round-trip -> layout.roundtrip.json");
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

    // RE probes for wiring real game data.
    if (strstr(buf, "ffxi")) {                            // dump the game-data interface vtable (vtbl[7])
        u32 ff = g_host.service_raw(7);
        debug::dump_vtable("ffxi[7]", ff, 80);
        g_host.console().print(">>> ffxi[7] vtable -> aiohud_debug.log <<<");
        return;
    }
    if (strstr(buf, "party")) {                           // dump live party: id + flags + leaders (find the QM bit)
        aio::party().load_from_memory();                  // refresh roster/vitals/flags from memory
        aio::PlayerInfo me; bool ok = aio::read_player(me);
        aio::PartyLeaders ld; bool okl = aio::read_party_leaders(ld);
        debug::log("MEM self='%s' id=%08X job=%d/%d (ok=%d)", ok ? me.name : "?", me.id, me.mjob, me.sjob, (int)ok);
        debug::log("LEADERS alliance=%08X p1=%08X p2=%08X p3=%08X (ok=%d)", ld.alliance, ld.p1, ld.p2, ld.p3, (int)okl);
        aio::PartyState& ps = aio::party();
        for (int i = 0; i < ps.count; ++i) {
            aio::PMember& pm = ps.m[i];
            debug::log("  m%d '%s' id=%08X flags=%08X hp=%d/%d mp=%d/%d hpp=%d zone=%d",
                       i, pm.name, pm.id, pm.flags, pm.hp, pm.maxHp, pm.mp, pm.maxMp, pm.hpp, pm.zone);
        }
        g_host.console().print(">>> party -> aiohud_debug.log <<<");
        return;
    }
    if (strstr(buf, "tgt") && !strstr(buf, "tgt2")) {     // DIFFERENTIAL scan: run 1 = NO target, run 2 = target yourself.
        // The address that flips to your server id between the two runs IS the target field.
        static u32 s_hits[1024]; static int s_nprev = -1;
        aio::PlayerInfo me; bool ok = aio::read_player(me);
        if (!ok) { g_host.console().print(">>> tgt: player not ready <<<"); return; }
        u32 want = me.id;                                 // search value = your own server id (target yourself)
        u32 lc = (u32)GetModuleHandleA("LuaCore.dll");
        u32 g = 0; if (lc) safe_read(lc + 0x1C8400, &g);
        u32 pl = 0; { u32 gg = 0; if (lc) safe_read(lc + 0x1C8400, &gg); if (valid_ptr(gg)) safe_read(gg + 0x3C, &pl); }

        // collect every readable address currently holding `want` (cap 1024).
        static u32 cur[1024];
        int ncur = collect_u32_hits(want, cur, 1024);
        debug::log("TGT diff: id=%08X g=%08X player=%08X  hits=%d  (run1=NO target, run2=target SELF)", want, g, pl, ncur);
        if (s_nprev < 0) {
            debug::log("  baseline saved. NOW target yourself and run //aio tgt again.");
        } else {
            int appeared = 0; g_ncand = 0;
            for (int i = 0; i < ncur; ++i) {
                bool was = false;
                for (int j = 0; j < s_nprev; ++j) if (s_hits[j] == cur[i]) { was = true; break; }
                if (!was) {
                    u32 a = cur[i];
                    char rel[80] = "";
                    if (valid_ptr(g)  && a > g  && a - g  < 0x40000) sprintf(rel, " = g+0x%X", a - g);
                    else if (valid_ptr(pl) && a > pl && a - pl < 0x40000) sprintf(rel, " = player+0x%X", a - pl);
                    else if (lc && a > lc && a - lc < 0x400000) sprintf(rel, " = LuaCore+0x%X", a - lc);
                    debug::log("  APPEARED @%08X%s  <-- candidate target field", a, rel);
                    if (g_ncand < 16) g_cand[g_ncand++] = a;   // remember for //aio anchor
                    if (++appeared >= 30) break;
                }
            }
            debug::log("  %d address(es) appeared (saved %d). Now run //aio anchor to find a STATIC pointer to them.", appeared, g_ncand);
            if (!appeared) debug::log("  none appeared -> the target is stored as an INDEX, not a server id. Tell me; I'll switch to the entity route.");
        }
        // save current as the new baseline
        for (int i = 0; i < ncur && i < 1024; ++i) s_hits[i] = cur[i];
        s_nprev = ncur;
        g_host.console().print(">>> tgt -> aiohud_debug.log <<<");
        return;
    }
    if (strstr(buf, "anchor")) {                          // find a STATIC pointer to the target struct (run //aio tgt first)
        if (g_ncand <= 0) { g_host.console().print(">>> anchor: run //aio tgt (x2) first <<<"); return; }
        debug::log("ANCHOR: searching for pointers into %d candidate struct(s) (window 0x400)", g_ncand);
        for (int i = 0; i < g_ncand; ++i) debug::log("  cand[%d] = %08X", i, g_cand[i]);
        static PtrHit hits[600];
        int nh = collect_ptr_hits(g_cand, g_ncand, 0x400, hits, 600);
        int statics = 0;
        for (int i = 0; i < nh; ++i) {
            char mod[64]; u32 mbase = debug::module_base_of(hits[i].loc, mod, sizeof(mod));
            if (!mbase) continue;                         // only STATIC pointers (inside a loaded module) = stable anchor
            debug::log("  STATIC @%s+0x%X  ->  %08X   (target field = *(%s+0x%X) + 0x%X)",
                       mod, hits[i].loc - mbase, hits[i].val, mod, hits[i].loc - mbase, hits[i].cand - hits[i].val);
            if (++statics >= 30) break;
        }
        debug::log("ANCHOR: %d pointer hit(s), %d static. A FFXiMain.dll/polcore static = the stable chain.", nh, statics);
        if (!statics) debug::log("  no static pointer -> chain is multi-hop (heap->heap). Tell me, I'll scan the next hop.");
        g_host.console().print(">>> anchor -> aiohud_debug.log <<<");
        return;
    }
    if (strstr(buf, "slots")) {                           // dump the 6 RAW party slots + member counts (find how dismissed trusts read)
        u32 lc = (u32)GetModuleHandleA("LuaCore.dll");
        u32 g = 0, pp = 0, ai = 0;
        if (lc) safe_read(lc + 0x1C8400, &g);
        if (valid_ptr(g)) safe_read(g + 0x248, &pp);
        if (valid_ptr(pp)) safe_read(pp, &ai);            // allianceinfo_t
        aio::PlayerInfo me; bool ok = aio::read_player(me);
        u32 base = pp - 4, id0 = 0;
        if (!(safe_read(base + 0x1C, &id0) && ok && id0 == me.id)) { if (safe_read(pp + 0x1C, &id0) && ok && id0 == me.id) base = pp; }
        debug::log("SLOTS: g=%08X pp=%08X allianceinfo=%08X base=%08X self=%08X", g, pp, ai, base, ok ? me.id : 0);
        if (valid_ptr(ai)) {
            u32 v10 = 0, v14 = 0; safe_read(ai + 0x10, &v10); safe_read(ai + 0x14, &v14);
            debug::log("  allianceinfo +0x10..0x17 = %08X %08X  (counts: p1=%d p2=%d p3=%d ; vis p1=%d p2=%d p3=%d)",
                       v10, v14, v14 & 0xFF, (v14 >> 8) & 0xFF, (v14 >> 16) & 0xFF, v10 & 0xFF, (v10 >> 8) & 0xFF, (v10 >> 16) & 0xFF);
        }
        for (int i = 0; i < 6; ++i) {
            u32 mb = base + i * 0x7C, id = 0, hp = 0, pk = 0, fl = 0;
            safe_read(mb + 0x1C, &id); safe_read(mb + 0x28, &hp); safe_read(mb + 0x34, &pk); safe_read(mb + 0x3C, &fl);
            char nm[20]; int k = 0; for (; k < 18; ++k) { u32 ch = 0; if (!safe_read(mb + 0x0A + k, &ch)) break; char c = (char)(ch & 0xFF); if (!c) break; nm[k] = c; } nm[k] = 0;
            debug::log("  slot%d id=%08X hp=%d hpp=%d flags=%08X name='%s'", i, id, hp, pk & 0xFF, fl, nm);
        }
        g_host.console().print(">>> slots -> aiohud_debug.log <<<");
        return;
    }
    if (strstr(buf, "sub")) {                             // toggle the sub-target offset probe (logs party-id matches every frame)
        g_sub_probe = !g_sub_probe; g_sub_tick = 0;
        if (g_sub_probe) {
            debug::log("ST-TIMELINE: ON (logs T0/T1 + flag candidates on each state change).");
            debug::log("  DO: /ma \"Cure\" <stpc> on a member and CONFIRM (actually cast it). Watch +78 (and +50/54/58).");
            debug::log("  -> we need the flag that goes 1 on cursor-open and BACK to 0 after the cast confirms (not just on cancel).");
            g_host.console().print(">>> st-timeline ON : cure via <stpc> & confirm. Check aiohud_debug.log <<<");
        } else {
            g_host.console().print(">>> sub-probe OFF <<<");
        }
        return;
    }
    if (strstr(buf, "pcur")) {                            // toggle the party-distribution cursor probe (menu-struct watcher)
        g_pcur_probe = !g_pcur_probe; g_pcur_tick = 0;
        if (g_pcur_probe) {
            debug::log("PCUR: ON. DO: Menu -> Party -> Distribution -> Quartermaster (the picker lands on YOU).");
            debug::log("  -> watch for a 'party-id in struct: +0xNN = <your id>' line that appears when the cursor opens.");
            g_host.console().print(">>> pcur ON : open Party>Distribution>Quartermaster, then check aiohud_debug.log <<<");
        } else {
            g_host.console().print(">>> pcur OFF <<<");
        }
        return;
    }
    if (strstr(buf, "tgt2")) {                            // locate the real target_t (Ashita layout) + a STATIC pointer to it
        u32 ffm = (u32)GetModuleHandleA("FFXiMain.dll");
        u32 id = 0; safe_read(ffm + 0x487F60, &id);
        if (!id || id == 0x04000000) { g_host.console().print(">>> tgt2: Tab a PARTY MEMBER first <<<"); return; }
        static unsigned hits[2048]; int nh = collect_u32_hits(id, hits, 2048);
        debug::log("TGT2: %d location(s) hold id %08X. Candidates (Index@-4 small AND T1.id@+0x28 = party id):", nh, id);
        u32 tbase = 0; int ncand = 0;
        for (int k = 0; k < nh; ++k) {
            u32 idx = 0, eptr = 0, aptr = 0, t1 = 0;
            safe_read(hits[k] - 0x04, &idx); safe_read(hits[k] + 0x04, &eptr);
            safe_read(hits[k] + 0x08, &aptr); safe_read(hits[k] + 0x28, &t1);
            bool t1ok = false; for (int i = 0; i < aio::party().count; ++i) if (aio::party().m[i].id == t1) t1ok = true;
            if (idx < 0x10000 && t1ok) {
                char mod[64]; u32 mb = debug::module_base_of(hits[k], mod, sizeof(mod));
                debug::log("  cand base=%08X (%s%s0x%X) idx=%X eptr=%08X aptr=%08X T1.id=%08X",
                           hits[k]-0x04, mb?mod:"heap", mb?"+":"", mb?hits[k]-mb:0, idx, eptr, aptr, t1);
                if (!tbase) tbase = hits[k] - 0x04;
                if (++ncand >= 12) break;
            }
        }
        if (!tbase) { debug::log("TGT2: target_t not found among %d hits", nh); g_host.console().print(">>> tgt2: not found -> log <<<"); return; }
        debug::hexdump("target_t", tbase, 0x60);
        unsigned ph[32]; int np = collect_u32_hits(tbase, ph, 32);   // STATIC pointers that hold the heap base
        debug::log("TGT2: %d pointer(s) to base:", np);
        for (int k = 0; k < np; ++k) {
            char mod[64]; u32 mb = debug::module_base_of(ph[k], mod, sizeof(mod));
            if (mb) debug::log("  STATIC ptr @ %s+0x%X -> target_t  <== chain anchor", mod, ph[k] - mb);
        }
        g_host.console().print(">>> tgt2 -> aiohud_debug.log <<<");
        return;
    }
    if (strstr(buf, "tcheck")) {                          // confirm the target field is a fixed RVA inside FFXiMain.dll
        u32 ffm = (u32)GetModuleHandleA("FFXiMain.dll");
        u32 cand = g_ncand > 1 ? g_cand[1] : 0x06E07F60;  // the FFXiMain-resident candidate from //aio anchor
        char mod[64]; u32 mbase = debug::module_base_of(cand, mod, sizeof(mod));
        debug::log("TCHECK: FFXiMain=%08X  cand=%08X  module_of(cand)=%s base=%08X", ffm, cand, mod[0]?mod:"<heap>", mbase);
        if (mbase) debug::log("  -> target id is STATIC at %s + 0x%X  (this RVA is what I hard-code)", mod, cand - mbase);
        else       debug::log("  -> cand is on the heap, not in a module (chain is via a pointer instead)");
        // dump the struct around the field so we can also spot <st> / other fields
        if (valid_ptr(cand)) debug::hexdump("target struct", cand - 0x20, 0x60);
        u32 v = 0; safe_read(cand, &v);
        debug::log("  current value @cand = %08X  (== your id while you target yourself?)", v);
        g_host.console().print(">>> tcheck -> aiohud_debug.log <<<");
        return;
    }
    if (strstr(buf, "vit")) {                             // dump the player vitals region (verify offsets vs real HP/MP)
        u32 lc = (u32)GetModuleHandleA("LuaCore.dll");
        u32 g = 0, pl = 0;
        safe_read(lc + 0x1C8400, &g);
        if (valid_ptr(g)) safe_read(g + 0x3C, &pl);
        debug::log("LuaCore=%08X  g=%08X  player=%08X", lc, g, pl);
        if (valid_ptr(pl)) debug::hexdump("player+0x50", pl + 0x50, 0x40);
        g_host.console().print(">>> vit -> aiohud_debug.log <<<");
        return;
    }
    if (strstr(buf, "chain")) {                           // resolve the party pointer chain (reversed from LuaCore get_party)
        u32 lc = (u32)GetModuleHandleA("LuaCore.dll");
        u32 a = 0, b = 0, pty = 0;
        safe_read(lc + 0x1C8400, &a);
        if (valid_ptr(a)) safe_read(a + 0x248, &b);
        if (valid_ptr(b)) safe_read(b, &pty);
        debug::log("LuaCore=%08X  a=%08X  b=%08X  party=%08X", lc, a, b, pty);
        if (valid_ptr(pty)) {
            u32 cnt = 0; safe_read(pty + 0x10, &cnt);
            u32 e0 = 0, e1 = 0; safe_read(pty + 0x18, &e0); safe_read(pty + 0x1C, &e1);
            debug::log("count(+0x13)=%d  e0=%08X  e1=%08X", (cnt >> 24) & 0xFF, e0, e1);
            if (valid_ptr(e0)) debug::hexdump("e0", e0, 0x110);   // name@+0x7C, hpp@+0xEC
            if (valid_ptr(e1)) debug::hexdump("e1", e1, 0x110);
        }
        g_host.console().print(">>> chain -> aiohud_debug.log <<<");
        return;
    }
    if (strstr(buf, "dump")) {                            // //aio dump <hexaddr> [hexlen] -> hexdump any address
        const char* p = strstr(buf, "dump") + 4;
        while (*p == ' ') p++;
        u32 addr = 0; while (*p) { char ch = *p; int d; if (ch >= '0' && ch <= '9') d = ch - '0'; else if (ch >= 'a' && ch <= 'f') d = ch - 'a' + 10; else break; addr = addr * 16 + d; p++; }
        while (*p == ' ') p++;
        int len = 0; while (*p) { char ch = *p; int d; if (ch >= '0' && ch <= '9') d = ch - '0'; else if (ch >= 'a' && ch <= 'f') d = ch - 'a' + 10; else break; len = len * 16 + d; p++; }
        if (len <= 0 || len > 0x800) len = 0x100;
        if (valid_ptr(addr)) { debug::hexdump("dump", addr, len); g_host.console().print(">>> dump -> aiohud_debug.log <<<"); }
        else g_host.console().print(">>> bad address <<<");
        return;
    }
    if (strstr(buf, "grabmod")) {                         // dump the LIVE (POL-decrypted) FFXiMain image to a file for Ghidra
        u32 ffm = (u32)GetModuleHandleA("FFXiMain.dll");
        if (!ffm) { g_host.console().print(">>> grabmod: FFXiMain not found <<<"); return; }
        u32 e = 0; safe_read(ffm + 0x3C, &e); u32 sizeImg = 0; safe_read(ffm + e + 0x50, &sizeImg);
        if (sizeImg == 0 || sizeImg > 0x4000000) sizeImg = 0xBE2000;   // fallback = file SizeOfImage
        const char* path = "D:\\Windower Tetsouo\\plugins\\_aiohud_re\\re\\ffximain_dump.bin";
        HANDLE h = CreateFileA(path, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
        if (h == INVALID_HANDLE_VALUE) { g_host.console().print(">>> grabmod: cannot open output file <<<"); return; }
        static unsigned char page[0x1000]; u32 wrote = 0, zero = 0; const unsigned RDM = PAGE_READONLY|PAGE_READWRITE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_WRITECOPY;
        for (u32 off = 0; off < sizeImg; off += 0x1000) {
            MEMORY_BASIC_INFORMATION mbi; bool ok = false;
            if (VirtualQuery((void*)(ffm + off), &mbi, sizeof(mbi))) {
                bool rd = (mbi.State == MEM_COMMIT) && (mbi.Protect & RDM) && !(mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS));
                if (rd && guarded_copy(ffm + off, page, 0x1000) == 0x1000) ok = true;
            }
            if (!ok) { memset(page, 0, 0x1000); ++zero; } else ++wrote;
            DWORD bw = 0; WriteFile(h, page, 0x1000, &bw, 0);
        }
        CloseHandle(h);
        debug::log("GRABMOD: FFXiMain base=%08X sizeImg=%08X -> %u pages written, %u zero-filled", ffm, sizeImg, wrote, zero);
        g_host.console().print(">>> grabmod: ffximain_dump.bin written (memory image @ base 0x10000000) <<<");
        return;
    }
    if (strstr(buf, "rcfind")) {                          // find the recast ARRAY : put an ability on recast, run (no menu needed)
        u32 ffm = (u32)GetModuleHandleA("FFXiMain.dll");
        u32 R = 0; safe_read(ffm + 0x63449C, &R);         // live reference frame-count (the on-recast action)
        if (R < 600) { g_host.console().print(">>> rcfind: 0x63449C too low -- put a long-recast JA on cooldown (Elemental Seal) first <<<"); return; }
        u32 lo = (R > 240) ? R - 240 : 1, hi = R + 240;   // +-4s window for inter-frame drift
        debug::log("RCFIND: ref 0x63449C=%u (%us)  scanning [%u,%u] (excl %08X)", R, R/60, lo, hi, ffm + 0x63449C);
        scan_word_range(lo, hi, ffm + 0x63449C);
        g_host.console().print(">>> rcfind -> aiohud_debug.log <<<");
        return;
    }
    if (strstr(buf, "recast")) {                          // decode the examined action's recast ("Next") -- hover an ON-RECAST JA, then run
        u32 ffm = (u32)GetModuleHandleA("FFXiMain.dll");
        u32 spell = 0, araw = 0, rc = 0;
        safe_read(ffm + 0x634F28, &spell); safe_read(ffm + 0x634590, &araw); safe_read(ffm + 0x63449C, &rc);
        int abil = (araw >= 0x200) ? (int)(araw - 0x200) : (int)araw;
        const aio::AbilRow* a = (araw >= 0x200) ? aio::abil_info(araw - 0x200) : 0;
        const aio::SpellRow* sp = aio::spell_info(spell);
        debug::log("RECAST: spell=%u(rid=%d) araw=%u abil=%d(%s rid=%d)  0x63449C=%08X (%u)",
                   spell, sp ? (int)sp->recast_id : -1, araw, abil, a ? a->en : "?", a ? (int)a->recast_id : -1, rc, rc);
        debug::log("  0x63449C as: /60=%u  /30=%u  /1000=%u  lo16=%u hi16=%u", rc/60, rc/30, rc/1000, rc & 0xFFFF, rc >> 16);
        debug::hexdump("around 0x63449C", ffm + 0x634480, 0x60);   // look for the countdown / an array
        g_host.console().print(">>> recast -> aiohud_debug.log (hover an ON-RECAST JA and tell me the in-game Next) <<<");
        return;
    }
    if (strstr(buf, "menu")) {                            // "//aio menu N" -> switch window skin ; bare "//aio menu" -> RE probe
        const char* mp = strstr(buf, "menu") + 4; while (*mp == ' ') ++mp;
        if (*mp >= '0' && *mp <= '9') {                   // //aio menu N : select the N-th window theme (1-based)
            g_hud.set_skin(atoi(mp) - 1);
            const char* tn = aio::window_theme_name(g_hud.skin_index());
            char msg[96]; wsprintfA(msg, ">>> window skin -> %d/%d : %s <<<", g_hud.skin_index() + 1, aio::window_theme_count(), tn ? tn : "?");
            g_host.console().print(msg);
            return;
        }
        u32 ffm = (u32)GetModuleHandleA("FFXiMain.dll");
        u32 mptr = 0; safe_read(ffm + 0x5EED6C, &mptr);
        u32 def  = 0; if (valid_ptr(mptr)) safe_read(mptr + 0x04, &def);
        u32 spell = 0, araw = 0; safe_read(ffm + 0x634F28, &spell); safe_read(ffm + 0x634590, &araw);
        debug::log("MENU: mptr=%08X def=%08X  spell=%u araw=%u (abil=%d)  recast=%08X",
                   mptr, def, spell, araw, (int)araw - 0x200, guarded_read32(ffm + 0x63449C));
        if (!valid_ptr(mptr)) { g_host.console().print(">>> menu: none open -- open Magic/Ability first <<<"); return; }
        char nm[9]; for (int i = 0; i < 8; ++i) { u32 c = 0; safe_read(def + 0x4E + i, &c); nm[i] = (char)(c & 0xFF); } nm[8] = 0;
        debug::log("  MENU NAME @def+0x4E = '%s'", nm);   // the zero-tap identifier
        dump_obj_ascii("mptr", mptr, 0x100);              // hunt the menu-name string in the live object
        dump_obj_ascii("def",  def,  0x100);              //   ... and in its category definition
        // follow the menu object's child pointers one level -- the highlighted item id (esp. for abilities,
        // whose examine cache is lazy) likely lives in an item list reached from one of these.
        unsigned kids[] = { mptr + 0x08, mptr + 0x0C, mptr + 0x14, mptr + 0x18, def + 0x00, def + 0x04, def + 0x08 };
        for (int k = 0; k < 7; ++k) { unsigned p = guarded_read32(kids[k]); if (valid_ptr(p)) { char lbl[24]; wsprintfA(lbl, "kid%d", k); dump_obj_ascii(lbl, p, 0x60); } }
        g_host.console().print(">>> menu -> aiohud_debug.log (open Magic, run; open Ability, run; diff) <<<");
        return;
    }
    if (strstr(buf, "obj")) {                             // dump the ffxi OBJECT memory (member ptrs -> game data)
        u32 ff = g_host.service_raw(7);
        debug::hexdump("ffxi-obj", ff, 256);
        g_host.console().print(">>> ffxi object -> aiohud_debug.log <<<");
        return;
    }
    if (strstr(buf, "find")) {                            // //aio find <hexid> -> scan memory for a 32-bit value (LE)
        const char* p = strstr(buf, "find") + 4;
        while (*p == ' ') p++;
        u32 v = 0; while (*p) { char ch = *p; int d; if (ch >= '0' && ch <= '9') d = ch - '0'; else if (ch >= 'a' && ch <= 'f') d = ch - 'a' + 10; else break; v = v * 16 + d; p++; }
        unsigned char pat[4] = { (unsigned char)v, (unsigned char)(v >> 8), (unsigned char)(v >> 16), (unsigned char)(v >> 24) };
        debug::log("find: scanning for %08X (LE %02X %02X %02X %02X)", v, pat[0], pat[1], pat[2], pat[3]);
        mem_scan(pat, 4);
        g_host.console().print(">>> find -> aiohud_debug.log <<<");
        return;
    }
    if (strstr(buf, "scan")) {                            // scan memory for the cached player name (find party struct)
        const char* nm = aio::party().count > 0 ? aio::party().m[0].name : 0;
        if (nm && nm[0]) { mem_scan((const unsigned char*)nm, (int)strlen(nm)); g_host.console().print(">>> scan done -> aiohud_debug.log <<<"); }
        else g_host.console().print(">>> no cached name -- load in a party first <<<");
        return;
    }
    if (strstr(buf, "pkt")) {                             // //aio pkt [hexid] -> log party packets (or a specific id)
        const char* p = strstr(buf, "pkt") + 3;
        while (*p == ' ') p++;
        int id = -1;
        if (*p) { id = 0; while (*p) { char ch = *p; int d; if (ch >= '0' && ch <= '9') d = ch - '0'; else if (ch >= 'a' && ch <= 'f') d = ch - 'a' + 10; else break; id = id * 16 + d; p++; } }
        g_pkt_filter = id;
        g_pkt_log = 30;
        g_host.console().print(id >= 0 ? ">>> logging next packets (specific id) <<<" : ">>> logging party packets 0xDD/0xDF <<<");
        return;
    }

    const char* lp = strstr(buf, "lay");                 // //aio lay N -> effect layers
    if (lp) {
        const char* p = lp + 3; while (*p && (*p < '0' || *p > '9')) p++;
        if (*p && g_hud.bars()) g_hud.bars()->set_layers(*p - '0');
        return;
    }
    parse_fill_string(buf);
}

const char* aio_plugin_name()        { return "AioTest"; }
// NB: Windower uses GetDescription (lowercased) as the plugin's CONSOLE COMMAND
// alias -> this string IS the //command name. Keep it a single clean word.
const char* aio_plugin_description() { return "aio"; }
