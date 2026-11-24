// hook_relay.cpp -- see hook_relay.h (audit R1).
#include "model/hook_relay.h"
#include "windower_debug.h"
#include <windows.h>
#include <string.h>

namespace aio {

volatile long g_relayInflight = 0;

namespace {

const char MAGIC[8] = { 'A', 'I', 'O', 'H', 'O', 'O', 'K', '1' };

bool read_guarded(unsigned at, void* out, unsigned n) {
    __try { memcpy(out, (const void*)(uintptr_t)at, n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// A block left by a previous load, for THIS function : the signature, the address it serves, a sane length, and a
// relay that really jumps through its own slot. Anything else is not ours and is not touched.
bool block_is_ours(unsigned b, unsigned at, int len) {
    unsigned char k[RELAY_BLOCK];
    if (!read_guarded(b, k, RELAY_BLOCK)) return false;
    if (memcmp(k, MAGIC, 8) != 0) return false;
    unsigned kAt = 0; memcpy(&kAt, k + RELAY_OFF_AT, 4);
    if (kAt != at || k[RELAY_OFF_LEN] != (unsigned char)len) return false;
    unsigned ind = 0; memcpy(&ind, k + RELAY_OFF_STUB + 2, 4);
    return k[RELAY_OFF_STUB] == 0xFF && k[RELAY_OFF_STUB + 1] == 0x25 && ind == b + RELAY_OFF_SLOT;
}

volatile LONG* slot_of(const Hook& h) { return (volatile LONG*)(h.blk + RELAY_OFF_SLOT); }

// The process-wide list of relay blocks : "AIOHUD_RELAYS=XXXXXXXX,XXXXXXXX,..." in the environment.
const char* const ENV = "AIOHUD_RELAYS";
void registry_add(unsigned blk) {
    char cur[512] = ""; const DWORD n = GetEnvironmentVariableA(ENV, cur, sizeof(cur));
    if (n >= sizeof(cur)) cur[0] = 0;                                   // too long to be ours : start over
    char me[12]; wsprintfA(me, "%08X", blk);
    if (strstr(cur, me)) return;
    char out[512]; wsprintfA(out, "%s%s%s", cur, cur[0] ? "," : "", me);
    SetEnvironmentVariableA(ENV, out);
}

} // namespace

int relay_list(RelayInfo* out, int cap) {
    char cur[512] = ""; const DWORD n = GetEnvironmentVariableA(ENV, cur, sizeof(cur));
    if (!n || n >= sizeof(cur)) return 0;
    int k = 0;
    for (const char* p = cur; *p && k < cap; ) {
        unsigned b = 0; int d = 0;
        while (*p && *p != ',') { const char c = *p++; b = b * 16 + (unsigned)(c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10); ++d; }
        if (*p == ',') ++p;
        if (d != 8) continue;
        unsigned char blk[RELAY_BLOCK];
        if (!read_guarded(b, blk, RELAY_BLOCK) || memcmp(blk, MAGIC, 8) != 0) continue;   // not (or no longer) a block of ours
        RelayInfo& r = out[k];
        memcpy(&r.at, blk + RELAY_OFF_AT, 4); r.len = blk[RELAY_OFF_LEN];
        if (r.len < 5 || r.len > 8 || !block_is_ours(b, r.at, r.len)) continue;
        memcpy(r.orig, blk + RELAY_OFF_ORIG, (size_t)r.len);
        ++k;
    }
    return k;
}

unsigned relay_aware_find(unsigned lo, unsigned hi, const unsigned char* pat, const bool* wild, int n, int* hitsOut) {
    if (hitsOut) *hitsOut = 0;
    if (!lo || hi <= lo || n < 1) return 0;
    RelayInfo R[16]; const int nr = relay_list(R, 16);
    auto overlaps = [&](unsigned q) { for (int i = 0; i < nr; ++i) if (q < R[i].at + (unsigned)R[i].len && q + (unsigned)n > R[i].at) return i; return -1; };
    int hits = 0; unsigned at = 0;
    // 1. the raw image, everywhere no relay covers the candidate
    __try {
        for (const unsigned char* q = (const unsigned char*)(uintptr_t)lo; q + n <= (const unsigned char*)(uintptr_t)hi; ++q) {
            if (q[0] != pat[0] && !wild[0]) continue;
            int i = 1; for (; i < n; ++i) if (!wild[i] && q[i] != pat[i]) break;
            if (i == n && overlaps((unsigned)(uintptr_t)q) < 0) { ++hits; at = (unsigned)(uintptr_t)q; if (hits > 1) break; }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    // 2. the candidates a relay covers, compared with the bytes the client had there
    for (int r = 0; r < nr && hits <= 1; ++r) {
        const unsigned from = R[r].at >= (unsigned)n - 1 ? R[r].at - (unsigned)n + 1 : 0, to = R[r].at + (unsigned)R[r].len;
        for (unsigned q = from < lo ? lo : from; q < to && q + (unsigned)n <= hi; ++q) {
            if (overlaps(q) != r) continue;                                  // a candidate under two relays : counted once
            unsigned char view[96]; if (n > 96 || !read_guarded(q, view, (unsigned)n)) continue;
            for (int k = 0; k < nr; ++k)                                     // every relay's original bytes, back in the view
                for (int j = 0; j < R[k].len; ++j) { const unsigned a = R[k].at + (unsigned)j; if (a >= q && a < q + (unsigned)n) view[a - q] = R[k].orig[j]; }
            int i = 0; for (; i < n; ++i) if (!wild[i] && view[i] != pat[i]) break;
            if (i == n) { ++hits; at = q; if (hits > 1) break; }
        }
    }
    if (hitsOut) *hitsOut = hits;
    return hits == 1 ? at : 0;
}

bool relay_on(Hook& h) {
    if (h.on) return true;
    if (!h.at || h.len < 5 || h.len > 8) return false;
    if (!h.blk) {
        unsigned char cur[8];
        if (!read_guarded(h.at, cur, (unsigned)h.len)) { windower::debug::log("NATIVEUI %s at %08X is unreadable -- NOT hooked", h.what, h.at); return false; }
        // ALREADY OURS : a previous load left its JMP and its block (they are never taken out) -> adopt them.
        if (cur[0] == 0xE9) {
            int rel = 0; memcpy(&rel, cur + 1, 4);
            const unsigned b = (unsigned)(h.at + 5 + rel) - RELAY_OFF_STUB;
            if (block_is_ours(b, h.at, h.len)) {
                h.blk = (unsigned char*)(uintptr_t)b;
                memcpy(h.orig, h.blk + RELAY_OFF_ORIG, (size_t)h.len);
                if (memcmp(h.orig, h.pro, (size_t)h.len) != 0) {   // a block for another version of the function
                    windower::debug::log("NATIVEUI %s : the relay left at %08X keeps a different prologue -- NOT hooked", h.what, h.at);
                    h.blk = 0; return false;
                }
                h.tramp = h.blk + RELAY_OFF_TRAMP;
                windower::debug::log("NATIVEUI %s : relay kept from a previous load re-armed (block %08X)", h.what, b);
            }
        }
        if (!h.blk) {
            if (memcmp(cur, h.pro, (size_t)h.len) != 0) { windower::debug::log("NATIVEUI %s prologue changed or unreadable -- NOT hooked", h.what); return false; }
            unsigned char* b = (unsigned char*)VirtualAlloc(0, RELAY_BLOCK, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
            if (!b) { windower::debug::log("NATIVEUI %s : no relay block (VirtualAlloc failed, error %lu) -- NOT hooked", h.what, GetLastError()); return false; }
            memset(b, 0xCC, RELAY_BLOCK);
            memcpy(b, MAGIC, 8);
            const unsigned bb = (unsigned)(uintptr_t)b, tramp = bb + RELAY_OFF_TRAMP;
            memcpy(b + RELAY_OFF_SLOT, &tramp, 4);                              // a pass-through until it is switched on
            memcpy(b + RELAY_OFF_AT, &h.at, 4);
            b[RELAY_OFF_LEN] = (unsigned char)h.len;
            memcpy(b + RELAY_OFF_ORIG, cur, (size_t)h.len);
            b[RELAY_OFF_STUB] = 0xFF; b[RELAY_OFF_STUB + 1] = 0x25;             // jmp dword ptr [slot]
            const unsigned slotAddr = bb + RELAY_OFF_SLOT; memcpy(b + RELAY_OFF_STUB + 2, &slotAddr, 4);
            memcpy(b + RELAY_OFF_TRAMP, cur, (size_t)h.len);                   // the original bytes, then back
            b[RELAY_OFF_TRAMP + h.len] = 0xE9;
            const int back = (int)((h.at + (unsigned)h.len) - (tramp + (unsigned)h.len + 5)); memcpy(b + RELAY_OFF_TRAMP + h.len + 1, &back, 4);
            FlushInstructionCache(GetCurrentProcess(), b, RELAY_BLOCK);
            // the game's JMP -> the relay. Written ONCE, here, on the thread that installs (the game's own) ; never undone.
            unsigned char j[8]; j[0] = 0xE9;
            const int rel = (int)((bb + RELAY_OFF_STUB) - (h.at + 5)); memcpy(j + 1, &rel, 4);
            for (int k = 5; k < h.len; ++k) j[k] = 0x90;
            DWORD old = 0;
            if (!VirtualProtect((void*)(uintptr_t)h.at, (SIZE_T)h.len, PAGE_EXECUTE_READWRITE, &old)) {
                windower::debug::log("NATIVEUI %s : VirtualProtect failed (error %lu) -- NOT hooked", h.what, GetLastError());
                return false;                                                   // (the block stays : harmless, unreferenced)
            }
            memcpy((void*)(uintptr_t)h.at, j, (size_t)h.len);
            VirtualProtect((void*)(uintptr_t)h.at, (SIZE_T)h.len, old, &old);
            FlushInstructionCache(GetCurrentProcess(), (void*)(uintptr_t)h.at, (SIZE_T)h.len);
            memcpy(h.orig, cur, (size_t)h.len);
            h.blk = b; h.tramp = b + RELAY_OFF_TRAMP;
            registry_add(bb);                                                   // the next load must find it (relay_list)
            windower::debug::log("NATIVEUI %s hooked through a permanent relay (block %08X)", h.what, bb);
        }
    }
    InterlockedExchange(slot_of(h), (LONG)(uintptr_t)h.dst);
    h.on = true;
    return true;
}

bool relay_off(Hook& h) {
    if (!h.on || !h.blk) { h.on = false; return true; }
    InterlockedExchange(slot_of(h), (LONG)(uintptr_t)h.tramp);   // calls go straight to the original from now on
    h.on = false;
    return true;
}

int relay_wait_idle(unsigned maxMs) {
    // Wait for 0 ; then a short grace and a second look : a call that read the slot just before it flipped is a few
    // instructions from its relay_enter, and must be seen before the DLL is let go.
    const DWORD t0 = GetTickCount();
    for (int pass = 0; pass < 2; ++pass) {
        while (g_relayInflight > 0 && GetTickCount() - t0 < maxMs) Sleep(1);
        if (g_relayInflight > 0) break;
        Sleep(5);
    }
    return (int)g_relayInflight;
}

} // namespace aio
