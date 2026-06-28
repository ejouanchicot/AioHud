# Windower 4 plugin — complete reverse-engineered reference

Everything we worked out about writing a native C++ plugin for **Windower 4**
(hook `4.7.9.0`, this install). All of it was recovered with Ghidra from
`Hook.dll`, `FFXIDB.dll`, `Timers.dll` — there is no public SDK. This is the
single source of truth; the framework in `include/` implements it.

---

## 1. The plugin DLL contract (ABI)

A Windower plugin is a 32-bit DLL that exports exactly two functions:

```c
extern "C" __declspec(dllexport) void*        CreateInstance();        // returns an IPlugin*
extern "C" __declspec(dllexport) unsigned int GetInterfaceVersion();   // MUST return 0x04070300
```

- Export them **undecorated** (`CreateInstance`, `GetInterfaceVersion`) via a `.def`.
- `GetInterfaceVersion` must return **`0x04070300`** or the hook refuses to load it.
- `CreateInstance` returns a pointer to an object whose first member is a 34-entry vtable.

Loader flow (Hook.dll `FUN_1006ee10`): `GetProcAddress` both exports → check
version → `CreateInstance()` → then immediately calls `vtbl[0]` and `vtbl[1]`
and runs `strlen` on the results (so they MUST return valid `char*`), then `vtbl[2]` (Init).

### Calling convention

All 34 vtable methods are `__stdcall` with **`this` passed as the first stack
argument** (callee cleans the stack, `ret N`). We model the vtable as a manual
array of `__stdcall` function pointers (see `windower_plugin.h`) so we control
each method's stack cleanup exactly. Stack-arg counts (incl. `self`) per slot,
measured from FFXIDB's `ret N`:

```
slot   0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33
args   1  1  2  1  1  1  1  2  2  4  4  5  5  6  4  5  5  1  1  0  0  0  0  1  1  3  3  4  4  5  3  4  4  0
```

### IPlugin slots we identified

| slot | meaning | notes |
|---|---|---|
| 0 | `char* GetName()` | shown in plugin list; strlen'd at load — must be valid |
| 1 | `char* GetDescription()` | strlen'd at load — must be valid |
| 2 | `Init(PluginManager* host)` | the host is the entry to everything (see §2) |
| 3 | `OnUnload()` | called once at `//unload` then `FreeLibrary` (`FUN_10070610` → `FUN_10070fb0` calls `vtbl[3]`). **Clean up your objects here.** |
| 4 | `bool IsCore()` | **MUST return 0** or `//unload` is refused ("Core plugins cannot be manually unloaded"). A `void` method leaks garbage in EAX → treated as core. |
| 5, 6 | per-frame render hooks | fire ~every frame (≈ framerate). Animate here. No device passed. |
| 7 | `HandleCommand(char* cmd)` | console command for this plugin (see §2b). `cmd` = the args AFTER the alias (leading space already there). Routed by the **GetDescription alias**, NOT GetName. |
| 11, 12 | packet in / out | return `byte` (handled?) |
| 13, 14 | keyboard / mouse | fire during play |
| 32 | scalar deleting destructor | `vtbl[0x80](1)` when refcount hits 0 |

Everything else: safe no-op returning 0. (Our framework returns `u32 0` from
every method so EAX is never garbage.)

### 2b. Console commands (`//yourcmd ...`)  — REVERSED & WORKING

Native plugins **do** get `//` commands — the routing is just not by GetName.
Reversed from Hook.dll + a live dump of its plugin table:

- Hook keeps a plugin table (`*(Hook+0x1cbe4c) → +0xc = manager`, vector at
  `manager+0xa14a8..+0xa14ac`, **stride 100**). Each entry: `+0x04` IPlugin*,
  `+0x08` name (`std::string`, = **GetName lowercased**, used by //unload///load),
  `+0x58/+0x5c` = **command-alias vector** (`std::string`, stride 0x18).
- The dispatcher **`FUN_10042840`** walks every plugin's alias list, byte-compares
  (case-sensitive, `FUN_10037a00`) the typed token against each alias, and on a
  hit calls that plugin's **slot 7** with the remaining args.
- **The alias = the plugin's `GetDescription()` string, lowercased.** (Verified:
  our description "AioHUD D3D liquid" became alias `aiohud d3d liquid`; ffxidb's
  is `ffxidb`, luacore's `lua`, guildwork's `gw`.) So to get `//foo`, make
  **`GetDescription()` return `"foo"`** (a single clean lowercase word).
- `HandleCommand` (slot 7) then receives the text **after** the alias, e.g.
  `//foo hp 50` → cmd = `" hp 50"` (FFXIDB skips the leading space).
- Don't fight it with file polling — the GetDescription alias is the real path.

---

## 2. The host: `PluginManager`  (RTTI `.?AVPluginManager@@`)

`Init` (slot 2) receives a `PluginManager*`. It has 9 vtable methods; the useful
ones are getters returning the service interfaces. Call them as `fn(host)`
(`__stdcall`, host on the stack); they ignore the arg but expect it.

| host vtbl[i] | returns | RTTI |
|---|---|---|
| `[2]` | **`IDirect3DDevice8*`** (the real device) | vtable in `d3d8.dll` |
| `[3]` | **Console** | `.?AVConsole@@` |
| `[4]` | **TextHandler** | `.?AVTextHandler@@` |
| `[5]` | **PrimitiveHandler** | `.?AVPrimitiveHandler@@` |
| `[6]` | PacketStreamHandler | `.?AVPacketStreamHandler@@` |
| `[7]` | **ffxi** (game data) | `.?AVffxi@@` (not yet mapped) |
| `[8]` | destructor — **never call** | |

---

## 3. Service interfaces

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

