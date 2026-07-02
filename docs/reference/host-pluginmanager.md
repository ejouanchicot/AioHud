---
title: The Host — PluginManager
summary: The PluginManager pointer passed to Init and its nine vtable getters that hand back the device and service interfaces.
source: REFERENCE.md §2
---
# The host: `PluginManager`  (RTTI `.?AVPluginManager@@`)

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

## See also
- [Plugin DLL contract (ABI)](plugin-abi.md)
- [Service interfaces](service-interfaces.md)
- [Direct D3D8 rendering](d3d8-rendering.md)
