// windower_debug.h -- exploration / RE helpers for poking at Windower internals
// from inside the plugin: append-to-file logging, hex dumps of objects, RTTI
// class-name resolution, and vtable method-RVA listing (relative to a module
// base, so addresses map straight into Ghidra at base 0x10000000).
#pragma once
#include "windower.h"
#include <cstdarg>

namespace windower { namespace debug {

// Absolute path NEXT TO the plugin DLL (Windower\plugins\aiohud_debug.log). A relative path wrote to the game's
// CWD, which FAILS silently when the game is installed under Program Files (write-protected -> no log, or a
// hidden VirtualStore redirect). The plugins\ folder is writable (the config saves there) and easy to find --
// right beside AioHud.dll. Resolved once from this DLL's own module path. Falls back to the relative name.
inline const char* log_path() {
    static char p[MAX_PATH] = { 0 };
    if (!p[0]) {
        HMODULE hm = NULL;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCSTR)&log_path, &hm) && hm && GetModuleFileNameA(hm, p, MAX_PATH)) {
            char* b = p; for (char* q = p; *q; ++q) if (*q == '\\' || *q == '/') b = q + 1;   // -> after the last slash
            lstrcpynA(b, "aiohud_debug.log", (int)(MAX_PATH - (b - p)));                       // ...\plugins\aiohud_debug.log
        } else { lstrcpynA(p, "aiohud_debug.log", MAX_PATH); }                                 // fallback : CWD
    }
    return p;
}

