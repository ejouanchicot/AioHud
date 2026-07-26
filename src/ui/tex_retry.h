// tex_retry.h -- bounded, time-spaced lazy texture load. Replaces the give-up-once `if (!tex && !tried) { tex =
// load(); tried = true; }` latch that turned a TRANSIENT miss (device not ready right after //load or a zone-in,
// an .raw briefly held by the updater / AV / Controlled Folder Access) into a PERMANENT missing icon for the whole
// session (CLAUDE.md rule 10). A genuinely-absent asset stops after ~12 tries so a broken install doesn't hammer.
//
// PER-HANDLE state, EXCEPT the buff atlas. Each widget keeps its OWN texture handle + its OWN TexRetry, and
// disposes exactly what it owns. v1.0.47 extended that rule to the buff atlas too, because the coherence pass
// showed consolidating those loaders "would triple-Release" -- correct for the design it evaluated: share one
// handle VALUE between owners that each still call release_texture, and the second Release hits a dead pointer.
// That hazard belongs to "shared value, several owners". Superseded 2026-07-26 by a SINGLE-owner design instead
// (ui/buff_atlas.cpp): consumers only READ the handle and never release it, so the whole program holds exactly
// one release_texture call site for that texture -- the hazard is removed by construction, not managed. It was
// worth doing because that one sheet was held by NINE handles (party x3, player, target, timers/debuffs, and
// three Help samples) at 2.5 MiB each. Nothing else is shared; keep this per-handle for every other texture.
//
// The `!r.nextMs` guard is load-bearing : nextMs starts 0 ("try now"), and comparing a raw GetTickCount() against 0
// goes NEGATIVE past ~24.8 days of uptime -- the exact bug that silently killed the buff atlas AND (once) the gear
// back-off. `| 1` on the stamp keeps it off the 0 sentinel.
#pragma once
#include "gfx/texture.h"
#include <windows.h>

namespace aio {

struct TexRetry { unsigned char tries = 0; unsigned nextMs = 0; };

inline bool tex_retry_due(u32 tex, const TexRetry& r) {
    if (tex || r.tries >= 12) return false;
    return !r.nextMs || (int)(GetTickCount() - r.nextMs) >= 0;
}
inline void tex_retry_note(u32 tex, TexRetry& r) {   // call after an attempt : a miss backs off ~300 ms
    if (!tex) { ++r.tries; r.nextMs = (GetTickCount() + 300u) | 1u; }
}

// Convenience wrappers for the two raw loaders (the mip one is used by a couple of icons).
inline u32 ensure_raw_tex(u32 dev, u32& tex, TexRetry& r, const char* path, int w, int h) {
    if (tex_retry_due(tex, r)) { tex = load_raw_texture(dev, path, w, h); tex_retry_note(tex, r); }
    return tex;
}
inline u32 ensure_raw_tex_mip(u32 dev, u32& tex, TexRetry& r, const char* path, int w, int h) {
    if (tex_retry_due(tex, r)) { tex = load_raw_texture_mip(dev, path, w, h); tex_retry_note(tex, r); }
    return tex;
}

} // namespace aio
