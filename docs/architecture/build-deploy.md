---
title: Build / deploy / iterate
summary: The build.bat -> deploy.bat -> load cycle, the file-lock and stale-DLL gotchas, and the in-game demo mode.
source: ARCHITECTURE.md §6
---
# Build / deploy / iterate

```
build.bat     -> build\AioTest.dll   (32-bit MSVC ; MUST regen the DLL — see gotcha)
# in game:  //unload AioTest
deploy.bat    -> copies into plugins\AioTest.dll
# in game:  //load AioTest
```
- Windower **file-locks** the loaded DLL → always `//unload` before `deploy`.
- **GOTCHA (cost us real time):** `build.bat` now fails on `cl`'s nonzero exit. Before, it only
  checked the DLL *existed*, so a failed compile silently kept the **stale** DLL and reported
  `[build] OK`. If you change the build, keep that exit-code check — and verify the DLL timestamp
  actually moved after a build.
- Demo without the game: `//aio party demo` (+ `alliance1|alliance2 demo`) drives the layout from
  baked rows so you can tune visuals out of game.

## See also
- [Notable conventions](conventions.md)
- [Widgets](widgets.md)
- [Workflow gotchas](../reference/workflow-gotchas.md)
- [C++ and build](../tech-stack/cpp-and-build.md)
- [Tooling](../tech-stack/tooling.md)
