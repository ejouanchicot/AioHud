# scripts/ghidra/

Ghidra **headless** scripts for static analysis of `FFXiMain`. They answer questions the running game cannot:
what does the client's own parser do with this packet, which function writes this field, is this offset really
what an addon claims.

They moved out of `scripts/` because they share nothing with the generators above — different tool, different
inputs, different workflow — and 32 Java files buried the 23 Python ones.

## Running one

```
analyzeHeadless <projDir> <projName> -process ffximain_dump.bin -noanalysis \
    -scriptPath scripts/ghidra -postScript <Script>.java <args>
```

Setup, the imported project and the image base are in
[`../../docs/reverse-engineering/ghidra-setup.md`](../../docs/reverse-engineering/ghidra-setup.md).

> **The output is `INFO`-prefixed.** Filtering those lines out discards every result and looks exactly like an
> empty query. Grep for the script's own tag instead — `... | grep "FindStr.java>"`.

> **The dump's image base is `0x05C60000`, not `0x10000000`.** An address computed against the wrong base
> decompiles to plausible-looking garbage rather than failing, which is the expensive way to find out.

## What is in here

| Family | Scripts | Use |
|---|---|---|
| Decompile | `DecompOne` · `DecompMany` · `DecompForce` · `DecompList` · `DecompContain` | Get C for a function by address — the workhorse |
| Find | `FindAOB` · `FindStr` · `FindSym` · `FindCallers` · `FindDisp` · `FindImmRange` · `FindJmpTables` · `FindLoader` · `FindPtrTable` · `FindRender` · `FindVtableCalls` | Locate a byte pattern, string, symbol or reference |
| Dump | `DumpStrings` · `DumpFuncs` · `DumpTable` · `DumpVtable` · `DumpClass` · `Bytes` | Bulk-extract a region or structure |
| Disassemble | `DisasmFn` | Raw instructions when the decompiler misleads |
| Misc | `CoreFlag` · `DiagRefs` | One-off diagnostics kept because re-writing them costs more than storing them |

## See also

- [`../../docs/reverse-engineering/recipe.md`](../../docs/reverse-engineering/recipe.md) — **read this first**: the three
  RE channels in cost order. Static analysis is channel 2; reaching for it when channel 1 would do is the
  usual way an offset hunt takes a night instead of twenty minutes.
- [`../README.md`](../README.md) — the generators these findings eventually feed
