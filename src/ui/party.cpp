// party.cpp -- see party.h.
#include "ui/party.h"
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

namespace aio {

// Box geometry/fonts are LIVE-TUNABLE per box (Party members + configure()), not statics, so
// layout.json can override every size/font and hot-reload via //aio layout.

// round to the nearest whole pixel -> crisp 1px borders (see draw(): every coord is snapped so
// each row sits at an identical pixel phase).
static inline float snap(float v) { return (float)(int)(v + 0.5f); }

// ---- baked party (DEMO mode, when no live data ; matches design/src/panels/party.js) ----
struct Member { const char* name; const char* job; const char* sub; unsigned role; int maxHp; int maxMp; const char* cast; };
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
             const unsigned short* buffs = nullptr; int nbuff = 0; };   // status icons (left of the row) ; null/0 = none (uint16 ids: > 255 exist)

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
static const char* BUFF_ATLAS_PATH = "D:\\Windower Tetsouo\\plugins\\_aiohud_re\\assets\\buff_atlas.raw";
static const int BATLAS_W = 1024, BATLAS_H = 640, BCELL = 32, BCOLS = 32;
static const int BATLAS_ROWS = BATLAS_H / BCELL;        // 20 -> highest mappable id = BCOLS*BATLAS_ROWS - 1 (639)
// demo buff set (mirrors design/src/panels/partyCore.js BUFF_POOL) -> something to render in //aio demo.
static const unsigned short BUFF_POOL[] = { 40, 41, 43, 33, 251, 134, 13, 4, 5, 3, 30, 31, 32, 36, 37, 44, 45, 46, 39, 42, 0, 1, 2, 6, 7, 8, 9, 10, 11, 12, 14, 15 };
static const int BUFF_POOL_N = (int)(sizeof(BUFF_POOL) / sizeof(BUFF_POOL[0]));
static const u32 C_OFF = 0xFF6E7689;   // out-of-zone member (greyed)

// ---- colours ----
static const u32 C_INK = 0xFFF0FFFF, C_DIM = 0xFFB6BFD6, C_GOLD = 0xFFFFDC78, C_BAD = 0xFFFF4646, C_MP = 0xFF4F9DFF;
static u32 tp_color(int tp) { return tp >= 1000 ? 0xFFFF7AE8 : 0xFFE35AD6; }

// lerp two ARGB colours (t in 0..1).
static u32 lerp_color(u32 a, u32 b, float t) {
    if (t < 0) t = 0; if (t > 1) t = 1;
    int aa = (a >> 24) & 0xFF, ar = (a >> 16) & 0xFF, ag = (a >> 8) & 0xFF, ab = a & 0xFF;
    int ba = (b >> 24) & 0xFF, br = (b >> 16) & 0xFF, bg = (b >> 8) & 0xFF, bb = b & 0xFF;
    int r = ar + (int)((br - ar) * t), g = ag + (int)((bg - ag) * t);
    int bl = ab + (int)((bb - ab) * t), al = aa + (int)((ba - aa) * t);
    return ((u32)al << 24) | ((u32)r << 16) | ((u32)g << 8) | (u32)bl;
}
// HP colour as a CONTINUOUS gradient (green 100..75 -> yellow 50 -> orange 25 -> red 0).
static u32 hp_color(float p) {
    const u32 GRN = 0xFF6FDC74, YEL = 0xFFF2E173, ORG = 0xFFF6A862, RED = 0xFFFB5A5A;
    if (p >= 75.0f) return GRN;
    if (p >= 50.0f) return lerp_color(YEL, GRN, (p - 50.0f) / 25.0f);
    if (p >= 25.0f) return lerp_color(ORG, YEL, (p - 25.0f) / 25.0f);
    return lerp_color(RED, ORG, p / 25.0f);
}

static u32 scl(u32 c, float f) {
    int r = (int)(((c >> 16) & 0xFF) * f), g = (int)(((c >> 8) & 0xFF) * f), b = (int)((c & 0xFF) * f);
    if (r > 255) r = 255; if (g > 255) g = 255; if (b > 255) b = 255;
    return (c & 0xFF000000) | (r << 16) | (g << 8) | b;
}
static u32 lt(u32 c, float f) {
    int r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
    r += (int)((255 - r) * f); g += (int)((255 - g) * f); b += (int)((255 - b) * f);
    return (c & 0xFF000000) | (r << 16) | (g << 8) | b;
}
// modern "shine" : a LOCALISED soft highlight that sweeps left->right once per cycle (smoothstep
// travel + fade in/out + pause). Drawn as N strips with a RAISED-COSINE falloff -> a smooth,
// glassy glint (not a hard triangle). All clamped to [px, px+w] so it never spills outside.
static void shine_sweep(u32 dev, float px, float ry, float w, float h, float ph, u32 rgb, float peakA) {
    if (ph >= 0.60f) return;                                  // pause between sweeps
    const float s  = ph / 0.60f;                             // 0..1 across the sweep
    const float e  = s * s * (3.0f - 2.0f * s);              // smoothstep -> non-linear travel
    const float bw = w * 0.36f;                              // band half-width
    const float c  = px - bw + (w + 2.0f * bw) * e;          // band centre : off-left -> off-right
    const float A  = peakA * sinf(s * 3.14159265f);         // peak alpha, ramped in then out
    if (A <= 0.003f) return;
    const u32 rgbm = rgb & 0x00FFFFFF;
    const int N = 12;
    float pa = -1.0f, pxc = 0.0f;                            // previous strip alpha + x (carry the edge colour)
    for (int i = 0; i <= N; ++i) {
        float x = c - bw + (2.0f * bw) * ((float)i / N);
        if (x < px) x = px; if (x > px + w) x = px + w;
        float d = (x - c) / bw; if (d < 0) d = -d;           // 0 at centre .. 1 at edge
        float v = d >= 1.0f ? 0.0f : 0.5f + 0.5f * cosf(d * 3.14159265f);   // raised cosine
        int qa = (int)(A * v * 255.0f + 0.5f); if (qa > 255) qa = 255;
        u32 col = ((u32)qa << 24) | rgbm;
        if (pa >= 0.0f && x > pxc) {
            u32 pcol = ((u32)(int)pa << 24) | rgbm;
            grad_quad(dev, pxc, ry, x - pxc, h, pcol, col, pcol, col);
        }
        pa = (float)qa; pxc = x;
    }
}

