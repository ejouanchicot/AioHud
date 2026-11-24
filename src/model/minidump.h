// minidump.h -- which module a crash dump says the game died in (for //aio doctor).
//
// WHY. Windows wrote 64 pol.exe crashes in 60 days on the author's machine (event log, 2026-11-24) : 21 in an unloaded
// LuaCore, 13 in Windower's Hook.dll -- 8 of those at the very same address, one right after an //unload of AioHUD.
// Audit R1 (our JMP hooks restored from the unload thread while the game runs them) would leave exactly that kind of
// trace, or a crash in "AioHud.dll" itself once unloaded. With Windows' LocalDumps on, every crash leaves a .dmp ; this
// reads the three things that settle "was it us" out of one, without a debugger : the exception code, its address, and
// the module (LOADED or UNLOADED) that address falls in.
//
// The minidump format (dbghelp's MINIDUMP_*), only the parts used : the header and its stream directory, the
// exception stream (6), the module list (4) and the unloaded-module list (14). A full dump (DumpType 2) has them too.
// Pure : the reader is a parameter -- tests/t_minidump.cpp feeds it a dump built in memory.
#pragma once
#include <string.h>

namespace aio {

struct DumpCrash {
    bool               ok;           // the dump was read and its exception located
    unsigned           code;         // e.g. 0xC0000005 (access violation)
    unsigned long long addr;         // the faulting instruction
    char               module[64];   // the module holding addr ("" = none) ; an unloaded one gets "_unloaded" appended
    unsigned long long offset;       // addr - that module's base
    bool               unloaded;     // the address was in a module ALREADY UNLOADED (the R1 signature, if it is ours)
};

namespace minidump_detail {
inline unsigned           u32at(const unsigned char* p) { unsigned v; memcpy(&v, p, 4); return v; }
inline unsigned long long u64at(const unsigned char* p) { unsigned long long v; memcpy(&v, p, 8); return v; }
// a MINIDUMP_STRING (byte length, then UTF-16) -> its base name in ASCII ('?' for anything else)
template <class Rd> inline void name_at(Rd& rd, unsigned rva, char* out, int cap) {
    out[0] = 0;
    unsigned char lenb[4]; if (!rd((unsigned long long)rva, lenb, 4)) return;
    unsigned len = u32at(lenb) / 2; if (len > 260) len = 260;
    unsigned char w[520]; if (!rd((unsigned long long)rva + 4, w, len * 2)) return;
    int n = 0;
    for (unsigned i = 0; i < len; ++i) {
        const unsigned c = (unsigned)w[2 * i] | ((unsigned)w[2 * i + 1] << 8);
        if (c == '\\' || c == '/') { n = 0; continue; }            // keep the base name only
        if (n < cap - 1) out[n++] = (c >= 32 && c < 127) ? (char)c : '?';
    }
    out[n] = 0;
}
} // namespace minidump_detail

// rd(offset, out, size) -> bool : read `size` bytes of the file at `offset`.
template <class Rd>
inline bool minidump_crash(Rd rd, DumpCrash& out) {
    using namespace minidump_detail;
    memset(&out, 0, sizeof(out));
    unsigned char h[32];
    if (!rd(0ull, h, 32) || u32at(h) != 0x504D444Du) return false;      // "MDMP"
    const unsigned nStreams = u32at(h + 8), dirRva = u32at(h + 12);
    if (nStreams == 0 || nStreams > 4096) return false;
    unsigned exRva = 0, modRva = 0, unlRva = 0;
    for (unsigned i = 0; i < nStreams; ++i) {
        unsigned char d[12]; if (!rd((unsigned long long)dirRva + 12ull * i, d, 12)) return false;
        const unsigned type = u32at(d), rva = u32at(d + 8);
        if (type == 6) exRva = rva; else if (type == 4) modRva = rva; else if (type == 14) unlRva = rva;
    }
    if (!exRva) return false;
    unsigned char ex[32];                                               // ThreadId, align, then MINIDUMP_EXCEPTION
    if (!rd((unsigned long long)exRva, ex, 32)) return false;
    out.code = u32at(ex + 8);
    out.addr = u64at(ex + 24);
    out.ok = true;
    if (modRva) {                                                       // the LOADED modules : 108 bytes each
        unsigned char nb[4];
        if (rd((unsigned long long)modRva, nb, 4)) {
            const unsigned n = u32at(nb) < 4096 ? u32at(nb) : 4096;
            for (unsigned i = 0; i < n; ++i) {
                unsigned char m[24]; if (!rd((unsigned long long)modRva + 4 + 108ull * i, m, 24)) break;
                const unsigned long long modBase = u64at(m); const unsigned modSize = u32at(m + 8);   // NOT "base" / "size" : the replay types fields BY NAME across model headers (sheet_.base)
                if (out.addr >= modBase && out.addr < modBase + modSize) {
                    name_at(rd, u32at(m + 20), out.module, sizeof(out.module)); out.offset = out.addr - modBase; return true;
                }
            }
        }
    }
    if (unlRva) {                                                       // ...then the ones already UNLOADED
        unsigned char uh[12];
        if (rd((unsigned long long)unlRva, uh, 12)) {
            const unsigned hdr = u32at(uh), esz = u32at(uh + 4);
            const unsigned n = u32at(uh + 8) < 4096 ? u32at(uh + 8) : 4096;
            for (unsigned i = 0; i < n && esz >= 24; ++i) {
                unsigned char e[24]; if (!rd((unsigned long long)unlRva + hdr + (unsigned long long)esz * i, e, 24)) break;
                const unsigned long long modBase = u64at(e); const unsigned modSize = u32at(e + 8);
                if (out.addr >= modBase && out.addr < modBase + modSize) {
                    name_at(rd, u32at(e + 20), out.module, sizeof(out.module) - 10);
                    strcat(out.module, "_unloaded"); out.offset = out.addr - modBase; out.unloaded = true; return true;   // rule10-ok: a field of the crash being read, not a latch
                }
            }
        }
    }
    return true;                                                        // located, in no module we know (generated code)
}

} // namespace aio
