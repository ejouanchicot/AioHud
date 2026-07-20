---
title: Map system - zone id, world->pixel transform, sub-map selection, heading
summary: RE for a minimap. The live zone id off g, the client world->map-pixel transform (scale/offset table), how the floor/sub-map is picked, the CONFIRMED zone-map ROM-DAT pipeline (file-id, VTABLE/FTABLE resolve, 8-bit-palette decode), and the entity heading offset.
source: LuaCore get_info (FUN_10070120) + FFXIDB.dll marker/transform/map-DAT decompile (FUN_1006b540/10069660/10062d60/100633c0) + FFXiMain AOB, verified vs live ROM DATs 2026-07-05
---
# Map system (minimap) - reversed 2026-07-05

Phase-0 research to place the player dot on a zone map. Reversed from LuaCore get_info (zone id), the
FFXIDB minimap plugin (plugins/FFXIDB.dll, imported in the Ghidra project - it reads the CLIENT own
map subsystem), and FFXiMain.dll (the map-info table static). Every read is SEH-guarded off
g = *(LuaCore+0x1C8400), the usual data root.

Confidence tags: [decomp] read straight from a decompiled binding; [decomp-ffxidb] read from FFXIDB
use of the client structs (cross-checked against our known offsets); [inferred] derived / needs a
live probe to pin the exact number.

---
## 1. Current zone id   [decomp]

Windower get_info().zone (LuaCore FUN_10070120) reads it as a u16 field literally named "zone":

    Z    = *(g + 0x40)          // session / zone-info struct
    zone = *(u16*)(Z + 0x02)    // current zone id (matches the zones resource + zoom.json keys)

Same struct Z also holds: +0x00 u8 logged_in, +0x04 u32 weather, +0x08 mog_house. This is the
authoritative "which zone am I in" for map selection and the zoom.json lookup.

> The client map subsystem keeps its own copy of the current zone (used as the map-table key,
> section 2). FFXIDB reaches it via a FFXiMain AOB, not off g; use *(g+0x40)+2 - same value, and it
> stays on our g-anchored, SEH-guarded style.

Probe: //aio zone -> debug::log *(u16*)(*(g+0x40)+2) (+ logged_in, weather). Walk between two known
zones and confirm the id flips to the expected FFXI zone ids.

---
## 2. World -> map-pixel transform (the crux)   [decomp-ffxidb] formula, [inferred] storage

FFXIDB per-entity marker placer (FUN_10068250) reads the entity world position and the current
sub-map scale/offset record, then:

    worldX =  float @ entity+0x04
    worldZ =  float @ entity+0x0C
    scale  =  u8    @ rec+0x05
    offX   =  s16   @ rec+0x0A
    offY   =  s16   @ rec+0x0C

    mapX = (scale *  worldX) / 5.0 - offX      // K = 5.0 (DAT float)
    mapY = (scale * -worldZ) / 5.0 - offY      // Z is negated (XOR 0x80000000)

Confirmed from the same function / its DAT constants:
- K = 5.0 (DAT_100b0a20), the divisor. The Z axis is flipped (worldZ XOR 0x80000000 = -worldZ).
- Map texture native size is 512 (param_1[5] = 0.001953125 = 1/512 in the minimap setup
  FUN_1006b9e0); (mapX,mapY) are map-image pixel coordinates, then scaled by zoom + recentred on the
  player for the scrolling minimap. For a full-map draw, use (mapX,mapY) directly on the 512px image.
- Markers past distance > 2500 (entity+0xD8, DAT_100b0a3c) are culled; entity+0x120 & 0x4000 = do
  not render.

### Where scale/offset live   [decomp-ffxidb / inferred RVA]
rec comes from the client map-info table, a flat array looked up by (zone id, sub-map index)
(FFXIDB FUN_100213e0):

    lookup(tableBase, key1 = zone u16, key2 = submapIdx u8):
      rec = tableBase;  stride = 0x0E (7 * u16)
      while (rec[0] in 1..0xFFF):
         if (rec[0]==zone && (u8)rec[2]==submapIdx) return rec
         rec += 0x0E

