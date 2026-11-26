// hud_skillchains.cpp -- split out of hud.cpp (pure move). Skillchains box renderer.
#include "ui/hud.h"
#include "ui/hud_internal.h"
#include "model/ui_config.h"
#include "ui/text_style.h"
#include "ui/box_style.h"
#include "ui/sc_circle.h"
#include "model/party_state.h"
#include "gfx/draw.h"
#include "gfx/d3d.h"
#include "model/skillchain.h"
#include "model/weapon_skills_gen.h"
#include "model/spells_gen.h"
#include "model/abilities_gen.h"
#include "model/mobskills_gen.h"
#include "windower_debug.h"
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>

unsigned g_scWsOnSphere = 0;   // (global : hud.cpp declares it at block scope)
namespace aio {


// ============================ SKILLCHAINS box ============================
// per-element typography helpers (mirror the minimap clock's mm_* ) : each text element (Title / Timer / Step /
// Property / WS list) has its own face / size / outline / bold-italic-caps / colour from ui_config().scText[].
static Font* sc_font(const Frame& f, int e) { return te_font(f, ui_config().scText[e]); }
static inline float sc_sz(int e, float base) { return te_sz(ui_config().scText[e], base); }
static inline float sc_ow(int e, float base) { return te_ow(ui_config().scText[e], base); }
static inline u32   sc_col(int e, u32 base)  { return te_col(ui_config().scText[e], base); }
static const char*  sc_up(int e, const char* s, char* buf, int cap) { return te_upper(ui_config().scText[e], s, buf, cap); }   // UPPERCASE into buf if the element wants CAPS
// FNV-1a over what the player can use right now (snapshot) : part of the continuation list's key, so the list is
// rebuilt when a weapon swap changes the WS, a pet brings its ready moves, the BLU set changes, or the bitmap
// resolves after a failed read (it used to stay empty until the target / step / job happened to change).
static unsigned usable_hash(const GameState& g) {
    unsigned h = 2166136261u;
    auto mix = [&h](const unsigned char* p, int n) { for (int i = 0; i < n; ++i) { h ^= p[i]; h *= 16777619u; } };
    mix(g.wsBits, sizeof(g.wsBits)); mix(g.jaBits, sizeof(g.jaBits));
    mix((const unsigned char*)g.blu, g.nBlu * (int)sizeof(g.blu[0]));
    h ^= g.jaOk ? 0x9E37u : 0u;
    return h;
}
// a small VECTOR "->" arrow, vertically centred at cy (the font atlas is Latin-1 only, so U+2192 can't be baked
// like the original addon's "→"). Caller is in colour-quad state ; returns the width consumed.
static float sc_arrow(u32 dev, float x, float cy, float sz, u32 col) {
    const float w = sz * 0.80f, th = 1.3f + sz * 0.06f, hs = sz * 0.26f;
    seg_soft(dev, x, cy, x + w, cy, th, col);                          // shaft
    seg_soft(dev, x + w - hs, cy - hs, x + w + 0.5f, cy, th, col);     // upper head
    seg_soft(dev, x + w - hs, cy + hs, x + w + 0.5f, cy, th, col);     // lower head
    return w + hs * 0.5f;
}
// lerp two ARGB colours (t in 0..1) -- used for the closing-burst timer fade (green -> yellow -> red as it expires).
static u32 sc_mix(u32 a, u32 b, float t) {
    if (t < 0.0f) t = 0.0f; if (t > 1.0f) t = 1.0f;
    const int ar=(a>>16)&0xFF, ag=(a>>8)&0xFF, ab=a&0xFF, aa=(a>>24)&0xFF;
    const int br=(b>>16)&0xFF, bg=(b>>8)&0xFF, bb=b&0xFF, ba=(b>>24)&0xFF;
    const int r=ar+(int)((br-ar)*t+0.5f), g=ag+(int)((bg-ag)*t+0.5f), bl=ab+(int)((bb-ab)*t+0.5f), al=aa+(int)((ba-aa)*t+0.5f);
    return ((u32)al<<24)|((u32)r<<16)|((u32)g<<8)|(u32)bl;
}
// a small filled colour SQUARE (an element "pip"), vertically centred at cy -> the burst element reads at a GLANCE
// by colour, not by name. Returns its side. Caller must be in colour-quad state (dColorQuadState).
static float sc_pip(u32 dev, float x, float cy, float sz, u32 col) {
    const float s = sz * 0.66f;
    rrect(dev, snap(x), snap(cy - s * 0.5f), s, s, s * 0.24f, col, col, 1.0f);
    return s;
}

// A small themed box on the CURRENT target's active skillchain : title / burst timer (Wait->Go!->Burst) /
// "Step: N > <closing move>" / "[property] (burst elements)" / the WS continuation list. Data from
// party().skillchain_for() ; shown while the resonance window is live, or a looping sample in preview/edit.
// Core renderer, extracted as a FREE function so the config PREVIEW and the Help sample reuse the EXACT same
// config-aware draw with no Hud instance.
void skillchains_draw(const Frame& f, bool preview, float ovX, float ovY, float ovS, float screenW, float screenH,
                      bool measureOnly = false, float* outW = 0, float* outH = 0) {
    if (!preview && !measureOnly) sc_circle_idle();   // the Circle's distortion copy : given back 5 s after its last use
    const UiConfig& C = ui_config();
    if (!C.scShow) return;
    const bool editing = C.editLayout && !preview;
    u32 dev = f.dev;
    if (!f.font && !f.fonts) return;

    // ---- gather the display data (live resonance, or a sample in preview/edit) ----
    int step, phase, nProp; unsigned char props[3]; bool formed; const char* actName; float timerSec; float burstFrac = 1.0f;
    // the Circle style's extra inputs : its countdown arc, its animation clock (seconds since the chain formed), the
    // chain's identity (a new one restarts the animation), whether it is closed
    float arcFrac = 1.0f, liveBt = -1.0f; unsigned liveKey = 0; bool closedC = false;
    if (preview || editing) {
        // the sample : the skillchain picked in the config (Display > Preview), or a lone move ; Lv.4 = a closed chain
        const int pv = (C.scPreview >= -1 && C.scPreview < SCP_N) ? C.scPreview : SCP_Fusion;
        step = pv < 0 ? 1 : 2; props[0] = (unsigned char)(pv < 0 ? SCP_Liquefaction : pv); nProp = 1; formed = pv >= 0; actName = "Savage Blade";
        phase = (pv >= 0 && sc_info(pv).lvl >= 4) ? 2 : 1;
        // preview AND edit layout : the same loop -- the countdown, the animation clock and the chain's identity all
        // run (edit layout froze them at 5 s / 2 s while the particles kept the real clock : half the sphere stood still)
        timerSec = 5.0f - (float)(GetTickCount() % 5000u) / 1000.0f;
        if (timerSec < 0.1f) timerSec = 5.0f;
        arcFrac = timerSec / 5.0f; liveBt = 5.0f - timerSec; liveKey = 0x5C1E0000u + GetTickCount() / 5000u + (unsigned)(pv + 1) * 0x10000u;
        closedC = phase == 2;
    } else {
        if (!f.game) return;
        const GameState& g = *f.game;
        // Which mob's chain to show. ALWAYS your current cursor target <t> (its own chain). The BROADER fallbacks are
        // ALL gated by config scNearby (default on) : your BATTLE target <bt> (engaged, reticle dropped) then the
        // newest LIVE chain on any nearby mob -- so a healer/support/observer sees a party member's SC. With scNearby
        // OFF the box is STRICT : only your <t> chain (the fallbacks were what made it show while engaged / not
        // targeting -- exactly the "box appears even when the toggle is off" case). The broader steps never fire while
        // your reticle is on a DIFFERENT mob (targetingMob), so the box can't linger on a mob you targeted away from.
        // ONLY THE MOB YOU FIGHT (user, 2026-11-24 : "a WS or SC on another mob than the one I fight, I don't want to see
        // it"). Your reticle's mob first, else your battle target <bt>. The "newest chain nearby" fallback (a healer
        // watching) runs ONLY when you are neither targeting a mob nor engaged -- in a party farm it showed another
        // member's chain on another mob whenever yours had none.
        const bool targetingMob = g.target.valid && g.target.spawnType == 0x10;
        const Resonating* r = party().skillchain_for(targetingMob ? g.target.id : 0);
        if (!r && !targetingMob && g.battleTargetId) r = party().skillchain_for(g.battleTargetId);
        if (!r && !targetingMob && !g.battleTargetId && C.scNearby) r = party().skillchain_newest_live();
        if (party().scapt_armed()) {                                  // //aio sclog : WHAT the box chose, logged on each change
            static unsigned lastT = 0xFFFFFFFFu, lastBt = 0xFFFFFFFFu, lastShown = 0xFFFFFFFFu;
            const unsigned tId = g.target.valid ? g.target.id : 0, shown = r ? r->target : 0;
            if (tId != lastT || g.battleTargetId != lastBt || shown != lastShown) {
                lastT = tId; lastBt = g.battleTargetId; lastShown = shown;
                const Resonating* nl = party().skillchain_newest_live();
                windower::debug::log("SCAPT show t=%08X spawn=%02X bt=%08X -> %s%08X | own window on t : %s | newest live window : %08X%s",
                    tId, g.target.valid ? g.target.spawnType : 0, g.battleTargetId, shown ? "showing " : "NOTHING ", shown,
                    party().skillchain_for(tId) ? "yes" : "no", nl ? nl->target : 0,
                    (!shown && targetingMob && nl) ? " (hidden : your reticle is on another mob)" : (!C.scNearby && !shown ? " (scNearby off)" : ""));
            }
        }
        if (!r) return;
        const unsigned now = GetTickCount();
        if (now >= r->endMs && (now - r->endMs) > 250u) return;     // past the window (+ short grace) -> hide
        step = r->step; formed = r->formed != 0;
        nProp = r->nProp > 3 ? 3 : r->nProp; for (int i = 0; i < nProp; ++i) props[i] = r->prop[i];
        if (nProp <= 0) { props[0] = SCP_Fusion; nProp = 1; }
        // timer : NEVER subtract two unsigned ms when the right side is bigger (it underflows to a huge value ->
        // the "94548988.54" garbage during the grace) -> clamp to 0 once we're at/after endMs.
        // A CLOSED chain (Lv.4 / step>5) goes straight to BURST for the whole remaining window -- NO Wait/Go phase and
        // NO continuation list (the reference addon's `if closed then disp_info=''`). Without this the closed chain
        // still passed through its post-close Wait window (phase 0), which REBUILDS the continuation list -> the stale
        // "Ukko's Fury -> Light" lingered a moment before the box reached phase 2 and cleared it.
        // WS LEAD : the Wait phase isn't hard -- a WS started ~0.5s BEFORE the window truly opens still lands in time
        // (server lag carries it), so flip to Go! that early and count the Wait down to the actionable moment, not to
        // delayMs. Practical difference the player feels : you may fire at ~0.5 left on Wait.
        const unsigned WS_LEAD = 500u;
        const unsigned goAt = (r->delayMs > WS_LEAD) ? (r->delayMs - WS_LEAD) : 0u;
        if      (r->closed)       { phase = 2; timerSec = (now < r->endMs) ? (float)(r->endMs - now) / 1000.0f : 0.0f;
                                    burstFrac = (r->endMs > r->openMs && now < r->endMs) ? (float)(r->endMs - now) / (float)(r->endMs - r->openMs) : 0.0f; }   // 1 = window full (green) .. 0 = expired (red)
        else if (now >= r->endMs) { phase = 1; timerSec = 0.0f; }
        else if (now < goAt)      { phase = 0; timerSec = (float)(goAt - now) / 1000.0f; }
        else                      { phase = 1; timerSec = (float)(r->endMs - now) / 1000.0f; }
        arcFrac = phase == 0 ? 1.0f : (phase == 2 ? burstFrac : ((r->endMs > goAt && now < r->endMs) ? (float)(r->endMs - now) / (float)(r->endMs - goAt) : 0.0f));
        liveBt = formed ? (float)(now - r->openMs) / 1000.0f : -1.0f;   // openMs = when the closing move landed
        liveKey = (r->target * 2654435761u) ^ r->openMs; closedC = r->closed != 0;
        actName = "";
        if (r->resource == SCR_WS)         { const WSRow* w = ws_info(r->actionId);        if (w) actName = w->en; }
        else if (r->resource == SCR_SPELL) { const SpellRow* s = spell_info(r->actionId);   if (s) actName = s->en; }
        else if (r->resource == SCR_MOB)   { const MobSkillRow* m = mobskill_info(r->actionId); if (m) actName = m->en; }
        else if (r->resource == SCR_JA)    { const AbilRow* a = abil_info(r->actionId);     if (a) actName = a->en; }
    }

    // ---- continuation list : YOUR usable weapon skills that extend the chain ("Name > Lv.X property").
    // Rebuilt only when the resonance changes (a get_abilities read + check_props per usable WS). No list once
    // the chain is CLOSED (lvl4 / step>5 = burst only), mirroring the addon. ----
    struct ScLine { char name[28]; unsigned char lvl, prop, now, wsk; };   // now = usable with the weapon in hand ; wsk = its weapon type
    static ScLine list[16]; static int listN = 0; static unsigned listKey = 0;
    if (preview || editing) {
        // what the "WS offered" setting gives : MY WEAPON = one sword's moves (8) ; ALL MY JOB'S WEAPONS = the worst case,
        // 16 (the most the box holds) across a WAR's weapons, 8 skillchains -- the room it really takes
        struct Smp { const char* n; int l; int p; int k; };   // k : the weapon (3 = sword, the one "in hand")
        static const Smp ONE[8] = {
            { "Savage Blade", 2, SCP_Fusion, 3 }, { "Requiescat", 2, SCP_Gravitation, 3 }, { "Sanguine Blade", 1, SCP_Compression, 3 }, { "Circle Blade", 1, SCP_Impaction, 3 },
            { "Red Lotus Blade", 1, SCP_Liquefaction, 3 }, { "Burning Blade", 1, SCP_Liquefaction, 3 }, { "Seraph Blade", 1, SCP_Scission, 3 }, { "Fast Blade", 1, SCP_Scission, 3 } };
        static const Smp ALL[16] = {
            { "Ukko's Fury", 3, SCP_Light, 6 }, { "Savage Blade", 2, SCP_Fusion, 3 }, { "Upheaval", 2, SCP_Fusion, 6 }, { "Resolution", 2, SCP_Fragmentation, 4 },
            { "Requiescat", 2, SCP_Gravitation, 3 }, { "Cross Reaper", 2, SCP_Distortion, 7 }, { "Impulse Drive", 2, SCP_Gravitation, 8 }, { "Judgment", 2, SCP_Fragmentation, 11 },
            { "Decimation", 1, SCP_Compression, 5 }, { "Steel Cyclone", 1, SCP_Compression, 6 }, { "Circle Blade", 1, SCP_Impaction, 3 }, { "Black Halo", 1, SCP_Impaction, 11 },
            { "Fell Cleave", 1, SCP_Scission, 6 }, { "Evisceration", 1, SCP_Scission, 2 }, { "Red Lotus Blade", 1, SCP_Liquefaction, 3 }, { "Shell Crusher", 1, SCP_Reverberation, 12 } };
        const Smp* SMP = C.scWsScope ? ALL : ONE; listN = C.scWsScope ? 16 : 8;
        for (int i = 0; i < listN; ++i) { int c = 0; for (; SMP[i].n[c] && c < 27; ++c) list[i].name[c] = SMP[i].n[c]; list[i].name[c] = 0; list[i].lvl = (unsigned char)SMP[i].l; list[i].prop = (unsigned char)SMP[i].p; list[i].wsk = (unsigned char)SMP[i].k; list[i].now = (unsigned char)(SMP[i].k == 3); }
    } else if (phase == 2) {
        listN = 0; listKey = 0;                                 // closed chain -> burst only, no continuation list
    } else {
        const int mjob = f.game->me.mjob;
        // AEONIC : if your equipped MAIN or RANGED is an Aeonic weapon, a WS bound to it gains its aeonic opening
        // property (Light/Darkness) -> it can form Radiance/Umbra. Reliable only for YOU : you can read your own gear ;
        // for another player an Aeonic is indistinguishable from a look-alike, so their aeonic props are never assumed.
        int selfAeonic = sc_aeonic_weapon_of_item(f.game->equip.id[0]);
        if (selfAeonic < 0) selfAeonic = sc_aeonic_weapon_of_item(f.game->equip.id[2]);
        // Aftermath gate (the reference aeonic_am) : an Aeonic's Radiance/Umbra can only close from a high enough step
        // for your AM level (270/271/272 = Lv.1/2/3 -> AM3 from step 1, AM2 from step 2, AM1 from step 3). No AM = no
        // Radiance/Umbra offered at all. `step` here is the CURRENT resonance step ; the continuation would be step+... .
        int amLvl = 0; for (int i = 0; i < f.game->nbuff; ++i) { const int s = f.game->buffs[i]; if (s == 272) { amLvl = 3; break; } else if (s == 271) { if (amLvl < 2) amLvl = 2; } else if (s == 270) { if (amLvl < 1) amLvl = 1; } }
        const bool aeonicOk = (selfAeonic >= 0) && (amLvl >= 1) && ((3 - amLvl) < step);
        // Hash ALL active opening properties + their count, not just props[0]. A new resonance that shares the
        // primary property but differs in props[1..2]/nProp (e.g. a lone WS with two props vs a closed chain leaving
        // one) reused the stale continuation list -> wrong "what continues" suggestions until the key happened to move.
        const unsigned key = f.game->target.id * 2654435761u + (unsigned)step * 131u + (unsigned)mjob * 97u
                           + (unsigned)props[0] * 7u + (unsigned)props[1] * 1013u + (unsigned)props[2] * 65537u + (unsigned)nProp * 40503u
                           + (unsigned)(selfAeonic + 1) * 277u + (unsigned)amLvl * 1201u + (unsigned)C.scWsScope * 7919u   // rebuild on weapon swap / AM change (aeonic on/off changes what continues)
                           + usable_hash(*f.game) * 104729u;   // ...and when what you CAN use changes : another weapon (its WS), a pet out (ready moves), BLU set spells, the bitmap resolving after a miss
        if (key != listKey) {
            listKey = key; listN = 0; int found = 0;
            // add a candidate move (weapon skill / job ability / SCH element) IF it continues the active chain.
            // The list holds 16 : once full, a candidate of a HIGHER level replaces the lowest kept one (2026-11-22 :
            // the cap used to stop the scan, and the level sort only ran on the first 16 found -- WS are scanned first,
            // so a better JA / BLU / SCH closer past 16 matches was never offered).
            auto tryAdd = [&](int resource, unsigned id, const char* nm, bool now, int wsk) {
                const SkillRow* sk = sc_skill_lookup(resource, id); if (!sk) return;
                // wsAeonic : THIS candidate is the WS of YOUR equipped Aeonic AND your Aftermath allows Radiance/Umbra.
                // Only then does it gain its aeonic opening property and can UPGRADE a Light/Darkness Lv.4 close to
                // Radiance/Umbra. An Empyrean (Ukko's Fury) or any non-Aeonic WS never does, even with its own Aftermath.
                const bool wsAeonic = (aeonicOk && sk->aeonic < SCP_N && sk->weapon != 255 && (int)sk->weapon == selfAeonic);
                unsigned char cp[3]; int ncp = 0; for (int j = 0; j < 3 && sk->prop[j] < SCP_N; ++j) cp[ncp++] = sk->prop[j];
                if (wsAeonic && ncp < 3) cp[ncp++] = sk->aeonic;   // add the aeonic opening property (Light/Darkness) so the aeonic WS can form a Light/Darkness chain
                int lv = 0, rs = 0;
                if (sc_check_props(props, nProp, cp, ncp, lv, rs)) {
                    if (wsAeonic) { if (rs == SCP_Light) rs = SCP_Radiance; else if (rs == SCP_Darkness) rs = SCP_Umbra; }   // aeonic close -> Radiance/Umbra ; otherwise the Light/Darkness Lv.4 stands
                    ++found;
                    int slot = listN;
                    if (listN >= 16) {                                                    // full : evict the lowest level (another weapon's first) if this one is better
                        slot = 0; for (int k = 1; k < 16; ++k) if (list[k].lvl < list[slot].lvl || (list[k].lvl == list[slot].lvl && !list[k].now && list[slot].now)) slot = k;
                        if (!(lv > list[slot].lvl || (lv == list[slot].lvl && now && !list[slot].now))) return;
                    } else ++listN;
                    ScLine& L = list[slot]; if (!nm) nm = "";
                    int c = 0; for (; nm[c] && c < 27; ++c) L.name[c] = nm[c]; L.name[c] = 0;
                    L.lvl = (unsigned char)lv; L.prop = (unsigned char)rs; L.now = now ? 1 : 0; L.wsk = (unsigned char)wsk;
                }
            };
            // what you can use comes from the frame SNAPSHOT (rule 6 : the poller reads the client, widgets never do)
            const GameState& G = *f.game;
            unsigned short ws[128]; int nws = 0;                                                        // everyone : weapon skills
            if (G.jaOk) for (int id = 1; id < 256 && nws < 128; ++id) if (G.wsBits[id >> 3] & (1u << (id & 7))) ws[nws++] = (unsigned short)id;
            const unsigned jm = C.scWsScope ? sc_job_weapon_mask(mjob) : 0u;
            if (jm) {   // EVERY weapon your job wields (as the weapon you might swap to) : the whole table, by weapon type ; plus what is usable now
                for (int i = 0; i < WEAPON_SKILLS_N; ++i) { const WSRow& w = WEAPON_SKILLS[i];
                    bool now = false; for (int j = 0; j < nws; ++j) if (ws[j] == w.id) { now = true; break; }
                    if (now || (w.skill && w.skill < 32 && (jm & (1u << w.skill)))) tryAdd(SCR_WS, w.id, w.en, now, w.skill); }
            } else
                for (int i = 0; i < nws; ++i) { const WSRow* w = ws_info(ws[i]); tryAdd(SCR_WS, ws[i], w ? w->en : 0, true, w ? w->skill : 0); }
            unsigned short ja[256]; int nja = 0;                                                        // BST/SMN : pet ready moves (self-gated on the pet being out) + any skillchaining JA
            if (G.jaOk) for (int id = 0; id < 1024 && nja < 256; ++id) if (G.jaBits[id >> 3] & (1u << (id & 7))) ja[nja++] = (unsigned short)id;
            for (int i = 0; i < nja; ++i) {
                const AbilRow* a = abil_info(ja[i]);
                // a Blood Pact (recast 173 Rage / 174 Ward) only on a SMN main : the game also marks Cait Sith's Regal Gash
                // (id 780, filed among the Ready moves) usable on BST -- seen in the SCLIST capture of 2026-10-29
                if (a && (a->recast_id == 173 || a->recast_id == 174) && mjob != 15) continue;
                tryAdd(SCR_JA, ja[i], a ? a->en : 0, true, 0);
            }
            // //aio sclog : every job ability the game marks usable (id, name, recast id), to tell why a move that cannot be
            // used is offered (2026-10-29 : Regal Gash, a Cait Sith Blood Pact, listed on BST after Sensilla Blades)
            if (party().scapt_armed()) {
                // debug::log stops at 1023 characters : flushed as numbered lines well before that
                char b[960]; int o = _snprintf(b, sizeof(b), "SCLIST mjob=%d props=%d,%d,%d step=%d usable JA (%d):", mjob, props[0], props[1], props[2], step, nja);
                int part = 1;
                for (int i = 0; i < nja && o > 0; ++i) {
                    if (o > (int)sizeof(b) - 48) { windower::debug::log("%s", b); o = _snprintf(b, sizeof(b), "SCLIST usable JA (cont %d):", ++part); }
                    const AbilRow* a = abil_info(ja[i]);
                    const int w = _snprintf(b + o, sizeof(b) - o, " %u=%s/rc%u", ja[i], a ? a->en : "?", a ? a->recast_id : 0u);
                    if (w < 0) break; o += w;
                }
                windower::debug::log("%s", b);
            }
            if (mjob == 16) for (int i = 0; i < G.nBlu; ++i) {                                         // BLU : your currently-SET blue magic
                const SpellRow* s = spell_info(G.blu[i]); tryAdd(SCR_SPELL, G.blu[i], s ? s->en : 0, true, 0); }
            if (mjob == 20) for (int el = 0; el < 8; ++el) tryAdd(SCR_ELEM, el, scelem_name(el), true, 0);        // SCH : the 8 elemental magics are always castable
            for (int i = 1; i < listN; ++i) { ScLine t = list[i]; int j = i - 1; while (j >= 0 && (list[j].lvl < t.lvl || (list[j].lvl == t.lvl && !list[j].now && t.now))) { list[j + 1] = list[j]; --j; } list[j + 1] = t; }   // highest level first, the weapon in hand before the others
            if (party().scapt_armed()) {   // what the box then offers
                char b[700]; int o = _snprintf(b, sizeof(b), "SCLIST offered (%d of %d continuing):", listN, found);
                for (int i = 0; i < listN && o > 0 && o < (int)sizeof(b) - 40; ++i) {
                    const int w = _snprintf(b + o, sizeof(b) - o, " %s(lv%d)", list[i].name, list[i].lvl);
                    if (w < 0) break; o += w;
                }
                windower::debug::log("%s", b);
            }
        }
    }

    // ===== the CIRCLE style (the default) : the resonance sphere, its animation, the moves that continue it beside it
    // (ui/sc_circle.h, engine in hud_scfx.cpp). The edit box = the sphere with its title and burst icons ; the list
    // follows it (scListPos / scListOff), so placing the sphere places both. =====
    if (C.scStyle == 0) {
        const float sc = C.scScale < 0.4f ? 0.4f : (C.scScale > 3.0f ? 3.0f : C.scScale);
        const float ls = C.scListScale < 0.5f ? 0.5f : (C.scListScale > 2.0f ? 2.0f : C.scListScale);
        const float k  = (ovS > 0.0f) ? ovS * (1000.0f / 1349.0f) * 1.25f : (screenH / 1349.0f) * 1.25f * sc;   // preview : ovS is the box's scale
        const float kl = (ovS > 0.0f) ? k * ls / sc : (screenH / 1349.0f) * 1.25f * ls;   // live : the list's size does NOT follow the sphere's -- the preview the same
        const float R = sc_circle_radius(k), cw = 2.0f * (R + 40.0f * k), top = R + 58.0f * k, ch = top + R + 40.0f * k;
        const int nItems = (C.scList && phase != 2) ? (listN < 16 ? listN : 16) : 0;
        const float gm = C.scListGap < 0.6f ? 0.6f : (C.scListGap > 3.0f ? 3.0f : C.scListGap);
        const float lH = nItems > 0 ? nItems * 14.0f * kl * gm + 40.0f * kl : 0.0f, lW = nItems > 0 ? 300.0f * kl : 0.0f;
        const bool vert = C.scListPos <= 1;
        const float totW = vert ? (cw > lW ? cw : lW) : cw + 2.0f * lW, totH = vert ? ch + lH : ch;
        if (measureOnly) { if (outW) *outW = totW; if (outH) *outH = totH; return; }
        float px, py;
        if (ovS > 0.0f) {   // preview : the whole centred on the point -- with the list's room ALWAYS counted, so a closed chain (no list) sits where the others do
            const int nP = C.scList ? (listN < 16 ? listN : 16) : 0;
            const float lHp = nP > 0 ? nP * 14.0f * kl * gm + 40.0f * kl : 0.0f, totHp = vert ? ch + lHp : ch;
            px = snap(ovX - cw * 0.5f); py = snap(ovY - totHp * 0.5f + (C.scListPos == 1 ? lHp : 0.0f)); }
        else            { px = box_grow_x(screenW, UiConfig::GB_SC, 0, C.scX, cw); py = snap(C.scY * screenH); }
        ScCircleItem items[16];
        for (int i = 0; i < nItems; ++i) { items[i].ws = list[i].name; items[i].prop = list[i].prop; items[i].lvl = list[i].lvl; items[i].other = list[i].now ? 0 : 1; items[i].wsk = list[i].wsk; }
        const int tpRawC = (preview || editing) ? 1000 : (f.game ? f.game->me.tp : 0);
        ScCircleLive L;
        L.key = liveKey; L.prop = formed ? props[0] : -1; L.bt = liveBt; L.phase = phase; L.closed = closedC;
        L.timer = timerSec; L.frac = arcFrac; L.tp = tpRawC < 0 ? 0 : (tpRawC > 3000 ? 3000 : tpRawC); L.opener = actName;
        L.items = items; L.nItems = nItems; L.CX = snap(px + cw * 0.5f); L.CY = snap(py + top); L.k = k; L.kl = kl;
        L.scaleRel = (ovS > 0.0f) ? 1.0f : sc; L.preview = ovS > 0.0f;
        if (preview || editing) {   // the sample : your weaponskill, then a magic burst in the shown chain's first element, in a loop
            const unsigned cyc = GetTickCount() % 6000u;
            static const char* MBN[8] = { "Fire VI", "Blizzard VI", "Aero VI", "Stone VI", "Thunder VI", "Water VI", "Holy II", "Impact" };
            if (C.scArcade && cyc < 2400u) { L.hitKind = 1; L.hitName = "Savage Blade"; L.hitDmg = 12345; L.hitMs = (float)cyc; }
            else if (C.scMbFx && cyc >= 3000u && cyc < 5400u) {
                const int el = L.prop >= 0 && sc_info(L.prop).nElem > 0 ? sc_info(L.prop).elem[0] : EL_Lightning;
                L.hitKind = 2; L.hitName = MBN[el & 7]; L.hitDmg = 23456; L.hitElem = el; L.hitMs = (float)(cyc - 3000u); }
        } else {   // live : the more recent of your weaponskill / your magic burst, each if switched on
            const unsigned nowMs = GetTickCount();
            const WsPopup& wp = party().ws_popup(); const MbPopup& mp = party().mb_popup();
            const unsigned aW = wp.startMs ? nowMs - wp.startMs : 0xFFFFFFFFu, aM = mp.startMs ? nowMs - mp.startMs : 0xFFFFFFFFu;
            const bool wOn = C.scArcade && wp.name[0] && aW < 2400u, mOn = C.scMbFx && aM < 2400u;
            if (mOn && (!wOn || aM <= aW)) { L.hitKind = 2; L.hitName = mp.name; L.hitDmg = mp.dmg; L.hitElem = mp.elem; L.hitHasDmg = mp.hasDmg != 0; L.hitMs = (float)aM; }
            else if (wOn) { L.hitKind = 1; L.hitName = wp.name; L.hitDmg = wp.dmg; L.hitMs = (float)aW; }
            if (wOn) g_scWsOnSphere = GetTickCount();   // the sphere shows this weaponskill : the centre popup stands down
        }
        if (ovS <= 0.0f) {
            // the WHOLE content (sphere + title + list) : the edit-layout box and the keep-on-screen rect. The stored
            // place stays the sphere's own (scX / scY) : the union is offset from it by (dx, dy).
            float ux = px, uy = py, uw = cw, uh = ch, lx, ly, lw, lh2;
            auto take = [&](float rx, float ry, float rw, float rh) {   // grow the union by a rect
                const float x1 = (rx + rw > ux + uw ? rx + rw : ux + uw), y1 = (ry + rh > uy + uh ? ry + rh : uy + uh);
                ux = rx < ux ? rx : ux; uy = ry < uy ? ry : uy; uw = x1 - ux; uh = y1 - uy; };
            const bool hasList = sc_circle_list_rect(f, L, lx, ly, lw, lh2);
            if (hasList) take(lx, ly, lw, lh2);
            if (editing && sc_circle_hit_rect(L, lx, ly, lw, lh2)) take(lx, ly, lw, lh2);   // the Arcade WS / Magic Burst popup : placed with the box
            if (hasList || editing) {
                // SYMMETRIC round the sphere horizontally : the edit grid's centre line then centres the SPHERE (a list
                // on one side made the union lopsided -> snapped to the centre, the sphere sat off it)
                const float cxS = px + cw * 0.5f, hw = (cxS - ux > ux + uw - cxS) ? cxS - ux : ux + uw - cxS;
                ux = cxS - hw; uw = 2.0f * hw; }
            const float dx = ux - px, dy = uy - py;
            if (editing) {
                static EditBox g_scEditC; const bool wasDrag = g_scEditC.dragging; float tx = 0.0f, ty = 0.0f;
                box_edit(f, g_scEditC, EDITBOX_SKILLCHAIN, ux, uy, snap(uw), snap(uh), ui_config().scScale, tx, ty, 0);
                px = ux - dx; py = uy - dy;
                const int an = C.boxGrow[UiConfig::GB_SC];
                ui_config().scX = (px + (an == 1 ? cw * 0.5f : an == 2 ? cw : 0.0f)) / screenW; ui_config().scY = py / screenH;
                if (wasDrag && !g_scEditC.dragging) save_ui_config();   // box_edit saved before the sphere's place was written back
            } else { box_on_screen(f, ux, uy, uw, uh); px = ux - dx; py = uy - dy; }
            L.CX = snap(px + cw * 0.5f); L.CY = snap(py + top);
        }
        sc_circle_draw(f, L);
        return;
    }

    float sscl = C.scScale; if (sscl < 0.5f) sscl = 0.5f; if (sscl > 2.0f) sscl = 2.0f;
    const float S = (ovS > 0.0f) ? ovS : (screenH / 1000.0f) * sscl;
    const float pad = (ui_config().scBox.on ? 10.0f : 0.0f) * S + box_inset(ui_config().scBox), lineH = 20.0f * S, titleH = 24.0f * S;   // no box chrome -> hug the content
    const u32 dim = 0xFFC8D2E6u, white = 0xFFEAF0FFu, gold = 0xFFE8C55Au, strk = 0xFF000000u;
    // per-element fonts / sizes / outlines
    Font* fTitle = sc_font(f, SC_TITLE); Font* fTimer = sc_font(f, SC_TIMER); Font* fStep = sc_font(f, SC_STEP);
    Font* fProp = sc_font(f, SC_PROP);   Font* fList = sc_font(f, SC_LIST);
    const float zTitle = sc_sz(SC_TITLE, 16.0f) * S, zTimer = sc_sz(SC_TIMER, 14.0f) * S, zStep = sc_sz(SC_STEP, 14.0f) * S, zProp = sc_sz(SC_PROP, 14.0f) * S, zList = sc_sz(SC_LIST, 14.0f) * S;
    const float oTitle = sc_ow(SC_TITLE, 1.2f) * S, oTimer = sc_ow(SC_TIMER, 1.2f) * S, oStep = sc_ow(SC_STEP, 1.2f) * S, oProp = sc_ow(SC_PROP, 1.2f) * S, oList = sc_ow(SC_LIST, 1.2f) * S;

    // TP indicator, appended to the timer line : GREEN once you have >=1000 TP, so at a glance you know whether the
    // listed WS continuations are actually doable RIGHT NOW (reads self TP from memory -- a packet-only addon can't).
    const int tpRaw = (preview || editing) ? 1000 : (f.game ? f.game->me.tp : 0);
    const int tpVal = tpRaw < 0 ? 0 : (tpRaw > 3000 ? 3000 : tpRaw);
    char tpBuf[16]; sprintf(tpBuf, "TP %d", tpVal);

    // ---- text lines (+ per-element CAPS + colour) ----
    char l2[24]; u32 tcol;
    if      (phase == 0) { sprintf(l2, "Wait  %.1f", timerSec); tcol = 0xFFFF6060u; }
    else if (phase == 1) { sprintf(l2, "Go!  %.1f",  timerSec); tcol = 0xFF66FF66u; }
    else                 { sprintf(l2, "Burst %.1f", timerSec);   // closing burst : fade green (full window) -> yellow -> red (0.0)
        tcol = burstFrac > 0.5f ? sc_mix(0xFFFFD84Au, 0xFF6EE66Eu, (burstFrac - 0.5f) * 2.0f)
                                : sc_mix(0xFFFF5A5Au, 0xFFFFD84Au, burstFrac * 2.0f); }
    char l3pre[24]; sprintf(l3pre, "Step: %d", step);
    const char* aname = (actName && actName[0]) ? actName : "?";
    char up0[24], up2[24], up3[32], up3b[32], pbuf[24];
    const char* title = sc_up(SC_TITLE, "Skillchains", up0, 24);
    const char* l2u = sc_up(SC_TIMER, l2, up2, 24);
    const char* l3preU = sc_up(SC_STEP, l3pre, up3, 32);
    const char* l3name = sc_up(SC_STEP, aname, up3b, 32);
    const float stepArrowW = zStep * 0.93f, stepGap = 4.0f * S;
    const float stepW = fStep->measure(l3preU, zStep) + stepGap + stepArrowW + stepGap + fStep->measure(l3name, zStep);
    const float listArrowW = zList * 0.93f, listGap = 4.0f * S;
    const u32 titleCol = sc_col(SC_TITLE, gold), timerCol = sc_col(SC_TIMER, tcol), stepCol = sc_col(SC_STEP, white), listNameCol = sc_col(SC_LIST, white);
    const u32 propOver = ui_config().scText[SC_PROP].colorOn ? ui_config().scText[SC_PROP].color : 0;   // 0 = keep semantic per-segment
    const SCInfo& si = sc_info(props[0]);   // elements only meaningful once formed

    const bool shTitle = C.scTitle != 0, shTimer = C.scTimer != 0, shStep = C.scStep != 0, shProps = C.scProps != 0, shTP = C.scTP != 0;
    const int  shownList = C.scList ? listN : 0;
    float scGap = C.scListGap; if (scGap < 0.6f) scGap = 0.6f; if (scGap > 3.0f) scGap = 3.0f;
    const float listPit = lineH * scGap;   // per-WS row pitch in the continuation list (config: WS spacing slider)

    // ---- NUKE bandeau (the redesigned property line) : once a chain has FORMED, a bordered strip = "NUKE" + a colour
    // PIP + name per burst element (a nuker reads which element to magic-burst at a GLANCE, not by memorising SC->element).
    // Before a chain forms it's just the resonating property name. The same pips are appended to each continuation row. ----
    const float pPip = zProp * 0.66f, lPip = zList * 0.66f, pipGap = 3.0f * S, bandGap = 7.0f * S, bandPad = 8.0f * S;
    char eb[24];
    float bandContent;
    if (formed) {
        bandContent = fProp->measure(sc_up(SC_PROP, "Nuke:", pbuf, 24), zProp);
        for (int i = 0; i < si.nElem; ++i) bandContent += bandGap + pPip + pipGap + fProp->measure(sc_up(SC_PROP, scelem_name(si.elem[i]), eb, 24), zProp);
    } else {
        bandContent = fProp->measure("[", zProp) + fProp->measure("]", zProp);
        for (int i = 0; i < nProp; ++i) { bandContent += fProp->measure(sc_up(SC_PROP, scprop_name(props[i]), pbuf, 24), zProp); if (i + 1 < nProp) bandContent += fProp->measure(" · ", zProp); }
    }
    const bool  bandBorder = formed && C.scBox.on && C.scBox.border;      // frame the Nuke strip only when the box itself has a border
    const float bandBoxH = lineH + 2.0f * S;                              // the framed strip's height (thin outline only)
    const float bandW = bandBorder ? (bandContent + 2.0f * bandPad) : bandContent;
    const float bandH = bandBorder ? (bandBoxH + 4.0f * S) : lineH;       // vertical space the bandeau consumes

    float bw = shTitle ? fTitle->measure(title, zTitle) : 0.0f;
    if (shTimer) { const float a = fTimer->measure(l2u, zTimer); if (a > bw) bw = a; }
    if (shStep && stepW > bw) bw = stepW;
    if (shProps && bandW > bw) bw = bandW;
    // continuation list = ALIGNED columns : [name] -> [Lv.X] [property] [element pips]. Column widths = the max across
    // rows, so arrow / level / property / pips line up vertically (reused in the draw pass below).
    const float lColGap = 8.0f * S;
    float lcName = 0.0f, lcLvl = 0.0f, lcProp = 0.0f, lcPips = 0.0f;
    for (int i = 0; i < shownList; ++i) {
        char nb[28], pb2[24], lvb[10]; sprintf(lvb, "Lv.%d", list[i].lvl);
        const float wN = fList->measure(sc_up(SC_LIST, list[i].name, nb, 28), zList);
        const float wL = fList->measure(lvb, zList);
        const float wP = fList->measure(sc_up(SC_LIST, scprop_name(list[i].prop), pb2, 24), zList);
        const int ne = sc_info(list[i].prop).nElem;
        const float wPips = ne > 0 ? (ne * lPip + (ne - 1) * pipGap) : 0.0f;
        if (wN > lcName) lcName = wN; if (wL > lcLvl) lcLvl = wL; if (wP > lcProp) lcProp = wP; if (wPips > lcPips) lcPips = wPips;
    }
    if (shownList > 0) { const float lw = lcName + listGap + listArrowW + listGap + lcLvl + lColGap + lcProp + (lcPips > 0.0f ? (lColGap + lcPips) : 0.0f); if (lw > bw) bw = lw; }
    if (shTP) { const float a = fTimer->measure(tpBuf, zTimer) + 18.0f * S; if (a > bw) bw = a; }   // TP gauge pill (bottom)
    if (bw < 40.0f * S) bw = 40.0f * S;   // floor : keep a sane minimum even with everything but one line off
    const float listH = (shownList > 0) ? (7.0f * S + shownList * listPit) : 0.0f;
    const float tpH = shTP ? (lineH + 6.0f * S) : 0.0f;   // the TP indicator line at the very bottom + a small gap above it
    const float bodyH = (shTimer ? lineH : 0.0f) + (shStep ? lineH : 0.0f) + (shProps ? bandH : 0.0f);
    const float boxW = bw + 2.0f * pad, boxH = (shTitle ? titleH : 0.0f) + bodyH + listH + tpH + 2.0f * pad;
    if (measureOnly) { if (outW) *outW = boxW; if (outH) *outH = boxH; return; }   // Help scale-to-fit : report dims, don't draw

    // ---- position (+ edit-mode drag). scX = the box's HORIZONTAL CENTRE (so it grows symmetrically as the
    // content width changes -> the anchor point stays put), scY = its TOP (it grows downward as the list grows). ----
    float px, py;
    if (ovS > 0.0f) { px = snap((ovX - boxW * 0.5f)); py = snap((ovY - boxH * 0.5f)); }        // preview : centre on the given point
    else            { px = box_grow_x(screenW, UiConfig::GB_SC, 0, C.scX, boxW); py = snap(C.scY * screenH); }
    if (editing) { static EditBox g_scEdit; box_edit(f, g_scEdit, EDITBOX_SKILLCHAIN, px, py, boxW, boxH, ui_config().scScale, ui_config().scX, ui_config().scY, C.boxGrow[UiConfig::GB_SC]); }
    if (!editing && ovS <= 0.0f) box_on_screen(f, px, py, boxW, boxH);   // grown past an edge -> slide back into view (draw only)

    // ---- box chrome : dark rounded panel + gold border (Help-box look) ----
    dColorQuadState(dev);
    draw_themed_box(dev, f.skin, px, py, boxW, boxH, ui_config().scBox, 1.0f, S);   // shared themed chrome (frame/transp/theme)

    const float cx = px + boxW * 0.5f;
    float cy = py + pad;
    if (shTitle) { cy += titleH * 0.5f; fTitle->begin(dev); fTitle->draw_c(dev, cx, cy, title, zTitle, titleCol, strk, oTitle); cy += titleH * 0.5f + lineH * 0.5f; }
    else cy += lineH * 0.5f;   // no title -> first body line starts at the top pad
    if (shTimer) { fTimer->begin(dev); fTimer->draw_c(dev, cx, cy, l2u, zTimer, timerCol, strk, oTimer); cy += lineH; }   // timer (phase colour)
    // ---- NUKE bandeau (right under the timer = the nuker's line). FORMED -> "Nuke:" + a colour PIP and name per burst
    // element (a thin outline only when the box has a border) ; not formed yet -> the resonating property name (centred). ----
    if (shProps) {
        if (formed) {
            float sx;
            if (bandBorder) {                                                      // frame only when the box has a border (matches its chrome)
                const float bx = snap(cx - bandW * 0.5f), by = snap(cy - bandBoxH * 0.5f);
                const u32 bcol = (box_style_border_color(dev, f.skin, C.scBox) & 0x00FFFFFFu) | 0xB4000000u;   // the box's OWN theme frame colour (Royal gold / iron / neon / FFXI skin), ~70% alpha
                dColorQuadState(dev);
                rrect_stroke(dev, bx, by, bandW, bandBoxH, 4.0f * S, bcol, 1.2f * S);   // thin outline, no fill, matching the theme
                sx = bx + bandPad;
            } else {
                sx = cx - bandW * 0.5f;
            }
            const char* lbl = sc_up(SC_PROP, "Nuke:", pbuf, 24);
            fProp->begin(dev); fProp->draw_lc(dev, sx, cy, lbl, zProp, dim, strk, oProp); sx += fProp->measure(lbl, zProp);
            for (int i = 0; i < si.nElem; ++i) {                                    // one colour PIP + element name per burst element
                sx += bandGap;
                const u32 ecol = scelem_color(si.elem[i]);
                dColorQuadState(dev); sc_pip(dev, sx, cy, zProp, ecol); sx += pPip + pipGap;
                const char* en = sc_up(SC_PROP, scelem_name(si.elem[i]), eb, 24);
                fProp->begin(dev); fProp->draw_lc(dev, sx, cy, en, zProp, ecol, strk, oProp); sx += fProp->measure(en, zProp);
            }
        } else {
            fProp->begin(dev);
            char sb[24]; float sx = cx - bandW * 0.5f;
            #define SEG(str, col) { const char* _s = (str); fProp->draw_lc(dev, sx, cy, _s, zProp, (col), strk, oProp); sx += fProp->measure(_s, zProp); }
            SEG("[", dim);
            for (int i = 0; i < nProp; ++i) { SEG(sc_up(SC_PROP, scprop_name(props[i]), sb, 24), propOver ? propOver : scprop_color(props[i])); if (i + 1 < nProp) SEG(" · ", dim); }
            SEG("]", dim);
            #undef SEG
        }
        cy += bandH;
    }
    if (shStep)  {   // "Step: N  ->  <move>" with a VECTOR arrow (font can't bake U+2192), centred
        float sx = cx - stepW * 0.5f;
        fStep->begin(dev); fStep->draw_lc(dev, sx, cy, l3preU, zStep, stepCol, strk, oStep); sx += fStep->measure(l3preU, zStep) + stepGap;
        dColorQuadState(dev); sc_arrow(dev, sx, cy, zStep, stepCol); sx += stepArrowW + stepGap;
        fStep->begin(dev); fStep->draw_lc(dev, sx, cy, l3name, zStep, stepCol, strk, oStep);
        cy += lineH;
    }

    // ---- continuation list : ALIGNED columns -> [name] -> [Lv.X] [property] [element pips]. Fixed column x from the max
    // widths measured above, so arrow / level / property / pips line up down the list. ----
    if (shownList > 0) {
        cy += 6.0f * S;   // small gap above the list
        const float lx = px + pad;                                   // col 1 : name
        const float arrowX = lx + lcName + listGap;                  //         arrow
        const float lvlX   = arrowX + listArrowW + listGap;          // col 2 : Lv.X
        const float propX  = lvlX + lcLvl + lColGap;                 // col 3 : property
        const float pipsX  = propX + lcProp + lColGap;               // col 4 : burst-element pips
        char nb[28], pb2[24], lvb[10];
        for (int i = 0; i < shownList; ++i) {
            const char* nm = sc_up(SC_LIST, list[i].name, nb, 28);
            fList->begin(dev); fList->draw_lc(dev, lx, cy, nm, zList, listNameCol, strk, oList);
            dColorQuadState(dev); sc_arrow(dev, arrowX, cy, zList, dim);
            sprintf(lvb, "Lv.%d", list[i].lvl);
            fList->begin(dev); fList->draw_lc(dev, lvlX, cy, lvb, zList, dim, strk, oList);
            const char* pp = sc_up(SC_LIST, scprop_name(list[i].prop), pb2, 24);
            fList->draw_lc(dev, propX, cy, pp, zList, scprop_color(list[i].prop), strk, oList);
            const SCInfo& ei = sc_info(list[i].prop);                 // this result's burst elements -> colour pips
            float ppx = pipsX; dColorQuadState(dev);
            for (int e = 0; e < ei.nElem; ++e) { sc_pip(dev, ppx, cy, zList, scelem_color(ei.elem[e])); ppx += lPip + pipGap; }
            cy += listPit;
        }
    }

    // ---- TP indicator (bottom) : a compact gauge PILL that fills toward 1000 (weaponskill-ready) and turns GREEN once
    // full -> at a glance you know if the listed continuations are actually doable now. Blue while building. ----
    if (shTP) {
        cy += 6.0f * S;
        const bool ready = tpVal >= 1000;
        const float frac = ready ? 1.0f : (float)tpVal / 1000.0f;
        const float pillW = fTimer->measure(tpBuf, zTimer) + 18.0f * S, pillH = zTimer + 6.0f * S, rr = pillH * 0.5f;
        const float bx = snap(cx - pillW * 0.5f), by = snap(cy - pillH * 0.5f);
        dColorQuadState(dev);
        rrect(dev, bx, by, pillW, pillH, rr, 0x552A3450u, 0x55151C30u, 1.2f);                       // dim track
        const u32 fTop = ready ? 0xFF57C878u : 0xFF3E6EA6u, fBot = ready ? 0xFF2E9E52u : 0xFF29477Au;
        if (ready)               rrect(dev, bx, by, pillW, pillH, rr, fTop, fBot, 1.2f);            // full capsule
        else if (frac > 0.03f)   rrect_left(dev, bx, by, pillW * frac, pillH, rr, fTop, fBot, 1.2f); // left-filled gauge (flat level edge)
        fTimer->begin(dev); fTimer->draw_c(dev, cx, cy, tpBuf, zTimer, ready ? 0xFFEFFFF3u : 0xFFDDE5F2u, strk, oTimer);
    }
}

// Live / edit path : the Hud draws the box at its configured screen position.
void Hud::draw_skillchains(const Frame& f, bool preview, float ovX, float ovY, float ovS) {
    skillchains_draw(f, preview, ovX, ovY, ovS, (float)screenW_, (float)screenH_);
}

// Help sample : the REAL Skillchains box in preview mode (config-aware), centred at (cx,cy) at scale `s`.
void skillchains_help_box(const Frame& f, float cx, float cy, float s) {
    skillchains_draw(f, true, cx, cy, s, 0.0f, 0.0f);
}

// Help scale-to-fit : measure at scale 1 (linear in S), pick the largest scale that fits availW (capped at maxScale).
void skillchains_help_fit(const Frame& f, float availW, float maxScale, float& outScale, float& outH) {
    float bw = 0.0f, bh = 0.0f;
    skillchains_draw(f, true, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, true, &bw, &bh);
    float s = (bw > 1.0f) ? (availW / bw) : maxScale;
    if (s > maxScale) s = maxScale; if (s < 0.6f) s = 0.6f;
    outScale = s; outH = bh * s;
}

} // namespace aio
