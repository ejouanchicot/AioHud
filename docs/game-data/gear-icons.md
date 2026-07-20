---
title: Gear Icons — the Equipment Viewer's Item Icons
summary: How the equipment viewer gets a 32x32 icon per item id — a bundled BMP seed, else a live decode from the game's ROM DAT files, else the raw id as text.
source: Windower addons/equipviewer (2021, unchanged) + AioHUD gfx load_bmp_texture
---
# Gear icons — the equipment viewer's item icons

Item icons are **32x32**. Two ways to obtain one per item id: the original addon decodes it
live from the game's ROM DAT files; AioHUD bundles the pre-extracted result. Both key off the
16 equipped item ids from [player-equipment](player-equipment.md).

## AioHUD's approach (current): bundled BMPs + a live ROM decode

Three tiers, in order, per equip slot (`src/ui/player.cpp`):

1. **Bundled BMP** — 1323 pre-extracted gearicons under `assets/gearicons/` (32x32 BITMAPV4,
   straight BGRA), named `<item_id>.bmp`, loaded via `gfx load_bmp_texture` (`src/gfx/texture.cpp`):
   BITMAPV4 parse + vertical flip (BMP is bottom-up), file BGRA maps straight onto `A8R8G8B8`.
2. **Live ROM-DAT decode** — `decode_gear_icon_from_rom()` (the port described below), so any item
   resolves without a pre-populated bundle.
3. **Item-id text** (one font pass) — only when the icon is genuinely unreachable.

**Cached per equip slot** (`gearTex_[16]` / `gearId_[16]`); a slot's texture is only (re)loaded when
its equipped item id changes, and at most 2 ROM decodes run per frame (the decode is synchronous file
IO + a mip-chain build, on the render thread).

> **The bundle is one player's history, not a curated set.** `assets/gearicons/` is the dev's own
> EquipViewer cache, so its coverage mirrors a single player's jobs and gear. On the dev's machine
> nearly every equipped item is a cache hit and tier 2 almost never runs; for another player tier 2 is
> the routine path. Never judge this code's exposure from a dev session.

### The decode must not depend on writing (fixed in v1.0.33)

`decode_gear_icon_from_rom()` returns the pixels **in memory** (32x32 ARGB, top-down) and never touches
the disk. Caching is a separate, best-effort `write_gear_icon_bmp()` whose failure the caller **ignores**.

It used to write the BMP itself and return false if that write failed — throwing away a finished decode
and showing the raw item id. Writes fail on a write-protected plugin folder (FFXI under Program Files
with a non-elevated token, Controlled Folder Access, an AV hold), which is why this hit testers and never
the dev. The cache write is now atomic (temp + `MoveFileEx`, `fwrite` **and** `fclose` checked): a
half-written BMP used to poison a slot permanently *and on disk*, because the "file exists" gates skipped
both the re-decode and the give-up. The load path no longer keys on existence, so such a file self-repairs.

`//aio geartrace` logs the whole per-slot chain (bundled hit/miss, id-range -> DAT, registry -> ROM dir,
decode, texture, cache write) to `aiohud_debug.log`. Deliberately **not** behind `AIOHUD_PROBES`, so a
tester's release build can produce the capture — every other `//aio` probe lives in the untracked
`aiohud_probes.cpp` and is therefore absent from a release ([release checklist](../architecture/release-checklist.md)).

**Verified by reproducing the tester's conditions** (2026-07-19), with an `icacls` deny on the gearicons
folder: the trace read `DECODE OK / TEX OK / CACHE FAILED errno=13 EACCES / RESULT ICON DRAWN` — the icon
renders, only the cache is lost. And with the folder writable again, the re-fabricated BMPs are
**SHA256-identical** to the bundled originals, which pins the palette decode, the doubled alpha and the
vertical flip all three at once.

> **`deploy.bat` re-seeds `assets/gearicons/` from the repo.** Any test that needs an icon to be
> *missing* is undone by the next deploy — delete the icons after deploying, not before.

## The original EquipViewer's approach (ROM DAT decode)

Windower `addons/equipviewer` (unchanged since 2021) reads icons straight from the game's ROM
DAT files on disk:

1. **item id -> DAT** by id-range, e.g. general `118/106`, usable `118/107`, weapons `118/108`,
   armor `118/109`, general2 `301/115`, armor2 `286/73`, ...
2. `seek((id - id_offset) * 0xC00 + 0x2BD)`, read `0x800` bytes = one 256-colour palettized
   32x32 icon: `0x400` bit-rotated BGRA palette + `0x400` pixel indices (alpha channel is
   **doubled + clamped**).
3. **Cache** the decoded icon to `assets/gearicons/<id>.bmp` (32x32 BITMAPV4, straight BGRA);
   only re-read the ROM on a **cache miss**.

The game path comes from the registry (PlayOnline `InstallFolder`). Three HKLM keys are probed
(`PlayOnlineUS` / `PlayOnline` / `PlayOnlineEU`, value `"0001"`); a **Steam install registers under the
same keys**, so no Steam-specific path is needed.

### The id-range table is verified complete (2026-07-19)

Checked against the DAT files themselves rather than inferred, which is possible because **each record
embeds its own item id**:

- every DAT is exactly the size of its declared id range at a 0xC00 stride (remainder 0 in all 11 cases);
- `record[(id - idOff)].id == id` for **~30,000 ids, 0 mismatches** — so there is **no icon-id/item-id
  indirection**, the raw item id indexes the DAT directly;
- of the 23,512 items in `itemnames_gen.h`, **none falls outside a range** (Limbus/Odyssey/Sortie gear
  lives in 0x4000-0x59FF and 0x5A00-0x6FFF, both covered);
- `0x2BD` is structural, not magic: the record holds an icon block at +0x280 whose 40-byte
  `BITMAPINFOHEADER` starts at +0x295, so pixels begin at +0x295+40. Identical in every DAT, no ROM2..9
  variance.

Other ROM files with the same 0xC00 stride are **not** icon DATs (their +0x280 is zeroed — they are help
text), or are the JP/DE/FR language variants of the files already used.

**So an item rendering as a raw id is never a gap in this table** — look at the decode's environment
(registry, ROM dir, folder writability) via `//aio geartrace` instead.

### Why frozen 2021 code still finds new items' icons

The ROM DATs are **patched by Square Enix every game update** (verified: the icon DATs on disk
are dated **2026**), and the extraction is **id-indexed** — the same formula reads the current
DAT and finds any new item's icon. The *code* is frozen; the *data* it reads is live. AioHUD's
bundle is exactly this decoder's cache, pre-run.

## See also
- [Player equipment](player-equipment.md) — the 16 item ids these icons render.
- [Player Equipment Viewer](../design/player-equipment-viewer.md) — the 4x4 grid UI these icons fill (placement, config, edit).
- [Encumbrance flags](encumbrance-flags.md) — the red cross overlaid on locked slots.
- [Release checklist](../architecture/release-checklist.md) — why `//aio geartrace` ships and the other probes don't, and the `deploy.bat` re-seed trap.
- [The in-game updater and file locks](../architecture/updater-and-file-locks.md) — the share mask `load_bmp_texture` uses for these BMPs.
