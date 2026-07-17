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
inline void log(const char* fmt, ...) {
    char buf[1026]; va_list ap; va_start(ap, fmt);   // 1024 for wvsprintfA + 2 for CRLF
    int n = wvsprintfA(buf, fmt, ap); va_end(ap);    // NB: wvsprintfA has no %f ; caps at 1023 chars
    if (n < 0) n = 0; if (n > 1023) n = 1023;        // never let CRLF write past buf (was buf[1024] overrun)
    buf[n++] = '\r'; buf[n++] = '\n';
    raw(buf, n);
}
inline void clear() {
    HANDLE h = CreateFileA(log_path(), GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
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