### ffxi — `[7]`  (partially mapped — see §9)
`.?AVffxi@@`. The Windower `windower.ffxi.*` equivalent (player/entity/zone).
host->vtbl[7] returns the singleton (its own vtable not yet walked). In the
meantime we read game data **directly from memory + party packets** — fully
documented in **§9 (game data: player memory & party packets)**.

---

## 3b. DIRECT D3D8 rendering (the FFXIDB path) — bypasses the primitive API

The Windower primitive/text API is the *easy* path but limited (no UV scroll, rect
clip only, no rotation). For full control, render **straight through the
IDirect3DDevice8** the host hands you at `host->vtbl[2]` — this is exactly what
**FFXIDB** does for its round, scrolling minimap. Decompiled from FFXIDB's
per-frame draw (`FUN_1006a910`): it stores the device and calls, per frame,

```
dev->vtbl[76](dev, 0x104)                       // SetVertexShader(FVF = XYZRHW|TEX1)
dev->vtbl[61](dev, 0, texture)                  // SetTexture(stage 0, tex)
dev->vtbl[63](dev, 0, type, value)              // SetTextureStageState
dev->vtbl[50](dev, state, value)                // SetRenderState
dev->vtbl[72](dev, 5, 2, &verts, 0x18)          // DrawPrimitiveUP(TRIANGLESTRIP, 2 tris, verts, stride=24)
```

i.e. it builds its **own vertices** (FVF `0x104` = `D3DFVF_XYZRHW|D3DFVF_TEX1`,
24-byte verts = screen pos + **UV**) and draws them with `DrawPrimitiveUP`. The UVs
are the player position → the map **scrolls** smoothly; the round shape is custom
geometry/mask. **You can do the same** (UV scroll, any size, rotation, circular
mask) — the primitive API's limits are not the engine's.

IDirect3DDevice8 vtable indices we use (× 4 = byte offset):
`50` SetRenderState, `51` GetRenderState, `54` ApplyStateBlock, `56` DeleteStateBlock,
`57` CreateStateBlock, `61` SetTexture, `63` SetTextureStageState, `72` DrawPrimitiveUP,
`76` SetVertexShader, `41` GetViewport.

The per-frame render hooks (IPlugin slots 5/6) fire from inside EndScene, so the
device is mid-scene and ready to draw. **Save/restore device state** around your
draw or you corrupt the game/Windower rendering — simplest: `CreateStateBlock(
D3DSBT_ALL)` (captures current) → set states → DrawPrimitiveUP → `ApplyStateBlock`
→ `DeleteStateBlock`. (FVF `0x104` verts are 24 bytes: float x,y,z,rhw + float u,v.)

**VERIFIED WORKING (2026-06-22), recipe:**
- **Draw on IPlugin slot 6, not slot 5.** Slot 5 fired but nothing showed; slot 6
  (FFXIDB's draw slot) draws to screen. (Wire `m06` → your render in
  windower_plugin.h.)
- **Coord space for XYZRHW = 2560×1400** (a (0,0)-(1280,700) quad covered exactly
  the top-left quarter) — i.e. the same logical space as the primitive API, NOT
  the supersampled 5120×2800 that GetViewport returns.
- **Make a texture yourself** (no d3dx/PNG loader needed): `device->CreateTexture(
  w,h,1,0,D3DFMT_A8R8G8B8=21,D3DPOOL_MANAGED=1,&tex)` [vtbl 20], then
  `tex->LockRect(0,&lr,0,0)` [tex vtbl 16] → fill `lr.pBits` rows (pitch `lr.Pitch`,
  A8R8G8B8) → `tex->UnlockRect(0)` [17]. Release on unload [tex vtbl 2].
- **UV scroll** = draw the quad with U range `[u0, u0+1]`, `u0 = time`, plus
  `SetTextureStageState(0, D3DTSS_ADDRESSU=13, D3DTADDRESS_WRAP=1)` → the texture
  scrolls smoothly forever (no frames). This is the smooth-liquid path.
- **Alpha blend**: `SetRenderState(ALPHABLENDENABLE=27,1)`, `SRCBLEND=19→SRCALPHA(5)`,
  `DESTBLEND=20→INVSRCALPHA(6)`. Texture stage for a pure texture:
  `COLOROP=1→SELECTARG1(2)`, `COLORARG1=2→D3DTA_TEXTURE(2)`, same for ALPHAOP/ARG1.
  Also `ZENABLE=7→0`, `CULLMODE=22→NONE(1)`, `LIGHTING=137→0`.
- This unlocks what the primitive API can't: UV scroll, any size, rotation, custom
  geometry (round/curved containers), multi-layer compositing.

### 3c. ⚠️ ZONING GOTCHA — a texture's ALPHA mis-samples while a zone loads

