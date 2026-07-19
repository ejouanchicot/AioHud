// party.cpp -- see party.h.
#include "ui/party.h"
#include "model/paths.h"
#include "ui/edit_box.h"      // edit_box_hover_glow : shared edit-mode hover affordance
#include "ui/liquid_bars.h"   // borrow the real fiole assets for the "Vial" gauge style
#include "ui/ui_colors.h"     // lerp_color / hp_color / scl : shared ARGB helpers (widgets' truncating versions)
#include "ui/text_style.h"       // te_* + fit_ellipsis (the ONE width-driven truncation)
#include "ui/party_internal.h"   // snap / lt / shine_sweep / setup_*_state / vgrad / rrnd (shared with party_gauges.cpp)
#include "gfx/draw.h"
#include "gfx/font.h"
#include "gfx/texture.h"
#include "gfx/window.h"
#include "io/json.h"
#include "model/party_state.h"
#include "model/game_mem.h"
#include "model/gamestate.h"
#include "model/spells_gen.h"
#include "model/abilities_gen.h"
#include "model/weapon_skills_gen.h"
#include "model/zones.h"
#include "model/ui_config.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "ui/buff_atlas.h"

namespace aio {

// Box geometry/fonts are LIVE-TUNABLE per box (Party members + configure()), not statics, so
// layout.json can override every size/font and hot-reload via //aio layout.

// round to the nearest whole pixel -> crisp 1px borders (see draw(): every coord is snapped so
// each row sits at an identical pixel phase).

// ---- baked party (DEMO mode, when no live data ; matches design/src/panels/party.js) ----
struct Member { const char* name; const char* job; const char* sub; unsigned role; int maxHp; int maxMp; const char* cast; int mlvl = 99; };   // mlvl : demo default (endgame party) -> the badge level preview
// 18 baked members = main party (0..5) + alliance party 1 (6..11) + alliance party 2 (12..17).
// A box of tier T reads DEMO[T*6 + i]. Each alliance group gets its own names/jobs so the demo
// looks like a real 3-party alliance. Leader/QM markers are derived by position (see demo_row).
static const Member DEMO[18] = {
    { "Cloud",  "PLD", "WAR", 0xFF7D9BF0, 1188,  980, "Flash"      },
    { "Yuna",   "RDM", "WHM", 0xFF86D36F, 1252, 1220, "Haste II"   },
    { "Squall", "WAR", "SAM", 0xFFE08585, 1444,  100, 0            },
    { "Aerith", "WHM", "SCH", 0xFF86D36F, 1244, 1166, "Cure VI"    },
    { "Lulu",   "BLM", "RDM", 0xFFE08585, 1256, 1648, 0            },
    { "Rikku",  "COR", "RNG", 0xFFECC94A, 1253, 1050, "Chaos Roll" },
    // ---- alliance party 1 ----
    { "Tidus",  "SAM", "WAR", 0xFFE08585, 1390,  220, 0            },
    { "Vivi",   "BLM", "WHM", 0xFFE08585, 1010, 1720, "Firaga III" },
    { "Auron",  "WAR", "SAM", 0xFFE08585, 1620,  120, 0            },
    { "Garnet", "WHM", "SMN", 0xFF86D36F, 1180, 1410, "Curaga III" },
    { "Zidane", "THF", "NIN", 0xFFE08585, 1305,  300, 0            },
    { "Eiko",   "GEO", "WHM", 0xFFECC94A, 1090, 1320, "Indi-Fury"  },
    // ---- alliance party 2 ----
    { "Vaan",   "DNC", "WAR", 0xFFECC94A, 1340,  280, 0            },
    { "Ashe",   "RDM", "BLM", 0xFFECC94A, 1210, 1290, "Refresh"    },
    { "Basch",  "PLD", "RUN", 0xFF7D9BF0, 1530,  640, 0            },
    { "Penelo", "WHM", "BRD", 0xFF86D36F, 1160, 1240, "Cure V"     },
    { "Balthier","COR","RNG", 0xFFECC94A, 1320,  410, "Wizard Roll"},
    { "Fran",   "RNG", "NIN", 0xFFE08585, 1280,  360, 0            },
};

// one display row, resolved from live data OR demo.
struct Row { const char* name; const char* job; const char* sub; unsigned role; int hpVal, mpVal, tp, hpp, mpp; bool plead, alead, qm; bool offzone; int zone; const char* cast; float castPct; float castAlpha; unsigned id; bool sel; bool subsel;
             bool outRange = false;   // beyond spell cast range -> drawn dimmed (out of healing/buff range)
             float dist = -1.0f;      // horizontal distance to the player (yalms) ; <0 = unknown (hidden)
             const unsigned short* buffs = nullptr; int nbuff = 0;   // status icons (left of the row) ; null/0 = none (uint16 ids: > 255 exist)
             int mlvl = 0;      // main-job level -> shown in the job badge (text modes) ; 0 = unknown (no level drawn)
             int slvl = 0; };   // sub-job level (self only : memory @pl+0xA0) -> appended to the sub abbr ; 0 = unknown (abbr only)

// Cast-range thresholds (yalms), from the FFXI distance reference. "max is exclusive" -> a spell
// FAILS at the max. Player-targeted single-target magic reaches ~21.8' on a PC (base ~20.9 + the
// target's model). 20.8' = the Cure reference (comfortable range). So:
//   < 20.8'   blue   (well in range)      20.8'..21.8'  yellow (marginal, still casts)
//   >= 21.8'  red    (out of range -> the member's row is dimmed)
// Distance colour code on the two key FFXI ranges :
//   < 10'        BLUE   : in range of EVERYTHING (cures + the ~10' stuff : Majesty AoE, -ra, ...)
//   10'..20.79'  YELLOW : in standard cast range (20.8') but out of the ~10' spells
//   >= 20.8'     RED    : out of cast range entirely (Cure 20.8' max is exclusive -> fails at 20.80')
//                         -> the row is dimmed. (We track ACTUAL castability, not the game's cursor,
//                          which misleadingly stays yellow up to ~21.3'.)
static const float kCastSafe  = 10.0f;   // blue -> yellow boundary (the ~10' AoE/short-range limit)
static const float kCastRange = 20.8f;   // yellow -> red : standard cast FAILS here (20.79 casts, 20.80 fails)

// ---- buff icons : a single status-icon atlas (assets/buff_atlas.raw, built by
// scripts/gen_buff_atlas.ps1 from XivParty's icon set). Fixed 32-col grid, 32px cells.
// A status id maps to cell (id%COLS, id/COLS) -> see gen_buff_atlas.ps1 for the layout.
// demo buff set (-> something realistic to render in //aio demo). Front = a plausible party buff spread
// (Protect/Shell/Haste/Refresh/Phalanx/Food, COR Chaos+Samurai Rolls, BRD Honor March + two Minuets, Blink,
// Stoneskin) ; the rest keeps the old filler. NOTE: FFXI collapses ALL Minuets to status 198 and ALL Marches
// to 214, so Minuet IV/V share one icon (198). (Dia 134 is a near-white icon -> looks blank ; kept in the tail.)
static const unsigned short BUFF_POOL[] = {
    40, 41, 33, 43, 116, 251, 317, 321, 214, 198, 198, 36, 37,      // the requested realistic spread
    134, 13, 4, 5, 3, 30, 31, 32, 44, 45, 46, 39, 42, 0, 1, 2, 6, 7 // filler (kept from the old pool)
};
static const int BUFF_POOL_N = (int)(sizeof(BUFF_POOL) / sizeof(BUFF_POOL[0]));
static const u32 C_OFF = 0xFF6E7689;   // out-of-zone member (greyed)

// ---- colours ----
static const u32 C_INK = 0xFFF0FFFF, C_DIM = 0xFFB6BFD6, C_GOLD = 0xFFFFDC78, C_BAD = 0xFFFF4646, C_MP = 0xFF4F9DFF;
static u32 tp_color(int tp) { return tp >= 1000 ? 0xFFFF7AE8 : 0xFF7A5C8E; }   // bright pink when a WS is ready (>= 1000), dull muted purple below

// lerp two ARGB colours (t in 0..1).

// The main party box (tier 0) is configured FIRST (file order), and publishes its exact style
// here so the alliance boxes adopt the identical gabarit (size / stroke / bold / font) instead
// of their own config -> they always match the party, even if the design tool regenerates them.
struct PartyStyle {
    float nameSz, nameStroke; bool nameBold; std::string nameFont;
    float barSz, barStroke; bool barBold; std::string barFont;
    float badgeSz, badgeStroke; bool badgeBold; std::string badgeFont;
    bool ready;
};
static PartyStyle g_mainStyle = { 0,0,false,"", 0,0,false,"", 0,0,false,"", false };

// shared vertical-stack origin : the main party box publishes its TOP Y so the alliance boxes
// stack upward, starting ABOVE the cost/next action box that floats over the party.
static float g_partyTopY = 0.0f;
static float g_partyStackTop = 0.0f;   // top of the party CLUSTER (party box top - the floating Cost MP/Next box) -> alliances stack here
static bool  g_partyTopReady = false;

// edit-mode drag state (shared across the 3 box widgets so only ONE is grabbed at a time).
static int   g_dragTier = -1;            // tier currently being dragged (-1 = none)
static float g_grabDX = 0.0f, g_grabDY = 0.0f;
// last-drawn rect of each box (px), for magnetic edge snapping while dragging.
static struct BoxRect { float x, y, w, h; bool valid; } g_boxRect[3] = {};

void Party::configure(const json::Value& cfg) {
    int n = (int)cfg["count"].as_num(6);
    if (n < 1) n = 1; if (n > MAXM) n = MAXM;
    count_ = n;
    tier_ = (int)cfg["tier"].as_num(tier_);               // which alliance group this box shows (0 = main)
    if (tier_ < 0) tier_ = 0; if (tier_ > 2) tier_ = 2;   // clamp : raw tier_ indexes size-3 per-box arrays (box[]/border[]/g_boxRect[]) -> OOB on a bad layout.json
    char key[16];
    for (int i = 0; i < MAXM; ++i) {
        sprintf(key, "p%d_hp", i); dhp_[i] = (int)cfg[key].as_num(dhp_[i]);
        sprintf(key, "p%d_mp", i); dmp_[i] = (int)cfg[key].as_num(dmp_[i]);
        sprintf(key, "p%d_tp", i); dtp_[i] = (int)cfg[key].as_num(dtp_[i]);
        sprintf(key, "p%d_buffs", i); dbuff_[i] = (int)cfg[key].as_num(dbuff_[i]);
    }
    // live-tunable style : per-text size / stroke / bold / font (everything else auto) -----
    nameSz_      = (float)cfg["nameSz"].as_num(nameSz_);
    nameStroke_  = (float)cfg["nameStroke"].as_num(nameStroke_);
    nameBold_    = cfg["nameBold"].as_bool(nameBold_);
    nameFont_    = cfg["nameFont"].as_str(nameFont_.c_str());
    barSz_       = (float)cfg["barSz"].as_num(barSz_);
    barStroke_   = (float)cfg["barStroke"].as_num(barStroke_);
    barBold_     = cfg["barBold"].as_bool(barBold_);
    barFont_     = cfg["barFont"].as_str(barFont_.c_str());
    badgeSz_     = (float)cfg["badgeSz"].as_num(badgeSz_);
    badgeStroke_ = (float)cfg["badgeStroke"].as_num(badgeStroke_);
    badgeBold_   = cfg["badgeBold"].as_bool(badgeBold_);
    badgeFont_   = cfg["badgeFont"].as_str(badgeFont_.c_str());

    if (tier_ == 0) {                                     // main party : publish the canonical gabarit
        g_mainStyle = { nameSz_, nameStroke_, nameBold_, nameFont_,
                        barSz_, barStroke_, barBold_, barFont_,
                        badgeSz_, badgeStroke_, badgeBold_, badgeFont_, true };
    } else if (g_mainStyle.ready) {                       // alliance : inherit the party's gabarit EXACTLY
        nameSz_ = g_mainStyle.nameSz; nameStroke_ = g_mainStyle.nameStroke; nameBold_ = g_mainStyle.nameBold; nameFont_ = g_mainStyle.nameFont;
        barSz_  = g_mainStyle.barSz;  barStroke_  = g_mainStyle.barStroke;  barBold_  = g_mainStyle.barBold;  barFont_  = g_mainStyle.barFont;
        badgeSz_= g_mainStyle.badgeSz;badgeStroke_= g_mainStyle.badgeStroke;badgeBold_= g_mainStyle.badgeBold;badgeFont_= g_mainStyle.badgeFont;
    }
}

static void fill_member(Row& r, const PMember& pm) {
    r.name = pm.name; r.job = job_abbr(pm.mjob); r.sub = job_abbr(pm.sjob); r.role = job_role_color(pm.mjob); r.mlvl = pm.mlvl; r.slvl = pm.slvl;
    r.hpVal = pm.hp; r.mpVal = pm.mp; r.tp = pm.tp; r.hpp = pm.hpp; r.mpp = pm.mpp;
    r.plead = pm.party_lead(); r.alead = pm.alliance_lead(); r.qm = pm.quarter_master();
    r.cast = party().cast_label(pm.id, r.castPct, r.castAlpha);   // live spell cast (0 if not casting)
    r.offzone = (pm.maxHp == 0);                       // no vitals at all -> member is out of our zone
    r.outRange = (pm.dist >= 0.0f && pm.dist >= kCastRange);  // in zone but beyond cast range -> dim
    r.dist = pm.dist;
    r.zone = pm.zone;
    r.id = pm.id; r.sel = false; r.subsel = false;
    const BuffSet* bs = party().buffs_for(pm.id);      // status icons from packet 0x076 (null until first seen)
    if (bs) { r.buffs = bs->ids; r.nbuff = bs->n; }
}
static void fill_self(Row& r, const PlayerInfo& me) {
    r.name = me.name; r.job = job_abbr(me.mjob); r.sub = job_abbr(me.sjob); r.role = job_role_color(me.mjob); r.mlvl = me.mlvl; r.slvl = me.slvl;
    r.hpVal = me.hp; r.mpVal = me.mp; r.tp = me.tp; r.hpp = me.hpp; r.mpp = me.mpp;
    r.plead = false; r.alead = false; r.qm = false; r.offzone = false; r.zone = 0; r.outRange = false; r.dist = -1.0f;   // self : no distance (always 00.00)
    r.cast = party().cast_label(me.id, r.castPct, r.castAlpha);   // self can cast too (own action packet echoes back)
    r.id = me.id; r.sel = false; r.subsel = false;
}

// build the displayed rows : live = local player (memory, always present + accurate) + the
// OTHER members from packets (the game never sends you your own party packet) ; else demo.
int Party::build_rows(void* outRows, const GameState& gs) const {
    Row* rows = (Row*)outRows;
    int n = 0;
    const int demo = ui_config().editLayout ? 3 : party_demo_level();   // edit mode : force the FULL demo (3 boxes, 6 each)
    // leaders / target come from the shared snapshot (0 = role/selection absent ; a real member
    // id is never 0, so comparing against a 0 field can't false-positive -> no extra "have" guard).
    auto isAlead = [&](unsigned id) { return id && id == gs.allianceLeader; };
    auto isPlead = [&](unsigned id) { return id && (id == gs.partyLead1 || id == gs.partyLead2 || id == gs.partyLead3); };

    // Alliance boxes (tier > 0). A demo command forces the baked roster (level 2 = +alliance1,
    // level 3 = +alliance2) ; otherwise LIVE from the member array (slots 6..17, via load_from_memory).
    if (tier_ > 0) {
        if (demo >= 1) {                                                      // demo command active
            if (demo <= tier_) return 0;                                     // this alliance tier not requested
            int ac = ui_config().editLayout ? 6 : party_demo_alliance_count(tier_);   // //aio party N -> partial alliance
            if (ac < 1) ac = 1; else if (ac > 6) ac = 6;
            for (int i = 0; i < ac; ++i) demo_row(i, &rows[i]);
            return ac;
        }
        int cnt = party().alliance_count(tier_);
        if (cnt <= 0) return 0;                                              // no such alliance party -> box hidden
        if (cnt > 6) cnt = 6;
        for (int i = 0; i < cnt; ++i) {
            const PMember& pm = party().alliance_member(tier_, i);
            fill_member(rows[i], pm);
            rows[i].alead = isAlead(pm.id); rows[i].plead = isPlead(pm.id);
            if (gs.targetId    && pm.id == gs.targetId)    rows[i].sel    = true;   // alliance members are targetable too
            if (gs.subTargetId && pm.id == gs.subTargetId) rows[i].subsel = true;
        }
        return cnt;
    }
    // Main party box : a demo command forces the baked roster; else live / cached fallback. The demo
    // member COUNT is configurable (//aio party N, N<=6) so you can preview the adaptive height / mask /
    // Set-Ref growth at any size -- edit mode keeps the full 6 for the footprint.
    if (demo >= 1) {
        const int dc = ui_config().editLayout ? 6 : party_demo_count();
        for (int i = 0; i < dc; ++i) demo_row(i, &rows[i]);
        return dc;
    }

    // self comes from the snapshot: gs.me lives in the HUD's GameState -> it outlives this call,
    // so fill_self can safely store r.name = gs.me.name (the rows are rendered by the caller).
    const PlayerInfo& me = gs.me;
    if (gs.inGame) {                                                          // in game : self ALWAYS first
        fill_self(rows[n], me);                                              // row 0 = self (snapshot : instant + accurate)
        rows[n].buffs = gs.buffs; rows[n].nbuff = gs.nbuff;                  // self buffs from the once/frame snapshot (no poll-in-draw ; gs outlives this call)
        int sidx = party().find(me.id);
        if (sidx >= 0) rows[n].qm = party().m[sidx].quarter_master();        // QM still from member flag (tentative)
        rows[n].alead = isAlead(me.id); rows[n].plead = isPlead(me.id);
        ++n;
        for (int i = 0; i < party().count && n < 6; ++i) {                   // then the OTHER members
            if (party().m[i].id == me.id) continue;                         // (self already shown on top)
            unsigned mid = party().m[i].id;
            fill_member(rows[n], party().m[i]);
            rows[n].alead = isAlead(mid); rows[n].plead = isPlead(mid);
            ++n;
        }
        // selection cursor : highlight the row whose server-id == current <t> / <st> (snapshot).
        for (int i = 0; i < n; ++i) {
            if (gs.targetId    && rows[i].id == gs.targetId)    rows[i].sel    = true;
            if (gs.subTargetId && rows[i].id == gs.subTargetId) rows[i].subsel = true;
        }
        // party-window picker (Quartermaster / Lottery / remove member / ...) : frame the hovered
        // member -- 1-based cursor index into the party list (= our row order). Reversed via //aio pcur.
        if (gs.partyMenuSel >= 1 && gs.partyMenuSel <= n) rows[gs.partyMenuSel - 1].subsel = true;   // party-menu picker -> BLUE (sub) frame, not the gold target/lock
        // TEST (//aio sim N) : append N fake members so the live party box grows and the alliances react
        // to its size -- lets you verify the dynamic placement without needing real extra players.
        for (int k = party_sim_extra(); k > 0 && n < 6; --k) { demo_row(n, &rows[n]); ++n; }
    } else {                                                                 // not in game -> demo
        n = count_ > 6 ? 6 : count_;
        for (int i = 0; i < n; ++i) row(i, &rows[i]);
    }
    return n;
}

void Party::row(int i, void* out) const {
    Row& r = *(Row*)out;
    if (party().count > 0) {                       // ---- cached live roster (instant party at load, pre-login) ----
        r.offzone = false; r.zone = 0; r.id = 0; r.sel = false; r.subsel = false; r.castPct = 0.0f; r.castAlpha = 0.0f;
        const PMember& pm = party().m[i];
        r.name = pm.name; r.job = job_abbr(pm.mjob); r.sub = job_abbr(pm.sjob);
        r.role = job_role_color(pm.mjob); r.mlvl = pm.mlvl; r.slvl = pm.slvl;
        r.hpVal = pm.hp; r.mpVal = pm.mp; r.tp = pm.tp; r.hpp = pm.hpp; r.mpp = pm.mpp;
        r.plead = pm.party_lead(); r.alead = pm.alliance_lead(); r.qm = pm.quarter_master();
        r.cast = party().cast_label(pm.id, r.castPct, r.castAlpha);
        r.id = pm.id;
    } else {                                       // ---- baked demo (no roster yet) ----
        demo_row(i, out);
    }
}

// Forced demo row (command-driven //aio demo) : always baked, tier-offset into DEMO so each
// alliance box shows its own group of names/jobs. dhp_/dmp_/dtp_ come from this box's config.
void Party::demo_row(int i, void* out) const {
    Row& r = *(Row*)out;
    // synthetic NON-ZERO id : anim_for() treats id==0 as a free slot, so id-0 demo rows would all
    // collapse onto one anim slot (dots never accumulate -> no leader/QM bullets). Unique per row.
    r.offzone = false; r.zone = 0; r.id = (unsigned)(tier_ * 10 + i + 1); r.sel = false; r.subsel = false; r.castPct = 0.0f; r.castAlpha = 0.0f;
    r.dist = (float)i * 6.7f; r.outRange = (r.dist > kCastRange);   // demo : spread distances, last rows out of range
    const Member& dm = DEMO[tier_ * 6 + i];
    const bool edit = ui_config().editLayout || party_demo_level() > 0;   // edit / //aio demo : spread every value across the range so all gauge states are visible at once
    // PREVIEW spread : HP high/mid/low(+critical blink), MP mixed, TP empty / just-ready(1000) / 1500 / 2000 / 3000 / charging.
    static const int EHP[6] = { 100,  76,  52,  24,   9,  88 };
    static const int EMP[6] = {  65, 100,  34,  82,  12,  48 };
    static const int ETP[6] = {   0, 1000, 1500, 2000, 3000, 780 };
    const int ei = i % 6;
    int hp = edit ? EHP[ei] : dhp_[i]; if (hp < 0) hp = 0; if (hp > 100) hp = 100;
    int mp = edit ? EMP[ei] : dmp_[i]; if (mp < 0) mp = 0; if (mp > 100) mp = 100;
    int tp = edit ? ETP[ei] : dtp_[i]; if (tp < 0) tp = 0; if (tp > 3000) tp = 3000;
    r.name = dm.name; r.job = dm.job; r.sub = dm.sub; r.role = dm.role; r.mlvl = dm.mlvl; r.slvl = dm.mlvl / 2;   // demo preview : classic floor(main/2) sub cap
    r.hpp = hp; r.mpp = mp; r.tp = tp; r.hpVal = (dm.maxHp * hp + 50) / 100; r.mpVal = (dm.maxMp * mp + 50) / 100;
    // leader markers (demo) : one PARTY lead per box (row 0), one ALLIANCE lead total (the main
    // party's leader), one QUARTERMASTER total (a distinct main-party member).
    r.plead = (i == 0);
    r.alead = (tier_ == 0 && i == 0);
    r.qm    = (tier_ == 0 && i == 1);
    r.cast = dm.cast; r.castAlpha = dm.cast ? 1.0f : 0.0f;
    r.sel = (tier_ == 0 && i == 0);                // only the main box shows the self cursor
    int nb = edit ? BUFF_POOL_N : dbuff_[i]; if (nb < 0) nb = 0; if (nb > BUFF_POOL_N) nb = BUFF_POOL_N;   // edit : max buffs
    r.buffs = BUFF_POOL; r.nbuff = (tier_ == 0) ? nb : 0;   // alliance members have no buffs (game doesn't send them)
}

static const float BOOST = 1.25f;   // party-wide size boost over the global UI scale (internal)
static float g_partyBandPx = 0.0f;  // party (tier 0) name-line height in px, cached each frame -> alliance cursors reuse it for an IDENTICAL hand size (scale_ is global, so this is comparable across tiers)

// Box width in BASE px : ALWAYS auto-fit to the columns, so it adapts when you change the
// name/badge/bar font sizes (marks + badge + name col + 3 gauges + inter-column gaps).
// No font here, so the name column is approximated from nameSz_.
float Party::box_w_base() const {
    const float gauges  = 3.0f * gaugeW() + 2.0f * gaugeGap();   // HP/MP/TP
    const float nameCol = nameSz_ * 5.0f * ui_config().text[tcfg()][TE_NAME].size + 12.0f;   // ~name (grows with its text size) + gap before the HP gauge
    return 2.0f * padB() + 18.0f + marksW() + badgeW() + nameCol + gauges;   // 18 = the 4+5+5+4 column gaps
}

void Party::measure(float& w, float& h) const {
    const float S = scale_ * BOOST * ui_config().box[tcfg()].scale;  // per-box size (grouped : both alliances share ; config / edit-mode wheel)
    w = box_w_base() * S;
    h = (rowPit() * 6 + 2 * padB()) * S;   // always sized for a full party of 6 (fixed box, solo or not)
}

// CONFIG-PREVIEW ONLY : the stage's left edge (px) past which the LEFTWARD member buff strip must not draw
// -> icons fill leftward until they'd cross this X, then the leftmost fitting cell shows "+N" (hidden count).
// 0 = live HUD (no cap, every buff drawn). Set by the Hud around the party-preview draw, reset right after.
static float g_previewBuffLeft = 0.0f;
void set_party_preview_buff_left(float x) { g_previewBuffLeft = x > 0.0f ? x : 0.0f; }

static const char* ICON_PATH() { static char b[260]; if (!b[0]) plugin_path(b, 260, "assets\\hand_cursor.raw"); return b; }
// job-emblem atlas (white masks, tinted per role) : 8 cols x 3 rows of 64px cells, in JOBS[1..22] order
// (WAR = cell 0 ... RUN = cell 21). Built by the python step from assets\job_icons_src\*.png.
static const char* JOBICON_PATH() { static char b[260]; if (!b[0]) plugin_path(b, 260, "assets\\job_icons.raw"); return b; }
static const int JI_W = 512, JI_H = 192, JI_CELL = 64, JI_COLS = 8;

// draw one job emblem (white mask) at (x,y,sz) from the atlas cell, tinted by `tint` (role colour).
// Leaves the device textured -> the caller's next draw resets it.
static void draw_job_icon(u32 dev, u32 tex, float x, float y, float sz, int cell, u32 tint) {
    if (!tex || cell < 0) return;
    setup_tex_state(dev, tex);
    const float au = (float)JI_CELL / (float)JI_W, av = (float)JI_CELL / (float)JI_H;
    // crop the transparent margin baked into each cell (measured : >= 6px empty on every side of every job
    // in JOBS[1..22]) so the emblem fills the badge instead of floating with air around it. 6/64 per side is
    // the SAFE max -- more would clip the widest emblems (WAR/PLD/… have only ~6-7px side margin).
    const float cr = 6.0f / (float)JI_CELL;                      // fractional crop per side
    const float cu = au * cr, cv = av * cr;
    const float u0 = (float)(cell % JI_COLS) * au + cu, u1 = (float)(cell % JI_COLS) * au + au - cu;
    const float v0 = (float)(cell / JI_COLS) * av + cv, v1 = (float)(cell / JI_COLS) * av + av - cv;
    tquad(dev, x, y, sz, sz, u0, u1, v0, v1, tint, tint);   // single role-colour tint
    dSetTex(dev, 0, 0);
}

void Party::ensure(u32 dev) {
    if (!valid_ptr(dev)) return;
    if (!dot_tex_) dot_tex_ = make_dot(dev);
    if (!icon_tex_ && !icon_tried_) { icon_tex_ = load_raw_texture(dev, ICON_PATH(), 128, 128); icon_tried_ = true; }
    if (!buff_tex_ && !buff_tried_) { buff_tex_ = load_raw_texture(dev, buff_atlas_path(), BUFF_ATLAS_W, BUFF_ATLAS_H); buff_tried_ = true; }
    if (!jobicon_tex_ && !jobicon_tried_) { jobicon_tex_ = load_raw_texture(dev, JOBICON_PATH(), JI_W, JI_H); jobicon_tried_ = true; }
}
void Party::on_device_lost() { dot_tex_ = 0; icon_tex_ = 0; icon_tried_ = false; buff_tex_ = 0; buff_tried_ = false; jobicon_tex_ = 0; jobicon_tried_ = false; }   // forget (dead device), reload next ensure
void Party::dispose() { release_texture(dot_tex_); dot_tex_ = 0; release_texture(icon_tex_); icon_tex_ = 0; icon_tried_ = false; release_texture(buff_tex_); buff_tex_ = 0; buff_tried_ = false; release_texture(jobicon_tex_); jobicon_tex_ = 0; jobicon_tried_ = false; }

// find the persisted animation slot for a member (or claim a free/stale one).
Party::RowAnim* Party::anim_for(unsigned id) {
    for (int i = 0; i < 8; ++i) if (anim_[i].id == id && id) return &anim_[i];
    for (int i = 0; i < 8; ++i) if (!anim_[i].id || !anim_[i].seen) {
        anim_[i] = RowAnim(); anim_[i].id = id; anim_[i].hpp = anim_[i].mpp = anim_[i].tpp = -1.0f; return &anim_[i];
    }
    return &anim_[0];
}

// Buff status icons LEFT of each party row : ONE row of icons sized to the row height (capped at
// 20), right-to-left from just left of the selection cursor. Atlas id -> cell (id%32, id/32).
// A static helper (it iterates the file-local Row) -> keeps draw() focused. buffTex 0 = no-op.
static void draw_member_buffs(u32 dev, u32 buffTex, const Row* rows, int n,
                              float px, float oy, float pad, float rowpit, float rowh, float S, float curBand, float iconH,
                              Font* font) {
    if (!buffTex) return;
    setup_tex_state(dev, buffTex, false);               // MIPFILTER NONE : pixel-exact buff atlas (kept crisp)
    const float bgap = snap(1.0f * S);                  // gap between icons
    float csz = ui_config().cursorScale; if (csz < 0.5f) csz = 0.5f; if (csz > 2.0f) csz = 2.0f;
    const float curW = curBand * 1.30f * csz;   // curBand = snap(coreBandH()*S) from the caller -> MUST match the cursor draw so the strip hugs the real, smaller cursor
    const float bmar = snap(curW * 0.55f + 6.0f * S);   // just LEFT of the VISIBLE cursor, + a small breathing gap
    const float au = (float)BUFF_CELL / (float)BUFF_ATLAS_W;    // one cell, in UV space
    const float av = (float)BUFF_CELL / (float)BUFF_ATLAS_H;
    int bmaxCfg = ui_config().buffMax; if (bmaxCfg < 0) bmaxCfg = 0; if (bmaxCfg > 32) bmaxCfg = 32;   // config choice (0 = no buffs)
    // ONE or TWO rows (config: Buff Rows). Two-row keeps a fixed icon size and reserves both rows so the
    // party size is stable ; one-row uses a bigger (full-line-tall) icon. The reserved band (party.h
    // buffBandH) matches whichever mode is active, so the row grows to fit.
    const bool  twoMode = (ui_config().buffRows > 1);
    const int   PERROW = twoMode ? 16 : 32;             // 2 rows : 16 + 16 ; 1 row : up to 32 on the single line
    const float vgap  = snap(1.0f * S);
    const float bs    = iconH;                          // constant icon size (row grows to fit)
    const float totalH = twoMode ? (2.0f * bs + vgap) : bs;
    const int   rowsN = twoMode ? 2 : 1;
    // CONFIG-PREVIEW ONLY : cap the LEFTWARD strip at the stage's left edge so it can't spill the rect ; the
    // clipped buffs collapse into a "+N" marker at the leftmost fitting cell. lim <= 0 (live HUD) = no cap.
    const float lim  = g_previewBuffLeft;
    const bool  cap  = lim > 0.0f;
    struct Mark { float x, y; int nn; };
    Mark marks[12]; int nmark = 0;                       // <= 6 members x 2 rows
    for (int i = 0; i < n; ++i) {
        const Row& r = rows[i];
        if (r.offzone || !r.buffs || r.nbuff <= 0) continue;
        const int   nbAll = r.nbuff < bmaxCfg ? r.nbuff : bmaxCfg;
        const float ry    = oy + pad + i * rowpit;
        const float top   = snap(ry + (rowh - totalH) * 0.5f);   // the (reserved) block CENTRED vertically in the row
        const float xr    = px - bmar;                  // right edge of the strip (just left of the cursor)
        for (int rw = 0; rw < rowsN; ++rw) {
            const int start = rw * PERROW;
            int cnt = nbAll - start; if (cnt < 0) cnt = 0; if (cnt > PERROW) cnt = PERROW;
            const float y = snap(top + (float)rw * (bs + vgap));
            // how many WHOLE cells fit before the left cap (cell j's left edge = xr - (j+1)*bs - j*bgap >= lim).
            // Live HUD (no cap) : draw them all (the x<1 screen-edge guard still applies below).
            int drawN = cnt;                            // icons to draw as real icons
            bool clip = false;                          // clipped -> reserve the leftmost cell for "+N"
            if (cap) {
                int fit = (int)((xr - bs - lim) / (bs + bgap)) + 1;   // cells with left edge >= lim
                if (fit < 0) fit = 0; if (fit > cnt) fit = cnt;
                if (fit < cnt) { clip = true; drawN = fit - 1; if (drawN < 0) drawN = 0; }   // last fitting cell -> "+N"
                else drawN = cnt;
            }
            for (int j = 0; j < drawN; ++j) {
                const float x = snap(xr - (float)(j + 1) * bs - (float)j * bgap);
                if (!cap && x < 1.0f) break;            // live : ran off the left of the screen -> stop
                const int id = r.buffs[start + j];
                if (id < 0 || id >= BUFF_COLS * BUFF_ATLAS_ROWS) continue;   // id outside the atlas -> skip
                const float u0 = (float)(id % BUFF_COLS) * au;
                const float v0 = (float)(id / BUFF_COLS) * av;
                tquad(dev, x, y, bs, bs, u0, u0 + au, v0, v0 + av, 0xFFFFFFFF, 0xFFFFFFFF);
            }
            if (clip && drawN >= 0 && nmark < 12) {     // "+N" (hidden count) in the leftmost fitting cell
                const int cellIdx = drawN;              // the cell just left of the last drawn icon
                marks[nmark].x  = snap(xr - (float)(cellIdx + 1) * bs - (float)cellIdx * bgap);
                marks[nmark].y  = y;
                marks[nmark].nn = cnt - drawN;          // buffs not shown as icons
                ++nmark;
            }
        }
    }
    dSetTex(dev, 0, 0);
    // draw the "+N" markers (preview only) in ONE font pass, centred on their reserved cell.
    if (cap && font && nmark > 0) {
        font->begin(dev);
        const float fsz = bs * 0.66f;                   // fits inside a single icon cell
        for (int m = 0; m < nmark; ++m) {
            int nn = marks[m].nn; if (nn > 99) nn = 99;
            char buf[8]; snprintf(buf, sizeof(buf), "+%d", nn);
            font->draw_c(dev, marks[m].x + bs * 0.5f, marks[m].y + bs * 0.5f, buf, fsz, 0xFFFFFFFF, 0xE0000000, 1.4f);
        }
        dSetTex(dev, 0, 0);
    }
}

// ---- per-element typography (ui_config().text[TE_*]) : each text element resolves its own Font + size /
// outline / colour / UPPERCASE from the global TextStyle, on top of the layout's base face/bold/size. ----
static int g_txtGrp = 0;   // typography config group of the box being drawn : 0 = Party, 1 = Alliance (set at Party::draw start)
static Font* te_font(FontManager* fm, int elem, const char* ovGlobal, const char* baseFace, bool baseBold) {
    const TextStyle& t = ui_config().text[g_txtGrp][elem];
    const char* face = t.face > 0 ? ui_font_face(t.face) : (ovGlobal[0] ? ovGlobal : baseFace);
    // Bold toggle is the SOLE authority : OFF = regular 400, ON = bold 700. So the button state always
    // matches what's on screen (the layout's own bold no longer forces it on when the toggle is off).
    (void)baseBold;
    return fm->get(face, t.bold ? 700 : 400, t.italic);
}
static inline float te_sz(int e, float base)  { return base * ui_config().text[g_txtGrp][e].size; }
static inline float te_ow(int e, float base)  { return base * ui_config().text[g_txtGrp][e].outline; }
static inline u32   te_col(int e, u32 base)   { const TextStyle& t = ui_config().text[g_txtGrp][e]; return t.colorOn ? t.color : base; }   // honours the custom RGBA (incl. alpha)
static const char*  te_up(int e, const char* s, char* buf, int n) {
    if (!ui_config().text[g_txtGrp][e].upper || !s) return s;
    int i = 0; for (; s[i] && i < n - 1; ++i) { char c = s[i]; buf[i] = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c; } buf[i] = 0; return buf;
}

void Party::draw(const Frame& f) {
    if (!visible_) return;
    u32 dev = f.dev;
    if (!valid_ptr(dev)) return;
    ensure(dev);                                  // lazily create the bullet dot texture
    g_txtGrp = tcfg();                             // this box's typography group (0 = Party, 1 = Alliance) for te_* below

    Row rows[6];
    static const GameState NOGAME{};              // fallback if drawn without a snapshot (demo still works) ; {} : newer MSVC needs a const object value-initialised
    const int n = build_rows(rows, f.game ? *f.game : NOGAME);
    if (n <= 0) return;                           // nothing to show (e.g. an alliance box outside demo)

    // CONFIG PREVIEW : demonstrate the target cursor by sliding ONE selection through all 18 demo
    // slots in the order PARTY -> ALLIANCE 2 -> ALLIANCE 1, then loop. Only this box's member when
    // the cursor is in its block. (block 0 = party/tier0, 1 = ally2/tier2, 2 = ally1/tier1.)
    if (demo_select() && party_demo_level() > 0) {
        const int gsel  = ((int)(f.t / 1.5f)) % 18;            // advance every 1.5s
        const int block = (tier_ == 0) ? 0 : (tier_ == 2 ? 1 : 2);
        const int local = gsel - block * 6;
        for (int i = 0; i < n; ++i) rows[i].sel = (i == local);
    }

    const float S = scale_ * BOOST * ui_config().box[tcfg()].scale;  // per-box size (grouped : both alliances share ; config / edit-mode wheel)
    // Live, //aio demo AND edit layout all share the SAME height + spacing (adaptive shrink, mask band,
    // Set-Ref growth, even member distribution) -> demo + edit are faithful PREVIEWS of the real in-game
    // party : the member spacing you tune live shows up identically everywhere, and you arrange the box
    // exactly where it will really sit. (Drag/snap operate on this real rect via boxOy/boxH below.)
    // Snap ALL box geometry to whole pixels so EVERY row sits at an identical pixel phase ->
    // the 1px borders (badge / selection frame) are crisp on every row, never blurred or
    // "truncated" on some rows (which happens with a fractional row pitch).
    float px = snap(px_);
    float oy = snap(py_);
    const float pad = snap(padB() * S), rowh = snap(rowH() * S);
    float rowpit = snap(rowPit() * S);   // may be widened below to evenly distribute members across the box
    const float gw = snap(gaugeW() * S), gh = snap(gaugeH() * S), ggap = snap(gaugeGap() * S);
    const float bw = snap(badgeW() * S), bh = snap(badgeH() * S), mw = snap(marksW() * S);
    const float mh = snap(mainBandH() * S);   // MAIN BAND : badge / name / gauges / distance all centre in [ry, ry+mh] ; the cast line sits below it
    const float inset = snap(4 * S), gap5 = snap(5 * S);
    const float w   = snap(box_w_base() * S);
    float H   = rowpit * 6 + 2 * pad;   // alliances : always full 6-row height. Party : adapted below.
    // Vertical stacking : the main party box publishes its top Y ; alliance boxes ignore their
    // authored Y and stack UPWARD from just above the floating cost/next action box. The two
    // alliances stack FLUSH (border against border, no gap) -> sepH = 0.
    // A user position override (set in edit mode) WINS over the default placement / stacking.
    const bool posSet = ui_config().box[tier_].posSet && f.screenW > 0.0f;
    if (posSet) { px = snap(ui_config().box[tier_].x * f.screenW); oy = snap(ui_config().box[tier_].y * f.screenH); }

    const float sepH = 0.0f;                               // no separator between the two alliance boxes
    // alliances stack on the party's CURRENT top (its placed/dragged position if set, else the default)
    // -> the demo preview is coherent with where you actually put the party. Drag an alliance to give it
    // its own independent position (then it ignores this stack).
    if (tier_ == 0) {
        g_partyTopY = (ui_config().box[0].posSet && f.screenH > 0.0f) ? snap(ui_config().box[0].y * f.screenH) : snap(py_);
        g_partyTopReady = true;
    }
    else if (!posSet && g_partyTopReady) {                 // alliances stack on the party ONLY when not user-placed
        const float fsC      = nameSz_ * S;
        const float costBoxH = 2.0f * fsC + 20.0f * S;     // reserve the 2-line cost/next box (2 lines + padding + the 10px top space ; keep == costH below)
        const float allLift  = 0.0f;                       // alliance 1 sits FLUSH on the cost box top (no gap) -> matches a hand-placed "glued to Cost MP/Next" layout
        // Stack each alliance FLUSH on the box just below it, using that box's ACTUAL height -> different
        // per-tier scales no longer leave a gap / overlap at the join. Tier 1 sits above the cost box ;
        // tier 2 sits on tier 1's REAL top (g_boxRect[1]), not on a 2*H assumption of equal heights.
        const float baseBottom = (tier_ == 2 && g_boxRect[1].valid)
                                 ? g_boxRect[1].y - sepH
                                 : g_partyTopY - costBoxH - allLift;
        oy = snap(baseBottom - H);
        // RIGHT-align to the party's right edge (published in g_boxRect[0]) so an alliance at a SMALLER
        // scale (narrower box) still lines up on the right instead of drifting left of the party.
        // Clamp on-screen : a stale/off-screen party rect (e.g. just after the config preview) must never
        // fling the alliance off the right edge in edit layout.
        if (g_boxRect[0].valid) {
            float ax = g_boxRect[0].x + g_boxRect[0].w - w;
            if (f.screenW > 0.0f && ax + w > f.screenW) ax = f.screenW - w;
            if (ax < 0.0f) ax = 0.0f;
            px = snap(ax);
        }
    }

    // MARKER-DRIVEN ALLIANCE PLACEMENT : when the four native alliance markers are set, drop our alliance
    // boxes onto the native layout (BOTTOM-anchored, keeping our readable row height). The LOWER slot (the
    // one nearest the party) has its bottom clamped to the party's LIVE top -> as the main party grows it
    // covers the bottom and the alliance rides UP ; the UPPER slot stacks flush on the lower one. So our
    // alliances move with the main party's member count, exactly like the native. (px / right-align kept.)
    if (tier_ > 0 && f.screenH > 0.0f) {
        float az[4]; const float* ar = guide_alliance_refs(az) ? az : ui_config().allyRefY;   // alliance ZONES override the old markers
        if (ar[0] >= 0.0f && ar[1] >= 0.0f && ar[2] >= 0.0f && ar[3] >= 0.0f) {
            const bool aLower = (ar[1] >= ar[3]);                          // which marker pair sits lower (near party)
            const float loB = (aLower ? ar[1] : ar[3]) * f.screenH;       // lower slot bottom
            const float hiB = (aLower ? ar[3] : ar[1]) * f.screenH;       // upper slot bottom
            float bottom = (tier_ == 1) ? loB : ((g_boxRect[1].valid) ? g_boxRect[1].y : hiB);
            if (g_partyTopReady && bottom > g_partyStackTop) bottom = g_partyStackTop;   // sit on the Cost MP/Next box top, not the party rows
            oy = snap(bottom - H);
        }
    }

    // EDIT LAYOUT safety : an alliance must never end up off-screen (a stale party rect, a bad stored
    // position, or stacking above a high party could push it out) -> always keep it visible + grabbable.
    if (tier_ > 0 && ui_config().editLayout && f.screenW > 0.0f && f.screenH > 0.0f) {
        if (px + w > f.screenW) px = snap(f.screenW - w); if (px < 0.0f) px = 0.0f;
        if (oy + H > f.screenH) oy = snap(f.screenH - H); if (oy < 0.0f) oy = 0.0f;
    }

    const float placedOy = oy;   // box top AS PLACED (config / alliance stack), before the mask / solo / Set-Ref grow-up -> lets a drag convert the visual cluster back to a stored position
    // In an ALLIANCE the party keeps its FULL 6-player footprint (whether 1 or 6 members), so the box reaches
    // straight up to the alliance with no gap to bridge -- the members sit at the bottom, the empty rows above
    // are just the (continuous) party box. Solo / no alliance : shrink to n rows as before.
    const bool partyFull6 = (tier_ == 0) && (ui_config().editLayout || party_demo_level() >= 2
                            || party().alliance_count(1) > 0 || party().alliance_count(2) > 0);
    // PARTY (tier 0) : BOTTOM-anchored (the bottom-right stays exactly where you place/drag it) and the
    // box GROWS UPWARD. First shrink to n rows (members at the bottom, bottom fixed)...
    if (tier_ == 0 && n < 6 && !partyFull6) {
        const float Hn = rowpit * (float)n + 2.0f * pad;
        oy += (H - Hn);
        H = Hn;
    }

    // height of the Cost/Next box that floats ABOVE the party (tier 0 only) -> used for snap + clamp.
    const float costH = (tier_ == 0) ? snap(2.0f * nameSz_ * S + 20.0f * S) : 0.0f;   // == draw_action_box bh2 max : 2 lines + padding + the 10px topPad (keep in sync)

    // LINE 0 (partyBottomY) is just a VISUAL reference for the native party's bottom edge -- it does NOT
    // move our box (you place the party freely at the screen bottom). It only documents, together with the
    // top lines, exactly where the native party spans for each member count.

    // GROW UP to the REFERENCE LINE per count : the box top reaches that line, so the game's native
    // party window is covered whatever the member count or row sizes -- no guessing. Set the lines in edit
    // layout (drag each onto the native party's top edge for that size). Unset -> no grow up.
    if (tier_ == 0 && f.screenH > 0.0f) {
        const int cnt = (n <= 1) ? 1 : (n >= 6 ? 6 : n);        // member count clamped to 1..6
        float lineY = guide_party_top(cnt, f.screenH);          // the "Party Np" ZONE top drives the grow-up
        if (lineY < 0.0f) { const float frac = ui_config().partyRef[cnt - 1]; if (frac >= 0.0f) lineY = frac * f.screenH; }   // fallback : old reference line
        if (lineY >= 0.0f) {
            lineY = snap(lineY);
            float maskBand = oy - lineY;   // grow up to the line (0 if already at/above it)
            if (maskBand < 0.0f) maskBand = 0.0f;
            oy -= maskBand; H += maskBand;
        }
    }

    // The chrome / cost box / edit overlay use the real box rect (boxOy, boxH). For the rows, EVENLY
    // DISTRIBUTE the members across the box : equal top/bottom margins AND equal gaps between members
    // (widen rowpit + shift the row origin so oy+pad+i*rowpit lands them with n+1 identical spaces).
    const float boxOy = oy, boxH = H;
    const float maskOff = placedOy - boxOy;   // how much the box grew UP from its placed top (mask + solo + Set-Ref) -> drag store-back removes it so the position round-trips
    // party top, and the stack top alliances sit on : ABOVE the reserved Cost MP/Next box (costH) -> the
    // always-on cost box sits in that band between the party and the alliance.
    if (tier_ == 0) { g_partyTopY = boxOy; g_partyStackTop = boxOy - costH; }
    if (tier_ == 0 && partyFull6 && n < 6) {
        oy = boxOy + (boxH - (rowpit * (float)n + 2.0f * pad));   // bottom-align the members in the full 6-box (empty rows on top bridge to the alliance)
    } else if (tier_ == 0 && n > 0) {
        const float evenGap = (H - (float)n * rowh) / (float)(n + 1);   // equal spaces (top, between rows, bottom)
        if (n == 1) {                                                   // solo : keep it centred
            if (evenGap > 0.0f) { oy += snap(evenGap) - pad; }
        } else {
            const float minM = snap(3.0f * S);                         // guaranteed top/bottom margin when packed
            float vm  = evenGap < minM ? minM : evenGap;               // reserve >= minM at top AND bottom
            float gap = (H - 2.0f * vm - (float)n * rowh) / (float)(n - 1);   // distribute the remainder BETWEEN rows
            if (gap < 0.0f) { vm = evenGap; gap = evenGap; }           // too tight -> fall back to even distribution
            if (vm > 0.0f) { oy += snap(vm) - pad; rowpit = snap(rowh + gap); }
        }
    }

    // Reserve the BUFF STRIP (drawn to the LEFT of the party box, tier 0 only) in the box's edit/occlusion
    // rect, so the buffs count toward the size even though they sit outside the box : they respect zones,
    // repel other boxes, and are grabbable. Width = the real drawn strip (widest member, capped by Max Buffs).
    float buffW = 0.0f;
    if (tier_ == 0) {
        int maxNb = 0; for (int i = 0; i < n; ++i) if (rows[i].buffs && rows[i].nbuff > maxNb) maxNb = rows[i].nbuff;
        int bmax = ui_config().buffMax; if (bmax < 0) bmax = 0; if (bmax > 32) bmax = 32; if (maxNb > bmax) maxNb = bmax;
        const int PERROW = (ui_config().buffRows > 1) ? 16 : 32;
        const int cnt = maxNb < PERROW ? maxNb : PERROW;
        if (cnt > 0) {
            float csz = ui_config().cursorScale; if (csz < 0.5f) csz = 0.5f; if (csz > 2.0f) csz = 2.0f;
            const float curBand = snap(coreBandH() * S), curW = curBand * 1.30f * csz;
            const float bmar = snap(curW * 0.55f + 6.0f * S), bs = snap(buffIconH() * S), bgap = snap(1.0f * S);
            buffW = bmar + (float)cnt * bs + (float)(cnt - 1) * bgap;
        }
    }
    buffReserveW_ = buffW;   // expose for the config preview (so it centres the box + buff-strip cluster)

    // EDIT MODE : drag this box to reposition it live on the game (stores a fraction-of-screen pos).
    // The drag operates on the REAL cluster rect (= g_boxRect : party box + the cost box on top + the left
    // buff strip, AFTER mask/solo grow-up), so dragging + snapping line up with what's drawn. The stored
    // position is the PLACED top (box top + costH + maskOff) so it round-trips through the grow-up next frame.
    if (ui_config().editLayout && f.mouse && f.screenW > 0.0f) {
        const MouseState* m = f.mouse;
        const float clX = px - buffW, clY = boxOy - costH, clW = w + buffW, clH = boxH + costH;   // cluster incl. the left buff strip
        const bool over = m->x >= clX && m->x < clX + clW && m->y >= clY && m->y < clY + clH;
        if (m->clicked && g_dragTier < 0 && over && edit_drag_grab(&g_dragTier)) {   // FRESH click + shared lock (can't grab while any other box drags)
            g_dragTier = tier_; g_grabDX = m->x - clX; g_grabDY = m->y - clY;
        }
        if (g_dragTier == tier_) {
            if (m->down) {
                float ex = m->x - g_grabDX, ey = m->y - g_grabDY;    // new cluster top-left (px)
                const float ew = clW, eh = clH;
                const bool altFree = (edit_shift() && edit_ctrl()) || edit_alt();   // Ctrl+Shift (or Alt) = FREE placement : no magnetic snap, no zone/box repulsion
                const float SNAP = snap(10.0f);
                if (!altFree) for (int b = 0; b < 3; ++b) {
                    if (b == tier_ || !g_boxRect[b].valid) continue;
                    const BoxRect& r = g_boxRect[b];
                    if      (fabsf(ex - r.x) < SNAP)              ex = r.x;              // left  -> left
                    else if (fabsf(ex - (r.x + r.w)) < SNAP)     ex = r.x + r.w;        // left  -> right
                    else if (fabsf((ex + ew) - (r.x + r.w)) < SNAP) ex = r.x + r.w - ew;// right -> right
                    else if (fabsf((ex + ew) - r.x) < SNAP)      ex = r.x - ew;         // right -> left
                    if      (fabsf(ey - r.y) < SNAP)              ey = r.y;              // top    -> top
                    else if (fabsf(ey - (r.y + r.h)) < SNAP)     ey = r.y + r.h;        // top    -> bottom
                    else if (fabsf((ey + eh) - (r.y + r.h)) < SNAP) ey = r.y + r.h - eh;// bottom -> bottom
                    else if (fabsf((ey + eh) - r.y) < SNAP)      ey = r.y - eh;         // bottom -> top
                }
                // keep the WHOLE cluster on screen
                if (ex > f.screenW - ew) ex = f.screenW - ew; if (ex < 0.0f) ex = 0.0f;
                if (ey > f.screenH - eh) ey = f.screenH - eh; if (ey < 0.0f) ey = 0.0f;
                // P3 : push out of any guide ZONE that forbids this box (party -> ZPERM_PARTY, alliance -> ZPERM_ALLIANCE), then re-clamp on screen. Alt = free placement -> skip both repulsions (screen clamp above still applies).
                if (!altFree) {
                    guide_push_out(tier_ == 0 ? ZPERM_PARTY : ZPERM_ALLIANCE, f.screenW, f.screenH, ex, ey, ew, eh);
                    if (ex > f.screenW - ew) ex = f.screenW - ew; if (ex < 0.0f) ex = 0.0f;
                    if (ey > f.screenH - eh) ey = f.screenH - eh; if (ey < 0.0f) ey = 0.0f;
                    edit_box_push_out(EDITBOX_PARTY + tier_, f.t, ex, ey, ew, eh);   // GLOBAL no-overlap : repelled by the standalone boxes + the other clusters (magnetic snap above already keeps them adjacent, so this only fires on a real overlap)
                    if (ex > f.screenW - ew) ex = f.screenW - ew; if (ex < 0.0f) ex = 0.0f;
                    if (ey > f.screenH - eh) ey = f.screenH - eh; if (ey < 0.0f) ey = 0.0f;
                }
                // cluster top-left -> stored PLACED top : the box px = ex + buffW (undo the left strip) ; undo costH + maskOff
                float nx = (ex + buffW) / f.screenW, ny = (ey + costH + maskOff) / f.screenH;
                nx = nx < 0.0f ? 0.0f : (nx > 1.0f ? 1.0f : nx);   // never store an off-screen position that
                ny = ny < 0.0f ? 0.0f : (ny > 1.0f ? 1.0f : ny);   // would make the box un-grabbable next time
                ui_config().box[tier_].posSet = true; ui_config().box[tier_].x = nx; ui_config().box[tier_].y = ny;
                px = snap(ex + buffW);                                 // immediate horizontal feedback (box px = cluster left + the buff strip)
            } else { g_dragTier = -1; edit_drag_release(&g_dragTier); }  // released -> free the shared lock
        }
        if (over && !edit_drag_busy())                                 // hovering a grabbable box (nothing dragging) -> same neon cue as the standalone boxes
            edit_box_hover_glow(f.dev, f, clX, clY, clW, clH);
        // wheel over this box -> resize it (0.5x .. 2.0x)
        if (over && ui_config().wheel != 0) {
            float s = ui_config().box[tcfg()].scale + (float)ui_config().wheel * 0.05f;   // resize the config GROUP (both alliances share their scale)
            ui_config().box[tcfg()].scale = s < 0.5f ? 0.5f : (s > 2.0f ? 2.0f : s);
            ui_config().wheel = 0;
        }
    }
    // store the CLUSTER rect (party box + the cost box above it for tier 0) so other boxes snap to
    // its real top, and the party itself snaps using the cost-box top.
    g_boxRect[tier_].x = px - buffW; g_boxRect[tier_].y = boxOy - costH; g_boxRect[tier_].w = w + buffW; g_boxRect[tier_].h = boxH + costH; g_boxRect[tier_].valid = true;   // incl. the left buff strip (right edge px+w unchanged -> alliance right-align still holds)
    if (ui_config().editLayout)   // publish the cluster (incl. the left buff strip) so the standalone boxes repel it too
        edit_box_publish(EDITBOX_PARTY + tier_, px - buffW, boxOy - costH, w + buffW, boxH + costH, f.t);
    const float cx  = px + pad + inset;
    const float gx0 = px + w - pad - inset - (3 * gw + 2 * ggap);
    // row-invariant column positions (only the row's Y varies) :
    const float ibx = snap(cx + mw + gap5);   // badge left  (snapped)
    const float bx  = cx + mw + gap5;          // badge anchor for text centring
    const float nx  = snap(bx + bw + gap5);    // name left
    const float badgeYoff = (mh - bh) * 0.5f;   // badge centred on the MAIN BAND (not the full row -> stays level with the name)

    // ---------- animation update (lerp displayed values toward live targets) ----------
    const float t = f.t;
    const float dt = (lastT_ < 0 || t < lastT_ || t - lastT_ > 0.5f) ? 0.016f : (t - lastT_);
    lastT_ = t;
    const float kBar = 1.0f - expf(-9.0f  * dt);   // bar fill / colour easing
    const float kDot = 1.0f - expf(-14.0f * dt);   // dot pop in/out
    const float kSel = 1.0f - expf(-15.0f * dt);   // selection slide / fade
    const float kZoom = 1.0f - expf(-12.0f * dt);  // selected-row zoom grow/shrink

    RowAnim* ra[6] = {0};
    for (int i = 0; i < 8; ++i) anim_[i].seen = false;
    int selRow = -1;
    int subRow = -1;
    for (int i = 0; i < n; ++i) {
        RowAnim* a = anim_for(rows[i].id); ra[i] = a; a->seen = true;
        float thp = (float)(rows[i].hpp < 0 ? 0 : rows[i].hpp > 100 ? 100 : rows[i].hpp);
        float tmp = (float)(rows[i].mpp < 0 ? 0 : rows[i].mpp > 100 ? 100 : rows[i].mpp);
        int   tpv = rows[i].tp < 0 ? 0 : (rows[i].tp > 3000 ? 3000 : rows[i].tp);
        float ttp = tpv / 30.0f;
        if (a->hpp < 0) { a->hpp = thp; a->mpp = tmp; a->tpp = ttp; }   // first sight -> snap, no animation
        else { a->hpp += (thp - a->hpp) * kBar; a->mpp += (tmp - a->mpp) * kBar; a->tpp += (ttp - a->tpp) * kBar; }
        const bool present[3] = { rows[i].alead, rows[i].plead, rows[i].qm };
        for (int k = 0; k < 3; ++k) a->dot[k] += ((present[k] ? 1.0f : 0.0f) - a->dot[k]) * kDot;
        if (rows[i].sel) selRow = i;
        if (rows[i].subsel) subRow = i;
    }
    // the party-window PICKER (Menu -> Party -> Distribution : Quartermaster / change leader / lottery /
    // remove) shows as the BLUE (sub) frame -- a secondary selection, distinct from the GOLD target/lock.
    // Force the blue cursor onto the menu-hovered member (priority over any <st>) while that menu is open,
    // so it follows the menu even when you are locked on someone (the lock keeps its own gold frame).
    if (tier_ == 0 && f.game && f.game->partyMenuSel >= 1 && f.game->partyMenuSel <= n)
        subRow = f.game->partyMenuSel - 1;
    // selection cursor : slide toward the targeted row (snap on first acquire, else ease) + fade
    if (selRow >= 0) {
        const unsigned sid = rows[selRow].id;
        const float ty = oy + pad + selRow * rowpit;
        if (sid != selId_) {                           // target moved to a different member
            selId_ = sid;
            selBobT0_ = t;                             // restart the bob from its centre on every target change
            selZoom_  = 0.0f;                          // replay the zoom-in on the newly targeted row
        }
        if (selA_ < 0.05f) selY_ = ty;                 // was hidden -> appear directly on the row
        selY_ += (ty - selY_) * kSel;
    }
    selA_    += (((selRow >= 0) ? 1.0f : 0.0f) - selA_) * kSel;
    selZoom_ += (((selRow >= 0) ? 1.0f : 0.0f) - selZoom_) * kZoom;   // grow to full on target, then HOLD while it stays targeted

    // sub-target bar : same slide/fade as the selection (ocean blue, drawn later, on top)
    if (subRow >= 0) {
        const unsigned sid = rows[subRow].id;
        const float ty = oy + pad + subRow * rowpit;
        if (sid != subId_) { subId_ = sid; subZoom_ = 0.0f; selBobT0_ = t; }   // sub change also restarts the shared rhythm
        if (subA_ < 0.05f) subY_ = ty;                 // was hidden -> appear directly on the row
        subY_ += (ty - subY_) * kSel;
    }
    subA_    += (((subRow >= 0) ? 1.0f : 0.0f) - subA_) * kSel;
    subZoom_ += (((subRow >= 0) ? 1.0f : 0.0f) - subZoom_) * kZoom;   // name zoom-in, HOLDS while sub-targeted

    // SHARED rhythm : cursor bob, frame pulse and name pulse all beat on THIS one clock.
    // Starts after the same short delay as the cursor bob, restarts on every target/sub change.
    const float gPulseT = t - selBobT0_ - 0.35f;
    const float gPulse  = (gPulseT > 0.0f) ? sinf(gPulseT * 4.6f) : 0.0f;   // -1..1  (cursor bob)
    const float gSwT = (gPulseT > 0.0f) ? gPulseT : 0.0f;                   // shine sweep phase 0..1 (1.4s cycle)
    const float gSweep = gSwT / 1.4f - (float)(int)(gSwT / 1.4f);

    // ---------- graphics : box chrome -- background ALWAYS ; the BORDER (frame) is per-box on/off ----------
    setup_color_state(dev);
    const bool drawBorder = ui_config().border[tcfg()];   // tcfg() : both alliance boxes share the Alliance config (was border[tier_] -> Alliance 2 ignored the toggle)
    // Alliance boxes (tier > 0) may carry their OWN theme (else follow the Party skinTheme). Procedural themes honour
    // the per-box index/lum/hue fully ; the FFXI native skin texture is shared (loaded once) -- same as Target/Player.
    const bool allyOwn = (tier_ > 0) && !ui_config().allyThemeCopy;
    const int      boxTheme = allyOwn ? ui_config().allyTheme : ui_config().skinTheme;
    const float    boxLum   = allyOwn ? ui_config().allyLum   : ui_config().skinLum;
    const unsigned boxHue   = allyOwn ? ui_config().allyHue   : ui_config().skinHue;
    const float bAlpha = allyOwn ? ui_config().allyBoxAlpha : ui_config().skinBoxAlpha;
    const float skinBa = bAlpha < 0.0f ? 0.0f : (bAlpha > 1.0f ? 1.0f : bAlpha);   // box chrome opacity (content stays opaque)
    const u32   skinTint = mul_a(0xFFFFFFFFu, skinBa);
    if (window_theme_is_proc(boxTheme)) {                       // procedural colour theme (no game texture)
        draw_proc_window(dev, boxTheme, px, boxOy, w, boxH, skinTint, false, drawBorder, boxLum, boxHue);
    } else if (f.skin && f.skin->ready()) {                     // FFXI native window skin (9-slice) : bg always, frame optional
        draw_window(dev, *f.skin, px, boxOy, w, boxH, skinTint, S, false, drawBorder);
    } else {                                                    // fallback : the built-in navy chrome (AA rounded)
        const float R = snap(6.0f);
        if (drawBorder)
            rrect(dev, px - 1, boxOy - 1, w + 2, boxH + 2, R + 1.0f, mul_a(0x6699BBFF, skinBa), mul_a(0x6699BBFF, skinBa));   // outer glow edge (the border)
        rrect(dev, px, boxOy, w, boxH, R, mul_a(0xFF232E54, skinBa), mul_a(0xFF080B1A, skinBa));          // main fill (background)
        vgrad(dev, px + R, boxOy, w - 2 * R, 3 * S, mul_a(0x4DBFD8FF, skinBa), mul_a(0x00BFD8FF, skinBa));            // top sheen (inset to stay in the rounded corners)
        vgrad(dev, px + R, boxOy + boxH - 4 * S, w - 2 * R, 4 * S, mul_a(0x00000000, skinBa), mul_a(0x40000000, skinBa));   // bottom vignette
    }

    // (the wide divider band between the two alliance boxes was removed -- they now stack flush.)

    // ---------- per-row : zebra background + badge + animated gauges ----------
    for (int i = 0; i < n; ++i) {
        const Row& r = rows[i];
        const float ry = oy + pad + i * rowpit;
        setup_color_state(dev);   // reset : the PREVIOUS row's gauge (esp. the textured Fiole) leaves the device
                                  // in a textured/additive state -> without this the badge renders additively (bright glow)
        // FULL row background, alternating shades (Excel cells). DISABLED for now (kept for later) --
        // toggle kRowCells to re-enable. It was hiding the FFXI window-skin background.
        static const bool kRowCells = false;
        if (kRowCells) { const u32 cellc = (i & 1) ? 0x99101A2Eu : 0x99223256u; vgrad(dev, px + 1, ry, w - 2, rowpit, cellc, cellc); }
        if (r.offzone) continue;                       // out of zone : no badge, no vitals/gauges
        // badge : NOT affected by the selection zoom (only the name zooms) ; dark inner then 4 border edges.
        if (ui_config().jobBadge[tcfg()] != 0) {       // 0 = job badge OFF (no box, column collapsed)
        const float iby = snap(ry + badgeYoff);
        const float bcx = ibx + bw * 0.5f, bcy = iby + bh * 0.5f, pbw = bw, pbh = bh;
        const float pbx = snap(bcx - pbw * 0.5f), pby = snap(bcy - pbh * 0.5f);
        const u32 rb = (r.role & 0x00FFFFFF) | 0xD0000000;
        rrect_bordered(dev, pbx, pby, pbw, pbh, snap(3.0f), 0xF0161D33, 0xF00A0E1C, rb, 1.0f);   // framed box : dark bg + role-colour border (same for TEXT and ICON)
        if (ui_config().jobBadge[tcfg()] == 3) {          // ICONS : the job emblem INSIDE that box, tinted by role
            const int cell = job_id_from_abbr(r.job) - 1;   // WAR=0 .. RUN=21 ; -1 (unknown/SPC) = skip
            const float bt2  = snap(1.0f);                  // just clear the 1px border ring (the emblem art already has its own transparent margin -> no extra pad)
            const float isz  = (pbw < pbh ? pbw : pbh) - 2.0f * bt2;
            const float ix   = snap(pbx + (pbw - isz) * 0.5f), iy = snap(pby + (pbh - isz) * 0.5f);   // CENTRED in the box (box may be non-square)
            draw_job_icon(dev, jobicon_tex_, ix, iy, isz, cell, (r.role & 0x00FFFFFF) | 0xFF000000);   // single role colour
        }
        }

        const float gy = ry + (mh - gh) * 0.5f;   // gauges centred on the MAIN BAND
        const RowAnim* a = ra[i];
        const float hpp = a ? a->hpp : (float)r.hpp, mpp = a ? a->mpp : (float)r.mpp, tpp = a ? a->tpp : 0.0f;
        const float wsReady = (r.tp >= 1000) ? 1.0f : 0.0f;   // TP >= 1000 -> pulse (WS ready)
        const float hpDanger = (r.hpp > 0 && r.hpp <= 25) ? 1.0f : 0.0f;   // HP <= 25% (alive) -> red danger blink
        const u32   gcol[3] = { hp_color(hpp), C_MP, tp_color(r.tp) };
        const float gpct[3] = { hpp, mpp, tpp };
        const float gpls[3] = { 0.0f, 0.0f, ui_config().animTP ? wsReady : 0.0f };   // TP WS-ready pulse (config option)
        const float gdng[3] = { ui_config().animHP ? hpDanger : 0.0f, 0.0f, 0.0f };   // HP critical blink (config option)
        const int   gstyleBox = ui_config().gaugeStyle[tcfg()];   // this box's gauge style
        for (int gi = 0; gi < 3; ++gi) {                 // bars are NOT affected by the selection zoom (geometry or brightness)
            const float gx = gx0 + gi * (gw + ggap);
            party_gauge(dev, gx, gy, gw, gh, gpct[gi], gcol[gi], t, gpls[gi], gdng[gi], gi, gstyleBox);   // gi = kind (HP/MP/TP) ; per-box style
        }
    }

    // ---------- buffs : status icons LEFT of each party row (main box only -- alliance buffs aren't sent) ----------
    if (tier_ == 0) {   // buffs LEFT of the row + (config preview only) a "+N" marker for the strip clipped at the stage edge
        Font* bfn = f.fonts ? f.fonts->get(ui_font_face(ui_config().fontFace), 700) : f.font;
        draw_member_buffs(dev, buff_tex_, rows, n, px, oy, pad, rowpit, mh, S, snap(coreBandH() * S), snap(buffIconH() * S), bfn);   // curBand = cursor ref (coreBandH) so the strip hugs the cursor ; icon size driven by Buff Size % + 1/2-row mode ; centred in the row
    }

    // ---------- leader / QM markers : round dots, animated pop-in/out (scale + fade) ----------
    if (dot_tex_) {
        const u32 dcol[3] = { 0xFFFFFFFF, 0xFFFFEF3F, 0xFF42D98A };   // alliance=white, party=canary yellow, QM=green
        const float sz = 5.5f * S, gap = -0.5f * S, pitch = sz + gap;   // smaller pips (distance text below gets the room)
        setup_tex_state(dev, dot_tex_);
        // FIXED 3-column layout so each marker keeps its own slot: alliance=LEFT, party=MIDDLE,
        // QM=RIGHT (dcol order matches). An absent marker just leaves its column empty.
        const float markCx = cx + mw * 0.5f;
        const float slotsW = 2.0f * pitch + sz;               // width of the 3 columns
        const float x0     = markCx - slotsW * 0.5f;          // left (alliance) column x
        for (int i = 0; i < n; ++i) {
            const RowAnim* a = ra[i]; if (!a) continue;
            const float ry = oy + pad + i * rowpit;
            const float markBlockH = snap(marksColH() * S);                     // the pips+distance UNIT height
            const float markTop = ry + (mh - markBlockH) * 0.5f;               // centre that unit in the band (so it stays balanced when buffs grow the row)
            const float cy = markTop + snap(2.0f * S) + sz * 0.5f;             // pips at the TOP of the unit
            for (int k = 0; k < 3; ++k) {                     // k: 0=alliance, 1=party, 2=QM
                const float amt = a->dot[k]; if (amt <= 0.02f) continue;
                const float s2 = sz * amt;                    // pop scale 0..1
                const u32 c = (dcol[k] & 0x00FFFFFF) | ((u32)(amt * ((dcol[k] >> 24) & 0xFF)) << 24);   // fade
                const float dx = x0 + (float)k * pitch;
                glow_quad(dev, dx + (sz - s2) * 0.5f, cy - s2 * 0.5f, s2, s2, c);
            }
        }
        dSetTex(dev, 0, 0);
    }

    // ---------- selection cursor ICON (hand pointing right) : slides with the selection ----------
    if (tier_ == 0) g_partyBandPx = snap(coreBandH() * S);   // cache the party name-line height so alliance cursors match party's size (drawn first each frame)
    if (icon_tex_ && (selA_ > 0.02f || subA_ > 0.02f)) {
        setup_tex_state(dev, icon_tex_);
        float csz = ui_config().cursorScale; if (csz < 0.5f) csz = 0.5f; if (csz > 2.0f) csz = 2.0f;   // Cursor Size %
        const float tierBoost = 1.0f;   // SAME cursor size on party AND alliance (both size off coreBandH -> no tier boost)
        const float cb = (tier_ != 0 && g_partyBandPx > 0.0f) ? g_partyBandPx : snap(coreBandH() * S);   // alliance reuses the PARTY band -> the hand is the SAME size even though alliance rows are shorter
        const float ih = cb * 1.30f * csz * tierBoost, iw = ih; // square icon, ~1.3 name-line tall * size (points at the name line)
        const float es  = 1.0f;                                 // cursor keeps a constant size on target change (no pop)
        const float bob = 1.5f * S * gPulse;                     // cursor bob on the SHARED rhythm (already delayed + phase-reset)
        const float cx  = px - iw * 0.5f + iw * 0.14f + bob;   // +0.14 iw : the hand art has a transparent right margin ; nudge the FINGER right up to the box edge
        // the cursor points at the SUB-target when one is in use, otherwise the main target
        const bool  onSub  = (subA_ > 0.02f);
        const bool  locked = (f.game && f.game->targetLocked && !onSub);   // <t> LOCKED -> red hand (+ red frame)
        const float cy  = (onSub ? subY_ : selY_) + mh * 0.5f;            // icon CENTRE y -> on the main band (name line)
        const u32 hrgb  = onSub ? 0x002E9CFF : (locked ? 0x00FF4030 : 0x00FFFFFF);   // blue = sub, RED = locked main, white = normal
        const u32 tint  = ((u32)(0xFF * (onSub ? subA_ : selA_)) << 24) | hrgb;
        glow_quad(dev, snap(cx - iw * es * 0.5f), snap(cy - ih * es * 0.5f), iw * es, ih * es, tint);
        dSetTex(dev, 0, 0);
    }

    // ---------- text : each element (name / bars / badge) has its OWN face+weight atlas ----
    if (!f.fonts) return;
    const char* ov = ui_font_face(ui_config().fontFace);   // "" = keep the per-element layout faces
    Font* fName  = te_font(f.fonts, TE_NAME,  ov, nameFont_.c_str(),  nameBold_);
    Font* fCast  = te_font(f.fonts, TE_CAST,  ov, nameFont_.c_str(),  nameBold_);   // cast : same base as the name
    Font* fHP    = te_font(f.fonts, TE_HP,    ov, barFont_.c_str(),   barBold_);
    Font* fMP    = te_font(f.fonts, TE_MP,    ov, barFont_.c_str(),   barBold_);
    Font* fTP    = te_font(f.fonts, TE_TP,    ov, barFont_.c_str(),   barBold_);
    Font* fBadge = te_font(f.fonts, TE_BADGE, ov, badgeFont_.c_str(), badgeBold_);
    Font* fDist  = te_font(f.fonts, TE_DIST,  ov, badgeFont_.c_str(), badgeBold_);
    fName->ensure(dev); fCast->ensure(dev); fHP->ensure(dev); fMP->ensure(dev); fTP->ensure(dev); fBadge->ensure(dev); fDist->ensure(dev);
    Font* fBarE[3] = { fHP, fMP, fTP };
    const u32 nSTK = nameStroke_  > 0 ? 0xFF000000u : 0u; const float nOWf = te_ow(TE_NAME,  nameStroke_  * S);
    const u32 vSTK = barStroke_   > 0 ? 0xFF000000u : 0u;
    const u32 bSTK = badgeStroke_ > 0 ? 0xFF000000u : 0u; const float bOWf = te_ow(TE_BADGE, badgeStroke_ * S);
    const float cOWf = te_ow(TE_CAST, nameStroke_ * S), dOWf = te_ow(TE_DIST, badgeStroke_ * S);
    // name/cast left, shifted RIGHT by the outline width (+1px AA) so the black STROKE -- drawn in 8
    // passes at x +/- nOWf -- doesn't poke out to the LEFT of nx (it was, and the config preview clipped it).
    const float nxt = snap(nx + nOWf + 1.0f);
    char buf[24];
    for (int i = 0; i < n; ++i) {
        const Row& r = rows[i];
        bool offz = r.offzone;
        bool dead = !offz && r.hpp <= 0;
        bool hasCast = castOn() && !dead && !offz && r.cast;   // casts shown per box type (castOn())
        const float ry = oy + pad + i * rowpit;
        const float by = snap(ry + badgeYoff);         // match the snapped badge rect
        // names never scale (no zoom on target) -> stable layout, room for the cast line below
        const float es = 1.0f;
        const float bcx = bx + bw * 0.5f, bcy = by + bh * 0.5f;               // badge centre (scale pivot)

        // distance (yalms) UNDER the leader/QM pips, centered in the marks column, format 00.00.
        if (distOn() && r.dist >= 0.0f && fDist->ready()) {
            float d = r.dist; if (d > 99.99f) d = 99.99f;
            char db[12]; sprintf(db, "%05.2f", d);
            float dsz = te_sz(TE_DIST, badgeSz_ * S * 1.20f);                 // as big as fits the marks column width
            const float dw = fDist->measure(db, dsz);
            if (dw > mw * 0.98f && dw > 0.0f) dsz *= (mw * 0.98f) / dw;
            const u32 dcol = r.dist >= kCastRange ? ui_config().distColFar    :   // red  : out of cast range
                             r.dist >= kCastSafe  ? ui_config().distColNormal :   // yellow : marginal (still casts)
                                                    ui_config().distColClose;     // blue : comfortably in range
            const float markBlockH = snap(marksColH() * S);                   // match the pips : distance sits at the BOTTOM of the centred marks unit
            const float markTop = ry + (mh - markBlockH) * 0.5f;
            fDist->begin(dev);
            fDist->draw_cc(dev, cx + mw * 0.5f, markTop + markBlockH - dsz * 0.62f, db, dsz, te_col(TE_DIST, dcol), bSTK, dOWf);
        }

        const int badgeMode = ui_config().jobBadge[tcfg()];    // 0 = off, 1 = main only, 2 = main + sub, 3 = icon (drawn earlier)
        if (!offz && badgeMode != 0 && badgeMode != 3 && fBadge->ready()) {   // out of zone / icon mode : no job badge TEXT
            fBadge->begin(dev);
            bool hasSub = badgeMode == 2 && r.sub && r.sub[0];   // mode 1 -> ignore the sub job
            const float mfrac = hasSub ? 0.34f : 0.52f;
            char jb[10], sb2[10];
            const float bInnerW = bw - snap(4.0f * S);   // FIXED box interior : text is fitted to this, so the level ("WHM 99") never grows/overflows the box
            // append the main-job LEVEL to the job abbr (e.g. "WHM 99"). Sub keeps its floor(main/2) level too (FFXI
            // subjob cap) -> "WHM 99 / BLM 49". mlvl == 0 (unknown) -> just the abbr, no number.
            char mtxt[16]; const char* mj = r.job ? r.job : "";
            if (r.mlvl > 0) { sprintf(mtxt, "%s %d", mj, r.mlvl); mj = mtxt; }
            const char* mtext = te_up(TE_BADGE, mj, jb, 10);
            float msz = te_sz(TE_BADGE, badgeSz_ * S);
            { const float mmw = fBadge->measure(mtext, msz); if (mmw > bInnerW && mmw > 0.0f) msz *= bInnerW / mmw; }   // shrink-to-fit the fixed box
            fBadge->draw_cc(dev, bcx, bcy + (mfrac - 0.5f) * bh, mtext, msz, te_col(TE_BADGE, r.role), bSTK, bOWf);
            if (hasSub) {   // sub : abbr + REAL sub level when known (self, memory @pl+0xA0 -> already the displayed
                            // capped value, e.g. "DNC 54" with Master levels). slvl == 0 (party members : not in memory) -> abbr only.
                char stxt[16]; const char* sj = r.sub ? r.sub : "";
                if (r.slvl > 0) { sprintf(stxt, "%s %d", sj, r.slvl); sj = stxt; }
                const char* stext = te_up(TE_BADGE, sj, sb2, 10);
                float ssz = te_sz(TE_BADGE, subSz() * S);
                { const float smw = fBadge->measure(stext, ssz); if (smw > bInnerW && smw > 0.0f) ssz *= bInnerW / smw; }
                fBadge->draw_cc(dev, bcx, bcy + 0.20f * bh, stext, ssz, te_col(TE_BADGE, C_DIM), bSTK, bOWf);
            }
        }
        if (!fName->ready()) continue;
        fName->begin(dev);
        // name : truncate to the width available before the HP gauge (leaves a real gap),
        // measured in the ACTUAL name font -> never touches the bar, no fixed char cap.
        char nm[28]; int nl = 0; for (; nl < 20 && r.name && r.name[nl]; ++nl) nm[nl] = r.name[nl];
        nm[nl] = 0;
        if (ui_config().text[tcfg()][TE_NAME].upper) for (int k = 0; k < nl; ++k) { char c = nm[k]; if (c >= 'a' && c <= 'z') nm[k] = (char)(c - 32); }
        const float nmax = gx0 - nx - 6.0f * S;                  // 6px gap before the gauges
        const float nsz = te_sz(TE_NAME, nameSz_ * S * es);      // measure at the ZOOMED size so a zoomed long name never overflows onto HP
        char nmFit[28];                                          // shared width-driven truncation ("...")
        const char* nmDraw = (nmax > 0) ? fit_ellipsis(fName, nm, nsz, nmax, nmFit, (int)sizeof(nmFit)) : nm;
        // name sits at a FIXED height (always a bit high) -> it never jumps when a cast starts,
        // and there is always room below for the cast / zone line.
        fName->draw_lc(dev, nxt, ry + mh * 0.5f, nmDraw, nsz, te_col(TE_NAME, offz ? C_OFF : (dead ? C_BAD : C_INK)), nSTK, nOWf);   // name centred on the MAIN BAND
        if (hasCast) {
            const float ca = r.castAlpha < 0.0f ? 0.0f : (r.castAlpha > 1.0f ? 1.0f : r.castAlpha);   // pop-in / depop fade
            const float cp = 0.5f + 0.5f * sinf(t * 5.0f);                       // OPACITY pulse during the cast
            const float af = ca * (0.55f + 0.45f * cp);                          // fade * pulse -> breathing opacity
            const u32 crgb = 0x00FFD970;                                         // light gold (constant colour)
            const u32 ccol = ((u32)(0xFF * af) << 24) | crgb;
            const u32 cstk = nSTK ? ((u32)(0xFF * af) << 24) : 0u;              // stroke breathes with the text
            char cbuf[16]; int cl = 0;                                          // cast name : 6 chars max, then "..."
            for (; cl < 6 && r.cast && r.cast[cl]; ++cl) cbuf[cl] = r.cast[cl];
            if (r.cast && r.cast[cl]) { while (cl > 0 && cbuf[cl - 1] == ' ') --cl; cbuf[cl] = '.'; cbuf[cl + 1] = '.'; cbuf[cl + 2] = '.'; cbuf[cl + 3] = 0; }
            else cbuf[cl] = 0;
            if (ui_config().text[tcfg()][TE_CAST].upper) for (int k = 0; cbuf[k]; ++k) { char c = cbuf[k]; if (c >= 'a' && c <= 'z') cbuf[k] = (char)(c - 32); }
            const u32 ccolF = ui_config().text[tcfg()][TE_CAST].colorOn ? (((u32)(0xFF * af) << 24) | (ui_config().text[tcfg()][TE_CAST].color & 0x00FFFFFF)) : ccol;
            fCast->begin(dev);
            fCast->draw_lc(dev, nxt, ry + mh * 0.5f + snap((nameSz_ * 0.5f + castSz() * 0.5f + 3.0f) * S), cbuf, te_sz(TE_CAST, castSz() * S), ccolF, cstk, cOWf);   // cast just UNDER the name
        }
        if (offz) {                                    // no vitals -> show the zone the member is in
            const char* zn = zone_name(r.zone);
            char zbuf[40]; if (zn && zn[0]) { sprintf(zbuf, "%.36s", zn); } else { strcpy(zbuf, "out of zone"); }
            fName->draw_lc(dev, nxt, ry + mh * 0.5f + snap((nameSz_ * 0.5f + castSz() * 0.5f + 3.0f) * S), zbuf, castSz() * S, C_OFF, nSTK, nOWf);  // zone just under the (greyed) name
            continue;
        }

        int tp = r.tp < 0 ? 0 : (r.tp > 3000 ? 3000 : r.tp);
        int vals[3] = { dead ? 0 : r.hpVal, r.mpVal, tp };
        const float gy = ry + (mh - gh) * 0.5f;   // bar values centred on the MAIN BAND (match the gauges)
        if (!fHP->ready()) continue;
        const int gstyle = ui_config().gaugeStyle[tcfg()];
        for (int g = 0; g < 3; ++g) {
            float gx = gx0 + g * (gw + ggap);
            sprintf(buf, "%d", vals[g]);
            u32 tcol = 0xFFFFFFFF;
            if (gstyle == 7) {   // TEXT style : the number itself is the gauge -> colour by state + animate
                u32 base = (g == 0) ? hp_color((float)r.hpp) : (g == 1) ? C_MP : tp_color(r.tp);
                float br = 1.0f;
                if (g == 2 && r.tp >= 1000 && ui_config().animTP)                 br = 0.78f + 0.32f * (0.5f + 0.5f * sinf(t * 7.5f));   // TP ready : pulse (option)
                else if (g == 0 && r.hpp > 0 && r.hpp <= 25 && ui_config().animHP) br = 0.55f + 0.45f * (0.5f + 0.5f * sinf(t * 7.5f));   // HP critical : blink (option)
                tcol = scl(base, br);
            }
            Font* fv = fBarE[g]; fv->begin(dev);   // HP / MP / TP each has its own element style
            fv->draw_cc(dev, gx + gw * 0.5f, gy + gh * 0.5f, buf, te_sz(TE_HP + g, barSz_ * S), te_col(TE_HP + g, tcol), vSTK, te_ow(TE_HP + g, barStroke_ * S));
        }
    }

    // Row overlays (selection / sub-target / out-of-range veil) stay INSIDE the skin border : inset
    // by the box padding so they sit on the background, never overlapping the window frame.
    // selection / veil span the BACKGROUND width : inset a CONSTANT (native frame thickness, not scaled
    // by S like pad) so they reach the inner border edge at any box size, without spilling onto it.
    const float selIn = snap(3.0f);
    const float sx = px + selIn, sw = w - 2.0f * selIn;
    // BALANCED selection frame : wrap the member content (main-band top .. cast-line bottom) with an
    // EQUAL margin above and below, instead of spanning the whole reserved row (which sat flush at the
    // top and overshot far below the cast). selBot/selH are measured from the row top (ry).
    const float selPad = snap(2.0f * S);
    const float selBot = castOn() ? (mh * 0.5f + snap((nameSz_ * 0.5f + castSz() + 3.0f) * S)) : mh;   // content bottom : cast line, or just the main band when casts are off
    const float selY0  = -selPad;                                                     // frame top, relative to ry
    const float selH   = selBot + 2.0f * selPad;                                      // frame height (content + equal margins)

    // ---------- sub-target bar : ocean blue (the game's <st> colour), on top, below the main selection ----------
    if (subA_ > 0.02f) {
        setup_color_state(dev);
        const float ry = snap(subY_);
        const float a  = subA_;                                   // steady opacity
        // strong ocean-blue fill
        const u32 fillT = ((u32)(0x80 * a) << 24) | 0x00159CFF;   // vivid ocean blue
        const u32 fillB = ((u32)(0x34 * a) << 24) | 0x00064FB0;   // deeper ocean toward the bottom
        vgrad(dev, sx, ry + selY0, sw, selH, fillT, fillB);
        // MODERN shine : the sub IS the cursor row whenever it shows, so it always sweeps (a bit stronger)
        shine_sweep(dev, sx, ry + selY0, sw, selH, gSweep, 0x00BFEFFF, 0.80f * a);
    }

    // ---------- selection bar : drawn LAST, on top of everything. Faux "loupe / glass" look ----------
    // convex glass = bright sheen strongest at the HORIZONTAL centre + darkened curved rims at the
    // sides. Kept faint so the player's bar stays readable ; the name is already magnified (es).
    if (selA_ > 0.02f) {
        setup_color_state(dev);                            // back to colour-only quads after the font passes
        const float ry = snap(selY_);
        const float a  = selA_;                                // steady opacity

        // 1) base fill : faint GOLD normally, RED when the main target is LOCKED (<t> lock-on)
        const bool locked = (f.game && f.game->targetLocked && subA_ <= 0.02f);
        const u32 fillT = locked ? (((u32)(0x52 * a) << 24) | 0x00FF6A5A) : (((u32)(0x3C * a) << 24) | 0x00FFE08A);
        const u32 fillB = locked ? (((u32)(0x24 * a) << 24) | 0x00D83028) : (((u32)(0x16 * a) << 24) | 0x00FFC850);
        vgrad(dev, sx, ry + selY0, sw, selH, fillT, fillB);

        // 2) MODERN shine : only when the CURSOR is on the main row (i.e. no sub-target active)
        if (subA_ <= 0.02f) shine_sweep(dev, sx, ry + selY0, sw, selH, gSweep, locked ? 0x00FFD5C8 : 0x00FFFFFF, 0.65f * a);

        // 3) curved rims : darken the far left/right edges -> the lens bulge (refraction pinch)
        const float rw = sw * 0.085f;                      // narrow rims so the bars stay clear
        const u32 rO = ((u32)(0x3E * a) << 24);            // black at the very edge
        const u32 rI = 0x00000000;                         // transparent inward
        grad_quad(dev, sx,           ry + selY0, rw, selH, rO, rI, rO, rI);   // left rim
        grad_quad(dev, sx + sw - rw, ry + selY0, rw, selH, rI, rO, rI, rO);   // right rim
    }

    // ---------- dim members beyond cast range : a translucent veil over their row, so it reads
    //            clearly as "out of casting range" (heals/buffs won't reach). ----------
    {
        setup_color_state(dev);
        const u32 veil = 0x8E101620;   // ~55% dark blue-grey
        const float veilBot = boxOy + boxH - snap(2.0f * S);   // never let the veil reach the box bottom border
        for (int i = 0; i < n; ++i) if (rows[i].outRange) {
            const float ry = oy + pad + (float)i * rowpit;
            const float vy = ry + selY0;                                   // SAME balanced box as the selection frame (content, not the whole reserved row)
            float vh = selH; if (vy + vh > veilBot) vh = veilBot - vy;     // clamp the LAST row's veil to the inner margin
            if (vh > 1.0f) grad_quad(dev, sx, vy, sw, vh, veil, veil, veil, veil);
        }
    }

    // ---------- floating spell / JA / WS info box (main box only -- alliances would stack it) ----------
    if (tier_ == 0) draw_action_box(f, S, px, w, boxOy, fName, nSTK, nOWf);

    // ---------- EDIT MODE : draggable outline around this box (brighter when hovered / grabbed) ----------
    if (ui_config().editLayout) {
        setup_color_state(dev);
        const float oy2 = boxOy - costH, H2 = boxH + costH;      // cluster rect (party + cost box above) -- the REAL box rect (boxOy), matches g_boxRect + the drag
        const bool drag  = (g_dragTier == tier_);
        const bool hover = f.mouse && f.mouse->x >= px && f.mouse->x < px + w && f.mouse->y >= oy2 && f.mouse->y < oy2 + H2;
        const u32  c = drag ? 0xFFFFD24A : (hover ? 0xFF7EC0FF : 0xAAFFFFFF);   // gold grabbed / blue hover / white idle
        const float t = snap(2.0f);
        grad_quad(dev, px,        oy2,         w, t, c, c, c, c);   // top
        grad_quad(dev, px,        oy2 + H2 - t, w, t, c, c, c, c);  // bottom
        grad_quad(dev, px,        oy2,         t, H2, c, c, c, c);  // left
        grad_quad(dev, px + w - t, oy2,        t, H2, c, c, c, c);  // right
        // a styled tag so it's clear which box is which -- OUTSIDE the box (left of its top-left corner) :
        // a rounded pill with an accent rim + colour dot per box (gold / blue / teal) + a top sheen.
        const char* lbl = tier_ == 0 ? "PARTY" : tier_ == 1 ? "ALLIANCE 1" : "ALLIANCE 2";
        if (fHP->ready()) {
            const u32 accent = (tier_ == 0) ? 0xFFFFCC55u : (tier_ == 1) ? 0xFF6EA8FFu : 0xFF4FD8C0u;   // gold / blue / teal
            const float lsz = snap(11.0f) * S;
            const float lw  = fHP->measure(lbl, lsz);
            const float bh  = snap(21.0f), bw = lw + snap(30.0f), r = bh * 0.34f;
            const float bx  = px - bw - snap(8.0f), by = oy2, cy2 = by + bh * 0.5f;
            rrnd(dev, bx + snap(1.0f), by + snap(2.0f), bw, bh, r, 0x66000000);          // soft drop shadow
            rrnd(dev, bx - snap(1.0f), by - snap(1.0f), bw + snap(2.0f), bh + snap(2.0f), r + snap(1.0f), (accent & 0x00FFFFFFu) | 0xE6000000u);  // accent rim
            rrnd(dev, bx, by, bw, bh, r, 0xF0151D2C);                                     // dark base
            vgrad(dev, bx + r, by + snap(1.0f), bw - 2.0f * r, bh * 0.46f, 0x2EFFFFFF, 0x00FFFFFF);   // top sheen
            disc(dev, bx + snap(12.0f), cy2, snap(3.2f), accent);                         // accent dot
            disc(dev, bx + snap(12.0f), cy2, snap(1.4f), 0xFFFFFFFF);                     // dot highlight
            fHP->begin(dev); fHP->draw_lc(dev, bx + snap(21.0f), cy2, lbl, lsz, 0xFFF2F6FF, 0xFF000000, 1.2f);
        }
    }
}

// the floating Magic / Job-Ability / Weapon-Skill info box : RIGHT-aligned ABOVE the party,
// showing the highlighted action's name + MP cost (spell) / recast "Next" (spell, JA) / live TP (WS).
// Extracted from draw() -- a self-contained feature that doesn't touch the party rows.
void Party::draw_action_box(const Frame& f, float S, float px, float w, float oy, Font* fName, u32 nSTK, float nOWf) {
    u32 dev = f.dev;
    // the action-menu state is noisy frame-to-frame -> a confidence counter : several positive frames
    // to show (kills closed-menu spikes), and it lingers a few frames (kills open-menu flicker).
    const int rawMenu = (f.game ? f.game->menuType : 0);
    if (rawMenu) {
        menuType_ = rawMenu; menuSpell_ = f.game->menuAction; menuHold_ += 2; if (menuHold_ > 10) menuHold_ = 10;
        // LIVE vs GHOST. The examine memory holds the auto-selected item's id immediately on open (the line you
        // see highlighted), so a FRESH open shows it right away. It then proves itself : moving the cursor
        // updates the id (live) ; if you move but the id stays FROZEN, it's a ghost (no examinable item, e.g. a
        // no-magic job's magic menu) -> hide. (The box FRAME still shows either way -> empty Cost/Next box.)
        const int cur = (int)f.game->menuCursor;
        if (menuType_ == 1) {
            // MAGIC : the reverse (//aio menu dumps) proved there is NO per-row spell id in the menu
            // struct -- the static examine cache (0x634F28) is the only id, and it goes STALE (a no-magic job's
            // empty magic menu, and re-opening Trust on the same trust, both leave it frozen). The robust signal
            // is the menu's shared examine-DESCRIPTION object : it is empty (sentinel) for a ghost and populated
            // for a real spell/trust -- correct on OPEN and on RE-OPEN of the same item. (menuExamValid.)
            menuLive_ = f.game->menuExamValid;
        } else {
            // ABILITY / WS : the examine cache is reliable here (no shared-ghost problem) -- keep the simple
            // open-shows-auto-selected + frozen-on-move-hides heuristic.
            if (menuRawPrev_ == 0)                       menuLive_ = (menuSpell_ != 0);
            else if (f.game->examAbilRaw != prevAbRaw_)  menuLive_ = true;
            else if (cur != menuPrevCur_)                menuLive_ = false;
        }
        menuPrevCur_ = cur;
    } else {
        menuHold_ -= 1; if (menuHold_ < 0) menuHold_ = 0;
        menuLive_ = false;                                       // menu closed -> next open re-evaluates
    }
    menuRawPrev_ = rawMenu;
    prevAbRaw_ = (f.game ? f.game->examAbilRaw : 0);
    const char* nm = 0; char infobuf[16]; const char* info = 0; u32 infoCol = 0xFFFFFFFF;
    char info2buf[16]; const char* info2 = 0; u32 info2Col = 0xFFFFFFFF;       // optional 2nd line (spell recast)
    if (menuHold_ >= 4 && menuSpell_ && menuLive_) {   // CONTENT only for a live selection (ghost -> empty frame)
        if (menuType_ == 1) { const SpellRow* sp = spell_info(menuSpell_); if (sp) { nm = sp->en;     // Spell : MP cost on top, "Next m:ss" below while on recast
            if (sp->mp) { sprintf(infobuf, "Cost %u MP", sp->mp); info = infobuf; }                   // keep the MP cost (top line)
            unsigned rs = spell_recast_sec(sp->recast_id);                                            // per-id lookup in the live recast array
            sprintf(info2buf, "Next %u:%02u", rs / 60, rs % 60); info2 = info2buf;                    // always show "Next" (0:00 when ready)
            info2Col = rs ? 0xFFFFB454 : 0xFF8FA0B8; } }                                              // amber while on recast, dim grey-blue when ready
        else if (menuType_ == 2) { const AbilRow* a = abil_info(menuSpell_); if (a) { nm = a->en;             // Job Ability : "Next m:ss" (always shown, 0:00 when ready)
            unsigned rs = ability_recast_sec(a->recast_id);                                                  // per-id lookup in the live recast table
            sprintf(infobuf, "Next %u:%02u", rs / 60, rs % 60); info = infobuf;
            infoCol = rs ? 0xFFFFB454 : 0xFF8FA0B8; } }                                                      // amber while on recast, dim grey-blue when ready
        else if (menuType_ == 3) { const WSRow* ws = ws_info(menuSpell_); if (ws) { nm = ws->en;             // Weapon Skill : show live TP
            if (f.game) { int tp = f.game->me.tp; sprintf(infobuf, "TP %d", tp); info = infobuf; infoCol = (tp >= 1000) ? 0xFF7CFF8A : 0xFFB0B0B0; } } }  // green when usable (>=1000)
    }
    if (ui_config().editLayout || party_demo_level() > 0) {   // edit AND //aio demo : always show a demo Cost/Next box so its footprint is visible
        nm = "Protect V"; sprintf(infobuf, "Cost 24 MP"); info = infobuf; infoCol = 0xFFFFFFFF;
        sprintf(info2buf, "Next 0:00"); info2 = info2buf; info2Col = 0xFF8FA0B8;
    }
    if (!fName->ready()) return;
    // per-element typography : the Cost/Next box is the TE_COST element (font / size / outline / caps / colour),
    // falling back to the passed name font. Reassigning the local params routes every draw + measure below.
    char nmbuf[40];
    if (f.fonts) { const char* ovc = ui_font_face(ui_config().fontFace); Font* fc = te_font(f.fonts, TE_COST, ovc, nameFont_.c_str(), nameBold_); fc->ensure(f.dev); if (fc->ready()) fName = fc; }
    nOWf = nameStroke_ * S * ui_config().text[tcfg()][TE_COST].outline;
    if (nm && ui_config().text[tcfg()][TE_COST].upper) { int i = 0; for (; nm[i] && i < 38; ++i) { char c = nm[i]; nmbuf[i] = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c; } nmbuf[i] = 0; nm = nmbuf; }
    // The FRAME shows (empty when no live content) whenever : a magic/ability menu is OPEN (so you get an
    // empty Cost/Next box even on a no-magic job's menu), OR we're in an alliance (a permanent slot between
    // the party and the alliance). Solo + no menu : only while an action (nm) is up.
    const bool menuOpen = (menuHold_ >= 4 && menuType_ != 0);
    const bool inAlliance = party().alliance_count(1) > 0 || party().alliance_count(2) > 0
                            || ui_config().editLayout || party_demo_level() >= 2;
    if (!nm && !menuOpen && !inAlliance) return;
    const float fs = te_sz(TE_COST, nameSz_ * S);
    const float pdx = 7.0f * S, pdy = 4.0f * S, gapm = 9.0f * S, lineGap = 2.0f * S;
    const float topPad = snap(10.0f * S);                       // +10px empty at the TOP (text stays bottom-anchored)
    const float infoW  = info  ? fName->measure(info, fs)  : 0.0f;
    const float info2W = info2 ? fName->measure(info2, fs) : 0.0f;
    const float bh2 = snap(fs + 2.0f * pdy + fs + lineGap) + topPad;   // reserve 2 lines (matches costH)
    // DEFAULT (empty) size : as if showing a typical "name + Cost MP / Next" -> the box NEVER shrinks below it.
    float defW;
    {
        const float nameW  = fName->measure("Protect V", fs);
        const float infoX  = pdx + nameW + gapm;
        const float line1W = pdx + nameW + gapm + fName->measure("Cost 24 MP", fs) + pdx;
        const float line2W = infoX + fName->measure("Next 0:00", fs) + pdx;
        defW = (line1W > line2W ? line1W : line2W);
    }
    // width = max(action content, default) so it grows for long names but never goes below the empty size.
    float bw2 = defW;
    if (nm) {
        const float nameW  = fName->measure(nm, fs);
        const float infoX  = pdx + nameW + gapm;
        const float line1W = pdx + nameW + (info ? gapm + infoW : 0.0f) + pdx;
        const float line2W = info2 ? (infoX + info2W + pdx) : 0.0f;
        const float cw = (line1W > line2W ? line1W : line2W);
        if (cw > defW) bw2 = cw;
    }
    bw2 = snap(bw2);
    const float rightX = snap(px + w);                          // party right edge (constant)
    const float botY   = snap(oy);                              // party top edge (constant ; bottom stays flush/merged)
    const float bx2 = snap(rightX - bw2);                       // grow left (right edge pinned)
    const float by2 = snap(botY - bh2);                         // grow up   (bottom pinned to the party top)
    setup_color_state(dev);
    const bool costBorder = ui_config().borderCost;             // cost-box border on/off (config page) ; background stays either way
    const float costBa = ui_config().skinBoxAlpha < 0.0f ? 0.0f : (ui_config().skinBoxAlpha > 1.0f ? 1.0f : ui_config().skinBoxAlpha);   // same opacity as the party box
    const u32   costTint = mul_a(0xFFFFFFFFu, costBa);
    if (window_theme_is_proc(ui_config().skinTheme)) {          // procedural colour theme (open at the bottom -> merges with the party box)
        draw_proc_window(dev, ui_config().skinTheme, bx2, by2, bw2, bh2, costTint, true, costBorder, ui_config().skinLum, ui_config().skinHue);
    } else if (f.skin && f.skin->ready()) {                     // FFXI window skin, open at the bottom -> merges with the party's top edge
        draw_window(dev, *f.skin, bx2, by2, bw2, bh2, costTint, S, true, costBorder);
    } else {                                                    // fallback : built-in navy chrome
        if (costBorder)
            grad_quad(dev, bx2 - 1, by2 - 1, bw2 + 2, bh2 + 2, mul_a(0x6699BBFF, costBa), mul_a(0x6699BBFF, costBa), mul_a(0x6699BBFF, costBa), mul_a(0x6699BBFF, costBa));  // outer glow (border)
        vgrad(dev, bx2, by2, bw2, bh2, mul_a(0xF0232E54, costBa), mul_a(0xF0080B1A, costBa));          // dark fill (background)
        vgrad(dev, bx2, by2, bw2, 3 * S, mul_a(0x4DBFD8FF, costBa), mul_a(0x00BFD8FF, costBa));        // top sheen
    }
    if (nm) {                                                   // CONTENT : only while on a spell / job ability / weapon skill
        const float maxInfoW = info2W > infoW ? info2W : infoW;
        const float infoColX = snap(rightX - pdx - maxInfoW);
        const float nameX    = snap(bx2 + pdx);
        fName->begin(dev);
        const float ty  = by2 + topPad + pdy + fs * 0.5f;                          // text anchored to the BOTTOM region
        fName->draw_lc(dev, nameX, ty, nm, fs, te_col(TE_COST, 0xFFFFD970), nSTK, nOWf);             // action name (gold, or the element colour), left
        if (info) fName->draw_lc(dev, infoColX, ty, info, fs, te_col(TE_COST, infoCol), nSTK, nOWf); // "Cost XX MP" / live TP, right column
        if (info2) {                                                                                 // recast "Next", below, same column as Cost
            const float ty2 = ty + fs + lineGap;
            fName->draw_lc(dev, infoColX, ty2, info2, fs, te_col(TE_COST, info2Col), nSTK, nOWf);
        }
    }
}

} // namespace aio
