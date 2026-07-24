// party_state_skillchain.cpp -- Skillchains module (resonance windows per target),
// split out of party_state.cpp. PURE MOVE : the sc_open / sc_close (opened/closed by on_action's
// 0x028 finish parse in party_state.cpp), prune_skillchains, skillchain_newest_live methods, plus
// the file-static resonance helpers sc_reson_slot / sc_set_timing (used only by sc_open/sc_close).
#include "model/party_state.h"
#include "model/skillchain.h"   // sc_info / SCP_N (Resonating property tables)
#include "model/game_mem.h"     // EntityVitals / read_entities_by_id
#include <windows.h>            // GetTickCount

namespace aio {

// SKILLCHAINS : pick a resonance slot for a target (reuse its own / a free one / the oldest).
static Resonating* sc_reson_slot(Resonating* r8, unsigned tid) {
    int freeS = -1, oldest = 0;
    for (int i = 0; i < 8; ++i) {
        if (r8[i].target == tid) return &r8[i];
        if (!r8[i].target && freeS < 0) freeS = i;
        if (r8[i].openMs < r8[oldest].openMs) oldest = i;
    }
    const int s = (freeS >= 0) ? freeS : oldest;
    r8[s] = Resonating{}; r8[s].target = tid; return &r8[s];
}
// burst-window timing (abs ms) : [openMs,delayMs)=Wait, [delayMs,endMs)=Go!/Burst. endMs = delayMs + (8-step)s
// (higher steps -> shorter continue window), mirroring the reference addon's clock+delay+8-step.
static void sc_set_timing(Resonating* r, int delay, int step) {
    const unsigned now = GetTickCount();
    r->openMs = now;
    r->delayMs = now + (unsigned)((delay > 0 ? delay : 3) * 1000);
    int win = 8 - step; if (win < 1) win = 1;
    r->endMs = r->delayMs + (unsigned)(win * 1000);
}
void PartyState::sc_open(unsigned tid, unsigned aid, int res, const unsigned char* props, int delay) {
    // an OPENING (non-chaining) WS always (re)starts a step-1 window -- like the addon. A WS that CONTINUES the
    // chain arrives via the CLOSE branch (it carries the skillchain add-effect), so it never lands here : no guard
    // needed, and guarding here (the old "don't clobber for 3s" check) wrongly blocked the box from RE-popping.
    Resonating* r = sc_reson_slot(reson_, tid);
    r->target = tid; r->actionId = aid; r->resource = (unsigned char)res;
    r->nProp = 0;
    for (int i = 0; i < 3 && props[i] < SCP_N; ++i) r->prop[r->nProp++] = props[i];
    if (!r->nProp) { r->prop[0] = props[0]; r->nProp = 1; }
    r->step = 1; r->lvl = (unsigned char)sc_info(r->prop[0]).lvl; r->closed = 0; r->formed = 0;
    sc_set_timing(r, delay, 1);
}
// Once/frame : drop any resonance window whose mob has DIED (hpp 0) or is no longer a mob, so the box (and the
// nearby-fallback) don't linger on a dead target. A read that can't find the entity (valid=false) is left to the
// window's own endMs expiry -- a transient bad read must not kill a still-live chain.
void PartyState::prune_skillchains() {
    unsigned ids[8]; int slot[8]; int nq = 0;
    for (int i = 0; i < 8; ++i) if (reson_[i].target && reson_[i].nProp) { ids[nq] = reson_[i].target; slot[nq] = i; ++nq; }
    if (!nq) return;
    EntityVitals ev[8];
    read_entities_by_id(ids, nq, ev);
    for (int q = 0; q < nq; ++q) {
        const EntityVitals& v = ev[q];
        if (v.valid && (v.hpp <= 0 || v.spawnType != 0x10)) reson_[slot[q]] = Resonating{};   // confirmed dead / not-a-mob -> drop
    }
}
// SCH Immanence (status 470) : while it is up, the caster's NEXT elemental spell can OPEN/continue a skillchain
// (the reference addon's chain_buff). Self reads the live memory buff list ; an ally reads its 0x076 icon cache.
bool PartyState::has_immanence(unsigned actor) const {
    if (actor == selfId_) {
        unsigned short mb[32]; const int n = read_player_buffs(mb, 32);
        for (int i = 0; i < n; ++i) if (mb[i] == 470) return true;
        return false;
    }
    const BuffSet* bs = buffs_for(actor);
    if (bs) for (int i = 0; i < bs->n; ++i) if (bs->ids[i] == 470) return true;
    return false;
}
const Resonating* PartyState::skillchain_newest_live() const {
    const unsigned now = GetTickCount();
    const Resonating* best = 0;
    for (int i = 0; i < 8; ++i) {
        const Resonating& r = reson_[i];
        if (!r.target || !r.nProp) continue;
        if (now >= r.endMs + 250u) continue;                        // window (+ the widget's grace) already passed
        if (!best || r.openMs > best->openMs) best = &r;            // newest-updated chain wins
    }
    return best;
}
void PartyState::sc_close(unsigned tid, unsigned aid, int res, int prop, int delay) {
    const unsigned now = GetTickCount();
    Resonating* r = sc_reson_slot(reson_, tid);
    const bool recent = (r->endMs && now < r->endMs + 3000u && r->step >= 1 && r->nProp > 0);
    const int prevStep = recent ? r->step : 1;
    // RE-COMPUTE the skillchain LEVEL. The 0x028 add-effect names the skillchain (its animation) but NOT its level,
    // so a Light+Light (Lv.4 -- which CLOSES) reads identically to a fresh Lv.3 Light. Using sc_info(prop).lvl (always
    // 3 for Light/Darkness) left a Light chain forever "open" -> the continuation list kept proposing Light. The
    // reference addon re-checks a Lv.3 result via check_props against the PREVIOUS resonance's props (aeonic doesn't
    // change the level, only the result name, so the base WS props are enough here).
    int lvl = sc_info(prop).lvl;
    if (lvl == 3 && recent) {
        const SkillRow* sk = sc_skill_lookup(res, aid);
        if (sk) {
            unsigned char cp[3]; int ncp = 0; for (int j = 0; j < 3 && sk->prop[j] < SCP_N; ++j) cp[ncp++] = sk->prop[j];
            int lv2 = 0, rs2 = 0;
            if (sc_check_props(r->prop, r->nProp, cp, ncp, lv2, rs2)) lvl = lv2;   // real chain level (Light+Light -> 4)
        }
    }
    r->target = tid; r->actionId = aid; r->resource = (unsigned char)res;
    r->prop[0] = (unsigned char)prop; r->nProp = 1; r->step = (unsigned char)(prevStep + 1);
    r->lvl = (unsigned char)lvl;
    r->closed = (r->step > 5 || r->lvl == 4) ? 1 : 0;   // Lv.4 (incl. Light+Light) or step>5 -> closed : burst only, no more continuation
    r->formed = 1;
    sc_set_timing(r, delay, (int)r->step);
}

} // namespace aio
