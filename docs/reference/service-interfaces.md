---
title: Service Interfaces
summary: Console, TextHandler/TextObject, PrimitiveHandler/PrimitiveObject and the ffxi game-data interface handed back by the PluginManager getters.
source: REFERENCE.md §3
---
# Service interfaces

### Console — `[3]`
- `[3] print(char*)` — writes a line to the Windower console.

### TextHandler — `[4]`
- `[0] create(char* name) -> TextObject*` — create-or-get by name; auto-added to the render list (rendered every frame by Windower).
- `[3] remove(TextObject*)` — clears the object's +0x1c "alive" flag → drops it from the list. **Call on unload or the text persists after the plugin is gone.**

### TextObject — `.?AVTextObject@@`, 21 methods
Every setter: lock → store at offset → set dirty bytes (+0xe0/+0xe1) → unlock.

| method | meaning | field | notes |
|---|---|---|---|
| `[0]` | SetText(char*) | std::wstring @+0x38 (UTF-16, SSO) | |
| `[1]` | SetVisible(char) | byte @+0x30 | default already 1 |
| `[3]` | SetFont(char* name) | font list | **REQUIRED** (e.g. "Arial") |
| `[4]` | SetSize(float) | @+0x7c | **REQUIRED — default 0 → invisible!** |
| `[6]` | SetColor(char a,r,g,b) | @+0xa4 | order **A,R,G,B**; default white |
| `[10]` | **SetPos(float x,y)** | @+0x50/+0x54 | **THE screen position.** ([9]@+0x58 is an offset/anchor (default -1,-1) and does NOT move the text.) |
| `[11]` | GetPos() -> (x,y) | reads +0x50 | |
| `[12]` | **GetExtents() -> (w,h)** | measured pixel size | valid the frame after SetText; use to centre text |

To render text you MUST set: font + size + text + visible (visible/colour default OK).

### PrimitiveHandler — `[5]`
- `[0] create(char* name) -> PrimitiveObject*`
- `[3] remove(PrimitiveObject*)`
- Object list (std::vector<shared_ptr>) at **handler+0xb8**.

### PrimitiveObject — `.?AVPrimitiveObject@@`, 10 methods, ctor `operator_new(200)`
**A box = POSITION + SIZE** (not corner-to-corner). lock/dirty field @+0xb8.

| method | meaning | field | notes |
|---|---|---|---|
| `[0]` | SetVisible(char) | @+0x28 | **default 0 → set it!** |
| `[1]` | SetColor(char a,r,g,b) | @+0x29 | order **A,R,G,B**; **default transparent → set it!** |
| `[2]` | SetTexture(char* path) | @+0x88 | optional; solid-colour rect needs no texture |
| `[7]` | **SetPosition(float x,y)** | @+0x30 | top-left |
| `[6]` | **SetSize(float w,h)** | @+0x38 | the SIZE, **not** the bottom-right corner |
| `[4]` | SetScale(float sx,sy) | @+0x40 | **default 1.0 — leave it; this is a SCALE, not size** |
| `[3]` | SetTexture2(int, char* path) | +0x60/+0x64/+0x88 | a 2nd SetTexture overload (int flag/id + name); not UV |
| `[5]` | SetSizeToTexture(bool) | +0x84, writes +0x38/+0x3c | if true & textured, sets SIZE to the texture's NATIVE w/h (from +0x68/+0x6c). Handy: `[5](1)` auto-sizes to the PNG. |
| `[8]` | GetPosition(float* x, float* y) | reads +0x30/+0x34 | a GETTER (out-params), not a setter |
| `[9]` | scalar deleting destructor(byte) | — | don't call |

**All 10 methods are now decompiled (Ghidra).** There is **NO** texture UV / source-rect
/ scroll method on PrimitiveObject — confirmed by reading every method, not guessed.
A textured quad cannot scroll its UVs natively. (Smooth "flowing" animation must be
done with a multi-frame PNG sequence flipped one frame at a time.)

Pitfalls that cost hours: `+0x40` is a scale (set to 200,60 → box ×200 = giant);
`+0x38` is size, not bottom-right (passing `x+w,y+h` stretched the box to the
screen edge — looked "enormous"). `rect(x,y,w,h)` = scale(1,1)+position(x,y)+size(w,h).

#### TEXTURED primitives behave differently from solid rects (calibrated in-game)
The size/scale/color semantics above were calibrated on **solid-colour** rects. A
primitive carrying a `SetTexture(path)` follows **different** rules — get these
wrong and you get the classic bugs noted:

- **Native size, top-left anchored.** The texture draws at its **own pixel size**
  (e.g. a 256×26 PNG draws 256×26), anchored at `position()`. There is **no
  built-in stretch** to an arbitrary size.
- **`size()` ([6], +0x38) = the CLIP rectangle**, not a stretch. The native-texel
  texture is drawn into it top-left. `size` ≥ texture → whole texture shows (the
  extra clip area is empty). `size` < texture → cropped. **You MUST set `size()`
  explicitly** to ≥ the texture: if left unset, +0x38 holds an undefined / reused
  value (objects are create-or-get by name, so a `//reload` can hand back a stale
  one) → the texture randomly renders as a **small square top-left** (~"1 load in
  2 bugs"). Always `size(texW, texH)`.
- **`scale()` ([4]) = UV TILING (repeat)**, not enlargement. `scale(2,2)` tiles
  the texture 2×2 inside the same footprint; it does NOT make it bigger.
- **`color()` RGB tints** the texture (multiply: white PNG × red = red veins).
  **`color()` ALPHA is IGNORED for textures** — a textured quad always renders at
  full opacity. So you **cannot cross-fade** two textures by alpha.
- **The PNG's OWN per-pixel alpha IS honoured** (transparent areas show whatever
  is behind). To make a textured element look translucent over the game, you
  cannot dim it via color-alpha; instead put a **semi-transparent solid rect**
  behind it (solid rects DO honour color alpha).
- **Animation** (no cross-fade available): flip ONE frame visible at a time
  (`show new before hiding old`; never hide+show the same object in a frame).
  There is **NO UV-scroll** — all 10 PrimitiveObject methods are decompiled and
  none offsets texture coords (see the method table above). Smooth flow therefore
  requires a multi-frame PNG sequence; smoothness is bounded by frame count/quality.

### ffxi — `[7]`  (partially mapped — see [game data](../game-data/README.md))
`.?AVffxi@@`. The Windower `windower.ffxi.*` equivalent (player/entity/zone).
host->vtbl[7] returns the singleton (its own vtable not yet walked). In the
meantime we read game data **directly from memory + party packets** — fully
documented in **[game data (player memory & party packets)](../game-data/README.md)**.

## See also
- [The host: PluginManager](host-pluginmanager.md)
- [Direct D3D8 rendering](d3d8-rendering.md)
- [Coordinate system](coordinates.md)
- [Player struct — read from memory](../game-data/player-struct.md)
- [Party member packets](../game-data/party-packets.md)
