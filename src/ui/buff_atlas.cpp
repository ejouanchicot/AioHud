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
#include "ui/buff_atlas.h"
#include "ui/tex_retry.h"
#include "gfx/texture.h"
#include "windower_debug.h"

namespace aio {

static u32      s_tex = 0;
static TexRetry s_retry;
static bool     s_gaveUpLogged = false;   // log the dead budget ONCE per budget, not once per frame

u32 buff_atlas_tex(u32 dev) {
    const bool due = tex_retry_due(s_tex, s_retry);
    ensure_raw_tex(dev, s_tex, s_retry, buff_atlas_path(), BUFF_ATLAS_W, BUFF_ATLAS_H);
    if (due && !s_tex && s_retry.tries >= 12 && !s_gaveUpLogged) {
        s_gaveUpLogged = true;
        windower::debug::log("buff atlas: giving up after 12 tries (%s) -- status icons will be missing everywhere",
                             buff_atlas_path());
    }
    return s_tex;
}

void buff_atlas_forget() { s_tex = 0; s_retry = TexRetry{}; s_gaveUpLogged = false; }   // FORGET only -- never Release (the old device may be dead)

void buff_atlas_dispose() { if (s_tex) release_texture(s_tex); buff_atlas_forget(); }   // the ONLY Release of this texture

unsigned buff_atlas_tries() { return s_retry.tries; }

} // namespace aio
