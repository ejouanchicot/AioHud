// t_validptr.cpp -- the address sanity check every guarded read and every COM call goes through
// (`windower::valid_ptr`, include/windower.h).
//
// WHY THIS EXISTS. Its upper bound used to be the literal 0x80000000 -- "a 32-bit process owns 2 GB" -- which is
// simply not true of the process this plugin lives in: pol.exe is built LARGE_ADDRESS_AWARE, so on 64-bit Windows
// it owns nearly 4 GB. Everything the upper half handed out was therefore called invalid, and the plugin threw it
// away. Measured on 2026-09-17: dgVoodoo's D3D8 wrapper allocated texture objects up there, CreateTexture returned
// S_OK, and the gear icons fell back to the raw item id -- intermittently, decided by nothing but where the
// allocator happened to land. `vfn()` sits behind the same bound, so a COM object placed high had every one of its
// methods resolve to 0 and do nothing at all, silently.
//
// The invariant worth pinning is not a number -- a number is exactly what was wrong. It is that the bound is the
// OS's own answer, so this holds whatever process the suite itself runs as.
#include "check.h"
#include "windower.h"

using windower::valid_ptr;

void test_validptr() {
    SYSTEM_INFO si; GetSystemInfo(&si);
    const windower::u32 hi = (windower::u32)(uintptr_t)si.lpMaximumApplicationAddress;

    SECTION("valid_ptr : the upper bound is the OS's, not a literal");
    CHECK(valid_ptr(hi));                  // the last address this process may hold IS holdable
    CHECK(!valid_ptr(hi + 1));             // and the first one past it is not
    // The regression itself: on a large-address-aware process the old literal refused everything above 2 GB.
    // Where the OS grants that half, valid_ptr must grant it too -- and where it does not, it must still refuse.
    CHECK(valid_ptr(0x90000000u) == (hi >= 0x90000000u));

    SECTION("valid_ptr : the low reserved page stays out");
    CHECK(!valid_ptr(0));                  // a null read is the bug this check is mostly here to stop
    CHECK(!valid_ptr(0x10u));
    CHECK(!valid_ptr(0xFFFFu));            // the 64 KB no-access region at the bottom of every address space
    CHECK(valid_ptr(0x10000u));            // ...and the first byte past it is fine

    SECTION("valid_ptr : an ordinary address is accepted");
    CHECK(valid_ptr(0x10000000u));         // where FFXiMain.dll loads
    CHECK(valid_ptr(0x7FFE0000u));
}
