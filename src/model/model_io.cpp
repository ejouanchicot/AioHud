// model_io.cpp -- see model_io.h. The in-game seams, and the no-op observer of a release build.
#include "model/model_io.h"
#include <windows.h>
#include <cstring>

namespace aio {

bool model_read_u32(u32 addr, u32* out) {
    u32 v = 0;
    const bool ok = windower::safe_read(addr, &v);
    if (out) *out = ok ? v : 0;
    if (tape_recording()) tape_note(TF_READ_U32, addr, 0, ok ? 1 : 0, &v, ok ? 4u : 0u);
    return ok;
}

bool model_copy(u32 addr, void* out, unsigned n) {
    bool ok = false;
    __try { memcpy(out, (const void*)addr, n); ok = true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    if (tape_recording()) tape_note(TF_COPY, addr, n, ok ? 1 : 0, out, ok ? n : 0u);
    return ok;
}

u32 model_module_base(const char* name) {
    const u32 base = (u32)(uintptr_t)GetModuleHandleA(name);
    if (tape_recording()) tape_note(TF_MODULE_BASE, tape_hash(name, (unsigned)strlen(name)), 0, base ? 1 : 0, &base, 4);
    return base;
}

#ifndef AIOHUD_DEVTOOLS
// Release build : nothing observes the model.
bool tape_recording() { return false; }
void tape_note(uint16_t, u32, u32, int, const void*, unsigned) {}
void tape_event_begin(char, unsigned, long long) {}
void tape_event_end() {}
void tape_packet(int, const unsigned char*) {}
void tape_frame(const void*, unsigned) {}
#endif

} // namespace aio
