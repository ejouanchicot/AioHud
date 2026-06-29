// windower_plugin.h -- IPlugin ABI scaffolding for a Windower 4 plugin.
// Reverse-engineered from FFXIDB.dll / Timers.dll + Hook.dll (hook 4.7.9.0).
//
// A Windower plugin DLL must export `CreateInstance` (returns an IPlugin*) and
// `GetInterfaceVersion` (must return 0x04070300). The IPlugin object's first
// member is a vtable of 34 methods, all __stdcall with `this` passed as the
// first stack argument. The per-slot stack-dword count (incl. self) is fixed by
// the ABI and encoded below -- getting it wrong corrupts the stack.
//
// Slots we understand / use:
//   [0] GetName()->char*   [1] GetDescription()->char*   [2] Init(host)
//   [4] IsCore()->bool (MUST return 0 or //unload is refused)   [7] HandleCommand(char*)
// Everything else is a safe no-op returning 0.
//
// IMPORTANT: include this header in EXACTLY ONE .cpp (it defines the vtable +
// the exported entry points). The host .cpp must implement the four aio_* hooks.
#pragma once
#include "windower.h"

// ===== implement these in your plugin .cpp =====
extern const char* aio_plugin_name();
extern const char* aio_plugin_description();
extern void        aio_plugin_init(windower::PluginManager host);
extern void        aio_plugin_render();        // slot 5, per-frame (FFXIDB uses it for UPDATE)
extern void        aio_plugin_render6();       // slot 6, per-frame (FFXIDB uses it for DRAW)
extern void        aio_plugin_unload();        // slot 3, called once at //unload (clean up your objects!)
extern void        aio_plugin_command(const char* cmd);
extern void        aio_plugin_packet_in(unsigned int a, unsigned int b, unsigned int c, unsigned int d); // slot 11
extern unsigned int aio_plugin_mouse(unsigned int eventtype, unsigned int x, unsigned int y, unsigned int delta, unsigned int blocked); // slot 13 ; return 1 to CONSUME
extern unsigned int aio_plugin_key(unsigned int a, unsigned int b, unsigned int c);   // slot 14 KEYBOARD ; return 1 to CONSUME (block the game)

namespace windower { namespace detail {

typedef void* P;   // the (ignored) `this` passed as first stack arg

// stackargs per slot (incl. self), measured from FFXIDB:
// 1 1 2 1 1 1 1 2 2 4 4 5 5 6 4 5 5 1 1 0 0 0 0 1 1 3 3 4 4 5 3 4 4 0
static u32 __stdcall m00(P) { return (u32)(const void*)aio_plugin_name(); }        // GetName
static u32 __stdcall m01(P) { return (u32)(const void*)aio_plugin_description(); } // GetDescription
static u32 __stdcall m02(P, u32 host) { aio_plugin_init(PluginManager(host)); return 0; }  // Init
static u32 __stdcall m03(P) { aio_plugin_unload(); return 0; }                     // OnUnload (cleanup)
static u32 __stdcall m04(P) { return 0; }                                          // IsCore -> not core
static u32 __stdcall m05(P) { aio_plugin_render(); return 0; }                      // per-frame (update)
static u32 __stdcall m06(P) { aio_plugin_render6(); return 0; }                     // per-frame (draw)
static u32 __stdcall m07(P, char* c) { aio_plugin_command(c); return 0; }          // HandleCommand
static u32 __stdcall m08(P, u32) { return 0; }
static u32 __stdcall m09(P, u32, u32, u32) { return 0; }
static u32 __stdcall m10(P, u32, u32, u32) { return 0; }
static u32 __stdcall m11(P, u32 a, u32 b, u32 c, u32 d) { aio_plugin_packet_in(a, b, c, d); return 0; } // packet in
static u32 __stdcall m12(P, u32, u32, u32, u32) { return 0; }                       // packet out
static u32 __stdcall m13(P, u32 et, u32 x, u32 y, u32 d, u32 bl) { return aio_plugin_mouse(et, x, y, d, bl); }  // MOUSE (eventtype,x,y,delta,blocked)
static u32 __stdcall m14(P, u32 a, u32 b, u32 c) { return aio_plugin_key(a, b, c); }  // KEYBOARD (key, down, blocked) ; return 1 to consume
static u32 __stdcall m15(P, u32, u32, u32, u32) { return 0; }
static u32 __stdcall m16(P, u32, u32, u32, u32) { return 0; }
static u32 __stdcall m17(P) { return 0; }
static u32 __stdcall m18(P) { return 0; }
static u32 __stdcall m19() { return 0; }   // 0 stack args
static u32 __stdcall m20() { return 0; }
static u32 __stdcall m21() { return 0; }
static u32 __stdcall m22() { return 0; }
static u32 __stdcall m23(P) { return 0; }
static u32 __stdcall m24(P) { return 0; }
static u32 __stdcall m25(P, u32, u32) { return 0; }
static u32 __stdcall m26(P, u32, u32) { return 0; }
static u32 __stdcall m27(P, u32, u32, u32) { return 0; }
static u32 __stdcall m28(P, u32, u32, u32) { return 0; }
static u32 __stdcall m29(P, u32, u32, u32, u32) { return 0; }
static u32 __stdcall m30(P, u32, u32) { return 0; }
static u32 __stdcall m31(P, u32, u32, u32) { return 0; }
static u32 __stdcall m32(P, u32, u32, u32) { return 0; }   // scalar deleting destructor
static u32 __stdcall m33() { return 0; }

static void* g_vtbl[34] = {
    m00,m01,m02,m03,m04,m05,m06,m07,m08,m09,m10,m11,m12,m13,m14,m15,m16,
    m17,m18,m19,m20,m21,m22,m23,m24,m25,m26,m27,m28,m29,m30,m31,m32,m33
};
struct Object { void** vtbl; };
static Object g_object = { g_vtbl };

}} // namespace windower::detail

extern "C" __declspec(dllexport) void* CreateInstance() { return &windower::detail::g_object; }
extern "C" __declspec(dllexport) unsigned int GetInterfaceVersion() { return windower::PLUGIN_INTERFACE_VERSION; }
