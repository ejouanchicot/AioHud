// hook_relay.h -- a JMP hook that is NEVER taken out of the game's code : switched by one atomic 4-byte write (audit R1).
//
// THE RACE IT REMOVES. A hook used to be a JMP from the client's function straight into this DLL, and //unload took it
// out by writing the function's first 5-8 bytes back -- from the unload thread, while the game's main thread could be
// executing them. A non-atomic memcpy over code another thread runs : the game could run half-restored bytes, or come
// back into a DLL already freed. The author's own use is the case at risk (143 unloads in two days of development), and
// Windows' event log held 8 identical crashes in Windower's Hook.dll, one right after an unload of AioHUD.
//
// THE RELAY. Each hooked function gets a small block of PERMANENT memory (VirtualAlloc, never freed) :
//   +0   "AIOHOOK1"                 the signature a later load recognises
//   +8   slot (4 bytes)             where the relay jumps : our handler, or the trampoline
//   +12  the hooked address         (so a block is only ever adopted for its own function)
//   +16  prologue length, +17 the original bytes
//   +32  the relay : FF 25 [slot]   (jmp dword ptr [slot])
//   +40  the trampoline : the original bytes, then a JMP back to the function's rest
// The game's JMP points at the relay, never at the DLL, and once written it stays for the life of the process.
// Switching a hook on or off -- the unload included -- is an InterlockedExchange of the slot (handler <-> trampoline) :
// no byte of the game's code is ever written from the unload thread. The next load finds the JMP, follows it to the
// block, checks the signature, and re-arms the same slot ("adopted") instead of hooking twice.
//
// IN FLIGHT. A call already inside our handler when the slot flips still has to leave this DLL before it is freed :
// handlers bracket themselves with relay_enter / relay_leave, and the shutdown waits (bounded) for the count to be 0.
//
// Model layer, no game knowledge : tests/t_hookrelay.cpp hooks a real x86 function it builds in executable memory.
#pragma once

namespace aio {

struct Hook {
    unsigned at;                   // the hooked function
    const unsigned char* pro;      // the prologue it must start with (whole instructions, no relative operand)
    int len;                       // 5..8 : how many bytes the JMP covers
    void* dst;                     // our handler
    unsigned char* tramp;          // call this to run the original (inside the block ; valid once on, and forever after)
    unsigned char orig[8];         // the original bytes
    bool on;                       // the slot points at dst
    const char* what;              // for the log
    unsigned char* blk;            // the permanent block (0 = none yet)
};

enum { RELAY_BLOCK = 64, RELAY_OFF_SLOT = 8, RELAY_OFF_AT = 12, RELAY_OFF_LEN = 16, RELAY_OFF_ORIG = 17,
       RELAY_OFF_STUB = 32, RELAY_OFF_TRAMP = 40 };

// Install (first time), adopt (a block left by a previous load of this DLL), then point the slot at h.dst.
// Refuses -- and says why -- a prologue that is not h.pro, an unreadable address, a failed allocation or protect.
bool relay_on(Hook& h);
// Point the slot back at the trampoline. Never writes the game's code ; always succeeds once a block exists.
bool relay_off(Hook& h);

// THE CLIENT'S OWN BYTES, as a signature search must see them. Our JMPs stay in the client's code for good, so a
// search for a hooked function's prologue no longer finds it after the first load -- measured 2026-11-24 : 50 reloads,
// the hooks armed on the first only ("move-to MISSING" 49 times). The relays a process holds are listed in its
// environment (AIOHUD_RELAYS : it outlives a DLL, dies with the game) ; each is re-validated by its signature.
struct RelayInfo { unsigned at; int len; unsigned char orig[8]; };
int relay_list(RelayInfo* out, int cap);
// A unique masked pattern in [lo, hi) as the client wrote it : where one of our relays covers the code, its original
// bytes are compared instead. `wild[i]` true = any byte. Returns the address, 0 when absent or not unique ; hits = matches.
unsigned relay_aware_find(unsigned lo, unsigned hi, const unsigned char* pat, const bool* wild, int n, int* hits = 0);

// The calls currently inside a handler (every handler brackets itself).
extern volatile long g_relayInflight;
inline void relay_enter();
inline void relay_leave();
// Wait until no call is inside a handler, at most maxMs ; returns how many still are (0 = clean).
int relay_wait_idle(unsigned maxMs);

} // namespace aio

#include <windows.h>
inline void aio::relay_enter() { InterlockedIncrement(&aio::g_relayInflight); }
inline void aio::relay_leave() { InterlockedDecrement(&aio::g_relayInflight); }
