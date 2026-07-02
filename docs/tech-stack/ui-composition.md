---
title: UI composition — immediate-mode controls + 9-slice skin
summary: How AioHUD's config UI is an immediate-mode GUI with per-control eased springs, why live boxes use retained zones instead, and the 9-slice window skin.
source: TECH_STACK.md §9b
---
# UI composition — immediate-mode controls + 9-slice skin

**Immediate-mode config UI.** `ui/config_page.cpp` is an **immediate-mode GUI** (IMGUI-style): each
control is drawn *and* hit-tested *and* returns its click in one call per frame
(`if (row_selector(...)) …`, `if (toggle_chip(...)) …`) — no retained widget tree, `ui_config()` is the
single source of truth. Hover animation is a per-control eased spring keyed by a small integer `uid`
(the `ease(uid, target)` slots — keep them unique per control, or two controls share one animation, as
the Job-Badge/Gauge-Style stepper collision showed). IMGUI's tradeoff — trivial to add a control, harder
to freely reposition — is exactly why the *live game* boxes use a separate retained layout (zones), not
IMGUI.

**9-slice window skin.** The optional FFXI-style window frame (`ui/window.cpp`, `skinTheme`) is a
**9-slice / nine-patch**: 4 corners stay fixed, the 4 edges stretch/tile along one axis, the centre
fills — one small skin asset dresses a box of any size without distortion.

**References.**
[About the IMGUI paradigm (Dear ImGui wiki)](https://github.com/ocornut/imgui/wiki/About-the-IMGUI-paradigm) ·
[Immediate vs retained UI (Collin Quinn)](https://collquinn.gitlab.io/portfolio/my-article.html) ·
[Proving IMGUIs are performant (forrestthewoods)](https://www.forrestthewoods.com/blog/proving-immediate-mode-guis-are-performant/) ·
[9-slice scaling (Wikipedia)](https://en.wikipedia.org/wiki/9-slice_scaling) ·
[9-slicing for scalable sprites (Unity Learn)](https://learn.unity.com/tutorial/using-9-slicing-for-scalable-sprites)

## See also
- [Fonts — GDI bundled, atlas-rasterized](fonts.md)
- [HUD & UI design references](hud-design.md)
- [Widgets](../architecture/widgets.md)
- [Edit zones](../design/edit-zones.md)
- [Layout JSON](../formats/layout-json.md)
