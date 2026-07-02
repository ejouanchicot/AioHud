---
title: Tooling — Ghidra & Python
summary: How AioHUD uses headless Ghidra to decompile LuaCore functions for struct offsets and Python + PIL for deterministic atlas regeneration.
source: TECH_STACK.md §11
---
# Tooling — Ghidra & Python

**Ghidra (RE).** Headless decompile of a specific `LuaCore` function to read field accesses off the
game struct:
```
analyzeHeadless <proj> aiohud_re -process LuaCore.dll -noanalysis \
    -scriptPath scripts -postScript DecompOne.java <FUN addr-without-0x>
```
Use Ghidra for **structure** (which `g+offset`), the live probe for **which field**. Create structs in
Ghidra's editor to name offsets as you confirm them.

**Python + PIL (assets).** Atlas assembly/regeneration (job icons, buffs). Deterministic, re-runnable;
the `.raw` output is committed, the script is the source of truth.

**References.**
[Ghidra 101: creating structures (Tripwire)](https://www.tripwire.com/state-of-security/ghidra-101-creating-structures-in-ghidra) ·
[Intro to decompiling C++ with Ghidra (RetroReversing)](https://www.retroreversing.com/intro-decompiling-with-ghidra) ·
[Ghidra headless docs (project README/analyzeHeadless)](https://github.com/NationalSecurityAgency/ghidra) ·
[Pillow (PIL) docs](https://pillow.readthedocs.io/)

## See also
- [Game data — RE'd memory reads + packet hooks](game-data-io.md)
- [Textures — raw BGRA](textures.md)
- [Reverse-engineering recipe](../architecture/reverse-engineering-recipe.md)
- [Debug and mapping](../reference/debug-and-mapping.md)
