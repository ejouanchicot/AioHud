---
title: The status-icon DAT (ROM/119/57.DAT, file id 87)
summary: The game's own 640 status icons — record layout, the two alpha conventions, and why the HUD prefers an installed icon pack over both the vanilla art and its own bundled sheet.
---

# The status-icon DAT

Reversed 2026-09-06 from a live install (Steam/EU client) plus an "IconsHD" XIPivot pack. Code:
`model/icon_dat.{h,cpp}` (decode), `ui/buff_atlas.cpp` (which source wins), `tools/aioicons.ps1` (the
player-facing converter).

**File id 87** — resolved through VTABLE/FTABLE like any other DAT (`dat_resolve_path`, `model/map_dat.cpp`),
never hardcoded as a path, so an XIPivot overlay takes it over exactly the way the game does. On this client
it lands at `ROM/119/57.DAT`.

## Record layout

The whole file is **640 fixed records of 6144 bytes** (0x1800) and nothing else — `640 × 6144 = 3 932 160`,
the exact file size. Record `k` is status id `k`, which is why the HUD atlas is a 32×20 grid of 32px cells:
that grid *is* this file's layout, so the id→cell mapping is an identity, not a table.

| Offset in record | What |
|---|---|
| `+0x285` | `char[16]` name, plain ASCII — `"sts_iconst00_32 "`. **The real magic**: a file of the right size with the wrong content decodes into 640 cells of noise, and only the name catches that. |
| `+0x295` | a plain `BITMAPINFOHEADER` — biSize 40, width 32, height 32, 1 plane, 32 or 8 bpp |
| `+0x2BD` | the pixel body (`= 0x295 + 40`) |

The body is a normal DIB: **BGRA, bottom-up**. Not obfuscated — unlike the *gear*-icon DATs, which are
palettised *and* `rotl3`-encoded (`gfx/texture.cpp :: decode_gear_icon_from_rom`). The rest of each record is
padding.

### Two pixel formats in the same file

Most records are 32 bpp. A pristine game sheet also holds **42 palettised 8-bpp records** — 256 BGRA palette
entries at `+0x2BD`, then 32×32 index bytes at `+0x6BD`. They are not obscure ones: all eight RUN runes, the
GEO and SCH icons, Ergon, Colonization Reive, `sts_iconrelim_32`… Since a single unreadable record fails the
whole file (below), refusing 8 bpp threw away the entire game sheet — so both depths are decoded.

(The live install on this machine has only *one* 8-bpp record left, the other 41 having been overwritten with
32-bpp art by a pack. One more reason the pristine copy is the reference.)

### Two alpha conventions, decided per record

The client's own art uses a **7-bit** alpha (0..0x80 = fully opaque); art drawn in an image editor uses the
full 0..255. Measured on three files:

| File | 7-bit records |
|---|---|
| a **pristine** copy of the game's sheet | **640 of 640** — uniformly the old convention |
| an "IconsHD" XIPivot pack | 0 of 640 — redrawn end to end |
| this machine's live `ROM/119/57.DAT` | 18 of 640 — a pack copied over the game files, 18 originals left |

So a per-*file* answer is only ever right for the first two. A file whose icons have been **partly** replaced
holds both conventions at once, and then doubling everything hardens every antialiased edge of the new art
while doubling nothing draws the originals at half opacity, as ghosts. **The record says which it is**: one
whose alpha never exceeds 0x80 is 7-bit. For a palettised record the convention lives in the *palette*, so
that is what gets scanned and scaled.

> The "18 of 640" line above is also a caution about method. That mixed state was first taken for the game's
> own, until a pristine copy (`FFXI Icon Editor/reference/game-files/`) showed the original is uniform — the
> live file had simply been written to. A file sitting in an install is not evidence of what shipped.

## Which sheet the HUD draws

One shared texture feeds party, player, target, timers, debuffs and the config Help samples
(`ui/buff_atlas.cpp`), so this order decides the whole interface:

1. `plugins\AioHud\icons\status_atlas.raw` — a sheet the player **built** (the FFXI Icon Editor's
   *Send the icons to AioHUD*, `AioHudIcons.exe`, or `tools/aioicons.ps1`).
2. the game DAT, whenever someone has **replaced** those icons — an XIPivot overlay, *or* a pack copied
   straight over the game's own ROM files. Game and HUD then agree with nothing to configure.
3. the bundled `assets\buff_atlas.raw`.
4. the game DAT when it is **untouched**, last resort.

Two things about that order. The untouched sheet sits *behind* the bundle because the client's original status
art is soft, washed-out 32px from 2002 and the bundled sheet is a crisp redraw of the same 640 statuses — the
two were rendered side by side, from a pristine copy, before this was decided; the semantics match icon for
icon (KO, weakness, sleep, poison…), only the drawing differs.

And "replaced" is **measured, not guessed**: an untouched sheet is 7-bit in all 640 records (the table above),
so anything else means somebody put their own art there. An overlay-only test looked equivalent and is not —
a pack copied over the game files is exactly the case this machine turned out to be running, and that player
would have been shown the bundled sheet instead of the HD icons they installed on purpose.

`//aio doctor` and `//aio selfcheck` print which of the four is live (`atlas=1(t0,game DAT (pack))`), because
"the icons look wrong" is a *source* question before it is anything else.

## The tools

**With a window** — the [FFXI Icon Editor](https://github.com/ejouanchicot/FFXI-Icon-Editor) (`G:\01_Development\Game_Project\FFXI Icon Editor`)
carries *File → Send the icons to AioHUD…*: pick the open file, a folder of PNGs or any DAT, and it writes the
sheet where the HUD reads it. `AioHudIcons.exe` from the same repository is that one window on its own, and
`ffxi-icons aiohud --from <file|folder>` the same thing on the command line. Its `AioHudSheet`/`AioHudLocator`
apply the very same record layout and alpha rule as `icon_dat.cpp` — all three outputs were compared
byte for byte against each other on the same DAT.

**Without one** — `tools/aioicons.ps1` ships next to the data folder (`plugins\AioHud\aioicons.ps1`).
PowerShell + System.Drawing only: no .NET SDK, no Python, nothing to install.

```
.\aioicons.ps1 -Extract                        # the game's current icons -> .\icons_png\0.png .. 639.png
.\aioicons.ps1 -Extract -Sheet -Out sheet.png  # ... as one 1024x640 contact sheet
.\aioicons.ps1 -Build -From "C:\mes_icones"    # a folder of <id>.png  -> the HUD uses them
.\aioicons.ps1 -Build -Dat "C:\pack\57.DAT"    # a pack that is NOT installed in XIPivot -> same
.\aioicons.ps1 -Reset                          # back to the game's icons
```

It finds the DAT the same way the plugin does (XIPivot overlays in their configured order, then the FFXI
install from the PlayOnline registry keys) and applies the same alpha and 8-bpp rules, so what it extracts is
what the HUD draws.

## Where else the sheet exists

A scan of all 53 488 DATs in the install finds the same 640-record file at `ROM/0/12`, `ROM/119/57`,
`ROM/176/107`, `ROM/178/46` and four more at `ROM/287/80..83` (named `sts_iconst00` without the `_32`). File
id 87 is the one the client resolves and the one every icon pack replaces; the others are not read.