inline void raw(const char* s, int len) {
    HANDLE h = CreateFileA(log_path(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) { DWORD w; WriteFile(h, s, len, &w, NULL); CloseHandle(h); }
}
// WHO wrote this line. Every client of a multi-boxed setup appends to the SAME file (the path is derived from
// the DLL, not from the character), so a capture from two clients interleaves into something that can be read
// both ways -- and was, on 2026-07-26 : a BUFFOWNER line from the mule's client was taken as proof that the
// main's `self_id()` was wrong, and cost a full round of analysis. A capture that cannot say who produced it is
// worse than no capture, because it produces confident wrong conclusions.
inline char* tag_buf() { static char t[24] = { 0 }; return t; }
inline void set_tag(const char* name) {   // called once the character name is known ; cheap, idempotent
    char* t = tag_buf();
    if (!name || !name[0]) { t[0] = 0; return; }
    if (t[0] && !lstrcmpA(t, name)) return;
    lstrcpynA(t, name, 24);
}
inline void log(const char* fmt, ...) {
    char buf[1060]; int pre = 0;                     // 1024 for wvsprintfA + the tag + 2 for CRLF
    { const char* t = tag_buf(); if (t[0]) { pre = wsprintfA(buf, "[%s] ", t); if (pre < 0) pre = 0; } }
    va_list ap; va_start(ap, fmt);
    int n = wvsprintfA(buf + pre, fmt, ap); va_end(ap);   // NB: wvsprintfA has no %f ; caps at 1023 chars
    if (n < 0) n = 0; if (n > 1023) n = 1023;        // never let CRLF write past buf (was buf[1024] overrun)
    n += pre;
    buf[n++] = '\r'; buf[n++] = '\n';
    raw(buf, n);
}
// "Log this once per distinct key" -- with the trap removed.
//
// MEASURED on a live log, 2026-07-27: 103 380 BUFFOWNER lines for 184 DISTINCT ones. A x562 amplification, in a
// diagnostic whose own comment promised "a handful of lines a session". Every hand-rolled copy of this idiom had
// the same shape:
//     if (!seen_it) { if (n < CAP) tbl[n++] = key; log(...); }      <-- the bug
// Once the table is FULL the key is no longer recorded, so `seen_it` is false forever after and the line logs on
// EVERY FRAME. Saturation does not stop the logging, it starts it. And it is not just disk: raw() opens, writes
// and closes the file per line, on the render thread, inside the draw loop.
//
// Here, a full table goes QUIET -- and says so once, because a probe that dies silently reads exactly like a bug
// that stopped happening (CLAUDE.md rule 10's corollary).
template <int N>
struct LogOnce {
    unsigned k[N] = {};
    int      n = 0;
    bool     warned = false;
    bool first(unsigned key) {
        for (int i = 0; i < n; ++i) if (k[i] == key) return false;
        if (n >= N) {
            if (!warned) { warned = true; log("log-once table full (%d distinct keys) -- further NEW keys are MUTED for this session", N); }
            return false;
        }
        k[n++] = key;
        return true;
    }
};

// SESSION START. This used to be clear() -- truncate the log at every plugin init -- and that quietly destroyed
// the evidence for the exact bug you were chasing. "//unload AioHud, //load AioHud" is a tester's first reflex
// when something looks wrong, and it is also what the auto-updater does on every update: both wiped the capture
// of the very thing that had just gone wrong, so the reply was always "reproduce it again". A log that erases
// itself at the moment of interest is worse than no log, because you believe you have one.
//
// Instead: BOUND it and ROTATE it. Past the cap, the current file becomes aiohud_debug.prev.log (one generation
// kept -- enough to survive exactly the unload/load cycle that used to destroy it) and a fresh one starts. Under
// the cap, we simply append. Either way the session banner below is the first thing written.
//
// The banner also answers the two questions every capture used to leave open -- WHICH BUILD, and WHEN. The
// updater silently replaces a tester's build, so "which version produced this line" was never answerable from
// the file itself; and with no timestamps, two clients' interleaved lines could not be ordered.
inline void begin_session(const char* version) {
    const DWORD CAP = 4u * 1024u * 1024u;                     // ~4 MB : dozens of sessions of ordinary logging
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (GetFileAttributesExA(log_path(), GetFileExInfoStandard, &fa) && fa.nFileSizeHigh == 0 && fa.nFileSizeLow > CAP) {
        char prev[MAX_PATH]; lstrcpynA(prev, log_path(), MAX_PATH);
        char* b = prev; for (char* q = prev; *q; ++q) if (*q == '\\' || *q == '/') b = q + 1;
        lstrcpynA(b, "aiohud_debug.prev.log", (int)(MAX_PATH - (b - prev)));
        DeleteFileA(prev);
        MoveFileA(log_path(), prev);                          // best-effort : if it fails we just keep appending
    }
    SYSTEMTIME t; GetLocalTime(&t);
    char line[256];
    int n = wsprintfA(line, "\r\n===== AioHUD v%s  session start  %04d-%02d-%02d %02d:%02d:%02d  pid=%lu =====\r\n",
                      version ? version : "?", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond,
                      (unsigned long)GetCurrentProcessId());
    if (n > 0) raw(line, n);
}

// MSVC RTTI class name of a polymorphic object (obj->vtbl[-1]=COL -> TypeDescriptor+8).
inline void rtti_name(u32 obj, char* out, int sz) {
    out[0] = 0; u32 vt, col, td;
    if (!safe_read(obj, &vt) || !valid_ptr(vt)) return;
    if (!safe_read(vt - 4, &col) || !valid_ptr(col)) return;
    if (!safe_read(col + 0x0c, &td) || !valid_ptr(td)) return;
    __try { lstrcpynA(out, (const char*)(td + 8), sz); } __except (EXCEPTION_EXECUTE_HANDLER) { out[0] = 0; }
}

inline u32 module_base_of(u32 code_addr, char* name_out, int name_sz) {
    if (name_out) name_out[0] = 0;
    HMODULE hm = NULL;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)code_addr, &hm) && hm) {
        if (name_out) {
            char p[MAX_PATH]; GetModuleFileNameA(hm, p, MAX_PATH);
            const char* b = p; for (const char* q = p; *q; q++) if (*q == '\\' || *q == '/') b = q + 1;
            lstrcpynA(name_out, b, name_sz);
        }
        return (u32)hm;
    }
    return 0;
}

inline void hexdump(const char* label, u32 addr, int len) {
    log("--- %s @0x%08X (%d bytes) ---", label, addr, len);
    char line[200];
    for (int off = 0; off < len; off += 16) {
        int n = wsprintfA(line, "+0x%03X:", off);
        for (int j = 0; j < 16; j += 4) { u32 v = 0; safe_read(addr + off + j, &v); n += wsprintfA(line + n, " %08X", v); }
        log("%s", line);
    }
}

