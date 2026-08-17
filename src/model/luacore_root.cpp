// luacore_root.cpp -- see luacore_root.h for WHY the root address is derived instead of hard-coded.
#include "model/luacore_root.h"
#include "windower.h"
#include "windower_debug.h"
#include <windows.h>
#include <cstdio>

namespace aio {

using windower::u32;
using windower::safe_read;
using windower::valid_ptr;

// Last known-good RVAs, newest first. These are a FALLBACK, not the answer : they only matter if the code
// scan below comes back empty (a LuaCore built by a compiler that stops emitting the idiom). A seed is
// never adopted on resemblance -- it has to read back as a live root, or it stays a candidate.
static const u32 SEED_RVA[] = { 0x1CA420, 0x1C8400 };   // 4.7.9.3 (LuaCore 2.6.8.4) ; <= 4.7.9.0
static const int SEED_N = (int)(sizeof(SEED_RVA) / sizeof(SEED_RVA[0]));

// The offsets the bindings dereference straight off the root. Same list as game_mem.cpp's anchors, which is
// the point : we recognise the root by the company it keeps.
static const u32 ROOT_OFF[] = { 0x248, 0x3C, 0x40, 0x24, 0x50, 0x4C, 0x5C, 0x54, 0x58 };
static const int ROOT_OFF_N = (int)(sizeof(ROOT_OFF) / sizeof(ROOT_OFF[0]));

static u32 luacore_module() { static u32 b = 0; if (!b) b = (u32)GetModuleHandleA("LuaCore.dll"); return b; }

// ---------------------------------------------------------------- module geometry ----

// .text bounds + the whole image range, straight out of the mapped PE headers.
static bool module_ranges(u32 base, u32& textLo, u32& textHi, u32& imgLo, u32& imgHi)
{
    textLo = textHi = imgLo = imgHi = 0;
    u32 e = 0, sig = 0;
    if (!safe_read(base + 0x3C, &e) || e < 0x40 || e > 0x1000) return false;
    if (!safe_read(base + e, &sig) || sig != 0x00004550) return false;          // "PE\0\0"
    u32 nsec = 0, optsz = 0, imgsz = 0;
    if (!safe_read(base + e + 0x06, &nsec)) return false; nsec &= 0xFFFF;
    if (!safe_read(base + e + 0x14, &optsz)) return false; optsz &= 0xFFFF;
    if (!safe_read(base + e + 0x50, &imgsz) || !imgsz || imgsz > 0x4000000) return false;
    if (!nsec || nsec > 64) return false;
    imgLo = base; imgHi = base + imgsz;

    const u32 sect = base + e + 0x18 + optsz;
    for (u32 i = 0; i < nsec; ++i) {
        const u32 s = sect + i * 40;
        u32 n0 = 0, n4 = 0, vsz = 0, va = 0, rsz = 0;
        if (!safe_read(s + 0x00, &n0) || !safe_read(s + 0x04, &n4)) return false;
        if (!safe_read(s + 0x08, &vsz) || !safe_read(s + 0x0C, &va) || !safe_read(s + 0x10, &rsz)) return false;
        if (n0 != 0x7865742E || (n4 & 0xFFFFFF) != 0x000074) continue;          // ".tex" + "t\0\0"
        const u32 sz = vsz ? vsz : rsz;
        if (!va || !sz || sz > imgsz) return false;
        textLo = base + va; textHi = base + va + sz;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------- the scan ----

struct Cand { u32 addr; int score; };

// Decode `mov r32,[imm32]` at p -> register + immediate + instruction length. Two encodings reach a global:
//   A1 imm32                 mov eax,[imm32]      (moffs form, eax only)
//   8B /r mod=00 rm=101      mov r32,[imm32]      (any register)
static bool decode_load(const unsigned char* p, int& reg, u32& imm, int& len)
{
    if (p[0] == 0xA1) { reg = 0; imm = *(const u32*)(p + 1); len = 5; return true; }
    if (p[0] == 0x8B) {
        const unsigned char m = p[1];
        if ((m >> 6) == 0 && (m & 7) == 5) { reg = (m >> 3) & 7; imm = *(const u32*)(p + 2); len = 6; return true; }
    }
    return false;
}

// Decode `mov r32,[reg+disp]` at p, for the SAME register the load just filled -> disp, or -1.
static int decode_deref(const unsigned char* p, int reg)
{
    if (p[0] != 0x8B) return -1;
    const unsigned char m = p[1];
    if ((int)(m & 7) != reg) return -1;
    if ((int)(m & 7) == 4) return -1;                   // rm=100 is a SIB byte, not our register
    const int mod = m >> 6;
    if (mod == 1) return (int)p[2];                     // disp8 (all our offsets are positive and small)
    if (mod == 2) return (int)*(const u32*)(p + 2);     // disp32
    return -1;                                          // mod=00 (no displacement) / mod=11 (register form)
}

// Walk .text counting, per global, how many times it is loaded and IMMEDIATELY dereferenced at one of the
// root offsets. Returns the winner, or 0 if nothing stands out. The whole walk is inside one SEH block:
// it reads a mapped, read-only section of our own process, but a malformed header must degrade to "no
// answer", never to a crash (rule 5).
static u32 scan_text(u32 textLo, u32 textHi, u32 imgLo, u32 imgHi, int& winScore, int& runnerUp)
{
    Cand cand[24];
    int nCand = 0;
    winScore = runnerUp = 0;

    __try {
        const unsigned char* lo = (const unsigned char*)textLo;
        const unsigned char* hi = (const unsigned char*)textHi;
        for (const unsigned char* p = lo; p + 16 < hi; ++p) {
            int reg = 0, len = 0; u32 imm = 0;
            if (!decode_load(p, reg, imm, len)) continue;
            if (imm < imgLo || imm >= imgHi || (imm & 3)) continue;             // a global, dword-aligned, in OUR image
            const int disp = decode_deref(p + len, reg);
            if (disp < 0) continue;
            bool known = false;
            for (int i = 0; i < ROOT_OFF_N; ++i) if ((u32)disp == ROOT_OFF[i]) { known = true; break; }
            if (!known) continue;

            int slot = -1;
            for (int i = 0; i < nCand; ++i) if (cand[i].addr == imm) { slot = i; break; }
            if (slot < 0) {
                if (nCand >= (int)(sizeof(cand) / sizeof(cand[0]))) continue;   // 24 distinct globals is already absurd
                slot = nCand++; cand[slot].addr = imm; cand[slot].score = 0;
            }
            ++cand[slot].score;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }

    u32 best = 0;
    for (int i = 0; i < nCand; ++i) {
        if (cand[i].score > winScore) { runnerUp = winScore; winScore = cand[i].score; best = cand[i].addr; }
        else if (cand[i].score > runnerUp) runnerUp = cand[i].score;
    }
    // The real root wins by a landslide (12 hits vs 0 on LuaCore 2.6.8.4). Demanding a MARGIN as well as a
    // count is what keeps a coincidence from being adopted as an anchor : a tie means we did not recognise
    // anything, and saying so is better than picking one.
    if (winScore < 5 || winScore <= runnerUp * 2) return 0;
    return best;
}

// ---------------------------------------------------------------- resolution ----

// Does this root read back like the real thing? Only ever used to CONFIRM (to choose between seeds, and to
// report health) -- never to reject : at the login screen every one of these pointers is legitimately 0,
// and treating "no world loaded yet" as "wrong address" is exactly the transient-becomes-permanent trap.
static bool root_alive(u32 slot)
{
    u32 g = 0;
    if (!safe_read(slot, &g) || !valid_ptr(g)) return false;
    u32 v = 0;
    if (safe_read(g + 0x248, &v) && valid_ptr(v)) return true;      // party array
    if (safe_read(g + 0x3C,  &v) && valid_ptr(v)) return true;      // player struct
    return false;
}

static u32         g_rva     = 0;
static const char* g_how     = "unresolved";
static bool        g_done    = false;   // an answer we will not revisit (scan hit, or a seed proven live)
static bool        g_scanned = false;   // .text has been walked once ; the image cannot change under us
static bool        g_warned  = false;   // the "nothing proven yet" line is worth saying ONCE, not per read

// The scan is DETERMINISTIC -- same image, same answer -- so it runs exactly once per session and its
// result is final. That is what keeps this off the hot path : data_root() calls in here on every single
// game read, and a scan that re-ran on failure would walk 1.4 MB of .text per read.
static void scan_stage(u32 base)
{
    if (g_scanned) return;
    u32 textLo = 0, textHi = 0, imgLo = 0, imgHi = 0;
    if (!module_ranges(base, textLo, textHi, imgLo, imgHi)) return;   // headers not readable : retry, don't latch
    g_scanned = true;

    int win = 0, second = 0;
    const u32 hit = scan_text(textLo, textHi, imgLo, imgHi, win, second);
    if (!hit) {
        windower::debug::log("lc: code scan recognised no data root in LuaCore .text (best %d hits, next %d) -- "
                             "falling back to the seeds. If the HUD is blank, this line is why.", win, second);
        return;
    }
    g_rva = hit - base; g_how = "code scan"; g_done = true;
    windower::debug::log("lc: data root = LuaCore+%06X (code scan : %d matching derefs, next best %d)%s",
                         g_rva, win, second, root_alive(base + g_rva) ? " [live]" : " [no world loaded yet]");
    for (int i = 0; i < SEED_N; ++i) if (SEED_RVA[i] == g_rva) return;
    // Worth saying out loud : the constant this build shipped with is wrong, and the scan is the only
    // reason anything works at all. That line is the entire point of the mechanism.
    windower::debug::log("lc: that is NOT the address this build shipped with (%06X) -- Windower was updated "
                         "under us and the root moved ; the scan recovered it", SEED_RVA[0]);
}

static void resolve(u32 base)
{
    scan_stage(base);
    if (g_done) return;

    // Seeds : cheap (a handful of reads), and only adopted once one actually reads back as a root. Until
    // then we keep handing out the newest seed -- an unproven best guess, revisited on the next call.
    for (int i = 0; i < SEED_N; ++i) {
        if (root_alive(base + SEED_RVA[i])) {
            g_rva = SEED_RVA[i]; g_how = "seed"; g_done = true;
            windower::debug::log("lc: data root = LuaCore+%06X (seed, proven live)", g_rva);
            return;
        }
    }
    g_rva = SEED_RVA[0]; g_how = "seed (unconfirmed)";
    if (!g_warned) {
        g_warned = true;
        windower::debug::log("lc: no data root proven yet -- using seed %06X. Normal at the login screen ; "
                             "if it persists in game, the HUD will stay blank and //aio doctor will say so.", g_rva);
    }
}

unsigned lc_root_addr()
{
    const u32 base = luacore_module();
    if (!base) return 0;                 // LuaCore not mapped : no answer, and nothing latched
    if (!g_done) resolve(base);
    return g_rva ? base + g_rva : 0;
}

unsigned    lc_root_rva()  { lc_root_addr(); return g_rva; }
const char* lc_root_how()  { lc_root_addr(); return g_how; }
bool        lc_root_live() { const u32 slot = lc_root_addr(); return slot && root_alive(slot); }

// ---------------------------------------------------------------- the recast block ----

static const u32 SEED_RECAST_SPELLS = 0x238;   // 4.7.9.3 ; it was 0x234 up to 4.7.9.0
static u32         g_rcSpells = 0;
static const char* g_rcHow    = "seed";
static bool        g_rcDone   = false;

// get_spell_recasts' shape : load the root, deref at X, and loop to 0x400 (the 1024-entry array). The bound
// is what makes it unmistakable -- no other binding walks that far off this root.
//   A1 <root> / 8B 0D <root>     mov r32,[root]
//   8B 80 <disp32>               mov r32,[r32+X]
//   ... 81 /7 00 04 00 00        cmp r32,0x400        (or 3D 00 04 00 00 for eax)
static bool sig_spell_loop(const unsigned char* after, int span)
{
    for (int i = 0; i + 6 <= span; ++i) {
        if (after[i] == 0x3D && after[i+1] == 0x00 && after[i+2] == 0x04 && after[i+3] == 0x00 && after[i+4] == 0x00) return true;
        if (after[i] == 0x81 && after[i+1] >= 0xF8 &&
            after[i+2] == 0x00 && after[i+3] == 0x04 && after[i+4] == 0x00 && after[i+5] == 0x00) return true;
    }
    return false;
}

// get_ability_recasts' shape : deref at Y into a register, then index it with a SCALE-8 byte read --
// `movzx r32, byte [Yptr + i*8]`, i.e. 0F B6 /r with rm=100 (SIB), scale=3, base = that register.
static bool sig_ids_stride8(const unsigned char* after, int span, int reg)
{
    for (int i = 0; i + 4 <= span; ++i) {
        if (after[i] != 0x0F || after[i+1] != 0xB6) continue;
        const unsigned char m = after[i+2], sib = after[i+3];
        if ((m >> 6) != 0 || (m & 7) != 4) continue;          // must be the [reg + idx*scale] form
        if ((sib >> 6) != 3) continue;                        // scale 8
        if ((sib & 7) == reg) return true;                    // ...based on the pointer we just loaded
    }
    return false;
}

// Walk .text once more, now that the root address is known, looking for those two shapes. Returns the
// SPELL-array offset, and reports what the ability side independently said.
static u32 scan_recast(u32 textLo, u32 textHi, u32 rootAddr, u32& idsOff)
{
    u32 spells = 0; idsOff = 0;
    __try {
        const unsigned char* lo = (const unsigned char*)textLo;
        const unsigned char* hi = (const unsigned char*)textHi;
        for (const unsigned char* p = lo; p + 80 < hi; ++p) {
            int reg = 0, len = 0; u32 imm = 0;
            if (!decode_load(p, reg, imm, len)) continue;
            if (imm != rootAddr) continue;
            // The deref is NOT necessarily the next instruction : the compiler schedules unrelated work in
            // between (get_spell_recasts puts four instructions there). Look ahead a short window for the
            // first `mov r32,[<our reg> + disp32]` -- requiring adjacency made this scan match nothing at
            // all, and it still LOOKED healthy because the seed happened to be right.
            for (const unsigned char* q = p + len; q < p + len + 32; ++q) {
                if (q[0] != 0x8B) continue;
                const unsigned char m = q[1];
                if ((int)(m & 7) != reg || (m & 7) == 4 || (m >> 6) != 2) continue;   // mov r32,[reg+disp32]
                const u32 disp = *(const u32*)(q + 2);
                const int dstReg = (m >> 3) & 7;                         // where the table pointer landed
                const unsigned char* after = q + 6;
                if (disp >= 0x100 && disp <= 0x800) {                    // the recast block lives up here
                    if (!spells && sig_spell_loop(after, 64)) spells = disp;
                    if (!idsOff && sig_ids_stride8(after, 24, dstReg)) idsOff = disp;
                }
                break;                                                   // one deref per load site
            }
            if (spells && idsOff) break;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
    return spells;
}

static void ensure_recast()
{
    if (g_rcDone) return;
    const u32 root = lc_root_addr();
    if (!root) { g_rcSpells = SEED_RECAST_SPELLS; return; }              // LuaCore not mapped : don't latch
    const u32 base = luacore_module();
    u32 tLo = 0, tHi = 0, iLo = 0, iHi = 0;
    if (!module_ranges(base, tLo, tHi, iLo, iHi)) { g_rcSpells = SEED_RECAST_SPELLS; return; }

    u32 ids = 0;
    const u32 spells = scan_recast(tLo, tHi, root, ids);
    g_rcDone = true;

    // Neither binding is trusted alone : the spell array must sit exactly one dword past the ids table,
    // which is the only layout either of them describes. Disagreement means we matched something else.
    if (spells && ids && spells == ids + 4) {
        g_rcSpells = spells; g_rcHow = "code scan";
        windower::debug::log("lc: recast block = g+%03X timers / g+%03X ids / g+%03X spells "
                             "(code scan, both bindings agree)", spells - 8, ids, spells);
    } else {
        g_rcSpells = SEED_RECAST_SPELLS; g_rcHow = "seed";
        windower::debug::log("lc: recast block NOT derived (spell sig %03X, ability sig %03X) -- using seed "
                             "g+%03X. If the recast list shows many spells on the same timer, this is why.",
                             spells, ids, g_rcSpells);
    }
}

unsigned    lc_recast_spells()    { ensure_recast(); return g_rcSpells; }
unsigned    lc_recast_ja_ids()    { ensure_recast(); return g_rcSpells - 4; }
unsigned    lc_recast_ja_timers() { ensure_recast(); return g_rcSpells - 8; }
const char* lc_recast_how()       { ensure_recast(); return g_rcHow; }

} // namespace aio
