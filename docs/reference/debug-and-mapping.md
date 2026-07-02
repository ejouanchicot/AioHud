---
title: Debug Enumeration & Address Mapping
summary: Dumping every on-screen text/primitive object from the handler vectors, plus converting runtime Hook.dll addresses to Ghidra addresses.
source: REFERENCE.md §5, §6
---
# 5. Enumerating everything on screen (debug)

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

## See also
- [Service interfaces](service-interfaces.md)
- [Coordinate system](coordinates.md)
- [Workflow & gotchas](workflow-gotchas.md)
