// t_hookrelay.cpp -- the R1 relay (model/hook_relay.h), on REAL x86 code.
//
// A "game function" is assembled into executable memory (mov eax, 1 ; ret), hooked through the relay and CALLED :
// on, off, a second load adopting the first one's relay, a refused prologue. Then the race R1 is about : one thread
// calls the function as fast as it can while the main thread switches the hook on and off thousands of times. With the
// old byte-restoring hook_off that is a thread running half-written instructions ; with the relay every call must
// return one of the two valid answers. Last, the in-flight count the shutdown waits on.
#include "check.h"
#include "model/hook_relay.h"
#include <windows.h>
#include <string.h>

using namespace aio;

namespace {

typedef int (__cdecl* Fn)();
unsigned char* g_code = 0;
Hook* g_cur = 0;                                             // the hook whose trampoline the handlers call

int __cdecl handler_a() { relay_enter(); const int r = ((Fn)g_cur->tramp)() + 10;  relay_leave(); return r; }
int __cdecl handler_b() { relay_enter(); const int r = ((Fn)g_cur->tramp)() + 100; relay_leave(); return r; }
volatile long g_holdMs = 0;
int __cdecl handler_slow() { relay_enter(); Sleep((DWORD)g_holdMs); const int r = ((Fn)g_cur->tramp)(); relay_leave(); return r; }

const unsigned char PRO[5] = { 0xB8, 0x01, 0x00, 0x00, 0x00 };   // mov eax, 1

Fn game_fn() { return (Fn)(void*)g_code; }

volatile long g_stop = 0, g_bad = 0, g_calls = 0;
DWORD WINAPI caller(void*) {
    while (!g_stop) { const int r = game_fn()(); if (r != 1 && r != 101) InterlockedIncrement(&g_bad); InterlockedIncrement(&g_calls); }
    return 0;
}
DWORD WINAPI one_call(void*) { game_fn()(); return 0; }

} // namespace

