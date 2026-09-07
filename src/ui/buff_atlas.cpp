// buff_atlas.cpp -- the ONE owner of the shared status-icon atlas texture (assets/buff_atlas.raw).
//
// WHY THIS EXISTS. NINE handles held this same 1024x640 BGRA sheet, each loaded independently: party x3 (the
// party box + the two alliance boxes are the same widget class), player, target, timers/debuffs, and three Help
// samples (timers, debuffs, and the target one owned by the config page). 2.5 MiB per handle -- up to ~23 MB of
// identical pixels resident, of which 4 to 6 copies are live in an ordinary session.
//
// WHY IT IS SAFE NOW. v1.0.47 (df59553) deliberately kept one handle per owner because "the coherence pass
// showed that would triple-Release" -- and it was RIGHT about the design it evaluated: share the handle VALUE
// between owners that each still call release_texture in their own dispose(), and the second Release hits a
// dead pointer. That hazard belongs to "shared value, several owners". Here there is ONE owner: consumers only
// ever READ the handle, and the single release_texture call site in the whole program is buff_atlas_dispose()
// below. The hazard is removed by construction rather than managed.
//
// THE RETRY BUDGET IS NOW SHARED, and that is the one real trade-off (rule 10). It is only ever spent on
// FAILED loads, so exhausting it means the file was unreadable 12 times in a row -- a state in which per-owner
// budgets fail identically. The narrow case where separate budgets would win: the file becomes readable AFTER
// the shared budget is spent but BEFORE the next device reset. buff_atlas_forget() RE-ARMS the budget, and the
// HUD calls it on every device lost/recreate -- i.e. at every zone-in and every //load, which is exactly when
// the "device not ready yet" miss this retry exists for happens. A dead budget also SAYS SO in the log
// (instrument the success path too -- a probe that goes quiet reads like a bug that isn't happening).
//
// WHERE THE PIXELS COME FROM (2026-09-06), in order. Party, player, target, timers, debuffs and the config
// Help samples all read this ONE handle, so whichever source wins here is what the whole interface shows.
//   1. icons\status_atlas.raw -- a sheet the player BUILT (tools/aioicons.ps1, from their own PNGs or any DAT).
//      An explicit choice, so it beats everything.
//   2. the game's icon DAT, whenever somebody has REPLACED those icons -- an XIPivot overlay, or a pack copied
//      straight over the game's own ROM files. That second case is real (it is what the dev machine turned out
//      to be running) and an overlay-only test ignores it, showing the bundled sheet to somebody who had
//      deliberately installed HD icons. Their game and their HUD agree, with nothing to configure.
//   3. the bundled assets\buff_atlas.raw.
//   4. the game's UNTOUCHED DAT, last resort. Deliberately behind the bundle: the client's original status art
//      is soft, washed-out 32 px from 2002 and the bundled sheet is a crisp redraw of the same 640 statuses --
//      the two were rendered side by side, from a PRISTINE copy, before this order was chosen.
//
// "Replaced" is measured, not guessed: the original sheet is 7-bit alpha in every one of its 640 records, and
// art drawn in an image editor is not (status_icons_untouched(), model/icon_dat.h).
//
// Every source fills the SAME 1024x640 / 32px grid, because that grid is the DAT's own layout (640 records,
// cell index == status id) -- so nothing downstream changes and each fallback is a genuine drop-in.
#include "ui/buff_atlas.h"
#include "ui/tex_retry.h"
#include "gfx/texture.h"
#include "model/icon_dat.h"
#include "windower_debug.h"

