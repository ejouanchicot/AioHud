// t_minidump.cpp -- //aio doctor reads which module a crash dump says the game died in (model/minidump.h).
//
// A dump is built here byte by byte in the MINIDUMP layout (header, stream directory, exception stream, module list,
// unloaded-module list), so the parser is held to the format itself, not to a sample file : a crash in a loaded module
// (Windower's Hook.dll), a crash in an UNLOADED one (AioHud.dll -- the trace audit R1 would leave), and one in no
// module at all ; then the files that must be refused.
#include "check.h"
#include "model/minidump.h"
#include <string.h>

using namespace aio;

namespace {

unsigned char g_dump[2048];
int g_size = 0;

void put32(int at, unsigned v) { memcpy(g_dump + at, &v, 4); }
void put64(int at, unsigned long long v) { memcpy(g_dump + at, &v, 8); }
void putname(int at, const char* s) {                       // MINIDUMP_STRING : byte length, then UTF-16
    const int n = (int)strlen(s);
    put32(at, (unsigned)(2 * n));
    for (int i = 0; i < n; ++i) { g_dump[at + 4 + 2 * i] = (unsigned char)s[i]; g_dump[at + 5 + 2 * i] = 0; }
}

void build(unsigned long long crashAt, bool withUnloaded = true) {
    memset(g_dump, 0, sizeof(g_dump));
    put32(0, 0x504D444Du); put32(8, withUnloaded ? 3 : 2); put32(12, 32);            // "MDMP", streams, directory
    put32(32, 6);  put32(36, 32); put32(40, 100);                                     // exception stream at 100
    put32(44, 4);  put32(48, 220); put32(52, 200);                                    // module list at 200
    if (withUnloaded) { put32(56, 14); put32(60, 36); put32(64, 800); }               // unloaded list at 800
    put32(100, 1234); put32(108, 0xC0000005u); put64(124, crashAt);                   // ThreadId, code, address
    put32(200, 2);                                                                    // two loaded modules
    put64(204, 0x00400000ull); put32(212, 0x10000); put32(224, 500);                  // pol.exe
    put64(204 + 108, 0x10000000ull); put32(212 + 108, 0x80000); put32(224 + 108, 560);   // Hook.dll
    putname(500, "D:\\SquareEnix\\PlayOnlineViewer\\pol.exe");
    putname(560, "D:\\Windower\\Hook.dll");
    if (withUnloaded) {
        put32(800, 12); put32(804, 24); put32(808, 1);                                // header size, entry size, count
        put64(812, 0x20000000ull); put32(820, 0x500000); put32(832, 900);             // AioHud.dll, unloaded
        putname(900, "D:\\Windower\\plugins\\AioHud.dll");
    }
    g_size = 1100;
}

bool rd(unsigned long long off, void* out, unsigned n) {
    if (off + n > (unsigned long long)g_size) return false;
    memcpy(out, g_dump + off, n); return true;
}

} // namespace

void test_minidump() {
    SECTION("minidump : the faulting module, loaded or already unloaded");
    {
        DumpCrash c;
        build(0x10041C62ull);                                   // the Hook.dll crash seen 8 times
        CHECK(minidump_crash(rd, c));
        CHECK(c.ok); CHECK_EQ(c.code, 0xC0000005u);
        CHECK_STR(c.module, "Hook.dll");
        CHECK_EQ(c.offset, 0x41C62);
        CHECK(!c.unloaded);

        build(0x20012345ull);                                   // a JMP into our DLL after it was unloaded : R1
        CHECK(minidump_crash(rd, c));
        CHECK_STR(c.module, "AioHud.dll_unloaded");
        CHECK(c.unloaded);
        CHECK_EQ(c.offset, 0x12345);

        build(0x7FFE0000ull);                                   // nowhere known : located, no module
        CHECK(minidump_crash(rd, c));
        CHECK(c.ok); CHECK_STR(c.module, "");

        build(0x20012345ull, false);                            // no unloaded-module stream : not invented
        CHECK(minidump_crash(rd, c));
        CHECK_STR(c.module, "");
    }

    SECTION("minidump : what is not a readable dump is refused, not guessed");
    {
        DumpCrash c;
        build(0x10041C62ull); g_dump[0] = 'X';                  // not "MDMP"
        CHECK(!minidump_crash(rd, c));
        build(0x10041C62ull); g_size = 110;                     // cut inside the exception stream
        CHECK(!minidump_crash(rd, c));
        CHECK(!c.ok);
        build(0x10041C62ull); put32(44, 0);                     // no exception stream at all (a dump taken by hand)
        put32(32, 0);
        CHECK(!minidump_crash(rd, c));
    }
}