Record (14 bytes): +0x00 u16 zone id (key1) . +0x02 u8 submap index (key2) . +0x05 u8 scale .
+0x0A s16 offsetX . +0x0C s16 offsetY (other bytes not yet decoded).

tableBase is a FFXiMain global (ASLR-shifted). FFXIDB resolves it (in FUN_10071330) by
signature-scanning FFXiMain for

    8B 74 24 14  8B 44 24 10  8B 7C 24 0C  8B 0D <ptr32>

where the "mov ecx,[<ptr32>]" operand is the table-pointer global (deref = tableBase). In the
captured dump <ptr32> = 0x0625F404 (code at RVA ~0x1F7236), i.e. real base ~0x06000000 -> data RVA
~0x25F404 - confirm live (ASLR moves the base each launch; do not hardcode the absolute).

Recommended for AioHUD (mirrors zoom.json / gear-icons): these scale/offset values are static ROM
data. Bundle a pre-extracted map/coords.json keyed zone_id -> {submap -> {scale,offX,offY}} (read the
live table once via a probe, or extract with POLUtils/AltanaView) instead of AOB-scanning in the
render path. plugins/FFXIDB/map/zoom.json (zone_id -> {submap -> zoomOffset}) is a small per-submap
fine-tune to add on top; not required for a first cut.

Probe: once tableBase is resolved (AOB or a one-off), //aio maprec -> dump the record for
(zone, submap) and verify mapX,mapY land on the dot for a couple of known standing spots.

---
## 3. Sub-map (floor) selection   [decomp-ffxidb]

Some zones have several maps (floors). The client picks the current sub-map itself: FFXIDB calls a
client function pointer for the index rather than deciding by height:

    submapIdx = (*(fnptr))()      // fnptr adjacent to the map-table global (FUN_10071330 scan)
    // 0xFFFFFFFF = none / not on a mapped sub-map

So the by-height / region-table logic lives inside FFXiMain and is exposed as one call returning the
index. Options for AioHUD: (a) resolve + call that function (heavier - calling game code from the
render thread), (b) reproduce the selection from a small per-zone region table (bundle it), or (c)
for single-map zones just use submap 0. zoom.json is keyed by this same index.

---
## 4. Zone map IMAGE (ROM DATs)   [decomp-ffxidb, CONFIRMED end-to-end 2026-07-05]

Fully reversed and verified against the live ROM DATs. The map image is loaded by the SAME client map
object FFXIDB drives; three functions do the work:

- FUN_1006b540  - computes the map file-id from the (zone,submap) record and stores it at obj+0x228.
- FUN_10069660  - calls the DAT decoder with that file-id and uploads the result to a D3D texture.
- FUN_10062d60  - the DAT decoder (open file -> walk chunks -> decode the graphic).
- FUN_100633c0  - resolves a file-id to a ROM path via the VTABLE/FTABLE volume scheme.

Verified resolves (real install): **zone 103 Valkurm -> ROM/17/27.DAT** (graphic "m_103_00", 512x512,
8-bit palette) and **zone 248 Selbina -> ROM/18/80.DAT** (graphic "m_248_00", 512x512, 8-bit palette).
Both decode to the correct parchment maps.

### 4a. map file-id from the record   [decomp-ffxidb FUN_1006b540]

Same 14-byte record as section 2 (looked up by zone+submap). Two more fields:

    +0x04 u16 flags   (bit 0 selects the id base)
    +0x08 u16 index   (the map file index within that base)

    base    = (rec.flags@0x04 & 1) ? 0xD02F : 0x14C0    // 0x14C0 = base maps, 0xD02F = expansion set
    fileId  = base + rec.index@0x08

Valkurm: flags=0x0100 (bit0=0) -> base 0x14C0, index 4 -> fileId 0x14C4.
Selbina: base 0x14C0, index 0xB9 -> fileId 0x1579. (The earlier "0x14C0 + u16@+0x08" formula was
already CORRECT - the earlier miss was entirely in the path resolve, 4b.)

### 4b. resolve fileId -> ROM path   [decomp-ffxidb FUN_100633c0]