namespace aio {

static u32         s_tex = 0;
static TexRetry    s_retry;
static bool        s_gaveUpLogged = false;   // log the dead budget ONCE per budget, not once per frame
static const char* s_src = "none yet";       // which source actually produced the live texture (//aio doctor)
static bool        s_datLogged = false;      // the "game DAT unusable" line : once per budget, not once per retry

// Decode the game's own icon sheet into a texture. Returns 0 (and says why, once) if this install has no
// readable icon DAT -- the caller then uses the bundled sheet IN THE SAME FRAME, so a missing DAT never costs
// a visible frame of blank icons. Not latched: buff_atlas_forget() re-arms everything at each device
// reset/zone-in, which is exactly when a "not ready yet" miss resolves itself (rule 10).
u32 buff_atlas_tex(u32 dev) {
    if (!tex_retry_due(s_tex, s_retry)) return s_tex;

    s_tex = load_raw_texture(dev, buff_custom_path(), BUFF_ATLAS_W, BUFF_ATLAS_H);   // 1) built by the player
    if (s_tex) { s_src = "custom"; tex_retry_note(s_tex, s_retry); return s_tex; }

    // The DAT is decoded ONCE per attempt and its pixels kept until we know whether they win : the decision
    // NEEDS the decode (see the header), and decoding a second time to answer one question would be waste.
    u32* px = (u32*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (SIZE_T)BUFF_ATLAS_W * BUFF_ATLAS_H * 4);
    // Initialised HERE as well as by the decoder : on an allocation failure the decoder never runs, and the
    // log line at the bottom still reads this.
    IconLoadDiag d = {}; d.step = ILS_NO_PATH; d.badRecord = -1;
    const bool haveDat = px && load_status_icons(px, BUFF_ATLAS_W, BUFF_ATLAS_H, BUFF_CELL, BUFF_COLS, &d);

    if (haveDat && (d.overlay || !status_icons_untouched(d))) {              // 2) icons somebody REPLACED
        s_tex = make_texture_argb(dev, BUFF_ATLAS_W, BUFF_ATLAS_H, px);
        if (s_tex) {
            s_src = d.overlay ? "game DAT (pack)" : "game DAT (replaced)";
            // Instrument the SUCCESS path too : a silent decode reads exactly like a decode that never ran, and
            // "which sheet am I looking at" is the first question when someone reports odd icons.
            windower::debug::log("buff atlas: %s -- %s (%u B, %d of %d records still on the game's 7-bit alpha)",
                                 d.overlay ? "XIPivot pack" : "replaced in the ROM", d.path, d.fileSize,
                                 d.halfAlpha, d.records);
        }
    }
    if (!s_tex) {                                                            // 3) the bundled sheet, same frame
        s_tex = load_raw_texture(dev, buff_atlas_path(), BUFF_ATLAS_W, BUFF_ATLAS_H);
        if (s_tex) s_src = "bundled";
    }
    if (!s_tex && haveDat) {                                                 // 4) the untouched sheet, last resort
        s_tex = make_texture_argb(dev, BUFF_ATLAS_W, BUFF_ATLAS_H, px);
        if (s_tex) s_src = "game DAT (untouched)";
    }
    if (!haveDat && !s_datLogged) {   // one line, and it NAMES the step -- no path / no file / not the icon sheet
        s_datLogged = true;
        windower::debug::log("buff atlas: game DAT unusable (step %d, record %d, '%s') -- using the bundled sheet",
                             d.step, d.badRecord, d.path[0] ? d.path : "unresolved");
    }
    if (px) HeapFree(GetProcessHeap(), 0, px);

    tex_retry_note(s_tex, s_retry);
    if (!s_tex && s_retry.tries >= 12 && !s_gaveUpLogged) {
        s_gaveUpLogged = true;
        windower::debug::log("buff atlas: giving up after 12 tries (custom sheet, game DAT, %s) -- status icons will be missing everywhere",
                             buff_atlas_path());
    }
    return s_tex;
}

void buff_atlas_forget() { s_tex = 0; s_retry = TexRetry{}; s_gaveUpLogged = false; s_datLogged = false; s_src = "none yet"; }   // FORGET only -- never Release (the old device may be dead)

void buff_atlas_dispose() { if (s_tex) release_texture(s_tex); buff_atlas_forget(); }   // the ONLY Release of this texture

unsigned buff_atlas_tries() { return s_retry.tries; }
const char* buff_atlas_source() { return s_src; }   // custom / game DAT (pack|replaced|untouched) / bundled

} // namespace aio