void test_hookrelay() {
    g_code = (unsigned char*)VirtualAlloc(0, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    CHECK(g_code != 0);
    if (!g_code) return;
    memcpy(g_code, PRO, 5); g_code[5] = 0xC3;                    // mov eax, 1 ; ret
    const unsigned at = (unsigned)(uintptr_t)g_code;

    SECTION("hook relay : on runs our handler, off runs the original -- and the game's JMP is never written back");
    Hook h = { at, PRO, 5, (void*)&handler_a, 0, {0}, false, "test fn", 0 };
    g_cur = &h;
    CHECK_EQ(game_fn()(), 1);
    CHECK(relay_on(h));
    CHECK(h.blk != 0);
    CHECK_EQ(game_fn()(), 11);
    CHECK_EQ(g_code[0], 0xE9);
    CHECK(relay_off(h));
    CHECK_EQ(game_fn()(), 1);                                    // the original, through the trampoline...
    CHECK_EQ(g_code[0], 0xE9);                                   // ...the JMP stays : nothing was restored
    CHECK(relay_on(h));
    CHECK_EQ(game_fn()(), 11);
    relay_off(h);

    SECTION("hook relay : a second load ADOPTS the relay the first one left (no second hook)");
    Hook h2 = { at, PRO, 5, (void*)&handler_b, 0, {0}, false, "test fn (reload)", 0 };
    g_cur = &h2;
    CHECK(relay_on(h2));
    CHECK(h2.blk == h.blk);                                      // the same block, found through the JMP
    CHECK_EQ(memcmp(h2.orig, PRO, 5), 0);
    CHECK_EQ(game_fn()(), 101);
    relay_off(h2);
    CHECK_EQ(game_fn()(), 1);

    SECTION("hook relay : a signature search still finds a function our relay covers (the in-game 50-reload failure)");
    {
        // The JMP stays in the code for good : searched raw, the hooked prologue is gone -- the second load found
        // nothing ("move-to MISSING" 49 times in 50 reloads, 2026-11-24). Searched relay-aware, it is where it was.
        CHECK_EQ(g_code[0], 0xE9);
        RelayInfo R[16]; const int nr = relay_list(R, 16);
        bool listed = false; for (int i = 0; i < nr; ++i) if (R[i].at == at && R[i].len == 5 && !memcmp(R[i].orig, PRO, 5)) listed = true;
        CHECK(listed);                                           // registered by the install, read back from the environment
        const unsigned char pat[6] = { 0xB8, 0x01, 0x00, 0x00, 0x00, 0xC3 };   // the prologue + the byte after it
        const bool none[6] = { false, false, false, false, false, false };
        int hits = 0;
        CHECK_EQ(relay_aware_find(at, at + 4096, pat, none, 6, &hits), at);
        CHECK_EQ(hits, 1);
        const unsigned char straddle[3] = { 0x00, 0x00, 0xC3 };  // a pattern that STARTS inside the relayed bytes
        CHECK_EQ(relay_aware_find(at, at + 4096, straddle, none, 3, &hits), at + 3);
        const bool w2[6] = { false, true, true, true, true, false };            // wildcards work there too
        CHECK_EQ(relay_aware_find(at, at + 4096, pat, w2, 6, &hits), at);
        const unsigned char raw[2] = { 0xE9, 0x00 };             // and the JMP itself is NOT what the client had
        const bool w1[2] = { false, true };
        CHECK_EQ(relay_aware_find(at, at + 8, raw, w1, 2, &hits), 0u);
    }

    SECTION("hook relay : what is not ours is refused, not hooked");
    {
        unsigned char* other = g_code + 64;
        const unsigned char NOPE[5] = { 0x55, 0x8B, 0xEC, 0x90, 0x90 };   // push ebp ; mov ebp, esp ...
        memcpy(other, NOPE, 5); other[5] = 0xC3;
        Hook bad = { (unsigned)(uintptr_t)other, PRO, 5, (void*)&handler_a, 0, {0}, false, "wrong prologue", 0 };
        CHECK(!relay_on(bad));
        CHECK_EQ(other[0], 0x55);                                // untouched
        Hook shortH = { at, PRO, 4, (void*)&handler_a, 0, {0}, false, "too short", 0 };
        CHECK(!relay_on(shortH));
    }

    SECTION("hook relay : R1 -- the hook switched 20 000 times while another thread calls the function");
    {
        g_cur = &h2; g_stop = 0; g_bad = 0; g_calls = 0;
        HANDLE t = CreateThread(0, 0, caller, 0, 0, 0);
        for (int k = 0; k < 20000; ++k) { relay_on(h2); relay_off(h2); }
        g_stop = 1; WaitForSingleObject(t, 5000); CloseHandle(t);
        CHECK(g_calls > 0);
        CHECK_EQ(g_bad, 0);                                      // every call : the original (1) or ours (101), never garbage
        CHECK_EQ(g_relayInflight, 0);
    }

    SECTION("hook relay : the shutdown waits for a call still inside a handler -- and only so long");
    {
        Hook hs = h2; hs.dst = (void*)&handler_slow; hs.on = false; g_cur = &hs;
        CHECK(relay_on(hs));
        g_holdMs = 60;
        HANDLE t = CreateThread(0, 0, one_call, 0, 0, 0);
        Sleep(20);                                               // it is inside the handler now
        CHECK_EQ(g_relayInflight, 1);
        relay_off(hs);                                           // what the unload does first
        CHECK_EQ(relay_wait_idle(200), 0);                       // ...then it waits : the call has left
        WaitForSingleObject(t, 2000); CloseHandle(t);
        relay_on(hs); g_holdMs = 400;
        t = CreateThread(0, 0, one_call, 0, 0, 0);
        Sleep(20);
        relay_off(hs);
        const DWORD t0 = GetTickCount();
        CHECK_EQ(relay_wait_idle(100), 1);                       // bounded : it says one is still in flight...
        CHECK(GetTickCount() - t0 < 300);                        // ...instead of waiting for ever
        WaitForSingleObject(t, 2000); CloseHandle(t);
        CHECK_EQ(g_relayInflight, 0);
        CHECK_EQ(game_fn()(), 1);
    }
    // the relay block stays allocated by design (a game JMP points at it for the life of the process) ; so does g_code
}
