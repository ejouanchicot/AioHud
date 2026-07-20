---
title: Limbus currencies have no static — a negative result
summary: Full Ghidra trace of the 0x118 currency2 handler: the Temenos/Apollyon unit totals are written into Currency-menu ROWS, never into a global, and the menu array is unusable — stop looking for an address.
source: Ghidra session on re/ffximain_dump.bin, 2026-07-19
---
# Limbus currencies have no static — a negative result

**Written down so it is not re-dug.** The question was: can the Temenos / Apollyon unit totals be
read from memory, so the HUD does not depend on a 0x118 packet that never fires mid-run?

**Answer: no.** There is no global holding them.

## What the 0x118 handler actually does

Traced end to end in Ghidra on `re/ffximain_dump.bin`. The handler does not store the values
anywhere durable — it writes them into the **rows of the Currency menu object**:

```
*(*(*(FFXiMain+0x6346B4) + 0x20) + row*8)
   row 95 = Temenos Units
   row 96 = Apollyon Units
guards:  *(obj+0x44) == 0x61  &&  *(obj+0x1C) == FFXiMain+0x229C70
```

Unusable in practice, for three independent reasons:

1. **0x118 only answers an outgoing 0x115**, which the client emits when you *open* the Currency
   menu. It does not fire on a schedule and it does not fire mid-run.
2. The row array is **shared with currency1 (0x113)** — the same storage serves both pages.
3. It is **reallocated**, so any address found is good until the next menu open.

The ~17 blocks a memory scan finds that contain the right numbers are **raw copies of the packet**,
not a decoded field anyone can key on.

## Consequence for the tracker

The HUD keeps the totals from the **0x02A award payload** (`p3` = your total, `p4` = your cap) and
treats 0x118 as a zone-entry seed only (`on_118`, `party_state_zonetracker.cpp:445-451`:
`+0x98` Temenos, `+0x9C` Apollyon, the same packet as Mog Segments `+0x8C`). Since the 0x02A award
fires only a handful of times per run, the displayed total is coarse **by construction** — that is
the ceiling, not a bug to fix.

## Methodological note — the on-disk FFXiMain is PACKED

`FFXiMain.dll` on disk is **2.9 MB**; the same module in memory is **12.4 MB**. Static analysis of
anything in it **must** use the runtime dump `re/ffximain_dump.bin`, image base **0x05C60000** —
analysing the on-disk file finds neither the handler nor the strings.

## See also
- [Limbus tracker](limbus.md) — where the totals actually come from.
- [Zone Tracker](zone-tracker.md) — `on_118` and the other providers that ride currency2.
- [Static analysis of FFXiMain](../architecture/re-static-analysis.md) — the dump, its base, and how to re-pin it.
- [Ghidra setup](../architecture/ghidra-setup.md) — which of the three FFXiMain images is usable.
- [The differential memory scanner](../architecture/re-memory-scanner.md) — why the ~17 "hits" were packet copies.