// list the first `count` vtable methods of an interface object, with their RVA
// relative to the owning module base (-> Ghidra addr = 0x10000000 + RVA).
inline void dump_vtable(const char* label, u32 obj, int count) {
    char rtti[160]; rtti_name(obj, rtti, sizeof(rtti));
    u32 vt = 0; safe_read(obj, &vt);
    char mod[MAX_PATH]; u32 base = module_base_of(vt, mod, sizeof(mod));
    log("--- %s = 0x%08X  vtbl=0x%08X in %s (base 0x%08X)  rtti=%s ---", label, obj, vt, mod, base, rtti);
    for (int i = 0; i < count; i++) {
        u32 fn = 0; if (!safe_read(vt + i * 4, &fn) || !valid_ptr(fn)) continue;
        log("   [%2d] 0x%08X = %s+0x%X", i, fn, mod, base ? fn - base : 0);
    }
}

inline float as_float(u32 v) { float f; for (int i = 0; i < 4; i++) ((char*)&f)[i] = ((char*)&v)[i]; return f; }

// read the name std::string of an object (MSVC SSO: inline buf at +off, or heap ptr if cap>=16)
inline void obj_name(u32 obj, char* out, int sz) {
    out[0] = 0; u32 cap = 0, off = obj + 4;
    safe_read(off + 0x14, &cap);
    if (cap >= 16) { u32 p = 0; if (safe_read(off, &p) && valid_ptr(p)) __try { lstrcpynA(out, (const char*)p, sz); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
    else __try { lstrcpynA(out, (const char*)off, sz); } __except (EXCEPTION_EXECUTE_HANDLER) { out[0] = 0; }
}

// enumerate a Handler's std::vector<shared_ptr<Object>> at +0x48(begin)/+0x4c(end), stride 8.
// kind 'P' = PrimitiveObject (vis+0x28, tl+0x30, br+0x38), 'T' = TextObject (vis+0x30, pos+0x58, size+0x7c)
inline void dump_list(const char* tag, u32 handler, char kind) {
    if (!valid_ptr(handler)) { log("%s: handler invalid", tag); return; }
    // auto-find the std::vector<shared_ptr> (begin<=end<=cap, all heap, 8-byte stride)
    u32 begin = 0, end = 0; int voff = -1;
    for (int off = 0x10; off <= 0x140; off += 4) {
        u32 b = 0, e = 0, c = 0;
        safe_read(handler + off, &b); safe_read(handler + off + 4, &e); safe_read(handler + off + 8, &c);
        if (valid_ptr(b) && valid_ptr(e) && valid_ptr(c) && b <= e && e <= c &&
            (e - b) <= 0x8000 && (e - b) >= 8 && ((e - b) % 8) == 0) { begin = b; end = e; voff = off; break; }
    }
    int count = (voff >= 0) ? (int)((end - begin) / 8) : -1;
    log("=== %s @0x%08X  vector@+0x%x begin=%08X end=%08X count=%d ===", tag, handler, voff, begin, end, count);
    if (count < 0) { hexdump("  handler bytes", handler, 0x140); return; }
    if (count > 500) return;
    int i = 0;
    for (u32 e = begin; e + 8 <= end && i < 120; e += 8, i++) {
        u32 obj = 0; if (!safe_read(e, &obj) || !valid_ptr(obj)) continue;
        char nm[28]; obj_name(obj, nm, sizeof(nm));
        if (kind == 'P') {
            u32 vis = 0, tlx = 0, tly = 0, brx = 0, bry = 0;
            safe_read(obj + 0x28, &vis); safe_read(obj + 0x30, &tlx); safe_read(obj + 0x34, &tly);
            safe_read(obj + 0x38, &brx); safe_read(obj + 0x3c, &bry);
            log("  P[%d] '%s' vis=%d tl=(%d,%d) br=(%d,%d) [%08X]",
                i, nm, vis & 1, (int)as_float(tlx), (int)as_float(tly), (int)as_float(brx), (int)as_float(bry), obj);
        } else {
            u32 vis = 0, px = 0, py = 0, sz = 0;
            safe_read(obj + 0x30, &vis); safe_read(obj + 0x58, &px); safe_read(obj + 0x5c, &py); safe_read(obj + 0x7c, &sz);
            log("  T[%d] '%s' vis=%d pos=(%d,%d) size=%d [%08X]",
                i, nm, vis & 1, (int)as_float(px), (int)as_float(py), (int)as_float(sz), obj);
        }
    }
}

}} // namespace windower::debug
