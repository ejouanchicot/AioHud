---
title: C++ (32-bit, MSVC)
summary: Why AioHUD is a 32-bit C++ DLL, how it is built with cl, and the low-level, no-heap-in-hot-path style plus CRT boundary caveats.
source: TECH_STACK.md §2
---
# C++ (32-bit, MSVC)

**Why.** FFXI + Windower are a 32-bit, C-ABI world; a compiled DLL is the only way to hook the
render loop and read memory at frame cost. We keep the style low-level (plain structs, fixed-capacity
arrays, no heap in the hot path) so a frame never allocates.

**How we use it.**
- One DLL, built by `build.bat` (`cl` /LD, 32-bit). **The build fails on a non-zero `cl` exit** —
  keep that check; a silent stale DLL cost us real debugging time (see [build / deploy](../architecture/build-deploy.md)).
- Deploy = `//unload AioTest` (Windower file-locks the loaded DLL) → `deploy.bat` → `//load`.

**Best-practices we follow.**
- No per-frame allocation; fixed arrays (`anim_[8]`, `guideGroup[GUIDE_GROUPS_MAX]`).
- One source of truth for every magic offset (the poller / `read_member`), never duplicated.
- Verify the DLL timestamp actually moved after a build.
- **CRT caveat:** a DLL linked to the *static* CRT (`/MT`) carries its own CRT state; don't pass CRT
  objects (allocations, FILE*) across the DLL boundary. We keep our surface C-ABI and self-contained.

**References.**
[CRT library features / `/MT` vs `/MD` (MS Learn)](https://learn.microsoft.com/en-us/cpp/c-runtime-library/crt-library-features) ·
[DLLs and VC++ runtime behavior (MS Learn)](https://learn.microsoft.com/en-us/cpp/build/run-time-library-behavior) ·
[Link with the correct CRT (KB Q140584)](https://jeffpar.github.io/kbarchive/kb/140/Q140584/) ·
[Build a 32-bit DLL with MSVC (MathWorks)](https://www.mathworks.com/help/coder/ug/build-32-bit-dll-on-64-bit-windows-platform-using-msvc-toolchain.html)

## See also
- [Windower 4 (IPlugin ABI)](windower-abi.md)
- [Tooling — Ghidra & Python](tooling.md)
- [Build & deploy](../architecture/build-deploy.md)
- [Workflow gotchas](../reference/workflow-gotchas.md)
