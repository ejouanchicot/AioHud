---
title: Minimal Working Example
summary: A complete minimal plugin that draws a solid background rect and an HP label and removes both on unload.
source: REFERENCE.md §8
---
# 8. Minimal working example

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

## See also
- [Plugin DLL contract (ABI)](plugin-abi.md)
- [Service interfaces](service-interfaces.md)
- [Workflow & gotchas](workflow-gotchas.md)