static void setup_color_state(u32 dev) {
    dSetVS(dev, FVF_XYZRHW_DIFFUSE);
    dSetRS(dev, D3DRS_ZENABLE, 0);
    dSetRS(dev, D3DRS_CULLMODE, D3DCULL_NONE);
    dSetRS(dev, D3DRS_LIGHTING, 0);
    dSetRS(dev, D3DRS_ALPHATESTENABLE, 0);
    dSetRS(dev, D3DRS_FOGENABLE, 0);
    dSetRS(dev, D3DRS_SPECULARENABLE, 0);
    dSetRS(dev, D3DRS_ALPHABLENDENABLE, 1);
    dSetRS(dev, D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    dSetRS(dev, D3DRS_BLENDOP, D3DBLENDOP_ADD);
    dSetTex(dev, 0, 0);
    dSetTSS(dev, 0, D3DTSS_COLOROP,   D3DTOP_SELECTARG1);
    dSetTSS(dev, 0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    dSetTSS(dev, 0, D3DTSS_ALPHAOP,   D3DTOP_SELECTARG1);
    dSetTSS(dev, 0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    dSetTSS(dev, 1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    dSetTSS(dev, 1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
}
static void vgrad(u32 dev, float x, float y, float w, float h, u32 top, u32 bot) {
    grad_quad(dev, x, y, w, h, top, top, bot, bot);
}
// one HP/MP/TP gauge. `pct` is the (already-lerped) fill % ; `t` drives a subtle liquid
// shimmer ; `pulse` (0..1) brightens + adds an outer glow (used for TP >= 1000 = WS ready).
// `danger` (0..1) : critical HP -> the bar BLINKS in alarm-red, same glow+pulse principle
// as the TP WS-ready pulse but tinted red (red halo breathing + red flash over the liquid).
static void draw_gauge(u32 dev, float gx, float gy, float gw, float gh, float pct, u32 col, float t, float pulse, float danger = 0.0f) {
    if (pct < 0) pct = 0; if (pct > 100) pct = 100;
    if (pulse > 0.0f) {                                  // WS-ready glow breathing around the bar
        float ph = 0.5f + 0.5f * sinf(t * 7.5f);         // 0..1
        u32 g1 = (col & 0x00FFFFFF) | ((u32)((0.35f + 0.45f * ph) * pulse * 255) << 24);   // soft halo (kept tight so it doesn't reach the neighbour bar)
        grad_quad(dev, gx - 3, gy - 3, gw + 6, gh + 6, g1, g1, g1, g1);
        u32 g2 = (col & 0x00FFFFFF) | ((u32)((0.60f + 0.40f * ph) * pulse * 255) << 24);   // tight bright halo
        grad_quad(dev, gx - 1, gy - 2, gw + 2, gh + 4, g2, g2, g2, g2);
    }
    if (danger > 0.0f) {                                 // CRITICAL HP : red alarm halo breathing around the bar
        float dh = 0.5f + 0.5f * sinf(t * 7.5f);         // 0..1 (same rhythm as the WS pulse)
        u32 d1 = 0x00FF2A2A | ((u32)((0.35f + 0.50f * dh) * danger * 255) << 24);   // soft red halo
        grad_quad(dev, gx - 3, gy - 3, gw + 6, gh + 6, d1, d1, d1, d1);
        u32 d2 = 0x00FF2A2A | ((u32)((0.60f + 0.40f * dh) * danger * 255) << 24);   // tight bright red halo
        grad_quad(dev, gx - 1, gy - 2, gw + 2, gh + 4, d2, d2, d2, d2);
    }
    grad_quad(dev, gx, gy, gw, gh, 0xFF2A3354, 0xFF2A3354, 0xFF2A3354, 0xFF2A3354);   // thin frame
    vgrad(dev, gx + 1, gy + 1, gw - 2, gh - 2, 0xFF0A0E1C, 0xFF161D33);               // recessed bg
    float fw = (gw - 2) * pct / 100.0f, fh = gh - 2;
    if (fw >= 0.5f) {
        float b = 1.0f + 0.34f * pulse * sinf(t * 9.4f);          // pulse brightness
        u32 c = scl(col, b > 1.6f ? 1.6f : (b < 0.5f ? 0.5f : b));
        vgrad(dev, gx + 1, gy + 1,              fw, fh * 0.52f, lt(c, 0.16f), c);     // liquid top
        vgrad(dev, gx + 1, gy + 1 + fh * 0.52f, fw, fh * 0.48f, c, scl(c, 0.70f));    // liquid bottom
        vgrad(dev, gx + 1, gy + 1,              fw, fh * 0.42f, 0x66FFFFFF, 0x00FFFFFF); // top gloss highlight
        if (danger > 0.0f) {                                     // red wash OVER the liquid -> the fill visibly blinks red
            float dl = 0.5f + 0.5f * sinf(t * 7.5f);             // 0..1, in sync with the halo
            u32 dw = 0x00FF1E1E | ((u32)(dl * danger * 0.55f * 255) << 24);
            grad_quad(dev, gx + 1, gy + 1, fw, fh, dw, dw, dw, dw);
        }
    }
}

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
    r.name = pm.name; r.job = job_abbr(pm.mjob); r.sub = job_abbr(pm.sjob); r.role = job_role_color(pm.mjob);
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
    r.name = me.name; r.job = job_abbr(me.mjob); r.sub = job_abbr(me.sjob); r.role = job_role_color(me.mjob);
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
            for (int i = 0; i < 6; ++i) demo_row(i, &rows[i]);
            return 6;
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
    // Main party box : a demo command forces the baked roster; else live / cached fallback.
    if (demo >= 1) { for (int i = 0; i < 6; ++i) demo_row(i, &rows[i]); return 6; }

    // self comes from the snapshot: gs.me lives in the HUD's GameState -> it outlives this call,
    // so fill_self can safely store r.name = gs.me.name (the rows are rendered by the caller).
    const PlayerInfo& me = gs.me;
    if (gs.inGame) {                                                          // in game : self ALWAYS first
        fill_self(rows[n], me);                                              // row 0 = self (snapshot : instant + accurate)
        static unsigned short selfBuffs[32];                                 // stable storage : rows are rendered by the caller
        rows[n].nbuff = read_player_buffs(selfBuffs, 32);                    // self buffs (party-specific read, player+0x1C)
        rows[n].buffs = selfBuffs;
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
        if (gs.partyMenuSel >= 1 && gs.partyMenuSel <= n) rows[gs.partyMenuSel - 1].sel = true;
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
        r.role = job_role_color(pm.mjob);
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
    const bool edit = ui_config().editLayout;             // edit mode : everything full to show the MAX footprint
    int hp = edit ? 100 : dhp_[i]; if (hp < 0) hp = 0; if (hp > 100) hp = 100;
    int mp = edit ? 100 : dmp_[i]; if (mp < 0) mp = 0; if (mp > 100) mp = 100;
    int tp = edit ? 3000 : dtp_[i]; if (tp < 0) tp = 0; if (tp > 3000) tp = 3000;
    r.name = dm.name; r.job = dm.job; r.sub = dm.sub; r.role = dm.role;
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

// Box width in BASE px : ALWAYS auto-fit to the columns, so it adapts when you change the
// name/badge/bar font sizes (marks + badge + name col + 3 gauges + inter-column gaps).
// No font here, so the name column is approximated from nameSz_.
float Party::box_w_base() const {
    const float gauges  = 3.0f * gaugeW() + 2.0f * gaugeGap();   // HP/MP/TP
    const float nameCol = nameSz_ * 5.0f + 12.0f;               // ~name + gap before the HP gauge (approx)
    return 2.0f * padB() + 18.0f + marksW() + badgeW() + nameCol + gauges;   // 18 = the 4+5+5+4 column gaps
}

void Party::measure(float& w, float& h) const {
    const float S = scale_ * BOOST * ui_config().box[tier_].scale;   // per-box size (config Font Size / edit-mode wheel)
    w = box_w_base() * S;
    h = (rowPit() * 6 + 2 * padB()) * S;   // always sized for a full party of 6 (fixed box, solo or not)
}

static const char* ICON_PATH = "D:\\Windower Tetsouo\\plugins\\_aiohud_re\\assets\\hand_cursor.raw";

void Party::ensure(u32 dev) {
    if (!valid_ptr(dev)) return;
    if (!dot_tex_) dot_tex_ = make_dot(dev);
    if (!icon_tex_ && !icon_tried_) { icon_tex_ = load_raw_texture(dev, ICON_PATH, 128, 128); icon_tried_ = true; }
    if (!buff_tex_ && !buff_tried_) { buff_tex_ = load_raw_texture(dev, BUFF_ATLAS_PATH, BATLAS_W, BATLAS_H); buff_tried_ = true; }
}
void Party::on_device_lost() { dot_tex_ = 0; icon_tex_ = 0; icon_tried_ = false; buff_tex_ = 0; buff_tried_ = false; }   // forget (dead device), reload next ensure
void Party::dispose() { release_texture(dot_tex_); dot_tex_ = 0; release_texture(icon_tex_); icon_tex_ = 0; icon_tried_ = false; release_texture(buff_tex_); buff_tex_ = 0; buff_tried_ = false; }

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
                              float px, float oy, float pad, float rowpit, float rowh, float S) {
    if (!buffTex) return;
    dSetVS(dev, FVF_XYZRHW_DIFFUSE_TEX1);
    dSetRS(dev, D3DRS_ALPHABLENDENABLE, 1);
    dSetRS(dev, D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    dSetTex(dev, 0, buffTex);
    dSetTSS(dev, 0, D3DTSS_COLOROP, D3DTOP_MODULATE); dSetTSS(dev, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE); dSetTSS(dev, 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    dSetTSS(dev, 0, D3DTSS_ALPHAOP, D3DTOP_MODULATE); dSetTSS(dev, 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE); dSetTSS(dev, 0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    dSetTSS(dev, 0, D3DTSS_MINFILTER, D3DTEXF_LINEAR); dSetTSS(dev, 0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR); dSetTSS(dev, 0, D3DTSS_MIPFILTER, D3DTEXF_NONE);
    dSetTSS(dev, 0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP); dSetTSS(dev, 0, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
    const float bgap = snap(1.0f * S);                  // gap between icons
    const float bmar = snap(rowh * 1.18f);              // start the strip just LEFT of the cursor
    const float au = (float)BCELL / (float)BATLAS_W;    // one cell, in UV space
    const float av = (float)BCELL / (float)BATLAS_H;
    const int   BMAX = 20;                              // cap : at most 20 icons per member
    float bf = ui_config().buffScale; if (bf < 0.40f) bf = 0.40f; if (bf > 1.0f) bf = 1.0f;   // fraction of the row (capped at the row height)
    const float bs = snap(rowh * bf);                   // size FOLLOWS the box (rowh scales) ; capped at one row, centred below
    for (int i = 0; i < n; ++i) {
        const Row& r = rows[i];
        if (r.offzone || !r.buffs || r.nbuff <= 0) continue;
        const float ry = oy + pad + i * rowpit;
        const float y  = snap(ry + (rowh - bs) * 0.5f); // icon centred in the row
        const float xr = px - bmar;                     // right edge of the strip (just left of the cursor)
        const int   nb = r.nbuff < BMAX ? r.nbuff : BMAX;
        for (int j = 0; j < nb; ++j) {
            const float x = snap(xr - (float)(j + 1) * bs - (float)j * bgap);
            if (x < 1.0f) break;                        // ran off the left of the screen -> stop
            const int id = r.buffs[j];
            if (id < 0 || id >= BCOLS * BATLAS_ROWS) continue;   // id outside the atlas -> skip
            const float u0 = (float)(id % BCOLS) * au;
            const float v0 = (float)(id / BCOLS) * av;
            tquad(dev, x, y, bs, bs, u0, u0 + au, v0, v0 + av, 0xFFFFFFFF, 0xFFFFFFFF);
        }
    }
    dSetTex(dev, 0, 0);
}

void Party::draw(const Frame& f) {
    if (!visible_) return;
    u32 dev = f.dev;
    if (!valid_ptr(dev)) return;
    ensure(dev);                                  // lazily create the bullet dot texture

    Row rows[6];
    static const GameState NOGAME;                // fallback if drawn without a snapshot (demo still works)
    const int n = build_rows(rows, f.game ? *f.game : NOGAME);
    if (n <= 0) return;                           // nothing to show (e.g. an alliance box outside demo)

    const float S = scale_ * BOOST * ui_config().box[tier_].scale;   // per-box size (config Font Size / edit-mode wheel)
    // Snap ALL box geometry to whole pixels so EVERY row sits at an identical pixel phase ->
    // the 1px borders (badge / selection frame) are crisp on every row, never blurred or
    // "truncated" on some rows (which happens with a fractional row pitch).
    float px = snap(px_);
    float oy = snap(py_);
    const float pad = snap(padB() * S), rowh = snap(rowH() * S);
    float rowpit = snap(rowPit() * S);   // may be widened below to evenly distribute members across the box
    const float gw = snap(gaugeW() * S), gh = snap(gaugeH() * S), ggap = snap(gaugeGap() * S);
    const float bw = snap(badgeW() * S), bh = snap(badgeH() * S), mw = snap(marksW() * S);
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
    // alliances stack on the party's DEFAULT top (py_), NOT its dragged position -> moving the party
    // does NOT move the alliances (they stay independent ; drag them separately to reposition).
    if (tier_ == 0) { g_partyTopY = snap(py_); g_partyTopReady = true; }
    else if (!posSet && g_partyTopReady) {                 // alliances stack on the party ONLY when not user-placed
        const float fsC      = nameSz_ * S;
        const float costBoxH = 2.0f * fsC + 10.0f * S;     // reserve the 2-line cost/next box (its max height)
        const float allLift  = snap(24.0f * S);            // raise the alliance stack above the cost box
        oy = snap(g_partyTopY - costBoxH - (float)tier_ * H - (float)(tier_ - 1) * sepH - allLift);
    }

    // PARTY (tier 0) adapts its height to the live member count, BOTTOM-anchored : the box shrinks
    // from the TOP (members fill the bottom rows ; no empty space), and its bottom-right corner stays
    // exactly where it was placed/dragged. (Alliances + edit mode keep the full 6-row footprint.)
    if (tier_ == 0 && !ui_config().editLayout && n < 6) {
        // Party adapts to the live member count : exactly n rows, BOTTOM-anchored (shrinks from the
        // top, members at the top, no empty box below).
        const float Hn = rowpit * (float)n + 2.0f * pad;
        oy += (H - Hn);
        H = Hn;
    }

    // height of the Cost/Next box that floats ABOVE the party (tier 0 only) -> used for snap + clamp.
    const float costH = (tier_ == 0) ? snap(2.0f * nameSz_ * S + 10.0f * S) : 0.0f;

    // TOP mask band : extends the box UP to also cover the game's target box. Our member block already
    // covers the native party (our rows are bigger), so the band SHRINKS as members are added (linear,
    // MASK_B < 0). Calibrate MASK_A (solo) + MASK_B (per member) from 2 counts. 0 in edit mode.
    const float MASK_A = 2.0f, MASK_B = -0.24f, MASK_OFF = 2.0f;   // MASK_OFF = constant px tweak
    float maskBand = (tier_ == 0 && !ui_config().editLayout) ? rowpit * (MASK_A + MASK_B * (float)(n - 1)) + MASK_OFF * S : 0.0f;
    if (maskBand < 0.0f) maskBand = 0.0f;
    maskBand = snap(maskBand);
    // Put the mask BELOW the members : grow the box UP (top moves up = same coverage), keep the members
    // at the TOP (no empty above), the empty/mask sits below them, bottom stays anchored.
    if (tier_ == 0 && !ui_config().editLayout) { oy -= maskBand; H += maskBand; }

    // The chrome / cost box / edit overlay use the real box rect (boxOy, boxH). For the rows, EVENLY
    // DISTRIBUTE the members across the box : equal top/bottom margins AND equal gaps between members
    // (widen rowpit + shift the row origin so oy+pad+i*rowpit lands them with n+1 identical spaces).
    const float boxOy = oy, boxH = H;
    if (tier_ == 0 && !ui_config().editLayout && n > 0) {
        const float gap = (H - (float)n * rowh) / (float)(n + 1);
        if (gap > 0.0f) { oy += snap(gap) - pad; rowpit = snap(rowh + gap); }
    }

    // EDIT MODE : drag this box to reposition it live on the game (stores a fraction-of-screen pos).
    if (ui_config().editLayout && f.mouse && f.screenW > 0.0f) {
        const MouseState* m = f.mouse;
        const bool over = m->x >= px && m->x < px + w && m->y >= oy - costH && m->y < oy + H;   // include the cost box above
        if (m->down && g_dragTier < 0 && over) { g_dragTier = tier_; g_grabDX = m->x - px; g_grabDY = m->y - oy; }
        if (g_dragTier == tier_) {
            if (m->down) {
                float npx = m->x - g_grabDX, npy = m->y - g_grabDY;    // raw box top-left (px)
                // The party CLUSTER's top is the Cost/Next box top (above the party) -> snap on the
                // EFFECTIVE rect (party box + the cost box on top), not the bare party box.
                const float topOff = (tier_ == 0) ? costH : 0.0f;
                float ex = npx, ey = npy - topOff; const float ew = w, eh = H + topOff;
                const float SNAP = snap(10.0f);
                for (int b = 0; b < 3; ++b) {
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
                // keep the WHOLE box (effective rect) on screen
                if (ex > f.screenW - ew) ex = f.screenW - ew; if (ex < 0.0f) ex = 0.0f;
                if (ey > f.screenH - eh) ey = f.screenH - eh; if (ey < 0.0f) ey = 0.0f;
                npx = ex; npy = ey + topOff;                           // back to the party box top-left
                float nx = npx / f.screenW, ny = npy / f.screenH;
                ui_config().box[tier_].posSet = true; ui_config().box[tier_].x = nx; ui_config().box[tier_].y = ny;
                px = snap(nx * f.screenW); oy = snap(ny * f.screenH);   // immediate feedback this frame
            } else g_dragTier = -1;                                     // released
        }
        // wheel over this box -> resize it (0.5x .. 2.0x)
        if (over && ui_config().wheel != 0) {
            float s = ui_config().box[tier_].scale + (float)ui_config().wheel * 0.05f;
            ui_config().box[tier_].scale = s < 0.5f ? 0.5f : (s > 2.0f ? 2.0f : s);
            ui_config().wheel = 0;
        }
    }
    // store the CLUSTER rect (party box + the cost box above it for tier 0) so other boxes snap to
    // its real top, and the party itself snaps using the cost-box top.
    g_boxRect[tier_].x = px; g_boxRect[tier_].y = oy - costH; g_boxRect[tier_].w = w; g_boxRect[tier_].h = H + costH; g_boxRect[tier_].valid = true;
    const float cx  = px + pad + inset;
    const float gx0 = px + w - pad - inset - (3 * gw + 2 * ggap);
    // row-invariant column positions (only the row's Y varies) :
    const float ibx = snap(cx + mw + gap5);   // badge left  (snapped)
    const float bx  = cx + mw + gap5;          // badge anchor for text centring
    const float nx  = bx + bw + gap5;          // name left
    const float badgeYoff = (rowh - bh) * 0.5f;   // badge vertical inset within a row

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

    // ---------- graphics : box chrome ----------
    setup_color_state(dev);
    if (f.skin && f.skin->ready()) {                            // FFXI native window skin (9-slice)
        draw_window(dev, *f.skin, px, boxOy, w, boxH, 0xFFFFFFFF, S);
    } else {                                                    // fallback : the built-in navy chrome
        grad_quad(dev, px - 1, boxOy - 1, w + 2, boxH + 2, 0x6699BBFF, 0x6699BBFF, 0x6699BBFF, 0x6699BBFF);  // outer glow edge
        vgrad(dev, px, boxOy, w, boxH, 0xFF232E54, 0xFF080B1A);     // deeper opaque main fill
        vgrad(dev, px, boxOy, w, 3 * S, 0x4DBFD8FF, 0x00BFD8FF);    // top sheen
        vgrad(dev, px, boxOy + boxH - 4 * S, w, 4 * S, 0x00000000, 0x40000000);   // bottom vignette
    }

    // (the wide divider band between the two alliance boxes was removed -- they now stack flush.)

    // ---------- per-row : zebra background + badge + animated gauges ----------
    for (int i = 0; i < n; ++i) {
        const Row& r = rows[i];
        const float ry = oy + pad + i * rowpit;
        // FULL row background, alternating shades (Excel cells). DISABLED for now (kept for later) --
        // toggle kRowCells to re-enable. It was hiding the FFXI window-skin background.
        static const bool kRowCells = false;
        if (kRowCells) { const u32 cellc = (i & 1) ? 0x99101A2Eu : 0x99223256u; vgrad(dev, px + 1, ry, w - 2, rowpit, cellc, cellc); }
        if (r.offzone) continue;                       // out of zone : no badge, no vitals/gauges
        // badge : NOT affected by the selection zoom (only the name zooms) ; dark inner then 4 border edges.
        const float iby = snap(ry + badgeYoff);
        const float bcx = ibx + bw * 0.5f, bcy = iby + bh * 0.5f, pbw = bw, pbh = bh;
        const float pbx = snap(bcx - pbw * 0.5f), pby = snap(bcy - pbh * 0.5f);
        const u32 rb = (r.role & 0x00FFFFFF) | 0xD0000000;
        vgrad(dev, pbx, pby, pbw, pbh, 0xF0161D33, 0xF00A0E1C);
        grad_quad(dev, pbx,           pby,           pbw,  1.0f, rb, rb, rb, rb);   // top
        grad_quad(dev, pbx,           pby + pbh - 1, pbw,  1.0f, rb, rb, rb, rb);   // bottom
        grad_quad(dev, pbx,           pby,           1.0f, pbh,  rb, rb, rb, rb);   // left
        grad_quad(dev, pbx + pbw - 1, pby,           1.0f, pbh,  rb, rb, rb, rb);   // right

        const float gy = ry + (rowh - gh) * 0.5f;
        const RowAnim* a = ra[i];
        const float hpp = a ? a->hpp : (float)r.hpp, mpp = a ? a->mpp : (float)r.mpp, tpp = a ? a->tpp : 0.0f;
        const float wsReady = (r.tp >= 1000) ? 1.0f : 0.0f;   // TP >= 1000 -> pulse (WS ready)
        const float hpDanger = (r.hpp > 0 && r.hpp <= 25) ? 1.0f : 0.0f;   // HP <= 25% (alive) -> red danger blink
        const u32   gcol[3] = { hp_color(hpp), C_MP, tp_color(r.tp) };
        const float gpct[3] = { hpp, mpp, tpp };
        const float gpls[3] = { 0.0f, 0.0f, wsReady };
        const float gdng[3] = { hpDanger, 0.0f, 0.0f };
        for (int gi = 0; gi < 3; ++gi) {                 // bars are NOT affected by the selection zoom (geometry or brightness)
            const float gx = gx0 + gi * (gw + ggap);
            draw_gauge(dev, gx, gy, gw, gh, gpct[gi], gcol[gi], t, gpls[gi], gdng[gi]);
        }
    }

    // ---------- buffs : status icons LEFT of each party row (main box only -- alliance buffs aren't sent) ----------
    if (tier_ == 0) draw_member_buffs(dev, buff_tex_, rows, n, px, oy, pad, rowpit, rowh, S);

    // ---------- leader / QM markers : round dots, animated pop-in/out (scale + fade) ----------
    if (dot_tex_) {
        const u32 dcol[3] = { 0xFFFFFFFF, 0xFFFFEF3F, 0xFF42D98A };   // alliance=white, party=canary yellow, QM=green
        const float sz = 5.5f * S, gap = -0.5f * S, pitch = sz + gap;   // smaller pips (distance text below gets the room)
        dSetVS(dev, FVF_XYZRHW_DIFFUSE_TEX1);
        dSetRS(dev, D3DRS_ALPHABLENDENABLE, 1);
        dSetRS(dev, D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        dSetTex(dev, 0, dot_tex_);
        dSetTSS(dev, 0, D3DTSS_COLOROP, D3DTOP_MODULATE); dSetTSS(dev, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE); dSetTSS(dev, 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
        dSetTSS(dev, 0, D3DTSS_ALPHAOP, D3DTOP_MODULATE); dSetTSS(dev, 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE); dSetTSS(dev, 0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
        dSetTSS(dev, 0, D3DTSS_MINFILTER, D3DTEXF_LINEAR); dSetTSS(dev, 0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR); dSetTSS(dev, 0, D3DTSS_MIPFILTER, D3DTEXF_LINEAR);
        dSetTSS(dev, 0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP); dSetTSS(dev, 0, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
        // FIXED 3-column layout so each marker keeps its own slot: alliance=LEFT, party=MIDDLE,
        // QM=RIGHT (dcol order matches). An absent marker just leaves its column empty.
        const float markCx = cx + mw * 0.5f;
        const float slotsW = 2.0f * pitch + sz;               // width of the 3 columns
        const float x0     = markCx - slotsW * 0.5f;          // left (alliance) column x
        for (int i = 0; i < n; ++i) {
            const RowAnim* a = ra[i]; if (!a) continue;
            const float ry = oy + pad + i * rowpit, cy = ry + snap(2.0f * S) + sz * 0.5f;   // pips at the TOP of the row
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
    if (icon_tex_ && (selA_ > 0.02f || subA_ > 0.02f)) {
        dSetVS(dev, FVF_XYZRHW_DIFFUSE_TEX1);
        dSetRS(dev, D3DRS_ALPHABLENDENABLE, 1);
        dSetRS(dev, D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        dSetRS(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        dSetTex(dev, 0, icon_tex_);
        dSetTSS(dev, 0, D3DTSS_COLOROP, D3DTOP_MODULATE); dSetTSS(dev, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE); dSetTSS(dev, 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
        dSetTSS(dev, 0, D3DTSS_ALPHAOP, D3DTOP_MODULATE); dSetTSS(dev, 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE); dSetTSS(dev, 0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
        dSetTSS(dev, 0, D3DTSS_MINFILTER, D3DTEXF_LINEAR); dSetTSS(dev, 0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR); dSetTSS(dev, 0, D3DTSS_MIPFILTER, D3DTEXF_LINEAR);
        dSetTSS(dev, 0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP); dSetTSS(dev, 0, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
        const float ih = rowh * 1.30f, iw = ih;                 // square icon, ~1.3 row tall
        const float es  = 1.0f;                                 // cursor keeps a constant size on target change (no pop)
        const float bob = 1.5f * S * gPulse;                     // cursor bob on the SHARED rhythm (already delayed + phase-reset)
        const float cx  = px - 0.0f * S - iw * 0.5f + bob;      // icon CENTRE x (finger points at the box) + bob
        // the cursor points at the SUB-target when one is in use, otherwise the main target
        const bool  onSub = (subA_ > 0.02f);
        const float cy  = (onSub ? subY_ : selY_) + rowh * 0.5f;            // icon CENTRE y
        const u32 hrgb  = onSub ? 0x002E9CFF : 0x00FFFFFF;                  // strong blue when pointing the sub, white for the main
        const u32 tint  = ((u32)(0xFF * (onSub ? subA_ : selA_)) << 24) | hrgb;
        glow_quad(dev, snap(cx - iw * es * 0.5f), snap(cy - ih * es * 0.5f), iw * es, ih * es, tint);
        dSetTex(dev, 0, 0);
    }

    // ---------- text : each element (name / bars / badge) has its OWN face+weight atlas ----
    if (!f.fonts) return;
    const char* ov = ui_font_face(ui_config().fontFace);   // "" = keep the per-element layout faces
    Font* fName  = f.fonts->get(ov[0] ? ov : nameFont_.c_str(),  nameBold_  ? 700 : 0);
    Font* fBar   = f.fonts->get(ov[0] ? ov : barFont_.c_str(),   barBold_   ? 700 : 0);
    Font* fBadge = f.fonts->get(ov[0] ? ov : badgeFont_.c_str(), badgeBold_ ? 700 : 0);
    fName->ensure(dev); fBar->ensure(dev); fBadge->ensure(dev);   // build lazily (same frame)
    const u32 nSTK = nameStroke_  > 0 ? 0xFF000000u : 0u; const float nOWf = nameStroke_  * S;
    const u32 vSTK = barStroke_   > 0 ? 0xFF000000u : 0u; const float vOWf = barStroke_   * S;
    const u32 bSTK = badgeStroke_ > 0 ? 0xFF000000u : 0u; const float bOWf = badgeStroke_ * S;
    char buf[24];
    for (int i = 0; i < n; ++i) {
        const Row& r = rows[i];
        bool offz = r.offzone;
        bool dead = !offz && r.hpp <= 0;
        bool hasCast = !dead && !offz && r.cast;
        const float ry = oy + pad + i * rowpit;
        const float by = snap(ry + badgeYoff);         // match the snapped badge rect
        // names never scale (no zoom on target) -> stable layout, room for the cast line below
        const float es = 1.0f;
        const float bcx = bx + bw * 0.5f, bcy = by + bh * 0.5f;               // badge centre (scale pivot)

        // distance (yalms) UNDER the leader/QM pips, centered in the marks column, format 00.00.
        if (r.dist >= 0.0f && fBadge->ready()) {
            float d = r.dist; if (d > 99.99f) d = 99.99f;
            char db[12]; sprintf(db, "%05.2f", d);
            float dsz = badgeSz_ * S * 1.20f;                                 // as big as fits the marks column width
            const float dw = fBadge->measure(db, dsz);
            if (dw > mw * 0.98f && dw > 0.0f) dsz *= (mw * 0.98f) / dw;
            const u32 dcol = r.dist >= kCastRange ? 0xFFE76C6C :              // red  : out of cast range
                             r.dist >= kCastSafe  ? 0xFFE7C95A :              // yellow : marginal (still casts)
                                                    0xFF8FC6FF;               // blue : comfortably in range
            fBadge->begin(dev);
            fBadge->draw_cc(dev, cx + mw * 0.5f, ry + rowh - dsz * 0.62f, db, dsz, dcol, bSTK, bOWf);
        }

        if (!offz && fBadge->ready()) {                // out of zone : no job badge text
            fBadge->begin(dev);
            bool hasSub = r.sub && r.sub[0];
            // with a subjob : main on top (0.34) + sub below (0.70). Without (e.g. SPC trusts,
            // no subjob) : centre the lone main job vertically. Offsets scale around the badge centre.
            const float mfrac = hasSub ? 0.34f : 0.52f;
            fBadge->draw_cc(dev, bcx, bcy + (mfrac - 0.5f) * bh, r.job, badgeSz_ * S, r.role, bSTK, bOWf);
            if (hasSub) fBadge->draw_cc(dev, bcx, bcy + 0.20f * bh, r.sub, subSz() * S, C_DIM, bSTK, bOWf);
        }
        if (!fName->ready()) continue;
        fName->begin(dev);
        // name : truncate to the width available before the HP gauge (leaves a real gap),
        // measured in the ACTUAL name font -> never touches the bar, no fixed char cap.
        char nm[28]; int nl = 0; for (; nl < 20 && r.name && r.name[nl]; ++nl) nm[nl] = r.name[nl];
        nm[nl] = 0;
        const float nmax = gx0 - nx - 6.0f * S;                  // 6px gap before the gauges
        const float nsz = nameSz_ * S * es;                      // measure at the ZOOMED size so a zoomed long name never overflows onto HP
        if (nmax > 0 && fName->measure(nm, nsz) > nmax) {
            while (nl > 0) {                                     // shrink + "..." until it fits
                nm[nl] = '.'; nm[nl + 1] = '.'; nm[nl + 2] = '.'; nm[nl + 3] = 0;
                if (fName->measure(nm, nsz) <= nmax) break;
                --nl;
            }
        }
        // name sits at a FIXED height (always a bit high) -> it never jumps when a cast starts,
        // and there is always room below for the cast / zone line.
        fName->draw_lc(dev, nx, ry + rowh * (offz ? 0.30f : 0.34f), nm, nsz, offz ? C_OFF : (dead ? C_BAD : C_INK), nSTK, nOWf);
        if (hasCast) {
            const float ca = r.castAlpha < 0.0f ? 0.0f : (r.castAlpha > 1.0f ? 1.0f : r.castAlpha);   // pop-in / depop fade
            const float cp = 0.5f + 0.5f * sinf(t * 5.0f);                       // OPACITY pulse during the cast
            const float af = ca * (0.55f + 0.45f * cp);                          // fade * pulse -> breathing opacity
            const u32 crgb = 0x00FFD970;                                         // light gold (constant colour)
            const u32 ccol = ((u32)(0xFF * af) << 24) | crgb;
            const u32 cstk = nSTK ? ((u32)(0xFF * af) << 24) : 0u;              // stroke breathes with the text
            fName->draw_lc(dev, nx, ry + rowh * 0.80f, r.cast, castSz() * S, ccol, cstk, nOWf);
        }
        if (offz) {                                    // no vitals -> show the zone the member is in
            const char* zn = zone_name(r.zone);
            char zbuf[40]; if (zn && zn[0]) { sprintf(zbuf, "%.36s", zn); } else { strcpy(zbuf, "out of zone"); }
            fName->draw_lc(dev, nx, ry + rowh * 0.72f, zbuf, castSz() * S, C_OFF, nSTK, nOWf);  // zone under the (greyed) name
            continue;
        }

        int tp = r.tp < 0 ? 0 : (r.tp > 3000 ? 3000 : r.tp);
        int vals[3] = { dead ? 0 : r.hpVal, r.mpVal, tp };
        const float gy = ry + (rowh - gh) * 0.5f;
        if (!fBar->ready()) continue;
        fBar->begin(dev);
        for (int g = 0; g < 3; ++g) {
            float gx = gx0 + g * (gw + ggap);
            sprintf(buf, "%d", vals[g]);
            fBar->draw_cc(dev, gx + gw * 0.5f, gy + gh * 0.5f, buf, barSz_ * S, 0xFFFFFFFF, vSTK, vOWf);   // bar values NOT zoomed
        }
    }

    // Row overlays (selection / sub-target / out-of-range veil) stay INSIDE the skin border : inset
    // by the box padding so they sit on the background, never overlapping the window frame.
    const float sx = px + pad, sw = w - 2.0f * pad;

    // ---------- sub-target bar : ocean blue (the game's <st> colour), on top, below the main selection ----------
    if (subA_ > 0.02f) {
        setup_color_state(dev);
        const float ry = snap(subY_);
        const float a  = subA_;                                   // steady opacity
        // strong ocean-blue fill
        const u32 fillT = ((u32)(0x80 * a) << 24) | 0x00159CFF;   // vivid ocean blue
        const u32 fillB = ((u32)(0x34 * a) << 24) | 0x00064FB0;   // deeper ocean toward the bottom
        vgrad(dev, sx, ry, sw, rowh, fillT, fillB);
        // MODERN shine : the sub IS the cursor row whenever it shows, so it always sweeps (a bit stronger)
        shine_sweep(dev, sx, ry, sw, rowh, gSweep, 0x00BFEFFF, 0.80f * a);
    }

    // ---------- selection bar : drawn LAST, on top of everything. Faux "loupe / glass" look ----------
    // convex glass = bright sheen strongest at the HORIZONTAL centre + darkened curved rims at the
    // sides. Kept faint so the player's bar stays readable ; the name is already magnified (es).
    if (selA_ > 0.02f) {
        setup_color_state(dev);                            // back to colour-only quads after the font passes
        const float ry = snap(selY_);
        const float a  = selA_;                                // steady opacity

        // 1) base tinted gold fill (faint -> readable)
        const u32 fillT = ((u32)(0x3C * a) << 24) | 0x00FFE08A;
        const u32 fillB = ((u32)(0x16 * a) << 24) | 0x00FFC850;
        vgrad(dev, sx, ry, sw, rowh, fillT, fillB);

        // 2) MODERN shine : only when the CURSOR is on the main row (i.e. no sub-target active)
        if (subA_ <= 0.02f) shine_sweep(dev, sx, ry, sw, rowh, gSweep, 0x00FFFFFF, 0.65f * a);

        // 3) curved rims : darken the far left/right edges -> the lens bulge (refraction pinch)
        const float rw = sw * 0.085f;                      // narrow rims so the bars stay clear
        const u32 rO = ((u32)(0x3E * a) << 24);            // black at the very edge
        const u32 rI = 0x00000000;                         // transparent inward
        grad_quad(dev, sx,           ry, rw, rowh, rO, rI, rO, rI);   // left rim
        grad_quad(dev, sx + sw - rw, ry, rw, rowh, rI, rO, rI, rO);   // right rim
    }

    // ---------- dim members beyond cast range : a translucent veil over their row, so it reads
    //            clearly as "out of casting range" (heals/buffs won't reach). ----------
    {
        setup_color_state(dev);
        const u32 veil = 0x8E101620;   // ~55% dark blue-grey
        for (int i = 0; i < n; ++i) if (rows[i].outRange) {
            const float ry = oy + pad + (float)i * rowpit;
            grad_quad(dev, sx, ry, sw, rowpit, veil, veil, veil, veil);
        }
    }

    // ---------- floating spell / JA / WS info box (main box only -- alliances would stack it) ----------
    if (tier_ == 0) draw_action_box(f, S, px, w, boxOy, fName, nSTK, nOWf);

    // ---------- EDIT MODE : draggable outline around this box (brighter when hovered / grabbed) ----------
    if (ui_config().editLayout) {
        setup_color_state(dev);
        const float oy2 = oy - costH, H2 = H + costH;            // cluster rect (party + cost box above)
        const bool drag  = (g_dragTier == tier_);
        const bool hover = f.mouse && f.mouse->x >= px && f.mouse->x < px + w && f.mouse->y >= oy2 && f.mouse->y < oy2 + H2;
        const u32  c = drag ? 0xFFFFD24A : (hover ? 0xFF7EC0FF : 0xAAFFFFFF);   // gold grabbed / blue hover / white idle
        const float t = snap(2.0f);
        grad_quad(dev, px,        oy2,         w, t, c, c, c, c);   // top
        grad_quad(dev, px,        oy2 + H2 - t, w, t, c, c, c, c);  // bottom
        grad_quad(dev, px,        oy2,         t, H2, c, c, c, c);  // left
        grad_quad(dev, px + w - t, oy2,        t, H2, c, c, c, c);  // right
        // a label so it's clear which box is which
        const char* lbl = tier_ == 0 ? "PARTY" : tier_ == 1 ? "ALLIANCE 1" : "ALLIANCE 2";
        if (fBar->ready()) { fBar->begin(dev); fBar->draw_lc(dev, px + snap(6.0f), oy2 + snap(9.0f), lbl, snap(11.0f) * S, c, 0xFF000000, 1.2f); }
    }
}

// the floating Magic / Job-Ability / Weapon-Skill info box : RIGHT-aligned ABOVE the party,
// showing the highlighted action's name + MP cost (spell) / recast "Next" (spell, JA) / live TP (WS).
// Extracted from draw() -- a self-contained feature that doesn't touch the party rows.
void Party::draw_action_box(const Frame& f, float S, float px, float w, float oy, Font* fName, u32 nSTK, float nOWf) {
    u32 dev = f.dev;
    // the action-menu state is noisy frame-to-frame -> a confidence counter : several positive frames
    // to show (kills closed-menu spikes), and it lingers a few frames (kills open-menu flicker).
    if (f.game && f.game->menuType) { menuType_ = f.game->menuType; menuSpell_ = f.game->menuAction; menuHold_ += 2; if (menuHold_ > 10) menuHold_ = 10; }
    else { menuHold_ -= 1; if (menuHold_ < 0) menuHold_ = 0; }
    const char* nm = 0; char infobuf[16]; const char* info = 0; u32 infoCol = 0xFFFFFFFF;
    char info2buf[16]; const char* info2 = 0; u32 info2Col = 0xFFFFFFFF;       // optional 2nd line (spell recast)
    if (menuHold_ >= 4 && menuSpell_) {
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
    if (ui_config().editLayout) {   // edit mode : always show a demo Cost/Next box so its footprint is visible
        nm = "Protect V"; sprintf(infobuf, "Cost 24 MP"); info = infobuf; infoCol = 0xFFFFFFFF;
        sprintf(info2buf, "Next 0:00"); info2 = info2buf; info2Col = 0xFF8FA0B8;
    }
    if (nm && fName->ready()) {
        const float fs = nameSz_ * S;
        const float pdx = 7.0f * S, pdy = 4.0f * S, gapm = 9.0f * S, lineGap = 2.0f * S;
        const float nameW = fName->measure(nm, fs);
        const float infoW = info ? fName->measure(info, fs) : 0.0f;
        const float info2W = info2 ? fName->measure(info2, fs) : 0.0f;
        const float infoX  = pdx + nameW + gapm;                            // left x of the info column (Cost / Next stack)
        const float line1W = pdx + nameW + (info ? gapm + infoW : 0.0f) + pdx;
        const float line2W = info2 ? (infoX + info2W + pdx) : 0.0f;          // Next sits in the same column as Cost
        // Anchor the BOTTOM-RIGHT corner to the party's top-right and grow LEFT + UP only. Snap the
        // fixed edges AND the size to integers so every edge is pixel-aligned -> the right edge and
        // bottom never move (not even sub-pixel) as the name length changes.
        const float bw2 = snap(line1W > line2W ? line1W : line2W);
        const float bh2 = snap(fs + 2.0f * pdy + (info2 ? fs + lineGap : 0.0f));   // grow for the recast line
        const float rightX = snap(px + w);                 // party right edge (constant)
        const float botY   = snap(oy);                     // party top edge (constant)
        const float bx2 = rightX - bw2;                    // grow left (right edge pinned)
        const float by2 = botY - bh2;                      // grow up   (bottom pinned)
        // info column (Cost / Next / TP) is pinned to the RIGHT edge, NOT placed after the name ->
        // it never moves when the spell/JA/WS name changes length. Snapped = pixel-stable.
        const float maxInfoW = info2W > infoW ? info2W : infoW;
        const float infoColX = snap(rightX - pdx - maxInfoW);
        const float nameX    = snap(bx2 + pdx);
        setup_color_state(dev);
        if (f.skin && f.skin->ready()) {                                 // FFXI window skin, open at the bottom -> merges with the party's top edge
            draw_window(dev, *f.skin, bx2, by2, bw2, bh2, 0xFFFFFFFF, S, true);
        } else {                                                         // fallback : built-in navy chrome
            grad_quad(dev, bx2 - 1, by2 - 1, bw2 + 2, bh2 + 2, 0x6699BBFF, 0x6699BBFF, 0x6699BBFF, 0x6699BBFF);  // outer glow
            vgrad(dev, bx2, by2, bw2, bh2, 0xF0232E54, 0xF0080B1A);          // dark fill
            vgrad(dev, bx2, by2, bw2, 3 * S, 0x4DBFD8FF, 0x00BFD8FF);        // top sheen
        }
        fName->begin(dev);
        const float ty  = by2 + pdy + fs * 0.5f;                                   // top line center (== box center when single-line)
        fName->draw_lc(dev, nameX, ty, nm, fs, 0xFFFFD970, nSTK, nOWf);                              // action name (gold), left
        if (info) fName->draw_lc(dev, infoColX, ty, info, fs, infoCol, nSTK, nOWf);                  // "Cost XX MP" / live TP, right column
        if (info2) {                                                                                 // recast "Next", below, same column as Cost
            const float ty2 = ty + fs + lineGap;
            fName->draw_lc(dev, infoColX, ty2, info2, fs, info2Col, nSTK, nOWf);
        }
    }
}

} // namespace aio