VTABLE = 1 byte per fileId (owning volume 1..9). FTABLE = **2 bytes (u16 LE) per fileId** (dir+file).
Volumes: v=1 uses root VTABLE.DAT/FTABLE.DAT and root ROM/; v>=2 uses ROM<v>/VTABLE<v>.DAT +
ROM<v>/FTABLE<v>.DAT and root ROM<v>/. The client scans dirs matching regex `^ROM(\d+)$` for the max
volume, then for v from max..1 picks the volume whose VTABLE<v>[fileId] == v.

    vol   = VTABLE_v[fileId]              // pick v where this == v
    entry = FTABLE_v[fileId*2]  (u16 LE)  // <-- FULL 16 bits, not one byte
    subdir = (entry >> 7) & 0x1FF         // 0..511   (9 bits)
    file   =  entry       & 0x7F          // 0..127   (7 bits)
    path   = ROOT / (v==1 ? "ROM" : "ROM"+v) / subdir / (file + ".DAT")

Valkurm fileId 0x14C4: vol 1, entry 0x089B -> subdir 17, file 27 -> ROM/17/27.DAT.
Selbina fileId 0x1579: vol 1, entry 0x0950 -> subdir 18, file 80 -> ROM/18/80.DAT.

> The two earlier-pass bugs, both here: (1) read only the LOW BYTE of the FTABLE entry (0x9B) instead
> of the u16 (0x089B) - dropping the high bits that carry the subdir; (2) swapped subdir<->file. The
> truncated+swapped 0x9B gave subdir 27 / file 1 -> ROM/27/1.DAT = an unrelated MOB list. The correct
> u16 decode gives subdir 17 / file 27 -> ROM/17/27.DAT = the map.

### 4c. decode the DAT -> {W,H,format,pixels}   [decomp-ffxidb FUN_10062d60]

The DAT is a chunk stream. 8-byte chunk header: word2 = u32 @ chunk+0x04, type = word2 & 0x7F,
chunkSize(bytes) = (word2 >> 7) * 16. Walk chunk += chunkSize until type/size 0. **type 0x20 = the
graphic**. (Chunk @0x00 is a type-1 32-byte file header; the graphic is the next chunk.)

Graphic sub-header starts at **chunk+0x10** (skip the 8-byte chunk header + 8 reserved bytes). Fields
relative to that header base H:

    H+0x00 u8   flags   ; (flags & 0x10) != 0  => 8-bit palette   ; == 0 => DXT3
    H+0x09      char[8] name    ; must match regex ^m_[0-9]+_[0-9]+$  (m_<zone>_<submap>); gates decode
    H+0x15 s32  width
    H+0x19 s32  height
    H+0x25 u32  dataSize        ; if 0 -> use width*height