**Symptom:** during a zone load (TP/door), any quad whose BLEND alpha comes from a
**texture** renders too opaque → too bright, then snaps correct the instant the
load finishes. RGB is unaffected the whole time. (Found 2026-06-22; the liquid
fiole "brightened" at every zoning. Proven via an in-game A/B harness — opaque
textured = stable, translucent textured = bugs, translucent *untextured* = stable,
DESTBLEND=ZERO still bugs so it's not the background, vertex-alpha = stable.)

**Root cause:** while the loading screen is up, sampling a `D3DPOOL_MANAGED`
texture returns its **alpha channel as ~255 (opaque)** while RGB stays correct.
So `ALPHAOP=MODULATE/SELECTARG1` with `ALPHAARG=D3DTA_TEXTURE` yields the wrong
(too-high) blend alpha. Things that DON'T trigger it: device reset, VRAM eviction
(PreLoad each frame does NOT help), gamma, our render states being overwritten
(read-back stable 2000+ frames), the background, WRAP/CLAMP, UV, magnification.
It is specifically the **sampled texture alpha**. Alpha at the safe extremes (0 or
255 — e.g. cap/icon art that's fully transparent or fully opaque) survives; only
**mid-range** texture alpha (a translucent fluid) visibly shifts.

**THE RULE — never drive blend alpha from a texture's alpha channel.** Take the
blend alpha from the **vertex/diffuse** instead:
```c
SetTextureStageState(0, COLOROP=1, MODULATE=4)      // RGB  = texture × diffuse  (RGB samples fine)
SetTextureStageState(0, COLORARG1=2, D3DTA_TEXTURE=2)
SetTextureStageState(0, COLORARG2=3, D3DTA_DIFFUSE=0)
SetTextureStageState(0, ALPHAOP=4, SELECTARG1=2)    // ALPHA = vertex/diffuse ONLY  (immune)
SetTextureStageState(0, ALPHAARG1=5, D3DTA_DIFFUSE=0)
```
- **Translucent textured fill (liquid):** put the translucency in the **vertex
  diffuse alpha**; keep RGB = texture×diffuse. (Per-pixel translucency baked into
  the texture's alpha is NOT usable — it mis-samples. If you need per-pixel
  translucency, bake it into RGB premultiplied instead.)
- **Additive shaped sprites (glow / halo / bubbles, DESTBLEND=ONE):** store the
  SHAPE/falloff in the texture **RGB (grayscale)**, not alpha. Edges = black = add
  nothing. Then `COLOROP=MODULATE` (shape×tint) + `ALPHAOP=SELECTARG1 DIFFUSE`
  (intensity from vertex). Shape now comes from RGB → immune.

This is why solid (untextured) rects, and opaque/icon textures, never showed the
glitch — only translucent texture-alpha fills did.

---

## 4. Coordinate system

**Primitives AND text share ONE space = the D3D viewport, 1:1 pixels.**
Here that is **2560×1400** (confirmed: a `(0,0)-(1280,700)` box = exactly the
top-left quarter). It is NOT:
- the window client (`GetClientRect` = 2558×1349),
- the desktop (`GetDisplayMode` = 5120×1440, dual monitor),
- the supersampled buffer (settings has `supersampling=2`).

⚠️ **Do NOT read the coord space from `GetViewport`.** On this install (with
`supersampling=2`) device `GetViewport` ([41]) returns the **supersampled
back-buffer = 5120×2800 = 2× the coord space**, not the 2560×1400 you draw in.
Trusting it placed elements at 2× their intended position (a centred box landed
bottom-right). The draw coord space here is 2560×1400; the supersampled buffer is
that × the supersampling factor. Until the factor is read at runtime, treat the
coord space as fixed (2560×1400 here) and only use GetViewport for diagnostics:
```c
u32 dev = host->vtbl[2](host);                 // IDirect3DDevice8
// IDirect3DDevice8::GetViewport == vtable index 41
struct VP { unsigned x,y,w,h; float minz,maxz; } vp;
dev->vtbl[41](dev, &vp);                        // vp.w,h = SUPERSAMPLED buffer (5120x2800), NOT the coord space
```
Other handy device indices: `[8]` GetDisplayMode, `[14]` Reset, `[15]` Present,
`[34]` BeginScene, `[35]` EndScene.

Centre an element: `x = (vp.w - elemW)/2`, `y = (vp.h - elemH)/2`. For text,
get `elemW/elemH` from `TextObject::GetExtents` ([12]).

---

## 5. Enumerating everything on screen (debug)

Both handlers hold a `std::vector<shared_ptr<Object>>`:
- **TextHandler**: begin=+0x44, end=+0x48, cap=+0x4c.
- **PrimitiveHandler**: vector @ +0xb8.
- stride 8 bytes per element; `element[0]` = object pointer.
- object **name** = std::string (SSO) at `obj+0x04`.

`debug::dump_list()` auto-finds the vector (scans for begin≤end≤cap, 8-aligned)
and logs every object with name, visibility and coords — including other plugins'
objects and orphans. Invaluable for "what's actually drawn".

---

## 6. Runtime ↔ Ghidra address mapping

Hook.dll loads at a runtime base (e.g. `0x64CC0000`); the Ghidra project uses the
preferred base `0x10000000`. So `GhidraAddr = RuntimeAddr - (RuntimeBase - 0x10000000)`.
The plugin can read the live base with `GetModuleHandleA("Hook.dll")` and log
method RVAs (`debug::dump_vtable`); then `GhidraAddr = 0x10000000 + RVA`.

---

## 7. Workflow & gotchas

- **Build**: MSVC 2017 (x86), `cl /LD /O2 /MT /I include sandbox.cpp /link /DEF:sandbox.def user32.lib kernel32.lib`. See `build.bat` / `deploy.bat`.
- **Iterate**: `//unload AioTest` in game → `deploy.bat` → `//load AioTest`. Windower keeps a loaded DLL **file-locked until //unload** (and never releases "core" plugins), so always unload before deploying. Single name `AioTest` works once IsCore returns 0.
- **Orphans**: objects you `create()` but never `remove()` stay in the handler lists and keep rendering after unload; cleared only by a **game restart**. Always remove on unload (slot 3).
- **Live tuning without rebuilds**: poll a config file (`plugins\aio_cfg.txt`) from the per-frame hook and apply it — lets you move/resize/recolour things by editing a text file. (Used to calibrate the geometry.)
- **Performance**: 1000 animated primitives (rect+color rewritten every frame) = **locked 59-60 fps, zero drop**. Solid-colour quads are ~free. Huge headroom for elaborate HUDs.

---

## 8. Minimal working example

```cpp
#include "windower_plugin.h"   // exports + 34-slot vtable, include ONCE
#include "windower.h"
using namespace windower;
static PluginManager host; static PrimitiveObject bg; static TextObject lbl;

const char* aio_plugin_name()        { return "AioTest"; }
const char* aio_plugin_description() { return "demo"; }
void aio_plugin_init(PluginManager h) {
    host = h;
    auto bgC = h.primitive_handler();
    bg = bgC.create("bg");
    bg.color(220,20,20,30); bg.rect(1120,687,320,26); bg.visible(true);   // pos+size
    lbl = h.text_handler().create("lbl");
    lbl.font("Arial"); lbl.size(15); lbl.color(255,255,255,255);
    lbl.text("HP"); lbl.pos(1130,690); lbl.visible(true);
}
void aio_plugin_render() { /* fires every frame: animate here */ }
void aio_plugin_unload() {                                                // remove on unload!
    host.primitive_handler().remove(bg);
    host.text_handler().remove(lbl);
}
void aio_plugin_command(const char*) {}
```

---

## 9. Game data: player memory & party packets  (REVERSED & WORKING, 2026-06-26)

Until host `ffxi[7]` is walked, AioHUD sources live data two ways: the **local
player** from memory (always present + accurate) and the **other party members**
from inbound packets (the game never sends you your *own* party packet). Code:
`model/game_mem.cpp` (memory), `model/party_state.cpp` (packets).

### 9a. Local player struct — read from memory

Pointer chain (this install, `LuaCore.dll 2.6.8.2`):
```
G      = *(LuaCore.dll + 0x1C8400)      // data root
player = *(G + 0x3C)                    // local player struct
```
**Validity gate:** read `max_hp` first; if `0` or `> 0x100000` the struct isn't
populated yet (mid-zone) — bail and keep last good values. Right after a zone the
**vitals populate before the name/job strings**, so gate on vitals, not the name.

Field offsets (relative to `player`), all confirmed against a live hexdump
(`//aio dump <player> 200`):

| offset | type | field | notes |
|---|---|---|---|
| `+0x00` | u32 | server id | e.g. `0x0006BD0B`; matches the packet member id |
| `+0x08` | char[] | **name** (ASCII, NUL-term, ≤18) | "Tetsouo" |
| `+0x1C..+0x5C` | u16[32] | **own buffs** (status icons) | `0xFF` = empty slot; ids are FFXI status ids. See `read_player_buffs`. The `0x076` packet never carries self → read here. |
| `+0x5C` | u32 | HP | |
| `+0x60` | u32 | **max HP** | the validity gate |
| `+0x64` | byte | HP % (0..100) | the game's own %, adapts to current max |
| `+0x68` | u32 | MP | |
| `+0x6C` | u32 | max MP | |
| `+0x70` | byte | MP % (0..100) | |
| `+0x74` | u32 | TP (0..3000) | |
| `+0x7D..+0x92` | byte[22] | **job-level array** (WAR..RUN) | all `0x63`=99 on a maxed char — see the trap in §9c |
| `+0x94` | u32 (low byte) | **main job id** | `4`=BLM. Ids: 1 WAR…22 RUN (see `JOBS[]` in `party_state.cpp`) |
| `+0x98` | u32 | main job **level** | `99` — NOT the job (cost us an afternoon, §9c) |
| `+0x9C` | u32 (low byte) | **sub job id** | `20`=SCH |
| `+0xA0` | char[] | linkshell name (ASCII) | "Harfang des Neiges" |
| `+0xB4..` | u16[] | combat/magic skills | `0x80xx` packed, not yet decoded |

### 9b. Party member packets

`packet_in` (IPlugin slot 11) gives `b` = the **decoded** inbound packet (a safe,
fully-populated buffer). `id = *(u16*)b & 0x1FF`; size = `((hdr>>9)&0x7F)*4`.

**`0x0DD` — party member full update** (`PartyState::on_dd`). One member per packet:

| offset | type | field |
|---|---|---|
| `+0x04` | u32 | member server id (key; `0` → ignore) |
| `+0x08` | u32 | HP  (`0` ⇒ member is **out of our zone** → no vitals) |
| `+0x0C` | u32 | MP |
| `+0x10` | u32 | TP |
| `+0x14` | u32 | flags (party/alliance leader, quarter-master bits) |
| `+0x1D` | byte | MP % |
| `+0x1E` | byte | HP % |
| `+0x20` | u16 | member zone id (set when out of our zone) |
| `+0x22` | byte | main job |
| `+0x24` | byte | sub job |
| `+0x28` | char[] | name (ASCII, ≤19) |

**`0x0DF` — vitals refresh** (`PartyState::on_df`): `+0x04` id, `+0x08` HP,
`+0x0C` MP, `+0x10` TP. Only updates a member already known from a `0x0DD`.

> Note the **same field, different offset** between sources: HP%/jobs sit at
> different places in the packet (`+0x1E`,`+0x22`/`+0x24`) vs memory
> (`+0x64`,`+0x94`/`+0x9C`). Don't copy one offset set onto the other.

**Alt — party struct in memory** (used by `//aio chain`): `a=*(LuaCore+0x1C8400)`,
`b=*(a+0x248)`, `party=*b` → this is the **allianceinfo_t** (leaders/counts, see §9d),
NOT a member array. Its `+0x18` pointers go to UI/menu structs, not clean member data
(an early wrong turn). The real member array is §9d.

### 9c. Two traps that cost real time (don't repeat them)

1. **Self name renders as `~??????` — dangling pointer, not a bad offset.**
   `Party::build_rows()` had `PlayerInfo me;` as a **stack local** and
   `fill_self()` stored `row.name = me.name`. The rows are consumed by the
   **caller** (`Party::draw`) *after* `build_rows` returns → `me` is gone → the
   `const char* name` dangles → garbage. Other members were fine because their
   `name` points into the **global** `g_party` (stable). **Fix:** make `me`
   `static`. **Rule:** `Row.name` is a *non-owning* pointer — only ever assign it
   something whose lifetime ≥ the frame (a global, a string literal, or `static`).

2. **Job badge empty — read the job ID, not the job LEVEL.** `read_player` first
   read main job at `+0x98`, which is the main-job **level** (`99`); `job_abbr(99)`
   is out of range → `""` → no badge. The `+0x7D..+0x92` all-99 **job-level array**
   is exactly what makes a stray `0x63` look like a "job". The real ids are the
   u32-aligned fields **`+0x94` (main)** and **`+0x9C` (sub)**, with the level
   wedged between them at `+0x98`. When an RE'd "job" reads ~99, you're on a level
   field — step back to the aligned id.

**Debugging recipe** (all via `//aio …`, plugin stays **loaded** — no unload):
`//aio vit` logs the `player` pointer; `//aio dump <player> 200` hexdumps it;
`//aio party` logs each member's `id` + `flags` + the resolved leaders;
`//aio chain` walks the alliance struct; `//aio find <hexid>` scans memory for a 32-bit
value (how we located the member array). Unload is ONLY needed to redeploy a rebuilt DLL.

### 9d. The party member ARRAY in memory — instant full party at load (the real table)

The packet path only fills members the game *sends*; loading mid-party shows nothing
until packets arrive. The fix is to read the game's own **party member array** at load.
On **retail** this is the documented **Ashita `partymember_t`** (stride **`0x7C`**, **18
slots**: 0..5 = your party, 6..11 / 12..17 = alliance parties 2 / 3). Verified field-by-
field in-game (`model/party_state.cpp` `load_from_memory`).

**Anchor:** `g = *(LuaCore+0x1C8400)`; `pp = *(g+0x248)` (points **4 bytes into**
member[0]); `member[0] = pp - 4`; `member[i] = member[0] + i*0x7C`. Self-validate:
`member[0].ServerId` must equal the player id (else don't trust it).

| member offset | field | notes |
|---|---|---|
| `+0x0A` | name (char[18], ASCII NUL-term) | |
| `+0x1C` | u32 server id | the anchor-validation key |
| `+0x28` | u32 HP | |
| `+0x2C` | u32 MP | |
| `+0x30` | u32 TP | |
| `+0x34` | byte HP % | |
| `+0x35` | byte MP % | |
| `+0x36` | u16 zone id | member's zone (≠ yours ⇒ out of zone; offzone members are kept here) |
| `+0x3C` | u32 **flag mask** | quartermaster = **bit `0x10`** (verified: the bit moved to the member given QM) |
| `+0x71` | byte **main job id** | 1 WAR … 5 RDM … 22 RUN (verified: Kaories RDM=5, Tetsouo BLM=4) |
| `+0x72` | byte main job level | `0x63` = 99 |
| `+0x73` | byte **sub job id** | (verified: Tetsouo SCH=20, Kaories WHM=3) |

Jobs **are** in this struct (`+0x71`/`+0x73`, reversed 2026-06-28 from a live alliance dump
— an earlier note wrongly read `0`). Reading them here gives correct job badges for **every**
member of all three boxes instantly, with no dependence on packet timing. `load_from_memory`
runs every frame, so the roster + vitals + jobs stay live. Trust members (no job byte) fall
back to a name → job DB.

**Leadership is NOT a member flag — it's server-id matching against `allianceinfo_t`.**
`allianceinfo = *(pp)` (= `*(g+0x248)` dereferenced; this is the `//aio chain` "party"):

| allianceinfo offset | field |
|---|---|
| `+0x00` | u32 alliance leader server id (0 = no alliance) |
| `+0x04` | u32 **party-1 leader** server id (your party) |
| `+0x08` / `+0x0C` | u32 alliance party 2 / 3 leader ids |
| `+0x10`/`+0x11`/`+0x12` | byte party 1/2/3 visible |
| `+0x13`/`+0x14`/`+0x15` | byte party 1/2/3 member count |

A member is *party leader* iff its id == `+0x04`, *alliance leader* iff == `+0x00`.
Read live each frame (`read_party_leaders`) → the `*`/`^` markers follow the real role
automatically when leadership changes. Quartermaster is the only role that lives in the
member flag mask (`+0x3C` bit `0x10`), not here.

### 9e. Target & SUB-target struct  (REVERSED & WORKING, 2026-06-27, retail)

This drives the gold "loupe" bar (main target `<t>`) **and** the ocean-blue bar (sub-target
`<st>`/`<stpc>`). Layout matches **Ashita's `target_t`** (`plugins/sdk/ffxi/target.h`):
two `targetentry_t` (40 bytes / `0x28` each) + a sub-active flag.

```
*(FFXiMain.dll + 0x57876C)        -> target_t base (HEAP ptr ; ASLR-shifted, resolve at runtime)
  target_t + 0x00  u32  Targets[0].Index       (entity index)
  target_t + 0x04  u32  Targets[0].ServerId    = ACTIVE RETICLE  (sub when <st> open, else main)
  target_t + 0x08  u32  Targets[0].EntityPointer
  target_t + 0x0C  u32  Targets[0].ActorPointer
  target_t + 0x28  u32  Targets[1].Index
  target_t + 0x2C  u32  Targets[1].ServerId    = LOCKED MAIN  (valid while a <st> cursor is up)
  target_t + 0x50  u32  flags ; bit 0x00010000 = sub-target CURSOR OPEN (set only while selecting)
```
`0x04000000` is the "nothing targeted" sentinel for a ServerId. Decode (see `read_target`):
- bit clear → main = `Targets[0].ServerId`, sub = none.
- bit set   → **sub = `Targets[0].ServerId`** (the cursor), **main = `Targets[1].ServerId`** (locked).

⚠️ Use the `+0x50` bit `0x00010000`, **NOT** the byte at `+0x78`. `+0x78` also goes 1 on
cursor-open but is STICKY: it stays 1 after you *confirm* the action (clears only on cancel),
leaving the sub bar stuck on. The `+0x50` bit clears on **both** confirm and cancel.

**Why not the obvious `FFXiMain+0x487F60`?** That static is only a *thin cache* of the
active reticle's `{index@+0x5C, id@+0x60}` (the value at `+0x58` is an unrelated constant
pointer). It follows the `<st>` cursor too, so it CANNOT tell main from sub, and it holds no
`Targets[1]`/flag. The real two-entry struct is the heap `target_t` above.

**How it was found (probe chain, all via `//aio …`):**
1. `//aio tgt2` — Tab a party member, then scan all memory for that ServerId. The real
   `target_t` is the hit with `Index@-4` small **and** a valid `EntityPointer@+4` **and**
   `Targets[1].ServerId@+0x28` is *another* party id. Then scan for a STATIC (FFXiMain)
   pointer holding that heap base → the `0x57876C` anchor. (Cap the id-scan high: the id
   appears 1000s of times; `target_t` can be past the 1024th hit.)
2. `//aio sub` — per-frame change-watcher over `target_t[0..0x80]`. Open then cancel a
   `<stpc>` cursor: the dword that flips `0↔1` on open/cancel is the flag (`+0x78`).
These probe commands live in `plugin/aiohud.cpp` (kept for re-locating after a client patch).

> **TODO — the party-DISTRIBUTION cursor (`//aio pcur` probe).** When you open
> *Menu → Party → Distribution → Quartermaster/Lottery*, the game spawns a party-member picker.
> Probed solo (2026-06-28), it sets **no world target at all**: `target_t` Targets[0/1] AND the
> LuaCore target struct `*(g+0x30)+0x04` both stay `0x04000000` ("nothing"); no party server-id
> appears anywhere. So the hovered member is a **bare INDEX** in the menu/cursor state, not an id.
> `*(g+0x30)+0x74` only blinks `0↔1` (cursor flash). **This CANNOT be reversed solo:** the lone
> member is index `0`, an all-zero byte indistinguishable from uninitialised memory. It needs
> **≥2 party members** — hover index 1+ and the field holding a non-zero index lights up at once
> (one clean cycle). Probe + dump live in `plugin/aiohud.cpp` (`g_pcur_probe`). Goal: surface it
> as a selection frame on the hovered member, like `<st>`.

### 9f. Party cast bar — the `0x028` action packet  (WORKING, 2026-06-27)

Shows the spell a party member is casting (name + progress), driven by the **inbound `0x028`
action packet** (the same source every Lua addon uses via `windower.packets.parse_action`).
Native parse in `party_state.cpp on_action()`. Bit layout (LITTLE-ENDIAN bit packing ; offsets
in BITS from byte 0 of the packet, verified vs Ivaar's `actions.lua` gist):

```
actor id            @ bit 40  (byte 5), 32 bits   -> caster's server id (match to a party member)
category            @ bit 82,            4 bits    -> 8 = BEGIN casting (magic), 4 = finish
target[0].action[0].param @ bit 213,    17 bits    -> SPELL ID (on a category-8 begin)
```
`getbits(p, off, w)` = read 8 bytes LE from `off/8`, `>> (off&7)`, mask `w` bits.

- On **category 8**: look up `spell_info(id)` (name + base `cast_time`) and start a cast slot.
- **Do NOT clear on category 4.** On this client the cat-4 "finish" arrives *one packet later,
  instantly*, so clearing on it kills the bar before it ever shows. The bar instead lives on its
  own **duration** (`cast_time` from the spell table) + a short fade, and self-expires.

**Spell table:** `src/model/spells_gen.h` (957 spells, id→en+cast_ms) is AUTO-GENERATED from
Windower `res/spells.lua` by `scratchpad/gen_spells.py` — regenerate if the client adds spells.

⚠️ **Gotcha that cost an hour:** the cast state must NOT live in `PMember` — `hud.cpp` calls
`party().load_from_memory()` EVERY frame, which does `pm = PMember()` and would wipe a
packet-set field next frame. Cast state lives in a separate `PartyState::casts_[18]` array
(keyed by server id) that the per-frame roster refresh never touches. `cast_label(id,&pct,&alpha)`
reads it (with pop-in / depop fade) for the UI.

### 9g. Action-menu info box — Magic / Job Ability / Weapon Skill  (WORKING, ZERO-TAP, 2026-06-27)

A small box, right-aligned, ABOVE the native party, that mirrors the game's MP-Cost/Next dialog
while you hover an action in the **Magic**, **Job Ability** or **Weapon Skill** menu. Drawn in
`party.cpp` (end of `render()`), fed by `read_action_menu(int& type, unsigned& id)` in
`game_mem.cpp` (`type` : 1=spell, 2=job ability, 3=weapon skill).

Statics in FFXiMain :
```
+0x5EED6C  u32  live-menu POINTER : 0 while no menu is open, a heap ptr while ANY menu is open.
                *(ptr + 0x04) = the menu DEFINITION pointer (the def — see the menu NAME below).
+0x634F28  u32  "examined SPELL" id      (the Magic menu writes the highlighted spell here).
+0x634590  u32  "examined ACTION" id in the "ability " menu, in FFXI's UNIFIED id space :
                   >= 0x200 -> Job Ability  (real id = value - 0x200)
                   <  0x200 -> Weapon Skill (raw id, no offset)
                   0xFFFFFFFF / 0 -> not populated yet.
+0x63449C  u32  examined action's recast state : 0 = READY, nonzero = ON RECAST (value oscillates,
                it is NOT the remaining time — the exact "Next" countdown is not decoded yet).
```

**ZERO-TAP menu identification (the key 2026-06-27 breakthrough).** The def carries the menu's
**internal name inline** — two 8-byte fields at `def+0x46` : a constant `"menu    "` tag, then the
**menu name at `def+0x4E`** :
```
"magic   "  -> spell list      (read 0x634F28)
"ability "  -> Job Ability AND Weapon Skill list (SAME menu name; disambiguate by 0x634590 range)
"abiselec"  -> the Abilities CATEGORY selector (no item examined -> 0x634590 = 0xFFFFFFFF) -> ignore
```
This name is **stable across sessions** (it's read from the def, not the per-session heap pointer
value), so we know the menu type **without moving the cursor**. It replaces the old "learn which def
moves which examine cache" trick (which cost one cursor tap per menu per session). `read_action_menu`
self-validates by checking `def+0x46 == "menu"` first. Other menus (Items/Trade/…) have other names
and are naturally ignored. Reversed with the `//aio menu` probe (dumps the menu object + def and
chases every dword as a char* to surface the ASCII name strings — in `aiohud.cpp`).

**Tables:** `spells_gen.h` + `abilities_gen.h` + `weapon_skills_gen.h` (957 spells / 626 abilities /
241 weapon skills). `weapon_skills_gen.h` (id→en) is generated by `scripts/gen_ws.py` from
`D:\Windower Tetsouo\res\weapon_skills.lua`; the spell/ability tables came from the now-lost
`gen_actions.py`. All three use a binary search (`spell_info`/`abil_info`/`ws_info`, sorted by id).

**Job-Ability recast ("Next") — reversed 2026-06-27 from LuaCore's `get_ability_recasts` (`FUN_1006FF00`).**
DEAD END first: `FFXiMain+0x63449C` is NOT the hovered action's recast — it's sticky (keeps the last
nonzero value), so it wrongly showed Elemental Seal's timer on every JA. The REAL source is the client's
32-slot recast table, hung off the SAME data root we already use, `g = *(LuaCore.dll + 0x1C8400)`:
```
*(g + 0x22C)  -> int32[32]   remaining recast in 1/60 s (frames).  seconds = timer / 60  (divisor 60.0
                             is a literal in LuaCore; Windower builds recasts[id] = timer/60).
*(g + 0x230)  -> stride-8 entries, byte[0] = that slot's recast_id.
```
We invert Windower's loop: take the highlighted JA's `recast_id` (from `abilities_gen.h`), scan the 32
slots for an ACTIVE one (timer>0) whose id matches → remaining seconds (`ability_recast_sec()` in
`game_mem.cpp`); 0 = ready. The box shows `Next m:ss` (amber) for a JA on cooldown — matches the game's
own "Next" exactly. Weapon skills have no recast (TP-gated) → they show live TP instead.

How we got the structure: FFXiMain.dll is **POL-packed** (encrypted `.text` on disk — static Ghidra
sees no code/refs; see the project memory note), so we dumped the live module (`//aio grabmod`). But the
clean win was reversing **LuaCore.dll** (unpacked, implements `get_ability_recasts`/`get_spell_recasts`)
instead. Registration site found via the `get_ability_recasts` string xref; the impl is the cclosure
pushed just before the name (off-by-one: `FUN_1006FF00`, not the next one).

**Spell recast ("Next" for Magic) — reversed 2026-06-28 from `get_spell_recasts` = `FUN_1006FE80`.**
(The old `FUN_100732B0` guess was WRONG — that one is `get_abilities`, it returns the learned
job_abilities/job_traits/weapon_skills/mounts bitmaps. The real impl is the cclosure pushed just before
the `"get_spell_recasts"` setfield: `FUN_1006FE80`.) Far simpler than abilities — no 32-slot scan, a
flat array hung off the same root:
```
base = *(g + 0x234)  -> ushort[1024], indexed DIRECTLY by recast_id, remaining recast in 1/60 s.
                        seconds = base[recast_id] / 60.  (0x234 sits right after 0x22C/0x230.)
```
`spell_recast_sec(recast_id)` in `game_mem.cpp` reads `base[recast_id]` (low ushort of a 32-bit
safe_read), 0/garbage → ready. `recast_id` from `spells_gen.h` (`SpellRow::recast_id`). The box shows
`Next m:ss` (amber) when a spell is on cooldown, else its MP cost. Weapon skills have no recast
(TP-gated) → they show live TP instead.

Done: Magic shows name + Next(recast)/MP, Job Ability shows name + Next(recast), Weapon Skill shows name
+ live TP — all **zero-tap**. (No per-WS TP threshold exists on retail: every WS is usable at 1000 TP;
1000/2000/3000 are the universal damage/effect tiers, so "live TP, green ≥ 1000" is already correct.)

### 9h. Party-member buffs — the `0x076` packet  (WORKING, 2026-06-28)

Status icons drawn to the **left of each party row** (2 stacked rows, mirror of
`design`'s `.pm-buffs`). Two **separate** sources — the `0x076` packet never carries
the local player, so **self** comes from memory and **everyone else** from the packet.

**Self buffs — memory** (reversed from LuaCore `get_player`, `FUN_10072040`). The same
`player = *(G + 0x3C)` struct holds a **32-entry `u16` array at `player+0x1C`**
(`[+0x1C, +0x5C)`); `0xFF` = empty slot. `get_player` loops it and appends every
non-`0xFF` id under the lua key `buffs`. Code: `read_player_buffs` (`game_mem.cpp`).

**Other members — packet `0x076`** (`PartyState::on_076`). `b` = decoded packet, same
base as `0x0DD` (payload at `+0x04`). **5 member slots of 48 (`0x30`) bytes:**

| per-slot field | offset (slot `k`) | notes |
|---|---|---|
| member server id | `k*48 + 4` (u32) | `0` = empty slot; local player never appears |
| buff `i` low byte | `k*48 + 20 + i` (i=0..31) | low 8 bits of the status id |
| buff `i` high 2 bits | `(p[k*48 + 12 + i/4] >> 2*(i%4)) & 3` | 8 bytes pack 32×2 high bits |

`buff = low + 256*high2`; `255` = empty. Buffs are kept in a transient
`PartyState::buffs_[18]` keyed by server id (NOT part of the cached roster — they
refresh every `0x076`); the UI reads them via `buffs_for(id)`.
*Credit: Kenshi/PartyBuffs + Byrth/GearSwap, via XivParty's `0x076` parse.*

**Icons:** a single atlas `assets/buff_atlas.raw` (1024×640 BGRA, 32-col grid of 32px
cells, id → cell `(id%32, id/32)`), built by `scripts/gen_buff_atlas.ps1` from
XivParty's `assets/buffIcons/*.png`. Ids ≥ 640 (outside the atlas) are skipped.

---

## 10. Clean 2D rendering in Direct3D 8 (technical reference)

Everything here draws as **pre-transformed quads (FVF XYZRHW)** straight through the
device — no D3DX, no sprite/font helper. That path is fast and flexible but has exact
rules; get one wrong and you get blur, dropped edges, or seams. Sources: MS "Directly
Mapping Texels to Pixels", "Texture Filtering with Mipmaps", GameDev.net D3D8 threads.

### 10a. The HALF-PIXEL rule (most important) — `x -= 0.5, y -= 0.5`
A pixel is a **point at the centre** of its cell; screen (0,0) is that centre, so the
top-left *corner* is (-0.5,-0.5). With XYZRHW you bypass all transforms, so YOU must
shift every vertex by **-0.5 in x and y**. If you don't:
- the rasterizer's **top-left fill rule drops the bottom row / right column** of a quad
  whose edges sit on integer pixel lines → the classic *"cut off at the bottom/right"*
  (this is exactly what cut our marker dots);
- sampled textures land **between four texels** → bilinear averages them → **blur**.
**Applied here in every `draw.cpp` helper** (`tquad*`, `glow_quad`, `cap_quad`,
`grad_quad`, `seg_soft`, `disc`). `font.cpp` used to do `-0.5` itself — now it must NOT
(the helper does it) or text double-shifts. Add the `-0.5` to any NEW vertex builder.

### 10b. Filtering — LINEAR + mips for anything scaled
- Set all three: `D3DTSS_MINFILTER`, `MAGFILTER`, `MIPFILTER = D3DTEXF_LINEAR`.
- **Build a mip chain** for any texture shown smaller than native (icons, dots): mips +
  `MIPFILTER_LINEAR` = clean minification, no shimmer. We use `make_texture_argb_mip`.
- `D3DTEXF_POINT` ONLY for true 1:1 pixel-art at integer positions, never scaled/rotated.
- (Filtering is a **sampler/stage** state — re-set it in your draw pass; the previous
  widget may have left POINT.)

### 10c. Addressing — CLAMP for sprites; inset atlas UVs
- A standalone sprite/icon/dot: `D3DTSS_ADDRESSU/ADDRESSV = D3DTADDRESS_CLAMP` so the
  edge texel doesn't WRAP to the opposite side (a faint line on one edge).
- A **texture atlas** (our font glyphs): leave a 1px **gutter** between cells AND inset
  the UVs by **half a texel** (`+0.5/W .. (cellW-0.5)/W`) so LINEAR filtering can't bleed
  a neighbour in. Bleeding shows as faint fringes around glyphs/sprites when scaled.
- UV-scroll effects (liquid) use `WRAP` deliberately — that's the exception.

### 10d. Alpha blending — straight vs premultiplied
- **Straight alpha** (what we use): `ALPHABLENDENABLE=1`, `SRCBLEND=SRCALPHA`,
  `DESTBLEND=INVSRCALPHA`. Formula `out = src.rgb*a + dst*(1-a)`. Simple; fine for solid
  icons/dots/text. Downside: bad at very soft edges over bright bg (dark halo) and can't
  mix add+over in one draw (D3D8 has **no** `SEPARATEALPHABLENDENABLE`).
- **Premultiplied alpha**: bake `rgb *= a` into the texture, draw with
  `SRCBLEND=ONE`, `DESTBLEND=INVSRCALPHA` → `out = src.rgb + dst*(1-a)`. Cleaner edges,
  and additive (glow) + normal in the same sheet (alpha 0 + bright rgb = additive). Worth
  it if marker/glow edges ever look haloed.
- **Tint a white silhouette**: `COLOROP=MODULATE`, `COLORARG1=TEXTURE`, `COLORARG2=DIFFUSE`
  + vertex colour = tint (texture×colour). For a full-colour PNG: tint = white
  (`0xFFFFFFFF`) → shows its own colours. Alpha likewise `ALPHAOP=MODULATE`(tex×diffuse)
  or `SELECTARG1 TEXTURE`.

### 10e. Textures: format, pool, size
- `D3DFMT_A8R8G8B8` (32-bit straight ARGB), `D3DPOOL_MANAGED` (survives a device **reset**;
  D3D restores it). Forget (don't release) handles on a device **recreate** — see the
  Widget `on_device_lost`/`ensure`/`dispose` lifecycle.
- Prefer **power-of-two** dimensions (32/64/128…): universally safe + required for a full
  mip chain. Non-PoT can fail or disable mips on older hardware.
- **Zoning gotcha (§3c):** a MANAGED texture's **alpha mis-samples as ~255 while a zone
  loads**. For anything whose translucency matters during loads, drive blend alpha from
  the **vertex/diffuse**, not the texture alpha (or premultiply into RGB). Tiny opaque
  markers can ignore it (a brief square during the load screen only).

### 10f. State hygiene (multi-widget)
The HUD wraps all widgets in ONE state block (save→draw→restore). But **within** that
block, each widget/pass must set the states it relies on (FVF, blend, tex-stage,
filtering, addressing) — the previous pass left its own. Always: set FVF for your vertex
type (`0x44` colour / `0x144` textured), bind/unbind your texture (`dSetTex(...,0)` after),
and don't assume defaults. `ZENABLE=0`, `CULLMODE=NONE`, `LIGHTING=0` for flat 2D.

### 10g. Coordinate space (recap of §4)
Draw in the logical **2560×1400** space here (NOT the supersampled back-buffer that
`GetViewport` reports). Supersampling=2 actually helps 2D edges (downsampled MSAA-like),
so geometry (e.g. the `disc` triangle fan) comes out smoother than at native res.