**BOTH branches are used by real maps** (CONFIRMED 2026-07-05, decoding live install DATs) :
- **OUTDOOR** zones (fields, Selbina, Valkurm) = **8-bit palette** (flags 0xB1, bit4 set), 512x512.
- **CITY** zones (San d'Oria, etc.) = **DXT3** (flags 0xA1, bit4 clear), 512x512 vanilla / up to 2048x2048
  for XIPivot HD overlays (Remapster). Earlier notes said "DXT3 not used by maps" — WRONG ; cities are DXT3.

8-bit palette layout, right after the header:

    palette : 1024 bytes at H + 0x39 + 4   (256 entries * 4 bytes, B,G,R,A ; skip 4 after the header)
    indices : width*height bytes at H + 0x39 + 4 + 0x400
    pixel[i] = palette[ indices[i] ]       // 4 bytes copied verbatim -> D3DFMT_A8R8G8B8 memory order

Palette bytes are already in D3DFMT_A8R8G8B8 memory order (little-endian ARGB = byte order B,G,R,A),
so the client copies each 4-byte entry with NO swizzle. **Alpha is 0..0x80 (0x80 = opaque)**: for a
normal SRCALPHA/INVSRCALPHA blend, double it (A = min(255, A*2)) or use a matching blend. Upload the
expanded W*H*4 buffer as D3DFMT_A8R8G8B8.

**DXT3 layout (city maps)** — reversed FourCC **"3TXD"** (= 'DXT3' little-endian, 0x44585433) at **H+0x39** ;
gates the branch. The compressed payload is **STANDARD DXT3** (8 alpha bytes then 8 colour bytes per 4x4 block)
starting at **H+0x45** (the fixed graphic header size ; the payload's first alpha block is FF*8 = opaque). CPU-
decode each block to A8R8G8B8. **Alpha here is full-range 0..255** (NOT the 8bpp 0..0x80). Getting the offset
wrong (e.g. H+0x50, off by 11) or swapping the alpha/colour blocks yields noise/stripes — verified against a
reference DXT decoder (PIL). A DXT city map decodes TOP-DOWN, so FLIP it vertically to match the 8bpp maps'
bottom-up storage (the minimap draw V-flips both alike).

### 4d. C++-ready pseudocode

    // (a) map file-id from the (zone,submap) record (section 2 lookup gives `rec`)
    uint32_t mapFileId(const uint8_t* rec) {
        uint16_t flags = read_u16(rec + 0x04);
        uint16_t index = read_u16(rec + 0x08);
        uint32_t base  = (flags & 1) ? 0xD02F : 0x14C0;
        return base + index;
    }

    // (b) resolve fileId -> ROM path (root has VTABLE.DAT/FTABLE.DAT; ROMn has VTABLE n/FTABLE n)
    //     preload VTABLE_v (1 byte/id) and FTABLE_v (2 bytes/id) per volume once.
    std::string resolvePath(uint32_t fileId) {
        for (int v = maxVolume; v >= 1; --v) {
            const auto& vt = VTABLE[v]; const auto& ft = FTABLE[v];
            if (fileId >= vt.size() || vt[fileId] != v) continue;
            uint16_t entry = ft[fileId*2] | (ft[fileId*2+1] << 8);   // FULL u16 LE
            if (entry == 0) continue;
            int subdir = (entry >> 7) & 0x1FF;
            int file   =  entry       & 0x7F;
            std::string rom = (v == 1) ? "ROM" : "ROM" + std::to_string(v);
            return ROOT + "/" + rom + "/" + std::to_string(subdir) + "/" + std::to_string(file) + ".DAT";
        }
        return {};
    }

    // (c) decode the map DAT -> {W,H,format,pixels}.  (SEH-guard the file read like game_mem.)
    struct MapImg { int W, H; bool dxt3; std::vector<uint8_t> pixels; }; // pixels = A8R8G8B8 (or raw DXT3)
    bool decodeMapDat(const uint8_t* d, size_t n, MapImg& out) {
        size_t off = 0;
        while (off + 8 <= n) {
            uint32_t w2 = read_u32(d + off + 4);
            uint32_t type = w2 & 0x7F, size = (w2 >> 7) * 16;
            if (size == 0) break;
            if (type == 0x20) {
                const uint8_t* H = d + off + 0x10;                 // graphic header
                uint8_t flags = H[0];
                // (optional) verify name at H+9 matches ^m_[0-9]+_[0-9]+$
                out.W = read_s32(H + 0x15);
                out.H = read_s32(H + 0x19);
                if (flags & 0x10) {                                // 8-bit palette (all zone maps)
                    out.dxt3 = false;
                    const uint8_t* pal = H + 0x39 + 4;             // 256 * BGRA
                    const uint8_t* idx = pal + 0x400;
                    out.pixels.resize((size_t)out.W * out.H * 4);
                    for (size_t i = 0; i < (size_t)out.W * out.H; ++i) {
                        const uint8_t* e = pal + idx[i]*4;         // B,G,R,A
                        uint8_t* o = &out.pixels[i*4];
                        o[0]=e[0]; o[1]=e[1]; o[2]=e[2];           // B,G,R verbatim (A8R8G8B8 mem order)
                        o[3]=(uint8_t)std::min(255, e[3]*2);       // A 0..0x80 -> 0..255
                    }
                } else if (H[0x39]=='3' && H[0x3A]=='T' && H[0x3B]=='X' && H[0x3C]=='D') {   // DXT3 (CITY maps)
                    out.dxt3 = true;                               // reversed "3TXD" FourCC gates it
                    const uint8_t* data = H + 0x45;                // STANDARD DXT3 (alpha8 + colour8 per 4x4 block)
                    out.pixels.resize((size_t)out.W * out.H * 4);
                    for (int by=0; by<out.H/4; ++by) for (int bx=0; bx<out.W/4; ++bx)
                        decode_dxt3_block(data + (by*(out.W/4)+bx)*16, &out.pixels[(by*4*out.W+bx*4)*4], out.W);
                    flip_vertical(out.pixels, out.W, out.H);       // top-down -> match 8bpp bottom-up
                }
                return true;
            }
            off += size;
        }
        return false;
    }

### 4e. AioHUD integration note

This is a live, patch-stable decode (ids come from the ROM tables, no hardcoded absolutes). Wrap the
file read in the usual SEH/valid_ptr guard (game_mem.cpp) and cache the decoded 512x512 A8R8G8B8 in a
D3D texture per (zone,submap), loaded on demand - same "decoder cache, pre-run" pattern as gear icons.
The map is stored horizontally MIRRORED in the DAT (the parchment banner text reads reversed); flip U
at draw (or flip on decode). Bundling pre-extracted PNGs is still a valid fallback, but the ROM decode
above is now proven and preferred.

---
## 5. Player / entity heading   [decomp]

Windower get_mob* table builder (LuaCore FUN_1008db90) exposes both "facing" and "heading" from the
SAME entity field:

    heading = float @ entity+0x18      // radians

Reach the self entity as read_self_speed does: pp=*(g+0x248), base=pp-4, self entity index at
base+0x20, entity = entity_array[idx] (entity_array = *(g+0x24)), then +0x18. Draw the arrow rotated
by this angle (sign / zero-direction to be confirmed against the in-game compass).

The same decomp confirmed the rest of the entity struct we rely on: +0x04 X, +0x08 Y, +0x0C Z,
+0x18 heading, +0x74 index, +0x78 id, +0x7C name, +0x98 movement_speed, +0xD8 distance, +0xEC HP%,
+0x16C status, +0x188 claim_id, +0x1D0 spawn_type.

Probe: //aio head -> hexdump the self entity and log float @+0x18 while you spin the camera /
character; confirm it sweeps 0..2*pi and note which world direction is 0.

---
## 6. Config options (the Minimap panel)   [implemented]

The minimap has a settings panel (`//aio config` → "Minimap" / "Minicarte", `src/ui/minimap_config.cpp`),
split into three sub-sections. It writes the `mm*` fields of `UiConfig` (`src/model/ui_config.h`); the
`Minimap` widget (`src/ui/minimap.cpp`) reads them each frame. What is user-configurable:

- **Display**
  - **Show minimap** (`mmShow`) — on/off.
  - **Size** (`mmScale`, 0.50..2.00, 5% steps) — the size multiplier on the 220px square footprint;
    also wheel-resizable in edit mode.
  - **Zoom** (`mmZoom`, 1.0x..24.0x) — player-centred zoom (1x = the whole 512px map fits, higher =
    zoomed in). Also mouse-wheel adjustable when the cursor is over the map outside edit mode
    (exponential, `powf(1.18, wheel)`).
  - **Background** (`mmBgAlpha`, 0..100%) — backdrop opacity behind the map (0 = fully transparent, the
    game shows through; colour `0x00080B12`, drawn as a disc when round, a quad when square).
- **Shape & Frame**
  - **Shape** (`mmShape`) — **Square** (0) or **Round** (1). Round uses a **stencil circle-clip**
    (`mm_clip_circle_begin`: clear the widget rect stencil to 0, set 1 inside the disc, draw content only
    where stencil == 1).
  - **Frame** (`mmFrame`) — **None** (0) / **Box Theme** (1, follows the party/alliance `skinTheme` via
    procedural hue or the FFXI skin border) / **Custom** (2). Round frame is an additive halo ring
    (`disc_glow`, blend reset after per rule 3); square frame is `rrect_stroke`.
  - **Colour** (`mmFrameColor`, only shown when Frame = Custom) — a swatch + R/G/B sliders (channels at
    shifts 16/8/0, alpha forced opaque).
- **Markers**
  - **Marker Size** (`mmMarkerScale`, 0.50..2.00) — entity/player marker size multiplier.
  - **Players (PC)** (`mmPC`), **NPCs** (`mmNPC`), **Monsters** (`mmMob`) — per-entity-type visibility
    toggles. Marker colours follow the Target box name-colour convention (mm_ent_color: NPC green, mob
    unclaimed gold / your-or-party claim or engaged red / other-claim magenta, PC party cyan / other-party
    blue / solo white).

> Preview: the config **live preview** (`Hud::draw_config_preview`, `section() == 3`) temporarily forces
> `mmPosSet` / `mmX` / `mmY` to centre the real minimap in the preview stage, draws it (it reads live game
> data — current zone + entities), then restores the real placement. See
> [architecture/config-panels.md](../architecture/config-panels.md).

---
## 7. Help-tab radar sample (`minimap_help_disc`)   [implemented]

The Minimap panel's Help tab renders a round "radar disc" live sample (`config_page.cpp` `it.kind == 30`
→ `minimap_help_disc`, `src/ui/minimap.cpp`). It is **not** a stylised mock — it draws the REAL thing,
reusing the widget's renderers so the sample matches the game exactly:

1. **The real current-zone map** — player-centred, north-up, V-flipped, the **same transform as the live
   widget**. It follows the user's `mmZoom`: `nativePerLens = 256.0f / Z` native px across the lens radius,
   the same extent the floating widget uses. UV: `mapX/mapY` from the section-2 formula
   (`(scale*meX)/5 - offX`, `(scale*-meZ)/5 - offY`), `u0 = mapX/512`, `v0 = 1 - mapY/512`, drawn with
   `tdisc`.
2. **The real live entity markers** from `f.game->mapEnts` — mobs = the Arrow icon rotated by `heading`,
   NPC/PC = tinted dots — using the same `mm_ent_color` convention and the same per-type toggles
   (`mmMob` / `mmNPC` / `mmPC`) as the widget, then the player pin at the centre.

### Config-owned map texture (not borrowed from the widget)

The **`ConfigPage` owns its own copy of the zone map** (`config_page.h`: `u32 mmMapTex_` +
`unsigned mmMapFileId_`). Under `it.kind == 30` it loads via `load_zone_map(f.game->map.fileId, …)` +
`make_texture_argb_mip`, **keyed by `map.fileId`**, and **retries every frame until it succeeds** — it only
advances `mmMapFileId_` on a successful load, so a transient failure retries next frame. On **device-lost it
FORGETS the handle** (sets `mmMapTex_ = 0`, does **not** `Release`) and resets `mmMapFileId_` (per rule 4).

This **replaced** an earlier fragile approach where the HUD borrowed the floating Minimap widget's texture
via `Minimap::map_tex()` → `ConfigPage::set_help_minimap_tex()` each frame (`hud.cpp`); that borrow + getter
+ setter were **removed**. The self-load decouples the Help sample from the floating widget's draw order and
its `mmShow` state, so the Help shows the map **even when the floating minimap is hidden**.

### Gotcha — bezel BEFORE map (draw order)

`draw_brass_bezel` ends with a **FILLED dark inner disc** (~lens radius). In `minimap_help_disc` the bezel
**MUST be drawn before the map**, i.e. order = **brass bezel → lens backdrop disc → map `tdisc` → entity
markers → player pin**. If the bezel is drawn *after* the map it paints over the entire map. The live widget
already draws bezel-then-map; the Help had it reversed, which hid the map even though the texture and load
were fine.

## See also
- Config panels — the split config page (../architecture/config-panels.md) — the Minimap settings panel.
- Local player struct (player-struct.md) . Party array (party-array.md) - the g chains reused here.
- Target & sub-target struct (target-substruct.md) - the same target_t FFXIDB reaches at the client
  map object +0x30 (0x04000000 = nothing; entity ptr at +0x08).
- Gear icons (gear-icons.md) - the ROM-DAT / bundle pattern recommended for the map images.
- Reverse-engineering recipe (../architecture/reverse-engineering-recipe.md).
